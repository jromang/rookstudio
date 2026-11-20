#include "ctg.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <string>

namespace fcg::study {

namespace {

constexpr std::size_t PAGE  = 4096;
constexpr std::size_t STATS = 33;

// The squares of a board as the book keeps it: White (the side to move) and Black (the other), square index
// rank * 8 + file. A piece is its kind, 8 added for Black's.
enum Kind : uint8_t { BOOK_EMPTY = 0, BOOK_PAWN = 1, BOOK_KNIGHT, BOOK_BISHOP, BOOK_ROOK, BOOK_QUEEN, BOOK_KING };
constexpr uint8_t BOOK_BLACK = 8;

struct BookBoard {
    std::array<uint8_t, 64> squares{};
    bool castle[4] = {};     // White's king side and queen side, then Black's
    int ep_file    = -1;     // a pawn may be taken en passant on that file
    bool colours   = false;  // Black was to move: ranks reversed and colours swapped
    bool files     = false;  // files reversed
};

uint8_t kindOf(chess::PieceType type) {
    switch (static_cast<int>(type)) {
        case 0:
            return BOOK_PAWN;
        case 1:
            return BOOK_KNIGHT;
        case 2:
            return BOOK_BISHOP;
        case 3:
            return BOOK_ROOK;
        case 4:
            return BOOK_QUEEN;
        case 5:
            return BOOK_KING;
        default:
            return BOOK_EMPTY;
    }
}

// The file of an en passant capture the side to move may make, -1 when there is none: the format writes it only when
// the capture is legal.
int enPassantFile(const chess::Board& board) {
    if (board.enpassantSq() == chess::Square::NO_SQ) return -1;
    chess::Movelist list;
    chess::movegen::legalmoves(list, board);
    for (const auto& m : list)
        if (m.typeOf() == chess::Move::ENPASSANT) return static_cast<int>(m.to().file());
    return -1;
}

BookBoard bookBoard(const chess::Board& board) {
    BookBoard b;
    const auto us = board.sideToMove();
    b.colours     = us == chess::Color::BLACK;
    for (int i = 0; i < 64; ++i) {
        const auto piece = board.at(chess::Square(i));
        if (piece == chess::Piece::NONE) continue;
        const uint8_t kind                = kindOf(piece.type());
        b.squares[b.colours ? i ^ 56 : i] = static_cast<uint8_t>(kind | (piece.color() == us ? 0 : BOOK_BLACK));
    }
    using Side      = chess::Board::CastlingRights::Side;
    const auto cr   = board.castlingRights();
    b.castle[0]     = cr.has(us, Side::KING_SIDE);
    b.castle[1]     = cr.has(us, Side::QUEEN_SIDE);
    b.castle[2]     = cr.has(~us, Side::KING_SIDE);
    b.castle[3]     = cr.has(~us, Side::QUEEN_SIDE);
    b.ep_file       = enPassantFile(board);
    const bool none = !b.castle[0] && !b.castle[1] && !b.castle[2] && !b.castle[3];
    if (none) {
        const auto king = std::find(b.squares.begin(), b.squares.end(), BOOK_KING);
        if (king != b.squares.end() && ((king - b.squares.begin()) & 7) < 4) {
            std::array<uint8_t, 64> flipped{};
            for (int i = 0; i < 64; ++i) flipped[i ^ 7] = b.squares[i];
            b.squares = flipped;
            if (b.ep_file >= 0) b.ep_file = 7 - b.ep_file;
            b.files = true;
        }
    }
    return b;
}

class Bits {
   public:
    void add(uint32_t value, int count) {
        for (int i = count - 1; i >= 0; --i) {
            if ((value >> i) & 1) bytes_[size_ / 8] |= static_cast<uint8_t>(0x80 >> (size_ % 8));
            ++size_;
        }
    }
    [[nodiscard]] int left() const { return (8 - size_ % 8) % 8; }  // bits to the end of the byte
    [[nodiscard]] std::vector<uint8_t> bytes() const {
        return {bytes_.begin(), bytes_.begin() + static_cast<std::ptrdiff_t>(size_ / 8)};
    }

   private:
    std::array<uint8_t, 32> bytes_{};
    int size_ = 0;
};

// Each square, file by file from a1 (a1, a2 ... a8, b1 ...): an empty one in one bit, a piece in three to six.
std::vector<uint8_t> keyOf(const BookBoard& b) {
    Bits bits;
    bits.add(0, 8);  // the byte of the key's length and flags, written last
    for (int file = 0; file < 8; ++file)
        for (int rank = 0; rank < 8; ++rank) {
            const uint8_t piece  = b.squares[rank * 8 + file];
            const uint32_t black = (piece & BOOK_BLACK) ? 1 : 0;
            switch (piece & 7) {
                case BOOK_EMPTY:
                    bits.add(0, 1);
                    break;
                case BOOK_PAWN:
                    bits.add(0b110 | black, 3);
                    break;
                case BOOK_KNIGHT:
                    bits.add(0b10010 | black, 5);
                    break;
                case BOOK_BISHOP:
                    bits.add(0b10100 | black, 5);
                    break;
                case BOOK_ROOK:
                    bits.add(0b10110 | black, 5);
                    break;
                case BOOK_QUEEN:
                    bits.add(0b100010 | black, 6);
                    break;
                case BOOK_KING:
                    bits.add(0b100000 | black, 6);
                    break;
                default:
                    break;
            }
        }
    const bool ep     = b.ep_file >= 0;
    const bool castle = b.castle[0] || b.castle[1] || b.castle[2] || b.castle[3];
    // Zeros up to the bits of en passant (3) and castling (4) at the end of the last byte, at least one.
    if (!ep && !castle) bits.add(0, 1);
    const int tail = (ep ? 3 : 0) + (castle ? 4 : 0);
    while (bits.left() != tail) bits.add(0, 1);
    if (ep) bits.add(static_cast<uint32_t>(b.ep_file), 3);
    if (castle) {
        bits.add(b.castle[2], 1);
        bits.add(b.castle[3], 1);
        bits.add(b.castle[0], 1);
        bits.add(b.castle[1], 1);
    }
    auto key = bits.bytes();
    key[0]   = static_cast<uint8_t>(key.size() | (ep ? 0x20 : 0) | (castle ? 0x40 : 0));
    return key;
}

constexpr std::array<uint32_t, 64> HASH_TABLE = {
    0x3100d2bf, 0x3118e3de, 0x34ab1372, 0x2807a847, 0x1633f566, 0x2143b359, 0x26d56488, 0x3b9e6f59,
    0x37755656, 0x3089ca7b, 0x18e92d85, 0x0cd0e9d8, 0x1a9e3b54, 0x3eaa902f, 0x0d9bfaae, 0x2f32b45b,
    0x31ed6102, 0x3d3c8398, 0x146660e3, 0x0f8d4b76, 0x02c77a5f, 0x146c8799, 0x1c47f51f, 0x249f8f36,
    0x24772043, 0x1fbc1e4d, 0x1e86b3fa, 0x37df36a6, 0x16ed30e4, 0x02c3148e, 0x216e5929, 0x0636b34e,
    0x317f9f56, 0x15f09d70, 0x131026fb, 0x38c784b1, 0x29ac3305, 0x2b485dc5, 0x3c049ddc, 0x35a9fbcd,
    0x31d5373b, 0x2b246799, 0x0a2923d3, 0x08a96e9d, 0x30031a9f, 0x08f525b5, 0x33611c06, 0x2409db98,
    0x0ca4feb2, 0x1000b71e, 0x30566e32, 0x39447d31, 0x194e3752, 0x08233a95, 0x0f38fe36, 0x29c7cd57,
    0x0f7b3a39, 0x328e8a16, 0x1e7d1388, 0x0fba78f5, 0x274c7e7c, 0x1e8be65c, 0x2fa0b0bb, 0x1eb6c371,
};

// A move's code names the piece that moves, the n-th White piece of its kind found file by file from a1 (a1, a2 ...
// a8, b1 ...), and its step in files and in ranks, counted modulo 8: a step of 7 is one back.
struct MoveCode {
    uint8_t code;
    char piece;
    uint8_t nth, files, ranks;
};

// clang-format off
constexpr MoveCode MOVE_CODES[] = {
    // kings
    {0x13, 'K', 0, 0, 1}, {0x0a, 'K', 0, 0, 7}, {0xbe, 'K', 0, 1, 0}, {0x67, 'K', 0, 1, 1}, {0x44, 'K', 0, 1, 7},
    {0x6b, 'K', 0, 2, 0}, {0xf6, 'K', 0, 6, 0}, {0x97, 'K', 0, 7, 0}, {0xdf, 'K', 0, 7, 1}, {0x8c, 'K', 0, 7, 7},
    // queens
    {0x05, 'Q', 0, 0, 1}, {0x9d, 'Q', 0, 0, 2}, {0x94, 'Q', 0, 0, 3}, {0x37, 'Q', 0, 0, 4}, {0xb7, 'Q', 0, 0, 5},
    {0x9a, 'Q', 0, 0, 6}, {0xfd, 'Q', 0, 0, 7}, {0x2f, 'Q', 0, 1, 0}, {0xf1, 'Q', 0, 1, 1}, {0x42, 'Q', 0, 1, 7},
    {0x74, 'Q', 0, 2, 0}, {0xa2, 'Q', 0, 2, 2}, {0x80, 'Q', 0, 2, 6}, {0x31, 'Q', 0, 3, 0}, {0x45, 'Q', 0, 3, 3},
    {0x4b, 'Q', 0, 3, 5}, {0xe5, 'Q', 0, 4, 0}, {0xe7, 'Q', 0, 4, 4}, {0x39, 'Q', 0, 5, 0}, {0x7e, 'Q', 0, 5, 3},
    {0x28, 'Q', 0, 5, 5}, {0x29, 'Q', 0, 6, 0}, {0xd9, 'Q', 0, 6, 2}, {0x61, 'Q', 0, 6, 6}, {0x8f, 'Q', 0, 7, 0},
    {0xd7, 'Q', 0, 7, 1}, {0xed, 'Q', 0, 7, 7}, {0x4d, 'Q', 1, 0, 1}, {0xbd, 'Q', 1, 0, 2}, {0xa0, 'Q', 1, 0, 3},
    {0xb0, 'Q', 1, 0, 4}, {0xc6, 'Q', 1, 0, 5}, {0x1a, 'Q', 1, 0, 6}, {0xf9, 'Q', 1, 0, 6}, {0xc9, 'Q', 1, 0, 7},
    {0xf8, 'Q', 1, 1, 0}, {0x92, 'Q', 1, 1, 1}, {0x63, 'Q', 1, 1, 7}, {0x03, 'Q', 1, 2, 0}, {0xfb, 'Q', 1, 2, 2},
    {0xab, 'Q', 1, 2, 6}, {0xfa, 'Q', 1, 3, 0}, {0xae, 'Q', 1, 3, 3}, {0x3b, 'Q', 1, 3, 5}, {0x08, 'Q', 1, 4, 0},
    {0x38, 'Q', 1, 4, 4}, {0xbc, 'Q', 1, 5, 0}, {0xfe, 'Q', 1, 5, 3}, {0x41, 'Q', 1, 5, 5}, {0x8e, 'Q', 1, 6, 2},
    {0x23, 'Q', 1, 6, 6}, {0xf0, 'Q', 1, 7, 0}, {0xee, 'Q', 1, 7, 1}, {0x6f, 'Q', 1, 7, 7},
    // rooks
    {0x81, 'R', 0, 0, 1}, {0xe8, 'R', 0, 0, 2}, {0x7a, 'R', 0, 0, 3}, {0x99, 'R', 0, 0, 4}, {0x87, 'R', 0, 0, 5},
    {0x50, 'R', 0, 0, 6}, {0xe1, 'R', 0, 0, 7}, {0x7f, 'R', 0, 1, 0}, {0xcd, 'R', 0, 2, 0}, {0x98, 'R', 0, 3, 0},
    {0xef, 'R', 0, 4, 0}, {0x6e, 'R', 0, 5, 0}, {0x52, 'R', 0, 6, 0}, {0x86, 'R', 0, 7, 0}, {0x7d, 'R', 1, 0, 1},
    {0xf4, 'R', 1, 0, 2}, {0xe3, 'R', 1, 0, 3}, {0xc5, 'R', 1, 0, 4}, {0xa5, 'R', 1, 0, 5}, {0x7b, 'R', 1, 0, 6},
    {0x69, 'R', 1, 0, 7}, {0xc4, 'R', 1, 1, 0}, {0xa9, 'R', 1, 2, 0}, {0x0e, 'R', 1, 3, 0}, {0xad, 'R', 1, 4, 0},
    {0xb5, 'R', 1, 5, 0}, {0xd8, 'R', 1, 6, 0}, {0x21, 'R', 1, 7, 0},
    // bishops
    {0x2e, 'B', 0, 1, 1}, {0x26, 'B', 0, 1, 7}, {0xc1, 'B', 0, 2, 2}, {0xb2, 'B', 0, 2, 6}, {0x0d, 'B', 0, 3, 3},
    {0x3c, 'B', 0, 3, 5}, {0x6a, 'B', 0, 4, 4}, {0x66, 'B', 0, 5, 3}, {0x15, 'B', 0, 5, 5}, {0xc3, 'B', 0, 6, 2},
    {0x3a, 'B', 0, 6, 6}, {0x1b, 'B', 0, 7, 1}, {0x12, 'B', 0, 7, 7}, {0xf5, 'B', 1, 1, 1}, {0x72, 'B', 1, 1, 7},
    {0xc2, 'B', 1, 2, 2}, {0x79, 'B', 1, 2, 6}, {0xb9, 'B', 1, 3, 3}, {0xcc, 'B', 1, 3, 5}, {0x9f, 'B', 1, 4, 4},
    {0xca, 'B', 1, 5, 3}, {0x32, 'B', 1, 5, 5}, {0x22, 'B', 1, 6, 2}, {0x09, 'B', 1, 6, 6}, {0x54, 'B', 1, 7, 1},
    {0x1d, 'B', 1, 7, 7},
    // knights
    {0x34, 'N', 0, 1, 2}, {0x8a, 'N', 0, 1, 6}, {0x36, 'N', 0, 2, 1}, {0xe9, 'N', 0, 2, 7}, {0xf7, 'N', 0, 6, 1},
    {0x0f, 'N', 0, 6, 7}, {0x85, 'N', 0, 7, 2}, {0xda, 'N', 0, 7, 6}, {0x4c, 'N', 1, 1, 2}, {0x2a, 'N', 1, 1, 6},
    {0xd2, 'N', 1, 2, 1}, {0xe0, 'N', 1, 2, 7}, {0xd3, 'N', 1, 6, 1}, {0x01, 'N', 1, 6, 7}, {0xf3, 'N', 1, 7, 2},
    {0x30, 'N', 1, 7, 6},
    // pawns
    {0xec, 'P', 0, 0, 1}, {0xdb, 'P', 0, 0, 2}, {0x8b, 'P', 0, 1, 1}, {0x0c, 'P', 0, 7, 1}, {0x04, 'P', 1, 0, 1},
    {0x62, 'P', 1, 0, 2}, {0x96, 'P', 1, 1, 1}, {0xd1, 'P', 1, 7, 1}, {0x55, 'P', 2, 0, 1}, {0x9b, 'P', 2, 0, 2},
    {0x7c, 'P', 2, 1, 1}, {0x27, 'P', 2, 7, 1}, {0xcf, 'P', 3, 0, 1}, {0xe6, 'P', 3, 0, 2}, {0xeb, 'P', 3, 1, 1},
    {0x06, 'P', 3, 7, 1}, {0xbb, 'P', 4, 0, 1}, {0x5f, 'P', 4, 0, 2}, {0x00, 'P', 4, 1, 1}, {0xde, 'P', 4, 7, 1},
    {0xcb, 'P', 5, 0, 1}, {0xb1, 'P', 5, 0, 2}, {0x2d, 'P', 5, 1, 1}, {0x82, 'P', 5, 7, 1}, {0x18, 'P', 6, 0, 1},
    {0xc8, 'P', 6, 0, 2}, {0x5c, 'P', 6, 1, 1}, {0xc7, 'P', 6, 7, 1}, {0xa3, 'P', 7, 0, 1}, {0x4a, 'P', 7, 0, 2},
    {0x14, 'P', 7, 1, 1}, {0x24, 'P', 7, 7, 1},
};
// clang-format on

struct CodeTable {
    std::array<const MoveCode*, 256> at{};
    constexpr CodeTable() {
        for (const auto& c : MOVE_CODES) at[c.code] = &c;
    }
};
constexpr CodeTable CODES;

uint8_t kindOfLetter(char letter) {
    switch (letter) {
        case 'K':
            return BOOK_KING;
        case 'Q':
            return BOOK_QUEEN;
        case 'R':
            return BOOK_ROOK;
        case 'B':
            return BOOK_BISHOP;
        case 'N':
            return BOOK_KNIGHT;
        default:
            return BOOK_PAWN;
    }
}

uint32_t be(const uint8_t* p, int bytes) {
    uint32_t v = 0;
    for (int i = 0; i < bytes; ++i) v = (v << 8) | p[i];
    return v;
}

// A position's games, from the side to move's point of view in the book, as White's and Black's.
TreeStats gamesOf(std::span<const uint8_t> stats, chess::Color to_move) {
    const uint8_t* s    = stats.data();
    const uint32_t wins = be(s + 3, 3);
    const uint32_t loss = be(s + 6, 3);
    const bool white    = to_move == chess::Color::WHITE;
    TreeStats t;
    t.games                  = be(s, 3);
    t.draws                  = be(s + 9, 3);
    t.white                  = white ? wins : loss;
    t.black                  = white ? loss : wins;
    const uint32_t own_rated = be(s + 16, 3), own_elo = be(s + 19, 4);
    const uint32_t opp_rated = be(s + 23, 3), opp_elo = be(s + 26, 4);
    t.white_rated = white ? own_rated : opp_rated;
    t.white_elo   = white ? own_elo : opp_elo;
    t.black_rated = white ? opp_rated : own_rated;
    t.black_elo   = white ? opp_elo : own_elo;
    return t;
}

// An evaluation's NAG with White and Black swapped: the book writes it for the side to move as White.
int swapped(int nag) { return nag >= 14 && nag <= 139 ? nag ^ 1 : nag; }

// The legal move of `board` from `from` to `to` (the king's square for castling), promoting to `promotion`.
std::optional<chess::Move> legalMove(const chess::Movelist& legal, int from, int to, chess::PieceType promotion) {
    for (const auto& m : legal) {
        if (m.from().index() != from) continue;
        if (m.typeOf() == chess::Move::CASTLING) {
            const int king_to = (m.from().index() & 56) + (m.to().index() > m.from().index() ? 6 : 2);
            if (king_to == to) return m;
            continue;
        }
        if (m.to().index() != to) continue;
        if (m.typeOf() == chess::Move::PROMOTION ? m.promotionType() == promotion : promotion == chess::PieceType::NONE)
            return m;
    }
    return std::nullopt;
}

std::filesystem::path sibling(const std::filesystem::path& file, char letter) {
    auto ext = file.extension().string();  // ".ctg", ".CTG"...
    if (ext.size() != 4) return {};
    const bool upper = ext[3] >= 'A' && ext[3] <= 'Z';
    ext[3]           = upper ? static_cast<char>(letter - 'a' + 'A') : letter;
    auto path        = file;
    path.replace_extension(ext);
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) return path;
    ext[3] = upper ? letter : static_cast<char>(letter - 'a' + 'A');  // the other case
    path.replace_extension(ext);
    return path;
}

}  // namespace

std::vector<uint8_t> ctgKey(const chess::Board& board) { return keyOf(bookBoard(board)); }

uint32_t ctgHash(std::span<const uint8_t> key) {
    uint32_t hash = 0, step = 0;
    for (const uint8_t c : key) {
        step += ((0x0fu - (c & 0x0fu)) << 2) + 1;
        hash += HASH_TABLE[step & 0x3f];
        step += ((0xf0u - (c & 0xf0u)) >> 2) + 1;
        hash += HASH_TABLE[step & 0x3f];
    }
    return hash;
}

CtgBook::CtgBook(const std::filesystem::path& file) {
    auto ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != ".ctg" && ext != ".ctb" && ext != ".cto")
        throw std::runtime_error(file.filename().string() + " is not a CTG book (.ctg, .ctb or .cto)");
    ctg_path_      = sibling(file, 'g');
    const auto cto = sibling(file, 'o');
    const auto ctb = sibling(file, 'b');
    for (const auto& part : {ctg_path_, cto, ctb}) {
        std::error_code ec;
        if (!std::filesystem::exists(part, ec))
            throw std::runtime_error(part.filename().string() + " is missing: a CTG book is its .ctg, .ctb and .cto");
    }
    ctg_ = MappedFile(ctg_path_);
    cto_ = MappedFile(cto);
    std::ifstream in(ctb, std::ios::binary);
    std::array<uint8_t, 12> bounds{};
    if (!in.read(reinterpret_cast<char*>(bounds.data()), bounds.size()))
        throw std::runtime_error(ctb.filename().string() + " is too short for a CTG book");
    lower_ = static_cast<int32_t>(be(bounds.data() + 4, 4));
    upper_ = static_cast<int32_t>(be(bounds.data() + 8, 4));
    if (ctg_.size() < PAGE || ctg_.size() % PAGE != 0 || cto_.size() < 16 || lower_ < 0 || upper_ < lower_)
        throw std::runtime_error(ctg_path_.filename().string() + " is not a CTG book");
    pages_     = static_cast<uint32_t>(ctg_.size() / PAGE - 1);
    games_     = be(ctg_.data() + 0x1c, 4);
    positions_ = be(ctg_.data() + 0x18, 4);
}

std::optional<CtgBook::Entry> CtgBook::inPage(uint32_t page, std::span<const uint8_t> key) const {
    if (page >= pages_) return std::nullopt;
    const uint8_t* p       = ctg_.data() + (static_cast<std::size_t>(page) + 1) * PAGE;
    const uint32_t count   = be(p, 2);
    const std::size_t used = std::min<std::size_t>(be(p + 2, 2), PAGE);
    std::size_t at         = 4;
    for (uint32_t i = 0; i < count; ++i) {
        if (at + 1 > used) break;
        const std::size_t length = p[at] & 0x1f;
        if (length == 0 || at + length + 1 > used) break;
        const std::size_t move_bytes = p[at + length];
        const std::size_t size       = length + move_bytes + STATS;
        if (move_bytes == 0 || at + size > used) break;
        if (length == key.size() && std::equal(key.begin(), key.end(), p + at))
            return Entry{{p + at + length + 1, move_bytes - 1}, {p + at + length + move_bytes, STATS}};
        at += size;
    }
    return std::nullopt;
}

std::optional<CtgBook::Entry> CtgBook::findKey(std::span<const uint8_t> key) const {
    const uint32_t hash = ctgHash(key);
    for (uint32_t n = 0; n < 0x7fffffffu; n = 2 * n + 1) {
        const int64_t index = static_cast<int64_t>(hash & n) + n;
        if (index < lower_) continue;
        const std::size_t at = 16 + 4 * static_cast<std::size_t>(index);
        if (at + 4 <= cto_.size()) {
            const auto page = static_cast<int32_t>(be(cto_.data() + at, 4));
            if (page >= 0)
                if (auto entry = inPage(static_cast<uint32_t>(page), key)) return entry;
        }
        if (index >= upper_) break;
    }
    return std::nullopt;
}

std::optional<CtgBook::Entry> CtgBook::find(const chess::Board& board) const {
    const auto key = ctgKey(board);
    return findKey(key);
}

std::optional<CtgAnswer> CtgBook::at(const chess::Board& board) const {
    const auto b     = bookBoard(board);
    const auto key   = keyOf(b);
    const auto entry = findKey(key);
    if (!entry) return std::nullopt;
    CtgAnswer answer;
    answer.games = gamesOf(entry->stats, board.sideToMove());

    chess::Movelist legal;
    chess::movegen::legalmoves(legal, board);
    // A square of the book's board back on the real one.
    const auto real = [&](int sq) { return (b.files ? sq ^ 7 : sq) ^ (b.colours ? 56 : 0); };
    for (std::size_t i = 0; i + 1 < entry->moves.size(); i += 2) {
        const MoveCode* code = CODES.at[entry->moves[i]];
        if (!code) continue;
        const uint8_t kind = kindOfLetter(code->piece);
        int from           = -1;
        int nth            = code->nth;
        for (int file = 0; file < 8 && from < 0; ++file)
            for (int rank = 0; rank < 8; ++rank)
                if (b.squares[rank * 8 + file] == kind && nth-- == 0) {
                    from = rank * 8 + file;
                    break;
                }
        if (from < 0) continue;
        const int to_file = ((from & 7) + code->files) & 7;
        const int to_rank = ((from >> 3) + code->ranks) & 7;
        const int to      = to_rank * 8 + to_file;
        // The format keeps a promotion as one move: each piece whose position the book has is one, a queen if none.
        std::vector<chess::PieceType> promotions{chess::PieceType::NONE};
        if (kind == BOOK_PAWN && to_rank == 7)
            promotions = {chess::PieceType::QUEEN, chess::PieceType::ROOK, chess::PieceType::BISHOP,
                          chess::PieceType::KNIGHT};
        std::vector<CtgMove> found;
        for (const auto promotion : promotions) {
            const auto move = legalMove(legal, real(from), real(to), promotion);
            if (!move) continue;
            CtgMove m;
            m.move     = *move;
            m.symbol   = entry->moves[i + 1];
            auto after = board;
            after.makeMove(*move);
            if (const auto next = find(after)) {
                m.games             = gamesOf(next->stats, after.sideToMove());
                const uint8_t flags = next->stats[30];
                m.main              = (flags & 0x80) != 0;
                m.avoid             = (flags & 0xc0) == 0x40;
                const int nag       = next->stats[32];
                m.evaluation        = after.sideToMove() == chess::Color::BLACK ? swapped(nag) : nag;
            } else if (promotions.size() > 1) {
                continue;
            }
            found.push_back(m);
        }
        if (found.empty() && promotions.size() > 1)
            if (const auto queen = legalMove(legal, real(from), real(to), chess::PieceType::QUEEN)) {
                CtgMove m;
                m.move   = *queen;
                m.symbol = entry->moves[i + 1];
                found.push_back(m);
            }
        answer.moves.insert(answer.moves.end(), found.begin(), found.end());
    }
    return answer;
}

CtgSummary CtgBook::summary(const std::atomic<bool>* cancel) const {
    CtgSummary s;
    for (uint32_t page = 0; page < pages_; ++page) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            s.cancelled = true;
            return s;
        }
        const uint8_t* p       = ctg_.data() + (static_cast<std::size_t>(page) + 1) * PAGE;
        const uint32_t count   = be(p, 2);
        const std::size_t used = std::min<std::size_t>(be(p + 2, 2), PAGE);
        std::size_t at         = 4;
        for (uint32_t i = 0; i < count; ++i) {
            if (at + 1 > used) break;
            const std::size_t length = p[at] & 0x1f;
            if (length == 0 || at + length + 1 > used) break;
            const std::size_t move_bytes = p[at + length];
            const std::size_t size       = length + move_bytes + STATS;
            if (move_bytes == 0 || at + size > used) break;
            const uint8_t flags = p[at + length + move_bytes + 30];
            ++s.positions;
            s.moves += (move_bytes - 1) / 2;
            if (flags & 0x80)
                ++s.main;
            else if (flags & 0x40)
                ++s.avoid;
            at += size;
        }
    }
    return s;
}

}  // namespace fcg::study
