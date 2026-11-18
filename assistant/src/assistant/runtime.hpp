#pragma once

// The assistant's child process: `<program> --assistant --model PATH [--gpu-layers N]` loads the model, says on its
// standard output that it is ready (or why it is not), then answers the requests of its standard input, one JSON object
// per line (protocol.hpp), until that input ends. A graphics driver that fails takes only this process down, and
// unloading the model is ending it.

#include <string>
#include <string_view>
#include <vector>

namespace fcg::assistant {

inline constexpr std::string_view FLAG = "--assistant";

// The child process's main, given the arguments after FLAG.
int run(const std::vector<std::string>& args);

}  // namespace fcg::assistant
