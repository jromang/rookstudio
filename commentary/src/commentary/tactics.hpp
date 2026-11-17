#pragma once

// Tactical patterns: forks, pins, skewers, discovered attacks, trapped pieces, mate patterns (the prototype's
// crates/facts/src/tactics.rs).

#include <optional>
#include <utility>
#include <vector>

#include "fact_types.hpp"
#include "position.hpp"

namespace fcg::commentary {

// Attacked, and either undefended or attacked by something cheaper: it cannot stay there.
[[nodiscard]] bool inBadSpot(const Board& board, Square square);
// The piece on `square` hits two or more worthwhile targets, and cannot simply be taken. A king's double attack is not
// called a fork.
[[nodiscard]] std::optional<Fact> fork(const Board& board, Square square);
// Pins to the king held by `color`'s bishops, rooks and queens, when they matter: the pinned piece cannot escape an
// attack, or cannot carry out its own.
[[nodiscard]] std::vector<Fact> pins(const Board& board, Color color);
// The slider on `square` attacks a valuable piece; when it steps aside, the one behind falls.
[[nodiscard]] std::optional<Fact> skewer(const Board& board, Square square);
// The enemy slider the piece on `square` shields one of its own from, and the piece shielded: the first beyond it on
// the slider's line, the king or a piece that could be taken with a gain once the shield is gone.
[[nodiscard]] std::optional<std::pair<Placed, Placed>> shielded(const Board& board, Square square);
// The piece leaving `from` (for `to`) uncovered attacks by `color`'s sliders.
[[nodiscard]] std::vector<Fact> discovered(const Board& board, Square from, Square to, Color color);
// `color`'s pieces, other than pawns and kings, that stand in a bad spot and have nowhere better to go: every move
// lands in a bad spot too, unless it takes something as valuable. Pinned pieces and positions in check are left out:
// those are other stories.
[[nodiscard]] std::vector<Placed> trapped(const Position& pos, Color color);
// The shape of a checkmate, if it has a name worth giving.
[[nodiscard]] std::optional<MatePattern> matePattern(const Position& pos);

}  // namespace fcg::commentary
