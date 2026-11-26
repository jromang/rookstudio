// Linking a Lichess account (OAuth 2 with PKCE): the browser played by the tests, Lichess by a server on this machine.
#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include <fmt/include/fmt/format.h>

#include "free_ports.hpp"
#include "https.hpp"
#include "lichess_account.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using namespace fcg::lichess;
using namespace fcg::test;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

void answer(net::TcpConnection& c, int status, const std::string& body) {
    c.sendAll(fmt::format("HTTP/1.1 {} X\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: "
                          "close\r\n\r\n",
                          status, body.size()) +
              body);
}

// A query's value in a URL or a form, decoded enough for the tests (their values have no '+' or '%').
std::string valueOf(const std::string& text, const std::string& name) {
    const auto at = text.find(name + "=");
    if (at == std::string::npos) return {};
    const auto from = at + name.size() + 1;
    const auto end  = text.find_first_of("& \r\n", from);
    std::string v   = text.substr(from, end == std::string::npos ? std::string::npos : end - from);
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '%' && i + 2 < v.size()) {
            out += static_cast<char>(std::stoi(v.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += v[i];
        }
    }
    return out;
}

// Lichess: the token endpoint (which checks the verifier against the challenge it was shown), the account, the
// revocation.
struct FakeLichess {
    FakeLichess()
        : port(fcg::test::freePort()), server(port, [this](const std::string& head, net::TcpConnection& c) {
              // The body: what came with the head, and the rest.
              const auto split = head.find("\r\n\r\n");
              std::string body = head.substr(split + 4);
              const auto size  = head.find("Content-Length: ");
              if (size != std::string::npos) {
                  const auto n = std::stoul(head.substr(size + 16));
                  if (body.size() < n) body += c.readUntil("\x01\x02\x03", n - body.size(), 2000);
              }
              std::lock_guard lock(mutex);
              if (head.rfind("POST /api/token/test", 0) == 0) {
                  // What the token may do: the one granted may read studies; its end in milliseconds.
                  answer(c, 200,
                         body == "lio_granted"
                             ? R"({"lio_granted":{"userId":"someone","scopes":"study:read","expires":1795000000000}})"
                             : fmt::format(R"({{"{}":null}})", body));
              } else if (head.rfind("POST /api/token", 0) == 0) {
                  posted = body;
                  if (challengeOf(valueOf(body, "code_verifier")) != challenge || valueOf(body, "code") != "the-code")
                      answer(c, 400, R"({"error":"invalid_grant"})");
                  else
                      answer(c, 200, R"({"token_type":"Bearer","access_token":"lio_granted","expires_in":31536000})");
              } else if (head.rfind("GET /api/account", 0) == 0) {
                  answer(c, head.find("Bearer lio_granted") != std::string::npos && !account_refused ? 200 : 401,
                         R"({"id":"someone","username":"SomeOne"})");
              } else if (head.rfind("DELETE /api/token", 0) == 0) {
                  revoked = head.find("Bearer lio_granted") != std::string::npos;
                  c.sendAll("HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n");
              } else {
                  answer(c, 404, "{}");
              }
          }) {
        REQUIRE(server.ok());
        servers().api = fmt::format("http://127.0.0.1:{}", port);
    }
    ~FakeLichess() { servers() = Servers{}; }
    uint16_t port;
    std::mutex mutex;
    std::string challenge, posted, asked;  // asked: Lichess's page, as the browser was given it
    bool revoked         = false;
    bool account_refused = false;  // the account's name answered 401, whatever the token
    ScriptedServer server;
};

// The browser: opens the page, and comes back to the program with what Lichess gives it.
HttpResult comeBack(const std::string& redirect, const std::string& query) {
    HttpRequest request;
    request.url    = redirect + query;
    request.method = "GET";
    return httpRequest(request);
}

}  // namespace

TEST_CASE("Lichess account: the proof of the question, as RFC 7636 makes it") {
    CHECK(challengeOf("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
    const auto a = makePkce(), b = makePkce();
    CHECK(a.verifier.size() == 64);
    CHECK(a.verifier.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~") ==
          std::string::npos);
    CHECK(a.challenge == challengeOf(a.verifier));
    CHECK(a.verifier != b.verifier);
    CHECK(a.state != b.state);
    servers().api  = "https://lichess.org";
    const auto url = authorizeUrl(a, "http://127.0.0.1:5555/callback", {"preference:read", "email:read"});
    CHECK(
        url.rfind("https://lichess.org/"
                  "oauth?response_type=code&client_id=rookstudio&redirect_uri=http%3A%2F%2F127.0.0.1%3A5555%2Fcallback"
                  "&code_challenge_method=S256&code_challenge=",
                  0) == 0);
    CHECK(valueOf(url, "code_challenge") == a.challenge);
    CHECK(valueOf(url, "state") == a.state);
    CHECK(valueOf(url, "scope") == "preference:read email:read");
    CHECK(authorizeUrl(a, "x", {}).find("scope") == std::string::npos);
}

TEST_CASE("Lichess account: the program's server takes the browser's return, and only that") {
    Callback callback;
    REQUIRE(callback.port() != 0);
    CHECK(callback.redirectUri() == fmt::format("http://127.0.0.1:{}/callback", callback.port()));
    HttpResult icon, back;
    std::thread browser([&] {
        HttpRequest request;
        request.url    = fmt::format("http://127.0.0.1:{}/favicon.ico", callback.port());
        request.method = "GET";
        icon           = httpRequest(request);
        back           = comeBack(callback.redirectUri(), "?code=abc%2Fdef&state=xyz");
    });
    const auto reply = callback.wait(20s);
    browser.join();
    CHECK(icon.status == 404);
    CHECK(back.status == 200);
    CHECK(back.body.find("you can close this page") != std::string::npos);
    CHECK_FALSE(reply.failed());
    CHECK(reply.code == "abc/def");
    CHECK(reply.state == "xyz");

    // The user said no on Lichess; nobody came back in time; the wait cancelled.
    Callback refused;
    std::thread no([&] { (void)comeBack(refused.redirectUri(), "?error=access_denied&state=xyz"); });
    const auto denied = refused.wait(20s);
    no.join();
    CHECK(denied.net == NetError::Forbidden);
    CHECK(denied.status == 0);  // through the browser
    CHECK(denied.said == "access_denied");
    // Another of OAuth's errors: Lichess's word, as it is; an answer without a code: not one to read.
    Callback odd;
    std::thread other([&] { (void)comeBack(odd.redirectUri(), "?error=invalid_scope&state=xyz"); });
    const auto scope = odd.wait(20s);
    other.join();
    CHECK(scope.net == NetError::Server);
    CHECK(describe(scope) == "invalid_scope");
    Callback empty;
    std::thread nothing([&] { (void)comeBack(empty.redirectUri(), "?state=xyz"); });
    CHECK(empty.wait(20s).net == NetError::BadAnswer);
    nothing.join();
    // An answer that does not carry the question's state (another program's) neither ends the wait nor makes it look
    // refused: it is answered 400, and the answer to this question is the one taken.
    Callback ours;
    HttpResult foreign;
    std::thread both([&] {
        foreign = comeBack(ours.redirectUri(), "?error=access_denied&state=not-ours");
        (void)comeBack(ours.redirectUri(), "?code=mine&state=ours");
    });
    const auto mine = ours.wait(20s, nullptr, "ours");
    both.join();
    CHECK(foreign.status == 400);
    CHECK_FALSE(mine.failed());
    CHECK(mine.code == "mine");
    Callback idle;
    CHECK(idle.wait(30ms).net == NetError::TimedOut);
    std::atomic<bool> cancel{true};
    CHECK(idle.wait(20s, &cancel).net == NetError::Cancelled);
}

TEST_CASE("Lichess account: linked, its token kept for its owner alone, then forgotten and revoked") {
    FakeLichess lichess;
    const auto opened = [&](const std::string& url) {
        // The user says yes on Lichess's page: the browser comes back with the code and the state.
        {
            std::lock_guard lock(lichess.mutex);
            lichess.challenge = valueOf(url, "code_challenge");
            lichess.asked     = url;
        }
        const auto redirect = valueOf(url, "redirect_uri");
        const auto state    = valueOf(url, "state");
        std::thread([redirect, state] { (void)comeBack(redirect, "?code=the-code&state=" + state); }).detach();
        return true;
    };
    // Linked again with a scope, the account offered by its name.
    const auto linked = link(opened, {SCOPE_STUDY_READ}, 20s, nullptr, "SomeOne");
    REQUIRE_MESSAGE(linked.value, describe(linked));
    CHECK(linked.value->username == "SomeOne");
    CHECK(linked.value->token == "lio_granted");
    // What the token may do and until when, as Lichess says it.
    CHECK(linked.value->scopes == std::vector<std::string>{"study:read"});
    CHECK(linked.value->has(SCOPE_STUDY_READ));
    CHECK(linked.value->expires == 1795000000);
    {
        std::lock_guard lock(lichess.mutex);
        CHECK(valueOf(lichess.asked, "scope") == "study:read");
        CHECK(valueOf(lichess.asked, "username") == "SomeOne");
    }
    // A token Lichess no longer takes: no state, and no failure.
    const auto gone = testToken("lio_revoked");
    CHECK_FALSE(gone.failed());
    CHECK_FALSE(gone.value);
    {
        std::lock_guard lock(lichess.mutex);
        CHECK(valueOf(lichess.posted, "grant_type") == "authorization_code");
        CHECK(valueOf(lichess.posted, "client_id") == "rookstudio");
        CHECK(valueOf(lichess.posted, "redirect_uri").rfind("http://127.0.0.1:", 0) == 0);
    }

    const auto profile = fs::temp_directory_path() / "lichess-profile";
    fs::remove_all(profile);
    fs::create_directories(profile);
    CHECK_FALSE(loadAccount(profile));
    saveAccount(profile, *linked.value);
    const auto again = loadAccount(profile);
    REQUIRE(again);
    CHECK(again->username == "SomeOne");
    CHECK(again->token == "lio_granted");
    CHECK(again->scopes == std::vector<std::string>{"study:read"});
#ifndef _WIN32
    const auto perms = fs::status(profile / "lichess-account.json").permissions();
    CHECK((perms & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
#endif
    CHECK_FALSE(forgetAccount(profile).failed());
    CHECK(lichess.revoked);
    CHECK_FALSE(fs::exists(profile / "lichess-account.json"));
}

TEST_CASE("Lichess account: the user's no on Lichess's page told from a token Lichess refuses after a yes") {
    FakeLichess lichess;
    // The browser comes back with `query` and the question's state.
    const auto browser = [&](const std::string& query) {
        return [&lichess, query](const std::string& url) {
            {
                std::lock_guard lock(lichess.mutex);
                lichess.challenge = valueOf(url, "code_challenge");
            }
            const auto redirect = valueOf(url, "redirect_uri");
            const auto state    = valueOf(url, "state");
            std::thread([redirect, query, state] { (void)comeBack(redirect, query + "&state=" + state); }).detach();
            return true;
        };
    };
    const auto no = link(browser("?error=access_denied"), {}, 20s);
    CHECK_FALSE(no.value);
    CHECK(linkRefused(no));
    CHECK(no.net == NetError::Forbidden);
    // A yes, then a 401 on the account's name: Lichess refusing the token, not the user (#101); the token revoked.
    {
        std::lock_guard lock(lichess.mutex);
        lichess.account_refused = true;
    }
    const auto yes = link(browser("?code=the-code"), {}, 20s);
    CHECK_FALSE(yes.value);
    CHECK(yes.status == 401);
    CHECK(yes.net == NetError::Unauthorized);
    CHECK(describe(yes) == "Lichess refused the token: link the account again");
    CHECK_FALSE(linkRefused(yes));
    // A 403 Lichess answers is no refusal of the user's either: only the browser's no, without a status, is.
    Failure answered;
    answered.net    = NetError::Forbidden;
    answered.status = 403;
    CHECK_FALSE(linkRefused(answered));
    std::lock_guard lock(lichess.mutex);
    CHECK(lichess.revoked);
}

TEST_CASE("Lichess account: an answer to another question is ignored, a code Lichess does not take links nothing") {
    FakeLichess lichess;
    // An answer that does not carry the question's state (another program's, here a refusal) is answered 400: it
    // neither ends the link nor makes it look refused, and the browser's real return is the one taken.
    std::thread browser;
    HttpResult foreign;
    const auto ours = link(
        [&](const std::string& url) {
            {
                std::lock_guard lock(lichess.mutex);
                lichess.challenge = valueOf(url, "code_challenge");
            }
            const auto redirect = valueOf(url, "redirect_uri");
            const auto state    = valueOf(url, "state");
            browser             = std::thread([&foreign, redirect, state] {
                foreign = comeBack(redirect, "?error=access_denied&state=someone-elses");
                (void)comeBack(redirect, "?code=the-code&state=" + state);
            });
            return true;
        },
        {}, 20s);
    browser.join();
    CHECK(foreign.status == 400);
    CHECK_FALSE(ours.failed());
    CHECK(ours.value);

    const auto stale = link(
        [&](const std::string& url) {
            {
                std::lock_guard lock(lichess.mutex);
                lichess.challenge = valueOf(url, "code_challenge");
            }
            const auto redirect = valueOf(url, "redirect_uri");
            const auto state    = valueOf(url, "state");
            std::thread([redirect, state] { (void)comeBack(redirect, "?code=old-code&state=" + state); }).detach();
            return true;
        },
        {}, 20s);
    CHECK_FALSE(stale.value);
    CHECK(stale.net == NetError::Server);
    CHECK(stale.status == 400);
    CHECK(describe(stale) == "Lichess did not take the code");
}

TEST_CASE("Lichess account: each failure of the token's requests told by its kind") {
    std::mutex mutex;
    std::pair<int, std::string> next{200, "{}"};  // what the server answers next
    const auto port = freePort();
    ScriptedServer server(port, [&](const std::string&, net::TcpConnection& c) {
        std::pair<int, std::string> now;
        {
            std::lock_guard lock(mutex);
            now = next;
        }
        answer(c, now.first, now.second);
    });
    REQUIRE(server.ok());
    servers().api = fmt::format("http://127.0.0.1:{}", port);
    HostGate::instance().setPause(50ms);
    const auto answering = [&](int status, std::string body) {
        std::lock_guard lock(mutex);
        next = {status, std::move(body)};
    };
    struct Case {
        int status;
        const char* body;
        NetError net;
    };
    for (const auto& [status, body, net] :
         {Case{403, R"({"error":"Missing scope"})", NetError::Forbidden}, Case{404, "{}", NetError::NotFound},
          Case{429, "{}", NetError::RateLimited}, Case{503, "<html>down</html>", NetError::Server},
          Case{200, "<html>not JSON</html>", NetError::BadAnswer}}) {
        CAPTURE(status);
        answering(status, body);
        const auto name = accountName("lio_x");
        CHECK_FALSE(name.value);
        CHECK(name.net == net);
        const auto tested = testToken("lio_x");
        CHECK_FALSE(tested.value);
        CHECK(tested.net == net);
        // A revocation reads no body: a 200 is taken, whatever it says.
        CHECK(revoke("lio_x").net == (status == 200 ? NetError::None : net));
    }
    answering(401, R"({"error":"No such token"})");
    CHECK(accountName("lio_x").net == NetError::Unauthorized);
    CHECK(describe(revoke("lio_x")) == "Lichess refused the token: link the account again");
    // Nothing listens: no answer.
    servers().api      = fmt::format("http://127.0.0.1:{}", freePort());
    const auto offline = accountName("lio_x");
    CHECK(offline.net == NetError::Offline);
    CHECK(offline.status == 0);
    CHECK(revoke("lio_x").net == NetError::Offline);
    // Cancelled while the host pauses after a 429: nothing sent.
    HostGate::instance().setPause(60s);
    HostGate::instance().tooMany("lichess-account.invalid");
    servers().api = "http://lichess-account.invalid";
    std::atomic<bool> cancel{true};
    CHECK(accountName("lio_x", &cancel).net == NetError::Cancelled);
    CHECK(testToken("lio_x", &cancel).net == NetError::Cancelled);
    servers() = Servers{};
}
