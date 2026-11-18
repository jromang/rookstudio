#pragma once

// The Chess space's databases (mockup M1): those the user made or added, kept in databases.json in the profile, each
// opened when first shown; an import into one of them at a time, then its opening tree built again, both in the
// background at a low priority (study/background.hpp) so that the tournaments' engines keep the machine.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <study/import.hpp>
#include <study/linked.hpp>
#include <study/query.hpp>
#include <study/store.hpp>
#include <study/tree.hpp>

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
};

// The quick filter of a game list, its words typed freely: a year (2024), an ECO code or its start (B90, B9, B), a
// result (1-0, 0-1, 1/2 or ½) are told apart, the other words are looked for in the players, the event and the site.
[[nodiscard]] study::Filter quickFilter(std::string_view text);

class DatabaseShelf {
   public:
    // The databases listed in `profile`'s databases.json; new ones go in its databases folder.
    explicit DatabaseShelf(std::filesystem::path profile);
    ~DatabaseShelf();
    DatabaseShelf(const DatabaseShelf&)            = delete;
    DatabaseShelf& operator=(const DatabaseShelf&) = delete;

    [[nodiscard]] const std::vector<DatabaseEntry>& entries() const noexcept { return entries_; }
    // A new database named `name` (made unique: "Name (2)"), listed last: its index. Throws std::runtime_error.
    std::size_t create(const std::string& name);
    // A database folder made elsewhere, listed last (or where it already is): its index. Throws when it is none.
    std::size_t add(const std::filesystem::path& folder);
    // Out of the list; its files stay. Not a linked one's.
    void remove(std::size_t index);

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
    // tree is built again (the tree's own progress), the database open all along.
    struct Import {
        std::size_t index = 0;
        std::filesystem::path file;
        study::ImportProgress progress;
        study::TreeProgress tree;
        std::atomic<bool> cancel{false};
        std::atomic<bool> building{false};  // the import done, the tree being built
        std::chrono::steady_clock::time_point started;
        std::future<study::DatabaseImport> done;
    };
    // False when one runs already, or the database cannot be opened.
    bool startImport(std::size_t index, const std::filesystem::path& file);
    [[nodiscard]] Import* import() noexcept { return import_.get(); }
    // The import's report once it is over (its tree built), taken once; none while it runs.
    [[nodiscard]] std::optional<study::DatabaseImport> finishedImport();
    void cancelImport();

    // A database's opening tree built again in the background, when an edit made it stale (a game saved or deleted):
    // one at a time and none during an import, whose own build follows it; asked again while one runs, it runs
    // again after it, the edits made meanwhile in it. treeSource() asks it for a stale tree it finds.
    struct TreeJob {
        std::size_t index = 0;
        study::TreeProgress progress;
        std::atomic<bool> cancel{false};
        std::future<study::TreeBuilt> done;
    };
    void rebuildTree(std::size_t index);
    // Each frame: the databases another program writes read again when it committed (every 2 s), and opened to be
    // written again once it lets them go (tried every 5 s).
    void keepReadOnlyCurrent();
    [[nodiscard]] TreeJob* treeJob() noexcept { return tree_job_.get(); }
    // Each frame: a build that ended taken, the next one asked for started. True when one ended.
    bool pollTreeJob();
    void cancelTreeJob();

   private:
    void save() const;

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
        std::shared_ptr<study::LinkedDatabase> db;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::future<study::LinkedRefresh> refresh;
        bool refreshed = false;  // once since it was listed
    };
    std::vector<Link> links_;  // beside entries_, empty for the user's own
    struct CachedTree {
        uint64_t generation = ~uint64_t{0};
        std::shared_ptr<const study::Tree> tree;
    };
    std::map<std::filesystem::path, CachedTree> trees_;  // by the database's folder, filled by treeSource()
    std::unique_ptr<Import> import_;
    std::unique_ptr<TreeJob> tree_job_;
    std::chrono::steady_clock::time_point read_only_read_{}, read_only_tried_{};
    std::optional<std::size_t> tree_again_;  // a build asked for while another ran, or during an import
};

}  // namespace fcg
