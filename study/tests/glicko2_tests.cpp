// Glicko-2 (#80): Glickman's own example, a period without games, the deviation's bounds and its growth with time.
#include <doctest/doctest.hpp>

#include <vector>

#include <study/glicko2.hpp>

using namespace fcg::study::glicko2;

TEST_CASE("glicko2: Glickman's example of a rating period") {
    // "Example of the Glicko-2 system", section 3: a player of 1500 (RD 200, volatility 0.06) wins against 1400 (RD
    // 30), loses against 1550 (RD 100) and 1700 (RD 300), with tau 0.5.
    const Rating player{1500, 200, 0.06};
    const std::vector<Game> games = {{1400, 30, 1}, {1550, 100, 0}, {1700, 300, 0}};
    const auto after              = update(player, games, 0.5);
    CHECK(after.rating == doctest::Approx(1464.06).epsilon(0.00001));
    CHECK(after.deviation == doctest::Approx(151.52).epsilon(0.0001));
    CHECK(after.volatility == doctest::Approx(0.05999).epsilon(0.0001));
}

TEST_CASE("glicko2: expected scores, a win and a loss against the same puzzle") {
    const Rating player{1742, 70, 0.06};
    CHECK(expected(player, 1742, 70) == doctest::Approx(0.5));
    CHECK(expected(player, 1612, 75) > 0.6);
    CHECK(expected(player, 1900, 75) < 0.3);
    // Solving a puzzle rated lower gains less than failing it loses.
    const auto won  = rate(player, std::vector<Game>{{1612, 75, 1}});
    const auto lost = rate(player, std::vector<Game>{{1612, 75, 0}});
    CHECK(won.rating > player.rating);
    CHECK(lost.rating < player.rating);
    CHECK(won.rating - player.rating < player.rating - lost.rating);
    // Worked out by hand from the formulas: +8.8 for a win, -18.3 for a loss.
    CHECK(won.rating - player.rating == doctest::Approx(8.8).epsilon(0.05));
    CHECK(player.rating - lost.rating == doctest::Approx(18.3).epsilon(0.05));
}

TEST_CASE("glicko2: a period without games, and the bounds of a deviation") {
    const Rating player{1600, 100, 0.06};
    const auto rested = update(player, std::vector<Game>{});
    CHECK(rested.rating == player.rating);
    CHECK(rested.deviation > player.deviation);
    CHECK(rested.volatility == player.volatility);
    // A new player's first puzzles move the rating a lot; never surer than 45, never less sure than 350.
    const Rating fresh{};
    const auto first = rate(fresh, std::vector<Game>{{1500, 75, 1}});
    CHECK(first.rating - fresh.rating > 100);
    Rating sure{2000, 46, 0.06};
    for (int i = 0; i < 200; ++i) sure = rate(sure, std::vector<Game>{{2000, 45, i % 2 ? 1.0 : 0.0}});
    CHECK(sure.deviation >= MIN_DEVIATION);
    CHECK(rate(Rating{1500, 500, 0.06}, std::vector<Game>{}).deviation == MAX_DEVIATION);
}

TEST_CASE("glicko2: days without a puzzle make the rating less sure, up to a new player's") {
    const Rating player{1800, 60, 0.06};
    CHECK(idle(player, 0).deviation == player.deviation);
    const auto month = idle(player, 30 * PERIODS_PER_DAY);
    CHECK(month.deviation > player.deviation);
    CHECK(month.deviation < 80);
    CHECK(idle(player, 1e9).deviation == MAX_DEVIATION);
    CHECK(month.rating == player.rating);
}
