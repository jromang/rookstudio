// Games replaced and deleted in a database: they keep their numbers, committed as appends are.
#include <doctest/doctest.hpp>

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

#include <chess.hpp>
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

int editsFiles(const fs::path& dir) {
    int n = 0;
    for (const auto& entry : fs::directory_iterator(dir))
        if (entry.path().filename().string().rfind("edits-", 0) == 0) ++n;
    return n;
}

}  // namespace

TEST_CASE("edits: a game replaced keeps its number, the program opened again too") {
    const auto games = randomGames(12, 7, 6);
    std::vector<ImportedGame> ten(games.begin(), games.begin() + 10);
    auto db = database("edits-replace", ten);
    std::vector<uint64_t> keys;
    for (std::size_t i = 0; i < 10; ++i) keys.push_back(db->snapshot()->key(i));

    db->replace(3, games[10]);
    auto snap = db->snapshot();
    CHECK(snap->games() == 10);
    CHECK(snap->key(3) == duplicateKey(games[10]));
    CHECK(snap->game(3).moves == games[10].moves);
    for (std::size_t i = 0; i < 10; ++i)
        if (i != 3) CHECK(snap->key(i) == keys[i]);

    // Replaced again; the program opened again.
    db->replace(3, games[11]);
    const auto dir = db->folder();
    db.reset();
    db   = Database::open(dir);
    snap = db->snapshot();
    CHECK(snap->games() == 10);
    CHECK(snap->key(3) == duplicateKey(games[11]));
    CHECK(snap->game(3).moves == games[11].moves);
    CHECK(editsFiles(dir) == 1);

    // The game it was is no longer in the database, the one it is is.
    CHECK(db->append({games[3]}).added == 1);
    CHECK(db->append({games[11]}).duplicates == 1);
    CHECK(db->snapshot()->games() == 11);
    CHECK_THROWS_AS(db->replace(99, games[0]), std::out_of_range);
}

TEST_CASE("edits: deleted games, left out of lists, searches and the tree, then brought back") {
    auto db = database("edits-delete", randomGames(10, 9, 6));
    const std::vector<GameId> gone{2, 5};
    db->setDeleted(gone, true);
    auto snap = db->snapshot();
    CHECK(snap->deleted(2));
    CHECK(snap->deleted(5));
    CHECK_FALSE(snap->deleted(3));
    CHECK(snap->deletedCount() == 2);
    CHECK(makeView(snap, Filter{}, SortBy::Number).ids == std::vector<GameId>{0, 1, 3, 4, 6, 7, 8, 9});
    Filter all;
    all.with_deleted = true;
    CHECK(makeView(snap, all, SortBy::Number).ids.size() == 10);

    // Every game reaches the start: the searches and the tree leave the deleted ones out.
    ScanBoard start;
    REQUIRE(start.setFen(chess::constants::STARTPOS, false));
    const auto hits = searchAll(snap, PositionQuery{start});
    CHECK(hits.size() == 8);
    TreeProgress progress;
    std::atomic<bool> cancel{false};
    REQUIRE(buildTree(*db, *snap, {}, progress, cancel).error.empty());
    const auto tree = Tree::open(db->folder());
    REQUIRE(tree);
    CHECK(tree->at(*snap, start.key())->total.games == 8);

    // Brought back, the program opened again.
    db->setDeleted(std::vector<GameId>{2}, false);
    const auto dir = db->folder();
    db.reset();
    db   = Database::open(dir);
    snap = db->snapshot();
    CHECK_FALSE(snap->deleted(2));
    CHECK(snap->deleted(5));
    CHECK(snap->deletedCount() == 1);
    CHECK(makeView(snap, Filter{}, SortBy::Number).ids.size() == 9);
}

TEST_CASE("edits: a crash at any step of a replacement leaves the game as it was, or as it is to be") {
    const auto games = randomGames(8, 13, 6);
    std::vector<ImportedGame> six(games.begin(), games.begin() + 6);
    std::vector<std::string> steps;
    {
        auto db = database("edits-steps", six);
        setCommitFaultHook([&](std::string_view step) { steps.emplace_back(step); });
        db->replace(2, games[6]);
        setCommitFaultHook({});
    }
    REQUIRE(steps.size() >= 8);
    CHECK(steps.back() == "manifest written");
    for (std::size_t k = 0; k < steps.size(); ++k) {
        CAPTURE(steps[k]);
        fs::path dir;
        {
            auto db = database("edits-crash", six);
            dir     = db->folder();
            db->replace(4, games[7]);  // an earlier edit, which must stay
            std::size_t seen = 0;
            setCommitFaultHook([&](std::string_view) {
                if (seen++ == k) throw Crash{};
            });
            CHECK_THROWS_AS(db->replace(2, games[6]), Crash);
            setCommitFaultHook({});
        }
        auto db         = Database::open(dir);
        const auto snap = db->snapshot();
        const bool all  = steps[k] == "manifest written";
        CHECK(snap->games() == 6);
        CHECK(snap->key(2) == duplicateKey(all ? games[6] : games[2]));
        CHECK(snap->key(4) == duplicateKey(games[7]));
        CHECK(editsFiles(dir) == 1);
        for (const auto& entry : fs::directory_iterator(dir))
            CHECK(entry.path().filename().string().find(".tmp") == std::string::npos);
        // And it goes on.
        db->replace(2, games[6]);
        CHECK(db->snapshot()->key(2) == duplicateKey(games[6]));
    }
}

TEST_CASE("edits: an opening tree built before a game was replaced answers nothing until built again") {
    auto db = database("edits-tree", randomGames(10, 21, 6));
    TreeProgress progress;
    std::atomic<bool> cancel{false};
    REQUIRE(buildTree(*db, *db->snapshot(), {}, progress, cancel).error.empty());
    ScanBoard start;
    REQUIRE(start.setFen(chess::constants::STARTPOS, false));
    auto tree = Tree::open(db->folder());
    CHECK_FALSE(tree->stale(*db->snapshot()));
    CHECK(tree->at(*db->snapshot(), start.key()));
    // Games added: the tree still answers for those it covers.
    db->append(randomGames(3, 22, 6), true);
    CHECK_FALSE(tree->stale(*db->snapshot()));
    CHECK(tree->at(*db->snapshot(), start.key())->total.games == 10);
    // A game replaced: stale.
    db->replace(0, randomGames(1, 23, 6)[0]);
    CHECK(tree->stale(*db->snapshot()));
    CHECK_FALSE(tree->at(*db->snapshot(), start.key()));
    REQUIRE(buildTree(*db, *db->snapshot(), {}, progress, cancel).error.empty());
    tree = Tree::open(db->folder());
    CHECK_FALSE(tree->stale(*db->snapshot()));
    CHECK(tree->at(*db->snapshot(), start.key())->total.games == 13);
}
