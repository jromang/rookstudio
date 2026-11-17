// The prototype's crates/facts/src/attack.rs tests.

#include "helpers.hpp"

#include <commentary/attack.hpp>

using namespace fcg::commentary;
using test::pos;

TEST_CASE("attack: exchanges") {
    // A pawn takes a queen: 900.
    auto p = pos("4k3/8/8/3p4/2Q5/8/8/4K3 b - - 0 1");
    CHECK(see(p.board(), Square::C4, Color::Black) == 900);
    // Rook takes a pawn defended by a pawn: loses the exchange, so not worth starting.
    p = pos("4k3/8/3p4/4p3/8/8/8/4RK2 w - - 0 1");
    CHECK(see(p.board(), Square::E5, Color::White) == 0);
    // Knight defended once, attacked twice: the exchanges net a pawn.
    p = pos("4k3/8/3p4/4n3/8/2B2N2/8/4K3 w - - 0 1");
    CHECK(see(p.board(), Square::E5, Color::White) == 100);
    CHECK(hanging(p.board(), Color::Black).size() == 1);
}
