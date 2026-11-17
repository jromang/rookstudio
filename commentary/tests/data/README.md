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
