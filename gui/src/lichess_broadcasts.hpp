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
    bool ongoing   = false;
};
struct BroadcastTour {
    std::string id, name, url;
    int tier = 0;  // Lichess's rank of the event: 5 the best known
    std::string players, location, time_control;
    std::string format;  // what its organisers call it: "8-player round-robin", "9-round Swiss" (#92)
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
// Lichess's recent official broadcasts whose names hold `query` (#55): a token allowed to read broadcasts
// (SCOPE_STUDY_READ) since 2026-10; 401 or 403 without.
[[nodiscard]] Answer<std::vector<BroadcastEntry>> searchBroadcasts(const std::string& query, const std::string& token,
                                                                   const std::atomic<bool>* cancel = nullptr);

// A round's tournament and names, from the round's address (lichess.org/broadcast/<tour>/<round>/<id>), for a round
// followed from a link (#55); a token allowed to read broadcasts, Lichess slows it down without.
[[nodiscard]] Answer<BroadcastEntry> broadcastRoundEntry(const std::string& round_url, const std::string& token,
                                                         const std::atomic<bool>* cancel = nullptr);

// A broadcast tournament's players and their standings (#55), as its organisers gave them; no token.
struct BroadcastPlayer {
    std::string name, title, fed, team;
    int rating = 0, played = 0, rank = 0;
    int performance = 0, rating_diff = 0;  // 0 when not told
    double score = 0;
    bool scored  = false;  // Lichess gives the scores
};
[[nodiscard]] Answer<std::vector<BroadcastPlayer>> broadcastPlayers(const std::string& tour_id,
                                                                    const std::atomic<bool>* cancel = nullptr);
// A team event's standings, the leader first: match points, then game points. Empty for another event.
struct BroadcastTeam {
    std::string name;
    double match_points = 0, game_points = 0;
    int matches = 0;
};
[[nodiscard]] Answer<std::vector<BroadcastTeam>> teamStandings(const std::string& tour_id,
                                                               const std::atomic<bool>* cancel = nullptr);

// A round's game as Lichess last sent it.
struct BroadcastGame {
    std::string url;  // its address: what tells the games apart
    std::string white, black, result, board;
    int white_elo = 0, black_elo = 0;
    std::string pgn;       // the whole game, as sent
    uint64_t version = 0;  // the times it was sent
    std::string round_id;  // its round's, from its BroadcastURL: a tournament followed whole sends several rounds
    std::string round;     // its round's number, from its Round tag ("6" of "6.1")
};

// A round followed on a thread of its own until stop() or the end of the follower; or, `whole_tour`, all the ongoing
// rounds of a tournament (#55), each new one as it starts: `id` is then the tournament's.
class RoundFollower {
   public:
    // `token`, when given, lets Lichess open more streams at once; Lichess asks one allowed to read studies and
    // broadcasts (SCOPE_STUDY_READ) since 2026-08, and may refuse or slow down a round followed without.
    explicit RoundFollower(std::string id, std::string token = {}, bool whole_tour = false);
    [[nodiscard]] bool wholeTour() const noexcept { return whole_; }
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
    bool whole_ = false;
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
