// A database's tournaments (crosstable.hpp): the games of an event, its site and year, told from online games, their
// duplicates counted once; a tournament's games found again, and its players and games read for its crosstable.

#include <algorithm>
#include <bit>
#include <chrono>
#include <map>
#include <mutex>
#include <numeric>
#include <optional>
#include <tuple>
#include <unordered_map>

#include "crosstable.hpp"
#include "crosstable_detail.hpp"
#include "dossier.hpp"
#include "duplicates.hpp"
#include "fide_list.hpp"
#include "parallel.hpp"
#include "players.hpp"
#include "query.hpp"

namespace fcg::study {

using crosstable::roundHalfUp;

bool byeName(std::string_view name) {
    if (anonymousName(name)) return true;
    std::string letters;
    for (const char c : foldText(name))
        if (c >= 'a' && c <= 'z') letters += c;
    return letters == "bye" || letters == "freilos" || letters == "spielfrei";
}

std::string eventKey(std::string_view event) {
    const auto folded = foldText(event);
    std::vector<std::string> words;
    std::string word;
    const auto flush = [&] {
        if (!word.empty()) words.push_back(std::move(word));
        word.clear();
    };
    for (const char c : folded) {
        const auto u = static_cast<unsigned char>(c);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || u >= 0x80)
            word += c;
        else
            flush();
    }
    flush();
    // Its years left out ("2026", "2025/26"): the key gives the year apart.
    std::string out;
    bool after_year = false;
    for (const auto& w : words) {
        const bool digits = std::all_of(w.begin(), w.end(), [](char c) { return c >= '0' && c <= '9'; });
        const int value   = digits && w.size() <= 4 ? std::stoi(w) : -1;
        const bool year   = digits && w.size() == 4 && value >= 1800 && value <= 2100;
        if (year || (after_year && digits && w.size() == 2)) {
            after_year = year;
            continue;
        }
        after_year = false;
        out += (out.empty() ? "" : " ") + w;
    }
    return out.empty() ? folded : out;
}

namespace {

uint64_t mixKey(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h *= 0xff51afd7ed558ccdULL;
    return h ^ (h >> 33);
}

uint64_t textHash(std::string_view s) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const unsigned char c : s) h = (h ^ c) * 0x100000001b3ULL;
    return h;
}

// A game as the dedup reads it: its players' surnames and initials (duplicates.hpp's rule, the table of names' name
// when it has one), its round as written, its date and plies.
struct DedupGame {
    uint32_t index = 0;
    uint64_t white = 0, black = 0;
    std::string_view round;
    uint32_t date  = 0;
    uint32_t plies = 0;
    char result    = '*';
    GameId id      = 0;
};

uint64_t dedupKey(const Snapshot& snap, uint32_t name_id) {
    const auto shown = snap.shownName(name_id);
    if (anonymousName(shown)) return 0;
    const auto parts = nameParts(shown);
    if (parts.surname.empty()) return textHash(shown);
    return mixKey(textHash(parts.surname), static_cast<uint64_t>(static_cast<unsigned char>(parts.initial)) + 1);
}

bool roundKnown(std::string_view r) { return readRound(r).round > 0; }

// The groups of games that are one game come twice: the same players with the same colours, rounds and dates that
// can be one, and their rounds told (else the same length). Each group's games by index, the one kept first: the
// longest, then one with a result, then the oldest.
std::vector<std::vector<uint32_t>> dedupGroups(std::vector<DedupGame> games) {
    std::sort(games.begin(), games.end(), [](const DedupGame& a, const DedupGame& b) {
        if (a.white != b.white) return a.white < b.white;
        if (a.black != b.black) return a.black < b.black;
        return a.id < b.id;
    });
    std::vector<std::vector<uint32_t>> out;
    for (std::size_t i = 0; i < games.size();) {
        std::size_t j = i + 1;
        while (j < games.size() && games[j].white == games[i].white && games[j].black == games[i].black) ++j;
        if (j - i >= 2) {
            std::vector<std::size_t> parent(j - i);
            std::iota(parent.begin(), parent.end(), std::size_t{0});
            const auto find = [&](std::size_t x) {
                while (parent[x] != x) x = parent[x] = parent[parent[x]];
                return x;
            };
            const auto compare = [&](std::size_t a, std::size_t b) {
                const auto& x = games[a];
                const auto& y = games[b];
                if (!roundsCompatible(x.round, y.round) || !datesCompatible(x.date, y.date)) return;
                if (!(roundKnown(x.round) && roundKnown(y.round)) && x.plies != y.plies) return;
                parent[find(b - i)] = find(a - i);
            };
            constexpr std::size_t WHOLE = 64, WINDOW = 16;
            if (j - i <= WHOLE) {
                for (std::size_t a = i; a < j; ++a)
                    for (std::size_t b = a + 1; b < j; ++b) compare(a, b);
            } else {
                // The games of a pair by the hundred (online games, an engines' match): each with its neighbours by
                // round, length and date, where a game that came twice is.
                std::vector<std::size_t> order(j - i);
                std::iota(order.begin(), order.end(), i);
                std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                    const auto ra = readRound(games[a].round), rb = readRound(games[b].round);
                    return std::tie(ra.round, ra.board, games[a].plies, games[a].date, games[a].id) <
                           std::tie(rb.round, rb.board, games[b].plies, games[b].date, games[b].id);
                });
                for (std::size_t k = 0; k < order.size(); ++k)
                    for (std::size_t m = k + 1; m < order.size() && m <= k + WINDOW; ++m)
                        compare(std::min(order[k], order[m]), std::max(order[k], order[m]));
            }
            std::map<std::size_t, std::vector<std::size_t>> sets;
            for (std::size_t k = i; k < j; ++k) sets[find(k - i)].push_back(k);
            for (auto& [root, members] : sets) {
                if (members.size() < 2) continue;
                std::stable_sort(members.begin(), members.end(), [&](std::size_t a, std::size_t b) {
                    const auto& x = games[a];
                    const auto& y = games[b];
                    if (x.plies != y.plies) return x.plies > y.plies;
                    if ((x.result != '*') != (y.result != '*')) return x.result != '*';
                    return x.id < y.id;
                });
                std::vector<uint32_t> group;
                for (const auto k : members) group.push_back(games[k].index);
                out.push_back(std::move(group));
            }
        }
        i = j;
    }
    return out;
}

constexpr uint32_t NONE         = UINT32_MAX;
constexpr uint32_t TABLE_PLAYER = 0x80000000u;

// A game of a group of the list, read once.
struct ListGame {
    GameId id      = 0;
    uint32_t white = 0, black = 0;  // their players: the table's (TABLE_PLAYER | index), else the name id
    uint32_t white_name = 0, black_name = 0;
    uint32_t site = 0, event = 0;
    std::string_view round;
    uint32_t date      = 0;
    uint32_t plies     = 0;
    uint16_t white_elo = 0, black_elo = 0;
    char result = '*';
};

uint32_t playerKey(const Snapshot& snap, uint32_t name_id) {
    if (const auto p = snap.players()->playerOf(name_id)) return TABLE_PLAYER | static_cast<uint32_t>(*p);
    return name_id;
}

std::string monthKey(uint32_t date) { return date ? std::to_string(date >> 9) : std::string(); }

}  // namespace

TournamentList listTournaments(const std::shared_ptr<const Snapshot>& snapshot, TournamentProgress& progress,
                               const std::atomic<bool>& cancel, int threads) {
    const auto started = std::chrono::steady_clock::now();
    TournamentList out;
    const auto& snap        = *snapshot;
    const auto total        = snap.games();
    const uint32_t names    = snap.names();
    progress.total          = 3 * total + 1;
    progress.done           = 0;
    const auto stopped      = [&] { return cancel.load(std::memory_order_relaxed); };
    const int workers       = threadCount(threads);
    constexpr uint64_t PART = 65536;

    // The events written (a bit a name), on the machine's threads.
    std::vector<std::atomic<uint64_t>> used((static_cast<std::size_t>(names) + 64) / 64);
    {
        std::atomic<uint64_t> next{0};
        onThreads(static_cast<int>(std::min<uint64_t>(workers, total / PART + 1)), [&](int) {
            for (;;) {
                const auto from = next.fetch_add(PART);
                if (from >= total || stopped()) break;
                const auto to = std::min(total, from + PART);
                for (GameId id = from; id < to; ++id) {
                    if (snap.deleted(id)) continue;
                    const auto e = snap.rowIds(id).event;
                    if (e < names) used[e / 64].fetch_or(uint64_t{1} << (e % 64), std::memory_order_relaxed);
                }
                progress.done += to - from;
            }
        });
    }
    if (stopped()) {
        out.cancelled = true;
        return out;
    }
    // Each event's key: its spellings together ("Autumn Open 2026" and "Autumn Open" in one), a name of none left out.
    std::vector<uint32_t> canon(names, NONE);
    std::vector<std::string> keys;
    std::vector<std::vector<uint32_t>> spellings;
    {
        std::unordered_map<std::string, uint32_t> by_key;
        for (uint32_t w = 0; w < used.size(); ++w) {
            auto bits = used[w].load();
            while (bits) {
                const int b = std::countr_zero(bits);
                bits &= bits - 1;
                const auto id  = w * 64 + static_cast<uint32_t>(b);
                const auto txt = snap.name(id);
                if (txt.empty() || anonymousName(txt) || txt == "?" || txt == "-") continue;
                auto key = eventKey(txt);
                if (key.empty()) continue;
                const auto [it, added] = by_key.emplace(std::move(key), static_cast<uint32_t>(keys.size()));
                if (added) {
                    keys.push_back(it->first);
                    spellings.emplace_back();
                }
                canon[id] = it->second;
                spellings[it->second].push_back(id);
            }
        }
    }
    // The groups: an event's games of a year.
    struct Group {
        uint32_t canon = 0;
        int year       = 0;
        uint64_t count = 0;
        uint32_t first = 0, last = 0;  // dates
        uint32_t rep = 0;              // the group it joins
    };
    std::vector<Group> groups;
    std::vector<std::vector<uint32_t>> of_canon(keys.size());
    std::vector<uint32_t> last_group(keys.size(), NONE);  // the games of a tournament come one after another
    const auto groupOf = [&](uint32_t c, int year) -> uint32_t {
        if (const auto last = last_group[c]; last != NONE && groups[last].year == year) return last;
        for (const auto g : of_canon[c])
            if (groups[g].year == year) return last_group[c] = g;
        Group g;
        g.canon = c;
        g.year  = year;
        g.rep   = static_cast<uint32_t>(groups.size());
        groups.push_back(g);
        of_canon[c].push_back(g.rep);
        return last_group[c] = g.rep;
    };
    for (GameId id = 0; id < total; ++id) {
        if ((id & 0xFFFF) == 0) {
            progress.done = total + id;
            if (stopped()) {
                out.cancelled = true;
                return out;
            }
        }
        if (snap.deleted(id)) continue;
        const auto c = canon[snap.rowIds(id).event];
        if (c == NONE) {
            ++out.others;
            continue;
        }
        const auto date = snap.numbers(id).date;
        auto& g         = groups[groupOf(c, static_cast<int>(date >> 9))];
        ++g.count;
        if (date) {
            if (g.first == 0 || date < g.first) g.first = date;
            g.last = std::max(g.last, date);
        }
    }
    // An event's games without a date join its only year; a tournament from December to January stays one.
    for (const auto& list : of_canon) {
        std::vector<uint32_t> dated;
        std::optional<uint32_t> undated;
        for (const auto g : list) {
            if (groups[g].year == 0)
                undated = g;
            else
                dated.push_back(g);
        }
        std::sort(dated.begin(), dated.end(), [&](uint32_t a, uint32_t b) { return groups[a].year < groups[b].year; });
        for (std::size_t k = 1; k < dated.size(); ++k) {
            const auto& a       = groups[groups[dated[k - 1]].rep];
            const auto& b       = groups[dated[k]];
            const bool december = ((a.last >> 5) & 15) == 12, january = ((b.first >> 5) & 15) == 1;
            if (b.year == groups[dated[k - 1]].year + 1 && december && january &&
                (31 - (a.last & 31)) + (b.first & 31) <= 31)
                groups[dated[k]].rep = groups[dated[k - 1]].rep;
        }
        if (undated && dated.size() == 1) groups[*undated].rep = dated[0];
    }
    // Each group's last year, and whether its event's games without a date joined it.
    std::vector<int> year_to(groups.size(), 0);
    std::vector<bool> with_undated(groups.size(), false);
    for (const auto& g : groups) {
        year_to[g.rep] = std::max(year_to[g.rep], g.year);
        if (g.year == 0) with_undated[g.rep] = true;
    }
    // Each group's games, in their order.
    std::vector<uint64_t> start(groups.size() + 1, 0);
    for (const auto& g : groups) start[g.rep + 1] += g.count;
    for (std::size_t g = 1; g < start.size(); ++g) start[g] += start[g - 1];
    std::vector<uint32_t> ids(static_cast<std::size_t>(start.back()));
    {
        auto fill = start;
        for (GameId id = 0; id < total; ++id) {
            if ((id & 0xFFFF) == 0) {
                progress.done = 2 * total + id;
                if (stopped()) {
                    out.cancelled = true;
                    return out;
                }
            }
            if (snap.deleted(id)) continue;
            const auto c = canon[snap.rowIds(id).event];
            if (c == NONE) continue;
            const auto g = groups[groupOf(c, static_cast<int>(snap.numbers(id).date >> 9))].rep;
            ids[static_cast<std::size_t>(fill[g]++)] = static_cast<uint32_t>(id);
        }
    }
    out.games = total;

    // The tournaments of each group: its sites apart when their players are (two events of one name), each told
    // from games that are not one (online games: no rounds, and not each against each).
    std::mutex merge;
    std::atomic<std::size_t> next{0};
    // What each name says, read once whatever thread asks first (the same value whoever computes it): its key for the
    // duplicates, and whether it is a bye's.
    constexpr uint64_t NOT_READ = ~uint64_t{0};
    std::vector<std::atomic<uint64_t>> dedup_keys(names);
    for (auto& k : dedup_keys) k.store(NOT_READ, std::memory_order_relaxed);
    std::vector<std::atomic<uint8_t>> byes(names);  // 0 not read, 1 a player, 2 a bye
    onThreads(static_cast<int>(std::min<std::size_t>(workers, groups.size() / 64 + 1)), [&](int) {
        std::vector<TournamentEntry> mine;
        uint64_t loose = 0;
        for (;;) {
            const auto g = next.fetch_add(1);
            if (g >= groups.size() || stopped()) break;
            if (groups[g].rep != g) continue;
            const auto from = start[g], to = start[g + 1];
            if (to - from < 2) {
                loose += to - from;
                continue;
            }
            std::vector<ListGame> games;
            games.reserve(static_cast<std::size_t>(to - from));
            for (auto k = from; k < to; ++k) {
                const GameId id    = ids[static_cast<std::size_t>(k)];
                const auto names_  = snap.rowIds(id);
                const auto numbers = snap.numbers(id);
                ListGame lg;
                lg.id         = id;
                lg.white_name = names_.white;
                lg.black_name = names_.black;
                lg.white      = playerKey(snap, names_.white);
                lg.black      = playerKey(snap, names_.black);
                lg.site       = names_.site;
                lg.event      = names_.event;
                lg.round      = snap.name(names_.round);
                lg.date       = numbers.date;
                lg.plies      = numbers.plies;
                lg.white_elo  = numbers.white_elo;
                lg.black_elo  = numbers.black_elo;
                lg.result     = numbers.result;
                games.push_back(lg);
            }
            const auto isBye = [&](uint32_t name_id) {
                if (name_id >= names) return false;
                auto v = byes[name_id].load(std::memory_order_relaxed);
                if (v == 0) {
                    v = byeName(snap.name(name_id)) ? 2 : 1;
                    byes[name_id].store(v, std::memory_order_relaxed);
                }
                return v == 2;
            };
            // The sites apart: a site joins another through a player of both.
            std::vector<uint32_t> site_ids;
            for (const auto& lg : games) site_ids.push_back(lg.site);
            std::sort(site_ids.begin(), site_ids.end());
            site_ids.erase(std::unique(site_ids.begin(), site_ids.end()), site_ids.end());
            std::vector<std::size_t> parent(site_ids.size());
            std::iota(parent.begin(), parent.end(), std::size_t{0});
            const auto find = [&](std::size_t x) {
                while (parent[x] != x) x = parent[x] = parent[parent[x]];
                return x;
            };
            const auto siteIndex = [&](uint32_t s) {
                return static_cast<std::size_t>(std::lower_bound(site_ids.begin(), site_ids.end(), s) -
                                                site_ids.begin());
            };
            if (site_ids.size() > 1) {
                std::unordered_map<uint32_t, std::size_t> seen;
                for (const auto& lg : games)
                    for (const auto p : {lg.white, lg.black}) {
                        if (!(p & TABLE_PLAYER) && isBye(p)) continue;
                        const auto [it, added] = seen.emplace(p, siteIndex(lg.site));
                        if (!added) parent[find(siteIndex(lg.site))] = find(it->second);
                    }
            }
            std::map<std::size_t, std::vector<uint32_t>> clusters;  // root -> sites
            for (std::size_t s = 0; s < site_ids.size(); ++s) clusters[find(s)].push_back(site_ids[s]);
            for (const auto& [root, sites] : clusters) {
                const bool split = clusters.size() > 1;
                std::vector<const ListGame*> mine_games;
                for (const auto& lg : games)
                    if (!split || find(siteIndex(lg.site)) == root) mine_games.push_back(&lg);
                // Its games, the duplicates counted once; then its players, those of the games kept.
                const auto dedupOf = [&](uint32_t name_id) {
                    if (name_id >= names) return uint64_t{0};
                    auto v = dedup_keys[name_id].load(std::memory_order_relaxed);
                    if (v == NOT_READ) {
                        v = dedupKey(snap, name_id);
                        dedup_keys[name_id].store(v, std::memory_order_relaxed);
                    }
                    return v;
                };
                uint32_t rounds_told = 0;
                // The spellings of its event and its sites, counted: a few each.
                std::vector<std::pair<uint32_t, uint32_t>> event_count, site_count;
                const auto tally = [](std::vector<std::pair<uint32_t, uint32_t>>& counts, uint32_t id) {
                    for (auto& [k, c] : counts)
                        if (k == id) {
                            ++c;
                            return;
                        }
                    counts.emplace_back(id, 1);
                };
                uint32_t first_date = 0, last_date = 0;
                std::vector<DedupGame> dedup;
                for (std::size_t k = 0; k < mine_games.size(); ++k) {
                    const auto* lg = mine_games[k];
                    if (readRound(lg->round).round > 0) ++rounds_told;
                    tally(event_count, lg->event);
                    if (lg->site) tally(site_count, lg->site);
                    if (lg->date) {
                        if (first_date == 0 || lg->date < first_date) first_date = lg->date;
                        last_date = std::max(last_date, lg->date);
                    }
                    DedupGame d;
                    d.index  = static_cast<uint32_t>(k);
                    d.white  = dedupOf(lg->white_name);
                    d.black  = dedupOf(lg->black_name);
                    d.round  = lg->round;
                    d.date   = lg->date;
                    d.plies  = lg->plies;
                    d.result = lg->result;
                    d.id     = lg->id;
                    dedup.push_back(d);
                }
                std::vector<bool> gone(mine_games.size(), false);
                uint32_t twice = 0;
                for (const auto& group : dedupGroups(dedup)) {
                    for (std::size_t k = 1; k < group.size(); ++k) gone[group[k]] = true;
                    twice += static_cast<uint32_t>(group.size() - 1);
                }
                std::unordered_map<uint32_t, uint32_t> local;
                std::vector<std::pair<int, uint16_t>> first_rating;  // (round, rating) of each player
                const auto localOf = [&](uint32_t key, uint32_t name_id) -> uint32_t {
                    if (!(key & TABLE_PLAYER) && isBye(name_id)) return NO_PLAYER;
                    const auto [it, added] = local.emplace(key, static_cast<uint32_t>(local.size()));
                    if (added) first_rating.emplace_back(INT32_MAX, 0);
                    return it->second;
                };
                std::vector<CrossGame> kept;
                for (std::size_t k = 0; k < mine_games.size(); ++k) {
                    if (gone[k]) continue;
                    const auto* lg = mine_games[k];
                    CrossGame c;
                    c.white  = localOf(lg->white, lg->white_name);
                    c.black  = localOf(lg->black, lg->black_name);
                    c.round  = readRound(lg->round);
                    c.result = lg->result;
                    c.plies  = lg->plies;
                    c.date   = lg->date;
                    c.ref    = lg->id;
                    // Each player's rating: the first their games give, by round.
                    const int r = c.round.round > 0 ? c.round.round : INT32_MAX - 1;
                    for (const bool white : {true, false}) {
                        const auto p   = white ? c.white : c.black;
                        const auto elo = white ? lg->white_elo : lg->black_elo;
                        if (p == NO_PLAYER || elo == 0) continue;
                        auto& fr = first_rating[p];
                        if (r < fr.first || fr.second == 0) fr = {r, elo};
                    }
                    kept.push_back(c);
                }
                const auto players = static_cast<uint32_t>(local.size());
                if (players < 3 || kept.size() < 2) {
                    loose += mine_games.size();
                    continue;
                }
                CrossOptions options;
                // A team event: its first games give teams.
                for (std::size_t k = 0; k < std::min<std::size_t>(mine_games.size(), 3); ++k)
                    if (!snap.tag(mine_games[k]->id, "WhiteTeam").empty() &&
                        !snap.tag(mine_games[k]->id, "BlackTeam").empty())
                        options.team = true;
                TournamentEntry e;
                e.shape = tournamentShape(players, kept, options);
                // Online games have no rounds and do not meet each other all: not a tournament.
                const bool all_met = e.shape.system.system == TournamentSystem::RoundRobin && !e.shape.uncertain;
                if (rounds_told * 2 < mine_games.size() && !all_met && !options.team) {
                    loose += mine_games.size();
                    continue;
                }
                e.duplicates = twice;
                int64_t sum  = 0;
                int rated    = 0;
                for (const auto& fr : first_rating)
                    if (fr.second > 0) {
                        sum += fr.second;
                        ++rated;
                    }
                e.mean_elo      = rated ? roundHalfUp(static_cast<double>(sum) / rated) : 0;
                e.first_date    = first_date;
                e.last_date     = last_date;
                const auto most = [](const std::vector<std::pair<uint32_t, uint32_t>>& counts) {
                    uint32_t best = 0, id = 0;
                    for (const auto& [k, n] : counts)
                        if (n > best) {
                            best = n;
                            id   = k;
                        }
                    return id;
                };
                e.event          = most(event_count);
                e.site           = most(site_count);
                const auto& head = groups[g];
                e.ref.events     = spellings[head.canon];
                std::sort(e.ref.events.begin(), e.ref.events.end());
                e.ref.year_from = head.year;
                e.ref.year_to   = std::max(head.year, year_to[g]);
                e.ref.undated   = with_undated[g];
                if (split) e.ref.sites = sites;
                e.key = keys[head.canon] + "|" + std::to_string(head.year);
                if (split) e.key += "|" + eventKey(snap.name(e.site));
                (void)monthKey;
                mine.push_back(std::move(e));
            }
        }
        std::lock_guard lock(merge);
        out.others += loose;
        for (auto& e : mine) out.tournaments.push_back(std::move(e));
    });
    if (stopped()) {
        out.cancelled = true;
        out.tournaments.clear();
        return out;
    }
    std::sort(out.tournaments.begin(), out.tournaments.end(), [](const TournamentEntry& a, const TournamentEntry& b) {
        if (a.mean_elo != b.mean_elo) return a.mean_elo > b.mean_elo;
        if (a.first_date != b.first_date) return a.first_date > b.first_date;
        return a.key < b.key;
    });
    // Keys told apart.
    {
        std::map<std::string, int> seen;
        for (auto& e : out.tournaments)
            if (const int n = seen[e.key]++; n > 0) e.key += "#" + std::to_string(n + 1);
    }
    progress.done = progress.total.load();
    out.seconds   = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return out;
}

std::vector<GameId> tournamentGames(const Snapshot& snapshot, const TournamentRef& ref,
                                    const std::atomic<bool>& cancel) {
    std::vector<GameId> out;
    const auto total = snapshot.games();
    for (GameId id = 0; id < total; ++id) {
        if ((id & 0xFFFF) == 0 && cancel.load(std::memory_order_relaxed)) return {};
        if (snapshot.deleted(id)) continue;
        const auto names = snapshot.rowIds(id);
        if (!std::binary_search(ref.events.begin(), ref.events.end(), names.event)) continue;
        const int year = static_cast<int>(snapshot.numbers(id).date >> 9);
        if (year == 0 ? !ref.undated : (year < ref.year_from || year > ref.year_to)) continue;
        if (!ref.sites.empty() && !std::binary_search(ref.sites.begin(), ref.sites.end(), names.site)) continue;
        out.push_back(id);
    }
    return out;
}

TournamentInput tournamentInput(const Snapshot& snapshot, std::span<const GameId> ids, const FideList* list) {
    TournamentInput in;
    const auto& snap  = snapshot;
    const auto& table = *snap.players();
    // The games, the duplicates counted once.
    struct Raw {
        Snapshot::NameIds names;
        HeaderNumbers numbers;
    };
    std::vector<Raw> raw;
    std::vector<DedupGame> dedup;
    std::unordered_map<uint32_t, uint64_t> dedup_keys;
    const auto dedupOf = [&](uint32_t name_id) {
        const auto [it, added] = dedup_keys.emplace(name_id, 0);
        if (added) it->second = dedupKey(snap, name_id);
        return it->second;
    };
    std::map<int, uint32_t> years;
    for (const auto id : ids) {
        Raw r{snap.rowIds(id), snap.numbers(id)};
        if (r.numbers.date >> 9) ++years[static_cast<int>(r.numbers.date >> 9)];
        DedupGame d;
        d.index  = static_cast<uint32_t>(raw.size());
        d.white  = dedupOf(r.names.white);
        d.black  = dedupOf(r.names.black);
        d.round  = snap.name(r.names.round);
        d.date   = r.numbers.date;
        d.plies  = r.numbers.plies;
        d.result = r.numbers.result;
        d.id     = id;
        dedup.push_back(d);
        raw.push_back(r);
    }
    std::vector<bool> gone(raw.size(), false);
    for (const auto& group : dedupGroups(dedup)) {
        TournamentInput::Twice twice;
        const auto kept_id    = ids[group[0]];
        const auto kept_moves = snap.moves(kept_id);
        for (const auto k : group) {
            twice.games.push_back(ids[k]);
            if (k == group[0]) continue;
            gone[k]          = true;
            const auto moves = snap.moves(ids[k]);
            const auto& a    = moves.size() <= kept_moves.size() ? moves : kept_moves;
            const auto& b    = moves.size() <= kept_moves.size() ? kept_moves : moves;
            if (!std::equal(a.begin(), a.end(), b.begin())) twice.same_moves = false;
            const char r0 = raw[group[0]].numbers.result, rk = raw[k].numbers.result;
            if (r0 != '*' && rk != '*' && r0 != rk) twice.same_results = false;
        }
        in.twice.push_back(std::move(twice));
    }
    // The players of the games kept.
    std::unordered_map<uint32_t, uint32_t> local;  // player key -> index
    std::vector<std::pair<int, uint16_t>> first_rating;
    std::vector<uint32_t> keys;
    const auto localOf = [&](uint32_t name_id) -> uint32_t {
        if (byeName(snap.name(name_id)) && !table.playerOf(name_id)) return NO_PLAYER;
        const auto key         = playerKey(snap, name_id);
        const auto [it, added] = local.emplace(key, static_cast<uint32_t>(in.players.size()));
        if (added) {
            CrossPlayer p;
            p.name = std::string(snap.shownName(name_id));
            if (const auto* known = table.of(name_id)) {
                p.federation = known->federation;
                p.title      = known->title;
                p.born       = known->born;
                p.fide       = known->fide;
                p.spellings  = known->spellings;
            } else {
                p.spellings = {name_id};
            }
            in.players.push_back(std::move(p));
            first_rating.emplace_back(INT32_MAX, 0);
            keys.push_back(key);
        }
        return it->second;
    };
    for (std::size_t k = 0; k < raw.size(); ++k) {
        if (gone[k]) continue;
        const auto& r = raw[k];
        CrossGame g;
        g.white         = localOf(r.names.white);
        g.black         = localOf(r.names.black);
        g.round         = readRound(snap.name(r.names.round));
        g.result        = r.numbers.result;
        g.plies         = r.numbers.plies;
        g.date          = r.numbers.date;
        g.ref           = ids[k];
        const int round = g.round.round > 0 ? g.round.round : INT32_MAX - 1;
        for (const bool white : {true, false}) {
            const auto p   = white ? g.white : g.black;
            const auto elo = white ? r.numbers.white_elo : r.numbers.black_elo;
            if (p == NO_PLAYER || elo == 0) continue;
            auto& fr = first_rating[p];
            if (round < fr.first || fr.second == 0) fr = {round, elo};
        }
        in.games.push_back(g);
    }
    // Its rating: classical unless its time control or its event's words say rapid or blitz.
    if (!ids.empty()) {
        const auto row   = snap.row(ids[0]);
        const auto speed = gameSpeed(row.time_control, row.event, false);
        in.kind          = speed == Speed::Blitz || speed == Speed::Bullet ? RatingKind::Blitz
                           : speed == Speed::Rapid                         ? RatingKind::Rapid
                                                                           : RatingKind::Standard;
        for (std::size_t k = 0; k < std::min<std::size_t>(ids.size(), 3); ++k)
            if (!snap.tag(ids[k], "WhiteTeam").empty() && !snap.tag(ids[k], "BlackTeam").empty()) in.team = true;
    }
    uint32_t most = 0;
    for (const auto& [year, count] : years)
        if (count > most) {
            most    = count;
            in.year = year;
        }
    // The players' ratings: their games', else the table of names'; their K from FIDE's list of the year or the next.
    int list_year = 0;
    if (list) list_year = std::atoi(list->info().month.substr(0, 4).c_str());
    for (std::size_t p = 0; p < in.players.size(); ++p) {
        auto& player        = in.players[p];
        player.elo          = first_rating[p].second;
        const Player* known = (keys[p] & TABLE_PLAYER) ? &table.players()[keys[p] & ~TABLE_PLAYER] : nullptr;
        if (player.elo == 0 && known)
            player.elo = in.kind == RatingKind::Rapid   ? known->rapid
                         : in.kind == RatingKind::Blitz ? known->blitz
                                                        : known->standard;
        if (list && player.fide > 0 && in.year > 0 && list_year >= in.year && list_year <= in.year + 1)
            if (const auto at = list->byId(static_cast<uint32_t>(player.fide))) {
                const auto fp = list->player(*at);
                player.k      = in.kind == RatingKind::Rapid   ? fp.k_rapid
                                : in.kind == RatingKind::Blitz ? fp.k_blitz
                                                               : fp.k_standard;
                if (player.born == 0) player.born = fp.born;
            }
    }
    return in;
}

}  // namespace fcg::study
