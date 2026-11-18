#pragma once

// The Chess space's databases (mockup M1): those the user made or added, kept in databases.json in the profile, each
// opened when first shown; an import into one of them at a time, then its opening tree built again, both in the
// background at a low priority (study/background.hpp) so that the tournaments' engines keep the machine.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <study/import.hpp>
#include <study/query.hpp>
#include <study/store.hpp>
#include <study/tree.hpp>

namespace fcg {

struct DatabaseEntry {
    std::string name;
    std::filesystem::path folder;  // Name.fcgdb
};

// What the list of databases says of one.
struct DatabaseFacts {
    uint64_t games = 0;      // its games, those deleted left out
    uint32_t names = 0;      // players, events, sites
    uint64_t bytes = 0;      // on the disk
    bool tree      = false;  // its opening tree covers its games
    std::string error;       // why it could not be opened
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
    // Out of the list; its files stay.
    void remove(std::size_t index);

    // The database, opened at its first call; null when it cannot be (facts().error says why).
    [[nodiscard]] std::shared_ptr<study::Database> open(std::size_t index);
    [[nodiscard]] DatabaseFacts facts(std::size_t index);

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
    std::unique_ptr<Import> import_;
};

}  // namespace fcg
