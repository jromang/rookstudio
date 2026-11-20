# Lichess's puzzles kept offline

Lichess publishes every month the puzzles of its site as a database (`lichess_db_puzzle.csv.zst` on
database.lichess.org, CC0). RookStudio downloads it when the user asks, reads it while it arrives into a store of its
own in the profile, and gives puzzles from it without a network, by theme, opening and rating (#80, mockup M12,
board 1). The code: `study/src/study/puzzles.*` (the file's lines, the store, its build), `study/src/study/glicko2.*`
(the RookStudio rating), `gui/src/puzzles.*` (the preparation, the user's puzzles, a puzzle's moves judged),
`gui/src/lichess_puzzles.*` (the batches online), `gui/src/view_training_page.cpp` and `gui/src/view_puzzles.cpp`
(the Training page, a series on the Board page).

## The file, measured

The file of 1 November 2026 (downloaded on 20 November), read whole by a measuring script first, then by
`fcg-study-bench puzzles <file> <folder>`:

| | |
|---|---|
| The file | 309,751,173 bytes, 36 zstd frames (pzstd's, each after a skippable frame), 1.18 GB of text |
| Columns | `PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,GameUrl,OpeningTags,DailyDate` |
| Puzzles | 6,193,530 lines, 190 bytes on average; every id five letters and digits (62 of them) |
| Moves | the opponent's move then the solution: 2 plies 14.9 %, 4 50.6 %, 6 26.3 %, 8 5.9 %, 10 1.6 %, 12 to 34 0.8 % |
| Men on the board | 3 to 32, half of the puzzles 19 or fewer |
| Ratings | 399 to 3330, median 1411; deviations 40 to 500, 93 % under 110 |
| Popularity | -100 to 100: 55 puzzles under 0, 83 % at 80 or more |
| Themes | 73 keys, 4.8 a puzzle (29.5 M in all): `endgame` 3.1 M, `short` 3.1 M, `middlegame` 2.8 M … `underPromotion` 1,139 |
| Openings | 156 families and 1,437 variations, on the 1,231,709 puzzles that start before move 20 |
| Games | every `GameUrl` is `https://lichess.org/game/<8 letters>[/black]#<ply>`; its colour is the FEN's side to move and its ply the one after the opponent's move (checked on every line): the game's id alone is kept |
| Castlings, en passant | 658,229 FENs with castling rights, 4,907 with an en passant square |

## What is kept, and how

A puzzle takes 44 bytes on average, in a record of its own length:

- its id (32 bits: base 62), its rating (16), its deviation (8, up to 255: 99.5 % of them exact), its popularity
  (8), its game's id (48: base 62);
- its position: the side to move, the castling rights and an en passant file (a byte, and one more for the file), the
  half-move clock and the full-move number, the occupied squares (64 bits) and the men on them in square order, a
  nibble each (`fenOf` writes the FEN back as the file had it);
- its moves, a byte each: the databases' one-byte codes (`scan_board.hpp`), on the positions they are played from,
  two bytes for a position of more than 256 codes (nine queens of a side);
- the numbers of its openings (16 bits each).

The records are sorted by rating, then by id: a rating window is a range of records, found in a table of the first
record of each rating (4,097 words). A table of where each block of 64 records starts gives a record in one lookup and
63 lengths at most. Each theme and each opening has a list of its records, in order: every 128th record a skip (its
number and where the next gaps start), the others as the gap from the one before, a varint (30.5 M records listed in
all, about 1 byte each). A puzzle's themes are read back from the lists (73 binary searches), so a record does not keep
them.

The store (`puzzles-<n>.bin`, a header of 128 bytes with its counts, the database's date and where each part starts)
is 307,628,754 bytes: 49.7 bytes a puzzle, records and lists, the size of the file downloaded. The other forms weighed:
the FEN as text (54 bytes a puzzle for the FEN alone); themes in the records as a bit set (10 bytes more a puzzle);
SQLite (the text, its index and its B-tree, several times the size); blocks of records compressed with zstd (RookStudio
builds zstd's decoder only).

## The preparation

The file is read while it downloads (or from the disk) through `readZstd` (`gui/src/zstd_reading.hpp`, as #58 and
#72): its place is saved at a frame's end every few seconds, and a reading cut short (Cancel, the network, the program
closed) goes on from there.

1. Each line gives a puzzle (`readPuzzleLine`, then its FEN read and each move coded on the position it is played
   from; a line whose FEN or moves do not read is left out and counted).
2. Each record is written to the build's file of its hundred rating points (`build/bucket-<n>.bin`, 41 of them),
   with its themes after it; the files' lengths and the names met so far are the reading's state at a frame's end.
3. At the file's end each bucket is read in memory (the largest, 1400 to 1499, under 20 MB), sorted by rating then id,
   and written to the store; the lists are made as the records go out. The store is written aside
   (`puzzles-<n>.bin.tmp`), then renamed; the buckets are removed.

A crash at any moment leaves the store as it was, or the new one whole. An update (next month's file) leaves the store
answering until the new one replaces it.

## Speed and room, measured

On an AMD Ryzen 9 9950X (16 cores), an NVMe disk, Linux, on 20 November 2026, with other work beside it (a load of 28
to 30 on the 32 hardware threads):

| | |
|---|---|
| Read from the disk and prepared, one thread | 16.8 s reading (decoding, the lines, the positions, the moves), 1.2 s sorting and writing |
| Memory | 65 MB at most (the largest bucket and the lists while they are written) |
| On the disk while it prepares | the buckets (about the store's size) beside the store kept |
| A pick of 20 puzzles (a theme, a rating, both at random) | cold median 1.4 ms, 90 % within 3 ms, the slowest 10 ms; warm median 1.3 ms |
| A puzzle read (its record, its moves, its themes) | 0.02 to 0.03 ms |
| Compared with their lines | a puzzle in a thousand (6,194): all the same, FEN, moves, rating, deviation, popularity, themes, openings, game's address |
| Under AddressSanitizer and UBSan | the whole file read and the store made and read without a report (61 s reading) |

- In the program, from a local stand-in for database.lichess.org serving the real file at 15 MB/s: cancelled at 210 MB
  (4,412,284 puzzles read), the program closed, then *Go on* from the frame's end (a Range request): the store came out
  byte for byte the one of the bench, its header's date apart.

## The RookStudio rating

Glicko-2 (`study/glicko2.hpp`), Lichess's system constant (0.75): each offline puzzle is a game against the puzzle's
rating and deviation, won when solved at the first try without a hint; the days since the last puzzle widen the
deviation first (Lichess's 0.21436 rating periods a day). The deviation stays within 45 and 350. It starts from the
linked account's puzzle rating and deviation (`/api/account`'s `perfs.puzzle`, read with the link's token, kept a day
in the profile like the rest of the account), else at 1500 with a new player's deviation. Lichess's own rating moves
only with its puzzles online (`lichess_puzzles.hpp`); nothing documented takes an offline puzzle's result.

## The user's puzzles

In the profile's `puzzles` folder, which removing the database leaves alone: `rating.json`, `results.jsonl` (a line per
puzzle answered: the puzzle, when, right or not, the ratings before and after, its themes, the time, online or a review)
and `review.json` (the puzzles missed, each with its FSRS card and the puzzle itself, so that the day's review needs no
store). A missed puzzle is graded Again and comes back once the training day ends (4 a.m.); in the review, right
Good, after a hint Hard, wrong Again (the time does not count, unlike a repertoire's move: a puzzle of several moves
takes its time). `online.json` keeps the batch online and its answers until Lichess took them.
