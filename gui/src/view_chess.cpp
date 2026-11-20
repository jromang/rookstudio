// The Chess space's own pages: its home (what runs first, the game to resume, what to start), the databases and the
// openings (their repertoires). Mockups M0, M1 and M3 (PLAN.md).

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include <study/game_tree.hpp>
#include <study/linked.hpp>

#include "app.hpp"
#include "assistant_service.hpp"
#include "board_view.hpp"
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
    // At the right, as wide as its label in the language of the interface, its key after it.
    const char* search_label = TR("Find a command, a game, a database");
    const auto& style        = ImGui::GetStyle();
    const float search_w     = style.FramePadding.x * 2 + ImGui::GetFrameHeight() * 0.42f + style.ItemInnerSpacing.x +
                               ImGui::CalcTextSize(search_label).x + px(6) + ImGui::CalcTextSize("Ctrl+K").x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(20), ImGui::GetContentRegionMax().x - search_w));
    if (ui::iconButton("chess_search", ui::Icon::Search, search_label)) {
        palette_request_ = true;
    }
    ImGui::SameLine(0, px(6));
    ImGui::TextDisabled("Ctrl+K");
    ImGui::Dummy(ImVec2(0, px(4)));
    auto& b = board_;
    // The cards of the view's pages (M6, board 3): the board's, the databases', Lichess', the tournaments'.
    const auto& v          = view();
    const bool board       = v.holds(views::PageId::Board) || v.holds(views::PageId::Play);
    const bool bases       = v.holds(views::PageId::Databases);
    const bool lichess     = v.holds(views::PageId::Lichess);
    const bool tournaments = v.holds(views::PageId::Tournaments);

    // Where the user was (mockup M0): the Board page's game at its move, where it comes from, what it holds.
    const bool has_game = !b.game.moves.empty() || !b.tree.variations.empty() || !b.title.empty();
    if (has_game && board) {
        ui::beginCard("chess_resume");
        BoardDecor decor;
        decor.eval_white = -1.0f;
        decor.flipped    = b.flip;
        drawBoard(atlas_, boardAt(b.game, b.chess960, b.ply), px(112), decor);
        ImGui::SameLine(0, px(16));
        ImGui::BeginGroup();
        ImGui::TextDisabled("%s", TR("Resume where you were"));
        const auto title = gameTitle(b.game);
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.15f);
        ImGui::TextUnformatted(!title.empty()     ? title.c_str()
                               : !b.title.empty() ? b.title.c_str()
                                                  : TR("The game on the board"));
        ImGui::PopFont();
        std::vector<std::string> parts;
        if (board_origin_) {
            const auto& o = *board_origin_;
            parts.push_back(o.filter.empty() || !o.view
                                ? trf("{} · game {}", o.name, ui::exactCount(o.id + 1))
                                : trf("{} · game {} of the filter “{}”", o.name, ui::exactCount(o.row + 1), o.filter));
        } else if (b.pgn) {
            parts.push_back(b.pgn->name);
        }
        if (b.ply > 0) parts.push_back(trf("at {}", boardMoveLabel(b.ply)));
        std::size_t comments = 0;
        const auto count     = [&](const std::vector<ReplayMove>& moves) {
            for (const auto& m : moves)
                comments += (m.notes.before.empty() ? 0 : 1) + (m.notes.after.empty() ? 0 : 1) +
                            (m.notes.assistant.empty() ? 0 : 1);
        };
        count(b.tree.main.moves);
        for (const auto& v : b.tree.variations) count(v.moves);
        if (comments > 0) parts.push_back(trn("{} comment", "{} comments", comments, comments));
        if (b.changes.any()) parts.push_back(TR("changed, not saved yet"));
        std::string sub;
        for (const auto& part : parts) sub += (sub.empty() ? "" : " · ") + part;
        if (!sub.empty()) ImGui::TextDisabled("%s", sub.c_str());
        ImGui::Dummy(ImVec2(0, px(4)));
        if (ImGui::Button(TR("Resume")) ||
            (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::GetIO().WantTextInput))
            page_ = Page::Board;
        ImGui::SameLine(0, px(4));
        ImGui::TextDisabled("%s", TR("Enter"));
        ImGui::SameLine(0, px(14));
        ImGui::TextDisabled("%s", TR("or Ctrl+Tab: the recent games"));
        ImGui::EndGroup();
        ui::endCard();
        ImGui::Dummy(ImVec2(0, px(6)));
    }

    // What to start, each with its key.
    const auto key = [](const char* k) {
        ImGui::SameLine(0, px(4));
        ImGui::TextDisabled("%s", k);
        ImGui::SameLine(0, px(14));
    };
    if (board) {
        if (ui::iconButton("chess_open", ui::Icon::Folder, TR("Open a PGN…"))) openBoardPgnDialog();
        key("Ctrl+O");
        if (ui::iconButton("chess_paste", ui::Icon::Paste, TR("Paste a game or a FEN"))) {
            page_ = Page::Board;
            pasteOnBoard();
        }
        key("Ctrl+V");
        if (ui::iconButton("chess_new", ui::Icon::Plus, TR("New game"))) newBoardGame();
        key("Ctrl+N");
    }
    if (bases) {
        if (ui::iconButton("chess_import", ui::Icon::Database, TR("Import into a database…"))) {
            page_ = Page::Databases;
            dialogs_.openFile(Dialogs::Purpose::ImportGames, {{"PGN", "pgn;zst;gz;zip"}});
        }
        ImGui::SameLine(0, px(14));
    }
    if (board && ui::iconButton("chess_play", ui::Icon::Play, TR("Play against an engine"))) {
        page_            = Page::Board;
        board_.play_open = true;
    }
    if (tournaments) {
        if (board) ImGui::SameLine(0, px(14));
        if (ui::iconButton("chess_tournament", ui::Icon::Trophy, TR("New tournament"))) page_ = Page::NewMatch;
    }
    if (board && !ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) {
        page_ = Page::Board;
        pasteOnBoard();
    }
    ImGui::Dummy(ImVec2(0, px(6)));

    // The tournaments running, as the Tournaments page shows them.
    if (tournaments) {
        refreshTournaments();
        drawRunCards();
        ImGui::Dummy(ImVec2(0, px(6)));
    }

    // The recent games and the databases, side by side.
    const float gap    = px(14);
    const float column = board && bases ? (ImGui::GetContentRegionAvail().x - gap) * 0.5f : 0.0f;
    std::optional<std::size_t> reopen;
    auto& shelf_    = shelf();
    const auto* job = shelf_.import();
    if (board) {
        ui::beginCard("chess_recent", ImVec2(column, 0));
        ImGui::TextUnformatted(TR("Recently"));
        for (std::size_t i = 0; i < std::min<std::size_t>(recent_games_.size(), 6); ++i) {
            const auto& r = recent_games_[i];
            ImGui::PushID(static_cast<int>(i));
            const auto start = ImGui::GetCursorPos();
            if (ImGui::Selectable("##recent", false, 0, ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 2.1f)))
                reopen = i;
            ImGui::SetCursorPos(ImVec2(start.x + px(4), start.y + px(2)));
            ImGui::TextUnformatted(r.title.c_str());
            auto sub = r.where;
            if (r.changes.any()) sub += (sub.empty() ? "" : " · ") + std::string(TR("changed, not saved yet"));
            ImGui::SetCursorPosX(start.x + px(4));
            ImGui::TextDisabled("%s", sub.empty() ? " " : sub.c_str());
            ImGui::PopID();
        }
        if (recent_games_.empty())
            ImGui::TextDisabled("%s", TR("The games you open on the board come here, Ctrl+Tab away."));
        ui::endCard();
    }
    if (board && bases) ImGui::SameLine(0, gap);
    if (bases) {
        ui::beginCard("chess_bases", ImVec2(0, 0));
        ImGui::TextUnformatted(TR("My databases"));
        const char* import_label = TR("+ Import…");
        const float import_w     = ImGui::CalcTextSize(import_label).x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(10), ImGui::GetContentRegionMax().x - import_w));
        if (ImGui::SmallButton(import_label)) {
            page_ = Page::Databases;
            dialogs_.openFile(Dialogs::Purpose::ImportGames, {{"PGN", "pgn;zst;gz;zip"}});
        }
        std::size_t linked = 0, shown = 0;
        for (std::size_t i = 0; i < shelf_.entries().size(); ++i) {
            const auto& e = shelf_.entries()[i];
            if (e.source) {
                linked += e.source->kind == study::LinkedSource::Kind::Tournament ? 1 : 0;
                continue;
            }
            ++shown;
            const auto facts = shelf_.facts(i);
            ImGui::PushID(static_cast<int>(i));
            const auto start = ImGui::GetCursorPos();
            if (ImGui::Selectable("##base", false, 0, ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 2.1f))) {
                db_selected_ = static_cast<int>(i);
                db_list_     = true;
                page_        = Page::Databases;
            }
            ImGui::SetCursorPos(ImVec2(start.x + px(4), start.y + px(2)));
            ImGui::TextUnformatted(e.name.c_str());
            const auto count = ui::exactCount(facts.games);
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(count.c_str()).x - px(4));
            ImGui::TextDisabled("%s", count.c_str());
            std::string sub;
            if (job && job->index == i) {
                const auto total = std::max<uint64_t>(1, job->progress.total_bytes);
                sub              = job->building ? std::string(TR("its opening tree being built"))
                                                 : trf("import of {}: {} %", job->file.filename().string(),
                                                       static_cast<int>(100 * job->progress.bytes.load() / total));
            } else if (!facts.error.empty()) {
                sub = facts.error;
            } else if (facts.changed) {
                sub = trf("changed {}", ui::dayText(facts.changed));
                if (facts.tree) sub += std::string(" · ") + TR("tree up to date");
            }
            ImGui::SetCursorPosX(start.x + px(4));
            ImGui::TextDisabled("%s", sub.empty() ? " " : sub.c_str());
            ImGui::PopID();
        }
        if (linked > 0) {
            if (ImGui::Selectable(trn("Tournaments ({}) · their games, read without a copy",
                                      "Tournaments ({}) · their games, read without a copy", linked, linked)
                                      .c_str()))
                page_ = Page::Databases;
        }
        if (shown == 0)
            ImGui::TextDisabled("%s", TR("No database yet: import a PGN file, or drop one on the Databases page."));
        drawTwicHomeNotice();  // a new issue of TWIC, when the weekly look found one (#71)
        ui::endCard();
    }
    if (reopen) restoreRecentGame(*reopen);

    // Lichess (M0, M4): the account, a player's games, the broadcasts live.
    if (lichess) {
        ImGui::Dummy(ImVec2(0, px(6)));
        drawLichessHomeCard();
    }

    // What runs: the game's analysis, the position's, an import, the assistant (M5, board 1); the positions of the
    // repertoires to review (#76).
    const bool importing = job != nullptr;
    const auto assistant = assistantStatus();
    const bool training  = trainingDue();
    if (b.reviewing() || b.analysing || importing || !assistant.empty() || training) {
        ImGui::Dummy(ImVec2(0, px(6)));
        ui::beginCard("chess_running");
        ImGui::TextUnformatted(TR("In progress"));
        if (training) drawTrainingHome();
        for (const auto& task : b.review_tasks) {
            const auto& done  = task.state.done;
            const auto count  = std::count(done.begin(), done.end(), true);
            const float share = done.empty() ? 0.0f : static_cast<float>(count) / static_cast<float>(done.size());
            const auto title  = gameTitle(b.review_game);
            ImGui::TextUnformatted(trf("The game's analysis by {}{}: pass {}", task.engine,
                                       title.empty() ? "" : " · " + title, task.state.pass + 1)
                                       .c_str());
            ImGui::SameLine();
            ImGui::ProgressBar(share, ImVec2(px(160), 0));
        }
        if (b.analysing) ImGui::TextUnformatted(TR("The analysis of the board's position is running."));
        if (importing) {
            const auto total = std::max<uint64_t>(1, job->progress.total_bytes);
            const float share =
                job->building ? 1.0f : static_cast<float>(job->progress.bytes.load()) / static_cast<float>(total);
            ImGui::TextUnformatted(trf("Import of {}", job->file.filename().string()).c_str());
            ImGui::SameLine();
            ImGui::ProgressBar(share, ImVec2(px(160), 0));
        }
        if (!assistant.empty()) {
            ImGui::TextUnformatted(assistant.c_str());
            if (assistant_->state() == AssistantService::State::Ready) {
                ImGui::SameLine();
                if (ImGui::SmallButton(TR("Unload"))) assistant_->unload();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", TR("Its process ends and gives its memory back; the next question loads "
                                               "it again."));
            }
        }
        ui::endCard();
    }
}

}  // namespace fcg
