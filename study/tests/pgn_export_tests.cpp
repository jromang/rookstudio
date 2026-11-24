// Games leaving a database as PGN (#93): as they were read, or with the table of names' names, FIDE ids and titles
// written in their tags; players of our own invention.

#include <doctest/doctest.hpp>

#include <study/import.hpp>
#include <study/pgn_export.hpp>
#include <study/pgn_reader.hpp>
#include <study/players.hpp>

#include "test_games.hpp"

using namespace fcg;
using namespace fcg::study;

namespace {

ImportedGame exportGame(const std::string& white, const std::string& black, const std::string& extra,
                        const std::string& moves) {
    const std::string text =
        "[Event \"Club \\\"Rook\\\" Cup\"]\n[Site \"Lyon\"]\n[Date \"2026.05.04\"]\n[Round \"2\"]\n"
        "[White \"" +
        white + "\"]\n[Black \"" + black + "\"]\n[Result \"1-0\"]\n" + extra + "\n" + moves + " 1-0\n";
    ImportedGame game;
    REQUIRE(readImportedGame(text, game));
    return game;
}

}  // namespace

TEST_CASE("pgn export: the games as they were read, or with the table's names, FIDE ids and titles in their tags") {
    std::vector<ImportedGame> games;
    games.push_back(exportGame("Duval,A", "Ito, Ken", "[WhiteElo \"2380\"]\n", "1. e4 c5 2. Nf3 d6 3. d4"));
    // Annotated: kept as its text, comment and variation.
    games.push_back(exportGame("Brennan, Liam", "Anne Duval", "", "1. d4 {A quiet start.} Nf6 (1... d5) 2. c4 e6"));
    auto db         = fcg::test::database("pgn-export", std::move(games));
    const auto snap = db->snapshot();
    std::vector<uint32_t> spellings;
    for (uint32_t id = 0; id < snap->names(); ++id)
        if (snap->name(id) == "Duval,A" || snap->name(id) == "Anne Duval") spellings.push_back(id);
    REQUIRE(spellings.size() == 2);
    db->setPlayers(withPlayer(PlayerTable{}, spellings, "Duval, Anne", [](Player& p) {
        p.fide  = 90000011;
        p.title = "WIM";
    }));
    const auto table = db->snapshot();

    // As read: the text the database gives, names as written.
    const auto plain = exportedPgn(*table, 0, false);
    CHECK(plain == importedPgn(table->game(0)));
    CHECK(plain.find("[White \"Duval,A\"]") != std::string::npos);
    CHECK(plain.find("FideId") == std::string::npos);

    // Written: the table's name, FIDE id and title; the opponent the table does not know as written.
    const auto named = exportedPgn(*table, 0, true);
    CHECK(named.find("[White \"Duval, Anne\"]") != std::string::npos);
    CHECK(named.find("[WhiteFideId \"90000011\"]") != std::string::npos);
    CHECK(named.find("[WhiteTitle \"WIM\"]") != std::string::npos);
    CHECK(named.find("[Black \"Ito, Ken\"]") != std::string::npos);
    CHECK(named.find("BlackFideId") == std::string::npos);
    CHECK(named.find("[WhiteElo \"2380\"]") != std::string::npos);
    CHECK(named.find("[Event \"Club \\\"Rook\\\" Cup\"]") != std::string::npos);  // escaped as it was
    // It reads back: the same moves, the names written.
    const auto read = readPgnGame(named);
    REQUIRE(read.tree);
    CHECK(read.tree->main.header("White") == "Duval, Anne");
    CHECK(read.tree->main.header("Event") == "Club \"Rook\" Cup");
    CHECK(read.tree->main.moves.size() == 5);

    // The annotated game: its comment and variation kept, Black's name written.
    const auto annotated = exportedPgn(*table, 1, true);
    CHECK(annotated.find("{A quiet start.}") != std::string::npos);
    CHECK(annotated.find("(1... d5)") != std::string::npos);
    CHECK(annotated.find("[Black \"Duval, Anne\"]") != std::string::npos);
    CHECK(annotated.find("[BlackFideId \"90000011\"]") != std::string::npos);
    CHECK(annotated.find("[White \"Brennan, Liam\"]") != std::string::npos);
    const auto again = readPgnGame(annotated);
    REQUIRE(again.tree);
    CHECK(again.tree->variations.size() == 1);
    CHECK(again.tree->main.moves[0].notes.after == "A quiet start.");

    // Written twice, the same: the tags already there are replaced, not added again.
    CHECK(pgnWithNames(named, tableName(*table, table->rowIds(0).white), std::nullopt) == named);
}
