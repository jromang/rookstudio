#pragma once

// The named openings: Lichess's table (chess-openings, CC0), each named position found by its Polyglot key, each ECO
// code's shortest line. Read by the dossier (dossier.hpp), the opening report and the Board page.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fcg::study {

// A line of Lichess's named openings (chess-openings, CC0).
struct NamedOpening {
    std::string eco;                 // "B90"
    std::string name;                // "Sicilian Defense: Najdorf Variation"
    std::vector<std::string> moves;  // its line in SAN, from the start
};

// The named openings, each named position found by its Polyglot key (a transposition finds its name too), and each
// ECO code's shortest line.
class OpeningNames {
   public:
    OpeningNames() = default;
    // The table's text: a header line, then a line an opening, "eco<TAB>name<TAB>pgn" ("1. e4 c5 2. Nf3"). A line whose
    // moves do not replay is left out.
    explicit OpeningNames(std::string_view tsv);

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const NamedOpening& operator[](std::size_t i) const { return entries_[i]; }
    // The opening named at a position: the first line the table gives it; none for a position without a name.
    [[nodiscard]] std::optional<std::size_t> at(uint64_t key) const;
    // An ECO code's shortest line (its moves the fewest, its name the most general); none for a code it does not have.
    [[nodiscard]] std::optional<std::size_t> shortest(std::string_view eco) const;

   private:
    std::vector<NamedOpening> entries_;
    std::unordered_map<uint64_t, uint32_t> by_key_;
    std::unordered_map<std::string, uint32_t> by_eco_;
};

// An opening's family: its name up to the first comma ("Sicilian Defense: Najdorf Variation" for "Sicilian Defense:
// Najdorf Variation, English Attack").
[[nodiscard]] std::string_view openingFamily(std::string_view name);

}  // namespace fcg::study
