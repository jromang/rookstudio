#include "databases.hpp"

#include <algorithm>
#include <cctype>
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
}

DatabaseShelf::~DatabaseShelf() {
    if (import_) {
        import_->cancel = true;
        if (import_->done.valid()) import_->done.wait();
    }
}

void DatabaseShelf::save() const {
    auto json = nlohmann::json::array();
    for (const auto& e : entries_) json.push_back({{"name", e.name}, {"folder", portable::stored(e.folder.string())}});
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
    auto db = study::Database::create(folder);
    entries_.push_back({name, folder});
    open_.push_back(std::move(db));
    errors_.emplace_back();
    facts_.emplace_back();
    save();
    return entries_.size() - 1;
}

std::size_t DatabaseShelf::add(const fs::path& folder) {
    std::error_code ec;
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [&](const DatabaseEntry& e) { return fs::equivalent(e.folder, folder, ec); });
    if (it != entries_.end()) return static_cast<std::size_t>(it - entries_.begin());
    if (!fs::is_regular_file(folder / "manifest.json", ec))
        throw std::runtime_error(folder.string() + " is not a database (it has no manifest.json)");
    auto name = folder.stem().string();
    entries_.push_back({name.empty() ? folder.string() : name, folder});
    open_.emplace_back();
    errors_.emplace_back();
    facts_.emplace_back();
    save();
    return entries_.size() - 1;
}

void DatabaseShelf::remove(std::size_t index) {
    if (index >= entries_.size()) return;
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
    save();
}

std::shared_ptr<study::Database> DatabaseShelf::open(std::size_t index) {
    if (index >= entries_.size()) return nullptr;
    if (!open_[index] && errors_[index].empty()) {
        try {
            open_[index] = study::Database::open(entries_[index].folder);
        } catch (const std::exception& e) {
            errors_[index] = e.what();
        }
    }
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
    if (cached.generation == snapshot->generation()) return cached.facts;
    out.games = snapshot->games() - snapshot->deletedCount();
    out.names = snapshot->names();
    out.bytes = folderBytes(entries_[index].folder);
    try {
        const auto tree = study::Tree::open(entries_[index].folder);
        out.tree        = tree && !tree->stale(*snapshot) && tree->games() == snapshot->games();
    } catch (const std::exception&) {
        out.tree = false;
    }
    cached = {snapshot->generation(), out};
    return out;
}

bool DatabaseShelf::startImport(std::size_t index, const fs::path& file) {
    if (import_) return false;
    const auto db = open(index);
    if (!db) return false;
    import_          = std::make_unique<Import>();
    import_->index   = index;
    import_->file    = file;
    import_->started = std::chrono::steady_clock::now();
    Import* job      = import_.get();
    job->done        = std::async(std::launch::async, [db, job] {
        study::enterBackground();
        const auto report = study::importInto(*db, job->file, study::ImportOptions{}, job->progress, job->cancel);
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

}  // namespace fcg
