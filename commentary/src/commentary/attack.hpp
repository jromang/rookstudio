#pragma once

// Who attacks what, and what an exchange on a square is worth (the prototype's crates/facts/src/attack.rs).

#include <cstdint>
#include <vector>

#include "fact_parts.hpp"
#include "position.hpp"

namespace fcg::commentary {

// Values for exchanges, finer than pawn counts so that bishop for knight is not even.
[[nodiscard]] int32_t exchangeValue(Role role) noexcept;

// Static exchange evaluation: what `side` gains, in centipawns, by starting a capture sequence on `target`, each side
// recapturing with its cheapest piece and stopping when going on would lose. Zero if `side` cannot or should not
// capture.
[[nodiscard]] int32_t see(const Board& board, Square target, Color side);

// `color`'s pieces that the opponent can win by capturing them, kings aside.
[[nodiscard]] std::vector<Placed> hanging(const Board& board, Color color);

// Whether `color` attacks `square`.
[[nodiscard]] bool attackedBy(const Board& board, Square square, Color color) noexcept;

}  // namespace fcg::commentary
