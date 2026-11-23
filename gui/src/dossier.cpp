#include "dossier.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <unordered_set>

#include <fmt/include/fmt/format.h>

#include <chess.hpp>
#include <commentary/formation.hpp>
#include <commentary/openings.hpp>
#include <study/import.hpp>
#include <study/names.hpp>
#include <study/query.hpp>
#include <study/scan_board.hpp>

#include "board_text.hpp"
#include "game_analysis.hpp"
#include "game_graph.hpp"
#include "mistakes.hpp"
#include "review_stats.hpp"

namespace fcg {

namespace fs = std::filesystem;

const study::StructureNamer& dossierStructures() {
    static const study::StructureNamer namer = [](uint64_t white, uint64_t black,
                                                  std::vector<std::pair<int, int>>& out) {
        auto board = commentary::Board::empty();
        for (const auto& [pawns, colour] :
             {std::pair{white, commentary::Color::White}, std::pair{black, commentary::Color::Black}})
            for (auto bits = pawns; bits; bits &= bits - 1)
                board.setPieceAt(commentary::square(static_cast<uint32_t>(std::countr_zero(bits))),
                                 {colour, commentary::Role::Pawn});
        for (const auto& [formation, owner] : commentary::formations(board))
            out.emplace_back(static_cast<int>(formation), owner == commentary::Color::White ? 0 : 1);
    };
    return namer;
}

const study::OpeningNames& dossierOpenings() {
    static const study::OpeningNames names(commentary::openingsTable());
    return names;
}

namespace {

// A game's review key (reviewKey(): its start position and its moves) from its codes, without reading its PGN: none
// for a game that starts from a position of its own or whose moves cannot be read.
std::optional<std::string> reviewKeyOf(const study::Snapshot& snap, study::GameId id) {
    const auto moves = snap.gameMoves(id);
    if (moves.flags & (study::GAME_FEN | study::GAME_CHESS960)) return std::nullopt;
    uint64_t hash   = 1469598103934665603ULL;
    const auto feed = [&](std::string_view text) {
        for (const char c : text) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ULL;
        }
    };
    feed(chess::constants::STARTPOS);
    chess::Board board;
    study::ScanBoard scan;
    if (!scan.set(board)) return std::nullopt;
    std::size_t p = 0;
    for (uint32_t ply = 0; ply < moves.plies; ++ply) {
        if (p >= moves.codes.size()) return std::nullopt;
        const int count = scan.codeCount();
        int code        = moves.codes[p++];
        if (count > 256) {
            if (p >= moves.codes.size()) return std::nullopt;
            code = (code << 8) | moves.codes[p++];
        }
        if (code >= count) return std::nullopt;
        const auto move = study::moveOfCode(board, scan, code);
        if (!move) return std::nullopt;
        feed(" ");
        feed(chess::uci::moveToUci(*move, true));
        board.makeMove(*move);
        scan.play(scan.decode(code));
    }
    return fmt::format("{:016x}", hash);
}

}  // namespace

DossierReviews dossierReviews(const study::Dossier& dossier, const fs::path& analyses,
                              const std::atomic<bool>& cancel) {
    DossierReviews out;
    std::unordered_set<std::string> reviewed;
    {
        std::error_code ec;
        for (fs::directory_iterator it(analyses, ec), end; !ec && it != end; it.increment(ec)) {
            const auto& p = it->path();
            if (p.extension() == ".json" && p.stem().string().size() == 16) reviewed.insert(p.stem().string());
        }
    }
    if (reviewed.empty()) return out;
    std::array<double, 3> sums{};
    std::array<uint32_t, 3> counts{};
    std::unordered_set<std::string> seen;  // a game in two databases counted once
    for (const auto& g : dossier.games) {
        if (cancel.load(std::memory_order_relaxed)) break;
        const auto& snap = *dossier.sources[g.source].snapshot;
        const auto key   = reviewKeyOf(snap, g.id);
        if (!key || !reviewed.contains(*key) || !seen.insert(*key).second) continue;
        std::string error;
        const auto game = readPgnGame(study::importedPgn(snap.game(g.id)), error);
        if (!game || game->moves.empty()) continue;
        const auto review = loadReview(analyses, *game);
        if (!review) continue;
        ++out.games;
        const auto judged = reviewMoves(*game, false, *review);
        const auto phases = gamePhases(*game, false);
        const auto stats  = gameStats(*game, false, *review, judged, phases);
        const auto& mine  = stats[g.white ? 0 : 1];
        for (std::size_t p = 0; p < 3; ++p)
            if (mine.phase_accuracy[p]) {
                sums[p] += *mine.phase_accuracy[p];
                ++counts[p];
            }
        const auto colour = g.white ? chess::Color::WHITE : chess::Color::BLACK;
        for (std::size_t i = 0; i < judged.size() && i < game->moves.size(); ++i)
            if (game->moves[i].mover == colour &&
                (judged[i].judgement == Judgement::Mistake || judged[i].judgement == Judgement::Blunder))
                ++out.mistakes[static_cast<std::size_t>(phaseAt(phases, static_cast<int>(i)))];
    }
    for (std::size_t p = 0; p < 3; ++p)
        if (counts[p]) out.accuracy[p] = sums[p] / counts[p];
    return out;
}

namespace {

DossierNamesake namesakeOf(const study::Player& p) {
    DossierNamesake n;
    n.fide       = p.fide;
    n.name       = p.name;
    n.federation = p.federation;
    n.title      = p.title;
    n.born       = p.born;
    n.standard   = p.standard;
    n.rapid      = p.rapid;
    n.blitz      = p.blitz;
    n.list       = p.list;
    return n;
}

DossierNamesake namesakeOf(const study::FidePlayer& p, const std::string& month) {
    DossierNamesake n;
    n.fide       = p.id;
    n.name       = std::string(p.name);
    n.federation = p.federation;
    n.title      = p.title;
    n.born       = p.born;
    n.standard   = p.standard;
    n.rapid      = p.rapid;
    n.blitz      = p.blitz;
    n.list       = month;
    return n;
}

std::string lowerAscii(std::string text) {
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

// The spellings written as `name` that no player with a FIDE id holds: those a choice of namesake brings together.
std::vector<uint32_t> freeSpellings(const study::Snapshot& snap, const std::string& name) {
    const auto& table = *snap.players();
    const auto wanted = study::foldText(name);
    std::vector<uint32_t> out;
    std::vector<uint8_t> owned;
    for (const auto& p : table.players()) {
        for (const auto id : p.spellings) {
            if (id >= owned.size()) owned.resize(id + 1, 0);
            owned[id] = 1;
        }
        if (p.fide == 0 && study::foldText(p.name) == wanted)
            out.insert(out.end(), p.spellings.begin(), p.spellings.end());
    }
    for (uint32_t id = 0; id < snap.names(); ++id)
        if ((id >= owned.size() || !owned[id]) && study::foldText(snap.name(id)) == wanted) out.push_back(id);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// What they play, from their games' tree: their first moves with White, their answers with Black to the two first
// moves they met most.
DossierPlay playOf(const study::Dossier& dossier, study::DossierParts parts) {
    DossierPlay out;
    const chess::Board start;
    study::ScanBoard scan;
    if (!scan.set(start)) return out;
    if (const auto white = dossier.at(scan.key(), true, parts)) {
        for (const auto& m : white->moves) {
            if (m.code == study::TreeMove::END || out.with_white.size() >= 4) continue;
            const auto move = study::moveOfCode(start, scan, m.code);
            if (!move) continue;
            DossierFirst f;
            f.san           = chess::uci::moveToSan(start, *move);
            f.games         = m.stats.games;
            f.share         = static_cast<double>(m.stats.games) / static_cast<double>(white->total.games);
            const auto done = m.stats.white + m.stats.draws + m.stats.black;
            f.score         = done ? (m.stats.white + m.stats.draws * 0.5) / static_cast<double>(done) : 0;
            out.with_white.push_back(std::move(f));
        }
    }
    if (const auto black = dossier.at(scan.key(), false, parts)) {
        for (const auto& m : black->moves) {
            if (m.code == study::TreeMove::END || out.with_black.size() >= 2) continue;
            const auto move = study::moveOfCode(start, scan, m.code);
            if (!move) continue;
            DossierAgainst a;
            a.san      = chess::uci::moveToSan(start, *move);
            a.games    = m.stats.games;
            a.families = study::dossierFamilies(dossier, parts, false, m.code, dossierOpenings());
            if (a.families.size() > 3) a.families.resize(3);
            out.with_black.push_back(std::move(a));
        }
    }
    return out;
}

// The user's repertoires against their games of the other colour: the lines they play that have no answer (#52's
// measure: three games and a twentieth of the position's at least), the most met first.
std::vector<DossierGap> gapsOf(const study::Dossier& dossier, study::DossierParts parts,
                               const std::vector<std::pair<std::string, study::Repertoire>>& repertoires) {
    std::vector<DossierGap> out;
    study::CoverageOptions options;
    options.least_games = 3;
    options.least_share = 0.05;
    options.listed      = 20;
    for (const auto& [name, repertoire] : repertoires) {
        const bool white = repertoire.side() == chess::Color::WHITE;
        for (auto& gap : study::coverage(repertoire, dossier.moves(!white, parts), options).gaps)
            out.push_back({name, std::move(gap), white});
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const DossierGap& a, const DossierGap& b) { return a.gap.games > b.gap.games; });
    if (out.size() > 8) out.resize(8);
    return out;
}

}  // namespace

DossierReport makeDossierReport(DossierAsk ask, study::DossierProgress& progress, const std::atomic<bool>& cancel) {
    const auto started = std::chrono::steady_clock::now();
    DossierReport out;
    auto subject = ask.subject;

    // A choice kept first, in the tables of names: put back, a namesake chosen, an account linked; each database's
    // table before the first change kept for Ctrl+Z.
    const auto remember = [&](const fs::path& folder, const study::PlayerTable& table) {
        if (std::none_of(out.tables_before.begin(), out.tables_before.end(),
                         [&](const auto& t) { return t.first == folder; }))
            out.tables_before.emplace_back(folder, table);
    };
    try {
        for (const auto& [folder, table] : ask.restore)
            for (const auto& base : ask.bases)
                if (base.folder == folder && base.db) base.db->setPlayers(table);
        if (ask.keep_namesake) {
            const auto& n = *ask.keep_namesake;
            for (const auto& base : ask.bases) {
                if (!base.db) continue;
                const auto snap      = base.db->snapshot();
                const auto spellings = freeSpellings(*snap, subject.name);
                if (spellings.empty()) continue;
                remember(base.folder, *snap->players());
                base.db->setPlayers(study::withPlayer(*snap->players(), spellings, n.name, [&](study::Player& p) {
                    p.name       = n.name;
                    p.fide       = n.fide;
                    p.federation = n.federation;
                    p.title      = n.title;
                    p.born       = n.born;
                    p.standard   = n.standard;
                    p.rapid      = n.rapid;
                    p.blitz      = n.blitz;
                    p.list       = n.list;
                    p.how        = "hand";
                }));
            }
            subject.fide = n.fide;
            subject.born = n.born;
            subject.name = n.name;
        }
        if (!ask.link_lichess.empty()) {
            for (const auto& base : ask.bases) {
                if (!base.db) continue;
                const auto snap  = base.db->snapshot();
                const auto found = study::findSubject(base.name, snap, subject);
                if (found.spellings.empty()) continue;
                remember(base.folder, *snap->players());
                base.db->setPlayers(study::withPlayer(*snap->players(), found.spellings, subject.name,
                                                      [&](study::Player& p) { p.lichess = ask.link_lichess; }));
            }
            subject.lichess = ask.link_lichess;
        }
    } catch (const std::exception& e) {
        out.error = e.what();
    }

    // Who they are: a player of the tables of their name (or of their Lichess account) and their FIDE identity.
    std::vector<std::shared_ptr<const study::Snapshot>> snapshots;
    for (const auto& base : ask.bases) snapshots.push_back(base.db ? base.db->snapshot() : nullptr);
    const auto from_lichess = !subject.lichess.empty() && lowerAscii(subject.name) == lowerAscii(subject.lichess);
    if (from_lichess) {
        for (const auto& snap : snapshots) {
            if (!snap) continue;
            for (const auto& p : snap->players()->players())
                if (!p.lichess.empty() && lowerAscii(p.lichess) == lowerAscii(subject.lichess)) {
                    subject.name = p.name;
                    if (p.fide) subject.fide = p.fide;
                    if (p.born) subject.born = p.born;
                    break;
                }
            if (lowerAscii(subject.name) != lowerAscii(subject.lichess)) break;
        }
    }
    std::map<int64_t, study::Player> fide_players;  // the tables' players of that name with a FIDE id
    {
        const auto wanted = study::foldText(subject.name);
        for (const auto& snap : snapshots) {
            if (!snap) continue;
            for (const auto& p : snap->players()->players())
                if (p.fide != 0 && (p.fide == subject.fide || study::foldText(p.name) == wanted))
                    fide_players.try_emplace(p.fide, p);
        }
    }
    if (subject.fide == 0 && fide_players.size() == 1) {
        subject.fide = fide_players.begin()->first;
        if (!subject.born) subject.born = fide_players.begin()->second.born;
    }
    if (subject.fide == 0 && fide_players.size() > 1)
        for (const auto& [fide, p] : fide_players) out.namesakes.push_back(namesakeOf(p));
    if (subject.fide != 0) {
        if (const auto it = fide_players.find(subject.fide); it != fide_players.end())
            out.identity = namesakeOf(it->second);
        else if (ask.fide_list)
            if (const auto at = ask.fide_list->byId(static_cast<uint32_t>(subject.fide)))
                out.identity = namesakeOf(ask.fide_list->player(*at), ask.fide_list->info().month);
        if (out.identity && !subject.born) subject.born = out.identity->born;
    }

    // Their games, and what they say.
    study::DossierOptions options;
    options.openings = &dossierOpenings();
    options.threads  = ask.threads;
    options.user     = [names = ask.user_names](std::string_view name) {
        return !names.empty() && isUserName(name, names);
    };
    const auto make = [&]() {
        std::vector<study::DossierSource> sources;
        for (std::size_t i = 0; i < ask.bases.size(); ++i)
            sources.push_back(study::findSubject(ask.bases[i].name, snapshots[i], subject));
        // Their Lichess account, linked in a table, when it was not given.
        if (subject.lichess.empty())
            for (const auto& s : sources) {
                for (const auto id : s.spellings)
                    if (const auto* p = s.snapshot->players()->of(id); p && !p->lichess.empty()) {
                        subject.lichess = p->lichess;
                        break;
                    }
                if (!subject.lichess.empty()) break;
            }
        if (ask.online && ask.online->db && !subject.lichess.empty())
            sources.push_back(study::findSubject(ask.online->name, ask.online->db->snapshot(), subject, true));
        return study::makeDossier(std::move(sources), subject, options, progress, cancel);
    };
    auto dossier = make();
    // No FIDE id yet: FIDE's list says whether one player has their name (theirs then), or several (to choose).
    if (!dossier.cancelled && subject.fide == 0 && out.namesakes.empty() && ask.fide_list) {
        const auto figures = study::dossierFigures(dossier, study::DossierParts::Board);
        double sum = 0, rated = 0;
        for (const auto& g : dossier.games)
            if (!g.online && g.ownElo() > 0) {
                sum += g.ownElo();
                ++rated;
            }
        const auto found = study::fideNamesakes(*ask.fide_list, subject.name, static_cast<int>(figures.first >> 9),
                                                rated > 0 ? sum / rated : 0);
        const auto month = ask.fide_list->info().month;
        if (found.size() == 1) {
            const auto card = ask.fide_list->player(found.front());
            out.identity    = namesakeOf(card, month);
            subject.fide    = card.id;
            subject.born    = card.born;
            dossier         = make();
        } else {
            for (const auto c : found) out.namesakes.push_back(namesakeOf(ask.fide_list->player(c), month));
        }
    }
    out.cancelled = dossier.cancelled;
    out.subject   = subject;
    if (out.cancelled) return out;
    {  // their spellings, each written once whatever the databases that have it
        std::set<std::string> spelled;
        for (const auto& s : dossier.sources)
            if (!s.online)
                for (const auto id : s.spellings) spelled.insert(study::foldText(s.snapshot->name(id)));
        out.spellings = static_cast<uint32_t>(spelled.size());
    }
    auto shared    = std::make_shared<const study::Dossier>(std::move(dossier));
    out.dossier    = shared;
    out.board      = study::dossierFigures(*shared, study::DossierParts::Board, dossierStructures());
    out.board_play = playOf(*shared, study::DossierParts::Board);
    out.gaps       = gapsOf(*shared, study::DossierParts::Board, ask.repertoires);
    if (ask.online) {
        out.online      = study::dossierFigures(*shared, study::DossierParts::Online);
        out.online_play = playOf(*shared, study::DossierParts::Online);
        out.gaps_both   = gapsOf(*shared, study::DossierParts::Both, ask.repertoires);
    }
    out.reviews = dossierReviews(*shared, ask.analyses, cancel);
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return out;
}

std::vector<DossierName> dossierNames(const std::vector<std::shared_ptr<const study::Snapshot>>& snapshots,
                                      const std::string& words, std::size_t most, const std::atomic<bool>& cancel) {
    std::vector<std::string> wanted;
    {
        const auto folded = study::foldText(words);
        std::string word;
        for (const char c : folded + " ") {
            if (c == ' ' || c == ',' || c == '.') {
                if (!word.empty()) wanted.push_back(std::move(word));
                word.clear();
            } else {
                word += c;
            }
        }
    }
    if (wanted.empty()) return {};
    std::map<std::string, std::pair<std::string, bool>> found;  // folded -> shown, a player of a table
    for (const auto& snap : snapshots) {
        if (!snap || cancel.load(std::memory_order_relaxed)) continue;
        const auto& table = *snap->players();
        for (uint32_t id = 0; id < snap->names(); ++id) {
            if ((id & 0xFFF) == 0 && cancel.load(std::memory_order_relaxed)) break;
            const auto shown = snap->shownName(id);
            if (shown.size() < wanted.front().size()) continue;
            const auto folded = study::foldText(shown);
            if (!std::all_of(wanted.begin(), wanted.end(),
                             [&](const std::string& w) { return folded.find(w) != std::string::npos; }))
                continue;
            if (study::anonymousName(shown)) continue;
            auto& f = found[folded];
            if (f.first.empty()) f.first = std::string(shown);
            f.second = f.second || table.of(id) != nullptr;
        }
    }
    std::vector<std::pair<std::string, bool>> all;
    for (auto& [folded, f] : found) all.push_back(std::move(f));
    std::stable_sort(all.begin(), all.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second;
        return a.first.size() < b.first.size();
    });
    std::vector<DossierName> out;
    for (auto& [name, player] : all) {
        if (out.size() >= most) break;
        out.push_back({std::move(name)});
    }
    return out;
}

}  // namespace fcg
