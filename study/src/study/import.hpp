#pragma once

// Games read from a file of any size, on several cores: the text cut into pieces at games' starts, each piece read
// by a worker (tags, moves through ScanBoard, their codes, clocks and evaluations), the games given back in the
// file's order whatever the number of workers.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
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

// The games of a piece of text, each with its tags and moves: where they start and end.
[[nodiscard]] std::vector<std::string_view> splitGames(std::string_view text);

struct ImportOptions {
    int threads             = 0;        // 0: the machine's
    std::size_t piece_bytes = 8 << 20;  // the text read by one worker at a time
};

struct ImportProgress {
    std::atomic<uint64_t> bytes{0};  // of the file read so far
    std::atomic<uint64_t> games{0};
    std::atomic<uint64_t> total_bytes{0};  // the file's size, read by the window as the import sets it
};

struct ImportReport {
    uint64_t games   = 0;
    uint64_t damaged = 0;
    bool cancelled   = false;
    std::string error;  // a file that could not be read; the games before it were given
};

// Whether the first bytes of a file's text can be a PGN's: no zero byte, and control bytes (but tabs, line ends, form
// feeds and the end-of-file mark of old files) far fewer than random bytes have.
[[nodiscard]] bool looksLikeText(std::string_view head);

// Reads `file` (PGN, .zst, .gz, .zip), giving its games to `sink` a batch at a time, in the file's order, from the
// calling thread. `cancel` stops it between batches. A file, or a file of an archive, whose first bytes are not text
// is refused (the report's error says so) before it makes junk games.
ImportReport importGames(const std::filesystem::path& file, const ImportOptions& options, ImportProgress& progress,
                         const std::atomic<bool>& cancel, const std::function<void(std::vector<ImportedGame>&&)>& sink);

// The lines of `file` as importGames reads it (PGN, .zst, .gz, .zip, the members of an archive one after the other),
// from line `first` (from 1), `count` of them at most: fewer when the text ends before. Each without its line end, and
// cut after 4 KB with "…". `cancel` stops it, with the lines read so far. Throws when the file cannot be read.
[[nodiscard]] std::vector<std::string> readLines(const std::filesystem::path& file, uint64_t first, std::size_t count,
                                                 const std::atomic<bool>& cancel);

}  // namespace fcg::study
