#include "tree.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <chess.hpp>
#include <json.hpp>
#include <tour/files.hpp>

#include "scan_board.hpp"

#ifndef _WIN32
#    include <fcntl.h>
#    include <unistd.h>
#endif

namespace fcg::study {

namespace fs = std::filesystem;

namespace {

constexpr uint32_t VERSION      = 1;
constexpr uint64_t POPULAR      = 256;  // a position with this many games keeps its moves' numbers
constexpr uint64_t INDEX_EVERY  = 32;   // index.bin: where every 32nd record starts
constexpr std::size_t HEADER    = 32;
constexpr char MAGIC[8]         = {'R', 'S', 'T', 'R', 'E', 'E', '1', '\0'};
constexpr const char* TREE_JSON = "tree.json";
constexpr uint32_t MAX_PLIES    = 250;

enum FileKind : uint32_t { ORDER = 1, KEYS = 2, INDEX = 3, RECORDS = 4 };

constexpr uint8_t RECORD_POPULAR = 1;
constexpr uint8_t RECORD_WIDE    = 2;

template <typename T>
T load(const uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

void putVarint(std::string& out, uint64_t v) {
    while (v >= 0x80) {
        out.push_back(static_cast<char>((v & 0x7F) | 0x80));
        v >>= 7;
    }
    out.push_back(static_cast<char>(v));
}

uint64_t getVarint(const uint8_t*& p, const uint8_t* end) {
    uint64_t v = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (p >= end) throw std::runtime_error("a damaged tree");
        const uint8_t b = *p++;
        v |= static_cast<uint64_t>(b & 0x7F) << shift;
        if (!(b & 0x80)) return v;
    }
    throw std::runtime_error("a damaged tree");
}

std::string header(FileKind kind, uint64_t count) {
    std::string h(HEADER, '\0');
    std::memcpy(h.data(), MAGIC, sizeof MAGIC);
    const uint32_t k = kind, version = VERSION;
    std::memcpy(h.data() + 8, &k, 4);
    std::memcpy(h.data() + 12, &version, 4);
    std::memcpy(h.data() + 16, &count, 8);
    return h;
}

// The count a file's header gives, after checking it is the file it should be.
uint64_t checkHeader(const MappedFile& file, FileKind kind, const fs::path& path) {
    if (file.size() < HEADER || std::memcmp(file.data(), MAGIC, sizeof MAGIC) != 0 ||
        load<uint32_t>(file.data() + 8) != kind)
        throw std::runtime_error(path.string() + ": not a tree file");
    if (load<uint32_t>(file.data() + 12) != VERSION) throw std::runtime_error(path.string() + ": another version");
    return load<uint64_t>(file.data() + 16);
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

// A file written from the start, its header written again at the end with what it holds.
class Writer {
   public:
    Writer(const fs::path& file, FileKind kind)
        : path_(file), kind_(kind), file_(tour::openFile(file, "wb"), &std::fclose) {
        if (!file_) throw std::runtime_error("cannot write " + file.string());
        put(header(kind, 0));
    }
    void put(std::string_view bytes) {
        buffer_.append(bytes);
        written_ += bytes.size();
        if (buffer_.size() >= (std::size_t{1} << 20)) flush();
    }
    template <typename T>
    void putValue(T v) {
        put(std::string_view(reinterpret_cast<const char*>(&v), sizeof v));
    }
    [[nodiscard]] uint64_t written() const noexcept { return written_ - HEADER; }  // after the header
    void finish(uint64_t count) {
        flush();
        const auto h = header(kind_, count);
        if (std::fseek(file_.get(), 0, SEEK_SET) != 0 || std::fwrite(h.data(), 1, h.size(), file_.get()) != h.size() ||
            !tour::syncFile(file_.get()))
            throw std::runtime_error("cannot write " + path_.string() + " (is the disk full?)");
        file_.reset();
    }

   private:
    void flush() {
        if (!buffer_.empty() && std::fwrite(buffer_.data(), 1, buffer_.size(), file_.get()) != buffer_.size())
            throw std::runtime_error("cannot write " + path_.string() + " (is the disk full?)");
        buffer_.clear();
    }
    fs::path path_;
    FileKind kind_;
    std::unique_ptr<std::FILE, int (*)(std::FILE*)> file_;
    std::string buffer_;
    uint64_t written_ = 0;
};

int threadCount(int threads) {
    return threads > 0 ? threads : static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
}

// Runs f(from, to) over parts of [0, n), each on a thread of its own.
template <typename F>
void forParts(std::size_t n, std::size_t parts, F&& f) {
    parts = std::max<std::size_t>(1, std::min(parts, n));
    if (parts == 1) {
        f(std::size_t{0}, n, std::size_t{0});
        return;
    }
    std::vector<std::thread> pool;
    for (std::size_t p = 0; p < parts; ++p) pool.emplace_back([&, p] { f(n * p / parts, n * (p + 1) / parts, p); });
    for (auto& t : pool) t.join();
}

// Sorts on several threads: parts sorted apart, then merged two by two.
template <typename T, typename Less>
void parallelSort(std::vector<T>& v, int threads, Less less) {
    const std::size_t n = v.size();
    const std::size_t parts =
        std::bit_floor(std::min<std::size_t>(static_cast<std::size_t>(threads), std::max<std::size_t>(1, n / 65536)));
    if (parts <= 1) {
        std::sort(v.begin(), v.end(), less);
        return;
    }
    std::vector<std::size_t> at(parts + 1);
    for (std::size_t p = 0; p <= parts; ++p) at[p] = n * p / parts;
    forParts(parts, parts, [&](std::size_t from, std::size_t, std::size_t) {
        std::sort(v.begin() + static_cast<std::ptrdiff_t>(at[from]),
                  v.begin() + static_cast<std::ptrdiff_t>(at[from + 1]), less);
    });
    std::vector<T> other(n);
    T* src = v.data();
    T* dst = other.data();
    for (std::size_t width = 1; width < parts; width *= 2) {
        std::vector<std::thread> pool;
        for (std::size_t p = 0; p < parts; p += 2 * width) {
            const auto a = at[p], b = at[std::min(parts, p + width)], c = at[std::min(parts, p + 2 * width)];
            pool.emplace_back([=] { std::merge(src + a, src + b, src + b, src + c, dst + a, less); });
        }
        for (auto& t : pool) t.join();
        std::swap(src, dst);
    }
    if (src != v.data()) v.swap(other);
}

// Sorts entries by key, then by start, in place on several threads: put into 256 buckets by 8 bits of their keys
// (uniform: Polyglot keys) from bit `shift`, then the buckets sorted at once.
template <typename T>
void sortByKey(T* v, std::size_t n, int shift, int threads) {
    const auto less = [](const T& a, const T& b) { return a.key != b.key ? a.key < b.key : a.start < b.start; };
    if (n < 65536 || threads <= 1) {
        std::sort(v, v + n, less);
        return;
    }
    const auto bucket = [shift](const T& e) { return static_cast<int>((e.key >> shift) & 0xFF); };
    std::array<std::size_t, 257> start{};
    for (std::size_t i = 0; i < n; ++i) ++start[bucket(v[i]) + 1];
    for (int b = 0; b < 256; ++b) start[b + 1] += start[b];
    std::array<std::size_t, 256> next{};
    for (int b = 0; b < 256; ++b) next[b] = start[b];
    for (int b = 0; b < 256; ++b) {
        while (next[b] < start[b + 1]) {
            const int t = bucket(v[next[b]]);
            if (t == b)
                ++next[b];
            else
                std::swap(v[next[b]], v[next[t]++]);
        }
    }
    std::atomic<int> taken{0};
    forParts(static_cast<std::size_t>(threads), static_cast<std::size_t>(threads),
             [&](std::size_t, std::size_t, std::size_t) {
                 for (int b; (b = taken++) < 256;) std::sort(v + start[b], v + start[b + 1], less);
             });
}

void addGame(TreeStats& s, char result, uint16_t white_elo, uint16_t black_elo, uint32_t date) noexcept {
    ++s.games;
    if (result == '1')
        ++s.white;
    else if (result == '=')
        ++s.draws;
    else if (result == '0')
        ++s.black;
    if (white_elo) {
        ++s.white_rated;
        s.white_elo += white_elo;
    }
    if (black_elo) {
        ++s.black_rated;
        s.black_elo += black_elo;
    }
    s.last = std::max(s.last, date);
}

// The answer of moves counted by code: the most played first, the games that end there last.
TreeAnswer answerOf(const std::map<int, TreeStats>& by) {
    TreeAnswer a;
    for (const auto& [code, stats] : by) {
        a.total.add(stats);
        a.moves.push_back({code, stats});
    }
    std::sort(a.moves.begin(), a.moves.end(), [](const TreeMove& x, const TreeMove& y) {
        if ((x.code == TreeMove::END) != (y.code == TreeMove::END)) return y.code == TreeMove::END;
        if (x.stats.games != y.stats.games) return x.stats.games > y.stats.games;
        return x.code < y.code;
    });
    return a;
}

void putStats(std::string& out, const TreeStats& s) {
    for (const uint64_t v : {s.games, s.white, s.draws, s.black, s.white_rated, s.white_elo, s.black_rated, s.black_elo,
                             static_cast<uint64_t>(s.last)})
        putVarint(out, v);
}

TreeStats getStats(const uint8_t*& p, const uint8_t* end) {
    TreeStats s;
    s.games       = getVarint(p, end);
    s.white       = getVarint(p, end);
    s.draws       = getVarint(p, end);
    s.black       = getVarint(p, end);
    s.white_rated = getVarint(p, end);
    s.white_elo   = getVarint(p, end);
    s.black_rated = getVarint(p, end);
    s.black_elo   = getVarint(p, end);
    s.last        = static_cast<uint32_t>(getVarint(p, end));
    return s;
}

// The code a game plays from the position it reaches at `depth`, END when it ends there.
int codeAt(std::span<const uint8_t> codes, uint32_t plies, uint32_t depth, bool wide) {
    if (depth >= plies || depth >= codes.size()) return TreeMove::END;
    if (!wide) return codes[depth];
    return depth + 1 < codes.size() ? (codes[depth] << 8) | codes[depth + 1] : TreeMove::END;
}

// ---------------------------------------------------------------------------------------------------------------------
// The build.

struct BuildGame {
    const uint8_t* codes = nullptr;
    uint64_t start       = 0;  // its start position's key
    uint32_t id          = 0;
    uint8_t bytes        = 0;  // of its codes, those it is sorted by: at most plies + 1
    uint8_t plies        = 0;  // its plies, at most plies + 1
    uint8_t chess960     = 0;
    uint8_t fen          = 0;  // it starts from a position of its own
};

bool sortedBefore(const BuildGame& a, const BuildGame& b) {
    if (a.start != b.start) return a.start < b.start;
    if (a.chess960 != b.chess960) return a.chess960 < b.chess960;
    const int c = std::memcmp(a.codes, b.codes, std::min(a.bytes, b.bytes));
    if (c != 0) return c < 0;
    if (a.bytes != b.bytes) return a.bytes < b.bytes;
    return a.id < b.id;
}

struct Number {
    uint32_t date      = 0;
    uint16_t white_elo = 0;
    uint16_t black_elo = 0;
    char result        = '*';
};

// A position several games reach by the same moves: the first of them in the order, and how many.
struct Node {
    uint64_t key   = 0;
    uint32_t start = 0;
    uint32_t count = 0;
    uint8_t depth  = 0;
    uint8_t wide   = 0;
};

// A position a game reaches by moves of its own (left uninitialized: a pass makes many of them at once).
struct Tail {
    uint64_t key;
    uint32_t start;
    uint8_t depth;
    uint8_t wide;
};

constexpr std::size_t TAIL_BLOCK = 1024;  // the tails a thread gathers before it moves them to the pass's array

// A pass's tails: an array sized for them, filled a block at a time by the threads; what would not fit, aside.
struct TailArray {
    std::unique_ptr<Tail[]> data;
    std::size_t capacity = 0;
    std::atomic<std::size_t> used{0};
    std::mutex mutex;
    std::vector<Tail> overflow;

    void add(const std::vector<Tail>& block) {
        const std::size_t k   = block.size();
        const std::size_t at  = used.fetch_add(k);
        const std::size_t fit = at >= capacity ? 0 : std::min(k, capacity - at);
        if (fit > 0) std::memcpy(data.get() + at, block.data(), fit * sizeof(Tail));
        if (fit < k) {
            std::lock_guard lock(mutex);
            overflow.insert(overflow.end(), block.begin() + static_cast<std::ptrdiff_t>(fit), block.end());
        }
    }
    // The tails, all in the array.
    std::size_t gathered() {
        std::size_t count = std::min(used.load(), capacity);
        if (!overflow.empty()) {
            std::unique_ptr<Tail[]> more(new Tail[count + overflow.size()]);
            std::memcpy(more.get(), data.get(), count * sizeof(Tail));
            std::memcpy(more.get() + count, overflow.data(), overflow.size() * sizeof(Tail));
            count += overflow.size();
            data     = std::move(more);
            capacity = count;
            std::vector<Tail>().swap(overflow);
        }
        return count;
    }
    void clear() {
        used = 0;
        overflow.clear();
    }
};

class Builder {
   public:
    Builder(const Snapshot& snap, const TreeOptions& options, TreeProgress& progress, const std::atomic<bool>& cancel)
        : snap_(snap),
          plies_(std::min(options.plies, MAX_PLIES)),
          threads_(threadCount(options.threads)),
          memory_(options.memory),
          progress_(progress),
          cancel_(cancel) {
        standard_.setFen(chess::constants::STARTPOS, false);
    }

    // Writes the tree's files into `dir`: false when cancelled.
    bool build(const fs::path& dir, TreeBuilt& out) {
        gather();
        if (cancel_) return false;
        parallelSort(games_, threads_, sortedBefore);
        if (cancel_) return false;
        {
            Writer order(dir / "order.bin", ORDER);
            for (const auto& g : games_) order.putValue<uint32_t>(g.id);
            order.finish(games_.size());
        }
        numbers();
        shared();
        if (cancel_) return false;

        // The positions a single game's moves reach, in passes over parts of the keys that fit in memory.
        plan();
        const std::size_t fixed =
            games_.size() * (sizeof(BuildGame) + sizeof(Number) + 2 * sizeof(int16_t)) + nodes_.size() * sizeof(Node);
        const std::size_t room = memory_ > fixed ? memory_ - fixed : std::max<std::size_t>(memory_ / 4, 1);
        const uint64_t wanted  = (tails_ * sizeof(Tail) + room - 1) / room;
        passes_         = static_cast<uint32_t>(std::bit_ceil(std::max<uint64_t>(1, std::min<uint64_t>(wanted, 1024))));
        pass_bits_      = std::countr_zero(passes_);
        out.passes      = passes_;
        progress_.total = games_.size() * (1 + uint64_t{passes_});
        progress_.done  = games_.size();

        TailArray tails;
        tails.capacity = (tails_ + passes_ - 1) / passes_ / 16 * 17 + TAIL_BLOCK * chunks_.size();
        tails.data.reset(new Tail[tails.capacity]);
        Writer keys(dir / "keys.bin", KEYS), index(dir / "index.bin", INDEX), records(dir / "records.bin", RECORDS);
        uint64_t positions = 0;
        for (uint32_t pass = 0; pass < passes_; ++pass) {
            tails.clear();
            std::vector<std::size_t> made(chunks_.size() - 1, 0);
            replay(pass, pass == 0, made, tails);
            if (cancel_) return false;
            if (pass == 0) closeNodes(made);
            const std::size_t count = tails.gathered();
            sortByKey(tails.data.get(), count, std::max(0, 56 - pass_bits_), threads_);
            if (cancel_) return false;
            write(pass, tails.data.get(), count, keys, index, records, positions);
            if (cancel_) return false;
        }
        keys.finish(positions);
        index.finish((positions + INDEX_EVERY - 1) / INDEX_EVERY);
        records.finish(records.written());
        out.games     = games_.size();
        out.positions = positions;
        return true;
    }

   private:
    void gather() {
        const uint64_t n = snap_.games();
        std::vector<BuildGame> all(n);
        std::vector<uint8_t> kept(n, 0);
        const uint64_t standard = standard_.key();
        forParts(n, static_cast<std::size_t>(threads_), [&](std::size_t from, std::size_t to, std::size_t) {
            for (std::size_t id = from; id < to; ++id) {
                const auto m = snap_.gameMoves(id);
                auto& g      = all[id];
                g.codes      = m.codes.data();
                g.id         = static_cast<uint32_t>(id);
                g.bytes      = static_cast<uint8_t>(std::min<std::size_t>(m.codes.size(), plies_ + 1));
                g.plies      = static_cast<uint8_t>(std::min<uint32_t>(m.plies, plies_ + 1));
                g.chess960   = (m.flags & GAME_CHESS960) ? 1 : 0;
                g.fen        = (m.flags & GAME_FEN) ? 1 : 0;
                g.start      = standard;
                if (g.fen || g.chess960) {
                    ScanBoard b;
                    if (!startBoard(g, b)) continue;  // a start position it cannot set: left out
                    g.start = b.key();
                }
                kept[id] = 1;
            }
        });
        games_.reserve(n);
        for (uint64_t id = 0; id < n; ++id)
            if (kept[id]) games_.push_back(all[id]);
    }

    // The board a game starts from.
    bool startBoard(const BuildGame& g, ScanBoard& board) const {
        if (g.fen) return board.setFen(snap_.game(g.id).start_fen, g.chess960 != 0);
        if (g.chess960) return board.setFen(chess::constants::STARTPOS, true);
        board = standard_;
        return true;
    }

    // The games' numbers, in the order of their moves.
    void numbers() {
        numbers_.resize(games_.size());
        forParts(games_.size(), static_cast<std::size_t>(threads_), [&](std::size_t from, std::size_t to, std::size_t) {
            for (std::size_t j = from; j < to; ++j) {
                const auto n = snap_.numbers(games_[j].id);
                numbers_[j]  = {n.date, n.white_elo, n.black_elo, n.result};
            }
        });
    }

    // How deep each game shares its moves with the game before it (-1: another start), and with either neighbour.
    void shared() {
        const std::size_t n = games_.size();
        lcp_.assign(n + 1, -1);
        forParts(n, static_cast<std::size_t>(threads_), [&](std::size_t from, std::size_t to, std::size_t) {
            for (std::size_t i = std::max<std::size_t>(from, 1); i < to; ++i) {
                const auto& a = games_[i - 1];
                const auto& b = games_[i];
                if (a.start != b.start || a.chess960 != b.chess960) continue;
                int k         = 0;
                const int lim = std::min(a.bytes, b.bytes);
                while (k < lim && a.codes[k] == b.codes[k]) ++k;
                lcp_[i] = static_cast<int16_t>(std::min<int>(k, static_cast<int>(plies_)));
            }
        });
        deep_.resize(n);
        tails_ = 0;
        for (std::size_t i = 0; i < n; ++i) {
            deep_[i]        = std::max(lcp_[i], lcp_[i + 1]);
            const int depth = std::min<int>(games_[i].plies, static_cast<int>(plies_));
            if (depth > deep_[i]) tails_ += static_cast<uint64_t>(depth - deep_[i]);
        }
    }

    [[nodiscard]] uint32_t partOf(uint64_t key) const noexcept {
        return pass_bits_ == 0 ? 0 : static_cast<uint32_t>(key >> (64 - pass_bits_));
    }

    // The games' parts, one a thread, the same in every pass, and where each part's nodes go: at most one at each
    // depth a game shares beyond the game before it.
    void plan() {
        const std::size_t n = games_.size();
        const std::size_t parts =
            std::max<std::size_t>(1, std::min<std::size_t>(static_cast<std::size_t>(threads_), n));
        chunks_.resize(parts + 1);
        for (std::size_t p = 0; p <= parts; ++p) chunks_[p] = n * p / parts;
        node_at_.assign(parts + 1, 0);
        for (std::size_t p = 0; p < parts; ++p) {
            std::size_t bound = 0;
            for (std::size_t i = chunks_[p]; i < chunks_[p + 1]; ++i)
                if (deep_[i] > lcp_[i]) bound += static_cast<std::size_t>(deep_[i] - lcp_[i]);
            node_at_[p + 1] = node_at_[p] + bound;
        }
        nodes_.resize(node_at_.back());
    }

    // Replays the games for a pass: the positions several games share by the same moves (the first pass), and those a
    // game reaches by moves of its own whose keys are in the pass's part.
    void replay(uint32_t pass, bool with_nodes, std::vector<std::size_t>& made, TailArray& tails) {
        const std::size_t parts = chunks_.size() - 1;
        forParts(parts, parts, [&](std::size_t p, std::size_t, std::size_t) {
            const std::size_t from = chunks_[p], to = chunks_[p + 1];
            Node* my_nodes      = nodes_.data() + node_at_[p];
            std::size_t my_made = 0;
            std::vector<Tail> block;
            block.reserve(TAIL_BLOCK);
            std::vector<ScanBoard> stack(plies_ + 1);
            ScanBoard own;
            std::vector<uint64_t> keys(plies_ + 1);
            int stops_at = INT_MAX;  // where the game before stopped being followed on the stack
            for (std::size_t i = from; i < to; ++i) {
                if ((i - from) % 4096 == 4095) {
                    progress_.done += 4096;
                    if (cancel_) return;
                }
                const auto& g    = games_[i];
                const int shared = lcp_[i];
                if (i != from && shared >= stops_at) continue;  // it stops where the game before did
                stops_at       = INT_MAX;
                const int last = std::min<int>(g.plies, static_cast<int>(plies_));
                const int deep = deep_[i];
                // A position: counted where the game reaches it first; true when the game is followed no further.
                const auto visit = [&](int d, const ScanBoard& b) {
                    const uint64_t key = b.key();
                    keys[d]            = key;
                    const bool wide    = b.codeCount() > 256;
                    for (int k = 2, lim = std::min(b.halfmoveClock(), d); k <= lim; k += 2)
                        if (keys[d - k] == key) return wide && d < g.plies;
                    const auto depth = static_cast<uint8_t>(d);
                    if (d <= deep) {
                        if (with_nodes && d > shared)
                            my_nodes[my_made++] = {key, static_cast<uint32_t>(i), 0, depth, static_cast<uint8_t>(wide)};
                    } else if (partOf(key) == pass) {
                        block.push_back({key, static_cast<uint32_t>(i), depth, static_cast<uint8_t>(wide)});
                        if (block.size() == TAIL_BLOCK) {
                            tails.add(block);
                            block.clear();
                        }
                    }
                    return wide && d < g.plies;  // its next move takes two bytes
                };
                int d = (i == from || shared < 0) ? 0 : shared + 1;
                if (d == 0 && !startBoard(g, stack[0])) continue;
                // The moves it shares with a neighbour, on the stack the next game starts from.
                bool stopped = false;
                for (; d <= std::min(last, deep); ++d) {
                    if (d > 0) {
                        const ScanBoard& before = stack[d - 1];
                        const int code          = g.codes[d - 1];
                        if (code >= before.codeCount()) {  // a damaged code, which a database does not hold
                            stops_at = d - 1;
                            stopped  = true;
                            break;
                        }
                        stack[d] = before;
                        stack[d].play(before.decode(code));
                    }
                    if (visit(d, stack[d])) {
                        stops_at = d;
                        stopped  = true;
                        break;
                    }
                }
                if (stopped || d > last) continue;
                // Its own moves, played on a board of its own.
                own = stack[d == 0 ? 0 : d - 1];
                for (; d <= last; ++d) {
                    if (d > 0) {
                        const int code = g.codes[d - 1];
                        if (code >= own.codeCount()) break;
                        own.play(own.decode(code));
                    }
                    if (visit(d, own)) break;
                }
            }
            if (!block.empty()) tails.add(block);
            made[p] = my_made;
            progress_.done += (to - from) % 4096;
        });
    }

    // Each node's games: those after its first that share as many moves.
    void closeNodes(const std::vector<std::size_t>& made) {
        std::size_t total = 0;
        for (std::size_t p = 0; p < made.size(); ++p) {
            if (made[p] > 0) std::memmove(nodes_.data() + total, nodes_.data() + node_at_[p], made[p] * sizeof(Node));
            total += made[p];
        }
        nodes_.resize(total);
        nodes_.shrink_to_fit();
        std::vector<std::size_t> open;
        std::size_t k       = 0;
        const std::size_t n = games_.size();
        for (std::size_t i = 0; i <= n; ++i) {
            const int keep = i < n ? lcp_[i] : -1;
            while (!open.empty() && nodes_[open.back()].depth > keep) {
                auto& node = nodes_[open.back()];
                node.count = static_cast<uint32_t>(i - node.start);
                open.pop_back();
            }
            while (k < nodes_.size() && nodes_[k].start == i) open.push_back(k++);
        }
        sortByKey(nodes_.data(), nodes_.size(), 56, threads_);
    }

    struct BuildRun {
        uint32_t start = 0;
        uint32_t count = 0;
        uint8_t depth  = 0;
    };

    // The code game j plays from the position it reaches at `depth`: there, as it is followed that far.
    [[nodiscard]] int codeOf(std::size_t j, uint32_t depth, bool wide) const {
        const auto& g = games_[j];
        if (depth >= g.plies) return TreeMove::END;
        return wide ? (g.codes[depth] << 8) | g.codes[depth + 1] : g.codes[depth];
    }

    // The moves of a popular position: within a run of games that share their moves, the next moves are in order,
    // each a run of its own found by halving.
    [[nodiscard]] TreeAnswer numbersOf(const std::vector<BuildRun>& runs, bool wide) const {
        std::map<int, TreeStats> by;
        for (const auto& r : runs) {
            std::size_t j         = r.start;
            const std::size_t end = std::size_t{r.start} + r.count;
            while (j < end) {
                const int code = codeOf(j, r.depth, wide);
                std::size_t hi = j + 1;
                if (!wide && end - j > 8) {
                    std::size_t lo = j, step = 1;
                    while (hi < end && codeOf(hi, r.depth, wide) == code) {
                        lo = hi;
                        step <<= 1;
                        hi = std::min(end, lo + step);
                    }
                    while (hi - lo > 1) {
                        const std::size_t mid = lo + (hi - lo) / 2;
                        if (codeOf(mid, r.depth, wide) == code)
                            lo = mid;
                        else
                            hi = mid;
                    }
                }
                auto& s = by[code];
                for (std::size_t k = j; k < hi; ++k) {
                    const auto& nb = numbers_[k];
                    addGame(s, nb.result, nb.white_elo, nb.black_elo, nb.date);
                }
                j = hi;
            }
        }
        return answerOf(by);
    }

    // The records of the keys in a pass's part: the nodes' and the tails' of each, merged.
    void write(uint32_t pass, const Tail* tails, std::size_t tail_count, Writer& keys, Writer& index, Writer& records,
               uint64_t& positions) {
        const auto part_begin = std::lower_bound(nodes_.begin(), nodes_.end(), pass,
                                                 [&](const Node& n, uint32_t p) { return partOf(n.key) < p; });
        const auto part_end   = std::lower_bound(part_begin, nodes_.end(), pass + 1,
                                                 [&](const Node& n, uint32_t p) { return partOf(n.key) < p; });
        auto ni               = part_begin;
        std::size_t ti        = 0;
        std::vector<BuildRun> runs;
        std::string body, length;
        while (ni != part_end || ti < tail_count) {
            if (positions % 65536 == 0 && cancel_) return;
            uint64_t key = ni != part_end ? ni->key : tails[ti].key;
            if (ti < tail_count) key = std::min(key, tails[ti].key);
            runs.clear();
            bool wide      = false;
            uint64_t games = 0;
            for (; ni != part_end && ni->key == key; ++ni) {
                runs.push_back({ni->start, ni->count, ni->depth});
                wide = wide || ni->wide;
                games += ni->count;
            }
            for (; ti < tail_count && tails[ti].key == key; ++ti) {
                runs.push_back({tails[ti].start, 1, tails[ti].depth});
                wide = wide || tails[ti].wide;
                ++games;
            }
            if (games < 2) continue;
            std::sort(runs.begin(), runs.end(), [](const BuildRun& a, const BuildRun& b) { return a.start < b.start; });
            body.clear();
            const bool popular = games >= POPULAR;
            body.push_back(static_cast<char>((popular ? RECORD_POPULAR : 0) | (wide ? RECORD_WIDE : 0)));
            putVarint(body, games);
            putVarint(body, runs.size());
            uint32_t before = 0;
            for (const auto& r : runs) {
                putVarint(body, r.start - before);
                putVarint(body, r.count - 1);
                body.push_back(static_cast<char>(r.depth));
                before = r.start;
            }
            if (popular) {
                const auto answer = numbersOf(runs, wide);
                putVarint(body, answer.moves.size());
                for (const auto& m : answer.moves) {
                    putVarint(body, static_cast<uint64_t>(m.code + 1));
                    putStats(body, m.stats);
                }
            }
            if (positions % INDEX_EVERY == 0) index.putValue<uint64_t>(records.written());
            length.clear();
            putVarint(length, body.size());
            records.put(length);
            records.put(body);
            keys.putValue<uint64_t>(key);
            ++positions;
        }
    }

    const Snapshot& snap_;
    const uint32_t plies_;
    const int threads_;
    const std::size_t memory_;
    TreeProgress& progress_;
    const std::atomic<bool>& cancel_;
    ScanBoard standard_;
    std::vector<BuildGame> games_;
    std::vector<Number> numbers_;
    std::vector<int16_t> lcp_, deep_;
    std::vector<Node> nodes_;
    std::vector<std::size_t> chunks_, node_at_;
    uint64_t tails_  = 0;
    uint32_t passes_ = 1;
    int pass_bits_   = 0;
};

std::string treeName(uint64_t number) {
    char name[32];
    std::snprintf(name, sizeof name, "tree-%06llu", static_cast<unsigned long long>(number));
    return name;
}

// The folder tree.json names, empty when none.
std::string namedTree(const fs::path& dir) {
    try {
        std::ifstream in(dir / TREE_JSON, std::ios::binary);
        if (!in) return {};
        const auto j = nlohmann::json::parse(in);
        return j.value("format", "") == "rookstudio-tree" ? j.value("folder", "") : std::string();
    } catch (const std::exception&) {
        return {};
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------

void TreeStats::add(const HeaderNumbers& game) noexcept {
    addGame(*this, game.result, game.white_elo, game.black_elo, game.date);
}

void TreeStats::add(const TreeStats& o) noexcept {
    games += o.games;
    white += o.white;
    draws += o.draws;
    black += o.black;
    white_rated += o.white_rated;
    white_elo += o.white_elo;
    black_rated += o.black_rated;
    black_elo += o.black_elo;
    last = std::max(last, o.last);
}

std::shared_ptr<const Tree> Tree::open(const fs::path& folder) {
    nlohmann::json j;
    {
        std::ifstream in(folder / TREE_JSON, std::ios::binary);
        if (!in) return nullptr;
        try {
            j = nlohmann::json::parse(in);
        } catch (const std::exception& e) {
            throw std::runtime_error((folder / TREE_JSON).string() + ": " + e.what());
        }
    }
    if (j.value("format", "") != "rookstudio-tree" || j.value("version", 0u) > VERSION) return nullptr;
    auto tree        = std::make_shared<Tree>();
    tree->name_      = j.value("folder", "");
    tree->games_     = j.value("games", uint64_t{0});
    tree->plies_     = j.value("plies", 0u);
    const auto dir   = folder / tree->name_;
    tree->order_     = MappedFile(dir / "order.bin");
    tree->keys_      = MappedFile(dir / "keys.bin");
    tree->index_     = MappedFile(dir / "index.bin");
    tree->records_   = MappedFile(dir / "records.bin");
    const auto games = tree->ordered_ = checkHeader(tree->order_, ORDER, dir / "order.bin");
    tree->positions_                  = checkHeader(tree->keys_, KEYS, dir / "keys.bin");
    const auto marks                  = checkHeader(tree->index_, INDEX, dir / "index.bin");
    const auto bytes                  = checkHeader(tree->records_, RECORDS, dir / "records.bin");
    if (tree->order_.size() != HEADER + games * 4 || tree->keys_.size() != HEADER + tree->positions_ * 8 ||
        marks != (tree->positions_ + INDEX_EVERY - 1) / INDEX_EVERY || tree->index_.size() != HEADER + marks * 8 ||
        tree->records_.size() != HEADER + bytes || games > tree->games_)
        throw std::runtime_error(dir.string() + ": a damaged tree");
    return tree;
}

uint64_t Tree::keyAt(uint64_t i) const noexcept { return load<uint64_t>(keys_.data() + HEADER + i * 8); }

std::optional<Tree::Record> Tree::record(uint64_t key) const {
    uint64_t lo = 0, hi = positions_;
    while (lo < hi) {
        const uint64_t mid = lo + (hi - lo) / 2;
        if (keyAt(mid) < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == positions_ || keyAt(lo) != key) return std::nullopt;
    const uint8_t* base = records_.data() + HEADER;
    const uint8_t* end  = records_.data() + records_.size();
    const uint8_t* p    = base + load<uint64_t>(index_.data() + HEADER + (lo / INDEX_EVERY) * 8);
    for (uint64_t skip = lo % INDEX_EVERY; skip > 0; --skip) {
        const uint64_t length = getVarint(p, end);
        p += length;
    }
    const uint64_t length = getVarint(p, end);
    if (length > static_cast<uint64_t>(end - p)) throw std::runtime_error("a damaged tree");
    end = p + length;
    Record r;
    const uint8_t flags = *p++;
    r.wide              = (flags & RECORD_WIDE) != 0;
    r.games             = getVarint(p, end);
    const uint64_t runs = getVarint(p, end);
    uint64_t start      = 0;
    for (uint64_t i = 0; i < runs; ++i) {
        Run run;
        start += getVarint(p, end);
        run.start = start;
        run.count = getVarint(p, end) + 1;
        if (p >= end) throw std::runtime_error("a damaged tree");
        run.depth = *p++;
        if (run.start >= ordered_ || run.count > ordered_ - run.start || run.depth > plies_)
            throw std::runtime_error("a damaged tree");
        r.runs.push_back(run);
    }
    if (flags & RECORD_POPULAR) {
        TreeAnswer a;
        const uint64_t moves = getVarint(p, end);
        for (uint64_t i = 0; i < moves; ++i) {
            TreeMove m;
            m.code  = static_cast<int>(getVarint(p, end)) - 1;
            m.stats = getStats(p, end);
            a.total.add(m.stats);
            a.moves.push_back(m);
        }
        r.numbers = std::move(a);
    }
    return r;
}

std::optional<TreeAnswer> Tree::at(const Snapshot& snapshot, uint64_t key) const {
    auto r = record(key);
    if (!r || snapshot.games() < games_) return std::nullopt;
    if (r->numbers) return std::move(r->numbers);
    std::map<int, TreeStats> by;
    for (const auto& run : r->runs) {
        for (uint64_t j = run.start; j < run.start + run.count; ++j) {
            const GameId id = load<uint32_t>(order_.data() + HEADER + j * 4);
            const auto n    = snapshot.numbers(id);
            by[codeAt(snapshot.moves(id), n.plies, run.depth, r->wide)].add(n);
        }
    }
    return answerOf(by);
}

std::vector<TreeGame> Tree::gamesAt(const Snapshot& snapshot, uint64_t key) const {
    std::vector<TreeGame> out;
    const auto r = record(key);
    if (!r || snapshot.games() < games_) return out;
    for (const auto& run : r->runs) {
        for (uint64_t j = run.start; j < run.start + run.count; ++j) {
            const GameId id = load<uint32_t>(order_.data() + HEADER + j * 4);
            const auto m    = snapshot.gameMoves(id);
            out.push_back({id, run.depth, codeAt(m.codes, m.plies, run.depth, r->wide)});
        }
    }
    return out;
}

std::optional<uint32_t> Tree::novelty(const Snapshot& snapshot, const ScanBoard& start,
                                      std::span<const int> line) const {
    if (!record(start.key())) return 0;
    ScanBoard board = start;
    // A single game known to reach the line's position, and the ply it reaches it at.
    std::optional<TreeGame> single;
    for (uint32_t i = 0; i < line.size(); ++i) {
        if (i + 1 > plies_) return std::nullopt;  // beyond what the tree follows
        const int code = line[i];
        if (code < 0 || code >= board.codeCount()) return i + 1;
        ScanBoard next = board;
        next.play(board.decode(code));
        if (record(next.key())) {
            single.reset();
        } else if (single) {
            // A position only that game reaches: is the line still its moves?
            const auto m    = snapshot.gameMoves(single->id);
            const bool wide = board.codeCount() > 256;
            if (single->ply + 1 > plies_ || codeAt(m.codes, m.plies, single->ply, wide) != code || wide) return i + 1;
            ++single->ply;
        } else {
            // The game, if one, that played the move from the position before (two would make the position kept).
            std::optional<TreeGame> found;
            for (const auto& g : gamesAt(snapshot, board.key()))
                if (g.code == code) found = g;
            if (!found || found->ply + 1 > plies_ || board.codeCount() > 256) return i + 1;
            single = TreeGame{found->id, found->ply + 1, TreeMove::END};
        }
        board = next;
    }
    return std::nullopt;
}

TreeBuilt buildTree(const Database& db, const Snapshot& snapshot, const TreeOptions& options, TreeProgress& progress,
                    const std::atomic<bool>& cancel) {
    TreeBuilt out;
    const fs::path dir = db.folder();
    if (snapshot.games() > UINT32_MAX) {
        out.error = "a tree holds at most 4 294 967 295 games";
        return out;
    }
    // A number after every tree's, and what an earlier build left: trees tree.json does not name.
    const std::string named = namedTree(dir);
    uint64_t number         = 1;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const auto name           = entry.path().filename().string();
        unsigned long long parsed = 0;
        if (name.rfind("tree-", 0) != 0 || std::sscanf(name.c_str(), "tree-%llu", &parsed) != 1) continue;
        number = std::max<uint64_t>(number, parsed + 1);
        if (name != named) fs::remove_all(entry.path(), ec);  // still mapped on Windows: removed by a later build
    }
    const std::string name = treeName(number);
    const fs::path tmp     = dir / (name + ".tmp");
    try {
        fs::remove_all(tmp, ec);
        fs::create_directories(tmp);
        Builder builder(snapshot, options, progress, cancel);
        if (!builder.build(tmp, out)) {
            fs::remove_all(tmp, ec);
            out           = TreeBuilt{};
            out.cancelled = true;
            return out;
        }
        syncFolder(tmp);
        fs::rename(tmp, dir / name);
        syncFolder(dir);
        const nlohmann::json j{{"format", "rookstudio-tree"},
                               {"version", VERSION},
                               {"folder", name},
                               {"games", snapshot.games()},
                               {"plies", std::min(options.plies, MAX_PLIES)},
                               {"positions", out.positions},
                               {"generation", snapshot.generation()}};
        tour::replaceFile(dir / TREE_JSON, j.dump(1));
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            const auto other = entry.path().filename().string();
            if (other.rfind("tree-", 0) == 0 && other != name) fs::remove_all(entry.path(), ec);
        }
        progress.done = progress.total.load();
    } catch (const std::exception& e) {
        fs::remove_all(tmp, ec);
        out       = TreeBuilt{};
        out.error = e.what();
    }
    return out;
}

}  // namespace fcg::study
