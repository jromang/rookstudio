#pragma once

// Practical moves (#94, mockup M13, board 7, point 17), in the study Board's Analysis tab: "Against a human of 1500,
// rapid". Among the moves the engine puts within 0.3 pawn of its best (or that keep the tables' verdict), the one that
// scores best against a player of that level in that level's games: Lichess's explorer at the level's rating group and
// speed (it needs a linked account), else the user's databases, their games whose players' average rating is within the
// level's band; a move's score counts from 200 games. Outside known games, the move that leaves the opponent the fewest
// good answers by the engine ("2 replies of 31 hold"). Never a losing move.
//
// What leaves the machine: with a linked account, the position and the level to Lichess's explorer; else nothing.

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <chess.hpp>
#include <study/store.hpp>
#include <study/tree.hpp>
#include <types/score.hpp>

#include "analysis.hpp"
#include "lichess_api.hpp"

namespace fcg {

inline constexpr int PRACTICAL_WINDOW_CP  = 30;   // a candidate is this close to the engine's best
inline constexpr uint64_t PRACTICAL_GAMES = 200;  // a move's score at a level counts from so many games
// A move that turns a position the engine does not see lost into one it does is no candidate, whatever the window.
inline constexpr int PRACTICAL_LOST_CP = -150;

// The levels offered, each the middle of one of the explorer's rating groups (2600: its last, 2500 and more).
[[nodiscard]] const std::vector<int>& practicalLevels();
// The explorer's rating group of a level ("ratings=1400" for 1500), and the band of ratings the databases' games are
// taken in: the players' average within [first, second).
[[nodiscard]] int explorerGroup(int level);
[[nodiscard]] std::pair<int, int> levelBand(int level);
// The speeds offered, as Lichess names them.
[[nodiscard]] const std::vector<std::string>& practicalSpeeds();

// A score as centipawns for its side, a mate far beyond any: what the window compares.
[[nodiscard]] int scoreCp(const fastchess::Score& score);

// What a level's games played from a position: each move (UCI), its games and how they ended.
struct LevelMove {
    std::string uci;
    uint64_t games = 0;
    uint64_t white = 0, draws = 0, black = 0;
};
// From Lichess's explorer, asked at the level's group and speed.
[[nodiscard]] std::vector<LevelMove> levelMoves(const lichess::Explorer& answer);
// From the user's databases (their opening trees): the games that reached the position whose players' average rating
// is within the level's band, by the move they played; the most played first.
struct LevelSource {
    std::shared_ptr<const study::Tree> tree;
    std::shared_ptr<const study::Snapshot> snapshot;
};
[[nodiscard]] std::vector<LevelMove> levelMoves(const std::vector<LevelSource>& sources, const chess::Board& board,
                                                int level, const std::atomic<bool>* cancel = nullptr);
// The share of the points a move's games gave its player, in percent; none without a decided or drawn game.
[[nodiscard]] std::optional<double> moverScore(const LevelMove& move, chess::Color mover);

// A candidate: the engine's line for it, what the level's games say of it, and what the engine says of the opponent's
// answers when no games are known.
struct PracticalMove {
    std::string uci;
    fastchess::Score score;  // the engine's, for the side to move
    int64_t depth = 0;
    int loss_cp   = 0;  // behind the engine's best
    std::vector<std::string> pv;
    uint64_t games = 0;              // at the level
    std::optional<double> expected;  // the mover's score at the level, once PRACTICAL_GAMES games say it
    std::optional<int> holding;      // the opponent's replies that hold, by the engine
    int replies = 0;                 // of how many
};

// The candidates, from the engine's lines (MultiPV, the best first): the moves within PRACTICAL_WINDOW_CP of its best,
// none that loses (a mate against when the best is not, a position the engine sees lost when the best is not). With
// the tables' answer for the position, the moves that keep its verdict, the window aside.
[[nodiscard]] std::vector<PracticalMove> practicalCandidates(const std::vector<AnalysisLine>& lines,
                                                             const lichess::Tablebase* tables = nullptr);

enum class PracticalWay { Games, Replies, Engine };
struct PracticalChoice {
    std::size_t index = 0;  // in the candidates: the practical move
    PracticalWay way  = PracticalWay::Engine;
};
// The level's games put on the candidates, then the choice: among those with PRACTICAL_GAMES games or more, the best
// score for the side to move; else, the replies counted, the fewest that hold; else the engine's best.
[[nodiscard]] PracticalChoice choosePractical(std::vector<PracticalMove>& candidates,
                                              const std::vector<LevelMove>& level, chess::Color mover);
// Whether the candidates are outside the known games, their replies to be counted.
[[nodiscard]] bool outsideKnownGames(const std::vector<PracticalMove>& candidates);

// The opponent's replies that hold after a candidate, from an engine's lines of the position after it over all of them
// (MultiPV): those within PRACTICAL_WINDOW_CP of its best.
[[nodiscard]] int holdingReplies(const std::vector<AnalysisLine>& lines);

// The mark a reply deserves by what it costs its player, centipawns: ?! from 50, ? from 100, ?? from 300; empty below.
[[nodiscard]] const char* replyMark(int cost_cp);

}  // namespace fcg
