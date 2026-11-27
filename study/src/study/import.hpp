#pragma once

// Games read from a file of any size, on several cores: the text cut into pieces at games' starts, each piece read
// by a worker (tags, moves through ScanBoard, their codes, clocks and evaluations), the games given back in the
// file's order whatever the number of workers.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "imported_game.hpp"

namespace fcg::study {

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
