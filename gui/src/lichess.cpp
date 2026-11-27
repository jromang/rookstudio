#include "lichess.hpp"

#include <ctime>
#include <set>

#include <tour/pgn_file.hpp>
#include <tour/util.hpp>

#include <fmt/include/fmt/format.h>

#include "console.hpp"
#include "i18n.hpp"
#include "storage.hpp"

namespace fcg {

namespace {

constexpr auto PUSH_EVERY = std::chrono::seconds(8);

std::string headers(const std::string& event, uint32_t round, const std::string& white, const std::string& black,
                    int white_elo, int black_elo, const std::string& result, const std::string& fen, bool frc) {
    const auto tag = [](const char* name, const std::string& value) {
        return fmt::format("[{} \"{}\"]\n", name, tour::pgnTagValue(value));
    };
    std::string out = tag("Event", event) + tag("Site", "RookStudio") + tag("Round", std::to_string(round)) +
                      tag("White", white) + tag("Black", black) + tag("Result", result);
    if (white_elo > 0) out += tag("WhiteElo", std::to_string(white_elo));
    if (black_elo > 0) out += tag("BlackElo", std::to_string(black_elo));
    if (frc) out += tag("Variant", "Chess960");
    if (!fen.empty()) out += tag("SetUp", "1") + tag("FEN", fen);
    return out;
}

std::string key(uint32_t round, const std::string& white, const std::string& black) {
    return fmt::format("{}|{}|{}", round, white, black);
}

std::string clock(int64_t ms) {
    const auto s = std::max<int64_t>(0, ms) / 1000;
    return fmt::format("[%clk {}:{:02}:{:02}]", s / 3600, s / 60 % 60, s % 60);
}

constexpr std::string_view START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

std::string now() {
    const auto t = std::time(nullptr);
    const auto tm = localTime(t);
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &tm);
    return buffer;
}

}  // namespace

std::string LichessGame::pgn(std::size_t annotate_from) const {
    std::string out = headers + "\n";
    bool white      = white_first;
    int number      = first_number;
    for (std::size_t i = 0; i < moves.size(); ++i) {
        if (white) {
            out += fmt::format("{}. ", number);
        } else if (i == 0) {
            out += fmt::format("{}... ", number);
        }
        out += moves[i].san;
        if (i >= annotate_from && !moves[i].comment.empty()) out += " {" + moves[i].comment + "}";
        out += ' ';
        if (!white) number++;
        white = !white;
    }
    return out + result + "\n\n";
}

LichessGame lichessLiveGame(const LiveGame& g, const std::string& event, int white_elo, int black_elo, bool frc) {
    // The game number is 1-based; its encounter, the round of its PGN, too.
    const auto round = static_cast<uint32_t>((std::max<uint64_t>(1, g.number) - 1) / 2 + 1);
    LichessGame game;
    game.key          = key(round, g.white, g.black);
    game.headers      = headers(event, round, g.white, g.black, white_elo, black_elo, "*",
                                frc || g.start_fen != START ? g.start_fen : "", frc);
    const auto space  = g.start_fen.find(' ');
    game.white_first  = space == std::string::npos || g.start_fen[space + 1] == 'w';
    const auto last   = g.start_fen.rfind(' ');
    game.first_number = last == std::string::npos ? 1 : std::max(1, std::atoi(g.start_fen.c_str() + last + 1));

    bool white   = game.white_first;
    const auto n = g.san.size();
    for (std::size_t i = 0; i < n; ++i) {
        LichessGame::Move m{g.san[i], {}};
        // The last move of each side is one of the last two: its clock is the side's clock now.
        if (g.clocks && i + 2 >= n) {
            m.comment = clock(white ? g.white_ms : g.black_ms);
        }
        game.moves.push_back(std::move(m));
        white = !white;
    }
    return game;
}

LichessGame lichessFinishedGame(std::string_view pgn, const std::string& event, uint32_t round,
                                const std::string& white, const std::string& black, int white_elo, int black_elo,
                                const std::string& result, bool frc) {
    LichessGame game;
    game.key      = key(round, white, black);
    game.result   = result;
    game.finished = true;

    std::string fen;
    if (const auto at = pgn.find("[FEN \""); at != std::string_view::npos) {
        const auto end = pgn.find("\"]", at + 6);
        if (end != std::string_view::npos) fen = std::string(pgn.substr(at + 6, end - at - 6));
    }
    game.headers = headers(event, round, white, black, white_elo, black_elo, result, fen, frc);
    if (!fen.empty()) {
        const auto space  = fen.find(' ');
        game.white_first  = space == std::string::npos || fen[space + 1] == 'w';
        const auto last   = fen.rfind(' ');
        game.first_number = last == std::string::npos ? 1 : std::max(1, std::atoi(fen.c_str() + last + 1));
    }

    // The moves; from each comment, the clock (without milliseconds): the one thing of a comment lichess keeps in a
    // broadcast, which drops the evaluations and the text.
    auto body           = pgn.find("\n\n");
    body                = body == std::string_view::npos ? pgn.size() : body + 2;
    const auto movetext = pgn.substr(body);
    const auto command  = [](std::string_view comment, std::string_view name) -> std::string {
        const auto at = comment.find(name);
        if (at == std::string_view::npos) return {};
        const auto start = at + name.size();
        const auto value = comment.substr(start, comment.find(']', start) - start);
        return std::string(value.substr(0, value.find('.')));  // "0:00:59.120": the milliseconds left out
    };
    for (std::size_t i = 0; i < movetext.size();) {
        const char c = movetext[i];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            ++i;
        } else if (c == '{') {
            const auto end     = movetext.find('}', i);
            const auto comment = movetext.substr(i, end == std::string_view::npos ? std::string_view::npos : end - i);
            if (const auto clock = command(comment, "[%clk "); !game.moves.empty() && !clock.empty()) {
                game.moves.back().comment = "[%clk " + clock + "]";
            }
            i = end == std::string_view::npos ? movetext.size() : end + 1;
        } else {
            auto end = movetext.find_first_of(" \n\r\t{", i);
            if (end == std::string_view::npos) end = movetext.size();
            const auto token  = movetext.substr(i, end - i);
            const bool number = std::isdigit(static_cast<unsigned char>(token[0])) && token.back() == '.';
            const bool over   = token == "1-0" || token == "0-1" || token == "1/2-1/2" || token == "*";
            if (!number && !over) game.moves.push_back({std::string(token), {}});
            i = end;
        }
    }
    return game;
}

std::optional<std::string> lichessRoundId(std::string_view text) {
    while (!text.empty() && (text.back() == '/' || text.back() == ' ')) text.remove_suffix(1);
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    const auto slash = text.rfind('/');
    const auto id    = slash == std::string_view::npos ? text : text.substr(slash + 1);
    if (id.size() != 8) return std::nullopt;
    for (const char c : id) {
        if (!std::isalnum(static_cast<unsigned char>(c))) return std::nullopt;
    }
    return std::string(id);
}

struct LichessRelay::Session {
    void loop();
    bool prepare();  // the broadcast and its first round, made or read; false when it cannot be
    bool newRound();
    void push();
    // Sent at once, not in the host's turns; a 429 pauses the host (HostGate), which the loop waits for. In the log
    // unless not `logged` (a push: said once for each round).
    lichess::Response post(const std::string& path, const std::string& type, const std::string& body,
                           bool logged = true);
    void save();
    void fail(LichessRelay::Step step, const lichess::Failure& why);  // said by the status

    Settings settings_;
    std::string host_;  // settings_.base's, as HostGate names it
    LichessTarget target_;
    std::atomic<bool> running_{true};
    std::set<std::string> pushed_to_;  // the rounds whose pushes the log has said

    std::mutex mutex_;  // what follows
    std::condition_variable wake_, ended_;
    bool stopping_ = false;
    bool done_     = false;  // the thread has ended
    std::vector<LichessGame> live_;
    std::map<std::string, LichessGame> pending_;  // finished games not pushed yet
    Status status_;
};

LichessRelay::~LichessRelay() {
    stop();
    // The session holds all the thread needs: detached, it ends on its own.
    if (!wait(std::chrono::seconds(10))) thread_.detach();
}

void LichessRelay::start(Settings settings) {
    stop();
    auto session       = std::make_shared<Session>();
    session->settings_ = std::move(settings);
    session_           = session;
    // The previous start's last push, then this one: one at a time writes lichess.json.
    thread_ = std::thread([session, previous = std::move(thread_)]() mutable {
        if (previous.joinable()) previous.join();
        session->loop();
        std::lock_guard<std::mutex> lock(session->mutex_);
        session->done_ = true;
        session->ended_.notify_all();
    });
}

void LichessRelay::stop() {
    if (!session_) return;
    session_->running_ = false;
    std::lock_guard<std::mutex> lock(session_->mutex_);
    session_->stopping_       = true;
    session_->status_.running = false;
    session_->wake_.notify_all();
}

bool LichessRelay::wait(std::chrono::milliseconds timeout) {
    if (!thread_.joinable()) return true;
    {
        std::unique_lock<std::mutex> lock(session_->mutex_);
        if (!session_->ended_.wait_for(lock, timeout, [&] { return session_->done_; })) return false;
    }
    thread_.join();
    return true;
}

bool LichessRelay::running() const noexcept { return session_ && session_->running_; }

LichessRelay::Status LichessRelay::status() const {
    if (!session_) return {};
    std::lock_guard<std::mutex> lock(session_->mutex_);
    return session_->status_;
}

void LichessRelay::offer(std::vector<LichessGame> live, std::vector<LichessGame> finished) {
    if (!session_) return;
    std::lock_guard<std::mutex> lock(session_->mutex_);
    session_->live_ = std::move(live);
    for (auto& g : finished) session_->pending_[g.key] = std::move(g);
}

lichess::Response LichessRelay::Session::post(const std::string& path, const std::string& type, const std::string& body,
                                              bool logged) {
    auto asked         = lichess::request("POST", settings_.base + path, settings_.token);
    asked.content_type = type;
    asked.body         = body;
    asked.logged       = logged;
    auto response      = lichess::responseOf(asked, httpRequest(asked));
    if (response.net == lichess::NetError::RateLimited) HostGate::instance().tooMany(host_);
    return response;
}

void LichessRelay::Session::save() { writeJson(settings_.dir / "lichess.json", target_); }

void LichessRelay::Session::fail(LichessRelay::Step step, const lichess::Failure& why) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.failed = step;
    status_.error  = why;
}

bool LichessRelay::Session::newRound() {
    const auto name = fmt::format("Part {}", target_.round_index);
    const auto made = lichess::readAnswer<std::pair<std::string, std::string>>(
        post("/broadcast/" + target_.tour_id + "/new", "application/x-www-form-urlencoded",
             formEncode({{"name", name}})),
        [&](const nlohmann::json& j) {
            const auto id = j.at("round").at("id").get<std::string>();
            return std::pair{id, j.at("round").value("url", settings_.base + "/broadcast/-/-/" + id)};
        });
    if (!made.value) {
        fail(Step::Round, made);
        return false;
    }
    target_.round_id = made.value->first;
    target_.url      = made.value->second;
    save();
    return true;
}

bool LichessRelay::Session::prepare() {
    // A round given by its URL or id; otherwise the one made earlier for this tournament, or a new broadcast.
    if (auto saved = readJson<nlohmann::json>(settings_.dir / "lichess.json")) {
        try {
            target_ = saved->get<LichessTarget>();
        } catch (...) {
            target_ = {};
        }
    }
    if (!settings_.round.empty()) {
        const auto id = lichessRoundId(settings_.round);
        if (!id) {
            fail(Step::Round, lichess::Failure::own("not a broadcast round URL or id: " + settings_.round));
            return false;
        }
        if (target_.round_id != *id) {
            target_          = {};
            target_.round_id = *id;
            target_.url = settings_.round.find('/') != std::string::npos ? settings_.round
                                                                         : settings_.base + "/broadcast/-/-/" + *id;
            save();
        }
    } else if (target_.round_id.empty() || target_.tour_id.empty()) {
        // 80 bytes at most, never cut inside a UTF-8 character.
        std::size_t cut = std::min<std::size_t>(settings_.name.size(), 80);
        while (cut > 0 && cut < settings_.name.size() && (settings_.name[cut] & 0xC0) == 0x80) --cut;
        auto name = settings_.name.substr(0, cut);
        while (name.size() < 3) name += '.';
        const auto tour = lichess::readAnswer<std::string>(
            post("/broadcast/new", "application/x-www-form-urlencoded",
                 formEncode({{"name", name},
                             {"visibility", settings_.listed ? "public" : "unlisted"},
                             {"markdown", settings_.description.empty()
                                              ? std::string("Engine games played with RookStudio.")
                                              : settings_.description + "\n\nEngine games played with RookStudio."}})),
            [](const nlohmann::json& j) -> std::optional<std::string> {
                auto id = j.at("tour").at("id").get<std::string>();
                if (id.empty()) return std::nullopt;
                return id;
            });
        if (!tour.value) {
            fail(Step::Broadcast, tour);
            return false;
        }
        target_         = {};
        target_.tour_id = *tour.value;
        if (!newRound()) return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    status_.url     = target_.url;
    status_.running = !stopping_;
    status_.games   = target_.rounds.size();
    return true;
}

void LichessRelay::Session::push() {
    std::vector<LichessGame> games;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& g : live_) {
            if (!target_.finished.count(g.key) && !pending_.count(g.key)) games.push_back(g);
        }
        for (const auto& [k, g] : pending_) {
            if (!target_.finished.count(k)) games.push_back(g);
        }
    }
    if (games.empty()) return;

    // Each game goes to the round it was first pushed to; new ones to the current round, or to a new one when it
    // is full (when the broadcast was made here).
    std::map<std::string, std::vector<const LichessGame*>> by_round;
    std::size_t in_round = 0;
    for (const auto& [k, r] : target_.rounds) in_round += r == target_.round_id;
    for (const auto& g : games) {
        auto it = target_.rounds.find(g.key);
        if (it == target_.rounds.end()) {
            if (in_round >= LICHESS_ROUND_GAMES && !target_.tour_id.empty()) {
                target_.round_index++;
                if (!newRound()) return;
                in_round = 0;
            }
            it = target_.rounds.emplace(g.key, target_.round_id).first;
            in_round++;
        }
        by_round[it->second].push_back(&g);
    }

    lichess::Failure failure;
    for (const auto& [round, list] : by_round) {
        std::string body;
        for (const auto* g : list) body += g->pgn(target_.annotated[g->key]);
        const auto path = "/api/broadcast/round/" + round + "/push";
        // What leaves the machine (R16), said once for each round: a push goes every few seconds.
        if (pushed_to_.insert(round).second) {
            Console::instance().add(
                fmt::format("POST {}{} (with the account's token), then at most every {} s while games are played",
                            settings_.base, path, PUSH_EVERY.count()),
                false, "network");
        }
        const auto r = post(path, "text/plain", body, false);
        if (r.net == lichess::NetError::RateLimited) {  // the host's pause, which the loop waits for
            failure = r;
            break;
        }
        if (r.failed()) {
            failure = r;
            continue;
        }
        // The games lichess could not read say why, in its words.
        try {
            for (const auto& g : nlohmann::json::parse(r.body).value("games", nlohmann::json::array())) {
                if (auto said = lichess::textOf(g, "error"); !said.empty()) {
                    failure        = {};
                    failure.net    = lichess::NetError::Server;
                    failure.status = r.status;
                    failure.said   = std::move(said);
                }
            }
        } catch (...) {
        }
        for (const auto* g : list) {
            if (g->finished) target_.finished.insert(g->key);
            auto& annotated = target_.annotated[g->key];
            annotated       = std::max(annotated, g->moves.size());
        }
    }
    save();

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& k : target_.finished) pending_.erase(k);
    status_.failed    = failure.failed() ? Step::Push : Step::None;
    status_.error     = failure;
    status_.last_push = now();
    status_.games     = target_.rounds.size();
    status_.url       = target_.url;
}

void LichessRelay::Session::loop() {
    host_ = urlHost(settings_.base);
    if (!prepare()) {
        running_ = false;
        return;
    }
    // After a 429, the pushes wait for the host's pause: HostGate's, which the window's other requests to Lichess share
    // when the relay runs in the window's process; a tournament process's own otherwise. The last push, at the stop,
    // goes all the same: it carries the results.
    const auto paused = [&] { return HostGate::instance().pausedFor(host_); };
    auto next         = std::chrono::steady_clock::now();
    while (true) {
        bool last = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_until(lock, std::max(next, std::chrono::steady_clock::now() + paused()),
                             [&] { return stopping_; });
            last = stopping_;
        }
        if (!last && paused().count() > 0) continue;
        push();
        if (last) break;
        next = std::chrono::steady_clock::now() + PUSH_EVERY;
    }
    running_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    status_.running = false;
}

std::string relayError(const LichessRelay::Status& status) {
    if (!status.error.failed()) return {};
    const auto why = lichess::describe(status.error);
    if (status.error.net == lichess::NetError::None) return trf("Lichess: {}", why);  // the program's own words
    switch (status.failed) {
        case LichessRelay::Step::Broadcast:
            return trf("The broadcast could not be made on Lichess: {}", why);
        case LichessRelay::Step::Round:
            return trf("A round could not be made on Lichess: {}", why);
        case LichessRelay::Step::Push:
        case LichessRelay::Step::None:
            break;
    }
    return trf("The games could not be pushed to Lichess: {}", why);
}

}  // namespace fcg
