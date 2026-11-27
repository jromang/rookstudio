// The mistakes' module (#107), built and driven without App: a context of the test's own answers what it asks of the
// program (a Board that only keeps the position it is given, a Training page and a day's review that only remember
// what they were told, the user's names, the library's mock engine, the repertoires' positions, the reviews in the
// background started as App starts them), and the module's pages are drawn in an ImGui context of the test's, a key
// pressed as the user presses it. The user's games are games of our own, imported into a database of the test's, two
// of them reviewed (a review file as the Board page keeps one): their mistakes become exercises, a session on the
// Board answers one right and one wrong (judged by the mock engine at the review's settings), both kept in the profile
// for the day's review; a game without a review is reviewed in the background; an answer no engine can judge is said
// and not counted.
#include <doctest/doctest.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <imgui.h>
#include <chess.hpp>
#include <json.hpp>
#include <study/game_tree.hpp>

#include "analysis.hpp"
#include "app_context.hpp"
#include "board_text.hpp"
#include "board_view.hpp"
#include "console.hpp"
#include "databases.hpp"
#include "focus_words.hpp"
#include "game_analysis.hpp"
#include "guide.hpp"
#include "jobs.hpp"
#include "mistakes.hpp"
#include "mistakes_page.hpp"
#include "mock_engines.hpp"
#include "model.hpp"
#include "review_runner.hpp"
#include "test_support.hpp"
#include "training.hpp"

using namespace fcg;
using namespace fcg::test;
namespace fs = std::filesystem;

namespace {

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

// What the module asks of App, answered by the test: the profile of a scratch folder and a shelf of databases in it, a
// Board that keeps the position the module gives it, a Training page and a day's review that only remember what they
// were told, the user's names and engines set by the test, the reviews in the background started as App starts them
// (the mock engine, three passes).
class FakeApp final : public MistakesContext {
   public:
    explicit FakeApp(const fs::path& profile) : profile_(profile), shelf_(jobs_, profile) { settings_.advance_ms = 0; }

    // The program.
    int frame() const override { return frame_; }
    double seconds() const override { return 100; }
    bool shortcutPressed(int) const override { return false; }
    bool scripted() const override { return false; }
    fs::path profileDir() const override { return profile_; }
    DatabaseShelf& shelf() override { return shelf_; }
    void toast(const std::string& text) override { toasts.push_back(text); }
    JobGroup& jobs() override { return jobs_; }

    // The Board taken by a session: what it was before, as the test made it, given back at the end.
    std::string sessionLock() const override { return live_game ? "Locked" : ""; }
    BoardTaken takeBoard(const std::string& title, bool tree) override {
        ++taken;
        board_title    = title;
        board_tree     = tree;
        game           = ReplayGame{};
        game.start_fen = std::string(chess::constants::STARTPOS);
        ply            = 0;
        return before;
    }
    void giveBoardBack(const BoardTaken& board, bool to_training) override {
        given_back.emplace_back(board, to_training);
    }
    void leaveBoard(const BoardTaken& board) override { left.push_back(board); }
    void showSession() override { ++board_page; }
    const ReplayGame& sessionGame() const override { return game; }
    int sessionPly() const override { return ply; }
    void showSessionGame(ReplayGame g) override {
        game = std::move(g);
        ply  = static_cast<int>(game.moves.size());
    }
    void playSessionMove(chess::Move move) override { ply = playOnGame(game, false, ply, move); }
    void setFlip(bool f) override { flip = f; }
    void dropPick() override { ++picks_dropped; }
    void dropTypedMove() override { ++typed_dropped; }
    void beginSessionPage() override { ImGui::TextUnformatted("Board"); }
    void drawBoardMessage() override {}
    void drawSessionBoard(float size, const BoardDecor& marks, bool moves) override {
        board_moves = moves;
        arrows      = marks.arrows.size();
        tints       = marks.tints.size();
        ImGui::Dummy(ImVec2(size, size));
    }
    void drawMoveField() override {}
    void moveKeys() override {}
    void drawBoardTabs() override { ++tabs_drawn; }
    void drawFairPlayBanner() override {}
    void analyse(bool) override {}
    bool analysisKey() override { return false; }

    // The Training page and its day's review.
    const TrainingSettings& trainingSettings() override { return settings_; }
    void setTrainingSettings(const TrainingSettings&) override {}
    void showTrainingPage(int part) override { training_part = part; }
    bool trainingShown() const override { return training_shown; }
    void setEnterStartsPuzzles(bool puzzles) override { enter_puzzles = puzzles; }
    void reviewOnly(ReviewKind kind) override { reviewed_only.push_back(kind); }
    bool reviewRunning() const override { return false; }
    bool nextReview() override { return false; }
    void stopReview() override { ++review_stopped; }
    void drawReviewNext(bool) override {}

    // Fair play; the user's names.
    bool liveGameOn() const override { return live_game; }
    std::vector<std::string> userNames() const override { return names; }
    void drawPlayerNames(const char* id) override { names_drawn.emplace_back(id); }
    void showNamesSettings() override { ++names_settings; }

    // The engines: the analysis's, the library's by name; the reviews in the background.
    const EngineEntry* analysisEngine() override { return engines.empty() ? nullptr : &engines.front(); }
    const AnalysisSettings& analysisSettings() override { return analysis_; }
    const EngineEntry* engineNamed(const std::string& name) const override {
        for (const auto& e : engines)
            if (e.name == name) return &e;
        return nullptr;
    }
    std::unique_ptr<ReviewRunner> reviewInBackground(const EngineEntry& engine, const ReplayGame& g,
                                                     int& processes) override {
        reviewed.push_back(g);
        ReviewRunner::Setup setup;
        setup.engine       = engine;
        setup.game         = g;
        setup.ceiling_ms   = passTime(2);  // three passes: deep enough for exercises
        setup.reproducible = true;
        processes          = 1;
        setup.processes    = processes;
        auto runner        = std::make_unique<ReviewRunner>();
        runner->start(std::move(setup));
        return runner;
    }
    int64_t reviewCeilingMs() override { return passTime(2); }
    bool boardAnalysing() const override { return false; }

    // The repertoires' positions, by side; the games opened on the Board.
    std::array<std::unordered_set<uint64_t>, 2> repertoirePositions() override { return repertoire; }
    void openDatabaseGame(const std::string&, const std::string& title, const fs::path&, const std::string&, uint64_t,
                          int) override {
        opened.push_back(title);
    }
    void openGameOnBoard(ReplayGame, int, std::string title) override { opened.push_back(std::move(title)); }

    // What the module was told, and what the test sets.
    int frame_ = 0;
    std::vector<std::string> toasts;
    BoardTaken before{true, true, 3};  // the Board before a session: a game put aside, Black below, the Tree's tab
    int taken = 0, board_page = 0, picks_dropped = 0, typed_dropped = 0, tabs_drawn = 0;
    std::string board_title;
    bool board_tree = false, board_moves = false, flip = false;
    std::size_t arrows = 0, tints = 0;
    ReplayGame game;
    int ply = 0;
    std::vector<std::pair<BoardTaken, bool>> given_back;
    std::vector<BoardTaken> left;
    int training_part = -1, review_stopped = 0, names_settings = 0;
    bool training_shown = false, enter_puzzles = true, live_game = false;
    std::vector<ReviewKind> reviewed_only;
    std::vector<std::string> names{"Tester"};
    std::vector<std::string> names_drawn;
    std::vector<EngineEntry> engines;
    std::vector<ReplayGame> reviewed;
    std::array<std::unordered_set<uint64_t>, 2> repertoire;
    std::vector<std::string> opened;

   private:
    fs::path profile_;
    JobGroup jobs_;
    DatabaseShelf shelf_;  // after its jobs: gone before them
    TrainingSettings settings_;
    AnalysisSettings analysis_;
};

// An ImGui context of the test's own, without a window: a frame draws a page of the module, a key pressed before it
// (and let go after it, in a frame of its own: Dear ImGui takes one change of a key a frame).
class Frames {
   public:
    Frames() : context_(ImGui::CreateContext()) {
        auto& io = ImGui::GetIO();
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280, 900);
        io.DeltaTime   = 1.0f / 60.0f;
    }
    ~Frames() { ImGui::DestroyContext(context_); }
    Frames(const Frames&)            = delete;
    Frames& operator=(const Frames&) = delete;

    void draw(FakeApp& app, const std::function<void()>& page, ImGuiKey key = ImGuiKey_None) {
        if (key != ImGuiKey_None) ImGui::GetIO().AddKeyEvent(key, true);
        frame(app, page);
        if (key != ImGuiKey_None) {
            ImGui::GetIO().AddKeyEvent(key, false);
            frame(app, page);
        }
    }

   private:
    static void frame(FakeApp& app, const std::function<void()>& page) {
        ++app.frame_;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("page", nullptr, ImGuiWindowFlags_NoDecoration);
        page();
        ImGui::End();
        ImGui::Render();
    }
    ImGuiContext* context_;
};

fastchess::Score cp(int64_t v) { return {fastchess::ScoreType::CP, v}; }

PositionEval eval(int64_t score, const char* best, const char* pv) {
    PositionEval p;
    p.score = cp(score);
    p.best  = best;
    p.pv    = pv;
    p.depth = 18;
    return p;
}

// A review deep enough to name a best move (three passes), by the library's mock engine.
GameReview review(std::vector<PositionEval> positions) {
    GameReview r;
    r.engine    = "Mock";
    r.passes    = 3;
    r.nodes     = passNodes(2);
    r.positions = std::move(positions);
    return r;
}

// The user's games, every name invented. The fork: Black (the user) had 23...Nc2+, forking the king and the rook,
// and played 23...Be6?? instead. The queen: White (the user) leaves the queen on h4 to the knight, 3.d3?? rather than
// 3.Nxh4. The last one, the latest, has no review.
constexpr const char* FORK_FEN  = "r1bq1rk1/ppp2ppp/3p4/8/3nP3/4QN2/PP1B1PPP/R3K2R b KQ - 0 23";
constexpr const char* QUEEN_FEN = "rnb1kbnr/pppp1ppp/8/4p3/4P2q/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3";
constexpr const char* FORK_PGN =
    "[Event \"Club game\"]\n[Date \"2026.11.12\"]\n[White \"Partner, Alex\"]\n[Black \"Tester, Sam\"]\n"
    "[Result \"1-0\"]\n[SetUp \"1\"]\n[FEN \"r1bq1rk1/ppp2ppp/3p4/8/3nP3/4QN2/PP1B1PPP/R3K2R b KQ - 0 23\"]\n\n"
    "23... Be6 24. Nxd4 1-0\n\n";
constexpr const char* QUEEN_PGN =
    "[Event \"Online blitz\"]\n[Date \"2026.11.15\"]\n[White \"Tester, Sam\"]\n[Black \"Partner, Lee\"]\n"
    "[Result \"0-1\"]\n[SetUp \"1\"]\n[FEN \"rnb1kbnr/pppp1ppp/8/4p3/4P2q/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3\"]\n\n"
    "3. d3 Qxe4+ 0-1\n\n";
constexpr const char* LATEST_PGN =
    "[Event \"Club game\"]\n[Date \"2026.11.22\"]\n[White \"Partner, Kim\"]\n[Black \"Tester, Sam\"]\n"
    "[Result \"*\"]\n\n1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 *\n\n";

ReplayGame readGame(const std::string& pgn) {
    std::string error;
    auto game = readPgnGame(pgn, error);
    REQUIRE_MESSAGE(game, error);
    return *game;
}

// The user's games imported into a database of the shelf, the fork's and the queen's reviews kept in the profile.
void userGames(FakeApp& app, const fs::path& profile, bool latest) {
    const auto file = profile / "my-games.pgn";
    std::ofstream(file, std::ios::binary) << FORK_PGN << QUEEN_PGN << (latest ? LATEST_PGN : "");
    auto& shelf      = app.shelf();
    const auto index = shelf.create("My games");
    REQUIRE(shelf.startImport(index, file));
    std::optional<study::DatabaseImport> report;
    REQUIRE(waitUntil([&] { return (report = shelf.finishedImport()).has_value(); }, 60s));
    REQUIRE(report->error.empty());
    REQUIRE(report->added == (latest ? 3u : 2u));
    REQUIRE(saveReview(
        profile / "analyses", readGame(FORK_PGN),
        review({eval(240, "d4c2", "d4c2 e1d1 c2a1"), eval(290, "f3d4", "f3d4 e6d7"), eval(-290, "d8f6", "d8f6")})));
    REQUIRE(saveReview(
        profile / "analyses", readGame(QUEEN_PGN),
        review({eval(880, "f3h4", "f3h4 g8f6"), eval(150, "h4e4", "h4e4 d1e2"), eval(-150, "d1e2", "d1e2 e4e2")})));
}

// The module looking for the user's games on the Training page until `done`.
bool scanned(FakeApp& app, MistakesPage& page, const std::function<bool()>& done) {
    app.training_shown = true;
    return waitUntil(
        [&] {
            ++app.frame_;
            page.poll();
            return done();
        },
        60s);
}

// The Board page, drawn by the session while it holds the Board, as App's hook does (App::drawBoardSession()).
std::function<void()> boardPage(MistakesPage& page) {
    return [&page] {
        if (page.onBoard()) page.drawSessionPage();
    };
}

// The palette's labels.
std::vector<std::string> commands(MistakesPage& page) {
    std::vector<std::string> labels;
    page.addCommands([&](std::string label, std::string, std::function<void()>) { labels.push_back(std::move(label)); },
                     "Study");
    return labels;
}
bool has(const std::vector<std::string>& labels, const std::string& label) {
    return std::find(labels.begin(), labels.end(), label) != labels.end();
}

// The answers kept, a line each.
std::vector<nlohmann::json> answers(const fs::path& profile) {
    std::vector<nlohmann::json> out;
    std::ifstream in(profile / "mistakes" / "answers.jsonl", std::ios::binary);
    for (std::string line; std::getline(in, line);) out.push_back(nlohmann::json::parse(line));
    return out;
}

// The mock engine of the library, the analysis's, searching as an analysis engine does.
EngineEntry mockEngine() {
    EngineEntry e;
    e.name     = "Mock";
    e.cmd      = randomMover();
    e.protocol = "uci";
    return e;
}

}  // namespace

TEST_CASE("mistakes page: a mistake of a reviewed game becomes an exercise, answered right then wrong, kept") {
    ScopedEnv analysis("FCG_MOCK_ANALYSIS", "1");
    ScratchDir profile("mistakes-page");
    FakeApp app(profile);
    app.engines.push_back(mockEngine());
    userGames(app, profile, false);
    MistakesPage page(app);

    // Before the Training page shows, nothing is looked for; then the user's two reviewed games give an exercise each.
    page.poll();
    CHECK(page.mistakeExercises().empty());
    REQUIRE(scanned(app, page, [&] { return page.mistakeExercises().size() == 2; }));
    const auto fork_key  = chess::Board(FORK_FEN).hash();
    const auto queen_key = chess::Board(QUEEN_FEN).hash();
    const auto& record   = page.mistakeRecord();
    REQUIRE(record.exercises.count(fork_key) == 1);
    REQUIRE(record.exercises.count(queen_key) == 1);
    const auto& fork = record.exercises.at(fork_key);
    CHECK(fork.played == "c8e6");
    CHECK(fork.best == "d4c2");
    CHECK(fork.judgement == Judgement::Blunder);
    CHECK(fork.side() == chess::Color::BLACK);
    CHECK(fork.database == "My games");
    CHECK(fork.card.fresh());
    CHECK(record.exercises.at(queen_key).side() == chess::Color::WHITE);
    // Kept in the profile at once; new, none due; the day's review offers nothing yet, the palette today's two.
    CHECK(readMistakeRecord(profile / "mistakes").exercises.size() == 2);
    CHECK(page.reviewCount().kind == ReviewKind::Mistakes);
    CHECK(page.reviewCount().due == 0);
    auto labels = commands(page);
    CHECK(has(labels, "My mistakes"));
    CHECK(has(labels, "Exercises from my mistakes · 2"));
    CHECK_FALSE(has(labels, "Review my mistakes · 1 due"));
    // A position the repertoires train is theirs, not an exercise.
    app.repertoire[1].insert(fork_key);
    ++app.frame_;
    CHECK(page.mistakeExercises().size() == 1);
    app.repertoire[1].clear();
    ++app.frame_;
    REQUIRE(page.mistakeExercises().size() == 2);

    // A session, as its focus word starts it: the Board taken, the latest game's exercise first, from the user's side.
    FocusWords words("mistakes-session");
    page.addFocusWords(words, "mistakes-session");
    REQUIRE(words.applied());
    REQUIRE(page.onBoard());
    CHECK(app.taken == 1);
    CHECK(app.board_title == "Your mistakes");
    CHECK(app.board_tree);
    const auto* run = page.session();
    REQUIRE(run);
    REQUIRE(run->total == 2);
    CHECK(run->exercises[run->current].key == queen_key);
    CHECK(chess::Board(app.game.start_fen).getFen() == chess::Board(QUEEN_FEN).getFen());
    CHECK(app.game.moves.empty());
    CHECK_FALSE(app.flip);  // White below
    CHECK(page.hidesAids());
    CHECK(std::string(page.guideTopic()) == guide::topics::Mistakes);
    Frames frames;
    frames.draw(app, boardPage(page));
    CHECK(app.board_moves);  // the question takes the user's moves
    CHECK(app.tabs_drawn == 0);

    // The review's best move: right, without an engine; graded, kept, the answer written.
    page.sessionMove(chess::uci::uciToMove(chess::Board(QUEEN_FEN), "f3h4"));
    CHECK(run->phase == MistakeRun::Phase::Right);
    CHECK(app.ply == 1);  // the answer on the board
    CHECK_FALSE(page.hidesAids());
    frames.draw(app, boardPage(page));
    CHECK(app.tints == 2);      // the answer in green
    CHECK(app.tabs_drawn > 0);  // with the answer, the tree and the engine come back
    CHECK_FALSE(app.board_moves);
    {
        const auto kept = readMistakeRecord(profile / "mistakes");
        CHECK(kept.exercises.at(queen_key).card.reps == 1);
        CHECK(kept.exercises.at(queen_key).since > 0);
        CHECK(kept.exercises.at(fork_key).card.fresh());
        const auto lines = answers(profile);
        REQUIRE(lines.size() == 1);
        CHECK(lines[0]["played"] == "Nxh4");
        CHECK(lines[0]["rating"].get<int>() >= static_cast<int>(study::fsrs::Rating::Hard));
    }

    // Enter: the other exercise, Black's, the board turned; a move of the user's own, judged by the review's engine
    // (the mock, at the review's settings): not as good, graded, the review's move shown.
    frames.draw(app, boardPage(page), ImGuiKey_Enter);
    REQUIRE(run->phase == MistakeRun::Phase::Asking);
    CHECK(run->exercises[run->current].key == fork_key);
    CHECK(chess::Board(app.game.start_fen).getFen() == chess::Board(FORK_FEN).getFen());
    CHECK(app.flip);
    page.sessionMove(chess::uci::uciToMove(chess::Board(FORK_FEN), "d8e7"));
    CHECK(run->phase == MistakeRun::Phase::Checking);
    CHECK(page.hidesAids());
    REQUIRE(waitUntil(
        [&] {
            page.poll();
            return run->phase != MistakeRun::Phase::Checking;
        },
        60s));
    REQUIRE(run->check_error.empty());
    CHECK(run->phase == MistakeRun::Phase::Wrong);
    CHECK(run->judge_used);
    CHECK_FALSE(run->judge_other);
    CHECK(run->judge_engine == "Mock");
    frames.draw(app, boardPage(page));
    CHECK(app.arrows == 1);  // the review's move, the green arrow
    CHECK(app.tints == 2);   // the answer in orange
    {
        const auto kept = readMistakeRecord(profile / "mistakes");
        CHECK(kept.exercises.at(fork_key).card.reps == 1);
        const auto lines = answers(profile);
        REQUIRE(lines.size() == 2);
        CHECK(lines[1]["played"] == "Qe7");
        CHECK(lines[1]["rating"].get<int>() == static_cast<int>(study::fsrs::Rating::Again));
    }

    // Escape: the session's end, one right, one missed; Escape again: the Board given back as it was, to the Training
    // page, Enter reviewing there again.
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    REQUIRE(run->phase == MistakeRun::Phase::End);
    REQUIRE(run->outcomes.size() == 2);
    CHECK(run->outcomes[0].right);
    CHECK_FALSE(run->outcomes[1].right);
    CHECK(run->outcomes[1].played == "Qe7");
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    CHECK_FALSE(page.onBoard());
    REQUIRE(app.given_back.size() == 1);
    CHECK(app.given_back[0].first.aside);
    CHECK(app.given_back[0].first.flip);
    CHECK(app.given_back[0].first.tab == 3);
    CHECK(app.given_back[0].second);
    CHECK(app.review_stopped == 1);
    CHECK_FALSE(app.enter_puzzles);

    // The day's review: the missed exercise comes back today, the palette offers it, its session asks it.
    const auto count = page.reviewCount();
    CHECK(count.due >= 1);
    labels = commands(page);
    CHECK(has(labels, "Review my mistakes · " + std::to_string(count.due) + " due"));
    REQUIRE(page.startReview());
    REQUIRE(page.onBoard());
    CHECK(page.session()->review);
    CHECK(app.taken == 2);
}

TEST_CASE("mistakes page: a game without a review, reviewed in the background as the Board page reviews one") {
    ScopedEnv analysis("FCG_MOCK_ANALYSIS", "1");
    ScratchDir profile("mistakes-page-reviews");
    FakeApp app(profile);
    app.engines.push_back(mockEngine());
    userGames(app, profile, true);
    MistakesPage page(app);
    // The latest game has no review: the palette offers to review it in the background.
    REQUIRE(scanned(app, page, [&] { return has(commands(page), "Review 1 recent game of mine in the background"); }));
    const auto latest = readGame(LATEST_PGN);
    CHECK_FALSE(fs::exists(profile / "analyses" / (reviewKey(latest) + ".json")));
    const auto before = Console::instance().added();
    page.addCommands(
        [&](const std::string& label, const std::string&, const std::function<void()>& run) {
            if (label == "Review 1 recent game of mine in the background") run();
        },
        "Study");
    REQUIRE(app.reviewed.size() == 1);
    CHECK(reviewKey(app.reviewed[0]) == reviewKey(latest));
    CHECK(has(commands(page), "Cancel the reviews of my games"));
    // Reviewed by the mock engine: the review kept with the game, the game no longer waiting for one.
    REQUIRE(waitUntil(
        [&] {
            page.poll();
            return !has(commands(page), "Cancel the reviews of my games");
        },
        60s));
    CHECK(fs::exists(profile / "analyses" / (reviewKey(latest) + ".json")));
    CHECK_FALSE(has(commands(page), "Review 1 recent game of mine in the background"));
    const auto said = Console::instance().since(before);
    CHECK(std::any_of(said.begin(), said.end(), [](const std::string& line) {
        return line.find("Your games reviewed in the background: 1 game") != std::string::npos;
    }));
    CHECK(app.toasts.empty());
}

TEST_CASE("mistakes page: an answer no engine can judge is said and not counted; the names asked first") {
    ScratchDir profile("mistakes-page-no-engine");
    FakeApp app(profile);  // no engine in the library
    userGames(app, profile, true);
    MistakesPage page(app);
    REQUIRE(scanned(app, page, [&] { return page.mistakeExercises().size() == 2; }));

    // Without an engine, the games without a review are not reviewed: said.
    page.addCommands(
        [&](const std::string& label, const std::string&, const std::function<void()>& run) {
            if (label == "Review 1 recent game of mine in the background") run();
        },
        "Study");
    CHECK(app.reviewed.empty());
    REQUIRE(app.toasts.size() == 1);
    CHECK(app.toasts[0] == "No engine to review with: add one on the Engines page.");

    // A session: a move that is neither the review's nor the game's, with no engine to judge it: said, not counted.
    FocusWords words("mistakes-session");
    page.addFocusWords(words, "mistakes-session");
    REQUIRE(page.onBoard());
    const auto* run  = page.session();
    const auto first = run->exercises[run->current];
    chess::Movelist legal;
    chess::movegen::legalmoves(legal, first.board());
    chess::Move other = chess::Move::NO_MOVE;
    for (const auto m : legal) {
        const auto uci = chess::uci::moveToUci(m);
        if (uci != first.best && uci != first.played && !endedAfter(first, m)) {
            other = m;
            break;
        }
    }
    REQUIRE(other != chess::Move::NO_MOVE);
    page.sessionMove(other);
    CHECK(run->phase == MistakeRun::Phase::Wrong);
    CHECK(run->check_error == "No engine to judge the move: add one on the Engines page.");
    CHECK(run->outcomes.empty());
    CHECK(readMistakeRecord(profile / "mistakes").exercises.at(first.key).card.fresh());
    CHECK_FALSE(fs::exists(profile / "mistakes" / "answers.jsonl"));
    // A live game of the user's on Lichess: no session starts meanwhile.
    Frames frames;
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    REQUIRE_FALSE(page.onBoard());
    app.live_game = true;
    FocusWords again("mistakes-session");
    page.addFocusWords(again, "mistakes-session");
    CHECK_FALSE(page.onBoard());
    CHECK(app.training_part == 2);  // the Training page's part instead

    // Without the user's names, the part asks for them first; Ctrl+K's "Your names…", among the mistakes' commands,
    // leads to Settings, where they are set.
    app.live_game = false;
    app.names.clear();
    frames.draw(app, [&] { page.drawPart(); });
    REQUIRE(app.names_drawn.size() == 1);
    CHECK(app.names_drawn[0] == "training_names");
    page.addCommands(
        [&](const std::string& label, const std::string& where, const std::function<void()>& run) {
            if (label != "Your names…") return;
            CHECK(where == "Settings");
            run();
        },
        "Study");
    CHECK(app.names_settings == 1);
}
