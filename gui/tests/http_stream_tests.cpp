// HTTP read as it comes: chunks and lines, streams, redirections, downloads resumed, and the turns and pauses Lichess
// asks for, against servers on this machine whose answers the tests write.
#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <json.hpp>

#include <fmt/include/fmt/format.h>

#include "assistant_models.hpp"
#include "console.hpp"
#include "free_ports.hpp"
#include "https.hpp"
#include "net.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using namespace fcg::test;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

HttpRequest get(const std::string& url) {
    HttpRequest request;
    request.url    = url;
    request.method = "GET";
    return request;
}

}  // namespace

TEST_CASE("HTTP: chunks decoded however the bytes are cut, lines however they come") {
    const std::string body =
        "4\r\nWiki\r\n5;name=value\r\npedia\r\nE\r\n in\r\n\r\nchunks.\r\n0\r\nExpires: never\r\n\r\n";
    std::mt19937 random(7);
    for (int round = 0; round < 200; ++round) {
        CAPTURE(round);
        ChunkDecoder decoder;
        std::string out;
        for (std::size_t at = 0; at < body.size();) {
            const std::size_t n = round == 0 ? body.size() : round == 1 ? 1 : 1 + random() % 9;
            CHECK(decoder.feed(std::string_view(body).substr(at, n), [&](std::string_view data) {
                out += data;
                return true;
            }));
            at += n;
        }
        CHECK(decoder.done());
        CHECK(out == "Wikipedia in\r\n\r\nchunks.");
    }
    // No chunk size, or a chunk longer than it said: not chunks.
    for (const std::string bad : {"zz\r\nabc", "3\r\nabcX\r\n", "-1\r\n"}) {
        CAPTURE(bad);
        ChunkDecoder decoder;
        CHECK_FALSE(decoder.feed(bad, [](std::string_view) { return true; }));
        CHECK(decoder.failed());
    }
    // Stopped by its reader: not a fault of the chunks.
    ChunkDecoder stopped;
    CHECK_FALSE(stopped.feed(body, [](std::string_view) { return false; }));
    CHECK_FALSE(stopped.failed());

    std::vector<std::string> lines;
    LineSplitter splitter([&](std::string_view line) {
        lines.emplace_back(line);
        return line != "stop";
    });
    for (const char c : std::string("{\"a\":1}\r\n\n\n{\"b\":2}\n{\"c\""))
        CHECK(splitter.feed(std::string_view(&c, 1)));
    CHECK(lines == std::vector<std::string>{"{\"a\":1}", "{\"b\":2}"});
    CHECK(splitter.feed(":3}\r"));
    CHECK(splitter.flush());
    CHECK(lines.back() == "{\"c\":3}");
    CHECK_FALSE(splitter.feed("stop\nnot read\n"));
    CHECK(lines.back() == "stop");
}

TEST_CASE("HTTP: a stream's lines come as they are sent, chunked or up to the end of the connection") {
    for (const bool chunked : {true, false}) {
        CAPTURE(chunked);
        std::atomic<bool> got_first{false}, streamed{false};
        const auto port = fcg::test::freePort();
        ScriptedServer server(port, [&](const std::string&, net::TcpConnection& c) {
            const auto piece = [&](std::string_view data) {
                return c.sendAll(chunked ? fmt::format("{:x}\r\n{}\r\n", data.size(), data) : std::string(data));
            };
            c.sendAll(std::string("HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\n") +
                      (chunked ? "Transfer-Encoding: chunked\r\n" : "") + "\r\n");
            piece("{\"n\":1}\n");
            // The rest is sent once the first line has come: what a stream is.
            streamed = waitUntil([&] { return got_first.load(); });
            piece("\n");  // a keep-alive
            piece("{\"n\":2}\n{\"n\":");
            piece("3}\n");
            if (chunked) c.sendAll("0\r\n\r\n");
        });
        REQUIRE(server.ok());
        std::vector<std::string> lines;
        LineSplitter splitter([&](std::string_view line) {
            lines.emplace_back(line);
            got_first = true;
            return true;
        });
        HttpStream stream;
        int status     = 0;
        stream.on_head = [&](int s, std::string_view head) {
            status = s;
            CHECK(httpHeader(head, "content-type") == std::optional<std::string>("application/x-ndjson"));
            return true;
        };
        stream.on_data    = [&](std::string_view data) { return splitter.feed(data); };
        const auto result = httpStream(get(fmt::format("http://127.0.0.1:{}/api/stream/event", port)), stream);
        CHECK(result.error.empty());
        CHECK(status == 200);
        CHECK(result.status == 200);
        CHECK(streamed.load());
        CHECK(lines == std::vector<std::string>{"{\"n\":1}", "{\"n\":2}", "{\"n\":3}"});
    }
}

TEST_CASE("HTTP: a stream ended by its cancel flag, by its reader, or by a server that says nothing") {
    std::atomic<bool> release{false};
    const auto port = fcg::test::freePort();
    ScriptedServer server(port, [&](const std::string&, net::TcpConnection& c) {
        c.sendAll("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n");
        waitUntil([&] { return release.load(); });  // and nothing more
    });
    REQUIRE(server.ok());
    const auto request = get(fmt::format("http://127.0.0.1:{}/api/stream/event", port));

    // Cancelled once the first piece came: an idle stream would otherwise wait 30 s.
    std::atomic<bool> cancel{false};
    HttpStream stream;
    stream.cancel  = &cancel;
    stream.on_data = [&](std::string_view data) {
        CHECK(data == "hello");
        cancel = true;
        return true;
    };
    auto result = httpStream(request, stream);
    CHECK(result.error == "cancelled");
    CHECK(result.status == 200);

    // Stopped by its reader: no error.
    HttpStream reader;
    reader.on_data = [](std::string_view) { return false; };
    result         = httpStream(request, reader);
    CHECK(result.error.empty());
    CHECK(result.status == 200);

    // Nothing for longer than allowed.
    HttpStream patient;
    patient.idle_s   = 1;
    const auto start = std::chrono::steady_clock::now();
    result           = httpStream(request, patient);
    CHECK(result.error == "nothing from 127.0.0.1 for 1 s");
    CHECK(std::chrono::steady_clock::now() - start >= 1s);
    release = true;
}

TEST_CASE("HTTP: redirections followed, the token sent to its own site only, a loop given up") {
    const uint16_t a = fcg::test::freePorts(2), b = a + 1;
    std::mutex mutex;
    std::vector<std::string> seen;  // "port path authorization"
    const auto record = [&](uint16_t port, const std::string& head) {
        const auto path = requestPath(head);
        std::lock_guard lock(mutex);
        seen.push_back(fmt::format("{} {} {}", port, path, httpHeader(head, "Authorization").value_or("-")));
        return path;
    };
    ScriptedServer first(a, [&](const std::string& head, net::TcpConnection& c) {
        const auto path = record(a, head);
        if (path == "/start") {
            c.sendAll("HTTP/1.1 302 Found\r\nLocation: next?x=1\r\nContent-Length: 0\r\n\r\n");
        } else if (path == "/next?x=1") {
            c.sendAll(fmt::format("HTTP/1.1 307 Temporary Redirect\r\nlocation: http://127.0.0.1:{}/final\r\n\r\n", b));
        } else if (path == "/loop") {
            c.sendAll("HTTP/1.1 301 Moved Permanently\r\nLocation: /loop\r\nContent-Length: 0\r\n\r\n");
        }
    });
    ScriptedServer second(b, [&](const std::string& head, net::TcpConnection& c) {
        record(b, head);
        c.sendAll("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\ndone");
    });
    REQUIRE(first.ok());
    REQUIRE(second.ok());

    auto request  = get(fmt::format("http://127.0.0.1:{}/start", a));
    request.token = "secret";
    std::string body;
    HttpStream stream;
    stream.on_data = [&](std::string_view data) {
        body += data;
        return true;
    };
    auto result = httpStream(request, stream);
    CHECK(result.error.empty());
    CHECK(result.status == 200);
    CHECK(body == "done");
    CHECK(seen == std::vector<std::string>{fmt::format("{} /start Bearer secret", a),
                                           fmt::format("{} /next?x=1 Bearer secret", a),
                                           fmt::format("{} /final -", b)});

    // A loop: five redirections followed, the sixth given as the answer.
    seen.clear();
    result = httpStream(get(fmt::format("http://127.0.0.1:{}/loop", a)), HttpStream{});
    CHECK(result.error.empty());
    CHECK(result.status == 301);
    CHECK(seen.size() == 6);
}

TEST_CASE("HTTP: a download cut short or cancelled goes on from where it stopped, unless the file changed") {
    // Two versions of the file on the server.
    std::vector<std::string> contents(2, std::string(300000, '\0'));
    std::mt19937 random(3);
    for (auto& content : contents) {
        for (auto& ch : content) ch = static_cast<char>(random());
    }
    std::atomic<int> version{1};
    std::atomic<int> cut_at{-1};   // the next answer stops there
    std::atomic<int> hold_at{-1};  // the next answer waits there for `released`
    std::atomic<bool> released{false};
    std::mutex mutex;
    std::vector<std::string> asked;  // "Range|If-Range" of each request
    const auto port = fcg::test::freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto range = httpHeader(head, "Range"), if_range = httpHeader(head, "If-Range");
        {
            std::lock_guard lock(mutex);
            asked.push_back(range.value_or("-") + "|" + if_range.value_or("-"));
        }
        if (requestPath(head) == "/missing") {
            c.sendAll("HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nnot found");
            return;
        }
        const auto& content = contents[static_cast<std::size_t>(version - 1)];
        const auto etag     = fmt::format("\"v{}\"", version.load());
        std::size_t from    = 0;
        if (range && (!if_range || *if_range == etag)) from = std::stoul(range->substr(6));  // "bytes=N-"
        if (from >= content.size()) {
            c.sendAll(fmt::format(
                "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */{}\r\nContent-Length: 0\r\n\r\n",
                content.size()));
            return;
        }
        if (from > 0) {
            c.sendAll(
                fmt::format("HTTP/1.1 206 Partial Content\r\nETag: {}\r\nContent-Range: bytes "
                            "{}-{}/{}\r\nContent-Length: {}\r\n\r\n",
                            etag, from, content.size() - 1, content.size(), content.size() - from));
        } else {
            c.sendAll(fmt::format("HTTP/1.1 200 OK\r\nETag: {}\r\nContent-Length: {}\r\n\r\n", etag, content.size()));
        }
        const int cut        = cut_at.exchange(-1);
        const int hold       = hold_at.exchange(-1);
        const std::size_t to = cut >= 0 ? static_cast<std::size_t>(cut) : content.size();
        if (hold >= 0 && static_cast<std::size_t>(hold) > from) {
            c.sendAll(std::string_view(content).substr(from, static_cast<std::size_t>(hold) - from));
            waitUntil([&] { return released.load(); });
            from = static_cast<std::size_t>(hold);
        }
        if (to > from) c.sendAll(std::string_view(content).substr(from, to - from));
    });
    REQUIRE(server.ok());
    const auto dir = fs::temp_directory_path() / "downloads";
    fs::remove_all(dir);
    const auto file = dir / "lichess_db.pgn.zst";
    const auto part = fs::path(file).concat(".part");
    const auto url  = fmt::format("http://127.0.0.1:{}/lichess_db.pgn.zst", port);

    // Cut after 100 000 bytes: what came is kept.
    cut_at = 100000;
    DownloadProgress progress;
    auto got = httpDownload(url, file, &progress);
    CHECK_FALSE(got.ok);
    CHECK(got.error == "the answer of 127.0.0.1 was cut short");
    CHECK(got.bytes == 100000);
    CHECK(fs::file_size(part) == 100000);
    CHECK(progress.size == 300000);
    CHECK_FALSE(fs::exists(file));
    // The rest, asked from where it stopped, of the same file.
    got = httpDownload(url, file, &progress);
    CHECK(got.ok);
    CHECK(got.resumed);
    CHECK(got.bytes == 300000);
    CHECK(progress.bytes == 300000);
    CHECK(readFile(file) == contents[0]);
    CHECK_FALSE(fs::exists(part));
    CHECK(asked == std::vector<std::string>{"-|-", "bytes=100000-|\"v1\""});

    // Cut, then the file changes on the server: downloaded again from its start.
    fs::remove(file);
    cut_at = 50000;
    CHECK_FALSE(httpDownload(url, file).ok);
    version = 2;
    got     = httpDownload(url, file);
    CHECK(got.ok);
    CHECK_FALSE(got.resumed);
    CHECK(readFile(file) == contents[1]);

    // Cancelled while it comes: kept, then resumed.
    fs::remove(file);
    hold_at = 1000;
    std::atomic<bool> cancel{false};
    DownloadProgress watched;
    std::thread canceller([&] {
        waitUntil([&] { return watched.bytes >= 1000; });
        cancel = true;
    });
    got = httpDownload(url, file, &watched, &cancel);
    canceller.join();
    released = true;
    CHECK(got.error == "cancelled");
    CHECK(fs::file_size(part) == 1000);
    got = httpDownload(url, file);
    CHECK(got.ok);
    CHECK(got.resumed);
    CHECK(readFile(file) == contents[1]);

    // A part already whole (the rename did not happen): the server has nothing more, and the file is there.
    fs::rename(file, part);
    std::ofstream(fs::path(file).concat(".part.tag")) << "\"v2\"\n";
    got = httpDownload(url, file);
    CHECK(got.ok);
    CHECK(readFile(file) == contents[1]);

    // Checked against its SHA-256: a file that differs is not kept.
    const auto reference = dir / "reference";
    std::ofstream(reference, std::ios::binary) << contents[1];
    const auto sum = sha256File(reference);
    REQUIRE(sum.size() == 64);
    fs::remove(file);
    CHECK(httpDownload(url, file, nullptr, nullptr, sum).ok);
    auto wrong = sum;
    wrong[0]   = wrong[0] == '0' ? '1' : '0';
    fs::remove(file);
    got = httpDownload(url, file, nullptr, nullptr, wrong);
    CHECK_FALSE(got.ok);
    CHECK(got.error.find("SHA-256") != std::string::npos);
    CHECK_FALSE(fs::exists(file));
    CHECK_FALSE(fs::exists(part));

    // A file the server does not have.
    got = httpDownload(fmt::format("http://127.0.0.1:{}/missing", port), dir / "missing");
    CHECK_FALSE(got.ok);
    CHECK(got.error.find("404") != std::string::npos);
    CHECK_FALSE(fs::exists(dir / "missing.part"));
}

TEST_CASE("HTTP: one request at a time to a host, and a pause after it answered 429") {
    auto& gate       = HostGate::instance();
    const auto pause = 500ms;
    gate.setPause(pause);
    std::mutex mutex;
    std::chrono::steady_clock::time_point refused_at{}, next_at{};
    // The requests being answered, counted up to their answer's last byte: a client goes on once it has it, without
    // waiting for the server to close the connection.
    std::atomic<int> answering{0}, most{0};
    const auto port = fcg::test::freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const int now = ++answering;
        for (int seen = most.load(); now > seen && !most.compare_exchange_weak(seen, now);) {
        }
        const auto path = requestPath(head);
        if (path == "/busy") {
            {
                std::lock_guard lock(mutex);
                refused_at = std::chrono::steady_clock::now();
            }
            --answering;
            c.sendAll("HTTP/1.1 429 Too Many Requests\r\nContent-Length: 0\r\n\r\n");
            return;
        }
        if (path == "/after") {
            std::lock_guard lock(mutex);
            next_at = std::chrono::steady_clock::now();
        }
        std::this_thread::sleep_for(20ms);  // room for another request to come meanwhile, were one let through
        --answering;
        c.sendAll("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
    });
    REQUIRE(server.ok());

    std::atomic<int> answered{0};
    std::vector<std::thread> clients;
    for (int t = 0; t < 4; ++t) {
        clients.emplace_back([&] {
            for (int i = 0; i < 3; ++i) {
                if (politeRequest(get(fmt::format("http://127.0.0.1:{}/games", port))).status == 200) ++answered;
            }
        });
    }
    for (auto& t : clients) t.join();
    CHECK(answered == 12);
    CHECK(most == 1);

    // A 429: the host's next request waits for the pause to end.
    CHECK(politeRequest(get(fmt::format("http://127.0.0.1:{}/busy", port))).status == 429);
    CHECK(gate.pausedFor("127.0.0.1") > 0ms);
    CHECK(politeRequest(get(fmt::format("http://127.0.0.1:{}/after", port))).status == 200);
    {
        std::lock_guard lock(mutex);
        CHECK(next_at - refused_at >= pause);
    }
    CHECK(gate.pausedFor("127.0.0.1") == 0ms);

    // In a pause, a request or a stream cancelled goes nowhere.
    gate.setPause(60s);
    gate.tooMany("paused.invalid");
    std::atomic<bool> cancel{true};
    CHECK(politeRequest(get("http://paused.invalid/api/account"), &cancel).error == "cancelled");
    HttpStream stream;
    stream.cancel = &cancel;
    CHECK(politeStream(get("http://paused.invalid/api/stream/event"), stream).error == "cancelled");
    CHECK(gate.pausedFor("paused.invalid") > 59s);
}

TEST_CASE("HTTP: one head for every way to ask, no Content-Length where no body goes, each request in the log") {
    std::mutex mutex;
    std::map<std::string, std::string> heads;  // path -> the head received
    const auto port = fcg::test::freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto end = head.find("\r\n\r\n");
        // The body read whole before the answer (Windows: scripted_server.hpp).
        if (!httpHeader(std::string_view(head).substr(0, end), "Transfer-Encoding")) {
            (void)requestBody(head, c);
        } else if (head.find("0\r\n\r\n", end + 4) == std::string::npos) {
            (void)c.readUntil("0\r\n\r\n", 1 << 16, 5000);
        }
        {
            std::lock_guard lock(mutex);
            heads[requestPath(head)] = head.substr(0, end + 2);
        }
        c.sendAll(head.rfind("HEAD ", 0) == 0 ? "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n"
                                              : "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
    });
    REQUIRE(server.ok());
    const auto base = fmt::format("http://127.0.0.1:{}", port);
    const auto ask  = [&](const char* method, const std::string& path, std::string body = {}) {
        auto request   = get(base + path);
        request.method = method;
        request.body   = std::move(body);
        return request;
    };
    const auto dir = fs::temp_directory_path() / "heads";
    fs::remove_all(dir);
    const auto from = Console::instance().added();

    CHECK(httpRequest(ask("GET", "/request-get")).body == "ok");
    const auto head = httpRequest(ask("HEAD", "/request-head"));
    CHECK(head.status == 200);
    CHECK(head.body.empty());
    CHECK(httpRequest(ask("DELETE", "/request-delete")).status == 200);
    CHECK(httpRequest(ask("POST", "/request-post-empty")).status == 200);
    auto form         = ask("POST", "/request-post", "a=1");
    form.token        = "secret";
    form.content_type = "application/x-www-form-urlencoded";
    CHECK(httpRequest(form).body == "ok");
    CHECK(politeRequest(ask("GET", "/polite-get")).status == 200);
    CHECK(httpStream(ask("GET", "/stream-get"), HttpStream{}).status == 200);
    CHECK(httpStream(ask("HEAD", "/stream-head"), HttpStream{}).status == 200);
    CHECK(httpDownload(base + "/download", dir / "download").ok);
    HttpUpload upload;
    upload.next = [](std::chrono::milliseconds) -> std::optional<std::string> { return std::nullopt; };
    CHECK(httpUpload(ask("POST", "/upload"), upload).status == 200);
    auto quiet   = ask("POST", "/quiet", "x");
    quiet.logged = false;
    CHECK(httpRequest(quiet).status == 200);

    std::lock_guard lock(mutex);
    REQUIRE(heads.size() == 11);
    const auto length = [&](const char* path) { return httpHeader(heads[path], "Content-Length"); };
    // No body, and a method that sends none: no Content-Length (RFC 9110, 8.6), whatever the way it was asked.
    for (const char* path : {"/request-get", "/request-head", "/request-delete", "/polite-get", "/stream-get",
                             "/stream-head", "/download"}) {
        CAPTURE(path);
        CHECK_FALSE(length(path));
    }
    CHECK(length("/request-post-empty") == std::optional<std::string>("0"));
    CHECK(length("/request-post") == std::optional<std::string>("3"));
    CHECK_FALSE(length("/upload"));
    CHECK(httpHeader(heads["/upload"], "Transfer-Encoding") == std::optional<std::string>("chunked"));
    CHECK(httpHeader(heads["/request-post"], "Authorization") == std::optional<std::string>("Bearer secret"));
    // The same head otherwise: its request line, the host, the program's name, the connection closed after.
    for (const auto& [path, received] : heads) {
        CAPTURE(path);
        CHECK(received.find(" " + path + " HTTP/1.1\r\n") != std::string::npos);
        CHECK(httpHeader(received, "Host") == std::optional<std::string>("127.0.0.1"));
        CHECK(httpHeader(received, "User-Agent").value_or("").rfind("RookStudio/", 0) == 0);
        CHECK(httpHeader(received, "Connection") == std::optional<std::string>("close"));
    }

    // Each request in the log as it leaves (R16), whatever the way: whether the token went, never the token; a
    // request not `logged` left out.
    const auto lines  = Console::instance().since(from);
    const auto logged = [&](const std::string& line) { return std::count(lines.begin(), lines.end(), line); };
    for (const char* line :
         {"GET /request-get", "HEAD /request-head", "DELETE /request-delete", "POST /request-post-empty",
          "GET /polite-get", "GET /stream-get", "HEAD /stream-head", "GET /download", "POST /upload"}) {
        const std::string text(line);
        const auto space = text.find(' ');
        CHECK_MESSAGE(logged(text.substr(0, space + 1) + base + text.substr(space + 1)) == 1, line);
    }
    CHECK(logged("POST " + base + "/request-post (with the account's token)") == 1);
    for (const auto& line : lines) {
        CHECK(line.find("secret") == std::string::npos);
        CHECK(line.find("/quiet") == std::string::npos);
    }
}

TEST_CASE("HTTP: a request cancelled ends at once, its connection closed, long before its limit") {
    std::atomic<bool> asked{false}, closed{false};
    const auto port = fcg::test::freePort();
    ScriptedServer server(port, [&](const std::string&, net::TcpConnection& c) {
        asked = true;
        // Never an answer: the read ends when the client closes the connection (or when the server stops).
        (void)c.readUntil("\x01never", 1, 60000);
        closed = true;
    });
    REQUIRE(server.ok());
    for (const bool polite : {true, false}) {
        CAPTURE(polite);
        asked  = false;
        closed = false;
        std::atomic<bool> cancel{false};
        // Cancelled once the server has the request: what an impatient user or a page left does.
        std::thread canceller([&] {
            if (waitUntil([&] { return asked.load(); })) cancel = true;
        });
        const auto request = get(fmt::format("http://127.0.0.1:{}/api/account", port));
        REQUIRE(request.timeout_s == 20);
        const auto start  = std::chrono::steady_clock::now();
        const auto result = polite ? politeRequest(request, &cancel) : httpRequest(request, &cancel);
        const auto took   = std::chrono::steady_clock::now() - start;
        canceller.join();
        CHECK(result.error == "cancelled");
        CHECK(result.status == 0);
        CHECK(took < 10s);
        CHECK(waitUntil([&] { return closed.load(); }));
    }
}

TEST_CASE("HTTP: a download takes no turn of its host, waits for its pause, and a 429 pauses it") {
    auto& gate = HostGate::instance();
    gate.setPause(300ms);
    std::atomic<bool> holding{false}, release{false};
    const auto port = fcg::test::freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto path = requestPath(head);
        if (path == "/busy.zip") {
            c.sendAll("HTTP/1.1 429 Too Many Requests\r\nContent-Length: 0\r\n\r\n");
        } else if (path == "/big.zip") {
            c.sendAll("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nab");
            holding = true;
            waitUntil([&] { return release.load(); });
            c.sendAll("cd");
        } else {
            c.sendAll("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
        }
    });
    REQUIRE(server.ok());
    const auto base = fmt::format("http://127.0.0.1:{}", port);
    const auto dir  = fs::temp_directory_path() / "polite-downloads";
    fs::remove_all(dir);

    // A download under way: the host's short requests go meanwhile, in their turns.
    Download got;
    std::thread downloading([&] { got = politeDownload(base + "/big.zip", dir / "big.zip"); });
    REQUIRE(waitUntil([&] { return holding.load(); }));
    std::atomic<bool> give_up{false};  // were the turn held, the request would wait for it: given up then
    std::thread watchdog([&] {
        (void)waitUntil([&] { return give_up.load(); });
        give_up = true;
    });
    CHECK(politeRequest(get(base + "/games"), &give_up).status == 200);
    give_up = true;
    watchdog.join();
    release = true;
    downloading.join();
    CHECK(got.ok);
    CHECK(got.status == 200);
    CHECK(readFile(dir / "big.zip") == "abcd");

    // A 429: the host pauses, and the next download waits for the pause to end.
    const auto busy = politeDownload(base + "/busy.zip", dir / "busy.zip");
    CHECK_FALSE(busy.ok);
    CHECK(busy.status == 429);
    CHECK(gate.pausedFor("127.0.0.1") > 0ms);
    CHECK(politeDownload(base + "/small.zip", dir / "small.zip").ok);
    CHECK(gate.pausedFor("127.0.0.1") == 0ms);

    // In a pause, a download cancelled goes nowhere.
    gate.setPause(60s);
    gate.tooMany("paused.invalid");
    std::atomic<bool> cancel{true};
    CHECK(politeDownload("http://paused.invalid/big.zip", dir / "paused.zip", nullptr, &cancel).error == "cancelled");
    CHECK(gate.pausedFor("paused.invalid") > 59s);
}

TEST_CASE("HTTP: a file's SHA-256") {
    const auto file = fs::temp_directory_path() / "abc.txt";
    std::ofstream(file, std::ios::binary) << "abc";
    CHECK(sha256File(file) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256File(fs::temp_directory_path() / "nothing-here").empty());
}

TEST_CASE("HTTPS: games streamed from Lichess, a file of its database downloaded (FCG_NET_TESTS=1 only)") {
    const char* wanted = std::getenv("FCG_NET_TESTS");
    if (!wanted || std::string(wanted) != "1") {
        MESSAGE("needs the internet: FCG_NET_TESTS=1, skipped");
        return;
    }
    // Three games of a public account, one NDJSON line each.
    auto request = get("https://lichess.org/api/games/user/penguingim1?max=3");
    request.headers.emplace_back("Accept", "application/x-ndjson");
    std::vector<std::string> lines;
    LineSplitter splitter([&](std::string_view line) {
        lines.emplace_back(line);
        return true;
    });
    HttpStream stream;
    stream.on_data    = [&](std::string_view data) { return splitter.feed(data); };
    const auto result = politeStream(request, stream);
    CHECK(splitter.flush());
    CHECK_MESSAGE(result.error.empty(), result.error);
    CHECK(result.status == 200);
    REQUIRE(lines.size() == 3);
    for (const auto& line : lines) CHECK(nlohmann::json::parse(line).contains("id"));

    // The checksums of the database's monthly files.
    const auto file = fs::temp_directory_path() / "lichess" / "sha256sums.txt";
    DownloadProgress progress;
    const auto got = httpDownload("https://database.lichess.org/standard/sha256sums.txt", file, &progress);
    CHECK_MESSAGE(got.ok, got.error);
    CHECK(progress.size == progress.bytes);
    CHECK(readFile(file).find("lichess_db_standard_rated_2013-01.pgn.zst") != std::string::npos);
}

TEST_CASE("assistant models: the recommended one is pinned to what was published") {
    const auto models = assistantModels();
    REQUIRE_FALSE(models.empty());
    const auto& m = models.front();
    CHECK(m.id == "commentary-v11");
    // A published model is never changed: its version is in its name, and its hash and size are those of the file.
    CHECK(m.file == "commentary-v11-Q4_K_M.gguf");
    CHECK(m.url.starts_with("https://huggingface.co/"));
    CHECK(m.url.ends_with(std::string("/") + std::string(m.file)));
    CHECK(m.sha256.size() == 64);
    CHECK(m.size == 2708804128);
    CHECK(m.license == "Apache-2.0");
    const auto profile = fs::temp_directory_path() / "profile";
    CHECK(modelFile(profile, m) == profile / "models" / "commentary-v11-Q4_K_M.gguf");
}

TEST_CASE("assistant models: a model comes into the profile, checked against its hash") {
    // A small file stands for the model, served from here.
    std::string content(70000, '\0');
    std::mt19937 random(7);
    for (auto& ch : content) ch = static_cast<char>(random() & 0xFF);
    const auto profile = fs::temp_directory_path() / "model-profile";
    fs::remove_all(profile);
    fs::create_directories(profile);
    {
        std::ofstream(profile / "reference", std::ios::binary) << content;
    }
    const auto hash = sha256File(profile / "reference");
    const auto port = fcg::test::freePort();
    ScriptedServer server(port, [&](const std::string&, net::TcpConnection& c) {
        c.sendAll(fmt::format("HTTP/1.1 200 OK\r\nContent-Length: {}\r\n\r\n", content.size()));
        c.sendAll(content);
    });
    REQUIRE(server.ok());
    const auto url = fmt::format("http://127.0.0.1:{}/small.gguf", port);
    const AssistantModel small{"small", "small.gguf", "unused", hash, content.size(), "MIT", "English"};
    CHECK_FALSE(modelInstalled(profile, small));
    DownloadProgress progress;
    const auto got = downloadModel(profile, small, &progress, nullptr, url);
    REQUIRE_MESSAGE(got.ok, got.error);
    CHECK(modelInstalled(profile, small));
    CHECK(readFile(modelFile(profile, small)) == content);
    CHECK(progress.bytes == content.size());
    // A file that is not the one published is removed.
    const std::string zeros(64, '0');  // the model holds a view of it
    const AssistantModel other{"other", "other.gguf", "unused", zeros, content.size(), "MIT", "English"};
    CHECK_FALSE(downloadModel(profile, other, nullptr, nullptr, url).ok);
    CHECK_FALSE(fs::exists(modelFile(profile, other)));
    CHECK_FALSE(modelInstalled(profile, other));
}
