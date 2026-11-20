#pragma once

// Theory studies (#77): an annotated PGN read as notes, one wherever the file says something about a move of one of
// its games, variations included: the comments before and after the move, its symbols, a written evaluation
// ([%eval]), its arrows and squares; and a game's comment before its first move, about its start. The notes are
// indexed by position, by the Polyglot keys the opening tree uses (ScanBoard): a position the file reaches by another
// move order finds them too (a transposition).
//
// A note is about the position its move leads to, and it is on that move from the position before: the Tree tab shows
// it at both, what the studies say of the position shown, and of the moves from it.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <types/score.hpp>

#include "game.hpp"

namespace fcg::study {

// What a file says at a move of one of its games, or before the game's first move.
struct TheoryNote {
    uint64_t from = 0;   // the position the move is played from; 0 for a comment before the game's first move
    uint64_t to   = 0;   // the position it leads to, which the note is about (the game's start, before its first move)
    std::string san;     // the move, as a board writes it (moveToSan); empty before the first move
    int number = 1;      // its move number
    bool black = false;  // Black's move ("7…Qb6")
    std::string before;  // the comment before the move
    std::string after;   // the comment after it (before the first move: the game's comment there)
    std::vector<uint8_t> nags;               // its symbols, $1 to $255, in their order
    std::vector<BoardMark> arrows, squares;  // [%cal] and [%csl]: drawn on the position it leads to
    std::optional<fastchess::Score> eval;    // a written evaluation ([%eval]), from White's point of view
    int depth     = 0;                       // its depth, when it was written with it
    uint32_t game = 0;                       // the file's game, 0 the first (splitGames' order)
    int line      = -1;                      // -1 the game's moves, else a variation as the PGN reader numbers it
    int ply       = 0;                       // the position of that line (from the game's start) it leads to
    std::vector<std::string> path;           // the moves from the game's start to the position it leads to
    std::vector<std::string> next;           // the moves after it in its line, four at most

    // A comment or an evaluation, not only symbols or marks.
    [[nodiscard]] bool hasText() const noexcept { return !before.empty() || !after.empty() || eval.has_value(); }
};

// A game of the file, as the notes name it.
struct TheoryGame {
    std::string name;       // its chapter (a Lichess study's), else its players, else its event; empty for none
    bool standard = true;   // played from the standard start: its notes' moves are comparable to a game's from there
    bool chess960 = false;  // its Variant tag says so
    bool read     = true;   // false: no game in its text, or no position to start from (no notes)
};

struct TheoryCounts {
    std::size_t games       = 0;  // in the file, read or not
    std::size_t unreadable  = 0;  // of them, without a game or a position to start from
    std::size_t variations  = 0;
    std::size_t notes       = 0;
    std::size_t positions   = 0;  // annotated: the positions the notes are about, each once
    std::size_t comments    = 0;
    std::size_t symbols     = 0;
    std::size_t evaluations = 0;
    std::size_t marks       = 0;  // arrows and squares
    std::size_t left_out    = 0;  // comments with the mark of TheoryOptions::left_out, which are no notes
};

struct TheoryOptions {
    // A [%...] command whose comment after the move is no note (the assistant's mark, "[%rs assistant]": its comments
    // are about one game's move, and the assistant comments any position on request). The move's symbols, evaluation
    // and marks stay notes.
    std::string_view left_out;
};

class TheoryNotes {
   public:
    [[nodiscard]] const std::vector<TheoryNote>& notes() const noexcept { return notes_; }
    [[nodiscard]] const std::vector<TheoryGame>& games() const noexcept { return games_; }
    [[nodiscard]] const TheoryCounts& counts() const noexcept { return counts_; }
    // The first problem met, with the game it is in; empty when there was none.
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

    // The notes about the position, those whose move leads to it; and those on the moves from it. In the file's
    // order: its games, each game's moves, then its variations, each one's moves.
    [[nodiscard]] std::vector<const TheoryNote*> about(uint64_t key) const;
    [[nodiscard]] std::vector<const TheoryNote*> from(uint64_t key) const;

    // The notes of a game (`chess960`: its castling is Chess960's), the next game of the file; `problem`: what its
    // reading met (an illegal move, which ends its line), the first one kept as the file's error.
    void add(const GameTree& tree, bool chess960, const TheoryOptions& options = {}, const std::string& problem = {});
    // A game of the file that could not be read, `why`: it keeps its place, so that the games after it keep their
    // numbers.
    void skip(const std::string& why);

   private:
    void index(std::size_t note);

    std::vector<TheoryNote> notes_;
    std::vector<TheoryGame> games_;
    TheoryCounts counts_;
    std::string error_;
    std::unordered_map<uint64_t, std::vector<uint32_t>> about_, from_;
};

// The notes of every game of a PGN text, as splitGames() cuts it. `cancel`, once set, stops the reading with what was
// read; `done` follows the bytes read.
[[nodiscard]] TheoryNotes readTheory(std::string_view text, const TheoryOptions& options = {},
                                     const std::atomic<bool>* cancel = nullptr, std::atomic<uint64_t>* done = nullptr);

// A game's name as the notes give it: its ChapterName tag (a Lichess study's), else its players ("White – Black"),
// else its event; empty when it has none of them.
[[nodiscard]] std::string theoryGameName(const ReplayGame& game);

// The key of a position as the opening tree has it (ScanBoard's, Polyglot's); 0 for none (a board without its kings).
[[nodiscard]] uint64_t theoryKey(const chess::Board& board);

}  // namespace fcg::study
