// FIDE's tables a crosstable reads (crosstable.hpp): the rating differences of fractional scores and the scoring
// probabilities (B.02 8.1), the development coefficient K (B.02 8.3.3), the tie-breaks' acronyms (C.07) and the orders
// FIDE's own events give them.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

#include "crosstable.hpp"

namespace fcg::study {

namespace {

// B.02 8.1.1: the rating difference of the fractional scores .50 to 1.00, by hundredths (the lower half mirrors it).
constexpr std::array<int, 51> RATING_DIFFERENCES{0,   7,   14,  21,  29,  36,  43,  50,  57,  65,  72,  80,  87,
                                                 95,  102, 110, 117, 125, 133, 141, 149, 158, 166, 175, 184, 193,
                                                 202, 211, 220, 230, 240, 251, 262, 273, 284, 296, 309, 322, 336,
                                                 351, 366, 383, 401, 422, 444, 470, 501, 538, 589, 677, 800};
// B.02 8.1.2: the highest difference of each scoring probability of the higher rated player, .50 to .99 (beyond
// 735: 1.00).
constexpr std::array<int, 50> PROBABILITY_BOUNDS{3,   10,  17,  25,  32,  39,  46,  53,  61,  68,  76,  83,  91,
                                                 98,  106, 113, 121, 129, 137, 145, 153, 162, 170, 179, 188, 197,
                                                 206, 215, 225, 235, 245, 256, 267, 278, 290, 302, 315, 328, 344,
                                                 357, 374, 391, 411, 432, 456, 484, 517, 559, 619, 735};

}  // namespace

int fideRatingDifference(double p) {
    const int hundredths = std::clamp(static_cast<int>(std::lround(p * 100.0 + 1e-7)), 0, 100);
    return hundredths >= 50 ? RATING_DIFFERENCES[static_cast<std::size_t>(hundredths - 50)]
                            : -RATING_DIFFERENCES[static_cast<std::size_t>(50 - hundredths)];
}

double fideExpectedScore(int difference) {
    const int d = std::abs(difference);
    int index   = 50;
    for (std::size_t i = 0; i < PROBABILITY_BOUNDS.size(); ++i)
        if (d <= PROBABILITY_BOUNDS[i]) {
            index = static_cast<int>(i);
            break;
        }
    const double higher = (50 + index) / 100.0;
    return difference >= 0 ? higher : 1.0 - higher;
}

int fideK(int rating, int born, int year, int listed, int games) {
    int k = listed > 0                                                                 ? listed
            : born > 0 && year > 0 && year - born <= 18 && rating > 0 && rating < 2300 ? 40
            : rating >= 2400                                                           ? 10
                                                                                       : 20;
    if (games > 0 && k * games > 700) k = std::max(1, 700 / games);
    return k;
}

// ---------------------------------------------------------------------------------------------------------------------
// Tie-breaks

std::string_view tiebreakCode(Tiebreak t) {
    switch (t) {
        case Tiebreak::DirectEncounter:
            return "DE";
        case Tiebreak::Wins:
            return "WIN";
        case Tiebreak::GamesWon:
            return "WON";
        case Tiebreak::BlackGames:
            return "BPG";
        case Tiebreak::BlackWins:
            return "BWG";
        case Tiebreak::Progressive:
            return "PS";
        case Tiebreak::Buchholz:
            return "BH";
        case Tiebreak::BuchholzCut1:
            return "BH-C1";
        case Tiebreak::BuchholzMedian1:
            return "BH-M1";
        case Tiebreak::SonnebornBerger:
            return "SB";
        case Tiebreak::Koya:
            return "KS";
        case Tiebreak::AverageRating:
            return "ARO";
        case Tiebreak::AverageRatingCut1:
            return "ARO-C1";
        case Tiebreak::Performance:
            return "TPR";
    }
    return "";
}

std::optional<Tiebreak> tiebreakFromCode(std::string_view code) {
    for (std::size_t i = 0; i < TIEBREAKS; ++i)
        if (tiebreakCode(static_cast<Tiebreak>(i)) == code) return static_cast<Tiebreak>(i);
    return std::nullopt;
}

bool ratingTiebreak(Tiebreak t) {
    return t == Tiebreak::AverageRating || t == Tiebreak::AverageRatingCut1 || t == Tiebreak::Performance;
}

std::vector<Tiebreak> fideTiebreaks(TournamentSystem system, bool all_rated) {
    switch (system) {
        case TournamentSystem::RoundRobin:
            return {Tiebreak::SonnebornBerger, Tiebreak::Wins, Tiebreak::DirectEncounter};
        case TournamentSystem::Swiss:
            if (all_rated)
                return {Tiebreak::AverageRatingCut1, Tiebreak::BuchholzCut1, Tiebreak::Buchholz,
                        Tiebreak::DirectEncounter};
            return {Tiebreak::BuchholzCut1, Tiebreak::Buchholz, Tiebreak::DirectEncounter};
        default:
            return {};
    }
}

}  // namespace fcg::study
