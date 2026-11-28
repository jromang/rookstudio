#pragma once

// Puzzles (#80, mockup M12, board 1), a module of its own out of App (#107; puzzles.hpp is their data,
// lichess_puzzles.hpp their part of the network): Lichess's puzzle database kept offline (downloaded, or read from a
// file downloaded before; Update, Remove), a batch of the user's Lichess account online, its results sent back at its
// end; the user's puzzles (their RookStudio rating, their results by theme, the missed ones as cards of the day's
// review); the Training page's Puzzles part, its card and the choice of puzzles its Today part shows too (a kind of
// training of the page's, training_kind.hpp); a series on the Board page, whose column becomes the puzzle's (a session
// of the Board's, board_session.hpp); its palette commands and --focus words.
//
// What it needs of the program around it, it asks of its context (PuzzlesContext, app_context.hpp): App's, or a test's.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>

#include <chess.hpp>

#include "app_context.hpp"
#include "focus_words.hpp"
#include "lichess_evals.hpp"
#include "lichess_puzzles.hpp"
#include "puzzles.hpp"
#include "repairs.hpp"
#include "review_queue.hpp"
#include "training_kind.hpp"

namespace fcg {

class PuzzlesPage final : public TrainingKind {
   public:
    // A series' puzzles, offline.
    static constexpr std::size_t PUZZLE_SERIES = 20;

    explicit PuzzlesPage(PuzzlesContext& app);
    ~PuzzlesPage() override;
    PuzzlesPage(const PuzzlesPage&)            = delete;
    PuzzlesPage& operator=(const PuzzlesPage&) = delete;

    // Once a frame, whatever shows: a preparation's end, Update's answer, the online batch's requests.
    void poll();
    // The Training page's Puzzles part (TrainingKind): the choice, the database's card once it is there, the results
    // by theme; its card on the Today part (the database, the user's rating).
    [[nodiscard]] Part part() const override;
    void drawPart() override;
    void drawTodayCards(TodayCards& cards) override;
    // The choice of puzzles (offline or online, the theme, the difficulty, what starts), on both parts of the Training
    // page; `enter`: Enter starts them, taken (false) when it did.
    void drawChooser(bool& enter);
    void drawTodayChoice(bool& enter) override { drawChooser(enter); }
    // The Home page's "In progress", while a series runs: where it is, Back to the series.
    void drawHomeLine() override;
    // Its entries of the command palette (Ctrl+K): the Puzzles part (in the view `training_view`), each theme's puzzles
    // once the database is there, its download, Update and Remove, the puzzles online.
    void addCommands(const PaletteAdd& add, const std::string& training_view) override;
    // Its --focus words, tried where the Training page has them (training_page.cpp): puzzles-online,
    // puzzles-file=FILE, puzzles-series[=THEME]. `word` is the word `words` was made with.
    void addFocusWords(FocusWords& words, const std::string& word) override;

    // A series of a theme (the Endgames part's mates): of the database on the disk, else of the account's batch when
    // the account allows it; false when there are neither (offline chosen, nothing started).
    bool startTheme(const std::string& theme);
    // The day's review: the missed puzzles due today and after; their series, the longest overdue first (false when
    // none is due); their share of "Review all" (TrainingKind): one step, "Next: 4 missed puzzles", "Only the puzzles".
    [[nodiscard]] ReviewCount reviewCount() override;
    bool startReview();
    [[nodiscard]] std::optional<ReviewKind> reviewKind() const override { return ReviewKind::Puzzles; }
    bool startReviewStep(std::size_t) override { return startReview(); }
    [[nodiscard]] std::string reviewNextLabel(std::size_t) override;
    [[nodiscard]] std::string dueWords(std::size_t n) const override;
    [[nodiscard]] const char* reviewOnlyLabel() const override;
    // Lichess's puzzle database read from `source` (an http(s) address, else a file of the disk) into the store.
    void startOffline(std::string source);

    // What the other parts of the Training page show of them: the database on the disk (its state, its store, null
    // when there is none), the user's rating, their puzzles.
    [[nodiscard]] OfflinePuzzles::State storeState() const { return offline_puzzles_->state(); }
    [[nodiscard]] std::shared_ptr<const study::PuzzleStore> store() const { return offline_puzzles_->store(); }
    [[nodiscard]] PuzzleRating puzzleRating();  // the user's, or where it would start
    [[nodiscard]] PuzzleRecord& puzzleRecord();
    [[nodiscard]] bool puzzleScopes() const;  // the token may read and write puzzles
    // Why the last request of the batch online gave nothing (empty when it went through), as the Puzzles part says it.
    [[nodiscard]] const std::string& onlineError() const { return online_error_; }
    // The same as a notice, in place of its line (#146): a token refused with Link Lichess again… (the answers kept go
    // by themselves once the account is linked again), a permission Lichess says the token lacks with the button that
    // asks for it, an outage, Lichess in trouble or its pause with Try again (the answers sent again, else the batch
    // asked again), the pause counted down; none while nothing failed. A fix of it carried out: Try again the module's
    // own (queued through the host's pause), the others the program's.
    [[nodiscard]] std::optional<Notice> onlineNotice() const;
    void repair(const Fix& fix);

    // A series holds the Board; it, while it does.
    [[nodiscard]] bool onBoard() const override { return puzzle_ != nullptr; }
    [[nodiscard]] const PuzzleRun* series() const { return puzzle_.get(); }

    // The Board's hooks while a series holds it (BoardSession).
    void sessionMove(chess::Move move) override { puzzleAnswer(move); }
    void drawSessionPage() override { drawPuzzlePage(); }
    void sessionDropped() override { dropPuzzles(); }
    void closeSession(bool back) override { closePuzzles(back); }
    [[nodiscard]] bool hidesAids() const override;
    [[nodiscard]] bool keptAside() const override { return puzzle_ && puzzle_->aside; }
    [[nodiscard]] const char* guideTopic() const override;

   private:
    [[nodiscard]] std::filesystem::path puzzlesFolder() const { return app_.profileDir() / "puzzles"; }
    [[nodiscard]] std::optional<int> lichessPuzzleRating() const;  // the linked account's, when known
    [[nodiscard]] std::string puzzleLock() const;                  // why a series cannot start, empty when it can
    // A series.
    void startPuzzles();  // of the page's choice
    void startPuzzleRun(std::unique_ptr<PuzzleRun> run);
    void askPuzzle();  // the next puzzle of the queue, the opponent's move played
    void showPuzzlePosition(std::size_t index,
                            std::size_t played);  // the board at the question, `played` solution moves after
    void puzzleAnswer(chess::Move move);
    void puzzleGrade(bool win);  // the first answer to the puzzle asked, kept
    void puzzleDontKnow();
    void puzzleRetry();
    void puzzleSolutionStep();  // the solution, a move at a time
    void puzzleNext();
    void finishPuzzles(bool stopped);
    void reviewMissedPuzzles();  // "Review the 4 now": the puzzles missed in a series of their own
    void closePuzzles(bool to_training);
    void dropPuzzles();  // another game came on the board: the series left where it is
    // The Board page during a series.
    void puzzleKeys();
    void drawPuzzlePage();
    void drawPuzzleBoard(float size);
    void drawPuzzleColumn();
    void drawPuzzleEnd();
    // The Training page.
    void drawPuzzleStoreCard();
    void drawOnlinePuzzles(bool& enter);
    void drawPuzzleThemes();  // the results by theme
    // Online (lichess_puzzles.hpp): the batch kept in the profile, its requests one at a time.
    lichess::OnlineBatch* onlineBatch();  // read from the profile once; null when none
    void saveOnlineBatch();
    void askPuzzleScopes();
    void askOnlineBatch();
    void sendOnlineResults();
    void askAgain(Retry what);  // its own Try again: the answers sent again, else a batch asked

    PuzzlesContext& app_;
    OwnRetries retries_;
    // Lichess's database offline, in the profile.
    std::unique_ptr<OfflinePuzzles> offline_puzzles_;
    std::string offline_puzzles_error_;  // why the last preparation stopped
    NetFailure offline_puzzles_failure_;  // the download's or the file's failure it says (#141), the file beside it
    std::string offline_puzzles_note_;   // what Update found
    bool offline_puzzles_remove_ = false;
    std::future<EvalsSize> offline_puzzles_update_;
    std::chrono::steady_clock::time_point offline_puzzles_since_{};
    uint64_t offline_puzzles_from_ = 0;
    // The user's puzzles: their rating, their results, the puzzles missed (in the profile's puzzles/ folder).
    std::optional<PuzzleRecord> puzzle_record_;
    // The page's choice: offline or online, the theme ("" every theme, else a theme's key or "opening:<key>"), the
    // difficulty.
    bool puzzles_online_ = false;
    std::string puzzles_theme_;
    PuzzleDifficulty puzzles_difficulty_ = PuzzleDifficulty::Normal;
    bool puzzles_themes_open_            = false;  // the list of every theme and opening
    std::array<char, 48> puzzles_filter_{};
    // A series on the Board page.
    std::unique_ptr<PuzzleRun> puzzle_;
    // Online: the batch, its requests, why the last one gave nothing.
    std::optional<lichess::OnlineBatch> online_batch_;
    bool online_batch_loaded_ = false;
    std::future<lichess::Answer<lichess::PuzzleBatch>> online_ask_;
    std::future<lichess::Answer<lichess::PuzzleSolved>> online_send_;
    std::string online_error_;                              // why the last request gave nothing
    lichess::Failure online_failure_;                       // the failure it says (none: nothing Lichess refused)
    std::string online_refused_token_;  // the token Lichess refused: once another is linked, the answers go with it
    std::chrono::steady_clock::time_point online_retry_{};  // a send that failed tried again then
    bool online_start_after_ = false;                       // the batch, once it comes, starts a series
    // The last 30 days on Lichess, by theme (puzzle:read): asked once a session, when the online choice shows.
    std::future<lichess::Answer<lichess::PuzzleDashboard>> online_dashboard_job_;
    std::optional<lichess::PuzzleDashboard> online_dashboard_;
    bool online_dashboard_asked_ = false;
};

}  // namespace fcg
