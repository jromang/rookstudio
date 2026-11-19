// The linked account's Lichess on the Home page (#46): its ratings and their history, its activity, a speed's figures,
// what a speed did lately, all kept a day in the profile.
#include <doctest/doctest.hpp>

#include <filesystem>

#include <json.hpp>

#include "lichess_profile.hpp"

using namespace fcg::lichess;
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
