#pragma once

// The opening report of a position (#91, mockup M13, board 4): what the user's databases say of it, from their opening
// trees and their games, made again each time it is asked (nothing of it is kept). In its sections, in the order they
// are made:
// - the moves played from the position, all the databases added, with the Tree tab's figures, and the main lines: the
//   most played continuations, as far as they stay common;
// - the pawn structures at move 20 in the position's games, and what each side does in the next fifteen moves: pawn
//   pushes and pieces' routes (as the Manoeuvre search follows a man), counted in those games, with their share and
//   score. The caller names the structures and the plans (the assistant's facts); a frequency is said from 50 games;
// - the key games: the strongest by the players' mean rating, the most recent above 2500, the first game of a novelty
//   repeated since;
// - the novelties along the main lines: a move's first year from the games that reach the position after it (the trees
//   of the databases, the position search near their horizon), how often it was repeated and its score; and the user's
//   own game that left the databases there, confirmed by the position search as the Tree tab confirms a novelty.
// A position fewer than two games of a database reach, or one past its tree's plies, is searched for in its games.

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "background.hpp"
#include "dossier.hpp"
#include "scan_board.hpp"
#include "store.hpp"
#include "tree.hpp"

namespace fcg::study {

// A database the report reads: its games and its opening tree, when the tree covers them (none: every game searched).
struct ReportSource {
    std::string name;
    std::shared_ptr<const Snapshot> snapshot;
    std::shared_ptr<const Tree> tree;
};

// A game of a source, and the ply after which it reaches the report's position.
struct ReportGame {
    uint32_t source                          = 0;
    GameId id                                = 0;
    uint32_t ply                             = 0;
    bool operator==(const ReportGame&) const = default;
};

// The moves played from the position, the sources added move by move.
struct ReportMoves {
    TreeStats total;
    std::vector<TreeMove> moves;       // the most played first; the games that end there left out
    std::vector<uint64_t> per_source;  // the position's games in each source
    // Each move's games in each source, by the moves' order (for the share of each at the cursor).
    std::vector<std::vector<uint64_t>> move_per_source;
    bool searched = false;  // a source answered by a search of the position, not by its tree
};

// A main line: its moves from the position (their codes, each on the board the moves before it lead to) and the games
// that reach its last position.
struct ReportLine {
    std::vector<int> codes;
    TreeStats stats;
    bool operator==(const ReportLine&) const = default;
};

// A pawn structure at move 20: the caller's name for it (StructureNamer's kind, -1: none of its names) and its owner.
struct ReportStructure {
    int kind  = -1;
    int owner = WHITE_SIDE;
    bool kept = false;  // the report's position has it already
    TreeStats stats;    // the games that have it, their results
    std::vector<ReportGame> games;
    bool operator==(const ReportStructure&) const = default;
};

// The caller's name for a pawn push's plan (the assistant's facts: a minority attack, a break and its lever…), from the
// board before the push; 0 for none.
using PlanNamer = std::function<int(const ScanBoard& before, const CodedMove& push)>;

// What a side does in the plies after the position, counted in its games.
struct ReportPlan {
    enum class Kind : uint8_t {
        Pushes,  // two pawn pushes, one then the other: "b4 then b5"
        Push,    // one pawn push: "e4"
        Route,   // a man's route, from where it stands: "Nf8-g6", "Nd7-f8-g6"
    };
    Kind kind = Kind::Pushes;
    int side  = WHITE_SIDE;
    // Pushes: the squares the two pushes go to; Push: one; Route: the man's squares, the first where it stands.
    std::vector<uint8_t> squares;
    uint8_t man = NO_KIND;  // Route: the man's kind
    int named   = 0;        // the caller's name (PlanNamer) most of its games give its pushes, 0 none
    TreeStats stats;        // the games where it is played, their results
    std::vector<ReportGame> games;
    bool operator==(const ReportPlan&) const = default;
};

struct ReportShapes {
    uint64_t replayed = 0;                    // the position's games replayed (all of them, or an even sample)
    uint64_t counted  = 0;                    // of them, those that reach the structure's ply
    uint32_t ply      = 0;                    // the structure's
    std::vector<ReportStructure> structures;  // the most met first, the games of none ("other") last
    std::vector<ReportPlan> plans;            // White's then Black's, the most played first
    bool enough = false;  // the position has the games a frequency needs: else neither structures nor plans
};

struct ReportKeyGame {
    enum class Why : uint8_t {
        Strongest,  // the players' highest mean rating
        Recent,     // the most recent with a mean rating of 2500 or more
        Novelty,    // the first game of a novelty repeated since (novelty: its index in ReportNovelties::moves)
    };
    Why why = Why::Strongest;
    ReportGame game;
    HeaderNumbers numbers;
    std::size_t novelty                         = 0;
    bool operator==(const ReportKeyGame&) const = default;
};

// A move along the main lines whose first game is recent, or the move by which a game of the user's left the databases.
struct ReportNovelty {
    std::vector<int> codes;  // from the report's position, the move itself last
    uint32_t first = 0;      // its first date (packDate()), from the games that reach the position after it
    TreeStats stats;         // the games that played it (reach the position after it)
    std::optional<ReportGame> first_game;
    // No earlier game reaches its position: as the trees tell (every game within their plies, by any move order), or
    // the position search near their horizon; for the user's game, no other game reaches it, as the position search
    // tells.
    bool confirmed = false;
    // The user's game that left the databases with it: their games at the position before it, none of which played it.
    bool yours = false;
    std::optional<ReportGame> game;
    uint64_t before                             = 0;
    bool operator==(const ReportNovelty&) const = default;
};

struct ReportNovelties {
    std::vector<ReportNovelty> moves;  // the most recent first; the user's last
};

struct ReportOptions {
    uint32_t plan_plies    = 30;     // the plies followed after the position: fifteen moves of each side
    uint32_t structure_ply = 40;     // move 20; later positions take the plan's end
    uint64_t least_games   = 50;     // the games a frequency needs
    uint64_t replayed      = 20000;  // the position's games replayed at most: an even sample of more
    double least_share     = 0.10;   // a plan's games, at least, of those replayed
    std::size_t plans      = 3;      // a side's plans, at most
    std::size_t lines      = 3;      // the main lines
    uint32_t novelty_years = 5;      // a novelty is a move first played in the last years of the databases
    StructureNamer structures;       // none: no structures
    PlanNamer plans_named;           // none: the plans by their moves alone
    // Whether a player's name is one of the user's (Settings › Your names); none: no game is theirs.
    std::function<bool(std::string_view name)> user;
    int threads = 0;  // 0: the machine's
};

enum class ReportSection : uint8_t { Moves, Shapes, KeyGames, Novelties, Done };

struct ReportProgress : JobProgress {
    std::atomic<int> section{0};  // ReportSection
};

struct OpeningReport {
    ReportSection made = ReportSection::Moves;  // the sections made so far: those before it
    ReportMoves moves;
    std::vector<ReportLine> lines;
    ReportShapes shapes;
    std::vector<ReportKeyGame> key_games;
    ReportNovelties novelties;
    bool cancelled = false;
    double seconds = 0;
};

// The report of `position` from `sources`, section by section in the background: `section` is told the report so far
// after each one. `cancel` stops it within a tenth of a second, the report then marked cancelled.
[[nodiscard]] OpeningReport makeOpeningReport(std::span<const ReportSource> sources, const ScanBoard& position,
                                              const ReportOptions& options, ReportProgress& progress,
                                              const std::atomic<bool>& cancel,
                                              const std::function<void(const OpeningReport&)>& section = {});

namespace report_detail {

// The moves of `position` in the sources: their trees, or a search of the position; the games through it, the sources
// searched told.
[[nodiscard]] ReportMoves movesAt(std::span<const ReportSource> sources, const ScanBoard& position,
                                  std::vector<std::vector<ReportGame>>* searched_games = nullptr);
// The main lines from the position: the most played moves, each followed by the most played move after it while that
// keeps a fifth of the line's first move's games, and 10 games at least, up to the trees' plies; the first `count` that
// go on so for a move at least.
[[nodiscard]] std::vector<ReportLine> mainLines(std::span<const ReportSource> sources, const ScanBoard& position,
                                                const ReportMoves& moves, std::size_t count);

}  // namespace report_detail

}  // namespace fcg::study
