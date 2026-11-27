#include "crosstable.hpp"

#include <algorithm>
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

Crosstable makeCrosstable(std::vector<CrossPlayer> players, std::vector<CrossGame> games, const CrossOptions& options) {
    return CrosstableBuilder(std::move(players), std::move(games), options).build();
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

}  // namespace

CrosstableBuilder::CrosstableBuilder(std::vector<CrossPlayer> players, std::vector<CrossGame> games,
                                     const CrossOptions& options)
    : options_(options), n_(static_cast<uint32_t>(players.size())), doubled_(n_, false) {
    t_.players = std::move(players);
    t_.games   = std::move(games);
    t_.kind    = options.kind;
}

Crosstable CrosstableBuilder::build() && {
    setHandResults();
    chooseSystem();
    numberPlayers();
    enterGames();
    findTwoNames();
    enterSwissRounds();
    enterMissingPairs();
    sortEntries();
    countPoints();
    categorise();
    computeTiebreaks();
    computeRatingChanges();
    rank();
    return std::move(t_);
}

void CrosstableBuilder::setHandResults() {
    std::map<std::string, uint32_t> by_name;
    for (uint32_t p = 0; p < n_; ++p) by_name.emplace(t_.players[p].name, p);
    for (const auto& h : options_.hand) {
        const auto w = by_name.find(h.white);
        if (w == by_name.end()) continue;
        if (h.black.empty()) {
            alone_points_[{w->second, h.round}] = h.points;
            continue;
        }
        const auto b = by_name.find(h.black);
        if (b == by_name.end() || h.result == '*') continue;
        bool set = false;
        for (auto& g : t_.games) {
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
            t_.games.push_back(g);
        }
    }
}

void CrosstableBuilder::chooseSystem() {
    t_.shape = tournamentShape(n_, t_.games, options_);
    // A Swiss's player alone in a round by a result set by hand (a bye, an absence) leaves no game to expect there.
    if (t_.shape.system.system == TournamentSystem::Swiss && t_.shape.rounds > 0 && !alone_points_.empty()) {
        std::set<std::pair<uint32_t, int>> alone;
        for (const auto& [key, points] : alone_points_) alone.insert(key);
        t_.shape.expected = std::max(swissExpected(n_, t_.games, t_.shape.rounds, alone), t_.shape.games);
    }
    t_.chosen = options_.system.has_value();
    t_.system = options_.system.value_or(t_.shape.system);
    if (t_.system.cycles < 1) t_.system.cycles = 1;
    t_.unrated  = std::any_of(t_.players.begin(), t_.players.end(), [](const CrossPlayer& p) { return p.elo <= 0; });
    int64_t sum = 0;
    int rated   = 0;
    for (const auto& p : t_.players)
        if (p.elo > 0) {
            sum += p.elo;
            ++rated;
        }
    t_.mean_elo   = rated ? roundHalfUp(static_cast<double>(sum) / rated) : 0;
    t_.fide_order = !options_.order.has_value();
    t_.tiebreaks  = options_.order ? *options_.order : fideTiebreaks(t_.system.system, !t_.unrated);
    swiss_        = t_.system.system == TournamentSystem::Swiss;
    rr_           = t_.system.system == TournamentSystem::RoundRobin;
}

void CrosstableBuilder::numberPlayers() {
    t_.rows.resize(n_);
    std::vector<uint32_t> order(n_);
    std::iota(order.begin(), order.end(), 0u);
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        if (t_.players[a].elo != t_.players[b].elo) return t_.players[a].elo > t_.players[b].elo;
        return t_.players[a].name < t_.players[b].name;
    });
    for (uint32_t i = 0; i < n_; ++i) {
        t_.rows[order[i]].player = order[i];
        t_.rows[order[i]].start  = static_cast<int>(i) + 1;
    }
}

void CrosstableBuilder::enterGames() {
    for (uint32_t g = 0; g < t_.games.size(); ++g) {
        const auto& game = t_.games[g];
        t_.rounds        = std::max(t_.rounds, game.round.round);
        if (game.live) ++t_.live;
        for (const bool white : {true, false}) {
            const auto p = white ? game.white : game.black;
            if (p >= n_) continue;
            const auto other = white ? game.black : game.white;
            CrossEntry e;
            e.opponent = other < n_ ? other : NO_PLAYER;
            e.white    = white;
            e.round    = game.round.round;
            e.game     = g;
            e.hand     = game.hand;
            if (other >= n_) {  // a bye written as a game
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
            t_.rows[p].entries.push_back(e);
        }
    }
    for (uint32_t g = 0; g < t_.games.size(); ++g) {
        const auto& game = t_.games[g];
        if (game.white < n_ && game.black < n_ && !game.live && game.result != '*' && decisive(game.result) &&
            game.plies < 2 && !game.hand)
            t_.forfeits.push_back(g);
    }
}

void CrosstableBuilder::findTwoNames() {
    // Before the byes are read: the second name's first round is not a bye. The players by their surnames' skeleton:
    // those of one bucket may be one.
    std::vector<NameParts> parts(n_);
    std::vector<std::pair<std::string, uint32_t>> skeletons;
    for (uint32_t p = 0; p < n_; ++p) {
        parts[p] = nameParts(t_.players[p].name);
        if (!parts[p].surname.empty()) skeletons.emplace_back(nameSkeleton(parts[p].surname), p);
    }
    std::sort(skeletons.begin(), skeletons.end());
    std::unordered_set<uint64_t> met;
    for (const auto& g : t_.games)
        if (g.white < n_ && g.black < n_) met.insert(pairKey(g.white, g.black));
    const auto spans = roundSpans(n_, t_.games);
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
                t_.two_names.push_back(two);
                doubled_[a] = doubled_[c] = true;
            }
        i = j;
    }
    std::sort(t_.two_names.begin(), t_.two_names.end(),
              [](const TwoNames& x, const TwoNames& y) { return std::tie(x.a, x.b) < std::tie(y.a, y.b); });
}

void CrosstableBuilder::enterSwissRounds() {
    if (!swiss_ || t_.rounds <= 0) return;
    const auto spans = roundSpans(n_, t_.games);
    std::vector<std::vector<bool>> has(n_, std::vector<bool>(static_cast<std::size_t>(t_.rounds) + 1, false));
    for (uint32_t p = 0; p < n_; ++p)
        for (const auto& e : t_.rows[p].entries)
            if (e.round > 0) has[p][static_cast<std::size_t>(e.round)] = true;
    const auto meet = [&](uint32_t a, uint32_t b) {
        for (const auto& e : t_.rows[a].entries)
            if (e.opponent == b) return true;
        return false;
    };
    // A bye at the edges of a player's rounds: in round 1 the only player whose first game is round 2, in the last
    // round the only one whose last game was the round before, when the round's players were even without them (the
    // field odd by one: theirs the bye). Otherwise a player out of their rounds came late or withdrew.
    for (int r = 1; r <= t_.rounds; ++r) {
        std::vector<uint32_t> active, absent, edge;
        for (uint32_t p = 0; p < n_; ++p) {
            const auto& sp = spans[p];
            if (sp.first == 0) continue;
            if (sp.first <= r && r <= sp.last) {
                active.push_back(p);
                if (!has[p][static_cast<std::size_t>(r)]) absent.push_back(p);
            } else if (!doubled_[p] &&
                       ((r == 1 && sp.first == 2) || (r == t_.rounds && r > 1 && sp.last == r - 1 && !options_.live))) {
                edge.push_back(p);
            }
        }
        const auto add = [&](uint32_t p, CrossEntry e) {
            e.round = r;
            if (const auto h = alone_points_.find({p, r}); h != alone_points_.end()) {
                e.kind     = CellKind::Absent;
                e.points   = h->second;
                e.known    = true;
                e.hand     = true;
                e.unsure   = false;
                e.opponent = NO_PLAYER;
            }
            t_.rows[p].entries.push_back(e);
        };
        std::vector<bool> handled(n_, false);
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
        } else if (absent.size() == 2 && !meet(absent[0], absent[1]) && !options_.live) {
            for (int side = 0; side < 2; ++side) {
                CrossEntry e;
                e.kind     = CellKind::Missing;
                e.known    = false;
                e.opponent = absent[static_cast<std::size_t>(1 - side)];
                add(absent[static_cast<std::size_t>(side)], e);
            }
            if (alone_points_.count({absent[0], r}) == 0 || alone_points_.count({absent[1], r}) == 0) {
                MissingGame m;
                m.round = r;
                m.white = t_.rows[absent[0]].start < t_.rows[absent[1]].start ? absent[0] : absent[1];
                m.black = m.white == absent[0] ? absent[1] : absent[0];
                t_.missing.push_back(m);
            }
        } else {
            for (const auto p : absent) {
                CrossEntry e;
                e.kind   = CellKind::Absent;
                e.unsure = absent.size() >= 2 && !options_.live;
                e.known  = !e.unsure;
                add(p, e);
                if (e.unsure && alone_points_.count({p, r}) == 0) {
                    MissingGame m;
                    m.round = r;
                    m.alone = p;
                    t_.missing.push_back(m);
                }
            }
        }
        // Before their first game and after their last: rounds without a game, kept so.
        for (uint32_t p = 0; p < n_; ++p) {
            const auto& sp = spans[p];
            if (sp.first == 0 || handled[p] || (r >= sp.first && r <= sp.last)) continue;
            if (r > sp.last && options_.live) continue;  // a round to come
            CrossEntry e;
            e.kind = CellKind::Absent;
            add(p, e);
        }
    }
}

void CrosstableBuilder::enterMissingPairs() {
    if (!rr_) return;
    std::unordered_map<uint64_t, uint32_t> met;
    for (const auto& g : t_.games)
        if (g.white < n_ && g.black < n_) ++met[pairKey(g.white, g.black)];
    std::vector<std::set<int>> rounds_of(n_);
    for (uint32_t p = 0; p < n_; ++p)
        for (const auto& e : t_.rows[p].entries)
            if (e.round > 0) rounds_of[p].insert(e.round);
    const int rounds = std::max(t_.rounds, t_.system.cycles * static_cast<int>(n_ - 1 + n_ % 2));
    for (uint32_t a = 0; a < n_; ++a)
        for (uint32_t b = a + 1; b < n_; ++b) {
            const auto it    = met.find(pairKey(a, b));
            const auto count = it == met.end() ? 0u : it->second;
            for (auto k = count; k < static_cast<uint32_t>(t_.system.cycles); ++k) {
                // Its round: the one round both have no game in, when there is one.
                int round = 0;
                if (!options_.live && t_.rounds > 0) {
                    std::vector<int> free;
                    for (int r = 1; r <= rounds; ++r)
                        if (!rounds_of[a].count(r) && !rounds_of[b].count(r)) free.push_back(r);
                    if (free.size() == 1) round = free[0];
                }
                for (const bool side : {true, false}) {
                    CrossEntry e;
                    e.kind     = options_.live ? CellKind::ToCome : CellKind::Missing;
                    e.known    = false;
                    e.opponent = side ? b : a;
                    e.round    = round;
                    t_.rows[side ? a : b].entries.push_back(e);
                }
                if (!options_.live) {
                    MissingGame m;
                    m.round = round;
                    m.white = t_.rows[a].start < t_.rows[b].start ? a : b;
                    m.black = m.white == a ? b : a;
                    t_.missing.push_back(m);
                }
            }
        }
}

void CrosstableBuilder::sortEntries() {
    for (auto& row : t_.rows)
        std::stable_sort(row.entries.begin(), row.entries.end(), [](const CrossEntry& a, const CrossEntry& b) {
            const int ra = a.round > 0 ? a.round : INT32_MAX, rb = b.round > 0 ? b.round : INT32_MAX;
            return ra < rb;
        });
}

void CrosstableBuilder::countPoints() {
    score_.assign(n_, 0);
    unsure_.assign(n_, false);
    for (uint32_t p = 0; p < n_; ++p) {
        auto& row = t_.rows[p];
        for (const auto& e : row.entries) {
            if (e.known) row.points += e.points;
            if (!e.known && e.kind != CellKind::Live && e.kind != CellKind::ToCome) ++row.unknown;
            if (e.known && (e.kind == CellKind::Game || e.kind == CellKind::Forfeit)) ++row.played;
        }
        score_[p]  = row.points;
        unsure_[p] = row.unknown > 0;
    }
}

}  // namespace fcg::study
