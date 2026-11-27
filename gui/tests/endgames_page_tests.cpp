// The endgames' module (#107), built and driven without App: a context of the test's own answers what it asks of the
// program (a Board that only keeps the position it is given, a Training page and a day's review that only remember what
// they were told, the user's mistakes and the puzzles' mates as the test sets them, the tables of the disk: the 3-piece
// Syzygy tables of the repository), and the module's pages are drawn in an ImGui context of the test's, a key pressed
// as the user presses it. An endgame of the catalogue is played against the disk's tables to its verdict, a fault said
// and taken back before the win, the position before it a card of the day's review; a position of the user's games is
// played against Lichess's tables online (a server of the test's, answering as Lichess answered for these positions),
// its fault judged by them; without tables nothing starts, a live game on Lichess holds it all, Lichess's refusal is
// said and the move played meanwhile kept, a question left is parked in the program's jobs.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <chess.hpp>
#include <study/game_tree.hpp>

#include "app_context.hpp"
#include "board_view.hpp"
#include "databases.hpp"
#include "endgames.hpp"
#include "endgames_page.hpp"
#include "focus_words.hpp"
#include "free_ports.hpp"
#include "guide.hpp"
#include "https.hpp"
#include "jobs.hpp"
#include "lichess_api.hpp"
#include "lichess_settings.hpp"
#include "mistakes.hpp"
#include "online_cache.hpp"
#include "puzzles.hpp"
#include "scripted_server.hpp"
#include "syzygy_download.hpp"
#include "tablebases.hpp"
#include "test_support.hpp"
#include "training.hpp"

using namespace fcg;
using namespace fcg::test;
namespace fs = std::filesystem;

namespace {

// The opposition (the catalogue's): Kd6 and Kf6 keep the win, Kd5 and Kf5 let the black king in front of the pawn.
constexpr const char* OPPOSITION = "4k3/8/4K3/4P3/8/8/8/8 w - - 0 1";
// A rook ending of our own (gui/tests/data/tablebase): a draw that Rxh7 loses; the position after Rxh7.
constexpr const char* ROOK_ENDING = "6k1/2R4p/8/5P2/6P1/r7/6K1/8 w - - 0 1";
constexpr const char* AFTER_RXH7  = "6k1/7R/8/5P2/6P1/r7/6K1/8 b - - 0 1";

// The 3-piece tables with their DTZ files, the repository's.
std::string tables3() { return (test::sourcePath() / "gui" / "tests" / "data" / "syzygy3").string(); }

std::string fixture(const char* name) {
    std::ifstream in(test::sourcePath() / "gui" / "tests" / "data" / "tablebase" / name, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// What the module asks of App, answered by the test: the profile of a scratch folder, a Board that keeps the position
// the module gives it, a Training page and a day's review that only remember what they were told, the user's mistakes
// and the puzzles as the test sets them, the tables of the disk the test names (no download).
class FakeApp final : public EndgamesContext {
   public:
    explicit FakeApp(const fs::path& profile) : profile_(profile), shelf_(jobs_, profile) {}

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
    void drawBoardTabs() override {}
    void drawFairPlayBanner() override { ++fair_play_banners; }
    void analyse(bool) override {}
    bool analysisKey() override { return false; }

    // The Training page and its day's review.
    const TrainingSettings& trainingSettings() override { return settings_; }
    void setGuessTheory(bool) override {}
    void showTrainingPage(int part) override { training_part = part; }
    bool trainingShown() const override { return false; }
    void setEnterStartsPuzzles(bool puzzles) override { enter_puzzles = puzzles; }
    void reviewOnly(ReviewKind kind) override { reviewed_only.push_back(kind); }
    bool reviewRunning() const override { return false; }
    bool nextReview() override { return false; }
    void stopReview() override { ++review_stopped; }
    void drawReviewNext(bool) override {}

    // Fair play; Lichess's settings (the online tables' consent).
    bool liveGameOn() const override { return live_game; }
    LichessSettings& lichessSettings() override { return lichess; }
    void saveLichessSettings() override { ++lichess_saved; }
    std::vector<std::string> ownLichessIds() override { return {}; }

    // The user's mistakes, their module's: the exercises the test made.
    std::map<std::string, std::size_t> mistakeKinds(uint32_t) override { return {}; }
    std::vector<const MistakeExercise*> mistakeExercises() override {
        std::vector<const MistakeExercise*> out;
        for (const auto& e : exercises) out.push_back(&e);
        return out;
    }

    // Lichess's puzzles, their module's: the mates' themes the test set, a series started when the database is there.
    ThemeResults puzzleTheme(const std::string& theme) override {
        const auto it = themes.find(theme);
        return it == themes.end() ? ThemeResults{} : it->second;
    }
    bool startPuzzleTheme(const std::string& theme) override {
        themes_started.push_back(theme);
        return puzzles_on_disk;
    }
    bool puzzlesOnDisk() const override { return puzzles_on_disk; }
    bool puzzlesOnline() const override { return false; }

    // The tables of the disk the test names; no download in the program.
    std::string diskTablesPaths() override { return paths; }
    std::string tablesSetting() const override { return paths; }
    int diskTablesPieces() override { return paths.empty() ? 0 : pieces; }
    bool diskTablesLocked() const override { return locked; }
    SyzygyDownload* tablesDownload() override { return nullptr; }
    const SyzygyDownload::State& tablesHeld() override { return held_; }
    const std::string& tablesDownloadError() const override { return download_error_; }
    void startTablesDownload() override { ++downloads; }
    void chooseTablesFolder() override { ++folder_dialogs; }

    // What the module was told, and what the test sets.
    int frame_ = 0;
    std::vector<std::string> toasts;
    BoardTaken before{true, true, 3};  // the Board before a session: a game put aside, Black below, the Tree's tab
    int taken = 0, board_page = 0, picks_dropped = 0, typed_dropped = 0, fair_play_banners = 0;
    std::string board_title;
    bool board_tree = false, board_moves = false, flip = true;
    std::size_t arrows = 0, tints = 0;
    ReplayGame game;
    int ply = 0;
    std::vector<std::pair<BoardTaken, bool>> given_back;
    std::vector<BoardTaken> left;
    int training_part = -1, review_stopped = 0, lichess_saved = 0, downloads = 0, folder_dialogs = 0;
    bool enter_puzzles = true, live_game = false, locked = false, puzzles_on_disk = false;
    std::vector<ReviewKind> reviewed_only;
    LichessSettings lichess;
    std::vector<MistakeExercise> exercises;
    std::map<std::string, ThemeResults> themes;
    std::vector<std::string> themes_started;
    std::string paths;
    int pieces = 3;

   private:
    fs::path profile_;
    JobGroup jobs_;
    DatabaseShelf shelf_;  // after its jobs: gone before them
    TrainingSettings settings_;
    SyzygyDownload::State held_;
    std::string download_error_;
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

// The Board page, drawn by the session while it holds the Board, as App's hook does (App::drawBoardSession()).
std::function<void()> boardPage(EndgamesPage& page) {
    return [&page] {
        if (page.onBoard()) page.drawSessionPage();
    };
}

// The palette's entries, by label.
std::map<std::string, std::function<void()>> commands(EndgamesPage& page) {
    std::map<std::string, std::function<void()>> out;
    page.addCommands([&](std::string label, std::string, std::function<void()> run) { out[label] = std::move(run); },
                     "Study");
    return out;
}

// A move on the Board's position, from UCI.
chess::Move move(const FakeApp& app, const std::string& uci) {
    const auto board = boardAt(app.game, false, app.ply);
    const auto m     = chess::uci::uciToMove(board, uci);
    REQUIRE(m != chess::Move::NO_MOVE);
    return m;
}

// The session's phase once the tables' reply came (half a second after the user's move).
EndgameRun::Phase replied(EndgamesPage& page) {
    REQUIRE(waitUntil(
        [&] {
            page.poll();
            return !page.session() || page.session()->phase != EndgameRun::Phase::Reply;
        },
        30s));
    return page.session()->phase;
}

// Lichess pointed at a port of this machine where nothing listens, unless a test's server stands in for it: nothing
// leaves the machine.
struct NoLichess {
    NoLichess() {
        const std::string nowhere = "http://127.0.0.1:9";
        lichess::servers() = lichess::Servers{nowhere, nowhere + "/tb", nowhere + "/explorer", nowhere + "/engine"};
    }
    ~NoLichess() { lichess::servers() = lichess::Servers{}; }
};

// Lichess's tablebase on this machine: the answers it gave for the rook ending and the position after Rxh7, `unknown`
// for any other; the first `refused` requests answered 429; a request held while `hold` is set, until let go.
struct Tables {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::string> asked;
    int refused   = 0;
    bool hold     = false;
    uint16_t port = freePort();
    ScriptedServer server{
        port, [this](const std::string& head, net::TcpConnection& c) {
            const auto path = head.substr(4, head.find(' ', 4) - 4);
            int status      = 200;
            {
                std::unique_lock lock(mutex);
                asked.push_back(path);
                if (refused > 0) {
                    --refused;
                    status = 429;
                }
                cv.wait_for(lock, 30s, [this] { return !hold; });
            }
            const auto has   = [&](const char* fen) { return path.find(lichess::urlEncode(fen)) != std::string::npos; };
            std::string body = R"({"category":"unknown","moves":[]})";
            if (status == 429)
                body = R"({"error":"Too many requests"})";
            else if (has(ROOK_ENDING))
                body = fixture("rook-ending-7.json");
            else if (has(AFTER_RXH7))
                body = fixture("rook-ending-after-rxh7.json");
            c.sendAll(fmt::format("HTTP/1.1 {} X\r\nContent-Type: application/json\r\nContent-Length: "
                                  "{}\r\nConnection: close\r\n\r\n",
                                  status, body.size()) +
                      body);
        }};
    Tables() {
        REQUIRE(server.ok());
        const auto base    = fmt::format("http://127.0.0.1:{}", port);
        lichess::servers() = lichess::Servers{base, base + "/tb", base + "/explorer", base + "/engine"};
        HostGate::instance().setPause(50ms);
    }
    ~Tables() {
        letGo();
        lichess::servers() = lichess::Servers{};
        HostGate::instance().setPause(60s);
    }
    void letGo() {
        {
            std::lock_guard lock(mutex);
            hold = false;
        }
        cv.notify_all();
    }
    std::size_t requests() {
        std::lock_guard lock(mutex);
        return asked.size();
    }
};

// A mistake of the user's in a position of 7 men, as the mistakes' module keeps one: Rxh7?? in the rook ending.
MistakeExercise rookEndingMistake() {
    MistakeExercise e;
    e.key       = chess::Board(ROOK_ENDING).hash();
    e.start_fen = ROOK_ENDING;
    e.white     = "Tester, Sam";
    e.black     = "Partner, Alex";
    e.played    = "c7h7";
    e.judgement = Judgement::Blunder;
    e.best      = "c7c8";
    return e;
}

}  // namespace

TEST_CASE("endgames page: an endgame of the catalogue against the disk's tables, a fault taken back, then the win") {
    const NoLichess nowhere;
    ScratchDir profile("endgames-page");
    FakeApp app(profile);
    app.paths = tables3();
    Frames frames;
    EndgamesPage page(app);
    {
        const auto lock = lockSyzygy();
        forgetSyzygy();
    }

    // Started as its focus word starts it: the Board taken, the start on it from the user's side, the disk's answer at
    // once (nothing asked of Lichess), the aids hidden while it is played.
    FocusWords words("endgame=opposition");
    page.addFocusWords(words, "endgame=opposition");
    REQUIRE(words.applied());
    REQUIRE(page.onBoard());
    CHECK(app.taken == 1);
    CHECK(app.board_title == "Endgames · The opposition");
    CHECK_FALSE(app.board_tree);
    CHECK_FALSE(app.flip);  // White below
    CHECK(app.game.start_fen == OPPOSITION);
    const auto* run = page.session();
    REQUIRE(run);
    CHECK(run->phase == EndgameRun::Phase::Asking);
    REQUIRE(run->answer);
    CHECK_FALSE(run->online);
    CHECK(run->verdict == 1);  // the tables: a win
    CHECK(page.hidesAids());
    CHECK(std::string(page.guideTopic()) == guide::topics::Endgames);
    frames.draw(app, boardPage(page));
    CHECK(app.board_moves);  // the board takes the user's move

    // The wrong move: Kd5 lets the black king in front of the pawn. The tables reply half a second later, the fault
    // said.
    page.sessionMove(move(app, "e6d5"));
    CHECK(page.session()->fault);
    CHECK(replied(page) == EndgameRun::Phase::Fault);
    run = page.session();
    CHECK(run->verdict == 0);  // a draw, now
    REQUIRE(run->judged);
    CHECK(run->judged->kept.size() == 2);  // Kd6 and Kf6 kept the win
    REQUIRE(run->changes.size() == 2);
    CHECK(run->changes.back().moves.find("1.Kd5?") == 0);
    CHECK(app.game.moves.size() == 2);
    // The position before the fault: a card of the day's review, kept at once, back tomorrow.
    auto kept = readEndgameRecord(profile / "endgames");
    REQUIRE(kept.cards.count(positionKey(OPPOSITION)) == 1);
    CHECK(kept.cards.at(positionKey(OPPOSITION)).entry == "opposition");
    CHECK(page.reviewCount().due == 0);
    CHECK(page.reviewCount().tomorrow == 1);
    // On the board: the fault in orange, the moves that kept the win in green.
    frames.draw(app, boardPage(page));
    CHECK(app.tints == 4);
    CHECK_FALSE(app.board_moves);

    // Enter takes the fault back: the start again, the endgame now counted as helped.
    frames.draw(app, boardPage(page), ImGuiKey_Enter);
    run = page.session();
    CHECK(run->phase == EndgameRun::Phase::Asking);
    CHECK(run->moves.empty());
    CHECK(run->helped);
    CHECK(run->verdict == 1);
    CHECK(app.game.moves.empty());

    // The right moves, the tables' best each time, to the win: the pawn becomes a queen, the win kept.
    for (int guard = 0; page.session()->phase != EndgameRun::Phase::Done; ++guard) {
        REQUIRE(guard < 40);
        run = page.session();
        if (run->phase == EndgameRun::Phase::Asking) {
            REQUIRE(run->answer);
            REQUIRE_FALSE(run->answer->moves.empty());
            page.sessionMove(move(app, run->answer->moves.front().uci));
            CHECK(page.session()->verdict == 1);
        } else {
            CHECK(replied(page) != EndgameRun::Phase::Fault);
        }
    }
    run = page.session();
    CHECK(run->outcome == EndgameOutcome::Won);
    CHECK(run->verdict == 1);
    CHECK_FALSE(page.hidesAids());
    REQUIRE(run->results.size() == 1);
    CHECK(run->results[0].outcome == EndgameOutcome::Won);
    CHECK(run->results[0].fault);
    // The record: tried once, done, the last try with a fault, never clean; the card stays for tomorrow.
    kept = readEndgameRecord(profile / "endgames");
    REQUIRE(kept.results.count("opposition") == 1);
    const auto& result = kept.results.at("opposition");
    CHECK(result.tries == 1);
    CHECK(result.done == 1);
    CHECK(result.missed_last);
    CHECK_FALSE(result.clean);
    CHECK(kept.cards.size() == 1);
    CHECK(kept.answer_ms > 0);
    CHECK(page.doneCount() == 1);

    // Escape: back to the Training page's Endgames part, the Board given back as it was taken.
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    CHECK_FALSE(page.onBoard());
    REQUIRE(app.given_back.size() == 1);
    CHECK(app.given_back[0].first.aside);
    CHECK(app.given_back[0].first.flip);
    CHECK(app.given_back[0].first.tab == 3);
    CHECK(app.given_back[0].second);
    CHECK(app.training_part == 4);
    CHECK_FALSE(app.enter_puzzles);

    // The palette: each endgame of the catalogue; the position missed only once it is due, tomorrow.
    const auto palette = commands(page);
    CHECK(palette.count("Endgame: The opposition") == 1);
    CHECK(palette.count("Review the missed endgames · 1 due") == 0);
    {
        const auto lock = lockSyzygy();
        forgetSyzygy();
    }
}

TEST_CASE("endgames page: a position of the user's games against Lichess's tables online, its fault judged by them") {
    ScratchDir profile("endgames-page-online");
    Tables tables;
    FakeApp app(profile);
    app.paths             = tables3();  // three pieces: the rook ending's seven are Lichess's
    app.lichess.tablebase = 1;          // the online tables' consent
    app.exercises.push_back(rookEndingMistake());
    Frames frames;
    EndgamesPage page(app);

    // The part offers the user's games' endgame; the palette starts it.
    frames.draw(app, [&] { page.drawPart(); });
    auto palette = commands(page);
    REQUIRE(palette.count("Endgames of my games") == 1);
    CHECK(palette.count("Endgame: The opposition") == 1);
    CHECK(palette.count("Endgames") == 1);
    palette.at("Endgames of my games")();
    REQUIRE(page.onBoard());
    CHECK(app.board_title == "Endgames · Endgames of your games");
    CHECK(app.game.start_fen == ROOK_ENDING);
    const auto* run = page.session();
    REQUIRE(run);
    REQUIRE(run->tasks.size() == 1);
    CHECK(run->tasks[0].title == "Tester, Sam – Partner, Alex, 1.Rxh7??");
    CHECK(run->asking.valid());  // asked of Lichess, in the background

    // Lichess's answer: a draw, which becomes the goal of a game's position; kept in the profile for the next time.
    REQUIRE(waitUntil(
        [&] {
            page.poll();
            return page.session()->answer.has_value();
        },
        30s));
    run = page.session();
    CHECK(run->online);
    CHECK(run->verdict == 0);
    CHECK(run->tasks[0].goal == EndgameGoal::Draw);
    CHECK(cachedTablebase(profile, ROOK_ENDING).has_value());
    CHECK(tables.requests() == 1);

    // Rxh7 again: a loss now, judged by Lichess's answer; the tables' reply asked for the position after it.
    page.sessionMove(move(app, "c7h7"));
    CHECK(page.session()->fault);
    CHECK(page.session()->verdict == -1);
    CHECK(replied(page) == EndgameRun::Phase::Fault);
    run = page.session();
    CHECK(tables.requests() == 2);
    CHECK(app.game.moves.size() == 2);
    REQUIRE(run->changes.size() == 2);
    CHECK(run->changes.back().moves.find("1.Rxh7?") == 0);
    CHECK(run->changes.back().verdict == -1);
    REQUIRE(run->judged);
    CHECK(run->judged->kept.size() == 16);  // the moves that kept the draw
    CHECK(readEndgameRecord(profile / "endgames").cards.count(positionKey(ROOK_ENDING)) == 1);
    frames.draw(app, boardPage(page));

    // Escape stops the endgame, then the session; Escape again gives the Board back to the Endgames part.
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    CHECK(page.session()->phase == EndgameRun::Phase::Done);
    CHECK(page.session()->stopped);
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    CHECK(page.session()->phase == EndgameRun::Phase::End);
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    CHECK_FALSE(page.onBoard());
    REQUIRE(app.given_back.size() == 1);
    CHECK(app.training_part == 4);
    // Stopped after a fault: a try that counts for no endgame of the catalogue; the card stays.
    CHECK(readEndgameRecord(profile / "endgames").results.empty());
}

TEST_CASE(
    "endgames page: without tables nothing starts, a live game holds it, Lichess's refusal said, a question left "
    "parked") {
    ScratchDir profile("endgames-page-failures");
    Tables tables;
    FakeApp app(profile);
    Frames frames;
    EndgamesPage page(app);

    // No tables on the disk, no consent to Lichess's: Lucena does not start, the part says what it needs.
    FocusWords words("endgame=lucena");
    page.addFocusWords(words, "endgame=lucena");
    REQUIRE(words.applied());
    CHECK_FALSE(page.onBoard());
    CHECK(app.training_part == 4);
    CHECK(app.taken == 0);
    frames.draw(app, [&] { page.drawPart(); });
    // The user's games' endgames: none can be played, said on the part.
    app.exercises.push_back(rookEndingMistake());
    commands(page).at("Endgames of my games")();
    CHECK_FALSE(page.onBoard());
    frames.draw(app, [&] { page.drawPart(); });
    CHECK(tables.requests() == 0);
    // The day's review has nothing to play.
    FocusWords review("endgames-review");
    app.training_part = -1;
    page.addFocusWords(review, "endgames-review");
    REQUIRE(review.applied());
    CHECK_FALSE(page.onBoard());
    CHECK(app.training_part == 4);

    // A game of the user's played live on Lichess: no endgame starts, the palette offers none.
    app.live_game = true;
    app.paths     = tables3();
    CHECK(commands(page).count("Endgame: The opposition") == 0);
    FocusWords locked("endgame=opposition");
    page.addFocusWords(locked, "endgame=opposition");
    CHECK_FALSE(page.onBoard());
    app.live_game = false;

    // Lichess refuses (429): said, the move played meanwhile kept, to be judged once Lichess answers.
    app.lichess.tablebase = 1;
    tables.refused        = 1;
    commands(page).at("Endgames of my games")();
    REQUIRE(page.onBoard());
    REQUIRE(waitUntil(
        [&] {
            page.poll();
            return !page.session()->waiting.empty();
        },
        30s));
    CHECK(page.session()->waiting_net == lichess::NetError::RateLimited);
    CHECK_FALSE(page.session()->answer);
    page.sessionMove(move(app, "c7c8"));
    REQUIRE(page.session()->kept);
    CHECK(app.game.moves.size() == 1);
    frames.draw(app, boardPage(page));
    // Left for another game: nothing parked, no question under way.
    page.sessionDropped();
    CHECK_FALSE(page.onBoard());
    CHECK(app.left.size() == 1);
    CHECK(app.jobs().parked() == 0);

    // A question under way when the session goes: cancelled, parked in the program's jobs until its task ends.
    {
        std::lock_guard lock(tables.mutex);
        tables.hold = true;
    }
    commands(page).at("Endgames of my games")();
    REQUIRE(page.onBoard());
    REQUIRE(waitUntil([&] { return tables.requests() == 2; }, 30s));
    page.closeSession(false);
    CHECK_FALSE(page.onBoard());
    CHECK(app.given_back.size() == 1);
    CHECK_FALSE(app.given_back[0].second);  // the Board page stays, for the session that comes
    CHECK(app.jobs().parked() == 1);
    tables.letGo();
    REQUIRE(waitUntil([&] { return app.jobs().poll() == 0; }, 30s));
}
