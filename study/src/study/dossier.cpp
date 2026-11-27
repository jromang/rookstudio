#include "dossier.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <tuple>

#include <chess.hpp>

#include "parallel.hpp"
#include "query.hpp"

namespace fcg::study {

// ---------------------------------------------------------------------------------------------------------------------
// The named openings

OpeningNames::OpeningNames(std::string_view tsv) {
    ScanBoard start;
    start.setFen(chess::constants::STARTPOS, false);
    bool header = true;
    for (std::size_t at = 0; at < tsv.size();) {
        auto end = tsv.find('\n', at);
        if (end == std::string_view::npos) end = tsv.size();
        auto line = tsv.substr(at, end - at);
        at        = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (std::exchange(header, false)) continue;
        const auto first  = line.find('\t');
        const auto second = first == std::string_view::npos ? first : line.find('\t', first + 1);
        if (second == std::string_view::npos) continue;
        auto pgn = line.substr(second + 1);
        if (const auto tab = pgn.find('\t'); tab != std::string_view::npos) pgn = pgn.substr(0, tab);
        NamedOpening o;
        o.eco       = std::string(line.substr(0, first));
        o.name      = std::string(line.substr(first + 1, second - first - 1));
        auto board  = start;
        bool played = true;
        for (std::size_t p = 0; p < pgn.size();) {
            while (p < pgn.size() && pgn[p] == ' ') ++p;
            auto q = pgn.find(' ', p);
            if (q == std::string_view::npos) q = pgn.size();
            const auto token = pgn.substr(p, q - p);
            p                = q;
            if (token.empty() || token.back() == '.') continue;
            const auto move = board.parseSan(token);
            if (!move) {
                played = false;
                break;
            }
            board.play(*move);
            o.moves.emplace_back(token);
        }
        if (!played || o.moves.empty()) continue;
        const auto index = static_cast<uint32_t>(entries_.size());
        by_key_.try_emplace(board.key(), index);
        const auto it = by_eco_.find(o.eco);
        if (it == by_eco_.end() || o.moves.size() < entries_[it->second].moves.size()) by_eco_[o.eco] = index;
        entries_.push_back(std::move(o));
    }
}

std::optional<std::size_t> OpeningNames::at(uint64_t key) const {
    const auto it = by_key_.find(key);
    if (it == by_key_.end()) return std::nullopt;
    return it->second;
}

std::optional<std::size_t> OpeningNames::shortest(std::string_view eco) const {
    const auto it = by_eco_.find(std::string(eco));
    if (it == by_eco_.end()) return std::nullopt;
    return it->second;
}

std::string_view openingFamily(std::string_view name) { return name.substr(0, name.find(',')); }

// ---------------------------------------------------------------------------------------------------------------------
// Who, and where

namespace {

std::string lowerAscii(std::string_view text) {
    std::string out(text);
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// Whether `name`, folded, is `folded`: its ASCII letters compared first, the accents folded only for a name that has
// some.
bool foldedIs(std::string_view name, std::string_view folded) {
    const bool ascii =
        std::all_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
    if (ascii) {
        if (name.size() != folded.size()) return false;
        for (std::size_t i = 0; i < name.size(); ++i) {
            char c = name[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != folded[i]) return false;
        }
        return true;
    }
    return foldText(name) == folded;
}

}  // namespace

DossierSource findSubject(std::string database, std::shared_ptr<const Snapshot> snapshot, const DossierSubject& subject,
                          bool online) {
    DossierSource out;
    out.name     = std::move(database);
    out.snapshot = std::move(snapshot);
    out.online   = online;
    if (!out.snapshot) return out;
    const auto& snap = *out.snapshot;
    if (online) {
        if (subject.lichess.empty()) return out;
        const auto account = lowerAscii(subject.lichess);
        for (uint32_t id = 0; id < snap.names(); ++id)
            if (lowerAscii(snap.name(id)) == account) out.spellings.push_back(id);
        return out;
    }
    const auto& table = *snap.players();
    const auto wanted = foldText(subject.name);
    std::vector<uint8_t> owned;  // the name ids of the table's players, whatever they are
    for (const auto& p : table.players()) {
        for (const auto id : p.spellings) {
            if (id >= owned.size()) owned.resize(id + 1, 0);
            owned[id] = 1;
        }
        const bool by_id   = subject.fide != 0 && p.fide == subject.fide;
        const bool by_name = !wanted.empty() && foldText(p.name) == wanted;
        if (by_id || (by_name && (subject.fide == 0 || p.fide == 0)))
            out.spellings.insert(out.spellings.end(), p.spellings.begin(), p.spellings.end());
        else if (by_name)
            out.doubtful.insert(out.doubtful.end(), p.spellings.begin(), p.spellings.end());
    }
    if (!wanted.empty())
        for (uint32_t id = 0; id < snap.names(); ++id)
            if ((id >= owned.size() || !owned[id]) && foldedIs(snap.name(id), wanted)) out.spellings.push_back(id);
    for (auto* v : {&out.spellings, &out.doubtful}) {
        std::sort(v->begin(), v->end());
        v->erase(std::unique(v->begin(), v->end()), v->end());
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// The games

namespace {

// "5400", "5400+30", "40/7200", "1/259200": the seconds of the first period, its increment, its moves (0 none).
struct Period {
    double base = 0, increment = 0;
    int moves = 0;
};

std::optional<Period> firstPeriod(std::string_view tc) {
    while (!tc.empty() && tc.front() == ' ') tc.remove_prefix(1);
    while (!tc.empty() && tc.back() == ' ') tc.remove_suffix(1);
    if (const auto colon = tc.find(':'); colon != std::string_view::npos) tc = tc.substr(0, colon);
    if (tc.empty() || tc == "?" || tc == "-") return std::nullopt;
    if (tc.front() == '*') tc.remove_prefix(1);  // a sandglass
    Period p;
    if (const auto slash = tc.find('/'); slash != std::string_view::npos) {
        p.moves = std::atoi(std::string(tc.substr(0, slash)).c_str());
        tc      = tc.substr(slash + 1);
    }
    const auto plus   = tc.find('+');
    const auto number = [](std::string_view s, double& v) {
        if (s.empty()) return false;
        char* end = nullptr;
        const std::string text(s);
        v = std::strtod(text.c_str(), &end);
        return end && *end == 0 && v >= 0;
    };
    if (!number(tc.substr(0, plus), p.base)) return std::nullopt;
    if (plus != std::string_view::npos && !number(tc.substr(plus + 1), p.increment)) return std::nullopt;
    return p;
}

}  // namespace

Speed gameSpeed(std::string_view time_control, std::string_view event, bool online) {
    const auto tc = time_control;
    if (online && (tc == "-" || tc == "–")) return Speed::Correspondence;
    if (const auto p = firstPeriod(tc)) {
        if (p->moves == 1 && p->base >= 86400) return Speed::Correspondence;  // days a move
        if (online) {
            const double seconds = p->base + 40 * p->increment;
            return seconds < 180    ? Speed::Bullet
                   : seconds < 480  ? Speed::Blitz
                   : seconds < 1500 ? Speed::Rapid
                                    : Speed::Classical;
        }
        const double seconds = p->base + 60 * p->increment;
        return seconds <= 600 ? Speed::Blitz : seconds < 3600 ? Speed::Rapid : Speed::Classical;
    }
    const auto words = foldText(event);
    const auto has   = [&](std::string_view w) { return words.find(w) != std::string::npos; };
    if (has("bullet")) return Speed::Bullet;
    if (has("blitz")) return Speed::Blitz;
    if (has("rapid") || has("rapido") || has("schnell")) return Speed::Rapid;
    if (has("correspondence") || has("iccf")) return Speed::Correspondence;
    if (has("classical") || has("standard")) return Speed::Classical;
    return online ? Speed::Unknown : Speed::Classical;
}

std::optional<double> DossierGame::points() const noexcept {
    switch (numbers.result) {
        case '1':
            return white ? 1.0 : 0.0;
        case '0':
            return white ? 0.0 : 1.0;
        case '=':
            return 0.5;
        default:
            return std::nullopt;
    }
}

void DossierScore::add(const DossierGame& game) noexcept {
    ++games;
    if (const auto p = game.points()) {
        if (*p == 1.0)
            ++won;
        else if (*p == 0.0)
            ++lost;
        else
            ++drawn;
    }
}

double DossierScore::share() const noexcept {
    const auto n = decided();
    return n ? (won + drawn * 0.5) / n : 0.0;
}

namespace {

// The plies of each game read for its opening, and the ply of its structure (move 20).
constexpr uint32_t OPENING_PLIES  = 40;
constexpr uint32_t STRUCTURE_PLY  = 40;
constexpr uint64_t SCAN_PART      = 16384;
constexpr std::size_t REPLAY_PART = 64;

struct Found {
    uint32_t source = 0;
    GameId id       = 0;
    bool white      = true;
    bool doubtful   = false;
};

}  // namespace

Dossier makeDossier(std::vector<DossierSource> sources, const DossierSubject& subject, const DossierOptions& options,
                    DossierProgress& progress, const std::atomic<bool>& cancel) {
    const auto started = std::chrono::steady_clock::now();
    Dossier out;
    out.sources  = std::move(sources);
    out.openings = options.openings;
    const int threads = threadCount(options.threads);
    uint64_t total = 0;
    for (const auto& s : out.sources)
        if (s.snapshot) total += s.snapshot->games();
    progress.total     = total + 1;
    progress.done      = 0;
    const auto stopped = [&] { return cancel.load(std::memory_order_relaxed); };

    // Their games: every game of each snapshot read for its players.
    std::vector<Found> found;
    for (uint32_t s = 0; s < out.sources.size() && !stopped(); ++s) {
        const auto& source = out.sources[s];
        if (!source.snapshot) continue;
        const auto& snap = *source.snapshot;
        if (source.spellings.empty() && source.doubtful.empty()) {
            progress.done += snap.games();
            continue;
        }
        std::vector<uint8_t> mark(snap.names() + 1, 0);  // 1 theirs, 2 a namesake's
        for (const auto id : source.spellings)
            if (id < mark.size()) mark[id] = 1;
        for (const auto id : source.doubtful)
            if (id < mark.size() && !mark[id]) mark[id] = 2;
        const auto games = snap.games();
        std::atomic<uint64_t> next{0};
        std::mutex merge;
        onThreads(static_cast<int>(std::min<uint64_t>(threads, games / SCAN_PART + 1)), [&](int) {
            std::vector<Found> mine;
            for (;;) {
                const auto from = next.fetch_add(SCAN_PART);
                if (from >= games || stopped()) break;
                const auto to = std::min(games, from + SCAN_PART);
                for (GameId id = from; id < to; ++id) {
                    if (snap.deleted(id)) continue;
                    const auto ids = snap.rowIds(id);
                    const auto w   = ids.white < mark.size() ? mark[ids.white] : 0;
                    const auto b   = ids.black < mark.size() ? mark[ids.black] : 0;
                    // A game whose two players are theirs is no game of one player: two namesakes, or a slip.
                    if ((w != 0) == (b != 0)) continue;
                    mine.push_back({s, id, w != 0, (w ? w : b) == 2});
                }
                progress.done += to - from;
            }
            const std::lock_guard lock(merge);
            found.insert(found.end(), mine.begin(), mine.end());
        });
    }
    if (stopped()) {
        out.cancelled = true;
        return out;
    }
    std::sort(found.begin(), found.end(),
              [](const Found& a, const Found& b) { return a.source != b.source ? a.source < b.source : a.id < b.id; });
    progress.total = total + found.size() + 1;

    // Each of theirs read and replayed: its numbers, players, opening, structure and the positions of its tree.
    std::vector<DossierGame> read(found.size());
    std::vector<uint8_t> kept(found.size(), 0);
    std::array<std::vector<Dossier::Reach>, 2> reach;
    std::mutex merge;
    std::atomic<std::size_t> next{0};
    const uint32_t replay_plies = std::max({options.tree_plies, OPENING_PLIES, STRUCTURE_PLY});
    onThreads(static_cast<int>(std::min<std::size_t>(threads, found.size() / REPLAY_PART + 1)), [&](int) {
        std::array<std::vector<Dossier::Reach>, 2> mine;
        std::vector<uint64_t> seen;
        for (;;) {
            const auto from = next.fetch_add(REPLAY_PART);
            if (from >= found.size() || stopped()) break;
            const auto to = std::min(found.size(), from + REPLAY_PART);
            for (std::size_t i = from; i < to; ++i) {
                const auto& f      = found[i];
                const auto& source = out.sources[f.source];
                const auto& snap   = *source.snapshot;
                auto& g            = read[i];
                g.source           = f.source;
                g.id               = f.id;
                g.white            = f.white;
                g.online           = source.online;
                g.numbers          = snap.numbers(f.id);
                // A namesake's game: the FIDE id it gives for their side says whose it is.
                if (!source.online && (f.doubtful || subject.fide != 0 || subject.born != 0)) {
                    int64_t tag = 0;
                    if (f.doubtful || subject.fide != 0)
                        tag =
                            std::strtoll(snap.tag(f.id, f.white ? "WhiteFideId" : "BlackFideId").c_str(), nullptr, 10);
                    const bool given = subject.fide != 0 && tag == subject.fide;
                    if (f.doubtful && !given) continue;
                    if (subject.fide != 0 && tag > 0 && !given) continue;
                    const int year = static_cast<int>(g.numbers.date >> 9);
                    if (subject.born != 0 && year > 0 && year < subject.born + 5 && !given) continue;
                }
                const auto row = snap.row(f.id);
                const auto ids = snap.rowIds(f.id);
                g.speed        = gameSpeed(row.time_control, row.event, source.online);
                g.opponent     = f.white ? ids.black : ids.white;
                if (options.user) g.user = options.user(snap.name(g.opponent));
                // Its moves: the tree's plies, its opening and its structure.
                const auto moves = snap.gameMoves(f.id);
                ScanBoard board;
                if (startOf(snap, f.id, moves.flags, board)) {
                    const bool standard = !(moves.flags & (GAME_FEN | GAME_CHESS960));
                    auto& tree          = mine[f.white ? 0 : 1];
                    seen.clear();
                    Cursor cursor(moves.codes, moves.plies);
                    for (uint32_t ply = 0;; ++ply) {
                        const uint64_t key = board.key();
                        if (ply == STRUCTURE_PLY) {  // reached: the game went on to it
                            g.at_move_20 = true;
                            g.pawns      = {board.pieces(WHITE_SIDE, PAWN), board.pieces(BLACK_SIDE, PAWN)};
                        }
                        if (standard && options.openings && ply > 0 && ply <= OPENING_PLIES)
                            if (const auto named = options.openings->at(key)) g.opening = static_cast<int>(*named);
                        // The next move: none at the game's end; a code that cannot be read ends it there too, its
                        // position kept as the end's is.
                        static_assert(Cursor::END == TreeMove::END);
                        const int code = cursor.next(board);
                        if (ply == 0 && code != TreeMove::END) g.first_move = code;
                        // The tree: the positions of its first plies, each where the game first reaches it, with the
                        // move it played there (none at its end); a position of more than 256 codes is not followed.
                        const bool in_tree =
                            ply < options.tree_plies || (ply == options.tree_plies && code == TreeMove::END);
                        if (in_tree && board.codeCount() <= 256 &&
                            std::find(seen.begin(), seen.end(), key) == seen.end()) {
                            seen.push_back(key);
                            tree.push_back({key, static_cast<int16_t>(code), static_cast<uint32_t>(i)});
                        }
                        if (code == TreeMove::END || ply >= replay_plies) break;
                        board.play(board.decode(code));
                    }
                }
                kept[i] = 1;
            }
            progress.done += to - from;
        }
        const std::lock_guard lock(merge);
        for (int side = 0; side < 2; ++side)
            reach[side].insert(reach[side].end(), mine[side].begin(), mine[side].end());
    });
    if (stopped()) {
        out.cancelled = true;
        return out;
    }

    // The latest first; the tree's games numbered so.
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < read.size(); ++i)
        if (kept[i]) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const auto& x = read[a];
        const auto& y = read[b];
        if (x.numbers.date != y.numbers.date) return x.numbers.date > y.numbers.date;
        if (x.source != y.source) return x.source < y.source;
        return x.id > y.id;
    });
    std::vector<uint32_t> place(read.size(), UINT32_MAX);
    out.games.reserve(order.size());
    for (const auto i : order) {
        place[i] = static_cast<uint32_t>(out.games.size());
        out.games.push_back(read[i]);
    }
    for (int side = 0; side < 2; ++side) {
        auto& r = reach[side];
        std::erase_if(r, [&](const Dossier::Reach& x) { return place[x.game] == UINT32_MAX; });
        for (auto& x : r) x.game = place[x.game];
        std::sort(r.begin(), r.end(), [](const Dossier::Reach& a, const Dossier::Reach& b) {
            return a.key != b.key ? a.key < b.key : a.game < b.game;
        });
        out.reach[side] = std::move(r);
    }
    progress.done = progress.total.load();
    out.seconds   = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return out;
}

std::optional<TreeAnswer> Dossier::at(uint64_t key, bool white, DossierParts parts) const {
    const auto& r  = reach[white ? 0 : 1];
    const auto low = std::lower_bound(r.begin(), r.end(), key, [](const Reach& x, uint64_t k) { return x.key < k; });
    TreeAnswer out;
    std::map<int, TreeStats> by_code;
    for (auto it = low; it != r.end() && it->key == key; ++it) {
        const auto& g = games[it->game];
        if (!of(g, parts)) continue;
        out.total.add(g.numbers);
        by_code[it->code].add(g.numbers);
    }
    if (out.total.games == 0) return std::nullopt;
    for (const auto& [code, stats] : by_code) out.moves.push_back({code, stats});
    std::stable_sort(out.moves.begin(), out.moves.end(), [](const TreeMove& a, const TreeMove& b) {
        if ((a.code == TreeMove::END) != (b.code == TreeMove::END)) return b.code == TreeMove::END;
        return a.stats.games > b.stats.games;
    });
    return out;
}

MovesAt Dossier::moves(bool white, DossierParts parts) const {
    return [this, white, parts](const ScanBoard& position) { return at(position.key(), white, parts); };
}

// ---------------------------------------------------------------------------------------------------------------------
// What it says

DossierFigures dossierFigures(const Dossier& dossier, DossierParts parts, const StructureNamer& structures) {
    DossierFigures f;
    f.sources.resize(dossier.sources.size());
    std::map<std::pair<bool, std::string>, DossierLine> lines;
    struct YearSum {
        double classical = 0, all = 0;
        uint32_t classical_n = 0, all_n = 0;
    };
    std::map<int, YearSum> years;
    const bool any_classical = std::any_of(dossier.games.begin(), dossier.games.end(), [&](const DossierGame& g) {
        return Dossier::of(g, parts) && g.speed == Speed::Classical && g.ownElo() > 0;
    });
    std::map<std::tuple<int, bool, bool>, DossierStructure> shapes;
    std::vector<std::pair<int, int>> named;
    for (std::size_t i = 0; i < dossier.games.size(); ++i) {
        const auto& g = dossier.games[i];
        if (!Dossier::of(g, parts)) continue;
        const auto at = static_cast<uint32_t>(i);
        ++f.games;
        const auto date = g.numbers.date;
        const int year  = static_cast<int>(date >> 9);
        if (date != 0) {
            f.first = f.first == 0 ? date : std::min(f.first, date);
            f.last  = std::max(f.last, date);
        }
        f.all.add(g);
        (g.white ? f.white : f.black).add(g);
        const int own = g.ownElo(), opponent = g.opponentElo();
        if (own > 0 && opponent > 0) {
            if (opponent > own) f.stronger.add(g);
            if (opponent < own) f.weaker.add(g);
        }
        if (g.user) {
            f.user.add(g);
            if (!f.last_user) f.last_user = i;
        }
        ++f.speeds[static_cast<std::size_t>(g.speed)];
        if (g.source < f.sources.size()) {
            auto& s = f.sources[g.source];
            ++s.games;
            if (year > 0) {
                s.first_year = s.first_year ? std::min(s.first_year, year) : year;
                s.last_year  = std::max(s.last_year, year);
            }
        }
        if (g.opening >= 0 && dossier.openings && static_cast<std::size_t>(g.opening) < dossier.openings->size()) {
            const auto& eco = (*dossier.openings)[static_cast<std::size_t>(g.opening)].eco;
            auto& line      = lines[{g.white, eco}];
            if (line.games.empty()) {
                line.eco     = eco;
                line.opening = dossier.openings->shortest(eco);
            }
            line.score.add(g);
            line.last = std::max(line.last, date);
            line.games.push_back(at);
        }
        if (own > 0 && year > 0) {
            auto& y = years[year];
            y.all += own;
            ++y.all_n;
            if (g.speed == Speed::Classical) {
                y.classical += own;
                ++y.classical_n;
            }
        }
        if (own > 0 && (!any_classical || g.speed == Speed::Classical) && own > f.highest) {
            f.highest      = own;
            f.highest_date = date;
        }
        if (structures && g.at_move_20) {
            named.clear();
            structures(g.pawns[0], g.pawns[1], named);
            std::set<std::tuple<int, bool, bool>> once;  // a structure counted once a game
            for (const auto& [kind, owner] : named) {
                const bool theirs = owner == (g.white ? 0 : 1);
                const auto k      = std::tuple{kind, theirs, g.white};
                if (!once.insert(k).second) continue;
                auto& s  = shapes[k];
                s.kind   = kind;
                s.theirs = theirs;
                s.white  = g.white;
                s.score.add(g);
                s.games.push_back(at);
            }
        }
    }
    for (auto& [key, line] : lines) (key.first ? f.lines_white : f.lines_black).push_back(std::move(line));
    for (auto* v : {&f.lines_white, &f.lines_black})
        std::stable_sort(v->begin(), v->end(),
                         [](const DossierLine& a, const DossierLine& b) { return a.score.games > b.score.games; });
    for (const auto& [year, y] : years) {
        DossierYear d;
        d.year  = year;
        d.rated = y.classical_n ? y.classical_n : y.all_n;
        d.elo   = static_cast<int>(std::lround(y.classical_n ? y.classical / y.classical_n : y.all / y.all_n));
        f.years.push_back(d);
    }
    for (auto& [key, s] : shapes) f.structures.push_back(std::move(s));
    std::stable_sort(
        f.structures.begin(), f.structures.end(),
        [](const DossierStructure& a, const DossierStructure& b) { return a.score.games > b.score.games; });
    return f;
}

std::vector<DossierFamily> dossierFamilies(const Dossier& dossier, DossierParts parts, bool white, int first_move,
                                           const OpeningNames& openings) {
    std::map<std::string, DossierScore> by;
    for (const auto& g : dossier.games) {
        if (!Dossier::of(g, parts) || g.white != white || g.opening < 0 ||
            static_cast<std::size_t>(g.opening) >= openings.size())
            continue;
        if (first_move >= 0 && g.first_move != first_move) continue;
        by[std::string(openingFamily(openings[static_cast<std::size_t>(g.opening)].name))].add(g);
    }
    std::vector<DossierFamily> out;
    for (auto& [family, score] : by) out.push_back({family, score});
    std::stable_sort(out.begin(), out.end(),
                     [](const DossierFamily& a, const DossierFamily& b) { return a.score.games > b.score.games; });
    return out;
}

}  // namespace fcg::study
