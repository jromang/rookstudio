#pragma once

// What a module of RookStudio's asks of the program around it (#107). A module holds an area of its own out of App
// (its state, its poll(), its draw(), its palette commands and --focus words: Lichess TV, lichess_tv_page.hpp; the
// arenas and Swiss tournaments, lichess_arenas_page.hpp; the Lichess bot, lichess_bot_page.hpp; the puzzles,
// puzzles_page.hpp; the user's mistakes as exercises, mistakes_page.hpp; guessing the moves of a game, guess_page.hpp),
// and reaches the rest through the facets below instead of App's members: the program (the frame, the profile and its
// databases, the jobs), the pages it opens, the databases' pages, the Board, the mosaic its games show in, the Lichess
// settings, the user's Lichess account and its link, the user's games on Lichess and the fair play their live game
// holds, the engines and the cores, the Board taken by a game followed live, the tokens given up, the window, the Board
// taken by a training session (board_session.hpp) and a game replayed on it, the Training page and its day's review,
// the user's mistakes, the file dialogs, the user's names, the analysis engine, the games reviewed in the background,
// the repertoires' positions, the games opened on the Board from a session, the Board's own game. A module's context
// is the facets it needs (TvContext, ArenasContext, BotContext, PuzzlesContext, MistakesContext, GuessContext); App
// answers them all (AppContext, by App::ModulesContext, app_chess.cpp), and a test only those of the module it builds,
// with a fake of its own, without App.
//
// Each method says what a module needs, in its words, not which member of App gives it. The facets hold only what the
// modules out of App need now, and grow with those that follow (the other kinds of training): a facet a module does not
// ask for is none of its fake's business.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <chess.hpp>

#include "board_session.hpp"

namespace fcg {

enum class ReviewKind;
struct AnalysisSettings;
struct BoardDecor;
struct BookEntry;
class CrosstableStore;
class DatabaseShelf;
struct EngineEntry;
class JobGroup;
struct LichessSettings;
class PieceAtlas;
struct ReplayGame;
class ReviewRunner;
struct TrainingSettings;
namespace lichess {
struct Account;
struct BoardState;
struct Failure;
struct OnlinePlaySettings;
struct PlayingGame;
struct Profile;
struct Reply;
}  // namespace lichess

// An entry of the command palette (Ctrl+K), as App::paletteEntries() makes them: add(label, where, run).
using PaletteAdd = std::function<void(std::string, std::string, std::function<void()>)>;

namespace context {

// The program around a module: the frame and its time, the keys, the profile and its databases, a toast, the jobs.
class Program {
   public:
    virtual ~Program() = default;

    // The frame being drawn (ImGui's count): what was drawn a few frames ago is still shown.
    [[nodiscard]] virtual int frame() const = 0;
    // The time the frames count, in seconds (ImGui's): a few seconds' wait measured as the window measures them.
    [[nodiscard]] virtual double seconds() const = 0;
    // A shortcut, a key chord (ImGuiKeyChord: ImGuiMod_Alt | ImGuiKey_P), pressed in this frame while no text is
    // being typed.
    [[nodiscard]] virtual bool shortcutPressed(int chord) const = 0;
    // Scripted screenshots: nothing of theirs is kept in the profile.
    [[nodiscard]] virtual bool scripted() const = 0;
    // The profile's folder, where a module keeps its files.
    [[nodiscard]] virtual std::filesystem::path profileDir() const = 0;
    // The profile's databases.
    [[nodiscard]] virtual DatabaseShelf& shelf() = 0;
    // Said a moment at the Board's corner.
    virtual void toast(const std::string& text) = 0;
    // The program's jobs (jobs.hpp, #105): a task nobody waits for any more parked there until it ends, waited for at
    // the end.
    [[nodiscard]] virtual JobGroup& jobs() = 0;
};

// The pages a module opens, or asks whether they show.
class Pages {
   public:
    virtual ~Pages() = default;

    virtual void showLichessPage() = 0;  // the Lichess page itself, not a mosaic
    virtual void showBoardPage()   = 0;
    virtual void toStudyBoard()    = 0;  // the Board, in a view that shows the study's
    // The Play page's "On Lichess › Tournaments" part (#95); `kept`: chosen by the user, kept in the settings as the
    // parts shown last.
    virtual void showPlayTournaments(bool kept)             = 0;
    [[nodiscard]] virtual bool playTournamentsShown() const = 0;
    [[nodiscard]] virtual bool homeShown() const            = 0;  // the Chess space's Home page
};

// The databases' pages, and what their crosstables keep.
class Databases {
   public:
    virtual ~Databases() = default;

    // The settings of the databases' crosstables (#92), their results set by hand among them.
    [[nodiscard]] virtual CrosstableStore& crosstables() = 0;
    // A database's games on the Databases page, through the quick filter `filter`.
    virtual void showDatabaseGames(std::size_t database, const std::string& filter) = 0;
    // A database's tournaments on the Databases page, the crosstable of `tournament` opened (`base`: the database's
    // name).
    virtual void openCrosstable(std::size_t database, const std::string& tournament, const std::string& base) = 0;
};

// The Board: what its game is, and the game it follows live from Lichess.
class Board {
   public:
    virtual ~Board() = default;

    // Its game's title; the address of the game it follows live (empty when none).
    [[nodiscard]] virtual std::string boardTitle() const   = 0;
    [[nodiscard]] virtual std::string boardFollows() const = 0;
    // `game` on the Board at its last move, said by `title`, then followed live as its moves come (`url`, `version`:
    // the follower's game and how far it went), the way back to the mosaic `mosaic`; `in_place`: the page shown and
    // the Board's way back stay as they are.
    virtual void followOnBoard(ReplayGame game, std::string title, std::string mosaic, std::string url,
                               uint64_t version, bool in_place) = 0;
};

// The broadcasts' mosaic (view_broadcast_mosaic.cpp), where the rounds followed, the followed players' games and the
// modules' show, and the game streams of this address its games take.
class Mosaic {
   public:
    virtual ~Mosaic() = default;

    // The key of the one shown (empty: the Lichess page itself); the frame that drew it last; one opened; the Lichess
    // page itself again, the mosaic's boards gone; the way back to it from the Board; the engine that analyses its
    // games, the user's choice.
    [[nodiscard]] virtual const std::string& mosaicShown() const = 0;
    [[nodiscard]] virtual int mosaicDrawn() const                = 0;
    virtual void openMosaic(const std::string& key)              = 0;
    virtual void closeMosaic()                                   = 0;
    [[nodiscard]] virtual bool boardBackToMosaic() const         = 0;  // the Board's game came from one still there
    virtual void backToMosaic()                                  = 0;
    virtual void setMosaicEngine(const std::string& engine)      = 0;
    // The game streams of this address, eight at most (lichess::MOST_GAME_STREAMS): those the others hold now, and the
    // followed players' share, set by the module that shares them out.
    struct GameStreams {
        std::size_t rounds  = 0;  // the rounds followed, one each
        std::size_t players = 0;  // the followed players' games, open
        std::size_t tv      = 0;  // Lichess TV's games, open (#86)
        std::size_t tour    = 0;  // a Lichess tournament's games, open (#95)
    };
    [[nodiscard]] virtual GameStreams gameStreams() const = 0;
    virtual void setPlayersQuota(std::size_t streams)     = 0;
};

// Lichess: the user's settings, saved; the user's own accounts, lowercase (their linked account, their bot's).
class Lichess {
   public:
    virtual ~Lichess() = default;

    [[nodiscard]] virtual LichessSettings& lichessSettings()       = 0;
    virtual void saveLichessSettings()                             = 0;
    [[nodiscard]] virtual std::vector<std::string> ownLichessIds() = 0;
};

// The user's own Lichess account (#45): its name, its token and what it may do; none while no account is linked. A
// module of another account keeps apart from it (the bot's, #85).
class LinkedAccount {
   public:
    virtual ~LinkedAccount() = default;

    [[nodiscard]] virtual const lichess::Account* lichessAccount() const = 0;
};

// The link of the user's Lichess account (#45): a link made again to ask for more, the question the Lichess page's card
// asks; the account's profile (#46), the user's figures.
class Account {
   public:
    virtual ~Account() = default;

    // A link under way: Lichess's page open in the browser, its answer awaited; why the last link failed.
    [[nodiscard]] virtual bool lichessLinking() const                      = 0;
    [[nodiscard]] virtual const lichess::Failure& lichessLinkError() const = 0;
    // The link made again, asking for `scopes` (those granted and one more), Lichess offering `username`'s account:
    // the token it replaces is revoked once the new one is kept.
    virtual void startLichessLink(std::vector<std::string> scopes, std::string username) = 0;
    // The permission the Lichess page's card asks for (empty: none); asked there; its question gone.
    [[nodiscard]] virtual std::string scopeAsked() const = 0;
    virtual void askScope(const std::string& scope)      = 0;
    virtual void dropScopeAsk()                          = 0;
    // The profile, none until read; read again when ten minutes old (nothing asked while scripted); asked once when
    // none is known, for the puzzle rating it holds (#80), kept a day.
    [[nodiscard]] virtual const lichess::Profile* lichessProfile() const = 0;
    virtual void askLichessProfile()                                     = 0;
    virtual void askLichessProfileOnce()                                 = 0;
};

// The game the user plays on the Board through Lichess's Board API (#84), as App holds it, for the frame: its id, the
// user's side, whether the Board shows it, whether its end was handled, its state as its stream told it.
struct OnlineGame {
    const std::string& id;
    bool white;
    bool shown;
    bool ended;
    const lichess::BoardState& state;
};

// Fair play (#48): a live game of the user's is being played on Lichess, or held between two (an arena's, #95): every
// aid is locked, and a module shows no engine's search (the bot's, #85).
class FairPlay {
   public:
    virtual ~FairPlay() = default;

    [[nodiscard]] virtual bool liveGameOn() const = 0;
};

// The user's games on Lichess: those in progress (#48), the one played on the Board (#84), the Play page's settings of
// them, those that ended and were kept (#49).
class Playing {
   public:
    virtual ~Playing() = default;

    [[nodiscard]] virtual const std::vector<lichess::PlayingGame>& playingGames() const = 0;
    [[nodiscard]] virtual std::optional<OnlineGame> onlineGame() const                  = 0;
    virtual void backToOnlineGame()                                                     = 0;
    // The opponent of the game on the Board, as said there: "Partner_3 (1836)".
    [[nodiscard]] virtual std::string onlineOpponent() const = 0;
    // A request about the game on the Board, made with the token; refused, said where the user plays, as `what`.
    virtual void onlineCall(std::string what, std::function<lichess::Reply(const std::string& token)> call) = 0;
    // The Play page's settings of the games on Lichess, saved.
    [[nodiscard]] virtual lichess::OnlinePlaySettings& onlineSettings() = 0;
    virtual void saveOnlineSettings()                                   = 0;
    // The user's games of a tournament that ended while it went on, kept with their review held until its end: how
    // many; whether the last came from Lichess whole (it may be reviewed); the last reviewed, the others let go; all
    // let go, their review offered no more.
    [[nodiscard]] virtual std::size_t heldGames(const std::string& tournament) const = 0;
    [[nodiscard]] virtual bool lastHeldArrived(const std::string& tournament) const  = 0;
    virtual void reviewLastHeld(const std::string& tournament)                       = 0;
    virtual void letGoHeld(const std::string& tournament)                            = 0;
    // RookStudio closes, the question asked when closing during a live game answered as by "Close anyway".
    virtual void closeAnyway() = 0;
};

// The engines: the library's, its books, the tables the tournaments give them; the cores of this computer the
// tournaments leave (the core budget); the Engines page.
class Engines {
   public:
    virtual ~Engines() = default;

    // The library's engines and books (the Engines and Books pages).
    [[nodiscard]] virtual const std::vector<EngineEntry>& engines() const = 0;
    [[nodiscard]] virtual const std::vector<BookEntry>& books() const     = 0;
    // The tournaments' tables (their engines' SyzygyPath; empty: none).
    [[nodiscard]] virtual std::string tournamentTables() const = 0;
    // A tournament plays on this computer (not on a machine of Settings > Machines); the cores of the budget that the
    // tournaments and the window's engines leave free.
    [[nodiscard]] virtual bool tournamentPlaysHere() const = 0;
    [[nodiscard]] virtual int freeCores() const            = 0;
    // The engine whose card the Engines page shows (none: another page, or no card); the Engines page shown, the card
    // of the library's engine `engine` selected when given (-1: none), the engines marked several at once let go when
    // `alone`.
    [[nodiscard]] virtual const EngineEntry* engineShown() const       = 0;
    virtual void showEngineCard(std::optional<int> engine, bool alone) = 0;
};

// The Board, taken by a game played elsewhere and followed move by move (the Lichess bot's, #85), and how it writes and
// draws a game.
class LiveBoard {
   public:
    virtual ~LiveBoard() = default;

    // The Board takes such a game: the one it showed goes among the recent games with its changes, its edits, analysis
    // and selection let go, the user's own game on Lichess goes on out of sight; an empty game from the start, titled
    // `title`, followed at its last move.
    virtual void takeBoard(const std::string& title) = 0;
    // Its game as it goes on (`chess960`: in that variant), at its last move unless the user went back in it; its
    // title, once known (empty: as it was); the side below, when `flip` is given.
    virtual void updateBoard(ReplayGame game, bool chess960, const std::string& title, std::optional<bool> flip) = 0;
    // The Board page, in a view that holds the Board (the study's, when the view shown holds none), without a way back
    // to the page before.
    virtual void showLiveBoard() = 0;
    // A move in SAN as the Board writes it (in the Chess space, its pieces' letters in the reader's language); the
    // pieces the boards are drawn with.
    [[nodiscard]] virtual std::string boardSan(std::string san) const = 0;
    [[nodiscard]] virtual const PieceAtlas& pieces() const            = 0;
};

// The Lichess tokens given up (one replaced by another, one RookStudio received for an account it refuses): revoked in
// the background, a failure said in the console, waited for at the end.
class Tokens {
   public:
    virtual ~Tokens() = default;

    virtual void revokeToken(std::string token) = 0;
};

// The window: back in front of the others (the browser, once Lichess answered there).
class Window {
   public:
    virtual ~Window() = default;

    virtual void raiseWindow() = 0;
};

// The Board taken by a training session (#76, #80–#82; board_session.hpp): one session at a time holds it, whatever its
// kind; its positions are the session's, the moves the user plays on it go to the session (BoardSession), and the page
// and its column are the session's, drawn with the Board's own parts; the analysis is cut while a question is asked; at
// the session's end, the Board is given back as it was.
class SessionBoard {
   public:
    virtual ~SessionBoard() = default;

    // Why a session cannot start now (a game of the user's played on Lichess, a game played on the board), empty when
    // it can.
    [[nodiscard]] virtual std::string sessionLock() const = 0;
    // The Board taken for a session: the session that holds it closed first (the day's review going on), the game it
    // shows put among the recent games (Ctrl+Tab), an empty game titled `title` on it, in a view that holds the study's
    // Board, without a way back to the page before; `tree`: the column at the Tree's tab rather than the notation's
    // editing (the session's moves are no game). What the Board was, to give it back at the end.
    virtual BoardTaken takeBoard(const std::string& title, bool tree) = 0;
    // The Board given back at the session's end: the game put aside on it again (else a new game), the side below and
    // the column's tab as they were, the session's position forgotten; the Training page shown (`to_training`), else
    // the Board page.
    virtual void giveBoardBack(const BoardTaken& taken, bool to_training) = 0;
    // The session left because another game comes on the Board (BoardSession::sessionDropped()): the side below and
    // the column's tab as they were, the session's position forgotten, the game put aside staying among the recent
    // ones.
    virtual void leaveBoard(const BoardTaken& taken) = 0;
    // The Board page shown again, with the session ("Back to the series").
    virtual void showSession() = 0;

    // The position the Board shows: its game, at `ply`; a position of the session's (`game` at its last move, the
    // user's selection, drag and typed move, the Board's history let go); a move played at the position shown, which
    // is shown then; the side below.
    [[nodiscard]] virtual const ReplayGame& sessionGame() const = 0;
    [[nodiscard]] virtual int sessionPly() const                = 0;
    virtual void showSessionGame(ReplayGame game)               = 0;
    virtual void playSessionMove(chess::Move move)              = 0;
    virtual void setFlip(bool flip)                             = 0;
    // The piece the user picked up let go; the move they are typing let go (the question answered, or over).
    virtual void dropPick()      = 0;
    virtual void dropTypedMove() = 0;

    // The page: its title and its guide's `?`, as the Board page's, the letters typed reaching the move field whatever
    // the keyboard's layout; the Board's message (a refusal said there), when there is one.
    virtual void beginSessionPage() = 0;
    virtual void drawBoardMessage() = 0;
    // The board, `size` wide: the position shown, its last move and the side below, `marks` (squares tinted, arrows)
    // over it; the user's moves taken there when `moves` (a click, a drag, a promotion's piece), each given to the
    // session (BoardSession::sessionMove()). In 3D when the appearance asks for it.
    virtual void drawSessionBoard(float size, const BoardDecor& marks, bool moves) = 0;
    // The Board's parts in the session's column: the move being typed (its field, played by Enter); the keys that begin
    // one (its letters open the field); the tabs (the tree, the engines, the notes), back with the answer; the fair
    // play's banner while a game of the user's is played on Lichess.
    virtual void drawMoveField()      = 0;
    virtual void moveKeys()           = 0;
    virtual void drawBoardTabs()      = 0;
    virtual void drawFairPlayBanner() = 0;
    // The analysis of the position shown, once the answer is given: started or stopped; its key (Alt+A) pressed in
    // this frame, the analysis then started or stopped (true).
    virtual void analyse(bool on) = 0;
    virtual bool analysisKey()    = 0;
};

// A game replayed on the Board taken by a session (a game guessed, #81): `game` shown at `ply`, in Chess960 or not,
// the user's selection and typed move let go; the ply shown moved in it (the end's arrows, a costly move clicked).
class SessionReplay {
   public:
    virtual ~SessionReplay() = default;

    virtual void replaySessionGame(ReplayGame game, int ply, bool chess960) = 0;
    virtual void showSessionPly(int ply)                                    = 0;
};

// The Training page (#80, view_training_page.cpp) and the day's review it starts, "Review all" taking the kinds one
// after the other (review_queue.hpp); the training's settings.
class Training {
   public:
    virtual ~Training() = default;

    // The training's settings (training.json): a right answer goes on by itself after advance_ms; whether the moves of
    // theory pass by themselves in a game guessed, changed and saved at once.
    [[nodiscard]] virtual const TrainingSettings& trainingSettings() = 0;
    virtual void setGuessTheory(bool on)                             = 0;
    // The Training page shown, at its part `part` (0 Today, 1 Puzzles, 2 My mistakes, 3 Guess the moves…); whether it
    // is the page shown.
    virtual void showTrainingPage(int part)          = 0;
    [[nodiscard]] virtual bool trainingShown() const = 0;
    // What Enter starts on the Training page: the puzzles, once one of their choices was clicked, else the day's
    // review.
    virtual void setEnterStartsPuzzles(bool puzzles) = 0;
    // The day's review: started for one kind alone ("Only my mistakes"); a session of its shown (its end goes on to
    // the next kind, or back to the Training page); the next kind started (false when none is left); the review
    // stopped, what it had left forgotten; at a session's end, its card that goes on or back to the Training page
    // (`enter`: Enter goes on).
    virtual void reviewOnly(ReviewKind kind)         = 0;
    [[nodiscard]] virtual bool reviewRunning() const = 0;
    virtual bool nextReview()                        = 0;
    virtual void stopReview()                        = 0;
    virtual void drawReviewNext(bool enter)          = 0;
};

// The user's mistakes as exercises (#81, their module's, mistakes_page.hpp): the kinds of their mistakes since `since`
// (a packed date, packedDaysBefore()), counted, as their reviews' "To work on" says them.
class Mistakes {
   public:
    virtual ~Mistakes() = default;

    [[nodiscard]] virtual std::map<std::string, std::size_t> mistakeKinds(uint32_t since) = 0;
};

// The file dialogs a module opens, the file chosen given back to the module by App (App::handleChessDialog()).
class FileDialogs {
   public:
    virtual ~FileDialogs() = default;

    // Lichess's puzzle database downloaded before (lichess_db_puzzle.csv.zst), read into the puzzles' store
    // (PuzzlesPage::startOffline()).
    virtual void choosePuzzleFile() = 0;
};

// The user's names (Settings › Your names, #81): a game where one of them plays is the user's; the linked Lichess
// account's name counts by itself.
class Names {
   public:
    virtual ~Names() = default;

    // The user's names, with the linked Lichess account's.
    [[nodiscard]] virtual std::vector<std::string> userNames() const = 0;
    // The names as Settings shows them: a chip each (a click takes it away), the Lichess account's, a field that adds
    // one; `id` tells this place's apart from the others.
    virtual void drawPlayerNames(const char* id) = 0;
};

// The Board page's analysis (#78), whose engine the training takes: the engine chosen for it, else the first of the
// library that works (none without any); its settings (its threads, the size of its table).
class Analysis {
   public:
    virtual ~Analysis() = default;

    [[nodiscard]] virtual const EngineEntry* analysisEngine()        = 0;
    [[nodiscard]] virtual const AnalysisSettings& analysisSettings() = 0;
};

// Games reviewed as the Board page reviews a game (#61, #63): the library's engine a review was made with, by its name
// (none when it is no longer there); a game reviewed in the background as the Board page reviews one in the Chess space
// (the analysis's settings, a number of nodes a position from an empty table, Lichess's evaluations kept offline taken
// as they are, the engine processes the tournaments leave, the engine's second line where a move can be the only one),
// its runner started, the processes it searches with in `processes`; the time a position takes at a review's last pass
// (Settings), which tells how many passes it makes; whether the Board page analyses or reviews a game now (the machine
// is its meanwhile).
class Reviews {
   public:
    virtual ~Reviews() = default;

    [[nodiscard]] virtual const EngineEntry* engineNamed(const std::string& name) const = 0;

    [[nodiscard]] virtual std::unique_ptr<ReviewRunner> reviewInBackground(const EngineEntry& engine,
                                                                           const ReplayGame& game, int& processes) = 0;

    [[nodiscard]] virtual int64_t reviewCeilingMs()   = 0;
    [[nodiscard]] virtual bool boardAnalysing() const = 0;
};

// The repertoires (the Openings page, #76): the positions their training asks, by the side that trains them (0 White,
// 1 Black). Trained there, they are not asked as the user's mistakes.
class Repertoires {
   public:
    virtual ~Repertoires() = default;

    [[nodiscard]] virtual std::array<std::unordered_set<uint64_t>, 2> repertoirePositions() = 0;
};

// A game opened on the Board from a session (the game an exercise comes from, #81): the session that holds the Board
// left there, its answers kept; at `ply` (its last move at most), said by `title`.
class GamesOnBoard {
   public:
    virtual ~GamesOnBoard() = default;

    // A game of a database, read from its PGN: the game `id` of the database `name` of `folder`, which Ctrl+S replaces.
    virtual void openDatabaseGame(const std::string& pgn, const std::string& title, const std::filesystem::path& folder,
                                  const std::string& name, uint64_t id, int ply) = 0;
    // A game of no database's.
    virtual void openGameOnBoard(ReplayGame game, int ply, std::string title) = 0;
};

// The game on the Board, which a game guessed may start from (#81, "The Board page's game"): its moves, the ply shown,
// Chess960, its title (empty: none), the game of a database it is (its folder and number); whether the Board page is
// the page shown; whether a session of the repertoires, the puzzles, the mistakes or a game guessed holds the Board
// (an endgame's and a vision exercise's games count as the Board's own).
struct ShownGame {
    const ReplayGame& game;
    int ply;
    bool chess960;
    const std::string& title;
    std::optional<std::pair<std::filesystem::path, uint64_t>> origin;
    bool page;
    bool session;
};

// The Board's own game, and the database its Tree tab reads (-1: none chosen there), whose theory a game guessed passes
// by itself.
class BoardGame {
   public:
    virtual ~BoardGame() = default;

    [[nodiscard]] virtual ShownGame shownGame() const = 0;
    [[nodiscard]] virtual int treeDatabase() const    = 0;
};

}  // namespace context

// What Lichess TV asks of the program (lichess_tv_page.hpp).
class TvContext : public virtual context::Program,
                  public virtual context::Pages,
                  public virtual context::Board,
                  public virtual context::Mosaic,
                  public virtual context::Lichess {};

// What Lichess's arenas and Swiss tournaments ask of the program (lichess_arenas_page.hpp).
class ArenasContext : public virtual context::Program,
                      public virtual context::Pages,
                      public virtual context::Databases,
                      public virtual context::Mosaic,
                      public virtual context::Lichess,
                      public virtual context::LinkedAccount,
                      public virtual context::Account,
                      public virtual context::FairPlay,
                      public virtual context::Playing {};

// What the Lichess bot asks of the program (lichess_bot_page.hpp).
class BotContext : public virtual context::Program,
                   public virtual context::Engines,
                   public virtual context::LiveBoard,
                   public virtual context::LinkedAccount,
                   public virtual context::FairPlay,
                   public virtual context::Tokens,
                   public virtual context::Window {};

// What the puzzles ask of the program (puzzles_page.hpp).
class PuzzlesContext : public virtual context::Program,
                       public virtual context::SessionBoard,
                       public virtual context::Training,
                       public virtual context::Mistakes,
                       public virtual context::FileDialogs,
                       public virtual context::LinkedAccount,
                       public virtual context::Account,
                       public virtual context::FairPlay {};

// What the user's mistakes as exercises ask of the program (mistakes_page.hpp).
class MistakesContext : public virtual context::Program,
                        public virtual context::SessionBoard,
                        public virtual context::Training,
                        public virtual context::FairPlay,
                        public virtual context::Names,
                        public virtual context::Analysis,
                        public virtual context::Reviews,
                        public virtual context::Repertoires,
                        public virtual context::GamesOnBoard {};

// What guessing the moves of a game asks of the program (guess_page.hpp).
class GuessContext : public virtual context::Program,
                     public virtual context::SessionBoard,
                     public virtual context::SessionReplay,
                     public virtual context::Training,
                     public virtual context::FairPlay,
                     public virtual context::Analysis,
                     public virtual context::BoardGame {};

// What App answers: every module's context, each facet once.
class AppContext : public TvContext,
                   public ArenasContext,
                   public BotContext,
                   public PuzzlesContext,
                   public MistakesContext,
                   public GuessContext {};

}  // namespace fcg
