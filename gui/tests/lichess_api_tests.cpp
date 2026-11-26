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
    REQUIRE_MESSAGE(e.value, describe(e));
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
    CHECK_FALSE(none.failed());
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
    // The op1 tables' DTC asked with it (dtc=auxiliary): their server answers their 8-piece positions only then.
    CHECK(lichess.lastPath() ==
          "/tb/standard?fen=4k3%2F6KP%2F8%2F8%2F8%2F8%2F7p%2F8%20w%20-%20-%200%201&dtc=auxiliary");
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

TEST_CASE("Lichess: an 8-piece position with opposing pawns, its distance to the next conversion") {
    Lichess lichess([](const std::string&, const std::string&, net::TcpConnection& c) {
        answer(c, 200,
               R"({"checkmate":false,"stalemate":false,"insufficient_material":false,"dtz":null,"precise_dtz":null,)"
               R"("dtm":null,"dtc":12,"category":"maybe-win","moves":[{"uci":"e1e2","san":"Ke2","zeroing":false,)"
               R"("checkmate":false,"stalemate":false,"dtz":null,"dtm":null,"dtc":-11,"category":"maybe-loss"}]})");
    });
    const auto t = tablebase("4k3/1p3p2/1P3P2/8/8/8/8/4K3 w - - 0 1");
    REQUIRE_MESSAGE(t.value, t.error);
    CHECK(t.value->outcome == Outcome::MaybeWin);
    CHECK_FALSE(t.value->dtz);
    CHECK(t.value->dtc == 12);
    REQUIRE(t.value->moves.size() == 1);
    CHECK(t.value->moves[0].dtc == -11);
    CHECK(t.value->moves[0].outcome == Outcome::MaybeLoss);
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
    CHECK(refused.net == NetError::Unauthorized);

    q.token            = "lip_test";
    q.moves            = 5;
    q.top_games        = 2;
    q.since            = "1990";
    const auto masters = explorer(q);
    REQUIRE_MESSAGE(masters.value, describe(masters));
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
    REQUIRE_MESSAGE(own.value, describe(own));
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
    CHECK(busy.net == NetError::RateLimited);
    CHECK(describe(busy) == "Lichess asks to wait a minute: too many requests");
    CHECK(HostGate::instance().pausedFor("127.0.0.1") > 0ms);
    // The next request waits for the pause, then reads what comes.
    const auto odd = tablebase("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    CHECK_FALSE(odd.value);
    CHECK(odd.status == 200);
    CHECK(odd.net == NetError::BadAnswer);
    CHECK(describe(odd) == "Lichess's answer could not be read");
}

TEST_CASE("Lichess: a position's games month by month, where they go, and a master's game") {
    Lichess lichess([](const std::string& path, const std::string& head, net::TcpConnection& c) {
        if (path.starts_with("/explorer/masters/pgn/")) {
            if (head.find("Authorization: Bearer lip_test") == std::string::npos) {
                answer(c, 401, "{}");
            } else if (path == "/explorer/masters/pgn/aAbqI4ey") {
                answer(c, 200, "[White \"Master, A\"]\n[Black \"Master, B\"]\n\n1. e4 c5 1-0\n",
                       "application/x-chess-pgn");
            } else {
                answer(c, 404, "{}");
            }
            return;
        }
        // Thirteen months: six of 10 games, six of 20, the month under way.
        std::string months;
        for (int m = 1; m <= 13; ++m)
            months += fmt::format(R"({}{{"month":"2025-{:02}","white":{},"draws":0,"black":0}})", m > 1 ? "," : "", m,
                                  m <= 6    ? 10
                                  : m <= 12 ? 20
                                            : 1);
        answer(c, 200, R"({"white":10,"draws":5,"black":5,"moves":[],"topGames":[],"history":[)" + months + "]}");
    });
    lichess::ExplorerQuery q;
    q.source     = lichess::ExplorerSource::Lichess;
    q.fen        = AFTER_E4;
    q.token      = "lip_test";
    q.history    = true;
    const auto e = lichess::explorer(q);
    REQUIRE(e.value);
    CHECK(lichess.lastPath().find("&history=true") != std::string::npos);
    REQUIRE(e.value->history.size() == 13);
    CHECK(e.value->history[0].month == "2025-01");
    CHECK(e.value->history[6].games == 20);
    CHECK(lichess::trendOf(e.value->history) == lichess::Trend::Rising);
    // Where they go: falling, steady, too few months, too few games.
    std::vector<lichess::ExplorerMonth> h(13);
    for (std::size_t i = 0; i < 13; ++i) h[i].games = i < 6 ? 30 : 10;
    CHECK(lichess::trendOf(h) == lichess::Trend::Falling);
    for (auto& m : h) m.games = 20;
    CHECK(lichess::trendOf(h) == lichess::Trend::Steady);
    CHECK(lichess::trendOf(std::vector<lichess::ExplorerMonth>(12, {"", 50})) == lichess::Trend::Unknown);
    CHECK(lichess::trendOf(std::vector<lichess::ExplorerMonth>(13, {"", 1})) == lichess::Trend::Unknown);
    // The masters' history is not asked: Lichess's explorer has none for them.
    q.source = lichess::ExplorerSource::Masters;
    (void)lichess::explorer(q);
    CHECK(lichess.lastPath().find("history") == std::string::npos);

    const auto game = lichess::mastersGame("aAbqI4ey", "lip_test");
    REQUIRE(game.value);
    CHECK(game.value->find("[White \"Master, A\"]") != std::string::npos);
    CHECK(lichess.lastPath() == "/explorer/masters/pgn/aAbqI4ey");
    const auto missing = lichess::mastersGame("missing1", "lip_test");
    CHECK(missing.net == NetError::NotFound);
    CHECK(describe(missing) == "Lichess has no such game");
    CHECK(lichess::mastersGame("aAbqI4ey", "").status == 401);
}

TEST_CASE("Lichess: each failure of the cloud, the tables and the explorer told by its kind") {
    std::mutex mutex;
    std::pair<int, std::string> next{200, "{}"};  // what the server answers next
    Lichess lichess([&](const std::string&, const std::string&, net::TcpConnection& c) {
        std::pair<int, std::string> now;
        {
            std::lock_guard lock(mutex);
            now = next;
        }
        answer(c, now.first, now.second);
    });
    const auto answering = [&](int status, std::string body) {
        std::lock_guard lock(mutex);
        next = {status, std::move(body)};
    };
    constexpr const char* KK = "4k3/8/8/8/8/8/8/4K3 w - - 0 1";
    ExplorerQuery masters;
    masters.fen          = AFTER_E4;
    masters.token        = "lip_test";
    ExplorerQuery player = masters;
    player.source        = ExplorerSource::Player;
    player.player        = "player";
    struct Case {
        int status;
        const char* body;
        NetError net;
    };
    for (const auto& [status, body, net] :
         {Case{401, R"({"error":"No such token"})", NetError::Unauthorized},
          Case{403, R"({"error":"Missing scope"})", NetError::Forbidden}, Case{429, "{}", NetError::RateLimited},
          Case{500, "<html>Internal error</html>", NetError::Server}, Case{503, "{}", NetError::Server},
          Case{200, "<html>not JSON</html>", NetError::BadAnswer}}) {
        CAPTURE(status);
        answering(status, body);
        CHECK(tablebase(KK).net == net);
        CHECK(tablebaseMainline(KK).net == net);
        CHECK(cloudEval(AFTER_E4).net == net);
        CHECK(explorer(masters).net == net);
        CHECK(explorer(player).net == net);
        // A game's PGN is text: any 200 is one.
        CHECK(mastersGame("aAbqI4ey", "lip_test").net == (status == 200 ? NetError::None : net));
    }
    // 404: no such game; the cloud has nothing for it, which is no failure; the tables are not where they were.
    answering(404, R"({"error":"Not found"})");
    CHECK(describe(mastersGame("missing1", "lip_test")) == "Lichess has no such game");
    CHECK_FALSE(cloudEval(AFTER_E4).failed());
    CHECK(tablebase(KK).net == NetError::NotFound);
    CHECK(explorer(player).net == NetError::NotFound);

    // Nothing listens: no answer.
    const auto nobody = fmt::format("http://127.0.0.1:{}", freePort());
    servers()         = Servers{nobody, nobody + "/tb", nobody + "/explorer"};
    CHECK(tablebase(KK).net == NetError::Offline);
    CHECK(cloudEval(AFTER_E4).net == NetError::Offline);
    CHECK(explorer(masters).net == NetError::Offline);
    CHECK(explorer(player).net == NetError::Offline);
    // Cancelled while the host pauses after a 429: nothing sent.
    HostGate::instance().setPause(60s);
    HostGate::instance().tooMany("lichess-api.invalid");
    servers() =
        Servers{"http://lichess-api.invalid", "http://lichess-api.invalid/tb", "http://lichess-api.invalid/explorer"};
    std::atomic<bool> cancel{true};
    CHECK(tablebase(KK, &cancel).net == NetError::Cancelled);
    CHECK(cloudEval(AFTER_E4, 1, &cancel).net == NetError::Cancelled);
    CHECK(explorer(masters, &cancel).net == NetError::Cancelled);
    CHECK(explorer(player, &cancel).net == NetError::Cancelled);
}
