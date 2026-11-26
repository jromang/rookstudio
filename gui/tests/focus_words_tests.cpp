// The table of --focus words (focus_words.hpp): its patterns, and the first entry that matches applied alone.

#include <doctest/doctest.hpp>

#include <string>

#include "focus_words.hpp"

using namespace fcg;

TEST_CASE("focus words: a pattern is the word itself, or has a '*' for any text") {
    CHECK(focusWordMatches("chess-home", "chess-home"));
    CHECK_FALSE(focusWordMatches("chess-home", "chess-homes"));
    CHECK_FALSE(focusWordMatches("chess-home", "chess"));
    CHECK_FALSE(focusWordMatches("chess-home", ""));

    // At the end: the word's start.
    CHECK(focusWordMatches("board-tree=*", "board-tree=games.pgn@3"));
    CHECK(focusWordMatches("board-tree=*", "board-tree="));
    CHECK_FALSE(focusWordMatches("board-tree=*", "board-tree"));
    CHECK_FALSE(focusWordMatches("board-tree=*", "board-trees=x"));
    CHECK(focusWordMatches("board-analysis*", "board-analysis"));
    CHECK(focusWordMatches("board-analysis*", "board-analysis-menu=games.pgn"));
    CHECK_FALSE(focusWordMatches("board-analysis*", "board-analyse"));

    // Inside: export=, export-db=… but no word of export without a '='.
    CHECK(focusWordMatches("export*=*", "export=games.pgn@html"));
    CHECK(focusWordMatches("export*=*", "export-db=pgn@all"));
    CHECK(focusWordMatches("export*=*", "exports="));
    CHECK_FALSE(focusWordMatches("export*=*", "export"));
    CHECK_FALSE(focusWordMatches("export*=*", "export-db"));
    CHECK_FALSE(focusWordMatches("export*=*", "submission-export=x"));

    CHECK(focusWordMatches("*", ""));
    CHECK(focusWordMatches("*", "anything"));
    CHECK_FALSE(focusWordMatches("a*", ""));
}

TEST_CASE("focus words: the first entry that matches is applied, the others passed over") {
    std::string applied;
    FocusWords words("databases-list=player535");
    words.on({"databases"}, [&] { applied += 'a'; });
    words.on({"databases-list", "databases-list=*"}, [&] { applied += 'b'; });
    words.on({"databases-list=*"}, [&] { applied += 'c'; });
    CHECK(words.applied());
    CHECK(applied == "b");

    FocusWords none("settings");
    none.on({"databases*", "lichess"}, [&] { applied += 'd'; });
    CHECK_FALSE(none.applied());
    CHECK(applied == "b");
}
