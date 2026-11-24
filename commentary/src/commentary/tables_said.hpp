#pragma once

// What the endgame tables say of a move, in the assistant's words (#42, mockup M13, board 7, point 20): a move that
// throws a win away or loses a draw, with the answer that shows it and the mate the tables then give; the move that
// kept the outcome; a move that keeps it. Written from the tables' verdicts and distances alone, so that "throws the
// win away" or "the theoretical draw" is said only when the tables say so.

#include <optional>
#include <string>

#include "fact_parts.hpp"
#include "phrases.hpp"

namespace fcg::commentary {

struct TablesSaid {
    std::string move;  // numbered, in the reader's letters: "52.Rxh7"
    Color mover    = Color::White;
    Verdict before = Verdict::Draw;         // the position's outcome for the mover
    Verdict after  = Verdict::Draw;         // what the move leaves the mover
    std::optional<std::string> refutation;  // the opponent's best answer to it, numbered: "52…Kxh7"
    std::optional<int> mate;                // after the move, the moves to mate the tables give (DTM), when known
    std::optional<std::string> kept;        // the tables' best move, which kept `before`, numbered
    int men = 7;                            // the pieces of the position: the "7-piece tables"
};

// One to two sentences, in `lang`.
[[nodiscard]] std::string tablesSentences(const TablesSaid& said, Lang lang);

}  // namespace fcg::commentary
