#pragma once

// The Week in Chess (theweekinchess.com): every Monday a numbered issue of the week's games, its PGN in a zip. Free for
// personal use; nothing of it ships with RookStudio, an issue is downloaded when the user asks for it (#71, mockup
// M10).

#include <atomic>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <study/store.hpp>

#include "net_failure.hpp"

namespace fcg::twic {

// The site's address: theweekinchess.com's, or a test's server (http://127.0.0.1:PORT).
void setServer(std::string base);
// Its page of issues.
[[nodiscard]] std::string indexUrl();

struct Issue {
    int number = 0;
    std::string date;  // its Monday, YYYY-MM-DD
    uint32_t games                      = 0;
    bool operator==(const Issue&) const = default;
};

// The issues the index page lists, the newest first; empty when it lists none (the page changed).
[[nodiscard]] std::vector<Issue> parseIndex(std::string_view html);

// The index page asked of the site (politeRequest) and read: its issues, or none and why (#141), the network's kind
// (an outage, a status, the host's pause after a 429), Unexpected for a page that lists none the program can read.
// Blocks: call it from a thread of its own; `cancel` ends the wait for the host's turn or pause, and the request
// (#148).
[[nodiscard]] std::pair<std::vector<Issue>, NetFailure> fetchIndex(const std::atomic<bool>* cancel = nullptr);
// Why the index gave none, as the sheet says it: an outage and the host's pause in their words, a status and a page
// that cannot be read after "theweekinchess.com did not answer".
[[nodiscard]] std::string indexSaid(const NetFailure& failure);

// The zip of an issue's PGN, and its name.
[[nodiscard]] std::string pgnUrl(int number);
[[nodiscard]] std::string pgnFile(int number);

// The Monday a week after `date` (YYYY-MM-DD), when the next issue is due; empty for a date it cannot read.
[[nodiscard]] std::string nextMonday(const std::string& date);

// What a batch of games just added holds, for the report after an issue: its strongest events (the average rating
// of their rated players) and its best-rated players, `most` of each.
struct Report {
    struct Event {
        std::string name;
        int average    = 0;
        uint32_t first = 0, last = 0;  // its games' dates (packDate()), 0 when none is known
        uint64_t games = 0;
    };
    struct Player {
        std::string name;
        int elo        = 0;  // the highest rating the games give
        uint64_t games = 0;
    };
    std::vector<Event> events;
    std::vector<Player> players;
};
[[nodiscard]] Report reportOf(const study::Snapshot& snapshot, study::GameId from, study::GameId to,
                              std::size_t most = 4);

// What the profile keeps of TWIC: the issues added to each database, the weekly look for a new issue (off until the
// user ticks it), and the newest issue seen then.
struct State {
    bool weekly = false;
    std::string database;                        // the database the weekly issue goes into (its folder)
    std::map<std::string, std::set<int>> added;  // by database folder
    int64_t checked       = 0;                   // the last look at the index, seconds since 1970
    int newest            = 0;                   // the newest issue it listed
    uint32_t newest_games = 0;
    std::string newest_date;
    bool operator==(const State&) const = default;
};
[[nodiscard]] std::string toJson(const State& state);
[[nodiscard]] State stateFromJson(std::string_view text);  // a default state for a text it cannot read

// The newest issue not yet in `database`, when the last look found one.
[[nodiscard]] std::optional<Issue> newIssue(const State& state, const std::string& database);

// Whether the weekly look is due: ticked, and the last one over six hours ago or on another day than today's Monday
// issue could be out (the index is asked for at most four times a day).
[[nodiscard]] bool lookDue(const State& state, int64_t now);

}  // namespace fcg::twic
