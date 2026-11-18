// A language model for the tests of the assistant's process, speaking its protocol as the program's child does
// (`--assistant --model PATH --gpu-layers N`): it answers at once with what it was asked, in words. A model named
// "missing" does not load; a last message "crash" ends the process, "hang" makes it wait for ever, "refuse" gets no
// answer.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>

#include <assistant/protocol.hpp>

#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

namespace {

class Echo final : public fcg::commentary::Model {
   public:
    std::optional<fcg::commentary::Generation> generate(const fcg::commentary::Request& r,
                                                        std::string* error) override {
        const auto last = r.messages.empty() ? std::string() : r.messages.back().second;
        if (last == "crash") std::exit(3);
        if (last == "hang") {
            for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (last == "refuse") {
            if (error) *error = "refused";
            return std::nullopt;
        }
        fcg::commentary::Generation g;
        g.text   = std::to_string(r.messages.size()) + " messages, seed " + std::to_string(r.seed) +
                   (r.temperature > 0.0f ? ", sampled" : ", greedy") + (r.grammar ? ", held to a grammar" : ", free") +
                   ": " + last;
        g.tokens = r.max_tokens;
        return g;
    }
};

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(0, _O_BINARY);
    _setmode(1, _O_BINARY);
#endif
    std::string model;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string_view(argv[i]) == "--model") model = argv[i + 1];
    }
    if (model == "missing") {
        std::fputs(fcg::assistant::errorLine("missing: the model does not load").c_str(), stdout);
        return 1;
    }
    std::fputs(fcg::assistant::readyLine("Mock (scripted)").c_str(), stdout);
    std::fflush(stdout);
    Echo echo;
    fcg::assistant::serve(stdin, stdout, echo);
    return 0;
}
