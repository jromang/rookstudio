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
#include "endgames_page.hpp"
#include "fide_rating_list.hpp"
#include "focus_words.hpp"
#include "guess_page.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "keyboard.hpp"
#include "lichess_arenas_page.hpp"
#include "lichess_bot_page.hpp"
#include "lichess_months.hpp"
#include "lichess_tv_page.hpp"
#include "mistakes_page.hpp"
#include "puzzles_page.hpp"
#include "repertoire_training_page.hpp"
#include "review_bridge.hpp"
#include "review_runner.hpp"
#include "training_page.hpp"
#include "twic.hpp"
#include "vision_page.hpp"

namespace fcg {

// What RookStudio's modules ask of App (app_context.hpp, #107), answered with App's members.
class App::ModulesContext final : public AppContext {
   public:
    explicit ModulesContext(App& app) : app_(app) {}

    // The program.
    int frame() const override { return ImGui::GetFrameCount(); }
    double seconds() const override { return ImGui::GetTime(); }
    bool shortcutPressed(int chord) const override { return keyboard::shortcut(chord); }
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
        return app_.lichess_profile_.value ? &*app_.lichess_profile_.value : nullptr;
    }
    void askLichessProfile() override { app_.askLichessProfile(); }

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
    std::string sessionLock() const override { return app_.training_->lock(); }
    BoardTaken takeBoard(const std::string& title, bool tree) override {
        app_.stopVisit();  // a guided visit under way ends: the Board is the session's now (#137)
        app_.training_->closeSessionForAnother();  // the session that holds it closed, the day's review going on (#80)
        auto& b = app_.board_;
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
    void replaySessionGame(ReplayGame game, int ply, bool chess960) override {
        auto& b    = app_.board_;
        b.game     = std::move(game);
        b.tree     = GameTree{b.game, {}};
        b.line     = -1;
        b.ply      = ply;
        b.chess960 = chess960;
        b.selected.reset();
        b.move_typed.reset();
        b.new_game = true;
    }
    void showSessionPly(int ply) override { app_.board_.ply = ply; }
    std::optional<chess::Square> drawFlatBoard(float size, const std::array<chess::Piece, 64>& pieces,
                                               const BoardDecor& marks, const chess::Board* moves) override {
        auto& b          = app_.board_;
        BoardDecor decor = marks;
        decor.flipped    = b.flip;
        decor.eval_white = -1.0f;
        if (b.ply > 0) decor.last_move = b.game.moves[static_cast<std::size_t>(b.ply - 1)].move;
        decor.selected = moves ? b.selected : std::nullopt;
        if (decor.selected)
            for (const auto& m : movesFrom(*moves, *decor.selected)) decor.targets.push_back(shownTarget(m));
        const ImVec2 top = ImGui::GetCursorScreenPos();
        drawBoard(app_.atlas_, pieces, size, decor);
        const auto hover = boardSquareAt(ImGui::GetMousePos(), top, size, decor);
        const bool left  = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        if (moves) {
            app_.boardPointer(*moves, left ? hover : std::nullopt, hover, ImGui::IsItemClicked(ImGuiMouseButton_Right));
            app_.drawPromotionPopup(*moves);
            return std::nullopt;
        }
        return left ? hover : std::nullopt;
    }
    void followSessionGame(ReplayGame game) override {
        auto& b = app_.board_;
        b.game  = std::move(game);
        b.tree  = GameTree{b.game, {}};
        b.line  = -1;
        b.ply   = static_cast<int>(b.game.moves.size());
        b.selected.reset();
    }
    bool typingMove() const override { return app_.board_.move_typed.has_value(); }
    void setFlip(bool flip) override { app_.board_.flip = flip; }
    void dropPick() override { app_.board_.selected.reset(); }
    void dropTypedMove() override { app_.board_.move_typed.reset(); }
    void beginSessionPage() override {
        ui::pageTitle(TR("Board"), app_.guideSection());
        // The letters typed reach the move field whatever the keyboard's layout (see drawBoardPage).
        if (keyboard::available() && !SDL_TextInputActive(app_.window_)) SDL_StartTextInput(app_.window_);
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

    // The Training page and its day's review, the Training page module's.
    const TrainingSettings& trainingSettings() override { return app_.training_->trainingSettings(); }
    void setTrainingSettings(const TrainingSettings& settings) override {
        app_.training_->setTrainingSettings(settings);
    }
    void showTrainingPage(TrainingPart part) override { app_.training_->showTrainingPage(part); }
    bool trainingShown() const override { return app_.training_->trainingShown(); }
    void setEnterStartsPuzzles(bool puzzles) override { app_.training_->setEnterStartsPuzzles(puzzles); }
    void reviewOnly(ReviewKind kind) override { app_.training_->reviewOnly(kind); }
    bool reviewRunning() const override { return app_.training_->reviewRunning(); }
    bool nextReview() override { return app_.training_->nextReview(); }
    void stopReview() override { app_.training_->stopReview(); }
    void drawReviewNext(bool enter) override { app_.training_->drawReviewNext(enter); }

    // The user's mistakes, their module's.
    std::map<std::string, std::size_t> mistakeKinds(uint32_t since) override {
        return app_.mistakes_->mistakeKinds(since);
    }
    std::vector<const MistakeExercise*> mistakeExercises() override { return app_.mistakes_->mistakeExercises(); }

    // Lichess's puzzles, their module's.
    ThemeResults puzzleTheme(const std::string& theme) override {
        const auto& themes = app_.puzzles_->puzzleRecord().themes;
        const auto it      = themes.find(theme);
        return it == themes.end() ? ThemeResults{} : it->second;
    }
    bool startPuzzleTheme(const std::string& theme) override { return app_.puzzles_->startTheme(theme); }
    bool puzzlesOnDisk() const override { return app_.puzzles_->store() != nullptr; }
    bool puzzlesOnline() const override { return app_.puzzles_->puzzleScopes(); }

    // The file dialogs.
    void choosePuzzleFile() override {
        app_.dialogs_.openFile([&app = app_](const std::string& path) { app.puzzles_->startOffline(path); },
                               {{"zstd", "zst"}, {TR("All files"), "*"}});
    }

    // The user's names.
    std::vector<std::string> userNames() const override { return app_.userNames(); }
    void drawPlayerNames(const char* id) override { app_.drawPlayerNames(id); }
    void showNamesSettings() override { app_.page_ = Page::Settings; }

    // The Board page's analysis.
    const EngineEntry* analysisEngine() override { return app_.analysisEngine(); }
    const AnalysisSettings& analysisSettings() override {
        app_.loadBoardSettings();
        return app_.board_.settings.analysis;
    }

    // Games reviewed as the Board page reviews a game.
    const EngineEntry* engineNamed(const std::string& name) const override { return app_.engineNamed(name); }
    std::unique_ptr<ReviewRunner> reviewInBackground(const EngineEntry& engine, const ReplayGame& game,
                                                     int& processes) override {
        // As the Board page reviews a game in the Chess space (#61, #63): nodes, one thread, an empty table each
        // position, Lichess's evaluations kept offline taken as they are, the processes the tournaments leave.
        app_.loadBoardSettings();
        const auto& settings = app_.board_.settings;
        ReviewRunner::Setup setup;
        setup.engine       = engine;
        setup.settings     = settings.analysis;
        setup.game         = game;
        setup.ceiling_ms   = settings.review_ceiling_ms;
        setup.known        = app_.reviewKnown();
        setup.reproducible = true;
        processes          = reviewProcesses(std::max(1, settings.analysis.threads), app_.topology_.physical,
                                             app_.topology_.logical, app_.tournamentCores());
        setup.processes    = processes;
        setup.second_line  = secondLines(game, false);
        auto runner        = std::make_unique<ReviewRunner>();
        runner->start(std::move(setup));
        return runner;
    }
    int64_t reviewCeilingMs() override { return app_.board_.settings.review_ceiling_ms; }
    bool boardAnalysing() const override { return app_.board_.analysing || app_.board_.reviewing(); }

    // The repertoires' positions, their training module's.
    std::array<std::unordered_set<uint64_t>, 2> repertoirePositions() override {
        return app_.rep_training_->repertoirePositions();
    }

    // Games opened on the Board from a session.
    void openDatabaseGame(const std::string& pgn, const std::string& title, const std::filesystem::path& folder,
                          const std::string& name, uint64_t id, int ply) override {
        app_.readOnBoard(pgn, title);
        BoardOrigin origin;
        origin.folder      = folder;
        origin.name        = name;
        origin.id          = id;
        app_.board_origin_ = std::move(origin);
        app_.board_.ply    = std::min(ply, static_cast<int>(app_.board_.game.moves.size()));
    }
    void openGameOnBoard(ReplayGame game, int ply, std::string title) override {
        app_.openOnBoard(std::move(game), ply, false, std::move(title), false);
    }

    // The Board's own game.
    context::ShownGame shownGame() const override {
        const auto& b = app_.board_;
        std::optional<std::pair<std::filesystem::path, uint64_t>> origin;
        if (app_.board_origin_) origin = std::make_pair(app_.board_origin_->folder, app_.board_origin_->id);
        // As the guessing's part always asked (#81): an endgame's or a vision exercise's game counts as the Board's.
        const bool session = app_.rep_training_->onBoard() || app_.puzzles_->onBoard() || app_.mistakes_->onBoard() ||
                             app_.guessing_->onBoard();
        return context::ShownGame{b.game, b.ply, b.chess960, b.title, std::move(origin), app_.page_ == Page::Board,
                                  session};
    }
    int treeDatabase() const override { return app_.tree_db_; }

    // The Syzygy tables of the disk, and their download.
    std::string diskTablesPaths() override { return app_.endgameTablesPaths(); }
    std::string tablesSetting() const override { return app_.setup_.tb_paths; }
    int diskTablesPieces() override { return app_.diskTablesPieces(); }
    bool diskTablesLocked() const override { return app_.diskTablesLocked(); }
    SyzygyDownload* tablesDownload() override { return app_.syzygy_download_.get(); }
    const SyzygyDownload::State& tablesHeld() override { return app_.syzygyState(); }
    const std::string& tablesDownloadError() const override { return app_.syzygy_error_; }
    void startTablesDownload() override {
        if (!app_.syzygy_download_ || app_.syzygy_download_->busy()) return;
        app_.syzygy_error_.clear();
        app_.syzygy_download_->start();
    }
    void chooseTablesFolder() override { app_.dialogs_.openFolder(Dialogs::Purpose::Tablebases); }

    // The Play page's opponent.
    const PlaySettings& playSettings() override { return app_.playSettings(); }
    const EngineEntry* playEngine() override {
        const EngineEntry* engine = app_.engineNamed(app_.playSettings().engine);
        if (engine && engine->inactive) engine = nullptr;
        return engine ? engine : app_.analysisEngine();
    }
    std::string playOpenings(chess::Color engine_side) override {
        std::string text, tag, note;
        uint64_t least = 1;
        (void)app_.openingSource(app_.playSettings(), engine_side, text, tag, note, least, false);
        return text;
    }
    std::function<std::optional<std::string>(const chess::Board&)> playBook(chess::Color engine_side,
                                                                            std::mt19937& rng) override {
        std::string text, tag, note;
        uint64_t least   = 1;
        const auto& play = app_.playSettings();
        auto source      = app_.openingSource(play, engine_side, text, tag, note, least);
        if (!source) return {};
        auto book = std::make_shared<OpeningBook>(std::move(source), play.vary, least, rng());
        return [book](const chess::Board& board) { return book->move(board); };
    }
    void showPlayPage() override { app_.page_ = Page::Play; }

    // The Openings page and its repertoires.
    RepertoireShelf& repertoires() override { return app_.repertoires(); }
    bool measuredOn(const std::filesystem::path& folder) const override {
        return !app_.coverage_left_.contains(folder);
    }
    void showOpeningsPage() override { app_.page_ = Page::Openings; }

    // The user's correspondence games on Lichess.
    bool playingOnLichess() const override { return !app_.lichess_playing_.empty(); }
    int correspondencePly(const ReplayGame& line) const override {
        return app_.correspondencePly(line, false, INT32_MAX);
    }

    // The Board as the repertoires' training asks on it.
    void askAgainAt(int ply) override {
        auto& b = app_.board_;
        if (b.game.moves.size() > static_cast<std::size_t>(ply)) {
            b.game.moves.resize(static_cast<std::size_t>(ply));
            b.tree = GameTree{b.game, {}};
        }
        b.line = -1;
        b.ply  = ply;
        b.selected.reset();
    }
    void showTreeAt(int ply) override {
        if (app_.board_.ply > ply) app_.board_.ply = ply;
        app_.board_side_tab_ = 2;
    }
    void sayOnBoard(const std::string& text) override {
        app_.board_.message       = text;
        app_.board_.message_error = true;
    }

    // The views and the pages offered once.
    bool hasViews() const override { return !app_.views_.empty(); }
    std::string viewName() const override { return app_.views_.empty() ? std::string() : app_.view().name; }
    bool viewHolds(views::PageId page) const override { return !app_.views_.empty() && app_.view().holds(page); }
    bool viewsHold(views::PageId page) const override {
        return std::any_of(app_.views_.begin(), app_.views_.end(), [&](const auto& v) { return v.holds(page); });
    }
    void addToView(views::PageId page, const std::vector<views::PageId>& after,
                   const std::vector<views::PageId>& before) override {
        if (app_.views_.empty()) return;
        auto& view  = app_.views_[static_cast<std::size_t>(app_.view_)];
        auto& pages = view.pages;
        auto at     = pages.end();
        for (const auto p : after)
            if (at == pages.end()) at = std::find(pages.begin(), pages.end(), p);
        if (at != pages.end()) {
            ++at;
        } else {
            for (const auto p : before)
                if (at == pages.end()) at = std::find(pages.begin(), pages.end(), p);
        }
        pages.insert(at, page);
        views::tidy(view);
        app_.saveViews();
    }
    std::vector<std::string> pagesOffered() const override { return app_.storage_.pagesOffered(); }
    void setPagesOffered(const std::vector<std::string>& pages) override { app_.storage_.setPagesOffered(pages); }

    // The Training page among the program's pages.
    void toTrainingPage() override { app_.page_ = Page::Training; }
    bool trainingPageShown() const override { return app_.page_ == Page::Training; }
    bool boardGamePlayed() const override {
        return (app_.board_.match && app_.board_.match_state.running) || app_.engineGameRunning();
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

void App::initChessModules() {
    modules_context_ = std::make_unique<ModulesContext>(*this);
    tv_              = std::make_unique<LichessTvPage>(*modules_context_);
    arenas_          = std::make_unique<LichessArenasPage>(*modules_context_);
    bot_             = std::make_unique<LichessBotPage>(*modules_context_);
    puzzles_         = std::make_unique<PuzzlesPage>(*modules_context_);
    mistakes_        = std::make_unique<MistakesPage>(*modules_context_);
    guessing_        = std::make_unique<GuessPage>(*modules_context_);
    endgames_        = std::make_unique<EndgamesPage>(*modules_context_);
    vision_          = std::make_unique<VisionPage>(*modules_context_);
    rep_training_    = std::make_unique<RepertoireTrainingPage>(*modules_context_);
    // The Training page and the kinds of training that plug into it, in the order the Board asks them which holds it.
    training_ = std::make_unique<TrainingPage>(
        *modules_context_, std::vector<TrainingKind*>{rep_training_.get(), puzzles_.get(), mistakes_.get(),
                                                      guessing_.get(), endgames_.get(), vision_.get()});
}

// The Training page's hooks in fastchess-gui's pages (#80), which do not include its module's header.
void App::drawTraining() { training_->draw(); }
const char* App::trainingGuideTopic() const { return training_->guideTopic(); }

// The training sessions on the Board page (board_session.hpp, #107): one at a time holds the Board, the Training page
// knows which.
BoardSession* App::boardSession() const { return training_->boardSession(); }

bool App::drawBoardSession() {
    auto* session = boardSession();
    if (!session) return false;
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

const char* App::sessionGuideTopic() const {
    const auto* session = boardSession();
    return session ? session->guideTopic() : nullptr;
}

bool App::trainingHidesAids() const {
    // The question of the training session that holds the Board, whatever its kind (#76, #80–#82).
    const auto* session = boardSession();
    return session && session->hidesAids();
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
    pollLichessProfile();      // the account's profile: its answer, the copy kept on disk, another account (#46)
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
    rep_training_->poll();     // a repertoire's training: a session's cards prepared, its first question (#76)
    puzzles_->poll();          // Lichess's puzzles: a preparation's end, the batch online's requests (#80)
    mistakes_->poll();         // the user's mistakes: looked for, their games reviewed, a session's engine (#81)
    guessing_->poll();         // guessing the moves: a game drawn at random, the theory, the engine's searches (#81)
    pollTablesDownload();      // the Syzygy tables' download: its end, the tables loaded again (#82)
    vision_->poll();           // board vision: the coordinates' thirty seconds, the blindfold game (#82)
    endgames_->poll();         // the endgames: the tables' answers and moves (#82)
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
    // The Training page, its parts and the day's review, then each kind's words (a repertoire's session, the puzzles'
    // database, the user's mistakes, guessing the moves, the endgames, board vision): their modules' (#76, #80–#82,
    // #107).
    training_->addFocusWords(words, w);
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

void App::openGamesImport(const std::vector<std::pair<std::string, std::string>>& filters) {
    dialogs_.openFile(
        [this](const std::string& path) { importIntoDatabase(db_selected_, std::filesystem::path(path)); }, filters);
}

void App::openAssistantModel() {
    dialogs_.openFile(
        [this](const std::string& path) {
            // A GGUF file in place of the catalogue's model: loaded at the next question.
            auto settings       = assistant_->settings();
            settings.model_file = path;
            settings.not_now    = false;
            assistant_->setSettings(settings);
            assistant_->unload();  // after: a request that starts meanwhile loads the file chosen
            assistant_->saveSettings();
        },
        {{"GGUF", "gguf"}});
}

void App::saveBoardPgnAs(const std::string& path) {
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
}

void App::appendBoardPgnTo(const std::string& path) {
    try {
        // After the file's games, a blank line between.
        if (chessBoard()) fillBoardEco();
        std::string text;
        {
            std::ifstream in(std::filesystem::path(path), std::ios::binary);
            text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
        if (!text.empty()) text += "\n\n";
        text += gamePgn(board_.tree, board_.chess960);
        tour::replaceFile(std::filesystem::path(path), text);
        board_.changes.saved(std::filesystem::path(path).filename().string());
        boardToast(trf("Saved at the end of “{}”", std::filesystem::path(path).filename().string()));
    } catch (const std::exception& e) {
        board_.message       = trf("Cannot write {}: {}", path, e.what());
        board_.message_error = true;
    }
}

}  // namespace fcg
