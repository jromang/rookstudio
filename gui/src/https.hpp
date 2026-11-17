#pragma once

// HTTP and HTTPS requests, for the lichess broadcasts and the check for a new version: Mbed TLS, linked in, trusting
// only the root certificates of tls_roots.cpp, and a client of the little HTTP/1.1 they need. HTTPS goes through
// the proxy the environment names, if any.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fcg {

struct HttpResult {
    int status = 0;  // HTTP status, 0 when there was no answer
    std::string body;
    std::string error;  // why there was no answer
};

struct HttpRequest {
    std::string url;           // http:// or https://
    std::string token;         // sent as "Authorization: Bearer <token>" when not empty
    std::string content_type;  // of the body
    std::string body;
    int timeout_s      = 20;
    std::string method = "POST";
    std::vector<std::pair<std::string, std::string>> headers;  // more of them: (name, value)
};

// The request, sent with the User-Agent "fastchess-gui/<version>".
[[nodiscard]] HttpResult httpRequest(const HttpRequest& request);
// The same, as a POST.
[[nodiscard]] HttpResult httpPost(const HttpRequest& request);

// application/x-www-form-urlencoded, from (name, value) pairs.
[[nodiscard]] std::string formEncode(const std::vector<std::pair<std::string, std::string>>& fields);

// A body sent with Transfer-Encoding: chunked, put back together; the text as it is when it is not one.
[[nodiscard]] std::string unchunk(std::string_view body);

// The proxy an HTTPS connection to `host` goes through, from the environment as curl reads it: https_proxy or
// HTTPS_PROXY, otherwise all_proxy or ALL_PROXY; none when no_proxy or NO_PROXY names the host. An http:// proxy,
// with a user and a password or not, through which the connection is tunnelled with CONNECT.
struct Proxy {
    std::string host;
    std::string port;
    std::string authorization;  // the value of Proxy-Authorization, empty without a user
    std::string variable;       // the variable it comes from, to name it in messages
    std::string error;          // why it cannot be used (a proxy set is not passed by for all that)
};
using Environment = std::function<std::optional<std::string>(const char* name)>;
[[nodiscard]] std::optional<Proxy> proxyFor(std::string_view host, const Environment& environment);
// From the environment of the program.
[[nodiscard]] std::optional<Proxy> proxyFor(std::string_view host);

// True when `list`, as NO_PROXY has it ("*", or names and addresses separated by commas), names `host`: a name
// also covers the names under it.
[[nodiscard]] bool noProxy(std::string_view list, std::string_view host);

// A response read as it comes, for Lichess's streams (NDJSON events, a game's or a broadcast's moves as they are
// played) and big downloads: its status and head first, then its body a piece at a time, a chunked body decoded.
struct HttpStream {
    // The status and the head of the answer as it came (its status line and headers, without the blank line after
    // them; httpHeader() reads one); false stops.
    std::function<bool(int status, std::string_view head)> on_head;
    // A piece of the body; false stops.
    std::function<bool(std::string_view data)> on_data;
    const std::atomic<bool>* cancel = nullptr;  // set: stops within a quarter of a second
    int idle_s                      = 30;       // nothing received for so long: the server is given up
    int redirects = 5;  // followed at most (301, 302, 303, 307, 308); the token goes to its own site only
};

// The request, its answer given to `stream` as it comes. The result's body stays empty; its error says why the
// answer stopped early ("cancelled" when `cancel` stopped it), empty when it came whole or `stream` stopped it.
[[nodiscard]] HttpResult httpStream(const HttpRequest& request, const HttpStream& stream);

// A header's value in the head of an answer, its name matched whatever its case; none when it is not there.
[[nodiscard]] std::optional<std::string> httpHeader(std::string_view head, std::string_view name);

// A chunked body (Transfer-Encoding: chunked) decoded a piece at a time, as it comes.
class ChunkDecoder {
   public:
    // The data of `in` given to `out`. False when `out` asked to stop, or when the chunks are not well formed
    // (failed()).
    bool feed(std::string_view in, const std::function<bool(std::string_view)>& out);
    [[nodiscard]] bool done() const noexcept { return state_ == State::Done; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }

   private:
    enum class State { Size, Data, DataEnd, Trailer, Done };
    State state_ = State::Size;
    std::string line_;
    uint64_t left_ = 0;
    bool failed_   = false;
};

// The lines of a stream (NDJSON): bytes in, each whole line out without its end ("\n" or "\r\n"), the empty lines
// (a stream's keep-alives) left out. A line still open at the end comes out at flush().
class LineSplitter {
   public:
    explicit LineSplitter(std::function<bool(std::string_view line)> on_line) : on_line_(std::move(on_line)) {}
    bool feed(std::string_view data);  // false when a line's handler asked to stop
    bool flush();

   private:
    std::function<bool(std::string_view)> on_line_;
    std::string pending_;
};

struct DownloadProgress {
    std::atomic<uint64_t> bytes{0};  // of the file, written so far (what an earlier try kept included)
    std::atomic<uint64_t> size{0};   // of the whole file, 0 while not known
};

struct Download {
    bool ok = false;
    std::string error;  // "cancelled" when `cancel` stopped it
    uint64_t bytes = 0;
    bool resumed   = false;  // went on from where an earlier try stopped
};

// `url` saved as `file`, its folder made if need be. Written to <file>.part, which a later call resumes from where it
// stopped when the server takes a Range (and the file there has not changed since), then renamed. Checked against
// `sha256` (hexadecimal) when given: a file that differs is removed.
[[nodiscard]] Download httpDownload(const std::string& url, const std::filesystem::path& file,
                                    DownloadProgress* progress = nullptr, const std::atomic<bool>* cancel = nullptr,
                                    std::string_view sha256 = {});

// The SHA-256 of a file, in hexadecimal; empty when it cannot be read.
[[nodiscard]] std::string sha256File(const std::filesystem::path& file);

// What Lichess asks of its API's clients: one request at a time to a host, and a pause of a full minute after it
// answered 429 (Too Many Requests).
class HostGate {
   public:
    static HostGate& instance();
    // Waits for `host`'s turn, then for its pause to end; the turn is held while the lock lives. A lock that owns
    // nothing when `cancel` ended the wait.
    [[nodiscard]] std::unique_lock<std::timed_mutex> turn(const std::string& host,
                                                          const std::atomic<bool>* cancel = nullptr);
    // Waits for `host`'s pause to end (true), or for `cancel` (false).
    bool waitPause(const std::string& host, const std::atomic<bool>* cancel = nullptr);
    void tooMany(const std::string& host);  // the host answered 429
    [[nodiscard]] std::chrono::milliseconds pausedFor(const std::string& host) const;
    void setPause(std::chrono::milliseconds pause);  // a minute; the tests make it shorter

   private:
    struct Host {
        std::timed_mutex turn;
        std::chrono::steady_clock::time_point until{};
    };
    Host& host(const std::string& name);

    mutable std::mutex mutex_;
    std::map<std::string, std::unique_ptr<Host>> hosts_;
    std::chrono::milliseconds pause_{60000};
};

// httpRequest in its host's turn, once its pause is over; a 429 pauses the host.
[[nodiscard]] HttpResult politeRequest(const HttpRequest& request, const std::atomic<bool>* cancel = nullptr);
// httpStream once its host's pause is over, taking no turn (Lichess lets a few streams stay open beside the
// requests); a 429 pauses the host.
[[nodiscard]] HttpResult politeStream(const HttpRequest& request, const HttpStream& stream);

}  // namespace fcg
