// The window's analysis of a game read as the commentary reads one (review_bridge.hpp).

#include <doctest/doctest.hpp>

#include <algorithm>
#include <string>

#include <commentary/story.hpp>
#include <commentary/writing.hpp>

#include "game_graph.hpp"
#include "pgn_store.hpp"
#include "review_bridge.hpp"

using namespace fcg;

namespace {

PositionEval eval(int64_t cp, std::string pv, bool mate = false) {
    PositionEval e;
    e.score = fastchess::Score{mate ? fastchess::ScoreType::MATE : fastchess::ScoreType::CP, cp};
    e.pv    = std::move(pv);
    e.best  = e.pv.substr(0, e.pv.find(' '));
    e.depth = 20;
    return e;
}

}  // namespace

TEST_CASE("review bridge: scores turn to White's, lines are read, the mate ends the analyses") {
    const auto game = parseReplay("1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf7# 1-0\n");
    REQUIRE(game);
    const auto line = mainLineOf(*game, false);
    REQUIRE(line);
    CHECK(line->moves().size() == 7);
    GameReview review;
    review.positions    = {eval(30, "e2e4 e7e5"), eval(-30, "e7e5 g1f3"), eval(30, "g1f3"),      eval(-20, "b8c6 d1h5"),
                           eval(20, "d1h5 g8f6"), eval(-30, "g7g6 h5f3"), eval(1, "h5f7", true), eval(0, "")};
    const auto analyses = analysesOf(*line, review);
    REQUIRE(analyses.size() == 8);
    // Black to move after 1.e4: -30 for Black is +30 for White.
    REQUIRE(analyses[1]);
    CHECK(analyses[1]->best()->score == commentary::Score::cp(30));
    CHECK(analyses[1]->best()->moves.size() == 2);
    // White mates in one: the mate is White's.
    REQUIRE(analyses[6]);
    CHECK(analyses[6]->best()->score == commentary::Score::mate(1));
    // After 4.Qxf7# the game is over.
    REQUIRE(analyses[7]);
    REQUIRE(analyses[7]->end);
    CHECK(analyses[7]->end->winner == commentary::Color::White);

    // 3…Nf6 lets the mate in: the review calls it a blunder.
    const auto r = commentaryReview(*game, false, review);
    REQUIRE(r);
    const auto* nf6 = r->report.get(6);
    REQUIRE(nf6);
    REQUIRE(nf6->judgment);
    CHECK(*nf6->judgment == commentary::Judgment::Blunder);
    // The same verdict for the Chess space's symbols.
    const auto judged = judgedMoves(*r);
    REQUIRE(judged.size() >= 6);
    CHECK(judged[5].judgement == Judgement::Blunder);
    CHECK(judged[5].loss > 40.0);
    CHECK(judged[0].judgement == Judgement::None);
}

TEST_CASE("review bridge: a Chess960 game, a position not searched") {
    const auto game = parseReplay("1. e4 e5 *\n");
    REQUIRE(game);
    CHECK_FALSE(mainLineOf(*game, true));
    CHECK_FALSE(commentaryReview(*game, true, GameReview{}));
    const auto line = mainLineOf(*game, false);
    REQUIRE(line);
    GameReview partial;
    partial.positions   = {eval(30, "e2e4")};
    const auto analyses = analysesOf(*line, partial);
    CHECK(analyses[0]);
    CHECK_FALSE(analyses[1]);
    CHECK_FALSE(analyses[2]);
}

namespace {

// A game of our own (Gaia 4.4.3 - Gaia 4.4.2, 2026), drawn by a threefold repetition after its book's twelve plies.
constexpr const char* DRAW =
    "[Result \"1/2-1/2\"]\n\n1. d4 {book} Nf6 {book} 2. c4 {book} e6 {book} 3. Nc3 {book} Bb4 {book} 4. Qc2 {book} Nc6 "
    "{book} 5. e3 {book} "
    "O-O {book} 6. Bd3 {book} e5 {book} 7. Ne2 Re8 8. O-O Bxc3 9. Qxc3 d5 10. dxe5 Nxe5 11. cxd5 Nxd5 12. Qc2 Nxd3 "
    "13. Qxd3 Nb4 14. Qc3 a5 15. b3 Nd5 16. Qd2 Ra6 17. e4 Rxe4 18. Nc3 Re5 19. Bb2 Rh5 20. Rae1 Re6 21. h3 h6 "
    "22. Rxe6 Bxe6 23. Ne4 Qe7 24. f4 Qb4 25. Qd3 Bf5 26. Qd1 Bxe4 27. Qxh5 Qd2 28. Rf2 Qe1+ 29. Rf1 Qd2 30. Rf2 "
    "Qe1+ 31. Rf1 Qd2 1/2-1/2\n";

// Its analysis as the runner leaves it: each move the engine's choice at a level score, but 6.Bd3, which the engine
// thinks loses (6.Bd2 instead); the last position, a draw by repetition, has the end's score and no line.
GameReview drawAnalysis(const ReplayGame& game) {
    GameReview review;
    review.engine = "Engine";
    for (std::size_t i = 0; i < game.moves.size(); ++i)
        review.positions.push_back(eval(0, chess::uci::moveToUci(game.moves[i].move)));
    review.positions[10] = eval(30, "c1d2");
    review.positions[11] = eval(150, "e6e5");  // Black's: +1.50 for Black after 6.Bd3
    PositionEval end;
    end.score = fastchess::Score{fastchess::ScoreType::CP, 0};
    review.positions.push_back(end);
    return review;
}

}  // namespace

TEST_CASE("review bridge: a game drawn by repetition ends the review, which is complete") {
    const auto game = parseReplay(DRAW);
    REQUIRE(game);
    REQUIRE(game->moves.size() == 62);
    REQUIRE(game->result == "1/2-1/2");
    const auto drawn = drawnByRule(*game, false);
    REQUIRE(drawn.size() == 63);
    CHECK(drawn.back());
    CHECK(std::count(drawn.begin(), drawn.end(), true) == 1);  // twice is not yet a draw
    const auto line = mainLineOf(*game, false);
    REQUIRE(line);
    const auto review = drawAnalysis(*game);
    // The position alone does not tell: without the game's history, no analysis there, and the review not complete.
    CHECK_FALSE(analysesOf(*line, review)[62]);
    const auto analyses = analysesOf(*line, review, drawn);
    REQUIRE(analyses[62]);
    REQUIRE(analyses[62]->end);
    CHECK_FALSE(analyses[62]->end->winner);

    const auto r = commentaryReview(*game, false, review);
    REQUIRE(r);
    CHECK(r->report.complete);
    // So the story is told: the opening, how it ended (#62's measure told none).
    const auto story = commentary::story(r->line, game->result, r->report, r->analyses, nullptr, commentary::Lang::En,
                                         commentary::PieceLetters::English);
    REQUIRE_FALSE(story.empty());
    CHECK(story.back().text.find("draw") != std::string::npos);
}

TEST_CASE("review bridge: the game's book moves are theory, never judged, nor key moments, nor commented") {
    const auto game = parseReplay(DRAW);
    REQUIRE(game);
    const auto review = drawAnalysis(*game);
    const auto r      = commentaryReview(*game, false, review);
    REQUIRE(r);
    const auto* bd3 = r->report.get(11);  // 6.Bd3, a {book} move the engine thinks loses
    REQUIRE(bd3);
    CHECK(bd3->book);
    CHECK_FALSE(bd3->judgment);
    CHECK(std::find(r->report.key_moments.begin(), r->report.key_moments.end(), 11) == r->report.key_moments.end());
    const auto judged = judgedMoves(*r);
    REQUIRE(judged.size() > 10);
    CHECK(judged[10].judgement == Judgement::None);
    for (const auto& [ply, text] :
         commentary::templateComments(r->line, r->report, r->analyses, {}, nullptr, nullptr, commentary::Lang::En,
                                      commentary::PieceLetters::English))
        CHECK(ply != 11);
    // The same move out of the book: a mistake at least.
    auto played = *game;
    for (auto& m : played.moves) m.book = false;
    const auto judged_out = judgedMoves(*commentaryReview(played, false, review));
    CHECK(judged_out[10].judgement >= Judgement::Mistake);
}

TEST_CASE("review bridge: the second line, where the review asked for one, makes an only move") {
    const auto game = parseReplay("1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 *\n");
    REQUIRE(game);
    const auto line = mainLineOf(*game, false);
    REQUIRE(line);
    GameReview review;
    review.engine = "Engine";
    for (std::size_t i = 0; i < game->moves.size(); ++i)
        review.positions.push_back(eval(20, chess::uci::moveToUci(game->moves[i].move)));
    review.positions.push_back(eval(20, "b5a4"));
    // 2...Nc6, the engine's choice, the next best far behind; the same move as a second line is no second line.
    review.positions[3].second_score = fastchess::Score{fastchess::ScoreType::CP, -300};
    review.positions[3].second_pv    = "d7d6 f1c4";
    review.positions[5].second_score = fastchess::Score{fastchess::ScoreType::CP, -300};
    review.positions[5].second_pv    = "f1b5 a7a6";
    const auto analyses              = analysesOf(*line, review);
    REQUIRE(analyses[3]);
    REQUIRE(analyses[3]->lines.size() == 2);
    CHECK(analyses[3]->second()->score == commentary::Score::cp(300));  // White's
    CHECK(analyses[3]->second()->moves.size() == 2);
    REQUIRE(analyses[5]);
    CHECK(analyses[5]->lines.size() == 1);
    const auto r = commentaryReview(*game, false, review);
    REQUIRE(r);
    REQUIRE(r->report.get(4));
    CHECK(r->report.get(4)->only_move);
    CHECK_FALSE(r->report.get(6)->only_move);
    // Told in the Review tab's key moments, with the next best (mockup M5).
    const auto judged  = judgedMoves(*r);
    const auto moments = reviewKeyMoments(*r, *game, review, judged, gamePhases(*game, false), -1);
    const auto only    = std::find_if(moments.begin(), moments.end(),
                                      [](const KeyMoment& k) { return k.kind == KeyMoment::Kind::OnlyMove; });
    REQUIRE(only != moments.end());
    CHECK(only->ply == 4);
    CHECK(only->before == doctest::Approx(-0.2));  // White's pawns: Black's +0.20
    CHECK(only->after == doctest::Approx(3.0));
    CHECK_FALSE(only->exact);
}

TEST_CASE("review bridge: the second line is asked for where the move played can be an only move") {
    // 1.e4 e5 2.Nf3 Nc6 3.Bb5 a6 4.Bxc6 dxc6: a recapture, the only way.
    const auto game = parseReplay("1. e4 {book} e5 2. Nf3 Nc6 3. Bb5 a6 4. Bxc6 dxc6 *\n");
    REQUIRE(game);
    const auto candidates = onlyMoveCandidates(*game, false);
    REQUIRE(candidates.size() == 8);
    CHECK_FALSE(candidates[0]);  // a book move
    CHECK(candidates[1]);
    CHECK(candidates[6]);  // 4.Bxc6, a capture of its own
    CHECK(candidates[7]);  // 4...dxc6: bxc6 takes back too
    const auto wanted = secondLines(*game, false);
    CHECK(wanted(1, eval(20, "e7e5")));
    CHECK_FALSE(wanted(1, eval(20, "c7c5")));    // not the engine's choice
    CHECK_FALSE(wanted(1, eval(-400, "e7e5")));  // lost: no other move can be that far behind
    CHECK(wanted(1, eval(150, "e7e5")));         // better, not yet won
    CHECK_FALSE(wanted(1, eval(400, "e7e5")));   // won whatever it plays: one-sided
    CHECK_FALSE(wanted(0, eval(20, "e2e4")));    // theory
    CHECK_FALSE(wanted(8, eval(20, "e1g1")));    // after the last move
    const auto alone = parseReplay("1. e4 d5 2. exd5 Qxd5 3. Nc3 Qd8 4. Bc4 Nf6 5. Bxf7+ Kxf7 *\n");
    REQUIRE(alone);
    CHECK_FALSE(onlyMoveCandidates(*alone, false)[9]);  // 5...Kxf7: the only way to take back
}
