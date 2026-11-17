// The prototype's crates/facts/src/cause.rs tests.

#include "helpers.hpp"

#include <commentary/cause.hpp>

using namespace fcg::commentary;
using test::moves;
using test::pos;

namespace {

Move replyTo(const Position& p, const Move& played, std::string_view uci) {
    auto after = p;
    after.playUnchecked(played);
    return moves(after, {uci})[0];
}

}  // namespace

TEST_CASE("cause: a piece that moves away stops guarding") {
    // The f3-knight guards the e5-pawn against the c6-knight; 1.Nd4 gives it up.
    const auto p     = pos("4k3/8/2n5/4P3/8/5N2/8/4K3 w - - 0 1");
    const auto nd4   = moves(p, {"f3d4"})[0];
    const auto takes = replyTo(p, nd4, "c6e5");
    const auto found = causes(p, nd4, takes);
    REQUIRE(found.size() == 1);
    const auto& u = std::get<fact::Unguards>(found[0]);
    CHECK(u.piece.square == Square::D4);
    CHECK(u.left.square == Square::E5);
    // 1.Ke2 instead: the knight still guards, nothing to tell.
    const auto ke2 = moves(p, {"e1e2"})[0];
    CHECK(causes(p, ke2, takes).empty());
    // Nor does the better move have to do anything about it.
    CHECK(remedies(p, nd4, ke2, takes, std::nullopt).empty());
}

TEST_CASE("cause: a piece already attacked is neglected and could be saved or guarded") {
    // The e5-knight is attacked by the d6-pawn and unguarded.
    auto p            = pos("4k3/8/3p4/4N3/8/8/8/3RK3 w - - 0 1");
    auto ke2          = moves(p, {"e1e2"})[0];
    auto takes        = replyTo(p, ke2, "d6e5");
    const auto knight = *Placed::at(p.board(), Square::E5);
    CHECK(causes(p, ke2, takes) == std::vector<Fact>{Fact{fact::Neglects{knight}}});
    const auto nf3 = moves(p, {"e5f3"})[0];
    CHECK(remedies(p, ke2, nf3, takes, std::nullopt) == std::vector<Fact>{Fact{fact::Saves{knight}}});
    // The retreat itself saves the knight; a move elsewhere, or onto a square where the pawn takes it all the same,
    // saves nothing.
    CHECK(saved(p, nf3) == std::optional<Fact>(Fact{fact::Saves{knight}}));
    CHECK_FALSE(saved(p, ke2));
    const auto p2 = pos("4k3/2p5/3p4/4N3/8/8/8/3RK3 w - - 0 1");
    CHECK(saved(p2, moves(p2, {"e5c6"})[0]) == std::optional<Fact>(Fact{fact::Saves{knight}}));
    CHECK_FALSE(saved(p2, moves(p2, {"e5d7"})[0]));
    // A guard does not save a knight from a pawn: no remedy to name.
    CHECK(remedies(p, ke2, moves(p, {"d1d5"})[0], takes, std::nullopt).empty());
    // Against a bishop it does.
    p                  = pos("4k3/6b1/8/4N3/8/8/8/3RK3 w - - 0 1");
    ke2                = moves(p, {"e1e2"})[0];
    takes              = replyTo(p, ke2, "g7e5");
    const auto guarded = *Placed::at(p.board(), Square::E5);
    CHECK(remedies(p, ke2, moves(p, {"d1d5"})[0], takes, std::nullopt) ==
          std::vector<Fact>{Fact{fact::Guards{guarded}}});
}

TEST_CASE("cause: a move opens the line of the answer") {
    // 1.f3 opens the e1-h4 diagonal: 1...Qh4+.
    const auto p     = pos("rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2");
    const auto f3    = moves(p, {"f2f3"})[0];
    const auto found = causes(p, f3, replyTo(p, f3, "d8h4"));
    REQUIRE(found.size() == 1);
    const auto& o = std::get<fact::OpensLine>(found[0]);
    CHECK(o.line == LineKind::Diagonal);
    CHECK(o.slider.role == Role::Queen);
    CHECK(o.slider.square == Square::D8);
}

TEST_CASE("cause: taking with the other piece and covering a square") {
    // Both the king and the bishop can take on f8.
    auto p           = pos("5Nk1/8/7b/8/8/8/8/4K3 b - - 0 1");
    const auto kxf8  = moves(p, {"g8f8"})[0];
    const auto found = remedies(p, kxf8, moves(p, {"h6f8"})[0], replyTo(p, kxf8, "e1e2"), std::nullopt);
    REQUIRE(found.size() == 1);
    const auto& t = std::get<fact::TakesWith>(found[0]);
    CHECK(t.by.role == Role::Bishop);
    CHECK_FALSE(t.back);
    // The rook enters on e2 unless the knight covers it from c1.
    p              = pos("4r1k1/8/8/8/8/8/N7/6K1 w - - 0 1");
    const auto kh1 = moves(p, {"g1h1"})[0];
    CHECK(remedies(p, kh1, moves(p, {"a2c1"})[0], replyTo(p, kh1, "e8e2"), std::nullopt) ==
          std::vector<Fact>{Fact{fact::Covers{Color::White, Square::E2}}});
}
