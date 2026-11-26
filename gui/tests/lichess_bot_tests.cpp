// A Lichess bot for one of the user's engines (#85): the account's checks before any request, the upgrade, the
// challenges its rules decline and Lichess's reason for each, the clocks given to its engine, and a game played by the
// mock engine against a server on this machine through the Bot API (its words, a draw offer and a takeback declined,
// a cut, its end), an engine that cannot start, and what the profile keeps.
#include <doctest/doctest.hpp>

#include <atomic>
#include <condition_variable>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <chess.hpp>
#include <json.hpp>

#include "free_ports.hpp"
#include "https.hpp"
#include "lichess_api.hpp"
#include "lichess_bot.hpp"
#include "mock_engines.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using namespace fcg::lichess;
using namespace fcg::test;
using namespace std::chrono_literals;

namespace {

using fcg::test::randomMover;

struct Seen {
    std::string line, path, authorization, body;
};

Seen readRequest(const std::string& head, net::TcpConnection& c) {
    Seen s;
    s.line            = head.substr(0, head.find("\r\n"));
    s.path            = requestPath(head);
    s.authorization   = httpHeader(head, "Authorization").value_or("");
    const auto length = std::stoul(httpHeader(head, "Content-Length").value_or("0"));
    s.body            = head.substr(head.find("\r\n\r\n") + 4);
    while (s.body.size() < length) {
        const auto more = c.readUntil("\x01never", length - s.body.size(), 2000);
        if (more.empty()) break;
        s.body += more;
    }
    return s;
}

std::string reply(int status, const std::string& body) {
    return fmt::format(
        "HTTP/1.1 {} X\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", status,
        body.size(), body);
}

const char* const STREAM_HEAD =
    "HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
std::string chunk(const std::string& data) { return fmt::format("{:x}\r\n{}\r\n", data.size(), data); }
const char* const LAST_CHUNK = "0\r\n\r\n";

Challenge challenge(std::string speed, ChallengeClock clock, bool rated = false, std::string variant = "standard") {
    Challenge c;
    c.id                = "cHal0001";
    c.challenger        = "Player_F";
    c.challenger_rating = 1790;
    c.speed             = std::move(speed);
    c.clock             = clock;
    c.rated             = rated;
    c.variant           = std::move(variant);
    return c;
}

std::string today() {
    const auto now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[16];
    std::strftime(text, sizeof(text), "%Y-%m-%d", &local);
    return text;
}

// A game of the bot's as Lichess would hold it: the bot has White against "Partner_3", who answers each move with its
// first legal one; the stream tells each state; Partner_3 offers a draw after the bot's first move and asks a
// takeback after its second; the game ends, Partner_3 resigning, after the bot's third move. The first stream is cut
// after the bot's first move, without the game's end.
struct FakeGame {
    std::mutex mutex;
    std::condition_variable cv;
    chess::Board board;
    std::vector<std::string> moves;
    std::vector<std::string> lines;  // the states told, in order
    std::vector<Seen> seen;
    std::vector<std::string> chat;  // what the bot said
    int refused = 0;
    int streams = 0;
    bool draw = false, takeback = false, ended = false;
    std::atomic<bool> stop{false};

    std::string state() const {
        nlohmann::json j{{"type", "gameState"},
                         {"moves", ""},
                         {"wtime", 178000},
                         {"btime", 179000},
                         {"winc", 2000},
                         {"binc", 2000},
                         {"status", ended ? "resign" : "started"}};
        std::string text;
        for (const auto& m : moves) text += (text.empty() ? "" : " ") + m;
        j["moves"] = text;
        if (ended) j["winner"] = "white";
        if (draw) j["bdraw"] = true;
        if (takeback) j["btakeback"] = true;
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
    // The bot's move taken, then Partner_3's.
    bool play(const std::string& uci) {
        std::lock_guard lock(mutex);
        if (ended || board.sideToMove() != chess::Color::WHITE) return false;
        chess::Movelist legal;
        chess::movegen::legalmoves(legal, board);
        const auto m = chess::uci::uciToMove(board, uci);
        if (std::find(legal.begin(), legal.end(), m) == legal.end()) return false;
        board.makeMove(m);
        moves.push_back(uci);
        draw = takeback = false;
        lines.push_back(state());
        const auto bot_moves = (moves.size() + 1) / 2;
        if (bot_moves >= 3) {
            ended = true;
            lines.push_back(state());
        } else {
            chess::Movelist answers;
            chess::movegen::legalmoves(answers, board);
            board.makeMove(answers[0]);
            moves.push_back(chess::uci::moveToUci(answers[0]));
            draw     = bot_moves == 1;
            takeback = bot_moves == 2;
            lines.push_back(state());
        }
        cv.notify_all();
        return true;
    }
};

}  // namespace

TEST_CASE("Lichess bot: an account's checks before anything is asked of it") {
    const auto facts = readBotAccount(nlohmann::json::parse(R"({"id": "rs_samplebot", "username": "RS_SampleBot",
        "createdAt": 1795392000000, "count": {"all": 0, "rated": 0}, "perfs": {}})"));
    REQUIRE(facts);
    CHECK(facts->username == "RS_SampleBot");
    CHECK(facts->created_ms == 1795392000000);
    CHECK(facts->games == 0);
    CHECK_FALSE(facts->bot());
    CHECK(botVerdict(*facts, "RS_Sample", true) == BotVerdict::Upgradable);
    CHECK(botVerdict(*facts, "", true) == BotVerdict::Upgradable);  // no account of the user's linked
    CHECK(botVerdict(*facts, "rs_samplebot", true) == BotVerdict::OwnAccount);
    CHECK(botVerdict(*facts, "RS_Sample", false) == BotVerdict::NoScope);
    auto played  = *facts;
    played.games = 3;
    CHECK(botVerdict(played, "RS_Sample", true) == BotVerdict::Played);
    auto bot  = played;
    bot.title = "BOT";
    CHECK(botVerdict(bot, "RS_Sample", true) == BotVerdict::Ready);  // its games were a bot's
    auto closed     = *facts;
    closed.disabled = true;
    CHECK(botVerdict(closed, "RS_Sample", true) == BotVerdict::Closed);
    auto marked          = *facts;
    marked.tos_violation = true;
    CHECK(botVerdict(marked, "RS_Sample", true) == BotVerdict::Closed);
    // The user's own account comes first, whatever else it is.
    CHECK(botVerdict(bot, "RS_SampleBot", true) == BotVerdict::OwnAccount);
    CHECK_FALSE(readBotAccount(nlohmann::json::parse(R"({"id": "x"})")));
}

TEST_CASE("Lichess bot: the challenges its rules decline, with Lichess's reason") {
    const BotRules rules;  // at first: casual, blitz and rapid, people and bots, any rating, no Chess960
    BotSituation now;
    now.own_account = "RS_Sample";
    CHECK(declineReason(challenge("blitz", {3, 2, 0}), rules, now).empty());
    CHECK(declineReason(challenge("rapid", {10, 5, 0}), rules, now).empty());
    CHECK(declineReason(challenge("ultraBullet", {0, 0, 0}), rules, now) == "tooFast");
    CHECK(declineReason(challenge("bullet", {1, 0, 0}), rules, now) == "tooFast");
    CHECK(declineReason(challenge("classical", {30, 0, 0}), rules, now) == "tooSlow");
    CHECK(declineReason(challenge("correspondence", {0, 0, 3}), rules, now) == "tooSlow");
    auto no_clock      = challenge("correspondence", {0, 0, 0});
    no_clock.unlimited = true;
    CHECK(declineReason(no_clock, rules, now) == "timeControl");
    // Between two speeds taken, one that is not.
    auto gap      = rules;
    gap.rapid     = false;
    gap.classical = true;
    CHECK(declineReason(challenge("rapid", {10, 0, 0}), gap, now) == "timeControl");
    // Rated: casual only while testing; never rated against the user's own account.
    CHECK(declineReason(challenge("blitz", {3, 2, 0}, true), rules, now) == "casual");
    auto rated  = rules;
    rated.rated = true;
    CHECK(declineReason(challenge("blitz", {3, 2, 0}, true), rated, now).empty());
    auto own       = challenge("blitz", {3, 2, 0}, true);
    own.challenger = "rs_sample";
    CHECK(declineReason(own, rated, now) == "casual");
    own.rated = false;
    CHECK(declineReason(own, rated, now).empty());  // a casual game against its owner
    // Variants: standard and from a position; Chess960 when the rules take it and the engine knows it.
    CHECK(declineReason(challenge("blitz", {3, 2, 0}, false, "fromPosition"), rules, now).empty());
    CHECK(declineReason(challenge("blitz", {3, 2, 0}, false, "chess960"), rules, now) == "standard");
    auto c960     = rules;
    c960.chess960 = true;
    CHECK(declineReason(challenge("blitz", {3, 2, 0}, false, "chess960"), c960, now) == "standard");
    now.engine_chess960 = true;
    CHECK(declineReason(challenge("blitz", {3, 2, 0}, false, "chess960"), c960, now).empty());
    CHECK(declineReason(challenge("blitz", {3, 2, 0}, false, "atomic"), c960, now) == "variant");
    // People, bots, ratings.
    auto bot             = challenge("blitz", {3, 2, 0});
    bot.challenger_title = "BOT";
    auto people          = rules;
    people.bots          = false;
    CHECK(declineReason(bot, people, now) == "noBot");
    auto bots   = rules;
    bots.humans = false;
    CHECK(declineReason(challenge("blitz", {3, 2, 0}), bots, now) == "onlyBot");
    CHECK(declineReason(bot, bots, now).empty());
    auto range       = rules;
    range.rating_min = 1800;
    CHECK(declineReason(challenge("blitz", {3, 2, 0}), range, now) == "generic");
    range.rating_min = 1500;
    range.rating_max = 1700;
    CHECK(declineReason(challenge("blitz", {3, 2, 0}), range, now) == "generic");
    range.rating_max = 2600;
    CHECK(declineReason(challenge("blitz", {3, 2, 0}), range, now).empty());
    // What it cannot take now: "later", after what the challenge itself asks.
    auto busy = now;
    busy.busy = true;
    CHECK(declineReason(challenge("blitz", {3, 2, 0}), rules, busy) == "later");
    CHECK(declineReason(challenge("bullet", {1, 0, 0}), rules, busy) == "tooFast");
    auto closing    = now;
    closing.closing = true;
    CHECK(declineReason(challenge("blitz", {3, 2, 0}), rules, closing) == "later");
    auto tournament         = now;
    tournament.cores_needed = true;
    CHECK(declineReason(challenge("rapid", {10, 5, 0}), rules, tournament) == "later");
    // The speed computed from the clock when the challenge does not say it.
    CHECK(declineReason(challenge("", {1, 0, 0}), rules, now) == "tooFast");
    CHECK(declineReason(challenge("", {5, 3, 0}), rules, now).empty());
    BotRules nothing;
    nothing.blitz = nothing.rapid = false;
    CHECK(declineReason(challenge("blitz", {3, 2, 0}), nothing, now) == "timeControl");
}

TEST_CASE("Lichess bot: the clocks given to its engine, less a margin for the network") {
    CHECK(clockMargin(180000) == 1000);
    CHECK(clockMargin(5000) == 500);
    CHECK(clockMargin(1000) == 200);
    BoardState s;
    s.full         = true;
    s.speed        = "blitz";
    s.clock_ms     = 180000;
    s.increment_ms = 2000;
    s.moves        = {"e2e4", "e7e5"};
    s.white_ms     = 170000;
    s.black_ms     = 175000;
    s.told         = std::chrono::steady_clock::now();
    // White to move: its clock runs from when it was told.
    CHECK(botGo(s, true, s.told) == "go wtime 169000 btime 175000 winc 2000 binc 2000");
    CHECK(botGo(s, true, s.told + 3s) == "go wtime 166000 btime 175000 winc 2000 binc 2000");
    CHECK(botBudget(s, true, s.told).count() == 169500);
    // Black's view of the same position: White's clock is the one running.
    s.moves.push_back("g1f3");
    CHECK(botGo(s, false, s.told) == "go wtime 170000 btime 174000 winc 2000 binc 2000");
    s.black_ms = 400;
    CHECK(botGo(s, false, s.told) == "go wtime 170000 btime 200 winc 2000 binc 2000");
    // Before both sides moved: a short move time within the first move's limit.
    s.moves         = {"e2e4"};
    s.first_move_ms = 20000;
    CHECK(botGo(s, false, s.told) == "go movetime 2000");
    s.first_move_ms = 2000;
    CHECK(botGo(s, false, s.told) == "go movetime 500");
    // By correspondence: a move time.
    s.speed = "correspondence";
    s.days  = 3;
    CHECK(botGo(s, true, s.told) == "go movetime 15000");
}

TEST_CASE("Lichess bot: the account asked, the upgrade, Lichess's refusal") {
    std::mutex mutex;
    std::vector<Seen> seen;
    std::atomic<int> upgrades{0};
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto s = readRequest(head, c);
        {
            std::lock_guard lock(mutex);
            seen.push_back(s);
        }
        if (s.path == "/api/account") {
            c.sendAll(reply(200, R"({"id": "rs_samplebot", "username": "RS_SampleBot", "createdAt": 1795392000000,
                "count": {"all": 0}})"));
        } else if (s.path == "/api/bot/account/upgrade") {
            c.sendAll(++upgrades == 1
                          ? reply(200, R"({"ok": true})")
                          : reply(400, R"({"error": "You already have games played. Make a new account."})"));
        } else {
            c.sendAll(reply(404, "{}"));
        }
    });
    REQUIRE(server.ok());
    servers().api    = fmt::format("http://127.0.0.1:{}", port);
    const auto facts = botAccount("bot-token");
    REQUIRE(facts.value);
    CHECK(facts.value->id == "rs_samplebot");
    CHECK(botVerdict(*facts.value, "RS_Sample", true) == BotVerdict::Upgradable);
    const auto done = upgradeToBot("bot-token");
    CHECK(done.ok);
    const auto refused = upgradeToBot("bot-token");
    CHECK_FALSE(refused.ok);
    CHECK(refused.status == 400);
    CHECK(refused.net == NetError::Server);
    CHECK(describe(refused) == "You already have games played. Make a new account.");
    servers() = Servers{};
    std::lock_guard lock(mutex);
    REQUIRE(seen.size() == 3);
    CHECK(seen[0].line == "GET /api/account HTTP/1.1");
    CHECK(seen[0].authorization == "Bearer bot-token");
    CHECK(seen[1].line == "POST /api/bot/account/upgrade HTTP/1.1");
    CHECK(seen[1].authorization == "Bearer bot-token");
}

TEST_CASE("Lichess bot: a game played by the engine through the Bot API, its words, a draw and a takeback declined") {
    FakeGame game;
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto s = readRequest(head, c);
        {
            std::lock_guard lock(game.mutex);
            game.seen.push_back(s);
        }
        const std::string base = "/api/bot/game/gBot0001";
        if (s.path == base + "/chat" && s.line.rfind("GET", 0) == 0) {
            c.sendAll(reply(200, "[]"));
            return;
        }
        if (s.path == base + "/chat") {
            std::lock_guard lock(game.mutex);
            const auto text = s.body.substr(s.body.find("text=") + 5);
            game.chat.push_back(text);
            c.sendAll(reply(200, R"({"ok": true})"));
            return;
        }
        if (s.path.rfind(base + "/move/", 0) == 0) {
            const bool ok = game.play(s.path.substr(base.size() + 6));
            if (!ok) {
                std::lock_guard lock(game.mutex);
                ++game.refused;
            }
            c.sendAll(ok ? reply(200, R"({"ok": true})") : reply(400, R"({"error": "Not your turn"})"));
            return;
        }
        if (s.path == base + "/draw/no" || s.path == base + "/takeback/no") {
            std::lock_guard lock(game.mutex);
            (s.path.find("draw") != std::string::npos ? game.draw : game.takeback) = false;
            c.sendAll(reply(200, R"({"ok": true})"));
            return;
        }
        if (s.path != "/api/bot/game/stream/gBot0001") {
            c.sendAll(reply(404, R"({"error": "Not found"})"));
            return;
        }
        std::unique_lock lock(game.mutex);
        const int n = ++game.streams;
        c.sendAll(STREAM_HEAD);
        c.sendAll(chunk(game.full()));
        std::size_t sent = game.lines.size();
        while (!game.stop) {
            game.cv.wait_for(lock, 50ms, [&] { return game.lines.size() > sent || game.stop; });
            while (sent < game.lines.size()) c.sendAll(chunk(game.lines[sent++]));
            if (game.ended) {
                c.sendAll(LAST_CHUNK);
                return;
            }
            // The first stream cut once the bot played its first move and Partner_3 answered.
            if (n == 1 && game.moves.size() >= 2) return;
        }
    });
    REQUIRE(server.ok());
    servers().api = fmt::format("http://127.0.0.1:{}", port);
    BotPlay play;
    play.engine.name     = "Mock";
    play.engine.cmd      = randomMover();
    play.engine.protocol = "uci";
    play.greeting        = "Good game!";
    play.thanks          = "Thank you for the game!";
    {
        BotGame bot("bot-token", "gBot0001", "RS_SampleBot", true, play);
        BotGame::State st;
        uint64_t version = 0;
        CHECK(waitUntil(
            [&] {
                (void)bot.state(st, version);
                return st.done;
            },
            60s));
        game.stop = true;
        game.cv.notify_all();
        CHECK(st.white);
        CHECK(st.game.status == "resign");
        CHECK(st.game.moves.size() == 5);
        CHECK(st.error.empty());
        CHECK(st.info.find("score cp 0") != std::string::npos);  // the engine's last search
    }
    servers() = Servers{};
    std::lock_guard lock(game.mutex);
    CHECK(game.refused == 0);  // no move sent twice, none out of turn, after the cut too
    CHECK(game.streams == 2);
    REQUIRE(game.chat.size() == 2);
    CHECK(game.chat[0] == "Good+game%21");
    CHECK(game.chat[1] == "Thank+you+for+the+game%21");
    // In order: the greeting before the first move, the draw declined before the second, the takeback after it.
    std::vector<std::string> order;
    for (const auto& s : game.seen) {
        if (s.line.rfind("POST", 0) != 0) continue;
        const auto rest = s.path.substr(std::string("/api/bot/game/gBot0001/").size());
        order.push_back(rest.rfind("move/", 0) == 0 ? "move" : rest);
        CHECK(s.authorization == "Bearer bot-token");
    }
    const std::vector<std::string> expected = {"chat", "move", "draw/no", "move", "takeback/no", "move", "chat"};
    CHECK(order == expected);
}

TEST_CASE("Lichess bot: an engine that cannot start aborts the game before both sides moved") {
    std::mutex mutex;
    std::vector<std::string> posts;
    std::atomic<bool> stop{false};
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto s = readRequest(head, c);
        if (s.line.rfind("POST", 0) == 0) {
            std::lock_guard lock(mutex);
            posts.push_back(s.path);
            c.sendAll(reply(200, R"({"ok": true})"));
            return;
        }
        if (s.path != "/api/bot/game/stream/gBot0002") {
            c.sendAll(reply(200, "[]"));
            return;
        }
        c.sendAll(STREAM_HEAD);
        c.sendAll(chunk(nlohmann::json::parse(R"({"type": "gameFull", "id": "gBot0002", "variant": {"key": "standard"},
            "speed": "blitz", "rated": false, "white": {"id": "rs_samplebot", "name": "RS_SampleBot"},
            "black": {"id": "partner_3", "name": "Partner_3"}, "initialFen": "startpos",
            "clock": {"initial": 180000, "increment": 2000},
            "state": {"type": "gameState", "moves": "", "wtime": 180000, "btime": 180000, "status": "started"}})")
                            .dump() +
                        "\n"));
        while (!stop) {
            if (!c.sendAll(chunk("\n"), 500)) return;
            std::this_thread::sleep_for(20ms);
        }
    });
    REQUIRE(server.ok());
    servers().api = fmt::format("http://127.0.0.1:{}", port);
    BotPlay play;
    play.engine.name = "Missing";
    play.engine.cmd  = (std::filesystem::temp_directory_path() / "no-such-engine").string();
    {
        BotGame bot("bot-token", "gBot0002", "RS_SampleBot", true, play);
        BotGame::State st;
        uint64_t version = 0;
        CHECK(waitUntil([&] {
            (void)bot.state(st, version);
            std::lock_guard lock(mutex);
            return !posts.empty() && !st.error.empty();
        }));
        CHECK(st.error.find("Missing") != std::string::npos);
        stop = true;
    }
    servers() = Servers{};
    std::lock_guard lock(mutex);
    REQUIRE_FALSE(posts.empty());
    CHECK(posts[0] == "/api/bot/game/gBot0002/abort");
}

TEST_CASE("Lichess bot: the victory claimed once the opponent left long enough") {
    std::mutex mutex;
    std::vector<std::string> posts;
    std::atomic<bool> claimed{false};
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto s = readRequest(head, c);
        if (s.line.rfind("POST", 0) == 0) {
            {
                std::lock_guard lock(mutex);
                posts.push_back(s.path);
            }
            if (s.path == "/api/bot/game/gBot0003/claim-victory") claimed = true;
            c.sendAll(reply(200, R"({"ok": true})"));
            return;
        }
        if (s.path != "/api/bot/game/stream/gBot0003") {
            c.sendAll(reply(200, "[]"));
            return;
        }
        // The bot has White and has moved; its opponent leaves, Lichess lets the bot claim in a second.
        c.sendAll(STREAM_HEAD);
        c.sendAll(chunk(nlohmann::json::parse(R"({"type": "gameFull", "id": "gBot0003", "variant": {"key": "standard"},
            "speed": "rapid", "rated": false, "white": {"id": "rs_samplebot", "name": "RS_SampleBot"},
            "black": {"id": "partner_3", "name": "Partner_3"}, "initialFen": "startpos",
            "clock": {"initial": 600000, "increment": 5000},
            "state": {"type": "gameState", "moves": "e2e4", "wtime": 600000, "btime": 600000, "status": "started"}})")
                            .dump() +
                        "\n"));
        c.sendAll(chunk(R"({"type": "opponentGone", "gone": true, "claimWinInSeconds": 1})"
                        "\n"));
        (void)waitUntil([&] { return claimed.load(); }, 10s);
        c.sendAll(chunk(R"({"type": "gameState", "moves": "e2e4", "wtime": 590000, "btime": 600000,)"
                        R"( "status": "timeout", "winner": "white"})"
                        "\n") +
                  LAST_CHUNK);
    });
    REQUIRE(server.ok());
    servers().api = fmt::format("http://127.0.0.1:{}", port);
    BotPlay play;
    play.engine.name = "Mock";
    play.engine.cmd  = randomMover();
    play.thanks      = "Thank you for the game!";
    {
        BotGame bot("bot-token", "gBot0003", "RS_SampleBot", true, play);
        BotGame::State st;
        uint64_t version = 0;
        CHECK(waitUntil([&] {
            (void)bot.state(st, version);
            return st.done;
        }));
        CHECK(st.game.status == "timeout");
        CHECK(st.game.winner == "white");
    }
    servers() = Servers{};
    std::lock_guard lock(mutex);
    REQUIRE(posts.size() == 2);
    CHECK(posts[0] == "/api/bot/game/gBot0003/claim-victory");
    CHECK(posts[1] == "/api/bot/game/gBot0003/chat");  // its thanks
}

TEST_CASE("Lichess bot: what the profile keeps, today's games and challenges declined") {
    const auto dir = std::filesystem::temp_directory_path() / "fcg-lichess-bot";
    std::filesystem::remove_all(dir);
    CHECK(readBotSettings(dir).engine.empty());  // nothing kept yet
    CHECK(readBotSettings(dir).rules == BotRules{});
    BotSettings s;
    s.engine           = "Gaia 4.4.3";
    s.rules.classical  = true;
    s.rules.rated      = true;
    s.rules.rating_min = 1500;
    s.book             = "/books/gaia-book.bin";
    s.greeting         = "Good game!";
    s.upgraded_at      = 1795392000;
    const auto now     = static_cast<int64_t>(std::time(nullptr));
    const auto day     = today();
    addBotRecord(s, {"g1", "Partner_3", 1836, false, "blitz", "3+2", false, true, "1-0", 77, now}, day);
    addBotRecord(s, {"g2", "Bot_2", 2410, true, "blitz", "3+0", false, false, "1-0", 90, now}, day);
    addBotRecord(s, {"g3", "Player_F", 1790, false, "rapid", "10+5", false, true, "1/2-1/2", 120, now}, day);
    addBotRecord(s, {"g4", "Player_B", 1790, false, "rapid", "10+5", false, true, "*", 1, now}, day);  // aborted
    addBotRecord(s, {"g0", "Old", 1700, false, "rapid", "10+5", false, true, "0-1", 50, now - 3 * 86400}, day);
    countDeclined(s, day);
    countDeclined(s, day);
    REQUIRE(writeBotSettings(dir, s));
    const auto back = readBotSettings(dir);
    CHECK(back.engine == "Gaia 4.4.3");
    CHECK(back.rules.classical);
    CHECK(back.rules.rating_min == 1500);
    CHECK(back.book == "/books/gaia-book.bin");
    CHECK(back.upgraded_at == 1795392000);
    REQUIRE(back.games.size() == 5);
    CHECK(back.games[0].id == "g0");  // the latest added first
    CHECK(back.declined == 2);
    const auto d = botDay(back, day);
    CHECK(d.games == 3);
    CHECK(d.won == 1);
    CHECK(d.drawn == 1);
    CHECK(d.lost == 1);  // Bot_2 won with White against the bot's Black
    // Another day: its declined count starts again.
    auto next = back;
    countDeclined(next, "2999-01-01");
    CHECK(next.declined == 1);
    // At most 20 games kept, the latest first; one added again replaces itself.
    for (int i = 0; i < 25; ++i)
        addBotRecord(next, {fmt::format("x{}", i), "P", 1500, false, "blitz", "3+2", false, true, "1-0", 9, now},
                     "2999-01-01");
    CHECK(next.games.size() == 20);
    CHECK(next.games[0].id == "x24");
    addBotRecord(next, {"x20", "P", 1500, false, "blitz", "3+2", false, true, "0-1", 9, now}, "2999-01-01");
    CHECK(next.games.size() == 20);
    CHECK(next.games[0].result == "0-1");
    std::filesystem::remove_all(dir);
}
