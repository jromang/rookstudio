// The Chess space's Board page: the game's headers, unfolded in place above the notation (mockup M2, board 7).

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include <commentary/openings.hpp>
#include <commentary/position.hpp>
#include <tour/util.hpp>

#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// The seven tags every game has, and those the form shows beside them.
constexpr const char* kShown[] = {"White", "WhiteElo", "Black", "BlackElo", "Event",
                                  "Site",  "Date",     "Round", "ECO",      "Annotator"};

bool shownTag(const std::string& key) {
    return key == "Result" || key == "SetUp" || key == "FEN" ||
           std::find_if(std::begin(kShown), std::end(kShown), [&](const char* k) { return key == k; }) !=
               std::end(kShown);
}

// A PGN date: YYYY.MM.DD, each part a number in its range or question marks.
bool validDate(std::string_view date) {
    if (date.empty()) return true;
    if (date.size() != 10 || date[4] != '.' || date[7] != '.') return false;
    const auto part = [&](std::size_t at, std::size_t n, int low, int high) {
        const auto s = date.substr(at, n);
        if (s.find_first_not_of('?') == std::string_view::npos) return true;
        if (s.find_first_not_of("0123456789") != std::string_view::npos) return false;
        const int v = std::stoi(std::string(s));
        return v >= low && v <= high;
    };
    return part(0, 4, 0, 9999) && part(5, 2, 1, 12) && part(8, 2, 1, 31);
}

}  // namespace

std::optional<commentary::OpeningCode> App::boardOpening() const {
    const auto& main = board_.tree.main;
    auto pos         = commentary::Position::initial();
    // From the usual start only: a FEN tag of it (some programs write one) counts as none.
    if (board_.chess960) return std::nullopt;
    if (!main.start_fen.empty()) {
        const auto start = commentary::Position::fromFen(main.start_fen);
        if (!start || start->fen() != pos.fen()) return std::nullopt;
    }
    std::vector<commentary::Move> moves;
    for (const auto& m : main.moves) {
        const auto move = pos.parseUci(chess::uci::moveToUci(m.move, false));
        if (!move) break;
        moves.push_back(*move);
        pos.playUnchecked(*move);
    }
    return commentary::openingCode(moves);
}

std::vector<std::string> App::nameSuggestions(std::string_view typed) {
    if (typed.size() < 2) return {};
    if (typed == suggest_typed_) return suggest_names_;
    suggest_typed_ = std::string(typed);
    suggest_names_.clear();
    // The names of the game's database, else of the one games were last saved into, else of the first.
    auto& s = shelf();
    std::optional<std::size_t> base;
    for (std::size_t i = 0; i < s.entries().size() && !base; ++i)
        if (board_origin_ && !board_origin_->linked && s.entries()[i].folder == board_origin_->folder) base = i;
    for (std::size_t i = 0; i < s.entries().size() && !base; ++i)
        if (!board_.settings.save_database.empty() &&
            s.entries()[i].folder == std::filesystem::path(board_.settings.save_database))
            base = i;
    for (std::size_t i = 0; i < s.entries().size() && !base; ++i)
        if (!s.entries()[i].source) base = i;
    if (!base) return {};
    const auto db = s.open(*base);
    if (!db) return {};
    const auto snapshot = db->snapshot();
    const auto start    = lower(typed);
    std::vector<std::string> found;
    for (uint32_t id = 0; id < snapshot->names() && found.size() < 200; ++id) {
        const auto name = snapshot->name(id);
        if (name.size() < start.size() || lower(name.substr(0, start.size())) != start) continue;
        found.emplace_back(name);
    }
    std::sort(found.begin(), found.end(),
              [](const auto& a, const auto& b) { return a.size() != b.size() ? a.size() < b.size() : a < b; });
    found.erase(std::unique(found.begin(), found.end()), found.end());
    if (found.size() > 6) found.resize(6);
    suggest_names_ = found;
    return found;
}

void App::setGameHeader(const std::string& key, const std::string& value) {
    auto& b             = board_;
    auto& main          = b.tree.main;
    std::string trimmed = value;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(0, 1);
    const bool result = key == "Result";
    if ((result ? main.result : main.header(key)) == trimmed) return;
    auto before = boardSnapshot();
    if (result) main.result = trimmed;
    auto it = std::find_if(main.headers.begin(), main.headers.end(), [&](const auto& h) { return h.first == key; });
    if (trimmed.empty() && !result) {
        if (it != main.headers.end()) main.headers.erase(it);
    } else if (it != main.headers.end()) {
        it->second = trimmed;
    } else {
        main.headers.emplace_back(key, trimmed);
    }
    b.game = lineOf(b.tree, b.line);
    // The game's title follows its players and its event.
    if (key == "White" || key == "Black" || key == "Event" || key == "Round") {
        const auto w = main.header("White"), bl = main.header("Black");
        auto title = fmt::format("{} – {}", w.empty() ? "?" : w, bl.empty() ? "?" : bl);
        if (const auto event = main.header("Event"); !event.empty() && event != "?") title += " · " + event;
        b.title = title;
    }
    boardChanged(std::move(before));
}

void App::drawGameHeaders() {
    auto& b          = board_;
    const auto& main = b.tree.main;
    const auto& io   = ImGui::GetIO();
    // Alt+H is a hint while a game against an engine lasts (#83).
    if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_H) && !engineGameRunning()) headers_open_ = !headers_open_;

    // The title line: a click unfolds the headers, or folds them.
    const auto value = [&](const char* key) {
        const auto v = main.header(key);
        return v == "?" ? std::string() : v;
    };
    std::string players = fmt::format("{}{} – {}{}", value("White").empty() ? "?" : value("White"),
                                      value("WhiteElo").empty() ? "" : " " + value("WhiteElo"),
                                      value("Black").empty() ? "?" : value("Black"),
                                      value("BlackElo").empty() ? "" : " " + value("BlackElo"));
    std::string where;
    const auto add = [&](const std::string& part) {
        if (!part.empty()) where += where.empty() ? part : " · " + part;
    };
    add(value("Event") + (value("Round").empty() || value("Round") == "-" ? "" : " (" + value("Round") + ")"));
    add(value("Date"));
    if (!main.result.empty() && main.result != "*") add(main.result);
    const auto label   = fmt::format("{}  {}###game_title", players, where);
    const auto edit    = std::string("✎ ") + TR("Headers");
    const float edit_w = ImGui::CalcTextSize(edit.c_str()).x + ImGui::CalcTextSize("Alt+H").x + px(16);
    const float avail  = ImGui::GetContentRegionAvail().x;
    if (ImGui::Selectable(label.c_str(), headers_open_, 0, ImVec2(std::max(px(80), avail - edit_w), 0)))
        headers_open_ = !headers_open_;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("The game's headers: a click unfolds them · Alt+H"));
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", edit.c_str());
    ImGui::SameLine(0, px(4));
    ImGui::TextDisabled("Alt+H");
    if (!headers_open_) return;

    // The form: each field written in place, a change once it is left (Tab, Enter, a click elsewhere).
    ui::beginCard("game_headers");
    bool fold = false;
    // The labels' column, as wide as the widest of them.
    float lw = px(60);
    for (const char* l :
         {TR("White"), TR("Black"), TR("Event"), TR("Site"), TR("Date"), TR("Result"), TR("ECO"), TR("Annotator")})
        lw = std::max(lw, ImGui::CalcTextSize(l).x);
    for (const auto& [key, v] : main.headers)
        if (!shownTag(key)) lw = std::max(lw, ImGui::CalcTextSize(key.c_str()).x);
    lw += ImGui::GetCursorPosX() + px(10);  // SameLine() counts from the window's edge, its padding included
    std::string under;                      // the names a field could take, said under its row
    const auto field = [&](const char* text, const std::string& key, float width, bool names) {
        auto& buffer = header_edit_[key];
        if (header_active_ != key)
            std::snprintf(buffer.data(), buffer.size(), "%s",
                          (key == "Result" ? main.result : main.header(key)).c_str());
        if (text) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(text);
            ImGui::SameLine(lw);
        }
        ImGui::SetNextItemWidth(width);
        const auto id = "##h_" + key;
        // Tab completes a name from the database's, a second Tab the next one.
        struct Completion {
            App* app;
            std::string key;
        } completion{this, key};
        const auto complete = [](ImGuiInputTextCallbackData* data) {
            auto* c          = static_cast<Completion*>(data->UserData);
            const auto names = c->app->nameSuggestions(c->app->suggest_base_);
            if (names.empty()) return 0;
            const std::string now(data->Buf, static_cast<std::size_t>(data->BufTextLen));
            auto at          = std::find(names.begin(), names.end(), now);
            const auto& next = at == names.end() || ++at == names.end() ? names.front() : *at;
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, next.c_str());
            return 0;
        };
        const ImGuiInputTextFlags flags =
            ImGuiInputTextFlags_EnterReturnsTrue | (names ? ImGuiInputTextFlags_CallbackCompletion : 0);
        const bool enter = ImGui::InputText(id.c_str(), buffer.data(), buffer.size(), flags,
                                            names ? +complete : nullptr, names ? &completion : nullptr);
        if (ImGui::IsItemActivated()) {
            header_active_ = key;
            suggest_base_  = buffer.data();
        }
        const bool active = ImGui::IsItemActive();
        if (active && names && ImGui::IsItemEdited()) suggest_base_ = buffer.data();
        if (enter || ImGui::IsItemDeactivatedAfterEdit()) {
            setGameHeader(key, buffer.data());
            if (enter) fold = true;
        }
        if (ImGui::IsItemDeactivated() && header_active_ == key) header_active_.clear();
        // The names it could be, said under its row while it is written.
        if (active && names) {
            const auto found = nameSuggestions(suggest_base_);
            if (!found.empty()) {
                under = TR("Tab:");
                for (std::size_t i = 0; i < found.size() && i < 4; ++i) under += (i ? " · " : " ") + found[i];
            }
        }
    };
    const auto row_end = [&] {
        if (under.empty()) return;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + lw);
        ImGui::TextDisabled("%s", under.c_str());
        under.clear();
    };
    const float wide = std::max(px(120), ImGui::GetContentRegionAvail().x - lw - px(110));
    field(TR("White"), "White", wide, true);
    ImGui::SameLine(0, px(8));
    ImGui::TextDisabled("Elo");
    ImGui::SameLine(0, px(4));
    field(nullptr, "WhiteElo", px(60), false);
    row_end();
    field(TR("Black"), "Black", wide, true);
    ImGui::SameLine(0, px(8));
    ImGui::TextDisabled("Elo");
    ImGui::SameLine(0, px(4));
    field(nullptr, "BlackElo", px(60), false);
    row_end();
    field(TR("Event"), "Event", wide, true);
    row_end();
    field(TR("Site"), "Site", wide, true);
    row_end();
    // A date that is not one is kept, and said.
    field(TR("Date"), "Date", px(110), false);
    ImGui::SameLine(0, px(12));
    ImGui::TextDisabled("%s", TR("Round"));
    ImGui::SameLine(0, px(4));
    field(nullptr, "Round", px(60), false);
    if (const auto date = header_active_ == "Date" ? std::string(header_edit_["Date"].data()) : main.header("Date");
        !validDate(date) && date != "?")
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(ui::BAD), "%s",
            trf("“{}” is not a date: YYYY.MM.DD, with ?? for what is not known (2024.??.?\?)", date).c_str());
    // The result: one click.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Result"));
    ImGui::SameLine(lw);
    static constexpr const char* results[] = {"1-0", "1/2-1/2", "0-1", "*"};
    static constexpr const char* shown[]   = {"1-0", "½-½", "0-1", "*"};
    for (int i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine(0, px(2));
        const bool on = main.result == results[i] || (i == 1 && main.result == "½-½") ||
                        (i == 3 && (main.result.empty() || main.result == "*"));
        if (ImGui::RadioButton(shown[i], on) && !on) setGameHeader("Result", results[i]);
    }
    // The ECO code, and the opening its moves reach.
    field(TR("ECO"), "ECO", px(60), false);
    if (const auto opening = boardOpening()) {
        ImGui::SameLine(0, px(8));
        if (main.header("ECO") != opening->eco) {
            if (ImGui::SmallButton(trf("From the moves: {} · {}", opening->eco, opening->name).c_str()))
                setGameHeader("ECO", opening->eco);
        } else {
            ImGui::TextDisabled("%s", opening->name.c_str());
        }
    }
    field(TR("Annotator"), "Annotator", wide, true);
    row_end();

    // The other headers, as the game has them; one more by its name.
    std::vector<std::string> others;
    for (const auto& [key, v] : main.headers)
        if (!shownTag(key)) others.push_back(key);
    if (!others.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", TR("The other headers, as the game has them:"));
        for (const auto& key : others) field(key.c_str(), key, wide, false);
    }
    ImGui::SetNextItemWidth(px(110));
    ImGui::InputTextWithHint("##h_new_key", TR("A header"), header_new_key_.data(), header_new_key_.size(),
                             ImGuiInputTextFlags_CharsNoBlank);
    ImGui::SameLine(0, px(4));
    ImGui::SetNextItemWidth(px(160));
    const bool added = ImGui::InputTextWithHint("##h_new_value", TR("its value"), header_new_value_.data(),
                                                header_new_value_.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine(0, px(4));
    if ((ImGui::SmallButton(TR("Add")) || added) && header_new_key_[0] && header_new_value_[0]) {
        setGameHeader(header_new_key_.data(), header_new_value_.data());
        header_new_key_.fill(0);
        header_new_value_.fill(0);
    }
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled(
        "%s", TR("Tab: the next field (or completes a name from the database) · Enter: fold · Esc: the field as "
                 "it was · each field changed is a change (Ctrl+Z), Ctrl+S saves the game"));
    ImGui::PopTextWrapPos();
    ui::endCard();
    if (fold || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !io.WantTextInput)) headers_open_ = false;
}

}  // namespace fcg
