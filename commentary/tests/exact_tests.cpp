// Engine lines held to the tablebases (the prototype's crates/analysis/src/exact.rs, mod tests), on the ending of
// So–Shankland, U.S. Championship 2026: after 65...Kxh4 only 66.Rb5 wins.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <vector>

#include "commentary/exact.hpp"
#include "helpers.hpp"

using namespace fcg::commentary;

namespace {

Move mv(const Position& p, std::string_view uci) { return test::moves(p, {uci}).front(); }

EngineLine line(const Position& p, std::string_view uci, int32_t cp) {
    return EngineLine{{mv(p, uci)}, Score::cp(cp), std::nullopt, 20};
}

// Tables taught that `wins` are the winning moves of a won position, the others drawing.
void teach(Tablebases& tables, const Position& p, std::initializer_list<std::string_view> wins) {
    std::vector<Move> winning;
    for (const auto w : wins) winning.push_back(mv(p, w));
    std::vector<std::pair<Move, Verdict>> lesson;
    for (const auto& m : winning) lesson.emplace_back(m, Verdict::Win);
    for (const auto& m : p.legalMoves())
        if (std::find(winning.begin(), winning.end(), m) == winning.end()) lesson.emplace_back(m, Verdict::Draw);
    tables.learn(p, Verdict::Win, lesson);
}

std::vector<Move> firsts(const PositionAnalysis& a) {
    std::vector<Move> out;
    for (const auto& l : a.lines) out.push_back(l.moves.front());
    return out;
}

}  // namespace

TEST_CASE("exact: the engine's lines give way to the tables") {
    // The engine likes two moves about as much; only the second wins.
    const auto p = test::pos("8/8/6p1/R1K3P1/P6k/8/8/1r6 w - - 0 66");
    Tablebases tables;
    teach(tables, p, {"a5b5"});
    PositionAnalysis a{{line(p, "c5c4", 88), line(p, "a5b5", 87), line(p, "c5c6", 19)}, std::nullopt};
    CHECK(settle(p, a, tables));
    CHECK(a.lines[0].moves == std::vector<Move>{mv(p, "a5b5")});
    CHECK(a.lines[0].whiteExpected() == 100.0);
    CHECK(a.lines[1].whiteExpected() == 50.0);
    CHECK(a.lines.size() == 3);
    CHECK(a.isExact());
    // Settled once, it stays as it is.
    CHECK_FALSE(settle(p, a, tables));
}

TEST_CASE("exact: a winning move the engine did not list leads") {
    // Neither of the engine's moves wins: the tables' move comes first, bare. With two winning moves, the second
    // follows, so that the first is not an only move.
    const auto p     = test::pos("8/8/6p1/R1K3P1/P6k/8/8/1r6 w - - 0 66");
    const auto lines = [&] { return PositionAnalysis{{line(p, "c5c4", 88), line(p, "c5c6", 19)}, std::nullopt}; };
    auto a           = lines();
    Tablebases one;
    teach(one, p, {"a5b5"});
    CHECK(settle(p, a, one));
    CHECK(firsts(a) == std::vector<Move>{mv(p, "a5b5"), mv(p, "c5c4")});
    CHECK(a.lines[0].depth == 0);
    a = lines();
    Tablebases two;
    teach(two, p, {"a5b5", "a5a8"});
    CHECK(settle(p, a, two));
    CHECK(firsts(a) == std::vector<Move>{mv(p, "a5b5"), mv(p, "a5a8")});

    // Black to move and lost whatever it plays: the scores are White's, exact.
    const auto q = test::pos("8/8/6p1/1RK3P1/P6k/8/8/1r6 b - - 1 66");
    Tablebases tables;
    std::vector<std::pair<Move, Verdict>> lost;
    for (const auto& m : q.legalMoves()) lost.emplace_back(m, Verdict::Loss);
    tables.learn(q, Verdict::Loss, lost);
    PositionAnalysis b{{line(q, "b1a1", 60), line(q, "b1b5", 90)}, std::nullopt};
    CHECK(settle(q, b, tables));
    CHECK(std::all_of(b.lines.begin(), b.lines.end(), [](const EngineLine& l) { return l.whiteExpected() == 100.0; }));
    CHECK(b.lines[0].moves == std::vector<Move>{mv(q, "b1a1")});

    // What the tables do not know stays the engine's.
    const auto unknown = test::pos("8/8/6p1/R1K2kP1/P6P/8/8/1r6 b - - 4 64");
    PositionAnalysis c{{line(unknown, "b1b4", 28)}, std::nullopt};
    CHECK_FALSE(settle(unknown, c, tables));
}

TEST_CASE("exact: one line, and the best of the other moves after it") {
    // The engine found the win, alone in its analysis: the tables add the best of the rest, a draw, so that the review
    // can tell an only move. With two winning moves, the second one, and no only move.
    const auto p = test::pos("8/8/6p1/R1K3P1/P6k/8/8/1r6 w - - 0 66");
    Tablebases one;
    teach(one, p, {"a5b5"});
    PositionAnalysis a{{line(p, "a5b5", 87)}, std::nullopt};
    CHECK(settle(p, a, one));
    REQUIRE(a.lines.size() == 2);
    CHECK(a.lines[0].moves == std::vector<Move>{mv(p, "a5b5")});
    CHECK(a.lines[0].whiteExpected() == 100.0);
    CHECK(a.lines[1].whiteExpected() == 50.0);
    CHECK_FALSE(settle(p, a, one));
    Tablebases two;
    teach(two, p, {"a5b5", "a5a8"});
    PositionAnalysis b{{line(p, "a5b5", 87)}, std::nullopt};
    CHECK(settle(p, b, two));
    REQUIRE(b.lines.size() == 2);
    CHECK(b.lines[1].moves == std::vector<Move>{mv(p, "a5a8")});
    CHECK(b.lines[1].whiteExpected() == 100.0);
}
