#pragma once

// A server on this machine whose answers a test writes, a connection at a time on threads of its own; and waits
// bounded by what they await (test_support.hpp).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "net.hpp"
#include "test_support.hpp"  // waitUntil()

namespace fcg::test {

using namespace std::chrono_literals;

// Each connection served on a thread of its own by `serve`, given the request's head: it writes what it wants,
// waiting where it wants. Counts the connections served at once.
class ScriptedServer {
   public:
    using Serve = std::function<void(const std::string& head, net::TcpConnection& c)>;
    ScriptedServer(uint16_t port, Serve serve) : serve_(std::move(serve)) {
        ok_     = listener_.listen(port, true).empty();
        thread_ = std::thread([this] {
            while (!stop_) {
                const auto fd = listener_.accept(50);
                if (!fd) continue;
                std::lock_guard lock(mutex_);
                workers_.emplace_back([this, handle = *fd] {
                    net::TcpConnection c(handle, &running_);
                    const auto head = c.readUntil("\r\n\r\n", 1 << 16, 10000);
                    if (head.find("\r\n\r\n") == std::string::npos) return;
                    const int now = ++open_;
                    int seen      = max_open_.load();
                    while (now > seen && !max_open_.compare_exchange_weak(seen, now)) {
                    }
                    serve_(head, c);
                    --open_;
                });
            }
        });
    }
    ~ScriptedServer() {
        stop_    = true;
        running_ = false;
        thread_.join();
        for (auto& w : workers_) w.join();
    }
    ScriptedServer(const ScriptedServer&)            = delete;
    ScriptedServer& operator=(const ScriptedServer&) = delete;

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] int maxOpen() const { return max_open_; }

   private:
    Serve serve_;
    net::TcpListener listener_;
    std::atomic_bool running_{true};
    std::atomic<bool> stop_{false};
    std::atomic<int> open_{0}, max_open_{0};
    std::mutex mutex_;
    std::vector<std::thread> workers_;
    std::thread thread_;
    bool ok_ = false;
};

inline std::string requestPath(const std::string& head) {
    const auto start = head.find(' ') + 1;
    return head.substr(start, head.find(' ', start) - start);
}

}  // namespace fcg::test
