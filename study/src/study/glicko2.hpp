#pragma once

// Glicko-2, Mark Glickman's rating system (as Lichess rates its players and its puzzles), written for RookStudio from
// his description of the algorithm ("Example of the Glicko-2 system", 2013). A rating is a value, its deviation (how
// sure it is: a rating ± twice the deviation holds the true strength 95 times in 100) and its volatility (how much the
// strength moves). A rating period's games give the new rating; time without games widens the deviation.
//
// - On Glicko-2's scale: mu = (r - 1500) / 173.7178, phi = RD / 173.7178.
// - g(phi) = 1 / sqrt(1 + 3 phi^2 / pi^2); E = 1 / (1 + exp(-g(phi_j) (mu - mu_j))).
// - v = 1 / sum(g(phi_j)^2 E_j (1 - E_j)); delta = v sum(g(phi_j) (s_j - E_j)).
// - The new volatility by the Illinois algorithm on f(x) = e^x (delta^2 - phi^2 - v - e^x) / (2 (phi^2 + v + e^x)^2)
//   - (x - ln sigma^2) / tau^2, to within 0.000001.
// - phi* = sqrt(phi^2 + sigma'^2); phi' = 1 / sqrt(1 / phi*^2 + 1 / v); mu' = mu + phi'^2 sum(g(phi_j) (s_j - E_j)).
//
// RookStudio rates the user's offline puzzles with it (#80): each puzzle a game against the puzzle's own rating, won
// when solved without a mistake or a hint.

#include <span>

namespace fcg::study::glicko2 {

struct Rating {
    double rating                        = 1500;
    double deviation                     = 350;
    double volatility                    = 0.06;
    bool operator==(const Rating&) const = default;
};

// A game of the period: the opponent's rating and deviation, and the score (1 a win, 0.5 a draw, 0 a loss).
struct Game {
    double rating    = 1500;
    double deviation = 350;
    double score     = 0;
};

// Lichess's system constant (how much the volatility may move), and the bounds of a deviation: no surer than 45, no
// less sure than a new player's 350.
inline constexpr double TAU           = 0.75;
inline constexpr double MIN_DEVIATION = 45;
inline constexpr double MAX_DEVIATION = 350;
// Rating periods a day, as Lichess counts them for the deviation that grows between games.
inline constexpr double PERIODS_PER_DAY = 0.21436;

// The chance of winning against an opponent: E above.
[[nodiscard]] double expected(const Rating& player, double rating, double deviation);

// The rating after a period of `games` (none: only its deviation grows, sqrt(phi^2 + sigma^2)). Unbounded: the
// formulas as Glickman gives them.
[[nodiscard]] Rating update(const Rating& player, std::span<const Game> games, double tau = TAU);

// The same, its deviation kept within MIN_DEVIATION and MAX_DEVIATION.
[[nodiscard]] Rating rate(const Rating& player, std::span<const Game> games, double tau = TAU);

// The rating after `periods` rating periods without a game: its deviation grown, MAX_DEVIATION at most.
[[nodiscard]] Rating idle(const Rating& player, double periods);

}  // namespace fcg::study::glicko2
