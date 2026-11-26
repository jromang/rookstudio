// Background jobs (jobs.hpp, #105): a job replaced or destroyed while its task runs parks the task, never waits for
// it; cancel reaches the task; what the task tells is shared with the owner and outlives the job; a group cancels its
// jobs at once, lets go of the tasks parked as they end and waits for the others when it goes.
#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "jobs.hpp"
#include "test_support.hpp"

using namespace fcg;
using namespace std::chrono_literals;

namespace {

// A gate the tasks wait at until the test opens it: bounded, so that a job that waited for its task would fail the
// test rather than hang it.
struct Gate {
    std::promise<void> opener;
    std::shared_future<void> opened = opener.get_future().share();
    void open() { opener.set_value(); }
    // True when it was opened in time.
    [[nodiscard]] bool pass() const { return opened.wait_for(60s) == std::future_status::ready; }
};

// Waits for `done`, a minute at most: false when it did not come.
template <typename F>
bool waitFor(F&& done) {
    return test::waitUntil(std::forward<F>(done), 60s);
}

// A task's wait until it is asked to stop, a minute at most: whether it was.
bool untilCancelled(const std::atomic<bool>& cancel) {
    return waitFor([&] { return cancel.load(); });
}

// What the tests' tasks tell as they go.
struct Steps {
    std::atomic<int> done{0};
};

}  // namespace

TEST_CASE("jobs: a task started, followed, its result taken once; what it tells read as it goes, and after") {
    JobGroup group;
    Job<int, Steps> job(group);
    CHECK(job.idle());
    CHECK_FALSE(job.running());
    CHECK_FALSE(job.ready());
    Gate gate;
    job.start([&gate](TaskState<Steps>& state) {
        state.done        = 1;
        const bool passed = gate.pass();
        state.done        = 2;
        return passed ? 42 : -1;
    });
    CHECK(job.running());
    CHECK_FALSE(job.idle());
    REQUIRE(waitFor([&] { return job.state().done == 1; }));
    CHECK_FALSE(job.ready());
    gate.open();
    REQUIRE(waitFor([&] { return job.ready(); }));
    CHECK_FALSE(job.running());
    CHECK(job.take() == 42);
    CHECK(job.idle());
    CHECK(job.state().done == 2);  // kept once it ended
    CHECK_FALSE(job.cancelled());
    CHECK(group.parked() == 0);
}

TEST_CASE("jobs: a job started again while its task runs: the task cancelled and parked, not waited for") {
    JobGroup group;
    Job<std::string> job(group);
    Gate gate;
    std::atomic<bool> cancelled{false}, ended{false};
    job.start([&](TaskState<>& state) {
        // Once cancelled, it still waits for the test: a start that waited for it would not come back.
        cancelled = untilCancelled(state.cancel);
        (void)gate.pass();
        ended = true;
        return std::string("first");
    });
    job.start([] { return std::string("second"); });
    CHECK_FALSE(ended);
    CHECK(group.parked() == 1);
    REQUIRE(waitFor([&] { return job.ready(); }));
    CHECK(job.take() == "second");
    REQUIRE(waitFor([&] { return cancelled.load(); }));
    CHECK(group.poll() == 1);  // still at its gate
    gate.open();
    REQUIRE(waitFor([&] { return group.poll() == 0; }));
    CHECK(ended);
}

TEST_CASE("jobs: a job destroyed while its task runs comes back at once; the group waits for the task when it goes") {
    Gate gate;
    std::atomic<bool> cancelled{false}, ended{false};
    {
        JobGroup group;
        {
            Job<void> job(group);
            job.start([&](TaskState<>& state) {
                cancelled = untilCancelled(state.cancel);
                (void)gate.pass();
                ended = true;
            });
        }
        CHECK_FALSE(ended);
        CHECK(group.parked() == 1);
        REQUIRE(waitFor([&] { return cancelled.load(); }));
        gate.open();
    }
    CHECK(ended);
}

TEST_CASE("jobs: cancel() reaches the task, which ends on its own") {
    JobGroup group;
    Job<bool> job(group);
    job.start([](TaskState<>& state) { return untilCancelled(state.cancel); });
    CHECK(job.running());
    CHECK_FALSE(job.cancelled());
    job.cancel();
    CHECK(job.cancelled());
    REQUIRE(waitFor([&] { return job.ready(); }));
    CHECK(job.take());
    CHECK(job.cancelled());  // said until the next start
    job.start([] { return true; });
    CHECK_FALSE(job.cancelled());
}

TEST_CASE("jobs: the group asks every job's task to stop at once") {
    JobGroup group;
    Job<bool> a(group);
    Job<bool, Steps> b(group);
    CHECK(group.enrolled() == 0);  // nothing started
    a.start([](TaskState<>& state) { return untilCancelled(state.cancel); });
    b.start([](TaskState<Steps>& state) { return untilCancelled(state.cancel); });
    CHECK(group.enrolled() == 2);
    group.cancelAll();
    REQUIRE(waitFor([&] { return a.ready() && b.ready(); }));
    CHECK(a.take());
    CHECK(b.take());
}

TEST_CASE("jobs: what the task throws comes back from take()") {
    JobGroup group;
    Job<int> job(group);
    job.start([]() -> int { throw std::runtime_error("the disk is full"); });
    REQUIRE(waitFor([&] { return job.ready(); }));
    CHECK_THROWS_WITH_AS(job.take(), "the disk is full", std::runtime_error);
    CHECK(job.idle());
}

TEST_CASE("jobs: a job moved keeps its task; one moved onto a running job parks the latter's task") {
    JobGroup group;
    Gate gate;
    Job<int> a(group);
    a.start([&gate] { return gate.pass() ? 1 : -1; });
    Job<int> b(std::move(a));
    CHECK(a.idle());
    CHECK(b.running());
    Job<int> c(group);
    std::atomic<bool> cancelled{false};
    c.start([&](TaskState<>& state) {
        cancelled = untilCancelled(state.cancel);
        return 3;
    });
    c = std::move(b);
    CHECK(b.idle());
    CHECK(group.parked() == 1);
    REQUIRE(waitFor([&] { return cancelled.load(); }));
    gate.open();
    REQUIRE(waitFor([&] { return c.ready(); }));
    CHECK(c.take() == 1);
    REQUIRE(waitFor([&] { return group.poll() == 0; }));
}

TEST_CASE("jobs: a task parked keeps its state: what it writes once its job is gone is its own") {
    JobGroup group;
    Gate gate;
    std::atomic<int> seen{0};
    {
        Job<void, Steps> job(group);
        job.start([&](TaskState<Steps>& state) {
            (void)gate.pass();
            for (int i = 1; i <= 1000; ++i) state.done = i;  // its job gone
            seen = state.done.load();
        });
    }
    gate.open();
    group.wait();
    CHECK(seen == 1000);
    CHECK(group.parked() == 0);
}

TEST_CASE("jobs: the group lets go of the tasks parked as they end, one after the other") {
    JobGroup group;
    Gate first, second;
    {
        Job<int> a(group), b(group);
        a.start([&first] { return first.pass() ? 1 : 0; });
        b.start([&second] { return second.pass() ? 2 : 0; });
    }
    CHECK(group.parked() == 2);
    CHECK(group.poll() == 2);
    first.open();
    REQUIRE(waitFor([&] { return group.poll() == 1; }));
    second.open();
    REQUIRE(waitFor([&] { return group.poll() == 0; }));
    // Their states let go of with them.
    CHECK(group.enrolled() == 0);
}
