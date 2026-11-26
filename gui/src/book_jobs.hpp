#pragma once

// The participants' PGN books turned into Polyglot books in the background (tour/pgn_book.hpp), one at a time, as
// soon as a setup has one: a start seldom has to wait for it, and when it does, it says so (see startGate). And the
// Polyglot books the user builds from a PGN file, on the Books page.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <tour/pgn_book.hpp>

#include "jobs.hpp"

namespace fcg {

class PgnBookJobs {
   public:
    struct Status {
        enum class State {
            Queued,     // to be prepared, after the one being prepared
            Preparing,  // being read
            Ready,      // in the cache, for the file as it is now
            Failed,     // cannot be prepared: `error`
        };
        State state     = State::Queued;
        double progress = 0;  // Preparing: the share of the file read
        tour::PgnBookStats stats;
        std::string error;
    };

    // A conversion that ended, for the console.
    struct Ended {
        std::string pgn;
        int plies = 0;  // of its games
        Status status;  // Ready or Failed
    };

    // A conversion in flight when it goes is cancelled and parked in `jobs` (#105).
    explicit PgnBookJobs(JobGroup& jobs) : job_(jobs) {}
    PgnBookJobs(const PgnBookJobs&)            = delete;
    PgnBookJobs& operator=(const PgnBookJobs&) = delete;

    // Where the book of `pgn` for `depth` stands. Asking is wanting: what shows a book asks at each frame, and so
    // does a start waiting for it; the next poll() starts its conversion when nothing else is being converted.
    Status want(const std::string& pgn, int depth, bool chess960);

    // A conversion that failed is done again when it is next asked for: the failure may have passed (a full disk,
    // a cache folder that could not be written, a file another program held). For what the user asks: a start, "Try
    // again"; never by itself, as what shows a book asks for it at each frame, and a PGN file that gives no move would
    // be read again and again.
    void retry(const std::string& pgn, int depth, bool chess960);

    // At each frame, after the frame's want(): collects the conversion that ended, cancels the one nobody asked
    // for since the last poll (a depth changed meanwhile, the book replaced), starts the next one asked for.
    std::vector<Ended> poll();

    // A conversion is in flight.
    [[nodiscard]] bool busy() const noexcept { return !job_.idle(); }

   private:
    struct Key {
        std::string pgn;
        int plies                          = 0;
        bool chess960                      = false;
        auto operator<=>(const Key&) const = default;
    };
    struct Book {
        uintmax_t size = 0;  // the file as it was when its status was found
        std::filesystem::file_time_type time{};
        bool known = false;  // its status was found for the file as it is
        Status status;
        uint64_t asked = 0;  // the frame it was last asked for
    };
    // The conversion in flight: its book and the file it reads as it was at its start (the window's), the share read.
    struct Converting {
        Key key;
        uintmax_t size = 0;
        std::filesystem::file_time_type time{};
        std::atomic<double> progress{0.0};
    };

    std::map<Key, Book> books_;
    Job<tour::PreparedBook, Converting> job_;
    uint64_t frame_ = 1;
};

// A Polyglot book built from a PGN file at the user's request (Books → Build a Polyglot book…), in the background,
// one at a time: the same conversion as the participants' books, with the user's options, written where the user
// chose rather than in the cache.
class PolyglotBuild {
   public:
    struct Ended {
        std::string pgn;
        std::string bin;
        tour::PgnBookOptions options;
        tour::PgnBookStats stats;  // what was read: also when no move was left (`empty`)
        std::string error;         // empty: the book is written
        bool empty     = false;    // failed as no move was left for the book
        bool cancelled = false;
    };

    // A build in flight when it goes is cancelled and parked in `jobs` (#105).
    explicit PolyglotBuild(JobGroup& jobs) : job_(jobs) {}
    PolyglotBuild(const PolyglotBuild&)            = delete;
    PolyglotBuild& operator=(const PolyglotBuild&) = delete;

    // Starts building `bin` from `pgn`; false when a build is in flight already.
    bool start(const std::string& pgn, const std::string& bin, const tour::PgnBookOptions& options);
    // Asks the build in flight to stop: it ends at its next progress report, and poll() gives it as cancelled.
    void cancel();

    [[nodiscard]] bool busy() const noexcept { return !job_.idle(); }
    [[nodiscard]] bool cancelling() const noexcept { return busy() && job_.cancelled(); }
    // The share of the PGN file read, 0 to 1.
    [[nodiscard]] double progress() const noexcept { return busy() ? job_.state().progress.load() : 0.0; }
    [[nodiscard]] std::string bin() const { return busy() ? job_.state().bin : std::string(); }
    [[nodiscard]] std::string pgn() const { return busy() ? job_.state().pgn : std::string(); }

    // At each frame: the build that ended, once.
    std::optional<Ended> poll();

   private:
    // The build in flight: what it reads and writes, with what options (the window's), the share read.
    struct Building {
        std::string pgn;
        std::string bin;
        tour::PgnBookOptions options;
        std::atomic<double> progress{0.0};
    };
    Job<tour::PgnBookStats, Building> job_;
};

}  // namespace fcg
