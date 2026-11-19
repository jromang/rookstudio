#pragma once

// The user's games in progress on Lichess (#48, mockup M8, board "Fairplay"), wherever they play them: asked with the
// link's token (/api/account/playing) when the Home page shows, or heard as they start and end through the event stream
// when the token may read challenges (one stream per token; Lichess sends the games in progress on open, then each
// gameStart and gameFinish). What RookStudio locks while they last (fair play) is the App's: a live game locks every
// aid, a correspondence game the positions of that game.

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <json.hpp>

#include "lichess_api.hpp"

namespace fcg::lichess {

inline constexpr const char* SCOPE_CHALLENGE_READ = "challenge:read";

struct PlayingGame {
    std::string id;       // the game's, 8 characters
    std::string full_id;  // with the player's own 4: the address that opens it as that player
    bool white = true;
    std::string fen;  // its position now
    bool my_turn = false;
    std::string speed;  // "bullet", "blitz", "rapid", "classical", "correspondence"
    bool rated           = false;
    int64_t seconds_left = -1;  // the user's clock, or the time left to move by correspondence; -1 unknown
    std::string opponent;       // their name ("AI level 3" for the computer)
    int opponent_rating                       = 0;
    bool operator==(const PlayingGame&) const = default;

    [[nodiscard]] bool live() const { return speed != "correspondence"; }
    [[nodiscard]] int move() const;  // the move being played, from its FEN; 0 unknown
};

// A game as /api/account/playing lists it and the event stream sends it; nullopt when it is not one.
[[nodiscard]] std::optional<PlayingGame> readPlayingGame(const nlohmann::json& j);
// /api/account/playing's answer: its "nowPlaying".
[[nodiscard]] std::vector<PlayingGame> readPlaying(const nlohmann::json& j);

// The games in progress, with the link's token (no scope more).
[[nodiscard]] Answer<std::vector<PlayingGame>> playingGames(const std::string& token,
                                                            const std::atomic<bool>* cancel = nullptr);

// An event of the stream: the stream opened (the games in progress follow, as started), a game started, or ended.
struct GameEvent {
    enum class Kind { Opened, Started, Finished };
    Kind kind = Kind::Started;
    PlayingGame game;  // started or ended
};
// One line of the event stream read; nullopt for the empty lines that keep it open and the events of challenges.
[[nodiscard]] std::optional<GameEvent> readGameEvent(std::string_view line);

// The event stream heard on a thread of its own while it lives, opened again a minute after a break; the events taken
// from the window's thread.
class EventListener {
   public:
    explicit EventListener(std::string token);
    ~EventListener();
    EventListener(const EventListener&)            = delete;
    EventListener& operator=(const EventListener&) = delete;

    [[nodiscard]] std::vector<GameEvent> take();             // the events heard since the last call
    [[nodiscard]] std::string error() const;                 // why the stream broke last, empty while it is open
    [[nodiscard]] bool refused() const { return refused_; }  // Lichess said no to the token: it stops
    [[nodiscard]] const std::string& token() const { return token_; }

   private:
    void run();

    std::string token_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> refused_{false};
    mutable std::mutex mutex_;
    std::deque<GameEvent> events_;
    std::string error_;
    std::thread thread_;
};

}  // namespace fcg::lichess
