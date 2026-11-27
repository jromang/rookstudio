// The guessing's module (#107), built and driven without App: a context of the test's own answers what it asks of the
// program (the Board's own game, a Board taken that only keeps the game it is given, a Training page and a day's review
// that only remember what they were told, the library's mock engine as the analysis's), and the module's pages are
// drawn in an ImGui context of the test's, a key pressed as the user presses it. A game of our own on the Board is
// guessed from its first move: the game's move guessed (3 points), another scored by the mock engine, the game
// replayed whole at the end and kept in the profile; a game drawn at random from a database of the test's; without an
// engine nothing starts, and a game at its last move has nothing to guess.
#include <doctest/doctest.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <chess.hpp>
#include <study/game_tree.hpp>

#include "analysis.hpp"
#include "app_context.hpp"
#include "board_text.hpp"
#include "board_view.hpp"
#include "databases.hpp"
#include "focus_words.hpp"
#include "guess_page.hpp"
#include "guide.hpp"
#include "jobs.hpp"
#include "mistakes.hpp"
#include "mock_engines.hpp"
#include "model.hpp"
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

// What the module asks of App, answered by the test: the profile of a scratch folder and a shelf of databases in it,
// the Board's own game set by the test, a Board taken that keeps the game the module replays on it, a Training page
// and a day's review that only remember what they were told, the analysis engine set by the test.
class FakeApp final : public GuessContext {
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

    // The Board taken by a session, and a game replayed on it.
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
        ImGui::Dummy(ImVec2(size, size));
    }
    void drawMoveField() override {}
    void moveKeys() override {}
    void drawBoardTabs() override {}
    void drawFairPlayBanner() override {}
    void analyse(bool) override {}
    bool analysisKey() override { return false; }
    void replaySessionGame(ReplayGame g, int at, bool chess960) override {
        game        = std::move(g);
        ply         = at;
        replayed960 = chess960;
        ++replays;
    }
    void showSessionPly(int at) override { ply = at; }

    // The Training page and its day's review.
    const TrainingSettings& trainingSettings() override { return settings_; }
    void setTrainingSettings(const TrainingSettings& settings) override { settings_ = settings; }
    void showTrainingPage(int part) override { training_part = part; }
    bool trainingShown() const override { return training_part >= 0; }
    void setEnterStartsPuzzles(bool) override {}
    void reviewOnly(ReviewKind) override {}
    bool reviewRunning() const override { return false; }
    bool nextReview() override { return false; }
    void stopReview() override { ++review_stopped; }
    void drawReviewNext(bool) override {}

    // Fair play; the analysis engine; the Board's own game and its tree.
    bool liveGameOn() const override { return live_game; }
    const EngineEntry* analysisEngine() override { return engine ? &*engine : nullptr; }
    const AnalysisSettings& analysisSettings() override { return analysis_; }
    context::ShownGame shownGame() const override {
        return context::ShownGame{shown, shown_ply, false, shown_title, std::nullopt, board_shown, false};
    }
    int treeDatabase() const override { return -1; }

    // What the module was told, and what the test sets.
    int frame_ = 0;
    std::vector<std::string> toasts;
    BoardTaken before{false, true, 1};  // the Board before: no game put aside, Black below, the Analysis tab
    int taken = 0, board_page = 0, picks_dropped = 0, typed_dropped = 0, replays = 0;
    std::string board_title;
    bool board_tree = false, board_moves = false, flip = false, replayed960 = false;
    std::size_t arrows = 0;
    ReplayGame game;
    int ply = 0;
    std::vector<std::pair<BoardTaken, bool>> given_back;
    std::vector<BoardTaken> left;
    int training_part = -1, review_stopped = 0;
    bool live_game = false, board_shown = true;
    std::optional<EngineEntry> engine;
    ReplayGame shown;  // the Board's own game
    int shown_ply = 0;
    std::string shown_title;

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

// A game of our own, long enough to be drawn at random (16 moves and more); two short ones of the user's.
constexpr const char* LONG_PGN =
    "[Event \"Open tournament\"]\n[White \"Player_A\"]\n[Black \"Player_B\"]\n[Result \"*\"]\n\n"
    "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O Be7 6. Re1 b5 7. Bb3 d6 8. c3 O-O 9. h3 Nb8 10. d4 Nbd7 *\n\n";
constexpr const char* SHORT_PGN =
    "[Event \"Club game\"]\n[White \"Tester, Sam\"]\n[Black \"Partner, Alex\"]\n[Result \"1/2-1/2\"]\n\n"
    "1. d4 d5 2. c4 e6 3. Nc3 Nf6 1/2-1/2\n\n"
    "[Event \"Club game\"]\n[White \"Partner, Kim\"]\n[Black \"Tester, Sam\"]\n[Result \"0-1\"]\n\n"
    "1. e4 c5 2. Nf3 d6 0-1\n\n";

ReplayGame readGame(const std::string& pgn) {
    std::string error;
    auto game = readPgnGame(pgn, error);
    REQUIRE_MESSAGE(game, error);
    return *game;
}

EngineEntry mockEngine() {
    EngineEntry e;
    e.name     = "Mock";
    e.cmd      = randomMover();
    e.protocol = "uci";
    return e;
}

// The Board page, drawn by the game guessed while it holds the Board, as App's hook does (App::drawBoardSession()).
std::function<void()> boardPage(GuessPage& page) {
    return [&page] {
        if (page.onBoard()) page.drawSessionPage();
    };
}

// The palette's entries, run by their label.
void runCommand(GuessPage& page, const std::string& wanted) {
    page.addCommands(
        [&](const std::string& label, const std::string&, const std::function<void()>& run) {
            if (label == wanted) run();
        },
        "Study");
}

chess::Move moveOf(const FakeApp& app, const std::string& uci) {
    return chess::uci::uciToMove(boardAt(app.game, false, app.ply), uci);
}

}  // namespace

TEST_CASE("guess page: the Board's game guessed from its first move, a right guess and one scored, kept") {
    ScopedEnv analysis("FCG_MOCK_ANALYSIS", "1");
    ScratchDir profile("guess-page");
    FakeApp app(profile);
    app.engine = mockEngine();
    app.shown  = readGame(LONG_PGN);
    GuessPage page(app);
    CHECK_FALSE(page.lastRecord());

    // The Board page's game, as its focus word starts it: the Board taken, the game up to the position shown replayed
    // on it from White's side, the first question asked.
    FocusWords words("guess-board");
    page.addFocusWords(words, "guess-board");
    REQUIRE(words.applied());
    REQUIRE(page.onBoard());
    CHECK(app.taken == 1);
    CHECK(app.board_title == "Guessing · Player_A – Player_B");
    CHECK(app.board_tree);
    const auto* run = page.session();
    REQUIRE(run);
    CHECK(run->phase == GuessRun::Phase::Asking);
    CHECK(run->side == chess::Color::WHITE);
    CHECK(run->ply == 0);
    CHECK(app.game.moves.empty());  // the game's moves after the position shown are not shown
    CHECK(app.ply == 0);
    CHECK_FALSE(app.replayed960);
    CHECK_FALSE(app.flip);
    CHECK(page.hidesAids());
    CHECK(std::string(page.guideTopic()) == guide::topics::GuessMoves);
    Frames frames;
    frames.draw(app, boardPage(page));
    CHECK(app.board_moves);  // the question takes the user's moves

    // The game's move: 3 points, the game's move shown.
    page.sessionMove(moveOf(app, "e2e4"));
    CHECK(run->phase == GuessRun::Phase::Shown);
    CHECK(run->points == 3);
    REQUIRE(run->guesses.size() == 1);
    CHECK(run->guesses[0].same);
    CHECK_FALSE(page.hidesAids());
    frames.draw(app, boardPage(page));
    CHECK(app.arrows == 1);  // the game's move, in green
    CHECK_FALSE(app.board_moves);

    // Enter: White's next move; another than the game's, weighed against it by the analysis engine (the mock).
    frames.draw(app, boardPage(page), ImGuiKey_Enter);
    REQUIRE(run->phase == GuessRun::Phase::Asking);
    CHECK(run->ply == 2);
    CHECK(app.ply == 2);
    REQUIRE(app.game.moves.size() == 2);
    page.sessionMove(moveOf(app, "b1c3"));
    REQUIRE(waitUntil(
        [&] {
            page.poll();
            return run->phase == GuessRun::Phase::Shown;
        },
        60s));
    CHECK(run->error.empty());
    REQUIRE(run->guesses.size() == 2);
    const auto& scored = run->guesses[1];
    CHECK_FALSE(scored.same);
    CHECK(scored.guess == "b1c3");
    CHECK(scored.game == "g1f3");
    CHECK(scored.points == guessPoints(false, scored.game_pawns, scored.guess_pawns));
    CHECK(run->points == 3 + scored.points);
    frames.draw(app, boardPage(page));
    CHECK(app.arrows == 2);  // the guess in blue, the game's move in green

    // Escape: the end, the whole game on the board at the last question, kept in the profile.
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    REQUIRE(run->phase == GuessRun::Phase::End);
    CHECK(run->stopped);
    CHECK(app.game.moves.size() == readGame(LONG_PGN).moves.size());
    CHECK(app.ply == 2);
    const auto kept = lastGuessRecord(profile / "guess");
    REQUIRE(kept);
    CHECK(kept->title == "Player_A – Player_B");
    CHECK(kept->white);
    CHECK(kept->points == run->points);
    CHECK(kept->max == 6);
    CHECK(kept->moves == 2);
    CHECK(kept->same == 1);
    REQUIRE(page.lastRecord());
    CHECK(page.lastRecord()->points == kept->points);
    // The whole game, a move at a time; Enter: the Board given back, to the Training page.
    frames.draw(app, boardPage(page), ImGuiKey_RightArrow);
    CHECK(app.ply == 3);
    frames.draw(app, boardPage(page), ImGuiKey_Home);
    CHECK(app.ply == 0);
    frames.draw(app, boardPage(page), ImGuiKey_End);
    CHECK(app.ply == static_cast<int>(app.game.moves.size()));
    frames.draw(app, boardPage(page), ImGuiKey_Enter);
    CHECK_FALSE(page.onBoard());
    REQUIRE(app.given_back.size() == 1);
    CHECK(app.given_back[0].second);
    CHECK(app.given_back[0].first.flip);
    CHECK(app.given_back[0].first.tab == 1);
    CHECK(app.review_stopped == 1);
}

TEST_CASE("guess page: a game drawn at random from the database the part chose, the long one") {
    ScopedEnv analysis("FCG_MOCK_ANALYSIS", "1");
    ScratchDir profile("guess-page-random");
    FakeApp app(profile);
    app.engine      = mockEngine();
    const auto file = profile / "games.pgn";
    std::ofstream(file, std::ios::binary) << SHORT_PGN << LONG_PGN;
    auto& shelf      = app.shelf();
    const auto index = shelf.create("Games");
    REQUIRE(shelf.startImport(index, file));
    std::optional<study::DatabaseImport> report;
    REQUIRE(waitUntil([&] { return (report = shelf.finishedImport()).has_value(); }, 60s));
    REQUIRE(report->added == 3);
    GuessPage page(app);
    // The Training page's part chooses the database; a game of it drawn in the background, the only long enough one.
    Frames frames;
    frames.draw(app, [&] { page.drawPart(); });
    runCommand(page, "Guess the moves: a random game");
    REQUIRE(waitUntil(
        [&] {
            page.poll();
            return page.onBoard();
        },
        60s));
    CHECK(app.board_title == "Guessing · Player_A – Player_B · Open tournament");
    CHECK(page.session()->side == chess::Color::WHITE);
    CHECK(page.session()->game.moves.size() == readGame(LONG_PGN).moves.size());
}

TEST_CASE("guess page: without an engine nothing starts, and a game at its last move has nothing to guess") {
    ScratchDir profile("guess-page-none");
    FakeApp app(profile);
    app.shown = readGame(LONG_PGN);
    GuessPage page(app);
    // No engine to score with: said, the Board not taken.
    FocusWords words("guess-board");
    page.addFocusWords(words, "guess-board");
    CHECK_FALSE(page.onBoard());
    CHECK(app.taken == 0);
    REQUIRE(app.toasts.size() == 1);
    CHECK(app.toasts[0] == "No engine to score with: add one on the Engines page.");
    // With one, a game at its last move: nothing left to guess; the palette's entry leads to the Training page's part.
    app.engine    = mockEngine();
    app.shown_ply = static_cast<int>(app.shown.moves.size());
    FocusWords again("guess-board");
    page.addFocusWords(again, "guess-board");
    CHECK_FALSE(page.onBoard());
    REQUIRE(app.toasts.size() == 2);
    CHECK(app.toasts[1] == "No move left to guess after the position shown.");
    runCommand(page, "Guess the moves");
    CHECK_FALSE(page.onBoard());
    CHECK(app.training_part == 3);
    // A live game of the user's on Lichess: no game guessed meanwhile, and no random game offered.
    app.live_game = true;
    app.shown_ply = 0;
    FocusWords locked("guess-board");
    page.addFocusWords(locked, "guess-board");
    CHECK_FALSE(page.onBoard());
    bool offered = false;
    page.addCommands(
        [&](const std::string& label, const std::string&, const std::function<void()>&) {
            offered = offered || label == "Guess the moves: a random game";
        },
        "Study");
    CHECK_FALSE(offered);
}
