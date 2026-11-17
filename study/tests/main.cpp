#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.hpp>

#include "own_temp.hpp"

namespace fastchess {
const char* version = "study-tests ";
}

int main(int argc, char** argv) {
    // Each run, each process, with a temporary folder of its own (own_temp.hpp).
    const fcg::test::OwnTempDir temp;
    return doctest::Context(argc, argv).run();
}
