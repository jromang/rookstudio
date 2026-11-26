#pragma once

// A Lichess link pasted (#47, mockup M8, board "Lien"): what it is, recognised without asking Lichess anything; then
// its games: one game's PGN for the board, or the PGN export of a study, a broadcast, an arena, a swiss or the
// bookmarks streamed into a database, as the sync of a player's games does (lichess_games.hpp). A player's link goes
// to that sync.

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "lichess_api.hpp"

namespace fcg::study {
class Database;
}

namespace fcg::lichess {

enum class LinkKind { Game, Study, Chapter, BroadcastRound, BroadcastTour, Arena, Swiss, Player, Bookmarks };

struct Link {
    LinkKind kind = LinkKind::Game;
    std::string id;             // the game's (8 characters), the study's, round's, tournament's; the player's name
    std::string chapter;        // a chapter's id
    std::optional<bool> white;  // the side a game's link shows: /white, /black, or a player's 12-character id
    int ply                            = -1;  // the move a game's link points at (#N), -1 without
    bool operator==(const Link&) const = default;
};

// The Lichess link in `text`: an address alone (spaces around it allowed), with or without https:// and www. or a
// language's subdomain. nullopt when it is no address on lichess.org, or one RookStudio does not know:
// lichessAddress() tells those apart, so that an unknown one can be said so, without asking Lichess anything.
[[nodiscard]] std::optional<Link> parseLink(std::string_view text);
[[nodiscard]] bool lichessAddress(std::string_view text);

// Where a link's games are exported as PGN (every kind but Player, which the sync of a player's games brings), and what
// the token needs: "study:read" for a broadcast; an account for the bookmarks. A token is sent when there is one: a
// private study needs it too.
struct Export {
    std::string url;
    std::string scope;
    bool account = false;
};
[[nodiscard]] Export exportOf(const Link& link);

// One game's PGN, with its clocks, Lichess's evaluations when it was analysed, and its opening.
[[nodiscard]] Answer<std::string> gamePgn(const std::string& id, const std::string& token,
                                          const std::atomic<bool>* cancel = nullptr);
// A link's export read whole: a game's, a chapter's (for the board), or a study's.
[[nodiscard]] Answer<std::string> linkPgn(const Link& link, const std::string& token,
                                          const std::atomic<bool>* cancel = nullptr);

// The games of a link (not a player's) streamed into `db`, committed `commit_games` at a time; `games` counts those
// read as they come. A cancel keeps what was committed. Why it stopped (Failure): Lichess's refusal or the network,
// worded as the link's (a broadcast's, the bookmarks', a page's); the database's refusal in its words.
struct LinkImport : Failure {
    uint64_t read       = 0;
    uint64_t added      = 0;
    uint64_t duplicates = 0;
    uint64_t damaged    = 0;
    bool cancelled      = false;
};
[[nodiscard]] LinkImport importLink(study::Database& db, const Link& link, const std::string& token,
                                    std::atomic<uint64_t>& games, const std::atomic<bool>& cancel,
                                    std::size_t commit_games = 2000);

}  // namespace fcg::lichess
