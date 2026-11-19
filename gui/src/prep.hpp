#pragma once

// Preparing a game against an opponent (#52, mockup M8, board "Prep"), from their games in a database of their own and
// its opening tree: their games and their years, those between them and the user, what they play with each colour,
// and where the user's repertoires leave lines they play often without an answer. Nothing here asks Lichess anything.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <study/repertoire.hpp>

namespace fcg {

struct PrepMove {
    std::string san;
    uint64_t games = 0;
    double share   = 0;  // of the games from the position, 0 to 1
};

struct PrepReport {
    uint64_t games = 0;                // theirs in the database
    uint32_t first = 0, last = 0;      // their first and last dates (study::packDate), 0 unknown
    int won = 0, drawn = 0, lost = 0;  // the games between the user and them, as the user scored
    uint32_t last_between = 0;         // the last of those games' date
    std::vector<PrepMove> with_white;  // their first moves with White
    struct Against {
        std::string san;  // the first move they met most with Black
        std::vector<PrepMove> replies;
    };
    std::vector<Against> with_black;  // their answers to the two first moves they met most
    struct Gap {
        std::string repertoire;  // whose line
        study::RepertoireGap gap;
    };
    std::vector<Gap> gaps;  // where the user's repertoires leave the lines they play without an answer, most met first
};

// The report on `opponent` (their name, whatever the case) from `base`, their database and its tree; `me` the user's
// name there, empty when unknown; `repertoires` the user's, each with its name.
[[nodiscard]] PrepReport prepReport(const study::TreeBase& base, const std::string& opponent, const std::string& me,
                                    const std::vector<std::pair<std::string, study::Repertoire>>& repertoires,
                                    std::size_t gaps = 3);

}  // namespace fcg
