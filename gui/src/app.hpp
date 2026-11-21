#pragma once

#include <array>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include <matchmaking/sprt/sprt.hpp>
#include <matchmaking/stats.hpp>

#include <commentary/openings.hpp>
#include <commentary/question.hpp>
#include <commentary/references.hpp>
#include <commentary/review.hpp>
#include <commentary/story.hpp>
#include <commentary/story_model.hpp>
#include <commentary/tablebase.hpp>
#include <study/ctg.hpp>
#include <study/query.hpp>
#include <study/search.hpp>
#include <tour/polyglot.hpp>
#include "assistant_notes.hpp"
#include "board_3d.hpp"
#include "board_nav.hpp"
#include "board_page.hpp"
#include "board_view.hpp"
#include "book_jobs.hpp"
#include "broadcaster.hpp"
#include "chesscom.hpp"
#include "console.hpp"
#include "databases.hpp"
#include "dialogs.hpp"
#include "endgames.hpp"
#include "engine_changes.hpp"
#include "engine_downloads.hpp"
#include "engine_import.hpp"
#include "engine_notes.hpp"
#include "engine_play.hpp"
#include "engines.hpp"
#include "fide_cards.hpp"
#include "game_graph.hpp"
#include "guide_view.hpp"
#include "home.hpp"
#include "https.hpp"
#include "layout.hpp"
#include "lichess_account.hpp"
#include "lichess_broadcasts.hpp"
#include "lichess_evals.hpp"
#include "lichess_games.hpp"
#include "lichess_links.hpp"
#include "lichess_month.hpp"
#include "lichess_months.hpp"
#include "lichess_players.hpp"
#include "lichess_playing.hpp"
#include "lichess_profile.hpp"
#include "lichess_puzzles.hpp"
#include "lichess_settings.hpp"
#include "lichess_share.hpp"
#include "live.hpp"
#include "machine_jobs.hpp"
#include "machine_run.hpp"
#include "mistakes.hpp"
#include "model.hpp"
#include "notice.hpp"
#include "offline_evals.hpp"
#include "opening_suite.hpp"
#include "pgn_store.hpp"
#include "portable.hpp"
#include "prep.hpp"
#include "profiles.hpp"
#include "puzzles.hpp"
#include "qr.hpp"
#include "remote_run.hpp"
#include "repertoires.hpp"
#include "review_queue.hpp"
#include "review_tables.hpp"
#include "run.hpp"
#include "stats.hpp"
#include "storage.hpp"
#include "suite_run.hpp"
#include "syzygy_download.hpp"
#include "tablebases.hpp"
#include "theory_shelf.hpp"
#include "topology.hpp"
#include "training.hpp"
#include "twic.hpp"
#include "ui.hpp"
#include "views.hpp"
#include "vision.hpp"

#include <tour/book_scan.hpp>
#include <tour/by_opening.hpp>
#include <tour/game_db.hpp>
#include <tour/incidents.hpp>
#include <tour/ratinglist.hpp>
#include <tour/standings.hpp>
#include <tour/submission.hpp>
#include <tour/teams.hpp>

struct SDL_Window;

namespace fcg {

class AssistantService;
class EngineOracle;
struct CommentaryReview;

class EngineHub;

// The Games tab's filter on the games whose opening was flagged unbalanced (the last of its list).
inline constexpr int UNBALANCED_FILTER = 11;
// The Openings tab's view of the openings with such a game.
inline constexpr int UNBALANCED_OPENINGS = 4;

// The date in the name of a tournament's folder: "20261109-185927_a_vs_b" -> "2026-11-09 18:59".
std::string folderDate(const std::filesystem::path& dir);

// The PgnField a chart reads from the move comments of a PGN: 0 for the evaluation, the time and the depth, which
// are always written.
constexpr uint8_t chartField(GraphChart chart) {
    switch (chart) {
        case GraphChart::Nodes:
            return PGN_NODES;
        case GraphChart::Speed:
            return PGN_NPS;
        case GraphChart::TbHits:
            return PGN_TBHITS;
        case GraphChart::Predicted:
            return PGN_PV;
        default:
            return 0;
    }
}
// The PgnField of its thinner line: the decision time beside the move time, the seldepth beside the depth.
constexpr uint8_t chartExtraField(GraphChart chart) {
    return chart == GraphChart::Time ? PGN_DECISION : chart == GraphChart::Depth ? PGN_SELDEPTH : 0;
}
// The name of the setup's PGN option that writes a PgnField.
const char* pgnFieldOption(uint8_t field);
// Whether a tournament's PGN options write a PgnField.
bool pgnFieldOn(const fastchess::config::Pgn& pgn, uint8_t field);
// Why a game's PGN lacks a PgnField: the option that writes it was off (`on` false), or it was on and the engines
// did not give what it needs (the decision time is only written when their info lines tell it); nullopt when the
// tournament's options are not known.
std::string missingPgnFieldTip(uint8_t field, std::optional<bool> on);

// "W 12 · D 80 · L 8 %" and a thin bar of the three, for a side's own win, draw, loss in permille.
void wdlLine(const std::array<int64_t, 3>& wdl);

// After a PGN file was added for the ratings: whether to show its players, for the user to say which library engine
// they are. Those that library engines may be (two of the same name, or one by a hint), that the user has not decided
// (an engine chosen, or a separate one) and that were not offered before the file was added: `before`, their names
// folded (tour::foldedName()).
[[nodiscard]] bool reviewPgnPlayers(const std::vector<tour::GameDb::Player>& players,
                                    const std::set<std::string>& before, const std::vector<std::string>& separate);

// The move the side to move in a live game thinks of and the reply it expects (the first two moves of its PV), each
// in the colour of the engine that would play it, the reply lighter; none between searches. `board`: the position.
[[nodiscard]] std::vector<BoardArrow> pvArrows(const SlotState& st, const chess::Board& board);

struct AppOptions {
    bool autostart = false;
    std::string focus_window;  // page or tab shown at startup (for screenshots)
    float ui_scale = 1.0f;     // DPI scale, to rebuild the style when the theme changes
    bool scripted  = false;    // scripted screenshots: nothing is asked from the internet
};

class App {
   public:
    App(SDL_Window* window, Storage storage, CpuTopology topology, AppOptions options);
    ~App();

    // Builds one frame of UI. Call between ImGui::NewFrame() and ImGui::Render().
    void frame();

    // The window asked to close: confirm first if a match is running.
    void requestQuit();
    [[nodiscard]] bool shouldQuit() const noexcept { return quit_; }
    // Frames wanted at a steadier pace than the usual 30 a second: a 3D board's camera moving.
    [[nodiscard]] bool wantsSmoothFrames() const { return board3d_.moving(); }

    // Quits without asking (--shot, --quit-after): what this window started stops, the tournaments found playing at
    // launch play on.
    void forceQuit();
    // Quits, the tournaments playing on in their own processes (the quick test stops).
    void keepRunningAndQuit();

    // The window came back to the front: an engine may have been rebuilt or moved meanwhile.
    void onFocusGained() { refreshPrograms(); }

    // A file dropped on the window: a PGN file, or a FEN or EPD position, opens on the Board page.
    void dropFile(const std::string& path);

    // The window moved to a screen of another scale (or the screen's changed): fonts and sizes follow it.
    void onDisplayScale(float scale);

    // For scripted screenshots.
    void focusWindow(const std::string& name);
    bool debugSelectGame(int index);

    [[nodiscard]] bool initGraphics();

   private:
    struct OpenRun {
        std::unique_ptr<Run> run;
        double weight   = 1;  // its share of the machine against the other tournaments' (budget.hpp)
        bool quick_test = false;
        // Found playing at launch (it played before this window): forceQuit() lets it play on.
        bool found = false;
        std::string finish_message;
        std::vector<Fix> finish_fixes;
        std::string finish_fixes_for, stopped_by;
        bool user_stop = false, resume_guarded = false;
        std::size_t games_at_start = 0;
        uint64_t console_mark      = 0;
        // watchStartFailures(): the version of its StartFailures seen, and the participants told set aside.
        uint64_t start_failures_seen = ~uint64_t(0);
        std::set<int> start_failures_told;
        // Played on a remote machine (machine_run.hpp): which, what became of its process there, and its files
        // copied back. Empty and null for a tournament of this computer.
        std::string machine_id, machine_name;
        std::shared_ptr<LinkStatus> link_status;
        std::shared_ptr<MachineSync> sync;
        bool sync_finishing = false;
    };
    // One view per concurrent game, shared by the mosaic, the focus panel and detached windows.
    struct SlotView {
        SlotState state;
        chess::Board board;
        uint64_t board_version = ~0ULL;
        bool flip              = false;
        // The game whose board was turned to the seed's side (gauntlet).
        std::size_t turned_for = ~std::size_t{0};
        std::size_t followed   = 0;  // moves shown, to keep the move list scrolled to the end
        int follow_frames      = 0;
        bool last_capture      = false;  // the last move took a piece: its sound (hearMoves)
        // The position the board shows: the last, unless the user stepped back in the game (reset for each game).
        FollowedPly nav;
        std::size_t nav_game = ~std::size_t{0};
        int listed_ply       = -1;  // nav's ply the move list was last scrolled to
        Clock::time_point last_active{};  // the last frame its game was being played (shownSlots)
        // Its opening and the end of its book, read again when a move comes (liveOpening): for the game and the
        // number of moves it was read at.
        LiveOpening opening;
        std::pair<std::size_t, std::size_t> opening_at{~std::size_t{0}, ~std::size_t{0}};
        float cards_fixed = 0;  // what the two engine cards took besides their lines, the last frame
        // What each side's card, or its search lines in a narrow panel, has needed so far in this game (reset for
        // each game): they keep that height, so that the moves and the curve under them stay where they are (#193).
        std::array<int, 2> pv_rows{};
        std::array<float, 2> card_h{};
        std::array<float, 2> search_h{};
    };

    enum class Page {
        Home,
        NewMatch,
        Tournament,
        Engines,
        Books,
        Board,
        Suites,
        Ratings,
        Log,
        Settings,
        // The Chess space's own pages.
        ChessHome,
        Databases,
        Openings,
        Lichess,
        Training,  // #80
        Play,      // #83
        Count      // none: the sheet of the views is shown in place of the page
    };
    enum class Tab { Overview, Standings, Bracket, Schedule, Live, Games, Openings, Broadcast };

    // The views (#43, mockup M6; views.hpp, view_views.cpp): one to three sets of pages of the user's, switched at the
    // top of the rail (Ctrl+1 to Ctrl+3), each with its rail and the Board it shows, in place of the two spaces.
    std::vector<views::View> views_ = views::fromSpaces();
    int view_                       = 0;
    std::vector<Page> view_pages_;  // the page each view comes back to
    [[nodiscard]] const views::View& view() const { return views_[static_cast<std::size_t>(view_)]; }
    [[nodiscard]] static std::optional<views::PageId> rootOf(Page page);  // the page of the rail it belongs to
    [[nodiscard]] static Page pageFor(views::PageId id);                  // where a page of the rail opens
    [[nodiscard]] bool viewHolds(Page page) const;                        // the view shown holds it
    void switchView(int index);                                           // to another view, on the page it showed last
    void saveViews();
    // A page shown by a command or a notice that the view does not hold: the first view that holds it, else it stays
    // in the view shown (drawPageInNoView offers to add it).
    void followPage();
    void toStudyBoard();  // the Board, in a view that shows the study's
    void drawViewSwitch(float width);
    void drawViewRail(float width, float button);
    void drawPageInNoView();
    // The sheet where the views are made (M6, board 2), in place of the page while open.
    struct ViewsSheet {
        bool open    = false;
        int selected = 0;
        std::vector<views::View> before;             // Esc: back to them
        std::vector<std::vector<views::View>> undo;  // Ctrl+Z
    } views_sheet_;
    void openViewsSheet(int selected);
    void drawViewsSheet();
    void drawBoardRailButton(float width, float button);  // in both rails, with its analysis's dot
    void drawChessHome();
    Page nav_page_ = Page::Home;  // the page ImGui's keyboard cursor was last hidden for (view_shell.cpp)
    void openBoardPgnDialog();    // Ctrl+O in the Chess space: a PGN file on the board
    void newBoardGame();  // Ctrl+N in the Chess space: the start position, the game shown among the recent ones //
                          // view_chess.cpp
    // The Databases page (view_databases.cpp): the databases of the profile, an import into one of them.
    void drawDatabases();
    DatabaseShelf& shelf();  // made at its first use
    // `file` imported into database `index`, or into a new database named after it for -1.
    // `pasted`: games of the clipboard written to a file of the program's, removed once imported.
    void importIntoDatabase(int index, const std::filesystem::path& file, bool pasted = false);
    void pasteGamesInto(int index);  // the clipboard's games imported into a database
    // The last import's outcome (mockup M1, board 4), said on its database until another starts or it is closed: what
    // happened, what was kept, and the gesture that mends it.
    struct ImportStart {
        std::filesystem::path folder;
        uint64_t games = 0, bytes = 0;  // the database's before it
        bool pasted  = false;
        bool created = false;  // the database was made for it: discarded when the file brings nothing
    };
    std::optional<ImportStart> db_import_start_;
    struct ImportOutcome {
        std::filesystem::path folder, file;
        study::DatabaseImport report;
        bool pasted     = false;
        bool disk_full  = false;
        uint64_t needed = 0;  // the bytes the rest of the file would take, roughly, when the disk was full
        struct Damaged {
            study::GameId id = 0;
            std::string why;
            uint64_t line = 0;  // the line of the file its text starts on, 0 when not known
            std::string move;   // the move its moves stop at, as written; empty when none
        };
        std::vector<Damaged> damaged;  // the first few
    };
    std::optional<ImportOutcome> db_outcome_;
    void drawImportOutcome(std::size_t index);
    // A ChessBase database dropped or chosen (#69, mockup M10, board 1): what it brings and leaves, asked before its
    // copy into a RookStudio database; an enciphered archive's password (.cbz, .2cbz) asked there, not kept, and
    // tried on the archive's first block before the copy starts: a wrong one is said there, to be typed again.
    struct ChessBaseAsk {
        int index = -1;  // the database it goes into; -1, a new one named after it
        std::filesystem::path file;
        std::string format;  // "CBH" or "2CBH", empty for an archive (counted as it is read)
        uint32_t records = 0;
        bool archive = false, enciphered = false;
        std::string error;
        std::array<char, 128> password{};
        bool wrong_password = false;  // the last one tried does not decipher the archive
        bool focus          = true;   // the password field takes the keyboard (when the card opens, after a wrong one)
    };
    std::optional<ChessBaseAsk> db_chessbase_;
    bool db_chessbase_go_ = false;
    std::string db_chessbase_password_;
    void drawChessBaseAsk();
    // Sources of games (#70, #71, mockup M10): the Databases page's "From a source…"; TWIC's sheet, its issues
    // downloaded then imported one after the other, its report; the weekly look for a new issue, off until ticked;
    // the followed sources under the list of databases, and a new issue on the Home page.
    void drawSourcesMenu();
    void drawTwicSheet();
    void drawTwicReport();
    void drawFollowedSources();
    void drawTwicHomeNotice();
    void startTwicIndex(bool weekly);
    void startTwicAdd(std::vector<twic::Issue> issues, int database);  // database: the shelf's index, -1 a new one
    bool twicImportEnded(const std::filesystem::path& file, const study::DatabaseImport& report);
    void pollTwic();
    void loadTwic();
    void saveTwic();
    twic::State twic_;
    bool twic_loaded_ = false;
    bool twic_sheet_  = false;
    std::set<int> twic_ticked_;
    int twic_target_ = -2;  // the database chosen in the sheet: the shelf's index, -1 a new one, -2 not chosen yet
    struct TwicIndexJob {
        std::future<std::pair<std::vector<twic::Issue>, std::string>> done;
        bool weekly = false;
    };
    std::optional<TwicIndexJob> twic_index_job_;
    std::vector<twic::Issue> twic_issues_;  // the index's, this session
    std::string twic_error_;
    struct TwicAdd {
        std::string title;                          // what is added, said by the report: "TWIC 1671", "…broadcasts of…"
        std::vector<twic::Issue> issues;            // TWIC's; empty for another source
        std::vector<std::string> urls;              // the files to download, one after the other
        std::vector<std::filesystem::path> wanted;  // where each goes
        std::string player;                         // chess.com's: the player followed, when followed
        std::vector<std::string> months;            // chess.com's: each file's month
        std::size_t index = 0;  // the database
        std::filesystem::path folder;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::shared_ptr<DownloadProgress> progress;
        std::future<std::vector<std::pair<std::filesystem::path, std::string>>> downloads;  // each file, or why not
        std::vector<std::filesystem::path> files;
        std::size_t next = 0;  // the next file to import
        bool downloaded  = false;
        bool importing   = false;
        uint64_t added = 0, duplicates = 0;
        study::GameId from = 0;  // the database's games before
    };
    std::optional<TwicAdd> twic_add_;
    struct TwicDone {
        std::string title;
        std::vector<int> numbers;
        std::size_t index = 0;
        std::string database;
        uint64_t added = 0, duplicates = 0;
        twic::Report report;
        std::string error;
    };
    std::optional<TwicDone> twic_done_;
    // A month of Lichess's broadcasts (#73): its page of files asked for once, read for both kinds of months.
    void drawBroadcastSheet();
    void drawFileAddProgress();  // the downloads and imports under way, with Cancel
    void startLichessMonths();
    // The downloads, then the imports; `target`, the sheet's choice of database, set to the new one when it is made.
    void startFileAdd(TwicAdd add, int database, const std::string& new_name, int* target);
    bool broadcast_sheet_ = false;
    int broadcast_pick_   = 0;
    int broadcast_target_ = -2;                                                           // as twic_target_
    std::optional<std::future<std::pair<std::string, std::string>>> lichess_months_job_;  // the page, or why not
    std::vector<lichess::Month> lichess_broadcast_months_, lichess_standard_months_;
    std::string lichess_months_error_;
    // A chess.com player's games (#74): their months asked for, then each month's PGN; a followed player under the
    // list of databases, looked at again when the user asks.
    void drawChesscomSheet();
    void startChesscomLookup(const std::string& player);
    void loadChesscom();
    void saveChesscom();
    bool chesscom_sheet_ = false;
    std::array<char, 64> chesscom_name_{};
    std::string chesscom_player_;  // the name looked up
    std::vector<std::string> chesscom_months_;
    std::optional<std::future<std::pair<std::vector<std::string>, std::string>>> chesscom_job_;
    std::string chesscom_error_;
    int chesscom_target_  = -2;
    bool chesscom_follow_ = true;
    bool chesscom_loaded_ = false;
    std::vector<chesscom::Followed> chesscom_followed_;
    // A month of Lichess's rated games (#72): read as it downloads, or from a file, filtered, into a database.
    void drawMonthSheet();
    void startMonthReading(const std::string& source);  // a URL, or a .pgn.zst on the disk
    void pollMonthReading();
    [[nodiscard]] bool monthReading() const { return month_reading_ && month_reading_->busy(); }
    bool month_sheet_ = false;
    bool lumbra_sheet_ = false;  // Lumbra's Gigabase (#75): downloaded on its site by the user, then imported
    void drawLumbraSheet();
    int month_pick_   = 0;
    int month_target_ = -2;
    lichess::MonthFilter month_filter_;
    std::unique_ptr<lichess::MonthReading> month_reading_;
    struct MonthRun {
        std::string source, title;
        std::size_t index  = 0;
        study::GameId from = 0;
        std::filesystem::path state;
        lichess::MonthFilter filter;
    };
    std::optional<MonthRun> month_run_;
    std::string month_stopped_;  // why the last reading stopped, before its end
    // "Open the file at this line": a damaged game's text as the file has it, from the line it starts on, read in the
    // background, its move shown.
    struct FileLines {
        std::filesystem::path file;
        uint64_t line = 0;
        std::string move;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::future<std::vector<std::string>> reading;
        std::vector<std::string> lines;
        std::string error;
        bool ready = false;
    };
    std::optional<FileLines> db_lines_;
    void openFileLines(const std::filesystem::path& file, uint64_t line, std::string move);
    void closeFileLines();
    void drawFileLines();
    void drawEmptyDatabase(std::size_t index);  // a database of the user's with no game: what to put in it
    // The frame a name was given, or a ChessBase copy started, with Enter: not the list's Enter too.
    int db_named_frame_ = -1;
    void pollDatabases();  // each frame: an import's end said
    std::unique_ptr<DatabaseShelf> shelf_;
    int db_selected_ = -1;
    bool db_naming_  = false;  // a new database's name being typed
    std::array<char, 128> db_name_{};
    std::string db_message_;     // the last import's outcome
    std::string db_linked_key_;  // the linked sources listed: the tournaments, the PGN file opened
    // The chosen database's games (mockup M1, board 2), in place of the list of databases: a quick filter, a sort, the
    // view made in the background, the game chosen.
    void drawGameList();
    void openListedGame(std::size_t row);
    // The list a game on the board was opened from (mockup M2, board 4): the previous and next games of it
    // (Ctrl+Left, Ctrl+Right), opened at the same move when they pass through the position shown; Ctrl+S puts the
    // game back in its database, under its number.
    struct BoardOrigin {
        std::filesystem::path folder;  // the database's
        std::string name;
        bool linked = false;  // a tournament's or a file's: not written
        std::shared_ptr<study::View> view;  // none for a game kept from the session before: its list is not kept
        std::size_t row  = 0;
        study::GameId id = 0;  // its number in the database: Ctrl+S puts it back there
        std::string filter;  // the quick filter it was made with
    };
    std::optional<BoardOrigin> board_origin_;
    void openListRow(std::size_t row);  // another game of the same list, at the same position when it has it
    bool saveBoardToDatabase();         // false when the game is not a database's to write
    // Where a game that is not a database's goes when saved (M2, board 8): a menu under the Save button; Ctrl+Shift+S
    // opens it for any game, a copy then.
    bool save_menu_ = false;  // to open
    bool save_copy_ = false;
    void boardSaveMenu(bool copy);
    void drawSaveMenu(ImVec2 under);
    // One more game there, the game that database's from then on; a copy leaves the game as it was.
    bool saveBoardInto(std::size_t database, bool copy = false);
    bool saveBoardInPgnFile();  // in place of itself in the PGN file it was read from
    void fillBoardEco();        // its ECO code from its moves, when it has none
    // The games the board showed before (mockup M2, board 4; R11): Ctrl+Tab and Ctrl+Shift+Tab through them, the one
    // chosen opened when Ctrl is let go, each with its changes and its place.
    struct RecentGame {
        std::string title;
        std::string where;  // its database, its file, or nothing
        GameTree tree;
        int line = -1, ply = 0;
        bool chess960 = false;
        GameChanges changes;  // since it was opened or saved
        std::optional<BoardOrigin> origin;
        std::optional<PgnSource> pgn;  // the file it was read from, its list of games
        MoveNotes start_notes;
        std::vector<BoardPage::Snapshot> undo, redo;  // its changes, still undone and redone
        std::optional<lichess::SentRecord> sent;      // sent to a Lichess study (#79)
        GameChanges since_sent;
    };
    std::deque<RecentGame> recent_games_;       // the newest first
    int recent_pick_ = -1;                      // the one chosen while Ctrl+Tab is held; -1 when the list is closed
    void rememberBoardGame();                   // the board's game, before another replaces it
    // The board's game and the recent ones, kept in the profile (recent.json) from one session to the next (M0's
    // Resume where you were, Recently): each with its tree, its move, its changes not saved and its database's number;
    // not their undo history nor their list.
    void saveRecentGames() const;
    void loadRecentGames();
    void restoreRecentGame(std::size_t index);  // on the board again, as it was left, the one shown remembered
    // The recent game that is game `id` of the database in `folder` with changes not saved, if there is one.
    [[nodiscard]] std::optional<std::size_t> changedCopyOf(const std::filesystem::path& folder, uint32_t id) const;
    void drawRecentGames();  // the list while Ctrl is held, and the game chosen opened
    // The strip over the column (mockup M2, board 1): the list's previous and next games, the recent games, the
    // game's changes or where it was saved, Save.
    void drawBoardStrip();
    void drawRecentPopup();                      // the recent games listed by the strip's button, a click opening one
    void boardSave();                            // Ctrl+S: into its database (the Chess space), else into a file
    void discardBoardChanges();                  // back to the game as saved or opened
    static bool discardAside(RecentGame& game);  // the same for a recent game; false when its history cannot
    bool forget_board_game_ = false;             // the next game opened does not remember the one shown
    bool db_list_ = false;
    std::array<char, 256> db_filter_{};
    study::SortBy db_sort_ = study::SortBy::Number;
    bool db_descending_    = false;
    std::shared_ptr<study::View> db_view_ = std::make_shared<study::View>();  // shared with board_origin_
    std::string db_view_of_;  // what db_view_ shows: the database, its snapshot, the filter and the sort
    struct ViewJob {
        std::string of;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::chrono::steady_clock::time_point started;
        std::future<study::View> done;
    };
    std::optional<ViewJob> db_view_job_;
    double db_view_ms_  = 0;
    std::size_t db_row_ = 0;
    // The search panel beside the list (M1, board 3; Ctrl+F): its header fields, and the Board page's position, whose
    // games a search finds in the background. The list follows them.
    void drawSearchPanel(const std::shared_ptr<const study::Snapshot>& snapshot);
    [[nodiscard]] study::Filter listFilter() const;  // the quick filter's words with the panel's fields
    bool db_search_open_ = false;
    struct SearchFields {
        std::array<char, 64> player{}, opponent{}, event{};
        int side = 0;  // the player: 0 either colour, 1 White, 2 Black
        std::array<char, 5> year_from{}, year_to{};
        std::array<char, 4> eco_from{}, eco_to{};
        std::array<char, 5> elo_min{};
        char result   = 0;
        bool position = false;  // the games through the Board page's position, in the list's filter
        // Material: for White then Black, pawns, knights, bishops, rooks, queens: "" any, "2", or "0-2".
        std::array<std::array<char, 8>, 10> material{};
        int material_plies = 4;  // that long at least: an exchange under way is not a material
        // A pattern: on each square (a1 = 0), -1 anything, a man (side * 6 + kind: pawn, knight, bishop, rook,
        // queen, king) or kEmptySquare; and a side's pawns only those it shows.
        std::array<int8_t, 64> pattern = [] {
            std::array<int8_t, 64> squares{};
            squares.fill(-1);
            return squares;
        }();
        std::array<bool, 2> only_pawns{};
        bool damaged = false;      // only the games a move of which could not be read
        std::string position_fen;  // the position a kept search brought; empty: the Board page's
    };
    // The searches kept (M1, board 6): their criteria, for every database, in the profile's searches.json.
    struct KeptSearch {
        std::string name;
        SearchFields fields;
    };
    std::vector<KeptSearch> kept_searches_;
    bool keep_naming_ = false;  // the name of the search to keep being written
    std::array<char, 96> keep_name_{};
    int kept_renaming_ = -1;
    std::array<char, 96> kept_rename_{};
    void loadKeptSearches();
    void saveKeptSearches();
    [[nodiscard]] std::string keptSearchName(const SearchFields& fields) const;  // made of its criteria
    void applyKeptSearch(std::size_t index);
    [[nodiscard]] static nlohmann::json keptSearchJson(const KeptSearch& kept);
    [[nodiscard]] static KeptSearch keptSearchFrom(const nlohmann::json& json);
    static constexpr int8_t kEmptySquare = 12;
    SearchFields db_search_;
    int search_tab_    = 0;  // Headers, Position, Material, Pattern
    int pattern_brush_ = 5;  // the man a click puts on the pattern: a white king
    // A search of the database in the background (study::Search): the games it found, once it is done.
    struct SearchJob {
        std::string of;  // what it searches: the database, its snapshot, the query; empty for nothing
        std::unique_ptr<study::Search> search;
        double ms  = 0;  // how long it took
        bool ready = false;
        std::vector<study::GameId> ids;  // sorted
    };
    SearchJob db_position_job_, db_material_job_, db_pattern_job_;
    // The three searches made again when what they search changed: the position when the panel shows it or the
    // filter has it, the material and the pattern when they ask for something.
    void pollSearches(const std::shared_ptr<const study::Snapshot>& snapshot, const std::string& folder);
    [[nodiscard]] std::optional<study::MaterialQuery> materialQuery() const;
    [[nodiscard]] std::optional<study::PatternQuery> patternQuery() const;
    void drawOpeningsPage();  // the repertoires (the tournament's Openings tab is drawOpenings)
    // The repertoires (repertoires.hpp): the star of the Board page's tree, the Openings page (view_openings.cpp).
    RepertoireShelf& repertoires();
    std::unique_ptr<RepertoireShelf> repertoires_;
    struct StarUndo {
        std::size_t index = 0;
        GameTree lines;
        std::string said;
        double at = 0;
    };
    std::optional<StarUndo> star_undo_;  // the last star's change, undone by its toast
    struct CoverageJob {
        std::string of;
        std::future<study::Coverage> done;
    };
    std::optional<CoverageJob> coverage_job_;
    std::optional<study::Coverage> coverage_;
    std::string coverage_of_;
    int coverage_depth_ = 0;  // 0 the ends of the lines, 1 move 10, 2 move 15
    std::set<std::filesystem::path> coverage_left_;  // the databases the coverage is not measured on (folders)
    bool rep_naming_    = false;
    std::array<char, 64> rep_name_{};
    int rep_side_ = 0;  // the new repertoire's colour: 0 White, 1 Black
    // The Board page's line to a position: from the start (the repertoires' start), none when it starts elsewhere.
    [[nodiscard]] std::optional<std::vector<chess::Move>> boardPath() const;
    // A line of moves from the start (SAN) shown on the board, at its end, the tree beside it.
    void showLineOnBoard(const std::vector<std::string>& line, const std::string& title);

    // The repertoires' training (#76, mockup M11, board 1; training.hpp, view_training.cpp): the training card of the
    // Openings page, a session on the Board page whose column becomes the training's, "In progress" on the Home page
    // and Ctrl+K. Each repertoire's cards and memories, counted for the day.
    struct TrainingSummary {
        uint64_t revision = UINT64_MAX;  // the repertoire's when its cards were made
        std::vector<study::TrainingCard> cards;
        TrainingFile kept;
        study::TrainingCounts counts;
        int64_t counted = 0;   // when (seconds), counted again each minute
        int new_per_day = -1;  // the limit it was counted with
    };
    std::map<std::filesystem::path, TrainingSummary> training_summaries_;  // by the repertoire's file
    [[nodiscard]] const TrainingSummary& trainingSummary(std::size_t index);
    // The cards ordered by what the databases play (the walk, the games of each), made in the background, one
    // repertoire at a time: nullptr until they are ready.
    struct TrainingPlanned {
        std::string of;  // the repertoire's file and revision, the databases
        std::vector<study::TrainingCard> cards;
    };
    std::map<std::filesystem::path, TrainingPlanned> training_planned_;
    struct TrainingPlanning {
        std::filesystem::path file;
        std::string of;
        std::future<std::vector<study::TrainingCard>> cards;
    };
    std::optional<TrainingPlanning> training_planning_;
    [[nodiscard]] const std::vector<study::TrainingCard>* plannedCards(std::size_t index);
    // The databases whose tree is built and that the Openings page measures on; `key` names them and their state.
    [[nodiscard]] std::vector<study::TreeBase> trainingBases(std::string* key, std::vector<std::string>* names);
    [[nodiscard]] std::optional<std::size_t> repertoireOf(const std::filesystem::path& file);
    std::optional<TrainingSettings> training_settings_;
    TrainingSettings& trainingSettings();
    void saveTrainingSettings();
    bool training_limit_menu_ = false;  // the menu of the new positions a day, asked from Ctrl+K
    std::unique_ptr<TrainingRun> training_;
    void drawTrainingCard(std::size_t index);                       // on the Openings page
    [[nodiscard]] std::string trainingListLine(std::size_t index);  // "23 to review", for the list of repertoires
    // Why a session cannot start now, empty when it can.
    [[nodiscard]] std::string trainingLock() const;
    void startTraining(std::size_t index, TrainingRun::Kind kind);
    void pollTraining();      // each frame: the cards prepared, the session's first question
    void drawTrainingPage();  // the Board page during a session
    void drawTrainingBoard(float size);
    void drawTrainingColumn();
    void drawTrainingEnd();
    void trainingKeys();
    void trainingAnswer(chess::Move move);  // a move played on the board or typed
    void trainingGrade(bool right);         // the first answer to the card asked, kept
    void trainingDontKnow();
    void trainingRetry();
    void trainingNext();
    void trainingAdd();  // the move played added to the repertoire (Ctrl+Z takes it back)
    void trainingTakeBack();
    void finishTraining(bool stopped);
    void askTrainingCard();  // the board on the card asked, the opponent's move just played
    void showTrainingCard(const study::TrainingCard& card);  // its line on the board, the question's position
    void reviewMissed();                           // "Review them now": the missed moves in a session of their own
    void closeTraining(bool to_openings);          // the session left, the board given back
    void dropTraining();                           // another game came on the board: the session left where it is
    [[nodiscard]] bool trainingHidesAids() const;  // a question: the tree, the engines, the notes hidden
    void drawTrainingHome();                       // the Home page's "In progress": the positions to review
    [[nodiscard]] bool trainingDue();

    // The Training page (#80, mockup M12, board 1; view_training_page.cpp): its Today part (the day's review, the kinds
    // of exercises, the puzzles to start) and its Puzzles part (the database offline, the batch online, the results by
    // theme). Offered once on the Home page of a view that lacks it.
    int training_tab_            = 0;      // 0 Today, 1 Puzzles
    int training_offer_          = -1;     // the page offered once: -1 not known yet, 0 to offer, 1 done
    bool training_enter_puzzles_ = false;  // Enter starts the puzzles, not the review: a choice of theirs was clicked
    bool training_scroll_        = false;  // the page brought to its puzzles (Ctrl+K)
    void drawTraining();
    void drawTrainingToday();
    void drawTrainingPuzzles();
    void drawReviewCard(bool& enter);
    void drawPuzzleChooser(bool& enter);
    void drawPuzzleStoreCard();
    void drawOnlinePuzzles(bool& enter);
    void drawPuzzleThemes();   // the results by theme
    void drawTrainingOffer();  // the Home page: the Training page offered once to a view without it
    [[nodiscard]] bool trainingOffered() const;
    void answerTrainingOffer(bool add);
    // The day's review (review_queue.hpp): what each kind has due, "Review all" taking them one after the other.
    [[nodiscard]] ReviewDay reviewDay();
    [[nodiscard]] ReviewCount reviewCount(ReviewKind kind);
    // Its session of the cards due, on the Board page; false when it has none. A kind that plugs in adds its case.
    bool startReviewOf(ReviewKind kind, std::size_t repertoire = 0);
    void startReview(std::optional<ReviewKind> only = std::nullopt);
    struct ReviewStep {
        ReviewKind kind        = ReviewKind::Repertoire;
        std::size_t repertoire = 0;  // its index, for the repertoire's sessions
    };
    std::deque<ReviewStep> review_chain_;  // what "Review all" takes after the session shown
    bool review_running_ = false;          // the session shown is one of them: its end goes on, or back to Training
    bool nextReview();                     // the next one started; false when none is left
    [[nodiscard]] std::string reviewNextLabel();  // "Next: 4 missed puzzles"
    void drawReviewNext(bool enter);              // at a session's end: go on, or back to the Training page
    void drawReviewHome();                        // the Home page's "In progress": one count, a click reviews

    // Puzzles (#80; puzzles.hpp, lichess_puzzles.hpp, view_puzzles.cpp). Lichess's database offline, in the profile.
    std::unique_ptr<OfflinePuzzles> offline_puzzles_;
    std::string offline_puzzles_error_;  // why the last preparation stopped
    std::string offline_puzzles_note_;   // what Update found
    bool offline_puzzles_remove_ = false;
    std::future<EvalsSize> offline_puzzles_update_;
    std::chrono::steady_clock::time_point offline_puzzles_since_{};
    uint64_t offline_puzzles_from_ = 0;
    void startOfflinePuzzles(std::string source);
    void pollPuzzles();  // each frame: a preparation's end, Update's answer, the online batch's requests
    // The user's puzzles: their rating, their results, the puzzles missed (in the profile's puzzles/ folder).
    std::optional<PuzzleRecord> puzzle_record_;
    PuzzleRecord& puzzleRecord();
    [[nodiscard]] std::filesystem::path puzzlesFolder() const { return storage_.root() / "puzzles"; }
    [[nodiscard]] PuzzleRating puzzleRating();                     // the user's, or where it would start
    [[nodiscard]] std::optional<int> lichessPuzzleRating() const;  // the linked account's, when known
    // The page's choice: offline or online, the theme ("" every theme, else a theme's key or "opening:<key>"), the
    // difficulty.
    bool puzzles_online_ = false;
    std::string puzzles_theme_;
    PuzzleDifficulty puzzles_difficulty_ = PuzzleDifficulty::Normal;
    bool puzzles_themes_open_            = false;  // the list of every theme and opening
    std::array<char, 48> puzzles_filter_{};
    static constexpr std::size_t PUZZLE_SERIES = 20;
    // A series on the Board page.
    std::unique_ptr<PuzzleRun> puzzle_;
    [[nodiscard]] std::string puzzleLock() const;  // why a series cannot start, empty when it can
    void startPuzzles();                           // a series of the page's choice
    void startPuzzleRun(std::unique_ptr<PuzzleRun> run);
    bool startPuzzleReview();  // the missed puzzles due today; false when there are none
    void askPuzzle();          // the next puzzle of the queue, the opponent's move played
    void showPuzzlePosition(std::size_t index,
                            std::size_t played);  // the board at the question, `played` solution moves after
    void puzzleAnswer(chess::Move move);
    void puzzleGrade(bool win);  // the first answer to the puzzle asked, kept
    void puzzleDontKnow();
    void puzzleRetry();
    void puzzleSolutionStep();  // the solution, a move at a time
    void puzzleNext();
    void finishPuzzles(bool stopped);
    void reviewMissedPuzzles();  // "Review the 4 now": the puzzles missed in a series of their own
    void closePuzzles(bool to_training);
    void dropPuzzles();  // another game came on the board: the series left where it is
    void puzzleKeys();
    void drawPuzzlePage();
    void drawPuzzleBoard(float size);
    void drawPuzzleColumn();
    void drawPuzzleEnd();
    // Online (lichess_puzzles.hpp): the batch kept in the profile, its requests one at a time.
    std::optional<lichess::OnlineBatch> online_batch_;
    bool online_batch_loaded_ = false;
    lichess::OnlineBatch* onlineBatch();  // read from the profile once; null when none
    void saveOnlineBatch();
    [[nodiscard]] bool puzzleScopes() const;  // the token may read and write puzzles
    void askPuzzleScopes();
    std::future<lichess::Answer<lichess::PuzzleBatch>> online_ask_;
    std::future<lichess::Answer<lichess::PuzzleSolved>> online_send_;
    std::string online_error_;                              // why the last request gave nothing
    int online_status_ = 0;                                 // its HTTP status
    std::chrono::steady_clock::time_point online_retry_{};  // a send that failed tried again then
    bool online_start_after_ = false;                       // the batch, once it comes, starts a series
    // The last 30 days on Lichess, by theme (puzzle:read): asked once a session, when the online choice shows.
    std::future<lichess::Answer<lichess::PuzzleDashboard>> online_dashboard_job_;
    std::optional<lichess::PuzzleDashboard> online_dashboard_;
    bool online_dashboard_asked_ = false;
    void askOnlineBatch();
    void sendOnlineResults();

    // Your mistakes as exercises and guessing the moves (#81, mockup M12, board 2; mistakes.hpp, view_mistakes.cpp,
    // view_guess.cpp). The user's names (Settings › Your names, and the linked Lichess account's); the exercises their
    // reviewed games give, looked for in the background when the databases, the names or the reviews change; the latest
    // of their games without a review reviewed in the background; a session of exercises, or a game guessed, on the
    // Board page.
    std::vector<std::string> player_names_;  // settings.json "player_names"
    std::array<char, 96> player_name_input_{};
    [[nodiscard]] std::vector<std::string> userNames() const;  // the user's, with the linked Lichess account's
    void setPlayerNames(std::vector<std::string> names);
    void drawPlayerNames(const char* id);  // the names as chips, a field that adds one (Settings, the Training page)
    std::optional<MistakeRecord> mistake_record_;
    MistakeRecord& mistakeRecord();
    [[nodiscard]] std::filesystem::path mistakesFolder() const { return storage_.root() / "mistakes"; }
    [[nodiscard]] std::filesystem::path guessFolder() const { return storage_.root() / "guess"; }
    void saveMistakeRecord();
    struct MistakeScanJob {
        std::string of;
        std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
        std::future<MistakeScan> done;
    };
    std::optional<MistakeScanJob> mistake_scan_job_;
    std::string mistake_scan_of_;  // what the last scan looked at: the names, the databases, the reviews
    std::chrono::steady_clock::time_point mistake_scan_checked_{};
    bool mistake_scanned_       = false;  // a scan came back this session
    std::size_t mistake_games_  = 0;      // the user's games it found
    std::size_t mistake_judged_ = 0;      // of them, with a review
    std::vector<UnreviewedGame> mistake_unreviewed_;
    [[nodiscard]] std::string mistakeScanKey();
    void pollMistakes();  // each frame: the scan, the background reviews, a session's judge and engine
    // The exercises asked: those of the record but a position of a repertoire of the same side (trained on the
    // Openings page); of `kind` when it is given.
    [[nodiscard]] std::vector<const MistakeExercise*> mistakeExercises(const std::string* kind = nullptr);
    [[nodiscard]] MistakeCounts mistakeDayCounts(const std::string* kind = nullptr);
    std::string mistake_kind_;     // the kind chosen on the page ("" every kind)
    int mistake_keys_frame_ = -1;  // the frame the repertoires' positions were gathered in, by side
    std::unordered_set<uint64_t> mistake_repertoire_keys_[2];
    // The user's latest games without a review, reviewed one after the other in the background as the Board page
    // reviews a game: its engine, its processes beside the tournaments, its passes; their exercises come game by game.
    struct MistakeReviews {
        std::vector<UnreviewedGame> games;
        std::size_t done = 0;
        std::unique_ptr<ReviewRunner> runner;
        ReviewRunner::State state;
        uint64_t version = 0;
        std::chrono::steady_clock::time_point started, game_started;
        double plies_done = 0, seconds = 0;  // what the games done took, for the time left
        int processes = 1;
        std::string engine;
        std::string error;
    };
    std::optional<MistakeReviews> mistake_reviews_;
    void startMistakeReviews();
    void cancelMistakeReviews();
    void startMistakeReview();  // the next game's
    // A session on the Board page.
    std::unique_ptr<MistakeRun> mistake_;
    [[nodiscard]] std::string mistakeLock() const;           // why a session cannot start, empty when it can
    bool startMistakes(bool review, bool new_only = false);  // the page's choice; false when nothing is to be asked
    void startMistakeRun(std::unique_ptr<MistakeRun> run);
    void askMistake();
    void showMistakePosition(const MistakeExercise& e, std::optional<chess::Move> then = std::nullopt);
    void mistakeAnswer(chess::Move move);
    void mistakeJudged();  // the search after the answer is back: right or wrong
    void mistakeGrade(bool right);
    void mistakeDontKnow();
    void mistakeRetry();
    void mistakeNext();
    void finishMistakes(bool stopped);
    void reviewMissedMistakes();  // "Review them now": the missed ones again, nothing graded
    void closeMistakes(bool to_training);
    void dropMistakes();                             // another game came on the board: the session left where it is
    void openMistakeGame(const MistakeExercise& e);  // the game it comes from, at the mistake
    void mistakeKeys();
    void drawMistakePage();
    void drawMistakeBoard(float size);
    void drawMistakeColumn();
    void drawMistakeEnd();
    void drawTrainingMistakes();    // the Training page's part
    void drawMistakeReviewsCard();  // the games without a review, reviewed in the background
    // Guessing the moves: the Training page's part, a game on the Board page.
    int guess_source_  = -1;    // the database a game is drawn from (the shelf's index); -1 the first of the user's
    int guess_side_    = 0;     // 0 White, 1 Black
    int guess_elo_     = 0;     // both players at least
    std::array<char, 96> guess_words_{};  // the quick filter's words: players, event, an ECO code, a year
    // A game drawn at random from the database and the filter chosen, in the background (a large database's filter
    // takes a moment).
    struct GuessPick {
        std::optional<ReplayGame> game;
        bool chess960 = false;
        std::string title;
        std::filesystem::path folder;
        uint64_t id = 0;
        std::string error;  // "none": no game passes the filter; "short": none long enough
    };
    std::future<GuessPick> guess_pick_;
    chess::Color guess_pick_side_ = chess::Color::WHITE;
    std::optional<GuessRecord> guess_last_;
    bool guess_last_read_ = false;
    std::string guess_error_;
    std::unique_ptr<GuessRun> guess_;
    void drawTrainingGuess();
    void pollGuess();         // each frame: the theory looked for, the searches after the game's move and the guess
    bool startGuessRandom();  // a game of the chosen database and filter, drawn in the background
    void startGuessBoard();   // the Board page's game, from the position shown, for the side to move
    void startGuess(ReplayGame game, bool chess960, std::string title, chess::Color side, int from,
                    std::optional<std::pair<std::filesystem::path, uint64_t>> origin);
    void askGuess();  // the next question, or the end
    void guessAnswer(chess::Move move);
    void guessScored();   // both searches back: the points
    void guessAdvance();  // the game's move and the reply played, the next question
    void finishGuess(bool stopped);
    void closeGuess(bool to_training);
    void dropGuess();
    void guessKeys();
    void drawGuessPage();
    void drawGuessBoard(float size);
    void drawGuessColumn();
    void drawGuessEnd();

    // Endgames against the tablebases, mates, board vision (#82, mockup M12, board 3; endgames.hpp, vision.hpp,
    // syzygy_download.hpp; view_endgames.cpp, view_vision.cpp): the Training page's Endgames and Vision parts, a
    // session of each on the Board page, the endgames missed in the day's review.
    std::optional<EndgameRecord> endgame_record_;
    EndgameRecord& endgameRecord();
    [[nodiscard]] std::filesystem::path endgamesFolder() const { return storage_.root() / "endgames"; }
    void saveEndgameRecord();
    // The tables of the disk: the setup's folders (Settings, the tournaments'), with the profile's own once it holds
    // tables (the download); the most pieces they cover, read off the interface's thread.
    [[nodiscard]] std::string endgameTablesPaths();
    [[nodiscard]] int diskTablesPieces();
    std::string disk_tables_of_;  // the paths diskTablesPieces() checked itself, before the checks in the background
    int disk_tables_pieces_ = 0;
    [[nodiscard]] bool diskTablesLocked() const;  // a tournament plays in this process: its tables, not to be probed
    // Why the position cannot be played against the tables now, empty when it can: the tables it needs.
    [[nodiscard]] std::string endgameNeed(const std::string& fen);
    std::unique_ptr<SyzygyDownload> syzygy_download_;
    std::optional<SyzygyDownload::State> syzygy_state_;
    std::chrono::steady_clock::time_point syzygy_state_at_{};
    std::chrono::steady_clock::time_point syzygy_since_{};
    uint64_t syzygy_from_       = 0;
    uint64_t syzygy_generation_ = 0;
    std::string syzygy_error_;
    const SyzygyDownload::State& syzygyState();
    void startSyzygyDownload();
    void pollEndgames();        // each frame: the download, the tables' answers, the tables' move, a blindfold game
    std::string endgame_need_;  // the endgame whose tables are missing: the card says what it needs
    int endgame_tables_choice_ = 0;      // 0 download the tables, 1 a folder already there, 2 Lichess's online
    int endgame_selected_      = 0;      // the catalogue's endgame the arrows move over
    bool endgame_keys_         = false;  // the arrows and Enter go to the catalogue: one of its endgames was clicked
    bool endgame_scroll_mates_ = false;  // the page brought to its mates (the Today part's card)
    std::string endgame_error_;
    void drawTrainingEndgames();
    void drawEndgameCatalogue(bool& enter);
    void drawEndgameTablesCard(bool& enter);  // the tables missing: download them, a folder, Lichess's online
    void drawSyzygyDownloadCard();
    void drawEndgamesFromGames();
    void drawMatesCard();
    [[nodiscard]] std::vector<EndgameTask> gameEndgames();  // the user's mistakes in positions of 7 pieces or fewer
    [[nodiscard]] std::string endgameState(const EndgameEntry& entry);  // "done in 9 (the tables: 7)", "5 pieces"…
    std::unique_ptr<EndgameRun> endgame_;
    // The questions to Lichess of a session left, cancelled, ending out of sight (a future of std::async waits for its
    // task when it goes).
    std::vector<std::future<lichess::Answer<lichess::Tablebase>>> endgame_left_;
    std::vector<std::future<lichess::Answer<std::vector<std::string>>>> endgame_lines_left_;
    void leaveEndgameQuestions();
    [[nodiscard]] std::string endgameLock() const;  // why a session cannot start, empty when it can
    bool startEndgame(const EndgameEntry& entry);
    bool startEndgameReview();
    bool startGameEndgames();
    void startEndgameRun(std::unique_ptr<EndgameRun> run);
    void beginEndgameTask();
    void showEndgamePosition();  // the task's start and its moves on the board
    void askEndgameTables();     // the tables' answer for the board's position: the disk's at once, Lichess's coming
    void endgameAnswered();      // an answer came: a move kept judged, the tables' move
    void endgameAnswer(chess::Move move);
    void endgameJudge(chess::Move move);
    void endgameTablesMove();
    void endgameTakeBack();
    void endgameRestart();
    void endgameContinue();
    void endgameSeeWin();
    void endgameNext();
    void endgameEnded(EndgameOutcome outcome);
    void finishEndgames(bool stopped);
    void closeEndgame(bool to_training);
    void dropEndgame();
    void endgameKeys();
    void drawEndgamePage();
    void drawEndgameBoard(float size);
    void drawEndgameColumn();
    void drawEndgameEnd();
    // Board vision.
    std::optional<VisionRecord> vision_record_;
    VisionRecord& visionRecord();
    void saveVisionRecord();
    CoordMode vision_mode_ = CoordMode::Find;
    CoordSide vision_side_ = CoordSide::White;
    Hidden vision_hidden_  = Hidden::All;
    int vision_colour_     = 0;  // blindfold: 0 White, 1 Black, 2 at random; its engine is the Play page's (#83)
    int vision_enter_      = 0;  // what Enter starts on the page: 0 the coordinates, 1 blindfold, 2 the lines
    std::string vision_error_;
    std::unique_ptr<VisionRun> vision_;
    void drawTrainingVision();
    void startCoordinates();
    void startBlindfold();
    void startHeadLines();
    void startVisionRun(std::unique_ptr<VisionRun> run);
    void visionAnswer(chess::Move move);
    void visionSquare(chess::Square square);  // a square clicked: the coordinates, "Where is…?"
    void visionNextSquare();
    void finishVision(bool stopped);
    void closeVision(bool to_training);
    void dropVision();
    void pollVision();
    void visionKeys();
    void drawVisionPage();
    void drawVisionBoard(float size);
    void drawVisionColumn();

    // Playing against an engine (#83, mockup M12, board 4; engine_play.hpp, view_play.cpp): the Play page's choice
    // (play.json: the engine, its strength, its openings, the user's colour and clock, a handicap); the game on the
    // Board page through board_.match, its aids hidden until its end ("Show them" marks it), its takebacks and hints
    // counted; at its end, the game saved into "My games against engines" with its settings in its tags, its review,
    // a rematch, the user's games against that engine at that level (play/games.jsonl). A game RookStudio closed
    // during is kept (play/current.json) and comes back at the next launch, its clocks waiting for Resume.
    std::optional<PlaySettings> play_settings_;
    PlaySettings& playSettings();
    void savePlaySettings();
    [[nodiscard]] std::filesystem::path playFolder() const { return storage_.root() / "play"; }
    std::optional<std::vector<PlayedGame>> played_games_;
    const std::vector<PlayedGame>& playedGames();
    struct EngineGame {
        KeptGame kept;              // as it was started: its settings, engine, strength, colour, start and clocks
        std::string openings_text;  // "your repertoire “Najdorf”"
        bool waiting   = false;     // kept when RookStudio closed: on the board, its clocks waiting for Resume
        int hints_used = 0;
        std::unique_ptr<Analyser> hint;  // a hint's search, by the analysis engine
        AnalysisPosition hint_position;
        std::optional<chess::Move> hint_move;  // shown until the game moves on
        int hint_ply     = -1;
        bool aids        = false;  // shown during the game: the game is marked "with aids"
        double resign_at = -100;   // "Resign…" clicked: confirmed by Enter, or a second click, a few seconds long
        uint64_t kept_at = 0;      // the match state's version last written to play/current.json
        bool over        = false;
        std::string result, reason;  // as it ended
        std::string saved_in;        // the database it went into
        std::string save_error;      // why it did not, and where it went instead
        bool reviewed = false;       // Review it clicked
        PlayedGame record;
        ReplayGame final_game;              // as it ended, its tags written
        std::optional<BoardOrigin> origin;  // where it was saved
        std::string me;                     // the user's name in it
    };
    std::unique_ptr<EngineGame> engine_game_;
    std::string play_error_;  // why the last game could not start
    int play_offer_ = -1;     // the Play page offered to a view made before it: -1 not known yet, 0 to offer, 1 done
    void drawPlay();
    void drawPlayEngine();
    void drawPlayOpponent(PlaySettings& s, bool& changed);
    void drawPlayStrength(PlaySettings& s, const EngineEntry& engine, bool& changed);
    void drawPlayOpenings(PlaySettings& s, bool& changed);
    void drawPlayYou(PlaySettings& s, bool& changed);
    void drawPlayHandicap(PlaySettings& s, bool& changed);
    [[nodiscard]] std::string playLock() const;  // why a game cannot start now, empty when it can
    [[nodiscard]] std::string playStartFen(const PlaySettings& s, chess::Color engine_side, std::string* why);
    // The engine's openings as the choice says: their source, what the game says of them, their tag; none for the
    // engine's own, or when the source is not there (`note` says so).
    OpeningBook::Source openingSource(const PlaySettings& s, chess::Color engine_side, std::string& text,
                                      std::string& tag, std::string& note, uint64_t& least, bool build = true);
    bool startEngineGame(std::optional<chess::Color> colour = std::nullopt);  // a rematch's colour, else the choice's
    // The game's setup: its players, clocks, start, the engine's nodes and openings, its tags; what the game says of
    // the openings and their tag, when asked.
    BoardMatchSetup engineGameSetup(const KeptGame& kept, const EngineEntry& engine, const std::string& me,
                                    std::string* openings_text = nullptr, std::string* openings_tag = nullptr);
    void takeBoardFor(const BoardMatchSetup& setup, bool remember = true);  // the board follows the game started
    void pollEngineGame();
    void finishEngineGame();
    void saveEngineGame(ReplayGame& game);               // into "My games against engines"
    void endEngineGame(ReplayGame game, int takebacks);  // over: its tags, saved, recorded
    void stopKeptEngineGame();                           // a game kept, ended without a result
    void showEngineGameOnBoard();                        // the game ended, on the board again
    [[nodiscard]] bool engineGameRunning() const;        // being played, or kept waiting for Resume
    [[nodiscard]] bool engineGameHidesAids() const;      // its aids hidden: until its end, or Show them
    void showEngineGameAids();
    void engineGameTakeBack();
    void engineGameHint();
    void engineGameResign();
    bool engineGameKeys();    // true when the keys were the game's
    void backToEngineGame();  // the board on the game again, after another was opened there
    void keepEngineGame(bool closing);
    void loadKeptEngineGame();  // at launch
    void resumeEngineGame();
    void closeEngineGame();  // the end's card closed: the game stays on the board as any game
    void drawEngineGameCard();
    void drawEngineGameEnd();
    void drawEngineGameBanner();  // in place of a tab hidden during the game
    [[nodiscard]] std::string engineGameTitle() const;
    [[nodiscard]] bool playOffered() const;
    void answerPlayOffer(bool add);
    void drawPlayOffer();

    // Theory studies (#77, mockup M11, board 2; theory_shelf.hpp, view_theory.cpp): annotated PGN files read where they
    // are, Lichess studies' PGN, the repertoires' comments. The Openings page's Theory part, the Tree tab's Theory
    // column and "What your studies say" under its moves, the Notation tab's "Theory · N".
    std::unique_ptr<TheoryShelf> theory_;
    TheoryShelf& theory();          // made when first needed, its studies read in the background
    bool openings_theory_ = false;  // the Openings page shows its Theory part
    int theory_chosen_    = 0;      // the study the part shows
    struct TheoryUndo {
        TheoryShelf::Removed removed;
        std::string said;
        double at = 0;
    };
    std::optional<TheoryUndo> theory_undo_;  // a study taken out, put back by its toast
    std::string theory_said_;                // what the last addition said
    double theory_said_at_ = -100;
    // What the notes say at the board's position, made again when it, the board's line or the notes change.
    struct TheoryHere {
        std::string of;
        TheoryAt at;
        std::map<std::string, chess::Move> moves;  // the moves the notes are on, as the board plays them
    };
    TheoryHere theory_here_;
    // The marks of the note under the mouse in the Tree tab, on the board while it shows their position.
    struct TheoryMarks {
        uint64_t position = 0;  // the board's hash()
        std::vector<BoardMark> arrows, squares;
        int frame = 0;  // drawn the frame they were set and the next only
    };
    std::optional<TheoryMarks> theory_marks_;
    std::chrono::steady_clock::time_point theory_synced_{};
    void pollTheory();                            // each frame: the readings, the files changed, the repertoires
    [[nodiscard]] const TheoryAt& boardTheory();  // at the Board page's position
    void drawOpeningsParts();                     // "Repertoires · Theory", in the Openings page's title line
    void drawTheoryPart();
    void drawTheoryStudy(std::size_t index, std::optional<std::size_t>& open, std::optional<std::size_t>& take_out);
    void drawTheoryUndo();
    void drawTreeTheoryCell(const std::string& san);  // a move's Theory column in the Tree tab
    // Under the Tree tab's moves, `height` high: the note under the mouse that is on a move gives it.
    std::optional<chess::Move> drawTreeTheory(float height);
    void drawTheoryChip();  // the Notation tab's, above its moves
    void addTheoryFile(const std::filesystem::path& file);
    void makeTheoryStudy(const std::string& id);  // a Lichess study's, read first when it never was on this machine
    void showTheoryPart(std::optional<std::size_t> chosen = std::nullopt);
    void openTheoryDialog();
    void openTheoryCopy(const TheorySource& source);  // its games on the Board, a copy
    void openTheoryNote(const TheoryFound& found);    // its game at its move, a copy

    // shell
    void drawShell();
    void drawRail(float width);
    // Games in progress: of the tournament shown (beside the live view), or of every tournament (the rail).
    [[nodiscard]] int gamesPlaying(bool all = false) const;

    // The home page as a control room (view_home.cpp): Pause all, the cores in use, what needs action first, the card
    // of the running tournament, the queue and when it all ends, the last tournaments finished with their result,
    // the health of the last 24 hours, then every tournament.
    void drawHome();
    void drawHomeHeader();      // the title, Pause all (or Resume all), Open a folder… and New tournament
    void drawGettingStarted();  // the steps of the guide's Quick start, until a first tournament is started
    // The machine's resources and who uses them: the cores of the running games against the hardware threads. The
    // memory and the GPUs join them once tournaments have a budget of them.
    [[nodiscard]] std::vector<home::ResourceBar> resourceBars();
    // The cores of the games being played: for each, the Threads of its engine that has the most (the two engines
    // think in turn).
    [[nodiscard]] int coresInUse();
    // The card of the running tournament, made from the run: one per tournament once several run at once.
    [[nodiscard]] home::RunCard runCard();
    void drawRunCards();
    // An engine that cannot start, the tournament an error stopped (with the fixes of its message, and Resume), a
    // drive too full: each with its fix. True when anything was shown.
    bool drawNeedsAction();
    // The last run ended by an error or an engine that stopped it, and the card saying so was not hidden.
    [[nodiscard]] bool stoppedByError() const;
    // The card of the tournament that can be resumed, with the error that stopped it when `error`.
    void drawInterrupted(bool error);
    void drawLastFinished();    // the last tournaments finished, their result, Relaunch with… and Open
    void drawAllTournaments();  // the table of every tournament, folded
    void pollDiskSpace();       // the free space of the tournaments' drives, asked on a worker thread now and then
    // Pause all: the running tournament paused and the queue held (it starts no tournament until Resume all).
    [[nodiscard]] bool canPauseAll() const;
    [[nodiscard]] bool allPaused() const;
    void pauseAll();
    void resumeAll();
    void drawNewMatch();
    // The setup of the new tournament page: its engines chosen, its SPRT bounds the wrong way round, an engine of it
    // being read. Start and Add to the queue follow them.
    [[nodiscard]] bool setupChosen() const;
    [[nodiscard]] bool setupBadSprt() const;
    [[nodiscard]] bool setupProbing() const;
    void drawWorkspace();
    void drawWorkspaceHeader();

    // The panels of the workspace (its views, docked in its dock space) and their layouts, see view_panels.cpp.
    [[nodiscard]] static ImGuiID workspaceDock();
    static void registerPanelSettings();  // what layout.ini keeps of them: which are open
    void loadPanels();                    // from layout.ini, once
    void drawWorkspaceDock();             // the dock space, under the header; the default layout if it has none
    void drawPanels();                    // every panel open, after the shell
    void drawPanelContent(const std::string& panel);
    void placeIfAsked(const std::string& panel);  // a panel floating alone in a layout built
    void drawLiveBadge();  // the number of games being played, on the tab (or the title) of the live view
    [[nodiscard]] bool panelAvailable(std::string_view panel) const;
    [[nodiscard]] bool panelOpen(std::string_view panel) const { return panels_open_.count(std::string(panel)) > 0; }
    void showPanel(const std::string& panel);  // opens it (docked again if it was closed) and brings it to the front
    void closePanel(const std::string& panel);
    void panelsChanged();  // panels_open_ changed: layout.ini saves it
    [[nodiscard]] std::vector<std::string> panelsAvailable() const;  // in the order of the menu and the palette
    void applyLayout(const layout::Layout& l);                       // on the next frame of the workspace
    bool applyLayout(const std::string& name);  // ready-made (by its key) or saved (by its name); false if none
    [[nodiscard]] layout::Layout currentLayout() const;
    bool saveLayoutAs(const std::string& name);  // trimmed; false without a name
    void deleteLayout(const std::string& name);
    void drawLayoutMenu();  // the Layout button of the tournament header
    void drawSaveLayout();  // Save the layout as…: its name

    void drawSettings();
    void drawRatings();
    void drawRatingFilters();
    void drawRatingList();
    void drawLibraryMatches();  // the players of the PGN files taken for library engines
    void drawPgnPlayers();      // the window where the user says which library engine a PGN player is
    void drawEngineCard();
    void drawHeadToHead();
    void drawProgress();
    void refreshRatings();
    [[nodiscard]] tour::GameFilter ratingFilter() const;
    [[nodiscard]] tour::RatingListInfo ratingInfo() const;  // rating_info_ with today's date and the version
    void showEngineCard(const std::string& name);
    void openGame(const tour::DbGame& game);  // opens its tournament on the Games tab, the game selected
    void applyThemeSetting();
    void handleZoomKeys();
    void refreshTournaments();
    void openTournament(const std::filesystem::path& dir);
    void importLegacyRuns();

    // views
    void drawSetup(bool two_columns, bool with_summary);
    void drawLive();
    void refreshSlot(int slot);
    // The parts of the game followed, each also a panel of its own: shown beside the mosaic when their panel is
    // closed, in a detached board all together.
    enum LivePart : unsigned {
        LiveBoard    = 1,
        LivePlayers  = 2,
        LiveSearches = 4,
        LiveMoves    = 8,
        LiveChart    = 16,
        LiveAll      = 31
    };
    static constexpr std::array<std::pair<unsigned, const char*>, 5> LIVE_PARTS{
        {{LiveBoard, layout::panel::BOARD},
         {LivePlayers, layout::panel::PLAYERS},
         {LiveSearches, layout::panel::SEARCHES},
         {LiveMoves, layout::panel::MOVES},
         {LiveChart, layout::panel::CHART}}};
    void drawLiveGame(SlotView& view, const char* id, unsigned parts, int slot, bool allow_detach = true);
    void drawLiveBoard(SlotView& view, float size);
    void drawLiveBoardFitted(SlotView& view);
    // The keys through the moves (navKeys) for the game of `view`, the window drawn having the focus.
    void liveNavKeys(SlotView& view);
    // The room the buttons take under a live board: none when the setting hides them.
    [[nodiscard]] float liveNavHeight() const;
    // The height its line took (in a card), `pv_room` at most.
    float drawLivePlayer(SlotView& view, chess::Color color, bool search, float pv_room = FLT_MAX);
    // The same in a large panel: a card with the logo as high as the name and the clock, and its search figure by
    // figure with the whole line, as much of it as `pv_room` holds.
    float drawLivePlayerCard(SlotView& view, chess::Color color, bool search, float width, float pv_room);
    void drawLiveGameLine(SlotView& view, int slot, bool allow_detach);
    // Above the moves: the game's opening, with where its book ended, and the tables' verdict once they know the
    // position shown.
    void drawLiveFacts(SlotView& view);
    void drawLiveMoves(SlotView& view, float height);
    // The score of a move or a search, from its mover's side, as the Live tab shows it: the mover's, or White's.
    [[nodiscard]] std::optional<fastchess::Score> liveScore(std::optional<fastchess::Score> score,
                                                            chess::Color mover) const;
    void drawLiveChart(SlotView& view);
    void drawLiveSide(SlotView& view, unsigned parts, int slot, bool allow_detach);
    [[nodiscard]] unsigned livePartsHere() const;  // the parts whose panel is closed
    void drawLivePart(unsigned part);              // in its own panel
    void drawDetachedBoard(int slot);
    void drawMosaic(int cols, float card);

    // Big screen (view_big_screen.cpp): one game filling the main window, or a detached board's window.
    struct BigView {
        int slot  = 0;                                   // the game shown
        int panel = -1;                                  // a detached board's: its slot; -1 in the main window
        std::chrono::steady_clock::time_point turned{};  // when it last turned to a game, to follow them
        bool controls_hovered = false;
    };
    void startBigScreen(int slot);  // in the main window
    void leaveBigScreen();
    // A detached board shown as a big screen in its own window, or, docked in the main one, the main window's.
    void startBigBoard(int slot);
    void leaveBigBoard(int slot);
    void toggleFullscreen(SDL_Window* window);
    void handleBigScreenKeys();  // F11, Esc, the arrows, + and -; the mouse moving shows the controls
    void drawBigScreen();        // the main window's, instead of the pages
    void drawBigGame(BigView& big, bool main_window);
    void drawBigControls(BigView& big, bool main_window, ImVec2 origin, int slots);
    void drawGames();
    void drawOpenings();
    void refreshOpenings();
    [[nodiscard]] std::vector<const tour::OpeningStats*> shownOpenings() const;  // filtered, in the shown order
    void drawResults();
    void drawCharts();
    // The search of the participants given by their indexes, a row each, by default in that order (a click on a column
    // sorts by it): in the overview of a match and, in the order of the standings, of a tournament.
    void drawSearchTable(const std::vector<std::size_t>& participants);
    void drawEngines();
    void drawConsole();
    // The engine console (view_engine_console.cpp), in the card of its engine.
    void openEngineConsole(int index);
    void closeEngineConsole();
    void pollEngineConsole();  // every frame, shown or not
    void drawEngineConsole(const EngineEntry& e);
    void saveConsoleLog(const std::string& path);

    // The Board page (view_board.cpp): a game or a position, and its analysis.
    void drawBoardPage();
    void drawBoardColumn(float size);  // the board, the buttons under it
    void drawPromotionPopup(const chess::Board& board);  // the piece a pawn becomes, once its move is chosen
    void drawBoardAnalysis();
    void drawBoardMoves(float height);
    void drawBoardTable();   // the Table tab: each move with its engine's score, depth and time
    void drawBoardReview();  // the Review tab: each side's accuracy and figures, the key moments, who saw them
    [[nodiscard]] std::string boardMoveLabel(int ply) const;  // "12…Qa5", the game's move leading to `ply`
    // A move's SAN as the board's space writes it: in the Chess space, the reader's piece letters, as the assistant's
    // sentences have them ("27.Txe8" in French); in the Engines space, as the PGN has it.
    [[nodiscard]] std::string boardSan(std::string san) const;
    // The line shown on the board: the game (-1) or a variation, at its position `ply`.
    void showBoardLine(int line, int ply);
    // The position of the game the board's position is on, or leaves it at (a variation).
    [[nodiscard]] int boardMainPly() const;
    // The players above and below the board: name, evaluation of its last move, time, clock at the move shown.
    [[nodiscard]] bool boardStrips() const;  // the game has players to show
    void updateBoardTablebase();             // the tablebases' result of each position of the board's game
    [[nodiscard]] int boardTablebasePly() const;  // the first position they know, -1 none
    void drawPlayerStrip(chess::Color side, float size, const std::vector<std::optional<int64_t>>& clocks);
    void drawBoardGraph(float height);  // the game graph card, under the moves
    [[nodiscard]] float boardGraphHeight() const;
    [[nodiscard]] std::string boardGraphTip(int ply) const;  // the graph's tooltip at a position
    void pollBoard();  // every frame: the analysis follows the board while the page is shown
    void loadBoardSettings();
    void saveBoardSettings();
    // Opens `game` on the Board page after `ply` moves, the page and tab shown kept to go back to; `analyse` starts
    // the analysis. `title` says what the game is.
    void openOnBoard(ReplayGame game, int ply, bool chess960, std::string title, bool analyse);
    void openPositionOnBoard(const std::string& fen, bool chess960, std::string title, bool analyse);
    void setBoardAnalysis(bool on);
    void setBoardThreat(bool on);  // X: the opponent's best move were it to move again
    // Ends the analysis engines and the threat engine, the analysis and the threat off: stopped, an engine stays
    // started, its hash table (a GPU's memory) held, for the next search.
    void unloadBoardEngines();
    [[nodiscard]] std::optional<AnalysisPosition> threatPosition() const;  // the board's, the turn passed
    // The analysis's engine's settings as its process gets them: in the study Board, its share of the threads; the
    // threat's, one line.
    [[nodiscard]] AnalysisSettings firstEngineSettings() const;
    [[nodiscard]] AnalysisSettings threatSettings() const;
    // Several engines side by side in the study Board's Analysis tab (#78, mockup M11, board 3;
    // view_board_engines.cpp): each a card with its colour, its arrow on the board, the threads shared between them.
    void drawSideBySide();
    struct SideGesture {  // asked of an engine's card, done once the cards are drawn
        std::optional<std::pair<std::size_t, std::size_t>> move;  // from, to (made first: to 0)
        std::optional<std::size_t> remove;
    };
    void drawSideEngineCard(std::size_t index, const std::vector<SideEngine>& list, const std::vector<int>& threads,
                            const chess::Board& board, const AnalysisPosition& position, SideGesture& gesture);
    // Its menu: lines, threads, hash, made first, removed. False when it closes.
    bool drawSideEngineMenu(std::size_t index, const std::vector<int>& threads, SideGesture& gesture);
    void drawAddEngineMenu();                     // "+ An engine": the library's engines, and the engine picker
    void drawAnalysisThreadsPopup();              // the analysis's threads, from its chip
    bool addSideEngine(const std::string& name);  // false when it cannot be (four already, locked, not there)
    void removeSideEngine(std::size_t index);
    void moveSideEngine(std::size_t from, std::size_t to);    // dragged, or made first
    void firstEngineChanged(const std::string& before);       // the threat, the review's curve follow the new first
    [[nodiscard]] Analyser* sideAnalyser(std::size_t index);  // the process of the engine at that place, 0 the first
    [[nodiscard]] std::vector<int> sideThreads() const;       // each engine's threads now, in the list's order
    // The analysis's threads, without the cores of the tournaments running on this computer (they keep them).
    [[nodiscard]] int analysisTotal() const;
    [[nodiscard]] int tournamentCores() const;  // what the tournaments running here keep: their games at once
    [[nodiscard]] const EngineEntry* engineNamed(const std::string& name) const;  // the library's, null when none
    // The engines' arrows on the board (one per move, its engines counted), their verdicts in words: the engines
    // whose search is of `position`.
    [[nodiscard]] std::vector<BoardArrow> sideBySideArrows(const chess::Board& board, const AnalysisPosition& position);
    [[nodiscard]] std::vector<EngineVerdict> sideVerdicts(const chess::Board& board, const AnalysisPosition& position);
    void keepSideTrails(const AnalysisPosition& position);  // each engine's evaluation of the game's position shown
    // The colour of an analysis of the game (the graph's curves, its arrows): in the study Board, an engine of the
    // analysis's own; else analysisColour's.
    [[nodiscard]] ImU32 reviewColour(std::size_t index, const std::string& engine) const;
    bool side_menu_open_    = false;                     // "+ An engine" opens at the next frame (Ctrl+K)
    int side_menu_pick_     = 0;                         // the engine Enter adds, among those the menu offers
    int side_engine_menu_   = -1;                        // that engine's menu opens at the next frame (Ctrl+K)
    bool side_threads_open_ = false;                     // the analysis's threads open at the next frame (Ctrl+K)
    std::set<std::string> side_unfolded_;                // the engines whose other lines are shown
    std::map<std::string, std::string> side_installed_;  // "installed …" by engine, read when the menu opens
    // Key A, pressed for a board now: once a frame, not while a text field or a popup has the keys.
    [[nodiscard]] bool boardKey();
    [[nodiscard]] std::string boardFen();  // the position of the Board page
    void boardPlay(chess::Move move);
    // A move the user makes on the board or types. In the Chess space, where the game went another way, it starts a
    // variation (boardPlay), unless Shift is held: it replaces the moves after; or Ctrl, or the setting: asked about.
    void boardPlayAsked(chess::Move move);
    // Lichess (mockup M4): the account, a player's games, the broadcasts, the online services of the analysis.
    LichessSettings lichess_;
    std::optional<lichess::Account> lichess_account_;
    std::string lichess_error_;  // why the last link failed
    struct LichessLink {
        std::shared_ptr<std::atomic<bool>> cancel;
        std::shared_ptr<std::string> url;  // Lichess's page, as given to the browser
        std::shared_ptr<std::atomic<bool>> opened;
        std::future<lichess::Answer<lichess::Account>> result;
        std::string previous_token;                          // the token a link made again replaces: revoked once kept
        std::optional<lichess::BroadcastEntry> then_follow;  // the round to follow once the scope is granted
    };
    std::unique_ptr<LichessLink> lichess_link_;
    std::future<std::string> lichess_unlink_;
    std::vector<std::future<std::string>> lichess_revokes_;  // tokens replaced by a link made again
    // A scope a feature needs and the token lacks (#45): asked on the Lichess page, the M4 way, before going on.
    struct LichessScopeAsk {
        std::string scope;
        std::optional<lichess::BroadcastEntry> follow;  // the round to follow with it (#44)
        std::string error;                              // why the last try gave nothing
        bool players = false;                           // the followed players' mosaic to open with it (#50)
        bool search  = false;                           // a search among the broadcasts to make with it (#55)
    };
    std::optional<LichessScopeAsk> lichess_scope_ask_;
    void drawLichessScopeAsk();
    // Chosen players followed live (#50, mockup M8, board "Prep"): those the account follows (follow:read, asked at the
    // first "Their games in the mosaic") and those added by name; their status asked while the Lichess page shows them,
    // their games in the mosaic, eight at most.
    std::vector<std::string> lichess_following_;  // the account's, read once a session
    std::future<lichess::Answer<std::vector<std::string>>> lichess_following_job_;
    std::string lichess_following_token_;  // the token they were read with
    std::vector<lichess::PlayerStatus> lichess_statuses_;
    std::future<lichess::Answer<std::vector<lichess::PlayerStatus>>> lichess_status_job_;
    std::chrono::steady_clock::time_point lichess_status_asked_{};
    std::string lichess_status_error_;
    int lichess_players_drawn_        = -1000;  // the frame that drew their card last
    bool lichess_players_then_mosaic_ = false;  // the permission granted, their mosaic opens
    std::unique_ptr<lichess::PlayersFollower> players_follower_;
    std::array<char, 40> lichess_add_player_{};
    [[nodiscard]] std::vector<std::string> followedPlayers() const;  // once each, never the user's own account
    void pollFollowedPlayers();
    void drawFollowedPlayers();  // the Lichess page's card
    void openPlayersMosaic(bool asked = false);
    // A tournament on the user's own Lichess broadcast (#51, mockup M8, board "Moteurs"): the sheet that makes it; the
    // account's token once it may create broadcasts (study:write, asked then), else the one pasted on the Broadcast
    // page.
    bool cast_sheet_ = false;
    std::array<char, 96> cast_name_{};
    bool cast_public_     = false;
    bool cast_after_link_ = false;  // the permission granted, the broadcast made
    [[nodiscard]] std::string broadcastToken() const;
    void openCastSheet();
    void startCast();
    void pollCast();
    void drawCastSheet();
    void drawCastBanner();
    // Preparing a game against an opponent (#52, mockup M8, board "Prep"): their games of the last year into "Lichess ·
    // opponent", their level; then, from that database's tree, what they play and the user's repertoires against it.
    struct Prep {
        std::string opponent;
        std::size_t index = SIZE_MAX;  // their database
        std::shared_ptr<std::atomic<bool>> cancel;
        std::shared_ptr<lichess::SyncProgress> progress;
        std::future<lichess::SyncReport> sync;
        std::string error;
        std::future<lichess::Answer<lichess::Profile>> profile_job;
        std::optional<lichess::Profile> profile;
        std::string profile_error;
        std::future<PrepReport> report_job;
        std::optional<PrepReport> report;
        uint64_t reported = UINT64_MAX;  // the snapshot and tree the report was made from
    };
    std::optional<Prep> prep_;
    std::array<char, 40> prep_name_{};
    bool prep_focus_     = false;
    bool prep_prefilled_ = false;
    void startPrep(const std::string& opponent);
    void pollPrep();
    void drawPrepCard();
    void openPrepLine(const std::vector<std::string>& line, const std::string& move);
    // A Lichess link pasted (#47, mockup M8, board "Lien"): its card, then its games coming into a database.
    struct LinkCard {
        lichess::Link link;
        std::string url;       // as pasted
        bool to_base = false;  // into a database rather than on the board
        bool follow  = false;  // a round followed live
        int base     = -1;     // the database chosen; -1: the one by default, made when missing
        std::future<lichess::Answer<std::string>> pgn;  // a game's or a chapter's, asked at once
        std::optional<lichess::Answer<std::string>> got;
    };
    std::optional<LinkCard> link_card_;
    struct LinkJob {
        lichess::Link link;
        std::size_t index = 0;  // the database's
        std::string name;
        std::shared_ptr<std::atomic<uint64_t>> games = std::make_shared<std::atomic<uint64_t>>(0);
        std::shared_ptr<std::atomic<bool>> cancel    = std::make_shared<std::atomic<bool>>(false);
        std::future<lichess::LinkImport> result;
        bool done = false;
        std::string said;  // its outcome
    };
    std::optional<LinkJob> link_job_;
    // The linked account on the Home page (#46, mockup M8): its profile, kept a day; a speed's figures.
    std::optional<lichess::Profile> lichess_profile_;
    bool lichess_profile_loaded_ = false;
    std::future<lichess::Answer<lichess::Profile>> lichess_profile_job_;
    std::string lichess_profile_error_;
    std::chrono::steady_clock::time_point lichess_profile_asked_{};
    int lichess_speed_ = 1;  // the speed whose figures show: blitz
    std::map<std::string, lichess::Answer<lichess::PerfFigures>> lichess_figures_;
    std::future<std::pair<std::string, lichess::Answer<lichess::PerfFigures>>> lichess_figures_job_;
    void pollLichessProfile();
    void drawLichessAccountPanel();
    // Fair play (#48, mockup M8, board "Fairplay"): the user's games in progress on Lichess. A live one locks every
    // aid on every position; a correspondence game the engine, the cloud, the tables and the assistant on the whole of
    // a line that passes through its position.
    std::vector<lichess::PlayingGame> lichess_playing_;
    std::future<lichess::Answer<std::vector<lichess::PlayingGame>>> lichess_playing_job_;
    std::chrono::steady_clock::time_point lichess_playing_asked_{};
    bool lichess_playing_ask_now_   = false;  // the Home page showed: looked at again if it was a while ago
    int lichess_playing_home_frame_ = -10;    // the frame that drew the Home page's list last
    std::unique_ptr<lichess::EventListener> lichess_events_;  // with the right to read challenges
    std::string lichess_playing_token_;                       // the token the games were asked with
    void pollLichessPlaying();
    [[nodiscard]] const lichess::PlayingGame* liveGameOn() const;
    // The first ply of `line`, up to `last`, whose position is a correspondence game's now, -1 when none; `game` set to
    // that game.
    [[nodiscard]] int correspondencePly(const ReplayGame& line, bool chess960, int last,
                                        const lichess::PlayingGame** game = nullptr) const;
    // The same of any game being played, live ones too.
    [[nodiscard]] int playingPly(const ReplayGame& line, bool chess960, int last, const lichess::PlayingGame** game,
                                 bool live_too) const;
    // The game whose line the board shows, for what leaves the machine (#54): any game being played, live or not,
    // whose position is on the line shown, or on the main line when the position shown is one of its; with
    // `whole_game`, anywhere on the main line. The rest stays possible during a live game.
    [[nodiscard]] const lichess::PlayingGame* toLichessLock(bool whole_game) const;
    // The game that locks the board: a live one; a correspondence game whose position is on the line shown, or on the
    // main line when the position shown is one of its; with `whole_game`, anywhere on the main line.
    [[nodiscard]] const lichess::PlayingGame* fairPlayLock(bool whole_game) const;
    [[nodiscard]] bool aidsLocked() const { return fairPlayLock(false) != nullptr; }  // on the board's position
    [[nodiscard]] bool gameLocked() const { return fairPlayLock(true) != nullptr; }   // on the whole game
    void drawFairPlayBanner(bool whole_game = false);
    // From the Board to lichess.org (#54, mockup M8, board "Lien"): the position opened on the analysis board, the
    // game shared, the position played out (an open challenge), a game of Lichess bookmarked; what each sends said
    // beside it, and nothing on the line of a game being played (R17).
    struct ToLichess {
        enum class Kind { Share, Challenge, Bookmark } kind = Kind::Share;
        bool busy                                           = false;
        std::string url;                                  // the game shared
        std::optional<lichess::OpenChallenge> challenge;  // its links
        lichess::ChallengeClock clock;
        std::string error;
    };
    std::optional<ToLichess> to_lichess_;  // what was asked last, under the board until closed
    std::future<lichess::Answer<std::string>> lichess_share_job_;
    std::future<lichess::Answer<lichess::OpenChallenge>> lichess_challenge_job_;
    std::future<lichess::Answer<bool>> lichess_bookmark_job_;
    std::string lichess_bookmark_pending_;                 // a game to bookmark once preference:write is allowed
    bool open_to_lichess_ = false;                         // the menu opened from Ctrl+K
    std::string focus_next_;                               // --focus: the word after a lichess-api= one
    [[nodiscard]] std::string boardLichessGameId() const;  // the game's on Lichess, when it comes from there
    void openPositionOnLichess();
    void shareGameOnLichess();
    void playPositionOnLichess(const lichess::ChallengeClock& clock);
    void bookmarkGameOnLichess();
    void pollToLichess();
    void drawToLichessMenu();               // its button under the board, and its menu
    void drawToLichessActions();            // the menu's actions
    void drawToLichessResult(float width);  // what came, under the board, as wide as it
    // The user's Lichess studies (#79, mockup M11, board 4): the Lichess page's card (the account's studies, a study's
    // chapters, imported into a database, opened on the Board, followed); the followed ones asked whether they
    // changed when the page opens (a HEAD each, nothing downloaded); a game of the Board sent into one of them or a
    // new one (lichess.org… › To a Lichess study…), and later its changes, on request.
    lichess::StudiesState studies_;
    bool studies_loaded_ = false;
    std::optional<std::vector<lichess::StudyInfo>> studies_list_;  // the account's, asked once the card opens
    std::string studies_list_error_;
    std::string studies_list_of_;  // the account and the permission the list was asked with
    std::future<lichess::Answer<std::vector<lichess::StudyInfo>>> studies_list_job_;
    std::string study_shown_;                                                   // the study whose chapters show
    int study_chapter_ = 0;                                                     // its chapter chosen
    std::map<std::string, std::vector<lichess::StudyChapter>> study_chapters_;  // as last read, by study
    struct StudyRead {
        lichess::Answer<lichess::StudyText> text;
        std::vector<lichess::StudyChapter> chapters;
        std::optional<lichess::StudyImport> import;  // when it went into a database too
    };
    struct StudyJob {
        std::string id;
        std::size_t base = SIZE_MAX;  // into this database too
        std::string base_name;
        std::filesystem::path folder;  // that database's
        bool open_after = false;       // its chosen chapter opened on the Board once read
        bool theory_after = false;       // made a theory study once read (#77)
        std::future<StudyRead> done;
    };
    std::optional<StudyJob> study_job_;
    std::string study_said_;     // the outcome of the last reading or import
    std::string study_said_of_;  // the study it is about
    bool study_said_bad_ = false;
    int study_base_      = -1;  // the database an import goes into; -1: the one it went into before, else the default
    std::future<std::vector<std::pair<std::string, lichess::Answer<std::string>>>> study_checks_job_;
    std::chrono::steady_clock::time_point study_checked_{};
    int lichess_page_frame_  = -10;    // the frame that drew the Lichess page last: one more after a gap opens it again
    bool studies_after_link_ = false;  // the permission to read studies asked from the card: the list again after
    // The card on the Board that sends the game.
    struct StudySend {
        int where = 0;  // 0 one of the user's studies, 1 a new study
        std::string study;
        std::array<char, 104> name{};  // the new study's
        int visibility = 0;            // private, unlisted, public
        std::array<char, 104> chapter{};
        bool black      = false;
        bool assistant  = false;
        bool after_link = false;  // sent once Lichess gave the permission
        bool busy       = false;
        std::string error;
        lichess::SentRecord going;                // what goes, while it goes
        uint64_t serial = 0;                      // the board's game then (board_game_serial_)
        int count       = 0;                      // and its changes
        std::optional<lichess::SentRecord> sent;  // what went, once it went
        int chapters = -1;                        // the study's then
        bool focus   = true;
    };
    std::optional<StudySend> study_send_;
    std::future<lichess::Answer<lichess::SendResult>> study_send_job_;
    std::future<lichess::Answer<bool>> study_update_job_;  // the changes sent
    lichess::SentRecord study_update_;                     // what they send
    uint64_t study_update_serial_ = 0;
    int study_update_count_       = 0;
    std::string study_update_error_;
    bool study_update_after_link_ = false;
    std::string study_origin_seen_;  // the database game last looked for among the games sent
    int study_popup_frame_ = -10;    // the last frame a list was open over the send card: its Escape not the card's
    void loadStudiesState();
    void saveStudiesState();
    void pollStudies();
    void drawLichessStudies();  // the Lichess page's card
    void drawStudyDetail(const lichess::StudyInfo& study);
    void readStudy(const std::string& id, std::optional<std::size_t> base, bool open_after = false);
    void checkFollowedStudies();
    void openStudyChapter(const std::string& id, std::size_t chapter);
    [[nodiscard]] std::string studyToken(bool write) const;  // the account's when it may, else none
    void askStudyPermission(bool write);                     // Lichess's page, the permission added
    void openStudySend();                                    // lichess.org… › To a Lichess study…
    void drawStudySendCard();                                // in the Board's column
    void startStudySend();
    void sendStudyChanges();  // the strip's "Send the changes"
    void drawSentLine();      // under the strip
    [[nodiscard]] bool studySendTakesKeys() const { return study_send_ && !study_send_->sent && page_ == Page::Board; }
    void drawPlayingGames();   // the Home page's list
    void drawListenOffer();    // the Lichess page's offer of the right to read challenges
    // A game of the user's that ended (#49, mockup M8, board "Fairplay"): brought into "Lichess · account", made when
    // missing; a banner offers its review (Enter), which asks Lichess again for its analysis first.
    struct FinishedGame {
        lichess::PlayingGame game;  // as it was listed while played
        std::string base;           // the database it goes into
        std::size_t index = SIZE_MAX;
        std::future<lichess::Arrival> arrival;
        std::optional<lichess::Arrival> got;
        std::future<lichess::Arrival> again;  // "Review it": its PGN asked again, for Lichess's analysis
        bool automatic = false;               // reviewed at once, as chosen on the Lichess page
        bool done      = false;               // reviewed or put aside: forgotten once nothing waits for Lichess
    };
    std::vector<FinishedGame> lichess_finished_;
    std::vector<std::string> lichess_finished_ids_;  // each game once, however it was heard ending
    bool lichess_playing_fresh_ = true;  // the list starts again (another token): what leaves it did not end
    void finishLichessGame(const lichess::PlayingGame& game);
    void pollLichessFinished();
    void reviewFinishedGame(FinishedGame& f);
    void openFinishedGame(FinishedGame& f, const std::string& pgn);
    [[nodiscard]] bool boardFree() const;  // nothing unsaved nor under way on the Board: a game may replace its own
    void drawFinishedBanner(bool home);
    [[nodiscard]] bool finishedCardTakesKeys() const {
        if (page_ != Page::Board) return false;
        for (const auto& f : lichess_finished_)
            if (!f.done && f.got && !f.got->pgn.empty() && !f.again.valid()) return true;
        return false;
    }
    int link_key_frame_ = -1;  // the frame whose Enter or Escape the card took
    // True when `text` is a Lichess address: its card shown, or said unknown. `databases`: pasted on the Databases
    // page, its games offered for the database chosen there.
    bool offerLichessLink(const std::string& text, bool databases = false);
    void pollLinkJob();
    void actOnLink();
    void drawLinkCard();
    [[nodiscard]] bool linkCardTakesKeys() const {
        return link_card_.has_value() || link_key_frame_ == ImGui::GetFrameCount() || finishedCardTakesKeys() ||
               studySendTakesKeys();
    }
    struct LichessSync {
        std::size_t index = 0;  // the database's
        std::string name;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::shared_ptr<lichess::SyncProgress> progress;
        std::shared_ptr<study::Database> db;
        std::future<lichess::SyncReport> done;
    };
    std::unique_ptr<LichessSync> lichess_sync_;
    std::optional<lichess::SyncReport> lichess_sync_report_;
    std::array<char, 64> lichess_player_{};
    bool lichess_player_active_ = false;
    lichess::Answer<lichess::BroadcastList> lichess_broadcasts_;
    std::future<lichess::Answer<lichess::BroadcastList>> lichess_broadcasts_job_;
    std::chrono::steady_clock::time_point lichess_broadcasts_time_{};
    bool lichess_all_broadcasts_ = false;  // all the live ones listed, not the six best known
    struct FollowedRound {
        lichess::BroadcastEntry entry;
        std::unique_ptr<lichess::RoundFollower> follower;
        bool with_token = false;  // followed with a token allowed to read broadcasts
        bool whole      = false;  // the whole tournament, its rounds one after another (#55)
        // What tells it apart: its round, or its tournament followed whole.
        [[nodiscard]] std::string key() const { return whole ? "tour:" + entry.tour.id : entry.round.id; }
    };
    std::vector<FollowedRound> followed_rounds_;
    // A broadcast, more (#55, mockup M8, board "Prep"): the players' standings and a team event's, asked while its
    // mosaic shows (no token); a search among the broadcasts (study:read).
    struct BroadcastStandings {
        std::string tour;  // the tournament's id
        lichess::Answer<std::vector<lichess::BroadcastPlayer>> players;
        lichess::Answer<std::vector<lichess::BroadcastTeam>> teams;
        std::chrono::steady_clock::time_point asked{};
        std::future<std::pair<lichess::Answer<std::vector<lichess::BroadcastPlayer>>,
                              lichess::Answer<std::vector<lichess::BroadcastTeam>>>>
            job;
    };
    BroadcastStandings broadcast_standings_;
    std::string broadcast_tour_round_;  // a tournament followed whole: the round shown, chosen; empty: the newest live
    bool broadcast_all_players_ = false;  // all the players in the standings, not the first twelve
    std::array<char, 80> broadcast_search_{};
    std::string broadcast_searched_;  // the words of the answer shown
    std::future<lichess::Answer<std::vector<lichess::BroadcastEntry>>> broadcast_search_job_;
    std::optional<lichess::Answer<std::vector<lichess::BroadcastEntry>>> broadcast_found_;
    bool broadcast_search_after_link_ = false;  // asked again once the permission is given
    // A round followed from a link: its tournament and names asked of Lichess, once.
    std::future<std::pair<std::string, lichess::Answer<lichess::BroadcastEntry>>> broadcast_named_job_;
    std::set<std::string> broadcast_named_;
    void startBroadcastSearch();
    void followWholeTournament(const std::string& key);  // a round followed becomes its tournament followed whole
    void pollBroadcastStandings();
    void drawBroadcastStandings();
    // The filters of a player's games, as the user chose them; the home's count of the new ones, asked at most once an
    // hour when the user chose it (M4, board 3).
    [[nodiscard]] lichess::SyncFilters lichessFilters() const;
    std::future<lichess::Answer<uint64_t>> lichess_count_job_;
    std::shared_ptr<std::atomic<bool>> lichess_count_cancel_ = std::make_shared<std::atomic<bool>>(false);
    std::optional<uint64_t> lichess_new_games_;
    std::chrono::steady_clock::time_point lichess_counted_{};
    std::array<char, 16> lichess_date_{};
    // The broadcast mosaic (mockup M4, board 4; view_broadcast.cpp): a followed round's games side by side, each
    // analysed by an engine of the library while the mosaic shows; the board can follow one of them live.
    struct BroadcastBoard {
        std::string url;  // the game's address: its place in the round
        // The tables' verdict for a position of 7 pieces or fewer (Lichess's, asked when the user said « always »).
        std::string tb_fen;
        std::optional<lichess::Outcome> tb;
        uint64_t version = 0;
        ReplayGame game;  // its moves and clocks, as Lichess last sent them
        chess::Board board;
        std::string white, black, result;
        int white_elo = 0, black_elo = 0;
        std::string pgn;
        std::unique_ptr<Analyser> analyser;
        std::string round_id, round;  // its round's id and number: a tournament followed whole has several
    };
    std::vector<BroadcastBoard> broadcast_boards_;
    std::future<lichess::Answer<lichess::Tablebase>> broadcast_tb_job_;  // one question to the tables at a time
    std::string broadcast_tb_fen_;
    std::string broadcast_round_;     // the round the Lichess page shows as a mosaic (its id); empty: the page itself
    std::string broadcast_selected_;  // the game chosen in it, by its address
    std::set<std::string> broadcast_saved_;  // the rounds put in the « Broadcasts » database once they ended
    std::map<std::string, uint64_t> broadcast_checked_;  // each followed round's changes looked at, for its end
    int broadcast_drawn_ = -1000;  // the frame the mosaic was last drawn in: its engines stop unseen
    void openBroadcastMosaic(const std::string& round_id);
    void drawBroadcastMosaic();
    void pollBroadcastBoards();  // once a frame: the games read again when they change, their engines, the round kept
    void saveBroadcastRound(const FollowedRound& round, const std::string& round_id,
                            const std::vector<lichess::BroadcastGame>& games);
    // The game the Board page follows live, from a followed round: it moves on with each move received.
    struct BoardBroadcast {
        std::string round, url;
        uint64_t version = 0;
    };
    std::optional<BoardBroadcast> board_broadcast_;
    void followBroadcastOnBoard(const BroadcastBoard& game);
    void drawBoardBroadcastLine();  // « Live from Lichess », and Back to live when the board went back
    void loadLichess();
    void saveLichessSettings();
    void pollLichess();
    void askBroadcasts();  // the list of broadcasts, asked again after ten minutes
    // The link, asking for `scopes`; `username`: the account Lichess offers, for a link made again with more scopes.
    void startLichessLink(std::vector<std::string> scopes = {}, std::string username = {},
                          std::optional<lichess::BroadcastEntry> then_follow = std::nullopt);
    void startLichessSync();  // the games of lichess_.player into "Lichess · player", made when it is missing
    [[nodiscard]] std::optional<std::size_t> lichessBase();  // that database
    void drawLichessHomeCard();
    // Lichess's answers for the board's position (M4, board 5): the tables online, the cloud's lines; each service
    // allowed at its first use, for a position or always; the answers kept in the profile's lichess-cache.
    struct OnlineTablebase {
        std::string fen;
        lichess::Answer<lichess::Tablebase> answer;
        int64_t when = 0;  // seconds since the epoch: when Lichess answered
    };
    struct OnlineCloud {
        std::string fen;
        lichess::Answer<lichess::CloudEval> answer;
        int64_t when = 0;
    };
    std::optional<OnlineTablebase> online_tb_;
    std::optional<OnlineCloud> online_cloud_;
    std::future<lichess::Answer<lichess::Tablebase>> online_tb_job_;
    std::future<lichess::Answer<lichess::CloudEval>> online_cloud_job_;
    std::future<lichess::Answer<std::vector<std::string>>> online_line_job_;
    std::string online_tb_asked_, online_cloud_asked_, online_line_asked_;
    std::set<std::string> online_once_;          // positions allowed once ("Ask for this position")
    std::set<std::string> online_not_in_cloud_;  // positions the cloud had nothing for, this session
    bool online_tb_not_now_ = false, online_cloud_not_now_ = false;
    std::string online_seen_;  // the position shown, since when: stepping through a game asks nothing on the way
    double online_since_ = 0;
    void drawBoardOnline();
    void drawOnlineTablebase(const chess::Board& board, int pieces);
    void drawOnlineCloud(const chess::Board& board);
    // `without_account`: anonymously, as Lichess lets it still (slowly), when the user chose not to give the scope.
    void followBroadcast(const lichess::BroadcastEntry& entry, bool without_account = false);
    void drawLichessPage();
    void drawLichessAccount();
    void drawLichessGames();
    void drawLichessBroadcasts();
    void drawLichessFollowed();
    void drawLichessOnline();
    // Lichess's evaluations kept offline (#58, mockup M11, board 6): the index in the profile; its card on the Lichess
    // page (what is kept, the preparation, Update, Remove them); the Analysis tab's line, the review's known positions
    // and the Tree tab's column.
    std::unique_ptr<OfflineEvals> offline_evals_;
    int offline_evals_choice_  = 0;      // what is kept: 0 every position, 1 the openings
    bool offline_evals_remove_ = false;  // Remove them asked: what it frees said, then the answer
    bool offline_evals_focus_  = false;  // the card to bring into view (Ctrl+K, --focus)
    bool offline_evals_keys_   = false;  // Enter is the card's: it was brought into view, not clicked away from
    float offline_evals_y_     = -1;     // where the card starts on the screen, for the page to scroll there
    std::string offline_evals_error_;    // why the last preparation stopped
    std::future<EvalsSize> offline_evals_update_;                  // Update: the server's file asked for its date first
    std::string offline_evals_note_;                               // what Update found
    std::chrono::steady_clock::time_point offline_evals_since_{};  // the rate: since when, from how many bytes
    uint64_t offline_evals_from_ = 0;
    std::map<std::string, std::optional<study::OfflineEval>> offline_evals_seen_;  // looked up, by position
    uint64_t offline_evals_generation_ = 0;                                        // of what answered when they were
    int offline_evals_frame_           = -1;  // the frame whose lookups' time is counted, and that time
    double offline_evals_spent_        = 0;
    void pollOfflineEvals();
    void drawLichessOfflineEvals();
    void startOfflineEvals(std::string source, int min_men);
    // A position's offline evaluation when it may show (standard chess, nothing locked by fair play), kept for the
    // frames that ask again.
    [[nodiscard]] std::optional<study::OfflineEval> offlineEval(const chess::Board& board);
    void drawOfflineEvalLine(const chess::Board& board);  // the Analysis tab's line, above the engine
    // The review's known positions: those the evaluations have deep enough; empty without them.
    [[nodiscard]] std::function<std::optional<PositionEval>(const chess::Board&)> reviewKnown() const;
    // The game's headers, unfolded in place above the notation (M2, board 7): Alt+H.
    bool headers_open_ = false;
    std::map<std::string, std::array<char, 256>> header_edit_;  // a field's text while it is written
    std::string header_active_;                                 // the field being written
    std::array<char, 64> header_new_key_{};
    std::array<char, 256> header_new_value_{};
    std::string suggest_base_, suggest_typed_;  // what a name field holds; what the names were looked up for
    std::vector<std::string> suggest_names_;
    void drawGameHeaders();
    void setGameHeader(const std::string& key, const std::string& value);
    [[nodiscard]] std::optional<commentary::OpeningCode> boardOpening() const;
    // The names that start as `typed` does (from two letters), in the game's database or the one games were last
    // saved into: players, events, sites.
    [[nodiscard]] std::vector<std::string> nameSuggestions(std::string_view typed);
    enum class NewMove { Variation, MainLine, Replace };
    void boardPlayAs(chess::Move move, NewMove as);
    std::optional<chess::Move> new_move_;  // asked about, waiting for the answer
    ImVec2 new_move_at_;
    int new_move_frame_ = 0;
    void drawNewMoveQuestion();
    // A move made on the board with the mouse: the user's move in the game they play, else boardPlayAsked().
    void boardMove(chess::Move move);
    void boardClick(const chess::Board& board, chess::Square square);
    // The left button on the board (board_.drag): `press`, the square it went down on this frame; `hover`, the square
    // under the pointer; `right`, a right click. A press acts as a click (boardClick); a piece picked up and dragged
    // lands where it is released when it may go there, and else stays picked up; one pressed again without moving is
    // put down.
    void boardPointer(const chess::Board& board, std::optional<chess::Square> press, std::optional<chess::Square> hover,
                      bool right);
    // The Board page as the Chess space has it (mockup M2): the notation written in place, the symbols typed, the
    // arrows and squares of the right button. The Engines space's stays as it was.
    [[nodiscard]] bool chessBoard() const { return !views_.empty() && view().study_board; }
    [[nodiscard]] BoardPage::Snapshot boardSnapshot() const;
    void boardChanged(BoardPage::Snapshot before);  // a change made: undone back to `before`
    void boardUndo(bool redo);
    [[nodiscard]] MoveNotes* boardNotes();  // those of the move shown, the start position's at its start
    void boardMark(chess::Square from, chess::Square to);
    void boardClearMarks();
    void boardEditKeys();
    void boardMoveKeys();  // a letter that begins a move opens the move field
    void boardTypeNag(char sign);
    void boardSetNag(uint8_t nag);
    // The field opened on the move shown: its comment before it, after it, or the assistant's after it (#60).
    void boardComment(bool before, bool assistant = false);
    void boardCommentDone();
    void boardToast(std::string text);
    void drawBoardNagBar();
    void drawMoveField();  // the move being typed, played by Enter (the Chess space)
    // The Chess space's column beside the board (mockups M2 and M3, view_board_tree.cpp): its tabs (Alt+1 to Alt+4),
    // the opening tree of the databases and books, the reference games of the position.
    void drawChessColumn();
    void drawChessTabs();  // the tabs and the one chosen
    void drawBoardTree();
    // The Questions tab (Alt+5, mockup M5, board 2; view_board_questions.cpp): questions about the position, answered
    // by the assistant from what the engine, the databases and the tables establish; the first time, the card that
    // offers its model (board 1).
    void drawBoardQuestions();
    void drawAssistantFirstUse();
    // `text` as the user reads it; `asked`, when not empty, what the assistant is given (a suggestion, in its own
    // language).
    void askQuestion(const std::string& text, const std::string& asked = {});
    void pollQuestions();  // once a frame: the answers that came, the assistant's download and unloading
    // Settings › Assistant (M5, board 5): where it stands, its model, where it runs, when it unloads.
    void drawAssistantSettings(float label);
    // What the assistant is doing, for the home's « In progress »: its download, its model loaded; empty otherwise.
    [[nodiscard]] std::string assistantStatus() const;
    // The running tournament whose engines use the GPU, by its name; empty when none.
    [[nodiscard]] std::string gpuTournament() const;
    // An answer put in the notation, on the move it was asked after: the assistant's comment, beside the one the move
    // may have (#60), after the assistant's earlier one.
    void addAssistantComment(const std::string& text, int line, int ply, const std::string& fen,
                             const std::vector<std::string>& basis = {});
    // What the assistant's comments written in this session rest on, by their text: said when one is hovered (R9).
    std::map<std::string, std::vector<std::string>> assistant_basis_;
    [[nodiscard]] std::vector<std::string> commentBasis(bool from_review);
    std::unique_ptr<AssistantService> assistant_;

    // The engine picker (#59, mockup M9; view_engine_picker.cpp): the strongest engines from their authors' GitHub
    // releases, the build this processor runs downloaded, checked, started and added to the library; the card that
    // offers Gaia where an engine is first needed; the new versions of the engines taken from it.
    struct PickerEngine {
        std::string id;
        bool asked = false;  // its releases are asked of GitHub (or read from the profile), or were
        std::optional<catalogue::Releases> releases;
        catalogue::Pick pick;
        std::shared_ptr<DownloadProgress> progress;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::future<catalogue::InstallResult> install;
        bool install_when_known = false;  // asked to install before its releases came
        bool keep_old           = false;  // an update keeps the version before as an engine of its own
        bool for_board          = false;  // from the first-need card: the analysis engine once installed
        std::string result;               // how its last install ended
        bool result_error = false;
    };
    struct EnginePicker {
        bool open = false;
        catalogue::Catalogue catalogue;
        bool catalogue_read    = false;
        bool catalogue_fetched = false;
        std::future<catalogue::Catalogue> catalogue_job;  // the repository's, once a day
        std::vector<PickerEngine> engines;                // in the catalogue's order
        std::deque<std::string> to_ask;                   // ids whose releases are to be asked, one at a time
        std::string asking;
        std::future<catalogue::Releases> answer;
        bool limited = false;                         // GitHub refused more requests: none asked again this session
        std::vector<catalogue::Installed> installed;  // installed.json, read again after an install
        bool installed_read = false;
        std::chrono::steady_clock::time_point updates_checked{};  // when the library's engines were last looked at
        bool updates_asked = false;
    } picker_;
    std::optional<CpuFeatures> cpu_;  // detected once
    [[nodiscard]] const CpuFeatures& cpu();
    // The kept or built-in catalogue, at once; with `fetch`, the repository's in the background, once a session.
    // The engines are made from it the first time only (syncPickerEngines): a PickerEngine& stays valid across it.
    void readPickerCatalogue(bool fetch);
    void syncPickerEngines();
    [[nodiscard]] PickerEngine* pickerEngine(std::string_view id);
    void askReleases(PickerEngine& e);  // queued; the profile's copy of the last day, else GitHub
    void openEnginePicker();
    void pollEnginePicker();  // every frame: the catalogue, the answers, the installs
    void drawEnginePicker();  // in place of the Engines page while open
    void installPicked(PickerEngine& e, bool keep_old = false);
    void pickerInstalled(PickerEngine& e, catalogue::InstallResult result);
    // An engine for what needs one (the analysis, the questions, the review), Gaia offered first (M9, board 2). `why`
    // says what needs it.
    void drawFirstEngineCard(const char* why);
    // The version installed of a library engine taken from the catalogue, and the newer one its releases offer: empty
    // when there is none, or it is not known yet.
    [[nodiscard]] std::string installedVersion(const EngineEntry& e);
    [[nodiscard]] std::string newerVersion(const EngineEntry& e);
    void checkEngineUpdates();  // the Engines page shown: the library's engines from the catalogue, once a day
    void drawEngineUpdates();   // their new versions, above the engine list
    std::shared_ptr<EngineOracle> oracle_;  // the engine that answers, the analysis's, kept while it is the same
    struct QuestionResult {
        std::variant<commentary::Answered, commentary::Problem> answer = commentary::Problem::NoEngine;
        std::string error;
        // Two moves compared: the position they are played from, and what the databases hold of it.
        std::optional<commentary::Position> from;
        std::optional<commentary::References> refs;
    };
    struct QuestionAsked {
        std::string question;
        std::string fen;  // the position it was asked in, on the move (line, ply) of the game
        int line = -1, ply = 0;
        std::string engine;                            // the engine that answered, for what the answer rests on
        std::vector<commentary::ReferenceBase> bases;  // the databases it looked in: a game it names opens from them
        std::future<QuestionResult> answer;
        std::optional<QuestionResult> done;
    };
    std::vector<QuestionAsked> questions_;                     // this game's, the oldest first
    std::vector<std::future<QuestionResult>> questions_left_;  // those of a game left, still being answered
    std::array<char, 512> question_text_{};
    bool question_focus_ = false;  // the question's field takes the keys at its next drawing
    // The assistant's comments on the board (M5, boards 3 and 4; view_board_assist.cpp): Alt+C on the move shown, the
    // key moments of the reviewed game in the background, the game annotated from its review. Each one change.
    void commentBoardMove();
    void commentKeyMoments();
    void annotateBoardGame();
    void pollBoardComments();
    void drawCommentProgress();  // the key moments being commented: how many, Cancel
    void drawReviewAssistant();  // the review tab's story of the game and the assistant's gestures
    void drawReviewCurve();      // the review tab's head: White's chances, the key moments as dots (M5, board 4)
    // A tournament's game on the study Board (#87, mockup M12, board 8): the review tab's head is the curve of the two
    // engines that played it, from the game alone, the review's over it once done; their moves' times under it, their
    // figures and the moments that stand out. Drawn by drawGameCurve(), the review's alone when it is null.
    void drawEnginesCurve();
    void drawGameCurve(const EngineCurves* engines);
    void drawEngineMoveTip(int ply, const MoveReview& judged);  // a move's fields in full, under the mouse
    void drawEngineTagChips();             // what the notation shows beside the moves of engines: the setting
    void boardExpectedVariation(int ply);  // the line the engine of move `ply` expected after it, a variation
    [[nodiscard]] EngineTags engineTags() const;
    // The commentary's review of the board's game, from its judging analysis; made again when that changes.
    [[nodiscard]] std::shared_ptr<const CommentaryReview> boardCommentaryReview();
    struct CommentJob {
        std::vector<AssistantTarget> targets;
        int total                                 = 0;
        bool key_moments                          = false;
        uint64_t game                             = 0;  // the board's game it was asked for (board_game_serial_)
        std::shared_ptr<std::atomic<int>> done    = std::make_shared<std::atomic<int>>(0);
        std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
        std::future<std::vector<std::string>> texts;  // one per target, empty when none
        std::vector<std::string> basis;               // what they rest on (commentBasis())
    };
    std::optional<CommentJob> comment_job_;
    std::shared_ptr<const CommentaryReview> board_commentary_;
    std::string board_commentary_of_;
    EngineCurves engine_curves_;    // the board's tournament game as its engines saw it (#87)
    std::string engine_curves_of_;  // the game it is of, and the language of its sentences
    // How far the board's game followed theory, against a database of the user's (the Tree tab's, else the first with
    // a tree): its book moves are not judged, its novelty is told. Looked for in the background.
    struct TheoryJob {
        std::string of;
        std::future<commentary::Opening> done;
    };
    std::optional<TheoryJob> theory_job_;
    std::string theory_of_;  // the game and database board_theory_ is of
    commentary::Opening board_theory_;
    commentary::Opening board_theory_used_;      // the same, ended before the first error the engine sees
    std::string board_theory_base_;              // the database's name, for what the review says
    [[nodiscard]] std::string boardTheoryKey();  // the game and the database its theory is looked for in; empty: none
    // The moves the review singles out, each with its template comment held to the boards (#64): the review tab's key
    // moments in words.
    std::vector<std::pair<std::size_t, std::string>> review_moments_;
    std::string review_moments_of_;
    // The story of the game as the templates tell it, held to the boards (commentary::heldStory()), and what each side
    // should work on, from the same review.
    std::vector<commentary::StoryPassage> review_story_;
    std::vector<commentary::WorkOn> review_work_on_;
    std::string review_story_of_;
    // The story told by the assistant's model (#62, mockup M11, board 5): written in the background once per review and
    // language when the model is there, again on "Write it again"; the templates' meanwhile, and without the model.
    struct StoryJob {
        std::string of;  // the review it is written for (review_story_of_)
        uint32_t round                            = 0;
        int total                                 = 0;  // its passages
        std::shared_ptr<std::atomic<int>> done    = std::make_shared<std::atomic<int>>(0);
        std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
        std::vector<std::string> templ;  // the templates' passages it tells again
        // The passages, and why the model gave no answer when it gave none.
        std::future<std::pair<std::vector<commentary::StoryPassage>, std::string>> story;
    };
    std::optional<StoryJob> story_job_;
    std::vector<commentary::StoryPassage> model_story_;
    std::string model_story_of_;      // the review it was written for
    std::string model_story_error_;   // why the model gave no answer
    std::string story_cancelled_of_;  // the review whose story was cancelled: the templates' until "Write it again"
    uint32_t model_story_round_ = 0;
    bool story_template_shown_  = false;  // "The template version" asked
    // Why the story stays the templates': the model is not there, it has not learnt RookStudio's comments, or a
    // tournament has the processor; empty when the model can write it.
    [[nodiscard]] std::string storyWithoutModel() const;
    void writeStory(bool again);  // the model writes the review's story, in the background; `again`: told otherwise
    void pollStory();             // once a frame: the story written
    void drawReviewStory(const CommentaryReview& review);  // the review tab's story, the model's or the templates'
    [[nodiscard]] std::string storyText() const;           // the story shown, as "Copy it" copies it
    // Lichess's tables for the review's endgame (#42, M5 board 4): the game's positions of seven men or fewer asked
    // once each, with « Always » for the tables on the Lichess page or for this game alone, in the background; taught
    // to tables the review is then held to (review_tables.hpp).
    struct ReviewTablesJob {
        std::string of;  // the game it asks for
        std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
        std::shared_ptr<std::atomic<int>> done    = std::make_shared<std::atomic<int>>(0);
        int total                                 = 0;
        std::future<std::string> finished;  // why it stopped early, empty when it went to the end
    };
    std::optional<ReviewTablesJob> review_tables_job_;
    std::shared_ptr<commentary::Tablebases> review_tables_;  // what they were taught, for review_tables_of_
    std::string review_tables_of_;                           // the game they are of
    int review_tables_taught_ = 0;  // positions dealt with when the review was last made: part of its key
    std::vector<TablePosition> review_tables_positions_;              // the game's positions they could be taught about
    std::shared_ptr<const commentary::MainLine> review_tables_line_;  // the game, as the tables read it
    bool review_tables_started_ = false;                              // asked for this game already
    bool review_tables_once_    = false;                              // asked for this game, without « Always »
    bool review_tables_not_now_ = false;                              // not asked for this game
    std::string review_tables_error_;
    void updateReviewTables();  // a new game's tables; the asking started when allowed, its end taken
    void drawReviewTables();    // the review's line about them: the question, the asking, what they judged
    [[nodiscard]] const commentary::Tablebases* reviewTables() const;  // when taught anything
    uint64_t board_game_serial_ = 0;  // one more at each game put on the board
    void drawBoardReferences();
    int board_side_tab_ = 0;  // 0 the notation, 1 the analysis, 2 the opening tree, 3 the reference games, 4 questions
    int engine_menu_ply_ = -1;  // the tournament engine's move whose menu is open in the notation (#87)
    struct TreeRow {
        std::string shares;  // My databases: each database's games, said over the Games column
        std::string san;
        chess::Move move;
        uint64_t games = 0;                           // or a book's weight, in thousandths
        double white = 0, draws = 0, black = 0;       // the shares of the games with a result
        int score = -1, elo = 0, perf = 0, last = 0;  // for the side to move; -1 or 0 when not known
        std::string months;  // Lichess's explorer: where its position's games month by month are kept (#56)
        int book           = 0;  // a CTG book's colour (#68): 1 its main move (green), -1 not for a tournament (red)
        uint8_t symbol     = 0;  // a CTG book's symbol of the move, and its evaluation of the position after it
        uint8_t evaluation = 0;
    };
    std::vector<TreeRow> tree_rows_;
    std::string tree_rows_of_;  // the position and the source the rows are of
    int tree_db_      = -1;     // the database the Tree tab shows (the shelf's index); -1: My databases, or a book
    std::set<std::filesystem::path> tree_left_;  // the databases My databases leaves out (folders), by its menu
    int tree_row_     = -1;     // the row chosen with Up and Down: Enter plays it, * stars it; -1 none
    bool tree_scroll_ = false;  // the row chosen brought into view
    void drawTreeNovelty(std::size_t index);
    void drawTreeBookMark(const TreeRow& row);  // a CTG book's colour and symbols of a move (#68)  // where the game's
                                                // line leaves the database, under the tree's summary
    // The tree's move under the mouse or chosen, an arrow on the board the next frame, with the position it is of.
    struct TreeArrow {
        chess::Move move;
        uint64_t position = 0;
    };
    std::optional<TreeArrow> tree_arrow_;
    // Where the game's line leaves the database shown (M3): the first ply after which no game reaches its position,
    // confirmed by a position search, since the tree does not see a position a single game reaches.
    struct NoveltyJob {
        std::string of;  // the database, its snapshot, the line
        std::optional<uint32_t> ply;
        std::unique_ptr<study::Search> search;  // the games through the position after it
        bool confirmed = false;                 // none found
        uint64_t found = 0;
    };
    NoveltyJob tree_novelty_;
    std::string tree_summary_;
    bool tree_book_  = false;  // the rows are a book's weights
    int tree_source_ = 0;      // the databases with a tree, the library's Polyglot books, then Lichess's explorer
    // Lichess's explorer in the Tree tab (M3, board 2; M4): its answers this session, by position and source.
    std::map<std::string, lichess::Answer<lichess::Explorer>> explorer_answers_;
    std::future<lichess::Answer<lichess::Explorer>> explorer_job_;
    double explorer_job_since_ = 0;  // when it was asked
    std::map<std::string, double>
        explorer_failed_;  // a question Lichess did not answer, and when: asked again after a minute
    std::string explorer_asked_;
    void drawLichessTreeCard(bool linked);  // what the source needs: a linked account, the explorer allowed
    // The Tree tab's rows from Lichess's explorer (0 the masters, 1 Lichess's players, 2 the player), asked when the
    // position has stayed a moment; false when the card that source needs is drawn instead.
    bool lichessTreeRows(int source, const std::string& fen);
    // The tree, more (#56, mockup M8, board "Prep"): each move's games month by month for Lichess's players (a
    // request a move, when the user asks for it), and the games the explorer names, opened on the board by a click.
    std::future<lichess::Answer<lichess::Explorer>> explorer_month_job_;
    std::string explorer_month_asked_;
    std::vector<lichess::ExplorerGame> tree_games_;  // those of the answer shown
    int tree_games_source_ = -1;                     // 0 the masters', 1 Lichess's
    std::future<lichess::Answer<std::string>> explorer_game_job_;
    std::string explorer_game_title_, explorer_game_fen_, explorer_game_error_;
    void pollExplorerMonths();
    void drawTreeGames();
    void drawTreeMonths(const std::string& key);  // a move's months, as a line in its row
    // FIDE cards for the players of the databases (#57, mockup M8, board "Prep"): on demand only, kept in the profile.
    std::unique_ptr<lichess::FideCards> fide_cards_;
    [[nodiscard]] lichess::FideCards& fideCards();
    struct FideAsk {
        std::string name;  // as the database writes it
        int64_t id = 0;    // the FIDE id the game gives, 0 without
        std::future<lichess::Answer<lichess::FideCard>> card;
        std::future<lichess::Answer<std::vector<lichess::FideCard>>> search;
        std::optional<std::vector<lichess::FideCard>> choices;  // namesakes: the user chooses
        std::optional<lichess::FideCard> shown;
        std::string error;
    };
    std::optional<FideAsk> fide_ask_;
    bool fide_popup_ = false;  // opened the next frame
    void askFideCard(std::string name, int64_t id);
    void pollFideCards();
    void drawFideCard(const lichess::FideCard& card);  // its lines
    void drawFidePopup();                              // the card asked for, or the namesakes, by the mouse
    void drawFideAsk(FideAsk& ask);                    // the popup's content
    std::string explorer_seen_;
    double explorer_since_ = 0;
    std::map<std::string, std::shared_ptr<tour::PolyglotBook>> tree_books_;
    std::map<std::string, std::shared_ptr<const study::CtgBook>> tree_ctg_books_;  // the CTG books shown (#68)
    std::string tree_book_request_;  // a book of the library the Tree tab is to show, asked by the Books page (#68)
    bool tree_ctg_ = false;          // the rows are a CTG book's
    struct ReferencesJob {
        std::string of;
        std::future<commentary::References> done;
        std::vector<DatabaseShelf::TreeSource> sources;
    };
    std::optional<ReferencesJob> references_job_;
    commentary::References references_;
    std::vector<DatabaseShelf::TreeSource> references_sources_;
    std::string references_of_;
    // The threads the running tournament's games use now, about: its games times the larger thread count of its
    // engines.
    [[nodiscard]] int tournamentThreads() const;
    // A, on the boards of the other pages: their position on the Board page, analysed.
    void analyseLiveGame(int slot);
    void analyseReplay();
    // Games and positions from elsewhere: a PGN file read in the background, the clipboard, a text.
    void openBoardPgn(const std::filesystem::path& file);
    void pollBoardPgn();
    void showPgnSource(PgnSource source);  // its first game on the board, its games listed
    // Game `index` of the board's PGN source, or of `fresh`, which becomes the board's source once the game shown has
    // been remembered with its own (openOnBoard, the recent games).
    void loadPgnGame(int index, std::optional<PgnSource> fresh = std::nullopt);
    void readOnBoard(const std::string& text, const std::string& name);  // a FEN, a PGN, moves
    void pasteOnBoard();
    void saveBoardPgn();  // asks where, then writes the game with its variations and notes
    void drawPgnGames();
    void openPositionEditor();
    void drawPositionEditor(float size);
    // A game analysed move by move, in the background, by the analysis engine (one line).
    [[nodiscard]] const EngineEntry* analysisEngine();  // the one chosen, or the first that works; null without any
    void startGameReview();
    void cancelGameReview();
    void pollGameReview();
    void drawGameReview();  // what it found
    // The analysis shown with the board's game: the one running for it (its latest values), else the first kept.
    [[nodiscard]] const GameReview* shownReview() const;
    // Every analysis of the board's game, the judging one first: those running with their latest values.
    [[nodiscard]] std::vector<const GameReview*> shownReviews() const;
    // Another engine of the library analyses the board's game too; an analysis judges or goes.
    void addReviewEngine(const std::string& engine);
    void judgeWith(const std::string& engine);
    void removeReview(const std::string& engine);
    void startReviewTask(const EngineEntry& engine, const std::vector<GameReview>& kept);
    [[nodiscard]] std::string judgementTip(int ply, const MoveReview& judged) const;
    // The position after the next (1) or previous (-1) judged move from the board's; -1 when there is none.
    [[nodiscard]] int judgedPly(int direction) const;
    void drawThisMove();  // under the moves: what the engine that played the move said, and each analysis's verdict
    [[nodiscard]] float thisMoveHeight() const;
    // Games between two engines of the library from the board's position, played apart from any tournament.
    void startBoardGames();
    void pollBoardGames();  // every frame: the board follows the game being played
    void drawBoardPlay();   // their settings, then the games as they are played
    [[nodiscard]] int engineThreads(const std::string& name) const;  // a library engine's Threads, 1 without
    void drawQuitModal();

    // The command palette (Ctrl+K) and the guide (F1, the `?` of the page titles), see view_palette.cpp.
    struct PaletteEntry {
        std::string label;
        std::string where;  // the page, the tournament or the menu it belongs to, shown beside it, searched after it
        std::function<void()> run;
    };
    void drawPalette();
    [[nodiscard]] std::vector<PaletteEntry> paletteEntries();
    [[nodiscard]] const char* guideSection() const;  // the guide's topic of the page and tab shown
    void toggleGuide();                              // F1
    bool palette_request_ = false;                   // opens on the next frame (Ctrl+K, --focus palette)
    std::string palette_query_;
    int palette_selected_ = 0;  // in the matches, best first
    // The guide panel at the right of the page (guide_view.hpp), and the home page's Getting started card hidden.
    GuideView guide_;
    bool getting_started_hidden_ = false;

    // tournaments
    [[nodiscard]] bool isMatch() const;
    void refreshTable();
    void drawTournamentOverview();
    void drawStandings();
    void drawStandingsTable(bool compact);
    void copyStandings(ui::CopyAs as);  // to the clipboard, in the order shown
    void drawStandingsRow(std::size_t i, bool compact, bool swiss);
    void drawPgnOutError();  // when the PGN file of setup "Also append every game to" cannot be written
    // The games from unbalanced openings and the openings replaced, when the tournament looks for them.
    void drawUnbalancedCount();
    void showUnbalancedGames();  // the Games tab, filtered on them
    void drawCrosstable();
    void drawOpponents(int id);  // the games of a participant against each opponent, under the standings table
    void drawTeamScore();        // a team match: the teams' score, its Elo and LOS
    void drawTeamStandings();    // a team match: the score, then each team's engines against the other team's
    void drawSchedule();
    void drawBracket();
    void drawEditPanel();
    void applyTournamentEdit(const tour::Edit& edit);
    void undoTournamentEdit();
    // The Schedule tab's change panel, set to withdraw or replace `participant`: its impact shows before anything
    // changes.
    void openParticipantEdit(tour::Edit::Kind kind, int participant);
    void refreshAfterEdit();
    // The context menu of a finished game (Games and Schedule tabs): play it again, void it, set its result.
    void drawGameMenu(uint32_t encounter, int game);
    // The context menu of a game being played (Live tab, detached boards): end it with a result.
    void drawEndGameMenu(int slot);
    // The result that counts of game `number` (its number()), nullptr when none does; asked of the tournament once
    // per change of it.
    [[nodiscard]] const tour::GameResult* gameResult(uint64_t number);

    // broadcast
    void updateBroadcast();
    void drawBroadcast();
    void drawLichess(bool& changed);
    void drawWebChat(bool& changed);   // under the web page: the chat link and the Discord widget
    void drawTunnel(bool& changed);    // under the web page
    void lichessTokenField();          // in the Broadcast tab and in Settings
    void saveLichessToken();           // to the profile, when it changed
    std::string lichess_token_;        // as typed
    std::string lichess_token_saved_;  // as the profile has it

    // setup sections
    void setupFormat();
    bool setupTeamOptions();  // a team match: the pairings and the teams' names; true when they changed
    // The participants, seeds first, each on a line wide enough for its name, with its reference Elo, what it plays
    // differently from the common settings, and ⋯ for its own settings. Ctrl and Shift select several; a drag moves
    // them; Delete removes them, Ctrl+Up and Ctrl+Down send them to the top or the bottom, Alt+Up and Alt+Down move
    // them by one.
    void setupParticipants();
    ImGuiSelectionBasicStorage participants_selected_;  // by index
    bool participants_focused_ = false;  // a participant had the keyboard at the last frame: the keys above apply
    int participant_menu_      = -1;     // the participant whose ⋯ menu opens on the next frame
    // A participant's own settings, its ⋯ menu: Hash, Threads, nodes or depth, time odds, its own book; then moves.
    enum class ParticipantAction { None, Again, Top, Up, Down, Remove };
    ParticipantAction participantMenu(ParticipantSetup& p, int index, bool open_book, bool& changed);
    // The library beside the participants (or beside A and B of a match): searched by name, group and notes, a group
    // chosen with its chip, sorted by name or reference Elo, the engines with a problem marked. Double-click or Enter
    // adds the engines selected (Ctrl and Shift select several), a drag drops them at a place in the participants;
    // opponents suggested around the first participant's rating. True when the setup changed.
    bool participantPicker();
    struct EngineLibrary {
        std::string search;
        std::string group;                    // the group whose chip is on, empty: every engine
        ImGuiSelectionBasicStorage selected;  // by ImHashStr of the engine's name
        bool focused = false;                 // one of its engines had the keyboard at the last frame: Enter adds
        int suggest  = 8;                     // opponents to suggest
        int range    = 150;                   // Elo, around the first participant's
        std::string message;                  // what the last suggestion found
    } library_;
    std::vector<std::string> library_drag_;  // the engines being dragged from the library to the participants
    float players_list_h_ = 0;               // the height of the participants' and the library's lists
    // Library engines added: A then B of a match, before participant `at` otherwise (-1: at the end).
    bool addParticipants(const std::vector<std::string>& names, int at = -1);
    int open_picker_ = 0;  // --focus setup-picker: 1 the library's search focused, 2 with opponents suggested
    // A participant's own book: a small button ("—", or the book's name) that opens the choice of the library's
    // Polyglot books and of the depth. True when `book` changed; `again` (when given) is set when the user asked
    // for the engine once more, with another book.
    bool ownBookButton(const char* id, OwnBook& book, float width, bool* again = nullptr, bool open = false);
    // Why a book cannot be a participant's own (ownBookProblem()), kept by path and read again when the file's size
    // or time changes; empty when it can, or when it is not found. Polyglot books only: a PGN book's problems are
    // those of its conversion (pgn_books_).
    const std::string& ownBookCheck(const std::string& path);
    // The participants' PGN books turned into Polyglot books, in the background.
    PgnBookJobs pgn_books_;
    // At each frame: the PGN books of the setup are wanted (prepared as soon as chosen or loaded), the conversions
    // move on, and those that ended say so in the console.
    void pollOwnBooks();
    struct OwnBookCheck {
        uintmax_t size = 0;
        std::filesystem::file_time_type time{};
        std::string problem;
    };
    std::map<std::string, OwnBookCheck> own_book_checks_;
    int open_own_book_       = -1;     // the participant whose book popup opens: --focus setup-book, a copy just added
    bool open_own_book_list_ = false;  // --focus setup-book-list: with its list of books open
    void drawSetupSummary();
    // The actionable messages of the setup and their fixes, see view_notices.cpp: the summary's checks of the
    // threads, the option values, the opening book and the tablebases (read off the UI thread, tb_checks_).
    [[nodiscard]] std::vector<Notice> setupNotices();
    void drawSetupNotices(std::vector<Notice> notices, const char* id);  // with the fixes possible now
    void applyFix(const Fix& fix);
    void selectEngine(const std::string& name);  // on the Engines page, as the palette opens an engine
    TablebaseChecks tb_checks_;
    void drawTemplates();
    void setupEngines();
    void setupEngineOptions();  // the settings forced on every engine
    void setupMatch();
    void drawSprtCalculator();
    void setupLimit();
    void setupConcurrency();
    void setupOpenings();
    void setupAdjudication();
    void setupPgn();

    // The rows of the setup's settings, each folded to a line: its profile, its summary, and ✎ that opens its
    // details in place. The five sections with a profile, then the concurrency and the PGN files of this tournament.
    enum SetupRow { RowFormat, RowTime, RowOpenings, RowAdjudication, RowEngines, RowConcurrency, RowPgn, ROW_COUNT };
    std::array<bool, ROW_COUNT> setup_open_{};         // the rows whose details are open
    std::array<float, ROW_COUNT> setup_details_h_{};  // their height at the last frame, for their background
    float setup_title_w_ = 0;                          // the column of their titles
    // The row of a name: "format", "time", "openings", "adjudication", "engines", "concurrency", "pgn".
    [[nodiscard]] static std::optional<SetupRow> setupRowOf(const std::string& key);
    int open_profile_list_ = -1;  // --focus setup-profiles=ROW: the row whose profile list opens
    // One row, then its details drawn by `details` under it when it is open. `section`: the profile's, none for the
    // rows without one; `more`: what the summary's tooltip adds to it.
    void setupRow(SetupRow row, const char* title, const std::string& summary, const std::string& more,
                  std::optional<SetupSection> section, void (App::*details)());
    // A section's row, its summary and its tooltip made from the setup.
    void sectionRow(SetupRow row, SetupSection section, void (App::*details)());
    void drawSetupRows();
    // The rule that chose the number of games played at once: one per hardware thread for a node count, physical
    // cores − 1 for a clock, or the user's; `brief` for the row's summary.
    [[nodiscard]] std::string concurrencyRule(bool brief) const;
    [[nodiscard]] std::string concurrencySummary() const;  // "15 games at a time · physical cores − 1, for a clock"
    [[nodiscard]] std::string pgnSummary() const;
    // The profile list of a section: a profile is applied when chosen; then Save as profile…, and Update, Revert and
    // Delete for the profile the section came from.
    void profileCombo(SetupSection section, float width);
    // Under the details of a section: Save as profile…, and Update the profile and Revert when it was changed for this
    // tournament only.
    void profileActions(SetupSection section);
    // The popups of Save as profile… (its name) and of Delete (the confirmation), once per frame.
    void drawProfilePopups();
    void applySetupProfile(SetupSection section, const SetupProfile& profile);  // to the setup, saved soon
    void saveSetupProfile(SetupSection section, const std::string& name);       // the section as profile `name`
    void saveProfiles() { storage_.saveProfiles(profiles_.toJson()); }
    std::string profile_name_;                        // Save as profile…: the name typed
    SetupSection profile_section_ = SetupSection::Format;  // the section it saves, or Delete removes from
    bool open_save_profile_       = false;                 // its popup opens on the next frame
    std::string profile_delete_;                           // the profile Delete asks to confirm, opened next frame

    // test suites
    void drawSuites();
    void drawSuiteGrid();
    void loadSuiteSetup();  // suite.json and its suite, once
    void saveSuiteSetup();  // to suite.json
    void loadSuite();
    void openSuite(const std::string& file);  // and shows the Suites page
    void startSuite();
    void exportSuiteCsv(const std::string& path);
    SuiteSetup suite_setup_;
    bool suite_loaded_ = false;  // suite.json read
    std::optional<tour::Suite> suite_;
    std::string suite_error_;
    SuiteRunner suite_runner_;
    bool suite_saved_run_ = true;                                            // the last run was kept
    std::vector<SuiteColumn> suite_saved_;                                   // saved runs added to the grid
    std::vector<std::pair<std::filesystem::path, std::string>> suite_runs_;  // the saved runs of this suite
    std::string suite_csv_;                                                  // waiting for its file name

    // books
    void drawBooks();
    void drawBookDetail(BookEntry& book);
    void drawBookPositions();
    // A CTG book of the library (#68, mockup M10, board 3): its numbers read once in the background, its lines
    // followed from the start when it is checked or copied as a Polyglot book for engines. One job at a time.
    enum class CtgJobKind { Summary, Check, Polyglot };
    void drawCtgDetail(const BookEntry& book);
    void startCtgJob(CtgJobKind kind, const std::string& path, const std::string& bin = {});
    void pollCtgJob();
    void openInTree(const std::string& book);  // the Board page's Tree tab, on that book of the library
    void addBook(const std::string& path);
    void startBookScan(int index);
    void pollBookScan();
    void cancelBookScan();
    // The Create menu of the list: books the program writes.
    void drawBookCreateMenu();
    // Where a book the program writes is offered to go, named `name`: next to the book chosen in the list, else the
    // last one of the library, else in <profile>/books (made now, for the dialog to open in).
    std::string newBookPath(const std::string& name);
    // A book the program wrote: added to the library and chosen, or read again when the library has it already.
    void addWrittenBook(const std::string& path, const std::string& description);
    // Asks where to write a Chess324 book, every position or the symmetrical ones (tour/chess324.hpp).
    void askChess324(bool symmetrical);
    void writeChess324Book(std::string path, bool symmetrical);
    bool open_book_create_ = false;  // --focus books-create: the Create menu open
    // Build a Polyglot book…: its form in the detail pane, the build in the background (tour/pgn_book.hpp).
    struct BookBuildForm {
        std::string pgn;
        int plies     = 20;
        int min_games = 3;
        bool by_score = true;
        bool rated    = false;
        int min_elo   = 2400;
    };
    void openBookBuild();  // shows the form, with the PGN book chosen in the list if it is one
    void drawBookBuild();
    void startBookBuild(std::string bin);
    void pollBookBuild();  // at each frame: the build that ended, added to the library
    [[nodiscard]] tour::PgnBookOptions bookBuildOptions() const;
    BookBuildForm book_build_form_;
    bool book_build_shown_ = false;  // the detail pane shows the form rather than the book chosen
    // A book of starting positions from Lichess's named openings (#53, mockup M8, board "Moteurs"): its form in the
    // detail pane too; what it would hold, worked out again when the options change.
    bool opening_suite_shown_ = false;
    OpeningSuiteOptions opening_suite_;
    std::optional<std::pair<OpeningSuiteOptions, std::vector<SuitePosition>>> opening_suite_preview_;
    void drawOpeningSuite();
    void writeOpeningSuiteBook(std::string path);
    // Its second source, the positions Lichess's players had analysed: Lichess's evaluations database read once,
    // while it downloads or from a file, for the positions of the named lines and one move off them, kept in the
    // profile; then the book made from them, out of an evaluation, a depth, the plies and a number.
    bool evaluated_suite_shown_ = false;
    EvaluatedSuiteOptions evaluated_suite_;
    EvalsReading evals_reading_;
    std::optional<EvalsFolder> evals_folder_;  // what the profile holds, read again when a reading stops
    std::optional<std::vector<EvaluatedPosition>> evaluated_positions_;
    std::future<std::vector<EvaluatedPosition>> evaluated_job_;  // what the profile kept, read off the frame
    std::optional<std::pair<EvaluatedSuiteOptions, std::vector<SuitePosition>>> evaluated_suite_preview_;
    std::future<EvalsSize> evals_size_job_;
    std::optional<EvalsSize> evals_size_;
    std::string evals_stopped_;  // why the last reading stopped before its end
    [[nodiscard]] std::filesystem::path evalsFolderPath() const;
    void openEvaluatedSuite();
    void startEvalsReading(std::string source);
    void pollEvalsReading();  // at each frame: a reading that stopped, what it kept read
    void drawEvaluatedSuite();
    void writeEvaluatedSuiteBook(std::string path);
    std::string book_build_error_;   // why the last build failed, shown in the form
    PolyglotBuild book_build_;

    // engines
    void refreshPrograms();
    void pollFingerprints();  // the CRCs refreshPrograms reads in the background
    // Reads the engine's options now, or first when another engine is being read.
    void startProbe(int index);
    void launchProbe(int index);  // reads it now: nothing else is being read
    // This engine's options are being read, or waiting to be.
    [[nodiscard]] bool probing(const std::string& name) const;
    void pollProbe();
    void applyProbe();  // the result of the read that ended
    // An engine of the setup: its read comes first.
    [[nodiscard]] bool inSetup(const std::string& name) const;
    // A start (of a run, a resume, the queue, a quick test) must not replace fastchess' configuration while a read or
    // a check reads it: it waits for the read of an engine it plays (`needs`), and interrupts the others, which are
    // done again later; it waits for a test suite to end, and for the PGN books it plays (`books`) to be turned into
    // Polyglot books (see startGate). Meanwhile no other read or check starts.
    // `key` names what it starts: asked again while it waits, nothing changes; asked while another start waits, it
    // is refused (see startAsked). `failed`, if any, is called instead of `start` when the read of an engine it needs
    // fails, or one of its books cannot be prepared. True when `start` was put off (or refused).
    bool startAfterReads(const std::string& key, std::vector<std::string> needs, std::vector<OwnBookNeed> books,
                         std::function<void()> start, std::function<void()> failed = {});
    // Moves a start put off on: true once it may start.
    bool advanceStart();
    void pollStart();
    [[nodiscard]] static std::string resumeKey(const std::filesystem::path& dir) { return "resume:" + dir.string(); }
    // The start put off under `key`, if any.
    [[nodiscard]] bool startPending(const std::string& key) const {
        return pending_start_ && pending_start_->key == key;
    }
    // Instead of the button that started it, the start put off under `key`: what it waits for, and Cancel. False when
    // there is none.
    bool drawStartWaiting(const char* id, const std::string& key, bool icon = true);
    // What the card of the tournament put off under `key` says it waits for; empty when there is nothing to say.
    [[nodiscard]] std::string startWaitingText(const std::string& key) const;
    void startCheck(int index);
    void startQuickTest(int index);
    // The quick test of the engine named so, put off: started once the engine is read, under its name then.
    std::function<void()> quickTestLater(const std::string& name);
    void finishQuickTest();
    void pollCheck();
    // Shows what an engine list or a folder holds, to choose the engines to add. `refreshed`: the remembered folder
    // looked in again, whose engines already in the library are left out. `list`: the engine list read, if it is one.
    void importEngines(const ImportResult& imported, const std::filesystem::path& refreshed = {},
                       const std::filesystem::path& list = {});
    void refreshEngineFolder();  // the new engines of the remembered folder, see Storage::engineFolder
    [[nodiscard]] const std::filesystem::path& engineFolder();
    void setInactive(const std::vector<std::string>& names, bool inactive);
    void drawImportPreview();
    void addImported();  // the engines chosen
    // A rating list (a list site's text or page, Ordo, CSV) whose ratings become the reference Elo of the library
    // engines it names, once seen in a preview.
    void openRatingImport(const std::string& text = {});
    void loadRatingList(const std::string& path);  // a file chosen, read into the preview
    void readRatingImport();                       // the text of the preview, read and matched again
    void drawRatingImport();
    void applyRatingImport();
    [[nodiscard]] bool engineInUse(const std::string& name) const;
    void addEngine(const std::string& path);
    std::string uniqueEngineName(const std::string& base, int ignore_index) const;
    // Renames the engine (made unique), and what refers to it by name follows: the setup and the queued tournaments,
    // the read and check queues, the quick test, the relaunch, the schedule's edit and the test suite.
    void renameEngine(int index, const std::string& name);
    void removeEngines(const std::vector<std::string>& names);
    void queueChecks(const std::vector<std::string>& names);          // checked one after the other, see pollCheck
    [[nodiscard]] std::vector<std::string> uncheckedEngines() const;  // what "Check those not checked" checks
    void askEngineList();                                             // Import > An engine list…: the file dialog

    // Programs rebuilt or replaced (view_changes.cpp).
    void showEngineChanges(const std::string& name);  // the engine's card, with what changed
    // The choice for every option of the engine's notice that waits for one; nullopt dismisses the notice. Saved.
    void settleEngineChanges(const std::string& name, std::optional<ValueChoice> choice);
    // What the new read of the engine's rebuilt program found, on its card, with the choices (disabled if `locked`).
    void drawProgramChanges(int index, bool locked);
    // The setup summary's warnings: the engines named whose program changed since their last tournament. A separate
    // function, so that the summary's other lines can change around it.
    void drawRebuiltSincePlayed(const std::vector<std::string>& names);
    // The header's notice of a resumed tournament whose participants' programs changed since they joined it.
    void drawResumedPrograms();
    std::map<std::string, PlayedProgram> played_programs_;  // by programKey(), from tournaments_
    std::vector<ParticipantProgram> resumed_programs_;      // of the tournament resumed, until Continue or Pause

    // run lifecycle
    void startRun();
    void resumeRun(bool at_launch = false);
    void resumeAtLaunch();           // the tournament running when the program ended, see RunningMarker
    void setRunPaused(bool paused);  // and the marker follows
    void stopRun(bool now);          // the user's stop: now, or once the running games are over
    [[nodiscard]] std::string runMarkerDir() const;  // the running tournament's folder, as the marker names it
    void enqueue();         // the current setup, to start after the running tournament
    // The first queued tournament, if any; with `go_on`, the next ones when it cannot start. One that cannot
    // start stays queued. `from`: the tournament it goes on from, named `label` (put off while an engine of it was
    // read, or the next after one that could not start); when the queue changed meanwhile, from the first.
    void startFromQueue(bool go_on = false, std::size_t from = 0, const std::string& label = {});
    void drawQueue();
    void drawHealth();  // the home page's: incidents of the last 24 hours, memory, engine processes
    // The engines of the open tournament that cannot start (tour::StartFailures): which one, the error, its engine
    // log, and Try again, Withdraw…, Replace…. A card of its own, under `title` when given; nothing when every engine
    // starts. The tournament header and the home page show it alike.
    bool drawStartFailures(const char* title = nullptr);
    // An engine set aside, back, or the tournament stopped for want of one: told once each, in the log, and on the
    // desktop when an engine is set aside.
    void watchStartFailures(OpenRun& open);
    // Starts the process playing the tournament in `dir` (created already, or to resume), in a new tab; an error
    // message, empty on success.
    std::string launchTournament(const std::filesystem::path& dir);
    // Those incidents by engine, the most first: what happened to it, with Open the engine and its tournament's folder.
    [[nodiscard]] std::vector<Notice> engineIncidentNotices(const tour::Incidents& incidents) const;
    // About how long the running tournament has left at its pace; nullopt without a game count or a pace yet.
    [[nodiscard]] std::optional<double> runSecondsLeft() const;
    // "Relaunch with…" of a finished match between `engine_a` and `engine_b`, played from the folder `from`: the
    // same match with another engine A. `open`: its popup opens now (the command palette).
    void drawRelaunch(const std::string& engine_a, const std::string& engine_b, const std::filesystem::path& from,
                      bool open = false);
    void resetRunView();  // forgets the games and statistics shown, before a run starts
    void clearRunView();  // the games, statistics and selections the views show, forgotten
    void pollRun();
    void onRunFinished();
    std::string runStatusText() const;
    std::string summaryText() const;
    // How the tournament's SPRT ended: H1, H0, or not by a decision (SPRT_CONTINUE: stopped, or its games over).
    // The LLR may be beyond a bound without one, before the minimum of games.
    [[nodiscard]] fastchess::SPRTResult sprtVerdict() const;

    // games
    void rebuildGameFilter();
    [[nodiscard]] bool gamesFiltered() const;  // some games of the Games tab are hidden
    void selectGame(int index);
    void exportGames(const std::string& path, bool all);
    // Export for a rating list: the dialog, then the files once their place is chosen (view_submission.cpp).
    void openSubmissionExport();
    void drawSubmissionExport();
    void writeSubmissionExport(const std::string& path);
    void drawSubmissionChecklist();  // in the overview of a finished tournament

    void handleDialogs();
    void saveSetupSoon() { setup_dirty_ = true; }

    SDL_Window* window_;
    Storage storage_;
    CpuTopology topology_;
    AppOptions options_;
    bool software_at_launch_ = false;  // the Settings page says that a change applies at the next launch

    std::vector<EngineEntry> engines_;
    MatchSetup setup_;
    // The profiles of the setup's sections (profiles.json), with the program's presets.
    ProfileStore profiles_;
    bool setup_dirty_ = false;
    std::chrono::steady_clock::time_point last_setup_save_{};

    // The tournaments open in the workspace, a tab each: those playing, each in a process of its own (RemoteRun), the
    // quick test (played here, RunController), and those opened to look at them. The views show `run_`, the tab
    // chosen; what they showed of another tab is kept with it (OpenRun) while it is not shown.
    std::vector<OpenRun> runs_;
    std::size_t shown_run_ = 0;
    RunController idle_run_;  // what the views read while no tournament is open
    Run* run_ = &idle_run_;
    [[nodiscard]] OpenRun* shownRun() { return shown_run_ < runs_.size() ? &runs_[shown_run_] : nullptr; }
    // The tab of the tournament in `dir`, -1 when none.
    [[nodiscard]] int findRun(const std::filesystem::path& dir) const;
    [[nodiscard]] int runsPlaying() const;  // tournaments playing, the quick test included
    // A tournament plays in this program (the quick test): fastchess' globals are taken, and its engines must not be
    // ended by an engine read interrupted for a start.
    [[nodiscard]] bool localPlaying() const;
    // A new tab, shown at once (the views start over from it); returns its index.
    std::size_t addRun(OpenRun run);
    // The workspace shows tab `index`: what the views showed of the tab before is kept with it.
    void showRun(std::size_t index);
    // Closes the tab of a tournament that does not play.
    void closeRun(std::size_t index);
    void stashRunView();  // what the views show of the shown tab, into its OpenRun
    void loadRunView();   // and back, then the views start over from its tournament
    void drawRunTabs();   // the tabs, above the workspace's header
    // The home page's card of a tournament playing that is not shown: what its copy and its process tell.
    home::RunCard runCardOf(std::size_t index);
    // Who leads a match, from A's point of view (`stats`, `point`): the LLR of its SPRT with the Elo and the score, or
    // the Elo of the engine ahead with the score. For its card on the home page.
    static std::pair<std::string, std::string> matchLeader(const RunInfo& info, const fastchess::Stats& stats,
                                                           const StatPoint& point, const fastchess::SPRT* sprt);
    // The controls of tab `index` on the home page's card: pause or resume, stop, its weight against the others'.
    void drawRunControls(std::size_t index);
    // The machine's budget (Settings, MachineBudget): what the tournaments share, its defaults resolved.
    MachineBudget budget_setting_;
    uint64_t installed_mb_ = 0;  // the machine's memory, read once
    [[nodiscard]] tour::Budget machineBudget() const;
    // The engine threads of the window's own engines: the quick test's games, the analysis engine of the Board page.
    [[nodiscard]] int localThreads() const;
    // The engine threads of the Board page (its analysis, its games; boardThreads()), counted with the cores the
    // tournaments share.
    [[nodiscard]] int analysisThreads() const;
    // The share of each tournament playing, by weight (budget.hpp): their allowances, sent to their processes when
    // they change. Every frame: a tournament started, ended, paused, its weight changed, the budget changed.
    void arbitrate();
    // The cores of the budget no tournament playing can take (each at its concurrency), nor the window's engines.
    [[nodiscard]] int freeCores() const;
    // The queue may start its next tournament beside those playing (MachineBudget::queue_when_free).
    [[nodiscard]] bool queueBeside(std::size_t index) const;
    // What a tournament about to start gets (with the weight `weight`), beside those playing.
    [[nodiscard]] tour::Allowance allowanceFor(const tour::TournamentSpec& spec, double weight) const;
    // Settings: the machine's budget.
    void drawMachineBudget(float label);
    int tab_select_frames_ = 0;  // the tab bar selects the tab shown (showRun() from elsewhere) for a few frames
    // What runStatusText() says of tab `index`, shown or not.
    [[nodiscard]] std::string runStatusText(std::size_t index) const;
    // Connects to the process playing the tournament in `dir` without the window (detached), in a new tab, with that
    // weight; false when none listens there.
    bool attachTournament(const std::filesystem::path& dir, double weight);
    // Why the tournament in `dir` cannot be resumed (finished, a book that cannot be used, played elsewhere); empty
    // when it can.
    [[nodiscard]] std::string resumeCheck(const std::filesystem::path& dir);
    // A tournament of a tab not shown ended: told, its queue goes on, as onRunFinished() does for the shown one.
    void onBackgroundRunFinished(std::size_t index);
    Dialogs dialogs_;
    PieceAtlas atlas_;
    Board3d board3d_;  // the Board page's, when appearance::current().board3d.on

    // Data copied from the observer of the current (or last) run.
    std::vector<GameRecord> games_;
    std::vector<StatPoint> history_;
    fastchess::Stats stats_;
    StatPoint current_;
    std::optional<fastchess::SPRT> sprt_;
    std::vector<std::string> reasons_;
    std::vector<OpeningName> openings_;
    std::string opening_filter_;  // Games tab: ECO code, range or words of the opening name
    std::vector<EngineTotals> totals_;
    std::chrono::steady_clock::time_point last_totals_{};
    double games_per_hour_      = 0;
    std::size_t games_at_start_ = 0;           // games a resumed match already had
    std::optional<tour::Summary> unfinished_;  // the last match, if it can be resumed
    std::chrono::steady_clock::time_point last_rate_update_{};

    // Live view.
    int live_slot_ = 0;
    std::vector<int> live_shown_;  // the slots the live view shows this frame (shownSlots)
    std::vector<SlotView> slot_views_;
    bool live_mosaic_       = true;
    GraphView live_graph_;    // the Live view's graph: its tab, hidden curves, folded
    GraphView replay_graph_;  // the Games page's

    // Big screen.
    struct {
        bool on = false;  // in the main window
        BigView view;
        std::chrono::steady_clock::time_point moved{};  // the mouse moved: the controls show for a few seconds
        bool made_full = false;  // the main window went full screen with it, and leaves full screen with it
    } big_;
    std::map<int, BigView> big_boards_;             // the detached boards shown as a big screen, by their slot
    std::map<int, SDL_Window*> big_board_windows_;  // the window each is in (the main one when it is docked there)
    int big_board_focused_       = -1;              // the one focused last, which F11 and Esc are for while it is
    int big_board_focused_frame_ = -1;              // the frame it was seen focused

    // Schedule tab: its rows and what is shown with them, computed again when the tournament changes (its
    // version), not every frame.
    struct ScheduleGroup {
        std::size_t first = 0, last = 0;  // rows
        uint32_t number  = 0;             // cycle, or round
        int block        = 0;             // a cycle can come in two blocks
        std::size_t done = 0, total = 0;
        bool running = false;
    };
    struct ScheduleView {
        const tour::TournamentState* tournament = nullptr;
        uint64_t version                        = ~uint64_t(0);
        std::vector<tour::ScheduleRow> rows;
        std::vector<ScheduleGroup> groups;
        std::map<uint64_t, std::string> result_of;  // by game number
        std::map<uint32_t, std::string> openings;   // the names of the openings encounters started with
        std::map<std::pair<uint32_t, uint32_t>, std::string> round_openings;  // (cycle, round), one opening per round
        std::size_t done = 0, running = 0, pending = 0;
        uint32_t last_round = 0;
    } schedule_;
    void refreshSchedule();

    // SPRT calculator: its own copy of the bounds, applied to the setup on request.

    bool sprt_calc_open_  = false;
    bool open_statistics_ = false;  // --focus statistics: the match's Statistics section opens
    bool open_pgn_setup_  = false;  // --focus setup-pgn: the setup's PGN section opens
    MatchSetup sprt_calc_;
    // The running tournament's tablebase adjudication limit (see tablebasePieces), read once per tournament.
    int tbPieces();
    const tour::TournamentState* tb_tournament_ = nullptr;
    int tb_pieces_                              = 0;
    // The verdict of the tournament's Syzygy tables (its adjudication's, else those forced on its engines) on `board`,
    // once probed; nullptr until then, or when they say nothing of it. Each position is probed once, off the GUI
    // thread, one at a time (live_tables_).
    [[nodiscard]] const TableVerdict* liveVerdict(const chess::Board& board);
    struct LiveTables {
        std::string paths;
        bool ignore50 = false;
        std::map<std::string, std::optional<TableVerdict>> known;  // by FEN; nullopt: the tables do not know it
        std::future<std::optional<TableVerdict>> job;
        std::string job_fen;
    } live_tables_;

    // Games view.
    int games_filter_     = 0;
    int games_subject_    = -1;    // a tournament: participant index whose games are shown, -1 = all
    float eval_threshold_ = 2.0f;  // pawns, for the filters on what the evaluations tell
    std::vector<int> filtered_;
    std::size_t filtered_for_ = ~std::size_t(0);
    int selected_game_        = -1;
    std::optional<ReplayGame> replay_;
    std::string replay_error_;
    std::filesystem::path replay_log_;  // the engine log of the game shown, empty when it has none
    std::vector<std::pair<uint32_t, std::string>> replay_pv_;  // its PV warnings: (ply, warning)
    int replay_ply_   = 0;                                     // number of moves played on the replay board
    int replay_followed_ = -1;                                    // replay_ply_ the move list was last scrolled to
    bool flip_replay_ = false;

    // Openings view: results by starting position.
    std::vector<tour::OpeningStats> by_opening_;
    std::size_t by_opening_for_ = ~std::size_t(0);  // games_.size() they were computed for
    int opening_show_ = 0;  // 0 all, 1 a colour won both games, 2 drawn twice, 3 decisive, 4 flagged unbalanced
    bool export_balanced_only_ = true;     // Export as EPD… leaves out the openings flagged unbalanced
    std::vector<int> opening_order_;       // rows shown, sorted
    uint64_t openings_replaced_seen_ = 0;  // the tournament's openingsReplaced() the views were refreshed for
    uint64_t network_voids_seen_ = 0;  // games voided for a machine's broken connection, as last seen
    bool opening_order_dirty_ = true;
    int selected_opening_     = -1;  // index into by_opening_
    std::vector<int> opening_games_;  // indexes into games_ of the selected opening's games
    int opening_games_for_ = -1;      // the opening they are of, -1 when to find again

    // Books view: the library, and the book shown, read in the background.
    std::vector<BookEntry> books_;
    int selected_book_ = -1;
    struct BookReading {
        int index = -1;
        std::string path;
        std::future<std::shared_ptr<tour::BookScan>> result;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::shared_ptr<std::atomic<std::size_t>> done;
    };
    std::optional<BookReading> book_reading_;
    struct CtgDone {
        study::CtgSummary summary;
        study::CtgWalk walk;
        uint64_t entries = 0, avoided = 0, lost = 0;  // the Polyglot copy's
        std::string error;
    };
    struct CtgJob {
        CtgJobKind kind = CtgJobKind::Summary;
        std::string path, bin;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::shared_ptr<std::atomic<uint64_t>> reached;
        std::future<CtgDone> done;
    };
    std::optional<CtgJob> ctg_job_;
    std::vector<std::future<CtgDone>> ctg_abandoned_;         // cancelled, still ending
    std::map<std::string, study::CtgSummary> ctg_summaries_;  // by book, this session
    std::map<std::string, std::string> ctg_failed_;           // a book that could not be read, and why
    std::string ctg_said_, ctg_said_of_;                      // the last check's or copy's report, and its book
    std::vector<std::future<std::shared_ptr<tour::BookScan>>> abandoned_scans_;  // cancelled, still ending
    std::shared_ptr<const tour::BookScan> book_scan_;                            // of book book_scan_index_
    int book_scan_index_ = -1;
    std::string book_scan_error_;
    int book_show_ = 0;  // 0 every opening, 1 those with a problem
    std::string book_search_;
    std::vector<int> book_rows_;  // openings shown
    bool book_rows_dirty_  = true;
    int book_item_         = -1;     // opening shown on the board
    int book_item_request_ = -1;     // --focus books-item=N: that opening shown once the book is read
    bool drop_mirrored_    = false;  // the cleaned copy also leaves out mirrored positions

    // Engines view.
    std::string quick_test_engine_;    // the engine of the quick test running, empty when none
    std::string quick_test_opponent_;  // empty: the engine itself
    int selected_engine_ = -1;
    // engines.json is saved once typing in the detail panel pauses, not at every key.
    std::optional<std::chrono::steady_clock::time_point> engines_save_due_;
    std::future<ProbeResult> probe_;
    int probe_index_ = -1;
    EngineEntry probe_entry_;   // the engine being read, as it was then: changed since, its result is stale
    bool probe_again_ = false;  // asked for again while it was read: its result is dropped, it is read again
    // A start put off, see startAfterReads.
    struct PendingStart : StartRequest {
        std::function<void()> start;
        std::function<void()> failed;  // instead, when the read of an engine it needs fails
        std::string waiting_for;       // the engine whose read it waits for; empty while reads are interrupted
        bool waiting_check = false;    // ... or whose check
        bool waiting_book  = false;    // ... or the participant whose book is being prepared
        bool waiting_suite = false;    // ... or the test suite
    };
    std::optional<PendingStart> pending_start_;
    std::function<void()> start_failed_;  // the `failed` of a start whose read failed, called at the next frame
    // Set to interrupt the read or the check in flight (its engine process ended too): it is done again later.
    std::shared_ptr<std::atomic_bool> probe_interrupted_, check_interrupted_;
    std::vector<std::string> probe_queue_;  // engine names waiting for a probe
    std::string probe_message_;
    std::string option_filter_;
    std::future<CheckResult> check_;
    int check_index_ = -1;
    std::vector<std::string> check_queue_;  // engine names waiting for a check
    std::atomic<bool> fingerprints_cancel_{false};
    std::future<std::vector<std::pair<std::string, tour::FileFingerprint>>> fingerprints_;  // by program
    bool refresh_programs_again_ = false;  // asked while the CRCs were read
    std::string engine_search_;
    std::string engine_group_;  // shown group, empty = all
    int engine_status_ = 0;     // shown: 0 all, 1 works, 2 failed or changed, 3 not checked, 4 missing, 5 WinBoard
    std::set<std::string> engine_marked_;    // several engines chosen (Ctrl or Shift click), by name
    std::vector<std::string> remove_names_;  // waiting for the removal to be confirmed
    std::string group_to_add_;
    std::string option_browse_;  // the UCI option a file dialog is open for
    struct ImportPreview {
        ImportResult result;
        std::string refreshed;     // the remembered folder, looked in again: only its new engines are shown
        std::string list;          // the engine list read, empty for a folder
        std::vector<char> known;   // by engine: already in the list, not added again
        std::vector<char> chosen;  // by engine: to add
    };
    std::optional<ImportPreview> import_preview_;
    std::optional<std::filesystem::path> engine_folder_;  // settings.json's, read once
    struct RatingImport {
        std::string text;  // as pasted or read from a file
        struct Row {
            std::string engine, listed;  // the library's name and the list's
            int elo_now = 0, elo = 0;
            bool chosen = true;
        };
        std::vector<Row> rows;               // the library engines the list names
        std::vector<std::string> unmatched;  // the names of the list that are no library engine
        std::size_t listed = 0;              // engines on the list
    };
    std::optional<RatingImport> rating_import_;
    bool open_rating_import_  = false;
    bool open_import_preview_ = false;

    // The engine console, while one is open.
    std::unique_ptr<EngineConsole> engine_console_;

    // The Board page, and where it was opened from (Back).
    BoardPage board_;
    struct BoardBack {
        Page page;
        std::optional<Tab> tab;
    };
    std::optional<BoardBack> board_back_;
    bool board_key_taken_  = false;  // key A served a board this frame
    bool review_when_read_ = false;  // --focus board-review: the game analysed once its file is read
    int review_ply_        = -1;     // --focus board-review-PLY: the board at that ply then
    int ply_when_read_     = -1;     // --focus board-analysis=FILE@PLY: the board at that ply once FILE is read

    // Console view.
    std::vector<Console::Line> console_lines_;
    uint64_t console_version_ = 0;
    std::string console_source_;                // the tournament whose lines are shown; empty: all; "-": the window's
    std::vector<std::string> console_sources_;  // the tournaments the lines come from
    std::vector<int> console_shown_;            // the lines shown, by index
    bool console_follow_ = true;

    // Standings of a round robin or gauntlet, recomputed when games arrive.
    tour::Table table_;
    tour::Table shown_;  // table_ in the order of the Standings tab, which its copies and export follow
    tour::TeamTable teams_;  // a team match: the teams' scores, computed with table_
    tour::Order standings_order_ = tour::Order::Points;
    std::size_t table_for_       = ~std::size_t(0);
    std::chrono::steady_clock::time_point table_next_{};  // a big table waits a while before it is computed again
    std::optional<tour::Anchor> anchor_;
    int crosstable_mode_ = 0;                             // 0 score, 1 wins/draws/losses, 2 Elo difference
    int standings_view_  = -1;  // 0 table, 1 crosstable, 2 teams (a team match), -1 as suits the tournament
    std::optional<std::pair<int, int>> crosstable_cell_;  // participant ids (row, column)
    std::optional<int> standings_engine_;  // participant id whose opponents are listed under the table
    struct SubmissionDialog {  // Export for a rating list, from the Games tab
        bool open = false;
        bool zip  = true;
        tour::SubmissionExport e;
    } submission_;
    tour::SubmissionFacts submission_facts_;  // of the checklist, for the tournament at this version()
    uint64_t submission_facts_for_ = ~uint64_t(0);
    int submission_tb_pieces_      = 0;  // the pieces of the first engine's SyzygyPath, read with submission_facts_
    std::string template_name_;
    std::vector<std::string> template_names_;  // the templates folder, listed now and then rather than every frame
    std::chrono::steady_clock::time_point templates_listed_{};
    std::string template_message_;  // why the last save or load failed
    std::string template_loaded_;   // the template the setup came from, named in the menu; empty: none
    bool open_save_template_ = false;  // Save the setup as a template… asked from the command palette
    // Templates loaded by name (the template menu, the command palette): the sections that played as their profile
    // take its values of today. False, with template_message_, when it cannot be read.
    bool loadTemplate(const std::string& name);
    void listTemplates();  // template_names_, listed again every few seconds

    // Clone: the New tournament page filled with the setup the tournament of `dir` started from, every value as it
    // played, to change before it starts. False when it has none (see readTournamentSetup).
    bool cloneTournament(const std::filesystem::path& dir);
    // Whether that setup is there, looked at once per folder: the buttons of a list ask it every frame.
    bool canClone(const std::filesystem::path& dir);
    // The Clone button of a tournament (`compact`: in a table's row), greyed with the reason when it cannot.
    void drawClone(const std::filesystem::path& dir, bool compact = false);
    std::map<std::string, bool> clone_sources_;  // canClone() by folder; cleared when the list is read again

    // Schedule tab and edits.
    int edit_kind_ = 0;        // tour::Edit::Kind
    std::string edit_engine_;  // engine list entry to add or bring in
    // Its settings of its own, when given (0, 100 %: the tournament's); otherwise the tournament's.
    bool edit_own_settings_ = false;
    ParticipantSetup edit_own_;
    std::string name_draft_of_;  // engine whose Name field is being edited, and the name typed so far
    std::string name_draft_;
    bool edit_catch_up_     = true;
    int edit_team_          = 0;   // a team match: the team a newcomer joins
    int edit_participant_   = -1;  // participant id
    bool edit_void_played_  = false;
    int edit_cycles_        = 0;
    int edit_replay_filter_ = 0;  // 0 losses on time and crashes, 1 every game of a participant
    std::string edit_message_;
    bool edit_error_ = false;
    // The games "Play games again" picks and the impact of the edit, computed again when they may have changed.
    struct EditPreview {
        std::string games_key;  // tournament version, filter, participant
        std::vector<std::pair<uint32_t, int>> games;
        std::string key;  // tournament version and the whole edit
        tour::Impact impact;
    } edit_preview_;
    // The game menu's "Set the result": the game it is open for (number()), the result chosen ("1-0", "1/2-1/2",
    // "0-1"), the reason typed, and the impact of the change, computed again when the choice or the tournament
    // changes.
    struct ResultMenu {
        uint64_t game = ~uint64_t{0};
        std::string result;
        std::string why;
        std::string key;
        tour::Impact impact;
    } result_menu_;
    // The live game menu's "End with result": the game it is open for (game_id), the result chosen, the reason, and
    // why the last try failed.
    struct EndMenu {
        std::size_t game = 0;
        std::string result;
        std::string why;
        std::string error;
    } end_menu_;
    uint64_t result_set_game_ = ~uint64_t{0};  // the game whose result was set last, while that can be undone
    uint64_t open_game_menu_  = 0;  // the game (game_id) whose menu opens in the Games tab (palette, --focus)
    int open_end_menu_        = 0;  // the slot + 1 whose board opens its "End with result" menu (palette)
    // The results of games asked for by the views (gameResult()), by number(); dropped when the tournament changes.
    std::map<uint64_t, std::optional<tour::GameResult>> result_cache_;
    uint64_t result_cache_version_ = ~uint64_t{0};
    const void* result_cache_of_   = nullptr;

    // Queue and relaunch.
    std::vector<QueuedTournament> queue_;
    std::chrono::steady_clock::time_point queue_checked_{};  // the queue looked at for cores free
    bool queue_held_    = false;  // Pause all: the queue starts no tournament until Resume all
    bool queue_blocked_ = false;  // a tournament ended while it was held: Resume all starts the next one
    std::string relaunch_engine_;
    std::filesystem::path relaunch_from_;     // the folder of the match whose Relaunch with… started last
    std::filesystem::path relaunch_request_;  // its popup opens on the home page (the command palette)

    // The home page's control room, computed again only when what it shows changed.
    struct CoresCache {
        const tour::TournamentState* of = nullptr;
        uint64_t version                = ~uint64_t(0);
        std::size_t names               = 0;
        std::vector<int> threads;  // by the observer's participant index
    } cores_cache_;
    struct LeaderCache {
        std::size_t games = ~std::size_t(0);
        Clock::time_point started{};
        std::string leader, detail;
        double games_per_hour = 0;  // the cards of the tournaments not shown
    } leader_cache_;
    // The same for the cards of the tournaments not shown, from their copy of the journal: by folder.
    std::map<std::string, LeaderCache> other_leaders_;
    std::string finish_hidden_;  // the finish message whose card under Needs action was hidden
    std::future<std::vector<home::DiskSpace>> disk_job_;
    std::vector<home::DiskSpace> disks_;
    std::chrono::steady_clock::time_point disk_asked_{};

    // Broadcast.
    Broadcaster broadcaster_;
    bool broadcast_editing_ = false;  // a folder, token or round field was being typed in at the last frame
    QrCode tunnel_qr_;                // of the tunnel's public address
    std::string tunnel_qr_text_;

    // Ratings page: every game of the profile, and what is computed from them, on a worker thread: a rating list
    // of thousands of engines takes a moment.
    struct RatingsResult {
        std::shared_ptr<const tour::GameDb> db;
        std::string anchor;  // the one used: the default when none was chosen
        double anchor_elo = 0;
        tour::RatingList list;
        tour::RatingListInfo info;  // how `list` was made
        std::string card_engine;
        tour::EngineRecord card;
        std::vector<tour::RatingPoint> card_history;
        std::string h2h_a, h2h_b;
        tour::HeadToHead h2h;
        std::set<std::string> progress_engines;
        std::map<std::string, std::vector<tour::RatingPoint>> progress;
    };
    std::future<RatingsResult> ratings_job_;
    std::shared_ptr<const tour::GameDb> game_db_ = std::make_shared<tour::GameDb>();
    bool game_db_dirty_ = true;  // read the tournaments again
    std::vector<std::string> game_db_library_;  // the library's names the PGN files' players were matched to
    tour::LibraryAliases game_db_aliases_;      // and the names the user gave to library engines
    bool open_pgn_players_ = false;
    // Open: the choices made in it are read with the games once it closes, not at each one.
    bool pgn_players_shown_ = false;
    // A PGN file added: 1, the games are to be read with it; 2, they are being read. Once read, the players that
    // two library engines may be, or one by a hint, are shown for the user to choose (see reviewPgnPlayers()).
    int review_pgn_players_ = 0;
    std::set<std::string> pgn_players_before_;  // the players offered before the file was added, folded
    std::optional<std::vector<std::string>> separate_players_;  // settings.json's, read once
    std::vector<std::string>& separatePlayers();
    std::string pgn_players_filter_;
    bool ratings_dirty_ = true;  // compute again, with other filters
    std::optional<std::vector<std::filesystem::path>> rating_pgns_;  // settings.json's, read once
    const std::vector<std::filesystem::path>& ratingPgns();
    void setRatingPgns(const std::vector<std::filesystem::path>& files);
    tour::RatingList rating_list_;
    tour::RatingListInfo rating_info_;  // how it was made, for the header of its exports
    bool rating_copy_header_ = false;   // the header on top of the copies too
    std::vector<int> rating_order_;  // the rows of the list as shown, sorted by a column
    bool rating_order_dirty_ = true;
    std::string rating_tc_;      // time control shown, empty = all
    std::string rating_anchor_;  // engine whose rating is fixed, empty = average 0
    double rating_anchor_elo_ = 0;
    std::string rating_group_;  // only the engines of this library group, empty = all
    int rating_period_    = 0;  // 0 every game, 1 the last 30 days, 2 the last 365 days
    int rating_min_games_ = 0;
    bool rating_external_ = true;  // with the games of the imported PGN files
    std::optional<int> ratings_tab_request_;
    std::string card_engine_;  // Engine tab
    tour::EngineRecord card_;
    std::vector<tour::RatingPoint> card_history_;
    struct SearchAverages {
        bool read      = false;
        uint64_t moves = 0;
        double depth = 0, nps = 0, time_ms = 0;
    } card_search_;
    std::string h2h_a_, h2h_b_;  // Head to head tab
    tour::HeadToHead h2h_;
    std::set<std::string> progress_engines_;  // Progress tab
    std::map<std::string, std::vector<tour::RatingPoint>> progress_;
    std::optional<uint64_t> select_game_id_;  // a game to show once its tournament is open

    // Shell.
    Page page_ = Page::Home;
    std::optional<Tab> tab_request_;  // tab to select on the next frame
    Tab shown_tab_ = Tab::Overview;   // the tab of the panel focused last, for the guide
    // The panels of the workspace (view_panels.cpp).
    std::set<std::string> panels_open_;
    bool panels_loaded_  = false;
    bool layout_checked_ = false;                   // the dock space was found usable, or got the default layout
    std::optional<layout::Layout> layout_request_;  // built on the next frame of the workspace
    std::set<std::string> redock_;                  // shown again after being closed: docked in the workspace
    std::optional<std::string> panel_request_;      // focused and brought to the front once shown
    int panel_request_frames_ = 0;                  // the frames it was shown without being in front
    std::set<std::string> fronts_;                  // to bring to the front of their dock node (a layout built)
    std::optional<layout::Layout> built_;           // the layout built last, its tabs put in order for a few frames
    int built_frames_ = 0;                          // the frames of the workspace since
    std::vector<layout::Placement> placements_;     // panels floating alone, placed at their next frame
    std::string panel_focused_;                     // the panel last focused, for the guide
    std::vector<layout::Layout> layouts_;           // the user's own, kept in layouts.json
    bool open_save_layout_ = false;
    bool open_layout_menu_ = false;  // --focus layout-menu
    std::string layout_name_;        // being typed in Save the layout as…

    std::vector<tour::Summary> tournaments_;
    bool tournaments_dirty_  = true;
    std::size_t legacy_runs_ = 0;
    // The list read on a worker thread, each journal again only when it changed.
    struct TournamentList {
        std::vector<tour::Summary> summaries;
        std::size_t legacy_runs = 0;
    };
    std::future<TournamentList> tournaments_job_;
    std::shared_ptr<tour::SummaryCache> summary_cache_ = std::make_shared<tour::SummaryCache>();
    bool tournaments_read_                             = false;  // once at least
    // The health panel's incidents, read on a worker thread now and then.
    std::shared_ptr<tour::IncidentScanner> incident_scanner_ = std::make_shared<tour::IncidentScanner>();
    std::future<tour::Incidents> incidents_job_;
    std::optional<tour::Incidents> incidents_;
    std::chrono::steady_clock::time_point incidents_asked_{};
    std::optional<ui::Theme> theme_applied_;
    int zoom_applied_ = 0;

    // Messages and state.
    std::string start_error_;
    std::string finish_message_;
    // What fixes them (an engine to open, a program to locate…), while they are the messages they were found for;
    // Open the log for any other start error. See drawMessage().
    std::vector<Fix> start_fixes_, finish_fixes_;
    std::string start_fixes_for_, finish_fixes_for_;
    // A start or end message as a notice (red when `error`), with its fixes when `fixes_for` is still `text`.
    void drawMessage(const std::string& text, const std::vector<Fix>& fixes, const std::string& fixes_for, bool error,
                     const char* id);
    void setStartFixes();        // the fixes of the setup's errors, after a start that failed
    void setFinishFixes();       // the engine that stopped the tournament, at its end
    std::string stopped_by_;     // that engine, as the tournament names it; empty when none did
    uint64_t console_mark_ = 0;  // the log's lines before the tournament started (Console::added())
    std::string window_title_;
    bool quit_            = false;
    bool quit_pending_    = false;
    bool open_quit_modal_ = false;
    bool focus_done_      = false;
    bool autostart_done_  = false;
    bool launch_checked_  = false;  // resumeAtLaunch() done
    bool user_stop_       = false;  // the user stopped the running tournament (not by quitting)
    bool resume_guarded_  = false;  // resumed at launch, the marker says so until it has played a while
    std::filesystem::path resume_crashed_;  // the tournament whose resume at launch ended the program

    // New versions (update_check.hpp): the check at launch, a notice on the home page, the version in Settings.
    void startUpdateCheck(bool asked);  // `asked`: from Settings, its result shown there
    void pollUpdateCheck();
    void drawUpdateNotice();
    void drawVersionSettings();
    void copyVersion();
    update::Settings updates_;
    update::Checker update_checker_;
    bool update_asked_ = false;  // the check running was asked from Settings
    std::string update_status_;  // what the check asked from Settings found
    bool update_notes_open_ = false;

    // "Report a problem…" (report.hpp): a new GitHub issue in the browser, filled with the version, the system and
    // the log; `seen`, when given, fills what went wrong.
    void reportProblem(const std::string& seen = {});
    bool reportButton(const char* id, bool small = false, const std::string& seen = {});  // the button, its tooltip
    [[nodiscard]] std::string systemReport() const;

    // Appearance (view_appearance.cpp): the boards' colours, the pieces and the move sounds, in Settings; the
    // engines' logos, on their cards.
    void loadAppearance();  // at startup, from settings.json
    // `settings` in force and saved; the pieces made again at the start of the next frame when they changed.
    void applyAppearance(const appearance::Settings& settings);
    void rebuildPieces();  // between frames: the last frame may still draw the old atlas
    void drawAppearanceSettings(float label);
    void drawBoardPreview(ui::Theme theme, float size);
    // A sound for each new move of the game in `slot`, when it is the one followed (Live, the big screen).
    void hearMoves(int slot);
    bool pieces_dirty_ = false;
    std::string pieces_error_;  // why the folder's pieces are not those drawn, empty when they are
    // settings.json written a moment after the last change: not at every step of a colour dragged in its picker.
    std::optional<std::chrono::steady_clock::time_point> appearance_save_due_;
    ui::Theme appearance_theme_ = ui::Theme::Light;  // the theme whose board the Settings page edits
    bool appearance_theme_set_  = false;             // chosen once, then left as the user put it
    bool scroll_to_appearance_  = false;             // asked from the command palette
    struct Heard {
        std::size_t game  = 0;
        std::size_t plies = 0;
        int frame         = -1;  // when: a game followed again after a while makes no sound for what it played since
    };
    std::map<int, Heard> heard_;  // by slot
    int heard_frame_ = -1;        // the frame a sound was last played: one at a time

    // The engines' logos, as textures made when first drawn; by file name in the logos folder. A small one too, for
    // SDL's renderer, which has no mipmaps to draw the large one small.
    struct LogoTexture {
        ImTextureID large = ImTextureID_Invalid;
        ImTextureID small = ImTextureID_Invalid;
        int width = 0, height = 0;  // the large one's: the proportions
        std::string error;          // why it cannot be shown, empty when it can
        [[nodiscard]] ImTextureID texture(float pixels) const;
    };
    [[nodiscard]] const LogoTexture* engineLogo(const std::string& engine);  // nullptr: none, or unreadable
    // The engine's logo as an item `height` high, its proportions kept; its width, 0 (and nothing drawn) without one.
    float drawEngineLogo(const std::string& engine, float height);
    void drawLogoRow(int index, float label);  // the engine card's row
    void setEngineLogo(int index, const std::string& picture);
    void removeEngineLogo(int index);
    void forgetLogoFile(const std::string& file);  // its texture freed, the file removed when no engine has it
    std::map<std::string, LogoTexture> logo_textures_;
    std::vector<ImTextureID> stale_textures_;  // freed at the start of the next frame: this one may draw them
    std::string logo_error_;      // the last choice of a logo that failed
    std::string logo_error_for_;  // the engine whose card shows it
    int logos_version_     = 0;   // changed with each logo set or removed
    int web_logos_version_ = -1;  // the version the web page's logos were made from

    // Remote machines (view_machines.cpp, machines.hpp): their cards in Settings, the jobs running on them, and the
    // passphrases of key files typed this session, in memory only.
    struct MachineActivity {
        std::unique_ptr<MachineJob> job;       // a test, Install, the engines' check, the files compared
        std::unique_ptr<MachineJob> transfer;  // books and tablebases copied, which may take hours, beside the others
        std::optional<JobResult> last;         // what the last job found, shown on the card
        MachineJob::Kind last_kind = MachineJob::Kind::Test;
        std::optional<JobResult> last_transfer;
        std::optional<FilesCheck> files;  // the last comparison, what a push sends
        std::string message;              // what keeps a job from starting (the machine incomplete, a key file)
        bool remove_asked = false;
        // An engine copied there (Copy to the machine, Replace with this computer's): the copy offered, its panel
        // open under the engines, then copied by the transfer job; and what the last copy said.
        std::optional<EngineCopy> copy;
        std::optional<JobResult> last_copy;
        bool open_engines  = false;  // the engines' list opened at the next frame (a copy asked from elsewhere)
        int scroll_to_copy = 0;      // and the page scrolled to the copy's panel, these frames (the card grows first)
    };
    void loadMachines();
    void saveMachines();
    void forgetPassphrases();  // wiped
    void pollMachines();
    void drawMachineSettings(float label);
    void drawMachineCard(std::size_t index);
    void drawMachineEngines(Machine& machine, MachineActivity& activity);
    void drawMachineFiles(Machine& machine, MachineActivity& activity);
    void drawEngineCopy(Machine& machine, MachineActivity& activity);
    // The copy of the library's engine `engine` to the machine `id` offered on its card (Settings opened there),
    // `build` the file chosen for the machine instead of the program known.
    void offerEngineCopy(const std::string& id, const std::string& engine, const std::filesystem::path& build = {});
    // This computer's program of each engine of the machine's list, for the builds' comparison.
    [[nodiscard]] std::map<std::string, std::filesystem::path> machinePrograms(const Machine& machine) const;
    // The format of a program of this computer, read again when the file changes.
    [[nodiscard]] const ProgramFormat& programFormatOf(const std::filesystem::path& program,
                                                       const tour::FileFingerprint& stamp);
    std::map<std::string, std::pair<tour::FileFingerprint, ProgramFormat>> program_formats_;
    float copy_panel_y_ = -1;  // where the copy's panel was drawn, the page scrolled there once (screen, -1: no)
    void drawPassphrasePrompt();
    // Starts a job on the machine `id`, its key's passphrase asked first when it has one not typed yet.
    void startMachineJob(const std::string& id, MachineJob::Kind kind);
    void addMachine();
    [[nodiscard]] Machine* machineById(const std::string& id);
    [[nodiscard]] FilesRequest setupFiles() const;  // the setup's books and tablebases
    // Tournaments on a machine: the one in `dir` started (or resumed) there and shown in its tab, or the window
    // connected to the one playing there already. An error message, empty on success.
    std::string launchOnMachine(const std::filesystem::path& dir, const std::string& machine_id);
    bool attachOnMachine(const std::filesystem::path& dir, double weight);
    // The tab of a tournament on a machine, its process reached through `link`.
    std::string openMachineRun(const std::filesystem::path& dir, const Machine& machine, const std::string& remote_dir,
                               std::unique_ptr<SshLink> link, std::shared_ptr<LinkStatus> status, double weight);
    // The passphrase of a machine's key, empty when it needs none or it was not typed yet (`missing` then).
    [[nodiscard]] std::string machinePassphrase(const Machine& machine, bool& missing);
    void pollMachineRuns();       // their files' last copy once they end, what their start said
    void arbitrateMachines();     // each machine's budget shared by its tournaments
    void drawRunOn(float label);  // the setup's Run on, its label `label` wide
    std::vector<Machine> machines_;
    std::map<std::string, MachineActivity> machine_activity_;  // by machine id
    std::map<std::string, std::string> machine_passphrases_;   // by key file
    std::string machine_open_;        // the machine whose card is open
    std::string machine_dialog_for_;  // the machine a file dialog was opened for
    std::string machine_dialog_engine_;  // and the engine, for its build for the machine
    std::string passphrase_machine_;  // the machine whose key's passphrase is asked, and the job then started
    MachineJob::Kind passphrase_kind_ = MachineJob::Kind::Test;
    bool open_passphrase_             = false;
    std::array<char, 256> passphrase_input_{};
    std::function<void()> passphrase_then_;  // run once the passphrase is typed, instead of a machine job

    // The engines of the library that run on a machine (engine_hub.hpp): the window's hub, for their reading, checks,
    // console, quick test, analysis and games here; started with the first such engine.
    std::unique_ptr<EngineHub> hub_;
    std::string hub_signature_;  // the engines it was last told it may start
    void syncEngineHub();
    // Before an engine that runs on a machine starts: false when its key's passphrase must be typed first (asked
    // then, `then` run once it is).
    bool remoteReady(const EngineEntry& e, std::function<void()> then);
    // A passphrase typed or refused, for the machine jobs and the hub alike (empty: forgotten).
    void rememberPassphrase(const std::string& key_file, const std::string& passphrase);
    // Why a read or a check of `e` failed, told by the hub when its machine could not be reached.
    [[nodiscard]] std::string remoteFailure(const EngineEntry& e) const;
    // Under the home page's cores: the machines the running tournaments' engines run on, and how they do.
    void drawRemoteMachinesLine();
    // The library's engine of that name, -1 when there is none (any more).
    [[nodiscard]] int engineIndex(const std::string& name) const;
    // The passphrases a tournament's engines on machines need, by key file, as far as they were typed.
    [[nodiscard]] std::map<std::string, std::string> secretsFor(const tour::TournamentSpec& spec);
    // Before a start: false when a machine of the setup's engines needs its key's passphrase first (asked, and
    // `then` run once typed).
    bool remoteEnginesReady(const std::vector<std::string>& engines, std::function<void()> then);

    // The card's Runs on: this computer, or a machine; shown once there are machines (or the engine runs on one).
    // True when it changed the engine.
    bool drawRunsOn(EngineEntry& e, float label);
    // `e` moved to `machine` (empty: this computer): the program there, or this computer's, taken back.
    void moveEngine(EngineEntry& e, const std::string& machine);
    // The program of an engine that runs on a machine, written in that machine's list of engines.
    void mirrorOnMachine(const EngineEntry& e);
    // A new engine of the library: the program `path` on `machine`, read at once.
    void addRemoteEngine(const std::string& machine, const std::string& path);
    // What is known of a machine an engine runs on, in a few words, and their colour.
    struct MachineLine {
        std::string text;
        unsigned color = 0;
    };
    [[nodiscard]] MachineLine machineLine(const std::string& id) const;

    // The files of a machine, looked through over SFTP in the page: to add an engine there, choose its program, its
    // folder, a file its options name, or the program of the machine's list of engines.
    struct RemoteListing {
        std::string home;  // the user's home folder there, shown as ~
        std::string dir;   // where the server says it is
        std::string file;  // the path typed is a file: it is chosen
        std::vector<RemoteEntry> entries;
        std::string error;
    };
    struct RemotePicker {
        enum class Purpose { AddEngine, ImportFolder, Program, Folder, Option, OptionFolder, MachinePath };
        Purpose purpose = Purpose::AddEngine;
        std::string machine;  // its id
        std::string engine;   // the engine it chooses for
        std::string option;   // Option: its name
        bool folders = false;  // a folder is chosen, not a file
        bool shown   = false;
        bool untested = false;  // the machine's host key is not known yet
        bool hidden   = false;  // the files whose name starts with a dot shown
        std::string home;  // known after the first listing
        std::string dir;
        std::string selected;
        std::array<char, 1024> typed{};
        std::vector<RemoteEntry> entries;
        std::string error;
        std::future<RemoteListing> listing;
    };
    std::optional<RemotePicker> remote_picker_;
    // The SHA-256 of the programs of engines that run on machines, read there in the background: a program rebuilt
    // there is noticed as a local one is (ProgramState::Changed, read again).
    struct RemoteHash {
        std::string engine, machine, program, hash;
    };
    std::future<std::vector<RemoteHash>> remote_hashes_;
    std::vector<std::string> remote_hashes_next_;  // asked for while others are read
    void hashRemotePrograms(std::vector<std::string> names);
    void pollRemoteHashes();
    void openRemotePicker(RemotePicker::Purpose purpose, const std::string& machine, const std::string& engine = {},
                          const std::string& option = {}, const std::string& start = {});
    void listRemote(const std::string& dir);
    void drawRemotePicker();
    void chooseRemote(const std::string& path);
    bool scroll_to_machines_    = false;  // asked from the command palette
    int machines_scroll_frames_ = 0;      // the frames it was asked on
};

}  // namespace fcg
