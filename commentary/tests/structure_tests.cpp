// The prototype's crates/facts/src/structure.rs tests.

#include "helpers.hpp"

#include <algorithm>
#include <string>

#include <commentary/structure.hpp>

using namespace fcg::commentary;
using test::moves;
using test::pos;

namespace {

bool has(const std::vector<Fact>& facts, const Fact& fact) {
    return std::find(facts.begin(), facts.end(), fact) != facts.end();
}

template <typename T>
bool any(const std::vector<Fact>& facts) {
    return std::any_of(facts.begin(), facts.end(), [](const Fact& f) { return std::holds_alternative<T>(f); });
}

}  // namespace

TEST_CASE("structure: pawn structure") {
    // White: a passed, isolated d-pawn and doubled f-pawns. Black's h-pawn is not passed: White's g-pawn stands in
    // front of it on the next file.
    const auto facts = positionFacts(pos("4k3/7p/8/3P4/8/5P2/5PP1/4K3 w - - 0 1"));
    const auto d5    = placed(Color::White, Role::Pawn, Square::D5);
    CHECK(has(facts, Fact{fact::PassedPawn{d5}}));
    CHECK(has(facts, Fact{fact::IsolatedPawn{d5}}));
    CHECK(has(facts, Fact{fact::DoubledPawns{Color::White, File::F}}));
    CHECK_FALSE(has(facts, Fact{fact::PassedPawn{placed(Color::Black, Role::Pawn, Square::H7)}}));
}

TEST_CASE("structure: rooks, outposts and kings") {
    const auto facts = positionFacts(pos("6k1/R5pp/8/4N3/3P4/8/5PPP/2R1Q1K1 w - - 0 1"));
    const auto white = [](Role role, Square square) { return placed(Color::White, role, square); };
    CHECK(has(facts, Fact{fact::RookOnOpenFile{white(Role::Rook, Square::C1)}}));
    CHECK(has(facts, Fact{fact::RookOnSeventh{white(Role::Rook, Square::A7)}}));
    CHECK(has(facts, Fact{fact::Outpost{white(Role::Knight, Square::E5)}}));
    const auto exposed = [](const std::string& fen, Color color) {
        const auto found = positionFacts(pos(fen));
        return std::any_of(found.begin(), found.end(), [&](const Fact& f) {
            const auto* e = std::get_if<fact::ExposedKing>(&f);
            return e && e->king.color == color;
        });
    };
    CHECK_FALSE(exposed("6k1/R5pp/8/4N3/3P4/8/5PPP/2R1Q1K1 w - - 0 1", Color::Black));
    // Without its pawns, against a queen, the king is exposed; without a queen, it is not.
    CHECK(exposed("6k1/R7/8/4N3/3P4/8/5PPP/2R1Q1K1 w - - 0 1", Color::Black));
    CHECK_FALSE(exposed("6k1/R7/8/4N3/3P4/8/5PPP/2R3K1 w - - 0 1", Color::Black));
    CHECK(exposed("q5k1/8/8/8/8/8/8/2R1Q1K1 w - - 0 1", Color::White));
    // In the centre after 1.e4 e5 2.d4: no shelter to speak of while it can castle.
    const std::string centre = "rnbqkbnr/pppp1ppp/8/4p3/3PP3/8/PPP2PPP/RNBQKBNR b";
    CHECK_FALSE(exposed(centre + " KQkq - 0 2", Color::White));
    CHECK(exposed(centre + " kq - 0 2", Color::White));
    // An f7-pawn could chase the knight away: no outpost.
    CHECK_FALSE(any<fact::Outpost>(positionFacts(pos("6k1/R4ppp/8/4N3/3P4/8/5PPP/2R1Q1K1 w - - 0 1"))));
}

TEST_CASE("structure: weak pawns, centres, bishops, kings and development") {
    const auto found = [](std::string_view fen) { return positionFacts(pos(fen)); };
    // The d6-pawn has no neighbour left behind it, and c4 and e4 hold d5.
    const auto sicilian = found("4k3/8/3p4/4p3/2P1P3/8/8/4K3 w - - 0 1");
    CHECK(has(sicilian, Fact{fact::BackwardPawn{placed(Color::Black, Role::Pawn, Square::D6)}}));
    CHECK_FALSE(has(sicilian, Fact{fact::BackwardPawn{placed(Color::Black, Role::Pawn, Square::E5)}}));
    CHECK(has(found("4k3/pp3ppp/8/8/2PP4/8/P4PPP/4K3 w - - 0 1"), Fact{fact::HangingPawns{Color::White, File::C}}));
    CHECK(
        has(found("rnbqkbnr/ppp2ppp/8/8/3PP3/8/PPP2PPP/RNBQKBNR b KQkq - 0 3"), Fact{fact::PawnCentre{Color::White}}));
    // The French advance: d4, e5 and c3 fixed on the dark squares of the c1-bishop.
    CHECK(has(found("4k3/8/4p3/3pP3/2pP4/2P5/8/2B1K3 w - - 0 1"),
              Fact{fact::BadBishop{placed(Color::White, Role::Bishop, Square::C1)}}));
    const auto kings = found("8/8/8/4k3/8/8/4K3/8 w - - 0 1");
    CHECK(has(kings, Fact{fact::ActiveKing{placed(Color::Black, Role::King, Square::E5)}}));
    CHECK_FALSE(std::any_of(kings.begin(), kings.end(), [](const Fact& f) {
        const auto* k = std::get_if<fact::ActiveKing>(&f);
        return k && k->king.color == Color::White;
    }));
    // Four white pieces out against one black knight.
    const auto ahead = found("r1bqkbnr/pppp1ppp/2n5/4p1B1/2B1P3/2N2N2/PPPP1PPP/R2QK2R b KQkq - 0 5");
    CHECK(has(ahead, Fact{fact::DevelopmentLead{Color::White}}));
    CHECK_FALSE(has(ahead, Fact{fact::DevelopmentLead{Color::Black}}));
}

TEST_CASE("structure: holes and attacks on the king") {
    const auto hole = [](std::string_view fen, Square square) {
        return has(positionFacts(pos(fen)), Fact{fact::Hole{Color::White, square}});
    };
    // The knight on c5 eyes e4, which no white pawn can guard; f3 would.
    CHECK(hole("3qk3/8/8/2n5/8/8/8/3QK3 w - - 0 1", Square::E4));
    CHECK_FALSE(hole("3qk3/8/8/2n5/8/5P2/8/3QK3 w - - 0 1", Square::E4));
    CHECK_FALSE(hole("4k3/8/8/2n5/8/8/8/4K3 w - - 0 1", Square::E4));
    // Queen, bishop and knight aimed at h7, nothing to defend it.
    CHECK(has(positionFacts(pos("6k1/5ppp/8/6NQ/8/3B4/8/6K1 w - - 0 1")), Fact{fact::KingAttack{Color::White}}));
    // The b2-bishop sees the whole long diagonal; a pawn on d4 shuts it.
    const Fact b2{fact::LongDiagonal{placed(Color::White, Role::Bishop, Square::B2)}};
    CHECK(has(positionFacts(pos("4k3/8/8/8/8/8/1B6/4K3 w - - 0 1")), b2));
    CHECK_FALSE(has(positionFacts(pos("4k3/8/8/8/3P4/8/1B6/4K3 w - - 0 1")), b2));
    // The white queen roams, the black one is walled in by its own king and pawns.
    const auto boxed = positionFacts(pos("6kq/6pp/8/8/3Q4/8/6PP/6K1 w - - 0 1"));
    CHECK(has(boxed, Fact{fact::MoreActive{Color::White}}));
    CHECK_FALSE(has(boxed, Fact{fact::MoreActive{Color::Black}}));
}

TEST_CASE("structure: changes are what the line creates") {
    // 1...Bxf3 2.gxf3 doubles White's f-pawns and leaves the h-pawn isolated.
    const auto start   = pos("rn1qkbnr/ppp1pppp/8/3p4/6b1/5N1P/PPPPPPP1/RNBQKB1R b KQkq - 0 3");
    const auto changes = structureChanges(start, moves(start, {"g4f3", "g2f3"}));
    CHECK(has(changes, Fact{fact::DoubledPawns{Color::White, File::F}}));
    CHECK(has(changes, Fact{fact::IsolatedPawn{placed(Color::White, Role::Pawn, Square::H3)}}));
    CHECK(has(changes, Fact{fact::BishopPair{Color::White}}));
    // A pawn that was passed and moves on is no new passed pawn.
    const auto endgame = pos("4k3/8/8/3P4/8/8/8/4K3 w - - 0 1");
    CHECK(structureChanges(endgame, moves(endgame, {"d5d6"})).empty());
}

TEST_CASE("structure: a king walk loses castling") {
    const auto start = pos("r3k2r/pppqpppp/8/8/8/8/PPPQPPPP/R3K2R w KQkq - 0 1");
    CHECK(has(structureChanges(start, moves(start, {"e1d1"})), Fact{fact::LosesCastling{Color::White}}));
    CHECK_FALSE(any<fact::LosesCastling>(structureChanges(start, moves(start, {"e1g1"}))));
}

TEST_CASE("structure: space is pawns further up and room behind them") {
    const auto space = [](std::string_view fen, Color side) { return hasSpace(pos(fen).board(), side); };
    // The French advance: the e5-pawn, and room behind the chain.
    const auto french = "r1bqkbnr/pp3ppp/2n1p3/2ppP3/3P4/2P2N2/PP3PPP/RNBQKB1R b KQkq - 0 5";
    CHECK(space(french, Color::White));
    CHECK_FALSE(space(french, Color::Black));
    // The Maroczy bind: c4 and e4 against pawns kept back.
    CHECK(space("r1bq1rk1/pp2ppbp/2np1np1/8/2PNP3/2N1B3/PP2BPPP/R2QK2R b KQ - 0 8", Color::White));
    // After 1.d4 Nf6 2.c4 Black has not shown its pawns yet.
    CHECK_FALSE(space("rnbqkb1r/pppppppp/5n2/8/2PP4/8/PP2PPPP/RNBQKBNR b KQkq - 0 2", Color::White));
    // Pawns face to face in the centre: neither side has more.
    const auto open = "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3";
    CHECK_FALSE(space(open, Color::White));
    CHECK_FALSE(space(open, Color::Black));
    // The same pawns as in the French, in an endgame: no pieces to be short of room.
    CHECK_FALSE(space("4k3/pp3ppp/4p3/2ppP3/3P4/2P5/PP3PPP/4K3 w - - 0 1", Color::White));
}

TEST_CASE("structure: majorities and weak colours") {
    // Three pawns against two on the queen's wing; doubled, they make no passed pawn.
    const auto healthy = pos("4k3/pp2pppp/8/8/8/8/PPP2PPP/4K3 w - - 0 1");
    CHECK(majorities(healthy.board(), Color::White) == std::vector<Wing>{Wing::Queenside});
    CHECK(majorities(healthy.board(), Color::Black).empty());
    CHECK(majorities(pos("4k3/pp2pppp/8/8/8/2P5/P1P2PPP/4K3 w - - 0 1").board(), Color::White).empty());
    // A pawn up is not a majority.
    CHECK(majorities(pos("4k3/pp3ppp/8/8/8/8/PPP2PPP/4K3 w - - 0 1").board(), Color::White).empty());
    // Black has traded its dark-squared bishop and keeps its pawns on light squares.
    CHECK(weakColour(pos("r2q1rk1/1b3p1p/p3p1p1/1p1p4/3P4/2N1PN2/PP3PPP/R2Q1RK1 w - - 0 1").board(), Color::Black) ==
          true);
    // With the bishop on g7 the dark squares are held.
    CHECK_FALSE(
        weakColour(pos("r2q1rk1/1b3pbp/p3p1p1/1p1p4/3P4/2N1PN2/PP3PPP/R2Q1RK1 w - - 0 1").board(), Color::Black));
}

TEST_CASE("structure: passed pawns, pressure and blockades") {
    const auto found      = [](std::string_view fen) { return positionFacts(pos(fen)); };
    const auto white      = [](Role role, Square square) { return placed(Color::White, role, square); };
    const auto black_pawn = [](Square square) { return placed(Color::Black, Role::Pawn, square); };
    // d5 is passed and guarded by e4, itself passed: connected passed pawns.
    const auto duo = found("4k3/8/8/3P4/4P3/8/8/4K3 w - - 0 1");
    CHECK(has(duo, Fact{fact::ProtectedPasser{white(Role::Pawn, Square::D5)}}));
    CHECK(has(duo, Fact{fact::ConnectedPassers{Color::White}}));
    // The a-pawn is far from all the others; the rook stands behind it.
    const auto outside = found("6k1/5pp1/8/8/P7/8/5PP1/R5K1 w - - 0 1");
    const auto a4      = white(Role::Pawn, Square::A4);
    CHECK(has(outside, Fact{fact::OutsidePasser{a4}}));
    CHECK(has(outside, Fact{fact::RookBehindPasser{white(Role::Rook, Square::A1), a4}}));
    // The rook bears on d6 along its half-open file; a pawn on c7 would guard it.
    const Fact pressed{fact::Pressure{white(Role::Rook, Square::D1), black_pawn(Square::D6)}};
    CHECK(has(found("4k3/8/3p4/8/8/8/8/3RK3 w - - 0 1"), pressed));
    CHECK_FALSE(has(found("4k3/2p5/3p4/8/8/8/8/3RK3 w - - 0 1"), pressed));
    CHECK_FALSE(has(found("4k3/8/3p4/8/8/3P4/8/3RK3 w - - 0 1"), pressed));
    // The knight sits in front of the isolated d-pawn.
    CHECK(has(found("4k3/8/8/3p4/3N4/8/8/4K3 w - - 0 1"),
              Fact{fact::Blockade{white(Role::Knight, Square::D4), black_pawn(Square::D5)}}));
}
