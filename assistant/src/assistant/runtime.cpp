#include "runtime.hpp"

#include <cstdio>
#include <exception>

#include "generator.hpp"
#include "protocol.hpp"

#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

namespace fcg::assistant {

int run(const std::vector<std::string>& args) {
#ifdef _WIN32
    // The lines as they are, without "\r\n".
    _setmode(0, _O_BINARY);
    _setmode(1, _O_BINARY);
#endif
    std::string model;
    int gpu_layers = 0;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--model" && i + 1 < args.size()) {
            model = args[++i];
        } else if (args[i] == "--gpu-layers" && i + 1 < args.size()) {
            try {
                gpu_layers = std::stoi(args[++i]);
            } catch (const std::exception&) {
                gpu_layers = 0;
            }
        }
    }
    const auto say = [](const std::string& line) {
        std::fputs(line.c_str(), stdout);
        std::fflush(stdout);
    };
    if (model.empty()) {
        say(errorLine("no model given (--model PATH)"));
        return 2;
    }
    std::string error;
    auto generator = Generator::load(model, gpu_layers, error);
    if (!generator) {
        say(errorLine(error));
        return 1;
    }
    say(readyLine(generator->devices()));
    serve(stdin, stdout, *generator);
    return 0;
}

}  // namespace fcg::assistant
