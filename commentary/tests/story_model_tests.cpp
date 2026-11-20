// The story of a game told by the language model (#62, mockup M11, board 5): each passage put to it with its facts,
// held to the grammar and checker of the comments plus the names and numbers of the facts, written again once when it
// is refused, the template's after a second refusal.

#include <doctest/doctest.hpp>

#include <atomic>
#include <deque>
#include <string>
#include <vector>

#include <commentary/story.hpp>
#include <commentary/story_model.hpp>

#include "helpers.hpp"

using namespace fcg::commentary;

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

EngineLine engineLine(const Position& pos, std::string_view uci, Score score) {
    EngineLine l;
    const auto m = pos.parseUci(uci);
    REQUIRE_MESSAGE(m, std::string(uci));
    l.moves.push_back(*m);
    l.score = score;
    l.depth = 20;
    return l;
}

// The game of the story's own test (game_tests.cpp): level until Black's queen goes to g4, where the h-pawn takes it.
// Its story: the opening, the turning point 12…Qg4??, what Black should work on, and White's win.
struct Reviewed {
    MainLine line;
    Analyses analyses;
    GameReport report;
};

Reviewed queenLost() {
    const auto start = Position::initial();
    MainLine g(start, test::moves(start, {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5", "d2d3", "d7d6", "b1c3",
                                          "g8f6", "c1g5", "h7h6", "g5h4", "c8g4", "h2h3", "g4h5", "d1d2", "d8d7",
                                          "e1c1", "e8c8", "c1b1", "c8b8", "a2a3", "d7g4", "h3g4", "h5g4"}));
    Analyses a(g.positions().size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto& pos = g.positions()[i];
        PositionAnalysis analysis;
        if (i == 23) {
            analysis.lines.push_back(engineLine(pos, "d7e6", Score::cp(20)));
        } else {
            const auto mv = i < g.moves().size() ? g.moves()[i] : pos.legalMoves().front();
            analysis.lines.push_back(engineLine(pos, uci(mv), Score::cp(i >= 24 ? 900 : 20)));
        }
        a[i] = analysis;
    }
    auto report = review(g, a, Opening{});
    REQUIRE(report.complete);
    return Reviewed{std::move(g), std::move(a), std::move(report)};
}

// The template's passages but the lessons, as the review tab shows the story.
std::vector<Passage> templates(const Reviewed& r, Lang lang, PieceLetters letters) {
    std::vector<Passage> out;
    for (auto& p : story(r.line, "1-0", r.report, r.analyses, nullptr, lang, letters))
        if (!p.lesson) out.push_back(std::move(p));
    return out;
}

}  // namespace

TEST_CASE("story by the model: each passage is the model's when it holds to its facts") {
    const auto r      = queenLost();
    const auto templ  = templates(r, Lang::En, PieceLetters::English);
    const auto passes = std::deque<std::string>{"Out of the opening, the position is balanced.",
                                                "A blunder that loses the queen.", "White wins."};
    REQUIRE(templ.size() == passes.size());
    Scripted model(passes);
    std::atomic<int> done{0};
    std::string error;
    const auto told = writtenStory(model, r.line, "1-0", r.report, r.analyses, nullptr, Lang::En, PieceLetters::English,
                                   Players{"Lasker, Emanuel", "Bob"}, 0, nullptr, &done, &error);
    CHECK(error.empty());
    CHECK(done == 3);
    REQUIRE(told.size() == 3);
    for (std::size_t i = 0; i < told.size(); ++i) {
        CAPTURE(i);
        CHECK(told[i].by_model);
        CHECK(told[i].attempts == 1);
        CHECK(told[i].refused.empty());
        CHECK(told[i].passage.text == passes[i]);
        CHECK(told[i].templ == templ[i].text);
        CHECK(told[i].passage.ply == templ[i].ply);  // a click goes to the same move
    }
    // The likeliest answer first, each passage with the story's instructions, its facts and its grammar.
    REQUIRE(model.requests.size() == 3);
    for (const auto& q : model.requests) {
        CHECK(q.temperature == 0.0f);
        CHECK(q.seed == 1);
        REQUIRE(q.messages.size() == 2);
        CHECK(q.messages[0].second.find("You tell the story of a chess game") == 0);
        CHECK(q.messages[1].second.find("Players: Lasker with White, Bob with Black.\nIn the story:") == 0);
        CHECK(q.messages[1].second.ends_with("\nStory:"));
        CHECK(q.grammar);
        CHECK(q.max_tokens == MAX_TOKENS);
    }
    const auto& turn = model.requests[1].messages[1].second;
    CHECK_MESSAGE(turn.find("In the story: the turning point; Black never came back after it.") != std::string::npos,
                  turn);
    CHECK_MESSAGE(turn.find("Move played: 12…Qg4 (Black).") != std::string::npos, turn);
    CHECK_MESSAGE(turn.find("Verdict: blunder.") != std::string::npos, turn);
    CHECK(turn.find("Comment:") == std::string::npos);
    CHECK(model.requests[0].messages[1].second.find("Out of the opening, the position is balanced.") !=
          std::string::npos);
    CHECK(model.requests[2].messages[1].second.find("White wins.\nLast move: 13…Bxg4 (Black).") != std::string::npos);
}

TEST_CASE("story by the model: a refused passage is written again, sampled; refused twice, the template's stands") {
    const auto r     = queenLost();
    const auto templ = templates(r, Lang::En, PieceLetters::English);
    // The opening at the second try; the turning point refused twice (a fork no fact shows, then a mistake the review
    // calls a blunder); the end at once.
    Scripted model({"The opening leaves White a fork.", "Out of the opening, the position is balanced.",
                    "A fork wins the queen.", "A mistake that loses the queen.", "White wins."});
    const auto told = writtenStory(model, r.line, "1-0", r.report, r.analyses, nullptr, Lang::En, PieceLetters::English,
                                   Players{}, 0);
    REQUIRE(told.size() == 3);
    CHECK(told[0].by_model);
    CHECK(told[0].attempts == 2);
    REQUIRE(told[0].refused.size() == 1);
    CHECK(told[0].refused[0].first == "The opening leaves White a fork.");
    CHECK(told[0].refused[0].second == Refusal{Refusal::Kind::Motif, "fourchette"});
    CHECK(told[0].passage.text == "Out of the opening, the position is balanced.");
    CHECK_FALSE(told[1].by_model);
    CHECK(told[1].attempts == STORY_ATTEMPTS);
    REQUIRE(told[1].refused.size() == 2);
    CHECK(told[1].refused[0].second.kind == Refusal::Kind::Motif);
    CHECK(told[1].refused[1].second == Refusal{Refusal::Kind::Verdict, "mistake"});
    CHECK(told[1].passage.text == templ[1].text);
    CHECK(told[2].by_model);
    // The second attempt samples; the first of each passage is the likeliest answer.
    REQUIRE(model.requests.size() == 5);
    CHECK(model.requests[0].temperature == 0.0f);
    CHECK(model.requests[1].temperature == 0.7f);
    CHECK(model.requests[1].seed == 2);
    CHECK(model.requests[2].temperature == 0.0f);
    CHECK(model.requests[3].temperature == 0.7f);
    // Written again (round 1), every attempt samples, with seeds of its own.
    const StoryBrief brief(
        story_detail::told(r.line, "1-0", r.report, r.analyses, nullptr, Lang::En, PieceLetters::English).front(),
        r.line, r.analyses, nullptr, Lang::En, PieceLetters::English, Players{});
    const auto again = storyRequest(brief, 1, 1);
    CHECK(again.temperature == 0.7f);
    CHECK(again.seed == 3);
    CHECK(storyRequest(brief, 1, 2).seed == 4);
}

TEST_CASE("story by the model: names and numbers the facts do not give are refused") {
    const auto r    = queenLost();
    const auto told = story_detail::told(r.line, "1-0", r.report, r.analyses, nullptr, Lang::En, PieceLetters::English);
    REQUIRE(told.size() >= 3);
    const StoryBrief end(told.back(), r.line, r.analyses, nullptr, Lang::En, PieceLetters::English,
                         Players{"Lasker, Emanuel", "Bob"});
    CHECK_FALSE(end.check("White wins."));
    CHECK_FALSE(end.check("In the end Lasker wins."));  // a player the game names
    CHECK(end.check("In the end Kasparov wins.") == Refusal{Refusal::Kind::Name, "Kasparov"});
    CHECK(end.check("White wins on move forty.") == Refusal{Refusal::Kind::Number, "forty"});
    CHECK(end.check("White wins after 12…Qg4.") == Refusal{Refusal::Kind::Grammar, {}});  // a move it was not given
    CHECK_FALSE(end.check("White wins after 13…Bxg4."));                                  // the last move
    // The players' names whatever their case: one the grammar's words cannot spell is allowed whole.
    const StoryBrief named(told.back(), r.line, r.analyses, nullptr, Lang::En, PieceLetters::English,
                           Players{"McShane", "?"});
    CHECK(named.prompt.find("Players: McShane with White.") == 0);
    CHECK_FALSE(named.check("In the end McShane wins."));
    CHECK(named.grammar.find("\"McShane\"") != std::string::npos);
    // A won position the facts do not tell: the opening left the position balanced.
    const StoryBrief opening(told.front(), r.line, r.analyses, nullptr, Lang::En, PieceLetters::English, Players{});
    CHECK_FALSE(opening.check("Out of the opening, the position is balanced."));
    CHECK(opening.check("Out of the opening, White is winning.") == Refusal{Refusal::Kind::Evaluation, "winning"});
    // « les deux camps » counts nothing.
    const StoryBrief fr(told.back(), r.line, r.analyses, nullptr, Lang::Fr, PieceLetters::French, Players{});
    CHECK_FALSE(fr.check("Les Blancs l'emportent, et les deux camps ont joué vite."));
    CHECK(fr.check("Les Blancs l'emportent au quarantième coup.") == Refusal{Refusal::Kind::Number, "quarantième"});
}

TEST_CASE("story by the model: a model that gives no answer, or a story cancelled, leaves the templates'") {
    const auto r     = queenLost();
    const auto templ = templates(r, Lang::Fr, PieceLetters::French);
    Scripted failing({});
    std::string error;
    const auto told = writtenStory(failing, r.line, "1-0", r.report, r.analyses, nullptr, Lang::Fr,
                                   PieceLetters::French, Players{}, 0, nullptr, nullptr, &error);
    CHECK(error == "no more answers");
    CHECK(failing.requests.size() == 1);  // not asked again
    REQUIRE(told.size() == templ.size());
    for (std::size_t i = 0; i < told.size(); ++i) {
        CHECK_FALSE(told[i].by_model);
        CHECK(told[i].passage == templ[i]);
    }
    CHECK(told[0].attempts == 1);
    CHECK(told[1].attempts == 0);

    std::atomic<bool> cancel{true};
    Scripted model({"Les Blancs l'emportent."});
    const auto cancelled = writtenStory(model, r.line, "1-0", r.report, r.analyses, nullptr, Lang::Fr,
                                        PieceLetters::French, Players{}, 0, &cancel);
    CHECK(model.requests.empty());
    REQUIRE(cancelled.size() == templ.size());
    CHECK(cancelled.back().passage == templ.back());
    CHECK(cancelled.back().attempts == 0);
}

TEST_CASE("story by the model: the game ends only in the passage about the end") {
    const auto r    = queenLost();
    const auto told = story_detail::told(r.line, "1-0", r.report, r.analyses, nullptr, Lang::Fr, PieceLetters::French);
    REQUIRE(told.size() >= 3);
    const StoryBrief opening(told.front(), r.line, r.analyses, nullptr, Lang::Fr, PieceLetters::French, Players{});
    const auto said = opening.prompt.find("L'ouverture se termine sur ");
    REQUIRE_MESSAGE(said != std::string::npos, opening.prompt);
    const auto from = said + std::string_view("L'ouverture se termine sur ").size();
    const auto last = opening.prompt.substr(from, opening.prompt.find('.', from + 3) - from);
    // The opening's end told as the game's: « la partie se termine sur 11…Dxe7 ».
    CHECK(opening.check("Au sortir de l'ouverture, la position est équilibrée, et la partie se termine sur " + last +
                        ".") == Refusal{Refusal::Kind::Wording, "la partie se termine"});
    CHECK_FALSE(opening.check("Au sortir de l'ouverture, la position est équilibrée, et l'ouverture se termine sur " +
                              last + "."));
    const StoryBrief end(told.back(), r.line, r.analyses, nullptr, Lang::Fr, PieceLetters::French, Players{});
    CHECK_FALSE(end.check("Les Blancs l'emportent, et la partie se termine sur 13…Fxg4."));
    CHECK(story_detail::checkEnd("The game ends with 13…Bxg4.", false) ==
          Refusal{Refusal::Kind::Wording, "the game ends"});
    CHECK_FALSE(story_detail::checkEnd("The opening ends with 13…Bxg4.", false));
    // As two passages about the opening said it.
    const Refusal game_ends{Refusal::Kind::Wording, "la partie se termine"};
    for (const auto* opening_end :
         {"Dans la Ruy Lopez, défense Steinitz, les Noirs sont un peu mieux au sortir de l'ouverture, et la partie se "
          "termine sur 11…Dxe7.",
          "Dans la défense polonaise, les Noirs ont déjà une position gagnante au sortir de l'ouverture, et la partie "
          "se termine sur 11…Fxh1."}) {
        CHECK_MESSAGE(story_detail::checkEnd(opening_end, false) == game_ends, opening_end);
        CHECK_FALSE(story_detail::checkEnd(opening_end, true));
    }
}
