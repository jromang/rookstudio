#pragma once

// Lichess's arenas and Swiss tournaments (#95, mockup M13, board 8), without ImGui: what the Lichess page's card lists
// (the arenas in progress and to come, read without an account; the Swiss tournaments of the user's teams, team:read),
// an arena or a Swiss followed in the mosaic (its standing, its players' sheets, the games of its first eight), joining
// and playing one through the Board API (tournament:write to join, pause and leave; board:play to play, #84), and a
// finished Swiss kept in a database with its standing (its TRF file), so that #92's crosstable reads it. Everything
// asked one request at a time, a minute after a 429 (https.hpp's HostGate).
//
// What Lichess allows (spec 2.0.180): the Board API plays rapid, classical and correspondence only, never blitz or
// bullet in a tournament: those arenas are followed here and played on lichess.org. An arena pairs a player away from
// its page for a minute after each join (pairMeAsap): RookStudio joins again while the user waits. Berserk only before
// the user's first move, where the arena allows it. A Swiss is for its team's members; its games in progress are found
// by player.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include <study/crosstable.hpp>
#include <study/import.hpp>

#include "lichess_api.hpp"
#include "lichess_board.hpp"
#include "lichess_profile.hpp"

namespace fcg::lichess {

// Joining, pausing and leaving a tournament; reading the user's teams (Lichess requires it for /api/team/of since
// 2026-02).
inline constexpr const char* SCOPE_TOURNAMENT_WRITE = "tournament:write";
inline constexpr const char* SCOPE_TEAM_READ        = "team:read";

// The mosaic of an arena or a Swiss followed, as the Lichess page names it: "arena:ID", "swiss:ID".
inline constexpr std::string_view ARENA_MOSAIC = "arena:";
inline constexpr std::string_view SWISS_MOSAIC = "swiss:";

inline constexpr auto STANDING_EVERY     = std::chrono::seconds(15);  // an arena's standing asked again
inline constexpr auto ARENA_REJOIN       = std::chrono::seconds(50);  // a join's pairing lasts a minute
inline constexpr auto ROUND_NOTICE       = std::chrono::minutes(2);   // a Swiss's next round said before it
inline constexpr std::size_t ARENA_GAMES = 8;                         // the first eight's games, in the mosaic

// ---------------------------------------------------------------------------
// What a tournament asks of a player

// A condition to enter, as the tournament states it, and as Lichess judged it for the user when it did (with a token).
struct TourCondition {
    enum class Kind { RatedGames, MaxRating, MinRating, Titled, Team, AccountAge, AllowList, Other };
    Kind kind = Kind::Other;
    int value = 0;        // games, a rating, days
    std::string perf;     // the speed it counts in ("rapid"); empty: the tournament's own
    std::string team_id;  // Kind::Team
    std::string team_name;
    std::string text;        // Lichess's words for it, when it judged it
    std::optional<bool> ok;  // accepted, refused; none: not judged
    std::string refusal;     // Lichess's words when refused
    bool operator==(const TourCondition&) const = default;
};

// The conditions of a tournament's JSON: its own fields (minRatedGames, maxRating, minRating, onlyTitled, teamMember,
// minAccountAgeInDays, allowList) in the order lila lists them, each given its verdict when Lichess's list
// ("verdicts") has as many; else Lichess's list as it words it (a Swiss's "Play your games" has no field of its own).
[[nodiscard]] std::vector<TourCondition> readConditions(const nlohmann::json& j);
// Whether every condition is met as far as `profile` tells (none: not known, or not judged by Lichess and not one of
// ratings and games); Lichess's own verdict first when it gave one.
[[nodiscard]] std::optional<bool> conditionsMet(const std::vector<TourCondition>& conditions, std::string_view perf,
                                                const Profile* profile);

// ---------------------------------------------------------------------------
// Arenas

enum class TourState { Created, Started, Finished };

// An arena, as the list (/api/tournament) and its own page (/api/tournament/{id}) give it.
struct ArenaInfo {
    std::string id, name;  // fullName
    std::string speed;     // its perf's key: "bullet", "blitz", "rapid", "classical", "ultraBullet"
    std::string variant = "standard";
    int64_t limit_s = 0, increment_s = 0;
    bool rated        = true;
    int minutes       = 0;  // its length
    int players       = 0;
    int64_t starts_at = 0, finishes_at = 0;  // ms since the epoch; 0 not known
    TourState state = TourState::Created;
    std::vector<TourCondition> conditions;
    bool berserkable = false;  // its own page only
    bool accepted    = true;   // Lichess's verdict, when it judged it (with a token)
    bool judged      = false;
    std::string freq;  // its schedule's: "hourly", "daily"…; empty for a user's arena
    [[nodiscard]] bool standard() const { return variant.empty() || variant == "standard"; }
};

// The arenas of standard chess in progress, then those to come, from /api/tournament's answer: the variants' left out,
// as are those finished. In progress the soonest to end first, to come the soonest to start first.
[[nodiscard]] std::vector<ArenaInfo> readArenaList(const nlohmann::json& j);
// The list, without an account.
[[nodiscard]] Answer<std::vector<ArenaInfo>> arenaList(const std::atomic<bool>* cancel = nullptr);

// The card's filter: rapid, classical, or all of them.
enum class ArenaSpeeds { Rapid, Classical, All };
[[nodiscard]] bool arenaShown(const ArenaInfo& arena, ArenaSpeeds speeds);
// Whether RookStudio plays it, through the Board API: rapid and classical (there is no correspondence arena).
[[nodiscard]] bool playableHere(const ArenaInfo& arena);

// What an arena's line offers: Join, or why not.
enum class JoinOffer { Join, WebOnly, ConditionNotMet, Finished };
[[nodiscard]] JoinOffer joinOffer(const ArenaInfo& arena, const Profile* profile);

// A game's points on a player's sheet: doubled while on a streak (two wins in a row: the next win 4 points, a draw 2),
// a point more for a game won berserk.
struct SheetScore {
    int points                               = 0;
    bool doubled                             = false;
    bool berserk                             = false;
    bool operator==(const SheetScore&) const = default;
};
// A sheet as Lichess writes it ("5533003": the newest game first), the oldest game first; `streaks` false for an arena
// without them.
[[nodiscard]] std::vector<SheetScore> readSheet(std::string_view scores, bool streaks = true);

struct ArenaPlayer {
    int rank = 0;
    std::string name, title;
    int rating = 0, score = 0;
    std::string sheet;    // as Lichess writes it, the newest game first
    bool fire   = false;  // on a streak: the next win counts double
    bool paused = false;
    int games = 0, performance = 0;  // a podium's
    bool operator==(const ArenaPlayer&) const = default;
};

// A game being played in the arena, as its page names it: its two players (no colours) and their ranks.
struct ArenaDuel {
    std::string id;
    std::string a, b;  // names
    int a_rank = 0, b_rank = 0, a_rating = 0, b_rating = 0;
};

// The user in the arena, as its page tells it with their token.
struct ArenaMe {
    int rank    = 0;
    bool paused = false;
    std::string game;     // the game they play now
    int pause_delay = 0;  // seconds before they may join again, after a pause
};

struct Arena {
    ArenaInfo info;
    int seconds_to_finish = -1, seconds_to_start = -1;
    bool streaks = true;
    int page     = 1;  // of the standing
    std::vector<ArenaPlayer> standing;
    std::vector<ArenaDuel> duels;
    std::string featured;
    std::optional<ArenaMe> me;
    std::vector<ArenaPlayer> podium;
    bool pairings_closed = false;
};
[[nodiscard]] Arena readArena(const nlohmann::json& j);
// An arena, a page of its standing (ten players a page), with the user's token when given (their verdicts, their
// rank).
[[nodiscard]] Answer<Arena> arenaOf(const std::string& id, int page = 1, const std::string& token = {},
                                    const std::atomic<bool>* cancel = nullptr);
// The page of the standing a rank is on.
[[nodiscard]] int standingPage(int rank);
// A player of the standing, by name whatever its case; none when not on that page.
[[nodiscard]] const ArenaPlayer* standingOf(const Arena& arena, std::string_view name);

// Joining (pairMeAsap: paired at once, away from the arena's page, for a minute), pausing (withdraw: the points stay).
[[nodiscard]] Reply joinArena(const std::string& token, const std::string& id, bool pair_me_asap = true,
                              const std::atomic<bool>* cancel = nullptr);
[[nodiscard]] Reply withdrawArena(const std::string& token, const std::string& id,
                                  const std::atomic<bool>* cancel = nullptr);
// Berserk in an arena's game: half the user's clock, a point more for a win; before their first move only.
[[nodiscard]] Reply berserk(const std::string& token, const std::string& game,
                            const std::atomic<bool>* cancel = nullptr);
// Whether berserk may still be asked: the arena allows it, the user has not moved, nor gone berserk already.
[[nodiscard]] bool berserkAllowed(bool berserkable, const BoardState& game, bool white);

// A tournament's game, an arena's or a Swiss's: its first move not played in time loses it (Lichess aborts another).
[[nodiscard]] bool tournamentGame(const BoardState& game);
// The user's side lost the game by not playing its first move in time.
[[nodiscard]] bool lostAtStart(const BoardState& game, bool white);
// The scopes a link made again asks: those granted, and `scope` (the token it replaces is revoked, #45).
[[nodiscard]] std::vector<std::string> scopesAdding(const std::vector<std::string>& granted, std::string_view scope);

// The user in an arena from RookStudio: in it, waiting for a game or playing one, or paused. Lichess pairs a player
// away from its page for a minute after a join: while they wait, a join again is due before that minute ends. The aids
// stay locked while they are in the arena, between two games too (point 18); a pause gives them back.
class ArenaSeat {
   public:
    using Clock = std::chrono::steady_clock;
    void joined(Clock::time_point now);  // a join Lichess took
    void pause();                        // the user paused, or left
    void playing(bool on);               // one of its games is being played
    void over();                         // the arena ended
    [[nodiscard]] bool in() const { return joined_ && !paused_ && !over_; }
    [[nodiscard]] bool paused() const { return joined_ && paused_ && !over_; }
    [[nodiscard]] bool isOver() const { return over_; }
    [[nodiscard]] bool waiting() const { return in() && !playing_; }
    [[nodiscard]] bool due(Clock::time_point now) const { return waiting() && now - last_ >= ARENA_REJOIN; }
    [[nodiscard]] bool locks() const { return in(); }
    [[nodiscard]] Clock::time_point lastJoin() const { return last_; }

   private:
    bool joined_ = false, paused_ = false, playing_ = false, over_ = false;
    Clock::time_point last_{};
};

// ---------------------------------------------------------------------------
// Swiss tournaments

struct Team {
    std::string id, name;
    int members                        = 0;
    bool operator==(const Team&) const = default;
};
[[nodiscard]] std::vector<Team> readTeams(const nlohmann::json& j);
// The teams of a player: the user's, with a token that may read them (SCOPE_TEAM_READ).
[[nodiscard]] Answer<std::vector<Team>> teamsOf(const std::string& username, const std::string& token,
                                                const std::atomic<bool>* cancel = nullptr);

struct SwissInfo {
    std::string id, name;
    std::string team_id, team_name;  // the team it is for, when known
    int64_t limit_s = 0, increment_s = 0;
    std::string variant = "standard";
    bool rated          = true;
    int round = 0, rounds = 0, players = 0, ongoing = 0;
    TourState state       = TourState::Created;
    int64_t starts_at     = 0;  // ms since the epoch
    int64_t next_round_at = 0;  // ms since the epoch; 0 none told
    std::vector<TourCondition> conditions;
    bool accepted = true, judged = false;
    [[nodiscard]] std::string speed() const;  // its clock's, as scalachess counts it
    [[nodiscard]] bool standard() const { return variant.empty() || variant == "standard"; }
};
[[nodiscard]] SwissInfo readSwiss(const nlohmann::json& j);
[[nodiscard]] std::vector<SwissInfo> readSwissList(std::string_view ndjson);
// Whether RookStudio plays it: standard chess, rapid, classical or correspondence (the Board API's).
[[nodiscard]] bool playableHere(const SwissInfo& swiss);
// A team's Swiss tournaments, the last to start first, `most` of them; with the user's token, their verdicts.
[[nodiscard]] Answer<std::vector<SwissInfo>> teamSwisses(const std::string& team, const std::string& token,
                                                         int most = 10, const std::atomic<bool>* cancel = nullptr);
[[nodiscard]] Answer<SwissInfo> swissOf(const std::string& id, const std::string& token = {},
                                        const std::atomic<bool>* cancel = nullptr);

// A row of a Swiss's standing (/api/swiss/{id}/results).
struct SwissRow {
    int rank = 0;
    std::string name, title;
    int rating    = 0;
    double points = 0, tie_break = 0;
    int performance                        = 0;
    bool absent                            = false;  // withdrawn: not paired any more
    bool operator==(const SwissRow&) const = default;
};
[[nodiscard]] std::vector<SwissRow> readSwissResults(std::string_view ndjson);
[[nodiscard]] Answer<std::vector<SwissRow>> swissResults(const std::string& id, int most = 0,
                                                         const std::atomic<bool>* cancel = nullptr);
[[nodiscard]] Reply joinSwiss(const std::string& token, const std::string& id,
                              const std::atomic<bool>* cancel = nullptr);
[[nodiscard]] Reply withdrawSwiss(const std::string& token, const std::string& id,
                                  const std::atomic<bool>* cancel = nullptr);
// Its FIDE Tournament Report File (/swiss/{id}.trf), and its games as PGN (/api/swiss/{id}/games).
[[nodiscard]] Answer<std::string> swissTrf(const std::string& id, const std::atomic<bool>* cancel = nullptr);
[[nodiscard]] Answer<std::string> swissGames(const std::string& id, const std::string& token = {},
                                             const std::atomic<bool>* cancel = nullptr);

// Whether the next round's notice is due: within the two minutes before it starts.
[[nodiscard]] bool roundNoticeDue(int64_t next_round_at_ms, int64_t now_ms);

// ---------------------------------------------------------------------------
// A Swiss's TRF, and the Swiss kept in a database

struct TrfRound {
    int opponent = 0;    // their start number, 0 none
    char colour  = 0;    // 'w', 'b', 0 none
    char code    = '-';  // '1', '0', '=', '+' and '-' forfeits (or '-' absent), 'H' a late join's half point, 'U' a
                         // bye, 'Z' being played
    bool operator==(const TrfRound&) const = default;
};
struct TrfPlayer {
    int number = 0;
    std::string name;  // Lichess writes the account's id, lowercase
    int rating    = 0;
    double points = 0;
    std::vector<TrfRound> rounds;
};
struct Trf {
    std::string name, url;
    int rounds = 0;
    std::vector<TrfPlayer> players;
    [[nodiscard]] const TrfPlayer* player(std::string_view name) const;  // whatever its case
    [[nodiscard]] const TrfPlayer* number(int n) const;
};
[[nodiscard]] Trf readTrf(std::string_view text);
// The points of a round's code: a win, a forfeit won or a bye 1; a draw or a late join's half point ½; else 0.
[[nodiscard]] double trfPoints(char code);

// A finished Swiss as a database keeps it: its games, each with its round from the TRF, its event and its site
// telling it apart from another Swiss of the same name; the rounds without a game (byes, late joins, absences, a game
// the export lacks) as results set by hand, which #92's crosstable reads.
struct KeptSwiss {
    std::vector<study::ImportedGame> games;
    std::vector<study::HandResult> hand;
    std::string event;  // the event its games are written with
    std::string key;    // the crosstable's key of the tournament they make in a database of their own
    int unmatched = 0;  // games whose pairing the TRF lacks, kept without a round
};
// `starts` the Swiss's start, as the event names it ("20:00 on 26 November 2026", the local time).
[[nodiscard]] KeptSwiss keepSwiss(const Trf& trf, std::string_view pgn, const std::string& name, const std::string& id,
                                  const std::string& starts);
// "20:00 on 26 November 2026", from ms since the epoch, in the local time (data: English, as PGN tags are).
[[nodiscard]] std::string swissStartText(int64_t ms);

// "2026-11-24T08:00:25Z" (milliseconds after a dot allowed), as ms since the epoch; none when it is not one.
[[nodiscard]] std::optional<int64_t> isoMillis(std::string_view text);

}  // namespace fcg::lichess
