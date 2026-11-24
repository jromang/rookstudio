#include "practical.hpp"

#include <algorithm>
#include <map>

#include <study/scan_board.hpp>

#include "endgames.hpp"

namespace fcg {

const std::vector<int>& practicalLevels() {
    static const std::vector<int> levels{1100, 1300, 1500, 1700, 1900, 2100, 2350, 2600};
    return levels;
}

int explorerGroup(int level) {
    // Lichess's groups start at 0, 1000, 1200, 1400, 1600, 1800, 2000, 2200 and 2500.
    static constexpr int groups[] = {2500, 2200, 2000, 1800, 1600, 1400, 1200, 1000};
    for (const int g : groups)
        if (level >= g) return g;
    return 0;
}

std::pair<int, int> levelBand(int level) {
    const int group = explorerGroup(level);
    switch (group) {
        case 2500:
            return {2500, 4000};
        case 2200:
            return {2200, 2500};
        case 0:
            return {0, 1000};
        default:
            return {group, group + 200};
    }
}

const std::vector<std::string>& practicalSpeeds() {
    static const std::vector<std::string> speeds{"blitz", "rapid", "classical", "correspondence"};
    return speeds;
}

int scoreCp(const fastchess::Score& score) {
    if (score.type == fastchess::ScoreType::MATE)
        return score.value > 0 ? 30000 - static_cast<int>(score.value) : -30000 - static_cast<int>(score.value);
    return static_cast<int>(score.value);
}

std::vector<LevelMove> levelMoves(const lichess::Explorer& answer) {
    std::vector<LevelMove> out;
    for (const auto& m : answer.moves) out.push_back({m.uci, m.white + m.draws + m.black, m.white, m.draws, m.black});
    std::stable_sort(out.begin(), out.end(), [](const LevelMove& a, const LevelMove& b) { return a.games > b.games; });
    return out;
}

std::vector<LevelMove> levelMoves(const std::vector<LevelSource>& sources, const chess::Board& board, int level,
                                  const std::atomic<bool>* cancel) {
    const auto [low, high] = levelBand(level);
    study::ScanBoard scan;
    if (!scan.set(board)) return {};
    std::map<std::string, LevelMove> by_uci;
    for (const auto& s : sources) {
        if (!s.tree || !s.snapshot || s.tree->stale(*s.snapshot)) continue;
        std::map<int, LevelMove> by_code;
        for (const auto& g : s.tree->gamesAt(*s.snapshot, scan.key())) {
            if (cancel && *cancel) return {};
            if (g.code == study::TreeMove::END) continue;
            const auto n = s.snapshot->numbers(g.id);
            if (n.white_elo == 0 || n.black_elo == 0) continue;
            const int average = (static_cast<int>(n.white_elo) + static_cast<int>(n.black_elo)) / 2;
            if (average < low || average >= high) continue;
            auto& m = by_code[g.code];
            ++m.games;
            if (n.result == '1')
                ++m.white;
            else if (n.result == '=')
                ++m.draws;
            else if (n.result == '0')
                ++m.black;
        }
        for (const auto& [code, m] : by_code) {
            const auto move = study::moveOfCode(board, scan, code);
            if (!move) continue;
            const auto uci = chess::uci::moveToUci(*move, board.chess960());
            auto& t        = by_uci[uci];
            t.uci          = uci;
            t.games += m.games;
            t.white += m.white;
            t.draws += m.draws;
            t.black += m.black;
        }
    }
    std::vector<LevelMove> out;
    for (auto& [uci, m] : by_uci) out.push_back(std::move(m));
    std::stable_sort(out.begin(), out.end(), [](const LevelMove& a, const LevelMove& b) { return a.games > b.games; });
    return out;
}

std::optional<double> moverScore(const LevelMove& move, chess::Color mover) {
    const auto scored = move.white + move.draws + move.black;
    if (scored == 0) return std::nullopt;
    const auto won = mover == chess::Color::WHITE ? move.white : move.black;
    return 100.0 * (static_cast<double>(won) + 0.5 * static_cast<double>(move.draws)) / static_cast<double>(scored);
}

std::vector<PracticalMove> practicalCandidates(const std::vector<AnalysisLine>& lines,
                                               const lichess::Tablebase* tables) {
    std::vector<PracticalMove> out;
    if (lines.empty() || !lines.front().score || lines.front().pv.empty()) return out;
    const int best = scoreCp(*lines.front().score);
    // The tables' verdict, when they know the position: the moves that keep it, whatever the engine's window.
    const std::optional<int> verdict = tables ? outcomeResult(tables->outcome) : std::nullopt;
    for (const auto& l : lines) {
        if (!l.score || l.pv.empty()) continue;
        const int cp = scoreCp(*l.score);
        if (verdict) {
            const auto entry = std::find_if(tables->moves.begin(), tables->moves.end(),
                                            [&](const lichess::TablebaseMove& m) { return m.uci == l.pv.front(); });
            if (entry == tables->moves.end() || moveResult(*entry) != verdict) continue;
        } else {
            if (best - cp > PRACTICAL_WINDOW_CP) continue;
            if (cp <= -29000 && best > -29000) continue;                        // mated, where the best is not
            if (cp <= PRACTICAL_LOST_CP && best > PRACTICAL_LOST_CP) continue;  // lost, where the best is not
        }
        PracticalMove m;
        m.uci     = l.pv.front();
        m.score   = *l.score;
        m.depth   = l.depth;
        m.loss_cp = std::max(0, best - cp);
        m.pv      = l.pv;
        out.push_back(std::move(m));
    }
    return out;
}

PracticalChoice choosePractical(std::vector<PracticalMove>& candidates, const std::vector<LevelMove>& level,
                                chess::Color mover) {
    PracticalChoice choice;
    for (auto& c : candidates) {
        c.games = 0;
        c.expected.reset();
        const auto at = std::find_if(level.begin(), level.end(), [&](const LevelMove& m) { return m.uci == c.uci; });
        if (at == level.end()) continue;
        c.games = at->games;
        if (at->games >= PRACTICAL_GAMES) c.expected = moverScore(*at, mover);
    }
    std::optional<std::size_t> by_games, by_replies;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto& c = candidates[i];
        if (c.expected && (!by_games || *c.expected > *candidates[*by_games].expected)) by_games = i;
        if (c.holding && (!by_replies || *c.holding < *candidates[*by_replies].holding)) by_replies = i;
    }
    if (by_games) return {*by_games, PracticalWay::Games};
    if (by_replies && outsideKnownGames(candidates)) return {*by_replies, PracticalWay::Replies};
    return choice;
}

bool outsideKnownGames(const std::vector<PracticalMove>& candidates) {
    return std::none_of(candidates.begin(), candidates.end(), [](const PracticalMove& c) { return c.expected; });
}

int holdingReplies(const std::vector<AnalysisLine>& lines) {
    if (lines.empty() || !lines.front().score) return 0;
    const int best = scoreCp(*lines.front().score);
    return static_cast<int>(std::count_if(lines.begin(), lines.end(), [&](const AnalysisLine& l) {
        return l.score && best - scoreCp(*l.score) <= PRACTICAL_WINDOW_CP;
    }));
}

const char* replyMark(int cost_cp) {
    if (cost_cp >= 300) return "??";
    if (cost_cp >= 100) return "?";
    if (cost_cp >= 50) return "?!";
    return "";
}

}  // namespace fcg
