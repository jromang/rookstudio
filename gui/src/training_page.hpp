#pragma once

// The Training page (#80, mockup M12, boards 1 to 3), a module of its own out of App (#107): its title line and its
// parts, Today and each kind's (the kinds of training plug in through TrainingKind, training_kind.hpp, and draw their
// own parts); the Today part (the day's review card, the kinds' cards, what the puzzles offer to start); the day's
// review in one queue (review_queue.hpp), "Review all" taking the kinds one after the other on the Board page, the
// repertoires one by one, "Next: …" at each session's end; the Home page's lines (the page offered once to a view made
// before it, the session under way, the day's count); the training's settings (training.json); why a session cannot
// start now; its palette commands and --focus words, with the kinds'. The session that holds the Board is the first of
// the kinds whose session does, in the order App gives them.
//
// What it needs of the program around it, it asks of its context (TrainingPageContext, app_context.hpp): App's, or a
// test's. It answers context::Training, which the kinds ask of App (App::ModulesContext hands it on).

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app_context.hpp"
#include "focus_words.hpp"
#include "review_queue.hpp"
#include "training_kind.hpp"

namespace fcg {

class TrainingPage final : public context::Training {
   public:
    // `kinds`: the kinds of training, App's modules, in the order the Board asks them which holds it.
    TrainingPage(TrainingPageContext& app, std::vector<TrainingKind*> kinds);
    ~TrainingPage() override;
    TrainingPage(const TrainingPage&)            = delete;
    TrainingPage& operator=(const TrainingPage&) = delete;

    // The page: its title and its parts, the part shown below; its guide's section (F1, the title's `?`).
    void draw();
    [[nodiscard]] const char* guideTopic() const;
    // The Home page: the page offered once to a view made before it (the Play page's offer waits while it is);
    // whether "In progress" has something of the training (a session holds the Board, or cards are due today); its
    // lines there (the session under way, the day's count and "Review all").
    void drawOffer();
    [[nodiscard]] bool offerPending() const { return offer_ == 0; }
    [[nodiscard]] bool due();
    void drawHome();
    // Its entries of the command palette (Ctrl+K), the page's in the view `training_view`: the page, "Review all",
    // the kinds' with their parts'. Its --focus words, tried where App's table has them (app_chess.cpp): review-all,
    // training-page and each part's (training-puzzles…), then the kinds' own. `word` is the word `words` was made with.
    void addCommands(const PaletteAdd& add, const std::string& training_view);
    void addFocusWords(FocusWords& words, const std::string& word);

    // The session that holds the Board, whatever its kind (none when none does); closed for another session that
    // starts (the Board page staying), the day's review going on.
    [[nodiscard]] BoardSession* boardSession() const;
    void closeSessionForAnother();
    // Why a session cannot start now (a game of the user's played on Lichess, a game played on the Board), empty when
    // it can (context::SessionBoard::sessionLock()).
    [[nodiscard]] std::string lock() const;
    // The day's review: what each kind has due, in the order "Review all" takes them; started, every kind or one alone.
    [[nodiscard]] ReviewDay reviewDay();
    void startReview(std::optional<ReviewKind> only = std::nullopt);

    // context::Training (app_context.hpp), which App answers with this page: the settings, the part shown, what Enter
    // starts, the day's review going on.
    [[nodiscard]] const TrainingSettings& trainingSettings() override;
    void setTrainingSettings(const TrainingSettings& settings) override;
    void showTrainingPage(TrainingPart part) override;
    [[nodiscard]] bool trainingShown() const override;
    void setEnterStartsPuzzles(bool puzzles) override { enter_puzzles_ = puzzles; }
    void reviewOnly(ReviewKind kind) override { startReview(kind); }
    [[nodiscard]] bool reviewRunning() const override { return review_running_; }
    bool nextReview() override;
    void stopReview() override;
    void drawReviewNext(bool enter) override;

   private:
    // A step of "Review all": a kind's session of its cards due (a repertoire's, for the repertoires' training).
    struct ReviewStep {
        TrainingKind* kind = nullptr;
        std::size_t step   = 0;
    };
    [[nodiscard]] TrainingKind* kindOf(ReviewKind kind) const;
    [[nodiscard]] std::vector<TrainingKind*> parts() const;  // the kinds with a part, in their places' order
    [[nodiscard]] std::string reviewNextLabel();             // the step that comes next: "Next: 4 missed puzzles"
    void saveSettings();
    // The Today part, its day's review card (`enter`: Enter reviews, taken when it did), the Home page's count.
    void drawToday();
    void drawReviewCard(bool& enter);
    void drawReviewHome();
    // The page offered once: already (a view holds it, or it was offered); the answer.
    [[nodiscard]] bool offered() const;
    void answerOffer(bool add);

    TrainingPageContext& app_;
    std::vector<TrainingKind*> kinds_;
    // The training's settings (training.json), read once: kept, but not by a run for screenshots.
    std::unique_ptr<TrainingSettings> settings_;
    TrainingKind* shown_ = nullptr;        // the part shown; none: Today
    int offer_           = -1;             // the page offered once: -1 not known yet, 0 to offer, 1 done
    bool enter_puzzles_  = false;          // Enter starts the puzzles, not the review: a choice of theirs was clicked
    std::deque<ReviewStep> review_chain_;  // what "Review all" takes after the session shown
    bool review_running_ = false;          // the session shown is one of them: its end goes on, or back to the page
};

}  // namespace fcg
