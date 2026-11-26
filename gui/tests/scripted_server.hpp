#pragma once

// A server on this machine whose answers a test writes, a connection at a time on threads of its own; and waits
// bounded by what they await (test_support.hpp).

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
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

// The request's body after its head (readUntil() may have read a part of it with the head), as long as its
// Content-Length says. A server reads it before it closes the connection: on Windows a socket closed with bytes it did
// not read resets the connection, and the client loses the answer already sent (a POST then reads as no answer).
inline std::string requestBody(const std::string& head, net::TcpConnection& c) {
    const auto end = head.find("\r\n\r\n");
    if (end == std::string::npos) return {};
    std::string lower = head.substr(0, end);
    for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    std::size_t length = 0;
    if (const auto at = lower.find("\r\ncontent-length:"); at != std::string::npos)
        length = static_cast<std::size_t>(std::strtoull(lower.c_str() + at + 17, nullptr, 10));
    auto body = head.substr(end + 4);
    while (body.size() < length) {
        const auto more = c.readUntil("\x01never", length - body.size(), 2000);
        if (more.empty()) break;
        body += more;
    }
    return body;
}

inline std::string requestPath(const std::string& head) {
    const auto start = head.find(' ') + 1;
    return head.substr(start, head.find(' ', start) - start);
}

}  // namespace fcg::test
