#pragma once

// A board made to read and replay games fast, for the databases: the position and its Polyglot key kept up to date
// move after move, a move's code of one byte, and the SAN of a game's moves read without generating every move.
//
// A move's code: the side to move's pieces are taken in the order of their squares (a1 first), each with as many codes
// as its kind of piece can have moves wherever it stands (a king 10: its 8 steps and the two castlings; a queen 28; a
// rook or a bishop 14: the files it can reach along each of its two lines; a knight 8; a pawn 4: one step, two steps,
// a capture towards the a-file or the h-file, and 12 on its last rank but one: those three moves each with its 4
// promotions). A move's code is the sum of the codes of the pieces before it plus its own move's; the code after
// the last is the null move. A full army has 142 codes and one byte holds them; with more queens than that, two bytes
// (codeCount() says which, the reader and the writer alike). Decoding walks the pieces once, no move generated.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <chess.hpp>

namespace fcg::study {

enum PieceKind : uint8_t { PAWN = 0, KNIGHT, BISHOP, ROOK, QUEEN, KING, NO_KIND };
enum Side : uint8_t { WHITE_SIDE = 0, BLACK_SIDE = 1 };

inline constexpr uint8_t MOVE_SHORT_CASTLE = 1;
inline constexpr uint8_t MOVE_LONG_CASTLE  = 2;
inline constexpr uint8_t MOVE_EN_PASSANT   = 4;
inline constexpr uint8_t MOVE_NULL         = 8;

// A move: a castling goes from the king's square to its rook's (as chess.hpp writes it, Chess960 included).
struct CodedMove {
    uint8_t from      = 0;
    uint8_t to        = 0;
    uint8_t promotion = NO_KIND;  // what a pawn becomes
    uint8_t flags     = 0;

    bool operator==(const CodedMove&) const = default;
};

class ScanBoard {
   public:
    // The position of a chess.hpp board (FEN, Chess960 and castling rights read there). False for none.
    bool set(const chess::Board& board);
    bool setFen(std::string_view fen, bool chess960);

    [[nodiscard]] uint64_t key() const noexcept { return key_; }  // Polyglot's
    [[nodiscard]] int sideToMove() const noexcept { return stm_; }
    [[nodiscard]] int fullMove() const noexcept { return fullmove_; }
    // What stands on a square: 0 nothing, else 1 + kind + 6 for a black piece.
    [[nodiscard]] uint8_t on(int square) const noexcept { return board_[square]; }
    [[nodiscard]] uint64_t pieces(int side, int kind) const noexcept { return bb_[side][kind]; }
    [[nodiscard]] uint64_t occupied() const noexcept { return occ_[0] | occ_[1]; }

    // The number of codes of the side to move, the null move's included: more than 256 takes two bytes.
    [[nodiscard]] int codeCount() const noexcept;
    [[nodiscard]] int encode(const CodedMove& move) const noexcept;
    [[nodiscard]] CodedMove decode(int code) const noexcept;
    void play(const CodedMove& move) noexcept;

    // A move written in SAN ("Nbd7", "exd6", "e8=Q+", "O-O", "0-0-0", "--"): the legal move it names, if one.
    [[nodiscard]] std::optional<CodedMove> parseSan(std::string_view san) const;
    [[nodiscard]] bool inCheck() const noexcept;

   private:
    [[nodiscard]] int codeSize(int square) const noexcept;
    [[nodiscard]] bool attacked(int square, int by, uint64_t occupied, uint64_t removed = 0) const noexcept;
    [[nodiscard]] bool legal(const CodedMove& move) const noexcept;
    [[nodiscard]] bool castlingLegal(bool short_side) const noexcept;
    void put(int square, int side, int kind) noexcept;
    void remove(int square) noexcept;
    [[nodiscard]] uint64_t epKey() const noexcept;
    [[nodiscard]] uint64_t castlingKey() const noexcept;

    uint64_t bb_[2][6]{};
    uint64_t occ_[2]{};
    int codes_[2]{};      // the codes of each side's pieces, kept as they come and go
    uint8_t size_[64]{};  // the codes of the piece on each square
    uint8_t board_[64]{};
    uint8_t king_[2]{};
    int8_t rook_[2][2] = {{-1, -1}, {-1, -1}};  // the castling rooks' squares: [side][short, long], -1 no right
    int8_t ep_         = -1;                    // the square behind a pawn that just went two squares
    uint8_t stm_       = WHITE_SIDE;
    int halfmove_      = 0;
    int fullmove_      = 1;
    uint64_t key_      = 0;
};

}  // namespace fcg::study
