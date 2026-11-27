#include "first_start.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <system_error>

#include <imgui.h>
#include <json.hpp>

#include "i18n.hpp"
#include "keyboard.hpp"
#include "logo.hpp"
#include "ui.hpp"
#include "views.hpp"

namespace fs = std::filesystem;

namespace fcg::first {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

std::optional<nlohmann::json> readJson(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    try {
        return nlohmann::json::parse(in);
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

int listed(const fs::path& file) {
    const auto json = readJson(file);
    return json && json->is_array() ? static_cast<int>(json->size()) : 0;
}

// The tournaments folder of a profile, as its settings.json names it: its own runs folder unless another was chosen.
fs::path tournamentsFolder(const fs::path& profile) {
    if (const auto settings = readJson(profile / "settings.json"); settings && settings->is_object()) {
        const auto dir = settings->value("tournaments_dir", std::string{});
        if (!dir.empty() && fs::path(dir).is_absolute()) return fs::path(dir);
    }
    return profile / "runs";
}

}  // namespace

int shownFirst(const Page& page) {
    // fastchess-gui's users arrive in Tests, the others in Study, when chosen; else the first view chosen.
    const int wanted = page.legacy.empty() ? 1 : 0;
    if (page.views[static_cast<std::size_t>(wanted)]) return wanted;
    for (int i = 0; i < 3; ++i)
        if (page.views[static_cast<std::size_t>(i)]) return i;
    return 0;
}

bool fresh(const fs::path& profile) {
    const auto settings = readJson(profile / "settings.json");
    return !settings || !settings->is_object() || (!settings->contains("space") && !settings->contains("views"));
}

fs::path legacyProfile(const fs::path& own) {
    const auto usable = [](const fs::path& dir) {
        std::error_code ec;
        return fs::is_regular_file(dir / "engines.json", ec) || fs::is_regular_file(dir / "setup.json", ec) ||
               fs::is_regular_file(dir / "settings.json", ec);
    };
    if (const char* env = std::getenv("FCG_LEGACY_PROFILE"); env && *env)
        return usable(env) ? fs::path(env) : fs::path();
    auto folder = own;
    if (!folder.has_filename()) folder = folder.parent_path();  // ".../rookstudio/"
    const auto legacy = folder.parent_path() / "fastchess" / "fastchess-gui";
    return usable(legacy) ? legacy : fs::path();
}

bool copied(const fs::path& relative) {
    const auto name = relative.filename().string();
    if (relative == "running.json" || relative == "queue.json" || relative == "imgui.ini") return false;
    const auto ext = relative.extension().string();
    return ext != ".log" && ext != ".sock" && ext != ".lock" && name != "control.json";
}

ProfileSummary summarize(const fs::path& profile) {
    ProfileSummary out;
    std::error_code ec;
    out.engines  = listed(profile / "engines.json");
    out.books    = listed(profile / "books.json");
    out.settings = fs::is_regular_file(profile / "settings.json", ec);
    for (fs::directory_iterator it(tournamentsFolder(profile), ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec)) ++out.tournaments;
    ec.clear();
    for (fs::recursive_directory_iterator it(profile, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const auto relative = fs::relative(it->path(), profile, ec);
        if (!copied(relative)) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec)) out.bytes += it->file_size(ec);
    }
    return out;
}

bool copyProfile(const fs::path& from, const fs::path& to, std::string& error, std::atomic<uint64_t>* done) {
    std::error_code ec;
    fs::create_directories(to, ec);
    for (fs::recursive_directory_iterator it(from, fs::directory_options::skip_permission_denied, ec), end; it != end;
         it.increment(ec)) {
        if (ec) break;
        const auto relative = fs::relative(it->path(), from, ec);
        if (ec) break;
        if (!copied(relative)) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        const auto target = to / relative;
        if (it->is_directory(ec)) {
            fs::create_directories(target, ec);
        } else if (it->is_regular_file(ec)) {
            // A file the program's profile has already is replaced: removed first. On Windows, MinGW's copy_file
            // refuses to write over it (overwrite_existing) since every file has the same inode there: a target is
            // taken for the source itself.
            fs::remove(target, ec);
            if (!ec) fs::copy_file(it->path(), target, ec);
            if (!ec && done) *done += it->file_size(ec);
        }
        if (ec) break;
    }
    if (ec) {
        error = ec.message();
        return false;
    }
    return true;
}

bool drawPage(Page& page) {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(32), px(28)));
    ImGui::Begin("##first_start", nullptr, flags);
    ImGui::PopStyleVar();

    bool chosen       = false;
    const float width = std::min(ImGui::GetContentRegionAvail().x, px(900));
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - width) * 0.5f));
    ImGui::BeginGroup();

    // The logo, the welcome, the language.
    {
        const auto pos   = ImGui::GetCursorScreenPos();
        const float size = px(44);
        logo::draw(ImGui::GetWindowDrawList(), pos, size, ui::ACCENT, IM_COL32_WHITE);
        ImGui::Dummy(ImVec2(size, size));
        ImGui::SameLine(0, px(14));
        ImGui::BeginGroup();
        ui::pageTitle(TR("Welcome to RookStudio"));
        ImGui::TextDisabled("%s", TR("What do you want to do first?"));
        ImGui::EndGroup();
        ImGui::SameLine(width - px(220));
        ImGui::SetNextItemWidth(px(220));
        const auto& current = i18n::info(i18n::language());
        if (ImGui::BeginCombo("##language", current.name)) {
            for (const auto& l : i18n::languages()) {
                if (ImGui::Selectable(l.name, l.language == current.language)) {
                    i18n::setLanguage(l.language);
                    page.language = l.code;
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::Dummy(ImVec2(0, px(18)));

    // The views offered (M6, board 4), each ticked or not: one at least.
    ImGui::TextUnformatted(TR("What do you want to do here? A view for each thing."));
    ImGui::Dummy(ImVec2(0, px(6)));
    const auto offered  = views::proposed();
    const char* what[3] = {
        TR("Make engines play each other: tournaments, opening suites, ratings."),
        TR("Your games and others': databases, notation, the opening tree, Lichess, the assistant."),
        TR("Both at once, every page in one rail."),
    };
    const char* which[3] = {TR("Tournaments · Engines · Books · Board · Suites · Ratings · Log"),
                            TR("Home · Board · Databases · Openings · Lichess · Engines"), TR("Every page")};
    const float card_w   = (width - 2 * px(14)) / 3.0f;
    const float card_h   = px(200);
    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine(0, px(14));
        const auto& colour = views::palette()[static_cast<std::size_t>(offered[static_cast<std::size_t>(i)].colour)];
        const bool on      = page.views[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_Border, on ? ImGui::ColorConvertU32ToFloat4(colour.full)
                                                  : ImGui::GetStyleColorVec4(ImGuiCol_Border));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, on ? 2.0f : 1.0f);
        ImGui::BeginChild("##view", ImVec2(card_w, card_h), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
        auto* dl       = ImGui::GetWindowDrawList();
        const auto top = ImGui::GetCursorScreenPos();
        const float sq = px(34);
        dl->AddRectFilled(ImVec2(top.x + px(4), top.y + px(4)), ImVec2(top.x + px(4) + sq, top.y + px(4) + sq),
                          on ? colour.full : colour.tint, px(9));
        ui::drawIcon(dl, ui::viewIcon(offered[static_cast<std::size_t>(i)].icon),
                     ImVec2(top.x + px(4) + sq * 0.5f, top.y + px(4) + sq * 0.5f), px(20),
                     on ? IM_COL32_WHITE : colour.full);
        // The tick, at the right.
        const ImVec2 box(top.x + card_w - px(40), top.y + px(10));
        dl->AddRect(box, ImVec2(box.x + px(18), box.y + px(18)), on ? colour.full : ImGui::GetColorU32(ImGuiCol_Border),
                    px(4), 0, px(1.5f));
        if (on) {
            dl->AddRectFilled(box, ImVec2(box.x + px(18), box.y + px(18)), colour.full, px(4));
            dl->AddPolyline(
                std::array<ImVec2, 3>{ImVec2(box.x + px(4), box.y + px(9)), ImVec2(box.x + px(8), box.y + px(13)),
                                      ImVec2(box.x + px(14), box.y + px(5))}
                    .data(),
                3, IM_COL32_WHITE, 0, px(2));
        }
        ImGui::Dummy(ImVec2(0, sq + px(10)));
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.2f);
        ImGui::TextUnformatted(offered[static_cast<std::size_t>(i)].name.c_str());
        ImGui::PopFont();
        ImGui::PushTextWrapPos(card_w - px(16));
        ImGui::TextUnformatted(what[i]);
        ImGui::TextDisabled("%s", which[i]);
        ImGui::PopTextWrapPos();
        // The whole card is the button; the last one ticked stays.
        ImGui::SetCursorScreenPos(ImGui::GetWindowPos());
        if (ImGui::InvisibleButton("##pick", ImGui::GetWindowSize())) {
            auto next                         = page.views;
            next[static_cast<std::size_t>(i)] = !next[static_cast<std::size_t>(i)];
            if (next[0] || next[1] || next[2]) page.views = next;
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    // What it makes, and that it is changed later.
    {
        std::vector<std::string> names;
        for (int i = 0; i < 3; ++i)
            if (page.views[static_cast<std::size_t>(i)]) names.push_back(offered[static_cast<std::size_t>(i)].name);
        const int first = shownFirst(page);
        std::string list;
        for (std::size_t i = 0; i < names.size(); ++i) list += (i ? ", " : "") + names[i];
        const auto summary = names.size() == 1 ? trf("One view: {}, without a switch in the rail.", names.front())
                                               : trf("{} views: {}; {} shown first, Ctrl+1 to Ctrl+{}.", names.size(),
                                                     list, offered[static_cast<std::size_t>(first)].name, names.size());
        ImGui::Dummy(ImVec2(0, px(4)));
        ImGui::TextUnformatted(summary.c_str());
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextDisabled("%s", TR("Everything can be changed later in Settings › Views, or with ⋯ under the switch at "
                                 "the top of the rail."));
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, px(14)));

    // fastchess-gui's profile, to copy.
    if (!page.legacy.empty()) {
        ui::beginCard("first_legacy", ImVec2(width, 0));
        ImGui::TextUnformatted(TR("A fastchess-gui profile is on this machine"));
        const auto& s = page.summary;
        ImGui::TextDisabled("%s",
                            trf("{}, {}, {} and your settings.", trn("{} engine", "{} engines", s.engines, s.engines),
                                trn("{} book", "{} books", s.books, s.books),
                                trn("{} tournament", "{} tournaments", s.tournaments, s.tournaments))
                                .c_str());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - px(32));
        ImGui::TextDisabled("%s", TR("RookStudio can copy it: fastchess-gui stays as it is and can keep running "
                                     "beside."));
        ImGui::PopTextWrapPos();
        ui::endCard();
        ImGui::Dummy(ImVec2(0, px(10)));
    }

    // Start.
    const bool enter = keyboard::enterPressed();
    const char* go   = page.legacy.empty() ? TR("Continue") : TR("Copy and continue");
    const float go_w = ImGui::CalcTextSize(go).x + px(28);
    float row        = go_w;
    if (!page.legacy.empty()) row += ImGui::CalcTextSize(TR("Start without")).x + px(28) + px(10);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + width - row);
    if (!page.legacy.empty()) {
        if (ImGui::Button(TR("Start without"))) {
            page.copy = false;
            chosen    = true;
        }
        ImGui::SameLine(0, px(10));
    }
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    if (ImGui::Button(go) || enter) {
        page.copy = !page.legacy.empty();
        chosen    = true;
    }
    ImGui::PopStyleColor(2);
    ImGui::SameLine(0, px(8));
    ImGui::TextDisabled("%s", TR("Enter"));

    ImGui::EndGroup();
    ImGui::End();
    return chosen;
}

}  // namespace fcg::first
