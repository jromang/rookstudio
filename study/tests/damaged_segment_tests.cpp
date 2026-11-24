// A database whose segment no longer reads when it opens (#97, mockup M13, board 1): a file of the segment cut short,
// removed, or changed in its header while the database was closed. It opens with its other segments, the damaged
// segment's games keeping their numbers; "Keep what can be read" writes the segment again as games set aside, undone
// until the database is closed; a database read-only is never changed; a crash at any step of it leaves a database
// that opens. Games of our own, players' names invented.
#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifndef _WIN32
#    include <sys/wait.h>
#    include <unistd.h>

#    include <csignal>
#endif

#include <chess.hpp>
#include <study/linked.hpp>
#include <study/query.hpp>
#include <study/scan_board.hpp>
#include <study/search.hpp>
#include <study/store.hpp>
#include <study/tree.hpp>

#include "test_games.hpp"

using namespace fcg::study;
using namespace fcg::test;
namespace fs = std::filesystem;

namespace {

struct Crash {};

// Ten games of a tournament: its event and site, two of `players` a game, each its own moves (twenty plies at least),
// an opening and a comment (what a segment's annex keeps). `all_names`: every column a name (annotator, time control),
// none empty. Their text, and the games as read.
struct Tournament {
    std::string text;
    std::vector<ImportedGame> games;
};
Tournament tournament(const std::string& event, const std::string& site, const std::vector<std::string>& players,
                      uint32_t seed, bool all_names = false) {
    Tournament t;
    std::vector<std::vector<std::string>> lines;
    for (const auto& line : randomLines(60, seed, 4, 70))
        if (line.size() >= 20 && lines.size() < 10) lines.push_back(line);
    REQUIRE(lines.size() == 10);
    const char* results[] = {"1-0", "0-1", "1/2-1/2"};
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string result = results[i % 3];
        const auto day           = std::to_string(i + 1);
        std::string text = "[Event \"" + event + "\"]\n[Site \"" + site + "\"]\n[Date \"2026.10." + (i < 9 ? "0" : "") +
                           day + "\"]\n[Round \"" + day + "\"]\n[White \"" + players[i % players.size()] +
                           "\"]\n[Black \"" + players[(i + 1) % players.size()] + "\"]\n[Result \"" + result +
                           "\"]\n[WhiteElo \"" + std::to_string(2000 + 10 * i) + "\"]\n[Opening \"Line " +
                           std::to_string(seed) + "." + day + "\"]\n";
        if (all_names) text += "[Annotator \"" + event + " team\"]\n[TimeControl \"5400+30\"]\n";
        text += "\n{ A game of our own. } " + movetext(lines[i]) + result + "\n\n";
        ImportedGame g;
        REQUIRE(readImportedGame(text, g));
        REQUIRE_FALSE(g.damaged);
        t.text += text;
        t.games.push_back(std::move(g));
    }
    return t;
}

std::string utf8(const fs::path& path) {
    const auto u8 = path.u8string();
    return {u8.begin(), u8.end()};
}

// The names the second import brings: its event, its site and its players; the third reads some of them.
const std::set<std::string> SECOND_NAMES = {"Autumn Cup", "Nantes", "Okafor, Ada", "Duval, Marc", "Ferreira, Rui"};

// A database of three imports of ten games each, the second's file kept beside it (to be imported again): games 0-9,
// 10-19 (the second segment) and 20-29, whose games use names the second brought.
struct Made {
    std::shared_ptr<Database> db;
    fs::path dir, autumn;             // the database, the second import's file
    std::vector<ImportedGame> games;  // as the database read them
    std::string second;               // the second segment's name
};

Made threeImports(const std::string& name, bool all_names = false) {
    Made m;
    m.dir    = fs::temp_directory_path() / (name + ".fcgdb");
    m.autumn = fs::temp_directory_path() / (name + "-autumn-cup.pgn");
    fs::remove_all(m.dir);
    const auto first =
        tournament("Spring Open", "Lyon", {"Moreau, Claire", "Lindqvist, Erik", "Brennan, Kate"}, 11, all_names);
    const auto second =
        tournament("Autumn Cup", "Nantes", {"Okafor, Ada", "Duval, Marc", "Ferreira, Rui"}, 12, all_names);
    const auto third =
        tournament("Autumn Cup", "Nantes", {"Okafor, Ada", "Duval, Marc", "Haddad, Lina"}, 13, all_names);
    std::ofstream(m.autumn, std::ios::binary) << second.text;
    m.db = Database::create(m.dir);
    m.db->append(first.games, true, "/x/spring-open.pgn");
    ImportProgress progress;
    std::atomic<bool> cancel{false};
    REQUIRE(importInto(*m.db, m.autumn, ImportOptions{2, 4096}, progress, cancel, true).added == 10);
    m.db->append(third.games, true, "/x/winter.pgn");
    const auto snap = m.db->snapshot();
    REQUIRE(snap->games() == 30);
    REQUIRE(snap->segmentInfos().size() == 3);
    m.second = snap->segmentInfos()[1].name;
    for (GameId id = 0; id < 30; ++id) m.games.push_back(snap->game(id));
    return m;
}

// A copy of a database's folder, for one try.
fs::path copyOf(const fs::path& from, const std::string& name) {
    const auto to = fs::temp_directory_path() / (name + ".fcgdb");
    fs::remove_all(to);
    fs::copy(from, to, fs::copy_options::recursive);
    return to;
}

std::string bytesOf(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeBytes(const fs::path& file, const std::string& bytes) { std::ofstream(file, std::ios::binary) << bytes; }

// What a database's folder holds, file by file.
std::map<std::string, std::string> contents(const fs::path& dir) {
    std::map<std::string, std::string> out;
    for (const auto& e : fs::recursive_directory_iterator(dir))
        out[utf8(fs::relative(e.path(), dir))] = e.is_regular_file() ? bytesOf(e.path()) : std::string("(folder)");
    return out;
}

int folders(const fs::path& dir, const std::string& prefix) {
    int n = 0;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().filename().string().rfind(prefix, 0) == 0) ++n;
    return n;
}

// A game as it should read: the names the second segment brought read "?" when they were lost with it.
void sameGame(const ImportedGame& read, const ImportedGame& was, bool names_lost) {
    REQUIRE(read.tags.size() == was.tags.size());
    for (std::size_t t = 0; t < was.tags.size(); ++t) {
        CHECK(read.tags[t].first == was.tags[t].first);
        const bool lost = names_lost && SECOND_NAMES.count(was.tags[t].second) > 0;
        CHECK(read.tags[t].second == (lost ? std::string("?") : was.tags[t].second));
    }
    CHECK(read.moves == was.moves);
    CHECK(read.plies == was.plies);
    CHECK(read.text == was.text);
    CHECK(read.result == was.result);
}

// The games that read, by number: all but the second segment's.
std::vector<GameId> others() {
    std::vector<GameId> ids;
    for (GameId id = 0; id < 30; ++id)
        if (id < 10 || id >= 20) ids.push_back(id);
    return ids;
}

CheckReport checked(Database& db) {
    CheckProgress progress;
    std::atomic<bool> cancel{false};
    return db.check(progress, cancel, 2);
}

enum class Harm { Cut, Removed, Header };
struct Case {
    std::string file;  // empty: the segment's folder
    Harm how        = Harm::Cut;
    std::size_t at  = 0;  // Header: the byte changed, and to what
    char byte       = 0;
    bool names_lost = false;
};

void damage(const fs::path& segment, const Case& c) {
    const auto file = c.file.empty() ? segment : segment / c.file;
    switch (c.how) {
        case Harm::Cut: {
            const auto bytes = bytesOf(file);
            REQUIRE(bytes.size() > 1);
            writeBytes(file, bytes.substr(0, bytes.size() / 2));
            break;
        }
        case Harm::Removed:
            REQUIRE(fs::remove_all(file) > 0);
            break;
        case Harm::Header: {
            auto bytes = bytesOf(file);
            REQUIRE(c.at < bytes.size());
            bytes[c.at] = c.byte;
            writeBytes(file, bytes);
            break;
        }
    }
}

}  // namespace

TEST_CASE("damaged segment: each file cut short, removed or changed in its header; the others read, then kept") {
    const std::vector<Case> cases = {
        {"cols.bin", Harm::Cut},
        {"moves.bin", Harm::Cut},
        {"annex.bin", Harm::Cut},
        {"names.bin", Harm::Cut, 0, 0, true},
        {"sig.bin", Harm::Cut},
        {"cols.bin", Harm::Removed},
        {"moves.bin", Harm::Removed},
        {"annex.bin", Harm::Removed},
        {"names.bin", Harm::Removed, 0, 0, true},
        {"sig.bin", Harm::Removed},
        {"", Harm::Removed, 0, 0, true},     // the whole folder
        {"cols.bin", Harm::Header, 0, 'X'},  // no segment's
        {"cols.bin", Harm::Header, 8, 9},    // a newer format
        {"cols.bin", Harm::Header, 12, 11},  // not the segment the manifest names: 11 games
        {"cols.bin", Harm::Header, 16, 4},   // nor 4 names
    };
    for (const auto& c : cases) {
        CAPTURE(c.file);
        CAPTURE(static_cast<int>(c.how));
        CAPTURE(c.at);
        auto m = threeImports("damaged-each");
        m.db.reset();  // closed: damaged while RookStudio was closed
        damage(m.dir / m.second, c);

        // It opens, with the other segments; the second's games keep their numbers.
        auto db   = Database::open(m.dir);
        auto snap = db->snapshot();
        REQUIRE(snap->unreadable().size() == 1);
        const auto& bad = snap->unreadable().front();
        CHECK(bad.name == m.second);
        CHECK(bad.first == 10);
        CHECK(bad.games == 10);
        CHECK_FALSE(bad.versions);
        CHECK_FALSE(bad.error.empty());
        CHECK(bad.source == utf8(m.autumn));
        CHECK(bad.names_kept == !c.names_lost);
        CHECK(bad.missing == (c.how == Harm::Removed));
        CHECK(snap->games() == 30);
        CHECK(snap->unreadableGames() == 10);
        CHECK(snap->deletedCount() == 10);
        for (GameId id = 0; id < 30; ++id) {
            CAPTURE(id);
            if (id >= 10 && id < 20) {
                CHECK(snap->deleted(id));
                CHECK_FALSE(snap->readable(id));
                CHECK_FALSE(snap->state(id).deleted);  // not deleted by an edit
                CHECK(snap->game(id).moves.empty());
                CHECK(snap->source(id) == utf8(m.autumn));
                continue;
            }
            CHECK_FALSE(snap->deleted(id));
            sameGame(snap->game(id), m.games[id], c.names_lost);
        }
        // The names the second segment brought: lost with its names.bin ("?"), kept otherwise; the third's own read.
        CHECK(snap->row(20).white == (c.names_lost ? "?" : "Okafor, Ada"));
        CHECK(snap->row(22).white == "Haddad, Lina");
        CHECK(snap->row(0).white == "Moreau, Claire");
        CHECK(snap->nameKnown(snap->rowIds(20).white) == !c.names_lost);
        CHECK(snap->nameKnown(snap->rowIds(22).white));

        // Its games damaged, named with the file they were imported from; a check says the same, the other games
        // sound (those whose names were lost too).
        const auto report = unreadableReport(*snap);
        REQUIRE(report.segments.size() == 1);
        CHECK(report.damaged.size() == 10);
        CHECK(report.damaged.front() == 10);
        CHECK(report.damaged_sources == std::map<std::string, uint64_t>{{utf8(m.autumn), 10}});
        auto check = checked(*db);
        REQUIRE(check.segments.size() == 1);
        CHECK(check.segments.front().name == m.second);
        CHECK(check.segments.front().names_kept == !c.names_lost);
        CHECK(check.damaged == report.damaged);
        CHECK(check.damaged_sources == report.damaged_sources);
        CHECK(db->compactPlan().segments.empty());  // nothing to compact until it is dealt with

        // Keep what can be read: its games set aside, their numbers kept; opened again, a check finds nothing.
        CHECK(db->keepReadable() == 10);
        snap = db->snapshot();
        CHECK(snap->unreadable().empty());
        CHECK(snap->state(15).deleted);
        db.reset();
        snap.reset();
        CHECK_FALSE(fs::exists(m.dir / m.second));  // the damaged files go once the database is closed
        CHECK(folders(m.dir, "seg-") == 3);
        db   = Database::open(m.dir);
        snap = db->snapshot();
        CHECK(snap->unreadable().empty());
        CHECK(snap->games() == 30);
        CHECK(snap->deletedCount() == 10);
        for (GameId id = 10; id < 20; ++id) CHECK(snap->state(id).deleted);
        for (const auto id : others()) sameGame(snap->game(id), m.games[id], c.names_lost);
        CHECK(snap->row(20).white == (c.names_lost ? "?" : "Okafor, Ada"));
        CHECK(snap->nameKnown(snap->rowIds(20).white) == !c.names_lost);
        check = checked(*db);
        CHECK(check.segments.empty());
        CHECK(check.damaged.empty());

        // It goes on: a game added after uses no name lost, and is read whole.
        db->append(tournament("Winter Rapid", "Brest", {"Okafor, Ada", "Haddad, Lina"}, 14).games, true);
        snap = db->snapshot();
        CHECK(snap->row(30).white == "Okafor, Ada");
        CHECK(snap->nameKnown(snap->rowIds(30).white));
        CHECK(checked(*db).damaged.empty());
    }
}

TEST_CASE("damaged segment: the list, the tree and the searches work on the other games") {
    auto m = threeImports("damaged-list");
    std::atomic<bool> cancel{false};
    TreeProgress tree_progress;
    REQUIRE(buildTree(*m.db, *m.db->snapshot(), {}, tree_progress, cancel).error.empty());
    m.db.reset();
    REQUIRE(fs::remove(m.dir / m.second / "moves.bin"));
    auto db   = Database::open(m.dir);
    auto snap = db->snapshot();

    // The list: the other games in their order; the damaged ones are not shown, the deleted ones asked for or not.
    CHECK(makeView(snap, Filter{}, SortBy::Number).ids == others());
    Filter all;
    all.with_deleted = true;
    CHECK(makeView(snap, all, SortBy::Number).ids == others());
    Filter okafor;
    okafor.player    = "okafor";
    const auto found = makeView(snap, okafor, SortBy::Number).ids;
    CHECK_FALSE(found.empty());
    CHECK(std::all_of(found.begin(), found.end(), [](GameId id) { return id >= 20; }));
    CHECK(makeView(snap, Filter{}, SortBy::White).ids.size() == 20);

    // The tree built before holds their games: it no longer answers, built again without them; it holds when the
    // database opens again, the segment still damaged.
    auto tree = Tree::open(m.dir);
    REQUIRE(tree);
    CHECK(tree->stale(*snap));
    REQUIRE(buildTree(*db, *snap, {}, tree_progress, cancel).error.empty());
    tree = Tree::open(m.dir);
    REQUIRE(tree);
    CHECK_FALSE(tree->stale(*snap));
    ScanBoard start;
    REQUIRE(start.setFen(chess::constants::STARTPOS, false));
    const auto answer = tree->at(*snap, start.key());
    REQUIRE(answer);
    CHECK(answer->total.games == 20);
    tree.reset();
    snap.reset();
    db.reset();
    db   = Database::open(m.dir);
    snap = db->snapshot();
    tree = Tree::open(m.dir);
    REQUIRE(tree);
    CHECK_FALSE(tree->stale(*snap));

    // A search: the games that read.
    std::vector<GameId> hits;
    for (const auto& hit : searchAll(snap, PositionQuery{start}, 2)) hits.push_back(hit.id);
    std::sort(hits.begin(), hits.end());
    CHECK(hits == others());

    // Games added meanwhile read with the others, the damaged ones still left out.
    db->append(tournament("Winter Rapid", "Brest", {"Haddad, Lina", "Okafor, Ada"}, 14).games, true, "/x/rapid.pgn");
    snap = db->snapshot();
    CHECK(snap->games() == 40);
    CHECK(snap->unreadableGames() == 10);
    CHECK(snap->row(30).white == "Haddad, Lina");
    CHECK(makeView(snap, Filter{}, SortBy::Number).ids.size() == 30);
    CHECK_FALSE(tree->stale(*snap));  // it answers for the games it covers, the import's built after it
}

TEST_CASE("damaged segment: Keep what can be read undone until the database is closed, the damaged files gone then") {
    auto m = threeImports("damaged-undo");
    m.db->setDeleted(std::vector<GameId>{3, 14}, true);  // deleted before: they stay so, and are not counted
    m.db.reset();
    damage(m.dir / m.second, {"annex.bin", Harm::Cut});
    auto db         = Database::open(m.dir);
    auto snap       = db->snapshot();
    const auto edit = snap->editsGeneration();
    CHECK(snap->unreadableGames() == 9);
    CHECK(snap->deletedCount() == 11);
    CHECK(unreadableReport(*snap).damaged.size() == 9);
    CompactProgress compacting;
    std::atomic<bool> cancel{false};
    const auto refused = db->compact(compacting, cancel);
    CHECK_FALSE(refused.done);
    CHECK_FALSE(refused.error.empty());
    CHECK_FALSE(db->keepReadableUndoable());

    CHECK(db->keepReadable() == 9);
    snap = db->snapshot();
    CHECK(snap->unreadable().empty());
    CHECK(snap->deletedCount() == 11);
    CHECK(snap->editsGeneration() != edit);
    CHECK(fs::exists(m.dir / m.second));  // kept until the database is closed
    CHECK(folders(m.dir, "seg-") == 4);
    CHECK(db->keepReadableUndoable());

    // Undone: the damaged segment back as it was, the segment written gone.
    snap.reset();
    CHECK(db->undoKeepReadable());
    snap = db->snapshot();
    REQUIRE(snap->unreadable().size() == 1);
    CHECK(snap->unreadable().front().name == m.second);
    CHECK(snap->unreadableGames() == 9);
    CHECK(snap->deletedCount() == 11);
    CHECK(snap->state(3).deleted);
    CHECK(snap->state(14).deleted);
    CHECK_FALSE(snap->state(15).deleted);
    CHECK(snap->editsGeneration() == edit);
    for (const auto id : others())
        if (id != 3) sameGame(snap->game(id), m.games[id], false);
    CHECK(folders(m.dir, "seg-") == 3);
    CHECK_FALSE(db->keepReadableUndoable());

    // Again; then anything committed after it: no undoing it.
    CHECK(db->keepReadable() == 9);
    db->setDeleted(std::vector<GameId>{4}, true);
    CHECK_FALSE(db->keepReadableUndoable());
    CHECK_FALSE(db->undoKeepReadable());
    // A check's damaged games alone, in segments that read: set aside the same way, and undone.
    CHECK(db->keepReadable(std::vector<GameId>{5, 5, 4}) == 1);
    CHECK(db->snapshot()->state(5).deleted);
    CHECK(db->keepReadableUndoable());
    CHECK(db->undoKeepReadable());
    CHECK_FALSE(db->snapshot()->state(5).deleted);
    CHECK(db->snapshot()->state(4).deleted);

    // Closed: the damaged files go. Opened again: sound, and a compaction has nothing of the games set aside to take.
    snap.reset();
    db.reset();
    CHECK_FALSE(fs::exists(m.dir / m.second));
    CHECK(folders(m.dir, "seg-") == 3);
    db = Database::open(m.dir);
    CHECK(db->snapshot()->deletedCount() == 12);
    const auto check = checked(*db);
    CHECK(check.segments.empty());
    CHECK(check.damaged.empty());
    const auto plan = db->compactPlan();
    CHECK(plan.deleted == 2);  // 3 and 4, which kept their moves; those set aside hold none
    CHECK(db->compact(compacting, cancel).done);
    CHECK(checked(*db).damaged.empty());
}

TEST_CASE("damaged segment: a database read-only, written by another program, is never changed") {
    auto m = threeImports("damaged-read-only");
    m.db.reset();
    damage(m.dir / m.second, {"names.bin", Harm::Removed, 0, 0, true});
    // What a commit cut short would have left: removed only by a program that writes to the database.
    fs::create_directories(m.dir / "seg-000099.tmp");
    const auto before = contents(m.dir);
    {
        auto db = Database::openReadOnly(m.dir);
        CHECK(db->readOnly());
        const auto snap = db->snapshot();
        REQUIRE(snap->unreadable().size() == 1);
        CHECK(snap->unreadableGames() == 10);
        CHECK(snap->row(0).white == "Moreau, Claire");
        CHECK(snap->row(20).white == "?");  // a name the second import brought
        CHECK(checked(*db).damaged.size() == 10);
        CHECK_THROWS_AS(db->keepReadable(), std::runtime_error);
        CHECK_FALSE(db->undoKeepReadable());
        CompactProgress compacting;
        std::atomic<bool> cancel{false};
        CHECK_FALSE(db->compact(compacting, cancel).done);
        CHECK_FALSE(db->refresh());
    }
    CHECK(contents(m.dir) == before);

    // Another program writes to it meanwhile: read as it commits, still nothing written from here.
    auto writer = Database::open(m.dir);
    auto reader = Database::openReadOnly(m.dir);
    CHECK(fs::exists(m.dir / m.second / "cols.bin"));  // the damaged segment is the user's to deal with
    writer->append(tournament("Winter Rapid", "Brest", {"Haddad, Lina", "Lindqvist, Erik"}, 14).games, true);
    CHECK(reader->refresh());
    CHECK(reader->snapshot()->games() == 40);
    CHECK(reader->snapshot()->unreadableGames() == 10);
    CHECK(reader->snapshot()->row(30).white == "Haddad, Lina");
    CHECK_THROWS_AS(reader->keepReadable(), std::runtime_error);
    CHECK(fs::exists(m.dir / m.second));
}

TEST_CASE("damaged segment: a segment of versions that cannot be read; a game replaced reads from its version") {
    auto m           = threeImports("damaged-versions");
    auto edited      = m.games[12];
    auto other       = m.games[25];
    edited.text      = {};
    other.text       = {};
    edited.annotated = other.annotated = false;
    m.db->replace(12, edited);
    m.db->replace(25, other);
    const auto infos = m.db->snapshot()->segmentInfos();
    REQUIRE(infos.size() == 5);
    const auto versions_25 = infos[4].name;
    const auto again_12    = m.db->snapshot()->game(12);
    m.db.reset();

    // The second segment damaged: game 12 reads from its version, the others of it cannot be read.
    damage(m.dir / m.second, {"cols.bin", Harm::Cut});
    auto db   = Database::open(m.dir);
    auto snap = db->snapshot();
    CHECK(snap->unreadableGames() == 9);
    CHECK(snap->readable(12));
    CHECK_FALSE(snap->readable(13));
    CHECK(snap->game(12).moves == again_12.moves);
    CHECK(db->keepReadable() == 9);
    CHECK_FALSE(db->snapshot()->deleted(12));
    CHECK(db->snapshot()->game(12).moves == again_12.moves);
    CHECK(checked(*db).damaged.empty());
    snap.reset();
    db.reset();

    // A segment of versions damaged: the game whose version it holds cannot be read; the others are as they were.
    damage(m.dir / versions_25, {"sig.bin", Harm::Removed});
    db   = Database::open(m.dir);
    snap = db->snapshot();
    REQUIRE(snap->unreadable().size() == 1);
    CHECK(snap->unreadable().front().versions);
    CHECK(snap->unreadable().front().first == 0);
    CHECK(snap->unreadableGames() == 1);
    CHECK_FALSE(snap->readable(25));
    CHECK(snap->readable(24));
    CHECK(snap->game(26).moves == m.games[26].moves);
    const auto report = unreadableReport(*snap);
    CHECK(report.damaged == std::vector<GameId>{25});
    CHECK(report.damaged_sources == std::map<std::string, uint64_t>{{"/x/winter.pgn", 1}});
    CHECK(checked(*db).damaged == std::vector<GameId>{25});
    CHECK(db->keepReadable() == 1);
    snap.reset();
    db.reset();
    db   = Database::open(m.dir);
    snap = db->snapshot();
    CHECK(snap->state(25).deleted);
    CHECK(snap->deletedCount() == 9 + 1);
    CHECK(snap->game(12).moves == again_12.moves);
    CHECK(checked(*db).damaged.empty());
}

TEST_CASE("damaged segment: every name given, none empty: the games set aside take an empty name of their own") {
    // Its names kept: no name a game without names has ("" or "?"), so a segment of that one name is added.
    auto m = threeImports("damaged-no-empty-name", true);
    m.db.reset();
    damage(m.dir / m.second, {"cols.bin", Harm::Removed});
    auto db = Database::open(m.dir);
    REQUIRE(db->snapshot()->unreadable().size() == 1);
    CHECK(db->keepReadable() == 10);
    db.reset();
    db        = Database::open(m.dir);
    auto snap = db->snapshot();
    CHECK(snap->segmentInfos().size() == 4);  // a segment of one name, the empty one, after the others
    const auto row = snap->row(15);
    CHECK(row.white.empty());
    CHECK(row.event.empty());
    CHECK(row.annotator.empty());
    CHECK(row.plies == 0);
    CHECK(snap->row(0).annotator == "Spring Open team");
    CHECK(snap->row(21).annotator == "Autumn Cup team");  // brought by the second import, kept
    auto check = checked(*db);
    CHECK(check.segments.empty());
    CHECK(check.damaged.empty());

    // Its names lost: they read "?", which the games set aside take.
    auto lost = threeImports("damaged-no-empty-name-lost", true);
    lost.db.reset();
    damage(lost.dir / lost.second, {"names.bin", Harm::Cut, 0, 0, true});
    db = Database::open(lost.dir);
    CHECK(db->snapshot()->row(21).white == "?");  // Duval, Marc
    CHECK(db->keepReadable() == 10);
    db.reset();
    db   = Database::open(lost.dir);
    snap = db->snapshot();
    CHECK(snap->segmentInfos().size() == 3);
    CHECK(snap->row(15).white == "?");
    CHECK(snap->row(21).annotator == "?");  // "Autumn Cup team": lost with the names
    CHECK(snap->row(0).annotator == "Spring Open team");
    check = checked(*db);
    CHECK(check.segments.empty());
    CHECK(check.damaged.empty());
}

TEST_CASE("damaged segment: its games imported again from their file, then the damaged ones set aside") {
    auto m = threeImports("damaged-again");
    m.db.reset();
    damage(m.dir / m.second, {"cols.bin", Harm::Removed});
    auto db           = Database::open(m.dir);
    const auto snap   = db->snapshot();
    const auto report = unreadableReport(*snap);
    REQUIRE(report.damaged_sources.size() == 1);
    const fs::path file = report.damaged_sources.begin()->first;  // the test's own folder: no letter beyond ASCII
    CHECK(fs::is_regular_file(file));
    std::vector<uint64_t> forget;
    for (const auto id : report.damaged) forget.push_back(snap->key(id));
    std::sort(forget.begin(), forget.end());
    ImportProgress progress;
    std::atomic<bool> cancel{false};
    const auto got =
        importInto(*db, file, ImportOptions{2, 4096}, progress, cancel, false, std::size_t{1} << 18, {}, forget);
    CHECK(got.added == 10);
    CHECK(got.duplicates == 0);
    auto now = db->snapshot();
    REQUIRE(now->games() == 40);
    for (GameId id = 30; id < 40; ++id) sameGame(now->game(id), m.games[id - 20], false);
    CHECK(db->keepReadable() == 10);
    now = db->snapshot();
    CHECK(now->games() - now->deletedCount() == 30);
    CHECK(checked(*db).damaged.empty());
    // The same file once more: its games are all there.
    ImportProgress again;
    CHECK(importInto(*db, file, ImportOptions{2, 4096}, again, cancel).added == 0);
}

TEST_CASE("damaged segment: a crash at any step of Keep what can be read leaves a database that opens") {
    auto m = threeImports("damaged-crash-model");
    m.db.reset();
    damage(m.dir / m.second, {"moves.bin", Harm::Cut});
    // Opened after the crash: as before Keep what can be read, or as after it, never between; and it goes on.
    const auto reopened = [&](const fs::path& dir) {
        auto db         = Database::open(dir);
        const auto snap = db->snapshot();
        const bool kept = snap->unreadable().empty();
        CHECK(snap->games() == 30);
        CHECK(snap->deletedCount() == 10);
        for (const auto id : others()) sameGame(snap->game(id), m.games[id], false);
        const auto check = checked(*db);
        CHECK(check.segments.size() == (kept ? 0u : 1u));
        CHECK(check.damaged.size() == (kept ? 0u : 10u));
        CHECK(folders(dir, "seg-") == 3);
        for (const auto& e : fs::directory_iterator(dir))
            CHECK(e.path().filename().string().find(".tmp") == std::string::npos);
        if (!kept) CHECK(db->keepReadable() == 10);
        CHECK(checked(*db).damaged.empty());
        return kept;
    };

    // Its steps.
    std::vector<std::string> steps;
    {
        auto db = Database::open(copyOf(m.dir, "damaged-crash-steps"));
        setCommitFaultHook([&](std::string_view step) { steps.emplace_back(step); });
        CHECK(db->keepReadable() == 10);
        setCommitFaultHook({});
    }
    REQUIRE(steps.size() >= 8);
    CHECK(steps.back() == "manifest written");

    // Stopped at each step, as a crash would stop it.
    for (std::size_t k = 0; k < steps.size(); ++k) {
        CAPTURE(steps[k]);
        const auto dir = copyOf(m.dir, "damaged-crash");
        {
            auto db          = Database::open(dir);
            std::size_t seen = 0;
            setCommitFaultHook([&](std::string_view) {
                if (seen++ == k) throw Crash{};
            });
            CHECK_THROWS_AS((void)db->keepReadable(), Crash);
            setCommitFaultHook({});
        }
        CHECK(reopened(dir) == (steps[k] == "manifest written"));
    }

#ifndef _WIN32
    // Killed at each step (kill -9): nothing of the program's own runs after it.
    for (std::size_t k = 0; k < steps.size(); ++k) {
        CAPTURE(steps[k]);
        const auto dir    = copyOf(m.dir, "damaged-killed");
        const pid_t child = fork();
        if (child == 0) {
            try {
                auto db          = Database::open(dir);
                std::size_t seen = 0;
                setCommitFaultHook([&](std::string_view) {
                    if (seen++ == k) ::kill(::getpid(), SIGKILL);
                });
                (void)db->keepReadable();
            } catch (...) {
            }
            _exit(0);  // the step not reached
        }
        int status = 0;
        REQUIRE(waitpid(child, &status, 0) == child);
        CHECK((WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL));
        CHECK(reopened(dir) == (steps[k] == "manifest written"));
    }
#endif
}

TEST_CASE("damaged segment: a file read as a database, its copy no longer read, is read again from the file") {
    const auto dir = fs::temp_directory_path() / "damaged-linked";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto file = dir / "club.pgn";
    std::ofstream(file, std::ios::binary) << tournament("Club Cup", "Lyon", {"Moreau, Claire", "Duval, Marc"}, 21).text;
    const LinkedSource source{LinkedSource::Kind::PgnFile, file};
    const auto cache = dir / "cache";
    std::atomic<bool> cancel{false};
    fs::path copy;
    {
        auto linked = LinkedDatabase::open(cache, source);
        CHECK(linked->refresh(cancel).added == 10);
        copy = linked->database()->folder();
        REQUIRE(linked->database()->snapshot()->segmentInfos().size() == 1);
        copy /= linked->database()->snapshot()->segmentInfos().front().name;
    }
    REQUIRE(fs::remove(copy / "moves.bin"));
    auto linked = LinkedDatabase::open(cache, source);
    CHECK(linked->database()->snapshot()->unreadable().empty());
    linked->refresh(cancel);
    CHECK(linked->database()->snapshot()->games() == 10);
    CHECK(linked->database()->snapshot()->row(0).white == "Moreau, Claire");
}
