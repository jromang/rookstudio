#include "lichess_tv.hpp"

#include <algorithm>
#include <array>
#include <cctype>

#include <fmt/include/fmt/format.h>

#include "https.hpp"

namespace fcg::lichess {

namespace {

constexpr std::array<std::string_view, 8> CHANNELS = {"best",      "bullet",      "blitz", "rapid",
                                                      "classical", "ultraBullet", "bot",   "computer"};

std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// A channel's key as /api/tv/channels may write it, lowered and without spaces or dashes: "toprated", "ultrabullet".
std::string squeezed(std::string_view key) {
    std::string out;
    for (const char c : key)
        if (c != ' ' && c != '-' && c != '_') out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out == "toprated" ? std::string("best") : out;
}

TvPlayer playerOf(const nlohmann::json& p) {
    TvPlayer out;
    if (!p.is_object()) return out;
    if (const auto it = p.find("aiLevel"); it != p.end() && it->is_number_integer()) out.ai = it->get<int>();
    if (const auto it = p.find("user"); it != p.end() && it->is_object()) {
        out.name  = textOf(*it, "name");
        out.id    = lower(textOf(*it, "id"));
        out.title = textOf(*it, "title");
        if (out.id.empty()) out.id = lower(out.name);
        if (out.name.empty()) out.name = out.id;
    } else if (out.ai == 0) {
        out.name = textOf(p, "name");  // an anonymous player
    }
    if (const auto it = p.find("rating"); it != p.end() && it->is_number_integer()) out.rating = it->get<int>();
    return out;
}

TvGame gameOf(const nlohmann::json& j) {
    TvGame g;
    g.id = textOf(j, "id");
    if (const auto it = j.find("players"); it != j.end() && it->is_object()) {
        if (const auto w = it->find("white"); w != it->end()) g.white = playerOf(*w);
        if (const auto b = it->find("black"); b != it->end()) g.black = playerOf(*b);
    }
    g.speed = textOf(j, "speed");
    if (const auto it = j.find("variant"); it != j.end()) {  // a key, or {key, name, short} in a game's stream
        if (it->is_string())
            g.variant = it->get<std::string>();
        else if (it->is_object())
            g.variant = textOf(*it, "key");
    }
    if (const auto it = j.find("clock"); it != j.end() && it->is_object()) {
        g.initial   = it->value("initial", int64_t{-1});
        g.increment = it->value("increment", int64_t{0});
    }
    if (const auto it = j.find("status"); it != j.end())
        g.status = it->is_string() ? it->get<std::string>() : it->is_object() ? textOf(*it, "name") : std::string();
    return g;
}

Answer<std::vector<TvFeatured>> featuredGames(const std::atomic<bool>* cancel) {
    return call<std::vector<TvFeatured>>(request("GET", servers().api + "/api/tv/channels"), readTvChannels, cancel);
}

// The games of an NDJSON answer, a line each (a line that is not one passed over).
Answer<std::vector<TvGame>> gamesOf(const Response& r) {
    auto a = failedWith<std::vector<TvGame>>(r);
    if (!r.failed()) a.value = readTvGames(r.body);
    return a;
}

Answer<std::vector<TvGame>> channelGames(const std::string& channel, std::size_t nb, const std::atomic<bool>* cancel) {
    nb = std::clamp<std::size_t>(nb, 1, TV_MOST_GAMES);
    return gamesOf(
        send(request("GET", fmt::format("{}/api/tv/{}?nb={}&moves=false", servers().api, urlEncode(channel), nb), {},
                     "application/x-ndjson"),
             cancel));
}

Answer<std::vector<TvGame>> gamesByIds(const std::vector<std::string>& ids, const std::atomic<bool>* cancel) {
    std::string body;
    for (std::size_t i = 0; i < ids.size() && i < 300; ++i) body += (body.empty() ? "" : ",") + ids[i];
    if (body.empty()) {
        Answer<std::vector<TvGame>> a;
        a.value.emplace();
        return a;
    }
    auto asked = request("POST", servers().api + "/api/games/export/_ids?moves=false", {}, "application/x-ndjson");
    asked.content_type = "text/plain";
    asked.body         = std::move(body);
    return gamesOf(send(asked, cancel));
}

}  // namespace

std::span<const std::string_view> tvChannels() { return CHANNELS; }

bool isTvChannel(std::string_view key) { return std::find(CHANNELS.begin(), CHANNELS.end(), key) != CHANNELS.end(); }

std::vector<TvFeatured> readTvChannels(const nlohmann::json& j) {
    std::vector<TvFeatured> out;
    if (!j.is_object()) return out;
    for (const auto key : CHANNELS) {
        for (const auto& [name, c] : j.items()) {
            if (squeezed(name) != squeezed(key) || !c.is_object()) continue;
            TvFeatured f;
            f.channel = std::string(key);
            f.game    = textOf(c, "gameId");
            if (const auto u = c.find("user"); u != c.end() && u->is_object()) {
                f.user    = textOf(*u, "name");
                f.user_id = lower(textOf(*u, "id"));
                f.title   = textOf(*u, "title");
                if (f.user_id.empty()) f.user_id = lower(f.user);
            }
            if (const auto it = c.find("rating"); it != c.end() && it->is_number_integer()) f.rating = it->get<int>();
            f.white = textOf(c, "color") != "black";
            if (!f.game.empty()) out.push_back(std::move(f));
            break;
        }
    }
    return out;
}

std::vector<TvGame> readTvGames(std::string_view ndjson) {
    std::vector<TvGame> out;
    std::size_t at = 0;
    while (at < ndjson.size()) {
        auto end = ndjson.find('\n', at);
        if (end == std::string_view::npos) end = ndjson.size();
        const auto line = ndjson.substr(at, end - at);
        at              = end + 1;
        if (line.find_first_not_of(" \r\t") == std::string_view::npos) continue;
        try {
            auto g = gameOf(nlohmann::json::parse(line));
            if (!g.id.empty()) out.push_back(std::move(g));
        } catch (const std::exception&) {
        }
    }
    return out;
}

std::string tvClockText(int64_t initial, int64_t increment) {
    if (initial < 0) return {};
    std::string base;
    switch (initial) {
        case 0:
            base = "0";
            break;
        case 15:
            base = "¼";
            break;
        case 30:
            base = "½";
            break;
        case 45:
            base = "¾";
            break;
        case 90:
            base = "1.5";
            break;
        default:
            base = initial % 60 == 0 ? std::to_string(initial / 60)
                                     : fmt::format("{:g}", static_cast<double>(initial) / 60);
    }
    return fmt::format("{}+{}", base, increment);
}

std::string tvClockText(std::string_view time_control) {
    const auto plus = time_control.find('+');
    if (plus == std::string_view::npos || plus == 0) return {};
    const auto number = [](std::string_view s, int64_t& out) {
        if (s.empty() ||
            !std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
            return false;
        out = std::stoll(std::string(s));
        return true;
    };
    int64_t initial = 0, increment = 0;
    if (!number(time_control.substr(0, plus), initial) || !number(time_control.substr(plus + 1), increment)) return {};
    return tvClockText(initial, increment);
}

int aiLevelOf(std::string_view name) {
    const auto l                    = lower(name);
    constexpr std::string_view head = "lichess ai level ";
    if (l.rfind(head, 0) != 0) return 0;
    const auto rest = std::string_view(l).substr(head.size());
    if (rest.empty() || rest.size() > 2 ||
        !std::all_of(rest.begin(), rest.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        return 0;
    return std::stoi(std::string(rest));
}

bool playedBy(const TvGame& game, const std::vector<std::string>& own) {
    const auto mine = [&](const TvPlayer& p) {
        return !p.id.empty() && std::find(own.begin(), own.end(), p.id) != own.end();
    };
    return mine(game.white) || mine(game.black);
}

bool playedBy(std::string_view white, std::string_view black, const std::vector<std::string>& own) {
    const auto w = lower(white), b = lower(black);
    return std::any_of(own.begin(), own.end(), [&](const auto& id) { return !id.empty() && (id == w || id == b); });
}

std::vector<std::string> tvPlaces(const std::vector<std::string>& shown, const std::set<std::string>& over,
                                  const std::vector<TvGame>& listed, const std::string& featured,
                                  const std::vector<std::string>& own, std::size_t room,
                                  const std::set<std::string>& resting) {
    // The games that may come: the featured one first, then as listed.
    std::vector<std::string> next;
    const auto offer = [&](const TvGame& g) {
        if (g.id.empty() || !g.standard() || playedBy(g, own) || over.count(g.id)) return;
        if (!g.status.empty() && g.status != "started" && g.status != "created") return;
        if (std::find(shown.begin(), shown.end(), g.id) != shown.end()) return;
        if (std::find(next.begin(), next.end(), g.id) != next.end()) return;
        next.push_back(g.id);
    };
    for (const auto& g : listed)
        if (g.id == featured) offer(g);
    for (const auto& g : listed) offer(g);
    std::size_t taken = 0;  // of `next`
    std::vector<std::string> out;
    std::size_t going   = 0;  // the games going on among them: a stream each
    std::size_t holding = 0;  // those that ended a moment ago: their place held, its stream with it
    for (const auto& id : shown) {
        if (over.count(id)) {
            if (resting.count(id)) {
                out.push_back(id);  // its result shown a moment
                ++holding;
            } else if (taken < next.size() && going + holding < room) {
                out.push_back(next[taken++]);  // its place to another of the channel
                ++going;
            } else {
                out.push_back(id);  // ended, its result shown, until another can take it
            }
        } else if (going + holding < room) {
            out.push_back(id);
            ++going;
        }
    }
    while (going + holding < room && taken < next.size()) {
        out.push_back(next[taken++]);
        ++going;
    }
    return out;
}

Answer<TvList> tvList(const std::string& channel, std::size_t nb, const std::atomic<bool>* cancel) {
    auto featured = featuredGames(cancel);
    if (!featured.value) return failedWith<TvList>(featured);
    auto games = channelGames(channel, nb, cancel);
    auto a     = failedWith<TvList>(games);
    if (!games.value) return a;
    TvList list;
    for (const auto& f : *featured.value)
        if (f.channel == channel) list.featured = f.game;
    list.games = std::move(*games.value);
    a.value    = std::move(list);
    return a;
}

Answer<std::vector<TvRow>> tvRows(const std::atomic<bool>* cancel) {
    auto featured = featuredGames(cancel);
    auto a        = failedWith<std::vector<TvRow>>(featured);
    if (!featured.value) return a;
    std::vector<std::string> ids;
    for (const auto& f : *featured.value)
        if (std::find(ids.begin(), ids.end(), f.game) == ids.end()) ids.push_back(f.game);
    // Both sides and the clock: when Lichess does not give them, the rows say what /api/tv/channels said.
    const auto games = gamesByIds(ids, cancel);
    std::vector<TvRow> rows;
    for (auto& f : *featured.value) {
        TvRow row;
        row.featured = std::move(f);
        if (games.value)
            for (const auto& g : *games.value)
                if (g.id == row.featured.game) row.game = g;
        rows.push_back(std::move(row));
    }
    a.value = std::move(rows);
    return a;
}

TvNext tvNext(const std::string& ended, const TvRow& featured, const std::vector<TvGame>& listed,
              const std::vector<std::string>& own) {
    TvNext out;
    const auto take = [&](const TvGame& g, bool is_featured) {
        out.game     = g.id;
        out.featured = is_featured;
        out.white    = g.white;
        out.black    = g.black;
        out.clock    = tvClockText(g.initial, g.increment);
    };
    const auto& f = featured.featured;
    if (!f.game.empty() && f.game != ended) {
        // Its sides: from the featured game asked by its id, else its player followed only.
        if (featured.game.id == f.game) {
            if (featured.game.standard() && !playedBy(featured.game, own)) {
                take(featured.game, true);
                return out;
            }
        } else if (std::find(own.begin(), own.end(), f.user_id) == own.end()) {
            for (const auto& g : listed)
                if (g.id == f.game && g.standard() && !playedBy(g, own)) {
                    take(g, true);
                    return out;
                }
        }
    }
    for (const auto& g : listed)
        if (g.id != ended && g.standard() && !playedBy(g, own)) {
            take(g, g.id == f.game);
            return out;
        }
    return out;
}

Answer<TvNext> askTvNext(const std::string& channel, const std::string& ended, const std::vector<std::string>& own,
                         bool others, const std::atomic<bool>* cancel) {
    auto rows = tvRows(cancel);
    auto a    = failedWith<TvNext>(rows);
    if (!rows.value) return a;
    TvRow row;
    for (const auto& r : *rows.value)
        if (r.featured.channel == channel) row = r;
    auto next = tvNext(ended, row, {}, own);
    // The featured game will not do: the channel's others, once Lichess has chosen another (one of the user's), or the
    // wait is over.
    if (next.game.empty() && (others || row.featured.game != ended)) {
        const auto games = channelGames(channel, TV_MOST_GAMES, cancel);
        if (!games.value) return failedWith<TvNext>(games);
        next = tvNext(ended, row, *games.value, own);
    }
    a.value = std::move(next);
    return a;
}

std::string tvSide(const TvPlayer& p, std::string_view ai_name, bool brackets) {
    if (p.ai > 0) return std::string(ai_name);
    std::string out = p.title.empty() || p.title == "BOT" ? p.name : fmt::format("{} {}", p.title, p.name);
    if (out.empty()) out = "?";
    if (p.rating > 0) out += brackets ? fmt::format(" ({})", p.rating) : fmt::format(" {}", p.rating);
    return out;
}

}  // namespace fcg::lichess
