// A language model for the tests of the assistant's process, speaking its protocol as the program's child does
// (`--assistant --model PATH --gpu-layers N`): it answers at once with what it was asked, in words, and where it ran.
// A model named "missing" does not load; on the GPU, one named "gpu-load-crash" falls while it loads, and one named
// "gpu-crash" while it answers. A last message "crash" ends the process, "hang" makes it wait for ever, "refuse" gets
// no answer.

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
    Echo(bool gpu, bool crash_on_gpu) : gpu_(gpu), crash_on_gpu_(crash_on_gpu) {}
    std::optional<fcg::commentary::Generation> generate(const fcg::commentary::Request& r,
                                                        std::string* error) override {
        const auto last = r.messages.empty() ? std::string() : r.messages.back().second;
        if (last == "crash" || (gpu_ && crash_on_gpu_)) std::exit(3);
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
                   (gpu_ ? ", on the GPU" : ", on the CPU") + ": " + last;
        g.tokens = r.max_tokens;
        return g;
    }

   private:
    bool gpu_, crash_on_gpu_;
};

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(0, _O_BINARY);
    _setmode(1, _O_BINARY);
#endif
    std::string model;
    int gpu_layers = 0;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string_view(argv[i]) == "--model") model = argv[i + 1];
        if (std::string_view(argv[i]) == "--gpu-layers") gpu_layers = std::atoi(argv[i + 1]);
    }
    if (model == "missing") {
        std::fputs(fcg::assistant::errorLine("missing: the model does not load").c_str(), stdout);
        return 1;
    }
    const bool gpu = gpu_layers > 0;
    if (gpu && model == "gpu-load-crash") std::exit(3);
    std::fputs(fcg::assistant::readyLine(gpu ? "Mock GPU" : "Mock (scripted)").c_str(), stdout);
    std::fflush(stdout);
    Echo echo(gpu, model == "gpu-crash");
    fcg::assistant::serve(stdin, stdout, echo);
    return 0;
}
