// A tournament's crosstable (#92) behind its views: each tournament's settings kept in the profile and read again, the
// words and cells the views show, a followed broadcast's crosstable from the games it follows and the rounds kept in
// "Broadcasts", Ctrl+K's names, and the games missing found in another database and copied in. Players' names are
// invented.
#include <doctest/doctest.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <json.hpp>
#include <study/crosstable.hpp>
#include <study/import.hpp>
#include <study/store.hpp>

#include "console.hpp"
#include "crosstables.hpp"
#include "test_support.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

std::string game(const std::string& event, const std::string& round, const std::string& white, const std::string& black,
                 const std::string& result, int white_elo = 2400, int black_elo = 2380, const std::string& more = {},
                 const std::string& moves = "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6") {
    return fmt::format(
        "[Event \"{}\"]\n[Site \"Gijon ESP\"]\n[Date \"2026.11.21\"]\n[Round \"{}\"]\n[White \"{}\"]\n"
        "[Black \"{}\"]\n[Result \"{}\"]\n[WhiteElo \"{}\"]\n[BlackElo \"{}\"]\n{}\n{} {}\n",
        event, round, white, black, result, white_elo, black_elo, more, moves, result);
}

std::shared_ptr<study::Database> databaseOf(const std::string& name, const std::vector<std::string>& pgns) {
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    auto db = study::Database::create(dir);
    std::vector<study::ImportedGame> games;
    for (const auto& text : pgns) {
        study::ImportedGame g;
        REQUIRE(study::readImportedGame(text, g));
        games.push_back(std::move(g));
    }
    if (!games.empty()) db->append(std::move(games), true);
    return db;
}

study::TournamentList listOf(const std::shared_ptr<const study::Snapshot>& snap) {
    study::TournamentProgress progress;
    std::atomic<bool> cancel{false};
    return study::listTournaments(snap, progress, cancel, 2);
}

}  // namespace

TEST_CASE("crosstables: each tournament's system, tie-breaks and results set by hand kept in the profile") {
    const auto profile = fs::temp_directory_path() / "ct-profile";
    fs::remove_all(profile);
    fs::create_directories(profile);
    const auto place = crosstablePlace(profile / "databases" / "Base 2026.fcgdb", "autumn open|2026");
    CHECK(place.find("autumn open|2026") != std::string::npos);
    CHECK(broadcastPlace("abcd1234") == "lichess:abcd1234");
    {
        CrosstableStore store(profile);
        CHECK(store.get(place).empty());
        CrosstableSettings s;
        s.system = study::SystemChoice{study::TournamentSystem::RoundRobin, 2};
        s.order  = std::vector<study::Tiebreak>{study::Tiebreak::Koya, study::Tiebreak::SonnebornBerger};
        s.hand.push_back(study::HandResult{"Rossi, Clara", "Schmidt, Owen", 6, '0', 0});
        s.hand.push_back(study::HandResult{"Haddad, Lena", "", 8, '*', 0.5});
        store.set(place, s);
        CrosstableSettings live;
        live.order = std::vector<study::Tiebreak>{};  // no tie-break at all: kept as such
        store.set(broadcastPlace("abcd1234"), live);
    }
    // Read again by another window: the same.
    CrosstableStore again(profile);
    const auto s = again.get(place);
    REQUIRE(s.system);
    CHECK(*s.system == study::SystemChoice{study::TournamentSystem::RoundRobin, 2});
    REQUIRE(s.order);
    CHECK(*s.order == std::vector<study::Tiebreak>{study::Tiebreak::Koya, study::Tiebreak::SonnebornBerger});
    REQUIRE(s.hand.size() == 2);
    CHECK(s.hand[0] == study::HandResult{"Rossi, Clara", "Schmidt, Owen", 6, '0', 0});
    CHECK(s.hand[1].white == "Haddad, Lena");
    CHECK(s.hand[1].black.empty());
    CHECK(s.hand[1].points == 0.5);
    CHECK(s.hand_from.empty());  // the user's
    REQUIRE(again.get(broadcastPlace("abcd1234")).order);
    CHECK(again.get(broadcastPlace("abcd1234")).order->empty());
    // A Swiss kept from Lichess: its rounds without a game come from its TRF file, and say so (#95).
    const auto kept = crosstablePlace(profile / "databases" / "Lichess tournaments.fcgdb", "rook club swiss|2026");
    CrosstableSettings swiss;
    swiss.hand.push_back(study::HandResult{"Player_F", "", 2, '*', 1.0});
    swiss.hand_from = "lichess-trf";
    again.set(kept, swiss);
    CHECK(CrosstableStore(profile).get(kept).hand_from == "lichess-trf");
    // Settings back to FIDE's and as guessed: gone from the file.
    again.set(place, CrosstableSettings{});
    CHECK(CrosstableStore(profile).get(place).empty());
}

TEST_CASE("crosstables: the words and cells the views show") {
    CHECK(systemName({study::TournamentSystem::RoundRobin, 1}) == "round robin");
    CHECK(systemName({study::TournamentSystem::RoundRobin, 2}) == "double round robin");
    CHECK(systemName({study::TournamentSystem::RoundRobin, 4}) == "round robin, 4 cycles");
    CHECK(systemName({study::TournamentSystem::Swiss, 1}) == "Swiss system");
    CHECK(systemName({study::TournamentSystem::Team, 1}) == "team event");
    CHECK(pointsText(5.5) == "5½");
    CHECK(pointsText(0.5) == "½");
    CHECK(pointsText(0) == "0");
    CHECK(pointsText(6, 1) == "6?");
    CHECK(tiebreakValue(study::Tiebreak::Buchholz, 48.5) == "48½");
    CHECK(tiebreakValue(study::Tiebreak::SonnebornBerger, 12.25) == "12.25");
    CHECK(tiebreakValue(study::Tiebreak::AverageRatingCut1, 2367) == "2367");
    // A column that has quarters: two decimals all along it.
    CHECK(tiebreakValue(study::Tiebreak::SonnebornBerger, 112.5, true) == "112.50");
    CHECK(tiebreakValue(study::Tiebreak::SonnebornBerger, 121, true) == "121.00");
    CHECK(tiebreakValue(study::Tiebreak::Performance, 2367, true) == "2367");
    CHECK(changeText(11.24) == "+11.2");
    CHECK(changeText(-3.06) == "−3.1");
    CHECK(changeText(0.01) == "±0.0");
    for (std::size_t k = 0; k < study::TIEBREAKS; ++k) {
        const auto t = static_cast<study::Tiebreak>(k);
        CHECK(std::string(tiebreakName(t)).size() > 1);
        CHECK(std::string(tiebreakHeading(t)).size() > 1);
        CHECK(std::string(tiebreakHelp(t)).size() > 10);
    }

    // A Swiss's cells: the opponent's start number, the colour, the result.
    std::vector<study::CrossPlayer> players(4);
    players[0].name = "Brennan, Liam";
    players[0].elo  = 2455;
    players[1].name = "Lindqvist, Erik";
    players[1].elo  = 2398;
    players[2].name = "Novak, Tomas";
    players[2].elo  = 2471;
    players[3].name = "Moreau, Claire";
    players[3].elo  = 2341;
    const auto g    = [](uint32_t w, uint32_t b, int round, char result, uint32_t plies = 40) {
        study::CrossGame c;
        c.white       = w;
        c.black       = b;
        c.round.round = round;
        c.result      = result;
        c.plies       = plies;
        return c;
    };
    study::CrossOptions options;
    options.system = study::SystemChoice{study::TournamentSystem::Swiss, 1};
    const auto t   = study::makeCrosstable(
        players, {g(0, 1, 1, '='), g(2, 3, 1, '1', 0), g(1, 2, 2, '0'), g(3, 0, 2, '0')}, options);
    const auto& brennan = *t.rowOf(0);
    CHECK(brennan.start == 2);                                       // Novak 2471 first
    CHECK(crossCellText(t, brennan.entries[0], true) == "3w½");      // Lindqvist no. 3, with White, a draw
    CHECK(crossCellText(t, brennan.entries[1], true) == "4b1");      // Moreau no. 4, with Black, a win
    CHECK(crossCellText(t, t.rowOf(2)->entries[0], true) == "4w+");  // a forfeit won
    CHECK(crossCellText(t, t.rowOf(3)->entries[0], true) == "1b−");
    CHECK(crossCellText(t, brennan.entries[0], false) == "½");
}

TEST_CASE("crosstables: a followed broadcast's crosstable from its games and the rounds kept in Broadcasts") {
    // Rounds 1 to 3 kept in "Broadcasts"; round 3 also still sent; round 4 being played.
    const std::string event = "Winter Festival 2026";
    auto saved =
        databaseOf("ct-broadcasts", {
                                        game(event, "1.1", "Arslan, Deniz", "Fujita, Ren", "1-0", 2498, 2402),
                                        game(event, "1.2", "Borg, Ingrid", "Eriksen, Jonas", "1/2-1/2", 2476, 2420),
                                        game(event, "1.3", "Castro, Mateo", "Diallo, Awa", "1/2-1/2", 2455, 2431),
                                        game(event, "2.1", "Diallo, Awa", "Fujita, Ren", "1-0", 2431, 2402),
                                        game(event, "2.2", "Arslan, Deniz", "Borg, Ingrid", "1/2-1/2", 2498, 2476),
                                        game(event, "3.1", "Borg, Ingrid", "Fujita, Ren", "1-0", 2476, 2402),
                                    });
    std::vector<lichess::BroadcastGame> followed;
    const auto sent = [&](const std::string& round, const std::string& white, const std::string& black,
                          const std::string& result, int we, int be) {
        lichess::BroadcastGame g;
        g.url       = "https://lichess.org/broadcast/winter-festival-2026/round-" + round + "/" + white.substr(0, 3);
        g.white     = white;
        g.black     = black;
        g.result    = result;
        g.white_elo = we;
        g.black_elo = be;
        g.round     = round;
        g.pgn       = game(event, round + ".1", white, black, result, we, be);
        followed.push_back(g);
    };
    sent("3", "Borg, Ingrid", "Fujita, Ren", "1-0", 2476, 2402);
    sent("4", "Eriksen, Jonas", "Fujita, Ren", "*", 2420, 2402);
    sent("4", "Arslan, Deniz", "Diallo, Awa", "*", 2498, 2431);
    const auto snap = saved->snapshot();
    const auto list = listOf(snap);
    REQUIRE(list.tournaments.size() == 1);
    std::atomic<bool> cancel{false};
    const auto earlier = study::tournamentGames(*snap, list.tournaments[0].ref, cancel);
    const auto live    = liveCrosstableInput(followed, snap.get(), earlier);
    CHECK(live.event == event);
    CHECK(live.saved == 5);  // rounds 1 and 2: round 3 comes from the broadcast
    CHECK(live.players.size() == 6);
    CHECK(live.games.size() == 8);
    CHECK(live.urls.size() == 3);
    study::CrossOptions options;
    options.live   = true;
    options.format = "6-player round-robin";
    const auto t   = study::makeCrosstable(live.players, live.games, options);
    CHECK(t.system == study::SystemChoice{study::TournamentSystem::RoundRobin, 1});
    CHECK(t.live == 2);
    CHECK(t.missing.empty());
    const auto row = [&](const std::string& name) {
        for (const auto& r : t.rows)
            if (t.players[r.player].name == name) return &r;
        return static_cast<const study::CrossRow*>(nullptr);
    };
    REQUIRE(row("Arslan, Deniz"));
    CHECK(row("Arslan, Deniz")->points == 1.5);
    CHECK(row("Arslan, Deniz")->played == 2);
    CHECK(row("Borg, Ingrid")->points == 2.0);
    int live_cells = 0, to_come = 0;
    for (const auto& e : row("Fujita, Ren")->entries) {
        live_cells += e.kind == study::CellKind::Live;
        to_come += e.kind == study::CellKind::ToCome;
    }
    CHECK(live_cells == 1);
    CHECK(to_come == 1);  // Castro: not met yet
    // The cells say it.
    for (const auto& e : row("Fujita, Ren")->entries) {
        if (e.kind == study::CellKind::Live) CHECK(crossCellText(t, e, false) == "●");
        if (e.kind == study::CellKind::ToCome) CHECK(crossCellText(t, e, false) == "·");
    }
    // Without the database: the games followed alone.
    const auto alone = liveCrosstableInput(followed, nullptr, {});
    CHECK(alone.saved == 0);
    CHECK(alone.games.size() == 3);
}

TEST_CASE("crosstables: Ctrl+K's names; the games missing found in another database and copied in") {
    auto base =
        databaseOf("ct-names-base",
                   {
                       game("Autumn Open 2026", "5.1", "Rinaldi, Ada", "Brennan, Liam", "1-0"),
                       game("Autumn Open 2026", "5.2", "Novak, Tomas", "Moreau, Claire", "0-1"),
                       game("Autumn Open 2026", "6.1", "Brennan, Liam", "Novak, Tomas", "1-0"),
                       game("Masters Tournament 2026", "1.1", "Kim, Seo-yeon", "Duval, Anne", "1/2-1/2", 2522, 2412),
                       game("Masters Tournament 2026", "1.2", "Navarro, Lucia", "Dubois, Hugo", "1-0", 2549, 2507),
                       game("Masters Tournament 2026", "2.1", "Duval, Anne", "Navarro, Lucia", "0-1", 2412, 2549),
                   });
    auto other =
        databaseOf("ct-names-other", {
                                         game("Autumn Open", "6", "Moreau,C", "Rinaldi,A", "1/2-1/2", 2341, 2290),
                                         game("Autumn Open", "7", "Moreau,C", "Novak,T", "1-0", 2341, 2290),
                                     });
    const auto snap = base->snapshot();
    const auto list = std::make_shared<const study::TournamentList>(listOf(snap));
    REQUIRE(list->tournaments.size() == 2);
    std::vector<TournamentsOf> lists{{base->folder(), "Base 2026", snap, list}};
    const auto open = crosstableNames(lists, "open", 8);
    REQUIRE(open.size() == 1);
    CHECK(open[0].label == "Autumn Open 2026 · Gijon ESP");
    CHECK(open[0].database == "Base 2026");
    CHECK(open[0].key == "autumn open|2026");
    CHECK(crosstableNames(lists, "gijon 2026", 8).size() == 2);
    CHECK(crosstableNames(lists, "masters gijon", 8).size() == 1);
    CHECK(crosstableNames(lists, "lyon", 8).empty());

    // Round 6, Rinaldi – Moreau, looked for: found in the other database under other spellings, round 6 only.
    std::vector<MissingAsked> asked{{"Rinaldi, Ada", "Moreau, Claire", 6, 2026}};
    std::atomic<bool> cancel{false};
    const auto found = lookForMissing(asked, {{other->folder(), "TWIC 2026", other->snapshot()}}, cancel);
    REQUIRE(found.size() == 1);
    CHECK(found[0].database == "TWIC 2026");
    CHECK(found[0].round == "6");
    CHECK(found[0].white == "Moreau,C");
    CHECK(found[0].result == '=');
    // Copied into the tournament: its event, site, round and players written as the tournament's, colours kept.
    const auto copy = missingGameCopy(other->snapshot()->game(found[0].id), "Autumn Open 2026", "Gijon ESP", 6,
                                      "Rinaldi, Ada", "Moreau, Claire");
    const auto tag  = [&](const std::string& name) {
        for (const auto& [k, v] : copy.tags)
            if (k == name) return v;
        return std::string();
    };
    CHECK(tag("Event") == "Autumn Open 2026");
    CHECK(tag("Site") == "Gijon ESP");
    CHECK(tag("White") == "Moreau, Claire");
    CHECK(tag("Black") == "Rinaldi, Ada");
    CHECK(tag("Round") == "6");
    base->append({copy}, true);
    const auto after                     = listOf(base->snapshot());
    const study::TournamentEntry* autumn = nullptr;
    for (const auto& e : after.tournaments)
        if (e.key == "autumn open|2026") autumn = &e;
    REQUIRE(autumn);
    CHECK(autumn->shape.games == 4);
    CHECK(autumn->shape.players == 4);
}

TEST_CASE("crosstables: settings of the wrong type set aside, the others kept, said; a file cut short too (#136)") {
    const test::ScratchDir profile("ct-types");
    const auto place = crosstablePlace(profile / "databases" / "Base 2026.fcgdb", "autumn open|2026");
    const auto other = broadcastPlace("abcd1234");
    {
        CrosstableStore store(profile);
        CrosstableSettings s;
        s.system = study::SystemChoice{study::TournamentSystem::RoundRobin, 2};
        s.hand.push_back(study::HandResult{"Rossi, Clara", "Schmidt, Owen", 6, '0', 0});
        store.set(place, s);
        CrosstableSettings swiss;
        swiss.system = study::SystemChoice{study::TournamentSystem::Swiss, 1};
        store.set(other, swiss);
    }
    const auto file = profile / "crosstables.json";
    // A text for the round of a result set by hand (an older or newer version, a hand edit): it used to stop the
    // program as soon as the crosstables' settings were read.
    auto j                                      = nlohmann::json::parse(test::readFile(file));
    j["crosstables"][place]["hand"][0]["round"] = "six";
    const auto written                          = j.dump();
    std::ofstream(file, std::ios::binary) << written;
    const auto before = Console::instance().added();
    std::optional<CrosstableStore> store;
    REQUIRE_NOTHROW(store.emplace(profile));
    CHECK(store->get(place).empty());
    REQUIRE(store->get(other).system);
    CHECK(store->get(other).system->system == study::TournamentSystem::Swiss);
    auto said = Console::instance().since(before);
    REQUIRE(said.size() == 1);
    CHECK(said[0].find("1 entry could not be read and was set aside") != std::string::npos);
    CHECK(said[0].find("autumn open|2026") != std::string::npos);  // the place on one line
    CHECK(test::readFile(fs::path(file).concat(".unreadable")) == written);
    // A file cut short: kept aside, said, rather than replaced in silence by the next change.
    std::ofstream(file, std::ios::binary) << "{\"crosstables\": {\"lichess:abcd";
    const auto cut = Console::instance().added();
    CHECK(CrosstableStore(profile).get(other).empty());
    said = Console::instance().since(cut);
    REQUIRE(said.size() == 1);
    CHECK(said[0].find("could not be read: kept as") != std::string::npos);
    CHECK(fs::exists(fs::path(file).concat(".unreadable2")));
}
