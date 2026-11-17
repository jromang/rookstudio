#include "review.hpp"

#include <algorithm>
#include <cmath>

#include "facts.hpp"
#include "line.hpp"

namespace fcg::commentary {

namespace {

constexpr double INACCURACY = 5.0;
constexpr double MISTAKE    = 10.0;
constexpr double BLUNDER    = 15.0;

// Points of the engine's expected score a mistake and a blunder cost at least, whatever the chances between people say:
// from level to clearly worse, from winning to level.
constexpr double DAMAGE = 20.0;
constexpr double RUIN   = 35.0;

// An only move's second best must not win anyway.
constexpr double ONLY_MOVE_SECOND_MAX = 90.0;

// A side with less than this was lost anyway, and with less than that beyond saving: what it then drops is a mistake at
// most, or an inaccuracy. The same from the top for a side that still wins after its move.
constexpr double LOST          = 10.0;
constexpr double BEYOND_SAVING = 3.0;

// Plans marked in a game, at most: more would be a lecture.
constexpr int PLANS = 3;

// From what the move costs a player, in points of the chances between people.
std::optional<Judgment> fromLoss(double loss) {
    if (loss >= BLUNDER) return Judgment::Blunder;
    if (loss >= MISTAKE) return Judgment::Mistake;
    if (loss >= INACCURACY) return Judgment::Inaccuracy;
    return std::nullopt;
}

// From what the move costs the position, in points of the engine's expected score.
std::optional<Judgment> fromDamage(double damage) {
    if (damage >= RUIN) return Judgment::Blunder;
    if (damage >= DAMAGE) return Judgment::Mistake;
    return std::nullopt;
}

// Option::max: none below any judgment.
std::optional<Judgment> worse(std::optional<Judgment> a, std::optional<Judgment> b) {
    if (!a) return b;
    if (!b) return a;
    return std::max(*a, *b);
}

// The judgment of a move that was not the engine's: `was` and `now` are the mover's expected score before and after
// it, `loss` what it cost in chances between people. Letting a mate in, or letting one go, is a blunder; otherwise the
// more severe of the two readings stands. Where the tablebases tell the outcome, only what the move does to it counts:
// against a certainty, the engine's centipawns say nothing. And a move is judged by how things stood anyway, as lichess
// judges mates: nothing is a blunder for a side already lost, or one that still wins.
std::optional<Judgment> judge(const PositionAnalysis& before, const PositionAnalysis& after, Color mover, double was,
                              double now, double loss) {
    const bool mated  = after.isMated(mover) && !before.isMated(mover);
    const bool let_go = before.hasMate(mover) && !after.hasMate(mover);
    const bool exact  = before.isExact() || after.isExact();
    std::optional<Judgment> judgment;
    if (mated || let_go)
        judgment = Judgment::Blunder;
    else
        judgment = worse(exact ? std::nullopt : fromLoss(loss), fromDamage(was - now));
    Judgment at_most = Judgment::Blunder;
    if (was < BEYOND_SAVING || now > 100.0 - BEYOND_SAVING)
        at_most = Judgment::Inaccuracy;
    else if (was < LOST || now > 100.0 - LOST)
        at_most = Judgment::Mistake;
    if (!judgment) return std::nullopt;
    return std::min(*judgment, at_most);
}

// Whether the position had a single good move, and by how much it led the second best.
std::pair<bool, double> onlyMove(const PositionAnalysis& analysis, Color mover) {
    const auto* best   = analysis.best();
    const auto* second = analysis.second();
    if (!best || !second) return {false, 0.0};
    const auto b = forSide(best->whiteExpected(), mover), s = forSide(second->whiteExpected(), mover);
    const auto gap = b - s;
    return {gap >= ONLY_MOVE_GAP && s < ONLY_MOVE_SECOND_MAX, gap};
}

// Marks the quiet moves that carry out a plan the books name, as the facts of the move and of the engine's answer to it
// tell: the first of each kind for each side, a few per game. A mistake, an only move or theory that starts a plan is
// commented for what it is.
void markPlans(const MainLine& line, const Analyses& analyses, std::vector<MoveReport>& moves) {
    std::vector<std::pair<std::size_t, Color>> seen;
    int marked = 0;
    for (auto& m : moves) {
        const auto& before = line.positions()[m.ply - 1];
        const auto played  = line.moves()[m.ply - 1];
        std::vector<Move> moves_line{played};
        if (m.ply < analyses.size() && analyses[m.ply]) {
            if (const auto* reply = analyses[m.ply]->best())
                moves_line.insert(moves_line.end(), reply->moves.begin(), reply->moves.end());
        }
        for (const auto& fact : lineFacts(before, moves_line)) {
            // Pawns are thrown at a king by the move that pushes one.
            const bool pushed = played.role() == Role::Pawn;
            const bool storm  = std::holds_alternative<fact::PawnStorm>(fact);
            const std::pair kind{fact.index(), m.mover};
            if (!isPlan(fact) || beneficiary(fact) != m.mover || (storm && !pushed) ||
                std::find(seen.begin(), seen.end(), kind) != seen.end())
                continue;
            seen.push_back(kind);
            const bool quiet = !m.judgment && !m.only_move && !m.book;
            if (quiet && !m.plan && marked < PLANS) {
                m.plan = true;
                ++marked;
            }
        }
    }
}

// The moves that decided the game: the worst mistakes first, then the most striking only moves.
std::vector<std::size_t> markKeyMoments(std::vector<MoveReport>& moves) {
    const std::size_t wanted = std::clamp<std::size_t>(moves.size() / 10, 3, 7);
    const auto ranked        = [&](auto weight) {
        std::vector<std::pair<double, std::size_t>> list;
        for (std::size_t i = 0; i < moves.size(); ++i) {
            if (const auto w = weight(moves[i])) list.emplace_back(*w, i);
        }
        std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        return list;
    };
    const auto mistakes   = ranked([](const MoveReport& m) -> std::optional<double> {
        if (!m.judgment) return std::nullopt;
        return m.loss();
    });
    const auto only_moves = ranked([](const MoveReport& m) -> std::optional<double> {
        if (!m.only_move) return std::nullopt;
        return m.only_gap;
    });
    std::vector<std::size_t> chosen;
    for (const auto* list : {&mistakes, &only_moves}) {
        for (const auto& [w, i] : *list) {
            if (chosen.size() < wanted) chosen.push_back(i);
        }
    }
    std::sort(chosen.begin(), chosen.end());
    std::vector<std::size_t> plies;
    for (const auto i : chosen) {
        moves[i].key = true;
        plies.push_back(moves[i].ply);
    }
    return plies;
}

double stdDev(std::span<const double> values) {
    const auto n = static_cast<double>(values.size());
    double mean  = 0.0;
    for (const auto v : values) mean += v;
    mean /= n;
    double sum = 0.0;
    for (const auto v : values) sum += (v - mean) * (v - mean);
    return std::sqrt(sum / n);
}

// Lichess's game accuracy: the mean of a volatility-weighted mean and the harmonic mean of the move accuracies. Moves
// in sharp phases weigh more; one bad move weighs a lot. `chances` are each mover's chances between people before and
// after its move.
std::optional<double> gameAccuracy(const std::vector<MoveReport>& moves,
                                   const std::vector<std::pair<double, double>>& chances, Color color) {
    if (moves.empty() || chances.empty()) return std::nullopt;
    // White's chances along the game: before the first move, then after each move.
    std::vector<double> whites{forSide(chances.front().first, moves.front().mover)};
    for (std::size_t i = 0; i < moves.size() && i < chances.size(); ++i)
        whites.push_back(forSide(chances[i].second, moves[i].mover));
    const std::size_t window = std::clamp<std::size_t>(moves.size() / 10, 2, 8);
    const std::size_t k      = std::min(window, whites.size());
    std::vector<std::span<const double>> windows(k >= 2 ? k - 2 : 0, std::span<const double>(whites).first(k));
    for (std::size_t i = 0; i + k <= whites.size(); ++i)
        windows.push_back(std::span<const double>(whites).subspan(i, k));
    const auto weight = [&](std::size_t i) {
        return i < windows.size() ? std::clamp(stdDev(windows[i]), 0.5, 12.0) : 0.5;
    };
    std::vector<std::pair<double, double>> own;
    for (std::size_t i = 0; i < moves.size(); ++i) {
        if (moves[i].mover == color) own.emplace_back(moves[i].accuracy, weight(i));
    }
    if (own.empty()) return std::nullopt;
    double total_weight = 0.0, weighted = 0.0, inverse = 0.0;
    for (const auto& [a, w] : own) {
        total_weight += w;
        weighted += a * w;
        inverse += 1.0 / std::max(a, 0.001);
    }
    weighted /= total_weight;
    const double harmonic = static_cast<double>(own.size()) / inverse;
    return (weighted + harmonic) / 2.0;
}

SideReport side(const std::vector<MoveReport>& moves, const std::vector<std::pair<double, double>>& chances,
                Color color, bool complete) {
    SideReport out;
    const auto mean = [](const std::vector<double>& values) -> std::optional<double> {
        if (values.empty()) return std::nullopt;
        double sum = 0.0;
        for (const auto v : values) sum += v;
        return sum / static_cast<double>(values.size());
    };
    std::array<std::vector<double>, 3> by_phase;
    std::vector<double> losses;
    for (const auto& m : moves) {
        if (m.mover != color) continue;
        by_phase[static_cast<int>(m.phase)].push_back(m.accuracy);
        losses.push_back(static_cast<double>(m.cp_loss));
        if (m.judgment == Judgment::Inaccuracy) ++out.inaccuracies;
        if (m.judgment == Judgment::Mistake) ++out.mistakes;
        if (m.judgment == Judgment::Blunder) ++out.blunders;
    }
    for (std::size_t p = 0; p < 3; ++p) out.phase_accuracy[p] = mean(by_phase[p]);
    if (complete) out.accuracy = gameAccuracy(moves, chances, color);
    out.acpl = mean(losses);
    return out;
}

}  // namespace

MainLine::MainLine(Position start, std::vector<Move> moves) : moves_(std::move(moves)) {
    positions_.reserve(moves_.size() + 1);
    positions_.push_back(std::move(start));
    for (const auto& m : moves_) {
        auto next = positions_.back();
        next.playUnchecked(m);
        positions_.push_back(std::move(next));
    }
}

std::optional<Move> MainLine::moveTo(std::size_t ply) const {
    if (ply == 0 || ply > moves_.size()) return std::nullopt;
    return moves_[ply - 1];
}

std::vector<Phase> phases(std::span<const Position> positions) {
    // Queens, rooks, bishops and knights of both sides.
    const auto majors_and_minors = [](const Board& b) { return (b.occupied() & ~b.pawns() & ~b.kings()).count(); };
    // A side has moved most of its pieces off its back rank.
    const auto backrank_sparse = [](const Board& b) {
        const auto white = b.byColor(Color::White) & Bitboard::rank(Rank::First);
        const auto black = b.byColor(Color::Black) & Bitboard::rank(Rank::Eighth);
        return white.count() < 4 || black.count() < 4;
    };
    auto phase = Phase::Opening;
    std::vector<Phase> out;
    out.reserve(positions.size());
    for (const auto& pos : positions) {
        const auto& board = pos.board();
        if (phase == Phase::Opening && (majors_and_minors(board) <= 10 || backrank_sparse(board)))
            phase = Phase::Middlegame;
        if (phase == Phase::Middlegame && majors_and_minors(board) <= 6) phase = Phase::Endgame;
        out.push_back(phase);
    }
    return out;
}

uint8_t nag(Judgment judgment) noexcept {
    switch (judgment) {
        case Judgment::Inaccuracy:
            return 6;
        case Judgment::Mistake:
            return 2;
        default:
            return 4;
    }
}

const MoveReport* GameReport::get(std::size_t ply) const {
    // Moves are in game order: their plies increase.
    const auto it =
        std::lower_bound(moves.begin(), moves.end(), ply, [](const MoveReport& m, std::size_t p) { return m.ply < p; });
    return it != moves.end() && it->ply == ply ? &*it : nullptr;
}

std::optional<Weighed> weigh(const PositionAnalysis& before, const PositionAnalysis& after, Color mover,
                             const Move& played) {
    const auto cp_before = before.whiteCp(), cp_after = after.whiteCp();
    const auto e_before = before.whiteExpected(), e_after = after.whiteExpected();
    if (!cp_before || !cp_after || !e_before || !e_after) return std::nullopt;
    const auto* best       = before.best();
    const bool played_best = best && !best->moves.empty() && best->moves.front() == played;
    // Playing the engine's move loses nothing: a lower score after it is search noise.
    const auto kept       = [&](double was, double now) { return played_best ? std::max(now, was) : now; };
    const auto win_before = forSide(*e_before, mover);
    const auto win_after  = kept(win_before, forSide(*e_after, mover));
    // Against a certainty of the tablebases, the engine's centipawns say nothing: the chances are the outcome's, and
    // nothing is lost unless the outcome is.
    const bool exact = before.isExact() || after.isExact();
    std::pair<double, double> chances{win_before, win_after};
    if (!exact) {
        const auto was = winPercentFor(*cp_before, mover);
        chances        = {was, kept(was, winPercentFor(*cp_after, mover))};
    }
    const bool unharmed    = exact && win_before - win_after < DAMAGE;
    const auto pov         = [&](int32_t cp) { return mover == Color::White ? cp : -cp; };
    const auto [only, gap] = onlyMove(before, mover);
    Weighed w;
    w.win_before = win_before;
    w.win_after  = win_after;
    w.chances    = chances;
    w.accuracy   = moveAccuracy(chances.first, chances.second);
    w.cp_loss    = played_best || unharmed ? 0 : std::max(pov(*cp_before) - pov(*cp_after), 0);
    w.judgment =
        played_best ? std::nullopt : judge(before, after, mover, win_before, win_after, chances.first - chances.second);
    w.played_best = played_best;
    w.only        = only;
    w.only_gap    = gap;
    return w;
}

bool takesBackTheOnlyWay(const Position& position, const std::optional<Move>& previous,
                         const std::optional<Move>& played) {
    if (!previous || !played) return false;
    const bool retakes = previous->isCapture() && played->isCapture() && played->to() == previous->to();
    std::size_t ways   = 0;
    for (const auto& m : position.legalMoves()) {
        if (m.isCapture() && m.to() == previous->to()) ++ways;
    }
    return retakes && ways == 1;
}

double moveAccuracy(double win_before, double win_after) noexcept {
    if (win_after >= win_before) return 100.0;
    const double raw =
        103.1668100711649 * std::exp(-0.04354415386753951 * (win_before - win_after)) - 3.166924740191411;
    // +1: lichess's allowance for the uncertainty of the evaluations.
    return std::clamp(raw + 1.0, 0.0, 100.0);
}

GameReport review(const MainLine& line, const Analyses& analyses, const Opening& opening) {
    const auto& positions = line.positions();
    const auto phase_of   = phases(positions);
    const auto analysis   = [&](std::size_t i) -> const PositionAnalysis* {
        return i < analyses.size() && analyses[i] ? &*analyses[i] : nullptr;
    };
    GameReport report;
    auto& moves = report.moves;
    // The chances between people before and after each move, for the accuracies.
    std::vector<std::pair<double, double>> chances;
    for (std::size_t i = 1; i < positions.size(); ++i) {
        const auto* a_before = analysis(i - 1);
        const auto* a_after  = analysis(i);
        if (!a_before || !a_after) continue;
        const auto& position = positions[i - 1];
        const auto mover     = position.turn();
        const auto played    = line.moves()[i - 1];
        const auto weighed   = weigh(*a_before, *a_after, mover, played);
        if (!weighed) continue;
        const bool forced = takesBackTheOnlyWay(position, line.moveTo(i - 1), played);
        // Plies from the start: the move to position i is ply i - 1, counted from 0.
        const bool book = i <= opening.book_plies;
        chances.push_back(weighed->chances);
        MoveReport m;
        m.ply         = i;
        m.mover       = mover;
        m.phase       = phase_of[i - 1];
        m.win_before  = weighed->win_before;
        m.win_after   = weighed->win_after;
        m.accuracy    = weighed->accuracy;
        m.cp_loss     = weighed->cp_loss;
        m.judgment    = book ? std::nullopt : weighed->judgment;
        m.played_best = weighed->played_best;
        m.only_move   = weighed->only && weighed->played_best && !book && !forced;
        m.only_gap    = weighed->only_gap;
        m.book        = book;
        m.novelty     = opening.novelty == i - 1;
        moves.push_back(m);
    }
    markPlans(line, analyses, moves);
    report.complete    = moves.size() + 1 == positions.size();
    report.key_moments = markKeyMoments(moves);
    report.white       = side(moves, chances, Color::White, report.complete);
    report.black       = side(moves, chances, Color::Black, report.complete);
    return report;
}

}  // namespace fcg::commentary
