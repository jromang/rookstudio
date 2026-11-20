#pragma once

// Lichess's evaluations kept offline (#58, mockup M11, board 6): the index in the profile (study/evals_index.hpp),
// prepared from Lichess's evaluations database while it downloads, or from a file downloaded before, and what it
// answers: the Analysis tab's line, the review's known positions, the Tree tab's column. A preparation cut short goes
// on from where it stopped, and meanwhile the positions it read answer already. Nothing leaves the machine but the
// request for the file.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <chess.hpp>
#include <study/evals_index.hpp>

#include "game_analysis.hpp"
#include "zstd_reading.hpp"

namespace fcg {

// The depth from which the review takes a position Lichess evaluated as it is, without searching it: deeper than
// what its last pass reaches by default (5 seconds a position on a few cores), and half of the database's positions.
inline constexpr int REVIEW_KNOWN_DEPTH = 30;

// What the database of 1 November 2026 measured (docs/design/offline-evals.md), for what is said before a preparation:
// its file, its positions, and the index of each choice.
inline constexpr uint64_t EVALS_FILE_BYTES     = 22499667845ull;
inline constexpr uint64_t EVALS_POSITIONS      = 417862375ull;
inline constexpr uint64_t EVALS_ALL_BYTES      = 3827631366ull;
inline constexpr uint64_t EVALS_OPENINGS_BYTES = 1569190732ull;

// A position's evaluation as the review keeps one: the side to move's score, the first move as its line, the depth;
// marked as known rather than searched.
[[nodiscard]] PositionEval knownEval(const study::OfflineEval& e, const chess::Board& board);

// A position's evaluation in `lookup` (standard chess only), White's; none when not kept, or when its move is not
// legal there (another position's: two keys alike in their first 56 bits).
[[nodiscard]] std::optional<study::OfflineEval> findEval(const study::EvalsLookup& lookup, const chess::Board& board);
// What the review takes of a position as it is: the evaluation kept, when its depth is REVIEW_KNOWN_DEPTH or more.
[[nodiscard]] std::optional<PositionEval> reviewKnownEval(const study::EvalsLookup& lookup, const chess::Board& board);

class OfflineEvals {
   public:
    struct Progress : ZstdProgress {       // the compressed bytes read, the file's size, a reading gone on
        std::atomic<uint64_t> lines{0};    // positions of the database read
        std::atomic<uint64_t> kept{0};     // of them, kept
        std::atomic<bool> merging{false};  // the runs being merged into the index
        std::atomic<uint64_t> merged{0}, total{0};
    };
    // What the folder holds.
    struct State {
        bool ready         = false;  // an index
        uint64_t positions = 0, bytes = 0, lines = 0;
        int min_men = 0;   // 0: every position
        std::string date;  // of the database read, "" when not known
        // A preparation cut short: where its reading stopped, what it kept so far.
        bool stopped        = false;
        uint64_t stopped_at = 0, size = 0, kept = 0;
        uint64_t answering  = 0;  // of those kept, the ones in its runs, which answer (the others are in its log)
        int stopped_min_men = 0;
        std::string source;
    };

    explicit OfflineEvals(std::filesystem::path folder);
    ~OfflineEvals();  // cancels and waits
    OfflineEvals(const OfflineEvals&)            = delete;
    OfflineEvals& operator=(const OfflineEvals&) = delete;

    [[nodiscard]] const std::filesystem::path& folder() const noexcept { return folder_; }
    // What answers now: the index, or the runs of a first preparation under way or cut short; null when nothing is
    // kept. Shared: a thread holding it keeps its files open.
    [[nodiscard]] std::shared_ptr<const study::EvalsLookup> lookup() const;
    // A position's evaluation (standard chess only), White's; none when not kept.
    [[nodiscard]] std::optional<study::OfflineEval> find(const chess::Board& board) const;
    [[nodiscard]] State state() const;
    // Changes each time what answers changes (a run written, the index made or removed).
    [[nodiscard]] uint64_t generation() const noexcept { return generation_; }

    // A preparation: the database read from `source` (an http(s) address, else a file), keeping the positions with
    // `min_men` men or more (0: all). One cut short goes on, unless its source or its choice were others: then it
    // starts again.
    void start(std::string source, int min_men);
    [[nodiscard]] bool busy() const noexcept { return busy_; }
    void cancel() noexcept { cancel_ = true; }
    [[nodiscard]] const Progress& progress() const noexcept { return progress_; }
    // Once a preparation has stopped: empty when it went to its end, else why it stopped ("cancelled" for Cancel).
    [[nodiscard]] std::optional<std::string> finished();
    // At each frame: the runs a preparation wrote opened, the index it made taken.
    void poll();
    // Everything removed, a preparation under way cancelled first.
    void remove();
    // A preparation cut short forgotten, its runs removed; the index, if any, stays.
    void forgetStopped();

    // The tests: seconds between saved places, the wait after a cut, the positions of a run.
    void setCheckpointEvery(double seconds) noexcept { checkpoint_every_ = seconds; }
    void setRetryWait(std::chrono::milliseconds wait) noexcept { retry_wait_ = wait; }
    void setRunEntries(std::size_t n) noexcept { run_entries_ = n; }
    // After each place saved, on the reading's thread (the tests copy the folder there, as a crash would leave it).
    void setOnSaved(std::function<void()> f) { on_saved_ = std::move(f); }

   private:
    void run(std::string source, int min_men);
    void reload();  // the state and the lookup read again from the disk
    [[nodiscard]] std::filesystem::path build() const { return folder_ / "build"; }

    std::filesystem::path folder_;
    mutable std::mutex mutex_;
    std::shared_ptr<const study::EvalsLookup> lookup_;
    State state_;
    std::thread thread_;
    std::atomic<bool> busy_{false}, cancel_{false};
    Progress progress_;
    std::optional<std::string> finished_;
    std::vector<std::filesystem::path> runs_;  // what the preparation published last
    std::atomic<uint64_t> runs_version_{0};
    std::atomic<uint64_t> generation_{0};
    uint64_t runs_seen_      = 0;
    double checkpoint_every_ = 5;
    std::chrono::milliseconds retry_wait_{2000};
    std::size_t run_entries_ = std::size_t{1} << 23;
    std::function<void()> on_saved_;
};

}  // namespace fcg
