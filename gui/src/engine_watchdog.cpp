#include "engine_watchdog.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <system_error>
#include <utility>

#include <core/globals/globals.hpp>
#include <core/threading/thread_vector.hpp>

#ifndef _WIN64  // as ProcessInformation
#    include <unistd.h>

#    include "proc_tree.hpp"
#endif

namespace fastchess {
// The engine processes started (see core/globals/globals.cpp).
extern util::ThreadVector<ProcessInformation> process_list;
}  // namespace fastchess

namespace fcg {

EngineWatchdog::EngineWatchdog(std::chrono::milliseconds period) {
#ifndef _WIN64
    if (period.count() <= 0) return;
    try {
        thread_ = std::thread([this, period] {
            std::unique_lock<std::mutex> lock(stop_mutex_);
            while (!stop_cv_.wait_for(lock, period, [this] { return stop_; })) {
                lock.unlock();
                try {
                    (void)look();
                } catch (const std::exception&) {  // out of memory: looked at again next time
                }
                lock.lock();
            }
        });
    } catch (const std::system_error&) {
        // No thread to spare: the games are played as without it.
    }
#else
    (void)period;
#endif
}

EngineWatchdog::~EngineWatchdog() {
    {
        std::lock_guard<std::mutex> lock(stop_mutex_);
        stop_ = true;
    }
    stop_cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

int EngineWatchdog::look() {
#ifdef _WIN64
    return 0;
#else
    std::lock_guard<std::mutex> lock(look_mutex_);
    std::vector<Ended> ended;
    int woken = 0;
    // Under the list's lock, an engine listed has its descriptor open: fastchess takes it off the list before it
    // closes it.
    const std::lock_guard listed(fastchess::process_list);
    for (const auto& process : fastchess::process_list) {
        const auto before = std::find_if(ended_.begin(), ended_.end(), [&](const Ended& e) {
            return e.pid == process.identifier && e.fd == process.fd_write;
        });
        if (before != ended_.end() && before->woken) {
            ended.push_back(*before);  // not again while it is listed
            continue;
        }
        if (!childEnded(process.identifier)) continue;
        if (before == ended_.end()) {
            ended.push_back({process.identifier, process.fd_write, false});  // woken at the next look, if still listed
            continue;
        }
        const uint64_t wake                 = 1;
        [[maybe_unused]] const auto written = ::write(process.fd_write, &wake, sizeof wake);
        ended.push_back({process.identifier, process.fd_write, true});
        ++woken;
    }
    ended_ = std::move(ended);  // those no longer listed are forgotten
    return woken;
#endif
}

}  // namespace fcg
