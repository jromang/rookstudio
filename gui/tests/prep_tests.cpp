// Preparing a game against an opponent (#52): the report from their games and the database's tree.
#include <doctest/doctest.hpp>

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <study/import.hpp>
#include <study/store.hpp>
#include <study/tree.hpp>

#include "board_text.hpp"
#include "prep.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

study::TreeBase baseOf(const std::string& name, const std::vector<std::string>& pgns) {
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    auto db = study::Database::create(dir);
    std::vector<study::ImportedGame> games;
    for (const auto& text : pgns) {
        study::ImportedGame g;
        REQUIRE(study::readImportedGame(text, g));
        games.push_back(std::move(g));
    }
    db->append(std::move(games), true);
    study::TreeProgress progress;
    std::atomic<bool> cancel{false};
    const auto built = study::buildTree(*db, *db->snapshot(), {}, progress, cancel);
    REQUIRE_MESSAGE(built.error.empty(), built.error);
    return {db->snapshot(), study::Tree::open(db->folder())};
}

void add(std::vector<std::string>& pgns, int times, const std::string& white, const std::string& black,
         const std::string& moves, const std::string& result = "1/2-1/2", const std::string& date = "2026.03.01") {
    for (int i = 0; i < times; ++i)
        pgns.push_back(fmt::format("[White \"{}\"]\n[Black \"{}\"]\n[Date \"{}\"]\n[Result \"{}\"]\n\n{} {}\n", white,
                                   black, date, result, moves, result));
}

study::Repertoire repertoireOf(const char* pgn, chess::Color side) {
    std::string error;
    const auto game = readPgnGame(pgn, error);
    REQUIRE(game);
    return study::Repertoire(GameTree{*game, {}}, side);
}

}  // namespace

TEST_CASE("prep: their games, those against the user, what they play, and the repertoires' gaps") {
    std::vector<std::string> pgns;
    add(pgns, 6, "Someone", "Opponent_A", "1. e4 c5");
    add(pgns, 2, "Someone", "Opponent_A", "1. e4 e5");
    add(pgns, 3, "Another", "Opponent_A", "1. d4 Nf6");
    add(pgns, 5, "Opponent_A", "Third", "1. e4 e6", "1/2-1/2", "2025.11.02");
    add(pgns, 2, "Opponent_A", "Third", "1. d4 d5");
    add(pgns, 2, "rs_me", "Opponent_A", "1. e4 c5", "1-0", "2026.10.12");  // the user won both
    add(pgns, 1, "Opponent_A", "rs_me", "1. e4 e6");                       // a draw
    add(pgns, 1, "Opponent_A", "rs_me", "1. d4 d5", "1-0");                // a loss
    add(pgns, 4, "Fourth", "Fifth", "1. e4 e5");                           // not theirs
    const auto base = baseOf("fcg-prep-report", pgns);

    std::vector<std::pair<std::string, study::Repertoire>> repertoires;
    repertoires.emplace_back("White", repertoireOf("1. e4 e5 2. Nf3 *", chess::Color::WHITE));
    repertoires.emplace_back("Black", repertoireOf("1. e4 c5 *", chess::Color::BLACK));
    const auto report = prepReport(base, "opponent_a", "RS_ME", repertoires);

    CHECK(report.games == 22);
    CHECK(report.first == study::packDate("2025.11.02"));
    CHECK(report.last == study::packDate("2026.10.12"));
    CHECK(report.won == 2);
    CHECK(report.drawn == 1);
    CHECK(report.lost == 1);
    CHECK(report.last_between == study::packDate("2026.10.12"));
    // With White: 1. e4 in 6 of their 9 games, then 1. d4.
    REQUIRE(report.with_white.size() == 2);
    CHECK(report.with_white[0].san == "e4");
    CHECK(report.with_white[0].games == 6);
    CHECK(report.with_white[0].share == doctest::Approx(6.0 / 9));
    CHECK(report.with_white[1].san == "d4");
    // With Black: against 1. e4, 1... c5 in 8 of 10; against 1. d4, 1... Nf6.
    REQUIRE(report.with_black.size() == 2);
    CHECK(report.with_black[0].san == "e4");
    REQUIRE_FALSE(report.with_black[0].replies.empty());
    CHECK(report.with_black[0].replies[0].san == "c5");
    CHECK(report.with_black[0].replies[0].share == doctest::Approx(0.8));
    CHECK(report.with_black[1].san == "d4");
    REQUIRE_FALSE(report.with_black[1].replies.empty());
    CHECK(report.with_black[1].replies[0].san == "Nf6");
    // The gaps: 1... c5 against the White repertoire (8 games), 1. d4 against the Black one (3).
    REQUIRE(report.gaps.size() == 2);
    CHECK(report.gaps[0].repertoire == "White");
    CHECK(report.gaps[0].gap.move == "c5");
    CHECK(report.gaps[0].gap.games == 8);
    CHECK(report.gaps[1].repertoire == "Black");
    CHECK(report.gaps[1].gap.move == "d4");
    CHECK(report.gaps[1].gap.line.empty());
    // Someone else: nothing of theirs.
    const auto none = prepReport(base, "Nobody", "", repertoires);
    CHECK(none.games == 0);
    CHECK(none.with_white.empty());
    CHECK(none.gaps.empty());
}
