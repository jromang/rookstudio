#pragma once

// A chess.com player's games (#74, mockup M10), from its public API without an account: the months they played, then
// each month's games as PGN, asked for one at a time. A followed player keeps the months already added, the newest
// asked for again when looked at (it may have grown).

#include <atomic>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "net_failure.hpp"

namespace fcg::chesscom {

// The API's address: api.chess.com's, or a test's server (http://127.0.0.1:PORT).
void setServer(std::string base);

// A player's name as the API takes it: lower case, only letters, digits, '-' and '_'; empty when it holds anything
// else (nothing is asked then).
[[nodiscard]] std::string playerKey(std::string_view name);

// The list of the player's months, and one month's games as PGN.
[[nodiscard]] std::string archivesUrl(const std::string& player);
[[nodiscard]] std::string monthPgnUrl(const std::string& player, const std::string& month);  // month: YYYY/MM

// The months the list names, the oldest first ("2026/10"); empty for a list it cannot read.
[[nodiscard]] std::vector<std::string> parseArchives(std::string_view json);

// The player's list of months asked of the API (politeRequest) and read: the months, or none and why (#141), the
// network's kind (NotFound: no player of that name; an outage, another status, the host's pause after a 429). None and
// no failure: the player has no game. Blocks: call it from a thread of its own; `cancel` ends the wait for the host's
// turn or pause, and the request (#148).
[[nodiscard]] std::pair<std::vector<std::string>, NetFailure> fetchMonths(const std::string& player,
                                                                          const std::atomic<bool>* cancel = nullptr);
// Why the months could not be had, as the sheet says it.
[[nodiscard]] std::string monthsSaid(const NetFailure& failure);

// A followed player: the database their games go into, the months already added.
struct Followed {
    std::string name;
    std::string database;  // its folder
    std::set<std::string> months;
    bool operator==(const Followed&) const = default;
};
[[nodiscard]] std::string toJson(const std::vector<Followed>& players);
[[nodiscard]] std::vector<Followed> fromJson(std::string_view text);

// The months to ask for: those not added yet, and the newest added again (its games may have grown since).
[[nodiscard]] std::vector<std::string> monthsToAsk(const std::vector<std::string>& listed,
                                                   const std::set<std::string>& added);

}  // namespace fcg::chesscom
