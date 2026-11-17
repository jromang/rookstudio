#pragma once

// Why a move fails, and what the better move does about it: how the move played makes the opponent's answer possible,
// and how the better one stops it (the prototype's crates/facts/src/cause.rs). These facts tie two moves together,
// where the others describe one line: a piece that stops guarding what the answer takes, a piece left where it was
// already attacked, a line the move opens; and, for the better move, the guard it adds, the piece it saves, the square
// it covers.

#include <optional>
#include <vector>

#include "fact_types.hpp"
#include "position.hpp"

namespace fcg::commentary {

// How the move `played` from `before` makes `answer`, the opponent's best reply, possible: the piece it takes was
// already attacked and stays there, or lost its guard to the move; the move opened the line the answer runs along.
[[nodiscard]] std::vector<Fact> causes(const Position& before, const Move& played, const Move& answer);

// The piece a move takes out of an attack that would have won it, when it is safe where it goes and takes nothing on
// the way: why a move that looks like a mere retreat had to be played.
[[nodiscard]] std::optional<Fact> saved(const Position& before, const Move& mv);

// What `best`, the better move from `before`, does about `answer`, the opponent's reply to the move `played`: it takes
// with another piece on the same square; it moves away the piece the answer takes, or guards it; it covers the square
// the answer lands on. `previous` is the move that led to `before`.
[[nodiscard]] std::vector<Fact> remedies(const Position& before, const Move& played, const Move& best,
                                         const Move& answer, std::optional<Move> previous);

// Where the moving piece lands: for castling, the king's destination.
[[nodiscard]] Square landing(const Move& mv, Color side) noexcept;

}  // namespace fcg::commentary
