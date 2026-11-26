// Chosen players followed live (#50, mockup M8, board "Prep"): those the account follows on Lichess (follow:read, asked
// at the first "Their games in the mosaic", M8 board "Points") and those added by name, never the user's own account;
// their status asked every fifteen seconds while the Lichess page shows them or their mosaic is open; their games in
// the broadcasts' mosaic, eight at most, the user's own games never.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include "app.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// A Lichess name: 2 to 30 letters, digits, "_" or "-".
bool lichessName(std::string_view name) {
    if (name.size() < 2 || name.size() > 30) return false;
    return std::all_of(name.begin(), name.end(),
                       [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; });
}

}  // namespace

std::vector<std::string> App::followedPlayers() const {
    std::vector<std::string> out;
    const auto me  = lichess_account_ ? lower(lichess_account_->username) : std::string();
    const auto add = [&](const std::string& name) {
        const auto key = lower(name);
        if (name.empty() || key == me) return;
        if (std::none_of(out.begin(), out.end(), [&](const auto& n) { return lower(n) == key; })) out.push_back(name);
    };
    for (const auto& n : lichess_following_) add(n);
    for (const auto& n : lichess_.followed) add(n);
    return out;
}

void App::pollFollowedPlayers() {
    const auto ready = [](auto& job) {
        return job.valid() && job.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    };
    // Whom the account follows: read once a session with a token allowed to, again when the token changes.
    if (ready(lichess_following_job_)) {
        auto answer = lichess_following_job_.get();
        if (answer.value)
            lichess_following_ = std::move(*answer.value);
        else
            lichess_status_error_ = answer;
    }
    const bool may_read = lichess_account_ && lichess_account_->has(lichess::SCOPE_FOLLOW_READ);
    if (!lichess_account_) {
        lichess_following_.clear();
        lichess_following_token_.clear();
    }
    if (may_read && lichess_following_token_ != lichess_account_->token && !lichess_following_job_.valid()) {
        lichess_following_token_ = lichess_account_->token;
        lichess_following_job_ =
            std::async(std::launch::async, [token = lichess_account_->token] { return lichess::followingOf(token); });
    }
    // The permission granted for their mosaic: it opens.
    if (lichess_players_then_mosaic_ && may_read) {
        lichess_players_then_mosaic_ = false;
        lichess_scope_ask_.reset();
        openPlayersMosaic(true);
    }
    // Their status, every fifteen seconds while the card or their mosaic shows.
    const bool mosaic = broadcast_round_ == lichess::PLAYERS_MOSAIC;
    const int frame   = ImGui::GetFrameCount();
    const bool shown  = frame - lichess_players_drawn_ < 3 || (mosaic && frame - broadcast_drawn_ < 3);
    if (ready(lichess_status_job_)) {
        auto answer = lichess_status_job_.get();
        if (answer.value) {
            lichess_statuses_     = std::move(*answer.value);
            lichess_status_error_ = {};
        } else {
            lichess_status_error_ = answer;
        }
    }
    const auto names = followedPlayers();
    const auto now   = std::chrono::steady_clock::now();
    if (shown && !names.empty() && !lichess_status_job_.valid() &&
        (lichess_status_asked_.time_since_epoch().count() == 0 ||
         now - lichess_status_asked_ > std::chrono::seconds(15))) {
        lichess_status_asked_ = now;
        lichess_status_job_   = std::async(std::launch::async, [names] { return lichess::playerStatuses(names); });
    }
    // Their games while their mosaic is open: those being played, the user's own never (fair play).
    if (!mosaic) {
        players_follower_.reset();
        return;
    }
    if (!players_follower_) players_follower_ = std::make_unique<lichess::PlayersFollower>();
    std::vector<std::string> ids;
    for (const auto& name : names) {
        for (const auto& s : lichess_statuses_) {
            if (s.id != lower(name) || !s.playing || s.game.empty()) continue;
            const bool mine = std::any_of(lichess_playing_.begin(), lichess_playing_.end(),
                                          [&](const auto& g) { return g.id == s.game; });
            if (!mine && std::find(ids.begin(), ids.end(), s.game) == ids.end()) ids.push_back(s.game);
        }
    }
    players_follower_->watch(ids, players_quota_);  // the streams Lichess allows, Lichess TV's counted (#86)
}

void App::openPlayersMosaic(bool asked) {
    // Whom the account follows needs one more permission, asked the first time (M8, board "Points").
    if (!asked && lichess_account_ && !lichess_account_->has(lichess::SCOPE_FOLLOW_READ) && !lichess_.follow_declined) {
        LichessScopeAsk ask;
        ask.scope          = lichess::SCOPE_FOLLOW_READ;
        ask.players        = true;
        lichess_scope_ask_ = std::move(ask);
        page_              = Page::Lichess;
        return;
    }
    openBroadcastMosaic(lichess::PLAYERS_MOSAIC);
    lichess_status_asked_ = {};  // their status asked at once
}

void App::drawFollowedPlayers() {
    lichess_players_drawn_ = ImGui::GetFrameCount();
    const ui::TopicRegion topic(guide::topics::FollowedPlayers);
    ui::beginCard("lichess_players");
    ImGui::PushTextWrapPos();
    ImGui::TextUnformatted(TR("Followed players"));
    const auto names  = followedPlayers();
    const auto status = [&](const std::string& name) -> const lichess::PlayerStatus* {
        for (const auto& s : lichess_statuses_)
            if (s.id == lower(name)) return &s;
        return nullptr;
    };
    if (names.empty())
        ImGui::TextDisabled("%s", lichess_account_
                                      ? TR("No player yet: add one by name; those your account follows come with their "
                                           "games' mosaic.")
                                      : TR("No player yet: add one by name."));
    // Those who play first, then those online; twelve at most, the others counted.
    std::vector<std::string> shown = names;
    const auto rank                = [&](const std::string& n) {
        const auto* s = status(n);
        return s && s->playing ? 0 : s && s->online ? 1 : 2;
    };
    std::stable_sort(shown.begin(), shown.end(), [&](const auto& a, const auto& b) { return rank(a) < rank(b); });
    const std::size_t most = 12;
    int playing            = 0;
    for (const auto& n : names) playing += rank(n) == 0 ? 1 : 0;
    for (std::size_t i = 0; i < std::min(shown.size(), most); ++i) {
        const auto& name = shown[i];
        const auto* s    = status(name);
        ImGui::PushID(static_cast<int>(i));
        const float r   = ImGui::GetFontSize() * 0.28f;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImU32 dot = s && s->playing ? IM_COL32(209, 67, 47, 255) : s && s->online ? ui::GOOD : ui::MUTED;
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + r, at.y + ImGui::GetTextLineHeight() / 2), r, dot);
        ImGui::Dummy(ImVec2(r * 2 + px(4), ImGui::GetTextLineHeight()));
        ImGui::SameLine();
        ImGui::TextUnformatted(name.c_str());
        ImGui::SameLine();
        if (s)
            ImGui::TextDisabled(
                "%s", s->playing
                          ? (s->clock.empty() ? std::string(TR("in a game")) : trf("in a game · {}", s->clock)).c_str()
                      : s->online ? TR("online")
                                  : TR("offline"));
        const auto hand = std::find_if(lichess_.followed.begin(), lichess_.followed.end(),
                                       [&](const auto& n) { return lower(n) == lower(name); });
        if (hand != lichess_.followed.end()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("×")) {
                lichess_.followed.erase(hand);
                saveLichessSettings();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("No longer follow"));
        }
        ImGui::PopID();
    }
    if (shown.size() > most) ImGui::TextDisabled("%s", trf("and {} more", shown.size() - most).c_str());
    // One more, by name.
    ImGui::SetNextItemWidth(px(200));
    const bool enter = ImGui::InputTextWithHint("##add_player", TR("A Lichess name"), lichess_add_player_.data(),
                                                lichess_add_player_.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const std::string typed = lichess_add_player_.data();
    if ((ImGui::SmallButton(TR("Add")) || enter) && lichessName(typed)) {
        if (std::none_of(lichess_.followed.begin(), lichess_.followed.end(),
                         [&](const auto& n) { return lower(n) == lower(typed); })) {
            lichess_.followed.push_back(typed);
            saveLichessSettings();
        }
        lichess_add_player_.fill('\0');
        lichess_status_asked_ = {};  // its status asked at once
    }
    if (!typed.empty() && !lichessName(typed))
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           TR("A Lichess name has 2 to 30 letters, digits, “_” or “-”."));
    // Their games.
    if (ui::iconButton("players_mosaic", ui::Icon::Broadcast, TR("Their games in the mosaic"),
                       !names.empty() || (lichess_account_ && !lichess_.follow_declined)))
        openPlayersMosaic();
    if (playing > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", trn("{} plays now", "{} play now", static_cast<uint64_t>(playing), playing).c_str());
    }
    ImGui::TextDisabled("%s", TR("Those your account follows, or added by name. Lichess shows their moves three moves "
                                 "late; eight games at once at most."));
    if (lichess_status_error_.failed())
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           lichess::describeAlone(lichess_status_error_).c_str());
    ImGui::PopTextWrapPos();
    ui::endCard();
}

}  // namespace fcg
