// From the Board to lichess.org (#54): the analysis board's address, a game imported, a position played out, a game
// bookmarked; what each sends.
#include <doctest/doctest.hpp>

#include <mutex>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>

#include "free_ports.hpp"
#include "https.hpp"
#include "lichess_share.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using namespace fcg::lichess;
using fcg::test::requestPath;
using fcg::test::ScriptedServer;

namespace {

std::string reply(int status, const std::string& body) {
    return fmt::format(
        "HTTP/1.1 {} X\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n"
        "\r\n{}",
        status, body.size(), body);
}

std::string formValue(const std::string& body, const std::string& name) {
    const auto at = ("&" + body).find("&" + name + "=");
    if (at == std::string::npos) return "<none>";
    const auto from = at + name.size() + 1;
    return body.substr(from, body.find('&', from) - from);
}

}  // namespace

TEST_CASE("lichess share: the analysis board's address, nothing but the position in it") {
    const std::string fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1";
    CHECK(analysisUrl(fen, false, false) ==
          "https://lichess.org/analysis/standard/rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR_b_KQkq_-_0_1");
    CHECK(analysisUrl(fen, true, true) ==
          "https://lichess.org/analysis/chess960/rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR_b_KQkq_-_0_1"
          "?color=black");
    CHECK(clockText({10, 5, 0}) == "10+5");
    CHECK(clockText({0, 0, 3}) == "3 days a move");
}

TEST_CASE("lichess share: a game imported without an account, a position played out, a game bookmarked") {
    struct Seen {
        std::string line, authorization, body;
    };
    std::mutex mutex;
    std::vector<Seen> seen;
    const auto port = fcg::test::freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        Seen s;
        s.line            = head.substr(0, head.find("\r\n"));
        s.authorization   = httpHeader(head, "Authorization").value_or("");
        const auto length = std::stoul(httpHeader(head, "Content-Length").value_or("0"));
        s.body            = head.substr(head.find("\r\n\r\n") + 4);
        while (s.body.size() < length) {
            const auto more = c.readUntil("\x01never", length - s.body.size(), 2000);
            if (more.empty()) break;
            s.body += more;
        }
        {
            std::lock_guard lock(mutex);
            seen.push_back(s);
        }
        const auto path = requestPath(head);
        if (path == "/api/import") {
            if (s.body.find("pgn=") == std::string::npos)
                c.sendAll(reply(400, R"({"error":"Invalid PGN"})"));
            else
                c.sendAll(reply(200, R"({"id":"Ab12Cd34","url":"https://lichess.org/game/Ab12Cd34"})"));
        } else if (path == "/api/challenge/open") {
            if (formValue(s.body, "variant") == "chess960")
                c.sendAll(reply(400, R"({"error":"Invalid position for chess960"})"));
            else
                c.sendAll(reply(200, R"({"id":"XyZ98765","url":"https://lichess.org/XyZ98765","status":"created",)"
                                     R"("urlWhite":"https://lichess.org/XyZ98765?color=white",)"
                                     R"("urlBlack":"https://lichess.org/XyZ98765?color=black"})"));
        } else if (path.rfind("/bookmark/game0001", 0) == 0) {
            c.sendAll(s.authorization == "Bearer allowed" ? "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n"
                                                          : reply(403, R"({"error":"Missing scope"})"));
        } else {
            c.sendAll(reply(404, "{}"));
        }
    });
    REQUIRE(server.ok());
    const auto before = servers().api;
    servers().api     = fmt::format("http://127.0.0.1:{}", port);

    // Imported: the PGN only, no token, the address given back.
    const auto pgn      = std::string("[Event \"Club\"]\n[White \"A\"]\n[Black \"B\"]\n\n1. e4 e5 *\n");
    const auto imported = importGame(pgn);
    REQUIRE(imported.value);
    CHECK(*imported.value == "https://lichess.org/game/Ab12Cd34");
    // Played out: the position, the clock, not rated, no token; a link for each side.
    const std::string fen = "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2";
    const auto played     = openChallenge(fen, false, {5, 3, 0});
    REQUIRE(played.value);
    CHECK(played.value->white == "https://lichess.org/XyZ98765?color=white");
    CHECK(played.value->black == "https://lichess.org/XyZ98765?color=black");
    CHECK(played.value->url == "https://lichess.org/XyZ98765");
    const auto correspondence = openChallenge(fen, false, {0, 0, 3});
    CHECK(correspondence.value);
    // Lichess's reason when it refuses.
    const auto refused = openChallenge(fen, true, {5, 3, 0});
    CHECK_FALSE(refused.value);
    CHECK(refused.error == "Invalid position for chess960");
    // Bookmarked: the game's id with the token; refused without the permission.
    const auto marked = bookmarkGame("game0001", "allowed");
    REQUIRE(marked.value);
    CHECK(*marked.value);
    const auto denied = bookmarkGame("game0001", "other");
    CHECK_FALSE(denied.value);
    CHECK(denied.status == 403);
    servers().api = before;

    std::lock_guard lock(mutex);
    REQUIRE(seen.size() == 6);
    CHECK(seen[0].line == "POST /api/import HTTP/1.1");
    CHECK(seen[0].authorization.empty());
    CHECK(seen[0].body == formEncode({{"pgn", pgn}}));
    CHECK(seen[1].line == "POST /api/challenge/open HTTP/1.1");
    CHECK(seen[1].authorization.empty());
    CHECK(formValue(seen[1].body, "fen") == formValue(formEncode({{"fen", fen}}), "fen"));
    CHECK(formValue(seen[1].body, "clock.limit") == "300");
    CHECK(formValue(seen[1].body, "clock.increment") == "3");
    CHECK(formValue(seen[1].body, "rated") == "false");
    CHECK(formValue(seen[1].body, "days") == "<none>");
    CHECK(formValue(seen[2].body, "days") == "3");
    CHECK(formValue(seen[2].body, "clock.limit") == "<none>");
    CHECK(seen[4].line == "POST /bookmark/game0001?v=true HTTP/1.1");
    CHECK(seen[4].authorization == "Bearer allowed");
    CHECK(seen[4].body.empty());
}
