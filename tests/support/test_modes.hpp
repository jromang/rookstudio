#pragma once

// What a test program does instead of running its tests when they start it again, a flag as its first argument: the
// book adapter (test_main.cpp), the WinBoard adapter, an engine's child... A source of the program registers its own as
// the program starts: `[[maybe_unused]] const bool registered = fcg::test::addMode("--flag", run);`.

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fcg::test {

// A mode: given the arguments after its flag, the program's exit code.
using Mode = int (*)(const std::vector<std::string>& args);

inline std::map<std::string, Mode, std::less<>>& modes() {
    static std::map<std::string, Mode, std::less<>> registered;
    return registered;
}

// Registers `mode` under `flag`: false when the flag has one already.
inline bool addMode(std::string flag, Mode mode) { return modes().emplace(std::move(flag), mode).second; }

// The mode of `flag`, or nullptr.
inline Mode modeOf(std::string_view flag) {
    const auto it = modes().find(flag);
    return it == modes().end() ? nullptr : it->second;
}

}  // namespace fcg::test
