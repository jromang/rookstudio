#pragma once

// What an engine found in a position, and the review of a move, as the comments read them (the prototype's
// crates/analysis: eval.rs, phase.rs and the move report of report.rs).

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "oracle.hpp"
#include "position.hpp"

namespace fcg::commentary {

// Centipawn scores are capped here, mates count as this much (as lichess does).
inline constexpr int32_t CP_CAP = 1000;

// One engine line. Its score is from White's point of view.
struct EngineLine {
    std::vector<Move> moves;
    Score score;
    std::optional<std::array<uint16_t, 3>> wdl;  // win, draw, loss in per mille, White first
    uint32_t depth = 0;

    [[nodiscard]] int32_t whiteCp() const noexcept;
    // The line's score is the tablebases', not the engine's: a certainty, and the cap, a level score or a mate.
    [[nodiscard]] bool isExact() const noexcept;
    // White's expected score in percent, as the engine itself sees it: from its win, draw and loss figures when it
    // gives them, else from its centipawns (expected()).
    [[nodiscard]] double whiteExpected() const noexcept;
    bool operator==(const EngineLine&) const = default;
};

[[nodiscard]] int32_t cappedCp(Score score) noexcept;
// The expected score in percent that centipawns stand for, for the side whose centipawns these are, with engines that
// anchor their scores on the pawn (+100: half the games won).
[[nodiscard]] double expected(int32_t cp) noexcept;
// A score in percent seen from `side`, given White's.
[[nodiscard]] constexpr double forSide(double white, Color side) noexcept {
    return side == Color::White ? white : 100.0 - white;
}
// The chances between people, in percent, for the side whose centipawns these are (lichess formula).
[[nodiscard]] double winPercent(int32_t cp) noexcept;
// An engine line from `pos`, its score turned to the side to move: what the oracle answers.
[[nodiscard]] Searched searched(const Position& pos, const EngineLine& line);

// Opening, middlegame, endgame along a game, divided the way lichess does it.
enum class Phase : uint8_t { Opening, Middlegame, Endgame };

enum class Judgment : uint8_t { Inaccuracy, Mistake, Blunder };

// How a side stands, from its expected score: the seven steps annotators tell apart.
enum class Standing : uint8_t { Lost, ClearlyWorse, SlightlyWorse, Balanced, SlightlyBetter, ClearlyBetter, Winning };
// From the expected score in percent of the side described.
[[nodiscard]] Standing standingOf(double expected) noexcept;
// The position symbol White's expected score deserves (=, ⩲, ±, +− and their mirrors), by the same steps.
[[nodiscard]] uint8_t positionNag(double white_expected) noexcept;

// The review's verdict on one move.
struct MoveReport {
    Color mover = Color::White;
    Phase phase = Phase::Opening;
    // The mover's expected score before and after the move, in percent, as the engine sees it.
    double win_before = 50.0;
    double win_after  = 50.0;
    // Lichess's accuracy, from the chances between people before and after the move.
    double accuracy = 100.0;
    // Centipawns lost, for the average centipawn loss.
    int32_t cp_loss = 0;
    std::optional<Judgment> judgment;
    bool played_best = false;
    // The best move was the only one, and it was played.
    bool only_move = false;
    // How far the best move was ahead of the second best, in points of expected score.
    double only_gap = 0.0;
    bool key        = false;
    // Opening theory: not judged, the players followed known games.
    bool book = false;
    // The first move no reference game had played.
    bool novelty = false;
    // The move carries out a plan the books have a name for, the first of its kind for its side.
    bool plan = false;

    [[nodiscard]] double loss() const noexcept { return win_before > win_after ? win_before - win_after : 0.0; }
    // A move worth a word: a mistake, an only move, a turning point or a plan.
    [[nodiscard]] bool isNotable() const noexcept { return judgment.has_value() || only_move || key || plan; }
};

}  // namespace fcg::commentary
