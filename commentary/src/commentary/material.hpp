#pragma once

// Material: piece values, balances, what a line wins once the captures settle (the prototype's
// crates/facts/src/material.rs).

#include <cstdint>
#include <optional>
#include <span>

#include "fact_types.hpp"
#include "position.hpp"

namespace fcg::commentary {

// Value in pawns, as players count.
[[nodiscard]] int32_t value(Role role) noexcept;

// `color`'s material minus the opponent's, in pawns.
[[nodiscard]] int32_t balance(const Board& board, Color color) noexcept;

// What `side` has won along the line once the captures settle: the balance after the first ply followed by two quiet
// moves. A line stopping in the middle of an exchange is finished statically, with an exchange evaluation on the last
// capture's square.
[[nodiscard]] int32_t settledSwing(const Position& start, std::span<const Move> line, Color side);

// What `color` has more of on the board, and what the opponent has more of, by kind; none when both have the same.
[[nodiscard]] std::optional<Trade> lead(const Board& board, Color color) noexcept;

// The trade behind settledSwing, for `winner`: known when the line shows its captures settle and no pawn promotes on
// the way.
[[nodiscard]] std::optional<Trade> trade(const Position& start, std::span<const Move> line, Color winner);

// The engine's expected score, in percent, from which a side behind in material is still said to hold the balance.
inline constexpr double HOLDS = 45.0;

// `side` giving up material along the line and left behind once its captures settle, the engine still giving it
// `win` percent, the balance at least: compensation. A deficit the line does not deepen is not its doing. The deficit
// is named when it is one plain thing, such as a pawn, a piece or the exchange.
[[nodiscard]] std::optional<Fact> compensation(const Position& start, std::span<const Move> line, Color side,
                                               double win);

}  // namespace fcg::commentary
