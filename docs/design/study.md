# The study layer: games, PGN and databases

`study/` is the library under the Chess space: the game model, the PGN reader and writer, and the databases. It has no
user interface (`fcg_study`, tested by `fcg-study-tests`, measured by `fcg-study-bench`).

## PGN

- `pgn_lexer` cuts PGN text into tokens without copying it.
- `pgn_reader` builds a `GameTree`: the moves, the variations, the comments before and after a move, the symbols (NAGs
  and `!` `?` glyphs), the graphics (`[%cal]`, `[%csl]`), the clocks and evaluations, and the commands it does not know,
  kept as they were. Text not in UTF-8 is read as Windows-1252.
- `pgn_writer` writes a tree back. Reading what it wrote gives the same tree, and writing what it read changes nothing
  more: a random mutation campaign of 600 000 texts and a fuzzer (`fuzz/pgn_reader.cpp`) hold it to that.

## The board and the move codes

`ScanBoard` is a board made for reading games fast: SAN parsed against it, moves played on it, and each move given a
code. The legal moves of a position are numbered in an order that only depends on the position, piece by piece; a
move's code is its rank in that order, one byte when the position has 256 legal moves or fewer (every real game), two
otherwise. Decoding a code needs no move generation: the code's piece and target are found by counting the pieces'
moves. A million random games (355 M plies, Chess960 and set-up positions among them) decode to the moves chess.hpp
plays, and the Polyglot key of each position (`zobrist.hpp`) is the one of the Polyglot books.

## Import

`importGames` reads a PGN file, plain or compressed (`.zst`, including Lichess's files made of frames after skippable
frames, `.gz`, `.zip` of `.pgn` entries, zip64 included), cut into pieces at the start of a game, read by several
workers, and given back in the file's order. The memory it holds stays bounded whatever the file's size. A game's text
is kept when it has comments, symbols or variations, or could not be read whole (damaged), its lines ended by `\n`
whatever the file's.

`importInto` adds a file's games to a database, committed every 262 144 games: a crash or a cancel keeps what was
committed, and the same file imported again adds only what is missing.

## Databases

A database is a folder:

```
Name.fcgdb/
  manifest.json   {"format":"rookstudio-database","version":1,"generation":N,"segments":[{name,games,names}]}
  tournament.lock held while a program writes to it (flock: released when the program ends, however it ends)
  seg-NNNNNN/     a segment, written whole and never changed:
    cols.bin      the header columns
    moves.bin     the move codes, one byte a ply
    annex.bin     what only some games have
    names.bin     the names this segment adds
    sig.bin       the games' signatures, for the searches (not in the first format's segments)
```

### Columns

`cols.bin` holds, after a 32-byte header, one column after another, each 8-byte aligned:

| Column | Bytes a game |
|---|---|
| White, Black, Event, Site, Round, Annotator, TimeControl | 4 each: a name's id |
| Date (year << 9, month << 5, day; 0 for a part not known) | 4 |
| Plies | 4 |
| White's rating, Black's rating, ECO (1 + A00..E99), flags | 2 each |
| Result | 1 |
| Key (for the duplicates) | 8 |
| Offsets in moves.bin and annex.bin | 8 each |

69 bytes a game. Names are kept once for the whole database: each segment's `names.bin` holds the names it adds, the
ids counting on from the segments before it.

### Annex

A game's annex record is a byte of the parts it has, then each part: its start position (FEN), its other tags, its
text, its clocks (zigzag differences from the clock two plies before, varints), its evaluations.

### Commits

An append writes its segments, syncs them, then writes the new manifest beside the old one, syncs it and renames it
over the old one: the rename is the only commit. A database opened after a crash is the one of its last manifest;
what a commit cut short left (a segment the manifest does not name, a `.tmp` file) is removed. The tests stop a commit
at each of its steps (`setCommitFaultHook`) and check that the database reopens with all of the commit or none of it,
and goes on.

A reader holds a `Snapshot`: the segments it maps stay valid while later commits add others.

### Games replaced and deleted

A game keeps its number. `replace` writes its new version in a segment of versions (named in the manifest with
`"versions": true`: its games are no games of their own), and the manifest names an edits file (`edits-NNNNNN.bin`:
each game replaced with its version's place, and the games deleted), written and synced before the manifest that
names it, the rename of the manifest still the only commit. `setDeleted` marks games deleted, or no longer: the list
shows them only when asked (`Filter::with_deleted`), the searches and the tree leave them out. The old versions stay
in their segments until the database is tidied (not written yet). A crash at any step of a replacement leaves the game
as it was, or as it is to be, as the tests check.

### Duplicates

A game's key is a hash of its event, site, round, players, date, start position, moves and result. An append leaves out
a game whose key the database has, or a game before it in the same append, unless asked to keep them. Two rounds of a
tournament with the same moves are two games.

### Views

A view is the list of a snapshot's game ids that pass a filter, in the order of a column: the game list pages through
it with no query per page. A filter folds each name once (letter case and Latin accents left out) into a bit for each
word it looks for, then checks each game in a few operations on its columns, on all cores. A sort reads one column,
names by their rank in alphabetical order, and sorts it four bytes at a time (stable; descending without reversing
the games of one key).

## Signatures

`sig.bin` holds 24 bytes a game: for each pawn's home square, the first ply after which its pawn is gone (255: still
there, or gone after ply 254), the material at the end (4 bits a count), and whether a pawn was promoted. A pawn never
comes back to its home square, and a side's pawns and men all told only go down (each kind too, without promotions):
the pawns a position has on and off their squares bound the plies at which a game can reach it, and its material
whether the game ever has as much. The importer makes the signatures as it reads the moves (4 % more time, 282 MB for
11.7 M games); a game added without one gets it from its codes. A database made before them has none, and its games
are searched by replaying them whole.

## Searches

`Search` looks through a snapshot's games on every core, a thread taking 4 096 games at a time, its hits in the order
of the games as they come; a cancel stops it between two games. Three queries:

- a position exactly (men, side to move, castling rights, en passant): the signature's plies, then a replay up to them;
- material: counts within bounds, for a number of plies in a row (an exchange under way is not a material);
- a pattern: men on squares, empty squares, and a side's pawns exactly those given.

The tests compare each with a replay of every ply of every game, signatures unused.

## Files and tournaments as databases

`LinkedDatabase` keeps a PGN file, or a tournament of the Engines space, as a database in the program's cache
(`<cache>/<key>.fcgdb`, beside a note of what was read, `<key>.json`), for the list, the searches and the tree to work
on them. A refresh reads what was appended since: a plain PGN file's new bytes, its whole games only (one still being
written waits for its end), or a tournament's newly counted games, in the order they finished, each with the text it
counts with (a result set by hand). What was read is read again from the start when it changed: the first or the
last bytes read of a file differ, a compressed file changed at all, a tournament's game was voided or its result set,
or the database holds another count of games than the note (a commit the note did not follow).

## Opening tree

`buildTree` makes a database's opening tree and `Tree` reads it: for a position, the games that reach it in their first
60 plies (a build option), the moves they play from it and how those games ended. A game counts once in a position,
where it reaches it first, with the move it plays from there.

### What is kept

Sorted by their moves, games make a trie: the games that open with the same moves follow one another, so the games
that reach a position by one move order are a run of that order. A position two games or more reach is kept with its
runs: one for each move order several games share, one for each game that reaches it by an order of its own (a
transposition, also when only such games reach it). A position a single game reaches is not kept: the runs of the
position before tell which game played the move that leads there. A position of 256 games or more keeps its moves'
numbers (games, results, ratings, latest date); the others count their games when asked, 255 at most.

```
Name.fcgdb/
  tree.json       {"format":"rookstudio-tree","version":1,"folder":"tree-NNNNNN","games":N,"plies":60,...}
  tree-NNNNNN/    order.bin    the games' ids in the order of their moves (4 bytes a game)
                  keys.bin     the kept positions' Polyglot keys, sorted (8 bytes a position)
                  index.bin    where every 32nd position's record starts
                  records.bin  a position's games, its runs (first game, length, depth), its moves' numbers if popular
```

### The build

1. The games sorted by their move codes (one byte a ply, so the order of a trie), and how many codes each shares with
   the game before it.
2. Every game replayed along that order from the board of the game before, where the two part: the positions games
   share by the same moves, and their runs.
3. The positions a game reaches by moves of its own: whether another game reaches them too is only known once all are
   counted, so they are gathered and sorted by key, in passes over parts of the keys that fit in the memory given
   (2 GB by default), each pass replaying the games.
4. The records written in key order into `tree-NNNNNN.tmp`, synced, renamed, then named in `tree.json`, whose
   replacement is the commit. The tree before is removed (on Windows, by a later build if it is still mapped).

The tests compare every answer with a recount that replays each game (transpositions, repetitions, duplicates, games
of their own start), and builds in one pass and in many, on one thread and on several, give the same files.

### Novelties

`novelty` walks a line from the start: a kept position is reached by two games or more; past the last one, the runs
of the position before give the single game that played the line's move, followed while the line is its moves. The
first position no game is seen to reach is the novelty. A position a single game reaches by other moves than the
line's is not seen: the position search (#15) confirms a novelty before a comment states it. Among random lines that
cross many move orders, a fifth of the novelties came earlier than a recount for that reason.

### Left out, measured

- A Bloom filter over positions a single game reaches, for an instant novelty mark: the reference database has 395 M
  of them within 60 plies, 0.5 GB at 1 % of false answers, as much as the tree itself.
- Delta generations for the games added after a build: a whole build of the 11.7 M games takes 16 s (29 s on 8
  threads), a smaller database seconds. Until a new build is done, the tree answers for the games it covers
  (`Tree::games()`).

## Beside the tournaments

A thread that calls `enterBackground()` runs at the lowest priority (a niceness of 19 on Linux, the lowest thread
priority on Windows, the utility class on macOS), and so does every thread the library starts from it: the import's
readers and workers, the opening tree's, the searches', the lists'. The window starts the study's long work from such a
thread, so that the engines of a running tournament, processes of their own, keep the machine first.

## Benchmark

`fcg-study-bench` measures the store on real files. It is built with `-DFCG_STUDY_BENCH=ON` and never runs in the CI:
the files are large, and some are not ours to share.

```sh
cmake -B build -DFCG_STUDY_BENCH=ON && cmake --build build --target fcg-study-bench
build/fcg-study-bench import <games> <database> [--threads N]   # a database that does not exist is made
build/fcg-study-bench query <database>                         # open, first rows, sorts, filters
build/fcg-study-bench decode <database>                        # every game replayed from its codes
build/fcg-study-bench crash <games> <database> [--kills N]     # imports killed (SIGKILL) at random moments
build/fcg-study-bench tree <database> [--plies N] [--memory MB] # the opening tree built, then asked
build/fcg-study-bench search <database>                        # positions, material, patterns, a cancel
```

### Results

Measured on 2026-11-17 on an AMD Ryzen 9 9950X (16 cores, 32 threads), an NVMe disk, Linux. The reference database is
a PGN export of a commercial database of 11 741 260 games (8.1 GB), kept on that machine only; the month of Lichess
games is January 2013 (CC0, `lichess_db_standard_rated_2013-01.pgn.zst`, 121 332 games).

| Measure | Threshold | Result |
|---|---|---|
| Decoding, one core | ≥ 25 M plies/s | 36.6 M plies/s (920 M plies of 11.7 M games in 25 s); 617 M plies/s on 32 threads |
| Import of 12 M games, 8 cores | ≤ 6 min | 15.4 s with the file in the page cache, 20.8 s read from the disk (534 MB/s) |
| First rows (open, view of all, 50 rows) | ≤ 150 ms | 17 ms |
| Sort of 11.7 M games | ≤ 1 s | 75 to 255 ms (the first sort by a name ranks the names) |
| Filters | — | 5 to 14 ms |
| Imports killed (SIGKILL) at 1 000 random moments | the database always reopens | always reopened, every game replayed, nothing left over; then an import not killed has every game |
| Opening tree, warm | ≤ 5 ms | 1 µs median, 0.07 ms at most (20 000 positions of random games within 30 plies, and the most played line to 40 plies) |
| Opening tree, built | — | 15.7 s on 32 threads, 28.5 s on 8 (8 passes); 1.7 GB of its own at most; 537 MB on disk |
| Position search | ≤ 1 s | 13 to 26 ms median by depth, 0.2 s at most; on 8 threads 27 to 61 ms, 0.6 s at most (240 positions of random games); without the signatures 1.3 s (3.3 s on 8) |
| Material and pattern search | ≤ 4 s | material 0.3 to 0.7 s, patterns 0.25 to 1.34 s (every game replayed whole); on 8 threads 0.6 to 3.5 s |
| A search cancelled | ≤ 50 ms | 0.2 ms |
| A tournament beside an import | no loss on time | with the import's place among the tasks (#13) |

- On disk, the 11.7 M games take 2.16 GB (184 bytes a game); the commercial program's own files take 12.15 GB.
- Memory, opened: the list of all games holds 124 MB of the process's own memory, at most 359 MB after a sort; the
  pages of the mapped files count besides, and the system takes them back when it needs them. To do with the game
  list (#13): a list of all games needs no ids, and four-byte ids would halve a sorted one.
- Memory, importing: at most 1.5 GB on 8 threads, 1.95 GB on 32 (14.4 s); commits of 131 072 games instead of
  262 144 save 240 MB and double the segments.
- The Lichess month: 0.4 s, 38.5 MB on disk, 5 470 names.
- The opening tree keeps 15.01 M positions of the 11.7 M games (60 plies): 5.1 M that games share by the same moves
  (1.55 M of them by several orders), 9.9 M that only games' own moves share. Followed to 40 plies it keeps 14.77 M,
  to 80 plies 15.05 M; what grows is the work of the build (258 M, 446 M, 578 M positions of single games' moves).

### What the benchmark changed

- Lichess's `.zst` files begin with a skippable frame, so they were read as text: 20 067 damaged "games" instead of
  121 332. A skippable frame now marks a zstd file.
- Duplicates within one append were looked for in quadratic time, and the known keys sorted again at each commit:
  now sorted once, then merged.
- A name was looked for in every segment, and filters read whole rows, names included: now a binary search, and
  filters read the numbers only.
- Decoding counted the side's codes again at each ply: the counts are now kept as the pieces move (8.7 to 36.6 M plies
  a second).
- Lichess's sites, each game's own address, added a name for each game (126 802 names for 121 332 games): kept in the
  annex now.
- A program killed in a commit left its new manifest, not renamed yet, and a database killed while it was being made
  could be neither opened nor made again: both are now cleared.
- The import's memory grew with the machine's cores: now bounded to 24 pieces in flight, those being read included.
