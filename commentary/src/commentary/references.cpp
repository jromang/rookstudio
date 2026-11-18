#include "references.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string_view>

#include <fmt/include/fmt/format.h>

#include <study/scan_board.hpp>
#include <study/search.hpp>

namespace fcg::commentary {

namespace {

// The legal move of `pos` that a tree's code names on `board`, the same position.
std::optional<Move> moveOf(const Position& pos, const study::ScanBoard& board, int code) {
    const auto coded = board.decode(code);
    for (const auto& mv : pos.legalMoves()) {
        const int promotion = mv.promotion() ? static_cast<int>(*mv.promotion()) : static_cast<int>(study::NO_KIND);
        if (static_cast<int>(mv.from()) == coded.from && static_cast<int>(mv.to()) == coded.to &&
            promotion == coded.promotion)
            return mv;
    }
    return std::nullopt;
}

// The players' mean rating, one alone when the other has none; 0 for none.
int meanRating(const study::HeaderNumbers& n) {
    if (n.white_elo && n.black_elo) return (n.white_elo + n.black_elo) / 2;
    return n.white_elo ? n.white_elo : n.black_elo;
}

// A player as the answers name them: the surname of "Surname, First names".
std::string surname(std::string_view name) {
    auto s = name.substr(0, name.find(','));
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    return s.empty() || s == "?" ? std::string("?") : std::string(s);
}

std::string resultText(char result) {
    switch (result) {
        case '1':
            return "1-0";
        case '0':
            return "0-1";
        case '=':
            return "½-½";
        default:
            return {};
    }
}

// "Masters 2026", "Masters 2026 and TWIC", "A, B and C".
std::string basesText(std::span<const ReferenceBase> bases, Lang lang) {
    std::string out;
    for (std::size_t i = 0; i < bases.size(); ++i) {
        if (i > 0) out += i + 1 == bases.size() ? (lang == Lang::Fr ? " et " : " and ") : ", ";
        out += bases[i].name;
    }
    return out;
}

std::string gamesText(uint64_t n, Lang lang) {
    if (lang == Lang::Fr) return n == 1 ? "1 partie" : counted(n, lang) + " parties";
    return n == 1 ? "1 game" : counted(n, lang) + " games";
}

// The share of the points `side` scored, in percent; none without a result.
std::optional<int> scored(const study::TreeStats& s, Color side) {
    const auto decided = s.white + s.draws + s.black;
    if (decided == 0) return std::nullopt;
    const double points = static_cast<double>(side == Color::White ? s.white : s.black) + s.draws / 2.0;
    return static_cast<int>(std::lround(100.0 * points / static_cast<double>(decided)));
}

// The mean rating of the players of `side`; none without one.
std::optional<int> rating(const study::TreeStats& s, Color side) {
    const auto rated = side == Color::White ? s.white_rated : s.black_rated;
    const auto sum   = side == Color::White ? s.white_elo : s.black_elo;
    if (rated == 0) return std::nullopt;
    return static_cast<int>(sum / rated);
}

// "Carlsen – Caruana, Norway Chess 2024, 1-0".
std::string gameText(const ReferenceGame& g) {
    std::string out = surname(g.white) + " – " + surname(g.black);
    std::string where;
    if (!g.event.empty() && g.event != "?") where = g.event;
    if (g.year > 0 && where.find(std::to_string(g.year)) == std::string::npos)
        where += (where.empty() ? "" : " ") + std::to_string(g.year);
    if (!where.empty()) out += ", " + where;
    if (const auto r = resultText(g.result); !r.empty()) out += ", " + r;
    return out;
}

}  // namespace

std::string counted(uint64_t n, Lang lang) {
    auto digits     = std::to_string(n);
    const char* sep = lang == Lang::Fr ? " " : ",";
    for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3)
        digits.insert(static_cast<std::size_t>(at), sep);
    return digits;
}

References references(const Position& pos, std::span<const ReferenceBase> bases, std::size_t strongest) {
    References out;
    study::ScanBoard board;
    if (!board.setFen(pos.fen(), false)) return out;
    const uint64_t key = board.key();
    std::map<int, study::TreeStats> by_code;
    // The games through the position, by database: those the strongest are looked for among.
    std::vector<std::pair<std::size_t, study::GameId>> through;
    for (std::size_t b = 0; b < bases.size(); ++b) {
        const auto& base = bases[b];
        if (!base.snapshot) continue;
        const auto& snap = *base.snapshot;
        std::optional<study::TreeAnswer> here;
        if (base.tree && !base.tree->stale(snap)) here = base.tree->at(snap, key);
        if (here) {
            out.total.add(here->total);
            for (const auto& m : here->moves) by_code[m.code].add(m.stats);
            if (here->total.games <= LISTED_GAMES) {
                for (const auto& g : base.tree->gamesAt(snap, key)) through.emplace_back(b, g.id);
            } else {
                out.listed = false;
            }
            continue;
        }
        // Past the tree's plies, a position a single game reaches, a database without a tree: every game searched.
        const auto hits = study::searchAll(base.snapshot, study::PositionQuery{board});
        for (const auto& hit : hits) {
            const auto numbers = snap.numbers(hit.id);
            out.total.add(numbers);
            const auto m   = snap.gameMoves(hit.id);
            const int code = hit.ply < m.plies && hit.ply < m.codes.size() && board.codeCount() <= 256
                                 ? m.codes[hit.ply]
                                 : study::TreeMove::END;
            by_code[code].add(numbers);
            if (through.size() < LISTED_GAMES) through.emplace_back(b, hit.id);
        }
    }
    for (const auto& [code, stats] : by_code) {
        if (code == study::TreeMove::END) continue;
        if (const auto mv = moveOf(pos, board, code)) out.moves.push_back({*mv, stats});
    }
    std::stable_sort(out.moves.begin(), out.moves.end(),
                     [](const PlayedMove& a, const PlayedMove& b) { return a.stats.games > b.stats.games; });
    // The strongest: the players' mean rating, then the newest.
    struct Ranked {
        int rating;
        uint32_t date;
        std::size_t base;
        study::GameId id;
    };
    std::vector<Ranked> ranked;
    ranked.reserve(through.size());
    for (const auto& [b, id] : through) {
        const auto n = bases[b].snapshot->numbers(id);
        ranked.push_back({meanRating(n), n.date, b, id});
    }
    const auto kept = std::min(strongest, ranked.size());
    std::partial_sort(ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(kept), ranked.end(),
                      [](const Ranked& a, const Ranked& b) {
                          if (a.rating != b.rating) return a.rating > b.rating;
                          if (a.date != b.date) return a.date > b.date;
                          return a.id < b.id;
                      });
    for (std::size_t i = 0; i < kept; ++i) {
        const auto& r  = ranked[i];
        const auto row = bases[r.base].snapshot->row(r.id);
        ReferenceGame g;
        g.base      = r.base;
        g.id        = r.id;
        g.white     = std::string(row.white);
        g.black     = std::string(row.black);
        g.event     = std::string(row.event);
        g.year      = static_cast<int>(row.date >> 9);
        g.white_elo = row.white_elo;
        g.black_elo = row.black_elo;
        g.result    = row.result;
        out.games.push_back(std::move(g));
    }
    return out;
}

std::string gamesAnswer(const References& refs, std::span<const ReferenceBase> bases, Lang lang) {
    const bool fr    = lang == Lang::Fr;
    const auto where = basesText(bases, lang);
    const auto n     = refs.total.games;
    if (n == 0)
        return fr ? fmt::format("Aucune partie de {} n'a atteint cette position.", where)
                  : fmt::format("No game of {} reached this position.", where);
    std::string out = n == 1 ? (fr ? fmt::format("Une partie de {} a atteint cette position", where)
                                   : fmt::format("One game of {} reached this position", where))
                             : (fr ? fmt::format("{} parties de {} ont atteint cette position", counted(n, lang), where)
                                   : fmt::format("{} games of {} reached this position", counted(n, lang), where));
    const auto& t   = refs.total;
    if (n > 1 && t.white + t.draws + t.black > 0)
        out += fmt::format(" (+{} ={} −{} {})", counted(t.white, lang), counted(t.draws, lang), counted(t.black, lang),
                           fr ? "pour les Blancs" : "for White");
    if (refs.games.empty()) return out + ".";
    if (n == 1) return out + (fr ? " : " : ": ") + gameText(refs.games.front()) + ".";
    out += fr ? ". Les plus fortes : " : ". The strongest: ";
    for (std::size_t i = 0; i < refs.games.size(); ++i) out += (i ? "; " : "") + gameText(refs.games[i]);
    return out + ".";
}

std::string movesAnswer(const Position& pos, const References& refs, std::span<const ReferenceBase> bases,
                        std::optional<Move> asked, Lang lang, PieceLetters letters) {
    const bool fr    = lang == Lang::Fr;
    const auto where = basesText(bases, lang);
    const auto side  = pos.turn();
    if (asked) {
        const auto name = moveText(pos, *asked, letters);
        const auto it =
            std::find_if(refs.moves.begin(), refs.moves.end(), [&](const PlayedMove& m) { return m.move == *asked; });
        if (it == refs.moves.end())
            return fr ? fmt::format("Aucune partie de {} n'a joué {} ici.", where, name)
                      : fmt::format("No game of {} played {} here.", where, name);
        const auto& s   = it->stats;
        std::string out = fr ? fmt::format("{} a été joué dans {} de {}", name, gamesText(s.games, lang), where)
                             : fmt::format("{} was played in {} of {}", name, gamesText(s.games, lang), where);
        if (const auto p = scored(s, side))
            out += fr ? fmt::format(" : {} y ont marqué {}\u202f%", sideName(side, lang), *p)
                      : fmt::format(": {} scored {}%", sideName(side, lang), *p);
        if (const auto elo = rating(s, side))
            out += fr ? fmt::format(", avec des joueurs classés {} en moyenne", *elo)
                      : fmt::format(", its players rated {} on average", *elo);
        if (s.last)
            out += fr ? fmt::format(", la dernière fois en {}", s.last >> 9) : fmt::format(", last in {}", s.last >> 9);
        return out + ".";
    }
    if (refs.total.games == 0)
        return fr ? fmt::format("Aucune partie de {} n'a atteint cette position.", where)
                  : fmt::format("No game of {} reached this position.", where);
    if (refs.moves.empty())
        return fr ? fmt::format("Les parties de {} qui ont atteint cette position s'y sont arrêtées.", where)
                  : fmt::format("The games of {} that reached this position ended there.", where);
    std::string out        = refs.total.games == 1
                                 ? (fr ? fmt::format("Dans la seule partie de {} : ", where)
                                       : fmt::format("In the only game of {}: ", where))
                                 : (fr ? fmt::format("Dans les {} de {} : ", gamesText(refs.total.games, lang), where)
                                       : fmt::format("In the {} of {}: ", gamesText(refs.total.games, lang), where));
    const std::size_t told = std::min<std::size_t>(refs.moves.size(), 4);
    for (std::size_t i = 0; i < told; ++i) {
        const auto& m = refs.moves[i];
        out += (i ? ", " : "") + moveText(pos, m.move, letters) + " (" + gamesText(m.stats.games, lang);
        if (const auto p = scored(m.stats, side)) {
            if (i == 0)
                out += fr ? fmt::format(", {} y ont marqué {}\u202f%", sideName(side, lang), *p)
                          : fmt::format(", {} scored {}%", sideName(side, lang), *p);
            else
                out += fr ? fmt::format(", {}\u202f%", *p) : fmt::format(", {}%", *p);
        }
        out += ")";
    }
    return out + ".";
}

std::string openingAnswer(const std::optional<std::string>& name, Lang lang) {
    if (!name)
        return lang == Lang::Fr ? "La liste des ouvertures ne nomme pas cette position."
                                : "The list of openings has no name for this position.";
    return lang == Lang::Fr ? fmt::format("L'ouverture : {}.", *name) : fmt::format("The opening: {}.", *name);
}

}  // namespace fcg::commentary
