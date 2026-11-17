#pragma once

// What a side threatens: what it would do if it could move twice in a row (the prototype's
// crates/facts/src/threat.rs).

#include <vector>

#include "fact_types.hpp"
#include "position.hpp"

namespace fcg::commentary {

// `side`'s threat in `pos`: a mate, or else the most valuable piece it could win.
[[nodiscard]] std::vector<Fact> threats(const Position& pos, Color side);

}  // namespace fcg::commentary
