#include "lichess_players.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>

#include <fmt/include/fmt/format.h>
#include <chess.hpp>
#include <study/game_tree.hpp>

#include "https.hpp"
#include "lichess_links.hpp"
#include "pgn_store.hpp"

namespace fcg::lichess {

namespace {

std::string textOf(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// The placement of a FEN: what tells two positions of a game apart here (the stream's FEN may lack the rest).
std::string_view placementOf(std::string_view fen) { return fen.substr(0, fen.find(' ')); }

std::string tagOf(const std::string& pgn, std::string_view tag) {
    const auto key = fmt::format("[{} \"", tag);
    const auto at  = pgn.find(key);
    if (at == std::string::npos) return {};
    const auto start = at + key.size();
    const auto end   = pgn.find('"', start);
    return end == std::string::npos ? std::string() : pgn.substr(start, end - start);
}

std::string clockText(int64_t seconds) {
    seconds = std::max<int64_t>(0, seconds);
    return fmt::format("{}:{:02}:{:02}", seconds / 3600, seconds % 3600 / 60, seconds % 60);
}

// Sleeps `seconds`, or less when `stop` is set.
void rest(const std::atomic<bool>& stop, int seconds) {
    for (int i = 0; i < seconds * 4 && !stop; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(250));
}

}  // namespace

std::vector<PlayerStatus> readStatuses(const nlohmann::json& j) {
    std::vector<PlayerStatus> out;
    if (!j.is_array()) return out;
    for (const auto& u : j) {
        if (!u.is_object()) continue;
        PlayerStatus s;
        s.name = textOf(u, "name");
        s.id   = lower(textOf(u, "id"));
        if (s.id.empty()) s.id = lower(s.name);
        if (s.id.empty()) continue;
        if (s.name.empty()) s.name = s.id;
        if (const auto it = u.find("online"); it != u.end() && it->is_boolean()) s.online = it->get<bool>();
        if (const auto it = u.find("playing"); it != u.end()) {
            if (it->is_boolean()) {
                s.playing = it->get<bool>();
            } else if (it->is_object()) {  // withGameMetas
                s.playing = true;
                s.game    = textOf(*it, "id");
                s.clock   = textOf(*it, "clock");
                if (const auto c = it->find("clock"); s.clock.empty() && c != it->end() && c->is_object()) {
                    const auto initial = c->value("initial", int64_t{0}), increment = c->value("increment", int64_t{0});
                    s.clock = initial % 60 == 0 ? fmt::format("{}+{}", initial / 60, increment)
                                                : fmt::format("{}s+{}", initial, increment);
                }
            }
        }
        if (s.game.empty()) s.game = textOf(u, "playingId");
        if (!s.game.empty()) s.playing = true;
        out.push_back(std::move(s));
    }
    return out;
}

Answer<std::vector<PlayerStatus>> playerStatuses(const std::vector<std::string>& names,
                                                 const std::atomic<bool>* cancel) {
    Answer<std::vector<PlayerStatus>> a;
    std::vector<PlayerStatus> all;
    for (std::size_t from = 0; from < names.size(); from += 100) {
        std::string ids;
        for (std::size_t i = from; i < std::min(names.size(), from + 100); ++i)
            ids += (ids.empty() ? "" : ",") + names[i];
        HttpRequest request;
        request.url    = fmt::format("{}/api/users/status?ids={}&withGameMetas=true", servers().api, ids);
        request.method = "GET";
        request.headers.emplace_back("Accept", "application/json");
        const auto r = politeRequest(request, cancel);
        a.status     = r.status;
        if (r.status != 200) {
            a.error = r.status == 0     ? (r.error.empty() ? std::string("Lichess did not answer") : r.error)
                      : r.status == 429 ? std::string("Lichess asks to wait a minute: too many requests")
                                        : fmt::format("Lichess answered {}", r.status);
            return a;
        }
        try {
            for (auto& s : readStatuses(nlohmann::json::parse(r.body))) all.push_back(std::move(s));
        } catch (const std::exception&) {
            a.error = "Lichess's answer could not be read";
            return a;
        }
    }
    a.value = std::move(all);
    return a;
}

std::vector<std::string> readFollowing(std::string_view ndjson) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at < ndjson.size()) {
        auto end = ndjson.find('\n', at);
        if (end == std::string_view::npos) end = ndjson.size();
        const auto line = ndjson.substr(at, end - at);
        at              = end + 1;
        if (line.find_first_not_of(" \r\t") == std::string_view::npos) continue;
        try {
            const auto j = nlohmann::json::parse(line);
            auto name    = textOf(j, "username");
            if (name.empty()) name = textOf(j, "name");
            if (name.empty()) name = textOf(j, "id");
            if (!name.empty()) out.push_back(std::move(name));
        } catch (const std::exception&) {
        }
    }
    return out;
}

Answer<std::vector<std::string>> followingOf(const std::string& token, const std::atomic<bool>* cancel) {
    Answer<std::vector<std::string>> a;
    HttpRequest request;
    request.url    = servers().api + "/api/rel/following";
    request.method = "GET";
    request.token  = token;
    request.headers.emplace_back("Accept", "application/x-ndjson");
    const auto r = politeRequest(request, cancel);
    a.status     = r.status;
    if (r.status != 200) {
        a.error = r.status == 0 ? (r.error.empty() ? std::string("Lichess did not answer") : r.error)
                  : r.status == 401 || r.status == 403 ? std::string("Lichess refused the token")
                                                       : fmt::format("Lichess answered {}", r.status);
        return a;
    }
    a.value = readFollowing(r.body);
    return a;
}

bool addStreamMove(std::string& pgn, const nlohmann::json& event) {
    const auto fen = textOf(event, "fen");
    if (fen.empty()) return true;  // nothing to add
    const auto replay = parseReplay(pgn);
    if (!replay) return false;
    auto board = boardAt(*replay, false, static_cast<int>(replay->moves.size()));
    if (placementOf(board.getFen()) == placementOf(fen)) return true;  // it has it
    const auto lm = textOf(event, "lm");
    if (lm.size() < 4) return false;
    const auto move = chess::uci::uciToMove(board, lm);
    chess::Movelist legal;
    chess::movegen::legalmoves(legal, board);
    if (std::find(legal.begin(), legal.end(), move) == legal.end()) return false;
    const auto san   = chess::uci::moveToSan(board, move);
    const bool white = board.sideToMove() == chess::Color::WHITE;
    const int number = board.fullMoveNumber();
    board.makeMove(move);
    if (placementOf(board.getFen()) != placementOf(fen)) return false;
    std::string clock;
    if (const auto it = event.find(white ? "wc" : "bc"); it != event.end() && it->is_number())
        clock = fmt::format(" {{ [%clk {}] }}", clockText(it->get<int64_t>()));
    // In the place of the result: the move, then the result again.
    std::string result = "*";
    if (const auto last = pgn.find_last_not_of(" \t\r\n"); last != std::string::npos) {
        const auto before      = pgn.find_last_of(" \t\r\n", last);
        const std::size_t from = before == std::string::npos ? 0 : before + 1;
        const auto token       = pgn.substr(from, last + 1 - from);
        if (token == "*" || token == "1-0" || token == "0-1" || token == "1/2-1/2") {
            result = token;
            pgn.erase(from);
        } else {
            pgn.erase(last + 1);
        }
    }
    if (!pgn.empty() && pgn.back() != ' ' && pgn.back() != '\n') pgn += ' ';
    pgn += fmt::format("{}{} {}{} {}\n", number, white ? "." : "...", san, clock, result);
    return true;
}

bool streamEnded(const nlohmann::json& event) {
    const auto it = event.find("status");
    if (it == event.end()) return false;
    const auto name = it->is_object() ? textOf(*it, "name") : it->is_string() ? it->get<std::string>() : std::string();
    return !name.empty() && name != "started" && name != "created";
}

PlayersFollower::PlayersFollower(std::size_t most)
    : most_(std::max<std::size_t>(1, most)), manager_([this] { manage(); }) {}

PlayersFollower::~PlayersFollower() {
    stop_ = true;
    if (manager_.joinable()) manager_.join();
}

void PlayersFollower::watch(std::vector<std::string> ids) {
    if (ids.size() > most_) ids.resize(most_);
    std::lock_guard lock(mutex_);
    wanted_ = std::move(ids);
}

std::vector<BroadcastGame> PlayersFollower::games() const {
    std::lock_guard lock(mutex_);
    std::vector<BroadcastGame> out;
    for (const auto& g : games_)
        if (!g->game.pgn.empty()) out.push_back(g->game);
    return out;
}

std::string PlayersFollower::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}

void PlayersFollower::manage() {
    while (!stop_) {
        std::vector<std::unique_ptr<Game>> gone;  // their threads joined out of the lock
        {
            std::lock_guard lock(mutex_);
            const auto wanted = [&](const std::string& id) {
                return std::find(wanted_.begin(), wanted_.end(), id) != wanted_.end();
            };
            // Those no longer wanted stop; one that ended stays until room is needed, one left unfinished goes.
            for (auto& g : games_)
                if (!wanted(g->id)) g->stop = true;
            for (auto it = games_.begin(); it != games_.end();) {
                const bool unfinished = (*it)->game.result.empty() || (*it)->game.result == "*";
                if (!(*it)->running && (*it)->stop && unfinished) {
                    gone.push_back(std::move(*it));
                    it = games_.erase(it);
                    ++changes_;
                } else {
                    ++it;
                }
            }
            // The wanted ones start while there is room.
            auto running = static_cast<std::size_t>(
                std::count_if(games_.begin(), games_.end(), [](const auto& g) { return g->running && !g->stop; }));
            for (const auto& id : wanted_) {
                if (running >= most_) break;
                if (std::any_of(games_.begin(), games_.end(), [&](const auto& g) { return g->id == id; })) continue;
                auto g      = std::make_unique<Game>();
                g->id       = id;
                g->game.url = fmt::format("https://lichess.org/{}", id);
                auto* raw   = g.get();
                g->thread   = std::thread([this, raw] {
                    follow(*raw);
                    raw->running = false;
                });
                games_.push_back(std::move(g));
                ++running;
            }
            // The games that ended, beyond twice the room, the oldest first.
            while (games_.size() > 2 * most_) {
                const auto it = std::find_if(games_.begin(), games_.end(), [](const auto& g) { return !g->running; });
                if (it == games_.end()) break;
                gone.push_back(std::move(*it));
                games_.erase(it);
                ++changes_;
            }
        }
        for (auto& g : gone)
            if (g->thread.joinable()) g->thread.join();
        rest(stop_, 1);
    }
    std::vector<std::unique_ptr<Game>> all;
    {
        std::lock_guard lock(mutex_);
        for (auto& g : games_) g->stop = true;
        all.swap(games_);
    }
    for (auto& g : all)
        if (g->thread.joinable()) g->thread.join();
}

void PlayersFollower::publish(Game& g, std::string pgn) {
    std::lock_guard lock(mutex_);
    g.game.white     = tagOf(pgn, "White");
    g.game.black     = tagOf(pgn, "Black");
    g.game.white_elo = std::atoi(tagOf(pgn, "WhiteElo").c_str());
    g.game.black_elo = std::atoi(tagOf(pgn, "BlackElo").c_str());
    g.game.result    = tagOf(pgn, "Result");
    if (g.game.result.empty()) g.game.result = "*";
    g.game.pgn = std::move(pgn);
    ++g.game.version;
    ++changes_;
}

void PlayersFollower::follow(Game& g) {
    int failures = 0, out_of_step = 0;
    while (!g.stop) {
        // The game whole: three moves behind while it goes on, as Lichess shows it to anyone.
        auto answer = gamePgn(g.id, {}, &g.stop);
        if (g.stop) return;
        if (!answer.value) {
            {
                std::lock_guard lock(mutex_);
                error_ = fmt::format("{}: {}", g.id, answer.error);
            }
            if (++failures >= 3) return;
            rest(g.stop, 10);
            continue;
        }
        failures        = 0;
        std::string pgn = std::move(*answer.value);
        publish(g, pgn);
        const auto result = tagOf(pgn, "Result");
        if (!result.empty() && result != "*") return;  // it ended
        // Its moves as they come; out of step with what came, or ended, it is asked whole again.
        bool ended = false, apart = false;
        std::string pending;
        HttpRequest request;
        request.url    = fmt::format("{}/api/stream/game/{}", servers().api, g.id);
        request.method = "GET";
        request.headers.emplace_back("Accept", "application/x-ndjson");
        HttpStream stream;
        stream.on_head = [](int status, std::string_view) { return status == 200; };
        stream.on_data = [&](std::string_view data) {
            pending.append(data);
            std::size_t at = 0;
            for (auto end = pending.find('\n', at); end != std::string::npos; end = pending.find('\n', at)) {
                const auto line = std::string_view(pending).substr(at, end - at);
                at              = end + 1;
                if (line.find_first_not_of(" \r\t") == std::string_view::npos) continue;
                nlohmann::json j;
                try {
                    j = nlohmann::json::parse(line);
                } catch (const std::exception&) {
                    continue;
                }
                if (streamEnded(j)) {
                    ended = true;
                    return false;
                }
                if (!j.contains("lm")) continue;  // the game's state, sent first
                if (!addStreamMove(pgn, j)) {
                    apart = true;
                    return false;
                }
                publish(g, pgn);
            }
            pending.erase(0, at);
            return !g.stop;
        };
        stream.cancel = &g.stop;
        stream.idle_s = 120;  // a long think sends nothing: the game is asked whole again then
        (void)politeStream(request, stream);
        if (g.stop) return;
        if (apart && ++out_of_step > 5) {
            std::lock_guard lock(mutex_);
            error_ = fmt::format("{}: its moves could not be followed", g.id);
            return;
        }
        if (!ended && !apart) rest(g.stop, 2);  // a break: not at once
    }
}

}  // namespace fcg::lichess
