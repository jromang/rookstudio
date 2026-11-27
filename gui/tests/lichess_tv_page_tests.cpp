// Lichess TV's module (#107), built and driven without App: a context of the test's own answers what it asks of
// the program, and Lichess is a server on this machine. Its --focus words and palette commands; a channel's games in
// the mosaic, the user's own left out; the Board's game kept into a database at its end, then the channel's next game
// on the Board in its place; why the channels did not come said with its repair (#146).
#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <json.hpp>

#include <study/game.hpp>

#include "app_context.hpp"
#include "console.hpp"
#include "databases.hpp"
#include "focus_words.hpp"
#include "https.hpp"
#include "jobs.hpp"
#include "lichess_broadcasts.hpp"
#include "lichess_settings.hpp"
#include "lichess_tv_page.hpp"
#include "scripted_server.hpp"
#include "test_support.hpp"

using namespace fcg;
using namespace fcg::test;

namespace {

// What the module asks of App, answered by the test: the profile and its databases of a scratch folder, a mosaic and
// a Board that only remember what they were told, the Lichess settings in memory.
class FakeApp final : public TvContext {
   public:
    explicit FakeApp(const std::filesystem::path& profile) : profile_(profile), shelf_(jobs_, profile) {}

    int frame() const override { return frame_; }
    double seconds() const override { return 0; }
    bool shortcutPressed(int) const override { return false; }
    bool scripted() const override { return false; }
    std::filesystem::path profileDir() const override { return profile_; }
    DatabaseShelf& shelf() override { return shelf_; }
    void toast(const std::string& text) override { toasts.push_back(text); }
    JobGroup& jobs() override { return jobs_; }

    void showLichessPage() override {
        ++lichess_page;
        mosaic.clear();
    }
    void showBoardPage() override { ++board_page; }
    void toStudyBoard() override { ++board_page; }
    void showPlayTournaments(bool) override {}
    bool playTournamentsShown() const override { return false; }
    bool homeShown() const override { return false; }

    std::string boardTitle() const override { return board.title; }
    std::string boardFollows() const override { return board.url; }
    void followOnBoard(ReplayGame game, std::string title, std::string mosaic_key, std::string url, uint64_t version,
                       bool in_place) override {
        board.title    = std::move(title);
        board.mosaic   = std::move(mosaic_key);
        board.url      = std::move(url);
        board.version  = version;
        board.in_place = in_place;
        board.moves    = game.moves.size();
        ++board.games;
    }

    const std::string& mosaicShown() const override { return mosaic; }
    int mosaicDrawn() const override { return frame_; }
    void openMosaic(const std::string& key) override { mosaic = key; }
    void closeMosaic() override { mosaic.clear(); }
    bool boardBackToMosaic() const override { return !board.mosaic.empty(); }
    void backToMosaic() override { mosaic = board.mosaic; }
    void setMosaicEngine(const std::string& engine) override { settings.broadcast_engine = engine; }
    GameStreams gameStreams() const override { return streams; }
    void setPlayersQuota(std::size_t quota) override { players_quota = quota; }

    LichessSettings& lichessSettings() override { return settings; }
    void saveLichessSettings() override { ++settings_saved; }
    std::vector<std::string> ownLichessIds() override { return own; }

    // The repairs (#146): the account as the test sets it, the hosts' pauses it sets, the fixes the program was given.
    LichessAccess lichessAccess(const std::string& needs) const override {
        auto a  = access;
        a.needs = needs;
        return a;
    }
    std::chrono::milliseconds pausedFor(const std::string& host) const override {
        const auto it = pauses.find(host);
        return it == pauses.end() ? std::chrono::milliseconds(0) : it->second;
    }
    void repair(const Fix& fix) override { repairs.push_back(fix); }

    // What the module was told, and what the test sets.
    struct Board {
        std::string title, mosaic, url;
        uint64_t version  = 0;
        bool in_place     = false;
        std::size_t moves = 0;
        int games         = 0;  // the games put on it
    } board;
    std::string mosaic;
    GameStreams streams;
    std::size_t players_quota = 99;
    LichessSettings settings;
    int settings_saved = 0, lichess_page = 0, board_page = 0;
    std::vector<std::string> toasts, own;
    int frame_ = 0;
    LichessAccess access;
    std::map<std::string, std::chrono::milliseconds> pauses;
    std::vector<Fix> repairs;

   private:
    std::filesystem::path profile_;
    JobGroup jobs_;
    DatabaseShelf shelf_;  // after its jobs: gone before them
};

// An ImGui context of the test's own, without a window: a frame draws the card, which then counts as shown.
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

    void draw(FakeApp& app, const std::function<void()>& page) {
        ++app.frame_;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("page", nullptr, ImGuiWindowFlags_NoDecoration);
        page();
        ImGui::End();
        ImGui::Render();
    }

   private:
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

// A game of the NDJSON exports.
std::string gameLine(const std::string& id, const std::string& white, const std::string& black) {
    const auto side = [](const std::string& name) {
        std::string id = name;
        for (auto& c : id) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return nlohmann::json{{"user", {{"name", name}, {"id", id}}}, {"rating", 2500}};
    };
    const nlohmann::json j = {{"id", id},
                              {"rated", true},
                              {"variant", "standard"},
                              {"speed", "blitz"},
                              {"status", "started"},
                              {"players", {{"white", side(white)}, {"black", side(black)}}},
                              {"clock", {{"initial", 180}, {"increment", 2}, {"totalTime", 260}}}};
    return j.dump() + "\n";
}

// Lichess on this machine: the channels (blitz's featured game `featured`), the blitz channel's games, the games by
// their ids, each game's PGN and its stream, open until the test ends the game.
class FakeLichess {
   public:
    struct Game {
        std::string white, black;
    };
    explicit FakeLichess(std::map<std::string, Game> games)
        : games_(std::move(games)),
          server_(0, [this](const std::string& head, net::TcpConnection& c) { serve(head, c); }) {
        const auto base    = fmt::format("http://127.0.0.1:{}", server_.port());
        lichess::servers() = lichess::Servers{base, base + "/tb", base + "/explorer", base + "/engine"};
    }
    ~FakeLichess() {
        quit_              = true;
        lichess::servers() = lichess::Servers{};
    }
    FakeLichess(const FakeLichess&)            = delete;
    FakeLichess& operator=(const FakeLichess&) = delete;

    [[nodiscard]] bool ok() const { return server_.ok(); }
    void setFeatured(const std::string& id) {
        std::lock_guard lock(mutex_);
        featured_ = id;
    }
    void setChannel(std::vector<std::string> ids) {
        std::lock_guard lock(mutex_);
        channel_ = std::move(ids);
    }
    void end(const std::string& id) {
        std::lock_guard lock(mutex_);
        ended_.insert(id);
    }
    [[nodiscard]] int streamsOpen() const { return streams_; }
    // A status every request is answered with (0: none), Lichess refusing or in trouble; the channels asked so far.
    std::atomic<int> refuse{0};
    [[nodiscard]] int channelsAsked() const { return channels_asked_; }

   private:
    [[nodiscard]] bool ended(const std::string& id) {
        std::lock_guard lock(mutex_);
        return ended_.count(id) > 0;
    }
    std::string pgn(const std::string& id) {
        const auto& g   = games_.at(id);
        const bool over = ended(id);
        return fmt::format(
            "[Event \"rated blitz game\"]\n[Site \"https://lichess.org/{}\"]\n[White \"{}\"]\n[Black \"{}\"]\n"
            "[Result \"{}\"]\n[TimeControl \"180+2\"]\n\n1. e4 e5 2. Nf3 Nc6 {}\n",
            id, g.white, g.black, over ? "1-0" : "*", over ? "3. Bc4 Nf6 4. Ng5 d5 1-0" : "*");
    }
    void serve(const std::string& head, net::TcpConnection& c) {
        const auto body = requestBody(head, c);
        const auto path = requestPath(head);
        const auto id   = path.substr(path.rfind('/') + 1).substr(0, 8);
        if (path == "/api/tv/channels") ++channels_asked_;
        if (const int status = refuse; status != 0) {
            c.sendAll(reply(status, "{}"));
        } else if (path == "/api/tv/channels") {
            std::lock_guard lock(mutex_);
            const nlohmann::json channels = {{"blitz",
                                              {{"user", {{"name", "Player_E"}, {"id", "player_e"}}},
                                               {"rating", 2500},
                                               {"gameId", featured_},
                                               {"color", "white"}}}};
            c.sendAll(reply(200, channels.dump()));
        } else if (path.rfind("/api/tv/blitz?", 0) == 0) {
            std::string out;
            std::vector<std::string> listed;
            {
                std::lock_guard lock(mutex_);
                listed = channel_;
            }
            for (const auto& g : listed) out += gameLine(g, games_.at(g).white, games_.at(g).black);
            c.sendAll(reply(200, out, "application/x-ndjson"));
        } else if (path.rfind("/api/games/export/_ids", 0) == 0) {
            std::string out;
            for (const auto& [g, sides] : games_)
                if (body.find(g) != std::string::npos) out += gameLine(g, sides.white, sides.black);
            c.sendAll(reply(200, out, "application/x-ndjson"));
        } else if (path.rfind("/game/export/", 0) == 0 && games_.count(id)) {
            c.sendAll(reply(200, pgn(id), "application/x-chess-pgn"));
        } else if (path.rfind("/api/stream/game/", 0) == 0 && games_.count(id)) {
            ++streams_;
            c.sendAll(std::string(STREAM_HEAD) +
                      chunk(fmt::format(R"({{"id": "{}", "status": {{"id": 20, "name": "started"}}}})", id) + "\n"));
            while (!ended(id) && !quit_) {
                if (!c.sendAll(chunk("\n"), 500)) break;  // the follower left
                (void)c.readUntil("\x01never", 1, 20);
            }
            if (ended(id))
                c.sendAll(chunk(fmt::format(R"({{"id": "{}", "status": {{"id": 30, "name": "mate"}}}})", id) + "\n") +
                          LAST_CHUNK);
            --streams_;
        } else {
            c.sendAll(reply(404, "{}"));
        }
    }

    const std::map<std::string, Game> games_;
    std::mutex mutex_;
    std::string featured_ = "game0003";
    std::vector<std::string> channel_;
    std::set<std::string> ended_;
    std::atomic<int> streams_{0}, channels_asked_{0};
    std::atomic<bool> quit_{false};
    ScriptedServer server_;  // last: stopped first, its threads using what is above
};

struct Entry {
    std::string label, where;
    std::function<void()> run;
};
std::vector<Entry> commands(LichessTvPage& tv) {
    std::vector<Entry> out;
    tv.addCommands(
        [&](std::string label, std::string where, std::function<void()> run) {
            out.push_back({std::move(label), std::move(where), std::move(run)});
        },
        "Chess");
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
bool applied(LichessTvPage& tv, const std::string& word) {
    FocusWords words(word);
    tv.addFocusWords(words, word);
    return words.applied();
}

}  // namespace

TEST_CASE("Lichess TV's module without App: its --focus words and its palette commands") {
    const ScratchDir profile("tv-module");
    FakeApp app(profile);
    LichessTvPage tv(app);

    // Its words, and only those: the others are left to the entries after it.
    app.mosaic = "players";
    CHECK(applied(tv, "lichess-tv"));
    CHECK(app.lichess_page == 1);
    CHECK(app.mosaic.empty());  // the page itself, where its card is
    CHECK(applied(tv, "tv-mosaic=blitz"));
    CHECK(app.mosaic == "tv:blitz");
    CHECK(app.settings.tv_channel == "blitz");  // the one shown last, saved
    CHECK(app.settings_saved == 1);
    CHECK(tv.channelShown() == "blitz");
    CHECK(applied(tv, "tv-mosaic=chess960"));  // a variant's channel: its word, but nothing opens
    CHECK(app.mosaic == "tv:blitz");
    for (const auto* other :
         {"lichess-tvx", "lichess", "lichess-tournaments", "tv-mosaic", "tv-board", "arena-mosaic=x"})
        CHECK_FALSE(applied(tv, other));
    CHECK_FALSE(tv.onBoard());
    CHECK(applied(tv, "tv-board=blitz:game0003"));
    CHECK(tv.onBoard());
    CHECK(app.board.url == "https://lichess.org/game0003");
    CHECK(app.board.mosaic == "tv:blitz");  // the way back
    CHECK(app.board.title == "game0003");
    CHECK_FALSE(app.board.in_place);
    CHECK(applied(tv, "tv-board=atomic:game0004"));  // not a channel shown: the Board keeps its game
    CHECK(app.board.games == 1);

    // Its commands: the card, each channel in the mosaic (none on the Board before the card read them), the Board's
    // game.
    auto listed = commands(tv);
    REQUIRE(listed.size() == 1 + 8 + 2);
    CHECK(listed[0].label == "Lichess TV");
    CHECK(listed[0].where == "Chess");  // the view the Lichess page is in
    CHECK(listed[1].label == "Lichess TV: Top rated in the mosaic");
    CHECK(listed[8].label == "Lichess TV: Computer in the mosaic");
    CHECK(listed[9].label == "Lichess TV: stay on this game");
    CHECK(listed[9].where == "Board");
    CHECK(listed[10].label == "Lichess TV: keep this game…");
    entry(listed, "Lichess TV: Bots in the mosaic")->run();
    CHECK(app.mosaic == "tv:bot");
    entry(listed, "Lichess TV: stay on this game")->run();
    listed = commands(tv);
    CHECK(entry(listed, "Lichess TV: go on to the channel's next game") != nullptr);
    CHECK(entry(listed, "Lichess TV: stay on this game") == nullptr);
    entry(listed, "Lichess TV: keep this game…")->run();
    CHECK(app.board_page == 1);
    entry(listed, "Lichess TV")->run();
    CHECK(app.lichess_page == 2);
    CHECK(app.mosaic.empty());

    // Another game on the Board: the channel's is no longer followed, its commands go.
    tv.leaveBoard();
    CHECK_FALSE(tv.onBoard());
    CHECK(labels(commands(tv)).size() == 1 + 8);
}

TEST_CASE("Lichess TV's module without App: a channel's games in the mosaic, never the user's own") {
    FakeLichess lichess({{"game0003", {"Player_A", "RS_Sample"}},
                         {"game0013", {"Player_C", "Player_D"}},
                         {"game0014", {"Player_E", "Player_F"}}});
    REQUIRE(lichess.ok());
    lichess.setChannel({"game0003", "game0013", "game0014"});
    const ScratchDir profile("tv-module");
    FakeApp app(profile);
    app.own = {"rs_sample"};  // the featured game is the user's: never shown
    {
        LichessTvPage tv(app);
        tv.openMosaic("blitz");
        const auto urls = [&] {
            std::vector<std::string> out;
            for (const auto& g : tv.mosaicGames()) out.push_back(g.url);
            return out;
        };
        REQUIRE(waitUntil([&] {
            tv.poll();
            return urls().size() == 2;
        }));
        CHECK(urls() == std::vector<std::string>{"https://lichess.org/game0013", "https://lichess.org/game0014"});
        CHECK(tv.slots() == urls());
        CHECK(tv.listedOf() == "blitz");
        CHECK(tv.featured() == "game0003");
        CHECK_FALSE(tv.listError().failed());
        CHECK(tv.room() == 8);          // the address's eight streams, none taken by the others
        CHECK(app.players_quota == 0);  // the followed players' mosaic is not the one shown
        REQUIRE(waitUntil([&] { return lichess.streamsOpen() == 2 && tv.streams() == 2; }));
        const auto games = tv.mosaicGames();
        CHECK(games[0].white == "Player_C");
        CHECK(games[0].result == "*");

        // The rounds followed take theirs: fewer left to the channel.
        app.streams.rounds = 7;
        tv.poll();
        CHECK(tv.room() == 1);

        // The mosaic closed: its games stop.
        app.mosaic.clear();
        tv.poll();
        CHECK(tv.slots().empty());
        CHECK(tv.streams() == 0);
        REQUIRE(waitUntil([&] { return lichess.streamsOpen() == 0; }));
    }
}

TEST_CASE("Lichess TV's module without App: the Board's game kept at its end, then the channel's next one") {
    FakeLichess lichess({{"game0003", {"Player_E", "Player_F"}}, {"game0021", {"Player_G", "Player_H"}}});
    REQUIRE(lichess.ok());
    const ScratchDir profile("tv-module");
    FakeApp app(profile);
    {
        LichessTvPage tv(app);
        tv.followOnBoard("blitz", "game0003", true, "Player_E – Player_F", "3+2");
        REQUIRE(tv.onBoard());
        tv.poll();  // as App's frames do before anything is drawn: the games kept before read from the profile
        // Kept into "Lichess TV", made for it, at its end; in the profile until then.
        tv.keepGame({}, "Lichess TV");
        REQUIRE(app.shelf().entries().size() == 1);
        CHECK(app.shelf().entries()[0].name == "Lichess TV");
        CHECK(app.settings.tv_keep_base == app.shelf().entries()[0].folder.string());
        const auto kept = nlohmann::json::parse(readFile(profile / "lichess-tv.json"));
        REQUIRE(kept["kept"].size() == 1);
        CHECK(kept["kept"][0]["id"] == "game0003");
        CHECK(kept["kept"][0]["title"] == "Player_E – Player_F");

        // Followed while it goes on.
        REQUIRE(waitUntil([&] {
            tv.poll();
            return lichess.streamsOpen() == 1;
        }));
        CHECK(app.players_quota == 0);

        // It ends: into its database, and the Board on to the channel's next featured game, where it is.
        lichess.setFeatured("game0021");
        lichess.end("game0003");
        REQUIRE(waitUntil([&] {
            tv.poll();
            return app.board.url == "https://lichess.org/game0021";
        }));
        CHECK(app.shelf().facts(0).games == 1);
        REQUIRE(app.toasts.size() == 1);
        CHECK(app.toasts[0] == "Player_E – Player_F kept in “Lichess TV”");
        CHECK(nlohmann::json::parse(readFile(profile / "lichess-tv.json"))["kept"].empty());
        CHECK(app.board.title == "Player_G – Player_H");
        CHECK(app.board.mosaic == "tv:blitz");
        CHECK(app.board.in_place);  // the page and the way back stay as they are
        CHECK(tv.onBoard());
        // Its game followed in its turn.
        REQUIRE(waitUntil([&] {
            tv.poll();
            const auto games = tv.games();
            return std::any_of(games.begin(), games.end(),
                               [](const auto& g) { return g.url == "https://lichess.org/game0021"; });
        }));
    }
}

TEST_CASE("Lichess TV's module without App: a game kept of the wrong type set aside and said; cut short too (#136)") {
    FakeLichess lichess({});  // nothing asked of lichess.org
    REQUIRE(lichess.ok());
    const ScratchDir profile("tv-kept-types");
    const auto file = profile / "lichess-tv.json";
    // A game kept whose id is a number (an older or newer version, a hand edit): it used to stop the program as soon as
    // the module read the games kept.
    const std::string written = R"({"kept": [{"id": 7, "title": "A – B", "folder": "x", "base": "TV"},)"
                                R"( {"id": "game0003", "title": "Player_E – Player_F", "folder": "y", "base": "TV"}]})";
    std::ofstream(file, std::ios::binary) << written;
    FakeApp app(profile);
    auto before = Console::instance().added();
    {
        LichessTvPage tv(app);
        REQUIRE_NOTHROW(tv.poll());
    }
    auto said = Console::instance().since(before);
    REQUIRE(said.size() == 1);
    CHECK(said[0].find("1 entry could not be read and was set aside (kept/1)") != std::string::npos);
    CHECK(readFile(std::filesystem::path(file).concat(".unreadable")) == written);
    // Cut short: kept aside, said.
    std::ofstream(file, std::ios::binary) << R"({"kept": [{"id": "game0)";
    before = Console::instance().added();
    {
        LichessTvPage tv(app);
        REQUIRE_NOTHROW(tv.poll());
    }
    said = Console::instance().since(before);
    REQUIRE(said.size() == 1);
    CHECK(said[0].find("could not be read: kept as") != std::string::npos);
    CHECK(std::filesystem::exists(std::filesystem::path(file).concat(".unreadable2")));
}

TEST_CASE("Lichess TV's module without App: why its channels did not come said with its repair (#146)") {
    using namespace std::chrono_literals;
    FakeLichess lichess({});
    REQUIRE(lichess.ok());
    HostGate::instance().setPause(50ms);  // the host's minute after a 429, short for the test
    const ScratchDir profile("tv-repairs");
    FakeApp app(profile);
    app.access.linked = true;  // an account linked, its token refused
    Frames frames;
    LichessTvPage tv(app);
    const auto host = lichess::apiHost();
    // The card drawn, as the Lichess page draws it, until `done`.
    const auto drawn = [&](const std::function<bool()>& done) {
        return waitUntil([&] {
            frames.draw(app, [&] { tv.drawCard(); });
            tv.poll();
            return done();
        });
    };

    // A 401: the channels are asked without the token, so no link would change it. Lichess refusing, with Try again,
    // the module's own; nothing given to the program, though an account is linked.
    lichess.refuse = 401;
    REQUIRE(drawn([&] { return tv.channelsNotice().has_value(); }));
    auto notice = *tv.channelsNotice();
    CHECK(notice.level == Notice::Level::Warning);
    CHECK(notice.text == "Lichess gave nothing (Lichess answered 401).");
    REQUIRE(notice.fixes.size() == 1);
    CHECK(notice.fixes[0].kind == Fix::Kind::RetryAfterPause);
    CHECK(notice.fixes[0].label == "Try again");

    // Lichess in trouble (503): Try again, asked at once, by the module.
    lichess.refuse = 503;
    tv.repair(notice.fixes[0]);
    REQUIRE(drawn([&] {
        const auto n = tv.channelsNotice();
        return n && n->text != notice.text;
    }));
    notice = *tv.channelsNotice();
    CHECK(notice.level == Notice::Level::Warning);
    CHECK(notice.text == "Lichess gave nothing (Lichess answered 503).");
    REQUIRE(notice.fixes.size() == 1);
    CHECK(notice.fixes[0].label == "Try again");
    CHECK(app.repairs.empty());  // the module's own: not the program's
    int asked      = lichess.channelsAsked();
    lichess.refuse = 429;
    tv.repair(notice.fixes[0]);
    REQUIRE(drawn([&] { return lichess.channelsAsked() == asked + 1 && tv.channelsNotice().has_value(); }));

    // Its pause (429), in orange, counted down on Try again; a click queues it and nothing is sent before its end.
    app.pauses[host] = 42s;
    frames.draw(app, [&] { tv.drawCard(); });
    notice = *tv.channelsNotice();
    CHECK(notice.level == Notice::Level::Warning);
    CHECK(notice.text == "Lichess asked for a pause after too many requests.");
    REQUIRE(notice.fixes.size() == 1);
    CHECK(notice.fixes[0].label == "Try again in 0:42");
    asked = lichess.channelsAsked();
    tv.repair(notice.fixes[0]);
    tv.repair(notice.fixes[0]);  // a second click: still once
    notice = *tv.channelsNotice();
    REQUIRE(notice.fixes.size() == 1);
    CHECK(notice.fixes[0].label == "Asks again in 0:42");
    CHECK(notice.fixes[0].disabled);
    const auto until = std::chrono::steady_clock::now() + 300ms;
    while (std::chrono::steady_clock::now() < until) {
        frames.draw(app, [&] { tv.drawCard(); });
        tv.poll();
    }
    CHECK(lichess.channelsAsked() == asked);
    // The pause over: asked once, and the channels come.
    app.pauses[host] = 0ms;
    lichess.refuse   = 0;
    REQUIRE(drawn([&] { return !tv.channelsNotice() && lichess.channelsAsked() == asked + 1; }));
    for (int i = 0; i < 20; ++i) {
        frames.draw(app, [&] { tv.drawCard(); });
        tv.poll();
    }
    CHECK(lichess.channelsAsked() == asked + 1);
    CHECK(app.repairs.empty());
}

TEST_CASE("Lichess TV's module without App: its requests in the host's pause end at once as it goes (#148)") {
    using namespace std::chrono_literals;
    FakeLichess lichess({});
    REQUIRE(lichess.ok());
    lichess.refuse   = 429;  // Lichess answers 429 to everything
    auto& gate       = HostGate::instance();
    const auto host  = lichess::apiHost();
    const auto pause = [&](std::chrono::milliseconds length) {
        gate.setPause(length);
        gate.tooMany(host);
    };
    pause(60s);  // its minute of pause after a 429
    const ScratchDir profile("tv-quit");
    FakeApp app(profile);
    Frames frames;
    // How long the module takes to go, its channels asked while the host is paused.
    const auto goes = [&](bool quitting) {
        auto tv = std::make_unique<LichessTvPage>(app);
        frames.draw(app, [&] { tv->drawCard(); });
        tv->poll();                            // the channels asked: the request waits for the pause
        std::this_thread::sleep_for(200ms);    // into its wait; shorter, it is cancelled before it, which ends it too
        if (quitting) app.jobs().cancelAll();  // as App's quit does first
        const auto start = std::chrono::steady_clock::now();
        tv.reset();
        return std::chrono::steady_clock::now() - start;
    };
    // The module goes (its page closed with the program): its request ends at once, not after the pause.
    CHECK(goes(false) < 5s);
    // The program quits: its flags set, then the module goes.
    CHECK(goes(true) < 5s);
    CHECK(lichess.channelsAsked() == 0);  // nothing sent during the pause
    CHECK(gate.pausedFor(host) > 50s);
    pause(50ms);  // over for what follows in this process
}
