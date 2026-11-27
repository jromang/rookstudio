#pragma once

// A player's dossier (#90, mockup M13, board 3): what their games in the user's databases say of them, made again each
// time it is opened from the databases as they are (nothing of it is kept). Their games are found through each
// database's table of names (players.hpp): all their spellings together, a namesake's games left out by the FIDE id
// the games give. From those games: the openings they play with each colour (Lichess's named openings, by ECO code),
// their results (against stronger and weaker players, against the user), their rating year by year from the Elo of
// their games, their pawn structures at move 20 (named by the caller: the assistant's facts), their last games, and
// the tree of their games from each side, which the Board page's Tree tab and the repertoires' coverage read.
//
// The games of their Lichess account, when it is known, are a part of their own (online), told apart from those of
// the databases (over the board): each figure is of one part, or of both.

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "background.hpp"
#include "opening_names.hpp"
#include "players.hpp"
#include "repertoire.hpp"
#include "scan_board.hpp"
#include "store.hpp"
#include "tree.hpp"

namespace fcg::study {

// ---------------------------------------------------------------------------------------------------------------------
// Who, and where

// The player a dossier is of.
struct DossierSubject {
    std::string name;     // as everything shows it: the table's name, else a spelling
    int64_t fide = 0;     // their FIDE id, 0 when not known
    int born     = 0;     // their birth year, 0 when not known
    std::string lichess;  // their Lichess account, empty when none
};

// A database the dossier reads, and the subject's name ids in it.
struct DossierSource {
    std::string name;  // the database's
    std::shared_ptr<const Snapshot> snapshot;
    std::vector<uint32_t> spellings;  // theirs, sorted
    // A namesake's: the spellings of a player of the table with another FIDE id. Only their games that give the
    // subject's FIDE id are the subject's.
    std::vector<uint32_t> doubtful;
    bool online = false;  // the games of their Lichess account
};

// The subject's spellings in a snapshot: those of the table's player with their FIDE id, and of the players and names
// written as their name is (letter case and accents aside); a player of that name with another FIDE id is a namesake
// (doubtful). Online, the names that are their Lichess account (letter case aside).
[[nodiscard]] DossierSource findSubject(std::string database, std::shared_ptr<const Snapshot> snapshot,
                                        const DossierSubject& subject, bool online = false);

// ---------------------------------------------------------------------------------------------------------------------
// The games

enum class Speed : uint8_t { Unknown, Bullet, Blitz, Rapid, Classical, Correspondence };

// A game's speed from its time control ("5400+30", "40/7200:3600", "180+2", "1/259200", "-") and, without one, from its
// event's words ("Blitz", "Rapid"). Online it is counted as Lichess counts it (the base and 40 increments: under 3
// minutes bullet, under 8 blitz, under 25 rapid), over the board as FIDE does (the base and 60 increments: 10 minutes
// at most blitz, under an hour rapid); a game of the board that says nothing is classical.
[[nodiscard]] Speed gameSpeed(std::string_view time_control, std::string_view event, bool online);

struct DossierGame {
    uint32_t source = 0;  // in the sources given
    GameId id       = 0;
    HeaderNumbers numbers;
    bool white        = true;  // the subject had White
    bool online       = false;
    bool user         = false;  // against the user
    Speed speed       = Speed::Unknown;
    uint32_t opponent = 0;            // the opponent's name id
    int first_move    = -1;           // its first move's code (ScanBoard::encode), -1 none
    int opening       = -1;           // the last named opening of its first 40 plies (OpeningNames), -1 none
    bool at_move_20   = false;        // it went on to its 40th ply
    std::array<uint64_t, 2> pawns{};  // White's and Black's pawns there

    [[nodiscard]] int ownElo() const noexcept { return white ? numbers.white_elo : numbers.black_elo; }
    [[nodiscard]] int opponentElo() const noexcept { return white ? numbers.black_elo : numbers.white_elo; }
    // The subject's points: 1 a win, 0.5 a draw, 0 a loss; none for a game without a result.
    [[nodiscard]] std::optional<double> points() const noexcept;
};

// The subject's results in a set of games.
struct DossierScore {
    uint32_t games = 0;  // all of them, those without a result too
    uint32_t won = 0, drawn = 0, lost = 0;
    void add(const DossierGame& game) noexcept;
    [[nodiscard]] uint32_t decided() const noexcept { return won + drawn + lost; }
    // The subject's score, 0 to 1; 0 without a result.
    [[nodiscard]] double share() const noexcept;
    bool operator==(const DossierScore&) const = default;
};

// The parts of a dossier: the games of the databases (over the board), those of the Lichess account, or both.
enum class DossierParts : uint8_t { Board = 1, Online = 2, Both = 3 };

struct DossierOptions {
    const OpeningNames* openings = nullptr;  // none: no openings named
    // Whether an opponent's name is one of the user's (their games against the subject); none: none is.
    std::function<bool(std::string_view name)> user;
    uint32_t tree_plies = TREE_PLIES;  // the plies of each game its tree follows
    int threads         = 0;           // 0: the machine's
};

using DossierProgress = JobProgress;

class Dossier {
   public:
    std::vector<DossierSource> sources;
    std::vector<DossierGame> games;          // the latest first
    const OpeningNames* openings = nullptr;  // the table that named their openings (DossierGame::opening), the caller's
    bool cancelled               = false;
    double seconds               = 0;

    // The moves their games played from a position (its Polyglot key) in their first tree plies, the subject with
    // White or Black, of one part or both: as the opening tree tells them (the most played first, the games that end
    // there last); none when no game of theirs reaches it.
    [[nodiscard]] std::optional<TreeAnswer> at(uint64_t key, bool white, DossierParts parts) const;
    // The same as the repertoires' coverage reads moves.
    [[nodiscard]] MovesAt moves(bool white, DossierParts parts) const;
    // Whether a game is of the parts.
    [[nodiscard]] static bool of(const DossierGame& game, DossierParts parts) noexcept {
        return (static_cast<int>(parts) & (game.online ? 2 : 1)) != 0;
    }

    // A position each game reaches first, and the move it played from there: its tree, by key.
    struct Reach {
        uint64_t key  = 0;
        int16_t code  = TreeMove::END;
        uint32_t game = 0;  // in games
    };
    std::array<std::vector<Reach>, 2> reach;  // the games with White, with Black; sorted by key
};

// The subject's games in `sources` and what they tell, in the background on `options.threads` threads: every game of
// each snapshot read for its players (the subject's spellings), then each of theirs replayed. `cancel` stops it within
// a tenth of a second, the dossier then marked cancelled.
[[nodiscard]] Dossier makeDossier(std::vector<DossierSource> sources, const DossierSubject& subject,
                                  const DossierOptions& options, DossierProgress& progress,
                                  const std::atomic<bool>& cancel);

// ---------------------------------------------------------------------------------------------------------------------
// What it says

// One colour's games of an ECO code (the last named opening of their first 40 plies).
struct DossierLine {
    std::string eco;
    std::optional<std::size_t> opening;  // the code's shortest line, in OpeningNames
    DossierScore score;
    uint32_t last = 0;            // the latest date (packDate())
    std::vector<uint32_t> games;  // in Dossier::games, the latest first
};

// A year's rating: the mean of their Elo in their games of the year, their classical games when the year has some.
struct DossierYear {
    int year       = 0;
    int elo        = 0;
    uint32_t rated = 0;  // the games it is the mean of
};

// A pawn structure at move 20 (their games' 40th ply), as the caller names them: its kind, whether it is the subject's
// side's (the owner of an isolated pawn, of a bind) and the subject's colour.
struct DossierStructure {
    int kind    = 0;
    bool theirs = false;
    bool white  = true;
    DossierScore score;
    std::vector<uint32_t> games;  // in Dossier::games, the latest first
};

// The structures of a position's pawns (White's, Black's): each a kind of the caller's and its owner (0 White, 1
// Black), added to `out`.
using StructureNamer =
    std::function<void(uint64_t white_pawns, uint64_t black_pawns, std::vector<std::pair<int, int>>& out)>;

// An opening family's share of a set of games.
struct DossierFamily {
    std::string family;
    DossierScore score;
};

struct DossierFigures {
    uint32_t games = 0;
    uint32_t first = 0, last = 0;  // their first and last dates (packDate()), 0 not known
    DossierScore all, white, black;
    // Against a stronger player (the opponent's Elo higher than theirs), a weaker one (lower); both Elo known.
    DossierScore stronger, weaker;
    DossierScore user;                                  // against the user
    std::optional<std::size_t> last_user;               // their last game against the user, in Dossier::games
    std::array<uint32_t, 6> speeds{};                   // by Speed
    std::vector<DossierLine> lines_white, lines_black;  // the most played first
    std::vector<DossierYear> years;                     // in order
    int highest           = 0;  // their highest Elo (of their classical games when they have some)
    uint32_t highest_date = 0;
    std::vector<DossierStructure> structures;  // the most met first
    // Per source (Dossier::sources): their games, the first and last years.
    struct Source {
        uint32_t games = 0;
        int first_year = 0, last_year = 0;
    };
    std::vector<Source> sources;
};

// The figures of a part of the dossier (or of both); `structures` names the pawn structures (none: no structures).
[[nodiscard]] DossierFigures dossierFigures(const Dossier& dossier, DossierParts parts,
                                            const StructureNamer& structures = {});

// The opening families of their games with a colour, after a first move (its code at the start; -1 any), the most
// played first.
[[nodiscard]] std::vector<DossierFamily> dossierFamilies(const Dossier& dossier, DossierParts parts, bool white,
                                                         int first_move, const OpeningNames& openings);

}  // namespace fcg::study
