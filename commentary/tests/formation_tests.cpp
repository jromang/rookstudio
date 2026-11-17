// The prototype's crates/facts/src/formation.rs tests.

#include "helpers.hpp"

#include <commentary/formation.hpp>

using namespace fcg::commentary;
using test::pos;

namespace {

std::vector<std::pair<Formation, Color>> found(std::string_view fen) { return formations(pos(fen).board()); }

using Found = std::vector<std::pair<Formation, Color>>;

Square sq(std::string_view name) { return square(static_cast<File>(name[0] - 'a'), static_cast<Rank>(name[1] - '1')); }

}  // namespace

TEST_CASE("formation: the formations of the books") {
    // The exchange variation of the Queen's Gambit Declined.
    CHECK(found("r1bq1rk1/pp1nbppp/2p2n2/3p2B1/3P4/2NBPN2/PPQ2PPP/R3K2R w KQ - 0 10") ==
          Found{{Formation::Carlsbad, Color::White}});
    // The exchange Caro-Kann: the same with colours reversed, Black's minority.
    CHECK(found("rnbqkbnr/pp2pppp/8/3p4/3P4/8/PPP2PPP/RNBQKBNR w KQkq - 0 4") ==
          Found{{Formation::Carlsbad, Color::Black}});
    // The accelerated Dragon against c4 and e4.
    CHECK(found("r1bqkb1r/pp1ppp1p/2n2np1/8/2PNP3/8/PP3PPP/RNBQKB1R w KQkq - 0 6") ==
          Found{{Formation::Maroczy, Color::White}});
    CHECK(found("rn1q1rk1/1b1nbppp/pp1ppn2/8/2PQP3/2N2NP1/PP3PBP/R1BR2K1 w - - 0 11") ==
          Found{{Formation::Hedgehog, Color::Black}});
    CHECK(found("rnbq1rk1/pp4pp/2pbpn2/3p1p2/2PP4/5NP1/PP2PPBP/RNBQ1RK1 w - - 0 8") ==
          Found{{Formation::Stonewall, Color::Black}});
    // The French advance, then the King's Indian with the centre closed.
    CHECK(found("r1bqkbnr/pp3ppp/2n1p3/2ppP3/3P4/2P2N2/PP3PPP/RNBQKB1R b KQkq - 0 5") ==
          Found{{Formation::KingsideChain, Color::White}});
    CHECK(found("r1bq1rk1/ppp1npbp/3p1np1/3Pp3/2P1P3/2N2N2/PP2BPPP/R1BQ1RK1 w - - 0 9") ==
          Found{{Formation::QueensideChain, Color::White}});
    CHECK(found("rnbqkb1r/pp3p1p/3p1np1/2pP4/4P3/2N5/PP3PPP/R1BQKBNR w KQkq - 0 7") ==
          Found{{Formation::Benoni, Color::White}});
    CHECK(found("r1bq1rk1/pp2bppp/2n1pn2/8/3P4/2NB1N2/PP3PPP/R1BQ1RK1 w - - 0 10") ==
          Found{{Formation::Isolani, Color::White}});
    CHECK(found("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1").empty());
}

TEST_CASE("formation: a break is told by what it strikes") {
    const auto lev = [](std::string_view fen, std::string_view from, std::string_view to, std::string_view target,
                        Color side) { return lever(pos(fen).board(), sq(from), sq(to), sq(target), side); };
    // The French advance: ...c5 strikes d4, which holds e5; ...f6 strikes e5 itself.
    const auto french = "rnbqkbnr/ppp2ppp/4p3/3pP3/3P4/8/PPP2PPP/RNBQKBNR b KQkq - 0 3";
    CHECK(lev(french, "c7", "c5", "d4", Color::Black) == Lever{Lever::Kind::ChainBase});
    CHECK(lev(french, "f7", "f6", "e5", Color::Black) == Lever{Lever::Kind::ChainHead});
    // The isolated d-pawn goes forward.
    CHECK(lev("r1bq1rk1/pp2bppp/2n1pn2/8/3P4/2NB1N2/PP3PPP/R1BQ1RK1 w - - 0 10", "d4", "d5", "e6", Color::White) ==
          Lever{Lever::Kind::Isolani});
    // ...b5 against the bind.
    CHECK(lev("r1bq1rk1/pp2ppbp/2np1np1/8/2PNP3/2N1B3/PP2BPPP/R2QK2R b KQ - 0 8", "b7", "b5", "c4", Color::Black) ==
          Lever::freeing(Formation::Maroczy));
    // A break that is only a break.
    CHECK_FALSE(lev("rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 2", "c2", "c4", "d5", Color::White));
}
