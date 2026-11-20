// A tournament on the user's own Lichess broadcast (#51, mockup M8, board "Moteurs"): "Broadcast on Lichess…" in the
// tournament's header opens a sheet (its name, unlisted unless "Public" is chosen, what goes to Lichess); the linked
// account's token is used once it may create broadcasts (study:write, asked at that moment, M8 board "Points"), else
// the token pasted on the Broadcast page. While it is on the air, a banner says how far it went, with its link and
// Stop.

#include <algorithm>
#include <cstring>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include "app.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// A card tinted with `tone`.
void beginTinted(const char* id, ImU32 tone) {
    const auto bg  = ImGui::ColorConvertU32ToFloat4(ui::CARD_BG);
    const auto t   = ImGui::ColorConvertU32ToFloat4(tone);
    const auto mix = [&](float k) {
        return ImVec4(bg.x + (t.x - bg.x) * k, bg.y + (t.y - bg.y) * k, bg.z + (t.z - bg.z) * k, 1.0f);
    };
    ImGui::PushStyleColor(ImGuiCol_ChildBg, mix(0.07f));
    ImGui::PushStyleColor(ImGuiCol_Border, mix(0.4f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(14), px(10)));
    ImGui::BeginChild(id, ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
}

void endTinted() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

}  // namespace

std::string App::broadcastToken() const {
    if (lichess_account_ && lichess_account_->has(lichess::SCOPE_STUDY_WRITE)) return lichess_account_->token;
    return lichess_token_;  // pasted on the Broadcast page
}

void App::openCastSheet() {
    cast_sheet_       = true;
    const auto* t     = run_->tournament();
    const auto name   = setup_.lichess_name.empty() && t ? t->spec().name : setup_.lichess_name;
    const auto length = std::min(name.size(), cast_name_.size() - 1);
    cast_name_.fill('\0');
    std::copy_n(name.data(), length, cast_name_.data());
    cast_public_ = setup_.lichess_listed;
}

void App::startCast() {
    setup_.lichess_round.clear();  // a new broadcast, or the one the tournament already has (lichess.json)
    setup_.lichess_listed    = cast_public_;
    setup_.lichess_name      = cast_name_.data();
    setup_.broadcast_lichess = true;
    saveSetupSoon();
    cast_sheet_      = false;
    cast_after_link_ = false;
}

void App::pollCast() {
    // The permission granted: the broadcast is made.
    if (cast_after_link_ && !lichess_link_ && !broadcastToken().empty()) startCast();
    if (cast_after_link_ && !lichess_link_ && broadcastToken().empty() && !lichess_error_.empty())
        cast_after_link_ = false;  // refused, or no answer: the sheet says so
}

void App::drawCastSheet() {
    if (!cast_sheet_) return;
    const ui::TopicRegion topic(guide::topics::LichessCast);
    ui::beginCard("cast_sheet");
    ImGui::PushTextWrapPos();
    ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize() * 1.1f);
    ImGui::TextUnformatted(TR("Broadcast this tournament on Lichess"));
    ImGui::PopFont();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Name"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(320));
    ImGui::InputText("##cast_name", cast_name_.data(), cast_name_.size());
    ImGui::SameLine(0, px(20));
    ImGui::TextUnformatted(TR("Visibility"));
    ImGui::SameLine();
    if (ImGui::RadioButton(TR("Unlisted: for those who have the link"), !cast_public_)) cast_public_ = false;
    ImGui::SameLine();
    if (ImGui::RadioButton(TR("Public: listed on Lichess"), cast_public_)) cast_public_ = true;
    ImGui::TextDisabled("%s",
                        TR("Each round of the tournament becomes a round of the broadcast, and each game is sent "
                           "there while it is played: its moves, its clocks, the engines' names. It is not rated; "
                           "Lichess shows it as it shows the official broadcasts, with its own analysis for those "
                           "who watch it."));
    ImGui::Spacing();
    ImGui::TextUnformatted(TR("What goes to Lichess"));
    ImGui::TextDisabled("%s", TR("The name chosen and the tournament's format and time control; for each game, its PGN "
                                 "as fastchess writes it, a hundred games at most a push. Nothing of other tournaments "
                                 "nor of your settings."));
    const bool may     = !broadcastToken().empty();
    const bool linked  = lichess_account_.has_value();
    const bool name_ok = std::strlen(cast_name_.data()) > 0;
    if (!may)
        ImGui::TextDisabled("%s", linked ? TR("One more permission: creating studies and broadcasts, asked now on "
                                              "Lichess's page.")
                                         : TR("Lichess's page opens to link your account, with the permission to "
                                              "create studies and broadcasts."));
    if (lichess_link_) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s",
                           TR("Waiting for Lichess: allow RookStudio in the browser's page, then come back here."));
    } else {
        if (!lichess_error_.empty() && !may)
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               lichess_error_.find("refused") != std::string::npos
                                   ? TR("You refused it on Lichess: nothing was made.")
                                   : trf("Lichess gave nothing ({}).", lichess_error_).c_str());
        ImGui::BeginDisabled(!name_ok);
        if (ImGui::Button(TR("Create the broadcast"))) {
            if (may) {
                startCast();
            } else {
                auto scopes = linked ? lichess_account_->scopes : std::vector<std::string>{};
                scopes.push_back(lichess::SCOPE_STUDY_WRITE);
                cast_after_link_ = true;
                startLichessLink(scopes, linked ? lichess_account_->username : std::string());
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    if (ImGui::SmallButton(TR("Cancel"))) {
        cast_sheet_      = false;
        cast_after_link_ = false;
        if (lichess_link_ && lichess_link_->cancel) *lichess_link_->cancel = true;
    }
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void App::drawCastBanner() {
    if (!setup_.broadcast_lichess) return;
    const auto status = broadcaster_.lichess();
    const ui::TopicRegion topic(guide::topics::LichessCast);
    beginTinted("cast_banner", ui::BAD);
    const float r   = ImGui::GetFontSize() * 0.25f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + r, at.y + ImGui::GetFrameHeight() / 2), r,
                                                status.running ? IM_COL32(209, 67, 47, 255) : ui::MUTED);
    ImGui::Dummy(ImVec2(r * 2 + px(4), ImGui::GetFrameHeight()));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(status.running ? TR("Live on Lichess") : TR("Lichess"));
    ImGui::SameLine();
    std::string detail;
    if (!status.error.empty())
        detail = "";
    else if (broadcastToken().empty())  // the account unlinked meanwhile, say
        detail =
            TR("no token: link the account with the permission to create broadcasts, or paste one in the "
               "Broadcast tab");
    else if (!status.running)
        detail = TR("connecting…");
    else if (status.last_push.empty())
        detail = TR("waiting for the first game");
    else
        detail = trn("{} game sent · the last at {}", "{} games sent · the last at {}", status.games, status.games,
                     status.last_push);
    ImGui::TextDisabled("%s", detail.c_str());
    // On the right: its page, Stop.
    const char* open  = TR("Open on lichess.org");
    const char* stop  = TR("Stop");
    const auto& style = ImGui::GetStyle();
    const float right =
        (status.url.empty() ? 0.0f : ImGui::CalcTextSize(open).x + style.FramePadding.x * 2 + style.ItemSpacing.x) +
        ImGui::CalcTextSize(stop).x + style.FramePadding.x * 2;
    ImGui::SameLine();
    if (const float avail = ImGui::GetContentRegionAvail().x; avail > right)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - right);
    if (!status.url.empty()) {
        if (ImGui::SmallButton(open)) SDL_OpenURL(status.url.c_str());
        ImGui::SameLine();
    }
    if (ImGui::SmallButton(stop)) {
        setup_.broadcast_lichess = false;  // the last games pushed, then the relay stops
        saveSetupSoon();
    }
    if (!status.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextWrapped("%s", status.error.c_str());
        ImGui::PopStyleColor();
    }
    endTinted();
}

}  // namespace fcg
