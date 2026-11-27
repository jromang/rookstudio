#include "widgets.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/include/fmt/format.h>

#include "board_text.hpp"

namespace fcg::ui {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

}  // namespace

ImU32 mix(ImU32 a, ImU32 b, float t) {
    const auto fa = ImGui::ColorConvertU32ToFloat4(a), fb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(fa.x + (fb.x - fa.x) * t, fa.y + (fb.y - fa.y) * t, fa.z + (fb.z - fa.z) * t, 1.0f));
}

void wrapped(const std::string& text, bool grey) {
    StyleColor colour;
    if (grey) colour.push(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
}

bool primaryButton(const char* label, bool enabled, ImU32 tone, PrimaryHover hover) {
    if (!tone) tone = ACCENT;
    const Disabled disabled(!enabled);
    StyleColor colours(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(tone));
    switch (hover) {
        case PrimaryHover::Lighter:
            colours.push(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(mix(tone, IM_COL32_WHITE, 0.15f)));
            colours.push(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(mix(tone, IM_COL32_BLACK, 0.15f)));
            break;
        case PrimaryHover::Flat:
            colours.push(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(tone));
            break;
        case PrimaryHover::Translucent:
            colours.push(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4((tone & 0x00ffffff) | 0xdd000000));
            break;
    }
    colours.push(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    return ImGui::Button(label);
}

bool chip(const char* label, bool chosen, const char* id) {
    const auto& style = ImGui::GetStyle();
    StyleVar vars(ImGuiStyleVar_FrameRounding, px(12));
    vars.push(ImGuiStyleVar_FramePadding, ImVec2(px(10), style.FramePadding.y * 0.8f));
    StyleColor colours(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(chosen ? mix(CARD_BG, ACCENT, 0.30f) : CARD_BG));
    colours.push(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(chosen ? ACCENT : CARD_BD));
    vars.push(ImGuiStyleVar_FrameBorderSize, 1.0f);
    return ImGui::Button(id ? fmt::format("{}##{}", label, id).c_str() : label);
}

bool dashedChip(const char* id, const std::string& text, bool on) {
    const ImVec2 pad(px(10), px(3));
    const ImVec2 label = ImGui::CalcTextSize(text.c_str());
    const ImVec2 size(label.x + pad.x * 2, label.y + pad.y * 2);
    const ImVec2 pos     = ImGui::GetCursorScreenPos();
    const bool clicked   = ImGui::InvisibleButton(id, size);
    const bool hovered   = ImGui::IsItemHovered();
    auto* dl             = ImGui::GetWindowDrawList();
    const ImVec2 end     = ImVec2(pos.x + size.x, pos.y + size.y);
    const float rounding = size.y * 0.5f;
    if (on) {
        dl->AddRectFilled(pos, end, (ACCENT & 0x00ffffff) | (hovered ? 0x55000000 : 0x33000000), rounding);
        dl->AddRect(pos, end, ACCENT, rounding);
    } else {
        if (hovered) dl->AddRectFilled(pos, end, ImGui::GetColorU32(ImGuiCol_FrameBgHovered), rounding);
        const ImU32 col  = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const float dash = px(4), gap = px(3);
        const auto line = [&](ImVec2 a, ImVec2 b) {
            const float length = std::hypot(b.x - a.x, b.y - a.y);
            for (float t = 0; t < length; t += dash + gap) {
                const float u = std::min(t + dash, length);
                dl->AddLine(ImVec2(a.x + (b.x - a.x) * t / length, a.y + (b.y - a.y) * t / length),
                            ImVec2(a.x + (b.x - a.x) * u / length, a.y + (b.y - a.y) * u / length), col);
            }
        };
        line(ImVec2(pos.x + rounding, pos.y), ImVec2(end.x - rounding, pos.y));
        line(ImVec2(pos.x + rounding, end.y - 1), ImVec2(end.x - rounding, end.y - 1));
        constexpr float pi = 3.14159265f;
        dl->PathArcTo(ImVec2(pos.x + rounding, pos.y + rounding), rounding - 0.5f, pi * 0.5f, pi * 1.5f, 10);
        dl->PathStroke(col, 0);
        dl->PathArcTo(ImVec2(end.x - rounding, pos.y + rounding), rounding - 0.5f, -pi * 0.5f, pi * 0.5f, 10);
        dl->PathStroke(col, 0);
    }
    dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), on ? ACCENT : ImGui::GetColorU32(ImGuiCol_Text), text.c_str());
    return clicked;
}

void beginTinted(const char* id, ImU32 tone, const Tint& tint) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(mix(CARD_BG, tone, tint.fill)));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImGui::ColorConvertU32ToFloat4(mix(tint.soft_edge ? CARD_BG : CARD_BD, tone, tint.edge)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(tint.padding.x), px(tint.padding.y)));
    ImGui::BeginChild(id, ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
}

void endTinted() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

std::string numbered(const chess::Board& before, chess::Move move) {
    return numbered(before, localSan(chess::uci::moveToSan(before, move)));
}

std::string numbered(const chess::Board& before, std::string_view shown) {
    return fmt::format("{}{}{}", before.fullMoveNumber(), before.sideToMove() == chess::Color::WHITE ? "." : "…",
                       shown);
}

}  // namespace fcg::ui
