// The prototype's crates/facts/src/oracle.rs and tablebase.rs tests (those its Syzygy tables need left out: none here).

#include "helpers.hpp"

#include <algorithm>

#include <commentary/oracle.hpp>
#include <commentary/tablebase.hpp>

using namespace fcg::commentary;
using test::moves;
using test::pos;

TEST_CASE("oracle: a threat is what the engine would play moving again") {
    // After 1.Nd5 (Black to move), White moving again plays 2.Nc7+ and takes the rook.
    const auto before = pos("r3k3/8/8/8/8/2N5/8/4K3 w - - 0 1");
    auto after        = before;
    after.playUnchecked(moves(before, {"c3d5"})[0]);
    const auto again = *after.swappedTurn();
    RecordedOracle table;
    table.answer(again, {}, {"d5c7", "e8d8", "c7a8"}, Score::cp(520));
    auto found = engineThreats(after, Color::White, table);
    REQUIRE(found);
    REQUIRE(found->size() == 1);
    const auto& capture = std::get<fact::ThreatensCapture>((*found)[0]);
    CHECK(capture.mv.to() == Square::C7);
    CHECK(capture.target.role == Role::Rook);
    CHECK(capture.target.square == Square::A8);
    // A queen traded on the way is not what the threat wins.
    const auto traded = pos("r2q3k/8/2N5/3Q4/8/8/8/7K b - - 0 1");
    RecordedOracle table2;
    table2.answer(*traded.swappedTurn(), {}, {"d5d8", "a8d8", "c6d8"}, Score::cp(520));
    found = engineThreats(traded, Color::White, table2);
    REQUIRE(found);
    REQUIRE(found->size() == 1);
    CHECK(std::get<fact::ThreatensCapture>((*found)[0]).target.role == Role::Rook);
    // Nothing won moving again: no threat, though one was asked about.
    RecordedOracle quiet;
    quiet.answer(again, {}, {"d5e3", "e8d8"}, Score::cp(30));
    found = engineThreats(after, Color::White, quiet);
    REQUIRE(found);
    CHECK(found->empty());
    // Not asked: no answer, the static detector stays.
    CHECK_FALSE(engineThreats(after, Color::White, RecordedOracle{}));
    // A mate, however many moves away.
    RecordedOracle mating;
    mating.answer(again, {}, {"d5c7", "e8d8"}, Score::mate(3));
    found = engineThreats(after, Color::White, mating);
    REQUIRE(found);
    REQUIRE(found->size() == 1);
    CHECK(std::holds_alternative<fact::ThreatensMate>((*found)[0]));
}

TEST_CASE("oracle: a trapped piece that the engine saves escapes") {
    // The bishop on a7, Black to move after b6: Bxb6 is met by cxb6, but Bb8 gets out.
    const auto p = pos("r3k3/B1p5/1p6/8/8/8/8/4K3 w - - 0 1");
    const Placed bishop{Color::White, Role::Bishop, Square::A7};
    RecordedOracle t1;
    t1.answer(p, {"a7b6", "a7b8"}, {"a7b8", "a8b8"}, Score::cp(-300));
    CHECK_FALSE(escapes(p, bishop, t1));  // taken on b8
    RecordedOracle t2;
    t2.answer(p, {"a7b6", "a7b8"}, {"a7b6", "c7b6"}, Score::cp(-300));
    CHECK_FALSE(escapes(p, bishop, t2));  // taken on b6
    RecordedOracle t3;
    t3.answer(p, {"a7b6", "a7b8"}, {"a7b8", "e8d7", "b8c7"}, Score::cp(0));
    CHECK(escapes(p, bishop, t3));                      // it got away
    CHECK_FALSE(escapes(p, bishop, RecordedOracle{}));  // not asked
}

TEST_CASE("oracle: zugzwang is standing worse with the move") {
    // Black to move must give way to the white king; White to move would get nowhere.
    const auto p = pos("8/8/3k4/3P4/3K4/8/8/8 b - - 0 1");
    RecordedOracle table;
    table.answer(*p.swappedTurn(), {}, {"d4e4", "d6d7"}, Score::cp(0));
    const auto chances = [](Score s) { return s == Score::cp(0) ? 50.0 : 100.0; };
    CHECK(zugzwang(p, Color::White, 100.0, chances, table) == std::optional<Fact>(Fact{fact::Zugzwang{Color::Black}}));
    CHECK_FALSE(zugzwang(p, Color::White, 55.0, chances, table));
}

TEST_CASE("tablebase: what the tables are taught they tell") {
    // Lucena's position, with nothing on disk: unknown until taught, with every move.
    Tablebases tb;
    const auto p = pos("1K1k4/1P6/8/8/8/8/r7/2R5 w - - 0 1");
    CHECK(tb.wants(p));
    CHECK_FALSE(tb.verdict(p));
    const auto rc4 = moves(p, {"c1c4"})[0];
    CHECK(tb.moveFacts(p, rc4).empty());
    std::vector<std::pair<Move, Verdict>> lesson{{rc4, Verdict::Win}};
    for (const auto& m : p.legalMoves()) {
        if (m != rc4) lesson.emplace_back(m, Verdict::Draw);
    }
    tb.learn(p, Verdict::Win, lesson);
    CHECK_FALSE(tb.wants(p));
    CHECK(tb.verdict(p) == Verdict::Win);
    CHECK(tb.best(p) == rc4);
    const auto has = [](const std::vector<Fact>& facts, const Fact& f) {
        return std::find(facts.begin(), facts.end(), f) != facts.end();
    };
    CHECK(has(tb.moveFacts(p, rc4), Fact{fact::OnlyMoveToKeep{Color::White, Verdict::Win}}));
    CHECK(has(tb.moveFacts(p, moves(p, {"c1c2"})[0]), Fact{fact::SpoilsWin{Color::White, Verdict::Draw}}));
    // The position a move leads to is known by its outcome, not yet by its moves.
    auto after = p;
    after.playUnchecked(rc4);
    CHECK(tb.verdict(after) == Verdict::Loss);
    CHECK(tb.wants(after));
    // A position reached from one the tables do not know is told for what it is.
    const auto before = pos("1K1k4/1P6/8/8/8/7P/r5p1/N1R5 b - - 0 1");
    CHECK_FALSE(tb.wants(before));  // eight men
    tb.learn(pos("1K1k4/1P6/8/8/8/7P/6p1/r1R5 w - - 0 2"), Verdict::Win, {});
    CHECK(tb.moveFacts(before, moves(before, {"a2a1"})[0]) ==
          std::vector<Fact>{Fact{fact::Tablebase{Color::Black, Verdict::Loss}}});
    // Castling rights are not in the tables.
    CHECK_FALSE(tb.wants(pos("4k3/8/8/8/8/8/8/4K2R w K - 0 1")));
}
