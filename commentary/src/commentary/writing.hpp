#pragma once

// The comments of a reviewed game (the prototype's crates/commentary/src/write.rs): what each move of the main line is
// commented from, and the template comments of the moves that deserve one.

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gather.hpp"
#include "notation.hpp"
#include "phrases.hpp"
#include "review.hpp"

namespace fcg::commentary {

// What theory says before the moves, by the ply of the position after each.
using Theories = std::map<std::size_t, Theory>;

// The opening the main line had reached at the position `ply`, when it started from the usual position.
[[nodiscard]] std::optional<std::string> openingAt(const MainLine& line, std::size_t ply);

// The context a reviewed move of the main line is commented from: the position before it, the engine's lines before and
// after it, the move before, the opening. `m.ply` must be a move of `line`; the context points into `line`, `m` and
// `analyses`, and lives as long as they do.
[[nodiscard]] MoveContext moveContext(const MainLine& line, const MoveReport& m, const Analyses& analyses,
                                      const Theory* theory, const Tablebases* tables, const Oracle* oracle);

// The template comments of the moves that deserve one, each with its move's ply: mistakes, only moves, key moments,
// plans, and the novelty when theory, as `theories` tells it, says what was played instead. The `oracle` confirms their
// facts when there is one.
[[nodiscard]] std::vector<std::pair<std::size_t, std::string>> templateComments(
    const MainLine& line, const GameReport& report, const Analyses& analyses, const Theories& theories,
    const Tablebases* tables, const Oracle* oracle, Lang lang, PieceLetters letters);

// The same comments as they are shown and written: each read sentence by sentence with the checks of the boards against
// its move's facts (Brief::heldTemplate(), #64), a sentence they refuse left out, a comment left with none dropped.
[[nodiscard]] std::vector<std::pair<std::size_t, std::string>> heldTemplateComments(
    const MainLine& line, const GameReport& report, const Analyses& analyses, const Theories& theories,
    const Tablebases* tables, const Oracle* oracle, Lang lang, PieceLetters letters);

}  // namespace fcg::commentary
