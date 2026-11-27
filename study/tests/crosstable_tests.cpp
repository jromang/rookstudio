// A tournament's crosstable and standings (#92, mockup M13, board 5): rounds read from Round, FIDE's tables and K, the
// tie-breaks of C.07 checked against tournaments worked by hand from FIDE's definitions (a round robin, a Swiss with a
// forfeit, byes and a withdrawal, direct encounters), a database's tournaments listed with their systems, and every
// figure of tournaments of our own compared with a count made apart (a forfeit, a missing game, a player under two
// names, games that came twice); steps of the crosstable's builder run alone. Players' names are invented, and so is
// the FIDE list.
#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <study/crosstable.hpp>
#include <study/crosstable_detail.hpp>
#include <study/fide_list.hpp>
#include <study/players.hpp>
#include <study/store.hpp>

#include "test_games.hpp"

using namespace fcg::study;
using namespace fcg::test;
namespace fs = std::filesystem;

namespace {

// A game's moves: the knights out and back, `plies` of them (none: a forfeit).
std::string dance(int plies) {
    static const char* const cycle[] = {"Nf3", "Nf6", "Ng1", "Ng8"};
    std::string out;
    for (int i = 0; i < plies; ++i) {
        if (i % 2 == 0) out += std::to_string(i / 2 + 1) + ". ";
        out += cycle[i % 4];
        out += ' ';
    }
    return out;
}

std::string resultText(char r) { return r == '1' ? "1-0" : r == '0' ? "0-1" : r == '=' ? "1/2-1/2" : "*"; }

struct G {
    std::string white, black, round;
    char result   = '=';
    int plies     = 40;
    int white_elo = 0, black_elo = 0;
    std::string event = "Masters Tournament 2026", site = "Leiden", date = "2026.04.10";
    std::vector<std::pair<std::string, std::string>> more;
};

ImportedGame make(const G& g) {
    std::string text = "[Event \"" + g.event + "\"]\n[Site \"" + g.site + "\"]\n[Date \"" + g.date + "\"]\n";
    if (!g.round.empty()) text += "[Round \"" + g.round + "\"]\n";
    text += "[White \"" + g.white + "\"]\n[Black \"" + g.black + "\"]\n[Result \"" + resultText(g.result) + "\"]\n";
    if (g.white_elo) text += "[WhiteElo \"" + std::to_string(g.white_elo) + "\"]\n";
    if (g.black_elo) text += "[BlackElo \"" + std::to_string(g.black_elo) + "\"]\n";
    for (const auto& [k, v] : g.more) text += "[" + k + " \"" + v + "\"]\n";
    text += "\n" + dance(g.plies) + resultText(g.result) + "\n";
    ImportedGame out;
    REQUIRE(readImportedGame(text, out));
    return out;
}

std::vector<ImportedGame> make(const std::vector<G>& games) {
    std::vector<ImportedGame> out;
    for (const auto& g : games) out.push_back(make(g));
    return out;
}

TournamentList listOf(const std::shared_ptr<const Snapshot>& snap) {
    TournamentProgress progress;
    std::atomic<bool> cancel{false};
    auto list = listTournaments(snap, progress, cancel, 2);
    REQUIRE_FALSE(list.cancelled);
    return list;
}

const TournamentEntry* entryOf(const TournamentList& list, const std::string& key) {
    for (const auto& e : list.tournaments)
        if (e.key == key) return &e;
    return nullptr;
}

// The crosstable of a database's tournament, as the window makes it.
Crosstable crosstableOf(const std::shared_ptr<const Snapshot>& snap, const TournamentEntry& entry,
                        CrossOptions options = {}, const FideList* list = nullptr) {
    std::atomic<bool> cancel{false};
    const auto ids = tournamentGames(*snap, entry.ref, cancel);
    auto in        = tournamentInput(*snap, ids, list);
    options.kind   = in.kind;
    options.year   = in.year;
    options.team   = in.team;
    return makeCrosstable(std::move(in.players), std::move(in.games), options);
}

// A player's row by name.
const CrossRow& row(const Crosstable& t, const std::string& name) {
    for (const auto& r : t.rows)
        if (t.players[r.player].name == name) return r;
    FAIL("no player " << name);
    return t.rows.front();
}

double tiebreak(const Crosstable& t, const std::string& name, Tiebreak which) {
    for (std::size_t k = 0; k < t.tiebreaks.size(); ++k)
        if (t.tiebreaks[k] == which) return row(t, name).tiebreaks[k];
    FAIL("no tie-break " << tiebreakCode(which));
    return 0;
}

std::vector<std::string> standings(const Crosstable& t) {
    std::vector<std::string> out;
    for (const auto& r : t.rows) out.push_back(t.players[r.player].name);
    return out;
}

const std::vector<Tiebreak> ALL = {Tiebreak::DirectEncounter,   Tiebreak::Wins,         Tiebreak::GamesWon,
                                   Tiebreak::BlackGames,        Tiebreak::BlackWins,    Tiebreak::Progressive,
                                   Tiebreak::Buchholz,          Tiebreak::BuchholzCut1, Tiebreak::BuchholzMedian1,
                                   Tiebreak::SonnebornBerger,   Tiebreak::Koya,         Tiebreak::AverageRating,
                                   Tiebreak::AverageRatingCut1, Tiebreak::Performance};

// The round robin worked by hand: five players, ten games, each round one player resting.
std::vector<G> roundRobinOfFive() {
    const auto g = [](const char* w, const char* b, const char* round, char result) {
        static const std::map<std::string, int> elo{{"Kovalenko, Petro", 2450},
                                                    {"Okafor, Daniel", 2400},
                                                    {"Navarro, Lucia", 2350},
                                                    {"Petersen, Mads", 2300},
                                                    {"Kim, Seo-yeon", 2250}};
        G out;
        out.white     = w;
        out.black     = b;
        out.round     = round;
        out.result    = result;
        out.white_elo = elo.at(w);
        out.black_elo = elo.at(b);
        return out;
    };
    return {g("Okafor, Daniel", "Kim, Seo-yeon", "1.1", '1'),    g("Navarro, Lucia", "Petersen, Mads", "1.2", '='),
            g("Kovalenko, Petro", "Navarro, Lucia", "2.1", '1'), g("Petersen, Mads", "Kim, Seo-yeon", "2.2", '0'),
            g("Kovalenko, Petro", "Petersen, Mads", "3.1", '0'), g("Okafor, Daniel", "Navarro, Lucia", "3.2", '0'),
            g("Kim, Seo-yeon", "Kovalenko, Petro", "4.1", '0'),  g("Petersen, Mads", "Okafor, Daniel", "4.2", '0'),
            g("Kovalenko, Petro", "Okafor, Daniel", "5.1", '='), g("Navarro, Lucia", "Kim, Seo-yeon", "5.2", '1')};
}

}  // namespace

TEST_CASE("crosstable: rounds read from Round, FIDE's tables of differences and probabilities, its K") {
    CHECK(readRound("9.2") == RoundBoard{9, 2});
    CHECK(readRound("9") == RoundBoard{9, 0});
    CHECK(readRound(" 12.10") == RoundBoard{12, 10});
    CHECK(readRound("1.1.3") == RoundBoard{1, 1});
    CHECK(readRound("?") == RoundBoard{});
    CHECK(readRound("-") == RoundBoard{});
    CHECK(readRound("") == RoundBoard{});
    CHECK(readRound("Final") == RoundBoard{});
    CHECK(readRound("0") == RoundBoard{});

    // B.02 8.1.1: the fractional score rounded to the hundredth.
    CHECK(fideRatingDifference(1.0) == 800);
    CHECK(fideRatingDifference(0.0) == -800);
    CHECK(fideRatingDifference(0.5) == 0);
    CHECK(fideRatingDifference(4.5 / 7) == 102);  // .64
    CHECK(fideRatingDifference(0.36) == -102);
    CHECK(fideRatingDifference(0.99) == 677);
    CHECK(fideRatingDifference(2.0 / 3) == 125);  // .6667, rounded .67
    CHECK(fideRatingDifference(0.83) == 273);
    CHECK(fideRatingDifference(0.17) == -273);
    // B.02 8.1.2: the higher rated player's probability, the lower's its complement.
    CHECK(fideExpectedScore(0) == doctest::Approx(0.50));
    CHECK(fideExpectedScore(3) == doctest::Approx(0.50));
    CHECK(fideExpectedScore(4) == doctest::Approx(0.51));
    CHECK(fideExpectedScore(-4) == doctest::Approx(0.49));
    CHECK(fideExpectedScore(100) == doctest::Approx(0.64));
    CHECK(fideExpectedScore(-100) == doctest::Approx(0.36));
    CHECK(fideExpectedScore(400) == doctest::Approx(0.92));
    CHECK(fideExpectedScore(735) == doctest::Approx(0.99));
    CHECK(fideExpectedScore(736) == doctest::Approx(1.00));
    CHECK(fideExpectedScore(-900) == doctest::Approx(0.00));
    // B.02 8.3.3.
    CHECK(fideK(2450, 0, 2026, 0, 9) == 10);
    CHECK(fideK(2399, 0, 2026, 0, 9) == 20);
    CHECK(fideK(2250, 2008, 2026, 0, 9) == 40);  // the year of the 18th birthday
    CHECK(fideK(2250, 2007, 2026, 0, 9) == 20);
    CHECK(fideK(2310, 2010, 2026, 0, 9) == 20);   // 2300 or more
    CHECK(fideK(2250, 0, 2026, 40, 9) == 40);     // FIDE's list's
    CHECK(fideK(2250, 2010, 2026, 0, 21) == 33);  // 40 × 21 > 700
    CHECK(fideK(2250, 0, 2026, 0, 40) == 17);

    for (std::size_t i = 0; i < TIEBREAKS; ++i) {
        const auto t = static_cast<Tiebreak>(i);
        CHECK(tiebreakFromCode(tiebreakCode(t)) == t);
    }
    CHECK_FALSE(tiebreakFromCode("XYZ"));
    CHECK(fideTiebreaks(TournamentSystem::RoundRobin, true) ==
          std::vector<Tiebreak>{Tiebreak::SonnebornBerger, Tiebreak::Wins, Tiebreak::DirectEncounter});
    CHECK(fideTiebreaks(TournamentSystem::Swiss, true) ==
          std::vector<Tiebreak>{Tiebreak::AverageRatingCut1, Tiebreak::BuchholzCut1, Tiebreak::Buchholz,
                                Tiebreak::DirectEncounter});
    CHECK(fideTiebreaks(TournamentSystem::Swiss, false) ==
          std::vector<Tiebreak>{Tiebreak::BuchholzCut1, Tiebreak::Buchholz, Tiebreak::DirectEncounter});
    CHECK(fideTiebreaks(TournamentSystem::Team, true).empty());
    CHECK(byeName("BYE"));
    CHECK(byeName("bye"));
    CHECK(byeName("-"));
    CHECK(byeName("?"));
    CHECK_FALSE(byeName("Byers, Tom"));
    CHECK(eventKey("Autumn Open 2026") == "autumn open");
    CHECK(eventKey("Autumn  Open") == "autumn open");
    CHECK(eventKey("Hastings Masters 2025/26") == "hastings masters");
    CHECK(eventKey("57th Hastings Masters") == "57th hastings masters");
    CHECK(eventKey("Championnat de Lyon, 2026") == "championnat de lyon");
}

TEST_CASE("crosstable: a round robin's tie-breaks, performance and rating change, worked by hand") {
    auto db         = database("ct-rr5", make(roundRobinOfFive()));
    const auto snap = db->snapshot();
    const auto list = listOf(snap);
    REQUIRE(list.tournaments.size() == 1);
    const auto& e = list.tournaments[0];
    CHECK(e.key == "masters tournament|2026");
    CHECK(e.shape.system == SystemChoice{TournamentSystem::RoundRobin, 1});
    CHECK_FALSE(e.shape.uncertain);
    CHECK(e.shape.players == 5);
    CHECK(e.shape.rounds == 5);
    CHECK(e.shape.games == 10);
    CHECK(e.shape.expected == 10);
    CHECK(e.mean_elo == 2350);  // (2450 + 2400 + 2350 + 2300 + 2250) / 5

    CrossOptions options;
    options.order = ALL;
    const auto t  = crosstableOf(snap, e, options);
    // The points: Kovalenko, Okafor and Navarro 2½, Petersen 1½, Kim 1.
    CHECK(row(t, "Kovalenko, Petro").points == 2.5);
    CHECK(row(t, "Okafor, Daniel").points == 2.5);
    CHECK(row(t, "Navarro, Lucia").points == 2.5);
    CHECK(row(t, "Petersen, Mads").points == 1.5);
    CHECK(row(t, "Kim, Seo-yeon").points == 1.0);
    // Sonneborn-Berger (9.1): each opponent's final score times the points scored against them.
    // Kovalenko: Okafor 2.5 × ½ + Navarro 2.5 × 1 + Petersen 1.5 × 0 + Kim 1 × 1 = 4.75.
    CHECK(tiebreak(t, "Kovalenko, Petro", Tiebreak::SonnebornBerger) == 4.75);
    // Okafor: Kovalenko 2.5 × ½ + Navarro × 0 + Petersen 1.5 × 1 + Kim 1 × 1 = 3.75.
    CHECK(tiebreak(t, "Okafor, Daniel", Tiebreak::SonnebornBerger) == 3.75);
    // Navarro: Kovalenko × 0 + Okafor 2.5 × 1 + Petersen 1.5 × ½ + Kim 1 × 1 = 4.25.
    CHECK(tiebreak(t, "Navarro, Lucia", Tiebreak::SonnebornBerger) == 4.25);
    // Petersen: Kovalenko 2.5 × 1 + Navarro 2.5 × ½ = 3.75; Kim: Petersen 1.5 × 1 = 1.5.
    CHECK(tiebreak(t, "Petersen, Mads", Tiebreak::SonnebornBerger) == 3.75);
    CHECK(tiebreak(t, "Kim, Seo-yeon", Tiebreak::SonnebornBerger) == 1.5);
    // Wins (7.1): two each for the three leaders, one for the others.
    CHECK(tiebreak(t, "Kovalenko, Petro", Tiebreak::Wins) == 2);
    CHECK(tiebreak(t, "Petersen, Mads", Tiebreak::Wins) == 1);
    CHECK(tiebreak(t, "Kim, Seo-yeon", Tiebreak::GamesWon) == 1);
    // Koya (9.2): against those with 2 points of 4 or more (the three leaders).
    CHECK(tiebreak(t, "Kovalenko, Petro", Tiebreak::Koya) == 1.5);  // ½ + 1
    CHECK(tiebreak(t, "Okafor, Daniel", Tiebreak::Koya) == 0.5);
    CHECK(tiebreak(t, "Navarro, Lucia", Tiebreak::Koya) == 1.0);
    CHECK(tiebreak(t, "Petersen, Mads", Tiebreak::Koya) == 1.5);  // 1 + 0 + ½
    CHECK(tiebreak(t, "Kim, Seo-yeon", Tiebreak::Koya) == 0);
    // Games with Black (7.3), won with Black (7.4).
    CHECK(tiebreak(t, "Kovalenko, Petro", Tiebreak::BlackGames) == 1);
    CHECK(tiebreak(t, "Okafor, Daniel", Tiebreak::BlackGames) == 2);
    CHECK(tiebreak(t, "Kim, Seo-yeon", Tiebreak::BlackGames) == 3);
    CHECK(tiebreak(t, "Kovalenko, Petro", Tiebreak::BlackWins) == 1);
    CHECK(tiebreak(t, "Kim, Seo-yeon", Tiebreak::BlackWins) == 1);
    // Progressive scores (7.5), the rounds without a game counted at the score before.
    // Kovalenko 0 + 1 + 1 + 2 + 2.5; Okafor 1 + 1 + 1 + 2 + 2.5; Navarro .5 + .5 + 1.5 + 1.5 + 2.5;
    // Petersen .5 + .5 + 1.5 + 1.5 + 1.5; Kim 0 + 1 + 1 + 1 + 1.
    CHECK(tiebreak(t, "Kovalenko, Petro", Tiebreak::Progressive) == 6.5);
    CHECK(tiebreak(t, "Okafor, Daniel", Tiebreak::Progressive) == 7.5);
    CHECK(tiebreak(t, "Navarro, Lucia", Tiebreak::Progressive) == 6.5);
    CHECK(tiebreak(t, "Petersen, Mads", Tiebreak::Progressive) == 5.5);
    CHECK(tiebreak(t, "Kim, Seo-yeon", Tiebreak::Progressive) == 4.0);
    // Average rating of the opponents (10.1), 0.5 rounded up; cut 1 (14.1) without its lowest.
    CHECK(tiebreak(t, "Kovalenko, Petro", Tiebreak::AverageRating) == 2325);      // 9300 / 4
    CHECK(tiebreak(t, "Okafor, Daniel", Tiebreak::AverageRating) == 2338);        // 9350 / 4 = 2337.5
    CHECK(tiebreak(t, "Petersen, Mads", Tiebreak::AverageRating) == 2363);        // 9450 / 4 = 2362.5
    CHECK(tiebreak(t, "Kovalenko, Petro", Tiebreak::AverageRatingCut1) == 2350);  // 7050 / 3
    CHECK(tiebreak(t, "Okafor, Daniel", Tiebreak::AverageRatingCut1) == 2367);    // 7100 / 3
    CHECK(tiebreak(t, "Kim, Seo-yeon", Tiebreak::AverageRatingCut1) == 2400);     // 7200 / 3
    // The performance (10.2): 2.5 / 4 = .625, rounded .63, +95; 1.5 / 4 = .375, rounded .38, −87; .25, −193.
    CHECK(row(t, "Kovalenko, Petro").performance == 2420);
    CHECK(row(t, "Okafor, Daniel").performance == 2433);
    CHECK(row(t, "Navarro, Lucia").performance == 2445);
    CHECK(row(t, "Petersen, Mads").performance == 2276);
    CHECK(row(t, "Kim, Seo-yeon").performance == 2182);
    CHECK(tiebreak(t, "Navarro, Lucia", Tiebreak::Performance) == 2445);
    // The rating change (B.02 8.3): Kovalenko (K 10) −.07 + .36 − .70 + .24 = −.17; Okafor (K 10) +.07 − .57 + .36
    // + .30 = .16; Navarro (K 20) −.36 + .57 − .07 + .36 = .50; Petersen (K 20) .70 − .36 + .07 − .57 = −.16; Kim
    // (K 20) −.24 − .30 − .36 + .57 = −.33.
    CHECK(*row(t, "Kovalenko, Petro").change == doctest::Approx(-1.7));
    CHECK(row(t, "Kovalenko, Petro").k == 10);
    CHECK(*row(t, "Okafor, Daniel").change == doctest::Approx(1.6));
    CHECK(*row(t, "Navarro, Lucia").change == doctest::Approx(10.0));
    CHECK(*row(t, "Petersen, Mads").change == doctest::Approx(-3.2));
    CHECK(*row(t, "Kim, Seo-yeon").change == doctest::Approx(-6.6));
    CHECK(row(t, "Kim, Seo-yeon").rated_games == 4);
    CHECK(row(t, "Kim, Seo-yeon").expected == doctest::Approx(1.33));

    // FIDE's order for a round robin: Sonneborn-Berger first.
    const auto fide = crosstableOf(snap, e);
    CHECK(fide.fide_order);
    CHECK(fide.tiebreaks ==
          std::vector<Tiebreak>{Tiebreak::SonnebornBerger, Tiebreak::Wins, Tiebreak::DirectEncounter});
    CHECK(standings(fide) == std::vector<std::string>{"Kovalenko, Petro", "Navarro, Lucia", "Okafor, Daniel",
                                                      "Petersen, Mads", "Kim, Seo-yeon"});
    CHECK(fide.rows[0].place_from == 1);
    CHECK(fide.rows[2].place_to == 3);
    // Koya first; then the direct encounter alone (6.2: all met, Kovalenko 1½, Navarro 1, Okafor ½).
    options.order = std::vector<Tiebreak>{Tiebreak::Koya};
    CHECK(standings(crosstableOf(snap, e, options))[0] == "Kovalenko, Petro");
    options.order = std::vector<Tiebreak>{Tiebreak::DirectEncounter};
    const auto de = crosstableOf(snap, e, options);
    CHECK(standings(de) == std::vector<std::string>{"Kovalenko, Petro", "Navarro, Lucia", "Okafor, Daniel",
                                                    "Petersen, Mads", "Kim, Seo-yeon"});
    CHECK(tiebreak(de, "Kovalenko, Petro", Tiebreak::DirectEncounter) == 1.5);
    CHECK(tiebreak(de, "Okafor, Daniel", Tiebreak::DirectEncounter) == 0.5);
    CHECK(row(de, "Petersen, Mads").states[0] == TiebreakState::None);  // in no tie
    // Without tie-breaks, the tie stays: places 1 to 3 shared.
    options.order   = std::vector<Tiebreak>{};
    const auto none = crosstableOf(snap, e, options);
    CHECK(none.rows[0].place_from == 1);
    CHECK(none.rows[0].place_to == 3);
    CHECK(none.rows[3].place_from == 4);

    // The cells: each opponent once, colours and points.
    const auto& kov = row(t, "Kovalenko, Petro");
    REQUIRE(kov.entries.size() == 4);
    CHECK(kov.entries[0].round == 2);
    CHECK(kov.entries[0].white);
    CHECK(kov.entries[0].points == 1);
    CHECK(t.players[kov.entries[0].opponent].name == "Navarro, Lucia");
    CHECK(kov.entries[2].round == 4);
    CHECK_FALSE(kov.entries[2].white);
    CHECK(t.missing.empty());
    CHECK(t.forfeits.empty());
    CHECK(t.two_names.empty());
}

TEST_CASE("crosstable: a Swiss's unplayed rounds (a forfeit, byes, a withdrawal) in its tie-breaks, worked by hand") {
    // Six players, four rounds. Round 1: Moreau–Duval 1-0, Ito–Brennan 0-1, Haddad–Sato won by forfeit (no move).
    // Round 2: Brennan–Moreau ½, Duval–Haddad 0-1, Sato–Ito 1-0. Sato withdraws. Round 3: Moreau–Haddad 1-0,
    // Ito–Duval ½, Brennan alone (a pairing-allocated bye). Round 4: Haddad–Brennan 0-1, Moreau–Ito 1-0, Duval alone.
    const std::map<std::string, int> elo{{"Moreau, Claire", 2300}, {"Brennan, Liam", 2200}, {"Haddad, Samir", 2100},
                                         {"Duval, Anne", 2000},    {"Ito, Yuki", 1900},     {"Sato, Ken", 1800}};
    const auto g = [&](const char* w, const char* b, const char* round, char result, int plies = 40) {
        G out;
        out.event     = "Autumn Open 2026";
        out.site      = "Lyon";
        out.date      = "2026.10.0" + std::string(1, round[0]);
        out.white     = w;
        out.black     = b;
        out.round     = round;
        out.result    = result;
        out.plies     = plies;
        out.white_elo = elo.at(w);
        out.black_elo = elo.at(b);
        return out;
    };
    auto db         = database("ct-swiss6", make(std::vector<G>{
                                                g("Moreau, Claire", "Duval, Anne", "1.1", '1'),
                                                g("Ito, Yuki", "Brennan, Liam", "1.2", '0'),
                                                g("Haddad, Samir", "Sato, Ken", "1.3", '1', 0),
                                                g("Brennan, Liam", "Moreau, Claire", "2.1", '='),
                                                g("Duval, Anne", "Haddad, Samir", "2.2", '0'),
                                                g("Sato, Ken", "Ito, Yuki", "2.3", '1'),
                                                g("Moreau, Claire", "Haddad, Samir", "3.1", '1'),
                                                g("Ito, Yuki", "Duval, Anne", "3.2", '='),
                                                g("Haddad, Samir", "Brennan, Liam", "4.1", '0'),
                                                g("Moreau, Claire", "Ito, Yuki", "4.2", '1'),
                                            }));
    const auto snap = db->snapshot();
    const auto list = listOf(snap);
    REQUIRE(list.tournaments.size() == 1);
    const auto& e = list.tournaments[0];
    CHECK(e.shape.system.system == TournamentSystem::Swiss);
    CHECK(e.shape.rounds == 4);
    CHECK(e.shape.games == 10);
    CHECK(e.shape.expected == 10);  // 3 + 3 + 2 + 2: five players after the withdrawal

    CrossOptions options;
    options.order = ALL;
    const auto t  = crosstableOf(snap, e, options);
    CHECK(t.system.system == TournamentSystem::Swiss);
    CHECK(row(t, "Moreau, Claire").points == 3.5);
    CHECK(row(t, "Brennan, Liam").points == 3.5);  // with the bye's point
    CHECK(row(t, "Haddad, Samir").points == 2.0);  // with the forfeit's
    CHECK(row(t, "Duval, Anne").points == 1.5);
    CHECK(row(t, "Ito, Yuki").points == 0.5);
    CHECK(row(t, "Sato, Ken").points == 1.0);
    REQUIRE(t.forfeits.size() == 1);
    CHECK(t.missing.empty());
    // Each round's entry: a game, a forfeit, a bye, an absence after the withdrawal.
    const auto& brennan = row(t, "Brennan, Liam");
    REQUIRE(brennan.entries.size() == 4);
    CHECK(brennan.entries[2].kind == CellKind::Bye);
    CHECK(brennan.entries[2].points == 1);
    const auto& haddad = row(t, "Haddad, Samir");
    CHECK(haddad.entries[0].kind == CellKind::Forfeit);
    CHECK(haddad.entries[0].white);
    const auto& sato = row(t, "Sato, Ken");
    REQUIRE(sato.entries.size() == 4);
    CHECK(sato.entries[0].kind == CellKind::Forfeit);
    CHECK(sato.entries[2].kind == CellKind::Absent);
    CHECK(sato.entries[3].kind == CellKind::Absent);
    CHECK(sato.entries[3].known);
    CHECK(row(t, "Duval, Anne").entries[3].kind == CellKind::Bye);

    // Buchholz (8.1, 16.3, 16.4): Sato's score adjusted for his opponents, his two rounds after withdrawing counted
    // as draws: 1 + ½ + ½ = 2. Each unplayed round of a player against a dummy of their own score, capped by a
    // forfeit's scheduled opponent's adjusted score, else by ½ × 4 rounds = 2.
    // Moreau: Duval 1.5 + Brennan 3.5 + Haddad 2 + Ito .5 = 7.5.
    CHECK(tiebreak(t, "Moreau, Claire", Tiebreak::Buchholz) == 7.5);
    // Brennan: Ito .5 + Moreau 3.5 + the bye's dummy min(3.5, 2) = 2 + Haddad 2 = 8.
    CHECK(tiebreak(t, "Brennan, Liam", Tiebreak::Buchholz) == 8.0);
    // Haddad: the forfeit's dummy min(2, Sato's 2) = 2 + Duval 1.5 + Moreau 3.5 + Brennan 3.5 = 10.5.
    CHECK(tiebreak(t, "Haddad, Samir", Tiebreak::Buchholz) == 10.5);
    // Duval: Moreau 3.5 + Haddad 2 + Ito .5 + the bye's dummy min(1.5, 2) = 1.5 → 7.5.
    CHECK(tiebreak(t, "Duval, Anne", Tiebreak::Buchholz) == 7.5);
    // Ito: Brennan 3.5 + Sato (adjusted) 2 + Duval 1.5 + Moreau 3.5 = 10.5.
    CHECK(tiebreak(t, "Ito, Yuki", Tiebreak::Buchholz) == 10.5);
    // Sato: the forfeit lost's dummy min(1, Haddad's 2) = 1 + Ito .5 + two absences' dummies min(1, 2) = 1 each: 3.5.
    CHECK(tiebreak(t, "Sato, Ken", Tiebreak::Buchholz) == 3.5);
    // Cut 1 (14.1, 16.5): the lowest value, a voluntary unplayed round's first. Moreau 7.5 − .5; Brennan 8 − .5 (a
    // pairing-allocated bye is no VUR); Haddad 10.5 − 1.5; Sato 3.5 − 1 (a VUR's 1, not the least value .5).
    CHECK(tiebreak(t, "Moreau, Claire", Tiebreak::BuchholzCut1) == 7.0);
    CHECK(tiebreak(t, "Brennan, Liam", Tiebreak::BuchholzCut1) == 7.5);
    CHECK(tiebreak(t, "Haddad, Samir", Tiebreak::BuchholzCut1) == 9.0);
    CHECK(tiebreak(t, "Sato, Ken", Tiebreak::BuchholzCut1) == 2.5);
    // Median 1 (14.3): then the highest too. Moreau 7 − 3.5; Brennan 7.5 − 3.5; Sato 2.5 − 1.
    CHECK(tiebreak(t, "Moreau, Claire", Tiebreak::BuchholzMedian1) == 3.5);
    CHECK(tiebreak(t, "Brennan, Liam", Tiebreak::BuchholzMedian1) == 4.0);
    CHECK(tiebreak(t, "Sato, Ken", Tiebreak::BuchholzMedian1) == 1.5);
    // Sonneborn-Berger (9.1, 16.4). Moreau 1.5 × 1 + 3.5 × ½ + 2 × 1 + .5 × 1 = 5.75; Brennan .5 + 1.75 + the bye's
    // 2 × 1 + 2 = 6.25; Haddad the forfeit's 2 × 1 + 1.5 = 3.5; Duval .5 × ½ + the bye's 1.5 = 1.75; Ito 1.5 × ½
    // = .75; Sato .5 × 1 = .5.
    CHECK(tiebreak(t, "Moreau, Claire", Tiebreak::SonnebornBerger) == 5.75);
    CHECK(tiebreak(t, "Brennan, Liam", Tiebreak::SonnebornBerger) == 6.25);
    CHECK(tiebreak(t, "Haddad, Samir", Tiebreak::SonnebornBerger) == 3.5);
    CHECK(tiebreak(t, "Duval, Anne", Tiebreak::SonnebornBerger) == 1.75);
    CHECK(tiebreak(t, "Ito, Yuki", Tiebreak::SonnebornBerger) == 0.75);
    CHECK(tiebreak(t, "Sato, Ken", Tiebreak::SonnebornBerger) == 0.5);
    // Wins (7.1) with the bye and the forfeit; games won (7.2) over the board only.
    CHECK(tiebreak(t, "Brennan, Liam", Tiebreak::Wins) == 3);
    CHECK(tiebreak(t, "Brennan, Liam", Tiebreak::GamesWon) == 2);
    CHECK(tiebreak(t, "Haddad, Samir", Tiebreak::Wins) == 2);
    CHECK(tiebreak(t, "Haddad, Samir", Tiebreak::GamesWon) == 1);
    // ARO cut 1 (as FIDE's Grand Swiss: a player with an unplayed round cuts nothing more). Moreau (2000 + 2200 +
    // 2100) / 3 without Ito's 1900; Brennan, a bye: (1900 + 2300 + 2100) / 3.
    CHECK(tiebreak(t, "Moreau, Claire", Tiebreak::AverageRatingCut1) == 2100);
    CHECK(tiebreak(t, "Brennan, Liam", Tiebreak::AverageRatingCut1) == 2100);
    // The performance over the board: Brennan (1900 + 2300 + 2100) / 3 = 2100, 2.5 / 3 = .83 → +273; Moreau 8200 / 4
    // = 2050, .875 → .88 → +336; Haddad without the forfeit (2000 + 2300 + 2200) / 3 = 2166.67 → 2167, .33 → −125.
    CHECK(row(t, "Brennan, Liam").performance == 2373);
    CHECK(row(t, "Moreau, Claire").performance == 2386);
    CHECK(row(t, "Haddad, Samir").performance == 2042);
    CHECK(row(t, "Sato, Ken").performance == 2700);  // one game won: +800
    // The rating change without the forfeit: Haddad (K 20) .36 − .24 − .36 = −.24; Sato +.64.
    CHECK(*row(t, "Haddad, Samir").change == doctest::Approx(-4.8));
    CHECK(row(t, "Haddad, Samir").rated_games == 3);
    CHECK(*row(t, "Sato, Ken").change == doctest::Approx(12.8));

    // FIDE's order for a Swiss, all rated: ARO-C1 ties Moreau and Brennan (2100), Buchholz cut 1 puts Brennan first.
    const auto fide = crosstableOf(snap, e);
    CHECK(fide.tiebreaks.front() == Tiebreak::AverageRatingCut1);
    CHECK(standings(fide) == std::vector<std::string>{"Brennan, Liam", "Moreau, Claire", "Haddad, Samir", "Duval, Anne",
                                                      "Sato, Ken", "Ito, Yuki"});
    // Start numbers by rating.
    CHECK(row(fide, "Moreau, Claire").start == 1);
    CHECK(row(fide, "Sato, Ken").start == 6);
}

TEST_CASE(
    "crosstable: direct encounter among tied players, applied again to a smaller tie, and in a Swiss not all met") {
    // A round robin of six, four tied on 2½: among them Amon and Berg 2½ each, Cruz and Dahl ½ each (6.1); the
    // encounter applied again to each pair (6.2) leaves both ties.
    const auto g = [](const char* w, const char* b, char result, int round) {
        G out;
        out.event  = "Club Championship 2026";
        out.site   = "Lyon";
        out.white  = w;
        out.black  = b;
        out.result = result;
        out.round  = std::to_string(round);
        return out;
    };
    auto db = database("ct-de", make(std::vector<G>{
                                    g("Amon, Rui", "Cruz, Ines", '1', 1),
                                    g("Berg, Ola", "Dahl, Siv", '1', 1),
                                    g("Eide, Tor", "Falk, Ida", '1', 1),
                                    g("Amon, Rui", "Dahl, Siv", '1', 2),
                                    g("Berg, Ola", "Cruz, Ines", '1', 2),
                                    g("Falk, Ida", "Eide, Tor", '0', 6),
                                    g("Amon, Rui", "Berg, Ola", '=', 3),
                                    g("Cruz, Ines", "Dahl, Siv", '=', 3),
                                    g("Eide, Tor", "Amon, Rui", '1', 4),
                                    g("Falk, Ida", "Berg, Ola", '1', 4),
                                    g("Cruz, Ines", "Eide, Tor", '1', 5),
                                    g("Dahl, Siv", "Falk, Ida", '1', 5),
                                    g("Falk, Ida", "Amon, Rui", '1', 6),
                                    g("Eide, Tor", "Berg, Ola", '1', 7),
                                    g("Cruz, Ines", "Falk, Ida", '1', 7),
                                    g("Dahl, Siv", "Eide, Tor", '1', 8),
                                }));
    // (Eide and Falk met twice: a pair met more often than the round robin lets it, its system not sure.)
    auto snap = db->snapshot();
    auto list = listOf(snap);
    REQUIRE(list.tournaments.size() == 1);
    CrossOptions options;
    options.system = SystemChoice{TournamentSystem::RoundRobin, 1};
    options.order  = std::vector<Tiebreak>{Tiebreak::DirectEncounter};
    auto t         = crosstableOf(snap, list.tournaments[0], options);
    CHECK(list.tournaments[0].shape.uncertain);
    CHECK(list.tournaments[0].shape.over_met == 1);
    // Points: Eide 3 + ...; the four tied.
    CHECK(row(t, "Amon, Rui").points == 2.5);
    CHECK(row(t, "Berg, Ola").points == 2.5);
    CHECK(row(t, "Cruz, Ines").points == 2.5);
    CHECK(row(t, "Dahl, Siv").points == 2.5);
    CHECK(tiebreak(t, "Amon, Rui", Tiebreak::DirectEncounter) == 2.5);
    CHECK(tiebreak(t, "Cruz, Ines", Tiebreak::DirectEncounter) == 0.5);
    const auto places = [&](const std::string& name) {
        const auto& r = row(t, name);
        return std::make_pair(r.place_from, r.place_to);
    };
    const int top = row(t, "Amon, Rui").place_from;
    CHECK(places("Amon, Rui") == std::make_pair(top, top + 1));
    CHECK(places("Berg, Ola") == std::make_pair(top, top + 1));
    CHECK(places("Cruz, Ines") == std::make_pair(top + 2, top + 3));
    CHECK(places("Dahl, Siv") == std::make_pair(top + 2, top + 3));

    // A Swiss: Xu beat Yeo and Zane, who never met. Xu alone at the top whatever their game would have given (6.3);
    // then Yeo and Zane, not met, stay tied.
    const auto s = [](const char* w, const char* b, char result, int round) {
        G out;
        out.event  = "Spring Swiss 2026";
        out.site   = "Brest";
        out.white  = w;
        out.black  = b;
        out.result = result;
        out.round  = std::to_string(round);
        return out;
    };
    db   = database("ct-de-swiss", make(std::vector<G>{
                                       s("Xu, Lin", "Yeo, Mei", '1', 1),
                                       s("Zane, Kit", "Abel, Jon", '1', 1),
                                       s("Bodo, Ana", "Cole, Eva", '=', 1),
                                       s("Xu, Lin", "Zane, Kit", '1', 2),
                                       s("Yeo, Mei", "Bodo, Ana", '1', 2),
                                       s("Abel, Jon", "Cole, Eva", '=', 2),
                                       s("Abel, Jon", "Xu, Lin", '1', 3),
                                       s("Yeo, Mei", "Cole, Eva", '1', 3),
                                       s("Zane, Kit", "Bodo, Ana", '1', 3),
                                   }));
    snap = db->snapshot();
    list = listOf(snap);
    REQUIRE(list.tournaments.size() == 1);
    options.system = SystemChoice{TournamentSystem::Swiss, 1};
    t              = crosstableOf(snap, list.tournaments[0], options);
    CHECK(row(t, "Xu, Lin").points == 2);
    CHECK(row(t, "Yeo, Mei").points == 2);
    CHECK(row(t, "Zane, Kit").points == 2);
    CHECK(row(t, "Xu, Lin").place_from == 1);
    CHECK(row(t, "Xu, Lin").place_to == 1);
    CHECK(row(t, "Yeo, Mei").place_from == 2);
    CHECK(row(t, "Yeo, Mei").place_to == 3);
    CHECK(row(t, "Zane, Kit").place_to == 3);
    // As a round robin, not all met: no one ranked by the encounter (6.2 asks all of them met).
    options.system = SystemChoice{TournamentSystem::RoundRobin, 1};
    t              = crosstableOf(snap, list.tournaments[0], options);
    CHECK(row(t, "Xu, Lin").place_to == 3);
}

TEST_CASE("crosstable: a database's tournaments, their systems, players, rounds, games and mean Elo") {
    std::vector<G> games;
    // A round robin of four (six games) of an event, site and date.
    const auto robin = [&](const std::vector<std::string>& players, const std::string& event, const std::string& site,
                           const std::vector<std::string>& dates, int base_elo, int cycles = 1) {
        // The circle method: rounds of pairs.
        std::vector<std::string> p = players;
        if (p.size() % 2) p.push_back("");
        const auto n = p.size();
        int round    = 0;
        for (int c = 0; c < cycles; ++c)
            for (std::size_t r = 0; r + 1 < n; ++r) {
                ++round;
                for (std::size_t k = 0; k < n / 2; ++k) {
                    auto a = p[k], b = p[n - 1 - k];
                    if (a.empty() || b.empty()) continue;
                    if ((r + k + c) % 2) std::swap(a, b);
                    G x;
                    x.event       = event;
                    x.site        = site;
                    x.date        = dates[static_cast<std::size_t>(round - 1) % dates.size()];
                    x.round       = std::to_string(round) + "." + std::to_string(k + 1);
                    x.white       = a;
                    x.black       = b;
                    x.result      = "10="[(round + static_cast<int>(k)) % 3];
                    const auto at = [&](const std::string& who) {
                        return base_elo -
                               10 * static_cast<int>(std::find(players.begin(), players.end(), who) - players.begin());
                    };
                    x.white_elo = at(a);
                    x.black_elo = at(b);
                    games.push_back(x);
                }
                std::rotate(p.begin() + 1, p.end() - 1, p.end());
            }
    };
    const std::vector<std::string> eight = {"Kovalenko, Petro", "Okafor, Daniel", "Velikanov, Arseni", "Navarro, Lucia",
                                            "Petersen, Mads",   "Kim, Seo-yeon",  "Dubois, Hugo",      "Duval, Anne"};
    robin(eight, "Masters Tournament 2026", "Leiden", {"2026.04.02"}, 2600);
    const std::vector<std::string> six = {"Arslan, Deniz", "Borg, Ingrid",   "Castro, Mateo",
                                          "Diallo, Awa",   "Eriksen, Jonas", "Fujita, Ren"};
    robin(six, "Club Championship 2026", "Lyon", {"2026.01.10", "2026.03.14", "2026.06.20"}, 1900, 2);
    // The same name in two places, their players apart: two tournaments.
    robin({"Abel, Jon", "Bodo, Ana", "Cole, Eva", "Dorn, Max"}, "City Open 2026", "Paris", {"2026.05.01"}, 2000);
    robin({"Egan, Rob", "Fry, Liv", "Gale, Ian", "Hart, Joy"}, "City Open 2026", "Nice", {"2026.05.01"}, 1800);
    // The same event two years: two tournaments.
    robin({"Abel, Jon", "Bodo, Ana", "Cole, Eva", "Dorn, Max"}, "Spring Cup 2025", "Brest", {"2025.04.01"}, 2100);
    robin({"Abel, Jon", "Bodo, Ana", "Cole, Eva", "Dorn, Max"}, "Spring Cup 2026", "Brest", {"2026.04.01"}, 2100);
    // From December to January: one.
    robin({"Egan, Rob", "Fry, Liv", "Gale, Ian", "Hart, Joy"}, "Winter Congress", "York",
          {"2025.12.29", "2025.12.30", "2026.01.02"}, 1700);
    // A team event: its games give teams.
    for (int r = 1; r <= 3; ++r)
        for (int b = 0; b < 4; ++b) {
            G x;
            x.event = "Interclubs 2026";
            x.site  = "Lille";
            x.round = std::to_string(r) + "." + std::to_string(b + 1);
            x.white = "Team" + std::to_string(r) + " Player" + std::to_string(b);
            x.black = "Rival" + std::to_string(r) + " Player" + std::to_string(b);
            x.white = std::string(1, static_cast<char>('A' + b)) + "ndersen, Ole" + std::to_string(r);
            x.black = std::string(1, static_cast<char>('K' + b)) + "rag, Per" + std::to_string(r);
            x.more  = {{"WhiteTeam", "Lille A"}, {"BlackTeam", "Arras B"}};
            games.push_back(x);
        }
    // Online games: no rounds, one account against many: not a tournament.
    for (int k = 0; k < 30; ++k) {
        G x;
        x.event = "Rated Blitz game";
        x.site  = "https://lichess.org/abc" + std::to_string(10000 + k);
        x.white = k % 2 ? "duval_a" : "opponent_" + std::to_string(k % 10);
        x.black = k % 2 ? "opponent_" + std::to_string(k % 10) : "duval_a";
        x.round.clear();
        games.push_back(x);
    }
    auto db         = database("ct-list", make(games));
    const auto snap = db->snapshot();
    const auto list = listOf(snap);
    CHECK(list.games == games.size());
    CHECK(list.others == 30);
    CHECK(list.tournaments.size() == 8);
    const auto* masters = entryOf(list, "masters tournament|2026");
    REQUIRE(masters);
    CHECK(masters->shape.system == SystemChoice{TournamentSystem::RoundRobin, 1});
    CHECK(masters->shape.players == 8);
    CHECK(masters->shape.rounds == 7);
    CHECK(masters->shape.games == 28);
    CHECK(masters->shape.expected == 28);
    CHECK(masters->mean_elo == 2565);  // 2600 down to 2530 by tens
    CHECK(std::string(snap->name(masters->event)) == "Masters Tournament 2026");
    CHECK(std::string(snap->name(masters->site)) == "Leiden");
    const auto* club = entryOf(list, "club championship|2026");
    REQUIRE(club);
    CHECK(club->shape.system == SystemChoice{TournamentSystem::RoundRobin, 2});
    CHECK(club->shape.rounds == 10);
    CHECK(club->shape.games == 30);
    CHECK(club->shape.expected == 30);
    CHECK(club->first_date == packDate("2026.01.10"));
    CHECK(club->last_date == packDate("2026.06.20"));
    CHECK(entryOf(list, "city open|2026|paris"));
    CHECK(entryOf(list, "city open|2026|nice"));
    CHECK(entryOf(list, "city open|2026|paris")->shape.players == 4);
    CHECK(entryOf(list, "spring cup|2025"));
    CHECK(entryOf(list, "spring cup|2026"));
    const auto* winter = entryOf(list, "winter congress|2025");
    REQUIRE(winter);
    CHECK(winter->shape.games == 6);
    CHECK(winter->ref.year_to == 2026);
    const auto* team = entryOf(list, "interclubs|2026");
    REQUIRE(team);
    CHECK(team->shape.system.system == TournamentSystem::Team);
    CHECK(team->shape.expected == 0);
    // The highest mean Elo first.
    CHECK(list.tournaments.front().key == "masters tournament|2026");
    for (std::size_t i = 1; i < list.tournaments.size(); ++i)
        CHECK(list.tournaments[i - 1].mean_elo >= list.tournaments[i].mean_elo);
    // A tournament's games found again: the Winter Congress over its two years, City Open in Paris alone.
    std::atomic<bool> cancel{false};
    CHECK(tournamentGames(*snap, winter->ref, cancel).size() == 6);
    CHECK(tournamentGames(*snap, entryOf(list, "city open|2026|nice")->ref, cancel).size() == 6);
    // A database without tournaments.
    std::vector<G> online(games.end() - 30, games.end());
    auto lichess    = database("ct-online", make(online));
    const auto none = listOf(lichess->snapshot());
    CHECK(none.tournaments.empty());
    CHECK(none.others == 30);
}

namespace {

// ---------------------------------------------------------------------------------------------------------------------
// The count made apart: a tournament's figures written again from the rules, without the library's tables.

struct NGame {
    int white = -1, black = -1, round = 0;
    char result  = '=';
    bool forfeit = false;
    bool missing = false;  // played, its result not in the data
};

struct NPlayer {
    std::string name;
    int elo = 0;
};

struct Count {
    std::vector<double> points, bh, bh_c1, bh_m1, sb, wins, won, black_games, black_wins, progressive, koya, aro,
        aro_c1;
    std::vector<std::optional<int>> tpr;
    std::vector<std::optional<double>> change;
    std::vector<int> unknown;
};

int tableDp(double p) {
    // B.02 8.1.1, written out.
    static const int dp[] = {0,   7,   14,  21,  29,  36,  43,  50,  57,  65,  72,  80,  87,  95,  102, 110, 117,
                             125, 133, 141, 149, 158, 166, 175, 184, 193, 202, 211, 220, 230, 240, 251, 262, 273,
                             284, 296, 309, 322, 336, 351, 366, 383, 401, 422, 444, 470, 501, 538, 589, 677, 800};
    const int h           = static_cast<int>(std::floor(p * 100 + 0.5 + 1e-9));
    return h >= 50 ? dp[h - 50] : -dp[50 - h];
}

double tablePd(int d) {
    static const int up[] = {3,   10,  17,  25,  32,  39,  46,  53,  61,  68,  76,  83,  91,  98,  106, 113, 121,
                             129, 137, 145, 153, 162, 170, 179, 188, 197, 206, 215, 225, 235, 245, 256, 267, 278,
                             290, 302, 315, 328, 344, 357, 374, 391, 411, 432, 456, 484, 517, 559, 619, 735};
    int i                 = 0;
    while (i < 50 && std::abs(d) > up[i]) ++i;
    const double h = 0.5 + i * 0.01;
    return d >= 0 ? h : 1 - h;
}

// A Swiss or a round robin without byes nor withdrawals (each player in every round), with forfeits and missing games.
Count count(const std::vector<NPlayer>& players, const std::vector<NGame>& games, bool swiss) {
    const auto n = players.size();
    Count c;
    c.points.assign(n, 0);
    c.unknown.assign(n, 0);
    for (const auto& g : games) {
        if (g.missing) {
            ++c.unknown[static_cast<std::size_t>(g.white)];
            ++c.unknown[static_cast<std::size_t>(g.black)];
            continue;
        }
        const double w = g.result == '1' ? 1 : g.result == '=' ? 0.5 : 0;
        c.points[static_cast<std::size_t>(g.white)] += w;
        c.points[static_cast<std::size_t>(g.black)] += 1 - w;
    }
    c.bh.assign(n, 0);
    c.bh_c1.assign(n, 0);
    c.bh_m1.assign(n, 0);
    c.sb.assign(n, 0);
    c.wins.assign(n, 0);
    c.won.assign(n, 0);
    c.black_games.assign(n, 0);
    c.black_wins.assign(n, 0);
    c.progressive.assign(n, 0);
    c.koya.assign(n, 0);
    c.aro.assign(n, 0);
    c.aro_c1.assign(n, 0);
    int rounds = 0;
    for (const auto& g : games) rounds = std::max(rounds, g.round);
    // Koya's half: of the most a player could score (the rounds of a Swiss, the games of each in a round robin).
    const double half = swiss ? rounds / 2.0 : (n - 1) * (rounds / static_cast<double>(n - 1)) / 2.0;
    c.tpr.assign(n, std::nullopt);
    c.change.assign(n, std::nullopt);
    for (std::size_t p = 0; p < n; ++p) {
        std::vector<double> parts;
        std::optional<double> forfeit_lost;  // its contribution: a voluntary unplayed round, cut first
        std::vector<int> ratings;
        double board = 0, expected = 0, scored = 0;
        bool unplayed = false;
        int rated     = 0;
        for (const auto& g : games) {
            const bool white = g.white == static_cast<int>(p);
            if (!white && g.black != static_cast<int>(p)) continue;
            const auto o   = static_cast<std::size_t>(white ? g.black : g.white);
            const double s = g.missing ? 0 : (g.result == '=' ? 0.5 : (g.result == '1') == white ? 1.0 : 0.0);
            if (g.forfeit && swiss) {
                // A dummy of the player's score, capped by the scheduled opponent's (C.07 16.4.1).
                const double dummy = std::min(c.points[p], c.points[o]);
                parts.push_back(dummy);
                c.sb[p] += dummy * s;
                if (s == 0) forfeit_lost = dummy;
            } else {
                parts.push_back(c.points[o]);
                c.sb[p] += c.points[o] * s;
            }
            if (!g.missing && s == 1) c.wins[p] += 1;
            if (!g.missing && c.points[o] + 1e-9 >= half) c.koya[p] += s;
            if (g.forfeit) {
                unplayed = true;
                continue;
            }
            if (g.missing) continue;
            if (s == 1) c.won[p] += 1;
            if (!white) {
                c.black_games[p] += 1;
                if (s == 1) c.black_wins[p] += 1;
            }
            ratings.push_back(players[o].elo);
            board += s;
            int d = players[p].elo - players[o].elo;
            if (players[p].elo < 2650) d = std::max(-400, std::min(400, d));
            expected += tablePd(d);
            scored += s;
            ++rated;
        }
        c.bh[p] = std::accumulate(parts.begin(), parts.end(), 0.0);
        if (forfeit_lost)
            c.bh_c1[p] = c.bh[p] - *forfeit_lost;
        else
            c.bh_c1[p] = c.bh[p] - (parts.empty() ? 0 : *std::min_element(parts.begin(), parts.end()));
        // Median 1: the cut 1's, less its highest value left.
        {
            auto left = parts;
            if (forfeit_lost)
                left.erase(std::find(left.begin(), left.end(), *forfeit_lost));
            else if (!left.empty())
                left.erase(std::min_element(left.begin(), left.end()));
            if (!left.empty()) left.erase(std::max_element(left.begin(), left.end()));
            c.bh_m1[p] = std::accumulate(left.begin(), left.end(), 0.0);
        }
        // The progressive score: after each round, its results known.
        {
            double running = 0;
            for (int r = 1; r <= rounds; ++r) {
                for (const auto& g : games) {
                    if (g.round != r || g.missing) continue;
                    if (g.white == static_cast<int>(p)) running += g.result == '1' ? 1 : g.result == '=' ? 0.5 : 0;
                    if (g.black == static_cast<int>(p)) running += g.result == '0' ? 1 : g.result == '=' ? 0.5 : 0;
                }
                c.progressive[p] += running;
            }
        }
        if (!ratings.empty()) {
            const double aro  = std::accumulate(ratings.begin(), ratings.end(), 0.0) / ratings.size();
            const int rounded = static_cast<int>(std::floor(aro + 0.5 + 1e-9));
            c.aro[p]          = rounded;
            c.tpr[p]          = rounded + tableDp(board / ratings.size());
            auto cut          = ratings;
            if (!unplayed) cut.erase(std::min_element(cut.begin(), cut.end()));
            c.aro_c1[p] =
                cut.empty() ? 0 : std::floor(std::accumulate(cut.begin(), cut.end(), 0.0) / cut.size() + 0.5 + 1e-9);
        }
        if (rated > 0) {
            const int k = players[p].elo >= 2400 ? 10 : 20;
            c.change[p] = k * (scored - expected);
        }
    }
    return c;
}

const std::vector<std::string> SURNAMES = {"Brennan", "Lindqvist", "Novak", "Moreau",  "Ferreira", "Sato",
                                           "Haddad",  "Martin",    "Ito",   "Okafor",  "Varga",    "Adams",
                                           "Castell", "Brandt",    "Quint", "Rinaldi", "Kowal",    "Dorn"};
const std::vector<std::string> GIVEN = {"Liam", "Erik", "Tomas", "Claire", "Joana", "Ken",   "Samir", "Paul",  "Yuki",
                                        "Ada",  "Lena", "Zoe",   "Nora",   "Felix", "Pavel", "Rosa",  "Petro", "Max"};

}  // namespace

TEST_CASE("crosstable: every figure against a count made apart: a forfeit, a missing game, two names, games twice") {
    std::mt19937 random(92);
    // Sixteen players, five rounds: the first five rounds of a round robin's circle (no pair twice), a Swiss's shape.
    std::vector<NPlayer> players;
    for (int i = 0; i < 16; ++i)
        players.push_back({SURNAMES[static_cast<std::size_t>(i)] + ", " + GIVEN[static_cast<std::size_t>(i)],
                           2480 - i * 23 - static_cast<int>(random() % 9)});
    std::vector<NGame> games;
    {
        std::vector<int> p(16);
        std::iota(p.begin(), p.end(), 0);
        for (int r = 1; r <= 5; ++r) {
            for (int k = 0; k < 8; ++k) {
                NGame g;
                g.white = p[static_cast<std::size_t>(k)];
                g.black = p[static_cast<std::size_t>(15 - k)];
                if ((r + k) % 2) std::swap(g.white, g.black);
                g.round  = r;
                g.result = "10=="[random() % 4];
                games.push_back(g);
            }
            std::rotate(p.begin() + 1, p.end() - 1, p.end());
        }
    }
    // A forfeit in round 2; a game of round 4 missing from the data.
    games[9].forfeit  = true;
    games[9].result   = '1';
    games[26].missing = true;
    // As a database writes them: Lindqvist under "Lindqvist,E" in rounds 1 and 2; rounds 3 and 5 come again from a
    // second source, the headers written otherwise.
    const int erik = 1;
    std::vector<G> pgn;
    for (std::size_t i = 0; i < games.size(); ++i) {
        const auto& g = games[i];
        if (g.missing) continue;
        G x;
        x.event = "Autumn Open 2026";
        x.site  = "Lyon";
        x.date  = "2026.10." + std::string(g.round < 10 ? "0" : "") + std::to_string(g.round);
        x.round = std::to_string(g.round) + "." + std::to_string(i % 8 + 1);
        x.white = players[static_cast<std::size_t>(g.white)].name;
        x.black = players[static_cast<std::size_t>(g.black)].name;
        if (g.round <= 2) {
            if (g.white == erik) x.white = "Lindqvist,E";
            if (g.black == erik) x.black = "Lindqvist,E";
        }
        x.white_elo = players[static_cast<std::size_t>(g.white)].elo;
        x.black_elo = players[static_cast<std::size_t>(g.black)].elo;
        x.result    = g.result;
        x.plies     = g.forfeit ? 0 : 30 + static_cast<int>(i % 20) * 2;
        pgn.push_back(x);
    }
    const auto first = pgn.size();
    for (std::size_t i = 0; i < first; ++i) {
        const auto r = readRound(pgn[i].round).round;
        if (r != 3 && r != 5) continue;
        auto x  = pgn[i];
        x.event = "Autumn Open";
        x.round = std::to_string(r);
        // "Surname,I"
        const auto initial = [](const std::string& name) {
            const auto comma = name.find(',');
            return name.substr(0, comma) + "," + name.substr(comma + 2, 1);
        };
        x.white = initial(x.white);
        x.black = initial(x.black);
        x.more  = {{"Annotator", "second source"}};
        pgn.push_back(x);
    }
    auto db   = database("ct-count", make(pgn));
    auto snap = db->snapshot();
    auto list = listOf(snap);
    REQUIRE(list.tournaments.size() == 1);
    auto entry = list.tournaments[0];
    CHECK(entry.key == "autumn open|2026");
    CHECK(entry.shape.system.system == TournamentSystem::Swiss);
    CHECK(entry.shape.rounds == 5);
    CHECK(entry.duplicates == 16);  // rounds 3 and 5 once more
    // Lindqvist under two names: 17 players; his first spelling's rounds 1 and 2, his second's 3 to 5.
    auto t = crosstableOf(snap, entry);
    CHECK(t.players.size() == 17);
    REQUIRE(t.two_names.size() == 1);
    CHECK(t.players[t.two_names[0].a].name == "Lindqvist,E");
    CHECK(t.players[t.two_names[0].b].name == "Lindqvist, Erik");
    CHECK(t.two_names[0].a_to == 2);
    CHECK(t.two_names[0].b_from == 3);
    CHECK(row(t, "Lindqvist,E").points + row(t, "Lindqvist, Erik").points ==
          count(players, games, true).points[static_cast<std::size_t>(erik)]);

    // His spellings brought together in the table of names: the figures against the count made apart.
    {
        const auto id_of = [&](const std::string& name) {
            for (uint32_t id = 0; id < snap->names(); ++id)
                if (snap->name(id) == name) return id;
            FAIL("no name " << name);
            return uint32_t{0};
        };
        Player p;
        p.name      = "Lindqvist, Erik";
        p.how       = "hand";
        p.spellings = {id_of("Lindqvist,E"), id_of("Lindqvist, Erik")};
        std::sort(p.spellings.begin(), p.spellings.end());
        db->setPlayers(PlayerTable({p}));
    }
    snap  = db->snapshot();
    list  = listOf(snap);
    entry = list.tournaments[0];
    CHECK(entry.shape.players == 16);
    CHECK(entry.shape.games == 39);
    CHECK(entry.shape.expected == 40);  // "39 of 40"
    CrossOptions options;
    options.order = std::vector<Tiebreak>{
        Tiebreak::Buchholz,          Tiebreak::BuchholzCut1, Tiebreak::SonnebornBerger, Tiebreak::Wins,
        Tiebreak::AverageRatingCut1, Tiebreak::Performance,  Tiebreak::BuchholzMedian1, Tiebreak::GamesWon,
        Tiebreak::BlackGames,        Tiebreak::BlackWins,    Tiebreak::Progressive,     Tiebreak::Koya,
        Tiebreak::AverageRating};
    t = crosstableOf(snap, entry, options);
    REQUIRE(t.players.size() == 16);
    CHECK(t.two_names.empty());
    CHECK(t.forfeits.size() == 1);
    REQUIRE(t.missing.size() == 1);
    CHECK(t.missing[0].round == 4);
    const auto c     = count(players, games, true);
    const auto index = [&](const std::string& name) {
        for (std::size_t i = 0; i < players.size(); ++i)
            if (players[i].name == name) return i;
        FAIL("no player " << name);
        return std::size_t{0};
    };
    {
        std::set<std::string> pair{t.players[t.missing[0].white].name, t.players[t.missing[0].black].name};
        CHECK(pair == std::set<std::string>{players[static_cast<std::size_t>(games[26].white)].name,
                                            players[static_cast<std::size_t>(games[26].black)].name});
    }
    // The start numbers by rating, as the count gives them.
    std::vector<std::size_t> by_rating(players.size());
    std::iota(by_rating.begin(), by_rating.end(), std::size_t{0});
    std::stable_sort(by_rating.begin(), by_rating.end(),
                     [&](std::size_t a, std::size_t b) { return players[a].elo > players[b].elo; });
    for (const auto& r : t.rows) {
        const auto& name = t.players[r.player].name;
        CAPTURE(name);
        const auto i = index(name);
        CHECK(r.points == c.points[i]);
        CHECK(r.unknown == c.unknown[i]);
        CHECK(r.tiebreaks[0] == doctest::Approx(c.bh[i]));
        CHECK(r.tiebreaks[1] == doctest::Approx(c.bh_c1[i]));
        CHECK(r.tiebreaks[2] == doctest::Approx(c.sb[i]));
        CHECK(r.tiebreaks[3] == c.wins[i]);
        CHECK(r.tiebreaks[4] == c.aro_c1[i]);
        CHECK(r.tiebreaks[5] == c.tpr[i].value_or(0));
        CHECK(r.tiebreaks[6] == doctest::Approx(c.bh_m1[i]));
        CHECK(r.tiebreaks[7] == c.won[i]);
        CHECK(r.tiebreaks[8] == c.black_games[i]);
        CHECK(r.tiebreaks[9] == c.black_wins[i]);
        CHECK(r.tiebreaks[10] == doctest::Approx(c.progressive[i]));
        CHECK(r.tiebreaks[11] == doctest::Approx(c.koya[i]));
        CHECK(r.tiebreaks[12] == c.aro[i]);
        CHECK(r.performance == c.tpr[i]);
        REQUIRE(r.change.has_value() == c.change[i].has_value());
        if (r.change) CHECK(*r.change == doctest::Approx(*c.change[i]));
        CHECK(r.start == static_cast<int>(std::find(by_rating.begin(), by_rating.end(), i) - by_rating.begin()) + 1);
        // Each round: the opponent, the colour and the result the count gives.
        for (int round = 1; round <= 5; ++round) {
            CAPTURE(round);
            const auto it = std::find_if(games.begin(), games.end(), [&](const NGame& g) {
                return g.round == round && (g.white == static_cast<int>(i) || g.black == static_cast<int>(i));
            });
            REQUIRE(it != games.end());
            const auto e =
                std::find_if(r.entries.begin(), r.entries.end(), [&](const CrossEntry& x) { return x.round == round; });
            REQUIRE(e != r.entries.end());
            const bool white = it->white == static_cast<int>(i);
            const auto other = static_cast<std::size_t>(white ? it->black : it->white);
            CHECK(t.players[e->opponent].name == players[other].name);
            if (it->missing) {
                CHECK(e->kind == CellKind::Missing);
                CHECK_FALSE(e->known);
                continue;
            }
            CHECK(e->white == white);
            CHECK(e->kind == (it->forfeit ? CellKind::Forfeit : CellKind::Game));
            const double s = it->result == '=' ? 0.5 : (it->result == '1') == white ? 1.0 : 0.0;
            CHECK(e->points == s);
        }
        // Those who met the missing game's players: their opponents' scores not sure.
        bool met_unsure = false;
        for (const auto& e : r.entries)
            if (e.opponent != NO_PLAYER && c.unknown[index(t.players[e.opponent].name)] > 0) met_unsure = true;
        CHECK((r.states[0] == TiebreakState::Unsure) == (met_unsure || c.unknown[i] > 0));
    }

    // A round robin of eight and a double round robin of six, their results drawn: points, SB, wins, performance and
    // rating change against the count.
    for (const int cycles : {1, 2}) {
        CAPTURE(cycles);
        const int n = cycles == 1 ? 8 : 6;
        std::vector<NPlayer> rr;
        for (int i = 0; i < n; ++i)
            rr.push_back({SURNAMES[static_cast<std::size_t>(17 - i)] + ", " + GIVEN[static_cast<std::size_t>(i)],
                          2300 - i * 31});
        std::vector<NGame> rg;
        std::vector<int> p(static_cast<std::size_t>(n));
        std::iota(p.begin(), p.end(), 0);
        int round = 0;
        for (int cy = 0; cy < cycles; ++cy)
            for (int r = 0; r < n - 1; ++r) {
                ++round;
                for (int k = 0; k < n / 2; ++k) {
                    NGame g;
                    g.white = p[static_cast<std::size_t>(k)];
                    g.black = p[static_cast<std::size_t>(n - 1 - k)];
                    if ((r + k + cy) % 2) std::swap(g.white, g.black);
                    g.round  = round;
                    g.result = "10="[random() % 3];
                    rg.push_back(g);
                }
                std::rotate(p.begin() + 1, p.end() - 1, p.end());
            }
        std::vector<G> rp;
        for (const auto& g : rg) {
            G x;
            x.event     = cycles == 1 ? "Masters Tournament 2026" : "Club Championship 2026";
            x.white     = rr[static_cast<std::size_t>(g.white)].name;
            x.black     = rr[static_cast<std::size_t>(g.black)].name;
            x.white_elo = rr[static_cast<std::size_t>(g.white)].elo;
            x.black_elo = rr[static_cast<std::size_t>(g.black)].elo;
            x.round     = std::to_string(g.round);
            x.result    = g.result;
            rp.push_back(x);
        }
        auto rdb      = database("ct-count-rr" + std::to_string(cycles), make(rp));
        const auto rs = rdb->snapshot();
        const auto rl = listOf(rs);
        REQUIRE(rl.tournaments.size() == 1);
        CHECK(rl.tournaments[0].shape.system == SystemChoice{TournamentSystem::RoundRobin, cycles});
        CrossOptions o;
        o.order          = std::vector<Tiebreak>{Tiebreak::SonnebornBerger, Tiebreak::Wins, Tiebreak::Koya,
                                                 Tiebreak::Progressive, Tiebreak::BlackGames};
        const auto table = crosstableOf(rs, rl.tournaments[0], o);
        const auto rc    = count(rr, rg, false);
        for (const auto& r : table.rows) {
            std::size_t i = 0;
            while (rr[i].name != table.players[r.player].name) ++i;
            CAPTURE(rr[i].name);
            CHECK(r.points == rc.points[i]);
            CHECK(r.tiebreaks[0] == doctest::Approx(rc.sb[i]));
            CHECK(r.tiebreaks[1] == rc.wins[i]);
            CHECK(r.tiebreaks[2] == doctest::Approx(rc.koya[i]));
            CHECK(r.tiebreaks[3] == doctest::Approx(rc.progressive[i]));
            CHECK(r.tiebreaks[4] == rc.black_games[i]);
            CHECK(r.performance == rc.tpr[i]);
            REQUIRE(r.change.has_value());
            CHECK(*r.change == doctest::Approx(*rc.change[i]));
            CHECK(r.entries.size() == static_cast<std::size_t>(cycles * (n - 1)));
        }
        // The standings: by points, then SB, then wins.
        for (std::size_t k = 1; k < table.rows.size(); ++k) {
            const auto& a = table.rows[k - 1];
            const auto& b = table.rows[k];
            CHECK((a.points > b.points || (a.points == b.points && (a.tiebreaks[0] > b.tiebreaks[0] + 1e-9 ||
                                                                    (std::abs(a.tiebreaks[0] - b.tiebreaks[0]) < 1e-9 &&
                                                                     a.tiebreaks[1] >= b.tiebreaks[1])))));
        }
    }
}

TEST_CASE("crosstable: a Swiss's rounds without a game, set by hand, are no games missing") {
    std::vector<CrossPlayer> players;
    for (const auto* name :
         {"Player_A", "Player_B", "Player_C", "Player_D", "Player_E", "Player_F", "Player_G", "Player_H"}) {
        CrossPlayer p;
        p.name = name;
        p.elo  = 2000 - static_cast<int>(players.size()) * 25;
        players.push_back(p);
    }
    const auto game = [](uint32_t w, uint32_t b, int round, char result) {
        CrossGame g;
        g.white       = w;
        g.black       = b;
        g.round.round = round;
        g.result      = result;
        g.plies       = 60;
        return g;
    };
    // Round 2: Player_F has a bye, Player_H is absent, as a Swiss kept from Lichess has them from its TRF file (#95).
    const std::vector<CrossGame> games{game(0, 1, 1, '1'), game(2, 3, 1, '='), game(4, 5, 1, '0'), game(6, 7, 1, '1'),
                                       game(2, 0, 2, '0'), game(1, 3, 2, '1'), game(4, 6, 2, '='), game(0, 4, 3, '='),
                                       game(5, 1, 3, '1'), game(2, 6, 3, '1'), game(3, 7, 3, '0')};
    auto t = makeCrosstable(players, games, {});
    REQUIRE(t.system.system == TournamentSystem::Swiss);
    CHECK(t.shape.games == 11);
    CHECK(t.shape.expected == 12);  // the two players without a game in round 2 look like a game missing
    CrossOptions options;
    options.hand.push_back(HandResult{"Player_F", "", 2, '*', 1.0});
    options.hand.push_back(HandResult{"Player_H", "", 2, '*', 0.0});
    t = makeCrosstable(players, games, options);
    CHECK(t.shape.games == 11);
    CHECK(t.shape.expected == 11);
    CHECK(row(t, "Player_F").points == 3);  // the bye's point with its two wins
}

TEST_CASE("crosstable: results set by hand, a broadcast being played, the system its organisers name, FIDE's K") {
    // A round robin of five with one game missing: its result set by hand.
    auto games = roundRobinOfFive();
    games.erase(games.begin() + 3);  // Petersen–Kim, round 2
    auto db   = database("ct-hand", make(games));
    auto snap = db->snapshot();
    auto list = listOf(snap);
    REQUIRE(list.tournaments.size() == 1);
    CHECK(list.tournaments[0].shape.games == 9);
    CHECK(list.tournaments[0].shape.expected == 10);
    auto t = crosstableOf(snap, list.tournaments[0]);
    REQUIRE(t.missing.size() == 1);
    CHECK(t.missing[0].round == 2);  // the one round both rest in
    CHECK(row(t, "Kim, Seo-yeon").unknown == 1);
    CHECK(row(t, "Kim, Seo-yeon").points == 0);
    CrossOptions options;
    options.hand.push_back(HandResult{"Petersen, Mads", "Kim, Seo-yeon", 2, '0', 0});
    t = crosstableOf(snap, list.tournaments[0], options);
    CHECK(row(t, "Kim, Seo-yeon").unknown == 0);
    CHECK(row(t, "Kim, Seo-yeon").points == 1);
    CHECK(*row(t, "Kim, Seo-yeon").change == doctest::Approx(-6.6));  // as the whole tournament's

    // A broadcast being played: a round robin of six, rounds 1 to 3 played, round 4 under way.
    std::vector<CrossPlayer> players;
    for (const auto* name :
         {"Arslan, Deniz", "Borg, Ingrid", "Castro, Mateo", "Diallo, Awa", "Eriksen, Jonas", "Fujita, Ren"}) {
        CrossPlayer p;
        p.name = name;
        p.elo  = 2500 - static_cast<int>(players.size()) * 20;
        players.push_back(p);
    }
    const auto game = [](uint32_t w, uint32_t b, int round, char result, bool live = false) {
        CrossGame g;
        g.white       = w;
        g.black       = b;
        g.round.round = round;
        g.result      = result;
        g.live        = live;
        g.plies       = live ? 20 : 60;
        return g;
    };
    std::vector<CrossGame> live{game(0, 5, 1, '1'),       game(1, 4, 1, '='),       game(2, 3, 1, '='),
                                game(3, 5, 2, '1'),       game(2, 4, 2, '='),       game(0, 1, 2, '='),
                                game(1, 5, 3, '1'),       game(0, 2, 3, '1'),       game(3, 4, 3, '='),
                                game(4, 5, 4, '*', true), game(0, 3, 4, '*', true), game(1, 2, 4, '*', true)};
    CrossOptions on;
    on.live    = true;
    on.planned = 5;
    t          = makeCrosstable(players, live, on);
    CHECK(t.system == SystemChoice{TournamentSystem::RoundRobin, 1});
    CHECK(t.live == 3);
    CHECK(t.missing.empty());
    const auto& arslan = row(t, "Arslan, Deniz");
    CHECK(arslan.points == 2.5);
    CHECK(arslan.played == 3);
    CHECK(arslan.unknown == 0);
    int live_cells = 0, to_come = 0;
    for (const auto& e : arslan.entries) {
        live_cells += e.kind == CellKind::Live;
        to_come += e.kind == CellKind::ToCome;
    }
    CHECK(live_cells == 1);
    CHECK(to_come == 1);  // Eriksen, round 5
    // Its rating change so far: the games finished only.
    CHECK(arslan.rated_games == 3);
    // Named by its organisers: a Swiss whatever its games look like.
    on.format = "9-round Swiss";
    CHECK(makeCrosstable(players, live, on).system.system == TournamentSystem::Swiss);
    on.format = "6-player double round-robin";
    CHECK(makeCrosstable(players, live, on).system == SystemChoice{TournamentSystem::RoundRobin, 2});

    // FIDE's K from the list for the players matched in the table of names, its list of the tournament's year.
    {
        std::string text;
        const auto pad = [](std::string s, std::size_t w) {
            s.resize(std::max(s.size(), w), ' ');
            return s;
        };
        const std::size_t widths[] = {15, 61, 4, 4, 5, 5, 15, 4, 6, 4, 3, 6, 4, 3, 6, 4, 3, 6, 4};
        const auto line            = [&](const std::vector<std::string>& v) {
            std::string out;
            for (std::size_t c = 0; c < v.size(); ++c) out += pad(v[c], widths[c]);
            return out + "\r\n";
        };
        text += line({"ID Number", "Name", "Fed", "Sex", "Tit", "WTit", "OTit", "FOA", "SRtng", "SGm", "SK", "RRtng",
                      "RGm", "Rk", "BRtng", "BGm", "BK", "B-day", "Flag"});
        text += line({"90300001", "Kim, Seo-yeon", "KOR", "F", "", "WIM", "", "", "2250", "9", "40", "0", "0", "20",
                      "0", "0", "20", "2004", "w"});
        const auto file = fs::temp_directory_path() / "ct-fide.txt";
        {
            std::ofstream out(file, std::ios::binary);
            out << text;
        }
        const auto prepared = fs::temp_directory_path() / "ct-fide" / "list.bin";
        FidePrepareProgress progress;
        std::atomic<bool> cancel{false};
        (void)prepareFideList(file, prepared, "2026-11", progress, cancel);
        const FideList fide(prepared);
        REQUIRE(fide.size() == 1);
        CHECK(fide.player(0).k_standard == 40);
        CHECK(fide.player(0).k_rapid == 20);
        // Kim matched in the table of names.
        auto all   = database("ct-k", make(roundRobinOfFive()));
        auto ksnap = all->snapshot();
        Player kim;
        kim.name = "Kim, Seo-yeon";
        kim.fide = 90300001;
        kim.how  = "list";
        for (uint32_t id = 0; id < ksnap->names(); ++id)
            if (ksnap->name(id) == "Kim, Seo-yeon") kim.spellings.push_back(id);
        all->setPlayers(PlayerTable({kim}));
        ksnap         = all->snapshot();
        const auto kl = listOf(ksnap);
        REQUIRE(kl.tournaments.size() == 1);
        const auto withK = crosstableOf(ksnap, kl.tournaments[0], {}, &fide);
        CHECK(row(withK, "Kim, Seo-yeon").k == 40);
        CHECK(row(withK, "Kim, Seo-yeon").k_listed);
        CHECK(*row(withK, "Kim, Seo-yeon").change == doctest::Approx(-13.2));
        // A list of another year: FIDE's rules (20).
        const auto prepared_old = fs::temp_directory_path() / "ct-fide" / "list-2030.bin";
        (void)prepareFideList(file, prepared_old, "2030-01", progress, cancel);
        const FideList later(prepared_old);
        const auto rules = crosstableOf(ksnap, kl.tournaments[0], {}, &later);
        CHECK(row(rules, "Kim, Seo-yeon").k == 20);
        CHECK_FALSE(row(rules, "Kim, Seo-yeon").k_listed);
    }
}

TEST_CASE("crosstable: the builder's steps alone: two names found before any round is read, results set by hand") {
    using crosstable::CrosstableBuilder;
    std::vector<CrossPlayer> players;
    for (const auto* name : {"Okafor, Daniel", "Okafor, D.", "Navarro, Lucia", "Petersen, Mads", "Petersen, M.",
                             "Kim, Seo-yeon", "Kim, S."}) {
        CrossPlayer p;
        p.name = name;
        players.push_back(p);
    }
    const auto game = [](uint32_t w, uint32_t b, int round, char result) {
        CrossGame g;
        g.white       = w;
        g.black       = b;
        g.round.round = round;
        g.result      = result;
        g.plies       = 60;
        return g;
    };
    // "Okafor, Daniel" plays rounds 1 to 3, "Okafor, D." rounds 4 and 5, never against each other: one player under
    // two names. "Petersen, M." plays round 3, within "Petersen, Mads"'s rounds; "Kim, S." plays "Kim, Seo-yeon".
    const std::vector<CrossGame> games{game(0, 2, 1, '1'), game(3, 5, 1, '='), game(5, 0, 2, '0'), game(2, 3, 2, '1'),
                                       game(0, 3, 3, '='), game(4, 2, 3, '0'), game(1, 2, 4, '='), game(3, 6, 4, '1'),
                                       game(6, 5, 4, '0'), game(5, 1, 5, '*'), game(2, 3, 5, '0')};
    {
        CrosstableBuilder builder(players, games, {});
        builder.findTwoNames();
        const auto& two = builder.table().two_names;
        REQUIRE(two.size() == 1);
        CHECK(two[0].a == 0);
        CHECK(two[0].b == 1);
        CHECK(two[0].a_from == 1);
        CHECK(two[0].a_to == 3);
        CHECK(two[0].b_from == 4);
        CHECK(two[0].b_to == 5);
        CHECK(builder.doubled() == std::vector<bool>{true, true, false, false, false, false, false});
    }
    // Results set by hand, as the window keeps them: the game without a result, named Black first, takes it seen from
    // White's side; a game the data does not have is added; a round alone keeps its points; a name of no player and
    // a result not known are left out.
    CrossOptions options;
    options.hand = {HandResult{"Okafor, D.", "Kim, Seo-yeon", 5, '1', 0},
                    HandResult{"Kim, S.", "Navarro, Lucia", 2, '=', 0}, HandResult{"Petersen, M.", "", 4, '*', 0.5},
                    HandResult{"Nobody, Else", "Kim, S.", 1, '1', 0},
                    HandResult{"Kim, S.", "Okafor, Daniel", 3, '*', 0}};
    CrosstableBuilder builder(players, games, options);
    builder.setHandResults();
    const auto& t = builder.table();
    REQUIRE(t.games.size() == games.size() + 1);
    CHECK(t.games[9].white == 5);
    CHECK(t.games[9].result == '0');
    CHECK(t.games[9].hand);
    const auto& added = t.games.back();
    CHECK(added.white == 6);
    CHECK(added.black == 2);
    CHECK(added.round.round == 2);
    CHECK(added.result == '=');
    CHECK(added.hand);
    CHECK(added.plies == 2);
    CHECK(added.ref == UINT64_MAX);
    CHECK(builder.alonePoints() == std::map<std::pair<uint32_t, int>, double>{{{4, 4}, 0.5}});
    for (std::size_t i = 0; i < games.size(); ++i)
        if (i != 9) CHECK_FALSE(t.games[i].hand);
}

TEST_CASE("crosstable: a Buchholz cut takes a voluntary unplayed round first, then the highest (C.07 16.5)") {
    using crosstable::Contribution;
    using crosstable::cutSum;
    const auto played = [](double score, double points) {
        Contribution c;
        c.opponent = score;
        c.bh       = score;
        c.sb       = score * points;
        return c;
    };
    auto vur = played(2.5, 0.5);
    vur.vur  = true;
    const std::vector<Contribution> parts{played(3, 1), played(2, 0.5), vur, played(4, 0)};
    // The least significant value is 2, but the voluntary unplayed round goes first: 3 + 2 + 4.
    CHECK(cutSum(parts, false, 1, 0) == doctest::Approx(9));
    // The median: the highest (4) goes too.
    CHECK(cutSum(parts, false, 1, 1) == doctest::Approx(5));
    // Without a VUR, the least significant value: Buchholz's lowest, Sonneborn-Berger's of the lowest opponent.
    const std::vector<Contribution> played_only{played(3, 1), played(2, 0.5), played(2.5, 0), played(4, 0)};
    CHECK(cutSum(played_only, false, 1, 0) == doctest::Approx(9.5));
    CHECK(cutSum(played_only, true, 1, 0) == doctest::Approx(3));  // 3 + 0 + 0: the 1 of the opponent of 2 cut
    CHECK(cutSum(played_only, true, 0, 0) == doctest::Approx(4));
}
