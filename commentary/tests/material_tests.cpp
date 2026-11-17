// The prototype's crates/facts/src/material.rs tests.

#include "helpers.hpp"

#include <commentary/material.hpp>

using namespace fcg::commentary;
using test::moves;
using test::pos;

TEST_CASE("material: swings settle after recaptures") {
    // White takes a knight defended by a pawn: Bxc6 dxc6 is an even trade.
    auto p    = pos("r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4");
    auto line = moves(p, {"b5c6", "d7c6", "e1g1", "f8d6"});
    CHECK(settledSwing(p, line, Color::White) == 0);
    // A free knight is a gain of three.
    p    = pos("4k3/8/2n5/8/8/8/8/2R1K3 w - - 0 1");
    line = moves(p, {"c1c6", "e8d7", "c6c1"});
    CHECK(settledSwing(p, line, Color::White) == 3);
    CHECK(balance(p.board(), Color::Black) == -2);
    // A line ending on a capture that cannot be answered keeps the gain.
    p    = pos("r3k3/8/8/1N6/8/8/8/4K3 w - - 0 1");
    line = moves(p, {"b5c7", "e8d8", "c7a8"});
    CHECK(settledSwing(p, line, Color::White) == 5);
    CHECK(trade(p, line, Color::White) == Trade{{0, 0, 1, 0}, {0, 0, 0, 0}});
}

TEST_CASE("material: a deficit the engine does not mind is compensation") {
    // 1.d5 exd5 2.Kd2 Kd7: the pawn is given, the engine does not mind.
    auto p            = pos("4k3/8/4p3/8/3P4/8/8/4K3 w - - 0 1");
    const auto gambit = moves(p, {"d4d5", "e6d5", "e1d2", "e8d7"});
    fact::Compensation expected{Color::White, 1, Trade{{1, 0, 0, 0}, {0, 0, 0, 0}}};
    CHECK(compensation(p, gambit, Color::White, 50.0) == std::optional<Fact>(Fact{expected}));
    CHECK_FALSE(compensation(p, gambit, Color::White, 30.0));
    // A pawn already missing before the line is not the line's doing.
    p = pos("4k3/7p/8/8/8/8/8/4K3 w - - 0 1");
    CHECK_FALSE(compensation(p, {}, Color::White, 50.0));
}

TEST_CASE("material: trades say what changed hands") {
    // 1.Rxd6 cxd6: a rook for a bishop, the exchange; nobody wins by the count alone.
    auto p    = pos("4k3/2p5/3b4/8/8/8/8/3RK3 w - - 0 1");
    auto line = moves(p, {"d1d6", "c7d6", "e1d2", "e8d7"});
    CHECK(settledSwing(p, line, Color::Black) == 2);
    CHECK(trade(p, line, Color::Black) == Trade{{0, 0, 1, 0}, {0, 1, 0, 0}});
    // A promotion on the way: what changed hands is no longer a trade.
    p    = pos("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1");
    line = moves(p, {"b7b8q", "e8d7", "e1d2"});
    CHECK_FALSE(trade(p, line, Color::White));
}
