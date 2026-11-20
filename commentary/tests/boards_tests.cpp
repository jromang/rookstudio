// What a sentence says of the boards it speaks of (#64): each false sentence of the November 18 reread and of the
// stories of #62, refused, beside sentences of the same positions that hold. The facts are those the prompts gave.

#include <doctest/doctest.hpp>

#include <string>
#include <vector>

#include <commentary/brief.hpp>
#include <commentary/text.hpp>

#include "helpers.hpp"

using namespace fcg::commentary;
using namespace fcg::commentary::brief_detail;

namespace {

constexpr auto White = Color::White;
constexpr auto Black = Color::Black;

Placed piece(Color color, Role role, Square square) { return Placed{color, role, square}; }

std::string said(const std::optional<Refusal>& r) { return r ? debug(*r) : "Ok"; }

// UCI moves from `start`, separated by spaces.
std::vector<Move> line(const Position& start, std::string_view ucis) {
    auto pos = start;
    std::vector<Move> out;
    for (const auto u : text::splitWhitespace(ucis)) {
        const auto m = pos.parseUci(u);
        REQUIRE_MESSAGE(m, std::string(u));
        pos.playUnchecked(*m);
        out.push_back(*m);
    }
    return out;
}

// What a prompt shows of the move `played` from `fen`: the move and the engine's answer, `reply_shown` plies of it, and
// the best line, `best_shown` of it; the moves a sentence may quote.
Allowed shown(std::string_view fen, std::string_view played, std::string_view reply, std::size_t reply_shown,
              std::string_view best, std::size_t best_shown, Lang lang) {
    const auto before  = test::pos(fen);
    const auto letters = lang == Lang::En ? PieceLetters::English : PieceLetters::French;
    auto moves         = line(before, played);
    auto after         = before;
    after.playUnchecked(moves.front());
    for (const auto& m : line(after, reply)) moves.push_back(m);
    Allowed a;
    a.lines.push_back(Shown::of(Shown::Kind::Played, before, moves, 1 + reply_shown, letters));
    if (!best.empty()) a.lines.push_back(Shown::of(Shown::Kind::Best, before, line(before, best), best_shown, letters));
    for (const auto& sh : a.lines) {
        for (const auto& forms : sh.texts) a.moves.insert(a.moves.end(), forms.begin(), forms.end());
    }
    a.played = a.lines.front().texts.front();
    return a;
}

// Before 26…Rg6 (an inaccuracy): 27.g3 hits the knight on f4, the rook gets to d3 a move later.
Allowed beforeRookToD3(Lang lang) {
    return shown("5r1k/1pp1q1pp/1r6/p3P3/P4n2/2P1RN1P/2Q2PPK/R7 b - - 2 26", "b6g6",
                 "g2g3 f4d5 e3d3 d5b6 a1e1 e7e8 c2e2 g6e6 f3g5 e6e7 e2e4", 4,
                 "f4d5 e3e4 d5f4 c2d2 e7f7 f3d4 c7c5 d4b5 b6g6 a1g1 f4g2 e5e6 g2h4 e4h4", 4, lang);
}

}  // namespace

TEST_CASE("checker: a piece named on a square stands on the board the sentence speaks of") {
    {
        // 26…Rg6 27.g3 Nd5 28.Rd3: the rook is on e3 until the line's third move.
        const auto a = beforeRookToD3(Lang::Fr);
        MoveFacts facts;
        facts.punished_by = {fact::ThreatensCapture{White, Move{}, piece(Black, Role::Knight, Square::F4)},
                             fact::RookOnOpenFile{piece(White, Role::Rook, Square::D3)}};
        facts.missed      = {fact::ThreatensCapture{Black, Move{}, piece(White, Role::Rook, Square::E3)}};
        const auto says   = [&](std::string_view text) { return said(checkBoards(text, facts, a)); };
        CHECK(says("Imprécision. Après 26…Tg6, 27.g3 attaque le cavalier en f4, et la tour blanche en d3 tient la "
                   "colonne ouverte. Mieux valait 26…Cd5, qui menaçait la tour blanche en e3.") ==
              R"(Piece("tour blanche en d3"))");
        // Where the line gets it there, or quoting no move of the line, as the facts put it.
        CHECK(says("Après 28.Td3, la tour blanche en d3 tient la colonne ouverte.") == "Ok");
        CHECK(says("26…Tg6 laisse la tour blanche en d3 tenir la colonne ouverte.") == "Ok");
        CHECK(says("Après 26…Tg6, 27.g3 attaque le cavalier en f4.") == "Ok");
    }
    {
        // 27.Rg1 Qf7 28.g3 Ne6: the knight is on f4 until the line's third move.
        const auto a = shown("5r1k/1pp1q1pp/6r1/p3P3/P4n2/2P1RN1P/2Q2PPK/R7 w - - 3 27", "a1g1",
                             "e7f7 g2g3 f4e6 g1d1 g6h6 h2g2 f7h5 f3g1 g7g5", 4,
                             "g2g3 f4d5 e3d3 d5b6 a1e1 e7e8 c2e2 g6e6 f3g5 e6e7 e2e4", 4, Lang::Fr);
        MoveFacts facts;
        facts.punished_by = {
            fact::Blockade{piece(Black, Role::Knight, Square::E6), piece(White, Role::Pawn, Square::E5)}};
        const auto says = [&](std::string_view text) { return said(checkBoards(text, facts, a)); };
        CHECK(says("Imprécision : après 27…Df7, le cavalier noir en e6 bloque le pion e5. Mieux valait 27.g3, qui "
                   "menaçait le cavalier en f4.") == R"(Piece("cavalier noir en e6"))");
        CHECK(says("Imprécision : après 28…Ce6, le cavalier noir en e6 bloque le pion e5.") == "Ok");
        CHECK(says("Après 27…Df7, les Noirs peuvent ensuite installer le cavalier noir en e6 devant le pion e5.") ==
              "Ok");
        CHECK(says("27.Tg1 laisse le cavalier noir en e6 bloquer le pion e5.") == "Ok");
        // Quoting no move, the sentence speaks of the board around the move played.
        CHECK(says("Imprécision : le cavalier noir en e6 bloque le pion e5, et les Blancs ne l'ont pas prévu.") ==
              R"(Piece("cavalier noir en e6"))");
    }
    {
        // 20.Qxb7 Qxe3+ 21.Kh1 Rc1: the bishop on e3 is taken the move before the rook comes to c1.
        const auto a = shown("2r3k1/pp1b2p1/4p1p1/3pP3/8/PQq1BN2/6PP/5RK1 w - - 0 20", "b3b7",
                             "c3e3 g1h1 c8c1 f1c1 e3c1 f3g1", 4, "b3c3 c8c3 e3a7 c3a3", 4, Lang::En);
        MoveFacts facts;
        facts.punished_by = {fact::Hanging{piece(White, Role::Bishop, Square::E3)}};
        const auto says   = [&](std::string_view text) { return said(checkBoards(text, facts, a)); };
        CHECK(says("Taking on b7 lets the win slip: after 21…Rc1 the rook on f1 is pinned against the king, and the "
                   "bishop on e3 is left hanging.") == R"(Piece("bishop on e3"))");
        CHECK(says("Taking on b7 leaves the bishop on e3 hanging, and after 21…Rc1 the rook on f1 is pinned.") == "Ok");
    }
}

TEST_CASE("checker: a move quoted otherwise than after is told with what its line goes on to do") {
    {
        // 7…e5 8.d5 Nd4: the knight settles on d4 two moves on.
        const auto a = shown("r2qkb1r/ppp1pp1p/2np1np1/8/2PPP3/2N2B2/PP3PPP/R1BQK2R b KQkq - 0 7", "f8g7",
                             "d4d5 c6b8 c1e3 c7c6 d1c2 e8g8", 4, "e7e5 d4d5 c6d4 c1e3 f8g7 e3d4", 4, Lang::En);
        MoveFacts facts;
        facts.punished_by = {fact::ThreatensCapture{White, Move{}, piece(Black, Role::Knight, Square::C6)},
                             fact::MoreActive{White}};
        facts.missed      = {
            fact::Outpost{piece(Black, Role::Knight, Square::D4)},
            fact::PawnBreak{piece(Black, Role::Pawn, Square::E5), piece(White, Role::Pawn, Square::D4), std::nullopt},
            fact::CentreLocks{Black}};
        const auto says = [&](std::string_view text) { return said(checkBoards(text, facts, a)); };
        CHECK(says("The bishop move lets White in with 8.d5, which attacks the knight on c6. 7…e5 was better: the "
                   "pawn strikes at d4, the centre locks, and the knight on d4 settles on an outpost.") == "Ok");
        CHECK(says("After 7…e5, the knight on d4 is on an outpost.") == R"(Piece("knight on d4"))");
        CHECK(says("After 7…e5, the knight on d4 then settles on an outpost.") == "Ok");
    }
    {
        // 29.Nd4 Rh6 30.Ne2 Ne6: no black knight ever stands on h3, where the fact the prompt gave put one.
        const auto a = shown("5r1k/1pp1q1pp/6r1/p3P3/P3Rn2/2P2N1P/2Q2PPK/6R1 w - - 7 29", "e4e3",
                             "e7f7 f3d4 c7c5 d4b5 f4d5 e3e4 f7f2 c2f2 f8f2", 5,
                             "f3d4 g6h6 d4e2 f4e6 g1f1 g7g5 e2d4 e6f4", 4, Lang::Fr);
        MoveFacts facts;
        facts.punished_by = {
            fact::DiscoveredAttack{piece(Black, Role::Queen, Square::F7), piece(White, Role::Pawn, Square::F2)}};
        facts.missed    = {fact::ThreatensCapture{White, Move{}, piece(Black, Role::Knight, Square::H3)}};
        const auto says = [&](std::string_view text) { return said(checkBoards(text, facts, a)); };
        CHECK(says("Plus précis était 29.Cd4, qui menaçait le cavalier noir en h3.") ==
              R"(Piece("cavalier noir en h3"))");
        CHECK(says("Plus précis était 29.Cd4, qui menaçait le cavalier noir en f4.") == "Ok");
    }
}

TEST_CASE("checker: a tactic is not told right after a move that only prepares it") {
    // 29.Re3 Qf7 30.Nd4 c5 31.Nb5 Nd5: the knight leaving f4 uncovers the queen's attack on f2.
    const auto a =
        shown("5r1k/1pp1q1pp/6r1/p3P3/P3Rn2/2P2N1P/2Q2PPK/6R1 w - - 7 29", "e4e3",
              "e7f7 f3d4 c7c5 d4b5 f4d5 e3e4 f7f2 c2f2 f8f2", 5, "f3d4 g6h6 d4e2 f4e6 g1f1 g7g5", 4, Lang::Fr);
    MoveFacts facts;
    facts.punished_by = {
        fact::DiscoveredAttack{piece(Black, Role::Queen, Square::F7), piece(White, Role::Pawn, Square::F2)}};
    const auto says = [&](std::string_view text) { return said(checkBoards(text, facts, a)); };
    CHECK(says("Imprécision : après 29…Df7, les Noirs découvrent une attaque sur le pion f2, mais la position reste "
               "gagnante.") == R"(Board("découvrent"))");
    CHECK(says("Après 31…Cd5, les Noirs découvrent une attaque sur le pion f2.") == "Ok");
    CHECK(says("29.Te3 permet une attaque à la découverte sur le pion f2.") == "Ok");
    // Told as what follows the move, it may come later: « une attaque à la découverte suit », not « vise ».
    CHECK(says("Après 29…Df7, une attaque à la découverte suit sur le pion f2.") == "Ok");
    CHECK(says("Après 29…Df7, une attaque à la découverte vise le pion en f2.") == R"(Board("découverte"))");
    // The better move that prepares it, as 29…Qf7 does before 29…Nd5.
    const auto b = shown("5r1k/1pp1q1pp/6r1/p3P3/P4n2/2P1RN1P/2Q2PPK/6R1 b - - 8 29", "f4d5",
                         "e3e4 d5f4 f3d4 e7h4 e4e3 c7c5", 4, "e7f7 f3d4 c7c5 d4b5 f4d5 e3e4 f7f2 c2f2", 5, Lang::Fr);
    MoveFacts missed;
    missed.punished_by = {fact::Hole{Black, Square::E6}};
    missed.missed      = facts.punished_by;
    CHECK(said(checkBoards("Le coup laisse la case e6 faible, alors que 29…Df7 préparait une attaque à la découverte "
                           "sur le pion f2.",
                           missed, b)) == "Ok");
}

TEST_CASE("checker: a pawn structure is said of the position the line settles in") {
    // 29…dxc4 30.Bxc4: the recapture undoes the doubled pawns the capture made.
    const auto a = shown("r5k1/1pp3pp/4p1r1/3p1p2/1PPP1P1q/p2BP3/P7/B1KQ1R2 b - - 0 29", "d5c4",
                         "d3c4 c7c6 a1c3 h4h2 c3d2 h2g2 f1e1 b7b5", 4, "h4h2 c1b1 c7c6 d1e1 h7h5 f1h1", 4, Lang::En);
    MoveFacts facts;
    facts.punished_by = {fact::DoubledPawns{Black, File::C}};
    facts.missed      = {fact::MoreActive{Black}};
    const auto says   = [&](std::string_view text) { return said(checkBoards(text, facts, a)); };
    CHECK(says("A blunder: after 29…dxc4 White takes back with 30.Bxc4, and Black is left with doubled pawns on the "
               "c-file. 29…Qh2 kept the clear advantage.") == R"(Board("doubled pawns on the c-file"))");
    // However the passage says it: they are no lasting feature.
    CHECK(says("29…dxc4 leaves doubled pawns on the c-file.") == R"(Board("doubled pawns on the c-file"))");
    CHECK(says("Black takes on c4 and is left with doubled pawns on the c-file.") ==
          R"(Board("doubled pawns on the c-file"))");
    // Rooks doubled on a file, a game gone into an endgame, are no pawn structure.
    CHECK(says("After 30.Bxc4, Black doubles the rooks, and the rooks are doubled on the c-file.") == "Ok");
    // A pawn the quoted move takes is not the one left on its square later.
    const auto b = shown("5qk1/2p4p/1p1p2p1/p2Pp3/P1P1P3/1P3Q2/6PP/6K1 b - - 1 25", "f8h6",
                         "h2h3 g8g7 g1f2 h6d2 f2f1 d2f4", 4, "f8f3 g2f3 g8g7 g1f2 g7h6 f2g2 h6g5 g2g3", 4, Lang::En);
    MoveFacts ending;
    ending.punished_by = {fact::ExposedKing{piece(Black, Role::King, Square::G7)}};
    ending.missed      = {fact::Endgame{Black, Ending::Pawn}, fact::BackwardPawn{piece(White, Role::Pawn, Square::F3)},
                          fact::IsolatedPawn{piece(White, Role::Pawn, Square::H2)}};
    CHECK(said(checkBoards("25…Qxf3 was the move, trading the queens and heading for a pawn endgame in which White's "
                           "f3 pawn is backward and the h2 pawn is isolated.",
                           ending, b)) == "Ok");
}

TEST_CASE("checker: what a quoted capture takes is named by its kind") {
    // 25…Qxf3 takes the queen; the pawn on f3 is the g-pawn that takes back.
    const auto b = shown("5qk1/2p4p/1p1p2p1/p2Pp3/P1P1P3/1P3Q2/6PP/6K1 b - - 1 25", "f8h6",
                         "h2h3 g8g7 g1f2 h6d2 f2f1 d2f4", 4, "f8f3 g2f3 g8g7 g1f2 g7h6 f2g2 h6g5 g2g3", 4, Lang::En);
    MoveFacts facts;
    facts.punished_by = {fact::ExposedKing{piece(Black, Role::King, Square::G7)}};
    facts.missed      = {fact::Endgame{Black, Ending::Pawn}, fact::BackwardPawn{piece(White, Role::Pawn, Square::F3)},
                         fact::IsolatedPawn{piece(White, Role::Pawn, Square::H2)}};
    const auto says   = [&](std::string_view text) { return said(checkBoards(text, facts, b)); };
    CHECK(says("A blunder: 25…Qh6 leaves the black king on g7 without pawn cover. 25…Qxf3 was the move, taking the "
               "pawn and heading for a pawn endgame in which White's f3 pawn is backward and the h2 pawn is "
               "isolated.") == R"(Material("the pawn"))");
    CHECK(says("25…Qxf3 was the move, taking White's f3 pawn and heading for a pawn endgame.") ==
          R"(Material("f3 pawn"))");
    CHECK(says("25…Qxf3 was the move, taking the queen.") == "Ok");
    // Taken back at once: "took the queens off".
    CHECK(says("25…Qxf3 took the queens off.") == "Ok");
}

TEST_CASE("checker: a piece said to be taken stands where it is taken") {
    // 11.h3 Bxf3 12.Qxf3 Nbd7 13.Nd2 Ne5: the bishop takes a knight on f3, the queen comes there after; the line is
    // shown as far as the fork.
    for (const auto lang : {Lang::En, Lang::Fr}) {
        const auto a = shown("rn1q1rk1/pp2bppp/3p1n2/2pP2B1/2B3b1/2P2N2/PP2QPPP/RN3RK1 w - - 8 11", "h2h3",
                             "g4f3 e2f3 b8d7 b1d2 d7e5", 5, "f1e1 g4f3 g2f3 h7h6", 4, lang);
        MoveFacts facts;
        facts.punished_by = {
            fact::Fork{piece(Black, Role::Knight, Square::E5),
                       {piece(White, Role::Queen, Square::F3), piece(White, Role::Bishop, Square::C4)}}};
        const auto says = [&](std::string_view text) { return said(checkBoards(text, facts, a)); };
        if (lang == Lang::En) {
            CHECK(says("A mistake that lets a clear advantage slip: after 11.h3 Black can take the queen on f3, and "
                       "the knight on e5 then forks it with the bishop on c4.") == R"(Material("queen on f3"))");
            CHECK(says("After 11.h3 Black can take the knight on f3, and the knight on e5 then forks the queen on f3 "
                       "and the bishop on c4.") == "Ok");
        } else {
            CHECK(says("Erreur : après 11.h3, les Noirs peuvent prendre la dame en f3, et le cavalier en e5 fait "
                       "ensuite une fourchette sur elle et sur le fou en c4.") == R"(Material("dame en f3"))");
            CHECK(says("Après 11.h3, les Noirs peuvent prendre le cavalier en f3.") == "Ok");
        }
    }
}

TEST_CASE("checker: material said to be won is counted along the line") {
    // 20.Qxb7 Qxe3+ 21.Kh1 Rc1 22.Rxc1 Qxc1+: the bishop on e3 for two pawns, b7 and a7.
    const auto a = shown("2r3k1/pp1b2p1/4p1p1/3pP3/8/PQq1BN2/6PP/5RK1 w - - 0 20", "b3b7",
                         "c3e3 g1h1 c8c1 f1c1 e3c1 f3g1 d7c6 b7a7 c1c4 g1f3 c4c1", 4,
                         "b3c3 c8c3 e3a7 c3a3 a7c5 a3a5 f1c1 g8h7 c5d4 g6g5", 4, Lang::En);
    MoveFacts facts;
    Trade bishop_for_pawns;
    bishop_for_pawns.won  = {0, 1, 0, 0};
    bishop_for_pawns.gave = {2, 0, 0, 0};
    facts.punished_by     = {fact::Hanging{piece(White, Role::Bishop, Square::E3)},
                             fact::WinsMaterial{Black, 1, bishop_for_pawns, piece(White, Role::Bishop, Square::E3)}};
    facts.missed          = {fact::Guards{piece(White, Role::Bishop, Square::E3)}};
    const auto says       = [&](std::string_view text) { return said(checkBoards(text, facts, a)); };
    CHECK(says("A blunder that throws away the win: White does not take back on c3, and 20…Qxe3+ wins the bishop on "
               "e3 and two pawns.") == R"(Material("the bishop on e3 and two pawns"))");
    CHECK(says("20…Qxe3+ wins the bishop on e3 for two pawns.") == "Ok");
    CHECK(says("20…Qxe3+ wins the bishop on e3.") == "Ok");
    // Another clause, not a second thing won.
    CHECK(says("Black wins a pawn, and the bishop on e3 hangs.") == "Ok");
    // What is lost may be named without what comes back for it: the queen for a pawn.
    const auto queen =
        shown("5qk1/2p4p/1p1p2p1/p2Pp3/P1P1P3/1P3Q2/6PP/6K1 b - - 1 25", "f8f3", "g2f3 g8g7", 2, "", 0, Lang::En);
    CHECK(said(checkBoards("25…Qxf3 trades the queens.", MoveFacts{}, queen)) == "Ok");
    CHECK(said(checkBoards("25…Qxf3 wins the queen for the queen.", MoveFacts{}, queen)) == "Ok");
    CHECK(said(checkBoards("25…Qxf3 wins the queen and a rook.", MoveFacts{}, queen)) ==
          R"(Material("the queen and a rook"))");
}

TEST_CASE("checker: the better move is not praised for what the move played does too") {
    // 30.Bxh7 takes the pawn that 30.Rg7 Re7 31.Rxe7 Nxe7 32.Bxh7 wins two moves later.
    const auto a = shown("4r3/1p5p/2nk1p2/p2p1B1p/3p1P2/5PR1/PP1K3P/8 w - - 0 30", "f5h7",
                         "e8e3 g3g6 d6c5 g6f6 e3f3 f6f8 f3f2 d2e1 f2h2 f8e8", 4,
                         "g3g7 e8e7 g7e7 c6e7 f5h7 h5h4 d2d3 d6c5 b2b3 a5a4", 4, Lang::Fr);
    MoveFacts facts;
    facts.missed    = {fact::WinsMaterial{White, 1, std::nullopt, std::nullopt}, fact::Endgame{White, Ending::Minor}};
    const auto says = [&](std::string_view text) { return said(checkBoards(text, facts, a)); };
    CHECK(says("Les Blancs prennent en h7. 30.Tg7 gagnait un pion et menait à une finale de pièces mineures.") ==
          R"(Reason("gagnait un pion"))");
    CHECK(says("30.Tg7 menait à une finale de pièces mineures.") == "Ok");
    // 27.Rxe8 is taken back at once, 27.Kxd4 wins a pawn: a difference.
    const auto b = shown("1n2r2r/1p5p/p2k1p2/3p1B1p/3p1P2/3K1P2/PP5P/4R1R1 w - - 4 27", "e1e8",
                         "h8e8 d3d4 e8e2 g1g7 e2b2 d4c3", 4, "d3d4 b8c6 d4d3 e8e7 a2a3 a6a5", 4, Lang::Fr);
    MoveFacts pawn;
    pawn.missed = {fact::WinsMaterial{White, 1, std::nullopt, std::nullopt}};
    CHECK(said(checkBoards("Grosse erreur : en prenant en e8, les Blancs laissent passer 27.Rxd4, qui gagnait un pion.",
                           pawn, b)) == "Ok");
}

TEST_CASE("checker: a position is called lost only beyond doubt") {
    const auto at = [](Color mover, double before, double after) {
        MoveReport m;
        m.mover      = mover;
        m.win_before = before;
        m.win_after  = after;
        return Lost::of(m);
    };
    // 23…Rf6 at −1.48: the engine's expected score gave Black 9.7 %, which the prompt calls lost.
    const auto lost = at(Black, 9.7, 3.5);
    CHECK(said(checkLost("La position des Noirs était perdue de toute façon, et 23…Tf6 ne change rien.", lost)) ==
          R"(Evaluation("perdue de toute façon"))");
    CHECK(said(checkLost("Black's position was lost anyway.", lost)) == R"(Evaluation("lost anyway"))");
    CHECK(said(checkLost("La position des Noirs reste perdue.", lost)) == R"(Evaluation("perdue"))");
    // Beyond saving, as the review says it, or mated.
    CHECK(said(checkLost("La position des Noirs était perdue de toute façon.", at(Black, 0.6, 0.4))) == "Ok");
    CHECK(said(checkLost("White is lost.", Lost::at(0.0))) == "Ok");
    CHECK(said(checkLost("White's position is lost.", Lost::at(1.0))) == "Ok");
    CHECK(said(checkLost("White is lost.", Lost::at(20.0))) == R"(Evaluation("lost"))");
    // Lost pieces, a lost advantage, are no lost position.
    CHECK(said(checkLost("The bishop is lost, and White has lost the advantage.", lost)) == "Ok");
    CHECK(said(checkLost("La pièce est perdue.", lost)) == "Ok");
}

TEST_CASE("checker: a king is never taken, won or given, and a word is not another") {
    CHECK(said(checkWording("La partie se termine par le mat : 42.Dxe8# donne le roi noir.", Lang::Fr)) ==
          R"(Wording("donne le roi"))");
    CHECK(said(checkWording("The game ends in mate: 42.Qxe8# takes the king on e8.", Lang::En)) ==
          R"(Wording("takes the king"))");
    CHECK(said(checkWording("Les Noirs concluent par le mat : 38…Dh5# met les Blancs hors d'état de parier.",
                            Lang::Fr)) == R"(Wording("parier"))");
    for (const auto* right : {"Le fou prend le roi et la tour en enfilade.", "Le coup donne de l'air au roi.",
                              "42.Dxe8# donne échec et mat au roi noir.", "Les Noirs ne peuvent plus parer le mat."})
        CHECK_MESSAGE(said(checkWording(right, Lang::Fr)) == "Ok", right);
    for (const auto* right : {"The move gives the king luft.", "White wins the king's pawn."})
        CHECK_MESSAGE(said(checkWording(right, Lang::En)) == "Ok", right);
}

TEST_CASE("checker: a brief reads each sentence against the lines its prompt shows") {
    // The prompt is the same; the lines it shows are what the checks read.
    const auto before = test::pos("4r3/1p5p/2nk1p2/p2p1B1p/3p1P2/5PR1/PP1K3P/8 w - - 0 30");
    const auto played = line(before, "f5h7").front();
    auto after        = before;
    after.playUnchecked(played);
    EngineLine best, reply;
    best.moves  = line(before, "g3g7 e8e7 g7e7 c6e7 f5h7 h5h4 d2d3 d6c5 b2b3 a5a4");
    best.score  = Score::cp(-240);
    best.depth  = 16;
    reply.moves = line(after, "e8e3 g3g6 d6c5 g6f6 e3f3 f6f8 f3f2 d2e1 f2h2 f8e8");
    reply.score = Score::cp(-356);
    reply.depth = 13;
    MoveReport report;
    report.mover      = White;
    report.phase      = Phase::Endgame;
    report.win_before = 1.5;
    report.win_after  = 0.2;
    report.judgment   = Judgment::Inaccuracy;
    MoveContext ctx;
    ctx.before = &before;
    ctx.played = played;
    ctx.report = &report;
    ctx.best   = &best;
    ctx.reply  = &reply;
    const Brief fr(ctx, Lang::Fr, PieceLetters::French);
    CHECK_MESSAGE(fr.prompt.find("Meilleur coup : 30.Tg7 ; la position des Blancs était perdue de toute façon.") !=
                      std::string::npos,
                  fr.prompt);
    CHECK(said(fr.check("Les Blancs prennent en h7. 30.Tg7 gagnait un pion.")) == R"(Reason("gagnait un pion"))");
    // At 1.5 % of the engine's expected score, White was lost beyond doubt; at 9 % the prompt still says so.
    CHECK(said(fr.check("Les Blancs prennent en h7 alors que la position était perdue de toute façon.")) == "Ok");
    report.win_before = 9.0;
    const Brief level(ctx, Lang::Fr, PieceLetters::French);
    CHECK(level.prompt == fr.prompt);
    CHECK(said(level.check("Les Blancs prennent en h7 alors que la position était perdue de toute façon.")) ==
          R"(Evaluation("perdue de toute façon"))");
}
