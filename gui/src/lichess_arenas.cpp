#include "lichess_arenas.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <map>
#include <set>

#include <fmt/include/fmt/format.h>
#include <tour/util.hpp>

#include "https.hpp"
#include "lichess_games.hpp"

namespace fcg::lichess {

namespace {

int64_t intOf(const nlohmann::json& j, const char* key, int64_t otherwise = 0) {
    const auto it = j.find(key);
    if (it == j.end()) return otherwise;
    if (it->is_number_integer()) return it->get<int64_t>();
    if (it->is_number_float()) return static_cast<int64_t>(it->get<double>());
    return otherwise;
}

double numberOf(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<double>() : 0.0;
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// An answer about a tournament: its 404 says Lichess has no such tournament.
template <typename T>
Answer<T> aboutTournament(Answer<T> a) {
    a.asked = Asked::Tournament;
    return a;
}

// Anyone's verdicts, asked without a token: none of them the user's.
void anyonesVerdicts(std::vector<TourCondition>& conditions) {
    for (auto& c : conditions) {
        c.ok.reset();
        c.refusal.clear();
    }
}

// A time Lichess writes as ms since the epoch, or as an ISO date.
int64_t millisOf(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end()) return 0;
    if (it->is_number()) return intOf(j, key);
    if (it->is_string()) return isoMillis(it->get<std::string>()).value_or(0);
    return 0;
}

// The speed of a clock, as scalachess counts it: its base and 40 times its increment, in seconds.
std::string speedOfSeconds(int64_t limit, int64_t increment) {
    const int64_t total = limit + 40 * increment;
    if (total < 30) return "ultraBullet";
    if (total < 180) return "bullet";
    if (total < 480) return "blitz";
    if (total < 1500) return "rapid";
    return "classical";
}

std::string variantOf(const nlohmann::json& j) {
    const auto it = j.find("variant");
    if (it == j.end()) return "standard";
    if (it->is_string()) return it->get<std::string>();
    if (it->is_object()) return textOf(*it, "key");
    return "standard";
}

ArenaInfo readArenaInfo(const nlohmann::json& j, bool with_token) {
    ArenaInfo a;
    a.id   = textOf(j, "id");
    a.name = textOf(j, "fullName");
    if (a.name.empty()) a.name = textOf(j, "name");
    if (const auto it = j.find("clock"); it != j.end() && it->is_object()) {
        a.limit_s     = intOf(*it, "limit");
        a.increment_s = intOf(*it, "increment");
    }
    if (const auto it = j.find("perf"); it != j.end() && it->is_object()) a.speed = textOf(*it, "key");
    if (a.speed.empty()) a.speed = speedOfSeconds(a.limit_s, a.increment_s);
    a.variant     = variantOf(j);
    a.rated       = !j.contains("rated") || boolOf(j, "rated");
    a.minutes     = static_cast<int>(intOf(j, "minutes"));
    a.players     = static_cast<int>(intOf(j, "nbPlayers"));
    a.starts_at   = millisOf(j, "startsAt");
    a.finishes_at = millisOf(j, "finishesAt");
    if (a.finishes_at == 0 && a.starts_at > 0 && a.minutes > 0)
        a.finishes_at = a.starts_at + int64_t{a.minutes} * 60000;
    const auto status = intOf(j, "status");
    a.state           = status >= 30 || boolOf(j, "isFinished")  ? TourState::Finished
                        : status >= 20 || boolOf(j, "isStarted") ? TourState::Started
                                                                 : TourState::Created;
    if (const auto it = j.find("schedule"); it != j.end() && it->is_object()) a.freq = textOf(*it, "freq");
    a.berserkable = boolOf(j, "berserkable");
    a.conditions  = readConditions(j);
    if (!with_token)
        for (auto& c : a.conditions) {  // an anonymous answer's verdicts are anyone's
            c.ok.reset();
            c.refusal.clear();
        }
    if (const auto v = j.find("verdicts"); with_token && v != j.end() && v->is_object()) {
        a.judged   = true;
        a.accepted = boolOf(*v, "accepted");
    }
    return a;
}

ArenaPlayer readPlayer(const nlohmann::json& p) {
    ArenaPlayer out;
    out.rank   = static_cast<int>(intOf(p, "rank"));
    out.name   = textOf(p, "name");
    out.title  = textOf(p, "title");
    out.rating = static_cast<int>(intOf(p, "rating"));
    out.score  = static_cast<int>(intOf(p, "score"));
    if (const auto it = p.find("sheet"); it != p.end() && it->is_object()) {
        out.sheet = textOf(*it, "scores");
        out.fire  = boolOf(*it, "fire");
    }
    out.paused = boolOf(p, "withdraw");
    if (const auto it = p.find("nb"); it != p.end() && it->is_object())
        out.games = static_cast<int>(intOf(*it, "game"));
    out.performance = static_cast<int>(intOf(p, "performance"));
    return out;
}

// The columns of a TRF line, from 1 (TRF16's numbering), trimmed.
std::string_view column(std::string_view line, std::size_t from, std::size_t to) {
    if (line.size() < from) return {};
    auto field = line.substr(from - 1, std::min(to, line.size()) - (from - 1));
    while (!field.empty() && field.front() == ' ') field.remove_prefix(1);
    while (!field.empty() && (field.back() == ' ' || field.back() == '\r')) field.remove_suffix(1);
    return field;
}

int numberIn(std::string_view field) {
    int value = 0;
    bool any  = false;
    for (const char c : field) {
        if (c < '0' || c > '9') return any ? value : 0;
        value = value * 10 + (c - '0');
        any   = true;
    }
    return value;
}

const char* const MONTHS[] = {"January", "February", "March",     "April",   "May",      "June",
                              "July",    "August",   "September", "October", "November", "December"};

}  // namespace

// ---------------------------------------------------------------------------
// Conditions

std::vector<TourCondition> readConditions(const nlohmann::json& j) {
    std::vector<TourCondition> own;
    const auto perfOf = [](const nlohmann::json& o) {
        const auto it = o.find("perf");
        if (it == o.end()) return std::string();
        if (it->is_string()) return it->get<std::string>();
        if (it->is_object()) return textOf(*it, "key");
        return std::string();
    };
    if (const auto it = j.find("minRatedGames"); it != j.end() && it->is_object()) {
        TourCondition c;
        c.kind  = TourCondition::Kind::RatedGames;
        c.value = static_cast<int>(intOf(*it, "nb"));
        c.perf  = perfOf(*it);
        own.push_back(c);
    }
    if (const auto it = j.find("maxRating"); it != j.end() && it->is_object()) {
        TourCondition c;
        c.kind  = TourCondition::Kind::MaxRating;
        c.value = static_cast<int>(intOf(*it, "rating"));
        c.perf  = perfOf(*it);
        own.push_back(c);
    }
    if (const auto it = j.find("minRating"); it != j.end() && it->is_object()) {
        TourCondition c;
        c.kind  = TourCondition::Kind::MinRating;
        c.value = static_cast<int>(intOf(*it, "rating"));
        c.perf  = perfOf(*it);
        own.push_back(c);
    }
    if (boolOf(j, "onlyTitled")) {
        TourCondition c;
        c.kind = TourCondition::Kind::Titled;
        own.push_back(c);
    }
    if (const auto it = j.find("teamMember"); it != j.end() && it->is_object()) {
        TourCondition c;
        c.kind      = TourCondition::Kind::Team;
        c.team_id   = textOf(*it, "teamId");
        c.team_name = textOf(*it, "teamName");
        if (c.team_name.empty()) c.team_name = c.team_id;
        own.push_back(c);
    }
    if (const auto days = intOf(j, "minAccountAgeInDays", intOf(j, "accountAge")); days > 0) {
        TourCondition c;
        c.kind  = TourCondition::Kind::AccountAge;
        c.value = static_cast<int>(days);
        own.push_back(c);
    }
    if (const auto it = j.find("allowList");
        it != j.end() && !it->is_null() && !(it->is_boolean() && !it->get<bool>())) {
        TourCondition c;
        c.kind = TourCondition::Kind::AllowList;
        own.push_back(c);
    }
    // Lichess's verdicts, in the order of its conditions.
    std::vector<TourCondition> said;
    if (const auto v = j.find("verdicts"); v != j.end() && v->is_object())
        if (const auto list = v->find("list"); list != v->end() && list->is_array())
            for (const auto& item : *list) {
                if (!item.is_object()) continue;
                TourCondition c;
                c.text             = textOf(item, "condition");
                const auto verdict = textOf(item, "verdict");
                c.ok               = verdict == "ok";
                if (verdict != "ok") c.refusal = verdict;
                said.push_back(std::move(c));
            }
    if (said.empty()) return own;
    if (said.size() == own.size()) {
        for (std::size_t i = 0; i < own.size(); ++i) {
            own[i].text    = said[i].text;
            own[i].ok      = said[i].ok;
            own[i].refusal = said[i].refusal;
        }
        return own;
    }
    return said;  // conditions RookStudio does not read: as Lichess words them
}

std::optional<bool> conditionsMet(const std::vector<TourCondition>& conditions, std::string_view perf,
                                  const Profile* profile) {
    bool unknown = false;
    for (const auto& c : conditions) {
        if (c.ok) {
            if (!*c.ok) return false;
            continue;
        }
        const SpeedRating* speed = nullptr;
        if (profile)
            for (const auto& s : profile->speeds)
                if (s.key == (c.perf.empty() ? perf : std::string_view(c.perf))) speed = &s;
        switch (c.kind) {
            case TourCondition::Kind::RatedGames:
                if (!speed)
                    unknown = true;
                else if (speed->games < c.value)
                    return false;
                break;
            case TourCondition::Kind::MaxRating:
                if (!speed || speed->games == 0)
                    unknown = true;
                else if (speed->rating > c.value)
                    return false;
                break;
            case TourCondition::Kind::MinRating:
                if (!speed || speed->games == 0)
                    unknown = true;
                else if (speed->rating < c.value || speed->provisional)
                    return false;
                break;
            default:
                unknown = true;
        }
    }
    if (unknown) return std::nullopt;
    return true;
}

// ---------------------------------------------------------------------------
// Arenas

std::optional<int64_t> isoMillis(std::string_view text) {
    if (text.size() < 19) return std::nullopt;
    std::string date(text.substr(0, 10)), time(text.substr(11, 8));
    if (text[4] != '-' || text[7] != '-' || (text[10] != 'T' && text[10] != ' ')) return std::nullopt;
    std::replace(date.begin(), date.end(), '-', '.');
    auto ms = startOf(date, time);
    if (!ms) return std::nullopt;
    // Its milliseconds, after a dot.
    if (text.size() > 20 && text[19] == '.') {
        int fraction = 0, digits = 0;
        for (std::size_t i = 20; i < text.size() && digits < 3 && text[i] >= '0' && text[i] <= '9'; ++i, ++digits)
            fraction = fraction * 10 + (text[i] - '0');
        while (digits > 0 && digits < 3) {
            fraction *= 10;
            ++digits;
        }
        *ms += fraction;
    }
    return ms;
}

std::vector<ArenaInfo> readArenaList(const nlohmann::json& j) {
    std::vector<ArenaInfo> started, created;
    const auto read = [&](const char* key, std::vector<ArenaInfo>& into) {
        if (const auto it = j.find(key); it != j.end() && it->is_array())
            for (const auto& t : *it) {
                if (!t.is_object()) continue;
                auto a = readArenaInfo(t, false);
                if (a.id.empty() || !a.standard() || a.state == TourState::Finished) continue;
                into.push_back(std::move(a));
            }
    };
    read("started", started);
    read("created", created);
    std::stable_sort(started.begin(), started.end(),
                     [](const auto& x, const auto& y) { return x.finishes_at < y.finishes_at; });
    std::stable_sort(created.begin(), created.end(),
                     [](const auto& x, const auto& y) { return x.starts_at < y.starts_at; });
    for (auto& a : created) started.push_back(std::move(a));
    return started;
}

Answer<std::vector<ArenaInfo>> arenaList(const std::atomic<bool>* cancel) {
    return call<std::vector<ArenaInfo>>(request("GET", servers().api + "/api/tournament"), readArenaList, cancel);
}

bool arenaShown(const ArenaInfo& arena, ArenaSpeeds speeds) {
    switch (speeds) {
        case ArenaSpeeds::Rapid:
            return arena.speed == "rapid";
        case ArenaSpeeds::Classical:
            return arena.speed == "classical";
        case ArenaSpeeds::All:
            return true;
    }
    return true;
}

bool playableHere(const ArenaInfo& arena) {
    return arena.standard() && (arena.speed == "rapid" || arena.speed == "classical");
}

JoinOffer joinOffer(const ArenaInfo& arena, const Profile* profile) {
    if (arena.state == TourState::Finished) return JoinOffer::Finished;
    if (!playableHere(arena)) return JoinOffer::WebOnly;
    if (arena.judged && !arena.accepted) return JoinOffer::ConditionNotMet;
    if (conditionsMet(arena.conditions, arena.speed, profile) == false) return JoinOffer::ConditionNotMet;
    return JoinOffer::Join;
}

std::vector<SheetScore> readSheet(std::string_view scores, bool streaks) {
    std::vector<SheetScore> out;
    int wins = 0;  // in a row
    for (auto it = scores.rbegin(); it != scores.rend(); ++it) {
        if (*it < '0' || *it > '9') continue;
        SheetScore s;
        s.points       = *it - '0';
        const bool on  = streaks && wins >= 2;
        s.doubled      = on && s.points > 0;  // a loss ends the streak: nothing to double
        const bool won = on ? s.points >= 4 : s.points >= 2;
        s.berserk      = won && (on ? s.points == 5 : s.points == 3);
        wins           = won ? wins + 1 : 0;
        out.push_back(s);
    }
    return out;
}

Arena readArena(const nlohmann::json& j) {
    Arena a;
    a.info              = readArenaInfo(j, true);
    a.seconds_to_finish = static_cast<int>(intOf(j, "secondsToFinish", -1));
    a.seconds_to_start  = static_cast<int>(intOf(j, "secondsToStart", -1));
    a.streaks           = !boolOf(j, "noStreak");
    if (const auto st = j.find("standing"); st != j.end() && st->is_object()) {
        a.page = static_cast<int>(intOf(*st, "page", 1));
        if (const auto players = st->find("players"); players != st->end() && players->is_array())
            for (const auto& p : *players)
                if (p.is_object()) a.standing.push_back(readPlayer(p));
    }
    if (const auto duels = j.find("duels"); duels != j.end() && duels->is_array())
        for (const auto& d : *duels) {
            if (!d.is_object()) continue;
            ArenaDuel duel;
            duel.id = textOf(d, "id");
            if (const auto p = d.find("p"); p != d.end() && p->is_array() && p->size() == 2) {
                const auto& x = (*p)[0];
                const auto& y = (*p)[1];
                duel.a        = textOf(x, "n");
                duel.b        = textOf(y, "n");
                duel.a_rank   = static_cast<int>(intOf(x, "k"));
                duel.b_rank   = static_cast<int>(intOf(y, "k"));
                duel.a_rating = static_cast<int>(intOf(x, "r"));
                duel.b_rating = static_cast<int>(intOf(y, "r"));
            }
            if (!duel.id.empty()) a.duels.push_back(std::move(duel));
        }
    if (const auto f = j.find("featured"); f != j.end() && f->is_object()) a.featured = textOf(*f, "id");
    if (const auto me = j.find("me"); me != j.end() && me->is_object()) {
        ArenaMe m;
        m.rank        = static_cast<int>(intOf(*me, "rank"));
        m.paused      = boolOf(*me, "withdraw");
        m.game        = textOf(*me, "gameId");
        m.pause_delay = static_cast<int>(intOf(*me, "pauseDelay"));
        a.me          = m;
    }
    if (const auto podium = j.find("podium"); podium != j.end() && podium->is_array())
        for (const auto& p : *podium)
            if (p.is_object()) a.podium.push_back(readPlayer(p));
    a.pairings_closed = boolOf(j, "pairingsClosed");
    return a;
}

Answer<Arena> arenaOf(const std::string& id, int page, const std::string& token, const std::atomic<bool>* cancel) {
    auto url = fmt::format("{}/api/tournament/{}", servers().api, urlEncode(id));
    if (page > 1) url += fmt::format("?page={}", page);
    return aboutTournament(call<Arena>(
        request("GET", url, token),
        [&](const nlohmann::json& j) {
            auto arena = readArena(j);
            if (token.empty()) {  // an anonymous answer's verdicts are anyone's
                arena.info.judged   = false;
                arena.info.accepted = true;
                anyonesVerdicts(arena.info.conditions);
            }
            return arena;
        },
        cancel));
}

int standingPage(int rank) { return rank <= 0 ? 1 : (rank - 1) / 10 + 1; }

const ArenaPlayer* standingOf(const Arena& arena, std::string_view name) {
    const auto wanted = lower(name);
    for (const auto& p : arena.standing)
        if (lower(p.name) == wanted) return &p;
    return nullptr;
}

Reply joinArena(const std::string& token, const std::string& id, bool pair_me_asap, const std::atomic<bool>* cancel) {
    std::vector<std::pair<std::string, std::string>> fields;
    if (pair_me_asap) fields.emplace_back("pairMeAsap", "true");
    return lichessPost(fmt::format("/api/tournament/{}/join", urlEncode(id)), token, fields, cancel);
}

Reply withdrawArena(const std::string& token, const std::string& id, const std::atomic<bool>* cancel) {
    return lichessPost(fmt::format("/api/tournament/{}/withdraw", urlEncode(id)), token, {}, cancel);
}

Reply berserk(const std::string& token, const std::string& game, const std::atomic<bool>* cancel) {
    return boardAction(token, game, "berserk", cancel);
}

bool berserkAllowed(bool berserkable, const BoardState& game, bool white) {
    if (!berserkable || !game.full || game.over() || !game.live()) return false;
    if (white ? game.white_berserk : game.black_berserk) return false;
    // Before the user's own first move: White's is the first, Black's the second.
    return white ? game.moves.empty() : game.moves.size() < 2;
}

bool tournamentGame(const BoardState& game) { return !game.tournament.empty() || !game.swiss.empty(); }

bool lostAtStart(const BoardState& game, bool white) {
    return game.status == "noStart" && game.winner == (white ? "black" : "white");
}

std::vector<std::string> scopesAdding(const std::vector<std::string>& granted, std::string_view scope) {
    auto out = granted;
    if (std::find(out.begin(), out.end(), scope) == out.end()) out.emplace_back(scope);
    return out;
}

void ArenaSeat::joined(Clock::time_point now) {
    joined_ = true;
    paused_ = false;
    last_   = now;
}

void ArenaSeat::pause() { paused_ = true; }

void ArenaSeat::playing(bool on) { playing_ = on; }

void ArenaSeat::over() { over_ = true; }

// ---------------------------------------------------------------------------
// Swiss tournaments

std::vector<Team> readTeams(const nlohmann::json& j) {
    std::vector<Team> out;
    if (!j.is_array()) return out;
    for (const auto& t : j) {
        if (!t.is_object()) continue;
        Team team;
        team.id      = textOf(t, "id");
        team.name    = textOf(t, "name");
        team.members = static_cast<int>(intOf(t, "nbMembers"));
        if (team.name.empty()) team.name = team.id;
        if (!team.id.empty()) out.push_back(std::move(team));
    }
    return out;
}

Answer<std::vector<Team>> teamsOf(const std::string& username, const std::string& token,
                                  const std::atomic<bool>* cancel) {
    auto a = call<std::vector<Team>>(
        request("GET", fmt::format("{}/api/team/of/{}", servers().api, urlEncode(username)), token), readTeams, cancel);
    a.asked = Asked::Player;
    return a;
}

std::string SwissInfo::speed() const { return speedOfSeconds(limit_s, increment_s); }

SwissInfo readSwiss(const nlohmann::json& j) {
    SwissInfo s;
    s.id   = textOf(j, "id");
    s.name = textOf(j, "name");
    if (const auto it = j.find("clock"); it != j.end() && it->is_object()) {
        s.limit_s     = intOf(*it, "limit");
        s.increment_s = intOf(*it, "increment");
    }
    s.variant         = variantOf(j);
    s.rated           = !j.contains("rated") || boolOf(j, "rated");
    s.round           = static_cast<int>(intOf(j, "round"));
    s.rounds          = static_cast<int>(intOf(j, "nbRounds"));
    s.players         = static_cast<int>(intOf(j, "nbPlayers"));
    s.ongoing         = static_cast<int>(intOf(j, "nbOngoing"));
    const auto status = textOf(j, "status");
    s.state           = status == "finished"  ? TourState::Finished
                        : status == "started" ? TourState::Started
                                              : TourState::Created;
    s.starts_at       = millisOf(j, "startsAt");
    if (const auto it = j.find("nextRound"); it != j.end() && it->is_object()) s.next_round_at = millisOf(*it, "at");
    s.conditions = readConditions(j);
    if (const auto v = j.find("verdicts"); v != j.end() && v->is_object()) {
        s.judged   = true;
        s.accepted = boolOf(*v, "accepted");
    }
    return s;
}

std::vector<SwissInfo> readSwissList(std::string_view ndjson) {
    std::vector<SwissInfo> out;
    LineSplitter lines([&](std::string_view line) {
        const auto j = nlohmann::json::parse(line, nullptr, false);
        if (j.is_object()) {
            auto s = readSwiss(j);
            if (!s.id.empty()) out.push_back(std::move(s));
        }
        return true;
    });
    lines.feed(ndjson);
    lines.flush();
    return out;
}

bool playableHere(const SwissInfo& swiss) {
    const auto speed = swiss.limit_s == 0 && swiss.increment_s == 0 ? std::string("correspondence") : swiss.speed();
    return swiss.standard() && (speed == "rapid" || speed == "classical" || speed == "correspondence");
}

Answer<std::vector<SwissInfo>> teamSwisses(const std::string& team, const std::string& token, int most,
                                           const std::atomic<bool>* cancel) {
    const auto r = send(
        request("GET", fmt::format("{}/api/team/{}/swiss?max={}", servers().api, urlEncode(team), std::max(1, most)),
                token, "application/x-ndjson"),
        cancel);
    return readBody<std::vector<SwissInfo>>(r, [&](std::string_view ndjson) {
        auto list = readSwissList(ndjson);
        for (auto& s : list) {
            s.team_id = team;
            if (token.empty()) {  // anyone's verdicts
                s.judged = false;
                anyonesVerdicts(s.conditions);
            }
        }
        return list;
    });
}

Answer<SwissInfo> swissOf(const std::string& id, const std::string& token, const std::atomic<bool>* cancel) {
    return aboutTournament(call<SwissInfo>(
        request("GET", fmt::format("{}/api/swiss/{}", servers().api, urlEncode(id)), token),
        [&](const nlohmann::json& j) {
            auto swiss = readSwiss(j);
            if (token.empty()) {
                swiss.judged = false;
                anyonesVerdicts(swiss.conditions);
            }
            return swiss;
        },
        cancel));
}

std::vector<SwissRow> readSwissResults(std::string_view ndjson) {
    std::vector<SwissRow> out;
    LineSplitter lines([&](std::string_view line) {
        const auto j = nlohmann::json::parse(line, nullptr, false);
        if (!j.is_object()) return true;
        SwissRow r;
        r.rank        = static_cast<int>(intOf(j, "rank"));
        r.name        = textOf(j, "username");
        r.title       = textOf(j, "title");
        r.rating      = static_cast<int>(intOf(j, "rating"));
        r.points      = numberOf(j, "points");
        r.tie_break   = numberOf(j, "tieBreak");
        r.performance = static_cast<int>(intOf(j, "performance"));
        r.absent      = boolOf(j, "absent");
        if (!r.name.empty()) out.push_back(std::move(r));
        return true;
    });
    lines.feed(ndjson);
    lines.flush();
    return out;
}

Answer<std::vector<SwissRow>> swissResults(const std::string& id, int most, const std::atomic<bool>* cancel) {
    auto url = fmt::format("{}/api/swiss/{}/results", servers().api, urlEncode(id));
    if (most > 0) url += fmt::format("?nb={}", most);
    return aboutTournament(readBody<std::vector<SwissRow>>(
        send(request("GET", url, {}, "application/x-ndjson"), cancel), readSwissResults));
}

Reply joinSwiss(const std::string& token, const std::string& id, const std::atomic<bool>* cancel) {
    return lichessPost(fmt::format("/api/swiss/{}/join", urlEncode(id)), token, {}, cancel);
}

Reply withdrawSwiss(const std::string& token, const std::string& id, const std::atomic<bool>* cancel) {
    return lichessPost(fmt::format("/api/swiss/{}/withdraw", urlEncode(id)), token, {}, cancel);
}

Answer<std::string> swissTrf(const std::string& id, const std::atomic<bool>* cancel) {
    const auto r =
        send(request("GET", fmt::format("{}/swiss/{}.trf", servers().api, urlEncode(id)), {}, "text/plain"), cancel);
    return aboutTournament(readBody<std::string>(r, [](std::string_view text) { return std::string(text); }));
}

Answer<std::string> swissGames(const std::string& id, const std::string& token, const std::atomic<bool>* cancel) {
    auto asked =
        request("GET", fmt::format("{}/api/swiss/{}/games?clocks=true&opening=true", servers().api, urlEncode(id)),
                token, "application/x-chess-pgn");
    asked.timeout_s = 120;
    return aboutTournament(
        readBody<std::string>(send(asked, cancel), [](std::string_view pgn) { return std::string(pgn); }));
}

bool roundNoticeDue(int64_t next_round_at_ms, int64_t now_ms) {
    const auto before = std::chrono::duration_cast<std::chrono::milliseconds>(ROUND_NOTICE).count();
    return next_round_at_ms > 0 && now_ms >= next_round_at_ms - before && now_ms < next_round_at_ms;
}

// ---------------------------------------------------------------------------
// TRF, and a Swiss kept

const TrfPlayer* Trf::player(std::string_view name) const {
    const auto wanted = lower(name);
    for (const auto& p : players)
        if (lower(p.name) == wanted) return &p;
    return nullptr;
}

const TrfPlayer* Trf::number(int n) const {
    for (const auto& p : players)
        if (p.number == n) return &p;
    return nullptr;
}

Trf readTrf(std::string_view text) {
    Trf t;
    std::size_t at = 0;
    while (at < text.size()) {
        const auto end = text.find('\n', at);
        auto line      = text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
        at             = end == std::string_view::npos ? text.size() : end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.size() < 3) continue;
        const auto code = line.substr(0, 3);
        if (code == "012") {
            t.name = std::string(column(line, 5, line.size()));
        } else if (code == "022") {
            t.url = std::string(column(line, 5, line.size()));
        } else if (code == "XXR") {
            t.rounds = numberIn(column(line, 5, line.size()));
        } else if (code == "001") {
            TrfPlayer p;
            p.number          = numberIn(column(line, 5, 8));
            p.name            = std::string(column(line, 15, 47));
            p.rating          = numberIn(column(line, 49, 52));
            const auto points = column(line, 81, 84);
            try {
                p.points = points.empty() ? 0.0 : std::stod(std::string(points));
            } catch (const std::exception&) {
                p.points = 0;
            }
            for (std::size_t from = 92; from <= line.size(); from += 10) {
                TrfRound r;
                r.opponent         = numberIn(column(line, from, from + 3));
                const auto colour  = column(line, from + 5, from + 5);
                const auto outcome = column(line, from + 7, from + 7);
                r.colour           = colour.empty() ? 0 : colour[0];
                r.code             = outcome.empty() ? '-' : outcome[0];
                p.rounds.push_back(r);
            }
            if (p.number > 0) t.players.push_back(std::move(p));
        }
    }
    if (t.rounds == 0)
        for (const auto& p : t.players) t.rounds = std::max<int>(t.rounds, static_cast<int>(p.rounds.size()));
    for (auto& p : t.players)  // what a line has after its rounds is none of them
        if (static_cast<int>(p.rounds.size()) > t.rounds) p.rounds.resize(static_cast<std::size_t>(t.rounds));
    return t;
}

double trfPoints(char code) {
    switch (code) {
        case '1':
        case '+':
        case 'W':
        case 'U':
        case 'F':
            return 1.0;
        case '=':
        case 'H':
        case 'D':
            return 0.5;
        default:
            return 0.0;
    }
}

std::string swissStartText(int64_t ms) {
    const auto t = static_cast<std::time_t>(ms / 1000);
    const auto tm = localTime(t);
    return fmt::format("{:02}:{:02} on {} {} {}", tm.tm_hour, tm.tm_min, tm.tm_mday, MONTHS[tm.tm_mon % 12],
                       tm.tm_year + 1900);
}

KeptSwiss keepSwiss(const Trf& trf, std::string_view pgn, const std::string& name, const std::string& id,
                    const std::string& starts) {
    KeptSwiss out;
    out.event       = starts.empty() ? name : fmt::format("{}, {}", name, starts);
    const auto site = fmt::format("https://lichess.org/swiss/{}", id);
    std::map<std::string, std::string> shown;  // a player's TRF name (lowercase) -> as the games write it
    std::set<std::pair<int, int>> played;      // (start number, round) of a game kept
    int year          = 0;
    const auto setTag = [](study::ImportedGame& g, const char* key, const std::string& value) {
        for (auto& [k, v] : g.tags)
            if (k == key) {
                v = value;
                return;
            }
        g.tags.emplace_back(key, value);
    };
    const auto tagOf = [](const study::ImportedGame& g, const char* key) {
        for (const auto& [k, v] : g.tags)
            if (k == key) return v;
        return std::string();
    };
    for (const auto text : study::splitGames(pgn)) {
        study::ImportedGame g;
        if (!study::readImportedGame(text, g)) continue;
        const auto white = tagOf(g, "White"), black = tagOf(g, "Black");
        const auto* w = trf.player(white);
        const auto* b = trf.player(black);
        if (w) shown[lower(w->name)] = white;
        if (b) shown[lower(b->name)] = black;
        int round = 0;
        if (w && b)
            for (std::size_t r = 0; r < w->rounds.size(); ++r) {
                const auto& wr = w->rounds[r];
                if (wr.opponent != b->number || wr.colour != 'w') continue;
                if (played.count({w->number, static_cast<int>(r) + 1})) continue;
                round = static_cast<int>(r) + 1;
                break;
            }
        // The game's own address kept as its id; the tournament's as its site.
        if (tagOf(g, "GameId").empty()) {
            const auto own = tagOf(g, "Site");
            if (const auto slash = own.rfind('/'); slash != std::string::npos && slash + 1 < own.size())
                setTag(g, "GameId", own.substr(slash + 1));
        }
        setTag(g, "Event", out.event);
        setTag(g, "Site", site);
        if (round > 0) {
            setTag(g, "Round", std::to_string(round));
            played.insert({w->number, round});
            played.insert({b->number, round});
        } else {
            ++out.unmatched;
        }
        if (year == 0) {
            const auto date = tagOf(g, "Date");
            if (date.size() >= 4) year = numberIn(std::string_view(date).substr(0, 4));
        }
        out.games.push_back(std::move(g));
    }
    // The rounds without a game kept: byes, late joins, absences; a game paired whose export is missing.
    std::set<std::pair<int, int>> missing;  // (round, the white player's number) set by hand already
    const auto nameOf = [&](const TrfPlayer& p) {
        const auto it = shown.find(lower(p.name));
        return it == shown.end() ? p.name : it->second;
    };
    for (const auto& p : trf.players) {
        if (!shown.count(lower(p.name))) continue;  // in no game: in no crosstable either
        for (std::size_t r = 0; r < p.rounds.size(); ++r) {
            const int round = static_cast<int>(r) + 1;
            const auto& pr  = p.rounds[r];
            if (played.count({p.number, round}) || pr.code == 'Z') continue;
            const auto* opponent = pr.opponent > 0 ? trf.number(pr.opponent) : nullptr;
            if (opponent && (pr.colour == 'w' || pr.colour == 'b')) {
                const bool white = pr.colour == 'w';
                const int first  = white ? p.number : opponent->number;
                if (!missing.insert({round, first}).second) continue;
                const double mine = trfPoints(pr.code);
                const char result = mine == 1.0 ? (white ? '1' : '0') : mine == 0.5 ? '=' : (white ? '0' : '1');
                study::HandResult h;
                h.white  = white ? nameOf(p) : nameOf(*opponent);
                h.black  = white ? nameOf(*opponent) : nameOf(p);
                h.round  = round;
                h.result = result;
                out.hand.push_back(std::move(h));
                continue;
            }
            study::HandResult h;
            h.white  = nameOf(p);
            h.round  = round;
            h.points = trfPoints(pr.code);
            out.hand.push_back(std::move(h));
        }
    }
    if (year > 0 && !out.games.empty()) out.key = study::eventKey(out.event) + "|" + std::to_string(year);
    return out;
}

}  // namespace fcg::lichess
