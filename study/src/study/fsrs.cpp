#include "fsrs.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fcg::study::fsrs {

namespace {

// Python's round(): to the nearest, a half to the even neighbour (the default rounding mode of the floating point
// unit, which nearbyint follows).
double roundHalfEven(double x) { return std::nearbyint(x); }

int grade(Rating rating) { return static_cast<int>(rating); }

// A uniform number in [0, 1) from a seed (splitmix64).
double uniform(uint64_t seed) {
    uint64_t z = seed + 0x9E3779B97F4A7C15ull;
    z          = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z          = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<double>(z >> 11) * 0x1.0p-53;
}

struct FuzzRange {
    double start, end, factor;
};
constexpr FuzzRange FUZZ_RANGES[] = {
    {2.5, 7.0, 0.15}, {7.0, 20.0, 0.1}, {20.0, std::numeric_limits<double>::infinity(), 0.05}};

}  // namespace

Scheduler::Scheduler(Settings settings) : s_(std::move(settings)) {
    decay_  = -s_.w[20];
    factor_ = std::pow(0.9, 1.0 / decay_) - 1.0;
}

double Scheduler::retrievability(const Card& card, int64_t now) const {
    if (card.fresh() || card.stability <= 0) return 0;
    const auto elapsed = std::max<int64_t>(0, (now - card.last) / DAY);  // whole days, as py-fsrs counts them
    return std::pow(1.0 + factor_ * static_cast<double>(elapsed) / card.stability, decay_);
}

double Scheduler::initialStability(Rating rating) const {
    return std::max(s_.w[static_cast<std::size_t>(grade(rating) - 1)], STABILITY_MIN);
}

double Scheduler::initialDifficulty(Rating rating, bool clamp) const {
    const double d = s_.w[4] - std::exp(s_.w[5] * (grade(rating) - 1)) + 1.0;
    return clamp ? std::clamp(d, DIFFICULTY_MIN, DIFFICULTY_MAX) : d;
}

double Scheduler::nextDifficulty(double difficulty, Rating rating) const {
    const double delta    = -(s_.w[6] * (grade(rating) - 3));
    const double damped   = difficulty + (10.0 - difficulty) * delta / 9.0;  // linear damping
    const double reverted = s_.w[7] * initialDifficulty(Rating::Easy, false) + (1.0 - s_.w[7]) * damped;
    return std::clamp(reverted, DIFFICULTY_MIN, DIFFICULTY_MAX);
}

double Scheduler::shortTermStability(double stability, Rating rating) const {
    double increase = std::exp(s_.w[17] * (grade(rating) - 3 + s_.w[18])) * std::pow(stability, -s_.w[19]);
    if (rating == Rating::Good || rating == Rating::Easy) increase = std::max(increase, 1.0);
    return std::max(stability * increase, STABILITY_MIN);
}

double Scheduler::recallStability(double difficulty, double stability, double retrievability, Rating rating) const {
    const double hard = rating == Rating::Hard ? s_.w[15] : 1.0;
    const double easy = rating == Rating::Easy ? s_.w[16] : 1.0;
    return stability * (1.0 + std::exp(s_.w[8]) * (11.0 - difficulty) * std::pow(stability, -s_.w[9]) *
                                  (std::exp((1.0 - retrievability) * s_.w[10]) - 1.0) * hard * easy);
}

double Scheduler::forgetStability(double difficulty, double stability, double retrievability) const {
    const double long_term  = s_.w[11] * std::pow(difficulty, -s_.w[12]) * (std::pow(stability + 1.0, s_.w[13]) - 1.0) *
                              std::exp((1.0 - retrievability) * s_.w[14]);
    const double short_term = stability / std::exp(s_.w[17] * s_.w[18]);
    return std::min(long_term, short_term);
}

double Scheduler::nextStability(double difficulty, double stability, double retrievability, Rating rating) const {
    const double s = rating == Rating::Again ? forgetStability(difficulty, stability, retrievability)
                                             : recallStability(difficulty, stability, retrievability, rating);
    return std::max(s, STABILITY_MIN);
}

int Scheduler::nextInterval(double stability) const {
    const double days  = stability / factor_ * (std::pow(s_.retention, 1.0 / decay_) - 1.0);
    const double whole = roundHalfEven(days);
    return static_cast<int>(std::clamp(whole, 1.0, static_cast<double>(s_.maximum_interval)));
}

std::pair<int, int> Scheduler::fuzzRange(int days) const {
    double delta = 1.0;
    for (const auto& r : FUZZ_RANGES)
        delta += r.factor * std::max(std::min(static_cast<double>(days), r.end) - r.start, 0.0);
    int low  = static_cast<int>(roundHalfEven(days - delta));
    int high = static_cast<int>(roundHalfEven(days + delta));
    low      = std::max(2, low);
    high     = std::min(high, s_.maximum_interval);
    low      = std::min(low, high);
    return {low, high};
}

int Scheduler::fuzzedInterval(int days, uint64_t seed) const {
    if (days < 2.5) return days;
    const auto [low, high] = fuzzRange(days);
    const double chosen    = std::floor(uniform(seed) * (high - low + 1) + low);
    return std::min(static_cast<int>(chosen), s_.maximum_interval);
}

Card Scheduler::review(Card card, Rating rating, int64_t now, uint64_t seed) const {
    const bool first = card.fresh() || card.stability <= 0;
    // The days since the review before, whole, as py-fsrs counts them; a review within a day is a short-term one.
    const bool same_day = !first && (now - card.last) / DAY < 1;
    const auto memory   = [&] {
        if (first) {
            card.stability  = initialStability(rating);
            card.difficulty = initialDifficulty(rating);
        } else if (same_day) {
            card.stability  = shortTermStability(card.stability, rating);
            card.difficulty = nextDifficulty(card.difficulty, rating);
        } else {
            card.stability  = nextStability(card.difficulty, card.stability, retrievability(card, now), rating);
            card.difficulty = nextDifficulty(card.difficulty, rating);
        }
    };
    int64_t interval    = 0;  // seconds
    const auto graduate = [&] {
        card.state = State::Review;
        card.step  = 0;
        interval   = static_cast<int64_t>(nextInterval(card.stability)) * DAY;
    };
    // The learning and relearning steps alike: Again back to the first, Hard where it is (the first step a little
    // longer), Good to the next or out of them, Easy out of them.
    const auto stepThrough = [&](const std::vector<int64_t>& steps) {
        if (steps.empty() || (card.step >= static_cast<int>(steps.size()) && rating != Rating::Again)) {
            graduate();
            return;
        }
        const auto at = static_cast<std::size_t>(std::max(0, std::min(card.step, static_cast<int>(steps.size()) - 1)));
        switch (rating) {
            case Rating::Again:
                card.step = 0;
                interval  = steps[0];
                break;
            case Rating::Hard:
                if (card.step == 0 && steps.size() == 1)
                    interval = steps[0] * 3 / 2;
                else if (card.step == 0)
                    interval = (steps[0] + steps[1]) / 2;
                else
                    interval = steps[at];
                break;
            case Rating::Good:
                if (card.step + 1 >= static_cast<int>(steps.size())) {
                    graduate();
                } else {
                    ++card.step;
                    interval = steps[static_cast<std::size_t>(card.step)];
                }
                break;
            case Rating::Easy:
                graduate();
                break;
        }
    };

    switch (card.state) {
        case State::Learning:
            memory();
            stepThrough(s_.learning_steps);
            break;
        case State::Relearning:
            memory();
            stepThrough(s_.relearning_steps);
            break;
        case State::Review:
            if (same_day)
                card.stability = shortTermStability(card.stability, rating);
            else
                card.stability = nextStability(card.difficulty, card.stability, retrievability(card, now), rating);
            card.difficulty = nextDifficulty(card.difficulty, rating);
            if (rating == Rating::Again) {
                ++card.lapses;
                if (s_.relearning_steps.empty()) {
                    interval = static_cast<int64_t>(nextInterval(card.stability)) * DAY;
                } else {
                    card.state = State::Relearning;
                    card.step  = 0;
                    interval   = s_.relearning_steps[0];
                }
            } else {
                interval = static_cast<int64_t>(nextInterval(card.stability)) * DAY;
            }
            break;
    }
    if (card.state == State::Review && s_.fuzz)
        interval = static_cast<int64_t>(fuzzedInterval(static_cast<int>(interval / DAY), seed)) * DAY;
    card.last = now;
    card.due  = now + interval;
    ++card.reps;
    return card;
}

}  // namespace fcg::study::fsrs
