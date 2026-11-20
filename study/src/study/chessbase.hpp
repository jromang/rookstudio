#pragma once

// ChessBase databases, read in place and never written:
// - CBH, the older format: a .cbh of 46-byte records (one a game), the moves in .cbg, the annotations in .cba, the
//   names in .cbp (players), .cbt (tournaments), .cbc (annotators), .cbs (sources), .cbe (teams);
// - 2CBH, the newer one: .2cbh (192-byte records), .2cbg, .2cba, and the names in .2lid;
// - an archive of either: .cbv, or .cbz enciphered with a password, its members unpacked as they are read.
//
// A database is opened from any of its files, then read record by record, in order or not: each game as a GameTree
// (tags, start position, moves with their notes and variations). The files are mapped and read where each record
// points, so a database of millions of games is never loaded whole.
//
// What a game's annotations become:
// - texts after and before a move: its comments (a text in several languages: English first, the others after);
// - symbols: NAGs; coloured squares and arrows: the marks of MoveNotes;
// - clocks: the move's clock; the time a move took: [%emt]; an engine's evaluation: the move's evaluation;
// - medals: a Medals tag (their bits, as stored); a time control: the TimeControl tag; a critical position: a comment.
// What is left out is counted, by kind, in the reader's report: training questions, pictures, sounds, videos, web
// links, games quoted, pawn structures and piece paths, variation colours, correspondence moves' dates, the texts
// (articles) a database keeps among its games, and annotations of a type this reader does not know.

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "game.hpp"
#include "import.hpp"

namespace fcg::study {

enum class ChessBaseFormat : uint8_t { CBH, TwoCBH };

// The file is one of a ChessBase database's (.cbh, .2cbh and their companions) or an archive of one (.cbv, .cbz),
// whatever the case of its name.
[[nodiscard]] bool isChessBase(const std::filesystem::path& file);

struct ChessBaseOptions {
    std::string password;  // a .cbz's
    // An archive's member of at most this many bytes is unpacked whole when the database is opened; a larger one a
    // block at a time as it is read (files of names are unpacked whole whatever their size, up to 1 GiB).
    uint64_t unpack_whole = 16u << 20;
};

// One game of a database.
struct ChessBaseGame {
    GameTree tree;
    bool chess960   = false;
    uint32_t record = 0;  // its record in the database, from 1
    bool damaged    = false;
    std::string problem;  // what could not be read, when damaged: the moves and notes before it are kept
};

// What the games read so far left out, and what could not be read.
struct ChessBaseReport {
    uint64_t records    = 0;  // read
    uint64_t games      = 0;  // given
    uint64_t texts      = 0;  // texts (articles) among the games, left out
    uint64_t deleted    = 0;  // games marked deleted, left out
    uint64_t unknown    = 0;  // records that are neither a game nor a text
    uint64_t damaged    = 0;  // games given with what could be read of them
    uint64_t unreadable = 0;  // records that could not be read at all

    // Annotations left out, by kind.
    uint64_t training          = 0;
    uint64_t pictures          = 0;
    uint64_t sounds            = 0;
    uint64_t videos            = 0;
    uint64_t web_links         = 0;
    uint64_t quotations        = 0;  // games quoted in a comment
    uint64_t pawn_structures   = 0;
    uint64_t piece_paths       = 0;
    uint64_t variation_colours = 0;
    uint64_t correspondence    = 0;  // correspondence moves' dates
    uint64_t undecoded         = 0;  // of a kind this reader knows but whose layout it cannot read here
    uint64_t other             = 0;  // of a type this reader does not know

    std::vector<std::string> problems;  // the first problems met, each naming its record
};

class ChessBaseDatabase {
   public:
    // `file` is any file of a database, or an archive. Throws std::runtime_error with a readable message when a file
    // the database needs is missing or is not of the format.
    explicit ChessBaseDatabase(const std::filesystem::path& file, const ChessBaseOptions& options = {});
    ~ChessBaseDatabase();
    ChessBaseDatabase(ChessBaseDatabase&&) noexcept;
    ChessBaseDatabase& operator=(ChessBaseDatabase&&) noexcept;
    ChessBaseDatabase(const ChessBaseDatabase&)            = delete;
    ChessBaseDatabase& operator=(const ChessBaseDatabase&) = delete;

    [[nodiscard]] ChessBaseFormat format() const noexcept;
    // Its records: games, texts, deleted games. Record numbers run from 1 to records().
    [[nodiscard]] uint32_t records() const noexcept;

    // Record `record`'s game; none for a text, a deleted game, or a record that cannot be read (the report says why).
    // Throws std::out_of_range for a record past the last.
    [[nodiscard]] std::optional<ChessBaseGame> read(uint32_t record);
    // The games from record `first` on, in order, each given to `sink`, which returns false to stop; `cancel` stops it
    // between games too.
    void forEach(const std::function<bool(ChessBaseGame&&)>& sink, uint32_t first = 1,
                 const std::atomic<bool>* cancel = nullptr);

    // What the games read so far left out.
    [[nodiscard]] const ChessBaseReport& report() const noexcept;
    // The size of the database's header file, and the bytes of it the records read so far end at: a progress.
    [[nodiscard]] uint64_t headerBytes() const noexcept;
    [[nodiscard]] uint64_t headerEnd(uint32_t record) const noexcept;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// One game as a database takes it, as the PGN import makes it: its tags, its moves' codes, its clocks and
// evaluations, and its text written as PGN when it has notes or variations.
[[nodiscard]] ImportedGame importedGame(const ChessBaseGame& game);

// Reads `file`'s games as importGames() reads a PGN file's: a batch at a time (of options.piece_bytes / 1024 games
// at most), in the database's order, from the calling thread; `progress` counts the header file's bytes. `cancel`
// stops it between batches. `left_out`, when given, receives what the games left out. The error of a database that
// cannot be opened is the report's.
ImportReport importChessBase(const std::filesystem::path& file, const ImportOptions& options, ImportProgress& progress,
                             const std::atomic<bool>& cancel,
                             const std::function<void(std::vector<ImportedGame>&&)>& sink,
                             ChessBaseReport* left_out = nullptr, const ChessBaseOptions& chessbase = {});

}  // namespace fcg::study
