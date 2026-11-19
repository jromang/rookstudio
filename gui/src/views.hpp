#pragma once

// The views (#43, mockup M6): one to three sets of pages of one's own, switched from the top of the rail (Ctrl+1 to
// Ctrl+3), in place of the two fixed spaces Engines | Chess. A view is the pages it holds, in the order of the rail,
// its name, icon and colour, and the Board it shows: the study's (the notation written in place, the marks, the five
// tabs) or the engines' analysis, as the Engines space had it. Settings are in every view. What a profile keeps
// (settings.json "views", "view"); a profile of before (its "space") is read as two views.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

namespace fcg::views {

// The pages a view can hold, in the order the sheet lists them.
enum class PageId {
    Home,
    Tournaments,
    Board,
    Databases,
    Openings,
    Lichess,
    Play,
    Engines,
    Books,
    Suites,
    Ratings,
    Log,
    Settings
};

[[nodiscard]] std::span<const PageId> allPages();
// As settings.json names them: "home", "tournaments", "board"…
[[nodiscard]] std::string_view idOf(PageId page);
[[nodiscard]] std::optional<PageId> pageOf(std::string_view id);

// The icons a view can take (Lucide's flask-conical, graduation-cap, layers, trophy, crown, swords, book-open, globe,
// cpu, chart-column, chess-knight, chess-rook), by name.
[[nodiscard]] std::span<const std::string_view> iconNames();

// The colours of the views: their full colour, the ink of a label on a light background, and a light tint.
struct Colour {
    unsigned full, ink, tint;  // IM_COL32 values
};
[[nodiscard]] std::span<const Colour> palette();

inline constexpr int MAX_VIEWS = 3;

struct View {
    std::string name;
    std::string icon = "flask";
    int colour       = 0;
    std::vector<PageId> pages;                  // in the rail's order; Settings among them, always
    bool study_board                   = true;  // the study's Board, else the engines' analysis
    bool operator==(const View&) const = default;

    [[nodiscard]] bool holds(PageId page) const;
};

// The three views offered at the first start (mockup M6, board 4): Tests (the Engines space's pages, its Board),
// Study (the Chess space's) and Everything; their names in the language of the interface.
[[nodiscard]] std::vector<View> proposed();

// What a profile of before the views becomes: its two spaces as Tests and Study.
[[nodiscard]] std::vector<View> fromSpaces();

// The views as settings.json keeps them, and back: nullopt when `json` holds none. What it holds is made sound: one to
// three views, the pages it does not know left out and each once, Settings in each, a name, a known icon and colour.
[[nodiscard]] nlohmann::json toJson(const std::vector<View>& views);
[[nodiscard]] std::optional<std::vector<View>> fromJson(const nlohmann::json& json);

// `view` made sound, as fromJson() makes each.
void tidy(View& view);

}  // namespace fcg::views
