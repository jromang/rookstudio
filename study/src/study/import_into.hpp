#pragma once

// A file's games imported into a database (store.hpp): a PGN (.zst, .gz, .zip) read as import.hpp reads it, or a
// ChessBase database or archive as chessbase.hpp does, committed as it is read.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "chessbase.hpp"
#include "import.hpp"
#include "store.hpp"
#include "store_error.hpp"

namespace fcg::study {

// What an import into a database did.
struct DatabaseImport {
    ImportReport read;                         // what reading the file gave
    std::optional<ChessBaseReport> chessbase;  // a ChessBase database's: what its games left out
    uint64_t added      = 0;
    uint64_t duplicates = 0;
    uint64_t damaged    = 0;  // of the games added, those damaged (read.damaged counts the duplicates too)
    uint64_t commits    = 0;
    std::string error;                        // why the disk refused; the games committed before stay
    StoreErrc error_code = StoreErrc::Other;  // what `error` is, for the window
    // The first damaged games read, 64 at most: their duplicateKey(), which Snapshot::key() gives back, and the line
    // of the file their text starts on.
    std::vector<std::pair<uint64_t, uint64_t>> damaged_lines;
};

// The games of `file` (PGN, .zst, .gz, .zip) added to `db`, committed every `commit_games` games and at the end: a
// crash or a cancel keeps what was committed, and importing the file again adds the rest only (the duplicates are
// left out, unless `keep_duplicates`). `cancel` stops the reading within a tenth of a second, what was read being
// committed; importInto sets it itself when the disk refuses (`error` then says why).
// A ChessBase database or archive (isChessBase()) is read as importChessBase() reads it, `chessbase` its options (a
// .cbz's password).
// The file is kept as its games' source (Snapshot::source()). The keys of `forget` (sorted) do not count as games the
// database has: a damaged game's, imported again from its file.
DatabaseImport importInto(Database& db, const std::filesystem::path& file, const ImportOptions& options,
                          ImportProgress& progress, std::atomic<bool>& cancel, bool keep_duplicates = false,
                          std::size_t commit_games = std::size_t{1} << 18, const ChessBaseOptions& chessbase = {},
                          const std::vector<uint64_t>& forget = {});

}  // namespace fcg::study
