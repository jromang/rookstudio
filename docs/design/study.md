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
  manifest.json   {"format":"rookstudio-database","version":1,"generation":N,"segments":[{name,games,names,source}],
                   "edits":"edits-NNNNNN.bin","players":"players-NNNNNN.bin"}
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
in their segments until the database is compacted (see The compaction). A crash at any step of a replacement leaves the game
as it was, or as it is to be, as the tests check.

### Duplicates

A game's key is a hash of its event, site, round, players, date, start position, moves and result. An append leaves out
a game whose key the database has, or a game before it in the same append, unless asked to keep them. Two rounds of a
tournament with the same moves are two games.

### Sources

Each segment an import writes keeps the file it came from (`"source"` in its manifest entry): `Snapshot::source()` gives
a game's, also once it is replaced. The maintenance names a game's source with it ("TWIC 1672", "Lichess broadcasts
2026-10", "club.pgn"; else from its tags: "Lichess broadcast"), tells where duplicates come from, and imports a damaged
game's file again (`importInto`'s `forget`: the keys of the damaged games do not count as games the database has).

### The table of names

A player written several ways ("Lindqvist, Erik", "Lindqvist,E", "Erik Lindqvist") is one player of the database's
table of names (`players.hpp`): its name as everything shows it (FIDE's list's, or a spelling chosen), its FIDE id, its
card from FIDE's list when it was matched with it (federation, title, birth year, ratings, the list's month), and the
name ids it gathers; a name id is in one player at most. The games are not changed. The table is a file named in the
manifest (`players-NNNNNN.bin`: "RSPLAYR1", the pairs of games told apart by hand, then each player), written and
synced before the manifest that names it, the rename of the manifest still the only commit; the edits generation does
not change, so the opening tree stays valid. A snapshot holds its table (`Snapshot::players()`, never null):

- `Snapshot::shownName(id)` is the name of a name id's player, else the name as written: the game list, the opening
  tree's reference games and the sources' reports show it;
- the filters and sorts fold a name with its player's name (a word of either finds the game) and rank the spellings of
  one player together (`query.cpp`'s name index, kept for a names count and a table);
- a player's dossier (#90) finds its games by the name ids of its player (`PlayerTable::players()[p].spellings`, the
  White and Black columns of `Snapshot::rowIds()`), its identity in the player's FIDE fields; a tournament's crosstable
  (#92) counts each player of an event once, by `PlayerTable::playerOf()` of the names of its games, its rating from the
  table when the games give none.

Measured: a table of about 270 000 players (the reference database's players matched with FIDE's list) takes 15.7 MB,
made in 254 ms, written in 15 ms, read in 64 ms.

An import's new spellings join a player of the table by themselves (`joinNewSpellings`, in the import's background
job) when their games give the player's FIDE id, or write its surname and given name in full; an initial alone waits
for the user.

### Duplicates from other sources

The same game can come from two sources with its headers written otherwise (a Lichess broadcast, then TWIC): their keys
differ, so the import keeps both. `findDuplicates` puts each game in a bucket of its players (the name the table of
names gives, else its surname and first initial: `nameParts()`) and its year, sorts each bucket by start and moves, and
compares each game with the games that follow it while their moves begin with its own: one game when the moves are the
same, or those of one the start of the other's and that one unfinished ('*', a broadcast cut short), the rounds
compatible ("9" and "9.2", or one not known), no part of the dates known in both that differs, and ten moves at least
(twenty when a day is not known); two games with an address of their own (Lichess's) differ, and the pairs told apart
by hand (the table's) are left out. A group is the same moves, one shorter, or results that differ.

On the reference database (11.7 M games, 6 threads), 1.6 s: one group, a game of two rounds' numbers ("1", "1.1"). The
rules came from it: games with no moves (results only) and short prefixes matched games of other events, hundreds of
them, before the ten moves and the unfinished prefix. On the month of Lichess games, 0.03 s and no group (games of
their own addresses).

`mergeGames` makes a group's game, the fullest of each field (`fullestChoice`, or the user's choice field by field): a
name in full over an initial, the longer event, a place over an address for the site (the address kept as GameURL), the
round that says more, the date that knows more, both ratings, the ECO code, the FIDE ids, the longer moves (an annotated
game's for the same moves, its text's tags and result written again), the most clocks and evaluations, a result over
none; the other tags of every game, and a MergedFrom tag that says which game each field came from.
`Database::merge` commits the groups in one manifest: each kept game (the oldest) replaced by its merged version in a
segment of versions, the others deleted; it gives the games' states before (`GameState`: version and deleted), which
`Database::restore` puts back in one commit (Ctrl+Z).

### The check

`Database::check` reads every segment again from the disk (a new mapping) and replays every game the database reads (its
last version, not deleted): its names within the database's, its offsets within its files, its annex whole, its moves
decoded to their end, its signature made again and compared, and its key (a hash of its headers and moves) made again
from the game read and compared with the key stored. The key is what tells a byte changed in the moves or the headers. A
segment that cannot be read again counts all its games. On the reference database (11.7 M games of the first format,
without signatures), 11.4 s on 6 threads, no game damaged: the key read back is the key written; on the month of Lichess
games, 0.1 s.

### The compaction

Games deleted and versions replaced keep their room in the files. `Database::compact` writes again the segments that
hold a replaced game or a deleted one not emptied yet, each game in its place: its last version's record copied (a
deleted game's headers alone, an empty game); the segments of versions become segments of their names only (0 games),
since the name ids after them count them. The numbers, the names, the keys and the deleted games do not change, nor
the edits generation: the opening tree stays valid. The new segments are written from the snapshot of when it starts,
without the writers' lock; the commit takes it, rereads the manifest (segments added meanwhile kept) and stops when a
game was replaced or deleted meanwhile. The old files are kept until the database is closed (`undoCompaction()` puts
the manifest before back while nothing was committed after); one segment at a time, each step commits and removes its
old files at once, needing the room of one segment. The tests stop it at each of its steps: the database reopens with
the games as they were, compacted or not.

### Views

A view is the list of a snapshot's game ids that pass a filter, in the order of a column: the game list pages through
it with no query per page. A filter folds each name once (letter case and Latin accents left out) into a bit for each
word it looks for, then checks each game in a few operations on its columns, on all cores. A sort reads one column,
names by their rank in alphabetical order, and sorts it four bytes at a time (stable; descending without reversing
the games of one key).

## FIDE's rating list

FIDE's combined list (players_list_foa.txt in players_list.zip): a header naming its columns, then a line of fixed
columns a player. `prepareFideList` finds each column by its name in the header (a list whose columns differ is refused:
`FideFormatError`), reads the lines (a value of the other titles longer than its column pushes the columns after it: 3
lines of the list of November 2026), and writes RookStudio's file: a header, the federations, 24 bytes a player sorted by
FIDE id (id, the place of its name, birth year, three ratings, federation, title, woman, inactive, and since #92 the K of
each rating, a crosstable's rating change reading them; a file of the first version, 20 bytes a player, is read, its K
not known), the names, and two indexes of the players by surname and by its skeleton (`nameSkeleton`: Kowalenko and
Kovalenko alike), searched by halves. `analyseNames` (`names.hpp`) groups a database's spellings (the table's players, the FIDE ids their games give,
then surname and given name, an initial joining the one name that starts with it) and matches each group with the list:
a single player of that surname whose given name fits (its transliterations when the games' ratings are within 150 of
the player's; an initial alone needs a rating within 200), never a player born after the games or less than five years
before them, nor a name of one word on either side; several are left to confirm, the nearest to the games' ratings
first.

Measured on the list of November 2026 (downloaded once, on this machine only): 1 960 520 players, 800 241 rated
(standard 573 475, rapid 486 148, blitz 321 078); read in 3.1 s, 476 MB at most, into 89.3 MB; a lookup by surname 10
to 50 µs. The reference database's spellings (about 480 000) matched in 19 s (about 270 000 players at once, 33 000 to
confirm); the month of Lichess games' 4 879 accounts are names of one word, all kept as they are.

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
of the games as they come; a cancel stops it between two games. The first three queries:

- a position exactly (men, side to move, castling rights, en passant): the signature's plies, then a replay up to them;
- material: counts within bounds, for a number of plies in a row (an exchange under way is not a material);
- a pattern: men on squares, empty squares, and a side's pawns exactly those given.

The tests compare each with a replay of every ply of every game, signatures unused.

### Patterns of the language, routes and structures (#89)

The search panel's advanced searches are queries of the same `Search`, each game replayed when its signature cannot
leave it out:

- a pattern of the language (`patterns.hpp`, `RelationQuery`): its first position's conditions, then a line of steps;
- a man's route (`ManoeuvreQuery`): a man of a kind going from its first square to the next, then on to each in turn,
  the same man or any of its kind, other moves of its side between or not, within so many moves, from anywhere or once
  a game reaches a position (the position search first);
- a pawn structure (`StructureQuery`): the pawns of both sides or of one, exactly or one or two pawns away, the colours
  reversed too, held so many plies in a row.

**The language** is our own with CQL's words, read from a text and written back to it (`readPattern`, `writePattern`):
men in FEN letters with squares (`Nf3`, `[qr]`, `p[a-h7]`, `_f6`), `attacks`, `defends`, `pin from … through … to`,
`en prise`, `passed`, `check`, `mate`, `stalemate`, `wtm`, `btm`, `not`, `or`, `only P`, `piece $n in N` for a name
(two names, two men), `line -->` for steps a ply after another (`move from … to … capture … promote …` and what holds
after it, or a position; `.{m,n}` for plies of anything before a step). A text that does not read gives its first
error with its line, the word it stops at and the word of the language it looks like (Damerau–Levenshtein, one letter
off for a short word, two for a longer); a word of CQL that the language does not read is told as such. A pattern read
is put in the order it is written in (`canonicalize`), so that reading what was written gives the same pattern: the
search panel's builder edits a `Pattern` and writes its text, the text read gives the builder its rows.
`contradiction()` finds two conditions of the first position that cannot both hold (a man on a square and the square
empty, both sides to move, mate and not check), told before any search.

`PatternMatcher` evaluates on the `ScanBoard` of each ply: the conditions without names first, the cheapest first (the
side to move, men on squares, then attacks, pins, en prise; mate and stalemate only at a game's last position, where
`ScanBoard::hasLegalMove()` tells them), then every binding of the names to their men. A line keeps the first
positions that fit with their bindings and the game's moves, then follows each in order, the names' men followed
through the moves (taken: gone). The men a pattern needs on their squares give the signature window of the men's
patterns (`patternWindow`). A route follows the men that set off along it; it gathers where the man went next and the
routes that part from it after its first move, each once a game. A structure is a distance a side (the larger of the
pawns missing and the pawns added: a pawn on another square is one), summed for both sides, the reversed colours the
board turned and the sides swapped; the signature bounds it at each ply from below (a pawn of the structure on its
starting square gone, a starting square it leaves empty still held, the end's pawns), so that only the plies it can
hold are replayed. Its statistics: the games held exactly, the ECO codes, the variants one pawn away with their
results.

A search gathers these numbers part by part (`SearchStats`), gives its hits as they come, in order (`hitsFrom`: the
list shows the first games found while the search goes on), and a search cancelled is resumed from the parts it
searched whole (`Search(…, resume)`). The tests compare every search, the statistics too, with a brute force written
apart, square by square on chess.hpp's boards and legal moves, signatures unused, over random games and games of our
own that hold each pattern; and a search cancelled then resumed with a whole one. The reader's tests read every word
and every error, and 20 000 texts mutated at random: none breaks it, and each one that reads is written and read back
the same.

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

## Repertoire

A repertoire is a game and its variations (a PGN the Board page edits like any game), the lines of one colour.
`Repertoire` finds its positions by their Polyglot keys, so that two move orders that reach one position give it the
moves of both, and keeps where its lines first reach each one: a move marked in the opening tree is added there
(`addMove`), whatever order the board came by.

`coverage` follows its moves from the start and, at each position where the opponent moves, weighs the moves the
databases' games played there (their trees summed, `treeMoves`): those it answers, and those played often enough (10
games and 2 % of the position's, by default) that it does not, its gaps. The end of a line after the opponent's move
is a gap when the games go on from there; after one of its own moves, it is where its owner stopped preparing, unless a
depth is asked for. Its own choices count alike, the opponent's moves as often as the games played them: how often a
game along the repertoire comes to each gap, by which the gaps are listed.

Measured on the reference database's tree: a White repertoire of 84 positions, covering 57.9 % of the games along it,
42 gaps, in 137 ms the first time (the tree's pages read from the disk) and 0.4 ms after.

## A player's dossier (#90)

`makeDossier` (`dossier.hpp`) gathers a player's games from several databases at once, made again each time it is
opened: nothing of it is kept. `findSubject` finds their name ids in a snapshot through its table of names: the
spellings of the player with their FIDE id, of the players and the names written as their name (case and accents
aside); a player of that name with another FIDE id is a namesake, whose games count only when they give the subject's
FIDE id. Every game of each snapshot is then read for its two name ids (a byte a name id says whether it is theirs, on
the machine's threads, 16 384 games at a time), and each of theirs is replayed once, up to its 60th ply: a game that
gives another FIDE id for their side is a namesake's, one played before they were five is too; its opening is the last
named position of its first 40 plies (Lichess's chess-openings, keyed by Polyglot keys: `OpeningNames`), its pawns at
the 40th ply are its structure, and each position of its first 60 plies where it first comes, with the move it played
there, goes into the dossier's own tree, a vector sorted by key for each colour: the Tree tab's source of their games
alone, and the moves the repertoires' coverage reads (`Dossier::moves`), with no database tree needed. What they say
(`dossierFigures`): lines by ECO code, each named by the code's shortest line; results against stronger and weaker
opponents (both Elo known); the mean of their Elo in each year, their classical games when the year has some; the
structures their caller names (the GUI passes the assistant's formations), whose and with which colour. A Lichess
account's database is a source of its own (`online`), each figure of one part or of both.

The window's side (`gui/src/dossier.hpp`) finds who the player is (the tables' FIDE identity, else FIDE's rating list:
one player of that name is theirs, several are a question, the nearest to their games' ratings first), writes a choice
kept in the tables (`withPlayer`: a namesake chosen, a Lichess account linked, the table's second version keeping the
account), reads the reviews the profile keeps for their games (each game's review key made from its codes, without its
PGN) and measures the user's repertoires against them, as #52 did.

Measured on 2026-11-23 on the reference database (11.7 M games, 561 343 names), at the lowest priority beside other work
that kept about 28 of the 32 threads busy (`fcg-study-bench dossier`), for the players with the most games, by rank:

| Player | Games | Their spellings | Their games, 32 threads | 4 threads | 1 thread | Figures |
|---|---|---|---|---|---|---|
| rank 1 | 11 552 | 14 ms | 116 ms | 45 ms | 84 ms | 0.3 ms |
| rank 10 | 6 482 | 12 ms | 86 ms | 60 ms | 160 ms | 1.4 ms |
| rank 100 | 4 014 | 13 ms | 36 ms | 44 ms | 114 ms | 1.1 ms |
| rank 1 000 | 1 740 | 13 ms | 20 ms | 31 ms | 104 ms | 0.6 ms |

The scan of every game's players takes most of it (about 80 ms on one thread for 11.7 M games), the replay of theirs
the rest; the threads help little on a busy machine. The tree of their games holds up to 374 205 positions (the player
of rank 10, 6 482 games); the bench's process held 76 MB after its twelve dossiers. The mockup expected a few seconds:
a tenth of a second, the reviews and the repertoires' gaps added in the program (a few milliseconds more each).

## A tournament's crosstable (#92)

`listTournaments` (`crosstable.hpp`) lists a database's tournaments: the event names written (a bit a name, on the
machine's threads) read once into keys (folded, their years and punctuation left out: "Autumn Open 2026" and "Autumn
Open" are one event), each game counted into the group of its event and year (the group of the last game asked first:
a tournament's games come one after another), an event's games without a date joined to its only year and a group of
December to one of January following it within a month joined to it, then each group's game numbers gathered in one
array and the groups read on the machine's threads. A group's sites are told apart when their players are (a site joins
another through a player of both: two events of one name in two places), its games that came twice counted once (the
same players' surnames and initials with the same colours, rounds and dates that can be one, and their rounds told or
their lengths the same; a pair's games by the hundred, online or an engines' match, compared with their sixteen
neighbours only), its players read through the table of names. A group is a tournament with three players or more and
its rounds told (or each having met each): online games are none. `tournamentShape` guesses its system from how often
its pairs met: nine pairs in ten or more, a round robin of the cycles most pairs met; half of them in as many rounds as a
round robin needs, one with games missing (not sure); fewer rounds than a round robin needs, a Swiss; games of one pair
within one round, a knockout; games that give teams, a team event. What a Swiss expects: in each round, half its players
between their first round and their last.

`makeCrosstable` computes a crosstable from players and games of its own (a database's from `tournamentInput`, a
broadcast's from the window's games followed): each player's games and rounds (a Swiss's rounds without a game read as a
bye when one player alone of an odd field misses it, as a game missing when two who never met miss it, as an absence
not known otherwise; two names of one player found first, so that the second's first round is no bye), the points, the
tie-breaks of C.07 (2026) with its rules for unplayed rounds (article 16: the scores adjusted for the opponents, the
dummy of the player's own score capped, a voluntary unplayed round cut first), the direct encounter applied to each tie
and again to the smaller ties it leaves (6.2; in a Swiss, one alone at the top whatever the missing games give, 6.3),
the performance (C.07 10.2 with FIDE's table of B.02 8.1.1) and the rating change (B.02 8.3: the table of 8.1.2, the 400
points' rule below 2650 since October 2025, K from FIDE's list, now kept in its file, when the player is matched with it,
else its rules). A result not in the data is counted "?" in what reads it.

Measured on 2026-11-23 on the reference database (11.7 M games, 561 343 names), at the lowest priority beside other work
that kept about 20 of the 32 threads busy (`fcg-study-bench crosstables`): its 89 934 tournaments listed in 0.63 s on 32
threads, 1.2 s on 4, 2.9 s on one; 662 424 games in none (online games, games without an event). They are 22 015 round
robins, 2 175 double ones and 94 of more cycles, 64 131 Swiss tournaments, 1 519 knockouts; 1 998 have a system not
sure, 48 474 games missing (opens whose top boards alone were kept, mostly), 569 games come twice. Its largest Swiss, 968
players over 11 rounds, 3 933 of 3 948 games: found in 76 ms (every game's event read), read in 3.2 ms, computed in 3.9
ms; its largest round robin, a double one of 22 players, 462 games: 78 ms, 0.2 ms and 0.3 ms. The bench's process held
100 MB after the first list (the list of 89 934 tournaments some 25 MB of it). The first measure listed them in 1.7 s on
32 threads and 13 s on one: the tournaments' last years were looked for among all the groups, for each, and each thread
kept its names' keys of its own; both done once now. The mockup expected a few milliseconds ("it is a sort"): so they
are for a database of a few thousand games, a large one's list is made in the background with its progress (R18).

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
build/fcg-study-bench advanced <database>                      # patterns of the language, lines, routes, structures
build/fcg-study-bench evals <lichess_db_eval.jsonl.zst> <folder> [--min-men N]  # Lichess's evaluations kept offline
build/fcg-study-bench evals-lookup <folder>                    # their lookups again, cold then warm
build/fcg-study-bench maintenance <database>                   # its check, duplicates and players' names, timed
build/fcg-study-bench compact <database>                       # compacted in place: work on a copy
build/fcg-study-bench fide <players_list.zip> <list.bin>       # FIDE's list read into RookStudio's file
build/fcg-study-bench fide-names <database> <list.bin>         # the database's players matched with the list
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
| Pattern of the language, sequence, route, structure (#89) | — | see below: 0.5 to 5.3 s on 32 threads, 1.7 to 13.4 s on 8 |

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
- The advanced searches (#89), measured on 2026-11-23 on the reference database (11.7 M games, its signatures) at the
  lowest priority, beside other work that kept about 22 of the 32 threads busy (`fcg-study-bench advanced`): each
  search to its end, every game that its signature does not leave out replayed.

  | Search | Games | 32 threads | 8 threads |
  |---|---|---|---|
  | Knight fork (a name, two attacks, Black to move) | 582 105 | 2.8 s | 7.0 s |
  | Pin on the king | 4 537 020 | 2.3 s | 6.2 s |
  | Back-rank mate | 102 063 | 1.8 s | 4.9 s |
  | Queen's double attack (two names, a defence denied) | 771 422 | 4.9 s | 13.2 s |
  | Sacrifice on h7 (seven men on squares, then three moves) | 2 048 | 1.6 s | 4.1 s |
  | Protected passed pawn | 2 204 424 | 3.3 s | 9.0 s |
  | A line: a bishop takes and is taken (every ply a first position) | 7 168 631 | 5.3 s | 13.4 s |
  | A line: a check, then mate within 3 plies | 495 410 | 2.5 s | 7.1 s |
  | Route Nb1-d2-f1-g3, in 15 moves | 164 003 | 1.8 s | 5.0 s |
  | Route Ra1-a3-h3 | 5 523 | 1.8 s | 5.0 s |
  | Carlsbad structure, exactly, 6 plies | 58 253 | 0.55 s | 1.7 s |
  | Carlsbad structure, one pawn away | 213 043 | 0.80 s | 2.2 s |
  | Carlsbad structure, two pawns away | 1 012 437 | 1.0 s | 3.0 s |
  | Carlsbad structure, one pawn away, reversed too | 381 691 | 1.1 s | 3.0 s |

  The mockup expected 4.1 s for a fork, 6.8 s for a sequence, 3.2 s for a route and 2.1 s for a structure on 1.28 M
  games: here 2.8, 1.6, 1.8 and 0.55 s on 11.7 M games and 32 busy threads, about a ninth of that on 1.28 M games. The
  same games are found without the signatures (the database of the first format): the fork as fast (its men can be
  anywhere), the sacrifice on h7 in 2.3 s instead of 1.6, the Carlsbad structure exactly in 2.0 s instead of 0.55
  (3.6 times). A search cancelled after 300 ms stopped in 0.6 ms, 1.3 M games kept; the rest, resumed, in 2.2 s.
  In the program, on a test database of 10.4 M games (the month of Lichess games, its players renamed with invented
  names, imported many times over), the knight fork took 2.6 s while an import into the same database went on.
- A player's dossier (#90), measured on 2026-11-23 on the reference database at the lowest priority beside other
  work (`fcg-study-bench dossier`): a player with 11 552 games in 116 ms on 32 threads (84 ms on one), one with 6 482
  games in 86 ms (160 ms), one with 1 740 in 20 ms (104 ms); their spellings found among the 561 343 names in 14 ms,
  the figures in 1.4 ms at most (see [A player's dossier](#a-players-dossier-90)).
- Lichess's evaluations kept offline (#58), measured on 2026-11-20 on Lichess's file of 1 November 2026 (22.5 GB,
  417 862 375 positions): read and made into the index from the disk in 7 min 37 s, with other work beside; the index
  3.83 GB (9.16 bytes a position), 1.57 GB for the openings only; a lookup 118 µs cold, 0.39 µs warm (median). The
  file's own figures, the format and the rest: `docs/design/offline-evals.md`.

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
- Lichess's evaluations, keyed first by Polyglot's hash: the file holds 19 positions made to share the keys of the start
  position and of 1. e4, and one of them (a mate searched deeper) took the start position's place. A position's key is
  now SipHash-2-4 under a secret drawn for each preparation.
- The evaluations' index, mapped without advice: 20 000 random lookups read most of its 3.8 GB (the system read 128 KB
  around each page). It is now read with `POSIX_MADV_RANDOM`.
- chess.hpp keeps an en passant square after a move that its FEN reader rightly drops (a capture that would leave the
  king in check): a position's key for the evaluations leaves that square out.
- The 64 481 positions of more than 32 men (boards set up by hand) were not read: a position now holds up to 64.
