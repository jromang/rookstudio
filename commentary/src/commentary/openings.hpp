#pragma once

// Opening names, from Lichess's chess-openings data set (CC0), looked up by position so that a transposition finds its
// name too (the prototype's crates/analysis/src/openings.rs).

#include <optional>
#include <span>
#include <string>

#include "position.hpp"

namespace fcg::commentary {

// The opening reached by `moves` from the usual starting position: the name of the last named position on the way.
[[nodiscard]] std::optional<std::string> openingName(std::span<const Move> moves);

}  // namespace fcg::commentary
