// Questions about the position (the prototype's crates/app/src/question.rs, mod tests), and answers to them.

#include <doctest/doctest.hpp>

#include <deque>
#include <string>
#include <variant>
#include <vector>

#include <commentary/question.hpp>

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

// An engine that plays the first move it may, then the first reply, and calls it level.
class FirstMoves final : public Oracle {
   public:
    std::optional<Searched> search(const Position& pos, std::span<const Move> only) const override {
        const auto legal = pos.legalMoves();
        if (legal.empty()) return std::nullopt;
        Searched s;
        s.moves.push_back(only.empty() ? legal.front() : only.front());
        auto after = pos;
        after.playUnchecked(s.moves.front());
        if (const auto replies = after.legalMoves(); !replies.empty()) s.moves.push_back(replies.front());
        s.score = Score::cp(0);
        return s;
    }
};

}  // namespace

TEST_CASE("question: moves go by both names, the reader's first") {
    // Rf1 is the rook's move in English, the king's in French: both are legal here.
    const auto p      = test::pos("4k3/8/8/8/8/8/8/4K2R w K - 0 1");
    const auto french = moveNames(p, Lang::Fr);
    CHECK(french.at("Rf1").role() == Role::King);
    CHECK(french.at("Tf1").role() == Role::Rook);
    // The English name too.
    REQUIRE(french.contains("Rh2"));
    CHECK(french.at("Rh2") == french.at("Th2"));
    const auto english = moveNames(p, Lang::En);
    CHECK(english.at("Rf1").role() == Role::Rook);
    CHECK(actionGrammar(french).find("\"Th2\"") != std::string::npos);
    CHECK(actionGrammar(french).starts_with("root ::= \"explain\" | \"best\" | \"threat\""));
}

TEST_CASE("question: an action names only moves the question names") {
    const auto p     = test::pos("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    const auto names = moveNames(p, Lang::Fr);
    const auto nf6   = names.at("Cf6");
    CHECK(readAction("try Nf6", "Et si les Noirs jouent 1…Cf6 ?", names) == Action{Action::Kind::Try, nf6});
    // A move the question does not name.
    CHECK(readAction("try Cf6", "pourquoi pas e5 ?", names) == std::nullopt);
    CHECK(readAction("compare e5 c5", "e5 ou c5 ?", names) ==
          Action{Action::Kind::Compare, names.at("e5"), names.at("c5")});
    CHECK(readAction("threat", "que menacent les Blancs ?", names) == Action{Action::Kind::Threat});
    CHECK_FALSE(mentions("pourquoi pas Cf5", "f5"));
    CHECK(mentions("pourquoi pas 23.Cf5 ?", "Cf5"));
}

TEST_CASE("question: the suggested questions need no model") {
    const auto p     = test::pos("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    const auto names = moveNames(p, Lang::En);
    const auto plain = [&](std::string_view q) { return plainAction(q, names); };
    CHECK(plain("What does White threaten?") == Action{Action::Kind::Threat});
    CHECK(plain("Que menacent les Blancs ?") == Action{Action::Kind::Threat});
    CHECK(plain("Quel est le meilleur coup ?") == Action{Action::Kind::Best});
    CHECK(plain("Why this move?") == Action{Action::Kind::Explain});
    // A move named: the model reads it.
    CHECK(plain("Does Nf6 meet the threat?") == std::nullopt);
    CHECK(plain("Who stands better?") == std::nullopt);
}

TEST_CASE("question: the request turning a question into an action") {
    const auto p     = test::pos("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    const auto names = moveNames(p, Lang::En);
    const auto r     = actionRequest("Why not Nf6?", p, "1.e4", names);
    REQUIRE(r.messages.size() == 2);
    CHECK(r.messages[0].first == Speaker::System);
    CHECK(r.messages[0].second.starts_with("You turn a chess player's question"));
    CHECK(r.messages[1] == std::pair{Speaker::User, std::string("Move just played: 1.e4. Black to move.\n"
                                                                "Question: Why not Nf6?\nAction:")});
    CHECK(r.grammar == actionGrammar(names));
    CHECK(r.max_tokens == 16);
    CHECK(r.temperature == 0.0f);
    CHECK(r.seed == 1);
}

TEST_CASE("question: answers from the review's lines, the engine's, and the model's reading") {
    const auto start = Position::initial();
    auto pos         = start;
    const auto e4    = *start.parseUci("e2e4");
    pos.playUnchecked(e4);
    // What the review found after 1.e4: 1…e5 2.Nf3, level.
    const std::vector<std::pair<Position, PositionAnalysis>> known = {
        {pos, PositionAnalysis{{EngineLine{test::moves(pos, {"e7e5", "g1f3"}), Score::cp(20), std::nullopt, 20}},
                               std::nullopt}}};
    const FirstMoves engine;
    Asked asked;
    asked.before   = &pos;
    asked.previous = e4;
    asked.earlier  = &start;
    asked.known    = known;
    asked.lang     = Lang::Fr;
    asked.letters  = PieceLetters::French;

    // A suggested question: no model asked, the review's move told.
    asked.question = "Quel est le meilleur coup ?";
    Scripted silent({});
    const auto best = answer(asked, silent, &engine, nullptr);
    REQUIRE(std::holds_alternative<Answered>(best));
    const auto& said = std::get<Answered>(best);
    CHECK(said.text.starts_with("Meilleur coup : 1…e5."));
    CHECK(said.best == *pos.parseUci("e7e5"));
    CHECK(said.moves == std::vector<Move>{*pos.parseUci("e7e5")});
    // A move of the position before, read by the model: played where it was legal.
    asked.question = "Et pourquoi pas 1.d4 ?";
    Scripted reader({"try d4"});
    const auto tried = answer(asked, reader, &engine, nullptr);
    REQUIRE(std::holds_alternative<Answered>(tried));
    CHECK(std::get<Answered>(tried).earlier);
    CHECK(std::get<Answered>(tried).moves == std::vector<Move>{*start.parseUci("d2d4")});
    REQUIRE(!reader.requests.empty());
    CHECK(reader.requests.front().messages[1].second.starts_with("Move just played: 1.e4. Black to move."));
    // A move the question does not name, a model that does not answer, no engine, nothing to explain.
    Scripted liar({"try Cf6"});
    CHECK(std::get<Problem>(answer(asked, liar, &engine, nullptr)) == Problem::UnknownMove);
    Scripted mute({});
    CHECK(std::get<Problem>(answer(asked, mute, &engine, nullptr)) == Problem::NoEngine);
    asked.question = "Quel est le meilleur coup ?";
    CHECK(std::get<Problem>(answer(asked, silent, nullptr, nullptr)) == Problem::NoEngine);
    asked.question = "Why this move?";
    CHECK(std::get<Problem>(answer(asked, silent, &engine, nullptr)) == Problem::Unanalyzed);
    // In check, the threat is the check itself.
    const auto checked = test::pos("4k3/8/8/8/8/8/4q3/4K3 w - - 0 1");
    Asked threat;
    threat.question = "What does Black threaten?";
    threat.before   = &checked;
    CHECK(std::get<Problem>(answer(threat, silent, &engine, nullptr)) == Problem::InCheck);
}
