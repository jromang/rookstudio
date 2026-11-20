#pragma once

// Lichess's evaluations kept offline (#58, mockup M11, board 6): for each position of Lichess's evaluations database
// (lichess_db_eval.jsonl.zst, CC0), the evaluation, the depth and the first move of its deepest search, in an index
// read in place. The database's lines are read as they come: their positions gather in memory, each few million
// sorted into a run written aside, and the runs merged into the index at the end. Meanwhile the runs answer, and a
// reading cut short goes on from the place it kept (the runs, and the log of the positions since the last one). The
// format, its sizes and its speed: docs/design/offline-evals.md.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <chess.hpp>
#include <json.hpp>

#include "mapped_file.hpp"

namespace fcg::study {

// What the index keeps of a position: the first line of its deepest search.
struct OfflineEval {
    int cp    = 0;  // White's, in centipawns, when not a mate
    int mate  = 0;  // moves to mate, > 0 when White mates; 0 when `cp` says
    int depth = 0;
    std::string best;  // its first move, in UCI ("e2e4", "e7e8q", "e1g1"); empty when the line had none
    bool operator==(const OfflineEval&) const = default;
};

// A line of the database, read.
struct EvalsLine {
    std::string_view fen;  // pieces, side to move, castling, en passant
    int searches = 0;      // its searches, each of one line or more
    // The deepest search (of two as deep, the one of more nodes) and its first line, White's score.
    int depth       = 0;
    uint64_t knodes = 0;
    int cp = 0, mate = 0;
    std::string_view move;  // the line's first move
    int lines = 0;          // that search's lines
    bool slow = false;      // read as JSON: its fields in another order (the FEN and the move then valid until the
                            // thread reads the next such line)
};
// False when the line is not one of the database's, or has no search with a score. `depths`, when given, gets the
// depth of each of its searches (for the measurements).
[[nodiscard]] bool readEvalsLine(std::string_view line, EvalsLine& out, std::vector<int>* depths = nullptr);

// A position as its key is made from: the squares occupied (64 bits), what stands on each in their order (a nibble),
// the side to move and the castling rights. Never the en passant square: chess.hpp keeps the square of a FEN and that
// of a move by two rules that do not always agree, and Lichess by a third, so a position just after a pawn went two
// squares would be kept under one key and asked for under another. Two positions that differ by that square alone
// share a key (the deepest evaluation kept); a best move that is not legal on the board asked about is not given.
struct EvalsPosition {
    uint8_t bytes[41]{};  // the occupied squares, then the side and the rights, then the men (two a byte)
    uint8_t size = 0;
    bool operator==(const EvalsPosition& other) const;
};
// A FEN's position: false when it cannot be read. Its castling rights as chess.hpp reads them (a right whose king or
// rook is not in its corner is dropped).
[[nodiscard]] bool evalsPosition(std::string_view fen, EvalsPosition& out);
[[nodiscard]] EvalsPosition evalsPosition(const chess::Board& board);
// The men on its board, kings included.
[[nodiscard]] int evalsMen(const EvalsPosition& position);
[[nodiscard]] int evalsMen(std::string_view fen);

// The secret of an index, and of the preparation that makes it: a position's key is SipHash-2-4 of the position under
// that secret, drawn at random for each preparation. Polyglot's keys are public, and Lichess's database holds
// positions made to share theirs with the start position and others (19 of them, side by side in the file of
// November 2026): keyed by Polyglot's hash, a deeper one replaced the real one. Under a secret, no position can be made
// to share another's key.
struct EvalsSecret {
    uint64_t k0 = 0, k1 = 0;
    bool operator==(const EvalsSecret&) const = default;
};
[[nodiscard]] EvalsSecret newEvalsSecret();
[[nodiscard]] uint64_t evalsKey(const EvalsPosition& position, const EvalsSecret& secret);
[[nodiscard]] uint64_t sipHash24(const uint8_t* data, std::size_t size, uint64_t k0, uint64_t k1);

// The openings, when not every position is kept: those with at least this many men on the board. The database's FENs
// carry no move number: in a month of Lichess's games, 24 men or more is 89 % of the positions of the first 20 moves
// and 8 % of the later ones (docs/design/offline-evals.md).
inline constexpr int EVALS_OPENING_MEN = 24;

// The 5 bytes an evaluation takes: its score (a mate as ±(32000 − moves)), its depth (255 at most) and its move (from,
// to and promotion).
struct PackedEval {
    uint8_t bytes[5]{};
    bool operator==(const PackedEval&) const = default;
};
[[nodiscard]] PackedEval packEval(int cp, int mate, int depth, std::string_view move);
[[nodiscard]] OfflineEval unpackEval(const PackedEval& packed);
[[nodiscard]] int packedDepth(const PackedEval& packed);

// The index: the positions sorted by their key, in buckets of the key's first bits (a table of where each starts), each
// position's next 32 bits beside its evaluation, and its secret in its header. A lookup reads a word of the table and a
// few records: two pages of the disk at first, then memory.
class EvalsIndex {
   public:
    // Throws std::runtime_error when the file cannot be read or is not an index (a file cut short included).
    explicit EvalsIndex(const std::filesystem::path& file);

    [[nodiscard]] std::optional<OfflineEval> find(const EvalsPosition& position) const;
    [[nodiscard]] std::optional<PackedEval> findKey(uint64_t key) const;  // a key made with secret()
    [[nodiscard]] const EvalsSecret& secret() const noexcept { return secret_; }
    [[nodiscard]] uint64_t positions() const noexcept { return count_; }
    [[nodiscard]] uint64_t lines() const noexcept { return lines_; }          // of the database read for it
    [[nodiscard]] int minMen() const noexcept { return min_men_; }            // 0: every position
    [[nodiscard]] const std::string& date() const noexcept { return date_; }  // of the database, "" when not known
    [[nodiscard]] uint64_t bytes() const noexcept { return file_.size(); }

   private:
    MappedFile file_;
    uint64_t count_ = 0, lines_ = 0;
    int bits_ = 0, min_men_ = 0;
    std::string date_;
    EvalsSecret secret_;
    const uint8_t* records_ = nullptr;
    const uint8_t* fence_   = nullptr;
};

// A run: the positions of a part of the database, sorted by their key (its first 56 bits), a table of where each of
// 65,536 buckets starts at the end.
class EvalsRun {
   public:
    explicit EvalsRun(const std::filesystem::path& file);  // throws std::runtime_error
    // The packed evaluation of a position, when the run has it: by the position, or by its key under secret().
    [[nodiscard]] std::optional<PackedEval> find(const EvalsPosition& position) const;
    [[nodiscard]] std::optional<PackedEval> findKey(uint64_t key) const;
    [[nodiscard]] const EvalsSecret& secret() const noexcept { return secret_; }
    [[nodiscard]] uint64_t positions() const noexcept { return count_; }

   private:
    MappedFile file_;
    EvalsSecret secret_;
    uint64_t count_         = 0;
    const uint8_t* records_ = nullptr;
    std::vector<uint32_t> fence_;  // in memory (256 KB): a lookup reads one page of the run, not two
};

// What answers: the index, or the runs of a preparation under way or cut short. Read-only, shared between threads.
class EvalsLookup {
   public:
    EvalsLookup() = default;
    EvalsLookup(std::unique_ptr<EvalsIndex> index, std::vector<std::unique_ptr<EvalsRun>> runs);
    [[nodiscard]] std::optional<OfflineEval> find(const EvalsPosition& position) const;
    [[nodiscard]] const EvalsIndex* index() const noexcept { return index_.get(); }
    [[nodiscard]] uint64_t positions() const noexcept;  // the index's, else those of the runs
    [[nodiscard]] bool empty() const noexcept { return !index_ && runs_.empty(); }

   private:
    std::unique_ptr<EvalsIndex> index_;
    std::vector<std::unique_ptr<EvalsRun>> runs_;
};

// The folder of the evaluations kept: index-<n>.bin, the newest that reads answering, and build/, a preparation
// under way or cut short (its reading's state in build/reading.json, its runs).
[[nodiscard]] std::filesystem::path newestEvalsIndex(const std::filesystem::path& folder);  // empty when none
[[nodiscard]] std::filesystem::path nextEvalsIndex(const std::filesystem::path& folder);
// The older indexes and the files left by a write cut short, removed (one in use stays, to be removed later).
void tidyEvalsFolder(const std::filesystem::path& folder);
// The runs a preparation's state names (`state`: what EvalsBuilder::mark() gave), as files of `build`.
[[nodiscard]] std::vector<std::filesystem::path> evalsRuns(const std::filesystem::path& build,
                                                           const nlohmann::json& state);

// The index made from the database's lines, in `build` (a folder of its own). Its state is what the reading keeps at
// a frame's end (mark()); the reading's place and that state make a reading cut short go on from there.
class EvalsBuilder {
   public:
    struct Options {
        int min_men             = 0;  // keep only the positions with this many men or more; 0 for all
        std::size_t run_entries = std::size_t{1} << 23;  // positions in memory before a run is written (8 M: 128 MB)
        std::optional<EvalsSecret> secret;               // the tests' own; drawn at random by default
    };
    EvalsBuilder(std::filesystem::path build, Options options);
    ~EvalsBuilder();
    EvalsBuilder(const EvalsBuilder&)            = delete;
    EvalsBuilder& operator=(const EvalsBuilder&) = delete;

    // A line of the database: its position kept when it has an evaluation and is of the scope.
    void line(std::string_view line);
    // The same, read already.
    void add(const EvalsLine& line);
    // At a frame's end: a run written when enough positions wait; the state there.
    [[nodiscard]] nlohmann::json mark();
    // The positions since the last run made safe in the log, and the files the state saved before no longer names
    // removed. The reading saves the last mark right after.
    void commit();
    // Back to a state mark() gave (and the reading saved): the runs it names, the positions of its log; the rest
    // removed.
    void restore(const nlohmann::json& state);
    // From the database's start: everything removed.
    void startAgain();
    // The database's end: the runs merged into the index, written to `index` aside then renamed; the runs and logs
    // removed. Empty, or why it could not. `merged`, while it merges: the positions written so far, of `total`.
    [[nodiscard]] std::string finish(const std::filesystem::path& index, const std::string& date,
                                     std::atomic<uint64_t>* merged = nullptr, std::atomic<uint64_t>* total = nullptr);

    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    [[nodiscard]] uint64_t lines() const noexcept { return lines_; }  // of the database read
    [[nodiscard]] uint64_t kept() const noexcept { return in_runs_ + buffer_.size(); }
    [[nodiscard]] uint64_t unread() const noexcept { return unread_; }  // lines that are not a position evaluated
    // The runs written so far, which answer while it goes on (on the reading's thread, as the rest).
    [[nodiscard]] std::vector<std::filesystem::path> runs() const;

   private:
    struct Entry {
        uint64_t key = 0;  // the key's first 56 bits
        PackedEval eval;
    };
    void cutRun();
    void openLog(bool truncate);
    void fail(std::string why);
    [[nodiscard]] std::filesystem::path runFile(uint64_t id) const;
    [[nodiscard]] std::filesystem::path logFile(uint64_t id) const;

    std::filesystem::path build_;
    Options options_;
    std::vector<Entry> buffer_;  // the positions since the last run, in their order
    std::size_t logged_ = 0;     // of them, written to the log
    std::vector<uint64_t> runs_;
    uint64_t log_   = 0;  // the log's id
    uint64_t next_  = 1;  // the next file's id
    uint64_t lines_ = 0, unread_ = 0, in_runs_ = 0;
    std::FILE* log_file_ = nullptr;
    // Logs replaced by a run, each with the mark it was replaced at: removed once a later state is saved.
    std::vector<std::pair<uint64_t, uint64_t>> retired_;
    uint64_t marks_ = 0, saved_mark_ = 0;
    EvalsSecret secret_;
    std::string error_;
};

}  // namespace fcg::study
