# Endgames against the tables, mates, board vision

A catalogue of endgames played against the tablebases, the mates of Lichess's puzzles, and board vision (#82, mockup
M12, board 3). The code: `gui/src/endgames.*` (the catalogue, the tables' answers from the disk or from Lichess, a move
judged, how a game ends, the profile's files), `gui/src/syzygy_download.*` (the tables of 3 to 5 pieces downloaded),
`gui/src/vision.*` (the coordinates, blindfold, a line read in one's head), `gui/src/view_endgames.cpp` and
`gui/src/view_vision.cpp` (the Training page's parts, a session on the Board page).

## The tables' answers

Both sources answer as Lichess's tablebase does (`lichess::Tablebase`): the side to move's outcome, and each legal move
with the outcome it leaves the other side, the best first for the side to move.

- **The disk** (`probeDiskTables()`): the setup's Syzygy folders (the tournaments', Settings › Adjudication), with the
  profile's `syzygy` folder once it holds tables, probed under `lockSyzygy()` as the Board page probes them, never while
  a tournament plays in the program's process (its games probe the same tables without the lock). With the DTZ tables,
  `tb_probe_root()` gives every move's outcome with the fifty-move counter and its distance to the next capture or pawn
  move; without them, the WDL tables give each move's outcome, the positions as if right after a capture or a pawn move.
  The order the tables play in: the best outcome; among wins the quickest (to mate when known, else to the next capture
  or pawn move), among losses the longest; then, among moves as good, the one after which the other side has the fewest
  moves keeping its outcome (the tables make it hard). A position where the side not to move is in check is none: the
  probing code would take the king.
- **Lichess** (`lichess::tablebase()`): up to 7 pieces, one request a position played, with the user's consent to the
  online tables (the Board page's, `lichess.json` `tablebase`: 1); its order kept. Each answer is kept in the profile's
  `lichess-cache` for good (a verdict does not change), as the Board page keeps them. A request that gives nothing (the
  network, a 503) is asked again after 10 seconds, a 429 after a minute (the host's pause, `HostGate`); a move of the
  user's played meanwhile is kept and judged when the answer comes.

The disk answers a position when its tables cover it whole: the most pieces whose WDL tables are all there, those of
fewer pieces too (a capture leads to them). Else Lichess, with the consent; else the position says what it needs.

## The catalogue

Eighteen endgames in four groups; the goal of each is what the tables say of its start, the user to move:

| Group | Endgame | Goal | Start | Checked |
|---|---|---|---|---|
| Elementary mates | Queen | mate | `8/8/8/4k3/8/8/8/4K2Q w` | the tables mate in 7 |
| | Rook | mate | `8/8/8/4k3/8/8/8/R3K3 w` | in 14 |
| | Two bishops | mate | `8/8/8/4k3/8/8/8/2B1KB2 w` | in 16 |
| | Bishop and knight | mate | `8/8/8/4k3/8/8/8/1N2KB2 w` | in 29 |
| Pawns | The opposition | win | `4k3/8/4K3/4P3/8/8/8/8 w` | Kd6 and Kf6 win, Kd5 and Kf5 draw |
| | The rule of the square | win | `8/8/8/8/P4k2/8/8/7K w` | a5 alone wins |
| | The outside passed pawn | win | `8/8/3k4/6p1/P2K2P1/8/8/8 w` | every move wins; the technique after |
| | Réti's manoeuvre | hold the draw | `7K/8/k1P5/7p/8/8/8/8 w` | Kg7 alone holds |
| Rooks | Lucena | win | `1K6/1P1k4/8/8/8/8/r7/2R5 w` | 11 moves of 14 win |
| | Philidor | hold the draw | `4k3/R7/1r6/4PK2/8/8/8/8 b` | 8 moves of 16 hold |
| | Vancura | hold the draw | `R7/8/P4rk1/8/8/8/6K1/8 b` | 4 moves of 19 hold |
| | Rook against pawn | win | `8/8/K7/8/5k2/4p3/8/7R w` | Kb5 alone wins |
| | Rook and two pawns against rook | win | `8/1R6/5k2/8/4KPP1/r7/8/8 w` | 6 pieces |
| Pieces and queen | Queen against rook | win | `3k4/2r5/3K4/8/8/8/8/4Q3 w` | Qa5 wins the rook at once, 14 other moves later |
| | Queen against pawn | win | `8/8/8/8/8/8/3pk3/K6Q w` | 4 moves of 23 win |
| | The wrong bishop | hold the draw | `8/5k2/8/4K2P/2B5/8/8/8 b` | Kg7 alone holds |
| | Rook against bishop | hold the draw | `8/6k1/4K3/8/8/8/1R6/5b2 b` | 9 moves of 13 hold |
| | Bishop against knight | win | `8/4n3/3k4/1P6/2K2P2/3B4/8/8 w` | 6 pieces |

The starts of 5 pieces or fewer were probed with the tables of 3 to 5 pieces (WDL and DTZ); the tests check those of 4
pieces or fewer against the repository's tables, and every one of 5 pieces or fewer when `FCG_SYZYGY` names a set of 3
to 5 pieces. The two of 6 pieces are textbook wins an engine with the 5-piece tables scores +7 and more after seconds;
their verdict is Lichess's when they are played (a start that turned out otherwise would be played for what the tables
say). Each endgame has a line of its idea, said with a fault and over its name.

## A game

The user plays from the start; each move is judged by the tables' answer for the position before it: the result for the
user (+1 a win, 0 a draw, the fifty-move rule's spoiled wins and losses included, -1 a loss) before and after it; a fault
when it falls, with the moves that kept it. The tables then answer, half a second later. After a fault: take it back
(both moves), see the tables' line from the position before it (the disk's followed move by move, else Lichess's
mainline in one request), or play on.

A win counts at the mate; or, unless the mate itself is the goal (the elementary mates), when a pawn of the user's is
promoted or the user takes the other side's last man but its king, the win kept. A draw counts when a rule draws the
game (stalemate, too little material, the threefold repetition, the fifty moves), or after 15 of the user's moves with
the draw kept. In the elementary mates, the user's moves to mate are set against the tables' from the start (Lichess's
DTM, else the DTZ, the same there: no capture nor pawn move comes before the mate).

## What the profile keeps

`endgames/record.json`: each catalogue endgame's tries, those won or held, whether once without a fault nor a hint, the
mates' best and the tables'; the positions missed, each a card of the day's review (FSRS, the repertoires' scheduler):
the position before a fault, back the next training day, then graded at its end (reached without a fault nor a hint
Good, with a hint or a move taken back Hard, a fault or no end Again). A fault in a card played again makes a card of its
own position, unless it is the card's. `vision.json`: the coordinates' records by way and side, the lines read and
right, the blindfold games, *Where is…?* asked and right.

## The download

The 290 files of 3 to 5 pieces (983,957,920 bytes) from `tablebase.lichess.ovh/tables/standard/3-4-5/`, into the
profile's `syzygy` folder, one at a time in the host's turn (`HostGate`), the smallest piece counts first: those of 3 and
4 pieces (4.3 MB) are there within seconds, and the tables are loaded again as each piece count's files are all there
(`forgetSyzygy()`, not while a tournament plays in the process). Each file through `httpDownload()`, resumed from its
part; checked by its size (16 bytes over a multiple of 64) and Syzygy's mark (WDL `71 e8 23 5d`, DTZ `d7 66 0c a5`):
the 3-piece files circulate in two variants of the same tables (fastchess' test files and Lichess's differ for KPvK, KQvK
and KRvK), so no single hash is known. Once all there, the folder joins the setup's tablebase folders, for the
tournaments too.

## Board vision

- **Coordinates**: 30 seconds; a square named (any but the last one) to click on a board without its letters and figures,
  or marked in blue to name with two keys; from White, Black, or either square by square. The record is the most squares
  right, by way and side.
- **Blindfold**: a `BoardMatch` between the user and an engine of the library (the analysis's by default), at
  `UCI_LimitStrength` and `UCI_Elo` when the engine has them (1000, 1500, 2000), else at its own strength; the user's clock
  an hour and 30 seconds a move, the engine's a minute and a second. The board hides every piece, every piece after the
  10th move, or all but the pawns. Shift held alone a quarter of a second (a capital letter typed does not) shows them two
  seconds, three times a game. *Where is…?* (F2) names a piece its colour and kind name alone, a bishop by its squares'
  colour when its side has two, the kings when nothing else is alone.
- **A line read in one's head**: from 16 well-known openings (their moves checked by the tests), a position after two
  moves of each side at least, the next six moves written, then the piece on a square they changed (the later moves'
  squares first).
