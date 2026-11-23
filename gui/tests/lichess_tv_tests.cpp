// Lichess TV (#86): its channels and their featured games, a channel's games, the clocks as Lichess writes them, the
// user's own games left out, the mosaic's places, the game the Board goes on to, the requests against a server on this
// machine, and the game streams of an address bounded as Lichess asks.
#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include <fmt/include/fmt/format.h>
#include <json.hpp>

#include "free_ports.hpp"
#include "lichess_api.hpp"
#include "lichess_players.hpp"
#include "lichess_tv.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using namespace fcg::lichess;
using namespace fcg::test;
using namespace std::chrono_literals;

namespace {

std::string reply(int status, const std::string& body, const char* type = "application/json") {
    return fmt::format("HTTP/1.1 {} X\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", status,
                       type, body.size(), body);
}

const char* const STREAM_HEAD =
    "HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
std::string chunk(const std::string& data) { return fmt::format("{:x}\r\n{}\r\n", data.size(), data); }
const char* const LAST_CHUNK = "0\r\n\r\n";

// /api/tv/channels as Lichess answers it: camel case keys, the variants' channels among them.
const char* const CHANNELS = R"({
  "chess960": {"user": {"name": "Player_V", "id": "player_v"}, "rating": 2295, "gameId": "var00001", "color": "black"},
  "best": {"user": {"name": "Player_A", "title": "FM", "id": "player_a"}, "rating": 3205, "gameId": "game0001",
           "color": "white"},
  "computer": {"user": {"name": "Player_T", "id": "player_t"}, "rating": 1925, "gameId": "game0008", "color": "white"},
  "bullet": {"user": {"name": "Player_A", "title": "FM", "id": "player_a"}, "rating": 3205, "gameId": "game0001",
             "color": "white"},
  "atomic": {"user": {"name": "Player_W", "id": "player_w"}, "rating": 1967, "gameId": "var00002", "color": "black"},
  "bot": {"user": {"name": "Bot_1", "title": "BOT", "id": "bot_1"}, "rating": 2970, "gameId": "game0007",
          "color": "white"},
  "ultraBullet": {"user": {"name": "Player_R", "id": "player_r"}, "rating": 2242, "gameId": "game0006",
                  "color": "black"},
  "blitz": {"user": {"name": "Player_F", "title": "IM", "id": "player_f"}, "rating": 2681, "gameId": "game0003",
            "color": "black"},
  "classical": {"user": {"name": "Player_P", "id": "player_p"}, "rating": 1896, "gameId": "game0005",
                "color": "black"},
  "rapid": {"user": {"name": "Player_M", "title": "NM", "id": "player_m"}, "rating": 2290, "gameId": "game0004",
            "color": "white"}
})";

// A game of the NDJSON exports.
std::string gameLine(const std::string& id, const std::string& white, const std::string& black, int initial = 180,
                     int increment = 0, const char* variant = "standard") {
    nlohmann::json j = {{"id", id},
                        {"rated", true},
                        {"variant", variant},
                        {"speed", "blitz"},
                        {"status", "started"},
                        {"players",
                         {{"white", {{"user", {{"name", white}, {"id", white}}}, {"rating", 2900}}},
                          {"black", {{"user", {{"name", black}, {"id", black}}}, {"rating", 2880}}}}},
                        {"clock", {{"initial", initial}, {"increment", increment}, {"totalTime", initial}}}};
    for (auto* side : {&j["players"]["white"], &j["players"]["black"]}) {
        auto& u = (*side)["user"];
        auto id = u["id"].get<std::string>();
        for (auto& c : id) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        u["id"] = id;
    }
    return j.dump() + "\n";
}

TvGame game(const std::string& id, const std::string& white = "player_x", const std::string& black = "player_y") {
    TvGame g;
    g.id       = id;
    g.white.id = g.white.name = white;
    g.black.id = g.black.name = black;
    g.status                  = "started";
    return g;
}

}  // namespace

TEST_CASE("Lichess TV: the channels shown, each with its featured game") {
    const auto channels = readTvChannels(nlohmann::json::parse(CHANNELS));
    REQUIRE(channels.size() == 8);  // the variants' left out
    const char* const order[] = {"best", "bullet", "blitz", "rapid", "classical", "ultraBullet", "bot", "computer"};
    for (std::size_t i = 0; i < channels.size(); ++i) CHECK(channels[i].channel == order[i]);
    CHECK(channels[0].game == "game0001");
    CHECK(channels[0].user == "Player_A");
    CHECK(channels[0].user_id == "player_a");
    CHECK(channels[0].title == "FM");
    CHECK(channels[0].rating == 3205);
    CHECK(channels[0].white);
    CHECK_FALSE(channels[2].white);
    CHECK(channels[6].title == "BOT");
    // The older names, a channel without its game, something else.
    const auto older = readTvChannels(nlohmann::json::parse(
        R"({"Top Rated": {"user": {"name": "Player_A", "id": "player_a"}, "rating": 3000, "gameId": "game0009"},
            "UltraBullet": {"user": {"name": "Player_R"}, "rating": 2200, "gameId": "game0010", "color": "black"},
            "Bot": {"user": {"name": "Bot_2", "id": "bot_2"}, "rating": 2900},
            "Crazyhouse": {"user": {"name": "Player_Z", "id": "player_z"}, "rating": 2100, "gameId": "game0011"}})"));
    REQUIRE(older.size() == 2);
    CHECK(older[0].channel == "best");
    CHECK(older[1].channel == "ultraBullet");
    CHECK(older[1].user_id == "player_r");  // its name lowered, without an id
    CHECK(readTvChannels(nlohmann::json::array()).empty());
    CHECK(isTvChannel("computer"));
    CHECK_FALSE(isTvChannel("crazyhouse"));
}

TEST_CASE("Lichess TV: a channel's games, Lichess's AI on a side, the clock") {
    // A line each, as NDJSON has them: written here over several lines, then made one.
    const auto line  = [](const char* json) { return nlohmann::json::parse(json).dump() + "\n"; };
    const auto games = readTvGames(
        line(R"({"id": "game0001", "rated": true, "variant": "standard", "speed": "bullet", "status": "started",
                 "players": {"white": {"user": {"name": "Player_A", "title": "FM", "id": "player_a"}, "rating": 3205},
                             "black": {"user": {"name": "Player_B", "id": "player_b"}, "rating": 2938}},
                 "clock": {"initial": 60, "increment": 0, "totalTime": 60}})") +
        "\nnot json\n" +
        line(R"({"id": "game0008", "variant": {"key": "standard", "name": "Standard"}, "speed": "blitz",
                 "status": {"id": 20, "name": "started"},
                 "players": {"white": {"user": {"name": "Player_T", "id": "player_t"}, "rating": 1925},
                             "black": {"aiLevel": 8}},
                 "clock": {"initial": 300, "increment": 3}})") +
        "\r\n" +
        line(R"({"id": "game0012", "variant": "chess960", "players": {"white": {"name": "Anonymous"}, "black": {}}})"));
    REQUIRE(games.size() == 3);
    CHECK(games[0].white.name == "Player_A");
    CHECK(games[0].white.title == "FM");
    CHECK(games[0].white.rating == 3205);
    CHECK(games[0].black.id == "player_b");
    CHECK(games[0].initial == 60);
    CHECK(games[0].speed == "bullet");
    CHECK(games[0].standard());
    CHECK(games[1].black.ai == 8);
    CHECK(games[1].black.id.empty());
    CHECK(games[1].status == "started");
    CHECK(games[1].increment == 3);
    CHECK(games[2].white.name == "Anonymous");
    CHECK(games[2].initial == -1);
    CHECK_FALSE(games[2].standard());
    // As the card and the tiles name a side.
    CHECK(tvSide(games[0].white, "AI", true) == "FM Player_A (3205)");
    CHECK(tvSide(games[0].black, "AI", false) == "Player_B 2938");
    CHECK(tvSide(games[1].black, "Lichess AI, level 8", true) == "Lichess AI, level 8");
    TvPlayer bot;
    bot.name   = "Bot_1";
    bot.title  = "BOT";
    bot.rating = 2970;
    CHECK(tvSide(bot, {}, true) == "Bot_1 (2970)");  // the Bot channel's players are all bots
}

TEST_CASE("Lichess TV: the clocks as Lichess writes them") {
    CHECK(tvClockText(15, 0) == "¼+0");
    CHECK(tvClockText(30, 0) == "½+0");
    CHECK(tvClockText(45, 0) == "¾+0");
    CHECK(tvClockText(60, 0) == "1+0");
    CHECK(tvClockText(90, 1) == "1.5+1");
    CHECK(tvClockText(180, 2) == "3+2");
    CHECK(tvClockText(1800, 20) == "30+20");
    CHECK(tvClockText(-1, 0).empty());
    CHECK(tvClockText("180+0") == "3+0");
    CHECK(tvClockText("15+0") == "¼+0");
    CHECK(tvClockText("-").empty());
    CHECK(tvClockText("").empty());
    CHECK(tvClockText("x+1").empty());
}

TEST_CASE("Lichess TV: the user's own games, Lichess's AI") {
    const std::vector<std::string> own = {"rs_sample", "rs_samplebot"};
    CHECK(playedBy(game("g1", "rs_sample", "player_y"), own));
    CHECK(playedBy(game("g1", "player_x", "rs_samplebot"), own));
    CHECK_FALSE(playedBy(game("g1"), own));
    CHECK_FALSE(playedBy(game("g1", "rs_sample", "player_y"), {}));
    CHECK(playedBy("RS_Sample", "Player_Y", own));  // a PGN's names, whatever their case
    CHECK_FALSE(playedBy("Player_X", "Player_Y", own));
    CHECK(aiLevelOf("lichess AI level 8") == 8);
    CHECK(aiLevelOf("Lichess AI Level 3") == 3);
    CHECK(aiLevelOf("lichess AI level ") == 0);
    CHECK(aiLevelOf("lichess AI level 8b") == 0);
    CHECK(aiLevelOf("Player_A") == 0);
}

TEST_CASE("Lichess TV: the mosaic's places, a game that ended giving its own to another") {
    const std::vector<std::string> own = {"rs_sample"};
    std::vector<TvGame> listed         = {game("g1"), game("g2"), game("g3"), game("g4"), game("g5")};
    // The featured game first, then as Lichess lists them, as many as there is room for.
    auto places = tvPlaces({}, {}, listed, "g3", own, 3);
    CHECK(places == std::vector<std::string>{"g3", "g1", "g2"});
    // Nothing changed: the same places.
    CHECK(tvPlaces(places, {}, listed, "g3", own, 3) == places);
    // g1 ended: its result shown a moment, then g4 takes its place, the others keep theirs.
    CHECK(tvPlaces(places, {"g1"}, listed, "g3", own, 3, {"g1"}) == places);
    places = tvPlaces(places, {"g1"}, listed, "g3", own, 3);
    CHECK(places == std::vector<std::string>{"g3", "g4", "g2"});
    // g2 ended and nothing else is listed: it keeps its place, its result shown, and takes no room.
    places = tvPlaces(places, {"g1", "g2"}, {game("g2"), game("g3"), game("g4")}, "g3", own, 3);
    CHECK(places == std::vector<std::string>{"g3", "g4", "g2"});
    // More room: the next ones; never one that ended, the user's own, nor a variant's.
    listed            = {game("g1"), game("g2"), game("g3"), game("g4"), game("g6", "player_x", "rs_sample"),
                         game("g7"), game("g8")};
    listed[5].variant = "chess960";
    places            = tvPlaces({"g3", "g4"}, {"g1", "g2"}, listed, "g3", own, 4);
    CHECK(places == std::vector<std::string>{"g3", "g4", "g8"});
    // Less room: the last ones going on leave.
    CHECK(tvPlaces({"g3", "g4", "g8"}, {}, listed, "g3", own, 1) == std::vector<std::string>{"g3"});
    CHECK(tvPlaces({"g3", "g4", "g8"}, {"g4"}, listed, "g3", own, 0) == std::vector<std::string>{"g4"});
    // A game Lichess lists as ended is not taken.
    listed           = {game("g1"), game("g2")};
    listed[0].status = "mate";
    CHECK(tvPlaces({}, {}, listed, "g1", own, 2) == std::vector<std::string>{"g2"});
}

TEST_CASE("Lichess TV: the game the Board goes on to") {
    const std::vector<std::string> own = {"rs_sample"};
    TvRow row;
    row.featured.channel             = "blitz";
    row.featured.game                = "g5";
    row.featured.user_id             = "player_x";
    row.game                         = game("g5");
    row.game.initial                 = 180;
    row.game.increment               = 2;
    const std::vector<TvGame> listed = {game("g4"), game("g5"), game("g6")};
    // The featured game.
    auto next = tvNext("g1", row, listed, own);
    CHECK(next.game == "g5");
    CHECK(next.featured);
    CHECK(next.clock == "3+2");
    CHECK(next.white.name == "player_x");
    // Still the one that ended: the first listed that is not.
    next = tvNext("g5", row, listed, own);
    CHECK(next.game == "g4");
    CHECK_FALSE(next.featured);
    // The user's own featured game: never; another.
    row.game = game("g5", "player_x", "rs_sample");
    next     = tvNext("g1", row, listed, own);
    CHECK(next.game == "g4");
    // Its sides not given: taken from the games listed, when there.
    row.game = TvGame{};
    next     = tvNext("g1", row, listed, own);
    CHECK(next.game == "g5");
    CHECK(next.featured);
    CHECK(tvNext("g1", row, {}, own).game.empty());
    // Its player followed is the user: not without the channel's games.
    row.featured.user_id = "rs_sample";
    CHECK(tvNext("g1", row, {}, own).game.empty());
    CHECK(tvNext("g4", row, {game("g4"), game("g5", "rs_sample")}, own).game.empty());
}

TEST_CASE("Lichess TV: the card, a channel's games and the next game, asked of a server") {
    std::mutex mutex;
    std::vector<std::string> heads, bodies;
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto path = requestPath(head);
        // The body: what came with the head, then the rest.
        std::string body = head.substr(head.find("\r\n\r\n") + 4);
        if (const auto at = head.find("Content-Length: "); at != std::string::npos) {
            const auto n = static_cast<std::size_t>(std::stoul(head.substr(at + 16)));
            while (body.size() < n) {
                const auto more = c.readUntil("\x01never", n - body.size(), 2000);
                if (more.empty()) break;
                body += more;
            }
        }
        {
            std::lock_guard lock(mutex);
            heads.push_back(head);
            bodies.push_back(body);
        }
        if (path == "/api/tv/channels") {
            c.sendAll(reply(200, CHANNELS));
        } else if (path == "/api/games/export/_ids?moves=false") {
            std::string out;
            if (body.find("game0003") != std::string::npos)
                out += gameLine("game0003", "Player_A", "RS_Sample", 180, 2);
            if (body.find("game0001") != std::string::npos) out += gameLine("game0001", "Player_A", "Player_B", 60);
            c.sendAll(reply(200, out, "application/x-ndjson"));
        } else if (path == "/api/tv/blitz?nb=30&moves=false") {
            c.sendAll(reply(200,
                            gameLine("game0003", "Player_A", "RS_Sample") +
                                gameLine("game0013", "Player_C", "Player_D") +
                                gameLine("game0014", "Player_E", "Player_F"),
                            "application/x-ndjson"));
        } else {
            c.sendAll(reply(404, "{}"));
        }
    });
    REQUIRE(server.ok());
    servers().api = fmt::format("http://127.0.0.1:{}", port);
    // The card: the channels, then their featured games by their ids, in one request.
    const auto rows = tvRows();
    REQUIRE(rows.value);
    REQUIRE(rows.value->size() == 8);
    CHECK((*rows.value)[0].game.id == "game0001");
    CHECK((*rows.value)[0].game.black.name == "Player_B");
    CHECK((*rows.value)[2].game.black.id == "rs_sample");
    CHECK((*rows.value)[3].game.id.empty());  // not given: the card says what the channels said
    {
        std::lock_guard lock(mutex);
        REQUIRE(heads.size() == 2);
        CHECK(heads[1].rfind("POST /api/games/export/_ids?moves=false ", 0) == 0);
        CHECK(heads[1].find("Accept: application/x-ndjson") != std::string::npos);
        CHECK(heads[1].find("Authorization") == std::string::npos);  // no account
        // Each featured game once, separated by commas.
        CHECK(bodies[1] == "game0001,game0003,game0004,game0005,game0006,game0007,game0008");
    }
    // A channel's games: its featured game, then thirty at most, without their moves.
    const auto list = tvList("blitz");
    REQUIRE(list.value);
    CHECK(list.value->featured == "game0003");
    CHECK(list.value->games.size() == 3);
    {
        std::lock_guard lock(mutex);
        CHECK(heads.back().rfind("GET /api/tv/blitz?nb=30&moves=false ", 0) == 0);
        CHECK(heads.back().find("Authorization") == std::string::npos);
    }
    // The next game of the Board: the featured one is the user's, so the channel's first other.
    auto next = askTvNext("blitz", "game0010", {"rs_sample"}, false);
    REQUIRE(next.value);
    CHECK(next.value->game == "game0013");
    CHECK_FALSE(next.value->featured);
    // Lichess still names the game that ended: none yet, the channel's games not asked; then another of them.
    const auto asked = [&] {
        std::lock_guard lock(mutex);
        return heads.size();
    };
    const auto before = asked();
    next              = askTvNext("blitz", "game0003", {}, false);
    REQUIRE(next.value);
    CHECK(next.value->game.empty());
    CHECK(asked() == before + 2);  // the channels, the featured games
    next = askTvNext("blitz", "game0003", {}, true);
    REQUIRE(next.value);
    CHECK(next.value->game == "game0013");
    // Lichess not there: said.
    servers().api = fmt::format("http://127.0.0.1:{}", freePort());
    CHECK_FALSE(tvRows().value);
    CHECK_FALSE(tvList("blitz").error.empty());
    servers() = Servers{};
}

TEST_CASE("Lichess TV: the games of an address followed a few at a time, as Lichess allows") {
    // Three games whose streams stay open until the test ends them, two at most at once.
    std::map<std::string, std::atomic<bool>> ended;
    for (const auto* id : {"game0021", "game0022", "game0023"}) ended[id] = false;
    std::atomic<int> open{0}, most{0};
    std::atomic<bool> quit{false};
    std::mutex mutex;
    std::vector<std::string> exported;
    const auto pgn = [](const std::string& id, bool over) {
        return fmt::format(
            "[Event \"rated blitz game\"]\n[Site \"https://lichess.org/game/{}\"]\n[White \"Player_A\"]\n"
            "[Black \"Player_B\"]\n[Result \"{}\"]\n\n1. e4 e5 {}\n",
            id, over ? "1-0" : "*", over ? "2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7# 1-0" : "*");
    };
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto path = requestPath(head);
        const auto id   = path.substr(path.rfind('/') + 1).substr(0, 8);
        if (path.rfind("/game/export/", 0) == 0 && ended.count(id)) {
            {
                std::lock_guard lock(mutex);
                exported.push_back(id);
            }
            c.sendAll(reply(200, pgn(id, ended.at(id)), "application/x-chess-pgn"));
        } else if (path.rfind("/api/stream/game/", 0) == 0 && ended.count(id)) {
            const int now = ++open;
            for (int seen = most.load(); now > seen && !most.compare_exchange_weak(seen, now);) {
            }
            c.sendAll(std::string(STREAM_HEAD) +
                      chunk(fmt::format(R"({{"id": "{}", "status": {{"id": 20, "name": "started"}}}})", id) + "\n"));
            while (!ended.at(id) && !quit) {
                if (!c.sendAll(chunk("\n"), 500)) break;  // the follower left
                (void)c.readUntil("\x01never", 1, 20);
            }
            if (ended.at(id))
                c.sendAll(chunk(fmt::format(R"({{"id": "{}", "status": {{"id": 30, "name": "mate"}}}})", id) + "\n") +
                          LAST_CHUNK);
            --open;
        } else {
            c.sendAll(reply(404, "{}"));
        }
    });
    REQUIRE(server.ok());
    servers().api = fmt::format("http://127.0.0.1:{}", port);
    {
        PlayersFollower follower;
        follower.watch({"game0021", "game0022", "game0023"}, 2);
        REQUIRE(waitUntil([&] { return open == 2; }));
        CHECK(follower.open() == 2);
        {
            std::lock_guard lock(mutex);
            CHECK(std::find(exported.begin(), exported.end(), "game0023") == exported.end());  // waiting for a place
        }
        // One ends: asked whole again, over, its place to the third.
        ended.at("game0021") = true;
        REQUIRE(waitUntil([&] {
            const auto games = follower.games();
            return std::any_of(games.begin(), games.end(),
                               [](const auto& g) { return g.url == "https://lichess.org/game0023"; });
        }));
        REQUIRE(waitUntil([&] { return open == 2; }));
        CHECK(most == 2);
        CHECK(follower.over().count("game0021") == 1);
        const auto games = follower.games();
        const auto first = std::find_if(games.begin(), games.end(),
                                        [](const auto& g) { return g.url == "https://lichess.org/game0021"; });
        REQUIRE(first != games.end());
        CHECK(first->result == "1-0");
        // None any more: the streams close, the game that ended stays.
        follower.watch({}, 0);
        REQUIRE(waitUntil([&] { return open == 0; }));
        CHECK(follower.open() == 0);
        CHECK(most == 2);
    }
    quit      = true;
    servers() = Servers{};
}
