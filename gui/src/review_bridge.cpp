#include "review_bridge.hpp"

#include <commentary/exact.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "game_graph.hpp"

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

namespace {

// An engine line from a position of the game: its score and its win, draw and loss figures turned to White's (they
// were the side to move's), its moves read as far as they are legal; none without a score or a legal move.
std::optional<commentary::EngineLine> lineOf(const commentary::Position& pos,
                                             const std::optional<fastchess::Score>& score,
                                             const std::optional<std::array<int64_t, 3>>& wdl, int depth,
                                             const std::string& moves) {
    if (!score || score->type.value == fastchess::ScoreType::ERR) return std::nullopt;
    const bool white = pos.turn() == commentary::Color::White;
    commentary::EngineLine l;
    const auto v = static_cast<int32_t>(white ? score->value : -score->value);
    l.score = score->type.value == fastchess::ScoreType::MATE ? commentary::Score::mate(v) : commentary::Score::cp(v);
    if (wdl) {
        const auto& w = *wdl;
        const auto pm = [](int64_t x) { return static_cast<uint16_t>(std::clamp<int64_t>(x, 0, 1000)); };
        l.wdl         = white ? std::array<uint16_t, 3>{pm(w[0]), pm(w[1]), pm(w[2])}
                              : std::array<uint16_t, 3>{pm(w[2]), pm(w[1]), pm(w[0])};
    }
    l.depth = static_cast<uint32_t>(std::max(0, depth));
    std::istringstream in(moves);
    auto at = pos;
    for (std::string u; in >> u;) {
        const auto mv = at.parseUci(u);
        if (!mv) break;
        l.moves.push_back(*mv);
        at.playUnchecked(*mv);
    }
    if (l.moves.empty()) return std::nullopt;
    return l;
}

}  // namespace

std::vector<bool> drawnByRule(const ReplayGame& game, bool chess960) {
    std::vector<bool> out;
    chess::Board board;
    board.set960(chess960);
    if (!game.start_fen.empty()) board.setFen(game.start_fen);
    const auto drawn = [&] {
        const auto [reason, result] = board.isGameOver();
        return result == chess::GameResult::DRAW && (reason == chess::GameResultReason::THREEFOLD_REPETITION ||
                                                     reason == chess::GameResultReason::FIFTY_MOVE_RULE);
    };
    out.push_back(drawn());
    for (const auto& m : game.moves) {
        board.makeMove(m.move);
        out.push_back(drawn());
    }
    return out;
}

commentary::Analyses analysesOf(const commentary::MainLine& line, const GameReview& review,
                                const std::vector<bool>& drawn) {
    const auto& positions = line.positions();
    commentary::Analyses out(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const auto& pos = positions[i];
        if (const auto end = commentary::gameEnd(pos)) {
            out[i] = commentary::PositionAnalysis::ended(*end);
            continue;
        }
        // A draw the position alone does not tell: the analysis gave it the end's score, and no line.
        if (i < drawn.size() && drawn[i]) {
            out[i] = commentary::PositionAnalysis::ended(commentary::GameEnd::draw());
            continue;
        }
        if (i >= review.positions.size()) continue;
        const auto& e = review.positions[i];
        // Its moves: the principal variation, else the best move alone.
        auto best = lineOf(pos, e.score, e.wdl, e.depth, e.pv.empty() ? e.best : e.pv);
        if (!best) continue;
        commentary::PositionAnalysis a;
        a.lines.push_back(std::move(*best));
        // The best of the other moves, where the review asked for it (#63).
        if (auto second = lineOf(pos, e.second_score, e.second_wdl, e.second_depth, e.second_pv);
            second && second->moves.front() != a.lines.front().moves.front())
            a.lines.push_back(std::move(*second));
        out[i] = std::move(a);
    }
    return out;
}

double moverExpected(const PositionEval& eval) {
    if (!eval.score || eval.score->type.value == fastchess::ScoreType::ERR) return 50.0;
    // The score and the figures are the side to move's: read as White's, they are the mover's.
    commentary::EngineLine l;
    const auto v = static_cast<int32_t>(eval.score->value);
    l.score =
        eval.score->type.value == fastchess::ScoreType::MATE ? commentary::Score::mate(v) : commentary::Score::cp(v);
    if (eval.wdl) {
        const auto pm = [](int64_t x) { return static_cast<uint16_t>(std::clamp<int64_t>(x, 0, 1000)); };
        l.wdl         = std::array<uint16_t, 3>{pm((*eval.wdl)[0]), pm((*eval.wdl)[1]), pm((*eval.wdl)[2])};
    }
    return l.whiteExpected();
}

std::vector<bool> onlyMoveCandidates(const ReplayGame& game, bool chess960) {
    std::vector<bool> out(game.moves.size());
    const auto line = mainLineOf(game, chess960);
    if (!line) return out;
    // The game's {book} moves are theory (commentaryReview()).
    std::size_t book = 0;
    while (book < game.moves.size() && game.moves[book].book) ++book;
    const auto& positions = line->positions();
    for (std::size_t i = book; i < game.moves.size(); ++i)
        out[i] = positions[i].legalMoves().size() >= 2 &&
                 !commentary::takesBackTheOnlyWay(positions[i], line->moveTo(i), line->moves()[i]);
    return out;
}

std::function<bool(int, const PositionEval&)> secondLines(const ReplayGame& game, bool chess960) {
    std::vector<std::string> played;
    for (const auto& m : game.moves) played.push_back(chess::uci::moveToUci(m.move, chess960));
    return [candidates = onlyMoveCandidates(game, chess960), played = std::move(played)](int ply,
                                                                                         const PositionEval& first) {
        if (ply < 0 || static_cast<std::size_t>(ply) >= candidates.size() || !candidates[static_cast<std::size_t>(ply)])
            return false;
        if (!first.score || first.best != played[static_cast<std::size_t>(ply)]) return false;
        const double mine = moverExpected(first);
        return mine >= commentary::ONLY_MOVE_GAP && mine <= SECOND_LINE_CEILING;
    };
}

std::vector<MoveReview> judgedMoves(const CommentaryReview& review) {
    std::vector<MoveReview> out;
    for (const auto& m : review.report.moves) {
        if (m.ply == 0) continue;
        if (out.size() < m.ply) out.resize(m.ply);
        auto& j = out[m.ply - 1];
        j.loss  = m.judgedLoss();  // the reading the judgment stands on, for the figure said with it
        j.best  = m.played_best;
        if (m.judgment && !m.book)
            j.judgement = *m.judgment == commentary::Judgment::Blunder   ? Judgement::Blunder
                          : *m.judgment == commentary::Judgment::Mistake ? Judgement::Mistake
                                                                         : Judgement::Inaccuracy;
    }
    return out;
}

std::vector<KeyMoment> reviewKeyMoments(const CommentaryReview& review, const ReplayGame& game,
                                        const GameReview& analysis, const std::vector<MoveReview>& judged,
                                        const GamePhases& phases, int tablebase) {
    std::vector<KeyMoment> out;
    // The other entries of the Engines space's list; its judged moves give way to the review's key moments.
    for (const auto& k : keyMoments(game, analysis, judged, phases, tablebase))
        if (k.kind != KeyMoment::Kind::TurningPoint && k.kind != KeyMoment::Kind::Judged) out.push_back(k);
    const auto white_pawns = [&](std::size_t p) {
        if (p >= analysis.positions.size()) return std::nan("");
        const auto stm = p < game.moves.size() ? game.moves[p].mover
                                               : (game.moves.empty() ? chess::Color::WHITE : ~game.moves.back().mover);
        return static_cast<double>(graphPawns(analysis.positions[p].score, stm));
    };
    const auto line_pawns = [](const commentary::EngineLine& l) {
        if (l.score.kind == commentary::Score::Kind::Mate)
            return static_cast<double>(l.score.value > 0 ? MATE_PAWNS : -MATE_PAWNS);
        return l.score.value / 100.0;
    };
    // The mistake or blunder that lost the most: the turning point.
    std::size_t worst = judged.size();
    for (std::size_t i = 0; i < judged.size(); ++i)
        if (judged[i].judgement >= Judgement::Mistake &&
            (worst == judged.size() || judged[i].loss > judged[worst].loss))
            worst = i;
    for (const auto ply : review.report.key_moments) {
        const auto* m = review.report.get(ply);
        if (!m || ply == 0) continue;
        const auto i = ply - 1;
        KeyMoment k;
        k.ply = static_cast<int>(ply);
        if (i < judged.size() && judged[i].judgement != Judgement::None) {
            k.kind      = i == worst ? KeyMoment::Kind::TurningPoint : KeyMoment::Kind::Judged;
            k.before    = white_pawns(i);
            k.after     = white_pawns(i + 1);
            k.judgement = judged[i].judgement;
        } else if (m->only_move && i < review.analyses.size() && review.analyses[i] && review.analyses[i]->best()) {
            const auto& a = *review.analyses[i];
            k.kind        = KeyMoment::Kind::OnlyMove;
            k.before      = line_pawns(*a.best());
            k.after       = a.second() ? line_pawns(*a.second()) : std::nan("");
            k.exact       = a.isExact();
        } else {
            continue;
        }
        out.push_back(k);
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& x, const auto& y) { return x.ply < y.ply; });
    return out;
}

std::shared_ptr<const CommentaryReview> commentaryReview(const ReplayGame& game, bool chess960,
                                                         const GameReview& review, const commentary::Opening& opening,
                                                         const commentary::Tablebases* tables) {
    auto line = mainLineOf(game, chess960);
    if (!line) return nullptr;
    auto analyses = analysesOf(*line, review, drawnByRule(game, chess960));
    if (tables) {
        const auto& positions = line->positions();
        for (std::size_t i = 0; i < analyses.size() && i < positions.size(); ++i)
            if (analyses[i]) commentary::settle(positions[i], *analyses[i], *tables);
    }
    // The moves a tournament's opening book played ({book}) are theory as well: not the players' choice, never judged,
    // as the prototype judges no move of theory.
    auto theory      = opening;
    std::size_t book = 0;
    while (book < game.moves.size() && game.moves[book].book) ++book;
    theory.book_plies = std::max(theory.book_plies, book);
    auto report       = commentary::review(*line, analyses, theory);
    return std::make_shared<const CommentaryReview>(
        CommentaryReview{std::move(*line), std::move(analyses), std::move(report)});
}

}  // namespace fcg
