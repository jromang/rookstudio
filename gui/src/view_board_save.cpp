// The Chess space's Board page: where a game goes when it is saved (mockup M2, boards 1 and 8). A database's game is
// replaced under its number; another one (pasted, new, a PGN file's, a tournament's) goes where a menu under the Save
// button says: a database, its PGN file at its place, a new file or the end of one. Ctrl+Shift+S opens that menu for
// any game, a copy then.

#include <algorithm>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include <study/import.hpp>
#include <tour/files.hpp>
#include <tour/pgn_index.hpp>

#include "app.hpp"
#include "board_text.hpp"
#include "i18n.hpp"
#include "portable.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

}  // namespace

void App::fillBoardEco() {
    // Written from the moves when the game has none (the M2 complement); a FEN game or a Chess960 one has none.
    auto& main = board_.tree.main;
    if (const auto eco = main.header("ECO"); !eco.empty() && eco != "?") return;
    const auto opening = boardOpening();
    if (!opening || opening->eco.empty()) return;
    std::erase_if(main.headers, [](const auto& h) { return h.first == "ECO"; });
    main.headers.emplace_back("ECO", opening->eco);
    board_.game = lineOf(board_.tree, board_.line);
}

void App::boardSaveMenu(bool copy) {
    // A game nobody played yet: its players first, the headers unfolded (M2, board 8).
    const auto& main = board_.tree.main;
    const auto named = [&](const char* key) {
        const auto v = main.header(key);
        return !v.empty() && v != "?";
    };
    if (!copy && !named("White") && !named("Black") && !headers_open_) {
        headers_open_ = true;
        boardToast(TR("Name the players, then Ctrl+S again"));
        return;
    }
    save_menu_ = true;
    save_copy_ = copy;
}

bool App::saveBoardInto(std::size_t index, bool copy) {
    auto& s = shelf();
    if (index >= s.entries().size()) return false;
    const auto& entry = s.entries()[index];
    const auto db     = s.open(index);
    if (!db) {
        boardToast(trf("« {} » cannot be opened.", entry.name));
        return false;
    }
    if (db->readOnly()) {
        boardToast(
            trf("« {} » is read-only for now, another program writes to it: the game stays changed here.", entry.name));
        return false;
    }
    fillBoardEco();
    study::ImportedGame game;
    if (!study::readImportedGame(gamePgn(board_.tree, board_.chess960), game)) {
        boardToast(TR("The game cannot be written as PGN."));
        return false;
    }
    study::GameId id = 0;
    try {
        id = db->snapshot()->games();  // the next number
        std::vector<study::ImportedGame> one;
        one.push_back(std::move(game));
        (void)db->append(std::move(one), true);
    } catch (const std::exception& e) {
        boardToast(e.what());
        return false;
    }
    if (copy) {  // the game stays where it came from, and as changed as it was
        boardToast(trf("A copy saved in « {} », game {}", entry.name, ui::exactCount(id + 1)));
        s.rebuildTree(index);
        return true;
    }
    // The game is that database's now: the next Ctrl+S replaces it there.
    BoardOrigin origin;
    origin.folder                 = entry.folder;
    origin.name                   = entry.name;
    origin.id                     = id;
    board_origin_                 = std::move(origin);
    board_.settings.save_database = entry.folder.string();
    saveBoardSettings();
    board_.changes.saved("« " + entry.name + " »");
    s.rebuildTree(index);
    boardToast(trf("Saved in « {} », game {}", entry.name, ui::exactCount(id + 1)));
    return true;
}

bool App::saveBoardInPgnFile() {
    auto& b = board_;
    if (!b.pgn || b.pgn->file.empty() || b.pgn->selected < 0) return false;
    auto& source    = *b.pgn;
    const auto file = source.file;
    const auto name = file.filename().string();
    const auto at   = static_cast<std::size_t>(source.selected);
    std::string text;
    {
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            boardToast(trf("« {} » cannot be read: the game stays changed here.", name));
            return false;
        }
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    // The file changed since it was read: the game found again by its players, else not written there.
    auto index = source.index;
    if (text.size() != index->bytes) {
        std::istringstream again(text);
        auto fresh      = std::make_shared<const tour::PgnIndex>(tour::indexPgn(again));
        const auto& was = index->games[at];
        if (at >= fresh->games.size() || fresh->str(fresh->games[at].white) != index->str(was.white) ||
            fresh->str(fresh->games[at].black) != index->str(was.black)) {
            boardToast(trf("« {} » has changed since: game {} is no longer there. Ctrl+Shift+S saves it elsewhere.",
                           name, at + 1));
            return false;
        }
        index = std::move(fresh);
    }
    const auto span = index->games[at].span;
    if (span.end() > text.size()) return false;
    fillBoardEco();
    auto pgn = gamePgn(b.tree, b.chess960);
    while (!pgn.empty() && (pgn.back() == '\n' || pgn.back() == '\r')) pgn.pop_back();
    // The blank line after it kept, unless it was the file's last game.
    pgn += span.end() < text.size() ? "\n\n" : "\n";
    text.replace(span.offset, span.length, pgn);
    try {
        tour::replaceFile(file, text);
    } catch (const std::exception& e) {
        boardToast(trf("Cannot write {}: {}", name, e.what()));
        return false;
    }
    std::istringstream written(text);
    source.index        = std::make_shared<const tour::PgnIndex>(tour::indexPgn(written));
    source.filter_dirty = true;
    b.changes.saved(name);
    boardToast(trf("Saved in « {} », game {}, at its place", name, at + 1));
    return true;
}

void App::drawSaveMenu(ImVec2 under) {
    constexpr const char* kId = "##save_menu";
    if (save_menu_) {
        save_menu_ = false;
        ImGui::OpenPopup(kId);
    }
    if (ImGui::IsPopupOpen(kId)) ImGui::SetNextWindowPos(under, ImGuiCond_Appearing, ImVec2(1, 0));
    if (!ImGui::BeginPopup(kId)) return;
    struct Choice {
        std::string label, detail;
        std::function<void()> act;
        std::string off;  // why it cannot be chosen; empty when it can
    };
    std::vector<Choice> choices;
    auto& s           = shelf();
    const auto& b     = board_;
    const bool own_db = board_origin_ && !board_origin_->linked;
    if (save_copy_ && own_db)
        choices.push_back({trf("Into « {} », under its number", board_origin_->name),
                           TR("what Ctrl+S does"),
                           [this] { (void)saveBoardToDatabase(); },
                           {}});
    const bool from_file = !own_db && b.pgn && !b.pgn->file.empty() && b.pgn->selected >= 0;
    if (from_file)
        choices.push_back({trf("Into « {} », at its place", b.pgn->file.filename().string()),
                           TR("the file written again at once, its other games as they are"),
                           [this] { (void)saveBoardInPgnFile(); },
                           {}});
    // The user's databases, the one saved into last first.
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < s.entries().size(); ++i)
        if (!s.entries()[i].source) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t c) {
        const auto last = std::filesystem::path(b.settings.save_database);
        return (s.entries()[a].folder == last) > (s.entries()[c].folder == last);
    });
    for (const auto i : order) {
        const auto& e    = s.entries()[i];
        const auto facts = s.facts(i);
        Choice c;
        c.label = save_copy_ ? trf("A copy in « {} »", e.name) : trf("Into « {} » · one more game", e.name);
        if (save_copy_ && own_db && e.folder == board_origin_->folder) c.label = trf("A copy in « {} » too", e.name);
        c.detail = e.folder == std::filesystem::path(b.settings.save_database)
                       ? std::string(TR("where you saved last time"))
                       : trn("{} game", "{} games", static_cast<int>(std::min<uint64_t>(facts.games, 1000000000)),
                             ui::exactCount(facts.games));
        if (from_file && !save_copy_) c.detail += std::string(" · ") + TR("the file left as it is");
        if (facts.read_only) c.off = TR("read-only: another program writes to it");
        if (!facts.error.empty()) c.off = facts.error;
        c.act = [this, i, copy = save_copy_] { (void)saveBoardInto(i, copy); };
        choices.push_back(std::move(c));
    }
    choices.push_back({TR("Into a PGN file…"), TR("a new file"), [this] { saveBoardPgn(); }, {}});
    choices.push_back({TR("At the end of a PGN file…"),
                       TR("the file's games kept, this one after them"),
                       [this] { dialogs_.openFile(Dialogs::Purpose::AppendBoardPgn, {{"PGN", "pgn"}}); },
                       {}});

    ImGui::TextDisabled("%s", save_copy_ ? TR("Save a copy of the game") : TR("Save the game"));
    std::function<void()> chosen;
    bool first = true;
    for (std::size_t i = 0; i < choices.size(); ++i) {
        const auto& c = choices[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginDisabled(!c.off.empty());
        const auto text = c.label + (first && c.off.empty() ? "   " + std::string(TR("Enter")) : std::string());
        if (ImGui::Selectable(text.c_str(), false, 0, ImVec2(px(380), 0))) chosen = c.act;
        ImGui::EndDisabled();
        if (!c.off.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", c.off.c_str());
        ImGui::Indent(px(10));
        ImGui::TextDisabled("%s", (c.off.empty() ? c.detail : c.off).c_str());
        ImGui::Unindent(px(10));
        if (first && c.off.empty()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
                chosen = c.act;
            first = false;
        }
        ImGui::PopID();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    if (chosen) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    if (chosen) chosen();
}

}  // namespace fcg
