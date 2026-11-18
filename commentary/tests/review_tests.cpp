// The review of a game (the prototype's crates/analysis/src/report.rs, phase.rs and eval.rs, mod tests).

#include <doctest/doctest.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string_view>

#include <commentary/review.hpp>

#include "helpers.hpp"

using namespace fcg::commentary;

namespace {

// A main line from UCI moves.
MainLine game(std::initializer_list<std::string_view> ucis) {
    const auto start = Position::initial();
    return MainLine(start, test::moves(start, ucis));
}

// The move `uci` in the position `ply` of the line.
Move mv(const MainLine& g, std::size_t ply, std::string_view uci) {
    const auto m = g.positions()[ply].parseUci(uci);
    REQUIRE_MESSAGE(m, std::string(uci));
    return *m;
}

EngineLine line(std::vector<Move> moves, int32_t cp, std::optional<std::array<uint16_t, 3>> wdl = std::nullopt,
                uint32_t depth = 20) {
    return EngineLine{std::move(moves), Score::cp(cp), wdl, depth};
}

PositionAnalysis analysis(std::vector<EngineLine> lines) { return PositionAnalysis{std::move(lines), std::nullopt}; }

// Analyses for every position of `g`, none yet.
Analyses none(const MainLine& g) { return Analyses(g.positions().size()); }

bool near(double a, double b, double within) { return std::abs(a - b) < within; }

}  // namespace

TEST_CASE("eval: lichess win percent") {
    CHECK(near(winPercent(0), 50.0, 1e-9));
    CHECK(near(winPercent(300), 75.1, 0.1));
    CHECK(near(winPercent(-300), 24.9, 0.1));
    CHECK(winPercent(5000) == winPercent(CP_CAP));
    CHECK(near(winPercentFor(300, Color::Black), 24.9, 0.1));
}

TEST_CASE("eval: expected scores are the engine's own") {
    // Without the engine's figures, the pawn-anchored curve: level at nothing, half the games won (and hardly any lost)
    // at a pawn, all but won at a pawn and a half.
    CHECK(near(expected(0), 50.0, 1e-9));
    CHECK(near(expected(100), 75.0, 1.0));
    CHECK((expected(150) > 90.0 && expected(300) > 99.5));
    CHECK(near(expected(-150) + expected(150), 100.0, 1e-9));
    // With them, they are the word: 82.5 % won and 17.4 % drawn is winning, whatever the centipawns say.
    const auto told = EngineLine{{}, Score::cp(-152), std::array<uint16_t, 3>{1, 174, 825}, 1};
    CHECK(near(told.whiteExpected(), 8.8, 0.1));
    CHECK(standingOf(told.whiteExpected()) == Standing::Lost);
    CHECK(near(EngineLine{{}, Score::cp(-152), std::nullopt, 1}.whiteExpected(), expected(-152), 1e-9));
    // A mate is a mate, and a finished game what it is.
    CHECK(EngineLine{{}, Score::mate(3), std::array<uint16_t, 3>{900, 100, 0}, 1}.whiteExpected() == 100.0);
    CHECK(EngineLine{{}, Score::mate(-1), std::nullopt, 1}.whiteExpected() == 0.0);
    CHECK(PositionAnalysis::ended(GameEnd::draw()).whiteExpected() == 50.0);
    CHECK(forSide(30.0, Color::Black) == 70.0);
}

TEST_CASE("eval: scores and symbols") {
    CHECK(cappedCp(Score::mate(-2)) == -CP_CAP);
    CHECK(positionNag(expected(0)) == 10);
    CHECK(positionNag(expected(50)) == 14);
    CHECK(positionNag(expected(-100)) == 17);
    CHECK(positionNag(expected(200)) == 18);
    CHECK(standingOf(50.0) == Standing::Balanced);
    CHECK(Standing::Lost < Standing::Winning);
    const auto mated = PositionAnalysis::ended(GameEnd::checkmate(Color::Black));
    CHECK(mated.whiteCp() == -CP_CAP);
    CHECK(mated.hasMate(Color::Black));
    CHECK(mated.isMated(Color::White));
    const auto mating = analysis({EngineLine{{}, Score::mate(-3), std::nullopt, 1}});
    CHECK((mating.hasMate(Color::Black) && !mating.hasMate(Color::White)));
}

TEST_CASE("phase: phases follow material") {
    const auto start     = Position::initial();
    const auto developed = test::pos("2r2rk1/pp1q1ppp/2n1pn2/3p4/3P4/2N1PN2/PP1Q1PPP/2R2RK1 w - - 0 15");
    const auto ending    = test::pos("8/5k2/8/3r4/8/2R5/5K2/8 w - - 0 50");
    const std::vector<Position> along{start, developed, ending};
    CHECK(phases(along) == std::vector<Phase>{Phase::Opening, Phase::Middlegame, Phase::Endgame});
    // Once in the middlegame, a position looking like an opening does not go back.
    const std::vector<Position> back{developed, start};
    CHECK(phases(back) == std::vector<Phase>{Phase::Middlegame, Phase::Middlegame});
}

TEST_CASE("review: judgments follow expected score losses") {
    // 1.e4 e5 2.Qh5 Ke7?? : Black walks into trouble.
    const auto g = game({"e2e4", "e7e5", "d1h5", "e8e7"});
    auto a       = none(g);
    a[0]         = analysis({line({mv(g, 0, "e2e4")}, 30)});
    a[1]         = analysis({line({mv(g, 1, "e7e5")}, 30)});
    a[2]         = analysis({line({mv(g, 2, "g1f3")}, 30)});
    a[3]         = analysis({line({mv(g, 3, "b8c6")}, -40)});
    a[4]         = analysis({line({mv(g, 4, "h5e5")}, 600)});
    const auto r = review(g, a, Opening{});
    CHECK(r.complete);
    CHECK(r.moves.size() == 4);
    CHECK(r.get(1)->played_best);
    CHECK(r.get(1)->judgment == std::nullopt);
    // 2.Qh5 instead of 2.Nf3: +0.30 to −0.40 for White, about 8 points: inaccuracy.
    CHECK(r.get(3)->judgment == Judgment::Inaccuracy);
    // 2...Ke7 instead of 2...Nc6: from +0.40 to −6.00 for Black: blunder.
    const auto* blunder = r.get(4);
    CHECK(blunder->judgment == Judgment::Blunder);
    CHECK(blunder->cp_loss == 640);
    CHECK(blunder->key);
    CHECK(r.black.blunders == 1);
    CHECK(r.white.inaccuracies == 1);
    CHECK(*r.white.accuracy > *r.black.accuracy);
}

TEST_CASE("review: mate rules and only moves") {
    const auto g = game({"f2f3", "e7e5", "g2g4", "d8h4"});
    auto a       = none(g);
    a[0]         = analysis({line({mv(g, 0, "e2e4")}, 30)});
    a[1]         = analysis({line({mv(g, 1, "e7e5")}, -20)});
    a[2]         = analysis({line({mv(g, 2, "e2e4")}, -10)});
    // Black to move: Qh4# is the only good move, far ahead of anything else.
    const EngineLine mating{{mv(g, 3, "d8h4")}, Score::mate(-1), std::nullopt, 5};
    a[3]         = analysis({mating, line({mv(g, 3, "b8c6")}, 50)});
    a[4]         = PositionAnalysis::ended(GameEnd::checkmate(Color::Black));
    const auto r = review(g, a, Opening{});
    // 2.g4?? lets a mate in from an even position: blunder.
    CHECK(r.get(3)->judgment == Judgment::Blunder);
    const auto* mate = r.get(4);
    CHECK((mate->played_best && mate->only_move));
    CHECK(mate->judgment == std::nullopt);
}

TEST_CASE("review: taking back the only way is not an only move") {
    // 1.e4 d5 2.exd5 Qxd5: the queen alone can take back, and anything else loses a pawn.
    {
        const auto g           = game({"e2e4", "d7d5", "e4d5", "d8d5"});
        auto a                 = none(g);
        a[3]                   = analysis({line({mv(g, 3, "d8d5")}, 30), line({mv(g, 3, "g8f6")}, 400)});
        a[4]                   = analysis({line({mv(g, 4, "b1c3")}, 30)});
        const auto r           = review(g, a, Opening{});
        const auto* taken_back = r.get(4);
        CHECK((taken_back->played_best && taken_back->only_gap > ONLY_MOVE_GAP));
        CHECK_FALSE(taken_back->only_move);
    }
    // With two ways to take back, the right one is an only move.
    const auto g = game({"e2e4", "d7d5", "b1c3", "g8f6", "e4d5", "f6d5"});
    auto a       = none(g);
    a[5]         = analysis({line({mv(g, 5, "f6d5")}, 30), line({mv(g, 5, "d8d5")}, 400)});
    a[6]         = analysis({line({mv(g, 6, "c3d5")}, 30)});
    CHECK(review(g, a, Opening{}).get(6)->only_move);
}

TEST_CASE("review: a move is judged by the more severe of two readings") {
    // The engine's own figures: White wins 85 % of the time before 2.Qh5, and the game is level after it. In centipawns
    // that is a pawn and a half, a mistake between people; for the position it is a win thrown away.
    using Wdl    = std::array<uint16_t, 3>;
    const auto g = game({"e2e4", "e7e5", "d1h5", "b8c6"});
    auto a       = none(g);
    a[0]         = analysis({line({mv(g, 0, "e2e4")}, 20)});
    a[1]         = analysis({line({mv(g, 1, "e7e5")}, 20)});
    a[2]         = analysis({line({mv(g, 2, "g1f3")}, 150, Wdl{850, 145, 5})});
    a[3]         = analysis({line({mv(g, 3, "g8f6")}, 0, Wdl{60, 880, 60})});
    // Black answers with something else and is lost for it: from level to a win for White in the engine's figures,
    // though the centipawns move by little more than a pawn.
    a[4]               = analysis({line({mv(g, 4, "h5f7")}, 130, Wdl{960, 40, 0})});
    const auto r       = review(g, a, Opening{});
    const auto* thrown = r.get(3);
    CHECK((thrown->win_before > 90.0 && near(thrown->win_after, 50.0, 1.0)));
    CHECK(thrown->judgment == Judgment::Blunder);
    // The accuracy still reads the chances between people, as lichess does.
    CHECK(near(thrown->accuracy, moveAccuracy(63.5, 50.0), 1.0));
    CHECK(r.get(4)->judgment == Judgment::Blunder);
    CHECK(r.get(4)->win_after < 5.0);
}

TEST_CASE("review: a side already lost is still told what it drops") {
    // Black is lost in the engine's figures before and after the move, which costs it five more pawns: nothing for the
    // position, a blunder between people, had the game not been lost already.
    using Wdl           = std::array<uint16_t, 3>;
    const auto g        = game({"e2e4", "e7e5", "d1h5", "e8e7"});
    auto a              = none(g);
    a[3]                = analysis({line({mv(g, 3, "b8c6")}, 250, Wdl{1000, 0, 0})});
    a[4]                = analysis({line({mv(g, 4, "h5e5")}, 750, Wdl{1000, 0, 0})});
    const auto r        = review(g, a, Opening{});
    const auto* dropped = r.get(4);
    CHECK(dropped->win_before == 0.0);
    CHECK(dropped->win_after == 0.0);
    // Told, but for what it is: the game was lost before it.
    CHECK(dropped->judgment == Judgment::Inaccuracy);
    // Judged by the chances between people it threw away, which the engine's figures do not see: the loss said with
    // the judgment is theirs.
    CHECK(dropped->loss() == 0.0);
    CHECK(dropped->judgedLoss() > 15.0);
    CHECK(dropped->judgedLoss() == dropped->chances_lost);
    // From clearly worse to lost, the same drop is a blunder.
    a[3] = analysis({line({mv(g, 3, "b8c6")}, 90, Wdl{420, 560, 20})});
    CHECK(review(g, a, Opening{}).get(4)->judgment == Judgment::Blunder);
}

TEST_CASE("review: against the tablebases only the outcome counts") {
    // The engine thought Black nearly lost; the tables say the position after the move is lost. Nothing says the move
    // lost it: no judgment, though the centipawns fall from the engine's to the cap.
    using Wdl    = std::array<uint16_t, 3>;
    const auto g = game({"e2e4", "e7e5", "d1h5", "e8e7"});
    auto a       = none(g);
    a[3]         = analysis({line({mv(g, 3, "b8c6")}, 132, Wdl{804, 196, 0})});
    a[4]         = analysis({line({mv(g, 4, "h5e5")}, 1000, Wdl{1000, 0, 0})});
    CHECK((a[4]->isExact() && !a[3]->isExact()));
    CHECK(review(g, a, Opening{}).get(4)->judgment == std::nullopt);
    // From a draw by the tables to a loss by the tables, it is a blunder.
    a[3] = analysis({line({mv(g, 3, "b8c6")}, 0, Wdl{0, 1000, 0})});
    CHECK(review(g, a, Opening{}).get(4)->judgment == Judgment::Blunder);
}

TEST_CASE("review: quiet moves that carry out a plan are marked") {
    // The French advance: 3.e5 takes space and sets up the chain, 3...c5 strikes at its base; each side's first plan is
    // marked, nothing else.
    const auto g                               = game({"e2e4", "e7e6", "d2d4", "d7d5", "e4e5", "c7c5", "c2c3", "b8c6"});
    const std::vector<std::string_view> played = {"e2e4", "e7e6", "d2d4", "d7d5", "e4e5", "c7c5", "c2c3", "b8c6"};
    auto a                                     = none(g);
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto next = i < played.size() ? played[i] : std::string_view("g1f3");
        a[i]            = analysis({line({mv(g, i, next)}, 20)});
    }
    const auto r = review(g, a, Opening{});
    std::vector<bool> plans;
    for (const auto& m : r.moves) plans.push_back(m.plan);
    CHECK(plans == std::vector<bool>{false, false, false, false, true, true, false, false});
    CHECK(r.get(5)->isNotable());
    // Theory is not commented for its plans.
    const auto book = review(g, a, Opening{8, std::nullopt});
    CHECK(std::none_of(book.moves.begin(), book.moves.end(), [](const MoveReport& m) { return m.plan; }));
}

TEST_CASE("review: partial reviews and the accuracy formula") {
    const auto g = game({"e2e4", "e7e5"});
    auto a       = none(g);
    a[1]         = analysis({line({mv(g, 1, "e7e5")}, 30)});
    a[2]         = analysis({line({mv(g, 2, "g1f3")}, 30)});
    const auto r = review(g, a, Opening{});
    CHECK_FALSE(r.complete);
    CHECK(r.moves.size() == 1);
    CHECK(r.white.accuracy == std::nullopt);
    CHECK(moveAccuracy(50.0, 50.0) == 100.0);
    CHECK(near(moveAccuracy(60.0, 50.0), 64.6, 0.1));
}
