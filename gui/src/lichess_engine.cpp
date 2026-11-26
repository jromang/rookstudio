#include "lichess_engine.hpp"

#include <algorithm>
#include <cctype>
#include <deque>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <psa/crypto.h>
#include <chess.hpp>

#include <fmt/include/fmt/format.h>
#include <tour/engine_text.hpp>

#include "console.hpp"
#include "engine_client.hpp"
#include "engines.hpp"
#include "https.hpp"
#include "lichess_playing.hpp"
#include "storage.hpp"

namespace fcg::lichess {

bool uciMoveText(std::string_view m) {
    if (m.size() != 4 && m.size() != 5) return false;
    const auto file = [](char c) { return c >= 'a' && c <= 'h'; };
    const auto rank = [](char c) { return c >= '1' && c <= '8'; };
    if (!file(m[0]) || !rank(m[1]) || !file(m[2]) || !rank(m[3])) return false;
    return m.size() == 4 || std::string_view("nbrq").find(m[4]) != std::string_view::npos;
}

namespace {

namespace fs = std::filesystem;
using Clock  = std::chrono::steady_clock;

int intOf(const nlohmann::json& j, const char* key, int otherwise = 0) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number_integer() ? it->get<int>() : otherwise;
}

// Lichess's words for a refusal: {"error": "…"}, or a form's {"error": {"name": ["…"]}}; empty when it gave none.
std::string lichessWords(const std::string& body) {
    const auto j = nlohmann::json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return {};
    const auto it = j.find("error");
    if (it == j.end()) return {};
    if (it->is_string()) return it->get<std::string>();
    if (!it->is_object()) return {};
    std::string out;
    for (const auto& [field, messages] : it->items()) {
        std::string said;
        if (messages.is_array()) {
            for (const auto& m : messages)
                if (m.is_string()) said += (said.empty() ? "" : " ") + m.get<std::string>();
        } else if (messages.is_string()) {
            said = messages.get<std::string>();
        }
        out += (out.empty() ? "" : "; ") + field + ": " + said;
    }
    return out;
}

std::string reason(const HttpResult& r) {
    if (r.status == 0) return r.error.empty() ? std::string("Lichess did not answer") : r.error;
    if (r.status == 401) return "Lichess refused the token";
    if (r.status == 403) return "the token may not register engines (engine:write)";
    if (r.status == 429) return "Lichess asks to wait a minute: too many requests";
    if (const auto words = lichessWords(r.body); !words.empty())
        return fmt::format("Lichess answered {}: {}", r.status, words);
    return fmt::format("Lichess answered {}", r.status);
}

HttpRequest apiRequest(const std::string& path, const std::string& method, const std::string& token,
                       std::string body = {}) {
    HttpRequest r;
    r.url    = servers().api + path;
    r.method = method;
    r.token  = token;
    r.headers.emplace_back("Accept", "application/json");
    if (!body.empty()) {
        r.content_type = "application/json";
        r.body         = std::move(body);
    }
    return r;
}

Answer<ExternalEngine> engineAnswer(const HttpResult& r) {
    Answer<ExternalEngine> a;
    a.status = r.status;
    if (r.status != 200) {
        a.error = reason(r);
        return a;
    }
    const auto j = nlohmann::json::parse(r.body, nullptr, false);
    if (auto e = j.is_discarded() ? std::nullopt : readExternalEngine(j))
        a.value = std::move(*e);
    else
        a.error = "Lichess's answer could not be read: the external engine API may have changed";
    return a;
}

std::vector<std::string> words(std::string_view line) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at < line.size()) {
        while (at < line.size() && (line[at] == ' ' || line[at] == '\t' || line[at] == '\r')) ++at;
        const auto start = at;
        while (at < line.size() && line[at] != ' ' && line[at] != '\t' && line[at] != '\r') ++at;
        if (at > start) out.emplace_back(line.substr(start, at - start));
    }
    return out;
}

// A FEN the board and the engine may be given: its placement whole (8 ranks of 8 squares), its side, and fields of the
// characters FENs use, nothing else (a line break would make the engine read another command).
bool fenText(std::string_view fen) {
    for (const unsigned char c : fen)
        if (c < 0x20 || c > 0x7e) return false;
    if (!menOf(fen)) return false;
    const auto w = words(fen);
    if (w.size() < 2 || w.size() > 6 || (w[1] != "w" && w[1] != "b")) return false;
    const auto all = [](const std::string& text, std::string_view allowed) {
        return !text.empty() && text.find_first_not_of(allowed) == std::string::npos;
    };
    if (w.size() > 2 && !all(w[2], "-KQkqABCDEFGHabcdefgh")) return false;
    if (w.size() > 3 && w[3] != "-" &&
        !(w[3].size() == 2 && w[3][0] >= 'a' && w[3][0] <= 'h' && (w[3][1] == '3' || w[3][1] == '6')))
        return false;
    for (std::size_t i = 4; i < w.size(); ++i)
        if (!all(w[i], "0123456789") || w[i].size() > 4) return false;
    return true;
}

// A board in UCI_Chess960's convention (castling as the king taking its rook), as Lichess sends the moves; a move
// written the usual way (e1g1) is read too. False when the FEN or a move cannot be read.
bool replay(const EngineWork& w, const std::function<void(const chess::Board&)>& each, chess::Board* end = nullptr) {
    if (!fenText(w.initial_fen)) return false;
    for (const auto& uci : w.moves)
        if (!uciMoveText(uci)) return false;
    chess::Board board;
    board.set960(true);
    try {
        if (!board.setFen(w.initial_fen)) return false;
    } catch (...) {
        return false;
    }
    const auto legal = [&](chess::Move m) {
        chess::Movelist moves;
        chess::movegen::legalmoves(moves, board);
        return std::find(moves.begin(), moves.end(), m) != moves.end();
    };
    each(board);
    for (const auto& uci : w.moves) {
        chess::Move m = chess::Move::NO_MOVE;
        try {
            m = chess::uci::uciToMove(board, uci);
            if (!legal(m) && uci.size() >= 4) {
                // e1g1: the king two squares aside, castling as most programs write it.
                const chess::Square from(uci.substr(0, 2)), to(uci.substr(2, 2));
                if (board.at(from).type() == chess::PieceType::KING && from.rank() == to.rank() &&
                    chess::Square::distance(from, to) == 2) {
                    const chess::Square rook(to > from ? chess::File::FILE_H : chess::File::FILE_A, from.rank());
                    m = chess::Move::make<chess::Move::CASTLING>(from, rook);
                }
            }
        } catch (...) {
            return false;
        }
        if (!legal(m)) return false;
        board.makeMove(m);
        each(board);
    }
    if (end) *end = board;
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// The registration

std::optional<ExternalEngine> readExternalEngine(const nlohmann::json& j) {
    if (!j.is_object()) return std::nullopt;
    ExternalEngine e;
    e.id            = textOf(j, "id");
    e.name          = textOf(j, "name");
    e.client_secret = textOf(j, "clientSecret");
    e.user_id       = textOf(j, "userId");
    e.max_threads   = intOf(j, "maxThreads");
    e.max_hash      = intOf(j, "maxHash");
    e.provider_data = textOf(j, "providerData");
    if (const auto v = j.find("variants"); v != j.end() && v->is_array())
        for (const auto& one : *v)
            if (one.is_string()) e.variants.push_back(one.get<std::string>());
    if (e.id.empty() || e.name.empty() || e.max_threads < 1 || e.max_hash < 1) return std::nullopt;
    return e;
}

std::string registrationJson(const EngineRegistration& r) {
    nlohmann::json j{{"name", r.name},
                     {"maxThreads", r.max_threads},
                     {"maxHash", r.max_hash},
                     {"variants", nlohmann::json::array({"chess"})},
                     {"providerSecret", r.provider_secret}};
    if (!r.provider_data.empty()) j["providerData"] = r.provider_data;
    return j.dump();
}

int nameLength(std::string_view utf8) {
    int n = 0;
    for (const unsigned char c : utf8)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

std::string registrationProblem(const EngineRegistration& r) {
    const int length = nameLength(r.name);
    if (length < NAME_CHARS_MIN || length > NAME_CHARS_MAX)
        return fmt::format("its name has {} characters: Lichess takes {} to {}", length, NAME_CHARS_MIN,
                           NAME_CHARS_MAX);
    if (r.max_threads < 1 || r.max_threads > THREADS_MOST) return "its threads: 1 to 65536";
    if (r.max_hash < 1 || r.max_hash > HASH_MOST_MB) return "its memory: 1 to 1048576 MB";
    const auto secret = static_cast<int>(r.provider_secret.size());
    if (secret < SECRET_CHARS_MIN || secret > SECRET_CHARS_MAX) return "its secret: 16 to 1024 characters";
    return {};
}

std::string newProviderSecret() {
    static constexpr char CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    if (psa_crypto_init() != PSA_SUCCESS) throw std::runtime_error("no random numbers for a secret");
    std::string out;
    while (out.size() < 48) {
        uint8_t bytes[64];
        if (psa_generate_random(bytes, sizeof(bytes)) != PSA_SUCCESS)
            throw std::runtime_error("no random numbers for a secret");
        for (const auto b : bytes)
            if (b < 248 && out.size() < 48) out += CHARS[b % 62];  // 248 = 4 × 62: no letter more likely
    }
    return out;
}

Answer<ExternalEngine> registerEngine(const std::string& token, const EngineRegistration& r,
                                      const std::atomic<bool>* cancel) {
    if (const auto problem = registrationProblem(r); !problem.empty()) {
        Answer<ExternalEngine> a;
        a.error = problem;
        return a;
    }
    return engineAnswer(politeRequest(apiRequest("/api/external-engine", "POST", token, registrationJson(r)), cancel));
}

Answer<std::vector<ExternalEngine>> listEngines(const std::string& token, const std::atomic<bool>* cancel) {
    Answer<std::vector<ExternalEngine>> a;
    const auto r = politeRequest(apiRequest("/api/external-engine", "GET", token), cancel);
    a.status     = r.status;
    if (r.status != 200) {
        a.error = reason(r);
        return a;
    }
    const auto j = nlohmann::json::parse(r.body, nullptr, false);
    if (j.is_discarded() || !j.is_array()) {
        a.error = "Lichess's answer could not be read: the external engine API may have changed";
        return a;
    }
    std::vector<ExternalEngine> list;
    for (const auto& one : j)
        if (auto e = readExternalEngine(one)) list.push_back(std::move(*e));
    a.value = std::move(list);
    return a;
}

Answer<ExternalEngine> engineRegistration(const std::string& token, const std::string& id,
                                          const std::atomic<bool>* cancel) {
    const auto r = politeRequest(apiRequest("/api/external-engine/" + urlEncode(id), "GET", token), cancel);
    if (r.status == 404) {
        Answer<ExternalEngine> a;
        a.status = 404;
        return a;
    }
    return engineAnswer(r);
}

Answer<ExternalEngine> updateEngine(const std::string& token, const std::string& id, const EngineRegistration& r,
                                    const std::atomic<bool>* cancel) {
    if (const auto problem = registrationProblem(r); !problem.empty()) {
        Answer<ExternalEngine> a;
        a.error = problem;
        return a;
    }
    return engineAnswer(
        politeRequest(apiRequest("/api/external-engine/" + urlEncode(id), "PUT", token, registrationJson(r)), cancel));
}

std::string withdrawEngine(const std::string& token, const std::string& id, const std::atomic<bool>* cancel) {
    const auto r = politeRequest(apiRequest("/api/external-engine/" + urlEncode(id), "DELETE", token), cancel);
    return r.status == 200 || r.status == 204 || r.status == 404 ? std::string() : reason(r);
}

// ---------------------------------------------------------------------------
// The work

std::optional<EngineWork> readEngineWork(std::string_view json, std::string* error) {
    const auto fail = [&](std::string why) -> std::optional<EngineWork> {
        if (error) *error = std::move(why);
        return std::nullopt;
    };
    const auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return fail("the work host's answer is not JSON");
    EngineWork w;
    w.id = textOf(j, "id");
    if (w.id.empty()) return fail("a job without its id");
    const auto engine = j.find("engine");
    if (engine == j.end() || !engine->is_object() || textOf(*engine, "id").empty())
        return fail("a job without its engine");
    w.engine_id     = textOf(*engine, "id");
    const auto work = j.find("work");
    if (work == j.end() || !work->is_object()) return fail("a job without its work");
    const auto& o     = *work;
    const auto number = [&](const char* key, int64_t low, int64_t high, int64_t& out) {
        const auto it = o.find(key);
        if (it == o.end()) return false;
        if (!it->is_number_integer()) return false;
        const auto v = it->get<int64_t>();
        if (v < low || v > high) return false;
        out = v;
        return true;
    };
    if (!o.contains("sessionId") || !o["sessionId"].is_string()) return fail("a job without its sessionId");
    w.session = o["sessionId"].get<std::string>();
    int64_t v = 0;
    if (!number("threads", 1, THREADS_MOST, v)) return fail("a job without its threads");
    w.threads = static_cast<int>(v);
    if (!number("hash", 1, HASH_MOST_MB, v)) return fail("a job without its hash");
    w.hash = static_cast<int>(v);
    if (!number("multiPv", 1, 5, v)) return fail("a job whose multiPv is not 1 to 5");
    w.multipv = static_cast<int>(v);
    w.variant = textOf(o, "variant");
    if (w.variant.empty()) return fail("a job without its variant");
    if (!o.contains("initialFen") || !o["initialFen"].is_string()) return fail("a job without its initialFen");
    w.initial_fen    = o["initialFen"].get<std::string>();
    const auto moves = o.find("moves");
    if (moves == o.end() || !moves->is_array()) return fail("a job without its moves");
    for (const auto& m : *moves) {
        if (!m.is_string()) return fail("a job whose moves are not UCI");
        w.moves.push_back(m.get<std::string>());
    }
    // Its limit: a time, a depth or nodes (the spec's oneOf).
    if (number("movetime", 1, INT64_MAX, v))
        w.movetime = v;
    else if (number("depth", 1, 1000, v))
        w.depth = static_cast<int>(v);
    else if (number("nodes", 1, INT64_MAX, v))
        w.nodes = v;
    else
        return fail("a job without its limit (movetime, depth or nodes)");
    return w;
}

std::string workPosition(const EngineWork& w) {
    std::string out = "position fen " + w.initial_fen;
    if (!w.moves.empty()) {
        out += " moves";
        for (const auto& m : w.moves) out += " " + m;
    }
    return out;
}

std::string workGo(const EngineWork& w) {
    if (w.movetime > 0) return fmt::format("go movetime {}", w.movetime);
    if (w.depth > 0) return fmt::format("go depth {}", w.depth);
    return fmt::format("go nodes {}", w.nodes);
}

std::string workFen(const EngineWork& w) {
    chess::Board end;
    if (!replay(w, [](const chess::Board&) {}, &end)) return {};
    return end.getFen();
}

std::optional<std::string> lineForLichess(std::string_view line, int64_t elapsed_ms) {
    const auto w = words(line);
    if (w.empty()) return std::nullopt;
    if (w[0] == "bestmove") {
        if (w.size() < 2) return std::nullopt;
        std::string out = "bestmove " + w[1];
        if (w.size() >= 4 && w[2] == "ponder") out += " ponder " + w[3];
        return out;
    }
    if (w[0] != "info") return std::nullopt;
    bool depth = false, score = false, multipv = false, nodes = false, time = false;
    std::size_t pv = 0;
    for (std::size_t i = 1; i < w.size(); ++i) {
        const auto& k = w[i];
        if (k == "string") return std::nullopt;  // the rest is text
        if (k == "pv") {
            pv = i;
            break;
        }
        if (k == "depth") depth = true;
        if (k == "score") score = true;
        if (k == "multipv") multipv = true;
        if (k == "nodes") nodes = true;
        if (k == "time") time = true;
    }
    if (!depth || !score || pv == 0 || pv + 1 >= w.size()) return std::nullopt;
    std::string out = "info";
    if (!multipv) out += " multipv 1";
    if (!time) out += fmt::format(" time {}", std::max<int64_t>(0, elapsed_ms));
    if (!nodes) out += " nodes 0";
    for (std::size_t i = 1; i < w.size(); ++i) out += " " + w[i];
    return out;
}

bool workLocked(const EngineWork& w, const std::vector<std::string>& locked, bool start_locked) {
    std::vector<Men> wanted;
    for (const auto& fen : locked)
        if (const auto m = menOf(std::string_view(fen))) wanted.push_back(*m);
    if (wanted.empty() && !start_locked) return false;
    bool found = false;
    chess::Board end;
    if (!replay(
            w,
            [&](const chess::Board& b) {
                if (found) return;
                const auto m = menOf(b);
                found        = std::find(wanted.begin(), wanted.end(), m) != wanted.end();
            },
            &end))
        return found;
    if (found) return true;
    // The start position, locked alone by a game that has had no move yet.
    static const auto start = *menOf(std::string_view(chess::constants::STARTPOS));
    if (start_locked && menOf(end) == start) return true;
    // One move from its end: the engine's line from the position before the opponent's move would answer it.
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, end);
    for (const auto& m : moves) {
        end.makeMove(m);
        const bool hit = std::find(wanted.begin(), wanted.end(), menOf(end)) != wanted.end();
        end.unmakeMove(m);
        if (hit) return true;
    }
    return false;
}

int serviceThreads(int limit, int capacity, int others, int tournaments) {
    return std::max(0, std::min(limit, capacity - others - tournaments));
}

// ---------------------------------------------------------------------------
// What the profile keeps

std::vector<OfferedEngine> loadOffers(const fs::path& profile) {
    std::ifstream in(profile / ENGINES_FILE, std::ios::binary);
    if (!in) return {};
    const auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("engines") || !j["engines"].is_array()) return {};
    std::vector<OfferedEngine> out;
    for (const auto& one : j["engines"]) {
        try {
            auto o = one.get<OfferedEngine>();
            if (!o.engine.empty()) out.push_back(std::move(o));
        } catch (const std::exception&) {
        }
    }
    return out;
}

bool saveOffers(const fs::path& profile, const std::vector<OfferedEngine>& offers, std::string* error) {
    return writeJson(profile / ENGINES_FILE, nlohmann::json{{"engines", offers}}, error, false, true);
}

void countRequest(OfferedEngine& offer, int requests, int64_t ms, const std::string& day) {
    if (offer.day != day) {
        offer.day         = day;
        offer.requests    = 0;
        offer.analysis_ms = 0;
    }
    offer.requests += requests;
    offer.analysis_ms += ms;
}

std::vector<std::string> offersToResume(const std::vector<OfferedEngine>& offers,
                                        const std::vector<EngineEntry>& library) {
    std::vector<std::string> out;
    for (const auto& o : offers) {
        if (!o.serving || o.id.empty() || o.secret.empty()) continue;
        if (std::any_of(library.begin(), library.end(), [&](const EngineEntry& e) { return e.name == o.engine; }))
            out.push_back(o.engine);
    }
    return out;
}

// ---------------------------------------------------------------------------
// The service

struct EngineService::Engine {
    EngineProcess process;
    std::vector<std::string> options;        // as it named them, buttons left out
    int threads = 0, hash = 0, multipv = 0;  // as last set
    std::deque<std::string> lines;           // read, not yet used
    [[nodiscard]] bool has(std::string_view name) const {
        return std::find(options.begin(), options.end(), name) != options.end();
    }
};

namespace {

// The engine's next line, waited for `wait` at most; nullopt when none came, or it ended (`ended` set).
std::optional<std::string> nextLine(EngineService::Engine& e, std::chrono::milliseconds wait, bool* ended = nullptr) {
    const auto deadline = Clock::now() + wait;
    for (;;) {
        if (!e.lines.empty()) {
            auto line = std::move(e.lines.front());
            e.lines.pop_front();
            return line;
        }
        for (auto& l : e.process.take())
            if (l.kind == EngineProcess::Line::Kind::Out) e.lines.push_back(std::move(l.text));
        if (!e.lines.empty()) continue;
        if (!e.process.running()) {
            if (ended) *ended = true;
            return std::nullopt;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (left.count() <= 0) return std::nullopt;
        e.process.wait(std::min(left, std::chrono::milliseconds(50)));
    }
}

// Waits for a line that starts with `word` within `wait`, passing the others to `each`; false on the deadline, the
// engine's end, or `stop` (the service stops: RookStudio closes, the service is stopped).
bool waitWord(EngineService::Engine& e, std::string_view word, std::chrono::milliseconds wait,
              const std::atomic<bool>& stop, const std::function<void(const std::string&)>& each = {}) {
    const auto deadline = Clock::now() + wait;
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (left.count() <= 0 || stop) return false;
        bool ended      = false;
        const auto line = nextLine(e, std::min(left, std::chrono::milliseconds(250)), &ended);
        if (ended) return false;
        if (!line) continue;
        if (line->rfind(word, 0) == 0 && (line->size() == word.size() || (*line)[word.size()] == ' ')) return true;
        if (each) each(*line);
    }
}

bool paused(const ServiceGuards& g) { return g.live_game || g.threads <= 0; }

}  // namespace

EngineService::EngineService(Setup setup, ServiceGuards guards) : setup_(std::move(setup)), guards_(std::move(guards)) {
    status_.state = paused(guards_) ? ServiceState::Paused : ServiceState::Waiting;
    status_.pause = guards_.live_game ? ServicePause::LiveGame
                    : paused(guards_) ? ServicePause::Tournament
                                      : ServicePause::None;
    thread_       = std::thread([this] { run(); });
}

EngineService::~EngineService() {
    stop_        = true;
    poll_cancel_ = true;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void EngineService::setGuards(const ServiceGuards& g) {
    {
        std::lock_guard lock(mutex_);
        if (guards_ == g) return;
        guards_ = g;
    }
    if (paused(g)) poll_cancel_ = true;  // requests unanswered from now on
    cv_.notify_all();
}

ServiceGuards EngineService::guards() const {
    std::lock_guard lock(mutex_);
    return guards_;
}

ServiceStatus EngineService::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

bool EngineService::status(ServiceStatus& out, uint64_t& version) const {
    std::lock_guard lock(mutex_);
    if (status_.version == version) return false;
    out     = status_;
    version = status_.version;
    return true;
}

EngineService::Counts EngineService::takeCounts() {
    std::lock_guard lock(mutex_);
    return std::exchange(counts_, Counts{});
}

template <typename F>
void EngineService::update(F f) {
    std::lock_guard lock(mutex_);
    f(status_);
    ++status_.version;
}

bool EngineService::rest(std::chrono::milliseconds d) {
    std::unique_lock lock(mutex_);
    const auto before = guards_;
    return !cv_.wait_for(lock, d, [&] { return stop_.load() || !(guards_ == before); });
}

void EngineService::run() {
    std::size_t failures = 0;  // the work host unanswered, in a row
    bool said            = false;
    while (!stop_ && !quit_) {
        const auto g = guards();
        if (paused(g)) {
            // Paused: no request taken, the engine idle; it quits after a while, as unasked.
            update([&](ServiceStatus& s) {
                s.state       = ServiceState::Paused;
                s.pause       = g.live_game ? ServicePause::LiveGame : ServicePause::Tournament;
                s.threads_now = 0;
            });
            if (engine_ && (Clock::now() - last_job_ > setup_.idle_quit || !engine_->process.running()))
                engine_.reset();
            (void)rest(std::chrono::milliseconds(500));
            continue;
        }
        if (engine_ && (Clock::now() - last_job_ > setup_.idle_quit || !engine_->process.running())) engine_.reset();
        update([&](ServiceStatus& s) {
            if (s.state == ServiceState::Paused || s.state == ServiceState::Busy) s.state = ServiceState::Waiting;
            s.pause = ServicePause::None;
        });
        // A 429 makes the host wait its minute.
        const auto host =
            setup_.host.substr(setup_.host.find("://") == std::string::npos ? 0 : setup_.host.find("://") + 3);
        const auto host_name = host.substr(0, host.find_first_of(":/"));
        if (HostGate::instance().pausedFor(host_name).count() > 0) {
            (void)HostGate::instance().waitPause(host_name, &stop_);
            continue;
        }
        if (!said) {
            said = true;
            Console::instance().add(
                fmt::format("{}: waiting for work at {} (the provider's secret only)", setup_.engine.name, setup_.host),
                false, "network");
        }
        // The long poll: the secret alone, no token.
        poll_cancel_ = false;
        if (paused(guards())) continue;
        HttpRequest request;
        request.url          = setup_.host + "/api/external-engine/work";
        request.method       = "POST";
        request.content_type = "application/json";
        request.body         = nlohmann::json{{"providerSecret", setup_.secret}}.dump();
        request.timeout_s    = 20;
        request.headers.emplace_back("Accept", "application/json");
        std::string body;
        HttpStream stream;
        stream.on_data = [&](std::string_view data) {
            body.append(data);
            return body.size() < (std::size_t{1} << 20);
        };
        stream.cancel    = &poll_cancel_;
        stream.idle_s    = setup_.poll_idle_s;
        stream.redirects = 0;  // its body holds the secret: to the work host alone
        const auto r     = httpStream(request, stream);
        if (stop_ || quit_) break;
        if (r.error == "cancelled") continue;  // a guard paused it
        const bool whole = r.error.empty();    // an answer cut short is the network's doing, not the API's
        if (whole && (r.status == 204 || r.status == 200)) {
            failures = 0;
            update([](ServiceStatus& s) {
                if (s.state != ServiceState::Offline) return;
                s.state = ServiceState::Waiting;
                s.error.clear();
            });
        }
        if (whole && r.status == 204) continue;
        if (whole && r.status == 200) {
            std::string why;
            const auto work = readEngineWork(body, &why);
            if (!work) {
                update([&](ServiceStatus& s) {
                    s.state = ServiceState::ApiChanged;
                    s.error = why;
                });
                break;
            }
            answer(*work);
            continue;
        }
        if (r.status == 401 || r.status == 403) {
            update([&](ServiceStatus& s) {
                s.state = ServiceState::Refused;
                s.error = fmt::format("{}", r.status);
            });
            break;
        }
        if (r.status == 429) {
            HostGate::instance().tooMany(host_name);
            update([&](ServiceStatus& s) {
                s.state    = ServiceState::Offline;
                s.error    = "429";
                s.retry_at = Clock::now() + HostGate::instance().pausedFor(host_name);
            });
            continue;
        }
        if (whole && r.status != 0 && r.status < 500) {
            // 400, 404, 405…: the work host no longer takes the request as this program makes it.
            update([&](ServiceStatus& s) {
                s.state = ServiceState::ApiChanged;
                s.error = fmt::format("the work host answered {}", r.status);
            });
            break;
        }
        // Offline, or the work host in trouble (5xx): asked again by itself, later and later.
        const auto wait = setup_.retry.empty() ? std::chrono::milliseconds(5000)
                                               : setup_.retry[std::min(failures, setup_.retry.size() - 1)];
        ++failures;
        update([&](ServiceStatus& s) {
            s.state    = ServiceState::Offline;
            s.error    = r.status == 0 || !whole ? r.error : fmt::format("the work host answered {}", r.status);
            s.retry_at = Clock::now() + wait;
        });
        const auto until = Clock::now() + wait;
        while (!stop_ && Clock::now() < until)
            (void)rest(std::min(std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now()),
                                std::chrono::milliseconds(500)));
    }
    engine_.reset();
    update([](ServiceStatus& s) {
        s.threads_now = 0;
        s.job.reset();
    });
    ended_ = !stop_;
}

void EngineService::answer(const EngineWork& w) {
    const auto g     = guards();
    const auto start = Clock::now();
    ServiceJob job;
    job.fen         = workFen(w);
    job.multipv     = std::clamp(w.multipv, 1, 5);
    job.threads     = std::max(1, std::min({w.threads, setup_.max_threads, std::max(1, g.threads)}));
    job.hash        = std::max(1, std::min(w.hash, setup_.max_hash));
    job.movetime    = w.movetime;
    job.limit_depth = w.depth;
    job.limit_nodes = w.nodes;
    job.since       = start;
    job.locked      = workLocked(w, g.locked, g.start_locked);
    // Answered empty: a correspondence game's position, a position that cannot be read (its FEN or a move: nothing of
    // it reaches the engine), a variant it does not serve.
    const bool empty = job.locked || job.fen.empty() || w.variant != "chess" ||
                       (!setup_.engine_id.empty() && w.engine_id != setup_.engine_id);
    update([&](ServiceStatus& s) {
        s.state       = ServiceState::Busy;
        s.job         = job;
        s.threads_now = empty ? 0 : job.threads;
        counts_.requests += 1;  // counted as it comes, with its state: today's requests hold the one being answered
    });
    HttpRequest request;
    request.url          = setup_.host + "/api/external-engine/work/" + urlEncode(w.id);
    request.method       = "POST";
    request.content_type = "text/plain";
    request.timeout_s    = 20;
    const auto finish    = [&](bool counted) {
        last_job_ = Clock::now();
        const auto ms =
            counted ? std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count() : 0;
        update([&](ServiceStatus& s) {
            counts_.ms += ms;
            if (s.state == ServiceState::Busy) s.state = ServiceState::Waiting;
            s.last = job;
            s.job.reset();
            s.threads_now = 0;
        });
    };
    // An empty answer: a correspondence game's position (never analysed), a variant it does not serve.
    const auto answerEmpty = [&] {
        bool sent = false;
        HttpUpload up;
        up.cancel = &stop_;
        up.next   = [&](std::chrono::milliseconds) -> std::optional<std::string> {
            if (sent) return std::nullopt;
            sent = true;
            return std::string("bestmove (none)\n");
        };
        (void)httpUpload(request, up);
    };
    if (empty) {
        answerEmpty();
        finish(false);
        return;
    }
    // The engine: started at the first request, kept between them; started once more when it ended meanwhile (between
    // two requests, or before its readyok: a crash, the system, a machine's connection).
    std::string failed;
    const auto start_engine = [&]() -> std::string {
        auto e = std::make_unique<Engine>();
        std::string why;
        if (!e->process.start(engineLaunch(setup_.engine), why))
            return fmt::format("{} could not start: {}", setup_.engine.name, why);
        const auto send = [&](const std::string& line) {
            (void)e->process.send(line);
            if (setup_.sent) setup_.sent(line);
        };
        const auto timeout = setup_.engine.start_timeout > 0
                                 ? std::chrono::milliseconds(setup_.engine.start_timeout * 1000LL)
                                 : ENGINE_STARTUP_TIME;
        send("uci");
        std::vector<UciOptionInfo> options;
        if (!waitWord(*e, "uciok", timeout, stop_, [&](const std::string& line) {
                if (auto o = parseUciOption(line)) options.push_back(std::move(*o));
            }))
            return fmt::format("{} does not answer uci", setup_.engine.name);
        for (const auto& o : options)
            if (o.type != "button") e->options.push_back(o.name);
        if (!e->has("UCI_Chess960"))
            return fmt::format("{} does not know the Chess960 mode, which Lichess asks for", setup_.engine.name);
        // The library's values, but what the service sets itself.
        static constexpr std::string_view OWN[] = {"Threads",         "Hash",  "MultiPV", "UCI_Chess960",
                                                   "UCI_AnalyseMode", "Ponder"};
        for (const auto& [name, value] : tour::forEngine(setup_.engine.options)) {
            if (!e->has(name) || std::find(std::begin(OWN), std::end(OWN), name) != std::end(OWN)) continue;
            send("setoption name " + name + " value " + value);
        }
        send("setoption name UCI_Chess960 value true");
        if (e->has("UCI_AnalyseMode")) send("setoption name UCI_AnalyseMode value true");
        if (e->has("Ponder")) send("setoption name Ponder value false");
        send("isready");
        if (!waitWord(*e, "readyok", timeout, stop_))
            return fmt::format("{} does not answer isready", setup_.engine.name);
        engine_ = std::move(e);
        session_.clear();
        return {};
    };
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (engine_ && !engine_->process.running()) engine_.reset();
        if (!engine_) failed = start_engine();
        if (stop_) return;  // RookStudio closes, or the service was stopped, while the engine started
        if (!failed.empty()) break;
        // This request's settings.
        auto& e         = *engine_;
        const auto send = [&](const std::string& line) {
            (void)e.process.send(line);
            if (setup_.sent) setup_.sent(line);
        };
        if (w.session != session_) {
            session_ = w.session;
            send("ucinewgame");
        }
        if (e.has("Threads") && e.threads != job.threads)
            send(fmt::format("setoption name Threads value {}", job.threads));
        if (e.has("Hash") && e.hash != job.hash) send(fmt::format("setoption name Hash value {}", job.hash));
        if (e.has("MultiPV") && e.multipv != job.multipv)
            send(fmt::format("setoption name MultiPV value {}", job.multipv));
        e.threads = job.threads;
        e.hash    = job.hash;
        e.multipv = job.multipv;
        send("isready");
        if (waitWord(e, "readyok", std::chrono::seconds(60), stop_)) break;
        if (stop_) return;
        // Ended meanwhile, or hung: ended, and started once more.
        engine_.reset();
        if (attempt == 1) failed = fmt::format("{} does not answer isready", setup_.engine.name);
    }
    if (!failed.empty()) {
        engine_.reset();
        answerEmpty();
        finish(false);
        update([&](ServiceStatus& s) {
            s.state = ServiceState::EngineFailed;
            s.error = failed;
        });
        quit_ = true;
        return;
    }
    auto& e         = *engine_;
    const auto send = [&](const std::string& line) {
        (void)e.process.send(line);
        if (setup_.sent) setup_.sent(line);
    };
    e.lines.clear();
    send(workPosition(w));
    send(workGo(w));
    const auto go_at = Clock::now();
    bool best_sent   = false;
    bool stopping    = false;
    bool died        = false;
    auto stop_by     = Clock::time_point{};
    const auto note  = [&](const std::string& line) {
        // What the card shows: the first line's depth, speed and first move.
        const auto ws = words(line);
        int multipv   = 1;
        for (std::size_t i = 1; i + 1 < ws.size(); ++i)
            if (ws[i] == "multipv") multipv = std::atoi(ws[i + 1].c_str());
        if (multipv != 1) return;
        for (std::size_t i = 1; i + 1 < ws.size(); ++i) {
            if (ws[i] == "depth") job.depth = std::atoi(ws[i + 1].c_str());
            if (ws[i] == "nps") job.nps = std::atoll(ws[i + 1].c_str());
            if (ws[i] == "pv") {
                job.best = ws[i + 1];
                break;
            }
        }
        update([&](ServiceStatus& s) { s.job = job; });
    };
    HttpUpload up;
    up.cancel = &stop_;
    up.next   = [&](std::chrono::milliseconds wait) -> std::optional<std::string> {
        if (best_sent) return std::nullopt;
        // Stopped, paused, or fewer threads left than it takes: the search ends there, its bestmove sent.
        if (!stopping) {
            const auto now_g = guards();
            if (stop_ || paused(now_g) || now_g.threads < job.threads) {
                send("stop");
                stopping = true;
                stop_by  = Clock::now() + setup_.stop_grace;
            }
        }
        std::string out;
        const auto deadline = Clock::now() + wait;
        for (;;) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
            bool ended      = false;
            const auto line = nextLine(
                e, out.empty() ? std::max(left, std::chrono::milliseconds(0)) : std::chrono::milliseconds(0), &ended);
            if (ended) {
                died = true;
                out += "bestmove (none)\n";
                best_sent = true;
                break;
            }
            if (!line) break;
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - go_at).count();
            if (auto taken = lineForLichess(*line, ms)) {
                if (taken->rfind("bestmove", 0) == 0) {
                    out += *taken + "\n";
                    best_sent = true;
                    break;
                }
                note(*taken);
                out += *taken + "\n";
            }
        }
        if (!best_sent && stopping && Clock::now() > stop_by) {
            out += "bestmove (none)\n";  // it does not stop: given up, and started again for the next request
            best_sent = true;
            died      = true;
        }
        return out;
    };
    bool whole   = false;
    const auto r = httpUpload(request, up, &whole);
    (void)r;
    if (!whole && !best_sent) {
        // Lichess closed the answer: the one who asked went away. The engine stops, its bestmove read.
        job.cut = true;
        send("stop");
        if (!waitWord(e, "bestmove", setup_.stop_grace, stop_)) died = true;
    }
    if (died) engine_.reset();
    finish(true);
}

}  // namespace fcg::lichess
