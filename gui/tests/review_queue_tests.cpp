// The day's review (#80): one count for every kind, the time it should take, the kinds in the order "Review all" takes
// them.
#include <doctest/doctest.hpp>

#include "review_queue.hpp"

using namespace fcg;

TEST_CASE("review queue: one count for every kind, in the order Review all takes them") {
    ReviewDay day;
    CHECK(day.due() == 0);
    CHECK(day.minutes() == 1);
    CHECK(day.dueKinds().empty());
    day.kinds = {{ReviewKind::Puzzles, 4, 2, 5, 30000}, {ReviewKind::Repertoire, 31, 19, 38, 6000}};
    CHECK(day.due() == 35);
    CHECK(day.tomorrow() == 21);
    CHECK(day.threeDays() == 43);
    // 31 answers at 6 s and 4 at 30 s: 5.1 minutes.
    CHECK(day.minutes() == 5);
    CHECK(day.dueKinds() == std::vector<ReviewKind>{ReviewKind::Repertoire, ReviewKind::Puzzles});
    REQUIRE(day.find(ReviewKind::Puzzles));
    CHECK(day.find(ReviewKind::Puzzles)->due == 4);
    CHECK_FALSE(day.find(ReviewKind::Endgames));
    // A kind without a pace: ten seconds an answer.
    day.kinds[0].answer_ms = 0;
    CHECK(day.minutes() == 4);  // 186 s + 40 s
    CHECK(reviewKinds().front() == ReviewKind::Mistakes);
}
