#pragma once

// A book of starting positions from Lichess's named openings (#53, mockup M8, board "Moteurs"; chess-openings, CC0,
// already in the program for the openings' names, so nothing is downloaded): the position each named line comes to,
// for an engine tournament, out of the families, the lengths and the number chosen.

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace fcg {

struct NamedOpening {
    std::string eco;   // "B90"
    std::string name;  // "Sicilian Defense: Najdorf Variation"
    std::string pgn;   // "1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6"
};
// The table, read once from the program.
[[nodiscard]] const std::vector<NamedOpening>& namedOpenings();

struct OpeningSuiteOptions {
    std::string families                              = "ABCDE";  // the ECO letters kept
    int min_plies                                     = 8;        // the lines' lengths kept, in plies
    int max_plies                                     = 16;
    bool one_per_name                                 = true;  // one position per name: several lines share one
    std::size_t most                                  = 500;   // when more come, an even spread over the ECO codes
    bool operator==(const OpeningSuiteOptions&) const = default;
};

struct SuitePosition {
    std::string epd;  // the position's four EPD fields, then its opening as c0
    std::string eco, name;
    int plies = 0;
};

// The positions of `all` the options keep, in the ECO order; a position reached by two lines comes once.
[[nodiscard]] std::vector<SuitePosition> openingSuite(const std::vector<NamedOpening>& all,
                                                      const OpeningSuiteOptions& options);
// Writes them as an EPD book, one a line; throws std::runtime_error when the file cannot be written.
void writeOpeningSuite(const std::filesystem::path& path, const std::vector<SuitePosition>& positions);

}  // namespace fcg
