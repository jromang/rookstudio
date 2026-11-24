// The guided visit of a reviewed game and "Why not?" (#94, mockup M13, board 7, point 16). The visit takes the key
// moments of the review one after the other, the position before the game's move, and asks "Before seeing why: what
// would you have played?": a move played or typed, nothing shown before the answer, Space for "I don't know". Then the
// answer judged as Guess the moves judges it (#81), the engine's move, an explanation held to the facts and what it
// rests on; Enter the next moment, Alt+Q "Why not another move?", "See the line", Escape stops. A wrong answer becomes
// a card of the day's queue, as the user's mistakes do. "Why not?" asks the Questions' Compare action (#38) with one
// key: the engine after each move, the facts of both positions, the databases, the continuation. No free conversation.

#include <algorithm>
#include <chrono>
#include <sstream>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>  // SetKeyOwner

#include <commentary/writing.hpp>

#include "app.hpp"
#include "assistant_service.hpp"
#include "board_text.hpp"
#include "explained.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "live.hpp"
#include "review_bridge.hpp"
#include "training.hpp"
#include "ui.hpp"
#include "visit.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// The engine's time a move, as Guess the moves gives it.
constexpr int64_t VISIT_SEARCH_MS = 1000;

bool frenchAssistant() { return i18n::language() == i18n::Language::French; }

// A move numbered from its position: in the interface's letters, or the assistant's (French, else English).
std::string numbered(const chess::Board& before, const std::string& uci, bool assistant) {
    const auto m = chess::uci::uciToMove(before, uci);
    if (m == chess::Move::NO_MOVE) return uci;
    const auto san   = chess::uci::moveToSan(before, m);
    const auto shown = assistant ? (frenchAssistant() ? frenchSan(san) : san) : localSan(san);
    return fmt::format("{}{}{}", before.fullMoveNumber(), before.sideToMove() == chess::Color::WHITE ? "." : "…",
                       shown);
}

AnalysisPosition positionOf(const ReplayGame& game, bool chess960, std::size_t ply, const std::string& then) {
    AnalysisPosition p;
    p.fen      = game.start_fen.empty() ? std::string(chess::constants::STARTPOS) : game.start_fen;
    p.chess960 = chess960;
    for (std::size_t i = 0; i < ply && i < game.moves.size(); ++i)
        p.moves.push_back(chess::uci::moveToUci(game.moves[i].move, chess960));
    if (!then.empty()) p.moves.push_back(then);
    return p;
}

}  // namespace

// ---------------------------------------------------------------------------
// "Why not?"

std::string App::whyNotReference(const chess::Board& before, const std::string& asked) {
    const auto& b = board_;
    // The game's move from the position, when the main line goes through it.
    for (std::size_t i = 0; i < b.tree.main.moves.size(); ++i) {
        if (boardAt(b.tree.main, b.chess960, static_cast<int>(i)).hash() != before.hash()) continue;
        const auto game = chess::uci::moveToUci(b.tree.main.moves[i].move, b.chess960);
        if (game != asked) return game;
        break;
    }
    // Else the engine's best, else its second, when its lines are of the position.
    const auto& lines = b.analyser->lines();
    if (b.analyser->active() && b.analyser->position().fen == before.getFen())
        for (const auto& l : lines)
            if (!l.pv.empty() && l.pv.front() != asked) return l.pv.front();
    // Else the review's best there.
    if (!b.reviews.empty())
        for (std::size_t i = 0; i + 1 < b.reviews.front().positions.size() && i <= b.tree.main.moves.size(); ++i)
            if (boardAt(b.tree.main, b.chess960, static_cast<int>(i)).hash() == before.hash() &&
                b.reviews.front().positions[i].best != asked)
                return b.reviews.front().positions[i].best;
    return {};
}

void App::whyNot(int at_ply, const std::string& asked, const std::string& reference) {
    if (gameLocked()) {  // fair play (#48)
        boardToast(TR("Locked while one of your games is played on Lichess."));
        return;
    }
    if (engineGameHidesAids()) {  // a game against an engine (#83)
        boardToast(TR("Hidden until the end of your game: Show them first, and the game is marked “with aids”"));
        return;
    }
    if (!assistant_) return;
    if (!visit_)
        board_side_tab_ = 4;  // the answer comes in the Questions tab; without the model, the templates write it
    if (asked.empty() || reference.empty() || asked == reference) {
        boardToast(TR("Alt+Q sets a move against another: the game's, or the engine's best"));
        return;
    }
    // Shown in the interface's letters, asked in the assistant's: "Why not 5…Nxd5 rather than 5…Na5?", which the
    // Questions read as Compare without the model.
    // Asked in the position where both moves are played, so that the names of the moves of the position after cannot
    // be taken for them (a white Be6 there, a black one before).
    const auto before = boardAt(board_.game, board_.chess960, at_ply);
    askQuestion(trf("Why not {}?", numbered(before, asked, false)),
                whyNotQuestion(before, asked, reference, frenchAssistant()), at_ply);
}

void App::whyNotHere() {
    if (visit_) return visitWhyNot();
    auto& b = board_;
    if (b.ply <= 0 || b.ply > static_cast<int>(b.game.moves.size())) {
        boardToast(TR("Alt+Q: after a move tried on the board, why not it rather than the game's"));
        return;
    }
    const auto before = boardAt(b.game, b.chess960, b.ply - 1);
    const auto move   = chess::uci::moveToUci(b.game.moves[static_cast<std::size_t>(b.ply - 1)].move, b.chess960);
    if (b.line >= 0) {
        // A variation tried: its move against the game's from the same position.
        whyNot(b.ply - 1, move, whyNotReference(before, move));
        return;
    }
    // The game's own move: the review's best set against it ("Why not the engine's move?").
    std::string best;
    if (!b.reviews.empty() && static_cast<std::size_t>(b.ply - 1) < b.reviews.front().positions.size())
        best = b.reviews.front().positions[static_cast<std::size_t>(b.ply - 1)].best;
    if (best.empty() || best == move) {
        boardToast(TR("Alt+Q: after a move tried on the board, why not it rather than the game's"));
        return;
    }
    whyNot(b.ply - 1, best, move);
}

// ---------------------------------------------------------------------------
// The guided visit

void App::startVisit() {
    if (gameLocked()) {  // fair play (#48)
        boardToast(TR("Locked while one of your games is played on Lichess."));
        return;
    }
    if (engineGameHidesAids()) {  // a game against an engine (#83)
        boardToast(TR("Hidden until the end of your game: Show them first, and the game is marked “with aids”"));
        return;
    }
    auto& b           = board_;
    const auto review = boardCommentaryReview();
    if (!review || !review->report.complete || b.reviews.empty()) {
        boardToast(TR("Analyse the game first (Alt+G): the visit goes through its review's key moments"));
        return;
    }
    auto moments = visitMoments(review->report);
    if (moments.empty()) {
        boardToast(TR("Nothing in the game calls for a visit: no key moment, no mistake"));
        return;
    }
    auto run      = std::make_unique<VisitRun>();
    run->moments  = std::move(moments);
    run->review   = review;
    run->analysis = b.reviews.front();
    run->line     = b.line;
    run->ply      = b.ply;
    run->game     = board_game_serial_;
    if (const auto* engine = analysisEngine()) {
        AnalysisSettings settings;
        settings.engine  = engine->name;
        settings.threads = 1;
        settings.hash    = b.settings.analysis.hash;
        settings.multipv = 1;
        run->engine.start(*engine, settings);
    }
    board_.preview.reset();
    visit_ = std::move(run);
    visitShowMoment();
}

void App::visitShowMoment() {
    auto& v = *visit_;
    auto& b = board_;
    if (v.current >= v.moments.size()) {
        v.phase = VisitRun::Phase::End;
        return;
    }
    const auto ply = v.moments[v.current];
    showBoardLine(-1, static_cast<int>(ply) - 1);
    b.move_typed.reset();
    b.selected.reset();
    b.preview.reset();
    v.phase    = VisitRun::Phase::Asking;
    v.answered = v.dont_know = v.card = false;
    v.answer                          = chess::Move::NO_MOVE;
    v.verdict                         = {};
    v.best_score.reset();
    v.answer_score.reset();
    v.answer_asked = false;
    v.question.reset();
    v.best = ply - 1 < v.analysis.positions.size() ? v.analysis.positions[ply - 1].best : std::string();
    // The explanation of the game's move, held to the facts: the model's when every move was explained, else the
    // template's (or the review's words), from the review's facts.
    v.explanation.clear();
    if (explained_)
        for (const auto& l : explained_->lines)
            if (l.written && l.from == ply && l.to == ply && l.source != ExplainedLine::Source::Theory)
                v.explanation = l.text;
    if (v.explanation.empty())
        if (const auto* m = v.review->report.get(ply)) {
            const auto lang = frenchAssistant() ? commentary::Lang::Fr : commentary::Lang::En;
            const auto letters =
                frenchAssistant() ? commentary::PieceLetters::French : commentary::PieceLetters::English;
            const auto ctx =
                commentary::moveContext(v.review->line, *m, v.review->analyses, nullptr, reviewTables(), nullptr);
            v.explanation = writeExplainedLine(nullptr, ctx, lang, letters).text;
        }
    // The engine's move searched while the answer is thought over, as Guess the moves searches the game's.
    if (!v.best.empty() && v.engine.active()) {
        v.best_position    = positionOf(b.game, b.chess960, ply - 1, v.best);
        chess::Board there = boardAt(b.game, b.chess960, static_cast<int>(ply) - 1);
        there.makeMove(chess::uci::uciToMove(there, v.best));
        if (const auto end = endScore(there))
            v.best_score = end;
        else
            v.engine.analyse(v.best_position, SearchLimit{VISIT_SEARCH_MS, 0, 0}, v.current == 0);
    }
}

void App::visitAnswer(chess::Move move) {
    auto& v = *visit_;
    auto& b = board_;
    if (v.phase != VisitRun::Phase::Asking || b.line >= 0 || b.ply != static_cast<int>(v.moments[v.current]) - 1)
        return;
    b.selected.reset();
    b.move_typed.reset();
    v.answer   = move;
    v.answered = true;
    if (chess::uci::moveToUci(move, b.chess960) == v.best) {
        v.verdict = judgeVisitAnswer(true, 0, 0, false);
        visitJudged();
        return;
    }
    const auto ply     = v.moments[v.current];
    v.answer_position  = positionOf(b.game, b.chess960, ply - 1, chess::uci::moveToUci(move, b.chess960));
    chess::Board there = boardAt(b.game, b.chess960, static_cast<int>(ply) - 1);
    there.makeMove(move);
    if (const auto end = endScore(there)) {
        v.answer_score = end;
        v.answer_asked = true;
    }
    v.phase = VisitRun::Phase::Checking;
    pollVisit();
}

void App::visitDontKnow() {
    auto& v = *visit_;
    if (v.phase != VisitRun::Phase::Asking) return;
    v.dont_know = true;
    v.verdict   = judgeVisitAnswer(false, 0, 0, true);
    board_.move_typed.reset();
    visitJudged();
}

void App::visitJudged() {
    auto& v      = *visit_;
    const auto p = v.moments[v.current];
    // A wrong answer: a card of the day's queue, the position before the game's move, the engine's move to find.
    if (v.verdict.wrong)
        if (auto exercise = visitExercise(board_.game, v.analysis, p)) {
            addVisitCard(mistakeRecord(), std::move(*exercise), epochNow(), trainingScheduler());
            saveMistakeRecord();
            v.card = true;
        }
    v.results.push_back(
        {p, v.answered ? chess::uci::moveToUci(v.answer, board_.chess960) : std::string(), v.verdict.points, v.card});
    v.phase = VisitRun::Phase::Shown;
}

void App::visitNext() {
    auto& v = *visit_;
    if (v.phase != VisitRun::Phase::Shown) return;
    ++v.current;
    visitShowMoment();
}

void App::stopVisit() {
    if (!visit_) return;
    auto& v = *visit_;
    if (v.game == board_game_serial_) showBoardLine(v.line, v.ply);
    board_.preview.reset();
    v.engine.stop();
    visit_.reset();
}

void App::pollVisit() {
    if (visit_when_read_ && !visit_ && !board_.review_key.empty() && !board_.reviews.empty()) {
        visit_when_read_ = false;
        startVisit();
    }
    if (!visit_) return;
    auto& v = *visit_;
    // Another game on the board; a game of the user's on Lichess, or against an engine, that hides the aids (R17).
    if (v.game != board_game_serial_ || gameLocked() || engineGameHidesAids()) return stopVisit();
    v.engine.poll();
    if (v.engine.finished() && !v.engine.lines().empty() && v.engine.lines().front().score) {
        if (!v.best_score && v.engine.position() == v.best_position) v.best_score = v.engine.lines().front().score;
        if (!v.answer_score && v.answer_asked && v.engine.position() == v.answer_position)
            v.answer_score = v.engine.lines().front().score;
    }
    if (v.phase != VisitRun::Phase::Checking) return;
    // The answer searched once the engine's move is, both for the same time.
    if (v.best_score && !v.answer_asked) {
        v.answer_asked = true;
        v.engine.analyse(v.answer_position, SearchLimit{VISIT_SEARCH_MS, 0, 0}, false);
    }
    // No engine: the answer is judged by the engine's move alone.
    if (!v.engine.active() || v.engine.state() == Analyser::State::Failed) {
        v.verdict = judgeVisitAnswer(false, 1, -1, false);
        visitJudged();
        return;
    }
    if (v.best_score && v.answer_score) {
        // The scores are the other side's, to move after each move: turned around for the side that played.
        v.verdict = judgeVisitAnswer(false, -scorePawns(*v.best_score), -scorePawns(*v.answer_score), false);
        visitJudged();
    }
}

void App::visitWhyNot() {
    auto& v = *visit_;
    if (v.phase != VisitRun::Phase::Shown) return;  // nothing shown before the answer
    const auto ply   = v.moments[v.current];
    const auto game  = chess::uci::moveToUci(board_.game.moves[ply - 1].move, board_.chess960);
    const auto mine  = v.answered ? chess::uci::moveToUci(v.answer, board_.chess960) : std::string();
    const auto asked = !mine.empty() && mine != v.best ? mine : game;
    whyNot(static_cast<int>(ply) - 1, asked, v.best);
    if (!questions_.empty()) v.question = questions_.size() - 1;
}

void App::drawVisitPage() {
    auto& v = *visit_;
    auto& b = board_;
    ui::pageTitle(TR("Board"), guide::topics::GuidedVisit);
    if (!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
        !SDL_TextInputActive(window_))
        SDL_StartTextInput(window_);
    // The keys: a move typed, Space "I don't know", Enter the next moment, Alt+Q "Why not?", Escape stops.
    const auto& io = ImGui::GetIO();
    if (!io.WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        const auto owner = ImGui::GetID("##visit_keys");
        for (const auto key : {ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Escape, ImGuiKey_Space})
            ImGui::SetKeyOwner(key, owner);
        const bool plain = !io.KeyCtrl && !io.KeyAlt && !io.KeySuper;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) return stopVisit();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_Q)) visitWhyNot();
        if (v.phase == VisitRun::Phase::Asking) {
            if (plain && ImGui::IsKeyPressed(ImGuiKey_Space, false)) visitDontKnow();
            boardMoveKeys();
        } else if (plain &&
                   (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
            if (v.phase == VisitRun::Phase::Shown)
                visitNext();
            else if (v.phase == VisitRun::Phase::End)
                return stopVisit();
        }
    }
    if (!visit_) return;

    // The board: the moment's position; after the answer, the answer in blue, the engine's move in green, the game's in
    // orange. Before it, nothing: no evaluation, no arrow, no mark.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float side   = std::clamp(avail.x * 0.42f, px(360), px(640));
    const float size   = std::floor(std::max(px(200), std::min(avail.x - side - px(32), avail.y - px(40))));
    const auto board   = boardAt(b.game, b.chess960, b.ply);
    BoardDecor decor;
    decor.flipped    = b.flip;
    decor.eval_white = -1.0f;  // no evaluation in the visit (visitShows())
    if (b.ply > 0 && b.ply <= static_cast<int>(b.game.moves.size()))
        decor.last_move = b.game.moves[static_cast<std::size_t>(b.ply - 1)].move;
    const bool asking = v.phase == VisitRun::Phase::Asking && b.line < 0 && v.current < v.moments.size() &&
                        b.ply == static_cast<int>(v.moments[v.current]) - 1;
    const auto shows  = visitShows(v.phase);
    if (shows.notation && v.phase == VisitRun::Phase::Shown && v.current < v.moments.size()) {
        const auto ply  = v.moments[v.current];
        const auto game = b.game.moves[ply - 1].move;
        decor.arrows.push_back({game, IM_COL32(226, 116, 31, 200)});
        if (v.answered && chess::uci::moveToUci(v.answer, b.chess960) != v.best)
            decor.arrows.push_back({v.answer, markColour('B', false)});
        if (const auto best = chess::uci::uciToMove(board, v.best); best != chess::Move::NO_MOVE)
            decor.arrows.push_back({best, markColour('G', false)});
    } else if (shows.arrows && v.phase == VisitRun::Phase::Checking && v.answered) {
        decor.arrows.push_back({v.answer, markColour('B', false)});
    }
    std::optional<chess::Board> preview;
    if (b.preview) {
        auto at = boardAt(b.game, b.chess960, b.preview->ply);
        for (const auto& mv : legalPrefix(at, b.preview->moves, b.preview->moves.size())) at.makeMove(mv);
        preview          = at;
        decor            = BoardDecor{};
        decor.flipped    = b.flip;
        decor.eval_white = -1.0f;
        decor.frame      = ui::ACCENT;
    }
    decor.selected = asking ? b.selected : std::nullopt;
    if (decor.selected)
        for (const auto& m : movesFrom(board, *decor.selected)) decor.targets.push_back(shownTarget(m));
    const bool dragging = asking && b.drag && b.drag->moved && b.selected == b.drag->from;
    if (dragging) {
        decor.lifted = b.drag->from;
        if (b.drag_over && std::find(decor.targets.begin(), decor.targets.end(), *b.drag_over) != decor.targets.end())
            decor.drop = b.drag_over;
    }
    ImGui::BeginGroup();
    const ImVec2 top = ImGui::GetCursorScreenPos();
    if (appearance::current().board3d.on && board3d_.available()) {
        board3d_.setMarks(Board3d::Marks::Off);
        const auto input = board3d_.draw(preview ? *preview : board, size, decor);
        if (asking && !preview) boardPointer(board, input.clicked, input.hovered, input.right_click);
    } else {
        if (!appearance::current().board3d.on) board3d_.release();
        drawBoard(atlas_, preview ? *preview : board, size, decor);
        const auto hover = boardSquareAt(ImGui::GetMousePos(), top, size, decor);
        if (asking && !preview) {
            const bool left  = ImGui::IsItemClicked(ImGuiMouseButton_Left),
                       right = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            boardPointer(board, left ? hover : std::nullopt, hover, right);
            if (dragging && b.drag) drawDraggedPiece(atlas_, board.at(b.drag->from), size);
        }
        if (preview && ImGui::IsItemClicked()) b.preview.reset();
    }
    drawPromotionPopup(board);
    ImGui::EndGroup();
    ImGui::SameLine(0, px(18));

    // The column: the moments, the question, then the answer and why.
    ImGui::BeginChild("##visit_side", ImVec2(0, 0));
    drawVisitColumn();
    ImGui::EndChild();
}

void App::drawVisitColumn() {
    auto& v = *visit_;
    auto& b = board_;
    // F1 opens the guide at the visit: a region ended before the column's child is.
    const ui::TopicRegion topic(guide::topics::GuidedVisit);
    ImGui::PushTextWrapPos();
    for (std::size_t i = 0; i < v.moments.size(); ++i) {
        const auto ply = v.moments[i];
        auto label     = fmt::format("{} · {}", i + 1, boardMoveLabel(static_cast<int>(ply)));
        if (ply - 1 < b.judged.size()) label += judgementMark(b.judged[ply - 1].judgement);
        const float w = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
        if (i > 0) ui::sameLineOrWrap(w, px(4));
        const bool on = i == v.current && v.phase != VisitRun::Phase::End;
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button, ui::ACCENT);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32_WHITE);
        }
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginDisabled(i > v.current || v.phase == VisitRun::Phase::End);
        (void)ImGui::SmallButton(label.c_str());
        ImGui::EndDisabled();
        ImGui::PopID();
        if (on) ImGui::PopStyleColor(2);
    }
    ImGui::Spacing();
    if (v.phase == VisitRun::Phase::End) {
        int found = 0, cards = 0;
        for (const auto& r : v.results) {
            found += r.points >= 2 ? 1 : 0;
            cards += r.card ? 1 : 0;
        }
        ui::beginCard("visit_end");
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(trf("The visit is over: {} of {} moments found.", found, v.results.size()).c_str());
        if (cards > 0)
            ImGui::TextDisabled(
                "%s", trn("{} card added to today's review: the position, its best move to find.",
                          "{} cards added to today's review: the positions, their best moves to find.", cards, cards)
                          .c_str());
        if (ImGui::Button(TR("Back to the game · Esc"))) stopVisit();
        ImGui::PopTextWrapPos();
        ui::endCard();
        ImGui::PopTextWrapPos();
        return;
    }
    const auto ply    = v.moments[v.current];
    const auto before = boardAt(b.game, b.chess960, static_cast<int>(ply) - 1);
    const auto game   = chess::uci::moveToUci(b.game.moves[ply - 1].move, b.chess960);
    ui::beginCard("visit_question");
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", trf("Moment {} of {} · your game played {}", v.current + 1, v.moments.size(),
                                  numbered(before, game, false))
                                  .c_str());
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.15f);
    ImGui::TextUnformatted(TR("Before seeing why: what would you have played?"));
    ImGui::PopFont();
    if (v.phase == VisitRun::Phase::Asking) {
        ImGui::TextDisabled("%s", TR("Play it on the board, or type it. Nothing is shown before your answer; Space: “I "
                                     "don't know”."));
        drawMoveField();
        if (ImGui::SmallButton(TR("I don't know · Space"))) visitDontKnow();
    } else if (v.phase == VisitRun::Phase::Checking) {
        ImGui::TextDisabled("%s", trf("The engine looks at your {}…",
                                      numbered(before, chess::uci::moveToUci(v.answer, b.chess960), false))
                                      .c_str());
    }
    ImGui::PopTextWrapPos();
    ui::endCard();
    if (v.phase == VisitRun::Phase::Shown) {
        // The answer judged, the engine's move, why, and what it rests on.
        const bool right = !v.verdict.wrong;
        ui::beginCard("visit_answer");
        ImGui::PushTextWrapPos();
        const auto best = numbered(before, v.best, false);
        std::string title =
            v.dont_know             ? trf("You did not know — the engine wanted {}", best)
            : v.verdict.points == 3 ? trf("You: {} — the move the engine wanted",
                                          numbered(before, chess::uci::moveToUci(v.answer, b.chess960), false))
            : v.verdict.points == 2 ? trf("You: {} — as good as the engine's {}",
                                          numbered(before, chess::uci::moveToUci(v.answer, b.chess960), false), best)
            : v.verdict.points == 1 ? trf("You: {} — playable; the engine wanted {}",
                                          numbered(before, chess::uci::moveToUci(v.answer, b.chess960), false), best)
                                    : trf("You: {} — the engine wanted {}",
                                          numbered(before, chess::uci::moveToUci(v.answer, b.chess960), false), best);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(right ? ui::GOOD : ui::WARN), "%s", title.c_str());
        if (!v.dont_know)
            ImGui::TextDisabled(
                "%s", trn("{} point, as Guess the moves counts them", "{} points, as Guess the moves counts them",
                          v.verdict.points, v.verdict.points)
                          .c_str());
        if (!v.explanation.empty()) {
            ImGui::PushFont(ui::italicFont(), 0.0f);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::assistantColour()), "%s", v.explanation.c_str());
            ImGui::PopFont();
        }
        const auto& pos = v.analysis.positions[ply - 1];
        std::string line;
        if (!pos.pv.empty()) {
            std::vector<std::string> pv;
            std::istringstream in(pos.pv);
            for (std::string m; in >> m;) pv.push_back(m);
            line = localLine(pvToSan(before, pv, 8));
        }
        ImGui::TextDisabled(
            "%s", (line.empty() ? trf("Rests on: the review ({}), the facts of the positions.", v.analysis.engine)
                                : trf("Rests on: the review ({}), the facts of the positions, the engine's line: {}",
                                      v.analysis.engine, line))
                      .c_str());
        if (v.card)
            ImGui::TextDisabled("%s", TR("A card of today's review now: this position, the engine's move to find."));
        ImGui::PopTextWrapPos();
        ui::endCard();
        if (ImGui::Button(TR("Next moment · Enter"))) visitNext();
        ImGui::SameLine();
        if (ImGui::Button(TR("Why not another move? · Alt+Q"))) visitWhyNot();
        ImGui::SameLine();
        if (ImGui::Button(TR("See the line")) && !pos.pv.empty()) {
            BoardPreview preview;
            preview.ply = preview.at = static_cast<int>(ply) - 1;
            std::istringstream in(pos.pv);
            for (std::string m; in >> m;) preview.moves.push_back(m);
            preview.label = TR("The engine's line");
            b.preview     = std::move(preview);
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Stop · Esc"))) {
            ImGui::PopTextWrapPos();
            return stopVisit();
        }
        // The "Why not?" asked, answered here.
        if (v.question && *v.question < questions_.size()) {
            std::optional<std::size_t> remove;
            drawQuestionCard(*v.question, remove);
            if (remove) v.question.reset();
        }
    } else if (ImGui::Button(TR("Stop · Esc"))) {
        ImGui::PopTextWrapPos();
        return stopVisit();
    }
    ImGui::TextDisabled("%s",
                        TR("The key moments come from the review, the question from Guess the moves; a wrong answer "
                           "becomes a card of today's review, as your mistakes do."));
    ImGui::PopTextWrapPos();
}

}  // namespace fcg
