// The user's games in progress on Lichess (#48): as /api/account/playing lists them, and heard through the event stream
// against a server on this machine.
#include <doctest/doctest.hpp>

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>

#include <fmt/include/fmt/format.h>
#include <json.hpp>

#include "free_ports.hpp"
#include "lichess_api.hpp"
#include "lichess_playing.hpp"
#include "scripted_server.hpp"

#include <study/store.hpp>

using namespace fcg;
using namespace fcg::lichess;
using namespace fcg::test;
using namespace std::chrono_literals;

namespace {

const char* const GAME =
    R"({"gameId": "abcd1234", "fullId": "abcd1234wxyz", "color": "black", "fen": "8/8/8/8/8/8/8/8 w - - 0 1",
        "isMyTurn": true, "speed": "correspondence", "rated": true, "secondsLeft": 100800,
        "opponent": {"id": "partner_1", "username": "Partner_1", "rating": 2010}})";

}  // namespace

TEST_CASE("Lichess playing: the games in progress as Lichess lists them") {
    const auto games = readPlaying(nlohmann::json::parse(fmt::format(
        R"({{"nowPlaying": [{}, {{"gameId": "efgh5678", "speed": "blitz", "opponent": {{"ai": 3}}}}, {{"color": "white"}}]}})",
        GAME)));
    REQUIRE(games.size() == 2);  // the last has no id
    CHECK(games[0].id == "abcd1234");
    CHECK(games[0].full_id == "abcd1234wxyz");
    CHECK_FALSE(games[0].white);
    CHECK(games[0].my_turn);
    CHECK_FALSE(games[0].live());
    CHECK(games[0].seconds_left == 100800);
    CHECK(games[0].opponent == "Partner_1");
    CHECK(games[0].opponent_rating == 2010);
    CHECK(games[1].live());
    CHECK(games[1].opponent == "AI level 3");
    CHECK(games[1].full_id == "efgh5678");  // without a full id, the game's
    CHECK(games[0].move() == 1);
    PlayingGame later;
    later.fen = "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 23";
    CHECK(later.move() == 23);
    later.fen = "8/8/8/8 w";
    CHECK(later.move() == 0);
    CHECK(readPlaying(nlohmann::json::array()).empty());
}

TEST_CASE("Lichess playing: the event stream's lines, games only") {
    const auto start = readGameEvent(fmt::format(R"({{"type": "gameStart", "game": {}}})", GAME) + "\r");
    REQUIRE(start);
    CHECK(start->kind == GameEvent::Kind::Started);
    CHECK(start->game.id == "abcd1234");
    const auto finish = readGameEvent(fmt::format(R"({{"type": "gameFinish", "game": {}}})", GAME));
    REQUIRE(finish);
    CHECK(finish->kind == GameEvent::Kind::Finished);
    CHECK_FALSE(readGameEvent(""));
    CHECK_FALSE(readGameEvent(R"({"type": "challenge", "challenge": {}})"));
    CHECK_FALSE(readGameEvent("not json"));
}

TEST_CASE("Lichess playing: the stream heard on its own thread; a refused token stops it") {
    std::atomic<bool> refuse{false};
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        if (requestPath(head) != "/api/stream/event" || refuse) {
            c.sendAll("HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            return;
        }
        // One event a line, as Lichess sends them; an empty line keeps the stream open.
        const auto game = nlohmann::json::parse(GAME).dump();
        const auto body = fmt::format("{}\n\n{}\n", fmt::format(R"({{"type": "gameStart", "game": {}}})", game),
                                      fmt::format(R"({{"type": "gameFinish", "game": {}}})", game));
        c.sendAll(
            fmt::format("HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\nContent-Length: {}\r\n"
                        "Connection: close\r\n\r\n{}",
                        body.size(), body));
    });
    REQUIRE(server.ok());
    servers().api = fmt::format("http://127.0.0.1:{}", port);
    {
        EventListener listener("a-token");
        std::vector<GameEvent> heard;
        for (int i = 0; i < 200 && heard.size() < 3; ++i) {
            for (auto& e : listener.take()) heard.push_back(std::move(e));
            if (heard.size() < 3) std::this_thread::sleep_for(25ms);
        }
        REQUIRE(heard.size() == 3);
        CHECK(heard[0].kind == GameEvent::Kind::Opened);  // the list starts again
        CHECK(heard[1].kind == GameEvent::Kind::Started);
        CHECK(heard[1].game.id == "abcd1234");
        CHECK(heard[2].kind == GameEvent::Kind::Finished);
        CHECK(listener.token() == "a-token");
    }  // stopped at once, though it waits a minute to open the stream again
    refuse = true;
    {
        EventListener listener("bad-token");
        for (int i = 0; i < 200 && !listener.refused(); ++i) std::this_thread::sleep_for(25ms);
        CHECK(listener.refused());
        CHECK(listener.error() == "Lichess refused the token");
    }
    servers() = Servers{};
}

TEST_CASE("Lichess playing: the games that ended, those no longer listed") {
    std::vector<PlayingGame> before(3), after(2);
    before[0].id     = "aaaa1111";
    before[1].id     = "bbbb2222";
    before[2].id     = "cccc3333";
    after[0].id      = "cccc3333";
    after[1].id      = "dddd4444";  // a new one
    const auto ended = endedGames(before, after);
    REQUIRE(ended.size() == 2);
    CHECK(ended[0].id == "aaaa1111");
    CHECK(ended[1].id == "bbbb2222");
    CHECK(endedGames({}, after).empty());
}

TEST_CASE("Lichess playing: a finished game brought into a database, once") {
    const char* const PGN = R"([Event "Rated blitz game"]
[Site "https://lichess.org/abcd1234"]
[White "Partner_3"]
[Black "rs_sample_0000"]
[Result "1-0"]

1. e4 { [%eval 0.2] [%clk 0:03:00] } 1... e5 { [%eval 0.25] [%clk 0:03:00] } 2. Qh5 { [%eval -0.1] } 2... Nc6 { [%eval 0.0] }
3. Bc4 { [%eval -0.1] } 3... Nf6 { [%eval 6.0] } 4. Qxf7# 1-0
)";
    const auto port       = freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto path    = requestPath(head);
        const bool aborted = path.rfind("/game/export/abor1234", 0) == 0;
        if (path.rfind("/game/export/abcd1234", 0) != 0 && !aborted) {
            c.sendAll("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            return;
        }
        const std::string body =
            aborted ? std::string("[Site \"https://lichess.org/abor1234\"]\n[Result \"*\"]\n\n1. e4 *\n") : PGN;
        c.sendAll(
            fmt::format("HTTP/1.1 200 OK\r\nContent-Type: application/x-chess-pgn\r\nContent-Length: {}\r\n"
                        "Connection: close\r\n\r\n{}",
                        body.size(), body));
    });
    REQUIRE(server.ok());
    servers().api  = fmt::format("http://127.0.0.1:{}", port);
    const auto dir = std::filesystem::temp_directory_path() / "fcg-lichess-finished";
    std::filesystem::remove_all(dir);
    const auto db = study::Database::create(dir);
    REQUIRE(db);
    const auto first = bringGame("abcd1234", "a-token", db.get());
    CHECK(first.error.empty());
    CHECK(first.added);
    CHECK(first.analysed);
    CHECK(first.plies == 7);
    CHECK(first.result == '1');
    const auto again = bringGame("abcd1234", "a-token", db.get());
    CHECK(again.error.empty());
    CHECK_FALSE(again.added);  // the database has it
    const auto without = bringGame("abcd1234", "a-token", nullptr);
    CHECK(without.pgn == PGN);
    CHECK_FALSE(without.added);
    const auto aborted = bringGame("abor1234", "a-token", db.get());
    CHECK(aborted.error.empty());
    CHECK(aborted.plies == 1);
    CHECK_FALSE(aborted.added);  // Lichess aborted it: it stays out
    const auto missing = bringGame("zzzz9999", "a-token", db.get());
    CHECK(missing.pgn.empty());
    CHECK_FALSE(missing.error.empty());
    servers() = Servers{};
}
