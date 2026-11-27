#pragma once

// Duplicate games (mockup M13, board 1): one game come from two sources, a Lichess broadcast and TWIC for instance,
// its headers written differently, so that the import did not know it for one it had (duplicateKey() reads the
// headers). Found by their players (read in the table of names; else their surnames and first initials), their year,
// rounds and dates that do not differ, and their moves: the same, or those of one the start of the other's (a
// broadcast cut short). A group is merged into one game: the fullest of each field, from one game or another, the
// other games marked deleted (Database::merge(), undone by Database::restore()).

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "background.hpp"
#include "store.hpp"

namespace fcg::study {

enum class DuplicateKind : uint8_t {
    SameMoves,  // the same moves, headers written differently
    Shorter,    // the moves of one the start of the other's: a broadcast cut short
    Results,    // results that differ: to see one at a time
};

struct DuplicateGroup {
    std::vector<GameId> games;  // two or more, the oldest (the smallest number) first
    DuplicateKind kind = DuplicateKind::SameMoves;
};

using DuplicateProgress = JobProgress;

struct Duplicates {
    std::vector<DuplicateGroup> groups;  // by kind, then by their first game
    uint64_t games = 0;                  // looked at
    double seconds = 0;
    bool cancelled = false;
    [[nodiscard]] std::size_t count(DuplicateKind kind) const;
};

// The duplicate games of `snapshot`, on `threads` threads (0: the machine's); only the groups that hold a game numbered
// `since` or more (an import's games: 0 for all). The deleted games and the pairs kept apart (the table of names'
// apart()) are left out. `cancel` stops it, the groups found so far given.
[[nodiscard]] Duplicates findDuplicates(const std::shared_ptr<const Snapshot>& snapshot, GameId since,
                                        DuplicateProgress& progress, const std::atomic<bool>& cancel);

// The fields a merge takes from one game or another, the comparison's rows.
enum class MergeField : uint8_t {
    White,
    Black,
    Event,
    Site,
    Round,
    Date,
    Ratings,  // WhiteElo and BlackElo
    Eco,
    FideIds,  // WhiteFideId and BlackFideId
    Moves,    // with the text of an annotated game, its start position
    Clocks,
    Evals,
    Result,
};
inline constexpr std::size_t MERGE_FIELDS = 13;

// For each field, the game of the group (its index) it is taken from.
using MergeChoice = std::array<uint8_t, MERGE_FIELDS>;

// Each game's value of each field, as the comparison shows it: "" when the game has none. Moves as their number of
// plies, clocks and evaluations as how many plies have one.
[[nodiscard]] std::vector<std::array<std::string, MERGE_FIELDS>> mergeFields(const std::vector<ImportedGame>& games);
// The fullest of each field: a name in full over an initial, a longer event, a round that says more ("9.2" over "9"),
// a date that knows more, both ratings, the longer moves (an annotated game's for the same moves), the most clocks and
// evaluations, a result over none.
[[nodiscard]] MergeChoice fullestChoice(const std::vector<ImportedGame>& games);
// The group's games merged as `choice` says, the other tags of all of them kept (the moves' game's first). The game it
// is made from says where each field came from in its MergedFrom tag: "<number> <source>: <fields>; ...", the sources
// given beside the games (their numbers, their sources' names).
[[nodiscard]] ImportedGame mergeGames(const std::vector<ImportedGame>& games, const MergeChoice& choice,
                                      const std::vector<std::pair<GameId, std::string>>& origins = {});

// A game's source as people name it: "TWIC 1672", "Lichess broadcasts 2026-10", "club.pgn" from the file it was
// imported from; else from its tags: "Lichess broadcast", "Lichess", "chess.com"; empty when not known.
[[nodiscard]] std::string sourceLabel(std::string_view file, const ImportedGame& game);

// Two rounds that can be one: the same, one of them not known ("?", "-"), or one the start of the other ("9", "9.2").
[[nodiscard]] bool roundsCompatible(std::string_view a, std::string_view b);
// Two dates (packDate()) that can be one: no part known in both that differs.
[[nodiscard]] bool datesCompatible(uint32_t a, uint32_t b);

}  // namespace fcg::study
