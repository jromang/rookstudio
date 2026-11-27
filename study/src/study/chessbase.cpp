// Ported from cbvault 0.1.6, MIT License, Copyright (c) 2026 cbvault Contributors: the text of names and comments
// (codepage.rs, cbh/bytes.rs, pgn/comments.rs: here a comment's spaces are run together, and a figurine is its
// letter), the tags (pgn/mod.rs), the older format's annotations placed by the order its moves are stored. cbvault's
// readers of the older format are themselves ported from oschess-cb-bridge (MIT License, Copyright (c) 2026 the
// oschess-cb-bridge contributors).
//
// A database opened from any of its files, its records read one at a time into games: the format's reader gives a
// record's tags, its tree and its annotations; the annotations are placed on the moves they belong to, those this
// reader keeps written into the tree and the others counted.

#include "chessbase.hpp"
#include <cctype>

#include <algorithm>
#include <cstdio>
#include <map>
#include <stdexcept>

#include <tour/util.hpp>

#include "chessbase_detail.hpp"
#include "mapped_file.hpp"
#include "pgn_reader.hpp"
#include "pgn_writer.hpp"
#include "scan_board.hpp"
#include "signature.hpp"

namespace fcg::study {

namespace {

std::string extensionOf(const std::filesystem::path& file) {
    auto ext = file.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

}  // namespace

bool isChessBase(const std::filesystem::path& file) {
    const auto ext = extensionOf(file);
    for (const char* known :
         {".cbh", ".cbg", ".cba", ".cbp", ".cbt", ".cbc", ".cbs", ".cbe", ".cbj", ".2cbh", ".2cbg", ".2cba", ".2lid"})
        if (ext == known) return true;
    return isChessBaseArchive(file);
}

bool isChessBaseArchive(const std::filesystem::path& file) {
    const auto ext = extensionOf(file);
    return ext == ".cbv" || ext == ".2cbv" || isEncipheredArchive(file);
}

bool isEncipheredArchive(const std::filesystem::path& file) {
    const auto ext = extensionOf(file);
    return ext == ".cbz" || ext == ".2cbz";
}

bool archivePasswordFits(const std::filesystem::path& file, const std::string& password) {
    return chessbase::passwordFits(file, password);
}

namespace chessbase {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------------------------------------------

void Source::outside(uint64_t offset, std::size_t size) const {
    throw Damage(name() + ": " + std::to_string(size) + " bytes wanted at byte " + std::to_string(offset) +
                 ", past its end (" + std::to_string(this->size()) + " bytes)");
}

namespace {

class MappedSource final : public Source {
   public:
    explicit MappedSource(const fs::path& file) : name_(file.string()), file_(file) {}
    [[nodiscard]] const std::string& name() const noexcept override { return name_; }
    [[nodiscard]] uint64_t size() const noexcept override { return file_.size(); }
    std::span<const uint8_t> read(uint64_t offset, std::size_t size) override {
        if (offset > file_.size() || size > file_.size() - offset) outside(offset, size);
        return {file_.data() + offset, size};
    }

   private:
    std::string name_;
    MappedFile file_;
};

class MemorySource final : public Source {
   public:
    MemorySource(std::string name, std::vector<uint8_t> bytes) : name_(std::move(name)), bytes_(std::move(bytes)) {}
    [[nodiscard]] const std::string& name() const noexcept override { return name_; }
    [[nodiscard]] uint64_t size() const noexcept override { return bytes_.size(); }
    std::span<const uint8_t> read(uint64_t offset, std::size_t size) override {
        if (offset > bytes_.size() || size > bytes_.size() - offset) outside(offset, size);
        return {bytes_.data() + offset, size};
    }

   private:
    std::string name_;
    std::vector<uint8_t> bytes_;
};

class FolderMembers final : public Members {
   public:
    explicit FolderMembers(fs::path stem) : stem_(std::move(stem)) {}
    [[nodiscard]] std::string label() const override { return stem_.string(); }
    [[nodiscard]] bool has(std::string_view extension) const override {
        std::error_code ec;
        return fs::is_regular_file(sibling(stem_, extension), ec);
    }
    std::unique_ptr<Source> open(std::string_view extension, bool) override {
        if (!has(extension)) return nullptr;
        return mappedSource(sibling(stem_, extension));
    }

   private:
    fs::path stem_;
};

}  // namespace

std::unique_ptr<Source> mappedSource(const fs::path& file) { return std::make_unique<MappedSource>(file); }

std::unique_ptr<Source> memorySource(std::string name, std::vector<uint8_t> bytes) {
    return std::make_unique<MemorySource>(std::move(name), std::move(bytes));
}

std::unique_ptr<Members> folderMembers(const fs::path& stem) { return std::make_unique<FolderMembers>(stem); }

fs::path sibling(const fs::path& stem, std::string_view extension) {
    fs::path exact = stem;
    exact += std::string(extension);
    std::error_code ec;
    if (fs::exists(exact, ec)) return exact;
    const auto want = lower(exact.filename().string());
    const auto dir  = exact.has_parent_path() ? exact.parent_path() : fs::path(".");
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (lower(it->path().filename().string()) == want) return it->path();
    return exact;
}

// ---------------------------------------------------------------------------------------------------------------
// Texts
// ---------------------------------------------------------------------------------------------------------------

namespace {

// How much of `s` is valid UTF-8, and whether what follows is a character cut by the end.
std::pair<std::size_t, bool> utf8Prefix(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        int extra    = 0;
        if (c < 0x80) {
            ++i;
            continue;
        }
        if ((c & 0xE0) == 0xC0 && c >= 0xC2)
            extra = 1;
        else if ((c & 0xF0) == 0xE0)
            extra = 2;
        else if ((c & 0xF8) == 0xF0 && c <= 0xF4)
            extra = 3;
        else
            return {i, false};
        for (int k = 1; k <= extra; ++k) {
            if (i + static_cast<std::size_t>(k) >= s.size()) return {i, true};
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return {i, false};
        }
        i += static_cast<std::size_t>(extra) + 1;
    }
    return {i, false};
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Windows-1252's characters 0x80 to 0x9f, U+FFFD where it has none.
constexpr uint16_t CP1252[32] = {0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};

// The code points of a text: UTF-8 where it is, each other byte a Windows-1252 character. `code_page`: some was not.
std::vector<uint32_t> codePoints(std::string_view s, bool& code_page) {
    std::vector<uint32_t> out;
    code_page = false;
    for (std::size_t i = 0; i < s.size();) {
        const std::size_t valid = utf8Prefix(s.substr(i)).first;
        // The valid part, decoded.
        for (std::size_t k = i; k < i + valid;) {
            const auto c = static_cast<unsigned char>(s[k]);
            if (c < 0x80) {
                out.push_back(c);
                ++k;
            } else if (c < 0xE0) {
                out.push_back((c & 0x1Fu) << 6 | (static_cast<unsigned char>(s[k + 1]) & 0x3Fu));
                k += 2;
            } else if (c < 0xF0) {
                out.push_back((c & 0x0Fu) << 12 | (static_cast<unsigned char>(s[k + 1]) & 0x3Fu) << 6 |
                              (static_cast<unsigned char>(s[k + 2]) & 0x3Fu));
                k += 3;
            } else {
                out.push_back((c & 0x07u) << 18 | (static_cast<unsigned char>(s[k + 1]) & 0x3Fu) << 12 |
                              (static_cast<unsigned char>(s[k + 2]) & 0x3Fu) << 6 |
                              (static_cast<unsigned char>(s[k + 3]) & 0x3Fu));
                k += 4;
            }
        }
        i += valid;
        if (i < s.size()) {
            const auto b = static_cast<unsigned char>(s[i++]);
            out.push_back(b >= 0x80 && b < 0xA0 ? CP1252[b - 0x80] : b);
            code_page = true;
        }
    }
    return out;
}

void appendCodePoint(std::string& out, uint32_t cp) {
    if (cp < 0x10000) {
        appendUtf8(out, cp);
        return;
    }
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
}

}  // namespace

std::string nameText(std::span<const uint8_t> field) {
    const auto end = std::find(field.begin(), field.end(), uint8_t{0});
    const std::string_view s(reinterpret_cast<const char*>(field.data()),
                             static_cast<std::size_t>(end - field.begin()));
    const auto [valid, cut] = utf8Prefix(s);
    if (valid == s.size()) return std::string(s);
    // A UTF-8 name cut at the field's end: the cut character left out. A name in a code page is never taken for one.
    if (cut && std::any_of(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(valid),
                           [](char c) { return static_cast<unsigned char>(c) >= 0x80; }))
        return std::string(s.substr(0, valid));
    std::string out;
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x80)
            out += ch;
        else if (c < 0xA0)
            appendUtf8(out, CP1252[c - 0x80] == 0xFFFD ? c : CP1252[c - 0x80]);
        else
            appendUtf8(out, c);
    }
    return out;
}

std::string commentText(std::span<const uint8_t> bytes, bool own_language) {
    const auto end = std::find(bytes.begin(), bytes.end(), uint8_t{0});
    const std::string_view s(reinterpret_cast<const char*>(bytes.data()),
                             static_cast<std::size_t>(end - bytes.begin()));
    bool code_page   = false;
    const auto chars = codePoints(s, code_page);
    const bool bare  = chars.size() == 1;  // a text that is the byte 4 alone stays as it is
    std::string out;
    bool space = false;
    for (const uint32_t c : chars) {
        // The diagram: the byte 9e of a text in a code page, U+E005, or in a text of a language of its own the byte 4.
        if ((code_page && c == 0x017E) || c == 0xE005 || (own_language && !bare && c == 4)) {
            if (space && !out.empty()) out += ' ';
            space = false;
            out += "[#]";
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0x0B || c == 0x0C || c == 0xA0) {
            space = true;
            continue;
        }
        if (space && !out.empty()) out += ' ';
        space = false;
        if (c == '{') {
            out += '(';
        } else if (c == '}') {
            out += ')';
        } else if (code_page && c >= 0xA2 && c <= 0xA7) {
            out += "KQNBRP"[c - 0xA2];  // the figurines of a text in a code page
        } else {
            appendCodePoint(out, c);
        }
    }
    return out;
}

NoteKind noteKind(uint16_t code) noexcept {
    switch (code) {
        case 0x02:
            return NoteKind::Text;
        case 0x82:
            return NoteKind::TextBefore;
        case 0x03:
            return NoteKind::Symbols;
        case 0x04:
            return NoteKind::Squares;
        case 0x05:
            return NoteKind::Arrows;
        case 0x07:
            return NoteKind::TimeSpent;
        case 0x09:
            return NoteKind::Training;
        case 0x0A:
            return NoteKind::Sound;
        case 0x0B:
            return NoteKind::Picture;
        case 0x13:
            return NoteKind::Quotation;
        case 0x14:
            return NoteKind::PawnStructure;
        case 0x15:
            return NoteKind::PiecePath;
        case 0x16:
            return NoteKind::ClockWhite;
        case 0x17:
            return NoteKind::ClockBlack;
        case 0x18:
            return NoteKind::Critical;
        case 0x19:
            return NoteKind::Correspondence;
        case 0x1C:
            return NoteKind::WebLink;
        case 0x20:
        case 0x25:
            return NoteKind::Video;
        case 0x21:
            return NoteKind::Evaluation;
        case 0x22:
            return NoteKind::Medal;
        case 0x23:
            return NoteKind::VariationColour;
        case 0x24:
            return NoteKind::TimeControl;
        case 0x26:
            return NoteKind::Evaluations;
        default:
            return NoteKind::Other;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Moves and the tree
// ---------------------------------------------------------------------------------------------------------------

std::array<char, 64> chess960Placement(int index) {
    std::array<char, 64> board{};
    if (index < 0 || index >= 960) return board;
    static constexpr std::array<std::pair<int, int>, 10> knights = {
        {{0, 1}, {0, 2}, {0, 3}, {0, 4}, {1, 2}, {1, 3}, {1, 4}, {2, 3}, {2, 4}, {3, 4}}};
    std::array<char, 8> files;
    files.fill('R');
    const int light = 2 * (index % 4) + 1, dark = 2 * ((index / 4) % 4);
    files[light] = files[dark] = 'B';
    std::vector<int> free;
    for (int f = 0; f < 8; ++f)
        if (f != light && f != dark) free.push_back(f);
    const auto take = [&](std::size_t i) {
        const int f = free[i];
        free.erase(free.begin() + static_cast<std::ptrdiff_t>(i));
        return f;
    };
    files[take(static_cast<std::size_t>((index / 16) % 6))] = 'Q';
    const auto [k1, k2]                                     = knights[static_cast<std::size_t>(index / 96)];
    files[take(static_cast<std::size_t>(k1))]               = 'N';
    files[take(static_cast<std::size_t>(k2 - 1))]           = 'N';
    files[take(0)]                                          = 'R';
    files[take(0)]                                          = 'K';
    files[take(0)]                                          = 'R';
    for (int f = 0; f < 8; ++f) {
        board[f]      = files[f];
        board[56 + f] = static_cast<char>(files[f] + 32);
        board[8 + f]  = 'P';
        board[48 + f] = 'p';
    }
    return board;
}

std::optional<chess::Move> legalMove(const chess::Board& board, int from, int to, int promotion, int castle) {
    chess::Movelist list;
    chess::movegen::legalmoves(list, board);
    for (const auto& m : list) {
        const bool castling = m.typeOf() == chess::Move::CASTLING;
        if (castle) {
            if (castling && (m.to().file() > m.from().file()) == (castle == 1)) return m;
            continue;
        }
        if (castling || m.from().index() != from || m.to().index() != to) continue;
        const int kind = m.typeOf() == chess::Move::PROMOTION ? static_cast<int>(m.promotionType()) : -1;
        if (kind == promotion) return m;
    }
    return std::nullopt;
}

void TreeBuilder::reset(const chess::Board& start) {
    tree = GameTree{};
    stored.clear();
    pending_.clear();
    board_  = start;
    before_ = start;
    line_   = -1;
    base_   = 0;
}

std::vector<ReplayMove>& TreeBuilder::moves(int line) {
    return line < 0 ? tree.main.moves : tree.variations[static_cast<std::size_t>(line)].moves;
}

void TreeBuilder::play(chess::Move move) {
    if (stored.size() >= MAX_MOVES) throw Damage("more moves than a game holds");
    ReplayMove m;
    m.move          = move;
    m.mover         = board_.sideToMove();
    const bool null = move == chess::Move(chess::Move::NULL_MOVE);
    // A side in check cannot pass: the position after would leave its king to be taken.
    if (null && board_.inCheck()) throw Damage("a null move in check");
    m.san           = null ? "--" : chess::uci::moveToSan(board_, move);
    before_         = board_;
    if (null)
        board_.makeNullMove();
    else
        board_.makeMove<true>(move);
    auto& list = moves(line_);
    stored.emplace_back(line_, static_cast<int>(list.size()));
    list.push_back(std::move(m));
}

void TreeBuilder::branchLast() {
    const auto& list = moves(line_);
    if (list.empty()) throw Damage("an alternative to no move");
    int line      = line_;
    const int ply = base_ + static_cast<int>(list.size()) - 1;
    // An alternative to a variation's first move is one more variation of the line that variation leaves.
    if (line >= 0 && list.size() == 1) line = tree.variations[static_cast<std::size_t>(line)].parent;
    pending_.push_back(Pending{line, ply, before_});
}

bool TreeBuilder::endLine() {
    if (pending_.empty()) return false;
    auto p = std::move(pending_.back());
    pending_.pop_back();
    Variation v;
    v.parent = p.line;
    v.from   = p.ply;
    tree.variations.push_back(std::move(v));
    line_   = static_cast<int>(tree.variations.size()) - 1;
    base_   = p.ply;
    board_  = std::move(p.before);
    before_ = board_;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Tags
// ---------------------------------------------------------------------------------------------------------------

std::string dateText(uint32_t packed) {
    std::string out = "????.??.??";
    const auto put  = [&](std::size_t at, std::size_t width, uint32_t v) {
        if (v == 0) return;
        for (std::size_t i = width; i-- > 0; v /= 10) out[at + i] = static_cast<char>('0' + v % 10);
    };
    put(0, 4, (packed >> 9) & 0xFFF);
    put(5, 2, (packed >> 5) & 15);
    put(8, 2, packed & 31);
    return out;
}

std::string roundText(int round, int subround) {
    if (round <= 0) return subround > 0 ? "?." + std::to_string(subround) : "?";
    return subround > 0 ? std::to_string(round) + "." + std::to_string(subround) : std::to_string(round);
}

std::string ecoText(uint32_t field) {
    if (field < 128 || field > 64127) return {};
    const uint32_t code = field / 128 - 1;
    return {static_cast<char>('A' + code / 100), static_cast<char>('0' + code / 10 % 10),
            static_cast<char>('0' + code % 10)};
}

std::string resultText(uint32_t field) {
    switch (field) {
        case 0:
        case 4:
            return "0-1";
        case 1:
        case 5:
            return "1/2-1/2";
        case 2:
        case 6:
            return "1-0";
        default:
            return "*";
    }
}

std::string playerText(const std::string& last, const std::string& first) {
    if (first.empty()) return last;
    std::string out = last + ", " + first;
    // A first name of one or two capitals or digits is initials, and takes a period ("Delaire, H.").
    if (first.size() <= 2 && std::all_of(first.begin(), first.end(),
                                         [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }))
        out += '.';
    return out;
}

void addTag(RawGame& game, std::string name, std::string value) {
    static constexpr std::string_view ROSTER[] = {"Event", "Site", "Date", "Round", "White", "Black", "Result"};
    if (value.empty()) {
        if (std::find(std::begin(ROSTER), std::end(ROSTER), name) == std::end(ROSTER)) return;
        value = "?";
    }
    game.tags.emplace_back(std::move(name), std::move(value));
}

// ---------------------------------------------------------------------------------------------------------------
// The annotations, placed on their moves
// ---------------------------------------------------------------------------------------------------------------

namespace {

using Place = std::pair<int, int>;  // a line (-1 the game) and a move's index in it

std::vector<ReplayMove>& movesOf(GameTree& tree, int line) {
    return line < 0 ? tree.main.moves : tree.variations[static_cast<std::size_t>(line)].moves;
}

int baseOf(const GameTree& tree, int line) {
    return line < 0 ? 0 : tree.variations[static_cast<std::size_t>(line)].from;
}

// The variations replacing each move, by (line, ply), in the order they were made.
std::map<Place, std::vector<int>> children(const GameTree& tree) {
    std::map<Place, std::vector<int>> out;
    for (std::size_t v = 0; v < tree.variations.size(); ++v)
        out[{tree.variations[v].parent, tree.variations[v].from}].push_back(static_cast<int>(v));
    return out;
}

// The moves in the order PGN writes them, and the variations in the order PGN opens them.
void pgnOrder(const GameTree& tree, std::vector<Place>* moves, std::vector<int>* variations) {
    const auto kids = children(tree);
    struct Frame {
        int line;
        std::size_t index = 0, child = 0;
        bool written = false;
    };
    std::vector<Frame> stack{{-1}};
    while (!stack.empty()) {
        const std::size_t top = stack.size() - 1;
        const auto& list =
            stack[top].line < 0 ? tree.main.moves : tree.variations[static_cast<std::size_t>(stack[top].line)].moves;
        if (stack[top].index >= list.size()) {
            stack.pop_back();
            continue;
        }
        if (!stack[top].written) {
            if (moves) moves->emplace_back(stack[top].line, static_cast<int>(stack[top].index));
            stack[top].written = true;
            stack[top].child   = 0;
        }
        const auto it =
            kids.find({stack[top].line, baseOf(tree, stack[top].line) + static_cast<int>(stack[top].index)});
        if (it != kids.end() && stack[top].child < it->second.size()) {
            const int v = it->second[stack[top].child++];
            if (variations) variations->push_back(v);
            stack.push_back(Frame{v});
            continue;
        }
        ++stack[top].index;
        stack[top].written = false;
    }
}

// The variations renumbered in the order PGN opens them: the order a PGN reader gives them.
void renumber(GameTree& tree) {
    std::vector<int> order;
    pgnOrder(tree, nullptr, &order);
    std::vector<int> index(tree.variations.size(), -1);
    for (std::size_t i = 0; i < order.size(); ++i) index[static_cast<std::size_t>(order[i])] = static_cast<int>(i);
    std::vector<Variation> out;
    out.reserve(order.size());
    for (const int v : order) {
        auto var   = std::move(tree.variations[static_cast<std::size_t>(v)]);
        var.parent = var.parent < 0 ? -1 : index[static_cast<std::size_t>(var.parent)];
        out.push_back(std::move(var));
    }
    tree.variations = std::move(out);
}

void join(std::string& to, const std::string& text) {
    if (text.empty()) return;
    if (!to.empty()) to += ' ';
    to += text;
}

std::string marksCommand(const char* name, const std::vector<BoardMark>& marks, bool arrows) {
    std::string out = std::string("[%") + name + " ";
    for (std::size_t i = 0; i < marks.size(); ++i) {
        if (i) out += ',';
        out += marks[i].colour;
        out += static_cast<char>('a' + marks[i].from % 8);
        out += static_cast<char>('1' + marks[i].from / 8);
        if (arrows) {
            out += static_cast<char>('a' + marks[i].to % 8);
            out += static_cast<char>('1' + marks[i].to / 8);
        }
    }
    return out + "]";
}

fastchess::Score scoreOf(int64_t value, bool mate, chess::Color mover) {
    const int64_t sign = mover == chess::Color::WHITE ? 1 : -1;
    return fastchess::Score{mate ? fastchess::ScoreType::MATE : fastchess::ScoreType::CP, sign * value};
}

// A clock's time as [%clk] writes it: h:mm:ss, with the milliseconds when there are some.
std::string clockText(int64_t ms) {
    const int64_t s = ms / 1000;
    char out[48];
    std::snprintf(out, sizeof out, "%lld:%02lld:%02lld", static_cast<long long>(s / 3600),
                  static_cast<long long>(s / 60 % 60), static_cast<long long>(s % 60));
    std::string text = out;
    if (ms % 1000 != 0) {
        std::snprintf(out, sizeof out, ".%03lld", static_cast<long long>(ms % 1000));
        text += out;
    }
    return text;
}

void setTag(GameTree& tree, const std::string& name, const std::string& value) {
    for (auto& [k, v] : tree.main.headers)
        if (k == name) {
            v = value;
            return;
        }
    tree.main.headers.emplace_back(name, value);
}

// The report's count of a kind of annotation left out; none for a kind that is kept.
uint64_t* leftOut(ChessBaseReport& report, NoteKind kind) {
    switch (kind) {
        case NoteKind::Training:
            return &report.training;
        case NoteKind::Sound:
            return &report.sounds;
        case NoteKind::Picture:
            return &report.pictures;
        case NoteKind::Quotation:
            return &report.quotations;
        case NoteKind::PawnStructure:
            return &report.pawn_structures;
        case NoteKind::PiecePath:
            return &report.piece_paths;
        case NoteKind::Correspondence:
            return &report.correspondence;
        case NoteKind::WebLink:
            return &report.web_links;
        case NoteKind::Video:
            return &report.videos;
        case NoteKind::VariationColour:
            return &report.variation_colours;
        case NoteKind::Other:
            return &report.other;
        default:
            return nullptr;
    }
}

// The notes of a game written into its tree, those left out counted.
void place(RawGame& raw, ChessBaseReport& report) {
    auto& tree       = raw.builder.tree;
    const auto order = raw.order == Order::Stored ? raw.builder.stored : [&] {
        std::vector<Place> moves;
        pgnOrder(tree, &moves, nullptr);
        return moves;
    }();
    struct Texts {
        std::vector<std::pair<uint16_t, std::string>> after, before;
    };
    std::map<Place, Texts> texts;
    Texts game_texts;
    std::vector<std::string> game_marks;
    uint32_t medals = 0;
    bool any_medal  = false;
    std::vector<std::pair<int16_t, int>> profile;

    for (auto& n : raw.notes) {
        std::optional<Place> at;
        if (n.position >= 0) {
            if (static_cast<std::size_t>(n.position) < order.size())
                at = order[static_cast<std::size_t>(n.position)];
            else if (!tree.main.moves.empty())  // past the last move: on the main line's last move
                at = Place{-1, static_cast<int>(tree.main.moves.size()) - 1};
        }
        ReplayMove* move = at ? &movesOf(tree, at->first)[static_cast<std::size_t>(at->second)] : nullptr;
        // What is left out, counted by its kind whether its data could be read or not.
        if (uint64_t* count = leftOut(report, n.kind)) {
            ++*count;
            continue;
        }
        if (!n.decoded) {
            ++report.undecoded;
            continue;
        }
        switch (n.kind) {
            case NoteKind::Text:
            case NoteKind::TextBefore: {
                auto& list = at ? (n.kind == NoteKind::Text ? texts[*at].after : texts[*at].before)
                                : (n.kind == NoteKind::Text ? game_texts.after : game_texts.before);
                list.emplace_back(n.language, std::move(n.text));
                break;
            }
            case NoteKind::Critical:
                (at ? texts[*at].after : game_texts.after).emplace_back(ANY_LANGUAGE, "Critical position");
                break;
            case NoteKind::Symbols:
                if (!move) {
                    ++report.undecoded;
                    break;
                }
                for (const uint8_t nag : n.nags)
                    if (nag) move->notes.nags.push_back(nag);
                break;
            case NoteKind::Squares:
            case NoteKind::Arrows: {
                const bool arrows = n.kind == NoteKind::Arrows;
                if (move) {
                    auto& list = arrows ? move->notes.arrows : move->notes.squares;
                    list.insert(list.end(), n.marks.begin(), n.marks.end());
                } else if (!n.marks.empty()) {
                    game_marks.push_back(marksCommand(arrows ? "cal" : "csl", n.marks, arrows));
                }
                break;
            }
            case NoteKind::TimeSpent:
                if (move)
                    move->notes.emt_ms = n.value;
                else
                    ++report.undecoded;
                break;
            case NoteKind::ClockWhite:
            case NoteKind::ClockBlack:
                if (move && (move->mover == chess::Color::WHITE) == (n.kind == NoteKind::ClockWhite))
                    move->clock_ms = n.value;
                else if (n.position < 0)  // the game's, not a move's: a tag
                    setTag(tree, n.kind == NoteKind::ClockWhite ? "WhiteClock" : "BlackClock", clockText(n.value));
                else
                    ++report.undecoded;
                break;
            case NoteKind::Evaluation:
                if (!move) {
                    ++report.undecoded;
                    break;
                }
                move->score          = scoreOf(n.value, n.mate, move->mover);
                move->depth          = n.depth;
                move->notes.eval_tag = true;
                break;
            case NoteKind::Evaluations:
                profile = n.profile;
                break;
            case NoteKind::Medal:
                medals |= static_cast<uint32_t>(n.value);
                any_medal = true;
                break;
            case NoteKind::TimeControl:
                setTag(tree, "TimeControl", n.text);
                break;
            default:
                ++report.other;
                break;
        }
    }

    // The main line's evaluations, on the moves that have none of their own: entry k is the position after move k.
    for (std::size_t k = 1; k < profile.size() && k <= tree.main.moves.size(); ++k) {
        auto& move          = tree.main.moves[k - 1];
        const int16_t value = profile[k].first;
        if (move.score || value == INT16_MIN) continue;
        const bool mate     = value > 20000 || value < -20000;
        move.score          = scoreOf(mate ? (value > 0 ? 30000 - value : -30000 - value) : value, mate, move.mover);
        move.depth          = profile[k].second;
        move.notes.eval_tag = true;
    }
    if (any_medal) {
        std::string current;
        for (const auto& [k, v] : tree.main.headers)
            if (k == "Medals") current = v;
        setTag(tree, "Medals", std::to_string(medals | (current.empty() ? 0 : std::stoul(current))));
    }

    // A move's texts: English first, the others after.
    const auto joined = [](std::vector<std::pair<uint16_t, std::string>>& list) {
        std::stable_partition(list.begin(), list.end(), [](const auto& t) { return t.first == ENGLISH; });
        std::string out;
        for (const auto& t : list) join(out, t.second);
        return out;
    };
    std::string pre;
    for (const auto& m : game_marks) join(pre, m);
    join(pre, joined(game_texts.before));
    join(pre, joined(game_texts.after));
    for (auto& [where, t] : texts) {
        auto& list = movesOf(tree, where.first);
        join(list[static_cast<std::size_t>(where.second)].notes.after, joined(t.after));
    }
    // A text before a move: the game's comment before its first move, a variation's first move's own, else the
    // comment after the move before it.
    for (auto& [where, t] : texts) {
        const auto text = joined(t.before);
        if (text.empty()) continue;
        auto& list = movesOf(tree, where.first);
        if (where.second > 0)
            join(list[static_cast<std::size_t>(where.second) - 1].notes.after, text);
        else if (where.first >= 0)
            join(list.front().notes.before, text);
        else
            join(pre, text);
    }
    tree.main.pre_comment = pre;
}

}  // namespace

}  // namespace chessbase

// ---------------------------------------------------------------------------------------------------------------
// The database
// ---------------------------------------------------------------------------------------------------------------

namespace {

namespace fs = std::filesystem;

constexpr std::size_t MAX_PROBLEMS = 100;

bool memberExtension(const std::string& ext) {
    static constexpr std::string_view KNOWN[] = {".cbh",  ".cbg",  ".cba",  ".cbp",  ".cbt",  ".cbc",   ".cbs",
                                                 ".cbe",  ".cbj",  ".cbl",  ".cbtt", ".cbm",  ".flags", ".cit",
                                                 ".cib",  ".cit2", ".cib2", ".cbb",  ".cbgi", ".ini",   ".2cbh",
                                                 ".2cbg", ".2cba", ".2lid", ".2lgd", ".2lcd"};
    return std::find(std::begin(KNOWN), std::end(KNOWN), ext) != std::end(KNOWN);
}

}  // namespace

struct ChessBaseDatabase::Impl {
    std::unique_ptr<chessbase::FormatReader> reader;
    ChessBaseReport report;

    void problem(std::string text) {
        if (report.problems.size() < MAX_PROBLEMS) report.problems.push_back(std::move(text));
    }
};

ChessBaseDatabase::ChessBaseDatabase(const fs::path& file, const ChessBaseOptions& options)
    : impl_(std::make_unique<Impl>()) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) throw std::runtime_error(file.string() + ": no such file");
    const auto ext = lower(file.extension().string());
    try {
        if (isChessBaseArchive(file)) {
            auto members       = chessbase::archiveMembers(file, options.password, options.unpack_whole);
            const bool classic = members->has(".cbh");
            impl_->reader =
                classic ? chessbase::cbhReader(std::move(members)) : chessbase::twoCbhReader(std::move(members));
            return;
        }
        fs::path stem = file;
        if (memberExtension(ext)) stem.replace_extension();
        auto members = chessbase::folderMembers(stem);
        if (members->has(".cbh"))
            impl_->reader = chessbase::cbhReader(std::move(members));
        else if (members->has(".2cbh"))
            impl_->reader = chessbase::twoCbhReader(std::move(members));
        else
            throw std::runtime_error(file.string() + ": not a ChessBase database (no .cbh or .2cbh beside it)");
    } catch (const chessbase::Damage& e) {
        throw std::runtime_error(e.what());
    }
}

ChessBaseDatabase::~ChessBaseDatabase()                                       = default;
ChessBaseDatabase::ChessBaseDatabase(ChessBaseDatabase&&) noexcept            = default;
ChessBaseDatabase& ChessBaseDatabase::operator=(ChessBaseDatabase&&) noexcept = default;

ChessBaseFormat ChessBaseDatabase::format() const noexcept { return impl_->reader->format(); }
uint32_t ChessBaseDatabase::records() const noexcept { return impl_->reader->records(); }
const ChessBaseReport& ChessBaseDatabase::report() const noexcept { return impl_->report; }
uint64_t ChessBaseDatabase::headerBytes() const noexcept { return impl_->reader->headerBytes(); }
uint64_t ChessBaseDatabase::headerEnd(uint32_t record) const noexcept { return impl_->reader->headerEnd(record); }

std::optional<ChessBaseGame> ChessBaseDatabase::read(uint32_t record) {
    if (record == 0 || record > records())
        throw std::out_of_range("record " + std::to_string(record) + " of a database of " + std::to_string(records()));
    auto& report = impl_->report;
    ++report.records;
    chessbase::RawGame raw;
    chessbase::RecordKind kind{};
    try {
        kind = impl_->reader->read(record, raw);
    } catch (const chessbase::Damage& e) {
        ++report.unreadable;
        impl_->problem("record " + std::to_string(record) + ": " + e.what());
        return std::nullopt;
    }
    switch (kind) {
        case chessbase::RecordKind::Text:
            ++report.texts;
            return std::nullopt;
        case chessbase::RecordKind::Deleted:
            ++report.deleted;
            return std::nullopt;
        case chessbase::RecordKind::Unknown:
            ++report.unknown;
            return std::nullopt;
        case chessbase::RecordKind::Game:
            break;
    }
    raw.builder.tree.main.headers = std::move(raw.tags);
    chessbase::place(raw, report);
    ChessBaseGame game;
    game.tree = std::move(raw.builder.tree);
    chessbase::renumber(game.tree);
    auto& main     = game.tree.main;
    main.start_fen = raw.fen.empty() ? std::string(chess::constants::STARTPOS) : raw.fen;
    if (!raw.fen.empty()) {
        main.headers.emplace_back("SetUp", "1");
        main.headers.emplace_back("FEN", raw.fen);
    }
    main.result   = main.header("Result");
    game.chess960 = raw.chess960;
    game.record   = record;
    game.damaged  = !raw.problem.empty();
    game.problem  = std::move(raw.problem);
    ++report.games;
    if (game.damaged) {
        ++report.damaged;
        impl_->problem("record " + std::to_string(record) + ": " + game.problem);
    }
    return game;
}

void ChessBaseDatabase::forEach(const std::function<bool(ChessBaseGame&&)>& sink, uint32_t first,
                                const std::atomic<bool>* cancel) {
    for (uint32_t record = std::max<uint32_t>(first, 1); record <= records(); ++record) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return;
        if (auto game = read(record); game && !sink(std::move(*game))) return;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// As a database takes it
// ---------------------------------------------------------------------------------------------------------------

ImportedGame importedGame(const ChessBaseGame& game) {
    ImportedGame out;
    const auto& main    = game.tree.main;
    out.tags            = main.headers;
    out.chess960        = game.chess960;
    const bool standard = main.start_fen.empty() || main.start_fen == chess::constants::STARTPOS;
    out.start_fen       = standard ? std::string() : main.start_fen;
    out.result    = main.result == "1-0" ? '1' : main.result == "0-1" ? '0' : main.result == "1/2-1/2" ? '=' : '*';
    out.damaged   = game.damaged;
    out.annotated = !game.tree.variations.empty() || !main.pre_comment.empty();

    ScanBoard board;
    if (!board.setFen(standard ? std::string_view(chess::constants::STARTPOS) : std::string_view(main.start_fen),
                      game.chess960)) {
        out.damaged = true;
    } else {
        SignatureMaker signature(board);
        bool any_clock = false, any_eval = false;
        std::vector<int32_t> clocks;
        std::vector<int16_t> evals;
        for (const auto& m : main.moves) {
            const auto coded = board.parseSan(m.san);
            if (!coded) {
                out.damaged = true;
                break;
            }
            const auto& n = m.notes;
            if (!n.before.empty() || !n.after.empty() || !n.nags.empty() || !n.arrows.empty() || !n.squares.empty() ||
                n.emt_ms || !n.commands.empty())
                out.annotated = true;
            appendCode(out.moves, board, *coded);
            signature.before(board, *coded, out.plies);
            board.play(*coded);
            ++out.plies;
            clocks.push_back(m.clock_ms ? static_cast<int32_t>(std::clamp<int64_t>(*m.clock_ms, 0, INT32_MAX)) : -1);
            any_clock    = any_clock || m.clock_ms.has_value();
            int16_t eval = ImportedGame::NO_EVAL;
            if (m.score && m.notes.eval_tag) {
                const int64_t white = m.mover == chess::Color::WHITE ? m.score->value : -m.score->value;
                if (m.score->type == fastchess::ScoreType::MATE)
                    eval = static_cast<int16_t>(white > 0 ? 30000 - std::min<int64_t>(white, 1000)
                                                          : -30000 + std::min<int64_t>(-white, 1000));
                else
                    eval = static_cast<int16_t>(std::clamp<int64_t>(white, -29000, 29000));
                any_eval = true;
            }
            evals.push_back(eval);
        }
        if (any_clock) out.clocks = std::move(clocks);
        if (any_eval) out.evals = std::move(evals);
        out.signature = signature.finish(board);
    }
    if (out.annotated || out.damaged) out.text = writePgnGame(game.tree, PgnWriteOptions{game.chess960, {}});
    return out;
}

ImportReport importChessBase(const fs::path& file, const ImportOptions& options, ImportProgress& progress,
                             const std::atomic<bool>& cancel,
                             const std::function<void(std::vector<ImportedGame>&&)>& sink, ChessBaseReport* left_out,
                             const ChessBaseOptions& chessbase) {
    ImportReport report;
    std::optional<ChessBaseDatabase> db;
    try {
        db.emplace(file, chessbase);
    } catch (const std::exception& e) {
        report.error = e.what();
        return report;
    }
    progress.total_bytes    = db->headerBytes();
    const std::size_t batch = std::max<std::size_t>(1, options.piece_bytes / 1024);
    std::vector<ImportedGame> games;
    const auto give = [&] {
        if (games.empty()) return;
        report.games += games.size();
        for (const auto& g : games) report.damaged += g.damaged;
        progress.games.store(report.games, std::memory_order_relaxed);
        sink(std::move(games));
        games.clear();
    };
    for (uint32_t record = 1; record <= db->records(); ++record) {
        if (games.empty() && cancel.load(std::memory_order_relaxed)) break;  // between batches
        if (auto game = db->read(record)) games.push_back(importedGame(*game));
        progress.bytes.store(db->headerEnd(record), std::memory_order_relaxed);
        if (games.size() >= batch) give();
    }
    if (!cancel.load(std::memory_order_relaxed)) give();
    report.cancelled = cancel.load();
    if (left_out) *left_out = db->report();
    return report;
}

}  // namespace fcg::study
