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
    // The assistant's comments marked as its own (ASSISTANT_MARK); without, plain comments, for another program.
    bool assistant_mark = true;
};

[[nodiscard]] std::string writePgnGame(const GameTree& tree, const PgnWriteOptions& options = {});

// A move's notes as the commands and text of one comment, without its braces ("[%clk 0:01:00] [%eval 0.17] Good"),
// empty when it has none to write after the move. The assistant's comment is in it, marked, when the move has no
// other; beside another, it is a comment of its own (assistantComment()). The commands the tournament's comment
// gives back (engineComment()) are left to it.
[[nodiscard]] std::string noteComment(const ReplayMove& move, bool assistant_mark = true);
// The tournament's comment of a move, without its braces, as the writer gives it right after the move: as it was read
// (ReplayMove::engine_comment) while the move's data is still what it says; else made from that data, in fastchess's
// style ("book", "+0.35/18 1.234s, n=…, pv=\"…\""); empty when the move has none.
[[nodiscard]] std::string engineComment(const ReplayMove& move);
// The assistant's comment as the second comment after the move, marked: "[%rs assistant] The knight..." (#60); empty
// when the move has none, or no other for it to follow (noteComment() has it then).
[[nodiscard]] std::string assistantComment(const ReplayMove& move, bool assistant_mark = true);

}  // namespace fcg::study
