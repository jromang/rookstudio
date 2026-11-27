// One of the user's engines offered to lichess.org's analysis board (#96): the registration and its secret kept, the
// long poll's 204 and a job answered (the engine set in UCI_Chess960 and UCI_AnalyseMode, its info lines and its
// bestmove streamed, 1 to 5 lines), a cut stopping the engine, the guards (a tournament's threads, the user's live
// game, a correspondence game's positions answered empty, the bot's threads first), stop, resume and withdraw, the
// service resumed at the start, the secret refused (401), a changed API and offline: against a Lichess of this machine,
// the mock engine playing the user's engine. The names are invented.
#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <chess.hpp>
#include <json.hpp>

#include "console.hpp"
#include "free_ports.hpp"
#include "https.hpp"
#include "lichess_api.hpp"
#include "lichess_engine.hpp"
#include "mock_engines.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using namespace fcg::lichess;
using namespace fcg::test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

constexpr const char* TOKEN = "lip_SampleToken0000";

void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value)
        setenv(name, value, 1);
    else
        unsetenv(name);
#endif
}

struct ScopedEnv {
    const char* name;
    ScopedEnv(const char* n, const char* value) : name(n) { setEnv(n, value); }
    ~ScopedEnv() { setEnv(name, nullptr); }
};

EngineEntry mover(const std::string& name = "Sample Engine 1.0") {
    EngineEntry e;
    e.name     = name;
    e.cmd      = randomMover();
    e.protocol = "uci";
    return e;
}

std::string reply(int status, const std::string& body, const char* type = "application/json") {
    return fmt::format("HTTP/1.1 {} X\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", status,
                       type, body.size(), body);
}

// A request's body, of the length its head says.
std::string bodyOf(const std::string& head, net::TcpConnection& c) {
    const auto length = std::stoul(httpHeader(head, "Content-Length").value_or("0"));
    auto body         = head.substr(head.find("\r\n\r\n") + 4);
    while (body.size() < length) {
        const auto more = c.readUntil("\x01never", length - body.size(), 2000);
        if (more.empty()) break;
        body += more;
    }
    return body;
}

std::string jobJson(const std::string& id, const std::string& engine, const nlohmann::json& work) {
    return nlohmann::json{{"id", id},
                          {"work", work},
                          {"engine",
                           {{"id", engine},
                            {"name", "Sample Engine 1.0 (RookStudio)"},
                            {"clientSecret", "ees_SampleClient01"},
                            {"userId", "rs_sample"},
                            {"maxThreads", 8},
                            {"maxHash", 1024},
                            {"variants", {"chess"}},
                            {"providerData", nullptr}}}}
        .dump();
}

nlohmann::json work(const std::string& fen, std::vector<std::string> moves, int multipv = 1) {
    return {{"sessionId", "s1"},        {"threads", 8},       {"hash", 32},
            {"multiPv", multipv},       {"variant", "chess"}, {"initialFen", fen},
            {"moves", std::move(moves)}};
}

const std::string START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// Lichess, on this machine: the registrations (with the token) and the work host under /engine (with the provider's
// secret alone). Each long poll takes the next of `polls` (a status, or a job's JSON), else waits a little and answers
// 204; each answer's lines are read as they come, the connection closed after `cut_after` of them when set.
struct FakeLichess {
    struct Poll {
        int status = 204;
        std::string body;
    };
    struct Answer {
        std::string id;
        std::vector<std::string> lines;
        bool whole = false;  // its last chunk came
    };

    // On a port the system gives, or on `port`.
    FakeLichess() : FakeLichess(0) {}
    explicit FakeLichess(uint16_t port)
        : server(port, [this](const std::string& head, net::TcpConnection& c) { serve(head, c); }) {
        REQUIRE(server.ok());
        const auto base = fmt::format("http://127.0.0.1:{}", server.port());
        servers()       = Servers{base, base + "/tb", base + "/explorer", base + "/engine"};
    }
    ~FakeLichess() { servers() = Servers{}; }

    void serve(const std::string& head, net::TcpConnection& c) {
        const auto line = head.substr(0, head.find("\r\n"));
        const auto path = requestPath(head);
        const auto auth = httpHeader(head, "Authorization").value_or("");
        if (path == "/engine/api/external-engine/work") {
            const auto body = bodyOf(head, c);
            Poll p;
            {
                std::unique_lock lock(mutex);
                poll_bodies.push_back(body);
                poll_auth.push_back(auth);
                poll_times.push_back(std::chrono::steady_clock::now());
                if (!polls.empty()) {
                    p = polls.front();
                    polls.pop_front();
                } else {
                    // Nothing to do: held a little, as a long poll is, then 204.
                    lock.unlock();
                    std::this_thread::sleep_for(30ms);
                }
            }
            c.sendAll(p.status == 204 ? "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n"
                                      : reply(p.status, p.body));
            return;
        }
        if (path.rfind("/engine/api/external-engine/work/", 0) == 0) {
            Answer a;
            a.id = path.substr(std::string("/engine/api/external-engine/work/").size());
            readChunks(head, c, a);
            const bool whole = a.whole;
            {
                // Kept before the answer: the service, which ends once answered, finds it there.
                std::lock_guard lock(mutex);
                answers.push_back(std::move(a));
            }
            if (whole) c.sendAll(reply(200, "", "text/plain"));
            return;
        }
        // The registrations, their body read whole before any answer, a refusal's too (Windows: scripted_server.hpp).
        const auto body = bodyOf(head, c);
        std::lock_guard lock(mutex);
        requests.push_back(line);
        if (auth != std::string("Bearer ") + TOKEN) {
            c.sendAll(reply(401, R"({"error":"No such token"})"));
            return;
        }
        if (line.rfind("POST /api/external-engine ", 0) == 0) {
            registrations.push_back(body);
            auto j = nlohmann::json::parse(registrations.back());
            c.sendAll(reply(200, nlohmann::json{{"id", "eei_Sample000001"},
                                                {"name", j["name"]},
                                                {"clientSecret", "ees_SampleClient01"},
                                                {"userId", "rs_sample"},
                                                {"maxThreads", j["maxThreads"]},
                                                {"maxHash", j["maxHash"]},
                                                {"variants", j["variants"]},
                                                {"providerData", nullptr}}
                                     .dump()));
        } else if (line.rfind("DELETE /api/external-engine/eei_Sample000001 ", 0) == 0) {
            withdrawn = true;
            c.sendAll(reply(200, R"({"ok":true})"));
        } else if (line.rfind("GET /api/external-engine/eei_Sample000001 ", 0) == 0) {
            c.sendAll(withdrawn ? reply(404, R"({"error":"Not found"})")
                                : reply(200, nlohmann::json{{"id", "eei_Sample000001"},
                                                            {"name", "Sample Engine 1.0 (RookStudio)"},
                                                            {"clientSecret", "ees_SampleClient01"},
                                                            {"userId", "rs_sample"},
                                                            {"maxThreads", 8},
                                                            {"maxHash", 1024},
                                                            {"variants", {"chess"}}}
                                                 .dump()));
        } else {
            c.sendAll(reply(404, R"({"error":"Not found"})"));
        }
    }

    // The chunked body, line by line as it comes; the connection closed after `cut_after` lines when set.
    void readChunks(const std::string& head, net::TcpConnection& c, Answer& a) {
        std::string data    = head.substr(head.find("\r\n\r\n") + 4), text;
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        for (;;) {
            // Whole chunks out of `data`.
            for (;;) {
                const auto eol = data.find("\r\n");
                if (eol == std::string::npos) break;
                const auto size = std::stoul(data.substr(0, eol), nullptr, 16);
                if (size == 0) {
                    a.whole = true;
                    break;
                }
                if (data.size() < eol + 2 + size + 2) break;
                text += data.substr(eol + 2, size);
                data.erase(0, eol + 2 + size + 2);
            }
            for (auto nl = text.find('\n'); nl != std::string::npos; nl = text.find('\n')) {
                std::lock_guard lock(mutex);
                a.lines.push_back(text.substr(0, nl));
                text.erase(0, nl + 1);
                live_lines = static_cast<int>(a.lines.size());
                if (cut_after > 0 && static_cast<int>(a.lines.size()) >= cut_after) return;  // the requester went
            }
            if (a.whole || std::chrono::steady_clock::now() > deadline) return;
            const auto more = c.readUntil("\x01never", 4096, 200);
            data += more;
        }
    }

    void push(int status, std::string body = {}) {
        std::lock_guard lock(mutex);
        polls.push_back({status, std::move(body)});
    }
    std::size_t pollCount() {
        std::lock_guard lock(mutex);
        return poll_bodies.size();
    }
    std::vector<Answer> answered() {
        std::lock_guard lock(mutex);
        return answers;
    }

    std::mutex mutex;
    std::deque<Poll> polls;
    std::vector<std::string> poll_bodies, poll_auth, requests, registrations;
    std::vector<std::chrono::steady_clock::time_point> poll_times;
    std::vector<Answer> answers;
    std::atomic<int> cut_after{0};
    std::atomic<int> live_lines{0};
    std::atomic<bool> withdrawn{false};
    ScriptedServer server;
};

EngineService::Setup setupFor(const FakeLichess&, std::vector<std::string>* sent, std::mutex* sent_mutex,
                              const EngineEntry& engine = mover()) {
    EngineService::Setup s;
    s.engine      = engine;
    s.secret      = "SampleSecret0123456789abcdefABCDEF0123456789xyzw";
    s.engine_id   = "eei_Sample000001";
    s.max_threads = 8;
    s.max_hash    = 1024;
    s.host        = servers().engine;
    s.retry       = {50ms};
    s.stop_grace  = 3s;
    if (sent)
        s.sent = [sent, sent_mutex](const std::string& line) {
            std::lock_guard lock(*sent_mutex);
            sent->push_back(line);
        };
    return s;
}

bool sentHas(std::vector<std::string>& sent, std::mutex& m, const std::string& line) {
    std::lock_guard lock(m);
    return std::find(sent.begin(), sent.end(), line) != sent.end();
}

bool sentStarting(std::vector<std::string>& sent, std::mutex& m, const std::string& start) {
    std::lock_guard lock(m);
    return std::any_of(sent.begin(), sent.end(), [&](const std::string& s) { return s.rfind(start, 0) == 0; });
}

std::vector<std::string> words(const std::string& line) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at < line.size()) {
        const auto end = std::min(line.find(' ', at), line.size());
        if (end > at) out.push_back(line.substr(at, end - at));
        at = end + 1;
    }
    return out;
}

}  // namespace

TEST_CASE("external engine: a registration's body, its limits and a secret drawn here") {
    EngineRegistration r;
    r.name            = "Sample Engine 1.0 (RookStudio)";
    r.max_threads     = 8;
    r.max_hash        = 1024;
    r.provider_secret = newProviderSecret();
    CHECK(registrationProblem(r).empty());
    const auto j = nlohmann::json::parse(registrationJson(r));
    CHECK(j["name"] == r.name);
    CHECK(j["maxThreads"] == 8);
    CHECK(j["maxHash"] == 1024);
    CHECK(j["variants"] == nlohmann::json::array({"chess"}));
    CHECK(j["providerSecret"] == r.provider_secret);
    CHECK_FALSE(j.contains("providerData"));

    // The secret: 48 letters and digits, another one each time.
    CHECK(r.provider_secret.size() == 48);
    CHECK(std::all_of(r.provider_secret.begin(), r.provider_secret.end(),
                      [](unsigned char c) { return std::isalnum(c) != 0; }));
    CHECK(newProviderSecret() != r.provider_secret);

    // Lichess's bounds: the name in characters (3 to 200), the limits, the secret's length.
    auto bad = r;
    bad.name = "Ab";
    CHECK_FALSE(registrationProblem(bad).empty());
    bad.name = "Éna";  // three characters, four bytes
    CHECK(nameLength(bad.name) == 3);
    CHECK(registrationProblem(bad).empty());
    bad.name = std::string(201, 'x');
    CHECK_FALSE(registrationProblem(bad).empty());
    bad             = r;
    bad.max_threads = 0;
    CHECK_FALSE(registrationProblem(bad).empty());
    bad          = r;
    bad.max_hash = 1048577;
    CHECK_FALSE(registrationProblem(bad).empty());
    bad                 = r;
    bad.provider_secret = "short";
    CHECK_FALSE(registrationProblem(bad).empty());
    bad.provider_secret = std::string(1025, 'a');
    CHECK_FALSE(registrationProblem(bad).empty());
}

TEST_CASE("external engine: registered with the token, its secret kept readable by its owner alone, never logged") {
    FakeLichess lichess;
    EngineRegistration r;
    r.name            = "Sample Engine 1.0 (RookStudio)";
    r.max_threads     = 6;
    r.max_hash        = 512;
    r.provider_secret = newProviderSecret();
    const auto logged = Console::instance().added();
    const auto got    = registerEngine(TOKEN, r);
    REQUIRE_MESSAGE(got.value, describe(got));
    CHECK(got.value->id == "eei_Sample000001");
    CHECK(got.value->user_id == "rs_sample");
    CHECK(got.value->max_threads == 6);
    REQUIRE(lichess.registrations.size() == 1);
    const auto sent = nlohmann::json::parse(lichess.registrations[0]);
    CHECK(sent["providerSecret"] == r.provider_secret);
    CHECK(sent["variants"] == nlohmann::json::array({"chess"}));
    // One request, and the secret in no line of the log.
    CHECK(lichess.requests.size() == 1);
    for (const auto& line : Console::instance().since(logged)) CHECK(line.find(r.provider_secret) == std::string::npos);

    // A token that is not the user's: Lichess's refusal, nothing registered.
    const auto refused = registerEngine("lip_Other", r);
    CHECK_FALSE(refused.value);
    CHECK(refused.status == 401);
    CHECK(refused.net == NetError::Unauthorized);
    CHECK(describe(refused).find(r.provider_secret) == std::string::npos);
    CHECK(refused.said.find(r.provider_secret) == std::string::npos);

    // What the profile keeps, readable and writable by its owner alone.
    const auto profile = fs::temp_directory_path() / "external-engine-profile";
    fs::remove_all(profile);
    fs::create_directories(profile);
    OfferedEngine o;
    o.engine  = "Sample Engine 1.0";
    o.name    = r.name;
    o.threads = 6;
    o.hash_mb = 512;
    o.id      = got.value->id;
    o.user    = got.value->user_id;
    o.secret  = r.provider_secret;
    o.serving = true;
    std::string error;
    REQUIRE_MESSAGE(saveOffers(profile, {o}, &error), error);
    const auto back = loadOffers(profile);
    REQUIRE(back.size() == 1);
    CHECK(back[0] == o);
#ifndef _WIN32
    const auto perms = fs::status(profile / ENGINES_FILE).permissions();
    CHECK((perms & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
#endif
    fs::remove_all(profile);
}

TEST_CASE("external engine: a job read as the spec has it, refused when the API changed") {
    std::string why;
    auto first     = work(START, {"e2e4", "e7e5"}, 3);
    first["depth"] = 12;
    auto w         = readEngineWork(jobJson("job0001", "eei_Sample000001", first), &why);
    REQUIRE_MESSAGE(w, why);
    CHECK(w->id == "job0001");
    CHECK(w->engine_id == "eei_Sample000001");
    CHECK(w->session == "s1");
    CHECK(w->threads == 8);
    CHECK(w->hash == 32);
    CHECK(w->multipv == 3);
    CHECK(w->moves == std::vector<std::string>{"e2e4", "e7e5"});
    CHECK(w->depth == 12);
    CHECK(w->movetime == 0);
    CHECK(w->nodes == 0);
    CHECK(workPosition(*w) == "position fen " + START + " moves e2e4 e7e5");
    // With each limit.
    auto with        = work(START, {});
    with["movetime"] = 2500;
    w                = readEngineWork(jobJson("j", "e", with), &why);
    REQUIRE(w);
    CHECK(workGo(*w) == "go movetime 2500");
    CHECK(workPosition(*w) == "position fen " + START);
    with.erase("movetime");
    with["depth"] = 18;
    w             = readEngineWork(jobJson("j", "e", with), &why);
    REQUIRE(w);
    CHECK(workGo(*w) == "go depth 18");
    with.erase("depth");
    with["nodes"] = 1000000;
    w             = readEngineWork(jobJson("j", "e", with), &why);
    REQUIRE(w);
    CHECK(workGo(*w) == "go nodes 1000000");

    // What this program does not read: the API changed.
    CHECK_FALSE(readEngineWork(jobJson("j", "e", work(START, {})), &why));  // no limit
    CHECK(why.find("limit") != std::string::npos);
    auto six     = work(START, {}, 6);
    six["depth"] = 10;
    CHECK_FALSE(readEngineWork(jobJson("j", "e", six), &why));  // 6 lines
    auto no_fen     = work(START, {});
    no_fen["depth"] = 10;
    no_fen.erase("initialFen");
    CHECK_FALSE(readEngineWork(jobJson("j", "e", no_fen), &why));
    CHECK_FALSE(readEngineWork(R"({"id":"j","work":{}})", &why));
    CHECK_FALSE(readEngineWork("not json", &why));

    // The position after its moves, castling as the king taking its rook (UCI_Chess960) or the usual way.
    auto castle     = work("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1", {"e1h1", "e8a8"});
    castle["depth"] = 5;
    w               = readEngineWork(jobJson("j", "e", castle), &why);
    REQUIRE(w);
    CHECK(workFen(*w).rfind("2kr3r/pppppppp/8/8/8/8/PPPPPPPP/R4RK1 w", 0) == 0);
    w->moves = {"e1g1", "e8c8"};
    CHECK(workFen(*w).rfind("2kr3r/pppppppp/8/8/8/8/PPPPPPPP/R4RK1 w", 0) == 0);
    w->moves = {"e1e5"};
    CHECK(workFen(*w).empty());  // not a move there
}

TEST_CASE("external engine: the lines Lichess takes from the engine") {
    // A whole line, as it is.
    CHECK(lineForLichess("info depth 20 seldepth 30 multipv 1 score cp 47 nodes 1494341 nps 1088376 time 1373 pv d2d4 "
                         "d7d5",
                         10) ==
          "info depth 20 seldepth 30 multipv 1 score cp 47 nodes 1494341 nps 1088376 time 1373 pv "
          "d2d4 d7d5");
    // What it lacks of what Lichess asks for: multipv 1, the time since the go, nodes.
    CHECK(lineForLichess("info depth 3 score mate 2 pv e2e4", 15) ==
          "info multipv 1 time 15 nodes 0 depth 3 score mate "
          "2 pv e2e4");
    // bestmove, with or without its ponder move.
    CHECK(lineForLichess("bestmove e2e4 ponder e7e5", 0) == "bestmove e2e4 ponder e7e5");
    CHECK(lineForLichess("bestmove (none)", 0) == "bestmove (none)");
    // The others: left out.
    CHECK_FALSE(lineForLichess("info string NNUE evaluation using nn.bin depth 5 score pv", 0));
    CHECK_FALSE(lineForLichess("info depth 12 currmove e2e4 currmovenumber 1", 0));
    CHECK_FALSE(lineForLichess("info depth 12 score cp 30 nodes 100", 0));  // no pv
    CHECK_FALSE(lineForLichess("info depth 12 score cp 30 pv", 0));         // an empty one
    CHECK_FALSE(lineForLichess("readyok", 0));
    CHECK_FALSE(lineForLichess("", 0));
}

TEST_CASE("external engine: a correspondence game's positions are never analysed, the threads a tournament leaves") {
    EngineWork w;
    w.initial_fen    = START;
    w.moves          = {"e2e4", "e7e5", "g1f3"};
    w.depth          = 10;
    const auto after = [](std::vector<std::string> moves) {
        chess::Board b;
        for (const auto& m : moves) b.makeMove(chess::uci::uciToMove(b, m));
        return b.getFen();
    };
    // On its line, before its end: the whole line.
    CHECK(workLocked(w, {after({"e2e4", "e7e5"})}, false));
    // Its end.
    CHECK(workLocked(w, {after({"e2e4", "e7e5", "g1f3"})}, false));
    // One move after its end: the engine's line would give the answer to it.
    CHECK(workLocked(w, {after({"e2e4", "e7e5", "g1f3", "b8c6"})}, false));
    // Elsewhere: analysed.
    CHECK_FALSE(workLocked(w, {after({"d2d4", "d7d5"})}, false));
    CHECK_FALSE(workLocked(w, {}, false));
    // A game with no move yet locks the start position alone, not the lines that pass through it.
    CHECK_FALSE(workLocked(w, {}, true));
    EngineWork start;
    start.initial_fen = START;
    start.depth       = 10;
    CHECK(workLocked(start, {}, true));
    // A FEN of Lichess's, the castling rights written another way: the men and the side to move decide.
    CHECK(workLocked(w, {"rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b - - 1 2"}, false));

    // The tournament first, the bot's threads first: what the machine's other engines leave of its cores.
    CHECK(serviceThreads(8, 15, 0, 0) == 8);   // nothing else plays
    CHECK(serviceThreads(8, 15, 0, 13) == 2);  // a tournament takes 13: 2 left
    CHECK(serviceThreads(8, 15, 0, 15) == 0);  // none left: paused
    CHECK(serviceThreads(8, 15, 2, 12) == 1);  // the bot's game takes 2 of what the tournament leaves
    CHECK(serviceThreads(8, 4, 2, 0) == 2);    // a small machine: the bot first
    CHECK(serviceThreads(8, 15, 4, 14) == 0);
}

TEST_CASE("external engine: the long poll's 204, then a job answered: the engine set, its lines, then its bestmove") {
    ScopedEnv analysis("FCG_MOCK_ANALYSIS", "1");
    ScopedEnv chess960("FCG_MOCK_CHESS960", "1");
    FakeLichess lichess;
    lichess.push(204);
    auto job     = work("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1", {"e1h1"}, 3);
    job["depth"] = 4;
    job["hash"]  = 4096;  // more than registered: its maximum
    lichess.push(200, jobJson("job0001", "eei_Sample000001", job));
    std::vector<std::string> sent;
    std::mutex sent_mutex;
    ServiceGuards guards;
    guards.threads = 2;  // a tournament leaves two
    auto setup     = setupFor(lichess, &sent, &sent_mutex);
    setup.max_hash = 1024;
    EngineService service(setup, guards);
    REQUIRE(waitUntil([&] { return lichess.answered().size() == 1; }));
    const auto a = lichess.answered()[0];
    CHECK(a.id == "job0001");
    CHECK(a.whole);
    REQUIRE(a.lines.size() >= 4);
    // Its info lines: the three lines asked, each with what Lichess asks for; then its bestmove, last.
    std::vector<int> multipvs;
    for (std::size_t i = 0; i + 1 < a.lines.size(); ++i) {
        const auto w = words(a.lines[i]);
        REQUIRE(w.front() == "info");
        for (const char* key : {"depth", "multipv", "score", "nodes", "time", "pv"})
            CHECK_MESSAGE(std::find(w.begin(), w.end(), key) != w.end(), a.lines[i]);
        multipvs.push_back(std::stoi(*(std::find(w.begin(), w.end(), "multipv") + 1)));
    }
    CHECK(std::find(multipvs.begin(), multipvs.end(), 3) != multipvs.end());
    CHECK(std::all_of(multipvs.begin(), multipvs.end(), [](int m) { return m >= 1 && m <= 3; }));
    CHECK(a.lines.back().rfind("bestmove ", 0) == 0);
    // The poll: the secret alone, no token.
    {
        std::lock_guard lock(lichess.mutex);
        REQUIRE(lichess.poll_bodies.size() >= 2);
        CHECK(nlohmann::json::parse(lichess.poll_bodies[0]) ==
              nlohmann::json{{"providerSecret", "SampleSecret0123456789abcdefABCDEF0123456789xyzw"}});
        CHECK(lichess.poll_auth[0].empty());
    }
    // The engine as Lichess asks: Chess960, the analysis mode, the threads left, the hash within its maximum.
    CHECK(sentHas(sent, sent_mutex, "setoption name UCI_Chess960 value true"));
    CHECK(sentHas(sent, sent_mutex, "setoption name UCI_AnalyseMode value true"));
    CHECK(sentHas(sent, sent_mutex, "setoption name Threads value 2"));
    CHECK(sentHas(sent, sent_mutex, "setoption name Hash value 1024"));
    CHECK(sentHas(sent, sent_mutex, "setoption name MultiPV value 3"));
    CHECK(sentHas(sent, sent_mutex, "ucinewgame"));
    CHECK(sentHas(sent, sent_mutex, "position fen r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1 moves e1h1"));
    CHECK(sentHas(sent, sent_mutex, "go depth 4"));
    // The king took its rook: the engine's move is Black's, from there.
    {
        chess::Board board;
        board.set960(true);
        board.setFen("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
        board.makeMove(chess::uci::uciToMove(board, "e1h1"));
        chess::Movelist legal;
        chess::movegen::legalmoves(legal, board);
        const auto best = words(a.lines.back()).at(1);
        CHECK(std::find(legal.begin(), legal.end(), chess::uci::uciToMove(board, best)) != legal.end());
    }
    REQUIRE(waitUntil([&] { return service.status().last.has_value(); }));
    const auto st = service.status();
    CHECK(st.last->multipv == 3);
    CHECK(st.last->threads == 2);
    CHECK(st.last->depth == 8);
    CHECK_FALSE(st.last->best.empty());
    const auto counts = service.takeCounts();
    CHECK(counts.requests == 1);
    CHECK(service.takeCounts().requests == 0);
}

TEST_CASE("external engine: Lichess closes the answer, the engine stops") {
    ScopedEnv analysis("FCG_MOCK_ANALYSIS", "1");
    ScopedEnv chess960("FCG_MOCK_CHESS960", "1");
    ScopedEnv timed("FCG_MOCK_MOVETIME", "1");
    FakeLichess lichess;
    lichess.cut_after = 3;  // the requester goes after three lines
    auto job          = work(START, {"d2d4"});
    job["movetime"]   = 60000;
    lichess.push(200, jobJson("job0002", "eei_Sample000001", job));
    std::vector<std::string> sent;
    std::mutex sent_mutex;
    EngineService service(setupFor(lichess, &sent, &sent_mutex));
    REQUIRE(waitUntil([&] { return sentHas(sent, sent_mutex, "stop"); }, 10s));
    REQUIRE(waitUntil([&] { return service.status().last.has_value(); }));
    const auto st = service.status();
    CHECK(st.last->cut);
    CHECK(st.state != ServiceState::EngineFailed);
    CHECK(lichess.answered().size() == 1);
    CHECK_FALSE(lichess.answered()[0].whole);
    // Back to waiting for the next request.
    const auto polls = lichess.pollCount();
    REQUIRE(waitUntil([&] { return lichess.pollCount() > polls; }));
}

TEST_CASE("external engine: no threads left by a tournament, then the user's live game: paused, requests unanswered") {
    ScopedEnv analysis("FCG_MOCK_ANALYSIS", "1");
    ScopedEnv chess960("FCG_MOCK_CHESS960", "1");
    ScopedEnv timed("FCG_MOCK_MOVETIME", "1");
    FakeLichess lichess;
    std::vector<std::string> sent;
    std::mutex sent_mutex;
    ServiceGuards none;
    none.threads = 0;  // a tournament takes every core
    EngineService service(setupFor(lichess, &sent, &sent_mutex), none);
    REQUIRE(waitUntil([&] { return service.status().state == ServiceState::Paused; }));
    CHECK(service.status().pause == ServicePause::Tournament);
    // The tournament leaves some again: from then on only, the work host is asked.
    const auto resumed = std::chrono::steady_clock::now();
    ServiceGuards some;
    some.threads    = 3;
    auto job        = work(START, {});
    job["movetime"] = 60000;
    lichess.push(200, jobJson("job0003", "eei_Sample000001", job));
    service.setGuards(some);
    REQUIRE(waitUntil([&] { return lichess.pollCount() >= 1; }));
    {
        std::lock_guard lock(lichess.mutex);
        CHECK(lichess.poll_times.front() >= resumed);
    }
    REQUIRE(waitUntil([&] { return service.status().state == ServiceState::Busy && lichess.live_lines > 0; }));
    CHECK(sentHas(sent, sent_mutex, "setoption name Threads value 3"));
    // The user's live game starts: the search stops at once, its bestmove sent, and nothing more is asked meanwhile.
    auto live      = some;
    live.live_game = true;
    service.setGuards(live);
    REQUIRE(waitUntil([&] { return lichess.answered().size() == 1; }, 10s));
    CHECK(sentHas(sent, sent_mutex, "stop"));
    CHECK(lichess.answered()[0].whole);
    CHECK(lichess.answered()[0].lines.back().rfind("bestmove", 0) == 0);
    REQUIRE(waitUntil([&] { return service.status().state == ServiceState::Paused; }));
    CHECK(service.status().pause == ServicePause::LiveGame);
    const auto polls = lichess.pollCount();
    // Its end: it resumes by itself.
    service.setGuards(some);
    REQUIRE(waitUntil([&] { return lichess.pollCount() > polls; }));
    CHECK(service.status().state != ServiceState::Paused);
}

TEST_CASE("external engine: a correspondence game's position answered empty, a tournament's threads taking a job's") {
    ScopedEnv analysis("FCG_MOCK_ANALYSIS", "1");
    ScopedEnv chess960("FCG_MOCK_CHESS960", "1");
    ScopedEnv timed("FCG_MOCK_MOVETIME", "1");
    FakeLichess lichess;
    const std::string fen = "rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2";
    auto locked           = work(START, {"e2e4", "e7e5", "g1f3"});
    locked["depth"]       = 5;
    lichess.push(200, jobJson("job0004", "eei_Sample000001", locked));
    std::vector<std::string> sent;
    std::mutex sent_mutex;
    ServiceGuards g;
    g.threads = 4;
    g.locked  = {fen};
    EngineService service(setupFor(lichess, &sent, &sent_mutex), g);
    REQUIRE(waitUntil([&] { return lichess.answered().size() == 1; }));
    CHECK(lichess.answered()[0].lines == std::vector<std::string>{"bestmove (none)"});
    CHECK_FALSE(sentStarting(sent, sent_mutex, "go"));  // the engine never saw it
    REQUIRE(waitUntil([&] { return service.status().last.has_value(); }));
    CHECK(service.status().last->locked);

    // A job under way when a tournament takes the threads it uses: stopped there.
    auto job        = work(START, {"d2d4"});
    job["movetime"] = 60000;
    lichess.push(200, jobJson("job0005", "eei_Sample000001", job));
    REQUIRE(waitUntil([&] { return service.status().state == ServiceState::Busy && lichess.live_lines > 0; }));
    auto fewer    = g;
    fewer.threads = 1;
    service.setGuards(fewer);
    REQUIRE(waitUntil([&] { return lichess.answered().size() == 2; }, 10s));
    CHECK(sentHas(sent, sent_mutex, "stop"));
    CHECK(lichess.answered()[1].lines.back().rfind("bestmove", 0) == 0);
}

TEST_CASE("external engine: stop, resume, withdraw, and the service resumed at the start") {
    ScopedEnv analysis("FCG_MOCK_ANALYSIS", "1");
    ScopedEnv chess960("FCG_MOCK_CHESS960", "1");
    FakeLichess lichess;
    const auto profile = fs::temp_directory_path() / "external-engine-resume";
    fs::remove_all(profile);
    fs::create_directories(profile);
    OfferedEngine o;
    o.engine           = "Sample Engine 1.0";
    o.name             = "Sample Engine 1.0 (RookStudio)";
    o.id               = "eei_Sample000001";
    o.secret           = "SampleSecret0123456789abcdefABCDEF0123456789xyzw";
    o.serving          = true;
    OfferedEngine idle = o;
    idle.engine        = "Idle Engine 2.0";
    idle.serving       = false;
    REQUIRE(saveOffers(profile, {o, idle}));
    // At the start: the one in service when RookStudio closed, its engine in the library.
    const auto library = std::vector<EngineEntry>{mover("Sample Engine 1.0"), mover("Idle Engine 2.0")};
    CHECK(offersToResume(loadOffers(profile), library) == std::vector<std::string>{"Sample Engine 1.0"});
    CHECK(offersToResume(loadOffers(profile), {mover("Another")}).empty());
    {
        EngineService service(setupFor(lichess, nullptr, nullptr));
        REQUIRE(waitUntil([&] { return lichess.pollCount() >= 2; }));
        // Stopped: no more waiting for work; the registration stays at Lichess.
    }
    const auto after_stop = lichess.pollCount();
    CHECK_FALSE(lichess.withdrawn);
    // Resumed: asked again.
    {
        EngineService service(setupFor(lichess, nullptr, nullptr));
        REQUIRE(waitUntil([&] { return lichess.pollCount() > after_stop; }));
    }
    // Withdrawn: one request with the token; the registration gone, its secret unused.
    CHECK_FALSE(withdrawEngine(TOKEN, o.id).failed());
    CHECK(lichess.withdrawn);
    const auto gone = engineRegistration(TOKEN, o.id);
    CHECK_FALSE(gone.value);
    CHECK_FALSE(gone.failed());
    CHECK(gone.status == 404);
    CHECK_FALSE(withdrawEngine(TOKEN, o.id).failed());  // gone already: nothing wrong
    fs::remove_all(profile);
}

TEST_CASE("external engine: the secret refused (401), a changed API, an engine without Chess960") {
    ScopedEnv analysis("FCG_MOCK_ANALYSIS", "1");
    {
        FakeLichess lichess;
        lichess.push(401, R"({"error":"invalid provider secret"})");
        EngineService service(setupFor(lichess, nullptr, nullptr));
        REQUIRE(waitUntil([&] { return service.ended(); }));
        CHECK(service.status().state == ServiceState::Refused);
        CHECK(service.status().failure.net == NetError::Unauthorized);
    }
    {
        FakeLichess lichess;
        lichess.push(200, jobJson("job0006", "eei_Sample000001", work(START, {})));  // no limit
        EngineService service(setupFor(lichess, nullptr, nullptr));
        REQUIRE(waitUntil([&] { return service.ended(); }));
        CHECK(service.status().state == ServiceState::ApiChanged);
        CHECK(service.status().error.find("limit") != std::string::npos);
        CHECK(lichess.answered().empty());  // rather than answer wrongly
    }
    {
        FakeLichess lichess;
        lichess.push(404, R"({"error":"Not found"})");
        EngineService service(setupFor(lichess, nullptr, nullptr));
        REQUIRE(waitUntil([&] { return service.ended(); }));
        CHECK(service.status().state == ServiceState::ApiChanged);
    }
    {
        // No UCI_Chess960 (the mock without FCG_MOCK_CHESS960): it cannot serve, and says so; the job answered empty.
        FakeLichess lichess;
        auto job     = work(START, {});
        job["depth"] = 3;
        lichess.push(200, jobJson("job0007", "eei_Sample000001", job));
        EngineService service(setupFor(lichess, nullptr, nullptr));
        REQUIRE(waitUntil([&] { return service.ended(); }));
        CHECK(service.status().state == ServiceState::EngineFailed);
        CHECK(service.status().chess960_missing);
        CHECK(service.status().error.find("Chess960") != std::string::npos);
        REQUIRE(lichess.answered().size() == 1);
        CHECK(lichess.answered()[0].lines == std::vector<std::string>{"bestmove (none)"});
    }
}

TEST_CASE("external engine: what a job holds is checked before the engine reads it, an ended engine started again") {
    // A move as UCI writes it, nothing else.
    CHECK(uciMoveText("e2e4"));
    CHECK(uciMoveText("e7e8q"));
    CHECK(uciMoveText("e1h1"));
    CHECK_FALSE(uciMoveText("a0b1"));  // a square that is not one
    CHECK_FALSE(uciMoveText("0000"));
    CHECK_FALSE(uciMoveText("e7e8k"));
    CHECK_FALSE(uciMoveText("e2e4 "));
    CHECK_FALSE(uciMoveText(""));
    ScopedEnv analysis("FCG_MOCK_ANALYSIS", "1");
    ScopedEnv chess960("FCG_MOCK_CHESS960", "1");
    ScopedEnv quit("FCG_MOCK_QUIT_AFTER", "1");  // the engine ends after each search
    FakeLichess lichess;
    // A line break in its FEN would give the engine another command; a move that is not one: answered empty, nothing of
    // either sent to the engine.
    auto injected     = work(START + "\nsetoption name Debug Log File value sample.log", {});
    injected["depth"] = 3;
    lichess.push(200, jobJson("job0008", "eei_Sample000001", injected));
    auto bad_move     = work(START, {"e2e4", "a0b1"});
    bad_move["depth"] = 3;
    lichess.push(200, jobJson("job0009", "eei_Sample000001", bad_move));
    // Two requests: the engine ends after the first, and is started again for the second.
    for (const char* id : {"job0010", "job0011"}) {
        auto good     = work(START, {"e2e4"});
        good["depth"] = 3;
        lichess.push(200, jobJson(id, "eei_Sample000001", good));
    }
    std::vector<std::string> sent;
    std::mutex sent_mutex;
    EngineService service(setupFor(lichess, &sent, &sent_mutex));
    REQUIRE(waitUntil([&] { return lichess.answered().size() == 4; }));
    const auto answers = lichess.answered();
    CHECK(answers[0].lines == std::vector<std::string>{"bestmove (none)"});
    CHECK(answers[1].lines == std::vector<std::string>{"bestmove (none)"});
    for (std::size_t i = 2; i < 4; ++i) {
        REQUIRE_FALSE(answers[i].lines.empty());
        CHECK(answers[i].lines.back().rfind("bestmove ", 0) == 0);
        CHECK(answers[i].lines.back() != "bestmove (none)");
    }
    CHECK_FALSE(service.ended());
    std::lock_guard lock(sent_mutex);
    CHECK(std::count(sent.begin(), sent.end(), "uci") == 2);
    CHECK(std::none_of(sent.begin(), sent.end(), [](const std::string& l) {
        return l.find("Debug Log File") != std::string::npos || l.find("a0b1") != std::string::npos;
    }));
}

TEST_CASE("external engine: offline, it waits for the network and resumes by itself") {
    ScopedEnv analysis("FCG_MOCK_ANALYSIS", "1");
    // A port known before anything listens there (freePort()): nothing at first, then Lichess on it.
    const auto port = freePort();
    const auto base = fmt::format("http://127.0.0.1:{}", port);
    servers()       = Servers{base, base + "/tb", base + "/explorer", base + "/engine"};
    EngineService::Setup setup;
    setup.engine    = mover();
    setup.secret    = "SampleSecret0123456789abcdefABCDEF0123456789xyzw";
    setup.engine_id = "eei_Sample000001";
    setup.host      = servers().engine;
    setup.retry     = {20ms, 40ms};
    EngineService service(setup);
    // Nothing listens: offline, asked again by itself.
    REQUIRE(waitUntil([&] { return service.status().state == ServiceState::Offline; }));
    CHECK(service.status().failure.net == NetError::Offline);
    CHECK(describe(service.status().failure) == "Lichess cannot be reached");
    CHECK_FALSE(service.ended());
    // The network back: it waits for work again.
    FakeLichess lichess(port);
    REQUIRE(waitUntil([&] { return lichess.pollCount() >= 1; }));
    REQUIRE(waitUntil([&] { return service.status().state == ServiceState::Waiting; }));
    servers() = Servers{};
}

TEST_CASE("external engine: too many requests, or the work host in trouble: offline, asked again by itself") {
    ScopedEnv analysis("FCG_MOCK_ANALYSIS", "1");
    {
        // A 429: the host's pause, then waiting for work again.
        HostGate::instance().setPause(400ms);
        FakeLichess lichess;
        lichess.push(429, R"({"error":"Too many requests"})");
        EngineService service(setupFor(lichess, nullptr, nullptr));
        REQUIRE(waitUntil([&] { return service.status().failure.net == NetError::RateLimited; }));
        CHECK(service.status().state == ServiceState::Offline);
        REQUIRE(waitUntil([&] { return service.status().state == ServiceState::Waiting; }));
        CHECK_FALSE(service.status().failure.failed());
        HostGate::instance().setPause(60s);
    }
    {
        // A 503: Lichess in trouble, asked again later.
        FakeLichess lichess;
        lichess.push(503, "<html>down</html>");
        auto setup  = setupFor(lichess, nullptr, nullptr);
        setup.retry = {60s};  // stopped meanwhile: the wait ends with it
        EngineService service(setup);
        REQUIRE(waitUntil([&] { return service.status().state == ServiceState::Offline; }));
        CHECK(service.status().failure.net == NetError::Server);
        CHECK(service.status().failure.status == 503);
        CHECK(describe(service.status().failure) == "Lichess answered 503");
        CHECK_FALSE(service.ended());
    }
}

TEST_CASE("external engine: each failure of the registration's requests told by its kind") {
    std::mutex mutex;
    std::pair<int, std::string> next{200, "{}"};  // what the server answers next
    ScriptedServer server(0, [&](const std::string& head, net::TcpConnection& c) {
        (void)requestBody(head, c);  // read whole before the answer (Windows: scripted_server.hpp)
        std::pair<int, std::string> now;
        {
            std::lock_guard lock(mutex);
            now = next;
        }
        c.sendAll(reply(now.first, now.second));
    });
    const auto port = server.port();
    REQUIRE(server.ok());
    servers().api = fmt::format("http://127.0.0.1:{}", port);
    HostGate::instance().setPause(50ms);
    const auto answering = [&](int status, std::string body) {
        std::lock_guard lock(mutex);
        next = {status, std::move(body)};
    };
    EngineRegistration r;
    r.name                       = "Sample Engine 1.0 (RookStudio)";
    r.provider_secret            = "SampleSecret0123456789abcdefABCDEF0123456789xyzw";
    constexpr const char* UNREAD = "Lichess's answer could not be read: the external engine API may have changed";
    struct Case {
        int status;
        const char* body;
        NetError net;
        const char* words;
    };
    for (const auto& [status, body, net, words] :
         {Case{403, R"({"error":"Missing scope"})", NetError::Forbidden,
               "the token may not register engines (engine:write)"},
          Case{400, R"({"error":{"name":["Too long"]}})", NetError::Server, "name: Too long"},
          Case{429, "{}", NetError::RateLimited, "Lichess asks to wait a minute: too many requests"},
          Case{503, "<html>down</html>", NetError::Server, "Lichess answered 503"},
          Case{200, "<html>not JSON</html>", NetError::BadAnswer, UNREAD},
          Case{200, R"({"name":"no id"})", NetError::BadAnswer, UNREAD}}) {
        CAPTURE(status);
        CAPTURE(body);
        answering(status, body);
        const auto registered = registerEngine(TOKEN, r);
        CHECK_FALSE(registered.value);
        CHECK(registered.net == net);
        CHECK(describe(registered) == words);
        CHECK(updateEngine(TOKEN, "eei_Sample000001", r).net == net);
        // A withdrawal reads no body: a 200 is taken.
        CHECK(withdrawEngine(TOKEN, "eei_Sample000001").net == (status == 200 ? NetError::None : net));
    }
    // A registration Lichess would refuse: not sent, in the program's words.
    auto bad        = r;
    bad.max_threads = 0;
    const auto own  = registerEngine(TOKEN, bad);
    CHECK(own.net == NetError::None);
    CHECK(own.failed());
    CHECK(describe(own) == registrationProblem(bad));
    // Nothing listens: no answer (freePort(): a free port nothing is started on).
    servers().api = fmt::format("http://127.0.0.1:{}", freePort());
    CHECK(registerEngine(TOKEN, r).net == NetError::Offline);
    CHECK(withdrawEngine(TOKEN, "eei_Sample000001").net == NetError::Offline);
    // Cancelled while the host pauses after a 429: nothing sent.
    HostGate::instance().setPause(60s);
    HostGate::instance().tooMany("lichess-engines.invalid");
    servers().api = "http://lichess-engines.invalid";
    std::atomic<bool> cancel{true};
    CHECK(registerEngine(TOKEN, r, &cancel).net == NetError::Cancelled);
    servers() = Servers{};
}
