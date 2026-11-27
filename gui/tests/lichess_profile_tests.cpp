// The linked account's Lichess on the Home page (#46): its ratings and their history, its activity, a speed's figures,
// what a speed did lately, all kept a day in the profile; read again when ten minutes old, the kept copy's age counting
// (#111).
#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>

#include <json.hpp>

#include "answering_server.hpp"
#include "jobs.hpp"
#include "lichess_profile.hpp"
#include "refreshing.hpp"
#include "test_support.hpp"

using namespace fcg::lichess;
using namespace fcg::test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

Profile sample() {
    Profile p;
    readAccount(nlohmann::json::parse(R"({"username": "MyAccount", "perfs": {
        "bullet": {"games": 412, "rating": 1987, "rd": 45, "prog": -8},
        "blitz": {"games": 1268, "rating": 2143, "prog": 12},
        "classical": {"games": 2, "rating": 1500, "prov": true},
        "puzzle": {"games": 900, "rating": 2200}}})"),
                p);
    return p;
}

}  // namespace

TEST_CASE("Lichess profile: the account's ratings, the five speeds in their order") {
    const auto p = sample();
    CHECK(p.username == "MyAccount");
    REQUIRE(p.speeds.size() == SPEEDS.size());
    CHECK(p.speeds[0].key == "bullet");
    CHECK(p.speeds[0].rating == 1987);
    CHECK(p.speeds[1].games == 1268);
    CHECK(p.speeds[2].key == "rapid");  // none: zero games
    CHECK(p.speeds[2].games == 0);
    CHECK(p.speeds[3].provisional);
}

TEST_CASE("Lichess profile: the rating history, Lichess's months counted from 0, oldest first") {
    auto p = sample();
    readRatingHistory(nlohmann::json::parse(R"([
        {"name": "Blitz", "points": [[2026, 10, 18, 2143], [2026, 10, 1, 2100], [2026, 9, 31, 2090]]},
        {"name": "Puzzles", "points": [[2026, 10, 18, 2200]]},
        {"name": "Bullet", "points": [[2026, 0, 1, "x"], [2026, 0, 1, 1960]]}])"),
                      p);
    const auto& blitz = p.speeds[1].history;
    REQUIRE(blitz.size() == 3);
    // 2026-10-31 (October: Lichess's 9), then 2026-11-01 and 2026-11-18.
    CHECK(blitz[0].rating == 2090);
    CHECK(blitz[1].day == blitz[0].day + 1);
    CHECK(blitz[2].day == blitz[1].day + 17);
    CHECK(p.speeds[0].history.size() == 1);  // a point that is not one left out

    // What blitz did lately: a week before, 2100 (its last game then).
    const int64_t today = blitz[2].day + 1;  // 2026-11-19
    const auto week     = progressOf(p.speeds[1], today);
    CHECK(week.known);
    CHECK(week.week);
    CHECK(week.delta == 43);
    // A provisional rating, or no game in the month: nothing said.
    CHECK_FALSE(progressOf(p.speeds[3], today).known);
    CHECK_FALSE(progressOf(p.speeds[1], today + 60).known);
}

TEST_CASE("Lichess profile: the activity of the last days, newest first, empty days left out") {
    Profile p;
    readActivity(nlohmann::json::parse(R"([
        {"interval": {"start": 1000, "end": 2000}, "games": {"blitz": {"win": 3, "loss": 2, "draw": 1}}},
        {"interval": {"start": 3000, "end": 4000}, "puzzles": {"score": {"win": 11, "loss": 3}},
         "games": {"rapid": {"win": 2}}, "studies": [{"id": "a"}]},
        {"interval": {"start": 5000, "end": 6000}, "follows": {}}])"),
                 p);
    REQUIRE(p.activity.size() == 2);
    CHECK(p.activity[0].start == 3000);
    CHECK(p.activity[0].puzzles_win == 11);
    CHECK(p.activity[0].studies == 1);
    REQUIRE(p.activity[1].games.size() == 1);
    CHECK(p.activity[1].games[0] == ActivityGames{"blitz", 3, 1, 2});
}

TEST_CASE("Lichess profile: a speed's figures") {
    const auto f = readPerfFigures(nlohmann::json::parse(R"({"stat": {
        "highest": {"int": 2156, "at": "2026-10-28T19:43:41.546Z"},
        "bestWins": {"results": [{"opRating": 2250}, {"opRating": 2301}]},
        "resultStreak": {"win": {"cur": {"v": 2}, "max": {"v": 9}}},
        "count": {"win": 611, "draw": 102, "loss": 555, "seconds": 219600}}})"));
    CHECK(f.highest == 2156);
    CHECK(f.highest_at == "2026-10-28");
    CHECK(f.best_win == 2301);
    CHECK(f.streak == 9);
    CHECK(f.win == 611);
    CHECK(f.seconds == 219600);
    CHECK(readPerfFigures(nlohmann::json::array()) == PerfFigures{});
}

TEST_CASE("Lichess profile: kept in the profile and read back") {
    auto p    = sample();
    p.read_at = 1795000000;
    readRatingHistory(nlohmann::json::parse(R"([{"name": "Blitz", "points": [[2026, 10, 18, 2143]]}])"), p);
    readActivity(nlohmann::json::parse(R"([{"interval": {"start": 3000}, "games": {"rapid": {"win": 2}}}])"), p);
    const auto root = fs::temp_directory_path() / "fcg-lichess-profile";
    fs::remove_all(root);
    CHECK_FALSE(keptProfile(root));
    keepProfile(root, p);
    const auto back = keptProfile(root);
    REQUIRE(back);
    CHECK(*back == p);
    fs::remove_all(root);
}

TEST_CASE("Lichess profile: an account's, a player's and a speed's figures asked, each failure told by its kind") {
    AnsweringServer server;
    REQUIRE(server.ok());
    fcg::HostGate::instance().setPause(50ms);
    server.answer(200, R"({"username": "MyAccount", "perfs": {"blitz": {"games": 1268, "rating": 2143}}})");
    const auto account = accountProfile("tok");
    REQUIRE_MESSAGE(account.value, describe(account));
    CHECK(account.value->username == "MyAccount");
    CHECK(account.value->read_at > 0);
    CHECK(publicProfile("MyAccount").value);
    CHECK(perfFigures("MyAccount", "blitz", "tok").value);
    struct Case {
        int status;
        const char* body;
        NetError net;
    };
    for (const auto& [status, body, net] :
         {Case{401, "{}", NetError::Unauthorized}, Case{403, R"({"error":"Missing scope"})", NetError::Forbidden},
          Case{404, R"({"error":"Not found"})", NetError::NotFound}, Case{429, "{}", NetError::RateLimited},
          Case{503, "<html>down</html>", NetError::Server}}) {
        CAPTURE(status);
        server.answer(status, body);
        CHECK(accountProfile("tok").net == net);
        CHECK(publicProfile("Nobody").net == net);
        CHECK(perfFigures("MyAccount", "blitz", "tok").net == net);
    }
    server.answer(404, R"({"error":"Not found"})");
    CHECK(describe(publicProfile("Nobody")) == "Lichess has no account of that name");
    // Not what Lichess sends: not JSON, or an account without its name.
    server.answer(200, "<html>not JSON</html>");
    CHECK(accountProfile("tok").net == NetError::BadAnswer);
    CHECK(perfFigures("MyAccount", "blitz", "tok").net == NetError::BadAnswer);
    server.answer(200, R"({"perfs": {}})");
    CHECK(publicProfile("Nobody").net == NetError::BadAnswer);
    pointNowhere();
    CHECK(accountProfile("tok").net == NetError::Offline);
    CHECK(describe(publicProfile("Nobody")) == "Lichess cannot be reached");
    pointAtPausedHost("lichess-profile.invalid");
    std::atomic<bool> cancel{true};
    CHECK(accountProfile("tok", &cancel).net == NetError::Cancelled);
    CHECK(perfFigures("MyAccount", "blitz", "tok", &cancel).net == NetError::Cancelled);
    servers() = Servers{};
}

TEST_CASE(
    "Lichess profile: read again when ten minutes old, once in ten minutes at most, the kept copy's age counting") {
    // As App holds it (app.hpp, #111): the Home page's card, the Play page's line, the arenas' conditions and the
    // puzzles' rating ask it alike, with a clock of the test's own here.
    using Clock = std::chrono::steady_clock;
    fcg::JobGroup group;
    auto now = Clock::time_point{} + 1h;
    fcg::Refreshing<Profile> profile(
        group, PROFILE_EVERY, fcg::Refresh::WhenStale, [&] { return now; }, [] { return Clock::duration(60s); });
    const auto read = [](int64_t at) {
        Profile p;
        p.username      = "RS_Sample";
        p.read_at       = at;
        p.puzzle_rating = 1810;
        return p;
    };
    const auto offline = [] {
        Answer<Profile> a;
        a.net = NetError::Offline;
        return a;
    };
    // Its answer waited for and taken: whether a new profile came.
    const auto taken = [&] {
        bool fresh = false;
        REQUIRE(waitUntil(
            [&] {
                fresh = profile.poll() || fresh;
                return !profile.asking();
            },
            60s));
        return fresh;
    };
    // The program starts, the copy kept on disk read five minutes ago: shown, Lichess not asked.
    const int64_t wall = 1'800'000'000;
    const auto kept    = read(wall - 300);
    CHECK(profileAge(kept, wall) == 5min);
    profile.keep(kept, profileAge(kept, wall));
    CHECK_FALSE(profile.due());
    now += 5min - 1s;
    CHECK_FALSE(profile.due());
    now += 1s;  // ten minutes old
    REQUIRE(profile.due());
    // Asked, Lichess unreachable: what was kept stays shown, why said, and Lichess is not asked again for ten minutes,
    // however stale it is.
    profile.ask(offline);
    CHECK_FALSE(profile.due());  // under way
    CHECK_FALSE(taken());
    REQUIRE(profile.value);
    CHECK(profile.value->read_at == wall - 300);
    CHECK(profile.failed());
    now += 10min - 1s;
    CHECK(profile.stale());
    CHECK_FALSE(profile.due());
    now += 1s;
    REQUIRE(profile.due());
    // Read: shown for ten minutes, then asked again.
    profile.ask([read, wall] {
        Answer<Profile> a;
        a.value = read(wall + 1200);
        return a;
    });
    CHECK(taken());
    CHECK(profile.value->read_at == wall + 1200);
    CHECK_FALSE(profile.failed());
    now += 10min - 1s;
    CHECK_FALSE(profile.due());
    now += 1s;
    CHECK(profile.due());
    // Another account: forgotten, asked at once.
    profile.reset();
    CHECK_FALSE(profile.value);
    CHECK(profile.due());
    // A copy kept an hour ago: asked at once.
    const auto old = read(wall - 3600);
    profile.keep(old, profileAge(old, wall));
    CHECK(profile.due());
}
