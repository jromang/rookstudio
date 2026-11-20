// The views (#43, mockup M6): the switch at the top of the rail, the rail of the view shown, a page shown by a command
// taken to a view that holds it, and the sheet where views are made (Settings › Views, or ⋯ under the switch).

#include <algorithm>
#include <array>

#include <imgui.h>
#include <imgui_internal.h>

#include <fmt/include/fmt/format.h>

#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

// `text` in capitals, for the letters of the interface's languages: ASCII, Latin-1, Latin Extended-A, Greek and
// Cyrillic; any other code point, or a malformed byte, as it is.
std::string capitals(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const auto b    = static_cast<unsigned char>(text[i]);
        std::size_t len = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : 1;
        if (i + len > text.size()) len = 1;
        char32_t cp = b;
        if (len == 2) cp = ((b & 0x1Fu) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3Fu);
        if (len == 3)
            cp = ((b & 0x0Fu) << 12) | ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(text[i + 2]) & 0x3Fu);
        char32_t up = cp;
        if (cp >= U'a' && cp <= U'z')
            up = cp - 0x20;
        else if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7)  // Latin-1
            up = cp - 0x20;
        else if ((cp >= 0x100 && cp <= 0x137) || (cp >= 0x14A && cp <= 0x177))  // Latin Extended-A: upper even
            up = cp & ~char32_t{1};
        else if (((cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E)) && cp % 2 == 0)  // upper odd
            up = cp - 1;
        else if (cp >= 0x3B1 && cp <= 0x3C9 && cp != 0x3C2)  // Greek
            up = cp - 0x20;
        else if (cp >= 0x430 && cp <= 0x44F)  // Cyrillic
            up = cp - 0x20;
        else if (cp >= 0x450 && cp <= 0x45F)
            up = cp - 0x50;
        if (up == cp) {
            out.append(text, i, len);
        } else if (up < 0x80) {
            out += static_cast<char>(up);
        } else if (up < 0x800) {
            out += static_cast<char>(0xC0 | (up >> 6));
            out += static_cast<char>(0x80 | (up & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (up >> 12));
            out += static_cast<char>(0x80 | ((up >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (up & 0x3F));
        }
        i += len;
    }
    return out;
}

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

using views::PageId;

const char* pageName(PageId page) {
    switch (page) {
        case PageId::Home:
            return TR("Home");
        case PageId::Tournaments:
            return TR("Tournaments");
        case PageId::Board:
            return TR("Board");
        case PageId::Databases:
            return TR("Databases");
        case PageId::Openings:
            return TR("Openings");
        case PageId::Training:
            return TR("Training");
        case PageId::Lichess:
            return "Lichess";
        case PageId::Play:
            return TR("Play");
        case PageId::Engines:
            return TR("Engines");
        case PageId::Books:
            return TR("Books");
        case PageId::Suites:
            return TR("Suites");
        case PageId::Ratings:
            return TR("Ratings");
        case PageId::Log:
            return TR("Log");
        case PageId::Settings:
            return TR("Settings");
    }
    return "";
}

ui::Icon pageIcon(PageId page) {
    switch (page) {
        case PageId::Home:
            return ui::Icon::Home;
        case PageId::Tournaments:
            return ui::Icon::Trophy;
        case PageId::Board:
            return ui::Icon::Board;
        case PageId::Databases:
            return ui::Icon::Database;
        case PageId::Openings:
            return ui::Icon::Openings;
        case PageId::Training:
            return ui::Icon::Cap;
        case PageId::Lichess:
            return ui::Icon::Globe;
        case PageId::Play:
            return ui::Icon::Play;
        case PageId::Engines:
            return ui::Icon::Engine;
        case PageId::Books:
            return ui::Icon::Book;
        case PageId::Suites:
            return ui::Icon::Suite;
        case PageId::Ratings:
            return ui::Icon::Chart;
        case PageId::Log:
            return ui::Icon::Log;
        case PageId::Settings:
            return ui::Icon::Settings;
    }
    return ui::Icon::Board;
}

// A view's token: its icon in its colour, filled when it is the view shown.
void drawToken(ImDrawList* dl, ImVec2 at, float side, const views::View& v, bool shown, bool hovered) {
    const auto& c = views::palette()[static_cast<std::size_t>(v.colour)];
    dl->AddRectFilled(at, ImVec2(at.x + side, at.y + side), shown ? c.full : (hovered ? c.tint : c.tint), side * 0.28f);
    if (hovered && !shown) dl->AddRect(at, ImVec2(at.x + side, at.y + side), c.full, side * 0.28f);
    ui::drawIcon(dl, ui::viewIcon(v.icon), ImVec2(at.x + side * 0.5f, at.y + side * 0.5f), side * 0.56f,
                 shown ? IM_COL32_WHITE : c.full);
}

}  // namespace

std::optional<PageId> App::rootOf(Page page) {
    switch (page) {
        case Page::Home:
        case Page::NewMatch:
        case Page::Tournament:
            return PageId::Tournaments;
        case Page::ChessHome:
            return PageId::Home;
        case Page::Board:
            return PageId::Board;
        case Page::Databases:
            return PageId::Databases;
        case Page::Openings:
            return PageId::Openings;
        case Page::Training:
            return PageId::Training;
        case Page::Lichess:
            return PageId::Lichess;
        case Page::Engines:
            return PageId::Engines;
        case Page::Books:
            return PageId::Books;
        case Page::Suites:
            return PageId::Suites;
        case Page::Ratings:
            return PageId::Ratings;
        case Page::Log:
            return PageId::Log;
        case Page::Settings:
            return PageId::Settings;
        case Page::Count:
            break;
    }
    return std::nullopt;
}

App::Page App::pageFor(PageId id) {
    switch (id) {
        case PageId::Home:
            return Page::ChessHome;
        case PageId::Tournaments:
            return Page::Home;
        case PageId::Board:
        case PageId::Play:
            return Page::Board;
        case PageId::Databases:
            return Page::Databases;
        case PageId::Openings:
            return Page::Openings;
        case PageId::Training:
            return Page::Training;
        case PageId::Lichess:
            return Page::Lichess;
        case PageId::Engines:
            return Page::Engines;
        case PageId::Books:
            return Page::Books;
        case PageId::Suites:
            return Page::Suites;
        case PageId::Ratings:
            return Page::Ratings;
        case PageId::Log:
            return Page::Log;
        case PageId::Settings:
            return Page::Settings;
    }
    return Page::Settings;
}

bool App::viewHolds(Page page) const {
    if (views_.empty()) return true;
    const auto root = rootOf(page);
    if (!root) return true;
    const auto& v = view();
    return v.holds(*root) || (*root == PageId::Board && v.holds(PageId::Play));
}

void App::saveViews() { storage_.setViews(views::toJson(views_), view_); }

void App::switchView(int index) {
    if (index < 0 || index >= static_cast<int>(views_.size()) || index == view_) return;
    view_pages_.resize(views_.size(), Page::Settings);
    view_pages_[static_cast<std::size_t>(view_)] = page_;
    view_                                        = index;
    page_                                        = view_pages_[static_cast<std::size_t>(view_)];
    if (!viewHolds(page_)) page_ = pageFor(view().pages.front());
    if (page_ == Page::Home) tournaments_dirty_ = true;
    saveViews();
}

void App::followPage() {
    if (views_.empty() || viewHolds(page_)) return;
    // A page shown by a command or a notice: the first view that holds it; none, and it stays in the view shown,
    // the time to look at it (drawPageInNoView).
    for (int i = 0; i < static_cast<int>(views_.size()); ++i) {
        const auto root = rootOf(page_);
        const auto& v   = views_[static_cast<std::size_t>(i)];
        if (root && (v.holds(*root) || (*root == PageId::Board && v.holds(PageId::Play)))) {
            const auto page = page_;
            view_pages_.resize(views_.size(), Page::Settings);
            view_pages_[static_cast<std::size_t>(view_)] = page_;
            view_                                        = i;
            page_                                        = page;
            saveViews();
            return;
        }
    }
}

void App::toStudyBoard() {
    if (!chessBoard()) {
        for (int i = 0; i < static_cast<int>(views_.size()); ++i) {
            const auto& v = views_[static_cast<std::size_t>(i)];
            if (v.study_board && (v.holds(PageId::Board) || v.holds(PageId::Play))) {
                switchView(i);
                break;
            }
        }
    }
    page_ = Page::Board;
}

void App::drawViewSwitch(float width) {
    if (views_.size() <= 1) return;
    auto* dl          = ImGui::GetWindowDrawList();
    const float token = px(34), gap = px(6);
    const auto top    = ImGui::GetCursorScreenPos();
    const float box_w = token + px(12);
    const float box_x = top.x + (width - box_w) * 0.5f;
    const float label = ImGui::GetFontSize() * 0.8f;
    const float box_h = static_cast<float>(views_.size()) * (token + gap) + label + px(6);
    dl->AddRectFilled(ImVec2(box_x, top.y), ImVec2(box_x + box_w, top.y + box_h), ImGui::GetColorU32(ImGuiCol_FrameBg),
                      px(12));
    for (int i = 0; i < static_cast<int>(views_.size()); ++i) {
        const auto& v = views_[static_cast<std::size_t>(i)];
        const ImVec2 at(box_x + px(6), top.y + px(6) + static_cast<float>(i) * (token + gap));
        ImGui::SetCursorScreenPos(at);
        ImGui::PushID(i);
        const bool pressed = ImGui::InvisibleButton("##view", ImVec2(token, token));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) openViewsSheet(i);
        ImGui::PopID();
        drawToken(dl, at, token, v, i == view_, hovered);
        if (hovered) {
            ImGui::SetTooltip("%s", trf("{} · Ctrl+{} · right click: edit the views", v.name, i + 1).c_str());
        }
        if (pressed) switchView(i);
    }
    // The name of the view shown, in small capitals when they fit (capitals run wide: "ИЗУЧЕНИЕ"), else as written.
    {
        ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 0.62f);
        auto name = capitals(view().name);
        if (ImGui::CalcTextSize(name.c_str()).x > box_w - px(4)) name = view().name;
        const auto ts = ImGui::CalcTextSize(name.c_str());
        const float y = top.y + px(6) + static_cast<float>(views_.size()) * (token + gap) - px(2);
        const auto& c = views::palette()[static_cast<std::size_t>(view().colour)];
        ImGui::PushStyleColor(ImGuiCol_Text, ui::currentTheme() == ui::Theme::Dark ? c.tint : c.ink);
        ImGui::RenderTextEllipsis(dl, ImVec2(std::max(box_x + px(2), top.x + (width - ts.x) * 0.5f), y),
                                  ImVec2(box_x + box_w - px(2), y + ts.y), box_x + box_w - px(2), name.c_str(), nullptr,
                                  nullptr);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    // ⋯: the sheet of the views.
    ImGui::SetCursorScreenPos(ImVec2(top.x + (width - px(30)) * 0.5f, top.y + box_h + px(4)));
    if (ImGui::InvisibleButton("##views_more", ImVec2(px(30), px(18)))) openViewsSheet(view_);
    const bool more_hovered = ImGui::IsItemHovered();
    ui::drawIcon(dl, ui::Icon::More, ImVec2(top.x + width * 0.5f, top.y + box_h + px(13)), px(16),
                 more_hovered ? ImGui::GetColorU32(ImGuiCol_Text) : ui::MUTED);
    if (more_hovered) ImGui::SetTooltip("%s", TR("Edit the views…"));
    ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + box_h + px(26)));
    dl->AddLine(ImVec2(top.x + width * 0.22f, top.y + box_h + px(26)),
                ImVec2(top.x + width * 0.78f, top.y + box_h + px(26)), ImGui::GetColorU32(ImGuiCol_Border));
    ImGui::Dummy(ImVec2(width, px(10)));
}

void App::drawViewRail(float width, float button) {
    for (const auto id : view().pages) {
        const auto key = fmt::format("rail_{}", views::idOf(id));
        if (id == PageId::Board) {
            drawBoardRailButton(width, button);
            continue;
        }
        bool selected = false;
        if (id == PageId::Tournaments)
            selected = page_ == Page::Home || page_ == Page::NewMatch || page_ == Page::Tournament;
        else if (id != PageId::Play)
            selected = page_ == pageFor(id);
        if (ui::railButton(key.c_str(), pageIcon(id), pageName(id), selected, width, button)) {
            page_ = pageFor(id);
            if (id == PageId::Tournaments) tournaments_dirty_ = true;
            if (id == PageId::Ratings) game_db_dirty_ = true;
            if (id == PageId::Play) board_.play_open = true;
        }
        if (id == PageId::Tournaments) {
            // The games being played, at the icon's top right.
            if (const int playing = gamesPlaying(true); playing > 0) {
                const auto min   = ImGui::GetItemRectMin();
                const float font = ImGui::GetFontSize();
                const auto count = std::to_string(playing);
                ui::drawBadge(ImGui::GetWindowDrawList(),
                              ImVec2(min.x + width * 0.5f + font * 0.75f, min.y + button * 0.36f - font * 0.5f),
                              count.c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s",
                                      trn("{} game being played", "{} games being played", playing, playing).c_str());
                }
            }
        }
    }
}

void App::drawPageInNoView() {
    if (views_.empty() || viewHolds(page_)) return;
    const auto root = rootOf(page_);
    if (!root) return;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(ui::CARD_BG));
    ImGui::BeginChild("##no_view", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", trf("{} is in no view: shown here while you look at it.", pageName(*root)).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(trf("Add it to {}", view().name).c_str())) {
        auto& pages = views_[static_cast<std::size_t>(view_)].pages;
        pages.insert(pages.end() - 1, *root);  // before Settings
        views::tidy(views_[static_cast<std::size_t>(view_)]);
        saveViews();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void App::openViewsSheet(int selected) {
    auto& s    = views_sheet_;
    s.open     = true;
    s.selected = std::clamp(selected, 0, static_cast<int>(views_.size()) - 1);
    s.before   = views_;
    s.undo.clear();
}

void App::drawViewsSheet() {
    auto& s    = views_sheet_;
    s.selected = std::clamp(s.selected, 0, static_cast<int>(views_.size()) - 1);
    // A change: what was before kept for Ctrl+Z, kept in the profile at once (the rail shows it).
    const auto change = [&](const std::function<void(std::vector<views::View>&)>& apply) {
        s.undo.push_back(views_);
        apply(views_);
        for (auto& v : views_) views::tidy(v);
        view_ = std::clamp(view_, 0, static_cast<int>(views_.size()) - 1);
        view_pages_.resize(views_.size(), Page::Settings);
        if (!viewHolds(page_)) page_ = Page::Settings;
        saveViews();
    };
    const bool typing = ImGui::GetIO().WantTextInput;
    if (!typing && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z) && !s.undo.empty()) {
        views_ = s.undo.back();
        s.undo.pop_back();
        view_ = std::clamp(view_, 0, static_cast<int>(views_.size()) - 1);
        saveViews();
    }
    bool close = false;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive()) {
        // Esc: the views as they were.
        views_ = s.before;
        view_  = std::clamp(view_, 0, static_cast<int>(views_.size()) - 1);
        saveViews();
        close = true;
    }

    ui::pageTitle(TR("Edit the views"));
    ImGui::TextDisabled("%s", TR("Settings › Views, or ⋯ under the switch · everything shows in the rail at once · "
                                 "Enter keeps, Esc leaves them as they were"));
    ImGui::Spacing();

    const float avail  = ImGui::GetContentRegionAvail().x;
    const float list_w = px(230), rail_w = px(150), gap = px(18);
    const float form_w = std::max(px(320), avail - list_w - rail_w - 2 * gap);

    // The views, their order, a new one.
    ImGui::BeginChild("##views_list", ImVec2(list_w, 0));
    ImGui::PushFont(ui::titleFont(), 0.0f);
    ImGui::TextUnformatted(TR("Your views"));
    ImGui::PopFont();
    for (int i = 0; i < static_cast<int>(views_.size()); ++i) {
        const auto& v = views_[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        const auto at = ImGui::GetCursorScreenPos();
        const float h = px(46);
        if (ImGui::Selectable("##row", s.selected == i, ImGuiSelectableFlags_None, ImVec2(list_w - px(8), h)))
            s.selected = i;
        auto* dl = ImGui::GetWindowDrawList();
        drawToken(dl, ImVec2(at.x + px(6), at.y + (h - px(30)) * 0.5f), px(30), v, true, false);
        dl->AddText(ImVec2(at.x + px(46), at.y + px(6)), ImGui::GetColorU32(ImGuiCol_Text), v.name.c_str());
        const auto sub = trf("{} · Ctrl+{}", trn("{} page", "{} pages", v.pages.size(), v.pages.size()), i + 1);
        dl->AddText(ImVec2(at.x + px(46), at.y + px(6) + ImGui::GetTextLineHeight()), ui::MUTED, sub.c_str());
        ImGui::PopID();
    }
    // The selected one up or down: Ctrl+1, Ctrl+2, Ctrl+3 follow the list.
    if (views_.size() > 1) {
        ImGui::BeginDisabled(s.selected == 0);
        if (ImGui::SmallButton(TR("Up##view"))) {
            const int from = s.selected;
            change([&](auto& list) {
                std::swap(list[static_cast<std::size_t>(from)], list[static_cast<std::size_t>(from - 1)]);
            });
            if (view_ == from)
                view_ = from - 1;
            else if (view_ == from - 1)
                view_ = from;
            s.selected = from - 1;
            saveViews();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(s.selected + 1 >= static_cast<int>(views_.size()));
        if (ImGui::SmallButton(TR("Down##view"))) {
            const int from = s.selected;
            change([&](auto& list) {
                std::swap(list[static_cast<std::size_t>(from)], list[static_cast<std::size_t>(from + 1)]);
            });
            if (view_ == from)
                view_ = from + 1;
            else if (view_ == from + 1)
                view_ = from;
            s.selected = from + 1;
            saveViews();
        }
        ImGui::EndDisabled();
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(static_cast<int>(views_.size()) >= views::MAX_VIEWS);
    if (ImGui::Button(TR("New view"))) {
        change([&](auto& list) {
            views::View v;
            v.name   = TR("New view");
            v.icon   = "crown";
            v.colour = static_cast<int>(list.size()) % static_cast<int>(views::palette().size());
            v.pages  = {PageId::Board, PageId::Engines, PageId::Settings};
            list.push_back(std::move(v));
        });
        s.selected = static_cast<int>(views_.size()) - 1;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("three at most"));
    ImGui::EndChild();

    ImGui::SameLine(0, gap);
    ImGui::BeginChild("##views_form", ImVec2(form_w, 0));
    auto& v = views_[static_cast<std::size_t>(s.selected)];
    // Its name.
    ui::label(TR("Name"), px(90));
    {
        char buffer[48];
        const auto n = std::min(v.name.size(), sizeof(buffer) - 1);
        std::copy_n(v.name.data(), n, buffer);
        buffer[n] = 0;
        ImGui::SetNextItemWidth(px(220));
        if (ImGui::InputText("##view_name", buffer, sizeof(buffer))) {
            const std::string name = buffer;
            const int at           = s.selected;
            if (!name.empty()) change([&](auto& list) { list[static_cast<std::size_t>(at)].name = name; });
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("short: it is written under the switch"));
    }
    // Its icon and colour.
    ui::label(TR("Icon"), px(90));
    {
        const auto& c = views::palette()[static_cast<std::size_t>(v.colour)];
        int k         = 0;
        for (const auto name : views::iconNames()) {
            if (k++ > 0) ImGui::SameLine(0, px(4));
            ImGui::PushID(k);
            const auto at      = ImGui::GetCursorScreenPos();
            const bool pressed = ImGui::InvisibleButton("##icon", ImVec2(px(30), px(30)));
            const bool hovered = ImGui::IsItemHovered();
            const bool chosen  = v.icon == name;
            auto* dl           = ImGui::GetWindowDrawList();
            dl->AddRectFilled(at, ImVec2(at.x + px(30), at.y + px(30)), chosen ? c.full : c.tint, px(8));
            if (hovered && !chosen) dl->AddRect(at, ImVec2(at.x + px(30), at.y + px(30)), c.full, px(8));
            ui::drawIcon(dl, ui::viewIcon(name), ImVec2(at.x + px(15), at.y + px(15)), px(17),
                         chosen ? IM_COL32_WHITE : c.full);
            ImGui::PopID();
            if (pressed && !chosen) {
                const int at_view = s.selected;
                const std::string icon(name);
                change([&](auto& list) { list[static_cast<std::size_t>(at_view)].icon = icon; });
            }
        }
    }
    ui::label(TR("Colour"), px(90));
    {
        const char* names[] = {TR("Blue"), TR("Green"), TR("Orange"), TR("Violet"), TR("Teal"), TR("Pink")};
        for (int i = 0; i < static_cast<int>(views::palette().size()); ++i) {
            if (i > 0) ImGui::SameLine(0, px(6));
            ImGui::PushID(100 + i);
            const auto at      = ImGui::GetCursorScreenPos();
            const bool pressed = ImGui::InvisibleButton("##colour", ImVec2(px(26), px(26)));
            auto* dl           = ImGui::GetWindowDrawList();
            const auto& c      = views::palette()[static_cast<std::size_t>(i)];
            dl->AddCircleFilled(ImVec2(at.x + px(13), at.y + px(13)), px(10), c.full, 20);
            if (v.colour == i) dl->AddCircle(ImVec2(at.x + px(13), at.y + px(13)), px(12.5f), c.full, 20, px(2));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", names[i]);
            ImGui::PopID();
            if (pressed && v.colour != i) {
                const int at_view = s.selected;
                change([&](auto& list) { list[static_cast<std::size_t>(at_view)].colour = i; });
            }
        }
    }
    // Its Board: the study's, or the engines' analysis (M6, board 3, track B).
    ui::label(TR("Board"), px(90));
    if (ImGui::RadioButton(TR("For study"), v.study_board) && !v.study_board) {
        const int at = s.selected;
        change([&](auto& list) { list[static_cast<std::size_t>(at)].study_board = true; });
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(TR("For the engines' analysis"), !v.study_board) && v.study_board) {
        const int at = s.selected;
        change([&](auto& list) { list[static_cast<std::size_t>(at)].study_board = false; });
    }
    ui::help(
        TR("For study: the notation written in place, the arrows and squares, the Tree, References and Questions "
           "tabs. For the engines' analysis: the Board of the engine testers, as it was."));

    // Its pages: those it holds, in the rail's order, then the others.
    ImGui::Spacing();
    ImGui::PushFont(ui::titleFont(), 0.0f);
    ImGui::TextUnformatted(TR("Pages"));
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("ticked in the rail's order · a page can be in several views"));
    const auto row = [&](PageId page, bool on, int index) {
        ImGui::PushID(static_cast<int>(page));
        bool tick         = on;
        const bool locked = page == PageId::Settings;
        ImGui::BeginDisabled(locked);
        if (ImGui::Checkbox("##on", &tick)) {
            const int at = s.selected;
            change([&](auto& list) {
                auto& pages = list[static_cast<std::size_t>(at)].pages;
                if (tick)
                    pages.insert(pages.end() - 1, page);  // before Settings
                else
                    std::erase(pages, page);
            });
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ui::inlineIcon(pageIcon(page), on ? ImGui::GetColorU32(ImGuiCol_Text) : ui::MUTED);
        ImGui::SameLine();
        ImGui::TextUnformatted(pageName(page));
        // Where else it is.
        std::string also;
        if (locked) {
            also = TR("in every view");
        } else {
            for (int i = 0; i < static_cast<int>(views_.size()); ++i) {
                if (i == s.selected || !views_[static_cast<std::size_t>(i)].holds(page)) continue;
                also += (also.empty() ? std::string() : std::string(", ")) + views_[static_cast<std::size_t>(i)].name;
            }
            if (!also.empty()) also = trf("also: {}", also);
        }
        if (!also.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", also.c_str());
        }
        // Up and down in the rail.
        if (on && !locked) {
            const auto& pages = v.pages;
            ImGui::SameLine(form_w - px(70));
            ImGui::BeginDisabled(index == 0);
            if (ImGui::SmallButton("↑")) {
                const int at = s.selected;
                change([&](auto& list) {
                    auto& p = list[static_cast<std::size_t>(at)].pages;
                    std::swap(p[static_cast<std::size_t>(index)], p[static_cast<std::size_t>(index - 1)]);
                });
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(index + 2 >= static_cast<int>(pages.size()));  // Settings stays last
            if (ImGui::SmallButton("↓")) {
                const int at = s.selected;
                change([&](auto& list) {
                    auto& p = list[static_cast<std::size_t>(at)].pages;
                    std::swap(p[static_cast<std::size_t>(index)], p[static_cast<std::size_t>(index + 1)]);
                });
            }
            ImGui::EndDisabled();
        }
        ImGui::PopID();
    };
    const auto pages = v.pages;  // a copy: a click changes the view
    for (int i = 0; i < static_cast<int>(pages.size()); ++i) row(pages[static_cast<std::size_t>(i)], true, i);
    for (const auto page : views::allPages()) {
        if (std::find(pages.begin(), pages.end(), page) == pages.end()) row(page, false, -1);
    }

    // Done, the views offered again, this one deleted.
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    if (ImGui::Button(TR("Done")) || (!typing && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) close = true;
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("Enter"));
    ImGui::SameLine(0, px(18));
    if (ImGui::Button(TR("Restore the views offered"))) {
        change([&](auto& list) { list = views::proposed(); });
        s.selected = 0;
    }
    if (views_.size() > 1) {
        ImGui::SameLine();
        if (ImGui::Button(TR("Delete this view"))) {
            const int at = s.selected;
            change([&](auto& list) { list.erase(list.begin() + at); });
            if (view_ >= at && view_ > 0) --view_;
            view_pages_.erase(view_pages_.begin() + std::min<std::ptrdiff_t>(at, view_pages_.size() - 1));
            s.selected = std::max(0, at - 1);
            saveViews();
        }
    }
    ImGui::TextDisabled("%s", TR("Ctrl+Z undoes each change while this page is open."));
    ImGui::EndChild();

    // The rail as it will be.
    ImGui::SameLine(0, gap);
    ImGui::BeginChild("##views_rail", ImVec2(rail_w, 0), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("%s", TR("The rail"));
    {
        const auto& sel = views_[static_cast<std::size_t>(s.selected)];
        auto* dl        = ImGui::GetWindowDrawList();
        for (int i = 0; i < static_cast<int>(views_.size()); ++i) {
            const auto at = ImGui::GetCursorScreenPos();
            drawToken(dl, ImVec2(at.x + (rail_w - px(40)) * 0.5f, at.y), px(28), views_[static_cast<std::size_t>(i)],
                      i == s.selected, false);
            ImGui::Dummy(ImVec2(0, px(32)));
        }
        ImGui::Separator();
        for (const auto page : sel.pages) {
            ui::inlineIcon(pageIcon(page), ImGui::GetColorU32(ImGuiCol_Text));
            ImGui::SameLine();
            ImGui::TextUnformatted(pageName(page));
        }
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", sel.pages.size() > 10
                                      ? TR("More than ten pages: the rail tightens.")
                                      : trn("{} page: each a click away.", "{} pages: each a click away.",
                                            sel.pages.size(), sel.pages.size())
                                            .c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();

    if (close) s.open = false;
}

}  // namespace fcg
