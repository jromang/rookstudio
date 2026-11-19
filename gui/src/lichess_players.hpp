#pragma once

// Chosen players' games followed live (#50, mockup M8, board "Prep"): who plays, from the players' status
// (/api/users/status, no account, a hundred names a request); their games, each asked whole when it starts (three moves
// behind, as Lichess shows a game going on to anyone), then its moves as they come (/api/stream/game/{id}, eight at a
// time, as Lichess allows an address), and asked whole again when it ends. The mosaic shows them as it shows a
// broadcast round.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <json.hpp>

#include "lichess_api.hpp"
#include "lichess_broadcasts.hpp"

namespace fcg::lichess {

inline constexpr const char* SCOPE_FOLLOW_READ = "follow:read";
inline constexpr std::size_t MOST_GAME_STREAMS = 8;          // the game streams Lichess lets an address open at once
inline constexpr const char* PLAYERS_MOSAIC    = "players";  // the mosaic of the followed players, not a round

struct PlayerStatus {
    std::string id;    // lowercase
    std::string name;  // as its owner writes it
    bool online  = false;
    bool playing = false;
    std::string game;   // the game it plays, its id; empty when unknown
    std::string clock;  // that game's time control ("3+2"); empty when unknown
};
// /api/users/status's answer: `playing` a flag or, with withGameMetas, the game ({id, clock, variant}).
[[nodiscard]] std::vector<PlayerStatus> readStatuses(const nlohmann::json& j);
// The players' status, a hundred names a request; no account.
[[nodiscard]] Answer<std::vector<PlayerStatus>> playerStatuses(const std::vector<std::string>& names,
                                                               const std::atomic<bool>* cancel = nullptr);

// The names of the users the account follows, as /api/rel/following streams them (one user a line).
[[nodiscard]] std::vector<std::string> readFollowing(std::string_view ndjson);
// Them, with a token that may read them (SCOPE_FOLLOW_READ).
[[nodiscard]] Answer<std::vector<std::string>> followingOf(const std::string& token,
                                                           const std::atomic<bool>* cancel = nullptr);

// A move of a game's stream ({"fen", "lm", "wc", "bc"}) added to its PGN when it follows from the PGN's last position:
// in SAN, with the mover's clock. True when it was added, or when the PGN had it already (the position is the
// event's); false when it does not follow (the game is asked whole again then).
bool addStreamMove(std::string& pgn, const nlohmann::json& event);
// Whether a line of a game's stream says the game ended (its status, sent again when it ends).
[[nodiscard]] bool streamEnded(const nlohmann::json& event);

// The games followed, on threads of their own: watch() says which, a game that ends stays until another takes its
// place.
class PlayersFollower {
   public:
    explicit PlayersFollower(std::size_t most = MOST_GAME_STREAMS);
    ~PlayersFollower();
    PlayersFollower(const PlayersFollower&)            = delete;
    PlayersFollower& operator=(const PlayersFollower&) = delete;

    // The games to follow (ids), `most` at most, the first ones first; the others stop.
    void watch(std::vector<std::string> ids);
    // The games, in the order they came: their PGN as it goes on, "*" until they end.
    [[nodiscard]] std::vector<BroadcastGame> games() const;
    [[nodiscard]] uint64_t changes() const noexcept { return changes_; }
    [[nodiscard]] std::string error() const;  // why a game could not be read, last

   private:
    struct Game {
        std::string id;
        BroadcastGame game;
        std::atomic<bool> stop{false};
        std::atomic<bool> running{true};
        std::thread thread;
    };
    void manage();         // the manager's thread: starts and stops the games' threads
    void follow(Game& g);  // a game's thread
    void publish(Game& g, std::string pgn);

    std::size_t most_;
    std::atomic<bool> stop_{false};
    std::atomic<uint64_t> changes_{0};
    mutable std::mutex mutex_;
    std::vector<std::string> wanted_;
    std::vector<std::unique_ptr<Game>> games_;
    std::string error_;
    std::thread manager_;
};

}  // namespace fcg::lichess
