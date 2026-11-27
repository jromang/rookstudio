#pragma once

// Lichess TV (#86, mockup M12, board 7), a module of its own out of App (#107; lichess_tv.hpp is its part of the
// network): its channels on the Lichess page, each with its featured game, read again while the card shows, every 30 s
// at most; a channel's games in the broadcasts' mosaic ("tv:<channel>"), a third source beside the rounds and the
// followed players, a finished game giving its place to another; a game on the Board, which goes on to the channel's
// next featured game when it ends, unless the user stays on it, and goes into a database at its end when kept. Without
// an account; never the user's own games. The game streams of this address, eight at most: the rounds followed take
// one each, then the Board's game and those kept, then the mosaic shown (a channel's or the followed players').
//
// What it needs of the program around it, it asks of its context (TvContext, app_context.hpp): App's, or a test's.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app_context.hpp"
#include "focus_words.hpp"
#include "lichess_tv.hpp"
#include "repairs.hpp"

namespace fcg {

struct ReplayGame;
namespace lichess {
struct BroadcastGame;
class PlayersFollower;
}  // namespace lichess

// Whether a mosaic's key is a channel's ("tv:blitz").
[[nodiscard]] inline bool isTvMosaic(std::string_view key) { return key.rfind(lichess::TV_MOSAIC, 0) == 0; }
// A channel as the card, its chips and the palette name it: "Blitz", in the reader's language.
[[nodiscard]] const char* tvChannelName(std::string_view key);
// A side as the tiles and the Board name it: Lichess's AI by its level, else as the PGN does.
[[nodiscard]] std::string tvName(const std::string& name);

class LichessTvPage {
   public:
    explicit LichessTvPage(TvContext& app);
    ~LichessTvPage();
    LichessTvPage(const LichessTvPage&)            = delete;
    LichessTvPage& operator=(const LichessTvPage&) = delete;

    // Once a frame, whatever shows: the card's answer, a channel's games, the Board's game, those kept.
    void poll();
    // The Lichess page's card: each channel's featured game, the mosaic and the Board a click away.
    void drawCard();
    // On the Board page, while it follows a game of a channel: what follows its end, Stay on this game, Keep it.
    void drawBoardCard();
    // On an engine's card (the Engines page): the Bots and Computer channels in the mosaic, analysed by `engine`.
    void drawEngineLine(const std::string& engine);
    // Its entries of the command palette (Ctrl+K): the card (in the view `lichess_view`), each channel in the mosaic,
    // its featured game on the Board once the card read them, the Board's game.
    void addCommands(const PaletteAdd& add, const std::string& lichess_view);
    // Its --focus words, tried where App's table has them (app_chess.cpp): lichess-tv, tv-mosaic=CHANNEL,
    // tv-board=CHANNEL:ID. `word` is the word `words` was made with.
    void addFocusWords(FocusWords& words, const std::string& word);

    // A channel in the mosaic, kept as the one shown last.
    void openMosaic(const std::string& channel);
    // The channel of the mosaic open, empty when it is none.
    [[nodiscard]] std::string channelShown() const;
    // The Board follows a game of a channel: its moves as they come; `known`, what is known of it already, else what
    // the follower has (nothing yet: the board fills as Lichess sends it); `in_place`: the page and the Board's way
    // back stay as they are.
    void followOnBoard(const std::string& channel, const std::string& game, bool featured, const std::string& title,
                       const std::string& clock, const ReplayGame* known = nullptr, uint64_t version = 0,
                       bool in_place = false);
    // A channel's featured game on the Board, the card's rows known.
    void followFeatured(const std::string& channel);
    // The Board's game into a database at its end (`folder`; empty: one named `name`, made when missing), now when it
    // ended already.
    void keepGame(const std::string& folder, const std::string& name);

    // What the mosaic shows of the channel (view_broadcast_mosaic.cpp): its games in the order of their places, a game
    // that ended keeping its tile until the one taking its place has come, so that the tiles do not move; the tiles'
    // games, in that order (URLs); every game followed (the Board's and those kept too); the channel's featured game,
    // as listed last; the channel listed last, why its list did not come; the streams left to the mosaic, those open.
    [[nodiscard]] std::vector<lichess::BroadcastGame> mosaicGames();
    [[nodiscard]] const std::vector<std::string>& slots() const { return tv_slots_; }
    [[nodiscard]] std::vector<lichess::BroadcastGame> games() const;
    [[nodiscard]] const std::string& featured() const { return tv_listed_.featured; }
    [[nodiscard]] const std::string& listedOf() const { return tv_listed_of_; }
    [[nodiscard]] const lichess::Failure& listError() const { return tv_list_error_; }
    [[nodiscard]] std::size_t room() const { return tv_room_; }
    [[nodiscard]] std::size_t streams() const;

    // The Board follows a game of a channel; it leaves it (another game on the Board).
    [[nodiscard]] bool onBoard() const { return board_tv_.has_value(); }
    void leaveBoard() { dropBoardTv(); }

    // Why the card's channels did not come, as the card says it in place of its line (#146): a token refused with Link
    // Lichess again…, an outage or the host's pause with Try again, the pause counted down; none while nothing failed.
    // A fix of it carried out: Try again the module's own (queued through the host's pause), the others the program's.
    [[nodiscard]] std::optional<Notice> channelsNotice() const;
    void repair(const Fix& fix);

   private:
    struct BoardTv {
        std::string channel, game;  // the game the Board follows, its channel
        bool featured = false;      // the channel's featured game when it was taken
        bool stay     = false;      // "Stay on this game": not on to the next one at its end
        std::string clock;          // "3+0"
        int tries = 0;              // the next game asked again since it ended
        std::chrono::steady_clock::time_point next_at{};
        std::future<lichess::Answer<lichess::TvNext>> next;
        std::shared_ptr<std::atomic<bool>> next_cancel;  // its request's: set when the Board leaves the game (#148)
        lichess::Failure error;                          // why the next game did not come
    };
    struct TvKept {
        std::string id, title;
        std::string folder;  // the database's
        std::string base;    // its name, as said
    };
    void drawKeepMenu();
    void storeGame(const TvKept& kept, const std::string& pgn);  // into its database, now
    void loadKept();
    void saveKept();
    void drawRepair(const Notice& notice, const char* id);  // in place of an error's line, its fix carried out
    void askAgain(Retry what);                              // its own Try again
    // The Board's game let go of: the next one's request cancelled and parked, never waited for (#148).
    void dropBoardTv();

    TvContext& app_;
    // The cancel flag of its requests (the channels, a channel's games): set when it goes, and by the program's
    // cancelAll() as it quits (#148), so that none waits on for the host's pause.
    std::shared_ptr<std::atomic<bool>> stop_;
    OwnRetries retries_;
    lichess::Answer<std::vector<lichess::TvRow>> tv_rows_;
    std::future<lichess::Answer<std::vector<lichess::TvRow>>> tv_rows_job_;
    std::chrono::steady_clock::time_point tv_rows_asked_{};
    int tv_card_drawn_ = -1000;                              // the frame that drew the card last
    std::unique_ptr<lichess::PlayersFollower> tv_follower_;  // the mosaic's games, the Board's, those kept
    lichess::TvList tv_listed_;  // the mosaic's channel: its featured game and games, as Lichess listed them last
    std::string tv_listed_of_;   // that channel
    lichess::Failure tv_list_error_;
    std::future<std::pair<std::string, lichess::Answer<lichess::TvList>>> tv_list_job_;
    std::string tv_list_asked_of_;  // the channel asked last, and when
    std::chrono::steady_clock::time_point tv_list_asked_at_{};
    std::vector<std::string> tv_places_;  // the mosaic's games, in the order of its tiles
    std::vector<std::string> tv_slots_;   // its tiles: a place's game, or the one it replaces until it comes (URLs)
    std::set<std::string> tv_over_;       // the games known to have ended, or that could not be read
    std::map<std::string, std::chrono::steady_clock::time_point> tv_over_since_;  // when: a result shown 5 s
    std::size_t tv_quota_ = 0;  // the game streams its follower may hold now
    std::size_t tv_room_  = 0;  // of them, the mosaic's
    std::optional<BoardTv> board_tv_;
    std::vector<TvKept> tv_kept_;  // games that go into a database at their end, kept in the profile until then
    std::map<std::string, std::string> tv_stored_;  // those that went this session: their id, the database's name
    bool tv_kept_loaded_ = false;
    bool tv_keep_menu_   = false;  // the databases offered to keep the Board's game
};

}  // namespace fcg
