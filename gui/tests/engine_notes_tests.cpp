// A tournament's game on the study Board (#87, mockup M12, board 8): the text beside each move, a move's fields in
// full, the line it expected, and the curve of the game as its two engines saw it, with its moments.

#include <doctest/doctest.hpp>

#include <cmath>

#include <study/pgn_reader.hpp>

#include "board_text.hpp"
#include "engine_notes.hpp"

using namespace fcg;

namespace {

GameTree treeOf(const char* pgn) {
    auto read = study::readPgnGame(pgn);
    REQUIRE(read.tree);
    REQUIRE(read.error.empty());
    return *read.tree;
}

// Two book moves; then the engines see different games, White winning, Black a draw; then the tablebases, a mate.
const char* APART = R"([White "Alpha"]
[Black "Bravo"]
[Result "1-0"]

1. e4 {book} e5 {book} 2. Nf3 {+1.00/20 1.000s, n=2000000, sd=30, nps=2000000, tbhits=0, pv="g1f3 b8c6 f1b5", wdl=900/100/0, dt=0.500s}
Nc6 {-0.30/18 2.000s, n=1000000, sd=28, nps=500000, tbhits=0, pv="b8c6 f1c4", wdl=100/600/300, dt=1.500s}
3. Bb5 {+1.10/22 1.000s, wdl=900/100/0} a6 {-0.35/19 1.000s, wdl=100/600/300}
4. Ba4 {+1.20/21 1.000s, wdl=900/100/0} Nf6 {-0.40/18 1.000s, wdl=100/600/300}
5. O-O {+1.30/22 1.000s, wdl=900/100/0} Be7 {-0.50/17 1.000s, tbhits=12, wdl=300/500/200}
6. Re1 {+M5/30 1.000s, tbhits=40, wdl=1000/0/0, White mates} 1-0
)";

}  // namespace

TEST_CASE("engine notes: a player's evaluation, the text beside its move, its time") {
    const auto tree   = treeOf(APART);
    const auto& moves = tree.main.moves;
    CHECK_FALSE(playerEvaluation(moves[0]));  // a book move
    CHECK(playerEvaluation(moves[2]));
    CHECK(hasPlayerEvaluations(tree.main));
    // White's evaluation and the depth, whoever moved; the time after them when asked.
    CHECK(engineTag(moves[2], EngineTags::EvalDepth) == "+1.00/20");
    CHECK(engineTag(moves[3], EngineTags::EvalDepth) == "+0.30/18");
    CHECK(engineTag(moves[3], EngineTags::EvalDepthTime) == "+0.30/18 2.00s");
    CHECK(engineTag(moves[10], EngineTags::EvalDepth) == "#3/30");
    CHECK(engineTag(moves[2], EngineTags::Nothing).empty());
    CHECK(engineTag(moves[0], EngineTags::EvalDepth).empty());

    // An analysis written into the game is not its players': Lichess's [%eval] alone. A tournament's ChessBase style
    // is, its time its [%emt].
    const auto lichess = treeOf("[Event \"?\"]\n\n1. e4 {[%eval 0.17] [%clk 0:03:00]} e5 {[%eval 0.19]} *\n");
    CHECK_FALSE(playerEvaluation(lichess.main.moves[0]));
    CHECK_FALSE(hasPlayerEvaluations(lichess.main));
    const auto chessbase = treeOf(
        "[Event \"?\"]\n\n1. e4 {[%eval 0.17,20] [%clk 0:00:59.000] [%emt 0:00:01.340], "
        "n=1000} e5 {[%eval 0.19,18] [%emt 0:00:02.000]} *\n");
    CHECK(playerEvaluation(chessbase.main.moves[0]));
    CHECK(moveTime(chessbase.main.moves[0]) == 1340);
    CHECK(engineTag(chessbase.main.moves[0], EngineTags::EvalDepthTime) == "+0.17/20 1.34s");
    CHECK(engineTag(chessbase.main.moves[1], EngineTags::EvalDepth) == "+0.19/18");
    CHECK(moveTime(moves[2]) == 1000);
    CHECK(moveTime(moves[0]) == -1);
}

TEST_CASE("engine notes: a move's fields in full, and the line it expected after it") {
    const auto tree = treeOf(APART);
    const auto d    = engineDetail(tree.main, false, 4);  // 2...Nc6
    REQUIRE(d);
    CHECK(d->title == "2…Nc6 · Bravo, Black");
    REQUIRE(d->rows.size() >= 5);
    CHECK(d->rows[0].first == "Evaluation");
    CHECK(d->rows[0].second == "+0.30 for White, -0.30 from its side");
    CHECK(d->rows[1].second == "18, selective 28");
    CHECK(d->rows[2].second == "2.00s · move chosen at 1.50s");
    CHECK(d->rows[3].second == "1.0M · 500k n/s");
    CHECK(d->rows[4].second == "no access");
    REQUIRE(d->wdl);
    CHECK((*d->wdl)[1] == doctest::Approx(0.6f));
    CHECK(d->wdl_text == "win 10 % · draw 60 % · loss 30 %");
    CHECK(d->expected == "3.Bc4");
    // White's move: its evaluation once; without the fields it was not written with.
    const auto white = engineDetail(tree.main, false, 5);
    REQUIRE(white);
    CHECK(white->rows[0].second == "+1.10 for White");
    CHECK(white->expected.empty());
    CHECK_FALSE(engineDetail(tree.main, false, 1));  // a book move
    CHECK_FALSE(engineDetail(tree.main, false, 99));

    // The PV from the move played on; none when it starts with another move.
    CHECK(expectedLine(tree.main.moves[2], false) == std::vector<std::string>{"b8c6", "f1b5"});
    auto other = tree.main.moves[2];
    other.pv   = "d2d4 d7d5";
    CHECK(expectedLine(other, false).empty());
}

TEST_CASE("engine notes: the game's curve as its engines saw it, and what stands out") {
    const auto tree = treeOf(APART);
    const auto c    = engineCurves(tree.main);
    CHECK(c.plies == 11);
    CHECK(c.book == 2);
    CHECK(c.names[0] == "Alpha");
    CHECK(c.names[1] == "Bravo");
    // Each at its own moves, White's chances from its WDL, turned around for Black.
    REQUIRE(c.win[0].size() == 5);
    CHECK(c.win[0][0].ply == 3);
    CHECK(c.win[0][0].win == doctest::Approx(0.95f));
    REQUIRE(c.win[1].size() == 4);
    CHECK(c.win[1][0].ply == 4);
    CHECK(c.win[1][0].win == doctest::Approx(0.6f));
    CHECK(curveAt(c, 1, 5) == doctest::Approx(0.6f));
    CHECK(std::isnan(curveAt(c, 1, 3)));
    // The time of each move, and the figures on average.
    CHECK(c.time_used[0] == 5000);
    CHECK(c.time_used[1] == 5000);
    REQUIRE(c.depth[0]);
    CHECK(*c.depth[0] == doctest::Approx(23.0));
    REQUIRE(c.nps[1]);

    // Apart from the first moves out of the book to the mate: one span, its moments in the game's order.
    REQUIRE(c.apart.size() == 1);
    CHECK(c.apart[0] == std::pair<int, int>{3, 11});
    REQUIRE(c.moments.size() == 4);
    CHECK(c.moments[0].kind == EngineMoment::Kind::BookEnd);
    CHECK(c.moments[0].ply == 3);
    CHECK(c.moments[0].text ==
          "Out of the book: 95 % for White according to Alpha, 60 % according to Bravo. 35 points apart.");
    CHECK(c.moments[1].kind == EngineMoment::Kind::Apart);
    CHECK(c.moments[1].last == 11);
    CHECK(c.moments[1].text ==
          "They see different games: 95 to 100 % for Alpha; 45 to 60 % for Bravo. Bravo sees a "
          "draw at 50 to 60 %. The tablebases enter their searches.");
    CHECK(c.moments[2].kind == EngineMoment::Kind::Mate);
    CHECK(c.moments[2].ply == 11);
    CHECK(c.moments[2].text == "Mate in 3 for Alpha, seen with the tablebases (40 hits).");
    CHECK(c.moments[3].kind == EngineMoment::Kind::End);
    CHECK(c.moments[3].text == "1-0, White mates.");

    // Two engines that agree, a draw: the book's end and the end only.
    const auto calm  = treeOf(R"([White "Alpha"]
[Black "Bravo"]
[Result "1/2-1/2"]

1. e4 {+0.20/20 1.000s} e5 {-0.10/20 1.000s} 2. Nf3 {+0.20/20 1.000s} Nc6 {-0.15/20 1.000s, Draw by 3-fold repetition} 1/2-1/2
)");
    const auto quiet = engineCurves(calm.main);
    CHECK(quiet.book == 0);
    CHECK(quiet.apart.empty());
    REQUIRE(quiet.moments.size() == 2);
    CHECK(quiet.moments[0].text.rfind("From the first moves:", 0) == 0);
    CHECK(quiet.moments[0].text.find("They agree.") != std::string::npos);
    CHECK(quiet.moments[1].text == "½-½, Draw by 3-fold repetition.");
}

TEST_CASE("engine notes: Copy PGN and saving give the tournament's fields back as they were read") {
    const auto tree = treeOf(APART);
    auto text       = gamePgn(tree, false);
    for (auto& c : text) c = c == '\n' ? ' ' : c;
    CHECK_MESSAGE(text.find("2. Nf3 {+1.00/20 1.000s, n=2000000, sd=30, nps=2000000, tbhits=0, pv=\"g1f3 b8c6 f1b5\", "
                            "wdl=900/100/0, dt=0.500s}") != std::string::npos,
                  text);
    CHECK_MESSAGE(text.find("6. Re1 {+M5/30 1.000s, tbhits=40, wdl=1000/0/0, White mates} 1-0") != std::string::npos,
                  text);
}
