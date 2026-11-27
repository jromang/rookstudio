// What the buttons, Ctrl+K and the home page's cards share (#116): a tournament's rules, Pause, Resume, Stop and Try
// again, each written once on Run.
#include <doctest/doctest.hpp>

#include <cstdint>
#include <string>

#include "run.hpp"

using namespace fcg;

namespace {

// A tournament that is only what the test says of it.
class FakeRun final : public Run {
   public:
    State state_s       = State::Idle;
    bool paused_s       = false;
    bool stopping_s     = false;
    bool stopping_idle_ = false;

    std::string applyEdit(const tour::Edit&) override { return {}; }
    std::string undoEdit() override { return {}; }
    std::string endGame(int, uint64_t, const std::string&, const std::string&) override { return {}; }
    void setPaused(bool paused) override { paused_s = paused; }
    [[nodiscard]] bool paused() const override { return paused_s; }
    bool tryEngineAgain(int) override { return false; }
    void stopNow() override { stopping_s = true; }
    void stopWhenIdle() override { stopping_idle_ = true; }
    [[nodiscard]] bool stoppingWhenIdle() const override { return stopping_idle_; }
    [[nodiscard]] bool stopping() const override { return stopping_s; }
    void update() override {}
    [[nodiscard]] State state() const override { return state_s; }
    [[nodiscard]] int runningGames() const override { return 0; }
    [[nodiscard]] LiveSource* observer() const override { return nullptr; }
    [[nodiscard]] const tour::TournamentState* tournament() const override { return nullptr; }
    [[nodiscard]] const RunInfo& info() const override { return info_; }
    [[nodiscard]] std::string error() const override { return {}; }
    [[nodiscard]] bool stoppedByUser() const override { return stopping_s; }
    [[nodiscard]] bool abnormal() const override { return false; }
    [[nodiscard]] bool takeFinishedEvent() override { return false; }

   private:
    RunInfo info_;
};

}  // namespace

TEST_CASE("commands: a tournament's Pause, Resume, Stop and Try again follow the rules its buttons had") {
    // Each rule as the buttons, Ctrl+K and the cards wrote it before it was named, for every state.
    for (int bits = 0; bits < 16; ++bits) {
        FakeRun r;
        r.state_s        = bits & 1 ? Run::State::Running : Run::State::Finished;
        r.paused_s       = (bits & 2) != 0;
        r.stopping_s     = (bits & 4) != 0;
        r.stopping_idle_ = (bits & 8) != 0;
        CAPTURE(bits);
        CHECK(r.canPause() == !r.stopping());
        CHECK(r.canStop() == !r.stopping());
        CHECK(r.canResume() == (!r.stopping() && !r.stoppingWhenIdle()));
        CHECK(r.canTryEngineAgain() == (r.running() && !r.stopping()));
        CHECK(r.pausable() == (r.running() && !r.paused() && !r.stopping()));
        CHECK(r.resumable() == (r.running() && r.paused() && !r.stopping() && !r.stoppingWhenIdle()));
    }

    // Stopped once its running games are over, it is paused, and that pause is not lifted.
    FakeRun r;
    r.state_s = Run::State::Running;
    CHECK(r.pausable());
    CHECK_FALSE(r.resumable());
    r.stopWhenIdle();
    r.setPaused(true);
    CHECK(r.canPause());
    CHECK(r.canStop());
    CHECK_FALSE(r.canResume());
    CHECK_FALSE(r.resumable());
    CHECK(r.canTryEngineAgain());
    // Stopped now, nothing more is offered.
    r.stopNow();
    CHECK_FALSE(r.canPause());
    CHECK_FALSE(r.canStop());
    CHECK_FALSE(r.canTryEngineAgain());
    CHECK_FALSE(r.pausable());
}
