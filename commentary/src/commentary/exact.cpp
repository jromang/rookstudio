#include "exact.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace fcg::commentary {

namespace {

// The score and the win, draw and loss figures, from White's side, of a line whose first move leaves the side to move
// with `verdict`.
std::pair<Score, std::array<uint16_t, 3>> exact(Verdict verdict, Color mover) {
    switch (mover == Color::White ? verdict : flip(verdict)) {
        case Verdict::Win:
            return {Score::cp(CP_CAP), {1000, 0, 0}};
        case Verdict::Loss:
            return {Score::cp(-CP_CAP), {0, 0, 1000}};
        case Verdict::Draw:
            break;
    }
    return {Score::cp(0), {0, 1000, 0}};
}

int rank(std::optional<Verdict> verdict) {
    if (verdict == Verdict::Win) return 0;
    if (verdict == Verdict::Loss) return 2;
    return 1;
}

}  // namespace

bool settle(const Position& position, PositionAnalysis& analysis, const Tablebases& tables) {
    if (analysis.end || analysis.lines.empty()) return false;
    const auto now = tables.verdict(position);
    if (!now) return false;
    const Color mover       = position.turn();
    const auto before       = analysis;
    const std::size_t asked = analysis.lines.size();
    const auto outcome      = [&](const EngineLine& line) -> std::optional<Verdict> {
        if (line.moves.empty()) return std::nullopt;
        return tables.after(position, line.moves.front());
    };
    for (auto& line : analysis.lines) {
        const auto verdict = outcome(line);
        if (!verdict) continue;
        const auto [score, wdl] = exact(*verdict, mover);
        // A mate the engine sees is a win all the same, and says how far.
        const bool mates = line.score.kind == Score::Kind::Mate && (line.score.value > 0) == (mover == Color::White);
        if (!(mates && *verdict == Verdict::Win)) line.score = score;
        line.wdl = wdl;
    }
    std::stable_sort(analysis.lines.begin(), analysis.lines.end(),
                     [&](const EngineLine& a, const EngineLine& b) { return rank(outcome(a)) < rank(outcome(b)); });
    // The moves that keep the outcome, the tables' own choice first.
    std::vector<Move> keeping;
    for (const auto& m : position.legalMoves())
        if (tables.after(position, m) == now) keeping.push_back(m);
    if (const auto best = tables.best(position)) {
        if (const auto it = std::find(keeping.begin(), keeping.end(), *best); it != keeping.end())
            std::iter_swap(keeping.begin(), it);
    }
    const auto bare = [&](const Move& mv) {
        const auto [score, wdl] = exact(*now, mover);
        return EngineLine{{mv}, score, wdl, 0};
    };
    const auto keeps = [&](const EngineLine& line) { return outcome(line) == now; };
    if (!keeps(analysis.lines.front()) && !keeping.empty())
        analysis.lines.insert(analysis.lines.begin(), bare(keeping.front()));
    const auto& leading             = analysis.lines.front().moves;
    const std::optional<Move> first = leading.empty() ? std::nullopt : std::optional<Move>(leading.front());
    if (asked >= 2 && !(analysis.lines.size() > 1 && keeps(analysis.lines[1]))) {
        const auto it =
            std::find_if(keeping.begin(), keeping.end(), [&](const Move& m) { return !first || !(m == *first); });
        if (it != keeping.end()) analysis.lines.insert(analysis.lines.begin() + 1, bare(*it));
    }
    // A single line, the engine's or the tables': the best of the other moves after it, by their outcome, so that an
    // only move can be told where the analysis gave one line (RookStudio's review keeps one).
    if (analysis.lines.size() == 1 && !analysis.lines.front().moves.empty()) {
        const auto lead = analysis.lines.front().moves.front();
        std::optional<std::pair<Move, Verdict>> other;
        for (const auto& m : position.legalMoves()) {
            if (m == lead) continue;
            const auto v = tables.after(position, m);
            if (v && (!other || rank(v) < rank(other->second))) other = std::pair{m, *v};
        }
        if (other) {
            const auto [score, wdl] = exact(other->second, mover);
            analysis.lines.push_back(EngineLine{{other->first}, score, wdl, 0});
        }
    }
    if (const auto keep = std::max<std::size_t>(asked, 2); analysis.lines.size() > keep) analysis.lines.resize(keep);
    return !(analysis == before);
}

}  // namespace fcg::commentary
