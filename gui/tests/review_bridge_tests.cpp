// The window's analysis of a game read as the commentary reads one (review_bridge.hpp).

#include <doctest/doctest.hpp>

#include <string>

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
