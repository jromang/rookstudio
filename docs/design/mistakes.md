# Your mistakes as exercises, and guessing the moves

The mistakes of the user's own games come back as exercises, scheduled with the repertoires' spaced repetition in the
day's review; and a game of their databases can be replayed from one side, a move to guess at each turn (#81, mockup
M12, board 2, point 5). The code: `gui/src/mistakes.*` (the user's names, what makes an exercise, an answer judged, the
profile's files, the user's games looked for, a guess scored), `gui/src/view_mistakes.cpp` (the Training page's part,
the games reviewed in the background, a session on the Board page), `gui/src/view_guess.cpp` (guessing), and the key
of "To work on"'s topics (`commentary::WorkOn::Topic::key`).

## Whose games

A game is the user's when one of its players has every word of one of their names, letter case and accents left out,
words split at anything but a letter or a figure (`isUserName()`): *Tester* is *Tester, Sam* and *TESTER, S.*, *Sam
Tester* is *Tester, Sam*, but *Test* is no one. The names are those of Settings › Your names (`settings.json`
`player_names`) and the linked Lichess account's. A game where both players are the user's (two accounts) gives no
side, and nothing. The databases looked in are the user's own, not a tournament's games nor a PGN file read where it is.

## What makes an exercise

From a game's judging review, the moves of the user's side that the review marks **?** and **??**, as the Board page
marks them (`judgedMoves()` of `commentaryReview()`: the commentary's two readings, theory and a tournament's book left
out; theory never hides a mistake, its book ending before the first one), where the best move is clear:

- the review searched the position deep enough to name a best move: its third pass at least (320,000 nodes a position
  for the Chess space's review, 320 ms for one made at a time, depth 14 for one made at a depth); a review of one or
  two passes says too little;
- its best move is known, legal, and not the move played;
- the game was not decided anyway: the best move keeps the user in the game (the mover's expected score at least 10 %,
  the review's "lost anyway"), and the move played did not keep a win (at most 90 %, the review's "still wins").

Inaccuracies (?!) are left out: they seldom have one good answer. An exercise is the position before the move, one card
a position (Polyglot key): the same mistake met in two games is one card, the latest game's shown. Its kind is the topic
of the review's "To work on" that quotes the move (forks, pieces left en prise, rook endgames…), else "others"; the keys
are the same in every language. The review's comment on the mistake, held to the boards (#64), is kept with it in the
language of the interface.

A position a repertoire of the same side trains is not asked here: it belongs to the Openings page's training.

## An answer judged

The review's best move is right without an engine; the move played in the game is wrong (the review marked it); a move
that ends the game is scored as the game's end is. Any other move is searched by the review's engine at the review's
settings: one thread, the review's hash (64 MB), the review's nodes a position (its last complete pass), from an empty
hash (`ucinewgame`), the game's start and moves sent as the review sent them (`Analyser`, as #61's review sets an
engine; a test checks that a position searched this way gives the review's own score and move, the mock engine
remembering its searches as a hash would). When the library no longer has the review's engine, the analysis engine
judges, and the answer says so.

The answer is then weighed as the review weighs a move (`commentary::weigh()`, the position's analysis against the
search after the answer): right when it loses 0.3 pawn at most against the review's best (the review's centipawns, mates
at their cap: a mate in more moves is as good), or when the review would not mark it and it keeps a win (the mover's
expected score 90 % or more). Letting a forced mate go is a blunder to the review, and a wrong answer here.

The grade comes from the answer, as for a repertoire's position (`study::gradeOf()`): right at the first try Good,
right after a hint or after more than 20 s Hard, wrong or "I don't know" Again. The cards are scheduled by the
repertoires' scheduler (FSRS-6, a 90 % retention, one learning and one relearning step of ten minutes): a card back
within the hour is asked again three exercises later in the session.

## In the day's review

The exercises due count in the day's review as their own kind (`ReviewKind::Mistakes`, first in "Review all"), with
the pace of their answers; *Only my mistakes* reviews them alone. The Training page's part asks those due, then today's
new ones: the latest games first, the blunders before the mistakes, as many a day as the repertoires' new positions
(`training.json` `new_per_day`).

## Looked for in the background

On the Training page, every two seconds at most, what the exercises come from is checked: the names, the language,
each database's snapshot generation and the analyses folder's time. When it changed, a thread at the lowest priority
(`study::enterBackground()`) finds the user's games (the names' words as the game list's player filter, then each game
told by its players' whole words), their review keys (the game read as the Board page reads it), and the exercises of
those reviewed; a game whose review did not change since keeps the exercises found before. The cards of the record live
on the screen side: answers given while the thread looked are kept. A game no longer found takes its exercises with it.

The latest 20 games of the user's (by date) without a review are offered: *Review them · in the background* reviews
them one after the other as the Board page reviews a game in the Chess space (its analysis engine, its ceiling, nodes on
one thread from an empty hash, Lichess's evaluations kept offline, a second line where a move can be the only one, the
processes `reviewProcesses()` leaves beside the tournaments), each review kept with its game in the profile's
`analyses/` as if it had been reviewed on the Board page; each game's exercises come as it ends. They wait while the
Board page analyses or reviews, and while one of the user's games is played live on Lichess.

## The profile's files

`mistakes/exercises.json`: the exercises by position, each with its game (start, moves up to the position, tags, its
database and number), the move played and its judgement and kind, the review's engine, limit and date, its best line and
the line after the move played with their scores, the review's comment, and the card (FSRS) with its first answer; and
the reviewed games by review key, each with its review's signature and the positions it gave. `mistakes/answers.jsonl`:
a line per answer (the position, when, its grade, its time, the move played). `guess/games.jsonl`: a line per game
guessed (its title and side, the points of the maximum, the guesses that were the game's move, the average pawns lost).
Nothing leaves the machine.

## Guessing the moves

A game drawn at random from a database through the game list's quick filter and a minimum Elo (a game of 16 plies at
least), or the Board page's game from the position shown, for the side to move there. The moves of theory of the user's
databases pass by themselves (`commentary::theoryOf()` on the first database of theirs with an opening tree, the game
itself left out), for a game from its usual start. The game's later moves are never on the board before they are
guessed.

The analysis engine, one thread, a second a move: the position after the game's move is searched while the user thinks,
the position after the guess once it is played. The guess scores 3 when it is the game's move; else, both evaluations
for the side that played: 2 when the guess is 0.2 pawn worse at most (or better), 1 when 0.5 pawn worse at most, else
0 (mates as 100 pawns less a tenth a move: a mate in more moves is as good, a mate given up for a few pawns is not). The
game goes on by itself after a moment (the repertoires' training's setting, and 1.5 s more), the opponent's reply played.

## Checked on the screen

On the private display, a profile with Gaia 4.4.3 and six games of ours (Gaia at a few nodes for the user against Gaia
at 40,000 nodes, 39 to 110 plies): the name added from the Training page, the six games offered, reviewed in the
background at a ceiling of 320 ms (three passes, four processes, about a minute on a machine whose 32 threads were
busy with other work), the exercises arriving game by game: 5 exercises from 5 games, two kinds ("Pieces left en prise",
the others). A session: a move other than the review's judged right by the engine, a wrong one (orange, the green arrow,
the engine's line after it), Retry, "I don't know", the end; the day's review chained from the Training page; a move
typed; a guessed game from both sides, the game going on by itself, the end; the Board page's game guessed from Ctrl+K;
the game of an exercise opened at its move.
