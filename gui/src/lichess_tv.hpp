#pragma once

// Lichess TV (#86, mockup M12, board 7): its channels of standard chess, each with the game Lichess features on it
// (/api/tv/channels), and a channel's games in progress, thirty at most (/api/tv/{channel}); no account. The games are
// followed as the followed players' are (PlayersFollower: whole, three moves behind, then their moves as Lichess shows
// them to anyone), in the broadcasts' mosaic and on the Board. The variants' channels are left out.

#include <atomic>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include "lichess_api.hpp"

namespace fcg::lichess {

// The channels shown, in the order of Lichess's TV page: "best" (top rated), "bullet", "blitz", "rapid", "classical",
// "ultraBullet", "bot", "computer". Those of the variants (crazyhouse, atomic, chess960…) are left out.
[[nodiscard]] std::span<const std::string_view> tvChannels();
[[nodiscard]] bool isTvChannel(std::string_view key);
inline constexpr std::size_t TV_MOST_GAMES = 30;  // the games of a channel Lichess lists at most
inline constexpr std::string_view TV_MOSAIC =
    "tv:";  // the mosaic of a channel, as the Lichess page names it: "tv:blitz"

// A channel's featured game, as /api/tv/channels names it: its id, and the player the channel follows in it.
struct TvFeatured {
    std::string channel;               // its key, one of tvChannels()
    std::string game;                  // the game's id
    std::string user, user_id, title;  // the player followed: as written, lowercase, their title
    int rating = 0;
    bool white = true;  // that player's side
};
// The channels shown, in their order, from /api/tv/channels's answer (its keys in camel case; the older names, "Top
// Rated", "Bot"…, read too).
[[nodiscard]] std::vector<TvFeatured> readTvChannels(const nlohmann::json& j);

// A side of a game in progress, as Lichess's game exports give it.
struct TvPlayer {
    std::string name, id, title;  // id lowercase; all empty for Lichess's AI
    int rating = 0;
    int ai     = 0;  // the level of Lichess's AI when it plays this side
};
// A game in progress, as Lichess's exports give it in NDJSON (a channel's games, games by their ids).
struct TvGame {
    std::string id;
    TvPlayer white, black;
    std::string speed;                    // "blitz"
    std::string variant = "standard";     // "standard", "fromPosition", "chess960"…
    int64_t initial = -1, increment = 0;  // the clock, in seconds; initial -1 without one
    std::string status;                   // "started" while it goes on
    [[nodiscard]] bool standard() const { return variant.empty() || variant == "standard"; }
};
[[nodiscard]] std::vector<TvGame> readTvGames(std::string_view ndjson);

// The clock as Lichess writes it: "3+0", "¼+0", "½+0", "1+0", "30+20"; empty without one.
[[nodiscard]] std::string tvClockText(int64_t initial, int64_t increment);
// The same from a PGN's TimeControl ("180+0", in seconds; "-" without a clock).
[[nodiscard]] std::string tvClockText(std::string_view time_control);
// The level of Lichess's AI as a PGN names it ("lichess AI level 8"); 0 for anyone else.
[[nodiscard]] int aiLevelOf(std::string_view name);

// Whether `own` (lowercase account ids: the user's own account, their bot) plays the game.
[[nodiscard]] bool playedBy(const TvGame& game, const std::vector<std::string>& own);
// The same from the names of a game's sides (its PGN's White and Black).
[[nodiscard]] bool playedBy(std::string_view white, std::string_view black, const std::vector<std::string>& own);

// The games a channel's mosaic follows, in the order of its tiles. Those it shows keep their place; one that is over
// (ended, or given up: `over` holds every game known to be, shown or not) gives it to the next game listed that is not
// shown yet nor over, the featured game first, never one of `own` nor a variant's; then more while there is room. An
// ended game keeps its place, its result shown, while it is in `resting` (a few seconds) or nothing can take it.
// `room` bounds the games still going on (a stream each), not those that ended.
[[nodiscard]] std::vector<std::string> tvPlaces(const std::vector<std::string>& shown,
                                                const std::set<std::string>& over, const std::vector<TvGame>& listed,
                                                const std::string& featured, const std::vector<std::string>& own,
                                                std::size_t room, const std::set<std::string>& resting = {});

// A channel's games as the mosaic needs them: its featured game's id, then its games in progress as Lichess lists them,
// the best rated first (/api/tv/channels, then /api/tv/{channel}?nb=: two requests).
struct TvList {
    std::string featured;
    std::vector<TvGame> games;
};
[[nodiscard]] Answer<TvList> tvList(const std::string& channel, std::size_t nb = TV_MOST_GAMES,
                                    const std::atomic<bool>* cancel = nullptr);

// The Lichess page's card: each channel's featured game, with both its sides and its clock when Lichess gave them
// (/api/tv/channels, then the featured games by their ids, POST /api/games/export/_ids: two requests).
struct TvRow {
    TvFeatured featured;
    TvGame game;  // its sides and clock; game.id empty when Lichess did not give them
};
[[nodiscard]] Answer<std::vector<TvRow>> tvRows(const std::atomic<bool>* cancel = nullptr);

// The game the Board goes on to once the one it followed ended: the channel's featured game, unless it is that one or
// one of `own`'s; else the first game listed that is neither. Empty when there is none yet.
struct TvNext {
    std::string game;
    bool featured = false;
    TvPlayer white, black;
    std::string clock;  // "3+0"
};
[[nodiscard]] TvNext tvNext(const std::string& ended, const TvRow& featured, const std::vector<TvGame>& listed,
                            const std::vector<std::string>& own);
// The same asked of Lichess: the channels, the featured game's sides, and the channel's games when it will not do.
// While Lichess still names the game that ended (it chooses the next one within seconds), none, unless `others`: the
// caller asks again a moment later, then takes another of the channel's.
[[nodiscard]] Answer<TvNext> askTvNext(const std::string& channel, const std::string& ended,
                                       const std::vector<std::string>& own, bool others = true,
                                       const std::atomic<bool>* cancel = nullptr);

// A side as the card and the tiles name it: Lichess's AI as `ai_name` says it ("Lichess AI, level 8", in the reader's
// language), a title before the name (not BOT: the Bot channel's players are all bots), the rating after, in brackets
// when `brackets`.
[[nodiscard]] std::string tvSide(const TvPlayer& p, std::string_view ai_name, bool brackets);

}  // namespace fcg::lichess
