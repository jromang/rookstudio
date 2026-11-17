#pragma once

// The detectors put together: what one move does, and what happens along a line (the prototype's
// crates/facts/src/line.rs). A line from an engine says what works. Along it, a tactic is kept only when the line
// cashes it in, taking one of its targets later or ending in mate: a fork the line ignores is not what makes the line
// work, and saying so would mislead.

#include <optional>
#include <span>
#include <vector>

#include "fact_types.hpp"
#include "oracle.hpp"
#include "position.hpp"

namespace fcg::commentary {

// What one move does on its own: what it delivers, creates, leaves en prise. Unlike lineFacts, nothing here is checked
// against what follows.
[[nodiscard]] std::vector<Fact> moveFacts(const Position& before, const Move& mv);

// What happens along a line: the tactics it cashes in, the material it wins or gives, the mate it ends in.
[[nodiscard]] std::vector<Fact> lineFacts(const Position& start, std::span<const Move> line);

// The same, the engine confirming what the detectors alone could get wrong: the threat of the line's first move, and a
// trapped piece having no way out.
[[nodiscard]] std::vector<Fact> lineFactsWith(const Position& start, std::span<const Move> line, const Oracle* oracle);

// What a line wins once its captures settle, for the side that comes out ahead.
[[nodiscard]] std::optional<Fact> material(const Position& start, std::span<const Move> line);

// The piece `winner`'s gain along the line comes down to, where it stands in `start`: the one piece, rook or queen it
// ends up ahead by, pawns and the exchange aside, when it took no other of that kind on the way.
[[nodiscard]] std::optional<Placed> taken(const Position& start, std::span<const Move> line, Color winner);

// Before taking back what `previous` took, the side starting the line plays a check or a capture elsewhere, and takes
// back later: an in-between move.
[[nodiscard]] std::optional<Fact> intermezzo(std::optional<Move> previous, const Position& start,
                                             std::span<const Move> line);

// The kind of endgame on the board, when it is one both sides share.
[[nodiscard]] std::optional<Ending> ending(const Board& board);

}  // namespace fcg::commentary
