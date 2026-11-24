#include "opening_summary.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

#include <fmt/include/fmt/format.h>

#include "eval.hpp"
#include "facts.hpp"
#include "line.hpp"
#include "references.hpp"
#include "story_model.hpp"
#include "text.hpp"

namespace fcg::commentary {

using Topic = SummaryPassage::Topic;

namespace {

// Attempts at each passage: the likeliest answer, then a sample (as the story's).
constexpr uint32_t SUMMARY_ATTEMPTS = 2;
// Room for two or three sentences with their figures.
constexpr std::size_t SUMMARY_TOKENS = 160;
// The moves a passage tells, at most; a move under this share of the games is not told.
constexpr std::size_t MOVES_TOLD = 5;
constexpr int MOVE_SHARE         = 5;

bool fr(Lang lang) { return lang == Lang::Fr; }

// « les Blancs », "White"; « des Blancs », "White's".
const char* side(Color c, Lang lang) { return sideName(c, lang); }
const char* ofSide(Color c, Lang lang) {
    if (fr(lang)) return c == Color::White ? "des Blancs" : "des Noirs";
    return c == Color::White ? "White's" : "Black's";
}
const char* forSide(Color c, Lang lang) {
    if (fr(lang)) return c == Color::White ? "pour les Blancs" : "pour les Noirs";
    return c == Color::White ? "for White" : "for Black";
}

// A structure by its name, its owner said when the formation is a side's.
std::string structureName(const SummaryFigures::Structure& s, Lang lang) {
    auto name = formationName(s.formation, s.owner, lang);
    switch (s.formation) {
        case Formation::Carlsbad:
        case Formation::KingsideChain:
        case Formation::QueensideChain:
            return name;
        default:
            return fmt::format("{} {}", name, fr(lang) ? ofSide(s.owner, lang) : forSide(s.owner, lang));
    }
}

// The words a formation is told by, in lower case.
std::vector<std::string> structureWords(Formation f, Lang lang) {
    switch (f) {
        case Formation::Carlsbad:
            return {"carlsbad"};
        case Formation::Maroczy:
            return {"maroczy"};
        case Formation::Hedgehog:
            return {fr(lang) ? "hérisson" : "hedgehog"};
        case Formation::Stonewall:
            return {"stonewall"};
        case Formation::KingsideChain:
        case Formation::QueensideChain:
            return {fr(lang) ? "chaîne" : "chain"};
        case Formation::Benoni:
            return {"benoni"};
        case Formation::Isolani:
            return {fr(lang) ? "isolé" : "isolated"};
    }
    return {};
}

// The words a plan is told by, in lower case.
std::vector<std::string> planKeywords(PlanName p, Lang lang) {
    const bool f = fr(lang);
    switch (p) {
        case PlanName::MinorityAttack:
            return {f ? "minorité" : "minority"};
        case PlanName::PawnStorm:
            return {f ? "assaut" : "storm"};
        case PlanName::ChainBase:
        case PlanName::ChainHead:
            return {f ? "chaîne" : "chain"};
        case PlanName::Isolani:
            return {f ? "isolé" : "isolated"};
        case PlanName::FreeingMaroczy:
        case PlanName::FreeingHedgehog:
            return {f ? "libératrice" : "freeing"};
        case PlanName::Space:
            return {"espace", "space"};
        case PlanName::None:
            break;
    }
    return {};
}

// Every plan's and structure's words, of both languages, each with what it names: what a sentence may not name when the
// report does not have it.
struct Named {
    std::string word;
    std::vector<PlanName> plans;
    std::vector<Formation> formations;
};
const std::vector<Named>& namedWords() {
    static const std::vector<Named> all = [] {
        std::vector<Named> out;
        const auto add = [&](const std::string& w, std::optional<PlanName> p, std::optional<Formation> f) {
            auto it = std::find_if(out.begin(), out.end(), [&](const Named& n) { return n.word == w; });
            if (it == out.end()) it = out.insert(out.end(), Named{w, {}, {}});
            if (p) it->plans.push_back(*p);
            if (f) it->formations.push_back(*f);
        };
        for (const auto lang : {Lang::En, Lang::Fr}) {
            for (const auto p :
                 {PlanName::MinorityAttack, PlanName::PawnStorm, PlanName::ChainBase, PlanName::ChainHead,
                  PlanName::Isolani, PlanName::FreeingMaroczy, PlanName::FreeingHedgehog, PlanName::Space})
                for (const auto& w : planKeywords(p, lang)) add(w, p, std::nullopt);
            for (const auto f : ALL_FORMATIONS)
                for (const auto& w : structureWords(f, lang)) add(w, std::nullopt, f);
        }
        return out;
    }();
    return all;
}

// A plan's moves in words: "b4 then b5", "the push e4", "the knight from f8 to g6".
std::string planMoves(const SummaryFigures::Plan& p, Lang lang) {
    const bool f  = fr(lang);
    const auto& q = p.squares;
    if (p.route) {
        if (q.size() < 2) return {};
        auto out =
            f ? fmt::format("{} de {} en {}", p.man, q[0], q[1]) : fmt::format("{} from {} to {}", p.man, q[0], q[1]);
        if (q.size() >= 3) out += f ? fmt::format(" puis en {}", q[2]) : fmt::format(" then {}", q[2]);
        return out;
    }
    if (q.size() >= 2) return f ? fmt::format("{} puis {}", q[0], q[1]) : fmt::format("{} then {}", q[0], q[1]);
    if (q.size() == 1) return f ? fmt::format("la poussée {}", q[0]) : fmt::format("the push {}", q[0]);
    return {};
}

// A plan told whole: its name when the facts give it one, then its moves.
std::string planText(const SummaryFigures::Plan& p, Lang lang) {
    if (p.name == PlanName::None) return planMoves(p, lang);
    return fmt::format("{}, {}", planWords(p.name, lang), planMoves(p, lang));
}

// How often a structure holds, in words.
const char* howOften(int share, Lang lang) {
    const bool f = fr(lang);
    if (share >= 75) return f ? "presque toujours" : "nearly always";
    if (share >= 50) return f ? "le plus souvent" : "usually";
    if (share >= 25) return f ? "souvent" : "often";
    return f ? "parfois" : "sometimes";
}

// "at move 20", « au 20e coup », or ten moves after the position.
std::string atMove(int move, Lang lang) {
    if (move <= 0) return fr(lang) ? "dix coups plus tard" : "ten moves later";
    return fr(lang) ? fmt::format("au {}e coup", move) : fmt::format("at move {}", move);
}

// The main moves evaluated: those of a twentieth of the games at least (the most played always), with an evaluation in
// centipawns, the most played first.
std::vector<const SummaryFigures::PlayedMove*> evaluated(const SummaryFigures& f) {
    std::vector<const SummaryFigures::PlayedMove*> out;
    for (std::size_t i = 0; i < f.moves.size(); ++i) {
        const auto& m = f.moves[i];
        if (m.cp && (i == 0 || m.share >= MOVE_SHARE) && out.size() < MOVES_TOLD) out.push_back(&m);
    }
    return out;
}

// The moves a passage tells: the most played, from a twentieth of the games.
std::vector<const SummaryFigures::PlayedMove*> told(const SummaryFigures& f) {
    std::vector<const SummaryFigures::PlayedMove*> out;
    for (const auto& m : f.moves)
        if ((m.share >= MOVE_SHARE || out.empty()) && out.size() < MOVES_TOLD) out.push_back(&m);
    return out;
}

std::vector<const SummaryFigures::Plan*> plansOf(const SummaryFigures& f, Color c) {
    std::vector<const SummaryFigures::Plan*> out;
    for (const auto& p : f.plans)
        if (p.side == c) out.push_back(&p);
    return out;
}

std::string joined(const std::vector<std::string>& moves) {
    std::string out;
    for (const auto& m : moves) out += (out.empty() ? "" : " ") + m;
    return out;
}

// Whether a word reads as a move: a piece's letter (English or French), a file, a rank, a capture, a promotion, check;
// castling; a move number before it.
bool looksLikeMove(std::string_view w) {
    while (!w.empty() && (w.back() == '+' || w.back() == '#' || w.back() == '!' || w.back() == '?')) w.remove_suffix(1);
    // Its number: "11." or "11…".
    std::size_t at = 0;
    while (at < w.size() && w[at] >= '0' && w[at] <= '9') ++at;
    if (at > 0) {
        if (w.substr(at).starts_with("…"))
            at += 3;
        else if (at < w.size() && w[at] == '.')
            while (at < w.size() && w[at] == '.') ++at;
        else
            return false;
    }
    w = w.substr(at);
    if (w == "O-O" || w == "O-O-O" || w == "0-0" || w == "0-0-0") return true;
    if (w.empty()) return false;
    std::size_t i = 0;
    if (std::string_view("KQRBNRDTFC").find(w[0]) != std::string_view::npos) ++i;
    // Then [file][rank][x] file rank [=piece].
    const auto file = [&](std::size_t k) { return k < w.size() && w[k] >= 'a' && w[k] <= 'h'; };
    const auto rank = [&](std::size_t k) { return k < w.size() && w[k] >= '1' && w[k] <= '8'; };
    // Find the destination square: the last file+rank pair.
    std::size_t dest = std::string_view::npos;
    for (std::size_t k = i; k + 1 < w.size(); ++k)
        if (file(k) && rank(k + 1)) dest = k;
    if (dest == std::string_view::npos) return false;
    for (std::size_t k = i; k < dest; ++k)
        if (!(file(k) || rank(k) || w[k] == 'x')) return false;
    std::size_t end = dest + 2;
    if (end < w.size() && w[end] == '=') end += 2;
    return end == w.size() && (i > 0 || dest > 0 || at > 0 || w.size() == 2);
}

// A sentence's words with where each starts: split at whitespace and at the punctuation around words.
struct Word {
    std::size_t at = 0;
    std::string raw;    // as written, the punctuation around it left out
    std::string lower;  // in lower case
};
std::vector<Word> wordsWithPlaces(std::string_view s) {
    std::vector<Word> out;
    std::size_t at = 0;
    while (at < s.size()) {
        const auto [c, n] = text::decode(s, at);
        if (text::isWhitespace(c) || text::holds(",;:()«»“”\"", c)) {
            at += n;
            continue;
        }
        std::size_t end = at;
        while (end < s.size()) {
            const auto [d, m] = text::decode(s, end);
            if (text::isWhitespace(d) || text::holds(",;:()«»“”\"", d)) break;
            end += m;
        }
        auto raw = std::string(text::trimEndMatches(s.substr(at, end - at), ".!?"));
        // "47%" keeps its sign; "b5." loses its full stop; a move keeps its dots ("11.Rab1").
        if (!raw.empty()) out.push_back({at, raw, text::toLower(raw)});
        at = end;
    }
    return out;
}

bool sideWord(const std::string& w, Color& c) {
    if (w == "white" || w == "blancs" || w == "white's") {
        c = Color::White;
        return true;
    }
    if (w == "black" || w == "noirs" || w == "black's") {
        c = Color::Black;
        return true;
    }
    return false;
}

// Advice where the report gives frequencies: "should", "must", « il faut », « devrait ».
std::optional<Refusal> checkAdvice(std::string_view sentence) {
    static constexpr std::string_view ADVICE[] = {
        "should",  "must",    "recommend", "recommended", "recommends", "advised",   "advisable",  "doit",
        "doivent", "devrait", "devraient", "faut",        "conseille",  "conseillé", "recommande", "recommandé"};
    for (const auto& w : brief_detail::wordsOf(sentence))
        if (std::find(std::begin(ADVICE), std::end(ADVICE), w) != std::end(ADVICE))
            return Refusal{Refusal::Kind::Advice, w};
    return std::nullopt;
}

// How the position stands, said against the evaluations' lowest and highest: an advantage where both are level, a clear
// one where neither is, level where neither is.
std::optional<Refusal> checkLevel(std::string_view sentence, std::optional<std::pair<Standing, Standing>> standing) {
    if (!standing) return std::nullopt;  // checkStanding refuses any of it
    static constexpr std::string_view ADVANTAGE[] = {"advantage", "edge",    "upper",    "avantage", "winning",
                                                     "gagnante",  "gagnant", "decisive", "décisif",  "décisive"};
    static constexpr std::string_view CLEAR[]     = {"clear", "clearly", "nette", "net", "nettement"};
    static constexpr std::string_view LEVEL[]     = {"level",     "balanced",   "equal", "équilibre",
                                                     "équilibré", "équilibrée", "égal",  "égale"};
    const auto [lo, hi]                           = *standing;
    const bool level                              = lo == Standing::Balanced && hi == Standing::Balanced;
    const bool some                               = lo <= Standing::Balanced && hi >= Standing::Balanced;
    const bool clear                              = lo <= Standing::ClearlyWorse || hi >= Standing::ClearlyBetter;
    for (const auto& w : brief_detail::wordsOf(sentence)) {
        const auto in = [&](const auto& list) {
            return std::find(std::begin(list), std::end(list), w) != std::end(list);
        };
        if ((in(ADVANTAGE) && level) || (in(CLEAR) && !clear) || (in(LEVEL) && !some))
            return Refusal{Refusal::Kind::Evaluation, w};
    }
    return std::nullopt;
}

void addForms(brief_detail::Allowed& allowed, const std::string& san) {
    for (auto& f : brief_detail::forms(san)) allowed.moves.push_back(std::move(f));
}

// Words with a capital after their first letter, which the grammar's words cannot spell: "Caro-Kann".
void innerCapitals(std::string_view name, std::vector<std::string>& out) {
    for (const auto w : text::split(name, [](char32_t c) { return c == U' ' || c == U',' || c == U':'; })) {
        bool first = true, inner = false;
        for (std::size_t at = 0; at < w.size();) {
            const auto [c, n] = text::decode(w, at);
            if (!first && text::isUppercase(c)) inner = true;
            first = false;
            at += n;
        }
        if (inner) out.emplace_back(w);
    }
}

void tidy(std::vector<std::string>& list) {
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Plans

PlanName planOf(const Position& before, const Move& push) {
    const auto mover = before.turn();
    const std::vector<Move> line{push};
    PlanName best   = PlanName::None;
    const auto rank = [](PlanName p) {
        switch (p) {
            case PlanName::MinorityAttack:
                return 0;
            case PlanName::PawnStorm:
                return 1;
            case PlanName::Space:
                return 3;
            case PlanName::None:
                return 9;
            default:
                return 2;
        }
    };
    for (const auto& f : lineFacts(before, line)) {
        if (!isPlan(f) || beneficiary(f) != mover) continue;
        PlanName p = PlanName::None;
        if (std::holds_alternative<fact::MinorityAttack>(f)) {
            p = PlanName::MinorityAttack;
        } else if (std::holds_alternative<fact::PawnStorm>(f)) {
            p = PlanName::PawnStorm;
        } else if (std::holds_alternative<fact::Space>(f)) {
            p = PlanName::Space;
        } else if (const auto* b = std::get_if<fact::PawnBreak>(&f); b && b->lever) {
            switch (b->lever->kind) {
                case Lever::Kind::ChainBase:
                    p = PlanName::ChainBase;
                    break;
                case Lever::Kind::ChainHead:
                    p = PlanName::ChainHead;
                    break;
                case Lever::Kind::Isolani:
                    p = PlanName::Isolani;
                    break;
                case Lever::Kind::Freeing:
                    p = b->lever->formation == Formation::Hedgehog ? PlanName::FreeingHedgehog
                                                                   : PlanName::FreeingMaroczy;
                    break;
            }
        }
        if (rank(p) < rank(best)) best = p;
    }
    return best;
}

std::string planWords(PlanName name, Lang lang) {
    const bool f = fr(lang);
    switch (name) {
        case PlanName::MinorityAttack:
            return f ? "l'attaque de minorité" : "the minority attack";
        case PlanName::PawnStorm:
            return f ? "l'assaut des pions contre le roque" : "the pawn storm against the king";
        case PlanName::ChainBase:
            return leverWords(Lever{Lever::Kind::ChainBase, Formation::Carlsbad}, lang);
        case PlanName::ChainHead:
            return leverWords(Lever{Lever::Kind::ChainHead, Formation::Carlsbad}, lang);
        case PlanName::Isolani:
            return leverWords(Lever{Lever::Kind::Isolani, Formation::Carlsbad}, lang);
        case PlanName::FreeingMaroczy:
            return leverWords(Lever::freeing(Formation::Maroczy), lang);
        case PlanName::FreeingHedgehog:
            return leverWords(Lever::freeing(Formation::Hedgehog), lang);
        case PlanName::Space:
            return f ? "le gain d'espace au centre" : "the gain of space in the centre";
        case PlanName::None:
            break;
    }
    return {};
}

// ---------------------------------------------------------------------------------------------------------------------
// Figures as written

namespace summary_detail {

std::string percent(int value, Lang lang) { return fr(lang) ? fmt::format("{} %", value) : fmt::format("{}%", value); }

std::string games(uint64_t value, Lang lang) { return counted(value, lang); }

std::string evaluation(std::optional<int32_t> cp, std::optional<int> mate, Lang lang) {
    if (mate) return fmt::format("#{}", *mate);
    if (!cp) return {};
    auto text = fmt::format("{:+.2f}", *cp / 100.0);
    if (fr(lang)) std::replace(text.begin(), text.end(), '.', ',');
    return text;
}

std::vector<std::pair<std::size_t, std::string>> figuresIn(std::string_view s) {
    std::vector<std::pair<std::size_t, std::string>> out;
    const auto digit   = [&](std::size_t k) { return k < s.size() && s[k] >= '0' && s[k] <= '9'; };
    const auto spaceAt = [&](std::size_t k) -> std::size_t {  // a no-break space's bytes there, 0 for none
        if (s.substr(k).starts_with(" ")) return 3;
        if (s.substr(k).starts_with(" ")) return 2;
        return 0;
    };
    for (std::size_t i = 0; i < s.size();) {
        std::size_t start = i, k = i;
        if ((s[k] == '+' || s[k] == '-' || s[k] == '#') && digit(k + 1))
            ++k;
        else if (s[k] == '#' && k + 2 < s.size() && s[k + 1] == '-' && digit(k + 2))
            k += 2;
        if (!digit(k)) {
            i += text::decode(s, i).second;
            continue;
        }
        // Not inside a word: a square ("b4"), a name.
        if (start > 0) {
            const auto prev   = text::lastStart(s.substr(0, start));
            const auto [c, n] = text::decode(s, prev);
            if (text::isAlphanumeric(c)) {
                while (k < s.size() && (digit(k) || std::isalpha(static_cast<unsigned char>(s[k])))) ++k;
                i = k;
                continue;
            }
        }
        while (digit(k)) ++k;
        for (;;) {
            if (k + 1 < s.size() && (s[k] == ',' || s[k] == '.') && digit(k + 1)) {
                k += 1;
                while (digit(k)) ++k;
                continue;
            }
            if (const auto sp = spaceAt(k); sp && digit(k + sp)) {
                k += sp;
                while (digit(k)) ++k;
                continue;
            }
            break;
        }
        // A move's number: "11.Rab1", "11…Nf8".
        if ((k < s.size() && s[k] == '.' && k + 1 < s.size() && !text::isWhitespace(text::decode(s, k + 1).first)) ||
            s.substr(k).starts_with("…")) {
            while (k < s.size() && !text::isWhitespace(text::decode(s, k).first)) k += text::decode(s, k).second;
            i = k;
            continue;
        }
        if (k < s.size() && s[k] == '%') {
            ++k;
        } else if (const auto sp = spaceAt(k); sp && k + sp < s.size() && s[k + sp] == '%') {
            k += sp + 1;
        } else if (k + 1 < s.size() && s[k] == ' ' && s[k + 1] == '%') {
            k += 2;
        } else if (k < s.size() && s[k] == 'e' &&
                   !(k + 1 < s.size() && std::isalpha(static_cast<unsigned char>(s[k + 1])))) {
            ++k;  // « 20e »
        }
        out.emplace_back(start, std::string(s.substr(start, k - start)));
        i = k;
    }
    return out;
}

}  // namespace summary_detail

using summary_detail::evaluation;
using summary_detail::games;
using summary_detail::percent;

// ---------------------------------------------------------------------------------------------------------------------
// The templates

std::vector<Topic> summaryTopics(const SummaryFigures& f) {
    std::vector<Topic> out;
    // A structure a tenth of the games have at least: the rest says little.
    if (!f.structures.empty() && f.structures.front().share >= 10) out.push_back(Topic::Structure);
    if (!plansOf(f, Color::White).empty()) out.push_back(Topic::WhitePlans);
    if (!plansOf(f, Color::Black).empty()) out.push_back(Topic::BlackPlans);
    if (!f.moves.empty()) out.push_back(Topic::Moves);
    if (!f.lines.empty() && f.lines.front().moves.size() >= 2) out.push_back(Topic::Line);
    if (!evaluated(f).empty()) out.push_back(Topic::Evaluations);
    return out;
}

std::string templatePassage(const SummaryFigures& f, Topic topic, Lang lang) {
    const bool fr_ = fr(lang);
    switch (topic) {
        case Topic::Structure: {
            if (f.structures.empty()) return {};
            const auto& s   = f.structures.front();
            const auto name = structureName(s, lang);
            std::string out;
            if (s.kept)
                out =
                    fr_ ? fmt::format("{} tient {} : {} des parties l'ont encore {}.", capitalize(name),
                                      howOften(s.share, lang), percent(s.share, lang), atMove(f.structure_move, lang))
                        : fmt::format("{} {} holds: {} of the games still have it {}.", capitalize(name),
                                      howOften(s.share, lang), percent(s.share, lang), atMove(f.structure_move, lang));
            else
                out = fr_ ? fmt::format("{}, {} des parties ont {}.", capitalize(atMove(f.structure_move, lang)),
                                        percent(s.share, lang), name)
                          : fmt::format("{}, {} of the games have {}.", capitalize(atMove(f.structure_move, lang)),
                                        percent(s.share, lang), name);
            if (f.structures.size() >= 2 && f.structures[1].share >= 10) {
                const auto& t = f.structures[1];
                out += fr_ ? fmt::format(" Puis vient {} ({}).", structureName(t, lang), percent(t.share, lang))
                           : fmt::format(" Next comes {} ({}).", structureName(t, lang), percent(t.share, lang));
            }
            return out;
        }
        case Topic::WhitePlans:
        case Topic::BlackPlans: {
            const auto c     = topic == Topic::WhitePlans ? Color::White : Color::Black;
            const auto plans = plansOf(f, c);
            if (plans.empty()) return {};
            const auto& p   = *plans.front();
            std::string out = fr_ ? fmt::format("Le plan le plus fréquent {} est {} : {} des parties", ofSide(c, lang),
                                                planText(p, lang), percent(p.share, lang))
                                  : fmt::format("{} most frequent plan is {}: {} of the games", ofSide(c, lang),
                                                planText(p, lang), percent(p.share, lang));
            if (p.score)
                out += fr_ ? fmt::format(", où {} marquent {}", side(c, lang), percent(*p.score, lang))
                           : fmt::format(", where {} scores {}", side(c, lang), percent(*p.score, lang));
            out += '.';
            for (std::size_t i = 1; i < plans.size() && i < 3; ++i) {
                const auto& q = *plans[i];
                out += fr_ ? fmt::format(" Ensuite, {} : {} des parties", planText(q, lang), percent(q.share, lang))
                           : fmt::format(" Next, {}: {} of the games", planText(q, lang), percent(q.share, lang));
                if (q.score)
                    out += fr_ ? fmt::format(", {} {}", percent(*q.score, lang), forSide(c, lang))
                               : fmt::format(", {} {}", percent(*q.score, lang), forSide(c, lang));
                out += '.';
            }
            return out;
        }
        case Topic::Moves: {
            const auto moves = told(f);
            if (moves.empty()) return {};
            const auto& m   = *moves.front();
            std::string out = fr_ ? fmt::format("{} est le coup le plus joué : {} parties ({})", m.san,
                                                games(m.games, lang), percent(m.share, lang))
                                  : fmt::format("{} is the most played move: {} games ({})", m.san,
                                                games(m.games, lang), percent(m.share, lang));
            if (m.score) out += fmt::format(", {} {}", percent(*m.score, lang), forSide(f.mover, lang));
            out += '.';
            if (moves.size() >= 2) {
                const auto& n = *moves[1];
                out += fr_ ? fmt::format(" {} suit : {} parties", n.san, games(n.games, lang))
                           : fmt::format(" {} follows: {} games", n.san, games(n.games, lang));
                if (n.score) out += fmt::format(", {}", percent(*n.score, lang));
                out += '.';
            }
            // The best score of the moves played often, when it is not the most played's.
            const SummaryFigures::PlayedMove* best = nullptr;
            for (const auto* x : moves)
                if (x->share >= 10 && x->score && (!best || *x->score > *best->score)) best = x;
            if (best && best != moves.front() && (moves.size() < 2 || best != moves[1]) && m.score &&
                *best->score >= *m.score + 3)
                out += fr_ ? fmt::format(" Parmi les coups souvent joués, {} marque le mieux : {}.", best->san,
                                         percent(*best->score, lang))
                           : fmt::format(" Of the moves played often, {} scores best: {}.", best->san,
                                         percent(*best->score, lang));
            return out;
        }
        case Topic::Line: {
            if (f.lines.empty()) return {};
            const auto& l = f.lines.front();
            std::string out =
                fr_ ? fmt::format("La ligne principale : {} ; {} parties", joined(l.moves), games(l.games, lang))
                    : fmt::format("The main line runs {}: {} games", joined(l.moves), games(l.games, lang));
            if (l.white_score)
                out += fr_ ? fmt::format(", où les Blancs marquent {}", percent(*l.white_score, lang))
                           : fmt::format(", where White scores {}", percent(*l.white_score, lang));
            return out + '.';
        }
        case Topic::Evaluations: {
            const auto moves = evaluated(f);
            if (moves.empty()) return {};
            if (moves.size() == 1)
                return fr_ ? fmt::format("Après {}, l'évaluation est de {}.", moves[0]->san,
                                         evaluation(moves[0]->cp, std::nullopt, lang))
                           : fmt::format("After {}, the evaluation is {}.", moves[0]->san,
                                         evaluation(moves[0]->cp, std::nullopt, lang));
            int32_t low = *moves[0]->cp, high = low;
            for (const auto* m : moves) {
                low  = std::min(low, *m->cp);
                high = std::max(high, *m->cp);
            }
            const auto lo = standingOf(expected(low)), hi = standingOf(expected(high));
            const char* tail = "";
            if (lo == Standing::Balanced && hi == Standing::Balanced)
                tail = fr_ ? ", proches de l'équilibre" : ", close to level";
            else if (lo >= Standing::SlightlyBetter)
                tail = fr_ ? ", en faveur des Blancs" : ", in White's favour";
            else if (hi <= Standing::SlightlyWorse)
                tail = fr_ ? ", en faveur des Noirs" : ", in Black's favour";
            if (low == high)
                return fr_ ? fmt::format("Les évaluations après les coups principaux sont toutes de {}{}.",
                                         evaluation(low, std::nullopt, lang), tail)
                           : fmt::format("The evaluations after the main moves are all {}{}.",
                                         evaluation(low, std::nullopt, lang), tail);
            return fr_ ? fmt::format("Les évaluations après les coups principaux vont de {} à {}{}.",
                                     evaluation(low, std::nullopt, lang), evaluation(high, std::nullopt, lang), tail)
                       : fmt::format("The evaluations after the main moves go from {} to {}{}.",
                                     evaluation(low, std::nullopt, lang), evaluation(high, std::nullopt, lang), tail);
        }
    }
    return {};
}

std::vector<SummaryPassage> templateSummary(const SummaryFigures& figures, Lang lang) {
    std::vector<SummaryPassage> out;
    for (const auto t : summaryTopics(figures)) {
        SummaryPassage p;
        p.topic = t;
        p.templ = templatePassage(figures, t, lang);
        p.text  = p.templ;
        if (!p.text.empty()) out.push_back(std::move(p));
    }
    return out;
}

std::string summaryText(const std::vector<SummaryPassage>& passages) {
    std::string out;
    for (const auto& p : passages)
        if (!p.text.empty()) out += (out.empty() ? "" : " ") + p.text;
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// The model's brief and its checker

SummaryBrief::SummaryBrief(const SummaryFigures& f, Topic topic, Lang lang) : lang_(lang) {
    const bool fr_ = fr(lang);
    system = fr_ ? "Tu écris un passage du résumé d'un rapport d'ouverture pour un joueur d'échecs : une à trois "
                   "phrases courtes et sobres, en français, à partir des chiffres donnés. N'utilise que ces "
                   "chiffres : n'écris aucun coup, aucune case, aucun plan, aucune structure, aucun nombre, "
                   "aucun nom qui n'y figure pas, et rattache chaque chiffre à ce dont il parle. Des fréquences, "
                   "pas des conseils : ne dis jamais quoi jouer."
                 : "You write one passage of the summary of an opening report for a chess player: one to three "
                   "short, sober sentences in English, from the figures given. Use only those figures: write no "
                   "move, square, plan, structure, number or name that is not in them, and attach each figure to "
                   "what it is about. Frequencies, not advice: never say what to play.";
    std::vector<std::string> lines;
    // The report.
    {
        std::string where = f.after.empty() ? (fr_ ? std::string("la position de départ") : std::string("the start"))
                                            : (fr_ ? fmt::format("la position après {}", f.after)
                                                   : fmt::format("the position after {}", f.after));
        if (!f.opening.empty()) where += fmt::format(" ({})", f.opening);
        lines.push_back(fr_ ? fmt::format("Le rapport : {}, {} au trait, {} parties.", where, side(f.mover, lang),
                                          games(f.games, lang))
                            : fmt::format("The report: {}, {} to move, {} games.", where, side(f.mover, lang),
                                          games(f.games, lang)));
        innerCapitals(f.opening, allowed_.names);
        allowed_.name_words = brief_detail::wordsOf(f.opening);
        if (!f.after.empty()) addForms(allowed_, f.after);
    }
    const auto figure = [&](std::string text, std::vector<std::string> of, std::optional<Color> whose = {}) {
        for (auto& o : of) o = text::toLower(o);
        subjects_.insert(subjects_.end(), of.begin(), of.end());
        allowed_.names.push_back(text);
        figures_.push_back({std::move(text), std::move(of), whose});
    };
    const auto moveWords = [](const std::string& san) {
        auto forms = brief_detail::forms(san);
        return forms;
    };
    for (const auto& p : f.plans) {
        plans_.push_back(p.name);
        sided_.emplace_back(p.name, p.side);
    }
    for (const auto& s : f.structures) formations_.push_back(s.formation);
    switch (topic) {
        case Topic::Structure: {
            lines.emplace_back(fr_ ? "Dans le résumé : les structures de pions, et combien de parties les ont."
                                   : "In the summary: the pawn structures, and how many games have them.");
            const auto move = f.structure_move > 0
                                  ? (fr_ ? fmt::format("{}e", f.structure_move) : std::to_string(f.structure_move))
                                  : std::string();
            if (!move.empty()) figure(move, {});
            for (std::size_t i = 0; i < f.structures.size() && i < 3; ++i) {
                const auto& s   = f.structures[i];
                const auto name = structureName(s, lang);
                auto words      = structureWords(s.formation, lang);
                lines.push_back(fr_ ? fmt::format("{}{} : {} des parties l'ont {}.", capitalize(name),
                                                  s.kept ? ", déjà sur l'échiquier" : "", percent(s.share, lang),
                                                  atMove(f.structure_move, lang))
                                    : fmt::format("{}{}: {} of the games have it {}.", capitalize(name),
                                                  s.kept ? ", already on the board" : "", percent(s.share, lang),
                                                  atMove(f.structure_move, lang)));
                figure(percent(s.share, lang), words);
            }
            break;
        }
        case Topic::WhitePlans:
        case Topic::BlackPlans: {
            const auto c = topic == Topic::WhitePlans ? Color::White : Color::Black;
            lines.push_back(fr_ ? fmt::format("Dans le résumé : ce que font {} dans les quinze coups suivants, "
                                              "comptés dans les parties.",
                                              side(c, lang))
                                : fmt::format("In the summary: what {} does in the next fifteen moves, counted in the "
                                              "games.",
                                              side(c, lang)));
            for (const auto* p : plansOf(f, c)) {
                std::vector<std::string> of(p->squares.begin(), p->squares.end());
                for (const auto& w : planKeywords(p->name, lang)) of.push_back(w);
                if (p->route && !p->man.empty()) of.push_back(brief_detail::wordsOf(p->man).back());
                for (const auto& sq : p->squares) allowed_.squares.push_back(sq);
                std::string said = fr_ ? fmt::format("Un plan {} : {} ; {} des parties", ofSide(c, lang),
                                                     planText(*p, lang), percent(p->share, lang))
                                       : fmt::format("A plan of {}: {}; {} of the games", side(c, lang),
                                                     planText(*p, lang), percent(p->share, lang));
                figure(percent(p->share, lang), of);
                if (p->score) {
                    said += fr_ ? fmt::format(", où {} marquent {}", side(c, lang), percent(*p->score, lang))
                                : fmt::format(", where {} scores {}", side(c, lang), percent(*p->score, lang));
                    figure(percent(*p->score, lang), of, c);
                }
                lines.push_back(said + '.');
            }
            break;
        }
        case Topic::Moves: {
            lines.emplace_back(fr_ ? "Dans le résumé : les coups joués dans la position."
                                   : "In the summary: the moves played in the position.");
            for (const auto* m : told(f)) {
                addForms(allowed_, m->san);
                const auto of    = moveWords(m->san);
                std::string said = fr_ ? fmt::format("{} : {} parties, {} de celles de la position", m->san,
                                                     games(m->games, lang), percent(m->share, lang))
                                       : fmt::format("{}: {} games, {} of the position's", m->san,
                                                     games(m->games, lang), percent(m->share, lang));
                figure(games(m->games, lang), of);
                figure(percent(m->share, lang), of);
                if (m->score) {
                    said += fr_ ? fmt::format(" ; {} y marquent {}", side(f.mover, lang), percent(*m->score, lang))
                                : fmt::format("; {} scores {} after it", side(f.mover, lang), percent(*m->score, lang));
                    figure(percent(*m->score, lang), of, f.mover);
                }
                lines.push_back(said + '.');
            }
            break;
        }
        case Topic::Line: {
            lines.emplace_back(fr_ ? "Dans le résumé : la ligne principale, les suites les plus jouées."
                                   : "In the summary: the main line, the continuations played most.");
            const auto& l = f.lines.front();
            std::vector<std::string> of;
            for (const auto& m : l.moves) {
                addForms(allowed_, m);
                for (const auto& w : moveWords(m)) of.push_back(w);
            }
            std::string said =
                fr_ ? fmt::format("La ligne principale : {} ; {} parties", joined(l.moves), games(l.games, lang))
                    : fmt::format("The main line: {}; {} games", joined(l.moves), games(l.games, lang));
            figure(games(l.games, lang), of);
            if (l.white_score) {
                said += fr_ ? fmt::format(", où les Blancs marquent {}", percent(*l.white_score, lang))
                            : fmt::format(", where White scores {}", percent(*l.white_score, lang));
                figure(percent(*l.white_score, lang), of, Color::White);
            }
            lines.push_back(said + '.');
            break;
        }
        case Topic::Evaluations: {
            lines.emplace_back(fr_ ? "Dans le résumé : les évaluations après les coups principaux, du point de vue "
                                     "des Blancs."
                                   : "In the summary: the evaluations after the main moves, from White's side.");
            const auto moves = evaluated(f);
            int32_t low = INT32_MAX, high = INT32_MIN;
            for (const auto* m : moves) {
                addForms(allowed_, m->san);
                const auto e = evaluation(m->cp, std::nullopt, lang);
                lines.push_back(fr_ ? fmt::format("Après {} : {}.", m->san, e)
                                    : fmt::format("After {}: {}.", m->san, e));
                // Of its move, or of the main moves together.
                auto of = moveWords(m->san);
                for (const char* w : {"moves", "coups", "evaluations", "évaluations"}) of.emplace_back(w);
                figure(e, std::move(of));
                low  = std::min(low, *m->cp);
                high = std::max(high, *m->cp);
            }
            if (!moves.empty()) {
                const auto lo = standingOf(expected(low)), hi = standingOf(expected(high));
                standing_ = std::pair{lo, hi};
                lines.push_back(
                    fr_ ? fmt::format("La position des Blancs après le moins bon d'entre eux : {} ; "
                                      "après le meilleur : {}.",
                                      brief_detail::standingWords(lo, lang), brief_detail::standingWords(hi, lang))
                        : fmt::format("White's position after the worst of them: {}; after the best: {}.",
                                      brief_detail::standingWords(lo, lang), brief_detail::standingWords(hi, lang)));
            }
            break;
        }
    }
    lines.emplace_back(fr_ ? "Résumé :" : "Summary:");
    for (std::size_t i = 0; i < lines.size(); ++i) prompt += (i ? "\n" : "") + lines[i];
    tidy(allowed_.moves);
    tidy(allowed_.squares);
    tidy(allowed_.names);
    tidy(subjects_);
    grammar = allowed_.grammar(lang);
    told_   = brief_detail::wordsOf(prompt);
}

std::optional<Refusal> SummaryBrief::check(std::string_view whole) const {
    using namespace brief_detail;
    const auto listed = [](const std::vector<std::string>& list, std::string_view w) {
        return std::find(list.begin(), list.end(), w) != list.end();
    };
    for (const auto sentence : sentenceTexts(whole)) {
        if (auto r = checkAdvice(sentence)) return r;
        const auto words = wordsWithPlaces(sentence);
        // Each figure: one the passage gives, of what the sentence names nearest to it, of the side said beside it.
        for (const auto& [at, text] : summary_detail::figuresIn(sentence)) {
            std::vector<const Figure*> candidates;
            for (const auto& fig : figures_)
                if (fig.text == text) candidates.push_back(&fig);
            if (candidates.empty()) return Refusal{Refusal::Kind::Figure, text};
            // What the figure is of: the nearest subject before it in its clause (up to a semicolon), else the nearest
            // after it there, else the nearest before it in the sentence ("b4 then b5: 47% of the games, where White
            // scores 55%"; "in 78% the knight goes from g8 to f6").
            const auto clause_from = [&] {
                const auto semi = sentence.substr(0, at).rfind(';');
                return semi == std::string_view::npos ? std::size_t{0} : semi;
            }();
            const auto clause_to = [&] {
                const auto semi = sentence.find(';', at);
                return semi == std::string_view::npos ? sentence.size() : semi;
            }();
            const auto names = [&](const Word& w) { return listed(subjects_, w.lower) || listed(subjects_, w.raw); };
            std::optional<std::string> subject;
            for (const auto& w : words)
                if (w.at >= clause_from && w.at < at && names(w)) subject = w.lower;
            if (!subject)
                for (const auto& w : words)
                    if (w.at > at && w.at < clause_to && names(w)) {
                        subject = w.lower;
                        break;
                    }
            if (!subject)
                for (const auto& w : words)
                    if (w.at < at && names(w)) subject = w.lower;
            std::vector<const Figure*> fitting;
            for (const auto* c : candidates)
                if (c->of.empty() || (subject && listed(c->of, *subject))) fitting.push_back(c);
            if (fitting.empty()) return Refusal{Refusal::Kind::Figure, text};
            // A side said within three words of it: the side whose score it is.
            std::optional<Color> said;
            std::size_t index = 0;
            while (index < words.size() && words[index].at < at) ++index;
            for (std::size_t k = index >= 3 ? index - 3 : 0; k < std::min(words.size(), index + 4); ++k) {
                Color c;
                if (k != index && sideWord(words[k].lower, c)) {
                    said = c;
                    if (k > index) break;  // the nearest after it wins: "55% for White"
                }
            }
            if (said && std::none_of(fitting.begin(), fitting.end(),
                                     [&](const Figure* c) { return !c->side || *c->side == *said; }))
                return Refusal{Refusal::Kind::Figure, text};
        }
        // Each move: one of the position or of the report's lines; a square written as a move is one too.
        for (const auto& w : words)
            if (looksLikeMove(w.raw) && !listed(allowed_.moves, w.raw) && !listed(allowed_.squares, w.raw))
                return Refusal{Refusal::Kind::Notation, w.raw};
        // The plans and structures it names: the report's, of the side it says.
        std::optional<Color> one_side;
        bool both = false;
        for (const auto& w : words) {
            Color c;
            if (!sideWord(w.lower, c)) continue;
            if (one_side && *one_side != c) both = true;
            one_side = c;
        }
        for (const auto& w : wordsOf(sentence)) {
            for (const auto& n : namedWords()) {
                if (n.word != w) continue;
                const bool plan      = std::any_of(n.plans.begin(), n.plans.end(), [&](PlanName p) {
                    return std::find(plans_.begin(), plans_.end(), p) != plans_.end();
                });
                const bool formation = std::any_of(n.formations.begin(), n.formations.end(), [&](Formation f) {
                    return std::find(formations_.begin(), formations_.end(), f) != formations_.end();
                });
                if (!plan && !formation) return Refusal{Refusal::Kind::Unsupported, w};
                // A plan of the other side, said of this one.
                if (plan && !formation && one_side && !both &&
                    std::none_of(sided_.begin(), sided_.end(), [&](const auto& s) {
                        return std::find(n.plans.begin(), n.plans.end(), s.first) != n.plans.end() &&
                               s.second == *one_side;
                    }))
                    return Refusal{Refusal::Kind::Unsupported, w};
            }
        }
        if (auto r = story_detail::checkNames(sentence, told_, allowed_)) return r;
        if (auto r = story_detail::checkNumbers(sentence, told_)) return r;
        if (auto r = checkStanding(sentence, standing_)) return r;
        if (auto r = checkLevel(sentence, standing_)) return r;
        if (auto r = checkFiller(sentence)) return r;
        if (auto r = checkWording(sentence, lang_)) return r;
        if (lang_ == Lang::Fr)
            if (auto r = checkElision(sentence)) return r;
    }
    if (!fitsGrammar(whole, allowed_, lang_)) return Refusal{Refusal::Kind::Grammar, {}};
    return std::nullopt;
}

Request summaryRequest(const SummaryBrief& brief, uint32_t round, uint32_t attempt) {
    Request r;
    r.messages    = {{Speaker::System, brief.system}, {Speaker::User, brief.prompt}};
    r.grammar     = brief.grammar;
    r.max_tokens  = SUMMARY_TOKENS;
    r.temperature = round == 0 && attempt == 1 ? 0.0f : 0.7f;
    r.seed        = round * SUMMARY_ATTEMPTS + attempt;
    return r;
}

std::vector<SummaryPassage> writtenSummary(Model& model, const SummaryFigures& figures, Lang lang, uint32_t round,
                                           const std::atomic<bool>* cancel, std::atomic<int>* done,
                                           std::string* error) {
    std::vector<SummaryPassage> out;
    bool failed = false;  // the model gave no answer: not asked again
    for (const auto topic : summaryTopics(figures)) {
        SummaryPassage p;
        p.topic = topic;
        p.templ = templatePassage(figures, topic, lang);
        p.text  = p.templ;
        if (!failed && !(cancel && cancel->load())) {
            const SummaryBrief brief(figures, topic, lang);
            for (uint32_t attempt = 1; attempt <= SUMMARY_ATTEMPTS; ++attempt) {
                if (cancel && cancel->load()) break;
                std::string why;
                const auto answer = model.generate(summaryRequest(brief, round, attempt), &why);
                p.attempts        = attempt;
                if (!answer) {
                    failed = true;
                    if (error) *error = why;
                    break;
                }
                const auto text = std::string(text::trim(answer->text));
                if (const auto refused =
                        text.empty() ? std::optional(Refusal{Refusal::Kind::Empty, {}}) : brief.check(text)) {
                    p.refused.emplace_back(text, *refused);
                    continue;
                }
                p.text     = text;
                p.by_model = true;
                break;
            }
        }
        if (!p.text.empty()) out.push_back(std::move(p));
        if (done) ++*done;
    }
    return out;
}

}  // namespace fcg::commentary
