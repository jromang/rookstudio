// The Board page's functions of RookStudio's own, out of view_board.cpp so that it stays close to fastchess-gui's and
// its releases merge without conflicts (#108): a move played as a variation, in place of the moves after or as asked,
// the notation's symbols, its changes undone and made again, its marks and comments, the keys of the moves, the field
// a move is typed in.

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>  // ImTextCharToUtf8

#include <fmt/include/fmt/format.h>

#include "app.hpp"
#include "board_text.hpp"
#include "i18n.hpp"
#include "notation_view.hpp"
#include "piece_letters.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

}  // namespace

void App::boardPlayAsked(chess::Move move) {
    if (boardSessionMove(move)) return;               // the answer to a training session's question (#76, #80–#82)
    if (visit_) return visitAnswer(move);             // the answer to the guided visit's question (#94)
    if (onlineShown()) return onlineMoveAsked(move);  // a game on Lichess: sent, waiting for Enter or its turn (#84)
    if (botShown()) {                                 // the Lichess bot's game: its engine plays it (#85)
        boardToast(TR("The bot plays this game: its moves are its engine's"));
        return;
    }
    auto& b = board_;
    // In a game the user plays, a move typed is theirs, at their turn only (#83); a game kept waits for Resume.
    if (b.humanGame()) {
        if (b.humanTurn()) boardMove(move);
        return;
    }
    if (engine_game_ && engine_game_->waiting) {
        boardToast(TR("The game waits: Resume it first (Enter)"));
        return;
    }
    if (!chessBoard()) return boardPlay(move);
    // Only a move that would start a variation is asked about: the line's next move, or one a variation starts with,
    // is played as it is.
    auto trial            = b.tree;
    const auto variations = trial.variations.size();
    (void)playInTree(trial, b.chess960, b.line, b.ply, move);
    if (trial.variations.size() == variations) return boardPlay(move);
    const auto& io = ImGui::GetIO();
    if (io.KeyShift) return boardPlayAs(move, NewMove::Replace);
    if (io.KeyCtrl || b.settings.ask_new_move) {
        new_move_       = move;
        new_move_at_    = io.MousePos;
        new_move_frame_ = ImGui::GetFrameCount();
        return;
    }
    boardPlay(move);
}

void App::boardPlayAs(chess::Move move, NewMove as) {
    auto& b = board_;
    if (as == NewMove::Variation) return boardPlay(move);
    auto before = boardSnapshot();
    int line = b.line, ply = b.ply;
    if (as == NewMove::Replace) {
        // The moves after the position taken away in the line that holds them, then the move played there.
        std::tie(line, ply) = truncateAt(b.tree, ownerOf(b.tree, b.line, b.ply), b.ply + 1);
        std::tie(line, ply) = playInTree(b.tree, b.chess960, line, ply, move);
        boardToast(TR("The moves after replaced · Ctrl+Z gives them back"));
    } else {
        std::tie(line, ply) = playInTree(b.tree, b.chess960, line, ply, move);
        line                = promoteVariation(b.tree, line);
        boardToast(TR("New main line · the moves it replaces became a variation · Ctrl+Z undoes it"));
    }
    b.game = lineOf(b.tree, line);
    showBoardLine(line, ply);
    boardChanged(std::move(before));
}

void App::drawNewMoveQuestion() {
    constexpr const char* kId = "##new_move";
    if (!new_move_) return;
    // Opened the frame after the move: the click that made it does not close it.
    if (!ImGui::IsPopupOpen(kId)) {
        if (ImGui::GetFrameCount() <= new_move_frame_ + 1) return;
        ImGui::OpenPopup(kId);
        ImGui::SetNextWindowPos(new_move_at_, ImGuiCond_Appearing);
    }
    if (!ImGui::BeginPopup(kId)) {  // a click elsewhere: the move is not played
        new_move_.reset();
        return;
    }
    const auto& b    = board_;
    const auto board = boardAt(b.game, b.chess960, b.ply);
    const auto san   = localSan(chess::uci::moveToSan(board, *new_move_));
    std::optional<NewMove> as;
    ImGui::TextDisabled("%s", trf("{}: the game went another way here", san).c_str());
    struct Choice {
        NewMove as;
        const char* label;
        const char* key;
        ImGuiKey imgui_key;
    };
    static constexpr Choice choices[] = {{NewMove::Variation, N_("A variation"), "V", ImGuiKey_V},
                                         {NewMove::MainLine, N_("A new main line"), "M", ImGuiKey_M},
                                         {NewMove::Replace, N_("Replace the moves after"), "R", ImGuiKey_R}};
    for (const auto& c : choices) {
        if (ImGui::Selectable(TR(c.label), c.as == NewMove::Variation, 0, ImVec2(px(240), 0))) as = c.as;
        ImGui::SameLine(px(250));
        ImGui::TextDisabled("%s", c.key);
        if (ImGui::IsKeyPressed(c.imgui_key, false)) as = c.as;
    }
    ImGui::TextDisabled("%s", TR("Enter: a variation · Esc: not played"));
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
        as = NewMove::Variation;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        new_move_.reset();
        ImGui::CloseCurrentPopup();
    }
    if (as) {
        const auto move = *new_move_;
        new_move_.reset();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        boardPlayAs(move, *as);
        return;
    }
    ImGui::EndPopup();
}

namespace {

bool moveNag(uint8_t nag) { return nag >= 1 && nag <= 9; }

}  // namespace

std::string nagText(uint8_t nag) {
    switch (nag) {
        case 1:
            return "!";
        case 2:
            return "?";
        case 3:
            return "!!";
        case 4:
            return "??";
        case 5:
            return "!?";
        case 6:
            return "?!";
        case 7:
        case 8:
            return "□";
        case 10:
            return "=";
        case 13:
            return "∞";
        case 14:  // the font may not have it
            return ImGui::GetFont()->IsGlyphInFont(0x2A72) ? "⩲" : "+=";
        case 15:
            return ImGui::GetFont()->IsGlyphInFont(0x2A71) ? "⩱" : "=+";
        case 16:
            return "±";
        case 17:
            return "∓";
        case 18:
            return "+−";
        case 19:
            return "−+";
        case 22:
        case 23:
            return "⨀";
        case 146:
            return "N";
        default:
            return fmt::format("${}", nag);
    }
}

ImU32 nagColour(uint8_t nag) {
    switch (nag) {
        case 1:
            return IM_COL32(40, 150, 70, 255);
        case 2:
            return IM_COL32(230, 120, 30, 255);
        case 3:
            return IM_COL32(26, 165, 160, 255);
        case 4:
            return IM_COL32(210, 50, 50, 255);
        case 5:
            return IM_COL32(80, 130, 215, 255);
        case 6:
            return IM_COL32(200, 155, 30, 255);
        default:
            return 0;
    }
}

BoardPage::Snapshot App::boardSnapshot() const { return fcg::boardSnapshot(board_); }

void App::boardChanged(BoardPage::Snapshot before) { fcg::boardChanged(board_, std::move(before)); }

void App::boardUndo(bool redo) {
    if (!fcg::boardUndo(board_, redo)) return;
    boardToast(redo ? TR("Change made again") : TR("Change undone · Ctrl+Y makes it again"));
}

MoveNotes* App::boardNotes() {
    auto& b = board_;
    return b.ply == 0 ? &b.start_notes : notesAt(b.tree, b.line, b.ply);
}

void App::boardClearMarks() {
    auto& b          = board_;
    MoveNotes* notes = boardNotes();
    if (!notes || (notes->arrows.empty() && notes->squares.empty())) return;
    auto before = boardSnapshot();
    notes->arrows.clear();
    notes->squares.clear();
    boardChanged(std::move(before));
    b.game = lineOf(b.tree, b.line);
    boardToast(TR("Marks cleared · Ctrl+Z brings them back"));
}

void App::boardToast(std::string text) {
    board_.toast      = std::move(text);
    board_.toast_time = ImGui::GetTime();
}

void App::boardComment(bool before, bool assistant) {
    auto& b = board_;
    if (b.ply <= 0) return;
    const MoveNotes* notes = notesAt(b.tree, b.line, b.ply);
    if (!notes) return;
    BoardPage::CommentEdit e;
    e.line           = ownerOf(b.tree, b.line, b.ply - 1);
    e.ply            = b.ply;
    e.before         = before;
    e.assistant      = assistant && !before;
    const auto& text = e.before ? notes->before : e.assistant ? notes->assistant : notes->after;
    std::snprintf(e.text.data(), e.text.size(), "%s", text.c_str());
    b.comment_edit = e;
}

void App::boardCommentDone() {
    auto& b = board_;
    if (!b.comment_edit) return;
    const auto e = *b.comment_edit;
    b.comment_edit.reset();
    auto before = boardSnapshot();
    if (!editComment(b.tree, e, e.text.data())) return;
    boardChanged(std::move(before));
    b.game = lineOf(b.tree, b.line);
}

void App::boardSetNag(uint8_t nag) {
    auto& b = board_;
    if (b.ply <= 0) return;
    auto before      = boardSnapshot();
    MoveNotes* notes = notesAt(b.tree, b.line, b.ply);
    if (!notes) return;
    toggleNag(notes->nags, nag);
    boardChanged(std::move(before));
    b.game = lineOf(b.tree, b.line);
}

void App::boardTypeNag(char sign) {
    auto& b = board_;
    if (b.ply <= 0) return;
    const double now                                              = ImGui::GetTime();
    const bool pair                                               = b.nag_typed.size() == 1 && now - b.nag_time < 0.8;
    b.nag_typed                                                   = pair ? b.nag_typed + sign : std::string(1, sign);
    b.nag_time                                                    = now;
    static constexpr std::pair<std::string_view, uint8_t> signs[] = {{"!", 1},  {"?", 2},  {"!!", 3},
                                                                     {"??", 4}, {"!?", 5}, {"?!", 6}};
    uint8_t nag                                                   = 0;
    for (const auto& [text, n] : signs)
        if (text == b.nag_typed) nag = n;
    if (nag == 0) return;
    auto before      = boardSnapshot();
    MoveNotes* notes = notesAt(b.tree, b.line, b.ply);
    if (!notes) return;
    if (pair) {
        // The second sign of a pair: the pair's symbol for the first's, in the same change.
        std::erase_if(notes->nags, moveNag);
        toggleNag(notes->nags, nag);
    } else {
        toggleNag(notes->nags, nag);
        boardChanged(std::move(before));
    }
    b.game = lineOf(b.tree, b.line);
}

void App::boardEditKeys() {
    auto& b        = board_;
    const auto& io = ImGui::GetIO();
    if (io.WantTextInput) return;  // a field has the keys: the move typed, a comment
    // A game against an engine (#83): its keys while it lasts (Ctrl+Z takes back, Alt+H a hint, the moves typed);
    // Enter reviews it once it is over.
    if (engineGameKeys()) return;
    // A game on Lichess (#84): Enter sends the move waiting, Escape takes it back; the moves typed.
    if (onlineGameKeys()) return;
    // Enter writes a comment, unless a move of the tree is chosen (Up, Down): it plays it then.
    const bool tree_chosen = board_side_tab_ == 2 && tree_row_ >= 0;
    if (b.ply > 0 && !io.KeyCtrl && !io.KeyAlt && !tree_chosen && !linkCardTakesKeys() &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
        boardComment(io.KeyShift);
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) boardUndo(false);
    if (chessBoard() && ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_C)) commentBoardMove();  // M5, board 3
    if (chessBoard() && ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_Q)) whyNotHere();        // #94
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) ||
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
        boardUndo(true);
    if (b.ply > 0 && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        auto before            = boardSnapshot();
        const auto [line, ply] = truncateAt(b.tree, b.line, b.ply);
        boardChanged(std::move(before));
        b.line = line;
        b.game = lineOf(b.tree, line);
        b.ply  = std::clamp(ply, 0, static_cast<int>(b.game.moves.size()));
        b.selected.reset();
        boardToast(TR("Moves deleted · Ctrl+Z brings them back"));
    }
    if (const int owner = b.ply > 0 ? ownerOf(b.tree, b.line, b.ply - 1) : -1;
        owner >= 0 && ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_UpArrow)) {
        auto before    = boardSnapshot();
        const int line = promoteVariation(b.tree, owner);
        boardChanged(std::move(before));
        b.line = line;
        b.game = lineOf(b.tree, line);
        b.ply  = std::clamp(b.ply, 0, static_cast<int>(b.game.moves.size()));
        boardToast(line < 0 ? TR("The main line now · Ctrl+Z undoes it") : TR("Variation moved up · Ctrl+Z undoes it"));
    }
    for (const ImWchar c : io.InputQueueCharacters)
        if (c == '!' || c == '?') boardTypeNag(static_cast<char>(c));
    boardMoveKeys();
}

void App::boardMoveKeys() {
    auto& b        = board_;
    const auto& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    for (const ImWchar c : io.InputQueueCharacters) {
        if (!b.move_typed && !io.KeyCtrl && !io.KeyAlt && beginsMove(c, i18n::language())) {
            // Its letter as UTF-8: the Russian letters are two bytes (#66).
            b.move_typed.emplace();
            ImTextCharToUtf8(b.move_typed->data(), c);
            b.move_focus = true;
            b.move_error.clear();
        }
    }
}

void App::drawMoveField() {
    auto& b = board_;
    if (!b.move_typed) return;
    const auto language = i18n::language();
    // During the opponent's turn of a live game on Lichess, the user's moves, made in advance (#84).
    const auto board = boardForPicking(boardAt(b.game, b.chess960, b.ply));
    // Focused, the field would select its text, which the next letter would replace: the cursor after it instead. The
    // arrows choose among the moves offered.
    struct Keys {
        bool at_end = false;
        int step    = 0;
    };
    static Keys keys;
    keys.step = 0;
    if (b.move_focus) {
        ImGui::SetKeyboardFocusHere();
        b.move_focus = false;
        keys.at_end  = true;
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    const bool enter = ImGui::InputTextWithHint(
        "##move_typed", TR("A move"), b.move_typed->data(), b.move_typed->size(),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_CallbackHistory,
        [](ImGuiInputTextCallbackData* data) {
            auto* k = static_cast<Keys*>(data->UserData);
            if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
                k->step += data->EventKey == ImGuiKey_UpArrow ? -1 : 1;
            } else if (k->at_end) {
                data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen;
                k->at_end                                                   = false;
            }
            return 0;
        },
        &keys);
    if (ImGui::IsItemDeactivated() && !enter && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        b.move_typed.reset();
        return;
    }
    // The legal moves it may be, as the notation writes them: in the language's letters first, then those English
    // letters find (#66).
    const std::string typed = b.move_typed->data();
    const auto matches      = typedMoves(board, typed, language);
    if (typed != b.move_choice_of) {
        b.move_choice    = 0;
        b.move_choice_of = typed;
    }
    if (!matches.empty())
        b.move_choice = std::clamp(b.move_choice + keys.step, 0, static_cast<int>(matches.size()) - 1);
    std::optional<chess::Move> play;
    if (enter) {
        // The move chosen with the arrows; else the text read as a whole move, in the language's letters, then in
        // English ones (a piece named on two files where one would do); else the first move offered.
        if (b.move_choice > 0 && b.move_choice < static_cast<int>(matches.size()))
            play = matches[static_cast<std::size_t>(b.move_choice)].move;
        for (const auto as : {language, i18n::Language::English}) {
            if (play) break;
            auto san = englishTyped(typed, as);
            std::erase_if(san, [](char c) { return c == '!' || c == '?'; });  // a symbol typed after it
            try {
                const auto m = chess::uci::parseSan(board, san);
                chess::Movelist legal;
                chess::movegen::legalmoves(legal, board);
                if (m != chess::Move::NO_MOVE && std::find(legal.begin(), legal.end(), m) != legal.end()) play = m;
            } catch (...) {
            }
        }
        if (!play && !matches.empty()) play = matches.front().move;
        if (!play) {
            b.move_error = TR("Not a legal move here");
            b.move_focus = true;
        }
    }
    ImGui::SameLine();
    if (!b.move_error.empty()) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::BAD), "%s", b.move_error.c_str());
    } else if (matches.empty()) {
        ImGui::TextDisabled("%s", TR("No legal move begins so"));
    } else {
        // The moves offered, the one Enter plays in the accent; a click plays one. Those English letters find where
        // the language reads another piece, after their piece's name: "rook (English)".
        static const char* const english_names[] = {N_("king (English)"), N_("queen (English)"), N_("rook (English)"),
                                                    N_("bishop (English)"), N_("knight (English)")};

        const auto& style = ImGui::GetStyle();
        bool marked       = false;
        for (std::size_t i = 0; i < matches.size() && i < 8; ++i) {
            const auto& m = matches[i];
            if (m.english && !marked) {
                marked           = true;
                const int index  = m.english == chess::PieceType::KING     ? 0
                                   : m.english == chess::PieceType::QUEEN  ? 1
                                   : m.english == chess::PieceType::ROOK   ? 2
                                   : m.english == chess::PieceType::BISHOP ? 3
                                                                           : 4;
                const auto label = trf("· {}", TR(english_names[index]));
                ui::sameLineOrWrap(ImGui::CalcTextSize(label.c_str()).x, px(6));
                ImGui::TextDisabled("%s", label.c_str());
            }
            const float w = ImGui::CalcTextSize(m.shown.c_str()).x + style.FramePadding.x * 2;
            if (i > 0 || marked) ui::sameLineOrWrap(w, px(4));
            const bool chosen = static_cast<int>(i) == b.move_choice;
            if (chosen) {
                ImGui::PushStyleColor(ImGuiCol_Button, ui::ACCENT);
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32_WHITE);
            }
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::SmallButton(m.shown.c_str())) play = m.move;
            ImGui::PopID();
            if (chosen) ImGui::PopStyleColor(2);
        }
        if (matches.size() > 8) {
            ui::sameLineOrWrap(ImGui::CalcTextSize("…").x, px(4));
            ImGui::TextDisabled("…");
        }
        // How to choose, and the English letters that find the same moves ("Ng" for the German "Sg"), said when they
        // do: the Russian "К" finds the king's moves too, which "N" does not.
        std::string note   = TR("↑ ↓ choose, Enter plays");
        const auto english = englishTyped(typed, language);
        if (english != englishTyped(typed, i18n::Language::English)) {
            const auto raw = [](const std::vector<TypedMove>& list) {
                std::vector<uint16_t> out;
                for (const auto& m : list) out.push_back(m.move.move());
                std::sort(out.begin(), out.end());
                return out;
            };
            if (raw(typedMoves(board, english, i18n::Language::English)) == raw(matches))
                note += " · " + trf("“{}” gives the same", english);
        }
        ui::sameLineOrWrap(ImGui::CalcTextSize(note.c_str()).x, px(8));
        ImGui::TextDisabled("%s", note.c_str());
    }
    if (play) {
        b.move_typed.reset();
        boardPlayAsked(*play);
    }
}

void App::drawBoardNagBar() {
    auto& b                = board_;
    const MoveNotes* notes = b.ply > 0 && b.ply <= static_cast<int>(b.game.moves.size())
                                 ? &b.game.moves[static_cast<std::size_t>(b.ply - 1)].notes
                                 : nullptr;
    struct Sign {
        uint8_t nag;
        const char* tip;
    };
    static const Sign signs[] = {{1, N_("Good move")},
                                 {2, N_("Mistake")},
                                 {3, N_("Brilliant move")},
                                 {4, N_("Blunder")},
                                 {5, N_("Interesting move")},
                                 {6, N_("Dubious move")},
                                 {7, N_("Only move")},
                                 {146, N_("Novelty")},
                                 {10, N_("Equal position")},
                                 {13, N_("Unclear position")},
                                 {14, N_("White is slightly better")},
                                 {16, N_("White is better")},
                                 {18, N_("White is winning")},
                                 {15, N_("Black is slightly better")},
                                 {17, N_("Black is better")},
                                 {19, N_("Black is winning")}};
    ImGui::BeginDisabled(!notes);
    for (std::size_t i = 0; i < std::size(signs); ++i) {
        const auto& s = signs[i];
        const bool on = notes && std::find(notes->nags.begin(), notes->nags.end(), s.nag) != notes->nags.end();
        if (i > 0) ImGui::SameLine(0, i == 8 ? px(12) : px(2));
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ui::ACCENT);
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Button(nagText(s.nag).c_str(), ImVec2(px(30), 0))) boardSetNag(s.nag);
        ImGui::PopID();
        if (on) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", TR(s.tip));
    }
    ImGui::EndDisabled();
    drawTheoryChip();  // what the theory studies say here: the Tree tab (#77)
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", TR("Enter: a comment after the move · Shift+Enter: before it · ! and ?: symbols · Del: "
                                "the moves from here · Alt+↑: the variation up · Ctrl+Z, Ctrl+Y · another move: a "
                                "variation, with Shift it replaces the moves after, with Ctrl a choice · the right "
                                "button on the board: arrows and squares (Shift red, Alt blue, both yellow)"));
    ImGui::PopStyleColor();
    if (ImGui::GetTime() - b.toast_time < 4.0)
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", b.toast.c_str());
}

}  // namespace fcg
