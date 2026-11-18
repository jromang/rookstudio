#pragma once

// A Lichess account's games in a database: read as Lichess exports them (PGN with clocks, evaluations and openings,
// oldest first), added as they come, committed every so many games. The database remembers whose games it holds and
// the start of the latest one (lichess.json in its folder): the next sync asks for the games from a minute before,
// those it has already left out as duplicates. A cancel, or a lost connection, keeps what was committed.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <study/store.hpp>

namespace fcg::lichess {

// What a database holds of an account's games.
struct SyncState {
    std::string username;
    int64_t since = 0;  // the start of the latest game it has, in ms since the epoch (Lichess's `since`)
};
[[nodiscard]] std::optional<SyncState> loadSyncState(const std::filesystem::path& database);
void saveSyncState(const std::filesystem::path& database, const SyncState& state);  // throws std::runtime_error

struct SyncProgress {
    std::atomic<uint64_t> games{0};  // read so far
};

struct SyncReport {
    uint64_t read = 0, added = 0, duplicates = 0, damaged = 0;
    bool cancelled = false;
    std::string error;  // why it stopped early; the games committed before stay
};

// The games of `username` that `db` does not have yet, as far as Lichess's export goes (`token`, when given, an
// account's, lets Lichess send them faster). A database that holds another account's games is refused.
[[nodiscard]] SyncReport syncGames(study::Database& db, const std::string& username, const std::string& token,
                                   SyncProgress& progress, const std::atomic<bool>& cancel,
                                   std::size_t commit_games = 500);

// The start of a game as Lichess's tags tell it (UTCDate "2026.11.18", UTCTime "14:03:59"), in ms since the epoch;
// none when they do not.
[[nodiscard]] std::optional<int64_t> startOf(std::string_view utc_date, std::string_view utc_time);

}  // namespace fcg::lichess
