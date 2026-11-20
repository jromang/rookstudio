#include "assistant_notes.hpp"

namespace fcg {

std::vector<std::string> placeAssistantComments(GameTree& tree, bool chess960,
                                                const std::vector<AssistantTarget>& targets,
                                                const std::vector<std::string>& texts) {
    std::vector<std::string> put;
    for (std::size_t i = 0; i < texts.size() && i < targets.size(); ++i) {
        const auto& t = targets[i];
        if (texts[i].empty() || t.line >= static_cast<int>(tree.variations.size())) continue;
        const auto game = lineOf(tree, t.line);
        if (t.ply <= 0 || t.ply > static_cast<int>(game.moves.size()) ||
            boardAt(game, chess960, t.ply).getFen() != t.fen)
            continue;
        MoveNotes* notes = notesAt(tree, t.line, t.ply);
        if (!notes || byAssistant(*notes)) continue;
        notes->assistant = texts[i];
        put.push_back(texts[i]);
    }
    return put;
}

}  // namespace fcg
