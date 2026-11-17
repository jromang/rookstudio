#pragma once

// What the endgame books teach about a move: the opposition, the square of a passed pawn, a king cut off by a rook, the
// key squares of a pawn, the Lucena position and Philidor's defence, the bishop that does not go with its rook's pawn
// (the prototype's crates/facts/src/endgame.rs).

#include <optional>
#include <vector>

#include "fact_types.hpp"
#include "position.hpp"

namespace fcg::commentary {

// What a move does that the endgame books have a name for.
[[nodiscard]] std::vector<Fact> endgameFacts(const Position& before, const Move& mv);

// Whether the pawn on `pawn` promotes whatever the enemy king does: the rule of the square, counted with the side to
// move. The pawn is passed, its path is clear, and the opponent has only its king to stop it.
[[nodiscard]] bool outruns(const Position& pos, Square pawn);

// The key squares of `color`'s pawn on `pawn`: with its king on one of them, king and pawn win against the bare king
// whoever is to move. A rook's pawn has none worth the name.
[[nodiscard]] Bitboard keySquares(Square pawn, Color color) noexcept;

// `color`'s bishop and pawns on one rook's file against a bare king that holds the promotion corner, a square of the
// colour the bishop does not go on: a draw.
[[nodiscard]] std::optional<Fact> wrongBishop(const Board& board, Color color);

}  // namespace fcg::commentary
