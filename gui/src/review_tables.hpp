#pragma once

// Lichess's tablebase for a game's review (#42, mockup M5, board 4): the positions of seven men or fewer of the game,
// each asked once (the answers kept in the profile, as the Analysis tab keeps them), taught to the tables the review is
// held to: in the endgame, a move is judged by what it does to the exact outcome, not by a score.

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <commentary/review.hpp>
#include <commentary/tablebase.hpp>

#include "lichess_api.hpp"

namespace fcg {

// The outcome Lichess tells, for the side to move, as the tables count it: a win the fifty-move rule spoils is a draw,
// a win only DTZ's rounding makes uncertain a win; none when the tables do not know.
[[nodiscard]] std::optional<commentary::Verdict> verdictOf(lichess::Outcome outcome);

// Teaches `tables` what Lichess said of `pos`: its outcome and, for each move, the outcome for the side that plays it.
// False when the answer tells nothing (an unknown position, a move that is not one here).
bool teachTables(commentary::Tablebases& tables, const commentary::Position& pos, const lichess::Tablebase& answer);

// A position of the game the tables would be taught about: its index in the main line's positions, its FEN.
struct TablePosition {
    std::size_t index = 0;
    std::string fen;
};
// The game's positions of seven men or fewer that `tables` want, in the game's order.
[[nodiscard]] std::vector<TablePosition> tablePositions(const commentary::MainLine& line,
                                                        const commentary::Tablebases& tables);

// Asks Lichess about `positions` one at a time, those the profile kept answers for read from it, and teaches `tables`
// each answer as it comes; `done` counts the positions dealt with. The error that stopped it, empty when it went to the
// end or `cancel` stopped it.
[[nodiscard]] std::string askTables(const std::vector<TablePosition>& positions, const commentary::MainLine& line,
                                    const std::filesystem::path& profile, commentary::Tablebases& tables,
                                    const std::atomic<bool>& cancel, std::atomic<int>& done);

}  // namespace fcg
