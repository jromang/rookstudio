// RookStudio's additions to App::frame() and App::handleDialogs(), out of those functions so that they stay as
// fastchess-gui writes them and its releases merge without conflicts (#108): the polls of RookStudio's pages and jobs,
// its --focus words, and what becomes of the files its dialogs chose.

#include "app.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <study/evals_index.hpp>
#include <study/game_tree.hpp>
#include <study/import.hpp>
#include <study/patterns.hpp>
#include <tour/files.hpp>

#include "app_context.hpp"
#include "assistant_service.hpp"
#include "board_session.hpp"
#include "board_text.hpp"
#include "board_view.hpp"
#include "chesscom.hpp"
#include "fide_rating_list.hpp"
#include "focus_words.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "lichess_arenas_page.hpp"
#include "lichess_bot_page.hpp"
#include "lichess_months.hpp"
#include "lichess_tv_page.hpp"
#include "twic.hpp"

namespace fcg {

// What RookStudio's modules ask of App (app_context.hpp, #107), answered with App's members.
class App::ModulesContext final : public AppContext {
   public:
    explicit ModulesContext(App& app) : app_(app) {}

    // The program.
    int frame() const override { return ImGui::GetFrameCount(); }
    double seconds() const override { return ImGui::GetTime(); }
    bool shortcutPressed(int chord) const override {
        return !ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(chord);
    }
    bool scripted() const override { return app_.options_.scripted; }
    std::filesystem::path profileDir() const override { return app_.storage_.root(); }
    DatabaseShelf& shelf() override { return app_.shelf(); }
    void toast(const std::string& text) override { app_.boardToast(text); }
    JobGroup& jobs() override { return app_.jobs_; }

    // The pages.
    void showLichessPage() override {
        app_.page_ = Page::Lichess;
        app_.broadcast_round_.clear();
    }
    void showBoardPage() override { app_.page_ = Page::Board; }
    void toStudyBoard() override { app_.toStudyBoard(); }
    void showPlayTournaments(bool kept) override {
        app_.online_part_ = 1;
        if (kept) {
            app_.onlineSettings().part = 1;
            app_.saveOnlineSettings();
        }
        app_.play_part_ = 1;
        if (kept) {
            app_.playSettings().part = 1;
            app_.savePlaySettings();
        }
        app_.page_ = Page::Play;
    }
    bool playTournamentsShown() const override {
        return app_.page_ == Page::Play && app_.play_part_ == 1 && app_.online_part_ == 1;
    }
    bool homeShown() const override { return app_.page_ == Page::ChessHome; }

    // The databases' pages.
    CrosstableStore& crosstables() override { return app_.crosstableStore(); }
    void showDatabaseGames(std::size_t database, const std::string& filter) override {
        app_.db_selected_ = static_cast<int>(database);
        app_.db_list_     = true;
        std::snprintf(app_.db_filter_.data(), app_.db_filter_.size(), "%s", filter.c_str());
        app_.page_ = Page::Databases;
    }
    void openCrosstable(std::size_t database, const std::string& tournament, const std::string& base) override {
        app_.db_selected_        = static_cast<int>(database);
        app_.db_list_            = true;
        app_.db_tournaments_     = true;
        app_.page_               = Page::Databases;
        app_.crosstable_request_ = std::make_pair(tournament, base);
    }

    // The Board.
    std::string boardTitle() const override { return app_.board_.title; }
    std::string boardFollows() const override {
        return app_.board_broadcast_ ? app_.board_broadcast_->url : std::string();
    }
    void followOnBoard(ReplayGame game, std::string title, std::string mosaic, std::string url, uint64_t version,
                       bool in_place) override {
        const auto page = app_.page_;
        const auto back = app_.board_back_;
        const int ply   = static_cast<int>(game.moves.size());
        app_.openOnBoard(std::move(game), ply, false, std::move(title), false);
        BoardBroadcast follow;
        follow.round          = std::move(mosaic);
        follow.url            = std::move(url);
        follow.version        = version;
        app_.board_broadcast_ = std::move(follow);
        if (in_place) {  // the board changes its game where it is
            app_.page_       = page;
            app_.board_back_ = back;
        }
    }

    // The mosaic.
    const std::string& mosaicShown() const override { return app_.broadcast_round_; }
    int mosaicDrawn() const override { return app_.broadcast_drawn_; }
    void openMosaic(const std::string& key) override { app_.openBroadcastMosaic(key); }
    void closeMosaic() override {
        app_.broadcast_round_.clear();
        app_.broadcast_boards_.clear();
    }
    bool boardBackToMosaic() const override { return app_.boardBackToMosaic(); }
    void backToMosaic() override { app_.backToMosaic(); }
    void setMosaicEngine(const std::string& engine) override {
        if (app_.lichess_.broadcast_engine != engine) {
            app_.lichess_.broadcast_engine = engine;
            for (auto& b : app_.broadcast_boards_) b.analyser.reset();  // started again with it
            app_.saveLichessSettings();
        }
    }
    GameStreams gameStreams() const override {
        GameStreams streams;
        streams.rounds  = app_.followed_rounds_.size();
        streams.players = app_.players_follower_ ? app_.players_follower_->open() : 0;
        streams.tv      = app_.tv_->streams();
        streams.tour    = app_.arenas_->streams();
        return streams;
    }
    void setPlayersQuota(std::size_t streams) override { app_.players_quota_ = streams; }

    // Lichess's settings.
    LichessSettings& lichessSettings() override { return app_.lichess_; }
    void saveLichessSettings() override { app_.saveLichessSettings(); }
    std::vector<std::string> ownLichessIds() override { return app_.ownLichessIds(); }

    // The Lichess account.
    const lichess::Account* lichessAccount() const override {
        return app_.lichess_account_ ? &*app_.lichess_account_ : nullptr;
    }
    bool lichessLinking() const override { return app_.lichess_link_ != nullptr; }
    const lichess::Failure& lichessLinkError() const override { return app_.lichess_error_; }
    void startLichessLink(std::vector<std::string> scopes, std::string username) override {
        app_.startLichessLink(std::move(scopes), std::move(username));
    }
    std::string scopeAsked() const override {
        return app_.lichess_scope_ask_ ? app_.lichess_scope_ask_->scope : std::string();
    }
    void askScope(const std::string& scope) override {
        LichessScopeAsk ask;
        ask.scope               = scope;
        app_.lichess_scope_ask_ = ask;
    }
    void dropScopeAsk() override { app_.lichess_scope_ask_.reset(); }
    const lichess::Profile* lichessProfile() const override {
        return app_.lichess_profile_ ? &*app_.lichess_profile_ : nullptr;
    }
    void askLichessProfile() override { app_.askArenaProfile(); }
    void askLichessProfileOnce() override {
        // As the Home page reads it: kept a day, asked once when none is known (the puzzles' rating, #80).
        if (!app_.lichess_account_) return;
        app_.pollLichessProfile();
        if (!app_.lichess_profile_ && !app_.lichess_profile_job_.valid() &&
            app_.lichess_profile_asked_.time_since_epoch().count() == 0) {
            app_.lichess_profile_asked_ = std::chrono::steady_clock::now();
            app_.lichess_profile_job_   = std::async(
                std::launch::async, [token = app_.lichess_account_->token] { return lichess::accountProfile(token); });
        }
    }

    // The user's games on Lichess.
    const std::vector<lichess::PlayingGame>& playingGames() const override { return app_.lichess_playing_; }
    std::optional<context::OnlineGame> onlineGame() const override {
        if (!app_.online_game_) return std::nullopt;
        const auto& g = *app_.online_game_;
        return context::OnlineGame{g.id, g.white, g.shown, g.ended, g.state};
    }
    void backToOnlineGame() override { app_.backToOnlineGame(); }
    std::string onlineOpponent() const override { return app_.onlineOpponent(); }
    void onlineCall(std::string what, std::function<lichess::Reply(const std::string& token)> call) override {
        app_.onlineCall(std::move(what), std::move(call));
    }
    lichess::OnlinePlaySettings& onlineSettings() override { return app_.onlineSettings(); }
    void saveOnlineSettings() override { app_.saveOnlineSettings(); }
    std::size_t heldGames(const std::string& tournament) const override { return held(tournament).size(); }
    bool lastHeldArrived(const std::string& tournament) const override {
        const auto games = held(tournament);
        return !games.empty() && games.back()->got && !games.back()->got->pgn.empty();
    }
    void reviewLastHeld(const std::string& tournament) override {
        const auto games = held(tournament);
        if (games.empty()) return;
        auto* last = games.back();
        for (auto* f : games) {
            f->held = false;
            if (f != last) f->done = true;
        }
        app_.reviewFinishedGame(*last);
    }
    void letGoHeld(const std::string& tournament) override {
        for (auto* f : held(tournament)) {
            f->held = false;
            f->done = true;
        }
    }
    void closeAnyway() override {
        app_.quit_online_said_ = true;
        app_.requestQuit();
    }

    // Fair play.
    bool liveGameOn() const override { return app_.liveGameOn() != nullptr; }

    // The engines.
    const std::vector<EngineEntry>& engines() const override { return app_.engines_; }
    const std::vector<BookEntry>& books() const override { return app_.books_; }
    std::string tournamentTables() const override { return app_.setup_.tb_paths; }
    bool tournamentPlaysHere() const override {
        return std::any_of(app_.runs_.begin(), app_.runs_.end(), [](const auto& r) {
            return r.run->remote() && r.run->running() && r.machine_id.empty() && r.run->tournament();
        });
    }
    int freeCores() const override { return app_.freeCores(); }
    const EngineEntry* engineShown() const override {
        const int i = app_.selected_engine_;
        if (app_.page_ != Page::Engines || i < 0 || i >= static_cast<int>(app_.engines_.size())) return nullptr;
        return &app_.engines_[static_cast<std::size_t>(i)];
    }
    void showEngineCard(std::optional<int> engine, bool alone) override {
        app_.page_ = Page::Engines;
        if (engine) app_.selected_engine_ = *engine;
        if (alone) app_.engine_marked_.clear();
    }

    // The Board taken by a game followed live.
    void takeBoard(const std::string& title) override {
        auto& b = app_.board_;
        app_.rememberBoardGame();
        app_.board_origin_.reset();
        b.undo.clear();
        b.redo.clear();
        b.start_notes = {};
        b.comment_edit.reset();
        b.changes  = {};
        b.watching = false;
        b.selected.reset();
        b.move_typed.reset();
        b.preview.reset();
        b.pgn.reset();
        if (b.analysing) app_.setBoardAnalysis(false);
        b.game           = ReplayGame{};
        b.game.start_fen = std::string(chess::constants::STARTPOS);
        b.tree           = GameTree{b.game, {}};
        b.line           = -1;
        b.ply            = INT32_MAX;
        b.chess960       = false;
        b.title          = title;
        b.message.clear();
        if (app_.online_game_) app_.online_game_->shown = false;
        ++app_.board_game_serial_;
    }
    void updateBoard(ReplayGame game, bool chess960, const std::string& title, std::optional<bool> flip) override {
        auto& b = app_.board_;
        // The board follows the game, unless the user went back in it.
        const bool at_end = b.line >= 0 || b.ply >= static_cast<int>(b.game.moves.size());
        b.game            = std::move(game);
        b.tree            = GameTree{b.game, {}};
        b.line            = -1;
        b.chess960        = chess960;
        if (at_end) b.ply = static_cast<int>(b.game.moves.size());
        b.ply = std::clamp(b.ply, 0, static_cast<int>(b.game.moves.size()));
        if (!title.empty()) b.title = title;
        if (flip) b.flip = *flip;
    }
    void showLiveBoard() override {
        if (!app_.views_.empty() && !app_.view().holds(views::PageId::Board))
            app_.toStudyBoard();
        else
            app_.page_ = Page::Board;
        app_.board_back_.reset();
    }
    std::string boardSan(std::string san) const override { return app_.boardSan(std::move(san)); }
    const PieceAtlas& pieces() const override { return app_.atlas_; }

    // The tokens given up.
    void revokeToken(std::string token) override { app_.revokeToken(std::move(token)); }

    // The window.
    void raiseWindow() override {
        if (app_.window_) SDL_RaiseWindow(app_.window_);
    }

    // The Board taken by a training session.
    std::string sessionLock() const override { return app_.trainingLock(); }
    BoardTaken takeBoard(const std::string& title, bool tree) override {
        const bool chained = app_.review_running_;  // the day's review goes on (#80)
        app_.closeBoardSession();
        app_.review_running_ = chained;
        auto& b              = app_.board_;
        BoardTaken taken;
        taken.flip = b.flip;
        taken.tab  = app_.board_side_tab_;
        // The game on the board goes among the recent games (Ctrl+Tab), given back after the session.
        taken.aside = !b.game.moves.empty() || !b.tree.variations.empty() || !b.title.empty();
        app_.toStudyBoard();
        app_.openOnBoard(ReplayGame{}, 0, false, title, false);
        app_.board_back_.reset();
        if (tree && app_.board_side_tab_ == 0) app_.board_side_tab_ = 2;
        return taken;
    }
    void giveBoardBack(const BoardTaken& taken, bool to_training) override {
        app_.board_side_tab_    = taken.tab;
        app_.forget_board_game_ = true;  // the session's position is no game to remember
        if (taken.aside && !app_.recent_games_.empty())
            app_.restoreRecentGame(0);
        else
            app_.newBoardGame();
        app_.forget_board_game_ = false;
        app_.board_.flip        = taken.flip;
        app_.board_back_.reset();
        app_.page_ = to_training ? Page::Training : Page::Board;
    }
    void leaveBoard(const BoardTaken& taken) override {
        app_.board_.flip        = taken.flip;
        app_.board_side_tab_    = taken.tab;
        app_.forget_board_game_ = true;  // the game that comes takes the board; the one put aside stays a recent one
    }
    void showSession() override { app_.page_ = Page::Board; }
    const ReplayGame& sessionGame() const override { return app_.board_.game; }
    int sessionPly() const override { return app_.board_.ply; }
    void showSessionGame(ReplayGame game) override {
        auto& b    = app_.board_;
        b.game     = std::move(game);
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
    void playSessionMove(chess::Move move) override {
        auto& b = app_.board_;
        b.ply   = playOnGame(b.game, b.chess960, b.ply, move);
        b.tree  = GameTree{b.game, {}};
    }
    void setFlip(bool flip) override { app_.board_.flip = flip; }
    void dropPick() override { app_.board_.selected.reset(); }
    void dropTypedMove() override { app_.board_.move_typed.reset(); }
    void beginSessionPage() override {
        ui::pageTitle(TR("Board"), app_.guideSection());
        // The letters typed reach the move field whatever the keyboard's layout (see drawBoardPage).
        if (!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
            !SDL_TextInputActive(app_.window_))
            SDL_StartTextInput(app_.window_);
    }
    void drawBoardMessage() override {
        const auto& b = app_.board_;
        if (b.message.empty()) return;
        ImGui::PushStyleColor(ImGuiCol_Text, b.message_error ? ui::BAD : ui::MUTED);
        ImGui::TextWrapped("%s", b.message.c_str());
        ImGui::PopStyleColor();
    }
    void drawSessionBoard(float size, const BoardDecor& marks, bool moves) override {
        auto& b          = app_.board_;
        const auto board = boardAt(b.game, b.chess960, b.ply);
        BoardDecor decor = marks;
        decor.flipped    = b.flip;
        decor.eval_white = -1.0f;  // no bar: no engine's word on the position
        if (b.ply > 0) decor.last_move = b.game.moves[static_cast<std::size_t>(b.ply - 1)].move;
        decor.selected = moves ? b.selected : std::nullopt;
        if (decor.selected)
            for (const auto& m : movesFrom(board, *decor.selected)) decor.targets.push_back(shownTarget(m));
        const bool dragging = moves && b.drag && b.drag->moved && b.selected == b.drag->from;
        if (dragging) {
            decor.lifted = b.drag->from;
            if (b.drag_over &&
                std::find(decor.targets.begin(), decor.targets.end(), *b.drag_over) != decor.targets.end())
                decor.drop = b.drag_over;
        }
        const ImVec2 top = ImGui::GetCursorScreenPos();
        if (appearance::current().board3d.on && app_.board3d_.available()) {
            app_.board3d_.setMarks(Board3d::Marks::Off);
            const auto input = app_.board3d_.draw(board, size, decor);
            if (moves) app_.boardPointer(board, input.clicked, input.hovered, input.right_click);
        } else {
            if (!appearance::current().board3d.on) app_.board3d_.release();
            drawBoard(app_.atlas_, board, size, decor);
            const auto hover = boardSquareAt(ImGui::GetMousePos(), top, size, decor);
            if (moves) {
                const bool left  = ImGui::IsItemClicked(ImGuiMouseButton_Left),
                           right = ImGui::IsItemClicked(ImGuiMouseButton_Right);
                app_.boardPointer(board, left ? hover : std::nullopt, hover, right);
                if (dragging && b.drag) drawDraggedPiece(app_.atlas_, board.at(b.drag->from), size);
            }
        }
        app_.drawPromotionPopup(board);
    }
    void drawMoveField() override { app_.drawMoveField(); }
    void moveKeys() override { app_.boardMoveKeys(); }
    void drawBoardTabs() override { app_.drawChessTabs(); }
    void drawFairPlayBanner() override { app_.drawFairPlayBanner(); }
    void analyse(bool on) override { app_.setBoardAnalysis(on); }
    bool analysisKey() override {
        if (!app_.boardKey()) return false;
        app_.setBoardAnalysis(!app_.board_.analysing);
        return true;
    }

    // The Training page and its day's review.
    const TrainingSettings& trainingSettings() override { return app_.trainingSettings(); }
    void showTrainingPage(int part) override {
        app_.page_         = Page::Training;
        app_.training_tab_ = part;
    }
    void setEnterStartsPuzzles(bool puzzles) override { app_.training_enter_puzzles_ = puzzles; }
    bool reviewRunning() const override { return app_.review_running_; }
    bool nextReview() override { return app_.nextReview(); }
    void stopReview() override {
        app_.review_running_ = false;
        app_.review_chain_.clear();
    }
    void drawReviewNext(bool enter) override { app_.drawReviewNext(enter); }

    // The user's mistakes.
    std::map<std::string, std::size_t> mistakeKinds(uint32_t since) override {
        std::map<std::string, std::size_t> kinds;
        for (const auto* e : app_.mistakeExercises())
            if (e->packed_date >= since) ++kinds[e->kind];
        return kinds;
    }

    // The file dialogs.
    void choosePuzzleFile() override {
        app_.dialogs_.openFile(Dialogs::Purpose::OfflinePuzzlesFile, {{"zstd", "zst"}, {TR("All files"), "*"}});
    }

   private:
    // The user's games of a tournament kept with their review held until its end (#95), in the order they ended.
    std::vector<FinishedGame*> held(const std::string& tournament) const {
        std::vector<FinishedGame*> out;
        for (auto& f : app_.lichess_finished_)
            if (f.held && f.game.tournament == tournament) out.push_back(&f);
        return out;
    }

    App& app_;
};

// A kind of training session still in App (#76, #80–#82): its session answers the Board's hooks with App's functions,
// until the kind moves into a module of its own.
class App::OwnSession final : public BoardSession {
   public:
    enum class Kind { Training, Puzzles, Mistakes, Guess, Endgame, Vision };
    OwnSession(App& app, Kind kind) : app_(app), kind_(kind) {}

    void sessionMove(chess::Move move) override {
        switch (kind_) {
            case Kind::Training:
                return app_.trainingAnswer(move);
            case Kind::Puzzles:
                return app_.puzzleAnswer(move);
            case Kind::Mistakes:
                return app_.mistakeAnswer(move);
            case Kind::Guess:
                return app_.guessAnswer(move);
            case Kind::Endgame:
                return app_.endgameAnswer(move);
            case Kind::Vision:
                return app_.visionAnswer(move);
        }
    }
    void drawSessionPage() override {
        switch (kind_) {
            case Kind::Training:
                return app_.drawTrainingPage();
            case Kind::Puzzles:
                return app_.drawPuzzlePage();
            case Kind::Mistakes:
                return app_.drawMistakePage();
            case Kind::Guess:
                return app_.drawGuessPage();
            case Kind::Endgame:
                return app_.drawEndgamePage();
            case Kind::Vision:
                return app_.drawVisionPage();
        }
    }
    void sessionDropped() override {
        switch (kind_) {
            case Kind::Training:
                return app_.dropTraining();
            case Kind::Puzzles:
                return app_.dropPuzzles();
            case Kind::Mistakes:
                return app_.dropMistakes();
            case Kind::Guess:
                return app_.dropGuess();
            case Kind::Endgame:
                return app_.dropEndgame();
            case Kind::Vision:
                return app_.dropVision();
        }
    }
    void closeSession(bool back) override {
        switch (kind_) {
            case Kind::Training:
                return app_.closeTraining(back);  // back to the Openings page, or the Training page in the review
            case Kind::Puzzles:
                return app_.closePuzzles(back);
            case Kind::Mistakes:
                return app_.closeMistakes(back);
            case Kind::Guess:
                return app_.closeGuess(back);
            case Kind::Endgame:
                return app_.closeEndgame(back);
            case Kind::Vision:
                return app_.closeVision(back);
        }
    }
    bool hidesAids() const override {
        switch (kind_) {
            case Kind::Training:  // a question, or its cards being prepared
                return app_.training_->phase == TrainingRun::Phase::Asking ||
                       app_.training_->phase == TrainingRun::Phase::Preparing;
            case Kind::Puzzles:  // a puzzle's question, its opponent's reply coming
                return app_.puzzle_->phase == PuzzleRun::Phase::Asking ||
                       app_.puzzle_->phase == PuzzleRun::Phase::Reply;
            case Kind::Mistakes:  // an exercise's question, its answer being judged
                return app_.mistake_->phase == MistakeRun::Phase::Asking ||
                       app_.mistake_->phase == MistakeRun::Phase::Checking;
            case Kind::Guess:  // a guess, until the game's move is shown
                return app_.guess_->phase != GuessRun::Phase::Shown && app_.guess_->phase != GuessRun::Phase::End;
            case Kind::Endgame:  // an endgame being played against the tables
                return app_.endgame_->phase != EndgameRun::Phase::Done &&
                       app_.endgame_->phase != EndgameRun::Phase::End;
            case Kind::Vision:
                return app_.vision_->phase != VisionRun::Phase::End;
        }
        return false;
    }
    bool keptAside() const override {
        switch (kind_) {
            case Kind::Training:
                return app_.training_->aside;
            case Kind::Puzzles:
                return app_.puzzle_->aside;
            case Kind::Mistakes:
                return app_.mistake_->aside;
            case Kind::Guess:
                return app_.guess_->aside;
            case Kind::Endgame:
                return app_.endgame_->aside;
            case Kind::Vision:
                return app_.vision_->aside;
        }
        return false;
    }
    const char* guideTopic() const override {
        switch (kind_) {
            case Kind::Training:
                return guide::topics::Training;
            case Kind::Puzzles:
                return guide::topics::Puzzles;
            case Kind::Mistakes:
                return guide::topics::Mistakes;
            case Kind::Guess:
                return guide::topics::GuessMoves;
            case Kind::Endgame:
                return guide::topics::Endgames;
            case Kind::Vision:
                return guide::topics::Vision;
        }
        return guide::topics::Board;
    }

   private:
    App& app_;
    Kind kind_;
};

void App::initChessModules() {
    modules_context_  = std::make_unique<ModulesContext>(*this);
    tv_               = std::make_unique<LichessTvPage>(*modules_context_);
    arenas_           = std::make_unique<LichessArenasPage>(*modules_context_);
    bot_              = std::make_unique<LichessBotPage>(*modules_context_);
    training_session_ = std::make_unique<OwnSession>(*this, OwnSession::Kind::Training);
    puzzle_session_   = std::make_unique<OwnSession>(*this, OwnSession::Kind::Puzzles);
    mistake_session_  = std::make_unique<OwnSession>(*this, OwnSession::Kind::Mistakes);
    guess_session_    = std::make_unique<OwnSession>(*this, OwnSession::Kind::Guess);
    endgame_session_  = std::make_unique<OwnSession>(*this, OwnSession::Kind::Endgame);
    vision_session_   = std::make_unique<OwnSession>(*this, OwnSession::Kind::Vision);
}

// The training sessions on the Board page (board_session.hpp, #107): one at a time holds the Board, asked in the order
// the Board's hooks asked the kinds before.
BoardSession* App::boardSession() const {
    if (training_) return training_session_.get();
    if (puzzle_) return puzzle_session_.get();
    if (mistake_) return mistake_session_.get();
    if (guess_) return guess_session_.get();
    if (endgame_) return endgame_session_.get();
    if (vision_) return vision_session_.get();
    return nullptr;
}

bool App::drawBoardSession() {
    auto* session = boardSession();
    // A guided visit (#94) started during an endgame or a vision exercise shows over their page, as it always did.
    if (!session || (visit_ && (endgame_ || vision_))) return false;
    session->drawSessionPage();
    return true;
}

bool App::boardSessionMove(chess::Move move) {
    auto* session = boardSession();
    if (!session) return false;
    session->sessionMove(move);
    return true;
}

void App::leaveBoardSession() {
    if (auto* session = boardSession()) session->sessionDropped();
}

void App::closeBoardSession() {
    if (auto* session = boardSession()) session->closeSession(false);
}

const char* App::sessionGuideTopic() const {
    const auto* session = boardSession();
    return session ? session->guideTopic() : nullptr;
}

// The arenas' hooks in fastchess-gui's pages (#95), which do not include its module's header.
bool App::arenaHoldsAids() const { return arenas_->holdsAids(); }
void App::drawSwissNotice() { arenas_->drawSwissNotice(); }

// Lichess TV's hooks in fastchess-gui's pages (#86), which do not include its module's header.
bool App::tvOnBoard() const { return tv_->onBoard(); }
void App::leaveTvBoard() { tv_->leaveBoard(); }
void App::drawTvBoardCard() { tv_->drawBoardCard(); }
void App::drawTvEngineLine(const EngineEntry& e) { tv_->drawEngineLine(e.name); }
std::string App::tvName(const std::string& name) { return fcg::tvName(name); }

// The Lichess bot's hooks in fastchess-gui's pages (#85), which do not include its module's header.
void App::drawBotPart(const EngineEntry& e) { bot_->drawEnginePart(e, lichess_engine_enter_); }
void App::drawBotRunning(bool home) { bot_->drawRunning(home); }
int App::botThreads() const { return bot_->threads(); }
bool App::botOnBoard() const { return bot_->onBoard(); }
bool App::botShown() const { return bot_->shown(); }
void App::drawBotGameCard() { bot_->drawBoardCard(); }
const lichess::BoardState& App::botGame() const { return bot_->game(); }
std::optional<std::string> App::botStripClock(chess::Color side) const { return bot_->stripClock(side); }
void App::leaveBotBoard() { bot_->leaveBoard(); }
bool App::botLive() const { return bot_->live(); }
void App::drawQuitBot() {
    // Close anyway: RookStudio closes, the bot's question answered (requestQuit() asks the others first).
    if (bot_->drawQuitQuestion(std::exchange(quit_bot_open_, false))) {
        quit_bot_said_ = true;
        requestQuit();
    }
}

void App::initChessServers() {
    // Scripted screenshots ask nothing of the internet (AppOptions::scripted): Lichess, its database, TWIC, chess.com
    // and FIDE pointed at a port of this machine where nothing listens, before anything asks them; a focus word puts a
    // test's server in place of one (lichess-api=, lichess-database=, twic-server=, chesscom-server=, fide-server=).
    if (options_.scripted) {
        const std::string nowhere = "http://127.0.0.1:9";
        lichess::servers() = lichess::Servers{nowhere, nowhere + "/tb", nowhere + "/explorer", nowhere + "/engine"};
        lichess::setDatabaseServer(nowhere);
        twic::setServer(nowhere);
        chesscom::setServer(nowhere);
        fide::setServer(nowhere + "/players_list.zip");
    }
    // A test's Lichess (--focus lichess-api=http://127.0.0.1:PORT), in place before anything asks lichess.org: the
    // games in progress are asked at the first frame, before the focus words are read.
    if (const auto& w = options_.focus_window; w.rfind("lichess-api=http://127.0.0.1:", 0) == 0) {
        const auto comma   = w.find(',');
        const auto base    = w.substr(12, comma == std::string::npos ? std::string::npos : comma - 12);
        lichess::servers() = lichess::Servers{base, base + "/tb", base + "/explorer", base + "/engine"};
    }
}

void App::pollChess() {
    pollLichess();             // a link, a sync, the broadcasts: whatever page shows
    pollLinkJob();             // a Lichess link pasted: its game asked, its import
    pollLichessPlaying();      // the user's games in progress on Lichess, and what they lock (#48)
    pollFollowedPlayers();     // the players followed live, their games in the mosaic (#50)
    pollCast();                // a tournament on the user's own Lichess broadcast, once allowed (#51)
    pollPrep();                // a game prepared against an opponent: their games (#52)
    pollDossier();             // a player's dossier: the job that ended, their Lichess games that came (#90)
    pollOpeningReport();       // an opening report: its sections, evaluations, summary, Lichess's answers (#91)
    pollExport();              // the Export sheet: a file written in the background, its weight estimated (#93)
    pollEvalsReading();        // Lichess's evaluations database read for a book (#53)
    pollOfflineEvals();        // Lichess's evaluations kept offline: a preparation's runs, its end (#58)
    pollToLichess();           // a game shared, a position played out, a game bookmarked on Lichess (#54)
    pollStudies();             // the user's Lichess studies: read, imported, followed, a game sent (#79)
    pollTheory();              // the theory studies: read, read again when their files change (#77)
    pollTraining();            // a repertoire's training: its cards prepared, its first question (#76)
    pollPuzzles();             // Lichess's puzzles: a preparation's end, the batch online's requests (#80)
    pollMistakes();            // the user's mistakes: looked for, their games reviewed, a session's engine (#81)
    pollEndgames();            // the endgames: the tables' answers and moves, the tables' download, vision's game (#82)
    pollEndgameCheck();        // a finished tournament's endgames against the tables (#94)
    pollEngineGame();          // a game against an engine: kept at each move, its end, a hint (#83)
    pollOnlinePlay();          // playing on Lichess: a seek, the challenges, the game on the Board (#84)
    bot_->poll();              // a Lichess bot for an engine: its account, its challenges, its game (#85)
    pollLichessEngines();      // the engines offered to lichess.org: their registrations, guards, requests (#96)
    pollBroadcastStandings();  // a broadcast's standings, a search among the broadcasts (#55)
    pollFideCards();           // a FIDE card asked for (#57)
    pollEnginePicker();
    pollEngineVersions();  // an engine's old versions deleted once the tournaments that play them are over (#65)
    pollQuestions();       // the answers, the assistant's download, its model unloaded when unused
}

bool App::applyChessFocusWord(const std::string& w) {
    // Tried in this order, before fastchess-gui's words: the first entry whose pattern matches the word is applied
    // (focus_words.hpp).
    FocusWords words(w);
    words.on({"chess-home"}, [&] { page_ = Page::ChessHome; });
    words.on({"databases"}, [&] { page_ = Page::Databases; });
    words.on({"databases-list", "databases-list=*"}, [&] {
        // The first database's games; databases-list=WORDS: through that quick filter.
        page_        = Page::Databases;
        db_selected_ = 0;
        db_list_     = true;
        if (w.size() > 15) std::snprintf(db_filter_.data(), db_filter_.size(), "%s", w.substr(15).c_str());
    });
    words.on({"databases-search", "databases-search-position"}, [&] {
        // The first database's games with the search panel; its position searched.
        page_               = Page::Databases;
        db_selected_        = 0;
        db_list_            = true;
        db_search_open_     = true;
        db_search_.position = w == "databases-search-position";
        std::snprintf(db_search_.year_from.data(), db_search_.year_from.size(), "2020");
    });
    words.on({"databases-search-pattern*", "databases-search-text*"}, [&] {
        // databases-search-pattern[=PRESET][,WORD], databases-search-text[=PRESET][,WORD]: the first database's
        // search panel on its Pattern tab (#89), a preset set (its key: knight-fork, h7-sacrifice…), its text
        // shown; then WORD, another of these (databases-import-into=FILE: an import going on).
        const auto comma = w.find(',');
        if (comma != std::string::npos) focus_next_ = w.substr(comma + 1);
        const auto word = w.substr(0, comma);
        page_           = Page::Databases;
        db_selected_    = 0;
        if (const auto eq = word.find('='); eq != std::string::npos) {
            const auto key = word.substr(eq + 1);
            for (std::size_t i = 0; i < study::patternPresets().size(); ++i)
                if (study::patternPresets()[i].key == key) applyPatternPreset(i);
        }
        openSearchOn(TabPattern);
        db_selected_       = 0;
        pattern_text_open_ = word.rfind("databases-search-text", 0) == 0;
    });
    words.on({"databases-search-route*"}, [&] {
        // databases-search-route[=b1,d2,f1,g3]: the Manoeuvre tab, the man of the first square on the Board page's
        // position going along that route.
        page_        = Page::Databases;
        db_selected_ = 0;
        openSearchOn(TabRoute);
        db_selected_ = 0;
        if (const auto eq = w.find('='); eq != std::string::npos) {
            auto& r = db_search_.route;
            r.squares.clear();
            for (std::size_t at = eq + 1; at + 1 < w.size() + 1;) {
                const auto comma = w.find(',', at);
                const auto item  = w.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
                if (item.size() == 2 && item[0] >= 'a' && item[0] <= 'h' && item[1] >= '1' && item[1] <= '8')
                    r.squares.push_back(static_cast<uint8_t>((item[1] - '1') * 8 + (item[0] - 'a')));
                if (comma == std::string::npos) break;
                at = comma + 1;
            }
            const auto board = boardAt(board_.game, board_.chess960, board_.ply);
            if (!r.squares.empty()) {
                const auto piece = board.at(chess::Square(r.squares[0]));
                if (piece != chess::Piece::NONE) {
                    r.side = piece.color() == chess::Color::WHITE ? 0 : 1;
                    r.kind = static_cast<int>(piece.type());
                }
            }
        }
    });
    words.on({"databases-search-structure"}, [&] {
        // The Structure tab: the Board page's pawns, searched.
        page_        = Page::Databases;
        db_selected_ = 0;
        openSearchOn(TabStructure);
        db_selected_ = 0;
    });
    words.on({"databases-care", "databases-care=*"}, [&] {
        // databases-care[=names|checks|check|pair]: the first database's maintenance (#88), at that part; check:
        // its check started; pair: its first group of duplicates compared.
        const auto part = w.size() > 15 ? w.substr(15) : std::string("duplicates");
        openCare(0, part == "names"                       ? CarePart::Names
                    : part == "checks" || part == "check" ? CarePart::Checks
                                                          : CarePart::Duplicates);
        if (care_) {
            care_->start_check     = part == "check";
            care_->open_first_pair = part == "pair";
        }
    });
    words.on({"dossier=*", "dossier-lichess=*"}, [&] {
        // dossier=NAME[@TAB]: the dossier of NAME (as the databases write it) over the Databases page (#90), at
        // its tab (overview, openings, rating, lichess); dossier-lichess=ACCOUNT[@TAB]: theirs from the Lichess
        // page's "Prepare a game against", its Lichess source ticked (with lichess-api= before it).
        const bool online = w[7] == '-';
        auto spec         = w.substr(w.find('=') + 1);
        std::string tab;
        if (const auto at = spec.rfind('@'); at != std::string::npos) {
            tab  = spec.substr(at + 1);
            spec = spec.substr(0, at);
        }
        page_ = online ? Page::Lichess : Page::Databases;
        study::DossierSubject subject;
        subject.name = spec;
        if (online) subject.lichess = spec;
        openDossier(std::move(subject), online);
        if (dossier_) dossier_->tab = tab == "openings" ? 1 : tab == "rating" ? 2 : tab == "lichess" ? 3 : 0;
    });
    words.on({"tournaments", "tournaments=*", "crosstable=*"}, [&] {
        // tournaments[=DATABASE]: the tournaments of the first database, or of the one of that name (#92);
        // crosstable=WORDS[@DATABASE]: the crosstable of its first tournament whose name holds WORDS, once listed.
        const bool table = w[0] == 'c';
        auto spec        = w.find('=') == std::string::npos ? std::string() : w.substr(w.find('=') + 1);
        std::string base = table ? std::string() : spec;
        if (table) {
            if (const auto at = spec.rfind('@'); at != std::string::npos) {
                base = spec.substr(at + 1);
                spec = spec.substr(0, at);
            }
            crosstable_request_ = std::make_pair(spec, base);
        }
        page_           = Page::Databases;
        db_selected_    = 0;
        db_list_        = true;
        db_tournaments_ = true;
        auto& s         = shelf();
        for (std::size_t i = 0; i < s.entries().size(); ++i)
            if (!base.empty() && s.entries()[i].name == base) db_selected_ = static_cast<int>(i);
    });
    words.on({"broadcast-follow=*"}, [&] {
        // broadcast-follow=ROUND@TOUR@NAME[@FORMAT][,WORD]: a broadcast's round followed (lichess-api= before it),
        // its mosaic shown (#92); then WORD, another of these (broadcast-crosstable).
        auto spec = w.substr(17);
        if (const auto comma = spec.find(','); comma != std::string::npos) {
            focus_next_ = spec.substr(comma + 1);
            spec        = spec.substr(0, comma);
        }
        std::vector<std::string> parts;
        for (std::size_t at = 0;;) {
            const auto end = spec.find('@', at);
            parts.push_back(spec.substr(at, end == std::string::npos ? std::string::npos : end - at));
            if (end == std::string::npos) break;
            at = end + 1;
        }
        lichess::BroadcastEntry entry;
        entry.round.id   = parts[0];
        entry.round.name = "Round";
        if (parts.size() > 1) entry.tour.id = parts[1];
        if (parts.size() > 2) entry.tour.name = parts[2];
        if (parts.size() > 3) entry.tour.format = parts[3];
        page_ = Page::Lichess;
        followBroadcast(entry, true);
    });
    words.on({"broadcast-crosstable"}, [&] {  // the followed round's crosstable in place of its mosaic (#92)
        broadcast_crosstable_ = true;
    });
    words.on({"fide-server=http://127.0.0.1:*"}, [&] {
        // fide-server=http://127.0.0.1:PORT[,WORD]: a test's server in place of ratings.fide.com (#88), on this
        // machine only; then WORD, another of these.
        const auto comma = w.find(',');
        const auto from  = std::string_view("fide-server=").size();
        fide::setServer(w.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
        if (comma != std::string::npos) focus_next_ = w.substr(comma + 1);
    });
    words.on({"databases-import-into=*"}, [&] {  // databases-import-into=FILE: into the first database
        page_        = Page::Databases;
        db_selected_ = 0;
        importIntoDatabase(0, std::filesystem::path(w.substr(22)));
    });
    words.on({"databases-import=*"}, [&] {  // databases-import=FILE: into a new database named after it
        page_ = Page::Databases;
        importIntoDatabase(-1, std::filesystem::path(w.substr(17)));
    });
    words.on({"openings", "openings-theory"}, [&] {  // the Openings page; its Theory part (#77)
        page_            = Page::Openings;
        openings_theory_ = w == "openings-theory";
    });
    words.on({"theory-add=*"}, [&] {
        // theory-add=FILE[,WORD]: FILE added to the theory (the profile left as it was in a run for captures);
        // then WORD, another of these.
        const auto comma = w.find(',');
        addTheoryFile(std::filesystem::path(w.substr(11, comma == std::string::npos ? std::string::npos : comma - 11)));
        if (comma != std::string::npos) focus_next_ = w.substr(comma + 1);
    });
    words.on({"training", "training-new"}, [&] {  // a session of the active repertoire's training (#76)
        if (repertoires().active() >= 0)
            startTraining(static_cast<std::size_t>(repertoires().active()),
                          w == "training" ? TrainingRun::Kind::Due : TrainingRun::Kind::New);
    });
    words.on({"training-page", "training-puzzles", "puzzles-online"}, [&] {
        // The Training page (#80): its Today part, its Puzzles part, the puzzles online chosen.
        page_           = Page::Training;
        training_tab_   = w == "training-page" ? 0 : 1;
        puzzles_online_ = w == "puzzles-online";
    });
    words.on({"puzzles-file=*"}, [&] {  // puzzles-file=FILE: Lichess's puzzle database read from it
        page_         = Page::Training;
        training_tab_ = 1;
        startOfflinePuzzles(w.substr(13));
    });
    words.on({"puzzles-series", "puzzles-series=*"}, [&] {
        // puzzles-series[=THEME]: a series of the offline puzzles at once, of THEME (a Lichess key) or any.
        puzzles_online_ = false;
        puzzles_theme_  = w.size() > 15 ? w.substr(15) : std::string();
        startPuzzles();
    });
    words.on({"review-all"}, [&] {  // the day's review, every kind one after the other (#80)
        startReview();
    });
    words.on({"training-mistakes", "training-guess"}, [&] {
        // The Training page's parts of #81: the mistakes as exercises, guessing the moves.
        page_         = Page::Training;
        training_tab_ = w == "training-mistakes" ? 2 : 3;
    });
    words.on({"mistakes-session", "mistakes-review"}, [&] {
        // A session of the exercises of the user's mistakes: today's, or the day's review of them alone (#81).
        if (!startMistakes(w == "mistakes-review")) {
            page_         = Page::Training;
            training_tab_ = 2;
        }
    });
    words.on({"training-endgames", "training-vision"}, [&] {
        // The Training page's parts of #82: the endgames against the tables and the mates, board vision.
        page_         = Page::Training;
        training_tab_ = w == "training-endgames" ? 4 : 5;
    });
    words.on({"endgame=*"}, [&] {  // endgame=ID: that endgame of the catalogue against the tables
        if (const auto* e = endgameEntry(w.substr(8))) {
            if (!endgameNeed(e->fen).empty() || !startEndgame(*e)) {
                page_         = Page::Training;
                training_tab_ = 4;
                endgame_need_ = e->id;
            }
        }
    });
    words.on({"endgames-review"}, [&] {  // the endgames missed, due today (#82)
        if (!startEndgameReview()) {
            page_         = Page::Training;
            training_tab_ = 4;
        }
    });
    words.on({"vision-coordinates", "vision-blindfold", "vision-lines"}, [&] {
        if (w == "vision-coordinates")
            startCoordinates();
        else if (w == "vision-blindfold")
            startBlindfold();
        else
            startHeadLines();
    });
    words.on({"guess-board", "guess-random"}, [&] {
        // Guessing the moves of the Board page's game from the position shown, or of a random game (#81).
        if (w == "guess-board")
            startGuessBoard();
        else
            (void)startGuessRandom();
    });
    words.on({"board-analysis*"}, [&] {
        // The study Board's Analysis tab (#78); board-analysis=FILE[@PLY]: FILE on the board (at that ply once
        // read), analysed; board-analysis-menu[=FILE[@PLY]]: its "+ An engine" open too.
        toStudyBoard();
        board_side_tab_ = 1;
        if (const auto eq = w.find('='); eq != std::string::npos) {
            auto file = w.substr(eq + 1);
            if (const auto at = file.rfind('@'); at != std::string::npos && at > 1) {
                ply_when_read_ = std::atoi(file.c_str() + at + 1);
                file           = file.substr(0, at);
            }
            dropFile(file);
            setBoardAnalysis(true);
        }
        if (w.rfind("board-analysis-menu", 0) == 0) side_menu_open_ = true;
    });
    words.on({"board-tree", "board-references", "board-tree=*", "board-notation=*"}, [&] {
        // The Chess space's column at that tab; board-tree=FILE[@PLY], board-notation=FILE[@PLY]: FILE on the
        // board (at that ply once read).
        toStudyBoard();
        board_side_tab_ = w.rfind("board-tree", 0) == 0 ? 2 : w.rfind("board-notation", 0) == 0 ? 0 : 3;
        if (const auto eq = w.find('='); eq != std::string::npos) {
            auto file = w.substr(eq + 1);
            if (const auto at = file.rfind('@'); at != std::string::npos && at > 1) {
                ply_when_read_ = std::atoi(file.c_str() + at + 1);
                file           = file.substr(0, at);
            }
            dropFile(file);
        }
    });
    words.on({"export*=*"}, [&] {
        // The Export sheet (#93) on a format (html, diagram, gif, pdf, pgn): export=FILE[@PLY]@FORMAT, FILE on the
        // study Board at PLY once read; export-db=FORMAT[@all], the first database's chosen game, or its filter's
        // games; export-repertoire=N[@FORMAT], the Openings page's repertoire N printed;
        // export-dossier=NAME@FORMAT, export-report=FILE@PLY@FORMAT, export-crosstable=WORDS@FORMAT, once the
        // report is made.
        auto spec       = w.substr(w.find('=') + 1);
        const auto kind = w.substr(0, w.find('='));
        int format      = static_cast<int>(ExportFormat::Html);
        // A gesture once the sheet shows: @print, or @save:PATH.
        if (spec.size() > 6 && spec.substr(spec.size() - 6) == "@print") {
            export_action_ = "print";
            spec.resize(spec.size() - 6);
        } else if (const auto save = spec.find("@save:"); save != std::string::npos) {
            export_action_ = "save:" + spec.substr(save + 6);
            spec.resize(save);
        }
        bool all        = false;
        const auto take = [&] {
            const auto at = spec.rfind('@');
            if (at == std::string::npos) return;
            const auto name = spec.substr(at + 1);
            if (name == "all") {
                all  = true;
                spec = spec.substr(0, at);
                return;
            }
            format = name == "diagram" ? static_cast<int>(ExportFormat::Diagram)
                     : name == "gif"   ? static_cast<int>(ExportFormat::Gif)
                     : name == "pdf"   ? static_cast<int>(ExportFormat::Pdf)
                     : name == "pgn"   ? static_cast<int>(ExportFormat::Pgn)
                                       : static_cast<int>(ExportFormat::Html);
            spec   = spec.substr(0, at);
        };
        if (kind == "export-db") {
            if (spec.size() > 4 && spec.substr(spec.size() - 4) == "@all") {
                all = true;
                spec.resize(spec.size() - 4);
            }
            spec = "x@" + spec;
            take();
            page_               = Page::Databases;
            db_selected_        = 0;
            db_list_            = true;
            export_when_listed_ = format;
            export_listed_all_  = all;
        } else if (kind == "export-repertoire") {
            take();
            page_ = Page::Openings;
            repertoires().setActive(std::atoi(spec.c_str()));
            openRepertoirePrint(static_cast<std::size_t>(std::max(0, std::atoi(spec.c_str()))));
        } else if (kind == "export-dossier") {
            take();
            page_ = Page::Databases;
            study::DossierSubject subject;
            subject.name = spec;
            openDossier(std::move(subject), false);
            export_when_made_ = {0, format};
        } else if (kind == "export-crosstable") {
            take();
            const auto at       = spec.rfind('@');
            crosstable_request_ = std::make_pair(at == std::string::npos ? spec : spec.substr(0, at),
                                                 at == std::string::npos ? std::string() : spec.substr(at + 1));
            page_               = Page::Databases;
            db_selected_        = 0;
            db_list_            = true;
            db_tournaments_     = true;
            export_when_made_   = {2, format};
        } else if (kind == "export-report") {
            take();
            toStudyBoard();
            if (const auto at = spec.rfind('@'); at != std::string::npos && at > 1) {
                ply_when_read_ = std::atoi(spec.c_str() + at + 1);
                spec           = spec.substr(0, at);
            }
            report_when_read_ = 0;
            export_when_made_ = {1, format};
            dropFile(spec);
        } else {
            take();
            toStudyBoard();
            if (const auto at = spec.rfind('@'); at != std::string::npos && at > 1) {
                ply_when_read_ = std::atoi(spec.c_str() + at + 1);
                spec           = spec.substr(0, at);
            }
            export_when_read_ = format;
            dropFile(spec);
        }
    });
    words.on({"opening-report", "opening-report=*"}, [&] {
        // opening-report[=FILE@PLY[@TAB]]: the report of the Board page's position (#91); FILE on the board first,
        // at PLY once read, the report then at TAB (lines, plans, games, novelties, summary).
        toStudyBoard();
        if (const auto eq = w.find('='); eq != std::string::npos) {
            auto spec         = w.substr(eq + 1);
            int tab           = 0;
            const auto tab_at = spec.rfind('@');
            if (tab_at != std::string::npos && tab_at + 1 < spec.size() &&
                !std::isdigit(static_cast<unsigned char>(spec[tab_at + 1]))) {
                const auto name = spec.substr(tab_at + 1);
                tab  = name == "plans" ? 1 : name == "games" ? 2 : name == "novelties" ? 3 : name == "summary" ? 4 : 0;
                spec = spec.substr(0, tab_at);
            }
            if (const auto at = spec.rfind('@'); at != std::string::npos && at > 1) {
                ply_when_read_ = std::atoi(spec.c_str() + at + 1);
                spec           = spec.substr(0, at);
            }
            report_when_read_ = tab;
            dropFile(spec);
        } else {
            openOpeningReport(Page::Board);
        }
    });
    words.on({"lichess-finished=*"}, [&] {
        // lichess-finished=FILE: a game of the user's that ended, as Lichess would send it (#49), its PGN read
        // from FILE; nothing asked of Lichess.
        std::ifstream in(w.substr(17), std::ios::binary);
        lichess::Arrival a;
        a.pgn.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        study::ImportedGame g;
        if (study::readImportedGame(a.pgn, g)) {
            a.plies  = g.plies;
            a.result = g.result;
        }
        a.analysed = a.pgn.find("[%eval ") != std::string::npos;
        a.added    = true;
        FinishedGame f;
        f.game.id              = "abcd1234";
        f.game.full_id         = "abcd1234wxyz";
        f.game.opponent        = "Partner_3";
        f.game.opponent_rating = 2016;
        f.game.speed           = "blitz";
        f.base                 = "Lichess · rs_sample_0000";
        std::promise<lichess::Arrival> came;  // as if Lichess had just sent it
        came.set_value(std::move(a));
        f.arrival = came.get_future();
        lichess_finished_.push_back(std::move(f));
    });
    words.on({"lichess-api=http://127.0.0.1:*"}, [&] {
        // lichess-api=http://127.0.0.1:PORT[,WORD]: a test's server in place of lichess.org's API, explorer
        // (under /explorer), tablebase (under /tb) and external engines' work host (under /engine), on this
        // machine only; then WORD, another of these.
        const auto comma   = w.find(',');
        const auto base    = w.substr(12, comma == std::string::npos ? std::string::npos : comma - 12);
        lichess::servers() = lichess::Servers{base, base + "/tb", base + "/explorer", base + "/engine"};
        if (comma != std::string::npos) focus_next_ = w.substr(comma + 1);
    });
    words.on({"lichess-playing=*"}, [&] {
        // lichess-playing=live|correspondence[@FILE[@PLY]][,WORD]: a game of the user's in progress, as Lichess
        // would list it (#48): live, or by correspondence at the position of the Board page's game at PLY (where
        // it is, without PLY); FILE (.txt, .fen) read on the board first; then WORD, another of these.
        auto spec = w.substr(16);
        if (const auto comma = spec.find(','); comma != std::string::npos) {
            focus_next_ = spec.substr(comma + 1);
            spec        = spec.substr(0, comma);
        }
        const auto at = spec.find('@');
        int ply       = -1;
        if (at != std::string::npos) {
            auto file       = spec.substr(at + 1);
            const auto last = file.rfind('@');
            if (last != std::string::npos) {
                ply  = std::atoi(file.c_str() + last + 1);
                file = file.substr(0, last);
            }
            page_ = Page::Board;
            dropFile(file);
        }
        lichess::PlayingGame g;
        g.id              = "abcd1234";
        g.full_id         = "abcd1234wxyz";
        g.opponent        = "Partner_3";
        g.opponent_rating = 2016;
        const bool live   = spec.substr(0, at) == "live";
        g.speed           = live ? "blitz" : "correspondence";
        g.my_turn         = !live;
        g.seconds_left    = live ? 161 : 100800;
        g.fen = ply >= 0 ? boardAt(board_.game, board_.chess960, std::min<int>(ply, board_.game.moves.size())).getFen()
                         : boardFen();
        lichess_playing_.push_back(g);
    });
    words.on({"lichess", "lichess-studies"}, [&] {  // the Lichess page; its Studies card open (#79)
        page_ = Page::Lichess;
        if (w == "lichess-studies") {
            loadStudiesState();
            studies_.shown = studies_.opened = true;
        }
    });
    // Lichess TV's, its module's (#86): lichess-tv, tv-mosaic=CHANNEL, tv-board=CHANNEL:ID.
    tv_->addFocusWords(words, w);
    // The arenas', their module's (#95): lichess-tournaments, arena-mosaic=, swiss-mosaic=, play-arena=, play-swiss=,
    // play-tournaments.
    arenas_->addFocusWords(words, w);
    words.on({"board-to-study=*"}, [&] {  // board-to-study=FILE: on the Board, sent to a study (#79)
        std::ifstream in(w.substr(15), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        toStudyBoard();
        readOnBoard(text, std::filesystem::path(w.substr(15)).filename().string());
        openStudySend();
    });
    words.on({"link=*"}, [&] {  // link=URL: a Lichess link, as pasted on the Board page (#47)
        page_ = Page::Board;
        (void)offerLichessLink(w.substr(5));
    });
    words.on({"board-explain=*"}, [&] {
        // board-explain=FILE: its first game on the study Board, reviewed once read, then every move explained, the
        // Explained tab shown (#94).
        toStudyBoard();
        board_side_tab_       = 0;
        board_.open_tab       = 4;
        review_when_read_     = true;
        explain_after_review_ = true;
        dropFile(w.substr(14));
    });
    words.on({"endgame-check=*"}, [&] {
        // endgame-check=DIR: that finished tournament's endgames checked against the tables, as its menu does
        // (#94).
        startEndgameCheck(w.substr(14));
    });
    words.on({"board-visit=*"}, [&] {
        // board-visit=FILE: its first game on the study Board, its guided visit started once its review is read
        // (#94).
        toStudyBoard();
        visit_when_read_ = true;
        dropFile(w.substr(12));
    });
    words.on({"board-tab=explained"}, [&] {  // the Board page's moves card on its Explained tab (#94)
        board_.open_tab = 4;
    });
    words.on({"play", "play-start"}, [&] {  // the Play page (#83); a game against an engine started
        page_      = Page::Play;
        play_part_ = 0;
        if (w == "play-start") startEngineGame();
    });
    words.on({"play-lichess"}, [&] {  // the Play page's "On Lichess" part (#84)
        page_      = Page::Play;
        play_part_ = 1;
    });
    // The Lichess bot's, its module's (#85): engines-bot, bot-online.
    bot_->addFocusWords(words, w);
    words.on({"lichess-game=*"}, [&] {
        // lichess-game=ID[@black]: a game of the user's on Lichess played on the Board (#84), its stream asked of
        // the server --focus lichess-api= named.
        const auto spec = w.substr(13);
        const auto at   = spec.find('@');
        openOnlineGame(spec.substr(0, at), at == std::string::npos || spec.substr(at + 1) != "black");
    });
    words.on({"books-openings=*"}, [&] {  // books-openings=FILE: Lichess's named openings book there
        page_ = Page::Books;
        writeOpeningSuiteBook(w.substr(15));
    });
    words.on({"twic-server=http://127.0.0.1:*"}, [&] {
        // twic-server=http://127.0.0.1:PORT[,WORD]: a test's server in place of theweekinchess.com (#71), on
        // this machine only; then WORD, another of these.
        const auto comma = w.find(',');
        const auto from  = std::string_view("twic-server=").size();
        twic::setServer(w.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
        if (comma != std::string::npos) focus_next_ = w.substr(comma + 1);
    });
    words.on({"lichess-database=http://127.0.0.1:*"}, [&] {
        // lichess-database=http://127.0.0.1:PORT[,WORD]: a test's server in place of database.lichess.org (#73).
        const auto comma = w.find(',');
        const auto from  = std::string_view("lichess-database=").size();
        lichess::setDatabaseServer(w.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
        if (comma != std::string::npos) focus_next_ = w.substr(comma + 1);
    });
    words.on({"chesscom-server=http://127.0.0.1:*"}, [&] {
        // chesscom-server=http://127.0.0.1:PORT[,WORD]: a test's server in place of api.chess.com (#74).
        const auto comma = w.find(',');
        const auto from  = std::string_view("chesscom-server=").size();
        chesscom::setServer(w.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
        if (comma != std::string::npos) focus_next_ = w.substr(comma + 1);
    });
    words.on({"lichess-evals"}, [&] {  // the card of Lichess's evaluations kept offline (#58)
        page_                = Page::Lichess;
        offline_evals_focus_ = true;
    });
    words.on({"lichess-evals=*"}, [&] {
        // lichess-evals=FILE[@openings]: their preparation from that file (a .jsonl.zst), every position or the
        // openings only.
        page_                = Page::Lichess;
        offline_evals_focus_ = true;
        auto file            = w.substr(14);
        const bool openings  = file.size() > 9 && file.substr(file.size() - 9) == "@openings";
        if (openings) file.resize(file.size() - 9);
        offline_evals_choice_ = openings ? 1 : 0;
        startOfflineEvals(file, openings ? study::EVALS_OPENING_MEN : 0);
    });
    words.on({"databases-chesscom"}, [&] {  // the sheet of a chess.com player's games
        page_           = Page::Databases;
        chesscom_sheet_ = true;
    });
    words.on({"databases-broadcasts"}, [&] {  // the sheet of Lichess's broadcasts by month
        page_            = Page::Databases;
        broadcast_sheet_ = true;
    });
    words.on({"databases-twic"}, [&] {  // TWIC's sheet on the Databases page
        page_       = Page::Databases;
        twic_sheet_ = true;
    });
    words.on({"books-evaluated"}, [&] {  // the form of Lichess's evaluated positions
        page_ = Page::Books;
        openEvaluatedSuite();
    });
    words.on({"books-evaluated=*"}, [&] {
        // books-evaluated=FILE.zst: Lichess's evaluations database read from that file; =FILE.epd: the book
        // written there from what the profile kept.
        page_ = Page::Books;
        openEvaluatedSuite();
        const auto file = w.substr(16);
        if (file.size() > 4 && file.substr(file.size() - 4) == ".epd") {
            writeEvaluatedSuiteBook(file);
        } else {
            startEvalsReading(file);
        }
    });
    return words.applied();
}

void App::handleChessDialog(const Dialogs::Answer& answer) {
    const auto& path = answer.path;
    switch (answer.purpose) {
        case Dialogs::Purpose::Export:  // the Export sheet's file (#93)
            exportTo(path);
            break;
        case Dialogs::Purpose::TheoryPgn:  // a theory study (#77)
            addTheoryFile(path);
            break;
        case Dialogs::Purpose::TheoryFind:
            if (theory_chosen_ >= 0 && static_cast<std::size_t>(theory_chosen_) < theory().studies().size())
                theory().relocate(static_cast<std::size_t>(theory_chosen_), path);
            break;
        case Dialogs::Purpose::CtgPolyglotBook:
            if (selected_book_ >= 0 && selected_book_ < static_cast<int>(books_.size()) &&
                study::isCtg(books_[selected_book_].path))
                startCtgJob(CtgJobKind::Polyglot, books_[selected_book_].path, path);
            break;
        case Dialogs::Purpose::OpeningSuiteBook:
            writeOpeningSuiteBook(path);
            break;
        case Dialogs::Purpose::EvaluatedSuiteBook:
            writeEvaluatedSuiteBook(path);
            break;
        case Dialogs::Purpose::EvalsFile:
            startEvalsReading(path);
            break;
        case Dialogs::Purpose::OfflineEvalsFile:
            startOfflineEvals(path, offline_evals_choice_ == 1 ? study::EVALS_OPENING_MEN : 0);
            break;
        case Dialogs::Purpose::OfflinePuzzlesFile:
            startOfflinePuzzles(path);
            break;
        case Dialogs::Purpose::LichessMonthFile:
            startMonthReading(path);
            break;
        case Dialogs::Purpose::AppendBoardPgn:
            try {
                // After the file's games, a blank line between.
                if (chessBoard()) fillBoardEco();
                std::string text;
                {
                    std::ifstream in(std::filesystem::path(path), std::ios::binary);
                    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                }
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
                    text.pop_back();
                if (!text.empty()) text += "\n\n";
                text += gamePgn(board_.tree, board_.chess960);
                tour::replaceFile(std::filesystem::path(path), text);
                board_.changes.saved(std::filesystem::path(path).filename().string());
                boardToast(trf("Saved at the end of “{}”", std::filesystem::path(path).filename().string()));
            } catch (const std::exception& e) {
                board_.message       = trf("Cannot write {}: {}", path, e.what());
                board_.message_error = true;
            }
            break;
        case Dialogs::Purpose::AssistantModel: {
            // A GGUF file in place of the catalogue's model: loaded at the next question.
            auto settings       = assistant_->settings();
            settings.model_file = path;
            settings.not_now    = false;
            assistant_->setSettings(settings);
            assistant_->unload();  // after: a request that starts meanwhile loads the file chosen
            assistant_->saveSettings();
            break;
        }
        case Dialogs::Purpose::SaveBoardPgn:
            try {
                if (chessBoard()) fillBoardEco();
                tour::replaceFile(std::filesystem::path(path), gamePgn(board_.tree, board_.chess960));
                board_.changes.saved(std::filesystem::path(path).filename().string());
                board_.message       = trf("Saved in {}", path);
                board_.message_error = false;
            } catch (const std::exception& e) {
                board_.message       = trf("Cannot write {}: {}", path, e.what());
                board_.message_error = true;
            }
            break;
        case Dialogs::Purpose::ImportGames:
            importIntoDatabase(db_selected_, std::filesystem::path(path));
            break;
        case Dialogs::Purpose::AddDatabase:
            try {
                db_selected_ = static_cast<int>(shelf().add(std::filesystem::path(path)));
            } catch (const std::exception& e) {
                db_message_ = e.what();
            }
            break;
        case Dialogs::Purpose::FideList:
            startFideList(std::filesystem::path(path));
            break;
        // fastchess-gui's, answered by handleDialogs() before it calls this: named rather than a default, so that
        // the compiler tells of a purpose added without its case in either function (-Wswitch).
        case Dialogs::Purpose::AddEngine:
        case Dialogs::Purpose::EngineCommand:
        case Dialogs::Purpose::EngineDir:
        case Dialogs::Purpose::Book:
        case Dialogs::Purpose::Tablebases:
        case Dialogs::Purpose::TablebasesFolder:
        case Dialogs::Purpose::ExportGames:
        case Dialogs::Purpose::ExportAll:
        case Dialogs::Purpose::OpenTournament:
        case Dialogs::Purpose::ExportStandings:
        case Dialogs::Purpose::ImportEngineList:
        case Dialogs::Purpose::ImportEngineFolder:
        case Dialogs::Purpose::ImportRatingList:
        case Dialogs::Purpose::ExportRatings:
        case Dialogs::Purpose::ExportRatingHtml:
        case Dialogs::Purpose::ExportRatingMarkdown:
        case Dialogs::Purpose::ExportRatingPgn:
        case Dialogs::Purpose::ImportRatingPgn:
        case Dialogs::Purpose::ExportHeadToHead:
        case Dialogs::Purpose::ExportOpenings:
        case Dialogs::Purpose::AddBook:
        case Dialogs::Purpose::BookFile:
        case Dialogs::Purpose::SaveCleanBook:
        case Dialogs::Purpose::Chess324Book:
        case Dialogs::Purpose::Chess324SymmetricalBook:
        case Dialogs::Purpose::BuildBookPgn:
        case Dialogs::Purpose::BuildBookBin:
        case Dialogs::Purpose::SuiteFile:
        case Dialogs::Purpose::ExportSuiteCsv:
        case Dialogs::Purpose::TournamentsFolder:
        case Dialogs::Purpose::PgnOut:
        case Dialogs::Purpose::ExportSubmission:
        case Dialogs::Purpose::EngineOption:
        case Dialogs::Purpose::ConsoleLog:
        case Dialogs::Purpose::BoardPgn:
        case Dialogs::Purpose::EngineLogo:
        case Dialogs::Purpose::PieceFolder:
        case Dialogs::Purpose::MachineKey:
        case Dialogs::Purpose::MachineProgram:
        case Dialogs::Purpose::MachineEngineBuild:
            break;
    }
}

}  // namespace fcg
