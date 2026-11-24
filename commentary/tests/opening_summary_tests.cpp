// The summary of an opening report (#91, mockup M13, board 4, point 11): the templates' version whole, in English and
// French; each passage written by the model held to the report's figures — a fake model writes sentences true and
// false (a wrong figure, a figure of another plan or side, a move not of the position, a plan not in the report or of
// the other side, advice), the checker refuses the false ones and the template's passage stands after two refusals.

#include <doctest/doctest.hpp>

#include <atomic>
#include <deque>
#include <string>
#include <vector>

#include <commentary/opening_summary.hpp>
#include <commentary/position.hpp>

using namespace fcg::commentary;
using Topic = SummaryPassage::Topic;

namespace {

// A model that gives its answers in turn, and none once they run out; the requests it got are kept.
class Scripted final : public Model {
   public:
    explicit Scripted(std::deque<std::string> answers) : answers_(std::move(answers)) {}
    std::optional<Generation> generate(const Request& request, std::string* error) override {
        requests.push_back(request);
        if (answers_.empty()) {
            if (error) *error = "no more answers";
            return std::nullopt;
        }
        Generation g;
        g.text = answers_.front();
        answers_.pop_front();
        return g;
    }
    std::vector<Request> requests;

   private:
    std::deque<std::string> answers_;
};

// The figures of a report on the exchange variation after 10…Nf8, the moves in the letters of `lang`.
SummaryFigures figures(Lang lang) {
    const bool fr = lang == Lang::Fr;
    SummaryFigures f;
    f.opening    = "D36 Queen's Gambit Declined: Exchange Variation";
    f.after      = fr ? "10…Cf8" : "10…Nf8";
    f.mover      = Color::White;
    f.games      = 4812;
    f.moves      = {{fr ? "11.Tab1" : "11.Rab1", 1902, 40, 55, 31, std::nullopt, false},
                    {"11.h3", 1120, 23, 53, 28, std::nullopt, false},
                    {fr ? "11.Tae1" : "11.Rae1", 802, 17, 59, 26, std::nullopt, false},
                    {"11.a3", 455, 9, 52, 22, std::nullopt, false},
                    {fr ? "11.Fxf6" : "11.Bxf6", 233, 5, 50, 12, std::nullopt, true}};
    f.lines      = {{fr ? std::vector<std::string>{"11.Tab1", "a5", "12.a3", "Ce4", "13.Fxe7", "Dxe7", "14.b4", "axb4",
                                                   "15.axb4", "Ff5"}
                        : std::vector<std::string>{"11.Rab1", "a5", "12.a3", "Ne4", "13.Bxe7", "Qxe7", "14.b4", "axb4",
                                                   "15.axb4", "Bf5"},
                     418, 56}};
    f.structures = {{Formation::Carlsbad, Color::White, true, 81}, {Formation::Isolani, Color::Black, false, 12}};
    f.structure_move  = 20;
    const auto knight = fr ? "le cavalier" : "the knight";
    f.plans           = {{Color::White, PlanName::MinorityAttack, {"b4", "b5"}, false, "", 47, 55},
                         {Color::White, PlanName::None, {"f3", "e4"}, false, "", 18, 57},
                         {Color::Black, PlanName::None, {"f8", "g6"}, true, knight, 52, 54},
                         {Color::Black, PlanName::None, {"f6", "e4"}, true, knight, 44, 48}};
    return f;
}

std::string passageOf(const std::vector<SummaryPassage>& passages, Topic topic) {
    for (const auto& p : passages)
        if (p.topic == topic) return p.text;
    return {};
}

}  // namespace

TEST_CASE("opening summary: the templates' version whole, in English and in French") {
    const auto en = templateSummary(figures(Lang::En), Lang::En);
    REQUIRE(en.size() == 6);
    CHECK(passageOf(en, Topic::Structure) ==
          "The Carlsbad structure nearly always holds: 81% of the games still have it at move 20. Next comes the "
          "isolated queen's pawn for Black (12%).");
    CHECK(passageOf(en, Topic::WhitePlans) ==
          "White's most frequent plan is the minority attack, b4 then b5: 47% of the games, where White scores 55%. "
          "Next, f3 then e4: 18% of the games, 57% for White.");
    CHECK(passageOf(en, Topic::BlackPlans) ==
          "Black's most frequent plan is the knight from f8 to g6: 52% of the games, where Black scores 54%. Next, "
          "the knight from f6 to e4: 44% of the games, 48% for Black.");
    CHECK(passageOf(en, Topic::Moves) ==
          "11.Rab1 is the most played move: 1,902 games (40%), 55% for White. 11.h3 follows: 1,120 games, 53%. Of the "
          "moves played often, 11.Rae1 scores best: 59%.");
    CHECK(passageOf(en, Topic::Line) ==
          "The main line runs 11.Rab1 a5 12.a3 Ne4 13.Bxe7 Qxe7 14.b4 axb4 15.axb4 Bf5: 418 games, where White "
          "scores 56%.");
    CHECK(passageOf(en, Topic::Evaluations) ==
          "The evaluations after the main moves go from +0.12 to +0.31, close to level.");
    for (const auto& p : en) {
        CHECK_FALSE(p.by_model);
        CHECK(p.text == p.templ);
    }

    const auto fr = templateSummary(figures(Lang::Fr), Lang::Fr);
    REQUIRE(fr.size() == 6);
    CHECK(passageOf(fr, Topic::Structure) ==
          "La structure de Carlsbad tient presque toujours : 81 % des parties l'ont encore au 20e coup. Puis vient "
          "le pion dame isolé des Noirs (12 %).");
    CHECK(passageOf(fr, Topic::WhitePlans) ==
          "Le plan le plus fréquent des Blancs est l'attaque de minorité, b4 puis b5 : 47 % des parties, où les "
          "Blancs marquent 55 %. Ensuite, f3 puis e4 : 18 % des parties, 57 % pour les Blancs.");
    CHECK(passageOf(fr, Topic::BlackPlans) ==
          "Le plan le plus fréquent des Noirs est le cavalier de f8 en g6 : 52 % des parties, où les Noirs "
          "marquent 54 %. Ensuite, le cavalier de f6 en e4 : 44 % des parties, 48 % pour les Noirs.");
    CHECK(passageOf(fr, Topic::Moves) ==
          "11.Tab1 est le coup le plus joué : 1 902 parties (40 %), 55 % pour les Blancs. 11.h3 suit : "
          "1 120 parties, 53 %. Parmi les coups souvent joués, 11.Tae1 marque le mieux : 59 %.");
    CHECK(passageOf(fr, Topic::Line) ==
          "La ligne principale : 11.Tab1 a5 12.a3 Ce4 13.Fxe7 Dxe7 14.b4 axb4 15.axb4 Ff5 ; 418 parties, où les "
          "Blancs marquent 56 %.");
    CHECK(passageOf(fr, Topic::Evaluations) ==
          "Les évaluations après les coups principaux vont de +0,12 à +0,31, proches de l'équilibre.");
    CHECK(summaryText(fr).find("La structure de Carlsbad") == 0);

    // A position past move 15, its structures ten moves later; a structure that comes rather than stays.
    auto later           = figures(Lang::En);
    later.structure_move = 0;
    later.structures     = {{Formation::Isolani, Color::Black, false, 34}};
    CHECK(templatePassage(later, Topic::Structure, Lang::En) ==
          "Ten moves later, 34% of the games have the isolated queen's pawn for Black.");
    // Too few games: neither structures nor plans, nor evaluations without them.
    SummaryFigures rare;
    rare.games = 12;
    rare.moves = {{"11.Rab1", 9, 75, 61, std::nullopt, std::nullopt, false}};
    CHECK(summaryTopics(rare) == std::vector{Topic::Moves});
    CHECK(templatePassage(rare, Topic::Moves, Lang::En) ==
          "11.Rab1 is the most played move: 9 games (75%), 61% for "
          "White.");
}

TEST_CASE("opening summary: the plans the assistant's facts name") {
    const auto pos = Position::fromFen("r1bqrnk1/pp2bppp/2p2n2/3p2B1/3P4/2NBPN2/PPQ2PPP/R4RK1 w - - 0 11");
    REQUIRE(pos);
    // The b-pawn against three pawns on the queen's wing, the c-pawn holding the d-pawn: the minority attack.
    const auto b4 = pos->parseUci("b2b4");
    REQUIRE(b4);
    CHECK(planOf(*pos, *b4) == PlanName::MinorityAttack);
    const auto h3 = pos->parseUci("h2h3");
    REQUIRE(h3);
    CHECK(planOf(*pos, *h3) == PlanName::None);
    CHECK(planWords(PlanName::MinorityAttack, Lang::En) == "the minority attack");
    CHECK(planWords(PlanName::MinorityAttack, Lang::Fr) == "l'attaque de minorité");
    // An isolated queen's pawn advancing: its thematic advance.
    const auto iqp = Position::fromFen("r1bq1rk1/pp2bppp/2n1pn2/8/3P4/2NB1N2/PP3PPP/R1BQ1RK1 w - - 0 10");
    REQUIRE(iqp);
    const auto d5 = iqp->parseUci("d4d5");
    REQUIRE(d5);
    CHECK(planOf(*iqp, *d5) == PlanName::Isolani);
}

TEST_CASE("opening summary: each passage the model's when it holds to the report's figures") {
    const auto f = figures(Lang::En);
    const std::deque<std::string> said{
        "The Carlsbad structure nearly always holds: 81% of the games still have it at move 20.",
        "White mostly goes for the minority attack, b4 then b5: 47% of the games, where White scores 55%.",
        "Black often brings the knight from f8 to g6: 52% of the games.",
        "11.Rab1 is the most played move, with 1,902 games.",
        "The main line runs 11.Rab1 a5 12.a3 Ne4 13.Bxe7 Qxe7 14.b4 axb4 15.axb4 Bf5, in 418 games.",
        "The evaluations go from +0.12 to +0.31 after the main moves, close to level."};
    Scripted model(said);
    std::atomic<int> done{0};
    std::string error;
    const auto written = writtenSummary(model, f, Lang::En, 0, nullptr, &done, &error);
    CHECK(error.empty());
    CHECK(done == 6);
    REQUIRE(written.size() == 6);
    for (std::size_t i = 0; i < written.size(); ++i) {
        CAPTURE(i);
        CAPTURE(written[i].refused.empty() ? std::string() : debug(written[i].refused.front().second));
        CHECK(written[i].by_model);
        CHECK(written[i].attempts == 1);
        CHECK(written[i].text == said[i]);
        CHECK(written[i].templ == templatePassage(f, written[i].topic, Lang::En));
    }
    // The likeliest answer first, each passage with its instructions, the report, its figures and its grammar.
    REQUIRE(model.requests.size() == 6);
    for (std::size_t i = 0; i < model.requests.size(); ++i) {
        const auto& q = model.requests[i];
        CHECK(q.temperature == 0.0f);
        CHECK(q.seed == 1);
        REQUIRE(q.messages.size() == 2);
        CHECK(q.messages[0].second.find("You write one passage of the summary of an opening report") == 0);
        CHECK(q.messages[1].second.find("The report: the position after 10…Nf8 (D36 Queen's Gambit Declined: "
                                        "Exchange Variation), White to move, 4,812 games.\nIn the summary:") == 0);
        CHECK(q.messages[1].second.ends_with("\nSummary:"));
        REQUIRE(q.grammar);
        if (i > 0) CHECK(q.grammar->find("\"81%\"") == std::string::npos);  // each passage's own figures
    }
    const auto& plans = model.requests[1];
    CHECK_MESSAGE(plans.messages[1].second.find("A plan of White: the minority attack, b4 then b5; 47% of the games, "
                                                "where White scores 55%.") != std::string::npos,
                  plans.messages[1].second);
    CHECK(plans.grammar->find("\"47%\"") != std::string::npos);
    CHECK(plans.grammar->find("\"b5\"") != std::string::npos);
    CHECK(model.requests[0].grammar->find("\"81%\"") != std::string::npos);
    CHECK(model.requests[3].grammar->find("\"11.Rab1\"") != std::string::npos);
    CHECK(model.requests[3].grammar->find("\"11.Qb3\"") == std::string::npos);
}

TEST_CASE("opening summary: the checker refuses false sentences, each with why") {
    const auto f = figures(Lang::En);
    const SummaryBrief plans(f, Topic::WhitePlans, Lang::En);
    const SummaryBrief black(f, Topic::BlackPlans, Lang::En);
    const SummaryBrief moves(f, Topic::Moves, Lang::En);
    const SummaryBrief evals(f, Topic::Evaluations, Lang::En);
    const auto refused = [](const SummaryBrief& b, std::string_view text) {
        const auto r = b.check(text);
        return r ? debug(*r) : std::string("held");
    };
    // True.
    CHECK(refused(plans, "White mostly goes for the minority attack, b4 then b5: 47% of the games.") == "held");
    CHECK(refused(plans, "f3 then e4 is rarer: 18% of the games, 57% for White.") == "held");
    // A figure told before what it is of, in its own clause.
    CHECK(refused(black,
                  "In 52% of the games Black scores 54% with the knight from f8 to g6; in 44% the knight goes "
                  "from f6 to e4.") == "held");
    // A figure the report does not give.
    CHECK(refused(plans, "White mostly goes for the minority attack, b4 then b5: 52% of the games.") ==
          "Figure(\"52%\")");
    CHECK(refused(moves, "About 50 games play 11.Rab1.") == "Figure(\"50\")");
    // A figure of another plan: 47% is the minority attack's.
    CHECK(refused(plans, "White plays f3 then e4 in 47% of the games.") == "Figure(\"47%\")");
    // A score of the wrong side.
    CHECK(refused(plans, "The minority attack, b4 then b5, comes in 47% of the games, where Black scores 55%.") ==
          "Figure(\"55%\")");
    // A move not of the position, nor of its lines.
    CHECK(refused(moves, "11.Qb3 is the most played move.") == "Notation(\"11.Qb3\")");
    CHECK(refused(plans, "White mostly goes for the minority attack with b4 then a4.") == "Notation(\"a4\")");
    // A plan the report does not have; one of the other side.
    CHECK(refused(plans, "White goes for the pawn storm against the king.") == "Unsupported(\"storm\")");
    CHECK(refused(black, "Black goes for the minority attack.") == "Unsupported(\"minority\")");
    CHECK(refused(plans, "White builds a hedgehog.") == "Unsupported(\"hedgehog\")");
    // Advice, where the report gives frequencies.
    CHECK(refused(plans, "White should play the minority attack, b4 then b5.") == "Advice(\"should\")");
    // How the position stands, outside the evaluations' passage, or against them.
    CHECK(refused(moves, "11.Rab1 keeps the balance.") == "Evaluation(\"balance\")");
    CHECK(refused(evals, "After 11.Rab1, the evaluation is +0.31, close to level.") == "held");
    CHECK(refused(evals, "After 11.Rab1, White has the advantage at +0.31.") == "Evaluation(\"advantage\")");
    CHECK(refused(evals, "After 11.Rab1, White is clearly better at +0.31.") == "Evaluation(\"clearly\")");
    // A score said as a share, a share as a score.
    CHECK(refused(moves, "11.Rab1 is played in 1,902 games, which gives White 55% of the position.") ==
          "Figure(\"55%\")");
    CHECK(refused(plans, "With the minority attack, b4 then b5, White scores 47%.") == "Figure(\"47%\")");
    // A sentence that names nothing of the report: nothing to read it against.
    CHECK(refused(evals, "After 11.Rab1, the evaluation is +0.31. White's position goes from lost to balanced.") ==
          "Unanchored(\"White's position goes from lost to balanced.\")");
    // Words the grammar does not allow.
    CHECK(refused(moves, "11.Rab1 is the most played move!!") == "Grammar");
    // French: its figures with their narrow spaces.
    const auto fr = figures(Lang::Fr);
    const SummaryBrief fr_plans(fr, Topic::WhitePlans, Lang::Fr);
    CHECK(refused(fr_plans, "Les Blancs jouent surtout l'attaque de minorité, b4 puis b5 : 47 % des parties.") ==
          "held");
    CHECK(refused(fr_plans, "Les Blancs jouent surtout l'attaque de minorité, b4 puis b5 : 48 % des parties.") ==
          "Figure(\"48 %\")");
    CHECK(refused(fr_plans, "Avec l'attaque de minorité, b4 puis b5, les Blancs marquent 47 %.") == "Figure(\"47 %\")");
}

TEST_CASE("opening summary: a passage refused twice gives way to the template's, which says why") {
    const auto f = figures(Lang::En);
    Scripted model({"The Carlsbad structure holds in 82% of the games.",
                    "The Carlsbad structure holds, Black should "
                    "keep it.",
                    "White mostly goes for the minority attack, b4 then b5: 47% of the games."});
    std::atomic<bool> cancel{false};
    std::string error;
    const auto written = writtenSummary(model, f, Lang::En, 0, &cancel, nullptr, &error);
    REQUIRE(written.size() == 6);
    const auto& s = written[0];
    CHECK(s.topic == Topic::Structure);
    CHECK_FALSE(s.by_model);
    CHECK(s.attempts == 2);
    CHECK(s.text == s.templ);
    REQUIRE(s.refused.size() == 2);
    CHECK(s.refused[0].first == "The Carlsbad structure holds in 82% of the games.");
    CHECK(debug(s.refused[0].second) == "Figure(\"82%\")");
    CHECK(debug(s.refused[1].second) == "Advice(\"should\")");
    // The second attempt is a sample of its own.
    REQUIRE(model.requests.size() >= 3);
    CHECK(model.requests[1].temperature > 0.0f);
    CHECK(model.requests[1].seed == 2);
    // The next passage held; the model out of answers after it: not asked again, the rest the templates'.
    CHECK(written[1].by_model);
    CHECK(error == "no more answers");
    for (std::size_t i = 2; i < written.size(); ++i) {
        CHECK_FALSE(written[i].by_model);
        CHECK(written[i].text == written[i].templ);
    }
    CHECK(model.requests.size() == 4);  // two, one, then the one that gave nothing
    // Written again: a sample from its first attempt.
    Scripted again({"The Carlsbad structure nearly always holds: 81% of the games still have it at move 20."});
    const auto second = writtenSummary(again, f, Lang::En, 1);
    REQUIRE_FALSE(again.requests.empty());
    CHECK(again.requests[0].temperature > 0.0f);
    CHECK(again.requests[0].seed == 3);
    CHECK(second[0].by_model);
    // Cancelled: the templates' throughout, the model not asked.
    Scripted unused({"x"});
    cancel             = true;
    const auto stopped = writtenSummary(unused, f, Lang::En, 0, &cancel);
    CHECK(unused.requests.empty());
    CHECK(summaryText(stopped) == summaryText(templateSummary(f, Lang::En)));
}
