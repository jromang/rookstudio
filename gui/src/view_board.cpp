// The Board page: a game or a position, from a tournament, a PGN file or the clipboard, analysed by an engine of the
// library. Key A on every board opens its position here.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>

#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_internal.h>  // SetKeyOwner
#include <implot.h>

#include <fmt/include/fmt/format.h>

#include <matchmaking/syzygy.hpp>
#include <study/pgn_writer.hpp>
#include <tour/chess_text.hpp>

#include "app.hpp"
#include "assistant_notes.hpp"
#include "board_text.hpp"
#include "console.hpp"
#include "i18n.hpp"
#include "notation_view.hpp"
#include "piece_letters.hpp"
#include "review_bridge.hpp"
#include "review_stats.hpp"
#include "run.hpp"
#include "storage.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

// The nodes of the Chess space's review in each position (#61): "320,000 nodes per position".
std::string reviewNodesText(uint64_t nodes) {
    return trn("{} node per position", "{} nodes per position", nodes, ui::exactCount(nodes));
}

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// The ids of the notation's comments, above those of its moves ((line + 1) * 100000 + ply).
constexpr int kCommentIds = 1 << 30;

// The best line's first move strong, the others lighter.
ImU32 arrowColour(int rank) {
    static constexpr int alpha[] = {215, 130, 95, 75};
    return IM_COL32(30, 110, 200, alpha[std::min(rank, 3)]);
}

// "+0.42", "#5", "#-3", from White's point of view, with ≥ or ≤ for a bound.
std::string whiteScoreText(const AnalysisLine& line, chess::Color stm) {
    if (!line.score) return "";
    auto text = ui::formatScore(whiteScore(*line.score, stm));
    // A lower bound of the side to move is a lower bound for White when White moves.
    const bool white = stm == chess::Color::WHITE;
    if (line.lowerbound) text = (white ? "≥" : "≤") + text;
    if (line.upperbound) text = (white ? "≤" : "≥") + text;
    return text;
}

// The eval bar's share for White, from a score of the side to move.
float barFraction(const fastchess::Score& score, chess::Color stm) {
    const auto white = whiteScore(score, stm);
    if (white.type == fastchess::ScoreType::MATE) return evalBarFraction(0, true, white.value >= 0 ? 1 : -1);
    return evalBarFraction(static_cast<int>(white.value), false, 0);
}

ImU32 judgementBadgeColour(Judgement judgement) {
    switch (judgement) {
        case Judgement::Inaccuracy:
            return IM_COL32(212, 169, 21, 255);
        case Judgement::Mistake:
            return IM_COL32(226, 116, 31, 255);
        default:
            return IM_COL32(200, 45, 40, 255);
    }
}

ImU32 judgementColour(Judgement judgement) {
    switch (judgement) {
        case Judgement::Inaccuracy:
            return IM_COL32(200, 160, 0, 255);
        case Judgement::Mistake:
            return ui::WARN;
        default:
            return ui::BAD;
    }
}

// The colour of each analysis on the graph and the board: the judging one in the text's, the others apart.
ImU32 analysisColour(std::size_t index) {
    if (index == 0) return ImGui::GetColorU32(ImGuiCol_Text);
    const bool light          = ui::currentTheme() == ui::Theme::Light;
    static const ImU32 lit[]  = {IM_COL32(138, 79, 214, 255), IM_COL32(0, 140, 128, 255), IM_COL32(205, 45, 125, 255),
                                 IM_COL32(120, 120, 0, 255), IM_COL32(150, 90, 40, 255)};
    static const ImU32 dark[] = {IM_COL32(180, 140, 255, 255), IM_COL32(80, 205, 190, 255),
                                 IM_COL32(245, 120, 185, 255), IM_COL32(205, 205, 90, 255),
                                 IM_COL32(215, 160, 110, 255)};
    return (light ? lit : dark)[(index - 1) % 5];
}

// A player's colour: a white or a black disc, whatever the theme.
void playerDot(ImDrawList* dl, ImVec2 c, float r, chess::Color side) {
    // In the dark theme, Black's disc a little lighter than the page, or it would read as an empty ring.
    const bool white = side == chess::Color::WHITE, dark = ui::currentTheme() == ui::Theme::Dark;
    const ImU32 fill = white  ? IM_COL32(246, 247, 249, 255)
                       : dark ? IM_COL32(62, 67, 77, 255)
                              : IM_COL32(24, 26, 31, 255);
    const ImU32 edge = white ? (dark ? IM_COL32(246, 247, 249, 255) : IM_COL32(70, 76, 88, 255))
                             : (dark ? IM_COL32(120, 127, 140, 255) : IM_COL32(24, 26, 31, 255));
    dl->AddCircleFilled(c, r, fill);
    dl->AddCircle(c, r, edge, 0, px(1.5f));
}

// chess::Board has virtual functions but no virtual destructor, which clang warns about when an optional destroys
// one; a final class of it is destroyed knowing its type.
struct LineBoard final : chess::Board {
    explicit LineBoard(const chess::Board& board) : chess::Board(board) {}
};

}  // namespace

std::string localSan(std::string san) { return sanIn(san, i18n::language()); }

std::string localLine(const std::string& line) {
    // Each move of "12.Nf3 Nc6 13.Be2", its number kept.
    std::string out, word;
    const auto flush = [&] {
        if (word.empty()) return;
        const auto dot = word.find_last_of('.');
        out += dot == std::string::npos ? localSan(word) : word.substr(0, dot + 1) + localSan(word.substr(dot + 1));
        word.clear();
    };
    for (const char c : line) {
        if (c == ' ') {
            flush();
            out += ' ';
        } else {
            word += c;
        }
    }
    flush();
    return out;
}

void App::loadBoardSettings() {
    if (board_.settings_loaded) return;
    board_.settings_loaded = true;
    if (const auto j = readJson<nlohmann::json>(storage_.root() / "board.json")) {
        try {
            board_.settings = j->get<BoardSettings>();
        } catch (...) {
        }
    }
}

void App::saveBoardSettings() { (void)writeJson(storage_.root() / "board.json", board_.settings); }

bool App::boardKey() {
    if (board_key_taken_) return false;
    const auto& io = ImGui::GetIO();
    // A, or Alt+A in the Chess space, where A begins a move.
    if (io.WantTextInput || io.KeyCtrl || io.KeySuper || io.KeyAlt != chessBoard()) return false;
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return false;
    if (!ImGui::IsKeyPressed(ImGuiKey_A, false)) return false;
    board_key_taken_ = true;
    return true;
}

std::string App::boardFen() { return boardAt(board_.game, board_.chess960, board_.ply).getFen(); }

int App::tournamentThreads() const {
    int threads = 0;
    for (const auto& r : runs_) {
        const auto* t = r.run->tournament();
        if (!r.run->running() || !t) continue;
        // A tournament in a process of its own tells what its games in flight take.
        if (r.run->remote()) {
            threads += r.run->inFlight().threads;
            continue;
        }
        // The two engines of a game take turns: the larger of their thread counts, for each game played.
        int per_game = 1;
        for (const auto& p : t->spec().participants) per_game = std::max(per_game, configThreads(p.engine));
        threads += r.run->runningGames() * per_game;
    }
    return threads;
}

void App::openOnBoard(ReplayGame game, int ply, bool chess960, std::string title, bool analyse) {
    if (training_) dropTraining();  // another game on the board: the session ends there, its answers kept (#76)
    if (puzzle_) dropPuzzles();     // so does a series of puzzles (#80)
    if (mistake_) dropMistakes();   // a session of exercises, a game guessed (#81)
    if (guess_) dropGuess();
    if (endgame_) dropEndgame();  // an endgame against the tables, a vision exercise (#82)
    if (vision_) dropVision();
    rememberBoardGame();            // its changes and undo history go with it
    // The questions were about the game left: those still being answered stop at their next request, out of sight;
    // so do the comments being written, which go nowhere.
    leaveQuestions();
    if (comment_job_) *comment_job_->cancel = true;
    ++board_game_serial_;
    board_broadcast_.reset();  // another game: the broadcast's, if it is one, is followed again by its caller
    board_tv_.reset();         // and Lichess TV's (#86)
    board_origin_.reset();
    board_.undo.clear();
    board_.redo.clear();
    board_.start_notes = {};
    board_.comment_edit.reset();
    board_.changes = {};
    board_.sent.reset();  // another game's chapter, if it was sent (#79)
    board_.since_sent = {};
    board_.toast.clear();  // what it said was about the game left
    board_.toast_time = -100;
    if (page_ != Page::Board) {
        board_back_ = BoardBack{page_, page_ == Page::Tournament ? std::optional<Tab>(shown_tab_) : std::nullopt};
    }
    if (game.start_fen.empty()) game.start_fen = std::string(chess::constants::STARTPOS);
    board_.game     = std::move(game);
    board_.tree     = GameTree{board_.game, {}};
    board_.line     = -1;
    board_.chess960 = chess960;
    board_.title    = std::move(title);
    board_.ply      = std::clamp(ply, 0, static_cast<int>(board_.game.moves.size()));
    board_.followed = -1;
    board_.selected.reset();
    board_.promotion.reset();
    board_.message.clear();
    board_.new_game = true;
    board_.watching = false;
    if (online_game_) online_game_->shown = false;  // a game on Lichess goes on meanwhile (#84)
    bot_shown_ = false;                             // and the Lichess bot's (#85)
    page_ = Page::Board;
    if (analyse) setBoardAnalysis(true);
}

void App::openBoardPgnDialog() {
    page_ = Page::Board;
    dialogs_.openFile(Dialogs::Purpose::BoardPgn, {{"PGN", "pgn"}}, storage_.runsDir().string());
}

void App::newBoardGame() { openPositionOnBoard(std::string(chess::constants::STARTPOS), false, {}, false); }

void App::openPositionOnBoard(const std::string& fen, bool chess960, std::string title, bool analyse) {
    ReplayGame game;
    game.start_fen = fen;
    openOnBoard(std::move(game), 0, chess960, std::move(title), analyse);
}

void App::analyseLiveGame(int slot) {
    if (slot < 0 || slot >= static_cast<int>(slot_views_.size())) return;
    const auto& st = slot_views_[static_cast<std::size_t>(slot)].state;
    if (!st.has_game) return;
    const auto* t = run_->tournament();
    auto game     = replayOfSlot(st);
    if (t) game.headers.emplace_back("Event", t->spec().name);
    const bool chess960 = t && t->spec().conditions.variant == fastchess::VariantType::FRC;
    // At the position the board shows: an earlier one when it was stepped back.
    const int ply = slot_views_[static_cast<std::size_t>(slot)].nav.shown(static_cast<int>(game.moves.size()));
    auto title    = fmt::format("{} – {}", st.white, st.black);
    if (t) title += " · " + t->spec().name;
    title += " · " + trf("game {}", st.game_id);
    openOnBoard(std::move(game), ply, chess960, std::move(title), true);
}

void App::analyseReplay() {
    if (!replay_ || selected_game_ < 0 || selected_game_ >= static_cast<int>(games_.size())) return;
    const auto* t       = run_->tournament();
    const bool chess960 = t && t->spec().conditions.variant == fastchess::VariantType::FRC;
    auto title          = fmt::format("{} – {}", replay_->header("White"), replay_->header("Black"));
    if (t) title += " · " + t->spec().name;
    title += " · " + trf("game {}", games_[static_cast<std::size_t>(selected_game_)].game_id);
    openOnBoard(*replay_, replay_ply_, chess960, std::move(title), true);
}

void App::setBoardAnalysis(bool on) {
    loadBoardSettings();
    if (!on) {
        board_.analysing = false;
        board_.analyser->halt();
        return;
    }
    if (aidsLocked()) {  // fair play (#48)
        board_.message       = TR("Locked while one of your games is played on Lichess.");
        board_.message_error = false;
        return;
    }
    if (engineGameHidesAids()) {  // a game against an engine (#83)
        boardToast(TR("Hidden until the end of your game: Show them first, and the game is marked “with aids”"));
        return;
    }
    auto& s = board_.settings.analysis;
    // The engine chosen, or the first that works.
    const EngineEntry* engine = nullptr;
    for (const auto& e : engines_) {
        if (e.name == s.engine) engine = &e;
    }
    if (!engine) {
        for (const auto& e : engines_) {
            if (!e.inactive && engineWorks(e)) {
                engine = &e;
                break;
            }
        }
    }
    if (!engine && !engines_.empty()) engine = &engines_.front();
    if (!engine) {
        board_.message       = TR("No engine to analyse with: add one on the Engines page.");
        board_.message_error = true;
        return;
    }
    if (s.engine != engine->name) {
        s.engine = engine->name;
        saveBoardSettings();
    }
    if (!board_.analyser->active() || board_.analyser->engine() != engine->name) {
        board_.analyser->start(*engine, firstEngineSettings());
        board_.new_game = true;
    }
    // The engines beside it that failed try again (#78).
    for (auto& a : board_.side)
        if (a && a->state() == Analyser::State::Failed) a->stop();
    board_.analysing = true;
}

std::optional<AnalysisPosition> App::threatPosition() const {
    const auto& b = board_;
    auto board    = boardAt(b.game, b.chess960, b.ply);
    if (board.inCheck() || board.isGameOver().second != chess::GameResult::NONE) return std::nullopt;
    board.makeNullMove();
    return AnalysisPosition{board.getFen(), {}, b.chess960};
}

void App::setBoardThreat(bool on) {
    auto& b = board_;
    if (!on) {
        b.threat_on = false;
        b.threat.halt();
        return;
    }
    const auto* engine = analysisEngine();
    if (!engine) {
        b.message       = TR("No engine to analyse with: add one on the Engines page.");
        b.message_error = true;
        return;
    }
    if (aidsLocked()) {  // fair play (#48)
        b.message       = TR("Locked while one of your games is played on Lichess.");
        b.message_error = false;
        return;
    }
    if (engineGameHidesAids()) {  // a game against an engine (#83)
        boardToast(TR("Hidden until the end of your game: Show them first, and the game is marked “with aids”"));
        return;
    }
    if (!b.threat.active() || b.threat.engine() != engine->name) b.threat.start(*engine, threatSettings());
    b.threat_on = true;
}

AnalysisSettings App::firstEngineSettings() const {
    auto s = board_.settings.analysis;
    if (!chessBoard()) {
        s.threads = std::max(1, s.threads);
        return s;
    }
    // In the study Board, its share of the analysis's threads, beside the others (#78).
    if (const auto threads = sideThreads(); !threads.empty()) s.threads = threads.front();
    return s;
}

AnalysisSettings App::threatSettings() const {
    auto one    = firstEngineSettings();
    one.multipv = 1;
    return one;
}

void App::unloadBoardEngines() {
    auto& b     = board_;
    b.analysing = false;
    b.threat_on = false;
    b.analyser->stop();
    for (auto& a : b.side)
        if (a) a->stop();
    b.threat.stop();
}

void App::pollBoard() {
    board_key_taken_ = false;
    board_.analyser->poll();
    for (auto& a : board_.side)
        if (a) a->poll();
    board_.threat.poll();
    if (board_.new_game) board_.side_new_game = true;
    // Fair play (#48): no engine on a locked position; while a live game lasts, none on any, the assistant's work
    // stopped too (a review's passes done are kept, for later).
    if (aidsLocked()) {
        if (board_.analysing || board_.threat_on) {
            board_.message       = TR("Locked while one of your games is played on Lichess.");
            board_.message_error = false;
        }
        if (board_.analysing) setBoardAnalysis(false);
        board_.threat_on = false;
    }
    if (engineGameHidesAids()) {  // a game against an engine, its aids hidden until its end (#83)
        if (board_.analysing) setBoardAnalysis(false);
        board_.threat_on = false;
    }
    if (board_.reviewing() &&
        (liveGameOn() || correspondencePly(board_.review_game, board_.review_chess960, INT32_MAX) >= 0))
        cancelGameReview();
    if (liveGameOn() && comment_job_ && comment_job_->cancel) *comment_job_->cancel = true;
    if (liveGameOn() && story_job_) *story_job_->cancel = true;  // fair play (#48): the story waits too
    if (board_.threat_on && board_.threat.state() == Analyser::State::Failed) board_.threat_on = false;
    if (board_.threat_on && page_ == Page::Board && !board_.editor && !trainingHidesAids()) {
        board_.threat.update(threatSettings());
        if (const auto position = threatPosition())
            board_.threat.analyse(*position);
        else
            board_.threat.halt();
    } else if (board_.threat.state() == Analyser::State::Searching) {
        board_.threat.halt();
    }
    pollBoardPgn();
    pollBoardGames();
    // The first engine failing ends the analysis, unless others search beside it (#78): its card says why.
    if (board_.analysing && board_.analyser->state() == Analyser::State::Failed &&
        !(chessBoard() && analysisEngines(board_.settings).size() > 1))
        board_.analysing = false;
    // A game analysed in passes runs on its own thread; the board's analysis (A) pauses it.
    auto& b = board_;
    pollGameReview();
    // The analysis follows the board while the page is shown, and waits meanwhile.
    // A question of the repertoire's training hides the engines: they wait for the answer (#76).
    const bool shown    = b.analysing && page_ == Page::Board && !b.editor && !trainingHidesAids();
    const auto position = analysisPosition(b.game, b.chess960, b.ply);
    if (shown) {
        if (!(b.analyser->position() == position)) b.review_offered = 0;
        // Its threads as the Board has them: the analysis's, or its share beside the others (#78).
        if (b.analyser->active() && b.analyser->engine() == b.settings.analysis.engine)
            b.analyser->update(firstEngineSettings());
        b.analyser->analyse(position, {}, b.new_game);
        b.new_game = false;
    } else if (b.analyser->state() == Analyser::State::Searching) {
        b.analyser->halt();
    }
    // The study Board's other engines search beside it (#78); elsewhere they wait, or end once out of the list.
    {
        const auto list    = analysisEngines(b.settings);
        const auto threads = sideThreads();
        std::vector<SideRun> runs;
        for (std::size_t i = 1; i < list.size(); ++i)
            runs.push_back({engineNamed(list[i].engine), {list[i].engine, threads[i], list[i].hash, list[i].multipv}});
        const bool search = shown && chessBoard() && !aidsLocked();
        runSideBySide(b.side, runs, search, position, search && b.side_new_game);
        if (search) b.side_new_game = false;
    }
    if (!shown) return;
    if (chessBoard()) keepSideTrails(position);
    // A position of the game analysed in passes, searched for longer than its pass did: its value offered.
    const auto& a               = *b.analyser;
    BoardPage::ReviewTask* same = nullptr;
    for (auto& t : b.review_tasks)
        if (t.engine == a.engine()) same = &t;
    if (same && b.line < 0 && b.review_game_key == b.review_key && a.position() == position && !a.lines().empty() &&
        a.lines().front().score && !a.lines().front().pv.empty()) {
        const auto& line = a.lines().front();
        const auto ply   = static_cast<std::size_t>(b.ply);
        // Once a second at most, and when it went further than what the analysis has.
        if (ply < same->state.live.positions.size() &&
            static_cast<int64_t>(line.time_ms) > same->state.live.positions[ply].time_ms &&
            static_cast<int64_t>(line.time_ms) >= b.review_offered + 1000) {
            b.review_offered = static_cast<int64_t>(line.time_ms);
            PositionEval value;
            value.score   = line.score;
            value.best    = line.pv.front();
            value.depth   = static_cast<int>(line.depth);
            value.time_ms = static_cast<int64_t>(line.time_ms);
            value.wdl     = line.wdl;
            for (const auto& m : line.pv) value.pv += (value.pv.empty() ? "" : " ") + m;
            same->runner->offer(b.ply, std::move(value));
        }
    }
}

// ---------------------------------------------------------------------------
// A game analysed move by move

const EngineEntry* App::analysisEngine() {
    loadBoardSettings();
    auto& s = board_.settings.analysis;
    for (const auto& e : engines_) {
        if (e.name == s.engine) return &e;
    }
    for (const auto& e : engines_) {
        if (!e.inactive && engineWorks(e)) {
            s.engine = e.name;
            saveBoardSettings();
            return &e;
        }
    }
    if (!engines_.empty()) {
        s.engine = engines_.front().name;
        saveBoardSettings();
        return &engines_.front();
    }
    return nullptr;
}

ImU32 App::reviewColour(std::size_t index, const std::string& engine) const {
    // The study Board: an engine of the analysis side by side keeps its colour there (#78); the judge is the area.
    if (chessBoard() && index > 0) {
        const auto list = analysisEngines(board_.settings);
        for (std::size_t k = 0; k < list.size(); ++k)
            if (list[k].engine == engine) return sideColour(k);
    }
    return analysisColour(index);
}

void App::startReviewTask(const EngineEntry& engine, const std::vector<GameReview>& kept) {
    auto& b = board_;
    ReviewRunner::Setup setup;
    setup.engine     = engine;
    setup.settings   = b.settings.analysis;
    setup.game       = b.review_game;
    setup.chess960   = b.review_chess960;
    setup.ceiling_ms = b.settings.review_ceiling_ms;
    // The Chess space: Lichess's evaluations kept offline, deep enough, taken as they are (#58); each position searched
    // for a number of nodes from an empty hash, so that the same game gets the same review, by several engine
    // processes side by side (#61): as many as the analysis has threads, at least half the cores up to four, fewer
    // beside tournaments; the engine's second line where a move can be the only one (#63). The Engines space's review
    // does not change.
    if (chessBoard()) {
        setup.known        = reviewKnown();
        setup.reproducible = true;
        setup.processes    = reviewProcesses(std::max(1, b.settings.analysis.threads), topology_.physical,
                                             topology_.logical, tournamentCores());
        setup.second_line  = secondLines(b.review_game, b.review_chess960);
    }
    // Resumed after the passes this engine completed the same way, unless it went as far as asked: then again from the
    // start.
    for (const auto& r : kept) {
        const bool same_way = setup.reproducible ? r.passes > 0 && r.nodes == passNodes(r.passes - 1) : r.nodes == 0;
        if (r.engine == engine.name && r.passes > 0 && r.complete(b.review_game.moves.size()) &&
            r.passes < reviewPasses(setup.ceiling_ms) && same_way)
            setup.resume = r;
    }
    BoardPage::ReviewTask task;
    task.engine = engine.name;
    task.saved  = setup.resume.passes;
    task.runner = std::make_unique<ReviewRunner>();
    task.runner->start(std::move(setup));
    (void)task.runner->state(task.state, task.version);
    b.review_tasks.push_back(std::move(task));
}

void App::startGameReview() {
    auto& b = board_;
    if (b.reviewing() || b.tree.main.moves.empty() || (b.watching && b.match && b.match_state.running)) return;
    if (gameLocked()) {  // fair play (#48)
        b.message       = TR("Locked while one of your games is played on Lichess.");
        b.message_error = false;
        return;
    }
    if (engineGameHidesAids()) {  // a game against an engine, kept waiting for Resume (#83)
        boardToast(TR("Hidden until the end of your game: Show them first, and the game is marked “with aids”"));
        return;
    }
    // Every analysis the game has goes on; without one, the analysis engine's.
    const auto kept = loadReviews(storage_.root() / "analyses", b.tree.main);
    std::vector<const EngineEntry*> engines;
    for (const auto& r : kept) {
        const auto it =
            std::find_if(engines_.begin(), engines_.end(), [&](const auto& e) { return e.name == r.engine; });
        if (it != engines_.end()) engines.push_back(&*it);
    }
    if (engines.empty()) {
        const auto* engine = analysisEngine();
        if (!engine) {
            b.message       = TR("No engine to analyse with: add one on the Engines page.");
            b.message_error = true;
            return;
        }
        engines.push_back(engine);
    }
    b.review_game     = b.tree.main;
    b.review_chess960 = b.chess960;
    b.review_game_key = reviewKey(b.tree.main);
    for (const auto* e : engines) startReviewTask(*e, kept);
    b.message.clear();
}

void App::addReviewEngine(const std::string& name) {
    auto& b       = board_;
    const auto it = std::find_if(engines_.begin(), engines_.end(), [&](const auto& e) { return e.name == name; });
    if (it == engines_.end() || b.tree.main.moves.empty()) return;
    if (gameLocked()) {  // fair play (#48)
        b.message       = TR("Locked while one of your games is played on Lichess.");
        b.message_error = false;
        return;
    }
    // The analyses of another game end first, their passes kept.
    if (b.reviewing() && b.review_game_key != reviewKey(b.tree.main)) cancelGameReview();
    if (std::any_of(b.review_tasks.begin(), b.review_tasks.end(), [&](const auto& t) { return t.engine == name; }))
        return;
    const auto kept = loadReviews(storage_.root() / "analyses", b.tree.main);
    if (!b.reviewing()) {
        b.review_game     = b.tree.main;
        b.review_chess960 = b.chess960;
        b.review_game_key = reviewKey(b.tree.main);
        // The game's other analyses go on with it, so that they stay comparable.
        for (const auto& r : kept) {
            const auto e =
                std::find_if(engines_.begin(), engines_.end(), [&](const auto& x) { return x.name == r.engine; });
            if (e != engines_.end() && r.engine != name) startReviewTask(*e, kept);
        }
    }
    startReviewTask(*it, kept);
}

void App::judgeWith(const std::string& engine) {
    auto& b       = board_;
    auto kept     = loadReviews(storage_.root() / "analyses", b.tree.main);
    const auto it = std::find_if(kept.begin(), kept.end(), [&](const auto& r) { return r.engine == engine; });
    if (it == kept.end() || it == kept.begin()) return;
    std::rotate(kept.begin(), it, it + 1);
    (void)saveReviews(storage_.root() / "analyses", b.tree.main, kept);
    b.review_key.clear();
}

void App::removeReview(const std::string& engine) {
    auto& b = board_;
    if (b.review_game_key == reviewKey(b.tree.main)) {
        for (auto it = b.review_tasks.begin(); it != b.review_tasks.end(); ++it) {
            if (it->engine != engine) continue;
            it->runner->stop();
            b.review_tasks.erase(it);
            break;
        }
    }
    auto kept = loadReviews(storage_.root() / "analyses", b.tree.main);
    std::erase_if(kept, [&](const auto& r) { return r.engine == engine; });
    (void)saveReviews(storage_.root() / "analyses", b.tree.main, kept);
    b.review_key.clear();
}

void App::cancelGameReview() {
    auto& b = board_;
    for (auto& t : b.review_tasks) t.runner->stop();
    pollGameReview();  // their passes done kept
    b.review_tasks.clear();
}

void App::pollGameReview() {
    auto& b = board_;
    if (b.review_tasks.empty()) return;
    // Together pass by pass, so that their curves compare: none starts a pass before the others finished theirs; all
    // at once when the cores allow, else one after the other, the judging one first.
    int lowest = 1 << 20;
    for (const auto& t : b.review_tasks)
        if (t.state.running) lowest = std::min(lowest, t.state.pass);
    int needed = tournamentThreads();
    for (const auto& t : b.review_tasks) needed += t.state.running ? std::max(1, b.settings.analysis.threads) : 0;
    const bool one_at_a_time = b.review_tasks.size() > 1 && needed > std::max(1, topology_.logical);
    bool given               = false;
    for (auto& t : b.review_tasks) {
        auto& r = *t.runner;
        r.setFirst(b.review_game_key == b.review_key ? boardMainPly() : -1);
        r.setCeiling(b.settings.review_ceiling_ms);
        r.setPassLimit(lowest);
        bool paused = b.analysing;
        if (one_at_a_time && t.state.running) {
            if (t.state.pass == lowest && !given)
                given = true;
            else
                paused = true;
        }
        r.setPaused(paused);
        if (!r.state(t.state, t.version)) continue;
        // A pass completed: kept with the game, and read again if it is the game shown.
        if (t.state.settled.passes > t.saved) {
            (void)saveReview(storage_.root() / "analyses", b.review_game, t.state.settled);
            t.saved = t.state.settled.passes;
            if (b.review_key == b.review_game_key) b.review_key.clear();
        }
        if (!t.state.running && !t.state.error.empty()) {
            b.message       = t.state.error;
            b.message_error = true;
        }
    }
    std::erase_if(b.review_tasks, [](const auto& t) { return !t.state.running; });
}

void App::boardPlay(chess::Move move) {
    // A move where the game went another way starts a variation; the game stays as it came.
    auto& b                = board_;
    const auto moves_count = [&] {
        std::size_t n = b.tree.main.moves.size();
        for (const auto& v : b.tree.variations) n += v.moves.size() + 1;
        return n;
    };
    const std::size_t was = moves_count(), variations = b.tree.variations.size();
    std::optional<BoardPage::Snapshot> before;
    if (chessBoard()) before = boardSnapshot();
    const auto [line, p] = playInTree(b.tree, b.chess960, b.line, b.ply, move);
    b.game               = lineOf(b.tree, line);
    showBoardLine(line, p);
    if (before && moves_count() != was) {
        boardChanged(std::move(*before));
        if (b.tree.variations.size() > variations)
            boardToast(TR("Variation added · Ctrl+Z takes it away · Alt+↑ makes it the main line"));
    }
}

void App::boardPlayAsked(chess::Move move) {
    if (training_) return trainingAnswer(move);  // the answer to the training's question (#76)
    if (puzzle_) return puzzleAnswer(move);      // to the puzzle's (#80)
    if (mistake_) return mistakeAnswer(move);    // to the exercise's, the guess (#81)
    if (guess_) return guessAnswer(move);
    if (endgame_) return endgameAnswer(move);  // to the endgame's, the vision's (#82)
    if (vision_) return visionAnswer(move);
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

// The colour of the mark the right button draws: green; Shift (or Ctrl) red, Alt blue, both yellow, as on Lichess.
char markColourKey() {
    const auto& io = ImGui::GetIO();
    if (io.KeyShift && io.KeyAlt) return 'Y';
    if (io.KeyShift || io.KeyCtrl) return 'R';
    if (io.KeyAlt) return 'B';
    return 'G';
}

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

void App::boardMark(chess::Square from, chess::Square to) {
    auto& b           = board_;
    const auto before = boardSnapshot();
    MoveNotes* notes  = boardNotes();
    if (!notes) return;
    toggleMark(*notes, markColourKey(), from.index(), to.index());
    boardChanged(before);
    b.game = lineOf(b.tree, b.line);
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

void App::boardMove(chess::Move move) {
    auto& b = board_;
    if (b.humanTurn()) {
        b.match->play(chess::uci::moveToUci(move, b.chess960));
        b.selected.reset();
        return;
    }
    boardPlayAsked(move);
}

void App::boardClick(const chess::Board& board, chess::Square square) {
    // In a game the user plays: their pieces at their turn only, on the position of the game; a click elsewhere in
    // the game brings the board back to it.
    if (auto& b = board_; b.humanGame()) {
        const int last = static_cast<int>(b.game.moves.size());
        if (b.line >= 0 || b.ply != last) {
            showBoardLine(-1, last);
            return;
        }
        if (!b.humanTurn()) {
            b.selected.reset();
            return;
        }
    }
    // A game on Lichess (#84): its position only, the user's men only (during the opponent's turn too, in a live game:
    // a move made in advance).
    if (onlineShown()) {
        auto& b        = board_;
        const int last = static_cast<int>(b.game.moves.size());
        if (b.line >= 0 || b.ply != last) {
            showBoardLine(-1, last);
            return;
        }
    }
    // The Lichess bot's game (#85): its engine plays it; a click brings the board back to its move.
    if (botShown()) {
        auto& b        = board_;
        const int last = static_cast<int>(b.game.moves.size());
        if (b.line >= 0 || b.ply != last) showBoardLine(-1, last);
        b.selected.reset();
        return;
    }
    if (board_.selected) {
        std::vector<chess::Move> moves;
        for (const auto& m : movesFrom(board, *board_.selected)) {
            if (shownTarget(m) == square) moves.push_back(m);
        }
        if (moves.size() == 1) {
            boardMove(moves.front());
            return;
        }
        if (moves.size() > 1) {  // a promotion: the piece is chosen
            board_.promotion = moves.front();
            ImGui::OpenPopup("##promotion");
            return;
        }
    }
    const auto piece = board.at(square);
    const bool theirs =
        onlineShown() && piece != chess::Piece::NONE && (piece.color() == chess::Color::WHITE) != online_game_->white;
    if (piece != chess::Piece::NONE && piece.color() == board.sideToMove() && board_.selected != square &&
        !movesFrom(board, square).empty() && !theirs) {
        board_.selected = square;
    } else {
        board_.selected.reset();
    }
}

void App::boardPointer(const chess::Board& board, std::optional<chess::Square> press,
                       std::optional<chess::Square> hover, bool right) {
    auto& b = board_;
    if (right) {
        b.selected.reset();
        b.drag.reset();
        return;
    }
    if (press) {
        if (b.selected == *press) {
            b.drag = BoardPage::Drag{*press, false, true};
        } else {
            boardClick(board, *press);
            if (b.selected == *press)
                b.drag = BoardPage::Drag{*press, false, false};
            else
                b.drag.reset();
        }
    }
    if (!b.drag) return;
    b.drag_over = hover;
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) b.drag->moved = true;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
    // Released.
    const auto drag = *b.drag;
    b.drag.reset();
    b.drag_over.reset();
    if (drag.moved) {
        if (!hover || *hover == drag.from || b.selected != drag.from) return;
        const auto moves = movesFrom(board, drag.from);
        if (std::any_of(moves.begin(), moves.end(), [&](const chess::Move& m) { return shownTarget(m) == *hover; }))
            boardClick(board, *hover);
    } else if (drag.picked_before) {
        b.selected.reset();
    }
}

void App::drawBoardPage() {
    loadBoardSettings();
    if (training_) return drawTrainingPage();  // a session of the repertoire's training: the column is its (#76)
    if (puzzle_) return drawPuzzlePage();      // a series of puzzles: the column is the puzzle's (#80)
    if (mistake_) return drawMistakePage();    // a session of exercises, a game guessed (#81)
    if (guess_) return drawGuessPage();
    if (visit_) return drawVisitPage();      // the guided visit of the key moments (#94)
    if (endgame_) return drawEndgamePage();  // an endgame against the tables, a vision exercise (#82)
    if (vision_) return drawVisionPage();
    auto& b = board_;

    ui::pageTitle(TR("Board"), guideSection());
    // The tools, on the title's line: open, paste, set up a position, analyse.
    {
        const char* open    = TR("Open a PGN…");
        const char* paste   = TR("Paste");
        const char* edit    = TR("Edit the position");
        const char* play    = TR("Play a game…");
        const char* analyse = b.analysing ? TR("Stop the analysis") : TR("Analyse");
        const char* review  = b.reviewing() ? TR("Stop the game's analysis") : TR("Analyse the game");
        // Their keys: G and A, under Alt in the Chess space, where letters begin typed moves (g3, a4).
        const bool alt          = chessBoard();
        const char* review_key  = alt ? "Alt+G" : "G";
        const char* analyse_key = alt ? "Alt+A" : "A";
        float width             = 0;
        for (const char* label : {open, paste, edit, play, review, analyse})
            width += ImGui::CalcTextSize(label).x + px(44);
        width += ImGui::CalcTextSize("Ctrl+V").x +
                 (alt ? ImGui::CalcTextSize(review_key).x + ImGui::CalcTextSize(analyse_key).x
                      : ImGui::CalcTextSize("A").x * 2) +
                 ImGui::GetFrameHeight() + px(28);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(20), ImGui::GetContentRegionMax().x - width));
        ImGui::BeginDisabled(b.editor.has_value());
        if (ui::iconButton("board_open", ui::Icon::Folder, open)) {
            dialogs_.openFile(Dialogs::Purpose::BoardPgn, {{"PGN", "pgn"}}, storage_.runsDir().string());
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("Any PGN file: its games listed and filtered by their tags, each replayed "
                                       "here. A file can also be dropped on the window."));
        }
        ImGui::SameLine();
        if (ui::iconButton("board_paste", ui::Icon::Paste, paste)) pasteOnBoard();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("A FEN, a PGN or a list of moves (e4 e5 Nf3, or e2e4 e7e5) from the "
                                       "clipboard."));
        }
        ImGui::SameLine(0, px(4));
        ImGui::TextDisabled("Ctrl+V");
        ImGui::SameLine();
        if (ui::iconButton("board_edit", ui::Icon::Edit, edit)) openPositionEditor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("Set up a position: pieces, side to move, castling, en passant, Chess960."));
        }
        ImGui::SameLine();
        // Engines playing on from a position the fair play locks would be an aid (#48, #84).
        const bool engines_locked = chessBoard() && aidsLocked();
        ImGui::BeginDisabled(engines_locked);
        if (ui::iconButton("board_play", ui::Icon::Play, play)) b.play_open = !b.play_open;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", engines_locked
                                        ? TR("Locked while one of your games is played on Lichess.")
                                        : TR("A game, or two with the colours reversed, between two engines of the "
                                             "library from this position, with a clock for each."));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        // The game analysed in passes, up to the time per position chosen beside.
        const bool live_game =
            (b.watching && b.match && b.match_state.running) || engineGameHidesAids() || onlineShown() || botShown();
        ImGui::BeginDisabled(b.editor.has_value() || (!b.reviewing() && (b.game.moves.empty() || live_game)));
        if (ui::iconButton("board_review", b.reviewing() ? ui::Icon::Stop : ui::Icon::Chart, review))
            b.reviewing() ? cancelGameReview() : startGameReview();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", live_game ? TR("Once the game is over.")
                                    : alt ? TR("Alt+G: the analysis engine goes through every position of the game, in "
                                               "the background, then again further at each pass: the graph comes at "
                                               "once and sharpens. Its inaccuracies, mistakes and blunders are marked, "
                                               "the same ones each time.")
                                          : TR("G: the analysis engine goes through every position of the game, in "
                                               "the background, then again for longer at each pass: the graph comes "
                                               "at once and sharpens. Its inaccuracies, mistakes and blunders are "
                                               "marked."));
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0, px(2));
        if (ImGui::ArrowButton("##review_ceiling", ImGuiDir_Down)) ImGui::OpenPopup("##review_ceiling_menu");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("How far the analysis of the game goes."));
        if (ImGui::BeginPopup("##review_ceiling_menu")) {
            ImGui::TextDisabled("%s", TR("Refine the analysis of the game up to"));
            for (const int64_t ceiling : {int64_t{320}, int64_t{1280}, int64_t{5120}, int64_t{20480}, int64_t{0}}) {
                // The Chess space's passes are of nodes (#61): its last one, as many thousands as milliseconds.
                const auto label = ceiling <= 0 ? std::string(TR("until stopped"))
                                   : alt        ? reviewNodesText(passNodes(reviewPasses(ceiling) - 1))
                                                : trf("{} per position", ui::formatClock(ceiling));
                if (ImGui::MenuItem(label.c_str(), nullptr, b.settings.review_ceiling_ms == ceiling)) {
                    b.settings.review_ceiling_ms = ceiling;
                    saveBoardSettings();
                }
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine(0, px(4));
        ImGui::TextDisabled("%s", review_key);
        ImGui::SameLine();
        if (ui::iconButton("board_analyse", b.analysing ? ui::Icon::Stop : ui::Icon::Analyse, analyse))
            setBoardAnalysis(!b.analysing);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", alt ? TR("Alt+A: the engines analyse the position shown, and follow the board.")
                                        : TR("A: an engine of the library analyses the position."));
        ImGui::SameLine(0, px(4));
        ImGui::TextDisabled("%s", analyse_key);
    }

    // Keys, unless a text field has them.
    const bool keys = !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    const int total = static_cast<int>(b.game.moves.size());
    if (b.preview && b.preview->hover) {
        if (!b.preview_hovered) b.preview.reset();
        b.preview_hovered = false;
    }
    if (b.preview && !b.preview->hover && b.preview->at != b.ply) b.preview.reset();
    // In the Chess space: ! and ? read as typed whatever the keyboard's layout, SDL's text input on while the page has
    // the keys (a text field's own ends it, and it comes back after).
    if (chessBoard() && keys && !b.editor && !SDL_TextInputActive(window_)) SDL_StartTextInput(window_);
    if (keys && !b.editor) {
        // The arrows, Home and End are the board's. Claimed before they are pressed, ImGui's keyboard navigation
        // leaves them alone: its cursor no longer shows on the button last clicked (a page of the rail), which
        // Enter or Space would press then, in place of a comment.
        const auto owner = ImGui::GetID("##board_keys");
        for (const auto key : {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow,
                               ImGuiKey_Home, ImGuiKey_End})
            ImGui::SetKeyOwner(key, owner);
        if (chessBoard()) {  // Enter writes a comment there
            ImGui::SetKeyOwner(ImGuiKey_Enter, owner);
            ImGui::SetKeyOwner(ImGuiKey_KeypadEnter, owner);
        }
        const int before = b.ply;
        b.ply            = stepPly(navKeys(b.flip, !chessBoard()), b.ply, total);
        if (chessBoard() && ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_F)) b.flip = !b.flip;
        if (b.ply != before) b.preview.reset();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (b.preview)
                b.preview.reset();
            else if (b.selected)
                b.selected.reset();
            else if (boardBackToMosaic() && !linkCardTakesKeys() && !headers_open_ && !b.comment_edit)
                backToMosaic();  // a game followed from a mosaic: back to it (#86)
        }
        if (ImGui::IsKeyPressed(ImGuiKey_N, false) && !ImGui::GetIO().KeyCtrl &&
            ImGui::GetIO().KeyAlt == chessBoard()) {
            if (const int to = judgedPly(ImGui::GetIO().KeyShift ? -1 : 1); to >= 0) showBoardLine(-1, to);
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) pasteOnBoard();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) boardSave();
        if (chessBoard() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)) boardSaveMenu(true);
        // The position's diagram into the clipboard, from anywhere on the page (#93).
        if (chessBoard() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_C)) copyDiagram();
        if (chessBoard()) boardEditKeys();
    }
    if (keys && boardKey()) setBoardAnalysis(!b.analysing);
    // The letters are the Engines space's keys; the Chess space's under Alt, its letters beginning moves.
    const bool letter_keys = !ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyAlt == chessBoard();
    if (keys && !b.editor && ImGui::IsKeyPressed(ImGuiKey_X, false) && letter_keys) setBoardThreat(!b.threat_on);
    if (keys && !b.editor && ImGui::IsKeyPressed(ImGuiKey_G, false) && letter_keys) {
        if (b.reviewing())
            cancelGameReview();
        else
            startGameReview();
    }

    if (b.reading) {
        const auto done = static_cast<double>(b.reading->done->load());
        ImGui::TextDisabled("%s", trf("Reading {}…", b.reading->file.filename().string()).c_str());
        ImGui::SameLine();
        ImGui::ProgressBar(b.reading->size ? static_cast<float>(done / static_cast<double>(b.reading->size)) : 0.0f,
                           ImVec2(px(200), 0));
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Cancel"))) *b.reading->cancel = true;
    }

    // What the game is: its title, result, time control, opening and how it ended.
    {
        const auto& main = b.tree.main;
        std::string line = b.title;
        const auto add   = [&](const std::string& part) {
            if (!part.empty() && part != "?" && part != "-") line += (line.empty() ? "" : " · ") + part;
        };
        if (!main.result.empty() && main.result != "*") add(main.result);
        add(main.header("TimeControl"));
        add((main.header("ECO").empty() ? "" : main.header("ECO") + " ") + main.header("Opening"));
        if (!main.final_comment.empty() && main.final_comment.size() < 60) add(main.final_comment);
        if (!line.empty()) ImGui::TextDisabled("%s", line.c_str());
        if (board_broadcast_) {  // a broadcast's game, followed live (M4, board 4)
            if (!line.empty()) ImGui::SameLine();
            drawBoardBroadcastLine();
        }
    }
    if (!b.message.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, b.message_error ? ui::BAD : ui::MUTED);
        ImGui::TextWrapped("%s", b.message.c_str());
        ImGui::PopStyleColor();
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float side   = std::clamp(avail.x * 0.42f, px(340), px(620));
    const float under  = ImGui::GetTextLineHeightWithSpacing() * 2 + ImGui::GetFrameHeightWithSpacing() * 2 +
                         (boardStrips() ? ImGui::GetFrameHeightWithSpacing() * 2 : 0.0f);
    const float size   = std::floor(
        std::max(px(200), std::min(avail.x - side - ImGui::GetStyle().ItemSpacing.x - px(20), avail.y - under)));

    if (b.editor) {
        drawPositionEditor(size);
        return;
    }
    ImGui::BeginGroup();
    drawBoardColumn(size);
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginChild("##board_side", ImVec2(0, 0));
    drawLinkCard();             // a Lichess link pasted (#47)
    drawStudySendCard();        // the game sent to a Lichess study (#79)
    drawFinishedBanner(false);  // a game of the user's that ended on Lichess (#49)
    if (online_game_) drawOnlineGameCard();  // a game on Lichess played here (#84)
    if (bot_shown_) drawBotGameCard();       // the Lichess bot's game followed here (#85)
    if (board_tv_) drawTvBoardCard();        // a game of Lichess TV's followed here (#86)
    // A game against an engine of the Play page (#83); once it is over, the card of the games from here closes it.
    if (engine_game_ && engine_game_->over && b.play_open) closeEngineGame();
    if (engine_game_)
        drawEngineGameCard();
    else if (b.play_open || (b.match && b.match_state.running))
        drawBoardPlay();
    if (chessBoard()) {
        drawChessColumn();
    } else {
        drawBoardAnalysis();
        const float graph = boardGraphHeight();
        drawBoardMoves(std::max(px(160), ImGui::GetContentRegionAvail().y - graph - ImGui::GetStyle().ItemSpacing.y));
        drawBoardGraph(graph);
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Games from a file or the clipboard

void App::openBoardPgn(const std::filesystem::path& file) {
    auto& b = board_;
    if (b.reading) *b.reading->cancel = true;  // its result is dropped
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec) {
        b.message       = trf("Cannot read {}", file.string());
        b.message_error = true;
        page_           = Page::Board;
        return;
    }
    if (page_ != Page::Board) {
        board_back_ = BoardBack{page_, page_ == Page::Tournament ? std::optional<Tab>(shown_tab_) : std::nullopt};
    }
    page_ = Page::Board;
    PgnReading r;
    r.file   = file;
    r.size   = size;
    r.cancel = std::make_shared<std::atomic<bool>>(false);
    r.done   = std::make_shared<std::atomic<uint64_t>>(0);
    // Read in the background: a file of a rating list holds millions of games.
    r.result  = std::async(std::launch::async, [file, cancel = r.cancel, done = r.done] {
        std::ifstream in(file, std::ios::binary);
        return tour::indexPgn(in, cancel.get(), done.get());
    });
    b.reading = std::move(r);
}

void App::pollBoardPgn() {
    auto& b = board_;
    if (!b.reading || b.reading->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto index        = b.reading->result.get();
    const auto file   = b.reading->file;
    const bool cancel = *b.reading->cancel;
    b.reading.reset();
    if (cancel) return;
    if (index.games.empty()) {
        b.message       = trf("No game in {}.", file.filename().string());
        b.message_error = true;
        return;
    }
    PgnSource source;
    source.name  = file.filename().string();
    source.file  = file;
    source.index = std::make_shared<const tour::PgnIndex>(std::move(index));
    showPgnSource(std::move(source));
    if (ply_when_read_ >= 0)
        board_.ply = std::clamp(std::exchange(ply_when_read_, -1), 0, static_cast<int>(board_.game.moves.size()));
    if (report_when_read_ >= 0) {  // --focus opening-report=FILE@PLY@TAB (#91)
        const int tab = std::exchange(report_when_read_, -1);
        openOpeningReport(Page::Board);
        if (opening_report_) opening_report_->tab = tab;
    }
    if (export_when_read_ >= 0) openBoardExport(std::exchange(export_when_read_, -1));  // --focus export=… (#93)
    if (review_when_read_) {
        review_when_read_ = false;
        board_.open_tab   = explain_after_review_ ? 4 : 0;  // the Explained tab, when every move is to be explained
        startGameReview();
        if (review_ply_ >= 0) board_.ply = std::clamp(review_ply_, 0, static_cast<int>(board_.game.moves.size()));
    }
}

void App::showPgnSource(PgnSource source) {
    auto& b         = board_;
    const auto n    = source.index->size();
    b.message       = trn("{} game in {}", "{} games in {}", n, n, source.name);
    b.message_error = false;
    loadPgnGame(0, std::move(source));
    if (n > 1) b.open_tab = 1;  // the list of the games
}

void App::loadPgnGame(int index, std::optional<PgnSource> fresh) {
    auto& b         = board_;
    const auto take = [&] {
        if (fresh) b.pgn = std::move(fresh);
        fresh.reset();
    };
    const PgnSource* source = fresh ? &*fresh : b.pgn ? &*b.pgn : nullptr;
    if (!source || index < 0 || index >= static_cast<int>(source->index->size())) {
        take();
        return;
    }
    const auto text = pgnSourceGame(*source, static_cast<std::size_t>(index));
    std::string error;
    auto game = text ? readPgnGame(*text, error) : std::nullopt;
    if (!game) {
        take();
        b.pgn->selected = index;
        b.message       = trf("Game {}: {}", index + 1, text ? error : std::string(TR("it cannot be read.")));
        b.message_error = true;
        return;
    }
    const bool chess960 = gameIsChess960(*game);
    auto title          = fmt::format("{} – {}", game->header("White"), game->header("Black"));
    if (const auto event = game->header("Event"); !event.empty() && event != "?") title += " · " + event;
    if (const auto round = game->header("Round"); !round.empty() && round != "?" && round != "-")
        title += " · " + trf("round {}", round);
    const auto back = board_back_;
    openOnBoard(std::move(*game), 0, chess960, std::move(title), false);
    take();
    b.pgn->selected   = index;
    board_back_       = back;
    b.tree.variations = readVariations(*text, b.tree.main, chess960);  // those of the file, kept
    b.message.clear();
}

void App::saveBoardPgn() {
    const auto& main = board_.tree.main;
    const auto white = main.header("White"), black = main.header("Black");
    const auto name = white.empty() && black.empty() ? std::string("game") : fileNameOf(white + " - " + black);
    dialogs_.saveFile(Dialogs::Purpose::SaveBoardPgn, {{"PGN", "pgn"}}, (storage_.root() / (name + ".pgn")).string());
}

void App::readOnBoard(const std::string& text, const std::string& name) {
    auto& b         = board_;
    const auto read = readBoardText(text);
    switch (read.kind) {
        case BoardText::Kind::Nothing:
            if (page_ != Page::Board) openPositionOnBoard(boardFen(), b.chess960, b.title, false);
            b.message       = read.error;
            b.message_error = true;
            return;
        case BoardText::Kind::Position:
            openPositionOnBoard(read.fen, read.chess960, name, false);
            b.pgn.reset();
            return;
        case BoardText::Kind::Games: {
            auto pasted = std::make_shared<const std::string>(read.pgn);
            std::istringstream in(*pasted);
            auto index = tour::indexPgn(in);
            if (index.games.empty()) {
                b.message       = TR("No position, game or move found in the text.");
                b.message_error = true;
                return;
            }
            if (page_ != Page::Board) {
                board_back_ =
                    BoardBack{page_, page_ == Page::Tournament ? std::optional<Tab>(shown_tab_) : std::nullopt};
            }
            page_ = Page::Board;
            PgnSource source;
            source.name  = name;
            source.text  = std::move(pasted);
            source.index = std::make_shared<const tour::PgnIndex>(std::move(index));
            if (source.index->size() == 1) {
                loadPgnGame(0, std::move(source));
                b.pgn.reset();  // one game: nothing to list
            } else {
                showPgnSource(std::move(source));
            }
            return;
        }
    }
}

void App::pasteOnBoard() {
    char* clip             = SDL_GetClipboardText();
    const std::string text = clip ? clip : "";
    SDL_free(clip);
    if (offerLichessLink(text)) {  // a Lichess link: its card (#47)
        page_ = Page::Board;
        return;
    }
    readOnBoard(text, TR("From the clipboard"));
}

void App::dropFile(const std::string& path) {
    const std::filesystem::path file(path);
    auto ext = file.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // On the Databases page, games go into the database chosen, or into a new one named after the file.
    if (page_ == Page::Databases &&
        (ext == ".pgn" || ext == ".zst" || ext == ".gz" || ext == ".zip" || ext == ".7z" || study::isChessBase(file))) {
        importIntoDatabase(db_selected_, file);
        return;
    }
    // On the Openings page, an annotated PGN becomes a theory study (#77).
    if (page_ == Page::Openings && ext == ".pgn") {
        addTheoryFile(file);
        return;
    }
    if (ext == ".pgn") {
        openBoardPgn(file);
        return;
    }
    if (ext == ".fen" || ext == ".epd" || ext == ".txt") {
        std::ifstream in(file, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        readOnBoard(text.substr(0, 1 << 20), file.filename().string());
        return;
    }
    Console::instance().add(trf("{}: only PGN files, FEN and EPD positions can be dropped here.", file.string()), true);
}

void App::drawPgnGames() {
    auto& src  = *board_.pgn;
    auto& list = *src.index;
    {
        char buffer[256];
        const auto n = std::min(src.filter.size(), sizeof(buffer) - 1);
        std::copy_n(src.filter.data(), n, buffer);
        buffer[n] = 0;
        ImGui::SetNextItemWidth(px(260));
        if (ImGui::InputTextWithHint("##pgn_filter", TR("Filter: players, event, opening, date…"), buffer,
                                     sizeof(buffer))) {
            src.filter       = buffer;
            src.filter_dirty = true;
        }
    }
    if (src.filter_dirty) {
        src.shown.clear();
        for (uint32_t i = 0; i < list.games.size(); ++i) {
            if (tour::pgnGameMatches(list, list.games[i], src.filter)) src.shown.push_back(i);
        }
        src.filter_dirty = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", trf("{} of {}", src.shown.size(), list.games.size()).c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", src.file.empty() ? src.name.c_str() : src.file.string().c_str());

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##pgn_games", 6, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("000000").x);
        ImGui::TableSetupColumn(TR("White"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(TR("Black"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(TR("Result"), ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("1/2-1/2").x);
        ImGui::TableSetupColumn(TR("Date"), ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("0000.00.00").x);
        ImGui::TableSetupColumn(TR("Event"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(src.shown.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto i  = src.shown[static_cast<std::size_t>(row)];
                const auto& g = list.games[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Selectable(fmt::format("{}##pg{}", i + 1, i).c_str(), src.selected == static_cast<int>(i),
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    loadPgnGame(static_cast<int>(i));
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(list.str(g.white).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(list.str(g.black).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(list.str(g.result).c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", list.str(g.date).c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", list.str(g.event).c_str());
            }
        }
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------------------
// The position editor

void App::openPositionEditor() {
    auto& b = board_;
    PositionEditor e;
    e.chess960 = b.chess960;
    (void)editorLoad(e, boardFen());
    b.editor = e;
    b.selected.reset();
}

void App::drawPositionEditor(float size) {
    auto& b = board_;
    auto& e = *b.editor;

    // The board: a click puts the piece chosen (again: takes it away), a right click takes it away.
    BoardDecor decor;
    decor.flipped    = b.flip;
    decor.eval_white = -1.0f;
    ImGui::BeginGroup();
    const ImVec2 top = ImGui::GetCursorScreenPos();
    drawBoard(atlas_, e.squares, size, decor);
    bool changed = false;
    if (ImGui::IsItemHovered()) {
        const bool left  = ImGui::IsMouseClicked(ImGuiMouseButton_Left),
                   right = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (left || right) {
            if (const auto square = boardSquareAt(ImGui::GetMousePos(), top, size, decor)) {
                auto& at = e.squares[static_cast<std::size_t>(square->index())];
                at       = right || e.erase || at == e.brush ? chess::Piece(chess::Piece::NONE) : e.brush;
                changed  = true;
            }
        }
    }
    ImGui::EndGroup();
    ImGui::SameLine();

    ImGui::BeginChild("##editor_side", ImVec2(0, 0));
    ui::beginCard("editor_tools");
    ImGui::TextUnformatted(TR("Edit the position"));
    ImGui::TextDisabled("%s", TR("Click: put the piece chosen (again: take it away). Right click: take it away."));
    // The pieces, white then black, and the eraser.
    const float button = px(40);
    for (const auto colour : {chess::Color::WHITE, chess::Color::BLACK}) {
        for (const auto type : {chess::PieceType::KING, chess::PieceType::QUEEN, chess::PieceType::ROOK,
                                chess::PieceType::BISHOP, chess::PieceType::KNIGHT, chess::PieceType::PAWN}) {
            const auto piece = chess::Piece(type, colour);
            ImVec2 uv0, uv1;
            atlas_.uv(piece, uv0, uv1);
            const bool chosen = !e.erase && e.brush == piece;
            ImGui::PushID(static_cast<int>(piece));
            if (chosen) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
            if (ImGui::ImageButton("##brush", atlas_.texture(button), ImVec2(button, button), uv0, uv1)) {
                e.brush = piece;
                e.erase = false;
            }
            if (chosen) ImGui::PopStyleColor();
            ImGui::PopID();
            ImGui::SameLine();
        }
        if (colour == chess::Color::WHITE) ImGui::NewLine();
    }
    if (e.erase) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    if (ImGui::Button(TR("Eraser"), ImVec2(0, button + ImGui::GetStyle().FramePadding.y * 2))) e.erase = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("A click on a square then empties it. A right click empties a square whatever is "
                                   "chosen."));
    ui::topic(guide::topics::BoardOpen);
    if (e.erase) ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("To move"));
    ImGui::SameLine();
    if (ImGui::RadioButton(TR("White"), e.stm == chess::Color::WHITE)) e.stm = chess::Color::WHITE, changed = true;
    ImGui::SameLine();
    if (ImGui::RadioButton(TR("Black"), e.stm == chess::Color::BLACK)) e.stm = chess::Color::BLACK, changed = true;
    ImGui::SameLine(0, px(24));
    ImGui::TextUnformatted(TR("Move number"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(60));
    if (ui::inputInt("##editor_move", &e.fullmove)) {
        e.fullmove = std::clamp(e.fullmove, 1, 9999);
        changed    = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("The number of the move to play, as the FEN gives it."));

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Castling"));
    static constexpr const char* rights[] = {"O-O##wk", "O-O-O##wq", "O-O##bk", "O-O-O##bq"};
    for (int i = 0; i < 4; ++i) {
        ImGui::SameLine();
        if (i == 2) {
            ImGui::TextDisabled("%s", TR("Black"));
            ImGui::SameLine();
        } else if (i == 0) {
            ImGui::TextDisabled("%s", TR("White"));
            ImGui::SameLine();
        }
        const bool possible = editorCanCastle(e, i);
        ImGui::BeginDisabled(!possible);
        bool on = e.castle[i] && possible;
        if (ImGui::Checkbox(rights[i], &on)) {
            e.castle[i] = on;
            changed     = true;
        }
        ImGui::EndDisabled();
        if (!possible && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", TR("The king and the rook are not where it needs them."));
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("En passant"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(80));
    {
        const std::string current =
            e.ep_file < 0 ? std::string(TR("none")) : std::string(1, static_cast<char>('a' + e.ep_file));
        if (ImGui::BeginCombo("##editor_ep", current.c_str())) {
            if (ImGui::Selectable(TR("none"), e.ep_file < 0)) e.ep_file = -1, changed = true;
            for (int f = 0; f < 8; ++f) {
                // Only where a pawn just moved two squares: the FEN's en passant field says it.
                auto test    = e;
                test.ep_file = f;
                std::istringstream fields(editorFen(test));
                std::string placement, side, castling, ep;
                fields >> placement >> side >> castling >> ep;
                if (ep == "-") continue;
                if (ImGui::Selectable(std::string(1, static_cast<char>('a' + f)).c_str(), e.ep_file == f))
                    e.ep_file = f, changed = true;
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("The file of a pawn that just moved two squares, to be taken en passant: only "
                                       "such files are offered."));
        }
        ui::topic(guide::topics::BoardOpen);
    }
    ImGui::SameLine(0, px(24));
    if (ImGui::Checkbox("Chess960", &e.chess960)) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("Castling as in Chess960: with the rooks where they stand."));

    // The position as a FEN: typed, it sets the board.
    if (changed) e.fen_text = editorFen(e);
    {
        char buffer[256];
        const auto n = std::min(e.fen_text.size(), sizeof(buffer) - 1);
        std::copy_n(e.fen_text.data(), n, buffer);
        buffer[n] = 0;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##editor_fen", buffer, sizeof(buffer))) {
            e.fen_text  = buffer;
            auto loaded = e;
            if (editorLoad(loaded, e.fen_text)) {
                loaded.fen_text = e.fen_text;  // as typed, until it is left
                loaded.brush    = e.brush;
                loaded.erase    = e.erase;
                e               = loaded;
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) e.fen_text = editorFen(e);
    }
    const auto fen     = editorFen(e);
    const auto problem = positionProblem(fen, e.chess960);
    if (!problem.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
        ImGui::TextWrapped("%s", problem.c_str());
        ImGui::PopStyleColor();
    }

    if (ImGui::Button(TR("Initial position"))) {
        (void)editorLoad(e, std::string(chess::constants::STARTPOS));
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Empty board"))) {
        (void)editorLoad(e, "8/8/8/8/8/8/8/8 w - - 0 1");
    }
    ImGui::SameLine();
    ImGui::Checkbox(TR("Flip##editor"), &b.flip);
    ImGui::SameLine();
    if (ImGui::Button(TR("Copy FEN"))) ImGui::SetClipboardText(fen.c_str());

    ImGui::Spacing();
    ImGui::BeginDisabled(!problem.empty());
    if (ImGui::Button(TR("Done"))) {
        const bool chess960 = e.chess960;
        b.editor.reset();
        openPositionOnBoard(fen, chess960, TR("Position set up"), false);
        b.pgn.reset();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(TR("Cancel"))) b.editor.reset();
    ui::endCard();
    ImGui::EndChild();
}

void App::drawBoardColumn(float size) {
    auto& b          = board_;
    const auto board = boardAt(b.game, b.chess960, b.ply);
    const auto stm   = board.sideToMove();
    // The men the user may pick: during the opponent's turn of a live game on Lichess, theirs (#84).
    const auto pick = boardForPicking(board);

    BoardDecor decor;
    decor.flipped = b.flip;
    if (b.ply > 0) decor.last_move = b.game.moves[static_cast<std::size_t>(b.ply - 1)].move;
    // The bar: the analysis of this position, else the score of the move that led to it.
    decor.eval_white    = 0.5f;
    const auto& lines   = b.analyser->lines();
    const auto position = analysisPosition(b.game, b.chess960, b.ply);
    // Fair play (#48): no engine's arrow nor evaluation under the lock, those of the search it stopped included: every
    // engine of the study Board while the aids are locked (#78), the engines' Board's while a game is played live.
    const bool hidden = engineGameHidesAids();  // a game against an engine (#83)
    const bool aids   = searchShown(chessBoard(), aidsLocked() || hidden, liveGameOn() != nullptr || hidden);
    const bool fresh = aids && b.analyser->active() && b.analyser->position() == position;
    // The study Board's engines side by side (#78): one arrow per move, in the colour of the first engine that plays
    // it, how many play it beside its start.
    const bool side_by_side = aids && chessBoard() && analysisEngines(b.settings).size() > 1;
    const auto side_arrows  = side_by_side ? sideBySideArrows(board, position) : std::vector<BoardArrow>{};
    if (fresh && !lines.empty() && lines.front().score) {
        decor.eval_white = barFraction(*lines.front().score, stm);
    } else {
        for (int i = b.ply - 1; i >= 0; --i) {
            const auto& m = b.game.moves[static_cast<std::size_t>(i)];
            if (m.book || !m.score) continue;
            decor.eval_white = barFraction(*m.score, m.mover);
            break;
        }
    }
    // Without the board's analysis, each analysis of the game's best move here, in its colour.
    const bool on_main = b.line < 0 || boardMainPly() == b.ply;
    if (!fresh && side_arrows.empty() && b.settings.arrows && on_main) {
        const auto reviews = shownReviews();
        for (std::size_t i = reviews.size(); i-- > 0;) {
            const auto& positions = reviews[i]->positions;
            if (static_cast<std::size_t>(b.ply) >= positions.size() ||
                positions[static_cast<std::size_t>(b.ply)].best.empty())
                continue;
            const auto moves = legalPrefix(board, {positions[static_cast<std::size_t>(b.ply)].best}, 1);
            if (!moves.empty()) {
                auto colour = reviewColour(i, reviews[i]->engine);
                colour      = (colour & ~IM_COL32_A_MASK) | (IM_COL32(0, 0, 0, 170) & IM_COL32_A_MASK);
                decor.arrows.push_back({moves.front(), colour});
            }
        }
    }
    // The practical moves while their card shows (#94): the engine's best in grey, the practical move in blue, in place
    // of the engines' arrows.
    const bool practical = aids && chessBoard() && practical_arrows_ && practical_arrows_->position == board.hash() &&
                           practical_arrows_->frame >= ImGui::GetFrameCount() - 1;
    if (practical) {
        const auto best = legalPrefix(board, {practical_arrows_->best}, 1);
        const auto move = legalPrefix(board, {practical_arrows_->practical}, 1);
        if (!best.empty() && practical_arrows_->best != practical_arrows_->practical)
            decor.arrows.push_back({best.front(), IM_COL32(110, 116, 128, 180)});
        if (!move.empty()) decor.arrows.push_back({move.front(), IM_COL32(47, 111, 222, 210)});
    } else if (side_by_side && b.settings.arrows) {
        decor.arrows.insert(decor.arrows.end(), side_arrows.begin(), side_arrows.end());
    } else if (fresh && b.settings.arrows) {
        // The first move of each line, the best on top.
        for (int i = static_cast<int>(lines.size()) - 1; i >= 0; --i) {
            const auto moves = legalPrefix(board, lines[static_cast<std::size_t>(i)].pv, 1);
            if (!moves.empty()) decor.arrows.push_back({moves.front(), arrowColour(i)});
        }
    }
    decor.selected = b.selected;
    if (b.selected) {
        for (const auto& m : movesFrom(pick, *b.selected)) decor.targets.push_back(shownTarget(m));
    } else if (b.move_typed && chessBoard()) {
        // A move being typed (#66): where the moves offered go, and the one Enter plays, in the accent's blue.
        const auto typed = typedMoves(pick, b.move_typed->data(), i18n::language());
        for (const auto& m : typed) decor.targets.push_back(shownTarget(m.move));
        if (b.move_choice >= 0 && b.move_choice < static_cast<int>(typed.size()) &&
            b.move_choice_of == b.move_typed->data())
            decor.arrows.push_back({typed[static_cast<std::size_t>(b.move_choice)].move, IM_COL32(47, 111, 222, 190)});
    }
    // A game on Lichess (#84): a move made in advance, dashed; by correspondence, the move waiting for Enter.
    if (onlineShown() && b.line < 0 && b.ply == static_cast<int>(b.game.moves.size())) {
        if (online_game_->premove) decor.arrows.push_back({*online_game_->premove, IM_COL32(47, 111, 222, 160), true});
        if (online_game_->held) decor.arrows.push_back({*online_game_->held, IM_COL32(47, 111, 222, 210)});
    }
    // A piece dragged: off its square in 2D, a ghost of it in 3D; where it would land, tinted.
    const bool dragging = b.drag && b.drag->moved && b.selected == b.drag->from;
    if (dragging) {
        decor.lifted = b.drag->from;
        if (b.drag_over && std::find(decor.targets.begin(), decor.targets.end(), *b.drag_over) != decor.targets.end())
            decor.drop = b.drag_over;
    }
    // The threat, in red.
    if (aids && b.threat_on && !b.threat.lines().empty() && !b.threat.lines().front().pv.empty()) {
        if (const auto position = threatPosition(); position && b.threat.position() == *position) {
            auto passed = board;
            passed.makeNullMove();
            if (const auto moves = legalPrefix(passed, {b.threat.lines().front().pv.front()}, 1); !moves.empty())
                decor.arrows.push_back({moves.front(), IM_COL32(205, 40, 40, 200)});
        }
    }
    // A hint in a game against an engine (#83), in green, on the position it is for.
    if (engine_game_ && engine_game_->hint_move && b.line < 0 && b.ply == engine_game_->hint_ply)
        decor.arrows.push_back({*engine_game_->hint_move, IM_COL32(40, 160, 80, 210)});
    // The tree's move under the mouse or chosen (the Chess space's Tree tab), in the accent's blue.
    if (chessBoard() && tree_arrow_ && tree_arrow_->position == board.hash())
        decor.arrows.push_back({tree_arrow_->move, IM_COL32(47, 111, 222, 190)});
    // The arrows and squares of the theory note under the mouse in the Tree tab (#77), lighter than the game's own.
    if (chessBoard() && theory_marks_ && theory_marks_->frame >= ImGui::GetFrameCount() - 1 &&
        theory_marks_->position == board.hash()) {
        for (const auto& a : theory_marks_->arrows)
            if (a.from >= 0 && a.to >= 0)
                decor.arrows.push_back(
                    {chess::Move::make<chess::Move::NORMAL>(chess::Square(a.from), chess::Square(a.to)),
                     (markColour(a.colour, false) & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 150)});
        for (const auto& q : theory_marks_->squares)
            if (q.from >= 0) decor.tints.push_back({chess::Square(q.from), markColour(q.colour, true)});
    }
    // The marks of the move shown, and the arrow being drawn (the Chess space).
    if (chessBoard()) {
        const MoveNotes& notes = b.ply > 0 ? b.game.moves[static_cast<std::size_t>(b.ply - 1)].notes : b.start_notes;
        for (const auto& a : notes.arrows)
            if (a.from >= 0 && a.to >= 0)
                decor.arrows.push_back(
                    {chess::Move::make<chess::Move::NORMAL>(chess::Square(a.from), chess::Square(a.to)),
                     markColour(a.colour, false)});
        for (const auto& s : notes.squares)
            if (s.from >= 0) decor.tints.push_back({chess::Square(s.from), markColour(s.colour, true)});
        if (b.mark_from && b.mark_hover && *b.mark_hover != *b.mark_from)
            decor.arrows.push_back({chess::Move::make<chess::Move::NORMAL>(*b.mark_from, *b.mark_hover),
                                    (markColour(markColourKey(), false) & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 120)});
    }
    if (b.ply > 0) {
        const auto& m = b.game.moves[static_cast<std::size_t>(b.ply - 1)];
        // The move's judgement on its square.
        if (on_main && static_cast<std::size_t>(b.ply - 1) < b.judged.size()) {
            const auto j = b.judged[static_cast<std::size_t>(b.ply - 1)].judgement;
            if (j != Judgement::None)
                decor.badge = BoardBadge{shownTarget(m.move), judgementMark(j), judgementBadgeColour(j)};
        }
        // The reply the engine that played it expected, in dashes: its PV's second move.
        if (b.settings.arrows && !m.pv.empty()) {
            std::istringstream pv(m.pv);
            std::string first, reply;
            if (pv >> first >> reply) {
                if (const auto moves = legalPrefix(board, {reply}, 1); !moves.empty())
                    decor.arrows.insert(decor.arrows.begin(), {moves.front(), IM_COL32(60, 64, 74, 190), true});
            }
        }
    }

    // A line shown on the board instead of the game: its last position, framed.
    std::optional<LineBoard> line_board;
    if (b.preview) {
        auto at = boardAt(b.game, b.chess960, std::clamp(b.preview->ply, 0, static_cast<int>(b.game.moves.size())));
        const auto moves = legalPrefix(at, b.preview->moves, b.preview->moves.size());
        for (const auto& mv : moves) at.makeMove(mv);
        line_board.emplace(at);
        const float bar  = decor.eval_white;  // kept: the board does not move
        decor            = BoardDecor{};
        decor.flipped    = b.flip;
        decor.eval_white = bar;
        if (!moves.empty()) decor.last_move = moves.back();
        decor.frame = ui::ACCENT;
    }
    const chess::Board& shown = line_board ? *line_board : board;

    const bool strips = boardStrips() && !line_board;
    std::vector<std::optional<int64_t>> clocks;
    if (strips) {
        clocks = moveClocks(b.game);
        drawPlayerStrip(b.flip ? chess::Color::WHITE : chess::Color::BLACK, size, clocks);
    } else if (boardStrips()) {
        ImGui::Dummy(ImVec2(size, ImGui::GetFrameHeight()));
    }
    const ImVec2 top = ImGui::GetCursorScreenPos();
    if (appearance::current().board3d.on && board3d_.available()) {
        board3d_.setMarks(chessBoard() ? Board3d::Marks::A : Board3d::Marks::Off);
        const auto input = board3d_.draw(shown, size, decor);
        board_hover_     = line_board ? std::nullopt : input.hovered;  // Ctrl+K "This manoeuvre"
        if (chessBoard() && !line_board) {
            b.mark_from  = input.marking;
            b.mark_hover = input.hovered;
            if (input.marked) boardMark(input.marked->first, input.marked->second);
            if (input.clicked && !b.selected && board.at(*input.clicked) == chess::Piece::NONE) boardClearMarks();
        }
        if (line_board && (input.clicked || input.right_click)) {
            b.preview.reset();
        } else {
            boardPointer(pick, input.clicked, input.hovered, input.right_click);
        }
    } else {
        if (!appearance::current().board3d.on) board3d_.release();
        drawBoard(atlas_, shown, size, decor);
        const auto hover = boardSquareAt(ImGui::GetMousePos(), top, size, decor);
        board_hover_     = line_board || !ImGui::IsItemHovered() ? std::nullopt : hover;  // Ctrl+K "This manoeuvre"
        const bool left  = ImGui::IsItemClicked(ImGuiMouseButton_Left),
                   right = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        // The right button: a square's mark where it is released on the square it pressed, an arrow elsewhere.
        if (chessBoard() && !line_board) {
            if (right && hover) b.mark_from = hover;
            if (b.mark_from) {
                b.mark_hover = hover;
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                    if (hover) boardMark(*b.mark_from, *hover);
                    b.mark_from.reset();
                    b.mark_hover.reset();
                }
            }
            if (left && hover && !b.selected && board.at(*hover) == chess::Piece::NONE) boardClearMarks();
        }
        if (line_board && (left || right)) {
            b.preview.reset();
        } else {
            boardPointer(pick, left ? hover : std::nullopt, hover, right);
        }
        // The piece dragged, at the pointer.
        if (dragging && b.drag) drawDraggedPiece(atlas_, board.at(b.drag->from), size);
    }
    // What the line is, over the board.
    if (line_board && b.preview) {
        auto* dl = ImGui::GetWindowDrawList();
        const auto label =
            b.preview->label + (b.preview->hover ? "" : "  ·  " + std::string(TR("Esc or a click: the game")));
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        const ImVec2 p0(top.x + evalBarSpace(size) + px(8), top.y + px(8));
        dl->AddRectFilled(p0, ImVec2(p0.x + ts.x + px(16), p0.y + ts.y + px(8)), IM_COL32(20, 24, 32, 215), px(6));
        dl->AddText(ImVec2(p0.x + px(8), p0.y + px(4)), IM_COL32(240, 243, 248, 255), label.c_str());
    }

    drawPromotionPopup(pick);  // the piece a pawn becomes

    if (strips)
        drawPlayerStrip(b.flip ? chess::Color::BLACK : chess::Color::WHITE, size, clocks);
    else if (boardStrips())
        ImGui::Dummy(ImVec2(size, ImGui::GetFrameHeight()));
    drawBoardInfo(atlas_, board, size, 0);

    // Through the moves, back to the move being played while a game from here goes on, and the position as text.
    const int total = static_cast<int>(b.game.moves.size());
    const bool live = (b.match && b.match_state.running && b.watching) || onlineShown() || botShown();
    if (setup_.nav_buttons || (live && b.ply < total)) {
        const float copy = ImGui::CalcTextSize(TR("Copy FEN")).x + ImGui::GetStyle().FramePadding.x * 2 +
                           ImGui::GetStyle().ItemSpacing.x;
        const auto click =
            drawBoardNav("##board_nav", b.ply, total, b.flip, setup_.nav_buttons, live, size - copy, !chessBoard());
        b.ply            = stepPly(click.step, b.ply, total);
        if (click.hide) {
            setup_.nav_buttons = false;
            saveSetupSoon();
        }
        ImGui::SameLine();
    }
    // From one judged move to the next.
    if (std::any_of(b.judged.begin(), b.judged.end(), [](const auto& j) { return j.judgement != Judgement::None; })) {
        const int prev = judgedPly(-1), next = judgedPly(1);
        ImGui::BeginDisabled(prev < 0);
        if (ImGui::Button("< ?##prev_judged")) showBoardLine(-1, prev);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", chessBoard()
                                        ? TR("Alt+Shift+N: the previous move the analysis marked (?!, ?, ?\?).")
                                        : TR("Shift+N: the previous move the analysis marked (?!, ?, ?\?)."));
        ImGui::SameLine(0, px(2));
        ImGui::BeginDisabled(next < 0);
        if (ImGui::Button("? >##next_judged")) showBoardLine(-1, next);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", chessBoard() ? TR("Alt+N: the next move the analysis marked (?!, ?, ?\?).")
                                                 : TR("N: the next move the analysis marked (?!, ?, ?\?)."));
        ImGui::SameLine();
    }
    if (ImGui::Button(TR("Copy FEN"))) ImGui::SetClipboardText(board.getFen().c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", board.getFen().c_str());
    ImGui::SameLine();
    drawToLichessMenu();  // the position, the game, to lichess.org (#54)
    // The study Board's game as a page to replay, a diagram, a GIF or a PDF (#93).
    if (chessBoard()) {
        const char* label = TR("Export…");
        ui::sameLineOrWrap(ImGui::GetFrameHeight() * 0.42f + ImGui::CalcTextSize(label).x +
                           ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemInnerSpacing.x);
        if (ui::iconButton("board_export", ui::Icon::Share, label)) openBoardExport();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s",
                              TR("The game as a page to replay, a diagram, an animated GIF or a PDF. Ctrl+Shift+C "
                                 "copies the diagram of the position."));
    }
    if (b.line >= 0) {
        ImGui::SameLine();
        if (ImGui::Button(TR("Back to the game"))) showBoardLine(-1, boardMainPly());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("The game's position this variation leaves it at; the variation stays in the "
                                       "moves."));
    }
    if (boardBackToMosaic() && !board_tv_) {  // a game followed from a mosaic; Lichess TV's card has it (#86)
        const char* back = TR("Back to the mosaic");
        ui::sameLineOrWrap(back);
        if (ui::iconButton("board_back", ui::Icon::Back, back)) backToMosaic();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Esc"));
    } else if (board_back_ && !board_tv_) {
        const bool dossier    = board_back_->dossier && dossier_;
        const bool crosstable = board_back_->crosstable && crosstable_;  // #92
        const bool report     = board_back_->report && opening_report_;  // #91
        const char* back      = dossier                                 ? TR("Back to the dossier")
                                : crosstable                            ? TR("Back to the crosstable")
                                : report                                ? TR("Back to the report")
                                : board_back_->page == Page::Tournament ? TR("Back to the tournament")
                                : board_back_->page == Page::Books      ? TR("Back to the books")
                                                                        : TR("Back");
        ui::sameLineOrWrap(back);
        if (ui::iconButton("board_back", ui::Icon::Back, back)) {
            page_ = board_back_->page;
            if (board_back_->tab) tab_request_ = board_back_->tab;
            if (dossier) {  // the player's dossier again, over the page it was opened from (#90)
                dossier_->shown = true;
                dossier_->over  = page_;
            }
            if (report) {  // the opening report again, over the Board page (#91)
                opening_report_->shown = true;
                opening_report_->over  = page_;
            }
            board_back_.reset();
        }
    }
    drawToLichessResult(size);  // a game shared, a position played out (#54)
}

void App::drawPromotionPopup(const chess::Board& board) {
    auto& b = board_;
    if (!ImGui::BeginPopup("##promotion")) {
        b.promotion.reset();
        return;
    }
    static constexpr chess::PieceType types[] = {chess::PieceType::QUEEN, chess::PieceType::ROOK,
                                                 chess::PieceType::BISHOP, chess::PieceType::KNIGHT};
    for (const auto type : types) {
        ImVec2 uv0, uv1;
        atlas_.uv(chess::Piece(type, board.sideToMove()), uv0, uv1);
        ImGui::PushID(static_cast<int>(type));
        if (ImGui::ImageButton("##piece", atlas_.texture(px(44)), ImVec2(px(44), px(44)), uv0, uv1) && b.promotion) {
            for (const auto& m : movesFrom(board, b.promotion->from())) {
                if (shownTarget(m) == shownTarget(*b.promotion) && m.typeOf() == chess::Move::PROMOTION &&
                    m.promotionType() == type) {
                    boardMove(m);
                }
            }
            b.promotion.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::EndPopup();
}

void App::drawBoardAnalysis() {
    if (aidsLocked()) {  // fair play (#48): no analysis of a locked position, nor of any while a live game lasts
        drawFairPlayBanner();
        return;
    }
    // No engine in the library: the card that offers one (M9, board 2).
    if (engines_.empty()) {
        drawFirstEngineCard(
            TR("The analysis, the game's review, the questions and the assistant's comments need an "
               "engine."));
        return;
    }
    auto& b          = board_;
    auto& s          = b.settings.analysis;
    auto& a          = *b.analyser;
    bool changed     = false;
    const auto board = boardAt(b.game, b.chess960, b.ply);

    ui::beginCard("board_analysis");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Analysis"));
    ImGui::SameLine();
    // The engine, among those of the library.
    ImGui::SetNextItemWidth(px(170));
    if (ImGui::BeginCombo("##analysis_engine", s.engine.empty() ? TR("Choose an engine") : s.engine.c_str(),
                          ImGuiComboFlags_HeightLargest)) {
        std::vector<std::string> names, more;
        for (const auto& e : engines_) {
            if (e.inactive && e.name != s.engine) continue;
            names.push_back(e.name);
            more.push_back(engineSearchText(e));
        }
        if (const int i = ui::searchComboItems(names, s.engine, &more); i >= 0 && names[i] != s.engine) {
            s.engine = names[i];
            saveBoardSettings();
            // Another engine: the one running ends, the new one starts if the analysis runs.
            a.stop();
            b.threat.stop();
            if (b.analysing) setBoardAnalysis(true);
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("The engine of the library that searches the position shown. Its options other "
                                   "than those beside are the library's, sent as a game sends them."));
    }
    ui::topic(guide::topics::Analysis);
    // Stopped, the engine stays started for the next search, its memory held: this ends it.
    ImGui::SameLine(0, px(4));
    if (ui::iconOnlyButton("##analysis_unload", ui::Icon::Unload,
                           TR("Unload the engine: it ends and frees its memory. Analyse starts it again."),
                           a.active() || b.threat.active())) {
        unloadBoardEngines();
    }

    // Threads, hash and lines, folded into one line: a click opens them. The row wraps where the card is narrow.
    {
        const auto summary = trf("{} · {} · {}", trn("{} thread", "{} threads", s.threads, s.threads),
                                 s.hash > 0 ? fmt::format("{} MB", s.hash) : std::string(TR("hash of the engine")),
                                 trn("{} line", "{} lines", s.multipv, s.multipv));
        ui::sameLineOrWrap(ImGui::CalcTextSize(summary.c_str()).x + ImGui::GetFrameHeight() +
                           ImGui::GetStyle().FramePadding.x * 2);
        if (ui::iconButton("analysis_settings", ui::Icon::Settings, summary.c_str()))
            ImGui::OpenPopup("##analysis_settings_popup");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("This analysis's own threads, hash and lines: they change neither the engine in "
                                       "the library nor a tournament."));
        }
        if (ImGui::BeginPopup("##analysis_settings_popup")) {
            const auto number = [&](const char* id, const char* label, int& value, int lo, int hi, const char* tip) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(label);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
                ImGui::SameLine(px(90));
                ImGui::SetNextItemWidth(px(80));
                if (ui::inputInt(id, &value)) {
                    value   = std::clamp(value, lo, hi);
                    changed = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
            };
            number("##analysis_threads", TR("Threads"), s.threads, 1, std::max(1, topology_.logical),
                   TR("The threads of the analysis: they count in the core budget, with a tournament's games."));
            number("##analysis_hash", TR("Hash"), s.hash, 0, 1 << 20, TR("MB; 0: the engine's own."));
            number("##analysis_lines", TR("Lines"), s.multipv, 1, 10, TR("How many best moves are shown (MultiPV)."));
            ImGui::TextDisabled("%s", TR("this analysis only"));
            ImGui::EndPopup();
        }
    }
    if (changed) {
        saveBoardSettings();
        a.update(s);
        auto one    = s;
        one.multipv = 1;
        b.threat.update(one);
    }
    // The threat: the opponent's best move, were it to move again.
    ui::sameLineOrWrap(ImGui::CalcTextSize(TR("Threat")).x + ImGui::GetStyle().FramePadding.x * 2 + px(4) +
                       ImGui::CalcTextSize("X").x);
    {
        const bool on = b.threat_on;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ui::SELECT);
        if (ImGui::SmallButton(TR("Threat"))) setBoardThreat(!b.threat_on);
        if (on) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("X: the opponent's best move if it could move again, as a red arrow (another "
                                       "process of the engine, its threads counted)."));
        }
        ImGui::SameLine(0, px(4));
        ImGui::TextDisabled("X");
    }
    // Its place among the cores, at the right.
    {
        const int threads = boardThreads();
        const int busy    = tournamentThreads();
        auto chip         = trf("{} / {} threads", threads, topology_.logical);
        if (busy > 0) chip += " · " + trf("tournament on {}", busy);
        const float width = ImGui::CalcTextSize(chip.c_str()).x;
        ui::sameLineOrWrap(width + px(8), px(8));
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - width - px(4)));
        ImGui::AlignTextToFramePadding();
        if (busy > 0)
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", chip.c_str());
        else
            ImGui::TextDisabled("%s", chip.c_str());
        if (ImGui::IsItemHovered()) {
            std::string tip = TR("The threads the analysis and the games of this page use now, out of the machine's.");
            if (busy > 0)
                tip += "\n" + trf("A tournament is running: the analysis competes with its games ({} threads).", busy);
            ImGui::SetTooltip("%s", tip.c_str());
        }
    }

    // How the search goes.
    const auto stm = board.sideToMove();
    if (a.state() == Analyser::State::Failed) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextWrapped("%s", a.error().c_str());
        ImGui::PopStyleColor();
    } else if (!a.active()) {
        ImGui::TextDisabled("%s", TR("Press A, or Analyse, for the engine's best moves here."));
    } else if (a.state() == Analyser::State::Starting) {
        ImGui::TextDisabled("%s", trf("Starting {}…", a.engine()).c_str());
    } else {
        const auto& lines = a.lines();
        const bool fresh  = a.position() == analysisPosition(b.game, b.chess960, b.ply);
        if (!fresh) {
            ImGui::TextDisabled("%s", b.analysing ? "…" : TR("Not this position."));
        } else if (!lines.empty()) {
            // The search in figures.
            const auto& best = lines.front();
            const std::array<std::pair<const char*, std::string>, 6> tiles{
                {{TR("Depth"), fmt::format("{}/{}", best.depth, best.seldepth)},
                 {TR("Nodes"), ui::formatCount(best.nodes)},
                 {TR("Speed"), ui::formatCount(best.nps) + "/s"},
                 {TR("Time"), ui::formatClock(best.time_ms)},
                 {TR("TB hits"), ui::formatCount(best.tbhits)},
                 {TR("Hash"), best.hashfull >= 0 ? fmt::format("{} %", best.hashfull / 10) : std::string("–")}}};
            const float gap = px(4);
            const float w   = (ImGui::GetContentRegionAvail().x - gap * 5) / 6;
            const float h   = ImGui::GetTextLineHeight() * 2.1f + px(6);
            auto* dl        = ImGui::GetWindowDrawList();
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const ImU32 tile =
                ui::currentTheme() == ui::Theme::Light ? IM_COL32(243, 245, 248, 255) : IM_COL32(32, 36, 43, 255);
            for (std::size_t i = 0; i < tiles.size(); ++i) {
                const ImVec2 p0(at.x + static_cast<float>(i) * (w + gap), at.y);
                dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), tile, px(5));
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.7f, ImVec2(p0.x + px(7), p0.y + px(3)),
                            ui::MUTED, tiles[i].first);
                dl->AddText(ImVec2(p0.x + px(7), p0.y + h - ImGui::GetTextLineHeight() - px(3)),
                            ImGui::GetColorU32(ImGuiCol_Text), tiles[i].second.c_str());
            }
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, h));
            if (best.wdl) {
                // The best line's chances, White's.
                auto wdl = *best.wdl;
                if (stm == chess::Color::BLACK) std::swap(wdl[0], wdl[2]);
                wdlLine(wdl);
            }
        }
        if (fresh) {
            const auto* figurines = appearance::current().figurines ? &atlas_ : nullptr;
            const float badge_w   = ImGui::CalcTextSize("+00.00").x + px(10);
            const float depth_w   = ImGui::CalcTextSize("d00").x + px(8);
            for (std::size_t i = 0; i < lines.size(); ++i) {
                const auto& line = lines[i];
                if (line.pv.empty()) continue;
                ImGui::PushID(static_cast<int>(i));
                // Its score, White's, the best one set apart; its depth; its moves on two lines at most.
                const auto score = whiteScoreText(line, stm);
                const ImVec2 p   = ImGui::GetCursorScreenPos();
                auto* dl         = ImGui::GetWindowDrawList();
                const float lh   = ImGui::GetTextLineHeight() + px(7);
                const ImU32 bg   = i == 0 ? (ui::currentTheme() == ui::Theme::Light ? IM_COL32(250, 251, 253, 255)
                                                                                    : IM_COL32(230, 234, 240, 255))
                                          : (ui::currentTheme() == ui::Theme::Light ? IM_COL32(236, 239, 244, 255)
                                                                                    : IM_COL32(40, 45, 54, 255));
                const ImU32 fg   = i == 0 && ui::currentTheme() != ui::Theme::Light ? IM_COL32(29, 35, 48, 255)
                                                                                    : ImGui::GetColorU32(ImGuiCol_Text);
                dl->AddRectFilled(ImVec2(p.x, p.y + px(2)), ImVec2(p.x + badge_w - px(6), p.y + lh - px(3)), bg, px(4));
                if (i == 0)
                    dl->AddRect(ImVec2(p.x, p.y + px(2)), ImVec2(p.x + badge_w - px(6), p.y + lh - px(3)), ui::CARD_BD,
                                px(4));
                const ImVec2 ts = ImGui::CalcTextSize(score.c_str());
                dl->AddText(ImVec2(p.x + (badge_w - px(6) - ts.x) / 2, p.y + (lh - ts.y) / 2), fg, score.c_str());
                dl->AddText(ImVec2(p.x + badge_w, p.y + (lh - ts.y) / 2), ui::MUTED,
                            fmt::format("d{}", line.depth).c_str());
                ImGui::SetCursorScreenPos(ImVec2(p.x + badge_w + depth_w, p.y));
                const auto items = lineItems(board, line.pv, 24);
                const auto ev    = drawNotation("##line", items, ImGui::GetContentRegionAvail().x, figurines, 2);
                // Hovered: the position after that move, on the board; clicked: the line played up to it.
                if (ev.hovered >= 0) {
                    BoardPreview preview;
                    preview.ply = preview.at = b.ply;
                    preview.moves.assign(
                        line.pv.begin(),
                        line.pv.begin() +
                            std::min<std::size_t>(line.pv.size(), static_cast<std::size_t>(ev.hovered) + 1));
                    preview.label     = pvToSan(board, preview.moves, preview.moves.size());
                    preview.hover     = true;
                    b.preview         = std::move(preview);
                    b.preview_hovered = true;
                    ImGui::SetTooltip("%s", TR("Click: the line played up to this move. Scores are White's."));
                }
                if (ev.clicked >= 0) {
                    for (const auto& m : legalPrefix(board, line.pv, static_cast<std::size_t>(ev.clicked) + 1))
                        boardPlay(m);
                    b.preview.reset();
                }
                ImGui::PopID();
            }
            // The threat.
            if (b.threat_on) {
                const auto& t = b.threat.lines();
                ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
                if (board.inCheck())
                    ImGui::TextUnformatted(TR("Threat: none while in check."));
                else if (!t.empty() && !t.front().pv.empty()) {
                    auto passed = board;
                    passed.makeNullMove();
                    ImGui::TextUnformatted(trf("Threat: {} ({})", pvToSan(passed, t.front().pv, 4),
                                               t.front().score ? whiteScoreText(t.front(), ~stm) : "")
                                               .c_str());
                } else {
                    ImGui::TextUnformatted(TR("Threat: …"));
                }
                ImGui::PopStyleColor();
            }
        }
    }
    ui::endCard();
}

void App::showBoardLine(int line, int ply) {
    auto& b = board_;
    if (line < -1 || line >= static_cast<int>(b.tree.variations.size())) line = -1;
    if (line != b.line || line >= 0) b.game = lineOf(b.tree, line);
    b.line = line;
    b.ply  = std::clamp(ply, 0, static_cast<int>(b.game.moves.size()));
    b.selected.reset();
}

void App::updateBoardTablebase() {
    auto& b = board_;
    // Fair play (#48): no tables while a live game lasts, nor on a game that passes through a correspondence game's
    // position.
    const bool hidden = liveGameOn() || correspondencePly(b.tree.main, b.chess960, INT32_MAX) >= 0 ||
                        engineGameHidesAids();  // a game against an engine (#83)
    const auto key    = fmt::format("{}|{}|{}", b.review_key, setup_.tb_paths, hidden);
    if (key == b.tb_key) return;
    if (hidden) b.tb.clear();  // at once, a tournament playing or not
    // fastchess' Syzygy tables are those of the process: not while a tournament plays in it.
    if (run_->running()) return;
    b.tb_key = key;
    if (!hidden) b.tb = tablebaseResults(b.tree.main, b.chess960, setup_.tb_paths);
}

int App::boardTablebasePly() const {
    const auto& tb = board_.tb;
    for (std::size_t p = 0; p < tb.size(); ++p)
        if (tb[p] != -2) return static_cast<int>(p);
    return -1;
}

bool App::boardStrips() const {
    const auto& g = board_.tree.main;
    const auto w = g.header("White"), k = g.header("Black");
    return (!w.empty() && w != "?") || (!k.empty() && k != "?");
}

void App::drawPlayerStrip(chess::Color side, float size, const std::vector<std::optional<int64_t>>& clocks) {
    const ui::TopicRegion region(guide::topics::BoardPlayers);
    const auto& b   = board_;
    const auto& g   = b.game;
    const float bar = evalBarSpace(size);
    auto* dl        = ImGui::GetWindowDrawList();
    const ImVec2 p  = ImGui::GetCursorScreenPos();
    const float h   = ImGui::GetFrameHeight();
    // Who has the move here, and what this side last played.
    const bool to_move = boardAt(g, b.chess960, b.ply).sideToMove() == side;
    int last           = -1;
    for (int i = std::min(b.ply, static_cast<int>(g.moves.size())) - 1; i >= 0; --i) {
        if (g.moves[static_cast<std::size_t>(i)].mover == side) {
            last = i;
            break;
        }
    }
    float x       = p.x + bar + px(4);
    const float r = ImGui::GetFontSize() * 0.32f;
    const ImVec2 c(x + r, p.y + h / 2);
    playerDot(dl, c, r, side);
    x += r * 2 + px(8);
    const float ty = p.y + (h - ImGui::GetTextLineHeight()) / 2;
    auto name      = g.header(side == chess::Color::WHITE ? "White" : "Black");
    if (name.empty() || name == "?") name = side == chess::Color::WHITE ? TR("White") : TR("Black");
    // A game against an engine on the board (#83): its strength, and the clocks as they run, or wait for Resume.
    const bool engine_game = engine_game_ && !engine_game_->over && (b.watching || engine_game_->waiting) &&
                             g.header(engine_game_->kept.white ? "Black" : "White") == engine_game_->kept.engine;
    const bool at_end      = b.line < 0 && b.ply == static_cast<int>(g.moves.size());
    const bool playing     = engine_game && b.match && b.match_state.running && at_end;
    if (engine_game && (side == chess::Color::WHITE) != engine_game_->kept.white)
        name += " · " + strengthText(engine_game_->kept.strength);
    // A game on Lichess (#84): the players' ratings, and the clocks as Lichess tells them, running.
    const bool online = online_game_ && online_game_->shown && online_game_->state.full;
    if (online) {
        const auto& player = side == chess::Color::WHITE ? online_game_->state.white : online_game_->state.black;
        if (player.rating > 0) name += " · " + std::to_string(player.rating) + (player.provisional ? "?" : "");
    }
    // The Lichess bot's game (#85): the same.
    const bool bot = !online && botShown() && bot_state_.game.full;
    if (bot) {
        const auto& player = side == chess::Color::WHITE ? bot_state_.game.white : bot_state_.game.black;
        if (player.rating > 0) name += " · " + std::to_string(player.rating) + (player.provisional ? "?" : "");
    }
    // A game followed from Lichess's TV or the followed players (#86): the ratings its PGN gives, Lichess's AI named.
    if (!online && !bot && board_broadcast_ && (board_tv_ || board_broadcast_->round == lichess::PLAYERS_MOSAIC)) {
        const auto said = tvName(name);
        const auto elo  = std::atoi(g.header(side == chess::Color::WHITE ? "WhiteElo" : "BlackElo").c_str());
        name            = said != name || elo <= 0 ? said : name + " · " + std::to_string(elo);
    }
    dl->AddText(ImVec2(x, ty), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
    dl->AddText(ImVec2(x + 0.6f, ty), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
    x += ImGui::CalcTextSize(name.c_str()).x + px(10);
    if (last >= 0) {
        const auto& m = g.moves[static_cast<std::size_t>(last)];
        // Its evaluation of the move, as a chip.
        if (m.score && !m.book) {
            auto white = *m.score;
            if (m.mover == chess::Color::BLACK) white.value = -white.value;
            const auto chip = ui::formatScore(white) + (m.depth > 0 ? fmt::format(" d{}", m.depth) : "");
            const ImVec2 cs = ImGui::CalcTextSize(chip.c_str());
            dl->AddRectFilled(ImVec2(x, ty - px(1)), ImVec2(x + cs.x + px(12), ty + cs.y + px(1)), ui::SELECT, px(4));
            dl->AddText(ImVec2(x + px(6), ty), ImGui::GetColorU32(ImGuiCol_Text), chip.c_str());
            x += cs.x + px(20);
        }
        if (!to_move) {
            // In the Chess space, a tournament's ChessBase style tells the time with its [%emt] (#87).
            const auto spent = chessBoard() ? moveTime(m) : m.time_ms;
            const auto played =
                spent >= 0 && !m.book ? trf("played {} in {}", m.san, ui::formatClock(spent)) : trf("played {}", m.san);
            dl->AddText(ImVec2(x, ty), ui::MUTED, played.c_str());
        }
    }
    if (to_move) dl->AddText(ImVec2(x, ty), ui::MUTED, TR("to move"));
    // Its clock after its last move, at the right, framed while it has the move; running in a game being played.
    std::optional<int64_t> clock;
    if (last >= 0 && static_cast<std::size_t>(last) < clocks.size()) clock = clocks[static_cast<std::size_t>(last)];
    if (playing) {
        const auto& st = b.match_state;
        auto ms        = st.clock_ms[side == chess::Color::WHITE ? 0 : 1];
        if (st.thinking == side)
            ms -=
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - st.think_start)
                    .count();
        clock = std::max<int64_t>(0, ms);
    } else if (engine_game && engine_game_->waiting && at_end) {
        clock = engine_game_->kept.clock_ms[side == chess::Color::WHITE ? 0 : 1];  // as they wait for Resume
    }
    std::optional<std::string> online_clock;
    if (online && at_end) online_clock = onlineStripClock(side);
    if (bot && at_end) online_clock = botStripClock(side);
    if (online_clock && online_clock->empty()) {  // by correspondence, the other side's time is not told
        online_clock.reset();
        clock.reset();
    }
    if (clock || online_clock) {
        const auto text = online_clock ? *online_clock : ui::formatClock(*clock);
        ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize() * 1.1f);
        const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
        const float x1 = p.x + bar + size, x0 = x1 - ts.x - px(20);
        dl->AddRectFilled(ImVec2(x0, p.y + px(1)), ImVec2(x1, p.y + h - px(1)), ui::CARD_BG, px(5));
        dl->AddRect(ImVec2(x0, p.y + px(1)), ImVec2(x1, p.y + h - px(1)), to_move ? ui::ACCENT : ui::CARD_BD, px(5), 0,
                    to_move ? px(2) : px(1));
        dl->AddText(ImVec2(x0 + px(10), p.y + (h - ts.y) / 2), ImGui::GetColorU32(ImGuiCol_Text), text.c_str());
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(bar + size, h));
}

int App::boardMainPly() const { return branchPly(board_.tree, board_.line, board_.ply); }

void App::drawBoardMoves(float height) {
    auto& b         = board_;
    const int total = static_cast<int>(b.game.moves.size());
    const auto& main = b.tree.main;

    ui::beginCard("board_game", ImVec2(0, height));
    // The moves, a table of them, and the games of the file when it has several.
    if (ImGui::BeginTabBar("##board_game_tabs")) {
        const auto tab = [&](const char* label, int index) {
            const ImGuiTabItemFlags flags = b.open_tab == index ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(label, nullptr, flags)) {
                b.game_tab = index;
                ImGui::EndTabItem();
            }
        };
        tab(TR("Moves###board_moves_tab"), 0);
        tab(TR("Table###board_table_tab"), 2);
        tab(TR("Review###board_review_tab"), 3);
        // Every move explained, beside the moves (#94): the Chess space's.
        if (chessBoard())
            tab(TR("Explained###board_explained_tab"), 4);
        else if (b.game_tab == 4)
            b.game_tab = 0;
        if (b.pgn && b.pgn->index->size() > 1)
            tab(fmt::format("{} ({})###board_games_tab", b.pgn->name, b.pgn->index->size()).c_str(), 1);
        else if (b.game_tab == 1)
            b.game_tab = 0;
        b.open_tab = -1;
        // The game as PGN, with its variations, at the right of the tabs.
        const char* copy = TR("Copy PGN");
        if (ImGui::TabItemButton(copy, ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
            ImGui::SetClipboardText(gamePgn(b.tree, b.chess960).c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("The game, with its tags, moves and variations, as PGN."));
        ImGui::EndTabBar();
    }
    if (b.game_tab == 1) {
        drawPgnGames();
        ui::endCard();
        return;
    }
    if (chessBoard()) {
        drawGameHeaders();  // the title line, and the headers unfolded on it (M2, board 7)
    } else {
        const auto white = main.header("White"), black = main.header("Black");
        if (!white.empty() || !black.empty()) {
            ImGui::TextUnformatted(
                fmt::format("{} – {}", white.empty() ? "?" : white, black.empty() ? "?" : black).c_str());
            if (!main.result.empty() && main.result != "*") {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", main.result.c_str());
            }
        }
    }

    // Its analyses: kept with the game, read again when another game comes.
    if (const auto key = reviewKey(main); key != b.review_key || b.judged_chess != chessBoard()) {
        b.review_key   = key;
        b.judged_chess = chessBoard();
        b.reviews = !main.moves.empty() ? loadReviews(storage_.root() / "analyses", main) : std::vector<GameReview>{};
        b.judged  = b.reviews.empty() ? std::vector<MoveReview>{} : reviewMoves(main, b.chess960, b.reviews.front());
        // In the Chess space, the moves judged as its review and its comments judge them (M5).
        if (chessBoard() && !b.reviews.empty())
            if (const auto review = boardCommentaryReview()) b.judged = judgedMoves(*review);
    }
    updateBoardTablebase();

    // This move under the moves and the table; the review is about the whole game. In the Chess space, under the moves
    // of a tournament's game, what the notation shows beside them (#87).
    const float strip = b.ply > 0 && b.ply <= total && b.game_tab != 3 && b.game_tab != 4 ? thisMoveHeight() : 0.0f;
    const float chips =
        chessBoard() && b.game_tab == 0 && hasPlayerEvaluations(main) ? ImGui::GetFrameHeightWithSpacing() : 0.0f;
    if (chessBoard() && b.game_tab == 0) drawBoardNagBar();  // the move field is the column's, above its tabs
    ImGui::BeginChild("##board_moves", ImVec2(0, -(strip + chips)), ImGuiChildFlags_Borders);
    std::optional<ui::TopicRegion> moves_region(
        std::in_place, b.tree.variations.empty() ? guide::topics::BoardMoves : guide::topics::Variations);
    if (b.game_tab == 3) {
        drawBoardReview();
    } else if (b.game_tab == 4) {
        drawBoardExplained();
    } else if (b.game_tab == 2) {
        drawBoardTable();
    } else if (total == 0 && main.moves.empty()) {
        ImGui::TextDisabled("%s", TR("Play a move on the board, or open a game."));
    } else {
        // The game and its variations as flowing text; each move a click away.
        const auto id_of    = [](int line, int ply) { return (line + 1) * 100000 + ply; };
        const int owner     = b.ply > 0 ? ownerOf(b.tree, b.line, b.ply - 1) : -2;
        const ImU32 var_col = ImGui::GetColorU32(ImGuiCol_Text, 0.62f);
        std::vector<NotationItem> items;
        // In the Chess space: the comments, word by word, and the field where one is written.
        const bool proto        = chessBoard();
        const ImU32 comment_col = IM_COL32(52, 146, 92, 255);
        // The assistant's: blue grey, in italics, after a bubble (M5, board 3).
        const ImU32 assistant_col = ui::assistantColour();
        // A comment's words give back an id of their own: a click goes to its move, a double click corrects it.
        struct CommentAt {
            int line, ply;
            bool before;
            bool assistant;
        };
        std::vector<CommentAt> comments;
        const auto words = [&](const std::string& text, float indent, int line, int ply, bool before, bool assistant) {
            const int id = ply > 0 ? kCommentIds + static_cast<int>(comments.size()) : -1;  // the game's own: none
            if (ply > 0) comments.push_back({line, ply, before, assistant});
            if (assistant) {
                NotationItem bubble;
                bubble.kind   = NotationItem::Kind::Text;
                bubble.icon   = ui::Icon::Comment;
                bubble.colour = assistant_col;
                bubble.indent = indent;
                bubble.id     = id;
                items.push_back(std::move(bubble));
            }
            std::istringstream in(text);
            for (std::string word; in >> word;) {
                NotationItem w;
                w.kind   = NotationItem::Kind::Text;
                w.text   = std::move(word);
                w.colour = assistant ? assistant_col : comment_col;
                w.italic = assistant;
                w.indent = indent;
                w.id     = id;
                items.push_back(std::move(w));
            }
        };
        // The assistant's comment beside another (#60, mockup M11, board 5): apart, in a box of its colour, under its
        // name; what can be done with it said in the box of the move shown.
        const auto block = [&](const std::string& text, float indent, int line, int ply) {
            const int id = kCommentIds + static_cast<int>(comments.size());
            comments.push_back({line, ply, false, true});
            NotationItem box;
            box.kind   = NotationItem::Kind::Block;
            box.icon   = ui::Icon::Comment;
            box.colour = assistant_col;
            box.title  = TR("Assistant");
            box.text   = text;
            box.indent = indent;
            box.id     = id;
            if (line == owner && ply == b.ply)
                box.hint = TR("Ctrl+Z takes it back · a double click corrects it, and it then becomes yours");
            items.push_back(std::move(box));
        };
        const auto field = [&](float indent) {
            auto& e = *b.comment_edit;
            NotationItem f;
            f.kind        = NotationItem::Kind::Field;
            f.field       = e.text.data();
            f.field_size  = e.text.size();
            f.field_focus = e.focus;
            f.text        = e.before      ? TR("A comment before the move · Enter keeps it, Esc leaves it")
                            : e.assistant ? TR("The assistant's comment · Enter keeps it, and it becomes yours; Esc "
                                               "leaves it")
                                          : TR("A comment after the move · Enter keeps it, Esc leaves it");
            f.colour      = e.assistant ? assistant_col : comment_col;
            f.indent      = indent;
            items.push_back(std::move(f));
        };
        int last_book = -1;
        for (int i = 0; i < static_cast<int>(main.moves.size()) && main.moves[static_cast<std::size_t>(i)].book; ++i)
            last_book = i;
        // A game against an engine (#83): where its openings ended, the engine playing from there.
        std::string book_text = proto ? TR("end of book") : TR("· end of book ·");
        if (engine_game_ && b.match && b.watching && b.match_state.book_out && b.match_state.last_book >= 0 &&
            last_book < 0) {
            last_book = b.match_state.last_book;
            book_text = engine_game_->kept.settings.openings == Openings::Repertoire
                            ? TR("end of your repertoire · then the engine")
                            : TR("end of its openings · then the engine");
        }
        const auto tags = engineTags();  // what shows beside a tournament engine's moves (#87)

        // In the Chess space, letters rather than figurines are the language's (#66).
        const bool letters = chessBoard() && !appearance::current().figurines;

        const std::function<void(int, int, chess::Board, int)> emit = [&](int line, int base, chess::Board at,
                                                                          int depth) {
            const auto& moves = line < 0 ? main.moves : b.tree.variations[static_cast<std::size_t>(line)].moves;
            bool number       = true;
            for (std::size_t i = 0; i < moves.size(); ++i) {
                const auto& m = moves[i];
                const int ply = base + static_cast<int>(i);  // the position before it
                const bool editing =
                    proto && b.comment_edit && b.comment_edit->line == line && b.comment_edit->ply == ply + 1;
                if (proto && editing && b.comment_edit->before) {
                    field(px(10) * static_cast<float>(depth));
                    number = true;
                } else if (proto && !m.notes.before.empty()) {
                    words(m.notes.before, px(10) * static_cast<float>(depth), line, ply + 1, true, false);
                    number = true;
                }
                if (at.sideToMove() == chess::Color::WHITE || number) {
                    NotationItem n;
                    n.kind = NotationItem::Kind::Number;
                    n.text = std::to_string(at.fullMoveNumber()) + (at.sideToMove() == chess::Color::WHITE ? "." : "…");
                    n.indent = px(10) * static_cast<float>(depth);
                    items.push_back(std::move(n));
                }
                number = false;
                NotationItem item;
                item.text = m.san.empty() ? chess::uci::moveToSan(at, m.move) : m.san;
                if (letters) item.text = localSan(item.text);
                item.id      = id_of(line, ply + 1);
                item.current = line == owner && ply + 1 == b.ply;
                item.indent  = px(10) * static_cast<float>(depth);
                if (line >= 0) item.colour = var_col;
                if (line < 0 && m.book) item.colour = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                if (line < 0 && static_cast<std::size_t>(ply) < b.judged.size()) {
                    const auto j = b.judged[static_cast<std::size_t>(ply)].judgement;
                    if (j != Judgement::None) {
                        item.mark        = judgementMark(j);
                        item.mark_colour = judgementColour(j);
                    }
                }
                if (proto) {
                    for (const uint8_t nag : m.notes.nags) {
                        if (nag < 1 || nag > 6) continue;
                        item.mark        = nagText(nag);
                        item.mark_colour = nagColour(nag);
                    }
                }
                items.push_back(std::move(item));
                if (proto) {
                    for (const uint8_t nag : m.notes.nags) {
                        if (nag >= 1 && nag <= 6) continue;
                        NotationItem sign;
                        sign.kind = NotationItem::Kind::Text;
                        sign.text = nagText(nag);
                        items.push_back(std::move(sign));
                    }
                    // A tournament engine's evaluation (White's) and depth beside its move, the rest on hovering it
                    // (#87, mockup M12, board 8).
                    if (line < 0) {
                        if (auto tag = engineTag(m, tags); !tag.empty()) {
                            NotationItem t;
                            t.kind = NotationItem::Kind::Tag;
                            t.text = std::move(tag);
                            t.id   = id_of(line, ply + 1);
                            items.push_back(std::move(t));
                        }
                    }
                    const float indent = px(10) * static_cast<float>(depth);
                    if (editing && !b.comment_edit->before && !b.comment_edit->assistant) {
                        field(indent);
                        number = true;
                    } else if (!m.notes.after.empty()) {
                        words(m.notes.after, indent, line, ply + 1, false, false);
                        number = true;
                    }
                    // The assistant's: inline after a bubble when the move has no other (M5, board 3), apart beside
                    // another (M11, board 5).
                    if (editing && b.comment_edit->assistant) {
                        field(indent);
                        number = true;
                    } else if (!m.notes.assistant.empty()) {
                        if (m.notes.after.empty())
                            words(m.notes.assistant, indent, line, ply + 1, false, true);
                        else
                            block(m.notes.assistant, indent, line, ply + 1);
                        number = true;
                    }
                }
                if (line < 0 && ply == last_book && last_book + 1 < static_cast<int>(main.moves.size())) {
                    NotationItem book;
                    book.kind   = NotationItem::Kind::Text;
                    book.text   = book_text;
                    book.chip   = proto;  // a pill in the Chess space (M12, board 8)
                    book.colour = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                    items.push_back(std::move(book));
                }
                for (std::size_t v = 0; v < b.tree.variations.size(); ++v) {
                    const auto& var = b.tree.variations[v];
                    if (var.parent != line || var.from != ply || var.moves.empty()) continue;
                    NotationItem open;
                    open.kind   = NotationItem::Kind::Text;
                    open.text   = "(";
                    open.colour = var_col;
                    items.push_back(std::move(open));
                    emit(static_cast<int>(v), ply, at, depth + 1);
                    NotationItem close = items.back();
                    close              = NotationItem{};
                    close.kind         = NotationItem::Kind::Text;
                    close.text         = ")";
                    close.colour       = var_col;
                    items.push_back(std::move(close));
                    number = true;
                }
                at.makeMove<true>(m.move);
            }
        };
        if (proto && !main.pre_comment.empty()) words(main.pre_comment, 0, -1, 0, true, false);
        emit(-1, 0, boardAt(main, b.chess960, 0), 0);
        if (!main.result.empty() && main.result != "*") {
            NotationItem result;
            result.kind = NotationItem::Kind::Text;
            result.text = main.result;
            items.push_back(std::move(result));
        }
        const bool follow = b.ply != b.followed || b.line != b.followed_line;
        if (follow && b.ply == 0) ImGui::SetScrollY(0);
        const auto ev = drawNotation("##moves", items, ImGui::GetContentRegionAvail().x,
                                     appearance::current().figurines ? &atlas_ : nullptr);
        if (follow && ev.current_min) {
            const float top = ImGui::GetWindowPos().y, bottom = top + ImGui::GetWindowHeight();
            if (ev.current_min->y < top || ev.current_max->y > bottom)
                ImGui::SetScrollY(ImGui::GetScrollY() + ev.current_min->y - top - ImGui::GetWindowHeight() * 0.35f);
        }
        if (ev.field_done) boardCommentDone();
        if (ev.field_cancelled) b.comment_edit.reset();
        if (b.comment_edit) b.comment_edit->focus = false;
        const auto comment = [&](int id) -> const CommentAt* {
            return id >= kCommentIds && id - kCommentIds < static_cast<int>(comments.size())
                       ? &comments[static_cast<std::size_t>(id - kCommentIds)]
                       : nullptr;
        };
        if (const auto* c = comment(ev.double_clicked)) {
            showBoardLine(c->line, c->ply);
            boardComment(c->before, c->assistant);
        } else if (const auto* at = comment(ev.clicked)) {
            showBoardLine(at->line, at->ply);
            b.preview.reset();
        } else if (ev.clicked >= 0) {
            showBoardLine(ev.clicked / 100000 - 1, ev.clicked % 100000);
            b.preview.reset();
        }
        if (const auto* c = comment(ev.hovered)) {
            std::string tip = c->assistant ? TR("The assistant's comment · a double click corrects it, and it "
                                                "becomes yours · Ctrl+Z takes it back")
                                           : TR("A double click corrects the comment");
            // What it rests on, when it was written in this session (R9).
            if (const MoveNotes* notes = c->assistant ? notesAt(b.tree, c->line, c->ply) : nullptr) {
                if (const auto it = assistant_basis_.find(notes->assistant); it != assistant_basis_.end()) {
                    tip += "\n\n" + std::string(TR("What it rests on:"));
                    for (const auto& s : it->second) tip += "\n· " + s;
                }
            }
            if (ImGui::BeginTooltip()) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
                ImGui::TextUnformatted(tip.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
        // A move of the game: what its engine said and its judgement; in the Chess space, a tournament engine's every
        // field (#87).
        if (ev.hovered >= 0 && ev.hovered < kCommentIds && ev.hovered / 100000 == 0) {
            const int i = ev.hovered % 100000 - 1;
            if (i >= 0 && i < static_cast<int>(main.moves.size()) && proto &&
                playerEvaluation(main.moves[static_cast<std::size_t>(i)])) {
                drawEngineMoveTip(i + 1, static_cast<std::size_t>(i) < b.judged.size()
                                             ? b.judged[static_cast<std::size_t>(i)]
                                             : MoveReview{});
            } else if (i >= 0 && i < static_cast<int>(main.moves.size())) {
                const auto& m = main.moves[static_cast<std::size_t>(i)];
                std::string tip;
                if (m.score && !m.book) {
                    tip = fmt::format("{}/{}  {}", ui::formatScore(m.score), m.depth,
                                      m.time_ms >= 0 ? ui::formatClock(m.time_ms) : "");
                }
                const auto judged = static_cast<std::size_t>(i) < b.judged.size()
                                        ? b.judged[static_cast<std::size_t>(i)]
                                        : MoveReview{};
                if (judged.judgement != Judgement::None) tip += (tip.empty() ? "" : "\n") + judgementTip(i, judged);
                if (!tip.empty()) ImGui::SetTooltip("%s", tip.c_str());
            }
        }
        // A variation's move: promote it, remove it.
        if (ev.right_clicked >= 0 && ev.right_clicked < kCommentIds && ev.right_clicked / 100000 > 0) {
            b.menu_line = ev.right_clicked / 100000 - 1;
            ImGui::OpenPopup("##variation_menu");
        }
        // A tournament engine's move, in the Chess space: the line it expected on the board or as a variation, its
        // comment as it is (#87, mockup M12, board 8).
        if (proto && ev.right_clicked > 0 && ev.right_clicked < kCommentIds && ev.right_clicked / 100000 == 0 &&
            ev.right_clicked <= static_cast<int>(main.moves.size()) &&
            playerEvaluation(main.moves[static_cast<std::size_t>(ev.right_clicked - 1)])) {
            engine_menu_ply_ = ev.right_clicked;
            ImGui::OpenPopup("##engine_move_menu");
        }
        if (ImGui::BeginPopup("##engine_move_menu")) {
            const int ply = engine_menu_ply_;
            if (ply >= 1 && ply <= static_cast<int>(main.moves.size())) {
                const auto& m    = main.moves[static_cast<std::size_t>(ply - 1)];
                const auto line  = expectedLine(m, b.chess960);
                const auto label = boardMoveLabel(ply);
                ImGui::TextDisabled("%s", label.c_str());
                ImGui::BeginDisabled(line.empty());
                if (ImGui::MenuItem(TR("The line it expected, on the board"))) {
                    showBoardLine(-1, ply);
                    auto name = main.header(m.mover == chess::Color::WHITE ? "White" : "Black");
                    if (name.empty() || name == "?") name = m.mover == chess::Color::WHITE ? TR("White") : TR("Black");
                    b.preview = BoardPreview{
                        ply, ply, line,
                        trf("{} expected {}", name, localLine(pvToSan(boardAt(main, b.chess960, ply), line, 6))),
                        false};
                }
                if (ImGui::MenuItem(TR("The line it expected, as a variation"))) boardExpectedVariation(ply);
                ImGui::EndDisabled();
                if (ImGui::MenuItem(TR("Copy its comment as it is")))
                    ImGui::SetClipboardText(study::engineComment(m).c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", TR("The engine's comment as the PGN has it: every field the tournament "
                                               "wrote."));
            }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopup("##variation_menu")) {
            if (ImGui::MenuItem(TR("Promote the variation"))) {
                const int line = promoteVariation(b.tree, b.menu_line);
                b.game         = lineOf(b.tree, line);
                showBoardLine(line, b.ply);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s",
                                  TR("It becomes the line it branches from; that line's moves after it become the "
                                     "variation."));
            if (ImGui::MenuItem(TR("Delete the variation"))) {
                const int ply    = b.ply;
                const int branch = branchPly(b.tree, b.menu_line, b.ply);
                const bool shown = b.line >= 0;
                const int parent = removeVariation(b.tree, b.menu_line);
                b.game           = lineOf(b.tree, shown ? parent : -1);
                showBoardLine(shown ? parent : -1, shown ? std::min(ply, branch) : ply);
            }
            if (ImGui::MenuItem(TR("Copy the variation"))) {
                const auto line = lineOf(b.tree, b.menu_line);
                std::vector<std::string> uci;
                for (const auto& m : line.moves) uci.push_back(chess::uci::moveToUci(m.move, b.chess960));
                const auto& var = b.tree.variations[static_cast<std::size_t>(b.menu_line)];
                const std::vector<std::string> own(uci.begin() + var.from, uci.end());
                ImGui::SetClipboardText(pvToSan(boardAt(line, b.chess960, var.from), own, own.size()).c_str());
            }
            ImGui::EndPopup();
        }
    }
    b.followed      = b.ply;
    b.followed_line = b.line;
    moves_region.reset();  // its group ends inside the child
    ImGui::EndChild();
    if (strip > 0) drawThisMove();
    if (chips > 0) drawEngineTagChips();
    ui::endCard();
}

void App::drawBoardTable() {
    auto& b          = board_;
    const auto& main = b.tree.main;
    if (main.moves.empty()) {
        ImGui::TextDisabled("%s", TR("Play a move on the board, or open a game."));
        return;
    }
    // Each move with what its engine said: score (White's), depth, time.
    int64_t longest = 1;
    for (const auto& m : main.moves) longest = std::max(longest, m.time_ms);
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("##moves_table", 9, flags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, px(34));
    for (const char* side : {TR("White"), TR("Black")}) {
        ImGui::TableSetupColumn(side, ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn(TR("Eval"), ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn(TR("Depth"), ImGuiTableColumnFlags_WidthStretch, 0.7f);
        ImGui::TableSetupColumn(TR("Time"), ImGuiTableColumnFlags_WidthStretch, 1.3f);
    }
    ImGui::TableHeadersRow();
    const auto start  = boardAt(main, b.chess960, 0);
    const bool black  = start.sideToMove() == chess::Color::BLACK;
    const int first   = start.fullMoveNumber();
    const int on_main = boardMainPly();
    const int rows    = (static_cast<int>(main.moves.size()) + (black ? 1 : 0) + 1) / 2;
    ImGuiListClipper clipper;
    clipper.Begin(rows);
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%d.", first + r);
            for (int side = 0; side < 2; ++side) {
                const int i = r * 2 + side - (black ? 1 : 0);  // the move's index
                if (i < 0 || i >= static_cast<int>(main.moves.size())) {
                    for (int k = 0; k < 4; ++k) ImGui::TableNextColumn();
                    continue;
                }
                const auto& m     = main.moves[static_cast<std::size_t>(i)];
                const auto judged = static_cast<std::size_t>(i) < b.judged.size()
                                        ? b.judged[static_cast<std::size_t>(i)].judgement
                                        : Judgement::None;
                ImGui::TableNextColumn();
                if (judged != Judgement::None) {
                    auto bg = judgementColour(judged);
                    bg      = (bg & ~IM_COL32_A_MASK) | (IM_COL32(0, 0, 0, 50) & IM_COL32_A_MASK);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, bg);
                }
                if (m.book) ImGui::PushStyleColor(ImGuiCol_Text, ui::MUTED4);
                const auto label = fmt::format("{}{}##t{}", m.san, judgementMark(judged), i);
                if (ImGui::Selectable(label.c_str(), on_main == i + 1 && b.line < 0, ImGuiSelectableFlags_None))
                    showBoardLine(-1, i + 1);
                if (m.book) ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                if (m.book) {
                    ImGui::TextDisabled("%s", TR("book"));
                } else if (m.score) {
                    ImGui::TextUnformatted(ui::formatScore(whiteScore(*m.score, m.mover)).c_str());
                }
                ImGui::TableNextColumn();
                if (!m.book && m.depth > 0) ImGui::Text("%d", m.depth);
                ImGui::TableNextColumn();
                if (!m.book && m.time_ms >= 0) {
                    ImGui::TextUnformatted(ui::formatClock(m.time_ms).c_str());
                    ImGui::SameLine(0, px(4));
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    const float w  = px(36) * static_cast<float>(m.time_ms) / static_cast<float>(longest);
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        ImVec2(p.x, p.y + ImGui::GetTextLineHeight() * 0.35f),
                        ImVec2(p.x + std::max(1.0f, w), p.y + ImGui::GetTextLineHeight() * 0.7f),
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), px(2));
                    ImGui::Dummy(ImVec2(px(36), ImGui::GetTextLineHeight()));
                }
            }
        }
    }
    ImGui::EndTable();
}

float App::thisMoveHeight() const {
    const auto& b = board_;
    const auto& m = b.game.moves[static_cast<std::size_t>(b.ply - 1)];
    int rows = 1 + (!m.book && m.pv.find(' ') != std::string::npos ? 1 : 0) +
               (static_cast<std::size_t>(b.ply) < b.tb.size() && b.tb[static_cast<std::size_t>(b.ply)] != -2 ? 1 : 0);
    for (const auto* r : shownReviews())
        if (static_cast<std::size_t>(b.ply) < r->positions.size()) ++rows;
    return static_cast<float>(rows) * ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2;
}

int App::judgedPly(int direction) const {
    const auto& b   = board_;
    const int total = static_cast<int>(std::min(b.judged.size(), b.tree.main.moves.size()));
    for (int ply = boardMainPly() + direction; ply >= 1 && ply <= total; ply += direction) {
        if (b.judged[static_cast<std::size_t>(ply - 1)].judgement != Judgement::None) return ply;
    }
    return -1;
}

void App::drawThisMove() {
    const ui::TopicRegion region(guide::topics::ThisMove);
    auto& b          = board_;
    const int ply    = b.ply;
    const auto& m    = b.game.moves[static_cast<std::size_t>(ply - 1)];
    const auto prior = boardAt(b.game, b.chess960, ply - 1);
    const auto after = boardAt(b.game, b.chess960, ply);
    const auto split = [](const std::string& pv) {
        std::vector<std::string> out;
        std::istringstream in(pv);
        for (std::string w; in >> w;) out.push_back(w);
        return out;
    };
    const auto name_of = [&](chess::Color c) {
        auto name = b.game.header(c == chess::Color::WHITE ? "White" : "Black");
        return name.empty() || name == "?" ? std::string(c == chess::Color::WHITE ? TR("White") : TR("Black")) : name;
    };
    const auto white = [](fastchess::Score score, chess::Color stm) {
        if (stm == chess::Color::BLACK) score.value = -score.value;
        return ui::formatScore(score);
    };
    const auto reviews = shownReviews();
    float label_w      = ImGui::CalcTextSize(TR("This move")).x;
    label_w            = std::max(label_w, ImGui::CalcTextSize(TR("It expected")).x);
    for (const auto* r : reviews) label_w = std::max(label_w, ImGui::CalcTextSize(r->engine.c_str()).x);
    label_w += px(12);
    const float show_w = ImGui::CalcTextSize(TR("Show")).x + ImGui::GetStyle().FramePadding.x * 2;
    int row_id         = 0;
    // A row: its label, its text (cut to the room), a Show that lays a line on the board.
    const auto row = [&](const std::string& label, const std::string& text, int base, std::vector<std::string> line,
                         const std::string& line_label) {
        ImGui::PushID(row_id++);
        const float start = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label.c_str());
        ImGui::SameLine(start + label_w);
        const float room  = ImGui::GetContentRegionAvail().x - (line.empty() ? 0.0f : show_w + px(8));
        std::string shown = text;
        while (!shown.empty() && ImGui::CalcTextSize(shown.c_str()).x > room) shown.pop_back();
        if (shown.size() < text.size() && shown.size() > 3) shown = shown.substr(0, shown.size() - 3) + "…";
        ImGui::TextUnformatted(shown.c_str());
        if (shown.size() < text.size() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text.c_str());
        if (!line.empty()) {
            ImGui::SameLine(ImGui::GetContentRegionMax().x - show_w);
            const bool on = b.preview && !b.preview->hover && b.preview->label == line_label;
            if (ImGui::SmallButton(on ? TR("Back") : TR("Show"))) {
                if (on)
                    b.preview.reset();
                else
                    b.preview = BoardPreview{base, b.ply, std::move(line), line_label, false};
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("The line on the board, the game untouched."));
        }
        ImGui::PopID();
    };

    ImGui::Separator();
    // The move, and what the engine that played it said.
    const int number  = prior.fullMoveNumber();
    const auto judged = (b.line < 0 || boardMainPly() == ply) && static_cast<std::size_t>(ply - 1) < b.judged.size()
                            ? b.judged[static_cast<std::size_t>(ply - 1)]
                            : MoveReview{};
    std::string text  = fmt::format("{}{}{}{}", number, m.mover == chess::Color::WHITE ? "." : "…", boardSan(m.san),
                                    judgementMark(judged.judgement));
    if (m.book) {
        text += "   " + std::string(TR("book move"));
    } else if (m.score) {
        text += "   " + name_of(m.mover) + ": " + white(*m.score, m.mover);
        if (m.depth > 0)
            text += m.seldepth > 0 ? fmt::format(" · d{}/{}", m.depth, m.seldepth) : fmt::format(" · d{}", m.depth);
        // In the Chess space, the time of a tournament's ChessBase style too, its [%emt] (#87).
        if (const auto t = chessBoard() ? moveTime(m) : m.time_ms; t >= 0) text += " · " + ui::formatClock(t);
        if (m.nodes > 0) text += " · " + ui::formatCount(m.nodes) + " n";
        if (m.nps > 0) text += " · " + ui::formatCount(static_cast<uint64_t>(m.nps)) + " n/s";
        if (m.tbhits > 0) text += " · TB " + ui::formatCount(m.tbhits);
    }
    row(TR("This move"), text, 0, {}, "");
    // The line it expected after it: in the Chess space, in the reader's piece letters.
    if (!m.book) {
        auto pv = split(m.pv);
        if (pv.size() > 1) {
            pv.erase(pv.begin());
            const auto local = [&](std::string line) { return chessBoard() ? localLine(line) : line; };
            const auto san   = local(pvToSan(after, pv, 10));
            row(TR("It expected"), san, ply, pv, trf("{} expected {}", name_of(m.mover), local(pvToSan(after, pv, 6))));
        }
    }
    // The tablebases: the result after the move, and whether the move kept the one before it.
    if ((b.line < 0 || boardMainPly() == ply) && static_cast<std::size_t>(ply) < b.tb.size() &&
        b.tb[static_cast<std::size_t>(ply)] != -2) {
        const int now  = b.tb[static_cast<std::size_t>(ply)];
        const auto stm = after.sideToMove();
        std::string tb = tablebaseScore(now, stm);
        const int was  = b.tb[static_cast<std::size_t>(ply - 1)];
        if (was != -2) {
            const char* before_score = tablebaseScore(was, prior.sideToMove());
            tb = std::string(before_score) == tb ? trf("{}, kept", tb)
                                                 : trf("{} → {}: the move spoils it", before_score, tb);
        }
        row(TR("Tablebases"), tb, 0, {}, "");
        ui::topic(guide::topics::BoardTablebases);
    }
    // Each analysis's verdict, and its best line from the position before the move: of the game's moves only.
    const bool on_main = b.line < 0 || boardMainPly() == b.ply;
    for (std::size_t i = 0; i < reviews.size() && on_main; ++i) {
        const auto& r = *reviews[i];
        if (static_cast<std::size_t>(ply) >= r.positions.size()) continue;
        const auto& before = r.positions[static_cast<std::size_t>(ply - 1)];
        MoveReview verdict;
        if (i == 0) {
            verdict = judged;
        } else if (const auto all = reviewMoves(b.game, b.chess960, r);
                   static_cast<std::size_t>(ply - 1) < all.size()) {
            verdict = all[static_cast<std::size_t>(ply - 1)];
        }
        std::string what;
        const auto best      = legalPrefix(prior, {before.best}, 1);
        const auto best_text = best.empty() || !before.score ? std::string()
                                                             : chess::uci::moveToSan(prior, best.front()) + " (" +
                                                                   white(*before.score, prior.sideToMove()) + ")";
        if (!before.score || !r.positions[static_cast<std::size_t>(ply)].score) {
            what = "…";
        } else if (verdict.best) {
            what = trf("The best move ({})", white(*before.score, prior.sideToMove()));
        } else {
            const char* kind = verdict.judgement == Judgement::Blunder      ? TR("Blunder")
                               : verdict.judgement == Judgement::Mistake    ? TR("Mistake")
                               : verdict.judgement == Judgement::Inaccuracy ? TR("Inaccuracy")
                                                                            : nullptr;
            what             = kind ? trf("{}: {:.0f} % of the winning chances lost", kind, verdict.loss)
                                    : trf("{:.0f} % of the winning chances lost", verdict.loss);
            if (!best_text.empty()) what += " · " + trf("best {}", best_text);
        }
        row(r.engine, what, ply - 1, split(before.pv), trf("{}: {}", r.engine, pvToSan(prior, split(before.pv), 6)));
    }
}

// ---------------------------------------------------------------------------
// The game graph

float App::boardGraphHeight() const {
    const auto& b = board_;
    if (b.game.moves.empty()) return 0.0f;
    const float pad = ImGui::GetStyle().WindowPadding.y * 2;
    if (b.settings.graph.folded) return foldedGraphHeight() + pad;
    return std::floor(std::max(px(230), std::min(px(320), ImGui::GetContentRegionAvail().y * 0.36f)));
}

std::string App::boardGraphTip(int ply) const {
    const auto& b     = board_;
    const auto judged = ply > 0 && static_cast<std::size_t>(ply - 1) < b.judged.size()
                            ? b.judged[static_cast<std::size_t>(ply - 1)]
                            : MoveReview{};
    auto tip =
        graphMoveTip(b.tree.main, ply, judged.judgement != Judgement::None ? judgementMark(judged.judgement) : "");
    const auto* review = shownReview();
    if (!tip.empty() && review && static_cast<std::size_t>(ply) < review->positions.size()) {
        const auto& pos = review->positions[static_cast<std::size_t>(ply)];
        if (const float v = graphPawns(pos.score, ~b.tree.main.moves[static_cast<std::size_t>(ply - 1)].mover);
            std::isfinite(v)) {
            const auto score =
                ui::formatScore(whiteScore(*pos.score, ~b.tree.main.moves[static_cast<std::size_t>(ply - 1)].mover));
            // A position taken as Lichess's evaluations kept offline have it (#58), not searched.
            tip += "\n" + (pos.known ? trf("Lichess offline, depth {}: {}", pos.depth, score)
                                     : trf("Analysis ({}): {}", review->engine, score));
        }
    }
    return tip;
}

void App::drawBoardGraph(float height) {
    auto& b = board_;
    if (height <= 0) return;
    ui::beginCard("board_graph", ImVec2(0, height));
    std::optional<ui::TopicRegion> region(std::in_place, guide::topics::GameGraph);
    const auto reviews = shownReviews();
    std::vector<GraphAnalysis> analyses;
    for (std::size_t i = 0; i < reviews.size(); ++i) {
        analyses.push_back({reviews[i]->engine, i == 0 && reviews.size() > 1 ? TR("judge") : TR("analysis"),
                            reviewColour(i, reviews[i]->engine), reviews[i]});
    }
    // The study Board's engines side by side (#78): what each one's search said of the game's positions it reached,
    // a thin line of its colour, unless it has an analysis of the game; the first's is the curve when none has.
    const auto with_menu = analyses.size();
    if (chessBoard() && !b.trails.empty() && b.trails_key == reviewKey(b.tree.main)) {
        const auto list = analysisEngines(b.settings);
        for (std::size_t k = 0; k < list.size(); ++k) {
            if (std::any_of(reviews.begin(), reviews.end(), [&](const auto* r) { return r->engine == list[k].engine; }))
                continue;
            const auto it = b.trails.find(list[k].engine);
            if (it != b.trails.end())
                analyses.push_back({list[k].engine, TR("live analysis"), sideColour(k), &it->second});
        }
    }
    auto graph      = buildGameGraph(b.tree.main, b.chess960, b.settings.graph.chart, analyses, b.judged);
    graph.tablebase = boardTablebasePly();
    // A mark is settled when the last pass did not change it and every other analysis judged the move alike.
    if (!b.reviews.empty()) {
        const auto& previous = b.reviews.front().previous;
        std::vector<std::vector<MoveReview>> others;
        for (std::size_t i = 1; i < b.reviews.size(); ++i) {
            if (b.reviews[i].complete(b.tree.main.moves.size()))
                others.push_back(reviewMoves(b.tree.main, b.chess960, b.reviews[i]));
        }
        for (auto& m : graph.marks) {
            const auto i = static_cast<std::size_t>(m.ply - 1);
            m.hollow     = i < previous.size() && previous[i] != static_cast<int8_t>(m.judgement);
            for (const auto& o : others) m.hollow |= i < o.size() && o[i].judgement != m.judgement;
        }
    }
    GraphDraw draw;
    draw.current       = boardMainPly();
    draw.tip           = [this](int ply) { return boardGraphTip(ply); };
    draw.can_add       = !b.tree.main.moves.empty() && !(b.watching && b.match && b.match_state.running);
    draw.analyses_menu = true;
    draw.menu_analyses = static_cast<int>(with_menu);  // not the live analysis's lines
    // The analyses in passes: where they are, what each position got.
    const bool running = b.reviewing() && b.review_game_key == b.review_key;
    if (running) {
        const auto ceiling = b.settings.review_ceiling_ms;
        const int passes   = reviewPasses(ceiling);
        int pass           = 1 << 20;
        std::size_t done = 0, all = 0;
        for (const auto& t : b.review_tasks) pass = std::min(pass, t.state.pass);
        for (const auto& t : b.review_tasks) {
            if (t.state.pass != pass) {
                done += t.state.done.size();  // waiting for the others
            } else {
                done += static_cast<std::size_t>(std::count(t.state.done.begin(), t.state.done.end(), true));
            }
            all += t.state.done.size();
        }
        // The Chess space's passes are of nodes (#61).
        if (chessBoard())
            draw.progress =
                ceiling > 0
                    ? trf("pass {} of {} · {} · {}/{}", pass + 1, passes, reviewNodesText(passNodes(pass)), done, all)
                    : trf("pass {} · {} · {}/{}", pass + 1, reviewNodesText(passNodes(pass)), done, all);
        else
            draw.progress = ceiling > 0 ? trf("pass {} of {} · {} per position · {}/{}", pass + 1, passes,
                                              ui::formatClock(passTime(pass)), done, all)
                                        : trf("pass {} · {} per position · {}/{}", pass + 1,
                                              ui::formatClock(passTime(pass)), done, all);
        if (b.analysing)
            draw.progress +=
                " · " + std::string(chessBoard() ? TR("paused while Alt+A analyses") : TR("paused while A analyses"));
        const auto& job = b.review_tasks.front().state;
        // The positions Lichess's evaluations kept offline know deep enough, taken as they are (#58).
        const auto known = static_cast<int>(std::count_if(job.live.positions.begin(), job.live.positions.end(),
                                                          [](const PositionEval& p) { return p.known; }));
        if (known > 0)
            draw.progress +=
                " · " + trn("{} position known from Lichess", "{} positions known from Lichess", known, known);
        const int top   = ceiling > 0 ? passes : pass + 1;
        for (const int level : job.level)
            graph.level.push_back(level < 0 ? 0.0f : static_cast<float>(level + 1) / static_cast<float>(top));
        draw.action = TR("Stop");
        draw.action_tip =
            chessBoard() ? TR("Alt+G: stop the analysis of the game. Its passes done are kept, and it goes on from "
                              "there next time.")
                         : TR("G: stop the analysis of the game. Its passes done are kept, and it goes on from there "
                              "next time.");
    } else if (!b.reviewing() && draw.can_add) {
        // An analysis goes on from the passes done the same way: a number of nodes in the Chess space (#61), a time
        // in the Engines space; done the other way, it is made again.
        bool more = false, other_way = false;
        for (const auto& r : b.reviews) {
            const bool same_way = chessBoard() ? r.passes > 0 && r.nodes == passNodes(r.passes - 1) : r.nodes == 0;
            more |= same_way && r.passes > 0 && r.passes < reviewPasses(b.settings.review_ceiling_ms);
            other_way |= !same_way && r.passes > 0;
        }
        draw.action = more                                               ? TR("Continue the analysis")
                      : b.reviews.empty() || (other_way && chessBoard()) ? TR("Analyse the game")
                                                                         : nullptr;
        draw.action_tip =
            chessBoard() ? TR("Alt+G: the analysis engine goes through every position of the game, in the background, "
                              "then again further at each pass: the graph comes at once and sharpens.")
                         : TR("G: the analysis engine goes through every position of the game, in the background, "
                              "then again for longer at each pass: the graph comes at once and sharpens.");
    }
    const auto ev = drawGameGraph("##board_graph", graph, b.settings.graph, draw,
                                  ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y));
    if (ev.go_to >= 0) showBoardLine(-1, ev.go_to);
    if (ev.view_changed) saveBoardSettings();
    if (ev.action) running ? cancelGameReview() : startGameReview();
    if (ev.judge >= 0 && static_cast<std::size_t>(ev.judge) < reviews.size())
        judgeWith(reviews[static_cast<std::size_t>(ev.judge)]->engine);
    if (ev.remove >= 0 && static_cast<std::size_t>(ev.remove) < reviews.size())
        removeReview(reviews[static_cast<std::size_t>(ev.remove)]->engine);
    // Another engine: those of the library that have no analysis of the game yet.
    if (ev.add_analysis) ImGui::OpenPopup("##add_analysis");
    if (ImGui::BeginPopup("##add_analysis")) {
        ImGui::TextDisabled("%s", TR("Another engine analyses the game too, for as long at each pass:"));
        bool any = false;
        for (const auto& e : engines_) {
            if (e.inactive ||
                std::any_of(reviews.begin(), reviews.end(), [&](const auto* r) { return r->engine == e.name; }))
                continue;
            any = true;
            if (ImGui::MenuItem(e.name.c_str())) addReviewEngine(e.name);
        }
        if (!any) ImGui::TextDisabled("%s", TR("Every engine of the library analyses it already."));
        ImGui::EndPopup();
    }
    region.reset();  // its group ends inside the card
    ui::endCard();
}

std::string App::boardMoveLabel(int ply) const {
    const auto& main = board_.tree.main;
    if (ply <= 0 || ply > static_cast<int>(main.moves.size())) return {};
    const auto before = boardAt(main, board_.chess960, ply - 1);
    const auto& m     = main.moves[static_cast<std::size_t>(ply - 1)];
    return fmt::format("{}{}{}", before.fullMoveNumber(), m.mover == chess::Color::WHITE ? "." : "…", boardSan(m.san));
}

std::string App::boardSan(std::string san) const {
    if (!chessBoard()) return san;
    return sanIn(san, i18n::language());
}

void App::drawBoardReview() {
    auto& b          = board_;
    const auto& main = b.tree.main;
    // A tournament's game in the Chess space: the curve of its two engines first, from the game alone, without any
    // analysis; the review's over it once done (#87, mockup M12, board 8).
    const bool engines_curve = chessBoard() && hasPlayerEvaluations(main);
    if (engines_curve) {
        drawEnginesCurve();
        ImGui::Spacing();
    }
    if (b.reviews.empty() && engines_.empty()) {
        drawFirstEngineCard(TR("The game's review needs an engine: it analyses each position of the game."));
        return;
    }
    if (b.reviews.empty() || !b.reviews.front().complete(main.moves.size())) {
        ImGui::TextWrapped("%s", chessBoard() ? TR("Analyse the game (Alt+G) to review it: each side's accuracy, its "
                                                   "mistakes, the key moments, and who saw them coming.")
                                              : TR("Analyse the game (G) to review it: each side's accuracy, its "
                                                   "mistakes, the key moments, and who saw them coming."));
        if (!b.reviewing() && !main.moves.empty() && ImGui::Button(TR("Analyse the game"))) startGameReview();
        return;
    }
    if (chessBoard() && !engines_curve) {
        drawReviewCurve();
        ImGui::Spacing();
    }
    const auto& review = b.reviews.front();
    const auto phases  = gamePhases(main, b.chess960);
    const auto stats   = gameStats(main, b.chess960, review, b.judged, phases);
    const float gap    = px(10);
    const float half   = (ImGui::GetContentRegionAvail().x - gap) / 2;
    const ImU32 box = ui::currentTheme() == ui::Theme::Light ? IM_COL32(243, 245, 248, 255) : IM_COL32(32, 36, 43, 255);
    const auto name_of = [&](int side) {
        auto name = main.header(side == 0 ? "White" : "Black");
        return name.empty() || name == "?" ? std::string(side == 0 ? TR("White") : TR("Black")) : name;
    };
    const auto percent = [](const std::optional<double>& v, double scale = 1.0) {
        return v ? fmt::format("{:.0f} %", *v * scale) : std::string("–");
    };

    // Each side: its accuracy, then its figures.
    const float rows  = ImGui::GetTextLineHeightWithSpacing();
    const float box_h = ImGui::GetFontSize() * 2.4f + rows * 5.4f + px(16);
    const ImVec2 top  = ImGui::GetCursorScreenPos();
    auto* dl          = ImGui::GetWindowDrawList();
    for (int side = 0; side < 2; ++side) {
        const auto& st = stats[static_cast<std::size_t>(side)];
        const ImVec2 p0(top.x + static_cast<float>(side) * (half + gap), top.y);
        dl->AddRectFilled(p0, ImVec2(p0.x + half, p0.y + box_h), box, px(8));
        ImGui::SetCursorScreenPos(ImVec2(p0.x + px(12), p0.y + px(8)));
        ImGui::BeginGroup();
        const ImVec2 dot = ImGui::GetCursorScreenPos();
        const float r    = ImGui::GetFontSize() * 0.35f;
        const ImVec2 c(dot.x + r, dot.y + ImGui::GetTextLineHeight() / 2);
        playerDot(dl, c, r, side == 0 ? chess::Color::WHITE : chess::Color::BLACK);
        ImGui::Dummy(ImVec2(r * 2 + px(4), ImGui::GetTextLineHeight()));
        ImGui::SameLine();
        ImGui::TextUnformatted(name_of(side).c_str());
        ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize() * 1.8f);
        ImGui::TextUnformatted(percent(st.accuracy).c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", TR("accuracy"));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("lichess's accuracy: each move's from the winning chances it lost, the moves "
                                       "of the game's sharp moments weighing more."));
        const float value_x = half - px(24);
        const auto kv       = [&](const char* label, const std::string& value) {
            ImGui::TextDisabled("%s", label);
            ImGui::SameLine(value_x - ImGui::CalcTextSize(value.c_str()).x);
            ImGui::TextUnformatted(value.c_str());
        };
        kv("?! · ? · ??", fmt::format("{} · {} · {}", st.judgements[0], st.judgements[1], st.judgements[2]));
        kv(TR("Average loss"), st.average_loss ? fmt::format("{:.0f} cp", *st.average_loss) : std::string("–"));
        kv(TR("Same move as the analysis"), percent(st.agreement, 100.0));
        kv(TR("Average depth"), st.depth ? fmt::format("{:.1f}", *st.depth) : std::string("–"));
        kv(TR("Time used"), st.time_ms > 0 ? ui::formatClock(st.time_ms) : std::string("–"));
        ImGui::EndGroup();
    }
    ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + box_h + px(8)));

    // Two opinions on the same game (#49, mockup M8, board "Fairplay"): each analysis's accuracy, Lichess's too when
    // the game came with its evaluations.
    {
        std::vector<std::pair<std::string, std::array<SideStats, 2>>> opinions;
        for (std::size_t i = 1; i < b.reviews.size(); ++i)
            if (b.reviews[i].complete(main.moves.size()))
                opinions.emplace_back(
                    b.reviews[i].engine,
                    gameStats(main, b.chess960, b.reviews[i], reviewMoves(main, b.chess960, b.reviews[i]), phases));
        if (const auto own = commentsReview(main))
            opinions.emplace_back(lichessGame(main) ? TR("Lichess's analysis") : TR("The game's evaluations"),
                                  gameStats(main, b.chess960, *own, reviewMoves(main, b.chess960, *own), phases));
        if (!opinions.empty()) {
            opinions.insert(opinions.begin(), {review.engine, stats});
            for (const auto& [name, st] : opinions)
                ImGui::TextDisabled(
                    "%s", trf("{}: accuracy {} · {}", name, percent(st[0].accuracy), percent(st[1].accuracy)).c_str());
            ImGui::Spacing();
        }
    }

    // The accuracy in each phase.
    {
        const char* names[] = {TR("Opening"), TR("Middlegame"), TR("Endgame")};
        const float label_w = std::max({ImGui::CalcTextSize(names[0]).x, ImGui::CalcTextSize(names[1]).x,
                                        ImGui::CalcTextSize(names[2]).x}) +
                              px(12);
        const float bar_w   = (ImGui::GetContentRegionAvail().x - label_w - gap) / 2;
        for (std::size_t p = 0; p < 3; ++p) {
            if (!stats[0].phase_accuracy[p] && !stats[1].phase_accuracy[p]) continue;
            ImGui::TextDisabled("%s", names[p]);
            for (std::size_t side = 0; side < 2; ++side) {
                ImGui::SameLine(label_w + static_cast<float>(side) * (bar_w + gap));
                const ImVec2 q = ImGui::GetCursorScreenPos();
                const float h  = ImGui::GetTextLineHeight() * 0.45f;
                const float y  = q.y + (ImGui::GetTextLineHeight() - h) / 2;
                dl->AddRectFilled(ImVec2(q.x, y), ImVec2(q.x + bar_w - px(40), y + h), ui::SELECT, h / 2);
                if (const auto& a = stats[side].phase_accuracy[p]) {
                    dl->AddRectFilled(ImVec2(q.x, y),
                                      ImVec2(q.x + (bar_w - px(40)) * static_cast<float>(*a) / 100.0f, y + h),
                                      ui::ACCENT, h / 2);
                    dl->AddText(ImVec2(q.x + bar_w - px(34), q.y), ui::MUTED, fmt::format("{:.0f}", *a).c_str());
                }
                ImGui::Dummy(ImVec2(bar_w, ImGui::GetTextLineHeight()));
            }
        }
    }

    // Several analyses: each one's accuracies.
    if (b.reviews.size() > 1) {
        std::string line;
        for (const auto& r : b.reviews) {
            if (!r.complete(main.moves.size())) continue;
            const auto judged = reviewMoves(main, b.chess960, r);
            const auto s      = gameStats(main, b.chess960, r, judged, phases);
            line += (line.empty() ? "" : " · ") +
                    fmt::format("{}: {} / {}", r.engine, percent(s[0].accuracy), percent(s[1].accuracy));
        }
        ImGui::TextDisabled("%s", trf("Accuracy by each analysis: {}", line).c_str());
    }

    // The key moments, each a click away.
    ImGui::Spacing();
    ImGui::TextDisabled("%s", TR("KEY MOMENTS"));
    // In the Chess space, the review's own: the dots of its curve, the verdicts of its symbols and words.
    const auto commentary = chessBoard() ? boardCommentaryReview() : nullptr;
    const auto moments    = commentary && commentary->report.complete
                                ? reviewKeyMoments(*commentary, main, review, b.judged, phases, boardTablebasePly())
                                : keyMoments(main, review, b.judged, phases, boardTablebasePly());
    if (moments.empty())
        ImGui::TextDisabled("%s", TR("No mistake: the game was lost little by little, or not at all."));
    const auto pawns = [](double v) {
        if (std::fabs(v) >= MATE_PAWNS * 0.99) return std::string(v > 0 ? "+M" : "-M");
        const double r = std::round(v * 100.0) / 100.0;
        return r == 0.0 ? std::string("0.00") : fmt::format("{:+.2f}", r);
    };
    const auto best_of = [&](int ply) -> std::string {
        if (ply <= 0 || static_cast<std::size_t>(ply - 1) >= review.positions.size()) return {};
        const auto& before = review.positions[static_cast<std::size_t>(ply - 1)];
        const auto board   = boardAt(main, b.chess960, ply - 1);
        const auto best    = legalPrefix(board, {before.best}, 1);
        return best.empty() ? std::string() : boardSan(chess::uci::moveToSan(board, best.front()));
    };
    const float move_w = px(96);
    for (std::size_t k = 0; k < moments.size(); ++k) {
        const auto& m = moments[k];
        std::string what;
        switch (m.kind) {
            case KeyMoment::Kind::TurningPoint:
                what = trf("Turning point: {} → {}.", pawns(m.before), pawns(m.after));
                if (const auto best = best_of(m.ply); !best.empty()) what += " " + trf("Best {}.", best);
                break;
            case KeyMoment::Kind::Judged:
                what = trf("{}: {:.0f} % of the winning chances lost.",
                           m.judgement == Judgement::Blunder   ? TR("Blunder")
                           : m.judgement == Judgement::Mistake ? TR("Mistake")
                                                               : TR("Inaccuracy"),
                           b.judged[static_cast<std::size_t>(m.ply - 1)].loss);
                if (const auto best = best_of(m.ply); !best.empty()) what += " " + trf("Best {}.", best);
                break;
            case KeyMoment::Kind::OnlyMove:
                // The engine's next best beside it (#63, mockup M5: "Gaia: +0.62, the next +0.31").
                what = m.exact ? std::string(TR("Only move, according to the tablebases."))
                       : std::isnan(m.after)
                           ? std::string(TR("Only move."))
                           : trf("Only move. {}: {}, the next {}.", review.engine, pawns(m.before), pawns(m.after));
                break;
            case KeyMoment::Kind::Disagreement:
                what = trf("The engines disagreed: {} against {}.", pawns(m.before), pawns(m.after));
                break;
            case KeyMoment::Kind::Endgame:
                what = TR("The endgame starts.");
                break;
            case KeyMoment::Kind::Tablebase:
                what = TR("The tablebases know the result from here.");
                break;
        }
        ImGui::PushID(static_cast<int>(k));
        const bool current = boardMainPly() == m.ply && b.line < 0;
        if (ImGui::Selectable("##moment", current, ImGuiSelectableFlags_None)) showBoardLine(-1, m.ply);
        ImGui::SameLine(px(4));
        auto label = boardMoveLabel(m.ply);
        if (m.judgement != Judgement::None) label += judgementMark(m.judgement);
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine(move_w);
        ImGui::TextWrapped("%s", what.c_str());
        ImGui::PopID();
    }

    // Who saw the turning point coming first: the engines that played, the analyses.
    const auto turning = std::find_if(moments.begin(), moments.end(),
                                      [](const auto& m) { return m.kind == KeyMoment::Kind::TurningPoint; });
    if (turning != moments.end()) {
        const int sign = turning->after > turning->before ? 1 : -1;
        std::vector<GraphAnalysis> analyses;
        for (const auto& r : b.reviews) analyses.push_back({r.engine, "", 0, &r});
        const auto graph = buildGameGraph(main, b.chess960, GraphChart::Eval, analyses, b.judged);
        std::vector<std::pair<int, std::string>> seen;
        for (const auto& c : graph.players)
            if (!c.x.empty()) seen.emplace_back(sawItAt(c, turning->ply, sign), c.name);
        for (const auto& c : graph.analyses)
            seen.emplace_back(sawItAt(c, turning->ply, sign), trf("{} (analysis)", c.name));
        std::stable_sort(seen.begin(), seen.end(), [](const auto& x, const auto& y) {
            return (x.first < 0 ? 1 << 20 : x.first) < (y.first < 0 ? 1 << 20 : y.first);
        });
        std::string text;
        for (const auto& [ply, who] : seen) {
            // A sentence of its own each: "at at the start" and "at never" did not read.
            const auto said = ply < 0    ? trf("{}: never", who)
                              : ply == 0 ? trf("{} from the start", who)
                                         : trf("{} at {}", who, boardMoveLabel(ply));
            text += (text.empty() ? "" : " · ") + said;
        }
        ImGui::Spacing();
        ImGui::TextDisabled("%s", TR("WHO SAW IT FIRST"));
        ImGui::TextWrapped("%s", text.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("For each engine, the first position, from 12 plies before the turning point, "
                                       "where it gave the side that gains a pawn or more."));
        }
    }

    // The game with its analysis, to the clipboard; the review for a forum.
    ImGui::Spacing();
    if (ImGui::Button(TR("Copy PGN with the analysis")))
        ImGui::SetClipboardText(pgnWithAnalysis(b.tree, b.chess960, review, b.judged).c_str());
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("The game as PGN with the analysis written in: each position's evaluation "
                                   "([%eval]), the judgements as NAGs, the line the analysis preferred after a marked "
                                   "move."));
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Copy as BBCode"))) {
        std::string out = fmt::format("[b]{} – {}[/b] {}\n", name_of(0), name_of(1), main.result);
        out += (review.nodes > 0
                    ? trf("Analysed by {}, {}.", review.engine, reviewNodesText(review.nodes))
                    : trf("Analysed by {}, {} per position.", review.engine, ui::formatClock(review.movetime_ms))) +
               "\n";
        for (int side = 0; side < 2; ++side) {
            const auto& st = stats[static_cast<std::size_t>(side)];
            out += fmt::format("[b]{}[/b]: {} {} · ?! {} · ? {} · ?? {} · {} {}\n", name_of(side), TR("accuracy"),
                               percent(st.accuracy), st.judgements[0], st.judgements[1], st.judgements[2],
                               TR("average loss"), st.average_loss ? fmt::format("{:.0f}", *st.average_loss) : "–");
        }
        for (const auto& m : moments) {
            if (m.kind != KeyMoment::Kind::TurningPoint && m.kind != KeyMoment::Kind::Judged) continue;
            out += fmt::format("{}{} ({} → {})\n", boardMoveLabel(m.ply), judgementMark(m.judgement), pawns(m.before),
                               pawns(m.after));
        }
        ImGui::SetClipboardText(out.c_str());
    }
    if (chessBoard()) drawReviewAssistant();  // the story, the key moments commented, the annotation (M5, board 4)
    ui::topic(guide::topics::GameAnalysis);
}

std::string App::judgementTip(int ply, const MoveReview& judged) const {
    const auto& b    = board_;
    const char* what = judged.judgement == Judgement::Blunder   ? TR("Blunder")
                       : judged.judgement == Judgement::Mistake ? TR("Mistake")
                                                                : TR("Inaccuracy");
    auto tip         = trf("{}: {:.0f} % of the winning chances lost.", what, judged.loss);
    // What the engine would have played.
    const auto* review = b.reviews.empty() ? nullptr : &b.reviews.front();
    if (review && static_cast<std::size_t>(ply) < review->positions.size()) {
        const auto& before = review->positions[static_cast<std::size_t>(ply)];
        const auto board   = boardAt(b.tree.main, b.chess960, ply);
        const auto best    = legalPrefix(board, {before.best}, 1);
        if (!best.empty() && before.score) {
            tip += "\n" + trf("Best: {} ({})", chess::uci::moveToSan(board, best.front()),
                              ui::formatScore(whiteScore(*before.score, board.sideToMove())));
        }
    }
    return tip;
}

void App::drawGameReview() {
    auto& b = board_;
    // What the analysis found, by side.
    if (b.reviews.empty() || !b.reviews.front().complete(b.tree.main.moves.size())) return;
    const auto& review = b.reviews.front();
    const auto counts  = judgementCounts(b.tree.main, b.judged);
    const auto side    = [&](int colour) {
        const auto& c = counts[static_cast<std::size_t>(colour)];
        std::string parts;
        const auto add = [&](std::string part) { parts += (parts.empty() ? "" : ", ") + part; };
        if (c[2]) add(trn("{} blunder", "{} blunders", c[2], c[2]));
        if (c[1]) add(trn("{} mistake", "{} mistakes", c[1], c[1]));
        if (c[0]) add(trn("{} inaccuracy", "{} inaccuracies", c[0], c[0]));
        if (parts.empty()) parts = TR("no mistake");
        auto name = b.tree.main.header(colour == 0 ? "White" : "Black");
        if (name.empty() || name == "?") name = colour == 0 ? TR("White") : TR("Black");
        return fmt::format("{}: {}", name, parts);
    };
    ImGui::TextWrapped("%s", trf("Analysis: {} · {}", side(0), side(1)).c_str());
    if (ImGui::IsItemHovered()) {
        const auto how = review.nodes > 0   ? reviewNodesText(review.nodes)
                         : review.depth > 0 ? trf("depth {}", review.depth)
                                            : trf("{:g} s per move", static_cast<double>(review.movetime_ms) / 1000.0);
        ImGui::SetTooltip("%s", trf("By {}, {}. Losses of 10, 20 and 30 % of the winning chances are an "
                                    "inaccuracy (?!), a mistake (?) and a blunder (?\?).",
                                    review.engine, how)
                                    .c_str());
    }
    ui::topic(guide::topics::GameAnalysis);
}

std::vector<const GameReview*> App::shownReviews() const {
    const auto& b = board_;
    std::vector<const GameReview*> out;
    const bool running = b.reviewing() && b.review_game_key == b.review_key;
    const auto task    = [&](const std::string& engine) -> const GameReview* {
        if (!running) return nullptr;
        for (const auto& t : b.review_tasks)
            if (t.engine == engine) return &t.state.live;
        return nullptr;
    };
    for (const auto& r : b.reviews) {
        const auto* live = task(r.engine);
        out.push_back(live ? live : &r);
    }
    // Those started that have no pass done yet.
    if (running) {
        for (const auto& t : b.review_tasks) {
            if (std::none_of(b.reviews.begin(), b.reviews.end(), [&](const auto& r) { return r.engine == t.engine; }))
                out.push_back(&t.state.live);
        }
    }
    return out;
}

const GameReview* App::shownReview() const {
    const auto all = shownReviews();
    return all.empty() ? nullptr : all.front();
}

// ---------------------------------------------------------------------------
// Games played from the board's position

int App::engineThreads(const std::string& name) const {
    for (const auto& e : engines_) {
        if (e.name == name) return e.threads();
    }
    return 1;
}

void App::startBoardGames() {
    if (engine_game_ && engine_game_->over) closeEngineGame();  // a game against an engine ended: it gives way (#83)
    auto& b                  = board_;
    auto& s                  = b.settings;
    const EngineEntry *first = nullptr, *second = nullptr;
    for (const auto& e : engines_) {
        if (e.name == s.game_first) first = &e;
        if (e.name == s.game_second) second = &e;
    }
    // The user in place of an engine: only a name, the one their games give them.
    EngineEntry you;
    you.name = humanName();
    if (s.game_human == 0) first = &you;
    if (s.game_human == 1) second = &you;
    if (!first || !second) {
        b.message       = TR("Choose the two engines of the game.");
        b.message_error = true;
        return;
    }
    BoardMatchSetup setup;
    setup.first        = first->name;
    setup.second       = second->name;
    setup.first_clock  = s.first_clock;
    setup.second_clock = s.same_clock ? s.first_clock : s.second_clock;
    setup.pair         = s.game_pair;
    setup.fen          = boardFen();
    setup.chess960     = b.chess960;
    setup.human        = s.game_human;
    // The analysis would compete with the engines of the game.
    if (b.analysing) setBoardAnalysis(false);
    std::error_code ec;
    const auto dir = storage_.root() / "games";
    std::filesystem::create_directories(dir, ec);
    const auto pgn = dir / (makeRunDirName(first->name, second->name) + ".pgn");
    if (!b.match) b.match = std::make_unique<BoardMatch>();
    std::string error;
    if (!b.match->start(*first, *second, setup, pgn, error)) {
        b.message       = error;
        b.message_error = true;
        return;
    }
    takeBoardFor(setup);
}

void App::pollBoardGames() {
    auto& b = board_;
    if (!b.match) return;
    const bool changed = b.match->state(b.match_state, b.match_version);
    if (!b.match_state.running) b.match->reap();
    if (!changed || !b.watching) return;
    // The board follows the game being played, unless the user went back in it.
    const auto& st  = b.match_state;
    const bool last = b.line >= 0 || b.ply >= static_cast<int>(b.game.moves.size());
    b.game          = st.current;
    b.tree          = GameTree{b.game, {}};
    b.line          = -1;
    if (last) b.ply = static_cast<int>(b.game.moves.size());
    b.ply = std::min(b.ply, static_cast<int>(b.game.moves.size()));
    b.selected.reset();
    // A game the user plays: their side at the bottom, once a game, so that they can still turn the board.
    if (st.you && b.flipped_game != st.game) {
        b.flip         = *st.you == chess::Color::BLACK;
        b.flipped_game = st.game;
    }
    if (st.games > 1) {
        b.title = trf("{} – {} · game {} of {}", st.current.header("White"), st.current.header("Black"), st.game + 1,
                      st.games);
    }
}

void App::drawBoardPlay() {
    auto& b             = board_;
    auto& s             = b.settings;
    const bool running  = b.match && b.match_state.running;
    const bool finished = b.match && !running && !b.match_state.finished.empty();
    bool changed        = false;

    ui::beginCard("board_play");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Play a game"));
    if (!running) {
        const char* close = TR("Close");
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(8),
                                 ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(close).x - px(12)));
        if (ImGui::SmallButton(close)) {
            b.play_open = false;
            if (finished) b.match.reset();
        }
    }

    if (running || finished) {
        const auto& st = b.match_state;
        if (running) {
            const auto& g = st.current;
            ImGui::TextUnformatted(
                trf("Game {} of {}: {} – {}", st.game + 1, st.games, g.header("White"), g.header("Black")).c_str());
            if (st.you) {
                ImGui::SameLine();
                if (ImGui::SmallButton(TR("Resign the game"))) b.match->resign();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", TR("The game ends, won by the engine; the second game of a pair "
                                               "follows."));
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Stop##games"))) b.match->stop();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("The game being played ends without a result."));
            // The clocks, the one running counting down.
            const auto now = std::chrono::steady_clock::now();
            for (int side = 0; side < 2; ++side) {
                const auto colour = side == 0 ? chess::Color::WHITE : chess::Color::BLACK;
                auto ms           = st.clock_ms[static_cast<std::size_t>(side)];
                const bool thinks = st.thinking == colour;
                if (thinks) ms -= std::chrono::duration_cast<std::chrono::milliseconds>(now - st.think_start).count();
                if (side == 1) ImGui::SameLine(0, px(24));
                const auto text = fmt::format("{} {}", g.header(side == 0 ? "White" : "Black"),
                                              ui::formatClock(std::max<int64_t>(0, ms)));
                if (thinks)
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", text.c_str());
                else
                    ImGui::TextDisabled("%s", text.c_str());
            }
            // What the side to move searches; in a game the user plays, whose turn it is (the engine's search
            // would tell its plan).
            if (b.humanTurn()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", TR("Your move"));
            if (st.thinking && !st.info.empty()) {
                const auto info  = fastchess::engine::UciEngine::parseInfo(st.info);
                const auto board = boardAt(st.current, b.chess960, static_cast<int>(st.current.moves.size()));
                std::string score;
                if (info.score) score = ui::formatScore(whiteScore(*info.score, board.sideToMove()));
                ImGui::TextDisabled("%s  d%lld  %s", score.c_str(), static_cast<long long>(info.depth.value_or(0)),
                                    pvToSan(board, info.pv, 8).c_str());
            }
        }
        for (std::size_t i = 0; i < st.finished.size(); ++i) {
            const auto& g = st.finished[i];
            ImGui::TextUnformatted(
                trf("Game {}: {} – {}  {}", i + 1, g.header("White"), g.header("Black"), g.result).c_str());
            if (!g.final_comment.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", g.final_comment.c_str());
            }
        }
        if (!st.error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
            ImGui::TextWrapped("%s", st.error.c_str());
            ImGui::PopStyleColor();
        }
        if (!st.finished.empty() || finished) {
            ImGui::TextDisabled("%s", trf("Saved in {}", st.pgn.filename().string()).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", st.pgn.string().c_str());
        }
        if (finished) {
            if (ImGui::Button(TR("Open the games"))) {
                const auto file = st.pgn;
                b.match.reset();
                b.play_open = false;
                openBoardPgn(file);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TR("Their PGN file, its games listed beside the moves."));
            ImGui::SameLine();
            if (ImGui::Button(TR("Folder##games"))) ui::openPath(st.pgn.parent_path());
            ImGui::SameLine();
            if (ImGui::Button(TR("New game"))) b.match.reset();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TR("Back to the choice of the engines and the clocks, for another game."));
        }
        ui::endCard();
        return;
    }

    // In the study Board, a game of the user's against an engine is the Play page's (#83, mockup M12, board 4).
    if (chessBoard()) {
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", TR("Against an engine, at a strength, with its openings and a handicap:"));
        ImGui::PopTextWrapPos();
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("The Play page"))) {
            page_                     = Page::Play;
            playSettings().from_board = true;
            savePlaySettings();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("The game from this position, chosen there; its aids hidden until the end."));
    }
    // The engines: the first has White in the first game. Human, first in each list, is the user, in place of its
    // engine, its moves made on the board.
    const auto engineCombo = [&](const char* id, std::string& name, int side) {
        const bool you = s.game_human == side;
        ImGui::SetNextItemWidth(px(150));
        if (ImGui::BeginCombo(id,
                              you            ? TR("Human")
                              : name.empty() ? TR("Choose an engine")
                                             : name.c_str(),
                              ImGuiComboFlags_HeightLargest)) {
            std::vector<std::string> names{TR("Human")}, more{std::string()};
            for (const auto& e : engines_) {
                if (e.inactive && e.name != name) continue;
                names.push_back(e.name);
                more.push_back(engineSearchText(e));
            }
            if (const int i = ui::searchComboItems(names, you ? names.front() : name, &more); i == 0) {
                s.game_human = side;
                changed      = true;
            } else if (i > 0) {
                name = names[static_cast<std::size_t>(i)];
                if (you) s.game_human = -1;
                changed = true;
            }
            ImGui::EndCombo();
        }
    };
    if (s.game_first.empty() && !engines_.empty()) s.game_first = engines_.front().name;
    if (s.game_second.empty() && engines_.size() > 1) s.game_second = engines_[1].name;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TR("White"));
    ImGui::SameLine();
    engineCombo("##game_first", s.game_first, 0);
    ImGui::SameLine();
    if (ImGui::SmallButton("<->")) {
        std::swap(s.game_first, s.game_second);
        std::swap(s.first_clock, s.second_clock);
        if (s.game_human >= 0) s.game_human = 1 - s.game_human;
        changed = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Swap the engines and their clocks."));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("Black"));
    ImGui::SameLine();
    engineCombo("##game_second", s.game_second, 1);

    // The clocks: minutes and an increment in seconds.
    const auto clockFields = [&](const char* id, const char* label, GameClock& clock) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine();
        double minutes = clock.base_s / 60.0;
        ImGui::SetNextItemWidth(px(60));
        if (ui::inputDouble(fmt::format("##{}_base", id).c_str(), &minutes, "%g")) {
            clock.base_s = std::clamp(minutes, 0.01, 600.0) * 60.0;
            changed      = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Minutes for the game."));
        ImGui::SameLine(0, px(4));
        ImGui::TextUnformatted("+");
        ImGui::SameLine(0, px(4));
        ImGui::SetNextItemWidth(px(52));
        if (ui::inputDouble(fmt::format("##{}_inc", id).c_str(), &clock.inc_s, "%g")) {
            clock.inc_s = std::clamp(clock.inc_s, 0.0, 600.0);
            changed     = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Seconds added after each move."));
    };
    clockFields("game_first", s.same_clock ? TR("Clock") : TR("White's clock"), s.first_clock);
    ImGui::SameLine(0, px(16));
    if (ImGui::Checkbox(TR("The same for both"), &s.same_clock)) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("One clock for both engines. Untick it to give each its own, for time odds."));
    ui::topic(guide::topics::BoardPlay);
    if (!s.same_clock) clockFields("game_second", TR("Black's clock"), s.second_clock);
    if (ImGui::Checkbox(TR("A second game, the colours reversed"), &s.game_pair)) changed = true;
    if (changed) saveBoardSettings();

    // From the position on the board.
    const auto fen = boardFen();
    ImGui::TextDisabled("%s", TR("From the position on the board"));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fen.c_str());

    // Its place among the cores: refused, or played anyway, when the machine is full.
    const int need  = std::max(s.game_human == 0 ? 0 : engineThreads(s.game_first),
                               s.game_human == 1 ? 0 : engineThreads(s.game_second));
    const int busy  = tournamentThreads() + boardThreads() + botThreads();  // the Lichess bot's game too (#85)
    const bool full = busy + need > topology_.logical;
    ImGui::TextDisabled("%s", trf("Counts in the core budget · {} of {} threads", need, topology_.logical).c_str());
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("The larger thread count of the two engines: they take turns. Their games are "
                                   "played apart from any tournament."));
    }
    if (full) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
        ImGui::TextWrapped("%s", trf("The machine is full: the tournament's games and the analysis use {} of its {} "
                                     "threads, and this game needs {}. It would slow them down, and its own engines.",
                                     busy, topology_.logical, need)
                                     .c_str());
        ImGui::PopStyleColor();
    }
    const bool ready = (s.game_human == 0 || !s.game_first.empty()) && (s.game_human == 1 || !s.game_second.empty()) &&
                       positionProblem(fen, b.chess960).empty();
    ImGui::BeginDisabled(!ready);
    if (!full || b.play_anyway) {
        if (ui::iconButton("board_play_start", ui::Icon::Play, s.game_pair ? TR("Play the two games") : TR("Play")))
            startBoardGames();
    } else if (ImGui::Button(TR("Play anyway"))) {
        b.play_anyway = true;
        startBoardGames();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("Shown here as they are played, and saved as PGN in games/ in the profile."));
    }
    ui::endCard();
}

}  // namespace fcg
