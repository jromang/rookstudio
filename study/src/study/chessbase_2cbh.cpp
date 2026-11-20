// The record framing of .2cbh, .2cbg and .2cba is ported from cbvault 0.1.6, MIT License, Copyright (c) 2026 cbvault
// Contributors (twocbh/header.rs, record.rs, segment.rs). cbvault reads no more of this format; its moves, names and
// annotations are read here by code written for RookStudio, checked on the databases of our own games in
// study/tests/data/chessbase.
//
// The newer ChessBase format, all its integers little-endian except the header of the names. Its files share a name:
// - .2cbh: 192-byte records, the first the file's own (the record size at 0x0a, the record count with it at 0x10).
//   A game's: its kind (1 a game) at 0x00; where its moves are in .2cbg (0x08) and its annotations in .2cba (0x10);
//   its white and black players, tournament, annotator, source, white and black teams by their number in .2lid (an
//   8-byte integer each from 0x18, -1 none); its result (0x58), round and sub-round (0x5a, 0x5c); the ratings of White
//   (0x60) and Black (0x70), each with its kind ("FIDE") 8 bytes after; its ECO (0x80); its date (0xbc), packed as in
//   the older format.
// - .2cbg and .2cba: a 12-byte header (the file's size, where its first record starts), then records that frame
//   themselves: a magic of 8 bytes, the content's length a and the padding's b, 8 bytes not used here, the content,
//   ff ff, b zero bytes, and the record's whole length (34 + a + b).
// - .2lid: the names. A header (big-endian: where the rows start, the number of kinds, then per kind 20 bytes: the
//   size of its record and its count), then rows of one record of each kind: players (who annotate too),
//   tournaments, sources, teams. A record is its length and its content; a text in it its length and its UTF-8 bytes.
//
// A game's moves are 16-bit words. The first is 1. Then fffc and the moves, or fffb, a set-up position, fffc and the
// moves. A move's word names the move in a table of every move a piece may make anywhere: for White then Black, the
// king, the queen, the knights, the bishops and the rooks, each from a1, a2 ... h8 (file by file) in turn, along
// directions in an order of their own, nearest square first, each of those moves six times (nothing taken, or a queen,
// knight, bishop, rook or pawn taken); then White's pawns and Black's, from the second rank to the seventh of each
// file, a file at a time: their moves forward (two squares then one from the start), then their captures towards the
// a-file then towards the h-file (five each, by what they take), an en passant capture after the captures on its side
// where one may be made; a promotion forward four times (queen, knight, bishop, rook), and on each side sixteen
// times (by what it takes, then what it becomes); then castling: White's long and short, Black's. The table starts at
// 1 and ends at b12c. fffd after a move: the move has an alternative, which follows once the line has ended (several:
// the one met last first, as the older format stores them; the games checked here hold one variation with one of its
// own); ffff ends a line. A set-up position: the move number, two words (0 in every game checked here; the side to move
// is the first move's), and a word per piece: c02d plus its place in a table of every piece on every square (as the
// moves' table, pawns on the second to the seventh rank alone).
//
// A game's annotations: a word (0x2000), then per move its number (moves counted as PGN writes them, -1 the game),
// the annotations' count and each annotation: its type in two bytes and its data, of a size the type sets; ffff ends
// them.

#include <algorithm>
#include <array>

#include <tour/chess_text.hpp>

#include "chessbase_detail.hpp"

namespace fcg::study::chessbase {

namespace {

constexpr std::size_t RECORD     = 192;
constexpr std::size_t FRAME      = 34;
constexpr std::size_t MAX_RECORD = 16 << 20;
constexpr uint8_t MAGIC[8]       = {0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};

constexpr uint32_t SETUP = 0xFFFB, MOVES = 0xFFFC, ALTERNATIVE = 0xFFFD, END = 0xFFFF;
constexpr uint32_t FIRST_PIECE = 0xC02D;

// A move of the table: its squares (rank * 8 + file), what a pawn becomes (a chess::PieceType, 7 none), its side,
// castling (1 the king's side, 2 the queen's), en passant.
struct TableMove {
    uint8_t from = 0, to = 0, promotion = 7, white = 1, castle = 0, en_passant = 0;
};

constexpr uint32_t TABLE_END = 0xB12D;

const std::vector<TableMove>& moveTable() {
    static const std::vector<TableMove> table = [] {
        std::vector<TableMove> t(TABLE_END);
        using Dirs        = std::vector<std::pair<int, int>>;
        const Dirs king   = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};
        const Dirs knight = {{-2, -1}, {-2, 1}, {2, -1}, {2, 1}, {-1, -2}, {-1, 2}, {1, -2}, {1, 2}};
        const Dirs bishop = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        const Dirs rook   = {{-1, 0}, {0, -1}, {1, 0}, {0, 1}};
        Dirs queen        = bishop;
        queen.insert(queen.end(), rook.begin(), rook.end());
        uint32_t code  = 1;
        const auto add = [&](int from, int to, int promotion, bool white, int castle, bool ep, int times) {
            for (int i = 0; i < times; ++i)
                t.at(code++) = TableMove{static_cast<uint8_t>(from),      static_cast<uint8_t>(to),
                                         static_cast<uint8_t>(promotion), static_cast<uint8_t>(white),
                                         static_cast<uint8_t>(castle),    static_cast<uint8_t>(ep)};
        };
        for (const bool white : {true, false}) {
            for (const auto& [dirs, slide] : std::vector<std::pair<Dirs, bool>>{
                     {king, false}, {queen, true}, {knight, false}, {bishop, true}, {rook, true}}) {
                for (int f = 0; f < 8; ++f)
                    for (int r = 0; r < 8; ++r)
                        for (const auto& [df, dr] : dirs)
                            for (int x = f + df, y = r + dr; x >= 0 && x < 8 && y >= 0 && y < 8; x += df, y += dr) {
                                add(r * 8 + f, y * 8 + x, 7, white, 0, false, 6);
                                if (!slide) break;
                            }
            }
        }
        for (const bool white : {true, false}) {
            const int forward = white ? 1 : -1, start = white ? 1 : 6, last = white ? 6 : 1, ep = white ? 4 : 3;
            for (int f = 0; f < 8; ++f)
                for (int r = 1; r <= 6; ++r) {
                    const int from = r * 8 + f, ahead = (r + forward) * 8;
                    if (r == last) {
                        for (const int p : {4, 1, 2, 3}) add(from, ahead + f, p, white, 0, false, 1);
                        for (const int df : {-1, 1}) {
                            if (f + df < 0 || f + df > 7) continue;
                            for (int taken = 0; taken < 4; ++taken)
                                for (const int p : {4, 1, 2, 3}) add(from, ahead + f + df, p, white, 0, false, 1);
                        }
                        continue;
                    }
                    if (r == start) add(from, (r + 2 * forward) * 8 + f, 7, white, 0, false, 1);
                    add(from, ahead + f, 7, white, 0, false, 1);
                    for (const int df : {-1, 1}) {
                        if (f + df < 0 || f + df > 7) continue;
                        add(from, ahead + f + df, 7, white, 0, false, 5);
                        if (r == ep) add(from, ahead + f + df, 7, white, 0, true, 1);
                    }
                }
        }
        add(4, 2, 7, true, 2, false, 1);
        add(4, 6, 7, true, 1, false, 1);
        add(60, 58, 7, false, 2, false, 1);
        add(60, 62, 7, false, 1, false, 1);
        return t;
    }();
    return table;
}

// The piece of a set-up position's word, as a FEN letter, and its square; none for a word of no piece.
std::optional<std::pair<char, int>> setupPiece(uint32_t word) {
    if (word < FIRST_PIECE) return std::nullopt;
    const uint32_t i               = word - FIRST_PIECE;
    static constexpr char PIECES[] = "KQNBRkqnbr";
    if (i < 640) {
        const uint32_t square = i % 64;  // file by file
        return std::pair{PIECES[i / 64], static_cast<int>((square % 8) * 8 + square / 8)};
    }
    if (i < 736) {
        const uint32_t p = (i - 640) % 48;
        return std::pair{i < 688 ? 'P' : 'p', static_cast<int>((p % 6 + 1) * 8 + p / 6)};
    }
    return std::nullopt;
}

// A record of .2cbg or .2cba, framed.
class Segment {
   public:
    explicit Segment(std::unique_ptr<Source> source) : source_(std::move(source)) {
        const auto& name = source_->name();
        if (source_->size() < 12) throw std::runtime_error(name + ": damaged (shorter than its header)");
        const auto h = source_->read(0, 12);
        if (le64(h.data()) != source_->size())
            throw std::runtime_error(name + ": damaged (its header says " + std::to_string(le64(h.data())) +
                                     " bytes, the file has " + std::to_string(source_->size()) + ")");
        first_ = le16(h.data() + 8);
        if (first_ < 12 || first_ > source_->size())
            throw std::runtime_error(name + ": damaged (its first record is outside the file)");
    }

    [[nodiscard]] const std::string& name() const noexcept { return source_->name(); }

    // The content of the record at `offset`.
    std::vector<uint8_t> content(uint64_t offset) {
        const auto& name = source_->name();
        const auto where = name + " at byte " + std::to_string(offset);
        if (offset < first_ || offset + 16 > source_->size()) throw Damage(where + ": no record there");
        const auto head = source_->read(offset, 16);
        if (!std::equal(head.begin(), head.begin() + 8, MAGIC)) throw Damage(where + ": no record there");
        const uint64_t a = le32(head.data() + 8), b = le32(head.data() + 12);
        const uint64_t total = FRAME + a + b;
        if (total > MAX_RECORD || offset + total > source_->size()) throw Damage(where + ": a record past the file");
        const auto r = source_->read(offset, total);
        if (le64(r.data() + total - 8) != total) throw Damage(where + ": a record whose length disagrees with it");
        return {r.begin() + 24, r.begin() + 24 + static_cast<std::ptrdiff_t>(a)};
    }

   private:
    std::unique_ptr<Source> source_;
    uint64_t first_ = 0;
};

// The names: rows of one record of each kind.
class Names {
   public:
    enum Kind : std::size_t { PLAYERS = 0, TOURNAMENTS = 1, SOURCES = 2, TEAMS = 4 };

    explicit Names(std::unique_ptr<Source> source) : source_(std::move(source)) {
        const auto& name = source_->name();
        if (source_->size() < 8) throw std::runtime_error(name + ": damaged (shorter than its header)");
        const auto h     = source_->read(0, 8);
        start_           = be32(h.data());
        const uint32_t n = be32(h.data() + 4);
        if (n == 0 || n > 64 || start_ < 8 + 20 * n || start_ > source_->size())
            throw std::runtime_error(name + ": not a file of names");
        const auto kinds = source_->read(8, 20 * n);
        uint64_t offset  = 0;
        for (uint32_t k = 0; k < n; ++k) {
            const uint32_t size = be32(kinds.data() + 20 * k);
            const auto count    = static_cast<int32_t>(be32(kinds.data() + 20 * k + 8));
            kinds_.push_back(Column{offset, size, count > 0 ? static_cast<uint32_t>(count) : 0});
            offset += size;
        }
        row_ = offset;
        if (row_ == 0 || row_ > (1u << 20)) throw std::runtime_error(name + ": not a file of names");
    }

    // The content of name `id` of a kind: none for no such name.
    std::optional<std::vector<uint8_t>> record(std::size_t kind, int64_t id) {
        if (kind >= kinds_.size() || id < 0 || static_cast<uint64_t>(id) >= kinds_[kind].count) return std::nullopt;
        const auto& c     = kinds_[kind];
        const uint64_t at = start_ + static_cast<uint64_t>(id) * row_ + c.offset;
        if (c.size < 4 || at + 4 > source_->size())
            throw Damage(source_->name() + ": name " + std::to_string(id) + " is past the file");
        const uint32_t length = le32(source_->read(at, 4).data());
        if (length > c.size - 4 || at + 4 + length > source_->size())
            throw Damage(source_->name() + ": name " + std::to_string(id) + " runs past its record");
        const auto r = source_->read(at + 4, length);
        return std::vector<uint8_t>(r.begin(), r.end());
    }

    // The texts of a record, one after the other: none past its end.
    static std::optional<std::string> text(const std::vector<uint8_t>& r, std::size_t& at) {
        if (r.size() < 4 || at > r.size() - 4) return std::nullopt;
        const uint32_t n = le32(r.data() + at);
        if (n > r.size() - at - 4) return std::nullopt;
        auto s = nameText(std::span(r.data() + at + 4, n));
        at += 4 + n;
        return s;
    }

   private:
    struct Column {
        uint64_t offset = 0;
        uint32_t size = 0, count = 0;
    };
    std::unique_ptr<Source> source_;
    uint64_t start_ = 0, row_ = 0;
    std::vector<Column> kinds_;
};

std::string seconds(int64_t hundredths) {
    if (hundredths % 100 == 0) return std::to_string(hundredths / 100);
    std::string s = std::to_string(hundredths / 100) + "." + std::to_string(100 + hundredths % 100).substr(1);
    while (s.back() == '0') s.pop_back();
    return s;
}

class TwoCbhReader final : public FormatReader {
   public:
    explicit TwoCbhReader(std::unique_ptr<Members> members) : members_(std::move(members)) {
        const auto label = members_->label();
        const auto need  = [&](const char* ext, const char* what, bool random) {
            auto s = members_->open(ext, random);
            if (!s)
                throw std::runtime_error(label + ext + " is missing: the ChessBase database keeps " + what + " in it");
            return s;
        };
        headers_ = need(".2cbh", "its games' headers", false);
        moves_.emplace(need(".2cbg", "its games' moves", false));
        notes_.emplace(need(".2cba", "its games' annotations", false));
        names_.emplace(need(".2lid", "its names", true));
        const uint64_t size = headers_->size();
        if (size < RECORD || size % RECORD != 0)
            throw std::runtime_error(headers_->name() + ": damaged (" + std::to_string(size) +
                                     " bytes is not whole records of 192 bytes)");
        const auto head = headers_->read(0, RECORD);
        if (le16(head.data() + 0x0A) != RECORD)
            throw std::runtime_error(headers_->name() +
                                     ": not a ChessBase header file (its records are not of 192 "
                                     "bytes)");
        if (le32(head.data() + 0x10) != size / RECORD)
            throw std::runtime_error(headers_->name() + ": damaged (its header says " +
                                     std::to_string(le32(head.data() + 0x10)) + " records, the file holds " +
                                     std::to_string(size / RECORD) + ")");
        records_ = static_cast<uint32_t>(size / RECORD - 1);
    }

    [[nodiscard]] ChessBaseFormat format() const noexcept override { return ChessBaseFormat::TwoCBH; }
    [[nodiscard]] uint32_t records() const noexcept override { return records_; }
    [[nodiscard]] uint64_t headerBytes() const noexcept override { return headers_->size(); }
    [[nodiscard]] uint64_t headerEnd(uint32_t record) const noexcept override {
        return std::min<uint64_t>((uint64_t{record} + 1) * RECORD, headers_->size());
    }

    RecordKind read(uint32_t id, RawGame& out) override {
        std::array<uint8_t, RECORD> b{};
        const auto r = headers_->read(uint64_t{id} * RECORD, RECORD);
        std::copy(r.begin(), r.end(), b.begin());
        if (b[0] & 0x80) return RecordKind::Deleted;
        if ((b[0] & 0x7F) != 1) return RecordKind::Unknown;
        out.order = Order::Pgn;
        tags(b, out);
        out.builder.reset(chess::Board());
        try {
            walk(moves_->content(le64(b.data() + 0x08)), out);
        } catch (const Damage& e) {
            out.problem = moves_->name() + ": game " + std::to_string(id) + ": " + e.what();
        }
        if (const uint64_t at = le64(b.data() + 0x10); at != 0) {
            try {
                annotations(id, notes_->content(at), out);
            } catch (const Damage& e) {
                if (out.problem.empty()) out.problem = e.what();
                out.notes.clear();
            }
        }
        return RecordKind::Game;
    }

   private:
    void tags(const std::array<uint8_t, RECORD>& b, RawGame& out) {
        const auto id     = [&](std::size_t at) { return static_cast<int64_t>(le64(b.data() + at)); };
        const auto player = [&](int64_t who) -> std::string {
            const auto r = names_->record(Names::PLAYERS, who);
            if (!r) return {};
            std::size_t at   = 0;
            const auto last  = Names::text(*r, at);
            const auto first = Names::text(*r, at);
            return playerText(last.value_or(""), first.value_or(""));
        };
        std::string event, site, event_date;
        if (const auto t = names_->record(Names::TOURNAMENTS, id(0x28))) {
            std::size_t at = 0;
            site           = Names::text(*t, at).value_or("");
            event          = Names::text(*t, at).value_or("");
            if (at + 4 <= t->size()) {
                const uint32_t date = le32(t->data() + at);
                if ((date >> 5) & 15) event_date = dateText(date);
            }
        }
        const auto team = [&](int64_t which) -> std::string {
            const auto r = names_->record(Names::TEAMS, which);
            if (!r) return {};
            std::size_t at = 0;
            return Names::text(*r, at).value_or("");
        };
        addTag(out, "Event", event);
        addTag(out, "Site", site);
        addTag(out, "Date", dateText(le32(b.data() + 0xBC)));
        addTag(out, "Round",
               roundText(static_cast<int>(le16(b.data() + 0x5A)), static_cast<int>(le16(b.data() + 0x5C))));
        addTag(out, "White", player(id(0x18)));
        addTag(out, "Black", player(id(0x20)));
        addTag(out, "Result", resultText(le16(b.data() + 0x58)));
        if (const uint32_t elo = le16(b.data() + 0x60)) addTag(out, "WhiteElo", std::to_string(elo));
        if (const uint32_t elo = le16(b.data() + 0x70)) addTag(out, "BlackElo", std::to_string(elo));
        addTag(out, "ECO", ecoText(le16(b.data() + 0x80)));
        addTag(out, "Annotator", player(id(0x30)));
        addTag(out, "WhiteTeam", team(id(0x40)));
        addTag(out, "BlackTeam", team(id(0x48)));
        addTag(out, "EventDate", event_date);
    }

    void walk(const std::vector<uint8_t>& content, RawGame& out) {
        missing_ = 0;
        std::vector<uint32_t> words(content.size() / 2);
        for (std::size_t i = 0; i < words.size(); ++i) words[i] = le16(content.data() + 2 * i);
        if (words.empty() || words[0] != 1) throw Damage("a move record of an unknown kind");
        std::size_t i = 1;
        std::array<char, 64> board{};
        int move_number  = 1;
        const bool setup = i < words.size() && words[i] == SETUP;
        if (setup) {
            if (i + 4 > words.size()) throw Damage("its start position is cut");
            move_number = static_cast<int>(words[i + 1]);
            for (i += 4; i < words.size() && words[i] != MOVES; ++i) {
                const auto piece = setupPiece(words[i]);
                if (!piece || board[piece->second]) throw Damage("its start position is not one");
                board[piece->second] = piece->first;
            }
        }
        if (i >= words.size() || words[i] != MOVES) throw Damage("its moves do not start");
        const std::span<const uint32_t> moves(words.data() + i + 1, words.size() - i - 1);
        const auto& table = moveTable();
        if (!setup) {
            play(moves, out);
            return;
        }
        // The side to move is the first move's; the castling rights those its castling needs, the en passant
        // square that of a first move taking en passant.
        bool black     = false;
        std::string ep = "-";
        for (const uint32_t w : moves) {
            if (w == 0 || w >= TABLE_END) break;
            black = !table[w].white;
            if (table[w].en_passant)
                ep = std::string{static_cast<char>('a' + table[w].to % 8), static_cast<char>('1' + table[w].to / 8)};
            break;
        }
        std::string castling;
        for (int attempt = 0;; ++attempt) {
            std::string fen;
            for (int rank = 7; rank >= 0; --rank) {
                int empty = 0;
                for (int f = 0; f < 8; ++f) {
                    const char c = board[rank * 8 + f];
                    if (!c) {
                        ++empty;
                        continue;
                    }
                    if (empty) fen += static_cast<char>('0' + empty);
                    empty = 0;
                    fen += c;
                }
                if (empty) fen += static_cast<char>('0' + empty);
                if (rank) fen += '/';
            }
            std::string sorted;
            for (const char c : {'K', 'Q', 'k', 'q'})
                if (castling.find(c) != std::string::npos) sorted += c;
            fen += std::string(black ? " b " : " w ") + (sorted.empty() ? "-" : sorted) + " " + ep + " 0 " +
                   std::to_string(std::max(1, move_number));
            if (!tour::readableFen(fen)) throw Damage("its start position is not one");
            chess::Board start(fen);
            if (!tour::legalPosition(start)) throw Damage("its start position is not legal");
            out.fen = start.getFen();
            out.builder.reset(start);
            try {
                play(moves, out);
                return;
            } catch (const Damage&) {
                // A castling the start's rights lack: the right added, when the king and the rook allow it.
                const char want = missing_;
                missing_        = 0;
                if (want && attempt < 4 && castling.find(want) == std::string::npos) {
                    castling += want;
                    chess::Board check(fen.substr(0, fen.find(' ')) + (black ? " b " : " w ") + std::string(1, want) +
                                       " - 0 1");
                    using Side      = chess::Board::CastlingRights::Side;
                    const auto side = want == 'K' || want == 'k' ? Side::KING_SIDE : Side::QUEEN_SIDE;
                    if (check.castlingRights().has(want < 'a' ? chess::Color::WHITE : chess::Color::BLACK, side))
                        continue;
                }
                throw;
            }
        }
    }

    void play(std::span<const uint32_t> words, RawGame& out) {
        const auto& table = moveTable();
        auto& tree        = out.builder;
        for (std::size_t i = 0; i < words.size(); ++i) {
            const uint32_t w = words[i];
            const auto where = "word " + std::to_string(i + 1) + " of its moves";
            if (w == ALTERNATIVE) {
                if (tree.pendingCount() >= 1024) throw Damage(where + ": variations nested too deep");
                tree.branchLast();
                continue;
            }
            if (w == END) {
                if (!tree.endLine()) return;  // the tree's end: what follows is left over
                continue;
            }
            if (w == 0 || w >= TABLE_END) throw Damage(where + ": " + std::to_string(w) + " is not a move");
            const auto& m     = table[w];
            const auto& board = tree.board();
            const bool white  = board.sideToMove() == chess::Color::WHITE;
            if (static_cast<bool>(m.white) != white) throw Damage(where + ": a move of the side not to move");
            const auto move = m.castle ? legalMove(board, -1, -1, -1, m.castle)
                                       : legalMove(board, m.from, m.to, m.promotion == 7 ? -1 : m.promotion);
            if (!move) {
                if (m.castle) {
                    using Side      = chess::Board::CastlingRights::Side;
                    const auto side = m.castle == 1 ? Side::KING_SIDE : Side::QUEEN_SIDE;
                    if (!board.castlingRights().has(board.sideToMove(), side))
                        missing_ = static_cast<char>((m.castle == 1 ? 'K' : 'Q') + (white ? 0 : 32));
                }
                throw Damage(where + ": a move that is not legal");
            }
            tree.play(*move);
        }
    }

    void annotations(uint32_t id, const std::vector<uint8_t>& c, RawGame& out) {
        const auto bad = [&](const std::string& what) {
            return Damage(notes_->name() + ": game " + std::to_string(id) + ": " + what);
        };
        if (c.size() < 2 || le16(c.data()) != 0x2000) throw bad("annotations of an unknown kind");
        std::size_t at  = 2;
        const auto need = [&](std::size_t n) {
            if (n > c.size() - at) throw bad("an annotation runs past its record");
        };
        while (at < c.size()) {
            if (c.size() - at == 2 && le16(c.data() + at) == 0xFFFF) break;
            need(8);
            const auto position  = static_cast<int32_t>(le32(c.data() + at));
            const uint32_t count = le32(c.data() + at + 4);
            at += 8;
            if (position < -1) throw bad("an annotation at no move");
            for (uint32_t k = 0; k < count; ++k) {
                need(2);
                const uint32_t type = le16(c.data() + at);
                at += 2;
                Note n;
                n.position = position;
                n.kind     = noteKind(type);
                switch (type) {
                    case 0x02:
                    case 0x82: {
                        need(8);
                        n.language         = static_cast<uint16_t>(le16(c.data() + at + 2));
                        const uint32_t len = le32(c.data() + at + 4);
                        at += 8;
                        need(len);
                        n.text = commentText(std::span(c.data() + at, len), n.language != ANY_LANGUAGE);
                        at += len;
                        break;
                    }
                    case 0x03:
                        need(3);
                        n.nags = {c[at + 2], c[at], c[at + 1]};  // the prefix, the move's, the position's
                        at += 3;
                        break;
                    case 0x04:
                    case 0x05: {
                        need(4);
                        const uint32_t len = le32(c.data() + at);
                        at += 4;
                        need(len);
                        const std::size_t item = type == 0x04 ? 2 : 3;
                        if (len % item) throw bad("coloured squares or arrows of a wrong size");
                        for (std::size_t i = at; i < at + len; i += item) {
                            for (std::size_t s = 1; s < item; ++s)
                                if (c[i + s] < 1 || c[i + s] > 64) throw bad("a coloured square off the board");
                            const char colours[] = {0, 0, 'G', 'Y', 'R', 0, 0, 'B'};
                            if (c[i] >= sizeof(colours) || !colours[c[i]]) continue;
                            const auto square = [](uint8_t v) {
                                return static_cast<int8_t>(((v - 1) % 8) * 8 + (v - 1) / 8);
                            };
                            n.marks.push_back(BoardMark{colours[c[i]], square(c[i + 1]),
                                                        item == 3 ? square(c[i + 2]) : static_cast<int8_t>(-1)});
                        }
                        at += len;
                        break;
                    }
                    case 0x07:
                        // A byte not used here, the seconds, the minutes, the hours.
                        need(4);
                        if (c[at + 1] < 60 && c[at + 2] < 60)
                            n.value = ((c[at + 3] * 60LL + c[at + 2]) * 60 + c[at + 1]) * 1000;
                        else
                            n.decoded = false;
                        at += 4;
                        break;
                    case 0x21: {
                        // The value, its kind (0 centipawns, 1 moves to mate), the depth.
                        need(6);
                        const uint32_t kind = le16(c.data() + at + 2);
                        n.value             = static_cast<int16_t>(le16(c.data() + at));
                        n.mate              = kind == 1;
                        n.depth             = static_cast<int16_t>(le16(c.data() + at + 4));
                        n.decoded           = (kind == 0 || kind == 1) && !(n.mate && n.value == 0);
                        at += 6;
                        break;
                    }
                    case 0x24:
                        // 01, three stages of 11 bytes (the time it starts with and the time added per move in
                        // hundredths of a second, its moves, its kind: 0 the rest of the game, 1 a number of moves,
                        // 3 the rest with an increment, 5 no time), four zero bytes.
                        need(38);
                        n.text    = timeControl(std::span(c.data() + at, 38));
                        n.decoded = !n.text.empty();
                        at += 38;
                        break;
                    case 0x26: {
                        // 01, the length of what follows, a count, then per position of the main line its value, depth
                        // and flag (0 centipawns, 1 a mate in plies, ff none).
                        need(7);
                        const uint32_t len = le32(c.data() + at + 1), entries = le16(c.data() + at + 5);
                        need(5 + static_cast<std::size_t>(len));
                        if (c[at] == 1 && len == 2 + 4 * entries) {
                            for (std::size_t e = at + 7; e < at + 7 + 4 * entries; e += 4) {
                                const auto value   = static_cast<int16_t>(le16(c.data() + e));
                                const uint8_t flag = c[e + 3];
                                if (flag == 0)
                                    n.profile.emplace_back(value, c[e + 2]);
                                else if (flag == 1 && value != 0)
                                    n.profile.emplace_back(static_cast<int16_t>(value > 0 ? 30000 - (value + 1) / 2
                                                                                          : -30000 + (-value + 1) / 2),
                                                           c[e + 2]);
                                else
                                    n.profile.emplace_back(INT16_MIN, 0);
                            }
                        } else {
                            n.decoded = false;
                        }
                        at += 5 + len;
                        break;
                    }
                    default:
                        // A type whose data's size is not known here: the annotations after it cannot be found.
                        n.decoded = false;
                        out.notes.push_back(std::move(n));
                        return;
                }
                out.notes.push_back(std::move(n));
            }
        }
    }

    static std::string timeControl(std::span<const uint8_t> d) {
        if (d[0] != 1 || d[34] || d[35] || d[36] || d[37]) return {};
        std::string out;
        for (int k = 0; k < 3; ++k) {
            const uint8_t* s     = d.data() + 1 + 11 * k;
            const auto initial   = static_cast<int32_t>(le32(s));
            const auto increment = static_cast<int32_t>(le32(s + 4));
            const uint32_t moves = le16(s + 8);
            const uint8_t kind   = s[10];
            if (initial < 0 || increment < 0) return {};
            if (kind == 5) {
                if (k == 0) out = "-";
                break;
            }
            if (kind != 0 && kind != 1 && kind != 3) return {};
            if (!out.empty()) out += ':';
            if (kind == 1) out += std::to_string(moves) + "/";
            out += seconds(initial);
            if (increment > 0) out += "+" + seconds(increment);
            if (kind != 1) break;
        }
        return out;
    }

    std::unique_ptr<Members> members_;
    std::unique_ptr<Source> headers_;
    std::optional<Segment> moves_, notes_;
    std::optional<Names> names_;
    uint32_t records_ = 0;
    char missing_     = 0;
};

}  // namespace

std::unique_ptr<FormatReader> twoCbhReader(std::unique_ptr<Members> members) {
    try {
        return std::make_unique<TwoCbhReader>(std::move(members));
    } catch (const Damage& e) {
        throw std::runtime_error(e.what());
    }
}

}  // namespace fcg::study::chessbase
