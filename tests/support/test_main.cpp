// The main of every test program (CMake's fcg_test_support compiles it into each): a mode the tests start the program
// in (test_modes.hpp), or its test cases, in a temporary folder of the process's own (own_temp.hpp).

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.hpp>

#include <string>
#include <vector>

#ifndef _WIN32
#    include <csignal>
#endif

#include <tour/book_adapter.hpp>
#include <tour/files.hpp>

#include "own_temp.hpp"
#include "test_modes.hpp"

namespace fastchess {
// fastchess' version, in the events of the tournaments the tests play: the test program's name.
const char* version = FCG_TEST_PROGRAM " ";
}  // namespace fastchess

// AddressSanitizer's options, unless ASAN_OPTIONS says otherwise: each call's locals on the stack, not in a frame of
// the heap's (its detection of a use after return). libssh2 sends the rest of a packet it could not send whole only
// from a call given the same buffer as the first time, and a keepalive's is a local of its keepalive function: moved at
// each call, the rest of a keepalive that went in part never goes (ssh_tests.cpp). The CI's sanitizers job runs the
// tests so too.
#if defined(__has_feature)
#    if __has_feature(address_sanitizer)
#        define FCG_TEST_ASAN 1
#    endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#    define FCG_TEST_ASAN 1
#endif
#ifdef FCG_TEST_ASAN
extern "C" const char* __asan_default_options() { return "detect_stack_use_after_return=0"; }
#endif

int main(int argc, char** argv) {
#ifndef _WIN32
    // A write to an engine that just died (one that never starts, one that crashes) must not end the tests.
    std::signal(SIGPIPE, SIG_IGN);
#endif

    // The book adapter: the tests start the engines that have a book of their own through the program, as the program
    // itself does. Then the modes the program's sources added.
    fcg::test::addMode("--book-adapter", fcg::tour::runBookAdapter);
    if (argc >= 2) {
        if (const auto mode = fcg::test::modeOf(argv[1])) return mode(std::vector<std::string>(argv + 2, argv + argc));
    }

    // Each run, each process, with a temporary folder of its own.
    const fcg::test::OwnTempDir temp;
    // The tests' files need not survive a power loss, and on Windows thousands of syncs at once take minutes.
    fcg::tour::setSyncToDisk(false);
    return doctest::Context(argc, argv).run();
}
