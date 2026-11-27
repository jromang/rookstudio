#pragma once

// A database: a folder whose games are kept in immutable segments, each written whole, synced, then named in the
// manifest, whose replacement by a rename is the only commit. After a crash a database opens as it was at its last
// commit, the leftovers of the commit cut short removed. Readers hold a Snapshot: the segments it maps stay valid
// while later commits add others. A segment damaged while the database was closed does not keep it from opening: its
// games keep their numbers, the others read (Snapshot::unreadable()).
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
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <tour/files.hpp>

#include "imported_game.hpp"
#include "players.hpp"
#include "scan_board.hpp"
#include "store_error.hpp"

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

// A game's move codes, what replaying it needs (a game with a start position of its own: GAME_FEN). Its plies never
// more than its codes' bytes, a damaged record's included.
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

namespace store {
class Segment;  // a segment as it is read (store_detail.hpp)
}

// A game's place as the edits leave it: its version (none: as it was added) and whether it is deleted. What undoing an
// edit puts back (Database::restore()).
struct GameState {
    GameId id = 0;
    std::optional<std::pair<uint32_t, uint32_t>> version;  // a segment of versions and its place there
    bool deleted = false;
};

// A segment that cannot be read: its games' number, why, the file it was imported from.
struct BadSegment {
    std::string name;
    GameId first   = 0;  // its first game's number; for a segment of versions, 0
    uint64_t games = 0;
    bool versions  = false;
    std::string error;
    std::string source;
    // Its names.bin still reads (or it brought no name): the names it brought, which later games may use, are kept.
    bool names_kept = true;
    // A file of it, or its folder, is not there; else one is there but changed (cut short, written over).
    bool missing = false;
};

class Snapshot {
   public:
    [[nodiscard]] uint64_t games() const noexcept { return games_; }
    [[nodiscard]] uint64_t generation() const noexcept { return generation_; }
    // The generation of the last commit that replaced or deleted games (0: none): what was built from the games
    // before, an opening tree, may no longer hold. While segments cannot be read, a number of its own for them.
    [[nodiscard]] uint64_t editsGeneration() const noexcept {
        return unreadable_edits_ ? unreadable_edits_ : edits_generation_;
    }
    [[nodiscard]] HeaderRow row(GameId id) const;
    [[nodiscard]] HeaderNumbers numbers(GameId id) const;
    [[nodiscard]] GameMoves gameMoves(GameId id) const;
    // The ids of a game's names, for the filters and sorts that read names once: each below names(), an id past them
    // (a damaged or crafted segment) read as no name.
    struct NameIds {
        uint32_t white = 0, black = 0, event = 0, site = 0, round = 0;
    };
    [[nodiscard]] NameIds rowIds(GameId id) const;
    [[nodiscard]] std::span<const uint8_t> moves(GameId id) const;
    // What its moves tell the searches (signature.hpp): none in a segment of the first format.
    [[nodiscard]] std::optional<GameSignature> signature(GameId id) const;
    // A game deleted keeps its number, its game shown or not as the list asks; searches and the tree leave it out. So
    // are the games of the segments that cannot be read (unreadable()).
    [[nodiscard]] bool deleted(GameId id) const noexcept {
        return (id / 64 < deleted_.size() && (deleted_[id / 64] >> (id % 64) & 1)) ||
               (id / 64 < unreadable_.size() && (unreadable_[id / 64] >> (id % 64) & 1));
    }
    [[nodiscard]] uint64_t deletedCount() const noexcept { return deleted_count_ + unreadable_count_; }

    // The segments that could not be read when the database was opened (a file of theirs cut short, missing, or not
    // a segment's): their games keep their numbers but read as empty games and count as deleted, so that the list,
    // the tree and the searches work on the others; the names they brought read from their names.bin when it still
    // reads, else as "?" (nameKnown()). Database::keepReadable() writes them again.
    [[nodiscard]] const std::vector<BadSegment>& unreadable() const noexcept { return unreadable_segments_; }
    // The games of those segments, those deleted before left out: the games that cannot be read, and their numbers.
    [[nodiscard]] uint64_t unreadableGames() const noexcept { return unreadable_count_; }
    [[nodiscard]] std::vector<GameId> unreadableIds() const;
    [[nodiscard]] bool readable(GameId id) const noexcept {
        return !(id / 64 < unreadable_.size() && (unreadable_[id / 64] >> (id % 64) & 1));
    }
    // Whether a name reads as it was written: false for a name brought by a segment whose names.bin could not be read
    // (it reads "?").
    [[nodiscard]] bool nameKnown(uint32_t id) const noexcept {
        for (const auto& [from, to] : unknown_names_)
            if (id >= from && id < to) return false;
        return true;
    }

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
    // its moves, its text when annotated or damaged, its clocks and evaluations. A record that does not read whole (a
    // damaged or crafted segment: its offsets out of its files, its annex cut short) gives its headers and what could
    // be read before the damage, marked damaged, without its moves when its start position is lost; never an
    // exception (the window and the threads that read games catch none).
    [[nodiscard]] ImportedGame game(GameId id) const;
    // Its start position's FEN, read alone (as tag() reads a tag); empty for a game of the standard start, and for a
    // record whose FEN cannot be read. Never throws for a damaged record: startOf() reads it on the searches' threads.
    [[nodiscard]] std::string startFen(GameId id) const;
    [[nodiscard]] uint32_t names() const noexcept { return names_; }
    [[nodiscard]] std::string_view name(uint32_t id) const;
    // A tag of a game kept with its other tags (not one of the columns): "WhiteFideId", "GameURL"; empty when none.
    [[nodiscard]] std::string tag(GameId id, std::string_view name) const;
    // The file the game was imported from (its segment's), empty when not known: a game added before the sources
    // were kept, or saved from the board. A game replaced keeps its first source.
    [[nodiscard]] const std::string& source(GameId id) const;

    // The table of names (players.hpp): never null, empty when none was made.
    [[nodiscard]] const std::shared_ptr<const PlayerTable>& players() const noexcept { return players_; }
    // A player's name as everything shows it: the name of the player of the table it belongs to, else as written.
    [[nodiscard]] std::string_view shownName(uint32_t name_id) const;
    // The game's state as its edits leave it, for undoing them.
    [[nodiscard]] GameState state(GameId id) const;
    // The bytes its moves and annex take: none for a deleted game a compaction emptied.
    [[nodiscard]] uint64_t storedBytes(GameId id) const;

    // The segments, those of versions included, in the manifest's order.
    struct SegmentInfo {
        std::string name;
        GameId first   = 0;  // its first game's number; for a segment of versions, 0
        uint32_t games = 0;
        bool versions  = false;
        std::string source;
    };
    [[nodiscard]] std::vector<SegmentInfo> segmentInfos() const;

   private:
    friend class Database;
    [[nodiscard]] std::pair<const store::Segment*, std::size_t> locate(GameId id) const;
    [[nodiscard]] std::size_t segmentOf(GameId id) const;  // the segment that added it (an index in segments_)
    // Deleted by an edit, the games that cannot be read left out: what the edits write and undo.
    [[nodiscard]] bool deletedByEdit(GameId id) const noexcept {
        return id / 64 < deleted_.size() && (deleted_[id / 64] >> (id % 64) & 1);
    }
    // What the segments that cannot be read make of the snapshot, made again after each change: their games, the
    // names not known, the edits generation of their own.
    void settle();
    // The name of a game without names, for the empty games of the segments that cannot be read: "" when the database
    // has that name, else "?"; none when it has neither.
    [[nodiscard]] std::optional<uint32_t> blankName() const;

    std::vector<std::shared_ptr<const store::Segment>> segments_;  // all of them, those of versions included
    std::vector<std::size_t> logical_;                      // the segments that add games, in order
    std::vector<uint64_t> first_game_;                      // of each segment that adds games
    std::vector<uint32_t> first_name_;                      // of each segment
    std::vector<Version> replaced_;                         // by id
    std::vector<uint64_t> deleted_;                         // a bit a game
    uint64_t deleted_count_    = 0;
    uint64_t edits_generation_ = 0;  // the manifest's
    std::vector<BadSegment> unreadable_segments_;
    std::vector<uint64_t> unreadable_;  // a bit a game of theirs, its version in them (none when all read)
    uint64_t unreadable_count_ = 0;     // of those not deleted
    std::vector<std::pair<uint32_t, uint32_t>> unknown_names_;  // the names not known, from and to
    uint64_t unreadable_edits_ = 0;  // while segments cannot be read: the edits generation made of theirs (else 0)
    uint64_t games_      = 0;
    uint32_t names_      = 0;
    uint32_t no_name_    = 0;  // what rowIds() gives for a name id past names_: the empty name, else "?" (blankName())
    uint64_t generation_ = 0;
    std::vector<std::string> segment_names_;  // of each segment
    std::vector<std::string> sources_;        // of each segment
    std::vector<bool> versions_;              // whether each segment holds versions
    std::shared_ptr<const PlayerTable> players_ = std::make_shared<const PlayerTable>();
    std::string players_file_;
};

// The board a game starts from, as its flags say (GameMoves::flags: a FEN of its own, Chess960 or not); false when it
// cannot be set, its FEN damaged included. Never throws: the searches, the opening tree, a dossier and an opening
// report call it on threads that catch nothing.
[[nodiscard]] bool startOf(const Snapshot& snapshot, GameId id, uint16_t flags, ScanBoard& board);

struct CheckProgress {
    std::atomic<uint64_t> done{0};  // of the games read
    std::atomic<uint64_t> total{0};
};

// What checking a database found (Database::check()): every segment read again from the disk, every game's moves
// replayed, its signature and its key (a hash of its headers and moves) made again and compared with the stored ones.
struct CheckReport {
    uint64_t games = 0;  // the games checked: those the database reads (not deleted), in their last versions
    // The segments that cannot be read again.
    using BadSegment = fcg::study::BadSegment;
    std::vector<BadSegment> segments;
    // The games read differently from how they were written, by number (those of the segments above included),
    // sorted, with the file each was imported from.
    std::vector<GameId> damaged;
    std::map<std::string, uint64_t> damaged_sources;  // their sources, and how many each
    bool tree       = false;                          // an opening tree covers the games
    bool signatures = true;  // every segment has its signatures (a database of the first format has none)
    bool cancelled  = false;
    double seconds  = 0;
};

// What the database found when it was opened, as a check says it: the segments that could not be read
// (Snapshot::unreadable()) and their games, damaged, with the files they were imported from; nothing when every
// segment read. Nothing is read again.
[[nodiscard]] CheckReport unreadableReport(const Snapshot& snapshot);

// What compacting would give back: the games deleted, the versions replaced, and the bytes they hold.
struct CompactPlan {
    uint64_t deleted  = 0;  // games deleted
    uint64_t replaced = 0;  // games replaced: their first versions, and the versions after them but the last
    uint64_t bytes    = 0;  // what goes
    uint64_t needed   = 0;  // what the new files take while the old ones are kept
    std::vector<std::string> segments;  // those written again
};

struct CompactProgress {
    std::atomic<uint64_t> segment{0};  // being written, from 1
    std::atomic<uint64_t> segments{0};
    std::atomic<uint64_t> games{0};  // of the segment written
};

struct Compacted {
    bool done      = false;
    bool cancelled = false;
    std::string error;
    StoreErrc error_code = StoreErrc::Other;  // what `error` is, for the window
    uint64_t bytes       = 0;                 // given back once the old files go
    bool undoable  = false;  // the old files kept: undoCompaction() until the database is closed
};

class Database {
   public:
    // A new database in an empty or missing folder. Throws std::runtime_error when the folder holds something else,
    // tour::FolderBusy when another program writes to it. The store's errors are StoreErrors (store_error.hpp), their
    // code with their text.
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
    // `keep_duplicates`; a key of `forget` (sorted) does not count as one it has. `source`, the file they come from,
    // is kept with their segments. Throws std::runtime_error when the disk refuses; the database is then as before.
    Appended append(std::vector<ImportedGame> games, bool keep_duplicates = false, const std::string& source = {},
                    const std::vector<uint64_t>& forget = {});

    // Replaces game `id` with `game`: it keeps its number, its new version written in a segment of versions; the
    // old one stays where it was until the database is tidied. Committed as append() is. Throws std::out_of_range
    // for a number the database does not have, std::runtime_error when the disk refuses (nothing changed then).
    void replace(GameId id, ImportedGame game);
    // Marks games deleted, or no longer deleted; numbers the database does not have are passed over. Committed and
    // thrown as replace().
    void setDeleted(std::span<const GameId> ids, bool deleted);
    [[nodiscard]] const std::filesystem::path& folder() const noexcept { return dir_; }

    // Duplicates merged (mockup M13, board 1): each `keep` replaced by its merged `game`, the `drop` games marked
    // deleted, all in one commit. Gives the games' states before, which restore() puts back. Throws as replace().
    struct Merge {
        GameId keep = 0;
        ImportedGame game;
        std::vector<GameId> drop;
    };
    std::vector<GameState> merge(std::vector<Merge> merges);
    // The games put back in the states given (their versions, deleted or not), in one commit: an edit undone.
    void restore(std::span<const GameState> states);
    // The table of names replaced, in one commit.
    void setPlayers(PlayerTable table);
    // Every game taken out, in one commit: the database as one just made (no segment, edits nor table of names), its
    // generation going on, so that what was built or kept of its games before (an opening tree, the window's views
    // and caches, which go by generation) no longer holds; the same Database for whoever holds it. A snapshot taken
    // before keeps reading its games: their files go once no snapshot holds them (at a later change, or when the
    // database is closed). Throws std::runtime_error when read-only, or when the disk refuses (nothing changed then).
    // A file read as a database is read again from the start so (LinkedDatabase).
    void clear();

    // Every segment read again and every game replayed, in the background on `threads` threads (0: the machine's),
    // the database read and written meanwhile; what is checked is the snapshot of when it starts.
    [[nodiscard]] CheckReport check(CheckProgress& progress, const std::atomic<bool>& cancel, int threads = 0) const;
    // What compact() would give back; nothing to do when it finds no game deleted nor replaced.
    [[nodiscard]] CompactPlan compactPlan() const;
    // The segments that hold games deleted or replaced written again without them, each game keeping its number (a
    // game deleted keeps its headers, an empty game): the segments of versions go. The opening tree stays valid.
    // Unless `one_at_a_time`, the old files are kept until the database is closed, undoCompaction() putting them
    // back while nothing else was committed; one at a time, each segment is committed and its old files removed at
    // once, needing the room of one segment only. The segments are written from the snapshot of when it starts, the
    // other changes going on: games added meanwhile are kept; a game replaced or deleted meanwhile stops it before its
    // commit (its error says so), nothing changed.
    Compacted compact(CompactProgress& progress, const std::atomic<bool>& cancel, bool one_at_a_time = false);
    // Whether undoCompaction() can put the files before the compaction back: nothing committed since.
    [[nodiscard]] bool compactionUndoable() const;
    bool undoCompaction();

    // The segments that cannot be read (Snapshot::unreadable()) written again, in one commit, as games set aside: each
    // game keeps its number, deleted and emptied; the names a segment brought are kept when its names.bin reads, else
    // they stay unknown (read as "?"). The games `damaged` (a check's, in segments that read) are set aside in the
    // same commit. The damaged files are kept until the database is closed: undoKeepReadable() puts them back while
    // nothing else was committed. Gives the games set aside. Throws std::runtime_error when read-only, or when the
    // disk refuses (nothing changed then).
    uint64_t keepReadable(std::span<const GameId> damaged = {});
    [[nodiscard]] bool keepReadableUndoable() const;
    bool undoKeepReadable();

    // The files a compaction left (those it replaced), those keepReadable() did and the segments clear() took out:
    // removed when the database is closed.
    ~Database();
    Database(const Database&)            = delete;
    Database& operator=(const Database&) = delete;

    explicit Database(const std::filesystem::path& dir);  // use create() or open()
    Database(const std::filesystem::path& dir, bool read_only);  // use openReadOnly()

   private:
    // `clean`: what a commit cut short left removed (and what a compaction left). `tries`: read-only, the times it
    // reads again a manifest the other program replaced as a segment it named could not be read.
    void load(bool clean = true, int tries = 4);
    uint32_t nameId(const std::string& name, std::vector<std::string>& added);
    // Writes `games` in new segments (of versions when `versions`) and commits them with the edits `replaced` and
    // `deleted` (the snapshot's own when null) and the table of names `players` (the snapshot's when null), in one
    // manifest. Under the writers' lock, mutex_ taken only for a moment: snapshot() never waits for its writes.
    void commit(std::vector<ImportedGame>& games, const std::vector<uint64_t>& keys, bool versions,
                const std::vector<Snapshot::Version>* replaced, const std::vector<uint64_t>* deleted,
                const PlayerTable* players = nullptr, const std::string& source = {});
    void dropLeftovers();  // a compaction's files and keepReadable()'s, at closing
    // The released segments' files that no snapshot holds any longer removed; closing, all of them.
    void removeReleased(bool closing = false);

    std::filesystem::path dir_;
    std::optional<tour::FolderLock> lock_;  // none when read-only
    // Held by every change for its whole length (a compaction's commit, a read-only database's refresh), before
    // mutex_: the changes one after the other, the readers never waiting for one (an import's commit writes and syncs
    // its files while the window takes a snapshot each frame). current_ changes only under both; the indexes of names
    // and keys are the writers' own, under this lock alone.
    std::mutex write_mutex_;
    std::mutex compact_mutex_;  // one compaction at a time, and its undoing
    // Taken for a moment: current_ read or published, next_segment_, leftovers_, kept_ and dropped_.
    mutable std::mutex mutex_;
    std::shared_ptr<const Snapshot> current_;
    std::unordered_map<std::string, uint32_t> name_ids_;  // filled at the first append
    bool names_indexed_ = false;
    std::vector<uint64_t> known_keys_;  // the games' keys, sorted, filled at the first append that skips duplicates
    bool keys_indexed_     = false;
    uint64_t next_segment_ = 1;
    // The last compaction's: the manifest before it, the generation it committed, the files it left.
    struct Leftovers {
        std::string manifest;
        uint64_t generation = 0;
        std::vector<std::string> files;      // the segments and files it replaced, kept for undoCompaction()
        std::vector<std::string> new_files;  // those it wrote, removed when it is undone
    };
    std::optional<Leftovers> leftovers_;
    std::optional<Leftovers> kept_;     // keepReadable()'s, as a compaction's
    std::vector<std::string> dropped_;  // the files of a keepReadable() before it, no longer undone: removed at closing
    // The segments clear() took out, while snapshots taken before it still read them (under the writers' lock).
    struct Released {
        std::string name;
        std::weak_ptr<const store::Segment> segment;
    };
    std::vector<Released> released_;
};

// For the tests: called at each step of a commit with its name; an exception thrown from it stops the commit there,
// as a crash would. Empty: none.
void setCommitFaultHook(std::function<void(std::string_view step)> hook);

}  // namespace fcg::study
