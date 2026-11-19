#include "lichess_playing.hpp"

#include <chrono>

#include <fmt/include/fmt/format.h>

#include "https.hpp"

namespace fcg::lichess {

namespace {

std::string textOf(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

}  // namespace

std::optional<PlayingGame> readPlayingGame(const nlohmann::json& j) {
    if (!j.is_object()) return std::nullopt;
    PlayingGame g;
    g.id      = textOf(j, "gameId");
    g.full_id = textOf(j, "fullId");
    if (g.id.empty()) return std::nullopt;
    if (g.full_id.empty()) g.full_id = g.id;
    g.white = textOf(j, "color") != "black";
    g.fen   = textOf(j, "fen");
    if (const auto it = j.find("isMyTurn"); it != j.end() && it->is_boolean()) g.my_turn = it->get<bool>();
    g.speed = textOf(j, "speed");
    if (g.speed.empty()) g.speed = textOf(j, "perf");
    if (const auto it = j.find("rated"); it != j.end() && it->is_boolean()) g.rated = it->get<bool>();
    if (const auto it = j.find("secondsLeft"); it != j.end() && it->is_number_integer())
        g.seconds_left = it->get<int64_t>();
    if (const auto it = j.find("opponent"); it != j.end() && it->is_object()) {
        g.opponent = textOf(*it, "username");
        if (const auto ai = it->find("ai"); g.opponent.empty() && ai != it->end() && ai->is_number_integer())
            g.opponent = fmt::format("AI level {}", ai->get<int>());
        if (const auto r = it->find("rating"); r != it->end() && r->is_number_integer())
            g.opponent_rating = r->get<int>();
    }
    return g;
}

int PlayingGame::move() const {
    // The FEN's sixth field.
    std::size_t at = 0;
    for (int field = 0; field < 5; ++field) {
        at = fen.find(' ', at);
        if (at == std::string::npos) return 0;
        ++at;
    }
    int n = 0;
    for (; at < fen.size() && fen[at] >= '0' && fen[at] <= '9' && n < 100000; ++at) n = n * 10 + (fen[at] - '0');
    return n;
}

std::vector<PlayingGame> readPlaying(const nlohmann::json& j) {
    std::vector<PlayingGame> out;
    if (!j.is_object()) return out;
    const auto it = j.find("nowPlaying");
    if (it == j.end() || !it->is_array()) return out;
    for (const auto& g : *it)
        if (auto game = readPlayingGame(g)) out.push_back(std::move(*game));
    return out;
}

Answer<std::vector<PlayingGame>> playingGames(const std::string& token, const std::atomic<bool>* cancel) {
    Answer<std::vector<PlayingGame>> a;
    HttpRequest request;
    request.url    = servers().api + "/api/account/playing?nb=50";
    request.method = "GET";
    request.token  = token;
    request.headers.emplace_back("Accept", "application/json");
    const auto r = politeRequest(request, cancel);
    a.status     = r.status;
    if (r.status != 200) {
        a.error = r.status == 0     ? (r.error.empty() ? std::string("Lichess did not answer") : r.error)
                  : r.status == 401 ? std::string("Lichess refused the token")
                  : r.status == 429 ? std::string("Lichess asks to wait a minute: too many requests")
                                    : fmt::format("Lichess answered {}", r.status);
        return a;
    }
    try {
        a.value = readPlaying(nlohmann::json::parse(r.body));
    } catch (const std::exception&) {
        a.error = "Lichess's answer could not be read";
    }
    return a;
}

std::optional<GameEvent> readGameEvent(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
    if (line.empty()) return std::nullopt;
    try {
        const auto j    = nlohmann::json::parse(line);
        const auto type = textOf(j, "type");
        if (type != "gameStart" && type != "gameFinish") return std::nullopt;
        const auto it = j.find("game");
        if (it == j.end()) return std::nullopt;
        auto game = readPlayingGame(*it);
        if (!game) return std::nullopt;
        return GameEvent{type == "gameStart" ? GameEvent::Kind::Started : GameEvent::Kind::Finished, std::move(*game)};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

EventListener::EventListener(std::string token) : token_(std::move(token)), thread_([this] { run(); }) {}

EventListener::~EventListener() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

std::vector<GameEvent> EventListener::take() {
    std::lock_guard lock(mutex_);
    std::vector<GameEvent> out(events_.begin(), events_.end());
    events_.clear();
    return out;
}

std::string EventListener::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}

void EventListener::run() {
    while (!stop_) {
        HttpRequest request;
        request.url    = servers().api + "/api/stream/event";
        request.method = "GET";
        request.token  = token_;
        request.headers.emplace_back("Accept", "application/x-ndjson");
        std::string pending;
        int status = 0;
        HttpStream stream;
        stream.on_head = [&](int s, std::string_view) {
            status = s;
            if (s == 200) {
                std::lock_guard lock(mutex_);
                error_.clear();
                events_.push_back(GameEvent{GameEvent::Kind::Opened, {}});
            }
            return s == 200;
        };
        stream.on_data = [&](std::string_view data) {
            pending.append(data);
            std::size_t at = 0;
            for (auto end = pending.find('\n', at); end != std::string::npos; end = pending.find('\n', at)) {
                if (auto e = readGameEvent(std::string_view(pending).substr(at, end - at))) {
                    std::lock_guard lock(mutex_);
                    events_.push_back(std::move(*e));
                }
                at = end + 1;
            }
            pending.erase(0, at);
            return !stop_;
        };
        stream.cancel     = &stop_;
        stream.idle_s     = 30;  // Lichess sends an empty line every 7 s
        const auto result = politeStream(request, stream);
        if (stop_) break;
        {
            std::lock_guard lock(mutex_);
            error_ = status == 401 || status == 403 ? std::string("Lichess refused the token")
                     : status != 200 && status != 0 ? fmt::format("Lichess answered {}", status)
                     : result.error.empty()         ? std::string("the stream ended")
                                                    : result.error;
        }
        if (status == 401 || status == 403) {
            refused_ = true;
            break;
        }
        // Opened again a minute later, the stop heard meanwhile.
        for (int i = 0; i < 240 && !stop_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

}  // namespace fcg::lichess
