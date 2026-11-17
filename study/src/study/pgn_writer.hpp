#pragma once

// A GameTree as PGN: the tags (the Seven Tag Roster first, then the others in their order), the moves with their
// notes and the engines' data, the variations nested in brackets, lines of at most 80 characters. What it writes,
// read back by readPgnGame(), gives the same tree, and written again the same text.

#include <functional>
#include <string>

#include "game.hpp"

namespace fcg::study {

struct PgnWriteOptions {
    bool chess960 = false;
    // Extra text after a move of the game (not of a variation), by its ply counted from 1: the analysis's symbols and
    // evaluations, for instance. Empty for none.
    std::function<std::string(int)> annotate;
};

[[nodiscard]] std::string writePgnGame(const GameTree& tree, const PgnWriteOptions& options = {});

// A move's notes as the commands and text of one comment, without its braces ("[%clk 0:01:00] [%eval 0.17] Good"),
// empty when it has none to write after the move.
[[nodiscard]] std::string noteComment(const ReplayMove& move);

}  // namespace fcg::study
