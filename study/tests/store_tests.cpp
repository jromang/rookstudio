#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#include <json.hpp>
#include <study/import.hpp>
#include <study/import_into.hpp>
#include <study/store.hpp>
#include <study/tree.hpp>

#include "test_games.hpp"
#include "test_support.hpp"

using namespace fcg::study;
namespace fs = std::filesystem;
using fcg::test::ScratchDir;

namespace {

using fcg::test::data;

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
    const ScratchDir dir("store-roundtrip.fcgdb");
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
        // Lichess's sites, each game's own address, are no names: in the annex, given back in their place.
        CHECK(row.site.empty());
        const auto first = snap->game(0);
        REQUIRE(first.tags.size() > 2);
        CHECK(first.tags[1] == std::pair<std::string, std::string>{"Site", "https://lichess.org/abc00000"});
        for (uint32_t n = 0; n < snap->names(); ++n) CHECK(snap->name(n).rfind("https://", 0) != 0);
    }
    auto db         = Database::open(dir);
    const auto snap = db->snapshot();
    REQUIRE(snap->games() == games.size());
    for (std::size_t i = 0; i < games.size(); ++i) sameGame(snap->game(i), games[i]);
    CHECK_THROWS(Database::create(dir));
}

TEST_CASE("store: a reader's snapshot outlives the commits after it; names shared across segments") {
    const auto games = fixtureGames();
    const ScratchDir dir("store-snapshots.fcgdb");
    auto db = Database::create(dir);
    db->append(std::vector<ImportedGame>(games.begin(), games.begin() + 60));
    const auto first = db->snapshot();
    db->append(std::vector<ImportedGame>(games.begin() + 60, games.end()));
    const auto middle = db->snapshot();
    // Six games it has already: left out, unless kept on purpose.
    const auto again = db->append(std::vector<ImportedGame>(games.begin(), games.begin() + 6));
    CHECK(again.added == 0);
    CHECK(again.duplicates == 6);
    CHECK(db->snapshot()->games() == 120);
    const auto kept = db->append(std::vector<ImportedGame>(games.begin(), games.begin() + 6), true);
    CHECK(kept.added == 6);
    const auto after_kept = db->snapshot();
    // A game twice in one batch, new to the database: added once.
    auto fresh = games[0];
    for (auto& [tag, value] : fresh.tags) {
        if (tag == "Event") value += " (again)";
    }
    const auto twice = db->append({fresh, fresh});
    CHECK(twice.added == 1);
    CHECK(twice.duplicates == 1);
    const auto last = db->snapshot();
    CHECK(last->key(120) == last->key(0));
    CHECK(first->games() == 60);
    CHECK(last->games() == 127);
    CHECK(last->generation() == first->generation() + 3);
    // The first snapshot reads its games as before.
    for (std::size_t i = 0; i < 60; ++i) sameGame(first->game(i), games[i]);
    for (std::size_t i = 0; i < 6; ++i) sameGame(last->game(120 + i), games[i]);
    // Names already in the database are not added again: six games seen before add none, the new event one.
    CHECK(after_kept->names() == middle->names());
    CHECK(last->names() == middle->names() + 1);
    CHECK(last->row(120).event == first->row(0).event);
}

TEST_CASE("store: a crash at any step of a commit leaves the database as it was, or with the whole commit") {
    const auto games = fixtureGames();
    const std::vector<ImportedGame> before(games.begin(), games.begin() + 30), added(games.begin() + 30, games.end());

    // The steps of one commit.
    std::vector<std::string> steps;
    {
        const ScratchDir dir("store-steps.fcgdb");
        auto db = Database::create(dir);
        setCommitFaultHook([&](std::string_view step) { steps.emplace_back(step); });
        db->append(added);
        setCommitFaultHook({});
    }
    REQUIRE(steps.size() >= 10);
    CHECK(steps.back() == "manifest written");

    for (std::size_t k = 0; k < steps.size(); ++k) {
        CAPTURE(steps[k]);
        const ScratchDir dir("store-crash.fcgdb");
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

TEST_CASE("store: what a killed program left is cleared when the database opens") {
    const auto games = fixtureGames();
    const ScratchDir dir("store-killed.fcgdb");
    {
        auto db = Database::create(dir);
        db->append(std::vector<ImportedGame>(games.begin(), games.begin() + 30));
    }
    // A program killed in a commit leaves its new manifest not renamed yet, and a segment the manifest does not name.
    std::ofstream(dir / "manifest.json.tmp") << "{\"format\":\"rookstudio-database\"";
    fs::create_directories(dir / "seg-000009");
    std::ofstream(dir / "seg-000009" / "cols.bin") << "half";
    auto db = Database::open(dir);
    CHECK(db->snapshot()->games() == 30);
    CHECK_FALSE(fs::exists(dir / "manifest.json.tmp"));
    CHECK_FALSE(fs::exists(dir / "seg-000009"));
    // Killed while it made a database: its lock and its first manifest are all there is, and a database can be made
    // there.
    const ScratchDir unborn("store-unborn.fcgdb");
    fs::create_directories(unborn);
    std::ofstream(unborn / "tournament.lock") << "";
    std::ofstream(unborn / "manifest.json.tmp") << "{";
    CHECK_THROWS(Database::open(unborn));
    CHECK(Database::create(unborn)->snapshot()->games() == 0);
    CHECK_FALSE(fs::exists(unborn / "manifest.json.tmp"));
    const ScratchDir taken("store-taken");
    fs::create_directories(taken);
    std::ofstream(taken / "notes.txt") << "mine";
    CHECK_THROWS(Database::create(taken));
    // The next segment does not take the leftover's number again.
    db->append(std::vector<ImportedGame>(games.begin() + 30, games.end()));
    CHECK(fs::exists(dir / "seg-000010"));
    CHECK(Database::open(dir)->snapshot()->games() == games.size());
}

TEST_CASE("store: a disk that refuses leaves the database as before; what is no database is refused") {
    const auto games = fixtureGames();
    const ScratchDir dir("store-refused.fcgdb");
    auto db = Database::create(dir);
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

    const ScratchDir nothing("store-nothing.fcgdb");
    CHECK_THROWS(Database::open(nothing));
    const ScratchDir broken("store-broken.fcgdb");
    fs::create_directories(broken);
    std::ofstream(broken / "manifest.json") << "{ not json";
    CHECK_THROWS(Database::open(broken));

    CHECK(packDate("1972.??.??") == (1972u << 9));
    CHECK(dateText(packDate("2026.11.17")) == "2026.11.17");
    CHECK(packDate("yesterday") == 0);
    CHECK(ecoText(packEco("B90")) == "B90");
    CHECK(packEco("Z99") == 0);
}

TEST_CASE("store: the line of each damaged game an import read, found again by the game's key") {
    const auto file = fs::temp_directory_path() / "store-damaged-lines.pgn";
    std::ofstream(file, std::ios::binary) << "[Event \"Fine\"]\n\n1. e4 e5 1-0\n\n"
                                          << "[Event \"Illegal\"]\n[Round \"2\"]\n\n1. e4 e5 2. Ke3 *\n\n"
                                          << "[Event \"Fine again\"]\n\n1. d4 d5 0-1\n\n"
                                          << "[Event \"Cut\"]\n\n1. c4 e5 2. Nc";
    const ScratchDir dir("store-damaged-lines.fcgdb");
    auto db = Database::create(dir);
    std::atomic<bool> go_on{false};
    ImportProgress progress;
    const auto got = importInto(*db, file, ImportOptions{2, 4096}, progress, go_on);
    REQUIRE(got.added == 4);
    REQUIRE(got.read.damaged == 2);
    CHECK(got.damaged == 2);
    REQUIRE(got.damaged_lines.size() == 2);
    const auto snap = db->snapshot();
    CHECK(got.damaged_lines[0] == std::pair<uint64_t, uint64_t>{snap->key(1), 5});
    CHECK(got.damaged_lines[1] == std::pair<uint64_t, uint64_t>{snap->key(3), 14});
}

TEST_CASE("store: the damaged games an import adds are those of its games added, the duplicates left out") {
    // The damaged game twice, then once more in another import: added once, counted once.
    const auto file = fs::temp_directory_path() / "store-damaged-twice.pgn";
    std::ofstream(file, std::ios::binary) << "[Event \"Illegal\"]\n\n1. e4 e5 2. Ke3 *\n\n"
                                          << "[Event \"Illegal\"]\n\n1. e4 e5 2. Ke3 *\n\n"
                                          << "[Event \"Fine\"]\n\n1. e4 e5 1-0\n\n";
    const ScratchDir dir("store-damaged-twice.fcgdb");
    auto db = Database::create(dir);
    std::atomic<bool> go_on{false};
    ImportProgress progress;
    auto got = importInto(*db, file, ImportOptions{2, 4096}, progress, go_on);
    CHECK(got.read.games == 3);
    CHECK(got.read.damaged == 2);
    CHECK(got.added == 2);
    CHECK(got.duplicates == 1);
    CHECK(got.damaged == 1);
    ImportProgress again;
    got = importInto(*db, file, ImportOptions{2, 4096}, again, go_on);
    CHECK(got.read.damaged == 2);
    CHECK(got.added == 0);
    CHECK(got.damaged == 0);
}

TEST_CASE("store: a file imported into a database, committed as it goes; cancelled or refused, it keeps what it had") {
    const auto games = fixtureGames();
    const ScratchDir dir("store-import.fcgdb");
    auto db = Database::create(dir);
    std::atomic<bool> go_on{false};
    ImportProgress progress;
    auto got = importInto(*db, data("games.pgn"), ImportOptions{2, 4096}, progress, go_on, false, 50);
    CHECK(got.error.empty());
    CHECK(got.read.games == 120);
    CHECK(got.added == 120);
    CHECK(got.commits >= 2);
    REQUIRE(db->snapshot()->games() == 120);
    for (std::size_t i = 0; i < games.size(); ++i) sameGame(db->snapshot()->game(i), games[i]);
    // Again, from another copy of the file: nothing added.
    ImportProgress again;
    got = importInto(*db, data("games.pgn.zst"), ImportOptions{2, 4096}, again, go_on);
    CHECK(got.added == 0);
    CHECK(got.duplicates == 120);

    // Cancelled at once: what was read is committed, and the next import adds the rest, in the file's order.
    const ScratchDir other_dir("store-import-cancelled.fcgdb");
    auto other = Database::create(other_dir);
    std::atomic<bool> stop{true};
    ImportProgress first;
    got = importInto(*other, data("games.pgn"), ImportOptions{2, 4096}, first, stop, false, 50);
    CHECK(got.read.cancelled);
    CHECK(other->snapshot()->games() < 120);
    ImportProgress rest;
    got = importInto(*other, data("games.pgn"), ImportOptions{2, 4096}, rest, go_on, false, 50);
    REQUIRE(other->snapshot()->games() == 120);
    for (std::size_t i = 0; i < games.size(); ++i) sameGame(other->snapshot()->game(i), games[i]);

    // The disk refuses the second commit: said, and the first kept.
    const ScratchDir refused_dir("store-import-refused.fcgdb");
    auto refused  = Database::create(refused_dir);
    int committed = 0;
    setCommitFaultHook([&](std::string_view step) {
        if (step == "manifest written") ++committed;
        if (committed == 1 && step == "write moves.bin") throw std::runtime_error("disk full");
    });
    ImportProgress full;
    got = importInto(*refused, data("games.pgn"), ImportOptions{2, 4096}, full, go_on, false, 50);
    setCommitFaultHook({});
    CHECK(got.error == "disk full");
    CHECK(got.commits == 1);
    CHECK(refused->snapshot()->games() == got.added);
    CHECK(got.added >= 50);
    CHECK(got.added < 120);
}

TEST_CASE("store: its errors carry a code for the window to say, with their text (#112)") {
    const auto games  = fixtureGames();
    const auto codeOf = [](const std::function<void()>& f) {
        try {
            f();
        } catch (const std::exception& e) {
            CHECK(std::string(e.what()) != "");  // the text kept, for the log and the errors no code names
            return errorCode(e);
        }
        FAIL("nothing thrown");
        return StoreErrc::Other;
    };
    // No database there, a manifest that does not read, another program's manifest, a newer program's.
    const ScratchDir nothing("store-codes-nothing.fcgdb");
    CHECK(codeOf([&] { (void)Database::open(nothing); }) == StoreErrc::NotADatabase);
    CHECK(codeOf([&] { (void)Database::openReadOnly(nothing); }) == StoreErrc::NotADatabase);
    const ScratchDir broken("store-codes-broken.fcgdb");
    fs::create_directories(broken);
    std::ofstream(broken / "manifest.json") << "{ not json";
    CHECK(codeOf([&] { (void)Database::open(broken); }) == StoreErrc::Damaged);
    const ScratchDir other("store-codes-other.fcgdb");
    fs::create_directories(other);
    std::ofstream(other / "manifest.json") << R"({"format": "something-else", "version": 1})";
    CHECK(codeOf([&] { (void)Database::open(other); }) == StoreErrc::NotADatabase);
    const ScratchDir newer("store-codes-newer.fcgdb");
    fs::create_directories(newer);
    std::ofstream(newer / "manifest.json")
        << R"({"format": "rookstudio-database", "version": 99, "generation": 0, "segments": []})";
    CHECK(codeOf([&] { (void)Database::open(newer); }) == StoreErrc::NewerFormat);

    // A database another program writes: a change refused, a compaction too.
    const ScratchDir dir("store-codes.fcgdb");
    auto writer = Database::create(dir);
    writer->append(std::vector<ImportedGame>(games.begin(), games.begin() + 10));
    auto reader = Database::openReadOnly(dir);
    CHECK(codeOf([&] { reader->append(std::vector<ImportedGame>(games.begin() + 10, games.begin() + 11)); }) ==
          StoreErrc::ReadOnly);
    const std::vector<GameId> first{0};
    CHECK(codeOf([&] { reader->setDeleted(first, true); }) == StoreErrc::ReadOnly);
    CompactProgress compacting;
    const std::atomic<bool> go_on{false};
    const auto refused = reader->compact(compacting, go_on);
    CHECK_FALSE(refused.error.empty());
    CHECK(refused.error_code == StoreErrc::ReadOnly);
    // Another program holds the folder.
    CHECK(errorCode(fcg::tour::FolderBusy("held")) == StoreErrc::Busy);

    // The disk without room in the middle of an import: the import stops, the code with its text.
    setCommitFaultHook([](std::string_view step) {
        if (step == "write moves.bin")
            throw fs::filesystem_error("cannot write moves.bin", std::make_error_code(std::errc::no_space_on_device));
    });
    std::atomic<bool> cancel{false};
    ImportProgress progress;
    const auto full = importInto(*writer, data("games.pgn"), ImportOptions{2, 4096}, progress, cancel, false, 50);
    setCommitFaultHook({});
    CHECK_FALSE(full.error.empty());
    CHECK(full.error_code == StoreErrc::DiskFull);
    CHECK(writer->snapshot()->games() == 10);

    // Any other error: its text alone.
    CHECK(errorCode(std::runtime_error("something else")) == StoreErrc::Other);
    CHECK(errorCode(std::exception_ptr()) == StoreErrc::Other);
    CHECK(errorCode(std::make_exception_ptr(StoreError(StoreErrc::Cancelled, "cancelled"))) == StoreErrc::Cancelled);
}

TEST_CASE("store: a database another program writes, read as it commits, nothing written") {
    const auto games = fixtureGames();
    const ScratchDir dir("store-read-only.fcgdb");
    auto writer = Database::create(dir);
    writer->append(std::vector<ImportedGame>(games.begin(), games.begin() + 30));
    CHECK_FALSE(writer->readOnly());
    CHECK_FALSE(writer->refresh());  // the writer's snapshot is always current

    auto reader = Database::openReadOnly(dir);
    CHECK(reader->readOnly());
    CHECK(reader->snapshot()->games() == 30);
    CHECK_FALSE(reader->refresh());  // nothing committed since

    // What the writer is writing, not committed yet, stays: the reader opening or refreshing clears nothing.
    fs::create_directories(dir / "seg-000099");
    std::ofstream(dir / "seg-000099" / "cols.bin") << "under way";
    std::ofstream(dir / "manifest.json.tmp") << "{";
    auto second = Database::openReadOnly(dir);
    CHECK(fs::exists(dir / "seg-000099" / "cols.bin"));
    CHECK(fs::exists(dir / "manifest.json.tmp"));
    fs::remove_all(dir / "seg-000099");
    fs::remove(dir / "manifest.json.tmp");

    // A commit of the writer: the reader takes it in, its snapshot from before left as it was.
    const auto before = reader->snapshot();
    writer->append(std::vector<ImportedGame>(games.begin() + 30, games.begin() + 70));
    CHECK(reader->refresh());
    CHECK(reader->snapshot()->games() == 70);
    CHECK(before->games() == 30);
    sameGame(reader->snapshot()->game(50), games[50]);
    CHECK_FALSE(reader->refresh());

    // Nothing written through the reader.
    CHECK_THROWS_AS(reader->append(std::vector<ImportedGame>(games.begin() + 70, games.begin() + 71)),
                    std::runtime_error);
    CHECK_THROWS_AS(reader->replace(0, games[0]), std::runtime_error);
    const std::vector<GameId> one{0};
    CHECK_THROWS_AS(reader->setDeleted(one, true), std::runtime_error);
    CHECK(writer->snapshot()->games() == 70);
    const ScratchDir none("store-read-only-none");
    CHECK_THROWS(Database::openReadOnly(none));
}

TEST_CASE("store: a snapshot comes at once while a change commits, with the games before it (#99)") {
    const auto games = fixtureGames();
    const ScratchDir dir("store-commit-readers.fcgdb");
    auto db = Database::create(dir);
    db->append(std::vector<ImportedGame>(games.begin(), games.begin() + 60));

    // Each change held in its commit, its segment written and synced, its manifest not yet: the window's snapshots,
    // taken meanwhile, come without waiting for it.
    std::mutex mutex;
    std::condition_variable changed;
    bool held = false, go_on = false;
    setCommitFaultHook([&](std::string_view step) {
        if (step != "write manifest") return;
        std::unique_lock lock(mutex);
        held = true;
        changed.notify_all();
        changed.wait(lock, [&] { return go_on; });
    });
    const auto whileCommitting = [&](const std::string& what, const std::function<void()>& change) {
        CAPTURE(what);
        {
            std::lock_guard lock(mutex);
            held  = false;
            go_on = false;
        }
        const auto before = db->snapshot();
        auto writer       = std::async(std::launch::async, change);
        {
            std::unique_lock lock(mutex);
            changed.wait(lock, [&] { return held; });
        }
        auto reader = std::async(std::launch::async, [&] {
            std::shared_ptr<const Snapshot> seen;
            for (int i = 0; i < 100; ++i) seen = db->snapshot();
            return seen;
        });
        // Bounded only for a reader that would wait for the commit: then the commit goes on, and the test fails.
        const bool came = reader.wait_for(std::chrono::seconds(60)) == std::future_status::ready;
        {
            std::lock_guard lock(mutex);
            go_on = true;
        }
        changed.notify_all();
        writer.get();
        CHECK(came);
        const auto seen = reader.get();
        CHECK(seen->generation() == before->generation());
        CHECK(seen->games() == before->games());
        CHECK(db->snapshot()->generation() == before->generation() + 1);
    };
    whileCommitting("append", [&] { db->append(std::vector<ImportedGame>(games.begin() + 60, games.end())); });
    CHECK(db->snapshot()->games() == games.size());
    whileCommitting("replace", [&] { db->replace(3, games[100]); });
    sameGame(db->snapshot()->game(3), games[100]);
    whileCommitting("delete", [&] { db->setDeleted(std::vector<GameId>{5, 6}, true); });
    CHECK(db->snapshot()->deleted(5));
    std::vector<GameState> before_merge;
    whileCommitting("merge", [&] { before_merge = db->merge({Database::Merge{10, games[11], {11}}}); });
    CHECK(db->snapshot()->deleted(11));
    whileCommitting("restore", [&] { db->restore(before_merge); });
    CHECK_FALSE(db->snapshot()->deleted(11));
    Player player;
    player.name      = "Someone, Anyone";
    player.spellings = {db->snapshot()->rowIds(0).white};
    whileCommitting("players", [&] { db->setPlayers(PlayerTable(std::vector<Player>{player})); });
    CHECK(db->snapshot()->players()->players().size() == 1);
    setCommitFaultHook({});

    // Reopened: every change in, the games as the last snapshot read them.
    const auto last = db->snapshot();
    db.reset();
    db              = Database::open(dir);
    const auto snap = db->snapshot();
    REQUIRE(snap->games() == last->games());
    for (GameId id = 0; id < snap->games(); ++id) {
        CAPTURE(id);
        sameGame(snap->game(id), last->game(id));
        CHECK(snap->deleted(id) == last->deleted(id));
    }
}

TEST_CASE("store: a database emptied in one commit, a snapshot taken before reading its games until let go (#100)") {
    const auto games = fixtureGames();
    const ScratchDir dir("store-clear.fcgdb");
    auto db = Database::create(dir);
    db->append(std::vector<ImportedGame>(games.begin(), games.begin() + 60));
    db->setDeleted(std::vector<GameId>{4}, true);
    Player player;
    player.name      = "Someone, Anyone";
    player.spellings = {db->snapshot()->rowIds(0).white};
    db->setPlayers(PlayerTable(std::vector<Player>{player}));
    std::atomic<bool> cancel{false};
    TreeProgress tree_progress;
    REQUIRE(buildTree(*db, *db->snapshot(), {}, tree_progress, cancel).error.empty());
    auto before        = db->snapshot();
    const auto segment = before->segmentInfos().front().name;

    // Stopped before its manifest is replaced: nothing changed.
    setCommitFaultHook([](std::string_view step) {
        if (step == "write manifest") throw std::runtime_error("disk full");
    });
    CHECK_THROWS_AS(db->clear(), std::runtime_error);
    setCommitFaultHook({});
    CHECK(db->snapshot() == before);
    CHECK(Database::openReadOnly(dir)->snapshot()->games() == 60);

    db->clear();
    const auto empty = db->snapshot();
    CHECK(empty->games() == 0);
    CHECK(empty->names() == 0);
    CHECK(empty->deletedCount() == 0);
    CHECK(empty->players()->empty());
    CHECK(empty->generation() == before->generation() + 1);
    CHECK(empty->editsGeneration() == empty->generation());
    // The snapshot taken before reads its games as they were, from its files, still there.
    CHECK(fs::exists(dir / segment / "moves.bin"));
    for (std::size_t i = 0; i < 60; ++i) sameGame(before->game(i), games[i]);

    // The games added after count from 0 again, their names too; a game the database had before is no duplicate now.
    db->append(std::vector<ImportedGame>(games.begin() + 60, games.end()));
    CHECK(db->append(std::vector<ImportedGame>(games.begin(), games.begin() + 1)).added == 1);
    auto snap = db->snapshot();
    REQUIRE(snap->games() == 61);
    sameGame(snap->game(0), games[60]);
    sameGame(snap->game(60), games[0]);
    CHECK(fs::exists(dir / segment));  // still held
    // The tree built before holds other games: it answers nothing, the count of games however close.
    const auto tree = Tree::open(dir);
    REQUIRE(tree);
    CHECK(tree->stale(*snap));
    // Let go of: its files go at the next change, the edits and the table of names before at once.
    before.reset();
    db->setDeleted(std::vector<GameId>{1}, true);
    CHECK_FALSE(fs::exists(dir / segment));
    std::size_t edits = 0, players = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        const auto name = entry.path().filename().string();
        edits += name.rfind("edits-", 0) == 0;
        players += name.rfind("players-", 0) == 0;
    }
    CHECK(edits == 1);  // the deletion's just made
    CHECK(players == 0);

    // Reopened: the games added since, the generation gone on.
    const auto generation = db->snapshot()->generation();
    db.reset();
    snap.reset();
    db = Database::open(dir);
    CHECK(db->snapshot()->generation() == generation);
    CHECK(db->snapshot()->games() == 61);
    CHECK(db->snapshot()->deleted(1));
    sameGame(db->snapshot()->game(60), games[0]);
    CHECK_THROWS_AS(Database::openReadOnly(dir)->clear(), std::runtime_error);  // read-only: nothing written
}

TEST_CASE("store: the files a database writes are the same bytes as before the fixes of #99-#102") {
    // The format does not change: a database of the fixture's games and random ones, a game replaced, three deleted
    // and a table of names, written by the code of 7645460c (before #99-#102) gave these sizes and FNV-1a hashes, file
    // by file. Made again only for a change of the format, saying why in the commit.
    const std::map<std::string, std::pair<std::size_t, uint64_t>> before{
        {"edits-000004.bin", {72, 0x78225c1dc4b037d7}},         {"manifest.json", {454, 0xdbce06cb27a9bf05}},
        {"players-000005.bin", {94, 0xf5d933c0be2294d5}},       {"seg-000001/annex.bin", {15881, 0x42f325bb6d82223b}},
        {"seg-000001/cols.bin", {8328, 0x44e42c3da1f1a04e}},    {"seg-000001/moves.bin", {720, 0xa01ff12fca46f3d5}},
        {"seg-000001/names.bin", {2471, 0x27cdaf3874c526e0}},   {"seg-000001/sig.bin", {2880, 0xdf4b44ead9ad7f85}},
        {"seg-000002/annex.bin", {0, 0xcbf29ce484222325}},      {"seg-000002/cols.bin", {207048, 0xdedef5523d68da02}},
        {"seg-000002/moves.bin", {134695, 0x6b5e2a65dcf6a51b}}, {"seg-000002/names.bin", {13773, 0xb8cc131e2a0cd1c3}},
        {"seg-000002/sig.bin", {72000, 0x4af21852a1a0296c}},    {"seg-000003/annex.bin", {307, 0x518b136493654be1}},
        {"seg-000003/cols.bin", {184, 0x1363f0de17f053f5}},     {"seg-000003/moves.bin", {11, 0xbefbfd65391428cb}},
        {"seg-000003/names.bin", {0, 0xcbf29ce484222325}},      {"seg-000003/sig.bin", {24, 0x21d8b0c2144168cb}},
    };
    const auto games = fixtureGames();
    const ScratchDir dir("store-format.fcgdb");
    {
        auto db = Database::create(dir);
        db->append(games, true, "/x/games.pgn");
        db->append(fcg::test::randomGames(3000, 99, 8), false, "/x/random.pgn");
        db->replace(5, games[7]);
        db->setDeleted(std::vector<GameId>{3, 9, 11}, true);
        Player player;
        player.name      = "Someone, Anyone";
        player.fide      = 123;
        player.spellings = {db->snapshot()->rowIds(0).white, db->snapshot()->rowIds(1).black};
        db->setPlayers(PlayerTable(std::vector<Player>{player}, {{1, 2}}));
    }
    std::map<std::string, std::pair<std::size_t, uint64_t>> now;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().filename() == "tournament.lock") continue;
        std::ifstream in(entry.path(), std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        uint64_t h = 0xcbf29ce484222325ULL;
        for (const unsigned char c : bytes) h = (h ^ c) * 0x100000001b3ULL;
        now[fs::relative(entry.path(), dir).generic_string()] = {bytes.size(), h};
    }
    CHECK(now == before);
}

namespace {

// A folder's entries, the lock left out, each its path, size and bytes in the order of their paths, hashed (FNV-1a):
// what a change leaves in a database's folder, the files it removed or kept included.
uint64_t folderHash(const fs::path& dir) {
    std::map<std::string, std::string> entries;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        const auto name = fs::relative(entry.path(), dir).generic_string();
        if (entry.is_directory())
            entries[name + "/"];
        else if (name != "tournament.lock")
            entries[name] = fcg::test::readFile(entry.path());
    }
    uint64_t h     = 0xcbf29ce484222325ULL;
    const auto mix = [&](std::string_view bytes) {
        for (const unsigned char c : bytes) h = (h ^ c) * 0x100000001b3ULL;
        h = (h ^ 0xFF) * 0x100000001b3ULL;
    };
    for (const auto& [name, bytes] : entries) {
        mix(name);
        mix(std::to_string(bytes.size()));
        mix(bytes);
    }
    return h;
}

// Its first half only, as a disk's damage leaves it.
void cutShort(const fs::path& file) {
    const auto bytes = fcg::test::readFile(file);
    REQUIRE(bytes.size() > 1);
    std::ofstream(file, std::ios::binary | std::ios::trunc) << bytes.substr(0, bytes.size() / 2);
}

}  // namespace

TEST_CASE("store: every change a database commits writes the same bytes as before the store's building blocks (#112)") {
    // Each way the store writes, in turn: a database made, appends (with and without a source, duplicates left out),
    // a replacement, deletions and their undoing, a merge and its undoing, a table of names, a compaction, its
    // undoing, one segment at a time, Keep what can be read (a segment whose names are lost), its undoing, again with
    // a check's damaged games, the database emptied and filled again; then a database whose games all have names, a
    // segment of the empty name added for its games set aside. The folder hashed after each, no snapshot held (the
    // files a change removes or keeps count too). The code of 2dae534f, before the SegmentWriter and the typed
    // Manifest, gave these hashes. Made again only for a change of the format, saying why in the commit.
    const std::vector<std::pair<std::string, uint64_t>> before{
        {"create", 0x4d7b4efc5399c27b},
        {"append", 0x20bec23454383213},
        {"append, duplicates left out", 0x404d16685e68bfe9},
        {"append without a source", 0x3aeefc1ddacd079d},
        {"replace", 0xa90dd22bb0b2a546},
        {"delete", 0x40d9d43bbbdc7e9b},
        {"delete undone", 0x5136997fc2d6e786},
        {"merge", 0x7b64b1765d3af86a},
        {"merge undone", 0x8e6250f6d5f4a698},
        {"players", 0xb64acf0480e9cbee},
        {"compact", 0x2f74a4905decc07b},
        {"compaction undone", 0x31492e7dfefa8962},
        {"compact one segment at a time", 0x44b0fa84654bb7a7},
        {"closed", 0x44b0fa84654bb7a7},
        {"keep what can be read", 0x457b48b99afc3ba4},
        {"keep what can be read undone", 0x7f92a9cf28b18647},
        {"keep what can be read, a check's damaged games", 0x199b0a3e07c3343c},
        {"clear", 0x02c03b7e49169000},
        {"append after clear", 0x0466cf160136e768},
        {"closed again", 0x5a82ccfbc48a1394},
        {"every name given", 0x750393d511c180c3},
        {"keep what can be read, a segment of the empty name", 0x4e1c0d5869579f1c},
        {"closed", 0x832a51f3ac462c77},
    };
    std::vector<std::pair<std::string, uint64_t>> now;
    const auto games = fixtureGames();
    CompactProgress progress;
    std::atomic<bool> cancel{false};
    {
        const ScratchDir dir("store-every-change.fcgdb");
        const auto mark = [&](const char* stage) { now.emplace_back(stage, folderHash(dir)); };
        auto db         = Database::create(dir);
        mark("create");
        db->append(games, true, "/x/games.pgn");
        mark("append");
        auto random = fcg::test::randomGames(400, 7, 6);
        random.insert(random.end(), games.begin(), games.begin() + 5);  // the database has them: left out
        CHECK(db->append(random, false, "/x/random.pgn").duplicates == 5);
        mark("append, duplicates left out");
        db->append({games.begin() + 10, games.begin() + 12}, true);
        mark("append without a source");
        db->replace(5, games[7]);
        mark("replace");
        db->setDeleted(std::vector<GameId>{3, 9, 11, 200}, true);
        mark("delete");
        db->setDeleted(std::vector<GameId>{9}, false);
        mark("delete undone");
        auto merged = db->snapshot()->game(130);
        merged.tags.emplace_back("Annotator", "Ada");
        const auto states = db->merge({{130, merged, {131, 132}}});
        mark("merge");
        db->restore(states);
        mark("merge undone");
        Player player;
        player.name      = "Someone, Anyone";
        player.fide      = 123;
        player.spellings = {db->snapshot()->rowIds(0).white, db->snapshot()->rowIds(1).black};
        db->setPlayers(PlayerTable(std::vector<Player>{player}, {{1, 2}}));
        mark("players");
        REQUIRE(db->compact(progress, cancel).done);
        mark("compact");
        REQUIRE(db->undoCompaction());
        mark("compaction undone");
        REQUIRE(db->compact(progress, cancel, true).done);
        mark("compact one segment at a time");
        const auto second = db->snapshot()->segmentInfos()[1].name;
        db.reset();
        mark("closed");
        cutShort(dir / second / "names.bin");
        db = Database::open(dir);
        REQUIRE(db->snapshot()->unreadable().size() == 1);
        CHECK(db->keepReadable() == 399);
        mark("keep what can be read");
        REQUIRE(db->undoKeepReadable());
        mark("keep what can be read undone");
        CHECK(db->keepReadable(std::vector<GameId>{20, 21}) == 401);
        mark("keep what can be read, a check's damaged games");
        db->clear();
        mark("clear");
        db->append({games.begin(), games.begin() + 3}, true, "/x/again.pgn");
        mark("append after clear");
        db.reset();
        mark("closed again");
    }
    {
        const ScratchDir dir("store-every-name.fcgdb");
        const auto mark = [&](const char* stage) { now.emplace_back(stage, folderHash(dir)); };
        auto named      = fcg::test::randomGames(30, 11, 6);
        for (std::size_t i = 0; i < named.size(); ++i) {
            auto& tags = named[i].tags;
            tags.emplace_back("White", "Moreau, Claire " + std::to_string(i % 4));
            tags.emplace_back("Black", "Duval, Marc " + std::to_string(i % 5));
            tags.emplace_back("Site", "Lyon");
            tags.emplace_back("Annotator", "Club team");
            tags.emplace_back("TimeControl", "5400+30");
        }
        auto db = Database::create(dir);
        for (std::size_t from = 0; from < named.size(); from += 10)
            db->append({named.begin() + static_cast<std::ptrdiff_t>(from),
                        named.begin() + static_cast<std::ptrdiff_t>(from + 10)},
                       true, "/x/club.pgn");
        const auto second = db->snapshot()->segmentInfos()[1].name;
        db.reset();
        mark("every name given");
        cutShort(dir / second / "moves.bin");
        db = Database::open(dir);
        REQUIRE(db->snapshot()->unreadable().size() == 1);
        CHECK(db->keepReadable() == 10);
        mark("keep what can be read, a segment of the empty name");
        db.reset();
        mark("closed");
    }
    REQUIRE(now.size() == before.size());
    for (std::size_t i = 0; i < now.size(); ++i) {
        CAPTURE(before[i].first);
        CHECK(now[i].first == before[i].first);
        CHECK(now[i].second == before[i].second);
    }
}

TEST_CASE("store: the manifest's keys a newer program wrote are kept through every change (#112)") {
    const ScratchDir dir("store-newer-keys.fcgdb");
    const auto games = fcg::test::randomGames(40, 5, 6);
    {
        auto db = Database::create(dir);
        db->append({games.begin(), games.begin() + 20}, true, "/x/first.pgn");
        db->append({games.begin() + 20, games.end()}, true, "/x/second.pgn");
    }
    // Keys of its own at the top and in a segment's entry: this program reads past them and writes them back.
    auto manifest                       = nlohmann::json::parse(fcg::test::readFile(dir / "manifest.json"));
    manifest["newer"]                   = {{"index", "index-000002.bin"}, {"level", 3}};
    manifest["segments"][1]["checksum"] = "c0ffee";
    std::ofstream(dir / "manifest.json", std::ios::binary | std::ios::trunc) << manifest.dump(1);
    const auto kept = [&](bool entry) {
        const auto now = nlohmann::json::parse(fcg::test::readFile(dir / "manifest.json"));
        CHECK(now.value("newer", nlohmann::json()) == manifest["newer"]);
        if (entry) CHECK(now["segments"][1].value("checksum", "") == "c0ffee");
    };
    auto db = Database::open(dir);
    CHECK(db->snapshot()->games() == 40);
    db->append(fcg::test::randomGames(5, 6, 6), true);
    kept(true);
    db->replace(3, games[4]);
    db->setDeleted(std::vector<GameId>{7}, true);
    kept(true);
    auto merged = db->snapshot()->game(10);
    merged.tags.emplace_back("Annotator", "Ada");
    db->restore(db->merge({{10, merged, {11}}}));
    kept(true);
    db->setPlayers(PlayerTable(std::vector<Player>{}, {{1, 2}}));
    kept(true);
    CompactProgress progress;
    std::atomic<bool> cancel{false};
    REQUIRE(db->compact(progress, cancel).done);
    kept(true);
    REQUIRE(db->undoCompaction());
    kept(true);
    REQUIRE(db->compact(progress, cancel, true).done);
    kept(true);
    const auto first = db->snapshot()->segmentInfos().front().name;
    db.reset();
    cutShort(dir / first / "moves.bin");
    db = Database::open(dir);
    REQUIRE(db->snapshot()->unreadable().size() == 1);
    CHECK(db->keepReadable() > 0);
    kept(true);
    REQUIRE(db->undoKeepReadable());
    kept(true);
    db->clear();  // its segments gone, their keys with them
    kept(false);
    CHECK(nlohmann::json::parse(fcg::test::readFile(dir / "manifest.json"))["segments"].empty());
    db.reset();
    CHECK(Database::open(dir)->snapshot()->games() == 0);
}
