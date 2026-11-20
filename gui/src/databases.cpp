#include "databases.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <system_error>

#include <json.hpp>

#include <study/background.hpp>
#include <tour/files.hpp>

#include "portable.hpp"

namespace fs = std::filesystem;

namespace fcg {

namespace {

fs::path listFile(const fs::path& profile) { return profile / "databases.json"; }

// A folder's name for a database's: its letters, digits, spaces, dashes and underscores, others as underscores.
std::string folderName(const std::string& name) {
    std::string out;
    for (const unsigned char c : name)
        out += (c >= 0x80 || std::isalnum(c) || c == ' ' || c == '-' || c == '_') ? static_cast<char>(c) : '_';
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out.empty() ? std::string("Database") : out;
}

uint64_t folderBytes(const fs::path& dir) {
    uint64_t bytes = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec)) bytes += it->file_size(ec);
    }
    return bytes;
}

std::string trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

}  // namespace

study::Filter quickFilter(std::string_view text) {
    study::Filter filter;
    std::string words;
    std::size_t at = 0;
    while (at < text.size()) {
        const auto start = text.find_first_not_of(" \t", at);
        if (start == std::string_view::npos) break;
        const auto end       = std::min(text.find_first_of(" \t", start), text.size());
        const auto word      = std::string(text.substr(start, end - start));
        at                   = end;
        const bool digits    = std::all_of(word.begin(), word.end(), [](char c) { return c >= '0' && c <= '9'; });
        const bool eco       = word.size() <= 3 && (word[0] >= 'A' && word[0] <= 'E') &&
                               std::all_of(word.begin() + 1, word.end(), [](char c) { return c >= '0' && c <= '9'; });
        const bool eco_lower = word.size() <= 3 && word.size() >= 2 && (word[0] >= 'a' && word[0] <= 'e') &&
                               std::all_of(word.begin() + 1, word.end(), [](char c) { return c >= '0' && c <= '9'; });
        if (digits && word.size() == 4 && word >= "1000" && word <= "2999") {
            filter.date_from = study::packDate(word + ".01.01");
            filter.date_to   = study::packDate(word + ".12.31");
        } else if (eco || eco_lower) {
            std::string code = word;
            code[0]          = static_cast<char>(std::toupper(static_cast<unsigned char>(code[0])));
            const auto low = code + std::string(3 - code.size(), '0'), high = code + std::string(3 - code.size(), '9');
            filter.eco_from = study::packEco(low);
            filter.eco_to   = study::packEco(high);
        } else if (word == "1-0") {
            filter.result = '1';
        } else if (word == "0-1") {
            filter.result = '0';
        } else if (word == "1/2" || word == "½" || word == "1/2-1/2" || word == "½-½") {
            filter.result = '=';
        } else {
            words += (words.empty() ? "" : " ") + word;
        }
    }
    filter.words = words;
    return filter;
}

DatabaseShelf::DatabaseShelf(fs::path profile) : profile_(std::move(profile)) {
    std::ifstream in(listFile(profile_), std::ios::binary);
    if (in) {
        try {
            const auto json = nlohmann::json::parse(in);
            for (const auto& e : json) {
                DatabaseEntry entry;
                entry.name   = e.value("name", std::string{});
                entry.folder = fs::path(portable::resolved(e.value("folder", std::string{})));
                if (!entry.folder.empty()) entries_.push_back(std::move(entry));
            }
        } catch (const nlohmann::json::exception&) {
        }
    }
    open_.resize(entries_.size());
    errors_.resize(entries_.size());
    facts_.resize(entries_.size());
    links_.resize(entries_.size());
}

DatabaseShelf::~DatabaseShelf() {
    if (import_) {
        import_->cancel = true;
        if (import_->done.valid()) import_->done.wait();
    }
    for (auto& link : links_) {
        if (link.cancel) *link.cancel = true;
        if (link.refresh.valid()) link.refresh.wait();
    }
}

void DatabaseShelf::save() const {
    auto json = nlohmann::json::array();
    for (const auto& e : entries_)
        if (!e.source) json.push_back({{"name", e.name}, {"folder", portable::stored(e.folder.string())}});
    tour::replaceFile(listFile(profile_), json.dump(2) + "\n");
}

std::size_t DatabaseShelf::create(const std::string& wanted) {
    auto base = trimmed(wanted);
    if (base.empty()) base = "Database";
    auto name = base;
    for (int n = 2;
         std::any_of(entries_.begin(), entries_.end(), [&](const DatabaseEntry& e) { return e.name == name; }); ++n)
        name = base + " (" + std::to_string(n) + ")";
    const auto dir = profile_ / "databases";
    auto folder    = dir / (folderName(name) + ".fcgdb");
    std::error_code ec;
    for (int n = 2; fs::exists(folder, ec); ++n) folder = dir / (folderName(name) + " " + std::to_string(n) + ".fcgdb");
    fs::create_directories(dir, ec);
    auto db        = study::Database::create(folder);
    const auto own = static_cast<std::ptrdiff_t>(
        std::count_if(entries_.begin(), entries_.end(), [](const DatabaseEntry& e) { return !e.source; }));
    entries_.insert(entries_.begin() + own, {name, folder, std::nullopt});
    open_.insert(open_.begin() + own, std::move(db));
    errors_.insert(errors_.begin() + own, std::string());
    facts_.insert(facts_.begin() + own, CachedFacts{});
    links_.insert(links_.begin() + own, Link{});
    if (import_ && import_->index >= static_cast<std::size_t>(own)) ++import_->index;
    save();
    return static_cast<std::size_t>(own);
}

std::size_t DatabaseShelf::add(const fs::path& folder) {
    std::error_code ec;
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [&](const DatabaseEntry& e) { return fs::equivalent(e.folder, folder, ec); });
    if (it != entries_.end()) return static_cast<std::size_t>(it - entries_.begin());
    if (!fs::is_regular_file(folder / "manifest.json", ec))
        throw std::runtime_error(folder.string() + " is not a database (it has no manifest.json)");
    auto name      = folder.stem().string();
    const auto own = static_cast<std::ptrdiff_t>(
        std::count_if(entries_.begin(), entries_.end(), [](const DatabaseEntry& e) { return !e.source; }));
    entries_.insert(entries_.begin() + own, {name.empty() ? folder.string() : name, folder, std::nullopt});
    open_.insert(open_.begin() + own, nullptr);
    errors_.insert(errors_.begin() + own, std::string());
    facts_.insert(facts_.begin() + own, CachedFacts{});
    links_.insert(links_.begin() + own, Link{});
    if (import_ && import_->index >= static_cast<std::size_t>(own)) ++import_->index;
    save();
    return static_cast<std::size_t>(own);
}

void DatabaseShelf::remove(std::size_t index) {
    if (index >= entries_.size() || entries_[index].source) return;
    if (import_ && import_->index == index) {
        import_->cancel = true;
        if (import_->done.valid()) import_->done.wait();
        import_.reset();
    } else if (import_ && import_->index > index) {
        --import_->index;
    }
    const auto at = static_cast<std::ptrdiff_t>(index);
    entries_.erase(entries_.begin() + at);
    open_.erase(open_.begin() + at);
    errors_.erase(errors_.begin() + at);
    facts_.erase(facts_.begin() + at);
    links_.erase(links_.begin() + at);
    save();
}

void DatabaseShelf::discard(std::size_t index) {
    if (index >= entries_.size() || entries_[index].source) return;
    const auto folder = entries_[index].folder;
    trees_.erase(folder);
    remove(index);  // closed: its lock let go
    std::error_code ec;
    fs::remove_all(folder, ec);
}

void DatabaseShelf::setLinked(const std::vector<std::pair<std::string, study::LinkedSource>>& sources) {
    // The user's own first, as they are; then the sources, those listed already keeping what they hold.
    std::vector<DatabaseEntry> entries;
    std::vector<std::shared_ptr<study::Database>> open;
    std::vector<std::string> errors;
    std::vector<CachedFacts> facts;
    std::vector<Link> links;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].source) continue;
        entries.push_back(std::move(entries_[i]));
        open.push_back(std::move(open_[i]));
        errors.push_back(std::move(errors_[i]));
        facts.push_back(facts_[i]);
        links.push_back(std::move(links_[i]));
    }
    const auto cache = profile_ / "cache" / "linked";
    for (const auto& [name, source] : sources) {
        const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const DatabaseEntry& e) {
            return e.source && e.source->kind == source.kind && e.source->path == source.path;
        });
        if (it != entries_.end()) {
            const auto i = static_cast<std::size_t>(it - entries_.begin());
            entries.push_back(std::move(entries_[i]));
            entries.back().name = name;
            open.push_back(std::move(open_[i]));
            errors.push_back(std::move(errors_[i]));
            facts.push_back(facts_[i]);
            links.push_back(std::move(links_[i]));
        } else {
            entries.push_back({name, cache / (study::linkedKey(source) + ".fcgdb"), source});
            open.emplace_back();
            errors.emplace_back();
            facts.emplace_back();
            links.emplace_back();
        }
    }
    // Those no longer listed: their refresh stopped.
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (i < links_.size() && links_[i].refresh.valid()) {
            if (links_[i].cancel) *links_[i].cancel = true;
            links_[i].refresh.wait();
        }
    }
    entries_ = std::move(entries);
    open_    = std::move(open);
    errors_  = std::move(errors);
    facts_   = std::move(facts);
    links_   = std::move(links);
}

void DatabaseShelf::refresh(std::size_t index) {
    if (index >= entries_.size() || !entries_[index].source) return;
    auto& link = links_[index];
    if (link.refreshed || link.refresh.valid()) return;
    if (!open(index) || !link.db) return;
    link.refreshed = true;
    link.cancel    = std::make_shared<std::atomic<bool>>(false);
    link.refresh   = std::async(std::launch::async, [db = link.db, cancel = link.cancel] {
        study::enterBackground();
        return db->refresh(*cancel);
    });
}

bool DatabaseShelf::refreshing(std::size_t index) {
    if (index >= links_.size() || !links_[index].refresh.valid()) return false;
    auto& link = links_[index];
    if (link.refresh.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return true;
    const auto done = link.refresh.get();
    if (!done.error.empty()) errors_[index] = done.error;
    facts_[index].generation = ~uint64_t{0};
    if (done.rebuilt) open_[index] = link.db->database();  // a database read again from the start
    return false;
}

std::shared_ptr<study::Database> DatabaseShelf::open(std::size_t index) {
    if (index >= entries_.size()) return nullptr;
    if (!open_[index] && errors_[index].empty()) {
        try {
            if (const auto& source = entries_[index].source) {
                links_[index].db = study::LinkedDatabase::open(profile_ / "cache" / "linked", *source);
                open_[index]     = links_[index].db->database();
            } else {
                try {
                    open_[index] = study::Database::open(entries_[index].folder);
                } catch (const tour::FolderBusy&) {
                    // Another program writes to it (another RookStudio importing): read as it commits.
                    open_[index] = study::Database::openReadOnly(entries_[index].folder);
                }
            }
        } catch (const std::exception& e) {
            errors_[index] = e.what();
        }
    }
    if (entries_[index].source && links_[index].db) open_[index] = links_[index].db->database();
    return open_[index];
}

DatabaseFacts DatabaseShelf::facts(std::size_t index) {
    DatabaseFacts out;
    const auto db = open(index);
    if (!db) {
        out.error = index < errors_.size() ? errors_[index] : std::string();
        return out;
    }
    const auto snapshot = db->snapshot();
    auto& cached        = facts_[index];
    if (cached.generation == snapshot->generation() && cached.facts.read_only == db->readOnly()) return cached.facts;
    out.read_only = db->readOnly();
    out.games     = snapshot->games() - snapshot->deletedCount();
    out.names = snapshot->names();
    out.bytes = folderBytes(entries_[index].folder);
    std::error_code ec;
    const auto written = std::filesystem::last_write_time(entries_[index].folder / "manifest.json", ec);
    // The file clock's duration is not the system clock's everywhere (libc++ counts it in __int128 nanoseconds).
    if (!ec)
        out.changed =
            std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                std::chrono::file_clock::to_sys(written)));
    try {
        const auto tree = study::Tree::open(entries_[index].folder);
        out.tree        = tree && !tree->stale(*snapshot) && tree->games() == snapshot->games();
    } catch (const std::exception&) {
        out.tree = false;
    }
    cached = {snapshot->generation(), out};
    return out;
}

std::optional<DatabaseShelf::TreeSource> DatabaseShelf::treeSource(std::size_t index) {
    const auto db = open(index);
    if (!db) return std::nullopt;
    const auto snapshot = db->snapshot();
    auto& cached        = trees_[entries_[index].folder];
    if (cached.generation != snapshot->generation() || (!cached.tree && !entries_[index].source)) {
        cached.generation = snapshot->generation();
        cached.tree.reset();
        if (!entries_[index].source) {
            try {
                auto tree = study::Tree::open(entries_[index].folder);
                if (tree && !tree->stale(*snapshot))
                    cached.tree = std::move(tree);
                else if (tree && !(tree_job_ && tree_job_->index == index) && tree_again_ != index)
                    rebuildTree(index);  // a game saved or deleted since: built again in the background
            } catch (const std::exception&) {
            }
        }
    }
    return TreeSource{entries_[index].name, snapshot, cached.tree};
}

bool DatabaseShelf::startImport(std::size_t index, const fs::path& file, const std::string& password) {
    if (import_ || index >= entries_.size() || entries_[index].source) return false;
    const auto db = open(index);
    if (!db || db->readOnly()) return false;
    import_          = std::make_unique<Import>();
    import_->index   = index;
    import_->file    = file;
    import_->started = std::chrono::steady_clock::now();
    Import* job      = import_.get();
    job->done        = std::async(std::launch::async, [db, job, password] {
        study::enterBackground();
        study::ChessBaseOptions chessbase;
        chessbase.password = password;
        const auto report = study::importInto(*db, job->file, study::ImportOptions{}, job->progress, job->cancel, false,
                                              std::size_t{1} << 18, chessbase);
        // The opening tree of every game committed, a cancelled import's too, unless the import was cancelled.
        if (!job->cancel && report.added > 0) {
            job->building = true;
            study::buildTree(*db, *db->snapshot(), study::TreeOptions{}, job->tree, job->cancel);
        }
        return report;
    });
    return true;
}

std::optional<study::DatabaseImport> DatabaseShelf::finishedImport() {
    if (!import_ || !import_->done.valid()) return std::nullopt;
    if (import_->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return std::nullopt;
    auto report = import_->done.get();
    if (import_->index < facts_.size()) facts_[import_->index].generation = ~uint64_t{0};  // its tree now
    import_.reset();
    return report;
}

void DatabaseShelf::cancelImport() {
    if (import_) import_->cancel = true;
}

void DatabaseShelf::keepReadOnlyCurrent() {
    const auto now  = std::chrono::steady_clock::now();
    const bool read = now - read_only_read_ >= std::chrono::seconds(2);
    const bool try_ = now - read_only_tried_ >= std::chrono::seconds(5);
    if (!read && !try_) return;
    if (read) read_only_read_ = now;
    if (try_) read_only_tried_ = now;
    for (std::size_t i = 0; i < open_.size() && i < entries_.size(); ++i) {
        if (!open_[i] || !open_[i]->readOnly() || entries_[i].source) continue;
        if (try_) {
            try {
                open_[i] = study::Database::open(entries_[i].folder);  // let go: written again here
                trees_.erase(entries_[i].folder);
                continue;
            } catch (const std::exception&) {
            }
        }
        if (read && open_[i]->refresh()) trees_.erase(entries_[i].folder);
    }
}

void DatabaseShelf::rebuildTree(std::size_t index) {
    if (index >= entries_.size() || entries_[index].source) return;
    if (const auto db = open(index); !db || db->readOnly()) return;  // the other program builds it
    if (import_ || tree_job_) {  // after it: a build running started from the snapshot before this edit
        tree_again_ = index;
        return;
    }
    const auto db = open(index);
    if (!db) return;
    tree_job_        = std::make_unique<TreeJob>();
    tree_job_->index = index;
    TreeJob* job     = tree_job_.get();
    job->done        = std::async(std::launch::async, [db, job] {
        study::enterBackground();
        return study::buildTree(*db, *db->snapshot(), study::TreeOptions{}, job->progress, job->cancel);
    });
}

bool DatabaseShelf::pollTreeJob() {
    bool ended = false;
    if (tree_job_ && tree_job_->done.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        (void)tree_job_->done.get();
        if (tree_job_->index < facts_.size()) facts_[tree_job_->index].generation = ~uint64_t{0};  // its tree now
        if (tree_job_->index < entries_.size()) trees_.erase(entries_[tree_job_->index].folder);
        tree_job_.reset();
        ended = true;
    }
    if (!tree_job_ && !import_ && tree_again_) {
        const auto index = *tree_again_;
        tree_again_.reset();
        rebuildTree(index);
    }
    return ended;
}

void DatabaseShelf::cancelTreeJob() {
    if (tree_job_) tree_job_->cancel = true;
    tree_again_.reset();
}

}  // namespace fcg
