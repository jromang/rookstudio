#include "names.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <unordered_map>

#include "players.hpp"

namespace fcg::study {

std::size_t NamesAnalysis::count(NameMatch match) const {
    return static_cast<std::size_t>(
        std::count_if(proposals.begin(), proposals.end(), [&](const PlayerProposal& p) { return p.match == match; }));
}

std::size_t NamesAnalysis::bringing() const {
    return static_cast<std::size_t>(std::count_if(proposals.begin(), proposals.end(), [](const PlayerProposal& p) {
        return p.match == NameMatch::Auto && (p.spellings.size() > 1 || !p.candidates.empty() || p.player);
    }));
}

namespace {

// How far a player's ratings are from the games' (their mean): 0 when the games give none.
double distance(const FidePlayer& p, double mean) {
    if (mean <= 0) return 0;
    double best = 1e9;
    for (const int r : {p.standard, p.rapid, p.blitz})
        if (r > 0) best = std::min(best, std::abs(r - mean));
    return best;
}

// The fullest of a group's spellings: a given name in full over an initial, then the most letters, then the most
// played.
std::size_t fullest(const Snapshot& snap, const std::vector<Spelling>& spellings, const std::vector<std::size_t>& of) {
    std::size_t best = of.front();
    auto score       = [&](std::size_t s) {
        const auto name  = snap.name(spellings[s].name);
        const auto parts = nameParts(name);
        const int given  = parts.given.empty() ? 0 : givenInitialOnly(parts.given) ? 1 : 2;
        return std::tuple<int, std::size_t, uint64_t>{given, parts.surname.size() + parts.given.size(),
                                                      spellings[s].games};
    };
    for (const auto s : of)
        if (score(s) > score(best)) best = s;
    return best;
}

}  // namespace

NamesAnalysis analyseNames(const std::shared_ptr<const Snapshot>& snapshot, const FideList* list,
                           NamesProgress& progress, const std::atomic<bool>& cancel) {
    const auto started = std::chrono::steady_clock::now();
    NamesAnalysis out;
    const auto& snap  = *snapshot;
    const auto& table = *snap.players();
    if (list) out.list_month = list->info().month;
    const auto n   = snap.games();
    progress.total = n + 1;
    progress.done  = 0;

    // The spellings and what their games say.
    std::unordered_map<uint32_t, std::size_t> at;  // name id -> spelling
    struct Tally {
        double elo_sum = 0;
        uint64_t rated = 0;
        std::map<uint32_t, uint64_t> fide;
        std::map<std::string_view, uint64_t> sources;
    };
    std::vector<Tally> tallies;
    for (GameId id = 0; id < n; ++id) {
        if ((id & 0x3FFF) == 0) {
            progress.done = id;
            if (cancel.load(std::memory_order_relaxed)) {
                out.cancelled = true;
                return out;
            }
        }
        if (snap.deleted(id)) continue;
        const auto ids     = snap.rowIds(id);
        const auto numbers = snap.numbers(id);
        for (const bool white : {true, false}) {
            const auto name_id = white ? ids.white : ids.black;
            auto it            = at.find(name_id);
            if (it == at.end()) {
                if (anonymousName(snap.name(name_id))) continue;
                it = at.emplace(name_id, out.spellings.size()).first;
                Spelling s;
                s.name   = name_id;
                s.sample = id;
                out.spellings.push_back(s);
                tallies.emplace_back();
            }
            auto& s       = out.spellings[it->second];
            auto& t       = tallies[it->second];
            const int elo = white ? numbers.white_elo : numbers.black_elo;
            ++s.games;
            if (const int year = static_cast<int>(numbers.date >> 9); year > 0) {
                s.year_min = s.year_min ? std::min(s.year_min, year) : year;
                s.year_max = std::max(s.year_max, year);
            }
            if (elo > 0) {
                s.elo_min = s.elo_min ? std::min(s.elo_min, elo) : elo;
                s.elo_max = std::max(s.elo_max, elo);
                t.elo_sum += elo;
                ++t.rated;
            }
            const auto fide = snap.tag(id, white ? "WhiteFideId" : "BlackFideId");
            if (!fide.empty()) {
                const auto v = std::strtoul(fide.c_str(), nullptr, 10);
                if (v > 0 && v < UINT32_MAX) ++t.fide[static_cast<uint32_t>(v)];
            }
            ++t.sources[snap.source(id)];
        }
    }
    for (std::size_t i = 0; i < out.spellings.size(); ++i) {
        auto& s       = out.spellings[i];
        const auto& t = tallies[i];
        s.elo_mean    = t.rated ? t.elo_sum / static_cast<double>(t.rated) : 0;
        uint64_t most = 0;
        for (const auto& [id, count] : t.fide)
            if (count > most) {
                most   = count;
                s.fide = id;
            }
        most = 0;
        for (const auto& [source, count] : t.sources)
            if (count > most) {
                most     = count;
                s.source = std::string(source);
            }
    }
    tallies.clear();

    // The table's players first: each gathers its spellings.
    std::vector<int> group_of(out.spellings.size(), -1);
    std::vector<std::vector<std::size_t>> groups;
    std::vector<std::optional<std::size_t>> group_player;
    std::vector<uint32_t> group_fide;
    std::map<std::size_t, std::size_t> player_group;
    for (std::size_t i = 0; i < out.spellings.size(); ++i) {
        const auto p = table.playerOf(out.spellings[i].name);
        if (!p) continue;
        auto g = player_group.find(*p);
        if (g == player_group.end()) {
            g = player_group.emplace(*p, groups.size()).first;
            groups.emplace_back();
            group_player.push_back(*p);
            group_fide.push_back(static_cast<uint32_t>(table.players()[*p].fide));
        }
        groups[g->second].push_back(i);
        group_of[i] = static_cast<int>(g->second);
    }
    // The spellings whose games give a FIDE id: with the player of that id, or together.
    std::map<uint32_t, std::size_t> by_fide;
    for (std::size_t g = 0; g < groups.size(); ++g)
        if (group_fide[g]) by_fide.emplace(group_fide[g], g);
    for (std::size_t i = 0; i < out.spellings.size(); ++i) {
        if (group_of[i] >= 0 || !out.spellings[i].fide) continue;
        auto g = by_fide.find(out.spellings[i].fide);
        if (g == by_fide.end()) {
            g = by_fide.emplace(out.spellings[i].fide, groups.size()).first;
            groups.emplace_back();
            group_player.emplace_back();
            group_fide.push_back(out.spellings[i].fide);
        }
        groups[g->second].push_back(i);
        group_of[i] = static_cast<int>(g->second);
    }
    // The others by surname and given names: a full given name joins the group of the same name; an initial joins
    // the one group of its surname whose given name starts with it, else stays alone.
    std::map<std::string, std::vector<std::size_t>> by_surname;  // surname -> groups having it
    std::vector<NameParts> all_parts(out.spellings.size());
    for (std::size_t i = 0; i < out.spellings.size(); ++i)
        all_parts[i] = nameParts(snap.shownName(out.spellings[i].name));
    const auto parts_of = [&](std::size_t i) -> const NameParts& { return all_parts[i]; };
    for (std::size_t g = 0; g < groups.size(); ++g) by_surname[parts_of(groups[g].front()).surname].push_back(g);
    std::vector<std::size_t> rest;
    for (std::size_t i = 0; i < out.spellings.size(); ++i)
        if (group_of[i] < 0) rest.push_back(i);
    // Full given names first, so that the initials find them.
    std::stable_sort(rest.begin(), rest.end(), [&](std::size_t a, std::size_t b) {
        return !givenInitialOnly(parts_of(a).given) && givenInitialOnly(parts_of(b).given);
    });
    for (const auto i : rest) {
        const auto& parts = parts_of(i);
        if (parts.surname.empty()) {
            group_of[i] = static_cast<int>(groups.size());
            groups.push_back({i});
            group_player.emplace_back();
            group_fide.push_back(0);
            continue;
        }
        auto& same = by_surname[parts.surname];
        std::vector<std::size_t> fits;
        for (const auto g : same) {
            const auto& other = parts_of(groups[g].front());
            if (givenCompatible(parts.given, other.given) && !parts.given.empty() && !other.given.empty())
                fits.push_back(g);
        }
        if (fits.size() == 1) {
            groups[fits.front()].push_back(i);
            group_of[i] = static_cast<int>(fits.front());
        } else {
            group_of[i] = static_cast<int>(groups.size());
            same.push_back(groups.size());
            groups.push_back({i});
            group_player.emplace_back();
            group_fide.push_back(0);
        }
    }

    // Each group a proposal, matched with the list.
    progress.total = n + groups.size() + 1;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        if ((g & 0xFF) == 0) progress.done = n + g;
        if (cancel.load(std::memory_order_relaxed)) {
            out.cancelled = true;
            break;
        }
        PlayerProposal p;
        p.spellings = groups[g];
        std::sort(p.spellings.begin(), p.spellings.end(),
                  [&](std::size_t a, std::size_t b) { return out.spellings[a].games > out.spellings[b].games; });
        p.player       = group_player[g];
        p.fide         = group_fide[g];
        double elo_sum = 0, rated = 0;
        for (const auto s : p.spellings) {
            const auto& sp = out.spellings[s];
            p.games += sp.games;
            if (sp.year_min) p.year_min = p.year_min ? std::min(p.year_min, sp.year_min) : sp.year_min;
            p.year_max = std::max(p.year_max, sp.year_max);
            if (sp.elo_min) p.elo_min = p.elo_min ? std::min(p.elo_min, sp.elo_min) : sp.elo_min;
            p.elo_max = std::max(p.elo_max, sp.elo_max);
            if (sp.elo_mean > 0) {
                elo_sum += sp.elo_mean * static_cast<double>(sp.games);
                rated += static_cast<double>(sp.games);
            }
        }
        const double mean = rated > 0 ? elo_sum / rated : 0;
        const auto best   = fullest(snap, out.spellings, p.spellings);
        p.name            = std::string(snap.name(out.spellings[best].name));
        if (p.player) {
            // A player of the table: done, unless spellings of its games' FIDE id join it.
            const bool all_in = std::all_of(p.spellings.begin(), p.spellings.end(), [&](std::size_t s) {
                return table.playerOf(out.spellings[s].name) == p.player;
            });
            p.match           = all_in ? NameMatch::Done : NameMatch::Auto;
            p.reason          = all_in ? NameReason::Table : NameReason::FideIdInGames;
            p.name            = table.players()[*p.player].name;
            out.proposals.push_back(std::move(p));
            continue;
        }
        if (p.fide) {
            p.match  = NameMatch::Auto;
            p.reason = NameReason::FideIdInGames;
            if (list)
                if (const auto at_id = list->byId(p.fide)) {
                    p.candidates.push_back(*at_id);
                    p.name = std::string(list->player(*at_id).name);
                }
            out.proposals.push_back(std::move(p));
            continue;
        }
        if (!list) {
            p.match  = NameMatch::None;
            p.reason = NameReason::NoList;
            out.proposals.push_back(std::move(p));
            continue;
        }
        // The list's players of that name; else of that name as another alphabet writes it. A name of one word (an
        // account's, "Bobby92") says no person: kept as it is.
        const auto parts = nameParts(snap.name(out.spellings[best].name));
        if (parts.given.empty()) {
            p.match  = NameMatch::None;
            p.reason = NameReason::NotOnList;
            out.proposals.push_back(std::move(p));
            continue;
        }
        // A namesake born after the games were played, or too young to play them, is not the player.
        const auto compatible = [&](std::size_t c) {
            const auto card = list->player(c);
            if (card.born && p.year_min && p.year_min < card.born + 5) return false;
            const auto given = nameParts(card.name).given;
            return !given.empty() && givenCompatible(parts.given, given);  // a name of one word on the list too
        };
        std::vector<std::size_t> found;
        for (const auto c : list->bySurname(parts.surname))
            if (compatible(c)) found.push_back(c);
        bool transliterated = false;
        if (found.empty()) {
            for (const auto c : list->bySkeleton(nameSkeleton(parts.surname)))
                if (compatible(c)) found.push_back(c);
            transliterated = !found.empty();
        }
        std::stable_sort(found.begin(), found.end(), [&](std::size_t a, std::size_t b) {
            return distance(list->player(a), mean) < distance(list->player(b), mean);
        });
        p.candidates = found;
        if (found.empty()) {
            p.match  = NameMatch::None;
            p.reason = NameReason::NotOnList;
        } else if (found.size() == 1 &&
                   (!transliterated || (mean > 0 && distance(list->player(found[0]), mean) <= 150)) &&
                   (!givenInitialOnly(parts.given) || (mean > 0 && distance(list->player(found[0]), mean) <= 200))) {
            // A single player: brought together at once, unless only an initial and no rating near his say it is him.
            p.match  = NameMatch::Auto;
            p.reason = transliterated ? NameReason::Transliteration : NameReason::OneOnList;
            p.name   = std::string(list->player(found[0]).name);
        } else {
            p.match  = NameMatch::Confirm;
            p.reason = NameReason::Several;
        }
        out.proposals.push_back(std::move(p));
    }
    std::stable_sort(out.proposals.begin(), out.proposals.end(), [](const PlayerProposal& a, const PlayerProposal& b) {
        const auto rank = [](NameMatch m) {
            return m == NameMatch::Confirm ? 0 : m == NameMatch::Auto ? 1 : m == NameMatch::None ? 2 : 3;
        };
        if (rank(a.match) != rank(b.match)) return rank(a.match) < rank(b.match);
        return a.games > b.games;
    });
    progress.done = progress.total.load();
    out.seconds   = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return out;
}

std::optional<PlayerTable> joinNewSpellings(const Snapshot& snap, GameId from) {
    const auto& table = *snap.players();
    if (table.players().empty() || from >= snap.games()) return std::nullopt;
    // The table's players by FIDE id, and by surname and first given name (written in full).
    std::map<int64_t, std::size_t> by_fide;
    std::map<std::pair<std::string, std::string>, std::vector<std::size_t>> by_name;
    const auto first = [](const std::string& given) { return given.substr(0, given.find(' ')); };
    for (std::size_t p = 0; p < table.players().size(); ++p) {
        const auto& player = table.players()[p];
        if (player.fide) by_fide.emplace(player.fide, p);
        const auto parts = nameParts(player.name);
        if (!parts.surname.empty() && !givenInitialOnly(parts.given) && !parts.given.empty())
            by_name[{parts.surname, nameSkeleton(first(parts.given))}].push_back(p);
    }
    std::map<uint32_t, std::size_t> joining;  // a spelling, the player it joins
    for (GameId id = from; id < snap.games(); ++id) {
        if (snap.deleted(id)) continue;
        const auto ids = snap.rowIds(id);
        for (const bool white : {true, false}) {
            const auto name = white ? ids.white : ids.black;
            if (table.playerOf(name) || joining.count(name) || anonymousName(snap.name(name))) continue;
            const auto fide = std::strtoll(snap.tag(id, white ? "WhiteFideId" : "BlackFideId").c_str(), nullptr, 10);
            if (const auto it = by_fide.find(fide); fide > 0 && it != by_fide.end()) {
                joining[name] = it->second;
                continue;
            }
            const auto parts = nameParts(snap.name(name));
            if (parts.given.empty() || givenInitialOnly(parts.given)) continue;
            const auto it = by_name.find({parts.surname, nameSkeleton(first(parts.given))});
            if (it != by_name.end() && it->second.size() == 1) joining[name] = it->second.front();
        }
    }
    if (joining.empty()) return std::nullopt;
    auto players = table.players();
    for (const auto& [name, p] : joining) players[p].spellings.push_back(name);
    return PlayerTable(std::move(players), table.apart());
}

PlayerTable bringTogether(const PlayerTable& before, const NamesAnalysis& analysis,
                          const std::vector<Bringing>& bringing, const FideList* list, const Snapshot& snapshot) {
    auto players = before.players();
    // A name id in one player at most: those brought into a new player leave the one they were in.
    std::unordered_map<uint32_t, std::size_t> owner;
    for (std::size_t p = 0; p < players.size(); ++p)
        for (const auto id : players[p].spellings) owner.emplace(id, p);
    const auto take = [&](uint32_t name, std::size_t into) {
        if (const auto it = owner.find(name); it != owner.end()) {
            if (it->second == into) return;
            std::erase(players[it->second].spellings, name);
        }
        players[into].spellings.push_back(name);
        owner[name] = into;
    };
    for (const auto& b : bringing) {
        if (b.proposal >= analysis.proposals.size()) continue;
        const auto& p = analysis.proposals[b.proposal];
        std::size_t into;
        if (p.player && *p.player < players.size()) {
            into = *p.player;
        } else {
            into = players.size();
            players.emplace_back();
            players.back().name = p.name;
            players.back().how  = p.fide ? "fide-id" : "hand";
            players.back().fide = p.fide;
        }
        auto& player = players[into];
        if (b.candidate && *b.candidate < p.candidates.size() && list) {
            const auto card   = list->player(p.candidates[*b.candidate]);
            player.name       = std::string(card.name);
            player.fide       = card.id;
            player.federation = card.federation;
            player.title      = card.title;
            player.born       = card.born;
            player.standard   = card.standard;
            player.rapid      = card.rapid;
            player.blitz      = card.blitz;
            player.list       = analysis.list_month;
            if (!p.fide) player.how = "list";
        }
        for (const auto s : p.spellings) take(analysis.spellings[s].name, into);
    }
    (void)snapshot;
    std::erase_if(players, [](const Player& p) { return p.spellings.empty(); });
    return PlayerTable(std::move(players), before.apart());
}

}  // namespace fcg::study
