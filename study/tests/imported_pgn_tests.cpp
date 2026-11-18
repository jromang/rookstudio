#include <doctest/doctest.hpp>

#include <string>

#include <study/import.hpp>

#include "test_games.hpp"

using namespace fcg::study;
using namespace fcg::test;

namespace {

ImportedGame readBack(const ImportedGame& game) {
    ImportedGame again;
    REQUIRE(readImportedGame(importedPgn(game), again));
    return again;
}

}  // namespace

TEST_CASE("imported games as PGN: tags, moves and result read back the same") {
    ImportedGame game;
    REQUIRE(
        readImportedGame("[Event \"Test \\\"quoted\\\"\"]\n[White \"A\"]\n[Black \"B\"]\n[Result \"0-1\"]\n\n"
                         "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. O-O Nf6 5. Re1 0-1\n",
                         game));
    CHECK(game.text.empty());  // a game without comments keeps no text
    const auto again = readBack(game);
    CHECK(again.moves == game.moves);
    CHECK(again.plies == 9);
    CHECK(again.tags == game.tags);
    CHECK(again.result == '0');
    CHECK(importedPgn(game).find("4. O-O Nf6 5. Re1 0-1") != std::string::npos);
}

TEST_CASE("imported games as PGN: an annotated game is its own text") {
    ImportedGame game;
    const std::string text = "[Event \"T\"]\n\n1. e4 {best by test} e5 2. Nf3 (2. f4) Nc6 *\n";
    REQUIRE(readImportedGame(text, game));
    CHECK(game.annotated);
    CHECK(importedPgn(game) == text);
}

TEST_CASE("imported games as PGN: a start of its own, Black to move, and random games") {
    ImportedGame game;
    REQUIRE(
        readImportedGame("[FEN \"4k3/8/8/8/8/8/4P3/4K3 b - - 0 1\"]\n[SetUp \"1\"]\n\n1... Kd7 2. e4 Ke6 *\n", game));
    CHECK(importedPgn(game).find("1... Kd7 2. e4 Ke6 *") != std::string::npos);
    CHECK(readBack(game).moves == game.moves);
    for (const auto& g : randomGames(300, 5, 8)) {
        const auto again = readBack(g);
        CHECK(again.moves == g.moves);
        CHECK(again.plies == g.plies);
    }
}
