// The views (#43, mockup M6): the three offered, a profile of the spaces read as two views, what settings.json keeps
// read back and made sound.

#include <doctest/doctest.hpp>

#include <json.hpp>

#include "views.hpp"

using namespace fcg::views;

TEST_CASE("views: the pages by their ids, the icons and colours") {
    for (const auto p : allPages()) {
        REQUIRE(pageOf(idOf(p)));
        CHECK(*pageOf(idOf(p)) == p);
    }
    CHECK(idOf(PageId::Tournaments) == "tournaments");
    CHECK_FALSE(pageOf("nowhere"));
    CHECK(iconNames().size() == 12);
    CHECK(palette().size() == 6);
}

TEST_CASE("views: the three offered, and the spaces as two of them") {
    const auto offered = proposed();
    REQUIRE(offered.size() == 3);
    // Tests: the Engines space's pages, in its order, its Board.
    CHECK(offered[0].pages == std::vector<PageId>{PageId::Tournaments, PageId::Engines, PageId::Books, PageId::Board,
                                                  PageId::Suites, PageId::Ratings, PageId::Log, PageId::Settings});
    CHECK_FALSE(offered[0].study_board);
    // Study: the Chess space's, the study's Board.
    CHECK(offered[1].pages == std::vector<PageId>{PageId::Home, PageId::Board, PageId::Databases, PageId::Openings,
                                                  PageId::Training, PageId::Lichess, PageId::Engines,
                                                  PageId::Settings});
    CHECK(offered[1].study_board);
    // Everything: every page.
    CHECK(offered[2].pages.size() == allPages().size());
    for (const auto& v : offered) CHECK(v.holds(PageId::Settings));
    const auto spaces = fromSpaces();
    REQUIRE(spaces.size() == 2);
    CHECK(spaces[0] == offered[0]);
    CHECK(spaces[1] == offered[1]);
}

TEST_CASE("views: kept in settings.json and read back, made sound") {
    auto views      = proposed();
    views[1].name   = "Prep";
    views[1].icon   = "crown";
    views[1].colour = 3;
    const auto read = fromJson(toJson(views));
    REQUIRE(read);
    CHECK(*read == views);

    // What a hand or an older version left: unknown pages out, each once, Settings added, a name, a known icon and
    // colour, three views at most.
    const auto odd  = nlohmann::json::parse(R"([
        {"name": "", "icon": "dragon", "colour": 9, "pages": ["board", "board", "nowhere", 3], "board": "engines"},
        {"name": "B", "pages": ["home"]}, {"name": "C"}, {"name": "D"}])");
    const auto made = fromJson(odd);
    REQUIRE(made);
    REQUIRE(made->size() == 3);
    const auto& first = made->front();
    CHECK_FALSE(first.name.empty());
    CHECK(first.icon == "flask");
    CHECK(first.colour == 5);
    CHECK(first.pages == std::vector<PageId>{PageId::Board, PageId::Settings});
    CHECK_FALSE(first.study_board);
    CHECK((*made)[1].study_board);
    CHECK((*made)[2].pages == std::vector<PageId>{PageId::Settings});
    // Nothing to read.
    CHECK_FALSE(fromJson(nlohmann::json::object()));
    CHECK_FALSE(fromJson(nlohmann::json::array()));
}
