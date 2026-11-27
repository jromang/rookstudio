#include "lichess_profile.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <stdexcept>

#include <fmt/include/fmt/format.h>
#include <tour/files.hpp>
#include <tour/util.hpp>

#include "https.hpp"
#include "profile_entries.hpp"

namespace fcg::lichess {

namespace {

// Days since 1970-01-01 of a date of the proleptic Gregorian calendar (Howard Hinnant's days_from_civil).
int64_t daysFrom(int y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int intOf(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end()) return 0;
    if (it->is_number_integer()) return it->get<int>();
    if (it->is_number_float()) return static_cast<int>(it->get<double>() + 0.5);
    return 0;
}

int64_t countOf(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<int64_t>() : 0;
}

const nlohmann::json* objectAt(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_object() ? &*it : nullptr;
}

// A JSON answer, whole: the readers below take what they know of it.
Answer<nlohmann::json> jsonOf(const std::string& url, const std::string& token, const std::atomic<bool>* cancel) {
    return call<nlohmann::json>(request("GET", url, token), [](const nlohmann::json& j) { return j; }, cancel);
}

void to_json(nlohmann::json& j, const Profile& p) {
    auto speeds = nlohmann::json::array();
    for (const auto& s : p.speeds) {
        auto history = nlohmann::json::array();
        for (const auto& h : s.history) history.push_back({h.day, h.rating});
        speeds.push_back({{"key", s.key},
                          {"rating", s.rating},
                          {"games", s.games},
                          {"provisional", s.provisional},
                          {"history", std::move(history)}});
    }
    auto activity = nlohmann::json::array();
    for (const auto& a : p.activity) {
        auto games = nlohmann::json::array();
        for (const auto& g : a.games)
            games.push_back({{"key", g.key}, {"win", g.win}, {"draw", g.draw}, {"loss", g.loss}});
        activity.push_back({{"start", a.start},
                            {"games", std::move(games)},
                            {"puzzles_win", a.puzzles_win},
                            {"puzzles_loss", a.puzzles_loss},
                            {"studies", a.studies}});
    }
    j = {{"format", "rookstudio-lichess-account"},
         {"username", p.username},
         {"read_at", p.read_at},
         {"speeds", std::move(speeds)},
         {"activity", std::move(activity)},
         {"puzzle", {{"rating", p.puzzle_rating}, {"rd", p.puzzle_deviation}, {"games", p.puzzle_games}}}};
}

}  // namespace

void readAccount(const nlohmann::json& j, Profile& out) {
    if (!j.is_object()) return;
    if (const auto it = j.find("username"); it != j.end() && it->is_string()) out.username = it->get<std::string>();
    const auto* perfs = objectAt(j, "perfs");
    out.speeds.clear();
    for (const auto key : SPEEDS) {
        SpeedRating s;
        s.key = std::string(key);
        if (perfs) {
            if (const auto* p = objectAt(*perfs, s.key.c_str())) {
                s.rating = intOf(*p, "rating");
                s.games  = intOf(*p, "games");
                if (const auto it = p->find("prov"); it != p->end() && it->is_boolean())
                    s.provisional = it->get<bool>();
            }
        }
        out.speeds.push_back(std::move(s));
    }
    if (perfs) {
        if (const auto* p = objectAt(*perfs, "puzzle")) {
            out.puzzle_rating    = intOf(*p, "rating");
            out.puzzle_deviation = intOf(*p, "rd");
            out.puzzle_games     = intOf(*p, "games");
        }
    }
}

void readRatingHistory(const nlohmann::json& j, Profile& out) {
    if (!j.is_array()) return;
    for (const auto& entry : j) {
        if (!entry.is_object()) continue;
        const auto name = entry.find("name");
        const auto pts  = entry.find("points");
        if (name == entry.end() || !name->is_string() || pts == entry.end() || !pts->is_array()) continue;
        const auto key = lower(name->get<std::string>());
        const auto s = std::find_if(out.speeds.begin(), out.speeds.end(), [&](const auto& x) { return x.key == key; });
        if (s == out.speeds.end()) continue;
        s->history.clear();
        for (const auto& p : *pts) {
            if (!p.is_array() || p.size() < 4 || !p[0].is_number_integer() || !p[1].is_number_integer() ||
                !p[2].is_number_integer() || !p[3].is_number_integer())
                continue;
            // Lichess's months count from 0.
            s->history.push_back({daysFrom(p[0].get<int>(), p[1].get<int>() + 1, p[2].get<int>()), p[3].get<int>()});
        }
        std::sort(s->history.begin(), s->history.end(), [](const auto& a, const auto& b) { return a.day < b.day; });
    }
}

void readActivity(const nlohmann::json& j, Profile& out) {
    out.activity.clear();
    if (!j.is_array()) return;
    for (const auto& day : j) {
        if (!day.is_object()) continue;
        ActivityDay a;
        if (const auto* interval = objectAt(day, "interval")) a.start = countOf(*interval, "start");
        if (const auto* games = objectAt(day, "games")) {
            for (const auto key : SPEEDS) {
                if (const auto* g = objectAt(*games, std::string(key).c_str()))
                    a.games.push_back({std::string(key), intOf(*g, "win"), intOf(*g, "draw"), intOf(*g, "loss")});
            }
        }
        if (const auto* puzzles = objectAt(day, "puzzles")) {
            if (const auto* score = objectAt(*puzzles, "score")) {
                a.puzzles_win  = intOf(*score, "win");
                a.puzzles_loss = intOf(*score, "loss");
            }
        }
        if (const auto it = day.find("studies"); it != day.end() && it->is_array())
            a.studies = static_cast<int>(it->size());
        if (a.games.empty() && a.puzzles_win + a.puzzles_loss == 0 && a.studies == 0) continue;
        out.activity.push_back(std::move(a));
    }
    std::sort(out.activity.begin(), out.activity.end(), [](const auto& a, const auto& b) { return a.start > b.start; });
}

PerfFigures readPerfFigures(const nlohmann::json& j) {
    PerfFigures f;
    const auto* stat = j.is_object() ? objectAt(j, "stat") : nullptr;
    if (!stat) return f;
    if (const auto* highest = objectAt(*stat, "highest")) {
        f.highest = intOf(*highest, "int");
        if (const auto it = highest->find("at"); it != highest->end() && it->is_string())
            f.highest_at = it->get<std::string>().substr(0, 10);
    }
    if (const auto* wins = objectAt(*stat, "bestWins")) {
        if (const auto it = wins->find("results"); it != wins->end() && it->is_array())
            for (const auto& r : *it)
                if (r.is_object()) f.best_win = std::max(f.best_win, intOf(r, "opRating"));
    }
    if (const auto* streak = objectAt(*stat, "resultStreak"))
        if (const auto* win = objectAt(*streak, "win"))
            if (const auto* max = objectAt(*win, "max")) f.streak = intOf(*max, "v");
    if (const auto* count = objectAt(*stat, "count")) {
        f.win     = intOf(*count, "win");
        f.draw    = intOf(*count, "draw");
        f.loss    = intOf(*count, "loss");
        f.seconds = countOf(*count, "seconds");
    }
    return f;
}

Progress progressOf(const SpeedRating& speed, int64_t today) {
    Progress p;
    if (speed.history.empty() || speed.provisional) return p;
    const auto ratingAt = [&](int64_t day) -> std::optional<int> {
        std::optional<int> at;
        for (const auto& h : speed.history)
            if (h.day <= day) at = h.rating;
        return at;
    };
    const auto last = speed.history.back().day;
    for (const auto& [days, week] : {std::pair{7, true}, std::pair{30, false}}) {
        if (last <= today - days) continue;  // no game in that time
        if (const auto before = ratingAt(today - days)) {
            p.delta = speed.rating - *before;
            p.week  = week;
            p.known = true;
            return p;
        }
    }
    return p;
}

namespace {

// An account's profile, read now; none without its name (not what Lichess sends).
std::optional<Profile> profileOf(const nlohmann::json& j) {
    Profile p;
    readAccount(j, p);
    if (p.username.empty()) return std::nullopt;
    p.read_at = static_cast<int64_t>(std::time(nullptr));
    return p;
}

}  // namespace

Answer<Profile> publicProfile(const std::string& username, const std::atomic<bool>* cancel) {
    auto a = call<Profile>(request("GET", fmt::format("{}/api/user/{}", servers().api, urlEncode(username))), profileOf,
                           cancel);
    a.asked = Asked::Player;
    return a;
}

Answer<Profile> accountProfile(const std::string& token, const std::atomic<bool>* cancel) {
    const auto& api = servers().api;
    auto a          = call<Profile>(request("GET", api + "/api/account", token), profileOf, cancel);
    if (!a.value) return a;
    // The history only a token makes Lichess compute; the activity of the last days: without them, the rest.
    auto& p         = *a.value;
    const auto user = urlEncode(p.username);
    if (const auto history = jsonOf(fmt::format("{}/api/user/{}/rating-history", api, user), token, cancel);
        history.value)
        readRatingHistory(*history.value, p);
    if (const auto activity = jsonOf(fmt::format("{}/api/user/{}/activity", api, user), token, cancel); activity.value)
        readActivity(*activity.value, p);
    p.read_at = static_cast<int64_t>(std::time(nullptr));
    return a;
}

Answer<PerfFigures> perfFigures(const std::string& username, std::string_view speed, const std::string& token,
                                const std::atomic<bool>* cancel) {
    return call<PerfFigures>(
        request("GET", fmt::format("{}/api/user/{}/perf/{}", servers().api, urlEncode(username), urlEncode(speed)),
                token),
        readPerfFigures, cancel);
}

void keepProfile(const std::filesystem::path& root, const Profile& profile) {
    nlohmann::json j;
    to_json(j, profile);
    std::error_code ec;
    std::filesystem::create_directories(root / "lichess-cache", ec);
    try {
        tour::replaceFile(root / "lichess-cache" / "account.json", j.dump());
    } catch (const std::exception&) {
        // a profile that cannot keep it asks Lichess again next time
    }
}

std::chrono::seconds profileAge(const Profile& profile, int64_t now) {
    return std::chrono::seconds(now - profile.read_at);
}

std::optional<Profile> keptProfile(const std::filesystem::path& root) {
    // Entry by entry (#143): a speed, a day of activity or a figure of another type is set aside and said, the others
    // kept, the file as it was kept beside it; a file cut short is kept aside and said. Lichess is asked again either
    // way once the profile is old.
    const auto file = root / "lichess-cache" / "account.json";
    const auto j    = readProfileJson(file);
    if (!j) return std::nullopt;
    if (const auto format = j->find("format");
        format == j->end() || !format->is_string() || *format != "rookstudio-lichess-account")
        return std::nullopt;
    ProfileEntries entries(file);
    Profile p;
    entries.entry("username", [&] { p.username = j->value("username", ""); });
    entries.entry("read_at", [&] { p.read_at = j->value("read_at", int64_t{0}); });
    // A list of objects, each an entry: the list's name and its place.
    const auto each = [&](const char* list, const auto& read) {
        const auto it = j->find(list);
        if (it == j->end()) return;
        if (!it->is_array()) {
            entries.entry(list, [] { throw std::runtime_error("not a list"); });
            return;
        }
        for (std::size_t i = 0; i < it->size(); ++i)
            entries.entry(fmt::format("{}/#{}", list, i + 1), [&] { read((*it)[i]); });
    };
    each("speeds", [&](const nlohmann::json& s) {
        SpeedRating r;
        r.key         = s.value("key", "");
        r.rating      = s.value("rating", 0);
        r.games       = s.value("games", 0);
        r.provisional = s.value("provisional", false);
        for (const auto& h : s.at("history")) r.history.push_back({h.at(0).get<int64_t>(), h.at(1).get<int>()});
        p.speeds.push_back(std::move(r));
    });
    each("activity", [&](const nlohmann::json& d) {
        ActivityDay a;
        a.start = d.value("start", int64_t{0});
        for (const auto& g : d.at("games"))
            a.games.push_back({g.value("key", ""), g.value("win", 0), g.value("draw", 0), g.value("loss", 0)});
        a.puzzles_win  = d.value("puzzles_win", 0);
        a.puzzles_loss = d.value("puzzles_loss", 0);
        a.studies      = d.value("studies", 0);
        p.activity.push_back(std::move(a));
    });
    if (const auto it = j->find("puzzle"); it != j->end() && it->is_object()) {
        entries.entry("puzzle", [&] {
            const int rating = it->value("rating", 0), deviation = it->value("rd", 0), games = it->value("games", 0);
            p.puzzle_rating    = rating;
            p.puzzle_deviation = deviation;
            p.puzzle_games     = games;
        });
    }
    entries.sayUnread();
    return p;
}

}  // namespace fcg::lichess
