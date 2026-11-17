// The model's comment held to the facts, the template's when it does not hold (the prototype's
// crates/commentary/src/llm.rs).

#include <doctest/doctest.hpp>

#include <deque>
#include <string>
#include <vector>

#include <commentary/comment.hpp>
#include <commentary/commentator.hpp>

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

// 13.Na3, an inaccuracy in an Italian game: 13.Nbd2 was better. `read` gets its context.
template <typename Read>
auto inaccuracy(Read read) {
    const auto start        = Position::initial();
    const auto before_moves = test::moves(
        start, {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5", "e1g1", "g8f6", "d2d3", "d7d6", "c2c3", "a7a5",
                "f1e1", "c5a7", "a2a4", "e8g8", "h2h3", "c8e6", "c4e6", "f7e6", "c1e3", "a7e3", "e1e3", "d6d5"});
    auto before = start;
    for (const auto& m : before_moves) before.playUnchecked(m);
    const auto played = *before.parseUci("b1a3");
    auto after        = before;
    after.playUnchecked(played);
    EngineLine reply, best;
    reply.moves = test::moves(after, {"d5d4", "e3e1"});
    best.moves  = test::moves(before, {"b1d2"});
    MoveReport report;
    report.mover      = Color::White;
    report.phase      = Phase::Middlegame;
    report.win_before = 50.0;
    report.win_after  = 40.0;
    report.judgment   = Judgment::Inaccuracy;
    MoveContext ctx;
    ctx.before = &before;
    ctx.played = played;
    ctx.report = &report;
    ctx.best   = &best;
    ctx.reply  = &reply;
    return read(ctx);
}

}  // namespace

TEST_CASE("commentator: the model's comment when it holds to the facts") {
    inaccuracy([](const MoveContext& ctx) {
        Scripted model({"Une imprécision."});
        const auto written = writtenComment(model, ctx, Lang::Fr, PieceLetters::French);
        CHECK(written == Written{"Une imprécision.", true, 1});
        // The likeliest answer, held to the brief's grammar.
        REQUIRE(model.requests.size() == 1);
        const Brief brief(ctx, Lang::Fr, PieceLetters::French);
        const auto& r = model.requests.front();
        CHECK(r == commentRequest(brief, 1));
        REQUIRE(r.messages.size() == 2);
        CHECK(r.messages[0] == std::pair{Speaker::System, std::string(brief.system)});
        CHECK(r.messages[1] == std::pair{Speaker::User, brief.prompt});
        CHECK(r.grammar == brief.grammar);
        CHECK(r.max_tokens == MAX_TOKENS);
        CHECK(r.temperature == 0.0f);
        CHECK(r.seed == 1);
    });
}

TEST_CASE("commentator: a refused answer is tried again, sampled") {
    inaccuracy([](const MoveContext& ctx) {
        // A blunder it is not: the review calls it an inaccuracy.
        Scripted model({"A blunder.", "An inaccuracy."});
        const auto written = writtenComment(model, ctx, Lang::En, PieceLetters::English);
        CHECK(written == Written{"An inaccuracy.", true, 2});
        REQUIRE(model.requests.size() == 2);
        CHECK(model.requests[1].temperature == 0.7f);
        CHECK(model.requests[1].seed == 2);
    });
}

TEST_CASE("commentator: the template when the model does not hold to the facts") {
    inaccuracy([](const MoveContext& ctx) {
        const auto templ = comment(ctx, Lang::En, PieceLetters::English);
        REQUIRE(templ);
        Scripted refused({"A blunder.", "Mate in 2.", "A fork wins."});
        CHECK(writtenComment(refused, ctx, Lang::En, PieceLetters::English) == Written{*templ, false, ATTEMPTS});
        CHECK(refused.requests.size() == ATTEMPTS);
        // A model that fails is not asked again.
        Scripted failing({});
        CHECK(writtenComment(failing, ctx, Lang::En, PieceLetters::English) == Written{*templ, false, ATTEMPTS});
        CHECK(failing.requests.size() == 1);
    });
}

TEST_CASE("commentator: a move with nothing to explain gets the template, unasked") {
    // A novelty and nothing else: no fact, no verdict, no theory.
    const auto start = Position::initial();
    const auto moves = test::moves(start, {"e2e4", "c7c5", "g1f3", "d7d6", "d2d4", "c5d4", "f3d4", "g8f6", "b1c3"});
    auto before      = start;
    for (const auto& m : moves) before.playUnchecked(m);
    MoveReport report;
    report.mover      = Color::Black;
    report.win_before = 45.0;
    report.win_after  = 45.0;
    report.novelty    = true;
    MoveContext ctx;
    ctx.before = &before;
    ctx.played = *before.parseUci("h7h6");
    ctx.report = &report;
    REQUIRE_FALSE(Brief(ctx, Lang::Fr, PieceLetters::French).hasSubstance());
    Scripted model({"Une nouveauté audacieuse."});
    CHECK(writtenComment(model, ctx, Lang::Fr, PieceLetters::French) == Written{"Nouveauté.", false, 0});
    CHECK(model.requests.empty());
}
