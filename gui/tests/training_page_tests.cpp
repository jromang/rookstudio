// The Training page module (#107), built and driven without App: a context of the test's own answers what it asks of
// the program (the profile of a scratch folder, a game of the user's live on Lichess and a game on the Board as the
// test sets them, a view and pages offered that only remember what they were told), and kinds of training of the test's
// plug into it as the session modules do (training_kind.hpp): parts, cards, counts, steps and sessions that only
// remember what they were asked. The page is drawn in an ImGui context of the test's, a key pressed or a button clicked
// as the user presses or clicks it. Today listing the kinds with cards due, in the order "Review all" takes them, their
// cards in their parts' order; the day's review from one kind to the next, a repertoire's steps one by one, a step that
// cannot start passed over, to its end and the way back to the page; the training's settings changed and kept, a run
// for screenshots keeping nothing; the page offered once to a view without it; its parts, palette commands and --focus
// words, the kinds' with them; the failure path: a game of the user's live on Lichess, or one played on the Board,
// keeps the review from starting.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>  // the windows' names, ActivateItemByID

#include "app_context.hpp"
#include "databases.hpp"
#include "focus_words.hpp"
#include "guide.hpp"
#include "jobs.hpp"
#include "test_support.hpp"
#include "training.hpp"
#include "training_page.hpp"
#include "ui.hpp"
#include "views.hpp"

using namespace fcg;
using namespace fcg::test;
namespace fs = std::filesystem;
using views::PageId;

namespace {

// What the page asks of App, answered by the test: the profile of a scratch folder; a live game on Lichess and a game
// on the Board as the test sets them; a view, Study, whose pages the test sets, and the pages offered once, kept as
// they are written; the Training page shown, remembered.
class FakeApp final : public TrainingPageContext {
   public:
    explicit FakeApp(const fs::path& profile) : profile_(profile), shelf_(jobs_, profile) {}

    // The program.
    int frame() const override { return frame_; }
    double seconds() const override { return 100; }
    bool shortcutPressed(int) const override { return false; }
    bool scripted() const override { return scripted_run; }
    fs::path profileDir() const override { return profile_; }
    DatabaseShelf& shelf() override { return shelf_; }
    void toast(const std::string& text) override { toasts.push_back(text); }
    JobGroup& jobs() override { return jobs_; }

    // Fair play.
    bool liveGameOn() const override { return live_game; }

    // The views and the pages offered once.
    bool hasViews() const override { return true; }
    std::string viewName() const override { return "Study"; }
    bool viewHolds(PageId page) const override { return std::find(pages.begin(), pages.end(), page) != pages.end(); }
    bool viewsHold(PageId page) const override { return viewHolds(page); }
    void addToView(PageId page, const std::vector<PageId>& after, const std::vector<PageId>& before) override {
        added.push_back({page, after, before});
        pages.push_back(page);
    }
    std::vector<std::string> pagesOffered() const override { return offered; }
    void setPagesOffered(const std::vector<std::string>& written) override {
        offered = written;
        ++offered_written;
    }

    // The Training page among the program's pages, a game played on the Board.
    void toTrainingPage() override {
        shown = true;
        ++training_page;
    }
    bool trainingPageShown() const override { return shown; }
    bool boardGamePlayed() const override { return board_game; }

    // What the page was told, and what the test sets.
    struct Added {
        PageId page;
        std::vector<PageId> after, before;
    };
    int frame_ = 0, training_page = 0, offered_written = 0;
    bool scripted_run = false, live_game = false, board_game = false, shown = false;
    std::vector<std::string> toasts, offered;
    std::vector<PageId> pages{PageId::Home,     PageId::Board,   PageId::Databases,
                              PageId::Openings, PageId::Lichess, PageId::Settings};
    std::vector<Added> added;

   private:
    fs::path profile_;
    JobGroup jobs_;
    DatabaseShelf shelf_;  // after its jobs: gone before them
};

// A kind of training of the test's: its part (none when its place is 0), its card, its counts, steps and labels as the
// test sets them. A step's session holds the Board until it is closed, dropped, or another starts: a session takes the
// Board as every session does, the one that held it closed for it (App's context::SessionBoard::takeBoard() asks the
// page so).
class FakeKind final : public TrainingKind {
   public:
    FakeKind(std::string name, std::string word, int place, std::optional<ReviewKind> review)
        : name_(std::move(name)), word_(std::move(word)), place_(place), review_(review) {}

    // BoardSession.
    void sessionMove(chess::Move) override {}
    void drawSessionPage() override { ++session_pages; }
    void sessionDropped() override { session.reset(); }
    void closeSession(bool back) override {
        closed.push_back(back);
        session.reset();
    }
    bool hidesAids() const override { return false; }
    bool keptAside() const override { return false; }
    const char* guideTopic() const override { return "a-session-of-the-tests"; }

    // TrainingKind.
    bool onBoard() const override { return session.has_value(); }
    void drawHomeLine() override {
        ++home_lines;
        if (session) ImGui::TextUnformatted(name_.c_str());
    }
    void addFocusWords(FocusWords& words, const std::string&) override {
        const auto pattern = "session-of-" + word_;
        words.on({std::string_view(pattern)}, [&] { ++words_applied; });
    }
    Part part() const override {
        if (place_ == 0) return {};
        return {static_cast<TrainingPart>(place_), name_.c_str(), word_.c_str(), topic_.c_str()};
    }
    void drawPart() override { ++parts_drawn; }
    void drawTodayCards(TodayCards& cards) override {
        if (order) order->push_back(name_);
        cards.card(card_id_.c_str(), ui::Icon::Puzzle, name_.c_str(), "A kind of the test's.", "a figure",
                   [this] { ++card_then; });
    }
    void drawTodayChoice(bool& enter) override {
        if (enter) ++choice_enter;
    }
    void addCommands(const PaletteAdd& add, const std::string& training_view) override {
        add(name_, training_view, [this] { ++command_run; });
    }
    std::optional<ReviewKind> reviewKind() const override { return review_; }
    ReviewCount reviewCount() override { return count; }
    std::vector<std::size_t> reviewSteps() override { return steps ? *steps : TrainingKind::reviewSteps(); }
    bool startReviewStep(std::size_t step) override {
        started.push_back(step);
        if (refused.contains(step)) return false;
        if (page) page->closeSessionForAnother();
        session = step;
        return true;
    }
    std::string reviewNextLabel(std::size_t step) override {
        const auto it = labels.find(step);
        return it == labels.end() ? std::string() : it->second;
    }
    std::string dueWords(std::size_t) const override { return "cards of " + name_; }
    const char* reviewOnlyLabel() const override { return only.empty() ? nullptr : only.c_str(); }

    // What the kind was asked, and what the test sets.
    TrainingPage* page              = nullptr;
    std::vector<std::string>* order = nullptr;  // the kinds' cards, in the order drawn
    ReviewCount count;
    std::optional<std::vector<std::size_t>> steps;  // none: one step when cards are due, as the interface's default
    std::set<std::size_t> refused;                  // the steps whose session cannot start
    std::map<std::size_t, std::string> labels;      // "Next: …" by step
    std::string only;                               // "Only the …"
    std::optional<std::size_t> session;             // the step whose session holds the Board
    std::vector<std::size_t> started;
    std::vector<bool> closed;
    int session_pages = 0, home_lines = 0, words_applied = 0, parts_drawn = 0, card_then = 0, choice_enter = 0;
    int command_run = 0;

   private:
    std::string name_, word_;
    int place_;
    std::optional<ReviewKind> review_;
    std::string topic_   = "the-part-of-" + word_;
    std::string card_id_ = "##card_of_" + word_;
};

// An ImGui context of the test's own, without a window: a frame draws what the test gives it, a key pressed before it
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
        auto& io = ImGui::GetIO();
        if (key != ImGuiKey_None) io.AddKeyEvent(key, true);
        frame(app, page);
        if (key != ImGuiKey_None) {
            io.AddKeyEvent(key, false);
            frame(app, page);
        }
    }
    // The item `label` of the window `window` (a child window's id, "##review_card") clicked in the next frame, as Dear
    // ImGui's keyboard navigation activates an item: false when no such window was drawn.
    static bool click(const char* window, const std::string& label) {
        for (auto* w : ImGui::GetCurrentContext()->Windows) {
            if (std::string_view(w->Name).find(window) == std::string_view::npos) continue;
            ImGui::ActivateItemByID(w->GetID(label.c_str()));
            return true;
        }
        return false;
    }
    // Whether the window `window` was drawn in the last frame.
    static bool drawn(const char* window) {
        const auto* context = ImGui::GetCurrentContext();
        for (const auto* w : context->Windows)
            if (std::string_view(w->Name).find(window) != std::string_view::npos &&
                w->LastFrameActive == context->FrameCount)
                return true;
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

// A session's end in the day's review, as the session modules draw it: its card, which goes on or back to the page.
std::function<void()> nextCard(TrainingPage& page) {
    return [&page] {
        ImGui::BeginChild("##next_card", ImVec2(0, 200));
        page.drawReviewNext(false);
        ImGui::EndChild();
    };
}

// The palette's entries, in their order, with where they lead.
std::vector<std::pair<std::string, std::string>> commands(
    TrainingPage& page, std::map<std::string, std::function<void()>>* runs = nullptr) {
    std::vector<std::pair<std::string, std::string>> out;
    page.addCommands(
        [&](std::string label, std::string where, std::function<void()> run) {
            if (runs) (*runs)[label] = run;
            out.emplace_back(std::move(label), std::move(where));
        },
        "Study");
    return out;
}

bool focus(TrainingPage& page, const std::string& word) {
    FocusWords words(word);
    page.addFocusWords(words, word);
    return words.applied();
}

// The kinds of a training of the test's: "Exercises" as the user's mistakes (first in the queue, 2 due, its button
// "Only the exercises"), "Lines" as the repertoires' training (no part of its own, two repertoires due, the first and
// the fourth, and a fifth whose positions came due), "Drills" as the puzzles (none due today, 4 tomorrow and 2 within
// three days), "Sight" as board vision (no cards). Given to the page in the order the Board asks them.
struct Kinds {
    FakeKind exercises{"Exercises", "exercises", 2, ReviewKind::Mistakes};
    FakeKind lines{"Lines", "lines", 0, ReviewKind::Repertoire};
    FakeKind drills{"Drills", "drills", 1, ReviewKind::Puzzles};
    FakeKind sight{"Sight", "sight", 5, std::nullopt};
    std::vector<std::string> order;

    Kinds() {
        exercises.count.due       = 2;
        exercises.count.answer_ms = 30000;
        exercises.only            = "Only the exercises";
        lines.count.due           = 5;
        lines.count.tomorrow      = 1;
        lines.steps               = std::vector<std::size_t>{0, 3, 4};
        lines.labels[0]           = "Next: 3 positions of the first";
        drills.count.tomorrow     = 4;
        drills.count.three_days   = 2;
        for (auto* k : all()) k->order = &order;
    }
    std::vector<FakeKind*> all() { return {&lines, &drills, &exercises, &sight}; }
    std::vector<TrainingKind*> given() { return {&lines, &drills, &exercises, &sight}; }
    void plug(TrainingPage& page) {
        for (auto* k : all()) k->page = &page;
    }
};

}  // namespace

TEST_CASE("training page: Today lists the kinds with cards due; the day's review takes them one after the other") {
    ScratchDir profile("training-page");
    FakeApp app(profile);
    Frames frames;
    Kinds kinds;
    TrainingPage page(app, kinds.given());
    kinds.plug(page);

    // The day: the kinds with cards, in the order "Review all" takes them, whatever the order the page was given them
    // in; those with cards due today; the time they should take (30 s an exercise, 10 s a position, rounded up).
    const auto day = page.reviewDay();
    REQUIRE(day.kinds.size() == 3);
    CHECK(day.kinds[0].kind == ReviewKind::Mistakes);
    CHECK(day.kinds[1].kind == ReviewKind::Repertoire);
    CHECK(day.kinds[2].kind == ReviewKind::Puzzles);
    CHECK(day.due() == 7);
    CHECK(day.tomorrow() == 5);
    CHECK(day.threeDays() == 2);
    CHECK(day.dueKinds() == std::vector<ReviewKind>{ReviewKind::Mistakes, ReviewKind::Repertoire});
    CHECK(day.minutes() == 2);
    CHECK(page.due());  // the Home page's "In progress" has the training
    CHECK(page.boardSession() == nullptr);

    // Today: the day's review card, the kinds' cards in their parts' order (the repertoires' training has none), what
    // the kinds offer to start under them; the guide's section of the page.
    frames.draw(app, [&] { page.draw(); });
    CHECK(Frames::drawn("##review_card"));
    CHECK(kinds.order == std::vector<std::string>{"Drills", "Exercises", "Sight"});
    CHECK(Frames::drawn("##card_of_drills"));
    CHECK_FALSE(Frames::drawn("##card_of_lines"));
    CHECK(std::string_view(page.guideTopic()) == guide::topics::TrainingPage);
    for (auto* k : kinds.all()) CHECK(k->parts_drawn == 0);

    // "Review all · 7": the exercises first, their session holding the Board.
    REQUIRE(Frames::click("##review_card", "Review all · 7"));
    frames.draw(app, [&] { page.draw(); });
    CHECK(kinds.exercises.started == std::vector<std::size_t>{0});
    CHECK(kinds.lines.started.empty());
    CHECK(page.reviewRunning());
    CHECK(page.boardSession() == &kinds.exercises);

    // At their session's end, its card goes on: the first repertoire next, its label the repertoires' training's; the
    // exercises' session closed for it, the review going on.
    frames.draw(app, nextCard(page));
    REQUIRE(Frames::click("##next_card", "Next: 3 positions of the first · Enter"));
    frames.draw(app, nextCard(page));
    CHECK(kinds.lines.started == std::vector<std::size_t>{0});
    CHECK(kinds.exercises.closed == std::vector<bool>{false});
    CHECK(page.reviewRunning());
    CHECK(page.boardSession() == &kinds.lines);

    // Then the fourth repertoire, whose step has no label of its own ("Next"); its session cannot start now: passed
    // over for the fifth's, the first's session closed for it.
    kinds.lines.refused = {3};
    frames.draw(app, nextCard(page));
    REQUIRE(Frames::click("##next_card", "Next · Enter"));
    frames.draw(app, nextCard(page));
    CHECK(kinds.lines.started == std::vector<std::size_t>{0, 3, 4});
    CHECK(kinds.lines.session == std::size_t{4});
    CHECK(kinds.lines.closed == std::vector<bool>{false});
    CHECK(page.reviewRunning());

    // The last one's end: the day's review is done, Enter back to the Training page, the session closed as the user
    // left it.
    app.shown = false;
    frames.draw(app, nextCard(page));
    REQUIRE(Frames::click("##next_card", "Back to the Training page · Enter"));
    frames.draw(app, nextCard(page));
    CHECK(kinds.lines.closed == std::vector<bool>{false, true});
    CHECK_FALSE(page.reviewRunning());
    CHECK(page.boardSession() == nullptr);
    CHECK(app.shown);

    // The Home page: each kind's line asked (the one whose session holds the Board shows it), then the day's count.
    frames.draw(app, [&] { page.drawHome(); });
    for (auto* k : kinds.all()) CHECK(k->home_lines == 1);
}

TEST_CASE("training page: the settings changed and kept; the page offered once; its parts, commands and words") {
    ScratchDir profile("training-page-settings");
    FakeApp app(profile);
    Frames frames;
    Kinds kinds;
    TrainingPage page(app, kinds.given());
    kinds.plug(page);

    // The training's settings: the defaults without training.json; changed, kept in it at once, read again by another
    // page; a run for screenshots changes them for itself only.
    CHECK(page.trainingSettings().advance_ms == 1000);
    CHECK(page.trainingSettings().new_per_day == 20);
    auto settings         = page.trainingSettings();
    settings.advance_ms   = 0;
    settings.new_per_day  = 5;
    settings.guess_theory = false;
    page.setTrainingSettings(settings);
    CHECK(page.trainingSettings().new_per_day == 5);
    CHECK(fs::exists(profile / "training.json"));
    {
        const auto kept = readTrainingSettings(profile);
        CHECK(kept.advance_ms == 0);
        CHECK(kept.new_per_day == 5);
        CHECK_FALSE(kept.guess_theory);
        TrainingPage again(app, kinds.given());
        CHECK(again.trainingSettings().new_per_day == 5);
    }
    app.scripted_run     = true;
    settings.new_per_day = 9;
    page.setTrainingSettings(settings);
    CHECK(page.trainingSettings().new_per_day == 9);
    CHECK(readTrainingSettings(profile).new_per_day == 5);
    app.scripted_run = false;

    // The page offered once on the Home page, to the view that lacks it: "Add it to Study" puts it after the Openings
    // (else before Lichess, else before Settings), and it is offered no more.
    frames.draw(app, [&] { page.drawOffer(); });
    CHECK(Frames::drawn("##training_offer"));
    CHECK(page.offerPending());
    REQUIRE(Frames::click("##training_offer", "Add it to Study"));
    frames.draw(app, [&] { page.drawOffer(); });
    REQUIRE(app.added.size() == 1);
    CHECK(app.added[0].page == PageId::Training);
    CHECK(app.added[0].after == std::vector<PageId>{PageId::Openings});
    CHECK(app.added[0].before == std::vector<PageId>{PageId::Lichess, PageId::Settings});
    CHECK(app.offered == std::vector<std::string>{"training"});
    CHECK_FALSE(page.offerPending());
    frames.draw(app, [&] { page.drawOffer(); });
    CHECK_FALSE(Frames::drawn("##training_offer"));
    // Another view made before it, the page offered before: not again; a run for screenshots writes nothing.
    {
        FakeApp other(profile);
        other.offered = {"play", "training"};
        TrainingPage offered(other, kinds.given());
        frames.draw(other, [&] { offered.drawOffer(); });
        CHECK_FALSE(Frames::drawn("##training_offer"));
        FakeApp fresh(profile);
        fresh.scripted_run = true;
        TrainingPage scripted(fresh, kinds.given());
        frames.draw(fresh, [&] { scripted.drawOffer(); });
        REQUIRE(Frames::click("##training_offer", "Not now##training_offer"));
        frames.draw(fresh, [&] { scripted.drawOffer(); });
        CHECK(fresh.added.empty());
        CHECK(fresh.offered_written == 0);
        CHECK_FALSE(scripted.offerPending());
    }

    // A kind's card leads to its part (the endgames' "Mates" to their mates: `then`), which the page then draws, its
    // section of the guide the part's.
    frames.draw(app, [&] { page.draw(); });
    REQUIRE(Frames::click("##card_of_drills", "##card_of_drills_go"));
    frames.draw(app, [&] { page.draw(); });
    CHECK(kinds.drills.card_then == 1);
    frames.draw(app, [&] { page.draw(); });
    CHECK(kinds.drills.parts_drawn >= 1);
    CHECK(std::string_view(page.guideTopic()) == "the-part-of-drills");
    // context::Training's parts by their places (0 Today); the page shown.
    page.showTrainingPage(TrainingPart::Vision);
    frames.draw(app, [&] { page.draw(); });
    CHECK(kinds.sight.parts_drawn == 1);
    CHECK(page.trainingShown());
    page.showTrainingPage(TrainingPart::Today);
    CHECK(std::string_view(page.guideTopic()) == guide::topics::TrainingPage);

    // Ctrl+K: the page, "Review all", then each kind's with its part, in the parts' order (the repertoires' training
    // adds its own among the Openings page's).
    std::map<std::string, std::function<void()>> runs;
    const auto list = commands(page, &runs);
    REQUIRE(list.size() == 5);
    CHECK(list[0] == std::pair<std::string, std::string>{"Training", "Study"});
    CHECK(list[1] == std::pair<std::string, std::string>{"Review all · 7 to review today", "Training"});
    CHECK(list[2].first == "Drills");
    CHECK(list[3].first == "Exercises");
    CHECK(list[4].first == "Sight");
    page.showTrainingPage(TrainingPart::Mistakes);
    app.shown = false;
    runs["Training"]();
    CHECK(app.shown);
    CHECK(std::string_view(page.guideTopic()) == guide::topics::TrainingPage);

    // --focus: the page at Today or at a part (training-<the part's word>), the day's review, the kinds' own words; a
    // kind without a part has no word of the page's.
    CHECK(focus(page, "training-exercises"));
    CHECK(std::string_view(page.guideTopic()) == "the-part-of-exercises");
    CHECK(focus(page, "training-page"));
    CHECK(std::string_view(page.guideTopic()) == guide::topics::TrainingPage);
    CHECK_FALSE(focus(page, "training-lines"));
    CHECK(focus(page, "session-of-lines"));
    CHECK(kinds.lines.words_applied == 1);
    CHECK(focus(page, "session-of-sight"));
    CHECK(kinds.sight.words_applied == 1);
    CHECK(focus(page, "review-all"));
    CHECK(kinds.exercises.started == std::vector<std::size_t>{0});
    CHECK(page.reviewRunning());
}

TEST_CASE("training page: a game live on Lichess, or one played on the Board, keeps the day's review from starting") {
    ScratchDir profile("training-page-locked");
    FakeApp app(profile);
    Frames frames;
    Kinds kinds;
    TrainingPage page(app, kinds.given());
    kinds.plug(page);

    // A game of the user's live on Lichess: said, the review card's button and Enter start nothing, Ctrl+K has no
    // "Review all", nor its --focus word.
    app.live_game = true;
    CHECK(page.lock() == "Locked while one of your games is played on Lichess.");
    frames.draw(app, [&] { page.draw(); });
    REQUIRE(Frames::click("##review_card", "Review all · 7"));
    frames.draw(app, [&] { page.draw(); }, ImGuiKey_Enter);
    CHECK(kinds.exercises.started.empty());
    CHECK_FALSE(page.reviewRunning());
    const auto list = commands(page);
    CHECK(std::none_of(list.begin(), list.end(), [](const auto& c) { return c.first.find("Review all") == 0; }));
    CHECK(focus(page, "review-all"));  // the word is the page's, and does nothing now
    CHECK(kinds.exercises.started.empty());
    // Enter on Today went to the review, not to what the kinds offer under their cards.
    for (auto* k : kinds.all()) CHECK(k->choice_enter == 0);

    // A game played on the Board: the same, said otherwise.
    app.live_game  = false;
    app.board_game = true;
    CHECK(page.lock() == "Once the game played on the board is over.");
    page.startReview();
    CHECK(kinds.exercises.started.empty());

    // Over: Enter on Today starts the day's review; "Only the exercises" theirs alone, the repertoires left.
    app.board_game = false;
    CHECK(page.lock().empty());
    frames.draw(app, [&] { page.draw(); }, ImGuiKey_Enter);
    CHECK(kinds.exercises.started == std::vector<std::size_t>{0});
    CHECK(page.reviewRunning());
    page.stopReview();
    kinds.exercises.closeSession(false);
    frames.draw(app, [&] { page.draw(); });
    REQUIRE(Frames::click("##review_card", "Only the exercises"));
    frames.draw(app, [&] { page.draw(); });
    CHECK(kinds.exercises.started == std::vector<std::size_t>{0, 0});
    CHECK(kinds.lines.started.empty());
    frames.draw(app, nextCard(page));
    REQUIRE(Frames::click("##next_card", "Back to the Training page · Enter"));
    frames.draw(app, nextCard(page));
    CHECK_FALSE(page.reviewRunning());
    CHECK(page.boardSession() == nullptr);
}
