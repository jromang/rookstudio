// The templates' sentences read with the checks of the boards (#64), as a model's are. The templates are the
// prototype's, held by the golden files: where the facts they are written from are false, they say false things,
// which are left out of what is shown and written; the true sentences of the same positions are kept. The positions
// and the engine's lines are those of games reviewed for #64: the US championship 2026 (So–Shankland,
// Shankland–Caruana) and a Lichess game of January 2013 (CC0).

#include <doctest/doctest.hpp>

#include <algorithm>
#include <deque>
#include <string>
#include <vector>

#include <commentary/brief.hpp>
#include <commentary/comment.hpp>
#include <commentary/commentator.hpp>
#include <commentary/review.hpp>
#include <commentary/story.hpp>
#include <commentary/story_model.hpp>
#include <commentary/text.hpp>
#include <commentary/writing.hpp>

#include "helpers.hpp"

using namespace fcg::commentary;

namespace {

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

EngineLine engineLine(const Position& from, std::string_view ucis, int32_t white_cp) {
    EngineLine l;
    l.moves = line(from, ucis);
    l.score = Score::cp(white_cp);
    l.depth = 30;
    return l;
}

// Moves of a game from the position `fen`, reviewed with the engine's line in each position (White's centipawns), as
// the review of the game had them.
struct Reviewed {
    MainLine game;
    Analyses analyses;
    GameReport report;
    [[nodiscard]] MoveContext context(std::size_t ply = 1) const {
        return moveContext(game, *report.get(ply), analyses, nullptr, nullptr, nullptr);
    }
};

Reviewed reviewed(std::string_view fen, std::string_view moves,
                  const std::vector<std::pair<std::string_view, int32_t>>& lines) {
    const auto start = test::pos(fen);
    MainLine game(start, line(start, moves));
    Analyses analyses;
    for (std::size_t i = 0; i < lines.size(); ++i)
        analyses.push_back(PositionAnalysis{{engineLine(game.positions()[i], lines[i].first, lines[i].second)}, {}});
    auto report = review(game, analyses, Opening{});
    REQUIRE(report.get(1));
    return Reviewed{std::move(game), std::move(analyses), std::move(report)};
}

// So–Shankland, 29.Re3?!: 29…Qf7 prepares the discovered attack on f2 that 31…Nd5 makes.
Reviewed rookToE3() {
    return reviewed("5r1k/1pp1q1pp/6r1/p3P3/P3Rn2/2P2N1P/2Q2PPK/6R1 w - - 7 29", "e4e3",
                    {{"f3d4 g6h6 d4e2 f4e6 g1f1 g7g5 e2d4 e6f4 c2d2 h6h4 f1e1 c7c5 d4f3 f8d8 d2c1 h4h6 c3c4 d8f8", 234},
                     {"e7f7 f3d4 c7c5 d4b5 f4d5 e3e4 f7f2 c2f2 f8f2 e5e6 d5e7 b5d6 h7h6 d6b7 g6g5", 169}});
}

// Shankland–Caruana, 29…a5?! 30.Bxh7?!: 30.Rg7 wins a pawn, which 30.Bxh7 takes too.
Reviewed pawnA5() {
    return reviewed(
        "4r3/1p5p/p1nk1p2/3p1B1p/3p1P2/5PR1/PP1K3P/8 b - - 3 29", "a6a5 f5h7",
        {{"e8e3 f5h7 a6a5 h2h4 d6c5 g3g6 e3f3 g6f6 f3f2 d2e1 f2b2 f6e6 b2h2 f4f5 h2h4 f5f6 h4f4 h7g8 h5h4 f6f7 c6d8 "
          "e6e8",
          -348},
         {"g3g7 e8e7 g7e7 c6e7 f5h7 h5h4 d2d3 d6c5 b2b3 a5a4 b3a4 e7c6 d3d2 c6a5 h7f5 a5c4 d2e2 c4e3 f5d7 e3g2 a4a5",
          -240},
         {"e8e3 g3g6 d6c5 g6f6 e3f3 f6f8 f3f2 d2e1 f2h2 f8e8 h2b2 f4f5 b2b1", -356}});
}

// crazy2013–cougar1 (Lichess, January 2013), from 29…dxc4?? to the mate: the doubled c-pawns are undone at once by
// the recapture 30.Bxc4. Then White's small advantage holds until Black walks into the mate.
Reviewed takesOnC4() {
    const auto start = test::pos("r5k1/1pp3pp/4p1r1/3p1p2/1PPP1P1q/p2BP3/P7/B1KQ1R2 b - - 0 29");
    MainLine game(start, line(start,
                              "d5c4 d3c4 a8d8 d1b3 h4g3 b3a3 g3e3 a3e3 h7h5 c4e6 g8h8 d4d5 c7c6 f1d1 c6d5 "
                              "d1d5 d8f8 e6f5 g6g2 e3h3 g2g1 c1b2 f8e8 h3h5 h8g8 h5e8"));
    const auto& ps = game.positions();
    Analyses analyses(ps.size());
    analyses[0] = PositionAnalysis{
        {engineLine(ps[0], "h4h2 c1b1 c7c6 d1e1 h7h5 f1h1 h2g3 a1c3 h5h4 e1g3 h4g3 h1g1 g3g2 b1c1 g8f7 c1d2 a8h8 d2e2",
                    -157)},
        {}};
    analyses[1] = PositionAnalysis{{engineLine(ps[1],
                                               "d3c4 c7c6 a1c3 h4h2 c3d2 h2g2 f1e1 b7b5 c4b3 g2e4 d1c2 e4c2 c1c2 g8f8 "
                                               "e1h1 h7h6 c2d1 f8e7 h1h2 g6g1 d2e1 g1f1",
                                               57)},
                                   {}};
    for (std::size_t i = 2; i + 2 < ps.size(); ++i)
        analyses[i] = PositionAnalysis{{engineLine(ps[i], uci(game.moves()[i]), 57)}, {}};
    EngineLine mate         = engineLine(ps[ps.size() - 2], "h5e8", 0);
    mate.score              = Score::mate(1);
    analyses[ps.size() - 2] = PositionAnalysis{{mate}, {}};
    analyses.back()         = PositionAnalysis::ended(GameEnd::checkmate(Color::White));
    auto report             = review(game, analyses, Opening{});
    REQUIRE(report.complete);
    return Reviewed{std::move(game), std::move(analyses), std::move(report)};
}

// A model that gives its answers in turn, and none once they run out.
class Scripted final : public Model {
   public:
    explicit Scripted(std::deque<std::string> answers) : answers_(std::move(answers)) {}
    std::optional<Generation> generate(const Request&, std::string* error) override {
        if (answers_.empty()) {
            if (error) *error = "no more answers";
            return std::nullopt;
        }
        Generation g;
        g.text = answers_.front();
        answers_.pop_front();
        return g;
    }

   private:
    std::deque<std::string> answers_;
};

}  // namespace

TEST_CASE("templates: a sentence the checks of the boards refuse is left out, the true ones kept") {
    {
        // The discovered attack told right after 29…Qf7, which only prepares it.
        const auto r   = rookToE3();
        const auto ctx = r.context();
        const Brief en(ctx, Lang::En, PieceLetters::English);
        const auto text = comment(ctx, Lang::En, PieceLetters::English);
        REQUIRE(text);
        CHECK(*text == "After 29…Qf7, a discovered attack hits the pawn on f2. 29.Nd4 was better.");
        CHECK(said(en.checkTemplate("After 29…Qf7, a discovered attack hits the pawn on f2.")) ==
              R"(Board("discovered"))");
        CHECK(said(en.checkTemplate("29.Nd4 was better.")) == "Ok");
        CHECK(en.heldTemplate(*text) == "29.Nd4 was better.");
        const Brief fr(ctx, Lang::Fr, PieceLetters::French);
        const auto texte = comment(ctx, Lang::Fr, PieceLetters::French);
        REQUIRE(texte);
        CHECK(*texte == "Après 29…Df7, une attaque à la découverte vise le pion en f2. Mieux valait 29.Cd4.");
        CHECK(fr.heldTemplate(*texte) == "Mieux valait 29.Cd4.");
    }
    {
        // The better move praised for the pawn the move played takes too: the comment has nothing left to say. The
        // move before, whose comment is true, keeps it whole.
        const auto r   = pawnA5();
        const auto ctx = r.context(2);
        const Brief en(ctx, Lang::En, PieceLetters::English);
        const auto text = comment(ctx, Lang::En, PieceLetters::English);
        REQUIRE(text);
        CHECK(*text == "Misses 30.Rg7, which wins a pawn.");
        CHECK(said(en.checkTemplate(*text)) == R"(Reason("wins a pawn"))");
        CHECK_FALSE(en.heldTemplate(*text));
        const auto texte = comment(ctx, Lang::Fr, PieceLetters::French);
        REQUIRE(texte);
        CHECK(*texte == "Manque 30.Tg7, qui gagnait un pion.");
        CHECK_FALSE(Brief(ctx, Lang::Fr, PieceLetters::French).heldTemplate(*texte));
        const auto before  = r.context(1);
        const auto earlier = comment(before, Lang::En, PieceLetters::English);
        REQUIRE(earlier);
        CHECK(*earlier == "Loses a pawn after 30.Rg7. 29…Re3 was better.");
        CHECK(Brief(before, Lang::En, PieceLetters::English).heldTemplate(*earlier) == *earlier);
    }
    {
        // Doubled pawns that the recapture 30.Bxc4 undoes at once.
        const auto r   = takesOnC4();
        const auto ctx = r.context();
        const Brief en(ctx, Lang::En, PieceLetters::English);
        const auto text = comment(ctx, Lang::En, PieceLetters::English);
        REQUIRE(text);
        CHECK(*text == "Leaves doubled pawns on the c-file. 29…Qh2 was better.");
        CHECK(said(en.checkTemplate("Leaves doubled pawns on the c-file.")) ==
              R"(Board("doubled pawns on the c-file"))");
        CHECK(en.heldTemplate(*text) == "29…Qh2 was better.");
        const auto texte = comment(ctx, Lang::Fr, PieceLetters::French);
        REQUIRE(texte);
        CHECK(*texte == "Laisse des pions doublés sur la colonne c. Mieux valait 29…Dh2.");
        CHECK(Brief(ctx, Lang::Fr, PieceLetters::French).heldTemplate(*texte) == "Mieux valait 29…Dh2.");
    }
    {
        // A fork told as what follows 22…Bxe5, which 23…Ng3 makes: true, kept, as the model's would be.
        const auto r = reviewed(
            "r4rk1/1ppbq1pp/4pb2/3pNp2/1P1PnPPP/p1PBP3/P3Q3/B1KR3R w - - 1 22", "g4g5",
            {{"e5d7 e7d7 h1g1 d7d6 h4h5 h7h6 e2g2 c7c6 d3e4 f5e4 g4g5", -49},
             {"f6e5 d4e5 e4g3 e2h2 g3h1 h4h5 c7c5 g5g6 h7h6 c1d2 c5b4 h2h1 f8c8 d1b1 b4c3 a1c3 d5d4 c3d4", -416}});
        const auto ctx  = r.context();
        const auto text = comment(ctx, Lang::En, PieceLetters::English);
        REQUIRE(text);
        CHECK(*text ==
              "After 22…Bxe5, a fork follows: the knight on g3 attacks the rook on h1 and the queen on e2. 22.Nxd7 was "
              "better: it attacks the rook on f8.");
        CHECK(Brief(ctx, Lang::En, PieceLetters::English).heldTemplate(*text) == *text);
        const auto texte = comment(ctx, Lang::Fr, PieceLetters::French);
        REQUIRE(texte);
        CHECK(*texte ==
              "Après 22…Fxe5, une fourchette suit : le cavalier en g3 attaque la tour en h1 et la dame en e2. Mieux "
              "valait 22.Cxd7, qui attaquait la tour en f8.");
        CHECK(Brief(ctx, Lang::Fr, PieceLetters::French).heldTemplate(*texte) == *texte);
    }
}

TEST_CASE("templates: the sentences of a text, as the checks read them") {
    using brief_detail::keptSentences;
    using brief_detail::sentenceTexts;
    CHECK(
        sentenceTexts("After 29…Qf7, a discovered attack hits the pawn on f2.  29.Nd4 was better.") ==
        std::vector<std::string_view>{"After 29…Qf7, a discovered attack hits the pawn on f2.", "29.Nd4 was better."});
    CHECK(sentenceTexts(" Mate! Is it? and on ") == std::vector<std::string_view>{"Mate!", "Is it?", "and on"});
    CHECK(sentenceTexts("  ").empty());
    const auto no_b = [](std::string_view s) { return s.starts_with("B"); };
    CHECK(keptSentences("A one. B two. C three.", no_b) == "A one. C three.");
    CHECK_FALSE(keptSentences("B one. B two.", no_b));
    CHECK_FALSE(keptSentences("", no_b));
}

TEST_CASE("templates: the model refused, the template's comment comes held to the boards") {
    const auto r   = rookToE3();
    const auto ctx = r.context();
    Scripted refused({"A blunder.", "A blunder.", "A blunder."});
    CHECK(writtenComment(refused, ctx, Lang::En, PieceLetters::English) ==
          Written{"29.Nd4 was better.", false, ATTEMPTS});
    // Nothing left of the template: nothing to say.
    const auto p = pawnA5();
    Scripted failing({});
    CHECK_FALSE(writtenComment(failing, p.context(2), Lang::En, PieceLetters::English));
}

TEST_CASE("templates: the key moments' comments and the story's passages, held to the boards") {
    const auto r = takesOnC4();
    // The comments of the moves that deserve one: the template's, then as they are shown and written.
    const auto raw =
        templateComments(r.game, r.report, r.analyses, {}, nullptr, nullptr, Lang::En, PieceLetters::English);
    const auto held =
        heldTemplateComments(r.game, r.report, r.analyses, {}, nullptr, nullptr, Lang::En, PieceLetters::English);
    REQUIRE_FALSE(raw.empty());
    CHECK(raw.front() ==
          std::pair<std::size_t, std::string>{1, "Leaves doubled pawns on the c-file. 29…Qh2 was better."});
    REQUIRE(held.size() == raw.size());
    CHECK(held.front() == std::pair<std::size_t, std::string>{1, "29…Qh2 was better."});
    for (std::size_t i = 1; i < raw.size(); ++i) CHECK(held[i] == raw[i]);
    // A comment left with no sentence is not shown.
    const auto p = pawnA5();
    for (const auto& [ply, text] :
         heldTemplateComments(p.game, p.report, p.analyses, {}, nullptr, nullptr, Lang::En, PieceLetters::English))
        CHECK(ply != 2);

    // The story: the passage of the turning point loses the doubled pawns, and keeps its template's text beside.
    const auto story =
        story_detail::told(r.game, "1-0", r.report, r.analyses, nullptr, Lang::En, PieceLetters::English);
    const auto swing = std::find_if(story.begin(), story.end(), [](const story_detail::Told& t) {
        return t.source.kind == story_detail::Source::Kind::Swing;
    });
    REQUIRE(swing != story.end());
    CHECK(swing->passage.text ==
          "The turning point is 29…dxc4??. Leaves doubled pawns on the c-file. 29…Qh2 was better.");
    const auto shown     = heldStory(r.game, "1-0", r.report, r.analyses, nullptr, Lang::En, PieceLetters::English);
    std::size_t passages = 0;
    for (const auto& t : story) passages += !t.passage.lesson;
    REQUIRE(shown.size() == passages);
    const auto it =
        std::find_if(shown.begin(), shown.end(), [&](const StoryPassage& p) { return p.templ == swing->passage.text; });
    REQUIRE(it != shown.end());
    CHECK(it->passage.text == "The turning point is 29…dxc4??. 29…Qh2 was better.");
    CHECK(it->passage.ply == swing->passage.ply);
    CHECK_FALSE(it->by_model);
    // Refused twice, the model's passage gives way to the template's, held the same way.
    Scripted refused({});
    const auto written =
        writtenStory(refused, r.game, "1-0", r.report, r.analyses, nullptr, Lang::En, PieceLetters::English, Players{});
    REQUIRE(written.size() == shown.size());
    for (std::size_t i = 0; i < written.size(); ++i) {
        CAPTURE(i);
        CHECK_FALSE(written[i].by_model);
        CHECK(written[i].passage.text == shown[i].passage.text);
        CHECK(written[i].templ == shown[i].templ);
    }
}
