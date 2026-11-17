// The facts' types: their debug text as the reference briefs quote it, whom they are good for and what they weigh.

#include <doctest/doctest.hpp>

#include <string>

#include <commentary/facts.hpp>

using namespace fcg::commentary;

TEST_CASE("facts: the debug text the prototype's briefs quote") {
    const Placed white_queen{Color::White, Role::Queen, Square::A2};
    fact::WinsMaterial wins;
    wins.side  = Color::Black;
    wins.pawns = 4;
    wins.trade = Trade{{0, 0, 0, 1}, {0, 0, 1, 0}};
    wins.taken = white_queen;
    CHECK(debug(Fact{wins}) ==
          "WinsMaterial { side: Black, pawns: 4, trade: Some(Trade { won: [0, 0, 0, 1], gave: [0, 0, 1, 0] }), taken: "
          "Some(Placed { color: White, role: Queen, square: A2 }) }");

    fact::ThreatensCapture threat{Color::Black, Move::normal(Role::Knight, Square::B4, Role::Queen, Square::A2),
                                  white_queen};
    CHECK(
        debug(Fact{threat}) ==
        "ThreatensCapture { side: Black, mv: Normal { role: Knight, from: B4, capture: Some(Queen), to: A2, promotion: "
        "None }, target: Placed { color: White, role: Queen, square: A2 } }");

    CHECK(debug(Fact{fact::Mate{Color::Black, 2, std::nullopt}}) == "Mate { side: Black, moves: 2, pattern: None }");
    CHECK(debug(Fact{fact::Formation{Formation::QueensideChain, Color::White}}) ==
          "Formation { kind: QueensideChain, side: White }");
    CHECK(
        debug(Fact{fact::Clearance{
            {Color::Black, Role::Knight, Square::E5}, {Color::Black, Role::Queen, Square::B8}, LineKind::Diagonal}}) ==
        "Clearance { piece: Placed { color: Black, role: Knight, square: E5 }, user: Placed { color: Black, role: "
        "Queen, square: B8 }, line: Some(Diagonal) }");
    CHECK(debug(Fact{fact::Castles{Color::White, CastlingSide::QueenSide}}) ==
          "Castles { side: White, wing: QueenSide }");
    CHECK(debug(Fact{fact::TakesWith{{Color::Black, Role::Bishop, Square::H6}, true}}) ==
          "TakesWith { by: Placed { color: Black, role: Bishop, square: H6 }, back: true }");
    CHECK(
        debug(Fact{fact::PawnBreak{{Color::White, Role::Pawn, Square::C4},
                                   {Color::Black, Role::Pawn, Square::D5},
                                   Lever::freeing(Formation::Maroczy)}}) ==
        "PawnBreak { pawn: Placed { color: White, role: Pawn, square: C4 }, target: Placed { color: Black, role: Pawn, "
        "square: D5 }, lever: Some(Freeing(Maroczy)) }");
    CHECK(debug(Fact{fact::Fork{{Color::White, Role::Knight, Square::E5}, {white_queen, white_queen}}}) ==
          "Fork { attacker: Placed { color: White, role: Knight, square: E5 }, targets: [Placed { color: White, role: "
          "Queen, square: A2 }, Placed { color: White, role: Queen, square: A2 }] }");
    CHECK(std::string(kindName(Fact{wins})) == "WinsMaterial");
}

TEST_CASE("facts: whom a fact is good for, which are plans, what they weigh") {
    const Placed black_knight{Color::Black, Role::Knight, Square::C6};
    CHECK(beneficiary(Fact{fact::Hanging{black_knight}}) == Color::White);
    CHECK(beneficiary(Fact{fact::Fork{black_knight, {}}}) == Color::Black);
    CHECK(beneficiary(Fact{fact::Tablebase{Color::White, Verdict::Loss}}) == Color::Black);
    CHECK_FALSE(beneficiary(Fact{fact::Tablebase{Color::White, Verdict::Draw}}));
    CHECK(isPlan(Fact{fact::PawnBreak{black_knight, black_knight, Lever{}}}));
    CHECK_FALSE(isPlan(Fact{fact::PawnBreak{black_knight, black_knight, std::nullopt}}));
    CHECK(salience(Fact{fact::Mate{}}) == 100);
    fact::WinsMaterial wins;
    wins.pawns = 3;
    CHECK(salience(Fact{wins}) == 58);
    wins.pawns = 20;
    CHECK(salience(Fact{wins}) == 64);
    CHECK(salience(Fact{fact::GainsSpace{}}) == 21);
}
