#pragma once

// Lichess's puzzles online (#80, mockup M12, board 1, point 4): a batch of 50 puzzles at most, never seen and at the
// user's Lichess rating (GET /api/puzzle/batch/{angle}, puzzle:read), solved here, online or not; at the batch's end
// its results go back in one request (POST /api/puzzle/batch/{angle}, puzzle:write), which brings the next batch. The
// batch and its answers are kept in the profile (puzzles/online.json) until Lichess took them: a 429, a network cut or
// the program closed loses nothing. What leaves the machine: the batch asked for (its theme or opening, its
// difficulty), then each puzzle's id with whether it was solved and whether it is rated.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <json.hpp>
#include <study/puzzles.hpp>

#include "lichess_api.hpp"

namespace fcg::lichess {

inline constexpr const char* SCOPE_PUZZLE_READ  = "puzzle:read";
inline constexpr const char* SCOPE_PUZZLE_WRITE = "puzzle:write";
inline constexpr int PUZZLE_BATCH               = 50;  // Lichess's most a batch

// The user's Lichess puzzle rating, as a batch's answer gives it.
struct PuzzleGlicko {
    double rating                              = 0;
    double deviation                           = 0;
    bool provisional                           = false;
    bool operator==(const PuzzleGlicko&) const = default;
};

struct PuzzleBatch {
    std::vector<study::Puzzle> puzzles;
    std::optional<PuzzleGlicko> glicko;
    std::size_t unread = 0;  // puzzles of the answer whose moves did not play: left out
};

// A puzzle's result as Lichess takes it.
struct PuzzleSolution {
    std::string id;
    bool win                                     = false;
    bool rated                                   = true;
    bool operator==(const PuzzleSolution&) const = default;
};
// What Lichess made of the results: each puzzle's change to the rating, and the next batch.
struct PuzzleRound {
    std::string id;
    bool win        = false;
    int rating_diff = 0;
};
struct PuzzleSolved {
    PuzzleBatch next;
    std::vector<PuzzleRound> rounds;
};

// A puzzle and its game as the API gives them: the game's moves (SAN) up to the opponent's last one, the puzzle's
// solution (UCI) after it. As a puzzle of the database: the position before that move, the move, then the solution.
// None when the moves do not play.
[[nodiscard]] std::optional<study::Puzzle> readPuzzleAndGame(const nlohmann::json& j);
[[nodiscard]] PuzzleBatch readPuzzleBatch(const nlohmann::json& j);
[[nodiscard]] PuzzleSolved readPuzzleSolved(const nlohmann::json& j);

// A batch of `nb` puzzles (50 at most) of `angle` (a theme, an opening, or "mix") at `difficulty` ("easier",
// "normal", "harder"), with a token allowed puzzle:read.
[[nodiscard]] Answer<PuzzleBatch> puzzleBatch(const std::string& token, const std::string& angle, int nb,
                                              const std::string& difficulty, const std::atomic<bool>* cancel = nullptr);
// The results sent, with a token allowed puzzle:write; `nb` > 0 brings the next batch in the same answer.
[[nodiscard]] Answer<PuzzleSolved> solvePuzzles(const std::string& token, const std::string& angle,
                                                const std::vector<PuzzleSolution>& solutions, int nb,
                                                const std::string& difficulty,
                                                const std::atomic<bool>* cancel = nullptr);

// The user's results of the last days on Lichess (GET /api/puzzle/dashboard/{days}, puzzle:read): in all and by theme.
struct PuzzleThemeResult {
    std::string key;  // Lichess's theme key
    int nb = 0, first_wins = 0;
};
struct PuzzleDashboard {
    int days = 0;
    int nb = 0, first_wins = 0;
    std::vector<PuzzleThemeResult> themes;  // the most played first
};
[[nodiscard]] PuzzleDashboard readPuzzleDashboard(const nlohmann::json& j);
[[nodiscard]] Answer<PuzzleDashboard> puzzleDashboard(const std::string& token, int days,
                                                      const std::atomic<bool>* cancel = nullptr);

// The batch as the profile keeps it.
struct OnlineBatch {
    std::string angle      = "mix";
    std::string difficulty = "normal";
    bool rated             = true;
    int64_t received       = 0;  // seconds since the epoch
    std::vector<study::Puzzle> puzzles;
    std::vector<PuzzleSolution> results;  // the answers so far, a puzzle each at most
    std::optional<PuzzleGlicko> glicko;
    // The last results sent: when, how many, what Lichess said ("" while none was sent).
    int64_t sent_at = 0;
    int sent_count  = 0;
    std::string sent_status;
    [[nodiscard]] bool complete() const { return !puzzles.empty() && results.size() >= puzzles.size(); }
    [[nodiscard]] std::size_t left() const { return puzzles.size() - std::min(results.size(), puzzles.size()); }
    // A puzzle's result, kept once (the first answer).
    void answer(const std::string& id, bool win);
    [[nodiscard]] bool answered(const std::string& id) const;
};
[[nodiscard]] std::optional<OnlineBatch> loadOnlineBatch(const std::filesystem::path& folder);
bool saveOnlineBatch(const std::filesystem::path& folder, const OnlineBatch& batch);
void forgetOnlineBatch(const std::filesystem::path& folder);

}  // namespace fcg::lichess
