// The linked account on the Home page's Lichess card (#46, mockup M8, board "Accueil"): a card per speed with its
// rating, its games, its curve and what it did lately; a click shows its figures; the activity of the last days. Asked
// with the link's token when the Home page shows and what was read is ten minutes old, kept a day in the profile, shown
// dated when Lichess cannot be reached.

#include <algorithm>
#include <chrono>
#include <ctime>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include "app.hpp"
#include "i18n.hpp"
#include "lichess_profile.hpp"
#include "ui.hpp"
#include "widgets.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

const char* speedName(std::string_view key) {
    if (key == "bullet") return TR("Bullet");
    if (key == "blitz") return TR("Blitz");
    if (key == "rapid") return TR("Rapid");
    if (key == "classical") return TR("Classical");
    return TR("Correspondence");
}

// "4 min ago", "3 h ago", "yesterday"…: how old what is shown is.
std::string agoText(int64_t then) {
    const auto seconds = static_cast<int64_t>(std::time(nullptr)) - then;
    if (seconds < 60) return TR("just now");
    if (seconds < 3600) return trf("{} min ago", seconds / 60);
    if (seconds < 86400) return trf("{} h ago", seconds / 3600);
    return ui::dayText(then);
}

int64_t today() { return static_cast<int64_t>(std::time(nullptr)) / 86400; }

}  // namespace

void App::pollLichessProfile() {
    // Another account, or none: its profile, its failure and its request forgotten.
    if (!lichess_account_ ||
        (lichess_profile_.value && lichess_profile_.value->username != lichess_account_->username)) {
        lichess_profile_.reset();
        lichess_figures_.clear();
        lichess_profile_loaded_ = false;
        if (!lichess_account_) return;
    }
    // What the profile kept, the first time, as old as it is.
    if (!lichess_profile_.value && !lichess_profile_loaded_) {
        lichess_profile_loaded_ = true;
        if (auto kept = lichess::keptProfile(storage_.root()); kept && kept->username == lichess_account_->username) {
            const auto age = lichess::profileAge(*kept, std::time(nullptr));
            lichess_profile_.keep(std::move(*kept), age);
        }
    }
    // Kept in the profile, unless the run is for screenshots: it leaves the profile as it was.
    if (lichess_profile_.poll() && !options_.scripted) lichess::keepProfile(storage_.root(), *lichess_profile_.value);
    if (isReady(lichess_figures_job_)) {
        auto [key, answer]    = lichess_figures_job_.get();
        lichess_figures_[key] = std::move(answer);
    }
}

void App::askLichessProfile() {
    if (lichess_account_ && lichess_profile_.due())
        lichess_profile_.ask([token = lichess_account_->token](const TaskState<>& state) {
            return lichess::accountProfile(token, &state.cancel);
        });
}

void App::drawLichessAccountPanel() {
    if (!lichess_account_) return;
    askLichessProfile();
    if (!lichess_profile_.value) {
        if (lichess_profile_.failed()) {  // a token refused: Link Lichess again… (#141); asked again when due
            ImGui::PushTextWrapPos();
            drawRepairNotice(
                lichessNotice(lichess_profile_, lichess::gaveNothing(lichess_profile_), lichessAccess(), {}),
                "lichess_profile");
            ImGui::PopTextWrapPos();
        } else {
            ImGui::TextDisabled("%s", TR("Asking Lichess…"));
        }
        return;
    }
    const auto& p = *lichess_profile_.value;
    ImGui::TextDisabled("%s", trf("read {} · kept a day", agoText(p.read_at)).c_str());
    if (lichess_profile_.failed()) {  // what is shown was read earlier
        ImGui::SameLine();
        ImGui::TextDisabled("· %s", lichess::describeAlone(lichess_profile_).c_str());
    }

    // A card per speed: its rating, its games, its curve, what it did lately; a click shows its figures.
    const float gap   = px(8);
    const float avail = ImGui::GetContentRegionAvail().x;
    const int count   = static_cast<int>(p.speeds.size());
    const float w     = std::max(px(110), (avail - gap * static_cast<float>(count - 1)) / static_cast<float>(count));
    const float h     = px(104);
    ImDrawList* dl    = ImGui::GetWindowDrawList();
    for (int i = 0; i < count; ++i) {
        const auto& s = p.speeds[static_cast<std::size_t>(i)];
        if (i > 0) ImGui::SameLine(0, gap);
        const ui::Id id(i);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##speed", ImVec2(w, h))) lichess_speed_ = i;
        const bool chosen  = lichess_speed_ == i;
        const bool hovered = ImGui::IsItemHovered();
        dl->AddRectFilled(at, ImVec2(at.x + w, at.y + h), chosen ? ui::SELECT : ui::CARD_BG, px(8));
        dl->AddRect(at, ImVec2(at.x + w, at.y + h),
                    chosen    ? ui::ACCENT
                    : hovered ? ui::CARD_BD
                              : ui::CARD_BD,
                    px(8), 0, chosen ? px(2) : px(1));
        const float pad = px(10);
        dl->AddText(ImVec2(at.x + pad, at.y + px(6)), ui::MUTED, speedName(s.key));
        const auto games   = trn("{} game", "{} games", static_cast<uint64_t>(s.games), ui::exactCount(s.games));
        const ImVec2 gsize = ImGui::CalcTextSize(games.c_str());
        dl->AddText(ImVec2(at.x + w - pad - gsize.x, at.y + px(6)), ui::MUTED, games.c_str());
        // The rating, large; "?" while Lichess is not sure of it.
        const auto rating = s.games == 0
                                ? std::string("—")
                                : std::to_string(s.rating) + (s.provisional ? std::string("?") : std::string());
        ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.45f);
        dl->AddText(ImVec2(at.x + pad, at.y + px(24)), ImGui::GetColorU32(ImGuiCol_Text), rating.c_str());
        ImGui::PopFont();
        // Its curve, the last six months.
        std::vector<lichess::RatingPoint> curve;
        for (const auto& point : s.history)
            if (point.day >= today() - 183) curve.push_back(point);
        const auto progress = lichess::progressOf(s, today());
        if (curve.size() >= 2) {
            int lo = curve.front().rating, hi = lo;
            for (const auto& point : curve) lo = std::min(lo, point.rating), hi = std::max(hi, point.rating);
            const float x0 = at.x + pad, x1 = at.x + w - pad, y0 = at.y + px(56), y1 = at.y + px(80);
            const float span = static_cast<float>(std::max(1, hi - lo));
            const auto first = static_cast<float>(curve.front().day);
            const float days = std::max(1.0f, static_cast<float>(curve.back().day) - first);
            std::vector<ImVec2> line;
            for (const auto& point : curve)
                line.emplace_back(x0 + (static_cast<float>(point.day) - first) / days * (x1 - x0),
                                  y1 - (static_cast<float>(point.rating - lo) / span) * (y1 - y0));
            dl->AddPolyline(line.data(), static_cast<int>(line.size()),
                            progress.known && progress.delta < 0 ? ui::WARN : ui::ACCENT, ImDrawFlags_None, px(2));
        }
        // What it did lately.
        std::string lately;
        ImU32 tone = ui::MUTED;
        if (s.games > 0 && s.provisional) {
            lately = TR("provisional rating");
        } else if (progress.known) {
            const auto signed_delta = (progress.delta > 0   ? "+"
                                       : progress.delta < 0 ? "−"
                                                            : "±") +
                                      std::to_string(std::abs(progress.delta));
            lately = progress.week ? trf("{} this week", signed_delta) : trf("{} this month", signed_delta);
            tone   = progress.delta > 0 ? ui::GOOD : progress.delta < 0 ? ui::WARN : ui::MUTED;
        }
        if (!lately.empty()) dl->AddText(ImVec2(at.x + pad, at.y + px(84)), tone, lately.c_str());
        if (hovered) ImGui::SetTooltip("%s", trf("{}: its figures", speedName(s.key)).c_str());
    }

    // The figures of the speed chosen, asked when it is first shown.
    ImGui::Dummy(ImVec2(0, px(4)));
    const auto& chosen = p.speeds[static_cast<std::size_t>(std::clamp(lichess_speed_, 0, count - 1))];
    const auto figures = lichess_figures_.find(chosen.key);
    if (figures == lichess_figures_.end() && !lichess_figures_job_.valid() && chosen.games > 0) {
        lichess_figures_job_ = std::async(
            std::launch::async,
            [user = p.username, key = chosen.key, token = lichess_account_->token, stop = jobs_.cancelFlag()] {
                return std::pair{key, lichess::perfFigures(user, key, token, stop.get())};
            });
    }
    const float half = (ImGui::GetContentRegionAvail().x - px(14)) * 0.5f;
    {
        const ui::Child box("##lichess_figures", ImVec2(half, 0),
                            ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(trf("{} · its figures", speedName(chosen.key)).c_str());
        const auto row = [&](const char* name, const std::string& value) {
            ImGui::TextDisabled("%s", name);
            ImGui::SameLine(px(170));
            ImGui::TextUnformatted(value.c_str());
        };
        if (chosen.games == 0) {
            ImGui::TextDisabled("%s", TR("No game in this speed yet."));
        } else if (figures == lichess_figures_.end()) {
            ImGui::TextDisabled("%s", TR("Asking Lichess…"));
        } else if (!figures->second.value) {
            ImGui::TextDisabled("%s", lichess::gaveNothing(figures->second).c_str());
        } else {
            const auto& f = *figures->second.value;
            if (f.highest > 0)
                row(TR("Best rating"), f.highest_at.empty()
                                           ? ui::exactCount(f.highest)
                                           : trf("{}, on {}", ui::exactCount(f.highest), f.highest_at));
            if (f.best_win > 0) row(TR("Best win"), trf("against {}", ui::exactCount(f.best_win)));
            if (f.streak > 0)
                row(TR("Longest winning streak"), trn("{} win", "{} wins", static_cast<uint64_t>(f.streak), f.streak));
            row(TR("Results"),
                fmt::format("+{} ={} −{}", ui::exactCount(f.win), ui::exactCount(f.draw), ui::exactCount(f.loss)));
            if (f.seconds > 0) row(TR("Time played"), trf("{} h", ui::exactCount(f.seconds / 3600)));
        }
    }
    ImGui::SameLine(0, px(14));
    // The last days.
    const ui::Child box("##lichess_activity", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
    ImGui::TextUnformatted(TR("These last days"));
    if (p.activity.empty()) ImGui::TextDisabled("%s", TR("Nothing played lately."));
    for (std::size_t i = 0; i < p.activity.size() && i < 3; ++i) {
        const auto& a    = p.activity[i];
        std::string line = ui::dayText(a.start / 1000);
        for (const auto& g : a.games) {
            const int n = g.win + g.draw + g.loss;
            line += " · " + trn("{} {} game", "{} {} games", static_cast<uint64_t>(n), n, speedName(g.key)) +
                    fmt::format(", +{} ={} −{}", g.win, g.draw, g.loss);
        }
        if (a.puzzles_win + a.puzzles_loss > 0)
            line += " · " + trn("{} puzzle, {} right", "{} puzzles, {} right",
                                static_cast<uint64_t>(a.puzzles_win + a.puzzles_loss), a.puzzles_win + a.puzzles_loss,
                                a.puzzles_win);
        if (a.studies > 0)
            line += " · " + trn("{} study updated", "{} studies updated", static_cast<uint64_t>(a.studies), a.studies);
        const ui::TextWrap wrap;
        ImGui::TextDisabled("%s", line.c_str());
    }
}

}  // namespace fcg
