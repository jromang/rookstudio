#pragma once

// What the user decided about Lichess (mockup M4): each online service of the analysis allowed or not, the explorer,
// the new games counted on the home, and the player whose games were imported last. In the profile's lichess.json; the
// account's token is apart (lichess-account.json, readable by its owner alone).

#include <string>
#include <vector>

#include <json.hpp>

namespace fcg {

struct LichessSettings {
    int cloud      = 0;      // the cloud's evaluation: 0 asked at its first use, 1 always, 2 never
    int tablebase  = 0;      // the tables online, up to 7 pieces: the same
    bool explorer  = true;   // Lichess's explorer in the Tree tab, the account linked
    bool count_new = false;  // the new games of the player counted when the home opens
    std::string player;      // whose games were imported last
    // The Tree tab's "Lichess" source: the speeds and the rating groups of its games.
    std::vector<std::string> explorer_speeds{"blitz", "rapid", "classical"};
    std::vector<int> explorer_ratings{2000, 2200, 2500};
    // The broadcast mosaic (M4, board 4): the engine of the library that analyses its games, none when empty, and
    // its threads for each game.
    std::string broadcast_engine;
    int broadcast_threads = 1;
    // A player's games (M4, board 3): the speeds asked for (0 all, 1 blitz, rapid and classical, 2 classical and
    // correspondence), from when (0 the first game, 1 a year back, 2 the date below, "2024-01-31").
    int sync_speeds = 0;
    int sync_since  = 0;
    std::string sync_date;
    bool listen_declined = false;  // "Not now" said to hearing the user's games start and end (#48)
    bool review_finished = false;  // a game of the user's that ended opened on the Board, its review started (#49)
    // The players followed live (#50): those added by name; "Without it" said to reading whom the account follows.
    std::vector<std::string> followed;
    bool follow_declined = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LichessSettings, cloud, tablebase, explorer, count_new, player,
                                                explorer_speeds, explorer_ratings, broadcast_engine, broadcast_threads,
                                                sync_speeds, sync_since, sync_date, listen_declined, review_finished,
                                                followed, follow_declined)

}  // namespace fcg
