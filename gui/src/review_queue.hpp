#pragma once

// The day's review (#80, mockup M12, board 1, point 2): one queue for what every kind of training has due today, each
// with its spaced repetition (FSRS) and its own page. One count for the Home page and the Training page ("35 to
// review"), "Review all" taking the kinds one after the other in the order below, the time it should take, what comes
// back tomorrow and within three days. A kind plugs in through TrainingKind (training_kind.hpp): its counts, the steps
// "Review all" takes for it and the session of each; the Training page holds the queue (training_page.hpp): the
// mistakes of the user's games (#81), the repertoires' positions (#76), the puzzles missed (#80), the endgames missed
// (#82).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fcg {

// In the order "Review all" takes them.
enum class ReviewKind { Mistakes, Repertoire, Puzzles, Endgames };
[[nodiscard]] const std::vector<ReviewKind>& reviewKinds();

struct ReviewCount {
    ReviewKind kind        = ReviewKind::Repertoire;
    std::size_t due        = 0;  // by the end of the training day
    std::size_t tomorrow   = 0;  // by the end of the next one, today's left out
    std::size_t three_days = 0;  // by the end of the third, today's left out
    int64_t answer_ms      = 0;  // how long an answer takes, on average; 0 when not known
};

struct ReviewDay {
    std::vector<ReviewCount> kinds;  // in reviewKinds()'s order, every kind that has cards
    [[nodiscard]] std::size_t due() const;
    [[nodiscard]] std::size_t tomorrow() const;
    [[nodiscard]] std::size_t threeDays() const;
    // The minutes the cards due should take: each kind's answers at their pace, 10 s an answer when not known; at
    // least one.
    [[nodiscard]] int64_t minutes() const;
    // The kinds with cards due, in order.
    [[nodiscard]] std::vector<ReviewKind> dueKinds() const;
    [[nodiscard]] const ReviewCount* find(ReviewKind kind) const;
};

}  // namespace fcg
