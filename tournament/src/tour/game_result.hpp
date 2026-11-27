#pragma once

// A finished game as the tournament keeps it: its result, how it ended, where its text is in the tournament's PGN
// files, and its journal line ("game_finished").

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <json.hpp>

#include <types/match_data.hpp>

#include "pgn_file.hpp"

namespace fcg::tour {

// The Termination tag of a game whose result the operator set, and the reason the views give for its end.
inline constexpr const char* OPERATOR_TERMINATION = "adjudicated by the operator";
inline constexpr const char* OPERATOR_REASON      = "Adjudicated by the operator";

// A result set by hand (a "result_set" edit, see Edit::Kind::SetResult): why, and how the game ended as played.
struct ResultSet {
    std::string why;
    std::string played_result;  // "*": the operator ended the game while it was played
    fastchess::MatchTermination played_termination = fastchess::MatchTermination::NORMAL;
    std::string played_reason;
};

// Why the games an engine on a machine lost to a broken connection were voided, before the machine's name.
inline constexpr const char* NETWORK_REASON = "connection to its machine lost";

// A finished, scored game, as kept in the journal.
struct GameResult {
    uint32_t encounter = 0;
    int game           = 0;
    int white          = 0;  // participant ids
    int black          = 0;
    std::string result;  // "1-0", "0-1" or "1/2-1/2"
    fastchess::MatchTermination termination = fastchess::MatchTermination::NORMAL;
    std::string reason;
    uint32_t plies       = 0;
    uint32_t duration_ms = 0;
    PgnSpan pgn;              // in the cutechess-style PGN file (spec.pgnFile())
    PgnSpan pgn_chessbase;    // in the ChessBase one
    int64_t finished_at = 0;  // unix time, milliseconds
    // What fastchess' PV checks found (an illegal PV move, a mate PV that does not mate...): (ply, warning).
    std::vector<std::pair<uint32_t, std::string>> pv_warnings;
    // The opening was unbalanced (unbalanced.hpp), as the tournament's rule tells it when the game ended: +1 for White,
    // -1 for Black, 0 not, or not looked for.
    int unbalanced = 0;

    // No longer counted (replay requested, participant replaced or withdrawn); kept to be shown as such.
    bool voided = false;
    std::string void_reason;

    // Set when the operator set the result by hand: result, termination (ADJUDICATION) and reason (OPERATOR_REASON)
    // are then theirs, and this keeps how the game ended as played. The PGN files keep the game as played:
    // countedPgn() gives its text as it counts.
    std::optional<ResultSet> set;

    [[nodiscard]] uint64_t number() const noexcept { return uint64_t{encounter} * 2 + game; }

    // Score of participant `id` in this game: 1, 0.5 or 0.
    [[nodiscard]] double scoreOf(int id) const noexcept;
};

// The comment a game whose result was set by hand gets before its result (see countedPgn()): the reason, and how it
// ended as played. Empty for another game.
[[nodiscard]] std::string setResultComment(const GameResult& r);

// The PGN text of a game as it counts, from its text in the tournament's PGN files: a game whose result was set by
// hand gets that result, the Termination tag OPERATOR_TERMINATION and setResultComment() before the result; another
// game comes back as it is. What the exports and the replay read, as voided games are left out of them.
[[nodiscard]] std::string countedPgn(std::string text, const GameResult& r);

// A game as its journal line keeps it ("game_finished"), and back; the latter throws on a line that is none.
[[nodiscard]] nlohmann::json gameResultJson(const GameResult& result);
[[nodiscard]] GameResult gameResultFromJson(const nlohmann::json& j);

}  // namespace fcg::tour
