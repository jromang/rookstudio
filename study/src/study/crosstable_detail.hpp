#pragma once

// The crosstable's own parts (crosstable.hpp), shared by its files: crosstable.cpp (rounds, the shape, the steps that
// read the games), crosstable_tiebreaks.cpp (the rounds' categories, C.07's tie-breaks, the rating change, the
// standings), crosstable_fide.cpp (FIDE's tables, the tie-breaks' codes) and crosstable_list.cpp (a database's
// tournaments). A crosstable is made by a CrosstableBuilder, one step after the other, which the tests can run one by
// one.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
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

// The categories of the unplayed rounds of a Swiss (C.07 16.2), and the rounds played.
enum class Category : uint8_t { Played, PairingBye, ForfeitWin, RequestedFollowed, ForfeitLoss, RequestedLast };

// A voluntary unplayed round (C.07 16.2.3 to 16.2.5).
[[nodiscard]] inline bool vur(Category c) {
    return c == Category::RequestedFollowed || c == Category::ForfeitLoss || c == Category::RequestedLast;
}

// A round's contribution to Buchholz and Sonneborn-Berger (C.07 8.1, 9.1, 16.4): its opponent's score, or a dummy's.
struct Contribution {
    double bh = 0, sb = 0, opponent = 0;
    bool vur = false;
};

// The sum of the contributions (Sonneborn-Berger's when `sb`, else Buchholz's) less their `lowest` least significant
// values, a VUR's first when it is as low (C.07 16.5), and their `highest` most significant ones.
[[nodiscard]] double cutSum(std::vector<Contribution> list, bool sb, int lowest, int highest);

// A crosstable made step by step, each step reading what those before it made; makeCrosstable() runs them all, in
// their order.
class CrosstableBuilder {
   public:
    CrosstableBuilder(std::vector<CrossPlayer> players, std::vector<CrossGame> games, const CrossOptions& options);

    // Every step, in its order: the crosstable.
    [[nodiscard]] Crosstable build() &&;

    // The steps, in their order.
    void setHandResults();        // the results set by hand: a missing game's, a round alone's points
    void chooseSystem();          // the shape the games give, the system, the mean Elo, the tie-breaks
    void numberPlayers();         // the rows: the start numbers, by rating then name
    void enterGames();            // each player's games, the forfeits
    void findTwoNames();          // two names of one player: never paired together, their rounds apart
    void enterSwissRounds();      // a Swiss's rounds without a game: byes, games missing, absences
    void enterMissingPairs();     // a round robin's pairs that met fewer times than its cycles: missing, or to come
    void sortEntries();           // each player's games and rounds by round
    void countPoints();           // the points, the results not known, the games played
    void categorise();            // the rounds' categories (C.07 16.2), the scores adjusted for the opponents (16.3)
    void computeTiebreaks();      // each player's performance and tie-breaks
    void computeRatingChanges();  // each player's rating change (B.02 8.3)
    void rank();                  // the standings: the points, then each tie-break in its order (C.07 4.2)

    // The crosstable as the steps run so far made it.
    [[nodiscard]] const Crosstable& table() const noexcept { return t_; }
    // A player's round alone whose points were set by hand (setHandResults): its points.
    [[nodiscard]] const std::map<std::pair<uint32_t, int>, double>& alonePoints() const noexcept {
        return alone_points_;
    }
    // The players that two names of one may be (findTwoNames).
    [[nodiscard]] const std::vector<bool>& doubled() const noexcept { return doubled_; }

   private:
    // A pair's average score for `a` over the games between them that count for a direct encounter; none when they did
    // not meet.
    [[nodiscard]] std::optional<double> encounter(uint32_t a, uint32_t b) const;
    // Direct encounter (C.07 Art. 6) among tied players: the classes it orders them in, first to last; `de` and
    // `applied` its value for each player, and whether it was computed.
    std::vector<std::vector<uint32_t>> directEncounter(const std::vector<uint32_t>& members, std::vector<double>& de,
                                                       std::vector<bool>& applied);
    // The contributions of a player's rounds to Buchholz and Sonneborn-Berger; `unsure` set when an opponent's score
    // reads a result not known.
    [[nodiscard]] std::vector<Contribution> contributions(uint32_t player, bool& unsure) const;
    // The players of a tie, ordered by the tie-breaks from the k-th on, into `classes`.
    void rankTie(std::vector<uint32_t> group, std::size_t k, std::vector<double>& de, std::vector<bool>& applied,
                 std::vector<std::vector<uint32_t>>& classes);

    CrossOptions options_;
    Crosstable t_;
    uint32_t n_ = 0;
    bool swiss_ = false, rr_ = false;  // the system it is made with (chooseSystem)
    std::map<std::pair<uint32_t, int>, double> alone_points_;
    std::vector<bool> doubled_;
    std::vector<double> score_;                      // final (countPoints)
    std::vector<double> adjusted_;                   // for their opponents' tie-breaks (C.07 16.3; categorise)
    std::vector<bool> unsure_;                       // their points read a result not known (countPoints)
    std::vector<std::vector<Category>> categories_;  // by entry (categorise)
};

}  // namespace fcg::study::crosstable
