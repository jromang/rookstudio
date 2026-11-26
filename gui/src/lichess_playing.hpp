#pragma once

// The user's games in progress on Lichess (#48, mockup M8, board "Fairplay"), wherever they play them: asked with the
// link's token (/api/account/playing) when the Home page shows, or heard as they start and end through the event stream
// when the token may read challenges or play (one stream per token; Lichess sends the games in progress and the
// challenges on open, then each gameStart, gameFinish and challenge). What RookStudio locks while they last (fair play)
// is the App's: a live game locks every aid, a correspondence game the positions of that game. The same stream brings
// the challenges sent to the user and the fate of those they send (#84).

#include <array>
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
#include "lichess_share.hpp"

namespace chess {
class Board;
}

namespace fcg::study {
class Database;
}

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
    int opponent_rating = 0;
    // What the event stream tells more (#84): whether Lichess's Board API can play it (compat.board; unknown from
    // /api/account/playing), where it comes from ("lobby", "friend", "pool"...), its variant's key, and how it ended.
    std::optional<bool> board;
    std::string source;
    std::string variant;
    std::string status;                             // "mate", "resign", "outoftime"... once it ended
    std::string winner;                             // "white", "black", empty for none
    int rating_diff                           = 0;  // the user's, once a rated game ended
    std::string tournament, swiss;                  // the arena's id, the Swiss's id, for a tournament's game (#95)
    bool operator==(const PlayingGame&) const = default;

    [[nodiscard]] bool live() const { return speed != "correspondence"; }
    [[nodiscard]] int move() const;  // the move being played, from its FEN; 0 unknown
    // Still at the start position, before its first move. Every game of standard chess passes through it: by
    // correspondence such a game locks that position alone, and the positions of its line from its first move on.
    [[nodiscard]] bool atStart() const;
};

// A position's men and side to move, without its castling rights and en passant square, which FENs write differently:
// what tells a game's position apart, for the fair play's lock (#48) and the engine offered to Lichess (#96).
struct Men {
    std::array<uint64_t, 12> bb{};  // pawns to kings, White then Black
    bool white                        = true;
    bool operator==(const Men&) const = default;
};
[[nodiscard]] Men menOf(const chess::Board& board);
// The men of a FEN as Lichess writes it; none when it cannot be read (a variant's pockets end the placement).
[[nodiscard]] std::optional<Men> menOf(std::string_view fen);

// Whether RookStudio can play the game on its Board (#84): the Board API can (compat.board when the stream told it;
// else, as Lichess decides it, a rapid, classical or correspondence game, or a blitz one from a challenge), and its
// variant is standard chess or Chess960.
[[nodiscard]] bool playableHere(const PlayingGame& game);

// A game as /api/account/playing lists it and the event stream sends it; nullopt when it is not one.
[[nodiscard]] std::optional<PlayingGame> readPlayingGame(const nlohmann::json& j);
// /api/account/playing's answer: its "nowPlaying".
[[nodiscard]] std::vector<PlayingGame> readPlaying(const nlohmann::json& j);

// The games of `before` that `after` no longer lists: they ended (#49).
[[nodiscard]] std::vector<PlayingGame> endedGames(const std::vector<PlayingGame>& before,
                                                  const std::vector<PlayingGame>& after);

// A game of the user's that ended, brought in (#49): its PGN as Lichess exports it (its clocks, its opening, Lichess's
// evaluations when it was analysed there), added to `db` when one is given and it has two plies or more (Lichess
// aborts a game left before them).
struct Arrival {
    std::string pgn;         // empty when Lichess did not send it
    bool added     = false;  // to the database (false: it had the game already, or none was given)
    uint32_t plies = 0;
    char result    = '*';    // '1' 1-0, '0' 0-1, '=' a draw, '*' none
    bool analysed  = false;  // Lichess's evaluations in it ([%eval])
    Failure failure;         // why it did not come (Lichess's), or did not go in (the database's own words)
};
[[nodiscard]] Arrival bringGame(const std::string& id, const std::string& token, study::Database* db,
                                const std::atomic<bool>* cancel = nullptr);

// The games in progress, with the link's token (no scope more).
[[nodiscard]] Answer<std::vector<PlayingGame>> playingGames(const std::string& token,
                                                            const std::atomic<bool>* cancel = nullptr);

// A challenge (#84), sent to the user or by them, as the event stream and Lichess's answers give it.
struct Challenge {
    std::string id;        // the game's too, once accepted
    bool incoming = true;  // sent to the user ("in"), else by them ("out")
    std::string challenger, challenger_title;
    int challenger_rating       = 0;
    bool challenger_provisional = false;
    std::string dest;  // the one challenged
    int dest_rating = 0;
    bool rated      = false;
    std::string speed;  // Lichess's: "blitz", "rapid", "classical", "correspondence"...
    std::string variant = "standard";
    ChallengeClock clock;        // days > 0: by correspondence
    int limit_s    = 0;          // a real-time clock's base in seconds (under a minute too)
    bool unlimited = false;      // no clock at all
    std::string colour;          // the challenger's choice: "white", "black" or "random"
    std::string final_colour;    // the challenger's colour once drawn: "white" or "black"
    std::string fen;             // from a position; empty from the start
    bool board = true;           // the Board API can play it (compat.board)
    std::string decline_reason;  // why it was declined, as Lichess words it in the user's language
    std::string decline_key;     // and its key: "generic", "later", "toofast", "tooslow", "timecontrol", "rated"...
    bool operator==(const Challenge&) const = default;

    // The user's colour once known: the other one when it is sent to them.
    [[nodiscard]] std::optional<bool> userWhite() const;
};
// A challenge's JSON read (the event's "challenge", an answer's body); `compat` the event's own, when it has one.
[[nodiscard]] std::optional<Challenge> readChallenge(const nlohmann::json& j, const nlohmann::json* compat = nullptr);

// An event of the stream: the stream opened (the games in progress and the challenges follow, as new), a game started,
// or ended; a challenge came (sent to the user, or by them), was called back by its sender, or was declined.
struct GameEvent {
    enum class Kind { Opened, Started, Finished, Challenge, ChallengeCanceled, ChallengeDeclined };
    Kind kind = Kind::Started;
    PlayingGame game;     // started or ended
    Challenge challenge;  // the challenge's kinds
};
// One line of the event stream read; nullopt for the empty lines that keep it open and what is not one of these.
[[nodiscard]] std::optional<GameEvent> readGameEvent(std::string_view line);

// The event stream heard on a thread of its own while it lives, opened again after a break (5 s, then longer, a minute
// at most; a 429 makes the host wait its minute); the events taken from the window's thread.
class EventListener {
   public:
    explicit EventListener(std::string token);
    ~EventListener();
    EventListener(const EventListener&)            = delete;
    EventListener& operator=(const EventListener&) = delete;

    [[nodiscard]] std::vector<GameEvent> take();  // the events heard since the last call
    // Why the stream broke last: no answer, or Lichess's refusal; none while it is open, or when Lichess closed it.
    [[nodiscard]] Failure failure() const;
    [[nodiscard]] bool refused() const { return refused_; }  // Lichess said no to the token: it stops
    // The stream is open now: a seek may go (Lichess asks for the stream first, not to miss the game's start).
    [[nodiscard]] bool connected() const { return connected_; }
    [[nodiscard]] const std::string& token() const { return token_; }

   private:
    void run();

    std::string token_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> refused_{false};
    std::atomic<bool> connected_{false};
    mutable std::mutex mutex_;
    std::deque<GameEvent> events_;
    Failure failure_;
    std::thread thread_;
};

}  // namespace fcg::lichess
