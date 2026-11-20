// A chess.com player's games (#74): their name, the list of their months, what to ask for, what the profile keeps.
#include <doctest/doctest.hpp>

#include "chesscom.hpp"

using namespace fcg;

TEST_CASE("chess.com: a player's name, their months, the months to ask for") {
    CHECK(chesscom::playerKey("Some_Player-1") == "some_player-1");
    CHECK(chesscom::playerKey(" spaced name ") == "spacedname");
    CHECK(chesscom::playerKey("bad/name").empty());
    CHECK(chesscom::playerKey("é").empty());
    CHECK(chesscom::archivesUrl("p1") == "https://api.chess.com/pub/player/p1/games/archives");
    CHECK(chesscom::monthPgnUrl("p1", "2026/10") == "https://api.chess.com/pub/player/p1/games/2026/10/pgn");
    const auto months = chesscom::parseArchives(
        R"({"archives":["https://api.chess.com/pub/player/p1/games/2026/10","https://api.chess.com/pub/player/p1/games/2025/12",)"
        R"("https://api.chess.com/pub/player/p1/games/2026/01","not a month"]})");
    CHECK(months == std::vector<std::string>{"2025/12", "2026/01", "2026/10"});
    CHECK(chesscom::parseArchives("{}").empty());
    CHECK(chesscom::parseArchives("not json").empty());
    // Nothing added: every month. Some added: the others, and the newest added again (it may have grown).
    CHECK(chesscom::monthsToAsk(months, {}) == months);
    CHECK(chesscom::monthsToAsk(months, {"2025/12", "2026/01"}) == std::vector<std::string>{"2026/01", "2026/10"});
    CHECK(chesscom::monthsToAsk(months, {"2025/12", "2026/01", "2026/10"}) == std::vector<std::string>{"2026/10"});
}

TEST_CASE("chess.com: the followed players the profile keeps") {
    const std::vector<chesscom::Followed> players{{"p1", "/data/Mine.fcgdb", {"2026/09", "2026/10"}},
                                                  {"p2", "/data/Other.fcgdb", {}}};
    CHECK(chesscom::fromJson(chesscom::toJson(players)) == players);
    CHECK(chesscom::fromJson("nonsense").empty());
}
