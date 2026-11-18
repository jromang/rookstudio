// SAN read back, and the opening names (the prototype's crates/analysis/src/openings.rs, mod tests).

#include <doctest/doctest.hpp>

#include <commentary/openings.hpp>

#include "helpers.hpp"

using namespace fcg::commentary;

TEST_CASE("position: SAN names the one legal move it describes") {
    const auto start = Position::initial();
    CHECK(start.parseSan("e4") == start.parseUci("e2e4"));
    CHECK(start.parseSan("Nf3+") == start.parseUci("g1f3"));
    CHECK(start.parseSan("Nd3") == std::nullopt);
    CHECK(start.parseSan("Ke2") == std::nullopt);
    CHECK(start.parseSan("") == std::nullopt);

    // Two knights reach d2: the file or the rank tells them apart; without it, the move is ambiguous.
    const auto knights = test::pos("4k3/8/8/8/8/5N2/8/1N2K3 w - - 0 1");
    CHECK(knights.parseSan("Nd2") == std::nullopt);
    CHECK(knights.parseSan("Nbd2") == knights.parseUci("b1d2"));
    CHECK(knights.parseSan("Nfd2") == knights.parseUci("f3d2"));
    CHECK(knights.parseSan("N1d2") == knights.parseUci("b1d2"));
    CHECK(knights.parseSan("Nb1d2") == knights.parseUci("b1d2"));

    // Captures say so; promotions name their piece, with or without "=".
    const auto captures = test::pos("1n2k3/P7/8/3p4/4P3/8/8/4K3 w - - 0 1");
    CHECK(captures.parseSan("exd5") == captures.parseUci("e4d5"));
    CHECK(captures.parseSan("ed5") == std::nullopt);
    CHECK(captures.parseSan("a8=Q") == captures.parseUci("a7a8q"));
    CHECK(captures.parseSan("axb8=N+") == captures.parseUci("a7b8n"));
    CHECK(captures.parseSan("axb8R") == captures.parseUci("a7b8r"));
    CHECK(captures.parseSan("a8") == std::nullopt);

    // En passant and castling.
    const auto ep = test::pos("r3k2r/8/8/3pP3/8/8/8/R3K2R w KQkq d6 0 1");
    CHECK(ep.parseSan("exd6") == ep.parseUci("e5d6"));
    CHECK(ep.parseSan("O-O") == ep.parseUci("e1g1"));
    CHECK(ep.parseSan("O-O-O") == ep.parseUci("e1c1"));
    CHECK(ep.parseSan("Kg1") == std::nullopt);
    // Each legal move's own SAN reads back as the move.
    for (const auto& p : {start, knights, captures, ep}) {
        for (const auto& m : p.legalMoves()) {
            CHECK(p.parseSan(p.sanPlus(m)) == m);
            CHECK(p.parseSan(p.san(m)) == m);
        }
    }
}

TEST_CASE("openings: the Najdorf is named and keeps its name") {
    const auto start = Position::initial();
    const auto najdorf =
        test::moves(start, {"e2e4", "c7c5", "g1f3", "d7d6", "d2d4", "c5d4", "f3d4", "g8f6", "b1c3", "a7a6"});
    CHECK(openingName(najdorf) == "Sicilian Defense: Najdorf Variation");
    // 6.h3 has a name of its own; a move the table does not know keeps it.
    auto after = start;
    for (const auto& m : najdorf) after.playUnchecked(m);
    auto later = najdorf;
    for (const auto& m : test::moves(after, {"h2h3", "a8a7"})) later.push_back(m);
    CHECK(openingName(later) == "Sicilian Defense: Najdorf Variation, Adams Attack");
    CHECK(openingName({}) == std::nullopt);
    // With its ECO code, for a game's ECO tag.
    REQUIRE(openingCode(najdorf));
    CHECK(openingCode(najdorf)->eco == "B90");
    CHECK(openingCode(najdorf)->name == "Sicilian Defense: Najdorf Variation");
    CHECK(openingCode(later)->eco == "B90");
    CHECK(openingCode({}) == std::nullopt);
    // A transposition finds the name of the position: the Queen's Gambit Declined reached by 1.c4.
    const auto english = test::moves(start, {"c2c4", "e7e6", "d2d4", "d7d5"});
    const auto qgd     = test::moves(start, {"d2d4", "d7d5", "c2c4", "e7e6"});
    REQUIRE(openingName(qgd));
    CHECK(openingName(english) == openingName(qgd));
}
