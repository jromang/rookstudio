#pragma once

// The game list's views: the games of a snapshot that pass a filter, in the order of one of its columns. A view is
// a list of game ids, so the list pages through it with no query per page. A filter reads each name once (folded:
// letter case and accents left out), then each game in a few operations on its columns, on several cores; a sort
// reads one column (names by their rank in alphabetical order) and sorts it by its bytes, stable.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "store.hpp"

namespace fcg::study {

enum class SortBy : uint8_t { Number, White, Black, Event, Site, Round, Date, WhiteElo, BlackElo, Eco, Plies, Result };

struct Filter {
    std::string words;       // each word in a player's name, the event or the site
    std::string white;       // words of White's name
    std::string black;       // of Black's
    std::string player;      // of either player's
    uint32_t date_from = 0;  // packDate(); 0: no bound. A date not known is outside a bounded range.
    uint32_t date_to   = 0;
    uint16_t elo_min   = 0;  // both players at least
    uint16_t eco_from  = 0;  // packEco(); 0: no bound
    uint16_t eco_to    = 0;
    char result        = 0;      // '1', '0', '=' or '*'; 0 for any
    bool annotated     = false;  // only games with comments, symbols or variations
    bool damaged       = false;  // only games a move of which could not be read (kept up to it)
    bool with_deleted  = false;  // the deleted games too

    [[nodiscard]] bool empty() const noexcept;
};

struct View {
    std::shared_ptr<const Snapshot> snapshot;
    std::vector<GameId> ids;  // the games, in the view's order
};

// A text for matching: letters in lower case, the accented ones of Latin alphabets without their accents.
[[nodiscard]] std::string foldText(std::string_view text);

// The games of `snapshot` that pass `filter`, sorted by `sort` (Number: the database's order), on `threads` cores
// (0: the machine's). `cancel`, when given and set, stops it: the view is then empty.
[[nodiscard]] View makeView(const std::shared_ptr<const Snapshot>& snapshot, const Filter& filter, SortBy sort,
                            bool descending = false, int threads = 0, const std::atomic<bool>* cancel = nullptr);

}  // namespace fcg::study
