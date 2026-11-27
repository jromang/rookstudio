#pragma once

// A server on this machine that answers every request with the status and the body a test set last: the failures a
// Lichess client must tell apart (#106), one after the other, and Lichess's servers pointed at it meanwhile.

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

#include <fmt/include/fmt/format.h>

#include "free_ports.hpp"
#include "lichess_api.hpp"
#include "scripted_server.hpp"

namespace fcg::test {

class AnsweringServer {
   public:
    AnsweringServer()
        : server_(0, [this](const std::string& head, net::TcpConnection& c) {
              (void)requestBody(head, c);  // read whole before the answer (Windows: scripted_server.hpp)
              std::pair<int, std::string> now;
              {
                  std::lock_guard lock(mutex_);
                  now = next_;
              }
              c.sendAll(
                  fmt::format("HTTP/1.1 {} X\r\nContent-Type: application/json\r\nContent-Length: {}\r\n"
                              "Connection: close\r\n\r\n{}",
                              now.first, now.second.size(), now.second));
          }) {
        const auto base    = this->base();
        lichess::servers() = lichess::Servers{base, base + "/tb", base + "/explorer", base + "/engine"};
    }
    ~AnsweringServer() { lichess::servers() = lichess::Servers{}; }
    AnsweringServer(const AnsweringServer&)            = delete;
    AnsweringServer& operator=(const AnsweringServer&) = delete;

    [[nodiscard]] bool ok() const { return server_.ok(); }
    [[nodiscard]] std::string base() const { return fmt::format("http://127.0.0.1:{}", server_.port()); }
    // What every request is answered with from now on.
    void answer(int status, std::string body) {
        std::lock_guard lock(mutex_);
        next_ = {status, std::move(body)};
    }

   private:
    std::mutex mutex_;
    std::pair<int, std::string> next_{200, "{}"};
    ScriptedServer server_;  // last: stopped first, its threads using what is above
};

// Lichess's servers pointed where nothing listens: no answer (freePort(): a free port nothing is started on).
inline void pointNowhere() {
    const auto nobody  = fmt::format("http://127.0.0.1:{}", freePort());
    lichess::servers() = lichess::Servers{nobody, nobody + "/tb", nobody + "/explorer", nobody + "/engine"};
}

// Lichess's servers pointed at a host that pauses after a 429, the pause a minute long: a request cancelled meanwhile
// goes nowhere. `host` is the test's own (a name under .invalid), never asked.
inline void pointAtPausedHost(const std::string& host) {
    HostGate::instance().setPause(std::chrono::seconds(60));
    HostGate::instance().tooMany(host);
    const auto base    = "http://" + host;
    lichess::servers() = lichess::Servers{base, base + "/tb", base + "/explorer", base + "/engine"};
}

}  // namespace fcg::test
