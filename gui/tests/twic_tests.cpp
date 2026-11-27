// The Week in Chess (#71): its index read, the next issue's Monday, the report of the games added, the profile's state.
#include <doctest/doctest.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <study/import_into.hpp>
#include <study/store.hpp>

#include "twic.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

// An index page as the site writes it: a table of the issues, the newest first, with their links and counts.
const char* INDEX = R"(<html><body><h2>The Week in Chess 1671</h2>
<table class="results-table"><tr class="tabhead">
<tr class="tabhead2"><th>TWIC</th><th>Date</th><th>Read</th><th>PGN</th><th>CBV</th><th>Games</th><th>Stories</th></tr>
<tr>
<td>1671</td>
<td>2026-11-16</td>
<td><a href="https://theweekinchess.com/html/twic1671.html">HTML</a></td>
<td><a href="https://theweekinchess.com/zips/twic1671g.zip">PGN</a></td>
<td><a href="https://theweekinchess.com/zips/twic1671c6.zip">Chessbase</a></td>
<td>7379</td>
<td>35</td>
</tr>
<tr><td>1670</td><td>2026-11-09</td><td><a href="x">HTML</a></td><td><a href="https://theweekinchess.com/zips/twic1670g.zip">PGN</a></td><td><a href="y">Chessbase</a></td><td>8391</td><td>28</td></tr>
<tr><td>1669</td><td>2026-11-02</td><td>HTML</td><td>no PGN this week</td><td></td><td>10</td><td>1</td></tr>
<tr><td>Total</td><td>-</td><td></td><td></td><td></td><td>99</td><td></td></tr>
</table></body></html>)";

}  // namespace

TEST_CASE("TWIC: the issues of its index, the newest first, those with a PGN only") {
    const auto issues = twic::parseIndex(INDEX);
    REQUIRE(issues.size() == 2);
    CHECK(issues[0] == twic::Issue{1671, "2026-11-16", 7379});
    CHECK(issues[1] == twic::Issue{1670, "2026-11-09", 8391});
    CHECK(twic::parseIndex("<html>a page that changed</html>").empty());
    CHECK(twic::pgnUrl(1671) == "https://theweekinchess.com/zips/twic1671g.zip");
}

TEST_CASE("TWIC: the next issue's Monday") {
    CHECK(twic::nextMonday("2026-11-16") == "2026-11-23");
    CHECK(twic::nextMonday("2026-12-28") == "2027-01-04");
    CHECK(twic::nextMonday("2028-02-28") == "2028-03-06");
    CHECK(twic::nextMonday("16/11/2026").empty());
}

TEST_CASE("TWIC: the report of the games an issue added") {
    const auto dir = fs::temp_directory_path() / "twic-report";
    fs::remove_all(dir);
    auto db        = study::Database::create(dir / "TWIC.fcgdb");
    const auto pgn = dir / "issue.pgn";
    {
        std::ofstream out(pgn);
        const auto game = [&](const char* event, const char* date, const char* white, int we, const char* black,
                              int be) {
            out << "[Event \"" << event << "\"]\n[Site \"?\"]\n[Date \"" << date << "\"]\n[Round \"1\"]\n[White \""
                << white << "\"]\n[Black \"" << black << "\"]\n[Result \"1/2-1/2\"]\n";
            if (we) out << "[WhiteElo \"" << we << "\"]\n";
            if (be) out << "[BlackElo \"" << be << "\"]\n";
            out << "\n1. e4 e5 1/2-1/2\n\n";
        };
        game("Strong Rapid", "2026.11.09", "Player A", 2800, "Player B", 2750);
        game("Strong Rapid", "2026.11.12", "Player B", 2750, "Player C", 2700);
        game("Open", "2026.11.10", "Player D", 2300, "Player E", 0);
        game("Open", "2026.11.11", "Player E", 0, "Player F", 2100);
        game("Unrated", "2026.11.10", "Player G", 0, "Player H", 0);
    }
    study::ImportProgress progress;
    std::atomic<bool> cancel{false};
    const auto done = study::importInto(*db, pgn, {}, progress, cancel);
    REQUIRE(done.added == 5);
    const auto snapshot = db->snapshot();
    const auto report   = twic::reportOf(*snapshot, 0, 5, 2);
    REQUIRE(report.events.size() == 2);  // the unrated event left out
    CHECK(report.events[0].name == "Strong Rapid");
    CHECK(report.events[0].average == (2800 + 2750 + 2750 + 2700) / 4);
    CHECK(report.events[0].games == 2);
    CHECK(report.events[0].first == study::packDate("2026.11.09"));
    CHECK(report.events[0].last == study::packDate("2026.11.12"));
    CHECK(report.events[1].name == "Open");
    CHECK(report.events[1].average == (2300 + 2100) / 2);
    REQUIRE(report.players.size() == 2);
    CHECK(report.players[0].name == "Player A");
    CHECK(report.players[0].elo == 2800);
    CHECK(report.players[1].name == "Player B");
    CHECK(report.players[1].games == 2);
    // Only the games of the batch: the first two.
    const auto first = twic::reportOf(*snapshot, 0, 2);
    REQUIRE(first.events.size() == 1);
    CHECK(first.events[0].games == 2);
}

TEST_CASE("TWIC: the profile's state, the new issue and the weekly look") {
    twic::State s;
    s.weekly                    = true;
    s.database                  = "/data/TWIC.fcgdb";
    s.added["/data/TWIC.fcgdb"] = {1669, 1670};
    s.checked                   = 1000;
    s.newest                    = 1671;
    s.newest_games              = 7379;
    s.newest_date               = "2026-11-16";
    const auto back             = twic::stateFromJson(twic::toJson(s));
    CHECK(back == s);
    CHECK(twic::stateFromJson("not json") == twic::State{});
    const auto next = twic::newIssue(s, "/data/TWIC.fcgdb");
    REQUIRE(next);
    CHECK(next->number == 1671);
    CHECK(next->games == 7379);
    CHECK_FALSE(twic::newIssue(s, "/data/Other.fcgdb"));  // nothing added there: nothing offered
    s.added["/data/TWIC.fcgdb"].insert(1671);
    CHECK_FALSE(twic::newIssue(s, "/data/TWIC.fcgdb"));
    CHECK(twic::lookDue(s, 1000 + 6 * 3600));
    CHECK_FALSE(twic::lookDue(s, 1000 + 3600));
    s.weekly = false;
    CHECK_FALSE(twic::lookDue(s, 1000 + 7 * 24 * 3600));
}
