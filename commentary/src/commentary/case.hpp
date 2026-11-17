#pragma once

// A move to comment, as the prototype's training and reference cases keep it (tools/dataset/src/case.rs): what its
// facts are computed from, not the facts themselves, so that better detectors or a new prompt need no new engine
// analysis. One line of JSON each.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <json.hpp>

#include "eval.hpp"
#include "gather.hpp"
#include "oracle.hpp"
#include "position.hpp"

namespace fcg::commentary {

// An engine line, from the position it starts in. Scores are from White's point of view.
struct CaseLine {
    std::vector<std::string> moves;  // UCI
    std::optional<int32_t> cp;
    std::optional<int32_t> mate;
    std::optional<std::array<uint16_t, 3>> wdl;
    uint32_t depth = 0;
};

// The review's verdict on the move.
struct CaseReview {
    std::string phase;  // "opening", "middlegame", "endgame"
    double win_before = 0, win_after = 0, accuracy = 0;
    int32_t cp_loss = 0;
    std::optional<std::string> judgment;  // "inaccuracy", "mistake", "blunder"
    bool played_best = false, only_move = false;
    double only_gap = 0;
    bool key = false, book = false, novelty = false, plan = false;
};

// One question the detectors put to the engine, and its answer: none when it had nothing to say.
struct CaseAnswer {
    std::string fen;
    std::vector<std::string> only;  // UCI, sorted; none for all moves
    std::optional<CaseLine> line;
};

struct Case {
    std::string id;                  // where the move comes from: <file>:<game>:<ply>
    std::vector<std::string> moves;  // from the start, in UCI; the last one is the move to comment
    CaseReview review;
    std::optional<CaseLine> best;    // the engine's best line before the move
    std::optional<CaseLine> reply;   // after it: how the opponent answers
    std::optional<CaseLine> second;  // before it, the second line, for an only move
    std::optional<std::string> human;
    std::optional<std::vector<CaseAnswer>> answers;

    // Throws nlohmann::json::exception on a line that is no case.
    [[nodiscard]] static Case fromJson(const nlohmann::json& j);
};

// A case made whole again, ready for the commentary.
struct Replayed {
    Position before;
    Move played;
    std::optional<Move> previous;
    // The opening the game had reached before the move.
    std::optional<std::string> opening;
    MoveReport report;
    std::optional<EngineLine> best, reply, second;
    // The engine's answers kept in the case, to confirm the facts as when it was asked.
    std::optional<RecordedOracle> oracle;

    // The move's context; the replayed case must outlive it.
    [[nodiscard]] MoveContext context(const Tablebases* tables = nullptr) const;
};

// The positions, moves and lines again, checked for legality; none on an illegal move or a line without a score,
// `error` then saying why. `opening` names the opening the moves before the move reached, when given.
[[nodiscard]] std::optional<Replayed> replay(const Case& c, std::string* error = nullptr,
                                             std::optional<std::string> (*opening)(std::span<const Move>) = nullptr);

}  // namespace fcg::commentary
