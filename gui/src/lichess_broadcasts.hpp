#pragma once

// Lichess's official broadcasts: those on now and those to come, and a round followed as it is played: the round as it
// stands, then a stream on which Lichess sends a game whole again whenever it changes. The follower keeps the latest
// of each game, told apart by their address (GameURL), and starts again after a break.

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "lichess_api.hpp"

namespace fcg::lichess {

struct BroadcastRound {
    std::string id, name, url;
    int64_t starts = 0;  // ms since the epoch, 0 when not told
    bool finished  = false;
};
struct BroadcastTour {
    std::string id, name, url;
    int tier = 0;  // Lichess's rank of the event: 5 the best known
    std::string players, location, time_control;
};
struct BroadcastEntry {
    BroadcastTour tour;
    BroadcastRound round;  // the round on now, or the next one
    std::string group;     // the event the tournament is part of, when it is
};
struct BroadcastList {
    std::vector<BroadcastEntry> active, upcoming;
};
[[nodiscard]] Answer<BroadcastList> topBroadcasts(const std::atomic<bool>* cancel = nullptr);

// A round's game as Lichess last sent it.
struct BroadcastGame {
    std::string url;  // its address: what tells the games apart
    std::string white, black, result, board;
    int white_elo = 0, black_elo = 0;
    std::string pgn;       // the whole game, as sent
    uint64_t version = 0;  // the times it was sent
};

// A round followed on a thread of its own until stop() or the end of the follower.
class RoundFollower {
   public:
    // `token`, when given, lets Lichess open more streams at once; Lichess asks one allowed to read studies and
    // broadcasts (SCOPE_STUDY_READ) since 2026-08, and may refuse or slow down a round followed without.
    explicit RoundFollower(std::string round_id, std::string token = {});
    ~RoundFollower();
    RoundFollower(const RoundFollower&)            = delete;
    RoundFollower& operator=(const RoundFollower&) = delete;

    void stop();  // returns within a quarter of a second
    // The games, in the order they first came.
    [[nodiscard]] std::vector<BroadcastGame> games() const;
    [[nodiscard]] uint64_t changes() const noexcept { return changes_; }  // grows with each game that changes
    [[nodiscard]] bool streaming() const noexcept { return streaming_; }
    [[nodiscard]] std::string error() const;  // why the stream broke, last; empty while it runs
    // Lichess refused the round to this token, or to no token (401, 403): an account allowed to read broadcasts is
    // needed. Cleared when the stream opens.
    [[nodiscard]] bool refused() const noexcept { return refused_; }
    [[nodiscard]] uint32_t connections() const noexcept { return connections_; }

    // The pause before opening the stream again after a break: it doubles after each failure, from `first` to `most`.
    static void setRetry(std::chrono::milliseconds first, std::chrono::milliseconds most);

   private:
    void run();
    void take(std::string_view pgn);

    std::string round_, token_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> streaming_{false};
    std::atomic<bool> refused_{false};
    std::atomic<uint64_t> changes_{0};
    std::atomic<uint32_t> connections_{0};
    mutable std::mutex mutex_;
    std::vector<BroadcastGame> games_;
    std::map<std::string, std::size_t> index_;  // address -> place in games_
    std::string error_;
    std::thread thread_;
};

}  // namespace fcg::lichess
