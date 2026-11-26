#pragma once

// The mock engines the tests play with, built next to the test program, their file names defined by CMake:
// FCG_RANDOM_MOVER, fastchess' random mover (UCI); FCG_WB_MOVER, a WinBoard engine; FCG_MOCK_ASSISTANT, a language
// model that answers at once. They are always built with the tests: one that is missing fails the test case, it does
// not skip it.

#include <doctest/doctest.hpp>

#include <filesystem>
#include <string>
#include <string_view>

#include "self_path.hpp"

namespace fcg::test {

// The mock engine of file name `file`, next to the test program: its path.
inline std::string mockEngine(std::string_view file) {
    const auto path = selfPath().parent_path() / file;
    REQUIRE_MESSAGE(std::filesystem::is_regular_file(path), "no mock engine ", path.string(),
                    ": it is built with the tests (cmake --build <folder> --target tests)");
    return path.string();
}

#ifdef FCG_RANDOM_MOVER
inline std::string randomMover() { return mockEngine(FCG_RANDOM_MOVER); }
#endif

#ifdef FCG_WB_MOVER
inline std::string wbMover() { return mockEngine(FCG_WB_MOVER); }
#endif

}  // namespace fcg::test
