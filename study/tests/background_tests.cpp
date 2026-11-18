// The study's threads in the background, beside the tournaments' engines.
#include <doctest/doctest.hpp>

#include <atomic>
#include <thread>

#include <study/background.hpp>
#include <study/query.hpp>
#include <study/store.hpp>

#include "test_games.hpp"

using namespace fcg::study;

TEST_CASE("background: a thread that enters it is lowered, and so are the study's threads it starts") {
    bool before = true, lowered = false, marked = false, worker_lowered = false, worker_marked = false;
    std::thread t([&] {
        before = inBackground();
        enterBackground();
        lowered = loweredBySystem();
        marked  = inBackground();
        const Inherited inherited;
        std::thread w([&, inherited] {
            inherited.apply();
            worker_lowered = loweredBySystem();
            worker_marked  = inBackground();
        });
        w.join();
    });
    t.join();
    CHECK_FALSE(before);
    CHECK(lowered);
    CHECK(marked);
    CHECK(worker_lowered);
    CHECK(worker_marked);
    // This thread was left as it was.
    CHECK_FALSE(inBackground());
}

TEST_CASE("background: the study's work runs there as it runs elsewhere") {
    auto db                  = fcg::test::database("background", fcg::test::randomGames(200, 4, 6));
    const auto snap          = db->snapshot();
    std::size_t lowered_view = 0, plain_view = 0;
    std::thread t([&] {
        enterBackground();
        lowered_view = makeView(snap, Filter{}, SortBy::Date, false, 4).ids.size();
    });
    t.join();
    plain_view = makeView(snap, Filter{}, SortBy::Date, false, 4).ids.size();
    CHECK(lowered_view == 200);
    CHECK(plain_view == 200);
}
