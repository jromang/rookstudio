// What the buttons, Ctrl+K and the home page's cards share (#116): a tournament's rules, Pause, Resume, Stop and Try
// again, each written once on Run; the keys' guards and the shortcuts written beside their button (keyboard.hpp),
// read in an ImGui context of the test's own, without a window.
#include <doctest/doctest.hpp>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include <imgui.h>

#include "keyboard.hpp"
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

// An ImGui context of the test's own; a frame is begun with the keys given held: those newly held pressed, those of
// the frame before no longer held let go (Dear ImGui takes one change of a key a frame).
class Frames {
   public:
    Frames() : context_(ImGui::CreateContext()) {
        auto& io = ImGui::GetIO();
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800, 600);
        io.DeltaTime   = 1.0f / 60.0f;
        // On macOS Dear ImGui swaps Ctrl and Cmd by default: the Ctrl events the tests send are meant as Ctrl.
        io.ConfigMacOSXBehaviors = false;
    }
    ~Frames() {
        if (open_) ImGui::EndFrame();
        ImGui::DestroyContext(context_);
    }
    Frames(const Frames&)            = delete;
    Frames& operator=(const Frames&) = delete;

    // Ends the frame begun, if any, and begins another with `keys` held.
    void begin(std::initializer_list<ImGuiKey> keys = {}) {
        if (open_) ImGui::EndFrame();
        auto& io = ImGui::GetIO();
        const std::vector<ImGuiKey> now(keys);
        for (const auto key : held_)
            if (std::find(now.begin(), now.end(), key) == now.end()) io.AddKeyEvent(key, false);
        for (const auto key : now)
            if (std::find(held_.begin(), held_.end(), key) == held_.end()) io.AddKeyEvent(key, true);
        held_ = now;
        ImGui::NewFrame();
        open_ = true;
    }

   private:
    ImGuiContext* context_;
    std::vector<ImGuiKey> held_;
    bool open_ = false;
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

TEST_CASE("commands: Ctrl+K offers Pause or Resume as the tournament's card does, another tab's as the shown one's") {
    // Every state: Resume where the card's Resume can be clicked, Pause where its Pause can, never both.
    for (int bits = 0; bits < 16; ++bits) {
        FakeRun r;
        r.state_s        = bits & 1 ? Run::State::Running : Run::State::Finished;
        r.paused_s       = (bits & 2) != 0;
        r.stopping_s     = (bits & 4) != 0;
        r.stopping_idle_ = (bits & 8) != 0;
        CAPTURE(bits);
        const auto offer = r.pauseOffer();
        CHECK((offer == Run::PauseOffer::Resume) == (r.running() && r.paused() && r.canResume()));
        CHECK((offer == Run::PauseOffer::Pause) == (r.running() && !r.paused() && r.canPause()));
    }
    // Stopping once its running games are over, it is paused: neither Resume (the card refuses it) nor Pause.
    FakeRun r;
    r.state_s = Run::State::Running;
    CHECK(r.pauseOffer() == Run::PauseOffer::Pause);
    r.setPaused(true);
    CHECK(r.pauseOffer() == Run::PauseOffer::Resume);
    r.stopWhenIdle();
    CHECK_FALSE(r.canResume());
    CHECK(r.pauseOffer() == Run::PauseOffer::None);
    // Stopped now: nothing either.
    FakeRun stopped;
    stopped.state_s = Run::State::Running;
    stopped.stopNow();
    CHECK(stopped.pauseOffer() == Run::PauseOffer::None);
    // Not playing: nothing.
    FakeRun idle;
    idle.paused_s = true;
    CHECK(idle.pauseOffer() == Run::PauseOffer::None);
}

TEST_CASE("commands: the page's keys are its own without a text field or a popup taking them") {
    Frames frames;
    frames.begin();
    CHECK(keyboard::available());
    CHECK(keyboard::plain());

    // A text field has them.
    ImGui::GetIO().WantTextInput = true;
    CHECK_FALSE(keyboard::available());
    CHECK_FALSE(keyboard::plain());
    ImGui::GetIO().WantTextInput = false;

    // A popup open: the page's keys are not its own, the popup's are.
    ImGui::OpenPopup("menu");
    CHECK_FALSE(keyboard::available());
    if (ImGui::BeginPopup("menu")) {
        CHECK(keyboard::available());
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    CHECK(keyboard::available());

    // Ctrl or Alt held: available, but no plain key.
    frames.begin({ImGuiMod_Ctrl});
    CHECK(keyboard::available());
    CHECK_FALSE(keyboard::plain());
    frames.begin({ImGuiMod_Alt});
    CHECK_FALSE(keyboard::plain());
    frames.begin({ImGuiMod_Shift});
    CHECK(keyboard::plain());
    frames.begin();
    CHECK(keyboard::plain());
}

TEST_CASE("commands: Enter is either Enter key, pressed once; a shortcut waits for no text field") {
    Frames frames;
    frames.begin({ImGuiKey_Enter});
    CHECK(keyboard::enterPressed());
    frames.begin();
    CHECK_FALSE(keyboard::enterPressed());
    frames.begin({ImGuiKey_KeypadEnter});
    CHECK(keyboard::enterPressed());

    frames.begin({ImGuiMod_Ctrl, ImGuiKey_K});
    CHECK(keyboard::shortcut(keyboard::PALETTE));
    CHECK_FALSE(keyboard::shortcut(keyboard::SAVE));
    ImGui::GetIO().WantTextInput = true;  // a field keeps Ctrl+K, Ctrl+V...
    CHECK_FALSE(keyboard::shortcut(keyboard::PALETTE));
    ImGui::GetIO().WantTextInput = false;
    // Ctrl+Shift+K is another chord.
    frames.begin();
    frames.begin({ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiKey_K});
    CHECK_FALSE(keyboard::shortcut(keyboard::PALETTE));
}

TEST_CASE("commands: the shortcuts beside their button are written as their hints were typed") {
    Frames frames;
    frames.begin();
    CHECK(keyboard::name(keyboard::PALETTE) == "Ctrl+K");
    CHECK(keyboard::name(keyboard::OPEN_PGN) == "Ctrl+O");
    CHECK(keyboard::name(keyboard::NEW_GAME) == "Ctrl+N");
    CHECK(keyboard::name(keyboard::PASTE) == "Ctrl+V");
    CHECK(keyboard::name(keyboard::SAVE) == "Ctrl+S");
    CHECK(keyboard::name(keyboard::IMPORT) == "Ctrl+I");
    CHECK(keyboard::name(keyboard::FILTER) == "Ctrl+L");
    CHECK(keyboard::name(keyboard::SEARCH) == "Ctrl+F");
}
