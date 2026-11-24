#include "tables_said.hpp"

#include <fmt/include/fmt/format.h>

namespace fcg::commentary {

namespace {

std::string winnerEn(Color c) { return c == Color::White ? "White" : "Black"; }
// « aux Blancs », « aux Noirs ».
std::string toWinnerFr(Color c) { return c == Color::White ? "aux Blancs" : "aux Noirs"; }

std::string tablesEn(int men) { return fmt::format("{}-piece tables", men); }
std::string tablesFr(int men) { return fmt::format("tables à {} pièces", men); }

}  // namespace

std::string tablesSentences(const TablesSaid& s, Lang lang) {
    const bool fr     = lang == Lang::Fr;
    const Color other = s.mover == Color::White ? Color::Black : Color::White;
    const auto worse  = static_cast<int>(s.after) > static_cast<int>(s.before);  // Win < Draw < Loss
    const auto after_it =
        s.refutation ? (fr ? fmt::format("après {}, ", *s.refutation) : fmt::format("after {}, ", *s.refutation))
                     : std::string();
    std::string out;
    if (worse && s.after == Verdict::Loss) {
        if (s.mate)
            out = fr ? fmt::format("{} perd : {}les tables donnent le mat {} en {} coups.", s.move, after_it,
                                   toWinnerFr(other), *s.mate)
                     : fmt::format("{} loses: {}the tables give {} mate in {} moves.", s.move, after_it,
                                   winnerEn(other), *s.mate);
        else
            out = fr ? fmt::format("{} perd : {}les tables donnent le gain {}.", s.move, after_it, toWinnerFr(other))
                     : fmt::format("{} loses: {}the tables give {} the win.", s.move, after_it, winnerEn(other));
    } else if (worse) {
        out = fr ? fmt::format("{} laisse filer le gain : {}les tables disent nulle.", s.move, after_it)
                 : fmt::format("{} throws the win away: {}the tables say draw.", s.move, after_it);
    }
    if (worse) {
        if (s.kept) {
            const bool win = s.before == Verdict::Win;
            out += ' ';
            out += fr ? fmt::format("{} gardait {} ({}).", *s.kept, win ? "le gain" : "la nulle théorique",
                                    tablesFr(s.men))
                      : fmt::format("{} kept {} ({}).", *s.kept, win ? "the win" : "the theoretical draw",
                                    tablesEn(s.men));
        }
        return out;
    }
    switch (s.after) {
        case Verdict::Win:
            if (s.mate)
                return fr ? fmt::format("{} garde le gain : mat en {} coups ({}).", s.move, *s.mate, tablesFr(s.men))
                          : fmt::format("{} keeps the win: mate in {} moves ({}).", s.move, *s.mate, tablesEn(s.men));
            return fr ? fmt::format("{} garde le gain ({}).", s.move, tablesFr(s.men))
                      : fmt::format("{} keeps the win ({}).", s.move, tablesEn(s.men));
        case Verdict::Draw:
            return fr ? fmt::format("{} garde la nulle théorique ({}).", s.move, tablesFr(s.men))
                      : fmt::format("{} keeps the theoretical draw ({}).", s.move, tablesEn(s.men));
        case Verdict::Loss:
            break;
    }
    return fr ? fmt::format("Après {}, les tables donnent toujours le gain {} ({}).", s.move, toWinnerFr(other),
                            tablesFr(s.men))
              : fmt::format("After {}, the tables still give {} the win ({}).", s.move, winnerEn(other),
                            tablesEn(s.men));
}

}  // namespace fcg::commentary
