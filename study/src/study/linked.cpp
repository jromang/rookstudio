#include "linked.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>

#include <json.hpp>
#include <tour/files.hpp>
#include <tour/pgn_file.hpp>
#include <tour/state.hpp>

#include "import.hpp"

namespace fcg::study {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t HEAD_BYTES = 65536;  // a file's first bytes, and the last ones read, tell it was not rewritten
constexpr std::size_t TAIL_BYTES = 4096;
constexpr std::size_t BATCH      = 4096;

uint64_t fnv1a(std::string_view s, uint64_t h = 14695981039346656037ull) {
    for (const unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

// A path's text in UTF-8 (C++20 gives it as char8_t).
std::string utf8(const fs::path& p) {
    const auto u = p.generic_u8string();
    return {reinterpret_cast<const char*>(u.data()), u.size()};
}

std::string hex(uint64_t v) {
    char text[17];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(v));
    return text;
}

// What was read of a source, kept beside its database.
struct ReadState {
    uint64_t games  = 0;  // in the database once it was read: another count means a commit the state did not follow
    uint64_t offset = 0;  // a file: the bytes read
    uint64_t head = 0, tail = 0;                         // a file: hashes of its first bytes and of the last ones read
    std::vector<std::pair<uint64_t, uint64_t>> counted;  // a tournament: (game number, what its text depends on)
};

ReadState loadState(const fs::path& file) {
    ReadState s;
    std::ifstream in(file, std::ios::binary);
    if (!in) return s;
    try {
        const auto j = nlohmann::json::parse(in);
        if (j.value("format", "") != "rookstudio-linked") return s;
        s.games  = j.value("games", uint64_t{0});
        s.offset = j.value("offset", uint64_t{0});
        s.head   = j.value("head", uint64_t{0});
        s.tail   = j.value("tail", uint64_t{0});
        for (const auto& g : j.value("counted", nlohmann::json::array()))
            s.counted.emplace_back(g.at(0).get<uint64_t>(), g.at(1).get<uint64_t>());
    } catch (const std::exception&) {
        return ReadState{};
    }
    return s;
}

void saveState(const fs::path& file, const LinkedSource& source, const ReadState& s) {
    nlohmann::json counted = nlohmann::json::array();
    for (const auto& [number, hash] : s.counted) counted.push_back({number, hash});
    const nlohmann::json j{{"format", "rookstudio-linked"},
                           {"version", 1},
                           {"kind", source.kind == LinkedSource::Kind::PgnFile ? "pgn" : "tournament"},
                           {"source", utf8(source.path)},
                           {"games", s.games},
                           {"offset", s.offset},
                           {"head", s.head},
                           {"tail", s.tail},
                           {"counted", counted}};
    tour::replaceFile(file, j.dump());
}

// Bytes [from, from + size) of a file; fewer at its end.
std::string bytesOf(const fs::path& file, uint64_t from, std::size_t size) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + file.string());
    in.seekg(static_cast<std::streamoff>(from));
    std::string out(size, '\0');
    in.read(out.data(), static_cast<std::streamsize>(size));
    out.resize(static_cast<std::size_t>(in.gcount()));
    return out;
}

uint64_t headOf(const fs::path& file, uint64_t offset) {
    return fnv1a(bytesOf(file, 0, static_cast<std::size_t>(std::min<uint64_t>(offset, HEAD_BYTES))));
}

uint64_t tailOf(const fs::path& file, uint64_t offset) {
    const uint64_t from = offset > TAIL_BYTES ? offset - TAIL_BYTES : 0;
    return fnv1a(bytesOf(file, from, static_cast<std::size_t>(offset - from)));
}

// A plain PGN file, which may grow: not one of the compressed kinds, which are read again whole.
bool plain(const fs::path& file) {
    const auto start = bytesOf(file, 0, 4);
    if (start.size() < 4) return true;
    const auto b         = [&](int i) { return static_cast<unsigned char>(start[static_cast<std::size_t>(i)]); };
    const bool zstd      = b(0) == 0x28 && b(1) == 0xB5 && b(2) == 0x2F && b(3) == 0xFD;
    const bool skippable = (b(0) & 0xF0) == 0x50 && b(1) == 0x2A && b(2) == 0x4D && b(3) == 0x18;
    const bool gzip      = b(0) == 0x1F && b(1) == 0x8B;
    const bool zip       = b(0) == 'P' && b(1) == 'K';
    return !(zstd || skippable || gzip || zip);
}

// A game's text that ends with its result: written whole.
bool whole(std::string_view text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\r' || text.back() == '\t'))
        text.remove_suffix(1);
    for (const std::string_view result : {"1-0", "0-1", "1/2-1/2", "*"})
        if (text.size() >= result.size() && text.substr(text.size() - result.size()) == result) return true;
    return false;
}

}  // namespace

std::string linkedKey(const LinkedSource& source) {
    std::error_code ec;
    auto full = fs::weakly_canonical(fs::absolute(source.path, ec), ec);
    if (ec) full = source.path;
    const char* kind = source.kind == LinkedSource::Kind::PgnFile ? "pgn:" : "tournament:";
    return (source.kind == LinkedSource::Kind::PgnFile ? "file-" : "tour-") + hex(fnv1a(utf8(full), fnv1a(kind)));
}

LinkedDatabase::LinkedDatabase(fs::path cache, std::string key, LinkedSource source, std::shared_ptr<Database> db)
    : cache_(std::move(cache)), key_(std::move(key)), source_(std::move(source)), db_(std::move(db)) {}

std::shared_ptr<LinkedDatabase> LinkedDatabase::open(const fs::path& cache, const LinkedSource& source) {
    const auto key = linkedKey(source);
    std::error_code ec;
    fs::create_directories(cache, ec);
    const auto dir = cache / (key + ".fcgdb");
    auto db        = fs::exists(dir / "manifest.json", ec) ? Database::open(dir) : Database::create(dir);
    auto linked    = std::make_shared<LinkedDatabase>(cache, key, source, std::move(db));
    // A copy with a segment that no longer reads: made again from its source, which has the games.
    if (!linked->db_->snapshot()->unreadable().empty()) linked->rebuild();
    return linked;
}

void LinkedDatabase::rebuild() {
    // Emptied in one commit rather than removed and made again: the window may hold the database, its snapshots and
    // the files they map, and copies the pointer while this runs on the refresh's thread; the generation goes on, so
    // that nothing kept of the games before passes for the new ones'. The note goes after: a crash in between reads
    // the file again whole, the note's count of games no longer the database's.
    db_->clear();
    std::error_code ec;
    fs::remove(cache_ / (key_ + ".json"), ec);
}

LinkedRefresh LinkedDatabase::refresh(std::atomic<bool>& cancel) {
    try {
        return source_.kind == LinkedSource::Kind::PgnFile ? refreshFile(cancel) : refreshTournament(cancel);
    } catch (const std::exception& e) {
        LinkedRefresh r;
        r.error = e.what();
        return r;
    }
}

LinkedRefresh LinkedDatabase::refreshFile(std::atomic<bool>& cancel) {
    LinkedRefresh r;
    const auto state_file = cache_ / (key_ + ".json");
    auto state            = loadState(state_file);
    std::error_code ec;
    const uint64_t size = fs::file_size(source_.path, ec);
    if (ec) throw std::runtime_error("cannot read " + source_.path.string());
    const bool grows = plain(source_.path);
    // Read as it was: the same games in the database, and the bytes read unchanged; a compressed file read whole
    // again when it changed at all.
    const bool kept = state.games == db_->snapshot()->games() && state.offset <= size &&
                      (state.offset == 0 || (headOf(source_.path, state.offset) == state.head &&
                                             tailOf(source_.path, state.offset) == state.tail));
    if (!kept || (!grows && state.offset != 0 && state.offset != size)) {
        if (state.offset > 0 || db_->snapshot()->games() > 0) {
            rebuild();
            r.rebuilt = true;
        }
        state = ReadState{};
    }
    if (state.offset == size) return r;
    if (state.offset == 0) {
        // The whole file, on every core.
        ImportProgress progress;
        const auto imported = importInto(*db_, source_.path, {}, progress, cancel, true);
        if (!imported.error.empty()) throw std::runtime_error(imported.error);
        r.added = imported.added;
        if (cancel) return r;  // read again next time: the state is not saved
        state.offset = size;
    } else {
        // What was appended: its whole games, the last one left for later while it is being written.
        const auto text   = bytesOf(source_.path, state.offset, static_cast<std::size_t>(size - state.offset));
        std::size_t taken = 0;
        std::vector<ImportedGame> batch;
        for (const auto piece : splitGames(text)) {
            if (!whole(piece)) break;
            ImportedGame g;
            if (readImportedGame(piece, g)) batch.push_back(std::move(g));
            taken = static_cast<std::size_t>(piece.data() - text.data()) + piece.size();
            if (batch.size() >= BATCH) {
                r.added += db_->append(std::move(batch), true).added;
                batch.clear();
            }
        }
        if (!batch.empty()) r.added += db_->append(std::move(batch), true).added;
        state.offset += taken;
    }
    state.games = db_->snapshot()->games();
    state.head  = headOf(source_.path, state.offset);
    state.tail  = tailOf(source_.path, state.offset);
    saveState(state_file, source_, state);
    return r;
}

LinkedRefresh LinkedDatabase::refreshTournament(std::atomic<bool>& cancel) {
    LinkedRefresh r;
    const auto state_file = cache_ / (key_ + ".json");
    auto state            = loadState(state_file);
    const auto tournament = tour::TournamentState::open(source_.path, true);
    const auto pgn        = tournament->pgnPath();
    // The games that count, in the order they finished, and what each one's text depends on.
    std::vector<tour::GameResult> counted;
    std::vector<std::pair<uint64_t, uint64_t>> now;
    for (auto& g : tournament->results()) {
        if (g.voided) continue;
        std::string depends = std::to_string(g.pgn.offset) + "/" + std::to_string(g.pgn.length);
        if (g.set) depends += "/" + g.result + "/" + g.reason;
        now.emplace_back(g.number(), fnv1a(depends));
        counted.push_back(std::move(g));
    }
    const bool prefix = state.games == db_->snapshot()->games() && state.counted.size() <= now.size() &&
                        std::equal(state.counted.begin(), state.counted.end(), now.begin());
    if (!prefix) {
        if (!state.counted.empty() || db_->snapshot()->games() > 0) {
            rebuild();
            r.rebuilt = true;
        }
        state = ReadState{};
    }
    std::vector<ImportedGame> batch;
    std::size_t done  = state.counted.size();
    const auto commit = [&] {
        if (batch.empty()) return;
        r.added += db_->append(std::move(batch), true).added;
        batch.clear();
        state.counted.assign(now.begin(), now.begin() + static_cast<std::ptrdiff_t>(done));
        state.games = db_->snapshot()->games();
        saveState(state_file, source_, state);
    };
    for (std::size_t i = state.counted.size(); i < counted.size() && !cancel; ++i) {
        // A game whose text cannot be read is passed over, counted as read so as not to be asked again.
        const auto text = tour::readSpan(pgn, counted[i].pgn);
        ImportedGame g;
        if (text && readImportedGame(tour::countedPgn(*text, counted[i]), g)) batch.push_back(std::move(g));
        ++done;
        if (batch.size() >= BATCH) commit();
    }
    commit();
    if (done == now.size() && (state.counted.size() != now.size() || state.games != db_->snapshot()->games())) {
        state.counted = now;
        state.games   = db_->snapshot()->games();
        saveState(state_file, source_, state);
    }
    return r;
}

}  // namespace fcg::study
