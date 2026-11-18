// Lichess's official broadcasts, and a round followed as it is played, against a server on this machine that answers
// and streams as Lichess does.
#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <fmt/include/fmt/format.h>

#include "free_ports.hpp"
#include "lichess_api.hpp"
#include "lichess_broadcasts.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using namespace fcg::lichess;
using namespace fcg::test;
using namespace std::chrono_literals;

namespace {

std::string game(const std::string& board, const std::string& white, const std::string& black, const std::string& moves,
                 const std::string& result) {
    return fmt::format(
        "[Event \"Test Open\"]\n[Round \"7\"]\n[White \"{}\"]\n[Black \"{}\"]\n[Result \"{}\"]\n"
        "[WhiteElo \"2700\"]\n[BlackElo \"2650\"]\n[Board \"{}\"]\n"
        "[GameURL \"https://lichess.org/broadcast/test-open/round-7/R1/g{}\"]\n\n{} {}\n\n\n",
        white, black, result, board, board, moves, result);
}

void chunk(net::TcpConnection& c, const std::string& text) {
    c.sendAll(fmt::format("{:x}\r\n{}\r\n", text.size(), text));
}

struct Servers_ {
    explicit Servers_(uint16_t port) { servers().api = fmt::format("http://127.0.0.1:{}", port); }
    ~Servers_() { servers() = Servers{}; }
};

}  // namespace

TEST_CASE("Lichess broadcasts: those on now and those to come") {
    const auto port = freePort();
    ScriptedServer server(port, [](const std::string& head, net::TcpConnection& c) {
        CHECK(requestPath(head) == "/api/broadcast/top?page=1");
        const std::string body =
            R"({"active":[{"tour":{"id":"qEXour3L","name":"Total Chess | Semifinals","slug":"s","info":{"format":"KO",)"
            R"("fideTC":"standard","location":"Budapest, Hungary","players":"Carlsen, Caruana"},"url":"https://lichess.org/b/q",)"
            R"("tier":5},"round":{"id":"m2m1nxI1","name":"Game 8 | Blitz","slug":"g8","startsAt":1795010400000,)"
            R"("url":"https://lichess.org/b/q/m2m1nxI1"},"group":"Total Chess 2026"}],)"
            R"("upcoming":[{"tour":{"id":"t2","name":"Next Open","url":"u","tier":3},"round":{"id":"r2","name":"Round 1",)"
            R"("startsAt":1795100000000,"url":"u2"}}],"past":{"currentPage":1}})";
        c.sendAll(
            fmt::format("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: {}\r\n"
                        "Connection: close\r\n\r\n{}",
                        body.size(), body));
    });
    REQUIRE(server.ok());
    Servers_ local(port);
    const auto list = topBroadcasts();
    REQUIRE_MESSAGE(list.value, list.error);
    REQUIRE(list.value->active.size() == 1);
    const auto& on = list.value->active[0];
    CHECK(on.tour.id == "qEXour3L");
    CHECK(on.tour.name == "Total Chess | Semifinals");
    CHECK(on.tour.tier == 5);
    CHECK(on.tour.location == "Budapest, Hungary");
    CHECK(on.tour.players == "Carlsen, Caruana");
    CHECK(on.tour.time_control == "standard");
    CHECK(on.round.id == "m2m1nxI1");
    CHECK(on.round.starts == 1795010400000);
    CHECK(on.group == "Total Chess 2026");
    REQUIRE(list.value->upcoming.size() == 1);
    CHECK(list.value->upcoming[0].round.name == "Round 1");
}

TEST_CASE("Lichess broadcasts: a round followed, its games as they change, the stream opened again after a break") {
    const auto port = freePort();
    std::atomic<int> connections{0};
    std::atomic<bool> release{false};
    std::atomic<int> snapshots{0};
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto path = requestPath(head);
        if (path == "/api/broadcast/round/R1.pgn") {
            // The round as it stands: then, after a break, finished on board 1.
            const auto body = snapshots++ == 0
                                  ? game("1", "Carlsen, Magnus", "Caruana, Fabiano", "1. e4", "*") +
                                        game("2", "So, Wesley", "Giri, Anish", "1. d4 Nf6", "*")
                                  : game("1", "Carlsen, Magnus", "Caruana, Fabiano", "1. e4 e5 2. Nf3", "1-0") +
                                        game("2", "So, Wesley", "Giri, Anish", "1. d4 Nf6", "*");
            c.sendAll(
                fmt::format("HTTP/1.1 200 OK\r\nContent-Type: application/x-chess-pgn\r\nContent-Length: {}\r\n"
                            "Connection: close\r\n\r\n{}",
                            body.size(), body));
            return;
        }
        if (path != "/api/stream/broadcast/round/R1.pgn") {
            c.sendAll("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            return;
        }
        c.sendAll(
            "HTTP/1.1 200 OK\r\nContent-Type: application/x-chess-pgn\r\nTransfer-Encoding: chunked\r\n"
            "Connection: close\r\n\r\n");
        if (connections++ == 0) {
            // Board 1 moves, sent in two pieces cut in the middle, then again as it was; then the stream breaks.
            const auto moved = game("1", "Carlsen, Magnus", "Caruana, Fabiano", "1. e4 e5", "*");
            chunk(c, moved.substr(0, 60));
            chunk(c, moved.substr(60));
            chunk(c, moved);
            c.sendAll("0\r\n\r\n");
            return;
        }
        (void)waitUntil([&] { return release.load(); });  // open, nothing changing
    });
    REQUIRE(server.ok());
    Servers_ local(port);
    RoundFollower::setRetry(10ms, 50ms);
    RoundFollower follower("R1");
    REQUIRE(waitUntil([&] { return follower.connections() == 2 && follower.changes() == 4; }));
    const auto games = follower.games();
    REQUIRE(games.size() == 2);
    CHECK(games[0].white == "Carlsen, Magnus");
    CHECK(games[0].result == "1-0");
    CHECK(games[0].version == 3);  // as it stood, after 1...e5, and finished; sent again unchanged, not counted
    CHECK(snapshots == 2);
    CHECK(games[0].board == "1");
    CHECK(games[0].white_elo == 2700);
    CHECK(games[0].pgn.find("2. Nf3") != std::string::npos);
    CHECK(games[1].black == "Giri, Anish");
    CHECK(games[1].version == 1);
    CHECK(games[1].url == "https://lichess.org/broadcast/test-open/round-7/R1/g2");
    CHECK(follower.streaming());
    CHECK(follower.error().empty());
    const auto asked = std::chrono::steady_clock::now();
    follower.stop();
    CHECK(std::chrono::steady_clock::now() - asked < 5s);
    release = true;
    RoundFollower::setRetry(5s, 60s);
}

TEST_CASE("Lichess broadcasts: a round Lichess does not have") {
    const auto port = freePort();
    ScriptedServer server(port, [](const std::string&, net::TcpConnection& c) {
        c.sendAll("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    });
    REQUIRE(server.ok());
    Servers_ local(port);
    RoundFollower::setRetry(10ms, 40ms);
    RoundFollower follower("nope");
    REQUIRE(waitUntil([&] { return follower.error() == "Lichess has no such round"; }));
    CHECK(follower.connections() == 0);
    CHECK(follower.games().empty());
    follower.stop();
    RoundFollower::setRetry(5s, 60s);
}
