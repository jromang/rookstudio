// Lichess's cloud evaluation, tablebase and opening explorer, against a server on this machine that answers as Lichess
// did (answers taken from lichess.org on 2026-11-18).
#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>

#include "free_ports.hpp"
#include "https.hpp"
#include "lichess_api.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using namespace fcg::lichess;
using namespace fcg::test;
using namespace std::chrono_literals;

namespace {

constexpr const char* AFTER_E4 = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1";

void answer(net::TcpConnection& c, int status, const std::string& body, const char* type = "application/json") {
    c.sendAll(fmt::format("HTTP/1.1 {} X\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                          status, type, body.size()));
    c.sendAll(body);
}

// A server whose answers depend on the request's path, which it keeps with the request's head.
struct Lichess {
    explicit Lichess(std::function<void(const std::string& path, const std::string& head, net::TcpConnection&)> serve)
        : port(fcg::test::freePort()), server(port, [this, serve](const std::string& head, net::TcpConnection& c) {
              const auto path = requestPath(head);
              {
                  std::lock_guard lock(mutex);
                  paths.push_back(path);
                  heads.push_back(head);
              }
              serve(path, head, c);
          }) {
        REQUIRE(server.ok());
        const auto base = fmt::format("http://127.0.0.1:{}", port);
        servers()       = Servers{base, base + "/tb", base + "/explorer"};
        HostGate::instance().setPause(50ms);
    }
    ~Lichess() {
        servers() = Servers{};
        HostGate::instance().setPause(60s);
    }
    std::string lastPath() {
        std::lock_guard lock(mutex);
        return paths.empty() ? std::string() : paths.back();
    }
    std::string lastHead() {
        std::lock_guard lock(mutex);
        return heads.empty() ? std::string() : heads.back();
    }
    uint16_t port;
    std::mutex mutex;
    std::vector<std::string> paths, heads;
    ScriptedServer server;
};

}  // namespace

TEST_CASE("Lichess: a value in a URL's query") {
    CHECK(urlEncode(AFTER_E4) == "rnbqkbnr%2Fpppppppp%2F8%2F8%2F4P3%2F8%2FPPPP1PPP%2FRNBQKBNR%20b%20KQkq%20-%200%201");
    CHECK(urlEncode("Magnus_Carlsen-1.~") == "Magnus_Carlsen-1.~");
    CHECK(urlEncode("é") == "%C3%A9");
}

TEST_CASE("Lichess: the cloud's evaluation of a position, or none") {
    Lichess lichess([](const std::string& path, const std::string&, net::TcpConnection& c) {
        if (path.find("8%2F8%2F8") != std::string::npos) {
            answer(c, 404, "{\"error\":\"No cloud evaluation available for that position\"}");
            return;
        }
        answer(c, 200,
               R"({"fen":"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1","knodes":119133,"depth":60,)"
               R"("pvs":[{"moves":"e7e5 g1f3 b8c6 f1b5","cp":22},{"moves":"f7f6 d2d4 g7g5 d1h5","mate":2}]})");
    });
    const auto e = cloudEval(AFTER_E4, 2);
    REQUIRE_MESSAGE(e.value, e.error);
    CHECK(lichess.lastPath() == std::string("/api/cloud-eval?fen=") + urlEncode(AFTER_E4) + "&multiPv=2");
    CHECK(e.value->depth == 60);
    CHECK(e.value->knodes == 119133);
    REQUIRE(e.value->lines.size() == 2);
    CHECK(e.value->lines[0].moves == std::vector<std::string>{"e7e5", "g1f3", "b8c6", "f1b5"});
    CHECK(e.value->lines[0].cp == 22);
    CHECK_FALSE(e.value->lines[0].mate);
    CHECK(e.value->lines[1].mate == 2);
    // A position the cloud has not evaluated: no value, and nothing wrong.
    const auto none = cloudEval("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    CHECK_FALSE(none.value);
    CHECK(none.error.empty());
    CHECK(none.status == 404);
    // Five lines at most, as Lichess gives.
    (void)cloudEval(AFTER_E4, 9);
    CHECK(lichess.lastPath().ends_with("&multiPv=5"));
}

TEST_CASE("Lichess: the tablebase's answer, each move with what it leaves the other side") {
    Lichess lichess([](const std::string&, const std::string&, net::TcpConnection& c) {
        answer(
            c, 200,
            R"({"checkmate":false,"stalemate":false,"variant_win":false,"variant_loss":false,)"
            R"("insufficient_material":false,"dtz":1,"precise_dtz":1,"dtm":17,"dtw":null,"dtc":null,"category":"win",)"
            R"("moves":[{"uci":"h7h8q","san":"h8=Q+","zeroing":true,"conversion":true,"checkmate":false,)"
            R"("stalemate":false,"variant_win":false,"variant_loss":false,"insufficient_material":false,"dtz":-2,)"
            R"("precise_dtz":-2,"dtm":-16,"dtw":null,"dtc":null,"category":"loss"},)"
            R"({"uci":"g7g6","san":"Kg6","zeroing":false,"checkmate":false,"stalemate":false,"dtz":null,)"
            R"("dtm":null,"category":"blessed-loss"},)"
            R"({"uci":"g7f6","san":"Kf6","zeroing":false,"checkmate":false,"stalemate":false,"dtz":0,)"
            R"("dtm":0,"category":"draw"}]})");
    });
    const auto t = tablebase("4k3/6KP/8/8/8/8/7p/8 w - - 0 1");
    REQUIRE_MESSAGE(t.value, t.error);
    CHECK(lichess.lastPath() == "/tb/standard?fen=4k3%2F6KP%2F8%2F8%2F8%2F8%2F7p%2F8%20w%20-%20-%200%201");
    CHECK(t.value->outcome == Outcome::Win);
    CHECK(t.value->dtz == 1);
    CHECK(t.value->dtm == 17);
    REQUIRE(t.value->moves.size() == 3);
    CHECK(t.value->moves[0].san == "h8=Q+");
    CHECK(t.value->moves[0].outcome == Outcome::Loss);  // for Black, after it
    CHECK(t.value->moves[0].zeroing);
    CHECK(t.value->moves[0].dtm == -16);
    CHECK(t.value->moves[1].outcome == Outcome::BlessedLoss);
    CHECK_FALSE(t.value->moves[1].dtz);
    CHECK(t.value->moves[2].outcome == Outcome::Draw);
}

TEST_CASE("Lichess: the opening explorer, with an account's token, and without") {
    Lichess lichess([](const std::string& path, const std::string& head, net::TcpConnection& c) {
        if (head.find("Authorization: Bearer lip_test") == std::string::npos) {
            answer(c, 401, "<html><body>401 Authorization Required</body></html>", "text/html");
            return;
        }
        if (path.starts_with("/explorer/player")) {
            // Counted as the games are read: a line at a time, the last one whole.
            c.sendAll("HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\nConnection: close\r\n\r\n");
            c.sendAll(R"({"white":1,"draws":0,"black":0,"moves":[],"recentGames":[]})"
                      "\n");
            c.sendAll(R"({"white":3,"draws":1,"black":2,"moves":[{"uci":"e7e5","san":"e5","white":3,"draws":1,)"
                      R"("black":2,"averageOpponentRating":1900}],"recentGames":[{"uci":"e7e5","id":"abcd1234",)"
                      R"("winner":"black","speed":"blitz","white":{"name":"someone","rating":1880},)"
                      R"("black":{"name":"player","rating":2010},"year":2026,"month":"2026-10"}]})"
                      "\n");
            return;
        }
        answer(
            c, 200,
            R"({"white":524563,"draws":670216,"black":426181,"moves":[{"uci":"c7c5","san":"c5","averageRating":2412,)"
            R"("white":180017,"draws":225434,"black":162080,"game":null},{"uci":"e7e6","san":"e6",)"
            R"("averageRating":2405,"white":1,"draws":0,"black":0,"game":{"id":"xyz98765"}}],)"
            R"("topGames":[{"uci":"c7c5","id":"ab12cd34","winner":"white","white":{"name":"Carlsen, M.",)"
            R"("rating":2862},"black":{"name":"Caruana, F.","rating":2832},"year":2019,"month":"2019-06"},)"
            R"({"uci":"e7e5","id":"ef56gh78","winner":null,"white":{"name":"Anand, V.","rating":2780},)"
            R"("black":{"name":"Kramnik, V.","rating":2790},"year":2008}],)"
            R"("opening":{"eco":"B00","name":"King's Pawn Game"}})");
    });
    ExplorerQuery q;
    q.fen = AFTER_E4;
    // Without a token: Lichess asks for an account.
    const auto refused = explorer(q);
    CHECK_FALSE(refused.value);
    CHECK(refused.status == 401);
    CHECK(refused.error == "Lichess asks for a linked account");

    q.token            = "lip_test";
    q.moves            = 5;
    q.top_games        = 2;
    q.since            = "1990";
    const auto masters = explorer(q);
    REQUIRE_MESSAGE(masters.value, masters.error);
    CHECK(lichess.lastPath() ==
          std::string("/explorer/masters?fen=") + urlEncode(AFTER_E4) + "&moves=5&topGames=2&since=1990");
    CHECK(masters.value->white == 524563);
    CHECK(masters.value->draws == 670216);
    CHECK(masters.value->black == 426181);
    REQUIRE(masters.value->moves.size() == 2);
    CHECK(masters.value->moves[0].san == "c5");
    CHECK(masters.value->moves[0].average_rating == 2412);
    CHECK(masters.value->moves[0].game.empty());
    CHECK(masters.value->moves[1].game == "xyz98765");
    REQUIRE(masters.value->top_games.size() == 2);
    CHECK(masters.value->top_games[0].white == "Carlsen, M.");
    CHECK(masters.value->top_games[0].black_rating == 2832);
    CHECK(masters.value->top_games[0].winner == 'w');
    CHECK(masters.value->top_games[1].winner == '=');
    CHECK(masters.value->top_games[1].month.empty());
    CHECK(masters.value->eco == "B00");
    CHECK(masters.value->opening == "King's Pawn Game");

    ExplorerQuery games = q;
    games.source        = ExplorerSource::Lichess;
    games.speeds        = {"blitz", "rapid"};
    games.ratings       = {2000, 2200};
    games.recent_games  = 1;
    games.since         = "2025-01";
    REQUIRE(explorer(games).value);
    CHECK(lichess.lastPath() == std::string("/explorer/lichess?variant=standard&fen=") + urlEncode(AFTER_E4) +
                                    "&moves=5&topGames=2&recentGames=1&speeds=blitz,rapid&ratings=2000,2200"
                                    "&since=2025-01");

    ExplorerQuery mine = q;
    mine.source        = ExplorerSource::Player;
    mine.player        = "player";
    mine.player_white  = false;
    mine.recent_games  = 3;
    mine.since         = "";
    const auto own     = explorer(mine);
    REQUIRE_MESSAGE(own.value, own.error);
    CHECK(lichess.lastPath() == std::string("/explorer/player?variant=standard&player=player&color=black&fen=") +
                                    urlEncode(AFTER_E4) + "&moves=5&recentGames=3");
    CHECK(own.value->white == 3);
    CHECK(own.value->black == 2);
    REQUIRE(own.value->recent_games.size() == 1);
    CHECK(own.value->recent_games[0].id == "abcd1234");
    CHECK(own.value->recent_games[0].winner == 'b');
    CHECK(own.value->recent_games[0].black_rating == 2010);
}

TEST_CASE("Lichess: too many requests pause its host, and an answer that is not Lichess's is told") {
    std::atomic<int> asked{0};
    Lichess lichess([&](const std::string&, const std::string&, net::TcpConnection& c) {
        if (asked++ == 0)
            answer(c, 429, "{}");
        else
            answer(c, 200, "not json", "text/plain");
    });
    const auto busy = tablebase("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    CHECK_FALSE(busy.value);
    CHECK(busy.status == 429);
    CHECK(busy.error == "Lichess asks to wait a minute: too many requests");
    CHECK(HostGate::instance().pausedFor("127.0.0.1") > 0ms);
    // The next request waits for the pause, then reads what comes.
    const auto odd = tablebase("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    CHECK_FALSE(odd.value);
    CHECK(odd.status == 200);
    CHECK(odd.error.starts_with("Lichess's answer could not be read"));
}
