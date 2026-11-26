#pragma once

// A Lichess bot for one of the user's engines (#85, mockup M12, board 6), without ImGui. The bot's account is a second
// Lichess account, new and game-less (RookStudio cannot create it), linked with bot:play only and kept apart from the
// user's own (its token in lichess-bot.json, readable by its owner alone; its own event stream, one per token). Before
// anything that changes the account, its checks: no game played, not the user's own account, a bot already or not;
// the upgrade to a bot is irreversible. Online, the bot accepts the challenges its rules allow and declines the others
// with the reason Lichess shows the challenger; its games are played by the engine through Lichess's Bot API (the
// stream and requests of #84's Board API under /api/bot/, lichess_board.hpp), the engine driven as #83's games drive
// theirs (board_game.hpp's EnginePlayer). The view is its module's, lichess_bot_page.hpp (#107).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <json.hpp>

#include "lichess_account.hpp"
#include "lichess_api.hpp"
#include "lichess_board.hpp"
#include "lichess_playing.hpp"
#include "model.hpp"

namespace fcg::lichess {

// Playing as a bot through the Bot API, on a bot account: the bot's token asks for it alone.
inline constexpr const char* SCOPE_BOT_PLAY = "bot:play";

// ---------------------------------------------------------------------------
// The bot's account

// What GET /api/account says of the account a token is for.
struct BotAccountFacts {
    std::string id, username;
    std::string title;           // "BOT" once upgraded
    int64_t created_ms = 0;      // createdAt, milliseconds since the epoch; 0 when not said
    int64_t games      = 0;      // count.all: the games it played
    bool disabled      = false;  // closed
    bool tos_violation = false;  // marked for a violation of Lichess's terms of service
    [[nodiscard]] bool bot() const { return title == "BOT"; }
    bool operator==(const BotAccountFacts&) const = default;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BotAccountFacts, id, username, title, created_ms, games, disabled,
                                                tos_violation)
[[nodiscard]] std::optional<BotAccountFacts> readBotAccount(const nlohmann::json& j);
[[nodiscard]] Answer<BotAccountFacts> botAccount(const std::string& token, const std::atomic<bool>* cancel = nullptr);

// A token checked before it is kept (one the link made, or one pasted): what it may do (POST /api/token/test) and its
// account's facts (GET /api/account); why not (Failure): a token Lichess no longer takes is Unauthorized.
struct BotTokenCheck : Failure {
    std::optional<Account> account;  // its name, the token, its scopes, its expiry
    std::optional<BotAccountFacts> facts;
    bool linked = false;  // made by RookStudio's link: revoked when it is not kept
};
[[nodiscard]] BotTokenCheck checkBotToken(const std::string& token, const std::atomic<bool>* cancel = nullptr);

// What the checks make of an account before anything is asked that changes it.
enum class BotVerdict {
    Upgradable,  // no game played, not a bot yet: the upgrade may be asked
    Ready,       // a bot already: it plays as it is
    OwnAccount,  // the user's own linked account: never
    Played,      // it played games: Lichess upgrades only an account that never played
    Closed,      // closed, or marked for a violation of Lichess's terms
    NoScope,     // its token may not play as a bot (bot:play)
};
// `own_account`: the user's own linked account's name, empty when none is linked; `may_play`: the token has bot:play.
[[nodiscard]] BotVerdict botVerdict(const BotAccountFacts& facts, std::string_view own_account, bool may_play);

// The upgrade, irreversible: POST /api/bot/account/upgrade with the bot's token (Lichess's words when it refuses).
[[nodiscard]] Reply upgradeToBot(const std::string& token, const std::atomic<bool>* cancel = nullptr);

// ---------------------------------------------------------------------------
// The challenges it accepts

// Its rules, on its card (M12, board 9, point 11: casual, blitz and rapid, one game at a time at first, widened
// later). UltraBullet never: Lichess does not let bots play it.
struct BotRules {
    bool bullet = false, blitz = true, rapid = true, classical = false, correspondence = false;
    bool rated  = false;  // rated challenges too; casual ones always (Lichess asks bots to play casual while testing)
    bool humans = true, bots = true;
    int rating_min = 0, rating_max = 0;              // the challenger's rating in the challenge's speed; 0: no bound
    bool chess960                          = false;  // Chess960 too, when the engine knows it (UCI_Chess960)
    bool operator==(const BotRules&) const = default;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BotRules, bullet, blitz, rapid, classical, correspondence, rated,
                                                humans, bots, rating_min, rating_max, chess960)

// Where the bot is when a challenge comes.
struct BotSituation {
    bool busy            = false;  // a game being played, or one accepted not started yet: one at a time
    bool closing         = false;  // going offline: no new challenge
    bool cores_needed    = false;  // a tournament needs the cores (R6): the tournament first
    bool engine_chess960 = false;  // the engine has UCI_Chess960
    std::string own_account;       // the user's own linked account's name: no rated game against it
};

// The speeds in order, the fastest first; -1 for none of them.
[[nodiscard]] int speedRank(std::string_view speed);
// The reason Lichess's decline is given ("tooFast", "casual", "later"...) for a challenge the bot does not accept,
// empty when it accepts it: what the challenge asks first (its variant, its speed, rated, who sends it), then whether
// the bot can play now.
[[nodiscard]] std::string declineReason(const Challenge& c, const BotRules& rules, const BotSituation& now);

// ---------------------------------------------------------------------------
// Its games

// What the bot plays with: its engine (the library's entry, its options), an opening book, the tables, its words.
struct BotPlay {
    EngineEntry engine;
    std::string book;      // a Polyglot book's file, played while it has a move; empty: none
    std::string syzygy;    // SyzygyPath, for an engine that has it; empty: its own setting
    std::string greeting;  // said in the players' chat at the start; empty: nothing
    std::string thanks;    // and at the end
};

// What the network may take of the bot's clock: a tenth of it, from 200 ms to a second.
[[nodiscard]] int64_t clockMargin(int64_t clock_ms);
// The engine's go for the bot's move in `s` at `now`: Lichess's clocks, the bot's less clockMargin() (the opponent's
// as they are); before both sides moved, a short move time within the time Lichess gives to make the first move; by
// correspondence, 15 seconds a move.
[[nodiscard]] std::string botGo(const BoardState& s, bool bot_white, std::chrono::steady_clock::time_point now);
// How long the engine is waited for before its search is stopped: what botGo() gave it, and a little.
[[nodiscard]] std::chrono::milliseconds botBudget(const BoardState& s, bool bot_white,
                                                  std::chrono::steady_clock::time_point now);

// A game of the bot's, played on a thread of its own: its stream heard (the Bot API's, opened again after a cut, as
// #84's games are), the book's move or the engine's at the bot's turn, sent to Lichess (again after a cut, while it
// is still the bot's turn); the opponent's draw offer taken when the engine stands clearly worse or level late in the
// game (engineAcceptsDraw, as #83's engine does), a takeback declined, the victory claimed once the opponent left long
// enough; its words in the chat at the start and the end. An engine that cannot play resigns the game (aborts it
// before both sides moved).
class BotGame {
   public:
    struct State {
        uint64_t version = 0;
        BoardState game;       // as its stream told it
        bool white    = true;  // the bot's side
        bool thinking = false;
        std::chrono::steady_clock::time_point think_start{};
        std::string info;        // the engine's last info line with a score
        int info_ply   = -1;     // the ply its search was for (the moves then)
        bool from_book = false;  // its last move came from the book
        int book_moves = 0;
        std::string error;  // why the engine could not play, or a move was refused
        bool done = false;  // the game is over (or Lichess does not know it): the thread ended
    };
    // `bot`: the bot's name, to find its side and its own lines in the chat; `white`, its side until the game says.
    BotGame(std::string token, std::string id, std::string bot, bool white, BotPlay play);
    ~BotGame();
    BotGame(const BotGame&)            = delete;
    BotGame& operator=(const BotGame&) = delete;

    // The state, copied when it changed since `version`; false when it did not.
    bool state(State& out, uint64_t& version) const;
    [[nodiscard]] const std::string& id() const { return id_; }
    [[nodiscard]] const std::string& token() const { return token_; }
    // The threads its engine takes while it plays, counted in the core budget.
    [[nodiscard]] int threads() const { return threads_; }
    // The user ends it: resigned, or aborted before both sides moved.
    void resign() { resign_ = true; }

   private:
    void run();
    template <typename F>
    void update(F f);

    std::string token_, id_, bot_;
    BotPlay play_;
    int threads_ = 1;
    std::atomic<bool> stop_{false};
    std::atomic<bool> resign_{false};
    mutable std::mutex mutex_;
    State state_;
    std::thread thread_;
};

// ---------------------------------------------------------------------------
// What the profile keeps: bot/bot.json

// A game of the bot's that ended, for its card.
struct BotRecord {
    std::string id;
    std::string opponent;
    int rating        = 0;
    bool opponent_bot = false;
    std::string speed;  // Lichess's
    std::string clock;  // "3+2", "3 days"
    bool rated = false;
    bool white = true;   // the bot's side
    std::string result;  // "1-0", "0-1", "1/2-1/2", "*" for an aborted game
    int plies                               = 0;
    int64_t at                              = 0;  // when it ended, seconds since the epoch
    bool operator==(const BotRecord&) const = default;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BotRecord, id, opponent, rating, opponent_bot, speed, clock, rated,
                                                white, result, plies, at)

// 1 the bot won, 0 drew, -1 lost; none for an aborted game.
[[nodiscard]] std::optional<int> botScore(const BotRecord& game);

struct BotSettings {
    std::string engine;     // the library engine it plays, by name
    BotAccountFacts facts;  // its account as last checked (no secret: the token is in lichess-bot.json)
    BotRules rules;
    std::string book;    // a Polyglot book of the library, by its file; empty: none
    bool tables = true;  // the tournaments' tables (SyzygyPath), for an engine that has the option
    std::string greeting, thanks;
    bool words_set      = false;  // the words chosen once (the defaults are the interface's language's)
    int64_t upgraded_at = 0;      // when RookStudio made it a bot, seconds since the epoch; 0: before, or elsewhere
    // Today's games and challenges declined (the day as YYYY-MM-DD, local), the last games.
    std::string day;
    int declined = 0;
    std::vector<BotRecord> games;  // the latest first, 20 at most
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BotSettings, engine, facts, rules, book, tables, greeting, thanks,
                                                words_set, upgraded_at, day, declined, games)

[[nodiscard]] BotSettings readBotSettings(const std::filesystem::path& folder);
bool writeBotSettings(const std::filesystem::path& folder, const BotSettings& settings);
// A game ended added (the day turned: today's counts start again); a challenge declined counted.
void addBotRecord(BotSettings& settings, BotRecord record, const std::string& today);
void countDeclined(BotSettings& settings, const std::string& today);

// Today's games: won, drawn, lost (aborted ones left out).
struct BotDay {
    int games = 0, won = 0, drawn = 0, lost = 0;
};
[[nodiscard]] BotDay botDay(const BotSettings& settings, const std::string& today);

}  // namespace fcg::lichess
