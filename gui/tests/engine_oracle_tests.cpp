// The engine that answers the assistant's questions (engine_oracle.hpp), on the mock engine.

#include <doctest/doctest.hpp>

#include <vector>

#include <commentary/position.hpp>

#include "../../tournament/tests/self_path.hpp"
#include "engine_oracle.hpp"

using namespace fcg;

namespace {

EngineEntry mockEngine() {
    EngineEntry e;
    e.name = "mock";
    e.cmd  = (fcg::test::selfPath().parent_path() / FCG_RANDOM_MOVER).string();
    return e;
}

}  // namespace

TEST_CASE("engine oracle: a line and its score from an engine of the library, kept for the next question") {
    const EngineOracle oracle(mockEngine(), 1000);
    const auto start = commentary::Position::initial();
    const auto first = oracle.search(start, {});
    REQUIRE(first);
    CHECK(!first->moves.empty());
    CHECK(first->score == commentary::Score::cp(0));
    // A second question goes to the same process.
    const auto moved = start.parseUci("e2e4");
    REQUIRE(moved);
    auto after = start;
    after.playUnchecked(*moved);
    const auto second = oracle.search(after, {});
    REQUIRE(second);
    CHECK(!second->moves.empty());
    CHECK(oracle.error().empty());
}

TEST_CASE("engine oracle: an engine that cannot start says why, and gives nothing") {
    EngineEntry missing;
    missing.name = "missing";
    missing.cmd  = (fcg::test::selfPath().parent_path() / "no-such-engine").string();
    const EngineOracle oracle(missing, 1000);
    CHECK(!oracle.search(commentary::Position::initial(), {}));
    CHECK(oracle.error().find("missing") != std::string::npos);
}
