#pragma once

// Lichess's arenas and Swiss tournaments (#95, mockup M13, board 8), a module of its own out of App (#107;
// lichess_arenas.hpp is its part of the network): the Lichess page's card (the arenas in progress and to come, read
// without an account; the user's teams' Swiss tournaments, team:read asked once); one followed in the mosaic
// ("arena:ID", "swiss:ID"), a fourth source beside the rounds, the followed players and the TV: its standing every 15 s
// and its players' sheets, the games of its first eight; joining one from the Play page's "On Lichess" part
// (tournament:write asked once), Lichess pairing a player away from its page for a minute after each join, so joined
// again while the user waits; its games played on the Board through #84's Board API under #48's lock, held between two
// games while the user is in the arena (point 18) until Pause (Alt+P); a Swiss's rounds, the next one said at the
// window's corner two minutes before, kept in a database with its standing (its TRF) once over. One request at a time,
// a minute after a 429 (HostGate).
//
// What it needs of the program around it, it asks of its context (ArenasContext, app_context.hpp): App's, or a test's.

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
#include <tuple>
#include <utility>
#include <vector>

#include "app_context.hpp"
#include "focus_words.hpp"
#include "lichess_arenas.hpp"
#include "lichess_arenas_schedule.hpp"
#include "lichess_players.hpp"
#include "lichess_playing.hpp"
#include "lichess_tv.hpp"
#include "refreshing.hpp"
#include "repairs.hpp"

namespace fcg {

// Whether a mosaic's key is a tournament's ("arena:ID", "swiss:ID").
[[nodiscard]] inline bool isTourMosaic(std::string_view key) {
    return key.rfind(lichess::ARENA_MOSAIC, 0) == 0 || key.rfind(lichess::SWISS_MOSAIC, 0) == 0;
}

class LichessArenasPage {
   public:
    explicit LichessArenasPage(ArenasContext& app);
    ~LichessArenasPage();
    LichessArenasPage(const LichessArenasPage&)            = delete;
    LichessArenasPage& operator=(const LichessArenasPage&) = delete;

    // Once a frame, whatever shows: the card's list, the teams' Swiss tournaments, the tournament followed, the arena
    // played (its joins again, its pause, its games, Alt+P), the Swiss played and kept.
    void poll();
    // The Lichess page's card, "Lichess tournaments"; the page scrolled to it once drawn, when it was asked for.
    void drawCard();
    void scrollToCard();
    // The Lichess page itself, scrolled to the card (Ctrl+K, --focus, the Play page, the mosaic's source).
    void showCard();
    // Its entries of the command palette (Ctrl+K): the card (in the view `lichess_view`), the Play page's part (in the
    // view `play_view`), the arenas of the list followed or joined, the tournament followed, in an arena the pause and
    // berserk, the user's teams' Swiss tournaments, a finished Swiss kept.
    void addCommands(const PaletteAdd& add, const std::string& lichess_view, const std::string& play_view);
    // Its --focus words, tried where App's table has them (app_chess.cpp): lichess-tournaments, arena-mosaic=ID[@NAME],
    // swiss-mosaic=ID[@NAME], play-arena=ID[@NAME], play-swiss=ID[@NAME], play-tournaments. `word` is the word `words`
    // was made with.
    void addFocusWords(FocusWords& words, const std::string& word);

    // The tournament followed in the mosaic (view_broadcast_mosaic.cpp): whether there is one, its key; whether the
    // mosaic shows it; its games in the order of their places, a game that ended keeping its tile until the one taking
    // its place has come, so that the tiles do not move; the tiles' games, in that order (URLs); its streams open; the
    // mosaic's head, its standing, and what the mosaic says while none of its games shows.
    [[nodiscard]] bool followed() const { return tour_follow_.has_value(); }
    [[nodiscard]] const std::string& followedKey() const;
    [[nodiscard]] bool followedShown() const;
    [[nodiscard]] std::vector<lichess::BroadcastGame> mosaicGames();
    [[nodiscard]] const std::vector<std::string>& slots() const;
    [[nodiscard]] std::size_t streams() const;
    void drawMosaicHead();
    void drawStanding();
    void drawMosaicEmpty();

    // The Play page's "On Lichess › Tournaments" part: the arena or the Swiss chosen last, else those to play.
    void drawPlayTournaments();
    // The arena the user plays from here, on the Board's card of its games and elsewhere: whether a game is of it, or
    // of the Swiss chosen; whether one is chosen; the user in it, or it over; its name and the last game played in it
    // from here (while one is chosen); whether it allows berserk; its line (rank of so many, points, the streak, the
    // time left, Pause); the game before, at its end; the arena over (rank, points, its games, their review); the
    // pause; the pause, then RookStudio closed once Lichess took it (or after a few seconds).
    [[nodiscard]] bool isArenaGame(const std::string& tournament) const;
    [[nodiscard]] bool isSwissGame(const std::string& swiss) const;
    [[nodiscard]] bool arenaChosen() const { return arena_play_.has_value(); }
    [[nodiscard]] bool inArena() const;
    [[nodiscard]] bool arenaOver() const;
    [[nodiscard]] const std::string& arenaName() const;
    [[nodiscard]] const std::string& lastGame() const;
    [[nodiscard]] bool berserkable() const;
    void drawArenaLine(bool board);
    void drawArenaGameBefore();
    void drawArenaOver(bool home);
    void pauseArena();
    void pauseAndClose();
    // A Swiss's game on the Board's card: its round, when it is the Swiss chosen.
    void drawGameRound(const std::string& swiss);
    // Back to the tournament of a game from the Board: its Swiss, else the arena; false when neither is chosen.
    bool backToTournament(const std::string& swiss);

    // Fair play (point 18): in the arena from here, between two games too, until Pause, every aid stays locked; what
    // the lock holds between two games, while it holds them.
    [[nodiscard]] bool holdsAids() const;
    [[nodiscard]] const lichess::PlayingGame& waiting() const;

    // The Home page, at its top (R3): the arena the user is in, a Swiss joined; the next round, at the window's corner.
    void drawHome();
    void drawSwissNotice();

    // Why the card's arenas, or its teams' Swiss tournaments, did not come, as the card says it in place of its line
    // (#146): a token refused with Link Lichess again… (the Swiss tournaments' with team:read), an outage or the host's
    // pause with Try again, the pause counted down; none while nothing failed. A fix of it carried out: Try again the
    // module's own (queued through the host's pause), the others the program's.
    [[nodiscard]] std::optional<Notice> listNotice() const;
    [[nodiscard]] std::optional<Notice> swissesNotice() const;
    void repair(const Fix& fix);

   private:
    struct ArenaCard {
        explicit ArenaCard(JobGroup& jobs) : list(jobs, lichess::ARENA_LIST_EVERY) {}
        Refreshing<std::vector<lichess::ArenaInfo>> list;  // asked while the card shows, every minute
        int drawn = -1000;                                 // the frame that drew the card last
        // The user's teams' Swiss tournaments: their teams, then each one's Swiss tournaments (team:read).
        lichess::Answer<std::vector<lichess::SwissInfo>> swisses;
        std::future<std::pair<lichess::Answer<std::vector<lichess::SwissInfo>>, std::size_t>> swiss_job;  // and teams
        std::chrono::steady_clock::time_point swiss_asked{};
        std::string swiss_token;  // the token they were read with
        std::size_t teams = 0;    // the user's teams, as read last
    };
    // A tournament followed in the mosaic: its standing, the first eight's games in progress (an arena's from its
    // standing and its players' status, a Swiss's from its standing at each round), a finished game giving its place.
    struct TourFollow {
        std::string key, id, name;  // key: "arena:ID", "swiss:ID"
        bool swiss = false;
        lichess::Answer<lichess::Arena> arena;
        lichess::Answer<lichess::SwissInfo> swiss_info;
        lichess::Answer<std::vector<lichess::SwissRow>> swiss_rows;
        int rows_round = -1;  // the round the Swiss's standing was read after
        std::future<lichess::Answer<lichess::Arena>> arena_job;
        std::future<std::pair<lichess::Answer<lichess::SwissInfo>, lichess::Answer<std::vector<lichess::SwissRow>>>>
            swiss_job;
        std::chrono::steady_clock::time_point asked{};
        std::chrono::steady_clock::time_point waits_until{};  // a 429: the standing waits until then, and says so
        std::future<lichess::Answer<std::vector<lichess::PlayerStatus>>> status_job;
        // The cancel flag of its requests (its standing, its players' status): set when it is no longer followed, and
        // as the program quits (#148).
        std::shared_ptr<std::atomic<bool>> cancel;
        std::chrono::steady_clock::time_point status_asked{};
        std::vector<std::string> top;         // the first eight's names, as the standing gives them
        std::vector<lichess::TvGame> listed;  // their games in progress, the first ranked first
        lichess::Failure status_error;        // why their status was not read, last
        std::unique_ptr<lichess::PlayersFollower> follower;
        std::vector<std::string> places, slots;  // as Lichess TV's: the games of the tiles, the tiles' games
        std::set<std::string> over;
        std::map<std::string, std::chrono::steady_clock::time_point> over_since;
        std::size_t quota = 0;
    };
    // The arena the user plays from RookStudio: in it, waiting or playing, or paused.
    struct ArenaPlay {
        std::string id, name;
        lichess::ArenaSeat seat;
        lichess::Answer<lichess::Arena> state;   // its page with the user's token: their verdicts, rank
        std::optional<lichess::ArenaPlayer> me;  // the user's row of the standing (their page)
        std::future<std::pair<lichess::Answer<lichess::Arena>, std::optional<lichess::ArenaPlayer>>> job;
        std::shared_ptr<std::atomic<bool>> cancel;  // of `job`, a reading: set when dropped, and at quit (#148)
        std::chrono::steady_clock::time_point asked{};
        std::chrono::steady_clock::time_point waits_until{};  // a 429
        // A join, a pause: sent once asked, a tournament chosen after them (dropArena()) or not; at quit, they no
        // longer wait for the host's pause (HostGate::close()).
        std::future<lichess::Reply> join_job, pause_job;
        std::chrono::steady_clock::time_point join_asked{};
        bool quiet_join   = false;  // a join again of the minute: its failure said in the line only
        bool join_pending = false;  // joined once tournament:write is granted
        lichess::Failure error;     // why the last join or pause failed: Lichess's refusal, in its words if any
        // The games played in it from here, and the last one's end: its result, its points, the streak.
        std::vector<std::string> games;
        std::string last_game, last_result;
        int last_moves    = 0;
        int sheet_before  = -1;  // the user's games on the sheet when it started; -1 not known
        int last_points   = -1;  // its points, once the sheet tells them
        bool last_doubled = false, streak_starts = false;
        bool over_seen    = false;  // the arena's end said: its review offered
        bool quit_after   = false;  // "Pause and close": RookStudio closes once Lichess took the pause
        double quit_asked = 0;
        lichess::PlayingGame waiting;  // what the lock holds between two games
    };
    // A finished Swiss kept: into which database, its games, its crosstable's key and its rounds set by hand.
    struct SwissKept {
        std::string base, key;
        lichess::Failure failure;  // Lichess's, or the database's in its own words
        std::size_t index = SIZE_MAX, games = 0;
        std::vector<study::HandResult> hand;
    };
    // The Swiss the user plays or looks at from the Play page: its rounds, its standing, their games by round.
    struct SwissPlay {
        std::string id, name, team_id, team_name;
        bool joined = false;
        lichess::Answer<lichess::SwissInfo> info;
        lichess::Answer<std::vector<lichess::SwissRow>> rows;
        std::optional<lichess::Trf> trf;  // their games by round
        int rows_round = -1;
        std::future<std::tuple<lichess::Answer<lichess::SwissInfo>, lichess::Answer<std::vector<lichess::SwissRow>>,
                               std::optional<lichess::Trf>>>
            job;
        // Of `job` and `keep_job`, readings: set when dropped, and at quit (#148). A join or a withdrawal is sent all
        // the same, as an arena's.
        std::shared_ptr<std::atomic<bool>> cancel;
        std::chrono::steady_clock::time_point asked{};
        std::future<lichess::Reply> join_job, pause_job;
        bool join_pending = false;
        // Why the last join or withdrawal failed.
        lichess::Failure error;
        int noticed_round = 0;  // the round its notice said, or put aside
        std::string game;       // its game the user plays now
        // Kept in a database once over: its games and its standing.
        std::future<SwissKept> keep_job;
        std::string kept_in, kept_folder;
        lichess::Failure keep_error;
        std::size_t kept_games = 0;
        bool keep_menu         = false;
    };

    void dropTour();   // the tournament followed, its requests left to end (parked in the program's jobs)
    void dropArena();  // the arena chosen
    void dropSwiss();  // the Swiss chosen
    void drawSwissList();
    void followTournament(const std::string& id, const std::string& name, bool swiss);
    void stopFollowing();
    void chooseArena(const std::string& id, const std::string& name);  // the Play page's Tournaments part, on it
    void chooseSwiss(const lichess::SwissInfo& swiss);
    void joinArenaHere(bool quiet = false);
    void toggleArenaPause();  // Alt+P
    void joinSwissHere();
    void pauseSwiss();
    void keepSwiss(const std::string& folder, const std::string& name);
    bool drawTournamentScope(bool enter);  // tournament:write asked; false when the user said "Not now"
    void drawArenaPart();
    void drawSwissPart();
    void drawSwissKeepMenu();
    [[nodiscard]] bool arenaGameOn() const;           // one of its games is being played here
    [[nodiscard]] std::string arenaTimeLeft() const;  // "31 min"
    void drawRepair(const Notice& notice, const char* id);  // in place of an error's line, its fix carried out
    void askAgain(Retry what);                              // its own Try again

    ArenasContext& app_;
    // The cancel flag of the card's requests (the user's teams' Swiss tournaments): set when it goes, and by the
    // program's cancelAll() as it quits (#148), so that none waits on for the host's pause.
    std::shared_ptr<std::atomic<bool>> stop_;
    OwnRetries retries_;
    ArenaCard arena_card_{app_.jobs()};
    float arena_card_y_ = -1;  // the card asked for (Ctrl+K, --focus): the page scrolled to it
    std::optional<TourFollow> tour_follow_;
    std::optional<ArenaPlay> arena_play_;
    std::optional<SwissPlay> swiss_play_;
    bool tour_part_swiss_ = false;  // the Tournaments part shows the Swiss chosen last, not the arena
};

}  // namespace fcg
