// What a model is told and what it may write back (the prototype's crates/commentary/src/brief.rs, mod tests).

#include <doctest/doctest.hpp>

#include <functional>
#include <string>
#include <tuple>
#include <vector>

#include <commentary/brief.hpp>
#include <commentary/comment.hpp>

#include "helpers.hpp"

using namespace fcg::commentary;
using namespace fcg::commentary::brief_detail;

namespace {

constexpr auto White = Color::White;
constexpr auto Black = Color::Black;

Placed piece(Color color, Role role, Square square) { return Placed{color, role, square}; }

Allowed allowed() {
    Allowed a;
    a.moves   = {"16.Db8+", "Db8+"};
    a.squares = {"b8"};
    a.played  = {"15…Dc7", "Dc7"};
    return a;
}

// A check's verdict as the prototype's tests write it: Ok, or the refusal's debug text.
std::string said(const std::optional<Refusal>& r) { return r ? debug(*r) : "Ok"; }

PieceLetters letters(Lang lang) { return lang == Lang::En ? PieceLetters::English : PieceLetters::French; }

Position played(const std::vector<std::string_view>& ucis) {
    auto pos = Position::initial();
    for (const auto u : ucis) {
        const auto m = pos.parseUci(u);
        REQUIRE_MESSAGE(m, std::string(u));
        pos.playUnchecked(*m);
    }
    return pos;
}

Move uci(const Position& pos, std::string_view text) {
    const auto m = pos.parseUci(text);
    REQUIRE_MESSAGE(m, std::string(text));
    return *m;
}

using Surveyed = std::vector<std::tuple<std::string_view, std::vector<uint8_t>, std::optional<std::string>>>;

// What `read` makes of the novelty `played` after `moves`, with what theory says of it.
template <typename Read>
auto theorized(const std::vector<std::string_view>& moves, std::string_view played_uci,
               const std::function<Theory(const Position&, const Move&)>& make_theory, Read read) {
    const auto before = played(moves);
    const auto mv     = uci(before, played_uci);
    const auto theory = make_theory(before, mv);
    MoveReport report;
    report.mover      = before.turn();
    report.phase      = Phase::Opening;
    report.win_before = 45.0;
    report.win_after  = 45.0;
    report.novelty    = true;
    MoveContext ctx;
    ctx.before = &before;
    ctx.played = mv;
    ctx.report = &report;
    ctx.theory = &theory;
    return read(ctx);
}

// What `read` makes of a novelty played after `moves`, where the surveys play `theory`.
template <typename Read>
auto surveyed(const std::vector<std::string_view>& moves, std::string_view played_uci, const Surveyed& theory,
              Read read) {
    const auto surveys = [&](const Position& before, const Move& mv) {
        std::vector<std::tuple<Move, std::vector<uint8_t>, std::optional<std::string>>> noted;
        for (const auto& [m, nags, note] : theory) noted.emplace_back(uci(before, m), nags, note);
        return Theory::surveys(std::move(noted), mv);
    };
    return theorized(moves, played_uci, surveys, read);
}

// The Open Sicilian before 5th move for Black, and a novelty played there.
Brief sicilian(std::string_view played_uci, const Surveyed& theory, Lang lang) {
    const std::vector<std::string_view> open = {"e2e4", "c7c5", "g1f3", "d7d6", "d2d4", "c5d4", "f3d4", "g8f6", "b1c3"};
    return surveyed(open, played_uci, theory, [&](const MoveContext& ctx) { return Brief(ctx, lang, letters(lang)); });
}

// The brief of the move `played` after `moves`, with the engine's answer to it, its best line there, and what the
// review makes of the move: its judgment and how the mover stood before and after it.
Brief judged(const std::vector<std::string_view>& moves, std::string_view played_uci,
             const std::vector<std::string_view>& reply, const std::vector<std::string_view>& best,
             std::tuple<std::optional<Judgment>, double, double> verdict, Lang lang) {
    const auto before = played(moves);
    const auto mv     = uci(before, played_uci);
    const auto line   = [](const Position& start, const std::vector<std::string_view>& ucis) {
        auto pos = start;
        EngineLine l;
        for (const auto u : ucis) {
            l.moves.push_back(uci(pos, u));
            pos.playUnchecked(l.moves.back());
        }
        l.score = Score::cp(0);
        l.depth = 20;
        return l;
    };
    auto after = before;
    after.playUnchecked(mv);
    const auto reply_line = line(after, reply), best_line = line(before, best);
    const auto [judgment, win_before, win_after] = verdict;
    MoveReport report;
    report.mover      = before.turn();
    report.phase      = Phase::Middlegame;
    report.win_before = win_before;
    report.win_after  = win_after;
    report.judgment   = judgment;
    report.novelty    = !judgment;
    MoveContext ctx;
    ctx.before = &before;
    ctx.played = mv;
    ctx.report = &report;
    ctx.best   = best_line.moves.empty() ? nullptr : &best_line;
    ctx.reply  = &reply_line;
    return Brief(ctx, lang, letters(lang));
}

// An Italian game as far as 13.Nbd2: Black's d-pawn is about to open the d-file.
const std::vector<std::string_view> ITALIAN = {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5", "e1g1", "g8f6", "d2d3",
                                               "d7d6", "c2c3", "a7a5", "f1e1", "c5a7", "a2a4", "e8g8", "h2h3", "c8e6",
                                               "c4e6", "f7e6", "c1e3", "a7e3", "e1e3", "d6d5", "b1d2"};

const std::vector<std::string_view> SHORT_ITALIAN = {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5"};

std::vector<std::string_view> firstOf(const std::vector<std::string_view>& moves, std::size_t n) {
    return {moves.begin(), moves.begin() + static_cast<std::ptrdiff_t>(n)};
}

bool contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

}  // namespace

TEST_CASE("brief: the checker refuses unsupported claims") {
    MoveFacts facts;
    facts.punished_by.push_back(fact::Mate{White, 2, std::nullopt});
    const auto a    = allowed();
    const auto says = [&](std::string_view text) { return said(check(text, facts, a, Black)); };
    CHECK(says("Permet un mat en deux coups après 16.Db8+.") == "Ok");
    CHECK(says("Une fourchette décisive.") == R"(Motif("fourchette"))");
    CHECK(says("Après Cbxb tout s'effondre.") == R"(Notation("Cbxb"))");
    CHECK(says("Then Nf6 holds.") == R"(Notation("Nf6"))");
    // Sentences open with capitals that only look like moves.
    CHECK(says("The end. Check and mate after Db8+.") == "Ok");
    // Squares and captures cut short where digits were not allowed.
    CHECK(says("Le cavalier va en f.") == R"(Notation("f"))");
    CHECK(says("En capturant cxc, c'est fini.") == R"(Notation("cxc"))");
    // The black queen is in no fact and Black is the mover: allowed; a white one is not.
    CHECK(says("La dame noire est perdue.") == "Ok");
    const auto white_queen = check("La dame blanche se sacrifie.", facts, a, Black);
    REQUIRE(white_queen);
    CHECK((white_queen->kind == Refusal::Kind::Motif || white_queen->kind == Refusal::Kind::Colour));
    facts.punished_by.push_back(fact::Sacrifice{White, piece(White, Role::Queen, Square::B8)});
    CHECK(says("La dame blanche se sacrifie pour le mat.") == "Ok");
}

TEST_CASE("brief: lasting features need their facts") {
    MoveFacts facts;
    const auto a    = allowed();
    const auto says = [&](std::string_view text) { return said(check(text, facts, a, Black)); };
    CHECK(says("Le pion reste isolé.") == R"(Motif("isolé"))");
    CHECK(says("White takes the open h-file.") == R"(Unsupported("colonne"))");
    CHECK(says("Le roi reste sans abri.") == R"(Unsupported("abri"))");
    facts.punished_by.push_back(fact::IsolatedPawn{piece(Black, Role::Pawn, Square::H7)});
    facts.punished_by.push_back(fact::RookOnOpenFile{piece(White, Role::Rook, Square::B8)});
    CHECK(says("Le pion reste isolé.") == "Ok");
    // A file is named by its letter alone.
    CHECK(says("La tour prend la colonne b.") == "Ok");
    CHECK(says("White takes the open b-file.") == "Ok");
}

TEST_CASE("brief: every form of a motif word needs its fact") {
    const MoveFacts facts;
    const auto a    = allowed();
    const auto says = [&](std::string_view text) { return said(check(text, facts, a, Black)); };
    CHECK(says("It pins the knight.") == R"(Motif("clouage"))");
    CHECK(says("Le fou cloue le cavalier.") == R"(Motif("clouage"))");
    CHECK(says("Then the knight forks both.") == R"(Motif("fourchette"))");
    CHECK(says("Les Noirs sacrifient tout.") == R"(Motif("sacrifice"))");
    CHECK(says("A central push.") == R"(Unsupported("centre"))");
    // Words that only begin like one are not it.
    CHECK(says("Black loses material.") == "Ok");
    CHECK(says("Les Noirs perdent du matériel.") == "Ok");
}

TEST_CASE("brief: strategic words need the facts that carry them") {
    MoveFacts facts;
    const auto a    = allowed();
    const auto says = [&](std::string_view text) { return said(check(text, facts, a, Black)); };
    CHECK(says("Trop passif.") == R"(Unsupported("passif"))");
    CHECK(says("White takes the initiative.") == R"(Unsupported("initiative"))");
    CHECK(says("Les Blancs obtiennent du contre-jeu.") == R"(Unsupported("contre-jeu"))");
    facts.punished_by.push_back(fact::Initiative{White});
    facts.punished_by.push_back(fact::DevelopmentLead{White});
    CHECK(says("White takes the initiative.") == "Ok");
    CHECK(says("Les Blancs ont une avance de développement.") == "Ok");
}

TEST_CASE("brief: every fact has a sentence in the prompt") {
    const auto pawn               = piece(White, Role::Pawn, Square::D4);
    const std::vector<Fact> facts = {
        fact::PerpetualCheck{White},
        fact::Stalemate{Black},
        fact::Endgame{White, Ending::OppositeBishops},
        fact::PawnBreak{pawn, piece(Black, Role::Pawn, Square::E5), Lever{Lever::Kind::ChainBase}},
        fact::Space{White},
        fact::GainsSpace{pawn},
        fact::Opposition{White, true},
        fact::Outruns{pawn},
        fact::EntersSquare{piece(Black, Role::King, Square::D5), pawn},
        fact::CutsOff{piece(White, Role::Rook, Square::E2), piece(Black, Role::King, Square::F6), LineKind::File},
        fact::Majority{White, Wing::Queenside},
        fact::WeakColour{Black, true},
        fact::Luft{White},
        fact::KeySquare{piece(White, Role::King, Square::E6), pawn},
        fact::Lucena{White},
        fact::Philidor{piece(Black, Role::Rook, Square::H6)},
        fact::WrongBishop{piece(White, Role::Bishop, Square::D1), piece(White, Role::Pawn, Square::H4)},
        fact::DevelopmentLead{White},
        fact::PawnCentre{White},
        fact::Compensation{White, 2, std::nullopt},
        fact::Initiative{White},
        fact::Counterplay{Black},
        fact::BackwardPawn{pawn},
        fact::HangingPawns{White, File::C},
        fact::BadBishop{piece(White, Role::Bishop, Square::C1)},
        fact::ActiveKing{piece(White, Role::King, Square::E4)},
        fact::KingAttack{White},
        fact::MoreActive{White},
        fact::LongDiagonal{piece(White, Role::Bishop, Square::B2)},
        fact::CentreOpens{White},
        fact::CentreLocks{White},
        fact::Hole{Black, Square::D5},
    };
    std::vector<Fact> formations;
    for (const auto kind : ALL_FORMATIONS) formations.push_back(fact::Formation{kind, Black});
    for (const auto lang : {Lang::En, Lang::Fr}) {
        for (const auto* list : {&facts, static_cast<const std::vector<Fact>*>(&formations)}) {
            for (const auto& f : *list) CHECK_MESSAGE(factSentence(f, lang), debug(f));
        }
    }
    // A black owner has its formation told from its side of the board.
    CHECK(factSentence(formations[4], Lang::Fr) ==
          "chaîne de pions d5 et e4 : les Noirs ont de l'espace à l'aile roi, les Blancs s'en prennent à sa base en d5 "
          "ou à sa tête en e4");
    CHECK(factSentence(
              fact::CutsOff{piece(White, Role::Rook, Square::A5), piece(Black, Role::King, Square::F6), LineKind::Rank},
              Lang::En) == "the white rook on a5 cuts the black king off from the passed pawn, along the fifth rank");
    CHECK(factSentence(fact::HangingPawns{White, File::C}, Lang::Fr) ==
          "les pions blancs des colonnes c et d sont des pions pendants");
    CHECK(factSentence(fact::MoreActive{Black}, Lang::Fr) == "les pièces noires sont plus actives");
}

TEST_CASE("brief: the opening may be named") {
    auto a       = allowed();
    a.names      = {"Caro-Kann"};
    a.name_words = wordsOf("Sicilian Defense, Pin Variation");
    CHECK(fitsGrammar("Une nouveauté dans la Caro-Kann.", a, Lang::Fr));
    CHECK_FALSE(fitsGrammar("Une nouveauté dans la Nimzo-Indienne.", a, Lang::Fr));
    // A name says nothing about the position: "Pin" here is no pin.
    const MoveFacts none;
    CHECK(said(check("A novelty in the Pin Variation.", none, a, Black)) == "Ok");
}

TEST_CASE("brief: a tactic later in the line is told with its move") {
    // 13...d4 14.Re1 Nh5 15.g3 dxc3 16.bxc3 Qxd3: the pawn that takes on c3 uncovers the queen's attack on d3, two
    // moves after the one commented.
    const std::vector<std::string_view> reply = {"e3e1", "f6h5", "g2g3", "d4c3", "b2c3", "d8d3"};
    const auto brief = [&](Lang lang) { return judged(ITALIAN, "d5d4", reply, {}, {std::nullopt, 50.0, 50.0}, lang); };
    const auto fr    = brief(Lang::Fr);
    CHECK_MESSAGE(contains(fr.prompt, "attaque à la découverte sur le pion blanc en d3 après 15…dxc3 ; gagne un pion"),
                  fr.prompt);
    // The line is shown as far as that move, which may then be quoted.
    CHECK_MESSAGE(contains(fr.prompt, "(14.Te1 Ch5 15.g3 dxc3)"), fr.prompt);
    CHECK(said(fr.check("Après 15…dxc3, la dame attaque le pion d3 à la découverte et le gagne.")) == "Ok");
    const auto en = brief(Lang::En);
    CHECK_MESSAGE(contains(en.prompt, "discovered attack on the white pawn on d3 after 15…dxc3"), en.prompt);
    // What the first move of the answer does is told of the answer, with no move named.
    const auto punished = judged(firstOf(ITALIAN, 24), "b1a3", {"d5d4", "e3e1", "d4c3", "b2c3", "d8d3"}, {"b1d2"},
                                 {Judgment::Inaccuracy, 50.0, 40.0}, Lang::En);
    CHECK_MESSAGE(!contains(punished.prompt, " after 13…d4"), punished.prompt);
}

TEST_CASE("brief: nothing keeps a lost position") {
    const std::vector<std::string_view> reply = {"d5d4", "e3e1"};
    const auto brief                          = [&](Lang lang, double before, double after) {
        return judged(firstOf(ITALIAN, 24), "b1a3", reply, {"b1d2"}, {Judgment::Inaccuracy, before, after}, lang);
    };
    const auto fr = brief(Lang::Fr, 2.0, 0.0);
    CHECK_MESSAGE(contains(fr.prompt, "Verdict : imprécision. La position des Blancs reste perdue."), fr.prompt);
    CHECK_MESSAGE(contains(fr.prompt, "Meilleur coup : 13.Cbd2 ; la position des Blancs était perdue de toute façon."),
                  fr.prompt);
    const auto en = brief(Lang::En, 2.0, 0.0);
    CHECK_MESSAGE(contains(en.prompt, "Best move: 13.Nbd2; White's position was lost anyway."), en.prompt);
    // A position that stays winning is kept, as any other.
    const auto kept = brief(Lang::En, 99.0, 98.0);
    CHECK_MESSAGE(contains(kept.prompt, "Best move: 13.Nbd2, which keeps White's position winning."), kept.prompt);
}

TEST_CASE("brief: theory is told and its main move may be quoted") {
    const Surveyed surveys = {{"a7a6", {}, "The most popular move, aiming at e5 or e6."}, {"b8c6", {5}, std::nullopt}};
    // A move the surveys do not play: their main move, and their note on it.
    const auto left = sicilian("h7h6", surveys, Lang::En);
    CHECK_MESSAGE(contains(left.prompt,
                           "Theory: the game leaves the surveys' lines here; their main move is 5…a6.\n"
                           "Survey note on 5…a6: “The most popular move, aiming at e5 or e6.”"),
                  left.prompt);
    CHECK(left.hasSubstance());
    CHECK(said(left.check("A new move, where theory plays 5…a6 to aim at e5.")) == "Ok");
    CHECK(contains(left.prompt, "It is a novelty."));
    // A sideline, and the mark it gets: a novelty in the game, the surveys took it up since, which one line tells.
    const auto side = sicilian("b8c6", surveys, Lang::Fr);
    CHECK_MESSAGE(contains(side.prompt,
                           "Théorie : une nouveauté dans cette partie, que les études d'ouverture donnent depuis en "
                           "variante secondaire et jugent intéressant ; leur coup principal est 5…a6."),
                  side.prompt);
    CHECK(!contains(side.prompt, "Note des études"));
    CHECK(!contains(side.prompt, "C'est une nouveauté."));
    // The main move itself.
    const auto main = sicilian("a7a6", {{"a7a6", {1, 5}, std::nullopt}}, Lang::En);
    CHECK_MESSAGE(contains(main.prompt,
                           "Theory: a novelty in this game, since become the surveys' main move, which they call good "
                           "or interesting."),
                  main.prompt);
    CHECK(!contains(main.prompt, "It is a novelty."));
    // Without theory, a bare novelty has nothing for a model to say.
    CHECK_FALSE(sicilian("h7h6", {}, Lang::En).hasSubstance());
}

TEST_CASE("brief: a piece is put only where it stands") {
    const auto check_in = [](Lang lang, std::string_view text) {
        return surveyed(SHORT_ITALIAN, "d2d3", {{"e1g1", {}, std::nullopt}},
                        [&](const MoveContext& ctx) { return said(Brief(ctx, lang, letters(lang)).check(text)); });
    };
    // Where the move lands, where a piece stood, with the right colour.
    CHECK(check_in(Lang::En, "A new move: the pawn on d3 holds.") == "Ok");
    CHECK(check_in(Lang::En, "A new move: the d3 pawn holds.") == "Ok");
    CHECK(check_in(Lang::En, "A new move, the white king on e1 waits.") == "Ok");
    CHECK(check_in(Lang::Fr, "Une nouveauté : le roi blanc en e1 attend.") == "Ok");
    CHECK(check_in(Lang::En, "A new move: the knight on d3 holds.") == R"(Piece("knight on d3"))");
    CHECK(check_in(Lang::En, "A new move: the d3 knight holds.") == R"(Piece("d3 knight"))");
    CHECK(check_in(Lang::Fr, "Une nouveauté : le roi noir en e1 attend.") == R"(Piece("roi noir en e1"))");
    // Where a piece goes is not where it stands, nor is a promotion square.
    CHECK(check_in(Lang::En, "A new move, the queen goes to d3 later.") == "Ok");
    CHECK(placedIn("The pawn threatens to queen on f8.").empty());
    CHECK(placedIn("Le pion en b8 fait dame, le pion d1 aussi.").empty());
    CHECK(placedIn("Il fourche la dame et la tour sur c6.").empty());
}

TEST_CASE("brief: a novelty names the usual move") {
    const auto said_in = [](Lang lang) {
        return surveyed(SHORT_ITALIAN, "d2d3", {{"e1g1", {}, std::nullopt}},
                        [&](const MoveContext& ctx) { return comment(ctx, lang, letters(lang)); });
    };
    CHECK(said_in(Lang::Fr) == "Nouveauté ; la ligne principale est 4.O-O.");
    CHECK(said_in(Lang::En) == "A novelty; the main line is 4.O-O.");
    // Without the surveys, the move the reference games played.
    const auto games = [](const Position& before, const Move&) { return Theory::played({uci(before, "e1g1")}); };
    const auto played_instead = theorized(
        SHORT_ITALIAN, "d2d3", games, [](const MoveContext& ctx) { return comment(ctx, Lang::En, letters(Lang::En)); });
    CHECK(played_instead == "A novelty; the usual move is 4.O-O.");
}

TEST_CASE("brief: a survey note supports what it names") {
    const auto note  = std::string("Black gets counterplay on the queenside.");
    const auto brief = sicilian("h7h6", {{"a7a6", {}, note}}, Lang::Fr);
    CHECK(said(brief.check("Une nouveauté : la théorie joue 5…a6, avec du contre-jeu.")) == "Ok");
    const auto bare = sicilian("h7h6", {{"a7a6", {}, std::nullopt}}, Lang::Fr);
    CHECK(said(bare.check("Une nouveauté : la théorie joue 5…a6, avec du contre-jeu.")) ==
          R"(Unsupported("contre-jeu"))");
}

TEST_CASE("brief: French elides before a vowel") {
    for (const auto* wrong :
         {"Une nouveauté au lieu du habituel 5…Cf6.", "Le échec est fatal.", "Il prend de un coup le pion."}) {
        const auto r = checkElision(wrong);
        CHECK_MESSAGE((r && r->kind == Refusal::Kind::Elision), wrong);
    }
    for (const auto* right : {"Au lieu de l'habituel 5…Cf6, le pion de e4 avance.",
                              "La colonne de a est ouverte, et le héros de la partie est le fou.",
                              "Un coup que Une telle position appelle."}) {
        CHECK_MESSAGE(said(checkElision(right)) == "Ok", right);
    }
}

TEST_CASE("brief: theory castling may be called so") {
    const auto brief = surveyed(SHORT_ITALIAN, "d2d3", {{"e1g1", {}, std::nullopt}},
                                [](const MoveContext& ctx) { return Brief(ctx, Lang::En, letters(Lang::En)); });
    CHECK(said(brief.check("A new move, where theory castles with 4.O-O.")) == "Ok");
    const auto other = sicilian("h7h6", {{"a7a6", {}, std::nullopt}}, Lang::En);
    CHECK(said(other.check("A new move, where 5…a6 was played before castling.")) == R"(Motif("roque"))");
}

TEST_CASE("brief: only plain English notes are quoted") {
    CHECK(noteShown("with very good counterplay for Black:") == "with very good counterplay for Black.");
    CHECK(noteShown("The f7 square is now open for the knight.") == "The f7 square is now open for the knight.");
    // Games, moves, results, German, scraps.
    for (const auto* refused : {
             "Lemoine,S (2285)-Garnier,A (2355) Tours 1998",
             "is the main move. See Paul Lemoine-Garnier, Tours 2023",
             "11...Bxe6 is the main alternative here.",
             "with a pawn given back, unter Rückgabe des Bauern",
             "½-½ Lemoine,O-Garnier,W Tours",
             "is better.",
             "See the game Smith-Jones (in this database).",
             "We are back in the main line. Wir sind wieder in der Hauptvariante.",
             // Unfinished, a sign left out, a survey's symbols.
             "Until now White has mostly tried",
             "White could claim a slight advantage by:",
             "The  is slightly misplaced on this square.",
             "/\\d4|^ costs Black precious time.",
         }) {
        CHECK_MESSAGE(noteShown(refused) == std::nullopt, refused);
    }
    // The unfinished sentence goes, the finished one stays.
    CHECK(noteShown("The queen will have to leave sooner or later. After") ==
          "The queen will have to leave sooner or later.");
    // Two spaces after a full stop are a typist's habit, not a sign left out.
    CHECK(noteShown("Black is fine.  White has no edge here.") == "Black is fine. White has no edge here.");
    // A long note is cut after a sentence that fits.
    std::string long_note = "White keeps a small edge. ";
    for (int i = 0; i < 12; ++i) long_note += "And then more words follow. ";
    const auto cut = noteShown(long_note);
    REQUIRE(cut);
    CHECK(cut->size() <= NOTE_LENGTH);
    CHECK(cut->starts_with("White keeps a small edge."));
}

TEST_CASE("brief: a move is the only one when the facts say so") {
    CHECK(said(checkOnly("27…Kf8 was the only way to keep the position.", false)) == R"(Verdict("only move"))");
    CHECK(said(checkOnly("Seul bon coup : la dame prend en g2.", false)) == R"(Verdict("seul coup"))");
    CHECK(said(checkOnly("Only move to keep the balance.", true)) == "Ok");
    // "Only" and « seule » about something else.
    CHECK(said(checkOnly("The only piece left is the knight.", false)) == "Ok");
    CHECK(said(checkOnly("Seule la dame défend le roi.", false)) == "Ok");
}

TEST_CASE("brief: only a move back is a retreat") {
    // Nc6-a5 for Black goes forward.
    for (const auto* wrong :
         {"Black's knight retreat to a5 walks into a pin.", "Retreating to a5 lets White off the hook.",
          "Le repli du cavalier en a5 perd une pièce.", "Le cavalier recule en a5 et perd une pièce."}) {
        const auto r = checkRetreat(wrong, std::string("a5"));
        CHECK_MESSAGE((r && r->kind == Refusal::Kind::Unsupported), wrong);
    }
    // A piece of the line may have to retreat; a move back may be called one.
    CHECK(said(checkRetreat("9…h6 hits the bishop, which has to retreat.", std::string("a5"))) == "Ok");
    CHECK(said(checkRetreat("Le fou doit reculer après 10.Fh4.", std::string("a5"))) == "Ok");
    CHECK(said(checkRetreat("Black's knight retreat to a5.", std::nullopt)) == "Ok");
}

TEST_CASE("brief: the centre opens or locks as the facts say") {
    const Fact locks = fact::CentreLocks{White};
    const Fact opens = fact::CentreOpens{White};
    const std::vector<const Fact*> only_locks{&locks}, only_opens{&opens}, both{&locks, &opens};
    for (const auto* wrong :
         {"Il fallait 22.d5, qui ouvre le centre.", "22.d5 was better, after which the centre opens."})
        CHECK_MESSAGE(checkCentre(wrong, only_locks), wrong);
    CHECK(said(checkCentre("14…exd4 bloque le centre.", only_opens)) == R"(Unsupported("bloque le centre"))");
    // As the facts say, denied, or both when each line has its own.
    for (const auto* right : {"Il fallait 22.d5, qui bloque le centre.", "Il fallait 22.d5, sans ouvrir le centre.",
                              "Le centre ne s'ouvre pas."})
        CHECK_MESSAGE(said(checkCentre(right, only_locks)) == "Ok", right);
    CHECK(said(checkCentre("22.d5 opens the centre.", both)) == "Ok");
}

TEST_CASE("brief: a move does not just prepare what follows") {
    for (const auto* wrong :
         {"Le fou en e6 prépare la suite.", "15.g4 prépare la suite de l'attaque.", "10.h3 prepares the ground.",
          // French has no such verb for a fork.
          "Le fou fourche les deux tours."}) {
        const auto r = checkFiller(wrong);
        CHECK_MESSAGE((r && r->kind == Refusal::Kind::Unsupported), wrong);
    }
    CHECK(said(checkFiller("Le pion prépare la promotion en c8.")) == "Ok");
    CHECK(said(checkFiller("Une fourchette gagne la tour.")) == "Ok");
}

TEST_CASE("brief: what the prompt says may be said back") {
    MoveFacts facts;
    const auto a    = allowed();
    const auto says = [&](std::string_view text, const MoveFacts& f) { return said(check(text, f, a, Black)); };
    // A piece moved out of danger, pawns thrown at a castled king, a pawn named by its file that promotes: the words of
    // their facts' own sentences.
    CHECK(says("Il fallait mettre la tour à l'abri.", facts) == R"(Unsupported("abri"))");
    facts.missed.push_back(fact::Saves{piece(Black, Role::Rook, Square::C8)});
    facts.missed.push_back(fact::PawnStorm{Black});
    facts.missed.push_back(fact::Promotes{Black, Role::Queen, Square::G1});
    CHECK(says("Il fallait mettre la tour à l'abri.", facts) == "Ok");
    CHECK(says("Les Noirs lancent leurs pions contre le roque adverse.", facts) == "Ok");
    CHECK(says("Le pion g, passé, ira à promotion.", facts) == "Ok");
    // A skewer along a file may name the file; an idiom is not a key square.
    const auto* file = "Le roi se place sur la colonne de la tour.";
    CHECK(says(file, facts) == R"(Unsupported("colonne"))");
    facts.punished_by.push_back(fact::Skewer{piece(White, Role::Rook, Square::D1), piece(Black, Role::King, Square::D7),
                                             piece(Black, Role::Rook, Square::D8)});
    CHECK(says(file, facts) == "Ok");
    CHECK(says("Un pion tombe, avec une promotion à la clé.", facts) == "Ok");
    CHECK(says("Le roi atteint une case clé.", facts) == R"(Motif("case clé"))");
    // The move played, when it castles, may be called so.
    const auto* castled = "Le roque perd un pion.";
    const MoveFacts none;
    CHECK(says(castled, none) == R"(Motif("roque"))");
    auto castling = allowed();
    castling.moves.emplace_back("17.O-O");
    CHECK(said(check(castled, none, castling, Black)) == "Ok");
}

TEST_CASE("brief: the change of evaluation is said plainly") {
    using enum Standing;
    const auto say = [](Standing before, Standing after) { return change(before, after, "White's", Lang::En); };
    CHECK(say(Balanced, Balanced) == "White's position stays balanced.");
    CHECK(say(ClearlyBetter, SlightlyBetter) == "White's position is still slightly better, no longer clearly better.");
    CHECK(say(SlightlyBetter, Balanced) == "White's position was slightly better and is now balanced.");
    CHECK(say(SlightlyWorse, ClearlyWorse) == "White's position was slightly worse and is now clearly worse.");
    CHECK(change(Winning, ClearlyBetter, "des Blancs", Lang::Fr) ==
          "La position des Blancs reste nettement meilleure, mais n'est plus gagnante.");
}

TEST_CASE("brief: evaluation words agree with the evaluation") {
    using enum Standing;
    const auto says = [](std::string_view text, Standing before, Standing after) {
        return said(checkStanding(text, std::pair{before, after}));
    };
    // An edge before, balance after: the best move kept the edge, not the balance.
    CHECK(says("White lets the advantage slip.", SlightlyBetter, Balanced) == "Ok");
    CHECK(says("The position is now balanced.", SlightlyBetter, Balanced) == "Ok");
    CHECK(says("17.Bb3 kept the balance.", SlightlyBetter, Balanced) == R"(Evaluation("balance"))");
    CHECK(says("17.Bb3 kept the balance.", Balanced, SlightlyWorse) == "Ok");
    // Still better after: the advantage is not lost.
    CHECK(says("Les Blancs perdent l'avantage.", ClearlyBetter, SlightlyBetter) == R"(Evaluation("avantage"))");
    CHECK(says("Black keeps the balance.", ClearlyBetter, SlightlyBetter) == R"(Evaluation("balance"))");
    // Without a verdict, no evaluation to speak of.
    CHECK(checkStanding("A balanced novelty.", std::nullopt));
    CHECK(said(checkStanding("A novelty.", std::nullopt)) == "Ok");
}

TEST_CASE("brief: a comment talks about the game") {
    const MoveFacts facts;
    const auto a    = allowed();
    const auto says = [&](std::string_view text) { return said(check(text, facts, a, Black)); };
    CHECK(says("A novelty; the facts give no further assessment.") == R"(Aside("facts"))");
    CHECK(says("I'd call it a novelty.") == R"(Aside("i"))");
    CHECK(says("Une nouveauté, j'imagine.") == R"(Aside("j"))");
    CHECK(says("Une nouveauté.") == "Ok");
}

TEST_CASE("brief: only the review's verdict may be named") {
    const std::optional<Judgment> blunder = Judgment::Blunder;
    CHECK(said(checkVerdict("Grosse erreur : la dame tombe.", blunder)) == "Ok");
    CHECK(said(checkVerdict("A blunder that drops the queen.", blunder)) == "Ok");
    CHECK(said(checkVerdict("A mistake that drops the queen.", blunder)) == R"(Verdict("mistake"))");
    CHECK(said(checkVerdict("Une erreur, pas une imprécision.", Judgment::Mistake)) == R"(Verdict("inaccuracy"))");
    CHECK(said(checkVerdict("Seul coup, sans erreur possible.", std::nullopt)) == R"(Verdict("mistake"))");
    CHECK(said(checkVerdict("The only move.", std::nullopt)) == "Ok");
}

TEST_CASE("brief: the grammar is held to without the model") {
    const auto a    = allowed();
    const auto fits = [&](std::string_view text, Lang lang) { return fitsGrammar(text, a, lang); };
    CHECK(fits("Seul coup : après 16.Db8+, la dame tombe.", Lang::Fr));
    CHECK(fits("The only move: after Db8+ the queen falls. It wins!", Lang::En));
    // A digit outside a listed move, a fourth sentence, French spacing in English.
    CHECK_FALSE(fits("Mate in 2.", Lang::En));
    CHECK(fits("One. Two. Three.", Lang::En));
    CHECK_FALSE(fits("One. Two. Three. Four.", Lang::En));
    CHECK_FALSE(fits("The only move : it holds.", Lang::En));
    CHECK_FALSE(fits("Le seul coup: il tient.", Lang::Fr));
    // No ending, a capture-like word, other punctuation.
    CHECK_FALSE(fits("The only move", Lang::En));
    CHECK_FALSE(fits("Nxe wins.", Lang::En));
    CHECK_FALSE(fits("It wins (a lot).", Lang::En));
    // A word may start with a capital and an "x" when it goes on: an opening's name.
    CHECK(fits("A novelty in the Exchange Variation.", Lang::En));
    CHECK_FALSE(fits("Bxef wins.", Lang::En));
}

TEST_CASE("brief: a move may be written without its number or sign") {
    CHECK(forms("41…Txe2#") == std::vector<std::string>{"41…Txe2#", "41…Txe2", "Txe2#", "Txe2"});
    CHECK(forms("12.O-O") == std::vector<std::string>{"12.O-O", "O-O"});
}

TEST_CASE("brief: the grammar lists the allowed tokens") {
    const auto g = allowed().grammar(Lang::Fr);
    CHECK(contains(g, "move ::= \"16.Db8+\" | \"Db8+\""));
    CHECK(contains(g, "square ::= \"b8\""));
    CHECK(contains(g, R"(sep ::= " " | ", " | " : " | " ; ")"));
    CHECK(contains(allowed().grammar(Lang::En), R"(end ::= "." | "!")"));
    // No name allowed: a rule that matches nothing a model would write.
    CHECK(contains(g, "name ::= \"\\u0001\""));
}

TEST_CASE("brief: pieces take their article") {
    const auto queen  = piece(White, Role::Queen, Square::D1);
    const auto knight = piece(Black, Role::Knight, Square::F6);
    CHECK(ofThePiece(queen, Lang::Fr) == "de la dame blanche en d1");
    CHECK(ofThePiece(knight, Lang::Fr) == "du cavalier noir en f6");
    CHECK(thePiece(knight, Lang::En) == "the black knight on f6");
}

TEST_CASE("brief: the motifs of the books are told and gated") {
    const Fact overloaded =
        fact::Overloaded{piece(Black, Role::Queen, Square::C8), piece(Black, Role::Knight, Square::D7),
                         piece(Black, Role::Rook, Square::A8)};
    const Fact cleared = fact::Clearance{piece(Black, Role::Rook, Square::E5), piece(Black, Role::Bishop, Square::G7),
                                         LineKind::Diagonal};
    const Fact queening =
        fact::Clearance{piece(White, Role::Rook, Square::B8), piece(White, Role::Pawn, Square::B7), std::nullopt};
    const Fact broken = fact::Breakthrough{piece(White, Role::Pawn, Square::B6), piece(White, Role::Pawn, Square::C5)};
    const Fact race   = fact::Race{Black, true, false};
    CHECK(factSentence(overloaded, Lang::Fr) ==
          "surcharge : la dame noire en c8 défendait à la fois le cavalier noir en d7 et la tour noire en a8");
    CHECK(factSentence(cleared, Lang::En) ==
          "clearance: the black rook on e5 clears the diagonal for the black bishop on g7");
    CHECK(factSentence(queening, Lang::Fr) ==
          "dégagement : la tour blanche en b8 libère la case de promotion du pion blanc en b7");
    CHECK(factSentence(broken, Lang::Fr) ==
          "percée : le pion blanc en b6 est offert pour faire passer le pion blanc en c5");
    CHECK(factSentence(race, Lang::En) == "race to promote: Black queens first, with check");
    // Their words need them.
    const auto a = allowed();
    MoveFacts facts;
    const auto says = [&](std::string_view text) { return said(check(text, facts, a, White)); };
    for (const auto& [text, word] : std::vector<std::pair<const char*, const char*>>{
             {"La dame est surchargée.", "surcharge"},
             {"The queen is overloaded.", "surcharge"},
             {"Un dégagement décisif.", "dégagement"},
             {"The rook clears the way.", "dégagement"},
             {"Une percée gagne.", "percée"},
             {"A breakthrough wins.", "percée"},
             {"Les Noirs gagnent la course.", "course"},
             {"Black wins the race.", "course"},
         }) {
        CHECK_MESSAGE(says(text) == "Motif(\"" + std::string(word) + "\")", text);
    }
    facts.punished_by = {overloaded, cleared, broken, race};
    for (const auto* text :
         {"La dame est surchargée.", "The queen is overloaded.", "Un dégagement décisif sur la diagonale.",
          "The rook clears the diagonal.", "Une percée gagne.", "A breakthrough wins.",
          "Les Noirs gagnent la course à la promotion.", "Black wins the race to promote."}) {
        CHECK_MESSAGE(says(text) == "Ok", text << ": " << says(text));
    }
}
