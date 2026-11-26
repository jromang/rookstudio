// The one way to ask Lichess (#106): a failure's kind from its status, Lichess's words, the user's words for each kind,
// and a request sent and read against a server on this machine that answers as Lichess does when it refuses.
#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <map>
#include <optional>
#include <string>

#include <fmt/include/fmt/format.h>

#include "free_ports.hpp"
#include "https.hpp"
#include "lichess_api.hpp"
#include "lichess_http.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using namespace fcg::lichess;
using namespace fcg::test;
using namespace std::chrono_literals;

namespace {

void answer(net::TcpConnection& c, int status, const std::string& body) {
    c.sendAll(fmt::format(
        "HTTP/1.1 {} X\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", status,
        body.size(), body));
}

HttpResult result(int status, std::string body = {}, std::string error = {}) {
    HttpResult r;
    r.status = status;
    r.body   = std::move(body);
    r.error  = std::move(error);
    return r;
}

struct Rating {
    int value = 0;
};

Rating ratingOf(const nlohmann::json& j) { return {j.at("rating").get<int>()}; }

}  // namespace

TEST_CASE("Lichess HTTP: a failure's kind from the status, or from no answer") {
    CHECK(netErrorOf(result(200, "{}")) == NetError::None);
    CHECK(netErrorOf(result(204)) == NetError::None);
    CHECK(netErrorOf(result(401)) == NetError::Unauthorized);
    CHECK(netErrorOf(result(403)) == NetError::Forbidden);
    CHECK(netErrorOf(result(404)) == NetError::NotFound);
    CHECK(netErrorOf(result(429)) == NetError::RateLimited);
    CHECK(netErrorOf(result(400)) == NetError::Server);
    CHECK(netErrorOf(result(500)) == NetError::Server);
    CHECK(netErrorOf(result(503)) == NetError::Server);
    CHECK(netErrorOf(result(302)) == NetError::Server);
    // No answer: the network's; stopped by the program: nothing to say; a stream cut short: the network's too.
    CHECK(netErrorOf(result(0, {}, "no answer from lichess.org in time")) == NetError::Offline);
    CHECK(netErrorOf(result(0)) == NetError::Offline);
    CHECK(netErrorOf(result(0, {}, "cancelled")) == NetError::Cancelled);
    CHECK(netErrorOf(result(200, {}, "cancelled")) == NetError::Cancelled);
    CHECK(netErrorOf(result(200, "{\"a\":", "the answer of lichess.org was cut short")) == NetError::Offline);
    CHECK(std::string(netName(NetError::RateLimited)) == "RateLimited");
}

TEST_CASE("Lichess HTTP: Lichess's own words in a refusal") {
    CHECK(lichessWords(R"({"error":"Not your turn, or game already over"})") == "Not your turn, or game already over");
    CHECK(lichessWords(R"({"error":{"name":["Too short","Bad"],"maxThreads":["Must be at least 1"]}})") ==
          "maxThreads: Must be at least 1; name: Too short Bad");
    CHECK(lichessWords(R"({"global":["This study is full"]})") == "This study is full");
    CHECK(lichessWords(R"({"ok":false})").empty());
    CHECK(lichessWords("<html>Bad gateway</html>").empty());
    CHECK(lichessWords("").empty());
    CHECK(lichessWords("[1,2]").empty());
}

TEST_CASE("Lichess HTTP: the user's words for each kind, by what was asked") {
    CHECK(describe(NetError::None).empty());
    CHECK(describe(NetError::Cancelled) == "cancelled");
    CHECK(describe(NetError::Offline) == "Lichess cannot be reached");
    CHECK(describe(NetError::TimedOut) == "no answer came from Lichess in time");
    CHECK(describe(NetError::Unauthorized) == "Lichess refused the token: link the account again");
    CHECK(describe(NetError::Forbidden) == "the token may not do it: one more permission is needed");
    CHECK(describe(NetError::NotFound) == "Lichess does not know it");
    CHECK(describe(NetError::RateLimited) == "Lichess asks to wait a minute: too many requests");
    CHECK(describe(NetError::Server, Asked::Anything, 503) == "Lichess answered 503");
    CHECK(describe(NetError::Server, Asked::Anything, 400, "This game is over") == "This game is over");
    CHECK(describe(NetError::BadAnswer) == "Lichess's answer could not be read");
    // What Lichess may not have.
    CHECK(describe(NetError::NotFound, Asked::Game) == "Lichess has no such game");
    CHECK(describe(NetError::NotFound, Asked::Study) == "Lichess has no such study, or it is private");
    CHECK(describe(NetError::NotFound, Asked::Chapter) == "Lichess has no such chapter any more");
    CHECK(describe(NetError::NotFound, Asked::Player) == "Lichess has no account of that name");
    CHECK(describe(NetError::NotFound, Asked::FidePlayer) == "Lichess has no such FIDE player");
    CHECK(describe(NetError::NotFound, Asked::Page) == "Lichess has nothing at this address");
    // Who may read what a link names: the same words for 401 and 403.
    for (const auto net : {NetError::Unauthorized, NetError::Forbidden}) {
        CHECK(describe(net, Asked::Page) == "Lichess refuses it: private, or for another account");
        CHECK(describe(net, Asked::Broadcast) == "Lichess refuses this without an account allowed to read broadcasts");
        CHECK(describe(net, Asked::Bookmarks) == "Lichess's bookmarks need a linked account");
    }
    CHECK(describe(NetError::Forbidden, Asked::Engines) == "the token may not register engines (engine:write)");
    CHECK(describe(NetError::BadAnswer, Asked::Engines) ==
          "Lichess's answer could not be read: the external engine API may have changed");
    // A link: the user's no (no status: it came through the browser), a code Lichess did not take, its own error.
    CHECK(describe(NetError::Forbidden, Asked::Link) == "the link was refused on Lichess");
    CHECK(describe(NetError::Forbidden, Asked::Link, 403) == "the token may not do it: one more permission is needed");
    CHECK(describe(NetError::Server, Asked::Link, 400, "invalid_grant") == "Lichess did not take the code");
    CHECK(describe(NetError::Server, Asked::Link, 0, "invalid_scope") == "invalid_scope");
    // A failure as it was kept: its kind's words, or the program's own words.
    Failure kept;
    CHECK(describe(kept).empty());
    CHECK_FALSE(kept.failed());
    kept.net   = NetError::NotFound;
    kept.asked = Asked::Study;
    CHECK(kept.failed());
    CHECK(describe(kept) == "Lichess has no such study, or it is private");
    Failure own;
    own.error = "a study's name has 2 to 100 characters";
    CHECK(own.failed());
    CHECK(describe(own) == "a study's name has 2 to 100 characters");
}

TEST_CASE("Lichess HTTP: one request builder, a form or JSON for its body") {
    const auto get = request("GET", "https://lichess.org/api/account", "lip_x");
    CHECK(get.method == "GET");
    CHECK(get.url == "https://lichess.org/api/account");
    CHECK(get.token == "lip_x");
    REQUIRE(get.headers.size() == 1);
    CHECK(get.headers[0] == std::pair<std::string, std::string>{"Accept", "application/json"});
    CHECK(get.body.empty());
    const auto pgn = request("GET", "https://explorer.lichess.ovh/masters/pgn/x", {}, "application/x-chess-pgn");
    CHECK(pgn.token.empty());
    CHECK(pgn.headers[0].second == "application/x-chess-pgn");
    const auto form = withForm(request("POST", "https://lichess.org/api/import"), {{"pgn", "1. e4 *"}});
    CHECK(form.content_type == "application/x-www-form-urlencoded");
    CHECK(form.body == "pgn=1.+e4+%2A");
    const auto json = withJson(request("POST", "https://lichess.org/api/external-engine"), {{"name", "Stockfish"}});
    CHECK(json.content_type == "application/json");
    CHECK(json.body == R"({"name":"Stockfish"})");
}

TEST_CASE("Lichess HTTP: a request sent and read, each failure with its kind") {
    const auto port = freePort();
    // Each path answers as Lichess does: a value, or a refusal with its words.
    static const std::map<std::string, std::pair<int, std::string>> ANSWERS = {
        {"/rating", {200, R"({"rating":1850})"}},
        {"/token", {401, R"({"error":"No such token"})"}},
        {"/scope", {403, R"({"error":"Missing scope"})"}},
        {"/missing", {404, R"({"error":"Not found"})"}},
        {"/busy", {429, R"({"error":"Too many requests. Try again later."})"}},
        {"/refused", {400, R"({"error":"This tournament is finished"})"}},
        {"/down", {502, "<html>Bad gateway</html>"}},
        {"/garbage", {200, "<html>Not JSON</html>"}},
        {"/other", {200, R"({"name":"x"})"}},
    };
    ScriptedServer server(port, [](const std::string& head, net::TcpConnection& c) {
        const auto it = ANSWERS.find(requestPath(head));
        if (it == ANSWERS.end())
            answer(c, 500, "{}");
        else
            answer(c, it->second.first, it->second.second);
    });
    REQUIRE(server.ok());
    HostGate::instance().setPause(50ms);
    const auto base = fmt::format("http://127.0.0.1:{}", port);
    const auto get  = [&](const char* path) { return request("GET", base + path); };

    const auto rating = call<Rating>(get("/rating"), ratingOf);
    REQUIRE_MESSAGE(rating.value, describe(rating));
    CHECK(rating.value->value == 1850);
    CHECK(rating.net == NetError::None);
    CHECK(rating.status == 200);
    CHECK_FALSE(rating.failed());

    const auto token = call<Rating>(get("/token"), ratingOf);
    CHECK_FALSE(token.value);
    CHECK(token.net == NetError::Unauthorized);
    CHECK(token.status == 401);
    CHECK(token.said == "No such token");
    CHECK(describe(token) == "Lichess refused the token: link the account again");  // our words, not Lichess's

    CHECK(call<Rating>(get("/scope"), ratingOf).net == NetError::Forbidden);
    CHECK(call<Rating>(get("/missing"), ratingOf).net == NetError::NotFound);

    const auto busy = call<Rating>(get("/busy"), ratingOf);
    CHECK(busy.net == NetError::RateLimited);
    CHECK(busy.status == 429);
    CHECK(HostGate::instance().pausedFor("127.0.0.1") > 0ms);  // the host waits its pause

    // Lichess's own words for a refusal NetError does not name.
    const auto refused = call<Rating>(get("/refused"), ratingOf);
    CHECK(refused.net == NetError::Server);
    CHECK(describe(refused) == "This tournament is finished");
    const auto down = call<Rating>(get("/down"), ratingOf);
    CHECK(down.net == NetError::Server);
    CHECK(down.said.empty());
    CHECK(describe(down) == "Lichess answered 502");

    // An answer that is not what Lichess sends: not JSON, or not its fields (the reader throws, or gives none).
    const auto garbage = call<Rating>(get("/garbage"), ratingOf);
    CHECK(garbage.net == NetError::BadAnswer);
    CHECK(garbage.status == 200);
    CHECK_FALSE(garbage.detail.empty());  // for the log
    CHECK(describe(garbage) == "Lichess's answer could not be read");
    CHECK(call<Rating>(get("/other"), ratingOf).net == NetError::BadAnswer);
    const auto none = call<Rating>(get("/other"), [](const nlohmann::json& j) -> std::optional<Rating> {
        if (!j.contains("rating")) return std::nullopt;
        return ratingOf(j);
    });
    CHECK(none.net == NetError::BadAnswer);
    CHECK_FALSE(none.value);

    // No answer: nothing listens there.
    const auto nobody = call<Rating>(request("GET", fmt::format("http://127.0.0.1:{}/rating", freePort())), ratingOf);
    CHECK(nobody.net == NetError::Offline);
    CHECK(nobody.status == 0);
    CHECK_FALSE(nobody.detail.empty());  // the network's words, for the log
    CHECK(describe(nobody) == "Lichess cannot be reached");

    // Cancelled while its host pauses after a 429: sent nowhere, nothing to say.
    HostGate::instance().setPause(60s);
    HostGate::instance().tooMany("lichess-http.invalid");
    std::atomic<bool> cancel{true};
    const auto cancelled = call<Rating>(request("GET", "http://lichess-http.invalid/rating"), ratingOf, &cancel);
    CHECK(cancelled.net == NetError::Cancelled);
    CHECK(cancelled.status == 0);
    const auto sent = send(request("GET", "http://lichess-http.invalid/rating"), &cancel);
    CHECK(sent.net == NetError::Cancelled);
    CHECK(sent.asked_for == "GET http://lichess-http.invalid/rating");
}

TEST_CASE("Lichess HTTP: a failure taken from another request's answer") {
    Answer<int> first;
    first.net         = NetError::RateLimited;
    first.status      = 429;
    first.said        = "Too many requests";
    const auto second = failedWith<std::string>(first);
    CHECK_FALSE(second.value);
    CHECK(second.net == NetError::RateLimited);
    CHECK(second.status == 429);
    CHECK(second.said == "Too many requests");
}
