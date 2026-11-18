// A review written into a game (the prototype's crates/analysis/src/annotate.rs and game/src/nag.rs, mod tests).

#include <doctest/doctest.hpp>

#include <initializer_list>
#include <string_view>

#include <commentary/annotate.hpp>
#include <study/pgn_reader.hpp>
#include <study/pgn_writer.hpp>

#include "helpers.hpp"

using namespace fcg;
using namespace fcg::commentary;

namespace {

MainLine game(std::initializer_list<std::string_view> ucis) {
    const auto start = Position::initial();
    return MainLine(start, test::moves(start, ucis));
}

// Moves from the position `ply` of the line, in UCI.
std::vector<Move> at(const MainLine& g, std::size_t ply, std::initializer_list<std::string_view> ucis) {
    return test::moves(g.positions()[ply], ucis);
}

PositionAnalysis analysis(std::vector<Move> moves, int32_t cp) {
    return PositionAnalysis{{EngineLine{std::move(moves), Score::cp(cp), std::nullopt, 18}}, std::nullopt};
}

GameTree tree(std::string_view pgn) {
    auto read = study::readPgnGame(pgn);
    REQUIRE(read.tree);
    return *read.tree;
}

}  // namespace

TEST_CASE("annotate: a move's assessments exclude one another, a position's too") {
    std::vector<uint8_t> nags;
    toggleNag(nags, 146);
    toggleNag(nags, 1);
    toggleNag(nags, 16);
    toggleNag(nags, 2);
    CHECK(nags == std::vector<uint8_t>{2, 16, 146});
    toggleNag(nags, 16);
    CHECK(nags == std::vector<uint8_t>{2, 146});
}

TEST_CASE("annotate: mistakes get their symbols and the better line, what the user wrote is kept") {
    const auto g = game({"e2e4", "e7e5", "d1h5", "e8e7"});
    Analyses a(g.positions().size());
    a[0] = analysis(at(g, 0, {"e2e4"}), 30);
    a[1] = analysis(at(g, 1, {"e7e5"}), 30);
    a[2] = analysis(at(g, 2, {"g1f3"}), 30);
    a[3] = analysis(at(g, 3, {"b8c6", "f1c4"}), -40);
    a[4] = analysis(at(g, 4, {"h5e5"}), 600);
    // The user judged 2.Qh5 themselves.
    auto t            = tree("1. e4 e5 2. Qh5 $5 Ke7 *");
    const auto report = review(g, a, Opening{});
    const auto added  = annotate(t, g, report, a);

    CHECK(t.main.moves[2].notes.nags == std::vector<uint8_t>{5});  // the user's symbol kept
    CHECK(t.main.moves[3].notes.nags == std::vector<uint8_t>{4});  // ?? added
    // 2...Ke7's evaluation, White's +6.00 as the game holds it: for the mover, Black.
    REQUIRE(t.main.moves[3].score);
    CHECK(t.main.moves[3].score->type == fastchess::ScoreType::CP);
    CHECK(t.main.moves[3].score->value == -600);
    CHECK(t.main.moves[3].depth == 18);
    CHECK(t.main.moves[3].notes.eval_tag);
    // 2...Ke7?? gets the better line 2...Nc6 3.Bc4, marked ⌓, ending with ⩱ for -0.40.
    REQUIRE(added == std::vector<int>{0});
    REQUIRE(t.variations.size() == 1);
    const auto& better = t.variations[0];
    CHECK(better.parent == -1);
    CHECK(better.from == 3);
    REQUIRE(better.moves.size() == 2);
    CHECK(better.moves[0].san == "Nc6");
    CHECK(better.moves[1].san == "Bc4");
    CHECK(better.moves[0].notes.nags == std::vector<uint8_t>{NAG_BETTER_IS});
    CHECK(better.moves[1].notes.nags == std::vector<uint8_t>{15});
    // The inaccuracy 2.Qh5 gets no line with the default options; annotating again adds no line twice.
    CHECK(annotate(t, g, report, a).empty());
    CHECK(t.variations.size() == 1);
    // As PGN, read back the same.
    const auto text = study::writePgnGame(t, {});
    CHECK(text.find("2. Qh5!? {[%eval -0.40,18]}") != std::string::npos);
    CHECK(text.find("2... Ke7?? {[%eval 6.00,18]}") != std::string::npos);
    CHECK(text.find("(2... Nc6 $142 3. Bc4 $15)") != std::string::npos);
    const auto again = study::readPgnGame(text);
    REQUIRE(again.tree);
    CHECK(again.tree->variations.size() == 1);
}

TEST_CASE("annotate: an only move, a novelty, and a tournament engine's evaluation kept") {
    const auto g = game({"e2e4", "e7e5"});
    Analyses a(g.positions().size());
    a[0]   = analysis(at(g, 0, {"e2e4"}), 30);
    a[1]   = analysis(at(g, 1, {"e7e5"}), 30);
    a[2]   = analysis(at(g, 2, {"g1f3"}), 30);
    auto t = tree("1. e4 {+0.25/20 1.0s} e5 *");
    GameReport report;
    report.moves.resize(2);
    report.moves[0].ply       = 1;
    report.moves[0].only_move = true;
    report.moves[1].ply       = 2;
    report.moves[1].novelty   = true;
    report.moves[1].judgment  = Judgment::Inaccuracy;
    annotate(t, g, report, a);
    CHECK(t.main.moves[0].notes.nags == std::vector<uint8_t>{NAG_ONLY_MOVE});
    CHECK(t.main.moves[1].notes.nags == std::vector<uint8_t>{6, NAG_NOVELTY});
    REQUIRE(t.main.moves[0].score);
    CHECK(t.main.moves[0].score->value == 25);  // the tournament engine's, not replaced
    CHECK_FALSE(t.main.moves[0].notes.eval_tag);
    CHECK(t.main.moves[1].score->value == -30);
    CHECK(t.variations.empty());
}
