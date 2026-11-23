#include "fide_list.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>

#include <tour/files.hpp>

#include "players.hpp"
#include "sources.hpp"

namespace fcg::study {

namespace fs = std::filesystem;

namespace {

constexpr std::array<char, 8> MAGIC{'R', 'S', 'F', 'I', 'D', 'E', '1', '\0'};
constexpr uint32_t VERSION      = 1;
constexpr std::size_t HEADER    = 128;
constexpr std::size_t RECORD    = 20;
constexpr uint8_t FLAG_WOMAN    = 1;
constexpr uint8_t FLAG_INACTIVE = 2;
constexpr std::array<const char*, 9> TITLES{"", "GM", "IM", "FM", "CM", "WGM", "WIM", "WFM", "WCM"};

// The columns of FIDE's combined list, in their order; those read are the ones named in Column.
constexpr std::array<std::string_view, 19> COLUMNS{"ID Number", "Name",  "Fed", "Sex",   "Tit",   "WTit", "OTit",
                                                   "FOA",       "SRtng", "SGm", "SK",    "RRtng", "RGm",  "Rk",
                                                   "BRtng",     "BGm",   "BK",  "B-day", "Flag"};
enum Column { ID, NAME, FED, SEX, TIT, WTIT, OTIT, FOA, SRTNG, SGM, SK, RRTNG, RGM, RK, BRTNG, BGM, BK, BDAY, FLAG };

struct Record {
    uint32_t id = 0;
    std::string name;
    uint16_t born = 0, standard = 0, rapid = 0, blitz = 0;
    std::string federation;
    uint8_t title = 0, flags = 0;
};

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

uint32_t number(std::string_view s) {
    s          = trimmed(s);
    uint64_t v = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') return 0;
        v = v * 10 + static_cast<uint64_t>(c - '0');
        if (v > UINT32_MAX) return 0;
    }
    return static_cast<uint32_t>(v);
}

template <typename T>
T readAt(const uint8_t* p, std::size_t offset) {
    T v;
    std::memcpy(&v, p + offset, sizeof v);
    return v;
}

template <typename T>
void put(std::string& out, T v) {
    out.append(reinterpret_cast<const char*>(&v), sizeof v);
}

// The text of FIDE's file, whatever it came as: its zip, or the text in it.
std::unique_ptr<ByteSource> openList(const fs::path& source) {
    char magic[4] = {};
    {
        std::ifstream in(source, std::ios::binary);
        if (!in) throw std::runtime_error("cannot read " + source.string());
        in.read(magic, 4);
    }
    if (std::memcmp(magic, "PK\x03\x04", 4) == 0) {
        auto files = zipFiles(source, [](std::string_view name) {
            return name.size() > 4 &&
                   (name.substr(name.size() - 4) == ".txt" || name.substr(name.size() - 4) == ".TXT");
        });
        if (files.empty()) throw FideFormatError("the archive holds no list (no .txt file)");
        return std::move(files.front());
    }
    auto sources = openSources(source);
    if (sources.empty()) throw std::runtime_error("cannot read " + source.string());
    return std::move(sources.front());
}

}  // namespace

FideListInfo prepareFideList(const fs::path& source, const fs::path& out, const std::string& month,
                             FidePrepareProgress& progress, const std::atomic<bool>& cancel) {
    std::error_code ec;
    progress.total = fs::file_size(source, ec);
    progress.bytes = 0;
    auto text      = openList(source);

    std::vector<Record> records;
    std::vector<std::string> federations;
    std::array<std::size_t, COLUMNS.size() + 1> at{};
    std::size_t header_size = 0;
    bool header             = false;
    FideListInfo info;
    info.month      = month;
    const auto line = [&](std::string_view l) {
        while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.remove_suffix(1);
        if (trimmed(l).empty()) return;
        if (!header) {
            // Each column where its name starts, the names in their order.
            std::size_t from = 0;
            for (std::size_t c = 0; c < COLUMNS.size(); ++c) {
                const auto p = l.find(COLUMNS[c], from);
                if (p == std::string_view::npos)
                    throw FideFormatError("its columns are not those of FIDE's lists (no “" + std::string(COLUMNS[c]) +
                                          "”)");
                at[c] = p;
                from  = p + COLUMNS[c].size();
            }
            if (at[ID] != 0) throw FideFormatError("its columns are not those of FIDE's lists");
            at[COLUMNS.size()] = std::string_view::npos;
            header_size        = l.size();
            header             = true;
            return;
        }
        // A value of the other titles longer than its column pushes the columns after it.
        const std::size_t shift = l.size() > header_size ? l.size() - header_size : 0;
        const auto field        = [&](Column c) {
            auto from = at[c], to = at[c + 1];
            if (c >= FOA) {
                from += shift;
                if (to != std::string_view::npos) to += shift;
            }
            if (from >= l.size()) return std::string_view{};
            return trimmed(l.substr(from, to == std::string_view::npos ? std::string_view::npos : to - from));
        };
        Record r;
        r.id = number(field(ID));
        if (r.id == 0) return;
        r.name           = std::string(field(NAME));
        r.federation     = std::string(field(FED));
        const auto title = field(TIT).empty() ? field(WTIT) : field(TIT);
        for (std::size_t t = 1; t < TITLES.size(); ++t)
            if (title == TITLES[t]) r.title = static_cast<uint8_t>(t);
        const auto clamp16 = [](uint32_t v) { return static_cast<uint16_t>(std::min<uint32_t>(v, 65535)); };
        r.standard         = clamp16(number(field(SRTNG)));
        r.rapid            = clamp16(number(field(RRTNG)));
        r.blitz            = clamp16(number(field(BRTNG)));
        const auto born    = number(field(BDAY));
        r.born             = born >= 1800 && born <= 2200 ? static_cast<uint16_t>(born) : uint16_t{0};
        const auto flag    = field(FLAG);
        if (field(SEX) == "F" || flag.find('w') != std::string_view::npos) r.flags |= FLAG_WOMAN;
        if (flag.find('i') != std::string_view::npos) r.flags |= FLAG_INACTIVE;
        ++info.players;
        info.standard += r.standard > 0;
        info.rapid += r.rapid > 0;
        info.blitz += r.blitz > 0;
        info.rated += r.standard > 0 || r.rapid > 0 || r.blitz > 0;
        records.push_back(std::move(r));
    };

    std::string pending;
    std::vector<char> buffer(1 << 20);
    for (;;) {
        if (cancel.load(std::memory_order_relaxed)) throw std::runtime_error("cancelled");
        const auto n = text->read(buffer.data(), buffer.size());
        if (n == 0) break;
        progress.bytes = text->consumed();
        pending.append(buffer.data(), n);
        std::size_t start = 0;
        for (auto nl = pending.find('\n'); nl != std::string::npos; nl = pending.find('\n', start)) {
            line(std::string_view(pending).substr(start, nl - start));
            start = nl + 1;
        }
        pending.erase(0, start);
    }
    line(pending);
    if (!header) throw FideFormatError("it holds no list");
    if (records.empty()) throw FideFormatError("it holds no player");

    std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) { return a.id < b.id; });
    records.erase(
        std::unique(records.begin(), records.end(), [](const Record& a, const Record& b) { return a.id == b.id; }),
        records.end());
    for (const auto& r : records)
        if (std::find(federations.begin(), federations.end(), r.federation) == federations.end() &&
            federations.size() < 255)
            federations.push_back(r.federation);

    // The indexes by surname and by its skeleton.
    std::vector<std::pair<std::string, uint32_t>> keys(records.size());
    for (std::size_t i = 0; i < records.size(); ++i)
        keys[i] = {nameParts(records[i].name).surname, static_cast<uint32_t>(i)};
    std::sort(keys.begin(), keys.end());
    std::vector<uint32_t> by_surname(records.size());
    for (std::size_t i = 0; i < keys.size(); ++i) by_surname[i] = keys[i].second;
    for (auto& [key, i] : keys) key = nameSkeleton(key);
    std::sort(keys.begin(), keys.end());
    std::vector<uint32_t> by_skeleton(records.size());
    for (std::size_t i = 0; i < keys.size(); ++i) by_skeleton[i] = keys[i].second;
    keys.clear();
    keys.shrink_to_fit();

    std::string names;
    std::string body;
    body.reserve(records.size() * RECORD);
    for (const auto& r : records) {
        put(body, r.id);
        put(body, static_cast<uint32_t>(names.size()));
        put(body, r.born);
        put(body, r.standard);
        put(body, r.rapid);
        put(body, r.blitz);
        const auto fed = std::find(federations.begin(), federations.end(), r.federation);
        put(body, static_cast<uint8_t>(fed == federations.end() ? 255 : fed - federations.begin()));
        put(body, r.title);
        put(body, r.flags);
        put(body, uint8_t{0});
        names += r.name;
    }
    std::string head(HEADER, '\0');
    std::memcpy(head.data(), MAGIC.data(), MAGIC.size());
    const auto set32 = [&](std::size_t o, uint32_t v) { std::memcpy(head.data() + o, &v, 4); };
    const auto set64 = [&](std::size_t o, uint64_t v) { std::memcpy(head.data() + o, &v, 8); };
    set32(8, VERSION);
    set32(12, static_cast<uint32_t>(records.size()));
    std::memcpy(head.data() + 16, month.data(), std::min<std::size_t>(month.size(), 7));
    set32(24, static_cast<uint32_t>(info.rated));
    set32(28, static_cast<uint32_t>(info.standard));
    set32(32, static_cast<uint32_t>(info.rapid));
    set32(36, static_cast<uint32_t>(info.blitz));
    set32(40, static_cast<uint32_t>(federations.size()));
    const uint64_t feds_at = HEADER, records_at = feds_at + ((federations.size() * 3 + 7) & ~std::size_t{7}),
                   names_at = records_at + body.size(), surname_at = (names_at + names.size() + 7) & ~uint64_t{7},
                   skel_at = surname_at + by_surname.size() * 4, end = skel_at + by_skeleton.size() * 4;
    set64(48, records_at);
    set64(56, names_at);
    set64(64, names.size());
    set64(72, surname_at);
    set64(80, skel_at);
    set64(88, feds_at);
    set64(96, end);
    std::string file = head;
    for (const auto& f : federations) file += (f + "   ").substr(0, 3);
    file.resize(records_at, '\0');
    file += body;
    file += names;
    file.resize(surname_at, '\0');
    file.append(reinterpret_cast<const char*>(by_surname.data()), by_surname.size() * 4);
    file.append(reinterpret_cast<const char*>(by_skeleton.data()), by_skeleton.size() * 4);
    if (cancel.load()) throw std::runtime_error("cancelled");
    fs::create_directories(out.parent_path(), ec);
    tour::replaceFile(out, file);
    info.players = records.size();
    return info;
}

FideList::FideList(const fs::path& file) : file_(file) {
    const auto* p  = file_.data();
    const auto bad = [&] { return std::runtime_error(file.string() + ": not a FIDE list of RookStudio's"); };
    if (file_.size() < HEADER || std::memcmp(p, MAGIC.data(), MAGIC.size()) != 0) throw bad();
    if (readAt<uint32_t>(p, 8) > VERSION) throw bad();
    count_ = readAt<uint32_t>(p, 12);
    info_.month.assign(reinterpret_cast<const char*>(p) + 16, strnlen(reinterpret_cast<const char*>(p) + 16, 8));
    info_.players         = count_;
    info_.rated           = readAt<uint32_t>(p, 24);
    info_.standard        = readAt<uint32_t>(p, 28);
    info_.rapid           = readAt<uint32_t>(p, 32);
    info_.blitz           = readAt<uint32_t>(p, 36);
    const auto feds       = readAt<uint32_t>(p, 40);
    const auto records_at = readAt<uint64_t>(p, 48), names_at = readAt<uint64_t>(p, 56),
               names_bytes = readAt<uint64_t>(p, 64), surname_at = readAt<uint64_t>(p, 72),
               skel_at = readAt<uint64_t>(p, 80), feds_at = readAt<uint64_t>(p, 88), end = readAt<uint64_t>(p, 96);
    if (end != file_.size() || records_at + count_ * RECORD > names_at || names_at + names_bytes > surname_at ||
        surname_at + count_ * 4 != skel_at || skel_at + count_ * 4 != end || feds_at + feds * 3 > records_at)
        throw bad();
    for (uint32_t f = 0; f < feds; ++f) {
        std::string fed(reinterpret_cast<const char*>(p) + feds_at + f * 3, 3);
        while (!fed.empty() && fed.back() == ' ') fed.pop_back();
        federations_.push_back(std::move(fed));
    }
    records_     = p + records_at;
    names_       = reinterpret_cast<const char*>(p) + names_at;
    by_surname_  = p + surname_at;
    by_skeleton_ = p + skel_at;
    // The last name ends where the names do.
    names_end_ = names_bytes;
}

std::string_view FideList::nameOf(std::size_t i) const {
    const auto from = readAt<uint32_t>(records_, i * RECORD + 4);
    const auto to   = i + 1 < count_ ? readAt<uint32_t>(records_, (i + 1) * RECORD + 4) : names_end_;
    return {names_ + from, to - from};
}

FidePlayer FideList::player(std::size_t i) const {
    FidePlayer out;
    const auto o = i * RECORD;
    out.id       = readAt<uint32_t>(records_, o);
    out.name     = nameOf(i);
    out.born     = readAt<uint16_t>(records_, o + 8);
    out.standard = readAt<uint16_t>(records_, o + 10);
    out.rapid    = readAt<uint16_t>(records_, o + 12);
    out.blitz    = readAt<uint16_t>(records_, o + 14);
    const auto f = records_[o + 16];
    if (f < federations_.size()) out.federation = federations_[f];
    const auto t = records_[o + 17];
    if (t < TITLES.size()) out.title = TITLES[t];
    out.woman    = records_[o + 18] & FLAG_WOMAN;
    out.inactive = records_[o + 18] & FLAG_INACTIVE;
    return out;
}

std::optional<std::size_t> FideList::byId(uint32_t id) const {
    std::size_t lo = 0, hi = count_;
    while (lo < hi) {
        const auto mid = (lo + hi) / 2;
        const auto v   = readAt<uint32_t>(records_, mid * RECORD);
        if (v == id) return mid;
        if (v < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    return std::nullopt;
}

namespace {

template <typename Key>
std::vector<std::size_t> range(const uint8_t* index, std::size_t count, std::string_view wanted, Key key) {
    const auto at  = [&](std::size_t k) { return static_cast<std::size_t>(readAt<uint32_t>(index, k * 4)); };
    std::size_t lo = 0, hi = count;
    while (lo < hi) {  // the first not below
        const auto mid = (lo + hi) / 2;
        if (key(at(mid)) < wanted)
            lo = mid + 1;
        else
            hi = mid;
    }
    std::vector<std::size_t> out;
    for (auto k = lo; k < count && out.size() < 4096; ++k) {
        if (key(at(k)) != wanted) break;
        out.push_back(at(k));
    }
    return out;
}

}  // namespace

std::vector<std::size_t> FideList::bySurname(std::string_view surname) const {
    if (surname.empty()) return {};
    return range(by_surname_, count_, surname, [&](std::size_t i) { return nameParts(nameOf(i)).surname; });
}

std::vector<std::size_t> FideList::bySkeleton(std::string_view skeleton) const {
    if (skeleton.empty()) return {};
    return range(by_skeleton_, count_, skeleton,
                 [&](std::size_t i) { return nameSkeleton(nameParts(nameOf(i)).surname); });
}

}  // namespace fcg::study
