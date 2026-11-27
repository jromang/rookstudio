// What Lichess asks of its API's clients, for every host (https.hpp): HostGate's turns and pauses, and the polite entry
// points that go through them.

#include <algorithm>
#include <chrono>
#include <thread>

#include "https.hpp"

namespace fcg {

HostGate& HostGate::instance() {
    static HostGate gate;
    return gate;
}

HostGate::Host& HostGate::host(const std::string& name) {
    std::lock_guard lock(mutex_);
    auto& h = hosts_[name];
    if (!h) h = std::make_unique<Host>();
    return *h;
}

std::unique_lock<std::timed_mutex> HostGate::turn(const std::string& name, const std::atomic<bool>* cancel) {
    auto& h = host(name);
    std::unique_lock lock(h.turn, std::defer_lock);
    while (!lock.try_lock_for(std::chrono::milliseconds(100))) {
        if (cancel && cancel->load()) return lock;
    }
    if (!waitPause(name, cancel)) lock.unlock();
    return lock;
}

bool HostGate::waitPause(const std::string& name, const std::atomic<bool>* cancel) {
    for (;;) {
        const auto left = pausedFor(name);
        if (left.count() == 0) return true;
        if (cancel && cancel->load()) return false;
        std::this_thread::sleep_for(std::min(left, std::chrono::milliseconds(100)));
    }
}

void HostGate::tooMany(const std::string& name) {
    auto& h = host(name);
    std::lock_guard lock(mutex_);
    h.until = std::chrono::steady_clock::now() + pause_;
}

std::chrono::milliseconds HostGate::pausedFor(const std::string& name) const {
    std::lock_guard lock(mutex_);
    const auto it = hosts_.find(name);
    if (it == hosts_.end()) return std::chrono::milliseconds(0);
    const auto left =
        std::chrono::duration_cast<std::chrono::milliseconds>(it->second->until - std::chrono::steady_clock::now());
    return std::max(std::chrono::milliseconds(0), left + std::chrono::milliseconds(left.count() > 0 ? 1 : 0));
}

void HostGate::setPause(std::chrono::milliseconds pause) {
    std::lock_guard lock(mutex_);
    pause_ = pause;
}

HttpResult politeRequest(const HttpRequest& request, const std::atomic<bool>* cancel) {
    const auto host = urlHost(request.url);
    if (host.empty()) return httpRequest(request, cancel);
    auto& gate      = HostGate::instance();
    const auto lock = gate.turn(host, cancel);
    if (!lock.owns_lock()) {
        HttpResult result;
        result.error = "cancelled";
        return result;
    }
    auto result = httpRequest(request, cancel);
    if (result.status == 429) gate.tooMany(host);
    return result;
}

HttpResult politeStream(const HttpRequest& request, const HttpStream& stream) {
    const auto host = urlHost(request.url);
    if (host.empty()) return httpStream(request, stream);
    auto& gate = HostGate::instance();
    if (!gate.waitPause(host, stream.cancel)) {
        HttpResult result;
        result.error = "cancelled";
        return result;
    }
    auto result = httpStream(request, stream);
    if (result.status == 429) gate.tooMany(host);
    return result;
}

Download politeDownload(const std::string& url, const std::filesystem::path& file, DownloadProgress* progress,
                        const std::atomic<bool>* cancel, std::string_view sha256) {
    const auto host = urlHost(url);
    if (host.empty()) return httpDownload(url, file, progress, cancel, sha256);
    auto& gate = HostGate::instance();
    if (!gate.waitPause(host, cancel)) {
        Download out;
        out.error = "cancelled";
        return out;
    }
    auto out = httpDownload(url, file, progress, cancel, sha256);
    if (out.status == 429) gate.tooMany(host);
    return out;
}

}  // namespace fcg
