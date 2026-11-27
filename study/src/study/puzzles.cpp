#include "puzzles.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstring>
#include <random>
#include <stdexcept>
#include <system_error>
#include <unordered_set>

#include <tour/files.hpp>

#include "scan_board.hpp"

namespace fcg::study {

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
// Little-endian words, varints, base 62

uint16_t load16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t load32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}
uint64_t load48(const uint8_t* p) {
    return static_cast<uint64_t>(load32(p)) | static_cast<uint64_t>(load16(p + 4)) << 32;
}
uint64_t load64(const uint8_t* p) {
    return static_cast<uint64_t>(load32(p)) | static_cast<uint64_t>(load32(p + 4)) << 32;
}

void put8(std::vector<uint8_t>& out, unsigned v) { out.push_back(static_cast<uint8_t>(v)); }
void put16(std::vector<uint8_t>& out, unsigned v) {
    for (int i = 0; i < 2; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void put48(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 6; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void put64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void putVarint(std::vector<uint8_t>& out, uint32_t v) {
    while (v >= 0x80) {
        out.push_back(static_cast<uint8_t>(v | 0x80));
        v >>= 7;
    }
    out.push_back(static_cast<uint8_t>(v));
}
uint32_t readVarint(const uint8_t*& p) {
    uint32_t v = 0;
    for (int shift = 0; shift < 35; shift += 7) {
        const uint8_t b = *p++;
        v |= static_cast<uint32_t>(b & 0x7F) << shift;
        if (!(b & 0x80)) break;
    }
    return v;
}

int digit62(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 36;
    return -1;
}
std::optional<uint64_t> base62(std::string_view text, std::size_t length) {
    if (text.size() != length) return std::nullopt;
    uint64_t v = 0;
    for (const char c : text) {
        const int d = digit62(c);
        if (d < 0) return std::nullopt;
        v = v * 62 + static_cast<uint64_t>(d);
    }
    return v;
}
std::string text62(uint64_t v, std::size_t length) {
    static constexpr char DIGITS[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string out(length, '0');
    for (std::size_t i = length; i-- > 0;) {
        out[i] = DIGITS[v % 62];
        v /= 62;
    }
    return out;
}

template <typename T>
bool number(std::string_view text, T& out) {
    const auto end = text.data() + text.size();
    const auto r   = std::from_chars(text.data(), end, out);
    return r.ec == std::errc{} && r.ptr == end;
}

// The words of a text, between spaces.
template <typename F>
void words(std::string_view text, F&& each) {
    std::size_t from = 0;
    while (from < text.size()) {
        const auto space = text.find(' ', from);
        const auto word  = text.substr(from, space == std::string_view::npos ? std::string_view::npos : space - from);
        if (!word.empty()) each(word);
        if (space == std::string_view::npos) break;
        from = space + 1;
    }
}

// ---------------------------------------------------------------------------
// The store's files

constexpr char MAGIC[8]          = {'R', 'S', 'P', 'U', 'Z', 'Z', 'L', '1'};
constexpr std::size_t HEADER     = 128;
constexpr int RATINGS            = 4096;  // ratings kept: 0 to 4095
constexpr uint32_t BLOCK         = 64;    // records of a block, its start in the table
constexpr uint32_t CHUNK         = 128;   // puzzles of a list between two of its skips
constexpr int MOST_THEMES        = 255;
constexpr int MOST_OPENINGS      = 65535;
constexpr std::size_t MOST_MOVES = 60;
constexpr int BUCKET_POINTS      = 100;

// The record's flags.
constexpr uint8_t BLACK_TO_MOVE = 1;
constexpr uint8_t CASTLING_K = 2, CASTLING_Q = 4, CASTLING_k = 8, CASTLING_q = 16;
constexpr uint8_t EN_PASSANT = 32;
constexpr uint8_t WIDE_CODES = 64;  // two bytes a move: a position with more than 256 codes (many queens)

// Where the parts of a record start: fixed fields, then the position, the moves and the openings.
struct Parts {
    uint8_t flags = 0;
    int ep_file   = -1;
    int halfmove = 0, fullmove = 1;
    uint64_t occupied          = 0;
    const uint8_t* men         = nullptr;
    int moves                  = 0;
    const uint8_t* codes       = nullptr;
    int openings               = 0;
    const uint8_t* opening_ids = nullptr;
};
// A record's layout: length (1), id (4), rating (2), deviation (1), popularity (1), game (6), flags (1), [en passant
// file (1)], halfmove clock (1), fullmove number (2), occupied squares (8), men (a nibble each), moves (1), codes (1 or
// 2 each), openings (1), their numbers (2 each).
Parts partsOf(const uint8_t* r) {
    Parts p;
    p.flags        = r[15];
    std::size_t at = 16;
    if (p.flags & EN_PASSANT) p.ep_file = r[at++];
    p.halfmove = r[at++];
    p.fullmove = load16(r + at);
    at += 2;
    p.occupied = load64(r + at);
    at += 8;
    p.men = r + at;
    at += (static_cast<std::size_t>(std::popcount(p.occupied)) + 1) / 2;
    p.moves = r[at++];
    p.codes = r + at;
    at += static_cast<std::size_t>(p.moves) * ((p.flags & WIDE_CODES) ? 2 : 1);
    p.openings    = r[at++];
    p.opening_ids = r + at;
    return p;
}

// The FEN of a record's position.
std::string fenOf(const Parts& p) {
    static constexpr char MEN[] = " PNBRQKpnbrqk";
    std::string out;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const int square = rank * 8 + file;
            if (!(p.occupied >> square & 1)) {
                ++empty;
                continue;
            }
            if (empty) out += static_cast<char>('0' + empty);
            empty = 0;
            // The men are kept in the order of their squares, a1 first.
            const int man   = std::popcount(p.occupied & ((uint64_t{1} << square) - 1));
            const int value = (p.men[man / 2] >> (4 * (man % 2))) & 0xF;
            out += value >= 1 && value <= 12 ? MEN[value] : '?';
        }
        if (empty) out += static_cast<char>('0' + empty);
        if (rank > 0) out += '/';
    }
    const bool black = p.flags & BLACK_TO_MOVE;
    out += black ? " b " : " w ";
    std::string castling;
    if (p.flags & CASTLING_K) castling += 'K';
    if (p.flags & CASTLING_Q) castling += 'Q';
    if (p.flags & CASTLING_k) castling += 'k';
    if (p.flags & CASTLING_q) castling += 'q';
    out += castling.empty() ? "-" : castling;
    out += ' ';
    if (p.ep_file >= 0) {
        out += static_cast<char>('a' + p.ep_file);
        out += black ? '3' : '6';
    } else {
        out += '-';
    }
    out += ' ' + std::to_string(p.halfmove) + ' ' + std::to_string(p.fullmove);
    return out;
}

int squareOf(char file, char rank) {
    if (file < 'a' || file > 'h' || rank < '1' || rank > '8') return -1;
    return (rank - '1') * 8 + (file - 'a');
}

// A move in UCI on `scan`'s position, as a coded move: its piece the side to move's, its kind's geometry checked by the
// code it gets (decoded, the code gives the same move back). A castling goes to its rook's corner.
std::optional<CodedMove> codedOf(const ScanBoard& scan, std::string_view uci) {
    if (uci.size() != 4 && uci.size() != 5) return std::nullopt;
    const int from = squareOf(uci[0], uci[1]), to = squareOf(uci[2], uci[3]);
    if (from < 0 || to < 0 || from == to) return std::nullopt;
    const uint8_t piece = scan.on(from);
    if (piece == 0) return std::nullopt;
    const int kind = (piece - 1) % 6, side = (piece - 1) / 6;
    if (side != scan.sideToMove()) return std::nullopt;
    CodedMove move;
    move.from    = static_cast<uint8_t>(from);
    move.to      = static_cast<uint8_t>(to);
    const int df = (to & 7) - (from & 7);
    if (kind == KING && (df == 2 || df == -2) && (to >> 3) == (from >> 3)) {
        move.flags = df > 0 ? MOVE_SHORT_CASTLE : MOVE_LONG_CASTLE;
        move.to    = static_cast<uint8_t>((from & ~7) + (df > 0 ? 7 : 0));
    } else {
        if (const uint8_t target = scan.on(to); target != 0 && (target - 1) / 6 == side) return std::nullopt;
        if (kind == PAWN && df != 0 && scan.on(to) == 0) move.flags = MOVE_EN_PASSANT;
    }
    if (uci.size() == 5) {
        if (kind != PAWN) return std::nullopt;
        switch (uci[4]) {
            case 'q':
                move.promotion = QUEEN;
                break;
            case 'r':
                move.promotion = ROOK;
                break;
            case 'b':
                move.promotion = BISHOP;
                break;
            case 'n':
                move.promotion = KNIGHT;
                break;
            default:
                return std::nullopt;
        }
    }
    const int code = scan.encode(move);
    if (code < 0 || code >= scan.codeCount() - 1) return std::nullopt;  // the last code is the null move's
    if (!(scan.decode(code) == move)) return std::nullopt;
    return move;
}

// A coded move in UCI, a castling as the king's two squares.
std::string uciOf(const CodedMove& move) {
    int to = move.to;
    if (move.flags & (MOVE_SHORT_CASTLE | MOVE_LONG_CASTLE))
        to = (move.from & ~7) + ((move.flags & MOVE_SHORT_CASTLE) ? 6 : 2);
    std::string out;
    out += static_cast<char>('a' + (move.from & 7));
    out += static_cast<char>('1' + (move.from >> 3));
    out += static_cast<char>('a' + (to & 7));
    out += static_cast<char>('1' + (to >> 3));
    if (move.promotion != NO_KIND) out += "pnbrqk"[move.promotion];
    return out;
}

// A file written through a buffer, its errors said once.
class Writer {
   public:
    explicit Writer(const fs::path& file) : file_(file), out_(tour::openFile(file, "wb")) {
        if (!out_) error_ = "cannot write " + file.string();
    }
    Writer(const Writer&)            = delete;
    Writer& operator=(const Writer&) = delete;

    void put(const void* data, std::size_t size) {
        if (!out_ || !error_.empty()) return;
        if (std::fwrite(data, 1, size, out_.get()) != size)
            error_ = "cannot write " + file_.string() + " (is the disk full?)";
        written_ += size;
    }
    void put(const std::vector<uint8_t>& bytes) { put(bytes.data(), bytes.size()); }
    // Back to its start, for the header written last.
    void rewind() {
        if (out_ && std::fseek(out_.get(), 0, SEEK_SET) != 0) error_ = "cannot write " + file_.string();
    }
    // Synced and closed: empty, or why not.
    std::string close() {
        if (out_) {
            if (error_.empty() && !tour::syncFile(out_.get()))
                error_ = "cannot write " + file_.string() + " (is the disk full?)";
            out_.reset();
        }
        return error_;
    }
    [[nodiscard]] uint64_t written() const noexcept { return written_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

   private:
    fs::path file_;
    tour::File out_;
    uint64_t written_ = 0;
    std::string error_;
};

// A list made as its puzzles come, in order: every 128th one a skip (its value and where the next gaps start), the
// others the gap from the one before, as a varint.
struct ListBuilder {
    std::vector<uint8_t> skips, bytes;
    uint32_t count = 0, last = 0;
    void add(uint32_t v) {
        if (count % CHUNK == 0) {
            put32(skips, v);
            put32(skips, static_cast<uint32_t>(bytes.size()));
        } else {
            putVarint(bytes, v - last);
        }
        last = v;
        ++count;
    }
};

uint64_t fileId(const fs::path& file) {
    const auto name                   = file.filename().string();
    constexpr std::string_view PREFIX = "puzzles-";
    if (name.size() <= PREFIX.size() + 4 || name.compare(0, PREFIX.size(), PREFIX) != 0 ||
        name.compare(name.size() - 4, 4, ".bin") != 0)
        return 0;
    uint64_t id    = 0;
    const auto beg = name.data() + PREFIX.size(), end = name.data() + name.size() - 4;
    const auto r = std::from_chars(beg, end, id);
    return r.ec == std::errc() && r.ptr == end ? id : 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// A puzzle, a line

std::string Puzzle::gameUrl() const {
    if (game.empty()) return {};
    std::vector<std::string_view> fields;
    words(fen, [&](std::string_view w) { fields.push_back(w); });
    const bool black = fields.size() > 1 && fields[1] == "b";
    int full         = 1;
    if (fields.size() > 5) (void)number(fields[5], full);
    const int ply = 2 * (std::max(1, full) - 1) + (black ? 1 : 0) + 1;
    return "https://lichess.org/game/" + game + (black ? "/black" : "") + "#" + std::to_string(ply);
}

bool readPuzzleLine(std::string_view line, PuzzleLine& out) {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    std::array<std::string_view, 11> fields{};
    std::size_t n = 0, from = 0;
    while (n < fields.size()) {
        const auto comma = line.find(',', from);
        fields[n++]      = line.substr(from, comma == std::string_view::npos ? std::string_view::npos : comma - from);
        if (comma == std::string_view::npos) break;
        from = comma + 1;
    }
    if (n < 10) return false;
    PuzzleLine p;
    if (!number(fields[3], p.rating) || !number(fields[4], p.deviation) || !number(fields[5], p.popularity))
        return false;
    p.id       = fields[0];
    p.fen      = fields[1];
    p.moves    = fields[2];
    p.themes   = fields[7];
    p.url      = fields[8];
    p.openings = fields[9];
    if (p.id.empty() || p.fen.empty() || p.moves.empty()) return false;
    out = p;
    return true;
}

std::string_view puzzleGameId(std::string_view url) {
    if (const auto hash = url.find('#'); hash != std::string_view::npos) url = url.substr(0, hash);
    for (const std::string_view side : {"/black", "/white"})
        if (url.size() > side.size() && url.substr(url.size() - side.size()) == side) url.remove_suffix(side.size());
    const auto slash = url.rfind('/');
    if (slash == std::string_view::npos) return {};
    const auto id = url.substr(slash + 1);
    return base62(id, 8) ? id : std::string_view{};
}

// ---------------------------------------------------------------------------
// The store

PuzzleStore::PuzzleStore(const fs::path& file) : file_(file) {
    const uint8_t* p  = file_.data();
    const auto size   = file_.size();
    const auto broken = [&](const char* why) {
        return std::runtime_error(file.string() + " is not a store of puzzles (" + why + ")");
    };
    if (size < HEADER || std::memcmp(p, MAGIC, sizeof MAGIC) != 0) throw broken("its header");
    if (load64(p + 104) != size) throw broken("cut short");
    const uint64_t count = load64(p + 8);
    if (count > UINT32_MAX) throw broken("its count");
    count_                  = static_cast<uint32_t>(count);
    lines_                  = load64(p + 16);
    skipped_                = load64(p + 24);
    std::size_t date_length = 0;
    while (date_length < 16 && p[32 + date_length] != 0) ++date_length;
    date_.assign(reinterpret_cast<const char*>(p + 32), date_length);
    const uint32_t themes = load32(p + 48), openings = load32(p + 52);
    const uint64_t names = load64(p + 56), ratings = load64(p + 64), blocks = load64(p + 72), records = load64(p + 80),
                   lists = load64(p + 88), block_count = load64(p + 96);
    const auto inside = [&](uint64_t at, uint64_t bytes) { return at <= size && bytes <= size - at; };
    if (block_count != (count + BLOCK - 1) / BLOCK + 1 || !inside(blocks, block_count * 8) ||
        !inside(ratings, (RATINGS + 1) * 4ull) || !inside(lists, (themes + openings) * 16ull) || !inside(names, 0) ||
        !inside(records, 0))
        throw broken("its tables");
    ratings_ = p + ratings;
    blocks_  = p + blocks;
    records_ = p + records;
    if (load64(blocks_ + (block_count - 1) * 8) > size - records) throw broken("its records");
    // The names, as many as the lists.
    const uint8_t* at    = p + names;
    const uint8_t* fence = p + size;
    for (uint32_t i = 0; i < themes + openings; ++i) {
        if (at >= fence || at + 1 + *at > fence) throw broken("its names");
        Tag tag;
        tag.name.assign(reinterpret_cast<const char*>(at + 1), *at);
        at += 1 + *at;
        (i < themes ? themes_ : openings_).push_back(std::move(tag));
    }
    for (uint32_t i = 0; i < themes + openings; ++i) {
        const uint8_t* entry  = p + lists + 16ull * i;
        const uint64_t offset = load64(entry);
        List list;
        list.count            = load32(entry + 8);
        list.bytes            = load32(entry + 12);
        const uint64_t chunks = (list.count + CHUNK - 1) / CHUNK;
        if (!inside(offset, chunks * 8 + list.bytes)) throw broken("its lists");
        list.skips                                                = p + offset;
        list.data                                                 = p + offset + chunks * 8;
        (i < themes ? themes_[i] : openings_[i - themes]).puzzles = list.count;
        lists_.push_back(list);
    }
    file_.adviseRandom();
}

std::optional<int> PuzzleStore::themeList(std::string_view name) const {
    for (std::size_t i = 0; i < themes_.size(); ++i)
        if (themes_[i].name == name) return static_cast<int>(i);
    return std::nullopt;
}

std::optional<int> PuzzleStore::openingList(std::string_view name) const {
    for (std::size_t i = 0; i < openings_.size(); ++i)
        if (openings_[i].name == name) return static_cast<int>(themes_.size() + i);
    return std::nullopt;
}

std::pair<uint32_t, uint32_t> PuzzleStore::ratingRange(int low, int high) const {
    low  = std::clamp(low, 0, RATINGS);
    high = std::clamp(high + 1, 0, RATINGS);
    if (high <= low) return {0, 0};
    return {load32(ratings_ + 4 * low), load32(ratings_ + 4 * high)};
}

const uint8_t* PuzzleStore::record(uint32_t number) const {
    const uint8_t* r = records_ + load64(blocks_ + 8ull * (number / BLOCK));
    for (uint32_t i = number % BLOCK; i > 0; --i) r += r[0];
    return r;
}

int PuzzleStore::ratingOf(uint32_t number) const { return load16(record(number) + 5); }

std::string PuzzleStore::idOf(uint32_t number) const { return text62(load32(record(number) + 1), 5); }

void PuzzleStore::walk(const List& list, uint32_t from, const std::function<bool(uint32_t)>& each) const {
    if (list.count == 0) return;
    const uint32_t chunks = (list.count + CHUNK - 1) / CHUNK;
    uint32_t lo = 0, hi = chunks;  // the last chunk that starts at `from` or before
    while (hi - lo > 1) {
        const uint32_t mid = (lo + hi) / 2;
        if (load32(list.skips + 8ull * mid) <= from)
            lo = mid;
        else
            hi = mid;
    }
    for (uint32_t c = lo; c < chunks; ++c) {
        uint32_t v         = load32(list.skips + 8ull * c);
        const uint8_t* gap = list.data + load32(list.skips + 8ull * c + 4);
        const uint32_t n   = std::min(CHUNK, list.count - c * CHUNK);
        for (uint32_t j = 0; j < n; ++j) {
            if (j > 0) v += readVarint(gap);
            if (v >= from && !each(v)) return;
        }
    }
}

std::vector<uint32_t> PuzzleStore::listed(int list, uint32_t first, uint32_t last, std::size_t limit) const {
    std::vector<uint32_t> out;
    if (list < 0 || static_cast<std::size_t>(list) >= lists_.size() || first >= last) return out;
    walk(lists_[static_cast<std::size_t>(list)], first, [&](uint32_t v) {
        if (v >= last || out.size() >= limit) return false;
        out.push_back(v);
        return true;
    });
    return out;
}

bool PuzzleStore::inList(int list, uint32_t puzzle) const {
    if (list < 0 || static_cast<std::size_t>(list) >= lists_.size()) return false;
    bool found = false;
    walk(lists_[static_cast<std::size_t>(list)], puzzle, [&](uint32_t v) {
        found = v == puzzle;
        return false;
    });
    return found;
}

std::vector<uint32_t> PuzzleStore::pick(int list, int rating, std::size_t n,
                                        const std::function<bool(const std::string&)>& skip, uint64_t seed) const {
    std::vector<uint32_t> out;
    if (count_ == 0 || n == 0) return out;
    std::mt19937_64 rng(seed);
    std::unordered_set<uint32_t> tried;
    const auto take = [&](uint32_t k) {
        if (!tried.insert(k).second) return;
        const uint8_t* r = record(k);
        if (static_cast<int8_t>(r[8]) < 0) return;  // its players disliked it
        if (skip && skip(text62(load32(r + 1), 5))) return;
        out.push_back(k);
    };
    for (const int window : {100, 200, 400, 800, 1600, RATINGS}) {
        const auto [first, last] = ratingRange(rating - window, rating + window);
        if (first >= last) continue;
        std::vector<uint32_t> candidates;
        if (list >= 0) {
            candidates = listed(list, first, last);
        } else if (last - first <= 4 * CHUNK * 64) {
            for (uint32_t k = first; k < last; ++k) candidates.push_back(k);
        } else {
            // Too many to list: drawn at random, a few times as many as wanted.
            for (std::size_t tries = 0; out.size() < n && tries < 50 * n; ++tries)
                take(first + static_cast<uint32_t>(rng() % (last - first)));
            if (out.size() >= n) break;
            continue;
        }
        // Shuffled as far as needed (Fisher and Yates, the same everywhere for a seed).
        for (std::size_t i = 0; i < candidates.size() && out.size() < n; ++i) {
            const auto j = i + static_cast<std::size_t>(rng() % (candidates.size() - i));
            std::swap(candidates[i], candidates[j]);
            take(candidates[i]);
        }
        if (out.size() >= n) break;
    }
    return out;
}

Puzzle PuzzleStore::puzzle(uint32_t number) const {
    const uint8_t* r = record(number);
    const auto parts = partsOf(r);
    Puzzle out;
    out.id         = text62(load32(r + 1), 5);
    out.rating     = load16(r + 5);
    out.deviation  = r[7];
    out.popularity = static_cast<int8_t>(r[8]);
    out.game       = text62(load48(r + 9), 8);
    out.fen        = fenOf(parts);
    ScanBoard scan;
    if (scan.setFen(out.fen, false)) {
        const bool wide = parts.flags & WIDE_CODES;
        for (int i = 0; i < parts.moves; ++i) {
            const int code = wide ? load16(parts.codes + 2 * i) : parts.codes[i];
            const auto m   = scan.decode(code);
            if (m.flags & MOVE_NULL) break;
            out.moves.push_back(uciOf(m));
            scan.play(m);
        }
    }
    for (int i = 0; i < parts.openings; ++i) {
        const auto id = load16(parts.opening_ids + 2 * i);
        if (id < openings_.size()) out.openings.push_back(openings_[id].name);
    }
    for (std::size_t t = 0; t < themes_.size(); ++t)
        if (inList(static_cast<int>(t), number)) out.themes.push_back(themes_[t].name);
    return out;
}

// ---------------------------------------------------------------------------
// The folder

fs::path newestPuzzleStore(const fs::path& folder) {
    std::error_code ec;
    fs::path best;
    uint64_t best_id = 0;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        const auto id = fileId(entry.path());
        if (id > best_id && entry.is_regular_file(ec)) {
            best_id = id;
            best    = entry.path();
        }
    }
    return best;
}

fs::path nextPuzzleStore(const fs::path& folder) {
    std::error_code ec;
    uint64_t most = 0;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        most      = std::max(most, fileId(entry.path()));
        auto name = entry.path().filename().string();
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0)
            most = std::max(most, fileId(entry.path().parent_path() / name.substr(0, name.size() - 4)));
    }
    return folder / ("puzzles-" + std::to_string(most + 1) + ".bin");
}

void tidyPuzzleFolder(const fs::path& folder) {
    const auto newest = newestPuzzleStore(folder);
    std::error_code ec;
    std::vector<fs::path> old;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        const auto name = entry.path().filename().string();
        const bool tmp =
            name.rfind("puzzles-", 0) == 0 && name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0;
        if (tmp || (fileId(entry.path()) > 0 && entry.path() != newest)) old.push_back(entry.path());
    }
    for (const auto& f : old) fs::remove(f, ec);
}

// ---------------------------------------------------------------------------
// The builder

PuzzleBuilder::PuzzleBuilder(fs::path build) : build_(std::move(build)) {
    std::error_code ec;
    fs::create_directories(build_, ec);
}

PuzzleBuilder::~PuzzleBuilder() { closeFiles(); }

fs::path PuzzleBuilder::bucketFile(int bucket) const { return build_ / ("bucket-" + std::to_string(bucket) + ".bin"); }

void PuzzleBuilder::closeFiles() {
    for (auto& f : files_) f.reset();
}

void PuzzleBuilder::fail(std::string why) {
    if (error_.empty()) error_ = std::move(why);
}

int PuzzleBuilder::nameOf(std::vector<std::string>& names, std::vector<std::pair<std::string, int>>& index,
                          std::string_view name, int most) {
    const auto it = std::lower_bound(index.begin(), index.end(), name,
                                     [](const auto& entry, std::string_view n) { return entry.first < n; });
    if (it != index.end() && it->first == name) return it->second;
    if (static_cast<int>(names.size()) >= most || name.size() > 255) return -1;
    const int id = static_cast<int>(names.size());
    names.emplace_back(name);
    index.insert(it, {std::string(name), id});
    return id;
}

bool PuzzleBuilder::encode(const PuzzleLine& line) {
    const auto id   = base62(line.id, 5);
    const auto game = base62(puzzleGameId(line.url), 8);
    if (!id || !game) return false;
    // The FEN's fields: the board read by the scan board (which checks it), the rest from the text.
    std::array<std::string_view, 6> fields{};
    std::size_t n = 0;
    words(line.fen, [&](std::string_view w) {
        if (n < fields.size()) fields[n] = w;
        ++n;
    });
    if (n < 4 || n > 6) return false;
    ScanBoard scan;
    if (!scan.setFen(line.fen, false)) return false;
    uint8_t flags = scan.sideToMove() == BLACK_SIDE ? BLACK_TO_MOVE : 0;
    // The board, before the moves are played on it: its squares, then its men in their order, a nibble each.
    const uint64_t occupied = scan.occupied();
    std::array<uint8_t, 32> men{};
    std::size_t man = 0;
    for (uint64_t bits = occupied; bits; bits &= bits - 1) {
        if (man >= 2 * men.size()) return false;  // more than 64 men: not a board
        const uint8_t value = scan.on(std::countr_zero(bits));
        men[man / 2] |= static_cast<uint8_t>(value << (4 * (man % 2)));
        ++man;
    }
    if (fields[2] != "-") {
        for (const char c : fields[2]) {
            switch (c) {
                case 'K':
                    flags |= CASTLING_K;
                    break;
                case 'Q':
                    flags |= CASTLING_Q;
                    break;
                case 'k':
                    flags |= CASTLING_k;
                    break;
                case 'q':
                    flags |= CASTLING_q;
                    break;
                default:
                    return false;  // Chess960's letters: not a puzzle of standard chess
            }
        }
    }
    int ep_file = -1;
    if (fields[3] != "-") {
        if (fields[3].size() != 2 || squareOf(fields[3][0], fields[3][1]) < 0) return false;
        ep_file = fields[3][0] - 'a';
        flags |= EN_PASSANT;
    }
    int halfmove = 0, fullmove = 1;
    if (n > 4 && !number(fields[4], halfmove)) return false;
    if (n > 5 && !number(fields[5], fullmove)) return false;
    // The moves, coded on the positions they are played from.
    std::vector<int> codes;
    bool wide = false;
    bool ok   = true;
    words(line.moves, [&](std::string_view uci) {
        if (!ok) return;
        const auto move = codedOf(scan, uci);
        if (!move || codes.size() >= MOST_MOVES) {
            ok = false;
            return;
        }
        if (scan.codeCount() > 256) wide = true;
        codes.push_back(scan.encode(*move));
        scan.play(*move);
    });
    if (!ok || codes.size() < 2) return false;
    if (wide) flags |= WIDE_CODES;
    std::vector<int> themes, openings;
    words(line.themes, [&](std::string_view t) {
        const int i = nameOf(themes_, theme_index_, t, MOST_THEMES);
        if (i >= 0 && std::find(themes.begin(), themes.end(), i) == themes.end()) themes.push_back(i);
    });
    words(line.openings, [&](std::string_view o) {
        const int i = nameOf(openings_, opening_index_, o, MOST_OPENINGS);
        if (i >= 0 && openings.size() < 8 && std::find(openings.begin(), openings.end(), i) == openings.end())
            openings.push_back(i);
    });

    // The record, then its themes (the store keeps them in its lists).
    auto& out = scratch_;
    out.clear();
    put8(out, 0);  // its length, below
    put32(out, static_cast<uint32_t>(*id));
    put16(out, static_cast<unsigned>(std::clamp(line.rating, 0, RATINGS - 1)));
    put8(out, static_cast<unsigned>(std::clamp(line.deviation, 0, 255)));
    put8(out, static_cast<uint8_t>(static_cast<int8_t>(std::clamp(line.popularity, -128, 127))));
    put48(out, *game);
    put8(out, flags);
    if (ep_file >= 0) put8(out, static_cast<unsigned>(ep_file));
    put8(out, static_cast<unsigned>(std::clamp(halfmove, 0, 255)));
    put16(out, static_cast<unsigned>(std::clamp(fullmove, 1, 65535)));
    put64(out, occupied);
    out.insert(out.end(), men.begin(), men.begin() + static_cast<std::ptrdiff_t>((man + 1) / 2));
    put8(out, static_cast<unsigned>(codes.size()));
    for (const int code : codes) {
        if (wide)
            put16(out, static_cast<unsigned>(code));
        else
            put8(out, static_cast<unsigned>(code));
    }
    put8(out, static_cast<unsigned>(openings.size()));
    for (const int o : openings) put16(out, static_cast<unsigned>(o));
    if (out.size() > 255) return false;
    out[0] = static_cast<uint8_t>(out.size());
    put8(out, static_cast<unsigned>(themes.size()));
    for (const int t : themes) put8(out, static_cast<unsigned>(t));
    return true;
}

void PuzzleBuilder::line(std::string_view text) {
    if (!error_.empty()) return;
    PuzzleLine puzzle;
    if (!readPuzzleLine(text, puzzle)) return;  // the header, an empty line
    ++lines_;
    if (!encode(puzzle)) return;
    const int bucket = std::min(std::clamp(puzzle.rating, 0, RATINGS - 1) / BUCKET_POINTS, BUCKETS - 1);
    auto& file       = files_[bucket];
    if (!file) {
        file.reset(tour::openFile(bucketFile(bucket), "ab"));
        if (!file) return fail("cannot write " + bucketFile(bucket).string());
    }
    if (std::fwrite(scratch_.data(), 1, scratch_.size(), file.get()) != scratch_.size())
        return fail("cannot write " + bucketFile(bucket).string() + " (is the disk full?)");
    lengths_[bucket] += scratch_.size();
    ++kept_;
}

nlohmann::json PuzzleBuilder::mark() const {
    auto buckets = nlohmann::json::array();
    for (const auto length : lengths_) buckets.push_back(length);
    return {{"lines", lines_}, {"kept", kept_}, {"themes", themes_}, {"openings", openings_}, {"buckets", buckets}};
}

void PuzzleBuilder::commit() {
    if (!error_.empty()) return;
    for (int b = 0; b < BUCKETS; ++b) {
        if (files_[b] && !tour::syncFile(files_[b].get())) return fail("cannot write " + bucketFile(b).string());
    }
}

void PuzzleBuilder::restore(const nlohmann::json& state) {
    closeFiles();
    error_.clear();
    try {
        lines_              = state.at("lines").get<uint64_t>();
        kept_               = state.at("kept").get<uint64_t>();
        themes_             = state.at("themes").get<std::vector<std::string>>();
        openings_           = state.at("openings").get<std::vector<std::string>>();
        const auto& buckets = state.at("buckets");
        if (!buckets.is_array() || buckets.size() != BUCKETS) throw std::runtime_error("its files");
        for (int b = 0; b < BUCKETS; ++b) {
            lengths_[b] = buckets[static_cast<std::size_t>(b)].get<uint64_t>();
            std::error_code ec;
            const auto file = bucketFile(b);
            const auto size = fs::exists(file, ec) ? fs::file_size(file, ec) : 0;
            if (size < lengths_[b]) throw std::runtime_error(file.string() + " is shorter than its state");
            if (lengths_[b] == 0)
                fs::remove(file, ec);
            else if (size > lengths_[b])
                fs::resize_file(file, lengths_[b], ec);  // the puzzles written after the state
            if (ec) throw std::runtime_error("cannot cut " + file.string());
        }
    } catch (const std::exception& e) {
        lines_ = kept_ = 0;
        themes_.clear();
        openings_.clear();
        std::fill(std::begin(lengths_), std::end(lengths_), 0);
        fail(std::string("the puzzles read so far cannot be gone on with: ") + e.what());
    }
    theme_index_.clear();
    opening_index_.clear();
    for (std::size_t i = 0; i < themes_.size(); ++i) theme_index_.emplace_back(themes_[i], static_cast<int>(i));
    for (std::size_t i = 0; i < openings_.size(); ++i) opening_index_.emplace_back(openings_[i], static_cast<int>(i));
    std::sort(theme_index_.begin(), theme_index_.end());
    std::sort(opening_index_.begin(), opening_index_.end());
}

void PuzzleBuilder::startAgain() {
    closeFiles();
    std::error_code ec;
    for (int b = 0; b < BUCKETS; ++b) fs::remove(bucketFile(b), ec);
    error_.clear();
    lines_ = kept_ = 0;
    std::fill(std::begin(lengths_), std::end(lengths_), 0);
    themes_.clear();
    openings_.clear();
    theme_index_.clear();
    opening_index_.clear();
}

std::string PuzzleBuilder::finish(const fs::path& store, const std::string& date, std::atomic<uint64_t>* done,
                                  std::atomic<uint64_t>* total) {
    if (!error_.empty()) return error_;
    closeFiles();
    if (kept_ > UINT32_MAX) return "too many puzzles for one store";
    if (total) *total = kept_;
    if (done) *done = 0;
    auto tmp = store;
    tmp += ".tmp";
    std::vector<uint64_t> blocks;
    std::vector<uint32_t> per_rating(RATINGS, 0);
    std::vector<ListBuilder> lists(themes_.size() + openings_.size());
    uint32_t number  = 0;
    uint64_t records = 0;  // their bytes
    Writer w(tmp);
    {
        const uint8_t blank[HEADER] = {};
        w.put(blank, HEADER);  // written again at the end
    }
    // Each bucket in its turn, sorted by rating then id in memory.
    for (int b = 0; b < BUCKETS && w.error().empty(); ++b) {
        if (lengths_[b] == 0) continue;
        std::vector<uint8_t> data(lengths_[b]);
        {
            const tour::File in{tour::openFile(bucketFile(b), "rb")};
            if (!in) return "cannot read " + bucketFile(b).string();
            const auto got = std::fread(data.data(), 1, data.size(), in.get());
            if (got != data.size()) return bucketFile(b).string() + " is shorter than its state";
        }
        std::vector<uint32_t> at;  // where each record starts
        for (std::size_t o = 0; o < data.size();) {
            const std::size_t length = data[o];
            if (length < 16 || o + length >= data.size() || o + length + 1 + data[o + length] > data.size())
                return bucketFile(b).string() + " is damaged";
            at.push_back(static_cast<uint32_t>(o));
            o += length + 1 + data[o + length];
        }
        std::sort(at.begin(), at.end(), [&](uint32_t x, uint32_t y) {
            const auto rx = load16(&data[x + 5]), ry = load16(&data[y + 5]);
            return rx != ry ? rx < ry : load32(&data[x + 1]) < load32(&data[y + 1]);
        });
        for (const auto o : at) {
            const uint8_t* r = &data[o];
            if (number % BLOCK == 0) blocks.push_back(records);
            w.put(r, r[0]);
            records += r[0];
            ++per_rating[load16(r + 5)];
            const uint8_t* themes = r + r[0];
            for (int t = 0; t < themes[0]; ++t)
                if (themes[1 + t] < themes_.size()) lists[themes[1 + t]].add(number);
            const auto parts = partsOf(r);
            for (int i = 0; i < parts.openings; ++i) {
                const auto id = load16(parts.opening_ids + 2 * i);
                if (id < openings_.size()) lists[themes_.size() + id].add(number);
            }
            ++number;
            if (done) *done = number;
        }
    }
    blocks.push_back(records);
    // The names, the first puzzle of each rating, the blocks, the lists.
    const uint64_t names_at = HEADER + records;
    std::vector<uint8_t> bytes;
    for (const auto* names : {&themes_, &openings_}) {
        for (const auto& name : *names) {
            put8(bytes, static_cast<unsigned>(name.size()));
            bytes.insert(bytes.end(), name.begin(), name.end());
        }
    }
    const uint64_t ratings_at = names_at + bytes.size();
    uint32_t first            = 0;
    for (int r = 0; r <= RATINGS; ++r) {
        put32(bytes, first);
        if (r < RATINGS) first += per_rating[static_cast<std::size_t>(r)];
    }
    const uint64_t blocks_at = names_at + bytes.size();
    for (const auto b : blocks) put64(bytes, b);
    const uint64_t lists_at = names_at + bytes.size();
    uint64_t offset         = lists_at + 16 * lists.size();
    for (const auto& list : lists) {
        put64(bytes, offset);
        put32(bytes, list.count);
        put32(bytes, static_cast<uint32_t>(list.bytes.size()));
        offset += list.skips.size() + list.bytes.size();
    }
    w.put(bytes);
    for (const auto& list : lists) {
        w.put(list.skips);
        w.put(list.bytes);
    }
    std::vector<uint8_t> header;
    header.insert(header.end(), MAGIC, MAGIC + sizeof MAGIC);
    put64(header, number);
    put64(header, lines_);
    put64(header, lines_ - std::min(lines_, kept_));
    for (std::size_t i = 0; i < 16; ++i) put8(header, i < date.size() ? static_cast<uint8_t>(date[i]) : 0);
    put32(header, static_cast<uint32_t>(themes_.size()));
    put32(header, static_cast<uint32_t>(openings_.size()));
    put64(header, names_at);
    put64(header, ratings_at);
    put64(header, blocks_at);
    put64(header, HEADER);
    put64(header, lists_at);
    put64(header, blocks.size());
    put64(header, offset);  // the file's size
    header.resize(HEADER, 0);
    w.rewind();
    w.put(header);
    if (auto why = w.close(); !why.empty()) {
        std::error_code ec;
        fs::remove(tmp, ec);
        return why;
    }
    std::error_code ec;
    fs::rename(tmp, store, ec);
    if (ec) return "cannot write " + store.string() + ": " + ec.message();
    for (int b = 0; b < BUCKETS; ++b) fs::remove(bucketFile(b), ec);
    std::fill(std::begin(lengths_), std::end(lengths_), 0);
    return {};
}

}  // namespace fcg::study
