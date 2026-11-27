#include "store.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>

#include <chess.hpp>
#include <json.hpp>

#include "scan_board.hpp"
#include "store_detail.hpp"
#include "store_error.hpp"

#ifndef _WIN32
#    include <fcntl.h>
#    include <unistd.h>
#endif

namespace fcg::study {

namespace fs = std::filesystem;

using namespace store;

namespace {

std::function<void(std::string_view)> g_fault_hook;

constexpr std::array<std::string_view, NAME_COLUMNS> NAME_TAGS{"White", "Black",     "Event",      "Site",
                                                               "Round", "Annotator", "TimeControl"};

// A rating as the columns keep it: 1 to 65535, written as such; 0 for anything else.
uint16_t number16(std::string_view s) {
    unsigned v         = 0;
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    const bool ok      = ec == std::errc() && p == s.data() + s.size() && v > 0 && v < 65536 && std::to_string(v) == s;
    return ok ? static_cast<uint16_t>(v) : uint16_t{0};
}

// Game `g` as record k of a new segment: the ids of its names (`name_id`, which adds those the database has not), its
// numbers, its moves and its signature (made from its moves when the import has not), and in its annex the tags the
// columns do not hold, its start position, text, clocks and evaluations.
template <typename NameId>
void putGame(SegmentBytes& segment, std::size_t k, const ImportedGame& g, uint64_t key, NameId&& name_id) {
    std::array<std::string, NAME_COLUMNS> names;
    std::string date, white_elo, black_elo, eco;
    std::vector<std::pair<std::string, std::string>> others;
    for (const auto& [tag, value] : g.tags) {
        const auto column = std::find(NAME_TAGS.begin(), NAME_TAGS.end(), tag);
        // A site that is the game's own address (Lichess's) would add a name for each game: kept in the annex with the
        // other tags.
        const bool own_address = tag == "Site" && (value.rfind("https://", 0) == 0 || value.rfind("http://", 0) == 0);
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
        segment.put(static_cast<Column>(c), k, name_id(names[static_cast<std::size_t>(c)]));
    segment.put(DATE, k, packDate(date));
    segment.put(PLIES, k, g.plies);
    segment.put(WHITE_ELO, k, number16(white_elo));
    segment.put(BLACK_ELO, k, number16(black_elo));
    segment.put(ECO, k, packEco(eco));
    uint16_t flags = 0;
    if (g.annotated) flags |= GAME_ANNOTATED;
    if (g.damaged) flags |= GAME_DAMAGED;
    if (g.chess960) flags |= GAME_CHESS960;
    if (!g.start_fen.empty()) flags |= GAME_FEN;
    if (!g.clocks.empty()) flags |= GAME_CLOCKS;
    if (!g.evals.empty()) flags |= GAME_EVALS;
    segment.put(FLAGS, k, flags);
    segment.put(RESULT, k, static_cast<uint8_t>(g.result));
    segment.put(KEY, k, key);
    segment.start(k);
    segment.moves.append(reinterpret_cast<const char*>(g.moves.data()), g.moves.size());
    auto signature = g.signature;
    if (!signature) {
        ScanBoard start;
        if (start.setFen(
                g.start_fen.empty() ? std::string_view(chess::constants::STARTPOS) : std::string_view(g.start_fen),
                g.chess960))
            signature = signatureOf(start, g.moves, g.plies);
    }
    if (signature) segment.signature(k, *signature);
    uint8_t parts = 0;
    if (!g.start_fen.empty()) parts |= ANNEX_FEN;
    if (!others.empty()) parts |= ANNEX_TAGS;
    if (!g.text.empty()) parts |= ANNEX_TEXT;
    if (!g.clocks.empty()) parts |= ANNEX_CLOCKS;
    if (!g.evals.empty()) parts |= ANNEX_EVALS;
    if (parts) {
        auto& annex = segment.annex;
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

std::string playersName(uint64_t generation) {
    char name[32];
    std::snprintf(name, sizeof name, "players-%06llu.bin", static_cast<unsigned long long>(generation));
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
// The store's own parts (store_detail.hpp).

namespace store {

void step(std::string_view name) {
    if (g_fault_hook) g_fault_hook(name);
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

StoreErrc writeErrc(int error) noexcept {
#ifdef EDQUOT
    if (error == EDQUOT) return StoreErrc::DiskFull;  // the user's quota: full as far as they are concerned
#endif
    return error == ENOSPC ? StoreErrc::DiskFull : StoreErrc::Other;
}
StoreErrc writeErrc(const std::error_code& ec) noexcept {
    return ec == std::errc::no_space_on_device ? StoreErrc::DiskFull : StoreErrc::Other;
}

StoreError readOnlyError(const fs::path& dir) {
    return StoreError(StoreErrc::ReadOnly, dir.string() + ": read-only, another program writes to it");
}

void writeFile(const fs::path& file, std::string_view bytes) {
    step("write " + file.filename().string());
    // Closed however this ends: Windows removes no folder whose files are open.
    const std::unique_ptr<std::FILE, int (*)(std::FILE*)> f(tour::openFile(file, "wb"), &std::fclose);
    if (!f) throw StoreError(writeErrc(errno), "cannot write " + file.string());
    const bool written    = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), f.get()) == bytes.size();
    const int write_error = written ? 0 : errno;
    step("sync " + file.filename().string());
    if (!written || !tour::syncFile(f.get()))
        throw StoreError(writeErrc(written ? errno : write_error),
                         "cannot write " + file.string() + " (is the disk full?)");
}

std::string editsName(uint64_t generation) {
    char name[32];
    std::snprintf(name, sizeof name, "edits-%06llu.bin", static_cast<unsigned long long>(generation));
    return name;
}

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

std::string SegmentWriter::write(const SegmentBytes& segment) {
    std::string name;
    {
        std::lock_guard lock(numbers_);
        name = segmentName(next_number_++);
    }
    const auto tmp = dir_ / (name + ".tmp");
    under_way_     = name;
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    if (ec) throw StoreError(writeErrc(ec), "cannot create " + tmp.string());
    writeFile(tmp / "cols.bin", segment.cols);
    writeFile(tmp / "moves.bin", segment.moves);
    writeFile(tmp / "annex.bin", segment.annex);
    writeFile(tmp / "names.bin", segment.names);
    writeFile(tmp / "sig.bin", segment.sig);
    syncFolder(tmp);
    step("rename segment");
    fs::rename(tmp, dir_ / name, ec);
    if (ec) throw std::runtime_error("cannot rename " + tmp.string() + ": " + ec.message());
    under_way_.clear();
    written_.push_back(name);
    step("sync folder");
    syncFolder(dir_);
    return name;
}

void SegmentWriter::discard() {
    std::error_code ec;
    for (const auto& name : written_) fs::remove_all(dir_ / name, ec);
    if (!under_way_.empty()) fs::remove_all(dir_ / (under_way_ + ".tmp"), ec);
    written_.clear();
    under_way_.clear();
}

Manifest Manifest::read(const fs::path& dir) {
    std::string text;
    {
        // Closed before the manifest is replaced: Windows does not rename over an open file.
        std::ifstream in(dir / MANIFEST, std::ios::binary);
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    return parse(text, dir);
}

Manifest Manifest::parse(std::string_view text, const fs::path& dir) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text);
    } catch (const std::exception& e) {
        throw StoreError(StoreErrc::Damaged, (dir / MANIFEST).string() + ": " + e.what());
    }
    if (j.value("format", "") != "rookstudio-database")
        throw StoreError(StoreErrc::NotADatabase, dir.string() + " is not a database");
    if (j.value("version", 0u) > VERSION)
        throw StoreError(StoreErrc::NewerFormat, dir.string() + ": a database of a newer program");
    const auto damaged = [&] { return StoreError(StoreErrc::Damaged, dir.string() + ": damaged manifest"); };
    Manifest m;
    m.version      = j.value("version", 0u);
    m.generation   = j.value("generation", uint64_t{0});
    uint64_t games = 0, names = 0;  // of the segments before
    for (const auto& s : j.at("segments")) {
        Entry e;
        e.name  = s.at("name").get<std::string>();
        e.games = s.at("games").get<uint32_t>();
        e.names = s.at("names").get<uint32_t>();
        // No commit writes more: seven names a game at most (a segment that cannot be read would make room for each).
        if (e.games > SEGMENT_GAMES || e.names > 7 * SEGMENT_GAMES + 1 || names > UINT32_MAX - e.names) throw damaged();
        e.versions      = s.value("versions", false);
        e.unknown_names = s.value("unknown_names", false);
        e.source        = s.value("source", std::string());
        for (const auto& [key, value] : s.items())
            if (key != "name" && key != "games" && key != "names" && key != "versions" && key != "unknown_names" &&
                key != "source")
                e.others[key] = value;
        if (!e.versions) games += e.games;
        names += e.names;
        m.segments.push_back(std::move(e));
    }
    // Every game names its players, if only with the empty name: games without any name are a manifest made up, whose
    // games' name ids could not read as no name (Snapshot::rowIds()).
    if (games > 0 && names == 0) throw damaged();
    m.edits = j.value("edits", std::string());
    if (j.contains("edits_generation")) m.edits_generation = j.at("edits_generation").get<uint64_t>();
    m.players = j.value("players", std::string());
    for (const auto& [key, value] : j.items())
        if (key != "format" && key != "version" && key != "generation" && key != "segments" && key != "edits" &&
            key != "edits_generation" && key != "players")
            m.others[key] = value;
    return m;
}

std::string Manifest::text() const {
    nlohmann::json j = others;
    j["format"]      = "rookstudio-database";
    j["version"]     = version;
    j["generation"]  = generation;
    auto list        = nlohmann::json::array();
    for (const auto& s : segments) {
        nlohmann::json e = s.others;
        e["name"]        = s.name;
        e["games"]       = s.games;
        e["names"]       = s.names;
        if (s.versions) e["versions"] = true;
        if (!s.source.empty()) e["source"] = s.source;
        if (s.unknown_names) e["unknown_names"] = true;
        list.push_back(std::move(e));
    }
    j["segments"] = std::move(list);
    if (!edits.empty()) j["edits"] = edits;
    if (edits_generation) j["edits_generation"] = *edits_generation;
    if (!players.empty()) j["players"] = players;
    return j.dump(1);
}

void Manifest::write(const fs::path& dir) const { tour::replaceFile(dir / MANIFEST, text()); }

}  // namespace store

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

}  // namespace

ImportedGame store::recordGame(const Snapshot& snap, const Segment& segment, std::size_t i, bool strict) {
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
    Manifest().write(dir);  // no segment, generation 0
    db->load();
    return db;
}

std::shared_ptr<Database> Database::open(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_regular_file(dir / MANIFEST, ec))
        throw StoreError(StoreErrc::NotADatabase, dir.string() + " is not a database");
    auto db = std::make_shared<Database>(dir);
    db->load();
    return db;
}

std::shared_ptr<Database> Database::openReadOnly(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_regular_file(dir / MANIFEST, ec))
        throw StoreError(StoreErrc::NotADatabase, dir.string() + " is not a database");
    auto db = std::make_shared<Database>(dir, true);
    db->load();
    return db;
}

bool Database::refresh() {
    if (lock_) return false;
    std::lock_guard writing(write_mutex_);  // current_ changes under it, as for a change
    uint64_t generation = 0;
    try {
        generation = Manifest::read(dir_).generation;
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
    const auto manifest = Manifest::read(dir_);
    auto snap           = std::make_shared<Snapshot>();
    snap->generation_   = manifest.generation;
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
    for (const auto& s : manifest.segments) {
        const auto& name      = s.name;
        const auto games      = s.games;
        const auto names      = s.names;
        const bool versions   = s.versions;
        const bool names_lost = s.unknown_names;  // written as "?" by keepReadable()
        std::shared_ptr<const Segment> segment;
        try {
            segment = std::make_shared<const Segment>(dir_ / name, games, names, !names_lost);
        } catch (const std::exception& e) {
            segment = std::make_shared<const Segment>(Segment::Unreadable{}, dir_ / name, games, names, 0, !names_lost);
            unread.push_back({snap->segments_.size(),
                              {name, versions ? GameId{0} : snap->games_, games, versions, e.what(), s.source,
                               names == 0 || segment->namesKnown()},
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
        snap->sources_.push_back(s.source);
        snap->versions_.push_back(versions);
        named.push_back(name);
        unsigned long long number = 0;
        std::sscanf(name.c_str(), "seg-%llu", &number);
        next_segment = std::max<uint64_t>(next_segment, number + 1);
    }
    // Read-only, a segment that the other program removed as it committed (a compaction's, an undoing's) is no damage:
    // read again once its manifest has settled.
    if (!lock_ && !unread.empty() && tries > 0) {
        uint64_t now = snap->generation_ + 1;  // a manifest being replaced as it is read has changed too
        try {
            now = Manifest::read(dir_).generation;
        } catch (const std::exception&) {
        }
        if (now != snap->generation_) return load(clean, tries - 1);
    }
    // The edits it names: the games replaced and those deleted.
    const auto& edits       = manifest.edits;
    snap->edits_generation_ = manifest.edits_generation.value_or(0);
    if (!edits.empty()) {
        std::ifstream in(dir_ / edits, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto bad = [&] { return StoreError(StoreErrc::Damaged, (dir_ / edits).string() + ": damaged edits"); };
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
    const auto& players = manifest.players;
    if (!players.empty()) {
        std::ifstream in(dir_ / players, std::ios::binary);
        if (!in) throw StoreError(StoreErrc::Damaged, (dir_ / players).string() + ": missing");
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        snap->players_ =
            std::make_shared<const PlayerTable>(spellingsWithin(PlayerTable::fromBytes(text), snap->names_));
        snap->players_file_ = players;
    }
    // The segments that cannot be read: their one empty game named as games without names are.
    if (!unread.empty()) {
        const auto blank = snap->blankName().value_or(0);
        for (auto& u : unread) {
            snap->segments_[u.index] = std::make_shared<const Segment>(
                Segment::Unreadable{}, dir_ / u.bad.name, static_cast<uint32_t>(u.bad.games),
                manifest.segments[u.index].names, blank, u.names_known);
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
    SegmentWriter writer(dir_, mutex_, next_segment_);  // the segments it writes
    const auto& made = writer.written();
    std::vector<std::pair<uint32_t, uint32_t>> counts;  // their games and names
    auto manifest             = Manifest::read(dir_);
    const auto edits_before   = manifest.edits;
    const auto players_before = manifest.players;
    std::string edits_written, players_written;
    bool committed = false;
    try {
        for (std::size_t from = 0; from < games.size(); from += SEGMENT_GAMES) {
            const std::size_t n     = std::min(SEGMENT_GAMES, games.size() - from);
            const auto names_before = added.size();
            SegmentBytes segment(n);
            for (std::size_t k = 0; k < n; ++k)
                putGame(segment, k, games[from + k], keys[from + k],
                        [&](const std::string& name) { return nameId(name, added); });
            const auto new_names = static_cast<uint32_t>(added.size() - names_before);
            for (std::size_t a = names_before; a < added.size(); ++a) segment.names += added[a] + "\n";
            segment.finish(new_names);
            writer.write(segment);
            counts.emplace_back(static_cast<uint32_t>(n), new_names);
        }
        for (std::size_t s = 0; s < made.size(); ++s) {
            Manifest::Entry entry;
            entry.name     = made[s];
            entry.games    = counts[s].first;
            entry.names    = counts[s].second;
            entry.versions = versions;
            entry.source   = source;
            manifest.segments.push_back(std::move(entry));
        }
        if (players) {
            players_written = playersName(current_->generation_ + 1);
            writeFile(dir_ / players_written, players->bytes());
            manifest.players = players_written;
        }
        if (replaced || deleted) {
            edits_written = editsName(current_->generation_ + 1);
            writeFile(dir_ / edits_written,
                      editsBytes(replaced ? *replaced : current_->replaced_, deleted ? *deleted : current_->deleted_));
            manifest.edits            = edits_written;
            manifest.edits_generation = current_->generation_ + 1;
        }
        manifest.generation = current_->generation_ + 1;
        step("write manifest");
        manifest.write(dir_);
        committed = true;
        step("manifest written");
    } catch (const std::exception&) {
        if (committed) throw;  // the games are in: nothing to undo
        // The disk refused: the database as before, the names this append gave forgotten.
        for (const auto& name : added) name_ids_.erase(name);
        writer.discard();
        std::error_code ec;
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
    if (!lock_) throw readOnlyError(dir_);
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
    if (!lock_) throw readOnlyError(dir_);
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
    if (!lock_) throw readOnlyError(dir_);
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

std::vector<GameState> Database::merge(std::vector<Merge> merges) {
    if (!lock_) throw readOnlyError(dir_);
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
    if (!lock_) throw readOnlyError(dir_);
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
    if (!lock_) throw readOnlyError(dir_);
    std::lock_guard writing(write_mutex_);
    table = spellingsWithin(std::move(table), current_->names_);
    std::vector<ImportedGame> none;
    commit(none, {}, false, nullptr, nullptr, &table);
}

void Database::clear() {
    if (!lock_) throw readOnlyError(dir_);
    std::lock_guard writing(write_mutex_);
    const auto before         = snapshot();
    auto manifest             = Manifest::read(dir_);
    const auto edits_before   = manifest.edits;
    const auto players_before = manifest.players;
    // As a database just made, its generation going on; every game taken out, as edits do: what was built from the
    // games before (an opening tree) no longer holds, nor what the window keeps by generation. A compaction under way
    // stops before its commit for it.
    const uint64_t generation = before->generation_ + 1;
    manifest.segments.clear();
    manifest.edits_generation = generation;
    manifest.generation       = generation;
    manifest.edits.clear();
    manifest.players.clear();
    step("write manifest");
    manifest.write(dir_);
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
