#include "https.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <vector>

#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <fmt/include/fmt/format.h>

#include "console.hpp"
#include "net.hpp"
#include "tls_roots.hpp"

namespace fcg {

namespace {

struct Url {
    bool tls = true;
    std::string host;
    std::string port;
    std::string path;
};

std::optional<Url> parseUrl(std::string_view url) {
    Url u;
    if (url.rfind("https://", 0) == 0) {
        url.remove_prefix(8);
    } else if (url.rfind("http://", 0) == 0) {
        u.tls = false;
        url.remove_prefix(7);
    } else {
        return std::nullopt;
    }
    const auto slash = url.find('/');
    const auto host  = url.substr(0, slash);
    u.path           = slash == std::string_view::npos ? "/" : std::string(url.substr(slash));
    const auto colon = host.rfind(':');
    u.host           = std::string(host.substr(0, colon));
    u.port           = colon == std::string_view::npos ? (u.tls ? "443" : "80") : std::string(host.substr(colon + 1));
    if (u.host.empty()) return std::nullopt;
    return u;
}

// GitHub's API refuses a request without one.
constexpr const char* USER_AGENT = "RookStudio/" FCG_VERSION " (+https://github.com/jromang/rookstudio)";

// A server that answers at all connects in far less.
constexpr int CONNECT_TIMEOUT_MS = 10000;

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string base64(std::string_view data) {
    static constexpr char DIGITS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = static_cast<uint32_t>(static_cast<unsigned char>(data[i])) << 16;
        if (i + 1 < data.size()) n |= static_cast<uint32_t>(static_cast<unsigned char>(data[i + 1])) << 8;
        if (i + 2 < data.size()) n |= static_cast<unsigned char>(data[i + 2]);
        out += DIGITS[(n >> 18) & 63];
        out += DIGITS[(n >> 12) & 63];
        out += i + 1 < data.size() ? DIGITS[(n >> 6) & 63] : '=';
        out += i + 2 < data.size() ? DIGITS[n & 63] : '=';
    }
    return out;
}

// "p%40ss" is "p@ss": a user or a password in a URL is percent-encoded.
std::string percentDecode(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            out += static_cast<char>(std::stoi(std::string(text.substr(i + 1, 2)), nullptr, 16));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

// http://[user:password@]host[:port][/], or host[:port] alone. The messages never repeat the user nor the password.
Proxy parseProxy(std::string_view text, const char* variable) {
    Proxy proxy;
    proxy.variable = variable;
    if (const auto scheme = text.find("://"); scheme != std::string_view::npos) {
        if (lower(std::string(text.substr(0, scheme))) != "http") {
            proxy.error =
                fmt::format("{}: only http:// proxies can be used, not {}://", variable, text.substr(0, scheme));
            return proxy;
        }
        text.remove_prefix(scheme + 3);
    }
    text = text.substr(0, text.find_first_of("/?#"));
    if (const auto at = text.rfind('@'); at != std::string_view::npos) {
        const auto user     = text.substr(0, at);
        const auto colon    = user.find(':');
        const auto password = colon == std::string_view::npos ? std::string{} : percentDecode(user.substr(colon + 1));
        proxy.authorization = "Basic " + base64(percentDecode(user.substr(0, colon)) + ":" + password);
        text.remove_prefix(at + 1);
    }
    std::string_view port;
    if (text.rfind('[', 0) == 0 && text.find(']') != std::string_view::npos) {  // [::1]:3128
        const auto close = text.find(']');
        proxy.host       = std::string(text.substr(1, close - 1));
        if (text.substr(close + 1).rfind(':', 0) == 0) port = text.substr(close + 2);
    } else {
        const auto colon = text.rfind(':');
        proxy.host       = std::string(text.substr(0, colon));
        if (colon != std::string_view::npos) port = text.substr(colon + 1);
    }
    proxy.port        = port.empty() ? "1080" : std::string(port);  // curl's default
    const bool digits = proxy.port.size() <= 5 && std::all_of(proxy.port.begin(), proxy.port.end(),
                                                              [](unsigned char c) { return std::isdigit(c) != 0; });
    if (proxy.host.empty() || !digits || std::stoi(proxy.port) < 1 || std::stoi(proxy.port) > 65535) {
        proxy.error = fmt::format("{}: not a proxy's address", variable);
    }
    return proxy;
}

std::optional<std::string> environmentVariable(const char* name) {
    const char* value = std::getenv(name);
    if (!value) return std::nullopt;
    return std::string(value);
}

int msLeft(std::chrono::steady_clock::time_point deadline) {
    const auto left =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    return static_cast<int>(std::clamp<long long>(left, 0, 1 << 30));
}

std::string mbedError(int code) {
    char text[200];
    mbedtls_strerror(code, text, sizeof(text));
    return fmt::format("{} (-0x{:04x})", text, -code);
}

// The connection, plain or TLS, closed when it goes out of scope.
class Connection {
   public:
    Connection() {
        mbedtls_net_init(&net_);
        mbedtls_ssl_init(&ssl_);
        mbedtls_ssl_config_init(&conf_);
        mbedtls_x509_crt_init(&roots_);
    }
    ~Connection() {
        if (tls_) mbedtls_ssl_close_notify(&ssl_);
        mbedtls_net_free(&net_);
        mbedtls_ssl_free(&ssl_);
        mbedtls_ssl_config_free(&conf_);
        mbedtls_x509_crt_free(&roots_);
    }
    Connection(const Connection&)            = delete;
    Connection& operator=(const Connection&) = delete;

    // An error message, empty once connected (and the server's certificate checked). Through `proxy`, when given.
    // `cancel`, when given, ends the wait for the connection ("cancelled").
    std::string open(const Url& url, const std::optional<Proxy>& proxy, int timeout_ms,
                     const std::atomic<bool>* cancel = nullptr) {
        timeout_ms_ = static_cast<uint32_t>(timeout_ms);
        // Our own connect, which gives up in time (mbedtls_net_connect waits as long as the system does), and whose
        // socket the engines started meanwhile do not inherit. Mbed TLS keeps sockets as int, on Windows too.
        std::string error;
        const auto fd = net::connectHost(proxy ? proxy->host : url.host, proxy ? proxy->port : url.port,
                                         std::min(timeout_ms, CONNECT_TIMEOUT_MS), error, cancel);
        if (cancel && cancel->load()) return "cancelled";
        if (!fd) return proxy ? fmt::format("{} (the proxy of {})", error, proxy->variable) : error;
        net_.fd = static_cast<int>(*fd);
        if (proxy) {
            if (auto refused = tunnel(url, *proxy); !refused.empty()) return refused;
        }
        if (!url.tls) return {};
        tls_ = true;

        if (psa_crypto_init() != PSA_SUCCESS) return "cannot start the cryptography";
        // The PEM text with its final NUL, as mbedtls_x509_crt_parse wants it.
        const auto* pem = reinterpret_cast<const unsigned char*>(TLS_ROOTS);
        if (const int rc = mbedtls_x509_crt_parse(&roots_, pem, std::char_traits<char>::length(TLS_ROOTS) + 1)) {
            return "bad root certificates: " + mbedError(rc);
        }
        if (const int rc = mbedtls_ssl_config_defaults(&conf_, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                                       MBEDTLS_SSL_PRESET_DEFAULT)) {
            return mbedError(rc);
        }
        // The server's certificate must chain to one of the roots and name the host.
        mbedtls_ssl_conf_authmode(&conf_, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&conf_, &roots_, nullptr);
        mbedtls_ssl_conf_read_timeout(&conf_, timeout_ms_);
        if (const int rc = mbedtls_ssl_setup(&ssl_, &conf_)) return mbedError(rc);
        if (const int rc = mbedtls_ssl_set_hostname(&ssl_, url.host.c_str())) return mbedError(rc);
        mbedtls_ssl_set_bio(&ssl_, &net_, mbedtls_net_send, nullptr, mbedtls_net_recv_timeout);

        int rc = 0;
        while ((rc = mbedtls_ssl_handshake(&ssl_)) != 0) {
            if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            if (const auto flags = mbedtls_ssl_get_verify_result(&ssl_); flags != 0 && flags != uint32_t(-1)) {
                char why[256];
                mbedtls_x509_crt_verify_info(why, sizeof(why), "", flags);
                std::string text(why);
                while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
                return "the certificate of " + url.host + " is not trusted: " + text;
            }
            return "TLS with " + url.host + ": " + mbedError(rc);
        }
        return {};
    }

    bool send(std::string_view data) {
        // A send that times out comes back as WANT_WRITE: tried again until the deadline only.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);
        while (!data.empty()) {
            const auto* p = reinterpret_cast<const unsigned char*>(data.data());
            const int n   = tls_ ? mbedtls_ssl_write(&ssl_, p, data.size()) : mbedtls_net_send(&net_, p, data.size());
            if ((n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) &&
                std::chrono::steady_clock::now() < deadline) {
                continue;
            }
            if (n <= 0) return false;
            data.remove_prefix(static_cast<std::size_t>(n));
        }
        return true;
    }

    // Up to `size` bytes, waiting `wait_ms` at most for the first: their number, 0 once the server closed, -1 when
    // nothing came in time, -2 on an error. A TLS read that times out leaves the connection unusable, so the socket
    // is awaited instead, and a record that has begun to come is read whole (within the timeout of open()).
    int receiveSome(unsigned char* buffer, std::size_t size, uint32_t wait_ms) {
        if (!tls_) {
            const int n = mbedtls_net_recv_timeout(&net_, buffer, size, wait_ms);
            if (n > 0) return n;
            if (n == 0 || n == MBEDTLS_ERR_NET_CONN_RESET) return 0;
            return n == MBEDTLS_ERR_SSL_TIMEOUT || n == MBEDTLS_ERR_SSL_WANT_READ ? -1 : -2;
        }
        if (mbedtls_ssl_get_bytes_avail(&ssl_) == 0 && mbedtls_ssl_check_pending(&ssl_) == 0) {
            const int ready = mbedtls_net_poll(&net_, MBEDTLS_NET_POLL_READ, wait_ms);
            if (ready == 0) return -1;
            if (ready < 0) return -2;
        }
        const int n = mbedtls_ssl_read(&ssl_, buffer, size);
        if (n > 0) return n;
        if (n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == MBEDTLS_ERR_NET_CONN_RESET) return 0;
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE ||
            n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
            return -1;  // a record that was not data
        return -2;
    }

   private:
    // Asks the proxy for a tunnel to the server, through which TLS then goes as it would directly, the server's name
    // in its handshake. An error message, empty once the tunnel is open.
    std::string tunnel(const Url& url, const Proxy& proxy) {
        const auto target =
            fmt::format("{}:{}", url.host.find(':') != std::string::npos ? "[" + url.host + "]" : url.host, url.port);
        auto request = fmt::format("CONNECT {0} HTTP/1.1\r\nHost: {0}\r\nUser-Agent: {1}\r\n", target, USER_AGENT);
        if (!proxy.authorization.empty()) request += "Proxy-Authorization: " + proxy.authorization + "\r\n";
        request += "\r\n";
        if (!send(request)) return "the request could not be sent to the proxy " + proxy.host;

        // Its answer, read up to the end of its headers and no further: what follows is the server's.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);
        std::string answer;
        while (answer.size() < 4 || answer.compare(answer.size() - 4, 4, "\r\n\r\n") != 0) {
            const int left = msLeft(deadline);
            if (left <= 0) return "no answer from the proxy " + proxy.host + " in time";
            if (answer.size() > 65536) return "not an HTTP answer from the proxy " + proxy.host;
            unsigned char c = 0;
            const int n     = mbedtls_net_recv_timeout(&net_, &c, 1, static_cast<uint32_t>(left));
            if (n == 1) {
                answer += static_cast<char>(c);
            } else if (n != MBEDTLS_ERR_SSL_TIMEOUT && n != MBEDTLS_ERR_SSL_WANT_READ) {
                return "the proxy " + proxy.host + " closed the connection";
            }
        }
        // "HTTP/1.1 200 Connection established"
        const auto line = answer.substr(0, answer.find("\r\n"));
        const auto code = line.find(' ');
        if (line.rfind("HTTP/", 0) != 0 || code == std::string::npos || line.size() < code + 4) {
            return "not an HTTP answer from the proxy " + proxy.host;
        }
        if (line[code + 1] != '2') {
            return fmt::format("the proxy {} refused the connection to {}: {}", proxy.host, url.host,
                               line.substr(code + 1));
        }
        return {};
    }

    mbedtls_net_context net_;
    mbedtls_ssl_context ssl_;
    mbedtls_ssl_config conf_;
    mbedtls_x509_crt roots_;
    bool tls_            = false;
    uint32_t timeout_ms_ = 20000;
};

// The connection to `url`'s server opened, through the proxy the environment names for it (HTTPS only: plain HTTP is
// for the tests, on this machine). An error message, empty once open; "cancelled" when `cancel` ended the wait.
std::string connect(Connection& connection, const Url& url, int timeout_s, const std::atomic<bool>* cancel) {
    const auto proxy = url.tls ? proxyFor(url.host) : std::nullopt;
    if (proxy && !proxy->error.empty()) return proxy->error;
    return connection.open(url, proxy, timeout_s * 1000, cancel);
}

// How a request's body goes: of a length its Content-Length gives, or in chunks as it is made (httpUpload).
enum class Body { Sized, Chunked };

// The head of `request` to `url`: its request line and its headers, up to the blank line that ends them. A body of a
// length is announced by its Content-Length, but for a method that sends none (GET, HEAD, DELETE…) when it has none:
// a Content-Length there is what RFC 9110 (8.6) asks a client not to send. POST, PUT and PATCH say 0.
std::string requestHead(const HttpRequest& request, const Url& url, Body body) {
    std::string head =
        fmt::format("{} {} HTTP/1.1\r\nHost: {}\r\nUser-Agent: {}\r\n", request.method, url.path, url.host, USER_AGENT);
    if (!request.token.empty()) head += "Authorization: Bearer " + request.token + "\r\n";
    if (!request.content_type.empty()) head += "Content-Type: " + request.content_type + "\r\n";
    for (const auto& [name, value] : request.headers) head += name + ": " + value + "\r\n";
    if (body == Body::Chunked) {
        head += "Transfer-Encoding: chunked\r\n";
    } else if (!request.body.empty() || request.method == "POST" || request.method == "PUT" ||
               request.method == "PATCH") {
        head += fmt::format("Content-Length: {}\r\n", request.body.size());
    }
    return head + "Connection: close\r\n\r\n";
}

// What leaves the machine, in the log (R16): the address asked, with what it carries (a FEN, a player's name), and
// whether the account's token went with it, never the token itself.
void logRequest(const HttpRequest& request) {
    Console::instance().add(
        fmt::format("{} {}{}", request.method, request.url, request.token.empty() ? "" : " (with the account's token)"),
        false, "network");
}

// `request` sent to `url` on `connection`, the one way every request starts: in the log first (unless it is not
// `logged`), then the connection opened (connect()), its head written (requestHead()), and its body when it is of a
// length (a chunked one is the caller's to send). An error message, empty once sent; "cancelled", and nothing sent nor
// logged, when `cancel` is set already.
std::string sendRequest(Connection& connection, const HttpRequest& request, const Url& url, Body body,
                        const std::atomic<bool>* cancel) {
    if (cancel && cancel->load()) return "cancelled";
    if (request.logged) logRequest(request);
    if (auto error = connect(connection, url, request.timeout_s, cancel); !error.empty()) return error;
    if (!connection.send(requestHead(request, url, body)) || (body == Body::Sized && !connection.send(request.body)))
        return "the request could not be sent to " + url.host;
    return {};
}

}  // namespace

std::string unchunk(std::string_view body) {
    std::string out;
    std::size_t at = 0;
    while (at < body.size()) {
        const auto eol = body.find("\r\n", at);
        if (eol == std::string_view::npos) return std::string(body);
        std::size_t size = 0;
        try {
            size = std::stoul(std::string(body.substr(at, eol - at)), nullptr, 16);
        } catch (...) {
            return std::string(body);
        }
        if (size == 0) return out;
        if (eol + 2 + size > body.size()) return std::string(body);
        out.append(body.substr(eol + 2, size));
        at = eol + 2 + size + 2;  // the chunk, then its CRLF
    }
    return out;
}

std::string formEncode(const std::vector<std::pair<std::string, std::string>>& fields) {
    const auto encode = [](std::string_view s) {
        std::string out;
        for (const unsigned char c : s) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                out += static_cast<char>(c);
            } else if (c == ' ') {
                out += '+';
            } else {
                out += fmt::format("%{:02X}", c);
            }
        }
        return out;
    };
    std::string out;
    for (const auto& [name, value] : fields) {
        if (!out.empty()) out += '&';
        out += encode(name);
        out += '=';
        out += encode(value);
    }
    return out;
}

bool noProxy(std::string_view list, std::string_view host) {
    const auto bare = [](std::string text) {
        if (text.size() > 1 && text.front() == '[' && text.find(']') != std::string::npos) {
            return text.substr(1, text.find(']') - 1);  // [::1], [::1]:8080
        }
        if (std::count(text.begin(), text.end(), ':') == 1) text.erase(text.find(':'));  // the port of name:port
        while (!text.empty() && text.front() == '.') text.erase(0, 1);                   // .example.com
        while (!text.empty() && text.back() == '.') text.pop_back();                     // a fully qualified name
        return text;
    };
    const auto name = bare(lower(std::string(host)));
    std::size_t at  = 0;
    while (at < list.size()) {
        const auto end   = std::min(list.find_first_of(", \t", at), list.size());
        const auto entry = bare(lower(std::string(list.substr(at, end - at))));
        at               = end + 1;
        if (entry.empty()) continue;
        if (entry == "*" || entry == name) return true;
        if (name.size() > entry.size() && name.compare(name.size() - entry.size(), entry.size(), entry) == 0 &&
            name[name.size() - entry.size() - 1] == '.') {
            return true;
        }
    }
    return false;
}

std::optional<Proxy> proxyFor(std::string_view host, const Environment& environment) {
    // As curl: the lowercase name first, and a variable set empty is not set.
    const auto first = [&](const char* a, const char* b) -> std::optional<std::pair<std::string, const char*>> {
        for (const char* name : {a, b}) {
            if (auto value = environment(name); value && !value->empty()) return std::pair{*value, name};
        }
        return std::nullopt;
    };
    auto setting = first("https_proxy", "HTTPS_PROXY");
    if (!setting) setting = first("all_proxy", "ALL_PROXY");
    if (!setting) return std::nullopt;
    if (const auto passed = first("no_proxy", "NO_PROXY"); passed && noProxy(passed->first, host)) return std::nullopt;
    return parseProxy(setting->first, setting->second);
}

std::optional<Proxy> proxyFor(std::string_view host) { return proxyFor(host, environmentVariable); }

HttpResult httpPost(const HttpRequest& request) {
    auto post   = request;
    post.method = "POST";
    return httpRequest(post);
}

HttpResult httpRequest(const HttpRequest& request, const std::atomic<bool>* cancel) {
    // The stream's way, which a cancel ends at once: its answer kept whole, a redirection's as it is.
    std::string head, body;
    HttpStream stream;
    stream.cancel    = cancel;
    stream.idle_s    = request.timeout_s;
    stream.redirects = 0;
    stream.on_head   = [&](int, std::string_view h) {
        head = h;
        return true;
    };
    stream.on_data = [&](std::string_view data) {
        body.append(data);
        return true;
    };
    auto result = httpStream(request, stream);
    result.head = std::move(head);
    result.body = std::move(body);
    return result;
}

namespace {

// The URL a redirection's Location names, from the URL that answered.
std::string resolveLocation(const Url& from, std::string_view location) {
    if (location.rfind("https://", 0) == 0 || location.rfind("http://", 0) == 0) return std::string(location);
    const std::string scheme = from.tls ? "https:" : "http:";
    if (location.rfind("//", 0) == 0) return scheme + std::string(location);
    const bool usual_port = from.port == (from.tls ? "443" : "80");
    const auto origin     = scheme + "//" + from.host + (usual_port ? "" : ":" + from.port);
    if (!location.empty() && location.front() == '/') return origin + std::string(location);
    auto folder = from.path.substr(0, from.path.find('?'));
    folder      = folder.substr(0, folder.rfind('/') + 1);
    return origin + folder + std::string(location);
}

bool redirection(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// One request, its answer given to `stream`; when `follow` and the answer is a redirection that names where to, its
// target in `location` instead, the body left unread.
HttpResult streamOnce(const HttpRequest& request, const Url& url, const HttpStream& stream, bool follow,
                      std::string& location) {
    HttpResult result;
    Connection connection;
    if (auto error = sendRequest(connection, request, url, Body::Sized, stream.cancel); !error.empty()) {
        result.error = std::move(error);
        return result;
    }

    const auto idle    = std::chrono::seconds(std::max(1, stream.idle_s));
    const auto stopped = [&] { return stream.cancel && stream.cancel->load(std::memory_order_relaxed); };
    auto last          = std::chrono::steady_clock::now();
    std::vector<unsigned char> buffer(std::size_t{1} << 16);

    // The status line and the headers.
    std::string answer;
    std::size_t end = std::string::npos;
    while (end == std::string::npos) {
        if (stopped()) {
            result.error = "cancelled";
            return result;
        }
        const int n = connection.receiveSome(buffer.data(), buffer.size(), 250);
        if (n > 0) {
            answer.append(reinterpret_cast<const char*>(buffer.data()), static_cast<std::size_t>(n));
            last = std::chrono::steady_clock::now();
            end  = answer.find("\r\n\r\n");
            if (end == std::string::npos && answer.size() > (std::size_t{1} << 20)) {
                result.error = "not an HTTP answer from " + url.host;
                return result;
            }
        } else if (n == 0 || n == -2) {
            result.error = "no answer from " + url.host;
            return result;
        } else if (std::chrono::steady_clock::now() - last > idle) {
            result.error = "no answer from " + url.host + " in time";
            return result;
        }
    }
    const std::string_view head_in(answer.data(), end);
    const auto space = head_in.find(' ');
    int status       = 0;
    if (head_in.rfind("HTTP/", 0) != 0 || space == std::string_view::npos ||
        std::from_chars(head_in.data() + space + 1, head_in.data() + head_in.size(), status).ec != std::errc()) {
        result.error = "not an HTTP answer from " + url.host;
        return result;
    }
    result.status = status;
    if (follow && redirection(status)) {
        if (const auto to = httpHeader(head_in, "Location"); to && !to->empty()) {
            location = resolveLocation(url, *to);
            return result;
        }
    }
    if (stream.on_head && !stream.on_head(status, head_in)) return result;

    // The body: chunked, of a length given, or up to the end of the connection.
    const auto encoding = httpHeader(head_in, "Transfer-Encoding");
    const bool chunked  = encoding && lower(*encoding).find("chunked") != std::string::npos;
    std::optional<uint64_t> length;
    if (const auto value = httpHeader(head_in, "Content-Length"); value && !chunked) {
        uint64_t v = 0;
        if (std::from_chars(value->data(), value->data() + value->size(), v).ec == std::errc()) length = v;
    }
    if (length && *length == 0) return result;
    if (status == 204 || status == 304 || request.method == "HEAD") return result;
    uint64_t received = 0;
    ChunkDecoder chunks;
    bool more          = true;  // the body goes on
    const auto deliver = [&](std::string_view data) {
        if (data.empty()) return;
        if (chunked) {
            if (!chunks.feed(data, [&](std::string_view piece) { return !stream.on_data || stream.on_data(piece); })) {
                if (chunks.failed()) result.error = "a badly chunked answer from " + url.host;
                more = false;
                return;
            }
            more = !chunks.done();
            return;
        }
        if (length)
            data = data.substr(0, static_cast<std::size_t>(std::min<uint64_t>(data.size(), *length - received)));
        received += data.size();
        if (!data.empty() && stream.on_data && !stream.on_data(data)) more = false;
        if (length && received >= *length) more = false;
    };
    deliver(std::string_view(answer).substr(end + 4));
    while (more) {
        if (stopped()) {
            result.error = "cancelled";
            return result;
        }
        const int n = connection.receiveSome(buffer.data(), buffer.size(), 250);
        if (n > 0) {
            last = std::chrono::steady_clock::now();
            deliver(std::string_view(reinterpret_cast<const char*>(buffer.data()), static_cast<std::size_t>(n)));
        } else if (n == 0) {
            if ((chunked && !chunks.done()) || (length && received < *length)) {
                result.error = "the answer of " + url.host + " was cut short";
            }
            return result;
        } else if (n == -2) {
            result.error = "the connection to " + url.host + " failed";
            return result;
        } else if (std::chrono::steady_clock::now() - last > idle) {
            result.error = fmt::format("nothing from {} for {} s", url.host, stream.idle_s);
            return result;
        }
    }
    return result;
}

}  // namespace

std::optional<std::string> httpHeader(std::string_view head, std::string_view name) {
    std::size_t at = head.find("\r\n");  // the status line left out
    while (at != std::string_view::npos) {
        const auto start = at + 2;
        auto stop        = head.find("\r\n", start);
        const auto line  = head.substr(start, stop == std::string_view::npos ? std::string_view::npos : stop - start);
        const auto colon = line.find(':');
        if (colon == name.size() && std::equal(name.begin(), name.end(), line.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            })) {
            auto value = line.substr(colon + 1);
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
            return std::string(value);
        }
        at = stop;
    }
    return std::nullopt;
}

HttpResult httpStream(const HttpRequest& request, const HttpStream& stream) {
    HttpRequest current = request;
    auto url            = parseUrl(current.url);
    for (int hop = 0;; ++hop) {
        if (!url) {
            HttpResult result;
            result.error = "not an http or https URL: " + current.url;
            return result;
        }
        std::string location;
        auto result = streamOnce(current, *url, stream, hop < stream.redirects, location);
        if (location.empty()) return result;
        const auto next = parseUrl(location);
        if (!next) {
            result.error = "redirected to " + location + ", which is no http or https URL";
            return result;
        }
        if (url->tls && !next->tls) {
            result.error = "redirected from https to http (" + location + "): refused";
            return result;
        }
        // The token goes to the site it was made for only.
        if (next->host != url->host || next->port != url->port) {
            current.token.clear();
            std::erase_if(current.headers, [](const auto& header) {
                return lower(header.first) == "authorization" || lower(header.first) == "cookie";
            });
        }
        if (result.status == 303 || ((result.status == 301 || result.status == 302) && current.method == "POST")) {
            current.method = "GET";
            current.body.clear();
            current.content_type.clear();
        }
        current.url = location;
        url         = next;
    }
}

HttpResult httpUpload(const HttpRequest& request, const HttpUpload& upload, bool* whole) {
    if (whole) *whole = false;
    HttpResult result;
    const auto url = parseUrl(request.url);
    if (!url) {
        result.error = "not an http or https URL: " + request.url;
        return result;
    }
    Connection connection;
    if (auto error = sendRequest(connection, request, *url, Body::Chunked, upload.cancel); !error.empty()) {
        result.error = std::move(error);
        return result;
    }
    const auto stopped = [&] { return upload.cancel && upload.cancel->load(std::memory_order_relaxed); };
    std::vector<unsigned char> buffer(std::size_t{1} << 14);
    std::string answer;       // what the server sent
    bool closed     = false;  // and whether it closed the connection
    const auto look = [&](uint32_t wait_ms) {
        const int n = connection.receiveSome(buffer.data(), buffer.size(), wait_ms);
        if (n > 0)
            answer.append(reinterpret_cast<const char*>(buffer.data()), static_cast<std::size_t>(n));
        else if (n == 0 || n == -2)
            closed = true;
        return n;
    };
    // The body, a chunk at a time, while the server says nothing.
    bool sent_whole = false;
    for (;;) {
        if (stopped()) {
            result.error = "cancelled";
            return result;
        }
        (void)look(1);  // 0 would wait for ever on a plain connection (mbedtls_net_recv_timeout)
        if (!answer.empty() || closed) break;
        const auto piece = upload.next ? upload.next(std::chrono::milliseconds(50)) : std::nullopt;
        if (!piece) {
            sent_whole = connection.send("0\r\n\r\n");
            if (whole) *whole = sent_whole;
            break;
        }
        if (piece->empty()) continue;
        if (!connection.send(fmt::format("{:x}\r\n", piece->size()) + *piece + "\r\n")) break;
    }
    // The answer: its head alone when it came before the body's end (what follows does not matter: the body was cut);
    // else up to the end of its length, of its last chunk, or of the connection.
    auto last = std::chrono::steady_clock::now();
    for (;;) {
        if (stopped()) {
            result.error = "cancelled";
            return result;
        }
        if (const auto end = answer.find("\r\n\r\n"); end != std::string::npos) {
            if (!sent_whole) break;
            const auto head_in = std::string_view(answer).substr(0, end);
            if (const auto length = httpHeader(head_in, "Content-Length")) {
                std::size_t n = 0;
                if (std::from_chars(length->data(), length->data() + length->size(), n).ec == std::errc() &&
                    answer.size() >= end + 4 + n)
                    break;
            } else if (const auto encoding = httpHeader(head_in, "Transfer-Encoding");
                       encoding && lower(*encoding).find("chunked") != std::string::npos) {
                ChunkDecoder chunks;
                (void)chunks.feed(std::string_view(answer).substr(end + 4), [](std::string_view) { return true; });
                if (chunks.done() || chunks.failed()) break;
            }
        }
        if (closed) break;
        const int n = look(250);
        if (n > 0) {
            last = std::chrono::steady_clock::now();
        } else if (n < 0 && !closed && std::chrono::steady_clock::now() - last > std::chrono::seconds(upload.idle_s)) {
            result.error = "no answer from " + url->host + " in time";
            return result;
        }
    }
    const auto headers_end = answer.find("\r\n\r\n");
    const auto space       = answer.find(' ');
    if (answer.rfind("HTTP/", 0) != 0 || headers_end == std::string::npos || space == std::string::npos ||
        std::from_chars(answer.data() + space + 1, answer.data() + answer.size(), result.status).ec != std::errc()) {
        result.status = 0;
        result.error =
            answer.empty() ? "the connection to " + url->host + " was closed" : "not an HTTP answer from " + url->host;
        return result;
    }
    result.head = answer.substr(0, headers_end);
    result.body = answer.substr(headers_end + 4);
    if (const auto encoding = httpHeader(result.head, "Transfer-Encoding");
        encoding && lower(*encoding).find("chunked") != std::string::npos)
        result.body = unchunk(result.body);
    return result;
}

bool ChunkDecoder::feed(std::string_view in, const std::function<bool(std::string_view)>& out) {
    while (!in.empty() && state_ != State::Done) {
        if (state_ == State::Data) {
            const auto take = static_cast<std::size_t>(std::min<uint64_t>(left_, in.size()));
            const auto data = in.substr(0, take);
            in.remove_prefix(take);
            left_ -= take;
            if (left_ == 0) state_ = State::DataEnd;
            if (!out(data)) return false;
            continue;
        }
        // A line: a chunk's size, the end of its data, or one of the trailer's.
        const auto eol = in.find('\n');
        line_.append(in.substr(0, eol));
        if (line_.size() > 4096) {
            failed_ = true;
            return false;
        }
        if (eol == std::string_view::npos) return true;
        in.remove_prefix(eol + 1);
        if (!line_.empty() && line_.back() == '\r') line_.pop_back();
        if (state_ == State::Size) {
            const std::string_view size = std::string_view(line_).substr(0, line_.find(';'));  // extensions left out
            uint64_t value              = 0;
            const auto [p, ec]          = std::from_chars(size.data(), size.data() + size.size(), value, 16);
            if (size.empty() || ec != std::errc() || p != size.data() + size.size()) {
                failed_ = true;
                return false;
            }
            left_  = value;
            state_ = value == 0 ? State::Trailer : State::Data;
        } else if (state_ == State::DataEnd) {
            if (!line_.empty()) {
                failed_ = true;
                return false;
            }
            state_ = State::Size;
        } else if (line_.empty()) {
            state_ = State::Done;  // the blank line that ends the trailer
        }
        line_.clear();
    }
    return true;
}

bool LineSplitter::feed(std::string_view data) {
    pending_.append(data);
    std::size_t start = 0;
    for (auto eol = pending_.find('\n'); eol != std::string::npos; eol = pending_.find('\n', start)) {
        std::string_view line(pending_.data() + start, eol - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        start = eol + 1;
        if (!line.empty() && !on_line_(line)) {
            pending_.erase(0, start);
            return false;
        }
    }
    pending_.erase(0, start);
    return true;
}

bool LineSplitter::flush() {
    std::string line;
    line.swap(pending_);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line.empty() || on_line_(line);
}

std::string urlHost(std::string_view url) {
    const auto u = parseUrl(url);
    return u ? u->host : std::string();
}

}  // namespace fcg
