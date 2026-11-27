#pragma once

// The Chess space's databases (mockup M1): those the user made or added, kept in databases.json in the profile, each
// opened when first shown; an import into one of them at a time, then its opening tree built again, both in the
// background at a low priority (study/background.hpp) so that the tournaments' engines keep the machine.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <study/duplicates.hpp>
#include <study/import.hpp>
#include <study/import_into.hpp>
#include <study/linked.hpp>
#include <study/query.hpp>
#include <study/store.hpp>
#include <study/tree.hpp>

#include "jobs.hpp"

namespace fcg {

struct DatabaseEntry {
    std::string name;
    std::filesystem::path folder;  // Name.fcgdb; a linked one's in the profile's cache
    // A tournament's games or a PGN file, read without a copy (study::LinkedDatabase): listed after the user's own
    // databases, never written but by the refresh that brings them up to date.
    std::optional<study::LinkedSource> source;
};

// What the list of databases says of one.
struct DatabaseFacts {
    uint64_t games = 0;      // its games, those deleted left out
    uint32_t names = 0;      // players, events, sites
    uint64_t bytes = 0;      // on the disk
    bool tree      = false;  // its opening tree covers its games
    bool read_only = false;  // another program writes to it: read as it commits, nothing written here
    std::time_t changed = 0;      // its last commit (an import, a game saved), from its manifest; 0 when not known
    std::string error;            // why it could not be opened
    // The games of its segments that could not be read when it was opened (#97): left out of `games`, its
    // maintenance's check says what to do with them.
    uint64_t unreadable = 0;
};

// What the maintenance's check says of a database's damage (mockup M13, board 1): the segments that cannot be read,
// by their numbers; how many damaged games they hold, and the segments that read; the files they were imported from,
// those still there to be imported again.
struct DamageSummary {
    struct Segment {
        std::string number;       // "7" for seg-000007
        std::string why;          // the store's reason, without the database's folder
        bool names_lost = false;  // the names it brought read "?"
        bool missing    = false;  // a file of it is not there (else one changed on the disk)
    };
    std::vector<Segment> segments;
    uint64_t in_segments = 0;                               // the damaged games of those segments
    uint64_t elsewhere   = 0;                               // those of the segments that read
    std::vector<std::pair<std::string, uint64_t>> sources;  // a file (empty: not known) and its damaged games
    std::vector<std::filesystem::path> files;               // those files still there
};
[[nodiscard]] DamageSummary damageSummary(const study::CheckReport& report, const study::Snapshot& snapshot);

// A store's error in the user's language: a phrase for its code (study/store_error.hpp), the store's own text (English,
// with its details) for an error no code names. In lower case without a final stop, for a sentence ("Not done: {}.").
[[nodiscard]] std::string storeErrorText(study::StoreErrc code, const std::string& text);
[[nodiscard]] std::string storeErrorText(const std::exception& e);
[[nodiscard]] std::string storeErrorText(const std::exception_ptr& e);  // a thread's, kept (study::Search::error())
// A store's error that one of the window's jobs received: `said`, the page's sentence for it (storeErrorText()'s words
// in it), returned and added to the log, the store's own text (its details) after it when those words replaced it.
std::string loggedStoreError(std::string said, const std::exception& e);

// The quick filter of a game list, its words typed freely: a year (2024), an ECO code or its start (B90, B9, B), a
// result (1-0, 0-1, 1/2 or ½) are told apart, the other words are looked for in the players, the event and the site.
[[nodiscard]] study::Filter quickFilter(std::string_view text);

class DatabaseShelf {
   public:
    // The databases listed in `profile`'s databases.json; new ones go in its databases folder. Its jobs (the import,
    // the opening tree's build, the linked databases' refreshes) are in `jobs`: one the shelf no longer wants, or that
    // runs when it goes, is cancelled and parked there, never waited for (#105).
    DatabaseShelf(JobGroup& jobs, std::filesystem::path profile);
    // The folders discard() left to an import, which App's jobs waited for: removed.
    ~DatabaseShelf();
    DatabaseShelf(const DatabaseShelf&)            = delete;
    DatabaseShelf& operator=(const DatabaseShelf&) = delete;

    [[nodiscard]] const std::vector<DatabaseEntry>& entries() const noexcept { return entries_; }
    // A new database named `name` (made unique: "Name (2)"), listed last: its index. Throws std::runtime_error.
    std::size_t create(const std::string& name);
    // A database folder made elsewhere, listed last (or where it already is): its index. Throws when it is none.
    std::size_t add(const std::filesystem::path& folder);
    // Out of the list; its files stay. Not a linked one's. Its import is cancelled and parked (the games it committed
    // stay), its opening tree's build cancelled; the builds of the databases after it, running or asked for, keep
    // theirs.
    void remove(std::size_t index);
    // Out of the list, its folder deleted: a database made for an import that brought nothing. While an import into it
    // still ends (parked by remove(), holding the database), its folder goes once the import let go of it.
    void discard(std::size_t index);

    // The linked sources, after the user's own databases: the tournaments, the PGN files opened; those already open
    // are kept.
    void setLinked(const std::vector<std::pair<std::string, study::LinkedSource>>& sources);
    // A linked database brought up to date in the background (a tournament's new games, a file rewritten), once
    // after it is shown; true while it runs.
    void refresh(std::size_t index);
    [[nodiscard]] bool refreshing(std::size_t index);

    // The database, opened at its first call; null when it cannot be (facts().error says why).
    [[nodiscard]] std::shared_ptr<study::Database> open(std::size_t index);
    [[nodiscard]] DatabaseFacts facts(std::size_t index);
    // The database's snapshot and its opening tree when the tree covers it (none else), the tree opened once for a
    // snapshot: what the Board page's tree and reference games read.
    struct TreeSource {
        std::string name;
        std::shared_ptr<const study::Snapshot> snapshot;
        std::shared_ptr<const study::Tree> tree;
    };
    [[nodiscard]] std::optional<TreeSource> treeSource(std::size_t index);

    // An import into a database, one at a time: started, followed, cancelled. After it, the database's opening
    // tree is built again (the tree's own progress), the database open all along. Its job's state: what the shelf
    // keeps of it (its database's place, its file, its start) and what its task tells.
    struct ImportState {
        std::size_t index = 0;  // the shelf's, moved as databases come and go
        std::filesystem::path file;
        std::chrono::steady_clock::time_point started;
        study::ImportProgress progress;
        study::TreeProgress tree;
        std::atomic<bool> looking{false};   // the import done, the same games from other sources looked for
        std::atomic<bool> building{false};  // then the tree being built
        // The groups of duplicates that hold games of the import (M13, board 1): the same game from another source.
        study::DuplicateProgress duplicates_progress;
    };
    using Import = TaskState<ImportState>;  // and its cancel
    // False when one runs already, or the database cannot be opened.
    bool startImport(std::size_t index, const std::filesystem::path& file, const std::string& password = {},
                     std::vector<uint64_t> forget = {});
    [[nodiscard]] Import* import() noexcept { return import_.idle() ? nullptr : &import_.state(); }
    // The import's report once it is over (its tree built), taken once; none while it runs.
    [[nodiscard]] std::optional<study::DatabaseImport> finishedImport();
    // The duplicates the import whose report finishedImport() just gave found, taken once.
    [[nodiscard]] study::Duplicates takeImportDuplicates() { return std::exchange(import_duplicates_, {}); }
    void cancelImport();

    // A database's opening tree built again in the background, when an edit made it stale (a game saved or deleted):
    // one at a time and none during an import, whose own build follows it; asked again while one runs, it runs
    // again after it, the edits made meanwhile in it. treeSource() asks it for a stale tree it finds.
    struct TreeState {
        // The index of a build whose database was removed: cancelled, it ends for no database.
        static constexpr std::size_t REMOVED = ~std::size_t{0};
        std::size_t index = 0;
        study::TreeProgress progress;
    };
    using TreeJob = TaskState<TreeState>;  // and its cancel
    void rebuildTree(std::size_t index);
    // Each frame: the databases another program writes read again when it committed (every 2 s), and opened to be
    // written again once it lets them go (tried every 5 s); the folders discard() left to an import removed once it
    // let go of them (tried every 5 s too).
    void keepReadOnlyCurrent();
    // The build of a database's tree; none while the one of a database removed ends.
    [[nodiscard]] TreeJob* treeJob() noexcept {
        return tree_job_.idle() || tree_job_.state().index == TreeJob::REMOVED ? nullptr : &tree_job_.state();
    }
    // Each frame: a build that ended taken, the next one asked for started. True when one ended.
    bool pollTreeJob();
    void cancelTreeJob();
    // At quit (#98): the import, the tree's build and the linked databases' refreshes cancelled, the build asked for
    // after them forgotten. Each ends within about a second, parked when the shelf goes.
    void cancelAll();

   private:
    // An import's result: its report, and the duplicates it found.
    struct ImportDone {
        study::DatabaseImport report;
        study::Duplicates duplicates;
    };
    void save() const;

    JobGroup& jobs_;
    std::filesystem::path profile_;
    std::vector<DatabaseEntry> entries_;
    std::vector<std::shared_ptr<study::Database>> open_;
    std::vector<std::string> errors_;
    struct CachedFacts {
        uint64_t generation = ~uint64_t{0};
        DatabaseFacts facts;
    };
    std::vector<CachedFacts> facts_;  // refreshed when the database's snapshot changes
    struct Link {
        explicit Link(JobGroup& jobs) : refresh(jobs) {}
        std::shared_ptr<study::LinkedDatabase> db;
        Job<study::LinkedRefresh> refresh;
        bool refreshed = false;  // once since it was listed
    };
    std::vector<Link> links_;  // beside entries_, empty for the user's own
    // The databases discarded while their import still ran: their folders, removed once nothing holds the database.
    struct Discarded {
        std::filesystem::path folder;
        std::weak_ptr<study::Database> db;
    };
    std::vector<Discarded> discarded_;
    void removeDiscarded();
    struct CachedTree {
        uint64_t generation = ~uint64_t{0};
        std::shared_ptr<const study::Tree> tree;
    };
    std::map<std::filesystem::path, CachedTree> trees_;  // by the database's folder, filled by treeSource()
    Job<ImportDone, ImportState> import_;
    study::Duplicates import_duplicates_;
    Job<study::TreeBuilt, TreeState> tree_job_;
    std::chrono::steady_clock::time_point read_only_read_{}, read_only_tried_{};
    std::optional<std::size_t> tree_again_;  // a build asked for while another ran, or during an import
};

// The databases' opening trees at a position (its Polyglot key), as the window's thread reads them (the Board's Tree
// tab, its explanations): their games added move by move, the most played first and those that end there last, and
// each move's games by database. A tree that does not read (#139: its records damaged) adds nothing and is taken out
// of `sources`, so that a caller that keeps them says it once; `error` says why (the page's sentence, also in the log),
// and the other trees answer all the same.
struct TreesAnswer {
    // None when no tree has two games there.
    std::optional<study::TreeAnswer> answer;
    // By move code: each database's name and games.
    std::map<int, std::vector<std::pair<std::string, uint64_t>>> by_database;
    std::string error;
};
[[nodiscard]] TreesAnswer treesAt(std::vector<DatabaseShelf::TreeSource>& sources, uint64_t key);

}  // namespace fcg
