// The Chess space's column beside the board (mockups M2 and M3): its tabs, the opening tree of the databases and the
// Polyglot books of the library, and the reference games of the position.

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <string>
#include <utility>

#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <json.hpp>

#include <commentary/position.hpp>
#include <study/import.hpp>
#include <study/pgn_reader.hpp>
#include <study/scan_board.hpp>
#include <tour/files.hpp>

#include "app.hpp"
#include "board_text.hpp"
#include "console.hpp"
#include "i18n.hpp"
#include "notation_view.hpp"
#include "portable.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// FIDE's rating difference for a score, from 0.50 to 1.00 by hundredths (FIDE handbook B.02, table 8.1a); a score
// under one half is the difference of its complement, negated.
int ratingDifference(double score) {
    static constexpr int DP[51] = {0,   7,   14,  21,  29,  36,  43,  50,  57,  65,  72,  80,  87,  95,  102, 110, 117,
                                   125, 133, 141, 149, 158, 166, 175, 184, 193, 202, 211, 220, 230, 240, 251, 262, 273,
                                   284, 296, 309, 322, 336, 351, 366, 383, 401, 422, 444, 470, 501, 538, 589, 677, 800};
    const bool below            = score < 0.5;
    const int at                = static_cast<int>(std::lround(std::abs((below ? 1 - score : score) - 0.5) * 100));
    const int dp                = DP[std::clamp(at, 0, 50)];
    return below ? -dp : dp;
}


}  // namespace

namespace {

bool boardHasGame(const BoardPage& b) {
    return !b.game.moves.empty() || !b.tree.variations.empty() || !b.title.empty();
}

// "Changed: 2 changes", or where it was saved while nothing changed since; empty for a game as it was opened.
std::string changesText(const GameChanges& c) {
    if (c.any()) {
        const auto n = static_cast<uint64_t>(c.count < 0 ? -c.count : c.count);
        return c.lost || n == 0 ? std::string(TR("Changed")) : trn("Changed: {} change", "Changed: {} changes", n, n);
    }
    return c.saved_in.empty() ? std::string() : trf("Saved in {}", c.saved_in);
}

// A row of the recent games: the title, then where it comes from and its state. `menu`: the right button on it.
bool recentRow(int id, const std::string& title, std::string where, const GameChanges& changes, bool open_now,
               bool chosen, bool* menu = nullptr) {
    const bool clicked = ImGui::Selectable(fmt::format("{}##recent{}", title, id).c_str(), chosen);
    if (menu) *menu = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    if (open_now) where += (where.empty() ? "" : " · ") + std::string(TR("open now"));
    if (changes.any()) where += (where.empty() ? "" : " · ") + std::string(TR("changed, not saved yet"));
    if (!where.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", where.c_str());
    }
    return clicked;
}

}  // namespace

void App::rememberBoardGame() {
    auto& b = board_;
    if (std::exchange(forget_board_game_, false) || !boardHasGame(b)) return;
    RecentGame game;
    game.title       = b.title.empty() ? std::string(TR("The game on the board")) : b.title;
    game.where       = board_origin_ ? board_origin_->name : b.pgn ? b.pgn->name : std::string();
    game.tree        = b.tree;
    game.line        = b.line;
    game.ply         = b.ply;
    game.chess960    = b.chess960;
    game.changes     = b.changes;
    game.origin      = board_origin_;
    game.pgn         = b.pgn;
    game.start_notes = b.start_notes;
    game.undo        = std::move(b.undo);  // the board takes another game: its history goes with this one
    game.redo        = std::move(b.redo);
    game.sent        = b.sent;
    game.since_sent  = b.since_sent;
    b.undo.clear();
    b.redo.clear();
    // One game of a list at a time, the last shown, unless a game has changes not saved: those stay until saved.
    // And a game read again from where it was read takes the place of its unchanged copy.
    std::erase_if(recent_games_, [&](const RecentGame& r) {
        if (r.changes.any()) return false;
        if (r.origin && game.origin)  // the same list, or the same game (a game kept from a session has no list)
            return (r.origin->view && r.origin->view == game.origin->view) ||
                   (r.origin->folder == game.origin->folder && r.origin->id == game.origin->id);
        return !r.origin && !game.origin && r.title == game.title &&
               r.tree.main.moves.size() == game.tree.main.moves.size();
    });
    recent_games_.push_front(std::move(game));
    // At most 12, the oldest unchanged ones going first.
    while (recent_games_.size() > 12) {
        auto last = std::find_if(recent_games_.rbegin(), recent_games_.rend(),
                                 [](const RecentGame& r) { return !r.changes.any(); });
        recent_games_.erase(last != recent_games_.rend() ? std::next(last).base() : std::prev(recent_games_.end()));
    }
}

void App::saveRecentGames() const {
    if (options_.scripted) return;  // a run for screenshots leaves the profile as it was
    nlohmann::json all = nlohmann::json::array();
    const auto entry = [&](const std::string& title, const std::string& where, const GameTree& tree, int line, int ply,
                           bool chess960, const GameChanges& changes, const std::optional<BoardOrigin>& origin,
                           bool current, const std::optional<lichess::SentRecord>& sent,
                           const GameChanges& since_sent) {
        nlohmann::json j;
        j["title"]    = title;
        j["where"]    = where;
        j["pgn"]      = gamePgn(tree, chess960);
        j["line"]     = line;
        j["ply"]      = ply;
        j["chess960"] = chess960;
        j["changes"]  = {{"count", changes.count}, {"lost", changes.lost}, {"saved_in", changes.saved_in}};
        if (origin)
            j["origin"] = {{"folder", portable::stored(origin->folder.string())},
                           {"name", origin->name},
                           {"linked", origin->linked},
                           {"id", origin->id}};
        if (current) j["current"] = true;
        if (sent) {  // sent to a Lichess study (#79)
            j["sent"]       = lichess::sentJson(*sent);
            j["since_sent"] = {{"count", since_sent.count}, {"lost", since_sent.lost}};
        }
        all.push_back(std::move(j));
    };
    const auto& b = board_;
    if (boardHasGame(b))
        entry(b.title,
              board_origin_ ? board_origin_->name
              : b.pgn       ? b.pgn->name
                            : std::string(),
              b.tree, b.line, b.ply, b.chess960, b.changes, board_origin_, true, b.sent, b.since_sent);
    for (const auto& r : recent_games_)
        entry(r.title, r.where, r.tree, r.line, r.ply, r.chess960, r.changes, r.origin, false, r.sent, r.since_sent);
    try {
        tour::replaceFile(storage_.root() / "recent.json", all.dump(1));
    } catch (const std::exception& e) {
        Console::instance().add(trf("The recent games could not be kept: {}", e.what()), true);
    }
}

void App::loadRecentGames() {
    if (options_.scripted) return;
    std::ifstream in(storage_.root() / "recent.json", std::ios::binary);
    if (!in) return;
    nlohmann::json all;
    try {
        all = nlohmann::json::parse(in);
    } catch (const std::exception&) {
        return;  // a file cut short: the games of the session before are let go
    }
    if (!all.is_array()) return;
    for (const auto& j : all) {
        if (!j.is_object()) continue;
        const auto read = study::readPgnGame(j.value("pgn", std::string()));
        if (!read.tree) continue;
        RecentGame r;
        r.title    = j.value("title", std::string());
        r.where    = j.value("where", std::string());
        r.tree     = *read.tree;
        r.line     = j.value("line", -1);
        r.ply      = j.value("ply", 0);
        r.chess960 = j.value("chess960", read.chess960);
        if (const auto c = j.find("changes"); c != j.end() && c->is_object()) {
            r.changes.count    = c->value("count", 0);
            r.changes.lost     = c->value("lost", false);
            r.changes.saved_in = c->value("saved_in", std::string());
        }
        if (const auto o = j.find("origin"); o != j.end() && o->is_object()) {
            BoardOrigin origin;
            origin.folder = std::filesystem::path(portable::resolved(o->value("folder", std::string())));
            origin.name   = o->value("name", std::string());
            origin.linked = o->value("linked", false);
            origin.id     = o->value("id", study::GameId{0});
            r.origin      = std::move(origin);
        }
        if (const auto sent = j.find("sent"); sent != j.end()) {
            r.sent = lichess::sentOf(*sent);
            if (const auto c = j.find("since_sent"); r.sent && c != j.end() && c->is_object()) {
                r.since_sent.count = c->value("count", 0);
                r.since_sent.lost  = c->value("lost", false);
            }
        }
        if (j.value("current", false) && !boardHasGame(board_)) {
            // The game the board showed, on it again, the page shown at the start kept.
            const auto page = page_;
            recent_games_.push_front(std::move(r));
            restoreRecentGame(0);
            page_ = page;
            board_back_.reset();
        } else if (recent_games_.size() < 12) {
            recent_games_.push_back(std::move(r));
        }
    }
}

void App::restoreRecentGame(std::size_t index) {
    if (index >= recent_games_.size()) return;
    auto chosen = std::move(recent_games_[index]);
    recent_games_.erase(recent_games_.begin() + static_cast<std::ptrdiff_t>(index));
    openOnBoard(chosen.tree.main, 0, chosen.chess960, chosen.title, false);  // remembers the one shown
    auto& b       = board_;
    b.tree        = std::move(chosen.tree);
    b.line        = chosen.line >= -1 && chosen.line < static_cast<int>(b.tree.variations.size()) ? chosen.line : -1;
    b.game        = lineOf(b.tree, b.line);
    b.ply         = std::clamp(chosen.ply, 0, static_cast<int>(b.game.moves.size()));
    b.start_notes = std::move(chosen.start_notes);
    b.undo        = std::move(chosen.undo);
    b.redo        = std::move(chosen.redo);
    b.changes     = chosen.changes;
    b.pgn         = std::move(chosen.pgn);
    board_origin_ = std::move(chosen.origin);
    b.sent        = std::move(chosen.sent);
    b.since_sent  = chosen.since_sent;
}

std::optional<std::size_t> App::changedCopyOf(const std::filesystem::path& folder, uint32_t id) const {
    for (std::size_t i = 0; i < recent_games_.size(); ++i) {
        const auto& r = recent_games_[i];
        if (r.changes.any() && r.origin && r.origin->folder == folder && r.origin->id == id) return i;
    }
    return std::nullopt;
}

void App::drawRecentGames() {
    // The rows: the game on the board ("open now"), then the recent ones, the newest first.
    const auto& io    = ImGui::GetIO();
    const int current = boardHasGame(board_) ? 1 : 0;
    const int n       = current + static_cast<int>(recent_games_.size());
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && io.KeyCtrl && !recent_games_.empty()) {
        recent_pick_ =
            recent_pick_ < 0 ? (io.KeyShift ? n - 1 : current) : (recent_pick_ + (io.KeyShift ? n - 1 : 1)) % n;
    }
    if (recent_pick_ < 0) return;
    if (recent_pick_ >= n) recent_pick_ = n - 1;
    if (!io.KeyCtrl) {
        // Ctrl let go: the game chosen on the board, the one there among the recent ones.
        const int pick = recent_pick_ - current;
        recent_pick_   = -1;
        if (pick >= 0) restoreRecentGame(static_cast<std::size_t>(pick));  // else the one already open
        return;
    }
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.4f),
        ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("##recent_games", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::TextUnformatted(TR("Recent games"));
    ImGui::TextDisabled("%s", TR("Ctrl+Tab forward, Ctrl+Shift+Tab back; letting Ctrl go opens the game chosen"));
    ImGui::Separator();
    if (current) {
        const auto& b = board_;
        recentRow(0, b.title.empty() ? std::string(TR("The game on the board")) : b.title,
                  board_origin_ ? board_origin_->name
                  : b.pgn       ? b.pgn->name
                                : std::string(),
                  b.changes, true, recent_pick_ == 0);
    }
    for (std::size_t i = 0; i < recent_games_.size(); ++i) {
        const auto& r = recent_games_[i];
        const int id  = current + static_cast<int>(i);
        recentRow(id, r.title, r.where, r.changes, false, recent_pick_ == id);
    }
    ImGui::End();
}

void App::drawRecentPopup() {
    if (!ImGui::BeginPopup("##recent_popup")) return;
    ImGui::TextUnformatted(TR("Recent games"));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + px(440));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", TR("A click opens one, the right button on a changed one discards its changes; Ctrl+Tab "
                                "from any page of a view whose Board is for study"));
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::Separator();
    std::optional<std::size_t> open, discard;
    auto& b = board_;
    if (boardHasGame(b)) {
        bool menu = false;
        recentRow(-1, b.title.empty() ? std::string(TR("The game on the board")) : b.title,
                  board_origin_ ? board_origin_->name
                  : b.pgn       ? b.pgn->name
                                : std::string(),
                  b.changes, true, false, &menu);
        if (menu && b.changes.any()) ImGui::OpenPopup("##recent_open_menu");
        if (ImGui::BeginPopup("##recent_open_menu")) {
            const bool can = b.changes.reversible(b.undo.size(), b.redo.size()) || board_origin_;
            if (ImGui::MenuItem(TR("Discard the changes"), nullptr, false, can)) discardBoardChanges();
            ImGui::EndPopup();
        }
    }
    for (std::size_t i = 0; i < recent_games_.size(); ++i) {
        auto& r         = recent_games_[i];
        bool menu       = false;
        const auto name = fmt::format("##recent_menu{}", i);
        if (recentRow(static_cast<int>(i), r.title, r.where, r.changes, false, false, &menu)) open = i;
        if (menu && r.changes.any()) ImGui::OpenPopup(name.c_str());
        if (ImGui::BeginPopup(name.c_str())) {
            const bool can = r.changes.reversible(r.undo.size(), r.redo.size()) || r.origin;
            if (ImGui::MenuItem(TR("Discard the changes"), nullptr, false, can)) discard = i;
            ImGui::EndPopup();
        }
    }
    if (recent_games_.empty()) ImGui::TextDisabled("%s", TR("The games opened on the board come here."));
    if (open) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    // A recent game of a database whose history cannot bring it back goes: reopened from its list, it is as saved.
    if (discard && !discardAside(recent_games_[*discard]))
        recent_games_.erase(recent_games_.begin() + static_cast<std::ptrdiff_t>(*discard));
    if (open) restoreRecentGame(*open);
}

bool App::discardAside(RecentGame& r) {
    if (!r.changes.reversible(r.undo.size(), r.redo.size())) return false;
    while (r.changes.any()) {
        const bool redo = r.changes.count < 0;
        auto& from      = redo ? r.redo : r.undo;
        (redo ? r.undo : r.redo).push_back({r.tree, r.start_notes, r.line, r.ply});
        auto s = std::move(from.back());
        from.pop_back();
        r.tree        = std::move(s.tree);
        r.start_notes = std::move(s.start_notes);
        r.line        = s.line;
        r.ply         = s.ply;
        r.changes.undone(redo);
    }
    return true;
}

void App::discardBoardChanges() {
    auto& b = board_;
    if (!b.changes.any()) return;
    if (b.changes.reversible(b.undo.size(), b.redo.size())) {
        while (b.changes.any()) boardUndo(b.changes.count < 0);  // Ctrl+Y brings each change back
        boardToast(TR("Changes discarded · Ctrl+Y makes them again"));
        return;
    }
    if (board_origin_) {  // read again from its database, by its number, the game shown forgotten
        auto& s           = shelf();
        const auto origin = *board_origin_;
        for (std::size_t i = 0; i < s.entries().size(); ++i) {
            if (s.entries()[i].folder != origin.folder) continue;
            const auto db = s.open(i);
            if (!db || origin.id >= db->snapshot()->games()) break;
            forget_board_game_ = true;
            readOnBoard(study::importedPgn(db->snapshot()->game(origin.id)),
                        trf("{}, game {}", origin.name, ui::exactCount(origin.id + 1)));
            forget_board_game_ = false;
            board_origin_      = origin;
            boardToast(TR("Changes discarded"));
            return;
        }
        boardToast(trf("“{}” cannot be read now: the changes stay.", origin.name));
    }
}

void App::boardSave() {
    if (!chessBoard()) return saveBoardPgn();
    if (saveBoardToDatabase()) return;  // a database's game: replaced under its number
    boardSaveMenu(false);               // else where it goes, asked under the Save button
}

void App::drawBoardStrip() {
    auto& b         = board_;
    const auto& io  = ImGui::GetIO();
    const bool keys = !io.WantTextInput;  // Ctrl+Left and Ctrl+Right are a word back or forward in a field
    std::optional<std::size_t> go;
    bool left = false;
    if (board_origin_ && board_origin_->view) {
        const auto& o    = *board_origin_;
        const auto row   = o.row;
        const auto total = o.view->ids.size();
        ImGui::BeginDisabled(row == 0);
        if (ImGui::SmallButton("◂##list_previous") ||
            (keys && row > 0 && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_LeftArrow)))
            go = row - 1;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", TR("The previous game of the list · Ctrl+←"));
        ImGui::SameLine(0, px(6));
        const auto place =
            o.filter.empty()
                ? trf("{} · game {} of {}", o.name, ui::exactCount(row + 1), ui::exactCount(total))
                : trf("{} · game {} of {} · “{}”", o.name, ui::exactCount(row + 1), ui::exactCount(total), o.filter);
        ImGui::TextUnformatted(place.c_str());
        ImGui::SameLine(0, px(6));
        ImGui::BeginDisabled(row + 1 >= total);
        if (ImGui::SmallButton("▸##list_next") ||
            (keys && row + 1 < total && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_RightArrow)))
            go = row + 1;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", TR("The next game of the list · Ctrl+→"));
        left = true;
    }

    // At the right: the recent games, the game's state, Save; their keys said when there is room.
    const auto status  = changesText(b.changes);
    const char* recent = TR("Recent");
    const char* save   = TR("Save");
    const auto& style  = ImGui::GetStyle();
    const auto button  = [&](const char* s) {
        return ImGui::CalcTextSize(s, nullptr, true).x + style.FramePadding.x * 2;
    };
    const float gap    = px(10);
    const float keys_w = px(4) + ImGui::CalcTextSize("Ctrl+Tab").x + px(4) + ImGui::CalcTextSize("Ctrl+S").x;
    const float bare =
        button(recent) + gap + (status.empty() ? 0 : ImGui::CalcTextSize(status.c_str()).x + gap) + button(save);
    const float right = ImGui::GetContentRegionMax().x;
    float x           = ImGui::GetCursorPosX();
    if (left) {
        ImGui::SameLine(0, gap);
        x = ImGui::GetCursorPosX();
    }
    const bool show_keys = x + bare + keys_w <= right;
    const float width    = bare + (show_keys ? keys_w : 0);
    if (left && x + width > right) ImGui::NewLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - width));
    if (ImGui::SmallButton(recent)) ImGui::OpenPopup("##recent_popup");
    const ImVec2 under(ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y + px(4));  // the list's top right
    if (show_keys) {
        ImGui::SameLine(0, px(4));
        ImGui::TextDisabled("Ctrl+Tab");
    }
    if (!status.empty()) {
        ImGui::SameLine(0, gap);
        if (b.changes.any()) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", status.c_str());
        } else {
            ImGui::TextDisabled("%s", status.c_str());
        }
    }
    ImGui::SameLine(0, gap);
    if (ImGui::SmallButton(save)) boardSave();
    const ImVec2 save_under(ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y + px(4));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "%s", board_origin_ && !board_origin_->linked
                      ? trf("Into “{}”, under its number · Ctrl+Shift+S: a copy elsewhere", board_origin_->name).c_str()
                      : TR("Into a database, its PGN file or another: a menu · Ctrl+S"));
    if (show_keys) {
        ImGui::SameLine(0, px(4));
        ImGui::TextDisabled("Ctrl+S");
    }
    if (ImGui::IsPopupOpen("##recent_popup")) ImGui::SetNextWindowPos(under, ImGuiCond_Appearing, ImVec2(1, 0));
    drawRecentPopup();
    drawSaveMenu(save_under);
    if (go) openListRow(*go);
}

void App::drawChessColumn() {
    drawNewMoveQuestion();  // a move where the game went another way, asked about
    drawBoardStrip();
    drawSentLine();         // sent to a Lichess study: the changes since (#79)
    drawMoveField();        // while a move is typed, whatever the tab
    drawCommentProgress();  // the key moments being commented, whatever the tab
    // The tabs, in the order of the mockups; Alt+1 to Alt+5.
    const char* names[] = {TR("Notation"), TR("Analysis"), TR("Tree"), TR("References"), TR("Questions")};
    constexpr int TABS  = 5;
    for (int i = 0; i < TABS; ++i) {
        if (!ImGui::IsKeyChordPressed(ImGuiMod_Alt | static_cast<ImGuiKey>(ImGuiKey_1 + i))) continue;
        board_side_tab_ = i;
        if (i == 4) question_focus_ = true;  // Alt+5: the question is typed at once
    }
    // Fair play (#48): a live game locks every tab but the notation; a correspondence game the Analysis tab on its
    // positions and the Questions tab on its game, the tree of the databases and their reference games staying open.
    const bool live     = liveGameOn() != nullptr;
    const bool position = aidsLocked();
    const bool game     = position || gameLocked();
    const auto locked   = [&](int tab) { return (live && tab != 0) || (position && tab == 1) || (game && tab == 4); };
    if (ImGui::BeginTabBar("##chess_column")) {
        for (int i = 0; i < TABS; ++i) {
            const auto label = fmt::format("{}###chess_tab{}", names[i], i);
            if (locked(i)) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::BeginTabItem(label.c_str(), nullptr,
                                    board_side_tab_ == i ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
                ImGui::EndTabItem();
            }
            if (locked(i)) ImGui::PopStyleColor();
            if (ImGui::IsItemClicked()) board_side_tab_ = i;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", locked(i)
                                            ? fmt::format("Alt+{} · {}", i + 1,
                                                          TR("Locked while one of your games is played on Lichess."))
                                                  .c_str()
                                            : fmt::format("Alt+{}", i + 1).c_str());
        }
        ImGui::EndTabBar();
    }
    if (locked(board_side_tab_)) {
        drawFairPlayBanner(board_side_tab_ == 4);
        return;
    }
    if (position) drawFairPlayBanner();
    switch (board_side_tab_) {
        case 0:
            drawBoardMoves(std::max(px(160), ImGui::GetContentRegionAvail().y));
            break;
        case 1: {
            drawBoardOnline();  // Lichess's tables and cloud, when the user has chosen them (M4, board 5)
            drawSideBySide();   // up to four engines side by side (#78, M11 board 3)
            const float graph = boardGraphHeight();
            drawBoardGraph(std::max(graph, ImGui::GetContentRegionAvail().y));
            break;
        }
        case 2:
            drawBoardTree();
            break;
        case 3:
            drawBoardReferences();
            break;
        default:
            drawBoardQuestions();
            break;
    }
}

void App::drawBoardTree() {
    auto& b = board_;
    auto& s = shelf();
    // The sources: the databases whose tree covers their games, then the library's Polyglot and CTG books.
    std::vector<std::pair<std::string, std::size_t>> databases;
    for (std::size_t i = 0; i < s.entries().size(); ++i) {
        if (s.entries()[i].source) continue;
        if (const auto source = s.treeSource(i); source && source->tree) databases.push_back({source->name, i});
    }
    std::vector<const BookEntry*> books;
    for (const auto& book : books_) {
        auto ext = std::filesystem::path(book.path).extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".bin" || study::isCtg(book.path)) books.push_back(&book);
    }
    // My databases: two or more together, their games added (M3, board 3); then each, then the books; then Lichess's
    // explorer: the masters, Lichess's players, the player whose games were imported (M3, board 2).
    const int together = databases.size() >= 2 ? 1 : 0;
    const int lichess_sources = lichess_.player.empty() ? 2 : 3;
    const int count           = together + static_cast<int>(databases.size() + books.size()) + lichess_sources;
    if (count == lichess_sources) {
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", TR("No database with its opening tree, nor book: import games on the Databases "
                                     "page (the tree is built after the import), or add a Polyglot or CTG book on the "
                                     "Books page."));
        ImGui::PopTextWrapPos();
    }
    tree_source_         = std::clamp(tree_source_, 0, count - 1);
    const int books_at   = together + static_cast<int>(databases.size());
    const int lichess_at = books_at + static_cast<int>(books.size());
    if (!tree_book_request_.empty()) {  // a book the Books page opened in the tree (#68)
        for (std::size_t i = 0; i < books.size(); ++i)
            if (books[i]->path == tree_book_request_) tree_source_ = books_at + static_cast<int>(i);
        tree_book_request_.clear();
    }
    std::string ratings_label;
    if (!lichess_.explorer_ratings.empty())
        ratings_label = fmt::format(
            " · {}+", *std::min_element(lichess_.explorer_ratings.begin(), lichess_.explorer_ratings.end()));
    std::size_t joined = 0;
    for (const auto& [name, i] : databases) joined += tree_left_.count(s.entries()[i].folder) ? 0 : 1;
    // The player's games online; told apart from the database their sync fills, which has the same name.
    std::string player_label = "Lichess · " + lichess_.player;
    if (std::any_of(databases.begin(), databases.end(), [&](const auto& d) { return d.first == player_label; }))
        player_label += std::string(" · ") + TR("online");
    for (int i = 0; i < count; ++i) {
        const auto label = i < together          ? trf("My databases · {}", joined)
                           : i < books_at        ? databases[static_cast<std::size_t>(i - together)].first
                           : i < lichess_at      ? books[static_cast<std::size_t>(i - books_at)]->name
                           : i == lichess_at     ? std::string(TR("Masters (Lichess)"))
                           : i == lichess_at + 1 ? "Lichess" + ratings_label
                                                 : player_label;
        // On as many lines as they need: the user's databases, the books, then Lichess's three.
        if (i > 0) ui::sameLineOrWrap(ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2);
        const bool on = tree_source_ == i;
        if (on) {  // the source shown stays blue under the mouse, its text white
            const auto accent = ImGui::ColorConvertU32ToFloat4(ui::ACCENT);
            ImGui::PushStyleColor(ImGuiCol_Button, accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        }
        if (ImGui::Button(fmt::format("{}##tree_source{}", label, i).c_str())) tree_source_ = i;
        if (on) ImGui::PopStyleColor(4);
        // Lichess's players: the right button chooses the speeds and the ratings.
        if (i == lichess_at + 1) {
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s",
                                  TR("Lichess's players' games; the right button chooses their speeds and ratings"));
            if (ImGui::BeginPopupContextItem("##lichess_filter")) {
                bool changed = false;
                ImGui::TextDisabled("%s", TR("Speeds"));
                for (const char* speed : {"bullet", "blitz", "rapid", "classical", "correspondence"}) {
                    auto& v = lichess_.explorer_speeds;
                    bool in = std::find(v.begin(), v.end(), speed) != v.end();
                    if (ImGui::Checkbox(speed, &in)) {
                        if (in)
                            v.push_back(speed);
                        else
                            std::erase(v, speed);
                        changed = true;
                    }
                }
                ImGui::TextDisabled("%s", TR("Ratings"));
                for (const int r : {1600, 1800, 2000, 2200, 2500}) {
                    auto& v = lichess_.explorer_ratings;
                    bool in = std::find(v.begin(), v.end(), r) != v.end();
                    if (ImGui::Checkbox(fmt::format("{}+##rating{}", r, r).c_str(), &in)) {
                        if (in)
                            v.push_back(r);
                        else
                            std::erase(v, r);
                        changed = true;
                    }
                }
                if (changed) saveLichessSettings();
                ImGui::EndPopup();
            }
        }
        // My databases: the right button chooses those it adds together (M3, board 3).
        if (i < together) {
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TR("The databases together; the right button chooses them"));
            if (ImGui::BeginPopupContextItem("##my_databases")) {
                ImGui::TextDisabled("%s", TR("Added together"));
                for (const auto& [name, index] : databases) {
                    const auto& folder = s.entries()[index].folder;
                    bool in            = !tree_left_.count(folder);
                    if (ImGui::Checkbox(fmt::format("{}##joined_{}", name, folder.string()).c_str(), &in)) {
                        if (in)
                            tree_left_.erase(folder);
                        else
                            tree_left_.insert(folder);
                    }
                }
                ImGui::EndPopup();
            }
        }
    }

    // A database whose tree is being built: its progress, and Cancel.
    const auto* tree_job = s.treeJob();
    const auto* import   = s.import();
    if ((tree_job && tree_job->index < s.entries().size()) || (import && import->building)) {
        const auto index = tree_job ? tree_job->index : import->index;
        const auto total = tree_job ? tree_job->progress.total.load() : import->tree.total.load();
        const auto done  = tree_job ? tree_job->progress.done.load() : import->tree.done.load();
        ImGui::TextDisabled("%s", trf("“{}”: its opening tree being built, {} %", s.entries()[index].name,
                                      total ? static_cast<int>(100 * done / total) : 0)
                                      .c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Cancel"))) tree_job ? s.cancelTreeJob() : s.cancelImport();
    }

    // The rows of the position shown, made again when it or the source changes.
    const auto fen      = boardFen();
    const bool online   = tree_source_ >= lichess_at;
    if (online && !lichessTreeRows(tree_source_ - lichess_at, fen)) return;  // the card it needs instead
    const bool book     = tree_source_ >= books_at;
    const bool combined = tree_source_ < together;
    tree_db_ =
        !book && !combined ? static_cast<int>(databases[static_cast<std::size_t>(tree_source_ - together)].second) : -1;
    std::string counts;
    for (const auto& [name, i] : databases)
        counts += fmt::format("{}{},", tree_left_.count(s.entries()[i].folder) ? "-" : "", s.facts(i).games);
    const auto of = fmt::format("{}|{}|{}", fen, tree_source_, book ? std::string() : counts);
    if (!online && of != tree_rows_of_) {
        tree_rows_of_ = of;
        tree_rows_.clear();
        tree_summary_.clear();
        tree_row_        = -1;
        const auto* chosen_book = book && !online ? books[static_cast<std::size_t>(tree_source_ - books_at)]
                                                  : static_cast<const BookEntry*>(nullptr);
        tree_ctg_               = chosen_book && study::isCtg(chosen_book->path);
        tree_book_              = book && !tree_ctg_;  // a Polyglot book's weights; a CTG book has its games
        const auto board = boardAt(b.game, b.chess960, b.ply);
        const auto mover = board.sideToMove();
        study::ScanBoard scan;
        if (!book && scan.setFen(fen, b.chess960)) {
            // One database's answer, or those of all, added move by move (a move's code is the position's own).
            std::optional<study::TreeAnswer> answer;
            std::map<int, std::string> shares;
            for (const auto& [name, i] : databases) {
                if (!combined && static_cast<int>(i) != tree_db_) continue;
                if (combined && tree_left_.count(s.entries()[i].folder)) continue;
                const auto source = s.treeSource(i);
                const auto one =
                    source && source->tree ? source->tree->at(*source->snapshot, scan.key()) : std::nullopt;
                if (!one) continue;
                if (!answer) answer.emplace();
                answer->total.add(one->total);
                for (const auto& m : one->moves) {
                    auto at = std::find_if(answer->moves.begin(), answer->moves.end(),
                                           [&](const study::TreeMove& t) { return t.code == m.code; });
                    if (at == answer->moves.end()) at = answer->moves.insert(answer->moves.end(), {m.code, {}});
                    at->stats.add(m.stats);
                    if (combined) {
                        auto& text = shares[m.code];
                        text += (text.empty() ? "" : "\n") + fmt::format("{}: {}", name, ui::exactCount(m.stats.games));
                    }
                }
            }
            if (answer)
                std::stable_sort(answer->moves.begin(), answer->moves.end(),
                                 [](const study::TreeMove& x, const study::TreeMove& y) {
                                     if ((x.code == study::TreeMove::END) != (y.code == study::TreeMove::END))
                                         return y.code == study::TreeMove::END;
                                     return x.stats.games > y.stats.games;
                                 });
            if (answer) {
                const auto& t      = answer->total;
                const auto decided = t.white + t.draws + t.black;
                tree_summary_ = trn("{} game", "{} games", static_cast<int>(std::min<uint64_t>(t.games, 1000000000)),
                                    ui::exactCount(t.games));
                if (decided > 0)
                    tree_summary_ +=
                        trf(" · White {} % · draws {} % · Black {} %", std::lround(100.0 * t.white / decided),
                            std::lround(100.0 * t.draws / decided), std::lround(100.0 * t.black / decided));
                for (const auto& m : answer->moves) {
                    if (m.code == study::TreeMove::END) continue;
                    const auto move = study::moveOfCode(board, scan, m.code);
                    if (!move) continue;
                    TreeRow row;
                    row.move              = *move;
                    row.san               = chess::uci::moveToSan(board, *move);
                    row.games             = m.stats.games;
                    const auto& st        = m.stats;
                    const auto done       = st.white + st.draws + st.black;
                    const bool white_side = mover == chess::Color::WHITE;
                    if (done > 0) {
                        row.white = static_cast<double>(st.white) / done;
                        row.draws = static_cast<double>(st.draws) / done;
                        row.black = static_cast<double>(st.black) / done;
                        const double score =
                            (static_cast<double>(white_side ? st.white : st.black) + st.draws / 2.0) / done;
                        row.score            = static_cast<int>(std::lround(100 * score));
                        const auto own_rated = white_side ? st.white_rated : st.black_rated;
                        const auto own_sum   = white_side ? st.white_elo : st.black_elo;
                        const auto opp_rated = white_side ? st.black_rated : st.white_rated;
                        const auto opp_sum   = white_side ? st.black_elo : st.white_elo;
                        if (own_rated) row.elo = static_cast<int>(own_sum / own_rated);
                        if (opp_rated) row.perf = static_cast<int>(opp_sum / opp_rated) + ratingDifference(score);
                    }
                    row.last = static_cast<int>(st.last >> 9);
                    if (const auto share = shares.find(m.code); share != shares.end()) row.shares = share->second;
                    tree_rows_.push_back(std::move(row));
                }
            } else {
                tree_summary_ = combined ? TR("No two games of these databases reach this position.")
                                         : TR("No two games of this database reach this position.");
            }
        } else if (tree_ctg_) {
            // A CTG book (#68, mockup M10, board 3): the games behind each move, its colour and its symbol; green
            // moves first, red ones last.
            auto& opened = tree_ctg_books_[chosen_book->path];
            try {
                if (!opened) opened = std::make_shared<const study::CtgBook>(chosen_book->path);
                const auto answer = opened->at(board);
                if (!answer) {
                    tree_summary_ = TR("The book does not have this position.");
                } else {
                    for (const auto& m : answer->moves) {
                        TreeRow row;
                        row.move       = m.move;
                        row.san        = chess::uci::moveToSan(board, m.move);
                        row.book       = m.main ? 1 : m.avoid ? -1 : 0;
                        row.symbol     = static_cast<uint8_t>(m.symbol);
                        row.evaluation = static_cast<uint8_t>(m.evaluation);
                        if (m.games) {
                            const auto& st        = *m.games;
                            row.games             = st.games;
                            const auto done       = st.white + st.draws + st.black;
                            const bool white_side = mover == chess::Color::WHITE;
                            if (done > 0) {
                                row.white = static_cast<double>(st.white) / done;
                                row.draws = static_cast<double>(st.draws) / done;
                                row.black = static_cast<double>(st.black) / done;
                                const double score =
                                    (static_cast<double>(white_side ? st.white : st.black) + st.draws / 2.0) / done;
                                row.score            = static_cast<int>(std::lround(100 * score));
                                const auto own_rated = white_side ? st.white_rated : st.black_rated;
                                const auto own_sum   = white_side ? st.white_elo : st.black_elo;
                                const auto opp_rated = white_side ? st.black_rated : st.white_rated;
                                const auto opp_sum   = white_side ? st.black_elo : st.white_elo;
                                if (own_rated) row.elo = static_cast<int>(own_sum / own_rated);
                                if (opp_rated)
                                    row.perf = static_cast<int>(opp_sum / opp_rated) + ratingDifference(score);
                            }
                        }
                        tree_rows_.push_back(std::move(row));
                    }
                    std::stable_sort(tree_rows_.begin(), tree_rows_.end(), [](const TreeRow& x, const TreeRow& y) {
                        if (x.book != y.book) return x.book > y.book;
                        return x.games > y.games;
                    });
                    tree_summary_ =
                        trf("{} positions in the book · {} here", ui::exactCount(opened->positions()),
                            trn("{} move", "{} moves", static_cast<int>(answer->moves.size()), answer->moves.size()));
                }
            } catch (const std::exception& e) {
                tree_summary_ = e.what();
            }
        } else if (book) {
            const auto* entry = books[static_cast<std::size_t>(tree_source_ - books_at)];
            auto& opened      = tree_books_[entry->path];
            try {
                if (!opened) opened = std::make_shared<tour::PolyglotBook>(std::filesystem::path(entry->path));
                auto copy          = board;
                const auto choices = opened->moves(copy);
                uint64_t total     = 0;
                for (const auto& c : choices) total += c.weight;
                for (const auto& c : choices) {
                    TreeRow row;
                    row.move  = c.move;
                    row.san   = chess::uci::moveToSan(board, c.move);
                    row.games = total ? static_cast<uint64_t>(std::lround(1000.0 * c.weight / total)) : 0;
                    tree_rows_.push_back(std::move(row));
                }
                tree_summary_ = choices.empty() ? std::string(TR("The book has no move here."))
                                                : trn("{} move in the book", "{} moves in the book",
                                                      static_cast<int>(choices.size()), choices.size());
            } catch (const std::exception& e) {
                tree_summary_ = e.what();
            }
        }
    }
    ImGui::PushTextWrapPos();  // a message from Lichess can be long
    ImGui::TextUnformatted(tree_summary_.c_str());
    ImGui::PopTextWrapPos();
    if (tree_db_ >= 0) drawTreeNovelty(static_cast<std::size_t>(tree_db_));
    // Lichess's players: each move's games month by month, when asked for (#56).
    const bool lichess_players = online && tree_source_ - lichess_at == 1;
    if (lichess_players) {
        if (ImGui::Checkbox(TR("Each move month by month"), &lichess_.explorer_months)) {
            tree_rows_of_.clear();  // the rows made again, with their months' keys
            saveLichessSettings();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("Asks Lichess, for each move listed, how many games played it month by month: "
                                       "a request a move, its position sent with the filters chosen."));
        pollExplorerMonths();
    }

    // The star of the active repertoire: a move of it, added or taken out by a click (any side to move).
    const auto path  = boardPath();
    auto& reps       = repertoires();
    const int active = reps.active();
    const auto mover = boardAt(b.game, b.chess960, b.ply).sideToMove();
    const auto now   = ImGui::GetTime();
    std::optional<chess::Move> star;

    // The keys (M3): Up and Down choose a move, Enter plays it, * stars it in the active repertoire.
    std::optional<chess::Move> play, hovered;
    const auto& io  = ImGui::GetIO();
    const bool keys = !io.WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && !io.KeyCtrl &&
                      !io.KeyAlt && !tree_rows_.empty();
    if (keys) {
        const int last = static_cast<int>(tree_rows_.size()) - 1;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) tree_row_ = std::min(tree_row_ + 1, last), tree_scroll_ = true;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) tree_row_ = std::max(tree_row_ - 1, -1), tree_scroll_ = true;
        if (tree_row_ > last) tree_row_ = last;
        if (tree_row_ >= 0) {
            if (!linkCardTakesKeys() &&
                (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
                play = tree_rows_[static_cast<std::size_t>(tree_row_)].move;
            for (const auto c : io.InputQueueCharacters)
                if (c == '*' && path) star = tree_rows_[static_cast<std::size_t>(tree_row_)].move;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) tree_row_ = -1;
        }
    }

    // The moves: a click plays one. Lichess's evaluation of the position after each, when they are kept offline
    // (#58), beside it.
    const auto here      = boardAt(b.game, b.chess960, b.ply);
    const bool evals     = offline_evals_ && offline_evals_->lookup() && !b.chess960 && !aidsLocked();
    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    const int columns    = (tree_book_ ? 3 : 9) + (evals ? 1 : 0);
    // Room under the table for the star's Undo and the line of keys.
    const float games_h = online && !tree_games_.empty()
                              ? ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(tree_games_.size() + 2)
                              : 0.0f;  // the games the explorer names, under the moves (#56)
    const float toast_h = (star_undo_ && now - star_undo_->at < 8 ? ImGui::GetFrameHeightWithSpacing() : 0.0f) +
                          ImGui::GetTextLineHeightWithSpacing() * 2 + games_h;
    if (!tree_rows_.empty() && ImGui::BeginTable("##tree_rows", columns, flags, ImVec2(0, -toast_h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableSetupColumn(TR("Move"));
        if (evals) ImGui::TableSetupColumn(TR("Eval."));
        if (tree_book_) {
            ImGui::TableSetupColumn(TR("Weight"), ImGuiTableColumnFlags_WidthStretch);
        } else {
            ImGui::TableSetupColumn(TR("Games"));
            ImGui::TableSetupColumn(TR("White · draws · Black"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(TR("Score"));
            ImGui::TableSetupColumn(TR("Avg. Elo"));
            ImGui::TableSetupColumn(TR("Perf."));
            ImGui::TableSetupColumn(tree_ctg_ ? TR("Book") : TR("Last"));
            ImGui::TableSetupColumn(lichess_players && lichess_.explorer_months ? TR("By month") : "");
        }
        ImGui::TableHeadersRow();
        for (std::size_t r = 0; r < tree_rows_.size(); ++r) {
            const auto& row = tree_rows_[r];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(r));
            // A CTG book's move not to play in a tournament: in the list, greyed (M10, board 3).
            const bool greyed = row.book < 0;
            if (greyed) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            // The move, the whole row a target: a click plays it, the mouse over it shows it as an arrow.
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Selectable(row.san.c_str(), tree_row_ == static_cast<int>(r),
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                play = row.move;
            if (ImGui::IsItemHovered()) hovered = row.move;
            if (tree_row_ == static_cast<int>(r) && tree_scroll_) {
                ImGui::SetScrollHereY();
                tree_scroll_ = false;
            }
            ImGui::TableSetColumnIndex(0);
            if (path) {
                const bool in = active >= 0 && reps.has(static_cast<std::size_t>(active), *path, row.move);
                const auto at = ImGui::GetCursorScreenPos();
                const float h = ImGui::GetTextLineHeight();
                if (ImGui::InvisibleButton("##repertoire_star", ImVec2(h, h))) star = row.move;
                ui::drawIcon(ImGui::GetWindowDrawList(), ui::Icon::Star, ImVec2(at.x + h * 0.5f, at.y + h * 0.5f), h,
                             in ? ui::WARN : (ImGui::IsItemHovered() ? ImGui::GetColorU32(ImGuiCol_Text) : ui::MUTED));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", in ? TR("In the active repertoire: a click takes it out")
                                               : TR("A click adds it to the active repertoire"));
            }
            ImGui::PopID();
            if (evals) {
                ImGui::TableSetColumnIndex(2);
                auto after = here;
                after.makeMove(row.move);
                if (const auto e = offlineEval(after)) {
                    ImGui::TextUnformatted(e->mate ? fmt::format("#{}", e->mate).c_str()
                                                   : fmt::format("{:+.2f}", e->cp / 100.0).c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", trf("Lichess's evaluation after {}, depth {}, kept offline. Scores "
                                                    "are White's.",
                                                    row.san, e->depth)
                                                    .c_str());
                }
            }
            ImGui::TableSetColumnIndex(evals ? 3 : 2);
            if (tree_book_) {
                ImGui::ProgressBar(static_cast<float>(row.games) / 1000.0f, ImVec2(-1, 0),
                                   fmt::format("{} %", std::lround(row.games / 10.0)).c_str());
                if (greyed) ImGui::PopStyleColor();
                continue;
            }
            ImGui::TextUnformatted(ui::exactCount(row.games).c_str());
            if (!row.shares.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", row.shares.c_str());
            ImGui::TableNextColumn();
            // White's share light, the draws grey, Black's dark: told apart by their lightness.
            {
                const auto pos = ImGui::GetCursorScreenPos();
                const float w  = std::max(px(60), ImGui::GetContentRegionAvail().x);
                const float h  = ImGui::GetTextLineHeight() * 0.8f;
                auto* dl       = ImGui::GetWindowDrawList();
                const float y  = pos.y + ImGui::GetTextLineHeight() * 0.1f;
                const float x1 = pos.x + w * static_cast<float>(row.white);
                const float x2 = x1 + w * static_cast<float>(row.draws);
                dl->AddRectFilled(ImVec2(pos.x, y), ImVec2(x1, y + h), IM_COL32(246, 247, 249, 255));
                dl->AddRectFilled(ImVec2(x1, y), ImVec2(x2, y + h), IM_COL32(154, 163, 176, 255));
                dl->AddRectFilled(ImVec2(x2, y), ImVec2(pos.x + w, y + h), IM_COL32(43, 48, 56, 255));
                dl->AddRect(ImVec2(pos.x, y), ImVec2(pos.x + w, y + h), IM_COL32(201, 206, 214, 255));
                ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", trf("White {} %, draws {} %, Black {} %", std::lround(100 * row.white),
                                                std::lround(100 * row.draws), std::lround(100 * row.black))
                                                .c_str());
            }
            ImGui::TableNextColumn();
            if (row.score >= 0) ImGui::Text("%d %%", row.score);
            ImGui::TableNextColumn();
            if (row.elo > 0) ImGui::Text("%d", row.elo);
            ImGui::TableNextColumn();
            if (row.perf > 0) ImGui::Text("%d", row.perf);
            ImGui::TableNextColumn();
            if (tree_ctg_) {
                drawTreeBookMark(row);
            } else if (row.last > 0) {
                ImGui::TextDisabled("%d", row.last);
            }
            ImGui::TableNextColumn();
            if (!row.months.empty()) drawTreeMonths(row.months);
            if (greyed) ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }
    if (online) drawTreeGames();
    if (star && path) {
        const auto index = active >= 0 ? static_cast<std::size_t>(active) : reps.activeOrNew(mover);
        auto before      = reps.entries()[index].lines;
        const auto board = boardAt(b.game, b.chess960, b.ply);
        const auto san   = chess::uci::moveToSan(board, *star);
        const bool added = reps.toggle(index, *path, *star);
        star_undo_       = StarUndo{
            index, std::move(before),
            trf(added ? TR("{} added to “{}”") : TR("{} taken out of “{}”"), san, reps.entries()[index].name), now};
    }
    if (star_undo_ && now - star_undo_->at < 8) {
        ImGui::TextUnformatted(star_undo_->said.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Undo"))) {
            repertoires().restore(star_undo_->index, std::move(star_undo_->lines));
            star_undo_.reset();
        }
    }
    // The keys, said under the moves (M3, board 1).
    if (!tree_rows_.empty()) {
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", TR("↑ ↓ choose a move · Enter plays it · * marks it in the repertoire · Alt+3 this "
                                     "tab"));
        ImGui::PopTextWrapPos();
    }
    // The arrow on the board: the move under the mouse, else the one chosen.
    const auto shown = hovered ? hovered
                       : tree_row_ >= 0 && tree_row_ < static_cast<int>(tree_rows_.size())
                           ? std::optional(tree_rows_[static_cast<std::size_t>(tree_row_)].move)
                           : std::nullopt;
    tree_arrow_.reset();
    if (shown) tree_arrow_ = TreeArrow{*shown, boardAt(b.game, b.chess960, b.ply).hash()};
    if (play) {
        tree_row_ = -1;
        boardPlay(*play);
    }
}

void App::drawTreeBookMark(const TreeRow& row) {
    // Its colour, then its symbol and the evaluation after it: "green · !", "red", "?!", "±".
    std::string text;
    if (row.book > 0) text = TR("green");
    if (row.book < 0) text = TR("red");
    for (const uint8_t nag : {row.symbol, row.evaluation})
        if (nag) text += (text.empty() ? "" : " · ") + nagText(nag);
    if (text.empty()) return;
    const ImU32 tone = row.book > 0 ? ui::GOOD : row.book < 0 ? ui::BAD : 0;
    if (tone) ImGui::PushStyleColor(ImGuiCol_Text, tone);
    ImGui::TextUnformatted(text.c_str());
    if (tone) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && row.book != 0)
        ImGui::SetTooltip("%s", row.book > 0 ? TR("The book's main move") : TR("Not to play in a tournament"));
}

void App::drawTreeMonths(const std::string& key) {
    const auto known = explorer_answers_.find(key);
    if (known == explorer_answers_.end() || !known->second.value) {
        ImGui::TextDisabled("%s", known == explorer_answers_.end() ? "…" : "–");
        return;
    }
    const auto& history = known->second.value->history;
    // Its last two years, the month under way left out, as a line; where it goes beside it.
    const std::size_t end  = history.empty() ? 0 : history.size() - 1;
    const std::size_t from = end > 24 ? end - 24 : 0;
    std::vector<float> values;
    for (std::size_t i = from; i < end; ++i) values.push_back(static_cast<float>(history[i].games));
    const auto trend = lichess::trendOf(history);
    const float w = px(64), h = ImGui::GetTextLineHeight();
    const auto at    = ImGui::GetCursorScreenPos();
    auto* dl         = ImGui::GetWindowDrawList();
    const float most = values.empty() ? 0.f : *std::max_element(values.begin(), values.end());
    const ImU32 tone = trend == lichess::Trend::Rising    ? ui::GOOD
                       : trend == lichess::Trend::Falling ? ui::BAD
                                                          : ui::MUTED;
    for (std::size_t i = 1; i < values.size() && most > 0; ++i) {
        const auto point = [&](std::size_t k) {
            return ImVec2(at.x + w * static_cast<float>(k) / static_cast<float>(values.size() - 1),
                          at.y + h - 2 - (h - 4) * values[k] / most);
        };
        dl->AddLine(point(i - 1), point(i), tone, px(1.5f));
    }
    ImGui::Dummy(ImVec2(w, h));
    if (ImGui::IsItemHovered() && end >= 12) {
        uint64_t recent = 0, before = 0;
        for (std::size_t i = end - 6; i < end; ++i) recent += history[i].games;
        for (std::size_t i = end - 12; i < end - 6; ++i) before += history[i].games;
        const char* says = trend == lichess::Trend::Rising    ? TR("more played lately")
                           : trend == lichess::Trend::Falling ? TR("less played lately")
                           : trend == lichess::Trend::Steady  ? TR("played as much lately")
                                                              : TR("too few games to tell");
        ImGui::SetTooltip(
            "%s", trf("{}: {} games in the last six months, {} in the six before ({} to {})", says,
                      ui::exactCount(recent), ui::exactCount(before), history[from].month, history[end - 1].month)
                      .c_str());
    }
}

void App::drawTreeNovelty(std::size_t index) {
    // The line of the game shown, from the standard start; the novelty only for a database with its tree.
    auto& b         = board_;
    const auto path = boardPath();
    const auto src  = shelf().treeSource(index);
    if (!path || path->empty() || !src || !src->tree) return;
    const auto of =
        fmt::format("{}|{}|{}", src->name, src->snapshot->generation(), boardFen()) + fmt::format("|{}", b.ply);
    if (of != tree_novelty_.of) {
        tree_novelty_    = NoveltyJob{};
        tree_novelty_.of = of;
        study::ScanBoard scan;
        if (!scan.setFen(chess::constants::STARTPOS, false)) return;
        const auto start = scan;
        std::vector<int> codes;
        chess::Board board;
        for (const auto& m : *path) {
            const auto move = scan.parseSan(chess::uci::moveToSan(board, m));
            if (!move) return;
            codes.push_back(scan.encode(*move));
            scan.play(*move);
            board.makeMove<false>(m);
        }
        tree_novelty_.ply = src->tree->novelty(*src->snapshot, start, codes);
        if (tree_novelty_.ply && *tree_novelty_.ply <= path->size()) {
            // The position after it: no game of the database reaches it, unless a single one by other moves.
            study::ScanBoard after;
            if (after.setFen(chess::constants::STARTPOS, false)) {
                chess::Board at;
                for (uint32_t i = 0; i < *tree_novelty_.ply; ++i) at.makeMove<false>((*path)[i]);
                if (after.setFen(at.getFen(), false))
                    tree_novelty_.search = std::make_unique<study::Search>(src->snapshot, study::PositionQuery{after});
            }
        }
    }
    auto& n = tree_novelty_;
    if (!n.ply || *n.ply > path->size()) return;
    if (n.search && n.search->done()) {
        const auto hits = n.search->hits();
        n.found         = hits.size();
        n.confirmed     = hits.empty();
        n.search.reset();
    }
    const auto label = boardMoveLabel(static_cast<int>(*n.ply));
    const auto move  = (*n.ply + 1) / 2;
    ImGui::PushTextWrapPos();
    if (n.search) {
        ImGui::TextDisabled("%s", trf("The game's line leaves “{}” at move {}: looking whether a game reached its "
                                      "position…",
                                      src->name, move)
                                      .c_str());
    } else if (n.confirmed) {
        ImGui::TextUnformatted(trf("The game's line leaves “{}” at move {}:", src->name, move).c_str());
        ImGui::SameLine(0, px(4));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", trf("{} is a novelty", label).c_str());
        ImGui::TextDisabled("%s", TR("No game of the database reached this position (the position search says so)."));
    } else {
        const auto text = n.found == 1 ? trf("After {}, a single game of “{}” reaches the position, by other moves.",
                                             label, src->name)
                                       : trn("After {}, {} game of “{}” reaches the position, by other moves.",
                                             "After {}, {} games of “{}” reach the position, by other moves.", n.found,
                                             label, ui::exactCount(n.found), src->name);
        ImGui::TextDisabled("%s", text.c_str());
    }
    ImGui::PopTextWrapPos();
    if (static_cast<int>(*n.ply) < b.ply || n.confirmed) {
        if (ImGui::SmallButton(TR("See the last known position"))) showBoardLine(b.line, static_cast<int>(*n.ply) - 1);
    }
}

void App::drawBoardReferences() {
    auto& s = shelf();
    // The databases, their trees when they cover them: the strongest games through the position.
    std::vector<DatabaseShelf::TreeSource> sources;
    for (std::size_t i = 0; i < s.entries().size(); ++i)
        if (!s.entries()[i].source)
            if (auto source = s.treeSource(i)) sources.push_back(std::move(*source));
    if (sources.empty()) {
        ImGui::TextDisabled("%s", TR("No database yet: the reference games come from the Databases page's."));
        return;
    }
    const auto fen = boardFen();
    std::string of = fen;
    for (const auto& src : sources) of += fmt::format("|{}", src.snapshot->generation());
    if (of != references_of_ && (!references_job_ || references_job_->of != of)) {
        ReferencesJob job;
        job.of      = of;
        job.sources = sources;
        std::vector<commentary::ReferenceBase> bases;
        for (const auto& src : sources) bases.push_back({src.name, src.snapshot, src.tree});
        job.done        = std::async(std::launch::async, [fen, bases = std::move(bases)] {
            const auto position = commentary::Position::fromFen(fen);
            return position ? commentary::references(*position, bases, 12) : commentary::References{};
        });
        references_job_ = std::move(job);
    }
    if (references_job_ && references_job_->done.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        references_         = references_job_->done.get();
        references_sources_ = std::move(references_job_->sources);
        references_of_      = references_job_->of;
        references_job_.reset();
    }
    if (references_job_ && references_of_ != of) {
        ImGui::TextDisabled("%s", TR("Looking…"));
        return;
    }
    const auto& t = references_.total;
    ImGui::TextUnformatted(trn("{} game reaches this position", "{} games reach this position",
                               static_cast<int>(std::min<uint64_t>(t.games, 1000000000)), ui::exactCount(t.games))
                               .c_str());
    ImGui::TextDisabled("%s",
                        TR("The strongest first (the players' mean rating), then the newest. A click opens one."));
    for (std::size_t i = 0; i < references_.games.size(); ++i) {
        const auto& g = references_.games[i];
        ImGui::PushID(static_cast<int>(i));
        const auto result = g.result == '1' ? "1-0" : g.result == '0' ? "0-1" : g.result == '=' ? "½-½" : "*";
        auto line         = fmt::format("{} – {}", g.white, g.black);
        if (g.white_elo || g.black_elo) line += fmt::format("  ({} · {})", g.white_elo, g.black_elo);
        if (ImGui::Selectable(fmt::format("{}##ref", line).c_str())) {
            if (g.base < references_sources_.size()) {
                const auto& snap = references_sources_[g.base].snapshot;
                readOnBoard(study::importedPgn(snap->game(g.id)), fmt::format("{} – {}", g.white, g.black));
            }
        }
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionAvail().x * 0.55f));
        ImGui::TextDisabled("%s", fmt::format("{}{}{} · {}", g.event, g.year ? " " : "",
                                              g.year ? std::to_string(g.year) : std::string(), result)
                                      .c_str());
        ImGui::PopID();
    }
    if (!references_.listed)
        ImGui::TextDisabled("%s",
                            TR("Too many games reach it to look for the strongest: early in the opening, the Tree "
                               "tab tells more."));
    // All of them, in the game list of a database: the one the Tree tab shows, else the first; the board's
    // position added to the list's filter (M3, board 1).
    if (t.games > 0) {
        std::optional<std::size_t> base;
        if (tree_db_ >= 0 && static_cast<std::size_t>(tree_db_) < s.entries().size()) base = tree_db_;
        for (std::size_t i = 0; i < s.entries().size() && !base; ++i)
            if (!s.entries()[i].source) base = i;
        if (base &&
            ImGui::SmallButton(trf("All the games of the position in “{}” ›", s.entries()[*base].name).c_str())) {
            db_selected_        = static_cast<int>(*base);
            db_search_          = SearchFields{};
            db_search_.position = true;
            db_filter_.fill(0);
            db_search_open_ = true;
            search_tab_     = 1;
            db_list_        = true;
            db_row_         = 0;
            page_           = Page::Databases;
        }
    }
}

}  // namespace fcg
