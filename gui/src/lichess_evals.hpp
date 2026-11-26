#pragma once

// The positions Lichess's players had analysed (#53, mockup M8, board "Moteurs"), the second source of a book of
// starting positions: Lichess's evaluations database (lichess_db_eval.jsonl.zst, CC0; 22.5 GB and some 418 million
// positions in November 2026), read once while it downloads and not kept. Only the positions of the named openings'
// lines (opening_suite), and those one move off them, are kept with their deepest evaluation: a few megabytes, from
// which a book is made as often as wanted, out of an evaluation, a depth, the lines' plies and a number.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "jobs.hpp"
#include "lichess_http.hpp"
#include "opening_suite.hpp"

#include "zstd_reading.hpp"

namespace fcg {

inline constexpr const char* LICHESS_EVALS_URL = "https://database.lichess.org/lichess_db_eval.jsonl.zst";

// A position looked for in the database: on a named line, or one move off one.
struct EvalCandidate {
    std::string epd;        // its four fields, then its counters (hmvc, fmvn) by the shortest way to it
    std::string eco, name;  // the named variation it belongs to: the last one on the way to it
    int plies    = 0;       // from the start, by the shortest way
    bool on_line = false;   // on a named line, rather than one move off
};
// The positions of the named lines (`all`) and those one move off them, each once, in the table's order.
[[nodiscard]] std::vector<EvalCandidate> evalCandidates(const std::vector<NamedOpening>& all);

// What the database says of a position: its deepest search's first line.
struct Evaluation {
    int depth                                = 0;
    int cp                                   = 0;  // for White, in centipawns, when not a mate
    int mate                                 = 0;  // moves to mate, > 0 for White; 0 when `cp` says
    uint64_t knodes                          = 0;
    bool operator==(const Evaluation&) const = default;
};
// The key of a position, its pieces, side to move and castling rights: the first three fields of a FEN or an EPD.
[[nodiscard]] std::string_view evalKey(std::string_view fen);
// A line of the database read: its position's key and its deepest evaluation. False when the line is not one.
[[nodiscard]] bool readEvalLine(std::string_view line, std::string_view& key, Evaluation& evaluation);

struct EvaluatedPosition {
    EvalCandidate position;
    Evaluation evaluation;
};
// A position found, as the files of a reading keep it: a line of tab-separated fields.
[[nodiscard]] std::string evaluatedRow(const EvaluatedPosition& found);
[[nodiscard]] std::optional<EvaluatedPosition> readEvaluatedRow(std::string_view row);

// The candidates found in the database's lines, as they come.
class EvalScanner {
   public:
    explicit EvalScanner(std::vector<EvalCandidate> candidates);
    EvalScanner(const EvalScanner&)            = delete;
    EvalScanner& operator=(const EvalScanner&) = delete;

    // A line of the database; its position, when a candidate, kept with the deepest of its evaluations. The position
    // found, nullptr when none (or a shallower evaluation of one found already).
    const EvaluatedPosition* line(std::string_view line);
    // A position found by a reading before (going on from where it stopped).
    void restore(const EvaluatedPosition& found);
    void forget();  // nothing found: a reading starting again
    [[nodiscard]] std::size_t candidates() const noexcept { return candidates_.size(); }
    [[nodiscard]] std::size_t kept() const noexcept { return found_.size(); }
    [[nodiscard]] std::vector<EvaluatedPosition> found() const;  // in the candidates' order

   private:
    bool keep(std::size_t index, const Evaluation& evaluation);
    std::vector<EvalCandidate> candidates_;
    std::unordered_map<std::string_view, std::size_t> index_;  // key → candidate
    std::unordered_map<std::size_t, EvaluatedPosition> found_;
};

struct EvaluatedSuiteOptions {
    int from_cp                                         = 30;  // the evaluation kept, for White, in centipawns
    int to_cp                                           = 80;
    int depth                                           = 30;  // at least
    int min_plies                                       = 8;   // the positions' plies from the start
    int max_plies                                       = 16;
    bool one_per_name                                   = true;  // one position per named variation at most
    std::size_t most                                    = 500;   // when more come, an even spread over the ECO codes
    bool operator==(const EvaluatedSuiteOptions&) const = default;
};
// The positions `options` keep, in the ECO order, each with its evaluation (ce, acd) and its opening (c0). Per named
// variation, the one on its line first, then the deepest evaluation, then the one nearest the window's middle.
[[nodiscard]] std::vector<SuitePosition> evaluatedSuite(const std::vector<EvaluatedPosition>& all,
                                                        const EvaluatedSuiteOptions& options);

// The database's size on the server and its date, asked by its first byte: what the reading will download. Why it is
// not known (Failure): no answer, Lichess's refusal, or an answer without the size (BadAnswer).
struct EvalsSize : lichess::Failure {
    uint64_t bytes = 0;
    std::string date;  // its Last-Modified, as the server gives it
};
[[nodiscard]] EvalsSize evalsSize(const std::string& url, const std::atomic<bool>* cancel = nullptr);

// What a folder holds of the readings: the positions of one gone to its end, and where one cut short stopped.
struct EvalsFolder {
    bool done = false;  // evaluations.tsv there
    std::string date;   // of the database read
    uint64_t lines   = 0;
    std::size_t kept = 0;
    // A reading cut short: where it stopped, of how many bytes, from where.
    uint64_t stopped_at = 0, size = 0;
    std::string source;  // its URL or its file
};
[[nodiscard]] EvalsFolder evalsFolder(const std::filesystem::path& folder);
// The positions a reading gone to its end kept in `folder`; empty when none did.
[[nodiscard]] std::vector<EvaluatedPosition> evaluatedPositions(const std::filesystem::path& folder);

// Lichess's evaluations database read for the candidates, while it downloads (`source` an http(s) URL) or from a
// file downloaded before, into a folder of the profile: evaluations.tsv at the end; while it reads, the positions
// found so far and where it is (at a frame's end, every few seconds), so that a reading cut short (the network, the
// program closed, Cancel) goes on from there. A server whose file has changed since is read again from its start.
class EvalsReading {
   public:
    struct Progress : ZstdProgress {     // the compressed bytes read, the file's size, a reading gone on
        std::atomic<uint64_t> lines{0};  // positions of the database read
        std::atomic<uint64_t> kept{0};   // candidates found
    };
    // A reading under way when it goes is cancelled and parked in `jobs` (#105).
    explicit EvalsReading(JobGroup& jobs) : job_(jobs) {}
    EvalsReading(const EvalsReading&)            = delete;
    EvalsReading& operator=(const EvalsReading&) = delete;

    void start(std::string source, std::filesystem::path folder);
    [[nodiscard]] bool busy() const noexcept { return job_.running(); }
    void cancel() noexcept { job_.cancel(); }
    [[nodiscard]] const Progress& progress() const noexcept { return job_.state(); }
    // Once a reading has stopped: empty when it went to its end, else why it stopped ("cancelled" for Cancel).
    [[nodiscard]] std::optional<std::string> finished();
    // Seconds between checkpoints; the tests make it 0.
    void setCheckpointEvery(double seconds) noexcept { checkpoint_every_ = seconds; }
    // The wait before going on after the download was cut short, doubled each time; the tests make it shorter.
    void setRetryWait(std::chrono::milliseconds wait) noexcept { retry_wait_ = wait; }

   private:
    double checkpoint_every_ = 5;
    std::chrono::milliseconds retry_wait_{2000};
    Job<std::string, Progress> job_;
};

}  // namespace fcg
