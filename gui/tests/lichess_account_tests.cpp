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
              if (head.rfind("POST /api/token", 0) == 0) {
                  posted = body;
                  if (challengeOf(valueOf(body, "code_verifier")) != challenge || valueOf(body, "code") != "the-code")
                      answer(c, 400, R"({"error":"invalid_grant"})");
                  else
                      answer(c, 200, R"({"token_type":"Bearer","access_token":"lio_granted","expires_in":31536000})");
              } else if (head.rfind("GET /api/account", 0) == 0) {
                  answer(c, head.find("Bearer lio_granted") != std::string::npos ? 200 : 401,
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
    std::string challenge, posted;
    bool revoked = false;
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
    CHECK(reply.error.empty());
    CHECK(reply.code == "abc/def");
    CHECK(reply.state == "xyz");

    // The user said no on Lichess; nobody came back in time; the wait cancelled.
    Callback refused;
    std::thread no([&] { (void)comeBack(refused.redirectUri(), "?error=access_denied&state=xyz"); });
    CHECK(refused.wait(20s).error == "access_denied");
    no.join();
    Callback idle;
    CHECK(idle.wait(30ms).error == "no answer came from Lichess in time");
    std::atomic<bool> cancel{true};
    CHECK(idle.wait(20s, &cancel).error == "cancelled");
}

TEST_CASE("Lichess account: linked, its token kept for its owner alone, then forgotten and revoked") {
    FakeLichess lichess;
    const auto opened = [&](const std::string& url) {
        // The user says yes on Lichess's page: the browser comes back with the code and the state.
        {
            std::lock_guard lock(lichess.mutex);
            lichess.challenge = valueOf(url, "code_challenge");
        }
        const auto redirect = valueOf(url, "redirect_uri");
        const auto state    = valueOf(url, "state");
        std::thread([redirect, state] { (void)comeBack(redirect, "?code=the-code&state=" + state); }).detach();
        return true;
    };
    const auto linked = link(opened, {}, 20s);
    REQUIRE_MESSAGE(linked.value, linked.error);
    CHECK(linked.value->username == "SomeOne");
    CHECK(linked.value->token == "lio_granted");
    CHECK(linked.value->expires > 1700000000);
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
#ifndef _WIN32
    const auto perms = fs::status(profile / "lichess-account.json").permissions();
    CHECK((perms & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
#endif
    CHECK(forgetAccount(profile).empty());
    CHECK(lichess.revoked);
    CHECK_FALSE(fs::exists(profile / "lichess-account.json"));
}

TEST_CASE("Lichess account: an answer to another question, or a code Lichess does not take, links nothing") {
    FakeLichess lichess;
    const auto wrong_state = link(
        [&](const std::string& url) {
            const auto redirect = valueOf(url, "redirect_uri");
            std::thread([redirect] { (void)comeBack(redirect, "?code=the-code&state=someone-elses"); }).detach();
            return true;
        },
        {}, 20s);
    CHECK_FALSE(wrong_state.value);
    CHECK(wrong_state.error == "an answer that does not come from this question");

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
    CHECK(stale.error == "Lichess did not take the code");
}
