// The Review tab's figures: a move's accuracy, each side's accuracy, loss, agreement, depth and time, the key moments,
// who saw the turning point first, and the PGN with the analysis written in.

#include <doctest/doctest.hpp>

#include <cmath>

#include "board_text.hpp"
#include "review_stats.hpp"

using namespace fcg;

namespace {

fastchess::Score cp(int64_t v) { return {fastchess::ScoreType::CP, v}; }

ReplayGame gameOf(const char* moves) {
    std::string error;
    auto text = readBoardText(moves);
    REQUIRE(text.kind == BoardText::Kind::Games);
    auto game = readPgnGame(text.pgn, error);
    REQUIRE(game);
    return *game;
}

}  // namespace

TEST_CASE("review: a move's accuracy from the winning chances it lost, as lichess counts it") {
    CHECK(moveAccuracy(60, 60) == doctest::Approx(100.0).epsilon(0.001));
    CHECK(moveAccuracy(60, 70) == doctest::Approx(100.0).epsilon(0.001));  // better than thought: nothing lost
    CHECK(moveAccuracy(60, 50) == doctest::Approx(103.1668 * std::exp(-0.4354) - 3.1669));
    CHECK(moveAccuracy(90, 0) == 0.0);
}

TEST_CASE("review: each side's figures, and the key moments of a game with a mistake") {
    // 1. e4 e5 2. Nf3 Nc6 3. Bc4 Nd4?: 7 positions, White's chances jumping after Black's 3rd move.
    auto game           = gameOf("e4 e5 Nf3 Nc6 Bc4 Nd4");
    game.moves[0].depth = 20, game.moves[0].time_ms = 1000;
    game.moves[2].depth = 22, game.moves[2].time_ms = 500;
    game.moves[1].depth = 18, game.moves[1].time_ms = 2000;
    GameReview review;
    review.engine = "Judge";
    // Scores of the side to move, and the engine's best moves (the game's, but 3...Nf6 instead of 3...Nd4).
    const std::pair<int, const char*> pos[] = {{30, "e2e4"}, {-30, "e7e5"}, {35, "g1f3"}, {-32, "b8c6"},
                                               {30, "f1c4"}, {-30, "g8f6"}, {400, "f3e5"}};
    for (const auto& [v, best] : pos) review.positions.push_back({cp(v), best, 15, 100, best, std::nullopt});
    const auto judged = reviewMoves(game, false, review);
    REQUIRE(judged.size() == 6);
    CHECK(judged[5].judgement == Judgement::Mistake);

    const auto phases = gamePhases(game, false);
    const auto stats  = gameStats(game, false, review, judged, phases);
    CHECK(stats[0].moves == 3);
    CHECK(stats[1].moves == 3);
    REQUIRE(stats[0].accuracy);
    REQUIRE(stats[1].accuracy);
    CHECK(*stats[0].accuracy > 95.0);
    CHECK(*stats[1].accuracy < *stats[0].accuracy);
    CHECK(stats[1].judgements == std::array<int, 3>{0, 1, 0});
    CHECK(stats[0].agreement == doctest::Approx(1.0));
    CHECK(stats[1].agreement == doctest::Approx(2.0 / 3.0));
    CHECK(stats[0].depth == doctest::Approx(21.0));
    CHECK(stats[0].time_ms == 1500);
    // Black's average loss: 5, 0 (it gained 2) and 370 centipawns over 3 moves; White's 0, 3, 0.
    REQUIRE(stats[1].average_loss);
    CHECK(*stats[1].average_loss == doctest::Approx(375.0 / 3.0));
    CHECK(*stats[0].average_loss == doctest::Approx(1.0));
    CHECK(stats[0].phase_accuracy[0].has_value());
    CHECK_FALSE(stats[0].phase_accuracy[2].has_value());

    const auto moments = keyMoments(game, review, judged, phases, -1);
    REQUIRE_FALSE(moments.empty());
    CHECK(moments.front().kind == KeyMoment::Kind::TurningPoint);
    CHECK(moments.front().ply == 6);
    CHECK(moments.front().before == doctest::Approx(0.30));
    CHECK(moments.front().after == doctest::Approx(4.00));
    // In the game's order, the tablebase zone among them.
    const auto with_tb = keyMoments(game, review, judged, phases, 4);
    REQUIRE(with_tb.size() == moments.size() + 1);
    CHECK(with_tb.front().kind == KeyMoment::Kind::Tablebase);
    CHECK(with_tb.front().ply == 4);

    // The PGN with the analysis: the mistake's NAG, the evaluations, the line preferred.
    const auto pgn = pgnWithAnalysis(GameTree{game, {}}, false, review, judged);
    INFO(pgn);
    CHECK(pgn.find("Nd4 $2 {[%eval 4.00] Judge: 3...Nf6}") != std::string::npos);
    CHECK(pgn.find("e4 {[%eval 0.30]}") != std::string::npos);
}

TEST_CASE("review: who saw the turning point coming first") {
    GraphCurve early, late, never;
    early.x = {2, 4, 6, 8};
    early.y = {0.2f, 1.1f, 2.0f, 2.5f};
    late.x  = {3, 5, 7, 9};
    late.y  = {0.1f, 0.3f, 0.9f, 1.4f};
    never.x = {2, 4};
    never.y = {0.1f, -0.2f};
    CHECK(sawItAt(early, 8, 1) == 4);
    CHECK(sawItAt(late, 8, 1) == 9);
    CHECK(sawItAt(never, 8, 1) == -1);
    CHECK(sawItAt(early, 30, 1) == -1);  // only from 12 plies before
    CHECK(sawItAt(early, 8, -1) == -1);
}

TEST_CASE("review: evaluations someone wrote into the game are one analysis, not the players' own") {
    // The same evaluations, as Lichess or a broadcast writes them ([%eval] alone), as fastchess writes an engine
    // game's, and as ChessBase does ([%eval] with [%emt]): a blunder answered by a blunder, two big jumps in a row.
    const auto annotated =
        gameOf("1. e4 {[%eval 0.3]} e5 {[%eval 0.3]} 2. Nf3 {[%eval 5.0]} Nc6 {[%eval 0.2]} 3. Bc4 {[%eval 0.3]} *");
    const auto engines = gameOf(
        "1. e4 {+0.30/20 1.0s} e5 {-0.30/20 1.0s} 2. Nf3 {+5.00/20 1.0s} Nc6 {-0.20/20 1.0s} "
        "3. Bc4 {+0.30/20 1.0s} *");
    const auto chessbase = gameOf(
        "1. e4 {[%eval 0.3,20] [%emt 0:00:01]} e5 {[%eval 0.3,20] [%emt 0:00:01]} 2. Nf3 {[%eval 5.0,20] "
        "[%emt 0:00:01]} Nc6 {[%eval 0.2,20] [%emt 0:00:01]} 3. Bc4 {[%eval 0.3,20] [%emt 0:00:01]} *");
    CHECK(evaluationsAnnotated(annotated));
    CHECK_FALSE(evaluationsAnnotated(engines));
    CHECK_FALSE(evaluationsAnnotated(chessbase));
    CHECK_FALSE(evaluationsAnnotated(gameOf("e4 e5 Nf3")));  // no evaluation at all

    // The players' engines disagree; one analyst's numbers only follow the moves.
    const auto disagreement = [](const ReplayGame& game) {
        const auto moments = keyMoments(game, GameReview{}, {}, gamePhases(game, false), -1);
        return std::count_if(moments.begin(), moments.end(),
                             [](const KeyMoment& m) { return m.kind == KeyMoment::Kind::Disagreement; });
    };
    CHECK(disagreement(engines) == 1);
    CHECK(disagreement(chessbase) == 1);
    CHECK(disagreement(annotated) == 0);

    // One curve on the graph, its own, and no players apart.
    const auto graph = buildGameGraph(annotated, false, GraphChart::Eval, {}, {});
    CHECK(graph.players[0].x.size() == 5);
    CHECK(graph.players[0].name == "The game's evaluations");
    CHECK(graph.players[1].x.empty());
    CHECK_FALSE(graph.available[static_cast<std::size_t>(GraphChart::Agreement)]);
    const auto two = buildGameGraph(engines, false, GraphChart::Eval, {}, {});
    CHECK(two.players[0].x.size() == 3);
    CHECK(two.players[1].x.size() == 2);
    CHECK(two.players[0].name == "White");
    CHECK(two.available[static_cast<std::size_t>(GraphChart::Agreement)]);
}
