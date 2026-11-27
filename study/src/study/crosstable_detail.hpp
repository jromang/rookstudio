#pragma once

// The crosstable's own parts (crosstable.hpp), shared by its files: crosstable.cpp (rounds, the shape, the crosstable),
// crosstable_fide.cpp (FIDE's tables, the tie-breaks' codes) and crosstable_list.cpp (a database's tournaments).

#include <cmath>
#include <cstdint>
#include <set>
#include <span>
#include <utility>
#include <vector>

#include "crosstable.hpp"

namespace fcg::study::crosstable {

inline constexpr double EPSILON = 1e-9;

[[nodiscard]] inline bool same(double a, double b) { return std::abs(a - b) < EPSILON; }

// 0.5 rounded up, as C.07 rounds its averages.
[[nodiscard]] inline int roundHalfUp(double v) { return static_cast<int>(std::floor(v + 0.5 + EPSILON)); }

// Two players' pair, whichever is named first.
[[nodiscard]] inline uint64_t pairKey(uint32_t a, uint32_t b) {
    if (a > b) std::swap(a, b);
    return static_cast<uint64_t>(a) << 32 | b;
}

[[nodiscard]] inline bool decisive(char result) { return result == '1' || result == '0'; }

// Each player's first and last round with a game (0 when none).
struct Span {
    int first = 0, last = 0;
};
[[nodiscard]] std::vector<Span> roundSpans(uint32_t players, std::span<const CrossGame> games);

// The games a Swiss of `rounds` rounds expects: in each round, half its players between their first game and their
// last, less those a result set by hand puts alone in that round (a bye, a late join, an absence).
[[nodiscard]] uint32_t swissExpected(uint32_t players, std::span<const CrossGame> games, int rounds,
                                     const std::set<std::pair<uint32_t, int>>& alone = {});

}  // namespace fcg::study::crosstable
