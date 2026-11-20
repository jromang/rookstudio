#pragma once

// The linked account's Lichess, for the Home page (#46, mockup M8, board "Accueil"): its rating in each speed with its
// history, the figures of a speed (best rating, best win, longest winning streak, results, time played) and the
// activity of the last days. Asked with the link's token, no scope more; kept a day in the profile, shown dated when
// Lichess cannot be reached.

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include "lichess_api.hpp"

namespace fcg::lichess {

// The speeds of the card, in its order, as Lichess names them.
inline constexpr std::array<std::string_view, 5> SPEEDS = {"bullet", "blitz", "rapid", "classical", "correspondence"};

struct RatingPoint {
    int64_t day                               = 0;  // days since 1970-01-01
    int rating                                = 0;
    bool operator==(const RatingPoint&) const = default;
};

struct SpeedRating {
    std::string key;  // "blitz"…
    int rating       = 0;
    int games        = 0;
    bool provisional = false;          // Lichess's "?": too few games for a sure rating
    std::vector<RatingPoint> history;  // oldest first
    bool operator==(const SpeedRating&) const = default;
};

struct ActivityGames {
    std::string key;  // the speed
    int win = 0, draw = 0, loss = 0;
    bool operator==(const ActivityGames&) const = default;
};

struct ActivityDay {
    int64_t start = 0;  // ms since the epoch, the day's start
    std::vector<ActivityGames> games;
    int puzzles_win = 0, puzzles_loss = 0;
    int studies                               = 0;
    bool operator==(const ActivityDay&) const = default;
};

struct Profile {
    std::string username;
    int64_t read_at = 0;                // seconds since the epoch: when Lichess was asked
    std::vector<SpeedRating> speeds;    // the five of SPEEDS, in its order
    std::vector<ActivityDay> activity;  // the last days, newest first
    // The puzzle rating (#80): where the RookStudio rating of the offline puzzles starts; 0 when Lichess gave none.
    int puzzle_rating = 0, puzzle_deviation = 0, puzzle_games = 0;
    bool operator==(const Profile&) const = default;
};

struct PerfFigures {
    int highest = 0;
    std::string highest_at;  // its date, YYYY-MM-DD
    int best_win = 0;        // the strongest opponent beaten, their rating
    int streak   = 0;        // the longest run of wins
    int win = 0, draw = 0, loss = 0;
    int64_t seconds                           = 0;  // time played
    bool operator==(const PerfFigures&) const = default;
};

// The account, its ratings and their history, its activity: three requests with `token` (/api/account, then the
// account's rating history and activity).
[[nodiscard]] Answer<Profile> accountProfile(const std::string& token, const std::atomic<bool>* cancel = nullptr);
// Another player's ratings, from their public profile (/api/user/{username}, no account): preparing a game (#52).
[[nodiscard]] Answer<Profile> publicProfile(const std::string& username, const std::atomic<bool>* cancel = nullptr);
// A speed's figures.
[[nodiscard]] Answer<PerfFigures> perfFigures(const std::string& username, std::string_view speed,
                                              const std::string& token, const std::atomic<bool>* cancel = nullptr);

// What the requests answer, read (for the tests too): /api/account's perfs, the rating history, the activity, a
// speed's figures. Values of another type are left out.
void readAccount(const nlohmann::json& j, Profile& out);
void readRatingHistory(const nlohmann::json& j, Profile& out);
void readActivity(const nlohmann::json& j, Profile& out);
[[nodiscard]] PerfFigures readPerfFigures(const nlohmann::json& j);

// What a speed did lately: its rating now against a week before, else a month before, when the history reaches so far.
struct Progress {
    int delta  = 0;
    bool week  = false;  // over the last week, else the last month
    bool known = false;
};
[[nodiscard]] Progress progressOf(const SpeedRating& speed, int64_t today);

// Kept in the profile: lichess-cache/account.json.
void keepProfile(const std::filesystem::path& root, const Profile& profile);
[[nodiscard]] std::optional<Profile> keptProfile(const std::filesystem::path& root);

}  // namespace fcg::lichess
