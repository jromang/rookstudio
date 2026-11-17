#pragma once

// A database: a folder whose games are kept in immutable segments, each written whole, synced, then named in the
// manifest, whose replacement by a rename is the only commit. After a crash a database opens as it was at its last
// commit, the leftovers of the commit cut short removed. Readers hold a Snapshot: the segments it maps stay valid
// while later commits add others.
//
//   Name.fcgdb/
//     manifest.json   the segments, in order, with their games and new names
//     seg-NNNNNN/     cols.bin (the header columns), moves.bin (the moves' codes), annex.bin (FEN, other tags, the
//                     text of annotated or damaged games, clocks, evaluations), names.bin (the names it adds)

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <tour/files.hpp>

#include "import.hpp"

namespace fcg::study {

using GameId = uint64_t;

// What a game's flags say.
inline constexpr uint16_t GAME_ANNOTATED = 1;
inline constexpr uint16_t GAME_DAMAGED   = 2;
inline constexpr uint16_t GAME_CHESS960  = 4;
inline constexpr uint16_t GAME_FEN       = 8;
inline constexpr uint16_t GAME_CLOCKS    = 16;
inline constexpr uint16_t GAME_EVALS     = 32;

// The games of one segment, at most.
inline constexpr std::size_t SEGMENT_GAMES = std::size_t{1} << 20;

// What the game list shows of a game: views into the database's mapped files, valid while its snapshot is held.
struct HeaderRow {
    std::string_view white, black, event, site, round, annotator, time_control;
    uint32_t date      = 0;  // year << 9 | month << 5 | day, 0 for a part not known
    uint16_t white_elo = 0;
    uint16_t black_elo = 0;
    uint16_t eco       = 0;  // 0 none, else 1 + A00..E99 counted from 0
    uint16_t flags     = 0;
    uint32_t plies     = 0;
    char result        = '*';
};

// A game's numbers, without its names: what filters and sorts read of every game.
struct HeaderNumbers {
    uint32_t date      = 0;
    uint32_t plies     = 0;
    uint16_t white_elo = 0;
    uint16_t black_elo = 0;
    uint16_t eco       = 0;
    uint16_t flags     = 0;
    char result        = '*';
};

// A game's move codes, what replaying it needs (a game with a start position of its own: GAME_FEN).
struct GameMoves {
    std::span<const uint8_t> codes;
    uint32_t plies = 0;
    uint16_t flags = 0;
};

[[nodiscard]] uint32_t packDate(std::string_view pgn_date);
// A game's key for finding duplicates: a hash of its event, site, round, players, date, start, moves and result.
// Two rounds of a tournament with the same moves are two games.
[[nodiscard]] uint64_t duplicateKey(const ImportedGame& game);
[[nodiscard]] std::string dateText(uint32_t date);  // "2026.11.17", "1972.??.??"
[[nodiscard]] uint16_t packEco(std::string_view eco);
[[nodiscard]] std::string ecoText(uint16_t eco);

class Segment;

class Snapshot {
   public:
    [[nodiscard]] uint64_t games() const noexcept { return games_; }
    [[nodiscard]] uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] HeaderRow row(GameId id) const;
    [[nodiscard]] HeaderNumbers numbers(GameId id) const;
    [[nodiscard]] GameMoves gameMoves(GameId id) const;
    // The ids of a game's names, for the filters and sorts that read names once.
    struct NameIds {
        uint32_t white = 0, black = 0, event = 0, site = 0, round = 0;
    };
    [[nodiscard]] NameIds rowIds(GameId id) const;
    [[nodiscard]] std::span<const uint8_t> moves(GameId id) const;
    // What tells a game from another: its event, site, round, players, date, start, moves and result
    // (duplicateKey()).
    [[nodiscard]] uint64_t key(GameId id) const;
    // Everything kept of a game: its tags in their order of import less those of the columns (which come first),
    // its moves, its text when annotated or damaged, its clocks and evaluations.
    [[nodiscard]] ImportedGame game(GameId id) const;
    [[nodiscard]] uint32_t names() const noexcept { return names_; }
    [[nodiscard]] std::string_view name(uint32_t id) const;

   private:
    friend class Database;
    [[nodiscard]] std::pair<const Segment*, std::size_t> locate(GameId id) const;

    std::vector<std::shared_ptr<const Segment>> segments_;
    std::vector<uint64_t> first_game_;  // of each segment
    std::vector<uint32_t> first_name_;
    uint64_t games_      = 0;
    uint32_t names_      = 0;
    uint64_t generation_ = 0;
};

class Database {
   public:
    // A new database in an empty or missing folder. Throws std::runtime_error when the folder holds something else,
    // tour::FolderBusy when another program writes to it.
    static std::shared_ptr<Database> create(const std::filesystem::path& dir);
    // An existing database, the leftovers of a commit cut short removed. Throws std::runtime_error when it is none.
    static std::shared_ptr<Database> open(const std::filesystem::path& dir);

    [[nodiscard]] std::shared_ptr<const Snapshot> snapshot() const;
    struct Appended {
        uint64_t added      = 0;
        uint64_t duplicates = 0;  // left out: a game it has, the same in all that duplicateKey() reads
    };
    // The games added, in segments of at most SEGMENT_GAMES, committed together: after a crash the database has all
    // of them or none. A game the database already has, or the batch had before it, is left out unless
    // `keep_duplicates`. Throws std::runtime_error when the disk refuses; the database is then as before.
    Appended append(std::vector<ImportedGame> games, bool keep_duplicates = false);
    [[nodiscard]] const std::filesystem::path& folder() const noexcept { return dir_; }

    explicit Database(const std::filesystem::path& dir);  // use create() or open()

   private:
    void load();
    uint32_t nameId(const std::string& name, std::vector<std::string>& added);

    std::filesystem::path dir_;
    tour::FolderLock lock_;
    mutable std::mutex mutex_;
    std::shared_ptr<const Snapshot> current_;
    std::unordered_map<std::string, uint32_t> name_ids_;  // filled at the first append
    bool names_indexed_ = false;
    std::vector<uint64_t> known_keys_;  // the games' keys, sorted, filled at the first append that skips duplicates
    bool keys_indexed_     = false;
    uint64_t next_segment_ = 1;
};

struct DatabaseImport {
    ImportReport read;  // what reading the file gave
    uint64_t added      = 0;
    uint64_t duplicates = 0;
    uint64_t commits    = 0;
    std::string error;  // why the disk refused; the games committed before stay
};

// The games of `file` (PGN, .zst, .gz, .zip) added to `db`, committed every `commit_games` games and at the end: a
// crash or a cancel keeps what was committed, and importing the file again adds the rest only (the duplicates are
// left out, unless `keep_duplicates`). `cancel` stops the reading within a tenth of a second, what was read being
// committed; importInto sets it itself when the disk refuses (`error` then says why).
DatabaseImport importInto(Database& db, const std::filesystem::path& file, const ImportOptions& options,
                          ImportProgress& progress, std::atomic<bool>& cancel, bool keep_duplicates = false,
                          std::size_t commit_games = std::size_t{1} << 18);

// For the tests: called at each step of a commit with its name; an exception thrown from it stops the commit there,
// as a crash would. Empty: none.
void setCommitFaultHook(std::function<void(std::string_view step)> hook);

}  // namespace fcg::study
