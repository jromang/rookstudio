#pragma once

// A game and its variations walked and edited: the position after a line's moves, a line's moves from the start, a
// move played into the tree, a variation removed or promoted. The Board page edits games with them, and a repertoire
// (repertoire.hpp) is a game and its variations too.

#include <utility>

#include <chess.hpp>

#include "game.hpp"

namespace fcg {

// The position after the first `ply` moves of `game` (its start position for 0). The moves were checked when the game
// was read or played.
[[nodiscard]] chess::Board boardAt(const ReplayGame& game, bool chess960, int ply);

// Plays `move` on the game after its first `ply` moves: the game's next move when it is that one, otherwise the moves
// after `ply` give way to it. Returns the new ply.
int playOnGame(ReplayGame& game, bool chess960, int ply, chess::Move move);

// The moves of a line from the start: the game's (-1), or a variation's after those of its parents up to where it
// starts. Its tags are the game's.
[[nodiscard]] ReplayGame lineOf(const GameTree& tree, int line);
// The line that holds the move after position `ply` of `line`: the line itself, or a parent when `ply` is before the
// line starts.
[[nodiscard]] int ownerOf(const GameTree& tree, int line, int ply);
// The position of the game that a line leaves it at (its own start, or its first parent's); the line's ply for the
// game itself.
[[nodiscard]] int branchPly(const GameTree& tree, int line, int ply);
// Plays `move` after position `ply` of `line`: the line's next move when it is that one, a variation from there that
// starts with it, else a new variation; at the end of a line, the line grows. The line and ply shown after.
std::pair<int, int> playInTree(GameTree& tree, bool chess960, int line, int ply, chess::Move move);
// Removes a variation and those branching from it. Returns its parent, as numbered after the removal.
int removeVariation(GameTree& tree, int line);
// The variation becomes the line it branches from, whose moves after it become the variation: those branching from
// either follow their moves. Returns that line.
int promoteVariation(GameTree& tree, int line);

}  // namespace fcg
