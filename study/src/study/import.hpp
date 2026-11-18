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
};

// One game's text into a game. False when it holds no game.
bool readImportedGame(std::string_view text, ImportedGame& out);

// The games of a piece of text, each with its tags and moves: where they start and end.
[[nodiscard]] std::vector<std::string_view> splitGames(std::string_view text);

struct ImportOptions {
    int threads             = 0;        // 0: the machine's
    std::size_t piece_bytes = 8 << 20;  // the text read by one worker at a time
};

struct ImportProgress {
    std::atomic<uint64_t> bytes{0};  // of the file read so far
    std::atomic<uint64_t> games{0};
    uint64_t total_bytes = 0;  // the file's size
};

struct ImportReport {
    uint64_t games   = 0;
    uint64_t damaged = 0;
    bool cancelled   = false;
    std::string error;  // a file that could not be read; the games before it were given
};

// Reads `file` (PGN, .zst, .gz, .zip), giving its games to `sink` a batch at a time, in the file's order, from the
// calling thread. `cancel` stops it between batches.
ImportReport importGames(const std::filesystem::path& file, const ImportOptions& options, ImportProgress& progress,
                         const std::atomic<bool>& cancel, const std::function<void(std::vector<ImportedGame>&&)>& sink);

}  // namespace fcg::study
