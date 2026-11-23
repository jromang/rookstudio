// A series of puzzles (#80, mockup M12, board 1; puzzles.hpp): on the Board page, whose column becomes the puzzle's, as
// the repertoires' training does. A puzzle from Lichess's database offline, from the batch of the user's Lichess
// account, or a puzzle missed asked again by the day's review. The states of the mockup: the question, right, wrong,
// the end of the series.
//
// The keys of a question: a move played on the board, or typed (its letters open the move field); Alt+H a hint (the
// piece to play; the puzzle then counts as missed), Space "I don't know", Escape stops. After the answer, Enter goes on
// (or tries again after a wrong one), → plays the solution a move at a time, Alt+A the engine.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>  // SetKeyOwner

#include <study/game_tree.hpp>

#include "app.hpp"
#include "board_text.hpp"
#include "board_view.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

using Clock       = std::chrono::steady_clock;
using PuzzlePhase = PuzzleRun::Phase;

double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

// The opponent's reply comes after this: time to see the move that was right.
constexpr double REPLY_SECONDS = 0.5;

ImU32 mix(ImU32 a, ImU32 b, float t) {
    const auto fa = ImGui::ColorConvertU32ToFloat4(a), fb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(fa.x + (fb.x - fa.x) * t, fa.y + (fb.y - fa.y) * t, fa.z + (fb.z - fa.z) * t, 1.0f));
}

void beginTinted(const char* id, ImU32 tone) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BG, tone, 0.10f)));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BD, tone, 0.55f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(14)));
    ImGui::BeginChild(id, ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
}

void endTinted() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

bool primaryButton(const char* label, bool enabled = true) {
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImGui::ColorConvertU32ToFloat4(mix(ui::ACCENT, IM_COL32(255, 255, 255, 255), 0.15f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImGui::ColorConvertU32ToFloat4(mix(ui::ACCENT, IM_COL32(0, 0, 0, 255), 0.15f)));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    const bool clicked = ImGui::Button(label);
    ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
    return clicked;
}

void bigCount(const std::string& n, const std::string& what) {
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.9f);
    ImGui::TextUnformatted(n.c_str());
    ImGui::PopFont();
    ImGui::TextDisabled("%s", what.c_str());
    ImGui::EndGroup();
}

void wrapped(const std::string& text, bool grey = false) {
    if (grey) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    if (grey) ImGui::PopStyleColor();
}

// A move with its number as the interface writes it: "24.Nc7+", "23…Qxd4".
std::string numbered(const chess::Board& before, chess::Move move) {
    return fmt::format("{}{}{}", before.fullMoveNumber(), before.sideToMove() == chess::Color::WHITE ? "." : "…",
                       localSan(chess::uci::moveToSan(before, move)));
}

// The game the board shows for a puzzle: from its position, the opponent's move, then `played` moves of the solution.
ReplayGame gameOf(const PuzzlePlay& play, std::size_t played) {
    ReplayGame game;
    game.start_fen = play.before.getFen();
    auto board     = play.before;
    std::vector<chess::Move> moves{play.opponent};
    for (std::size_t i = 0; i < played && i < play.solution.size(); ++i) moves.push_back(play.solution[i]);
    for (const auto m : moves) {
        ReplayMove r;
        r.san   = chess::uci::moveToSan(board, m);
        r.move  = m;
        r.mover = board.sideToMove();
        game.moves.push_back(std::move(r));
        board.makeMove(m);
    }
    return game;
}

// The board after the opponent's move and `played` moves of the solution.
chess::Board boardOf(const PuzzlePlay& play, std::size_t played) {
    auto board = play.before;
    board.makeMove(play.opponent);
    for (std::size_t i = 0; i < played && i < play.solution.size(); ++i) board.makeMove(play.solution[i]);
    return board;
}

// "Fork, middlegame, short": a puzzle's themes in the words of the interface.
std::string themesText(const std::vector<std::string>& themes) {
    std::string out;
    for (const auto& t : themes) out += (out.empty() ? "" : ", ") + puzzleThemeName(t);
    return out;
}

// The theme a puzzle is shown with: its first in the page's order (the motifs before the phases and lengths).
std::string mainTheme(const std::vector<std::string>& themes, const std::string& otherwise) {
    for (const auto key : puzzleThemesShown())
        if (std::find(themes.begin(), themes.end(), key) != themes.end()) return puzzleThemeName(key);
    return themes.empty() ? otherwise : puzzleThemeName(themes.front());
}

// "1742 → 1751 (+9)".
std::string ratingChange(double before, double after) {
    const int b = static_cast<int>(std::lround(before)), a = static_cast<int>(std::lround(after));
    return fmt::format("{} → {} ({:+})", b, a, a - b);
}

// When a card comes back: "tomorrow", "in 6 days".
std::string whenText(int64_t seconds) {
    const auto days = std::max<int64_t>(1, (seconds + study::fsrs::DAY / 2) / study::fsrs::DAY);
    if (days == 1) return TR("tomorrow");
    return trn("in {} day", "in {} days", static_cast<uint64_t>(days), days);
}

}  // namespace

// ---------------------------------------------------------------------------
// A series

std::string App::puzzleLock() const { return trainingLock(); }

void App::startPuzzles() {
    if (!puzzleLock().empty()) return;
    auto run = std::make_unique<PuzzleRun>();
    if (puzzles_online_) {
        auto* batch = onlineBatch();
        if (!puzzleScopes()) return;
        if (!batch || batch->left() == 0) {
            online_start_after_ = true;
            askOnlineBatch();
            return;
        }
        run->source = PuzzleRun::Source::Online;
        run->title  = batch->angle == "mix" ? std::string(TR("Random")) : puzzleThemeName(batch->angle);
        for (const auto& p : batch->puzzles)
            if (!batch->answered(p.id)) run->puzzles.push_back(p);
    } else {
        const auto store = offline_puzzles_ ? offline_puzzles_->store() : nullptr;
        if (!store) return;
        int list = -1;
        if (!puzzles_theme_.empty()) {
            const auto opening = std::string_view(puzzles_theme_).rfind("opening:", 0) == 0;
            const auto found =
                opening ? store->openingList(puzzles_theme_.substr(8)) : store->themeList(puzzles_theme_);
            if (!found) {
                boardToast(trf("No puzzle of “{}” in this database.", puzzleThemeName(puzzles_theme_)));
                return;
            }
            list = *found;
        }
        const int rating =
            static_cast<int>(std::lround(puzzleRating().glicko.rating)) + difficultyOffset(puzzles_difficulty_);
        const auto& seen = puzzleRecord().seen;
        const auto seed  = static_cast<uint64_t>(Clock::now().time_since_epoch().count());
        for (const auto k :
             store->pick(list, rating, PUZZLE_SERIES, [&](const std::string& id) { return seen.count(id) > 0; }, seed))
            run->puzzles.push_back(store->puzzle(k));
        run->source = PuzzleRun::Source::Offline;
        run->title  = puzzles_theme_.empty() ? std::string(TR("Random"))
                      : std::string_view(puzzles_theme_).rfind("opening:", 0) == 0
                          ? puzzleOpeningName(puzzles_theme_.substr(8))
                          : puzzleThemeName(puzzles_theme_);
    }
    if (run->puzzles.empty()) {
        boardToast(TR("No puzzle left of this choice: every one was seen."));
        return;
    }
    startPuzzleRun(std::move(run));
}

bool App::startPuzzleReview() {
    if (!puzzleLock().empty()) return false;
    const auto day = trainingDay(epochNow());
    auto run       = std::make_unique<PuzzleRun>();
    run->source    = PuzzleRun::Source::Review;
    run->title     = TR("Puzzles to review");
    // The longest overdue first.
    std::vector<const PuzzleCard*> due;
    for (const auto& [id, c] : puzzleRecord().cards)
        if (c.card.due <= day.end) due.push_back(&c);
    std::sort(due.begin(), due.end(), [](const auto* a, const auto* b) { return a->card.due < b->card.due; });
    for (const auto* c : due) run->puzzles.push_back(c->puzzle);
    if (run->puzzles.empty()) return false;
    startPuzzleRun(std::move(run));
    return true;
}

void App::startPuzzleRun(std::unique_ptr<PuzzleRun> run) {
    if (training_) closeTraining(false);
    const bool chained = review_running_;
    if (puzzle_) closePuzzles(false);
    if (mistake_) closeMistakes(false);  // a session of exercises, a game guessed (#81)
    if (guess_) closeGuess(false);
    if (endgame_) closeEndgame(false);  // an endgame against the tables, a vision exercise (#82)
    if (vision_) closeVision(false);
    review_running_ = chained;
    for (std::size_t i = 0; i < run->puzzles.size(); ++i) run->queue.push_back(i);
    run->total        = run->puzzles.size();
    run->flip         = board_.flip;
    run->tab          = board_side_tab_;
    run->rating_start = puzzleRating().glicko.rating;
    // The game on the board goes among the recent games (Ctrl+Tab), given back after the series.
    const auto& b = board_;
    run->aside    = !b.game.moves.empty() || !b.tree.variations.empty() || !b.title.empty();
    toStudyBoard();
    openOnBoard(ReplayGame{}, 0, false, trf("Puzzles · {}", run->title), false);
    board_back_.reset();
    if (board_side_tab_ == 0) board_side_tab_ = 2;  // with the answer, the tree rather than the notation's editing
    run->started = Clock::now();
    puzzle_      = std::move(run);
    askPuzzle();
}

void App::showPuzzlePosition(std::size_t index, std::size_t played) {
    auto& p = *puzzle_;
    if (index != p.current || !p.play) return;
    auto& b    = board_;
    b.game     = gameOf(*p.play, played);
    b.tree     = GameTree{b.game, {}};
    b.line     = -1;
    b.ply      = static_cast<int>(b.game.moves.size());
    b.chess960 = false;
    b.selected.reset();
    b.promotion.reset();
    b.preview.reset();
    b.drag.reset();
    b.drag_over.reset();
    b.move_typed.reset();
    b.move_error.clear();
    b.comment_edit.reset();
    b.undo.clear();
    b.redo.clear();
    b.changes     = {};
    b.start_notes = {};
    b.new_game    = true;
}

void App::askPuzzle() {
    auto& p = *puzzle_;
    while (!p.queue.empty()) {
        p.current = p.queue.front();
        p.play    = puzzlePlay(p.puzzles[p.current]);
        if (p.play) break;
        p.queue.pop_front();  // its moves do not play: left out
        p.total = p.total > 0 ? p.total - 1 : 0;
    }
    if (p.queue.empty()) return finishPuzzles(false);
    p.again = std::any_of(p.outcomes.begin(), p.outcomes.end(), [&](const auto& o) { return o.puzzle == p.current; });
    p.step  = 0;
    p.hint = p.hinted = p.retry = p.shown = p.hovered = p.next_again = false;
    p.solution_shown                                                 = 0;
    p.played.clear();
    p.phase = PuzzlePhase::Asking;
    showPuzzlePosition(p.current, 0);
    // From the side of the one who solves it: the side to move after the opponent's move.
    board_.flip = p.play->before.sideToMove() == chess::Color::WHITE;
    p.asked     = Clock::now();
}

void App::puzzleAnswer(chess::Move move) {
    auto& p = *puzzle_;
    if (p.phase != PuzzlePhase::Asking || !p.play || liveGameOn()) return;
    auto& b = board_;
    if (b.ply != static_cast<int>(1 + p.step)) return;  // the question's position only
    const auto board  = boardAt(b.game, b.chess960, b.ply);
    const auto answer = judgeMove(*p.play, p.step, board, move);
    p.played          = chess::uci::moveToSan(board, move);
    p.played_move     = move;
    b.ply             = playOnGame(b.game, b.chess960, b.ply, move);
    b.tree            = GameTree{b.game, {}};
    b.selected.reset();
    b.move_typed.reset();
    switch (answer) {
        case PuzzleAnswer::Right:
            ++p.step;
            p.hint     = false;
            p.phase    = PuzzlePhase::Reply;
            p.reply_at = Clock::now();
            break;
        case PuzzleAnswer::Solved:
            p.step     = p.play->solution.size();
            p.answered = Clock::now();
            if (!p.retry && !p.practice) puzzleGrade(!p.hinted);
            p.phase = PuzzlePhase::Right;
            break;
        case PuzzleAnswer::Wrong:
            p.answered = Clock::now();
            if (!p.retry && !p.practice) puzzleGrade(false);
            p.phase = PuzzlePhase::Wrong;
            break;
    }
}

void App::puzzleGrade(bool win) {
    auto& p             = *puzzle_;
    const auto& puzzle  = p.puzzles[p.current];
    p.seconds           = since(p.asked);
    const auto now      = epochNow();
    auto& record        = puzzleRecord();
    const bool scripted = options_.scripted;  // a run for screenshots leaves the profile as it was
    PuzzleResult result;
    result.id     = puzzle.id;
    result.at     = now;
    result.win    = win;
    result.rating = puzzle.rating;
    result.themes = puzzle.themes;
    result.ms     = static_cast<int64_t>(std::min(p.seconds, 600.0) * 1000);
    switch (p.source) {
        case PuzzleRun::Source::Offline: {
            const auto before = puzzleRating();
            const auto after  = ratedAfter(before, puzzle.rating, puzzle.deviation, win, now);
            record.rating     = after;
            p.rating_before   = before.glicko.rating;
            p.rating_after    = after.glicko.rating;
            result.before     = p.rating_before;
            result.after      = p.rating_after;
            if (!scripted) (void)writePuzzleRating(puzzlesFolder(), after);
            break;
        }
        case PuzzleRun::Source::Online:
            result.online = true;
            if (auto* batch = onlineBatch()) {
                batch->answer(puzzle.id, win);
                saveOnlineBatch();
                if (batch->complete()) sendOnlineResults();  // the batch done: its results go in one request
            }
            break;
        case PuzzleRun::Source::Review: {
            result.review = true;
            auto it       = record.cards.find(puzzle.id);
            if (it == record.cards.end()) break;
            // Right Good, after a hint Hard, wrong Again: a puzzle of several moves takes its time, unlike a
            // repertoire's move, so the time does not make it hard.
            const auto rating = !win       ? study::fsrs::Rating::Again
                                : p.hinted ? study::fsrs::Rating::Hard
                                           : study::fsrs::Rating::Good;
            it->second.card   = trainingScheduler().review(it->second.card, rating, now);
            p.next_review     = it->second.card.due;
            // Back within the hour (missed again): asked again a few puzzles later in the series.
            p.next_again = it->second.card.due - now < study::TrainingSession::AGAIN_WITHIN;
            if (p.next_again) {
                const auto at = std::min<std::size_t>(p.queue.size(), 1 + study::TrainingSession::AGAIN_AFTER);
                p.queue.insert(p.queue.begin() + static_cast<std::ptrdiff_t>(at), p.current);
            }
            if (!scripted) (void)writePuzzleCards(puzzlesFolder(), record.cards);
            break;
        }
    }
    // Missed: a card of the day's review, back tomorrow.
    if (!win && p.source != PuzzleRun::Source::Review) {
        record.cards[puzzle.id] = missedCard(puzzle, now, trainingDayEnd(now), trainingScheduler());
        p.next_review           = record.cards[puzzle.id].card.due;
        if (!scripted) (void)writePuzzleCards(puzzlesFolder(), record.cards);
    }
    record.add(result);
    if (!scripted) appendPuzzleResult(puzzlesFolder(), result);
    if (!p.again) {
        p.outcomes.push_back({p.current, win, win ? std::string() : p.played});
        ++p.done;
    }
}

void App::puzzleDontKnow() {
    auto& p = *puzzle_;
    if (p.phase != PuzzlePhase::Asking || liveGameOn()) return;
    p.played.clear();
    p.answered = Clock::now();
    if (!p.retry && !p.practice) puzzleGrade(false);
    p.shown          = true;
    p.solution_shown = p.step;
    board_.move_typed.reset();
    p.phase = PuzzlePhase::Wrong;
}

void App::puzzleRetry() {
    auto& p = *puzzle_;
    if (p.phase != PuzzlePhase::Wrong || !p.play) return;
    showPuzzlePosition(p.current, p.step);
    p.phase = PuzzlePhase::Asking;
    p.retry = true;
    p.shown = false;
    p.hint  = false;
    p.played.clear();
    p.asked = Clock::now();
}

void App::puzzleSolutionStep() {
    auto& p = *puzzle_;
    if ((p.phase != PuzzlePhase::Wrong && p.phase != PuzzlePhase::End) || !p.play) return;
    if (!p.shown) {
        p.shown          = true;
        p.solution_shown = p.step;
    }
    if (p.solution_shown < p.play->solution.size()) ++p.solution_shown;
    showPuzzlePosition(p.current, p.solution_shown);
}

void App::puzzleNext() {
    auto& p = *puzzle_;
    if (!p.queue.empty()) p.queue.pop_front();
    if (p.queue.empty()) return finishPuzzles(false);
    askPuzzle();
}

void App::finishPuzzles(bool stopped) {
    auto& p        = *puzzle_;
    p.phase        = PuzzlePhase::End;
    p.stopped      = stopped;
    p.took         = since(p.started);
    p.missed_shown = -1;
    board_.selected.reset();
    board_.move_typed.reset();
}

void App::reviewMissedPuzzles() {
    auto& p = *puzzle_;
    std::deque<std::size_t> missed;
    for (const auto& o : p.outcomes)
        if (!o.win) missed.push_back(o.puzzle);
    if (missed.empty()) return;
    p.queue    = std::move(missed);
    p.practice = true;  // their cards stay as they are: back tomorrow
    p.stopped  = false;
    p.started  = Clock::now();
    askPuzzle();
}

void App::closePuzzles(bool to_training) {
    if (!puzzle_) return;
    if (to_training) {
        review_running_ = false;
        review_chain_.clear();
        training_enter_puzzles_ = false;  // back on the page, Enter reviews again
    }
    const bool aside = puzzle_->aside, flip = puzzle_->flip;
    board_side_tab_ = puzzle_->tab;
    puzzle_.reset();
    forget_board_game_ = true;  // the series' position is no game to remember
    if (aside && !recent_games_.empty())
        restoreRecentGame(0);
    else
        newBoardGame();
    forget_board_game_ = false;
    board_.flip        = flip;
    board_back_.reset();
    page_ = to_training ? Page::Training : Page::Board;
}

void App::dropPuzzles() {
    board_.flip     = puzzle_->flip;
    board_side_tab_ = puzzle_->tab;
    puzzle_.reset();
    review_running_ = false;
    review_chain_.clear();
    forget_board_game_ = true;  // the game that comes takes the board; the one put aside stays among the recent ones
}

// ---------------------------------------------------------------------------
// The Board page during a series

void App::puzzleKeys() {
    auto& p        = *puzzle_;
    const auto& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    const auto owner = ImGui::GetID("##puzzle_keys");
    for (const auto key : {ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Space, ImGuiKey_Escape, ImGuiKey_LeftArrow,
                           ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Home, ImGuiKey_End})
        ImGui::SetKeyOwner(key, owner);
    const bool plain = !io.KeyCtrl && !io.KeyAlt && !io.KeySuper;
    const bool enter =
        plain && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    switch (p.phase) {
        case PuzzlePhase::Asking:
            if (escape) {
                finishPuzzles(true);
            } else if (!liveGameOn()) {
                if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_H)) p.hint = p.hinted = true;
                if (plain && ImGui::IsKeyPressed(ImGuiKey_Space, false))
                    puzzleDontKnow();
                else
                    boardMoveKeys();
            }
            break;
        case PuzzlePhase::Reply:
            if (escape) finishPuzzles(true);
            break;
        case PuzzlePhase::Right:
        case PuzzlePhase::Wrong:
            if (escape) {
                finishPuzzles(true);
            } else if (enter) {
                if (p.phase == PuzzlePhase::Right || p.shown)
                    puzzleNext();
                else
                    puzzleRetry();
            } else if (p.phase == PuzzlePhase::Wrong && plain && ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
                puzzleSolutionStep();
            } else if (boardKey()) {
                setBoardAnalysis(!board_.analysing);  // Alt+A: the engine, now that the answer is given
            }
            break;
        case PuzzlePhase::End: {
            const bool missed =
                !p.practice && std::any_of(p.outcomes.begin(), p.outcomes.end(), [](const auto& o) { return !o.win; });
            if (review_running_) {
                if (escape)
                    closePuzzles(true);
                else if (enter && !nextReview())
                    closePuzzles(true);
            } else if (escape) {
                closePuzzles(true);
            } else if (enter) {
                missed ? reviewMissedPuzzles() : closePuzzles(true);
            }
            break;
        }
    }
}

void App::drawPuzzlePage() {
    ui::pageTitle(TR("Board"), guideSection());
    if (!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
        !SDL_TextInputActive(window_))
        SDL_StartTextInput(window_);
    puzzleKeys();
    if (!puzzle_) return;  // left by a key
    auto& p = *puzzle_;
    // The opponent's reply, half a second after a right move.
    if (p.phase == PuzzlePhase::Reply && since(p.reply_at) >= REPLY_SECONDS && p.play &&
        p.step < p.play->solution.size()) {
        auto& b = board_;
        b.ply   = playOnGame(b.game, b.chess960, b.ply, p.play->solution[p.step]);
        b.tree  = GameTree{b.game, {}};
        ++p.step;
        p.phase = PuzzlePhase::Asking;
    }
    // A right answer goes on by itself after a moment, unless the mouse is on it.
    if (p.phase == PuzzlePhase::Right && trainingSettings().advance_ms > 0 && !p.hovered &&
        since(p.answered) * 1000 >= trainingSettings().advance_ms) {
        puzzleNext();
        if (!puzzle_) return;
    }
    if (!board_.message.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, board_.message_error ? ui::BAD : ui::MUTED);
        ImGui::TextWrapped("%s", board_.message.c_str());
        ImGui::PopStyleColor();
    }
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float side   = std::clamp(avail.x * 0.42f, px(340), px(620));
    const float under  = ImGui::GetTextLineHeightWithSpacing() * 4;
    const float size   = std::floor(
        std::max(px(200), std::min(avail.x - side - ImGui::GetStyle().ItemSpacing.x - px(20), avail.y - under)));

    ImGui::BeginGroup();
    // Over the board: the theme and the puzzle's rating, where the series is.
    {
        const auto start = ImGui::GetCursorPosX();
        std::string what = p.title;
        if (p.phase != PuzzlePhase::End && p.current < p.puzzles.size())
            what += " · " + trf("puzzle {}", ui::exactCount(static_cast<uint64_t>(p.puzzles[p.current].rating)));
        ImGui::TextDisabled("%s", what.c_str());
        std::string where;
        if (p.phase != PuzzlePhase::End) {
            // The puzzle shown: once answered, it is among those done.
            const bool answered = p.phase == PuzzlePhase::Right || p.phase == PuzzlePhase::Wrong;
            const auto at       = std::clamp<std::size_t>(p.again || answered ? p.done : p.done + 1, 1, p.total);
            where               = trf("{} of {}", at, p.total);
            if (p.again) where += " · " + std::string(TR("asked again"));
        }
        if (!where.empty()) {
            ImGui::SameLine(
                std::max(ImGui::GetCursorPosX() + px(10), start + size - ImGui::CalcTextSize(where.c_str()).x));
            ImGui::TextDisabled("%s", where.c_str());
        }
    }
    drawPuzzleBoard(size);
    // Under it: what was just played, where the puzzle comes from.
    if (p.play && p.phase != PuzzlePhase::End) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size);
        // The opponent's last move: the one before the puzzle, or its last reply.
        const bool black = p.play->before.sideToMove() == chess::Color::BLACK;
        // The last move of the opponent's among those the board shows.
        const auto& moves = board_.game.moves;
        auto just         = numbered(p.play->before, p.play->opponent);
        for (std::size_t i = moves.size(); i-- > 1;) {
            if (moves[i].mover != p.play->before.sideToMove()) continue;
            just = numbered(boardAt(board_.game, false, static_cast<int>(i)), moves[i].move);
            break;
        }
        ImGui::TextDisabled("%s", (black ? trf("Black has just played {} (in pale green). The puzzle's position comes "
                                               "from a game on Lichess.",
                                               just)
                                         : trf("White has just played {} (in pale green). The puzzle's position comes "
                                               "from a game on Lichess.",
                                               just))
                                      .c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, px(18));
    ImGui::BeginChild("##puzzle_side", ImVec2(0, 0));
    drawPuzzleColumn();
    ImGui::EndChild();
}

void App::drawPuzzleBoard(float size) {
    auto& b          = board_;
    auto& p          = *puzzle_;
    const auto board = boardAt(b.game, b.chess960, b.ply);
    BoardDecor decor;
    decor.flipped    = b.flip;
    decor.eval_white = -1.0f;  // no bar: no engine's word on the position
    if (b.ply > 0) decor.last_move = b.game.moves[static_cast<std::size_t>(b.ply - 1)].move;
    const auto green    = markColour('G', true);
    const auto orange   = IM_COL32(236, 140, 52, 150);
    const bool question = p.play && b.ply == static_cast<int>(1 + p.step);
    switch (p.phase) {
        case PuzzlePhase::Asking:
            if (p.hint && p.play && p.step < p.play->solution.size())
                decor.tints.push_back({p.play->solution[p.step].from(), markColour('B', true)});
            break;
        case PuzzlePhase::Right:
            if (decor.last_move) {
                decor.tints.push_back({decor.last_move->from(), green});
                decor.tints.push_back({shownTarget(*decor.last_move), green});
            }
            break;
        case PuzzlePhase::Wrong:
            if (!p.play) break;
            if (p.shown) {  // the solution on the board
                if (decor.last_move && p.solution_shown > p.step) {
                    decor.tints.push_back({decor.last_move->from(), green});
                    decor.tints.push_back({shownTarget(*decor.last_move), green});
                }
                if (b.ply == static_cast<int>(1 + p.step) && p.step < p.play->solution.size())
                    decor.arrows.push_back({p.play->solution[p.step], markColour('G', false)});
                break;
            }
            if (!question && decor.last_move) {  // the move played in orange, the solution's arrow in green
                decor.tints.push_back({decor.last_move->from(), orange});
                decor.tints.push_back({shownTarget(*decor.last_move), orange});
                if (p.step < p.play->solution.size())
                    decor.arrows.push_back({p.play->solution[p.step], markColour('G', false)});
            }
            break;
        case PuzzlePhase::End:
            if (p.play && p.missed_shown >= 0 && p.step < p.play->solution.size() &&
                b.ply == static_cast<int>(1 + p.step))
                decor.arrows.push_back({p.play->solution[p.step], markColour('G', false)});
            break;
        case PuzzlePhase::Reply:
            break;
    }
    const bool moves = p.phase == PuzzlePhase::Asking && question && !liveGameOn();
    decor.selected   = moves ? b.selected : std::nullopt;
    if (decor.selected)
        for (const auto& m : movesFrom(board, *decor.selected)) decor.targets.push_back(shownTarget(m));
    const bool dragging = moves && b.drag && b.drag->moved && b.selected == b.drag->from;
    if (dragging) {
        decor.lifted = b.drag->from;
        if (b.drag_over && std::find(decor.targets.begin(), decor.targets.end(), *b.drag_over) != decor.targets.end())
            decor.drop = b.drag_over;
    }
    const ImVec2 top = ImGui::GetCursorScreenPos();
    if (appearance::current().board3d.on && board3d_.available()) {
        board3d_.setMarks(Board3d::Marks::Off);
        const auto input = board3d_.draw(board, size, decor);
        if (moves) boardPointer(board, input.clicked, input.hovered, input.right_click);
    } else {
        if (!appearance::current().board3d.on) board3d_.release();
        drawBoard(atlas_, board, size, decor);
        const auto hover = boardSquareAt(ImGui::GetMousePos(), top, size, decor);
        if (moves) {
            const bool left  = ImGui::IsItemClicked(ImGuiMouseButton_Left),
                       right = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            boardPointer(board, left ? hover : std::nullopt, hover, right);
            if (dragging && b.drag) drawDraggedPiece(atlas_, board.at(b.drag->from), size);
        }
    }
    drawPromotionPopup(board);
}

void App::drawPuzzleColumn() {
    auto& p = *puzzle_;
    const ui::TopicRegion topic(guide::topics::Puzzles);
    if (liveGameOn()) {  // fair play (#48): a live game on Lichess stops the series where it is
        drawFairPlayBanner();
        if (ImGui::Button(TR("Stop · Esc"))) finishPuzzles(true);
        return;
    }
    if (p.phase == PuzzlePhase::End) return drawPuzzleEnd();
    if (!p.play) return;
    const auto& puzzle = p.puzzles[p.current];
    const auto& play   = *p.play;

    const float done = p.total ? static_cast<float>(p.done) / static_cast<float>(p.total) : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    ImGui::ProgressBar(done, ImVec2(-1, px(6)), "");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, px(4)));

    const bool white = play.before.sideToMove() == chess::Color::BLACK;  // the solver: the side after the opponent
    const auto at    = [&](std::size_t played) { return boardOf(play, played); };
    const auto first = numbered(at(0), play.solution.front());
    const auto taken = static_cast<int>(std::lround(p.seconds));
    switch (p.phase) {
        case PuzzlePhase::Asking:
        case PuzzlePhase::Reply: {
            beginTinted("##puzzle_question", ui::CARD_BD);
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.2f);
            ImGui::TextUnformatted(p.retry ? (white ? TR("Again, White") : TR("Again, Black"))
                                           : (white ? TR("Your move, White") : TR("Your move, Black")));
            ImGui::PopFont();
            wrapped(TR("Play on the board, or type the move and press Enter."), true);
            drawMoveField();
            if (ImGui::Button(TR("A hint · Alt+H"))) p.hint = p.hinted = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TR("H alone begins a move of the h-pawn: the hint is under Alt."));
            ImGui::SameLine();
            if (ImGui::Button(TR("I don't know · Space"))) puzzleDontKnow();
            ImGui::SameLine();
            if (ImGui::Button(TR("Stop · Esc"))) finishPuzzles(true);
            if (p.phase != PuzzlePhase::Asking && p.phase != PuzzlePhase::Reply) {  // answered by a button
                endTinted();
                return;
            }
            wrapped(p.hinted ? TR("The piece to play is on the square in blue; the puzzle counts as missed.")
                             : TR("The hint shows the piece to play; the puzzle then counts as missed."),
                    true);
            endTinted();
            ImGui::Dummy(ImVec2(0, px(4)));
            wrapped(TR("The engine, the tree and your notes are hidden during the question; the puzzle's themes stay "
                       "hidden until the answer. The opponent's moves are played by RookStudio, the rest of the "
                       "solution."),
                    true);
            break;
        }
        case PuzzlePhase::Right: {
            beginTinted("##puzzle_right", ui::GOOD);
            ui::inlineIcon(ui::Icon::Check, ui::GOOD);
            ImGui::SameLine();
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.2f);
            const auto moves = (play.solution.size() + 1) / 2;
            ImGui::TextUnformatted((p.retry ? trf("{}! Solved, this time", first)
                                            : trn("{}! Solved in {} move, {} s", "{}! Solved in {} moves, {} s", moves,
                                                  first, moves, taken))
                                       .c_str());
            ImGui::PopFont();
            // What it was: the solution in moves.
            {
                std::string line;
                auto board = at(0);
                for (std::size_t i = 0; i < play.solution.size(); ++i) {
                    line +=
                        (line.empty() ? "" : " ") + (i == 0 || board.sideToMove() == chess::Color::WHITE
                                                         ? numbered(board, play.solution[i])
                                                         : localSan(chess::uci::moveToSan(board, play.solution[i])));
                    board.makeMove(play.solution[i]);
                }
                wrapped(trf("The solution: {}.", line));
            }
            std::string after;
            if (p.retry || p.practice)
                after = TR("Played again: it no longer counts.");
            else if (p.source == PuzzleRun::Source::Offline)
                after = p.hinted
                            ? trf("After a hint: missed. Your rating {}", ratingChange(p.rating_before, p.rating_after))
                            : trf("Your rating {}", ratingChange(p.rating_before, p.rating_after));
            else if (p.source == PuzzleRun::Source::Online)
                after = TR("Its result goes to Lichess at the batch's end.");
            else
                after = p.next_again ? std::string(TR("Asked again in this series, then tomorrow."))
                                     : trf("Next review {}.", whenText(p.next_review - epochNow()));
            if (!puzzle.themes.empty()) after += " · " + trf("themes: {}", themesText(puzzle.themes));
            wrapped(after, true);
            if (primaryButton(TR("Next"))) puzzleNext();
            if (puzzle_ && puzzle_->phase == PuzzlePhase::Right) {
                ImGui::SameLine(0, px(8));
                const int advance = trainingSettings().advance_ms;
                ImGui::TextDisabled(
                    "%s", advance > 0 ? trf("Enter, or on its own after {} s", advance / 1000.0).c_str() : TR("Enter"));
                ImGui::SameLine(0, px(10));
                if (ImGui::Button(TR("Analyse here · Alt+A"))) setBoardAnalysis(true);
                if (!puzzle.game.empty()) {
                    ui::sameLineOrWrap(TR("The game on lichess.org"));
                    if (ImGui::SmallButton(TR("The game on lichess.org"))) SDL_OpenURL(puzzle.gameUrl().c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", TR("Opens the game the puzzle comes from in the browser."));
                }
                if (advance > 0 && !p.hovered)
                    ImGui::ProgressBar(static_cast<float>(std::min(1.0, since(p.answered) * 1000 / advance)),
                                       ImVec2(-1, px(3)), "");
            }
            endTinted();
            if (puzzle_)
                puzzle_->hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
                                                        ImGuiHoveredFlags_AllowWhenBlockedByPopup);
            break;
        }
        case PuzzlePhase::Wrong: {
            beginTinted("##puzzle_wrong", ui::WARN);
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.2f);
            const auto question = at(p.step);
            const auto expected = p.step < play.solution.size() ? numbered(question, play.solution[p.step]) : first;
            if (!p.played.empty())
                ImGui::TextUnformatted(trf("{} is not the solution", numbered(question, p.played_move)).c_str());
            else
                ImGui::TextUnformatted(trf("The solution: {}", expected).c_str());
            ImGui::PopFont();
            if (!p.played.empty()) wrapped(trf("The solution: {}, the green arrow.", expected));
            if (!p.retry && !p.practice) {
                std::string what;
                if (p.source == PuzzleRun::Source::Offline)
                    what = trf("Missed: your rating {}. It comes back tomorrow in “To review today”.",
                               ratingChange(p.rating_before, p.rating_after));
                else if (p.source == PuzzleRun::Source::Online)
                    what = TR(
                        "Missed: its result goes to Lichess at the batch's end; it comes back tomorrow in “To review "
                        "today”.");
                else
                    what = p.next_again ? std::string(TR("Asked again in this series, then tomorrow."))
                                        : trf("Next review {}.", whenText(p.next_review - epochNow()));
                wrapped(what, true);
            }
            if (!p.shown) {
                if (primaryButton(TR("Retry · Enter"))) puzzleRetry();
            } else if (primaryButton(TR("Next · Enter"))) {
                puzzleNext();
            }
            if (puzzle_ && puzzle_->phase == PuzzlePhase::Wrong) {
                ImGui::SameLine();
                ImGui::BeginDisabled(p.shown && p.solution_shown >= play.solution.size());
                if (ImGui::Button(TR("The solution move by move · →"))) puzzleSolutionStep();
                ImGui::EndDisabled();
                if (!p.shown) {
                    ImGui::SameLine();
                    if (ImGui::Button(TR("Next"))) puzzleNext();
                }
            }
            if (puzzle_ && !puzzle_->retry)
                wrapped(TR("Played again right, it no longer counts: the grade is given at the first move."), true);
            endTinted();
            break;
        }
        default:
            break;
    }
    if (!puzzle_ || puzzle_->phase == PuzzlePhase::End) return;
    // With the answer, the tree and the engine come back.
    if (puzzle_->phase == PuzzlePhase::Right || puzzle_->phase == PuzzlePhase::Wrong) {
        ImGui::Separator();
        drawChessTabs();
    }
}

void App::drawPuzzleEnd() {
    auto& p              = *puzzle_;
    const auto& outcomes = p.outcomes;
    std::size_t won      = 0;
    std::vector<const PuzzleRun::Outcome*> missed;
    for (const auto& o : outcomes) {
        if (o.win)
            ++won;
        else
            missed.push_back(&o);
    }
    ui::inlineIcon(ui::Icon::Check, ui::GOOD);
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.35f);
    if (outcomes.empty())
        ImGui::TextUnformatted(p.stopped ? TR("Stopped before the first answer") : TR("No puzzle to solve now"));
    else
        ImGui::TextUnformatted(trn("{} puzzle in {}", "{} puzzles in {}", outcomes.size(),
                                   ui::exactCount(outcomes.size()), ui::formatDuration(p.took))
                                   .c_str());
    ImGui::PopFont();
    if (p.stopped && p.total > outcomes.size() && p.source == PuzzleRun::Source::Online)
        wrapped(trn("Stopped: {} puzzle of the batch waits for the next time.",
                    "Stopped: {} puzzles of the batch wait for the next time.", p.total - outcomes.size(),
                    p.total - outcomes.size()),
                true);
    if (!outcomes.empty()) {
        ImGui::Dummy(ImVec2(0, px(4)));
        bigCount(ui::exactCount(won), TR("right at the first try"));
        ImGui::SameLine(0, px(30));
        bigCount(ui::exactCount(missed.size()),
                 p.source == PuzzleRun::Source::Review ? TR("missed again") : TR("missed, to review tomorrow"));
        if (p.source == PuzzleRun::Source::Offline) {
            const auto now = puzzleRating().glicko.rating;
            ImGui::SameLine(0, px(30));
            bigCount(
                fmt::format("{:+}", static_cast<int>(std::lround(now)) - static_cast<int>(std::lround(p.rating_start))),
                trf("your rating: {}", static_cast<int>(std::lround(now))));
        }
    }
    if (!missed.empty()) {
        ImGui::Dummy(ImVec2(0, px(6)));
        ImGui::TextUnformatted(TR("The missed"));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("· a click shows the position and the solution"));
        ui::beginCard("##puzzles_missed");
        for (const auto* o : missed) {
            const auto& puzzle = p.puzzles[o->puzzle];
            const auto play    = puzzlePlay(puzzle);
            if (!play) continue;
            const auto question = boardOf(*play, 0);
            const auto label    = fmt::format("{} · {}", mainTheme(puzzle.themes, p.title),
                                              ui::exactCount(static_cast<uint64_t>(puzzle.rating)));
            std::string yours   = localSan(o->played);
            try {
                if (const auto m = chess::uci::parseSan(question, o->played); m != chess::Move::NO_MOVE)
                    yours = numbered(question, m);
            } catch (const std::exception&) {
            }
            const auto said =
                o->played.empty()
                    ? trf("I don't know · the solution: {}", numbered(question, play->solution.front()))
                    : trf("you: {} · the solution: {}", yours, numbered(question, play->solution.front()));
            ImGui::PushID(static_cast<int>(o->puzzle));
            if (ImGui::Selectable(label.c_str(), p.missed_shown == static_cast<int>(o->puzzle))) {
                p.missed_shown = static_cast<int>(o->puzzle);
                p.current      = o->puzzle;
                p.play         = play;
                p.step         = 0;
                showPuzzlePosition(p.current, 0);
                board_.flip = play->before.sideToMove() == chess::Color::WHITE;
            }
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(10),
                                     ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(said.c_str()).x));
            ImGui::TextDisabled("%s", said.c_str());
            ImGui::PopID();
        }
        ui::endCard();
    }
    // Online: where the batch's results are.
    if (p.source == PuzzleRun::Source::Online) {
        ImGui::Dummy(ImVec2(0, px(4)));
        const auto* batch = onlineBatch();
        if (online_send_.valid())
            wrapped(TR("Sending the batch's answers to Lichess…"), true);
        else if (!online_error_.empty())
            wrapped(trf("Lichess has not taken the batch's answers yet ({}): they are kept, and sent again by "
                        "themselves.",
                        online_error_),
                    true);
        else if (batch && batch->sent_status == "accepted" && batch->results.empty() && batch->sent_at > 0)
            wrapped(trf("Lichess took your {} answers; the next batch is here: {} puzzles.", batch->sent_count,
                        batch->puzzles.size()),
                    true);
        else if (batch && !batch->complete())
            wrapped(trn("{} puzzle of the batch left: its answers go to Lichess once it is done.",
                        "{} puzzles of the batch left: their answers go to Lichess once it is done.", batch->left(),
                        batch->left()),
                    true);
    }
    ImGui::Dummy(ImVec2(0, px(6)));
    if (review_running_) {  // the day's review: the next kind, or back to the Training page
        drawReviewNext(false);
        return;
    }
    if (!missed.empty() && !p.practice) {
        if (primaryButton(trn("Review {} now", "Review the {} now", missed.size(), missed.size()).c_str()))
            return reviewMissedPuzzles();
        ImGui::SameLine();
        if (ImGui::Button(TR("Back to the Training page"))) return closePuzzles(true);
        ImGui::SameLine(0, px(8));
        ImGui::TextDisabled("%s", TR("Enter: review them · Esc: back"));
    } else {
        if (primaryButton(TR("Back to the Training page"))) return closePuzzles(true);
        ImGui::SameLine(0, px(8));
        ImGui::TextDisabled("%s", TR("Enter"));
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    wrapped(p.source == PuzzleRun::Source::Online
                ? TR("A series stopped midway keeps its answers; the batch's results go to Lichess at its end.")
                : TR("A series stopped midway keeps its answers. The missed ones come back in “To review today” "
                     "(FSRS): tomorrow, then further apart when they are right."),
            true);
}

}  // namespace fcg
