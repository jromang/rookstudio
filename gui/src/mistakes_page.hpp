#pragma once

// Your mistakes as exercises (#81, mockup M12, board 2), a module of its own out of App (#107; mistakes.hpp is their
// data): the exercises the user's reviewed games give, looked for in the background when the databases, the names or
// the reviews change; the latest of the user's games without a review, reviewed in the background as the Board page
// reviews a game; the Training page's part "My mistakes"; the day's review of them; a session on the Board page whose
// column becomes the exercise's (a session of the Board's, board_session.hpp); the Home page's line; its palette
// commands and --focus words.
//
// What it needs of the program, it asks of its context (MistakesContext, app_context.hpp): App's, or a test's.

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <chess.hpp>

#include "app_context.hpp"
#include "board_session.hpp"
#include "focus_words.hpp"
#include "mistakes.hpp"
#include "review_queue.hpp"
#include "review_runner.hpp"

namespace fcg {

class MistakesPage final : public BoardSession {
   public:
    explicit MistakesPage(MistakesContext& app);
    ~MistakesPage() override;
    MistakesPage(const MistakesPage&)            = delete;
    MistakesPage& operator=(const MistakesPage&) = delete;

    // Once a frame, whatever shows: the user's games looked for again (on the Training page, when what it looks at
    // changed), the games reviewed in the background, a session's answer judged by the review's engine.
    void poll();
    // The Training page's part "My mistakes": the user's names first, then the exercises, their kinds and the games
    // they come from, the games without a review, what starts.
    void drawPart();
    // The Home page's "In progress", while a session runs: where it is, Back to the session.
    void drawHomeLine();
    // Its entries of the command palette (Ctrl+K): the part (in the view `training_view`), the day's review of the
    // mistakes alone, today's exercises, the games reviewed in the background.
    void addCommands(const PaletteAdd& add, const std::string& training_view);
    // Its --focus words, tried where App's table has them (app_chess.cpp): mistakes-session, mistakes-review. `word` is
    // the word `words` was made with.
    void addFocusWords(FocusWords& words, const std::string& word);

    // The exercises asked: those of the record but a position of a repertoire of the same side (trained on the
    // Openings page); of `kind` when it is given ("-": those of no kind).
    [[nodiscard]] std::vector<const MistakeExercise*> mistakeExercises(const std::string* kind = nullptr);
    // The kinds of the mistakes since `since` (a packed date, packedDaysBefore()), counted (context::Mistakes).
    [[nodiscard]] std::map<std::string, std::size_t> mistakeKinds(uint32_t since);
    // The day's review: the exercises due and after, how long an answer takes; their session (false when none is due).
    [[nodiscard]] ReviewCount reviewCount();
    bool startReview();
    // The record of the exercises and their cards (mistakes/exercises.json), read once; kept (not by a run for
    // screenshots), a failure said in the console.
    [[nodiscard]] MistakeRecord& mistakeRecord();
    void saveMistakeRecord();
    // The user's names changed: their games looked for again at once.
    void lookAgain() { mistake_scan_checked_ = {}; }
    // The scan of the user's games stopped at its next game (the program closing).
    void cancelScan();

    // A session holds the Board; it, while it does.
    [[nodiscard]] bool onBoard() const { return mistake_ != nullptr; }
    [[nodiscard]] const MistakeRun* session() const { return mistake_.get(); }

    // The Board's hooks while a session holds it (BoardSession).
    void sessionMove(chess::Move move) override { mistakeAnswer(move); }
    void drawSessionPage() override { drawMistakePage(); }
    void sessionDropped() override { dropMistakes(); }
    void closeSession(bool back) override { closeMistakes(back); }
    [[nodiscard]] bool hidesAids() const override;
    [[nodiscard]] bool keptAside() const override { return mistake_ && mistake_->aside; }
    [[nodiscard]] const char* guideTopic() const override;

   private:
    [[nodiscard]] std::filesystem::path mistakesFolder() const { return app_.profileDir() / "mistakes"; }
    [[nodiscard]] std::string mistakeScanKey();  // what the scan looks at: the names, the databases, the reviews
    [[nodiscard]] MistakeCounts mistakeDayCounts(const std::string* kind = nullptr);
    // The games without a review, reviewed in the background one after the other.
    void startMistakeReviews();
    void cancelMistakeReviews();
    void startMistakeReview();  // the next game's
    void drawMistakeReviewsCard();
    // A session.
    [[nodiscard]] std::string mistakeLock() const;           // why a session cannot start, empty when it can
    bool startMistakes(bool review, bool new_only = false);  // the page's choice; false when nothing is to be asked
    void startMistakeRun(std::unique_ptr<MistakeRun> run);
    void askMistake();
    void showMistakePosition(const MistakeExercise& e, std::optional<chess::Move> then = std::nullopt);
    void mistakeAnswer(chess::Move move);
    void mistakeJudged();  // the search after the answer is back: right or wrong
    void mistakeGrade(bool right);
    void mistakeDontKnow();
    void mistakeRetry();
    void mistakeNext();
    void finishMistakes(bool stopped);
    void reviewMissedMistakes();  // "Review them now": the missed ones again, nothing graded
    void closeMistakes(bool to_training);
    void dropMistakes();                             // another game came on the board: the session left where it is
    void openMistakeGame(const MistakeExercise& e);  // the game it comes from, at the mistake
    // The Board page during a session.
    void mistakeKeys();
    void drawMistakePage();
    void drawMistakeBoard(float size);
    void drawMistakeColumn();
    void drawMistakeEnd();

    MistakesContext& app_;
    std::optional<MistakeRecord> mistake_record_;
    // The user's games looked for in the background.
    struct MistakeScanJob {
        std::string of;
        std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
        std::future<MistakeScan> done;
    };
    std::optional<MistakeScanJob> mistake_scan_job_;
    std::string mistake_scan_of_;  // what the last scan looked at: the names, the databases, the reviews
    std::chrono::steady_clock::time_point mistake_scan_checked_{};
    bool mistake_scanned_       = false;  // a scan came back this session
    std::size_t mistake_games_  = 0;      // the user's games it found
    std::size_t mistake_judged_ = 0;      // of them, with a review
    std::vector<UnreviewedGame> mistake_unreviewed_;
    std::string mistake_kind_;     // the kind chosen on the page ("" every kind)
    int mistake_keys_frame_ = -1;  // the frame the repertoires' positions were gathered in, by side
    std::array<std::unordered_set<uint64_t>, 2> mistake_repertoire_keys_;
    // The user's latest games without a review, reviewed one after the other in the background as the Board page
    // reviews a game: its engine, its processes beside the tournaments, its passes; their exercises come game by game.
    struct MistakeReviews {
        std::vector<UnreviewedGame> games;
        std::size_t done = 0;
        std::unique_ptr<ReviewRunner> runner;
        ReviewRunner::State state;
        uint64_t version = 0;
        std::chrono::steady_clock::time_point started, game_started;
        double plies_done = 0, seconds = 0;  // what the games done took, for the time left
        int processes = 1;
        std::string engine;
        std::string error;
    };
    std::optional<MistakeReviews> mistake_reviews_;
    // A session on the Board page.
    std::unique_ptr<MistakeRun> mistake_;
};

}  // namespace fcg
