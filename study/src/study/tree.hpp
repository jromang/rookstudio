#pragma once

// The opening tree of a database: for a position, the games that reach it in their first plies, the moves they play
// from it and how those games ended. Built from a snapshot, written beside the segments, read in place.
//
// Sorted by their moves, a database's games make a trie: games that open with the same moves follow one another, so
// the games that reach a position by one move order are a run of that order. A position two games or more reach is
// kept with its runs: one for each move order several games share, and one for each game that reaches it by an order
// of its own (a transposition, also when only such games reach the position). A position a single game reaches is
// not kept, nor its game: the run of the position before tells which game played the move that leads there.
//
// What a game counts for: each position of its first plies() plies (the start included), where it reaches it first,
// with the move it plays from there or none when it ends there. A position with more than 256 move codes (a board of
// many queens) is the last followed of a game.
//
//   Name.fcgdb/
//     tree.json        {"format":"rookstudio-tree","version":1,"folder":"tree-NNNNNN","games":N,"plies":P,...}
//     tree-NNNNNN/     order.bin    the games' ids (u32) in the order of their moves
//                      keys.bin     the kept positions' Polyglot keys (u64), sorted
//                      index.bin    where every 32nd position's record starts in records.bin (u64)
//                      records.bin  each position's games and runs, and when it is popular its moves' numbers

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "mapped_file.hpp"
#include "scan_board.hpp"
#include "store.hpp"

namespace fcg::study {

// The plies of each game the tree follows, unless told otherwise: theory seldom goes further, and positions shared
// beyond are few (a commercial database of 11.7 M games: 14.77 M shared within 40 plies, 15.00 M within 60).
inline constexpr uint32_t TREE_PLIES = 60;

// What a set of games scored.
struct TreeStats {
    uint64_t games = 0;
    uint64_t white = 0, draws = 0, black = 0;  // won by White, drawn, won by Black; the others have no result
    uint64_t white_rated = 0, white_elo = 0;   // the games with White's rating, and the sum of those ratings
    uint64_t black_rated = 0, black_elo = 0;
    uint32_t last = 0;  // the latest date (packDate()), 0 when none is known
    void add(const HeaderNumbers& game) noexcept;
    void add(const TreeStats& other) noexcept;
    bool operator==(const TreeStats&) const = default;
};

// A move played from a position: its code on the position's board (ScanBoard::decode), END for the games that end
// there.
struct TreeMove {
    static constexpr int END = -1;
    int code                 = END;
    TreeStats stats;
    bool operator==(const TreeMove&) const = default;
};

// The games through a position, and the moves they played from it: the most played first, those that end there last.
struct TreeAnswer {
    TreeStats total;
    std::vector<TreeMove> moves;
    bool operator==(const TreeAnswer&) const = default;
};

// A game through a position: its id, the ply it reaches the position at, and the code of the move it plays from there.
struct TreeGame {
    GameId id                              = 0;
    uint32_t ply                           = 0;
    int code                               = TreeMove::END;
    bool operator==(const TreeGame&) const = default;
};

class Tree {
   public:
    // The tree written in a database's folder: none when it has none, or one of a newer program. Throws
    // std::runtime_error when its files are damaged.
    static std::shared_ptr<const Tree> open(const std::filesystem::path& folder);

    [[nodiscard]] uint64_t games() const noexcept { return games_; }  // it covers the database's first games()
    [[nodiscard]] uint32_t plies() const noexcept { return plies_; }
    [[nodiscard]] uint64_t positions() const noexcept { return positions_; }  // those kept
    [[nodiscard]] const std::string& folderName() const noexcept { return name_; }

    // The games that reach a position (its Polyglot key) in their first plies(): none when fewer than two do. The
    // snapshot holds the tree's games (a later one of the same database does).
    [[nodiscard]] std::optional<TreeAnswer> at(const Snapshot& snapshot, uint64_t key) const;
    // Those games, in the order of their moves.
    [[nodiscard]] std::vector<TreeGame> gamesAt(const Snapshot& snapshot, uint64_t key) const;

    // Where a line leaves the database: the first ply after which no game reaches the line's position in its first
    // plies(), as far as the tree tells it. The line is its moves' codes on the boards from `start`
    // (ScanBoard::encode). None when the line stays in the database up to its end, or up to plies(). A position a
    // single game reaches by other moves than the line's is not seen (the position search finds it); a start fewer than
    // two games have is new.
    [[nodiscard]] std::optional<uint32_t> novelty(const Snapshot& snapshot, const ScanBoard& start,
                                                  std::span<const int> line) const;

    // The kept positions' keys, in order: for the tests.
    [[nodiscard]] uint64_t keyAt(uint64_t i) const noexcept;

    Tree() = default;  // use open()

   private:
    struct Run {
        uint64_t start = 0;
        uint64_t count = 0;
        uint32_t depth = 0;
    };
    struct Record {
        uint64_t games = 0;
        bool wide      = false;  // the position has more than 256 codes: two bytes a move
        std::vector<Run> runs;
        std::optional<TreeAnswer> numbers;  // popular positions'
    };
    [[nodiscard]] std::optional<Record> record(uint64_t key) const;

    MappedFile order_, keys_, index_, records_;
    uint64_t ordered_   = 0;  // the games order.bin holds
    uint64_t games_     = 0;
    uint32_t plies_     = 0;
    uint64_t positions_ = 0;
    std::string name_;
};

struct TreeOptions {
    uint32_t plies     = TREE_PLIES;            // at most 250
    int threads        = 0;                     // 0: the machine's
    std::size_t memory = std::size_t{2} << 30;  // about the most the build holds at once: more makes it take longer
};

struct TreeProgress {
    std::atomic<uint64_t> done{0};
    std::atomic<uint64_t> total{0};
};

struct TreeBuilt {
    uint64_t games     = 0;
    uint64_t positions = 0;
    uint32_t passes    = 0;  // over the games, for the positions a single game's moves reach
    bool cancelled     = false;
    std::string error;  // why the disk refused
};

// Builds the tree of a snapshot's games and makes it the database's: its files written and synced, then named in
// tree.json, whose replacement by a rename is the commit. A cancel, within about a second, or a refusal of the disk
// leaves the tree before as it was. One build at a time for a database.
TreeBuilt buildTree(const Database& db, const Snapshot& snapshot, const TreeOptions& options, TreeProgress& progress,
                    const std::atomic<bool>& cancel);

}  // namespace fcg::study
