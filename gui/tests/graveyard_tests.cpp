// The futures of tasks nobody waits for (graveyard.hpp, #98): kept without waiting, let go of once their task ended,
// what was to be done with their result done then (#105), waited for at the end.
#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include "graveyard.hpp"

using namespace fcg;
using namespace std::chrono_literals;

namespace {

// A gate the tasks wait at until the test opens it: bounded, so that a graveyard that waited for them would fail the
// test rather than hang it.
struct Gate {
    std::promise<void> opener;
    std::shared_future<void> opened = opener.get_future().share();
    void open() { opener.set_value(); }
    // True when it was opened in time.
    [[nodiscard]] bool pass() const { return opened.wait_for(60s) == std::future_status::ready; }
};

}  // namespace

TEST_CASE("graveyard: a future parked comes back at once, its task running; it is let go of once the task ended") {
    Gate gate;
    std::atomic<bool> passed{false};
    Graveyard graveyard;
    graveyard.park(std::async(std::launch::async, [&gate, &passed] {
        passed = gate.pass();
        return 42;
    }));
    // park() did not wait: the task still waits at the gate, its future kept.
    CHECK(graveyard.size() == 1);
    CHECK(graveyard.poll() == 1);
    gate.open();
    const auto give_up = std::chrono::steady_clock::now() + 60s;
    while (graveyard.poll() > 0 && std::chrono::steady_clock::now() < give_up) std::this_thread::sleep_for(1ms);
    CHECK(graveyard.empty());
    CHECK(passed);
}

TEST_CASE("graveyard: futures of any result kept together; wait() comes back once every task ended") {
    Gate gate;
    std::atomic<int> ended{0};
    Graveyard graveyard;
    graveyard.park(std::async(std::launch::async, [&] {
        if (gate.pass()) ++ended;
    }));
    graveyard.park(std::async(std::launch::async, [&] {
        if (gate.pass()) ++ended;
        return std::string("a text");
    }));
    graveyard.park(std::async(std::launch::async, [&] {
        if (gate.pass()) ++ended;
        return std::make_unique<int>(7);  // a result that cannot be copied
    }));
    // Futures with no task: none made, one whose result was taken. Nothing to keep.
    graveyard.park(std::future<int>{});
    std::promise<int> promise;
    auto taken = promise.get_future();
    promise.set_value(1);
    CHECK(taken.get() == 1);
    graveyard.park(std::move(taken));
    CHECK(graveyard.size() == 3);
    gate.open();
    graveyard.wait();
    CHECK(ended == 3);
    CHECK(graveyard.empty());
    CHECK(graveyard.poll() == 0);
}

TEST_CASE("graveyard: what to do with a result done once its task ended, on the thread that polls; not when it threw") {
    Gate gate;
    Graveyard graveyard;
    const auto polling = std::this_thread::get_id();
    std::string said;
    bool on_polling_thread = true, after_throw = false;
    int voids = 0, later = 0;
    graveyard.park(std::async(std::launch::async, [&gate] { return std::string(gate.pass() ? "revoked" : "late"); }),
                   [&](std::string result) {
                       said = std::move(result);
                       on_polling_thread &= std::this_thread::get_id() == polling;
                   });
    graveyard.park(std::async(std::launch::async, [&gate] { (void)gate.pass(); }), [&] { ++voids; });
    graveyard.park(std::async(std::launch::async,
                              [&gate]() -> int {
                                  (void)gate.pass();
                                  throw std::runtime_error("refused");
                              }),
                   [&](int) { after_throw = true; });
    // What is done with a result may park another future: kept, done at a later poll.
    graveyard.park(std::async(std::launch::async, [] { return 1; }), [&](int) {
        graveyard.park(std::async(std::launch::async, [] { return 2; }), [&](int n) { later = n; });
    });
    CHECK(said.empty());
    gate.open();
    const auto give_up = std::chrono::steady_clock::now() + 60s;
    while ((graveyard.poll() > 0 || later == 0) && std::chrono::steady_clock::now() < give_up)
        std::this_thread::sleep_for(1ms);
    CHECK(said == "revoked");
    CHECK(on_polling_thread);
    CHECK(voids == 1);
    CHECK_FALSE(after_throw);
    CHECK(later == 2);
    // wait() does it too, at the end.
    Gate last;
    int seven = 0;
    graveyard.park(std::async(std::launch::async, [&last] { return last.pass() ? 7 : 0; }), [&](int n) { seven = n; });
    last.open();
    graveyard.wait();
    CHECK(seven == 7);
    CHECK(graveyard.empty());
}

TEST_CASE("graveyard: a task cancelled, then parked, ends on its own; the graveyard waits for it when it goes") {
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    std::atomic<bool> stopped{false};
    {
        Graveyard graveyard;
        // A task that runs until it is cancelled, as the program's jobs do.
        auto job = std::async(std::launch::async, [cancel, &stopped] {
            const auto give_up = std::chrono::steady_clock::now() + 60s;
            while (!*cancel && std::chrono::steady_clock::now() < give_up) std::this_thread::sleep_for(1ms);
            stopped = static_cast<bool>(*cancel);
        });
        *cancel  = true;
        graveyard.park(std::move(job));
        CHECK_FALSE(job.valid());
    }
    CHECK(stopped);
}
