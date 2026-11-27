// The guided visit and "Why not?" (#94, mockup M13, board 7, point 16): the moments in the game's order, nothing shown
// before the answer, the answer judged as Guess the moves judges a guess, a wrong one a card of the day's queue that
// the scans of the user's games keep; nothing read of the Board once a training session took it (#137); "Why not?"
// asked as the Questions' Compare, its two lines the engine's after each move.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

#include <commentary/position.hpp>
#include <commentary/question.hpp>

#include <study/game_tree.hpp>

#include "endgames.hpp"
#include "mistakes.hpp"
#include "pgn_store.hpp"
#include "review_bridge.hpp"
#include "training.hpp"
#include "visit.hpp"
#include "visit_cards.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

PositionEval eval(int64_t cp, std::string pv) {
    PositionEval e;
    e.score = fastchess::Score{fastchess::ScoreType::CP, cp};
    e.pv    = std::move(pv);
    e.best  = e.pv.substr(0, e.pv.find(' '));
    e.depth = 20;
    return e;
}

// 1.e4 e5 2.Nf3 Nc6 3.Bc4 Nd4 4.Nxe5? Qg5 5.Nxf7?? Qxg2: two mistakes of White's, reviewed.
constexpr const char* GAME = "1. e4 e5 2. Nf3 Nc6 3. Bc4 Nd4 4. Nxe5 Qg5 5. Nxf7 Qxg2 *";

GameReview reviewOf(const ReplayGame& game) {
    GameReview r;
    r.engine = "Engine";
    r.passes = 3;
    r.nodes  = 320000;
    const std::vector<std::pair<int64_t, std::string>> evals{
        {30, "e2e4"}, {-30, "e7e5"}, {30, "g1f3"},   {-30, "b8c6"}, {30, "f1c4"},  {-20, "g8f6"},
        {40, "f3d4"}, {150, "d8g5"}, {-160, "e5g4"}, {400, "g5g2"}, {-700, "h1f1"}};
    for (const auto& [cp, pv] : evals) r.positions.push_back(eval(cp, pv));
    (void)game;
    return r;
}

class Scripted final : public commentary::Model {
   public:
    std::optional<commentary::Generation> generate(const commentary::Request&, std::string* error) override {
        if (error) *error = "silent";
        return std::nullopt;
    }
};

// An engine that plays the first move it may, then the first reply, and calls it level.
class FirstMoves final : public commentary::Oracle {
   public:
    std::optional<commentary::Searched> search(const commentary::Position& pos,
                                               std::span<const commentary::Move> only) const override {
        const auto legal = pos.legalMoves();
        if (legal.empty()) return std::nullopt;
        commentary::Searched s;
        s.moves.push_back(only.empty() ? legal.front() : only.front());
        auto after = pos;
        after.playUnchecked(s.moves.front());
        if (const auto replies = after.legalMoves(); !replies.empty()) s.moves.push_back(replies.front());
        s.score = commentary::Score::cp(0);
        return s;
    }
};

}  // namespace

TEST_CASE("guided visit: the key moments in the game's order, the marked moves when there are none") {
    commentary::GameReport report;
    report.key_moments = {9, 7, 9};
    CHECK(visitMoments(report) == std::vector<std::size_t>{7, 9});
    commentary::GameReport none;
    commentary::MoveReport m;
    m.ply      = 3;
    m.judgment = commentary::Judgment::Mistake;
    none.moves.push_back(m);
    m.ply      = 5;
    m.judgment = commentary::Judgment::Inaccuracy;  // not a moment of the visit
    none.moves.push_back(m);
    m.ply      = 8;
    m.judgment = commentary::Judgment::Blunder;
    none.moves.push_back(m);
    CHECK(visitMoments(none) == std::vector<std::size_t>{3, 8});
    // From a real review: White's two mistakes, 4.Nxe5 and 5.Nxf7.
    const auto game = parseReplay(GAME);
    REQUIRE(game);
    const auto review = commentaryReview(*game, false, reviewOf(*game));
    REQUIRE(review);
    const auto moments = visitMoments(review->report);
    REQUIRE(!moments.empty());
    CHECK(std::is_sorted(moments.begin(), moments.end()));
    CHECK(std::find(moments.begin(), moments.end(), 9) != moments.end());  // 5.Nxf7??
}

TEST_CASE("guided visit: nothing shown before the answer; the answer judged as Guess the moves judges a guess") {
    CHECK_FALSE(visitShows(VisitPhase::Asking).arrows);
    CHECK_FALSE(visitShows(VisitPhase::Asking).evaluation);
    CHECK_FALSE(visitShows(VisitPhase::Asking).notation);
    CHECK_FALSE(visitShows(VisitPhase::Checking).notation);  // the answer's arrow alone
    CHECK(visitShows(VisitPhase::Shown).arrows);
    CHECK(visitShows(VisitPhase::Shown).notation);
    for (const auto phase : {VisitPhase::Asking, VisitPhase::Checking, VisitPhase::Shown, VisitPhase::End})
        CHECK_FALSE(visitShows(phase).evaluation);
    // The engine's move: 3 points; as good (0.2 pawn worse at most) 2; playable (0.5) 1; else 0, wrong.
    CHECK(judgeVisitAnswer(true, 0, 0, false).points == 3);
    CHECK_FALSE(judgeVisitAnswer(true, 0, 0, false).wrong);
    CHECK(judgeVisitAnswer(false, 1.0, 0.85, false).points == 2);
    CHECK(judgeVisitAnswer(false, 1.0, 0.6, false).points == 1);
    CHECK_FALSE(judgeVisitAnswer(false, 1.0, 0.6, false).wrong);
    CHECK(judgeVisitAnswer(false, 1.0, 0.2, false).points == 0);
    CHECK(judgeVisitAnswer(false, 1.0, 0.2, false).wrong);
    CHECK(judgeVisitAnswer(false, 1.0, 1.0, true).wrong);  // "I don't know"
    CHECK(judgeVisitAnswer(false, 1.0, 1.0, true).points == 0);
}

TEST_CASE("guided visit: a wrong answer, a card of the day's queue that the scans keep") {
    const auto game = parseReplay(GAME);
    REQUIRE(game);
    const auto review = reviewOf(*game);
    // 5.Nxf7??: the position before it, the engine's move to find (5.Ng4, its best), White to move.
    const auto exercise = visitExercise(*game, review, 9);
    REQUIRE(exercise);
    CHECK(exercise->visit);
    CHECK(exercise->played == "e5f7");
    CHECK(exercise->best == "e5g4");  // the review's best there
    CHECK(exercise->board().hash() == boardAt(*game, false, 8).hash());
    CHECK(exercise->side() == chess::Color::WHITE);
    CHECK(exercise->engine == "Engine");
    CHECK(exercise->nodes == 320000);
    CHECK_FALSE(visitExercise(*game, review, 0));
    CHECK_FALSE(visitExercise(*game, review, 99));

    MistakeRecord record;
    const int64_t now    = 1'800'000'000;
    const auto scheduler = trainingScheduler();
    addVisitCard(record, *exercise, now, scheduler);
    REQUIRE(record.exercises.size() == 1);
    const auto& kept = record.exercises.begin()->second;
    CHECK(kept.since == now);
    CHECK(kept.card.reps == 1);
    CHECK(kept.card.due > now);
    CHECK(kept.card.due - now < study::fsrs::DAY);  // back within the day
    // Answered wrong again: the same card graded again.
    addVisitCard(record, *exercise, now + 600, scheduler);
    CHECK(record.exercises.size() == 1);
    CHECK(record.exercises.begin()->second.card.reps == 2);
    // Written and read back; a scan of the user's games, which finds no game of it, keeps it.
    const auto dir = fs::temp_directory_path() / "visit-cards";
    fs::remove_all(dir);
    fs::create_directories(dir);
    REQUIRE(writeMistakeRecord(dir, record));
    const auto back = readMistakeRecord(dir);
    REQUIRE(back.exercises.size() == 1);
    CHECK(back.exercises.begin()->second.visit);
    const auto scan = scanMistakes({}, {"Tester"}, dir / "analyses", back, 0, commentary::Lang::En);
    REQUIRE(scan.record.exercises.size() == 1);
    CHECK(scan.record.exercises.begin()->second.visit);
    CHECK(scan.record.kept.empty());
    fs::remove_all(dir);
}

TEST_CASE("guided visit: a training session that takes the Board leaves the visit nothing of it to read (#137)") {
    // A visit under way on the Board's game, the serial of the Board's game 7 when it started: each moment's move is
    // the game's (5.Nxf7?? at ply 9).
    const auto game = parseReplay(GAME);
    REQUIRE(game);
    const auto review = commentaryReview(*game, false, reviewOf(*game));
    REQUIRE(review);
    const auto moments = visitMoments(review->report);
    REQUIRE(!moments.empty());
    const uint64_t made_for = 7;
    for (const auto ply : moments) CHECK(visitReadsMoment(*game, ply, made_for, made_for));
    REQUIRE(std::find(moments.begin(), moments.end(), 9) != moments.end());
    CHECK(game->moves[8].san == "Nxf7");

    // An endgame of the catalogue takes the Board, as every session takes it (context::SessionBoard::takeBoard()): an
    // empty game from the endgame's position on the Board, the serial of the Board's game one further. Every moment of
    // the visit lies past its moves, which the visit's column read before (#137): nothing is read now.
    const auto* queen = endgameEntry("queen");
    REQUIRE(queen);
    ReplayGame session;
    session.start_fen = queen->fen;
    for (const auto ply : moments) CHECK_FALSE(visitReadsMoment(session, ply, made_for, made_for + 1));
    // The endgame played on: its moves are the session's, however many there are.
    session.moves.resize(moments.back() + 2);
    for (const auto ply : moments) CHECK_FALSE(visitReadsMoment(session, ply, made_for, made_for + 1));

    // Both tell: another game on the Board, even of the same moves; the Board's game cut short before a moment; the
    // start, which no move leads to.
    CHECK_FALSE(visitReadsMoment(*game, moments.front(), made_for, made_for + 1));
    auto cut = *game;
    cut.moves.resize(moments.back() - 1);
    CHECK_FALSE(visitReadsMoment(cut, moments.back(), made_for, made_for));
    CHECK(visitReadsMoment(cut, moments.back() - 1, made_for, made_for));
    CHECK_FALSE(visitReadsMoment(*game, 0, made_for, made_for));
}

TEST_CASE("why not: the question the Questions read as Compare, its figures the engine's after each move") {
    // After 1.e4 e5 2.Nf3 Nc6 3.Bc4 Nf6 4.Ng5 d5 5.exd5: why not 5...Nxd5 rather than 5...Na5?
    const auto game = parseReplay("1. e4 e5 2. Nf3 Nc6 3. Bc4 Nf6 4. Ng5 d5 5. exd5 *");
    REQUIRE(game);
    const auto before = boardAt(*game, false, 9);
    const auto en     = whyNotQuestion(before, "f6d5", "c6a5", false);
    CHECK(en == "Why not 5…Nxd5 rather than 5…Na5?");
    const auto fr = whyNotQuestion(before, "f6d5", "c6a5", true);
    CHECK(fr == "Pourquoi pas 5…Cxd5 plutôt que 5…Ca5 ?");
    const auto pos = commentary::Position::fromFen(before.getFen());
    REQUIRE(pos);
    const auto nxd5 = *pos->parseUci("f6d5");
    const auto na5  = *pos->parseUci("c6a5");
    CHECK(commentary::namedAction(en, commentary::moveNames(*pos, commentary::Lang::En)) ==
          commentary::Action{commentary::Action::Kind::Compare, nxd5, na5});
    CHECK(commentary::namedAction(fr, commentary::moveNames(*pos, commentary::Lang::Fr)) ==
          commentary::Action{commentary::Action::Kind::Compare, nxd5, na5});
    // Answered as Compare: the engine's line after each move, in their order, for the table under the answer.
    commentary::Asked asked;
    asked.question = en;
    asked.before   = &*pos;
    asked.lang     = commentary::Lang::En;
    asked.letters  = commentary::PieceLetters::English;
    Scripted silent;
    const FirstMoves engine;
    const auto answered = commentary::answer(asked, silent, &engine, nullptr);
    REQUIRE(std::holds_alternative<commentary::Answered>(answered));
    const auto& a = std::get<commentary::Answered>(answered);
    CHECK(a.kind == commentary::Action::Kind::Compare);
    CHECK(a.moves == std::vector<commentary::Move>{nxd5, na5});
    REQUIRE(a.lines.size() == 2);
    CHECK_FALSE(a.lines[0].moves.empty());
    CHECK_FALSE(a.lines[1].moves.empty());
    CHECK_FALSE(a.earlier);  // both moves of the position asked about
}
