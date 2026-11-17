#pragma once

// Pawn formations with a name, as the books on pawn structures know them, and what a pawn break means in the structure
// it strikes at (the prototype's crates/facts/src/formation.rs).

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "fact_parts.hpp"
#include "position.hpp"

namespace fcg::commentary {

// The square on `file`, `steps` ranks up from `color`'s first rank.
[[nodiscard]] Square squareFor(Color color, File file, uint32_t steps) noexcept;

// The named formations on the board, each with its owner.
[[nodiscard]] std::vector<std::pair<Formation, Color>> formations(const Board& board);

// What the break of `side`'s pawn going from `from` to `to`, striking at `target`, means in the structure on `board`,
// the position before it.
[[nodiscard]] std::optional<Lever> lever(const Board& board, Square from, Square to, Square target, Color side);

}  // namespace fcg::commentary
