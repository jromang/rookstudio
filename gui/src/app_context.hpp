#pragma once

// What a module of RookStudio's asks of the program around it (#107). A module holds an area of its own out of App
// (its state, its poll(), its draw(), its palette commands and --focus words: Lichess TV first, lichess_tv_page.hpp),
// and reaches the rest through this interface instead of App's members: the pages it opens, the Board, the mosaic its
// games show in, the databases, the profile, the Lichess settings and account. App answers it (App::ModulesContext,
// app_chess.cpp); a test, with a fake of its own, builds a module without App.
//
// Each method says what a module needs, in its words, not which member of App gives it. The interface holds only what
// the modules out of App need now, and grows with those that follow (the arenas, the bot, training).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace fcg {

class DatabaseShelf;
struct LichessSettings;
struct ReplayGame;

// An entry of the command palette (Ctrl+K), as App::paletteEntries() makes them: add(label, where, run).
using PaletteAdd = std::function<void(std::string, std::string, std::function<void()>)>;

class AppContext {
   public:
    virtual ~AppContext() = default;

    // The frame being drawn (ImGui's count): what was drawn a few frames ago is still shown.
    [[nodiscard]] virtual int frame() const = 0;
    // Scripted screenshots: nothing of theirs is kept in the profile.
    [[nodiscard]] virtual bool scripted() const = 0;
    // The profile's folder, where a module keeps its files.
    [[nodiscard]] virtual std::filesystem::path profileDir() const = 0;
    // The profile's databases.
    [[nodiscard]] virtual DatabaseShelf& shelf() = 0;
    // Said a moment at the Board's corner.
    virtual void toast(const std::string& text) = 0;

    // The pages.
    virtual void showLichessPage() = 0;  // the Lichess page itself, not a mosaic
    virtual void showBoardPage()   = 0;
    virtual void toStudyBoard()    = 0;  // the Board, in a view that shows the study's

    // The Board: what its game is, and the game it follows live from Lichess (its address; empty when none).
    [[nodiscard]] virtual std::string boardTitle() const   = 0;
    [[nodiscard]] virtual std::string boardFollows() const = 0;
    // `game` on the Board at its last move, said by `title`, then followed live as its moves come (`url`, `version`:
    // the follower's game and how far it went), the way back to the mosaic `mosaic`; `in_place`: the page shown and
    // the Board's way back stay as they are.
    virtual void followOnBoard(ReplayGame game, std::string title, std::string mosaic, std::string url,
                               uint64_t version, bool in_place) = 0;

    // The broadcasts' mosaic (view_broadcast_mosaic.cpp), where the rounds followed, the followed players' games and
    // the modules' show: the key of the one shown (empty: the Lichess page itself); one opened; the way back to it
    // from the Board; the engine that analyses its games, the user's choice.
    [[nodiscard]] virtual const std::string& mosaicShown() const = 0;
    virtual void openMosaic(const std::string& key)              = 0;
    [[nodiscard]] virtual bool boardBackToMosaic() const         = 0;  // the Board's game came from one still there
    virtual void backToMosaic()                                  = 0;
    virtual void setMosaicEngine(const std::string& engine)      = 0;
    // The game streams of this address, eight at most (lichess::MOST_GAME_STREAMS): those the others hold now, and the
    // followed players' share, set by the module that shares them out.
    struct GameStreams {
        std::size_t rounds  = 0;  // the rounds followed, one each
        std::size_t players = 0;  // the followed players' games, open
        std::size_t tour    = 0;  // a Lichess tournament's games, open (#95)
    };
    [[nodiscard]] virtual GameStreams gameStreams() const = 0;
    virtual void setPlayersQuota(std::size_t streams)     = 0;

    // Lichess: the user's settings, saved; the user's own accounts, lowercase (their linked account, their bot's).
    [[nodiscard]] virtual LichessSettings& lichessSettings()       = 0;
    virtual void saveLichessSettings()                             = 0;
    [[nodiscard]] virtual std::vector<std::string> ownLichessIds() = 0;
};

}  // namespace fcg
