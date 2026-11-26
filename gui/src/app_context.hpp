#pragma once

// What a module of RookStudio's asks of the program around it (#107). A module holds an area of its own out of App
// (its state, its poll(), its draw(), its palette commands and --focus words: Lichess TV, lichess_tv_page.hpp; the
// arenas and Swiss tournaments, lichess_arenas_page.hpp; the Lichess bot, lichess_bot_page.hpp), and reaches the rest
// through the facets below instead of App's members: the program (the frame, the profile and its databases, the jobs),
// the pages it opens, the databases' pages, the Board, the mosaic its games show in, the Lichess settings, the user's
// Lichess account and its link, the user's games on Lichess and the fair play their live game holds, the engines and
// the cores, the Board taken by a game followed live, the tokens given up, the window. A module's context is the facets
// it needs (TvContext, ArenasContext, BotContext); App answers them all (AppContext, by App::ModulesContext,
// app_chess.cpp), and a test only those of the module it builds, with a fake of its own, without App.
//
// Each method says what a module needs, in its words, not which member of App gives it. The facets hold only what the
// modules out of App need now, and grow with those that follow (training): a facet a module does not ask for is none
// of its fake's business.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace fcg {

struct BookEntry;
class CrosstableStore;
class DatabaseShelf;
struct EngineEntry;
class JobGroup;
struct LichessSettings;
class PieceAtlas;
struct ReplayGame;
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
    // The profile, none until read; read again when ten minutes old (nothing asked while scripted).
    [[nodiscard]] virtual const lichess::Profile* lichessProfile() const = 0;
    virtual void askLichessProfile()                                     = 0;
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

// What App answers: every module's context, each facet once.
class AppContext : public TvContext, public ArenasContext, public BotContext {};

}  // namespace fcg
