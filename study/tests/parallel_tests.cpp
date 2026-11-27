// The study's threads (parallel.hpp): how many it takes, the parts each runs, the background they inherit, and what
// one of them throws reaching the caller once all have ended (#112).
#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <study/background.hpp>
#include <study/parallel.hpp>

using namespace fcg::study;

TEST_CASE("parallel: the threads asked for, else the machine's, one at least") {
    const int machine = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    CHECK(threadCount(3) == 3);
    CHECK(threadCount(1) == 1);
    CHECK(threadCount(0) == machine);
    CHECK(threadCount(-2) == machine);
    CHECK(threadCount() == machine);
}

TEST_CASE("parallel: each thread runs once, and each part of a range once, sized alike") {
    std::vector<int> runs(5, 0);
    onThreads(5, [&](int t) { ++runs[static_cast<std::size_t>(t)]; });
    CHECK(runs == std::vector<int>(5, 1));

    // One thread, or none asked: the caller's own.
    const auto caller = std::this_thread::get_id();
    for (const int threads : {1, 0, -3}) {
        std::thread::id ran;
        int index = -1;
        onThreads(threads, [&](int t) {
            ran   = std::this_thread::get_id();
            index = t;
        });
        CHECK(ran == caller);
        CHECK(index == 0);
    }

    for (const auto& [n, parts] :
         std::vector<std::pair<std::size_t, std::size_t>>{{10, 3}, {3, 8}, {0, 4}, {1, 1}, {100000, 7}, {64, 64}}) {
        CAPTURE(n);
        CAPTURE(parts);
        std::mutex mutex;
        std::vector<std::tuple<std::size_t, std::size_t, std::size_t>> seen;  // from, to, index
        forEachPart(n, parts, [&](std::size_t from, std::size_t to, std::size_t index) {
            const std::lock_guard lock(mutex);
            seen.emplace_back(from, to, index);
        });
        const auto expected = std::max<std::size_t>(1, std::min(parts, n));
        REQUIRE(seen.size() == expected);
        std::sort(seen.begin(), seen.end());
        std::size_t at = 0, smallest = n, largest = 0;
        for (std::size_t k = 0; k < seen.size(); ++k) {
            const auto [from, to, index] = seen[k];
            CHECK(from == at);  // in order, without a gap or an overlap
            CHECK(index == k);  // each index once, in the range's order
            smallest = std::min(smallest, to - from);
            largest  = std::max(largest, to - from);
            at       = to;
        }
        CHECK(at == n);
        CHECK(largest - smallest <= 1);
    }
}

TEST_CASE("parallel: the threads run in the background of the thread that starts them") {
    std::vector<int> lowered(4, 0), marked(4, 0), part_marked(3, 0);
    std::thread t([&] {
        enterBackground();
        onThreads(4, [&](int i) {
            lowered[static_cast<std::size_t>(i)] = loweredBySystem();
            marked[static_cast<std::size_t>(i)]  = inBackground();
        });
        forEachPart(3, 3, [&](std::size_t, std::size_t, std::size_t p) { part_marked[p] = inBackground(); });
    });
    t.join();
    CHECK(lowered == std::vector<int>(4, 1));
    CHECK(marked == std::vector<int>(4, 1));
    CHECK(part_marked == std::vector<int>(3, 1));

    // Started from a thread in the foreground, they stay there.
    std::vector<int> plain(4, 1);
    onThreads(4, [&](int i) { plain[static_cast<std::size_t>(i)] = inBackground(); });
    CHECK(plain == std::vector<int>(4, 0));
}

TEST_CASE("parallel: what a part throws reaches the caller once every part has ended") {
    // One part throws: the others run to their end, then the caller gets the exception (no thread left running, the
    // program not ended).
    std::atomic<int> ended{0};
    CHECK_THROWS_WITH_AS(onThreads(6,
                                   [&](int t) {
                                       if (t == 2) throw std::runtime_error("part 2");
                                       ++ended;
                                   }),
                         "part 2", std::runtime_error);
    CHECK(ended == 5);

    // Several throw: one of their exceptions, the others dropped.
    try {
        forEachPart(8, 8, [](std::size_t from, std::size_t, std::size_t) {
            if (from % 2 == 0) throw std::invalid_argument("part " + std::to_string(from));
        });
        FAIL("nothing thrown");
    } catch (const std::invalid_argument& e) {
        const std::string what = e.what();
        CHECK((what == "part 0" || what == "part 2" || what == "part 4" || what == "part 6"));
    }

    // On the caller's thread alone, and an exception of no std::exception's kind, the same.
    CHECK_THROWS_WITH_AS(
        forEachPart(1, 4, [](std::size_t, std::size_t, std::size_t) { throw std::logic_error("one"); }), "one",
        std::logic_error);
    CHECK_THROWS_AS(onThreads(3,
                              [](int t) {
                                  if (t == 1) throw 42;
                              }),
                    int);

    // The first one kept, those after it dropped.
    FirstError first;
    CHECK_FALSE(first.failed());
    CHECK_NOTHROW(first.rethrow());
    first.run([] {});
    CHECK_FALSE(first.failed());
    first.run([] { throw std::runtime_error("first"); });
    first.run([] { throw std::runtime_error("second"); });
    CHECK(first.failed());
    CHECK_THROWS_WITH_AS(first.rethrow(), "first", std::runtime_error);
}
