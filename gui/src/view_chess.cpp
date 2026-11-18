// The Chess space's own pages: its home (what runs first, the game to resume, what to start), the databases and the
// openings (their repertoires). Mockups M0, M1 and M3 (PLAN.md).

#include <algorithm>
#include <string>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// "White – Black, Event", from a game's tags; empty for a game without players.
std::string gameTitle(const ReplayGame& game) {
    const auto known = [](const std::string& tag) { return !tag.empty() && tag != "?"; };
    const auto white = game.header("White"), black = game.header("Black"), event = game.header("Event");
    if (!known(white) && !known(black)) return {};
    std::string out = (known(white) ? white : "?") + " – " + (known(black) ? black : "?");
    if (known(event)) out += ", " + event;
    return out;
}

}  // namespace

void App::drawChessHome() {
    ui::pageTitle(TR("Home"), guideSection());
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(20), ImGui::GetContentRegionMax().x - px(340)));
    if (ui::iconButton("chess_search", ui::Icon::Search, TR("Find a command, a game, a database"))) {
        palette_request_ = true;
    }
    ImGui::SameLine(0, px(6));
    ImGui::TextDisabled("Ctrl+K");
    ImGui::Dummy(ImVec2(0, px(4)));

    // What runs, first: the game's analysis.
    if (board_.reviewing() || board_.analysing) {
        ui::beginCard("chess_running");
        ImGui::TextUnformatted(TR("In progress"));
        for (const auto& task : board_.review_tasks) {
            const auto& done  = task.state.done;
            const auto count  = std::count(done.begin(), done.end(), true);
            const float share = done.empty() ? 0.0f : static_cast<float>(count) / static_cast<float>(done.size());
            const auto title  = gameTitle(board_.review_game);
            ImGui::TextUnformatted(trf("The game's analysis by {}{}: pass {}", task.engine,
                                       title.empty() ? "" : " · " + title, task.state.pass + 1)
                                       .c_str());
            ImGui::SameLine();
            ImGui::ProgressBar(share, ImVec2(px(160), 0));
        }
        if (board_.analysing) ImGui::TextUnformatted(TR("The analysis of the board's position is running."));
        if (ImGui::Button(TR("Show the board"))) page_ = Page::Board;
        ui::endCard();
        ImGui::Dummy(ImVec2(0, px(6)));
    }

    // Where the user was: the Board page's game.
    if (!board_.game.moves.empty()) {
        ui::beginCard("chess_resume");
        ImGui::TextDisabled("%s", TR("Resume where you were"));
        const auto title = gameTitle(board_.game);
        ImGui::TextUnformatted(title.empty() ? TR("The game on the board") : title.c_str());
        ImGui::TextDisabled("%s", trn("{} move", "{} moves", static_cast<int>(board_.game.moves.size() + 1) / 2,
                                      static_cast<int>(board_.game.moves.size() + 1) / 2)
                                      .c_str());
        if (ImGui::Button(TR("Resume")) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) page_ = Page::Board;
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("or Ctrl+Tab: the recent games"));
        ui::endCard();
        ImGui::Dummy(ImVec2(0, px(6)));
    }

    // What to start.
    ui::beginCard("chess_start");
    ImGui::TextUnformatted(TR("Start"));
    if (ui::iconButton("chess_open", ui::Icon::Folder, TR("Open a PGN…"))) {
        page_ = Page::Board;
        dialogs_.openFile(Dialogs::Purpose::BoardPgn, {{"PGN", "pgn"}}, storage_.runsDir().string());
    }
    ImGui::SameLine();
    if (ui::iconButton("chess_paste", ui::Icon::Paste, TR("Paste a game or a FEN"))) {
        page_ = Page::Board;
        pasteOnBoard();
    }
    ImGui::SameLine();
    if (ui::iconButton("chess_position", ui::Icon::Edit, TR("Set up a position"))) {
        page_ = Page::Board;
        openPositionEditor();
    }
    ImGui::SameLine();
    if (ui::iconButton("chess_play", ui::Icon::Play, TR("Play against an engine"))) {
        page_            = Page::Board;
        board_.play_open = true;
    }
    ImGui::TextDisabled("%s", TR("Everything works without an account or the Internet. A PGN file dropped on the "
                                 "window opens on the board."));
    ui::endCard();
}

void App::drawOpeningsPage() {
    ui::pageTitle(TR("Openings"), guideSection());
    ImGui::TextDisabled("%s", TR("No repertoire yet."));
}

}  // namespace fcg
