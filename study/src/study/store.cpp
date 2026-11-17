#include "store.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>

#include <json.hpp>

#include "mapped_file.hpp"

#ifndef _WIN32
#    include <fcntl.h>
#    include <unistd.h>
#endif

namespace fcg::study {

namespace fs = std::filesystem;

namespace {

constexpr std::array<char, 8> MAGIC{'F', 'C', 'G', 'S', 'E', 'G', '1', '\0'};
constexpr uint32_t VERSION        = 1;
constexpr const char* MANIFEST    = "manifest.json";
constexpr std::size_t HEADER_SIZE = 32;

std::function<void(std::string_view)> g_fault_hook;
void step(std::string_view name) {
    if (g_fault_hook) g_fault_hook(name);
}

// The header columns of a segment of n games, each 8-byte aligned, in this order.
enum Column : int {
    WHITE,
    BLACK,
    EVENT,
    SITE,
    ROUND,
    ANNOTATOR,
    TIME_CONTROL,  // names (u32)
    DATE,
    PLIES,  // u32
    WHITE_ELO,
    BLACK_ELO,
    ECO,
    FLAGS,   // u16
    RESULT,  // u8
    KEY,     // u64: duplicateKey()
    MOVES,   // u64, n + 1 offsets
    ANNEX,   // u64, n + 1 offsets
    COLUMNS
};
constexpr int NAME_COLUMNS = 7;
constexpr std::array<std::string_view, NAME_COLUMNS> NAME_TAGS{"White", "Black",     "Event",      "Site",
                                                               "Round", "Annotator", "TimeControl"};

std::size_t columnBytes(int column, std::size_t n) {
    if (column <= PLIES) return 4 * n;
    if (column <= FLAGS) return 2 * n;
    if (column == RESULT) return n;
    if (column == KEY) return 8 * n;
    return 8 * (n + 1);
}

std::array<std::size_t, COLUMNS + 1> layout(std::size_t n) {
    std::array<std::size_t, COLUMNS + 1> at{};
    std::size_t p = HEADER_SIZE;
    for (int c = 0; c < COLUMNS; ++c) {
        at[static_cast<std::size_t>(c)] = p;
        p += (columnBytes(c, n) + 7) & ~std::size_t{7};
    }
    at[COLUMNS] = p;
    return at;
}

template <typename T>
T readAt(const uint8_t* base, std::size_t offset, std::size_t i) {
    T v;
    std::memcpy(&v, base + offset + i * sizeof(T), sizeof(T));
    return v;
}

void putVarint(std::string& out, uint64_t v) {
    while (v >= 0x80) {
        out += static_cast<char>(0x80 | (v & 0x7F));
        v >>= 7;
    }
    out += static_cast<char>(v);
}
uint64_t getVarint(const uint8_t*& p, const uint8_t* end) {
    uint64_t v = 0;
    for (int shift = 0; p < end && shift < 64; shift += 7) {
        const uint8_t b = *p++;
        v |= uint64_t{b & 0x7Fu} << shift;
        if (!(b & 0x80)) return v;
    }
    throw std::runtime_error("damaged annex record");
}
void putText(std::string& out, std::string_view s) {
    putVarint(out, s.size());
    out.append(s);
}
std::string getText(const uint8_t*& p, const uint8_t* end) {
    const auto size = getVarint(p, end);
    if (size > static_cast<uint64_t>(end - p)) throw std::runtime_error("damaged annex record");
    std::string s(reinterpret_cast<const char*>(p), static_cast<std::size_t>(size));
    p += size;
    return s;
}
uint64_t zigzag(int64_t v) { return (static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63); }
int64_t unzigzag(uint64_t v) { return static_cast<int64_t>(v >> 1) ^ -static_cast<int64_t>(v & 1); }

// The annex: what the columns do not hold.
enum AnnexPart : uint8_t { ANNEX_FEN = 1, ANNEX_TAGS = 2, ANNEX_TEXT = 4, ANNEX_CLOCKS = 8, ANNEX_EVALS = 16 };

// A rating as the columns keep it: 1 to 65535, written as such; 0 for anything else.
uint16_t number16(std::string_view s) {
    unsigned v         = 0;
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    const bool ok      = ec == std::errc() && p == s.data() + s.size() && v > 0 && v < 65536 && std::to_string(v) == s;
    return ok ? static_cast<uint16_t>(v) : uint16_t{0};
}

void syncFolder([[maybe_unused]] const fs::path& dir) {
#ifndef _WIN32
    const int fd = ::open(dir.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#endif
}

void writeFile(const fs::path& file, std::string_view bytes) {
    step("write " + file.filename().string());
    // Closed however this ends: Windows removes no folder whose files are open.
    const std::unique_ptr<std::FILE, int (*)(std::FILE*)> f(tour::openFile(file, "wb"), &std::fclose);
    if (!f) throw std::runtime_error("cannot write " + file.string());
    const bool written = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), f.get()) == bytes.size();
    step("sync " + file.filename().string());
    if (!written || !tour::syncFile(f.get()))
        throw std::runtime_error("cannot write " + file.string() + " (is the disk full?)");
}

std::string segmentName(uint64_t number) {
    char name[32];
    std::snprintf(name, sizeof name, "seg-%06llu", static_cast<unsigned long long>(number));
    return name;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------

class Segment {
   public:
    Segment(const fs::path& dir, uint32_t expected_games, uint32_t expected_names)
        : cols_(dir / "cols.bin"), moves_(dir / "moves.bin"), annex_(dir / "annex.bin"), names_(dir / "names.bin") {
        const auto* c = cols_.data();
        if (cols_.size() < HEADER_SIZE || std::memcmp(c, MAGIC.data(), MAGIC.size()) != 0)
            throw std::runtime_error(dir.string() + ": not a segment");
        if (readAt<uint32_t>(c, 8, 0) != VERSION) throw std::runtime_error(dir.string() + ": a newer format");
        games_      = readAt<uint32_t>(c, 12, 0);
        name_count_ = readAt<uint32_t>(c, 16, 0);
        if (games_ != expected_games || name_count_ != expected_names)
            throw std::runtime_error(dir.string() + ": not the segment the manifest names");
        at_ = layout(games_);
        if (cols_.size() != at_[COLUMNS]) throw std::runtime_error(dir.string() + ": damaged columns");
        if (readAt<uint64_t>(c, at_[MOVES], games_) != moves_.size() ||
            readAt<uint64_t>(c, at_[ANNEX], games_) != annex_.size())
            throw std::runtime_error(dir.string() + ": damaged games");
        name_offsets_.reserve(name_count_ + 1);
        name_offsets_.push_back(0);
        for (std::size_t i = 0; i < names_.size(); ++i) {
            if (names_.data()[i] == '\n') name_offsets_.push_back(static_cast<uint32_t>(i + 1));
        }
        if (name_offsets_.size() != name_count_ + std::size_t{1})
            throw std::runtime_error(dir.string() + ": damaged names");
    }

    [[nodiscard]] uint32_t games() const noexcept { return games_; }
    [[nodiscard]] uint32_t nameCount() const noexcept { return name_count_; }
    [[nodiscard]] std::string_view name(uint32_t local) const {
        const auto from = name_offsets_[local], to = name_offsets_[local + 1] - 1;
        return {reinterpret_cast<const char*>(names_.data()) + from, to - from};
    }
    template <typename T>
    [[nodiscard]] T value(Column column, std::size_t i) const {
        return readAt<T>(cols_.data(), at_[column], i);
    }
    [[nodiscard]] std::span<const uint8_t> moves(std::size_t i) const {
        const auto from = value<uint64_t>(MOVES, i), to = value<uint64_t>(MOVES, i + 1);
        return {moves_.data() + from, static_cast<std::size_t>(to - from)};
    }
    [[nodiscard]] std::span<const uint8_t> annex(std::size_t i) const {
        const auto from = value<uint64_t>(ANNEX, i), to = value<uint64_t>(ANNEX, i + 1);
        return {annex_.data() + from, static_cast<std::size_t>(to - from)};
    }

   private:
    MappedFile cols_, moves_, annex_, names_;
    uint32_t games_      = 0;
    uint32_t name_count_ = 0;
    std::array<std::size_t, COLUMNS + 1> at_{};
    std::vector<uint32_t> name_offsets_;
};

// ---------------------------------------------------------------------------------------------------------------------

uint32_t packDate(std::string_view s) {
    // "2026.11.17", "1972.??.??", "2026.11" — each part a number or not known.
    const auto part = [&](std::size_t from, std::size_t length) -> uint32_t {
        if (from + length > s.size()) return 0;
        unsigned v         = 0;
        const auto [p, ec] = std::from_chars(s.data() + from, s.data() + from + length, v);
        return ec == std::errc() && p == s.data() + from + length ? v : 0;
    };
    const auto year = part(0, 4), month = part(5, 2), day = part(8, 2);
    if (year > 9999 || month > 12 || day > 31) return 0;
    return year << 9 | month << 5 | day;
}

std::string dateText(uint32_t date) {
    const auto year = date >> 9, month = (date >> 5) & 15, day = date & 31;
    std::string out = year ? std::to_string(year) : "????";
    while (out.size() < 4) out.insert(0, "0");
    const auto two = [](uint32_t v) { return v ? (v < 10 ? "0" : "") + std::to_string(v) : std::string("??"); };
    return out + "." + two(month) + "." + two(day);
}

uint16_t packEco(std::string_view eco) {
    if (eco.size() != 3 || eco[0] < 'A' || eco[0] > 'E' || eco[1] < '0' || eco[1] > '9' || eco[2] < '0' || eco[2] > '9')
        return 0;
    return static_cast<uint16_t>(1 + (eco[0] - 'A') * 100 + (eco[1] - '0') * 10 + (eco[2] - '0'));
}

std::string ecoText(uint16_t eco) {
    if (eco == 0 || eco > 500) return {};
    const int i = eco - 1;
    return std::string{static_cast<char>('A' + i / 100), static_cast<char>('0' + (i / 10) % 10),
                       static_cast<char>('0' + i % 10)};
}

void setCommitFaultHook(std::function<void(std::string_view)> hook) { g_fault_hook = std::move(hook); }

uint64_t duplicateKey(const ImportedGame& game) {
    // FNV-1a over the parts, each ended by a byte no text holds.
    uint64_t h     = 0xcbf29ce484222325ULL;
    const auto mix = [&](std::string_view bytes) {
        for (const unsigned char c : bytes) h = (h ^ c) * 0x100000001b3ULL;
        h = (h ^ 0xFF) * 0x100000001b3ULL;
    };
    const auto tag = [&](std::string_view name) -> std::string_view {
        for (const auto& [k, v] : game.tags) {
            if (k == name) return v;
        }
        return {};
    };
    mix(tag("Event"));
    mix(tag("Site"));
    mix(tag("Round"));
    mix(tag("White"));
    mix(tag("Black"));
    mix(tag("Date"));
    mix(game.start_fen);
    mix(std::string_view(reinterpret_cast<const char*>(game.moves.data()), game.moves.size()));
    mix(std::string_view(&game.result, 1));
    return h;
}

// ---------------------------------------------------------------------------------------------------------------------

std::pair<const Segment*, std::size_t> Snapshot::locate(GameId id) const {
    if (id >= games_) throw std::out_of_range("no such game");
    const auto it = std::upper_bound(first_game_.begin(), first_game_.end(), id);
    const auto s  = static_cast<std::size_t>(it - first_game_.begin()) - 1;
    return {segments_[s].get(), static_cast<std::size_t>(id - first_game_[s])};
}

std::string_view Snapshot::name(uint32_t id) const {
    // The segment that added it: the last whose first name is at most id among those that added some.
    for (std::size_t s = segments_.size(); s-- > 0;) {
        if (segments_[s]->nameCount() > 0 && first_name_[s] <= id) return segments_[s]->name(id - first_name_[s]);
    }
    return {};
}

HeaderRow Snapshot::row(GameId id) const {
    const auto [segment, i] = locate(id);
    HeaderRow row;
    row.white        = name(segment->value<uint32_t>(WHITE, i));
    row.black        = name(segment->value<uint32_t>(BLACK, i));
    row.event        = name(segment->value<uint32_t>(EVENT, i));
    row.site         = name(segment->value<uint32_t>(SITE, i));
    row.round        = name(segment->value<uint32_t>(ROUND, i));
    row.annotator    = name(segment->value<uint32_t>(ANNOTATOR, i));
    row.time_control = name(segment->value<uint32_t>(TIME_CONTROL, i));
    row.date         = segment->value<uint32_t>(DATE, i);
    row.plies        = segment->value<uint32_t>(PLIES, i);
    row.white_elo    = segment->value<uint16_t>(WHITE_ELO, i);
    row.black_elo    = segment->value<uint16_t>(BLACK_ELO, i);
    row.eco          = segment->value<uint16_t>(ECO, i);
    row.flags        = segment->value<uint16_t>(FLAGS, i);
    row.result       = static_cast<char>(segment->value<uint8_t>(RESULT, i));
    return row;
}

Snapshot::NameIds Snapshot::rowIds(GameId id) const {
    const auto [segment, i] = locate(id);
    return {segment->value<uint32_t>(WHITE, i), segment->value<uint32_t>(BLACK, i), segment->value<uint32_t>(EVENT, i),
            segment->value<uint32_t>(SITE, i), segment->value<uint32_t>(ROUND, i)};
}

uint64_t Snapshot::key(GameId id) const {
    const auto [segment, i] = locate(id);
    return segment->value<uint64_t>(KEY, i);
}

std::span<const uint8_t> Snapshot::moves(GameId id) const {
    const auto [segment, i] = locate(id);
    return segment->moves(i);
}

ImportedGame Snapshot::game(GameId id) const {
    const auto [segment, i] = locate(id);
    const auto r            = row(id);
    ImportedGame g;
    const auto tag = [&](std::string_view name, std::string_view value) {
        if (!value.empty()) g.tags.emplace_back(std::string(name), std::string(value));
    };
    tag("Event", r.event);
    tag("Site", r.site);
    if (r.date) tag("Date", dateText(r.date));
    tag("Round", r.round);
    tag("White", r.white);
    tag("Black", r.black);
    g.result = r.result;
    tag("Result", r.result == '1' ? "1-0" : r.result == '0' ? "0-1" : r.result == '=' ? "1/2-1/2" : "*");
    if (r.white_elo) tag("WhiteElo", std::to_string(r.white_elo));
    if (r.black_elo) tag("BlackElo", std::to_string(r.black_elo));
    if (r.eco) tag("ECO", ecoText(r.eco));
    tag("TimeControl", r.time_control);
    tag("Annotator", r.annotator);
    g.plies          = r.plies;
    g.chess960       = r.flags & GAME_CHESS960;
    g.annotated      = r.flags & GAME_ANNOTATED;
    g.damaged        = r.flags & GAME_DAMAGED;
    const auto moves = segment->moves(i);
    g.moves.assign(moves.begin(), moves.end());

    const auto annex   = segment->annex(i);
    const uint8_t* p   = annex.data();
    const uint8_t* end = p + annex.size();
    if (p == end) return g;
    const uint8_t parts = *p++;
    if (parts & ANNEX_FEN) {
        g.start_fen = getText(p, end);
        tag("FEN", g.start_fen);
    }
    if (parts & ANNEX_TAGS) {
        const auto count = getVarint(p, end);
        for (uint64_t t = 0; t < count; ++t) {
            auto name  = getText(p, end);
            auto value = getText(p, end);
            g.tags.emplace_back(std::move(name), std::move(value));
        }
    }
    if (parts & ANNEX_TEXT) g.text = getText(p, end);
    if (parts & ANNEX_CLOCKS) {
        const auto count = getVarint(p, end);
        g.clocks.resize(static_cast<std::size_t>(std::min<uint64_t>(count, g.plies)));
        for (std::size_t k = 0; k < g.clocks.size(); ++k) {
            const int64_t before = k >= 2 ? g.clocks[k - 2] : 0;
            g.clocks[k]          = static_cast<int32_t>(before + unzigzag(getVarint(p, end)));
        }
    }
    if (parts & ANNEX_EVALS) {
        const auto count = getVarint(p, end);
        g.evals.resize(static_cast<std::size_t>(std::min<uint64_t>(count, g.plies)));
        for (auto& e : g.evals) e = static_cast<int16_t>(unzigzag(getVarint(p, end)));
    }
    return g;
}

// ---------------------------------------------------------------------------------------------------------------------

Database::Database(const fs::path& dir) : dir_(dir), lock_(dir) {}

std::shared_ptr<Database> Database::create(const fs::path& dir) {
    std::error_code ec;
    if (fs::exists(dir / MANIFEST, ec)) throw std::runtime_error(dir.string() + " already holds a database");
    if (fs::exists(dir, ec) && !fs::is_empty(dir, ec)) throw std::runtime_error(dir.string() + " is not empty");
    fs::create_directories(dir, ec);
    if (ec) throw std::runtime_error("cannot create the folder " + dir.string());
    auto db = std::make_shared<Database>(dir);
    nlohmann::json manifest{{"format", "rookstudio-database"},
                            {"version", VERSION},
                            {"generation", 0},
                            {"segments", nlohmann::json::array()}};
    tour::replaceFile(dir / MANIFEST, manifest.dump(1));
    db->load();
    return db;
}

std::shared_ptr<Database> Database::open(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_regular_file(dir / MANIFEST, ec)) throw std::runtime_error(dir.string() + " is not a database");
    auto db = std::make_shared<Database>(dir);
    db->load();
    return db;
}

void Database::load() {
    nlohmann::json manifest;
    try {
        std::ifstream in(dir_ / MANIFEST, std::ios::binary);
        manifest = nlohmann::json::parse(in);
    } catch (const std::exception& e) {
        throw std::runtime_error((dir_ / MANIFEST).string() + ": " + e.what());
    }
    if (manifest.value("format", "") != "rookstudio-database")
        throw std::runtime_error(dir_.string() + " is not a database");
    if (manifest.value("version", 0u) > VERSION)
        throw std::runtime_error(dir_.string() + ": a database of a newer program");

    auto snap         = std::make_shared<Snapshot>();
    snap->generation_ = manifest.value("generation", uint64_t{0});
    std::vector<std::string> named;
    for (const auto& s : manifest.at("segments")) {
        const auto name  = s.at("name").get<std::string>();
        const auto games = s.at("games").get<uint32_t>();
        const auto names = s.at("names").get<uint32_t>();
        auto segment     = std::make_shared<const Segment>(dir_ / name, games, names);
        snap->first_game_.push_back(snap->games_);
        snap->first_name_.push_back(snap->names_);
        snap->games_ += games;
        snap->names_ += names;
        snap->segments_.push_back(std::move(segment));
        named.push_back(name);
        unsigned long long number = 0;
        std::sscanf(name.c_str(), "seg-%llu", &number);
        next_segment_ = std::max<uint64_t>(next_segment_, number + 1);
    }
    // What a commit cut short left: segments the manifest does not name. A number such a leftover holds is not used
    // again, should it not go.
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir_, ec)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("seg-", 0) != 0) continue;
        unsigned long long number = 0;
        if (std::sscanf(name.c_str(), "seg-%llu", &number) == 1)
            next_segment_ = std::max<uint64_t>(next_segment_, number + 1);
        if (std::find(named.begin(), named.end(), name) == named.end()) fs::remove_all(entry.path(), ec);
    }
    std::lock_guard lock(mutex_);
    current_ = std::move(snap);
}

std::shared_ptr<const Snapshot> Database::snapshot() const {
    std::lock_guard lock(mutex_);
    return current_;
}

uint32_t Database::nameId(const std::string& name, std::vector<std::string>& added) {
    if (const auto it = name_ids_.find(name); it != name_ids_.end()) return it->second;
    const auto id = current_->names_ + static_cast<uint32_t>(added.size());
    name_ids_.emplace(name, id);
    added.push_back(name);
    return id;
}

Database::Appended Database::append(std::vector<ImportedGame> games, bool keep_duplicates) {
    Appended appended;
    std::lock_guard lock(mutex_);
    if (!keep_duplicates) {
        if (!keys_indexed_) {
            known_keys_.reserve(current_->games_);
            for (GameId id = 0; id < current_->games_; ++id) known_keys_.push_back(current_->key(id));
            std::sort(known_keys_.begin(), known_keys_.end());
            keys_indexed_ = true;
        }
        std::vector<uint64_t> batch;
        std::size_t kept = 0;
        for (std::size_t i = 0; i < games.size(); ++i) {
            const auto key = duplicateKey(games[i]);
            if (std::binary_search(known_keys_.begin(), known_keys_.end(), key) ||
                std::find(batch.begin(), batch.end(), key) != batch.end()) {
                ++appended.duplicates;
                continue;
            }
            batch.push_back(key);
            if (kept != i) games[kept] = std::move(games[i]);
            ++kept;
        }
        games.resize(kept);
    }
    if (games.empty()) return appended;
    if (!names_indexed_) {
        for (uint32_t id = 0; id < current_->names_; ++id) name_ids_.emplace(std::string(current_->name(id)), id);
        names_indexed_ = true;
    }
    std::vector<std::string> added;                     // the names this append adds, all segments
    std::vector<std::string> made;                      // the segments it wrote
    std::vector<std::pair<uint32_t, uint32_t>> counts;  // their games and names
    nlohmann::json manifest;
    {
        // Closed before the manifest is replaced: Windows does not rename over an open file.
        std::ifstream in(dir_ / MANIFEST, std::ios::binary);
        manifest = nlohmann::json::parse(in);
    }
    bool committed = false;
    try {
        for (std::size_t from = 0; from < games.size(); from += SEGMENT_GAMES) {
            const std::size_t n     = std::min(SEGMENT_GAMES, games.size() - from);
            const auto names_before = added.size();
            const auto at           = layout(n);
            std::string cols(at[COLUMNS], '\0'), moves, annex;
            std::memcpy(cols.data(), MAGIC.data(), MAGIC.size());
            const auto put32 = [&](std::size_t offset, uint32_t v) { std::memcpy(cols.data() + offset, &v, 4); };
            const auto put   = [&](Column column, std::size_t i, auto v) {
                std::memcpy(cols.data() + at[column] + i * sizeof(v), &v, sizeof(v));
            };
            put32(8, VERSION);
            put32(12, static_cast<uint32_t>(n));
            for (std::size_t k = 0; k < n; ++k) {
                const auto& g = games[from + k];
                std::array<std::string, NAME_COLUMNS> names;
                std::string date, white_elo, black_elo, eco;
                std::vector<std::pair<std::string, std::string>> others;
                for (const auto& [tag, value] : g.tags) {
                    const auto column = std::find(NAME_TAGS.begin(), NAME_TAGS.end(), tag);
                    if (column != NAME_TAGS.end()) {
                        auto& slot = names[static_cast<std::size_t>(column - NAME_TAGS.begin())];
                        if (slot.empty()) {
                            slot = value;
                            std::replace(slot.begin(), slot.end(), '\n', ' ');
                            continue;
                        }
                    }
                    if (tag == "Date" && date.empty() && packDate(value) && dateText(packDate(value)) == value) {
                        date = value;
                        continue;
                    }
                    if (tag == "WhiteElo" && white_elo.empty() && number16(value)) {
                        white_elo = value;
                        continue;
                    }
                    if (tag == "BlackElo" && black_elo.empty() && number16(value)) {
                        black_elo = value;
                        continue;
                    }
                    if (tag == "ECO" && eco.empty() && packEco(value)) {
                        eco = value;
                        continue;
                    }
                    if (tag == "Result" || tag == "FEN") continue;  // the result's column, the annex's FEN
                    others.emplace_back(tag, value);
                }
                for (int c = 0; c < NAME_COLUMNS; ++c)
                    put(static_cast<Column>(c), k, nameId(names[static_cast<std::size_t>(c)], added));
                put(DATE, k, packDate(date));
                put(PLIES, k, g.plies);
                put(WHITE_ELO, k, number16(white_elo));
                put(BLACK_ELO, k, number16(black_elo));
                put(ECO, k, packEco(eco));
                uint16_t flags = 0;
                if (g.annotated) flags |= GAME_ANNOTATED;
                if (g.damaged) flags |= GAME_DAMAGED;
                if (g.chess960) flags |= GAME_CHESS960;
                if (!g.start_fen.empty()) flags |= GAME_FEN;
                if (!g.clocks.empty()) flags |= GAME_CLOCKS;
                if (!g.evals.empty()) flags |= GAME_EVALS;
                put(FLAGS, k, flags);
                put(RESULT, k, static_cast<uint8_t>(g.result));
                put(KEY, k, duplicateKey(g));
                put(MOVES, k, static_cast<uint64_t>(moves.size()));
                moves.append(reinterpret_cast<const char*>(g.moves.data()), g.moves.size());
                put(ANNEX, k, static_cast<uint64_t>(annex.size()));
                uint8_t parts = 0;
                if (!g.start_fen.empty()) parts |= ANNEX_FEN;
                if (!others.empty()) parts |= ANNEX_TAGS;
                if (!g.text.empty()) parts |= ANNEX_TEXT;
                if (!g.clocks.empty()) parts |= ANNEX_CLOCKS;
                if (!g.evals.empty()) parts |= ANNEX_EVALS;
                if (parts) {
                    annex += static_cast<char>(parts);
                    if (parts & ANNEX_FEN) putText(annex, g.start_fen);
                    if (parts & ANNEX_TAGS) {
                        putVarint(annex, others.size());
                        for (const auto& [tag, value] : others) {
                            putText(annex, tag);
                            putText(annex, value);
                        }
                    }
                    if (parts & ANNEX_TEXT) putText(annex, g.text);
                    if (parts & ANNEX_CLOCKS) {
                        putVarint(annex, g.clocks.size());
                        for (std::size_t c = 0; c < g.clocks.size(); ++c)
                            putVarint(annex, zigzag(int64_t{g.clocks[c]} - (c >= 2 ? g.clocks[c - 2] : 0)));
                    }
                    if (parts & ANNEX_EVALS) {
                        putVarint(annex, g.evals.size());
                        for (const auto e : g.evals) putVarint(annex, zigzag(e));
                    }
                }
            }
            put(MOVES, n, static_cast<uint64_t>(moves.size()));
            put(ANNEX, n, static_cast<uint64_t>(annex.size()));
            const auto new_names = static_cast<uint32_t>(added.size() - names_before);
            put32(16, new_names);
            std::string names_text;
            for (std::size_t a = names_before; a < added.size(); ++a) names_text += added[a] + "\n";

            const auto name = segmentName(next_segment_ + made.size());
            const auto tmp  = dir_ / (name + ".tmp");
            std::error_code ec;
            fs::remove_all(tmp, ec);
            fs::create_directories(tmp, ec);
            if (ec) throw std::runtime_error("cannot create " + tmp.string());
            made.push_back(name);
            writeFile(tmp / "cols.bin", cols);
            writeFile(tmp / "moves.bin", moves);
            writeFile(tmp / "annex.bin", annex);
            writeFile(tmp / "names.bin", names_text);
            syncFolder(tmp);
            step("rename segment");
            fs::rename(tmp, dir_ / name, ec);
            if (ec) throw std::runtime_error("cannot rename " + tmp.string() + ": " + ec.message());
            step("sync folder");
            syncFolder(dir_);
            counts.emplace_back(static_cast<uint32_t>(n), new_names);
        }
        for (std::size_t s = 0; s < made.size(); ++s)
            manifest["segments"].push_back(
                {{"name", made[s]}, {"games", counts[s].first}, {"names", counts[s].second}});
        manifest["generation"] = current_->generation_ + 1;
        step("write manifest");
        tour::replaceFile(dir_ / MANIFEST, manifest.dump(1));
        committed = true;
        step("manifest written");
    } catch (const std::exception&) {
        if (committed) throw;  // the games are in: nothing to undo
        // The disk refused: the database as before, the names this append gave forgotten.
        for (const auto& name : added) name_ids_.erase(name);
        std::error_code ec;
        for (const auto& name : made) {
            fs::remove_all(dir_ / name, ec);
            fs::remove_all(dir_ / (name + ".tmp"), ec);
        }
        throw;
    }

    auto snap         = std::make_shared<Snapshot>(*current_);
    snap->generation_ = current_->generation_ + 1;
    for (std::size_t s = 0; s < made.size(); ++s) {
        snap->first_game_.push_back(snap->games_);
        snap->first_name_.push_back(snap->names_);
        snap->segments_.push_back(std::make_shared<const Segment>(dir_ / made[s], counts[s].first, counts[s].second));
        snap->games_ += counts[s].first;
        snap->names_ += counts[s].second;
    }
    next_segment_ += made.size();
    current_ = std::move(snap);
    if (keys_indexed_) {
        for (const auto& g : games) known_keys_.push_back(duplicateKey(g));
        std::sort(known_keys_.begin(), known_keys_.end());
    }
    appended.added = games.size();
    return appended;
}

}  // namespace fcg::study
