# The commentary's data

## openings.tsv

Opening names from Lichess's [chess-openings](https://github.com/lichess-org/chess-openings) (CC0): one line per named
line of moves, `eco`, `name` and `pgn` separated by tabs, after a header. It is the copy the Rust prototype embeds
(`jromang/rookstudio-rust`, `crates/analysis/data/openings.tsv`), kept the same so that the prompts name the openings as
the prototype's did. Embedded at build time (`cmake/embed_binary.cmake`) and read by `openings.cpp`.
