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
            out.tour.format       = textOf(*info, "format");
        }
    }
    if (const auto r = e.find("round"); r != e.end() && r->is_object()) {
        out.round.id       = textOf(*r, "id");
        out.round.name     = textOf(*r, "name");
        out.round.url      = textOf(*r, "url");
        out.round.starts   = numberOf(*r, "startsAt");
        out.round.finished = r->value("finished", false);
        out.round.ongoing  = r->value("ongoing", false);
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
    return call<BroadcastList>(
        request("GET", servers().api + "/api/broadcast/top?page=1"),
        [](const nlohmann::json& j) {
            BroadcastList list;
            for (const auto& [key, out] : {std::pair{"active", &list.active}, std::pair{"upcoming", &list.upcoming}}) {
                if (const auto it = j.find(key); it != j.end() && it->is_array())
                    for (const auto& e : *it) out->push_back(entryOf(e));
            }
            return list;
        },
        cancel);
}

namespace {

// An answer about a broadcast: who may read it, what Lichess has not.
template <typename T>
Answer<T> about(Asked asked, Answer<T> a) {
    a.asked = asked;
    return a;
}

// The first number of an object of numbers ({"standard": 2509}), 0 without.
double firstNumber(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_object()) return 0;
    for (const auto& [k, v] : it->items())
        if (v.is_number()) return v.get<double>();
    return 0;
}

}  // namespace

Answer<std::vector<BroadcastEntry>> searchBroadcasts(const std::string& query, const std::string& token,
                                                     const std::atomic<bool>* cancel) {
    return about(
        Asked::Broadcast,
        call<std::vector<BroadcastEntry>>(
            request("GET", fmt::format("{}/api/broadcast/search?q={}&page=1", servers().api, urlEncode(query)), token),
            [](const nlohmann::json& j) -> std::optional<std::vector<BroadcastEntry>> {
                if (!j.is_object()) return std::nullopt;
                std::vector<BroadcastEntry> found;
                if (const auto it = j.find("currentPageResults"); it != j.end() && it->is_array())
                    for (const auto& e : *it)
                        if (e.is_object()) found.push_back(entryOf(e));
                return found;
            },
            cancel));
}

Answer<BroadcastEntry> broadcastRoundEntry(const std::string& round_url, const std::string& token,
                                           const std::atomic<bool>* cancel) {
    const auto at = round_url.find("/broadcast/");
    if (at == std::string::npos) return failedWith<BroadcastEntry>(Failure::own("not a broadcast round's address"));
    auto path = round_url.substr(at);  // /broadcast/<tour>/<round>/<id>
    if (const auto end = path.find_first_of("?#"); end != std::string::npos) path.resize(end);
    return about(Asked::Broadcast, call<BroadcastEntry>(
                                       request("GET", servers().api + "/api" + path, token),
                                       [](const nlohmann::json& j) -> std::optional<BroadcastEntry> {
                                           if (!j.is_object()) return std::nullopt;
                                           return entryOf(j);
                                       },
                                       cancel));
}

namespace {

std::vector<BroadcastPlayer> playersOf(const nlohmann::json& j) {
    std::vector<BroadcastPlayer> players;
    for (const auto& p : j) {
        if (!p.is_object()) continue;
        BroadcastPlayer b;
        b.name        = textOf(p, "name");
        b.title       = textOf(p, "title");
        b.fed         = textOf(p, "fed");
        b.team        = textOf(p, "team");
        b.rating      = static_cast<int>(numberOf(p, "rating"));
        b.played      = static_cast<int>(numberOf(p, "played"));
        b.rank        = static_cast<int>(numberOf(p, "rank"));
        b.performance = static_cast<int>(firstNumber(p, "performances"));
        b.rating_diff = static_cast<int>(firstNumber(p, "ratingDiffs"));
        if (const auto s = p.find("score"); s != p.end() && s->is_number()) {
            b.score  = s->get<double>();
            b.scored = true;
        }
        if (!b.name.empty()) players.push_back(std::move(b));
    }
    // By rank when Lichess gives it, else by score, then rating.
    std::stable_sort(players.begin(), players.end(), [](const auto& x, const auto& y) {
        if ((x.rank > 0) != (y.rank > 0)) return x.rank > 0;
        if (x.rank != y.rank) return x.rank < y.rank;
        if (x.score != y.score) return x.score > y.score;
        return x.rating > y.rating;
    });
    return players;
}

std::vector<BroadcastTeam> teamsOf(const nlohmann::json& j) {
    std::vector<BroadcastTeam> teams;
    for (const auto& t : j) {
        if (!t.is_object()) continue;
        BroadcastTeam b;
        b.name         = textOf(t, "name");
        b.match_points = t.value("mp", 0.0);
        b.game_points  = t.value("gp", 0.0);
        if (const auto m = t.find("matches"); m != t.end() && m->is_array()) b.matches = static_cast<int>(m->size());
        if (!b.name.empty()) teams.push_back(std::move(b));
    }
    std::stable_sort(teams.begin(), teams.end(), [](const auto& x, const auto& y) {
        if (x.match_points != y.match_points) return x.match_points > y.match_points;
        return x.game_points > y.game_points;
    });
    return teams;
}

// A JSON array read by `read`; another JSON is not what Lichess sends.
template <typename T>
auto arrayOf(T (*read)(const nlohmann::json&)) {
    return [read](const nlohmann::json& j) -> std::optional<T> {
        if (!j.is_array()) return std::nullopt;
        return read(j);
    };
}

}  // namespace

Answer<std::vector<BroadcastPlayer>> broadcastPlayers(const std::string& tour_id, const std::atomic<bool>* cancel) {
    return about(Asked::BroadcastTour,
                 call<std::vector<BroadcastPlayer>>(
                     request("GET", fmt::format("{}/broadcast/{}/players", servers().api, urlEncode(tour_id))),
                     arrayOf(playersOf), cancel));
}

Answer<std::vector<BroadcastTeam>> teamStandings(const std::string& tour_id, const std::atomic<bool>* cancel) {
    return about(Asked::BroadcastTour,
                 call<std::vector<BroadcastTeam>>(
                     request("GET", fmt::format("{}/broadcast/{}/teams/standings", servers().api, urlEncode(tour_id))),
                     arrayOf(teamsOf), cancel));
}

void RoundFollower::setRetry(std::chrono::milliseconds first, std::chrono::milliseconds most) {
    g_first_retry_ms = first.count();
    g_most_retry_ms  = most.count();
}

RoundFollower::RoundFollower(std::string id, std::string token, bool whole_tour)
    : round_(std::move(id)), token_(std::move(token)), whole_(whole_tour), thread_([this] { run(); }) {}

RoundFollower::~RoundFollower() { stop(); }

void RoundFollower::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

std::vector<BroadcastGame> RoundFollower::games() const {
    std::lock_guard lock(mutex_);
    return games_;
}

Failure RoundFollower::failure() const {
    std::lock_guard lock(mutex_);
    return failure_;
}

void RoundFollower::take(std::string_view pgn) {
    study::ImportedGame g;
    if (!study::readImportedGame(pgn, g)) return;
    BroadcastGame game;
    std::string round;
    for (const auto& [tag, value] : g.tags) {
        if (tag == "GameURL") game.url = value;
        if (tag == "BroadcastURL") game.round_id = value.substr(value.rfind('/') + 1);
        if (tag == "White") game.white = value;
        if (tag == "Black") game.black = value;
        if (tag == "Result") game.result = value;
        if (tag == "Board") game.board = value;
        if (tag == "Round") round = value;
        if (tag == "WhiteElo") game.white_elo = std::atoi(value.c_str());
        if (tag == "BlackElo") game.black_elo = std::atoi(value.c_str());
    }
    if (game.url.empty()) game.url = round + "|" + game.board + "|" + game.white + "|" + game.black;
    game.round = round.substr(0, round.find('.'));
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
        // The round as it stands first: the stream sends the games that change. A tournament's stream sends its
        // ongoing and recent rounds as they stand itself.
        Failure broke;  // why it stopped this time: none when Lichess closed the stream
        if (!whole_) {
            const auto now =
                send(request("GET", fmt::format("{}/api/broadcast/round/{}.pgn", servers().api, urlEncode(round_)),
                             token_, "application/x-chess-pgn"),
                     &stop_);
            broke = now;
            if (!now.failed())
                for (const auto piece : study::splitGames(now.body))
                    if (whole(piece)) take(piece);
        }
        const auto asked = request("GET",
                                   fmt::format("{}/api/stream/broadcast/{}/{}.pgn", servers().api,
                                               whole_ ? "tour" : "round", urlEncode(round_)),
                                   token_, "application/x-chess-pgn");
        std::string pending;
        bool opened = false;
        HttpStream stream;
        stream.on_head = [&](int s, std::string_view) {
            opened = s == 200;
            if (opened) {
                ++connections_;
                streaming_ = true;
                refused_   = false;
                std::lock_guard lock(mutex_);
                failure_ = {};
            }
            return opened;
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
        if (!broke.failed()) {
            auto ended = politeStream(asked, stream);
            // Once open, its end is no failure, whatever ended it (Lichess closing it, two minutes without a change, a
            // cut): it is opened again in a moment, and a failure to open it then is said.
            if (!opened) broke = responseOf(asked, std::move(ended));
        }
        streaming_ = false;
        if (stop_) break;
        {
            std::lock_guard lock(mutex_);
            refused_       = broke.net == NetError::Unauthorized || broke.net == NetError::Forbidden;
            failure_       = broke;
            failure_.asked = whole_ ? Asked::BroadcastTour : Asked::BroadcastRound;
        }
        if (opened) pause = std::chrono::milliseconds(g_first_retry_ms.load());
        // Then again, after a pause that grows while it fails.
        const auto until = std::chrono::steady_clock::now() + pause;
        while (!stop_ && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (!opened) pause = std::min(pause * 2, std::chrono::milliseconds(g_most_retry_ms.load()));
    }
}

}  // namespace fcg::lichess
