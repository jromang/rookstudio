// The Lichess bot's module (#107), built and driven without App: a context of the test's own answers what it asks of
// the program, Lichess is a server on this machine, the bot's engine is the mock engine, and the module's cards are
// drawn in an ImGui context of the test's, a key pressed as the user presses it. Its --focus words and palette
// commands; the bot put online by Enter on its card, a challenge declined with Lichess's reason and one accepted, its
// game played by the mock engine to its end, followed on the Board and kept in its database, the question at closing
// asked during it; a token Lichess refuses: the bot offline at once.
#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <chess.hpp>
#include <json.hpp>
#include <study/game.hpp>

#include "app_context.hpp"
#include "board_view.hpp"
#include "databases.hpp"
#include "focus_words.hpp"
#include "guide.hpp"
#include "https.hpp"
#include "jobs.hpp"
#include "lichess_account.hpp"
#include "lichess_bot.hpp"
#include "lichess_bot_page.hpp"
#include "mock_engines.hpp"
#include "model.hpp"
#include "notice.hpp"
#include "scripted_server.hpp"
#include "test_support.hpp"

using namespace fcg;
using namespace fcg::test;

namespace {

// What the module asks of App, answered by the test: the profile and its databases of a scratch folder, the mock
// engine alone in the library, an Engines page and a Board that only remember what they were told, the user's own
// account and their live game set by the test, the tokens given up and the window raised counted.
class FakeApp final : public BotContext {
   public:
    explicit FakeApp(const std::filesystem::path& profile) : profile_(profile), shelf_(jobs_, profile) {
        EngineEntry mock;
        mock.name     = "Mock";
        mock.cmd      = randomMover();
        mock.protocol = "uci";
        library.push_back(std::move(mock));
    }

    // The program.
    int frame() const override { return frame_; }
    double seconds() const override { return 100; }
    bool shortcutPressed(int) const override { return false; }
    bool scripted() const override { return false; }
    std::filesystem::path profileDir() const override { return profile_; }
    DatabaseShelf& shelf() override { return shelf_; }
    void toast(const std::string&) override {}
    JobGroup& jobs() override { return jobs_; }

    // The engines.
    const std::vector<EngineEntry>& engines() const override { return library; }
    const std::vector<BookEntry>& books() const override { return books_; }
    std::string tournamentTables() const override { return {}; }
    bool tournamentPlaysHere() const override { return false; }
    int freeCores() const override { return 8; }
    const EngineEntry* engineShown() const override {
        return engines_page && card >= 0 ? &library[static_cast<std::size_t>(card)] : nullptr;
    }
    void showEngineCard(std::optional<int> engine, bool alone) override {
        engines_page = true;
        if (engine) card = *engine;
        if (alone) ++unmarked;
    }

    // The Board taken by its game.
    void takeBoard(const std::string& title) override {
        ++taken;
        board_title = title;
        board_game  = ReplayGame{};
    }
    void updateBoard(ReplayGame game, bool, const std::string& title, std::optional<bool> flip) override {
        board_game = std::move(game);
        if (!title.empty()) board_title = title;
        if (flip) board_flip = *flip;
    }
    void showLiveBoard() override { ++board_page; }
    std::string boardSan(std::string san) const override { return san; }
    const PieceAtlas& pieces() const override { return atlas_; }

    // The user's own account and their live game, the tokens given up, the window.
    const lichess::Account* lichessAccount() const override { return own ? &*own : nullptr; }
    bool liveGameOn() const override { return live_game; }
    void revokeToken(std::string token) override { revoked.push_back(std::move(token)); }
    void raiseWindow() override { ++raised; }

    // What the module was told, and what the test sets.
    int frame_ = 0;
    std::vector<EngineEntry> library;
    bool engines_page = false;
    int card = -1, unmarked = 0;
    int taken = 0, board_page = 0, raised = 0;
    std::string board_title;
    ReplayGame board_game;
    std::optional<bool> board_flip;
    std::optional<lichess::Account> own;
    bool live_game = false;
    std::vector<std::string> revoked;

   private:
    std::filesystem::path profile_;
    JobGroup jobs_;
    DatabaseShelf shelf_;  // after its jobs: gone before them
    std::vector<BookEntry> books_;
    PieceAtlas atlas_;  // without textures: the boards drawn without their pieces
};

// The bot's account, linked already, and its settings: a bot of Lichess's, the mock engine its engine.
void linkBot(const std::filesystem::path& profile, const std::string& token) {
    lichess::Account account;
    account.username = "RS_SampleBot";
    account.token    = token;
    account.scopes   = {lichess::SCOPE_BOT_PLAY};
    lichess::saveAccount(profile, account, lichess::BOT_ACCOUNT_FILE);
    lichess::BotSettings settings;
    settings.engine         = "Mock";
    settings.facts.id       = "rs_samplebot";
    settings.facts.username = "RS_SampleBot";
    settings.facts.title    = "BOT";
    REQUIRE(lichess::writeBotSettings(profile / "bot", settings));
}

// An ImGui context of the test's own, without a window: a frame draws a card of the module, a key pressed before it
// (and let go after it, in a frame of its own: Dear ImGui takes one change of a key a frame).
class Frames {
   public:
    Frames() : context_(ImGui::CreateContext()) {
        auto& io = ImGui::GetIO();
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280, 900);
        io.DeltaTime   = 1.0f / 60.0f;
    }
    ~Frames() { ImGui::DestroyContext(context_); }
    Frames(const Frames&)            = delete;
    Frames& operator=(const Frames&) = delete;

    void draw(FakeApp& app, const std::function<void()>& page, ImGuiKey key = ImGuiKey_None) {
        if (key != ImGuiKey_None) ImGui::GetIO().AddKeyEvent(key, true);
        frame(app, page);
        if (key != ImGuiKey_None) {
            ImGui::GetIO().AddKeyEvent(key, false);
            frame(app, page);
        }
    }

   private:
    static void frame(FakeApp& app, const std::function<void()>& page) {
        ++app.frame_;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("page", nullptr, ImGuiWindowFlags_NoDecoration);
        page();
        ImGui::End();
        ImGui::Render();
    }
    ImGuiContext* context_;
};

std::string reply(int status, const std::string& body, const char* type = "application/json") {
    return fmt::format("HTTP/1.1 {} X\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", status,
                       type, body.size(), body);
}
const char* const STREAM_HEAD =
    "HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
std::string chunk(const std::string& data) { return fmt::format("{:x}\r\n{}\r\n", data.size(), data); }
const char* const LAST_CHUNK = "0\r\n\r\n";
// An event of a stream, on one line (NDJSON).
std::string event(const char* json) { return chunk(nlohmann::json::parse(json).dump() + "\n"); }

// Two challenges to the bot, as its event stream sends them: a bullet one (its rules decline it, too fast) and a casual
// blitz one, which they take.
const char* const BULLET_CHALLENGE = R"({"type": "challenge", "challenge": {"id": "cHal0001", "status": "created",
    "challenger": {"name": "Player_G", "id": "player_g", "rating": 1702}, "destUser": {"name": "RS_SampleBot",
    "id": "rs_samplebot", "title": "BOT"}, "variant": {"key": "standard"}, "rated": false, "speed": "bullet",
    "timeControl": {"type": "clock", "limit": 60, "increment": 0}, "color": "random", "direction": "in"},
    "compat": {"bot": true, "board": false}})";
const char* const BLITZ_CHALLENGE  = R"({"type": "challenge", "challenge": {"id": "cHal0002", "status": "created",
    "challenger": {"name": "Partner_3", "id": "partner_3", "rating": 1836}, "destUser": {"name": "RS_SampleBot",
    "id": "rs_samplebot", "title": "BOT"}, "variant": {"key": "standard"}, "rated": false, "speed": "blitz",
    "timeControl": {"type": "clock", "limit": 180, "increment": 2}, "color": "black", "finalColor": "black",
    "direction": "in"}, "compat": {"bot": true, "board": true}})";
const char* const GAME_START       = R"({"type": "gameStart", "game": {"fullId": "gBot0001wxyz", "gameId": "gBot0001",
    "fen": "startpos", "color": "white", "source": "friend", "status": {"id": 20, "name": "started"},
    "variant": {"key": "standard"}, "speed": "blitz", "rated": false, "opponent": {"id": "partner_3",
    "username": "Partner_3", "rating": 1836}, "isMyTurn": true, "secondsLeft": 180,
    "compat": {"bot": true, "board": true}}})";

// Lichess on this machine, for the bot's token: its event stream (the two challenges, then the game's start once the
// blitz one is accepted; refused for a revoked token), the challenges accepted and declined, and its game as Lichess
// would hold it: the bot has White against Partner_3, who answers each move with its first legal one (the first once
// the test lets it, when it holds it) and resigns after the bot's third; its chat; the game's PGN at its end. Every
// name invented.
class FakeLichess {
   public:
    FakeLichess() : server_(0, [this](const std::string& head, net::TcpConnection& c) { serve(head, c); }) {
        const auto base    = fmt::format("http://127.0.0.1:{}", server_.port());
        lichess::servers() = lichess::Servers{base, base + "/tb", base + "/explorer", base + "/engine"};
    }
    ~FakeLichess() {
        {
            std::lock_guard lock(mutex_);
            quit_ = true;
        }
        cv_.notify_all();
        lichess::servers() = lichess::Servers{};
    }
    FakeLichess(const FakeLichess&)            = delete;
    FakeLichess& operator=(const FakeLichess&) = delete;

    struct Seen {
        std::string method, path, authorization, body;
    };

    [[nodiscard]] bool ok() const { return server_.ok(); }
    [[nodiscard]] std::vector<Seen> seen(const std::string& path) {
        std::lock_guard lock(mutex_);
        std::vector<Seen> out;
        for (const auto& s : seen_)
            if (s.path == path) out.push_back(s);
        return out;
    }
    [[nodiscard]] std::vector<std::string> moves() {
        std::lock_guard lock(mutex_);
        return moves_;
    }
    [[nodiscard]] std::vector<std::string> chat() {
        std::lock_guard lock(mutex_);
        return chat_;
    }
    [[nodiscard]] int refused() {
        std::lock_guard lock(mutex_);
        return refused_;
    }
    [[nodiscard]] int eventStreams() const { return event_streams_; }
    // Partner_3 thinks over the bot's first move until answer().
    void holdAnswer() {
        std::lock_guard lock(mutex_);
        hold_ = true;
    }
    void answer() {
        {
            std::lock_guard lock(mutex_);
            hold_ = false;
            if (waiting_) {
                waiting_ = false;
                answerMove();
                lines_.push_back(state());
            }
        }
        cv_.notify_all();
    }

   private:
    std::string state() const {
        std::string text;
        for (const auto& m : moves_) text += (text.empty() ? "" : " ") + m;
        nlohmann::json j{{"type", "gameState"},
                         {"moves", text},
                         {"wtime", 178000},
                         {"btime", 179000},
                         {"winc", 2000},
                         {"binc", 2000},
                         {"status", ended_ ? "resign" : "started"}};
        if (ended_) j["winner"] = "white";
        return j.dump() + "\n";
    }
    std::string full() const {
        auto j     = nlohmann::json::parse(R"({"type": "gameFull", "id": "gBot0001", "variant": {"key": "standard"},
            "speed": "blitz", "perf": {"name": "Blitz"}, "rated": false,
            "white": {"id": "rs_samplebot", "name": "RS_SampleBot", "title": "BOT", "rating": 1500},
            "black": {"id": "partner_3", "name": "Partner_3", "rating": 1836},
            "initialFen": "startpos", "clock": {"initial": 180000, "increment": 2000}})");
        j["state"] = nlohmann::json::parse(state());
        return j.dump() + "\n";
    }
    // Partner_3's move, its first legal one (the mutex held).
    void answerMove() {
        chess::Movelist answers;
        chess::movegen::legalmoves(answers, board_);
        board_.makeMove(answers[0]);
        moves_.push_back(chess::uci::moveToUci(answers[0]));
    }
    // The bot's move taken, then Partner_3's answer (held, when the test holds it), or its resignation after the bot's
    // third move.
    bool play(const std::string& uci) {
        {
            std::lock_guard lock(mutex_);
            chess::Movelist legal;
            chess::movegen::legalmoves(legal, board_);
            const auto m = chess::uci::uciToMove(board_, uci);
            if (ended_ || waiting_ || board_.sideToMove() != chess::Color::WHITE ||
                std::find(legal.begin(), legal.end(), m) == legal.end())
                return false;
            board_.makeMove(m);
            moves_.push_back(uci);
            if ((moves_.size() + 1) / 2 >= 3)
                ended_ = true;
            else if (hold_)
                waiting_ = true;
            else
                answerMove();
            lines_.push_back(state());
        }
        cv_.notify_all();
        return true;
    }
    // The game's PGN, as Lichess exports it.
    std::string pgn() {
        std::lock_guard lock(mutex_);
        chess::Board board;
        std::string text;
        for (std::size_t i = 0; i < moves_.size(); ++i) {
            const auto m = chess::uci::uciToMove(board, moves_[i]);
            if (i % 2 == 0) text += fmt::format("{}. ", i / 2 + 1);
            text += chess::uci::moveToSan(board, m) + " ";
            board.makeMove(m);
        }
        return fmt::format(
            "[Event \"Casual blitz game\"]\n[Site \"https://lichess.org/gBot0001\"]\n[White \"RS_SampleBot\"]\n"
            "[Black \"Partner_3\"]\n[Result \"1-0\"]\n[WhiteTitle \"BOT\"]\n[TimeControl \"180+2\"]\n\n{}1-0\n",
            text);
    }
    void serve(const std::string& head, net::TcpConnection& c) {
        Seen s;
        s.method        = head.substr(0, head.find(' '));
        s.body          = requestBody(head, c);
        s.path          = requestPath(head);
        s.authorization = httpHeader(head, "Authorization").value_or("");
        {
            std::lock_guard lock(mutex_);
            seen_.push_back(s);
        }
        const std::string game = "/api/bot/game/gBot0001";
        if (s.path == "/api/stream/event") {
            if (s.authorization != "Bearer tok-bot") {
                c.sendAll(reply(401, R"({"error": "No such token"})"));
                return;
            }
            ++event_streams_;
            c.sendAll(std::string(STREAM_HEAD) + event(BULLET_CHALLENGE) + event(BLITZ_CHALLENGE));
            bool started = false;
            while (true) {
                bool start = false;
                {
                    std::unique_lock lock(mutex_);
                    cv_.wait_for(lock, std::chrono::milliseconds(200),
                                 [&] { return quit_ || (accepted_ && !started); });
                    if (quit_) break;
                    start = accepted_ && !started;
                }
                if (start) {
                    started = true;
                    if (!c.sendAll(event(GAME_START))) break;
                }
                if (!c.sendAll(chunk("\n"), 500)) break;  // the bot went offline
            }
            --event_streams_;
        } else if (s.path == "/api/challenge/cHal0002/accept" || s.path == "/api/challenge/cHal0001/decline") {
            if (s.path.find("accept") != std::string::npos) {
                std::lock_guard lock(mutex_);
                accepted_ = true;
            }
            cv_.notify_all();
            c.sendAll(reply(200, R"({"ok": true})"));
        } else if (s.path == game + "/chat" && s.method == "GET") {
            c.sendAll(reply(200, "[]"));
        } else if (s.path == game + "/chat") {
            {
                std::lock_guard lock(mutex_);
                chat_.push_back(s.body.substr(s.body.find("text=") + 5));
            }
            c.sendAll(reply(200, R"({"ok": true})"));
        } else if (s.path.rfind(game + "/move/", 0) == 0) {
            const bool ok = play(s.path.substr(game.size() + 6));
            if (!ok) {
                std::lock_guard lock(mutex_);
                ++refused_;
            }
            c.sendAll(ok ? reply(200, R"({"ok": true})") : reply(400, R"({"error": "Not your turn"})"));
        } else if (s.path == "/api/bot/game/stream/gBot0001") {
            std::unique_lock lock(mutex_);
            c.sendAll(std::string(STREAM_HEAD) + chunk(full()));
            std::size_t sent = lines_.size();
            while (!quit_) {
                cv_.wait_for(lock, std::chrono::milliseconds(50), [&] { return lines_.size() > sent || quit_; });
                while (sent < lines_.size()) c.sendAll(chunk(lines_[sent++]));
                if (ended_) {
                    c.sendAll(LAST_CHUNK);
                    return;
                }
            }
        } else if (s.path.rfind("/game/export/gBot0001", 0) == 0) {
            c.sendAll(reply(200, pgn(), "application/x-chess-pgn"));
        } else {
            c.sendAll(reply(404, R"({"error": "Not found"})"));
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<Seen> seen_;
    chess::Board board_;
    std::vector<std::string> moves_, lines_, chat_;
    int refused_   = 0;
    bool accepted_ = false, ended_ = false, quit_ = false, hold_ = false, waiting_ = false;
    std::atomic<int> event_streams_{0};
    ScriptedServer server_;  // last: stopped first, its threads using what is above
};

struct Entry {
    std::string label, where;
    std::function<void()> run;
};
std::vector<Entry> commands(LichessBotPage& bot) {
    std::vector<Entry> out;
    bot.addCommands([&](std::string label, std::string where, std::function<void()> run) {
        out.push_back({std::move(label), std::move(where), std::move(run)});
    });
    return out;
}
std::vector<std::string> labels(const std::vector<Entry>& entries) {
    std::vector<std::string> out;
    for (const auto& e : entries) out.push_back(e.label);
    return out;
}
const Entry* entry(const std::vector<Entry>& entries, const std::string& label) {
    for (const auto& e : entries)
        if (e.label == label) return &e;
    return nullptr;
}
bool applied(LichessBotPage& bot, const std::string& word) {
    FocusWords words(word);
    bot.addFocusWords(words, word);
    return words.applied();
}
// The index of the profile's database named `name`, if there is one.
std::optional<std::size_t> databaseNamed(DatabaseShelf& shelf, const std::string& name) {
    for (std::size_t i = 0; i < shelf.entries().size(); ++i)
        if (shelf.entries()[i].name == name) return i;
    return std::nullopt;
}

}  // namespace

TEST_CASE("Lichess bot's module without App: its --focus words and its palette commands") {
    const ScratchDir profile("bot-module");
    FakeApp app(profile);
    {
        LichessBotPage bot(app);
        // No bot yet: its words take the Engines page, the first engine's card; its setup offered for the engine shown.
        CHECK(bot.account() == nullptr);
        CHECK(commands(bot).empty());  // the Engines page does not show
        CHECK(applied(bot, "engines-bot"));
        CHECK(app.engines_page);
        CHECK(app.card == 0);
        CHECK(applied(bot, "bot-online"));  // its account not linked: nothing more
        CHECK_FALSE(bot.running());
        for (const auto* other : {"engines", "engines-card", "bot", "bot-online=x", "engines-bot2", "lichess-bot",
                                  "lichess-tv", "play-lichess", "Engines-bot"})
            CHECK_FALSE(applied(bot, other));
        const auto listed = commands(bot);
        REQUIRE(listed.size() == 1);
        CHECK(listed[0].label == "Make Mock a Lichess bot…");
        CHECK(listed[0].where == "Engines");
        listed[0].run();
        CHECK(lichess::readBotSettings(profile / "bot").engine == "Mock");  // the engine its setup started from
    }

    // Its account linked, a bot: put online or offline from the palette, its card a command away.
    linkBot(profile, "tok-bot");
    LichessBotPage bot(app);
    REQUIRE(bot.account() != nullptr);
    CHECK(bot.account()->username == "RS_SampleBot");  // the user's own accounts count it
    const auto listed = commands(bot);
    CHECK(labels(listed) == std::vector<std::string>{"Put the Lichess bot online", "The Lichess bot's card"});
    CHECK(listed[0].where == "RS_SampleBot");
    app.card = -1;
    entry(listed, "The Lichess bot's card")->run();
    CHECK(app.card == 0);
    CHECK(app.unmarked == 1);  // the engines marked let go: its card shows
}

TEST_CASE("Lichess bot's module without App: online by Enter, a challenge declined, one played to its end and kept") {
    FakeLichess lichess;
    REQUIRE(lichess.ok());
    const ScratchDir profile("bot-module");
    linkBot(profile, "tok-bot");
    FakeApp app(profile);
    app.own.emplace();
    app.own->username = "RS_Sample";  // the user's own account, apart
    app.own->token    = "tok-user";
    lichess.holdAnswer();  // its game waits for Partner_3 after the bot's first move
    Frames frames;
    {
        LichessBotPage bot(app);
        bot.poll();
        // Enter on its card puts it online: its own event stream opened with its token.
        frames.draw(app, [&] { bot.drawEnginePart(app.library[0], false); }, ImGuiKey_Enter);
        CHECK(bot.running());
        CHECK(labels(commands(bot)) ==
              std::vector<std::string>{"Put the Lichess bot offline", "The Lichess bot's card"});
        REQUIRE(waitUntil([&] { return lichess.eventStreams() == 1; }));
        CHECK(lichess.seen("/api/stream/event").at(0).authorization == "Bearer tok-bot");

        // The bullet challenge declined with Lichess's reason, the blitz one accepted; its game starts.
        REQUIRE(waitUntil([&] {
            bot.poll();
            return bot.playing();
        }));
        const auto declined = lichess.seen("/api/challenge/cHal0001/decline");
        REQUIRE(declined.size() == 1);
        CHECK(declined[0].body == "reason=tooFast");
        CHECK(declined[0].authorization == "Bearer tok-bot");
        CHECK(lichess.seen("/api/challenge/cHal0002/accept").size() == 1);
        CHECK(bot.threads() == 1);  // its engine's, in the core budget
        frames.draw(app, [&] {
            bot.drawEnginePart(app.library[0], false);
            bot.drawRunning(true);
        });

        // Its game on the Board, from the palette: the Board taken, then following the game, the bot's side below.
        auto listed = commands(bot);
        CHECK(labels(listed) == std::vector<std::string>{"Put the Lichess bot offline",
                                                         "The Lichess bot's game on the Board",
                                                         "The Lichess bot's card"});
        entry(listed, "The Lichess bot's game on the Board")->run();
        CHECK(app.taken == 1);
        CHECK(app.board_page == 1);
        CHECK(bot.onBoard());
        REQUIRE(waitUntil([&] {
            bot.poll();
            return bot.game().full && bot.game().moves.size() == 1 && app.board_game.moves.size() == 1;
        }));
        CHECK(app.board_title == "RS_SampleBot – Partner_3");
        CHECK(app.board_flip == std::optional<bool>(false));
        CHECK(bot.game().black.rating == 1836);
        CHECK(bot.shown());
        CHECK(bot.stripClock(chess::Color::WHITE).has_value());  // Lichess's clocks on the Board's strips
        frames.draw(app, [&] { bot.drawBoardCard(); });
        // Closing RookStudio now would lose it on time: asked first; Esc stays.
        REQUIRE(bot.live());
        frames.draw(app, [&] {
            CHECK_FALSE(bot.drawQuitQuestion(true));
            CHECK(ImGui::IsPopupOpen("Close RookStudio?###quit_bot"));
        });
        frames.draw(
            app,
            [&] {
                CHECK_FALSE(bot.drawQuitQuestion(false));
                CHECK_FALSE(ImGui::IsPopupOpen("Close RookStudio?###quit_bot"));
            },
            ImGuiKey_Escape);
        // The user's own live game on Lichess hides its search; its game goes on.
        app.live_game = true;
        frames.draw(app, [&] { bot.drawBoardCard(); });
        app.live_game = false;

        // Played to its end by the mock engine: Partner_3 answers, and resigns after the bot's third move.
        lichess.answer();
        REQUIRE(waitUntil(
            [&] {
                bot.poll();
                return !bot.playing();
            },
            std::chrono::seconds(60)));
        CHECK_FALSE(bot.live());
        CHECK_FALSE(bot.shown());
        CHECK(bot.onBoard());  // its card says how it ended
        CHECK(bot.threads() == 0);
        CHECK(lichess.moves().size() == 5);
        CHECK(lichess.refused() == 0);            // no move sent twice, none out of turn
        CHECK(app.board_game.moves.size() == 5);  // its end on the Board
        CHECK(lichess.chat() == std::vector<std::string>{"Have+a+good+game%21", "Thank+you+for+the+game%21"});
        frames.draw(app, [&] { bot.drawBoardCard(); });

        // Its game kept in "Lichess · RS_SampleBot", made for it; its record on its card.
        REQUIRE(waitUntil([&] {
            bot.poll();
            const auto index = databaseNamed(app.shelf(), "Lichess · RS_SampleBot");
            return index && app.shelf().facts(*index).games == 1;
        }));
        const auto kept = lichess::readBotSettings(profile / "bot");
        REQUIRE(kept.games.size() == 1);
        CHECK(kept.games[0].id == "gBot0001");
        CHECK(kept.games[0].opponent == "Partner_3");
        CHECK(kept.games[0].result == "1-0");
        CHECK(kept.games[0].white);
        CHECK(kept.declined == 1);
        CHECK(app.revoked.empty());

        // Offline from the palette: its event stream closed.
        listed = commands(bot);
        CHECK(labels(listed) == std::vector<std::string>{"Put the Lichess bot offline", "The Lichess bot's card"});
        entry(listed, "Put the Lichess bot offline")->run();
        CHECK_FALSE(bot.running());
        REQUIRE(waitUntil([&] { return lichess.eventStreams() == 0; }));
    }
}

TEST_CASE("Lichess bot's module without App: a token Lichess refuses puts the bot offline at once") {
    FakeLichess lichess;
    REQUIRE(lichess.ok());
    const ScratchDir profile("bot-module");
    linkBot(profile, "tok-revoked");
    FakeApp app(profile);
    Frames frames;
    {
        LichessBotPage bot(app);
        REQUIRE(applied(bot, "bot-online"));
        CHECK(bot.running());
        // Lichess refuses its token on its event stream: offline at once (its stream closed, not asked again), its
        // card saying to link its account again, online a gesture away.
        REQUIRE(waitUntil([&] {
            bot.poll();
            return !bot.running();
        }));
        const auto asked = lichess.seen("/api/stream/event");
        REQUIRE(asked.size() == 1);
        CHECK(asked[0].authorization == "Bearer tok-revoked");
        CHECK(labels(commands(bot)) ==
              std::vector<std::string>{"Put the Lichess bot online", "The Lichess bot's card"});
        frames.draw(app, [&] { bot.drawEnginePart(app.library[0], false); });
        CHECK(app.revoked.empty());  // its token stays: linking its account again replaces it
        // Said as a notice in place of its line (#146), an error, with its own link: the bot's account, not the
        // user's (never given to the program).
        const auto notice = bot.onlineNotice();
        REQUIRE(notice);
        CHECK(notice->level == Notice::Level::Error);
        CHECK(notice->text == "Lichess refused the bot's token: link its account again.");
        REQUIRE(notice->fixes.size() == 1);
        CHECK(notice->fixes[0].kind == Fix::Kind::LinkLichess);
        CHECK(notice->fixes[0].label == "Link the bot's account…");
        CHECK(notice->fixes[0].path == lichess::SCOPE_BOT_PLAY);
        CHECK(noticeTopic(*notice) == std::string(guide::topics::LichessBot));
    }
}
