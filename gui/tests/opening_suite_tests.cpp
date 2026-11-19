// A book of starting positions from Lichess's named openings (#53).
#include <doctest/doctest.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "opening_suite.hpp"

using namespace fcg;

TEST_CASE("opening suite: the families, the lengths, one position per name and per position, an even spread") {
    const std::vector<NamedOpening> all = {
        {"B20", "Sicilian Defense", "1. e4 c5"},
        {"B27", "Sicilian Defense: Hyperaccelerated", "1. e4 c5 2. Nf3 g6"},
        {"B27", "Sicilian Defense: Hyperaccelerated", "1. e4 c5 2. Nf3 g6 3. d4"},  // the name taken already
        {"C50", "Italian Game", "1. e4 e5 2. Nf3 Nc6 3. Bc4"},
        {"C50", "Italian Game: Transposed", "1. Nf3 Nc6 2. e4 e5 3. Bc4"},  // the same position
        {"D06", "Queen's Gambit", "1. d4 d5 2. c4"},
        {"A00", "Bad Line", "1. e4 Zz9"},  // cannot be read
    };
    OpeningSuiteOptions options;
    options.min_plies = 3;
    options.max_plies = 6;
    auto kept         = openingSuite(all, options);
    REQUIRE(kept.size() == 3);
    CHECK(kept[0].eco == "B27");
    CHECK(kept[0].plies == 4);
    CHECK(kept[0].epd ==
          "rnbqkbnr/pp1ppp1p/6p1/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - hmvc 0; fmvn 3; c0 \"B27 Sicilian Defense: "
          "Hyperaccelerated\";");
    CHECK(kept[1].name == "Italian Game");
    CHECK(kept[2].eco == "D06");
    // Two families; the second line of a name when the first is out of the lengths.
    options.families  = "BD";
    options.min_plies = 5;
    kept              = openingSuite(all, options);
    REQUIRE(kept.size() == 1);
    CHECK(kept[0].plies == 5);
    CHECK(kept[0].name == "Sicilian Defense: Hyperaccelerated");
    // An even spread when more come than wanted.
    options           = {};
    options.min_plies = 1;
    options.max_plies = 10;
    options.most      = 2;
    kept              = openingSuite(all, options);
    REQUIRE(kept.size() == 2);
    CHECK(kept[0].eco == "B20");
    CHECK(kept[1].eco == "C50");
}

TEST_CASE("opening suite: the program's table, and the book written") {
    const auto& table = namedOpenings();
    REQUIRE(table.size() > 3000);
    CHECK(table.front().eco == "A00");
    const auto kept = openingSuite(table, {});
    CHECK(kept.size() == 500);
    for (const auto& p : kept) {
        CHECK(p.plies >= 8);
        CHECK(p.plies <= 16);
    }
    const auto path = std::filesystem::temp_directory_path() / "fcg-opening-suite.epd";
    writeOpeningSuite(path, kept);
    std::ifstream in(path);
    std::string line;
    std::size_t lines = 0;
    while (std::getline(in, line)) {
        ++lines;
        CHECK(line.find(" c0 \"") != std::string::npos);
    }
    CHECK(lines == 500);
}
