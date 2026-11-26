// The continuous analysis of a game: its passes, the principal variation replayed, the analyses kept with the game
// (several, and as the first versions wrote one), and the runner against the mock engine.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>

#include "game_analysis.hpp"
#include "mock_engines.hpp"
#include "review_runner.hpp"
#include "storage.hpp"

using namespace fcg;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

using fcg::test::randomMover;

void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value)
        setenv(name, value, 1);
    else
        unsetenv(name);
#endif
}

struct ScopedEnv {
    const char* name;
    ScopedEnv(const char* n, const char* value) : name(n) { setEnv(n, value); }
    ~ScopedEnv() { setEnv(name, nullptr); }
};

ReplayGame shortGame() {
    ReplayGame game;
    chess::Board board;
    for (const char* uci : {"e2e4", "e7e5", "g1f3", "b8c6"}) {
        ReplayMove m;
        m.move  = chess::uci::uciToMove(board, uci);
        m.san   = chess::uci::moveToSan(board, m.move);
        m.mover = board.sideToMove();
        board.makeMove(m.move);
        game.moves.push_back(m);
    }
    return game;
}

fastchess::Score cp(int64_t v) { return {fastchess::ScoreType::CP, v}; }

}  // namespace

TEST_CASE("continuous analysis: four times longer at each pass, as many passes as the ceiling allows") {
    CHECK(passTime(0) == FIRST_PASS_MS);
    CHECK(passTime(1) == 4 * FIRST_PASS_MS);
    CHECK(passTime(3) == 64 * FIRST_PASS_MS);
    CHECK(reviewPasses(passTime(2)) == 3);
    CHECK(reviewPasses(passTime(2) + 1) == 3);
    CHECK(reviewPasses(5120) == 5);
    CHECK(reviewPasses(1) == 1);   // always one pass
    CHECK(reviewPasses(0) == 13);  // until stopped
}

TEST_CASE("the Chess space's review: its engine processes by default") {
    // The analysis's threads, at least half the physical cores up to four.
    CHECK(reviewProcesses(1, 16, 32, 0) == 4);
    CHECK(reviewProcesses(1, 6, 12, 0) == 3);
    CHECK(reviewProcesses(1, 4, 8, 0) == 2);
    CHECK(reviewProcesses(1, 2, 4, 0) == 1);
    CHECK(reviewProcesses(1, 1, 1, 0) == 1);
    CHECK(reviewProcesses(6, 16, 32, 0) == 6);
    CHECK(reviewProcesses(0, 2, 2, 0) == 1);
    // A tournament playing keeps its cores: the review takes what it leaves, one process at least.
    CHECK(reviewProcesses(1, 16, 32, 30) == 2);
    CHECK(reviewProcesses(6, 16, 32, 20) == 6);
    CHECK(reviewProcesses(1, 16, 32, 32) == 1);
    CHECK(reviewProcesses(1, 4, 8, 7) == 1);
}

TEST_CASE("continuous analysis: the positions a principal variation goes through, legal and before the end") {
    const AnalysisPosition start{std::string(chess::constants::STARTPOS), {"e2e4"}, false};
    // Its last move left out: nothing searched after it.
    auto line = pvPositions(start, "e7e5 g1f3 b8c6", 8);
    REQUIRE(line.size() == 2);
    CHECK(line[0].moves == std::vector<std::string>{"e2e4", "e7e5"});
    CHECK(line[1].moves == std::vector<std::string>{"e2e4", "e7e5", "g1f3"});
    // At most `max`.
    CHECK(pvPositions(start, "e7e5 g1f3 b8c6 f1b5 a7a6", 2).size() == 2);
    // An illegal move ends it, a move alone gives nothing.
    CHECK(pvPositions(start, "e7e5 e1e3 b8c6", 8).size() == 1);
    CHECK(pvPositions(start, "e7e5", 8).empty());
    // Nothing after a mate: 1.f3 e5 2.g4 Qh4#.
    const AnalysisPosition fools{std::string(chess::constants::STARTPOS), {"f2f3", "e7e5", "g2g4"}, false};
    CHECK(pvPositions(fools, "d8h4 a2a3 a7a6", 8).size() == 1);
}

TEST_CASE("continuous analysis: several analyses kept with a game, and one as the first versions wrote it") {
    const auto dir  = fs::temp_directory_path() / "reviews";
    const auto game = shortGame();
    fs::remove_all(dir);
    CHECK(loadReviews(dir, game).empty());

    GameReview one, two;
    one.engine      = "One";
    one.passes      = 3;
    one.movetime_ms = passTime(2);
    for (int i = 0; i < 5; ++i)
        one.positions.push_back({cp(10 * i), "e2e4", 12, passTime(2), "e2e4 e7e5", std::nullopt});
    one.positions[2].wdl = std::array<int64_t, 3>{300, 600, 100};
    one.previous         = {0, 1, 0, 2};
    two                  = one;
    two.engine           = "Two";
    REQUIRE(saveReviews(dir, game, {one, two}));
    auto loaded = loadReviews(dir, game);
    REQUIRE(loaded.size() == 2);
    CHECK(loaded[0].engine == "One");
    CHECK(loaded[0].passes == 3);
    CHECK(loaded[0].positions[3].time_ms == passTime(2));
    CHECK(loaded[0].positions[3].pv == "e2e4 e7e5");
    REQUIRE(loaded[0].positions[2].wdl);
    CHECK((*loaded[0].positions[2].wdl)[1] == 600);
    CHECK(loaded[0].previous == std::vector<int8_t>{0, 1, 0, 2});
    CHECK(loaded[1].engine == "Two");

    // The same engine's analysis saved again: in its place; another engine's after the others.
    two.passes = 4;
    REQUIRE(saveReview(dir, game, two));
    loaded = loadReviews(dir, game);
    REQUIRE(loaded.size() == 2);
    CHECK(loaded[1].engine == "Two");
    CHECK(loaded[1].passes == 4);
    GameReview three = one;
    three.engine     = "Three";
    REQUIRE(saveReview(dir, game, three));
    loaded = loadReviews(dir, game);
    REQUIRE(loaded.size() == 3);
    CHECK(loaded[0].engine == "One");
    CHECK(loaded[2].engine == "Three");

    // A file of the first versions: one analysis at its top level.
    const auto file = dir / (reviewKey(game) + ".json");
    nlohmann::json old;
    old["key"]         = reviewKey(game);
    old["engine"]      = "Old";
    old["movetime_ms"] = 1000;
    old["depth"]       = 0;
    old["positions"]   = nlohmann::json::array({{{"type", "cp"}, {"value", 20}, {"best", "e2e4"}, {"depth", 10}}});
    REQUIRE(writeJson(file, old));
    loaded = loadReviews(dir, game);
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].engine == "Old");
    CHECK(loaded[0].passes == 0);
    CHECK(loaded[0].positions[0].time_ms == 0);

    REQUIRE(saveReviews(dir, game, {}));
    CHECK_FALSE(fs::exists(file));
    fs::remove_all(dir);
}

TEST_CASE("continuous analysis: the mock engine searches every position at each pass, until the ceiling") {
    ScopedEnv env("FCG_MOCK_ANALYSIS", "1");
    const int before = boardThreads();

    ReviewRunner runner;
    ReviewRunner::Setup setup;
    setup.engine.name      = "mover";
    setup.engine.cmd       = randomMover();
    setup.engine.protocol  = "uci";
    setup.settings.threads = 2;
    setup.game             = shortGame();
    setup.ceiling_ms       = passTime(1);  // two passes
    runner.start(setup);

    ReviewRunner::State st;
    uint64_t version = 0;
    const auto until = std::chrono::steady_clock::now() + 60s;
    while (std::chrono::steady_clock::now() < until) {
        (void)runner.state(st, version);
        if (!st.running) break;
        std::this_thread::sleep_for(10ms);
    }
    CHECK(st.error.empty());
    REQUIRE_FALSE(st.running);
    CHECK(st.settled.engine == "mover");
    CHECK(st.settled.passes == 2);
    CHECK(st.settled.movetime_ms == passTime(1));
    REQUIRE(st.settled.positions.size() == 5);
    for (const auto& p : st.settled.positions) {
        CHECK(p.score.has_value());
        CHECK(p.time_ms == passTime(1));
        CHECK_FALSE(p.best.empty());
    }
    // The judgements of the first pass, one per move, to show what the second changed.
    CHECK(st.settled.previous.size() == 4);
    CHECK(st.level == std::vector<int>(5, 1));
    CHECK(runner.threads() == 0);
    CHECK(boardThreads() == before);

    // Resumed after its passes, with a higher ceiling: only the third pass is searched.
    setup.resume     = st.settled;
    setup.ceiling_ms = passTime(2);
    runner.start(setup);
    version = 0;
    while (std::chrono::steady_clock::now() < until + 60s) {
        (void)runner.state(st, version);
        if (!st.running) break;
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE_FALSE(st.running);
    CHECK(st.settled.passes == 3);
    CHECK(st.settled.positions[0].time_ms == passTime(2));
}

TEST_CASE("continuous analysis: stopped, it keeps the passes it completed") {
    ScopedEnv env("FCG_MOCK_ANALYSIS", "1");
    ReviewRunner runner;
    ReviewRunner::Setup setup;
    setup.engine.name     = "mover";
    setup.engine.cmd      = randomMover();
    setup.engine.protocol = "uci";
    setup.game            = shortGame();
    setup.ceiling_ms      = 0;  // until stopped
    runner.start(setup);

    // Once the first pass is complete, stopped during a later one.
    ReviewRunner::State st;
    uint64_t version = 0;
    const auto until = std::chrono::steady_clock::now() + 60s;
    while (std::chrono::steady_clock::now() < until) {
        (void)runner.state(st, version);
        if (st.settled.passes >= 1 || !st.running) break;
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE(st.settled.passes >= 1);
    runner.stop();
    (void)runner.state(st, version);
    CHECK_FALSE(st.running);
    CHECK(st.error.empty());
    CHECK(st.settled.passes >= 1);
    CHECK(st.settled.positions.size() == 5);
    CHECK(runner.threads() == 0);
}

TEST_CASE("continuous analysis: an analysis waits at a pass for the others, then goes on") {
    ScopedEnv env("FCG_MOCK_ANALYSIS", "1");
    ReviewRunner runner;
    ReviewRunner::Setup setup;
    setup.engine.name     = "mover";
    setup.engine.cmd      = randomMover();
    setup.engine.protocol = "uci";
    setup.game            = shortGame();
    setup.ceiling_ms      = passTime(1);
    runner.start(setup);
    runner.setPassLimit(0);

    // Its first pass done, it holds before the second.
    ReviewRunner::State st;
    uint64_t version = 0;
    const auto until = std::chrono::steady_clock::now() + 60s;
    while (std::chrono::steady_clock::now() < until) {
        (void)runner.state(st, version);
        if (st.settled.passes >= 1 || !st.running) break;
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE(st.settled.passes == 1);
    CHECK(st.running);
    CHECK(st.pass == 1);
    CHECK(std::count(st.done.begin(), st.done.end(), true) == 0);

    // Let go, it completes its last pass.
    runner.setPassLimit(1);
    while (std::chrono::steady_clock::now() < until) {
        (void)runner.state(st, version);
        if (!st.running) break;
        std::this_thread::sleep_for(10ms);
    }
    CHECK(st.settled.passes == 2);
}

TEST_CASE("continuous analysis: positions known deep enough already are taken as they are, the others searched") {
    ScopedEnv env("FCG_MOCK_ANALYSIS", "1");
    // The start and the positions after the first two moves known, as Lichess's evaluations kept offline give them.
    const auto game = shortGame();
    std::map<uint64_t, PositionEval> known;
    {
        chess::Board board;
        for (int ply = 0; ply <= 2; ++ply) {
            PositionEval e;
            e.score = cp(20 + ply);
            e.best  = chess::uci::moveToUci(game.moves[static_cast<std::size_t>(ply)].move);
            e.pv    = e.best;
            e.depth = 40;
            e.known = true;
            known.emplace(board.hash(), e);
            board.makeMove(game.moves[static_cast<std::size_t>(ply)].move);
        }
    }
    std::atomic<int> asked{0};
    ReviewRunner runner;
    ReviewRunner::Setup setup;
    setup.engine.name     = "mover";
    setup.engine.cmd      = randomMover();
    setup.engine.protocol = "uci";
    setup.game            = game;
    setup.ceiling_ms      = passTime(1);  // two passes
    setup.known           = [&](const chess::Board& board) -> std::optional<PositionEval> {
        ++asked;
        const auto it = known.find(board.hash());
        if (it == known.end()) return std::nullopt;
        return it->second;
    };
    runner.start(setup);
    ReviewRunner::State st;
    uint64_t version = 0;
    const auto until = std::chrono::steady_clock::now() + 60s;
    while (std::chrono::steady_clock::now() < until) {
        (void)runner.state(st, version);
        if (!st.running) break;
        std::this_thread::sleep_for(10ms);
    }
    CHECK(st.error.empty());
    REQUIRE_FALSE(st.running);
    CHECK(st.settled.passes == 2);
    CHECK(asked == 5);  // once per position, not at each pass
    REQUIRE(st.settled.positions.size() == 5);
    for (std::size_t ply = 0; ply < 5; ++ply) {
        CAPTURE(ply);
        const auto& p = st.settled.positions[ply];
        CHECK(p.time_ms == passTime(1));
        if (ply <= 2) {
            CHECK(p.known);
            CHECK(p.depth == 40);
            CHECK(p.best == chess::uci::moveToUci(game.moves[ply].move));
            REQUIRE(p.score.has_value());
            CHECK(p.score->value == static_cast<int64_t>(20 + ply));
        } else {
            CHECK_FALSE(p.known);  // searched by the engine
            CHECK_FALSE(p.best.empty());
        }
    }
    // Kept with the game as it was found: known, not searched.
    const auto dir = fs::temp_directory_path() / "known-reviews";
    fs::remove_all(dir);
    REQUIRE(saveReview(dir, game, st.settled));
    const auto back = loadReview(dir, game);
    REQUIRE(back);
    CHECK(back->positions[0].known);
    CHECK_FALSE(back->positions[4].known);
}

namespace {

// The settled analysis of `setup`, run to its end (or to `stop_after` complete passes, then stopped).
ReviewRunner::State runToEnd(ReviewRunner& runner, const ReviewRunner::Setup& setup, int stop_after = 0) {
    runner.start(setup);
    ReviewRunner::State st;
    uint64_t version = 0;
    const auto until = std::chrono::steady_clock::now() + 120s;
    while (std::chrono::steady_clock::now() < until) {
        (void)runner.state(st, version);
        if (!st.running || (stop_after > 0 && st.settled.passes >= stop_after)) break;
        std::this_thread::sleep_for(10ms);
    }
    if (stop_after > 0) runner.stop();
    (void)runner.state(st, version);
    return st;
}

ReviewRunner::Setup reproducibleSetup() {
    ReviewRunner::Setup setup;
    setup.engine.name     = "mover";
    setup.engine.cmd      = randomMover();
    setup.engine.protocol = "uci";
    setup.game            = shortGame();
    setup.ceiling_ms      = passTime(1);  // two passes
    setup.reproducible    = true;
    return setup;
}

bool sameScore(const std::optional<fastchess::Score>& a, const std::optional<fastchess::Score>& b) {
    if (!a || !b) return !a && !b;
    return a->type == b->type && a->value == b->value;
}

// The same analysis, position by position: scores, moves, lines and second lines.
void checkSame(const GameReview& a, const GameReview& b) {
    REQUIRE(a.positions.size() == b.positions.size());
    for (std::size_t p = 0; p < a.positions.size(); ++p) {
        CAPTURE(p);
        const auto &x = a.positions[p], &y = b.positions[p];
        CHECK(sameScore(x.score, y.score));
        CHECK(x.best == y.best);
        CHECK(x.pv == y.pv);
        CHECK(x.depth == y.depth);
        CHECK(sameScore(x.second_score, y.second_score));
        CHECK(x.second_pv == y.second_pv);
    }
}

}  // namespace

TEST_CASE(
    "the Chess space's review: each position for a number of nodes from an empty hash, the same whatever the "
    "order, the processes, a stop") {
    ScopedEnv env("FCG_MOCK_ANALYSIS", "1");
    ScopedEnv hash("FCG_MOCK_HASH", "1");  // what a search finds depends on the searches made before it
    CHECK(passNodes(0) == FIRST_PASS_NODES);
    CHECK(passNodes(2) == 16 * FIRST_PASS_NODES);
    CHECK(passNodes(reviewPasses(5120) - 1) == 5120000);  // a ceiling of 5.12 s: up to 5,120,000 nodes
    const int before = boardThreads();

    ReviewRunner runner;
    auto setup     = reproducibleSetup();
    const auto one = runToEnd(runner, setup);
    REQUIRE(one.error.empty());
    REQUIRE_FALSE(one.running);
    CHECK(one.settled.passes == 2);
    CHECK(one.settled.nodes == passNodes(1));
    CHECK(one.settled.movetime_ms == 0);
    REQUIRE(one.settled.positions.size() == 5);
    for (const auto& p : one.settled.positions) {
        REQUIRE(p.score.has_value());
        CHECK_FALSE(p.best.empty());
        CHECK_FALSE(p.second_score.has_value());  // none asked for
    }
    CHECK(runner.threads() == 0);
    CHECK(boardThreads() == before);

    // Three processes side by side, the board on the third position first: the same analysis.
    setup.processes = 3;
    runner.start(setup);
    runner.setFirst(2);
    ReviewRunner::State three;
    uint64_t version = 0;
    const auto until = std::chrono::steady_clock::now() + 120s;
    while (std::chrono::steady_clock::now() < until) {
        (void)runner.state(three, version);
        if (!three.running) break;
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE(three.error.empty());
    checkSame(one.settled, three.settled);
    CHECK(boardThreads() == before);

    // Stopped after its first pass, then resumed: the same again, a longer search of the board's not taken.
    setup.processes  = 1;
    const auto first = runToEnd(runner, setup, 1);
    REQUIRE(first.settled.passes >= 1);
    setup.resume = first.settled;
    runner.start(setup);
    PositionEval longer = one.settled.positions[1];
    longer.score        = cp(999);
    longer.time_ms      = 60000;
    runner.offer(1, longer);
    version = 0;
    ReviewRunner::State resumed;
    while (std::chrono::steady_clock::now() < until + 120s) {
        (void)runner.state(resumed, version);
        if (!resumed.running) break;
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE(resumed.error.empty());
    CHECK(resumed.settled.passes == 2);
    checkSame(one.settled, resumed.settled);

    // The time per position on one engine process, its hash kept: what the same position gets depends on what was
    // searched before it.
    ReviewRunner::Setup timed = setup;
    timed.reproducible        = false;
    timed.resume              = {};
    const auto by_time        = runToEnd(runner, timed);
    REQUIRE(by_time.error.empty());
    REQUIRE(by_time.settled.positions.size() == 5);
    CHECK_FALSE(sameScore(by_time.settled.positions[0].score, one.settled.positions[0].score));
}

TEST_CASE("the Chess space's review: the engine's second line where it is asked for, two lines searched") {
    ScopedEnv env("FCG_MOCK_ANALYSIS", "1");
    std::vector<int> asked;
    std::mutex mutex;
    auto setup        = reproducibleSetup();
    setup.ceiling_ms  = passTime(0);  // one pass
    setup.processes   = 2;
    setup.second_line = [&](int ply, const PositionEval& first) {
        std::lock_guard lock(mutex);
        asked.push_back(ply);
        CHECK(first.score.has_value());
        return ply == 1 || ply == 3;
    };
    ReviewRunner runner;
    const auto st = runToEnd(runner, setup);
    REQUIRE(st.error.empty());
    std::sort(asked.begin(), asked.end());
    CHECK(asked == std::vector<int>{0, 1, 2, 3, 4});  // once per position searched
    REQUIRE(st.settled.positions.size() == 5);
    for (int ply = 0; ply < 5; ++ply) {
        CAPTURE(ply);
        const auto& p = st.settled.positions[static_cast<std::size_t>(ply)];
        if (ply == 1 || ply == 3) {
            // The mock's second line: its second legal move, a score 15 below.
            REQUIRE(p.second_score.has_value());
            REQUIRE(p.score.has_value());
            CHECK(p.second_score->value == p.score->value - 15);
            CHECK_FALSE(p.second_pv.empty());
            CHECK(p.second_pv.substr(0, 4) != p.best);
        } else {
            CHECK_FALSE(p.second_score.has_value());
        }
    }

    // Kept with the game, and read back; a file without them, as before, still opens.
    const auto dir = fs::temp_directory_path() / "second-lines";
    fs::remove_all(dir);
    REQUIRE(saveReview(dir, setup.game, st.settled));
    const auto back = loadReview(dir, setup.game);
    REQUIRE(back);
    CHECK(back->nodes == passNodes(0));
    checkSame(st.settled, *back);
}
