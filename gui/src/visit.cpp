#include "visit.hpp"
#include "visit_cards.hpp"

#include <algorithm>

#include <fmt/include/fmt/format.h>

#include <study/game_tree.hpp>
#include <study/store.hpp>

#include "board_text.hpp"

namespace fcg {

VisitShows visitShows(VisitPhase phase) {
    VisitShows v;
    v.arrows   = phase == VisitPhase::Shown || phase == VisitPhase::Checking;  // Checking: the answer alone
    v.notation = phase == VisitPhase::Shown || phase == VisitPhase::End;
    return v;
}

std::vector<std::size_t> visitMoments(const commentary::GameReport& report) {
    std::vector<std::size_t> out = report.key_moments;
    if (out.empty())
        for (const auto& m : report.moves)
            if (m.judgment && *m.judgment != commentary::Judgment::Inaccuracy) out.push_back(m.ply);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool visitReadsMoment(const ReplayGame& game, std::size_t ply, uint64_t made_for, uint64_t shown) {
    return made_for == shown && ply >= 1 && ply <= game.moves.size();
}

VisitVerdict judgeVisitAnswer(bool engine_move, double best_pawns, double answer_pawns, bool dont_know) {
    VisitVerdict v;
    if (dont_know) return v;
    v.points = guessPoints(engine_move, best_pawns, answer_pawns);
    v.wrong  = v.points == 0;
    return v;
}

std::optional<MistakeExercise> visitExercise(const ReplayGame& game, const GameReview& review, std::size_t ply) {
    if (ply == 0 || ply > game.moves.size() || ply >= review.positions.size()) return std::nullopt;
    const auto& before = review.positions[ply - 1];
    const auto& after  = review.positions[ply];
    const auto start   = game.start_fen.empty() ? std::string(chess::constants::STARTPOS) : game.start_fen;
    chess::Board board(start);
    std::vector<std::string> moves;
    for (std::size_t i = 0; i + 1 < ply; ++i) {
        moves.push_back(chess::uci::moveToUci(game.moves[i].move));
        board.makeMove(game.moves[i].move);
    }
    const auto best = chess::uci::uciToMove(board, before.best);
    chess::Movelist legal;
    chess::movegen::legalmoves(legal, board);
    if (before.best.empty() || std::find(legal.begin(), legal.end(), best) == legal.end()) return std::nullopt;
    MistakeExercise e;
    e.key          = board.hash();
    e.start_fen    = start;
    e.moves        = std::move(moves);
    e.game_key     = reviewKey(game);
    e.white        = game.header("White");
    e.black        = game.header("Black");
    e.event        = game.header("Event");
    e.site         = game.header("Site");
    e.date         = game.header("Date");
    e.time_control = game.header("TimeControl");
    e.packed_date  = study::packDate(e.date);
    e.played       = chess::uci::moveToUci(game.moves[ply - 1].move);
    e.judgement    = Judgement::Mistake;
    e.visit        = true;
    e.engine       = review.engine;
    e.nodes        = review.nodes;
    e.movetime_ms  = review.nodes > 0 ? 0 : review.movetime_ms;
    e.best         = before.best;
    e.line         = before.pv.empty() ? before.best : before.pv;
    e.score        = before.score;
    e.wdl          = before.wdl;
    e.reply        = after.pv.empty() ? after.best : after.pv;
    e.after        = after.score;
    e.after_wdl    = after.wdl;
    return e;
}

void addVisitCard(MistakeRecord& record, MistakeExercise exercise, int64_t now,
                  const study::fsrs::Scheduler& scheduler) {
    auto it = record.exercises.find(exercise.key);
    if (it == record.exercises.end()) {
        (void)record.giveBack(exercise);  // a card kept of an exercise gone, given back
        it = record.exercises.emplace(exercise.key, std::move(exercise)).first;
    }
    auto& kept = it->second;
    if (kept.since == 0) kept.since = now;
    const uint64_t seed = kept.key ^ (static_cast<uint64_t>(kept.card.reps) * 0x9E3779B97F4A7C15ull);
    kept.card           = scheduler.review(kept.card, study::fsrs::Rating::Again, now, seed);
}

std::string whyNotQuestion(const chess::Board& before, const std::string& asked, const std::string& reference,
                           bool french) {
    const auto text = [&](const std::string& uci) {
        const auto m = chess::uci::uciToMove(before, uci);
        if (m == chess::Move::NO_MOVE) return uci;
        const auto san = chess::uci::moveToSan(before, m);
        return fmt::format("{}{}{}", before.fullMoveNumber(), before.sideToMove() == chess::Color::WHITE ? "." : "…",
                           french ? frenchSan(san) : san);
    };
    return french ? fmt::format("Pourquoi pas {} plutôt que {} ?", text(asked), text(reference))
                  : fmt::format("Why not {} rather than {}?", text(asked), text(reference));
}

}  // namespace fcg
