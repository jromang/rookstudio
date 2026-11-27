#pragma once

// Lichess's puzzles kept offline (#80, mockup M12, board 1): its puzzle database (lichess_db_puzzle.csv.zst on
// database.lichess.org, CC0: 6,193,530 puzzles in November 2026) read into a store of RookStudio's own, read in place.
// Each puzzle takes about 44 bytes: its position in squares and men, its moves a byte each (the databases' codes,
// scan_board.hpp), its rating, its game's id; the puzzles sorted by rating, so that a rating's puzzles are side by
// side, and lists of the puzzles of each theme and opening, by rating too. The format, its sizes and its speed:
// docs/design/puzzles.md.
//
// The database's lines are read as they come: each puzzle is written to a file of the build as it is read, and at the
// end they are sorted into the store. A reading cut short goes on from the place it kept (the file's length and the
// names met so far).

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>
#include <tour/files.hpp>

#include "mapped_file.hpp"

namespace fcg::study {

// A puzzle as the database gives it: the position before the opponent's last move, that move, then the solution.
struct Puzzle {
    std::string id;   // Lichess's: five letters and digits
    std::string fen;  // the position before the opponent's move
    // In UCI ("e2e4", "e7e8q", "e1g1"): the opponent's move, then the solution; the solver plays those at odd places.
    std::vector<std::string> moves;
    int rating     = 0;
    int deviation  = 0;                 // of its rating; 255 at most kept
    int popularity = 0;                 // -100 to 100: what its players voted
    std::vector<std::string> themes;    // Lichess's keys: "fork", "mateIn2", "endgame"
    std::vector<std::string> openings;  // "Sicilian_Defense", "Sicilian_Defense_Najdorf_Variation"
    std::string game;                   // the game it comes from: its id, eight letters and digits
    bool operator==(const Puzzle&) const = default;

    // The game at the puzzle's position, as the database links it: https://lichess.org/game/<id>[/black]#<ply>, the
    // ply after the opponent's move, from the side of the one who plays it.
    [[nodiscard]] std::string gameUrl() const;
};

// A line of the database (PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,GameUrl,OpeningTags[,
// DailyDate]), its fields as text; false when it is not one (the header among them).
struct PuzzleLine {
    std::string_view id, fen, moves, themes, url, openings;
    int rating = 0, deviation = 0, popularity = 0;
};
[[nodiscard]] bool readPuzzleLine(std::string_view line, PuzzleLine& out);

// The game id in a puzzle's GameUrl ("https://lichess.org/game/787zsVup/black#48", or without "game/"); empty when it
// is not one.
[[nodiscard]] std::string_view puzzleGameId(std::string_view url);

// The store: header, the names of the themes and openings, the first puzzle of each rating, where each block of 64
// puzzles starts, the puzzles, then a list of each theme's and each opening's puzzles.
class PuzzleStore {
   public:
    // Throws std::runtime_error when the file cannot be read or is not a store (one cut short included).
    explicit PuzzleStore(const std::filesystem::path& file);

    [[nodiscard]] uint32_t size() const noexcept { return count_; }           // puzzles
    [[nodiscard]] uint64_t lines() const noexcept { return lines_; }          // of the database read for it
    [[nodiscard]] uint64_t skipped() const noexcept { return skipped_; }      // of them, not kept (unreadable)
    [[nodiscard]] const std::string& date() const noexcept { return date_; }  // of the database, "" when not known
    [[nodiscard]] uint64_t bytes() const noexcept { return file_.size(); }

    // A theme or an opening, and its puzzles.
    struct Tag {
        std::string name;
        uint32_t puzzles = 0;
    };
    [[nodiscard]] const std::vector<Tag>& themes() const noexcept { return themes_; }
    [[nodiscard]] const std::vector<Tag>& openings() const noexcept { return openings_; }
    // A list: a theme's index, or the themes' count plus an opening's; none when the store has no such name.
    [[nodiscard]] std::optional<int> themeList(std::string_view name) const;
    [[nodiscard]] std::optional<int> openingList(std::string_view name) const;

    // The puzzles of ratings `low` to `high`, as a range of their numbers [first, last).
    [[nodiscard]] std::pair<uint32_t, uint32_t> ratingRange(int low, int high) const;
    // A list's puzzles within [first, last), in order; at most `limit`.
    [[nodiscard]] std::vector<uint32_t> listed(int list, uint32_t first, uint32_t last,
                                               std::size_t limit = SIZE_MAX) const;
    [[nodiscard]] bool inList(int list, uint32_t puzzle) const;

    // Up to `n` puzzles near `rating` (100 points first, wider when it has too few), of `list` (-1: every puzzle), at
    // random, those `skip` says not (already seen) and the ones its players disliked left out.
    [[nodiscard]] std::vector<uint32_t> pick(int list, int rating, std::size_t n,
                                             const std::function<bool(const std::string& id)>& skip,
                                             uint64_t seed) const;

    // A puzzle, by its number (0 to size() - 1).
    [[nodiscard]] Puzzle puzzle(uint32_t number) const;
    [[nodiscard]] int ratingOf(uint32_t number) const;
    [[nodiscard]] std::string idOf(uint32_t number) const;

   private:
    [[nodiscard]] const uint8_t* record(uint32_t number) const;
    struct List {
        const uint8_t* skips = nullptr;  // (first, offset) every 128 puzzles
        const uint8_t* data  = nullptr;  // the gaps between the others, as varints
        uint32_t count = 0, bytes = 0;
    };
    // Calls `each` with the list's puzzles from the chunk holding `from` on, until it returns false.
    void walk(const List& list, uint32_t from, const std::function<bool(uint32_t)>& each) const;

    MappedFile file_;
    uint32_t count_   = 0;
    uint64_t lines_   = 0;
    uint64_t skipped_ = 0;
    std::string date_;
    std::vector<Tag> themes_, openings_;
    const uint8_t* ratings_ = nullptr;  // 4097 u32: the first puzzle of each rating
    const uint8_t* blocks_  = nullptr;  // u64: where each block of 64 starts among the records
    const uint8_t* records_ = nullptr;
    std::vector<List> lists_;  // the themes', then the openings'
};

// The folder of the store: puzzles-<n>.bin, the newest that reads answering, and build/, a preparation under way or cut
// short.
[[nodiscard]] std::filesystem::path newestPuzzleStore(const std::filesystem::path& folder);  // empty when none
[[nodiscard]] std::filesystem::path nextPuzzleStore(const std::filesystem::path& folder);
// The older stores and the files left by a write cut short, removed (one in use stays, to be removed later).
void tidyPuzzleFolder(const std::filesystem::path& folder);

// The store made from the database's lines, in `build` (a folder of its own). Each puzzle read is written to the file
// of its hundred rating points (bucket-<n>.bin), so that at the end each file, sorted in memory (a few tens of
// megabytes at most), takes its place in the store. Its state is what the reading keeps at a frame's end (mark()): the
// files' lengths and the names met; the reading's place and that state make a reading cut short go on from there.
class PuzzleBuilder {
   public:
    explicit PuzzleBuilder(std::filesystem::path build);
    ~PuzzleBuilder();
    PuzzleBuilder(const PuzzleBuilder&)            = delete;
    PuzzleBuilder& operator=(const PuzzleBuilder&) = delete;

    // A line of the database: its puzzle kept when it reads (its position, its moves one after the other).
    void line(std::string_view line);
    // At a frame's end: the state there (the puzzles written, the names met).
    [[nodiscard]] nlohmann::json mark() const;
    // The puzzles written made safe on the disk. The reading saves the last mark right after.
    void commit();
    // Back to a state mark() gave: the puzzles written after it forgotten.
    void restore(const nlohmann::json& state);
    // From the database's start: everything forgotten.
    void startAgain();
    // The database's end: the puzzles sorted into the store, written to `store` aside then renamed; the build's files
    // removed. Empty, or why it could not be. `done` and `total` count the puzzles written.
    [[nodiscard]] std::string finish(const std::filesystem::path& store, const std::string& date,
                                     std::atomic<uint64_t>* done = nullptr, std::atomic<uint64_t>* total = nullptr);

    [[nodiscard]] uint64_t lines() const noexcept { return lines_; }  // the database's puzzles read
    [[nodiscard]] uint64_t kept() const noexcept { return kept_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

    static constexpr int BUCKETS = 41;  // a hundred rating points each, 4000 and more in the last

   private:
    [[nodiscard]] std::filesystem::path bucketFile(int bucket) const;
    void closeFiles();
    void fail(std::string why);
    [[nodiscard]] bool encode(const PuzzleLine& line);  // into scratch_: the store's record, then its themes
    [[nodiscard]] int nameOf(std::vector<std::string>& names, std::vector<std::pair<std::string, int>>& index,
                             std::string_view name, int most);

    std::filesystem::path build_;
    tour::File files_[BUCKETS];
    uint64_t lengths_[BUCKETS] = {};
    uint64_t lines_            = 0;
    uint64_t kept_             = 0;
    std::string error_;
    std::vector<std::string> themes_, openings_;
    // The names' numbers, sorted by name: found by a binary search.
    std::vector<std::pair<std::string, int>> theme_index_, opening_index_;
    std::vector<uint8_t> scratch_;
};

}  // namespace fcg::study
