#pragma once

// When the arenas' module (lichess_arenas_page.cpp, #95) asks Lichess, as plain functions of what it holds and of the
// time (#111), without ImGui: its poll() calls each one where it decides, and the tests drive them with a clock of
// their own. The card's list is a Refreshing<T> of the module's (refreshing.hpp), asked every minute while the card
// shows; the rest is here: the user's teams' Swiss tournaments, the tournament followed in the mosaic (its standing,
// the games of its first eight), the arena played (its page, its joins again) and the Swiss played or looked at. One
// request at a time for each; after a 429 the module saw (`waits_until`), the standings and the joins wait for the
// host's pause to end, and say so, while the card's lists wait in their host's turn (HostGate).

#include <chrono>
#include <cstddef>

#include "lichess_arenas.hpp"

namespace fcg::lichess {

inline constexpr auto ARENA_LIST_EVERY     = std::chrono::minutes(1);   // the card's list of arenas
inline constexpr auto TEAM_SWISSES_EVERY   = std::chrono::minutes(5);   // the user's teams' Swiss tournaments
inline constexpr auto SWISS_STANDING_EVERY = std::chrono::seconds(30);  // a Swiss followed: its standing after a round
inline constexpr auto SWISS_PLAYED_EVERY   = std::chrono::seconds(20);  // the Swiss joined, or in progress
inline constexpr auto SWISS_LOOKED_EVERY   = std::chrono::minutes(1);   // a Swiss to come, looked at

using SteadyTime = std::chrono::steady_clock::time_point;

// The user's teams' Swiss tournaments (team:read): asked while the card shows, with an account that may read its
// teams, again after five minutes; at once with a token they were not read with (the first time, another account).
struct TeamSwissesAsk {
    bool shown     = false;  // the card, drawn lately
    bool readable  = false;  // an account linked that may read its teams
    bool new_token = false;  // not the token they were read with
    bool asking    = false;  // their request under way
    SteadyTime asked{};      // when they were asked last
};
[[nodiscard]] bool teamSwissesDue(const TeamSwissesAsk& ask, SteadyTime now);

// The followed tournament's standing: asked while the mosaic draws it, every 15 s (STANDING_EVERY), a Swiss's every
// 30 s (its standing after each round); not during the host's pause after a 429.
struct StandingAsk {
    bool drawn  = false;       // the mosaic shows it, drawn lately
    bool swiss  = false;       // a Swiss, not an arena
    bool asking = false;       // its request under way
    SteadyTime asked{};        // when it was asked last
    SteadyTime waits_until{};  // after a 429: the end of the host's pause
};
[[nodiscard]] bool standingDue(const StandingAsk& ask, SteadyTime now);

// The games of its first eight, from their status: asked while the mosaic shows it and its games are being played, at
// once when the first eight change, then every 15 s while a tile's game is over or tiles wait for a game; not during
// the host's pause after a 429.
struct StatusesAsk {
    bool shown         = false;  // the mosaic shows it
    bool playing       = false;  // its games are being played (an arena started, a Swiss's round under way)
    std::size_t top    = 0;      // its first eight, as the standing names them now
    bool top_changed   = false;  // not those whose status was asked last
    bool some_over     = false;  // a tile's game is over
    std::size_t places = 0;      // the tiles' games
    bool asking        = false;
    SteadyTime asked{};
    SteadyTime waits_until{};
};
[[nodiscard]] bool statusesDue(const StatusesAsk& ask, SteadyTime now);

// The arena played from here: its page with the user's token (their rank, their verdicts) every 15 s while they are in
// it or look at it, until it is over; not during the host's pause after a 429.
struct ArenaPageAsk {
    bool looked = false;  // the user is in it, or the Play page's part or the Home page shows it
    bool over   = false;
    bool asking = false;
    SteadyTime asked{};  // none: asked again at once (joined, a game ended)
    SteadyTime waits_until{};
};
[[nodiscard]] bool arenaPageDue(const ArenaPageAsk& ask, SteadyTime now);

// The arena joined again while the user waits in it (Lichess pairs a player away from its page for a minute after
// each join): the seat due, the account linked, no join under way, not during the host's pause after a 429.
[[nodiscard]] bool rejoinDue(const ArenaSeat& seat, bool linked, bool joining, SteadyTime waits_until, SteadyTime now);

// The Swiss played or looked at from the Play page: its page every 20 s while the user is in it or it is under way,
// else every minute, while the Play page's part shows or the user is in it; never again once it is over with its
// standing and its games read.
struct SwissPageAsk {
    bool looked   = false;  // the Play page's part shows, or the user is in it
    bool joined   = false;
    bool started  = false;  // under way
    bool complete = false;  // over, its standing and its games by round read
    bool asking   = false;
    SteadyTime asked{};  // none: asked again at once (joined, a game of it started)
};
[[nodiscard]] bool swissPageDue(const SwissPageAsk& ask, SteadyTime now);

// Whether a Swiss's standing is asked with its page (`info`, none until read): none read yet, one read before another
// round (`rows_round`), or the Swiss over (its standing final).
[[nodiscard]] bool swissRowsDue(bool rows, const SwissInfo* info, int rows_round);

}  // namespace fcg::lichess
