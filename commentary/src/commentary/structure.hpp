#pragma once

// Pawn structure and lasting features of a position, and the ones a line creates (the prototype's
// crates/facts/src/structure.rs).

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "fact_types.hpp"
#include "position.hpp"

namespace fcg::commentary {

// Plies over which a line's lasting changes are counted: the move, its answer and the exchanges that settle it. Later
// changes belong to later moves.
inline constexpr std::size_t SETTLING_PLIES = 4;

[[nodiscard]] Bitboard filesAround(File file) noexcept;
// The ranks in front of `square`, from `color`'s point of view.
[[nodiscard]] Bitboard ahead(Square square, Color color) noexcept;
// No enemy pawn in front of it, on its file or the neighbouring ones.
[[nodiscard]] bool isPassed(const Board& board, Square square, Color color) noexcept;
[[nodiscard]] Bitboard pawns(const Board& board, Color color) noexcept;
[[nodiscard]] constexpr Placed placed(Color color, Role role, Square square) noexcept { return {color, role, square}; }
// How far up the board `square` is for `color`: 0 on its first rank, 7 on its last.
[[nodiscard]] uint32_t advance(Square square, Color color) noexcept;
// Whether the board is full enough for space to matter: eight pieces or more besides kings and pawns.
[[nodiscard]] bool isCrowded(const Board& board) noexcept;
// Whether the position is an endgame: no queens, four pieces at most besides kings and pawns.
[[nodiscard]] bool isEndgame(const Board& board) noexcept;

// Static features of the position: passed, isolated, backward, doubled and hanging pawns, a pawn centre, the bishop
// pair and bad bishops, rooks on open files and on the seventh rank, outposts, exposed or active kings, a lead in
// development.
[[nodiscard]] std::vector<Fact> positionFacts(const Position& pos);

// The wings where `color` has a healthy pawn majority: more pawns than the opponent on files a to c, or f to h, two at
// least against one at least, each on a file of its own and none of them passed yet. Both sides have as many pawns in
// all: a pawn more is a pawn more, not a majority.
[[nodiscard]] std::vector<Wing> majorities(const Board& board, Color color);

// In a middlegame where the enemy queen is on, the squares of one colour weak in `color`'s camp, the dark ones when
// true: it has no bishop on them, and its pawns stand on the other colour, five at least and twice as many as on this
// one, or all four of them.
[[nodiscard]] std::optional<bool> weakColour(const Board& board, Color color);

// More space for `color`, in a middlegame with queens: a pawn on its fifth rank on files c to f where the opponent has
// none that far, or two on its fourth against none; and clearly more room behind its pawns than the opponent has. Not
// while the opponent still has three of its c- to f-pawns at home: it has yet to say where it puts them.
[[nodiscard]] bool hasSpace(const Board& board, Color color);

// The lasting features a line creates once its first plies settle: those at the end it did not have at the start.
// Features are counted per kind and side, so that a passed pawn which merely moves on is not a new one.
[[nodiscard]] std::vector<Fact> structureChanges(const Position& start, std::span<const Move> line);

}  // namespace fcg::commentary
