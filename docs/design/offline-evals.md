# Lichess's evaluations kept offline

Lichess publishes every month the evaluations its servers computed for its players' analyses
(`lichess_db_eval.jsonl.zst` on database.lichess.org, CC0). RookStudio keeps of it, on the disk, the evaluation, the
depth and the first move of each position's deepest search, read in place by the Analysis tab, the review of a game
and the Tree tab (#58, mockup M11, board 6). The code: `study/src/study/evals_index.*` (the file's lines, the index,
its build), `gui/src/offline_evals.*` (the preparation, what answers), `gui/src/view_lichess_evals.cpp` (the card,
the Analysis tab's line).

## The file, measured

`fcg-study-bench evals <file> <folder>` reads the whole file as the program does and makes the index; its figures on
the file of 1 November 2026 (downloaded on 19 November):

| | |
|---|---|
| The file | 22 499 667 845 bytes compressed, 115.0 GB of text, 6 854 zstd frames (pzstd's, each after a skippable frame) |
| Positions | 417 862 375 lines, one position each; every one read by the reader below, none needing the JSON parser |
| Searches per position | 1: 91.6 %, 2: 6.6 %, 3: 1.5 %, 4: 0.3 %, 5 or more: 0.03 % (461.9 M in all) |
| Lines of the deepest search | 1: 59.6 %, 2: 8.7 %, 3: 16.6 %, 4: 2.7 %, 5: 12.3 % |
| Depth of the deepest search | median 24, mean 32.3; under 20: 5.6 %, 20–24: 49.8 %, 25–29: 21.3 %, 30–39: 10.7 %, 40 and more: 12.7 % |
| Depth 30 or more | 23.4 % of the positions; 35 or more 15.6 %, 50 or more 8.0 % |
| Mates | 12.4 % |
| Men on the board | 24 or more: 40.8 %; 28 or more: 21.7 %; 7 or fewer: 8.6 % (the deepest: endings searched to depth 60 and more) |
| FENs | four fields; 697 893 with an en passant square, 941 with castling rights other than KQkq (Chess960's letters, read as chess.hpp reads them), 64 481 boards of more than 32 men (set up by hand) |

## What is kept, and how

For each position, 9 bytes: the key's bits that its bucket does not say (32), its score (16: centipawns, White's, or
a mate as ±(32000 − moves)), its depth (8) and its move (16: from, to, promotion).

- **The position** a key is made from: the occupied squares (64 bits), the men on them in square order (a nibble
  each), the side to move and the castling rights (`EvalsPosition`, 25 bytes for 32 men, 41 for a board of 64 set up by
  hand: the file has some 15 000 positions of more than 32 men). Not the en passant square:
  chess.hpp keeps the square of a FEN and that of a move by two rules that do not agree
  (`rnb1k2r/p2pp1b1/5nN1/1pp1PK1p/…` after d7-d5: the move keeps d6; the FEN, rightly, does not, d7-d5 uncovering the
  c8 bishop's check), and Lichess writes it by a third. A position differing by that square alone shares the key; a
  best move that is not legal on the board asked about is not given. A FEN's position is read without building a
  board (the castling rights checked where the king and rook stand, chess.hpp asked otherwise): 4.3 M of them were
  compared with chess.hpp's boards, none differing.
- **The key** is SipHash-2-4 of the position under a secret drawn at random for each preparation, kept in its runs'
  and index's headers. Polyglot's Zobrist keys came first, and failed on the file: it holds 19 positions made to share
  them with two others, side by side (lines 169 151 641 to 169 175 945): 17 with the start position's key, 2 with the
  key of the position after 1. e4, boards crowded with queens and rooks, most of them mates searched deep. Kept by
  the deepest evaluation of a key, one of them took the start position's place (a mate in 5, depth 85, its move
  f7f8). Polyglot's keys are public; a secret's keys cannot be aimed at.
- **The index** (`index-<n>.bin`) is the positions sorted by key, in buckets of the key's first bits (24 for the whole
  file: 16.8 M buckets of about 25 positions), a table of where each bucket starts at the end, a header of 96 bytes
  (its counts, the database's date, the secret). A lookup reads a word of the table, then a binary search in the
  bucket's records: two pages of the disk at first, then memory. 32 bits after 24 bucket bits make a key of 56 bits: a
  position the index does not have is taken for one it has once in 45 million lookups, and the move's legality check
  catches most of those.
- Not kept: the other searches and lines. Every line of every search is 1 006 676 609 lines of 9.75 moves (9.8 G
  moves; 836.5 M lines in the deepest searches): at a byte a move (a move's code), three bytes a line and five a
  search, about 17.7 GB, 4.6 times the index, for what an engine finds again in a second.

The other formats weighed: a hash table (no order, so no merge of runs, and a third of its slots empty); the whole
64-bit key beside each record (13 bytes a position: 5.4 GB); SQLite (a B-tree with its keys, several times the
size). The sorted file is the smallest of those, needs no memory of its own (the system keeps the pages read), and is
made by merging sorted runs, which is what lets a preparation answer while it goes on.

### The openings only

The file's FENs carry no move number, so "up to move 20" is told by the men on the board: 24 or more
(`EVALS_OPENING_MEN`). Measured on a month of Lichess's games (January 2013, CC0, 121 332 games, 8.2 M positions):
at move 20, 23 men at the median; 24 men or more is 89.4 % of the positions of the first 20 moves and 7.6 % of the
later ones (22: 96.0 % and 16.4 %; 26: 78.7 % and 2.7 %; 28: 65.0 % and 0.7 %). In the evaluations' file, 40.8 % of
the positions.

## The preparation

The file is read while it downloads (or from the disk) through `readZstd` (`gui/src/zstd_reading.hpp`, as #53 and
#72): its place is saved at a frame's end every few seconds, and a reading cut short goes on from there.

1. Each line gives a position (`readEvalsLine`, a reader of Lichess's own layout without a JSON parser, which falls
   back on one for another layout).
2. The positions gather in memory: 8 M of them (128 MB), then they are sorted into a run (`build/run-<id>.bin`, 12
   bytes a position and a table of 65 536 buckets), written aside and renamed.
3. The positions since the last run are written to a log (`build/log-<id>.bin`) before each place is saved, so that
   a reading cut short loses at most a few seconds of the file.
4. At the file's end the runs are merged into the index, written aside (`index-<n>.bin.tmp`) and renamed; the runs
   and logs are removed.

The reading's state (`build/reading.json`) names the runs and the log, and how many positions of the log count:
going on, the runs it does not name and the log's tail are removed (written after the place saved), and a log is
removed only once a later state is saved. A crash at any moment leaves the index as it was, or the new one whole:
the files being written are not named yet, and the program's start removes them.

While it prepares, the runs written so far answer, until the index replaces them; an update (the next month's file)
leaves the index answering until the new one is ready.

## Speed and room, measured

On an AMD Ryzen 9 9950X (16 cores), an NVMe disk, Linux, on 20 November 2026, with other work beside it (the
program preparing the same file during the first run):

| | Every position | The openings only |
|---|---|---|
| Read from the disk and prepared | 7 min 37 s: 7 min 20 s reading (decoding, the lines, the keys, the runs: 55 s of it writing them), 17 s merging | 6 min 29 s: 6 min 16 s reading (18 s writing runs), 13 s merging |
| The index | 3 827 631 623 bytes (3.83 GB, 9.16 bytes a position), 417 835 851 positions (24 246 merged: positions that differ by an en passant square alone) | 1 569 190 836 bytes (1.57 GB, 9.20 bytes a position), 170 626 256 positions |
| On the disk while it prepares | 8.81 GB at most (49 runs, 4.98 GB, beside the index) | 3.59 GB at most (20 runs) |
| Memory | 128 MB of positions waiting, 64 MB of the index's table while it merges, the runs read through buffers | the same, a table of 32 MB |
| Lookup, cold (its pages dropped from the cache) | median 118 µs, 90 % within 143 µs, 99 % within 0.78 ms (another run: median 85 µs) | median 84 µs, 99 % within 0.27 ms |
| Lookup, warm | median 0.39 µs (a position not kept: 0.35 µs) | median 0.37 µs |
| Lookup in the runs, while it prepares | cold 3.4 ms (a page in each of 49 runs) | cold 1.3 ms, warm 3.2 µs (20 runs) |

- From database.lichess.org, the download sets the pace: the preparation keeps up with 50 to 68 MB/s.
- The index is read with `POSIX_MADV_RANDOM`: without it, the system read 128 KB around each page asked for, and
  20 000 lookups brought most of the file into memory.
- A run's table of buckets is kept in memory (256 KB a run): a lookup in a run reads one page, not two.
- While a preparation goes on, the program spends at most 6 ms a frame on lookups that read the disk: the Tree tab's
  column fills over a few frames rather than holding one.
- In the program, the same file read from a local stand-in for database.lichess.org (a cut of the network
  included, gone on with by itself) gave the same index, byte for byte the same size: 417 835 851 positions.
