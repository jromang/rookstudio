#pragma once

// A month of Lichess's rated games (database.lichess.org, CC0: about 95 million games, 30 GB compressed) read as it
// downloads, or from a file downloaded before, each game kept or left by its headers; only those kept reach the
// database, committed by batches, the database read all along. Resumable, as any zstd reading (#72, mockup M10).

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <study/store.hpp>

#include "jobs.hpp"
#include "zstd_reading.hpp"

namespace fcg::lichess {

struct MonthFilter {
    int min_elo                               = 2400;   // both players rated at least this; 0: any
    bool bullet                               = false;  // bullet games kept: base time + 40 × increment under 3 minutes
    bool operator==(const MonthFilter&) const = default;
};

// Whether a game's text, by its headers, passes the filter.
[[nodiscard]] bool keepGame(std::string_view game, const MonthFilter& filter);

class MonthReading {
   public:
    struct Progress : ZstdProgress {
        std::atomic<uint64_t> games{0};  // read
        std::atomic<uint64_t> kept{0};   // passing the filter
        std::atomic<uint64_t> added{0};  // to the database (those it had already left out)
    };
    // A reading under way when it goes is cancelled and parked in `jobs` (#105).
    explicit MonthReading(JobGroup& jobs) : job_(jobs) {}
    MonthReading(const MonthReading&)            = delete;
    MonthReading& operator=(const MonthReading&) = delete;

    // `source`: the month's URL, or a .pgn.zst on the disk; its place kept in `state_file`, gone on from there when
    // the same source and filter are read again.
    void start(std::string source, std::shared_ptr<study::Database> db, MonthFilter filter,
               std::filesystem::path state_file);
    [[nodiscard]] bool busy() const noexcept { return job_.running(); }
    void cancel() noexcept { job_.cancel(); }
    [[nodiscard]] const Progress& progress() const noexcept { return job_.state(); }
    // Once stopped: empty when it went to its end, else why ("cancelled" for Cancel).
    [[nodiscard]] std::optional<std::string> finished();
    void setOptions(ZstdReadingOptions options) { options_ = options; }  // the tests' shorter waits
    static constexpr std::size_t BATCH = 20000;                          // games committed together, at most

   private:
    ZstdReadingOptions options_;
    Job<std::string, Progress> job_;
};

}  // namespace fcg::lichess
