// Lichess's arenas and Swiss tournaments' module (#107), built and driven without App: a context of the test's own
// answers what it asks of the program, Lichess is a server on this machine, and the module's pages are drawn in an
// ImGui context of the test's, a key pressed as the user presses it. Its --focus words and palette commands; the card's
// list read and an arena of it followed, the games of its first eight in the mosaic, never the user's own; an arena
// joined by Enter once Lichess takes the join, refused first: every aid locked while the user is in it, between two
// games too, until Pause (Alt+P), and no longer once it is over; Pause and close, RookStudio closed once Lichess took
// the pause, or five seconds after all the same; why the card's lists did not come said with their repair (#146).
#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <json.hpp>

#include "app_context.hpp"
#include "crosstables.hpp"
#include "databases.hpp"
#include "focus_words.hpp"
#include "https.hpp"
#include "jobs.hpp"
#include "lichess_account.hpp"
#include "lichess_arenas_page.hpp"
#include "lichess_board.hpp"
#include "lichess_broadcasts.hpp"
#include "lichess_settings.hpp"
#include "scripted_server.hpp"
#include "test_support.hpp"

using namespace fcg;
using namespace fcg::test;

namespace {

constexpr int ALT_P = ImGuiMod_Alt | ImGuiKey_P;

// The game on the Board through Lichess's Board API, as App holds it. Outside the fake: a nested class's default member
// initializers are read once the class around it is complete, and clang finds it not default-constructible where
// std::optional asks, within that class (emplace() refused).
struct BoardGame {
    std::string id;
    bool ended = false;
    lichess::BoardState state;
};

// What the module asks of App, answered by the test: the profile and its databases of a scratch folder, pages and a
// mosaic that only remember what they were told (the mosaic drawn at every frame while it shows), the Lichess settings
// and account in memory, a game on the Board that the test plays, the frames and their time counted by the test.
class FakeApp final : public ArenasContext {
   public:
    explicit FakeApp(const std::filesystem::path& profile)
        : profile_(profile), shelf_(jobs_, profile), crosstables_(profile) {}

    // The program.
    int frame() const override { return frame_; }
    double seconds() const override { return seconds_; }
    bool shortcutPressed(int chord) const override { return chord == ALT_P && std::exchange(alt_p, false); }
    bool scripted() const override { return false; }
    std::filesystem::path profileDir() const override { return profile_; }
    DatabaseShelf& shelf() override { return shelf_; }
    void toast(const std::string& text) override { toasts.push_back(text); }
    JobGroup& jobs() override { return jobs_; }

    // The pages.
    void showLichessPage() override {
        ++lichess_page;
        mosaic.clear();
    }
    void showBoardPage() override { ++board_page; }
    void toStudyBoard() override { ++board_page; }
    void showPlayTournaments(bool kept) override {
        tournaments_shown = true;
        ++(kept ? tournaments_kept : tournaments_peeked);
    }
    bool playTournamentsShown() const override { return tournaments_shown; }
    bool homeShown() const override { return false; }

    // The databases' pages.
    CrosstableStore& crosstables() override { return crosstables_; }
    void showDatabaseGames(std::size_t, const std::string&) override {}
    void openCrosstable(std::size_t, const std::string&, const std::string&) override {}

    // The mosaic.
    const std::string& mosaicShown() const override { return mosaic; }
    int mosaicDrawn() const override { return mosaic.empty() ? -1000 : frame_; }
    void openMosaic(const std::string& key) override { mosaic = key; }
    void closeMosaic() override { mosaic.clear(); }
    bool boardBackToMosaic() const override { return false; }
    void backToMosaic() override {}
    void setMosaicEngine(const std::string&) override {}
    GameStreams gameStreams() const override { return streams; }
    void setPlayersQuota(std::size_t) override {}

    // Lichess's settings.
    LichessSettings& lichessSettings() override { return settings; }
    void saveLichessSettings() override { ++settings_saved; }
    std::vector<std::string> ownLichessIds() override { return own; }

    // The account.
    const lichess::Account* lichessAccount() const override { return account ? &*account : nullptr; }
    bool lichessLinking() const override { return false; }
    const lichess::Failure& lichessLinkError() const override { return link_error_; }
    void startLichessLink(std::vector<std::string> scopes, std::string) override { links.push_back(std::move(scopes)); }
    std::string scopeAsked() const override { return scope_asked; }
    void askScope(const std::string& scope) override { scope_asked = scope; }
    void dropScopeAsk() override { scope_asked.clear(); }
    const lichess::Profile* lichessProfile() const override { return nullptr; }
    void askLichessProfile() override {}

    // The user's games on Lichess.
    const std::vector<lichess::PlayingGame>& playingGames() const override { return playing; }
    std::optional<context::OnlineGame> onlineGame() const override {
        if (!online) return std::nullopt;
        return context::OnlineGame{online->id, true, true, online->ended, online->state};
    }
    bool liveGameOn() const override { return false; }
    void backToOnlineGame() override {}
    std::string onlineOpponent() const override { return "Player_Z (1851)"; }
    void onlineCall(std::string what, std::function<lichess::Reply(const std::string&)>) override {
        calls.push_back(std::move(what));
    }
    lichess::OnlinePlaySettings& onlineSettings() override { return online_settings_; }
    void saveOnlineSettings() override {}
    std::size_t heldGames(const std::string&) const override { return 0; }
    bool lastHeldArrived(const std::string&) const override { return false; }
    void reviewLastHeld(const std::string&) override {}
    void letGoHeld(const std::string&) override {}
    void closeAnyway() override { ++closed; }

    // The repairs (#146): the account the test links, the hosts' pauses it sets, the fixes the program was given.
    LichessAccess lichessAccess(const std::string& needs) const override {
        LichessAccess a;
        a.linked = account.has_value();
        if (account) a.scopes = account->scopes;
        a.needs = needs;
        return a;
    }
    std::chrono::milliseconds pausedFor(const std::string& host) const override {
        const auto it = pauses.find(host);
        return it == pauses.end() ? std::chrono::milliseconds(0) : it->second;
    }
    void repair(const Fix& fix) override { repairs.push_back(fix); }

    // What the module was told, and what the test sets.
    int frame_         = 0;
    double seconds_    = 100;
    mutable bool alt_p = false;  // Alt+P pressed at the next poll
    std::string mosaic, scope_asked;
    GameStreams streams;
    LichessSettings settings;
    std::optional<lichess::Account> account;
    std::vector<std::string> own, toasts, calls;
    std::vector<std::vector<std::string>> links;
    std::vector<lichess::PlayingGame> playing;
    std::optional<BoardGame> online;
    std::map<std::string, std::chrono::milliseconds> pauses;
    std::vector<Fix> repairs;
    bool tournaments_shown = false;
    int tournaments_kept = 0, tournaments_peeked = 0, lichess_page = 0, board_page = 0, settings_saved = 0, closed = 0;

   private:
    std::filesystem::path profile_;
    JobGroup jobs_;
    DatabaseShelf shelf_;  // after its jobs: gone before them
    CrosstableStore crosstables_;
    lichess::Failure link_error_;
    lichess::OnlinePlaySettings online_settings_;
};

lichess::Account sampleAccount(std::vector<std::string> scopes) {
    lichess::Account a;
    a.username = "RS_Sample";
    a.token    = "tok-arenas";
    a.scopes   = std::move(scopes);
    return a;
}

// An ImGui context of the test's own, without a window: a frame draws a page of the module, a key pressed before it
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

// /api/tournament, shaped as lichess.org answered it on 2026-11-24: a rapid arena in progress, a blitz one, a classical
// one to come.
const char* const LIST = R"({
 "created": [
  {"id": "cla55ic1", "system": "arena", "minutes": 120, "clock": {"limit": 1800, "increment": 0}, "rated": true,
   "fullName": "Daily Classical Arena", "nbPlayers": 388, "variant": {"key": "standard"}, "startsAt": 1795515000000,
   "finishesAt": 1795522200000, "status": 10, "perf": {"key": "classical"}}],
 "started": [
  {"id": "bl1tz320", "system": "arena", "minutes": 57, "clock": {"limit": 180, "increment": 2}, "rated": true,
   "fullName": "Hourly Blitz Arena", "nbPlayers": 4512, "variant": {"key": "standard"}, "startsAt": 1795506000000,
   "finishesAt": 1795509420000, "status": 20, "perf": {"key": "blitz"}},
  {"id": "rap1d100", "system": "arena", "minutes": 117, "clock": {"limit": 600, "increment": 0}, "rated": true,
   "fullName": "Hourly Rapid Arena", "nbPlayers": 1204, "variant": {"key": "standard"}, "startsAt": 1795504000000,
   "finishesAt": 1795511020000, "status": 20, "perf": {"key": "rapid"}}],
 "finished": []
})";

// Lichess on this machine: the list, the rapid arena's page (with the user's token, their verdicts and, once they
// joined, their rank), its join (refused while the test says so: a team's members only) and its pause, the players'
// status, the games' PGN and their streams, open until the test ends. Every name invented.
class FakeLichess {
   public:
    FakeLichess() : server_(0, [this](const std::string& head, net::TcpConnection& c) { serve(head, c); }) {
        const auto base    = fmt::format("http://127.0.0.1:{}", server_.port());
        lichess::servers() = lichess::Servers{base, base + "/tb", base + "/explorer", base + "/engine"};
    }
    ~FakeLichess() {
        quit_              = true;
        lichess::servers() = lichess::Servers{};
    }
    FakeLichess(const FakeLichess&)            = delete;
    FakeLichess& operator=(const FakeLichess&) = delete;

    struct Seen {
        std::string path, authorization, body;
    };

    [[nodiscard]] bool ok() const { return server_.ok(); }
    std::atomic<int> refuse{0};               // a status every request is answered with (0: none), #146
    std::atomic<bool> refuse_joins{false};    // a team's members only
    std::atomic<bool> hold_withdraws{false};  // a pause answered once the test lets it go
    std::atomic<bool> finished{false};        // the arena over
    [[nodiscard]] int joins() const { return joins_; }
    [[nodiscard]] int withdraws() const { return withdraws_; }
    [[nodiscard]] int streamsOpen() const { return streams_; }
    [[nodiscard]] std::vector<Seen> seen(const std::string& path) {
        std::lock_guard lock(mutex_);
        std::vector<Seen> out;
        for (const auto& s : seen_)
            if (s.path == path) out.push_back(s);
        return out;
    }

   private:
    std::string arena(bool token) {
        auto j = nlohmann::json::parse(R"({
 "nbPlayers": 1204, "duels": [], "secondsToFinish": 1860, "isStarted": true,
 "standing": {"page": 1, "players": [
  {"name": "Player_A", "rank": 1, "rating": 1834, "score": 21, "sheet": {"scores": "55533", "fire": true},
   "withdraw": true},
  {"name": "Player_B", "rank": 2, "rating": 1793, "score": 20, "sheet": {"scores": "444422", "fire": true}},
  {"name": "Player_C", "rank": 3, "rating": 2202, "score": 20, "sheet": {"scores": "455330", "fire": true}},
  {"name": "Player_D", "rank": 4, "rating": 1934, "score": 19, "sheet": {"scores": "045532"}}]},
 "id": "rap1d100", "startsAt": "2026-11-24T08:00:25Z", "system": "arena", "fullName": "Hourly Rapid Arena",
 "minutes": 117, "perf": {"key": "rapid"}, "clock": {"limit": 600, "increment": 0}, "variant": "standard",
 "rated": true, "berserkable": true, "minRatedGames": {"nb": 10},
 "verdicts": {"list": [{"condition": "≥ 10 rated Rapid games", "verdict": "ok"}], "accepted": true}})");
        if (finished) {
            j.erase("isStarted");
            j["isFinished"] = true;
        }
        if (token && joined_) j["me"] = {{"rank", 214}, {"withdraw", false}, {"gameId", ""}, {"pauseDelay", 0}};
        return j.dump();
    }
    std::string pgn(const std::string& id, const std::string& white, const std::string& black) {
        return fmt::format(
            "[Event \"Hourly Rapid Arena\"]\n[Site \"https://lichess.org/{}\"]\n[White \"{}\"]\n[Black \"{}\"]\n"
            "[Result \"*\"]\n[TimeControl \"600+0\"]\n\n1. d4 d5 2. c4 e6 *\n",
            id, white, black);
    }
    void serve(const std::string& head, net::TcpConnection& c) {
        Seen s;
        s.body          = requestBody(head, c);
        s.path          = requestPath(head);
        s.authorization = httpHeader(head, "Authorization").value_or("");
        {
            std::lock_guard lock(mutex_);
            seen_.push_back(s);
        }
        // The games of the first eight: their players, white and black.
        static const std::map<std::string, std::pair<std::string, std::string>> games = {
            {"gameb001", {"Player_B", "Player_41"}},
            {"gamec001", {"Player_77", "Player_C"}},
            {"mine0001", {"Player_D", "RS_Sample"}}};
        const auto& path = s.path;
        const auto id    = path.substr(path.rfind('/') + 1).substr(0, 8);
        if (const int status = refuse; status != 0) {
            c.sendAll(reply(status, "{}"));
        } else if (path == "/api/tournament") {
            c.sendAll(reply(200, LIST));
        } else if (path.rfind("/api/tournament/rap1d100/join", 0) == 0) {
            ++joins_;
            if (refuse_joins) {
                c.sendAll(
                    reply(403, R"({"error": "This tournament is reserved for the members of the team Rook Club"})"));
            } else {
                joined_ = true;
                c.sendAll(reply(200, R"({"ok": true})"));
            }
        } else if (path.rfind("/api/tournament/rap1d100/withdraw", 0) == 0) {
            while (hold_withdraws && !quit_) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            ++withdraws_;
            joined_ = false;
            c.sendAll(reply(200, R"({"ok": true})"));
        } else if (path.rfind("/api/tournament/rap1d100", 0) == 0) {
            c.sendAll(reply(200, arena(!s.authorization.empty())));
        } else if (path.rfind("/api/users/status?", 0) == 0) {
            // Player_B and Player_C play a game each, Player_D the user's own.
            c.sendAll(reply(200, R"([{"id": "player_b", "name": "Player_B", "playing": {"id": "gameb001"}},
                                     {"id": "player_c", "name": "Player_C", "playing": {"id": "gamec001"}},
                                     {"id": "player_d", "name": "Player_D", "playing": {"id": "mine0001"}}])"));
        } else if (path.rfind("/game/export/", 0) == 0 && games.count(id)) {
            c.sendAll(reply(200, pgn(id, games.at(id).first, games.at(id).second), "application/x-chess-pgn"));
        } else if (path.rfind("/api/stream/game/", 0) == 0 && games.count(id)) {
            ++streams_;
            c.sendAll(std::string(STREAM_HEAD) +
                      chunk(fmt::format(R"({{"id": "{}", "status": {{"id": 20, "name": "started"}}}})", id) + "\n"));
            while (!quit_) {
                if (!c.sendAll(chunk("\n"), 500)) break;  // the follower left
                (void)c.readUntil("\x01never", 1, 20);
            }
            --streams_;
        } else {
            c.sendAll(reply(404, R"({"error": "Not found"})"));
        }
    }

    std::mutex mutex_;
    std::vector<Seen> seen_;
    std::atomic<bool> joined_{false}, quit_{false};
    std::atomic<int> joins_{0}, withdraws_{0}, streams_{0};
    ScriptedServer server_;  // last: stopped first, its threads using what is above
};

struct Entry {
    std::string label, where;
    std::function<void()> run;
};
std::vector<Entry> commands(LichessArenasPage& arenas) {
    std::vector<Entry> out;
    arenas.addCommands(
        [&](std::string label, std::string where, std::function<void()> run) {
            out.push_back({std::move(label), std::move(where), std::move(run)});
        },
        "Chess", "Play view");
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
bool applied(LichessArenasPage& arenas, const std::string& word) {
    FocusWords words(word);
    arenas.addFocusWords(words, word);
    return words.applied();
}
std::vector<std::string> urls(const std::vector<lichess::BroadcastGame>& games) {
    std::vector<std::string> out;
    for (const auto& g : games) out.push_back(g.url);
    return out;
}

// The user joined in the rapid arena from the Play page: its page read with their token, then Enter on it.
void joinRapidArena(FakeApp& app, LichessArenasPage& arenas, Frames& frames, FakeLichess& lichess) {
    REQUIRE(applied(arenas, "play-arena=rap1d100@Hourly Rapid Arena"));
    REQUIRE(waitUntil([&] {
        arenas.poll();
        return arenas.berserkable();  // its page read
    }));
    const int before = lichess.joins();
    frames.draw(app, [&] { arenas.drawPlayTournaments(); }, ImGuiKey_Enter);
    REQUIRE(waitUntil([&] { return lichess.joins() == before + 1; }));
    REQUIRE(waitUntil([&] {
        arenas.poll();
        return arenas.inArena();
    }));
}

}  // namespace

TEST_CASE("Lichess arenas' module without App: its --focus words and its palette commands") {
    const ScratchDir profile("arenas-module");
    FakeApp app(profile);
    LichessArenasPage arenas(app);

    // Its words, and only those: the others are left to the entries after it.
    app.mosaic = "players";
    CHECK(applied(arenas, "lichess-tournaments"));
    CHECK(app.lichess_page == 1);
    CHECK(app.mosaic.empty());  // the page itself, scrolled to the card
    CHECK(applied(arenas, "play-tournaments"));
    CHECK(app.tournaments_peeked == 1);  // shown, not kept as the user's choice
    CHECK(app.tournaments_kept == 0);
    CHECK_FALSE(arenas.followed());
    CHECK(applied(arenas, "arena-mosaic=rap1d100@Hourly Rapid Arena"));
    CHECK(app.mosaic == "arena:rap1d100");
    CHECK(arenas.followedKey() == "arena:rap1d100");
    CHECK(arenas.followedShown());
    CHECK(applied(arenas, "swiss-mosaic=club0004"));
    CHECK(app.mosaic == "swiss:club0004");
    CHECK(arenas.followedKey() == "swiss:club0004");  // one tournament followed at a time
    CHECK_FALSE(arenas.arenaChosen());
    CHECK(applied(arenas, "play-arena=rap1d100@Hourly Rapid Arena"));
    CHECK(arenas.arenaChosen());
    CHECK(arenas.arenaName() == "Hourly Rapid Arena");
    CHECK(arenas.isArenaGame("rap1d100"));
    CHECK_FALSE(arenas.isArenaGame(""));
    CHECK_FALSE(arenas.inArena());
    CHECK_FALSE(arenas.holdsAids());  // chosen, not joined: nothing locked
    CHECK(app.tournaments_kept == 1);
    CHECK(applied(arenas, "play-swiss=club0002"));
    CHECK(arenas.isSwissGame("club0002"));
    CHECK_FALSE(arenas.arenaChosen());  // chosen, not joined: it gives way to the Swiss
    CHECK(app.tournaments_kept == 2);
    for (const auto* other : {"lichess-tournament", "lichess-tournaments=x", "lichess", "lichess-tv", "arena-mosaic",
                              "tv-mosaic=blitz", "play-arenas", "play-lichess", "play"})
        CHECK_FALSE(applied(arenas, other));

    // Its commands without an account: the card, the Play page's part, the tournament followed.
    auto listed = commands(arenas);
    REQUIRE(listed.size() == 3);
    CHECK(listed[0].label == "Lichess tournaments: arenas and Swiss tournaments");
    CHECK(listed[0].where == "Chess");  // the view the Lichess page is in
    CHECK(listed[1].label == "Play an arena or a Swiss tournament on Lichess");
    CHECK(listed[1].where == "Play view");
    CHECK(listed[2].label == "The tournament followed in the mosaic: ");
    app.mosaic.clear();
    listed[2].run();
    CHECK(app.mosaic == "swiss:club0004");
    listed[0].run();
    CHECK(app.lichess_page == 2);
    CHECK(app.mosaic.empty());
    listed[1].run();
    CHECK(app.tournaments_kept == 3);

    // An account that may not read its teams: their Swiss tournaments asked for on the Lichess page.
    app.account       = sampleAccount({"board:play"});
    listed            = commands(arenas);
    const auto* teams = entry(listed, "Read my teams' Swiss tournaments…");
    REQUIRE(teams != nullptr);
    CHECK(teams->where == "Lichess");
    teams->run();
    CHECK(app.scope_asked == "team:read");
    CHECK(app.lichess_page == 3);
}

TEST_CASE("Lichess arenas' module without App: the card's list, an arena of it followed, its first eight's games") {
    FakeLichess lichess;
    REQUIRE(lichess.ok());
    const ScratchDir profile("arenas-module");
    FakeApp app(profile);
    app.account = sampleAccount({"board:play", "tournament:write"});
    app.playing = {[] {
        lichess::PlayingGame g;  // the user's own game, in the arena: never in the mosaic
        g.id         = "mine0001";
        g.tournament = "rap1d100";
        return g;
    }()};
    Frames frames;
    {
        LichessArenasPage arenas(app);
        // The card drawn: its list asked, without the token, and read; each arena followed, those the Board API plays
        // joined.
        REQUIRE(waitUntil([&] {
            frames.draw(app, [&] { arenas.drawCard(); });
            arenas.poll();
            return entry(commands(arenas), "Follow the arena: Hourly Rapid Arena") != nullptr;
        }));
        frames.draw(app, [&] { arenas.drawCard(); });  // drawn with its list
        const auto listed = labels(commands(arenas));
        CHECK(listed ==
              std::vector<std::string>{"Lichess tournaments: arenas and Swiss tournaments",
                                       "Play an arena or a Swiss tournament on Lichess",
                                       "Follow the arena: Hourly Blitz Arena", "Follow the arena: Hourly Rapid Arena",
                                       "Join the arena: Hourly Rapid Arena", "Follow the arena: Daily Classical Arena",
                                       "Join the arena: Daily Classical Arena", "Read my teams' Swiss tournaments…"});
        const auto asked = lichess.seen("/api/tournament");
        REQUIRE(asked.size() == 1);  // once a minute at most
        CHECK(asked[0].authorization.empty());

        // An arena of it followed: the mosaic opens on it; its standing read while the mosaic shows it, the games of
        // its first eight found by their status, in the standing's order, never the user's own.
        entry(commands(arenas), "Follow the arena: Hourly Rapid Arena")->run();
        CHECK(app.mosaic == "arena:rap1d100");
        CHECK(arenas.followedShown());
        REQUIRE(waitUntil([&] {
            arenas.poll();
            return urls(arenas.mosaicGames()).size() == 2;
        }));
        CHECK(urls(arenas.mosaicGames()) ==
              std::vector<std::string>{"https://lichess.org/gameb001", "https://lichess.org/gamec001"});
        CHECK(arenas.slots() == urls(arenas.mosaicGames()));
        CHECK(lichess.seen("/api/users/status?ids=Player_B,Player_C,Player_D&withGameMetas=true").size() == 1);
        frames.draw(app, [&] {
            arenas.drawMosaicHead();
            arenas.drawStanding();
        });
        REQUIRE(waitUntil([&] { return arenas.streams() == 2 && lichess.streamsOpen() == 2; }));

        // The rounds followed take their streams: one left to the arena's games.
        app.streams.rounds = 7;
        REQUIRE(waitUntil([&] {
            arenas.poll();
            return arenas.streams() == 1 && lichess.streamsOpen() == 1;
        }));

        // The mosaic closed: its games stop.
        app.mosaic.clear();
        arenas.poll();
        CHECK_FALSE(arenas.followedShown());
        CHECK(arenas.streams() == 0);
        REQUIRE(waitUntil([&] { return lichess.streamsOpen() == 0; }));
    }
}

TEST_CASE(
    "Lichess arenas' module without App: a join refused locks nothing; in the arena every aid locked until Pause") {
    FakeLichess lichess;
    REQUIRE(lichess.ok());
    const ScratchDir profile("arenas-module");
    FakeApp app(profile);
    app.account = sampleAccount({"board:play", "tournament:write"});
    Frames frames;
    {
        LichessArenasPage arenas(app);
        REQUIRE(applied(arenas, "play-arena=rap1d100@Hourly Rapid Arena"));
        CHECK(app.tournaments_shown);
        // Its page, with the user's token, read while the Play page shows it.
        REQUIRE(waitUntil([&] {
            arenas.poll();
            return arenas.berserkable();
        }));
        CHECK(lichess.seen("/api/tournament/rap1d100").at(0).authorization == "Bearer tok-arenas");
        CHECK_FALSE(arenas.holdsAids());

        // Enter joins it; Lichess refuses (a team's members only): not in the arena, nothing locked.
        lichess.refuse_joins = true;
        frames.draw(app, [&] { arenas.drawPlayTournaments(); }, ImGuiKey_Enter);
        REQUIRE(waitUntil([&] { return lichess.joins() == 1; }));
        const auto join = lichess.seen("/api/tournament/rap1d100/join").at(0);
        CHECK(join.authorization == "Bearer tok-arenas");
        CHECK(join.body == "pairMeAsap=true");
        // Its refusal read, Enter joins again (the second join not served yet, nothing came of it), and Lichess
        // takes it.
        lichess.refuse_joins = false;
        REQUIRE(waitUntil([&] {
            arenas.poll();
            if (lichess.joins() == 2) return true;
            CHECK_FALSE(arenas.inArena());
            CHECK_FALSE(arenas.holdsAids());
            frames.draw(app, [&] { arenas.drawPlayTournaments(); }, ImGuiKey_Enter);
            return false;
        }));
        REQUIRE(waitUntil([&] {
            arenas.poll();
            return arenas.inArena();
        }));

        // In the arena (point 18): every aid locked, the lock holding the arena between two games.
        CHECK(arenas.holdsAids());
        CHECK(arenas.waiting().tournament == "rap1d100");
        CHECK(arenas.waiting().speed == "rapid");
        auto listed = labels(commands(arenas));
        CHECK(std::count(listed.begin(), listed.end(), "Pause in the arena") == 1);
        CHECK(std::count(listed.begin(), listed.end(), "Back to the arena") == 1);
        frames.draw(app, [&] {
            arenas.drawPlayTournaments();
            arenas.drawHome();
        });
        // A game of it played on the Board: locked during it, berserk offered before the first move; locked still
        // once it is over, waiting for the next one.
        app.online.emplace();
        app.online->id               = "game0001";
        app.online->state.full       = true;
        app.online->state.speed      = "rapid";
        app.online->state.tournament = "rap1d100";
        arenas.poll();
        CHECK(arenas.holdsAids());
        listed = labels(commands(arenas));
        CHECK(std::count(listed.begin(), listed.end(), "Berserk") == 1);
        entry(commands(arenas), "Berserk")->run();
        CHECK(app.calls == std::vector<std::string>{"Berserk"});
        app.online->state.moves  = {"e2e4", "e7e5"};
        app.online->state.status = "mate";
        app.online->state.winner = "white";
        app.online->ended        = true;
        arenas.poll();
        CHECK(arenas.lastGame() == "game0001");
        CHECK(arenas.holdsAids());
        frames.draw(app, [&] { arenas.drawArenaGameBefore(); });

        // Alt+P: paused, the aids back at once, Lichess told.
        app.alt_p = true;
        arenas.poll();
        CHECK_FALSE(arenas.inArena());
        CHECK_FALSE(arenas.holdsAids());
        REQUIRE(waitUntil([&] { return lichess.withdraws() == 1; }));
        listed = labels(commands(arenas));
        CHECK(std::count(listed.begin(), listed.end(), "Back in the arena") == 1);
        // Alt+P again: in it again, locked again.
        app.alt_p = true;
        REQUIRE(waitUntil([&] {
            arenas.poll();
            return lichess.joins() == 3 && arenas.inArena();
        }));
        CHECK(arenas.holdsAids());

        // The arena over (its page read again at the end of the next game): nothing locked any more.
        lichess.finished = true;
        app.online->id   = "game0002";
        REQUIRE(waitUntil([&] {
            arenas.poll();
            return arenas.arenaOver();
        }));
        CHECK_FALSE(arenas.holdsAids());
        CHECK_FALSE(arenas.inArena());
    }
}

TEST_CASE("Lichess arenas' module without App: Pause and close, closed once Lichess took the pause or 5 s after") {
    FakeLichess lichess;
    REQUIRE(lichess.ok());
    const ScratchDir profile("arenas-module");
    FakeApp app(profile);
    app.account = sampleAccount({"board:play", "tournament:write"});
    Frames frames;
    {
        LichessArenasPage arenas(app);
        joinRapidArena(app, arenas, frames, lichess);
        REQUIRE(arenas.holdsAids());

        // Lichess takes the pause: RookStudio closes then.
        arenas.pauseAndClose();
        CHECK_FALSE(arenas.holdsAids());  // the aids back at once
        REQUIRE(waitUntil([&] {
            arenas.poll();
            return app.closed == 1;
        }));
        CHECK(lichess.withdraws() == 1);

        // In it again; Lichess slow to take the pause: closed all the same five seconds after, as the frames count.
        app.alt_p = true;
        REQUIRE(waitUntil([&] {
            arenas.poll();
            return arenas.inArena();
        }));
        lichess.hold_withdraws = true;
        arenas.pauseAndClose();
        app.seconds_ += 4.9;
        arenas.poll();
        CHECK(app.closed == 1);
        app.seconds_ += 0.2;
        arenas.poll();
        CHECK(app.closed == 2);
        lichess.hold_withdraws = false;
        REQUIRE(waitUntil([&] { return lichess.withdraws() == 2; }));
    }
}

TEST_CASE("Lichess arenas' module without App: why the card's lists did not come said with their repair (#146)") {
    using namespace std::chrono_literals;
    FakeLichess lichess;
    REQUIRE(lichess.ok());
    HostGate::instance().setPause(50ms);  // the host's minute after a 429, short for the test
    const ScratchDir profile("arenas-repairs");
    FakeApp app(profile);
    app.account = sampleAccount({lichess::SCOPE_TEAM_READ});  // its teams' Swiss tournaments read too
    Frames frames;
    LichessArenasPage arenas(app);
    const auto host = lichess::apiHost();
    // The card drawn, as the Lichess page draws it, until `done`.
    const auto drawn = [&](const std::function<bool()>& done) {
        return waitUntil([&] {
            frames.draw(app, [&] { arenas.drawCard(); });
            arenas.poll();
            return done();
        });
    };
    const auto listAsked = [&] { return static_cast<int>(lichess.seen("/api/tournament").size()); };

    // A token refused (401): the arenas and the teams' Swiss tournaments say it alike, as the studies card does, with
    // Link Lichess again…, which the program carries out.
    lichess.refuse = 401;
    REQUIRE(drawn([&] { return arenas.listNotice() && arenas.swissesNotice(); }));
    for (const auto& n : {*arenas.listNotice(), *arenas.swissesNotice()}) {
        CHECK(n.level == Notice::Level::Error);
        CHECK(n.text == "Lichess refused the token: it was revoked or has expired.");
        REQUIRE(n.fixes.size() == 1);
        CHECK(n.fixes[0].kind == Fix::Kind::LinkLichess);
        CHECK(n.fixes[0].label == "Link Lichess again…");
        CHECK(n.fixes[0].path.empty());  // the account has team:read already
    }
    arenas.repair(arenas.swissesNotice()->fixes[0]);
    REQUIRE(app.repairs.size() == 1);
    CHECK(app.repairs[0].kind == Fix::Kind::LinkLichess);

    // The Swiss tournaments' Try again, at once: asked again with the token.
    const auto teams = [&] { return static_cast<int>(lichess.seen("/api/team/of/RS_Sample").size()); };
    int asked        = teams();
    lichess.refuse   = 503;
    arenas.repair(retryAfterPauseFix(Retry::TeamSwisses, host));
    REQUIRE(drawn([&] {
        const auto n = arenas.swissesNotice();
        return teams() == asked + 1 && n && n->level == Notice::Level::Warning;
    }));
    CHECK(arenas.swissesNotice()->text == "Lichess gave nothing (Lichess answered 503).");
    CHECK(arenas.swissesNotice()->fixes.at(0).label == "Try again");

    // Lichess in trouble (503): the arenas' Try again, asked at once by the module.
    asked = listAsked();
    arenas.repair(retryAfterPauseFix(Retry::ArenaList, host));
    REQUIRE(drawn([&] {
        const auto n = arenas.listNotice();
        return listAsked() == asked + 1 && n && n->level == Notice::Level::Warning;
    }));
    auto notice = *arenas.listNotice();
    CHECK(notice.text == "Lichess gave nothing (Lichess answered 503).");
    REQUIRE(notice.fixes.size() == 1);
    CHECK(notice.fixes[0].label == "Try again");
    CHECK(app.repairs.size() == 1);  // the module's own: not the program's
    asked          = listAsked();
    lichess.refuse = 429;
    arenas.repair(notice.fixes[0]);
    REQUIRE(drawn([&] { return listAsked() == asked + 1 && arenas.listNotice().has_value(); }));

    // Its pause (429), in orange, counted down on Try again; a click queues it and nothing is sent before its end.
    app.pauses[host] = 42s;
    notice           = *arenas.listNotice();
    CHECK(notice.level == Notice::Level::Warning);
    CHECK(notice.text == "Lichess asked for a pause after too many requests.");
    REQUIRE(notice.fixes.size() == 1);
    CHECK(notice.fixes[0].label == "Try again in 0:42");
    asked = listAsked();
    arenas.repair(notice.fixes[0]);
    arenas.repair(notice.fixes[0]);  // a second click: still once
    notice = *arenas.listNotice();
    REQUIRE(notice.fixes.size() == 1);
    CHECK(notice.fixes[0].label == "Asks again in 0:42");
    CHECK(notice.fixes[0].disabled);
    const auto until = std::chrono::steady_clock::now() + 300ms;
    while (std::chrono::steady_clock::now() < until) {
        frames.draw(app, [&] { arenas.drawCard(); });
        arenas.poll();
    }
    CHECK(listAsked() == asked);
    // The pause over: asked once, and the list comes.
    app.pauses[host] = 0ms;
    lichess.refuse   = 0;
    REQUIRE(drawn([&] { return !arenas.listNotice() && listAsked() == asked + 1; }));
    for (int i = 0; i < 20; ++i) {
        frames.draw(app, [&] { arenas.drawCard(); });
        arenas.poll();
    }
    CHECK(listAsked() == asked + 1);
    CHECK(app.repairs.size() == 1);
}
