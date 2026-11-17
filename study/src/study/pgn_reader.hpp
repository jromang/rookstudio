#pragma once

// A game's PGN into a GameTree: its tags, its moves with their notes (comments before and after, symbols, arrows and
// squares, clocks, evaluations, other [%...] commands kept verbatim), the engines' data of a tournament's comments,
// and its variations, nested to any depth.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "game.hpp"

namespace fcg::study {

struct PgnRead {
    std::optional<GameTree> tree;  // none: no game in the text, or a start position that is no position
    bool chess960 = false;         // the Variant tag says so
    // The first problem met, empty when there was none. The moves read before it are kept: an illegal move ends its
    // line (the game, or the variation, whose other moves are left out).
    std::string error;
    std::size_t end = 0;  // where the game's text ends: the next game, if any, starts there
};

// The first game of `text`.
[[nodiscard]] PgnRead readPgnGame(std::string_view text);

// A text as UTF-8: as it is when it is valid UTF-8, else read as Windows-1252 (the code page of older PGN files).
[[nodiscard]] std::string utf8Text(std::string_view text);

// A comment's commands taken out into the move ([%clk], [%eval], [%emt], [%cal], [%csl]; any other kept verbatim in
// its notes), what is left of the text then going to its notes' comment after it, or, when it is a tournament's
// comment ("+0.35/18 1.234s, ..."), to its engine fields. Returns that tournament comment's text, empty for another.
std::string applyMoveComment(std::string_view comment, ReplayMove& move);

}  // namespace fcg::study
