#pragma once

// Playing on Lichess from RookStudio (#84, mockup M12, board 5), through Lichess's Board API, made for people (an
// engine's help, or anyone's, is forbidden there): a seek in the lobby (rapid, classical and correspondence only, as
// Lichess keeps blitz and bullet for its site), a challenge sent to a player (blitz too), a challenge received, and the
// game itself, its moves heard on a stream of its own and played one request each. Everything with the account's token
// once it may play (board:play, asked at the first use); the event stream, one per token, is lichess_playing's, shared
// with the fair play's lock (#48) and the finished games (#49): it tells when a game starts, a challenge comes, or one
// is declined. No ImGui here: the Play page and the Board page draw it (view_play_lichess.cpp).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <chess.hpp>
#include <json.hpp>

#include <study/game.hpp>

#include "lichess_api.hpp"
#include "lichess_share.hpp"

namespace fcg::lichess {

// Playing the user's games, seeking, challenging, answering challenges: Lichess's Board API.
inline constexpr const char* SCOPE_BOARD_PLAY = "board:play";

// ---------------------------------------------------------------------------
// Clocks, and what the Board API allows

// Lichess's speed of a time control, as scalachess counts it: a real-time clock's base and 40 times its increment,
// under 30 s "ultraBullet", 3 min "bullet", 8 min "blitz", 25 min "rapid", else "classical"; "correspondence" with
// days.
[[nodiscard]] std::string speedOf(const ChallengeClock& clock);
// The Board API's rules (lichess.org/api, its Board tag): a seek is rapid, classical or correspondence; a direct
// challenge may be blitz too; bullet never.
[[nodiscard]] bool seekAllowed(const ChallengeClock& clock);
[[nodiscard]] bool challengeAllowed(const ChallengeClock& clock);
// The values Lichess takes: days a move 1, 2, 3, 5, 7, 10 or 14; a seek's base 0 to 180 minutes and increment 0 to 180
// seconds; a challenge's base a whole number of minutes up to 180 (0 too) and increment 0 to 60 seconds.
[[nodiscard]] bool validDays(int days);
[[nodiscard]] bool validClock(const ChallengeClock& clock, bool seek);

// A move of a game's stream as the board plays it: UCI, castling written either way (e1g1, or the king taking its rook
// as Chess960 writes it); none when it is not legal in `board`.
[[nodiscard]] std::optional<chess::Move> legalUci(const chess::Board& board, std::string_view uci);

// ---------------------------------------------------------------------------
// One request each

// The two APIs a game is played through: the Board API, for people (#84), and the Bot API, for a bot account's engine
// (#85, lichess_bot.hpp). A game's endpoints are the same under /api/board/ and /api/bot/.
enum class PlayApi { Board, Bot };

// What Lichess answered: taken, or why not (Failure: its own words when it gave some, "Not your turn, or game already
// over"; status 0 without an answer, a cut).
struct Reply : Failure {
    bool ok = false;
};
// A POST of the API (`path`, from "/api/"), its form `fields` (none: no body), with the token: Lichess's answer, its
// own words when it refused. The tournaments' requests go through it too (#95).
[[nodiscard]] Reply lichessPost(const std::string& path, const std::string& token,
                                const std::vector<std::pair<std::string, std::string>>& fields = {},
                                const std::atomic<bool>* cancel = nullptr, int timeout_s = 15);
// A move (UCI) in a game of the user's (of the bot's), a draw offered (or taken) with it.
[[nodiscard]] Reply boardMove(const std::string& token, const std::string& game, const std::string& uci,
                              bool offer_draw = false, const std::atomic<bool>* cancel = nullptr,
                              PlayApi api = PlayApi::Board);
// What the game's card asks: "resign", "abort", "draw/yes" (offer, or take the offer), "draw/no", "takeback/yes",
// "takeback/no", "claim-victory", "claim-draw" (the opponent gone long enough).
[[nodiscard]] Reply boardAction(const std::string& token, const std::string& game, std::string_view action,
                                const std::atomic<bool>* cancel = nullptr, PlayApi api = PlayApi::Board);
// A line in the game's chat, the players' room.
[[nodiscard]] Reply boardChat(const std::string& token, const std::string& game, const std::string& text,
                              const std::atomic<bool>* cancel = nullptr, PlayApi api = PlayApi::Board);
// A challenge received taken, or declined with one of Lichess's reasons ("generic", "later", "tooFast", "tooSlow",
// "timeControl", "rated", "casual", "standard", "variant", "noBot", "onlyBot"); one sent called back.
[[nodiscard]] Reply acceptChallenge(const std::string& token, const std::string& id,
                                    const std::atomic<bool>* cancel = nullptr);
[[nodiscard]] Reply declineChallenge(const std::string& token, const std::string& id, std::string_view reason,
                                     const std::atomic<bool>* cancel = nullptr);
[[nodiscard]] Reply cancelChallenge(const std::string& token, const std::string& id,
                                    const std::atomic<bool>* cancel = nullptr);

// ---------------------------------------------------------------------------
// A seek in Lichess's lobby

struct SeekAsk {
    ChallengeClock clock{10, 5, 0};
    bool rated     = true;
    int rating_min = 0, rating_max = 0;  // the opponents' ratings; 0 and 0: anyone
    bool operator==(const SeekAsk&) const = default;
};

// A real-time seek lives while its request stays open, on a thread of its own: Lichess closes it when someone takes it
// (the game's start comes on the event stream) or when it expires; closing it calls the seek back. A correspondence
// seek is one request: Lichess keeps it in its lobby until someone takes it, and the API cannot call it back.
class Seek {
   public:
    enum class State {
        Opening,  // asked
        Open,     // in the lobby, waiting for an opponent
        Closed,   // Lichess closed it: taken (or expired); a correspondence seek made (its id())
        Failed,   // refused: error() says why
    };
    Seek(std::string token, SeekAsk ask);
    ~Seek();  // a seek still open is called back
    Seek(const Seek&)            = delete;
    Seek& operator=(const Seek&) = delete;

    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] std::string error() const;
    [[nodiscard]] std::string id() const;  // a correspondence seek's
    [[nodiscard]] const SeekAsk& ask() const { return ask_; }
    [[nodiscard]] std::chrono::steady_clock::time_point since() const { return since_; }
    [[nodiscard]] std::chrono::steady_clock::time_point closedAt() const;

   private:
    void run();

    std::string token_;
    SeekAsk ask_;
    std::chrono::steady_clock::time_point since_ = std::chrono::steady_clock::now();
    std::atomic<State> state_{State::Opening};
    std::atomic<bool> stop_{false};
    mutable std::mutex mutex_;
    std::string error_, id_;
    std::chrono::steady_clock::time_point closed_{};
    std::thread thread_;
};

// ---------------------------------------------------------------------------
// A challenge sent to a player

struct ChallengeAsk {
    std::string user;  // their name on Lichess
    ChallengeClock clock{5, 3, 0};
    bool rated                                 = false;
    std::string colour                         = "random";  // the user's: "white", "black", "random"
    bool operator==(const ChallengeAsk&) const = default;
};

// A real-time challenge is held by its request (keepAliveStream: it would expire after 20 s otherwise) until Lichess
// says it was accepted, declined or called back ({"done": ...}); a correspondence one is one request, its fate told by
// the event stream. Called back, it is cancelled at Lichess too.
class ChallengeOut {
   public:
    enum class State {
        Sending,  // asked
        Waiting,  // sent: the player has it
        Done,     // accepted, declined or called back: done()
        Failed,   // refused: error() says why
    };
    ChallengeOut(std::string token, ChallengeAsk ask);
    ~ChallengeOut();  // one still waiting is called back, unless settle() said how it ended
    ChallengeOut(const ChallengeOut&)            = delete;
    ChallengeOut& operator=(const ChallengeOut&) = delete;

    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] std::string id() const;
    [[nodiscard]] std::string done() const;  // "accepted", "declined", "canceled"; empty before
    [[nodiscard]] std::string error() const;
    [[nodiscard]] const ChallengeAsk& ask() const { return ask_; }
    [[nodiscard]] std::chrono::steady_clock::time_point since() const { return since_; }
    void cancel();  // called back: its request closed, Lichess asked to cancel it
    // Its fate heard elsewhere (the event stream: the game started, or the player declined): its request closed, and
    // nothing asked of Lichess, which would abort a game accepted but not yet played.
    void settle(const std::string& done);

   private:
    void run();

    std::string token_;
    ChallengeAsk ask_;
    std::chrono::steady_clock::time_point since_ = std::chrono::steady_clock::now();
    std::atomic<State> state_{State::Sending};
    std::atomic<bool> stop_{false};
    std::atomic<bool> settled_{false};
    mutable std::mutex mutex_;
    std::string id_, done_, error_;
    std::thread thread_;
};

// ---------------------------------------------------------------------------
// A game played: its stream

struct BoardPlayer {
    std::string id, name, title;
    int rating                                = 0;
    bool provisional                          = false;
    int ai                                    = 0;  // Lichess's computer: its level, 0 for a person
    bool operator==(const BoardPlayer&) const = default;
};

struct ChatLine {
    std::string room;  // "player" or "spectator"
    std::string user, text;
    bool operator==(const ChatLine&) const = default;
};

// The players' chat of a game so far (its stream tells the lines said after it opens).
[[nodiscard]] Answer<std::vector<ChatLine>> boardChatLines(const std::string& token, const std::string& game,
                                                           const std::atomic<bool>* cancel = nullptr,
                                                           PlayApi api                     = PlayApi::Board);

// A game as its stream told it (gameFull, then gameState, chatLine, opponentGone), and the stream's connection.
struct BoardState {
    uint64_t version = 0;
    bool full        = false;  // gameFull came: the players, the clock
    std::string id;
    BoardPlayer white, black;
    std::string speed;  // "rapid", "classical", "correspondence", "blitz"...
    std::string perf;   // its name, as Lichess translates it
    bool rated          = false;
    std::string variant = "standard";
    std::string initial_fen;                  // empty: the start position
    int64_t clock_ms = -1, increment_ms = 0;  // a real-time clock's base and increment; -1 none
    int days = 0;                             // by correspondence: days a move
    std::vector<std::string> moves;           // UCI
    // The clocks when they were told, and when (their own clock runs from there for the side to move).
    int64_t white_ms = 0, black_ms = 0;
    std::chrono::steady_clock::time_point told{};
    std::string status = "started";  // Lichess's: "created", "started", "mate", "resign", "outoftime", "draw"...
    std::string winner;              // "white", "black", empty for none
    bool white_draw = false, black_draw = false;          // a draw offered by that side
    bool white_takeback = false, black_takeback = false;  // a takeback asked by that side
    int64_t first_move_ms = -1;  // the time left to play the first move before Lichess aborts the game; -1 none
    bool opponent_gone    = false;
    int64_t claim_in_s    = -1;  // the opponent gone: the seconds before a win (or a draw) can be claimed
    std::chrono::steady_clock::time_point gone_told{};
    std::vector<ChatLine> chat;
    // A tournament's game (#95): its arena, or its Swiss, as gameFull names them; a side gone berserk (half its clock).
    std::string tournament, swiss;
    bool white_berserk = false, black_berserk = false;
    // The stream.
    bool connected = false;
    int tries      = 0;  // since it broke, the next try counted
    std::chrono::steady_clock::time_point retry_at{};
    std::string error;   // why it broke last
    bool ended = false;  // it will not come back: the game is over, unknown to Lichess, or the token refused

    // The game is over: anything but created and started.
    [[nodiscard]] bool over() const { return !status.empty() && status != "created" && status != "started"; }
    [[nodiscard]] bool live() const { return speed != "correspondence" && days == 0; }
    // A side's clock now, its time running while it is to move, both moves of each side made.
    [[nodiscard]] int64_t clockNow(bool white, std::chrono::steady_clock::time_point now) const;
};

// A line of a game's stream applied to `state`; false for a keep-alive or what is not one of its events.
bool readBoardLine(std::string_view line, BoardState& state);

// The game as the board holds it: its tags (the players and their ratings, the event, the site, the time control) and
// its moves from its start; a move that is not legal ends it there.
[[nodiscard]] ReplayGame replayOf(const BoardState& state);
// "Rated rapid 10+5", "Casual correspondence · 3 days a move"
[[nodiscard]] std::string gameKind(const BoardState& state);

// The game's stream, held on a thread of its own: opened again after a break (1 s, 2 s, 3 s, then every 5 s), the
// whole game coming back each time (gameFull), the players' chat so far asked first; it stops once the game is over,
// or when Lichess does not know it or refuses the token. Through the Board API, or the Bot API for a bot's game.
class GameStream {
   public:
    GameStream(std::string token, std::string id, PlayApi api = PlayApi::Board);
    ~GameStream();
    GameStream(const GameStream&)            = delete;
    GameStream& operator=(const GameStream&) = delete;

    // The game, copied when it changed since `version`; false when it did not.
    bool state(BoardState& out, uint64_t& version) const;
    [[nodiscard]] const std::string& id() const { return id_; }
    [[nodiscard]] const std::string& token() const { return token_; }

   private:
    void run();
    template <typename F>
    void update(F f);

    std::string token_, id_;
    PlayApi api_ = PlayApi::Board;
    std::atomic<bool> stop_{false};
    mutable std::mutex mutex_;
    BoardState state_;
    std::thread thread_;
};

// ---------------------------------------------------------------------------
// What the profile keeps: play/lichess.json

struct OnlinePlaySettings {
    // The last seek: its clock, rated, the opponents' ratings around the user's (±; 0 anyone).
    int seek_minutes = 10, seek_increment = 5, seek_days = 0;
    bool seek_rated = true;
    int seek_range  = 0;
    // The last challenge: whom, its clock, rated, the user's colour (0 White, 1 Black, 2 at random).
    std::string opponent;
    int challenge_minutes = 5, challenge_increment = 3, challenge_days = 0;
    bool challenge_rated = false;
    int challenge_colour = 2;
    bool scope_declined  = false;  // "Not now" said to board:play
    std::string playing;           // the live game being played here when RookStudio closed: back at the next launch
    // The tournaments (#95): the part shown last (0 the games, 1 the tournaments); "Not now" said to tournament:write.
    int part                 = 0;
    bool tournament_declined = false;

    [[nodiscard]] ChallengeClock seekClock() const { return {seek_minutes, seek_increment, seek_days}; }
    [[nodiscard]] ChallengeClock challengeClock() const {
        return {challenge_minutes, challenge_increment, challenge_days};
    }
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OnlinePlaySettings, seek_minutes, seek_increment, seek_days, seek_rated,
                                                seek_range, opponent, challenge_minutes, challenge_increment,
                                                challenge_days, challenge_rated, challenge_colour, scope_declined,
                                                playing, part, tournament_declined)

[[nodiscard]] OnlinePlaySettings readOnlinePlay(const std::filesystem::path& folder);
bool writeOnlinePlay(const std::filesystem::path& folder, const OnlinePlaySettings& settings);

}  // namespace fcg::lichess
