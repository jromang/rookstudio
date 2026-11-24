#pragma once

// The engine, asked to confirm what the detectors see: what a side would play if it could move twice in a row, which
// tells its threats and a zugzwang, and whether a trapped piece could be saved after all (the prototype's
// crates/facts/src/oracle.rs). The questions are the same wherever the facts are found: the program asks an engine on
// the spot, the training cases keep its answers (RecordedOracle).

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "fact_types.hpp"
#include "position.hpp"

namespace fcg::commentary {

// An engine's score, for the side to move: centipawns, or moves to mate (negative when it is mated).
struct Score {
    enum class Kind : uint8_t { Cp, Mate };
    Kind kind     = Kind::Cp;
    int32_t value = 0;
    [[nodiscard]] static constexpr Score cp(int32_t v) noexcept { return {Kind::Cp, v}; }
    [[nodiscard]] static constexpr Score mate(int32_t v) noexcept { return {Kind::Mate, v}; }
    bool operator==(const Score&) const = default;
};

// The engine's best line from a position, and its score there.
struct Searched {
    std::vector<Move> moves;
    Score score;
    uint32_t depth                         = 0;  // the depth it reached, 0 when not known (an answer kept)
    bool operator==(const Searched&) const = default;
};

// An engine answering the detectors' questions.
class Oracle {
   public:
    virtual ~Oracle() = default;
    // The engine's best line from `pos`, among the moves `only` when there are some; none when it cannot tell.
    [[nodiscard]] virtual std::optional<Searched> search(const Position& pos, std::span<const Move> only) const = 0;
};

// Answers kept from an engine, by question: the position's FEN (en passant square only when a capture there is legal)
// and the moves the search was kept to, in UCI and sorted. The line is in UCI.
class RecordedOracle : public Oracle {
   public:
    void answer(const Position& pos, std::vector<std::string> only, std::vector<std::string> line, Score score);
    void answer(const std::string& fen, std::vector<std::string> only, std::vector<std::string> line, Score score);
    [[nodiscard]] std::optional<Searched> search(const Position& pos, std::span<const Move> only) const override;
    [[nodiscard]] bool empty() const noexcept { return answers_.empty(); }

   private:
    std::map<std::pair<std::string, std::vector<std::string>>, std::pair<std::vector<std::string>, Score>> answers_;
};

// What `side`, having just moved in `pos`, would play if it could move again: the engine's line from `pos` with the
// turn given back. None in check, where the check is the threat.
[[nodiscard]] std::optional<Searched> movingAgain(const Position& pos, Color side, const Oracle& oracle);

// `side`'s threat in `pos` as the engine sees it, the opponent to move: the mate, or the material, `side` would win if
// it could move again. Empty when the engine finds nothing worth a threat; none when it was not asked or could not
// answer.
[[nodiscard]] std::optional<std::vector<Fact>> engineThreats(const Position& pos, Color side, const Oracle& oracle);

// Whether the engine finds `piece`, trapped in `pos` with its side to move, a way out: its best move among the piece's
// own keeps it, never taken along the line that follows.
[[nodiscard]] bool escapes(const Position& pos, Placed piece, const Oracle& oracle);

// This many points of expected score or more, lost when it is one's turn, mean zugzwang: the difference between holding
// and not holding.
inline constexpr double ZUGZWANG_POINTS = 35.0;

// The opponent of `side`, to move in `pos` after `side`'s move, is in zugzwang: were it `side`'s turn again, `side`
// would stand clearly worse than it does now. `now` is `side`'s expected score in `pos`, and `chances` turns the
// engine's score for `side` into one.
[[nodiscard]] std::optional<Fact> zugzwang(const Position& pos, Color side, double now,
                                           const std::function<double(Score)>& chances, const Oracle& oracle);

}  // namespace fcg::commentary
