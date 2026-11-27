#pragma once

// A tournament's crosstable and standings (#92, mockup M13, board 5). The tournaments of a database are the games of
// one event, its site and year: listed with the system each was played under (guessed: a round robin when each played
// each, a double round robin, Swiss otherwise; team events and knockouts told, for later), their players, rounds, mean
// Elo and games ("286 of 288" when some are missing). A tournament's crosstable reads its players through the
// database's table of names (players.hpp: all their spellings together), counts the games that came twice once (the
// rules of duplicates.hpp), reads each round from Round ("9.2": round 9, board 2) and computes each player's points,
// FIDE's tie-breaks (C.07, Play-Off and Tie-Break Regulations, effective from 1 March 2026), performance and rating
// change (B.02, Rating Regulations, effective from 1 March 2024 with their amendment of 1 October 2025), and says what
// is wrong with the data: missing games, a player under two names, games that came twice, forfeits, a system not sure.
//
// The computations take players and games of their own (CrossPlayer, CrossGame): a database's are made by
// crosstableInput(), a followed broadcast's by the window from the games it follows (gui/src/crosstables.hpp).

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "store.hpp"

namespace fcg::study {

class FideList;

// ---------------------------------------------------------------------------------------------------------------------
// Rounds, and FIDE's tables

// A round read from a Round tag: "9.2" round 9, board 2; "9" round 9; "?", "-", "" or words: not known (0).
struct RoundBoard {
    int round                                = 0;
    int board                                = 0;
    bool operator==(const RoundBoard&) const = default;
};
[[nodiscard]] RoundBoard readRound(std::string_view text);

// The rating difference FIDE's table gives a fractional score (B.02 8.1.1): `p` rounded to the hundredth, ±800 at
// 1 and 0.
[[nodiscard]] int fideRatingDifference(double p);
// A player's scoring probability against an opponent `difference` points weaker (negative: stronger), from FIDE's
// table (B.02 8.1.2); no cap applied.
[[nodiscard]] double fideExpectedScore(int difference);

enum class RatingKind : uint8_t { Standard, Rapid, Blitz };

// FIDE's development coefficient K (B.02 8.3.3): 40 until the end of the year of their 18th birthday while rated under
// 2300 (`born` known), 10 from 2400, 20 otherwise; `listed`, FIDE's list's K, in place of those when given (not 0);
// then the largest such that K × `games` does not exceed 700.
[[nodiscard]] int fideK(int rating, int born, int year, int listed, int games);

// ---------------------------------------------------------------------------------------------------------------------
// Systems and tie-breaks

enum class TournamentSystem : uint8_t {
    Unknown,
    RoundRobin,  // each plays each, `cycles` times
    Swiss,
    Team,      // a team event: its crosstable later (a team broadcast keeps Lichess's team standings)
    Knockout,  // matches of several games each round: later
};

struct SystemChoice {
    TournamentSystem system                    = TournamentSystem::Unknown;
    int cycles                                 = 1;  // a round robin's
    bool operator==(const SystemChoice&) const = default;
};

// FIDE's tie-breaks (C.07), by their acronyms there.
enum class Tiebreak : uint8_t {
    DirectEncounter,    // DE (Art. 6)
    Wins,               // WIN: the rounds won, with or without playing (7.1)
    GamesWon,           // WON: the games won over the board (7.2)
    BlackGames,         // BPG: the games played with Black over the board (7.3)
    BlackWins,          // BWG: the games won with Black over the board (7.4)
    Progressive,        // PS: the sum of the scores after each round (7.5)
    Buchholz,           // BH (8.1)
    BuchholzCut1,       // BH-C1 (14.1)
    BuchholzMedian1,    // BH-M1 (14.3)
    SonnebornBerger,    // SB (9.1)
    Koya,               // KS (9.2)
    AverageRating,      // ARO (10.1)
    AverageRatingCut1,  // ARO-C1 (14.1)
    Performance,        // TPR (10.2)
};
inline constexpr std::size_t TIEBREAKS = 14;

[[nodiscard]] std::string_view tiebreakCode(Tiebreak t);  // FIDE's acronym: "BH-C1"; what the profile keeps
[[nodiscard]] std::optional<Tiebreak> tiebreakFromCode(std::string_view code);
// The tie-breaks taken when none were chosen: a round robin's as FIDE's own Candidates Tournament 2026 orders them
// (SB, WIN, DE), a Swiss's as FIDE's Grand Swiss 2025 does (ARO-C1, BH-C1, BH, DE), ARO-C1 left out when a player has
// no rating (C.07 Art. 10). The current regulations themselves recommend none.
[[nodiscard]] std::vector<Tiebreak> fideTiebreaks(TournamentSystem system, bool all_rated);

// ---------------------------------------------------------------------------------------------------------------------
// What a crosstable is made of

inline constexpr uint32_t NO_PLAYER = UINT32_MAX;

struct CrossPlayer {
    std::string name;  // as shown: the table of names' name, else as written
    std::string federation, title;
    int elo      = 0;  // their rating in the tournament (the first their games give), 0 unrated
    int born     = 0;  // their birth year, 0 not known
    int k        = 0;  // the K of FIDE's list for the tournament's rating, 0 when not known
    int64_t fide = 0;
    std::vector<uint32_t> spellings;  // their names in the database (name ids), what bringing two together reads
};

struct CrossGame {
    uint32_t white = NO_PLAYER, black = NO_PLAYER;  // a side without player: a bye written as a game
    RoundBoard round;
    char result     = '*';  // '1', '0', '=', '*': not known, or being played when `live`
    uint32_t plies  = 0;
    uint32_t date   = 0;
    bool live       = false;  // being played (a broadcast followed)
    bool hand       = false;  // its result set by hand
    uint64_t ref    = 0;      // its number in its database, or its place among a broadcast's games
    uint32_t source = 0;      // 0: the database's; the window's own sources otherwise
};

// A result set by hand, kept with the tournament: a missing game's (`white` and `black`), or the points of a player's
// round without a game (`black` empty). Players by their names as shown.
struct HandResult {
    std::string white, black;
    int round                                = 0;
    char result                              = '*';
    double points                            = 0;
    bool operator==(const HandResult&) const = default;
};

struct CrossOptions {
    std::optional<SystemChoice> system;          // none: guessed
    std::optional<std::vector<Tiebreak>> order;  // none: FIDE's for the system
    RatingKind kind = RatingKind::Standard;
    int year        = 0;      // the tournament's, for the K of the young
    bool live       = false;  // being played: the games missing are to come
    int planned     = 0;      // the rounds planned, when known (a broadcast's)
    std::string format;       // what its organisers call it, when known ("8-player round-robin", "9-round Swiss")
    bool team = false;        // its games give teams
    std::vector<HandResult> hand;
};

// ---------------------------------------------------------------------------------------------------------------------
// The shape of a tournament: its system guessed, its games counted

struct TournamentShape {
    SystemChoice system;
    bool uncertain = false;
    std::vector<SystemChoice> choices;  // when not sure: what it can be, the guess first
    uint32_t players  = 0;
    int rounds        = 0;  // the last round its games give (0: none known)
    uint32_t games    = 0;  // counted once
    uint32_t expected = 0;  // what its system expects; 0 when not known (a team event, a knockout)
    uint32_t over_met = 0;  // pairs that met more often than its system lets them
};

// The shape of `games` (counted once) between `players` players. `team`: their games give teams.
[[nodiscard]] TournamentShape tournamentShape(uint32_t players, std::span<const CrossGame> games,
                                              const CrossOptions& options);

// ---------------------------------------------------------------------------------------------------------------------
// The crosstable

enum class CellKind : uint8_t {
    Game,     // played over the board, its result known
    Forfeit,  // a decisive result without both moving: in the points, not in the performance nor the rating change
    Bye,      // the only player unpaired in a round: a pairing-allocated bye, a win's points (C.04.1)
    Missing,  // played, its pairing known, its result not in the data (counted "?")
    Absent,   // a round without a game: a bye asked for, a withdrawal, or a game missing whose pairing is not known
    Live,     // being played
    ToCome,   // a round robin's game still to be played (a broadcast)
};

// A player's game or round.
struct CrossEntry {
    CellKind kind     = CellKind::Game;
    uint32_t opponent = NO_PLAYER;
    bool white        = false;
    double points     = 0;
    bool known        = true;  // its points known: a result, a hand's, a bye's
    bool hand         = false;
    bool unsure       = false;  // an absence whose pairing is not known: perhaps a game missing (counted "?")
    int round         = 0;
    uint32_t game     = UINT32_MAX;  // in the crosstable's games
};

// What a tie-break's value is for a player.
enum class TiebreakState : uint8_t {
    Known,
    Unsure,  // it reads a result that is not known
    None,    // not applied: a direct encounter outside a tie that it decided, a rating without rated games
};

struct CrossRow {
    uint32_t player = 0;
    int start       = 0;               // the start number: by rating, then by name
    int place_from = 0, place_to = 0;  // shared when the tie-breaks leave a tie
    double points = 0;
    int unknown   = 0;              // results not known in the points (missing games, absences unsure)
    int played    = 0;              // games with a result, over the board or by forfeit
    std::vector<double> tiebreaks;  // by the crosstable's tie-breaks
    std::vector<TiebreakState> states;
    // The performance (C.07 10.2: the average rating of the rated opponents met over the board, plus the difference
    // FIDE's table gives their score there), its parts; none without such a game.
    std::optional<int> performance;
    int opponents_rating = 0;  // their average, rounded
    double over_board    = 0;  // the points of those games
    int over_board_games = 0;
    // The rating change (B.02 8.3): K × (score − expected) over the games rated; none for an unrated player or without
    // a game rated.
    std::optional<double> change;
    double expected = 0, scored = 0;
    int rated_games = 0;
    int k           = 0;
    bool k_listed   = false;          // FIDE's list's
    std::vector<CrossEntry> entries;  // its games and rounds, by round, then as they came
};

// A game missing: its round (0 not known) and its players, or a player alone when its pairing is not known.
struct MissingGame {
    int round      = 0;
    uint32_t white = NO_PLAYER, black = NO_PLAYER;  // the pair, colours not known (white first by start number)
    uint32_t alone = NO_PLAYER;
};

// Two players that are likely one: names that can be one player's, never paired together, their rounds apart.
struct TwoNames {
    uint32_t a = 0, b = 0;                           // the one met first, then the other
    int a_from = 0, a_to = 0, b_from = 0, b_to = 0;  // their rounds
};

struct Crosstable {
    TournamentShape shape;  // what the games say, the system guessed
    SystemChoice system;    // the system it is made with: chosen, else guessed
    bool chosen = false;    // chosen by the user
    std::vector<Tiebreak> tiebreaks;
    bool fide_order = true;  // FIDE's (none chosen)
    std::vector<CrossPlayer> players;
    std::vector<CrossGame> games;
    std::vector<CrossRow> rows;  // the standings, first to last
    int rounds   = 0;
    bool unrated = false;  // a player has no rating: the tie-breaks of ratings left out of FIDE's
    int mean_elo = 0;
    std::vector<MissingGame> missing;
    std::vector<uint32_t> forfeits;  // games
    std::vector<TwoNames> two_names;
    int live        = 0;  // games being played
    RatingKind kind = RatingKind::Standard;

    // A player's row, by player.
    [[nodiscard]] const CrossRow* rowOf(uint32_t player) const;
    std::vector<uint32_t> row_of;  // each player's place in `rows`
};

// The crosstable of `games` (counted once, as crosstableInput() gives them) between `players`.
[[nodiscard]] Crosstable makeCrosstable(std::vector<CrossPlayer> players, std::vector<CrossGame> games,
                                        const CrossOptions& options);

// ---------------------------------------------------------------------------------------------------------------------
// A database's tournaments

// What finds a tournament's games in its database again.
struct TournamentRef {
    std::vector<uint32_t> events;    // its event's spellings (name ids), sorted
    int year_from = 0, year_to = 0;  // 0: its games have no date
    bool undated = false;            // its event's games without a date are its own
    std::vector<uint32_t> sites;     // the sites that tell it from another of the same event (empty: any), sorted
};

struct TournamentEntry {
    TournamentRef ref;
    std::string key;               // what tells it apart in its database, kept with its settings: "autumn open|2026"
    uint32_t event = 0, site = 0;  // the spellings shown (name ids): the most frequent
    uint32_t first_date = 0, last_date = 0;
    TournamentShape shape;
    int mean_elo        = 0;
    uint32_t duplicates = 0;  // games that came twice, counted once
};

struct TournamentList {
    std::vector<TournamentEntry> tournaments;  // the highest mean Elo first
    uint64_t games  = 0;                       // looked at
    uint64_t others = 0;                       // in no tournament: online games, games without an event
    double seconds  = 0;
    bool cancelled  = false;
};

struct TournamentProgress {
    std::atomic<uint64_t> done{0};
    std::atomic<uint64_t> total{0};
};

// The tournaments of `snapshot`, on `threads` threads (0: the machine's). A group of games is one when it has three
// players or more and its rounds are told (or each of its players met each other): online games are none. `cancel`
// stops it.
[[nodiscard]] TournamentList listTournaments(const std::shared_ptr<const Snapshot>& snapshot,
                                             TournamentProgress& progress, const std::atomic<bool>& cancel,
                                             int threads = 0);

// The games of a tournament, sorted.
[[nodiscard]] std::vector<GameId> tournamentGames(const Snapshot& snapshot, const TournamentRef& ref,
                                                  const std::atomic<bool>& cancel);

// A database tournament's players and games, its duplicates counted once.
struct TournamentInput {
    std::vector<CrossPlayer> players;
    std::vector<CrossGame> games;  // counted once: `ref` its game's number
    // The games that came twice: each group's games (the one kept first), whether their moves agree (the same, or one
    // the start of the other's: they merge as duplicates.hpp merges them) and their results.
    struct Twice {
        std::vector<GameId> games;
        bool same_moves   = true;
        bool same_results = true;
    };
    std::vector<Twice> twice;
    RatingKind kind = RatingKind::Standard;
    int year        = 0;
    bool team       = false;
};
// `list`, FIDE's rating list when taken, gives the K of the players matched with it in the table of names, when it is
// of the tournament's year or the next.
[[nodiscard]] TournamentInput tournamentInput(const Snapshot& snapshot, std::span<const GameId> ids,
                                              const FideList* list = nullptr);

// A name read as a bye's: "bye", "BYE", "-", "Freilos", or a name of no one (anonymousName()).
[[nodiscard]] bool byeName(std::string_view name);

// The text a tournament's key is made of: its event folded, its years and punctuation left out.
[[nodiscard]] std::string eventKey(std::string_view event);

}  // namespace fcg::study
