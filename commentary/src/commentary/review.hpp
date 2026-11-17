#pragma once

// A game's review: judgments, accuracy, key moments, computed from analyzed positions (the prototype's
// crates/analysis/src/report.rs and phase.rs).
//
// A move is judged by two readings of the engine, and the more severe one stands. What it costs a player: the chances
// between people, by lichess's formula and thresholds, so that the verdicts read the same (5, 10 and 15 points lost
// make an inaccuracy, a mistake, a blunder). And what it costs the position: the engine's own expected score, where a
// win thrown away or a level game lost is a blunder whatever the centipawns call it. Nothing is a blunder for a side
// already lost, or one that still wins. How a side stands, and which move was the only one, are told on the engine's
// scale alone.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "eval.hpp"
#include "position.hpp"

namespace fcg::commentary {

// A game's main line: where it starts, the moves played, and the position before each move and after the last.
class MainLine {
   public:
    MainLine(Position start, std::vector<Move> moves);

    [[nodiscard]] const std::vector<Move>& moves() const noexcept { return moves_; }
    // positions()[i] stands before moves()[i]; the last one after the last move.
    [[nodiscard]] const std::vector<Position>& positions() const noexcept { return positions_; }
    // The move that led to the position `ply`; none for the first one.
    [[nodiscard]] std::optional<Move> moveTo(std::size_t ply) const;

   private:
    std::vector<Move> moves_;
    std::vector<Position> positions_;
};

// How a game ends in a position, when it does: mate, or a draw by stalemate or too little material (shakmaty's
// outcome; the fifty-move rule and repetitions need the game's history).
[[nodiscard]] std::optional<GameEnd> gameEnd(const Position& pos);

// The engine's analyses of the main line's positions, by index; a position not analyzed yet has none.
using Analyses = std::vector<std::optional<PositionAnalysis>>;

// The phase of each position: the middlegame starts once pieces have been traded or developed, the endgame once few
// pieces remain.
[[nodiscard]] std::vector<Phase> phases(std::span<const Position> positions);

// `?!`, `?`, `??`.
[[nodiscard]] uint8_t nag(Judgment judgment) noexcept;

struct SideReport {
    std::optional<double> accuracy;
    std::optional<double> acpl;
    std::size_t inaccuracies = 0;
    std::size_t mistakes     = 0;
    std::size_t blunders     = 0;
    // Mean accuracy in the opening, middlegame and endgame.
    std::array<std::optional<double>, 3> phase_accuracy{};
    bool operator==(const SideReport&) const = default;
};

struct GameReport {
    // Main-line moves whose positions before and after are both analyzed, in game order.
    std::vector<MoveReport> moves;
    SideReport white, black;
    // The plies of the moves that decided the game, in game order.
    std::vector<std::size_t> key_moments;
    // Every move of the main line is reported.
    bool complete = false;

    // The report of the move that led to the position `ply`.
    [[nodiscard]] const MoveReport* get(std::size_t ply) const;
    [[nodiscard]] const SideReport& side(Color color) const noexcept { return color == Color::White ? white : black; }
};

// Where the main line stands against opening theory, in plies from the start.
struct Opening {
    // The first plies are theory.
    std::size_t book_plies = 0;
    // The ply of the first move never played before, counted from 0, if any.
    std::optional<std::size_t> novelty;
};

// Reviews the main line from whatever positions are analyzed so far.
[[nodiscard]] GameReport review(const MainLine& line, const Analyses& analyses, const Opening& opening);

// What the review makes of one move, from the engine's analyses of the positions before and after it.
struct Weighed {
    // The mover's expected score before and after the move, as the engine sees it.
    double win_before = 0.0, win_after = 0.0;
    // The mover's chances between people before and after the move.
    std::pair<double, double> chances{};
    double accuracy = 0.0;
    int32_t cp_loss = 0;
    // The judgment the move deserves, were it not theory.
    std::optional<Judgment> judgment;
    bool played_best = false;
    // The engine's first move was the only good one, and by how much it led the second.
    bool only                             = false;
    double only_gap                       = 0.0;
    bool operator==(const Weighed&) const = default;
};

// Weighs the move `played` by `mover`, given the analyses of the positions before and after it; none while one of them
// has nothing to say.
[[nodiscard]] std::optional<Weighed> weigh(const PositionAnalysis& before, const PositionAnalysis& after, Color mover,
                                           const Move& played);

// The move `played` in `position` takes back what `previous` just took, and nothing else could: that it had to be
// played needs no telling.
[[nodiscard]] bool takesBackTheOnlyWay(const Position& position, const std::optional<Move>& previous,
                                       const std::optional<Move>& played);

// Lichess's move accuracy, from the chances between people before and after the move.
[[nodiscard]] double moveAccuracy(double win_before, double win_after) noexcept;

// A move is the only solution when it beats the second best by this many points of the engine's expected score.
inline constexpr double ONLY_MOVE_GAP = 35.0;

}  // namespace fcg::commentary
