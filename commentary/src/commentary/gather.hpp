#pragma once

// What a comment on one move may draw on, and the facts of the move sorted by what they tell (the prototype's
// crates/commentary/src/lib.rs).

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "eval.hpp"
#include "fact_types.hpp"
#include "oracle.hpp"
#include "position.hpp"
#include "tablebase.hpp"

namespace fcg::commentary {

// What theory says about the position before a move.
struct Theory {
    enum class Source : uint8_t {
        Surveys,  // theory surveys: their lines, the marks they give and their authors' remarks
        Games,    // the reference base's games: the moves played, without judgment
    };
    // Its moves from the position, the most played first, with the marks (NAGs) they get.
    std::vector<std::pair<Move, std::vector<uint8_t>>> moves;
    // An author's remark on a move: the one played, or else the main move.
    std::optional<std::pair<Move, std::string>> note;
    Source source = Source::Surveys;

    // From the surveys' moves, the most played first, each with its marks and its remark. The note kept is the remark
    // on `played`, or else on their main move when the game leaves their lines.
    [[nodiscard]] static Theory surveys(
        std::vector<std::tuple<Move, std::vector<uint8_t>, std::optional<std::string>>> moves, const Move& played);
    // The moves the reference games played, the most played first.
    [[nodiscard]] static Theory played(const std::vector<Move>& moves);
    // The marks the surveys give `mv`, if they play it.
    [[nodiscard]] const std::vector<uint8_t>* marks(const Move& mv) const;
    // The surveys' main move.
    [[nodiscard]] std::optional<Move> main() const;
};

// Everything a comment on one move may draw on.
struct MoveContext {
    const Position* before = nullptr;
    Move played;
    const MoveReport* report = nullptr;
    // The engine's best line in the position before the move.
    const EngineLine* best = nullptr;
    // The engine's best line after the move: how the opponent answers it.
    const EngineLine* reply = nullptr;
    // The engine's second line in the position before the move: what an only move is the only one against.
    const EngineLine* second = nullptr;
    // Exact verdicts for endgames with few pieces.
    const Tablebases* tables = nullptr;
    // The move that led to `before`, to tell a recapture from a gain.
    std::optional<Move> previous;
    // The opening the game had reached before the move, by name.
    std::optional<std::string> opening;
    // What theory surveys say about the position before the move.
    const Theory* theory = nullptr;
    // The engine, to confirm what the detectors see: threats, trapped pieces, zugzwang.
    const Oracle* oracle = nullptr;
};

// The facts of a move, sorted by what they tell, most telling first.
struct MoveFacts {
    // What the move and the engine's continuation of it achieve.
    std::vector<Fact> achieves;
    // What the opponent's best answer does to it.
    std::vector<Fact> punished_by;
    // What the better move would have achieved.
    std::vector<Fact> missed;
    // For an only move: what the opponent gets against the next best move.
    std::vector<Fact> otherwise;

    // Whether the move is faulted for what it misses, not for what it allows: the better move wins something outright,
    // and the answer to the move played wins nothing.
    [[nodiscard]] bool missedChance() const;
    bool operator==(const MoveFacts&) const = default;
};

[[nodiscard]] MoveFacts gather(const MoveContext& ctx);

// For an only move, the engine's second line: the next best move and how it is answered.
struct Alternative {
    const EngineLine* second;
    Move alt;
    std::span<const Move> rest;
};
[[nodiscard]] std::optional<Alternative> alternative(const MoveContext& ctx);

// Facts weaker than this are not worth a sentence on a move that was fine.
inline constexpr uint32_t WORTH_SAYING = 58;
// Facts this telling are tactics, or what a move does about one.
inline constexpr uint32_t TACTIC = 58;
// Facts this telling change how a game goes: a king laid bare, a passed pawn, the initiative; below them, the smaller
// features of a structure.
inline constexpr uint32_t NOTABLE = 30;

// Whether a fact tells how a move made its refutation possible, rather than the refutation.
[[nodiscard]] bool isCause(const Fact& fact);
// Whether a fact is a small feature of the structure: one a line creates over its first moves, which is the move's
// doing only when the move itself creates it.
[[nodiscard]] bool isFeature(const Fact& fact);
// Whether a fact is something won outright: a mate, material, a promotion, a race to promote, a draw saved or a result
// the tablebases settle.
[[nodiscard]] bool isConcrete(const Fact& fact);
[[nodiscard]] bool isRecapture(std::optional<Move> previous, std::optional<Move> mv);
// Whether the first move of the answer does what `fact` tells, rather than the line it starts: the piece that forks,
// pins or is given is the one that just moved.
[[nodiscard]] bool atOnce(const Fact& fact, const Position& after, const Move& answer);

}  // namespace fcg::commentary
