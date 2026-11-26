// The assistant's child process, as the window talks with it: its protocol, and a scripted model in a process of its
// own (gui/tests/mock/assistant.cpp).

#ifdef FCG_ASSISTANT

#    include <doctest/doctest.hpp>

#    include <chrono>
#    include <string>

#    include <assistant/protocol.hpp>

#    include "assistant_process.hpp"
#    include "mock_engines.hpp"
#    include "self_path.hpp"

using namespace fcg;
using namespace std::chrono_literals;

namespace {

commentary::Request asked(std::string text, uint32_t seed = 1, float temperature = 0.0f, bool grammar = true) {
    commentary::Request r;
    r.messages    = {{commentary::Speaker::System, "You annotate chess games."}, {commentary::Speaker::User, text}};
    r.max_tokens  = 130;
    r.temperature = temperature;
    r.seed        = seed;
    if (grammar) r.grammar = "root ::= \"x\"";
    return r;
}

}  // namespace

TEST_CASE("assistant: requests and answers go one JSON object per line, whatever they hold") {
    auto r          = asked("Move played: 13…Qe7 (Black).\nComment: « une nouveauté »\t\"quoted\"", 2, 0.7f);
    const auto line = assistant::requestLine(r);
    CHECK(line.back() == '\n');
    CHECK(line.find('\n') == line.size() - 1);
    std::string error;
    const auto back = assistant::parseRequest(line, &error);
    REQUIRE_MESSAGE(back, error);
    CHECK(*back == r);
    r.grammar.reset();
    CHECK(*assistant::parseRequest(assistant::requestLine(r), nullptr) == r);
    CHECK_FALSE(assistant::parseRequest("{\"messages\": [[\"robot\", \"hi\"]]}", &error));
    CHECK(error.starts_with("not a request"));

    commentary::Generation g;
    g.text            = "Une nouveauté.\nSur deux lignes.";
    g.prompt_tokens   = 420;
    g.tokens          = 7;
    g.seconds         = 0.25;
    const auto answer = assistant::parseAnswer(assistant::generationLine(g));
    REQUIRE(answer.generation);
    CHECK(answer.generation->text == g.text);
    CHECK(answer.generation->prompt_tokens == 420);
    CHECK(answer.generation->tokens == 7);
    CHECK(assistant::parseAnswer(assistant::errorLine("no context")).error == "no context");
    CHECK(assistant::parseAnswer(assistant::readyLine("Vulkan0 (GPU), CPU")).ready == "Vulkan0 (GPU), CPU");
    CHECK(assistant::parseAnswer("not json").error);
}

TEST_CASE("assistant: the child loads its model, then answers requests one after the other") {
    AssistantProcess assistant;
    std::string error;
    REQUIRE_MESSAGE(assistant.start(test::mockAssistant(), "commentary.gguf", 99, error, 30s), error);
    CHECK(assistant.running());
    CHECK(assistant.devices() == "Mock GPU");
    const auto first = assistant.generate(asked("A novelty."), &error);
    REQUIRE_MESSAGE(first, error);
    CHECK(first->text == "2 messages, seed 1, greedy, held to a grammar, on the GPU: A novelty.");
    CHECK(first->tokens == 130);
    const auto second = assistant.generate(asked("Nouveauté.", 2, 0.7f, false), &error);
    REQUIRE_MESSAGE(second, error);
    CHECK(second->text == "2 messages, seed 2, sampled, free, on the GPU: Nouveauté.");
    // A request it cannot answer leaves it running.
    CHECK_FALSE(assistant.generate(asked("refuse"), &error));
    CHECK(error == "refused");
    CHECK(assistant.running());
    CHECK(assistant.generate(asked("Again."), &error));
    // Unloading is ending it.
    assistant.stop();
    CHECK_FALSE(assistant.running());
    CHECK_FALSE(assistant.generate(asked("After."), &error));
    CHECK(error == "the assistant is not running");
}

TEST_CASE("assistant: a model that does not load says why") {
    AssistantProcess assistant;
    std::string error;
    CHECK_FALSE(assistant.start(test::mockAssistant(), "missing", 0, error, 30s));
    CHECK(error == "missing: the model does not load");
    CHECK_FALSE(assistant.running());
    CHECK_FALSE(assistant.start((test::selfPath().parent_path() / "no-such-program").string(), "x", 0, error, 30s));
}

TEST_CASE("assistant: a child that fails or hangs is told apart, and a new one starts") {
    AssistantProcess assistant;
    std::string error;
    REQUIRE_MESSAGE(assistant.start(test::mockAssistant(), "commentary.gguf", 0, error, 30s), error);
    // Its driver failed: the process is gone.
    CHECK_FALSE(assistant.generate(asked("crash"), &error));
    CHECK(error == "the assistant ended");
    CHECK_FALSE(assistant.running());
    // A new start begins anew; one that hangs is ended once its time is up.
    REQUIRE_MESSAGE(assistant.start(test::mockAssistant(), "commentary.gguf", 0, error, 30s), error);
    assistant.answer_within = 300ms;
    CHECK_FALSE(assistant.generate(asked("hang"), &error));
    CHECK(error == "the assistant did not answer in time");
    CHECK_FALSE(assistant.running());
    REQUIRE_MESSAGE(assistant.start(test::mockAssistant(), "commentary.gguf", 0, error, 30s), error);
    assistant.answer_within = 30s;
    CHECK(assistant.generate(asked("Fine again."), &error));
}

TEST_CASE("assistant: a GPU that fails leaves the model on the CPU") {
    AssistantProcess assistant;
    std::string error;
    // The driver fails while the model loads: started again on the CPU.
    REQUIRE_MESSAGE(assistant.start(test::mockAssistant(), "gpu-load-crash", 99, error, 30s), error);
    CHECK(assistant.gpuFailed());
    CHECK(assistant.devices() == "Mock (scripted)");
    // It fails while it answers: on the CPU since, and the request answered all the same.
    REQUIRE_MESSAGE(assistant.start(test::mockAssistant(), "gpu-crash", 99, error, 30s), error);
    CHECK_FALSE(assistant.gpuFailed());
    const auto answered = assistant.generate(asked("A novelty."), &error);
    REQUIRE_MESSAGE(answered, error);
    CHECK(answered->text == "2 messages, seed 1, greedy, held to a grammar, on the CPU: A novelty.");
    CHECK(assistant.gpuFailed());
    CHECK(assistant.running());
    // On the CPU from the start, a crash is told as it is.
    REQUIRE_MESSAGE(assistant.start(test::mockAssistant(), "commentary.gguf", 0, error, 30s), error);
    CHECK_FALSE(assistant.generate(asked("crash"), &error));
    CHECK(error == "the assistant ended");
    CHECK_FALSE(assistant.gpuFailed());
}

TEST_CASE("assistant: the program's comment comes from the child's text, held to the facts") {
    // A move with nothing for the model to explain gets the template's comment, the child not asked.
    AssistantProcess assistant;
    std::string error;
    REQUIRE_MESSAGE(assistant.start(test::mockAssistant(), "commentary.gguf", 0, error, 30s), error);
    auto before = commentary::Position::initial();
    for (const auto* uci : {"e2e4", "c7c5", "g1f3", "d7d6", "d2d4", "c5d4", "f3d4", "g8f6", "b1c3"})
        before.playUnchecked(*before.parseUci(uci));
    commentary::MoveReport report;
    report.mover      = commentary::Color::Black;
    report.win_before = 45.0;
    report.win_after  = 45.0;
    report.novelty    = true;
    commentary::MoveContext ctx;
    ctx.before = &before;
    ctx.played = *before.parseUci("h7h6");
    ctx.report = &report;
    const auto written =
        commentary::writtenComment(assistant, ctx, commentary::Lang::En, commentary::PieceLetters::English);
    REQUIRE(written);
    CHECK(*written == commentary::Written{"A novelty.", false, 0});
}

#endif  // FCG_ASSISTANT
