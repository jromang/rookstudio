#pragma once

// A game as a database takes it (store.hpp) and as an import reads it (import.hpp): its tags, its moves' codes, its
// clocks and evaluations, its text when it has notes; read from one game's text, written back as PGN, and why a
// damaged one stops where it does.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "signature.hpp"

namespace fcg::study {

// A game as a database takes it.
struct ImportedGame {
    std::vector<std::pair<std::string, std::string>> tags;  // as read, in UTF-8
    std::string start_fen;                                  // empty: the standard start
    bool chess960 = false;
    // Its moves' codes (scan_board.hpp): one byte each, two (the high one first) where the position has more than
    // 256 codes.
    std::vector<uint8_t> moves;
    uint32_t plies = 0;
    char result    = '*';  // '1' 1-0, '0' 0-1, '=' 1/2-1/2, '*' none
    // The game's text is kept, to be read again, when it has comments, symbols or variations, or when a move could not
    // be read (the moves before it are kept).
    bool annotated = false;
    bool damaged   = false;
    std::string text;
    std::vector<int32_t> clocks;  // [%clk] after each ply, in milliseconds, -1 when none; empty when none at all
    std::vector<int16_t> evals;   // [%eval] after each ply, centipawns for White, mates ±(30000 - moves); NO_EVAL none
    static constexpr int16_t NO_EVAL = -32768;
    // What its moves tell the searches, made as they are read; a database makes it when it is missing.
    std::optional<GameSignature> signature;
    // The line its text starts on in the file read (from 1, the members of an archive one after the other); 0 when it
    // was not read from a file. Not kept by a database.
    uint64_t line = 0;
};

// One game's text into a game. False when it holds no game.
bool readImportedGame(std::string_view text, ImportedGame& out);

// A game as PGN, to be read again: its own text when it kept one (comments, symbols, variations, a move that could
// not be read), else its tags, its moves in SAN and its result.
[[nodiscard]] std::string importedPgn(const ImportedGame& game);

// Why a damaged game's moves stop where they do: the move its text has there, which could not be read or is not
// legal (empty when the text ends there), or a start position (FEN) that is not one, no move being kept then.
struct Damage {
    std::string move;        // as written, its glyphs left out
    bool cut       = false;  // the move is the text's last word: the text was cut in it (the end of a file)
    bool bad_start = false;
};
[[nodiscard]] Damage damageOf(const ImportedGame& game);

}  // namespace fcg::study
