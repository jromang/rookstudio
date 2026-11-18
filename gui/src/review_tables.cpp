#include "review_tables.hpp"

#include <utility>

#include "online_cache.hpp"

namespace fcg {

std::optional<commentary::Verdict> verdictOf(lichess::Outcome outcome) {
    switch (outcome) {
        case lichess::Outcome::Win:
        case lichess::Outcome::MaybeWin:
            return commentary::Verdict::Win;
        case lichess::Outcome::Loss:
        case lichess::Outcome::MaybeLoss:
            return commentary::Verdict::Loss;
        case lichess::Outcome::Draw:
        case lichess::Outcome::CursedWin:
        case lichess::Outcome::BlessedLoss:
            return commentary::Verdict::Draw;
        case lichess::Outcome::Unknown:
            break;
    }
    return std::nullopt;
}

bool teachTables(commentary::Tablebases& tables, const commentary::Position& pos, const lichess::Tablebase& answer) {
    const auto now = verdictOf(answer.outcome);
    if (!now) return false;
    std::vector<std::pair<commentary::Move, commentary::Verdict>> moves;
    for (const auto& m : answer.moves) {
        const auto mv = pos.parseUci(m.uci);
        if (!mv) return false;
        // The outcome Lichess gives a move is the opponent's, to move after it.
        if (const auto theirs = verdictOf(m.outcome)) moves.emplace_back(*mv, commentary::flip(*theirs));
    }
    tables.learn(pos, *now, moves);
    return true;
}

std::vector<TablePosition> tablePositions(const commentary::MainLine& line, const commentary::Tablebases& tables) {
    std::vector<TablePosition> out;
    const auto& positions = line.positions();
    for (std::size_t i = 0; i < positions.size(); ++i)
        if (tables.wants(positions[i])) out.push_back({i, positions[i].fen()});
    return out;
}

std::string askTables(const std::vector<TablePosition>& positions, const commentary::MainLine& line,
                      const std::filesystem::path& profile, commentary::Tablebases& tables,
                      const std::atomic<bool>& cancel, std::atomic<int>& done) {
    const auto& all = line.positions();
    for (const auto& p : positions) {
        if (cancel) return {};
        if (p.index >= all.size()) break;
        const auto& pos = all[p.index];
        // An answer an earlier move taught whole, or one the profile kept.
        if (tables.wants(pos)) {
            if (const auto kept = cachedTablebase(profile, p.fen)) {
                teachTables(tables, pos, *kept);
            } else {
                const auto answer = lichess::tablebase(p.fen, &cancel);
                if (cancel) return {};
                if (!answer.value) {
                    if (!answer.error.empty()) return answer.error;
                } else if (teachTables(tables, pos, *answer.value)) {
                    keepTablebase(profile, p.fen, *answer.value);
                }
            }
        }
        ++done;
    }
    return {};
}

}  // namespace fcg
