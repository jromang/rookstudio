// Lichess's puzzles online (#80): a batch read as puzzles of the database (the game's moves up to the opponent's, then
// the solution), a batch asked for and results sent against a server of the test (the token, the theme, the
// difficulty, the results in one request, the next batch in its answer), a 429 and a refused token said, the batch kept
// in the profile with its answers.
#include <doctest/doctest.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <chess.hpp>
#include <json.hpp>

#include "answering_server.hpp"
#include "console.hpp"
#include "https.hpp"
#include "lichess_api.hpp"
#include "lichess_puzzles.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using namespace fcg::lichess;
using fcg::test::requestPath;
using fcg::test::ScriptedServer;
using namespace fcg::test;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

std::string reply(int status, const std::string& body) {
    return fmt::format(
        "HTTP/1.1 {} X\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", status,
        body.size(), body);
}

// A puzzle as the API gives one, from a random game of our own: its moves in SAN up to the opponent's, then a
// solution of legal moves in UCI. What the test expects of it: the position before the opponent's move, then all the
// moves in UCI.
struct Made {
    nlohmann::json json;
    std::string fen;
    std::vector<std::string> moves;
};
Made madePuzzle(uint32_t seed, const std::string& id) {
    std::mt19937 rng(seed);
    for (;;) {
        chess::Board board;
        std::vector<std::string> sans;
        Made made;
        bool ok = true;
        for (int ply = 0; ply < 20 + static_cast<int>(rng() % 30); ++ply) {
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, board);
            if (moves.empty()) {
                ok = false;
                break;
            }
            const auto m = moves[static_cast<int>(rng() % static_cast<uint32_t>(moves.size()))];
            made.fen     = board.getFen();
            sans.push_back(chess::uci::moveToSan(board, m));
            made.moves = {chess::uci::moveToUci(m)};
            board.makeMove(m);
        }
        if (!ok) continue;
        std::vector<std::string> solution;
        for (int i = 0; i < 3; ++i) {
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, board);
            if (moves.empty()) break;
            const auto m = moves[static_cast<int>(rng() % static_cast<uint32_t>(moves.size()))];
            solution.push_back(chess::uci::moveToUci(m));
            board.makeMove(m);
        }
        if (solution.empty()) continue;
        for (const auto& s : solution) made.moves.push_back(s);
        std::string pgn;
        for (const auto& san : sans) pgn += (pgn.empty() ? "" : " ") + san;
        made.json = {{"game",
                      {{"id", "Gm" + id + "x"},
                       {"perf", {{"key", "rapid"}, {"name", "Rapid"}}},
                       {"rated", true},
                       {"players",
                        {{{"name", "Player_A"}, {"id", "player_a"}, {"color", "white"}, {"rating", 1600}},
                         {{"name", "Player_B"}, {"id", "player_b"}, {"color", "black"}, {"rating", 1620}}}},
                       {"pgn", pgn},
                       {"clock", "10+0"}}},
                     {"puzzle",
                      {{"id", id},
                       {"rating", 1500 + static_cast<int>(seed % 300)},
                       {"plays", 1234},
                       {"solution", solution},
                       {"themes", {"fork", "short"}},
                       {"initialPly", static_cast<int>(sans.size()) - 1}}}};
        return made;
    }
}

nlohmann::json batchJson(std::size_t n, uint32_t seed, std::vector<Made>* made = nullptr) {
    auto puzzles = nlohmann::json::array();
    for (std::size_t i = 0; i < n; ++i) {
        auto m = madePuzzle(seed + static_cast<uint32_t>(i), fmt::format("p{:04}", seed + i));
        puzzles.push_back(m.json);
        if (made) made->push_back(std::move(m));
    }
    return {{"puzzles", puzzles}, {"glicko", {{"rating", 1905.4}, {"deviation", 62.1}}}};
}

}  // namespace

TEST_CASE("lichess puzzles: a batch read as puzzles of the database") {
    std::vector<Made> made;
    const auto batch = readPuzzleBatch(batchJson(5, 80, &made));
    REQUIRE(batch.puzzles.size() == 5);
    CHECK(batch.unread == 0);
    REQUIRE(batch.glicko);
    CHECK(batch.glicko->rating == doctest::Approx(1905.4));
    CHECK(batch.glicko->deviation == doctest::Approx(62.1));
    for (std::size_t i = 0; i < made.size(); ++i) {
        const auto& p = batch.puzzles[i];
        CHECK(p.fen == made[i].fen);
        CHECK(p.moves == made[i].moves);
        CHECK(p.themes == std::vector<std::string>{"fork", "short"});
        CHECK(p.game.rfind("Gm", 0) == 0);
    }
    // A puzzle whose moves do not play, one without its game: left out, counted.
    auto j                                = batchJson(2, 90);
    j["puzzles"][0]["puzzle"]["solution"] = {"a1a1"};
    j["puzzles"][1].erase("game");
    const auto bad = readPuzzleBatch(j);
    CHECK(bad.puzzles.empty());
    CHECK(bad.unread == 2);
    // The results of a send: each puzzle's change, the next batch.
    auto solved      = batchJson(3, 100);
    solved["rounds"] = {{{"id", "p0001"}, {"win", true}, {"ratingDiff", 7}},
                        {{"id", "p0002"}, {"win", false}, {"ratingDiff", -12}}};
    const auto read  = readPuzzleSolved(solved);
    CHECK(read.next.puzzles.size() == 3);
    REQUIRE(read.rounds.size() == 2);
    CHECK(read.rounds[1].id == "p0002");
    CHECK_FALSE(read.rounds[1].win);
    CHECK(read.rounds[1].rating_diff == -12);
}

TEST_CASE("lichess puzzles: a batch asked for, its results sent in one request that brings the next") {
    struct Seen {
        std::string line, authorization, body;
    };
    std::mutex mutex;
    std::vector<Seen> seen;
    std::atomic<int> status{200};
    ScriptedServer server(0, [&](const std::string& head, net::TcpConnection& c) {
        Seen s;
        s.line            = head.substr(0, head.find("\r\n"));
        s.authorization   = httpHeader(head, "Authorization").value_or("");
        const auto length = std::stoul(httpHeader(head, "Content-Length").value_or("0"));
        s.body            = head.substr(head.find("\r\n\r\n") + 4);
        while (s.body.size() < length) {
            const auto more = c.readUntil("\x01never", length - s.body.size(), 2000);
            if (more.empty()) break;
            s.body += more;
        }
        {
            std::lock_guard lock(mutex);
            seen.push_back(s);
        }
        if (status != 200) {
            c.sendAll(reply(status, R"({"error":"refused"})"));
            return;
        }
        if (requestPath(head).rfind("/api/puzzle/batch/", 0) != 0) {
            c.sendAll(reply(404, "{}"));
            return;
        }
        if (s.line.rfind("GET ", 0) == 0) {
            c.sendAll(reply(200, batchJson(50, 200).dump()));
        } else {
            auto j      = batchJson(50, 300);
            j["rounds"] = nlohmann::json::array({{{"id", "p0200"}, {"win", true}, {"ratingDiff", 6}}});
            c.sendAll(reply(200, j.dump()));
        }
    });
    const auto port = server.port();
    REQUIRE(server.ok());
    servers().api = fmt::format("http://127.0.0.1:{}", port);
    HostGate::instance().setPause(50ms);

    // Asked for: the theme in the path, fifty, the difficulty, the token.
    const auto batch = puzzleBatch("tok", "fork", 50, "harder");
    REQUIRE(batch.value);
    CHECK(batch.value->puzzles.size() == 50);
    {
        std::lock_guard lock(mutex);
        REQUIRE(seen.size() == 1);
        CHECK(seen[0].line == "GET /api/puzzle/batch/fork?nb=50&difficulty=harder HTTP/1.1");
        CHECK(seen[0].authorization == "Bearer tok");
    }
    // The results: each puzzle's id, won or not, rated or not; the next batch in the answer.
    std::vector<PuzzleSolution> results;
    for (std::size_t i = 0; i < batch.value->puzzles.size(); ++i)
        results.push_back({batch.value->puzzles[i].id, i % 3 != 0, i % 10 != 0});
    const auto solved = solvePuzzles("tok", "fork", results, 50, "harder");
    REQUIRE(solved.value);
    CHECK(solved.value->next.puzzles.size() == 50);
    CHECK(solved.value->rounds.size() == 1);
    {
        std::lock_guard lock(mutex);
        REQUIRE(seen.size() == 2);
        CHECK(seen[1].line == "POST /api/puzzle/batch/fork?nb=50&difficulty=harder HTTP/1.1");
        const auto body = nlohmann::json::parse(seen[1].body);
        REQUIRE(body["solutions"].size() == 50);
        CHECK(body["solutions"][0] == nlohmann::json{{"id", results[0].id}, {"win", false}, {"rated", false}});
        CHECK(body["solutions"][1] == nlohmann::json{{"id", results[1].id}, {"win", true}, {"rated", true}});
        CHECK(body.size() == 1);  // nothing else of the user's
    }
    // Too many requests, a refused token: said, nothing read.
    status       = 429;
    const auto a = solvePuzzles("tok", "fork", results, 50, "harder");
    CHECK_FALSE(a.value);
    CHECK(a.status == 429);
    CHECK(a.net == NetError::RateLimited);
    CHECK(describe(a).find("wait a minute") != std::string::npos);
    HostGate::instance().setPause(1ms);  // its minute's pause, short for the test
    status       = 401;
    const auto b = puzzleBatch("old", "mix", 50, "normal");
    CHECK_FALSE(b.value);
    CHECK(b.status == 401);
    CHECK(b.net == NetError::Unauthorized);
    CHECK(describe(b) == "Lichess refused the token: link the account again");
    servers() = Servers{};
    HostGate::instance().setPause(60s);
}

TEST_CASE("lichess puzzles: the last days' results, by theme, the most played first") {
    const auto j = nlohmann::json::parse(R"({"days": 30,
        "global": {"firstWins": 120, "nb": 200, "performance": 1900, "puzzleRatingAvg": 1850, "replayWins": 10},
        "themes": {
            "pin": {"theme": "Pin", "results": {"firstWins": 40, "nb": 70, "performance": 1800,
                                                "puzzleRatingAvg": 1840, "replayWins": 3}},
            "fork": {"theme": "Fork", "results": {"firstWins": 90, "nb": 120, "performance": 1950,
                                                  "puzzleRatingAvg": 1860, "replayWins": 5}},
            "odd": "not an object"}})");
    const auto d = readPuzzleDashboard(j);
    CHECK(d.days == 30);
    CHECK(d.nb == 200);
    CHECK(d.first_wins == 120);
    REQUIRE(d.themes.size() == 2);
    CHECK(d.themes[0].key == "fork");
    CHECK(d.themes[0].nb == 120);
    CHECK(d.themes[0].first_wins == 90);
    CHECK(d.themes[1].key == "pin");
    CHECK(readPuzzleDashboard(nlohmann::json::array()).themes.empty());
}

TEST_CASE("lichess puzzles: the batch kept in the profile with its answers") {
    const auto folder = fs::temp_directory_path() / "online-batch";
    fs::remove_all(folder);
    CHECK_FALSE(loadOnlineBatch(folder));
    OnlineBatch batch;
    batch.angle      = "pin";
    batch.difficulty = "easier";
    batch.rated      = false;
    batch.received   = 1'800'000'000;
    batch.puzzles    = readPuzzleBatch(batchJson(4, 400)).puzzles;
    batch.glicko     = PuzzleGlicko{1890, 70, false};
    CHECK_FALSE(batch.complete());
    CHECK(batch.left() == 4);
    batch.answer(batch.puzzles[0].id, true);
    batch.answer(batch.puzzles[0].id, false);  // the first answer only
    batch.answer(batch.puzzles[1].id, false);
    CHECK(batch.results.size() == 2);
    CHECK(batch.results[0].win);
    CHECK_FALSE(batch.results[0].rated);
    CHECK(batch.answered(batch.puzzles[1].id));
    CHECK_FALSE(batch.answered(batch.puzzles[2].id));
    CHECK(batch.left() == 2);
    batch.sent_at     = 1'800'000'100;
    batch.sent_count  = 50;
    batch.sent_status = "accepted";
    REQUIRE(saveOnlineBatch(folder, batch));
    auto kept = loadOnlineBatch(folder);
    REQUIRE(kept);
    CHECK(kept->angle == "pin");
    CHECK(kept->difficulty == "easier");
    CHECK_FALSE(kept->rated);
    CHECK(kept->received == batch.received);
    CHECK(kept->puzzles == batch.puzzles);
    CHECK(kept->results == batch.results);
    CHECK(kept->glicko == batch.glicko);
    CHECK(kept->sent_count == 50);
    CHECK(kept->sent_status == "accepted");
    for (const auto& p : batch.puzzles) kept->answer(p.id, true);
    CHECK(kept->complete());
    forgetOnlineBatch(folder);
    CHECK_FALSE(loadOnlineBatch(folder));
}

TEST_CASE("lichess puzzles: a batch of the wrong types keeps what reads, the rest said; a file cut short too (#136)") {
    const test::ScratchDir folder("online-batch-types");
    OnlineBatch batch;
    batch.angle    = "pin";
    batch.received = 1'800'000'000;
    batch.puzzles  = readPuzzleBatch(batchJson(3, 136)).puzzles;
    batch.answer(batch.puzzles[0].id, true);
    batch.answer(batch.puzzles[1].id, false);
    REQUIRE(saveOnlineBatch(folder, batch));
    const auto file = folder / "online.json";
    // A text for a boolean, a number for a text (an older or newer version, a hand edit): each used to stop the program
    // as soon as the puzzles' page read the batch.
    auto j                 = nlohmann::json::parse(test::readFile(file));
    j["results"][1]["win"] = "no";
    j["sent_status"]       = 7;
    const auto written     = j.dump();
    std::ofstream(file, std::ios::binary) << written;
    const auto before = Console::instance().added();
    std::optional<OnlineBatch> back;
    REQUIRE_NOTHROW(back = loadOnlineBatch(folder));
    REQUIRE(back);
    CHECK(back->angle == "pin");
    CHECK(back->puzzles == batch.puzzles);
    REQUIRE(back->results.size() == 1);
    CHECK(back->results[0].id == batch.puzzles[0].id);
    CHECK(back->sent_status.empty());
    auto said = Console::instance().since(before);
    REQUIRE(said.size() == 1);
    CHECK(said[0].find("2 entries could not be read and were set aside (results/2, sent_status)") != std::string::npos);
    CHECK(test::readFile(fs::path(file).concat(".unreadable")) == written);
    // A file cut short: kept aside, said; a new batch is asked for.
    std::ofstream(file, std::ios::binary) << "{\"angle\": \"pin\", \"puzzles\": [{\"id\": ";
    const auto cut = Console::instance().added();
    CHECK_FALSE(loadOnlineBatch(folder));
    said = Console::instance().since(cut);
    REQUIRE(said.size() == 1);
    CHECK(said[0].find("could not be read: kept as") != std::string::npos);
    CHECK(fs::exists(fs::path(file).concat(".unreadable2")));
}

TEST_CASE("lichess puzzles: each failure of a batch, its answers or the dashboard told by its kind") {
    AnsweringServer server;
    REQUIRE(server.ok());
    HostGate::instance().setPause(50ms);
    const std::vector<PuzzleSolution> results = {{"aaaaa", true, true}};
    struct Case {
        int status;
        const char* body;
        NetError net;
    };
    for (const auto& [status, body, net] :
         {Case{401, R"({"error":"No such token"})", NetError::Unauthorized},
          Case{403, R"({"error":"Missing scope"})", NetError::Forbidden}, Case{404, "{}", NetError::NotFound},
          Case{429, "{}", NetError::RateLimited}, Case{500, "<html>Internal error</html>", NetError::Server},
          Case{200, "<html>not JSON</html>", NetError::BadAnswer}, Case{200, "[1, 2]", NetError::BadAnswer}}) {
        CAPTURE(status);
        CAPTURE(body);
        server.answer(status, body);
        CHECK(puzzleBatch("tok", "mix", 10, "normal").net == net);
        CHECK(solvePuzzles("tok", "mix", results, 10, "normal").net == net);
        CHECK(puzzleDashboard("tok", 30).net == net);
    }
    pointNowhere();
    CHECK(puzzleBatch("tok", "mix", 10, "normal").net == NetError::Offline);
    pointAtPausedHost("lichess-puzzles.invalid");
    std::atomic<bool> cancel{true};
    CHECK(puzzleBatch("tok", "mix", 10, "normal", &cancel).net == NetError::Cancelled);
    CHECK(puzzleDashboard("tok", 30, &cancel).net == NetError::Cancelled);
}
