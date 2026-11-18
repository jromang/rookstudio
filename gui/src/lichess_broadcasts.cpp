#include "lichess_broadcasts.hpp"

#include <algorithm>
#include <chrono>

#include <json.hpp>

#include <fmt/include/fmt/format.h>
#include <study/import.hpp>

#include "https.hpp"

namespace fcg::lichess {

namespace {

std::atomic<int64_t> g_first_retry_ms{5000};
std::atomic<int64_t> g_most_retry_ms{60000};

std::string textOf(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

int64_t numberOf(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number_integer() ? it->get<int64_t>() : 0;
}

BroadcastEntry entryOf(const nlohmann::json& e) {
    BroadcastEntry out;
    if (const auto t = e.find("tour"); t != e.end() && t->is_object()) {
        out.tour.id   = textOf(*t, "id");
        out.tour.name = textOf(*t, "name");
        out.tour.url  = textOf(*t, "url");
        out.tour.tier = static_cast<int>(numberOf(*t, "tier"));
        if (const auto info = t->find("info"); info != t->end() && info->is_object()) {
            out.tour.players      = textOf(*info, "players");
            out.tour.location     = textOf(*info, "location");
            out.tour.time_control = textOf(*info, "fideTC");
        }
    }
    if (const auto r = e.find("round"); r != e.end() && r->is_object()) {
        out.round.id       = textOf(*r, "id");
        out.round.name     = textOf(*r, "name");
        out.round.url      = textOf(*r, "url");
        out.round.starts   = numberOf(*r, "startsAt");
        out.round.finished = r->value("finished", false);
    }
    out.group = textOf(e, "group");
    return out;
}

// Whether a game's text ends with its result: a game Lichess sent whole.
bool whole(std::string_view text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\r' || text.back() == '\t'))
        text.remove_suffix(1);
    for (const std::string_view result : {"1-0", "0-1", "1/2-1/2", "*"})
        if (text.size() >= result.size() && text.substr(text.size() - result.size()) == result) return true;
    return false;
}

}  // namespace

Answer<BroadcastList> topBroadcasts(const std::atomic<bool>* cancel) {
    Answer<BroadcastList> a;
    HttpRequest request;
    request.url    = servers().api + "/api/broadcast/top?page=1";
    request.method = "GET";
    request.headers.emplace_back("Accept", "application/json");
    const auto r = politeRequest(request, cancel);
    a.status     = r.status;
    if (r.status != 200) {
        a.error = r.status == 0 ? (r.error.empty() ? std::string("Lichess did not answer") : r.error)
                                : fmt::format("Lichess answered {}", r.status);
        return a;
    }
    try {
        const auto j = nlohmann::json::parse(r.body);
        BroadcastList list;
        for (const auto& [key, out] : {std::pair{"active", &list.active}, std::pair{"upcoming", &list.upcoming}}) {
            if (const auto it = j.find(key); it != j.end() && it->is_array())
                for (const auto& e : *it) out->push_back(entryOf(e));
        }
        a.value = std::move(list);
    } catch (const std::exception& e) {
        a.error = std::string("Lichess's answer could not be read: ") + e.what();
    }
    return a;
}

void RoundFollower::setRetry(std::chrono::milliseconds first, std::chrono::milliseconds most) {
    g_first_retry_ms = first.count();
    g_most_retry_ms  = most.count();
}

RoundFollower::RoundFollower(std::string round_id, std::string token)
    : round_(std::move(round_id)), token_(std::move(token)), thread_([this] { run(); }) {}

RoundFollower::~RoundFollower() { stop(); }

void RoundFollower::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

std::vector<BroadcastGame> RoundFollower::games() const {
    std::lock_guard lock(mutex_);
    return games_;
}

std::string RoundFollower::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}

void RoundFollower::take(std::string_view pgn) {
    study::ImportedGame g;
    if (!study::readImportedGame(pgn, g)) return;
    BroadcastGame game;
    std::string round;
    for (const auto& [tag, value] : g.tags) {
        if (tag == "GameURL") game.url = value;
        if (tag == "White") game.white = value;
        if (tag == "Black") game.black = value;
        if (tag == "Result") game.result = value;
        if (tag == "Board") game.board = value;
        if (tag == "Round") round = value;
        if (tag == "WhiteElo") game.white_elo = std::atoi(value.c_str());
        if (tag == "BlackElo") game.black_elo = std::atoi(value.c_str());
    }
    if (game.url.empty()) game.url = round + "|" + game.board + "|" + game.white + "|" + game.black;
    game.pgn.assign(pgn);
    {
        std::lock_guard lock(mutex_);
        const auto it = index_.find(game.url);
        if (it == index_.end()) {
            game.version = 1;
            index_.emplace(game.url, games_.size());
            games_.push_back(std::move(game));
        } else {
            auto& known = games_[it->second];
            if (known.pgn == game.pgn) return;  // sent again as it was
            game.version = known.version + 1;
            known        = std::move(game);
        }
    }
    ++changes_;
}

void RoundFollower::run() {
    auto pause = std::chrono::milliseconds(g_first_retry_ms.load());
    while (!stop_) {
        // The round as it stands first: the stream sends the games that change.
        HttpRequest now;
        now.url      = fmt::format("{}/api/broadcast/round/{}.pgn", servers().api, urlEncode(round_));
        now.method   = "GET";
        now.token    = token_;
        const auto s = politeRequest(now, &stop_);
        if (s.status == 200)
            for (const auto piece : study::splitGames(s.body))
                if (whole(piece)) take(piece);
        HttpRequest request;
        request.url    = fmt::format("{}/api/stream/broadcast/round/{}.pgn", servers().api, urlEncode(round_));
        request.method = "GET";
        request.token  = token_;
        std::string pending;
        int status = 0;
        HttpStream stream;
        stream.on_head = [&](int s, std::string_view) {
            status = s;
            if (s == 200) {
                ++connections_;
                streaming_ = true;
                refused_   = false;
                std::lock_guard lock(mutex_);
                error_.clear();
            }
            return s == 200;
        };
        // The games that came whole: those that end with their result; the rest waits for more.
        stream.on_data = [&](std::string_view data) {
            pending.append(data);
            std::size_t done = 0;
            for (const auto piece : study::splitGames(pending)) {
                if (!whole(piece)) break;
                take(piece);
                done = static_cast<std::size_t>(piece.data() - pending.data()) + piece.size();
            }
            pending.erase(0, done);
            return !stop_;
        };
        stream.cancel = &stop_;
        stream.idle_s = 120;  // Lichess sends nothing while no game changes
        const auto r  = s.status == 200 ? politeStream(request, stream) : HttpResult{s.status, {}, s.error};
        if (s.status != 200) status = s.status;
        streaming_ = false;
        if (stop_) break;
        {
            std::lock_guard lock(mutex_);
            refused_ = status == 401 || status == 403;
            error_   = status == 200   ? (r.error.empty() ? std::string("the stream ended") : r.error)
                       : status == 0   ? (r.error.empty() ? std::string("Lichess did not answer") : r.error)
                       : status == 404 ? std::string("Lichess has no such round")
                       : refused_      ? std::string("Lichess asks for an account allowed to read broadcasts")
                                       : fmt::format("Lichess answered {}", status);
        }
        if (status == 200) pause = std::chrono::milliseconds(g_first_retry_ms.load());
        // Then again, after a pause that grows while it fails.
        const auto until = std::chrono::steady_clock::now() + pause;
        while (!stop_ && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (status != 200) pause = std::min(pause * 2, std::chrono::milliseconds(g_most_retry_ms.load()));
    }
}

}  // namespace fcg::lichess
