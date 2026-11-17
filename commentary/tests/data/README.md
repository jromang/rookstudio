# The commentary's reference files

What the Rust prototype (`jromang/rookstudio-rust`) and its chess library said, which the port must say again.

## shakmaty-positions.jsonl.zst

For each position, what shakmaty 0.30.2 (the prototype's chess library) says of it: its FEN with the en passant square
only when a capture there is legal, whether it is check, mate, stalemate or a draw for lack of material, and each legal
move in shakmaty's order with its UCI, SAN, debug text and the FEN after it. One JSON object per line.

The positions: those of `tools/port/shakmaty-ref/positions.txt` (made to try castling, en passant, checks, promotions
and the ends of a game), then one position in four of the games of the prototype's held-out commentary cases (Lichess
games of January 2013, CC0), each position once. Made with `tools/port/shakmaty-ref` (`cargo build --release`):

```sh
shakmaty-ref < tools/port/shakmaty-ref/positions.txt > hand.jsonl
# the games: one line "moves 4 <uci> <uci> ..." per case of cases-heldout-confirmed.jsonl
shakmaty-ref < games.txt > games.jsonl   # then each position kept once (by its board, turn, rights and en passant)
cat hand.jsonl games.jsonl | zstd -19 > shakmaty-positions.jsonl.zst
```

## cases-heldout.jsonl.zst, briefs-heldout.jsonl.zst

The prototype's held-out commentary cases: 294 moves of Lichess games of January 2013 (CC0), each with what its facts
are computed from (the moves from the start, the review's verdict, the engine's lines before and after the move, and
the answers the engine gave to the questions the detectors asked it), as `rookstudio-dataset cases` and `confirm`
wrote them (`cases-heldout-confirmed.jsonl`). No human comment, no theory text.

The briefs are what the prototype (`jromang/rookstudio-rust` at 69471b2) makes of each case, in English and in French:
the position, the move in each language's notation, the facts it achieves, what punishes it, what the better move would
have done, the prompt its language model gets and the comment its templates write:

```sh
rookstudio-dataset briefs cases-heldout-confirmed.jsonl --out briefs.jsonl --lang both   # no tablebases
zstd -19 briefs.jsonl -o briefs-heldout.jsonl.zst
```

`golden_tests.cpp` holds the port to them: the facts, the template comments, and the prompts, which the port makes with
the opening names of `commentary/data/openings.tsv` as the prototype's tool does.

## reviews-heldout.jsonl.zst

The review of each held-out case's move made again by the prototype's present rules, from the engine lines the case
keeps (`rookstudio-dataset rescore`), the cases it no longer singles out left out; of each, its id and review only:

```sh
rookstudio-dataset rescore cases-heldout-confirmed.jsonl rescored.jsonl
# keep {"id", "review"} of each line, then
zstd -19 reviews.jsonl -o reviews-heldout.jsonl.zst
```

A larger corpus made the same way (the prototype's training cases, kept out of the repository) checks the branches the
held-out cases do not reach:

```sh
rookstudio-dataset briefs cases.jsonl --out briefs.jsonl --lang both
FCG_GOLDEN_CASES=cases.jsonl FCG_GOLDEN_BRIEFS=briefs.jsonl FCG_GOLDEN_REVIEWS=reviews.jsonl \
    build/fcg-commentary-tests -tc='golden*'
```
