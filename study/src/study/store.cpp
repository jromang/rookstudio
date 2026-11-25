#include "store.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <thread>

#include <chess.hpp>
#include <json.hpp>

#include "background.hpp"
#include "mapped_file.hpp"
#include "scan_board.hpp"
#include "tree.hpp"

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

std::string playersName(uint64_t generation) {
    char name[32];
    std::snprintf(name, sizeof name, "players-%06llu.bin", static_cast<unsigned long long>(generation));
    return name;
}

std::string segmentName(uint64_t number) {
    char name[32];
    std::snprintf(name, sizeof name, "seg-%06llu", static_cast<unsigned long long>(number));
    return name;
}

// A table of names without the name ids past a database's `names` (a file damaged or made by hand): no spelling of its,
// and the callers index arrays of names() entries with them (#102).
PlayerTable spellingsWithin(PlayerTable table, uint32_t names) {
    const auto past = [&](uint32_t id) { return id >= names; };
    if (std::none_of(table.players().begin(), table.players().end(),
                     [&](const Player& p) { return std::any_of(p.spellings.begin(), p.spellings.end(), past); }))
        return table;
    auto players = table.players();
    for (auto& p : players) std::erase_if(p.spellings, past);
    std::erase_if(players, [](const Player& p) { return p.spellings.empty(); });  // as withPlayer() leaves none
    return PlayerTable(std::move(players), table.apart());
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------

class Segment {
   public:
    // `names_known`: false for names written as "?" in place of names lost (Database::keepReadable()).
    Segment(const fs::path& dir, uint32_t expected_games, uint32_t expected_names, bool names_known = true)
        : cols_(dir / "cols.bin"),
          moves_(dir / "moves.bin"),
          annex_(dir / "annex.bin"),
          names_(dir / "names.bin"),
          names_known_(names_known) {
        const auto* c = cols_.data();
        cols_data_    = c;
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

    // A segment that cannot be read, in its place (Database::load()): the games and names the manifest gives it, its
    // records read as one empty game (record 0, `blank_name` its names), its names those of its names.bin when that
    // still reads with the manifest's count, else "?".
    struct Unreadable {};
    Segment(Unreadable, const fs::path& dir, uint32_t games, uint32_t names, uint32_t blank_name, bool names_known)
        : version_(VERSION), games_(games), name_count_(names), unreadable_(true) {
        at_ = layout(1);
        blank_.assign(at_[COLUMNS], 0);
        for (int c = 0; c < NAME_COLUMNS; ++c)
            std::memcpy(blank_.data() + at_[static_cast<std::size_t>(c)], &blank_name, sizeof blank_name);
        blank_[at_[RESULT]] = static_cast<uint8_t>('*');
        cols_data_          = blank_.data();
        names_known_        = names_known && readNames(dir / "names.bin", names, names_, name_offsets_);
    }

    // Whether `file` holds `count` names, one a line: their offsets then, and the file mapped.
    static bool readNames(const fs::path& file, uint32_t count, MappedFile& names, std::vector<uint32_t>& offsets) {
        try {
            names = MappedFile(file);
        } catch (const std::exception&) {
            return false;
        }
        offsets.clear();
        offsets.reserve(std::size_t{count} + 1);
        offsets.push_back(0);
        for (std::size_t i = 0; i < names.size(); ++i)
            if (names.data()[i] == '\n') offsets.push_back(static_cast<uint32_t>(i + 1));
        if (offsets.size() == std::size_t{count} + 1 && (names.size() == 0 || names.data()[names.size() - 1] == '\n'))
            return true;
        names = MappedFile();
        offsets.clear();
        return false;
    }

    [[nodiscard]] uint32_t games() const noexcept { return games_; }
    [[nodiscard]] uint32_t nameCount() const noexcept { return name_count_; }
    [[nodiscard]] std::string_view name(uint32_t local) const {
        if (name_offsets_.empty()) return "?";  // lost with a segment that cannot be read
        const auto from = name_offsets_[local], to = name_offsets_[local + 1] - 1;
        return {reinterpret_cast<const char*>(names_.data()) + from, to - from};
    }
    // Whether its names read as they were written ("?" in place of names lost otherwise).
    [[nodiscard]] bool namesKnown() const noexcept { return names_known_; }
    // A segment that could not be read: its records are one empty game.
    [[nodiscard]] bool unreadable() const noexcept { return unreadable_; }
    template <typename T>
    [[nodiscard]] T value(Column column, std::size_t i) const {
        return readAt<T>(cols_data_, at_[column], i);
    }
    // A record's moves and annex; none when its offsets are out of order or past its file (a damaged or crafted
    // segment, whose bytes are not checked when it opens: offsetsSound() tells it, the game reads as damaged).
    [[nodiscard]] std::span<const uint8_t> moves(std::size_t i) const {
        const auto from = value<uint64_t>(MOVES, i), to = value<uint64_t>(MOVES, i + 1);
        if (from > to || to > moves_.size()) return {};
        return {moves_.data() + from, static_cast<std::size_t>(to - from)};
    }
    [[nodiscard]] std::span<const uint8_t> annex(std::size_t i) const {
        const auto from = value<uint64_t>(ANNEX, i), to = value<uint64_t>(ANNEX, i + 1);
        if (from > to || to > annex_.size()) return {};
        return {annex_.data() + from, static_cast<std::size_t>(to - from)};
    }
    [[nodiscard]] uint32_t version() const noexcept { return version_; }
    // The files' bytes, for a check and a compaction.
    [[nodiscard]] std::size_t movesBytes() const noexcept { return moves_.size(); }
    [[nodiscard]] std::size_t annexBytes() const noexcept { return annex_.size(); }
    [[nodiscard]] std::size_t bytes() const noexcept {
        return cols_.size() + moves_.size() + annex_.size() + names_.size() + sig_.size();
    }
    [[nodiscard]] std::string_view namesText() const noexcept {
        return {reinterpret_cast<const char*>(names_.data()), names_.size()};
    }
    // A record's offsets within its files: false when they are not (a damaged file).
    [[nodiscard]] bool offsetsSound(std::size_t i) const {
        const auto mf = value<uint64_t>(MOVES, i), mt = value<uint64_t>(MOVES, i + 1);
        const auto af = value<uint64_t>(ANNEX, i), at = value<uint64_t>(ANNEX, i + 1);
        return mf <= mt && mt <= moves_.size() && af <= at && at <= annex_.size();
    }
    // The signature's bytes as stored, null in a segment of the first format.
    [[nodiscard]] const uint8_t* signatureBytes(std::size_t i) const {
        return sig_.data() ? sig_.data() + i * SIG_BYTES : nullptr;
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
    const uint8_t* cols_data_ = nullptr;  // the columns: cols.bin's, or the empty game's of a segment not read
    std::vector<uint8_t> blank_;          // that empty game's columns
    uint32_t version_    = 0;
    uint32_t games_      = 0;
    uint32_t name_count_ = 0;
    bool names_known_    = true;
    bool unreadable_     = false;
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
                          std::size_t commit_games, const ChessBaseOptions& chessbase,
                          const std::vector<uint64_t>& forget) {
    DatabaseImport out;
    std::vector<ImportedGame> pending;
    const auto u8 = file.u8string();
    const std::string source(u8.begin(), u8.end());
    const auto commit = [&] {
        if (pending.empty() || !out.error.empty()) return;
        try {
            const auto appended = db.append(std::move(pending), keep_duplicates, source, forget);
            out.added += appended.added;
            out.duplicates += appended.duplicates;
            out.damaged += appended.damaged;
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
    // A segment that cannot be read gives its one empty game for each of its own.
    const auto at = [](const Segment* segment, std::size_t i) {
        return std::pair<const Segment*, std::size_t>{segment, segment->unreadable() ? 0 : i};
    };
    if (!replaced_.empty()) {
        const auto v = std::lower_bound(replaced_.begin(), replaced_.end(), id,
                                        [](const Version& r, GameId x) { return r.id < x; });
        if (v != replaced_.end() && v->id == id) return at(segments_[v->segment].get(), v->local);
    }
    const auto it = std::upper_bound(first_game_.begin(), first_game_.end(), id);
    const auto s  = static_cast<std::size_t>(it - first_game_.begin()) - 1;
    return at(segments_[logical_[s]].get(), static_cast<std::size_t>(id - first_game_[s]));
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

namespace {

// A segment's record i as its columns give it, its names read in `snap`.
HeaderRow recordRow(const Snapshot& snap, const Segment& segment, std::size_t i) {
    HeaderRow row;
    row.white        = snap.name(segment.value<uint32_t>(WHITE, i));
    row.black        = snap.name(segment.value<uint32_t>(BLACK, i));
    row.event        = snap.name(segment.value<uint32_t>(EVENT, i));
    row.site         = snap.name(segment.value<uint32_t>(SITE, i));
    row.round        = snap.name(segment.value<uint32_t>(ROUND, i));
    row.annotator    = snap.name(segment.value<uint32_t>(ANNOTATOR, i));
    row.time_control = snap.name(segment.value<uint32_t>(TIME_CONTROL, i));
    row.date         = segment.value<uint32_t>(DATE, i);
    row.plies        = segment.value<uint32_t>(PLIES, i);
    row.white_elo    = segment.value<uint16_t>(WHITE_ELO, i);
    row.black_elo    = segment.value<uint16_t>(BLACK_ELO, i);
    row.eco          = segment.value<uint16_t>(ECO, i);
    row.flags        = segment.value<uint16_t>(FLAGS, i);
    row.result       = static_cast<char>(segment.value<uint8_t>(RESULT, i));
    return row;
}

// Everything kept of a segment's record i (Snapshot::game()). A record that does not read whole (its offsets out of its
// files, its annex record cut short or made up) throws std::runtime_error when `strict` (the check); else it gives its
// headers and what could be read before the damage, marked damaged (#102).
ImportedGame recordGame(const Snapshot& snap, const Segment& segment, std::size_t i, bool strict) {
    const auto r = recordRow(snap, segment, i);
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
    g.plies     = r.plies;
    g.chess960  = r.flags & GAME_CHESS960;
    g.annotated = r.flags & GAME_ANNOTATED;
    g.damaged   = r.flags & GAME_DAMAGED;
    if (!segment.offsetsSound(i)) {
        if (strict) throw std::runtime_error("damaged offsets");
        g.damaged = true;  // its moves or its annex, whichever its offsets leave out, read as none
    }
    const auto moves = segment.moves(i);
    g.moves.assign(moves.begin(), moves.end());

    const auto annex   = segment.annex(i);
    const uint8_t* p   = annex.data();
    const uint8_t* end = p + annex.size();
    try {
        const uint8_t parts = p == end ? uint8_t{0} : *p++;
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
        // A clock or an evaluation takes a byte at least: a count past the bytes left is a damaged one, never a size
        // to make (a count read from the file could ask for gigabytes).
        if (parts & ANNEX_CLOCKS) {
            const auto count = getVarint(p, end);
            if (count > static_cast<uint64_t>(end - p)) throw std::runtime_error("damaged annex record");
            g.clocks.resize(static_cast<std::size_t>(std::min<uint64_t>(count, g.plies)));
            for (std::size_t k = 0; k < g.clocks.size(); ++k) {
                const int64_t before = k >= 2 ? g.clocks[k - 2] : 0;
                g.clocks[k]          = static_cast<int32_t>(before + unzigzag(getVarint(p, end)));
            }
        }
        if (parts & ANNEX_EVALS) {
            const auto count = getVarint(p, end);
            if (count > static_cast<uint64_t>(end - p)) throw std::runtime_error("damaged annex record");
            g.evals.resize(static_cast<std::size_t>(std::min<uint64_t>(count, g.plies)));
            for (auto& e : g.evals) e = static_cast<int16_t>(unzigzag(getVarint(p, end)));
        }
    } catch (const std::runtime_error&) {
        if (strict) throw;
        g.damaged = true;
    }
    if (strict) return g;
    // A ply takes a code of a byte or two: more plies than bytes is a damaged count.
    if (g.plies > g.moves.size()) {
        g.damaged = true;
        g.plies   = static_cast<uint32_t>(g.moves.size());
    }
    // Its start position lost with its annex: its moves, which are those of that position, cannot be replayed.
    if ((r.flags & GAME_FEN) && g.start_fen.empty()) {
        g.damaged = true;
        g.moves.clear();
        g.plies = 0;
    }
    if (g.clocks.size() > g.plies) g.clocks.resize(g.plies);
    if (g.evals.size() > g.plies) g.evals.resize(g.plies);
    return g;
}

}  // namespace

HeaderRow Snapshot::row(GameId id) const {
    const auto [segment, i] = locate(id);
    return recordRow(*this, *segment, i);
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
    const auto codes        = segment->moves(i);
    // A ply takes a code of a byte or two: no more plies than bytes, whatever a damaged count says (the replays index
    // the codes by ply).
    const auto plies = std::min<uint64_t>(segment->value<uint32_t>(PLIES, i), codes.size());
    return {codes, static_cast<uint32_t>(plies), segment->value<uint16_t>(FLAGS, i)};
}

Snapshot::NameIds Snapshot::rowIds(GameId id) const {
    const auto [segment, i] = locate(id);
    // A name id past the database's names (a damaged or crafted segment) reads as no name: the callers index arrays
    // of names() entries with them.
    const auto known = [this](uint32_t name) { return name < names_ ? name : no_name_; };
    return {known(segment->value<uint32_t>(WHITE, i)), known(segment->value<uint32_t>(BLACK, i)),
            known(segment->value<uint32_t>(EVENT, i)), known(segment->value<uint32_t>(SITE, i)),
            known(segment->value<uint32_t>(ROUND, i))};
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
    return recordGame(*this, *segment, i, false);
}

std::string Snapshot::startFen(GameId id) const {
    const auto [segment, i] = locate(id);
    const auto annex        = segment->annex(i);
    const uint8_t* p        = annex.data();
    const uint8_t* end      = p + annex.size();
    if (p == end || !(*p & ANNEX_FEN)) return {};
    ++p;
    try {
        return getText(p, end);
    } catch (const std::runtime_error&) {
        return {};  // a damaged record: no start that can be set
    }
}

bool startOf(const Snapshot& snapshot, GameId id, uint16_t flags, ScanBoard& board) {
    static const ScanBoard standard = [] {
        ScanBoard b;
        b.setFen(chess::constants::STARTPOS, false);
        return b;
    }();
    static const ScanBoard standard960 = [] {
        ScanBoard b;
        b.setFen(chess::constants::STARTPOS, true);
        return b;
    }();
    const bool chess960 = (flags & GAME_CHESS960) != 0;
    if (flags & GAME_FEN) {
        const auto fen = snapshot.startFen(id);
        return !fen.empty() && board.setFen(fen, chess960);
    }
    board = chess960 ? standard960 : standard;
    return true;
}

std::string Snapshot::tag(GameId id, std::string_view wanted) const {
    const auto [segment, i] = locate(id);
    const auto annex        = segment->annex(i);
    const uint8_t* p        = annex.data();
    const uint8_t* end      = p + annex.size();
    if (p == end) return {};
    const uint8_t parts = *p++;
    try {
        if (parts & ANNEX_FEN) (void)getText(p, end);
        if (!(parts & ANNEX_TAGS)) return {};
        const auto count = getVarint(p, end);
        for (uint64_t t = 0; t < count; ++t) {
            const auto size = getVarint(p, end);
            if (size > static_cast<uint64_t>(end - p)) return {};
            const std::string_view name(reinterpret_cast<const char*>(p), static_cast<std::size_t>(size));
            p += size;
            auto value = getText(p, end);
            if (name == wanted) return value;
        }
    } catch (const std::exception&) {
    }
    return {};
}

uint64_t Snapshot::storedBytes(GameId id) const {
    const auto [segment, i] = locate(id);
    return segment->moves(i).size() + segment->annex(i).size();
}

std::size_t Snapshot::segmentOf(GameId id) const {
    if (id >= games_) throw std::out_of_range("no such game");
    const auto it = std::upper_bound(first_game_.begin(), first_game_.end(), id);
    return logical_[static_cast<std::size_t>(it - first_game_.begin()) - 1];
}

const std::string& Snapshot::source(GameId id) const {
    static const std::string none;
    const auto s = segmentOf(id);
    return s < sources_.size() ? sources_[s] : none;
}

std::string_view Snapshot::shownName(uint32_t name_id) const {
    if (const auto* p = players_->of(name_id); p && !p->name.empty()) return p->name;
    return name(name_id);
}

GameState Snapshot::state(GameId id) const {
    GameState out;
    out.id = id;
    if (id >= games_) return out;
    const auto v =
        std::lower_bound(replaced_.begin(), replaced_.end(), id, [](const Version& r, GameId x) { return r.id < x; });
    if (v != replaced_.end() && v->id == id) out.version = std::pair{v->segment, v->local};
    out.deleted = deletedByEdit(id);
    return out;
}

std::vector<GameId> Snapshot::unreadableIds() const {
    std::vector<GameId> out;
    out.reserve(static_cast<std::size_t>(unreadable_count_));
    for (std::size_t w = 0; w < unreadable_.size(); ++w) {
        auto bits = unreadable_[w] & ~(w < deleted_.size() ? deleted_[w] : uint64_t{0});
        for (; bits; bits &= bits - 1) out.push_back(w * 64 + static_cast<GameId>(std::countr_zero(bits)));
    }
    return out;
}

void Snapshot::settle() {
    // What a name id past the names reads as (rowIds()): no name, as for the empty games of the segments that cannot be
    // read. Found among the first names, the empty one being any game's that lacks a name.
    no_name_ = blankName().value_or(0);
    unknown_names_.clear();
    for (std::size_t s = 0; s < segments_.size(); ++s)
        if (!segments_[s]->namesKnown() && segments_[s]->nameCount() > 0)
            unknown_names_.emplace_back(first_name_[s], first_name_[s] + segments_[s]->nameCount());
    unreadable_.clear();
    unreadable_count_ = 0;
    unreadable_edits_ = 0;
    if (unreadable_segments_.empty()) return;
    // Their games: those a segment that adds games holds, but those replaced (their versions read), and those whose
    // version is in a segment of versions that cannot be read.
    unreadable_.assign(static_cast<std::size_t>((games_ + 63) / 64), 0);
    const auto set      = [&](GameId id) { unreadable_[id / 64] |= uint64_t{1} << (id % 64); };
    const auto replaced = [&](GameId id) {
        const auto v = std::lower_bound(replaced_.begin(), replaced_.end(), id,
                                        [](const Version& r, GameId x) { return r.id < x; });
        return v != replaced_.end() && v->id == id;
    };
    // The tree built before they could no longer be read holds their games: not that of these edits.
    uint64_t mix = 0xcbf29ce484222325ULL;
    for (const unsigned char c : std::to_string(edits_generation_)) mix = (mix ^ c) * 0x100000001b3ULL;
    for (const auto& bad : unreadable_segments_) {
        for (const unsigned char c : bad.name) mix = (mix ^ c) * 0x100000001b3ULL;
        if (bad.versions) continue;
        for (GameId id = bad.first; id < bad.first + bad.games; ++id)
            if (!replaced(id)) set(id);
    }
    for (const auto& v : replaced_)
        if (segments_[v.segment]->unreadable()) set(v.id);
    for (std::size_t w = 0; w < unreadable_.size(); ++w)
        unreadable_count_ +=
            static_cast<uint64_t>(std::popcount(unreadable_[w] & ~(w < deleted_.size() ? deleted_[w] : uint64_t{0})));
    unreadable_edits_ = uint64_t{1} << 63 | mix >> 1;  // never a generation's
}

std::optional<uint32_t> Snapshot::blankName() const {
    for (const std::string_view wanted : {std::string_view(), std::string_view("?")})
        for (std::size_t s = 0; s < segments_.size(); ++s)
            for (uint32_t local = 0; local < segments_[s]->nameCount(); ++local)
                if (segments_[s]->name(local) == wanted) return first_name_[s] + local;
    return std::nullopt;
}

CheckReport unreadableReport(const Snapshot& snapshot) {
    CheckReport report;
    report.games    = snapshot.games() - snapshot.deletedCount();
    report.segments = snapshot.unreadable();
    report.damaged  = snapshot.unreadableIds();
    for (const auto id : report.damaged) ++report.damaged_sources[snapshot.source(id)];
    return report;
}

std::vector<Snapshot::SegmentInfo> Snapshot::segmentInfos() const {
    std::vector<SegmentInfo> out;
    std::size_t logical = 0;
    for (std::size_t s = 0; s < segments_.size(); ++s) {
        SegmentInfo info;
        info.name     = s < segment_names_.size() ? segment_names_[s] : std::string();
        info.games    = segments_[s]->games();
        info.versions = s < versions_.size() && versions_[s];
        info.source   = s < sources_.size() ? sources_[s] : std::string();
        if (!info.versions && logical < logical_.size() && logical_[logical] == s) info.first = first_game_[logical++];
        out.push_back(std::move(info));
    }
    return out;
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
    std::lock_guard writing(write_mutex_);  // current_ changes under it, as for a change
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

void Database::load(bool clean, int tries) {
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
    uint64_t next_segment = 1;  // the numbers the segments there take, and the next one
    // A segment that cannot be read (a file of it cut short, missing, or not a segment's: damaged while the database
    // was closed) keeps its place, its games and names counted as the manifest gives them (#97).
    struct Unread {
        std::size_t index = 0;
        BadSegment bad;
        bool names_known = true;
    };
    std::vector<Unread> unread;
    for (const auto& s : manifest.at("segments")) {
        const auto name       = s.at("name").get<std::string>();
        const auto games      = s.at("games").get<uint32_t>();
        const auto names      = s.at("names").get<uint32_t>();
        // No commit writes more: seven names a game at most (a segment that cannot be read would make room for each).
        if (games > SEGMENT_GAMES || names > 7 * SEGMENT_GAMES + 1 || snap->names_ > UINT32_MAX - names)
            throw std::runtime_error(dir_.string() + ": damaged manifest");
        const bool versions   = s.value("versions", false);
        const bool names_lost = s.value("unknown_names", false);  // written as "?" by keepReadable()
        std::shared_ptr<const Segment> segment;
        try {
            segment = std::make_shared<const Segment>(dir_ / name, games, names, !names_lost);
        } catch (const std::exception& e) {
            segment = std::make_shared<const Segment>(Segment::Unreadable{}, dir_ / name, games, names, 0, !names_lost);
            unread.push_back({snap->segments_.size(),
                              {name, versions ? GameId{0} : snap->games_, games, versions, e.what(),
                               s.value("source", std::string()), names == 0 || segment->namesKnown()},
                              !names_lost});
            // A file missing, or one changed: said apart (sig.bin only where its format has one, as the error says).
            const std::string why = e.what();
            std::error_code ec;
            bool missing = !fs::is_directory(dir_ / name, ec);
            for (const char* file : {"cols.bin", "moves.bin", "annex.bin", "names.bin", "sig.bin"}) {
                if (std::string_view(file) == "sig.bin" && why.find("sig.bin") == std::string::npos) continue;
                missing = missing || !fs::exists(dir_ / name / file, ec);
            }
            unread.back().bad.missing = missing;
        }
        if (!versions) {
            snap->logical_.push_back(snap->segments_.size());
            snap->first_game_.push_back(snap->games_);
            snap->games_ += games;
        }
        snap->first_name_.push_back(snap->names_);
        snap->names_ += names;
        snap->segments_.push_back(std::move(segment));
        snap->segment_names_.push_back(name);
        snap->sources_.push_back(s.value("source", std::string()));
        snap->versions_.push_back(versions);
        named.push_back(name);
        unsigned long long number = 0;
        std::sscanf(name.c_str(), "seg-%llu", &number);
        next_segment = std::max<uint64_t>(next_segment, number + 1);
    }
    // Every game names its players, if only with the empty name: games without any name are a manifest made up, whose
    // games' name ids could not read as no name (rowIds()).
    if (snap->games_ > 0 && snap->names_ == 0) throw std::runtime_error(dir_.string() + ": damaged manifest");
    // Read-only, a segment that the other program removed as it committed (a compaction's, an undoing's) is no damage:
    // read again once its manifest has settled.
    if (!lock_ && !unread.empty() && tries > 0) {
        uint64_t now = snap->generation_ + 1;  // a manifest being replaced as it is read has changed too
        try {
            std::ifstream in(dir_ / MANIFEST, std::ios::binary);
            now = nlohmann::json::parse(in).value("generation", uint64_t{0});
        } catch (const std::exception&) {
        }
        if (now != snap->generation_) return load(clean, tries - 1);
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
        // Counts read from the file, compared with its size without overflowing (#102).
        const auto replaced = at(16, uint64_t{0}), deleted = at(24, uint64_t{0});
        const uint64_t room = bytes.size() - 32;
        if (replaced > room / 16 || (room - replaced * 16) % 8 != 0 || deleted != (room - replaced * 16) / 8)
            throw bad();
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
        // Counted from the games marked: a number written twice in a file made by hand counts once.
        for (const auto w : snap->deleted_) snap->deleted_count_ += static_cast<uint64_t>(std::popcount(w));
    }
    // The table of names it names.
    const auto players = manifest.value("players", std::string());
    if (!players.empty()) {
        std::ifstream in(dir_ / players, std::ios::binary);
        if (!in) throw std::runtime_error((dir_ / players).string() + ": missing");
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        snap->players_ =
            std::make_shared<const PlayerTable>(spellingsWithin(PlayerTable::fromBytes(text), snap->names_));
        snap->players_file_ = players;
    }
    // The segments that cannot be read: their one empty game named as games without names are.
    if (!unread.empty()) {
        const auto blank = snap->blankName().value_or(0);
        for (auto& u : unread) {
            const auto& entry        = manifest.at("segments")[u.index];
            snap->segments_[u.index] = std::make_shared<const Segment>(
                Segment::Unreadable{}, dir_ / u.bad.name, static_cast<uint32_t>(u.bad.games),
                entry.at("names").get<uint32_t>(), blank, u.names_known);
            snap->unreadable_segments_.push_back(std::move(u.bad));
        }
    }
    snap->settle();
    // What a commit cut short left: segments the manifest does not name, and the new manifest not yet renamed over
    // the old one (a killed program leaves it); edits it does not name. A number such a leftover segment holds is not
    // used again, should it not go.
    // Read-only, they are left alone: they may be the other program's commit under way.
    std::error_code ec;
    if (lock_ && clean) {
        for (const auto& entry : fs::directory_iterator(dir_, ec)) {
            const auto name      = entry.path().filename().string();
            const bool temporary = name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0;
            if (name.rfind("seg-", 0) == 0) {
                unsigned long long number = 0;
                if (std::sscanf(name.c_str(), "seg-%llu", &number) == 1)
                    next_segment = std::max<uint64_t>(next_segment, number + 1);
                if (temporary || std::find(named.begin(), named.end(), name) == named.end())
                    fs::remove_all(entry.path(), ec);
            } else if (temporary || (name.rfind("edits-", 0) == 0 && name != edits) ||
                       (name.rfind("players-", 0) == 0 && name != players)) {
                fs::remove_all(entry.path(), ec);
            }
        }
    }
    std::lock_guard lock(mutex_);
    next_segment_ = std::max(next_segment_, next_segment);
    current_      = std::move(snap);
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
                      const std::vector<Snapshot::Version>* replaced, const std::vector<uint64_t>* deleted,
                      const PlayerTable* players, const std::string& source) {
    if (!names_indexed_) {
        // A name lost with a segment that cannot be read is no game's to come: it reads "?" for its own.
        for (uint32_t id = 0; id < current_->names_; ++id)
            if (current_->nameKnown(id)) name_ids_.emplace(std::string(current_->name(id)), id);
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
    const auto edits_before   = manifest.value("edits", std::string());
    const auto players_before = manifest.value("players", std::string());
    std::string edits_written, players_written;
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

            std::string name;
            {
                std::lock_guard lock(mutex_);  // the numbers a compaction takes too, without the writers' lock
                name = segmentName(next_segment_++);
            }
            const auto tmp = dir_ / (name + ".tmp");
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
            if (!source.empty()) entry["source"] = source;
            manifest["segments"].push_back(std::move(entry));
        }
        if (players) {
            players_written = playersName(current_->generation_ + 1);
            writeFile(dir_ / players_written, players->bytes());
            manifest["players"] = players_written;
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
        if (!players_written.empty()) fs::remove(dir_ / players_written, ec);
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
        snap->segment_names_.push_back(made[s]);
        snap->sources_.push_back(source);
        snap->versions_.push_back(versions);
        snap->names_ += counts[s].second;
    }
    if (players) {
        snap->players_      = std::make_shared<const PlayerTable>(*players);
        snap->players_file_ = players_written;
    }
    if (replaced || deleted) snap->edits_generation_ = snap->generation_;
    if (replaced) snap->replaced_ = *replaced;
    if (deleted) {
        snap->deleted_       = *deleted;
        snap->deleted_count_ = 0;
        for (const auto w : snap->deleted_) snap->deleted_count_ += static_cast<uint64_t>(std::popcount(w));
    }
    snap->settle();
    // Published: the readers take it from now on, never having waited for the files written above.
    bool edits_kept = false, players_kept = false;
    {
        std::lock_guard lock(mutex_);
        const auto kept = [&](const std::string& name) {
            return leftovers_ &&
                   std::find(leftovers_->files.begin(), leftovers_->files.end(), name) != leftovers_->files.end();
        };
        edits_kept   = kept(edits_before);
        players_kept = kept(players_before);
        current_     = std::move(snap);
    }
    // The files before, unless a compaction keeps them for its undoing: read by nobody, they are in memory.
    if (!edits_written.empty() && !edits_before.empty() && edits_before != edits_written && !edits_kept) {
        std::error_code gone;
        fs::remove(dir_ / edits_before, gone);
    }
    if (!players_written.empty() && !players_before.empty() && players_before != players_written && !players_kept) {
        std::error_code gone;
        fs::remove(dir_ / players_before, gone);
    }
    removeReleased();
}

Database::Appended Database::append(std::vector<ImportedGame> games, bool keep_duplicates, const std::string& source,
                                    const std::vector<uint64_t>& forget) {
    if (!lock_) throw std::runtime_error(dir_.string() + ": read-only, another program writes to it");
    Appended appended;
    std::lock_guard writing(write_mutex_);
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
            if ((j > 0 && order[j - 1].first == key) ||
                (std::binary_search(known_keys_.begin(), known_keys_.end(), key) &&
                 !std::binary_search(forget.begin(), forget.end(), key)))
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
    uint64_t damaged = 0;
    for (const auto& game : games) damaged += game.damaged;
    commit(games, keys, false, nullptr, nullptr, nullptr, source);
    if (keys_indexed_) {
        // Merged in: a sort of the new keys only, however many the database has.
        const auto before = static_cast<std::ptrdiff_t>(known_keys_.size());
        known_keys_.insert(known_keys_.end(), keys.begin(), keys.end());
        std::sort(known_keys_.begin() + before, known_keys_.end());
        std::inplace_merge(known_keys_.begin(), known_keys_.begin() + before, known_keys_.end());
    }
    appended.added   = games.size();
    appended.damaged = damaged;
    return appended;
}

void Database::replace(GameId id, ImportedGame game) {
    if (!lock_) throw std::runtime_error(dir_.string() + ": read-only, another program writes to it");
    std::lock_guard writing(write_mutex_);
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
    std::lock_guard writing(write_mutex_);
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

// ---------------------------------------------------------------------------------------------------------------------
// Merges and their undoing.

namespace {

void setBit(std::vector<uint64_t>& bits, GameId id, bool on) {
    if (bits.size() <= id / 64) {
        if (!on) return;
        bits.resize(id / 64 + 1, 0);
    }
    if (on)
        bits[id / 64] |= uint64_t{1} << (id % 64);
    else
        bits[id / 64] &= ~(uint64_t{1} << (id % 64));
}

void trim(std::vector<uint64_t>& bits) {
    while (!bits.empty() && bits.back() == 0) bits.pop_back();
}

}  // namespace

std::vector<GameState> Database::merge(std::vector<Merge> merges) {
    if (!lock_) throw std::runtime_error(dir_.string() + ": read-only, another program writes to it");
    std::lock_guard writing(write_mutex_);
    std::vector<GameState> before;
    for (const auto& m : merges) {
        if (m.keep >= current_->games_) throw std::out_of_range("no game " + std::to_string(m.keep) + " to merge");
        before.push_back(current_->state(m.keep));
        for (const auto d : m.drop) {
            if (d >= current_->games_) throw std::out_of_range("no game " + std::to_string(d) + " to merge");
            before.push_back(current_->state(d));
        }
    }
    if (merges.empty()) return before;
    std::map<GameId, Snapshot::Version> versions;
    for (const auto& v : current_->replaced_) versions[v.id] = v;
    auto deleted = current_->deleted_;
    std::vector<ImportedGame> games;
    std::vector<uint64_t> keys;
    const auto first = current_->segments_.size();
    for (std::size_t k = 0; k < merges.size(); ++k) {
        auto& m          = merges[k];
        versions[m.keep] = {m.keep, static_cast<uint32_t>(first + k / SEGMENT_GAMES),
                            static_cast<uint32_t>(k % SEGMENT_GAMES)};
        setBit(deleted, m.keep, false);
        for (const auto d : m.drop)
            if (d != m.keep) setBit(deleted, d, true);
        keys.push_back(duplicateKey(m.game));
        games.push_back(std::move(m.game));
    }
    trim(deleted);
    std::vector<Snapshot::Version> replaced;
    replaced.reserve(versions.size());
    for (const auto& [id, v] : versions) replaced.push_back(v);
    commit(games, keys, true, &replaced, &deleted);
    keys_indexed_ = false;  // made again from the keys at the next append that looks for duplicates
    return before;
}

void Database::restore(std::span<const GameState> states) {
    if (!lock_) throw std::runtime_error(dir_.string() + ": read-only, another program writes to it");
    std::lock_guard writing(write_mutex_);
    std::map<GameId, Snapshot::Version> versions;
    for (const auto& v : current_->replaced_) versions[v.id] = v;
    auto deleted = current_->deleted_;
    // The states given in their order: the last of a game holds.
    for (const auto& state : states) {
        if (state.id >= current_->games_) continue;
        if (state.version) {
            const auto [segment, local] = *state.version;
            if (segment >= current_->segments_.size() || local >= current_->segments_[segment]->games() ||
                segment >= current_->versions_.size() || !current_->versions_[segment])
                throw std::runtime_error("the game's version is no longer in the database (compacted since)");
            versions[state.id] = {state.id, segment, local};
        } else {
            versions.erase(state.id);
        }
        setBit(deleted, state.id, state.deleted);
    }
    trim(deleted);
    std::vector<Snapshot::Version> replaced;
    for (const auto& [id, v] : versions) replaced.push_back(v);
    const bool same_versions = replaced.size() == current_->replaced_.size() &&
                               std::equal(replaced.begin(), replaced.end(), current_->replaced_.begin(),
                                          [](const Snapshot::Version& a, const Snapshot::Version& b) {
                                              return a.id == b.id && a.segment == b.segment && a.local == b.local;
                                          });
    if (same_versions && deleted == current_->deleted_) return;
    std::vector<ImportedGame> none;
    commit(none, {}, false, &replaced, &deleted);
    keys_indexed_ = false;
}

void Database::setPlayers(PlayerTable table) {
    if (!lock_) throw std::runtime_error(dir_.string() + ": read-only, another program writes to it");
    std::lock_guard writing(write_mutex_);
    table = spellingsWithin(std::move(table), current_->names_);
    std::vector<ImportedGame> none;
    commit(none, {}, false, nullptr, nullptr, &table);
}

void Database::clear() {
    if (!lock_) throw std::runtime_error(dir_.string() + ": read-only, another program writes to it");
    std::lock_guard writing(write_mutex_);
    const auto before = snapshot();
    nlohmann::json manifest;
    {
        std::ifstream in(dir_ / MANIFEST, std::ios::binary);
        manifest = nlohmann::json::parse(in);
    }
    const auto edits_before   = manifest.value("edits", std::string());
    const auto players_before = manifest.value("players", std::string());
    // As a database just made, its generation going on; every game taken out, as edits do: what was built from the
    // games before (an opening tree) no longer holds, nor what the window keeps by generation. A compaction under way
    // stops before its commit for it.
    const uint64_t generation    = before->generation_ + 1;
    manifest["segments"]         = nlohmann::json::array();
    manifest["edits_generation"] = generation;
    manifest["generation"]       = generation;
    manifest.erase("edits");
    manifest.erase("players");
    step("write manifest");
    tour::replaceFile(dir_ / MANIFEST, manifest.dump(1));
    step("manifest written");
    auto snap               = std::make_shared<Snapshot>();
    snap->generation_       = generation;
    snap->edits_generation_ = generation;
    snap->settle();
    bool edits_kept = false, players_kept = false;
    {
        std::lock_guard lock(mutex_);
        const auto kept = [&](const std::string& name) {
            return leftovers_ &&
                   std::find(leftovers_->files.begin(), leftovers_->files.end(), name) != leftovers_->files.end();
        };
        edits_kept   = kept(edits_before);
        players_kept = kept(players_before);
        current_     = std::move(snap);
    }
    name_ids_.clear();
    names_indexed_ = false;
    known_keys_.clear();
    keys_indexed_ = false;
    // The segments before, still read by the snapshots taken before this commit: their files go once none holds them.
    for (std::size_t s = 0; s < before->segments_.size(); ++s)
        released_.push_back({before->segment_names_[s], before->segments_[s]});
    std::error_code gone;
    if (!edits_before.empty() && !edits_kept) fs::remove(dir_ / edits_before, gone);  // in memory, read by nobody
    if (!players_before.empty() && !players_kept) fs::remove(dir_ / players_before, gone);
    removeReleased();  // an earlier clear's, let go of since
}

void Database::removeReleased(bool closing) {
    std::erase_if(released_, [&](const Released& r) {
        if (!closing && !r.segment.expired()) return false;
        // Closing, those a snapshot still maps go too where the system lets them (Windows keeps them for the next
        // opening, which removes the segments its manifest does not name).
        std::error_code ec;
        fs::remove_all(dir_ / r.name, ec);
        return !ec || closing;
    });
}

// ---------------------------------------------------------------------------------------------------------------------
// The check.

namespace {

// Whether segment record i reads as it was written: its names within the database's, its offsets within its files,
// its annex whole, its moves replayed to their end, its signature and its key made again the same.
bool recordSound(const Snapshot& snap, const Segment& segment, std::size_t i) {
    try {
        for (int c = 0; c < NAME_COLUMNS; ++c)
            if (segment.value<uint32_t>(static_cast<Column>(c), i) >= snap.names()) return false;
        if (!segment.offsetsSound(i)) return false;
        const auto game  = recordGame(snap, segment, i, true);  // throws on a damaged annex
        const auto flags = segment.value<uint16_t>(FLAGS, i);
        ScanBoard board;
        const bool start = board.setFen(
            game.start_fen.empty() ? std::string_view(chess::constants::STARTPOS) : std::string_view(game.start_fen),
            game.chess960);
        if (!start) {
            if (!(flags & GAME_DAMAGED) || game.plies != 0 || !game.moves.empty()) return false;
        } else {
            SignatureMaker signature(board);
            const auto& codes = game.moves;
            std::size_t at    = 0;
            for (uint32_t ply = 0; ply < game.plies; ++ply) {
                const int count = board.codeCount();
                if (at >= codes.size()) return false;
                int code = codes[at++];
                if (count > 256) {
                    if (at >= codes.size()) return false;
                    code = code << 8 | codes[at++];
                }
                if (code >= count) return false;
                const auto move = board.decode(code);
                if (move.from >= 64 || move.to >= 64) return false;
                signature.before(board, move, ply);
                board.play(move);
            }
            if (at != codes.size()) return false;
            if (const auto* stored = segment.signatureBytes(i); stored && (stored[21] & SIG_KNOWN)) {
                const auto again = signature.finish(board);
                if (std::memcmp(stored, again.home.data(), 16) != 0) return false;
                for (int j = 0; j < 5; ++j)
                    if (stored[16 + j] != static_cast<uint8_t>((again.material[2 * j] & 0x0F) |
                                                               (std::min<uint8_t>(again.material[2 * j + 1], 15) << 4)))
                        return false;
                if (((stored[21] & SIG_PROMOTED) != 0) != again.promoted) return false;
            }
        }
        // A name lost with a segment that cannot be read reads "?": the key made of it cannot be made again.
        const bool known =
            snap.nameKnown(segment.value<uint32_t>(WHITE, i)) && snap.nameKnown(segment.value<uint32_t>(BLACK, i)) &&
            snap.nameKnown(segment.value<uint32_t>(EVENT, i)) && snap.nameKnown(segment.value<uint32_t>(SITE, i)) &&
            snap.nameKnown(segment.value<uint32_t>(ROUND, i));
        return !known || duplicateKey(game) == segment.value<uint64_t>(KEY, i);
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace

CheckReport Database::check(CheckProgress& progress, const std::atomic<bool>& cancel, int threads) const {
    const auto started = std::chrono::steady_clock::now();
    const auto snap    = snapshot();
    CheckReport report;
    const auto infos = snap->segmentInfos();
    uint64_t total   = 0;
    for (const auto& info : infos) total += info.games;
    progress.total = total;
    progress.done  = 0;
    if (threads <= 0) threads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    // The game a record is the current version of, if any.
    std::map<std::pair<uint32_t, uint32_t>, GameId> current_version;
    for (const auto& v : snap->replaced_) current_version[{v.segment, v.local}] = v.id;
    const auto replaced = [&](GameId id) {
        const auto v = std::lower_bound(snap->replaced_.begin(), snap->replaced_.end(), id,
                                        [](const Snapshot::Version& r, GameId x) { return r.id < x; });
        return v != snap->replaced_.end() && v->id == id;
    };
    std::vector<GameId> damaged;
    std::mutex found;
    std::atomic<uint64_t> checked{0};
    const Inherited inherited;
    for (std::size_t s = 0; s < infos.size() && !cancel; ++s) {
        const auto& info = infos[s];
        // The game record i is, when it is one as the database reads it; none for an old version.
        const auto gameOf = [&](std::size_t i) -> std::optional<GameId> {
            if (info.versions) {
                const auto it = current_version.find({static_cast<uint32_t>(s), static_cast<uint32_t>(i)});
                if (it == current_version.end()) return std::nullopt;
                return it->second;
            }
            const GameId id = info.first + i;
            if (replaced(id)) return std::nullopt;
            return id;
        };
        std::shared_ptr<const Segment> segment;
        const auto names = snap->segments_[s]->nameCount();
        try {
            segment = std::make_shared<const Segment>(dir_ / info.name, info.games, names);
        } catch (const std::exception& e) {
            MappedFile file;
            std::vector<uint32_t> offsets;
            const bool names_kept =
                names == 0 || Segment::readNames(dir_ / info.name / "names.bin", names, file, offsets);
            report.segments.push_back(
                {info.name, info.first, info.games, info.versions, e.what(), info.source, names_kept});
            for (std::size_t i = 0; i < info.games; ++i)
                if (const auto id = gameOf(i); id && !snap->deletedByEdit(*id)) damaged.push_back(*id);
            progress.done += info.games;
            continue;
        }
        if (segment->version() < 2 && info.games > 0) report.signatures = false;
        std::atomic<std::size_t> next{0};
        std::vector<std::thread> pool;
        const auto parts = std::min<std::size_t>(static_cast<std::size_t>(threads), info.games / 4096 + 1);
        for (std::size_t t = 0; t < parts; ++t)
            pool.emplace_back([&] {
                inherited.apply();
                std::vector<GameId> mine;
                for (;;) {
                    const auto from = next.fetch_add(4096);
                    if (from >= info.games || cancel.load(std::memory_order_relaxed)) break;
                    const auto to = std::min<std::size_t>(from + 4096, info.games);
                    for (std::size_t i = from; i < to; ++i) {
                        const auto id = gameOf(i);
                        if (!id || snap->deleted(*id)) continue;  // an old version, or a game no one reads
                        ++checked;
                        if (!recordSound(*snap, *segment, i)) mine.push_back(*id);
                    }
                    progress.done += to - from;
                }
                std::lock_guard lock(found);
                damaged.insert(damaged.end(), mine.begin(), mine.end());
            });
        for (auto& t : pool) t.join();
    }
    report.games     = checked.load();
    report.cancelled = cancel.load();
    std::sort(damaged.begin(), damaged.end());
    damaged.erase(std::unique(damaged.begin(), damaged.end()), damaged.end());
    for (const auto id : damaged) ++report.damaged_sources[snap->source(id)];
    report.damaged = std::move(damaged);
    try {
        const auto tree = Tree::open(dir_);
        report.tree     = tree && !tree->stale(*snap) && tree->games() == snap->games();
    } catch (const std::exception&) {
        report.tree = false;
    }
    report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return report;
}

// ---------------------------------------------------------------------------------------------------------------------
// The compaction.

namespace {

// Whether a deleted game's record still holds its moves or its annex: what a compaction takes away.
bool heldDeleted(const Snapshot& snap, GameId id) { return snap.deleted(id) && snap.storedBytes(id) > 0; }

// Whether a logical segment holds a game replaced, or a deleted game not emptied yet.
bool staleSegment(const Snapshot& snap, const Snapshot::SegmentInfo& info) {
    if (info.versions) return false;
    for (GameId id = info.first; id < info.first + info.games; ++id)
        if (snap.state(id).version || heldDeleted(snap, id)) return true;
    return false;
}

}  // namespace

CompactPlan Database::compactPlan() const {
    const auto snap = snapshot();
    CompactPlan plan;
    if (!snap->unreadable().empty()) return plan;  // keepReadable() first: those segments cannot be copied
    const auto infos = snap->segmentInfos();
    // What the segments written again take now, and what their new files will take.
    uint64_t before = 0, after = 0;
    for (std::size_t s = 0; s < infos.size(); ++s) {
        const auto& info = infos[s];
        const auto& seg  = *snap->segments_[s];
        if (info.versions) {
            if (info.games == 0) continue;  // its names only: compacted already
            plan.segments.push_back(info.name);
            before += seg.bytes();
            after += layout(0)[COLUMNS] + seg.namesText().size();
            continue;
        }
        if (!staleSegment(*snap, info)) continue;
        plan.segments.push_back(info.name);
        uint64_t kept = layout(info.games)[COLUMNS] + seg.namesText().size() + uint64_t{info.games} * SIG_BYTES;
        for (GameId id = info.first; id < info.first + info.games; ++id) {
            if (snap->deleted(id)) {
                if (snap->storedBytes(id) > 0) ++plan.deleted;
                continue;
            }
            if (snap->state(id).version) ++plan.replaced;
            kept += snap->storedBytes(id);
        }
        before += seg.bytes();
        after += kept;
    }
    if (plan.segments.empty()) return CompactPlan{};
    plan.needed = after;
    plan.bytes  = before > after ? before - after : 0;
    return plan;
}

namespace {

// A segment's files written in folder `tmp`: `n` records, each copied from (segment, record) or emptied when deleted,
// its names those of `names`. A record without a segment is a game set aside whose record could not be read: no
// headers, its names `blank_name`.
struct RecordSource {
    const Segment* segment = nullptr;
    std::size_t index      = 0;
    bool emptied           = false;  // a deleted game: its headers only
};

void writeSegment(const fs::path& tmp, const std::vector<RecordSource>& records, std::string_view names,
                  uint32_t name_count, const std::atomic<bool>& cancel, std::atomic<uint64_t>& done,
                  uint32_t blank_name = 0) {
    const auto n  = records.size();
    const auto at = layout(n);
    std::string cols(at[COLUMNS], '\0'), moves, annex, sig(n * SIG_BYTES, '\0');
    std::memcpy(cols.data(), MAGIC.data(), MAGIC.size());
    const auto put32 = [&](std::size_t offset, uint32_t v) { std::memcpy(cols.data() + offset, &v, 4); };
    const auto put   = [&](Column column, std::size_t i, auto v) {
        std::memcpy(cols.data() + at[column] + i * sizeof(v), &v, sizeof(v));
    };
    put32(8, VERSION);
    put32(12, static_cast<uint32_t>(n));
    put32(16, name_count);
    for (std::size_t k = 0; k < n; ++k) {
        if ((k & 0xFFF) == 0 && cancel.load(std::memory_order_relaxed)) throw std::runtime_error("cancelled");
        const auto& r = records[k];
        if (!r.segment) {  // set aside: nothing kept of it but its number
            for (int c = 0; c < NAME_COLUMNS; ++c) put(static_cast<Column>(c), k, blank_name);
            put(RESULT, k, static_cast<uint8_t>('*'));
            put(MOVES, k, static_cast<uint64_t>(moves.size()));
            put(ANNEX, k, static_cast<uint64_t>(annex.size()));
            ++done;
            continue;
        }
        const auto& seg = *r.segment;
        for (int c = 0; c < NAME_COLUMNS; ++c)
            put(static_cast<Column>(c), k, seg.value<uint32_t>(static_cast<Column>(c), r.index));
        put(DATE, k, seg.value<uint32_t>(DATE, r.index));
        put(WHITE_ELO, k, seg.value<uint16_t>(WHITE_ELO, r.index));
        put(BLACK_ELO, k, seg.value<uint16_t>(BLACK_ELO, r.index));
        put(ECO, k, seg.value<uint16_t>(ECO, r.index));
        put(RESULT, k, seg.value<uint8_t>(RESULT, r.index));
        put(KEY, k, seg.value<uint64_t>(KEY, r.index));
        put(MOVES, k, static_cast<uint64_t>(moves.size()));
        put(ANNEX, k, static_cast<uint64_t>(annex.size()));
        if (r.emptied) {
            put(PLIES, k, uint32_t{0});
            put(FLAGS, k, uint16_t{0});
        } else {
            put(PLIES, k, seg.value<uint32_t>(PLIES, r.index));
            put(FLAGS, k, seg.value<uint16_t>(FLAGS, r.index));
            const auto m = seg.moves(r.index), a = seg.annex(r.index);
            moves.append(reinterpret_cast<const char*>(m.data()), m.size());
            annex.append(reinterpret_cast<const char*>(a.data()), a.size());
            if (const auto* bytes = seg.signatureBytes(r.index)) {
                std::memcpy(sig.data() + k * SIG_BYTES, bytes, SIG_BYTES);
            } else {
                // A segment of the first format: the signature made from the moves.
                ScanBoard start;
                const auto flags = seg.value<uint16_t>(FLAGS, r.index);
                std::string fen;
                if (flags & GAME_FEN) {
                    const uint8_t* p   = a.data();
                    const uint8_t* end = p + a.size();
                    if (p < end && (*p++ & ANNEX_FEN)) fen = getText(p, end);
                }
                if (start.setFen(fen.empty() ? std::string_view(chess::constants::STARTPOS) : std::string_view(fen),
                                 flags & GAME_CHESS960))
                    if (const auto signature = signatureOf(start, m, seg.value<uint32_t>(PLIES, r.index))) {
                        char* out = sig.data() + k * SIG_BYTES;
                        std::memcpy(out, signature->home.data(), 16);
                        for (int j = 0; j < 5; ++j)
                            out[16 + j] =
                                static_cast<char>((signature->material[2 * j] & 0x0F) |
                                                  (std::min<uint8_t>(signature->material[2 * j + 1], 15) << 4));
                        out[21] = static_cast<char>(SIG_KNOWN | (signature->promoted ? SIG_PROMOTED : 0));
                    }
            }
        }
        ++done;
    }
    put(MOVES, n, static_cast<uint64_t>(moves.size()));
    put(ANNEX, n, static_cast<uint64_t>(annex.size()));
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    if (ec) throw std::runtime_error("cannot create " + tmp.string());
    writeFile(tmp / "cols.bin", cols);
    writeFile(tmp / "moves.bin", moves);
    writeFile(tmp / "annex.bin", annex);
    writeFile(tmp / "names.bin", names);
    writeFile(tmp / "sig.bin", sig);
    syncFolder(tmp);
}

}  // namespace

Compacted Database::compact(CompactProgress& progress, const std::atomic<bool>& cancel, bool one_at_a_time) {
    Compacted out;
    if (!lock_) {
        out.error = dir_.string() + ": read-only, another program writes to it";
        return out;
    }
    // One compaction at a time. The segments are written without the writers' lock, from the snapshot of when it
    // starts: games added meanwhile are kept; a game replaced or deleted meanwhile stops it (compact again).
    std::lock_guard compacting(compact_mutex_);
    auto snap = snapshot();
    if (!snap->unreadable().empty()) {
        out.error = "a segment cannot be read: keep what can be read first";
        return out;
    }
    const auto infos = snap->segmentInfos();
    const auto plan  = compactPlan();
    std::vector<std::string> unremoved;  // one segment at a time: old files still mapped, removed at the end
    if (plan.segments.empty()) {
        out.done = true;
        return out;
    }
    if (!one_at_a_time) {
        std::error_code ec;
        const auto space = fs::space(dir_, ec);
        if (!ec && space.available < plan.needed + (uint64_t{64} << 20)) {
            out.error = "disk full";
            return out;
        }
    }
    std::string manifest_text;
    {
        std::ifstream in(dir_ / MANIFEST, std::ios::binary);
        manifest_text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const auto old_edits = nlohmann::json::parse(manifest_text).value("edits", std::string());
    // The steps: all the segments at once; or each logical segment, then the segments of versions.
    std::vector<std::size_t> logical_stale, versions;
    for (std::size_t s = 0; s < infos.size(); ++s) {
        if (infos[s].versions) {
            if (infos[s].games > 0) versions.push_back(s);
        } else if (staleSegment(*snap, infos[s])) {
            logical_stale.push_back(s);
        }
    }
    std::vector<std::vector<std::size_t>> steps;
    if (one_at_a_time) {
        for (const auto s : logical_stale) steps.push_back({s});
        if (!versions.empty()) steps.push_back(versions);
    } else {
        auto all = logical_stale;
        all.insert(all.end(), versions.begin(), versions.end());
        steps.push_back(all);
    }
    progress.segments = logical_stale.size() + versions.size();
    progress.segment  = 0;
    auto replaced     = snap->replaced_;  // what stays replaced, the segments folded so far taken out
    std::vector<std::string> old_files, new_files;
    uint64_t old_bytes    = 0;
    std::string edits_now = old_edits;  // the edits file the manifest names
    std::string manifest_after;
    for (const auto& batch : steps) {
        std::vector<std::pair<std::size_t, std::string>> written;  // a segment's index, its new folder
        std::string edits_written;
        std::string tmp_now;
        const auto clean = [&] {
            std::error_code ec;
            for (const auto& [s, name] : written) fs::remove_all(dir_ / name, ec);
            if (!tmp_now.empty()) fs::remove_all(dir_ / tmp_now, ec);
            if (!edits_written.empty()) fs::remove(dir_ / edits_written, ec);
        };
        try {
            for (const auto s : batch) {
                ++progress.segment;
                progress.games   = 0;
                const auto& info = infos[s];
                const auto& seg  = *snap->segments_[s];
                std::vector<RecordSource> records;
                if (!info.versions) {
                    records.reserve(info.games);
                    for (GameId id = info.first; id < info.first + info.games; ++id) {
                        const auto [where, i] = snap->locate(id);
                        records.push_back({where, i, snap->deleted(id)});
                    }
                }
                std::string name;
                {
                    std::lock_guard lock(mutex_);  // the numbers the appends take too
                    name = segmentName(next_segment_++);
                }
                tmp_now = name + ".tmp";
                writeSegment(dir_ / tmp_now, records, seg.namesText(), seg.nameCount(), cancel, progress.games);
                step("rename segment");
                std::error_code ec;
                fs::rename(dir_ / tmp_now, dir_ / name, ec);
                if (ec) throw std::runtime_error("cannot rename " + (dir_ / tmp_now).string());
                tmp_now.clear();
                written.emplace_back(s, name);
            }
            syncFolder(dir_);
            if (cancel) throw std::runtime_error("cancelled");
        } catch (const std::exception& e) {
            // Nothing of this step committed: its files go. Those of the steps before stay committed.
            clean();
            out.cancelled = cancel.load();
            out.error     = out.cancelled ? std::string() : std::string(e.what());
            return out;
        }
        {
            // The commit, under the writers' lock: the manifest as it is now (the segments added meanwhile kept)
            // naming the new segments, the edits without the versions folded in, the edits generation kept: the
            // games read the same, and the opening tree holds.
            std::lock_guard writing(write_mutex_);
            try {
                if (snapshot()->editsGeneration() != snap->editsGeneration())
                    throw std::runtime_error("a game was replaced or deleted meanwhile");
                nlohmann::json next;
                {
                    std::ifstream in(dir_ / MANIFEST, std::ios::binary);
                    next = nlohmann::json::parse(in);
                }
                for (const auto& [s, name] : written) {
                    auto& entry   = next["segments"][s];
                    entry["name"] = name;
                    if (infos[s].versions) {
                        entry["games"] = 0;
                    } else {
                        const auto first = infos[s].first, last = first + infos[s].games;
                        std::erase_if(replaced,
                                      [&](const Snapshot::Version& v) { return v.id >= first && v.id < last; });
                    }
                }
                const auto generation = next.value("generation", uint64_t{0}) + 1;
                if (!replaced.empty() || !snap->deleted_.empty()) {
                    edits_written = editsName(generation);
                    writeFile(dir_ / edits_written, editsBytes(replaced, snap->deleted_));
                    next["edits"] = edits_written;
                } else {
                    next.erase("edits");
                }
                next["generation"] = generation;
                step("write manifest");
                tour::replaceFile(dir_ / MANIFEST, next.dump(1));
                manifest_after = next.dump(1);
            } catch (const std::exception& e) {
                clean();
                out.error = e.what();
                return out;
            }
            // Committed: read again, the old files kept for now.
            step("manifest written");
            load(false);
            keys_indexed_ = false;
        }
        std::vector<std::string> step_old;
        for (const auto& [s, name] : written) {
            step_old.push_back(infos[s].name);
            new_files.push_back(name);
            old_bytes += snap->segments_[s]->bytes();
        }
        if (!edits_written.empty()) new_files.push_back(edits_written);
        if (edits_now != edits_written && !edits_now.empty()) step_old.push_back(edits_now);
        edits_now = edits_written;
        if (one_at_a_time) {
            for (const auto& name : step_old) {
                std::error_code ec;
                fs::remove_all(dir_ / name, ec);
                if (ec) unremoved.push_back(name);  // Windows: mapped by the snapshot of the start
            }
        } else {
            old_files.insert(old_files.end(), step_old.begin(), step_old.end());
        }
    }
    // What was given back: the old segments' bytes less the new ones'.
    const auto now     = snapshot();
    uint64_t new_bytes = 0;
    for (const auto& name : new_files)
        for (std::size_t s = 0; s < now->segment_names_.size(); ++s)
            if (now->segment_names_[s] == name) new_bytes += now->segments_[s]->bytes();
    out.bytes = old_bytes > new_bytes ? old_bytes - new_bytes : 0;
    out.done  = true;
    if (one_at_a_time) {
        // The start's snapshot let go: what Windows kept mapped can go now (what a reader still maps, at the next open).
        snap.reset();
        for (const auto& name : unremoved) {
            std::error_code ec;
            fs::remove_all(dir_ / name, ec);
        }
        return out;
    }
    // Undoing it puts the manifest before back, unless something was committed since: the segments added meanwhile
    // would go with it.
    Leftovers left;
    left.manifest   = now->generation() == snap->generation() + 1 ? manifest_text : std::string();
    left.generation = now->generation();
    left.files      = old_files;
    left.new_files  = new_files;
    std::lock_guard lock(mutex_);
    leftovers_   = std::move(left);
    out.undoable = !leftovers_->manifest.empty();
    return out;
}

bool Database::compactionUndoable() const {
    std::lock_guard lock(mutex_);
    return leftovers_ && !leftovers_->manifest.empty() && current_->generation_ == leftovers_->generation;
}

bool Database::undoCompaction() {
    std::lock_guard compacting(compact_mutex_);
    std::lock_guard writing(write_mutex_);
    nlohmann::json manifest;
    {
        std::lock_guard lock(mutex_);
        if (!leftovers_ || leftovers_->manifest.empty() || current_->generation_ != leftovers_->generation)
            return false;
        manifest               = nlohmann::json::parse(leftovers_->manifest);
        manifest["generation"] = current_->generation_ + 1;  // readers see a commit
    }
    tour::replaceFile(dir_ / MANIFEST, manifest.dump(1));
    std::vector<std::string> made;
    {
        std::lock_guard lock(mutex_);
        made = leftovers_->new_files;
        leftovers_.reset();
    }
    load(true);  // the compaction's files, no longer named, go
    // Again, once the snapshot that mapped them is let go (Windows removes no mapped file); a reader holding it
    // still leaves them to the next open.
    for (const auto& name : made) {
        std::error_code ec;
        fs::remove_all(dir_ / name, ec);
    }
    keys_indexed_ = false;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// The segments that cannot be read, written again (#97).

uint64_t Database::keepReadable(std::span<const GameId> damaged) {
    if (!lock_) throw std::runtime_error(dir_.string() + ": read-only, another program writes to it");
    std::lock_guard compacting(compact_mutex_);
    std::lock_guard writing(write_mutex_);
    const auto snap = snapshot();
    // What is set aside: the games that cannot be read, and the damaged ones given.
    auto deleted       = snap->deleted_;
    uint64_t set_aside = 0;
    const auto aside   = [&](GameId id) {
        if (id >= snap->games_ || (id / 64 < deleted.size() && (deleted[id / 64] >> (id % 64) & 1))) return;
        setBit(deleted, id, true);
        ++set_aside;
    };
    for (const auto id : snap->unreadableIds()) aside(id);
    for (const auto id : damaged) aside(id);
    if (snap->unreadable_segments_.empty() && set_aside == 0) return 0;

    std::string manifest_text;
    {
        std::ifstream in(dir_ / MANIFEST, std::ios::binary);
        manifest_text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    auto manifest             = nlohmann::json::parse(manifest_text);
    const auto old_edits      = manifest.value("edits", std::string());
    const uint64_t generation = snap->generation_ + 1;
    const auto blank          = snap->blankName();
    const uint32_t blank_name = blank.value_or(snap->names_);  // none: a name "" added after the others
    std::vector<std::string> written, old_files;
    std::string tmp_now, edits_written;
    try {
        const auto made = [&](std::vector<RecordSource> records, std::string_view names, uint32_t count) {
            std::string name;
            {
                std::lock_guard lock(mutex_);  // the numbers the appends take too
                name = segmentName(next_segment_++);
            }
            tmp_now = name + ".tmp";
            std::atomic<bool> go_on{false};
            std::atomic<uint64_t> done{0};
            writeSegment(dir_ / tmp_now, records, names, count, go_on, done, blank_name);
            step("rename segment");
            std::error_code ec;
            fs::rename(dir_ / tmp_now, dir_ / name, ec);
            if (ec) throw std::runtime_error("cannot rename " + (dir_ / tmp_now).string() + ": " + ec.message());
            tmp_now.clear();
            written.push_back(name);
            return name;
        };
        for (std::size_t s = 0; s < snap->segments_.size(); ++s) {
            const auto& segment = *snap->segments_[s];
            if (!segment.unreadable()) continue;
            // Its names as they read, or "?" for each when they cannot: the later games' ids of names hold.
            std::string names;
            if (segment.namesKnown()) {
                names = segment.namesText();
            } else {
                names.reserve(std::size_t{segment.nameCount()} * 2);
                for (uint32_t k = 0; k < segment.nameCount(); ++k) names += "?\n";
            }
            auto& entry = manifest["segments"][s];
            old_files.push_back(entry.at("name").get<std::string>());
            entry["name"] = made(std::vector<RecordSource>(segment.games()), names, segment.nameCount());
            if (!segment.namesKnown() && segment.nameCount() > 0) entry["unknown_names"] = true;
        }
        if (!blank && !snap->unreadable_segments_.empty())  // a segment of names only, as a compaction leaves them
            manifest["segments"].push_back(
                {{"name", made({}, "\n", 1)}, {"games", 0}, {"names", 1}, {"versions", true}});
        syncFolder(dir_);
        edits_written = editsName(generation);
        writeFile(dir_ / edits_written, editsBytes(snap->replaced_, deleted));
        manifest["edits"]            = edits_written;
        manifest["edits_generation"] = generation;
        manifest["generation"]       = generation;
        step("write manifest");
        tour::replaceFile(dir_ / MANIFEST, manifest.dump(1));
    } catch (const std::exception&) {
        // Nothing committed: what it wrote goes, the database as before.
        std::error_code ec;
        for (const auto& name : written) fs::remove_all(dir_ / name, ec);
        if (!tmp_now.empty()) fs::remove_all(dir_ / tmp_now, ec);
        if (!edits_written.empty()) fs::remove(dir_ / edits_written, ec);
        throw;
    }
    // Committed: read again, the damaged files kept until the database is closed (or the commit undone).
    step("manifest written");
    load(false);
    keys_indexed_  = false;
    names_indexed_ = false;
    name_ids_.clear();
    if (!old_edits.empty() && old_edits != edits_written) old_files.push_back(old_edits);
    Leftovers left;
    left.manifest   = manifest_text;
    left.generation = generation;
    left.files      = std::move(old_files);
    left.new_files  = std::move(written);
    left.new_files.push_back(edits_written);
    std::lock_guard lock(mutex_);
    if (kept_)  // an earlier one's, no longer undone: its files go at closing all the same
        dropped_.insert(dropped_.end(), kept_->files.begin(), kept_->files.end());
    kept_ = std::move(left);
    return set_aside;
}

bool Database::keepReadableUndoable() const {
    std::lock_guard lock(mutex_);
    return kept_ && !kept_->manifest.empty() && current_->generation_ == kept_->generation;
}

bool Database::undoKeepReadable() {
    std::lock_guard compacting(compact_mutex_);
    std::lock_guard writing(write_mutex_);
    nlohmann::json manifest;
    std::vector<std::string> made;
    {
        std::lock_guard lock(mutex_);
        if (!kept_ || kept_->manifest.empty() || current_->generation_ != kept_->generation) return false;
        manifest               = nlohmann::json::parse(kept_->manifest);
        manifest["generation"] = current_->generation_ + 1;  // readers see a commit
    }
    tour::replaceFile(dir_ / MANIFEST, manifest.dump(1));
    {
        std::lock_guard lock(mutex_);
        made = kept_->new_files;
        kept_.reset();
    }
    load(true);                      // the files it wrote, no longer named, go; those it set aside are named again
    for (const auto& name : made) {  // again, once their snapshot is let go (Windows removes no mapped file)
        std::error_code ec;
        fs::remove_all(dir_ / name, ec);
    }
    keys_indexed_  = false;
    names_indexed_ = false;
    name_ids_.clear();
    return true;
}

void Database::dropLeftovers() {
    if (!lock_) return;
    std::error_code ec;
    if (leftovers_)
        for (const auto& name : leftovers_->files) fs::remove_all(dir_ / name, ec);
    if (kept_)
        for (const auto& name : kept_->files) fs::remove_all(dir_ / name, ec);
    for (const auto& name : dropped_) fs::remove_all(dir_ / name, ec);
    leftovers_.reset();
    kept_.reset();
    dropped_.clear();
    removeReleased(true);
}

Database::~Database() {
    try {
        dropLeftovers();
    } catch (...) {
    }
}

}  // namespace fcg::study
