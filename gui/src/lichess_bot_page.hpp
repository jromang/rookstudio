#pragma once

// A Lichess bot for one of the user's engines (#85, mockup M12, board 6), a module of its own out of App (#107;
// lichess_bot.hpp is its part of the network). On the Engines page, an engine's card gets "On Lichess, as a bot": the
// bot's account (a second Lichess account, new and game-less, linked with bot:play only, or by a token pasted), its
// checks before any request, the upgrade once its name is typed; its rules; online and offline. Online, its own event
// stream (one per token) brings the challenges, accepted or declined with Lichess's reason; its game is played by the
// engine, followed on the Board with its clocks, and brought into "Lichess · <bot>" at its end. It shows where what
// runs shows (the Home page and the Tournaments page), its engine's threads count in the core budget, a tournament
// comes first (R6), and closing RookStudio during its live game asks first. The user's own account and games stay
// apart: the bot's are not in the games in progress the fair play's lock reads; during the user's own live game, the
// bot's search is not shown.
//
// What it needs of the program around it, it asks of its context (BotContext, app_context.hpp): App's, or a test's.

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app_context.hpp"
#include "focus_words.hpp"
#include "lichess_bot.hpp"
#include "notice.hpp"

namespace fcg {

class LichessBotPage {
   public:
    explicit LichessBotPage(BotContext& app);
    ~LichessBotPage();
    LichessBotPage(const LichessBotPage&)            = delete;
    LichessBotPage& operator=(const LichessBotPage&) = delete;

    // Once a frame, whatever shows: an account linked or a token pasted, checked before it is kept; the upgrade; its
    // answers to the challenges; its finished games brought in; its event stream; its game, followed, then ended.
    void poll();
    // On an engine's card (the Engines page, both spaces): "On Lichess, as a bot", one line while there is no bot, then
    // its setup, its rules or its card online; `enter_below`: a part of the card below takes Enter this frame (the
    // Lichess analysis's button, #96), not Put online.
    void drawEnginePart(const EngineEntry& e, bool enter_below);
    // Over the Board's column, while the Board follows its game (onBoard()).
    void drawBoardCard();
    // Where what runs shows, while it is online or plays (running()): the Home page's "In progress" (`home`), the
    // Tournaments page's card.
    void drawRunning(bool home);
    // Closing RookStudio during its live game (live()): the question, opened when `open`; true once the user chose
    // Close anyway.
    bool drawQuitQuestion(bool open);
    // Its entries of the command palette (Ctrl+K): online or offline, its game on the Board, its card; while there is
    // no bot, the setup of the engine the Engines page shows.
    void addCommands(const PaletteAdd& add);
    // Its --focus words, tried where App's table has them (app_chess.cpp): engines-bot (the first engine's card, its
    // bot's setup started), bot-online (the bot put online, its account linked already). `word` is the word `words`
    // was made with.
    void addFocusWords(FocusWords& words, const std::string& word);

    // Its account, the second one (none while none is linked), read from the profile the first time it is asked: the
    // user's own accounts count it.
    [[nodiscard]] const lichess::Account* account();
    // Online or playing (shown where what runs shows); a game of its being played; a live one, which closing RookStudio
    // would lose on time; its engine's threads while it plays, in the core budget.
    [[nodiscard]] bool running() const;
    [[nodiscard]] bool playing() const;
    [[nodiscard]] bool live() const;
    [[nodiscard]] int threads() const;
    // The Board follows its game, ended or not (its card shows); the game it follows is being played (its moves are the
    // engine's alone); the Board leaves it for another game.
    [[nodiscard]] bool onBoard() const { return bot_shown_; }
    [[nodiscard]] bool shown() const;
    void leaveBoard() { bot_shown_ = false; }
    // Its game as its stream told it, for the Board's strips: the players' ratings; a side's clock, running (none: not
    // told).
    [[nodiscard]] const lichess::BoardState& game() const { return bot_state_.game; }
    [[nodiscard]] std::optional<std::string> stripClock(chess::Color side) const;

    // What went wrong online, as its card says it in place of its line (#146): its token refused, an error, with Link
    // the bot's account… (its own link, Lichess's page for the bot's account, never the user's); an answer to a
    // challenge Lichess did not take, without a button (an online action is never sent again blindly). None while
    // nothing failed. A fix of it carried out by the module.
    [[nodiscard]] std::optional<Notice> onlineNotice() const;
    void repair(const Fix& fix);

   private:
    // The card's setup, on the engine it was started from: the account's steps, then the confirmation.
    enum class BotStep { Card, Account, Confirm };
    struct BotLink {  // the bot's account linked through Lichess's page, then checked
        std::shared_ptr<std::atomic<bool>> cancel;
        std::shared_ptr<std::string> url;  // Lichess's page, as given to the browser
        std::shared_ptr<std::atomic<bool>> opened;
        std::future<lichess::BotTokenCheck> result;
    };
    struct BotAnswer {
        lichess::Challenge challenge;
        std::string reason;  // Lichess's key; empty: accepted
    };
    struct BotCall {
        std::string what;      // what it asks, as said if it fails
        std::string accepted;  // the challenge it accepts, when it does: free again if Lichess refuses
        std::future<lichess::Reply> reply;
    };
    struct BotArrival {
        std::string id;
        std::size_t index = 0;
        std::future<lichess::Arrival> arrival;
    };

    void loadBot();  // its account and settings, read once
    lichess::BotSettings& botSettings();
    void saveBotSettings();
    [[nodiscard]] std::filesystem::path botFolder() const { return app_.profileDir() / "bot"; }
    void botEvent(const lichess::GameEvent& e);
    void startBotLink();
    void checkBotToken(std::string token);  // pasted
    void keepBotAccount(const lichess::Account& account);
    void forgetBotAccount();
    void setBotOnline(bool online);
    void startBotGame(const std::string& id, bool white);
    void finishBotGame(const lichess::BotGame::State& st);
    void openBotGame();                   // its game on the Board
    void showBotGame();                   // the Board follows its state
    [[nodiscard]] bool botReady() const;  // an account that is a bot, its engine in the library
    // A tournament needs the cores the bot's engine would take (R6): the tournament first.
    [[nodiscard]] bool botCoresNeeded() const;
    // The user's own live game on Lichess: the bot's search is not shown (fair play, #48).
    [[nodiscard]] bool botSearchHidden() const { return app_.liveGameOn(); }
    [[nodiscard]] const EngineEntry* botEngine() const;
    [[nodiscard]] lichess::BotPlay botPlay() const;
    [[nodiscard]] std::string botOpponent() const;  // "Bot_2 (2410)"
    void drawBotIntro(const EngineEntry& e);
    void drawBotSteps();
    void drawBotConfirm();
    void drawBotRefusal();
    void drawBotRules(const EngineEntry& e, bool enter_below);
    void drawBotOnline();

    BotContext& app_;
    std::optional<lichess::Account> bot_account_;
    bool bot_loaded_ = false;
    std::optional<lichess::BotSettings> bot_settings_;
    // The account as last checked, the check under way, the refusal said.
    std::optional<lichess::BotAccountFacts> bot_facts_;
    std::future<lichess::BotTokenCheck> bot_check_;  // a token pasted, checked
    lichess::Failure bot_error_;                     // why the last link, check or upgrade gave nothing
    bool bot_unanswered_ = false;                    // Lichess did not answer: nothing changed, to try again
    // An account refused before it was kept (the user's own, closed, a token that may not play), and why.
    std::optional<lichess::BotAccountFacts> bot_candidate_;
    lichess::BotVerdict bot_verdict_ = lichess::BotVerdict::Upgradable;
    BotStep bot_step_                = BotStep::Card;
    std::string bot_setup_engine_;
    std::unique_ptr<BotLink> bot_link_;
    std::array<char, 600> bot_token_text_{};  // a token pasted
    std::array<char, 40> bot_confirm_{};      // the name typed to confirm the upgrade
    bool bot_confirm_focus_ = false;
    std::future<lichess::Reply> bot_upgrade_;
    std::future<lichess::Failure> bot_unlink_;
    bool bot_unlink_asked_ = false;  // "Unlink…" armed: a second click
    // Online: its event stream, its game, the challenges answered.
    std::unique_ptr<lichess::EventListener> bot_events_;
    bool bot_online_  = false;  // accepting the challenges its rules allow
    bool bot_closing_ = false;  // "Put offline": no new challenge, the game being played goes on to its end
    std::string bot_online_error_;
    bool bot_token_refused_ = false;  // that error is its token refused: its account to link again
    std::unique_ptr<lichess::BotGame> bot_game_;
    lichess::BotGame::State bot_state_;
    uint64_t bot_version_ = 0;
    std::vector<std::pair<std::string, bool>> bot_waiting_;  // games started while one plays: id, the bot's side
    std::string bot_accepted_;  // a challenge accepted, its game not started yet: one at a time
    std::chrono::steady_clock::time_point bot_accepted_at_{};
    std::vector<BotAnswer> bot_answered_;  // the latest first
    std::vector<BotCall> bot_calls_;       // its requests under way
    bool bot_shown_       = false;         // the Board follows its game
    bool bot_flipped_     = false;
    bool bot_board_dirty_ = false;
    std::string bot_shown_id_;              // the game the Board follows, ended or not
    std::vector<BotArrival> bot_arrivals_;  // its games that ended, brought into its database
};

}  // namespace fcg
