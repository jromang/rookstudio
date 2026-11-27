// What Lichess asks of its API's clients, for every host (https.hpp): HostGate's turns and pauses, and the polite entry
// points that go through them.

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

#include "https.hpp"

namespace fcg {

namespace {

// A number of digits only, none otherwise; too big for 64 bits, the largest.
std::optional<uint64_t> digits(std::string_view text) {
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return std::nullopt;
    uint64_t n    = 0;
    const auto at = std::from_chars(text.data(), text.data() + text.size(), n);
    return at.ec == std::errc() ? n : std::numeric_limits<uint64_t>::max();
}

constexpr std::array<std::string_view, 12> MONTHS = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// The time an HTTP date names (RFC 9110, section 5.6.7: "Sun, 06 Nov 1994 08:49:37 GMT", the obsolete
// "Sunday, 06-Nov-94 08:49:37 GMT" and "Sun Nov  6 08:49:37 1994"), always in GMT; none when it is not one.
std::optional<std::chrono::system_clock::time_point> httpDate(std::string_view text) {
    std::vector<std::string_view> words;
    for (std::size_t at = 0; at < text.size();) {
        const auto end = text.find_first_of(" ,-", at);
        const auto len = (end == std::string_view::npos ? text.size() : end) - at;
        if (len > 0) words.push_back(text.substr(at, len));
        at += len + 1;
    }
    if (words.size() < 5) return std::nullopt;
    const auto month = [](std::string_view w) -> unsigned {
        const auto it = std::find(MONTHS.begin(), MONTHS.end(), w);
        return it == MONTHS.end() ? 0 : static_cast<unsigned>(it - MONTHS.begin() + 1);
    };
    // After the day's name: day month year time (IMF-fixdate, RFC 850), or month day time year (asctime).
    const bool ansi  = month(words[1]) != 0;
    const auto day   = digits(words[ansi ? 2 : 1]);
    const unsigned m = month(words[ansi ? 1 : 2]);
    auto year        = digits(words[ansi ? 4 : 3]);
    const auto hms   = words[ansi ? 3 : 4];
    if (!day || m == 0 || !year || hms.size() != 8 || hms[2] != ':' || hms[5] != ':') return std::nullopt;
    const auto h = digits(hms.substr(0, 2)), mi = digits(hms.substr(3, 2)), sec = digits(hms.substr(6, 2));
    if (!h || !mi || !sec || *h > 23 || *mi > 59 || *sec > 60) return std::nullopt;
    if (words[ansi ? 4 : 3].size() == 2) *year += *year < 70 ? 2000 : 1900;  // RFC 850's two digits
    if (*year < 1970 || *year > 9999) return std::nullopt;
    using namespace std::chrono;
    const year_month_day date{std::chrono::year{static_cast<int>(*year)}, std::chrono::month{m},
                              std::chrono::day{static_cast<unsigned>(*day)}};
    if (!date.ok()) return std::nullopt;
    return time_point_cast<system_clock::duration>(sys_days{date} + hours(*h) + minutes(*mi) + seconds(*sec));
}

// What a 429's head asks for, read by retryAfter().
std::optional<std::chrono::seconds> retryAfterIn(std::string_view head) {
    const auto value = httpHeader(head, "Retry-After");
    return value ? retryAfter(*value) : std::nullopt;
}

}  // namespace

std::optional<std::chrono::seconds> retryAfter(std::string_view value, std::chrono::system_clock::time_point now) {
    using namespace std::chrono;
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
    seconds asked{};
    if (const auto n = digits(value)) {
        asked = seconds(static_cast<seconds::rep>(std::min<uint64_t>(*n, RETRY_AFTER_MOST.count())));
    } else if (const auto when = httpDate(value)) {
        asked = ceil<seconds>(*when - now);
    } else {
        return std::nullopt;
    }
    return std::clamp(asked, RETRY_AFTER_LEAST, RETRY_AFTER_MOST);
}

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

void HostGate::tooMany(const std::string& name, std::optional<std::chrono::seconds> asked) {
    auto& h = host(name);
    std::lock_guard lock(mutex_);
    // Never shorter than the pause: Lichess asks for a full minute after a 429, whatever a Retry-After says.
    h.until = std::chrono::steady_clock::now() +
              (asked ? std::max(pause_, std::chrono::duration_cast<std::chrono::milliseconds>(*asked)) : pause_);
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
    if (result.status == 429) gate.tooMany(host, retryAfterIn(result.head));
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
    // A 429's Retry-After, read from its head as it comes.
    std::optional<std::chrono::seconds> asked;
    auto watched    = stream;
    watched.on_head = [&](int status, std::string_view head) {
        if (status == 429) asked = retryAfterIn(head);
        return !stream.on_head || stream.on_head(status, head);
    };
    auto result = httpStream(request, watched);
    if (result.status == 429) gate.tooMany(host, asked);
    return result;
}

Download politeDownload(const std::string& url, const std::filesystem::path& file, DownloadProgress* progress,
                        const std::atomic<bool>* cancel, std::string_view sha256) {
    const auto host = urlHost(url);
    if (host.empty()) return httpDownload(url, file, progress, cancel, sha256);
    auto& gate = HostGate::instance();
    if (!gate.waitPause(host, cancel)) {
        Download out;
        out.error          = "cancelled";
        out.failure.kind   = NetFailure::Kind::Cancelled;
        out.failure.host   = host;
        out.failure.path   = file;
        out.failure.detail = out.error;
        return out;
    }
    auto out = httpDownload(url, file, progress, cancel, sha256);
    if (out.status == 429) gate.tooMany(host, out.retry_after);
    return out;
}

}  // namespace fcg
