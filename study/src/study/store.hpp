#pragma once

// A database: a folder whose games are kept in immutable segments, each written whole, synced, then named in the
// manifest, whose replacement by a rename is the only commit. After a crash a database opens as it was at its last
// commit, the leftovers of the commit cut short removed. Readers hold a Snapshot: the segments it maps stay valid
// while later commits add others.
//
//   Name.fcgdb/
//     manifest.json   the segments, in order, with their games and new names
//     seg-NNNNNN/     cols.bin (the header columns), moves.bin (the moves' codes), annex.bin (FEN, other tags, the
//                     text of annotated or damaged games, clocks, evaluations), names.bin (the names it adds),
//                     sig.bin (the games' signatures, for the searches; not in the first format's segments)

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <tour/files.hpp>

#include "chessbase.hpp"
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
    // The generation of the last commit that replaced or deleted games (0: none): what was built from the games
    // before, an opening tree, may no longer hold.
    [[nodiscard]] uint64_t editsGeneration() const noexcept { return edits_generation_; }
    [[nodiscard]] HeaderRow row(GameId id) const;
    [[nodiscard]] HeaderNumbers numbers(GameId id) const;
    [[nodiscard]] GameMoves gameMoves(GameId id) const;
    // The ids of a game's names, for the filters and sorts that read names once.
    struct NameIds {
        uint32_t white = 0, black = 0, event = 0, site = 0, round = 0;
    };
    [[nodiscard]] NameIds rowIds(GameId id) const;
    [[nodiscard]] std::span<const uint8_t> moves(GameId id) const;
    // What its moves tell the searches (signature.hpp): none in a segment of the first format.
    [[nodiscard]] std::optional<GameSignature> signature(GameId id) const;
    // A game deleted keeps its number, its game shown or not as the list asks; searches and the tree leave it out.
    [[nodiscard]] bool deleted(GameId id) const noexcept {
        return id / 64 < deleted_.size() && (deleted_[id / 64] >> (id % 64) & 1);
    }
    [[nodiscard]] uint64_t deletedCount() const noexcept { return deleted_count_; }

    // A game replaced: where its new version is (a segment of versions, and its place there).
    struct Version {
        GameId id        = 0;
        uint32_t segment = 0;
        uint32_t local   = 0;
    };
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

    std::vector<std::shared_ptr<const Segment>> segments_;  // all of them, those of versions included
    std::vector<std::size_t> logical_;                      // the segments that add games, in order
    std::vector<uint64_t> first_game_;                      // of each segment that adds games
    std::vector<uint32_t> first_name_;                      // of each segment
    std::vector<Version> replaced_;                         // by id
    std::vector<uint64_t> deleted_;                         // a bit a game
    uint64_t deleted_count_    = 0;
    uint64_t edits_generation_ = 0;
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
    // A database another program writes (it holds the folder's lock, open() throws tour::FolderBusy): its games read
    // as that program commits them (refresh()), nothing written; the files it is writing are left alone. Throws as
    // open() does.
    static std::shared_ptr<Database> openReadOnly(const std::filesystem::path& dir);
    [[nodiscard]] bool readOnly() const noexcept { return !lock_.has_value(); }
    // Read-only: the snapshot made again when the other program committed since (its manifest's generation changed).
    // True when it was; false otherwise, and for a database this program writes, always current.
    bool refresh();

    [[nodiscard]] std::shared_ptr<const Snapshot> snapshot() const;
    struct Appended {
        uint64_t added      = 0;
        uint64_t duplicates = 0;  // left out: a game it has, the same in all that duplicateKey() reads
        uint64_t damaged    = 0;  // of the games added, those kept up to what could not be read
    };
    // The games added, in segments of at most SEGMENT_GAMES, committed together: after a crash the database has all
    // of them or none. A game the database already has, or the batch had before it, is left out unless
    // `keep_duplicates`. Throws std::runtime_error when the disk refuses; the database is then as before.
    Appended append(std::vector<ImportedGame> games, bool keep_duplicates = false);

    // Replaces game `id` with `game`: it keeps its number, its new version written in a segment of versions; the
    // old one stays where it was until the database is tidied. Committed as append() is. Throws std::out_of_range
    // for a number the database does not have, std::runtime_error when the disk refuses (nothing changed then).
    void replace(GameId id, ImportedGame game);
    // Marks games deleted, or no longer deleted; numbers the database does not have are passed over. Committed and
    // thrown as replace().
    void setDeleted(std::span<const GameId> ids, bool deleted);
    [[nodiscard]] const std::filesystem::path& folder() const noexcept { return dir_; }

    explicit Database(const std::filesystem::path& dir);  // use create() or open()
    Database(const std::filesystem::path& dir, bool read_only);  // use openReadOnly()

   private:
    void load();
    uint32_t nameId(const std::string& name, std::vector<std::string>& added);
    // Writes `games` in new segments (of versions when `versions`) and commits them with the edits `replaced` and
    // `deleted` (the snapshot's own when null), in one manifest. Under the lock.
    void commit(std::vector<ImportedGame>& games, const std::vector<uint64_t>& keys, bool versions,
                const std::vector<Snapshot::Version>* replaced, const std::vector<uint64_t>* deleted);

    std::filesystem::path dir_;
    std::optional<tour::FolderLock> lock_;  // none when read-only
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
    std::optional<ChessBaseReport> chessbase;  // a ChessBase database's: what its games left out
    uint64_t added      = 0;
    uint64_t duplicates = 0;
    uint64_t damaged    = 0;  // of the games added, those damaged (read.damaged counts the duplicates too)
    uint64_t commits    = 0;
    std::string error;  // why the disk refused; the games committed before stay
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
DatabaseImport importInto(Database& db, const std::filesystem::path& file, const ImportOptions& options,
                          ImportProgress& progress, std::atomic<bool>& cancel, bool keep_duplicates = false,
                          std::size_t commit_games = std::size_t{1} << 18, const ChessBaseOptions& chessbase = {});

// For the tests: called at each step of a commit with its name; an exception thrown from it stops the commit there,
// as a crash would. Empty: none.
void setCommitFaultHook(std::function<void(std::string_view step)> hook);

}  // namespace fcg::study
