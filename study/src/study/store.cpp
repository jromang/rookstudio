#include "store.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>

#include <chess.hpp>
#include <json.hpp>

#include "mapped_file.hpp"
#include "scan_board.hpp"

#ifndef _WIN32
#    include <fcntl.h>
#    include <unistd.h>
#endif

namespace fcg::study {

namespace fs = std::filesystem;

namespace {

constexpr std::array<char, 8> MAGIC{'F', 'C', 'G', 'S', 'E', 'G', '1', '\0'};
constexpr uint32_t VERSION        = 2;  // 2: sig.bin, the games' signatures
constexpr std::size_t SIG_BYTES   = 24;
constexpr uint8_t SIG_KNOWN       = 1;
constexpr uint8_t SIG_PROMOTED    = 2;
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

constexpr std::array<char, 8> EDITS_MAGIC{'F', 'C', 'G', 'E', 'D', 'I', 'T', '1'};

std::string editsName(uint64_t generation) {
    char name[32];
    std::snprintf(name, sizeof name, "edits-%06llu.bin", static_cast<unsigned long long>(generation));
    return name;
}

// The edits a manifest names: the games replaced (their versions' places) and the games deleted.
std::string editsBytes(const std::vector<Snapshot::Version>& replaced, const std::vector<uint64_t>& deleted) {
    std::vector<uint64_t> ids;
    for (std::size_t w = 0; w < deleted.size(); ++w)
        for (uint64_t bits = deleted[w]; bits; bits &= bits - 1)
            ids.push_back(w * 64 + static_cast<uint64_t>(std::countr_zero(bits)));
    std::string out(EDITS_MAGIC.begin(), EDITS_MAGIC.end());
    const auto put = [&](auto v) { out.append(reinterpret_cast<const char*>(&v), sizeof v); };
    put(uint32_t{1});
    put(uint32_t{0});
    put(static_cast<uint64_t>(replaced.size()));
    put(static_cast<uint64_t>(ids.size()));
    for (const auto& r : replaced) {
        put(static_cast<uint64_t>(r.id));
        put(r.segment);
        put(r.local);
    }
    for (const auto id : ids) put(id);
    return out;
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
        version_ = readAt<uint32_t>(c, 8, 0);
        if (version_ > VERSION) throw std::runtime_error(dir.string() + ": a newer format");
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
        if (version_ >= 2) {
            sig_ = MappedFile(dir / "sig.bin");
            if (sig_.size() != std::size_t{games_} * SIG_BYTES)
                throw std::runtime_error(dir.string() + ": damaged signatures");
        }
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
    [[nodiscard]] std::optional<GameSignature> signature(std::size_t i) const {
        if (!sig_.data()) return std::nullopt;  // a segment of the first format
        const uint8_t* p = sig_.data() + i * SIG_BYTES;
        if (!(p[21] & SIG_KNOWN)) return std::nullopt;
        GameSignature g;
        std::memcpy(g.home.data(), p, 16);
        for (int j = 0; j < 5; ++j) {
            g.material[2 * j]     = p[16 + j] & 0x0F;
            g.material[2 * j + 1] = p[16 + j] >> 4;
        }
        g.promoted = (p[21] & SIG_PROMOTED) != 0;
        return g;
    }

   private:
    MappedFile cols_, moves_, annex_, names_, sig_;
    uint32_t version_    = 0;
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

DatabaseImport importInto(Database& db, const std::filesystem::path& file, const ImportOptions& options,
                          ImportProgress& progress, std::atomic<bool>& cancel, bool keep_duplicates,
                          std::size_t commit_games, const ChessBaseOptions& chessbase) {
    DatabaseImport out;
    std::vector<ImportedGame> pending;
    const auto commit = [&] {
        if (pending.empty() || !out.error.empty()) return;
        try {
            const auto appended = db.append(std::move(pending), keep_duplicates);
            out.added += appended.added;
            out.duplicates += appended.duplicates;
            ++out.commits;
        } catch (const std::exception& e) {
            out.error = e.what();
            cancel    = true;  // the rest of the file would have nowhere to go
        }
        pending.clear();
    };
    const auto take = [&](std::vector<ImportedGame>&& batch) {
        if (!out.error.empty()) return;
        for (const auto& game : batch)
            if (game.damaged && out.damaged_lines.size() < 64)
                out.damaged_lines.emplace_back(duplicateKey(game), game.line);
        if (pending.empty()) {
            pending = std::move(batch);
        } else {
            pending.insert(pending.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));
        }
        if (pending.size() >= commit_games) commit();
    };
    if (isChessBase(file)) {
        ChessBaseReport left_out;
        out.read      = importChessBase(file, options, progress, cancel, take, &left_out, chessbase);
        out.chessbase = std::move(left_out);
    } else {
        out.read = importGames(file, options, progress, cancel, take);
    }
    commit();
    return out;
}

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
    if (!replaced_.empty()) {
        const auto v = std::lower_bound(replaced_.begin(), replaced_.end(), id,
                                        [](const Version& r, GameId x) { return r.id < x; });
        if (v != replaced_.end() && v->id == id) return {segments_[v->segment].get(), v->local};
    }
    const auto it = std::upper_bound(first_game_.begin(), first_game_.end(), id);
    const auto s  = static_cast<std::size_t>(it - first_game_.begin()) - 1;
    return {segments_[logical_[s]].get(), static_cast<std::size_t>(id - first_game_[s])};
}

std::string_view Snapshot::name(uint32_t id) const {
    // The segment that added it: the last whose first name is at most id among those that added some.
    auto s =
        static_cast<std::size_t>(std::upper_bound(first_name_.begin(), first_name_.end(), id) - first_name_.begin());
    while (s-- > 0) {
        if (segments_[s]->nameCount() == 0) continue;
        const auto at = id - first_name_[s];
        return at < segments_[s]->nameCount() ? segments_[s]->name(at) : std::string_view{};
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
    const auto n     = numbers(id);
    row.date         = n.date;
    row.plies        = n.plies;
    row.white_elo    = n.white_elo;
    row.black_elo    = n.black_elo;
    row.eco          = n.eco;
    row.flags        = n.flags;
    row.result       = n.result;
    return row;
}

HeaderNumbers Snapshot::numbers(GameId id) const {
    const auto [segment, i] = locate(id);
    HeaderNumbers n;
    n.date      = segment->value<uint32_t>(DATE, i);
    n.plies     = segment->value<uint32_t>(PLIES, i);
    n.white_elo = segment->value<uint16_t>(WHITE_ELO, i);
    n.black_elo = segment->value<uint16_t>(BLACK_ELO, i);
    n.eco       = segment->value<uint16_t>(ECO, i);
    n.flags     = segment->value<uint16_t>(FLAGS, i);
    n.result    = static_cast<char>(segment->value<uint8_t>(RESULT, i));
    return n;
}

GameMoves Snapshot::gameMoves(GameId id) const {
    const auto [segment, i] = locate(id);
    return {segment->moves(i), segment->value<uint32_t>(PLIES, i), segment->value<uint16_t>(FLAGS, i)};
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

std::optional<GameSignature> Snapshot::signature(GameId id) const {
    const auto [segment, i] = locate(id);
    return segment->signature(i);
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
        // A site kept in the annex (an address) back in its place, after the event.
        if (r.site.empty()) {
            const auto site =
                std::find_if(g.tags.begin(), g.tags.end(), [](const auto& t) { return t.first == "Site"; });
            if (site != g.tags.end()) std::rotate(g.tags.begin() + (r.event.empty() ? 0 : 1), site, site + 1);
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

Database::Database(const fs::path& dir) : dir_(dir), lock_(std::in_place, dir) {}

Database::Database(const fs::path& dir, bool read_only) : dir_(dir) {
    if (!read_only) lock_.emplace(dir);
}

std::shared_ptr<Database> Database::create(const fs::path& dir) {
    std::error_code ec;
    if (fs::exists(dir / MANIFEST, ec)) throw std::runtime_error(dir.string() + " already holds a database");
    // Empty, or holding only what a program killed while it made a database there left: its lock and its first
    // manifest not renamed yet, cleared once the lock is taken.
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const auto name = entry.path().filename().string();
        const bool left =
            name == "tournament.lock" || (name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0);
        if (!left) throw std::runtime_error(dir.string() + " is not empty");
    }
    ec.clear();
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

std::shared_ptr<Database> Database::openReadOnly(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_regular_file(dir / MANIFEST, ec)) throw std::runtime_error(dir.string() + " is not a database");
    auto db = std::make_shared<Database>(dir, true);
    db->load();
    return db;
}

bool Database::refresh() {
    if (lock_) return false;
    uint64_t generation = 0;
    try {
        std::ifstream in(dir_ / MANIFEST, std::ios::binary);
        generation = nlohmann::json::parse(in).value("generation", uint64_t{0});
    } catch (const std::exception&) {
        return false;  // being replaced as it was read: read again next time
    }
    if (generation == snapshot()->generation()) return false;
    try {
        load();
    } catch (const std::exception&) {
        return false;
    }
    return true;
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
        const auto name     = s.at("name").get<std::string>();
        const auto games    = s.at("games").get<uint32_t>();
        const auto names    = s.at("names").get<uint32_t>();
        const bool versions = s.value("versions", false);
        auto segment        = std::make_shared<const Segment>(dir_ / name, games, names);
        if (!versions) {
            snap->logical_.push_back(snap->segments_.size());
            snap->first_game_.push_back(snap->games_);
            snap->games_ += games;
        }
        snap->first_name_.push_back(snap->names_);
        snap->names_ += names;
        snap->segments_.push_back(std::move(segment));
        named.push_back(name);
        unsigned long long number = 0;
        std::sscanf(name.c_str(), "seg-%llu", &number);
        next_segment_ = std::max<uint64_t>(next_segment_, number + 1);
    }
    // The edits it names: the games replaced and those deleted.
    const auto edits        = manifest.value("edits", std::string());
    snap->edits_generation_ = manifest.value("edits_generation", uint64_t{0});
    if (!edits.empty()) {
        std::ifstream in(dir_ / edits, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto bad = [&] { return std::runtime_error((dir_ / edits).string() + ": damaged edits"); };
        if (bytes.size() < 32 || std::memcmp(bytes.data(), EDITS_MAGIC.data(), EDITS_MAGIC.size()) != 0) throw bad();
        const auto at = [&](std::size_t o, auto v) {
            std::memcpy(&v, bytes.data() + o, sizeof v);
            return v;
        };
        const auto replaced = at(16, uint64_t{0}), deleted = at(24, uint64_t{0});
        if (bytes.size() != 32 + replaced * 16 + deleted * 8) throw bad();
        for (uint64_t i = 0; i < replaced; ++i) {
            const std::size_t o = 32 + i * 16;
            const Snapshot::Version v{at(o, uint64_t{0}), at(o + 8, uint32_t{0}), at(o + 12, uint32_t{0})};
            if (v.id >= snap->games_ || v.segment >= snap->segments_.size() ||
                v.local >= snap->segments_[v.segment]->games())
                throw bad();
            snap->replaced_.push_back(v);
        }
        for (uint64_t i = 0; i < deleted; ++i) {
            const auto id = at(32 + replaced * 16 + i * 8, uint64_t{0});
            if (id >= snap->games_) throw bad();
            if (snap->deleted_.size() <= id / 64) snap->deleted_.resize(id / 64 + 1, 0);
            snap->deleted_[id / 64] |= uint64_t{1} << (id % 64);
        }
        snap->deleted_count_ = deleted;
    }
    // What a commit cut short left: segments the manifest does not name, and the new manifest not yet renamed over
    // the old one (a killed program leaves it); edits it does not name. A number such a leftover segment holds is not
    // used again, should it not go.
    // Read-only, they are left alone: they may be the other program's commit under way.
    std::error_code ec;
    if (lock_) {
        for (const auto& entry : fs::directory_iterator(dir_, ec)) {
            const auto name      = entry.path().filename().string();
            const bool temporary = name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0;
            if (name.rfind("seg-", 0) == 0) {
                unsigned long long number = 0;
                if (std::sscanf(name.c_str(), "seg-%llu", &number) == 1)
                    next_segment_ = std::max<uint64_t>(next_segment_, number + 1);
                if (temporary || std::find(named.begin(), named.end(), name) == named.end())
                    fs::remove_all(entry.path(), ec);
            } else if (temporary || (name.rfind("edits-", 0) == 0 && name != edits)) {
                fs::remove_all(entry.path(), ec);
            }
        }
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

void Database::commit(std::vector<ImportedGame>& games, const std::vector<uint64_t>& keys, bool versions,
                      const std::vector<Snapshot::Version>* replaced, const std::vector<uint64_t>* deleted) {
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
    const auto edits_before = manifest.value("edits", std::string());
    std::string edits_written;
    bool committed = false;
    try {
        for (std::size_t from = 0; from < games.size(); from += SEGMENT_GAMES) {
            const std::size_t n     = std::min(SEGMENT_GAMES, games.size() - from);
            const auto names_before = added.size();
            const auto at           = layout(n);
            std::string cols(at[COLUMNS], '\0'), moves, annex, sig(n * SIG_BYTES, '\0');
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
                    // A site that is the game's own address (Lichess's) would add a name for each game: kept in
                    // the annex with the other tags.
                    const bool own_address =
                        tag == "Site" && (value.rfind("https://", 0) == 0 || value.rfind("http://", 0) == 0);
                    if (column != NAME_TAGS.end() && !own_address) {
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
                put(KEY, k, keys[from + k]);
                put(MOVES, k, static_cast<uint64_t>(moves.size()));
                moves.append(reinterpret_cast<const char*>(g.moves.data()), g.moves.size());
                auto signature = g.signature;
                if (!signature) {
                    ScanBoard start;
                    if (start.setFen(g.start_fen.empty() ? std::string_view(chess::constants::STARTPOS)
                                                         : std::string_view(g.start_fen),
                                     g.chess960))
                        signature = signatureOf(start, g.moves, g.plies);
                }
                if (signature) {
                    char* out = sig.data() + k * SIG_BYTES;
                    std::memcpy(out, signature->home.data(), 16);
                    for (int j = 0; j < 5; ++j)
                        out[16 + j] = static_cast<char>((signature->material[2 * j] & 0x0F) |
                                                        (std::min<uint8_t>(signature->material[2 * j + 1], 15) << 4));
                    out[21] = static_cast<char>(SIG_KNOWN | (signature->promoted ? SIG_PROMOTED : 0));
                }
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
            writeFile(tmp / "sig.bin", sig);
            syncFolder(tmp);
            step("rename segment");
            fs::rename(tmp, dir_ / name, ec);
            if (ec) throw std::runtime_error("cannot rename " + tmp.string() + ": " + ec.message());
            step("sync folder");
            syncFolder(dir_);
            counts.emplace_back(static_cast<uint32_t>(n), new_names);
        }
        for (std::size_t s = 0; s < made.size(); ++s) {
            nlohmann::json entry{{"name", made[s]}, {"games", counts[s].first}, {"names", counts[s].second}};
            if (versions) entry["versions"] = true;
            manifest["segments"].push_back(std::move(entry));
        }
        if (replaced || deleted) {
            edits_written = editsName(current_->generation_ + 1);
            writeFile(dir_ / edits_written,
                      editsBytes(replaced ? *replaced : current_->replaced_, deleted ? *deleted : current_->deleted_));
            manifest["edits"]            = edits_written;
            manifest["edits_generation"] = current_->generation_ + 1;
        }
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
        if (!edits_written.empty()) fs::remove(dir_ / edits_written, ec);
        throw;
    }

    auto snap         = std::make_shared<Snapshot>(*current_);
    snap->generation_ = current_->generation_ + 1;
    for (std::size_t s = 0; s < made.size(); ++s) {
        if (!versions) {
            snap->logical_.push_back(snap->segments_.size());
            snap->first_game_.push_back(snap->games_);
            snap->games_ += counts[s].first;
        }
        snap->first_name_.push_back(snap->names_);
        snap->segments_.push_back(std::make_shared<const Segment>(dir_ / made[s], counts[s].first, counts[s].second));
        snap->names_ += counts[s].second;
    }
    if (replaced || deleted) snap->edits_generation_ = snap->generation_;
    if (replaced) snap->replaced_ = *replaced;
    if (deleted) {
        snap->deleted_       = *deleted;
        snap->deleted_count_ = 0;
        for (const auto w : snap->deleted_) snap->deleted_count_ += static_cast<uint64_t>(std::popcount(w));
    }
    if (!edits_written.empty() && !edits_before.empty() && edits_before != edits_written) {
        std::error_code gone;
        fs::remove(dir_ / edits_before, gone);  // still read on Windows by nobody: the edits are in memory
    }
    next_segment_ += made.size();
    current_ = std::move(snap);
}

Database::Appended Database::append(std::vector<ImportedGame> games, bool keep_duplicates) {
    if (!lock_) throw std::runtime_error(dir_.string() + ": read-only, another program writes to it");
    Appended appended;
    std::lock_guard lock(mutex_);
    // Each game's key, made once: for the duplicates, the key column and the keys known after.
    std::vector<uint64_t> keys(games.size());
    for (std::size_t i = 0; i < games.size(); ++i) keys[i] = duplicateKey(games[i]);
    if (!keep_duplicates) {
        if (!keys_indexed_) {
            known_keys_.reserve(current_->games_);
            for (GameId id = 0; id < current_->games_; ++id) known_keys_.push_back(current_->key(id));
            std::sort(known_keys_.begin(), known_keys_.end());
            keys_indexed_ = true;
        }
        // Left out: a game whose key the database has, or a game before it in the batch. Sorted by key, then by
        // place, the first of a key comes first.
        std::vector<std::pair<uint64_t, std::size_t>> order(games.size());
        for (std::size_t i = 0; i < games.size(); ++i) order[i] = {keys[i], i};
        std::sort(order.begin(), order.end());
        std::vector<char> left_out(games.size(), 0);
        for (std::size_t j = 0; j < order.size(); ++j) {
            const auto [key, i] = order[j];
            if ((j > 0 && order[j - 1].first == key) || std::binary_search(known_keys_.begin(), known_keys_.end(), key))
                left_out[i] = 1;
        }
        std::size_t kept = 0;
        for (std::size_t i = 0; i < games.size(); ++i) {
            if (left_out[i]) {
                ++appended.duplicates;
                continue;
            }
            if (kept != i) {
                games[kept] = std::move(games[i]);
                keys[kept]  = keys[i];
            }
            ++kept;
        }
        games.resize(kept);
        keys.resize(kept);
    }
    if (games.empty()) return appended;
    commit(games, keys, false, nullptr, nullptr);
    if (keys_indexed_) {
        // Merged in: a sort of the new keys only, however many the database has.
        const auto before = static_cast<std::ptrdiff_t>(known_keys_.size());
        known_keys_.insert(known_keys_.end(), keys.begin(), keys.end());
        std::sort(known_keys_.begin() + before, known_keys_.end());
        std::inplace_merge(known_keys_.begin(), known_keys_.begin() + before, known_keys_.end());
    }
    appended.added = games.size();
    return appended;
}

void Database::replace(GameId id, ImportedGame game) {
    if (!lock_) throw std::runtime_error(dir_.string() + ": read-only, another program writes to it");
    std::lock_guard lock(mutex_);
    if (id >= current_->games_) throw std::out_of_range("no game " + std::to_string(id) + " to replace");
    const uint64_t old_key = current_->key(id);
    std::vector<uint64_t> keys{duplicateKey(game)};
    std::vector<ImportedGame> games;
    games.push_back(std::move(game));
    // The new version goes in a segment of its own, the next one.
    auto replaced = current_->replaced_;
    const Snapshot::Version version{id, static_cast<uint32_t>(current_->segments_.size()), 0};
    const auto at = std::lower_bound(replaced.begin(), replaced.end(), id,
                                     [](const Snapshot::Version& r, GameId x) { return r.id < x; });
    if (at != replaced.end() && at->id == id)
        *at = version;
    else
        replaced.insert(at, version);
    commit(games, keys, true, &replaced, nullptr);
    if (keys_indexed_) {
        if (const auto it = std::lower_bound(known_keys_.begin(), known_keys_.end(), old_key);
            it != known_keys_.end() && *it == old_key)
            known_keys_.erase(it);
        known_keys_.insert(std::upper_bound(known_keys_.begin(), known_keys_.end(), keys[0]), keys[0]);
    }
}

void Database::setDeleted(std::span<const GameId> ids, bool deleted) {
    if (!lock_) throw std::runtime_error(dir_.string() + ": read-only, another program writes to it");
    std::lock_guard lock(mutex_);
    auto bits = current_->deleted_;
    for (const auto id : ids) {
        if (id >= current_->games_) continue;
        if (bits.size() <= id / 64) bits.resize(id / 64 + 1, 0);
        if (deleted)
            bits[id / 64] |= uint64_t{1} << (id % 64);
        else
            bits[id / 64] &= ~(uint64_t{1} << (id % 64));
    }
    while (!bits.empty() && bits.back() == 0) bits.pop_back();
    if (bits == current_->deleted_) return;
    std::vector<ImportedGame> none;
    commit(none, {}, false, nullptr, &bits);
}

}  // namespace fcg::study
