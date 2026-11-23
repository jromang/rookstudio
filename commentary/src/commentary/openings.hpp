#pragma once

// Opening names, from Lichess's chess-openings data set (CC0), looked up by position so that a transposition finds its
// name too (the prototype's crates/analysis/src/openings.rs).

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "position.hpp"

namespace fcg::commentary {

// The opening reached by `moves` from the usual starting position: the name of the last named position on the way.
[[nodiscard]] std::optional<std::string> openingName(std::span<const Move> moves);

// The same with its ECO code ("B92", "Sicilian Defense: Najdorf Variation, Opočenský Variation").
struct OpeningCode {
    std::string eco;
    std::string name;
};
[[nodiscard]] std::optional<OpeningCode> openingCode(std::span<const Move> moves);

// The table itself, as the data set writes it: a header line, then "eco<TAB>name<TAB>pgn" a line.
[[nodiscard]] std::string_view openingsTable();

// The name an ECO code goes by ("D36": "Queen's Gambit Declined: Exchange Variation"): the name of its shortest line,
// empty for a code the table does not have.
[[nodiscard]] std::string ecoName(std::string_view eco);

}  // namespace fcg::commentary
