#pragma once

// What every test program shares (CMake's fcg_test_support), of the standard library and doctest only: the sources'
// folder, a folder of a test's own, waits bounded by what they await, a file read whole. The mock engines are in
// mock_engines.hpp, the process's temporary folder in own_temp.hpp.

#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#ifndef FCG_SOURCE_DIR
#    error "FCG_SOURCE_DIR is not defined: the test program must link fcg_test_support"
#endif

namespace fcg::test {

using namespace std::chrono_literals;

// The sources' folder, whose files some tests read: where FCG_SOURCE_DIR in the environment points (the CI's Windows
// tests, run on another machine than the one that compiled them, where the sources are checked out again), else where
// they were compiled (FCG_SOURCE_DIR, defined for every test program). A folder that is not there fails the test case.
inline std::filesystem::path sourcePath() {
    namespace fs = std::filesystem;
#ifdef _WIN32
    const wchar_t* env = _wgetenv(L"FCG_SOURCE_DIR");  // as Windows has it, whatever its letters
#else
    const char* env = std::getenv("FCG_SOURCE_DIR");
#endif
    const std::string_view compiled = FCG_SOURCE_DIR;  // UTF-8, from CMake
    const fs::path path = env && *env ? fs::path(env) : fs::path(std::u8string(compiled.begin(), compiled.end()));
    REQUIRE_MESSAGE(fs::is_directory(path), "no sources in ", path.string(), ": FCG_SOURCE_DIR names their folder");
    return path;
}

// A folder of a test's own, empty, under the process's temporary folder (own_temp.hpp): made with the object, removed
// with what it holds when the object goes, as far as it can be (on Windows, not while a process still holds a file of
// it: the process's temporary folder goes at the end). Its name, `name` and a number, is new in the process. A
// temporary object gives no path: its folder would be gone before the path is used.
class ScratchDir {
   public:
    explicit ScratchDir(std::string_view name = "scratch") {
        static std::atomic<unsigned> made{0};
        path_ = std::filesystem::temp_directory_path() / (std::string(name) + "-" + std::to_string(++made));
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        std::filesystem::create_directories(path_);
    }
    ~ScratchDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    ScratchDir(const ScratchDir&)            = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const& { return path_; }
    const std::filesystem::path& path() const&& = delete;
    operator const std::filesystem::path&() const& { return path_; }
    operator const std::filesystem::path&() const&& = delete;
    [[nodiscard]] std::filesystem::path operator/(const std::filesystem::path& name) const& { return path_ / name; }
    std::filesystem::path operator/(const std::filesystem::path& name) const&& = delete;
    [[nodiscard]] std::string string() const& { return path_.string(); }
    std::string string() const&& = delete;

   private:
    std::filesystem::path path_;
};

// True once `done()` is, false if it is not within `limit`: what a wait is bounded by is what it awaits, the limit
// only ends a test case that would wait for ever.
template <typename F>
bool waitUntil(F&& done, std::chrono::milliseconds limit = 20s) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(2ms);
    }
    return true;
}

// A file's bytes, whole: empty when it cannot be read.
inline std::string readFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace fcg::test
