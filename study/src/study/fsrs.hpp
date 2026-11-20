#pragma once

// FSRS-6, the Free Spaced Repetition Scheduler (the open algorithm Anki uses), written for RookStudio from its
// published formulas and default parameters (open-spaced-repetition, FSRS-6; py-fsrs 6 is the reference
// implementation its tests are checked against). A card's memory is its stability (the days until the chance of
// recalling it falls to 90 %) and its difficulty (1 to 10); each review, graded Again, Hard, Good or Easy, updates
// them, and the next review comes when the chance of recalling falls to the retention wanted.
//
// - Retrievability after t days: R = (1 + F·t/S)^-w20, F = 0.9^(-1/w20) - 1, so that R(S) = 0.9.
// - The interval for a retention r: I = S/F · (r^(-1/w20) - 1), in whole days (rounded half to even), 1 at least.
// - The first review: S0 = w[G-1]; D0 = w4 - e^(w5·(G-1)) + 1.
// - The difficulty after a review: D + (10 - D)·(-w6·(G-3))/9, pulled back towards D0(Easy) by w7.
// - A review on the day of the one before: S·e^(w17·(G-3+w18))·S^-w19, never lower for Good or Easy.
// - A recall: S·(1 + e^w8·(11-D)·S^-w9·(e^(w10·(1-R)) - 1)·(w15 if Hard)·(w16 if Easy)).
// - A lapse: min(w11·D^-w12·((S+1)^w13 - 1)·e^(w14·(1-R)), S/e^(w17·w18)).
//
// Cards go through learning steps (minutes) before their first interval in days, and through relearning steps after a
// lapse, as py-fsrs' scheduler does; intervals of 3 days or more are spread a little (fuzz) so that cards learnt
// together do not all come back on the same day.

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace fcg::study::fsrs {

enum class Rating : int { Again = 1, Hard = 2, Good = 3, Easy = 4 };
enum class State : int { Learning = 1, Review = 2, Relearning = 3 };

// FSRS-6's default parameters, w0 to w20.
inline constexpr std::array<double, 21> DEFAULT_PARAMETERS{0.212,  1.2931, 2.3065, 8.2956, 6.4133, 0.8334, 3.0194,
                                                           0.001,  1.8722, 0.1666, 0.796,  1.4835, 0.0614, 0.2629,
                                                           1.6483, 0.6014, 1.8729, 0.5425, 0.0912, 0.0658, 0.1542};
inline constexpr double STABILITY_MIN  = 0.001;
inline constexpr double DIFFICULTY_MIN = 1.0;
inline constexpr double DIFFICULTY_MAX = 10.0;
inline constexpr int64_t DAY           = 86400;  // seconds

struct Card {
    State state       = State::Learning;
    int step          = 0;  // in the learning or relearning steps
    double stability  = 0;  // days; 0 until its first review
    double difficulty = 0;  // 1 to 10; 0 until its first review
    int64_t due       = 0;  // seconds since the epoch
    int64_t last      = 0;  // its last review, 0 for none
    int reps          = 0;  // its reviews
    int lapses        = 0;  // the reviews of a learnt card graded Again
    [[nodiscard]] bool fresh() const noexcept { return last == 0; }
    bool operator==(const Card&) const = default;
};

struct Settings {
    std::array<double, 21> w = DEFAULT_PARAMETERS;
    double retention         = 0.9;                // the chance of recalling a card wanted at its review
    std::vector<int64_t> learning_steps{60, 600};  // seconds; py-fsrs' defaults
    std::vector<int64_t> relearning_steps{600};
    int maximum_interval = 36500;  // days
    bool fuzz            = true;
};

class Scheduler {
   public:
    explicit Scheduler(Settings settings = {});

    [[nodiscard]] const Settings& settings() const noexcept { return s_; }
    // The chance of recalling the card at `now` (seconds since the epoch); 0 for a card never reviewed.
    [[nodiscard]] double retrievability(const Card& card, int64_t now) const;
    // The card after a review at `now` graded `rating`: its memory, its state and step, when it is due. `seed` chooses
    // its fuzz (the same seed, the same interval).
    [[nodiscard]] Card review(Card card, Rating rating, int64_t now, uint64_t seed = 0) const;

    // The formulas, for the tests.
    [[nodiscard]] double initialStability(Rating rating) const;
    [[nodiscard]] double initialDifficulty(Rating rating, bool clamp = true) const;
    [[nodiscard]] double nextDifficulty(double difficulty, Rating rating) const;
    [[nodiscard]] double shortTermStability(double stability, Rating rating) const;
    [[nodiscard]] double recallStability(double difficulty, double stability, double retrievability,
                                         Rating rating) const;
    [[nodiscard]] double forgetStability(double difficulty, double stability, double retrievability) const;
    // The interval in days for a stability, for the retention wanted.
    [[nodiscard]] int nextInterval(double stability) const;
    // The days an interval may be spread over, and the one `seed` chooses among them.
    [[nodiscard]] std::pair<int, int> fuzzRange(int days) const;
    [[nodiscard]] int fuzzedInterval(int days, uint64_t seed) const;

   private:
    // The stability after a review a day or more after the one before.
    [[nodiscard]] double nextStability(double difficulty, double stability, double retrievability, Rating rating) const;

    Settings s_;
    double decay_  = 0;  // -w20
    double factor_ = 0;  // 0.9^(1/decay) - 1
};

}  // namespace fcg::study::fsrs
