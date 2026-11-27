// A database's maintenance (mockup M13, board 1): the sources of its games, duplicate games found and merged (and the
// merge undone), its table of names, its check and its compaction. Players' names are invented.
#include <doctest/doctest.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <study/duplicates.hpp>
#include <study/import_into.hpp>
#include <study/query.hpp>
#include <study/store.hpp>
#include <study/tree.hpp>

#include "test_games.hpp"
#include "test_support.hpp"

using namespace fcg::study;
using namespace fcg::test;
namespace fs = std::filesystem;
using fcg::test::ScratchDir;

namespace {

// Games long enough to be told apart by their moves (ten moves at least when their days are known).
const char* const MOVES =
    "1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 6. Be3 e5 7. Nb3 Be6 8. f3 Be7 9. Qd2 O-O 10. O-O-O Nbd7 "
    "11. g4 b5 12. g5 b4 13. Ne2 Ne8 14. f4 a5 15. f5 a4";
const char* const SHORT =
    "1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 6. Be3 e5 7. Nb3 Be6 8. f3 Be7 9. Qd2 O-O 10. O-O-O Nbd7";
const char* const OTHER =
    "1. d4 Nf6 2. c4 e6 3. Nc3 Bb4 4. e3 O-O 5. Bd3 d5 6. Nf3 c5 7. O-O dxc4 8. Bxc4 Nbd7 9. Qe2 b6 10. Rd1 cxd4 "
    "11. exd4 Bb7";

std::string resultText(char r) { return r == '1' ? "1-0" : r == '0' ? "0-1" : r == '=' ? "1/2-1/2" : "*"; }

// A game as a source writes it: its tags as given, its moves, its result.
ImportedGame game(const std::vector<std::pair<std::string, std::string>>& tags, const std::string& moves, char result,
                  const std::string& comments = {}) {
    std::string text;
    for (const auto& [k, v] : tags) text += "[" + k + " \"" + v + "\"]\n";
    text += "[Result \"" + resultText(result) + "\"]\n\n" + moves + comments + " " + resultText(result) + "\n";
    ImportedGame g;
    REQUIRE(readImportedGame(text, g));
    return g;
}

// The same game as TWIC and a Lichess broadcast write it.
ImportedGame twic(const std::string& white, const std::string& black, const std::string& round,
                  const std::string& moves = MOVES, char result = '=') {
    return game({{"Event", "Autumn Open"},
                 {"Site", "Lyon FRA"},
                 {"Date", "2026.10.20"},
                 {"Round", round},
                 {"White", white},
                 {"Black", black},
                 {"WhiteElo", "2341"},
                 {"BlackElo", "2398"},
                 {"ECO", "B90"}},
                moves, result);
}

ImportedGame broadcast(const std::string& white, const std::string& black, const std::string& round,
                       const std::string& moves = MOVES, char result = '=') {
    return game({{"Event", "Autumn Open 2026"},
                 {"Site", "https://lichess.org/broadcast/autumn-open/round-9/abcd1234"},
                 {"Date", "2026.10.20"},
                 {"Round", round},
                 {"White", white},
                 {"Black", black},
                 {"WhiteElo", "2341"},
                 {"BlackElo", "2398"},
                 {"WhiteFideId", "90000001"},
                 {"BlackFideId", "90000002"}},
                moves, result, " { [%clk 1:30:00] Played at once. }");
}

std::vector<GameId> groupOf(const Duplicates& found, GameId id) {
    for (const auto& g : found.groups)
        if (std::find(g.games.begin(), g.games.end(), id) != g.games.end()) return g.games;
    return {};
}

}  // namespace

TEST_CASE("maintenance: each game's source is the file it was imported from, kept as people name it") {
    const ScratchDir folder("maint-sources");
    auto db = Database::create(folder);
    db->append({twic("Moreau, Claire", "Lindqvist, Erik", "9")}, false, "/tmp/twic1672g.zip");
    db->append({broadcast("Moreau, Claire", "Lindqvist, Erik", "9.2")}, false, "");
    auto snap = db->snapshot();
    CHECK(snap->source(0) == "/tmp/twic1672g.zip");
    CHECK(snap->source(1).empty());
    CHECK(sourceLabel(snap->source(0), snap->game(0)) == "TWIC 1672");
    CHECK(sourceLabel(snap->source(1), snap->game(1)) == "Lichess broadcast");
    CHECK(sourceLabel("/x/lichess_db_broadcast_2026-10.pgn.zst", ImportedGame{}) == "Lichess broadcasts 2026-10");
    CHECK(sourceLabel("C:\\games\\club.pgn", ImportedGame{}) == "club.pgn");
    CHECK(snap->tag(1, "WhiteFideId") == "90000001");
    CHECK(snap->tag(0, "WhiteFideId").empty());

    // A game replaced keeps its first source; the program opened again knows them.
    db->replace(0, twic("Moreau, Claire", "Lindqvist, Erik", "9", OTHER));
    const auto dir = db->folder();
    db.reset();
    db   = Database::open(dir);
    snap = db->snapshot();
    CHECK(snap->source(0) == "/tmp/twic1672g.zip");

    // An import keeps its file.
    const auto file = fs::temp_directory_path() / "maint-sources.pgn";
    {
        std::ofstream out(file);
        out << importedPgn(twic("Okafor, Ada", "Duval, Marc", "3", OTHER, '0'));
    }
    ImportProgress progress;
    std::atomic<bool> cancel{false};
    const auto report = importInto(*db, file, ImportOptions{1, 4096}, progress, cancel);
    CHECK(report.added == 1);
    const auto u8 = file.u8string();
    CHECK(db->snapshot()->source(2) == std::string(u8.begin(), u8.end()));
}

TEST_CASE("maintenance: duplicates from two sources found by players, year, round and moves, whatever headers") {
    std::vector<ImportedGame> games{
        twic("Moreau,C", "Lindqvist,E", "9"),                         // 0
        broadcast("Moreau, Claire", "Lindqvist, Erik", "9.2"),        // 1: game 0 from a broadcast
        twic("Brennan, Liam", "Novak, Ivo", "5"),                     // 2
        broadcast("Brennan, Liam", "Novak, Ivo", "5.1", SHORT, '*'),  // 3: game 2 cut short
        twic("Ito, Ken", "Martin, Paul", "6", OTHER, '0'),            // 4
        broadcast("Ito, Ken", "Martin, Paul", "6.3", OTHER, '1'),     // 5: the results differ
        twic("Ferreira, Rui", "Haddad, Sami", "5"),                   // 6
        broadcast("Ferreira, Rui", "Haddad, Sami", "7.1"),            // 7: another round
        twic("Okafor, Ada", "Duval, Marc", "3", SHORT, '='),          // 8: a short game that ended
        broadcast("Okafor, Ada", "Duval, Marc", "3"),                 // 9: is not the start of this one
        twic("NN", "NN", "1"),                                        // 10: no players
        broadcast("NN", "NN", "1"),                                   // 11
        twic("Moreau, Claire", "Lindqvist, Erik", "9", MOVES, '=')};  // 12: game 0 a third time
    auto db = database("maint-dupes", games);
    std::atomic<bool> cancel{false};
    DuplicateProgress progress;
    auto found = findDuplicates(db->snapshot(), 0, progress, cancel);
    CHECK_FALSE(found.cancelled);
    CHECK(groupOf(found, 0) == std::vector<GameId>{0, 1, 12});
    CHECK(groupOf(found, 2) == std::vector<GameId>{2, 3});
    CHECK(groupOf(found, 4) == std::vector<GameId>{4, 5});
    CHECK(groupOf(found, 6).empty());
    CHECK(groupOf(found, 8).empty());
    CHECK(groupOf(found, 10).empty());
    REQUIRE(found.groups.size() == 3);
    CHECK(found.groups[0].kind == DuplicateKind::SameMoves);
    CHECK(found.groups[1].kind == DuplicateKind::Shorter);
    CHECK(found.groups[2].kind == DuplicateKind::Results);
    CHECK(found.count(DuplicateKind::SameMoves) == 1);

    // Only the groups of an import's games: those numbered 12 or more.
    auto since = findDuplicates(db->snapshot(), 12, progress, cancel);
    REQUIRE(since.groups.size() == 1);
    CHECK(since.groups[0].games == std::vector<GameId>{0, 1, 12});

    // Deleted games and pairs kept apart are not offered.
    db->setDeleted(std::vector<GameId>{12}, true);
    db->setPlayers(PlayerTable({}, {{3, 2}}));
    found = findDuplicates(db->snapshot(), 0, progress, cancel);
    CHECK(groupOf(found, 0) == std::vector<GameId>{0, 1});
    CHECK(groupOf(found, 2).empty());

    // Cancelled at once: nothing.
    cancel = true;
    CHECK(findDuplicates(db->snapshot(), 0, progress, cancel).cancelled);

    CHECK(roundsCompatible("9", "9.2"));
    CHECK(roundsCompatible("09", "9"));
    CHECK(roundsCompatible("?", "4"));
    CHECK_FALSE(roundsCompatible("5", "7.1"));
    CHECK(datesCompatible(packDate("2026.10.??"), packDate("2026.10.20")));
    CHECK_FALSE(datesCompatible(packDate("2026.10.21"), packDate("2026.10.20")));
}

TEST_CASE("maintenance: a group merged into its fullest fields, the others deleted, undone by Ctrl+Z") {
    std::vector<ImportedGame> games{
        twic("Moreau,C", "Lindqvist,E", "9"), broadcast("Moreau, Claire", "Lindqvist, Erik", "9.2"),
        twic("Brennan, Liam", "Novak, Ivo", "5"), broadcast("Brennan, Liam", "Novak, Ivo", "5.1", SHORT, '*')};
    auto db   = database("maint-merge", games);
    auto snap = db->snapshot();
    const std::vector<ImportedGame> pair{snap->game(0), snap->game(1)};
    const auto choice = fullestChoice(pair);
    CHECK(choice[static_cast<std::size_t>(MergeField::White)] == 1);  // a name in full over an initial
    CHECK(choice[static_cast<std::size_t>(MergeField::Round)] == 1);  // 9.2 says more than 9
    CHECK(choice[static_cast<std::size_t>(MergeField::Event)] == 1);
    CHECK(choice[static_cast<std::size_t>(MergeField::Eco)] == 0);  // TWIC's
    CHECK(choice[static_cast<std::size_t>(MergeField::FideIds)] == 1);
    CHECK(choice[static_cast<std::size_t>(MergeField::Clocks)] == 1);
    CHECK(choice[static_cast<std::size_t>(MergeField::Moves)] == 1);  // the same moves, the annotated one's
    const auto fields = mergeFields(pair);
    CHECK(fields[0][static_cast<std::size_t>(MergeField::Eco)] == "B90");
    CHECK(fields[1][static_cast<std::size_t>(MergeField::Eco)].empty());
    CHECK(fields[1][static_cast<std::size_t>(MergeField::Clocks)] == "1");

    auto merged    = mergeGames(pair, choice, {{0, "TWIC 1672"}, {1, "Lichess broadcast"}});
    const auto tag = [](const ImportedGame& g, std::string_view name) -> std::string {
        for (const auto& [k, v] : g.tags)
            if (k == name) return v;
        return {};
    };
    CHECK(tag(merged, "White") == "Moreau, Claire");
    CHECK(tag(merged, "Round") == "9.2");
    CHECK(tag(merged, "ECO") == "B90");
    CHECK(tag(merged, "WhiteFideId") == "90000001");
    CHECK(tag(merged, "Site") == "Lyon FRA");                                // a place over an address,
    CHECK(tag(merged, "GameURL").find("lichess.org") != std::string::npos);  // the broadcast keeping its source
    CHECK(tag(merged, "MergedFrom").find("1 TWIC 1672: Site, Date, ratings, ECO, result") != std::string::npos);
    CHECK(tag(merged, "MergedFrom").find("2 Lichess broadcast: White, Black") != std::string::npos);
    CHECK(merged.moves == pair[0].moves);
    CHECK_FALSE(merged.clocks.empty());
    // Its text, read again, has the merged tags and the comments.
    ImportedGame again;
    REQUIRE(readImportedGame(importedPgn(merged), again));
    CHECK(tag(again, "ECO") == "B90");
    CHECK(tag(again, "Round") == "9.2");
    CHECK(again.moves == merged.moves);

    // A cut broadcast: the moves of the longer game, the result of the game that ended.
    const std::vector<ImportedGame> cut{snap->game(2), snap->game(3)};
    const auto longer = mergeGames(cut, fullestChoice(cut));
    CHECK(longer.plies == cut[0].plies);
    CHECK(longer.result == '=');

    // Merged in the database: game 0 keeps its number, game 1 deleted; then undone.
    const auto before = db->merge({{0, merged, {1}}});
    snap              = db->snapshot();
    CHECK(snap->deleted(1));
    CHECK_FALSE(snap->deleted(0));
    CHECK(tag(snap->game(0), "ECO") == "B90");
    CHECK(tag(snap->game(0), "WhiteFideId") == "90000001");
    CHECK(snap->games() == 4);
    CHECK(makeView(snap, Filter{}, SortBy::Number).ids == std::vector<GameId>{0, 2, 3});
    db->restore(before);
    snap = db->snapshot();
    CHECK_FALSE(snap->deleted(1));
    CHECK(tag(snap->game(0), "WhiteFideId").empty());
    CHECK(tag(snap->game(0), "White") == "Moreau,C");

    // Merged again, the program opened again: as merged.
    db->merge({{0, merged, {1}}});
    const auto dir = db->folder();
    db.reset();
    db   = Database::open(dir);
    snap = db->snapshot();
    CHECK(snap->deleted(1));
    CHECK(tag(snap->game(0), "Round") == "9.2");
    CHECK_THROWS_AS(db->merge({{9, merged, {1}}}), std::out_of_range);
}

TEST_CASE("maintenance: the table of names, read by the list, its filters and sorts; the games untouched") {
    std::vector<ImportedGame> games{
        twic("Lindqvist,E", "Brennan, Liam", "1"), twic("Brennan, Liam", "Lindqvist, Erik", "2", OTHER),
        twic("Adams, Zoe", "Erik Lindqvist", "3", SHORT), twic("Kowalenko, Petro", "Adams, Zoe", "4", OTHER)};
    auto db       = database("maint-table", games);
    auto snap     = db->snapshot();
    const auto id = [&](GameId g, bool white) {
        const auto ids = snap->rowIds(g);
        return white ? ids.white : ids.black;
    };
    Player erik;
    erik.name      = "Lindqvist, Erik";
    erik.fide      = 90000002;
    erik.how       = "list";
    erik.spellings = {id(0, true), id(1, false), id(2, false)};
    db->setPlayers(PlayerTable({erik}));
    snap = db->snapshot();
    CHECK(snap->shownName(id(0, true)) == "Lindqvist, Erik");
    CHECK(snap->shownName(id(2, false)) == "Lindqvist, Erik");
    CHECK(snap->shownName(id(0, false)) == "Brennan, Liam");
    CHECK(snap->row(0).white == "Lindqvist,E");  // the game as it was written
    CHECK(snap->players()->of(id(2, false))->fide == 90000002);

    // A word of the player's name finds every spelling; sorted by White, the player's games together.
    Filter filter;
    filter.player = "erik";
    CHECK(makeView(snap, filter, SortBy::Number).ids == std::vector<GameId>{0, 1, 2});
    filter.player.clear();
    filter.white = "lindqvist";
    CHECK(makeView(snap, filter, SortBy::Number).ids == std::vector<GameId>{0});
    // Sorted by Black, the player's two spellings together, under his name.
    const auto sorted = makeView(snap, Filter{}, SortBy::Black).ids;
    CHECK(sorted == std::vector<GameId>{3, 0, 1, 2});

    // Opened again: the table is read with the database; the tree is not stale for it.
    TreeProgress progress;
    std::atomic<bool> cancel{false};
    REQUIRE(buildTree(*db, *snap, {}, progress, cancel).error.empty());
    const auto dir = db->folder();
    db.reset();
    db   = Database::open(dir);
    snap = db->snapshot();
    REQUIRE(snap->players()->players().size() == 1);
    CHECK(snap->players()->players()[0].spellings.size() == 3);
    db->setPlayers(PlayerTable({erik}, {{0, 1}}));
    const auto tree = Tree::open(dir);
    REQUIRE(tree);
    CHECK_FALSE(tree->stale(*db->snapshot()));
    int tables = 0;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().filename().string().rfind("players-", 0) == 0) ++tables;
    CHECK(tables == 1);  // the table before is gone

    CHECK_THROWS((void)PlayerTable::fromBytes("{\"format\":\"other\"}"));
    CHECK_THROWS((void)PlayerTable::fromBytes(PlayerTable({erik}).bytes().substr(0, 30)));
    const auto back = PlayerTable::fromBytes(PlayerTable({erik}, {{4, 1}}).bytes());
    REQUIRE(back.players().size() == 1);
    CHECK(back.players()[0].name == "Lindqvist, Erik");
    CHECK(back.players()[0].how == "list");
    CHECK(back.keptApart(1, 4));
}

TEST_CASE("maintenance: names read for matching, their parts and their transliterations") {
    auto p = nameParts("Lindqvist, Erik");
    CHECK(p.surname == "lindqvist");
    CHECK(p.given == "erik");
    CHECK(p.initial == 'e');
    CHECK(nameParts("Lindqvist,E").given == "e");
    CHECK(nameParts("Lindqvist E.").surname == "lindqvist");
    CHECK(nameParts("Lindqvist E.").given == "e");
    CHECK(nameParts("E. Lindqvist").surname == "lindqvist");
    CHECK(nameParts("Erik Lindqvist").surname == "lindqvist");
    CHECK(nameParts("Erik Lindqvist").given == "erik");
    CHECK(nameParts("van der Berg, Jan").surname == "vanderberg");
    CHECK(nameParts("Müller, Jürgen").surname == "muller");
    CHECK(givenCompatible("e", "erik"));
    CHECK(givenCompatible("erik", "erik johan"));
    CHECK_FALSE(givenCompatible("emma", "erik"));
    CHECK(givenCompatible("", "erik"));
    CHECK(givenCompatible("dmitrij", "dmitry"));
    CHECK(nameSkeleton("kowalenko") == nameSkeleton("kovalenko"));
    CHECK(nameSkeleton("aleksandr") == nameSkeleton("alexandr"));
    CHECK(anonymousName("NN"));
    CHECK(anonymousName("?"));
    CHECK_FALSE(anonymousName("Ng, Wei"));
}

namespace {

std::vector<ImportedGame> allGames(const Snapshot& snap) {
    std::vector<ImportedGame> out;
    for (GameId id = 0; id < snap.games(); ++id) out.push_back(snap.deleted(id) ? ImportedGame{} : snap.game(id));
    return out;
}

bool sameGames(const std::vector<ImportedGame>& a, const std::vector<ImportedGame>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].tags != b[i].tags || a[i].moves != b[i].moves || a[i].plies != b[i].plies || a[i].text != b[i].text ||
            a[i].clocks != b[i].clocks || a[i].evals != b[i].evals || a[i].start_fen != b[i].start_fen)
            return false;
    return true;
}

int folders(const fs::path& dir, const std::string& prefix) {
    int n = 0;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().filename().string().rfind(prefix, 0) == 0) ++n;
    return n;
}

void overwrite(const fs::path& file, std::size_t at, char byte) {
    std::string bytes;
    {
        std::ifstream in(file, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    REQUIRE(at < bytes.size());
    bytes[at] = byte;
    // Written beside it and renamed: the database's mapping keeps the old file.
    const auto tmp = fs::path(file.string() + ".new");
    {
        std::ofstream out(tmp, std::ios::binary);
        out << bytes;
    }
    fs::rename(tmp, file);
}

// A database of three segments, with games deleted, replaced and merged: what a compaction takes away.
std::shared_ptr<Database> edited(const std::string& name) {
    auto games = randomGames(30, 21, 6);
    Spec fen;
    fen.moves = "1. Kd2 Kd7 2. Ke3 Ke6";
    fen.fen   = "4k3/8/8/8/8/8/8/4K3 w - - 0 1";
    games.push_back(make(fen, 30));
    auto db = database(name, {games.begin(), games.begin() + 10});
    db->append({games.begin() + 10, games.begin() + 20}, true);
    db->append({games.begin() + 20, games.end()}, true);
    db->setDeleted(std::vector<GameId>{2, 3}, true);
    db->replace(12, games[5]);
    db->replace(12, games[6]);  // two versions: the first is no longer read
    auto merged = db->snapshot()->game(21);
    merged.tags.emplace_back("Annotator", "Ada");
    db->merge({{21, merged, {22}}});
    return db;
}

}  // namespace

TEST_CASE("maintenance: a database checked, its damaged games and segments found, a sound one sound") {
    auto db = edited("maint-check");
    CheckProgress progress;
    std::atomic<bool> cancel{false};
    auto report = db->check(progress, cancel, 2);
    CHECK(report.segments.empty());
    CHECK(report.damaged.empty());
    CHECK_FALSE(report.cancelled);
    CHECK(report.signatures);
    CHECK_FALSE(report.tree);
    CHECK(progress.done.load() == progress.total.load());
    TreeProgress tree;
    REQUIRE(buildTree(*db, *db->snapshot(), {}, tree, cancel).error.empty());
    CHECK(db->check(progress, cancel).tree);

    // A byte of a game's moves changed on the disk: that game read differently. (The database closed meanwhile:
    // Windows replaces no file a program maps.)
    const auto dir = db->folder();
    std::string first, second;
    std::size_t offset = 0;
    char byte          = 0;
    {
        const auto snap  = db->snapshot();
        first            = snap->segmentInfos().front().name;
        second           = snap->segmentInfos()[1].name;
        const auto moves = snap->moves(5);
        REQUIRE(moves.size() > 3);
        byte = static_cast<char>(moves[2] ^ 0x01);
        for (GameId id = 0; id < 5; ++id) offset += snap->moves(id).size();
    }
    db.reset();
    overwrite(dir / first / "moves.bin", offset + 2, byte);
    db     = Database::open(dir);
    report = db->check(progress, cancel);
    CHECK(report.damaged == std::vector<GameId>{5});
    CHECK(report.segments.empty());
#ifndef _WIN32
    // A segment's columns changed while the database is open (Windows replaces no file a program maps): its games, and
    // why.
    overwrite(dir / second / "cols.bin", 0, 'X');
    report = db->check(progress, cancel);
    REQUIRE(report.segments.size() == 1);
    CHECK(report.segments[0].name == second);
    CHECK(report.segments[0].first == 10);
    CHECK(report.damaged.size() == 1 + 9);  // game 5, and the second segment's but game 12, read from its version

    // "Keep what can be read": the damaged games deleted; the database checked again is sound.
    db->setDeleted(report.damaged, true);
    report = db->check(progress, cancel);
    CHECK(report.damaged.empty());
    CHECK(report.segments.size() == 1);  // its files still cannot be read: a compaction writes them again
    CompactProgress compacting;
    REQUIRE(db->compact(compacting, cancel).done);
    report = db->check(progress, cancel);
    CHECK(report.segments.empty());
    CHECK(report.damaged.empty());
#else
    (void)second;
#endif
    cancel = true;
    CHECK(db->check(progress, cancel).cancelled);
}

TEST_CASE("maintenance: a game's signature changed on the disk is found by the check, not a bit it does not use") {
    auto db        = edited("maint-signature");
    const auto dir = db->folder();
    const auto seg = db->snapshot()->segmentInfos().front().name;
    db.reset();
    // Game 4's 24 bytes: the plies its home pawns left at, its material, its flags (known, promoted), two unused.
    const auto sig      = dir / seg / "sig.bin";
    const auto original = readFile(sig);
    struct Change {
        std::size_t at = 0;
        uint8_t bits   = 0;
        bool found     = false;
    };
    for (const auto& c : std::vector<Change>{{3, 0x01, true},   // the ply the d-pawn left at
                                             {17, 0x10, true},  // Black's knights
                                             {21, 0x02, true},  // promoted
                                             {21, 0x04, false},
                                             {22, 0xFF, false}}) {
        CAPTURE(c.at);
        const auto at = 4 * 24 + c.at;
        overwrite(sig, at, static_cast<char>(original[at] ^ c.bits));
        CheckProgress progress;
        std::atomic<bool> cancel{false};
        const auto report = Database::open(dir)->check(progress, cancel);
        CHECK(report.damaged == (c.found ? std::vector<GameId>{4} : std::vector<GameId>{}));
        overwrite(sig, at, original[at]);
    }
}

TEST_CASE("maintenance: a compaction takes the games deleted and the old versions out, numbers and tree kept") {
    auto db        = edited("maint-compact");
    const auto dir = db->folder();
    std::atomic<bool> cancel{false};
    TreeProgress tree_progress;
    REQUIRE(buildTree(*db, *db->snapshot(), {}, tree_progress, cancel).error.empty());
    const auto before = allGames(*db->snapshot());
    const auto plan   = db->compactPlan();
    CHECK(plan.deleted == 3);   // 2, 3 and the merged 22
    CHECK(plan.replaced == 2);  // 12 and 21
    CHECK(plan.bytes > 0);
    CHECK(plan.segments.size() == 3 + 3);  // the three segments, and the three of versions
    const auto edits = db->snapshot()->editsGeneration();

    CompactProgress progress;
    auto done = db->compact(progress, cancel);
    REQUIRE(done.error.empty());
    CHECK(done.done);
    CHECK(done.undoable);
    CHECK(done.bytes > 0);
    CHECK(done.bytes == plan.bytes);  // what the plan said
    auto snap = db->snapshot();
    CHECK(sameGames(allGames(*snap), before));
    CHECK(snap->deleted(2));
    CHECK(snap->deleted(22));
    CHECK(snap->editsGeneration() == edits);
    const auto tree = Tree::open(dir);
    REQUIRE(tree);
    CHECK_FALSE(tree->stale(*snap));
    CHECK(db->compactPlan().segments.empty());  // nothing more to take
    CheckProgress check;
    CHECK(db->check(check, cancel).damaged.empty());
    CHECK(folders(dir, "seg-") == 6 + 6);  // the old ones kept until the database is closed

    // Undone: the files before back; then done again, and the database closed: the old files go. (Its snapshot let go
    // first: Windows removes no file a program maps.)
    snap.reset();
    CHECK(db->compactionUndoable());
    CHECK(db->undoCompaction());
    CHECK(sameGames(allGames(*db->snapshot()), before));
    CHECK(folders(dir, "seg-") == 6);
    CHECK(db->compactPlan().deleted == 3);
    done = db->compact(progress, cancel);
    CHECK(done.done);
    db.reset();
    CHECK(folders(dir, "seg-") == 6);
    db = Database::open(dir);
    CHECK(sameGames(allGames(*db->snapshot()), before));
    CHECK_FALSE(db->compactionUndoable());

    // Anything committed after a compaction: no undoing it.
    auto fresh_db = edited("maint-compact-2");
    CHECK(fresh_db->compact(progress, cancel).undoable);
    fresh_db->setDeleted(std::vector<GameId>{0}, true);
    CHECK_FALSE(fresh_db->compactionUndoable());
    CHECK_FALSE(fresh_db->undoCompaction());
}

TEST_CASE("maintenance: a compaction one segment at a time, a cancel, and a crash at any of its steps") {
    // One at a time: each segment's old files go at once.
    auto db           = edited("maint-compact-one");
    const auto dir    = db->folder();
    const auto before = allGames(*db->snapshot());
    std::atomic<bool> cancel{false};
    CompactProgress progress;
    const auto done = db->compact(progress, cancel, true);
    CHECK(done.done);
    CHECK_FALSE(done.undoable);
    CHECK(folders(dir, "seg-") == 6);
    CHECK(sameGames(allGames(*db->snapshot()), before));
    CHECK(progress.segments.load() == 6);

    // Cancelled before it starts: nothing changes.
    auto other           = edited("maint-compact-cancel");
    cancel               = true;
    const auto cancelled = other->compact(progress, cancel);
    CHECK(cancelled.cancelled);
    CHECK_FALSE(cancelled.done);
    CHECK(folders(other->folder(), "seg-") == 6);
    CHECK(other->compactPlan().deleted == 3);
    cancel = false;

    // A crash at each step: the database opens with the games as they were, compacted or not.
    struct Crash {};
    std::vector<std::string> steps;
    {
        auto probe = edited("maint-compact-steps");
        setCommitFaultHook([&](std::string_view step) { steps.emplace_back(step); });
        (void)probe->compact(progress, cancel);
        setCommitFaultHook({});
    }
    REQUIRE(steps.size() > 5);
    for (std::size_t k = 0; k < steps.size(); ++k) {
        auto victim      = edited("maint-compact-crash");
        const auto path  = victim->folder();
        const auto was   = allGames(*victim->snapshot());
        std::size_t seen = 0;
        setCommitFaultHook([&](std::string_view) {
            if (seen++ == k) throw Crash{};
        });
        CHECK_THROWS_AS((void)victim->compact(progress, cancel), Crash);
        setCommitFaultHook({});
        victim.reset();
        auto again = Database::open(path);
        CHECK(sameGames(allGames(*again->snapshot()), was));
        CHECK(folders(path, "seg-") == 6);
        CheckProgress check;
        CHECK(again->check(check, cancel).damaged.empty());
    }
}

TEST_CASE("maintenance: games added while a compaction runs are kept; a game deleted meanwhile stops it") {
    auto db           = edited("maint-compact-meanwhile");
    const auto before = allGames(*db->snapshot());
    const auto added  = randomGames(3, 99, 6);
    std::atomic<bool> cancel{false};
    CompactProgress progress;
    bool done_once = false;
    setCommitFaultHook([&](std::string_view step) {
        if (step != "rename segment" || done_once) return;
        done_once = true;
        db->append(added, true);  // while the compaction writes its segments
    });
    const auto compacted = db->compact(progress, cancel);
    setCommitFaultHook({});
    REQUIRE(compacted.error.empty());
    CHECK(compacted.done);
    CHECK_FALSE(compacted.undoable);  // undoing it would take the games added away
    auto snap = db->snapshot();
    REQUIRE(snap->games() == before.size() + 3);
    auto now = allGames(*snap);
    CHECK(sameGames({now.begin(), now.begin() + static_cast<std::ptrdiff_t>(before.size())}, before));
    CHECK(now.back().moves == added.back().moves);
    CHECK(db->compactPlan().segments.empty());

    // A game deleted meanwhile: stopped before its commit, nothing else changed.
    auto other = edited("maint-compact-deleted");
    done_once  = false;
    setCommitFaultHook([&](std::string_view step) {
        if (step != "rename segment" || done_once) return;
        done_once = true;
        other->setDeleted(std::vector<GameId>{7}, true);
    });
    const auto stopped = other->compact(progress, cancel);
    setCommitFaultHook({});
    CHECK_FALSE(stopped.done);
    CHECK(stopped.error.find("meanwhile") != std::string::npos);
    CHECK(other->snapshot()->deleted(7));
    CHECK(folders(other->folder(), "seg-") == 6);
    CHECK(other->compact(progress, cancel).done);  // again, now that nothing changes
}
