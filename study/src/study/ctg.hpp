#pragma once

// Opening books in the CTG format, read in place and never written. A book is three files of the same name: the .ctg
// holds the positions in pages of 4 KB, the .cto finds a position's page by a hash of the position, and the .ctb holds
// the range of that index in use.
//
// Positions are kept with White to move. One with Black to move is looked up mirrored: ranks reversed, colours
// swapped. One where neither side may castle and the side to move's king stands on files a to d is looked up with its
// files reversed as well. A position has the moves the book plays from it, each with a symbol, and the games that
// reached it: how many, how they ended, the players' ratings. A move's colour (the book's main move, or a move not to
// play in a tournament) and an evaluation are kept with the position the move leads to, so a move is told by the
// position it leads to, transpositions included.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include <chess.hpp>
#include <tour/pgn_book.hpp>

#include "mapped_file.hpp"
#include "tree.hpp"

namespace fcg::study {

// The file is one of a CTG book's: .ctg, .ctb or .cto, whatever the case.
[[nodiscard]] bool isCtg(const std::filesystem::path& file);

// What the book says of a move from a position.
struct CtgMove {
    chess::Move move;
    // The move's symbol, a NAG: 1 !, 2 ?, 3 !!, 4 ??, 5 !?, 6 ?!, 8 only move, 22 zugzwang. 0 when it has none.
    int symbol = 0;
    // The games of the position the move leads to, whichever moves led there. None when the book does not have that
    // position (it was past the depth the book was made to).
    std::optional<TreeStats> games;
    bool main  = false;  // the book's main move, shown green
    bool avoid = false;  // a move not to play in a tournament, shown red
    // The evaluation of the position the move leads to, a NAG for White and Black as they stand on the board: 10 =,
    // 13 unclear, 14 +=, 15 =+, 16 +/-, 17 -/+, 18 +-, 19 -+, 44 compensation... 0 when it has none.
    int evaluation = 0;
};

struct CtgAnswer {
    TreeStats games;             // the games that reached the position
    std::vector<CtgMove> moves;  // in the book's order; a promotion the format keeps as one move may come out several
    uint32_t skipped = 0;        // moves of the book that could not be read on the position (a damaged page)
};

// What a whole book holds, found by reading every page.
struct CtgSummary {
    uint64_t positions = 0;
    uint64_t moves     = 0;
    uint64_t main      = 0;  // positions a main move leads to
    uint64_t avoid     = 0;  // positions a move not to play in a tournament leads to
    bool cancelled     = false;
};

class CtgBook {
   public:
    // `file` is any of the book's three files: the other two are found beside it. Throws std::runtime_error with a
    // readable message when one is missing or is not of the format.
    explicit CtgBook(const std::filesystem::path& file);

    // The .ctg of the book, whichever file it was opened by.
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return ctg_path_; }
    // As the book's first page says: the games it was made of and the positions it holds.
    [[nodiscard]] uint32_t games() const noexcept { return games_; }
    [[nodiscard]] uint32_t positions() const noexcept { return positions_; }

    // The position of `board` in the book, with its moves: none when the book does not have it. A move that is not
    // legal on the board (a damaged page) is left out.
    [[nodiscard]] std::optional<CtgAnswer> at(const chess::Board& board) const;

    // Reads every page. `cancel` stops it within a page.
    [[nodiscard]] CtgSummary summary(const std::atomic<bool>* cancel = nullptr) const;

   private:
    struct Entry {
        std::span<const uint8_t> moves;  // two bytes a move: its code and its symbol
        std::span<const uint8_t> stats;  // 33 bytes
    };
    [[nodiscard]] std::optional<Entry> find(const chess::Board& board) const;
    [[nodiscard]] std::optional<Entry> findKey(std::span<const uint8_t> key) const;
    [[nodiscard]] std::optional<Entry> inPage(uint32_t page, std::span<const uint8_t> key) const;

    std::filesystem::path ctg_path_;
    MappedFile ctg_, cto_;
    int64_t lower_ = 0, upper_ = 0;  // the range of the index in use, from the .ctb
    uint32_t pages_ = 0, games_ = 0, positions_ = 0;
};

// What a walk through a book from the standard start found.
struct CtgWalk {
    uint64_t positions = 0;  // reached, each once
    uint64_t moves     = 0;
    uint64_t skipped   = 0;  // moves that could not be read on their position (a damaged page)
    uint64_t beyond    = 0;  // moves to a position the book does not have (past the depth it was made to)
    bool cancelled     = false;
};

// Walks a book's lines from the standard start, each position once (a transposition counts the first line that
// reaches it), calling `visit` with the board and the book's answer. `reached` counts the positions as they come;
// `cancel` stops the walk within a position.
CtgWalk walkCtg(const CtgBook& book, const std::function<void(const chess::Board&, const CtgAnswer&)>& visit,
                std::atomic<uint64_t>* reached = nullptr, const std::atomic<bool>* cancel = nullptr);

// A book as a Polyglot book for engines, which read no CTG. Each move weighs the points its games gave the side that
// played it (2 a win, 1 a draw), twice that for the book's main moves; a move whose games the book does not have
// weighs 1. Left out: the moves not to play in a tournament, and those that scored nothing. A position's weights are
// scaled down together when one passes 65535.
struct CtgPolyglot {
    std::vector<tour::PolyglotEntry> entries;
    CtgWalk walk;
    uint64_t avoided = 0;  // moves left out: not to play in a tournament
    uint64_t lost    = 0;  // moves left out: no point scored
};
[[nodiscard]] CtgPolyglot ctgPolyglot(const CtgBook& book, std::atomic<uint64_t>* reached = nullptr,
                                      const std::atomic<bool>* cancel = nullptr);

// A position's key in the book: its bytes as the format writes them (first byte: their number, 0x20 when an en passant
// capture is possible, 0x40 when a side may castle), and the hash its page is found by. For the tests.
[[nodiscard]] std::vector<uint8_t> ctgKey(const chess::Board& board);
[[nodiscard]] uint32_t ctgHash(std::span<const uint8_t> key);

}  // namespace fcg::study
