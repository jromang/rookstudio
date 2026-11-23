#include "lichess_board.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <fmt/include/fmt/format.h>

#include "https.hpp"
#include "storage.hpp"

namespace fcg::lichess {

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

std::string textOf(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

int64_t intOf(const nlohmann::json& j, const char* key, int64_t otherwise = 0) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number_integer() ? it->get<int64_t>() : otherwise;
}

bool boolOf(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_boolean() && it->get<bool>();
}

// The API's segment of a game's endpoints.
const char* apiName(PlayApi api) { return api == PlayApi::Bot ? "bot" : "board"; }

std::string refusal(int status, const std::string& error) {
    return status == 0     ? (error.empty() ? std::string("Lichess did not answer") : error)
           : status == 401 ? std::string("Lichess refused the token")
           : status == 403 ? std::string("the token may not do it")
           : status == 404 ? std::string("Lichess does not know it")
           : status == 429 ? std::string("Lichess asks to wait a minute: too many requests")
                           : fmt::format("Lichess answered {}", status);
}

// Lichess's own words, when its answer gives some: {"error": "..."}, or a form's {"global": ["..."]}.
std::string reasonOf(std::string_view body) {
    const auto j = nlohmann::json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return {};
    if (const auto it = j.find("error"); it != j.end()) {
        if (it->is_string()) return it->get<std::string>();
        if (it->is_object())  // {"error": {"field": ["what"]}}
            for (const auto& [field, what] : it->items())
                if (what.is_array() && !what.empty() && what[0].is_string()) return what[0].get<std::string>();
    }
    if (const auto it = j.find("global"); it != j.end() && it->is_array() && !it->empty() && (*it)[0].is_string())
        return (*it)[0].get<std::string>();
    return {};
}

std::string whyNot(int status, std::string_view body, const std::string& error) {
    auto reason = reasonOf(body);
    return reason.empty() ? refusal(status, error) : reason;
}

using Fields = std::vector<std::pair<std::string, std::string>>;

HttpRequest postRequest(const std::string& path, const std::string& token, const Fields& fields, int timeout_s) {
    HttpRequest request;
    request.url       = servers().api + path;
    request.method    = "POST";
    request.token     = token;
    request.timeout_s = timeout_s;
    if (!fields.empty()) {
        request.content_type = "application/x-www-form-urlencoded";
        request.body         = formEncode(fields);
    }
    return request;
}

Reply post(const std::string& path, const std::string& token, const Fields& fields, const std::atomic<bool>* cancel,
           int timeout_s = 15) {
    auto request = postRequest(path, token, fields, timeout_s);
    request.headers.emplace_back("Accept", "application/json");
    const auto r = politeRequest(request, cancel);
    Reply out;
    out.status = r.status;
    out.ok     = r.status == 200;
    if (!out.ok) out.error = whyNot(r.status, r.body, r.error);
    return out;
}

// The fields of a clock: a seek's base in minutes ("time"), a challenge's in seconds ("clock.limit"); or days.
void addClock(Fields& fields, const ChallengeClock& clock, bool seek) {
    if (clock.days > 0) {
        fields.emplace_back("days", std::to_string(clock.days));
    } else if (seek) {
        fields.emplace_back("time", std::to_string(clock.minutes));
        fields.emplace_back("increment", std::to_string(clock.increment));
    } else {
        fields.emplace_back("clock.limit", std::to_string(clock.minutes * 60));
        fields.emplace_back("clock.increment", std::to_string(clock.increment));
    }
}

// The challenge of an answer, wrapped or not ({"challenge": {...}}).
const nlohmann::json* challengeOf(const nlohmann::json& j) {
    if (!j.is_object()) return nullptr;
    if (const auto it = j.find("challenge"); it != j.end() && it->is_object()) return &*it;
    return &j;
}

void sleepFor(std::chrono::milliseconds wait, const std::atomic<bool>& stop) {
    const auto until = Clock::now() + wait;
    while (!stop && Clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

BoardPlayer playerOf(const nlohmann::json& j) {
    BoardPlayer p;
    if (!j.is_object()) return p;
    p.id          = textOf(j, "id");
    p.name        = textOf(j, "name");
    p.title       = textOf(j, "title");
    p.rating      = static_cast<int>(intOf(j, "rating"));
    p.provisional = boolOf(j, "provisional");
    p.ai          = static_cast<int>(intOf(j, "aiLevel"));
    if (p.name.empty() && p.ai > 0) p.name = fmt::format("Stockfish level {}", p.ai);
    return p;
}

void applyState(const nlohmann::json& j, BoardState& s) {
    if (!j.is_object()) return;
    s.moves.clear();
    std::istringstream in(textOf(j, "moves"));
    for (std::string m; in >> m;) s.moves.push_back(m);
    s.white_ms = intOf(j, "wtime", s.white_ms);
    s.black_ms = intOf(j, "btime", s.black_ms);
    s.told     = Clock::now();
    if (auto status = textOf(j, "status"); !status.empty()) s.status = std::move(status);
    s.winner         = textOf(j, "winner");
    s.white_draw     = boolOf(j, "wdraw");
    s.black_draw     = boolOf(j, "bdraw");
    s.white_takeback = boolOf(j, "wtakeback");
    s.black_takeback = boolOf(j, "btakeback");
    s.first_move_ms  = -1;
    if (const auto it = j.find("expiration"); it != j.end() && it->is_object())
        s.first_move_ms = std::max<int64_t>(0, intOf(*it, "millisToMove") - intOf(*it, "idleMillis"));
}

// The side to move first in a game from `fen` (empty: the start position).
bool whiteFirst(const std::string& fen) {
    const auto space = fen.find(' ');
    return space == std::string::npos || space + 1 >= fen.size() || fen[space + 1] != 'b';
}

}  // namespace

// ---------------------------------------------------------------------------
// Clocks

std::string speedOf(const ChallengeClock& clock) {
    if (clock.days > 0) return "correspondence";
    const int64_t total = int64_t{clock.minutes} * 60 + int64_t{40} * clock.increment;
    if (total < 30) return "ultraBullet";
    if (total < 180) return "bullet";
    if (total < 480) return "blitz";
    if (total < 1500) return "rapid";
    return "classical";
}

bool validDays(int days) {
    static constexpr int DAYS[] = {1, 2, 3, 5, 7, 10, 14};
    return std::find(std::begin(DAYS), std::end(DAYS), days) != std::end(DAYS);
}

bool validClock(const ChallengeClock& clock, bool seek) {
    if (clock.days != 0) return validDays(clock.days);
    if (clock.minutes < 0 || clock.minutes > 180 || clock.increment < 0) return false;
    if (clock.increment > (seek ? 180 : 60)) return false;
    return clock.minutes > 0 || clock.increment > 0;
}

bool seekAllowed(const ChallengeClock& clock) {
    if (!validClock(clock, true)) return false;
    const auto speed = speedOf(clock);
    return speed == "rapid" || speed == "classical" || speed == "correspondence";
}

bool challengeAllowed(const ChallengeClock& clock) {
    if (!validClock(clock, false)) return false;
    const auto speed = speedOf(clock);
    return speed == "blitz" || speed == "rapid" || speed == "classical" || speed == "correspondence";
}

std::optional<chess::Move> legalUci(const chess::Board& board, std::string_view uci) {
    std::string wanted(uci);
    for (auto& c : wanted) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    chess::Movelist legal;
    chess::movegen::legalmoves(legal, board);
    for (const auto& m : legal)
        if (chess::uci::moveToUci(m, false) == wanted || chess::uci::moveToUci(m, true) == wanted) return m;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// One request each

Reply boardMove(const std::string& token, const std::string& game, const std::string& uci, bool offer_draw,
                const std::atomic<bool>* cancel, PlayApi api) {
    // A move waits less than the other requests: the clock runs.
    return post(fmt::format("/api/{}/game/{}/move/{}{}", apiName(api), urlEncode(game), urlEncode(uci),
                            offer_draw ? "?offeringDraw=true" : ""),
                token, {}, cancel, 10);
}

Reply boardAction(const std::string& token, const std::string& game, std::string_view action,
                  const std::atomic<bool>* cancel, PlayApi api) {
    return post(fmt::format("/api/{}/game/{}/{}", apiName(api), urlEncode(game), action), token, {}, cancel);
}

Reply boardChat(const std::string& token, const std::string& game, const std::string& text,
                const std::atomic<bool>* cancel, PlayApi api) {
    return post(fmt::format("/api/{}/game/{}/chat", apiName(api), urlEncode(game)), token,
                {{"room", "player"}, {"text", text}}, cancel);
}

Reply acceptChallenge(const std::string& token, const std::string& id, const std::atomic<bool>* cancel) {
    return post(fmt::format("/api/challenge/{}/accept", urlEncode(id)), token, {}, cancel);
}

Reply declineChallenge(const std::string& token, const std::string& id, std::string_view reason,
                       const std::atomic<bool>* cancel) {
    return post(fmt::format("/api/challenge/{}/decline", urlEncode(id)), token, {{"reason", std::string(reason)}},
                cancel);
}

Reply cancelChallenge(const std::string& token, const std::string& id, const std::atomic<bool>* cancel) {
    return post(fmt::format("/api/challenge/{}/cancel", urlEncode(id)), token, {}, cancel);
}

// ---------------------------------------------------------------------------
// A seek

Seek::Seek(std::string token, SeekAsk ask) : token_(std::move(token)), ask_(ask), thread_([this] { run(); }) {}

Seek::~Seek() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

std::string Seek::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}

std::string Seek::id() const {
    std::lock_guard lock(mutex_);
    return id_;
}

Clock::time_point Seek::closedAt() const {
    std::lock_guard lock(mutex_);
    return closed_;
}

void Seek::run() {
    Fields fields = {{"rated", ask_.rated ? "true" : "false"}, {"variant", "standard"}};
    addClock(fields, ask_.clock, true);
    if (ask_.rating_min > 0 && ask_.rating_max > ask_.rating_min)
        fields.emplace_back("ratingRange", fmt::format("{}-{}", ask_.rating_min, ask_.rating_max));
    const auto fail = [&](std::string why) {
        std::lock_guard lock(mutex_);
        error_  = std::move(why);
        closed_ = Clock::now();
        state_  = State::Failed;
    };
    // By correspondence: one request, the seek's id in its answer.
    if (ask_.clock.days > 0) {
        auto request = postRequest("/api/board/seek", token_, fields, 20);
        request.headers.emplace_back("Accept", "application/json");
        const auto r = politeRequest(request, &stop_);
        if (r.status != 200) return fail(whyNot(r.status, r.body, r.error));
        const auto j = nlohmann::json::parse(r.body, nullptr, false);
        std::lock_guard lock(mutex_);
        if (!j.is_discarded() && j.is_object()) id_ = textOf(j, "id");
        closed_ = Clock::now();
        state_  = State::Closed;
        return;
    }
    // In real time: the request held open while the seek lives; Lichess closes it when someone takes it.
    auto request = postRequest("/api/board/seek", token_, fields, 20);
    request.headers.emplace_back("Accept", "application/x-ndjson");
    int status = 0;
    std::string body;
    HttpStream stream;
    stream.on_head = [&](int s, std::string_view) {
        status = s;
        if (s == 200) state_ = State::Open;
        return true;
    };
    stream.on_data = [&](std::string_view data) {
        if (status != 200 && body.size() < 4096) body.append(data);
        return !stop_;
    };
    stream.cancel     = &stop_;
    stream.idle_s     = 60;  // Lichess sends an empty line now and then
    const auto result = politeStream(request, stream);
    if (stop_) {
        std::lock_guard lock(mutex_);
        closed_ = Clock::now();
        state_  = State::Closed;
        return;
    }
    if (status != 200) return fail(whyNot(status, body, result.error));
    if (!result.error.empty()) return fail("the connection to Lichess broke: " + result.error);
    std::lock_guard lock(mutex_);
    closed_ = Clock::now();
    state_  = State::Closed;
}

// ---------------------------------------------------------------------------
// A challenge sent

ChallengeOut::ChallengeOut(std::string token, ChallengeAsk ask)
    : token_(std::move(token)), ask_(std::move(ask)), thread_([this] { run(); }) {}

ChallengeOut::~ChallengeOut() {
    cancel();
    if (thread_.joinable()) thread_.join();
}

std::string ChallengeOut::id() const {
    std::lock_guard lock(mutex_);
    return id_;
}

std::string ChallengeOut::done() const {
    std::lock_guard lock(mutex_);
    return done_;
}

std::string ChallengeOut::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}

void ChallengeOut::cancel() { stop_ = true; }

void ChallengeOut::settle(const std::string& done) {
    {
        std::lock_guard lock(mutex_);
        if (done_.empty()) done_ = done;
        settled_ = true;
    }
    stop_ = true;
}

void ChallengeOut::run() {
    const bool live = ask_.clock.days <= 0;
    Fields fields   = {{"rated", ask_.rated ? "true" : "false"}, {"color", ask_.colour}, {"variant", "standard"}};
    addClock(fields, ask_.clock, false);
    if (live) fields.emplace_back("keepAliveStream", "true");
    const auto path = "/api/challenge/" + urlEncode(ask_.user);
    const auto fail = [&](std::string why) {
        std::lock_guard lock(mutex_);
        error_ = std::move(why);
        state_ = State::Failed;
    };
    const auto take = [&](const nlohmann::json& j) {
        const auto* c = challengeOf(j);
        if (!c) return;
        if (const auto it = j.find("done"); it != j.end() && it->is_string()) {
            std::lock_guard lock(mutex_);
            done_ = it->get<std::string>();
            return;
        }
        if (auto id = textOf(*c, "id"); !id.empty()) {
            std::lock_guard lock(mutex_);
            id_ = std::move(id);
        }
    };
    // Called back by the user: cancelled at Lichess too, once its id is known (a few seconds at most: RookStudio may
    // be closing); settled, nothing more is asked.
    const auto cancelled = [&] {
        const auto id = this->id();
        if (!settled_ && done().empty() && !id.empty())
            (void)post(fmt::format("/api/challenge/{}/cancel", urlEncode(id)), token_, {}, nullptr, 5);
        std::lock_guard lock(mutex_);
        if (done_.empty()) done_ = "canceled";
        state_ = State::Done;
    };
    if (!live) {
        auto request = postRequest(path, token_, fields, 20);
        request.headers.emplace_back("Accept", "application/json");
        const auto r = politeRequest(request, &stop_);
        if (stop_ && r.status == 0) return cancelled();
        if (r.status != 200) return fail(whyNot(r.status, r.body, r.error));
        const auto j = nlohmann::json::parse(r.body, nullptr, false);
        if (!j.is_discarded()) take(j);
        state_ = State::Waiting;
        // Its fate comes on the event stream; called back meanwhile, it is cancelled.
        while (!stop_) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return cancelled();
    }
    auto request = postRequest(path, token_, fields, 20);
    request.headers.emplace_back("Accept", "application/x-ndjson");
    int status = 0;
    std::string body;
    LineSplitter lines([&](std::string_view line) {
        const auto j = nlohmann::json::parse(line, nullptr, false);
        if (!j.is_discarded()) take(j);
        return true;
    });
    HttpStream stream;
    stream.on_head = [&](int s, std::string_view) {
        status = s;
        if (s == 200) state_ = State::Waiting;
        return true;
    };
    stream.on_data = [&](std::string_view data) {
        if (status != 200) {
            if (body.size() < 4096) body.append(data);
            return !stop_;
        }
        return lines.feed(data) && !stop_;
    };
    stream.cancel     = &stop_;
    stream.idle_s     = 60;
    const auto result = politeStream(request, stream);
    if (status == 200) (void)lines.flush();
    if (stop_ && (settled_ || done().empty())) return cancelled();
    if (status != 200) return fail(whyNot(status, body, result.error));
    if (done().empty()) {
        // The request broke before Lichess said anything: the challenge expires on its own; called back to be sure.
        const auto id = this->id();
        if (!id.empty()) (void)post(fmt::format("/api/challenge/{}/cancel", urlEncode(id)), token_, {}, nullptr, 5);
        return fail(result.error.empty() ? std::string("Lichess closed the challenge")
                                         : "the connection to Lichess broke: " + result.error);
    }
    state_ = State::Done;
}

// ---------------------------------------------------------------------------
// A game's stream

Answer<std::vector<ChatLine>> boardChatLines(const std::string& token, const std::string& game,
                                             const std::atomic<bool>* cancel, PlayApi api) {
    Answer<std::vector<ChatLine>> out;
    HttpRequest request;
    request.url    = fmt::format("{}/api/{}/game/{}/chat", servers().api, apiName(api), urlEncode(game));
    request.method = "GET";
    request.token  = token;
    request.headers.emplace_back("Accept", "application/json");
    const auto r = politeRequest(request, cancel);
    out.status   = r.status;
    if (r.status != 200) {
        out.error = whyNot(r.status, r.body, r.error);
        return out;
    }
    const auto j = nlohmann::json::parse(r.body, nullptr, false);
    if (j.is_discarded() || !j.is_array()) {
        out.error = "Lichess's answer could not be read";
        return out;
    }
    std::vector<ChatLine> lines;
    for (const auto& line : j)
        if (line.is_object()) lines.push_back({"player", textOf(line, "user"), textOf(line, "text")});
    out.value = std::move(lines);
    return out;
}

int64_t BoardState::clockNow(bool white, Clock::time_point now) const {
    int64_t ms = white ? white_ms : black_ms;
    if (over() || status == "created") return ms;
    const bool white_to_move = whiteFirst(initial_fen) == (moves.size() % 2 == 0);
    // In real time, the clocks run once each side has moved; by correspondence, the time to move from the start.
    if (white_to_move != white || (live() && moves.size() < 2)) return ms;
    ms -= std::chrono::duration_cast<std::chrono::milliseconds>(now - told).count();
    return std::max<int64_t>(0, ms);
}

bool readBoardLine(std::string_view line, BoardState& s) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
    if (line.empty()) return false;
    const auto j = nlohmann::json::parse(line, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;
    const auto type = textOf(j, "type");
    if (type == "gameFull") {
        s.full = true;
        if (auto id = textOf(j, "id"); !id.empty()) s.id = std::move(id);
        if (const auto it = j.find("variant"); it != j.end() && it->is_object()) s.variant = textOf(*it, "key");
        if (s.variant.empty()) s.variant = "standard";
        s.speed = textOf(j, "speed");
        if (const auto it = j.find("perf"); it != j.end() && it->is_object()) s.perf = textOf(*it, "name");
        s.rated = boolOf(j, "rated");
        s.white = playerOf(j.value("white", nlohmann::json()));
        s.black = playerOf(j.value("black", nlohmann::json()));
        s.initial_fen.clear();
        if (auto fen = textOf(j, "initialFen"); fen != "startpos") s.initial_fen = std::move(fen);
        s.clock_ms     = -1;
        s.increment_ms = 0;
        if (const auto it = j.find("clock"); it != j.end() && it->is_object()) {
            s.clock_ms     = intOf(*it, "initial", -1);
            s.increment_ms = intOf(*it, "increment");
        }
        s.days = static_cast<int>(intOf(j, "daysPerTurn"));
        if (const auto it = j.find("state"); it != j.end()) applyState(*it, s);
    } else if (type == "gameState") {
        applyState(j, s);
    } else if (type == "chatLine") {
        s.chat.push_back({textOf(j, "room"), textOf(j, "username"), textOf(j, "text")});
    } else if (type == "opponentGone") {
        s.opponent_gone = boolOf(j, "gone");
        s.claim_in_s    = s.opponent_gone ? intOf(j, "claimWinInSeconds", -1) : -1;
        s.gone_told     = Clock::now();
    } else {
        return false;
    }
    ++s.version;
    return true;
}

ReplayGame replayOf(const BoardState& s) {
    ReplayGame g;
    const bool chess960 = s.variant == "chess960";
    g.start_fen         = s.initial_fen.empty() ? std::string(chess::constants::STARTPOS) : s.initial_fen;
    const auto add      = [&](const char* key, const std::string& value) {
        if (!value.empty()) g.headers.emplace_back(key, value);
    };
    // As Lichess's own export writes them.
    const auto speed = s.speed.empty() ? std::string("game") : s.speed;
    add("Event", fmt::format("{} {} game", s.rated ? "Rated" : "Casual", speed));
    add("Site", s.id.empty() ? std::string() : fmt::format("https://lichess.org/{}", s.id));
    add("White", s.white.name.empty() ? std::string("?") : s.white.name);
    add("Black", s.black.name.empty() ? std::string("?") : s.black.name);
    if (s.white.rating > 0) add("WhiteElo", std::to_string(s.white.rating));
    if (s.black.rating > 0) add("BlackElo", std::to_string(s.black.rating));
    add("WhiteTitle", s.white.title);
    add("BlackTitle", s.black.title);
    if (s.clock_ms >= 0)
        add("TimeControl", fmt::format("{}+{}", s.clock_ms / 1000, s.increment_ms / 1000));
    else if (s.days > 0 || s.speed == "correspondence")
        add("TimeControl", "-");
    if (chess960) add("Variant", "Chess960");
    chess::Board board(g.start_fen, chess960);
    for (const auto& uci : s.moves) {
        const auto move = legalUci(board, uci);
        if (!move) break;
        ReplayMove m;
        m.san   = chess::uci::moveToSan(board, *move);
        m.move  = *move;
        m.mover = board.sideToMove();
        board.makeMove(*move);
        g.moves.push_back(std::move(m));
    }
    if (s.over()) {
        g.result = s.winner == "white"                                                               ? "1-0"
                   : s.winner == "black"                                                             ? "0-1"
                   : (s.status == "aborted" || s.status == "noStart" || s.status == "unknownFinish") ? "*"
                                                                                                     : "1/2-1/2";
    } else {
        g.result = "*";
    }
    add("Result", g.result);
    return g;
}

GameStream::GameStream(std::string token, std::string id, PlayApi api)
    : token_(std::move(token)), id_(std::move(id)), api_(api) {
    state_.id = id_;
    thread_   = std::thread([this] { run(); });
}

GameStream::~GameStream() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

bool GameStream::state(BoardState& out, uint64_t& version) const {
    std::lock_guard lock(mutex_);
    if (state_.version == version) return false;
    out     = state_;
    version = state_.version;
    return true;
}

template <typename F>
void GameStream::update(F f) {
    std::lock_guard lock(mutex_);
    f(state_);
    ++state_.version;
}

void GameStream::run() {
    int breaks = 0;  // in a row
    while (!stop_) {
        // The players' chat so far: what the stream says after it opens is added to it.
        if (auto chat = boardChatLines(token_, id_, &stop_, api_); chat.value)
            update([&](BoardState& st) { st.chat = std::move(*chat.value); });
        HttpRequest request;
        request.url    = fmt::format("{}/api/{}/game/stream/{}", servers().api, apiName(api_), urlEncode(id_));
        request.method = "GET";
        request.token  = token_;
        request.headers.emplace_back("Accept", "application/x-ndjson");
        int status = 0;
        LineSplitter lines([&](std::string_view line) {
            std::lock_guard lock(mutex_);
            readBoardLine(line, state_);
            return true;
        });
        HttpStream stream;
        stream.on_head = [&](int s, std::string_view) {
            status = s;
            if (s == 200) {
                update([](BoardState& st) {
                    st.connected = true;
                    st.tries     = 0;
                    st.error.clear();
                });
                breaks = 0;
            }
            return s == 200;
        };
        stream.on_data    = [&](std::string_view data) { return lines.feed(data) && !stop_; };
        stream.cancel     = &stop_;
        stream.idle_s     = 30;  // Lichess sends an empty line now and then
        const auto result = politeStream(request, stream);
        if (status == 200) (void)lines.flush();
        if (stop_) break;
        bool ended = false;
        update([&](BoardState& st) {
            st.connected = false;
            if (status == 404) {
                st.ended = true;
                st.error = "Lichess does not know this game";
            } else if (status == 401 || status == 403) {
                st.ended = true;
                st.error = "Lichess refused the token";
            } else if (st.over()) {
                st.ended = true;
            } else {
                st.error = status != 200 && status != 0 ? fmt::format("Lichess answered {}", status)
                           : result.error.empty()       ? std::string("the stream ended")
                                                        : result.error;
            }
            ended = st.ended;
        });
        if (ended) break;
        // Tried again after 1 s, 2 s, 3 s, then every 5 s: the clock runs on Lichess meanwhile.
        static constexpr int WAIT_MS[] = {1000, 2000, 3000, 5000};
        const int wait                 = WAIT_MS[std::min(breaks, 3)];
        ++breaks;
        update([&](BoardState& st) {
            st.tries    = breaks;
            st.retry_at = Clock::now() + std::chrono::milliseconds(wait);
        });
        sleepFor(std::chrono::milliseconds(wait), stop_);
    }
}

// ---------------------------------------------------------------------------
// The profile

OnlinePlaySettings readOnlinePlay(const fs::path& folder) {
    std::ifstream in(folder / "lichess.json", std::ios::binary);
    if (!in) return {};
    const auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return {};
    try {
        return j.get<OnlinePlaySettings>();
    } catch (const std::exception&) {
        return {};
    }
}

bool writeOnlinePlay(const fs::path& folder, const OnlinePlaySettings& settings) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    return writeJson(folder / "lichess.json", nlohmann::json(settings));
}

}  // namespace fcg::lichess
