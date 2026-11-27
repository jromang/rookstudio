// When to ask again (refreshing.hpp, #111), driven by a clock of the test's own: a value refreshed is asked at once the
// first time, again once its maximum age has passed, never twice at a time, nothing during its host's pause after a
// 429; a failure leaves the value shown and says why. Asked again only once stale, a value kept elsewhere counts its
// age. And the pages' small checks: a time never set, a request due again, a card drawn in the last frames.
#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "https.hpp"
#include "jobs.hpp"
#include "lichess_api.hpp"
#include "refreshing.hpp"
#include "test_support.hpp"

using namespace fcg;
using namespace std::chrono_literals;

namespace {

using Clock = std::chrono::steady_clock;

// The test's "now": an hour after the clock's start, never the time point a schedule reads as "never asked".
const Clock::time_point START = Clock::time_point{} + 1h;

// What a fetch answers.
template <typename T>
lichess::Answer<T> gave(T value) {
    lichess::Answer<T> a;
    a.value = std::move(value);
    return a;
}
template <typename T>
lichess::Answer<T> failed(lichess::NetError net, int status = 0) {
    lichess::Answer<T> a;
    a.net    = net;
    a.status = status;
    return a;
}

// Its answer waited for (a minute at most) and taken: whether a new value came.
template <typename T>
bool taken(Refreshing<T>& r) {
    bool fresh = false;
    REQUIRE(test::waitUntil(
        [&] {
            fresh = r.poll() || fresh;
            return !r.asking();
        },
        60s));
    return fresh;
}

// A pause of a minute after a 429, as HostGate's.
Clock::duration minute() { return 60s; }

}  // namespace

TEST_CASE("Refreshing: asked at once the first time, again once its maximum age has passed, never twice at a time") {
    JobGroup group;
    auto now = START;
    Refreshing<int> r(group, 60s, Refresh::Every, [&] { return now; }, minute);
    CHECK(never(r.askedAt()));
    CHECK(r.stale());  // nothing shown yet
    CHECK(r.due());
    // The request under way: no other, however long it takes.
    std::promise<void> opener;
    std::shared_future<void> opened = opener.get_future().share();
    r.ask([opened] {
        (void)opened.wait_for(60s);
        return gave(7);
    });
    CHECK(r.askedAt() == START);
    CHECK(r.asking());
    CHECK_FALSE(r.due());
    now += 90s;
    CHECK_FALSE(r.due());
    CHECK_FALSE(r.poll());  // nothing came yet
    opener.set_value();
    CHECK(taken(r));
    REQUIRE(r.value);
    CHECK(*r.value == 7);
    CHECK_FALSE(r.failed());
    CHECK_FALSE(r.stale());  // it just came
    // Asked a minute and a half ago: due again, at its pace, whatever the age of what is shown.
    CHECK(r.due());
    r.ask([] { return gave(8); });
    CHECK(taken(r));
    CHECK(*r.value == 8);
    now += 59s;
    CHECK_FALSE(r.due());
    CHECK_FALSE(r.stale());
    now += 1s;
    CHECK(r.due());
    CHECK(r.stale());  // a minute old
}

TEST_CASE("Refreshing: a failure leaves the value shown and says why, until a value comes again") {
    JobGroup group;
    auto now = START;
    Refreshing<std::vector<std::string>> r(group, 30s, Refresh::Every, [&] { return now; }, minute);
    // Nothing yet, and a failure: none shown, why said.
    r.ask([] { return failed<std::vector<std::string>>(lichess::NetError::Offline); });
    CHECK_FALSE(taken(r));
    CHECK_FALSE(r.value);
    CHECK(r.failed());
    CHECK(r.net == lichess::NetError::Offline);
    now += 30s;
    REQUIRE(r.due());
    r.ask([] { return gave(std::vector<std::string>{"Hourly Rapid Arena", "Daily Classical Arena"}); });
    CHECK(taken(r));
    CHECK_FALSE(r.failed());  // said no longer
    // Lichess fails again: the list of before stays, the failure said.
    now += 30s;
    REQUIRE(r.due());
    r.ask([] { return failed<std::vector<std::string>>(lichess::NetError::Server, 503); });
    CHECK_FALSE(taken(r));
    REQUIRE(r.value);
    CHECK(r.value->size() == 2);
    CHECK(r.failed());
    CHECK(r.status == 503);
    CHECK(lichess::gaveNothing(r) == lichess::gaveNothing(failed<int>(lichess::NetError::Server, 503)));
    // An answer without a value nor a failure (Lichess has nothing for it): the value kept, nothing said.
    now += 30s;
    REQUIRE(r.due());
    r.ask([] { return lichess::Answer<std::vector<std::string>>{}; });
    CHECK_FALSE(taken(r));
    CHECK(r.value->size() == 2);
    CHECK_FALSE(r.failed());
    // A value again: it replaces the one shown.
    now += 30s;
    REQUIRE(r.due());
    r.ask([] { return gave(std::vector<std::string>{"Hourly Blitz Arena"}); });
    CHECK(taken(r));
    CHECK(*r.value == std::vector<std::string>{"Hourly Blitz Arena"});
}

TEST_CASE("Refreshing: after a 429 of its own, nothing asked before its host's pause is over, at once again or not") {
    JobGroup group;
    auto now = START;
    Refreshing<int> r(group, 10s, Refresh::Every, [&] { return now; }, minute);
    r.ask([] { return gave(1); });
    CHECK(taken(r));
    now += 10s;
    REQUIRE(r.due());
    r.ask([] { return failed<int>(lichess::NetError::RateLimited, 429); });
    CHECK_FALSE(taken(r));
    CHECK(r.pausedUntil() == START + 70s);  // a minute from its answer
    CHECK(*r.value == 1);
    CHECK(r.failed());
    now += 10s;
    CHECK_FALSE(r.due());  // its pace says yes, the pause no
    r.again();             // what changed waits for the pause all the same
    CHECK_FALSE(r.due());
    now = START + 70s - 1ms;
    CHECK_FALSE(r.due());
    now = START + 70s;
    CHECK(r.due());
    // Another failure does not pause it.
    r.ask([] { return failed<int>(lichess::NetError::Offline); });
    CHECK_FALSE(taken(r));
    CHECK(r.pausedUntil() == START + 70s);
    now += 10s;
    CHECK(r.due());
}

TEST_CASE("Refreshing: the pause after a 429 is HostGate's for Lichess's API host, by default") {
    // A host of this test's own: no other test's requests go there, nor any.
    struct Servers {
        lichess::Servers before = lichess::servers();
        Servers() { lichess::servers().api = "http://refreshing-tests.invalid:8080"; }
        ~Servers() { lichess::servers() = before; }
    } servers;
    CHECK(lichess::apiHost() == "refreshing-tests.invalid");
    HostGate::instance().setPause(60s);
    CHECK(lichess::apiPause() == 0ms);
    HostGate::instance().tooMany("refreshing-tests.invalid");
    CHECK(lichess::apiPause() > 50s);
    JobGroup group;
    auto now = START;
    Refreshing<int> r(group, 10s, Refresh::Every, [&] { return now; });
    r.ask([] { return failed<int>(lichess::NetError::RateLimited, 429); });
    CHECK_FALSE(taken(r));
    CHECK(r.pausedUntil() > START + 50s);
    CHECK(r.pausedUntil() <= START + 61s);
    now += 30s;
    CHECK_FALSE(r.due());
}

TEST_CASE("Refreshing: asked again at once on demand; forgotten, value, failure and request, as a new one") {
    JobGroup group;
    auto now = START;
    Refreshing<int> r(group, 15s, Refresh::Every, [&] { return now; }, minute);
    r.ask([] { return gave(3); });
    CHECK(taken(r));
    now += 5s;
    CHECK_FALSE(r.due());
    r.again();  // joined: the user's rank shows in its next answer
    CHECK(never(r.askedAt()));
    CHECK(r.due());
    r.ask([] { return failed<int>(lichess::NetError::Offline); });
    CHECK_FALSE(taken(r));
    // A request under way let go of when it is forgotten: asked to stop, never waited for.
    const auto stopped = std::make_shared<std::atomic<bool>>(false);
    now += 15s;
    r.ask([stopped](const TaskState<>& state) {
        (void)test::waitUntil([&] { return state.cancel.load(); }, 60s);
        *stopped = state.cancel.load();
        return gave(4);
    });
    REQUIRE(r.asking());
    r.reset();
    CHECK_FALSE(r.asking());
    CHECK_FALSE(r.value);
    CHECK_FALSE(r.failed());
    CHECK(never(r.askedAt()));
    CHECK(r.due());
    CHECK(test::waitUntil([&] { return group.poll() == 0; }, 60s));
    CHECK(stopped->load());
}

TEST_CASE("Refreshing: asked again only once stale, a value kept elsewhere counting its age") {
    JobGroup group;
    auto now = START;
    Refreshing<int> r(group, 10min, Refresh::WhenStale, [&] { return now; }, minute);
    CHECK(r.due());  // nothing shown: asked at once
    r.keep(5, 4min);
    CHECK_FALSE(r.stale());
    CHECK_FALSE(r.due());  // never asked in this run, but what is shown is four minutes old
    now += 6min - 1s;
    CHECK_FALSE(r.due());
    now += 1s;
    CHECK(r.stale());
    CHECK(r.due());
    r.keep(6);  // as old as nothing
    CHECK_FALSE(r.due());
    r.again();  // asked again on demand: only once stale all the same
    CHECK_FALSE(r.due());
}

TEST_CASE("Schedules: a time never set, a request due again after so long, a card drawn in the last frames") {
    CHECK(never(Clock::time_point{}));
    CHECK_FALSE(never(START));
    CHECK(dueAfter(Clock::time_point{}, 30s, START));  // never asked
    CHECK_FALSE(dueAfter(START, 30s, START + 29s));
    CHECK(dueAfter(START, 30s, START + 30s));
    CHECK(dueAfter(START, 30s, START + 1h));
    // The frame that drew a card last, and the frame now: the card shows while it was drawn in the last three.
    CHECK(drawnLately(100, 100));
    CHECK(drawnLately(100, 102));
    CHECK_FALSE(drawnLately(100, 103));
    CHECK_FALSE(drawnLately(-1000, 0));  // never drawn
    // In the last two: the frame before, or this one.
    CHECK(drawnLately(100, 101, 2));
    CHECK_FALSE(drawnLately(100, 102, 2));
}
