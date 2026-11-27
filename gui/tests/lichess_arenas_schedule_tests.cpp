// When the arenas' module asks Lichess (lichess_arenas_schedule.hpp, #111), driven by a clock of the test's own: the
// card's list every minute while the card shows, the list of before kept when a refresh fails; the user's teams' Swiss
// tournaments every five minutes, at once with another token; the followed tournament's standing every 15 s (a Swiss's
// every 30 s) while the mosaic draws it, the games of its first eight; the arena played, its page every 15 s and its
// joins again; the Swiss played or looked at. One request at a time, nothing during the host's pause after a 429.
#include <doctest/doctest.hpp>

#include <chrono>
#include <vector>

#include "jobs.hpp"
#include "lichess_arenas.hpp"
#include "lichess_arenas_schedule.hpp"
#include "refreshing.hpp"
#include "test_support.hpp"

using namespace fcg;
using namespace fcg::lichess;
using namespace std::chrono_literals;

namespace {

using Clock = std::chrono::steady_clock;

// The test's "now": an hour after the clock's start, never the time point a schedule reads as "never asked".
const Clock::time_point START = Clock::time_point{} + 1h;

}  // namespace

TEST_CASE("Lichess arenas' schedule: the card's list every minute while the card shows, kept when a refresh fails") {
    // As the module holds it (lichess_arenas_page.hpp), asked when the card was drawn in the last frames.
    JobGroup group;
    auto now = START;
    Refreshing<std::vector<ArenaInfo>> list(
        group, ARENA_LIST_EVERY, Refresh::Every, [&] { return now; }, [] { return Clock::duration(60s); });
    const auto take = [&] {
        REQUIRE(test::waitUntil(
            [&] {
                (void)list.poll();
                return !list.asking();
            },
            60s));
    };
    int frame = 10, drawn = -1000;
    CHECK_FALSE(drawnLately(drawn, frame));  // not drawn yet: nothing asked
    drawn = frame;
    REQUIRE((drawnLately(drawn, frame) && list.due()));
    list.ask([] {
        Answer<std::vector<ArenaInfo>> a;
        a.value = std::vector<ArenaInfo>(3);
        return a;
    });
    take();
    REQUIRE(list.value);
    now += 59s;
    CHECK_FALSE(list.due());
    now += 1s;
    CHECK(list.due());
    // Lichess unreachable: the three arenas stay, why said.
    list.ask([] {
        Answer<std::vector<ArenaInfo>> a;
        a.net = NetError::Offline;
        return a;
    });
    take();
    CHECK(list.value->size() == 3);
    CHECK(list.failed());
    now += 1min;
    CHECK(list.due());
    // The card no longer drawn: not asked, however old the list.
    frame += 3;
    CHECK_FALSE(drawnLately(drawn, frame));
}

TEST_CASE(
    "Lichess arenas' schedule: the user's teams' Swiss tournaments every five minutes, at once with a new token") {
    TeamSwissesAsk ask{.shown = true, .readable = true, .new_token = true};
    CHECK(teamSwissesDue(ask, START));  // the first time: a token they were not read with
    ask.new_token = false;
    ask.asked     = START;
    CHECK_FALSE(teamSwissesDue(ask, START + 5min - 1s));
    CHECK(teamSwissesDue(ask, START + 5min));
    // Another account, or the link made again: at once.
    ask.new_token = true;
    CHECK(teamSwissesDue(ask, START + 1s));
    // Never while their request runs, the card hidden, or without the permission to read the teams.
    CHECK_FALSE(teamSwissesDue({.shown = true, .readable = true, .new_token = true, .asking = true}, START));
    CHECK_FALSE(teamSwissesDue({.shown = false, .readable = true, .new_token = true}, START));
    CHECK_FALSE(teamSwissesDue({.shown = true, .readable = false, .new_token = true}, START));
}

TEST_CASE(
    "Lichess arenas' schedule: the followed standing every 15 s, a Swiss's every 30 s, while the mosaic draws it") {
    StandingAsk arena{.drawn = true};
    CHECK(standingDue(arena, START));  // never asked
    arena.asked = START;
    CHECK_FALSE(standingDue(arena, START + 14s));
    CHECK(standingDue(arena, START + 15s));
    StandingAsk swiss{.drawn = true, .swiss = true, .asked = START};
    CHECK_FALSE(standingDue(swiss, START + 29s));
    CHECK(standingDue(swiss, START + 30s));
    // One request at a time; the mosaic showing another, or not drawn: nothing.
    arena.asking = true;
    CHECK_FALSE(standingDue(arena, START + 1min));
    CHECK_FALSE(standingDue({.drawn = false}, START));
    // After a 429: nothing before the host's pause is over.
    StandingAsk paused{.drawn = true, .asked = START, .waits_until = START + 60s};
    CHECK_FALSE(standingDue(paused, START + 15s));
    CHECK_FALSE(standingDue(paused, START + 59s));
    CHECK(standingDue(paused, START + 60s));
}

TEST_CASE("Lichess arenas' schedule: the first eight's games when they change, then every 15 s while a tile waits") {
    StatusesAsk ask{.shown = true, .playing = true, .top = 3, .places = 3};
    CHECK(statusesDue(ask, START));  // the first time
    ask.asked = START;
    CHECK_FALSE(statusesDue(ask, START + 5s));
    // The first eight changed: at once.
    ask.top_changed = true;
    CHECK(statusesDue(ask, START + 5s));
    ask.top_changed = false;
    // Every tile showing a game in progress: not again, however long.
    CHECK_FALSE(statusesDue(ask, START + 10min));
    // A tile's game over, or a tile waiting for one: every 15 s.
    ask.some_over = true;
    CHECK_FALSE(statusesDue(ask, START + 14s));
    CHECK(statusesDue(ask, START + 15s));
    ask.some_over = false;
    ask.places    = 2;
    CHECK(statusesDue(ask, START + 15s));
    // Nothing while their request runs, during a 429's pause, the tournament not being played, or none ranked.
    auto running   = ask;
    running.asking = true;
    CHECK_FALSE(statusesDue(running, START + 1min));
    auto paused        = ask;
    paused.waits_until = START + 1min;
    CHECK_FALSE(statusesDue(paused, START + 59s));
    CHECK(statusesDue(paused, START + 1min));
    CHECK_FALSE(statusesDue({.shown = true, .playing = false, .top = 3}, START));
    CHECK_FALSE(statusesDue({.shown = true, .playing = true, .top = 0}, START));
    CHECK_FALSE(statusesDue({.shown = false, .playing = true, .top = 3}, START));
}

TEST_CASE("Lichess arenas' schedule: the arena played, its page every 15 s and its joins again, until it is over") {
    ArenaPageAsk page{.looked = true};
    CHECK(arenaPageDue(page, START));
    page.asked = START;
    CHECK_FALSE(arenaPageDue(page, START + 14s));
    CHECK(arenaPageDue(page, START + 15s));
    page.asked = {};  // joined, or a game of it over: asked again at once
    CHECK(arenaPageDue(page, START + 1s));
    CHECK_FALSE(arenaPageDue({.looked = false}, START));
    CHECK_FALSE(arenaPageDue({.looked = true, .over = true}, START));
    CHECK_FALSE(arenaPageDue({.looked = true, .asking = true}, START));
    CHECK_FALSE(arenaPageDue({.looked = true, .waits_until = START + 30s}, START + 29s));
    CHECK(arenaPageDue({.looked = true, .waits_until = START + 30s}, START + 30s));
    // Joined again while the user waits in it, a pairing lasting a minute.
    ArenaSeat seat;
    CHECK_FALSE(rejoinDue(seat, true, false, {}, START));  // not in it
    seat.joined(START);
    CHECK_FALSE(rejoinDue(seat, true, false, {}, START + ARENA_REJOIN - 1s));
    CHECK(rejoinDue(seat, true, false, {}, START + ARENA_REJOIN));
    CHECK_FALSE(rejoinDue(seat, false, false, {}, START + ARENA_REJOIN));           // the account unlinked
    CHECK_FALSE(rejoinDue(seat, true, true, {}, START + ARENA_REJOIN));             // a join under way
    CHECK_FALSE(rejoinDue(seat, true, false, START + 2min, START + ARENA_REJOIN));  // a 429's pause
    seat.playing(true);
    CHECK_FALSE(rejoinDue(seat, true, false, {}, START + 5min));  // one of its games being played
    seat.playing(false);
    seat.pause();
    CHECK_FALSE(rejoinDue(seat, true, false, {}, START + 5min));
}

TEST_CASE("Lichess arenas' schedule: the Swiss played or looked at, every 20 s in it or under way, else every minute") {
    SwissPageAsk looked{.looked = true};
    CHECK(swissPageDue(looked, START));
    looked.asked = START;
    CHECK_FALSE(swissPageDue(looked, START + 59s));  // to come, looked at
    CHECK(swissPageDue(looked, START + 1min));
    auto joined   = looked;
    joined.joined = true;
    CHECK_FALSE(swissPageDue(joined, START + 19s));
    CHECK(swissPageDue(joined, START + 20s));
    auto started    = looked;
    started.started = true;
    CHECK(swissPageDue(started, START + 20s));
    // Over with its standing and its games: never again; nothing while its request runs, or when nobody looks.
    auto complete     = looked;
    complete.complete = true;
    CHECK_FALSE(swissPageDue(complete, START + 1h));
    CHECK_FALSE(swissPageDue({.looked = true, .asking = true}, START));
    CHECK_FALSE(swissPageDue({.looked = false}, START));
    // Its standing asked with its page: none read yet, read before another round, or the Swiss over.
    SwissInfo info;
    info.round = 3;
    info.state = TourState::Started;
    CHECK(swissRowsDue(false, &info, 3));
    CHECK(swissRowsDue(true, nullptr, 3));
    CHECK_FALSE(swissRowsDue(true, &info, 3));
    CHECK(swissRowsDue(true, &info, 2));
    info.state = TourState::Finished;
    CHECK(swissRowsDue(true, &info, 3));
}
