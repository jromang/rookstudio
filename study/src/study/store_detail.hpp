#pragma once

// The store's own parts (store.hpp), shared by its files: store.cpp (the snapshots, the commits, the edits),
// store_check.cpp (the check) and store_compact.cpp (the compaction, Keep what can be read). A segment's files and
// their bytes, a segment as it is read and its records read as games, the files a commit writes, and the steps the
// tests stop a commit at.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <json.hpp>

#include "import.hpp"
#include "mapped_file.hpp"
#include "signature.hpp"
#include "store.hpp"
#include "store_error.hpp"

namespace fcg::study::store {

inline constexpr std::array<char, 8> MAGIC{'F', 'C', 'G', 'S', 'E', 'G', '1', '\0'};
inline constexpr uint32_t VERSION        = 2;  // 2: sig.bin, the games' signatures
inline constexpr std::size_t SIG_BYTES   = 24;
inline constexpr uint8_t SIG_KNOWN       = 1;
inline constexpr uint8_t SIG_PROMOTED    = 2;
inline constexpr const char* MANIFEST    = "manifest.json";
inline constexpr std::size_t HEADER_SIZE = 32;

// Called at each step of a commit with its name: what the tests stop it at (setCommitFaultHook()).
void step(std::string_view name);

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
inline constexpr int NAME_COLUMNS = 7;

inline std::size_t columnBytes(int column, std::size_t n) {
    if (column <= PLIES) return 4 * n;
    if (column <= FLAGS) return 2 * n;
    if (column == RESULT) return n;
    if (column == KEY) return 8 * n;
    return 8 * (n + 1);
}

inline std::array<std::size_t, COLUMNS + 1> layout(std::size_t n) {
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

inline void putVarint(std::string& out, uint64_t v) {
    while (v >= 0x80) {
        out += static_cast<char>(0x80 | (v & 0x7F));
        v >>= 7;
    }
    out += static_cast<char>(v);
}
inline uint64_t getVarint(const uint8_t*& p, const uint8_t* end) {
    uint64_t v = 0;
    for (int shift = 0; p < end && shift < 64; shift += 7) {
        const uint8_t b = *p++;
        v |= uint64_t{b & 0x7Fu} << shift;
        if (!(b & 0x80)) return v;
    }
    throw std::runtime_error("damaged annex record");
}
inline void putText(std::string& out, std::string_view s) {
    putVarint(out, s.size());
    out.append(s);
}
inline std::string getText(const uint8_t*& p, const uint8_t* end) {
    const auto size = getVarint(p, end);
    if (size > static_cast<uint64_t>(end - p)) throw std::runtime_error("damaged annex record");
    std::string s(reinterpret_cast<const char*>(p), static_cast<std::size_t>(size));
    p += size;
    return s;
}
inline uint64_t zigzag(int64_t v) { return (static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63); }
inline int64_t unzigzag(uint64_t v) { return static_cast<int64_t>(v >> 1) ^ -static_cast<int64_t>(v & 1); }

// The annex: what the columns do not hold.
enum AnnexPart : uint8_t { ANNEX_FEN = 1, ANNEX_TAGS = 2, ANNEX_TEXT = 4, ANNEX_CLOCKS = 8, ANNEX_EVALS = 16 };

void syncFolder(const std::filesystem::path& dir);

// What a write the system refused tells the window: the disk full (errno), or another reason.
StoreErrc writeErrc(int error) noexcept;
StoreErrc writeErrc(const std::error_code& ec) noexcept;

// A change refused: another program writes to the database (it holds the folder's lock).
StoreError readOnlyError(const std::filesystem::path& dir);

// A file written whole and synced, a step before each: StoreError when the system refuses.
void writeFile(const std::filesystem::path& file, std::string_view bytes);

inline constexpr std::array<char, 8> EDITS_MAGIC{'F', 'C', 'G', 'E', 'D', 'I', 'T', '1'};

std::string editsName(uint64_t generation);

// The edits a manifest names: the games replaced (their versions' places) and the games deleted.
std::string editsBytes(const std::vector<Snapshot::Version>& replaced, const std::vector<uint64_t>& deleted);

std::string segmentName(uint64_t number);

// manifest.json, the only file a commit replaces (by a rename: tour::replaceFile()): the segments in their order, the
// edits and the table of names it names, its generation. Read and written here alone. A manifest this program wrote
// reads back to the same bytes; the keys it does not know (a newer program's) are kept as they are.
struct Manifest {
    struct Entry {  // a segment
        std::string name;
        uint32_t games = 0;             // none for a segment of versions a compaction emptied: its names only
        uint32_t names = 0;             // the names it adds, their ids counting on from those of the segments before
        bool versions  = false;         // its games are versions of games before (Database::replace())
        std::string source;             // the file its games were imported from; empty when not known
        bool unknown_names    = false;  // its names written as "?" in place of names lost (Database::keepReadable())
        nlohmann::json others = nlohmann::json::object();  // its keys not known here
    };
    uint32_t version    = VERSION;  // of the format it was made in
    uint64_t generation = 0;        // of its commit
    std::vector<Entry> segments;
    std::string edits;  // the edits file (editsName()), empty when none
    // The generation of the last commit that replaced or deleted games (Snapshot::editsGeneration()): none until one.
    std::optional<uint64_t> edits_generation;
    std::string players;                               // the table of names' file, empty when none
    nlohmann::json others = nlohmann::json::object();  // its keys not known here

    // The manifest of the database in `dir`, `text` its file's. Throws StoreError: Damaged when it does not read (no
    // JSON, or numbers no commit writes), NotADatabase for another program's file, NewerFormat for a newer one's.
    [[nodiscard]] static Manifest read(const std::filesystem::path& dir);
    [[nodiscard]] static Manifest parse(std::string_view text, const std::filesystem::path& dir);
    // As it is written: the keys in their order, a space of indent.
    [[nodiscard]] std::string text() const;
    // In place of the one in `dir`, at once (tour::replaceFile()): the commit.
    void write(const std::filesystem::path& dir) const;
};

class Segment {
   public:
    // `names_known`: false for names written as "?" in place of names lost (Database::keepReadable()).
    Segment(const std::filesystem::path& dir, uint32_t expected_games, uint32_t expected_names, bool names_known = true)
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
    Segment(Unreadable, const std::filesystem::path& dir, uint32_t games, uint32_t names, uint32_t blank_name,
            bool names_known)
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
    static bool readNames(const std::filesystem::path& file, uint32_t count, MappedFile& names,
                          std::vector<uint32_t>& offsets) {
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

// Everything kept of a segment's record i (Snapshot::game()). A record that does not read whole (its offsets out of its
// files, its annex record cut short or made up) throws std::runtime_error when `strict` (the check); else it gives its
// headers and what could be read before the damage, marked damaged (#102).
ImportedGame recordGame(const Snapshot& snap, const Segment& segment, std::size_t i, bool strict);

// A game's bit in a vector of a bit a game (the games deleted), set or cleared; trim() takes off the words left empty.
inline void setBit(std::vector<uint64_t>& bits, GameId id, bool on) {
    if (bits.size() <= id / 64) {
        if (!on) return;
        bits.resize(id / 64 + 1, 0);
    }
    if (on)
        bits[id / 64] |= uint64_t{1} << (id % 64);
    else
        bits[id / 64] &= ~(uint64_t{1} << (id % 64));
}

inline void trim(std::vector<uint64_t>& bits) {
    while (!bits.empty() && bits.back() == 0) bits.pop_back();
}

}  // namespace fcg::study::store
