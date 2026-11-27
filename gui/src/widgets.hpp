#pragma once

// RookStudio's own widgets, shared by its pages (#110), and scope guards for Dear ImGui's Begin/End pairs. The shared
// ui.hpp is fastchess-gui's and stays as it is: what RookStudio adds goes here.
//
// The guards: each one ends, when it goes out of scope, what its constructor began, on every path out of the scope (an
// early return, a break, a continue). New code uses them rather than Begin/End pairs written by hand, and a function
// that is touched adopts them. A guard is named, never a temporary: `ui::Card card("##x");`, not `ui::Card("##x");`,
// which would end the card at once (the compiler warns). Those whose Begin may fail (Child, Popup, Table) say so as a
// bool:
//
//     if (const ui::Popup menu("##more"); menu) {
//         ...                                  // EndPopup when the scope ends, only if the popup opened
//     }
//     const ui::Card card("##stats");          // endCard at the end of the scope, whatever path leaves it
//     if (nothing) return;

#include <string>
#include <string_view>

#include <imgui.h>
#include <chess.hpp>

#include "ui.hpp"

namespace fcg::ui {

// `a` drawn towards `b` by `t` (0: `a`, 1: `b`), opaque.
[[nodiscard]] ImU32 mix(ImU32 a, ImU32 b, float t);

// A paragraph wrapped at the window's edge, in the disabled text's grey when `grey`.
void wrapped(const std::string& text, bool grey = false);

// How a primary button answers the mouse. Lighter, the usual: lighter under the mouse, darker when pressed. The engine
// picker's and the export sheet's keep their own until a mockup settles them: Flat, the same tone under the mouse;
// Translucent, the tone a little transparent under it; both pressed in the theme's colour.
enum class PrimaryHover { Lighter, Flat, Translucent };

// The button of the main gesture: filled with `tone` (0: the accent), its label white; greyed out and inert unless
// `enabled`. True when clicked.
bool primaryButton(const char* label, bool enabled = true, ImU32 tone = 0, PrimaryHover hover = PrimaryHover::Lighter);

// A rounded choice among a few ("All", "Today"…), filled with the accent when `chosen`; `id`, when given, tells apart
// chips of the same label. True when clicked.
bool chip(const char* label, bool chosen, const char* id = nullptr);

// A filter's chip drawn by hand: a dashed grey outline when off, the accent's colours when on. True when clicked.
bool dashedChip(const char* id, const std::string& text, bool on);

// How a tinted card is drawn: the tone's share of its background and of its border, the border drawn from the cards'
// background rather than their border colour (a softer one), and its padding in pixels at the 16 px font size.
struct Tint {
    float fill     = 0.08f;
    float edge     = 0.55f;
    bool soft_edge = false;
    ImVec2 padding = ImVec2(16, 14);
};

// A card tinted with `tone` (a verdict, a warning), filling the width and as tall as what it holds. Pair with
// endTinted(), or use the Tinted guard.
void beginTinted(const char* id, ImU32 tone, const Tint& tint = {});
void endTinted();

// A move numbered as one reads it: "12.Nf3", "12…Nf6". The move in SAN with the language's letters, or `shown` as it is
// (already in the letters it is to be shown in).
[[nodiscard]] std::string numbered(const chess::Board& before, chess::Move move);
[[nodiscard]] std::string numbered(const chess::Board& before, std::string_view shown);

// ---------------------------------------------------------------------------------------------------------------------
// Scope guards

// A card (beginCard), ended by endCard.
class [[nodiscard]] Card {
   public:
    [[nodiscard]] explicit Card(const char* id, ImVec2 size = ImVec2(0, 0)) { beginCard(id, size); }
    ~Card() { endCard(); }
    Card(const Card&)            = delete;
    Card& operator=(const Card&) = delete;
};

// A tinted card (beginTinted), ended by endTinted.
class [[nodiscard]] Tinted {
   public:
    [[nodiscard]] Tinted(const char* id, ImU32 tone, const Tint& tint = {}) { beginTinted(id, tone, tint); }
    ~Tinted() { endTinted(); }
    Tinted(const Tinted&)            = delete;
    Tinted& operator=(const Tinted&) = delete;
};

// A child window (BeginChild), ended (EndChild) whether it shows or not; false when out of sight, so that it may skip
// what it holds.
class [[nodiscard]] Child {
   public:
    [[nodiscard]] explicit Child(const char* id, ImVec2 size = ImVec2(0, 0), ImGuiChildFlags child_flags = 0,
                                 ImGuiWindowFlags window_flags = 0)
        : visible_(ImGui::BeginChild(id, size, child_flags, window_flags)) {}
    ~Child() { ImGui::EndChild(); }
    Child(const Child&)            = delete;
    Child& operator=(const Child&) = delete;
    explicit operator bool() const { return visible_; }

   private:
    bool visible_;
};

// A popup (BeginPopup, or a modal one), ended (EndPopup) only when it is open: true then.
class [[nodiscard]] Popup {
   public:
    [[nodiscard]] explicit Popup(const char* id, ImGuiWindowFlags flags = 0) : open_(ImGui::BeginPopup(id, flags)) {}
    [[nodiscard]] static Popup modal(const char* name, bool* open = nullptr, ImGuiWindowFlags flags = 0) {
        return Popup(Began{ImGui::BeginPopupModal(name, open, flags)});
    }
    ~Popup() {
        if (open_) ImGui::EndPopup();
    }
    Popup(const Popup&)            = delete;
    Popup& operator=(const Popup&) = delete;
    explicit operator bool() const { return open_; }

   private:
    struct Began {
        bool open;
    };
    explicit Popup(Began began) : open_(began.open) {}
    bool open_;
};

// A table (BeginTable), ended (EndTable) only when it shows: true then.
class [[nodiscard]] Table {
   public:
    [[nodiscard]] Table(const char* id, int columns, ImGuiTableFlags flags = 0, ImVec2 outer_size = ImVec2(0, 0),
                        float inner_width = 0.0f)
        : open_(ImGui::BeginTable(id, columns, flags, outer_size, inner_width)) {}
    ~Table() {
        if (open_) ImGui::EndTable();
    }
    Table(const Table&)            = delete;
    Table& operator=(const Table&) = delete;
    explicit operator bool() const { return open_; }

   private:
    bool open_;
};

// Widgets greyed out and inert (BeginDisabled) when `disabled`, until the end of the scope.
class [[nodiscard]] Disabled {
   public:
    [[nodiscard]] explicit Disabled(bool disabled = true) { ImGui::BeginDisabled(disabled); }
    ~Disabled() { ImGui::EndDisabled(); }
    Disabled(const Disabled&)            = delete;
    Disabled& operator=(const Disabled&) = delete;
};

// An id pushed on the id stack (PushID), popped at the end of the scope.
class [[nodiscard]] Id {
   public:
    [[nodiscard]] explicit Id(int id) { ImGui::PushID(id); }
    [[nodiscard]] explicit Id(const char* id) { ImGui::PushID(id); }
    [[nodiscard]] explicit Id(const void* id) { ImGui::PushID(id); }
    [[nodiscard]] explicit Id(std::string_view id) { ImGui::PushID(id.data(), id.data() + id.size()); }
    ~Id() { ImGui::PopID(); }
    Id(const Id&)            = delete;
    Id& operator=(const Id&) = delete;
};

// The text's wrapping position (PushTextWrapPos; 0: the window's edge), until the end of the scope.
class [[nodiscard]] TextWrap {
   public:
    [[nodiscard]] explicit TextWrap(float local_x = 0.0f) { ImGui::PushTextWrapPos(local_x); }
    ~TextWrap() { ImGui::PopTextWrapPos(); }
    TextWrap(const TextWrap&)            = delete;
    TextWrap& operator=(const TextWrap&) = delete;
};

// Colours of the style (PushStyleColor) until the end of the scope: one given to the constructor, others pushed after.
class [[nodiscard]] StyleColor {
   public:
    [[nodiscard]] StyleColor() = default;
    [[nodiscard]] StyleColor(ImGuiCol idx, ImU32 colour) { push(idx, colour); }
    [[nodiscard]] StyleColor(ImGuiCol idx, const ImVec4& colour) { push(idx, colour); }
    ~StyleColor() {
        if (count_ > 0) ImGui::PopStyleColor(count_);
    }
    StyleColor(const StyleColor&)            = delete;
    StyleColor& operator=(const StyleColor&) = delete;
    StyleColor& push(ImGuiCol idx, ImU32 colour) {
        ImGui::PushStyleColor(idx, colour);
        ++count_;
        return *this;
    }
    StyleColor& push(ImGuiCol idx, const ImVec4& colour) {
        ImGui::PushStyleColor(idx, colour);
        ++count_;
        return *this;
    }

   private:
    int count_ = 0;
};

// Variables of the style (PushStyleVar) until the end of the scope: one given to the constructor, others pushed after.
class [[nodiscard]] StyleVar {
   public:
    [[nodiscard]] StyleVar() = default;
    [[nodiscard]] StyleVar(ImGuiStyleVar idx, float value) { push(idx, value); }
    [[nodiscard]] StyleVar(ImGuiStyleVar idx, ImVec2 value) { push(idx, value); }
    ~StyleVar() {
        if (count_ > 0) ImGui::PopStyleVar(count_);
    }
    StyleVar(const StyleVar&)            = delete;
    StyleVar& operator=(const StyleVar&) = delete;
    StyleVar& push(ImGuiStyleVar idx, float value) {
        ImGui::PushStyleVar(idx, value);
        ++count_;
        return *this;
    }
    StyleVar& push(ImGuiStyleVar idx, ImVec2 value) {
        ImGui::PushStyleVar(idx, value);
        ++count_;
        return *this;
    }

   private:
    int count_ = 0;
};

}  // namespace fcg::ui
