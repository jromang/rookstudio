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
// (codeCount() says which, the reader and the writer alike: Cursor and appendCode()). Decoding walks the pieces once,
// no move generated.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
    // The position in FEN: castling rights in KQkq for rooks on their first files, by the rook's file otherwise.
    [[nodiscard]] std::string fen() const;

    [[nodiscard]] uint64_t key() const noexcept { return key_; }  // Polyglot's
    [[nodiscard]] int sideToMove() const noexcept { return stm_; }
    [[nodiscard]] int fullMove() const noexcept { return fullmove_; }
    // The plies since the last capture or pawn move: a position can only be met again within them.
    [[nodiscard]] int halfmoveClock() const noexcept { return halfmove_; }
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
    // The men of side `by` that attack `square` (a pin left aside): those that defend a man there, or threaten it.
    [[nodiscard]] uint64_t attackers(int square, int by) const noexcept;
    // Whether the side to move has a legal move: none is mate when in check, stalemate otherwise.
    [[nodiscard]] bool hasLegalMove() const noexcept;

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

// The legal move of `board` that a code of the same position (`scan`, set to it) names; none when no move does.
[[nodiscard]] std::optional<chess::Move> moveOfCode(const chess::Board& board, const ScanBoard& scan, int code);

// A game's codes read one after the other on the board the game reaches, the one decoder of the databases' moves: a
// byte a code, two where the position has more than 256 (the high one first). The game ends at its last ply, or at a
// code that cannot be read, cut short or beyond the position's codes (a damaged record's): nothing is read past its
// codes, and nothing after such a code.
class Cursor {
   public:
    // The reading of a game of `plies` plies from ply `from`, every position before it of one-byte codes (the opening
    // tree follows no game past a position of more than 256).
    Cursor(std::span<const uint8_t> codes, uint32_t plies, uint32_t from = 0) noexcept
        : codes_(codes), plies_(plies), at_(from), ply_(from) {}

    static constexpr int END = -1;  // no move: the game's end, or a code that cannot be read (as TreeMove::END)

    // The next move's code on `board`, the position the game reached; END at the game's end, or for a code that
    // cannot be read, which ends it there. (An int, not an optional: the replays' speed.)
    [[nodiscard]] int next(const ScanBoard& board) noexcept {
        if (ply_ >= plies_ || at_ >= codes_.size()) [[unlikely]]
            return END;
        const int count = board.codeCount();
        int code        = codes_[at_++];
        if (count > 256) [[unlikely]] {
            if (at_ >= codes_.size()) return stop();
            code = (code << 8) | codes_[at_++];
        }
        if (code >= count) [[unlikely]]
            return stop();
        ++ply_;
        return code;
    }
    // The moves read, and the bytes read: a game read whole has all its plies (an end before them is a code that
    // could not be read), and all its bytes once its plies are read.
    [[nodiscard]] uint32_t ply() const noexcept { return ply_; }
    [[nodiscard]] std::size_t bytes() const noexcept { return at_; }

   private:
    int stop() noexcept {
        plies_ = ply_;
        return END;
    }
    std::span<const uint8_t> codes_;
    uint32_t plies_ = 0;
    std::size_t at_ = 0;
    uint32_t ply_   = 0;
};

// Appends the code of `move` on `board`, the position it is played from, to a game's codes, as Cursor reads it back.
inline void appendCode(std::vector<uint8_t>& codes, const ScanBoard& board, const CodedMove& move) {
    const int code = board.encode(move);
    if (board.codeCount() > 256) codes.push_back(static_cast<uint8_t>(code >> 8));
    codes.push_back(static_cast<uint8_t>(code & 0xFF));
}

}  // namespace fcg::study
