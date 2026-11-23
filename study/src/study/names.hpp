#pragma once

// The players of a database brought together (mockup M13, board 1): its spellings of one player gathered, matched
// with FIDE's rating list when the user took it. What it proposes is a list of players, each a group of spellings with
// what tells who it is:
// - brought together at once: the FIDE id its games give (Lichess's broadcasts give it), or a single player of FIDE's
//   list with that name (its transliterations too, when the games' ratings are near the player's);
// - to confirm: several players of the list may be it, the nearest to its games' ratings proposed first;
// - not on the list (or no list): kept as they are; two spellings of one surname and given name can be brought
//   together by hand.
// The table of names (players.hpp) keeps what was brought together; the games are not changed.

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fide_list.hpp"
#include "store.hpp"

namespace fcg::study {

// A name of the White or Black column, and what its games say of it.
struct Spelling {
    uint32_t name  = 0;  // its id
    uint64_t games = 0;
    int elo_min = 0, elo_max = 0;  // the ratings its games give it, 0 when none
    double elo_mean = 0;
    int year_min = 0, year_max = 0;  // the years of its games, 0 when none is known
    uint32_t fide = 0;               // the FIDE id its games give (the most of them), 0 when none
    std::string source;              // the file most of its games come from
    GameId sample = 0;               // one of its games
};

enum class NameMatch : uint8_t {
    Done,     // already one player of the table
    Auto,     // brought together at once
    Confirm,  // several players of FIDE's list may be it
    None,     // not on the list, or no list: kept as it is
};

enum class NameReason : uint8_t {
    Table,            // the table has it
    FideIdInGames,    // its games give its FIDE id
    OneOnList,        // a single player of the list has its name
    Transliteration,  // a single player of the list has its name as another alphabet writes it, near its ratings
    Several,          // several players of the list may be it
    NotOnList,
    NoList,
};

struct PlayerProposal {
    std::vector<std::size_t> spellings;  // indices in NamesAnalysis::spellings, the most played first
    NameMatch match   = NameMatch::None;
    NameReason reason = NameReason::NoList;
    std::optional<std::size_t> player;  // the player of the table it is (Done), or joins
    uint32_t fide = 0;                  // the FIDE id its games give
    // The players of FIDE's list it may be (their places in the list), the one proposed first.
    std::vector<std::size_t> candidates;
    std::string name;  // the name it takes: FIDE's, else the fullest spelling
    uint64_t games = 0;
    int elo_min = 0, elo_max = 0;
    int year_min = 0, year_max = 0;
};

struct NamesProgress {
    std::atomic<uint64_t> done{0};
    std::atomic<uint64_t> total{0};
};

struct NamesAnalysis {
    std::vector<Spelling> spellings;
    std::vector<PlayerProposal> proposals;  // to confirm first, then brought together at once, then kept as they are
    std::string list_month;                 // the FIDE list read, "" without one
    bool cancelled = false;
    double seconds = 0;
    [[nodiscard]] std::size_t count(NameMatch match) const;
    // The proposals that bring something together: Auto ones with two spellings or more, or a FIDE player.
    [[nodiscard]] std::size_t bringing() const;
};

// The players of `snapshot` (its White and Black names), their table read, matched with `list` when given.
[[nodiscard]] NamesAnalysis analyseNames(const std::shared_ptr<const Snapshot>& snapshot, const FideList* list,
                                         NamesProgress& progress, const std::atomic<bool>& cancel);

// The table `before` with proposals brought together: each proposal given with the candidate chosen (an index in its
// candidates; none: no FIDE player, its spellings together all the same). `how` says why ("fide-id", "list", "hand").
struct Bringing {
    std::size_t proposal = 0;
    std::optional<std::size_t> candidate;
};
[[nodiscard]] PlayerTable bringTogether(const PlayerTable& before, const NamesAnalysis& analysis,
                                        const std::vector<Bringing>& bringing, const FideList* list,
                                        const Snapshot& snapshot);

// The players of FIDE's list who may be `name` ("Surname, Given"), as the names' analysis finds them: the same surname
// (else the same as another alphabet writes it), given names that can be the same, not born after `first_year` less
// five (0: not known); none for a name of one word. The nearest to `elo` (the games' mean rating, 0 not known) first.
[[nodiscard]] std::vector<std::size_t> fideNamesakes(const FideList& list, std::string_view name, int first_year,
                                                     double elo);

// The spellings of the games numbered `from` or more (an import's) that join a player of the table by themselves: the
// FIDE id their games give is the player's, or they write the player's surname and given name in full. None when
// nothing joins one (or the table is empty): the table to commit otherwise.
[[nodiscard]] std::optional<PlayerTable> joinNewSpellings(const Snapshot& snapshot, GameId from);

}  // namespace fcg::study
