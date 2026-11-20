#include "views.hpp"

#include <algorithm>
#include <array>

#include <imgui.h>

#include "i18n.hpp"

namespace fcg::views {

namespace {

constexpr std::array<PageId, 14> ALL = {PageId::Home,     PageId::Tournaments, PageId::Board,   PageId::Databases,
                                        PageId::Openings, PageId::Training,    PageId::Lichess, PageId::Play,
                                        PageId::Engines,  PageId::Books,       PageId::Suites,  PageId::Ratings,
                                        PageId::Log,      PageId::Settings};
constexpr std::array<std::string_view, 14> IDS   = {"home",     "tournaments", "board", "databases", "openings",
                                                    "training", "lichess",     "play",  "engines",   "books",
                                                    "suites",   "ratings",     "log",   "settings"};
constexpr std::array<std::string_view, 12> ICONS = {"flask", "cap",   "layers", "trophy", "crown", "swords",
                                                    "book",  "globe", "cpu",    "chart",  "board", "rook"};
constexpr std::array<Colour, 6> COLOURS          = {{
    {IM_COL32(47, 111, 222, 255), IM_COL32(31, 85, 181, 255), IM_COL32(227, 236, 251, 255)},   // blue
    {IM_COL32(26, 127, 75, 255), IM_COL32(20, 107, 62, 255), IM_COL32(224, 241, 231, 255)},    // green
    {IM_COL32(194, 86, 12, 255), IM_COL32(154, 63, 8, 255), IM_COL32(251, 233, 220, 255)},     // orange
    {IM_COL32(109, 63, 214, 255), IM_COL32(85, 48, 176, 255), IM_COL32(236, 230, 251, 255)},   // violet
    {IM_COL32(14, 124, 134, 255), IM_COL32(11, 100, 108, 255), IM_COL32(223, 241, 242, 255)},  // teal
    {IM_COL32(192, 50, 94, 255), IM_COL32(158, 40, 77, 255), IM_COL32(249, 227, 234, 255)},    // pink
}};

}  // namespace

std::span<const PageId> allPages() { return ALL; }

std::string_view idOf(PageId page) { return IDS[static_cast<std::size_t>(page)]; }

std::optional<PageId> pageOf(std::string_view id) {
    for (std::size_t i = 0; i < IDS.size(); ++i) {
        if (IDS[i] == id) return ALL[i];
    }
    return std::nullopt;
}

std::span<const std::string_view> iconNames() { return ICONS; }

std::span<const Colour> palette() { return COLOURS; }

bool View::holds(PageId page) const { return std::find(pages.begin(), pages.end(), page) != pages.end(); }

void tidy(View& view) {
    std::vector<PageId> pages;
    for (const auto p : view.pages) {
        if (std::find(pages.begin(), pages.end(), p) == pages.end()) pages.push_back(p);
    }
    if (std::find(pages.begin(), pages.end(), PageId::Settings) == pages.end()) pages.push_back(PageId::Settings);
    view.pages = std::move(pages);
    if (std::find(ICONS.begin(), ICONS.end(), view.icon) == ICONS.end()) view.icon = ICONS.front();
    view.colour = std::clamp(view.colour, 0, static_cast<int>(COLOURS.size()) - 1);
    if (view.name.empty()) view.name = TR("View");
}

std::vector<View> proposed() {
    using P = PageId;
    std::vector<View> out;
    out.push_back({TR("Tests"),
                   "flask",
                   0,
                   {P::Tournaments, P::Engines, P::Books, P::Board, P::Suites, P::Ratings, P::Log, P::Settings},
                   false});
    // The Training page (#80) between the Openings and Lichess, in Study and Everything.
    out.push_back({TR("Study"),
                   "cap",
                   1,
                   {P::Home, P::Board, P::Databases, P::Openings, P::Training, P::Lichess, P::Engines, P::Settings},
                   true});
    out.push_back({TR("Everything"),
                   "layers",
                   2,
                   {P::Home, P::Tournaments, P::Board, P::Databases, P::Openings, P::Training, P::Lichess, P::Play,
                    P::Engines, P::Books, P::Suites, P::Ratings, P::Log, P::Settings},
                   true});
    return out;
}

std::vector<View> fromSpaces() {
    auto out = proposed();
    out.resize(2);
    return out;
}

nlohmann::json toJson(const std::vector<View>& views) {
    auto out = nlohmann::json::array();
    for (const auto& v : views) {
        auto pages = nlohmann::json::array();
        for (const auto p : v.pages) pages.push_back(std::string(idOf(p)));
        out.push_back({{"name", v.name},
                       {"icon", v.icon},
                       {"colour", v.colour},
                       {"pages", std::move(pages)},
                       {"board", v.study_board ? "study" : "engines"}});
    }
    return out;
}

std::optional<std::vector<View>> fromJson(const nlohmann::json& json) {
    if (!json.is_array()) return std::nullopt;
    std::vector<View> out;
    for (const auto& j : json) {
        if (!j.is_object()) continue;
        View v;
        if (const auto it = j.find("name"); it != j.end() && it->is_string()) v.name = it->get<std::string>();
        if (const auto it = j.find("icon"); it != j.end() && it->is_string()) v.icon = it->get<std::string>();
        if (const auto it = j.find("colour"); it != j.end() && it->is_number_integer()) v.colour = it->get<int>();
        if (const auto it = j.find("pages"); it != j.end() && it->is_array()) {
            for (const auto& p : *it) {
                if (!p.is_string()) continue;
                if (const auto page = pageOf(p.get<std::string>())) v.pages.push_back(*page);
            }
        }
        if (const auto it = j.find("board"); it != j.end() && it->is_string()) v.study_board = *it != "engines";
        tidy(v);
        out.push_back(std::move(v));
        if (static_cast<int>(out.size()) == MAX_VIEWS) break;
    }
    if (out.empty()) return std::nullopt;
    return out;
}

}  // namespace fcg::views
