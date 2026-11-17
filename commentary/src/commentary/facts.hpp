#pragma once

// What the facts mean, as the prototype had it (crates/facts/src/lib.rs, impl Fact): whom a fact is good for, which
// facts are plans the books name or principles of their endgame chapters, and how much a fact weighs when choosing what
// to say.

#include <cstdint>
#include <optional>

#include "fact_types.hpp"

namespace fcg::commentary {

// The side the fact is good for, when it is good for one.
[[nodiscard]] std::optional<Color> beneficiary(const Fact& fact);
// Whether the fact is a plan the books have a name for, or a principle of their endgame chapters: what makes a move
// worth a word though nothing is wrong with it.
[[nodiscard]] bool isPlan(const Fact& fact);
// How much the fact weighs when choosing what to say: mates first, then won material…
[[nodiscard]] uint32_t salience(const Fact& fact);

}  // namespace fcg::commentary
