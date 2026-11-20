#pragma once

// The assistant's comments in a game's notes (mockups M5, board 3, and M11, board 5): in a slot of their own
// (MoveNotes::assistant), beside the comment the move may have already, the user's, Lichess's analysis or a game's
// annotator's, which they never change (#60). Written as a comment of its own, marked [%rs assistant]; another program
// reads two comments. Corrected by the user, a comment becomes theirs: it joins the move's other comment.

#include <string>
#include <vector>

#include <study/game_tree.hpp>

namespace fcg {

[[nodiscard]] inline bool byAssistant(const MoveNotes& notes) { return !notes.assistant.empty(); }

// The assistant's comment corrected into `text`: the user's from then on, after the comment the move had (whose text
// stays as it was), the assistant's slot emptied. Nothing when `text` is still the assistant's.
inline void correctAssistant(MoveNotes& notes, const std::string& text) {
    if (text == notes.assistant) return;
    notes.assistant.clear();
    if (text.empty()) return;
    if (!notes.after.empty()) notes.after += ' ';
    notes.after += text;
}

// A comment asked of the assistant: on the move leading to position `ply` of `line`, `fen` the position after it.
struct AssistantTarget {
    int line = -1, ply = 0;
    std::string fen;  // the move is still the game's when it leads there
};

// The assistant's comments put on their moves, `texts[i]` on `targets[i]`: on a move that still leads where it did,
// beside the comment it may have, which stays as it is (#60), unless the assistant has one there already, or the text
// is empty. Returns the texts put.
std::vector<std::string> placeAssistantComments(GameTree& tree, bool chess960,
                                                const std::vector<AssistantTarget>& targets,
                                                const std::vector<std::string>& texts);

}  // namespace fcg
