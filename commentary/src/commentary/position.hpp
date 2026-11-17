#pragma once

// The board and the position as the prototype's detectors saw them (shakmaty 0.30): the same legal moves in the same
// order, the same notation, the same FEN and the same debug text, so that the port of the detectors and of the
// commentary says what the prototype said. Attacks come from chess.hpp's tables.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fcg::commentary {

enum class Color : uint8_t { White, Black };
[[nodiscard]] constexpr Color operator!(Color c) noexcept { return c == Color::White ? Color::Black : Color::White; }

// shakmaty's order, which its loops over the roles follow.
enum class Role : uint8_t { Pawn, Knight, Bishop, Rook, Queen, King };
inline constexpr std::array<Role, 6> ALL_ROLES{Role::Pawn, Role::Knight, Role::Bishop,
                                               Role::Rook, Role::Queen,  Role::King};

enum class CastlingSide : uint8_t { KingSide, QueenSide };

// clang-format off
enum class Square : uint8_t {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
};
// clang-format on
enum class File : uint8_t { A, B, C, D, E, F, G, H };
enum class Rank : uint8_t { First, Second, Third, Fourth, Fifth, Sixth, Seventh, Eighth };

[[nodiscard]] constexpr int index(Square s) noexcept { return static_cast<int>(s); }
[[nodiscard]] constexpr Square square(int i) noexcept { return static_cast<Square>(i); }
[[nodiscard]] constexpr Square square(File f, Rank r) noexcept {
    return static_cast<Square>(static_cast<int>(r) * 8 + static_cast<int>(f));
}
[[nodiscard]] constexpr File fileOf(Square s) noexcept { return static_cast<File>(index(s) & 7); }
[[nodiscard]] constexpr Rank rankOf(Square s) noexcept { return static_cast<Rank>(index(s) >> 3); }
// The square `delta` steps away in the board's numbering, none off the board (shakmaty's Square::offset).
[[nodiscard]] constexpr std::optional<Square> offset(Square s, int delta) noexcept {
    const int i = index(s) + delta;
    if (i < 0 || i > 63) return std::nullopt;
    return square(i);
}
// The rank as `color` counts it: White's Second is Black's Seventh.
[[nodiscard]] constexpr Rank relative(Rank r, Color c) noexcept {
    return c == Color::White ? r : static_cast<Rank>(7 - static_cast<int>(r));
}
// The king's and the rook's squares after castling.
[[nodiscard]] constexpr Square kingTo(CastlingSide side, Color c) noexcept {
    return square(side == CastlingSide::KingSide ? File::G : File::C, relative(Rank::First, c));
}
[[nodiscard]] constexpr Square rookTo(CastlingSide side, Color c) noexcept {
    return square(side == CastlingSide::KingSide ? File::F : File::D, relative(Rank::First, c));
}
// Chebyshev distance: the king's steps between two squares.
[[nodiscard]] int distance(Square a, Square b) noexcept;

class Bitboard {
   public:
    constexpr Bitboard() noexcept = default;
    constexpr explicit Bitboard(uint64_t bits) noexcept : bits_(bits) {}
    [[nodiscard]] static constexpr Bitboard from(Square s) noexcept { return Bitboard(uint64_t{1} << index(s)); }
    [[nodiscard]] static constexpr Bitboard file(File f) noexcept {
        return Bitboard(uint64_t{0x0101010101010101} << static_cast<int>(f));
    }
    [[nodiscard]] static constexpr Bitboard rank(Rank r) noexcept {
        return Bitboard(uint64_t{0xFF} << (8 * static_cast<int>(r)));
    }
    [[nodiscard]] static constexpr Bitboard relativeRank(Color c, Rank r) noexcept { return rank(relative(r, c)); }
    static constexpr uint64_t ALL       = ~uint64_t{0};
    static constexpr uint64_t DARK      = 0xAA55AA55AA55AA55ULL;
    static constexpr uint64_t LIGHT     = 0x55AA55AA55AA55AAULL;
    static constexpr uint64_t BACKRANKS = 0xFF000000000000FFULL;
    static constexpr uint64_t CENTER    = 0x0000001818000000ULL;
    static constexpr uint64_t CORNERS   = 0x8100000000000081ULL;
    [[nodiscard]] static constexpr Bitboard darkSquares() noexcept { return Bitboard(DARK); }
    [[nodiscard]] static constexpr Bitboard lightSquares() noexcept { return Bitboard(LIGHT); }

    [[nodiscard]] constexpr uint64_t bits() const noexcept { return bits_; }
    [[nodiscard]] constexpr bool contains(Square s) const noexcept { return (bits_ >> index(s)) & 1; }
    [[nodiscard]] constexpr bool any() const noexcept { return bits_ != 0; }
    [[nodiscard]] constexpr bool isEmpty() const noexcept { return bits_ == 0; }
    [[nodiscard]] int count() const noexcept { return __builtin_popcountll(bits_); }
    [[nodiscard]] constexpr bool moreThanOne() const noexcept { return (bits_ & (bits_ - 1)) != 0; }
    [[nodiscard]] std::optional<Square> first() const noexcept;
    [[nodiscard]] std::optional<Square> last() const noexcept;
    [[nodiscard]] std::optional<Square> singleSquare() const noexcept;
    [[nodiscard]] constexpr Bitboard with(Square s) const noexcept {
        return Bitboard(bits_ | (uint64_t{1} << index(s)));
    }
    [[nodiscard]] constexpr Bitboard without(Square s) const noexcept {
        return Bitboard(bits_ & ~(uint64_t{1} << index(s)));
    }
    // Moved `delta` squares up (negative: down) in the board's numbering, what falls off the board lost.
    [[nodiscard]] constexpr Bitboard shift(int delta) const noexcept {
        return Bitboard(delta >= 0 ? bits_ << delta : bits_ >> -delta);
    }

    constexpr Bitboard operator&(Bitboard o) const noexcept { return Bitboard(bits_ & o.bits_); }
    constexpr Bitboard operator|(Bitboard o) const noexcept { return Bitboard(bits_ | o.bits_); }
    constexpr Bitboard operator^(Bitboard o) const noexcept { return Bitboard(bits_ ^ o.bits_); }
    constexpr Bitboard operator~() const noexcept { return Bitboard(~bits_); }
    constexpr Bitboard& operator&=(Bitboard o) noexcept {
        bits_ &= o.bits_;
        return *this;
    }
    constexpr Bitboard& operator|=(Bitboard o) noexcept {
        bits_ |= o.bits_;
        return *this;
    }
    constexpr Bitboard& operator^=(Bitboard o) noexcept {
        bits_ ^= o.bits_;
        return *this;
    }
    constexpr bool operator==(const Bitboard&) const noexcept = default;

    // The squares, a1 first, as shakmaty's iterator gives them.
    class iterator {
       public:
        explicit constexpr iterator(uint64_t bits) noexcept : bits_(bits) {}
        Square operator*() const noexcept { return square(__builtin_ctzll(bits_)); }
        iterator& operator++() noexcept {
            bits_ &= bits_ - 1;
            return *this;
        }
        bool operator!=(const iterator& o) const noexcept { return bits_ != o.bits_; }

       private:
        uint64_t bits_;
    };
    [[nodiscard]] iterator begin() const noexcept { return iterator(bits_); }
    [[nodiscard]] iterator end() const noexcept { return iterator(0); }

   private:
    uint64_t bits_ = 0;
};

namespace attacks {
[[nodiscard]] Bitboard pawn(Color c, Square s) noexcept;
[[nodiscard]] Bitboard knight(Square s) noexcept;
[[nodiscard]] Bitboard bishop(Square s, Bitboard occupied) noexcept;
[[nodiscard]] Bitboard rook(Square s, Bitboard occupied) noexcept;
[[nodiscard]] Bitboard queen(Square s, Bitboard occupied) noexcept;
[[nodiscard]] Bitboard king(Square s) noexcept;
// The squares strictly between two squares on a line, none when they share no line.
[[nodiscard]] Bitboard between(Square a, Square b) noexcept;
// The whole line through two squares, both included, none when they share no line.
[[nodiscard]] Bitboard ray(Square a, Square b) noexcept;
[[nodiscard]] bool aligned(Square a, Square b, Square c) noexcept;
}  // namespace attacks

struct Piece {
    Color color                                            = Color::White;
    Role role                                              = Role::Pawn;
    constexpr bool operator==(const Piece&) const noexcept = default;
};

namespace attacks {
// What a piece attacks from `s` with `occupied` the squares in the way (shakmaty's attacks::attacks).
[[nodiscard]] Bitboard of(Piece p, Square s, Bitboard occupied) noexcept;
}  // namespace attacks

class Board {
   public:
    [[nodiscard]] static Board empty() noexcept { return {}; }
    [[nodiscard]] static Board initial() noexcept;

    [[nodiscard]] Bitboard byRole(Role r) const noexcept { return role_[static_cast<int>(r)]; }
    [[nodiscard]] Bitboard byColor(Color c) const noexcept { return color_[static_cast<int>(c)]; }
    [[nodiscard]] Bitboard byPiece(Piece p) const noexcept { return byRole(p.role) & byColor(p.color); }
    [[nodiscard]] Bitboard occupied() const noexcept { return color_[0] | color_[1]; }
    [[nodiscard]] Bitboard white() const noexcept { return color_[0]; }
    [[nodiscard]] Bitboard black() const noexcept { return color_[1]; }
    [[nodiscard]] Bitboard pawns() const noexcept { return byRole(Role::Pawn); }
    [[nodiscard]] Bitboard knights() const noexcept { return byRole(Role::Knight); }
    [[nodiscard]] Bitboard bishops() const noexcept { return byRole(Role::Bishop); }
    [[nodiscard]] Bitboard rooks() const noexcept { return byRole(Role::Rook); }
    [[nodiscard]] Bitboard queens() const noexcept { return byRole(Role::Queen); }
    [[nodiscard]] Bitboard kings() const noexcept { return byRole(Role::King); }
    [[nodiscard]] Bitboard rooksAndQueens() const noexcept { return rooks() | queens(); }
    [[nodiscard]] Bitboard bishopsAndQueens() const noexcept { return bishops() | queens(); }
    [[nodiscard]] Bitboard sliders() const noexcept { return bishops() | rooks() | queens(); }
    [[nodiscard]] Bitboard steppers() const noexcept { return knights() | kings() | pawns(); }

    [[nodiscard]] std::optional<Role> roleAt(Square s) const noexcept;
    [[nodiscard]] std::optional<Color> colorAt(Square s) const noexcept;
    [[nodiscard]] std::optional<Piece> pieceAt(Square s) const noexcept;
    [[nodiscard]] std::optional<Square> kingOf(Color c) const noexcept;

    // What the piece on `s` attacks, the board's pieces in the way; nothing for an empty square.
    [[nodiscard]] Bitboard attacksFrom(Square s) const noexcept;
    // `attacker`'s pieces attacking `s` with `occupied` the squares in the way.
    [[nodiscard]] Bitboard attacksTo(Square s, Color attacker, Bitboard occupied) const noexcept;

    void setPieceAt(Square s, Piece p) noexcept;
    std::optional<Piece> removePieceAt(Square s) noexcept;

    bool operator==(const Board&) const noexcept = default;

   private:
    std::array<Bitboard, 6> role_{};
    std::array<Bitboard, 2> color_{};
};

class Move {
   public:
    enum class Kind : uint8_t { Normal, EnPassant, Castle, Put };

    [[nodiscard]] static Move normal(Role role, Square from, std::optional<Role> capture, Square to,
                                     std::optional<Role> promotion = std::nullopt) noexcept;
    [[nodiscard]] static Move enPassant(Square from, Square to) noexcept;
    [[nodiscard]] static Move castle(Square king, Square rook) noexcept;

    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    // The moving piece: a king for castling.
    [[nodiscard]] Role role() const noexcept;
    [[nodiscard]] Square from() const noexcept { return from_; }
    // Where the piece lands: for castling, the rook's square (shakmaty's Move::to).
    [[nodiscard]] Square to() const noexcept { return to_; }
    // A pawn for en passant.
    [[nodiscard]] std::optional<Role> capture() const noexcept;
    [[nodiscard]] std::optional<Role> promotion() const noexcept { return promotion_; }
    [[nodiscard]] bool isCapture() const noexcept { return capture().has_value(); }
    [[nodiscard]] bool isEnPassant() const noexcept { return kind_ == Kind::EnPassant; }
    [[nodiscard]] bool isCastle() const noexcept { return kind_ == Kind::Castle; }
    [[nodiscard]] bool isPromotion() const noexcept { return promotion_.has_value(); }
    // A pawn move or a capture: resets the fifty-move count.
    [[nodiscard]] bool isZeroing() const noexcept;
    [[nodiscard]] std::optional<CastlingSide> castlingSide() const noexcept;

    bool operator==(const Move&) const noexcept = default;

   private:
    Kind kind_   = Kind::Normal;
    Role role_   = Role::Pawn;
    Square from_ = Square::A1;
    Square to_   = Square::A1;
    std::optional<Role> capture_;
    std::optional<Role> promotion_;
};

// The castling rights left: the rook each one castles with (standard castling only).
class Castles {
   public:
    [[nodiscard]] std::optional<Square> rook(Color c, CastlingSide side) const noexcept;
    [[nodiscard]] bool has(Color c, CastlingSide side) const noexcept { return rook(c, side).has_value(); }
    [[nodiscard]] bool hasColor(Color c) const noexcept {
        return has(c, CastlingSide::KingSide) || has(c, CastlingSide::QueenSide);
    }
    [[nodiscard]] bool isEmpty() const noexcept { return !hasColor(Color::White) && !hasColor(Color::Black); }
    // The squares that must be empty to castle: the king's and the rook's ways, themselves left out.
    [[nodiscard]] Bitboard path(Color c, CastlingSide side) const noexcept;
    void set(Color c, CastlingSide side, std::optional<Square> rook) noexcept;
    void discardColor(Color c) noexcept;
    void discardRook(Square s) noexcept;
    bool operator==(const Castles&) const noexcept = default;

   private:
    std::array<std::optional<Square>, 4> rooks_{};  // [2 * color + side]
};

class Position {
   public:
    // Standard castling; none when the FEN is not one of a legal position, `error` then saying why.
    [[nodiscard]] static std::optional<Position> fromFen(std::string_view fen, std::string* error = nullptr);
    [[nodiscard]] static Position initial();

    [[nodiscard]] const Board& board() const noexcept { return board_; }
    [[nodiscard]] Color turn() const noexcept { return turn_; }
    [[nodiscard]] const Castles& castles() const noexcept { return castles_; }
    [[nodiscard]] uint32_t halfmoves() const noexcept { return halfmoves_; }
    [[nodiscard]] uint32_t fullmoves() const noexcept { return fullmoves_; }
    // The square behind a pawn that just went two squares, as it is kept (shakmaty's raw ep square).
    [[nodiscard]] std::optional<Square> rawEpSquare() const noexcept { return ep_; }
    // The same, only when a legal capture goes there (EnPassantMode::Legal).
    [[nodiscard]] std::optional<Square> epSquare() const;

    [[nodiscard]] Bitboard us() const noexcept { return board_.byColor(turn_); }
    [[nodiscard]] Bitboard them() const noexcept { return board_.byColor(!turn_); }
    [[nodiscard]] Bitboard our(Role r) const noexcept { return board_.byPiece({turn_, r}); }
    [[nodiscard]] Bitboard their(Role r) const noexcept { return board_.byPiece({!turn_, r}); }
    [[nodiscard]] Bitboard checkers() const noexcept;
    [[nodiscard]] bool isCheck() const noexcept { return checkers().any(); }

    // In shakmaty's order: en passant captures, then the other pieces' moves (pawns, knights, bishops, rooks,
    // queens), the king's, and castling, king side first; in check, the king's moves first, then the others that
    // block or take the checker.
    [[nodiscard]] std::vector<Move> legalMoves() const;
    [[nodiscard]] bool isLegal(const Move& m) const;
    [[nodiscard]] bool isCheckmate() const;
    [[nodiscard]] bool isStalemate() const;
    [[nodiscard]] bool hasInsufficientMaterial(Color c) const noexcept;
    [[nodiscard]] bool isInsufficientMaterial() const noexcept {
        return hasInsufficientMaterial(Color::White) && hasInsufficientMaterial(Color::Black);
    }
    [[nodiscard]] bool isGameOver() const { return legalMoves().empty() || isInsufficientMaterial(); }

    void playUnchecked(const Move& m) noexcept;
    // The same position with the other side to move, the en passant square dropped (a null move); none when the side to
    // move is in check, which would leave the other side in check (shakmaty's swap_turn).
    [[nodiscard]] std::optional<Position> swappedTurn() const;

    // FEN, the en passant square only when a capture there is legal.
    [[nodiscard]] std::string fen() const;
    // A move in UCI ("e2e4", "e7e8q", castling "e1g1" or "e1h1"): the legal move it names, if one.
    [[nodiscard]] std::optional<Move> parseUci(std::string_view uci) const;
    // SAN, with "+" or "#" after a move that checks or mates (shakmaty's SanPlus).
    [[nodiscard]] std::string sanPlus(const Move& m) const;
    // SAN without them.
    [[nodiscard]] std::string san(const Move& m) const;

    bool operator==(const Position&) const noexcept = default;

   private:
    void generate(std::vector<Move>& moves) const;

    Board board_;
    Color turn_ = Color::White;
    Castles castles_;
    std::optional<Square> ep_;
    uint32_t halfmoves_ = 0;
    uint32_t fullmoves_ = 1;
};

// UCI as shakmaty writes it for standard chess: castling as the king's move ("e1g1").
[[nodiscard]] std::string uci(const Move& m);

// Rust's Debug text of shakmaty's types: "White", "Bishop", "D3", "C", "Seventh", "KingSide",
// "Normal { role: Knight, from: G1, capture: None, to: F3, promotion: None }".
[[nodiscard]] std::string debug(Color c);
[[nodiscard]] std::string debug(Role r);
[[nodiscard]] std::string debug(Square s);
[[nodiscard]] std::string debug(File f);
[[nodiscard]] std::string debug(Rank r);
[[nodiscard]] std::string debug(CastlingSide s);
[[nodiscard]] std::string debug(const Move& m);

// "d3", "c", "7"; a role's letter in SAN ('N'; 'P' for a pawn).
[[nodiscard]] std::string name(Square s);
[[nodiscard]] char letter(File f) noexcept;
[[nodiscard]] char letter(Rank r) noexcept;
[[nodiscard]] char upperLetter(Role r) noexcept;

}  // namespace fcg::commentary
