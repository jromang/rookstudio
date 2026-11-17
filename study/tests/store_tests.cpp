#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <study/import.hpp>
#include <study/store.hpp>

using namespace fcg::study;
namespace fs = std::filesystem;

namespace {

fs::path data(const std::string& name) {
    const char* env = std::getenv("FCG_SOURCE_DIR");
    return fs::path(env ? env : FCG_SOURCE_DIR) / "study" / "tests" / "data" / name;
}

std::vector<ImportedGame> fixtureGames() {
    std::vector<ImportedGame> games;
    ImportProgress progress;
    std::atomic<bool> cancel{false};
    const auto report = importGames(data("games.pgn"), ImportOptions{2, 4096}, progress, cancel,
                                    [&](std::vector<ImportedGame>&& batch) {
                                        for (auto& g : batch) games.push_back(std::move(g));
                                    });
    REQUIRE(report.error.empty());
    REQUIRE(games.size() == 120);
    return games;
}

fs::path freshFolder(const std::string& name) {
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    return dir;
}

// What a database must give back of a game: its moves, flags, text, clocks and evaluations, and its tags whatever
// their order.
void sameGame(const ImportedGame& stored, const ImportedGame& imported) {
    std::multimap<std::string, std::string> a(stored.tags.begin(), stored.tags.end());
    std::multimap<std::string, std::string> b(imported.tags.begin(), imported.tags.end());
    // A tag with nothing in it, or a rating or date given as "?", is left out: the columns keep numbers.
    for (auto it = b.begin(); it != b.end();) {
        if (it->second.empty() || it->second == "?" || it->second == "????.??.??")
            it = b.erase(it);
        else
            ++it;
    }
    for (auto it = a.begin(); it != a.end();) {
        if (it->second == "?" || it->second == "????.??.??")
            it = a.erase(it);
        else
            ++it;
    }
    CHECK(a == b);
    CHECK(stored.start_fen == imported.start_fen);
    CHECK(stored.chess960 == imported.chess960);
    CHECK(stored.moves == imported.moves);
    CHECK(stored.plies == imported.plies);
    CHECK(stored.result == imported.result);
    CHECK(stored.annotated == imported.annotated);
    CHECK(stored.damaged == imported.damaged);
    CHECK(stored.text == imported.text);
    CHECK(stored.clocks == imported.clocks);
    CHECK(stored.evals == imported.evals);
}

struct Crash {};  // a simulated crash: no std::exception, so nothing catches it on the way

}  // namespace

TEST_CASE("store: games written, read back as they were imported, and again once the database is reopened") {
    const auto games = fixtureGames();
    const auto dir   = freshFolder("store-roundtrip.fcgdb");
    {
        auto db = Database::create(dir);
        CHECK(db->snapshot()->games() == 0);
        db->append(games);
        const auto snap = db->snapshot();
        REQUIRE(snap->games() == games.size());
        for (std::size_t i = 0; i < games.size(); ++i) {
            CAPTURE(i);
            sameGame(snap->game(i), games[i]);
        }
        const auto row = snap->row(0);
        CHECK(row.white.rfind("player", 0) == 0);
        CHECK(row.event == "Rated Blitz game 0");
        CHECK(dateText(row.date) == "2026.11.16");
        CHECK(row.white_elo > 0);
        CHECK(row.result == '1');
        CHECK((row.flags & GAME_CLOCKS) != 0);
        CHECK((snap->row(3).flags & GAME_CHESS960) != 0);
        CHECK((snap->row(4).flags & GAME_DAMAGED) != 0);
        CHECK(snap->moves(0).size() == 9);
    }
    auto db         = Database::open(dir);
    const auto snap = db->snapshot();
    REQUIRE(snap->games() == games.size());
    for (std::size_t i = 0; i < games.size(); ++i) sameGame(snap->game(i), games[i]);
    CHECK_THROWS(Database::create(dir));
}

TEST_CASE("store: a reader's snapshot outlives the commits after it; names shared across segments") {
    const auto games = fixtureGames();
    const auto dir   = freshFolder("store-snapshots.fcgdb");
    auto db          = Database::create(dir);
    db->append(std::vector<ImportedGame>(games.begin(), games.begin() + 60));
    const auto first = db->snapshot();
    db->append(std::vector<ImportedGame>(games.begin() + 60, games.end()));
    const auto middle = db->snapshot();
    db->append(std::vector<ImportedGame>(games.begin(), games.begin() + 6));
    const auto last = db->snapshot();
    CHECK(first->games() == 60);
    CHECK(last->games() == 126);
    CHECK(last->generation() == first->generation() + 2);
    // The first snapshot reads its games as before.
    for (std::size_t i = 0; i < 60; ++i) sameGame(first->game(i), games[i]);
    for (std::size_t i = 0; i < 6; ++i) sameGame(last->game(120 + i), games[i]);
    // Names already in the database are not added again: six games seen before add none.
    CHECK(last->names() == middle->names());
    CHECK(last->row(120).event == first->row(0).event);
}

TEST_CASE("store: a crash at any step of a commit leaves the database as it was, or with the whole commit") {
    const auto games = fixtureGames();
    const std::vector<ImportedGame> before(games.begin(), games.begin() + 30), added(games.begin() + 30, games.end());

    // The steps of one commit.
    std::vector<std::string> steps;
    {
        const auto dir = freshFolder("store-steps.fcgdb");
        auto db        = Database::create(dir);
        setCommitFaultHook([&](std::string_view step) { steps.emplace_back(step); });
        db->append(added);
        setCommitFaultHook({});
    }
    REQUIRE(steps.size() >= 10);
    CHECK(steps.back() == "manifest written");

    for (std::size_t k = 0; k < steps.size(); ++k) {
        CAPTURE(steps[k]);
        const auto dir = freshFolder("store-crash.fcgdb");
        {
            auto db = Database::create(dir);
            db->append(before);
            std::size_t seen = 0;
            setCommitFaultHook([&](std::string_view) {
                if (seen++ == k) throw Crash{};
            });
            CHECK_THROWS_AS(db->append(added), Crash);
            setCommitFaultHook({});
        }
        auto db         = Database::open(dir);
        const auto snap = db->snapshot();
        const bool all  = steps[k] == "manifest written";
        CHECK(snap->games() == (all ? games.size() : before.size()));
        for (std::size_t i = 0; i < snap->games(); ++i) sameGame(snap->game(i), games[i]);
        // Nothing left of the commit cut short.
        for (const auto& entry : fs::directory_iterator(dir)) {
            const auto name = entry.path().filename().string();
            CHECK(name.find(".tmp") == std::string::npos);
        }
        // And the database goes on: the games added again.
        if (!all) {
            db->append(added);
            CHECK(db->snapshot()->games() == games.size());
        }
    }
}

TEST_CASE("store: a disk that refuses leaves the database as before; what is no database is refused") {
    const auto games = fixtureGames();
    const auto dir   = freshFolder("store-refused.fcgdb");
    auto db          = Database::create(dir);
    db->append(std::vector<ImportedGame>(games.begin(), games.begin() + 10));
    setCommitFaultHook([](std::string_view step) {
        if (step == "write moves.bin") throw std::runtime_error("disk full");
    });
    CHECK_THROWS_AS(db->append(games), std::runtime_error);
    setCommitFaultHook({});
    CHECK(db->snapshot()->games() == 10);
    db->append(std::vector<ImportedGame>(games.begin() + 10, games.end()));
    CHECK(db->snapshot()->games() == games.size());
    for (std::size_t i = 0; i < games.size(); ++i) sameGame(db->snapshot()->game(i), games[i]);

    CHECK_THROWS(Database::open(freshFolder("store-nothing.fcgdb")));
    const auto broken = freshFolder("store-broken.fcgdb");
    fs::create_directories(broken);
    std::ofstream(broken / "manifest.json") << "{ not json";
    CHECK_THROWS(Database::open(broken));

    CHECK(packDate("1972.??.??") == (1972u << 9));
    CHECK(dateText(packDate("2026.11.17")) == "2026.11.17");
    CHECK(packDate("yesterday") == 0);
    CHECK(ecoText(packEco("B90")) == "B90");
    CHECK(packEco("Z99") == 0);
}
