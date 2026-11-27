// The crosstable's figures (crosstable.hpp), the builder's last steps: the categories of a Swiss's unplayed rounds and
// the scores they adjust (C.07 16), each player's tie-breaks (C.07 Art. 6 to 10, 14), performance and rating change
// (B.02 8.3), then the standings the tie-breaks order (C.07 4.2).

#include <algorithm>
#include <cstdlib>
#include <map>
#include <numeric>
#include <optional>
#include <vector>

#include "crosstable.hpp"
#include "crosstable_detail.hpp"

namespace fcg::study::crosstable {

void CrosstableBuilder::categorise() {
    categories_.resize(n_);
    adjusted_ = score_;
    for (uint32_t p = 0; p < n_; ++p) {
        const auto& entries = t_.rows[p].entries;
        auto& cats          = categories_[p];
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
            if (!vur(cats[i]) && entries[i].kind != CellKind::ToCome) later_played = true;
        }
        if (swiss_)
            for (std::size_t i = 0; i < entries.size(); ++i)
                if (cats[i] == Category::RequestedLast)
                    adjusted_[p] += 0.5 - (entries[i].known ? entries[i].points : 0);
    }
}

std::vector<Contribution> CrosstableBuilder::contributions(uint32_t player, bool& unsure) const {
    const double draw_rounds = 0.5 * std::max(t_.rounds, 1);
    const auto& entries      = t_.rows[player].entries;
    const auto& cats         = categories_[player];
    std::vector<Contribution> parts;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        if (e.kind == CellKind::Live || e.kind == CellKind::ToCome) continue;
        Contribution part;
        part.vur = swiss_ && vur(cats[i]);
        if (!swiss_) {
            // A round robin: the opponents' final scores, forfeits as games (15.2); its rounds without a game are
            // none.
            if (e.opponent == NO_PLAYER) continue;
            part.opponent = score_[e.opponent];
            part.bh       = part.opponent;
            part.sb       = part.opponent * (e.known ? e.points : 0);
            if (unsure_[e.opponent]) unsure = true;
        } else if (cats[i] == Category::Played) {
            if (e.opponent == NO_PLAYER) continue;
            part.opponent = adjusted_[e.opponent];
            part.bh       = part.opponent;
            part.sb       = part.opponent * (e.known ? e.points : 0);
            if (unsure_[e.opponent]) unsure = true;
        } else {
            // A dummy whose score is the player's own, capped (16.4).
            double dummy = score_[player];
            if (cats[i] == Category::ForfeitWin || cats[i] == Category::ForfeitLoss) {
                if (e.opponent != NO_PLAYER) dummy = std::min(dummy, adjusted_[e.opponent]);
            } else {
                dummy = std::min(dummy, draw_rounds);
            }
            part.opponent = dummy;
            part.bh       = dummy;
            part.sb       = dummy * (e.known ? e.points : 0);
        }
        parts.push_back(part);
    }
    return parts;
}

double cutSum(std::vector<Contribution> list, bool sb, int lowest, int highest) {
    const auto value = [&](const Contribution& x) { return sb ? x.sb : x.bh; };
    for (int c = 0; c < lowest && !list.empty(); ++c) {
        // The least significant value: the lowest contribution (for SB, that of the opponent with the lowest score,
        // the lowest of theirs).
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
}

void CrosstableBuilder::computeTiebreaks() {
    const std::size_t tbs = t_.tiebreaks.size();
    for (auto& row : t_.rows) {
        row.tiebreaks.assign(tbs, 0);
        row.states.assign(tbs, TiebreakState::Known);
    }
    const double max_score =
        rr_ ? static_cast<double>(t_.system.cycles) * (n_ > 0 ? n_ - 1 : 0) : static_cast<double>(t_.rounds);
    for (uint32_t p = 0; p < n_; ++p) {
        auto& row             = t_.rows[p];
        const auto& entries   = row.entries;
        bool opponents_unsure = false;
        const auto parts      = contributions(p, opponents_unsure);
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
            if (e.opponent == NO_PLAYER || t_.players[e.opponent].elo <= 0) continue;
            ratings.push_back(t_.players[e.opponent].elo);
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
        const bool own_unsure = unsure_[p];
        for (std::size_t k = 0; k < tbs; ++k) {
            double v    = 0;
            bool unsure = own_unsure;
            switch (t_.tiebreaks[k]) {
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
                    const int last = std::max(t_.rounds, by_round.empty() ? 0 : by_round.rbegin()->first);
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
                    v      = cutSum(parts, false, 1, 0);
                    unsure = unsure || opponents_unsure;
                    break;
                case Tiebreak::BuchholzMedian1:
                    v      = cutSum(parts, false, 1, 1);
                    unsure = unsure || opponents_unsure;
                    break;
                case Tiebreak::SonnebornBerger:
                    for (const auto& x : parts) v += x.sb;
                    unsure = unsure || opponents_unsure;
                    break;
                case Tiebreak::Koya:
                    for (const auto& e : entries)
                        if (e.known && e.opponent != NO_PLAYER && score_[e.opponent] + EPSILON >= max_score / 2 &&
                            (e.kind == CellKind::Game || e.kind == CellKind::Forfeit)) {
                            v += e.points;
                            if (unsure_[e.opponent]) unsure = true;
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
    }
}

void CrosstableBuilder::computeRatingChanges() {
    // The games played over the board against a rated opponent, both having moved.
    for (uint32_t p = 0; p < n_; ++p) {
        auto& row     = t_.rows[p];
        const int own = t_.players[p].elo;
        if (own <= 0) continue;
        int games_rated = 0;
        double expected = 0, scored = 0;
        for (const auto& e : row.entries) {
            if (e.kind != CellKind::Game || e.opponent == NO_PLAYER) continue;
            const int other = t_.players[e.opponent].elo;
            if (other <= 0) continue;
            const auto& game = t_.games[e.game];
            if (game.plies < 2 && !game.hand) continue;
            int d = own - other;
            if (options_.kind == RatingKind::Standard) {
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
        const auto& player = t_.players[p];
        row.k_listed       = player.k > 0;
        row.k              = fideK(own, player.born, options_.year, player.k, games_rated);
        if (games_rated > 0) row.change = row.k * (scored - expected);
    }
}

std::optional<double> CrosstableBuilder::encounter(uint32_t a, uint32_t b) const {
    // The games played over the board, and in a round robin the forfeits too (C.07 6.1.1, 15.2).
    double sum = 0;
    int count  = 0;
    for (const auto& e : t_.rows[a].entries) {
        if (e.opponent != b || !e.known) continue;
        if (e.kind == CellKind::Game || (!swiss_ && e.kind == CellKind::Forfeit)) {
            sum += e.points;
            ++count;
        }
    }
    if (count == 0) return std::nullopt;
    return sum / count;
}

std::vector<std::vector<uint32_t>> CrosstableBuilder::directEncounter(const std::vector<uint32_t>& members,
                                                                      std::vector<double>& de,
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
    if (!swiss_) return {members};
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

void CrosstableBuilder::rankTie(std::vector<uint32_t> group, std::size_t k, std::vector<double>& de,
                                std::vector<bool>& applied, std::vector<std::vector<uint32_t>>& classes) {
    if (group.size() <= 1 || k >= t_.tiebreaks.size()) {
        std::stable_sort(group.begin(), group.end(),
                         [&](uint32_t x, uint32_t y) { return t_.rows[x].start < t_.rows[y].start; });
        classes.push_back(std::move(group));
        return;
    }
    if (t_.tiebreaks[k] == Tiebreak::DirectEncounter) {
        for (auto& c : directEncounter(group, de, applied)) rankTie(std::move(c), k + 1, de, applied, classes);
        return;
    }
    std::stable_sort(group.begin(), group.end(), [&](uint32_t x, uint32_t y) {
        return t_.rows[x].tiebreaks[k] > t_.rows[y].tiebreaks[k] + EPSILON;
    });
    std::vector<uint32_t> part;
    for (const auto p : group) {
        if (!part.empty() && !same(t_.rows[part.front()].tiebreaks[k], t_.rows[p].tiebreaks[k])) {
            rankTie(std::move(part), k + 1, de, applied, classes);
            part.clear();
        }
        part.push_back(p);
    }
    if (!part.empty()) rankTie(std::move(part), k + 1, de, applied, classes);
}

void CrosstableBuilder::rank() {
    std::vector<double> de(n_, 0);
    std::vector<bool> de_applied(n_, false);
    std::vector<std::vector<uint32_t>> classes;
    std::vector<uint32_t> all(n_);
    std::iota(all.begin(), all.end(), 0u);
    std::stable_sort(all.begin(), all.end(), [&](uint32_t x, uint32_t y) {
        if (!same(score_[x], score_[y])) return score_[x] > score_[y];
        return t_.rows[x].start < t_.rows[y].start;
    });
    std::vector<uint32_t> part;
    for (const auto p : all) {
        if (!part.empty() && !same(score_[part.front()], score_[p])) {
            rankTie(std::move(part), 0, de, de_applied, classes);
            part.clear();
        }
        part.push_back(p);
    }
    if (!part.empty()) rankTie(std::move(part), 0, de, de_applied, classes);
    for (std::size_t k = 0; k < t_.tiebreaks.size(); ++k)
        if (t_.tiebreaks[k] == Tiebreak::DirectEncounter)
            for (uint32_t p = 0; p < n_; ++p) {
                t_.rows[p].tiebreaks[k] = de[p];
                t_.rows[p].states[k] =
                    de_applied[p] ? (unsure_[p] ? TiebreakState::Unsure : TiebreakState::Known) : TiebreakState::None;
            }
    std::vector<CrossRow> ordered;
    ordered.reserve(n_);
    int place = 1;
    for (const auto& c : classes) {
        for (const auto p : c) {
            auto row       = std::move(t_.rows[p]);
            row.place_from = place;
            row.place_to   = place + static_cast<int>(c.size()) - 1;
            ordered.push_back(std::move(row));
        }
        place += static_cast<int>(c.size());
    }
    t_.rows = std::move(ordered);
    t_.row_of.assign(n_, UINT32_MAX);
    for (uint32_t r = 0; r < t_.rows.size(); ++r) t_.row_of[t_.rows[r].player] = r;
}

}  // namespace fcg::study::crosstable
