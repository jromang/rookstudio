// RookStudio's shared widgets and scope guards (#110), drawn in an ImGui context of the test's own, without a window:
// each guard ends what it began, on an early return too, nested ones in the reverse order, and a popup or a table only
// when it opened; the widgets leave ImGui's stacks as they found them; a colour mixed, a move numbered. ImGui's
// recoverable errors (a missing End, an End too many) are counted, and none is expected.
#include <doctest/doctest.hpp>

#include <functional>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <chess.hpp>

#include "widgets.hpp"

using namespace fcg;

namespace {

// What the guards push on ImGui's stacks, in the page's window.
struct Stacks {
    int windows  = 0;
    int colours  = 0;
    int vars     = 0;
    int ids      = 0;
    int disabled = 0;
    int wraps    = 0;
    int popups   = 0;
    int tables   = 0;

    bool operator==(const Stacks&) const = default;
};

Stacks stacks() {
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImGuiWindow* w  = g.CurrentWindow;
    return {g.CurrentWindowStack.Size, g.ColorStack.Size,           g.StyleVarStack.Size,   w->IDStack.Size,
            g.DisabledStackSize,       w->DC.TextWrapPosStack.Size, g.BeginPopupStack.Size, g.TablesTempDataStacked};
}

// An ImGui context of the test's own, its recoverable errors counted rather than asserted.
class Frames {
   public:
    Frames() : context_(ImGui::CreateContext()) {
        auto& io = ImGui::GetIO();
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.IniFilename                     = nullptr;
        io.DisplaySize                     = ImVec2(1280, 900);
        io.DeltaTime                       = 1.0f / 60.0f;
        io.ConfigErrorRecoveryEnableAssert = false;
        context_->ErrorCallback            = [](ImGuiContext*, void* errors, const char* message) {
            static_cast<std::vector<std::string>*>(errors)->push_back(message);
        };
        context_->ErrorCallbackUserData = &errors_;
    }
    ~Frames() { ImGui::DestroyContext(context_); }
    Frames(const Frames&)            = delete;
    Frames& operator=(const Frames&) = delete;

    // A frame drawing `page` in a window filling the display (collapsed when asked: what it holds is out of sight).
    void draw(const std::function<void()>& page, bool collapsed = false) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::SetNextWindowCollapsed(collapsed);
        ImGui::Begin("page", nullptr, collapsed ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoDecoration);
        page();
        ImGui::End();
        ImGui::Render();
    }

    [[nodiscard]] const std::vector<std::string>& errors() const { return errors_; }

   private:
    ImGuiContext* context_;
    std::vector<std::string> errors_;
};

std::string joined(const std::vector<std::string>& errors) {
    std::string all;
    for (const auto& e : errors) all += e + "\n";
    return all;
}

}  // namespace

TEST_CASE("widgets: each scope guard ends what it began, on an early return too") {
    Frames frames;
    frames.draw([] {
        const auto before = stacks();
        // A guard made, the stacks grown while it lives, then left by an early return or by the scope's end.
        const auto check = [&](const std::string& what, const std::function<void()>& make) {
            CAPTURE(what);
            make();
            CHECK(stacks() == before);
        };
        for (const bool early : {true, false}) {
            CAPTURE(early);
            check("Card", [&] {
                const ui::Card card("##card");
                CHECK(stacks().windows == before.windows + 1);
                CHECK(stacks().colours == before.colours + 2);
                if (early) return;
                ImGui::TextUnformatted("in the card");
            });
            check("Tinted", [&] {
                const ui::Tinted tinted("##tinted", ui::WARN, {.fill = 0.10f, .soft_edge = true});
                CHECK(stacks().windows == before.windows + 1);
                if (early) return;
                ImGui::TextUnformatted("in the tinted card");
            });
            check("Child", [&] {
                const ui::Child child("##child", ImVec2(200, 100));
                CHECK(child);
                CHECK(stacks().windows == before.windows + 1);
                if (early) return;
                ImGui::TextUnformatted("in the child");
            });
            check("Disabled", [&] {
                const ui::Disabled disabled;
                CHECK(stacks().disabled == before.disabled + 1);
                CHECK((ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0);
                if (early) return;
                ImGui::Button("greyed");
            });
            check("Disabled(false)", [&] {
                const ui::Disabled disabled(false);
                CHECK(stacks().disabled == before.disabled + 1);
                CHECK((ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) == 0);
                if (early) return;
            });
            check("Id", [&] {
                const ui::Id by_int(7);
                const ui::Id by_text("seven");
                const ui::Id by_view(std::string_view("seven, a part of it").substr(0, 5));
                const ui::Id by_pointer(&before);
                CHECK(stacks().ids == before.ids + 4);
                if (early) return;
            });
            check("TextWrap", [&] {
                const ui::TextWrap wrap(300);
                CHECK(stacks().wraps == before.wraps + 1);
                if (early) return;
                ImGui::TextUnformatted("wrapped");
            });
            check("StyleColor", [&] {
                ui::StyleColor colours(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
                colours.push(ImGuiCol_Button, ImVec4(0, 0, 1, 1)).push(ImGuiCol_Border, ui::ACCENT);
                CHECK(stacks().colours == before.colours + 3);
                CHECK(ImGui::GetStyleColorVec4(ImGuiCol_Button).z == 1.0f);
                if (early) return;
            });
            check("StyleVar", [&] {
                ui::StyleVar vars(ImGuiStyleVar_FrameRounding, 12.0f);
                vars.push(ImGuiStyleVar_FramePadding, ImVec2(10, 3));
                CHECK(stacks().vars == before.vars + 2);
                CHECK(ImGui::GetStyle().FrameRounding == 12.0f);
                if (early) return;
            });
            check("empty StyleColor and StyleVar", [&] {
                const ui::StyleColor colours;
                const ui::StyleVar vars;
                CHECK(stacks() == before);
            });
        }
    });
    CHECK_MESSAGE(frames.errors().empty(), joined(frames.errors()));
}

TEST_CASE("widgets: nested guards end in the reverse order, back in the page's window") {
    Frames frames;
    frames.draw([] {
        auto* page        = ImGui::GetCurrentWindow();
        const auto before = stacks();
        const auto nested = [&](int leave_at) {
            const ui::Card card("##outer");
            const ui::Id id(leave_at);
            if (leave_at == 0) return;
            const ui::StyleColor colour(ImGuiCol_Text, ui::GOOD);
            const ui::Tinted tinted("##inner", ui::GOOD);
            CHECK(ImGui::GetCurrentWindow() != page);
            if (leave_at == 1) return;
            const ui::Disabled disabled;
            const ui::TextWrap wrap;
            ImGui::TextUnformatted("deepest");
        };
        for (int leave_at = 0; leave_at < 3; ++leave_at) {
            CAPTURE(leave_at);
            nested(leave_at);
            CHECK(ImGui::GetCurrentWindow() == page);
            CHECK(stacks() == before);
        }
    });
    CHECK_MESSAGE(frames.errors().empty(), joined(frames.errors()));
}

TEST_CASE("widgets: a popup or a table is ended only when it opened") {
    Frames frames;
    frames.draw([] {
        const auto before = stacks();
        {
            const ui::Popup closed("##never_opened");
            CHECK_FALSE(closed);
            CHECK(stacks() == before);
        }
        CHECK(stacks() == before);
        ImGui::OpenPopup("##menu");
        {
            const ui::Popup menu("##menu");
            REQUIRE(menu);
            CHECK(stacks().popups == before.popups + 1);
            ImGui::TextUnformatted("in the menu");
        }
        CHECK(stacks() == before);
        ImGui::OpenPopup("##modal");
        if (const auto modal = ui::Popup::modal("##modal"); modal) {
            CHECK(stacks().popups == before.popups + 1);
            ImGui::CloseCurrentPopup();
        } else {
            FAIL("the modal popup did not open");
        }
        CHECK(stacks() == before);
        {
            const ui::Table table("##table", 2);
            REQUIRE(table);
            CHECK(stacks().tables == before.tables + 1);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("a cell");
        }
        CHECK(stacks() == before);
    });
    // In a collapsed window nothing shows: the table does not begin, so it is not ended; the child is ended all the
    // same.
    frames.draw(
        [] {
            const auto before = stacks();
            {
                const ui::Table table("##hidden_table", 2);
                CHECK_FALSE(table);
            }
            {
                const ui::Child child("##hidden_child", ImVec2(100, 100));
                CHECK_FALSE(child);
            }
            CHECK(stacks() == before);
        },
        true);
    CHECK_MESSAGE(frames.errors().empty(), joined(frames.errors()));
}

TEST_CASE("widgets: the shared widgets leave ImGui's stacks as they found them") {
    Frames frames;
    frames.draw([] {
        const auto before = stacks();
        ui::wrapped("A paragraph, wrapped at the window's edge.");
        ui::wrapped("The same in grey.", true);
        for (const auto hover : {ui::PrimaryHover::Lighter, ui::PrimaryHover::Flat, ui::PrimaryHover::Translucent}) {
            CHECK_FALSE(ui::primaryButton("Start##lighter", true, 0, hover));
            CHECK_FALSE(ui::primaryButton("Start##disabled", false, ui::GOOD, hover));
            CHECK(stacks() == before);
        }
        CHECK_FALSE(ui::chip("All", true));
        CHECK_FALSE(ui::chip("Today", false, "today_chip"));
        CHECK_FALSE(ui::dashedChip("##on", "In the report", true));
        CHECK_FALSE(ui::dashedChip("##off", "+ Lichess", false));
        CHECK(stacks() == before);
        for (const auto& tint : {ui::Tint{}, ui::Tint{.fill = 0.10f}, ui::Tint{.edge = 0.4f, .soft_edge = true},
                                 ui::Tint{.padding = ImVec2(12, 8)}}) {
            ui::beginTinted("##tinted", ui::ACCENT, tint);
            CHECK(stacks().windows == before.windows + 1);
            ui::endTinted();
            CHECK(stacks() == before);
        }
    });
    CHECK_MESSAGE(frames.errors().empty(), joined(frames.errors()));
}

TEST_CASE("widgets: a colour mixed, opaque; a move numbered") {
    CHECK(ui::mix(IM_COL32(0, 0, 0, 255), IM_COL32(255, 255, 255, 255), 0.5f) == IM_COL32(128, 128, 128, 255));
    CHECK(ui::mix(IM_COL32(10, 20, 30, 0), IM_COL32(200, 100, 50, 0), 0.0f) == IM_COL32(10, 20, 30, 255));
    CHECK(ui::mix(IM_COL32(10, 20, 30, 255), IM_COL32(200, 100, 50, 64), 1.0f) == IM_COL32(200, 100, 50, 255));

    chess::Board board;
    const auto e4 = chess::uci::uciToMove(board, "e2e4");
    CHECK(ui::numbered(board, e4) == "1.e4");  // a pawn's move reads the same in every language
    board.makeMove(e4);
    CHECK(ui::numbered(board, chess::uci::uciToMove(board, "e7e5")) == "1…e5");
    board.makeMove(chess::uci::uciToMove(board, "e7e5"));
    CHECK(ui::numbered(board, std::string("Cf3")) == "2.Cf3");  // shown as it is given
    board.makeMove(chess::uci::uciToMove(board, "g1f3"));
    CHECK(ui::numbered(board, "Nc6") == "2…Nc6");
}
