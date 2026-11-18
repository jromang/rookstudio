#pragma once

// The assistant's comments in a game's notes (mockup M5, board 3): written as the user's are, between braces, with a
// mark that tells them apart at the next reading, [%rs assistant]; another program reads them as ordinary comments.
// Corrected by the user, a comment becomes theirs: the mark goes.

#include <algorithm>
#include <string>
#include <string_view>

#include <study/game.hpp>

namespace fcg {

inline constexpr std::string_view ASSISTANT_MARK = "[%rs assistant]";

[[nodiscard]] inline bool byAssistant(const MoveNotes& notes) {
    return std::find(notes.commands.begin(), notes.commands.end(), ASSISTANT_MARK) != notes.commands.end();
}

inline void markByAssistant(MoveNotes& notes, bool mark) {
    std::erase(notes.commands, std::string(ASSISTANT_MARK));
    if (mark) notes.commands.emplace_back(ASSISTANT_MARK);
}

}  // namespace fcg
