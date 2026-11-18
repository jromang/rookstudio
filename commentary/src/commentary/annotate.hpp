#pragma once

// A review written into a game the way engine-annotated games look (the prototype's annotate.rs): each move's
// evaluation, the review's symbols (?!, ?, ??, the only move □, the novelty N), and after a mistake the line that
// should have been played, marked "better is" (⌓) and ending with how it leaves the game.

#include <cstddef>
#include <vector>

#include <study/game.hpp>

#include "review.hpp"

namespace fcg::commentary {

struct AnnotateOptions {
    std::size_t variation_plies = 8;                  // the better lines' length, at most
    Judgment variations_from    = Judgment::Mistake;  // the moves at least this bad get one
};

// NAGs: "better is" (ChessBase's prefix of the line that should have been played), only move, novelty.
inline constexpr uint8_t NAG_BETTER_IS = 142;
inline constexpr uint8_t NAG_ONLY_MOVE = 7;
inline constexpr uint8_t NAG_NOVELTY   = 146;

// Sets `nag` on a move, replacing a NAG of its exclusive group (a move's assessment $1 to $9, a position's $10 to
// $19); takes it off when it is there. The move's assessment first, then the position's, then the others in order.
void toggleNag(std::vector<uint8_t>& nags, uint8_t nag);

// Writes `report` into `game`, whose main line is `line`'s moves. What the user wrote is kept: a move with an
// assessment of its own keeps it, and a better line the game already has is not added again. Returns the variations
// added (indices into game.variations), the engine's, for them to be taken out again.
std::vector<int> annotate(GameTree& game, const MainLine& line, const GameReport& report, const Analyses& analyses,
                          const AnnotateOptions& options = {});

}  // namespace fcg::commentary
