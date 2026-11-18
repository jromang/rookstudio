// A PGN file and a tournament read as databases: brought up to date with what was appended, read again when what
// was read changed.
#include <doctest/doctest.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <study/linked.hpp>
#include <study/store.hpp>
#include <tour/state.hpp>

#include "test_games.hpp"

using namespace fcg::study;
using namespace fcg::test;
namespace fs = std::filesystem;

namespace {

std::string pgnOf(const std::vector<std::string>& line, int round, const char* result = "1-0") {
    return "[Event \"Linked\"]\n[Round \"" + std::to_string(round) + "\"]\n[Result \"" + result + "\"]\n\n" +
           movetext(line) + result + "\n\n";
}

void write(const fs::path& file, const std::string& text, bool append) {
    std::ofstream out(file, append ? std::ios::binary | std::ios::app : std::ios::binary | std::ios::trunc);
    out << text;
}

uint64_t games(const LinkedDatabase& linked) { return linked.database()->snapshot()->games(); }

}  // namespace

TEST_CASE("linked: a PGN file, the games appended to it, a game being written, the file rewritten") {
    const auto dir   = fs::temp_directory_path() / "linked-file";
    const auto cache = dir / "cache";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto file  = dir / "games.pgn";
    const auto lines = randomLines(12, 3, 6, 40);
    std::string text;
    for (int i = 0; i < 5; ++i) text += pgnOf(lines[static_cast<std::size_t>(i)], i);
    write(file, text, false);

    std::atomic<bool> cancel{false};
    auto linked = LinkedDatabase::open(cache, {LinkedSource::Kind::PgnFile, file});
    auto r      = linked->refresh(cancel);
    CHECK_MESSAGE(r.error.empty(), r.error);
    CHECK(r.added == 5);
    CHECK_FALSE(r.rebuilt);
    CHECK(games(*linked) == 5);
    CHECK(linked->refresh(cancel).added == 0);

    // Three games appended: only they are read.
    for (int i = 5; i < 8; ++i) write(file, pgnOf(lines[static_cast<std::size_t>(i)], i), true);
    r = linked->refresh(cancel);
    CHECK(r.added == 3);
    CHECK_FALSE(r.rebuilt);
    CHECK(games(*linked) == 8);

    // A game being written waits for its end.
    const auto next = pgnOf(lines[8], 8);
    write(file, next.substr(0, next.size() - 7), true);
    CHECK(linked->refresh(cancel).added == 0);
    write(file, next.substr(next.size() - 7), true);
    CHECK(linked->refresh(cancel).added == 1);
    CHECK(games(*linked) == 9);

    // The program again: what was read is known.
    linked.reset();
    linked = LinkedDatabase::open(cache, {LinkedSource::Kind::PgnFile, file});
    r      = linked->refresh(cancel);
    CHECK(r.added == 0);
    CHECK_FALSE(r.rebuilt);
    CHECK(games(*linked) == 9);

    // The file rewritten before what was read: read again whole.
    std::string other;
    for (int i = 9; i < 12; ++i) other += pgnOf(lines[static_cast<std::size_t>(i)], i);
    write(file, other, false);
    r = linked->refresh(cancel);
    CHECK(r.rebuilt);
    CHECK(r.added == 3);
    CHECK(games(*linked) == 3);

    // What was read lost (a crash between a commit and its note): read again rather than twice.
    fs::remove(cache / (linkedKey({LinkedSource::Kind::PgnFile, file}) + ".json"));
    r = linked->refresh(cancel);
    CHECK(r.rebuilt);
    CHECK(games(*linked) == 3);
}

TEST_CASE("linked: a compressed file is read again only when it changes") {
    const auto dir = fs::temp_directory_path() / "linked-gz";
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::copy_file(data("games.pgn.gz"), dir / "games.pgn.gz");
    std::atomic<bool> cancel{false};
    auto linked = LinkedDatabase::open(dir / "cache", {LinkedSource::Kind::PgnFile, dir / "games.pgn.gz"});
    auto r      = linked->refresh(cancel);
    CHECK_MESSAGE(r.error.empty(), r.error);
    CHECK(r.added >= 100);
    r = linked->refresh(cancel);
    CHECK(r.added == 0);
    CHECK_FALSE(r.rebuilt);
}

namespace {

fastchess::MatchData played(const fcg::tour::GameTask& task, const std::string& result) {
    using chess::GameResult;
    fastchess::MatchData data(chess::constants::STARTPOS);
    const auto white = result == "1-0" ? GameResult::WIN : result == "0-1" ? GameResult::LOSE : GameResult::DRAW;
    const auto black = result == "1-0" ? GameResult::LOSE : result == "0-1" ? GameResult::WIN : GameResult::DRAW;
    data.players     = {fastchess::MatchData::PlayerInfo(task.white, white),
                        fastchess::MatchData::PlayerInfo(task.black, black)};
    data.termination = fastchess::MatchTermination::NORMAL;
    data.reason      = "test";
    data.duration    = "00:00:02";
    return data;
}

}  // namespace

TEST_CASE("linked: a tournament, its games as they finish, a game voided") {
    using namespace fcg::tour;
    const auto dir = fs::temp_directory_path() / "linked-tournament";
    fs::remove_all(dir);
    TournamentSpec spec;
    spec.name      = "linked";
    spec.format    = Format::Match;
    spec.max_games = 8;
    for (int i = 0; i < 2; ++i) {
        Participant p;
        p.id          = i;
        p.engine.name = i == 0 ? "A" : "B";
        spec.participants.push_back(p);
    }
    auto state      = TournamentState::create(dir / "t", spec);
    const auto play = [&](int n) {
        for (int i = 0; i < n; ++i) {
            const auto task = state->claimNext();
            REQUIRE(task);
            state->complete(*task, played(*task, i % 3 == 0 ? "1-0" : i % 3 == 1 ? "0-1" : "1/2-1/2"));
        }
    };
    play(4);
    std::atomic<bool> cancel{false};
    auto linked = LinkedDatabase::open(dir / "cache", {LinkedSource::Kind::Tournament, dir / "t"});
    auto r      = linked->refresh(cancel);
    CHECK_MESSAGE(r.error.empty(), r.error);
    CHECK(r.added == 4);
    CHECK(games(*linked) == 4);

    play(2);
    r = linked->refresh(cancel);
    CHECK(r.added == 2);
    CHECK_FALSE(r.rebuilt);
    CHECK(games(*linked) == 6);
    CHECK(linked->database()->snapshot()->row(5).white == "B");

    // A game voided: it no longer counts, so the database is read again without it.
    Edit drop;
    drop.kind   = Edit::Kind::VoidGames;
    drop.games  = {{0, 1}};
    drop.replay = false;
    state->applyEdit(drop);
    r = linked->refresh(cancel);
    CHECK(r.rebuilt);
    CHECK(r.added == 5);
    CHECK(games(*linked) == 5);
    CHECK(linked->refresh(cancel).added == 0);
}
