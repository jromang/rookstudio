#include "glicko2.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fcg::study::glicko2 {

namespace {

constexpr double SCALE   = 173.7178;
constexpr double EPSILON = 0.000001;

double g(double phi) { return 1.0 / std::sqrt(1.0 + 3.0 * phi * phi / (std::numbers::pi * std::numbers::pi)); }

double e(double mu, double mu_j, double phi_j) { return 1.0 / (1.0 + std::exp(-g(phi_j) * (mu - mu_j))); }

}  // namespace

double expected(const Rating& player, double rating, double deviation) {
    return e((player.rating - 1500) / SCALE, (rating - 1500) / SCALE, deviation / SCALE);
}

Rating update(const Rating& player, std::span<const Game> games, double tau) {
    const double mu = (player.rating - 1500) / SCALE, phi = player.deviation / SCALE, sigma = player.volatility;
    if (games.empty()) {
        Rating out    = player;
        out.deviation = std::sqrt(phi * phi + sigma * sigma) * SCALE;
        return out;
    }
    double v_inverse = 0, sum = 0;
    for (const auto& game : games) {
        const double mu_j = (game.rating - 1500) / SCALE, phi_j = game.deviation / SCALE;
        const double gj = g(phi_j), ej = e(mu, mu_j, phi_j);
        v_inverse += gj * gj * ej * (1 - ej);
        sum += gj * (game.score - ej);
    }
    const double v = 1 / v_inverse, delta = v * sum;

    // The volatility: the Illinois algorithm (a regula falsi that halves the side kept twice).
    const double a = std::log(sigma * sigma);
    const auto f   = [&](double x) {
        const double ex = std::exp(x), d = phi * phi + v + ex;
        return ex * (delta * delta - phi * phi - v - ex) / (2 * d * d) - (x - a) / (tau * tau);
    };
    double big_a = a, big_b = 0;
    if (delta * delta > phi * phi + v) {
        big_b = std::log(delta * delta - phi * phi - v);
    } else {
        int k = 1;
        while (f(a - k * tau) < 0) ++k;
        big_b = a - k * tau;
    }
    double f_a = f(big_a), f_b = f(big_b);
    while (std::abs(big_b - big_a) > EPSILON) {
        const double c = big_a + (big_a - big_b) * f_a / (f_b - f_a), f_c = f(c);
        if (f_c * f_b <= 0) {
            big_a = big_b;
            f_a   = f_b;
        } else {
            f_a /= 2;
        }
        big_b = c;
        f_b   = f_c;
    }
    const double sigma_new = std::exp(big_a / 2);
    const double phi_star  = std::sqrt(phi * phi + sigma_new * sigma_new);
    const double phi_new   = 1 / std::sqrt(1 / (phi_star * phi_star) + 1 / v);
    const double mu_new    = mu + phi_new * phi_new * sum;
    return {mu_new * SCALE + 1500, phi_new * SCALE, sigma_new};
}

Rating rate(const Rating& player, std::span<const Game> games, double tau) {
    auto out      = update(player, games, tau);
    out.deviation = std::clamp(out.deviation, MIN_DEVIATION, MAX_DEVIATION);
    return out;
}

Rating idle(const Rating& player, double periods) {
    if (periods <= 0) return player;
    Rating out       = player;
    const double phi = player.deviation / SCALE, sigma = player.volatility;
    out.deviation = std::min(MAX_DEVIATION, std::sqrt(phi * phi + sigma * sigma * std::max(0.0, periods)) * SCALE);
    return out;
}

}  // namespace fcg::study::glicko2
