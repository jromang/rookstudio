// Board vision's module (#107), built and driven without App: a context of the test's own answers what it asks of the
// program (a Board that only keeps the position it is given, drawn flat by the test, which clicks the square it wants;
// a Training page that only remembers the part it was told; the Play page's opponent: the library's mock engine), and
// the module's pages are drawn in an ImGui context of the test's, a key pressed or a character typed as the user does.
// The coordinates: a square found with a click, another missed, said; a line read in one's head answered right then
// wrong, kept in vision.json; a game played blindfold against the mock engine, "Where is…?" answered with a click right
// then wrong, stopped by a live game on Lichess; without an engine, no blindfold game starts.

#include <doctest/doctest.hpp>

#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <chess.hpp>
#include <study/game_tree.hpp>

#include "app_context.hpp"
#include "board_view.hpp"
#include "databases.hpp"
#include "engine_play.hpp"
#include "focus_words.hpp"
#include "guide.hpp"
#include "jobs.hpp"
#include "mock_engines.hpp"
#include "model.hpp"
#include "test_support.hpp"
#include "training.hpp"
#include "vision.hpp"
#include "vision_page.hpp"

using namespace fcg;
using namespace fcg::test;
namespace fs = std::filesystem;

namespace {

// What the module asks of App, answered by the test: the profile of a scratch folder, a Board that keeps the position
// the module gives it and is drawn flat by the test (a click on the square the test chose), a Training page that only
// remembers the part it was told, the Play page's opponent the test set.
class FakeApp final : public VisionContext {
   public:
    explicit FakeApp(const fs::path& profile) : profile_(profile), shelf_(jobs_, profile) {}

    // The program.
    int frame() const override { return frame_; }
    double seconds() const override { return 100; }
    bool shortcutPressed(int) const override { return false; }
    bool scripted() const override { return false; }
    fs::path profileDir() const override { return profile_; }
    DatabaseShelf& shelf() override { return shelf_; }
    void toast(const std::string&) override {}
    JobGroup& jobs() override { return jobs_; }

    // The Board taken by an exercise: what it was before, as the test made it, given back at the end.
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
    void drawSessionBoard(float size, const BoardDecor&, bool) override { ImGui::Dummy(ImVec2(size, size)); }
    void drawMoveField() override { ++move_fields; }
    void moveKeys() override {}
    void drawBoardTabs() override {}
    void drawFairPlayBanner() override {}
    void analyse(bool) override {}
    bool analysisKey() override { return false; }

    // The Board drawn flat: what it showed, and the square the test clicks on it, once.
    std::optional<chess::Square> drawFlatBoard(float size, const std::array<chess::Piece, 64>& p,
                                               const BoardDecor& marks, const chess::Board* moves) override {
        pieces      = p;
        coordinates = marks.coordinates;
        tints       = marks.tints.size();
        board_moves = moves != nullptr;
        ImGui::Dummy(ImVec2(size, size));
        return std::exchange(click, std::nullopt);
    }
    void followSessionGame(ReplayGame g) override {
        game = std::move(g);
        ply  = static_cast<int>(game.moves.size());
        ++followed;
    }
    bool typingMove() const override { return false; }

    // The Training page.
    const TrainingSettings& trainingSettings() override { return settings_; }
    void setTrainingSettings(const TrainingSettings&) override {}
    void showTrainingPage(TrainingPart part) override { training_part = part; }
    bool trainingShown() const override { return false; }
    void setEnterStartsPuzzles(bool) override {}
    void reviewOnly(ReviewKind) override {}
    bool reviewRunning() const override { return false; }
    bool nextReview() override { return false; }
    void stopReview() override {}
    void drawReviewNext(bool) override {}

    // Fair play.
    bool liveGameOn() const override { return live_game; }

    // The Play page's opponent: the test's engine at full strength, its own openings.
    const PlaySettings& playSettings() override { return play_; }
    const EngineEntry* playEngine() override { return engines.empty() ? nullptr : &engines.front(); }
    std::string playOpenings(chess::Color) override { return "its own"; }
    std::function<std::optional<std::string>(const chess::Board&)> playBook(chess::Color, std::mt19937&) override {
        return {};
    }
    void showPlayPage() override { ++play_page; }

    // What the module was told, and what the test sets.
    int frame_ = 0;
    BoardTaken before{true, false, 2};  // the Board before an exercise: a game put aside, White below, the Tree's tab
    int taken = 0, board_page = 0, picks_dropped = 0, typed_dropped = 0, move_fields = 0, followed = 0, play_page = 0;
    std::string board_title;
    bool board_tree = false, flip = true, coordinates = true, board_moves = false;
    std::array<chess::Piece, 64> pieces{};
    std::size_t tints = 0;
    std::optional<chess::Square> click;
    ReplayGame game;
    int ply = 0;
    std::vector<std::pair<BoardTaken, bool>> given_back;
    std::vector<BoardTaken> left;
    std::optional<TrainingPart> training_part;
    bool live_game    = false;
    std::vector<EngineEntry> engines;

   private:
    fs::path profile_;
    JobGroup jobs_;
    DatabaseShelf shelf_;  // after its jobs: gone before them
    TrainingSettings settings_;
    PlaySettings play_ = [] {
        PlaySettings s;
        s.full = true;
        return s;
    }();
};

// An ImGui context of the test's own, without a window: a frame draws a page of the module, a key pressed before it
// (and let go after it, in a frame of its own: Dear ImGui takes one change of a key a frame), or a character typed.
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
    void type(FakeApp& app, const std::function<void()>& page, char c) {
        ImGui::GetIO().AddInputCharacter(static_cast<unsigned>(static_cast<unsigned char>(c)));
        frame(app, page);
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

// The Board page, drawn by the exercise while it holds the Board, as App's hook does (App::drawBoardSession()).
std::function<void()> boardPage(VisionPage& page) {
    return [&page] {
        if (page.onBoard()) page.drawSessionPage();
    };
}

// The palette's entries, by label.
std::map<std::string, std::function<void()>> commands(VisionPage& page) {
    std::map<std::string, std::function<void()>> out;
    page.addCommands([&](std::string label, std::string, std::function<void()> run) { out[label] = std::move(run); },
                     "Study");
    return out;
}

// A piece as FEN writes it, "-" for none: the key that answers a line read in one's head.
char pieceKey(chess::Piece piece) {
    if (piece == chess::Piece::NONE) return '-';
    const char c = static_cast<std::string>(piece)[0];
    return c;
}

// The mock engine of the library, the Play page's opponent.
EngineEntry mockEngine() {
    EngineEntry e;
    e.name     = "Mock";
    e.cmd      = randomMover();
    e.protocol = "uci";
    return e;
}

}  // namespace

TEST_CASE("vision page: thirty seconds of coordinates, a square found with a click, another missed") {
    ScratchDir profile("vision-page");
    FakeApp app(profile);
    Frames frames;
    VisionPage page(app);

    // Started as its focus word starts it: the Board taken, the board without its letters and figures, Enter awaited.
    FocusWords words("vision-coordinates");
    page.addFocusWords(words, "vision-coordinates");
    REQUIRE(words.applied());
    REQUIRE(page.onBoard());
    CHECK(app.taken == 1);
    CHECK(app.board_title == "Board vision");
    CHECK_FALSE(app.board_tree);
    CHECK_FALSE(app.flip);  // seen from White
    const auto* v = page.session();
    REQUIRE(v);
    CHECK(v->kind == VisionRun::Kind::Coordinates);
    CHECK(v->mode == CoordMode::Find);
    CHECK(v->phase == VisionRun::Phase::Ready);
    CHECK(page.hidesAids());
    CHECK(std::string(page.guideTopic()) == guide::topics::Vision);
    frames.draw(app, boardPage(page));
    CHECK_FALSE(app.coordinates);
    CHECK_FALSE(app.board_moves);  // a square clicked, no move
    CHECK(app.pieces[0] == chess::Piece::NONE);

    // Enter starts the thirty seconds: a square asked.
    frames.draw(app, boardPage(page), ImGuiKey_Enter);
    v = page.session();
    CHECK(v->phase == VisionRun::Phase::Asking);
    REQUIRE(v->asked != chess::Square::NO_SQ);

    // A click on the square asked: right, the next one asked.
    const auto first = v->asked;
    app.click        = first;
    frames.draw(app, boardPage(page));
    v = page.session();
    CHECK(v->right == 1);
    CHECK(v->faults == 0);
    CHECK(v->asked != first);

    // A click on another square: a fault, the square asked kept in green and the one given in orange.
    const auto asked = v->asked;
    const auto wrong = asked == chess::Square::SQ_A1 ? chess::Square::SQ_H8 : chess::Square::SQ_A1;
    app.click        = wrong;
    frames.draw(app, boardPage(page));
    v = page.session();
    CHECK(v->right == 1);
    CHECK(v->faults == 1);
    REQUIRE(v->missed);
    CHECK(*v->missed == asked);
    REQUIRE(v->wrong);
    CHECK(*v->wrong == wrong);

    // Escape stops before the thirty seconds: the squares said, no record kept.
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    v = page.session();
    CHECK(v->phase == VisionRun::Phase::End);
    CHECK_FALSE(page.hidesAids());
    CHECK(page.visionRecord().coordinates.empty());
    CHECK_FALSE(fs::exists(profile / "vision.json"));

    // Escape again: back to the Training page's Vision part, the Board given back as it was taken.
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    CHECK_FALSE(page.onBoard());
    REQUIRE(app.given_back.size() == 1);
    CHECK(app.given_back[0].first.aside);
    CHECK(app.given_back[0].first.tab == 2);
    CHECK(app.given_back[0].second);
    CHECK(app.training_part == TrainingPart::Vision);

    // The palette: the part, and naming the squares; seen from Black when the part chose it.
    auto palette = commands(page);
    REQUIRE(palette.count("Coordinates: name the square") == 1);
    palette.at("Vision")();
    CHECK(app.training_part == TrainingPart::Vision);
    palette.at("Coordinates: name the square")();
    REQUIRE(page.onBoard());
    CHECK(page.session()->mode == CoordMode::Name);
    frames.draw(app, boardPage(page), ImGuiKey_Enter);
    // The square marked in blue, named with its two keys.
    const auto marked = page.session()->asked;
    const auto name   = squareName(marked);
    frames.type(app, boardPage(page), name[0]);
    frames.type(app, boardPage(page), name[1]);
    CHECK(page.session()->right == 1);
    CHECK(app.tints == 1);  // the next square marked
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    CHECK_FALSE(page.onBoard());
}

TEST_CASE("vision page: a line read in one's head, answered right then wrong, kept in vision.json") {
    ScratchDir profile("vision-page-lines");
    FakeApp app(profile);
    Frames frames;
    VisionPage page(app);

    FocusWords words("vision-lines");
    page.addFocusWords(words, "vision-lines");
    REQUIRE(page.onBoard());
    const auto* v = page.session();
    REQUIRE(v);
    CHECK(v->kind == VisionRun::Kind::Line);
    CHECK(v->phase == VisionRun::Phase::Asking);
    REQUIRE(v->lines.size() == 10);
    CHECK(app.flip == (chess::Board(v->lines[0].fen).sideToMove() == chess::Color::BLACK));
    frames.draw(app, boardPage(page));
    CHECK(app.tints == 1);  // the square asked about, in blue

    // The right piece, typed as FEN writes it: right, the position after the moves shown.
    const auto right = v->lines[0].answer;
    frames.type(app, boardPage(page), pieceKey(right));
    v = page.session();
    CHECK(v->phase == VisionRun::Phase::Answered);
    CHECK(v->lines_right == 1);
    // Enter: the next line.
    frames.draw(app, boardPage(page), ImGuiKey_Enter);
    v = page.session();
    CHECK(v->line == 1);
    CHECK(v->phase == VisionRun::Phase::Asking);

    // A wrong piece: said, not counted right.
    const auto answer = v->lines[1].answer;
    const char wrong  = answer == chess::Piece(chess::PieceType::QUEEN, chess::Color::WHITE) ? 'q' : 'Q';
    frames.type(app, boardPage(page), wrong);
    v = page.session();
    CHECK(v->phase == VisionRun::Phase::Answered);
    CHECK(v->lines_right == 1);
    REQUIRE(v->said);
    CHECK(*v->said != answer);

    // Escape stops: the two lines read kept in the profile, one right.
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    CHECK(page.session()->phase == VisionRun::Phase::End);
    const auto kept = readVisionRecord(profile);
    CHECK(kept.lines == 2);
    CHECK(kept.lines_right == 1);
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    CHECK_FALSE(page.onBoard());
}

TEST_CASE(
    "vision page: blindfold against the mock engine, Where is…? clicked right then wrong; without an engine, none") {
    ScratchDir profile("vision-page-blindfold");
    FakeApp app(profile);
    Frames frames;
    VisionPage page(app);

    // Without an engine in the library: nothing starts, the part says why.
    FocusWords none("vision-blindfold");
    page.addFocusWords(none, "vision-blindfold");
    REQUIRE(none.applied());
    CHECK_FALSE(page.onBoard());
    CHECK(app.taken == 0);
    frames.draw(app, [&] { page.drawPart(); });

    // The mock engine, the Play page's: the user White, every piece hidden.
    app.engines.push_back(mockEngine());
    FocusWords words("vision-blindfold");
    page.addFocusWords(words, "vision-blindfold");
    REQUIRE(page.onBoard());
    CHECK(app.board_title == "Blindfold · Mock");
    CHECK_FALSE(app.flip);
    const auto* v = page.session();
    REQUIRE(v);
    CHECK(v->kind == VisionRun::Kind::Blindfold);
    CHECK(v->you == chess::Color::WHITE);
    CHECK(v->engine == "Mock");
    REQUIRE(waitUntil(
        [&] {
            page.poll();
            const auto* s = page.session();
            return s->state.running && s->state.thinking == chess::Color::WHITE;
        },
        60s));
    frames.draw(app, boardPage(page));
    CHECK(app.board_moves);  // the board takes the user's move
    for (const auto p : app.pieces) CHECK(p == chess::Piece::NONE);
    CHECK(app.move_fields > 0);

    // The user's move, then the engine's: the Board follows the game.
    page.sessionMove(chess::uci::uciToMove(chess::Board(), "e2e4"));
    REQUIRE(waitUntil(
        [&] {
            page.poll();
            return app.game.moves.size() >= 2 && page.session()->state.thinking == chess::Color::WHITE;
        },
        60s));
    CHECK(app.followed > 0);
    CHECK(app.game.moves[0].san == "e4");

    // F2: where is a piece? A click on its square: right, counted in the profile.
    frames.draw(app, boardPage(page), ImGuiKey_F2);
    v = page.session();
    REQUIRE(v->where);
    CHECK_FALSE(app.board_moves);  // the board answers the question, not a move
    app.click = v->where->square;
    frames.draw(app, boardPage(page));
    v = page.session();
    REQUIRE(v->where_said);
    CHECK(*v->where_said == v->where->square);
    auto kept = readVisionRecord(profile);
    CHECK(kept.where == 1);
    CHECK(kept.where_right == 1);
    // Enter: back to the game; F2 again, a wrong square this time.
    frames.draw(app, boardPage(page), ImGuiKey_Enter);
    CHECK_FALSE(page.session()->where);
    frames.draw(app, boardPage(page), ImGuiKey_F2);
    v = page.session();
    REQUIRE(v->where);
    app.click = v->where->square == chess::Square::SQ_A4 ? chess::Square::SQ_H5 : chess::Square::SQ_A4;
    frames.draw(app, boardPage(page));
    kept = readVisionRecord(profile);
    CHECK(kept.where == 2);
    CHECK(kept.where_right == 1);
    frames.draw(app, boardPage(page), ImGuiKey_Enter);

    // A game of the user's played live on Lichess: the blindfold game stops, counted, its pieces shown.
    app.live_game = true;
    page.poll();
    v = page.session();
    CHECK(v->phase == VisionRun::Phase::End);
    CHECK(readVisionRecord(profile).blind == 1);
    app.live_game = false;
    frames.draw(app, boardPage(page));
    CHECK(app.pieces[chess::Square(chess::Square::SQ_E1).index()] ==
          chess::Piece(chess::PieceType::KING, chess::Color::WHITE));
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    CHECK_FALSE(page.onBoard());
    CHECK(app.training_part == TrainingPart::Vision);
}
