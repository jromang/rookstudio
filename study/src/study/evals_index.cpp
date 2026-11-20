#include "evals_index.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstring>
#include <limits>
#include <queue>
#include <random>
#include <stdexcept>
#include <system_error>

#include <chess.hpp>
#include <tour/chess_text.hpp>
#include <tour/files.hpp>

namespace fcg::study {

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
// Little-endian words

uint32_t load32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}
uint64_t load64(const uint8_t* p) {
    return static_cast<uint64_t>(load32(p)) | static_cast<uint64_t>(load32(p + 4)) << 32;
}
uint64_t load56(const uint8_t* p) {
    return static_cast<uint64_t>(load32(p)) | static_cast<uint64_t>(p[4]) << 32 | static_cast<uint64_t>(p[5]) << 40 |
           static_cast<uint64_t>(p[6]) << 48;
}
void store32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
void store64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
void store56(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 7; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

// ---------------------------------------------------------------------------
// The files

constexpr char INDEX_MAGIC[8]      = {'R', 'S', 'E', 'V', 'A', 'L', 'S', '1'};
constexpr char RUN_MAGIC[8]        = {'R', 'S', 'E', 'V', 'R', 'U', 'N', '1'};
constexpr std::size_t INDEX_HEADER = 96;
constexpr std::size_t INDEX_RECORD = 9;  // the key's 32 bits after the bucket's, then the evaluation
constexpr std::size_t RUN_HEADER   = 32;
constexpr std::size_t RUN_RECORD   = 12;  // the key's first 56 bits, then the evaluation
constexpr int RUN_BITS             = 16;  // a run's buckets: the key's first 16 bits
constexpr std::size_t LOG_RECORD   = 12;
constexpr int MIN_BITS = 8, MAX_BITS = 24;

// The index's buckets: about 16 to 32 positions each.
int bucketBits(uint64_t positions) {
    int bits = MIN_BITS;
    while (bits < MAX_BITS && (positions >> (bits + 1)) >= 16) ++bits;
    return bits;
}

// The key's 56 bits after `bits` bucket bits: the next 32 kept beside the evaluation.
uint32_t restOf(uint64_t key56, int bits) { return static_cast<uint32_t>(key56 >> (24 - bits)); }

// A file written whole, through a buffer.
class Writer {
   public:
    explicit Writer(const fs::path& file) : file_(tour::openFile(file, "wb")) {
        if (file_) std::setvbuf(file_, nullptr, _IOFBF, 1 << 20);
    }
    ~Writer() {
        if (file_) std::fclose(file_);
    }
    Writer(const Writer&)            = delete;
    Writer& operator=(const Writer&) = delete;
    [[nodiscard]] bool ok() const noexcept { return file_ && ok_; }
    void put(const void* data, std::size_t n) {
        if (ok() && std::fwrite(data, 1, n, file_) != n) ok_ = false;
    }
    void at(uint64_t offset) {
        if (!ok()) return;
#ifdef _WIN32
        if (_fseeki64(file_, static_cast<long long>(offset), SEEK_SET) != 0) ok_ = false;
#else
        if (fseeko(file_, static_cast<off_t>(offset), SEEK_SET) != 0) ok_ = false;
#endif
    }
    // Synced to the disk and closed: false when anything failed.
    bool close() {
        if (!file_) return false;
        if (ok_ && !tour::syncFile(file_)) ok_ = false;
        if (std::fclose(file_) != 0) ok_ = false;
        file_ = nullptr;
        return ok_;
    }

   private:
    std::FILE* file_ = nullptr;
    bool ok_         = true;
};

// Renames `from` over `to`; false when it cannot.
bool renameOver(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::rename(from, to, ec);
    return !ec;
}

// The id of a builder's file, "run-12.bin" → 12; 0 when it is not one of `prefix`.
uint64_t fileId(const fs::path& file, std::string_view prefix) {
    const auto name = file.filename().string();
    if (name.size() <= prefix.size() + 4 || name.compare(0, prefix.size(), prefix) != 0 ||
        name.compare(name.size() - 4, 4, ".bin") != 0)
        return 0;
    uint64_t id    = 0;
    const auto beg = name.data() + prefix.size(), end = name.data() + name.size() - 4;
    const auto r = std::from_chars(beg, end, id);
    return r.ec == std::errc() && r.ptr == end ? id : 0;
}

// A run read from its start, a record at a time, through a buffer.
class RunReader {
   public:
    explicit RunReader(const fs::path& file) : file_(tour::openFile(file, "rb")) {
        if (!file_) return;
        uint8_t header[RUN_HEADER];
        if (std::fread(header, 1, RUN_HEADER, file_) != RUN_HEADER || std::memcmp(header, RUN_MAGIC, 8) != 0) return;
        count_ = load64(header + 8);
        ok_    = true;
    }
    ~RunReader() {
        if (file_) std::fclose(file_);
    }
    RunReader(const RunReader&)            = delete;
    RunReader& operator=(const RunReader&) = delete;
    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] uint64_t count() const noexcept { return count_; }
    // The next record; false at the run's end (or when it cannot be read: ok() false).
    bool next() {
        if (!ok_ || read_ >= count_) return false;
        if (at_ >= filled_) {
            const auto want = static_cast<std::size_t>(std::min<uint64_t>(count_ - read_, buffer_.size() / RUN_RECORD));
            filled_         = std::fread(buffer_.data(), 1, want * RUN_RECORD, file_);
            at_             = 0;
            if (filled_ != want * RUN_RECORD) {
                ok_ = false;
                return false;
            }
        }
        record_ = buffer_.data() + at_;
        at_ += RUN_RECORD;
        ++read_;
        return true;
    }
    [[nodiscard]] uint64_t key() const { return load56(record_); }
    [[nodiscard]] PackedEval eval() const {
        PackedEval e;
        std::memcpy(e.bytes, record_ + 7, 5);
        return e;
    }

   private:
    std::FILE* file_ = nullptr;
    bool ok_         = false;
    uint64_t count_ = 0, read_ = 0;
    std::vector<uint8_t> buffer_ = std::vector<uint8_t>(RUN_RECORD * 21846);  // 256 KB
    std::size_t at_ = 0, filled_ = 0;
    const uint8_t* record_ = nullptr;
};

// ---------------------------------------------------------------------------
// A line of the database

bool expect(std::string_view s, std::size_t& at, std::string_view token) {
    if (s.compare(at, token.size(), token) != 0) return false;
    at += token.size();
    return true;
}

bool readInt(std::string_view s, std::size_t& at, int64_t& out) {
    const bool negative = at < s.size() && s[at] == '-';
    if (negative) ++at;
    const std::size_t from = at;
    int64_t v              = 0;
    while (at < s.size() && s[at] >= '0' && s[at] <= '9' && at - from < 18) v = v * 10 + (s[at++] - '0');
    if (at == from) return false;
    out = negative ? -v : v;
    return true;
}

void take(EvalsLine& out, bool& any, int64_t depth, int64_t knodes, bool mate, int64_t score, std::string_view move,
          int lines) {
    if (any && (depth < out.depth || (depth == out.depth && static_cast<uint64_t>(knodes) <= out.knodes))) return;
    out.depth  = static_cast<int>(std::clamp<int64_t>(depth, 0, 1 << 20));
    out.knodes = static_cast<uint64_t>(std::max<int64_t>(0, knodes));
    out.cp     = mate ? 0 : static_cast<int>(std::clamp<int64_t>(score, -1000000, 1000000));
    out.mate   = mate ? static_cast<int>(std::clamp<int64_t>(score, -1000000, 1000000)) : 0;
    out.move   = move;
    out.lines  = lines;
    any        = true;
}

// The database's lines as Lichess writes them: {"fen":…,"evals":[{"pvs":[{"cp":…,"line":…},…],"knodes":…,
// "depth":…},…]}, read without a JSON parser.
bool readFast(std::string_view s, EvalsLine& out, std::vector<int>* depths) {
    std::size_t at = 0;
    if (!expect(s, at, "{\"fen\":\"")) return false;
    const auto fen_end = s.find('"', at);
    if (fen_end == std::string_view::npos) return false;
    out.fen = s.substr(at, fen_end - at);
    at      = fen_end + 1;
    if (!expect(s, at, ",\"evals\":[")) return false;
    bool any = false;
    for (;;) {
        if (!expect(s, at, "{\"pvs\":[")) return false;
        bool scored = false, mate = false;
        int64_t score = 0;
        std::string_view move;
        int lines = 0;
        for (;;) {
            if (!expect(s, at, "{\"")) return false;
            bool is_mate = false;
            if (expect(s, at, "mate\":"))
                is_mate = true;
            else if (!expect(s, at, "cp\":"))
                return false;
            int64_t v = 0;
            if (!readInt(s, at, v) || !expect(s, at, ",\"line\":\"")) return false;
            const auto close = s.find('"', at);
            if (close == std::string_view::npos) return false;
            if (lines == 0 && !(is_mate && v == 0)) {
                const auto line = s.substr(at, close - at);
                move            = line.substr(0, line.find(' '));
                mate            = is_mate;
                score           = v;
                scored          = true;
            }
            ++lines;
            at = close + 1;
            if (!expect(s, at, "}")) return false;
            if (expect(s, at, ",")) continue;
            if (expect(s, at, "]")) break;
            return false;
        }
        int64_t knodes = 0, depth = 0;
        if (!expect(s, at, ",\"knodes\":") || !readInt(s, at, knodes)) return false;
        if (!expect(s, at, ",\"depth\":") || !readInt(s, at, depth)) return false;
        if (!expect(s, at, "}")) return false;
        ++out.searches;
        if (depths) depths->push_back(static_cast<int>(depth));
        if (scored) take(out, any, depth, knodes, mate, score, move, lines);
        if (expect(s, at, ",")) continue;
        if (expect(s, at, "]")) break;
        return false;
    }
    if (!expect(s, at, "}")) return false;
    while (at < s.size() && (s[at] == ' ' || s[at] == '\r')) ++at;
    return at == s.size() && any;
}

// Another order of the fields, or spaces: read as JSON (the FEN and the move then point into `keep`).
bool readSlow(std::string_view s, EvalsLine& out, std::vector<int>* depths, std::string (&keep)[2]) {
    const auto j = nlohmann::json::parse(s, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("fen") || !j["fen"].is_string() || !j.contains("evals") ||
        !j["evals"].is_array())
        return false;
    keep[0]  = j["fen"].get<std::string>();
    out.fen  = keep[0];
    bool any = false;
    for (const auto& e : j["evals"]) {
        if (!e.is_object()) continue;
        const int64_t depth  = e.contains("depth") && e["depth"].is_number_integer() ? e["depth"].get<int64_t>() : 0;
        const int64_t knodes = e.contains("knodes") && e["knodes"].is_number_integer() ? e["knodes"].get<int64_t>() : 0;
        ++out.searches;
        if (depths) depths->push_back(static_cast<int>(depth));
        if (!e.contains("pvs") || !e["pvs"].is_array() || e["pvs"].empty() || !e["pvs"][0].is_object()) continue;
        const auto& pv = e["pvs"][0];
        bool mate      = false;
        int64_t score  = 0;
        if (pv.contains("mate") && pv["mate"].is_number_integer() && pv["mate"].get<int64_t>() != 0) {
            mate  = true;
            score = pv["mate"].get<int64_t>();
        } else if (pv.contains("cp") && pv["cp"].is_number_integer()) {
            score = pv["cp"].get<int64_t>();
        } else {
            continue;
        }
        const bool deeper =
            !any || depth > out.depth || (depth == out.depth && static_cast<uint64_t>(knodes) > out.knodes);
        if (deeper && pv.contains("line") && pv["line"].is_string()) {
            const auto line = pv["line"].get<std::string>();
            keep[1]         = line.substr(0, line.find(' '));
        } else if (deeper) {
            keep[1].clear();
        }
        take(out, any, depth, knodes, mate, score, deeper ? std::string_view(keep[1]) : out.move,
             static_cast<int>(e["pvs"].size()));
    }
    return any;
}

// A man as the position's bytes have it: 1 to 6 White's pawn, knight, bishop, rook, queen, king; 7 to 12 Black's.
int nibbleOf(char c) {
    switch (c) {
        case 'P':
            return 1;
        case 'N':
            return 2;
        case 'B':
            return 3;
        case 'R':
            return 4;
        case 'Q':
            return 5;
        case 'K':
            return 6;
        case 'p':
            return 7;
        case 'n':
            return 8;
        case 'b':
            return 9;
        case 'r':
            return 10;
        case 'q':
            return 11;
        case 'k':
            return 12;
        default:
            return 0;
    }
}

// The position's bytes from a board of men (0 for none, else a nibble), the side (0 White) and the rights (K 1, Q 2,
// k 4, q 8).
EvalsPosition encode(const uint8_t (&men)[64], int side, unsigned rights) {
    EvalsPosition out;
    uint64_t occupied = 0;
    for (int square = 0; square < 64; ++square)
        if (men[square]) occupied |= uint64_t{1} << square;
    store64(out.bytes, occupied);
    out.bytes[8] = static_cast<uint8_t>(side | rights << 1);
    int n        = 0;
    for (uint64_t rest = occupied; rest; rest &= rest - 1) {
        const auto nibble = men[std::countr_zero(rest)];
        if (n % 2 == 0)
            out.bytes[9 + n / 2] = nibble;
        else
            out.bytes[9 + n / 2] |= static_cast<uint8_t>(nibble << 4);
        ++n;
    }
    out.size = static_cast<uint8_t>(9 + (n + 1) / 2);  // up to 64 men: a board set up by hand may hold more than 32
    return out;
}

// The position of a FEN the fast way does not read (castling rights a board does not allow, or in Shredder's letters),
// as chess.hpp reads it.
bool boardPosition(std::string_view fen, EvalsPosition& out) {
    if (!tour::readableFen(fen)) return false;
    chess::Board board;
    if (!board.setFen(fen)) return false;
    out = evalsPosition(board);
    return true;
}

uint64_t rotl(uint64_t x, int b) { return (x << b) | (x >> (64 - b)); }

}  // namespace

// ---------------------------------------------------------------------------

bool readEvalsLine(std::string_view line, EvalsLine& out, std::vector<int>* depths) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
    out            = EvalsLine{};
    const auto had = depths ? depths->size() : 0;
    if (readFast(line, out, depths)) return true;
    if (depths) depths->resize(had);
    // Kept in the line's own storage: a thread's, valid until the next line it reads this way.
    thread_local std::string keep[2];
    out      = EvalsLine{};
    out.slow = true;
    return readSlow(line, out, depths, keep);
}

bool EvalsPosition::operator==(const EvalsPosition& other) const {
    return size == other.size && std::memcmp(bytes, other.bytes, size) == 0;
}

bool evalsPosition(std::string_view fen, EvalsPosition& out) {
    uint8_t men[64] = {};
    char board[64]  = {};
    std::size_t at  = 0;
    int rank = 7, file = 0;
    for (; at < fen.size() && fen[at] != ' '; ++at) {
        const char c = fen[at];
        if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) return false;
        } else if (c == '/') {
            if (file != 8 || rank == 0) return false;
            --rank;
            file = 0;
        } else {
            const int nibble = nibbleOf(c);
            if (nibble == 0 || file >= 8) return false;
            men[rank * 8 + file]   = static_cast<uint8_t>(nibble);
            board[rank * 8 + file] = c;
            ++file;
        }
    }
    if (rank != 0 || file != 8 || at + 2 >= fen.size()) return false;
    const char side = fen[at + 1];
    if ((side != 'w' && side != 'b') || fen[at + 2] != ' ') return false;
    at += 3;
    const auto end      = fen.find(' ', at);
    const auto castling = fen.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
    if (castling.empty()) return false;
    unsigned rights = 0;
    if (castling != "-") {
        // Standard chess's rights, each where the king and its rook stand; anything else as chess.hpp reads it.
        for (const char c : castling) {
            switch (c) {
                case 'K':
                    if (board[4] != 'K' || board[7] != 'R') return boardPosition(fen, out);
                    rights |= 1;
                    break;
                case 'Q':
                    if (board[4] != 'K' || board[0] != 'R') return boardPosition(fen, out);
                    rights |= 2;
                    break;
                case 'k':
                    if (board[60] != 'k' || board[63] != 'r') return boardPosition(fen, out);
                    rights |= 4;
                    break;
                case 'q':
                    if (board[60] != 'k' || board[56] != 'r') return boardPosition(fen, out);
                    rights |= 8;
                    break;
                default:
                    return boardPosition(fen, out);
            }
        }
    }
    out = encode(men, side == 'w' ? 0 : 1, rights);  // the en passant square left out
    return true;
}

EvalsPosition evalsPosition(const chess::Board& board) {
    uint8_t men[64] = {};
    for (auto rest = board.occ().getBits(); rest; rest &= rest - 1) {
        const int square = std::countr_zero(rest);
        const auto piece = board.at(chess::Square(square));
        const int kind   = static_cast<int>(piece.type());  // pawn 0 … king 5
        men[square]      = static_cast<uint8_t>(1 + kind + (piece.color() == chess::Color::BLACK ? 6 : 0));
    }
    using Side        = chess::Board::CastlingRights::Side;
    const auto rights = board.castlingRights();
    unsigned mask     = 0;
    if (rights.has(chess::Color::WHITE, Side::KING_SIDE)) mask |= 1;
    if (rights.has(chess::Color::WHITE, Side::QUEEN_SIDE)) mask |= 2;
    if (rights.has(chess::Color::BLACK, Side::KING_SIDE)) mask |= 4;
    if (rights.has(chess::Color::BLACK, Side::QUEEN_SIDE)) mask |= 8;
    return encode(men, board.sideToMove() == chess::Color::WHITE ? 0 : 1, mask);
}

int evalsMen(const EvalsPosition& position) { return std::popcount(load64(position.bytes)); }

EvalsSecret newEvalsSecret() {
    std::random_device device;
    const auto now = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::seed_seq seed{
        device(), device(), device(), device(), static_cast<unsigned>(now), static_cast<unsigned>(now >> 32)};
    std::mt19937_64 rng(seed);
    EvalsSecret s;
    s.k0 = rng();
    s.k1 = rng();
    return s;
}

uint64_t sipHash24(const uint8_t* data, std::size_t size, uint64_t k0, uint64_t k1) {
    uint64_t v0 = 0x736f6d6570736575ULL ^ k0, v1 = 0x646f72616e646f6dULL ^ k1, v2 = 0x6c7967656e657261ULL ^ k0,
             v3      = 0x7465646279746573ULL ^ k1;
    const auto round = [&] {
        v0 += v1;
        v1 = rotl(v1, 13);
        v1 ^= v0;
        v0 = rotl(v0, 32);
        v2 += v3;
        v3 = rotl(v3, 16);
        v3 ^= v2;
        v0 += v3;
        v3 = rotl(v3, 21);
        v3 ^= v0;
        v2 += v1;
        v1 = rotl(v1, 17);
        v1 ^= v2;
        v2 = rotl(v2, 32);
    };
    const std::size_t whole = size - size % 8;
    for (std::size_t i = 0; i < whole; i += 8) {
        const uint64_t m = load64(data + i);
        v3 ^= m;
        round();
        round();
        v0 ^= m;
    }
    uint64_t last = static_cast<uint64_t>(size) << 56;
    for (std::size_t i = whole; i < size; ++i) last |= static_cast<uint64_t>(data[i]) << (8 * (i - whole));
    v3 ^= last;
    round();
    round();
    v0 ^= last;
    v2 ^= 0xff;
    for (int i = 0; i < 4; ++i) round();
    return v0 ^ v1 ^ v2 ^ v3;
}

uint64_t evalsKey(const EvalsPosition& position, const EvalsSecret& secret) {
    return sipHash24(position.bytes, position.size, secret.k0, secret.k1);
}

int evalsMen(std::string_view fen) {
    int men = 0;
    for (const char c : fen) {
        if (c == ' ') break;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) ++men;
    }
    return men;
}

PackedEval packEval(int cp, int mate, int depth, std::string_view move) {
    int value = 0;
    if (mate > 0)
        value = 32000 - std::min(mate, 999);
    else if (mate < 0)
        value = -32000 + std::min(-mate, 999);
    else
        value = std::clamp(cp, -30000, 30000);
    const auto v      = static_cast<uint16_t>(static_cast<int16_t>(value));
    uint16_t m        = 0;
    const auto square = [](char f, char r) {
        return (f >= 'a' && f <= 'h' && r >= '1' && r <= '8') ? (r - '1') * 8 + (f - 'a') : -1;
    };
    if (move.size() >= 4) {
        const int from = square(move[0], move[1]), to = square(move[2], move[3]);
        int promotion = 0;
        if (move.size() >= 5) {
            static constexpr std::string_view PIECES = "nbrq";
            const auto p                             = PIECES.find(move[4]);
            promotion                                = p == std::string_view::npos ? -1 : static_cast<int>(p) + 1;
        }
        if (from >= 0 && to >= 0 && from != to && promotion >= 0)
            m = static_cast<uint16_t>(from | to << 6 | promotion << 12);
    }
    PackedEval out;
    out.bytes[0] = static_cast<uint8_t>(v);
    out.bytes[1] = static_cast<uint8_t>(v >> 8);
    out.bytes[2] = static_cast<uint8_t>(std::clamp(depth, 0, 255));
    out.bytes[3] = static_cast<uint8_t>(m);
    out.bytes[4] = static_cast<uint8_t>(m >> 8);
    return out;
}

int packedDepth(const PackedEval& packed) { return packed.bytes[2]; }

OfflineEval unpackEval(const PackedEval& packed) {
    OfflineEval out;
    const auto value = static_cast<int16_t>(static_cast<uint16_t>(packed.bytes[0] | packed.bytes[1] << 8));
    if (value > 30500)
        out.mate = 32000 - value;
    else if (value < -30500)
        out.mate = -(value + 32000);
    else
        out.cp = value;
    out.depth    = packed.bytes[2];
    const auto m = static_cast<uint16_t>(packed.bytes[3] | packed.bytes[4] << 8);
    if (m != 0) {
        const int from = m & 63, to = (m >> 6) & 63, promotion = m >> 12;
        out.best = {static_cast<char>('a' + from % 8), static_cast<char>('1' + from / 8),
                    static_cast<char>('a' + to % 8), static_cast<char>('1' + to / 8)};
        if (promotion >= 1 && promotion <= 4) out.best += "nbrq"[promotion - 1];
    }
    return out;
}

// ---------------------------------------------------------------------------
// The index

EvalsIndex::EvalsIndex(const fs::path& file) : file_(file) {
    const auto* d  = file_.data();
    const auto n   = file_.size();
    const auto bad = [&](const char* why) {
        return std::runtime_error(file.string() + " is not an index of evaluations (" + why + ")");
    };
    if (n < INDEX_HEADER || std::memcmp(d, INDEX_MAGIC, 8) != 0) throw bad("its header");
    if (load32(d + 8) != 1) throw bad("a later version");
    bits_    = static_cast<int>(load32(d + 12));
    count_   = load64(d + 16);
    lines_   = load64(d + 24);
    min_men_ = d[32];
    date_.assign(reinterpret_cast<const char*>(d + 40), 16);
    date_.resize(std::strlen(date_.c_str()));
    secret_ = {load64(d + 56), load64(d + 64)};
    if (bits_ < MIN_BITS || bits_ > MAX_BITS || count_ > std::numeric_limits<uint32_t>::max()) throw bad("its sizes");
    const uint64_t buckets = uint64_t{1} << bits_;
    if (n != INDEX_HEADER + INDEX_RECORD * count_ + 4 * (buckets + 1)) throw bad("cut short");
    records_ = d + INDEX_HEADER;
    fence_   = records_ + INDEX_RECORD * count_;
    if (load32(fence_) != 0 || load32(fence_ + 4 * buckets) != count_) throw bad("its table");
    file_.adviseRandom();  // a lookup reads a word of the table and a few records, nothing around them
}

std::optional<OfflineEval> EvalsIndex::find(const EvalsPosition& position) const {
    const auto found = findKey(evalsKey(position, secret_));
    if (!found) return std::nullopt;
    return unpackEval(*found);
}

std::optional<PackedEval> EvalsIndex::findKey(uint64_t key) const {
    const uint64_t key56  = key >> 8;
    const uint64_t bucket = key56 >> (56 - bits_);
    const uint32_t rest   = restOf(key56, bits_);
    uint32_t lo = load32(fence_ + 4 * bucket), hi = load32(fence_ + 4 * (bucket + 1));
    if (hi > count_ || lo > hi) return std::nullopt;
    const uint32_t end = hi;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (load32(records_ + INDEX_RECORD * mid) < rest)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= end || load32(records_ + INDEX_RECORD * lo) != rest) return std::nullopt;
    PackedEval packed;
    std::memcpy(packed.bytes, records_ + INDEX_RECORD * lo + 4, 5);
    return packed;
}

// ---------------------------------------------------------------------------
// A run

EvalsRun::EvalsRun(const fs::path& file) : file_(file) {
    const auto* d = file_.data();
    const auto n  = file_.size();
    if (n < RUN_HEADER || std::memcmp(d, RUN_MAGIC, 8) != 0)
        throw std::runtime_error(file.string() + " is not a run of evaluations");
    count_  = load64(d + 8);
    secret_ = {load64(d + 16), load64(d + 24)};
    if (count_ > std::numeric_limits<uint32_t>::max() ||
        n != RUN_HEADER + RUN_RECORD * count_ + 4 * ((uint64_t{1} << RUN_BITS) + 1))
        throw std::runtime_error(file.string() + " is cut short");
    records_          = d + RUN_HEADER;
    const auto* fence = records_ + RUN_RECORD * count_;
    fence_.resize((std::size_t{1} << RUN_BITS) + 1);
    for (std::size_t i = 0; i < fence_.size(); ++i) fence_[i] = load32(fence + 4 * i);
    file_.adviseRandom();
}

std::optional<PackedEval> EvalsRun::find(const EvalsPosition& position) const {
    return findKey(evalsKey(position, secret_));
}

std::optional<PackedEval> EvalsRun::findKey(uint64_t key) const {
    const uint64_t key56  = key >> 8;
    const uint64_t bucket = key56 >> (56 - RUN_BITS);
    uint32_t lo = fence_[bucket], hi = fence_[bucket + 1];
    if (hi > count_ || lo > hi) return std::nullopt;
    const uint32_t end = hi;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (load56(records_ + RUN_RECORD * mid) < key56)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= end || load56(records_ + RUN_RECORD * lo) != key56) return std::nullopt;
    PackedEval packed;
    std::memcpy(packed.bytes, records_ + RUN_RECORD * lo + 7, 5);
    return packed;
}

// ---------------------------------------------------------------------------

EvalsLookup::EvalsLookup(std::unique_ptr<EvalsIndex> index, std::vector<std::unique_ptr<EvalsRun>> runs)
    : index_(std::move(index)), runs_(std::move(runs)) {}

std::optional<OfflineEval> EvalsLookup::find(const EvalsPosition& position) const {
    if (index_) {
        if (auto found = index_->find(position)) return found;
    }
    // The runs of one preparation share its secret: the key made once.
    std::optional<PackedEval> best;
    std::optional<EvalsSecret> secret;
    uint64_t key = 0;
    for (const auto& run : runs_) {
        if (!secret || !(*secret == run->secret())) {
            secret = run->secret();
            key    = evalsKey(position, *secret);
        }
        const auto found = run->findKey(key);
        if (found && (!best || packedDepth(*found) > packedDepth(*best))) best = found;
    }
    if (best) return unpackEval(*best);
    return std::nullopt;
}

uint64_t EvalsLookup::positions() const noexcept {
    if (index_) return index_->positions();
    uint64_t n = 0;
    for (const auto& run : runs_) n += run->positions();
    return n;
}

fs::path newestEvalsIndex(const fs::path& folder) {
    std::error_code ec;
    fs::path best;
    uint64_t best_id = 0;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        const auto id = fileId(entry.path(), "index-");
        if (id > best_id && entry.is_regular_file(ec)) {
            best_id = id;
            best    = entry.path();
        }
    }
    return best;
}

fs::path nextEvalsIndex(const fs::path& folder) {
    std::error_code ec;
    uint64_t most = 0;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        most = std::max(most, fileId(entry.path(), "index-"));
        // One being written: its number is taken too.
        auto name = entry.path().filename().string();
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0)
            most = std::max(most, fileId(entry.path().parent_path() / name.substr(0, name.size() - 4), "index-"));
    }
    return folder / ("index-" + std::to_string(most + 1) + ".bin");
}

void tidyEvalsFolder(const fs::path& folder) {
    const auto newest = newestEvalsIndex(folder);
    std::error_code ec;
    std::vector<fs::path> old;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        const auto name = entry.path().filename().string();
        const bool tmp =
            name.rfind("index-", 0) == 0 && name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0;
        if (tmp || (fileId(entry.path(), "index-") > 0 && entry.path() != newest)) old.push_back(entry.path());
    }
    for (const auto& f : old) fs::remove(f, ec);
}

std::vector<fs::path> evalsRuns(const fs::path& build, const nlohmann::json& state) {
    std::vector<fs::path> out;
    if (!state.is_object() || !state.contains("runs") || !state["runs"].is_array()) return out;
    for (const auto& id : state["runs"])
        if (id.is_number_unsigned()) out.push_back(build / ("run-" + std::to_string(id.get<uint64_t>()) + ".bin"));
    return out;
}

// ---------------------------------------------------------------------------
// The builder

EvalsBuilder::EvalsBuilder(fs::path build, Options options) : build_(std::move(build)), options_(options) {
    std::error_code ec;
    fs::create_directories(build_, ec);
}

EvalsBuilder::~EvalsBuilder() {
    if (log_file_) std::fclose(log_file_);
}

fs::path EvalsBuilder::runFile(uint64_t id) const { return build_ / ("run-" + std::to_string(id) + ".bin"); }
fs::path EvalsBuilder::logFile(uint64_t id) const { return build_ / ("log-" + std::to_string(id) + ".bin"); }

void EvalsBuilder::fail(std::string why) {
    if (error_.empty()) error_ = std::move(why);
}

std::vector<fs::path> EvalsBuilder::runs() const {
    std::vector<fs::path> out;
    for (const auto id : runs_) out.push_back(runFile(id));
    return out;
}

void EvalsBuilder::line(std::string_view text) {
    EvalsLine l;
    if (!readEvalsLine(text, l)) {
        ++lines_;
        ++unread_;
        return;
    }
    add(l);
}

void EvalsBuilder::add(const EvalsLine& l) {
    ++lines_;
    EvalsPosition position;
    if (!evalsPosition(l.fen, position)) {
        ++unread_;
        return;
    }
    if (options_.min_men > 0 && evalsMen(position) < options_.min_men) return;
    buffer_.push_back({evalsKey(position, secret_) >> 8, packEval(l.cp, l.mate, l.depth, l.move)});
}

void EvalsBuilder::openLog(bool truncate) {
    if (log_file_) std::fclose(log_file_);
    log_file_ = tour::openFile(logFile(log_), truncate ? "wb" : "ab");
    if (!log_file_) fail("cannot write " + logFile(log_).string());
}

void EvalsBuilder::cutRun() {
    if (buffer_.empty() || !error_.empty()) return;
    // Sorted by key; of two of the same key (a position met twice), the deepest.
    std::sort(buffer_.begin(), buffer_.end(), [](const Entry& a, const Entry& b) {
        if (a.key != b.key) return a.key < b.key;
        return packedDepth(a.eval) > packedDepth(b.eval);
    });
    buffer_.erase(
        std::unique(buffer_.begin(), buffer_.end(), [](const Entry& a, const Entry& b) { return a.key == b.key; }),
        buffer_.end());
    const uint64_t id = next_++;
    const auto file   = runFile(id);
    auto tmp          = file;
    tmp += ".tmp";
    {
        Writer w(tmp);
        uint8_t header[RUN_HEADER] = {};
        std::memcpy(header, RUN_MAGIC, 8);
        store64(header + 8, buffer_.size());
        store64(header + 16, secret_.k0);
        store64(header + 24, secret_.k1);
        w.put(header, sizeof header);
        std::vector<uint8_t> chunk;
        chunk.reserve(RUN_RECORD * 4096);
        std::vector<uint32_t> fence((std::size_t{1} << RUN_BITS) + 1, 0);
        std::size_t filled = 0;  // the buckets whose start is known
        for (std::size_t i = 0; i < buffer_.size(); ++i) {
            const auto bucket = static_cast<std::size_t>(buffer_[i].key >> (56 - RUN_BITS));
            while (filled <= bucket) fence[filled++] = static_cast<uint32_t>(i);
            uint8_t record[RUN_RECORD];
            store56(record, buffer_[i].key);
            std::memcpy(record + 7, buffer_[i].eval.bytes, 5);
            chunk.insert(chunk.end(), record, record + RUN_RECORD);
            if (chunk.size() >= RUN_RECORD * 4096) {
                w.put(chunk.data(), chunk.size());
                chunk.clear();
            }
        }
        w.put(chunk.data(), chunk.size());
        while (filled < fence.size()) fence[filled++] = static_cast<uint32_t>(buffer_.size());
        std::vector<uint8_t> bytes(4 * fence.size());
        for (std::size_t i = 0; i < fence.size(); ++i) store32(bytes.data() + 4 * i, fence[i]);
        w.put(bytes.data(), bytes.size());
        if (!w.close()) {
            fail("cannot write " + tmp.string() + " (is the disk full?)");
            return;
        }
    }
    if (!renameOver(tmp, file)) {
        fail("cannot write " + file.string());
        return;
    }
    in_runs_ += buffer_.size();
    runs_.push_back(id);
    buffer_.clear();
    logged_ = 0;
    // The log of these positions is no longer needed once a state naming the run is saved.
    retired_.emplace_back(log_, marks_);
    log_ = next_++;
    openLog(true);
}

nlohmann::json EvalsBuilder::mark() {
    ++marks_;
    if (buffer_.size() >= options_.run_entries) cutRun();
    return {{"runs", runs_},
            {"log", log_},
            {"pending", buffer_.size()},
            {"lines", lines_},
            {"unread", unread_},
            {"in_runs", in_runs_},
            {"next", next_},
            {"min_men", options_.min_men},
            {"secret", {secret_.k0, secret_.k1}}};
}

void EvalsBuilder::commit() {
    if (!error_.empty()) return;
    if (!log_file_) openLog(false);
    if (log_file_ && logged_ < buffer_.size()) {
        std::vector<uint8_t> bytes;
        bytes.reserve(LOG_RECORD * (buffer_.size() - logged_));
        for (std::size_t i = logged_; i < buffer_.size(); ++i) {
            uint8_t record[LOG_RECORD];
            store56(record, buffer_[i].key);
            std::memcpy(record + 7, buffer_[i].eval.bytes, 5);
            bytes.insert(bytes.end(), record, record + LOG_RECORD);
        }
        if (std::fwrite(bytes.data(), 1, bytes.size(), log_file_) != bytes.size() || std::fflush(log_file_) != 0) {
            fail("cannot write " + logFile(log_).string() + " (is the disk full?)");
            return;
        }
        logged_ = buffer_.size();
    }
    // The logs a run replaced before the state saved last: no state that may be read again names them.
    std::error_code ec;
    std::erase_if(retired_, [&](const auto& r) {
        if (r.second > saved_mark_) return false;
        fs::remove(logFile(r.first), ec);
        return true;
    });
    saved_mark_ = marks_;
}

void EvalsBuilder::restore(const nlohmann::json& state) {
    if (log_file_) std::fclose(log_file_);
    log_file_ = nullptr;
    error_.clear();
    buffer_.clear();
    logged_ = 0;
    retired_.clear();
    marks_ = saved_mark_ = 0;
    runs_.clear();
    try {
        for (const auto& id : state.at("runs")) runs_.push_back(id.get<uint64_t>());
        log_               = state.at("log").get<uint64_t>();
        lines_             = state.value("lines", uint64_t{0});
        unread_            = state.value("unread", uint64_t{0});
        in_runs_           = state.value("in_runs", uint64_t{0});
        next_              = std::max<uint64_t>(state.value("next", uint64_t{1}), log_ + 1);
        options_.min_men   = state.value("min_men", options_.min_men);  // the scope it began with
        const auto& secret = state.at("secret");
        secret_            = {secret.at(0).get<uint64_t>(), secret.at(1).get<uint64_t>()};
        const auto pending = state.value("pending", std::size_t{0});
        // The files the state does not name: written after it (a run, a log), removed.
        std::error_code ec;
        std::vector<fs::path> others;
        for (const auto& entry : fs::directory_iterator(build_, ec)) {
            const auto run = fileId(entry.path(), "run-"), log = fileId(entry.path(), "log-");
            next_ = std::max(next_, std::max(run, log) + 1);
            const bool named =
                (run && std::find(runs_.begin(), runs_.end(), run) != runs_.end()) || (log && log == log_);
            const auto name = entry.path().filename().string();
            if ((run || log) && !named) others.push_back(entry.path());
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0) others.push_back(entry.path());
        }
        for (const auto& f : others) fs::remove(f, ec);
        for (const auto id : runs_)
            if (!fs::exists(runFile(id), ec)) throw std::runtime_error("a run is missing: " + runFile(id).string());
        // The positions of its log, as many as the state counts (it may hold more, written after it).
        const auto file = logFile(log_);
        const auto size = fs::exists(file, ec) ? fs::file_size(file, ec) : 0;
        if (size < LOG_RECORD * pending) throw std::runtime_error(file.string() + " is shorter than its state");
        if (pending > 0) {
            MappedFile log(file);
            buffer_.resize(pending);
            for (std::size_t i = 0; i < pending; ++i) {
                buffer_[i].key = load56(log.data() + LOG_RECORD * i);
                std::memcpy(buffer_[i].eval.bytes, log.data() + LOG_RECORD * i + 7, 5);
            }
        }
        fs::resize_file(file, LOG_RECORD * pending, ec);
        if (ec && pending > 0) throw std::runtime_error("cannot cut " + file.string() + " to its state");
        logged_ = pending;
        openLog(pending == 0);
    } catch (const std::exception& e) {
        runs_.clear();
        buffer_.clear();
        fail(std::string("the preparation cannot go on: ") + e.what());
    }
}

void EvalsBuilder::startAgain() {
    if (log_file_) std::fclose(log_file_);
    log_file_ = nullptr;
    error_.clear();
    std::error_code ec;
    std::vector<fs::path> old;
    for (const auto& entry : fs::directory_iterator(build_, ec)) {
        const auto run = fileId(entry.path(), "run-"), log = fileId(entry.path(), "log-");
        next_           = std::max(next_, std::max(run, log) + 1);
        const auto name = entry.path().filename().string();
        if (run || log || (name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0))
            old.push_back(entry.path());
    }
    for (const auto& f : old) fs::remove(f, ec);
    buffer_.clear();
    logged_ = 0;
    runs_.clear();
    retired_.clear();
    marks_ = saved_mark_ = 0;
    lines_ = unread_ = in_runs_ = 0;
    secret_                     = options_.secret ? *options_.secret : newEvalsSecret();
    log_                        = next_++;
    openLog(true);
}

std::string EvalsBuilder::finish(const fs::path& index, const std::string& date, std::atomic<uint64_t>* merged,
                                 std::atomic<uint64_t>* total) {
    if (!error_.empty()) return error_;
    cutRun();
    if (!error_.empty()) return error_;
    if (log_file_) std::fclose(log_file_);
    log_file_ = nullptr;
    // The runs read from their start through buffers (mapped, their pages would count as the program's memory).
    std::vector<std::unique_ptr<RunReader>> runs;
    uint64_t sum = 0;
    for (const auto id : runs_) {
        runs.push_back(std::make_unique<RunReader>(runFile(id)));
        if (!runs.back()->ok()) return runFile(id).string() + " is not a run, or cannot be read";
        sum += runs.back()->count();
    }
    if (total) *total = sum;
    if (sum > std::numeric_limits<uint32_t>::max()) return "too many positions for one index";
    const int bits         = bucketBits(sum);
    const uint64_t buckets = uint64_t{1} << bits;
    auto tmp               = index;
    tmp += ".tmp";
    uint64_t written = 0;
    {
        Writer w(tmp);
        uint8_t header[INDEX_HEADER] = {};
        w.put(header, sizeof header);  // written again at the end
        // The runs merged in key order: of the positions of one key (or of one bucket's 32 bits), the deepest.
        struct Head {
            uint64_t key;
            std::size_t run;
            bool operator>(const Head& o) const { return key > o.key; }
        };
        std::priority_queue<Head, std::vector<Head>, std::greater<>> heap;
        for (std::size_t r = 0; r < runs.size(); ++r)
            if (runs[r]->next()) heap.push({runs[r]->key(), r});
        std::vector<uint32_t> fence(buckets + 1, 0);
        uint64_t filled = 0;
        std::vector<uint8_t> chunk;
        chunk.reserve(INDEX_RECORD * 8192);
        bool held            = false;  // a record waiting, for another of the same bucket and bits
        uint64_t held_bucket = 0;
        uint32_t held_rest   = 0;
        PackedEval held_eval;
        const auto flush = [&] {
            uint8_t record[INDEX_RECORD];
            store32(record, held_rest);
            std::memcpy(record + 4, held_eval.bytes, 5);
            chunk.insert(chunk.end(), record, record + INDEX_RECORD);
            ++written;
            if (chunk.size() >= INDEX_RECORD * 8192) {
                w.put(chunk.data(), chunk.size());
                chunk.clear();
                if (merged) *merged = written;
            }
        };
        while (!heap.empty()) {
            const auto head = heap.top();
            heap.pop();
            auto& run             = *runs[head.run];
            const PackedEval eval = run.eval();
            if (run.next()) heap.push({run.key(), head.run});
            const uint64_t bucket = head.key >> (56 - bits);
            const uint32_t rest   = restOf(head.key, bits);
            if (held && bucket == held_bucket && rest == held_rest) {
                if (packedDepth(eval) > packedDepth(held_eval)) held_eval = eval;
                continue;
            }
            if (held) flush();
            while (filled <= bucket) fence[filled++] = static_cast<uint32_t>(written);
            held        = true;
            held_bucket = bucket;
            held_rest   = rest;
            held_eval   = eval;
        }
        if (held) flush();
        for (std::size_t r = 0; r < runs.size(); ++r)
            if (!runs[r]->ok()) return runFile(runs_[r]).string() + " cannot be read to its end";
        w.put(chunk.data(), chunk.size());
        while (filled <= buckets) fence[filled++] = static_cast<uint32_t>(written);
        std::vector<uint8_t> bytes(4 * fence.size());
        for (std::size_t i = 0; i < fence.size(); ++i) store32(bytes.data() + 4 * i, fence[i]);
        w.put(bytes.data(), bytes.size());
        std::memcpy(header, INDEX_MAGIC, 8);
        store32(header + 8, 1);
        store32(header + 12, static_cast<uint32_t>(bits));
        store64(header + 16, written);
        store64(header + 24, lines_);
        header[32] = static_cast<uint8_t>(std::clamp(options_.min_men, 0, 255));
        std::memcpy(header + 40, date.data(), std::min<std::size_t>(date.size(), 15));
        store64(header + 56, secret_.k0);
        store64(header + 64, secret_.k1);
        w.at(0);
        w.put(header, sizeof header);
        if (!w.close()) {
            std::error_code ec;
            fs::remove(tmp, ec);
            return "cannot write " + tmp.string() + " (is the disk full?)";
        }
    }
    if (merged) *merged = written;
    runs.clear();  // closed before they are removed
    if (!renameOver(tmp, index)) return "cannot write " + index.string();
    std::error_code ec;
    for (const auto id : runs_) fs::remove(runFile(id), ec);
    fs::remove(logFile(log_), ec);
    for (const auto& r : retired_) fs::remove(logFile(r.first), ec);
    runs_.clear();
    retired_.clear();
    in_runs_ = 0;
    return {};
}

}  // namespace fcg::study
