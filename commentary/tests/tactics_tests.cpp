// The prototype's crates/facts/src/tactics.rs tests.

#include "helpers.hpp"

#include <commentary/tactics.hpp>

using namespace fcg::commentary;
using test::moves;
using test::pos;

namespace {

Position after(std::string_view fen, std::string_view uci) {
    auto p        = pos(fen);
    const auto mv = moves(p, {uci})[0];
    p.playUnchecked(mv);
    return p;
}

}  // namespace

TEST_CASE("tactics: knight fork") {
    const auto p     = after("r3k3/8/8/1N6/8/8/8/4K3 w - - 0 1", "b5c7");
    const auto found = fork(p.board(), Square::C7);
    REQUIRE(found);
    const auto& f = std::get<fact::Fork>(*found);
    CHECK(f.attacker.role == Role::Knight);
    CHECK(f.targets.size() == 2);
}

TEST_CASE("tactics: pin to the king") {
    const auto p     = after("4k3/8/2n5/8/8/3B4/8/4K3 w - - 0 1", "d3b5");
    const auto found = pins(p.board(), Color::White);
    REQUIRE(found.size() == 1);
    const auto& pin = std::get<fact::Pin>(found[0]);
    CHECK(pin.pinned.role == Role::Knight);
    CHECK(pin.behind.role == Role::King);
}

TEST_CASE("tactics: skewer king and queen") {
    const auto p     = after("4q3/8/8/4k3/8/8/8/R6K w - - 0 1", "a1e1");
    const auto found = skewer(p.board(), Square::E1);
    REQUIRE(found);
    const auto& s = std::get<fact::Skewer>(*found);
    CHECK(s.front.role == Role::King);
    CHECK(s.behind.role == Role::Queen);
}

TEST_CASE("tactics: discovered check") {
    const auto p     = after("4k3/8/8/8/4N3/8/8/4RK2 w - - 0 1", "e4c5");
    const auto found = discovered(p.board(), Square::E4, Square::C5, Color::White);
    REQUIRE(found.size() == 1);
    CHECK(std::get<fact::DiscoveredAttack>(found[0]).target.role == Role::King);
}

TEST_CASE("tactics: trapped bishop") {
    const auto p     = pos("r3k3/B1p5/1p6/8/8/8/8/4K3 b - - 0 1");
    const auto found = trapped(p, Color::White);
    REQUIRE(found.size() == 1);
    CHECK(found[0].square == Square::A7);
}

TEST_CASE("tactics: mate patterns") {
    auto p = after("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1", "a1a8");
    CHECK(matePattern(p) == MatePattern::BackRank);
    p = after("6rk/6pp/8/6N1/8/8/8/6K1 w - - 0 1", "g5f7");
    CHECK(matePattern(p) == MatePattern::Smothered);
}
