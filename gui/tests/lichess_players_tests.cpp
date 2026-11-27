// Chosen players' games followed live (#50): their status, whom the account follows, a game's moves added as its
// stream sends them, and the follower against a server on this machine.
#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <fmt/include/fmt/format.h>
#include <json.hpp>

#include "answering_server.hpp"
#include "lichess_api.hpp"
#include "lichess_players.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using namespace fcg::lichess;
using namespace fcg::test;
using namespace std::chrono_literals;

namespace {

const char* const ONGOING = R"([Event "Rated blitz game"]
[Site "https://lichess.org/game0001"]
[White "Partner_1"]
[Black "Player_C"]
[Result "*"]
[WhiteElo "2010"]
[BlackElo "2188"]

1. e4 { [%clk 0:03:00] } 1... e5 { [%clk 0:03:00] } *
)";

const char* const FINISHED = R"([Event "Rated blitz game"]
[Site "https://lichess.org/game0001"]
[White "Partner_1"]
[Black "Player_C"]
[Result "1-0"]
[WhiteElo "2010"]
[BlackElo "2188"]

1. e4 1... e5 2. Qh5 2... Nc6 3. Bc4 3... Nf6 4. Qxf7# 1-0
)";

std::string httpOk(const std::string& type, const std::string& body) {
    return fmt::format("HTTP/1.1 200 OK\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", type,
                       body.size(), body);
}

}  // namespace

TEST_CASE("Lichess players: their status, playing a game or not") {
    const auto statuses = readStatuses(nlohmann::json::parse(R"([
        {"id": "partner_1", "name": "Partner_1", "online": true, "playing": {"id": "game0001", "clock": "3+2",
         "variant": "standard"}},
        {"id": "player_c", "name": "Player_C", "online": true, "playing": true, "playingId": "game0002"},
        {"id": "partner_2", "name": "Partner_2", "online": true},
        {"name": "Player_D"},
        {"id": "player_e", "playing": {"id": "game0003", "clock": {"initial": 600, "increment": 5}}}
    ])"));
    REQUIRE(statuses.size() == 5);
    CHECK(statuses[0].playing);
    CHECK(statuses[0].game == "game0001");
    CHECK(statuses[0].clock == "3+2");
    CHECK(statuses[1].game == "game0002");
    CHECK_FALSE(statuses[2].playing);
    CHECK(statuses[2].online);
    CHECK(statuses[3].id == "player_d");  // the name lowered
    CHECK_FALSE(statuses[3].online);
    CHECK(statuses[4].clock == "10+5");
    CHECK(statuses[4].name == "player_e");
    CHECK(readStatuses(nlohmann::json::object()).empty());
}

TEST_CASE("Lichess players: whom the account follows, a user a line") {
    const auto names = readFollowing(
        "{\"id\": \"partner_1\", \"username\": \"Partner_1\"}\n\n"
        "{\"id\": \"player_c\", \"username\": \"Player_C\"}\r\nnot json\n"
        "{\"id\": \"player_d\"}");
    REQUIRE(names.size() == 3);
    CHECK(names[0] == "Partner_1");
    CHECK(names[1] == "Player_C");
    CHECK(names[2] == "player_d");
}

TEST_CASE("Lichess players: a move of a game's stream added to its PGN") {
    std::string pgn = ONGOING;
    // 2. Qh5, its clock after it.
    CHECK(addStreamMove(pgn, nlohmann::json::parse(
                                 R"({"fen": "rnbqkbnr/pppp1ppp/8/4p2Q/4P3/8/PPPP1PPP/RNB1KBNR b KQkq - 1 2",
                                     "lm": "d1h5", "wc": 170, "bc": 178})")));
    CHECK(pgn.find("2. Qh5 { [%clk 0:02:50] } *") != std::string::npos);
    const auto once = pgn;
    // Sent again: it has it.
    CHECK(addStreamMove(pgn, nlohmann::json::parse(
                                 R"({"fen": "rnbqkbnr/pppp1ppp/8/4p2Q/4P3/8/PPPP1PPP/RNB1KBNR b KQkq - 1 2",
                                     "lm": "d1h5"})")));
    CHECK(pgn == once);
    // Black's answer, its FEN without the rest, as a stream may send it.
    CHECK(addStreamMove(pgn, nlohmann::json::parse(
                                 R"({"fen": "r1bqkbnr/pppp1ppp/2n5/4p2Q/4P3/8/PPPP1PPP/RNB1KBNR", "lm": "b8c6",
                                     "wc": 170, "bc": 175})")));
    CHECK(pgn.find("2... Nc6 { [%clk 0:02:55] } *") != std::string::npos);
    // A move that does not follow from it: the game is asked whole again.
    const auto before = pgn;
    CHECK_FALSE(addStreamMove(pgn, nlohmann::json::parse(R"({"fen": "8/8/8/8/8/8/8/8 w - - 0 1", "lm": "e2e4"})")));
    CHECK(pgn == before);
    CHECK(streamEnded(nlohmann::json::parse(R"({"id": "game0001", "status": {"id": 30, "name": "mate"}})")));
    CHECK_FALSE(streamEnded(nlohmann::json::parse(R"({"id": "game0001", "status": {"id": 20, "name": "started"}})")));
    CHECK_FALSE(streamEnded(nlohmann::json::parse(R"({"fen": "8/8/8/8/8/8/8/8", "lm": "e2e4"})")));
}

TEST_CASE("Lichess players: a game's stream that sends the game again from its start") {
    // Lichess sends the game again from its first move when the stream opens, then each move (#86).
    const auto event  = [](const char* json) { return nlohmann::json::parse(json); };
    std::string pgn   = ONGOING;  // 1. e4 e5, as the export gave it
    std::size_t known = 0;
    const auto before = pgn;
    CHECK(takeStreamMove(pgn, known,
                         event(R"({"fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "wc": 180,
                                   "bc": 180})")));
    CHECK(known == 0);
    CHECK(takeStreamMove(
        pgn, known, event(R"({"fen": "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", "lm": "e2e4"})")));
    CHECK(known == 1);
    CHECK(takeStreamMove(
        pgn, known, event(R"({"fen": "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "lm": "e7e5"})")));
    CHECK(known == 2);
    CHECK(pgn == before);  // known already: nothing changes
    // Then the moves played since: added.
    CHECK(takeStreamMove(pgn, known, event(R"({"fen": "rnbqkbnr/pppp1ppp/8/4p2Q/4P3/8/PPPP1PPP/RNB1KBNR b KQkq - 1 2",
                                   "lm": "d1h5", "wc": 170, "bc": 178})")));
    CHECK(known == 3);
    CHECK(pgn.find("2. Qh5 { [%clk 0:02:50] } *") != std::string::npos);
    // Neither a position of the game nor one that follows: out of step.
    CHECK_FALSE(takeStreamMove(pgn, known, event(R"({"fen": "8/8/8/8/8/8/8/8 w - - 0 1", "lm": "e2e4"})")));
    // A stream that starts where the PGN ends: its moves follow.
    std::string other = ONGOING;
    std::size_t from  = 0;
    CHECK(takeStreamMove(other, from, event(R"({"fen": "rnbqkbnr/pppp1ppp/8/4p2Q/4P3/8/PPPP1PPP/RNB1KBNR b KQkq - 1 2",
                                   "lm": "d1h5"})")));
    CHECK(from == 3);
    CHECK(other.find("2. Qh5") != std::string::npos);
}

TEST_CASE("Lichess players: a game followed from its start to its end") {
    std::atomic<int> exports{0};
    ScriptedServer server(0, [&](const std::string& head, net::TcpConnection& c) {
        const auto path = requestPath(head);
        if (path.rfind("/game/export/game0001", 0) == 0) {
            c.sendAll(httpOk("application/x-chess-pgn", exports++ == 0 ? ONGOING : FINISHED));
        } else if (path == "/api/stream/game/game0001") {
            const std::string body =
                "{\"id\": \"game0001\", \"fen\": \"rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2\", "
                "\"status\": {\"id\": 20, \"name\": \"started\"}}\n"
                "{\"fen\": \"rnbqkbnr/pppp1ppp/8/4p2Q/4P3/8/PPPP1PPP/RNB1KBNR b KQkq - 1 2\", \"lm\": \"d1h5\", "
                "\"wc\": 170, \"bc\": 178}\n"
                "{\"id\": \"game0001\", \"status\": {\"id\": 30, \"name\": \"mate\"}}\n";
            c.sendAll(httpOk("application/x-ndjson", body));
        } else {
            c.sendAll("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        }
    });
    const auto port = server.port();
    REQUIRE(server.ok());
    servers().api = fmt::format("http://127.0.0.1:{}", port);
    {
        PlayersFollower follower;
        follower.watch({"game0001"});
        std::vector<BroadcastGame> games;
        for (int i = 0; i < 400; ++i) {
            games = follower.games();
            if (games.size() == 1 && games[0].result == "1-0") break;
            std::this_thread::sleep_for(25ms);
        }
        REQUIRE(games.size() == 1);
        CHECK(games[0].result == "1-0");
        CHECK(games[0].white == "Partner_1");
        CHECK(games[0].black_elo == 2188);
        CHECK(games[0].url == "https://lichess.org/game0001");
        CHECK(games[0].pgn.find("Qxf7#") != std::string::npos);
        CHECK(exports >= 2);  // whole when it started, whole again when it ended
        // No longer watched, it ended: it stays until room is needed.
        follower.watch({});
        std::this_thread::sleep_for(50ms);
        CHECK(follower.games().size() == 1);
    }  // stopped at once
    servers() = Servers{};
}

TEST_CASE("Lichess players: each failure of their status, whom the account follows, a game followed by kind") {
    AnsweringServer server;
    REQUIRE(server.ok());
    HostGate::instance().setPause(50ms);
    struct Case {
        int status;
        const char* body;
        NetError net;
    };
    for (const auto& [status, body, net] :
         {Case{401, "{}", NetError::Unauthorized}, Case{403, R"({"error":"Missing scope"})", NetError::Forbidden},
          Case{404, R"({"error":"Not found"})", NetError::NotFound}, Case{429, "{}", NetError::RateLimited},
          Case{503, "<html>down</html>", NetError::Server}}) {
        CAPTURE(status);
        server.answer(status, body);
        CHECK(playerStatuses({"Player_A", "Player_B"}).net == net);
        CHECK(followingOf("tok").net == net);
        // A game that cannot be read: its id and why.
        PlayersFollower follower;
        follower.watch({"game0001"});
        REQUIRE(waitUntil([&] { return follower.failure().failure.net == net; }));
        CHECK(follower.failure().game == "game0001");
    }
    server.answer(404, "{}");
    {
        PlayersFollower follower;
        follower.watch({"game0001"});
        REQUIRE(waitUntil([&] { return follower.failure().failure.failed(); }));
        CHECK(describe(follower.failure().failure) == "Lichess has no such game");
    }
    server.answer(200, "<html>not JSON</html>");
    CHECK(playerStatuses({"Player_A"}).net == NetError::BadAnswer);
    pointNowhere();
    CHECK(playerStatuses({"Player_A"}).net == NetError::Offline);
    CHECK(followingOf("tok").net == NetError::Offline);
    pointAtPausedHost("lichess-players.invalid");
    std::atomic<bool> cancel{true};
    CHECK(playerStatuses({"Player_A"}, &cancel).net == NetError::Cancelled);
    CHECK(followingOf("tok", &cancel).net == NetError::Cancelled);
    servers() = Servers{};
}
