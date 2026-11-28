#pragma once

// A repertoire: the lines a player prepares with one colour, kept as a game and its variations (a PGN the Board page
// edits like any game), and what it covers of the games of the databases. At each of its positions where the
// opponent moves, the moves the databases' games played there that it answers, and those it leaves out: its gaps,
// the most often met first. A position its lines reach by two move orders is one position, found by its Polyglot key,
// with the moves of both (a transposition).
//
// What counts:
// - the end of a line after one of the repertoire's moves is where its owner stopped preparing: covered (unless
//   CoverageOptions::until_ply asks for the opponent's replies there);
// - the end of a line after an opponent's move is a gap when the databases' games go on from there: the repertoire's
//   own move is missing;
// - an opponent's move the games played often enough (CoverageOptions) that the repertoire does not answer is a gap;
// - its own choices at a position, when it has several, count alike; the opponent's moves count as often as the
//   games played them.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <chess.hpp>

#include "game.hpp"
#include "scan_board.hpp"
#include "store.hpp"
#include "tree.hpp"

namespace fcg::study {

// A move of the repertoire from one of its positions.
struct RepertoireMove {
    int code = 0;  // on the position's board (ScanBoard::encode), as the opening tree names its moves
    std::string san;
    uint64_t to = 0;  // the key of the position it leads to
};

// A position the repertoire's lines reach.
struct RepertoirePosition {
    uint64_t key = 0;
    std::string fen;
    bool own = false;  // the repertoire's side moves here: its moves are its choices, else the answers it has ready
    int ply  = 0;      // the fewest plies of its lines to it
    std::vector<RepertoireMove> moves;
    // Where its lines first reach it, a move being added there (playInTree): the line (-1 the game, else a variation)
    // and its ply in that line.
    int line     = -1;
    int line_ply = 0;
    // The position before on the shortest line to it (0 for the start: no position has key 0 in practice), and the
    // move from there.
    uint64_t from = 0;
    std::string via;
};

class Repertoire {
   public:
    // The positions of `lines`, the repertoire of `side`. A line stops at a null move.
    Repertoire(const GameTree& lines, chess::Color side, bool chess960 = false);

    [[nodiscard]] chess::Color side() const noexcept { return side_; }
    [[nodiscard]] bool chess960() const noexcept { return chess960_; }
    [[nodiscard]] uint64_t start() const noexcept { return start_; }
    [[nodiscard]] std::size_t size() const noexcept { return positions_.size(); }
    [[nodiscard]] const std::unordered_map<uint64_t, RepertoirePosition>& positions() const noexcept {
        return positions_;
    }
    [[nodiscard]] const RepertoirePosition* at(uint64_t key) const;
    // A move of the repertoire from the position: the star of the opening tree.
    [[nodiscard]] bool has(uint64_t key, int code) const;
    // The moves from the start to the position by its shortest line, in SAN; empty for the start or a position not in
    // the repertoire.
    [[nodiscard]] std::vector<std::string> lineTo(uint64_t key) const;

   private:
    chess::Color side_ = chess::Color::WHITE;
    bool chess960_     = false;
    uint64_t start_    = 0;
    std::unordered_map<uint64_t, RepertoirePosition> positions_;
};

// Adds `move`, played from the position after `path` (moves from the start of `lines`), to the repertoire's lines:
// where they reach that position already, by any move order, else as a variation where `path` leaves them, with the
// rest of `path`. False, and the lines unchanged, when a move is not legal.
bool addMove(GameTree& lines, bool chess960, std::span<const chess::Move> path, chess::Move move);

// The moves the databases' games played from a position, and how they scored: none when they have no games there to
// tell of.
using MovesAt = std::function<std::optional<TreeAnswer>(const ScanBoard& position)>;

// A database's games and their opening tree.
struct TreeBase {
    std::shared_ptr<const Snapshot> snapshot;
    std::shared_ptr<const Tree> tree;
};
// The opening trees of several databases, their numbers summed; a tree older than its database's edits is left out.
// `reading`, when given, is set to the base being read, by its place in `bases`: the database a store's error the moves
// throw comes from (a damaged tree, #149). It must outlive the moves.
[[nodiscard]] MovesAt treeMoves(std::vector<TreeBase> bases, std::size_t* reading = nullptr);

// The moves of a database's games where `player` (its name, whatever the case) had White, or Black: what the player
// and their opponents played from each position, out of those games alone (#52, preparing a game against them).
[[nodiscard]] MovesAt playerMoves(TreeBase base, std::string_view player, bool white);

struct CoverageOptions {
    double least_share   = 0.02;  // an opponent's move under this share of the position's games is no gap
    uint64_t least_games = 10;    // nor one fewer games played; nor a line's end that fewer games go on from
    std::size_t listed   = 100;   // the gaps listed
    // The end of a line after one of the repertoire's moves before this ply is a gap too, for the opponent's replies
    // the games played there: the depth its owner wants prepared. 0: those ends are covered.
    int until_ply = 0;
};

struct RepertoireGap {
    uint64_t key = 0;               // the position
    std::vector<std::string> line;  // the moves from the start to it, in SAN
    // The opponent's move the repertoire has no answer to, in SAN; empty: the repertoire's own move is missing here.
    std::string move;
    uint64_t games = 0;  // the games that played that move, or that went on from the position
    double share   = 0;  // that move's share of the games that went on from the position; 1 for a missing own move
    double reach   = 0;  // the share of the games along the repertoire that come to it
};

struct Coverage {
    // The share of the games along the repertoire that it answers to the end of its lines: the opponent's moves too
    // seldom played to be gaps left out.
    double covered = 1;
    std::vector<RepertoireGap> gaps;  // the most often reached first
    std::size_t positions = 0;        // the repertoire's
    std::size_t known     = 0;        // of them, those the databases have games of
};

[[nodiscard]] Coverage coverage(const Repertoire& repertoire, const MovesAt& moves,
                                const CoverageOptions& options = {});

}  // namespace fcg::study
