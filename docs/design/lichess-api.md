# Lichess API and data services: inventory and integration study for RookStudio

> The study behind issues #44 to #58 (2026-11-18). The endpoints those issues name were checked against the spec afterwards, as were the broadcasts' `study:read` requirement, the mandatory User-Agent and the tablebase fields; the rest is as the study found it.

Date: 2026-11-18. Research only: no Lichess endpoint was called, no account was used, nothing was posted.

## Sources

- OpenAPI spec of github.com/lichess-org/api, `doc/specs/lichess-api.yaml` version 2.0.180 (commit `448070d`, 2026-11-16), and every file it references under `doc/specs/tags/`, `schemas/`, `examples/`. The repository README states that https://lichess.org/api is generated from it. lichess.org itself refuses automated page fetches (HTTP 403), so the rendered page was not read; the spec is its source.
- The repository history since 2025-01-01 (797 commits), for what was added, changed, deprecated or removed.
- https://database.lichess.org, read on 2026-11-18 (games, variants, puzzles, evaluations, broadcasts, licences, known issues).
- Spot checks in lila's public source (github.com/lichess-org/lila, master): `conf/routes`, the auth wrappers of a few controllers, the analysis-board URL parser, the study PGN comment parser, and the FAQ translation source for the correspondence rule. Also the READMEs of lichess-org/external-engine and lichess-org/chess-openings.

The spec has 24 tags and 190 operations. Section 3 lists all of them.

## Contents

1. Ground rules for any integration
2. The DONE integrations after the 2025-2026 changes (action items)
3. Endpoint inventory (190 operations, by tag)
4. Data services: database.lichess.org and related open data
5. Integration proposals (DONE, PLANNED, NEW)
6. Scope plan: which scope, for which feature, asked when
7. Changes in 2025-2026
8. Dropped ideas, out-of-spec routes, and what could not be verified

## 1. Ground rules for any integration

Hosts

| Host | Serves |
|---|---|
| `lichess.org` | everything else |
| `explorer.lichess.org` | opening explorer (moved from `explorer.lichess.ovh` in 2026-03) |
| `tablebase.lichess.org` | tablebase (moved from `tablebase.lichess.ovh` in 2026-03) |
| `engine.lichess.ovh` | external engine `analyse` and `work` calls |

Requests

- User-Agent is mandatory since 2026-11-13: "All requests going to lichess.org MUST include a `User-Agent` header with your website URL, or the name of your application, or a contact email." Requests without one "may be rejected or rate limited". For RookStudio: `RookStudio/<version> (+<project URL>)`.
- Rate limits: "Only make one request at a time." On HTTP 429, "waiting one minute before retrying will be sufficient" in most cases, "but some limits may require longer". The docs do not say how this rule applies to long-lived streams (event stream, game streams, broadcast PGN streams), which are meant to stay open and are capped separately (one event stream per token, 8 game streams per IP); they ask not to poll what can be streamed. Keep the other calls serialized.
- Documented quotas worth designing around:

| Endpoint | Limit |
|---|---|
| `/api/games/user/{u}` | 20 games/s anonymous, 30 with a token, 60 for your own games |
| `/api/tournament/{id}/games`, `/api/swiss/{id}/games` | 20 games/s anonymous, 30 with a token |
| `/api/stream/game/{id}` | at most 8 open per IP |
| `/api/stream/event` | one per token; a new one closes the previous |
| `/api/stream/games-by-users` | 300 users (500 with a token, lila source) |
| `/api/stream/games/{streamId}` | 500 game IDs, 1000 with a token |
| `/api/users/status` | 100 IDs; cheap, polling every ~5 s is fine |
| `POST /api/users` | 300 IDs per call; 10,000 per 10 min and 150,000 per day with a token |
| `POST /api/games/export/_ids` | 300 IDs per call |
| `POST /api/import` | 100 games/h anonymous, 200/h with a token |
| `POST /api/study` | 30 new studies per day; 64 chapters per study |
| `POST /api/broadcast/round/{id}/push` | 100 games per request |
| `POST /api/tournament` | 12 public or 24 private per day; Swiss 12 per day |
| `POST /api/bulk-pairing` | 500 games per 10 min |
| `/api/puzzle/batch/{angle}` | 50 puzzles per call |

Streaming

- NDJSON: one JSON object per line; empty lines are keep-alives (every 7 s on the event stream). PGN streams (broadcasts) re-send the whole PGN of a game on each update.
- Ongoing games are delayed by 3 moves on the export and spectator endpoints (`/game/export/{id}`, `/api/user/{username}/current-game`, `/api/games/export/_ids`, `/api/stream/game/{id}`; 2026-01), "to prevent cheat bots from using this API".

Authentication

- OAuth 2 authorization code with PKCE (`S256` only); public clients pick any unique `client_id`; tokens last about a year and cannot be refreshed. `POST /api/token/test` reports a token's user, scopes and expiry; `DELETE /api/token` revokes it.
- In the spec, `security: OAuth2: []` means "a token is accepted": for some endpoints it is optional (faster or fuller answers), for others required. A list of several scopes means "any of them". Section 3 states which, checked against lila's controllers where the spec was ambiguous.
- The 24 scopes: `preference:read`, `preference:write`, `email:read`, `engine:read`, `engine:write`, `challenge:read`, `challenge:write`, `challenge:bulk`, `study:read`, `study:write`, `tournament:write`, `racer:write`, `puzzle:read`, `puzzle:write`, `team:read`, `team:write`, `team:lead`, `follow:read`, `follow:write`, `msg:write`, `note:write` (added 2026-09), `board:play`, `bot:play`, `web:mod` (moderators only).

Fair play, as Lichess words it

- Board API: "Engine assistance, or any kind of outside help, is forbidden."
- Correspondence (lila FAQ source, `faq.xml`): "On Lichess, the main difference in rules for correspondence chess is that an opening book is allowed. The use of engines is still prohibited and will result in being flagged for engine assistance."
- Bot API: "Engine assistance is allowed", on Bot accounts only.
- Consequence for RookStudio's lock: during a real-time game, lock every aid (engine, cloud eval, tablebase, explorer, opening tree and repertoire, assistant, "open on lichess.org"). During a correspondence game, the opening book (explorer, tree, repertoire) may stay available; engine, cloud eval, tablebase and assistant must not.

Privacy model used in section 5

- "Nothing" means only the request itself (and the token, when one is sent) leaves the machine.
- Usernames, FENs and PGNs sent to Lichess are listed per feature. Anything published (studies, broadcasts, imported games) is listed with its visibility.

## 2. The DONE integrations after the 2025-2026 changes

These are maintenance items for features already shipped. They come first because some of them can break silently.

| DONE feature | What changed | When | Action | Effort |
|---|---|---|---|---|
| Broadcasts (round followed through its PGN stream) | The round, tournament and group PGN streams, round info, tournament info and both PGN exports now require at least `study:read` (the tag says so for "all tournament endpoint requests"); anonymous calls are "heavily rate-limited and might stop functioning" (lila PR 21283). Search joined them on 2026-10-27. The official list (`/api/broadcast`, `/api/broadcast/top`), players and team standings stay open. | 2026-08-14 | Ask `study:read` the first time the user follows a round (the link asks no scope today). Keep the anonymous path only as a fallback with backoff. | S |
| Opening explorer | lila-openingexplorer v4: masters `since`/`until` are now `YYYY-MM` or `YYYY` strings (were integer years); games carry `mode` and player `title`; `recentGames` is always present; errors are `text/plain` 400s; `/player` answers 503 when its indexing queue is full. | 2026-11-15 | Check the parser and the masters date filters; treat 503 on `/player` as "retry later". | S |
| Every request | `User-Agent` is now a MUST. | 2026-11-13 | Make sure every request (explorer and tablebase hosts included) sends it. | S |
| Games sync | Game URLs became `https://lichess.org/game/{id}`; game JSON uses `arenaTour`/`swissTour` instead of `tournament`/`swiss`; games against the Lichess AI have a `GamePlayerAi` side without `user`; per-player `analysis` gained `phases`. | 2026-06 to 2026-10 | When extracting a game ID from `Site` or a URL, accept `/game/{id}`, `/{id}`, `/{id}/white|black` and 12-character player IDs (the first 8 characters are the game ID). Check the NDJSON parser on AI games. Consider `sort=dateAsc` with `since` for a resumable sync. | S |
| Cloud eval and tablebase | Tablebase: `dtc=auxiliary` is the default and gives DTC for 8-piece "op1" positions (opposing pawns); DTM extends to 6 pieces; new categories `maybe-win`/`maybe-loss`. Cloud eval unchanged. | 2025-10 to 2026-02 | Allow lookups of 8-piece positions with at least one pair of opposing pawns, and show `dtc`/`dtm` when present. | S |
| OAuth link | Tokens expire after about a year and cannot be refreshed. | (long-standing) | Read the expiry with `POST /api/token/test` and ask the user to re-link before it lapses (see N2). | S |

## 3. Endpoint inventory (190 operations, by tag)

Generated from the spec, then checked by hand. Columns:

- Auth / scope: `none`; `token (no scope)` when any token is required; "none; token ..." when a token is optional and what it adds; a scope in backticks when that scope is required; "any of" when the spec lists alternatives. Where the spec and lila's controllers disagree, the table says so.
- Format: `JSON`; `NDJSON, streamed` for a finite download sent progressively; `NDJSON live stream` / `PGN live stream` for a connection that stays open and pushes events; `(Accept)` when the `Accept` header picks PGN or JSON/NDJSON.
- New: month the operation first appeared in the spec, when in 2025 or 2026.

#### OAuth (4)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/oauth` | none | browser redirect | Request authorization code |  |
| POST | `/api/token` | PKCE `code_verifier` | JSON | Obtain access token |  |
| DELETE | `/api/token` | the token being revoked | 204 | Revoke access token |  |
| POST | `/api/token/test` | none | JSON | Test multiple OAuth tokens |  |

- PKCE with `S256` only; public, unregistered clients (any unique `client_id`, no secret). `/oauth` takes `response_type=code`, `client_id`, `redirect_uri`, `code_challenge_method=S256`, `code_challenge`, `scope` (space separated), `username` (login hint), `state`.
- Tokens are long-lived ("expect one year"), no refresh tokens; `POST /api/token` returns `expires_in`. `POST /api/token/test` (up to 1000 comma-separated tokens) returns `userId`, `scopes` and `expires` (ms, or null).
- Token format `^[A-Za-z0-9_]+$`; handle at least 512 characters. Adding scopes later means a new authorization with the full scope list, then revoking the old token.

#### Account (5)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/api/account` | token (no scope) | JSON | Get my profile |  |
| GET | `/api/account/email` | `email:read` | JSON | Get my email address |  |
| GET | `/api/account/preferences` | `preference:read` | JSON | Get my preferences |  |
| GET | `/api/account/kid` | `preference:read` | JSON | Get my kid mode status |  |
| POST | `/api/account/kid` | `preference:write` | JSON | Set my kid mode status |  |

- `/api/account`: profile, perfs, counts, play time; the natural "who am I" call after linking.
- Preferences (`preference:read`) include board and piece themes (`theme`, `pieceSet`, `is3d`, `theme3d`, `pieceSet3d`), `autoQueen`, `premove`, `confirmResign`, `submitMove`, `zen`, `ratings`, `coords`, `highlight`, `destination`, `keyboardMove`, `rookCastle`, `sayGG`. `dark` and `transp` were removed (2026-08).
- `/api/timeline` was removed from the API (2026-09).

#### Users (13)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/api/users/status` | none | JSON | Get real-time users status |  |
| GET | `/api/player` | none | JSON | Get all top 10 |  |
| GET | `/api/player/top/{nb}/{perfType}` | none | JSON (`application/vnd.lichess.v3+json`) | Get one leaderboard |  |
| GET | `/api/user/{username}` | none; token optional | JSON | Get user public data |  |
| GET | `/api/user/{username}/rating-history` | none, but only a token makes Lichess compute it (else cached or `[]`) | JSON | Get rating history of a user |  |
| GET | `/api/user/{username}/perf/{perf}` | none | JSON | Get performance statistics of a user |  |
| GET | `/api/user/{username}/activity` | none | JSON | Get user activity |  |
| POST | `/api/users` | none; token raises the quota | JSON | Get users by ID |  |
| GET | `/api/streamer/live` | none | JSON | Get live streamers |  |
| GET | `/api/crosstable/{user1}/{user2}` | none | JSON | Get crosstable |  |
| GET | `/api/player/autocomplete` | none; token optional | JSON | Autocomplete usernames |  |
| GET | `/api/user/{username}/note` | token; lila requires `note:write` (spec lists no scope) | JSON | Get notes for a user |  |
| POST | `/api/user/{username}/note` | token; lila requires `note:write` (spec lists no scope) | JSON | Add a note for a user |  |

- `/api/users/status`: up to 100 IDs; `withSignal`, `withGameIds`, `withGameMetas` (id, time control and variant of the game being played). Documented as cheap: polling every ~5 s is fine.
- `/api/user/{username}` flags: `trophies`, `profile` (default true), `rank` (2025-11), `fideId` (2026-02); with a token the answer adds `followable`, `following`, `blocking`.
- Rating history entries are `[year, month (0-based), day, rating]`, one per day; names became perf keys (`bullet`, not `Bullet`) in 2026-10; Lichess only computes the history for authenticated requests (2026-08), anonymous calls get a cache or `[]`.
- Perf statistics: glicko, rank, percentile, highest and lowest with dates, best wins, worst losses, result and play streaks, counts.
- Activity: games, puzzles, storm, racer, streak, tournaments, practice, simuls, correspondence moves and ends, follows, studies, teams, posts, stream.
- `POST /api/users`: up to 300 IDs per call; 10,000 users per 10 min and 150,000 per day when authenticated, fewer anonymously (2026-11). "Do not try to download all the Lichess users".
- Crosstable: `matchup=true` adds the current match score. Autocomplete: `term` needs 4 characters (was 3, 2026-11); `object`, `friend`, `team`, `tour`, `swiss`, `teacher`, `exists`.
- Also listed in the tag description: Atom feeds of user blogs (`/@/{user}/blog.atom`, `/blog/community.atom`).

#### Relations (5)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/api/rel/following` | `follow:read` | NDJSON, streamed | Get users followed by the logged in user |  |
| POST | `/api/rel/follow/{username}` | `follow:write` | JSON | Follow a player |  |
| POST | `/api/rel/unfollow/{username}` | `follow:write` | JSON | Unfollow a player |  |
| POST | `/api/rel/block/{username}` | `follow:write` | JSON | Block a player |  |
| POST | `/api/rel/unblock/{username}` | `follow:write` | JSON | Unblock a player |  |

- `/api/rel/following` streams the followed users as NDJSON; block/unblock share `follow:write`.

#### Games (14)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/game/export/{gameId}` | none | PGN or JSON (Accept) | Export one game |  |
| GET | `/api/game/{gameId}/chat` | none | JSON | Fetch the spectator game chat | 2026-03 |
| GET | `/api/user/{username}/current-game` | none | PGN or JSON (Accept) | Export ongoing game of a user |  |
| GET | `/api/games/user/{username}` | none; token: 30 games/s, own games 60/s (anon 20/s) | PGN or NDJSON (Accept), streamed | Export games of a user |  |
| POST | `/api/games/export/_ids` | none | PGN or NDJSON (Accept), streamed | Export games by IDs |  |
| POST | `/api/stream/games-by-users` | none (token: 500 users per lila, spec says 300) | NDJSON live stream | Stream games of users |  |
| POST | `/api/stream/games/{streamId}` | none (500 IDs; 1000 with a token) | NDJSON live stream | Stream games by IDs |  |
| POST | `/api/stream/games/{streamId}/add` | none | JSON | Add game IDs to stream |  |
| GET | `/api/account/playing` | token (no scope) | JSON | Get my ongoing games |  |
| GET | `/api/stream/game/{id}` | none | NDJSON live stream | Stream moves of a game |  |
| POST | `/api/import` | none; token: 200/h instead of 100/h, game listed in your imports | JSON | Import one game |  |
| GET | `/api/games/export/imports` | token (no scope) | PGN | Export your imported games |  |
| GET | `/api/games/export/bookmarks` | token (no scope) | PGN or NDJSON (Accept), streamed | Export your bookmarked games | 2025-01 |
| POST | `/bookmark/{gameId}` | `preference:write` | empty | Bookmark a game | 2026-07 |

- Export parameters: `moves`, `pgnInJson`, `tags`, `clocks`, `evals`, `accuracy` (JSON only), `opening`, `division` (JSON only), `literate` (text annotations of mistakes), `lastFen` (NDJSON), `withBookmarked`, `sort=dateAsc|dateDesc`; user filters `since`, `until`, `max`, `vs`, `rated`, `perfType` (comma list), `color`, `analysed`, `ongoing`, `finished`.
- The export and spectator endpoints delay ongoing games by 3 moves (2026-01).
- `games-by-users` only emits games where both players are in the list, and only start and finish events (no moves); `stream/games/{streamId}` does the same for arbitrary game IDs. Moves come from `/api/stream/game/{id}`: a description, then `{fen, lm, wc, bc}` per move, then a final description; at most 8 such streams per IP.
- `POST /api/import`: one game, returns `{id, url}` with URLs now of the form `https://lichess.org/game/{id}` (2026-10). For a position or line, an analysis-board URL is enough (see section 5).
- `/api/account/playing`: `nb` 1 to 50 (default 9); `nbMyTurn`, and per game `fen`, `isMyTurn`, `secondsLeft`, `opponent`, `rating`.
- Game JSON changes: `arenaTour`/`swissTour` replace `tournament`/`swiss` (2026-06), Lichess AI players are a separate `GamePlayerAi` shape without `user` (2026-09), per-player `analysis` has `inaccuracy`, `mistake`, `blunder`, `acpl`, `accuracy`, and `phases` since 2026-09, per-move `analysis[]` has `eval`/`mate`, `best`, `variation` and `judgment`.

#### TV (4)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/api/tv/channels` | none | JSON | Get current TV games |  |
| GET | `/api/tv/feed` | none | NDJSON live stream | Stream current TV game |  |
| GET | `/api/tv/{channel}/feed` | none | NDJSON live stream | Stream current TV game of a TV channel |  |
| GET | `/api/tv/{channel}` | none | PGN or NDJSON (Accept), streamed | Get best ongoing games of a TV channel |  |

- Channels in the schema: `best`, `bullet`, `blitz`, `rapid`, `classical`, `ultraBullet`, `bot`, `computer`, `crazyhouse`, `atomic`, `antichess`, `horde`, `kingOfTheHill`, `racingKings`, `threeCheck`.
- Feeds send a `featured` message then `fen` messages. `/api/tv/{channel}`: `nb` 1 to 30 (default 10), PGN or NDJSON.

#### Puzzles (11)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/api/puzzle/daily` | none | JSON | Get the daily puzzle |  |
| GET | `/api/puzzle/{id}` | none | JSON | Get a puzzle by its ID |  |
| GET | `/api/puzzle/next` | none; `puzzle:read` gives unseen puzzles at your rating | JSON | Get a new puzzle |  |
| GET | `/api/puzzle/batch/{angle}` | none; `puzzle:read` gives unseen puzzles at your rating | JSON | Get multiple puzzles at once | 2025-09 |
| POST | `/api/puzzle/batch/{angle}` | `puzzle:write` | JSON | Solve multiple puzzles at once | 2025-09 |
| GET | `/api/puzzle/activity` | `puzzle:read` | NDJSON, streamed | Get your puzzle activity |  |
| GET | `/api/puzzle/replay/{days}/{theme}` | `puzzle:read` | JSON | Get puzzles to replay | 2025-03 |
| GET | `/api/puzzle/dashboard/{days}` | `puzzle:read` | JSON | Get your puzzle dashboard |  |
| GET | `/api/storm/dashboard/{username}` | none | JSON | Get the storm dashboard of a player |  |
| POST | `/api/racer` | `racer:write` | JSON | Create and join a puzzle race |  |
| GET | `/api/racer/{id}` | none (lila: anonymous) | JSON | Get puzzle race results | 2025-08 |

- `angle` is a theme or an opening key (`mix` recommended); `difficulty` from `easiest` to `hardest`, relative to the user's puzzle rating (1500 when anonymous); `color` (batch: only with `nb=1`).
- Batch: `nb` 1 to 50 (default 15). Solve body: `{"solutions":[{"id","win","rated"}]}`, optional `nb` to receive the next batch in the same call, which allows solving offline and sending results later.
- Some puzzle endpoints return the initial FEN (2026-04); puzzles gained `lastMove` (2026-05); `next` returns `fen` (2026-11).
- Activity: `max`, `before`, `since` (2026-02). Dashboard: global and per-theme results over `days`. Replay: IDs of puzzles still to retry for a theme.
- Racer: the creator must start the race on lichess.org; results live 30 minutes.
- Do not enumerate puzzles through the API; use the puzzle dump.

#### Teams (16)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/api/team/{teamId}/swiss` | none | NDJSON, streamed | Get team swiss tournaments |  |
| GET | `/api/team/{teamId}` | none | JSON | Get a single team |  |
| GET | `/api/team/all` | none | JSON | Get popular teams |  |
| GET | `/api/team/of/{username}` | token; lila requires `team:read` (spec lists no scope) | JSON | Teams of a player |  |
| GET | `/api/team/search` | none | JSON | Search teams |  |
| GET | `/api/team/{teamId}/users` | none; `team:read` if the member list is private | NDJSON, streamed | Get members of a team |  |
| GET | `/api/team/{teamId}/arena` | none | NDJSON, streamed | Get team Arena tournaments |  |
| POST | `/team/{teamId}/join` | `team:write` | JSON | Join a team |  |
| POST | `/team/{teamId}/quit` | `team:write` | JSON | Leave a team |  |
| GET | `/api/team/{teamId}/requests` | `team:read` | JSON | Get join requests |  |
| POST | `/api/team/{teamId}/request/{userId}/accept` | `team:lead` | JSON | Accept join request |  |
| POST | `/api/team/{teamId}/request/{userId}/decline` | `team:lead` | JSON | Decline join request |  |
| POST | `/api/team/{teamId}/kick/{userId}` | `team:lead` | JSON | Kick a user from your team |  |
| GET | `/team/updates` | `team:read` | JSON | Get updates from your teams | 2026-08 |
| GET | `/team/updates/{teamId}` | `team:read` | JSON | Get updates from one of your teams | 2026-08 |
| POST | `/team/{teamId}/pm-all` | `team:lead` | JSON | Send a team update |  |

- `team/of/{username}` became authenticated (2026-02); lila requires `team:read`.
- Members: up to 5,000, NDJSON. Arena and Swiss lists filter by `status`, `createdBy`, `name` (2025-03).
- Join fails with 403 when the team needs a password or a message and none is given.

#### Board (13)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/api/stream/event` | any of `challenge:read`, `board:play`, `bot:play` | NDJSON live stream | Stream incoming events |  |
| POST | `/api/board/seek` | `board:play` | NDJSON live (real-time seek: keep open); JSON (correspondence) | Create a seek |  |
| GET | `/api/board/game/stream/{gameId}` | `board:play` | NDJSON live stream | Stream Board game state |  |
| POST | `/api/board/game/{gameId}/move/{move}` | `board:play` | JSON | Make a Board move |  |
| GET | `/api/board/game/{gameId}/chat` | `board:play` | JSON | Fetch the player chat |  |
| POST | `/api/board/game/{gameId}/chat` | `board:play` | JSON | Write in the chat |  |
| POST | `/api/board/game/{gameId}/abort` | `board:play` | JSON | Abort a game |  |
| POST | `/api/board/game/{gameId}/resign` | `board:play` | JSON | Resign a game |  |
| POST | `/api/board/game/{gameId}/draw/{accept}` | `board:play` | JSON | Handle draw offers |  |
| POST | `/api/board/game/{gameId}/takeback/{accept}` | `board:play` | JSON | Handle takeback offers |  |
| POST | `/api/board/game/{gameId}/claim-victory` | `board:play` | JSON | Claim victory of a game |  |
| POST | `/api/board/game/{gameId}/claim-draw` | `board:play` | JSON | Claim draw of a game | 2025-07 |
| POST | `/api/board/game/{gameId}/berserk` | `board:play` | JSON | Berserk a tournament game |  |

- Normal accounts. "Engine assistance, or any kind of outside help, is forbidden." Time controls: Rapid, Classical and Correspondence; Blitz only for direct challenges, games against the AI and bulk pairings.
- Event stream: one per token (opening one closes the previous one with the same token); an empty line every 7 s; types `gameStart`, `gameFinish` (with `ratingDiff`), `challenge`, `challengeCanceled`, `challengeDeclined`. All current games and challenges are sent on open.
- Game stream: `gameFull` first, then `gameState` (with `expiration`, 2025-12), `chatLine`, `opponentGone`; closed by the server at the end.
- A real-time seek lives only while its connection is open; open the event stream first. Moves are UCI, with `offeringDraw=true` to offer a draw.

#### Bot (12)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/api/bot/online` | none | NDJSON, streamed | Get online bots |  |
| POST | `/api/bot/account/upgrade` | `bot:play` | JSON | Upgrade to Bot account |  |
| GET | `/api/bot/game/stream/{gameId}` | `bot:play` | NDJSON live stream | Stream Bot game state |  |
| POST | `/api/bot/game/{gameId}/move/{move}` | `bot:play` | JSON | Make a Bot move |  |
| GET | `/api/bot/game/{gameId}/chat` | `bot:play` | JSON | Fetch the game chat |  |
| POST | `/api/bot/game/{gameId}/chat` | `bot:play` | JSON | Write in the chat |  |
| POST | `/api/bot/game/{gameId}/abort` | `bot:play` | JSON | Abort a game |  |
| POST | `/api/bot/game/{gameId}/resign` | `bot:play` | JSON | Resign a game |  |
| POST | `/api/bot/game/{gameId}/draw/{accept}` | `bot:play` | JSON | Handle draw offers |  |
| POST | `/api/bot/game/{gameId}/takeback/{accept}` | `bot:play` | JSON | Handle takeback offers |  |
| POST | `/api/bot/game/{gameId}/claim-victory` | `bot:play` | JSON | Claim victory of a game | 2025-04 |
| POST | `/api/bot/game/{gameId}/claim-draw` | `bot:play` | JSON | Claim draw of a game | 2025-07 |

- Bot accounts only. The upgrade is irreversible and only possible for an account that has never played a game.
- Challenge games only ("pools and tournaments are off-limits"), except Arenas created with `conditions.bots=true`. No UltraBullet; 0+1 and ½+0 are allowed. Terms of service apply (no sandbagging, boosting, constant aborting; play casual while testing).
- `/api/bot/online`: `nb` default 100, maximum 512 (2026-04).

#### Challenges (11)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/api/challenge` | `challenge:read` | JSON | List your challenges |  |
| POST | `/api/challenge/{username}` | any of `challenge:write`, `bot:play`, `board:play` | JSON; NDJSON live with `keepAliveStream` | Create a challenge |  |
| GET | `/api/challenge/{challengeId}/show` | `challenge:read` | JSON | Show one challenge |  |
| POST | `/api/challenge/{challengeId}/accept` | any of `challenge:write`, `bot:play`, `board:play` | JSON | Accept a challenge |  |
| POST | `/api/challenge/{challengeId}/decline` | any of `challenge:write`, `bot:play`, `board:play` | JSON | Decline a challenge |  |
| POST | `/api/challenge/{challengeId}/cancel` | any of `challenge:write`, `bot:play`, `board:play` | JSON | Cancel a challenge |  |
| POST | `/api/challenge/ai` | any of `challenge:write`, `bot:play`, `board:play` | JSON | Challenge the AI |  |
| POST | `/api/challenge/open` | none; a token lets you cancel it later | JSON | Open-ended challenge |  |
| POST | `/api/challenge/{gameId}/start-clocks` | `challenge:write` (both players' tokens) | JSON | Start clocks of a game |  |
| POST | `/api/round/{gameId}/add-time/{seconds}` | `challenge:write` | JSON | Add time to the opponent clock |  |
| POST | `/api/token/admin-challenge` | `web:mod` (Lichess admins only) | JSON | Admin challenge tokens |  |

- Clock: `clock.limit` 0, 15, 30, 45, 60, 90 or a multiple of 60 up to 10800, `clock.increment` 0 to 60; or `days` (1, 2, 3, 5, 7, 10, 14); or unlimited. `rated`, `color`, `variant`, `fen` (games from a position cannot be rated), `rules` (`noAbort`, `noRematch`, `noGiveTime`, `noClaimWin`, `noEarlyDraw`).
- Real-time challenges expire after 20 s unless `keepAliveStream` keeps the request open.
- AI: `level` 1 to 8. Open challenge: whoever opens the URL plays; `users` restricts it to a pair (first gets White), `name`, `expiresAt` (default 24 h, at most 2 weeks), `urlWhite`/`urlBlack`.
- Accept takes `color` (2025-08); decline takes a `reason`.

#### Bulk pairings (6)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/api/bulk-pairing` | `challenge:bulk` | JSON | View your bulk pairings |  |
| POST | `/api/bulk-pairing` | `challenge:bulk` + every player's `challenge:write` token | JSON | Create a bulk pairing |  |
| POST | `/api/bulk-pairing/{id}/start-clocks` | `challenge:bulk` | JSON | Manually start clocks |  |
| GET | `/api/bulk-pairing/{id}` | `challenge:bulk` | JSON | Show a bulk pairing |  |
| DELETE | `/api/bulk-pairing/{id}` | `challenge:bulk` | JSON | Cancel a bulk pairing |  |
| GET | `/api/bulk-pairing/{id}/games` | `challenge:bulk` | PGN or NDJSON (Accept), streamed | Export games of a bulk pairing |  |

- For organisers: up to 500 games per 10 minutes, scheduled up to 24 h ahead, all or nothing; every paired player's `challenge:write` token is required.

#### Tournaments (Arena) (13)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/api/tournament` | none | JSON | Get current tournaments |  |
| POST | `/api/tournament` | `tournament:write` | JSON | Create a new Arena tournament |  |
| GET | `/api/tournament/{id}` | none | JSON | Get info about an Arena tournament |  |
| POST | `/api/tournament/{id}` | `tournament:write` | JSON | Update an Arena tournament |  |
| POST | `/api/tournament/{id}/join` | `tournament:write` | JSON | Join an Arena tournament |  |
| POST | `/api/tournament/{id}/withdraw` | `tournament:write` | JSON | Pause or leave an Arena tournament |  |
| POST | `/api/tournament/{id}/terminate` | `tournament:write` | JSON | Terminate an Arena tournament |  |
| POST | `/api/tournament/team-battle/{id}` | `tournament:write` | JSON | Update a team battle |  |
| GET | `/api/tournament/{id}/games` | none; token: 30 games/s (anon 20/s) | PGN or NDJSON (Accept), streamed | Export games of an Arena tournament |  |
| GET | `/api/tournament/{id}/results` | none | NDJSON, streamed | Get results of an Arena tournament |  |
| GET | `/api/tournament/{id}/teams` | none | JSON | Get team standing of a team battle |  |
| GET | `/api/user/{username}/tournament/created` | none; token raises throughput | NDJSON, streamed | Get tournaments created by a user |  |
| GET | `/api/user/{username}/tournament/played` | none; token raises throughput | NDJSON, streamed | Get tournaments played by a user |  |

- Creation: 12 public or 24 private per day, with a clock-to-length constraint. Conditions: `teamMember`, `minRating`, `maxRating`, `nbRatedGame`, `allowList`, `bots`, `accountAge`. Team battles via `teamBattleByTeam`.
- Results stream as NDJSON (`sheet` for score sheets); results of a running tournament may be inconsistent.

#### Tournaments (Swiss) (10)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| POST | `/api/swiss/new/{teamId}` | `tournament:write` | JSON | Create a new Swiss tournament |  |
| GET | `/api/swiss/{id}` | none | JSON | Get info about a Swiss tournament |  |
| POST | `/api/swiss/{id}/edit` | `tournament:write` | JSON | Update a Swiss tournament |  |
| POST | `/api/swiss/{id}/schedule-next-round` | `tournament:write` | 204 | Manually schedule the next round |  |
| POST | `/api/swiss/{id}/join` | `tournament:write` | JSON | Join a Swiss tournament |  |
| POST | `/api/swiss/{id}/withdraw` | `tournament:write` | JSON | Pause or leave a swiss tournament |  |
| POST | `/api/swiss/{id}/terminate` | `tournament:write` | JSON | Terminate a Swiss tournament |  |
| GET | `/swiss/{id}.trf` | none | text (FIDE TRF) | Export TRF of a Swiss tournament |  |
| GET | `/api/swiss/{id}/games` | none; token: 30 games/s (anon 20/s) | PGN or NDJSON (Accept), streamed | Export games of a Swiss tournament |  |
| GET | `/api/swiss/{id}/results` | none | NDJSON, streamed | Get results of a swiss tournament |  |

- Created for a team (12 per day); `forbiddenPairings`, `manualPairings`; next round can be scheduled by hand.
- `/swiss/{id}.trf` exports the FIDE Tournament Report File.

#### Simuls (1)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/api/simul` | none; token adds your pending simuls | JSON | Get current simuls |  |

- Lists only (created, started, finished; filtered to strong hosts).

#### Studies (10)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/api/study/{studyId}/{chapterId}.pgn` | none for public; `study:read` for unlisted/private | PGN | Export one study chapter |  |
| GET | `/api/study/{studyId}.pgn` | none for public; `study:read` for unlisted/private | PGN | Export all chapters |  |
| HEAD | `/api/study/{studyId}.pgn` | none | headers only (`Last-Modified`) | Study metadata |  |
| POST | `/api/study` | `study:write` | JSON | Create a new Study | 2026-03 |
| POST | `/api/study/{studyId}/import-pgn` | `study:write` | JSON | Import PGN into a study |  |
| POST | `/api/study/{studyId}/{chapterId}/tags` | `study:write` | 204 | Update PGN tags of a study chapter | 2025-09 |
| POST | `/api/study/{studyId}/{chapterId}/moves` | `study:write` | 204 | Update the moves of a study chapter | 2026-03 |
| GET | `/api/study/by/{username}/export.pgn` | none for public; `study:read` adds unlisted/private | PGN | Export all studies of a user | moved 2026-01 |
| GET | `/api/study/by/{username}` | none for public; `study:read` adds unlisted/private | NDJSON, streamed | List studies of a user |  |
| DELETE | `/api/study/{studyId}/{chapterId}` | `study:write` | 204 | Delete a study chapter |  |

- Create (2026-03): `name` 2 to 100 characters, `visibility` (`public`, `unlisted`, `private`; the schema says the default is `unlisted` while its description says `public`, so always send it), `computer`, `explorer`, `cloneable`, `shareable`, `chat` (each `nobody`, `owner`, `contributor`, `member`, `everyone`; required), `sticky`, `description`, `flair`. 30 new studies per day.
- Import PGN: several games make several chapters; at most 64 chapters per study; `name`, `orientation`, `variant`, `mode` (`practice`, `conceal`, `gamebook`; 2026-04).
- Chapter tags (2025-09) and moves (2026-03) can be replaced in place; both answer 204.
- Export flags `clocks`, `comments`, `variations`, `orientation` (adds an `[Orientation]` tag); `Last-Modified` header, and `HEAD` for change detection.
- Comment commands parsed on import (lila source, `modules/study/.../CommentParser.scala`): arrows `[%cal Ge2e4,Rd7d5]`, circles `[%csl Gd4]` (G, R, Y, B), `[%clk]`, `[%emt]`, TCEC `tl=` clocks, and an undocumented `[%anno "Name"]` comment author.

#### Messaging (1)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| POST | `/inbox/{username}` | `msg:write` | JSON | Send a private message |  |

- One endpoint: send a private message.

#### Broadcasts (21)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/api/broadcast` | none | NDJSON, streamed | Get official broadcasts |  |
| GET | `/api/broadcast/top` | none | JSON | Get paginated top broadcast previews |  |
| GET | `/api/broadcast/by/{username}` | `study:read` | JSON | Get broadcasts created by a user |  |
| GET | `/api/broadcast/search` | `study:read` | JSON | Search broadcasts | 2025-05 |
| POST | `/broadcast/new` | `study:write` | JSON | Create a broadcast tournament |  |
| GET | `/api/broadcast/{broadcastTournamentId}` | `study:read` | JSON | Get a broadcast tournament |  |
| GET | `/broadcast/{broadcastTournamentId}/players` | none | JSON | Get players of a broadcast |  |
| GET | `/broadcast/{broadcastTournamentId}/players/{playerId}` | none | JSON | Get a player of a broadcast | 2025-09 |
| GET | `/broadcast/{broadcastTournamentId}/teams/standings` | none | JSON | Get the team leaderboard of a broadcast | 2026-02 |
| POST | `/broadcast/{broadcastTournamentId}/edit` | `study:write` | JSON | Update your broadcast tournament |  |
| POST | `/broadcast/{broadcastTournamentId}/new` | `study:write` | JSON | Create a broadcast round |  |
| GET | `/api/broadcast/{broadcastTournamentSlug}/{broadcastRoundSlug}/{broadcastRoundId}` | `study:read` | JSON | Get a broadcast round |  |
| POST | `/broadcast/round/{broadcastRoundId}/edit` | `study:write` | JSON | Update a broadcast round |  |
| POST | `/api/broadcast/round/{broadcastRoundId}/reset` | `study:write` | JSON | Reset a broadcast round |  |
| POST | `/api/broadcast/round/{broadcastRoundId}/push` | `study:write` | JSON | Push PGN to a broadcast round |  |
| GET | `/api/stream/broadcast/round/{broadcastRoundId}.pgn` | `study:read` | PGN live stream | Stream an ongoing broadcast round as PGN |  |
| GET | `/api/stream/broadcast/tour/{broadcastTourId}.pgn` | `study:read` | PGN live stream | Stream ongoing broadcast rounds of a tournament as PGN | 2026-08 |
| GET | `/api/stream/broadcast/group/{broadcastGroupId}.pgn` | `study:read` | PGN live stream | Stream ongoing broadcast rounds of a group as PGN | 2026-06 |
| GET | `/api/broadcast/round/{broadcastRoundId}.pgn` | `study:read` | PGN | Export one round as PGN |  |
| GET | `/api/broadcast/{broadcastTournamentId}.pgn` | `study:read` | PGN | Export all rounds as PGN |  |
| GET | `/api/broadcast/my-rounds` | `study:read` | NDJSON, streamed | Get your broadcast rounds |  |

- Since 2026-08 every tournament and round endpoint needs at least a `study:read` token; anonymous calls are "heavily rate-limited and might stop functioning" (lila PR 21283). Search joined them in 2026-10. The official lists, players and team standings stay open.
- Official list: `nb` up to 100, `live=true` (2026-04), `html`. `top` is paginated; its `upcoming` field is deprecated and always empty.
- Players: FIDE IDs, scores, `ratingsMap`, `ratingDiffs` and `performances` by FIDE time control (the single `ratingDiff`/`performance` were removed in 2026-04), tiebreaks, rank; player detail with games; team standings (2026-02).
- PGN streams for a round, a tournament (2026-08) or a group (2026-06), with `clocks` and `comments` flags; the whole PGN of a game is re-sent on each update.
- Own broadcasts: tournament form (`name`, `info`, `markdown`, `visibility`, `showScores`, `showRatingDiffs`, `teamTable`, `players` replacements, `teams`, `tiebreaks`, `grouping`); round form (`syncSource` = `push`, `url`, `urls`, `ids` or `users`; `syncUrl` must be reachable from the Internet; `syncIds`/`syncUsers` up to 100; `startsAt` or `startsAfterPrevious`; `delay` up to 3600 s; `status`; `rated`; `customScoring`). Round edit accepts `?patch=true` for a partial update (2025-11); the tournament edit does not, so send every field. Push takes up to 100 games per request and answers with tags, move count or error per game; it works only for rounds without a source URL.

#### Analysis (1)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/api/cloud-eval` | none | JSON | Get cloud evaluation of a position |  |

- `fen` (required), `multiPv`, `variant`; 404 when the position is not cached. Lila serves it with a one-day public cache. "Use this endpoint to fetch a few positions here and there"; for volume, use the evaluations dump.

#### External engine (8)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/api/external-engine` | `engine:read` | JSON | List external engines |  |
| POST | `/api/external-engine` | `engine:write` | JSON | Create external engine |  |
| GET | `/api/external-engine/{id}` | `engine:read` | JSON | Get external engine |  |
| PUT | `/api/external-engine/{id}` | `engine:write` | JSON | Update external engine |  |
| DELETE | `/api/external-engine/{id}` | `engine:write` | JSON | Delete external engine |  |
| POST | `/api/external-engine/{id}/analyse` (engine.lichess.ovh) | `clientSecret` in body (no OAuth) | NDJSON live stream | Analyse with external engine |  |
| POST | `/api/external-engine/work` (engine.lichess.ovh) | `providerSecret` in body (no OAuth) | JSON, long polling (204 = no work) | Acquire analysis request |  |
| POST | `/api/external-engine/work/{id}` (engine.lichess.ovh) | work id from the acquire call | request body = streamed UCI text | Answer analysis request |  |

- Alpha, "subject to change". Registration: `name`, `maxThreads`, `maxHash`, `variants` (`chess`, `crazyhouse`, `antichess`, `atomic`, `horde`, `kingofthehill`, `racingkings`, `3check`), `providerSecret` (16 to 1024 characters, never readable again), `providerData`.
- Client side: `POST .../{id}/analyse` with `clientSecret` and a work item (`sessionId`, `threads`, `hash`, `multiPv` 1 to 5, `variant`, `initialFen`, `moves`, and one of `movetime`, `depth`, `nodes`) streams `{time, depth, nodes, pvs[{depth, cp|mate, moves}]}`.
- Provider side: long-poll `POST /work` with the `providerSecret` (204 means nothing to do), then stream UCI `info` lines (with `depth`, `multipv`, `score`, `nodes`, `time`, `pv`) and a final `bestmove` to `/work/{id}`; the engine runs in `UCI_Chess960` mode, `UCI_AnalyseMode` if available; stop when the connection closes. Reference provider: github.com/lichess-org/external-engine ("alpha 2", main branch active in 2026-09).

#### Opening Explorer (4)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/masters` (explorer.lichess.org) | token (no scope), required since 2026 | JSON | Masters database |  |
| GET | `/lichess` (explorer.lichess.org) | token (no scope), required since 2026 | JSON | Lichess games |  |
| GET | `/player` (explorer.lichess.org) | token (no scope), required since 2026 | NDJSON live stream | Player games |  |
| GET | `/masters/pgn/{gameId}` (explorer.lichess.org) | token (no scope), required since 2026 | PGN | OTB master game |  |

- Any token is required since 2026 (Lichess blog "The opening explorer now requires authentication"). Version 4 of lila-openingexplorer is documented since 2026-11-15.
- Masters: `fen` or `play`, `since`/`until` as `YYYY-MM` or `YYYY` (were integer years), `moves`, `topGames`. Lichess: `variant`, `speeds`, `ratings` (groups 0, 1000, ... 2500), `since`/`until`, `moves`, `topGames` (up to 4), `recentGames` (up to 4 or 8), `history` (monthly history of the position). Player: `player`, `color`, `modes`, `speeds`, `since`/`until`, `recentGames`; streams updates while indexing (new games indexed at most once a minute), 503 when the indexing queue is full.
- Errors are now `text/plain` 400s; games carry `mode` and player `title`.

#### Tablebase (3)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/standard` (tablebase.lichess.org) | none | JSON | Tablebase lookup |  |
| GET | `/atomic` (tablebase.lichess.org) | none | JSON | Tablebase lookup for Atomic chess |  |
| GET | `/antichess` (tablebase.lichess.org) | none | JSON | Tablebase lookup for Antichess |  |

- `fen` as X-FEN (`_` allowed for spaces). `dtc` = `never`, `auxiliary` (default since 2026-02) or `always`: DTC for 8-piece "op1" positions (more than one pawn of material per side and at least one pair of opposing pawns). DTZ up to 7 pieces (variants 6), DTM up to 6 pieces (2026-02), DTW for Antichess up to 4 pieces.
- Categories: `win`, `unknown`, `syzygy-win`, `maybe-win`, `cursed-win`, `draw`, `blessed-loss`, `maybe-loss`, `syzygy-loss`, `loss`; moves listed best first.

#### FIDE (3)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/api/fide/player/{playerId}` | none | JSON | Get a FIDE player | 2025-02 |
| GET | `/api/fide/player/{playerId}/ratings` | none | JSON | Get ratings history of a FIDE player | 2026-02 |
| GET | `/api/fide/player` | none | JSON | Search FIDE players | 2025-02 |

- Player: `id`, `name`, `title`, `federation`, `year` (birth), `inactive`, `standard`, `rapid`, `blitz`, `gender` (2026-04), `photo` (2025-12); rating history (2026-02); search by `q`. Data comes from FIDE's public download lists.

#### Server (1)

| Method | Path | Auth / scope | Format | What it does | New |
|---|---|---|---|---|---|
| GET | `/api/version` | none | JSON | Get server version | 2026-10 |

- `/api/version`: date and git commit of the deployed lila (documented 2026-10).

## 4. Data services: database.lichess.org and related open data

Licence statement on the page: "Database exports are released under the Creative Commons CC0 license. Use them for research, commercial purpose, publication, anything you like." One exception: "Broadcast games are released under the Creative Commons Attribution-ShareAlike 4.0 license."

| Dataset | File | Size (2026-11-18) | Content | Licence |
|---|---|---|---|---|
| Standard rated games | `standard/lichess_db_standard_rated_YYYY-MM.pgn.zst`, one month per file, not cumulative, `.torrent` per file, `list.txt`, `sha256sums.txt` | 166 files, 2.63 TB compressed (about ×7.1 uncompressed), 8,315,708,510 games, 2013-01 to 2026-10; 2026-10 alone is 31 GB and 95,395,628 games | About 6% of games carry `[%eval]` (always from White's side); `[%clk]` to the second since 2017-04; `WhiteElo`/`BlackElo` are Glicko-2; bots have `BOT` in `WhiteTitle`/`BlackTitle` | CC0 |
| Variant rated games | `antichess/`, `atomic/`, `chess960/`, `crazyhouse/`, `horde/`, `kingOfTheHill/`, `racingKings/`, `threeCheck/`, same naming | Antichess 38.3 M games, Atomic 29.8 M, Chess960 30.2 M, Crazyhouse 31.0 M, Horde 7.5 M, King of the Hill 8.7 M, Racing Kings 6.7 M, Three-check 11.1 M | `[Variant "..."]` tag | CC0 |
| Puzzles | `lichess_db_puzzle.csv.zst` | 6,193,530 puzzles (updated 2026-11-11) | Columns `PuzzleId, FEN, Moves, Rating, RatingDeviation, Popularity, NbPlays, Themes, GameUrl, OpeningTags, DailyDate`; FEN is the position before the opponent's move, moves in UCI; `OpeningTags` only for puzzles starting before move 20 | CC0 (the API docs also call the puzzles "public domain") |
| Evaluations | `lichess_db_eval.jsonl.zst` | 417,862,375 positions (updated 2026-11-11) | One position per line: `fen` (pieces, side, castling, en passant only), `evals[]` with `knodes`, `depth`, `pvs[]` (`cp` or `mate`, `line` in UCI_Chess960); recommended pick: highest depth, first PV | CC0 |
| Broadcasts | `broadcast/lichess_db_broadcast_YYYY-MM.pgn.zst` | 82 files, 767 MB, 1,299,343 games, 2020-01 to 2026-10 | Official broadcasts; almost all games have Stockfish evals; `%clk`; rich tags such as `BroadcastName`, `GameURL` | CC BY-SA 4.0 (attribution, share-alike) |
| Opening names | github.com/lichess-org/chess-openings, `a.tsv` to `e.tsv` (`eco`, `name`, `pgn`); the built `dist/` adds `uci` and `epd`; also on Hugging Face as Parquet | | Names like `Sicilian Defense: Najdorf Variation, English Attack`; suggested classification: walk the game backwards until a named position is found | CC0 ("As a collection of facts, this data set is in the public domain") |
| Centisecond clocks | a separate export linked from the page (link not checked) | 2013 to 2021 | `%clkc` comments | not stated |

Notes for an importer

- Decompression: `pzstd -d` (faster than `unzstd`), PeaZip on Windows, or stream with `zstdcat`; "partial downloads can be decompressed", so an importer can stop early.
- The page warns that SCID and ChessBase may fail on these files; it suggests pgnsplit, python-chess or Scoutfish.
- Known issues to filter or flag on import: 2023-11 some Chess960 rematches have invalid castling rights in their starting FEN; 2022-12 some Antichess games carry bullet ratings; 2021-03-12 some results are wrong after a datacenter fire; 2021-02-09 some games were resigned after they ended; 2020-12 and 2021-01 many variant games were analysed with standard NNUE (wrong evals); up to 2020-12 some exports lack `Round`, `Date` and black move numbers after comments; 2020-07 and 2020-08 opening evals (first 15 plies) may be wrong; 2016-12 many evals may be wrong; before 2016 mate distances may be off; 2020-06 and before 2016-03 some players could play themselves rated; up to 2016-08 seven games with illegal castling.
- The API docs send bulk users here: cloud eval says "If you want to download a lot of positions, get the full list"; puzzles say "DO NOT use this endpoint to enumerate puzzles for mass download".
- The cloud-eval endpoint description says "about 320 million positions"; the dump has 417.9 million. The page number is the newer one.

## 5. Integration proposals

Each item gives: space and page, what the user gets, endpoints and scopes, fair play, privacy (what leaves the machine), effort (S, M, L), value (high, medium, low), status (DONE, PLANNED, NEW). Every endpoint named here is in the spec (section 3) unless it is marked as a lila web route or a data file.

### 5.1 NEW proposals, ranked by value, then effort

| Rank | ID | Proposal | Space, page | Value | Effort | Scopes |
|---|---|---|---|---|---|---|
| 1 | N1 | Keep the DONE integrations working after the 2026 changes (section 2) | all | high | S | `study:read` |
| 2 | N2 | Scope and token manager | Settings, Lichess account | high | S | asks the others |
| 3 | N3 | Opening names offline from lichess-org/chess-openings | Chess: database, game view, tree | high | S | none, no network |
| 4 | N4 | Rating and performance dashboard | Chess: home | high | S | token |
| 5 | N5 | Import anything from a Lichess link | Chess: database import | high | S-M | none, token, `study:read` |
| 6 | N6 | Fair-play guard for games played anywhere | app-wide | high | M | `challenge:read` (or token only) |
| 7 | N7 | Finished-game pipeline with Lichess's own analysis | Chess: home, review | high | M | `challenge:read` (or none) |
| 8 | N8 | Follow chosen players live | Chess: Live mosaic | high | M | none (`follow:read` optional) |
| 9 | N9 | Engine tournament live on your own Lichess broadcast | Engines: tournament | high | M | `study:write` |
| 10 | N10 | Opponent preparation from Lichess games | Chess: database, tree | high | M | token |
| 11 | N11 | Engine opening suites from Lichess open data | Engines: opening suites | high | M | none |
| 12 | N12 | Open on lichess.org, share a game | Chess: board, database | medium | S | none (token optional) |
| 13 | N13 | Correspondence "your move" panel | Chess: home | medium | S | token |
| 14 | N14 | Bookmarks both ways | Chess: database | medium | S | token, `preference:write` |
| 15 | N15 | Broadcast extras: standings, whole events, live filter, search | Chess: broadcasts | medium | S | none, `study:read` |
| 16 | N16 | Explorer and tablebase extras: trends, master games, 8-piece DTC | Chess: tree, analysis | medium | S | token, none |
| 17 | N17 | Play out a position on Lichess | Chess: board | medium | S | none |
| 18 | N18 | FIDE player cards | Chess: database, broadcasts | medium | M | none |
| 19 | N19 | Offline evaluation cache from the evaluations dump | Chess: analysis | medium | L | none |
| 20 | N20 | Lichess preferences and kid mode for online play | Settings, online play | low | S | `preference:read` |
| 21 | N21 | Player notes sync | Chess: database player view | low | S | `note:write` |

### 5.2 NEW proposals in detail

N1. Keep the DONE integrations working. See section 2: `study:read` for broadcasts, explorer v4, User-Agent, game URL and game JSON changes, 8-piece tablebase, token expiry.

N2. Scope and token manager. NEW.
- Space: Settings, Lichess account.
- User gets: the list of Lichess features with the scope each needs, granted or not, and the token's expiry; consent asked feature by feature at first use; re-link before expiry; unlink revokes.
- Endpoints: `GET /oauth` with `scope` = granted plus newly needed scopes and `username=<linked user>` as a hint; `POST /api/token`; `POST /api/token/test` (returns `scopes` and `expires`); `DELETE /api/token` on the old token once the new one works; `GET /api/account` to confirm it is the same user.
- Fair play: none. Privacy: the token only. Effort S. Value high: every later feature asks through it.

N3. Opening names offline. NEW.
- Space: Chess, database (ECO and Opening columns and filters), one-game view (the opening-name request from TalkChess, fastchess-gui #182), opening tree (name of the node), review and assistant prompts.
- Data: lichess-org/chess-openings `dist/` (EPD, name, ECO), CC0; classify by walking each game backwards to the last named position, as the dataset recommends. Lichess's own exports (`opening=true`) use the same names, so synced and local games agree.
- Fair play: none. Privacy: nothing (bundled data). Effort S. Value high.

N4. Rating and performance dashboard. NEW.
- Space: Chess, home page.
- User gets: rating curves per perf, rank and percentile, highest and lowest ratings with dates, best wins and worst losses (opened from the local copy), streaks, the week's activity.
- Endpoints: `GET /api/user/{me}/rating-history` with the token (anonymous calls get a cache or `[]`); `GET /api/user/{me}/perf/{perf}` for each perf played; `GET /api/account`; `GET /api/user/{me}/activity`; optionally `GET /api/user/{me}?rank=true&fideId=true`.
- Fair play: none. Privacy: the linked username. Effort S, charts aside. Value high. Perf names are keys (`bullet`) since 2026-10.

N5. Import anything from a Lichess link. NEW.
- Space: Chess, database import dialog; paste on the board.
- User gets: paste one or many lichess.org links and get the games.
- Link forms (lila routes) and endpoints:
  - Game `/game/{id}`, `/{id}`, `/{id}/white|black`, 12-character player links: `POST /api/games/export/_ids` (300 per call) or `GET /game/export/{id}`; none.
  - Study `/study/{id}`, `/study/{id}/{chapterId}`: `GET /api/study/{id}.pgn`, `GET /api/study/{id}/{chapterId}.pgn`; none for public, `study:read` for unlisted or private.
  - Broadcast `/broadcast/{tourSlug}/{tourId}`, `/broadcast/{tourSlug}/{roundSlug}/{roundId}`: `GET /api/broadcast/{tourId}.pgn`, `GET /api/broadcast/round/{roundId}.pgn`; `study:read`.
  - Arena `/tournament/{id}`: `GET /api/tournament/{id}/games`; Swiss `/swiss/{id}`: `GET /api/swiss/{id}/games` and `/swiss/{id}.trf`; none, faster with a token.
  - Player `/@/{username}`: `GET /api/games/user/{username}` with filters; none, faster with a token.
  - Master game from the explorer: `GET /masters/pgn/{id}` on explorer.lichess.org; token.
- Fair play: refuse the linked user's ongoing games. Privacy: the pasted IDs. Effort S-M. Value high.

N6. Fair-play guard for games played anywhere. NEW (complements the PLANNED lock, which covers games played inside RookStudio).
- Space: app-wide banner and lock; Settings, Lichess.
- User gets: when the linked account has a game in progress, in RookStudio, a browser or the phone, RookStudio locks its aids and says why, so analysing in RookStudio while playing cannot get the account flagged by accident.
- Endpoints: `GET /api/stream/event` (any of `challenge:read`, `board:play`, `bot:play`; lila checks exactly these three): all current games on open, then `gameStart` and `gameFinish`. Without that scope: poll `GET /api/account/playing` (token) or `GET /api/users/status?ids={me}&withGameMetas=true` (none, documented as cheap).
- Lock rule, from Lichess's own words: real-time game, all aids off; correspondence game, engine, cloud eval, tablebase and assistant off for the positions of that game (FEN from `/api/account/playing`), opening book allowed.
- One event stream per token: P5 must reuse this one.
- Privacy: the token only. Effort M (the lock touches every aid). Value high.

N7. Finished-game pipeline with Lichess's own analysis. NEW.
- Space: Chess, home notification and game review.
- User gets: a Lichess game ends; RookStudio imports it, runs the local review and the assistant's comments, and says "review ready"; when the game was analysed on Lichess, Lichess's accuracy, mistakes and per-phase accuracy show next to the local review.
- Endpoints: `gameFinish` from `GET /api/stream/event` (`challenge:read`), or the existing sync with `since` when the scope is not granted; `GET /game/export/{id}` with `Accept: application/json` and `evals`, `accuracy`, `division`, `opening`, `clocks`, `literate` (none).
- Limit: asking Lichess to analyse a game is not in the API (only a web route), so Lichess's numbers exist only when the user requested them on the site.
- Fair play: finished games only. Privacy: nothing. Effort M. Value high.

N8. Follow chosen players live. NEW.
- Space: Chess, Live mosaic (same tiles as broadcasts).
- User gets: tiles for the games of chosen players (friends, club mates, streamers, top players) analysed by local engines; finished games saved to a database.
- Endpoints:
  - Choosing players: `GET /api/rel/following` (`follow:read`), `GET /api/player/autocomplete?term=&object=true` (none; 4 characters minimum), `GET /api/streamer/live` (none), `GET /api/player` (none).
  - Finding their games: `GET /api/users/status?ids=...&withGameMetas=true` (none; 100 IDs; polling every ~5 s is fine).
  - Moves: `GET /api/stream/game/{id}` (none; 8 at a time per IP; 3 moves behind).
  - Club games: `POST /api/stream/games-by-users?withCurrentGames=true` (none; only games where both players are listed; start and finish only).
  - Context and archive: `GET /api/crosstable/{a}/{b}?matchup=true`; `GET /game/export/{id}` at the end.
- `games-by-users` cannot follow one player against anyone; status polling plus game streams can.
- Fair play: watching others is allowed (Lichess delays the moves); never a tile for the user's own ongoing game (N6). Privacy: the followed usernames. Effort M. Value high.

N9. Engine tournament live on your own Lichess broadcast. NEW.
- Space: Engines, tournament page, "Broadcast on Lichess".
- User gets: a running fastchess tournament on lichess.org, public, unlisted or private, with clocks and scores, without hosting anything.
- Endpoints (`study:write`):
  1. `POST /broadcast/new`: `name`, `markdown` (engines, hardware, time control, opening suite), `visibility`, `showScores=true`, `tiebreaks`, optional `players` lines to fix names.
  2. `POST /broadcast/{tourId}/new` per round: `name`, `syncSource=push`, `startsAt` or `startsAfterPrevious`, `rated=false`.
  3. `POST /api/broadcast/round/{roundId}/push`: text/plain PGN of the round's changed games, up to 100 per request; the answer gives tags, move count or error per game. Clocks as `[%clk h:mm:ss]`, which lila parses; engine evaluations and depths as plain comments.
  4. `POST /broadcast/round/{roundId}/edit?patch=true` with `status=finished`; `POST /api/broadcast/round/{roundId}/reset` to start over; `GET /api/broadcast/my-rounds` (`study:read`) to resume after a restart.
- Constraints: one request at a time, so gather moves into one push every few seconds; pushing works only for rounds without a source URL (`syncUrl` would need a public server).
- Fair play: not concerned. Privacy: the tournament's PGN and description become visible as chosen. Effort M. Value high.

N10. Opponent preparation from Lichess games. NEW.
- Space: Chess, database ("Prepare against...") and opening tree.
- User gets: the opponent's repertoire from their Lichess games, per colour and speed, against the user's repertoire: where they leave it, how they score, which lines they never meet.
- Endpoints: `GET /api/games/user/{opponent}` with `perfType`, `color`, `rated=true`, `max`, `opening=true` (none; 30 games/s with a token) into a separate database; `GET /player?player={opponent}&color=` on explorer.lichess.org (token; streams while it indexes) for a quick look; `GET /api/crosstable/{me}/{opponent}`; `GET /api/user/{opponent}` and `GET /api/user/{opponent}/perf/{perf}`.
- Fair play: preparation before a game is fine; while a game against that opponent is in progress, lock the report (N6); in correspondence, the opening-book part may stay, the engine part may not.
- Privacy: the opponent's username; the data is public. Effort M. Value high.

N11. Engine opening suites from Lichess open data. NEW.
- Space: Engines, opening suites, "Build from Lichess data".
- User gets: new suites without hunting for files: one position per named opening; balanced or unbalanced positions chosen by evaluation; the positions most played at a rating level.
- Data: chess-openings `dist/` EPDs; `lichess_db_eval.jsonl.zst` filtered by depth, `cp` window and material (complete the 4-field FEN with `0 1`); the monthly games dumps for popularity. All CC0, so suites can be shared. Output EPD or PGN for fastchess `-openings`.
- Fair play: none. Privacy: downloads only. Effort M. Value high.

N12. Open on lichess.org, share a game. NEW.
- Space: Chess, board context menu ("Open in Lichess analysis") and database ("Share on Lichess").
- Endpoints:
  - No API call: `https://lichess.org/analysis/{variant}/{FEN with _ for spaces}?color=black` (or `/analysis/{FEN}`), and `https://lichess.org/analysis/pgn/{SAN moves joined by _}` (5,000 characters at most; standard tags accepted when URL-encoded). Checked in lila's `UserAnalysis` controller and scalachess `Fen.clean`.
  - `POST /api/import` (none; 100/h, or 200/h with a token, which also lists the game in the user's imports): returns `https://lichess.org/game/{id}`. One game per call; whether comments and variations survive is not documented, so a commented game belongs in a study (P1).
- Fair play: both are aids, under the lock. Privacy: the position, or the whole PGN with player names; an imported game has a public URL. Effort S. Value medium.

N13. Correspondence "your move" panel. NEW.
- Space: Chess, home page.
- Endpoint: `GET /api/account/playing?nb=50` (token): `nbMyTurn`, and per game `isMyTurn`, `secondsLeft`, `opponent`, `rating`, `fen`. A click opens `https://lichess.org/game/{gameId}` (or the in-app board once P5 exists).
- Fair play: these positions must not reach engine, cloud eval, tablebase or assistant; explorer and repertoire are allowed in correspondence. Privacy: nothing. Effort S. Value medium.

N14. Bookmarks both ways. NEW.
- Space: Chess, database: a "Lichess bookmarks" collection, a star in game lists.
- Endpoints: `GET /api/games/export/bookmarks` (token; PGN or NDJSON; `since`, `until`, `max`, `sort`); `withBookmarked=true` on the existing sync; `POST /bookmark/{gameId}?v=true|false` (`preference:write`; `v` makes it idempotent).
- Fair play: none. Privacy: game IDs. Effort S. Value medium.

N15. Broadcast extras. NEW (extends DONE).
- Standings and players: `GET /broadcast/{tourId}/players`, `GET /broadcast/{tourId}/players/{playerId}`, `GET /broadcast/{tourId}/teams/standings` (none).
- Whole events: `GET /api/stream/broadcast/tour/{tourId}.pgn` (every ongoing round of a tournament) and `GET /api/stream/broadcast/group/{groupId}.pgn` (an event in several sections) (`study:read`).
- Lists: `GET /api/broadcast?live=true` (none); `GET /api/broadcast/search?q=` (`study:read`).
- History: the monthly broadcast dumps (P2, CC BY-SA 4.0).
- Fair play: none. Privacy: nothing. Effort S each. Value medium.

N16. Explorer and tablebase extras. NEW (extends DONE).
- `history=true` on `GET /lichess` (explorer.lichess.org, token): monthly popularity of the position, as a sparkline in the tree.
- `GET /masters/pgn/{gameId}` (token): open a master game from the explorer in the board or a database.
- Tablebase: `dtc=auxiliary` (default) answers 8-piece op1 positions; DTM covers 6 pieces; show DTC and DTM, allow lookups past 7 pieces when pawns oppose.
- Fair play: under the lock. Privacy: positions, as today. Effort S. Value medium.

N17. Play out a position on Lichess. NEW.
- Space: Chess, board, "Play this position on Lichess".
- User gets: a link to play the current position with a friend (or, for a coach, between two students) on lichess.org.
- Endpoints: `POST /api/challenge/open` (none; with a token it can be cancelled later) with `fen`, `clock.limit` and `clock.increment` or `days`, `name`, `users=white,black` to restrict who joins, `expiresAt` (2 weeks at most); the answer has `url`, `urlWhite`, `urlBlack`. To challenge one user: `POST /api/challenge/{username}` with `fen` (`challenge:write`).
- Games from a position cannot be rated. Fair play: if the linked user plays it, N6 locks the aids. Privacy: the position. Effort S. Value medium.

N18. FIDE player cards. NEW.
- Space: Chess, database player view and broadcast tiles.
- User gets: title, federation, birth year, standard, rapid and blitz ratings, photo and rating history, once the user confirms the match.
- Endpoints: `GET /api/fide/player?q=`, `GET /api/fide/player/{id}`, `GET /api/fide/player/{id}/ratings` (none); `GET /api/user/{username}?fideId=true` for Lichess users who show their FIDE ID.
- One request at a time: normalising millions of names is a job for FIDE's downloadable list, which the API is built from.
- Fair play: none. Privacy: the searched names; private OTB databases may name private persons, so on demand only. Effort M. Value medium.

N19. Offline evaluation cache from the evaluations dump. NEW (complements the DONE cloud eval).
- Space: Chess, analysis tab, consulted before `/api/cloud-eval`.
- User gets: instant evaluations of known positions with no request, so no position leaves the machine.
- Data: `lichess_db_eval.jsonl.zst` (417.9 M positions, CC0), filtered on import (positions of the user's databases, or above a material threshold), keyed on the 4-field FEN.
- Fair play: under the lock. Privacy: nothing after the download. Effort L. Value medium.

N20. Lichess preferences and kid mode. NEW.
- Endpoints: `GET /api/account/preferences` (`preference:read`): `autoQueen`, `premove`, `confirmResign`, `submitMove`, `zen`, `ratings`, `coords`, `pieceNotation`, board and piece theme names; `GET /api/account/kid` (`preference:read`).
- Use: online-play defaults (P5) that match the user's Lichess habits; kid mode turns off game chat. Map piece and board names onto RookStudio's own sets (Lichess's sets have their own licences).
- Effort S. Value low.

N21. Player notes sync. NEW.
- Endpoints: `GET /api/user/{username}/note`, `POST /api/user/{username}/note` (lila requires `note:write`; the spec lists no scope).
- Use: RookStudio's notes on an opponent and Lichess's private notes stay in step. Effort S. Value low.

### 5.3 DONE items and their extensions

| ID | DONE feature | Endpoints in use | Extensions |
|---|---|---|---|
| D1 | OAuth PKCE link, no scope at link, revoke on unlink | `/oauth`, `POST /api/token`, `DELETE /api/token` | N2 (scopes on demand, expiry) |
| D2 | Games sync | `GET /api/games/user/{u}` (NDJSON, `since`, `perfType`) | section 2 (URLs, game JSON); `sort=dateAsc` for a resumable sync; `accuracy`, `division`, `lastFen`; N7, N13, N14 |
| D3 | Opening explorer: masters, lichess, player | explorer.lichess.org `/masters`, `/lichess`, `/player` (token) | section 2 (v4); N16 |
| D4 | Cloud eval and tablebase, with consent and cache | `GET /api/cloud-eval`, tablebase.lichess.org `/standard` | section 2 (8-piece DTC); N16; N19 |
| D5 | Broadcasts: list, round PGN stream, live mosaic, save round | `/api/broadcast`, `/api/stream/broadcast/round/{id}.pgn` | section 2 (`study:read`); N15 |

### 5.4 PLANNED items, checked against the spec

P1. Studies import and export. PLANNED.
- Read: `GET /api/study/{id}.pgn`, `GET /api/study/{id}/{chapterId}.pgn` (`clocks`, `comments`, `variations`, `orientation`; none for public, `study:read` otherwise), `HEAD /api/study/{id}.pgn` (`Last-Modified`), `GET /api/study/by/{username}` (NDJSON `id`, `name`, `createdAt`, `updatedAt`), `GET /api/study/by/{username}/export.pgn` (moved 2026-01).
- Write (`study:write`): `POST /api/study` (2026-03), `POST /api/study/{id}/import-pgn`, `POST /api/study/{id}/{chapterId}/moves` (2026-03), `POST /api/study/{id}/{chapterId}/tags` (2025-09), `DELETE /api/study/{id}/{chapterId}`.
- Extensions, verified:
  - Annotated game to a study: assistant comments as text, marked as coming from the assistant; arrows `[%cal Ge2e4]` and circles `[%csl Gd4]` (G, R, Y, B), `[%clk]` and `[%emt]` are parsed on import (lila `CommentParser`); a later edit replaces the chapter's moves with `/moves` instead of adding a chapter. lila also reads `[%anno "Name"]` as a comment's author (undocumented).
  - Repertoire as an interactive lesson: `import-pgn` with `mode=gamebook` (or `practice`, `conceal`) and `orientation`, one chapter per line family (64 per study at most); importing a study back with `orientation=true` gives the repertoire's side.
  - Sync without re-downloading: `HEAD` and `Last-Modified`.
- Gotchas: 30 new studies a day; always send `visibility` (schema default and description disagree); default to `private` when the assistant's text is included.
- Privacy: what is pushed, at the chosen visibility. Value high. Effort M.

P2. Monthly database dumps import. PLANNED.
- Section 4 has the files, sizes, fields and known issues.
- Extensions: the broadcast dumps (1.3 M OTB games, almost all with evals; CC BY-SA 4.0, so keep attribution and licence on anything redistributed); filters on `[%eval]` presence, `BOT` titles, Glicko-2 ratings, time control; flags for the known-issue months; stop a download early (partial files decompress).

P3. Puzzles with results sent back. DONE (#80, 2026-11-20): batches of 50 at most (`GET /api/puzzle/batch/{angle}?nb=50&difficulty=`, `puzzle:read`), solved here, kept in the profile until their results are taken (`POST`, `puzzle:write`, `nb=50` bringing the next batch in the same answer; `difficulty` is sent with the POST too, which the spec documents for the GET only: when the user chose another theme or difficulty since, the POST asks for no batch and a GET follows); a 429 or a cut sent again a minute later, a refused token said; `GET /api/puzzle/dashboard/30` read once a session for the last 30 days by theme. Not used: `replay`, `activity`, `daily`, `next`. Offline, the dump (`docs/design/puzzles.md`). As planned:
- Endpoints: `GET /api/puzzle/batch/{angle}?nb=50&difficulty=` (none; `puzzle:read` for unseen puzzles at the user's rating); solve offline; `POST /api/puzzle/batch/{angle}` (`puzzle:write`) with `{"solutions":[{"id","win","rated"}]}` and `nb` to get the next batch in the same call; `GET /api/puzzle/dashboard/{days}`, `GET /api/puzzle/replay/{days}/{theme}`, `GET /api/puzzle/activity?since=` (`puzzle:read`); `GET /api/puzzle/daily`, `GET /api/puzzle/{id}`, `GET /api/storm/dashboard/{username}` (none).
- Extensions: train the weakest dashboard themes, and tie them to the key moments of the local review (for instance, forks missed in the user's own games shown next to their success rate on the `fork` theme); a daily-puzzle tile on the home page; the dump's `GameUrl` shows which of the user's synced games became Lichess puzzles.
- Never use `next` or `batch` to download the collection (the dump exists). Value high. Effort M.

P4. TV. PLANNED.
- Endpoints (none): `GET /api/tv/channels`, `GET /api/tv/{channel}/feed`, `GET /api/tv/feed`, `GET /api/tv/{channel}?nb=30`, then `GET /api/stream/game/{id}` per game (8 per IP).
- Extension for the Engines space: the `bot` and `computer` channels as a mosaic of the strongest Lichess bots, with local engines' evaluations beside their moves.

P5. Online play through the Board API, with a fair-play lock. PLANNED.
- Endpoints (`board:play`): `GET /api/stream/event`, `POST /api/board/seek`, `GET /api/board/game/stream/{id}`, `POST /api/board/game/{id}/move/{uci}` (`offeringDraw`), chat (GET and POST), `abort`, `resign`, `draw/{yes|no}`, `takeback/{yes|no}`, `claim-victory`, `claim-draw` (2025-07), `berserk`; challenges `POST /api/challenge/{username}`, `.../accept` (`color`), `.../decline` (`reason`), `.../cancel`, `POST /api/challenge/ai` (level 1 to 8), `GET /api/challenge`.
- Constraints: Rapid, Classical and Correspondence (Blitz only for direct challenges, the AI and bulk pairings); a real-time seek lives while its connection is open; open the event stream first; one event stream per token, shared with N6 and N7.
- Extensions: the correspondence rule of section 1 (opening book allowed); N20 preferences; kid mode turns off chat; `GET /api/account/playing` to resume games after a restart.

P6. Lichess bot for a local engine. PLANNED.
- Endpoints (`bot:play`): `POST /api/bot/account/upgrade`, `GET /api/stream/event`, `GET /api/bot/game/stream/{id}`, `POST /api/bot/game/{id}/move/{uci}`, chat, `abort`, `resign`, `draw`, `takeback`, `claim-victory` (2025-04), `claim-draw` (2025-07); challenges as in P5; `GET /api/bot/online?nb=` (none, up to 512).
- Extensions: before the upgrade, check with `GET /api/account` that `count.all` is 0 and say plainly it cannot be undone; a "bot gauntlet" that challenges online bots (`POST /api/challenge/{bot}`) and feeds RookStudio's ratings; Arenas created with `conditions.bots=true` (`POST /api/tournament/{id}/join`, `tournament:write`).
- Rules: otherwise challenge games only; no UltraBullet; no sandbagging or boosting; casual while testing.

P7. Arenas and Swiss. PLANNED.
- Endpoints in section 3; reading is open, joining needs `tournament:write`.
- Extensions: the user's team events on the home page (`GET /api/team/of/{me}`, `team:read` per lila; `GET /api/team/{id}/arena` and `/swiss` with `status`, `createdBy`, `name`; `GET /team/updates`, `team:read`); importing a club Swiss with standings (`GET /swiss/{id}.trf`, `GET /api/swiss/{id}/games`, `GET /api/swiss/{id}/results`).

P8. External engine. PLANNED.
- Endpoints: `POST /api/external-engine` (`engine:write`) with `name`, `maxThreads`, `maxHash`, `variants`, a random `providerSecret`; `GET`, `PUT`, `DELETE /api/external-engine/{id}` (`engine:read`, `engine:write`); provider loop on engine.lichess.ovh: `POST /api/external-engine/work` (`providerSecret`; long polling; 204 when idle), then stream UCI output to `POST /api/external-engine/work/{id}`.
- Fit: RookStudio already drives UCI engines locally and over SSH; as the provider it can put any of them, a remote one included, on the user's lichess.org analysis board.
- Requirements: `UCI_Chess960` mode, `multipv` 1 to 5, `info` lines with depth, multipv, score, nodes, time and pv, then `bestmove`; stop when the connection closes; the `providerSecret` stays in RookStudio.
- Status of the API: alpha, "subject to change"; reference provider github.com/lichess-org/external-engine.
- Fair play: Lichess offers external engines on analysis pages only. Privacy: Lichess sees the engine output; requests come from the user's own browser. Value high. Effort M.

## 6. Scope plan: which scope, for which feature, asked when

A new authorization replaces the token: ask for the union of the granted scopes and the new one, pass `username` as a hint, check the user with `GET /api/account`, then revoke the old token (N2).

| Scope | Features | Ask when |
|---|---|---|
| any token, no scope | games sync (faster), explorer, rating history (N4), ongoing games (N6 fallback, N13), bookmarks export (N14), import credited to the user (N12) | at link time (as today) |
| `study:read` | broadcasts (DONE, now required), private and unlisted studies (P1, N5), whole-event streams and search (N15), resuming an own broadcast (N9) | first time a broadcast is followed |
| `study:write` | pushing to studies (P1), own broadcast (N9) | first push |
| `challenge:read` | event stream for the fair-play guard (N6) and the finished-game pipeline (N7) | when the user turns either on |
| `puzzle:read`, `puzzle:write` | puzzles (P3, DONE) | both at once, at the first use of the puzzles online |
| `follow:read` | seeding the Live follow list with friends (N8) | when "follow my friends" is chosen |
| `preference:read` | preferences and kid mode (N20) | first online game setup |
| `preference:write` | bookmark toggle (N14) | first star |
| `board:play` | online play (P5); also opens the event stream | first online game |
| `challenge:write` | direct challenges outside P5 (N17) | first challenge |
| `engine:read`, `engine:write` | external engine (P8) | when an engine is registered |
| `tournament:write` | joining Arenas and Swiss (P7), bot Arenas (P6) | first join |
| `team:read` | team events (P7) | when team events are turned on |
| `note:write` | notes sync (N21) | first note sync |
| `bot:play` | bot (P6), on a separate, game-less account with its own token | bot setup |
| not needed | `email:read`, `msg:write`, `racer:write`, `challenge:bulk`, `team:write`, `team:lead`, `web:mod`; `follow:write` only if a "follow" button is wanted | never |

## 7. Changes in 2025-2026

From the repository history. Months are when the spec documented the change, which can trail the server by days.

Added

- 2025-01: bookmarked games export (`/api/games/export/bookmarks`).
- 2025-02: FIDE player lookup and search; clocks of AI games can be started; broadcast rounds fed from Lichess usernames (`syncUsers`); Arena conditions documented, including `conditions.bots`.
- 2025-03: puzzles to replay (`/api/puzzle/replay/{days}/{theme}`); team Arena and Swiss filters.
- 2025-04: Bot claim-victory; `@lichess-org/types` npm package of the spec's types.
- 2025-05: broadcast search.
- 2025-07: claim-draw for Board and Bot; broadcast tiebreaks; broadcast tournament `visibility`.
- 2025-08: puzzle race results (`/api/racer/{id}`); event-stream rule (one per token) and the 8-streams-per-IP rule documented; `insufficientMaterialClaim` status; `color` on challenge accept.
- 2025-09: study chapter tags update; puzzle batch fetch and solve, with the new `puzzle:write` scope; puzzle `color`; broadcast player detail; broadcast groups; `patronTier`.
- 2025-10: tablebase DTC and op1 documented; authenticated callers see their private and unlisted broadcasts.
- 2025-11: broadcast round `syncSource` and `?patch=true`; `Accept` header documented for PGN or NDJSON; `profile` and `rank` flags on `GET /api/user/{username}` and `POST /api/users`; `exists` and `teacher` on autocomplete; docs moved to the Scalar renderer.
- 2025-12: `gameState.expiration`; photos of FIDE and broadcast players.
- 2026-02: `fideId` flag on `GET /api/user/{username}`; broadcast team leaderboard; FIDE rating history; broadcast player stats by FIDE time control; `clocks` and `comments` on broadcast PGN; `since` on puzzle activity; tablebase `dtc=auxiliary` by default and 6-piece DTM.
- 2026-03: study creation (`POST /api/study`, 30 per day); spectator chat of a game; replacing a study chapter's moves.
- 2026-04: player rating in game stream events; puzzles return their initial FEN; FIDE `gender`; study import `mode`; `nb` on online bots (up to 512); `live=true` on the official broadcasts list.
- 2026-05: broadcast player `ongoing`; puzzle `lastMove`; broadcast group and regulations info.
- 2026-06: broadcast `customScoring` and `teamCustomScoring`; group PGN stream.
- 2026-07: bookmark toggle (`POST /bookmark/{gameId}`); study `flair` and `description`.
- 2026-08: tournament PGN stream; team updates.
- 2026-09: `note:write` scope; Arena player `realName`; per-phase accuracy in game analysis; `rating` in `/api/account/playing`; `from`/`to` in perf stats; `ratingDiff` in game stream players.
- 2026-10: `/api/version` documented.
- 2026-11: `fen` in `/api/puzzle/next`.

Changed

- 2026-01: ongoing games on the export and spectator endpoints are delayed by 3 moves (was 3 to 60 seconds depending on the time control); study export by user moved from `/study/by/{username}/export.pgn` to `/api/study/by/{username}/export.pgn`.
- 2026-02: `/api/team/of/{username}` needs a token.
- 2026-03: explorer and tablebase hosts moved from `.ovh` to `.org`; the explorer requires a token.
- 2026-06: `arenaTour`/`swissTour` replace `tournament`/`swiss` in game JSON.
- 2026-08: broadcast tournament and round endpoints need `study:read`; rating history only computed for authenticated requests.
- 2026-09: Lichess AI sides modelled as `GamePlayerAi`; `opponent` in `/api/account/playing` documented as a player, the AI or an anonymous player.
- 2026-10: game URLs are `/game/{id}`; broadcast search needs `study:read`; rating-history names are perf keys.
- 2026-11: autocomplete needs 4 characters; User-Agent mandatory; asset URLs on `lichess.org` instead of `lichess1.org`; explorer v4; `/api/users` limits raised to 10,000 users per 10 min and 150,000 per day for authenticated calls (from 8,000 and 120,000 for everyone), "fewer" for anonymous ones.

Deprecated, still present

- `BroadcastTop.upcoming` (always empty, 2025-11); `BroadcastRoundInfo.finished` (use `finishedAt`); the `patron` boolean (use `patronColor` or `patronTier`); private tier of broadcast tournaments (2025-07).

Removed

- 2025-04: study `source` argument. 2025-06: `players` parameter of the game export endpoints. 2026-04: single `ratingDiff` and `performance` of broadcast players (replaced by per-FIDE-time-control maps). 2026-08: `dark` and `transp` preferences. 2026-09: `/api/timeline`.

## 8. Dropped ideas, out-of-spec routes, and what could not be verified

Dropped

- Private messages (`POST /inbox/{username}`, `msg:write`): little use in a desktop GUI, abuse risk.
- E-mail address (`email:read`): not needed.
- Bulk pairings (`challenge:bulk`): an organiser tool that needs every player's token.
- Puzzle races (`racer:write`): the race runs on lichess.org.
- Simuls: listing only.
- Team management (`team:write`, `team:lead`): better on the website.
- Admin challenge tokens (`web:mod`): moderators only.
- Asking Lichess to analyse a game: not in the API (lila has a web route, `POST /game/{id}/request-analysis`, undocumented). N7 uses Lichess's analysis only when it already exists.
- Conditional premoves (forecasts): web routes only.
- Lichess Insights: no API.
- `/api/timeline`: removed in 2026-09.
- Following one player's games through `games-by-users`: it only reports games where both players are listed; N8 uses status polling and game streams instead.
- Bulk use of the cloud eval or puzzle endpoints: the docs forbid it; use the dumps (P2, N11, N19).
- `syncUrl` for N9: needs a server reachable from the Internet; push instead.

Out-of-spec routes seen in lila, not proposed

- GIF exports: `/game/export/gif/{id}.gif`, `/study/{id}/{chapterId}.gif`, `/export/fen.gif`, puzzle thumbnails.
- Embeds: `/embed/game/{id}`, `/embed/analysis?fen=`, `/study/embed/{id}/{chapterId}`, `/embed/broadcast/...`.
- These are web pages and images, not API, so they can change without notice. The analysis URLs of N12 are in the same category but are quoted by the API docs themselves (`/analysis/pgn/...`, in the `/api/import` description).

Not verified, or inconsistent in the sources

1. https://lichess.org/api was not fetched (lichess.org answers automated fetches with 403); its source, the spec, was read in full.
2. Whether `[%eval]` comments pushed into a broadcast round are shown; not documented.
3. The anonymous rate limits for broadcasts after lila PR 21283; the docs only say "heavily rate-limited".
4. Whether `/api/import` keeps comments and variations; the docs only say "most standard tags are supported".
5. Whether TV feeds count against the 8 game streams per IP.
6. The largest number of games in one broadcast round; documented limits are 100 per push and 100 `syncIds`.
7. Spec and lila source disagree: `/api/team/of/{username}` (spec: any token; lila: `team:read`); notes (spec: any token; lila: `note:write`); `games-by-users` (spec: 300 users; lila: 500 with a token).
8. The spec disagrees with itself: study `visibility` default (`unlisted` in the schema, `public` in the text); open-challenge URLs (`whiteUrl`/`blackUrl` in the text, `urlWhite`/`urlBlack` in the schema and the example); cloud-eval size (about 320 M in the spec, 417.9 M in the dump); the Bot tag says tournaments are off-limits while Arenas accept `conditions.bots`.
9. `[%anno "Name"]` comment authors come from lila's source, not the API docs.
10. The external-engine API is alpha; nothing here was tested against it.
11. No endpoint was called: behaviours are those documented in the spec and visible in lila's source.

## Appendix: method

- Cloned github.com/lichess-org/api (HEAD `448070d`, 2026-11-16) and parsed every operation referenced by `doc/specs/lichess-api.yaml` with a script (YAML only, nothing executed from the clone): 190 operations, 24 tags, including one `HEAD`.
- Dated each operation by its first appearance in the history (`git log -S` on the operationId and on the path), and read the diffs of the changes listed in section 7.
- Read database.lichess.org in three parts (its text is 276,000 characters).
- Checked in lila's source: `conf/routes`; the auth wrappers of `Api.eventStream`, `Api.gamesByUsersStream`, `Game` exports, `TeamApi.teamsOf`, `User` notes and rating history, `Study` exports, `Racer.apiGet`, `Account.apiNowPlaying`, `Importer.apiSendGame`; `UserAnalysis` for analysis URLs; scalachess `Fen.clean`; `modules/study/.../CommentParser.scala`; `translation/source/faq.xml`.
