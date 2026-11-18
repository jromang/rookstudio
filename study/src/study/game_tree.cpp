#include "game_tree.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace fcg {

chess::Board boardAt(const ReplayGame& game, bool chess960, int ply) {
    chess::Board board;
    board.set960(chess960);
    board.setFen(game.start_fen.empty() ? std::string(chess::constants::STARTPOS) : game.start_fen);
    const int n = std::clamp(ply, 0, static_cast<int>(game.moves.size()));
    for (int i = 0; i < n; ++i) board.makeMove<true>(game.moves[static_cast<std::size_t>(i)].move);
    return board;
}

int playOnGame(ReplayGame& game, bool chess960, int ply, chess::Move move) {
    ply = std::clamp(ply, 0, static_cast<int>(game.moves.size()));
    if (ply < static_cast<int>(game.moves.size()) && game.moves[static_cast<std::size_t>(ply)].move == move)
        return ply + 1;
    const auto board = boardAt(game, chess960, ply);
    game.moves.resize(static_cast<std::size_t>(ply));
    ReplayMove played;
    played.move  = move;
    played.mover = board.sideToMove();
    played.san   = chess::uci::moveToSan(board, move);
    game.moves.push_back(std::move(played));
    // The game's result is no longer this game's.
    game.result.clear();
    game.final_comment.clear();
    return ply + 1;
}

ReplayGame lineOf(const GameTree& tree, int line) {
    if (line < 0 || line >= static_cast<int>(tree.variations.size())) return tree.main;
    const auto& v = tree.variations[static_cast<std::size_t>(line)];
    auto game     = lineOf(tree, v.parent);
    game.moves.resize(std::min(game.moves.size(), static_cast<std::size_t>(std::max(0, v.from))));
    game.moves.insert(game.moves.end(), v.moves.begin(), v.moves.end());
    game.result.clear();
    game.final_comment.clear();
    return game;
}

int ownerOf(const GameTree& tree, int line, int ply) {
    while (line >= 0 && line < static_cast<int>(tree.variations.size()) &&
           ply < tree.variations[static_cast<std::size_t>(line)].from)
        line = tree.variations[static_cast<std::size_t>(line)].parent;
    return line < static_cast<int>(tree.variations.size()) ? line : -1;
}

int branchPly(const GameTree& tree, int line, int ply) {
    while (line >= 0 && line < static_cast<int>(tree.variations.size())) {
        const auto& v = tree.variations[static_cast<std::size_t>(line)];
        ply           = std::min(ply, v.from);
        line          = v.parent;
    }
    return ply;
}

std::pair<int, int> playInTree(GameTree& tree, bool chess960, int line, int ply, chess::Move move) {
    const auto shown = lineOf(tree, line);
    ply              = std::clamp(ply, 0, static_cast<int>(shown.moves.size()));
    const auto board = boardAt(shown, chess960, ply);
    ReplayMove played;
    played.move  = move;
    played.san   = chess::uci::moveToSan(board, move);
    played.mover = board.sideToMove();
    // The line's own next move.
    if (ply < static_cast<int>(shown.moves.size()) && shown.moves[static_cast<std::size_t>(ply)].move == move)
        return {line, ply + 1};
    // At its end, the line grows.
    if (ply == static_cast<int>(shown.moves.size())) {
        if (line < 0)
            tree.main.moves.push_back(std::move(played));
        else
            tree.variations[static_cast<std::size_t>(line)].moves.push_back(std::move(played));
        return {line, ply + 1};
    }
    // A variation from here that starts with it, else a new one.
    const int owner = ownerOf(tree, line, ply);
    for (std::size_t v = 0; v < tree.variations.size(); ++v) {
        const auto& var = tree.variations[v];
        if (var.parent == owner && var.from == ply && !var.moves.empty() && var.moves.front().move == move)
            return {static_cast<int>(v), ply + 1};
    }
    tree.variations.push_back({owner, ply, {std::move(played)}});
    return {static_cast<int>(tree.variations.size()) - 1, ply + 1};
}

int removeVariation(GameTree& tree, int line) {
    if (line < 0 || line >= static_cast<int>(tree.variations.size())) return -1;
    // It and those branching from it, from the last.
    std::vector<bool> gone(tree.variations.size(), false);
    gone[static_cast<std::size_t>(line)] = true;
    for (bool more = true; more;) {
        more = false;
        for (std::size_t v = 0; v < tree.variations.size(); ++v) {
            const int p = tree.variations[v].parent;
            if (!gone[v] && p >= 0 && gone[static_cast<std::size_t>(p)]) gone[v] = more = true;
        }
    }
    std::vector<int> index(tree.variations.size(), -1);
    std::vector<Variation> kept;
    for (std::size_t v = 0; v < tree.variations.size(); ++v) {
        if (gone[v]) continue;
        index[v] = static_cast<int>(kept.size());
        kept.push_back(tree.variations[v]);
    }
    for (auto& v : kept)
        if (v.parent >= 0) v.parent = index[static_cast<std::size_t>(v.parent)];
    const int parent = tree.variations[static_cast<std::size_t>(line)].parent;
    tree.variations  = std::move(kept);
    return parent < 0 ? -1 : index[static_cast<std::size_t>(parent)];
}

int promoteVariation(GameTree& tree, int line) {
    if (line < 0 || line >= static_cast<int>(tree.variations.size())) return -1;
    auto& v            = tree.variations[static_cast<std::size_t>(line)];
    const int parent   = v.parent;
    const int from     = v.from;
    auto& parent_moves = parent < 0 ? tree.main.moves : tree.variations[static_cast<std::size_t>(parent)].moves;
    const int start    = parent < 0 ? 0 : tree.variations[static_cast<std::size_t>(parent)].from;
    const auto local   = static_cast<std::size_t>(std::clamp(from - start, 0, static_cast<int>(parent_moves.size())));
    std::vector<ReplayMove> tail(parent_moves.begin() + static_cast<std::ptrdiff_t>(local), parent_moves.end());
    parent_moves.resize(local);
    parent_moves.insert(parent_moves.end(), v.moves.begin(), v.moves.end());
    tree.variations[static_cast<std::size_t>(line)].moves = std::move(tail);
    // Those branching from the moves that changed line follow them.
    for (std::size_t i = 0; i < tree.variations.size(); ++i) {
        auto& o = tree.variations[i];
        if (static_cast<int>(i) == line) continue;
        if (o.parent == parent && o.from > from)
            o.parent = line;
        else if (o.parent == line)
            o.parent = parent;
    }
    if (parent < 0) tree.main.result.clear();
    return parent;
}

}  // namespace fcg
