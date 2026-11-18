#pragma once

// The window's analysis of a game (game_analysis.hpp) read the way the commentary reads one (commentary/review.hpp):
// the main line, and for each of its positions the engine's line and score, from White's point of view. The comments,
// the story and the annotation of a game are made from it (mockup M5).

#include <memory>
#include <optional>

#include <commentary/review.hpp>
#include <commentary/tablebase.hpp>

#include "game_analysis.hpp"
#include "pgn_store.hpp"

namespace fcg {

// The commentary's review of a game: its main line, the positions analysed, and what the review made of them. Held
// together, since the review's moves point into the line and the analyses.
struct CommentaryReview {
    commentary::MainLine line;
    commentary::Analyses analyses;
    commentary::GameReport report;
};

// The main line of `game`, as the commentary plays it: none for a Chess960 game, or one whose start or moves it cannot
// read.
[[nodiscard]] std::optional<commentary::MainLine> mainLineOf(const ReplayGame& game, bool chess960);

// What `review` found in each position of `line`: the engine's line and score, White's; the end of the game where it
// ends; nothing for a position not searched.
[[nodiscard]] commentary::Analyses analysesOf(const commentary::MainLine& line, const GameReview& review);

// The review of `game` from `review`, `opening` telling how far it followed theory; none where mainLineOf() has none.
// With `tables`, the engine's lines give way to the outcomes they know (commentary::settle()) before the moves are
// judged.
[[nodiscard]] std::shared_ptr<const CommentaryReview> commentaryReview(const ReplayGame& game, bool chess960,
                                                                       const GameReview& review,
                                                                       const commentary::Opening& opening   = {},
                                                                       const commentary::Tablebases* tables = nullptr);

// The moves judged as the commentary judges them (two readings of the engine, lichess's thresholds on the chances
// between people): the Chess space's symbols, which its comments agree with. One per move up to the last one the
// review reports, the loss in points of the mover's expected score.
[[nodiscard]] std::vector<MoveReview> judgedMoves(const CommentaryReview& review);

}  // namespace fcg
