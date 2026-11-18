#include "query.hpp"

#include <algorithm>
#include <map>
#include <mutex>
#include <numeric>
#include <thread>

namespace fcg::study {

namespace {

// Latin-1's letters U+00C0 to U+00FF without their accents, lower case; 0 for the signs among them.
constexpr char LATIN1[64] = {'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',
                             'd', 'n', 'o', 'o', 'o', 'o', 'o', 0,   'o', 'u', 'u', 'u', 'u', 'y', 't', 's',
                             'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',
                             'd', 'n', 'o', 'o', 'o', 'o', 'o', 0,   'o', 'u', 'u', 'u', 'u', 'y', 't', 'y'};

std::vector<std::string> words(std::string_view text) {
    std::vector<std::string> out;
    const auto folded = foldText(text);
    std::size_t at    = 0;
    while (at < folded.size()) {
        auto end = folded.find_first_of(" ,;", at);
        if (end == std::string::npos) end = folded.size();
        if (end > at) out.push_back(folded.substr(at, end - at));
        at = end + 1;
    }
    return out;
}

int threadsFor(int threads) {
    return threads > 0 ? threads : static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
}

// Runs f(from, to) on ranges of [0, n) on `threads` threads.
template <typename F>
void parallel(std::size_t n, int threads, F&& f) {
    const auto parts = static_cast<std::size_t>(
        std::min<std::size_t>(static_cast<std::size_t>(threads), std::max<std::size_t>(1, n / 65536)));
    if (parts <= 1) {
        f(std::size_t{0}, n, std::size_t{0});
        return;
    }
    std::vector<std::thread> pool;
    for (std::size_t p = 0; p < parts; ++p) pool.emplace_back([&, p] { f(n * p / parts, n * (p + 1) / parts, p); });
    for (auto& t : pool) t.join();
}

// The names of a snapshot folded and ranked in alphabetical order, kept while the names do not change: a later
// snapshot with the same names reuses them.
struct NameIndex {
    uint32_t count = 0;
    std::vector<std::string> folded;
    std::vector<uint32_t> rank;
};

std::shared_ptr<const NameIndex> nameIndex(const Snapshot& snapshot) {
    static std::mutex mutex;
    static std::shared_ptr<const NameIndex> cached;
    std::lock_guard lock(mutex);
    if (cached && cached->count == snapshot.names()) return cached;
    auto index   = std::make_shared<NameIndex>();
    index->count = snapshot.names();
    index->folded.resize(index->count);
    for (uint32_t id = 0; id < index->count; ++id) index->folded[id] = foldText(snapshot.name(id));
    std::vector<uint32_t> order(index->count);
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        if (index->folded[a] != index->folded[b]) return index->folded[a] < index->folded[b];
        return snapshot.name(a) < snapshot.name(b);
    });
    index->rank.resize(index->count);
    for (uint32_t r = 0; r < index->count; ++r) index->rank[order[r]] = r;
    cached = index;
    return index;
}

// The bits of the words a name holds: bit i for word i.
std::vector<uint32_t> wordBits(const NameIndex& names, const std::vector<std::string>& list) {
    std::vector<uint32_t> bits;
    if (list.empty()) return bits;
    bits.resize(names.count);
    for (uint32_t id = 0; id < names.count; ++id) {
        uint32_t b = 0;
        for (std::size_t w = 0; w < list.size() && w < 32; ++w) {
            if (names.folded[id].find(list[w]) != std::string::npos) b |= 1u << w;
        }
        bits[id] = b;
    }
    return bits;
}

// A stable sort of (key, id) by key: four passes of one byte.
void radixSort(std::vector<std::pair<uint32_t, GameId>>& items) {
    std::vector<std::pair<uint32_t, GameId>> other(items.size());
    for (int shift = 0; shift < 32; shift += 8) {
        std::array<std::size_t, 257> count{};
        for (const auto& item : items) ++count[((item.first >> shift) & 0xFF) + 1];
        if (count[1] == items.size() && shift > 0) continue;  // every key alike in this byte
        for (std::size_t b = 1; b < count.size(); ++b) count[b] += count[b - 1];
        for (const auto& item : items) other[count[(item.first >> shift) & 0xFF]++] = item;
        items.swap(other);
    }
}

}  // namespace

std::string foldText(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        } else if (c == 0xC3 && i + 1 < text.size()) {
            const auto next = static_cast<unsigned char>(text[i + 1]);
            if (next >= 0x80 && next <= 0xBF && LATIN1[next - 0x80]) {
                out += LATIN1[next - 0x80];
                ++i;
                continue;
            }
            out += static_cast<char>(c);
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

bool Filter::empty() const noexcept {
    return words.empty() && white.empty() && black.empty() && player.empty() && date_from == 0 && date_to == 0 &&
           elo_min == 0 && eco_from == 0 && eco_to == 0 && result == 0 && !annotated;
}

View makeView(const std::shared_ptr<const Snapshot>& snapshot, const Filter& filter, SortBy sort, bool descending,
              int threads, const std::atomic<bool>* cancel) {
    View view;
    view.snapshot  = snapshot;
    threads        = threadsFor(threads);
    const auto n   = static_cast<std::size_t>(snapshot->games());
    const auto all = words(filter.words), white = words(filter.white), black = words(filter.black),
               player       = words(filter.player);
    const bool names_needed = !all.empty() || !white.empty() || !black.empty() || !player.empty() ||
                              (sort >= SortBy::White && sort <= SortBy::Round);
    const auto names        = names_needed ? nameIndex(*snapshot) : nullptr;
    const auto all_bits     = names ? wordBits(*names, all) : std::vector<uint32_t>{};
    const auto white_bits   = names ? wordBits(*names, white) : std::vector<uint32_t>{};
    const auto black_bits   = names ? wordBits(*names, black) : std::vector<uint32_t>{};
    const auto player_bits  = names ? wordBits(*names, player) : std::vector<uint32_t>{};
    const auto full         = [](const std::vector<std::string>& list) {
        const auto k = std::min<std::size_t>(list.size(), 32);
        return k == 32 ? ~0u : (1u << k) - 1;
    };

    std::vector<std::vector<GameId>> parts(static_cast<std::size_t>(threads));
    const bool hide_deleted = !filter.with_deleted && snapshot->deletedCount() > 0;
    const bool everything   = filter.empty() && !hide_deleted;
    parallel(n, threads, [&](std::size_t from, std::size_t to, std::size_t part) {
        auto& out = parts[part];
        if (everything) {
            out.resize(to - from);
            std::iota(out.begin(), out.end(), static_cast<GameId>(from));
            return;
        }
        for (std::size_t id = from; id < to; ++id) {
            if ((id & 0xFFFF) == 0 && cancel && cancel->load(std::memory_order_relaxed)) return;
            if (hide_deleted && snapshot->deleted(id)) continue;
            const auto row = snapshot->numbers(id);
            if (filter.result && row.result != filter.result) continue;
            if (filter.annotated && !(row.flags & GAME_ANNOTATED)) continue;
            if (filter.elo_min && (row.white_elo < filter.elo_min || row.black_elo < filter.elo_min)) continue;
            if ((filter.date_from || filter.date_to) &&
                (row.date == 0 || (filter.date_from && row.date < filter.date_from) ||
                 (filter.date_to && row.date > filter.date_to)))
                continue;
            if ((filter.eco_from || filter.eco_to) && (row.eco == 0 || (filter.eco_from && row.eco < filter.eco_from) ||
                                                       (filter.eco_to && row.eco > filter.eco_to)))
                continue;
            if (names) {
                const auto ids = snapshot->rowIds(id);
                if (!all.empty() &&
                    (all_bits[ids.white] | all_bits[ids.black] | all_bits[ids.event] | all_bits[ids.site]) != full(all))
                    continue;
                if (!white.empty() && white_bits[ids.white] != full(white)) continue;
                if (!black.empty() && black_bits[ids.black] != full(black)) continue;
                if (!player.empty() && player_bits[ids.white] != full(player) && player_bits[ids.black] != full(player))
                    continue;
            }
            out.push_back(static_cast<GameId>(id));
        }
    });
    if (cancel && cancel->load()) return view;
    std::size_t total = 0;
    for (const auto& p : parts) total += p.size();
    view.ids.reserve(total);
    for (auto& p : parts) view.ids.insert(view.ids.end(), p.begin(), p.end());

    if (sort == SortBy::Number) {
        if (descending) std::reverse(view.ids.begin(), view.ids.end());
        return view;
    }
    std::vector<std::pair<uint32_t, GameId>> items(view.ids.size());
    parallel(view.ids.size(), threads, [&](std::size_t from, std::size_t to, std::size_t) {
        for (std::size_t i = from; i < to; ++i) {
            const auto id = view.ids[i];
            uint32_t key  = 0;
            if (sort >= SortBy::White && sort <= SortBy::Round) {
                const auto ids      = snapshot->rowIds(id);
                const uint32_t name = sort == SortBy::White   ? ids.white
                                      : sort == SortBy::Black ? ids.black
                                      : sort == SortBy::Event ? ids.event
                                      : sort == SortBy::Site  ? ids.site
                                                              : ids.round;
                key                 = names->rank[name];
            } else {
                const auto row = snapshot->numbers(id);
                switch (sort) {
                    case SortBy::Date:
                        key = row.date;
                        break;
                    case SortBy::WhiteElo:
                        key = row.white_elo;
                        break;
                    case SortBy::BlackElo:
                        key = row.black_elo;
                        break;
                    case SortBy::Eco:
                        key = row.eco;
                        break;
                    case SortBy::Plies:
                        key = row.plies;
                        break;
                    default:
                        key = static_cast<unsigned char>(row.result);
                        break;
                }
            }
            // Descending: the keys turned over, the games of one key still in the database's order.
            items[i] = {descending ? ~key : key, id};
        }
    });
    radixSort(items);
    for (std::size_t i = 0; i < items.size(); ++i) view.ids[i] = items[i].second;
    return view;
}

}  // namespace fcg::study
