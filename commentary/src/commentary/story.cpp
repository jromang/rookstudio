#include "story.hpp"

#include <algorithm>
#include <array>

#include <fmt/include/fmt/format.h>

#include "brief.hpp"
#include "comment.hpp"
#include "facts.hpp"
#include "formation.hpp"
#include "line.hpp"
#include "material.hpp"
#include "structure.hpp"
#include "writing.hpp"

namespace fcg::commentary {

using story_detail::Motif;
using story_detail::Oversight;
using story_detail::Source;
using story_detail::Told;

namespace {

// A game shorter than this has no story to tell.
constexpr std::size_t MIN_PLIES = 16;
// A mover's chances, in percent, from which it stands clearly better.
constexpr double AHEAD = 70.0;
// Chances under which the advantage is gone.
constexpr double LEVEL = 55.0;
// Chances from which a side is still in the game.
constexpr double ALIVE = 30.0;
// The chances a mistake must cost to turn a game.
constexpr double TURNING = 10.0;
// An endgame shorter than this is only the end of the game.
constexpr std::size_t ENDGAME_PLIES = 8;
// Captures after the first move of the endgame that still belong to the exchanges leading into it.
constexpr std::size_t SETTLING = 4;
// The moves quoted for a recurring kind of mistake.
constexpr std::size_t QUOTED = 3;
// Chances from which a position is winning: letting it go is a matter of technique.
constexpr double WINNING = 90.0;
// The things to work on told to a side, at most.
constexpr std::size_t LESSONS = 2;

constexpr std::array<Color, 2> COLORS = {Color::White, Color::Black};

template <typename... T>
bool is(const Fact& f) {
    return (std::holds_alternative<T>(f) || ...);
}

// What a move did to the course of the game.
enum class Swing : uint8_t {
    Lead,  // it gave the opponent a clear advantage, the first of the game
    Slip,  // it let a clear advantage go
    Turn,  // after it, its side never came back
};

// What a side's mistakes say it should work on.
struct Lesson {
    enum class Kind : uint8_t {
        EnPrise,
        Motif,    // tactics of one kind
        Tactics,  // tactics of kinds that differ
        King,
        Missed,
        Conversion,  // a winning position let go
        Endgame,     // mistakes in endings, of one kind when they share it
        Opening,     // mistakes before the opening was over
    };
    Lesson(Kind k, std::optional<Motif> m = std::nullopt, std::optional<Ending> e = std::nullopt)
        : kind(k), motif(m), ending(e) {}
    Kind kind = Kind::EnPrise;
    std::optional<Motif> motif;
    std::optional<Ending> ending;
    bool operator==(const Lesson&) const = default;
};

// « aux Blancs ».
const char* toSide(Color color) { return color == Color::White ? "aux Blancs" : "aux Noirs"; }
// « des Blancs ».
const char* ofSide(Color color) { return color == Color::White ? "des Blancs" : "des Noirs"; }

// The last of the largest by `key`, as Rust's max_by and max_by_key give it.
template <typename It, typename Key>
It lastMax(It begin, It end, Key key) {
    It best = end;
    for (It it = begin; it != end; ++it) {
        if (best == end || !(key(*it) < key(*best))) best = it;
    }
    return best;
}

class Teller {
   public:
    Teller(const MainLine& line, std::string_view result, const GameReport& report, const Analyses& analyses,
           const Tablebases* tables, Lang lang, PieceLetters letters)
        : line_(line),
          result_(result),
          report_(report),
          analyses_(analyses),
          tables_(tables),
          lang_(lang),
          letters_(letters) {}

    std::vector<Told> tell() const {
        // The course of the game in the order it was played: its swings, the plans the books have a name for, and where
        // it went into an endgame.
        auto course = this->course();
        std::vector<std::size_t> told;
        for (const auto& t : course) {
            if (t.passage.ply) told.push_back(*t.passage.ply);
        }
        for (auto& t : plans(told)) course.push_back(std::move(t));
        if (auto t = endgame()) course.push_back(std::move(*t));
        std::stable_sort(course.begin(), course.end(),
                         [&](const Told& a, const Told& b) { return order(a.passage.ply) < order(b.passage.ply); });
        std::vector<Told> passages;
        if (auto t = opening()) passages.push_back(std::move(*t));
        for (auto& t : course) passages.push_back(std::move(t));
        for (auto& t : lessons()) passages.push_back(std::move(t));
        if (auto t = ended()) passages.push_back(std::move(*t));
        return passages;
    }

    // The lessons on their own, for each side that has some.
    std::vector<WorkOn> workOn() const {
        const auto* decided                           = decisive();
        const std::optional<std::size_t> decisive_ply = decided ? std::optional(decided->ply) : std::nullopt;
        std::vector<WorkOn> out;
        for (const auto c : COLORS) {
            const auto found = lessonsOf(c, decisive_ply);
            if (found.empty()) continue;
            WorkOn w;
            w.side = c;
            for (const auto& [lesson, moves] : found) {
                WorkOn::Topic t;
                t.name = topic(lesson);
                t.key  = topicKey(lesson);
                for (const auto* m : moves) t.plies.push_back(m->ply);
                w.topics.push_back(std::move(t));
            }
            for (const auto& [lesson, moves] : found) {
                if (const auto* said = advice(lesson)) {
                    w.advice = said;
                    break;
                }
            }
            out.push_back(std::move(w));
        }
        return out;
    }

   private:
    bool fr() const { return lang_ == Lang::Fr; }
    const char* side(Color c) const { return sideName(c, lang_); }

    // Where a passage's move stands among the reviewed ones; a passage without one comes first.
    std::optional<std::size_t> order(const std::optional<std::size_t>& ply) const {
        if (!ply) return std::nullopt;
        for (std::size_t i = 0; i < report_.moves.size(); ++i) {
            if (report_.moves[i].ply == *ply) return i;
        }
        return std::nullopt;
    }

    // The winner, by the game's result.
    std::optional<Color> winner() const {
        if (result_ == "1-0") return Color::White;
        if (result_ == "0-1") return Color::Black;
        return std::nullopt;
    }

    // « 17…Dc7? », the move with the review's mark.
    std::string named(const MoveReport& m) const {
        const char* mark = "";
        if (m.judgment == Judgment::Inaccuracy) mark = "?!";
        if (m.judgment == Judgment::Mistake) mark = "?";
        if (m.judgment == Judgment::Blunder) mark = "??";
        return moveText(line_.positions()[m.ply - 1], line_.moves()[m.ply - 1], letters_) + mark;
    }

    MoveContext context(const MoveReport& m) const {
        return moveContext(line_, m, analyses_, nullptr, tables_, nullptr);
    }

    // The facts of a move, as its comment gathers them.
    MoveFacts facts(const MoveReport& m) const { return gather(context(m)); }

    // What the mistake `m` overlooked.
    std::optional<Oversight> oversight(const MoveReport& m) const {
        const auto ctx   = context(m);
        const bool takes = ctx.reply && !ctx.reply->moves.empty() && ctx.reply->moves.front().isCapture();
        return story_detail::oversight(gather(ctx), takes);
    }

    // The comment the move gets on its own.
    std::optional<std::string> comment(const MoveReport& m) const {
        return commentary::comment(context(m), lang_, letters_);
    }

    // The opening by name, how the sides stand when it is over, and the formation it left.
    std::optional<Told> opening() const {
        const auto& moves = report_.moves;
        if (line_.moves().empty()) return std::nullopt;
        std::vector<std::string> sentences;
        Source source;
        source.kind = Source::Kind::Opening;
        if (const auto name = openingAt(line_, line_.moves().size())) {
            sentences.push_back(*name + ".");
            source.said.push_back(fr() ? fmt::format("Ouverture : {}.", *name) : fmt::format("Opening: {}.", *name));
        }
        const auto joined = [&] {
            std::string out;
            for (std::size_t i = 0; i < sentences.size(); ++i) out += (i ? " " : "") + sentences[i];
            return out;
        };
        // A game that never leaves the opening has left nothing yet but its name.
        std::optional<std::size_t> last;
        for (std::size_t i = 0; i < moves.size(); ++i) {
            if (moves[i].phase == Phase::Opening) last = i;
        }
        if (!last || *last + 1 >= moves.size()) {
            if (sentences.empty()) return std::nullopt;
            return Told{Passage{joined(), std::nullopt}, std::move(source)};
        }
        const auto& m      = moves[*last];
        const double white = m.mover == Color::White ? m.win_after : 100.0 - m.win_after;
        sentences.push_back(outOfTheOpening(white));
        source.move = &m;
        source.said.push_back(sentences.back());
        // What the pawns look like by then: a formation with a name, hanging pawns, or simply more space for one side.
        const auto& position = line_.positions()[m.ply];
        const auto& board    = position.board();
        const auto shapes    = formations(board);
        std::optional<Fact> hanging;
        for (const auto& f : positionFacts(position)) {
            if (is<fact::HangingPawns>(f)) {
                hanging = f;
                break;
            }
        }
        std::optional<Color> roomy;
        for (const auto c : COLORS) {
            if (hasSpace(board, c)) {
                roomy = c;
                break;
            }
        }
        std::optional<std::string> said;
        if (!shapes.empty()) {
            said = formationSentence(shapes.front().first, shapes.front().second, lang_);
            source.facts.emplace_back(fact::Formation{shapes.front().first, shapes.front().second});
        } else if (hanging) {
            said = brief_detail::factSentence(*hanging, lang_);
            source.facts.push_back(*hanging);
        } else if (roomy) {
            said = fr() ? fmt::format("{} ont plus d'espace", side(*roomy))
                        : fmt::format("{} has more space", side(*roomy));
            source.facts.emplace_back(fact::Space{*roomy});
        }
        if (said) {
            sentences.push_back(capitalize(*said) + ".");
            source.said.push_back(sentences.back());
        }
        return Told{Passage{joined(), m.ply}, std::move(source)};
    }

    std::string outOfTheOpening(double white) const {
        const bool white_ahead = white >= 50.0;
        const double chances   = white_ahead ? white : 100.0 - white;
        const auto* who        = side(white_ahead ? Color::White : Color::Black);
        if (fr()) {
            if (chances <= LEVEL) return "Au sortir de l'ouverture, la position est équilibrée.";
            if (chances <= AHEAD) return fmt::format("Au sortir de l'ouverture, {} sont un peu mieux.", who);
            if (chances <= 90.0) return fmt::format("Au sortir de l'ouverture, {} sont nettement mieux.", who);
            return fmt::format("Au sortir de l'ouverture, {} ont déjà une position gagnante.", who);
        }
        if (chances <= LEVEL) return "Out of the opening, the position is balanced.";
        if (chances <= AHEAD) return fmt::format("Out of the opening, {} is slightly better.", who);
        if (chances <= 90.0) return fmt::format("Out of the opening, {} is clearly better.", who);
        return fmt::format("Out of the opening, {} is already winning.", who);
    }

    // The mistakes big enough to make the course of a game.
    std::vector<const MoveReport*> judged() const {
        std::vector<const MoveReport*> out;
        for (const auto& m : report_.moves) {
            if (m.judgment && m.loss() >= TURNING) out.push_back(&m);
        }
        return out;
    }

    // The mistake that decided the game: the loser's last one made while still in the game, the one that put it out of
    // it for choice; else its costliest.
    const MoveReport* decisive() const {
        const auto w = winner();
        if (!w) return nullptr;
        std::vector<const MoveReport*> lost;
        for (const auto* m : judged()) {
            if (m->mover != *w) lost.push_back(m);
        }
        const auto alive = [](const MoveReport* m) { return m->win_before >= ALIVE; };
        for (auto it = lost.rbegin(); it != lost.rend(); ++it) {
            if (alive(*it) && (*it)->win_after < ALIVE) return *it;
        }
        for (auto it = lost.rbegin(); it != lost.rend(); ++it) {
            if (alive(*it)) return *it;
        }
        const auto it = lastMax(lost.begin(), lost.end(), [](const MoveReport* m) { return m->loss(); });
        return it == lost.end() ? nullptr : *it;
    }

    // The mistakes that made the course of the game, in the order they were played: the one that first gave a side a
    // clear advantage, the advantages let go, and the one after which the loser never came back.
    std::vector<Told> course() const {
        const auto judged = this->judged();
        std::vector<std::pair<const MoveReport*, Swing>> swings;
        const auto* turn       = decisive();
        const MoveReport* lead = nullptr;
        for (const auto* m : judged) {
            if (m->win_before > 100.0 - AHEAD && m->win_after <= 100.0 - AHEAD) {
                lead = m;
                break;
            }
        }
        std::vector<const MoveReport*> slips;
        for (const auto* m : judged) {
            if (m->win_before >= AHEAD && m->win_after <= LEVEL) slips.push_back(m);
        }
        std::stable_sort(slips.begin(), slips.end(),
                         [](const MoveReport* a, const MoveReport* b) { return a->loss() > b->loss(); });
        if (slips.size() > 2) slips.resize(2);
        if (turn) swings.emplace_back(turn, Swing::Turn);
        std::vector<std::pair<const MoveReport*, Swing>> others;
        if (lead) others.emplace_back(lead, Swing::Lead);
        for (const auto* m : slips) others.emplace_back(m, Swing::Slip);
        for (const auto& [m, swing] : others) {
            const bool known =
                std::any_of(swings.begin(), swings.end(), [&](const auto& s) { return s.first->ply == m->ply; });
            if (!known) swings.emplace_back(m, swing);
        }
        std::stable_sort(swings.begin(), swings.end(),
                         [&](const auto& a, const auto& b) { return order(a.first->ply) < order(b.first->ply); });
        // A game lost without a mistake that size was lost little by little.
        if (swings.empty()) {
            if (const auto w = winner()) {
                std::vector<const MoveReport*> faulted;
                for (const auto& m : report_.moves) {
                    if (m.mover != *w && m.judgment) faulted.push_back(&m);
                }
                const auto it = lastMax(faulted.begin(), faulted.end(), [](const MoveReport* m) { return m->loss(); });
                if (it != faulted.end()) {
                    const auto* worst = *it;
                    const auto mv     = named(*worst);
                    const auto text =
                        fr()
                            ? fmt::format(
                                  "La partie se décide peu à peu, sans grosse faute ; le coup qui coûte le plus {} est "
                                  "{}.",
                                  toSide(!*w), mv)
                            : fmt::format(
                                  "The game is decided little by little, without a big mistake; the move that costs "
                                  "{} most is {}.",
                                  side(!*w), mv);
                    Source source;
                    source.kind = Source::Kind::Gradual;
                    source.move = worst;
                    return {Told{Passage{text, worst->ply}, std::move(source)}};
                }
            }
        }
        std::vector<Told> out;
        for (const auto& [m, swing] : swings) {
            auto text = this->swing(*m, swing);
            if (const auto said = comment(*m)) text += " " + *said;
            Source source;
            source.kind  = Source::Kind::Swing;
            source.move  = m;
            source.swing = swing == Swing::Lead   ? Source::Swing::Lead
                           : swing == Swing::Slip ? Source::Swing::Slip
                                                  : Source::Swing::Turn;
            out.push_back(Told{Passage{text, m->ply}, std::move(source)});
        }
        return out;
    }

    // The plans the books have a name for, at the moves the review marks for carrying one out. The moves in `told` have
    // their passage already, and the space and the formation the opening leaves are told with the opening.
    std::vector<Told> plans(const std::vector<std::size_t>& told) const {
        std::vector<Told> out;
        for (const auto& m : report_.moves) {
            if (!m.plan || std::find(told.begin(), told.end(), m.ply) != told.end()) continue;
            const auto facts = this->facts(m);
            const auto it    = std::find_if(facts.achieves.begin(), facts.achieves.end(), isPlan);
            if (it == facts.achieves.end()) continue;
            const bool left = m.phase == Phase::Opening && is<fact::Space, fact::Formation>(*it);
            auto text       = plan(*it, m);
            if (!text || left) continue;
            Source source;
            source.kind = Source::Kind::Plan;
            source.move = &m;
            source.facts.push_back(*it);
            out.push_back(Told{Passage{std::move(*text), m.ply}, std::move(source)});
        }
        return out;
    }

    // A plan in one sentence naming the move that carries it out; nothing for a fact that is no plan.
    std::optional<std::string> plan(const Fact& fact, const MoveReport& m) const {
        const auto mv   = named(m);
        const auto* who = side(m.mover);
        const bool f    = fr();
        if (const auto* x = std::get_if<fact::Formation>(&fact))
            return f ? fmt::format("{} amène {}.", mv, formationName(x->kind, x->side, lang_))
                     : fmt::format("{} brings about {}.", mv, formationName(x->kind, x->side, lang_));
        if (const auto* x = std::get_if<fact::PawnBreak>(&fact); x && x->lever)
            return f ? fmt::format("{} est {}.", mv, leverWords(*x->lever, lang_))
                     : fmt::format("{} is {}.", mv, leverWords(*x->lever, lang_));
        if (is<fact::Space>(fact))
            return f ? fmt::format("Par {}, {} prennent plus d'espace.", mv, who)
                     : fmt::format("With {}, {} takes more space.", mv, who);
        if (is<fact::MinorityAttack>(fact))
            return f ? fmt::format("Par {}, {} lancent l'attaque de minorité.", mv, who)
                     : fmt::format("With {}, {} starts the minority attack.", mv, who);
        if (is<fact::PawnStorm>(fact))
            return f ? fmt::format("Par {}, {} lancent leurs pions contre le roque adverse.", mv, who)
                     : fmt::format("With {}, {} storms the enemy king with pawns.", mv, who);
        if (is<fact::Simplifies>(fact))
            return f ? fmt::format("Par {}, {} simplifient avec du matériel en plus.", mv, who)
                     : fmt::format("With {}, {} simplifies while ahead in material.", mv, who);
        if (is<fact::Opposition>(fact))
            return f ? fmt::format("Par {}, {} prennent l'opposition.", mv, who)
                     : fmt::format("With {}, {} takes the opposition.", mv, who);
        if (is<fact::CutsOff>(fact))
            return f ? fmt::format("Par {}, la tour coupe le roi adverse.", mv)
                     : fmt::format("With {}, the rook cuts the enemy king off.", mv);
        if (is<fact::EntersSquare>(fact))
            return f ? fmt::format("Par {}, le roi entre dans le carré du pion.", mv)
                     : fmt::format("With {}, the king steps into the square of the pawn.", mv);
        if (const auto* x = std::get_if<fact::Majority>(&fact))
            return f ? fmt::format("Par {}, {} obtiennent la majorité de pions {}.", mv, who, ofWing(x->wing, lang_))
                     : fmt::format("With {}, {} gets the pawn majority {}.", mv, who, ofWing(x->wing, lang_));
        if (is<fact::KeySquare>(fact))
            return f ? fmt::format("Par {}, le roi atteint une case clé de son pion.", mv)
                     : fmt::format("With {}, the king reaches a key square of its pawn.", mv);
        if (is<fact::Lucena>(fact))
            return f ? fmt::format("{} atteint la position de Lucena.", mv)
                     : fmt::format("{} reaches the Lucena position.", mv);
        if (is<fact::Philidor>(fact))
            return f ? fmt::format("{} installe la défense de Philidor.", mv)
                     : fmt::format("{} sets up Philidor's defence.", mv);
        return std::nullopt;
    }

    // What a move did to the game, in one sentence naming it.
    std::string swing(const MoveReport& m, Swing swing) const {
        const auto mv    = named(m);
        const auto mover = m.mover, other = !m.mover;
        switch (swing) {
            case Swing::Lead:
                return fr() ? fmt::format("{} donne l'avantage {}.", mv, toSide(other))
                            : fmt::format("{} hands {} the advantage.", mv, side(other));
            case Swing::Slip:
                return fr() ? fmt::format("{} laisse filer l'avantage {}.", mv, ofSide(mover))
                            : fmt::format("{} lets {}'s advantage slip.", mv, side(mover));
            default:
                // A side already worse does not turn the game: it loses it for good.
                if (m.win_before < 45.0)
                    return fr() ? fmt::format("L'erreur décisive est {}.", mv)
                                : fmt::format("The decisive mistake is {}.", mv);
                return fr() ? fmt::format("Le tournant est {}.", mv) : fmt::format("The turning point is {}.", mv);
        }
    }

    // For each side, what its mistakes say it should work on: the kinds it made most, each twice at least or the one
    // that decided the game, with the moves that show them, and the advice the first one calls for.
    std::vector<Told> lessons() const {
        const auto* decided                           = decisive();
        const std::optional<std::size_t> decisive_ply = decided ? std::optional(decided->ply) : std::nullopt;
        std::vector<Told> passages;
        for (const auto c : COLORS) {
            const auto found = lessonsOf(c, decisive_ply);
            if (found.empty()) continue;
            std::vector<std::string> listed;
            for (const auto& [lesson, moves] : found) {
                std::string quoted;
                for (std::size_t i = 0; i < moves.size() && i < QUOTED; ++i)
                    quoted += (i ? ", " : "") + named(*moves[i]);
                const char* more = moves.size() > QUOTED ? ", …" : "";
                listed.push_back(fmt::format("{} ({}{})", topic(lesson), quoted, more));
            }
            std::string all;
            for (std::size_t i = 0; i < listed.size(); ++i) all += (i ? (fr() ? " et " : " and ") : "") + listed[i];
            auto text = fr() ? fmt::format("À travailler pour {} : {}.", side(c), all)
                             : fmt::format("For {} to work on: {}.", side(c), all);
            // The advice of the first lesson that one sentence can give.
            for (const auto& [lesson, moves] : found) {
                if (const auto* said = advice(lesson)) {
                    text += " ";
                    text += said;
                    break;
                }
            }
            const auto& first_moves = found.front().second;
            Source source;
            source.kind = Source::Kind::Lesson;
            source.move = first_moves.empty() ? nullptr : first_moves.front();
            passages.push_back(
                Told{Passage{text, first_moves.empty() ? std::nullopt : std::optional(first_moves.front()->ply), true},
                     std::move(source)});
        }
        return passages;
    }

    using Moves = std::vector<const MoveReport*>;

    // The lessons of `side`'s mistakes, the most telling first, each with its moves.
    std::vector<std::pair<Lesson, Moves>> lessonsOf(Color color, std::optional<std::size_t> decided) const {
        Moves made;
        for (const auto& m : report_.moves) {
            if (m.mover == color && m.judgment && *m.judgment >= Judgment::Mistake) made.push_back(&m);
        }
        std::vector<std::pair<const MoveReport*, Oversight>> overlooked;
        for (const auto* m : made) {
            if (const auto o = oversight(*m)) overlooked.emplace_back(m, *o);
        }
        const auto of = [&](auto wanted) {
            Moves out;
            for (const auto& [m, o] : overlooked) {
                if (wanted(o)) out.push_back(m);
            }
            return out;
        };
        using K                                       = Oversight::Kind;
        std::vector<std::pair<Lesson, Moves>> lessons = {
            {Lesson{Lesson::Kind::EnPrise}, of([](const Oversight& o) { return o.kind == K::EnPrise; })},
            {Lesson{Lesson::Kind::King}, of([](const Oversight& o) { return o.kind == K::King; })},
            {Lesson{Lesson::Kind::Missed}, of([](const Oversight& o) { return o.kind == K::Missed; })},
        };
        // Tactics by their kind when one comes back, or decided the game; else all together.
        const auto tactics = of([](const Oversight& o) { return o.kind == K::Tactic; });
        const auto told    = [&](const Moves& moves) {
            return moves.size() >= 2 ||
                   std::any_of(moves.begin(), moves.end(), [&](const MoveReport* m) { return m->ply == decided; });
        };
        std::vector<std::pair<Lesson, Moves>> motifs;
        for (const auto kind : {Motif::Fork, Motif::Pin, Motif::Skewer, Motif::Discovery, Motif::Guard, Motif::Trapped,
                                Motif::BackRank}) {
            auto moves = of([&](const Oversight& o) { return o == Oversight{K::Tactic, kind}; });
            if (told(moves)) motifs.emplace_back(Lesson{Lesson::Kind::Motif, kind, std::nullopt}, std::move(moves));
        }
        const auto named_motif = lastMax(motifs.begin(), motifs.end(), [](const auto& p) { return p.second.size(); });
        lessons.push_back(named_motif != motifs.end() ? *named_motif
                                                      : std::pair{Lesson{Lesson::Kind::Tactics}, tactics});
        // A winning position let go: twice, or once in a game not won.
        Moves let_go;
        for (const auto* m : made) {
            if (m->win_before >= WINNING && m->win_after <= AHEAD) let_go.push_back(m);
        }
        if (let_go.size() >= 2 || (!let_go.empty() && winner() != color))
            lessons.emplace_back(Lesson{Lesson::Kind::Conversion}, let_go);
        const auto ending_of = [&](const MoveReport* m) -> std::optional<Ending> {
            const auto e = ending(line_.positions()[m->ply - 1].board());
            if (e == Ending::Queenless) return std::nullopt;
            return e;
        };
        Moves endgame;
        for (const auto* m : made) {
            if (m->phase == Phase::Endgame) endgame.push_back(m);
        }
        // The endings of one kind when two of the mistakes were made in it; else all of them.
        std::vector<std::pair<Lesson, Moves>> endings;
        for (const auto kind : {Ending::Pawn, Ending::Rook, Ending::Queen, Ending::Minor, Ending::OppositeBishops}) {
            Moves moves;
            for (const auto* m : endgame) {
                if (ending_of(m) == kind) moves.push_back(m);
            }
            if (moves.size() >= 2) endings.emplace_back(Lesson{Lesson::Kind::Endgame, std::nullopt, kind}, moves);
        }
        const auto named_ending =
            lastMax(endings.begin(), endings.end(), [](const auto& p) { return p.second.size(); });
        lessons.push_back(named_ending != endings.end() ? *named_ending
                                                        : std::pair{Lesson{Lesson::Kind::Endgame}, endgame});
        Moves opening;
        for (const auto* m : made) {
            if (m->phase == Phase::Opening) opening.push_back(m);
        }
        lessons.emplace_back(Lesson{Lesson::Kind::Opening}, opening);
        // Where and when the mistakes were made is told when it adds moves to what they overlooked; a lesson of one
        // move only when that move decided the game.
        std::erase_if(lessons, [&](const auto& p) {
            const auto& [lesson, moves] = p;
            const bool general          = lesson.kind == Lesson::Kind::Endgame || lesson.kind == Lesson::Kind::Opening;
            if (general) return moves.size() < 2;
            return !(told(moves) || (lesson.kind == Lesson::Kind::Conversion && !moves.empty()));
        });
        std::stable_sort(lessons.begin(), lessons.end(),
                         [](const auto& a, const auto& b) { return a.second.size() > b.second.size(); });
        std::vector<std::pair<Lesson, Moves>> kept;
        for (auto& [lesson, moves] : lessons) {
            const bool fresh = std::any_of(moves.begin(), moves.end(), [&](const MoveReport* m) {
                return std::none_of(kept.begin(), kept.end(), [&](const auto& k) {
                    return std::any_of(k.second.begin(), k.second.end(),
                                       [&](const MoveReport* o) { return o->ply == m->ply; });
                });
            });
            if (fresh && kept.size() < LESSONS) kept.emplace_back(lesson, moves);
        }
        return kept;
    }

    // A lesson as something to work on: « les fourchettes », "rook endgames".
    const char* topic(const Lesson& lesson) const {
        const bool f = fr();
        switch (lesson.kind) {
            case Lesson::Kind::EnPrise:
                return f ? "les pièces laissées en prise" : "pieces left en prise";
            case Lesson::Kind::Motif:
                switch (*lesson.motif) {
                    case Motif::Fork:
                        return f ? "les fourchettes" : "forks";
                    case Motif::Pin:
                        return f ? "les clouages" : "pins";
                    case Motif::Skewer:
                        return f ? "les enfilades" : "skewers";
                    case Motif::Discovery:
                        return f ? "les attaques à la découverte" : "discovered attacks";
                    case Motif::Guard:
                        return f ? "les défenseurs détournés ou surchargés" : "defenders drawn away or overloaded";
                    case Motif::Trapped:
                        return f ? "les pièces enfermées" : "trapped pieces";
                    default:
                        return f ? "le mat du couloir" : "the back-rank mate";
                }
            case Lesson::Kind::Tactics:
                return f ? "les tactiques de l'adversaire" : "the opponent's tactics";
            case Lesson::Kind::King:
                return f ? "la sécurité du roi" : "king safety";
            case Lesson::Kind::Missed:
                return f ? "les occasions manquées" : "missed chances";
            case Lesson::Kind::Conversion:
                return f ? "la réalisation de l'avantage" : "converting an advantage";
            case Lesson::Kind::Endgame:
                if (lesson.ending == Ending::Pawn) return f ? "les finales de pions" : "pawn endgames";
                if (lesson.ending == Ending::Rook) return f ? "les finales de tours" : "rook endgames";
                if (lesson.ending == Ending::Queen) return f ? "les finales de dames" : "queen endgames";
                if (lesson.ending == Ending::Minor)
                    return f ? "les finales de pièces mineures" : "minor-piece endgames";
                if (lesson.ending == Ending::OppositeBishops)
                    return f ? "les finales de fous de couleurs opposées" : "endgames with bishops of opposite colours";
                return f ? "les finales" : "endgames";
            default:
                return f ? "l'ouverture" : "the opening";
        }
    }

    // A lesson's kind, the same in every language (WorkOn::Topic::key).
    static const char* topicKey(const Lesson& lesson) {
        switch (lesson.kind) {
            case Lesson::Kind::EnPrise:
                return "en_prise";
            case Lesson::Kind::Motif:
                switch (*lesson.motif) {
                    case Motif::Fork:
                        return "fork";
                    case Motif::Pin:
                        return "pin";
                    case Motif::Skewer:
                        return "skewer";
                    case Motif::Discovery:
                        return "discovery";
                    case Motif::Guard:
                        return "guard";
                    case Motif::Trapped:
                        return "trapped";
                    default:
                        return "back_rank";
                }
            case Lesson::Kind::Tactics:
                return "tactics";
            case Lesson::Kind::King:
                return "king";
            case Lesson::Kind::Missed:
                return "missed";
            case Lesson::Kind::Conversion:
                return "conversion";
            case Lesson::Kind::Endgame:
                if (lesson.ending == Ending::Pawn) return "pawn_endgame";
                if (lesson.ending == Ending::Rook) return "rook_endgame";
                if (lesson.ending == Ending::Queen) return "queen_endgame";
                if (lesson.ending == Ending::Minor) return "minor_endgame";
                if (lesson.ending == Ending::OppositeBishops) return "bishops_endgame";
                return "endgame";
            default:
                return "opening";
        }
    }

    // The advice a lesson calls for, when one sentence can give it.
    const char* advice(const Lesson& lesson) const {
        const bool f = fr();
        switch (lesson.kind) {
            case Lesson::Kind::EnPrise:
                return f ? "Avant chaque coup, vérifier ce qu'il laisse sans défense."
                         : "Before each move, check what it leaves unguarded.";
            case Lesson::Kind::Motif:
                switch (*lesson.motif) {
                    case Motif::Fork:
                        return f ? "Repérer les cases d'où une pièce adverse en attaquerait deux."
                                 : "Look for the squares from which an enemy piece would attack two of yours.";
                    case Motif::Pin:
                        return f ? "Se méfier d'une pièce placée devant son roi, ou devant une pièce plus forte, sur "
                                   "une ligne ouverte."
                                 : "Beware of a piece standing in front of its king, or of a stronger piece, on an "
                                   "open "
                                   "line.";
                    case Motif::Skewer:
                        return f ? "Ne pas laisser deux pièces de valeur sur une même ligne ouverte."
                                 : "Do not leave two valuable pieces on the same open line.";
                    case Motif::Discovery:
                        return f ? "Regarder ce qu'une pièce adverse démasque quand elle se déplace."
                                 : "Look at what an enemy piece uncovers when it moves.";
                    case Motif::Guard:
                        return f ? "Compter ce que chaque pièce doit défendre : une pièce ne défend pas tout."
                                 : "Count what each piece has to guard: one piece cannot guard everything.";
                    case Motif::Trapped:
                        return f ? "Avant d'avancer une pièce, s'assurer qu'elle a une case de retraite."
                                 : "Before sending a piece forward, make sure it has a square to come back to.";
                    default:
                        return f ? "Donner de l'air au roi avant que les tours et la dame adverses n'entrent."
                                 : "Give the king luft before the enemy rooks and queen come in.";
                }
            case Lesson::Kind::Tactics:
                return f ? "À chaque coup, regarder d'abord les échecs, les prises et les menaces de l'adversaire."
                         : "At every move, look first at the opponent's checks, captures and threats.";
            case Lesson::Kind::King:
                return f ? "Garder des défenseurs près du roi et ses pions devant lui."
                         : "Keep defenders near the king and its pawns in front of it.";
            case Lesson::Kind::Missed:
                return f ? "À chaque coup, regarder d'abord ses propres échecs, prises et menaces."
                         : "At every move, look first at your own checks, captures and threats.";
            case Lesson::Kind::Conversion:
                return f ? "Avec l'avantage, simplifier et ne laisser aucun contre-jeu."
                         : "When ahead, simplify and allow no counterplay.";
            default:
                return nullptr;
        }
    }

    // The endgame the game went into, when it lasted.
    std::optional<Told> endgame() const {
        const auto& moves = report_.moves;
        std::optional<std::size_t> first;
        for (std::size_t i = 0; i < moves.size(); ++i) {
            if (moves[i].phase == Phase::Endgame) {
                first = i;
                break;
            }
        }
        if (!first || moves.size() - *first < ENDGAME_PLIES) return std::nullopt;
        const auto& m = moves[*first];
        // The material is counted once the exchanges that led there are over.
        const MoveReport* settled = &m;
        for (std::size_t i = *first + 1; i < moves.size() && i <= *first + SETTLING; ++i) {
            if (!line_.moves()[moves[i].ply - 1].isCapture()) break;
            settled = &moves[i];
        }
        const auto& board = line_.positions()[settled->ply].board();
        const auto number = line_.positions()[m.ply - 1].fullmoves();
        const bool f      = fr();
        const char* kind  = f ? "en finale" : "the endgame";
        if (const auto e = ending(board)) {
            switch (*e) {
                case Ending::Pawn:
                    kind = f ? "en finale de pions" : "a pawn endgame";
                    break;
                case Ending::Rook:
                    kind = f ? "en finale de tours" : "a rook endgame";
                    break;
                case Ending::Queen:
                    kind = f ? "en finale de dames" : "a queen endgame";
                    break;
                case Ending::Minor:
                    kind = f ? "en finale de pièces mineures" : "a minor-piece endgame";
                    break;
                case Ending::OppositeBishops:
                    kind = f ? "en finale de fous de couleurs opposées" : "an endgame with bishops of opposite colours";
                    break;
                default:
                    break;
            }
        }
        const auto material = this->material(board);
        auto text =
            f ? fmt::format("La partie passe {} au {}{} coup, {}.", kind, number, number == 1 ? "er" : "e", material)
              : fmt::format("The game goes into {} on move {}, {}.", kind, number, material);
        // The model is told the move, the number being out of its words.
        Source source;
        source.kind         = Source::Kind::Endgame;
        source.move         = &m;
        const auto entering = moveText(line_.positions()[m.ply - 1], line_.moves()[m.ply - 1], letters_);
        source.said.push_back(f ? fmt::format("La partie passe {} avec {}, {}.", kind, entering, material)
                                : fmt::format("The game goes into {} with {}, {}.", kind, entering, material));
        // The pawn majorities each side takes into it.
        for (const auto c : COLORS) {
            for (const auto wing : majorities(board, c)) {
                const auto said =
                    capitalize(f ? fmt::format("{} ont la majorité de pions {}.", side(c), ofWing(wing, lang_))
                                 : fmt::format("{} has the pawn majority {}.", side(c), ofWing(wing, lang_)));
                text += " ";
                text += said;
                source.said.push_back(said);
                source.facts.emplace_back(fact::Majority{c, wing});
            }
        }
        return Told{Passage{text, m.ply}, std::move(source)};
    }

    // Who is ahead in material, and by what: « avec la qualité de plus pour les Noirs ».
    std::string material(const Board& board) const {
        const bool f       = fr();
        const auto balance = commentary::balance(board, Color::White);
        const auto ahead   = balance > 0 ? Color::White : Color::Black;
        const auto* who    = side(ahead);
        const auto lead    = commentary::lead(board, ahead);
        if (!lead || balance == 0) return f ? "à matériel égal" : "with material level";
        const auto what = materialWords(balance < 0 ? -balance : balance, lead, lang_);
        // « une pièce contre deux pions »: an imbalance, told as it is. A rook for a piece is the exchange, not
        // something given.
        const auto exchanges = std::min(lead->won[2], lead->gave[1]);
        const bool mixed     = lead->gave[1] > exchanges || lead->gave[0] > 0 || lead->gave[2] > 0 || lead->gave[3] > 0;
        if (f)
            return mixed ? fmt::format("avec {} pour {}", what, who)
                         : fmt::format("avec {} de plus pour {}", what, who);
        return mixed ? fmt::format("{} with {}", who, what) : fmt::format("{} {} up", who, what);
    }

    // How the game ended.
    std::optional<Told> ended() const {
        const std::optional<std::size_t> last =
            line_.moves().empty() ? std::nullopt : std::optional(line_.moves().size());
        std::optional<Color> mated;
        if (last && *last < analyses_.size() && analyses_[*last] && analyses_[*last]->end)
            mated = analyses_[*last]->end->winner;
        std::optional<std::string> text;
        const bool f = fr();
        if (mated)
            text = f ? fmt::format("{} concluent par le mat.", side(*mated))
                     : fmt::format("{} finishes with mate.", side(*mated));
        else if (result_ == "1/2-1/2")
            text = f ? "La partie se termine par la nulle." : "The game ends in a draw.";
        else if (const auto w = winner())
            text = f ? fmt::format("{} l'emportent.", side(*w)) : fmt::format("{} wins.", side(*w));
        if (!text) return std::nullopt;
        Source source;
        source.kind  = Source::Kind::End;
        source.move  = last ? report_.get(*last) : nullptr;
        source.mated = mated;
        source.said.push_back(capitalize(*text));
        return Told{Passage{capitalize(*text), last}, std::move(source)};
    }

    const MainLine& line_;
    std::string_view result_;
    const GameReport& report_;
    const Analyses& analyses_;
    const Tablebases* tables_;
    Lang lang_;
    PieceLetters letters_;
};

}  // namespace

std::vector<Passage> story(const MainLine& line, std::string_view result, const GameReport& report,
                           const Analyses& analyses, const Tablebases* tables, Lang lang, PieceLetters letters) {
    std::vector<Passage> out;
    for (auto& t : story_detail::told(line, result, report, analyses, tables, lang, letters))
        out.push_back(std::move(t.passage));
    return out;
}

std::vector<WorkOn> workOn(const MainLine& line, std::string_view result, const GameReport& report,
                           const Analyses& analyses, const Tablebases* tables, Lang lang, PieceLetters letters) {
    if (!report.complete || report.moves.size() < MIN_PLIES) return {};
    return Teller(line, result, report, analyses, tables, lang, letters).workOn();
}

namespace story_detail {

std::vector<Told> told(const MainLine& line, std::string_view result, const GameReport& report,
                       const Analyses& analyses, const Tablebases* tables, Lang lang, PieceLetters letters) {
    if (!report.complete || report.moves.size() < MIN_PLIES) return {};
    return Teller(line, result, report, analyses, tables, lang, letters).tell();
}

std::optional<Oversight> oversight(const MoveFacts& facts, bool takes) {
    using K = Oversight::Kind;
    if (facts.missedChance()) return Oversight{K::Missed};
    if (std::any_of(facts.punished_by.begin(), facts.punished_by.end(), [](const Fact& f) {
            return is<fact::Neglects, fact::Unguards, fact::Hanging, fact::NotRetaken>(f);
        }))
        return Oversight{K::EnPrise};
    if (facts.punished_by.empty()) return std::nullopt;
    const auto& first = facts.punished_by.front();
    const auto motif  = [](Motif kind) { return Oversight{K::Tactic, kind}; };
    if (const auto* m = std::get_if<fact::Mate>(&first); m && m->pattern == MatePattern::BackRank)
        return motif(Motif::BackRank);
    if (is<fact::Mate, fact::ThreatensMate, fact::ExposedKing, fact::KingAttack>(first)) return Oversight{K::King};
    if (is<fact::Fork>(first)) return motif(Motif::Fork);
    if (is<fact::Pin>(first)) return motif(Motif::Pin);
    if (is<fact::Skewer>(first)) return motif(Motif::Skewer);
    if (is<fact::DiscoveredAttack, fact::DoubleCheck>(first)) return motif(Motif::Discovery);
    if (is<fact::Deflection, fact::Overloaded, fact::Attraction, fact::RemovesDefender>(first))
        return motif(Motif::Guard);
    if (is<fact::Trapped>(first)) return motif(Motif::Trapped);
    if (is<fact::Intermezzo, fact::Sacrifice, fact::Clearance, fact::Breakthrough>(first)) return Oversight{K::Tactic};
    // Material lost to a plain capture was there to be taken; lost to a quiet move, a tactic the facts have no name
    // for.
    if (is<fact::WinsMaterial>(first)) return takes ? Oversight{K::EnPrise} : Oversight{K::Tactic};
    return std::nullopt;
}

}  // namespace story_detail

}  // namespace fcg::commentary
