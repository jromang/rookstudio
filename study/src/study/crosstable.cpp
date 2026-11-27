#include "crosstable.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "crosstable_detail.hpp"
#include "players.hpp"
#include "query.hpp"

namespace fcg::study {

using namespace crosstable;

// ---------------------------------------------------------------------------------------------------------------------
// Rounds

RoundBoard readRound(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    RoundBoard out;
    std::size_t at = 0;
    long round     = 0;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9' && round < 100000)
        round = round * 10 + (text[at++] - '0');
    if (at == 0 || round <= 0 || round >= 100000) return {};
    out.round = static_cast<int>(round);
    if (at < text.size() && text[at] == '.') {
        ++at;
        long board      = 0;
        const auto from = at;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9' && board < 100000)
            board = board * 10 + (text[at++] - '0');
        if (at > from && board > 0 && board < 100000) out.board = static_cast<int>(board);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// The shape

namespace {

// What the organisers call it: a round robin ("8-player round-robin", "double round robin") or a Swiss; none
// otherwise.
std::optional<SystemChoice> formatSystem(std::string_view format) {
    const auto words = foldText(format);
    const auto has   = [&](std::string_view w) { return words.find(w) != std::string::npos; };
    if (has("swiss")) return SystemChoice{TournamentSystem::Swiss, 1};
    if (has("knockout") || has("knock-out")) return SystemChoice{TournamentSystem::Knockout, 1};
    if (has("round robin") || has("round-robin") || has("roundrobin")) {
        if (has("double")) return SystemChoice{TournamentSystem::RoundRobin, 2};
        if (has("triple")) return SystemChoice{TournamentSystem::RoundRobin, 3};
        if (has("quadruple")) return SystemChoice{TournamentSystem::RoundRobin, 4};
        return SystemChoice{TournamentSystem::RoundRobin, 1};
    }
    return std::nullopt;
}

}  // namespace

std::vector<Span> crosstable::roundSpans(uint32_t players, std::span<const CrossGame> games) {
    std::vector<Span> spans(players);
    for (const auto& g : games) {
        const int r = g.round.round;
        if (r <= 0) continue;
        for (const auto p : {g.white, g.black}) {
            if (p >= players) continue;
            auto& s = spans[p];
            if (s.first == 0 || r < s.first) s.first = r;
            s.last = std::max(s.last, r);
        }
    }
    return spans;
}

uint32_t crosstable::swissExpected(uint32_t players, std::span<const CrossGame> games, int rounds,
                                   const std::set<std::pair<uint32_t, int>>& alone) {
    const auto spans  = roundSpans(players, games);
    uint32_t expected = 0;
    for (int r = 1; r <= rounds; ++r) {
        uint32_t active = 0;
        for (uint32_t p = 0; p < players; ++p) {
            const auto& sp = spans[p];
            if (sp.first > 0 && sp.first <= r && r <= sp.last && !alone.contains({p, r})) ++active;
        }
        expected += active / 2;
    }
    return expected;
}

TournamentShape tournamentShape(uint32_t players, std::span<const CrossGame> games, const CrossOptions& options) {
    TournamentShape s;
    s.players = players;
    // Each game's pair and round, sorted: how often each pair met, and the games of a pair within one round (matches
    // of several games: a knockout).
    std::vector<std::pair<uint64_t, int>> pairs_rounds;
    pairs_rounds.reserve(games.size());
    for (const auto& g : games) {
        if (g.white >= players || g.black >= players) continue;  // a bye
        ++s.games;
        pairs_rounds.emplace_back(pairKey(g.white, g.black), g.round.round);
        s.rounds = std::max(s.rounds, g.round.round);
    }
    std::sort(pairs_rounds.begin(), pairs_rounds.end());
    std::vector<std::pair<uint64_t, uint32_t>> met;  // pair, games
    uint32_t in_matches = 0;
    for (std::size_t i = 0; i < pairs_rounds.size();) {
        std::size_t j = i;
        while (j < pairs_rounds.size() && pairs_rounds[j].first == pairs_rounds[i].first) {
            std::size_t k = j;
            while (k < pairs_rounds.size() && pairs_rounds[k] == pairs_rounds[j]) ++k;
            if (k - j >= 2 && pairs_rounds[j].second > 0) in_matches += static_cast<uint32_t>(k - j);
            j = k;
        }
        met.emplace_back(pairs_rounds[i].first, static_cast<uint32_t>(j - i));
        i = j;
    }
    const uint64_t n     = players;
    const uint64_t pairs = n * (n - 1) / 2;
    if (options.team) {
        s.system = {TournamentSystem::Team, 1};
        return s;
    }
    if (s.games >= 4 && in_matches * 2 >= s.games) {
        s.system = {TournamentSystem::Knockout, 1};
        return s;
    }
    // The cycles: how often most pairs met.
    std::map<uint32_t, uint32_t> counts;
    uint32_t most = 0;
    for (const auto& [key, count] : met) {
        ++counts[count];
        most = std::max(most, count);
    }
    uint32_t cycles = 1, best = 0;
    for (const auto& [count, how_many] : counts)
        if (how_many > best) {
            best   = how_many;
            cycles = count;
        }
    const double share  = pairs ? static_cast<double>(met.size()) / static_cast<double>(pairs) : 0.0;
    const int rr_rounds = static_cast<int>(cycles) * static_cast<int>(n - 1 + n % 2);
    const SystemChoice rr{TournamentSystem::RoundRobin, static_cast<int>(cycles)};
    const SystemChoice swiss{TournamentSystem::Swiss, 1};
    if (const auto named = formatSystem(options.format)) {
        s.system = *named;
    } else if (share >= 0.9 && !options.live) {
        s.system = rr;
        for (const auto& [key, count] : met)
            if (count > cycles) ++s.over_met;
        s.uncertain = s.over_met > 0;
    } else if (share >= 0.5 && most <= cycles && (s.rounds == 0 || s.rounds >= rr_rounds) && !options.live) {
        // Most pairs met, and as many rounds as a round robin needs (or none told): one with games missing.
        s.system    = rr;
        s.uncertain = true;
    } else if (options.live && most <= 1 && n <= 16 && (options.planned == 0 || options.planned <= rr_rounds)) {
        // A round robin being played: its pairs met once at most, its field small.
        s.system    = {TournamentSystem::RoundRobin, 1};
        s.uncertain = true;
    } else {
        // Fewer rounds than a round robin needs: a Swiss, however many pairs met.
        s.system = swiss;
        for (const auto& [key, count] : met)
            if (count > 1) ++s.over_met;
        s.uncertain = share >= 0.5 && s.rounds == 0;
    }
    if (s.uncertain)
        s.choices = s.system.system == TournamentSystem::Swiss ? std::vector<SystemChoice>{swiss, rr}
                                                               : std::vector<SystemChoice>{s.system, swiss};
    // What the system expects.
    if (s.system.system == TournamentSystem::RoundRobin) {
        s.expected = static_cast<uint32_t>(static_cast<uint64_t>(s.system.cycles) * pairs);
    } else if (s.system.system == TournamentSystem::Swiss) {
        if (s.rounds == 0)
            s.expected = s.games;
        else
            s.expected = std::max(swissExpected(players, games, s.rounds), s.games);
    }
    return s;
}

// ---------------------------------------------------------------------------------------------------------------------
// The crosstable

const CrossRow* Crosstable::rowOf(uint32_t player) const {
    if (player < row_of.size() && row_of[player] < rows.size()) return &rows[row_of[player]];
    for (const auto& r : rows)
        if (r.player == player) return &r;
    return nullptr;
}

namespace {

double pointsFor(char result, bool white) {
    switch (result) {
        case '1':
            return white ? 1.0 : 0.0;
        case '0':
            return white ? 0.0 : 1.0;
        case '=':
            return 0.5;
        default:
            return 0.0;
    }
}

// The categories of the unplayed rounds of a Swiss (C.07 16.2), and the rounds played.
enum class Category : uint8_t { Played, PairingBye, ForfeitWin, RequestedFollowed, ForfeitLoss, RequestedLast };

struct Builder {
    const CrossOptions& options;
    Crosstable& t;
    uint32_t n = 0;
    bool swiss = false;
    std::vector<double> score;                      // final
    std::vector<double> adjusted;                   // for their opponents' tie-breaks (C.07 16.3)
    std::vector<bool> unsure;                       // their points read a result not known
    std::vector<std::vector<Category>> categories;  // by entry

    Builder(const CrossOptions& o, Crosstable& c) : options(o), t(c) {}

    [[nodiscard]] static bool overBoard(const CrossEntry& e) { return e.kind == CellKind::Game; }

    [[nodiscard]] bool vur(Category c) const {
        return c == Category::RequestedFollowed || c == Category::ForfeitLoss || c == Category::RequestedLast;
    }

    // A pair's average score for `a` over the games between them that count for a direct encounter: those played
    // over the board, and in a round robin the forfeits too (C.07 6.1.1, 15.2); none when they did not meet.
    [[nodiscard]] std::optional<double> encounter(uint32_t a, uint32_t b) const {
        double sum = 0;
        int count  = 0;
        for (const auto& e : t.rows[a].entries) {
            if (e.opponent != b || !e.known) continue;
            if (e.kind == CellKind::Game || (!swiss && e.kind == CellKind::Forfeit)) {
                sum += e.points;
                ++count;
            }
        }
        if (count == 0) return std::nullopt;
        return sum / count;
    }

    // Direct encounter (C.07 Art. 6) among tied players: the classes it orders them in, first to last.
    std::vector<std::vector<uint32_t>> directEncounter(const std::vector<uint32_t>& members, std::vector<double>& de,
                                                       std::vector<bool>& applied) {
        if (members.size() <= 1) return {members};
        std::map<uint32_t, double> scores;
        std::map<uint32_t, int> missing;
        bool all_met = true;
        for (const auto a : members) {
            scores[a]  = 0;
            missing[a] = 0;
            for (const auto b : members) {
                if (a == b) continue;
                if (const auto s = encounter(a, b)) {
                    scores[a] += *s;
                } else {
                    all_met = false;
                    ++missing[a];
                }
            }
        }
        for (const auto a : members) {
            if (!applied[a]) {
                de[a]      = scores[a];
                applied[a] = true;
            }
        }
        const auto byScore = [&](std::vector<uint32_t> set) {
            std::stable_sort(set.begin(), set.end(),
                             [&](uint32_t x, uint32_t y) { return scores[x] > scores[y] + EPSILON; });
            std::vector<std::vector<uint32_t>> classes;
            for (const auto p : set) {
                if (classes.empty() || !same(scores[classes.back().front()], scores[p]))
                    classes.push_back({p});
                else
                    classes.back().push_back(p);
            }
            return classes;
        };
        if (all_met) {
            auto classes = byScore(members);
            if (classes.size() == 1) return classes;
            std::vector<std::vector<uint32_t>> out;
            for (auto& c : classes) {
                if (c.size() == 1) {
                    out.push_back(std::move(c));
                    continue;
                }
                for (auto& sub : directEncounter(c, de, applied)) out.push_back(std::move(sub));
            }
            return out;
        }
        if (!swiss) return {members};
        // C.07 6.3: one alone at the top whatever the missing games give, then the next, and so on.
        std::vector<std::vector<uint32_t>> out;
        std::vector<uint32_t> remaining = members;
        for (;;) {
            std::optional<uint32_t> top;
            for (const auto a : remaining) {
                bool alone = true;
                for (const auto b : remaining)
                    if (b != a && scores[a] <= scores[b] + missing[b] + EPSILON) alone = false;
                if (alone) top = a;
            }
            if (!top || remaining.size() <= 1) break;
            out.push_back({*top});
            std::erase(remaining, *top);
        }
        if (out.empty()) return {members};
        if (!remaining.empty()) {
            for (auto& sub : directEncounter(remaining, de, applied)) out.push_back(std::move(sub));
        }
        return out;
    }
};

}  // namespace

Crosstable makeCrosstable(std::vector<CrossPlayer> players, std::vector<CrossGame> games, const CrossOptions& options) {
    Crosstable t;
    t.players    = std::move(players);
    t.games      = std::move(games);
    t.kind       = options.kind;
    const auto n = static_cast<uint32_t>(t.players.size());

    // The results set by hand: a missing game's, or a round alone's points.
    std::map<std::string, uint32_t> by_name;
    for (uint32_t p = 0; p < n; ++p) by_name.emplace(t.players[p].name, p);
    std::map<std::pair<uint32_t, int>, double> alone_points;
    for (const auto& h : options.hand) {
        const auto w = by_name.find(h.white);
        if (w == by_name.end()) continue;
        if (h.black.empty()) {
            alone_points[{w->second, h.round}] = h.points;
            continue;
        }
        const auto b = by_name.find(h.black);
        if (b == by_name.end() || h.result == '*') continue;
        bool set = false;
        for (auto& g : t.games) {
            const bool pair =
                (g.white == w->second && g.black == b->second) || (g.white == b->second && g.black == w->second);
            if (!pair || g.round.round != h.round || g.result != '*' || g.live) continue;
            g.result = g.white == w->second ? h.result : h.result == '1' ? '0' : h.result == '0' ? '1' : h.result;
            g.hand   = true;
            set      = true;
            break;
        }
        if (!set) {
            CrossGame g;
            g.white       = w->second;
            g.black       = b->second;
            g.round.round = h.round;
            g.result      = h.result;
            g.hand        = true;
            g.plies       = 2;
            g.ref         = UINT64_MAX;
            t.games.push_back(g);
        }
    }

    t.shape = tournamentShape(n, t.games, options);
    // A Swiss's player alone in a round by a result set by hand (a bye, an absence) leaves no game to expect there.
    if (t.shape.system.system == TournamentSystem::Swiss && t.shape.rounds > 0 && !alone_points.empty()) {
        std::set<std::pair<uint32_t, int>> alone;
        for (const auto& [key, points] : alone_points) alone.insert(key);
        t.shape.expected = std::max(swissExpected(n, t.games, t.shape.rounds, alone), t.shape.games);
    }
    t.chosen = options.system.has_value();
    t.system = options.system.value_or(t.shape.system);
    if (t.system.cycles < 1) t.system.cycles = 1;
    t.unrated = std::any_of(t.players.begin(), t.players.end(), [](const CrossPlayer& p) { return p.elo <= 0; });
    {
        int64_t sum = 0;
        int rated   = 0;
        for (const auto& p : t.players)
            if (p.elo > 0) {
                sum += p.elo;
                ++rated;
            }
        t.mean_elo = rated ? roundHalfUp(static_cast<double>(sum) / rated) : 0;
    }
    t.fide_order = !options.order.has_value();
    if (options.order) {
        t.tiebreaks = *options.order;
    } else {
        t.tiebreaks = fideTiebreaks(t.system.system, !t.unrated);
    }
    const bool swiss = t.system.system == TournamentSystem::Swiss;
    const bool rr    = t.system.system == TournamentSystem::RoundRobin;

    // The rows: start numbers by rating, then name.
    t.rows.resize(n);
    {
        std::vector<uint32_t> order(n);
        std::iota(order.begin(), order.end(), 0u);
        std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
            if (t.players[a].elo != t.players[b].elo) return t.players[a].elo > t.players[b].elo;
            return t.players[a].name < t.players[b].name;
        });
        for (uint32_t i = 0; i < n; ++i) {
            t.rows[order[i]].player = order[i];
            t.rows[order[i]].start  = static_cast<int>(i) + 1;
        }
    }
    // Their games.
    for (uint32_t g = 0; g < t.games.size(); ++g) {
        const auto& game = t.games[g];
        t.rounds         = std::max(t.rounds, game.round.round);
        if (game.live) ++t.live;
        for (const bool white : {true, false}) {
            const auto p = white ? game.white : game.black;
            if (p >= n) continue;
            const auto other = white ? game.black : game.white;
            CrossEntry e;
            e.opponent = other < n ? other : NO_PLAYER;
            e.white    = white;
            e.round    = game.round.round;
            e.game     = g;
            e.hand     = game.hand;
            if (other >= n) {  // a bye written as a game
                e.kind   = CellKind::Bye;
                e.points = game.result == '*' ? 1.0 : pointsFor(game.result, white);
            } else if (game.live) {
                e.kind  = CellKind::Live;
                e.known = false;
            } else if (game.result == '*') {
                e.kind  = CellKind::Missing;
                e.known = false;
            } else {
                e.kind   = decisive(game.result) && game.plies < 2 && !game.hand ? CellKind::Forfeit : CellKind::Game;
                e.points = pointsFor(game.result, white);
            }
            t.rows[p].entries.push_back(e);
        }
    }
    for (uint32_t g = 0; g < t.games.size(); ++g) {
        const auto& game = t.games[g];
        if (game.white < n && game.black < n && !game.live && game.result != '*' && decisive(game.result) &&
            game.plies < 2 && !game.hand)
            t.forfeits.push_back(g);
    }

    // Two names of one player: never paired together, their rounds apart (before the byes are read: the second
    // name's first round is not a bye).
    std::vector<bool> doubled(n, false);
    {
        // The players by their surnames' skeleton: those of one bucket may be one.
        std::vector<NameParts> parts(n);
        std::vector<std::pair<std::string, uint32_t>> skeletons;
        for (uint32_t p = 0; p < n; ++p) {
            parts[p] = nameParts(t.players[p].name);
            if (!parts[p].surname.empty()) skeletons.emplace_back(nameSkeleton(parts[p].surname), p);
        }
        std::sort(skeletons.begin(), skeletons.end());
        std::unordered_set<uint64_t> met;
        for (const auto& g : t.games)
            if (g.white < n && g.black < n) met.insert(pairKey(g.white, g.black));
        const auto spans = roundSpans(n, t.games);
        for (std::size_t i = 0; i < skeletons.size();) {
            std::size_t j = i + 1;
            while (j < skeletons.size() && skeletons[j].first == skeletons[i].first) ++j;
            for (std::size_t x = i; x < j; ++x)
                for (std::size_t y = x + 1; y < j; ++y) {
                    const auto a = std::min(skeletons[x].second, skeletons[y].second);
                    const auto c = std::max(skeletons[x].second, skeletons[y].second);
                    if (!givenCompatible(parts[a].given, parts[c].given)) continue;
                    const auto& sa = spans[a];
                    const auto& sc = spans[c];
                    if (sa.first == 0 || sc.first == 0) continue;
                    if (!(sa.last < sc.first || sc.last < sa.first)) continue;
                    if (met.count(pairKey(a, c))) continue;
                    TwoNames two;
                    const bool a_first = sa.first < sc.first;
                    two.a              = a_first ? a : c;
                    two.b              = a_first ? c : a;
                    two.a_from         = a_first ? sa.first : sc.first;
                    two.a_to           = a_first ? sa.last : sc.last;
                    two.b_from         = a_first ? sc.first : sa.first;
                    two.b_to           = a_first ? sc.last : sa.last;
                    t.two_names.push_back(two);
                    doubled[a] = doubled[c] = true;
                }
            i = j;
        }
        std::sort(t.two_names.begin(), t.two_names.end(),
                  [](const TwoNames& x, const TwoNames& y) { return std::tie(x.a, x.b) < std::tie(y.a, y.b); });
    }

    // The rounds without a game.
    if (swiss && t.rounds > 0) {
        const auto spans = roundSpans(n, t.games);
        std::vector<std::vector<bool>> has(n, std::vector<bool>(static_cast<std::size_t>(t.rounds) + 1, false));
        for (uint32_t p = 0; p < n; ++p)
            for (const auto& e : t.rows[p].entries)
                if (e.round > 0) has[p][static_cast<std::size_t>(e.round)] = true;
        const auto meet = [&](uint32_t a, uint32_t b) {
            for (const auto& e : t.rows[a].entries)
                if (e.opponent == b) return true;
            return false;
        };
        // A bye at the edges of a player's rounds: in round 1 the only player whose first game is round 2, in the last
        // round the only one whose last game was the round before, when the round's players were even without them
        // (the field odd by one: theirs the bye). Otherwise a player out of their rounds came late or withdrew.
        for (int r = 1; r <= t.rounds; ++r) {
            std::vector<uint32_t> active, absent, edge;
            for (uint32_t p = 0; p < n; ++p) {
                const auto& sp = spans[p];
                if (sp.first == 0) continue;
                if (sp.first <= r && r <= sp.last) {
                    active.push_back(p);
                    if (!has[p][static_cast<std::size_t>(r)]) absent.push_back(p);
                } else if (!doubled[p] && ((r == 1 && sp.first == 2) ||
                                           (r == t.rounds && r > 1 && sp.last == r - 1 && !options.live))) {
                    edge.push_back(p);
                }
            }
            const auto add = [&](uint32_t p, CrossEntry e) {
                e.round = r;
                if (const auto h = alone_points.find({p, r}); h != alone_points.end()) {
                    e.kind     = CellKind::Absent;
                    e.points   = h->second;
                    e.known    = true;
                    e.hand     = true;
                    e.unsure   = false;
                    e.opponent = NO_PLAYER;
                }
                t.rows[p].entries.push_back(e);
            };
            std::vector<bool> handled(n, false);
            if (absent.empty() && active.size() % 2 == 0 && edge.size() == 1) {
                CrossEntry e;
                e.kind   = CellKind::Bye;
                e.points = 1;
                add(edge[0], e);
                handled[edge[0]] = true;
            }
            if (absent.size() == 1 && active.size() % 2 == 1) {
                CrossEntry e;
                e.kind   = CellKind::Bye;
                e.points = 1;
                add(absent[0], e);
            } else if (absent.size() == 2 && !meet(absent[0], absent[1]) && !options.live) {
                for (int side = 0; side < 2; ++side) {
                    CrossEntry e;
                    e.kind     = CellKind::Missing;
                    e.known    = false;
                    e.opponent = absent[static_cast<std::size_t>(1 - side)];
                    add(absent[static_cast<std::size_t>(side)], e);
                }
                if (alone_points.count({absent[0], r}) == 0 || alone_points.count({absent[1], r}) == 0) {
                    MissingGame m;
                    m.round = r;
                    m.white = t.rows[absent[0]].start < t.rows[absent[1]].start ? absent[0] : absent[1];
                    m.black = m.white == absent[0] ? absent[1] : absent[0];
                    t.missing.push_back(m);
                }
            } else {
                for (const auto p : absent) {
                    CrossEntry e;
                    e.kind   = CellKind::Absent;
                    e.unsure = absent.size() >= 2 && !options.live;
                    e.known  = !e.unsure;
                    add(p, e);
                    if (e.unsure && alone_points.count({p, r}) == 0) {
                        MissingGame m;
                        m.round = r;
                        m.alone = p;
                        t.missing.push_back(m);
                    }
                }
            }
            // Before their first game and after their last: rounds without a game, kept so.
            for (uint32_t p = 0; p < n; ++p) {
                const auto& sp = spans[p];
                if (sp.first == 0 || handled[p] || (r >= sp.first && r <= sp.last)) continue;
                if (r > sp.last && options.live) continue;  // a round to come
                CrossEntry e;
                e.kind = CellKind::Absent;
                add(p, e);
            }
        }
    } else if (rr) {
        // The pairs that met fewer times than the cycles: games missing, or to come.
        std::unordered_map<uint64_t, uint32_t> met;
        for (const auto& g : t.games)
            if (g.white < n && g.black < n) ++met[pairKey(g.white, g.black)];
        std::vector<std::set<int>> rounds_of(n);
        for (uint32_t p = 0; p < n; ++p)
            for (const auto& e : t.rows[p].entries)
                if (e.round > 0) rounds_of[p].insert(e.round);
        const int rounds = std::max(t.rounds, t.system.cycles * static_cast<int>(n - 1 + n % 2));
        for (uint32_t a = 0; a < n; ++a)
            for (uint32_t b = a + 1; b < n; ++b) {
                const auto it    = met.find(pairKey(a, b));
                const auto count = it == met.end() ? 0u : it->second;
                for (auto k = count; k < static_cast<uint32_t>(t.system.cycles); ++k) {
                    // Its round: the one round both have no game in, when there is one.
                    int round = 0;
                    if (!options.live && t.rounds > 0) {
                        std::vector<int> free;
                        for (int r = 1; r <= rounds; ++r)
                            if (!rounds_of[a].count(r) && !rounds_of[b].count(r)) free.push_back(r);
                        if (free.size() == 1) round = free[0];
                    }
                    for (const bool side : {true, false}) {
                        CrossEntry e;
                        e.kind     = options.live ? CellKind::ToCome : CellKind::Missing;
                        e.known    = false;
                        e.opponent = side ? b : a;
                        e.round    = round;
                        t.rows[side ? a : b].entries.push_back(e);
                    }
                    if (!options.live) {
                        MissingGame m;
                        m.round = round;
                        m.white = t.rows[a].start < t.rows[b].start ? a : b;
                        m.black = m.white == a ? b : a;
                        t.missing.push_back(m);
                    }
                }
            }
    }
    for (auto& row : t.rows)
        std::stable_sort(row.entries.begin(), row.entries.end(), [](const CrossEntry& a, const CrossEntry& b) {
            const int ra = a.round > 0 ? a.round : INT32_MAX, rb = b.round > 0 ? b.round : INT32_MAX;
            return ra < rb;
        });

    // The points.
    Builder b(options, t);
    b.n     = n;
    b.swiss = swiss;
    b.score.assign(n, 0);
    b.unsure.assign(n, false);
    for (uint32_t p = 0; p < n; ++p) {
        auto& row = t.rows[p];
        for (const auto& e : row.entries) {
            if (e.known) row.points += e.points;
            if (!e.known && e.kind != CellKind::Live && e.kind != CellKind::ToCome) ++row.unknown;
            if (e.known && (e.kind == CellKind::Game || e.kind == CellKind::Forfeit)) ++row.played;
        }
        b.score[p]  = row.points;
        b.unsure[p] = row.unknown > 0;
    }
    // The categories of the rounds (C.07 16.2), and the scores adjusted for the opponents (16.3).
    b.categories.resize(n);
    b.adjusted = b.score;
    for (uint32_t p = 0; p < n; ++p) {
        const auto& entries = t.rows[p].entries;
        auto& cats          = b.categories[p];
        cats.assign(entries.size(), Category::Played);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& e = entries[i];
            switch (e.kind) {
                case CellKind::Bye:
                    cats[i] = Category::PairingBye;
                    break;
                case CellKind::Forfeit:
                    cats[i] = e.points > 0.5 ? Category::ForfeitWin : Category::ForfeitLoss;
                    break;
                case CellKind::Absent:
                    cats[i] = Category::RequestedLast;  // until a round after it is not a VUR
                    break;
                default:
                    break;
            }
        }
        // A bye asked for followed by a round that is not a voluntary unplayed round (16.2.3).
        bool later_played = false;
        for (std::size_t i = entries.size(); i-- > 0;) {
            if (cats[i] == Category::RequestedLast && later_played) cats[i] = Category::RequestedFollowed;
            if (!b.vur(cats[i]) && entries[i].kind != CellKind::ToCome) later_played = true;
        }
        if (swiss)
            for (std::size_t i = 0; i < entries.size(); ++i)
                if (cats[i] == Category::RequestedLast)
                    b.adjusted[p] += 0.5 - (entries[i].known ? entries[i].points : 0);
    }

    // The tie-breaks.
    const std::size_t tbs = t.tiebreaks.size();
    for (auto& row : t.rows) {
        row.tiebreaks.assign(tbs, 0);
        row.states.assign(tbs, TiebreakState::Known);
    }
    const double draw_rounds = 0.5 * std::max(t.rounds, 1);
    const double max_score =
        rr ? static_cast<double>(t.system.cycles) * (n > 0 ? n - 1 : 0) : static_cast<double>(t.rounds);
    for (uint32_t p = 0; p < n; ++p) {
        auto& row           = t.rows[p];
        const auto& entries = row.entries;
        const auto& cats    = b.categories[p];
        // The contributions of Buchholz and Sonneborn-Berger, each round's (C.07 8.1, 9.1, 16.4).
        struct Part {
            double bh = 0, sb = 0, opponent = 0;
            bool vur = false;
        };
        std::vector<Part> parts;
        bool opponents_unsure = false;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& e = entries[i];
            if (e.kind == CellKind::Live || e.kind == CellKind::ToCome) continue;
            Part part;
            part.vur = swiss && b.vur(cats[i]);
            if (!swiss) {
                // A round robin: the opponents' final scores, forfeits as games (15.2); its rounds without a game
                // are none.
                if (e.opponent == NO_PLAYER) continue;
                part.opponent = b.score[e.opponent];
                part.bh       = part.opponent;
                part.sb       = part.opponent * (e.known ? e.points : 0);
                if (b.unsure[e.opponent]) opponents_unsure = true;
            } else if (cats[i] == Category::Played) {
                if (e.opponent == NO_PLAYER) continue;
                part.opponent = b.adjusted[e.opponent];
                part.bh       = part.opponent;
                part.sb       = part.opponent * (e.known ? e.points : 0);
                if (b.unsure[e.opponent]) opponents_unsure = true;
            } else {
                // A dummy whose score is the player's own, capped (16.4).
                double dummy = b.score[p];
                if (cats[i] == Category::ForfeitWin || cats[i] == Category::ForfeitLoss) {
                    if (e.opponent != NO_PLAYER) dummy = std::min(dummy, b.adjusted[e.opponent]);
                } else {
                    dummy = std::min(dummy, draw_rounds);
                }
                part.opponent = dummy;
                part.bh       = dummy;
                part.sb       = dummy * (e.known ? e.points : 0);
            }
            parts.push_back(part);
        }
        // The least significant value cut, a VUR's first (16.5).
        const auto cut = [&](std::vector<Part> list, bool sb, int lowest, int highest) {
            const auto value = [&](const Part& x) { return sb ? x.sb : x.bh; };
            for (int c = 0; c < lowest && !list.empty(); ++c) {
                // The least significant value: the lowest contribution (for SB, that of the opponent with the
                // lowest score, the lowest of theirs).
                std::size_t least = 0;
                for (std::size_t k = 1; k < list.size(); ++k) {
                    if (sb) {
                        if (list[k].opponent < list[least].opponent - EPSILON ||
                            (same(list[k].opponent, list[least].opponent) && list[k].sb < list[least].sb))
                            least = k;
                    } else if (list[k].bh < list[least].bh) {
                        least = k;
                    }
                }
                std::optional<std::size_t> vur;
                for (std::size_t k = 0; k < list.size(); ++k)
                    if (list[k].vur && (!vur || value(list[k]) < value(list[*vur]))) vur = k;
                std::size_t gone = least;
                if (vur && value(list[*vur]) >= value(list[least]) - EPSILON) gone = *vur;
                list.erase(list.begin() + static_cast<std::ptrdiff_t>(gone));
            }
            for (int c = 0; c < highest && !list.empty(); ++c) {
                std::size_t most = 0;
                for (std::size_t k = 1; k < list.size(); ++k)
                    if (value(list[k]) > value(list[most])) most = k;
                list.erase(list.begin() + static_cast<std::ptrdiff_t>(most));
            }
            double sum = 0;
            for (const auto& x : list) sum += value(x);
            return sum;
        };
        // Over the board against rated opponents: the ratings' tie-breaks and the performance.
        std::vector<int> ratings;
        double board_points = 0;
        bool unplayed       = false;
        for (const auto& e : entries) {
            if (e.kind == CellKind::Live || e.kind == CellKind::ToCome || e.kind == CellKind::Missing) continue;
            if (e.kind != CellKind::Game) {
                unplayed = true;
                continue;
            }
            if (e.opponent == NO_PLAYER || t.players[e.opponent].elo <= 0) continue;
            ratings.push_back(t.players[e.opponent].elo);
            board_points += e.points;
        }
        const auto average = [](const std::vector<int>& list) {
            if (list.empty()) return 0;
            const double sum = std::accumulate(list.begin(), list.end(), 0.0);
            return roundHalfUp(sum / static_cast<double>(list.size()));
        };
        row.opponents_rating = average(ratings);
        row.over_board       = board_points;
        row.over_board_games = static_cast<int>(ratings.size());
        if (!ratings.empty())
            row.performance =
                row.opponents_rating + fideRatingDifference(board_points / static_cast<double>(ratings.size()));
        const bool own_unsure = b.unsure[p];
        for (std::size_t k = 0; k < tbs; ++k) {
            double v    = 0;
            bool unsure = own_unsure;
            switch (t.tiebreaks[k]) {
                case Tiebreak::DirectEncounter:
                    break;  // in the ranking
                case Tiebreak::Wins:
                    for (const auto& e : entries)
                        if (e.known && e.points > 0.75 && e.kind != CellKind::Live) v += 1;
                    break;
                case Tiebreak::GamesWon:
                    for (const auto& e : entries)
                        if (e.kind == CellKind::Game && e.points > 0.75) v += 1;
                    break;
                case Tiebreak::BlackGames:
                    for (const auto& e : entries)
                        if (e.kind == CellKind::Game && !e.white) v += 1;
                    break;
                case Tiebreak::BlackWins:
                    for (const auto& e : entries)
                        if (e.kind == CellKind::Game && !e.white && e.points > 0.75) v += 1;
                    break;
                case Tiebreak::Progressive: {
                    // The score after each round, added up; the rounds not known after them.
                    double running = 0;
                    std::map<int, double> by_round;
                    double unrounded = 0;
                    for (const auto& e : entries) {
                        if (!e.known) continue;
                        if (e.round > 0)
                            by_round[e.round] += e.points;
                        else
                            unrounded += e.points;
                    }
                    const int last = std::max(t.rounds, by_round.empty() ? 0 : by_round.rbegin()->first);
                    for (int r = 1; r <= last; ++r) {
                        if (const auto it = by_round.find(r); it != by_round.end()) running += it->second;
                        v += running;
                    }
                    if (unrounded > 0) {
                        v += running + unrounded;
                        unsure = true;
                    }
                    break;
                }
                case Tiebreak::Buchholz:
                    for (const auto& x : parts) v += x.bh;
                    unsure = unsure || opponents_unsure;
                    break;
                case Tiebreak::BuchholzCut1:
                    v      = cut(parts, false, 1, 0);
                    unsure = unsure || opponents_unsure;
                    break;
                case Tiebreak::BuchholzMedian1:
                    v      = cut(parts, false, 1, 1);
                    unsure = unsure || opponents_unsure;
                    break;
                case Tiebreak::SonnebornBerger:
                    for (const auto& x : parts) v += x.sb;
                    unsure = unsure || opponents_unsure;
                    break;
                case Tiebreak::Koya:
                    for (const auto& e : entries)
                        if (e.known && e.opponent != NO_PLAYER && b.score[e.opponent] + EPSILON >= max_score / 2 &&
                            (e.kind == CellKind::Game || e.kind == CellKind::Forfeit)) {
                            v += e.points;
                            if (b.unsure[e.opponent]) unsure = true;
                        }
                    break;
                case Tiebreak::AverageRating:
                    v = row.opponents_rating;
                    if (ratings.empty()) row.states[k] = TiebreakState::None;
                    break;
                case Tiebreak::AverageRatingCut1: {
                    auto list = ratings;
                    if (!unplayed && !list.empty()) list.erase(std::min_element(list.begin(), list.end()));
                    v = average(list);
                    if (list.empty()) row.states[k] = TiebreakState::None;
                    break;
                }
                case Tiebreak::Performance:
                    v = row.performance.value_or(0);
                    if (!row.performance) row.states[k] = TiebreakState::None;
                    break;
            }
            row.tiebreaks[k] = v;
            if (row.states[k] == TiebreakState::Known && unsure) row.states[k] = TiebreakState::Unsure;
        }

        // The rating change (B.02 8.3): the games played over the board against a rated opponent, both having moved.
        const int own = t.players[p].elo;
        if (own > 0) {
            int games_rated = 0;
            double expected = 0, scored = 0;
            for (const auto& e : entries) {
                if (e.kind != CellKind::Game || e.opponent == NO_PLAYER) continue;
                const int other = t.players[e.opponent].elo;
                if (other <= 0) continue;
                const auto& game = t.games[e.game];
                if (game.plies < 2 && !game.hand) continue;
                int d = own - other;
                if (options.kind == RatingKind::Standard) {
                    if (own < 2650) d = std::clamp(d, -400, 400);  // the 400 points' rule (since 1 October 2025)
                } else {
                    // Rapid and blitz: the cap always; a gap of 600 with a player above 2600 not rated.
                    if (std::abs(own - other) >= 600 && (own > 2600 || other > 2600)) continue;
                    d = std::clamp(d, -400, 400);
                }
                expected += fideExpectedScore(d);
                scored += e.points;
                ++games_rated;
            }
            row.rated_games    = games_rated;
            row.expected       = expected;
            row.scored         = scored;
            const auto& player = t.players[p];
            row.k_listed       = player.k > 0;
            row.k              = fideK(own, player.born, options.year, player.k, games_rated);
            if (games_rated > 0) row.change = row.k * (scored - expected);
        }
    }

    // The standings: the points, then each tie-break in its order (C.07 4.2).
    std::vector<double> de(n, 0);
    std::vector<bool> de_applied(n, false);
    std::vector<std::vector<uint32_t>> classes;
    std::function<void(std::vector<uint32_t>, std::size_t)> rank = [&](std::vector<uint32_t> group, std::size_t k) {
        if (group.size() <= 1 || k >= tbs) {
            std::stable_sort(group.begin(), group.end(),
                             [&](uint32_t x, uint32_t y) { return t.rows[x].start < t.rows[y].start; });
            classes.push_back(std::move(group));
            return;
        }
        if (t.tiebreaks[k] == Tiebreak::DirectEncounter) {
            for (auto& c : b.directEncounter(group, de, de_applied)) rank(std::move(c), k + 1);
            return;
        }
        std::stable_sort(group.begin(), group.end(), [&](uint32_t x, uint32_t y) {
            return t.rows[x].tiebreaks[k] > t.rows[y].tiebreaks[k] + EPSILON;
        });
        std::vector<uint32_t> part;
        for (const auto p : group) {
            if (!part.empty() && !same(t.rows[part.front()].tiebreaks[k], t.rows[p].tiebreaks[k])) {
                rank(std::move(part), k + 1);
                part.clear();
            }
            part.push_back(p);
        }
        if (!part.empty()) rank(std::move(part), k + 1);
    };
    {
        std::vector<uint32_t> all(n);
        std::iota(all.begin(), all.end(), 0u);
        std::stable_sort(all.begin(), all.end(), [&](uint32_t x, uint32_t y) {
            if (!same(b.score[x], b.score[y])) return b.score[x] > b.score[y];
            return t.rows[x].start < t.rows[y].start;
        });
        std::vector<uint32_t> part;
        for (const auto p : all) {
            if (!part.empty() && !same(b.score[part.front()], b.score[p])) {
                rank(std::move(part), 0);
                part.clear();
            }
            part.push_back(p);
        }
        if (!part.empty()) rank(std::move(part), 0);
    }
    for (std::size_t k = 0; k < tbs; ++k)
        if (t.tiebreaks[k] == Tiebreak::DirectEncounter)
            for (uint32_t p = 0; p < n; ++p) {
                t.rows[p].tiebreaks[k] = de[p];
                t.rows[p].states[k] =
                    de_applied[p] ? (b.unsure[p] ? TiebreakState::Unsure : TiebreakState::Known) : TiebreakState::None;
            }
    std::vector<CrossRow> ordered;
    ordered.reserve(n);
    int place = 1;
    for (const auto& c : classes) {
        for (const auto p : c) {
            auto row       = std::move(t.rows[p]);
            row.place_from = place;
            row.place_to   = place + static_cast<int>(c.size()) - 1;
            ordered.push_back(std::move(row));
        }
        place += static_cast<int>(c.size());
    }
    t.rows = std::move(ordered);
    t.row_of.assign(n, UINT32_MAX);
    for (uint32_t r = 0; r < t.rows.size(); ++r) t.row_of[t.rows[r].player] = r;

    return t;
}

}  // namespace fcg::study
