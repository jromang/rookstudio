#pragma once

// Searches through a database's games on every core: a position exactly, material, and patterns of men and pawns.
// The games' signatures leave out those that cannot match without replaying them. A search runs in the background,
// its hits growing as it goes, the games in order, and stops within a twentieth of a second when cancelled.

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <variant>
#include <vector>

#include "scan_board.hpp"
#include "store.hpp"

namespace fcg::study {

// A game that matches, and the first ply after which it does (0: its start).
struct SearchHit {
    GameId id                               = 0;
    uint32_t ply                            = 0;
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

using SearchQuery = std::variant<PositionQuery, MaterialQuery, PatternQuery>;

// The ply after which a game first matches, none when it never does. `use_signature` false replays every ply: what
// the tests compare with.
[[nodiscard]] std::optional<uint32_t> searchGame(const Snapshot& snapshot, GameId id, const SearchQuery& query,
                                                 bool use_signature = true);

class Search {
   public:
    // Starts searching a snapshot's games on `threads` threads (0: the machine's).
    Search(std::shared_ptr<const Snapshot> snapshot, SearchQuery query, int threads = 0);
    ~Search();
    Search(const Search&)            = delete;
    Search& operator=(const Search&) = delete;

    void cancel() noexcept;  // the threads stop within a twentieth of a second
    void wait();
    [[nodiscard]] bool done() const noexcept { return finished_ == threads_.size(); }
    [[nodiscard]] bool cancelled() const noexcept { return cancel_; }
    [[nodiscard]] uint64_t searched() const noexcept { return searched_; }  // games looked at
    [[nodiscard]] uint64_t total() const noexcept { return snapshot_->games(); }
    // The hits found so far, in the order of the games.
    [[nodiscard]] std::vector<SearchHit> hits() const;

   private:
    void work();

    std::shared_ptr<const Snapshot> snapshot_;
    SearchQuery query_;
    std::atomic<bool> cancel_{false};
    std::atomic<uint64_t> next_{0};  // the next part of the games to take
    std::atomic<uint64_t> searched_{0};
    std::atomic<std::size_t> finished_{0};
    mutable std::mutex mutex_;
    std::vector<std::vector<SearchHit>> parts_;  // each part's hits, once searched
    std::vector<std::thread> threads_;
};

// A whole search, waited for: for the tests and the benchmark.
[[nodiscard]] std::vector<SearchHit> searchAll(std::shared_ptr<const Snapshot> snapshot, const SearchQuery& query,
                                               int threads = 0);

}  // namespace fcg::study
