#pragma once

// From the Board to lichess.org (#54, mockup M8, board "Lien"): a position opened on its analysis board, a game shared
// (imported, without an account), a position played out (an open challenge, a link for each side), a game of Lichess
// bookmarked (preference:write). What each sends is the position, the game, the time control, or the game's id: nothing
// else of the user's.

#include <atomic>
#include <string>

#include "lichess_api.hpp"

namespace fcg::lichess {

inline constexpr const char* SCOPE_PREFERENCE_WRITE = "preference:write";

// Lichess's analysis board on a position: the position in the address, nothing else.
[[nodiscard]] std::string analysisUrl(const std::string& fen, bool chess960, bool black_below);

// A game imported, without an account: its address on lichess.org, which anyone who has it can open.
[[nodiscard]] Answer<std::string> importGame(const std::string& pgn, const std::atomic<bool>* cancel = nullptr);

// A time control for a game played out: minutes and seconds added a move, or days a move (correspondence).
struct ChallengeClock {
    int minutes                                  = 10;
    int increment                                = 5;
    int days                                     = 0;  // > 0: correspondence, the clock left out
    bool operator==(const ChallengeClock&) const = default;
};
// "10+5", "3 days"
[[nodiscard]] std::string clockText(const ChallengeClock& clock);

// An open challenge from a position: whoever has a side's link plays it; not rated.
struct OpenChallenge {
    std::string id;
    std::string url;           // the challenge's page
    std::string white, black;  // each side's link
};
[[nodiscard]] Answer<OpenChallenge> openChallenge(const std::string& fen, bool chess960, const ChallengeClock& clock,
                                                  const std::atomic<bool>* cancel = nullptr);

// A game of Lichess bookmarked (or not) on the account: preference:write. 401 or 403 when the token cannot.
[[nodiscard]] Answer<bool> bookmarkGame(const std::string& id, const std::string& token, bool on = true,
                                        const std::atomic<bool>* cancel = nullptr);

}  // namespace fcg::lichess
