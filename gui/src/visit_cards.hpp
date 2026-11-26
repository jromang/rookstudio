#pragma once

// A guided visit's wrong answers made exercises of the user's own (#94): apart from visit.hpp, which app.hpp includes,
// so that the mistakes' state (mistakes.hpp) changes without recompiling what includes App (#107).

#include <cstddef>
#include <cstdint>
#include <optional>

#include <study/fsrs.hpp>

#include "game_analysis.hpp"
#include "mistakes.hpp"
#include "pgn_store.hpp"

namespace fcg {

// The card a wrong answer becomes: an exercise of the position before the game's move `ply` (the game's move leading to
// position `ply`), the review's best move to find, as the exercises of the user's mistakes are; marked a visit's, kept
// whatever the scans of the user's games find. None when the review does not name a legal best move there.
[[nodiscard]] std::optional<MistakeExercise> visitExercise(const ReplayGame& game, const GameReview& review,
                                                           std::size_t ply);

// Puts it in the record as a card of the day's queue: graded Again now, back within the day; an exercise of the same
// position already there keeps its own and its card gets the grade.
void addVisitCard(MistakeRecord& record, MistakeExercise exercise, int64_t now,
                  const study::fsrs::Scheduler& scheduler);

}  // namespace fcg
