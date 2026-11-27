// A chess.com player's games (#74): their name, the list of their months, what to ask for, what the profile keeps.
#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <tuple>

#include <fmt/include/fmt/format.h>

#include "chesscom.hpp"
#include "free_ports.hpp"
#include "https.hpp"
#include "scripted_server.hpp"

using namespace fcg;

TEST_CASE("chess.com: a player's name, their months, the months to ask for") {
    CHECK(chesscom::playerKey("Some_Player-1") == "some_player-1");
    CHECK(chesscom::playerKey(" spaced name ") == "spacedname");
    CHECK(chesscom::playerKey("bad/name").empty());
    CHECK(chesscom::playerKey("é").empty());
    CHECK(chesscom::archivesUrl("p1") == "https://api.chess.com/pub/player/p1/games/archives");
    CHECK(chesscom::monthPgnUrl("p1", "2026/10") == "https://api.chess.com/pub/player/p1/games/2026/10/pgn");
    const auto months = chesscom::parseArchives(
        R"({"archives":["https://api.chess.com/pub/player/p1/games/2026/10","https://api.chess.com/pub/player/p1/games/2025/12",)"
        R"("https://api.chess.com/pub/player/p1/games/2026/01","not a month"]})");
    CHECK(months == std::vector<std::string>{"2025/12", "2026/01", "2026/10"});
    CHECK(chesscom::parseArchives("{}").empty());
    CHECK(chesscom::parseArchives("not json").empty());
    // Nothing added: every month. Some added: the others, and the newest added again (it may have grown).
    CHECK(chesscom::monthsToAsk(months, {}) == months);
    CHECK(chesscom::monthsToAsk(months, {"2025/12", "2026/01"}) == std::vector<std::string>{"2026/01", "2026/10"});
    CHECK(chesscom::monthsToAsk(months, {"2025/12", "2026/01", "2026/10"}) == std::vector<std::string>{"2026/10"});
}

TEST_CASE("chess.com: the followed players the profile keeps") {
    const std::vector<chesscom::Followed> players{{"p1", "/data/Mine.fcgdb", {"2026/09", "2026/10"}},
                                                  {"p2", "/data/Other.fcgdb", {}}};
    CHECK(chesscom::fromJson(chesscom::toJson(players)) == players);
    CHECK(chesscom::fromJson("nonsense").empty());
}

TEST_CASE("chess.com: the player's months that cannot be had keep why by their kind, worded by the sheet (#141)") {
    using namespace std::chrono_literals;
    using Kind = NetFailure::Kind;
    auto& gate = HostGate::instance();
    gate.setPause(50ms);  // the host's minute after a 429, shorter for the test
    std::atomic<int> status{200};
    fcg::test::ScriptedServer server(0, [&](const std::string& head, net::TcpConnection& c) {
        (void)fcg::test::requestBody(head, c);
        if (status == 200)
            c.sendAll("HTTP/1.1 200 OK\r\nContent-Length: 16\r\n\r\n{\"archives\": []}");
        else
            c.sendAll(fmt::format("HTTP/1.1 {} No\r\nContent-Length: 0\r\n\r\n", status.load()));
    });
    const auto port = server.port();
    REQUIRE(server.ok());
    chesscom::setServer(fmt::format("http://127.0.0.1:{}", port));

    status                = 404;
    auto [months, failed] = chesscom::fetchMonths("nobody");
    CHECK(months.empty());
    CHECK(failed.kind == Kind::NotFound);
    CHECK(chesscom::monthsSaid(failed) == "chess.com knows no player of that name.");

    status = 502;
    failed = chesscom::fetchMonths("someone").second;
    CHECK(failed.kind == Kind::Status);
    CHECK(failed.status == 502);
    CHECK(chesscom::monthsSaid(failed) == "HTTP 502");

    status = 429;
    failed = chesscom::fetchMonths("someone").second;
    CHECK(failed.kind == Kind::RateLimited);
    CHECK(gate.pausedFor(failed.host) > 0ms);
    CHECK(chesscom::monthsSaid(failed) == "chess.com asks to wait: too many requests.");
    CHECK(gate.waitPause(failed.host));

    // A player without a game: no month, and no failure.
    status                   = 200;
    std::tie(months, failed) = chesscom::fetchMonths("someone");
    CHECK(months.empty());
    CHECK(failed.kind == Kind::None);

    // No server at all: an outage (freePort(): a free port nothing is started on).
    chesscom::setServer(fmt::format("http://127.0.0.1:{}", fcg::test::freePort()));
    failed = chesscom::fetchMonths("someone").second;
    CHECK(failed.kind == Kind::Offline);
    CHECK(chesscom::monthsSaid(failed) == "chess.com cannot be reached from here.");
    chesscom::setServer("https://api.chess.com");
}
