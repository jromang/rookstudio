// The searches kept in the profile (searches.json, M1 board 6): written and read back without the window (#117).

#include <doctest/doctest.hpp>

#include <cstdio>
#include <string>

#include <json.hpp>
#include <study/patterns.hpp>

#include "search_fields.hpp"

using namespace fcg;

namespace {

template <std::size_t N>
void put(std::array<char, N>& a, const char* text) {
    std::snprintf(a.data(), a.size(), "%s", text);
}

}  // namespace

TEST_CASE("kept searches: every field written and read back the same, the pattern's men and language apart") {
    KeptSearch k;
    k.name  = "Carlsen's Najdorfs";
    auto& f = k.fields;
    put(f.player, "Carlsen, Magnus");
    f.side = 1;
    put(f.opponent, "Nakamura");
    put(f.event, "Tata Steel");
    put(f.year_from, "2010");
    put(f.year_to, "2026");
    put(f.eco_from, "B90");
    put(f.eco_to, "B99");
    put(f.elo_min, "2700");
    f.result       = '1';
    f.position     = true;  // written with its position
    f.position_fen = "rnbqkb1r/1p2pppp/p2p1n2/8/3NP3/2N5/PPP2PPP/R1BQKB1R w KQkq - 0 6";
    put(f.material[0], "8");
    put(f.material[9], "0-1");
    f.material_plies = 6;
    f.pattern[28]    = 0;             // a white pawn on e4
    f.pattern[36]    = kEmptySquare;  // e5 empty
    f.only_pawns     = {false, true};
    f.damaged        = true;
    const auto read  = study::readPattern("Nf3\nbtm");
    REQUIRE(read.pattern);
    setPattern(f, *read.pattern);  // the language's men go to the board, the rest stays language
    f.pattern[28]          = 0;
    f.pattern[36]          = kEmptySquare;
    f.only_pawns           = {false, true};
    f.route.side           = 1;
    f.route.kind           = 2;
    f.route.squares        = {57, 42, 27};
    f.route.others_between = false;
    f.route.within         = 12;
    f.route.from_board     = true;
    f.route.board_fen      = f.position_fen;
    f.structure.set        = true;
    f.structure.pawns      = {0x0000000010E30000ULL, 0x00E7080000000000ULL};
    f.structure.distance   = 1;
    f.structure.both_sides = false;
    f.structure.side       = 1;
    f.structure.reversed   = true;
    f.structure.plies      = 10;
    f.structure.said       = "after 10…Nf8";

    const auto json = keptSearchJson(k);
    CHECK(json.at("name") == "Carlsen's Najdorfs");
    CHECK(json.at("player") == "Carlsen, Magnus");
    CHECK(json.at("result") == "1");
    CHECK(json.at("pattern").size() == 64);
    CHECK_FALSE(json.at("pattern_text").get<std::string>().empty());
    const auto back = keptSearchFrom(json);
    CHECK(back.name == k.name);
    CHECK(std::string(back.fields.player.data()) == "Carlsen, Magnus");
    CHECK(back.fields.side == 1);
    CHECK(back.fields.result == '1');
    CHECK(back.fields.position);
    CHECK(back.fields.pattern == f.pattern);
    CHECK(back.fields.only_pawns == f.only_pawns);
    CHECK(study::writePattern(fullPattern(back.fields)) == study::writePattern(fullPattern(f)));
    CHECK(back.fields.route.squares == f.route.squares);
    CHECK(back.fields.structure.pawns == f.structure.pawns);
    CHECK(back.fields.structure.taken);
    // And written again, the same JSON.
    CHECK(keptSearchJson(back) == json);
}

TEST_CASE("kept searches: what is missing or out of range reads as the panel's defaults") {
    const auto k =
        keptSearchFrom(nlohmann::json{{"name", "old"}, {"side", 7}, {"route", {{"kind", 9}, {"within", 0}}}});
    CHECK(k.name == "old");
    CHECK(k.fields.side == 2);
    CHECK(k.fields.route.kind == 5);
    CHECK(k.fields.route.within == 1);
    CHECK(k.fields.material_plies == 4);
    CHECK(k.fields.result == 0);
    CHECK_FALSE(k.fields.position);
    CHECK(k.fields.pattern[0] == -1);
    CHECK_FALSE(k.fields.structure.taken);
}
