#pragma once

// A PGN file, or a tournament of the Engines space, read as a database without being copied where the user keeps
// things: its games kept in a database of the program's cache, brought up to date with what was appended since (the
// games a running tournament finishes, those added at the end of a file), built again when what was read before
// changed (a file rewritten, a tournament's game voided or its result set by hand). The bridge between the two
// spaces: the list, the searches and the opening tree on engine games.
//
//   <cache>/<key>.fcgdb   the database
//   <cache>/<key>.json    what it was read from: the source, and how far

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "store.hpp"

namespace fcg::study {

struct LinkedSource {
    enum class Kind { PgnFile, Tournament };
    Kind kind = Kind::PgnFile;
    std::filesystem::path path;  // the file, or the tournament's folder
};

struct LinkedRefresh {
    uint64_t added = 0;      // games added
    bool rebuilt   = false;  // read again from the start
    std::string error;       // why it could not be brought up to date; the database stays as it was
};

class LinkedDatabase {
   public:
    // The database of `source` in `cache` (a folder of the program's), made when it is not there yet, or again when a
    // segment of it no longer reads; refresh() fills it. Throws std::runtime_error when the cache cannot hold it,
    // tour::FolderBusy when another program writes it.
    static std::shared_ptr<LinkedDatabase> open(const std::filesystem::path& cache, const LinkedSource& source);

    // Brings the database up to date with its source, as above. A cancel keeps what was committed; a refusal of the
    // disk sets `cancel` too, as importInto() does.
    LinkedRefresh refresh(std::atomic<bool>& cancel);

    // The database, a copy of the pointer for the caller to hold: always the same one, which refresh() changes by
    // commits only, on its own thread while the window reads it (#100). Read again from the start, it is emptied in
    // one commit (Database::clear()): a snapshot taken before keeps its games, and their files stay until none holds
    // them.
    [[nodiscard]] std::shared_ptr<Database> database() const noexcept { return db_; }
    [[nodiscard]] const LinkedSource& source() const noexcept { return source_; }

    LinkedDatabase(std::filesystem::path cache, std::string key, LinkedSource source,
                   std::shared_ptr<Database> db);  // use open()

   private:
    LinkedRefresh refreshFile(std::atomic<bool>& cancel);
    LinkedRefresh refreshTournament(std::atomic<bool>& cancel);
    void rebuild();  // the database emptied, and the note of what was read gone

    std::filesystem::path cache_;
    std::string key_;
    LinkedSource source_;
    const std::shared_ptr<Database> db_;  // never another: no lock needed to copy it
};

// The name of a source's files in the cache: a hash of its kind and its full path.
[[nodiscard]] std::string linkedKey(const LinkedSource& source);

}  // namespace fcg::study
