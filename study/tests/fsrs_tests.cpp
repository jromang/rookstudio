#include <doctest/doctest.hpp>

#include <cmath>
#include <set>
#include <vector>

#include <study/fsrs.hpp>

using namespace fcg::study::fsrs;

namespace {

constexpr int64_t START = 1669725000;  // 2022-11-29 12:30 UTC, the reference tests' start

Settings withoutFuzz(Settings s = {}) {
    s.fuzz = false;
    return s;
}

}  // namespace

TEST_CASE("fsrs: the published intervals of a card answered Good six times, Again twice, then Good") {
    // py-fsrs 6's test_review_card, with FSRS-6's default parameters and steps (1 and 10 minutes, then 10 minutes),
    // each review when the card is due.
    const Scheduler scheduler(withoutFuzz());
    const std::vector<Rating> ratings = {Rating::Good, Rating::Good,  Rating::Good,  Rating::Good, Rating::Good,
                                         Rating::Good, Rating::Again, Rating::Again, Rating::Good, Rating::Good,
                                         Rating::Good, Rating::Good,  Rating::Good};
    Card card;
    int64_t now = START;
    std::vector<int64_t> intervals;
    for (const auto rating : ratings) {
        card = scheduler.review(card, rating, now);
        intervals.push_back((card.due - card.last) / DAY);
        now = card.due;
    }
    CHECK(intervals == std::vector<int64_t>{0, 2, 11, 46, 163, 498, 0, 0, 2, 4, 7, 12, 21});
    CHECK(card.state == State::Review);
    CHECK(card.reps == 13);
    CHECK(card.lapses == 1);
}

TEST_CASE("fsrs: the memory after reviews on given days") {
    // py-fsrs 6's test_memo_state: Again then Good five times, 0, 0, 1, 3, 8 and 21 days apart.
    const Scheduler scheduler;
    const std::vector<Rating> ratings = {Rating::Again, Rating::Good, Rating::Good,
                                         Rating::Good,  Rating::Good, Rating::Good};
    const std::vector<int> gaps       = {0, 0, 1, 3, 8, 21};
    Card card;
    int64_t now = START;
    for (std::size_t i = 0; i < ratings.size(); ++i) {
        now += gaps[i] * DAY;
        card = scheduler.review(card, ratings[i], now);
    }
    CHECK(card.stability == doctest::Approx(53.62691).epsilon(1e-6));
    CHECK(card.difficulty == doctest::Approx(6.3574867).epsilon(1e-6));
}

TEST_CASE("fsrs: the formulas: the first review, the curve, the interval, the difficulty") {
    const Scheduler scheduler(withoutFuzz());
    CHECK(scheduler.initialStability(Rating::Again) == doctest::Approx(0.212));
    CHECK(scheduler.initialStability(Rating::Good) == doctest::Approx(2.3065));
    CHECK(scheduler.initialStability(Rating::Easy) == doctest::Approx(8.2956));
    // D0(G) = w4 - e^(w5 (G - 1)) + 1, kept within 1 and 10.
    CHECK(scheduler.initialDifficulty(Rating::Good) == doctest::Approx(6.4133 - std::exp(0.8334 * 2) + 1));
    CHECK(scheduler.initialDifficulty(Rating::Easy) == doctest::Approx(1.0));
    CHECK(scheduler.initialDifficulty(Rating::Easy, false) < 0);
    CHECK(scheduler.initialDifficulty(Rating::Again) == doctest::Approx(6.4133));
    // A card is recalled with a 90 % chance after its stability's days; the interval for 90 % is its stability.
    Card card;
    card.stability = 10;
    card.last      = START;
    CHECK(scheduler.retrievability(card, START + 10 * DAY) == doctest::Approx(0.9));
    CHECK(scheduler.retrievability(card, START + 9 * DAY + DAY / 2) > 0.9);  // whole days: still 9
    CHECK(scheduler.retrievability(card, START + 30 * DAY) < 0.9);
    CHECK(scheduler.retrievability(Card{}, START) == 0);
    CHECK(scheduler.nextInterval(10) == 10);
    CHECK(scheduler.nextInterval(2.3065) == 2);
    CHECK(scheduler.nextInterval(0.2) == 1);
    CHECK(scheduler.nextInterval(1e9) == 36500);
    // A higher retention asks sooner.
    Settings demanding  = withoutFuzz();
    demanding.retention = 0.95;
    CHECK(Scheduler(demanding).nextInterval(10) < 10);
    // Easy again and again brings the difficulty down to 1; Again raises it.
    Card easy;
    int64_t now = START;
    for (int i = 0; i < 10; ++i) {
        easy = scheduler.review(easy, Rating::Easy, now);
        now += 1;
    }
    CHECK(easy.difficulty == doctest::Approx(1.0));
    CHECK(scheduler.nextDifficulty(5, Rating::Again) > 5);
    CHECK(scheduler.nextDifficulty(5, Rating::Good) ==
          doctest::Approx(5 + 0.001 * (scheduler.initialDifficulty(Rating::Easy, false) - 5)));
    // The same day, Good never lowers the stability; Again does.
    CHECK(scheduler.shortTermStability(5, Rating::Good) >= 5);
    CHECK(scheduler.shortTermStability(5, Rating::Again) < 5);
    // A lapse lowers it, a recall raises it, Hard less than Good, Good less than Easy.
    CHECK(scheduler.forgetStability(5, 10, 0.9) < 10);
    const double hard = scheduler.recallStability(5, 10, 0.9, Rating::Hard);
    const double good = scheduler.recallStability(5, 10, 0.9, Rating::Good);
    const double best = scheduler.recallStability(5, 10, 0.9, Rating::Easy);
    CHECK(10 < hard);
    CHECK(hard < good);
    CHECK(good < best);
}

TEST_CASE("fsrs: the fuzz spreads an interval within its range, the same seed giving the same day") {
    const Scheduler scheduler;
    // py-fsrs 6's ranges.
    CHECK(scheduler.fuzzRange(3) == std::pair{2, 4});
    CHECK(scheduler.fuzzRange(10) == std::pair{8, 12});
    CHECK(scheduler.fuzzRange(30) == std::pair{27, 33});
    CHECK(scheduler.fuzzRange(100) == std::pair{93, 107});
    CHECK(scheduler.fuzzedInterval(2, 7) == 2);  // under 2.5 days, as it is
    std::set<int> seen;
    for (uint64_t seed = 0; seed < 500; ++seed) {
        const int days = scheduler.fuzzedInterval(10, seed);
        CHECK(days >= 8);
        CHECK(days <= 12);
        seen.insert(days);
        CHECK(scheduler.fuzzedInterval(10, seed) == days);
    }
    CHECK(seen.size() == 5);
    // A review is fuzzed once the card is learnt, the same for the same seed.
    Card card;
    card = scheduler.review(card, Rating::Easy, START, 42);
    CHECK(card.state == State::Review);
    const auto again = scheduler.review(Card{}, Rating::Easy, START, 42);
    CHECK(again.due == card.due);
}

TEST_CASE("fsrs: one learning step and one relearning step, as the training uses them") {
    Settings s         = withoutFuzz();
    s.learning_steps   = {600};
    s.relearning_steps = {600};
    const Scheduler scheduler(s);
    // A new card right at once: learnt, back in two days.
    const auto good = scheduler.review(Card{}, Rating::Good, START);
    CHECK(good.state == State::Review);
    CHECK(good.due - START == 2 * DAY);
    // Right with difficulty: the step once more, a little longer.
    const auto hard = scheduler.review(Card{}, Rating::Hard, START);
    CHECK(hard.state == State::Learning);
    CHECK(hard.due - START == 900);
    // Wrong: ten minutes, then right: tomorrow.
    auto wrong = scheduler.review(Card{}, Rating::Again, START);
    CHECK(wrong.state == State::Learning);
    CHECK(wrong.due - START == 600);
    wrong = scheduler.review(wrong, Rating::Good, wrong.due);
    CHECK(wrong.state == State::Review);
    CHECK(wrong.due - wrong.last == DAY);
    // A learnt card forgotten: relearnt in ten minutes, then a day or more.
    auto lapse = scheduler.review(good, Rating::Again, good.due);
    CHECK(lapse.state == State::Relearning);
    CHECK(lapse.due - lapse.last == 600);
    CHECK(lapse.lapses == 1);
    CHECK(lapse.stability < good.stability);
    lapse = scheduler.review(lapse, Rating::Good, lapse.due);
    CHECK(lapse.state == State::Review);
    CHECK(lapse.due - lapse.last >= DAY);
    // Good when due: a longer interval than the one before.
    const auto later = scheduler.review(good, Rating::Good, good.due);
    CHECK(later.due - later.last > 2 * DAY);
}
