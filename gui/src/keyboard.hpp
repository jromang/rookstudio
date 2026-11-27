#pragma once

#include <string>

#include <imgui.h>

// The keys: the guards the pages' key checks share, and the shortcuts whose key is written beside their button,
// bound and named from the same chord.
namespace fcg::keyboard {

// The page's keys (the letters, Enter, Escape, the arrows) are its own: no text field has them and no popup is open.
[[nodiscard]] inline bool available() {
    return !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
}

// The same, a key pressed alone: neither Ctrl nor Alt is held.
[[nodiscard]] inline bool plain() {
    const auto& io = ImGui::GetIO();
    return available() && !io.KeyCtrl && !io.KeyAlt;
}

// Enter, or the keypad's, pressed (not repeated).
[[nodiscard]] inline bool enterPressed() {
    return ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
}

// A shortcut pressed while no text field has the keys (which keep Ctrl+V, Ctrl+Z...).
[[nodiscard]] inline bool shortcut(ImGuiKeyChord chord) {
    return !ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(chord);
}

// A chord as its hint writes it: "Ctrl+K".
[[nodiscard]] std::string name(ImGuiKeyChord chord);

// The shortcuts written beside their button.
inline constexpr ImGuiKeyChord PALETTE  = ImGuiMod_Ctrl | ImGuiKey_K;  // the command palette
inline constexpr ImGuiKeyChord OPEN_PGN = ImGuiMod_Ctrl | ImGuiKey_O;  // a PGN file on the Board page
inline constexpr ImGuiKeyChord NEW_GAME = ImGuiMod_Ctrl | ImGuiKey_N;
inline constexpr ImGuiKeyChord PASTE    = ImGuiMod_Ctrl | ImGuiKey_V;  // a game, a FEN or a Lichess link pasted
inline constexpr ImGuiKeyChord SAVE     = ImGuiMod_Ctrl | ImGuiKey_S;  // the Board page's game saved
inline constexpr ImGuiKeyChord IMPORT   = ImGuiMod_Ctrl | ImGuiKey_I;  // a file of games imported into a database
inline constexpr ImGuiKeyChord FILTER   = ImGuiMod_Ctrl | ImGuiKey_L;  // a list's quick filter
inline constexpr ImGuiKeyChord SEARCH   = ImGuiMod_Ctrl | ImGuiKey_F;  // a database's search

}  // namespace fcg::keyboard
