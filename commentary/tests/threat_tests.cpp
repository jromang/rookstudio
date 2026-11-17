// The prototype's crates/facts/src/threat.rs tests.

#include "helpers.hpp"

#include <commentary/threat.hpp>

using namespace fcg::commentary;
using test::moves;
using test::pos;

TEST_CASE("threat: scholar's mate threat") {
    auto p         = pos("r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/8/PPPP1PPP/RNBQK1NR w KQkq - 2 3");
    const auto qh5 = moves(p, {"d1h5"})[0];
    CHECK(threats(p, Color::White).empty());
    p.playUnchecked(qh5);
    const auto found = threats(p, Color::White);
    REQUIRE(found.size() == 1);
    const auto* mate = std::get_if<fact::ThreatensMate>(&found[0]);
    REQUIRE(mate);
    CHECK(mate->mv.to() == Square::F7);
}

TEST_CASE("threat: capture threat") {
    // The rook attacks an undefended knight.
    const auto p     = pos("4k3/8/2n5/8/8/8/8/2R1K3 b - - 0 1");
    const auto found = threats(p, Color::White);
    REQUIRE(found.size() == 1);
    const auto* capture = std::get_if<fact::ThreatensCapture>(&found[0]);
    REQUIRE(capture);
    CHECK(capture->target.role == Role::Knight);
}
