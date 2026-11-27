// FIDE cards for the players of the databases (#57): asked by id or by name, matched, kept in the profile.
#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <json.hpp>

#include "answering_server.hpp"
#include "console.hpp"
#include "fide_cards.hpp"
#include "free_ports.hpp"
#include "https.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using namespace fcg::lichess;
using fcg::test::requestPath;
using fcg::test::ScriptedServer;
using namespace fcg::test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

TEST_CASE("FIDE cards: a player by id, a search by name, the match, and what leaves the machine") {
    std::mutex mutex;
    std::vector<std::string> asked;
    const auto port = fcg::test::freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto path = requestPath(head);
        {
            std::lock_guard lock(mutex);
            asked.push_back(path + "|" + httpHeader(head, "Authorization").value_or(""));
        }
        const auto ok = [&](const std::string& body) {
            c.sendAll(
                fmt::format("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: {}\r\n"
                            "Connection: close\r\n\r\n{}",
                            body.size(), body));
        };
        if (path == "/api/fide/player/1503014") {
            ok(R"({"id":1503014,"name":"Carlsen, Magnus","title":"GM","federation":"NOR","year":1990,)"
               R"("inactive":0,"standard":2830,"rapid":2830,"blitz":2885,"gender":"M"})");
        } else if (path == "/api/fide/player?q=Player%2C%20Sample") {
            ok(R"([{"id":11,"name":"Player, Sample","federation":"FRA","year":1980,"standard":2100},)"
               R"({"id":12,"name":"Player, Samuel","federation":"BEL","standard":1900}])");
        } else if (path == "/api/fide/player?q=Twin") {
            ok(R"([{"id":21,"name":"Twin","federation":"ESP"},{"id":22,"name":"Twin","federation":"POR"}])");
        } else {
            c.sendAll("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        }
    });
    REQUIRE(server.ok());
    const auto before = servers().api;
    servers().api     = fmt::format("http://127.0.0.1:{}", port);

    const auto card = fidePlayer(1503014);
    REQUIRE(card.value);
    CHECK(card.value->name == "Carlsen, Magnus");
    CHECK(card.value->title == "GM");
    CHECK(card.value->federation == "NOR");
    CHECK(card.value->year == 1990);
    CHECK(card.value->standard == 2830);
    CHECK(card.value->blitz == 2885);
    CHECK(card.value->asked > 0);
    const auto missing = fidePlayer(999);
    CHECK(missing.net == NetError::NotFound);
    CHECK(describe(missing) == "Lichess has no such FIDE player");

    const auto found = fideSearch("Player, Sample");
    REQUIRE(found.value);
    REQUIRE(found.value->size() == 2);
    // The one of that name, whatever its spaces and case; none when two have it, or none does.
    const auto match = fideMatch(*found.value, "player,sample");
    REQUIRE(match);
    CHECK(match->id == 11);
    CHECK_FALSE(fideMatch(*fideSearch("Twin").value, "Twin"));
    CHECK_FALSE(fideMatch(*found.value, "Somebody, Else"));
    servers().api = before;

    std::lock_guard lock(mutex);
    for (const auto& a : asked) CHECK(a.substr(a.find('|')) == "|");  // never a token
    CHECK(fideUrl(*card.value) == "https://lichess.org/fide/1503014/Carlsen_Magnus");
}

TEST_CASE("FIDE cards: kept in the profile, by id and by the names confirmed") {
    const auto file = fs::temp_directory_path() / "fide-cards" / "fide.json";
    fs::remove_all(file.parent_path());
    {
        FideCards cards(file);
        CHECK(cards.byName("Carlsen, Magnus") == nullptr);
        FideCard c;
        c.id       = 1503014;
        c.name     = "Carlsen, Magnus";
        c.title    = "GM";
        c.standard = 2830;
        c.asked    = 1795000000;
        cards.keep(c, "Carlsen,M");  // the database's way of writing it
        FideCard other;
        other.id   = 42;
        other.name = "Somebody";
        cards.keep(other);  // by its id only
        cards.save();
    }
    FideCards again(file);
    const auto* c = again.byName("carlsen m");
    REQUIRE(c != nullptr);
    CHECK(c->id == 1503014);
    CHECK(c->title == "GM");
    CHECK(c->standard == 2830);
    CHECK(c->asked == 1795000000);
    CHECK(again.byName("Carlsen, Magnus") == nullptr);  // only the names confirmed
    REQUIRE(again.byId(42) != nullptr);
    CHECK(again.byId(42)->name == "Somebody");
    CHECK(fideNameKey("Carlsen, Magnus") == "carlsenmagnus");
}

TEST_CASE("FIDE cards: each failure of Lichess's answers told by its kind") {
    AnsweringServer server;
    REQUIRE(server.ok());
    HostGate::instance().setPause(50ms);
    struct Case {
        int status;
        const char* body;
        NetError net;
    };
    for (const auto& [status, body, net] :
         {Case{401, "{}", NetError::Unauthorized}, Case{403, "{}", NetError::Forbidden},
          Case{404, R"({"error":"Not found"})", NetError::NotFound}, Case{429, "{}", NetError::RateLimited},
          Case{502, "<html>Bad gateway</html>", NetError::Server},
          Case{200, "<html>not JSON</html>", NetError::BadAnswer}, Case{200, R"({"cards":[]})", NetError::BadAnswer}}) {
        CAPTURE(status);
        server.answer(status, body);
        CHECK(fidePlayer(1503014).net == (status == 200 && body[0] == '{' ? NetError::None : net));
        CHECK(fideSearch("Carlsen").net == net);
    }
    pointNowhere();
    CHECK(fidePlayer(1503014).net == NetError::Offline);
    CHECK(fideSearch("Carlsen").net == NetError::Offline);
    pointAtPausedHost("lichess-fide.invalid");
    std::atomic<bool> cancel{true};
    CHECK(fidePlayer(1503014, &cancel).net == NetError::Cancelled);
    CHECK(fideSearch("Carlsen", &cancel).net == NetError::Cancelled);
}

TEST_CASE("FIDE cards: a card of the wrong type set aside, the others kept, said; a cache cut short too (#136)") {
    const test::ScratchDir profile("fide-cards-types");
    const auto file = profile / "lichess-cache" / "fide.json";
    {
        FideCards cards(file);
        FideCard c;
        c.id       = 1503014;
        c.name     = "Carlsen, Magnus";
        c.standard = 2830;
        c.asked    = 1795000000;
        cards.keep(c, "Carlsen,M");
        FideCard other;
        other.id    = 42;
        other.name  = "Somebody";
        other.asked = 1795000000;
        cards.keep(other);
        cards.save();
    }
    // When it was asked as a text (an older or newer version, a hand edit): it used to stop the program as soon as a
    // page read the cards.
    auto j = nlohmann::json::parse(test::readFile(file));
    for (auto& card : j["cards"])
        if (card["id"] == 42) card["asked"] = "yesterday";
    const auto written = j.dump();
    std::ofstream(file, std::ios::binary) << written;
    const auto before = Console::instance().added();
    std::optional<FideCards> back;
    REQUIRE_NOTHROW(back.emplace(file));
    REQUIRE(back->byName("Carlsen,M"));
    CHECK(back->byName("Carlsen,M")->standard == 2830);
    CHECK(back->byId(42) == nullptr);  // asked of Lichess again when it is wanted
    auto said = Console::instance().since(before);
    REQUIRE(said.size() == 1);
    CHECK(said[0].find("1 entry could not be read and was set aside") != std::string::npos);
    CHECK(test::readFile(fs::path(file).concat(".unreadable")) == written);
    // A cache cut short: kept aside, said.
    std::ofstream(file, std::ios::binary) << "{\"cards\": [{\"id\": 15030";
    const auto cut = Console::instance().added();
    CHECK(FideCards(file).byId(1503014) == nullptr);
    said = Console::instance().since(cut);
    REQUIRE(said.size() == 1);
    CHECK(said[0].find("could not be read: kept as") != std::string::npos);
    CHECK(fs::exists(fs::path(file).concat(".unreadable2")));
}
