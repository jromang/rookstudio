#pragma once

// Puzzles (#80, mockup M12, board 1). Lichess's puzzle database kept offline: the store in the profile
// (study/puzzles.hpp), made from the database while it downloads (or from a file downloaded before); a preparation
// cut short goes on from where it stopped. And what the user keeps of their puzzles, in the profile's puzzles/ folder,
// which removing the store leaves alone: their RookStudio rating (Glicko-2, starting from their Lichess puzzle rating
// when an account is linked), a line per puzzle answered, and the puzzles missed, cards of the day's review (FSRS, as
// the repertoires' training). Nothing leaves the machine but the request for the file.
//
// A series runs on the Board page (puzzles_page.hpp, the puzzles' module, #107), whose column becomes the puzzle's:
// PuzzleRun is its state.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <chess.hpp>
#include <study/fsrs.hpp>
#include <study/glicko2.hpp>
#include <study/puzzles.hpp>
#include <study/training.hpp>

#include "jobs.hpp"
#include "zstd_reading.hpp"

namespace fcg {

// What the database of 1 November 2026 measured (docs/design/puzzles.md), for what is said before a download.
inline constexpr uint64_t PUZZLES_FILE_BYTES  = 309751173ull;
inline constexpr uint64_t PUZZLES_COUNT       = 6193530ull;
inline constexpr uint64_t PUZZLES_STORE_BYTES = 307628754ull;

class OfflinePuzzles {
   public:
    struct Progress : ZstdProgress {       // the compressed bytes read, the file's size, a reading gone on
        std::atomic<uint64_t> lines{0};    // puzzles of the database read
        std::atomic<uint64_t> kept{0};     // of them, kept
        std::atomic<bool> writing{false};  // the store being written, sorted by rating
        std::atomic<uint64_t> written{0}, total{0};
    };
    // What the folder holds.
    struct State {
        bool ready       = false;  // a store
        uint32_t puzzles = 0;
        uint64_t bytes   = 0;
        std::string date;  // of the database read, "" when not known
        // A preparation cut short: where its reading stopped, what it kept so far.
        bool stopped        = false;
        uint64_t stopped_at = 0, size = 0, kept = 0;
        std::string source;
    };

    // A preparation under way when it goes is cancelled and parked in `jobs` (#105).
    OfflinePuzzles(JobGroup& jobs, std::filesystem::path folder);
    OfflinePuzzles(const OfflinePuzzles&)            = delete;
    OfflinePuzzles& operator=(const OfflinePuzzles&) = delete;

    [[nodiscard]] const std::filesystem::path& folder() const noexcept { return folder_; }
    // The store, null when there is none. Shared: a thread holding it keeps its file open.
    [[nodiscard]] std::shared_ptr<const study::PuzzleStore> store() const;
    [[nodiscard]] State state() const;
    [[nodiscard]] uint64_t generation() const noexcept { return generation_; }  // changes with the store

    // A preparation: the database read from `source` (an http(s) address, else a file). One cut short goes on, unless
    // its source was another: then it starts again. The store there answers until the new one replaces it.
    void start(std::string source);
    [[nodiscard]] bool busy() const noexcept { return job_.running(); }
    void cancel() noexcept { job_.cancel(); }
    [[nodiscard]] const Progress& progress() const noexcept { return job_.state(); }
    // Once a preparation has stopped: how it ended, done when it went to its end.
    [[nodiscard]] std::optional<TaskEnd> finished();
    // The store removed, a preparation under way cancelled first: no store at once; the files go once it ended, at the
    // finished() that sees its end (it writes in the folder until then), and it is not said. The user's own puzzles
    // stay (another folder).
    void remove();
    // A preparation cut short forgotten, its files removed; the store, if any, stays.
    void forgetStopped();

    // The tests: seconds between saved places, the wait after a cut.
    void setCheckpointEvery(double seconds) noexcept { checkpoint_every_ = seconds; }
    void setRetryWait(std::chrono::milliseconds wait) noexcept { retry_wait_ = wait; }
    void setOnSaved(std::function<void()> f) { on_saved_ = std::move(f); }

   private:
    void reload();
    void removeNow();  // the folder removed, nothing preparing in it
    [[nodiscard]] std::filesystem::path build() const { return folder_ / "build"; }

    std::filesystem::path folder_;
    mutable std::mutex mutex_;
    std::shared_ptr<const study::PuzzleStore> store_;
    State state_;
    std::atomic<uint64_t> generation_{0};
    bool remove_when_ended_  = false;  // remove() during a preparation
    double checkpoint_every_ = 5;
    std::chrono::milliseconds retry_wait_{2000};
    std::function<void()> on_saved_;
    Job<TaskEnd, Progress> job_;
};

// ---------------------------------------------------------------------------
// The user's puzzles

// Their RookStudio rating: Glicko-2 over the offline puzzles, a puzzle a game against its own rating.
struct PuzzleRating {
    study::glicko2::Rating glicko;
    int games        = 0;
    std::string from = "default";  // "lichess": started from the linked account's puzzle rating
    int64_t last     = 0;          // the last puzzle rated, seconds since the epoch
};
// Where it starts: the Lichess puzzle rating when one is known (its deviation too), else 1500 and a new player's
// deviation.
[[nodiscard]] PuzzleRating startingRating(std::optional<int> lichess_rating, std::optional<int> lichess_deviation = {});
// The rating after a puzzle, the days since the last one widening its deviation first; `win`: solved at the first
// try, without a hint.
[[nodiscard]] PuzzleRating ratedAfter(const PuzzleRating& before, int puzzle_rating, int puzzle_deviation, bool win,
                                      int64_t now);

// A puzzle answered, as puzzles/results.jsonl keeps it (a line each).
struct PuzzleResult {
    std::string id;
    int64_t at    = 0;  // seconds since the epoch
    bool win      = false;
    int rating    = 0;             // the puzzle's
    double before = 0, after = 0;  // the user's rating (offline puzzles only)
    std::vector<std::string> themes;
    int64_t ms                                 = 0;
    bool online                                = false;  // a puzzle of a Lichess batch: its result goes to Lichess
    bool review                                = false;  // a missed puzzle asked again by the day's review: no rating
    bool operator==(const PuzzleResult&) const = default;
};

// A puzzle missed: a card of the day's review, the puzzle kept with it (it needs no store to be asked again).
struct PuzzleCard {
    study::Puzzle puzzle;
    study::fsrs::Card card;
    int64_t since                            = 0;  // when it was missed first
    bool operator==(const PuzzleCard&) const = default;
};

// A theme's results: puzzles tried, solved at the first try.
struct ThemeResults {
    int tried = 0, won = 0;
};

// What the profile keeps of the user's puzzles.
struct PuzzleRecord {
    std::optional<PuzzleRating> rating;       // none before the first offline puzzle
    std::vector<PuzzleResult> results;        // in the order answered
    std::map<std::string, PuzzleCard> cards;  // by puzzle id
    // Read from the results: the puzzles seen (never picked again), each theme's results (offline and online).
    std::set<std::string> seen;
    std::map<std::string, ThemeResults> themes;
    void add(const PuzzleResult& result);  // to the results, the seen and the themes
};
[[nodiscard]] PuzzleRecord readPuzzleRecord(const std::filesystem::path& folder);
void appendPuzzleResult(const std::filesystem::path& folder, const PuzzleResult& result);
bool writePuzzleRating(const std::filesystem::path& folder, const PuzzleRating& rating);
bool writePuzzleCards(const std::filesystem::path& folder, const std::map<std::string, PuzzleCard>& cards);

// The card of a puzzle just missed: graded Again, back once the training day ends (not within the series): due a
// second after `day_end`, it counts for tomorrow.
[[nodiscard]] PuzzleCard missedCard(const study::Puzzle& puzzle, int64_t now, int64_t day_end,
                                    const study::fsrs::Scheduler& scheduler);
// The cards due by the end of a training day, by `limit` (seconds since the epoch).
[[nodiscard]] std::size_t cardsDue(const std::map<std::string, PuzzleCard>& cards, int64_t limit);

// ---------------------------------------------------------------------------
// Solving

// The position of the puzzle's question (the opponent's move played) and the moves of the solution, as the board
// plays them; none when the puzzle's moves do not play (a store from another program's file).
struct PuzzlePlay {
    chess::Board before;                // the puzzle's position, before the opponent's move
    chess::Move opponent;               // that move
    std::vector<chess::Move> solution;  // the solver's moves at even places, the opponent's replies at odd ones
};
[[nodiscard]] std::optional<PuzzlePlay> puzzlePlay(const study::Puzzle& puzzle);

// A move of the solver's at `step` (an even place of the solution): right (the next move of the solution, or a
// checkmate, which Lichess accepts whatever the solution), the puzzle solved by it, or wrong.
enum class PuzzleAnswer { Right, Solved, Wrong };
[[nodiscard]] PuzzleAnswer judgeMove(const PuzzlePlay& play, std::size_t step, const chess::Board& board,
                                     chess::Move move);

// Lichess's theme keys in the words of the interface ("fork": "Fork", "mateIn2": "Mate in 2"); a key it does not know
// as it is.
[[nodiscard]] std::string puzzleThemeName(std::string_view key);
// The themes in the order the page offers them first: the motifs, then the mates, the phases, the lengths.
[[nodiscard]] const std::vector<std::string_view>& puzzleThemesShown();
// An opening tag ("Sicilian_Defense_Najdorf_Variation") as a name ("Sicilian Defense Najdorf Variation").
[[nodiscard]] std::string puzzleOpeningName(std::string_view key);

// Lichess's puzzle difficulties, by how far from the user's rating: easier -300, normal, harder +300.
enum class PuzzleDifficulty { Easier, Normal, Harder };
[[nodiscard]] int difficultyOffset(PuzzleDifficulty difficulty);
[[nodiscard]] const char* difficultyKey(PuzzleDifficulty difficulty);  // "easier", "normal", "harder"

// A series on the Board page.
struct PuzzleRun {
    enum class Source { Offline, Online, Review };
    enum class Phase { Asking, Reply, Right, Wrong, End };
    Source source = Source::Offline;
    std::string title;  // "Fork", "Puzzles to review"…
    std::vector<study::Puzzle> puzzles;
    std::deque<std::size_t> queue;  // the puzzles still to ask, the one asked first
    std::size_t total = 0;          // asked in all, those asked again in a review counted once
    std::size_t done  = 0;          // answered
    Phase phase       = Phase::Asking;

    // The puzzle asked.
    std::size_t current = 0;  // its index in `puzzles`
    std::optional<PuzzlePlay> play;
    std::size_t step           = 0;      // the solution's next move
    bool hint                  = false;  // the piece to play shown in blue, for the move asked
    bool hinted                = false;  // a hint was asked for in this puzzle: it counts as missed
    bool retry                 = false;  // played again after a wrong answer: not graded again
    bool shown                 = false;  // "I don't know", or the solution shown
    std::size_t solution_shown = 0;      // the solution's moves on the board, a move at a time
    bool again                 = false;  // a review's puzzle asked again in the series
    bool practice              = false;  // "Review them now": the missed puzzles again, nothing graded
    std::string played;                  // the wrong move, in SAN
    chess::Move played_move{};
    std::chrono::steady_clock::time_point started, asked, answered, reply_at;
    double seconds = 0;
    // What the answer changed.
    double rating_before = 0, rating_after = 0;
    int64_t next_review = 0;      // a review's card: when it comes back (seconds since the epoch)
    bool next_again     = false;  // a review's card asked again in this series
    bool hovered        = false;  // the mouse on the answer: no going on by itself meanwhile

    // The first answer to each puzzle, in the order asked.
    struct Outcome {
        std::size_t puzzle = 0;
        bool win           = false;
        std::string played;  // the wrong move in SAN, "" for "I don't know"
    };
    std::vector<Outcome> outcomes;
    double rating_start = 0;  // the user's rating when the series began (offline)

    // The board before the series, given back after it.
    bool aside = false;
    bool flip  = false;
    int tab    = 0;

    // The end.
    double took      = 0;
    bool stopped     = false;
    int missed_shown = -1;  // the missed puzzle whose position the board shows
};

}  // namespace fcg
