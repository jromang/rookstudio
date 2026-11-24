#pragma once

// The guided visit of a reviewed game (#94, mockup M13, board 7, point 16): each key moment of the review, the position
// before the game's move, and "Before seeing why: what would you have played?". The answer, a move played or typed
// (or "I don't know"), is judged as Guess the moves judges a guess (#81): the engine's move 3 points, one as good 2, a
// playable one 1, else 0; then the engine's move, an explanation held to the facts and what it rests on. A wrong answer
// becomes a card of the day's queue, as the user's mistakes do (mistakes.hpp).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <commentary/phrases.hpp>
#include <commentary/review.hpp>
#include <study/fsrs.hpp>

#include "game_analysis.hpp"
#include "mistakes.hpp"
#include "pgn_store.hpp"

namespace fcg {

// Where a visit stands at a moment: asking the user's move, the engine looking at it, the answer shown; its end.
enum class VisitPhase { Asking, Checking, Shown, End };

// What the visit's board shows by its phase: nothing before the answer (no evaluation, no arrow, no mark of the game's
// move), the answer, the engine's move and the game's after it.
struct VisitShows {
    bool evaluation = false;
    bool arrows     = false;
    bool notation   = false;  // the game's moves, the review's marks
};
[[nodiscard]] VisitShows visitShows(VisitPhase phase);

// The moments a visit goes through, as the plies of the game's moves: the review's key moments, in the game's order;
// the moves it marked ? and ?? when it has none.
[[nodiscard]] std::vector<std::size_t> visitMoments(const commentary::GameReport& report);

// An answer judged: its points (guessPoints(): the engine's move 3, as good 2, playable 1, else 0) by the engine's
// evaluations after the engine's move and after the answer, in pawns for the side that plays; and whether it is wrong:
// no point, or "I don't know".
struct VisitVerdict {
    int points = 0;
    bool wrong = true;
};
[[nodiscard]] VisitVerdict judgeVisitAnswer(bool engine_move, double best_pawns, double answer_pawns, bool dont_know);

// The card a wrong answer becomes: an exercise of the position before the game's move `ply` (the game's move leading to
// position `ply`), the review's best move to find, as the exercises of the user's mistakes are; marked a visit's, kept
// whatever the scans of the user's games find. None when the review does not name a legal best move there.
[[nodiscard]] std::optional<MistakeExercise> visitExercise(const ReplayGame& game, const GameReview& review,
                                                           std::size_t ply);

// Puts it in the record as a card of the day's queue: graded Again now, back within the day; an exercise of the same
// position already there keeps its own and its card gets the grade.
void addVisitCard(MistakeRecord& record, MistakeExercise exercise, int64_t now,
                  const study::fsrs::Scheduler& scheduler);

// "Why not?" (#94, point 16): the question the Questions' Compare action reads without the model, `asked` set against
// `reference` (UCI moves of `before`), in the assistant's language and letters: "Why not 5…Nxd5 rather than 5…Na5?",
// « Pourquoi pas 5…Cxd5 plutôt que 5…Ca5 ? ».
[[nodiscard]] std::string whyNotQuestion(const chess::Board& before, const std::string& asked,
                                         const std::string& reference, bool french);

}  // namespace fcg
