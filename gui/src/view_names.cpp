// The user's names (#81, Settings › Your names): the names they play under, as their databases write them, and the
// linked Lichess account's, which counts by itself. A game where one of them plays is the user's: the exercises of
// their mistakes (mistakes_page.hpp), a player's dossier, an opening report, the Play page. Drawn on the Settings page
// and on the Training page's part "My mistakes" until the first is given.

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include "app.hpp"
#include "i18n.hpp"
#include "lichess_account.hpp"
#include "mistakes.hpp"
#include "mistakes_page.hpp"
#include "ui.hpp"
#include "widgets.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

using ui::chip;
using ui::mix;

void tip(const std::string& text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

}  // namespace

std::vector<std::string> App::userNames() const {
    auto out = player_names_;
    if (lichess_account_ && !lichess_account_->username.empty() && !isUserName(lichess_account_->username, out))
        out.push_back(lichess_account_->username);
    return out;
}

void App::setPlayerNames(std::vector<std::string> names) {
    std::erase_if(names, [](const std::string& n) { return nameWords(n).empty(); });
    player_names_ = std::move(names);
    if (!options_.scripted) storage_.setPlayerNames(player_names_);
    mistakes_->lookAgain();  // the user's games looked for again at once
}

void App::drawPlayerNames(const char* id) {
    ImGui::PushID(id);
    std::optional<std::size_t> remove;
    for (std::size_t i = 0; i < player_names_.size(); ++i) {
        const auto label = player_names_[i] + "  ×";
        if (i > 0) ui::sameLineOrWrap(label.c_str());
        if (chip(label.c_str(), false, fmt::format("name{}", i).c_str())) remove = i;
        tip(TR("Click to take this name away."));
    }
    if (lichess_account_ && !lichess_account_->username.empty()) {
        const auto label = trf("{} (Lichess)", lichess_account_->username);
        if (!player_names_.empty()) ui::sameLineOrWrap(label.c_str());
        ImGui::BeginDisabled();
        (void)chip(label.c_str(), true, "lichess_name");
        ImGui::EndDisabled();
        tip(TR("The linked Lichess account's name counts by itself."));
    }
    if (remove) {
        auto names = player_names_;
        names.erase(names.begin() + static_cast<std::ptrdiff_t>(*remove));
        setPlayerNames(std::move(names));
    }
    ImGui::SetNextItemWidth(px(260));
    const bool entered =
        ImGui::InputTextWithHint("##player_name", TR("A name you play under, then Enter"), player_name_input_.data(),
                                 player_name_input_.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const bool add = ImGui::Button(TR("Add##player_name"));
    if ((entered || add) && !nameWords(player_name_input_.data()).empty()) {
        auto names = player_names_;
        names.emplace_back(player_name_input_.data());
        setPlayerNames(std::move(names));
        player_name_input_.fill(0);
    }
    ImGui::PopID();
}

}  // namespace fcg
