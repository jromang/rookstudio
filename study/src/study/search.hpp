#pragma once

// Searches through a database's games on every core: a position exactly, material, patterns of men and pawns, patterns
// of the language (relations, lines of moves: patterns.hpp), a man's route, and a pawn structure exactly or near. The
// games' signatures leave out those that cannot match without replaying them. A search runs in the background, its
// hits growing as it goes, the games in order, and stops within a twentieth of a second when cancelled; a search
// cancelled can be resumed, the parts it searched kept.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <variant>
#include <vector>

#include "patterns.hpp"
#include "scan_board.hpp"
#include "store.hpp"

namespace fcg::study {

// A game that matches, and the first ply after which it does (0: its start); for a structure, how near (0 exactly,
// else the pawns of difference).
struct SearchHit {
    GameId id                               = 0;
    uint32_t ply                            = 0;
    uint8_t level                           = 0;
    bool operator==(const SearchHit&) const = default;
};

// A position exactly: the same men on the same squares, the same side to move, castling rights and en passant.
struct PositionQuery {
    ScanBoard position;
};

// Material: for White then Black, pawns, knights, bishops, rooks and queens, each within [low, high], for `plies`
// plies in a row at least (an exchange under way is not a material).
struct MaterialQuery {
    std::array<uint8_t, 10> low{};
    std::array<uint8_t, 10> high{8, 10, 10, 10, 9, 8, 10, 10, 10, 9};
    uint32_t plies = 1;
};

// A pattern: men on given squares, empty squares, and when asked a side's pawns exactly those given.
struct PatternQuery {
    std::array<std::array<uint64_t, 6>, 2> men{};  // [side][kind]: the squares such a man stands on
    uint64_t empty = 0;                            // squares nothing stands on
    std::array<bool, 2> only_these_pawns{};        // a side has no other pawns
    int side_to_move = -1;                         // WHITE_SIDE, BLACK_SIDE, or -1 for either
};

// A pattern of the language: conditions in one position, then a line of moves (patterns.hpp). Its ply: the first
// position of the pattern.
struct RelationQuery {
    std::shared_ptr<const PatternMatcher> matcher;
};

// A man's route: a man of `side` and `kind` goes from route[0] to route[1], then on to each square in turn, within
// `within` moves of its side; its ply: the position after its last move.
struct ManoeuvreQuery {
    int side = WHITE_SIDE;
    int kind = KNIGHT;
    std::vector<uint8_t> route;     // its squares, the first where it starts: two at least
    bool others_between = true;     // the side's other moves between the man's
    bool same_man       = true;     // one man all along; else a man of that kind on each square in turn
    uint32_t within     = 15;       // the side's moves from the route's first to its last, at most
    std::optional<ScanBoard> from;  // the route once a game reaches this position (the Board page's)
};

// A pawn structure: the pawns of both sides (or of one), exactly or one or two pawns away (a pawn more, fewer or on
// another square: one pawn), the colours reversed too when asked, held for `plies` plies in a row; the pieces do not
// count. Its ply: the first position of the first run at its nearest.
struct StructureQuery {
    std::array<uint64_t, 2> pawns{};  // White's, Black's
    int distance    = 0;              // the pawns of difference allowed
    bool both_sides = true;
    int side        = WHITE_SIDE;  // the side compared when not both
    bool reversed   = false;       // the colours reversed (and the board turned) too
    uint32_t plies  = 1;
};

using SearchQuery =
    std::variant<PositionQuery, MaterialQuery, PatternQuery, RelationQuery, ManoeuvreQuery, StructureQuery>;

// What a search found besides its games, gathered as it goes: the games' results, and for a route or a structure what
// the search panel tells of them.
struct SearchStats {
    std::array<uint64_t, 4> results{};  // the hits: White won, drawn, Black won, not known
    // A structure: the hits held exactly; the hits by ECO code (0: none); the variants one pawn away (variantKey())
    // with their results.
    uint64_t exact = 0;
    std::map<uint16_t, uint64_t> ecos;
    std::map<uint32_t, std::array<uint64_t, 4>> variants;
    // A route: the games where the man set off along it; where it went next after the route (by square); the routes
    // that part from it, sharing its first move at least (their squares), each counted once a game.
    uint64_t started = 0;
    std::array<uint64_t, 64> next{};
    std::map<std::vector<uint8_t>, uint64_t> parted;

    void add(const SearchStats& other);
};

// A structure's variant one pawn away, in the asked structure's colours: the side whose pawns differ, the square of a
// pawn gone (64: none) and of a pawn added (64: none); a pawn on another square has both.
[[nodiscard]] constexpr uint32_t variantKey(int side, int gone, int added) noexcept {
    return static_cast<uint32_t>(side) << 16 | static_cast<uint32_t>(gone) << 8 | static_cast<uint32_t>(added);
}

// The ply after which a game first matches, none when it never does. `use_signature` false replays every ply: what
// the tests compare with.
[[nodiscard]] std::optional<uint32_t> searchGame(const Snapshot& snapshot, GameId id, const SearchQuery& query,
                                                 bool use_signature = true);
// The same as a hit, with its level.
[[nodiscard]] std::optional<SearchHit> findInGame(const Snapshot& snapshot, GameId id, const SearchQuery& query,
                                                  bool use_signature = true);

class Search {
   public:
    // Starts searching a snapshot's games on `threads` threads (0: the machine's). `resume`: a search of the same query
    // and snapshot that was cancelled, whose parts searched are kept and not searched again.
    Search(std::shared_ptr<const Snapshot> snapshot, SearchQuery query, int threads = 0,
           const Search* resume = nullptr);
    ~Search();
    Search(const Search&)            = delete;
    Search& operator=(const Search&) = delete;

    void cancel() noexcept;  // the threads stop within a twentieth of a second
    void wait();
    [[nodiscard]] bool done() const noexcept { return finished_ == threads_.size(); }
    [[nodiscard]] bool cancelled() const noexcept { return cancel_; }
    [[nodiscard]] uint64_t searched() const noexcept { return searched_; }  // games looked at, in parts searched whole
    [[nodiscard]] uint64_t total() const noexcept { return snapshot_->games(); }
    // How long it took, from its start to its last thread's end, once done (not counting a wait for its result).
    [[nodiscard]] double elapsedMs() const noexcept { return static_cast<double>(elapsed_us_) / 1000.0; }
    // The hits found so far, in the order of the games.
    [[nodiscard]] std::vector<SearchHit> hits() const;
    // The hits of the parts searched one after the other from `part` on, appended to `out`; `part` moves past them:
    // the hits as they come, in order, without reading again those read before.
    void hitsFrom(std::size_t& part, std::vector<SearchHit>& out) const;
    // What it found besides, in the parts searched so far.
    [[nodiscard]] SearchStats stats() const;
    [[nodiscard]] const SearchQuery& query() const noexcept { return query_; }
    [[nodiscard]] const std::shared_ptr<const Snapshot>& snapshot() const noexcept { return snapshot_; }

   private:
    void work();

    std::shared_ptr<const Snapshot> snapshot_;
    SearchQuery query_;
    std::atomic<bool> cancel_{false};
    std::atomic<uint64_t> next_{0};  // the next part of the games to take
    std::atomic<uint64_t> searched_{0};
    std::atomic<std::size_t> finished_{0};
    std::size_t planned_                           = 0;  // the threads it starts
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
    std::atomic<int64_t> elapsed_us_{0};
    mutable std::mutex mutex_;
    std::vector<std::vector<SearchHit>> parts_;  // each part's hits, once searched
    std::vector<SearchStats> stats_;             // each part's, once searched
    std::vector<uint8_t> done_;                  // the parts searched whole
    std::vector<std::thread> threads_;
};

// A whole search, waited for: for the tests and the benchmark.
[[nodiscard]] std::vector<SearchHit> searchAll(std::shared_ptr<const Snapshot> snapshot, const SearchQuery& query,
                                               int threads = 0);

}  // namespace fcg::study
