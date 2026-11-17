// The prototype's crates/facts/src/line.rs tests.

#include "helpers.hpp"

#include <algorithm>
#include <functional>

#include <commentary/line.hpp>

using namespace fcg::commentary;
using test::moves;
using test::pos;

namespace {

bool has(const std::vector<Fact>& facts, const std::function<bool(const Fact&)>& wanted) {
    return std::any_of(facts.begin(), facts.end(), wanted);
}

bool contains(const std::vector<Fact>& facts, const Fact& f) {
    return std::find(facts.begin(), facts.end(), f) != facts.end();
}

template <typename T>
bool any(const std::vector<Fact>& facts) {
    return has(facts, [](const Fact& f) { return std::holds_alternative<T>(f); });
}

template <typename T>
std::function<bool(const Fact&)> is(std::function<bool(const T&)> test) {
    return [test](const Fact& f) {
        const auto* t = std::get_if<T>(&f);
        return t && test(*t);
    };
}

std::vector<Fact> facts(std::string_view fen, std::initializer_list<std::string_view> ucis) {
    const auto p = pos(fen);
    return lineFacts(p, moves(p, ucis));
}

std::string dump(const std::vector<Fact>& facts) {
    std::string out;
    for (const auto& f : facts) out += debug(f) + "\n";
    return out;
}

}  // namespace

TEST_CASE("line: a blunder leaves the queen en prise") {
    const auto p     = pos("4k3/8/8/3p4/8/3Q4/8/4K3 w - - 0 1");
    const auto found = moveFacts(p, moves(p, {"d3c4"})[0]);
    CHECK_MESSAGE(has(found, is<fact::Hanging>([](const auto& h) { return h.piece.role == Role::Queen; })),
                  dump(found));
}

TEST_CASE("line: winning line with fork") {
    // 1.Nc7+ Kd8 2.Nxa8: the fork wins the rook.
    const auto p     = pos("r3k3/8/8/1N6/8/8/8/4K3 w - - 0 1");
    const auto found = lineFacts(p, moves(p, {"b5c7", "e8d8", "c7a8"}));
    CHECK_MESSAGE(any<fact::Fork>(found), dump(found));
    CHECK_MESSAGE(contains(found, Fact{fact::WinsMaterial{Color::White, 5, Trade{{0, 0, 1, 0}, {0, 0, 0, 0}},
                                                          Placed::at(p.board(), Square::A8)}}),
                  dump(found));
    // The same fork, not followed up: not what the line is about.
    CHECK_FALSE(any<fact::Fork>(lineFacts(p, moves(p, {"b5c7", "e8d8", "c7b5"}))));
}

TEST_CASE("line: line ending in smothered mate") {
    const auto found = facts("6rk/6pp/8/6N1/8/8/8/6K1 w - - 0 1", {"g5f7"});
    std::vector<Fact> mates;
    for (const auto& f : found) {
        if (std::holds_alternative<fact::Mate>(f)) mates.push_back(f);
    }
    CHECK(mates == std::vector<Fact>{Fact{fact::Mate{Color::White, 1, MatePattern::Smothered}}});
}

TEST_CASE("line: breaks, perpetuals, stalemates and endings") {
    auto found = facts("4k3/8/4p3/8/5P2/8/8/4K3 w - - 0 1", {"f4f5"});
    CHECK_MESSAGE(has(found, is<fact::PawnBreak>([](const auto& b) { return b.target.square == Square::E6; })),
                  dump(found));
    // Qg5+ Kh8 Qf6+ Kg8 Qg5+: checks until the position comes back.
    found = facts("6k1/5p1p/8/8/8/8/3Q1PPP/6K1 w - - 0 1", {"d2g5", "g8h8", "g5f6", "h8g8", "f6g5", "g8h8"});
    CHECK_MESSAGE(contains(found, Fact{fact::PerpetualCheck{Color::White}}), dump(found));
    CHECK_MESSAGE(contains(found, Fact{fact::Initiative{Color::White}}), dump(found));
    found = facts("7k/8/6K1/8/8/8/5Q2/8 w - - 0 1", {"f2f7"});
    CHECK_MESSAGE(contains(found, Fact{fact::Stalemate{Color::Black}}), dump(found));
    // d5 locks the centre; exd5 Qxd5 opens it.
    found = facts("3qk3/8/3p4/4p3/3PP3/8/8/3QK3 w - - 0 1", {"d4d5"});
    CHECK_MESSAGE(contains(found, Fact{fact::CentreLocks{Color::White}}), dump(found));
    found = facts("3qk3/8/8/3p4/3PP3/8/8/3QK3 w - - 0 1", {"e4d5", "d8d5"});
    CHECK_MESSAGE(contains(found, Fact{fact::CentreOpens{Color::White}}), dump(found));
    // Queens off: a rook endgame.
    found = facts("3qk2r/8/8/8/8/8/8/R2QK3 w - - 0 1", {"d1d8", "e8d8"});
    CHECK_MESSAGE(contains(found, Fact{fact::Endgame{Color::White, Ending::Rook}}), dump(found));
}

TEST_CASE("line: a pinned queen taken where it went still pays the pin") {
    // 1.Bc4 pins the queen to its king; 1...Qxc4 2.Qxc4 wins it.
    auto found = facts("6k1/8/4q3/8/8/8/2Q1B3/6K1 w - - 0 1", {"e2c4", "e6c4", "c2c4"});
    CHECK_MESSAGE(has(found, is<fact::Pin>([](const auto& p) { return p.pinned.role == Role::Queen; })), dump(found));
    // A pawn that queens on the side's second move still promotes in the line.
    found = facts("8/8/1P6/8/8/8/k7/4K3 w - - 0 1", {"b6b7", "a2a3", "b7b8q"});
    CHECK_MESSAGE(contains(found, Fact{fact::Promotes{Color::White, Role::Queen, Square::B8}}), dump(found));
}

TEST_CASE("line: a piece that takes its pinner was not pinned") {
    // 1.Qxd8 stands between nothing and the rook on f8, which takes it: an exchange of queens, not a pin of the rook to
    // its king.
    const auto found = facts("3q1r1k/6pp/8/8/8/8/6PP/3Q2K1 w - - 0 1", {"d1d8", "f8d8"});
    CHECK_MESSAGE(!any<fact::Pin>(found), dump(found));
}

TEST_CASE("line: a pin that matters is the motif even without a capture") {
    // 1.Bc4 pins the queen to its king: attacked by a bishop, it cannot step away.
    for (const auto& line : {std::initializer_list<std::string_view>{"e2c4", "e6c4", "c2c4"},
                             std::initializer_list<std::string_view>{"e2c4", "g8h8", "c2c3"}}) {
        const auto found = facts("6k1/8/4q3/8/8/8/2Q1B3/6K1 w - - 0 1", line);
        CHECK_MESSAGE(has(found, is<fact::Pin>([](const auto& p) { return p.pinned.role == Role::Queen; })),
                      dump(found));
    }
}

TEST_CASE("line: greek gift sacrifice") {
    // 1.Bxh7+ Kxh7 2.Ng5+ Kg8 3.Qh5: the bishop is given for the attack.
    const auto found = facts("r1bq1rk1/pppn1ppp/4p3/3pP3/1b1P4/2NB1N2/PPP2PPP/R2QK2R w KQ - 0 9",
                             {"d3h7", "g8h7", "f3g5", "h7g8", "d1h5"});
    CHECK_MESSAGE(has(found, is<fact::Sacrifice>(
                                 [](const auto& s) { return s.side == Color::White && s.piece.role == Role::Bishop; })),
                  dump(found));
    CHECK(has(found, [](const Fact& f) {
        return std::holds_alternative<fact::ThreatensMate>(f) || std::holds_alternative<fact::WinsMaterial>(f);
    }));
}

TEST_CASE("line: a defender is drawn away or taken first") {
    // 1.h5+ Kxh5 2.Rxf7: the pawn draws the king away from its rook.
    auto found = facts("8/3R1r2/6k1/1p6/p1pB3P/3n2K1/5P2/8 w - - 4 44", {"h4h5", "g6h5", "d7f7"});
    CHECK_MESSAGE(has(found, is<fact::Deflection>([](const auto& d) {
                          return d.piece.role == Role::King && d.guarded.square == Square::F7;
                      })),
                  dump(found));
    // 1.Rxe8+ Qxe8 2.Bxd6: the queen guarded both the rook and the bishop on d6.
    found = facts("r2qr1k1/1p3ppn/p1nb2bp/2p5/2Pp4/P2P1NB1/1P1N1PPP/1Q1RRBK1 w - - 4 20", {"e1e8", "d8e8", "g3d6"});
    CHECK_MESSAGE(has(found, is<fact::Overloaded>([](const auto& o) {
                          return o.piece.square == Square::D8 && o.first.square == Square::E8 &&
                                 o.second.square == Square::D6;
                      })),
                  dump(found));
    CHECK_MESSAGE(!any<fact::Deflection>(found), dump(found));
    CHECK_MESSAGE(has(found, is<fact::WinsMaterial>([](const auto& w) {
                          return w.taken && w.taken->square == Square::D6 && w.taken->role == Role::Bishop;
                      })),
                  dump(found));
    // 1.Bxf6 gxf6 2.Rxd5: the knight that guarded the bishop goes first.
    found = facts("6k1/6pp/5n2/3b2B1/8/8/8/3R2K1 w - - 0 1", {"g5f6", "g7f6", "d1d5"});
    CHECK_MESSAGE(has(found, is<fact::RemovesDefender>([](const auto& r) {
                          return r.defender.square == Square::F6 && r.guarded.square == Square::D5;
                      })),
                  dump(found));
    // The same exchange with nothing behind it is only an exchange.
    CHECK_FALSE(any<fact::RemovesDefender>(facts("6k1/6pp/5n2/3b2B1/8/8/8/3R2K1 w - - 0 1", {"g5f6", "g7f6", "g1f2"})));
}

TEST_CASE("line: a piece with two things to guard is overloaded") {
    // 1.Rxd7 Qxd7 2.Qxa8+: the queen guarded both the knight and the rook.
    auto found = facts("r1q3k1/3nbppp/pp2p3/4B3/8/2N2Q2/PPPR1PPP/6K1 w - - 1 19", {"d2d7", "c8d7", "f3a8"});
    CHECK_MESSAGE(has(found, is<fact::Overloaded>([](const auto& o) {
                          return o.piece.square == Square::C8 && o.first.square == Square::D7 &&
                                 o.second.square == Square::A8;
                      })),
                  dump(found));
    // 1.Qxf7+ Rxf7 2.Rd8#: the rook guarded the pawn and the square the mate comes from.
    found = facts("5rk1/5ppp/1r6/3Q4/2B1P3/2q3P1/3R1PKP/8 w - - 0 28", {"d5f7", "f8f7", "d2d8"});
    CHECK_MESSAGE(has(found, is<fact::Overloaded>([](const auto& o) {
                          return o.piece.square == Square::F8 && o.first.square == Square::F7 &&
                                 o.second.square == Square::D8 && o.second.color == Color::White;
                      })),
                  dump(found));
    // Taking twice on one square is an exchange there, whoever takes back.
    found = facts("6k1/6b1/8/4P3/2n2P2/8/8/6K1 b - - 0 1", {"c4e5", "f4e5", "g7e5"});
    CHECK_MESSAGE((!any<fact::Overloaded>(found) && !any<fact::Deflection>(found)), dump(found));
}

TEST_CASE("line: a square or a line is cleared for another piece") {
    // 1...Rxe6 2.dxe6 Bd4: the rook gives itself to open the diagonal for the bishop, which pins the queen.
    auto found = facts("4r1k1/p5bp/1q2N1p1/2pPrp2/1pP5/6PP/P4QB1/1R3RK1 b - - 3 25",
                       {"e5e6", "d5e6", "g7d4", "g1h1", "d4f2", "f1f2"});
    CHECK_MESSAGE(has(found, is<fact::Clearance>([](const auto& c) {
                          return c.piece.square == Square::E5 && c.user.square == Square::G7 &&
                                 c.line == LineKind::Diagonal;
                      })),
                  dump(found));
    // 1.Rf8+ Ke7 2.b8=Q: the rook leaves the promotion square with a check.
    found = facts("1R6/1P6/4pkp1/5p2/3P4/3KP2p/8/1r6 w - - 0 44", {"b8f8", "f6e7", "b7b8q", "b1b8", "f8b8"});
    CHECK_MESSAGE(has(found, is<fact::Clearance>([](const auto& c) {
                          return c.piece.square == Square::B8 && c.user.role == Role::Pawn &&
                                 c.user.square == Square::B7 && !c.line;
                      })),
                  dump(found));
    // A knight that steps back, and a queen that then crosses its square with a check: pieces regroup that way in any
    // game.
    found = facts("6k1/5ppp/8/8/8/5N2/5PPP/3Q2K1 w - - 0 1", {"f3e1", "g8f8", "d1d8"});
    CHECK_MESSAGE(!any<fact::Clearance>(found), dump(found));
}

TEST_CASE("line: pawns break through and race") {
    // 1.b6 cxb6 2.a6 bxa6 3.c6: two pawns given for a passed pawn nothing stops.
    auto found = facts("7k/ppp5/8/PPP5/8/8/8/7K w - - 0 1", {"b5b6", "c7b6", "a5a6", "b7a6", "c5c6"});
    CHECK_MESSAGE(has(found, is<fact::Breakthrough>([](const auto& b) {
                          return b.pawn.square == Square::B6 && b.passer.square == Square::C5;
                      })),
                  dump(found));
    // A pawn traded for a pawn is no breakthrough, passed pawn or not.
    found = facts("4k3/8/1p6/8/1PP5/8/8/4K3 w - - 0 1", {"c4c5", "b6c5", "b4c5"});
    CHECK_MESSAGE(!any<fact::Breakthrough>(found), dump(found));
    // Both sides run a pawn: Black's queens while White's stands on its sixth rank.
    found = facts("8/8/8/3K2kp/8/2P1P3/8/8 w - - 0 50",
                  {"c3c4", "h5h4", "d5e4", "g5g4", "c4c5", "h4h3", "e4e5", "h3h2", "c5c6", "h2h1q"});
    CHECK_MESSAGE(contains(found, Fact{fact::Race{Color::Black, false, false}}), dump(found));
    // A pawn that queens while nothing runs against it wins no race.
    found = facts("8/8/1P6/8/8/8/k7/4K3 w - - 0 1", {"b6b7", "a2a3", "b7b8q"});
    CHECK_MESSAGE(!any<fact::Race>(found), dump(found));
}

TEST_CASE("line: a king is lured into a fork") {
    // 1.Bxe7+ Kxe7 2.Rxe8+ Kxe8 3.Nd6+ and 4.Nxc4.
    const auto found = facts("3Rbk2/1p2bp1p/5Bp1/1N2P3/2r5/7P/6P1/5K2 w - - 2 43",
                             {"f6e7", "f8e7", "d8e8", "e7e8", "b5d6", "e8e7", "d6c4"});
    CHECK_MESSAGE(has(found, is<fact::Attraction>([](const auto& a) {
                          return a.piece.role == Role::King && a.piece.square == Square::E8;
                      })),
                  dump(found));
}

TEST_CASE("line: a pinned piece neither guards nor escapes") {
    // The b5-pawn shields the b8-rook from the queen: it cannot take back on c4.
    auto found = facts("1r4k1/3qrppp/b3p3/1p6/1Qp5/P3PB1P/3N1PP1/R1R3K1 w - - 2 27", {"d2c4", "f7f6", "c1d1"});
    CHECK_MESSAGE(has(found, is<fact::Pin>([](const auto& p) {
                          return p.pinner.square == Square::B4 && p.pinned.square == Square::B5 &&
                                 p.behind.square == Square::B8;
                      })),
                  dump(found));
    // A knight pinned to its queen, attacked by a pawn, is lost where it stands.
    found = facts("3qk3/8/5n2/6B1/4P3/8/8/4K3 w - - 0 1", {"e4e5", "e8f8", "e5f6"});
    CHECK_MESSAGE(has(found, is<fact::Pin>([](const auto& p) {
                          return p.pinned.square == Square::F6 && p.behind.role == Role::Queen;
                      })),
                  dump(found));
}

TEST_CASE("line: a piece for pawns is given, and a piece lost already is not") {
    // 1...Bxg3 2.hxg3 Rxg3+: a bishop for two pawns and the king's cover.
    auto found =
        facts("r3b1q1/p1k3r1/1pn1p2p/3pP3/1pPP3b/1N1B2P1/P2NQR1P/5RK1 b - - 0 20", {"h4g3", "h2g3", "g7g3", "g1h1"});
    CHECK_MESSAGE(has(found, is<fact::Sacrifice>([](const auto& s) { return s.piece.role == Role::Bishop; })),
                  dump(found));
    // In check with a rook en prise: taking the pawn does not give the rook.
    found = facts("8/5R2/p5k1/7p/2PpN3/P2P2p1/1P3KP1/8 w - - 0 58", {"f2g3", "g6f7", "b2b4"});
    CHECK_MESSAGE(!any<fact::Sacrifice>(found), dump(found));
}

TEST_CASE("line: a check before taking back is an in-between move") {
    const auto before = pos("4k3/8/5n2/3P4/4P3/8/8/4KB2 b - - 0 1");
    const auto took   = moves(before, {"f6d5"})[0];
    const auto p      = pos("4k3/8/8/3n4/4P3/8/8/4KB2 w - - 0 2");
    CHECK(intermezzo(took, p, moves(p, {"f1b5", "e8f8", "e4d5"})) ==
          std::optional<Fact>(Fact{fact::Intermezzo{Color::White}}));
    // Taking back at once is no in-between move.
    CHECK_FALSE(intermezzo(took, p, moves(p, {"e4d5", "e8f8"})));
}

TEST_CASE("line: plans a line carries out") {
    // 1.Nf3 brings the knight out.
    const auto p = Position::initial();
    auto found   = lineFacts(p, moves(p, {"g1f3"}));
    CHECK_MESSAGE(has(found, is<fact::Develops>([](const auto& d) {
                          return d.piece.role == Role::Knight && d.piece.square == Square::F3;
                      })),
                  dump(found));
    // Three pawns up, White trades the queens.
    found = facts("3qk3/8/8/8/8/8/PPP5/3QK3 w - - 0 1", {"d1d8", "e8d8"});
    CHECK_MESSAGE(contains(found, Fact{fact::Simplifies{Color::White}}), dump(found));
    // Kings on opposite wings: g4 joins the h-pawn against the black king.
    found = facts("3q2k1/5ppp/8/8/7P/8/6P1/2KQ4 w - - 0 1", {"g2g4", "d8d7"});
    CHECK_MESSAGE(contains(found, Fact{fact::PawnStorm{Color::White}}), dump(found));
    // Two pawns against three, c6 holding d5: b5 is the minority attack.
    found = facts("4k3/pp6/2p5/3p4/1P1P4/4P3/P7/4K3 w - - 0 1", {"b4b5"});
    CHECK_MESSAGE(contains(found, Fact{fact::MinorityAttack{Color::White}}), dump(found));
}

TEST_CASE("line: luft and majorities") {
    // ...h6 gives the king a square: until then its own pawns shut it in.
    auto found = facts("6k1/5ppp/8/8/8/8/5PPP/3R2K1 b - - 0 1", {"h7h6"});
    CHECK_MESSAGE(contains(found, Fact{fact::Luft{Color::Black}}), dump(found));
    // A king that already has one gets nothing new from ...g6.
    CHECK_FALSE(any<fact::Luft>(facts("6k1/5pp1/7p/8/8/8/5PPP/3R2K1 b - - 0 1", {"g7g6"})));
    // Nor is it luft while no rook or queen bears on the first rank.
    CHECK_FALSE(any<fact::Luft>(facts("6k1/5ppp/8/8/8/3P4/5PPP/3R2K1 b - - 0 1", {"h7h6"})));
    // Bh6 trades off the bishop that held Black's dark squares.
    found =
        facts("r2q1rk1/1b3pbp/p3p1p1/1p1p2B1/3P4/2N1PN2/PP1Q1PPP/R4RK1 w - - 0 1", {"g5h6", "f8e8", "h6g7", "g8g7"});
    CHECK_MESSAGE(contains(found, Fact{fact::WeakColour{Color::Black, true}}), dump(found));
    // ...cxd4 Nxd4 leaves White three pawns against two on the queen's wing.
    found = facts("4k3/pp2pppp/8/2p5/3P4/5N2/PPP2PPP/4K3 b - - 0 1", {"c5d4", "f3d4"});
    CHECK_MESSAGE(contains(found, Fact{fact::Majority{Color::White, Wing::Queenside}}), dump(found));
}

TEST_CASE("line: space and formations a move sets up") {
    // 3.e5 in the French: space, and the chain d4-e5.
    auto found = facts("rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/8/PPP2PPP/RNBQKBNR w KQkq - 0 3", {"e4e5", "c7c5"});
    const Placed e5{Color::White, Role::Pawn, Square::E5};
    CHECK_MESSAGE(contains(found, Fact{fact::Space{Color::White}}), dump(found));
    CHECK_MESSAGE(contains(found, Fact{fact::GainsSpace{e5}}), dump(found));
    CHECK_MESSAGE(contains(found, Fact{fact::Formation{Formation::KingsideChain, Color::White}}), dump(found));
    // And 3...c5 strikes at the base of the chain.
    found = facts("rnbqkbnr/ppp2ppp/4p3/3pP3/3P4/8/PPP2PPP/RNBQKBNR b KQkq - 0 3", {"c7c5"});
    CHECK_MESSAGE(
        has(found, is<fact::PawnBreak>([](const auto& b) { return b.lever == Lever{Lever::Kind::ChainBase}; })),
        dump(found));
    // A pawn that strikes at a pawn is a break, not space gained.
    CHECK_MESSAGE(!any<fact::GainsSpace>(found), dump(found));
    // The exchange on d5 in the Queen's Gambit leaves the Carlsbad structure.
    found = facts("rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 2 4", {"c4d5", "e6d5"});
    CHECK_MESSAGE(contains(found, Fact{fact::Formation{Formation::Carlsbad, Color::White}}), dump(found));
}
