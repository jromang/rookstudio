#pragma once

// The Endgames tab of the study Board (#42, mockup M13, board 7, point 20): what the tablebases say of the position
// shown, once seven pieces or fewer are left (eight in the positions of Lichess's op1 tables): the verdict and where it
// comes from, the moves grouped by the outcome they leave the side that plays them, their distances, and each move that
// spoils the outcome with why: its refutation and what the tables then say. In players' words (R10): a cursed win is
// "won, but drawn by the fifty-move rule", DTZ "moves to a capture or a pawn move", DTC "moves to the next capture,
// promotion or mate"; the other programs' names in the tooltips.
//
// The disk's tables first (Syzygy: the verdict and DTZ, no distance to mate), Lichess's for the positions they lack,
// once the user allowed them (mockup M4: for this position, always, or not now), each answer kept in the profile
// (online_cache.hpp), never asked again: only the position the user looks at, the main lines after its moves, one
// request at a time.

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <chess.hpp>

#include "lichess_api.hpp"

namespace fcg {

// A position of Lichess's op1 tables, by their server's own test (lila-tablebase's src/op1.rs): eight pieces, no
// castling rights, a white pawn with a black pawn ahead on its file, and more than a lone pawn on each side. They
// answer it with its verdict and its distance to the next capture, promotion or mate (DTC), asked with `dtc=auxiliary`.
[[nodiscard]] bool op1Position(const chess::Board& board);

// Whether the Endgames tab can speak of a position, or why not.
enum class TablesReach { Yes, TooMany, Castling, Chess960 };
[[nodiscard]] TablesReach tablesReach(const chess::Board& board, bool chess960);

// Where the tab takes a position's answer from: the disk's tables, when they cover it (and no tournament of this
// process holds them); else Lichess's, once the user allowed them (always, or for this position), up to seven pieces
// and op1's eight; the consent card while the user has not decided; nothing once they refused, or beyond any table.
enum class TablesWay { Disk, Lichess, Consent, Refused, Beyond };
[[nodiscard]] TablesWay tablesWay(int pieces, bool op1, int disk_pieces, bool disk_locked, int lichess_setting,
                                  bool once);

// The outcome for one side: of the position for the side to move, of a move for the side that plays it.
enum class TablesGroup { Win, CursedWin, Draw, BlessedLoss, Loss, Unknown };
[[nodiscard]] TablesGroup positionGroup(const lichess::Tablebase& answer);
[[nodiscard]] TablesGroup moveGroup(const lichess::TablebaseMove& move);

// The moves of an answer by the outcome they leave the side to move, the best group first; within a group, the order
// the tables play them (the quickest win, the longest loss: sortTableMoves()). Indices into answer.moves.
struct TablesMoves {
    TablesGroup group = TablesGroup::Unknown;
    std::vector<std::size_t> moves;
};
[[nodiscard]] std::vector<TablesMoves> groupTablesMoves(const lichess::Tablebase& answer);

// A move that leaves the side playing it worse off than the position does: a win thrown away, a draw lost.
[[nodiscard]] bool spoilsOutcome(const lichess::Tablebase& answer, const lichess::TablebaseMove& move);

// The moves to mate a DTM counts, the moves to a capture or a pawn move a DTZ counts (both in plies).
[[nodiscard]] int movesOfPlies(int plies);

// The words, in the interface's language. The position's verdict for the side to move: "White wins", "Draw", "Won for
// White, but drawn by the fifty-move rule"...
[[nodiscard]] std::string tablesVerdict(TablesGroup group, chess::Color side_to_move);
// The same from the tables' own category, a win the fifty-move rule and the game played may spoil said so (op1's
// "maybe").
[[nodiscard]] std::string outcomeVerdict(lichess::Outcome outcome, chess::Color side_to_move);
// The most telling distance an answer has: to mate (DTM, plies), else to a capture or a pawn move (DTZ, plies), else to
// the next capture, promotion or mate (DTC, moves); empty for none, or none left (a draw, a position decided at once).
[[nodiscard]] std::string tablesDistance(std::optional<int> dtm, std::optional<int> dtz, std::optional<int> dtc);
// Its name among the other programs': "DTM", "DTZ", "DTC"; empty for none.
[[nodiscard]] const char* tablesMetric(std::optional<int> dtm, std::optional<int> dtz, std::optional<int> dtc);
// A group's title, given the position's outcome: "Keep the draw", "Throw the win away", "Lose"...
[[nodiscard]] std::string tablesGroupTitle(TablesGroup group, TablesGroup position);
// What the tables say after a move, from the move's own entry, for the side that played it: "Black wins: mate in 19
// moves", "a draw".
[[nodiscard]] std::string afterMoveWords(const lichess::TablebaseMove& move, chess::Color mover);

// Where the answer came from, in words: "your tables on the disk (Syzygy, up to 5 pieces)", "Lichess's 7-piece tables
// (your disk's go up to 5 pieces)".
[[nodiscard]] std::string tablesSource(bool online, int pieces, int disk_pieces);

// An answer as the tab gets it in the background: the profile's kept answer first, else Lichess asked (one request),
// the answer kept when it knows the position (a verdict never changes). `asked` tells whether a request went out.
[[nodiscard]] lichess::Answer<lichess::Tablebase> keptOrAskedTables(const std::filesystem::path& root,
                                                                    const std::string& fen, bool& asked,
                                                                    const std::atomic<bool>* cancel = nullptr);
// The same for the tables' main line from a position (lichess::tablebaseMainline()), kept when it has moves.
[[nodiscard]] lichess::Answer<std::vector<std::string>> keptOrAskedLine(const std::filesystem::path& root,
                                                                        const std::string& fen, bool& asked,
                                                                        const std::atomic<bool>* cancel = nullptr);

}  // namespace fcg
