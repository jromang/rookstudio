#pragma once

// One or two sober sentences about a move, from its facts only (the prototype's crates/commentary/src/lib.rs: comment,
// refutation).

#include <optional>
#include <string>

#include "gather.hpp"
#include "notation.hpp"
#include "phrases.hpp"

namespace fcg::commentary {

// One or two sentences about the move, or nothing when there is nothing telling to say.
[[nodiscard]] std::optional<std::string> comment(const MoveContext& ctx, Lang lang, PieceLetters letters);

}  // namespace fcg::commentary
