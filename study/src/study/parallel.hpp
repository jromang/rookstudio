#pragma once

// The study's work shared among threads: how many it takes, and the threads that run it. Each thread runs in the
// background of the thread that starts it (background.hpp), and what one of them throws reaches the caller once they
// all ended: a part that fails neither ends the program nor goes unnoticed.

#include <algorithm>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

#include "background.hpp"

namespace fcg::study {

// The threads asked for, or the machine's when none is asked (0 or less): at least one.
[[nodiscard]] inline int threadCount(int asked = 0) noexcept {
    return asked > 0 ? asked : static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
}

// The first exception that threads throw, kept to be thrown again once they all ended.
class FirstError {
   public:
    // Runs `f`, what it throws kept unless something was kept before.
    template <typename F>
    void run(F&& f) noexcept {
        try {
            f();
        } catch (...) {
            const std::lock_guard lock(mutex_);
            if (!error_) error_ = std::current_exception();
        }
    }
    // Something was kept.
    [[nodiscard]] bool failed() const {
        const std::lock_guard lock(mutex_);
        return error_ != nullptr;
    }
    // Throws again what was kept, if anything.
    void rethrow() const {
        std::exception_ptr error;
        {
            const std::lock_guard lock(mutex_);
            error = error_;
        }
        if (error) std::rethrow_exception(error);
    }

   private:
    mutable std::mutex mutex_;
    std::exception_ptr error_;
};

// Runs `work(thread)` for each of `threads` threads (on the caller's alone for one or fewer), each in the background
// of the caller, and waits for them all: then throws again the first exception one of them threw. A thread that
// cannot be started throws once those started have ended.
template <typename Work>
void onThreads(int threads, Work&& work) {
    if (threads <= 1) {
        work(0);
        return;
    }
    const Inherited background;
    FirstError error;
    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(threads));
    try {
        for (int t = 0; t < threads; ++t)
            pool.emplace_back([&, t] {
                background.apply();
                error.run([&] { work(t); });
            });
    } catch (...) {
        for (auto& th : pool) th.join();
        throw;
    }
    for (auto& th : pool) th.join();
    error.rethrow();
}

// Runs `part(from, to, index)` over `parts` parts of [0, n), of sizes within one of each other, each on a thread of
// its own (onThreads): no more parts than elements, and one at least (empty when n is 0).
template <typename Part>
void forEachPart(std::size_t n, std::size_t parts, Part&& part) {
    parts = std::max<std::size_t>(1, std::min(parts, n));
    onThreads(static_cast<int>(parts), [&](int thread) {
        const auto p = static_cast<std::size_t>(thread);
        part(n * p / parts, n * (p + 1) / parts, p);
    });
}

}  // namespace fcg::study
