// An account's Lichess games brought into a database, against a server on this machine that exports games as Lichess
// does: PGN, oldest first, from a date on.
#include <doctest/doctest.hpp>

#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <chess.hpp>
#include <study/store.hpp>

#include "free_ports.hpp"
#include "lichess_api.hpp"
#include "lichess_games.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using namespace fcg::lichess;
using namespace fcg::test;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

struct Exported {
    int64_t start = 0;  // ms
    std::string pgn;
};

// Games as Lichess exports them: tags, then moves with clocks; one a minute from 2026-01-01 on.
std::vector<Exported> lichessGames(int count, int from, uint32_t seed) {
    std::mt19937 random(seed);
    std::vector<Exported> games;
    for (int i = from; i < from + count; ++i) {
        chess::Board board;
        std::string moves;
        const int length = 10 + static_cast<int>(random() % 50);
        for (int ply = 0; ply < length; ++ply) {
            chess::Movelist list;
            chess::movegen::legalmoves(list, board);
            if (list.empty()) break;
            const auto move = list[static_cast<int>(random() % list.size())];
            if (ply % 2 == 0) moves += fmt::format("{}. ", ply / 2 + 1);
            moves += chess::uci::moveToSan(board, move) + fmt::format(" {{ [%clk 0:02:{:02}] }} ", 59 - ply % 60);
            board.makeMove(move);
        }
        const int hour = i / 60, minute = i % 60;
        Exported g;
        g.start = *startOf("2026.01.01", fmt::format("{:02}:{:02}:00", hour % 24, minute)) + (hour / 24) * 86400000LL;
        g.pgn   = fmt::format(
            "[Event \"Rated blitz game\"]\n[Site \"https://lichess.org/g{:07}\"]\n[Date \"2026.01.{:02}\"]\n"
            "[White \"Me\"]\n[Black \"opponent{}\"]\n[Result \"1-0\"]\n[UTCDate \"2026.01.{:02}\"]\n"
            "[UTCTime \"{:02}:{:02}:00\"]\n[WhiteElo \"1900\"]\n[BlackElo \"1850\"]\n[TimeControl "
            "\"180+0\"]\n\n{}1-0\n\n\n",
            i, 1 + hour / 24, i, 1 + hour / 24, hour % 24, minute, moves);
        games.push_back(std::move(g));
    }
    return games;
}

// Lichess's export: the games from `since` on, a piece at a time, chunked; slowly when asked.
struct Export {
    explicit Export(std::vector<Exported> all)
        : games(std::move(all)), port(freePort()), server(port, [this](const std::string& head, net::TcpConnection& c) {
              const auto path = requestPath(head);
              {
                  std::lock_guard lock(mutex);
                  paths.push_back(path);
              }
              std::string folded = path;  // Lichess's names are the same whatever their case
              for (auto& ch : folded) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
              if (folded.rfind("/api/games/user/me?", 0) != 0) {
                  c.sendAll("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                  return;
              }
              int64_t since = 0;
              if (const auto at = path.find("since="); at != std::string::npos) since = std::stoll(path.substr(at + 6));
              c.sendAll(
                  "HTTP/1.1 200 OK\r\nContent-Type: application/x-chess-pgn\r\nTransfer-Encoding: chunked\r\n"
                  "Connection: close\r\n\r\n");
              std::vector<Exported> copy;
              {
                  std::lock_guard lock(mutex);
                  copy = games;
              }
              for (const auto& g : copy) {
                  if (g.start < since) continue;
                  // In two pieces, cut in the middle of the game.
                  const auto half = g.pgn.size() / 2;
                  for (const auto& piece : {g.pgn.substr(0, half), g.pgn.substr(half)})
                      if (!c.sendAll(fmt::format("{:x}\r\n{}\r\n", piece.size(), piece))) return;
                  if (slow) std::this_thread::sleep_for(5ms);
              }
              c.sendAll("0\r\n\r\n");
          }) {
        REQUIRE(server.ok());
        servers().api = fmt::format("http://127.0.0.1:{}", port);
    }
    ~Export() { servers() = Servers{}; }
    std::string lastPath() {
        std::lock_guard lock(mutex);
        return paths.empty() ? std::string() : paths.back();
    }
    std::vector<Exported> games;
    std::atomic<bool> slow{false};
    std::mutex mutex;
    std::vector<std::string> paths;
    uint16_t port;
    ScriptedServer server;
};

std::shared_ptr<study::Database> emptyDatabase(const std::string& name) {
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    return study::Database::create(dir);
}

}  // namespace

TEST_CASE("Lichess games: the start of a game from its UTC tags") {
    CHECK(startOf("1970.01.01", "00:00:01") == 1000);
    CHECK(startOf("2000.03.01", "00:00:00") == int64_t{951868800} * 1000);
    CHECK(startOf("2026.11.18", "14:03:59") == int64_t{1795010639} * 1000);
    CHECK_FALSE(startOf("2026.11.18", ""));
    CHECK_FALSE(startOf("2026.13.01", "00:00:00"));
    CHECK_FALSE(startOf("????.??.??", "00:00:00"));
}

TEST_CASE("Lichess games: an account's games brought in, then those played since, without the ones there") {
    Export lichess(lichessGames(120, 0, 1));
    auto db = emptyDatabase("lichess-sync");
    SyncProgress progress;
    std::atomic<bool> cancel{false};
    const auto first = syncGames(*db, "Me", "", progress, cancel, 50);
    CHECK_MESSAGE(first.error.empty(), first.error);
    CHECK(first.read == 120);
    CHECK(first.added == 120);
    CHECK(first.duplicates == 0);
    CHECK(first.damaged == 0);
    CHECK(progress.games == 120);
    CHECK(db->snapshot()->games() == 120);
    CHECK(lichess.lastPath() == "/api/games/user/Me?clocks=true&evals=true&opening=true&sort=dateAsc");
    const auto state = loadSyncState(db->folder());
    REQUIRE(state);
    CHECK(state->username == "Me");
    CHECK(state->since == lichess.games.back().start);
    // The clocks came with the moves.
    CHECK((db->snapshot()->numbers(0).flags & study::GAME_CLOCKS) != 0);

    // Five games played since: asked from a minute before the latest game's start; the two games of that minute come
    // again and are left out.
    {
        std::lock_guard lock(lichess.mutex);
        for (auto& g : lichessGames(5, 120, 2)) lichess.games.push_back(std::move(g));
    }
    const auto second = syncGames(*db, "me", "", progress, cancel, 50);  // a name is the same whatever its case
    CHECK_MESSAGE(second.error.empty(), second.error);
    CHECK(lichess.lastPath() ==
          fmt::format("/api/games/user/me?clocks=true&evals=true&opening=true&sort=dateAsc&since={}",
                      state->since - 60000));
    CHECK(second.read == 7);
    CHECK(second.added == 5);
    CHECK(second.duplicates == 2);
    CHECK(db->snapshot()->games() == 125);
    CHECK(loadSyncState(db->folder())->since == lichess.games.back().start);

    // The database is another account's.
    const auto other = syncGames(*db, "SomeoneElse", "", progress, cancel);
    CHECK(other.error == "this database holds Me's games");
    CHECK(other.read == 0);
}

TEST_CASE("Lichess games: a cancel keeps what was committed, and the next sync brings the rest") {
    Export lichess(lichessGames(400, 0, 3));
    lichess.slow = true;
    auto db      = emptyDatabase("lichess-cancel");
    SyncProgress progress;
    std::atomic<bool> cancel{false};
    std::thread stopper([&] {
        (void)waitUntil([&] { return progress.games >= 120; });
        cancel = true;
    });
    const auto stopped = syncGames(*db, "Me", "", progress, cancel, 50);
    stopper.join();
    CHECK(stopped.cancelled);
    CHECK(stopped.error.empty());
    const auto kept = db->snapshot()->games();
    CHECK(kept >= 100);
    CHECK(kept < 400);
    CHECK(kept == stopped.added);
    // The state names the latest game committed: the next sync brings the rest.
    const auto state = loadSyncState(db->folder());
    REQUIRE(state);
    CHECK(state->since == lichess.games[kept - 1].start);
    lichess.slow    = false;
    cancel          = false;
    const auto rest = syncGames(*db, "Me", "", progress, cancel, 50);
    CHECK_MESSAGE(rest.error.empty(), rest.error);
    CHECK(db->snapshot()->games() == 400);
    CHECK(rest.duplicates == 2);
}

TEST_CASE("Lichess games: an account Lichess does not have") {
    Export lichess({});
    auto db = emptyDatabase("lichess-none");
    SyncProgress progress;
    std::atomic<bool> cancel{false};
    const auto r = syncGames(*db, "Nobody", "", progress, cancel);
    CHECK(r.error == "Lichess has no account named Nobody");
    CHECK_FALSE(loadSyncState(db->folder()));
}
