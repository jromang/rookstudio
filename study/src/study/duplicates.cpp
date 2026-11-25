#include "duplicates.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <numeric>

#include "pgn_lexer.hpp"
#include "players.hpp"

namespace fcg::study {

std::size_t Duplicates::count(DuplicateKind kind) const {
    return static_cast<std::size_t>(
        std::count_if(groups.begin(), groups.end(), [&](const DuplicateGroup& g) { return g.kind == kind; }));
}

namespace {

uint64_t mix64(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h *= 0xff51afd7ed558ccdULL;
    return h ^ (h >> 33);
}

uint64_t hashText(std::string_view s) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const unsigned char c : s) h = (h ^ c) * 0x100000001b3ULL;
    return h;
}

// A player's key for the duplicates: the surname and the first initial of the name everything shows (the table of
// names' when the name is in it); 0 for a name that says no one.
uint64_t playerKey(const Snapshot& snap, uint32_t name_id) {
    const auto shown = snap.shownName(name_id);
    if (anonymousName(shown)) return 0;
    const auto parts = nameParts(shown);
    if (parts.surname.empty()) return 0;
    return mix64(hashText(parts.surname), static_cast<uint64_t>(static_cast<unsigned char>(parts.initial)) + 1);
}

std::vector<std::string> roundParts(std::string_view r) {
    while (!r.empty() && (r.front() == ' ' || r.front() == '\t')) r.remove_prefix(1);
    while (!r.empty() && (r.back() == ' ' || r.back() == '\t')) r.remove_suffix(1);
    std::vector<std::string> parts;
    if (r.empty() || r == "?" || r == "-" || r == "*") return parts;
    std::size_t at = 0;
    while (at <= r.size()) {
        const auto end = std::min(r.find('.', at), r.size());
        auto part      = r.substr(at, end - at);
        while (part.size() > 1 && part.front() == '0') part.remove_prefix(1);
        parts.emplace_back(part);
        at = end + 1;
    }
    return parts;
}

bool resultsAgree(const std::vector<char>& results) {
    char seen = 0;
    for (const char r : results) {
        if (r == '*') continue;
        if (seen && r != seen) return false;
        seen = r;
    }
    return true;
}

struct Candidate {
    GameId id = 0;
    std::string address;  // its own address (Lichess's games have one): two games of their own differ
    std::span<const uint8_t> moves;
    uint32_t plies = 0;
    uint32_t date  = 0;
    uint16_t flags = 0;
    char result    = '*';
    std::string_view round;
    std::string fen;
};

bool isPrefix(std::span<const uint8_t> a, std::span<const uint8_t> b) {
    return a.size() <= b.size() && std::equal(a.begin(), a.end(), b.begin());
}

// The groups of one bucket of games: the same players and year.
void bucketGroups(const Snapshot& snap, std::vector<Candidate>& games, std::vector<DuplicateGroup>& out) {
    std::sort(games.begin(), games.end(), [](const Candidate& a, const Candidate& b) {
        if (a.fen != b.fen) return a.fen < b.fen;
        if ((a.flags & GAME_CHESS960) != (b.flags & GAME_CHESS960)) return (a.flags & GAME_CHESS960) == 0;
        const auto m = std::lexicographical_compare(a.moves.begin(), a.moves.end(), b.moves.begin(), b.moves.end());
        if (m) return true;
        if (std::lexicographical_compare(b.moves.begin(), b.moves.end(), a.moves.begin(), a.moves.end())) return false;
        return a.id < b.id;
    });
    std::vector<std::size_t> parent(games.size());
    std::iota(parent.begin(), parent.end(), std::size_t{0});
    const auto find = [&](std::size_t x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    };
    const auto& table = *snap.players();
    for (std::size_t k = 0; k < games.size(); ++k) {
        const auto& a = games[k];
        // The games whose moves start with a's follow it: those it can be.
        for (std::size_t m = k + 1; m < games.size() && m <= k + 256; ++m) {
            const auto& b = games[m];
            if (b.fen != a.fen || (b.flags & GAME_CHESS960) != (a.flags & GAME_CHESS960)) break;
            if (!isPrefix(a.moves, b.moves)) break;
            if (a.plies == 0 && b.plies > 0) break;
            const bool same_moves = a.moves.size() == b.moves.size() && a.plies == b.plies;
            // A game cut short is unfinished: one that ended differs from the game that went on.
            if (!same_moves && a.result != '*') continue;
            if (!roundsCompatible(a.round, b.round) || !datesCompatible(a.date, b.date)) continue;
            // Moves enough to tell a game: ten moves when both days are known (and the same), twenty otherwise.
            const bool days = (a.date & 31) && (b.date & 31);
            if (a.plies < (days ? 20u : 40u)) continue;
            if (table.keptApart(a.id, b.id)) continue;
            if (!a.address.empty() && !b.address.empty() && a.address != b.address) continue;
            parent[find(m)] = find(k);
        }
    }
    std::map<std::size_t, std::vector<std::size_t>> sets;
    for (std::size_t k = 0; k < games.size(); ++k) sets[find(k)].push_back(k);
    for (const auto& [root, members] : sets) {
        if (members.size() < 2) continue;
        DuplicateGroup group;
        std::vector<char> results;
        uint32_t longest = 0, shortest = UINT32_MAX;
        for (const auto k : members) {
            group.games.push_back(games[k].id);
            results.push_back(games[k].result);
            longest  = std::max(longest, games[k].plies);
            shortest = std::min(shortest, games[k].plies);
        }
        std::sort(group.games.begin(), group.games.end());
        if (!resultsAgree(results))
            group.kind = DuplicateKind::Results;
        else
            group.kind = longest == shortest ? DuplicateKind::SameMoves : DuplicateKind::Shorter;
        out.push_back(std::move(group));
    }
}

}  // namespace

bool roundsCompatible(std::string_view a, std::string_view b) {
    const auto pa = roundParts(a), pb = roundParts(b);
    if (pa.empty() || pb.empty()) return true;
    const auto k = std::min(pa.size(), pb.size());
    return std::equal(pa.begin(), pa.begin() + static_cast<std::ptrdiff_t>(k), pb.begin());
}

bool datesCompatible(uint32_t a, uint32_t b) {
    const uint32_t pa[3] = {a >> 9, (a >> 5) & 15, a & 31}, pb[3] = {b >> 9, (b >> 5) & 15, b & 31};
    for (int i = 0; i < 3; ++i)
        if (pa[i] && pb[i] && pa[i] != pb[i]) return false;
    return true;
}

Duplicates findDuplicates(const std::shared_ptr<const Snapshot>& snapshot, GameId since, DuplicateProgress& progress,
                          const std::atomic<bool>& cancel) {
    const auto started = std::chrono::steady_clock::now();
    Duplicates out;
    const auto& snap = *snapshot;
    const auto n     = snap.games();
    progress.total   = 2 * n;
    progress.done    = 0;
    // Each game in the bucket of its players and its year.
    std::vector<uint64_t> keys(snap.names(), ~uint64_t{0});
    const auto key = [&](uint32_t id) -> uint64_t {
        if (id >= keys.size()) return 0;
        auto& k = keys[id];
        if (k == ~uint64_t{0}) k = playerKey(snap, id);
        return k;
    };
    std::vector<std::pair<uint64_t, GameId>> items;
    items.reserve(static_cast<std::size_t>(n));
    for (GameId id = 0; id < n; ++id) {
        if ((id & 0xFFFF) == 0) {
            progress.done = id;
            if (cancel.load(std::memory_order_relaxed)) {
                out.cancelled = true;
                return out;
            }
        }
        if (snap.deleted(id)) continue;
        const auto ids = snap.rowIds(id);
        const auto w = key(ids.white), b = key(ids.black);
        if (w == 0 || b == 0) continue;
        items.emplace_back(mix64(mix64(w, b * 31 + 7), snap.numbers(id).date >> 9), id);
    }
    out.games = items.size();
    if (since > 0) {
        std::vector<uint64_t> wanted;
        for (const auto& [bucket, id] : items)
            if (id >= since) wanted.push_back(bucket);
        std::sort(wanted.begin(), wanted.end());
        wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
        std::erase_if(items,
                      [&](const auto& item) { return !std::binary_search(wanted.begin(), wanted.end(), item.first); });
    }
    std::sort(items.begin(), items.end());
    progress.done = n;
    std::vector<Candidate> bucket;
    for (std::size_t i = 0; i < items.size();) {
        if (cancel.load(std::memory_order_relaxed)) {
            out.cancelled = true;
            break;
        }
        std::size_t j = i + 1;
        while (j < items.size() && items[j].first == items[i].first) ++j;
        if (j - i >= 2) {
            bucket.clear();
            for (std::size_t k = i; k < j; ++k) {
                const auto id = items[k].second;
                Candidate c;
                c.id               = id;
                const auto moves   = snap.gameMoves(id);
                c.moves            = moves.codes;
                c.plies            = moves.plies;
                c.flags            = moves.flags;
                const auto numbers = snap.numbers(id);
                c.date             = numbers.date;
                c.result           = numbers.result;
                c.round            = snap.name(snap.rowIds(id).round);
                if (c.flags & GAME_FEN) c.fen = snap.startFen(id);
                // A site that is the game's own address is kept with the other tags, not in the column.
                if (snap.row(id).site.empty()) {
                    c.address = snap.tag(id, "Site");
                    if (c.address.rfind("http", 0) != 0 ||
                        c.address.find("/broadcast/") != std::string::npos)  // a broadcast's round, not a game's
                        c.address.clear();
                }
                bucket.push_back(std::move(c));
            }
            bucketGroups(snap, bucket, out.groups);
        }
        i             = j;
        progress.done = n + (items.empty() ? n : n * i / items.size());
    }
    if (since > 0) std::erase_if(out.groups, [&](const DuplicateGroup& g) { return g.games.back() < since; });
    std::sort(out.groups.begin(), out.groups.end(), [](const DuplicateGroup& a, const DuplicateGroup& b) {
        if (a.kind != b.kind) return a.kind < b.kind;
        return a.games.front() < b.games.front();
    });
    progress.done = 2 * n;
    out.seconds   = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// The merge.

namespace {

std::string_view tagOf(const ImportedGame& game, std::string_view name) {
    for (const auto& [k, v] : game.tags)
        if (k == name) return v;
    return {};
}

// The tags each field takes.
const std::array<std::vector<std::string_view>, MERGE_FIELDS>& fieldTags() {
    static const std::array<std::vector<std::string_view>, MERGE_FIELDS> tags = {{
        {"White"},
        {"Black"},
        {"Event"},
        {"Site"},
        {"Round"},
        {"Date"},
        {"WhiteElo", "BlackElo"},
        {"ECO"},
        {"WhiteFideId", "BlackFideId"},
        {"FEN", "SetUp", "Variant", "PlyCount"},  // the moves'
        {},
        {},
        {"Result"},
    }};
    return tags;
}

int fieldOfTag(std::string_view name) {
    const auto& tags = fieldTags();
    for (std::size_t f = 0; f < tags.size(); ++f)
        for (const auto t : tags[f])
            if (t == name) return static_cast<int>(f);
    return -1;
}

bool unknown(std::string_view v) {
    while (!v.empty() && v.front() == ' ') v.remove_prefix(1);
    return v.empty() || v == "?" || v == "-" || v == "*";
}

std::size_t lettersIn(std::string_view v) {
    return static_cast<std::size_t>(
        std::count_if(v.begin(), v.end(), [](unsigned char c) { return std::isalnum(c) || c >= 0x80; }));
}

std::size_t known(const std::vector<int32_t>& clocks) {
    return static_cast<std::size_t>(std::count_if(clocks.begin(), clocks.end(), [](int32_t c) { return c >= 0; }));
}
std::size_t known(const std::vector<int16_t>& evals) {
    return static_cast<std::size_t>(
        std::count_if(evals.begin(), evals.end(), [](int16_t e) { return e != ImportedGame::NO_EVAL; }));
}

const char* resultText(char r) { return r == '1' ? "1-0" : r == '0' ? "0-1" : r == '=' ? "1/2-1/2" : "*"; }

std::string escaped(std::string_view v) {
    std::string out;
    for (const char c : v) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out;
}

// An annotated game's text with these tags and this result: its moves and comments as they were.
std::string withTags(std::string_view text, const std::vector<std::pair<std::string, std::string>>& tags,
                     std::string_view result) {
    PgnLexer lex(text);
    auto word = lex.next();
    while (word.kind == PgnToken::Tag) word = lex.next();
    const std::size_t moves_at = word.kind == PgnToken::End ? text.size() : word.at;
    std::size_t result_at = std::string_view::npos, result_size = 0;
    int depth = 0;
    for (; word.kind != PgnToken::End && word.kind != PgnToken::Tag; word = lex.next()) {
        if (word.kind == PgnToken::VariationStart) {
            ++depth;
        } else if (word.kind == PgnToken::VariationEnd) {
            if (depth > 0) --depth;
        } else if (word.kind == PgnToken::Result && depth == 0) {
            result_at   = word.at;
            result_size = word.text.size();
            break;
        }
    }
    std::string out;
    for (const auto& [k, v] : tags) out += "[" + k + " \"" + escaped(v) + "\"]\n";
    out += "\n";
    if (result_at != std::string_view::npos) {
        out += text.substr(moves_at, result_at - moves_at);
        out += result;
        out += text.substr(result_at + result_size);
    } else {
        auto moves = text.substr(moves_at);
        while (!moves.empty() && (moves.back() == '\n' || moves.back() == ' ')) moves.remove_suffix(1);
        out += std::string(moves) + (moves.empty() ? "" : " ") + std::string(result) + "\n";
    }
    return out;
}

constexpr std::array<const char*, MERGE_FIELDS> FIELD_NAMES = {"White",  "Black",       "Event", "Site",     "Round",
                                                               "Date",   "ratings",     "ECO",   "FIDE ids", "moves",
                                                               "clocks", "evaluations", "result"};

}  // namespace

std::vector<std::array<std::string, MERGE_FIELDS>> mergeFields(const std::vector<ImportedGame>& games) {
    std::vector<std::array<std::string, MERGE_FIELDS>> out;
    for (const auto& g : games) {
        std::array<std::string, MERGE_FIELDS> f;
        const auto set = [&](MergeField field, std::string v) { f[static_cast<std::size_t>(field)] = std::move(v); };
        set(MergeField::White, std::string(tagOf(g, "White")));
        set(MergeField::Black, std::string(tagOf(g, "Black")));
        set(MergeField::Event, std::string(tagOf(g, "Event")));
        set(MergeField::Site, std::string(tagOf(g, "Site")));
        set(MergeField::Round, std::string(tagOf(g, "Round")));
        set(MergeField::Date, std::string(tagOf(g, "Date")));
        const auto we = tagOf(g, "WhiteElo"), be = tagOf(g, "BlackElo");
        set(MergeField::Ratings, we.empty() && be.empty()
                                     ? std::string()
                                     : std::string(we.empty() ? "?" : we) + " – " + std::string(be.empty() ? "?" : be));
        set(MergeField::Eco, std::string(tagOf(g, "ECO")));
        const auto wf = tagOf(g, "WhiteFideId"), bf = tagOf(g, "BlackFideId");
        set(MergeField::FideIds, wf.empty() && bf.empty()
                                     ? std::string()
                                     : std::string(wf.empty() ? "?" : wf) + " – " + std::string(bf.empty() ? "?" : bf));
        set(MergeField::Moves, std::to_string(g.plies));
        set(MergeField::Clocks, known(g.clocks) ? std::to_string(known(g.clocks)) : std::string());
        set(MergeField::Evals, known(g.evals) ? std::to_string(known(g.evals)) : std::string());
        set(MergeField::Result, resultText(g.result));
        out.push_back(std::move(f));
    }
    return out;
}

MergeChoice fullestChoice(const std::vector<ImportedGame>& games) {
    MergeChoice choice{};
    if (games.empty()) return choice;
    const auto best = [&](MergeField field, auto score) {
        std::size_t pick = 0;
        auto top         = score(games[0]);
        for (std::size_t i = 1; i < games.size(); ++i) {
            const auto s = score(games[i]);
            if (s > top) {
                top  = s;
                pick = i;
            }
        }
        choice[static_cast<std::size_t>(field)] = static_cast<uint8_t>(pick);
    };
    const auto name = [](std::string_view v) {
        if (unknown(v)) return std::pair<int, std::size_t>{0, 0};
        const auto parts = nameParts(v);
        return std::pair<int, std::size_t>{givenInitialOnly(parts.given) || parts.given.empty() ? 1 : 2, lettersIn(v)};
    };
    const auto text = [](std::string_view v) { return unknown(v) ? std::size_t{0} : 1 + lettersIn(v); };
    best(MergeField::White, [&](const ImportedGame& g) { return name(tagOf(g, "White")); });
    best(MergeField::Black, [&](const ImportedGame& g) { return name(tagOf(g, "Black")); });
    best(MergeField::Event, [&](const ImportedGame& g) { return text(tagOf(g, "Event")); });
    // A place over an address: a broadcast's address is the mention of its source, kept as its GameURL.
    best(MergeField::Site, [&](const ImportedGame& g) {
        const auto site    = tagOf(g, "Site");
        const bool address = site.rfind("http", 0) == 0;
        return std::pair<int, std::size_t>{address ? 0 : 1, text(site)};
    });
    best(MergeField::Round, [&](const ImportedGame& g) {
        const auto parts = roundParts(tagOf(g, "Round"));
        return parts.size();
    });
    best(MergeField::Date, [&](const ImportedGame& g) {
        const auto d = packDate(tagOf(g, "Date"));
        return int{(d >> 9) != 0} + int{((d >> 5) & 15) != 0} + int{(d & 31) != 0};
    });
    best(MergeField::Ratings, [&](const ImportedGame& g) {
        return int{!tagOf(g, "WhiteElo").empty()} + int{!tagOf(g, "BlackElo").empty()};
    });
    best(MergeField::Eco, [&](const ImportedGame& g) { return int{!tagOf(g, "ECO").empty()}; });
    best(MergeField::FideIds, [&](const ImportedGame& g) {
        return int{!tagOf(g, "WhiteFideId").empty()} + int{!tagOf(g, "BlackFideId").empty()};
    });
    best(MergeField::Moves,
         [&](const ImportedGame& g) { return std::pair<uint32_t, int>{g.plies, g.text.empty() ? 0 : 1}; });
    best(MergeField::Clocks, [&](const ImportedGame& g) { return known(g.clocks); });
    best(MergeField::Evals, [&](const ImportedGame& g) { return known(g.evals); });
    // A result over none; among results, the longer game's (its end).
    best(MergeField::Result,
         [&](const ImportedGame& g) { return std::pair<int, uint32_t>{g.result == '*' ? 0 : 1, g.plies}; });
    return choice;
}

ImportedGame mergeGames(const std::vector<ImportedGame>& games, const MergeChoice& choice,
                        const std::vector<std::pair<GameId, std::string>>& origins) {
    const auto from = [&](MergeField field) -> const ImportedGame& {
        return games[std::min<std::size_t>(choice[static_cast<std::size_t>(field)], games.size() - 1)];
    };
    const auto& moves = from(MergeField::Moves);
    ImportedGame out  = moves;
    out.line          = 0;
    out.result        = from(MergeField::Result).result;
    out.clocks        = from(MergeField::Clocks).clocks;
    if (!out.clocks.empty()) out.clocks.resize(out.plies, -1);
    out.evals = from(MergeField::Evals).evals;
    if (!out.evals.empty()) out.evals.resize(out.plies, ImportedGame::NO_EVAL);

    // The tags: the fields' from the games chosen, in the seven tags' order first; then the others of every game.
    std::vector<std::pair<std::string, std::string>> tags;
    const auto add = [&](std::string_view name, MergeField field) {
        if (name == "Result") {
            tags.emplace_back("Result", resultText(out.result));
            return;
        }
        const auto v = tagOf(from(field), name);
        if (!v.empty()) tags.emplace_back(std::string(name), std::string(v));
    };
    add("Event", MergeField::Event);
    add("Site", MergeField::Site);
    add("Date", MergeField::Date);
    add("Round", MergeField::Round);
    add("White", MergeField::White);
    add("Black", MergeField::Black);
    add("Result", MergeField::Result);
    add("WhiteElo", MergeField::Ratings);
    add("BlackElo", MergeField::Ratings);
    add("ECO", MergeField::Eco);
    add("WhiteFideId", MergeField::FideIds);
    add("BlackFideId", MergeField::FideIds);
    for (const auto& [k, v] : moves.tags)  // the moves' own: their start, their variant
        if (fieldOfTag(k) == static_cast<int>(MergeField::Moves)) tags.emplace_back(k, v);
    const auto has = [&](std::string_view name) {
        return std::any_of(tags.begin(), tags.end(), [&](const auto& t) { return t.first == name; });
    };
    // A game's address in Site left for a place: kept, as its GameURL, unless the games give one.
    if (!has("GameURL"))
        for (const auto& g : games)
            if (const auto site = tagOf(g, "Site"); site.rfind("http", 0) == 0 &&
                                                    site != tagOf(from(MergeField::Site), "Site") &&
                                                    tagOf(g, "GameURL").empty()) {
                tags.emplace_back("GameURL", std::string(site));
                break;
            }
    std::vector<std::size_t> order(games.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_partition(order.begin(), order.end(), [&](std::size_t i) { return &games[i] == &moves; });
    for (const auto i : order)
        for (const auto& [k, v] : games[i].tags)
            if (fieldOfTag(k) < 0 && k != "MergedFrom" && !has(k)) tags.emplace_back(k, v);

    // Where each field came from, a field none of the games has left out.
    if (origins.size() == games.size()) {
        const auto values = mergeFields(games);
        std::string said;
        for (std::size_t i = 0; i < games.size(); ++i) {
            std::string fields;
            for (std::size_t f = 0; f < MERGE_FIELDS; ++f)
                if (choice[f] == i && !values[i][f].empty())
                    fields += (fields.empty() ? "" : ", ") + std::string(FIELD_NAMES[f]);
            const auto& [number, source] = origins[i];
            said += (said.empty() ? "" : "; ") + std::to_string(number + 1) + (source.empty() ? "" : " " + source) +
                    ": " + (fields.empty() ? std::string("nothing") : fields);
        }
        tags.emplace_back("MergedFrom", said);
    }
    out.tags = std::move(tags);
    if (!out.text.empty()) out.text = withTags(out.text, out.tags, resultText(out.result));
    return out;
}

std::string sourceLabel(std::string_view file, const ImportedGame& game) {
    if (!file.empty()) {
        const auto slash = file.find_last_of("/\\");
        const auto name  = file.substr(slash == std::string_view::npos ? 0 : slash + 1);
        std::string lower(name);
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.rfind("twic", 0) == 0) {
            std::string digits;
            for (std::size_t i = 4; i < lower.size() && std::isdigit(static_cast<unsigned char>(lower[i])); ++i)
                digits += lower[i];
            if (!digits.empty()) return "TWIC " + digits;
        }
        const auto month = [&](std::string_view prefix) -> std::string {
            if (lower.rfind(prefix, 0) != 0 || lower.size() < prefix.size() + 7) return {};
            return std::string(lower.substr(prefix.size(), 7));
        };
        if (const auto m = month("lichess_db_broadcast_"); !m.empty()) return "Lichess broadcasts " + m;
        if (const auto m = month("lichess_db_standard_rated_"); !m.empty()) return "Lichess " + m;
        if (lower.rfind("pasted-", 0) != 0) return std::string(name);
    }
    const auto site = tagOf(game, "Site"), url = tagOf(game, "GameURL");
    if (!tagOf(game, "BroadcastURL").empty() || !tagOf(game, "BroadcastName").empty() ||
        site.find("lichess.org/broadcast") != std::string_view::npos ||
        url.find("lichess.org/broadcast") != std::string_view::npos)
        return "Lichess broadcast";
    if (site.find("lichess.org") != std::string_view::npos) return "Lichess";
    if (site.find("chess.com") != std::string_view::npos) return "chess.com";
    return {};
}

}  // namespace fcg::study
