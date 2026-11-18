#include "review_bridge.hpp"

#include <algorithm>
#include <array>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fcg {

std::optional<commentary::MainLine> mainLineOf(const ReplayGame& game, bool chess960) {
    if (chess960) return std::nullopt;
    const auto start = commentary::Position::fromFen(game.start_fen.empty() ? std::string(chess::constants::STARTPOS)
                                                                            : game.start_fen);
    if (!start) return std::nullopt;
    std::vector<commentary::Move> moves;
    auto at = *start;
    for (const auto& m : game.moves) {
        const auto mv = at.parseUci(chess::uci::moveToUci(m.move, false));
        if (!mv) return std::nullopt;
        moves.push_back(*mv);
        at.playUnchecked(*mv);
    }
    return commentary::MainLine(*start, std::move(moves));
}

commentary::Analyses analysesOf(const commentary::MainLine& line, const GameReview& review) {
    const auto& positions = line.positions();
    commentary::Analyses out(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const auto& pos = positions[i];
        if (const auto end = commentary::gameEnd(pos)) {
            out[i] = commentary::PositionAnalysis::ended(*end);
            continue;
        }
        if (i >= review.positions.size()) continue;
        const auto& e = review.positions[i];
        if (!e.score || e.score->type.value == fastchess::ScoreType::ERR) continue;
        const bool white = pos.turn() == commentary::Color::White;
        commentary::EngineLine l;
        // The score and the win, draw and loss figures were the side to move's: White's here.
        const auto v = static_cast<int32_t>(white ? e.score->value : -e.score->value);
        l.score =
            e.score->type.value == fastchess::ScoreType::MATE ? commentary::Score::mate(v) : commentary::Score::cp(v);
        if (e.wdl) {
            const auto& w = *e.wdl;
            const auto pm = [](int64_t x) { return static_cast<uint16_t>(std::clamp<int64_t>(x, 0, 1000)); };
            l.wdl         = white ? std::array<uint16_t, 3>{pm(w[0]), pm(w[1]), pm(w[2])}
                                  : std::array<uint16_t, 3>{pm(w[2]), pm(w[1]), pm(w[0])};
        }
        l.depth = static_cast<uint32_t>(std::max(0, e.depth));
        // Its moves: the principal variation, else the best move alone; read as far as they are legal.
        std::istringstream in(e.pv.empty() ? e.best : e.pv);
        auto at = pos;
        for (std::string u; in >> u;) {
            const auto mv = at.parseUci(u);
            if (!mv) break;
            l.moves.push_back(*mv);
            at.playUnchecked(*mv);
        }
        if (l.moves.empty()) continue;
        commentary::PositionAnalysis a;
        a.lines.push_back(std::move(l));
        out[i] = std::move(a);
    }
    return out;
}

std::vector<MoveReview> judgedMoves(const CommentaryReview& review) {
    std::vector<MoveReview> out;
    for (const auto& m : review.report.moves) {
        if (m.ply == 0) continue;
        if (out.size() < m.ply) out.resize(m.ply);
        auto& j = out[m.ply - 1];
        j.loss  = m.loss();
        j.best  = m.played_best;
        if (m.judgment && !m.book)
            j.judgement = *m.judgment == commentary::Judgment::Blunder   ? Judgement::Blunder
                          : *m.judgment == commentary::Judgment::Mistake ? Judgement::Mistake
                                                                         : Judgement::Inaccuracy;
    }
    return out;
}

std::shared_ptr<const CommentaryReview> commentaryReview(const ReplayGame& game, bool chess960,
                                                         const GameReview& review, const commentary::Opening& opening) {
    auto line = mainLineOf(game, chess960);
    if (!line) return nullptr;
    auto analyses = analysesOf(*line, review);
    auto report   = commentary::review(*line, analyses, opening);
    return std::make_shared<const CommentaryReview>(
        CommentaryReview{std::move(*line), std::move(analyses), std::move(report)});
}

}  // namespace fcg
