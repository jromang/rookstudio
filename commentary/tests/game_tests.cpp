// A reviewed game commented, and its story (the prototype's crates/commentary/src/tests.rs and story.rs, mod tests).

#include <doctest/doctest.hpp>

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include <commentary/phrases.hpp>
#include <commentary/story.hpp>
#include <commentary/writing.hpp>

#include "helpers.hpp"

using namespace fcg::commentary;

namespace {

using Ucis = std::initializer_list<std::string_view>;

PieceLetters letters(Lang lang) { return lang == Lang::En ? PieceLetters::English : PieceLetters::French; }

MainLine played(const Position& start, Ucis ucis) { return MainLine(start, test::moves(start, ucis)); }

EngineLine line(const Position& pos, const std::vector<std::string_view>& ucis, Score score) {
    EngineLine l;
    auto p = pos;
    for (const auto u : ucis) {
        const auto m = p.parseUci(u);
        REQUIRE_MESSAGE(m, std::string(u));
        l.moves.push_back(*m);
        p.playUnchecked(*m);
    }
    l.score = score;
    l.depth = 20;
    return l;
}

PositionAnalysis analysed(const MainLine& g, std::size_t ply,
                          std::initializer_list<std::pair<std::vector<std::string_view>, Score>> lines) {
    PositionAnalysis a;
    for (const auto& [ucis, score] : lines) a.lines.push_back(line(g.positions()[ply], ucis, score));
    return a;
}

// The template comments written at the move that led to `ply`.
std::vector<std::string> comments(const MainLine& g, const GameReport& report, const Analyses& a, Lang lang,
                                  std::size_t ply) {
    std::vector<std::string> out;
    for (const auto& [p, text] : templateComments(g, report, a, {}, nullptr, nullptr, lang, letters(lang))) {
        if (p == ply) out.push_back(text);
    }
    return out;
}

std::string joined(const std::vector<std::string>& texts, std::string_view with) {
    std::string out;
    for (std::size_t i = 0; i < texts.size(); ++i) out += (i ? std::string(with) : "") + texts[i];
    return out;
}

// The template comment of the first move of `g`, judged from `best` before it and `reply` after it, in both languages.
std::pair<std::string, std::string> judgedComment(const MainLine& g,
                                                  std::pair<std::vector<std::string_view>, Score> best,
                                                  std::pair<std::vector<std::string_view>, Score> reply) {
    Analyses a(g.positions().size());
    a[0]              = analysed(g, 0, {best});
    a[1]              = analysed(g, 1, {reply});
    const auto report = review(g, a, Opening{});
    return {joined(comments(g, report, a, Lang::Fr, 1), " | "), joined(comments(g, report, a, Lang::En, 1), " | ")};
}

Placed piece(Color color, Role role, Square square) { return Placed{color, role, square}; }

}  // namespace

TEST_CASE("game: fool's mate, the blunder") {
    const auto g = played(Position::initial(), {"f2f3", "e7e5", "g2g4", "d8h4"});
    Analyses a(g.positions().size());
    a[0]              = analysed(g, 0, {{{"e2e4"}, Score::cp(30)}});
    a[1]              = analysed(g, 1, {{{"e7e5"}, Score::cp(-20)}});
    a[2]              = analysed(g, 2, {{{"e2e4", "b8c6"}, Score::cp(-10)}});
    a[3]              = analysed(g, 3, {{{"d8h4"}, Score::mate(-1)}, {{"b8c6"}, Score::cp(50)}});
    a[4]              = PositionAnalysis::ended(GameEnd::checkmate(Color::Black));
    const auto report = review(g, a, Opening{});
    CHECK(comments(g, report, a, Lang::Fr, 3) ==
          std::vector<std::string>{"Permet le mat : 2…Dh4#. Mieux valait 2.e4."});
    CHECK(comments(g, report, a, Lang::En, 3) == std::vector<std::string>{"Allows mate: 2…Qh4#. 2.e4 was better."});
}

TEST_CASE("game: a winning fork is named") {
    const auto g = played(test::pos("r3k3/8/8/1N6/8/8/8/4K3 w - - 0 1"), {"b5c7"});
    Analyses a(g.positions().size());
    a[0]                = analysed(g, 0, {{{"b5c7", "e8d8", "c7a8"}, Score::cp(600)}});
    a[1]                = analysed(g, 1, {{{"e8d8", "c7a8"}, Score::cp(600)}});
    auto report         = review(g, a, Opening{});
    report.moves[0].key = true;
    CHECK(comments(g, report, a, Lang::Fr, 1) ==
          std::vector<std::string>{"Fourchette : le cavalier en c7 attaque la tour en a8 et le roi."});
}

TEST_CASE("game: every lasting feature can explain a mistake") {
    const Fact isolated              = fact::IsolatedPawn{piece(Color::White, Role::Pawn, Square::H2)};
    const std::vector<Fact> features = {
        fact::PassedPawn{piece(Color::Black, Role::Pawn, Square::G2)},
        isolated,
        fact::DoubledPawns{Color::White, File::F},
        fact::BishopPair{Color::Black},
        fact::RookOnOpenFile{piece(Color::Black, Role::Rook, Square::D8)},
        fact::RookOnSeventh{piece(Color::Black, Role::Rook, Square::H2)},
        fact::Outpost{piece(Color::Black, Role::Knight, Square::D4)},
        fact::ExposedKing{piece(Color::White, Role::King, Square::G1)},
        fact::LosesCastling{Color::White},
        fact::Promotes{Color::Black, Role::Queen, Square::E1},
        fact::PerpetualCheck{Color::Black},
        fact::Stalemate{Color::Black},
        fact::PawnBreak{piece(Color::Black, Role::Pawn, Square::F5), piece(Color::White, Role::Pawn, Square::E4),
                        std::nullopt},
        fact::DevelopmentLead{Color::Black},
        fact::PawnCentre{Color::Black},
        fact::Compensation{Color::Black, 1, std::nullopt},
        fact::Initiative{Color::Black},
        fact::Counterplay{Color::Black},
        fact::BackwardPawn{piece(Color::White, Role::Pawn, Square::D3)},
        fact::HangingPawns{Color::White, File::C},
        fact::BadBishop{piece(Color::White, Role::Bishop, Square::C1)},
        fact::ActiveKing{piece(Color::Black, Role::King, Square::D4)},
        fact::KingAttack{Color::Black},
        fact::MoreActive{Color::Black},
        fact::LongDiagonal{piece(Color::Black, Role::Bishop, Square::G7)},
        fact::CentreOpens{Color::Black},
        fact::Hole{Color::White, Square::D4},
    };
    for (const auto lang : {Lang::En, Lang::Fr}) {
        for (const auto& f : features) {
            const auto said = phrasebook(lang).punishment(f);
            CHECK_MESSAGE((said && said->ends_with('.')), debug(f));
        }
    }
    CHECK(phrasebook(Lang::Fr).punishment(isolated) == "Laisse le pion en h2 isolé.");
}

TEST_CASE("game: won material says what changed hands") {
    const auto won = [](Trade trade, int32_t pawns) -> Fact {
        return fact::WinsMaterial{Color::White, pawns, trade, std::nullopt};
    };
    const auto exchange        = won(Trade{{0, 0, 1, 0}, {0, 1, 0, 0}}, 2);
    const auto piece_for_pawns = won(Trade{{0, 1, 0, 0}, {2, 0, 0, 0}}, 1);
    CHECK(phrasebook(Lang::Fr).achieves(exchange) == "Gagne la qualité.");
    CHECK(phrasebook(Lang::En).achieves(exchange) == "Wins the exchange.");
    CHECK(phrasebook(Lang::Fr).achieves(piece_for_pawns) == "Gagne une pièce contre deux pions.");
    CHECK(phrasebook(Lang::En).achieves(piece_for_pawns) == "Wins a piece for two pawns.");
}

TEST_CASE("game: an only move says what the next best runs into") {
    // Without air for the king, the queen and rook mate on the back rank.
    const auto g = played(test::pos("4r1k1/4qppp/8/8/8/8/P4PPP/3Q2K1 w - - 0 1"), {"g2g3"});
    Analyses a(g.positions().size());
    a[0] = analysed(g, 0, {{{"g2g3", "e7e5"}, Score::cp(0)}, {{"a2a3", "e7e1", "d1e1", "e8e1"}, Score::mate(-2)}});
    a[1] = analysed(g, 1, {{{"e7e5"}, Score::cp(0)}});
    const auto report = review(g, a, Opening{});
    CHECK(report.moves[0].only_move);
    CHECK(comments(g, report, a, Lang::Fr, 1) ==
          std::vector<std::string>{"Seul coup : il donne de l'air au roi. 1.a3 permet un mat du couloir en 2 coups par "
                                   "1…De1+."});
    CHECK(comments(g, report, a, Lang::En, 1) ==
          std::vector<std::string>{"Only move: it gives the king luft. 1.a3 allows a back-rank mate in 2 starting with "
                                   "1…Qe1+."});
}

TEST_CASE("game: an only move that saves a piece says so") {
    // The queen is attacked by the knight; anything but a queen move loses it.
    const auto g = played(test::pos("4k3/8/2n5/8/3Q4/8/8/4K3 w - - 0 1"), {"d4h4"});
    Analyses a(g.positions().size());
    a[0] = analysed(g, 0, {{{"d4h4", "e8d7"}, Score::cp(600)}, {{"e1e2", "c6d4", "e2d3"}, Score::cp(-300)}});
    a[1] = analysed(g, 1, {{{"e8d7"}, Score::cp(600)}});
    const auto report = review(g, a, Opening{});
    CHECK(report.moves[0].only_move);
    const auto fr = joined(comments(g, report, a, Lang::Fr, 1), " ");
    CHECK_MESSAGE(fr.starts_with("Seul coup : la dame, attaquée en d4, se met à l'abri."), fr);
    const auto en = joined(comments(g, report, a, Lang::En, 1), " ");
    CHECK_MESSAGE(en.starts_with("Only move: the queen, attacked on d4, steps out of danger."), en);
}

TEST_CASE("game: a lost pawn ending is told by the square of the pawn") {
    // Trading rooks lets the b-pawn run: the black king is too far to catch it.
    const auto g = played(test::pos("8/8/6kp/rR4p1/P7/1P6/7P/6K1 b - - 4 44"), {"a5b5"});
    Analyses a(g.positions().size());
    a[0]              = analysed(g, 0, {{{"a5a6", "g1f2"}, Score::cp(0)}});
    a[1]              = analysed(g, 1, {{{"a4b5", "g6f5", "b5b6", "f5e4"}, Score::cp(900)}});
    const auto report = review(g, a, Opening{});
    CHECK(
        comments(g, report, a, Lang::Fr, 1) ==
        std::vector<std::string>{"Après 45.axb5, le pion ne peut plus être rattrapé : le roi est hors du carré. Mieux "
                                 "valait 44…Ta6."});
    CHECK(comments(g, report, a, Lang::En, 1) ==
          std::vector<std::string>{"After 45.axb5, the pawn can no longer be caught: the king is outside its square. "
                                   "44…Ra6 was better."});
}

TEST_CASE("game: the story of a game is told from its review") {
    const auto g = played(Position::initial(), {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5", "d2d3", "d7d6", "b1c3",
                                                "g8f6", "c1g5", "h7h6", "g5h4", "c8g4", "h2h3", "g4h5", "d1d2", "d8d7",
                                                "e1c1", "e8c8", "c1b1", "c8b8", "a2a3", "d7g4", "h3g4", "h5g4"});
    // The next move of the game from each position, else the first legal one.
    const auto next = [&](std::size_t ply) {
        const auto m = ply < g.moves().size() ? g.moves()[ply] : g.positions()[ply].legalMoves().front();
        return uci(m);
    };
    Analyses a(g.positions().size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        // Level until Black's queen goes to g4, where the h-pawn takes it.
        if (i == 23) {
            a[i] = analysed(g, i, {{{"d7e6"}, Score::cp(20)}});
        } else {
            const auto mv = next(i);
            a[i]          = analysed(g, i, {{{mv}, Score::cp(i >= 24 ? 900 : 20)}});
        }
    }
    const auto report = review(g, a, Opening{});
    REQUIRE(report.complete);
    const auto told = story(g, "1-0", report, a, nullptr, Lang::Fr, PieceLetters::French);
    REQUIRE(told.size() >= 3);
    std::string all;
    for (const auto& p : told) all += "\n" + p.text;
    CHECK_MESSAGE(told[0].text.ends_with("Au sortir de l'ouverture, la position est équilibrée."), all);
    CHECK_MESSAGE(told[1].text.starts_with("Le tournant est 12…Dg4??. "), all);
    CHECK_MESSAGE(told[1].text.ends_with("Mieux valait 12…De6."), all);
    CHECK(told[1].ply == 24);
    // The mistake that decided the game is a lesson by itself.
    CHECK(
        told[told.size() - 2].text ==
        "À travailler pour les Noirs : les pièces laissées en prise (12…Dg4?\?). Avant chaque coup, vérifier ce qu'il "
        "laisse sans défense.");
    CHECK(told.back().text == "Les Blancs l'emportent.");
    // The lessons on their own, as the review shows them apart: the same kind of mistake, its move, the advice.
    CHECK(told[told.size() - 2].lesson);
    CHECK_FALSE(told[1].lesson);
    const auto work = workOn(g, "1-0", report, a, nullptr, Lang::Fr, PieceLetters::French);
    REQUIRE(work.size() == 1);
    CHECK(work[0].side == Color::Black);
    REQUIRE(work[0].topics.size() == 1);
    CHECK(work[0].topics[0].name == "les pièces laissées en prise");
    CHECK(work[0].topics[0].plies == std::vector<std::size_t>{24});
    CHECK(work[0].advice == "Avant chaque coup, vérifier ce qu'il laisse sans défense.");
    const auto en = story(g, "1-0", report, a, nullptr, Lang::En, PieceLetters::English);
    REQUIRE(en.size() >= 3);
    CHECK(en.back().text == "White wins.");
    CHECK(en[en.size() - 2].text ==
          "For Black to work on: pieces left en prise (12…Qg4?\?). Before each move, check what it leaves unguarded.");
    CHECK_MESSAGE(en[1].text.starts_with("The turning point is 12…Qg4??. "), en[1].text);
    // A game too short, or not reviewed to its end, has no story yet.
    auto partial = a;
    partial[10].reset();
    CHECK(story(g, "1-0", review(g, partial, Opening{}), partial, nullptr, Lang::En, PieceLetters::English).empty());
    CHECK(workOn(g, "1-0", review(g, partial, Opening{}), partial, nullptr, Lang::En, PieceLetters::English).empty());
}

TEST_CASE("game: a quiet move that carries out a plan gets a word") {
    const std::vector<std::string_view> moves = {"e2e4", "e7e6", "d2d4", "d7d5", "e4e5", "c7c5", "c2c3", "b8c6"};
    const auto g = played(Position::initial(), {"e2e4", "e7e6", "d2d4", "d7d5", "e4e5", "c7c5", "c2c3", "b8c6"});
    Analyses a(g.positions().size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto next = i < moves.size() ? moves[i] : std::string_view("g1f3");
        a[i]            = analysed(g, i, {{{next}, Score::cp(20)}});
    }
    const auto report = review(g, a, Opening{});
    CHECK(comments(g, report, a, Lang::Fr, 5) ==
          std::vector<std::string>{"Chaîne de pions d4 et e5 : les Blancs ont de l'espace à l'aile roi, les Noirs s'en "
                                   "prennent à sa base en d4 ou à sa tête en e5."});
    CHECK(comments(g, report, a, Lang::En, 6) ==
          std::vector<std::string>{"The attack on the base of the pawn chain, on d4."});
    // The moves around them have nothing to say.
    CHECK(comments(g, report, a, Lang::En, 7).empty());
}

TEST_CASE("game: the motifs of the books are named") {
    {
        // 18…Nd7 puts a second thing on the queen's hands: 19.Rxd7 Qxd7 20.Qxa8+.
        const auto g = played(test::pos("r1q3k1/4bppp/pp2pn2/4B3/8/2N2Q2/PPPR1PPP/6K1 b - - 0 18"), {"f6d7"});
        const auto [fr, en] =
            judgedComment(g, {{"a8a7"}, Score::cp(0)}, {{"d2d7", "c8d7", "f3a8", "e7f8"}, Score::cp(600)});
        CHECK(
            fr ==
            "Après 19.Txd7, la dame en c8, qui défendait à la fois le cavalier en d7 et la tour en a8, est surchargée, "
            "ce qui coûte le cavalier en d7. Mieux valait 18…Ta7.");
        CHECK(en ==
              "After 19.Rxd7, the queen on c8, which guarded both the knight on d7 and the rook on a8, is overloaded, "
              "which costs the knight on d7. 18…Ra7 was better.");
    }
    {
        // 25.Ne6 lets the rook give itself to clear the long diagonal: 25…Rxe6 26.dxe6 Bd4.
        const auto g        = played(test::pos("4r1k1/p5bp/1q4p1/2pPrp2/1pP2N2/6PP/P4QB1/1R3RK1 w - - 2 25"), {"f4e6"});
        const auto [fr, en] = judgedComment(g, {{"f4d3"}, Score::cp(0)},
                                            {{"e5e6", "d5e6", "g7d4", "g1h1", "d4f2", "f1f2"}, Score::cp(-500)});
        CHECK(fr ==
              "Permet 25…T5xe6, qui dégage la diagonale pour le fou en g7. Mieux valait 25.Cd3, qui attaquait la tour "
              "en e5.");
        CHECK(en ==
              "Allows 25…R5xe6, which clears the diagonal for the bishop on g7. 25.Nd3 was better: it attacks the rook "
              "on e5.");
    }
    {
        // 1.Kg2 lets Black break through: 1…b3 2.axb3 c3 3.bxc3 a3.
        const auto g        = played(test::pos("7k/8/8/8/ppp5/8/PPP5/7K w - - 0 1"), {"h1g2"});
        const auto [fr, en] = judgedComment(
            g, {{"b2b3"}, Score::cp(0)}, {{"b4b3", "a2b3", "c4c3", "b2c3", "a4a3", "g2f3", "a3a2"}, Score::cp(-800)});
        CHECK(
            fr ==
            "Permet la percée 1…b3 : le pion en a4 passe. Mieux valait 1.b3, qui lançait la rupture contre le pion en "
            "a4.");
        CHECK(en ==
              "Allows the breakthrough 1…b3: the pawn on a4 gets through. 1.b3 was better: it starts the pawn break "
              "against the pawn on a4.");
    }
    {
        // Both sides run a pawn; 1.Ke4 loses a move, and the race.
        const auto g = played(test::pos("8/8/8/3K2kp/2P5/4P3/8/8 w - - 0 1"), {"d5e4"});
        const auto [fr, en] =
            judgedComment(g, {{"c4c5", "h5h4", "c5c6", "h4h3", "c6c7", "h3h2", "c7c8q"}, Score::cp(800)},
                          {{"h5h4", "c4c5", "h4h3", "c5c6", "h3h2", "c6c7", "h2h1q"}, Score::cp(-800)});
        CHECK(fr ==
              "Perd la course à la promotion après 1…h4 : l'adversaire fait dame le premier, avec échec. Mieux valait "
              "1.c5, qui gagnait la course à la promotion.");
        CHECK(en ==
              "Loses the race to promote after 1…h4: the opponent queens first, with check. 1.c5 was better: it wins "
              "the race to promote.");
    }
}

TEST_CASE("story: a mistake is filed by what it overlooked") {
    using story_detail::Motif;
    using story_detail::Oversight;
    using K         = Oversight::Kind;
    const Fact lost = fact::WinsMaterial{Color::White, 3, std::nullopt, std::nullopt};
    const Fact fork =
        fact::Fork{piece(Color::White, Role::Knight, Square::C7),
                   {piece(Color::Black, Role::Rook, Square::A8), piece(Color::Black, Role::King, Square::E8)}};
    const auto mate = [](std::optional<MatePattern> pattern) -> Fact { return fact::Mate{Color::White, 2, pattern}; };
    const auto punished = [](std::vector<Fact> by) {
        MoveFacts f;
        f.punished_by = std::move(by);
        return f;
    };
    CHECK(story_detail::oversight(punished({fork, lost}), true) == Oversight{K::Tactic, Motif::Fork});
    CHECK(story_detail::oversight(punished({mate(MatePattern::BackRank)}), false) ==
          Oversight{K::Tactic, Motif::BackRank});
    CHECK(story_detail::oversight(punished({mate(std::nullopt)}), false) == Oversight{K::King});
    // Material lost to a plain capture was en prise; to a quiet move, a tactic.
    CHECK(story_detail::oversight(punished({lost}), true) == Oversight{K::EnPrise});
    CHECK(story_detail::oversight(punished({lost}), false) == Oversight{K::Tactic});
    // Nothing punishes the move, and the better one won something: a chance missed.
    MoveFacts missed;
    missed.missed = {lost};
    CHECK(story_detail::oversight(missed, false) == Oversight{K::Missed});
    CHECK(story_detail::oversight(MoveFacts{}, false) == std::nullopt);
}
