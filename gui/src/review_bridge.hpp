#pragma once

// The window's analysis of a game (game_analysis.hpp) read the way the commentary reads one (commentary/review.hpp):
// the main line, and for each of its positions the engine's line and score, from White's point of view. The comments,
// the story and the annotation of a game are made from it (mockup M5).

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <commentary/review.hpp>
#include <commentary/tablebase.hpp>

#include "game_analysis.hpp"
#include "pgn_store.hpp"
#include "review_stats.hpp"

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

// The positions of `game` its history makes a draw of: a threefold repetition, or fifty moves without a capture or a
// pawn move. The analysis gives them the end's score and no line; a position alone does not tell.
[[nodiscard]] std::vector<bool> drawnByRule(const ReplayGame& game, bool chess960);

// What `review` found in each position of `line`: the engine's line and score, White's, and its second line where the
// review has one (#63); the end of the game where it ends, the positions `drawn` (drawnByRule()) a draw; nothing for a
// position not searched.
[[nodiscard]] commentary::Analyses analysesOf(const commentary::MainLine& line, const GameReview& review,
                                              const std::vector<bool>& drawn = {});

// The expected score of the side to move by the engine's first line, in percent, as the review reads a line
// (commentary::EngineLine::whiteExpected()).
[[nodiscard]] double moverExpected(const PositionEval& eval);

// Which positions of `game` the Chess space's review asks the engine's second line for (#63), for
// ReviewRunner::Setup::second_line: those where the move played can be an only move as the review tells one
// (commentary::review()), given the engine's first line there. The move must be the engine's first choice, neither
// theory (the game's {book} moves) nor the only way to take back, one of several; and the position not one-sided: the
// mover's expected score at least commentary::ONLY_MOVE_GAP, below which no other move can be that far behind, and at
// most SECOND_LINE_CEILING, above which the opponent is beyond saving whatever is played (the review's own limit).
// Measured on five long games (#63): 27 % of the moves get a second search, which finds 25 of the 33 only moves that
// second lines everywhere find; the 8 others in positions already won.
inline constexpr double SECOND_LINE_CEILING = 97.0;
[[nodiscard]] std::function<bool(int ply, const PositionEval& first)> secondLines(const ReplayGame& game,
                                                                                  bool chess960);
// The same without the first line's verdict: where a move can be an only move at all.
[[nodiscard]] std::vector<bool> onlyMoveCandidates(const ReplayGame& game, bool chess960);

// The review of `game` from `review`, `opening` telling how far it followed theory; none where mainLineOf() has none.
// With `tables`, the engine's lines give way to the outcomes they know (commentary::settle()) before the moves are
// judged.
[[nodiscard]] std::shared_ptr<const CommentaryReview> commentaryReview(const ReplayGame& game, bool chess960,
                                                                       const GameReview& review,
                                                                       const commentary::Opening& opening   = {},
                                                                       const commentary::Tablebases* tables = nullptr);

// The Review tab's key moments in the Chess space: the review's own, the dots of its curve (mockup M5, board 4), each
// with its verdict (the judged move that lost the most is the turning point; an only move is told with the engine's
// next best, #63); then where the players' engines disagreed, the endgame and the tablebases, as keyMoments() has
// them. `analysis` is the game's analysis the review was made from, `judged` judgedMoves() of it.
[[nodiscard]] std::vector<KeyMoment> reviewKeyMoments(const CommentaryReview& review, const ReplayGame& game,
                                                      const GameReview& analysis, const std::vector<MoveReview>& judged,
                                                      const GamePhases& phases, int tablebase);

// The moves judged as the commentary judges them (two readings of the engine, lichess's thresholds on the chances
// between people): the Chess space's symbols, which its comments agree with. One per move up to the last one the
// review reports, the loss in points of the mover's expected score.
[[nodiscard]] std::vector<MoveReview> judgedMoves(const CommentaryReview& review);

}  // namespace fcg
