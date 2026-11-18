#pragma once

// A game and its variations walked and edited: the position after a line's moves, a line's moves from the start, a
// move played into the tree, a variation removed or promoted. The Board page edits games with them, and a repertoire
// (repertoire.hpp) is a game and its variations too.

#include <utility>
#include <vector>

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

// The notes of the move that leads to position `ply` of `line`; none at the start.
[[nodiscard]] MoveNotes* notesAt(GameTree& tree, int line, int ply);
// Takes away the move that leads to position `ply` of `line` and those after it in its line, with the variations
// branching from them; the whole variation for its first move. An alternative to the first move taken away carries
// the line on. The line and ply shown after.
std::pair<int, int> truncateAt(GameTree& tree, int line, int ply);
// A symbol on a move, or off it when it has it: one judgement of the move (!, ?, ...: $1 to $9) and one of the
// position ($10 to $19) at most.
void toggleNag(std::vector<uint8_t>& nags, uint8_t nag);
// An arrow (from != to) or a square's mark in a colour (G, R, Y, B) on a move: off when it has the same, recoloured
// when it has it in another colour.
void toggleMark(MoveNotes& notes, char colour, int from, int to);

}  // namespace fcg
