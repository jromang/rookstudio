// Several engines side by side in the study Board's analysis (#78): the threads they share, a GPU engine told apart,
// the words that say how they agree, one arrow per move, their evaluations along the game, their settings kept in
// board.json, and three of the mock engine searching together with their share of the threads.

#include <doctest/doctest.hpp>

#include <chrono>
#include <filesystem>
#include <thread>

#include "../../tournament/tests/self_path.hpp"
#include "board_page.hpp"
#include "engine_client.hpp"
#include "side_by_side.hpp"
#include "storage.hpp"

using namespace fcg;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

std::string randomMover() { return (fcg::test::selfPath().parent_path() / FCG_RANDOM_MOVER).string(); }

// Sets a variable of the environment for as long as it lives.
struct ScopedEnv {
    const char* name;
    ScopedEnv(const char* n, const char* value) : name(n) {
#ifdef _WIN32
        _putenv_s(n, value);
#else
        setenv(n, value, 1);
#endif
    }
    ~ScopedEnv() {
#ifdef _WIN32
        _putenv_s(name, "");
#else
        unsetenv(name);
#endif
    }
};

fastchess::Score cp(int64_t value) { return {fastchess::ScoreType::CP, value}; }
fastchess::Score mate(int64_t value) { return {fastchess::ScoreType::MATE, value}; }

// The strong runs' text: the moves.
std::vector<std::string> strong(const std::vector<TextRun>& runs) {
    std::vector<std::string> out;
    for (const auto& r : runs)
        if (r.strong) out.push_back(r.text);
    return out;
}

AnalysisLine line(int64_t depth, int64_t score, std::vector<std::string> pv) {
    AnalysisLine l;
    l.depth = depth;
    l.score = cp(score);
    l.pv    = std::move(pv);
    return l;
}

}  // namespace

TEST_CASE("analysis side by side: one total of threads, shared equally, those set by hand kept, a GPU's left out") {
    // Equally, what does not divide to the first ones.
    CHECK(shareThreads(24, {{}, {}, {}}) == std::vector<int>{8, 8, 8});
    CHECK(shareThreads(25, {{}, {}, {}}) == std::vector<int>{9, 8, 8});
    CHECK(shareThreads(6, {{}}) == std::vector<int>{6});
    // A GPU engine's threads are its own (0: not sent) and out of the share.
    CHECK(shareThreads(24, {{}, {0, true}, {}}) == std::vector<int>{12, 0, 12});
    CHECK(shareThreads(24, {{}, {3, true}, {}}) == std::vector<int>{12, 3, 12});  // set by hand, still not counted
    // Set by hand: kept, and taken from the total before the others share it.
    CHECK(shareThreads(24, {{4}, {}, {}}) == std::vector<int>{4, 10, 10});
    CHECK(shareThreads(8, {{10}, {}}) == std::vector<int>{10, 1});  // more than the total: the others one each
    // One thread each at least, however small the total.
    CHECK(shareThreads(2, {{}, {}, {}, {}}) == std::vector<int>{1, 1, 1, 1});
    CHECK(shareThreads(0, {{}, {}}) == std::vector<int>{1, 1});
    // Only GPU engines, or none.
    CHECK(shareThreads(16, {{0, true}, {2, true}}) == std::vector<int>{0, 2});
    CHECK(shareThreads(16, {}).empty());
}

TEST_CASE("analysis side by side: a GPU engine, as the library says or as its Backend option names one") {
    EngineEntry e;
    e.name = "net";
    CHECK_FALSE(searchesOnGpu(e));
    e.gpu = true;  // "Uses a GPU" in the library
    CHECK(searchesOnGpu(e));
    e.gpu = false;

    // Lc0: its Backend's default, or the value the library gives it.
    UciOptionInfo backend{"Backend", "combo", "cuda-auto", "", "", {"cuda-auto", "cuda", "blas", "eigen"}};
    e.uci_options = {backend};
    CHECK(searchesOnGpu(e));
    e.uci_options.front().default_value = "blas";
    CHECK_FALSE(searchesOnGpu(e));
    e.setOption("Backend", "cudnn-fp16");
    CHECK(searchesOnGpu(e));
    e.setOption("Backend", "eigen");
    CHECK_FALSE(searchesOnGpu(e));
    // A meta backend runs those its options name.
    e.setOption("Backend", "demux");
    e.uci_options.push_back({"BackendOptions", "string", "", "", "", {}});
    CHECK_FALSE(searchesOnGpu(e));
    e.setOption("BackendOptions", "backend=opencl,(gpu=0),(gpu=1)");
    CHECK(searchesOnGpu(e));
    for (const char* name : {"dx12", "metal", "onnx-dml", "onnx-trt", "OpenCL"}) {
        CAPTURE(name);
        e.setOption("BackendOptions", "");
        e.setOption("Backend", name);
        CHECK(searchesOnGpu(e));
    }
    // A Backend value without an option of that name is not the engine's.
    EngineEntry plain;
    plain.setOption("Backend", "cuda");
    CHECK_FALSE(searchesOnGpu(plain));
}

TEST_CASE("analysis side by side: which engines agree, or how far apart they are, in words") {
    // The mockup's: two on one move, one on another, their evaluations close.
    auto runs = agreementRuns({{"Stockfish", "7…Be7", cp(24)}, {"Leela", "7…Qb6", cp(13)}, {"Gaia", "7…Be7", cp(19)}});
    CHECK(runsText(runs) == "7…Be7 for Stockfish and Gaia · 7…Qb6 for Leela · their evaluations within 0.11");
    CHECK(strong(runs) == std::vector<std::string>{"7…Be7", "7…Qb6"});

    // All on one move.
    runs = agreementRuns({{"Alpha", "1.e4", cp(30)}, {"Beta", "1.e4", cp(30)}});
    CHECK(runsText(runs) == "Alpha and Beta agree on 1.e4 · the same evaluation");
    CHECK(strong(runs) == std::vector<std::string>{"1.e4"});
    CHECK(runsText(agreementRuns({{"A", "1.d4", cp(20)}, {"B", "1.d4", cp(31)}, {"C", "1.d4", cp(25)}})) ==
          "A, B and C agree on 1.d4 · their evaluations within 0.11");

    // Far apart, from either side.
    CHECK(runsText(agreementRuns({{"A", "12.Nf5", cp(30)}, {"B", "12.Qd2", cp(-90)}})) ==
          "12.Nf5 for A · 12.Qd2 for B · their evaluations 1.20 apart");

    // A mate, as each engine sees it, rather than a distance.
    CHECK(runsText(agreementRuns({{"A", "30.Qh7+", mate(3)}, {"B", "30.Qh7+", cp(850)}, {"C", "30.Rd8", mate(3)}})) ==
          "30.Qh7+ for A and B · 30.Rd8 for C · A and C see #3");
    CHECK(runsText(agreementRuns({{"A", "41…Kg8", mate(-5)}, {"B", "41…Kg8", cp(-600)}})) ==
          "A and B agree on 41…Kg8 · A sees #-5");

    // Without a score yet, the moves alone; an engine without a move left out; fewer than two: nothing.
    CHECK(runsText(agreementRuns({{"A", "1.e4", std::nullopt}, {"B", "1.c4", cp(10)}, {"C", "", cp(15)}})) ==
          "1.e4 for A · 1.c4 for B");
    CHECK(agreementRuns({{"A", "1.e4", cp(10)}, {"B", "", std::nullopt}}).empty());
    CHECK(agreementRuns({}).empty());
}

TEST_CASE("analysis side by side: one arrow per move, in the first engine's colour, with how many play it") {
    const auto arrows = sideArrows({"c8e7", "d8b6", "c8e7", ""});
    REQUIRE(arrows.size() == 2);
    CHECK(arrows[0].move == "c8e7");
    CHECK(arrows[0].engine == 0);
    CHECK(arrows[0].count == 2);
    CHECK(arrows[1].move == "d8b6");
    CHECK(arrows[1].engine == 1);
    CHECK(arrows[1].count == 1);
    CHECK(sideArrows({"", "e2e4", "e2e4", "e2e4"}).front().engine == 1);
    CHECK(sideArrows({"", "e2e4", "e2e4", "e2e4"}).front().count == 3);
    CHECK(sideArrows({"", ""}).empty());

    // Their colours: dark blue, light orange, teal, grey, opaque and apart.
    CHECK(sideColour(0) == 0xFFB5551Fu);  // #1f55b5
    CHECK(sideColour(1) == 0xFF2F91E0u);  // #e0912f
    CHECK(sideColour(2) == 0xFF8F9D2Au);  // #2a9d8f
    CHECK(sideColour(3) != sideColour(0));
    CHECK(sideColour(4) == sideColour(0));
}

TEST_CASE("analysis side by side: each engine's evaluation of the game's positions, the deeper search kept") {
    GameReview trail;
    keepTrail(trail, 10, 4, line(12, 35, {"g1f3", "b8c6"}));
    REQUIRE(trail.positions.size() == 11);
    CHECK(trail.positions[4].score->value == 35);
    CHECK(trail.positions[4].best == "g1f3");
    CHECK(trail.positions[4].pv == "g1f3 b8c6");
    CHECK(trail.positions[4].depth == 12);
    CHECK_FALSE(trail.positions[3].score.has_value());

    keepTrail(trail, 10, 4, line(9, 80, {"d2d4"}));  // shallower: the deeper one stays
    CHECK(trail.positions[4].score->value == 35);
    keepTrail(trail, 10, 4, line(15, 20, {"d2d4"}));  // deeper: it replaces it
    CHECK(trail.positions[4].score->value == 20);
    CHECK(trail.positions[4].best == "d2d4");

    auto bound       = line(30, 500, {"e2e4"});
    bound.lowerbound = true;
    keepTrail(trail, 10, 5, bound);                   // a bound is not an evaluation
    keepTrail(trail, 10, 11, line(30, 5, {"a2a3"}));  // beyond the game
    keepTrail(trail, 10, -1, line(30, 5, {"a2a3"}));
    keepTrail(trail, 10, 6, line(30, 5, {}));  // no move
    for (const int p : {5, 6}) CHECK_FALSE(trail.positions[static_cast<std::size_t>(p)].score.has_value());
    CHECK(trail.positions.size() == 11);
}

TEST_CASE("analysis side by side: the engines and their own settings kept in board.json, and read back") {
    BoardSettings s;
    s.analysis.threads = 24;  // the analysis's total
    setAnalysisEngines(s, {{"Stockfish", 4, 256, 2}, {"Leela", 0, 0, 1}, {"Gaia", 0, 128, 3}});
    // The first is the analysis's engine: the review's, the assistant's, the engines' Board's.
    CHECK(s.analysis.engine == "Stockfish");
    CHECK(s.analysis.hash == 256);
    CHECK(s.analysis.multipv == 2);
    CHECK(s.analysis.threads == 24);
    CHECK(s.first_threads == 4);
    REQUIRE(s.side_engines.size() == 2);
    CHECK(s.side_engines[0] == SideEngine{"Leela", 0, 0, 1});

    // Through the profile's file.
    const auto file = fs::temp_directory_path() / "side_by_side" / "board.json";
    fs::create_directories(file.parent_path());
    REQUIRE(writeJson(file, s));
    const auto read = readJson<nlohmann::json>(file);
    REQUIRE(read.has_value());
    const auto back = read->get<BoardSettings>();
    CHECK(analysisEngines(back) == analysisEngines(s));
    CHECK(analysisEngines(back) ==
          std::vector<SideEngine>{{"Stockfish", 4, 256, 2}, {"Leela", 0, 0, 1}, {"Gaia", 0, 128, 3}});
    CHECK(back.analysis.threads == 24);

    // A board.json of before: its one engine.
    auto old = *read;
    old.erase("first_threads");
    old.erase("side_engines");
    const auto before = old.get<BoardSettings>();
    CHECK(analysisEngines(before) == std::vector<SideEngine>{{"Stockfish", 0, 256, 2}});

    // Each engine once, four at most; set back, the list as it reads.
    s.side_engines.push_back({"Stockfish", 2, 0, 3});
    s.side_engines.push_back({"Berserk"});
    s.side_engines.push_back({"Caissa"});
    s.side_engines.push_back({"", 1, 0, 1});
    const auto tidy = analysisEngines(s);
    REQUIRE(tidy.size() == MAX_SIDE_ENGINES);
    CHECK(tidy[3].engine == "Berserk");
    setAnalysisEngines(s, tidy);
    CHECK(analysisEngines(s) == tidy);
    CHECK(s.side_engines.size() == 3);

    // Without engines: none, the analysis's threads kept.
    setAnalysisEngines(s, {});
    CHECK(s.analysis.engine.empty());
    CHECK(analysisEngines(s).empty());
    CHECK(s.analysis.threads == 24);
}

TEST_CASE("analysis side by side: three mock engines search together, each with its share of the threads") {
    if (!fs::is_regular_file(randomMover())) return;
    ScopedEnv env("FCG_MOCK_ANALYSIS", "1");
    const int before = boardThreads();
    std::vector<EngineEntry> entries(3);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        entries[i].name     = "mock " + std::to_string(i + 1);
        entries[i].cmd      = randomMover();
        entries[i].protocol = "uci";
    }
    const auto runs = [&](const std::vector<int>& threads) {
        std::vector<SideRun> out;
        for (std::size_t i = 0; i < threads.size(); ++i)
            out.push_back({&entries[i], {entries[i].name, threads[i], 0, 2}});
        return out;
    };
    const AnalysisPosition start{std::string(chess::constants::STARTPOS), {}, false};
    std::vector<std::unique_ptr<Analyser>> analysers;
    // Polls them, brought to `wanted` each time as the Board page does every frame, until `done` or 10 seconds.
    const auto until = [&](const std::vector<SideRun>& wanted, bool search, const AnalysisPosition& position,
                           const auto& done) {
        const auto end = std::chrono::steady_clock::now() + 10s;
        while (std::chrono::steady_clock::now() < end) {
            runSideBySide(analysers, wanted, search, position, false);
            for (auto& a : analysers) a->poll();
            if (done()) return true;
            std::this_thread::sleep_for(5ms);
        }
        return false;
    };
    const auto searching = [&](int threads) {
        return std::all_of(analysers.begin(), analysers.end(), [&](const auto& a) {
            return a->state() == Analyser::State::Searching && a->threadsInUse() == threads && a->lines().size() == 2 &&
                   a->lines().front().depth == 8;
        });
    };

    // Six threads for three engines: two each.
    auto wanted = runs(shareThreads(6, {{}, {}, {}}));
    REQUIRE(until(wanted, true, start, [&] { return analysers.size() == 3 && searching(2); }));
    CHECK(boardThreads() == before + 6);
    for (std::size_t i = 0; i < 3; ++i) CHECK(analysers[i]->engine() == entries[i].name);

    // One leaves: the two others get three threads each, their processes kept.
    const auto* kept = analysers[0].get();
    wanted           = runs(shareThreads(6, {{}, {}}));
    REQUIRE(until(wanted, true, start, [&] { return analysers.size() == 2 && searching(3); }));
    CHECK(analysers[0].get() == kept);
    CHECK(boardThreads() == before + 6);

    // Another position: both search it.
    const AnalysisPosition after{std::string(chess::constants::STARTPOS), {"e2e4"}, false};
    REQUIRE(until(wanted, true, after, [&] {
        return searching(3) && analysers[0]->position() == after && analysers[1]->position() == after &&
               analysers[1]->lines().front().pv.front() == "a7a6";
    }));

    // The second's threads left to the engine (a GPU engine): its own, as it declared them.
    wanted[1].settings.threads = 0;
    REQUIRE(until(wanted, true, after, [&] {
        return analysers[1]->state() == Analyser::State::Searching && analysers[1]->threadsInUse() == 1;
    }));

    // Not searching: they wait, started, holding no thread.
    REQUIRE(until(wanted, false, after, [&] {
        return std::all_of(analysers.begin(), analysers.end(),
                           [](const auto& a) { return a->state() == Analyser::State::Ready; });
    }));
    CHECK(boardThreads() == before);
    CHECK(analysers[0].get() == kept);

    // An engine the library no longer has ends; one that cannot start keeps its error, not started again.
    wanted[0].entry = nullptr;
    auto broken     = entries[2];
    broken.cmd      = (fs::temp_directory_path() / "no-such-engine-fcg").string();
    wanted[1].entry = &broken;
    runSideBySide(analysers, wanted, true, after, false);
    CHECK(analysers[0]->state() == Analyser::State::Off);
    CHECK(analysers[1]->state() == Analyser::State::Failed);
    const auto error = analysers[1]->error();
    CHECK(error.find(broken.name) != std::string::npos);
    runSideBySide(analysers, wanted, true, after, false);
    CHECK(analysers[1]->state() == Analyser::State::Failed);
    CHECK(analysers[1]->error() == error);

    runSideBySide(analysers, {}, false, after, false);
    CHECK(analysers.empty());
    CHECK(boardThreads() == before);
}
