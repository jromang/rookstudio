// The repertoires' training module (#107), built and driven without App: a context of the test's own answers what it
// asks of the program (a Board that only keeps the position it is given, the repertoires of a scratch profile, a
// Training page and a day's review that only remember what they were told, the user's correspondence games as the test
// sets them), and the module's pages are drawn in an ImGui context of the test's, a key pressed as the user presses it.
// A Black repertoire made in the test: a session of its position due and its new ones, a right answer and a wrong one,
// their grades and next dates kept beside the repertoire, a new position introduced, asked again later in the session,
// Escape and the way back to the Openings page; a move added to the repertoire from a wrong answer, Ctrl+Z taking it
// back, Retry; the day's review of its positions due and the way back to the Training page; the failure paths: a
// session locked by a live game on Lichess, the positions of a correspondence game left out, an answer that could not
// be kept said over the Board.

#include <doctest/doctest.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>  // the windows' names, ActivateItemByID
#include <chess.hpp>
#include <json.hpp>
#include <study/game_tree.hpp>

#include "app_context.hpp"
#include "board_view.hpp"
#include "databases.hpp"
#include "focus_words.hpp"
#include "jobs.hpp"
#include "repertoire_training_page.hpp"
#include "repertoires.hpp"
#include "test_support.hpp"
#include "training.hpp"

using namespace fcg;
using namespace fcg::test;
namespace fs = std::filesystem;
using Phase  = TrainingRun::Phase;
using study::fsrs::Rating;

namespace {

// What the module asks of App, answered by the test: the profile of a scratch folder and its repertoires, a Board that
// keeps the position the module gives it, a Training page and a day's review that only remember what they were told,
// the user's correspondence games on Lichess as the test sets them (the positions they are at, as FEN writes them).
class FakeApp final : public RepertoireTrainingContext {
   public:
    explicit FakeApp(const fs::path& profile) : profile_(profile), shelf_(jobs_, profile), repertoires_(profile) {}

    // The program.
    int frame() const override { return frame_; }
    double seconds() const override { return 100; }
    bool shortcutPressed(int) const override { return false; }
    bool scripted() const override { return false; }
    fs::path profileDir() const override { return profile_; }
    DatabaseShelf& shelf() override { return shelf_; }
    void toast(const std::string&) override {}
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
        tints       = marks.tints.size();
        arrows      = marks.arrows.size();
        board_moves = moves;
        ImGui::Dummy(ImVec2(size, size));
    }
    void drawMoveField() override { ++move_fields; }
    void moveKeys() override {}
    void drawBoardTabs() override { ++tabs; }
    void drawFairPlayBanner() override { ++fair_play_banners; }
    void analyse(bool) override {}
    bool analysisKey() override { return false; }

    // The Board as the training asks on it.
    void askAgainAt(int at) override {
        if (game.moves.size() > static_cast<std::size_t>(at)) game.moves.resize(static_cast<std::size_t>(at));
        ply = at;
        ++asked_again;
    }
    void showTreeAt(int at) override {
        if (ply > at) ply = at;
        tree_tab = true;
    }
    void sayOnBoard(const std::string& text) override { message = text; }

    // The Training page and its day's review.
    const TrainingSettings& trainingSettings() override { return settings_; }
    void setTrainingSettings(const TrainingSettings& s) override {
        settings_ = s;
        ++settings_saved;
    }
    void showTrainingPage(int part) override { training_part = part; }
    bool trainingShown() const override { return false; }
    void setEnterStartsPuzzles(bool) override {}
    void reviewOnly(ReviewKind) override {}
    bool reviewRunning() const override { return review; }
    bool nextReview() override { return false; }
    void stopReview() override { review = false; }
    void drawReviewNext(bool) override { ++review_next; }

    // Fair play, the user's correspondence games.
    bool liveGameOn() const override { return live_game; }
    bool playingOnLichess() const override { return !correspondence.empty(); }
    int correspondencePly(const ReplayGame& line) const override {
        auto board = boardAt(line, false, 0);
        for (int at = 0;; ++at) {
            std::istringstream fields(board.getFen());
            std::string placement, side;
            fields >> placement >> side;
            if (correspondence.count(placement + " " + side)) return at;
            if (at >= static_cast<int>(line.moves.size())) return -1;
            board.makeMove(line.moves[static_cast<std::size_t>(at)].move);
        }
    }

    // The Openings page and its repertoires.
    RepertoireShelf& repertoires() override { return repertoires_; }
    bool measuredOn(const fs::path&) const override { return true; }
    void showOpeningsPage() override { ++openings_page; }

    // What the module was told, and what the test sets.
    int frame_ = 0;
    BoardTaken before{true, false, 0};  // the Board before a session: a game put aside, White below, the notation
    int taken = 0, board_page = 0, picks_dropped = 0, typed_dropped = 0, move_fields = 0, tabs = 0, asked_again = 0;
    int fair_play_banners = 0, settings_saved = 0, openings_page = 0, review_next = 0, training_part = -1;
    std::string board_title, message;
    bool board_tree = false, flip = false, board_moves = false, tree_tab = false;
    std::size_t tints = 0, arrows = 0;
    ReplayGame game;
    int ply = 0;
    std::vector<std::pair<BoardTaken, bool>> given_back;
    std::vector<BoardTaken> left;
    bool live_game = false, review = false;
    std::set<std::string> correspondence;  // "placement side" of each position

   private:
    fs::path profile_;
    JobGroup jobs_;
    DatabaseShelf shelf_;  // after its jobs: gone before them
    RepertoireShelf repertoires_;
    TrainingSettings settings_;
};

// An ImGui context of the test's own, without a window: a frame draws a page of the module, a key pressed before it,
// with its modifier (and let go after it, in a frame of its own: Dear ImGui takes one change of a key a frame).
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

    void draw(FakeApp& app, const std::function<void()>& page, ImGuiKey key = ImGuiKey_None,
              ImGuiKey modifier = ImGuiKey_None) {
        auto& io = ImGui::GetIO();
        if (modifier != ImGuiKey_None) io.AddKeyEvent(modifier, true);
        if (key != ImGuiKey_None) io.AddKeyEvent(key, true);
        frame(app, page);
        if (key != ImGuiKey_None) io.AddKeyEvent(key, false);
        if (modifier != ImGuiKey_None) io.AddKeyEvent(modifier, false);
        if (key != ImGuiKey_None || modifier != ImGuiKey_None) frame(app, page);
    }
    // The button `label` of the card `card` (a child window's id, "##training_wrong") clicked in the next frame, as
    // Dear ImGui's keyboard navigation activates an item: false when no such card was drawn.
    static bool click(const char* card, const std::string& label) {
        for (auto* window : ImGui::GetCurrentContext()->Windows) {
            if (std::string_view(window->Name).find(card) == std::string_view::npos) continue;
            ImGui::ActivateItemByID(window->GetID(label.c_str()));
            return true;
        }
        return false;
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
std::function<void()> boardPage(RepertoireTrainingPage& page) {
    return [&page] {
        if (page.onBoard()) page.drawSessionPage();
    };
}

// The palette's entries, by label, with where they lead.
std::map<std::string, std::pair<std::string, std::function<void()>>> commands(RepertoireTrainingPage& page) {
    std::map<std::string, std::pair<std::string, std::function<void()>>> out;
    page.addOpeningsCommands([&](std::string label, std::string where, std::function<void()> run) {
        out[label] = {std::move(where), std::move(run)};
    });
    return out;
}

std::vector<chess::Move> moves(const std::vector<std::string>& line) {
    chess::Board board;
    std::vector<chess::Move> out;
    for (const auto& san : line) {
        out.push_back(chess::uci::parseSan(board, san));
        board.makeMove<false>(out.back());
    }
    return out;
}

chess::Move next(const std::vector<std::string>& line, const std::string& san) {
    chess::Board board;
    for (const auto& s : line) board.makeMove<false>(chess::uci::parseSan(board, s));
    return chess::uci::parseSan(board, san);
}

// A Black repertoire made as the Board's star makes one: 1. e4 c5 2. Nf3 d6, and 1. d4 Nf6; its positions where Black
// moves are three cards. The card after 1. e4 was reviewed before, and is due since yesterday: the others are new.
std::size_t najdorf(FakeApp& app, int64_t now) {
    auto& shelf      = app.repertoires();
    const auto index = shelf.create("Najdorf", chess::Color::BLACK);
    REQUIRE(shelf.toggle(index, moves({"e4"}), next({"e4"}, "c5")));
    REQUIRE(shelf.toggle(index, moves({"e4", "c5", "Nf3"}), next({"e4", "c5", "Nf3"}, "d6")));
    REQUIRE(shelf.toggle(index, moves({"d4"}), next({"d4"}, "Nf6")));
    const auto& entry = shelf.entries()[index];
    const auto cards  = study::trainingCards(entry.lines, entry.side);
    REQUIRE(cards.size() == 3);
    for (const auto& card : cards) {
        if (card.line != std::vector<std::string>{"e4"}) continue;
        study::CardMemory m;
        m.moves           = study::movesKey(card.moves);
        m.since           = now - 10 * study::fsrs::DAY;
        m.card.state      = study::fsrs::State::Review;
        m.card.stability  = 4;
        m.card.difficulty = 5;
        m.card.reps       = 2;
        m.card.last       = now - 5 * study::fsrs::DAY;
        m.card.due        = now - study::fsrs::DAY;
        TrainingFile kept;
        kept.memories[card.key] = m;
        kept.answer_ms          = 8000;
        REQUIRE(writeTrainingFile(entry.file, kept));
    }
    return index;
}

// The card the session asks, none when it asks nothing.
const study::TrainingCard* asked(const RepertoireTrainingPage& page) {
    const auto* run = page.session();
    if (!run || !run->session || !run->session->current()) return nullptr;
    return &run->session->cards()[*run->session->current()];
}

// A move of the position the Board shows, in SAN, as the user plays it on the board (App's hook: boardPlayAsked()).
void playOnBoard(FakeApp& app, RepertoireTrainingPage& page, const std::string& san) {
    page.sessionMove(chess::uci::parseSan(boardAt(app.game, false, app.ply), san));
}

// The session's cards ready (they are made in the background), its first question asked.
bool questionAsked(RepertoireTrainingPage& page) {
    return waitUntil([&] {
        page.poll();
        return page.session() && page.session()->phase != Phase::Preparing;
    });
}

std::vector<nlohmann::json> reviewsOf(const fs::path& repertoire) {
    std::ifstream in(reviewsFileOf(repertoire));
    std::vector<nlohmann::json> out;
    for (std::string line; std::getline(in, line);) out.push_back(nlohmann::json::parse(line));
    return out;
}

}  // namespace

TEST_CASE("repertoire training page: a right and a wrong answer graded and kept, a new position introduced") {
    ScratchDir profile("repertoire-training");
    FakeApp app(profile);
    Frames frames;
    RepertoireTrainingPage page(app);
    const auto now    = epochNow();
    const auto index  = najdorf(app, now);
    const auto& entry = app.repertoires().entries()[index];

    // What the Openings page, the Training page and Ctrl+K show of it: one position due, two new ones.
    CHECK(page.cardCount(index) == 3);
    CHECK(page.listLine(index) == "1 to review");
    const auto day = page.reviewCount();
    CHECK(day.kind == ReviewKind::Repertoire);
    CHECK(day.due == 1);
    CHECK(day.answer_ms == 8000);
    CHECK(page.dueRepertoires() == std::vector<std::size_t>{index});
    CHECK(page.reviewNextLabel(index) == "Next: 1 position of “Najdorf”");
    CHECK(page.reviewNextLabel(index + 1).empty());
    const auto positions = page.repertoirePositions();
    CHECK(positions[0].empty());
    CHECK(positions[1].size() == 3);
    auto entries = commands(page);
    REQUIRE(entries.count("Train the repertoire “Najdorf”"));
    CHECK(entries.at("Train the repertoire “Najdorf”").first == "1 position to review");
    CHECK(entries.count("Train only the new positions of “Najdorf”"));
    frames.draw(app, [&] { page.drawCard(index); });  // the card, Start · 3 positions

    // Started as its focus word starts it: the Board taken for it, seen from Black, the column at the Tree's tab.
    FocusWords words("training");
    page.addFocusWords(words, "training");
    REQUIRE(words.applied());
    REQUIRE(page.onBoard());
    CHECK(app.taken == 1);
    CHECK(app.board_title == "Training · Najdorf");
    CHECK(app.board_tree);
    CHECK(app.flip);
    CHECK(page.keptAside());
    CHECK(page.hidesAids());  // its cards being prepared: the tree, the engines and the notes hidden meanwhile
    REQUIRE(questionAsked(page));
    const auto* run = page.session();
    REQUIRE(run->phase == Phase::Asking);
    REQUIRE(run->session->total() == 3);
    CHECK(page.hidesAids());

    // The position due comes first: after 1. e4, the opponent's move played on the Board; c5 is right at the first try.
    const auto* card = asked(page);
    REQUIRE(card);
    CHECK(card->line == std::vector<std::string>{"e4"});
    CHECK(app.ply == 1);
    frames.draw(app, boardPage(page));
    CHECK(app.board_moves);  // the question's position takes the user's moves
    const auto due_key = card->key;
    playOnBoard(app, page, "c5");
    REQUIRE(run->phase == Phase::Right);
    CHECK_FALSE(page.hidesAids());
    CHECK(run->rating == Rating::Good);
    CHECK(app.ply == 2);
    // Its grade and its next date kept beside the repertoire, its PGN as it was, the answer at the end of the reviews.
    auto kept = readTrainingFile(entry.file);
    REQUIRE(kept.memories.count(due_key));
    const auto& reviewed = kept.memories.at(due_key);
    CHECK(reviewed.card.state == study::fsrs::State::Review);
    CHECK(reviewed.card.reps == 3);
    CHECK(reviewed.card.due == run->graded.after.card.due);
    CHECK(reviewed.card.due >= now + study::fsrs::DAY);  // days later
    CHECK(reviewed.since == now - 10 * study::fsrs::DAY);
    auto reviews = reviewsOf(entry.file);
    REQUIRE(reviews.size() == 1);
    CHECK(reviews[0]["rating"] == 3);
    CHECK(reviews[0]["played"] == "c5");
    frames.draw(app, boardPage(page));
    CHECK(app.tabs > 0);    // the tree, the engines and the notes back with the answer
    CHECK(app.tints == 2);  // the move played in green

    // Enter: the next card, a new one, after 1. e4 c5 2. Nf3; e6 is not the repertoire's move.
    frames.draw(app, boardPage(page), ImGuiKey_Enter);
    REQUIRE(run->phase == Phase::Asking);
    card = asked(page);
    REQUIRE(card);
    CHECK(card->line == std::vector<std::string>{"e4", "c5", "Nf3"});
    CHECK(run->position == 2);
    const auto new_key = card->key;
    CHECK_FALSE(kept.memories.count(new_key));  // never reviewed
    playOnBoard(app, page, "e6");
    REQUIRE(run->phase == Phase::Wrong);
    CHECK(run->rating == Rating::Again);
    CHECK(run->graded.again);  // asked again later in the session
    frames.draw(app, boardPage(page));
    CHECK(app.tints == 4);   // the move played in orange, the repertoire's in green
    CHECK(app.arrows == 1);  // and its arrow
    // The new position introduced: its first review today, back within the hour.
    kept = readTrainingFile(entry.file);
    REQUIRE(kept.memories.count(new_key));
    const auto& introduced = kept.memories.at(new_key);
    CHECK(introduced.since == run->graded_at);
    CHECK(introduced.card.state == study::fsrs::State::Learning);
    CHECK(introduced.card.due - run->graded_at == 600);
    CHECK(introduced.moves == "d6");
    reviews = reviewsOf(entry.file);
    REQUIRE(reviews.size() == 2);
    CHECK(reviews[1]["rating"] == 1);
    CHECK(reviews[1]["played"] == "e6");

    // Retry (Enter): the question again, the move played taken back; right this time, not graded again.
    frames.draw(app, boardPage(page), ImGuiKey_Enter);
    REQUIRE(run->phase == Phase::Asking);
    CHECK(run->retry);
    CHECK(app.asked_again == 1);
    CHECK(app.ply == 3);
    CHECK(app.game.moves.size() == 3);
    playOnBoard(app, page, "d6");
    REQUIRE(run->phase == Phase::Right);
    CHECK(reviewsOf(entry.file).size() == 2);

    // The last new one, then the one missed again; Escape stops, its answers kept; Escape again back to the Openings
    // page, the Board given back as it was.
    frames.draw(app, boardPage(page), ImGuiKey_Enter);
    card = asked(page);
    REQUIRE(card);
    CHECK(card->line == std::vector<std::string>{"d4"});
    playOnBoard(app, page, "Nf6");
    REQUIRE(run->phase == Phase::Right);
    frames.draw(app, boardPage(page), ImGuiKey_Enter);
    card = asked(page);
    REQUIRE(card);
    CHECK(card->key == new_key);
    CHECK(run->again);
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    REQUIRE(run->phase == Phase::End);
    CHECK(run->stopped);
    CHECK(run->session->outcomes().size() == 3);
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    CHECK_FALSE(page.onBoard());
    REQUIRE(app.given_back.size() == 1);
    CHECK(app.given_back[0].first.aside);
    CHECK_FALSE(app.given_back[0].first.flip);
    CHECK_FALSE(app.given_back[0].second);  // not the Training page
    CHECK(app.openings_page == 1);
    CHECK(readTrainingFile(entry.file).memories.size() == 3);
}

TEST_CASE("repertoire training page: a move added to the repertoire from a wrong answer, Ctrl+Z taking it back") {
    ScratchDir profile("repertoire-training-add");
    FakeApp app(profile);
    Frames frames;
    RepertoireTrainingPage page(app);
    const auto index  = najdorf(app, epochNow());
    const auto& entry = app.repertoires().entries()[index];
    const auto pgn    = readFile(entry.file);

    // Its new positions alone, from Ctrl+K: the one after 1. e4 c5 2. Nf3 first.
    auto entries = commands(page);
    REQUIRE(entries.count("Train only the new positions of “Najdorf”"));
    entries.at("Train only the new positions of “Najdorf”").second();
    REQUIRE(questionAsked(page));
    const auto* run = page.session();
    CHECK(run->kind == TrainingRun::Kind::New);
    CHECK(run->session->total() == 2);
    const auto* card = asked(page);
    REQUIRE(card);
    REQUIRE(card->line == std::vector<std::string>{"e4", "c5", "Nf3"});
    const auto key  = card->key;
    const auto path = moves({"e4", "c5", "Nf3"});
    playOnBoard(app, page, "e6");
    REQUIRE(run->phase == Phase::Wrong);
    frames.draw(app, boardPage(page));

    // "Add 2…e6 to the repertoire": a second answer accepted there, the card's memory following it.
    REQUIRE(Frames::click("##training_wrong", "Add 2…e6 to the repertoire"));
    frames.draw(app, boardPage(page));
    REQUIRE(run->added);
    CHECK(app.repertoires().has(index, path, next({"e4", "c5", "Nf3"}, "e6")));
    CHECK(app.repertoires().has(index, path, next({"e4", "c5", "Nf3"}, "d6")));
    CHECK(readFile(entry.file) != pgn);
    auto kept = readTrainingFile(entry.file);
    REQUIRE(kept.memories.count(key));
    CHECK(kept.memories.at(key).moves == "d6 e6");
    CHECK(page.cardCount(index) == 3);

    // Ctrl+Z takes it back: the repertoire's lines as they were, its PGN too, the card's memory with them.
    frames.draw(app, boardPage(page), ImGuiKey_Z, ImGuiMod_Ctrl);
    CHECK_FALSE(run->added);
    CHECK_FALSE(app.repertoires().has(index, path, next({"e4", "c5", "Nf3"}, "e6")));
    CHECK(app.repertoires().has(index, path, next({"e4", "c5", "Nf3"}, "d6")));
    CHECK(readFile(entry.file) == pgn);
    kept = readTrainingFile(entry.file);
    REQUIRE(kept.memories.count(key));
    CHECK(kept.memories.at(key).moves == "d6");
    REQUIRE(run->phase == Phase::Wrong);

    // "See the tree here": the question's position, the Tree's tab beside it; Retry with Enter, then the right move.
    REQUIRE(Frames::click("##training_wrong", "See the tree here"));
    frames.draw(app, boardPage(page));
    CHECK(app.tree_tab);
    CHECK(app.ply == 3);
    frames.draw(app, boardPage(page), ImGuiKey_Enter);
    REQUIRE(run->phase == Phase::Asking);
    CHECK(run->retry);
    playOnBoard(app, page, "d6");
    CHECK(run->phase == Phase::Right);
}

TEST_CASE("repertoire training page: the day's review of its positions due, back to the Training page") {
    ScratchDir profile("repertoire-training-review");
    FakeApp app(profile);
    Frames frames;
    RepertoireTrainingPage page(app);
    const auto index = najdorf(app, epochNow());

    // The day's review starts the repertoire's session of its position due alone.
    app.review = true;
    REQUIRE(page.startReview(index));
    REQUIRE(questionAsked(page));
    const auto* run = page.session();
    CHECK(run->kind == TrainingRun::Kind::Review);
    CHECK(run->session->total() == 1);
    playOnBoard(app, page, "c5");
    REQUIRE(run->phase == Phase::Right);
    frames.draw(app, boardPage(page), ImGuiKey_Enter);
    REQUIRE(run->phase == Phase::End);
    frames.draw(app, boardPage(page));
    CHECK(app.review_next > 0);  // the review's card: the next kind, or back to the Training page
    // Nothing more due today: no other session of it in the review.
    CHECK_FALSE(page.startReview(index));
    CHECK(page.dueRepertoires().empty());
    // Escape: back to the Training page, the review over.
    frames.draw(app, boardPage(page), ImGuiKey_Escape);
    CHECK_FALSE(page.onBoard());
    REQUIRE(app.given_back.size() == 1);
    CHECK(app.given_back[0].second);  // the Training page
    CHECK_FALSE(app.review);
    CHECK(app.openings_page == 0);
}

TEST_CASE("repertoire training page: a live game locks it, a correspondence position left out, a failure said") {
    ScratchDir profile("repertoire-training-locks");
    FakeApp app(profile);
    Frames frames;
    RepertoireTrainingPage page(app);
    const auto index  = najdorf(app, epochNow());
    const auto& entry = app.repertoires().entries()[index];

    // A game of the user's played live on Lichess: nothing starts, the card says why (fair play, #48).
    app.live_game = true;
    FocusWords words("training-new");
    page.addFocusWords(words, "training-new");
    CHECK(words.applied());
    CHECK_FALSE(page.onBoard());
    CHECK(app.taken == 0);
    CHECK_FALSE(page.startReview(index));
    frames.draw(app, [&] { page.drawCard(index); });
    app.live_game = false;

    // A correspondence game of the user's at the position after 1. d4: that card is left out while the game lasts, the
    // repertoire would tell its next move.
    chess::Board after_d4;
    after_d4.makeMove<false>(chess::uci::parseSan(after_d4, "d4"));
    std::istringstream fields(after_d4.getFen());
    std::string placement, side;
    fields >> placement >> side;
    app.correspondence.insert(placement + " " + side);
    // The training file made a folder: no answer can be kept there.
    fs::remove(trainingFileOf(entry.file));
    fs::create_directories(trainingFileOf(entry.file) / "in-the-way");
    FocusWords again("training");
    page.addFocusWords(again, "training");
    REQUIRE(page.onBoard());
    REQUIRE(questionAsked(page));
    const auto* run = page.session();
    CHECK(run->left_out == 1);
    CHECK(run->session->total() == 2);  // the position due and the new one left

    // The answer graded, but it could not be kept: said over the Board, with the file's name.
    const auto* card = asked(page);
    REQUIRE(card);
    playOnBoard(app, page, card->moves.front());
    REQUIRE(run->phase == Phase::Right);
    CHECK(app.message.find("The answer could not be kept in") != std::string::npos);
    CHECK(app.message.find("Najdorf.training.json") != std::string::npos);

    // A live game started meanwhile: the question waits, its moves not taken, the banner of fair play shown.
    frames.draw(app, boardPage(page), ImGuiKey_Enter);
    REQUIRE(run->phase == Phase::Asking);
    app.live_game = true;
    frames.draw(app, boardPage(page));
    CHECK(app.fair_play_banners > 0);
    CHECK_FALSE(app.board_moves);
    card = asked(page);
    REQUIRE(card);
    playOnBoard(app, page, card->moves.front());
    CHECK(run->phase == Phase::Asking);  // not taken

    // Another game opened on the Board: the session left where it is, the game it put aside staying a recent one.
    page.sessionDropped();
    CHECK_FALSE(page.onBoard());
    CHECK(app.left.size() == 1);
    CHECK(app.given_back.empty());
}
