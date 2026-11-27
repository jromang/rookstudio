#include "keyboard.hpp"

#include <imgui_internal.h>

namespace fcg::keyboard {

std::string name(ImGuiKeyChord chord) { return ImGui::GetKeyChordName(chord); }

}  // namespace fcg::keyboard
