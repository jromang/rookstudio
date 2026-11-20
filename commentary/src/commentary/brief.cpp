#include "brief.hpp"

#include <algorithm>

#include <fmt/include/fmt/format.h>

#include "facts.hpp"
#include "formation.hpp"
#include "structure.hpp"
#include "text.hpp"

namespace fcg::commentary {

using namespace brief_detail;

namespace {

// Plies of each engine line the prompt shows: those the lasting changes of a line are counted over, so that the
// position a fact describes is one the moves shown reach.
constexpr std::size_t QUOTED_PLIES = SETTLING_PLIES;
// Facts told per heading, the weightiest, besides the material won: more only crowd out what matters.
constexpr std::size_t FACTS_TOLD = 3;
// Plies of a line shown at most, when a tactic told of it comes after the usual ones.
constexpr std::size_t QUOTED_AT_MOST = 7;

template <typename... T>
bool is(const Fact& f) {
    return (std::holds_alternative<T>(f) || ...);
}

const char* system(Lang lang) {
    if (lang == Lang::Fr)
        return "Tu annotes des parties d'échecs comme un commentateur de ChessBase : une à trois phrases courtes, "
               "simples et sobres, en français. N'utilise que les faits donnés. N'invente aucun coup, aucune case, "
               "aucun motif, aucune idée stratégique qui n'y figure pas. Pas de chiffres, pas de pourcentages ; cite "
               "au plus deux coups.";
    return "You annotate chess games like a ChessBase annotator: one to three short, simple, sober sentences in "
           "English. Use only the facts given. Never invent a move, a square, a motif or a strategic idea that is not "
           "in them. No numbers, no percentages; quote at most two moves.";
}

std::vector<const Fact*> allFacts(const MoveFacts& facts) {
    std::vector<const Fact*> out;
    for (const auto* list : {&facts.achieves, &facts.punished_by, &facts.missed, &facts.otherwise}) {
        for (const auto& f : *list) out.push_back(&f);
    }
    return out;
}

// The move of `line`, played from `start`, that does the tactic `fact` tells: the fork, the pin, the skewer, the
// discovered attack, the capture that overloads a piece or the move that clears a line is its own doing, not the
// line's as a whole. None for any other fact, and for a tactic no move shown could do.
std::optional<std::size_t> doneAt(const Fact& fact, const Position& start, std::span<const Move> line) {
    const bool tactic =
        is<fact::Fork, fact::Pin, fact::Skewer, fact::DiscoveredAttack, fact::Overloaded, fact::Clearance>(fact);
    const auto side = beneficiary(fact);
    auto pos        = start;
    for (std::size_t i = 0; i < line.size() && i < QUOTED_AT_MOST; ++i) {
        // By the side the tactic is good for: a piece taken where it forked did not fork.
        const bool does = tactic && side == pos.turn() && atOnce(fact, pos, line[i]);
        pos.playUnchecked(line[i]);
        if (does) return i;
    }
    return std::nullopt;
}

// How many plies of `line`, played from `start`, the prompt shows for the facts told of it: `usual`, or as far as the
// move that does the last of their tactics, so that a fork or a discovered attack three moves on is not taken for the
// first move's doing.
std::size_t reach(std::span<const Fact* const> facts, const Position& start, std::span<const Move> line,
                  std::size_t usual) {
    std::optional<std::size_t> last;
    for (const auto* f : facts) {
        if (const auto i = doneAt(*f, start, line); i && (!last || *i > *last)) last = i;
    }
    return last ? std::max(usual, *last + 1) : usual;
}

std::vector<std::string> lineTexts(const Position& start, std::span<const Move> moves, PieceLetters letters) {
    auto pos = start;
    std::vector<std::string> out;
    for (const auto& mv : moves) {
        out.push_back(moveText(pos, mv, letters));
        pos.playUnchecked(mv);
    }
    return out;
}

std::span<const Move> first(const EngineLine* line, std::size_t plies) {
    if (!line) return {};
    return std::span<const Move>(line->moves).first(std::min(line->moves.size(), plies));
}

// The first plies of the best line and of the answer to the move: those the prompt shows, as far as the facts count a
// line's lasting changes, and further when a tactic told of the line comes later. A sound move is itself the first ply
// of the line its facts come from.
std::pair<std::span<const Move>, std::span<const Move>> shown(const MoveContext& ctx, const MoveFacts& facts) {
    const std::size_t usual = ctx.report->judgment ? QUOTED_PLIES : QUOTED_PLIES - 1;
    auto after              = *ctx.before;
    after.playUnchecked(ctx.played);
    const std::span<const Move> reply = ctx.reply ? std::span<const Move>(ctx.reply->moves) : std::span<const Move>();
    const std::span<const Move> best  = ctx.best ? std::span<const Move>(ctx.best->moves) : std::span<const Move>();
    // What the move played does itself needs no more of the line.
    std::vector<const Fact*> told;
    for (const auto& f : facts.achieves) {
        if (!atOnce(f, *ctx.before, ctx.played)) told.push_back(&f);
    }
    for (const auto& f : facts.punished_by) told.push_back(&f);
    const auto answered = reach(told, after, reply, usual);
    std::vector<const Fact*> missed;
    for (const auto& f : facts.missed) missed.push_back(&f);
    const auto better = reach(missed, *ctx.before, best, QUOTED_PLIES);
    return {first(ctx.best, better), first(ctx.reply, answered)};
}

// For an only move, the next best move and the first moves of its refutation, as the prompt shows them.
std::optional<std::pair<Move, std::span<const Move>>> otherShown(const MoveContext& ctx, const MoveFacts& facts) {
    const auto other = alternative(ctx);
    if (!other) return std::nullopt;
    auto tried = *ctx.before;
    tried.playUnchecked(other->alt);
    std::vector<const Fact*> told;
    for (const auto& f : facts.otherwise) told.push_back(&f);
    const auto plies = reach(told, tried, other->rest, QUOTED_PLIES);
    return std::pair{other->alt, other->rest.first(std::min(other->rest.size(), plies))};
}

// The squares a formation's sentence names: its owner's pawns, and where the plans go.
std::vector<Square> formationSquares(Formation kind, Color side) {
    const auto mine   = [&](File file, uint32_t steps) { return squareFor(side, file, steps); };
    const auto theirs = [&](File file, uint32_t steps) { return squareFor(!side, file, steps); };
    using enum File;
    switch (kind) {
        case Formation::Maroczy:
            return {mine(C, 3), mine(E, 3), theirs(B, 3), theirs(D, 3)};
        case Formation::Hedgehog:
            return {mine(A, 2), mine(B, 2), mine(D, 2), mine(E, 2), mine(B, 3), mine(D, 3)};
        case Formation::Stonewall:
            return {mine(D, 3), mine(E, 2), mine(F, 3), mine(E, 4), mine(E, 3)};
        case Formation::KingsideChain:
            return {mine(D, 3), mine(E, 4)};
        case Formation::QueensideChain:
            return {mine(E, 3), mine(D, 4)};
        default:
            return {};
    }
}

// The opening's name as the prompt gives it, for a move of the opening or a novelty: its parts joined by commas, those
// naming moves left out.
std::optional<std::string> openingShown(const MoveContext& ctx) {
    if (!ctx.opening) return std::nullopt;
    if (ctx.report->phase != Phase::Opening && !ctx.report->novelty) return std::nullopt;
    std::string out;
    for (const auto part : text::split(*ctx.opening, [](char32_t c) { return c == U':' || c == U','; })) {
        const auto p = text::trim(part);
        if (p.empty() || std::any_of(p.begin(), p.end(), [](char c) { return c >= '0' && c <= '9'; })) continue;
        out += (out.empty() ? "" : ", ") + std::string(p);
    }
    if (out.empty()) return std::nullopt;
    return out;
}

// What the prompt tells of theory: its lines, the surveys' main move when the game does not play it, which the model
// may then quote, and the note's words and squares.
struct TheoryShown {
    std::vector<std::string> lines;
    std::optional<Move> main;
    std::vector<std::string> words;
    std::vector<std::string> squares;
};

// Little words telling a note is in German, or partly: some surveys give their remarks in both languages, one after the
// other.
constexpr std::string_view GERMAN[] = {
    "der",  "die", "das",  "des",  "dem",  "den",  "und",  "ist", "mit", "nicht", "ein",    "eine", "einen", "einer",
    "auf",  "für", "von",  "zu",   "zum",  "zur",  "vom",  "im",  "bei", "über",  "sich",   "wird", "hat",   "kann",
    "sind", "wir", "aber", "auch", "noch", "nach", "oder", "als", "wie", "siehe", "dieser", "diese"};

// Last words that leave a note unfinished: the surveys' sentence went on with moves ("White could claim an edge by",
// "Until now White has mostly tried").
constexpr std::string_view DANGLING[] = {"a",    "an",    "the",    "of",        "by",    "with", "after", "before",
                                         "to",   "for",   "and",    "or",        "than",  "as",   "in",    "at",
                                         "from", "into",  "via",    "like",      "is",    "are",  "was",   "were",
                                         "be",   "tried", "played", "including", "namely"};

// Punctuation a note may hold beside letters, digits and spaces; any other sign is one of the surveys' symbols, a model
// could not read it.
constexpr std::string_view NOTE_PUNCTUATION = ".,;:!?'’‘\"“”()-–—…/&";

bool in(std::string_view w, std::span<const std::string_view> list) {
    return std::find(list.begin(), list.end(), w) != list.end();
}

// The surveys' marks of a move in words, "interesting", "interesting or dubious" when they differ; none for marks that
// are not about the move.
std::optional<std::string> marksSaid(const std::vector<uint8_t>& nags, Lang lang) {
    const bool fr = lang == Lang::Fr;
    std::vector<const char*> words;
    for (const auto nag : nags) {
        const char* word = nullptr;
        switch (nag) {
            case 1:
                word = fr ? "bon" : "good";
                break;
            case 2:
                word = fr ? "mauvais" : "bad";
                break;
            case 3:
                word = "excellent";
                break;
            case 4:
                word = fr ? "très mauvais" : "very bad";
                break;
            case 5:
                word = fr ? "intéressant" : "interesting";
                break;
            case 6:
                word = fr ? "douteux" : "dubious";
                break;
            default:
                continue;
        }
        if (std::none_of(words.begin(), words.end(), [&](const char* w) { return std::string_view(w) == word; }))
            words.push_back(word);
    }
    if (words.empty()) return std::nullopt;
    std::string out;
    for (std::size_t i = 0; i < words.size(); ++i) out += (i ? (fr ? " ou " : " or ") : "") + std::string(words[i]);
    return out;
}

// What the surveys make of the move played: their main move, a sideline with the marks they give it (`marks`), or none
// of theirs. A `novelty` they play is one the reference games had not seen before the game, and that the surveys have
// taken up since.
std::string surveysLine(const std::vector<uint8_t>* marks, bool main, bool novelty, std::string_view main_text,
                        Lang lang) {
    const bool en = lang == Lang::En;
    if (!marks) {
        if (en)
            return fmt::format("Theory: the game leaves the surveys' lines here; their main move is {}.", main_text);
        return fmt::format(
            "Théorie : la partie quitte ici les lignes des études d'ouverture ; leur coup principal est {}.",
            main_text);
    }
    const auto said = marksSaid(*marks, lang);
    // ", which they call good", « , qui le jugent bon », « et jugent bon »
    const auto which = [&](const char* en_form, const char* fr_form) {
        if (!said) return std::string();
        return fmt::format(fmt::runtime(en ? en_form : fr_form), *said);
    };
    if (main && novelty) {
        if (en)
            return fmt::format("Theory: a novelty in this game, since become the surveys' main move{}.",
                               which(", which they call {}", ""));
        return fmt::format(
            "Théorie : une nouveauté dans cette partie, devenue depuis le coup principal des études d'ouverture{}.",
            which("", ", qui le jugent {}"));
    }
    if (novelty) {
        if (en)
            return fmt::format(
                "Theory: a novelty in this game, since given by the surveys as a sideline{}; their main move is {}.",
                which(", which they call {}", ""), main_text);
        return fmt::format(
            "Théorie : une nouveauté dans cette partie, que les études d'ouverture donnent depuis en variante "
            "secondaire{} ; leur coup principal est {}.",
            which("", " et jugent {}"), main_text);
    }
    if (main) {
        if (en) return fmt::format("Theory: this is the surveys' main move{}.", which(", which they call {}", ""));
        return fmt::format("Théorie : c'est le coup principal des études d'ouverture{}.",
                           which("", ", qui le jugent {}"));
    }
    if (en)
        return fmt::format("Theory: the surveys give it as a sideline{}; their main move is {}.",
                           which(", which they call {}", ""), main_text);
    return fmt::format(
        "Théorie : les études d'ouverture le donnent en variante secondaire{} ; leur coup principal est {}.",
        which("", " et le jugent {}"), main_text);
}

TheoryShown theoryShown(const MoveContext& ctx, Lang lang, PieceLetters letters) {
    if (!ctx.theory) return {};
    const auto& theory = *ctx.theory;
    const auto main    = theory.main();
    if (!main) return {};
    const bool fr        = lang == Lang::Fr;
    const auto main_text = moveText(*ctx.before, *main, letters);
    std::string line;
    if (theory.source == Theory::Source::Games) {
        if (*main == ctx.played) return {};
        line = fr ? fmt::format("Théorie : ici, les parties de référence jouaient {}.", main_text)
                  : fmt::format("Theory: the reference games played {} here.", main_text);
    } else {
        line = surveysLine(theory.marks(ctx.played), *main == ctx.played, ctx.report->novelty, main_text, lang);
    }
    TheoryShown shown;
    shown.lines.push_back(std::move(line));
    if (*main != ctx.played) shown.main = main;
    // Theory castling may be called so.
    if (shown.main && shown.main->isCastle()) shown.words.emplace_back("castle");
    if (theory.note) {
        if (const auto note = noteShown(theory.note->second)) {
            const auto on = moveText(*ctx.before, theory.note->first, letters);
            shown.lines.push_back(fr ? fmt::format("Note des études sur {} (en anglais) : « {} »", on, *note)
                                     : fmt::format("Survey note on {}: “{}”", on, *note));
            for (auto& w : wordsOf(*note)) shown.words.push_back(std::move(w));
            shown.squares.clear();
            for (const auto w : text::split(*note, [](char32_t c) {
                     return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
                 })) {
                if (w.size() == 2 && w[0] >= 'a' && w[0] <= 'h' && w[1] >= '1' && w[1] <= '8')
                    shown.squares.emplace_back(w);
            }
        }
    }
    return shown;
}

// Whether the surveys play the move played, as their main move or a sideline.
bool surveysPlay(const MoveContext& ctx) {
    return ctx.theory && ctx.theory->source == Theory::Source::Surveys && ctx.theory->marks(ctx.played) != nullptr;
}

std::optional<std::pair<Standing, Standing>> standings(const MoveContext& ctx) {
    if (!ctx.report->judgment && !ctx.report->only_move) return std::nullopt;
    return std::pair{standingOf(ctx.report->win_before), standingOf(ctx.report->win_after)};
}

Allowed collect(const MoveContext& ctx, const MoveFacts& facts, const std::optional<std::string>& opening,
                const TheoryShown& theory, PieceLetters letters) {
    Allowed allowed;
    allowed.played = forms(moveText(*ctx.before, ctx.played, letters));
    auto& moves    = allowed.moves;
    moves          = allowed.played;
    if (theory.main) {
        for (auto& f : forms(moveText(*ctx.before, *theory.main, letters))) moves.push_back(std::move(f));
    }
    auto after = *ctx.before;
    after.playUnchecked(ctx.played);
    const auto [best, reply] = shown(ctx, facts);
    // For an only move, the next best move and its refutation, from the same position.
    std::vector<Move> other;
    if (const auto o = otherShown(ctx, facts)) {
        other.push_back(o->first);
        other.insert(other.end(), o->second.begin(), o->second.end());
    }
    const std::pair<const Position*, std::span<const Move>> quoted[] = {
        {ctx.before, best}, {&after, reply}, {ctx.before, other}};
    for (const auto& [start, line] : quoted) {
        for (const auto& m : lineTexts(*start, line, letters)) {
            for (auto& f : forms(m)) moves.push_back(std::move(f));
        }
    }
    std::sort(moves.begin(), moves.end());
    moves.erase(std::unique(moves.begin(), moves.end()), moves.end());
    // The facts' pieces, and where the moves the prompt shows start and land.
    const auto present = allFacts(facts);
    auto& squares      = allowed.squares;
    for (const auto* f : present) {
        for (const auto s : factSquares(*f)) squares.emplace_back(name(s));
    }
    std::vector<Move> moved{ctx.played};
    moved.insert(moved.end(), best.begin(), best.end());
    moved.insert(moved.end(), reply.begin(), reply.end());
    moved.insert(moved.end(), other.begin(), other.end());
    if (theory.main) moved.push_back(*theory.main);
    for (const auto& mv : moved) {
        squares.emplace_back(name(mv.from()));
        squares.emplace_back(name(mv.to()));
    }
    squares.insert(squares.end(), theory.squares.begin(), theory.squares.end());
    std::sort(squares.begin(), squares.end());
    squares.erase(std::unique(squares.begin(), squares.end()), squares.end());
    for (const auto* f : present) {
        for (const auto& p : factPieces(*f)) allowed.pieces.push_back(p);
    }
    // The lines themselves, with their boards, the move played first: what each sentence is read against (#64).
    std::vector<Move> played_line{ctx.played};
    if (ctx.reply) played_line.insert(played_line.end(), ctx.reply->moves.begin(), ctx.reply->moves.end());
    allowed.lines.push_back(Shown::of(Shown::Kind::Played, *ctx.before, played_line, 1 + reply.size(), letters));
    if (ctx.best && !best.empty())
        allowed.lines.push_back(Shown::of(Shown::Kind::Best, *ctx.before, ctx.best->moves, best.size(), letters));
    if (const auto alt = alternative(ctx); alt && !other.empty()) {
        std::vector<Move> other_line{alt->alt};
        other_line.insert(other_line.end(), alt->rest.begin(), alt->rest.end());
        allowed.lines.push_back(Shown::of(Shown::Kind::Other, *ctx.before, other_line, other.size(), letters));
    }
    if (theory.main)
        allowed.lines.push_back(
            Shown::of(Shown::Kind::Theory, *ctx.before, std::vector<Move>{*theory.main}, 1, letters));
    const auto board = [&](const Position& pos) {
        for (int i = 0; i < 64; ++i) {
            const auto s = square(i);
            if (const auto p = Placed::at(pos.board(), s)) allowed.pieces.push_back(*p);
        }
    };
    board(*ctx.before);
    std::vector<Move> main_line;
    if (theory.main) main_line.push_back(*theory.main);
    const std::pair<const Position*, std::span<const Move>> lines[] = {
        {ctx.before, best}, {&after, reply}, {ctx.before, main_line}, {ctx.before, other}};
    for (const auto& [start, line] : lines) {
        auto pos = *start;
        board(pos);
        for (const auto& mv : line) {
            pos.playUnchecked(mv);
            board(pos);
        }
    }
    const std::string name_of = opening.value_or("");
    for (const auto w : text::split(name_of, [](char32_t c) { return c == U' ' || c == U','; })) {
        bool capital = false;
        bool first   = true;
        for (std::size_t at = 0; at < w.size();) {
            const auto [c, n] = text::decode(w, at);
            if (!first && text::isUppercase(c)) capital = true;
            first = false;
            at += n;
        }
        if (capital) allowed.names.emplace_back(w);
    }
    allowed.name_words = wordsOf(name_of);
    allowed.noted      = theory.words;
    return allowed;
}

std::string prompt(const MoveContext& ctx, const MoveFacts& facts, const std::optional<std::string>& opening,
                   const std::vector<std::string>& theory, Lang lang, PieceLetters letters) {
    const bool fr      = lang == Lang::Fr;
    const bool white   = ctx.before->turn() == Color::White;
    const char* mover  = fr ? (white ? "les Blancs" : "les Noirs") : (white ? "White" : "Black");
    const char* movers = fr ? (white ? "des Blancs" : "des Noirs") : (white ? "White's" : "Black's");
    const auto& before = *ctx.before;
    const auto& report = *ctx.report;
    std::vector<std::string> lines;
    const auto played = moveText(before, ctx.played, letters);
    lines.push_back(fr ? fmt::format("Coup joué : {} ({}).", played, mover)
                       : fmt::format("Move played: {} ({}).", played, mover));
    if (opening) lines.push_back(fr ? fmt::format("Ouverture : {}.", *opening) : fmt::format("Opening: {}.", *opening));
    const char* verdict = nullptr;
    if (report.judgment == Judgment::Blunder) verdict = fr ? "grosse erreur" : "blunder";
    if (report.judgment == Judgment::Mistake) verdict = fr ? "erreur" : "mistake";
    if (report.judgment == Judgment::Inaccuracy) verdict = fr ? "imprécision" : "inaccuracy";
    if (!report.judgment && report.only_move) verdict = fr ? "seul bon coup" : "only good move";
    const auto standing = standings(ctx);
    if (verdict && standing) {
        const auto said = change(standing->first, standing->second, movers, lang);
        lines.push_back(fr ? fmt::format("Verdict : {}. {}", verdict, said)
                           : fmt::format("Verdict: {}. {}", verdict, said));
    }
    // A novelty the surveys have taken up since: their line tells both.
    if (report.novelty && !surveysPlay(ctx)) lines.emplace_back(fr ? "C'est une nouveauté." : "It is a novelty.");
    lines.insert(lines.end(), theory.begin(), theory.end());
    // `own` says whether the first move of `line` is the one the facts are told of: the answer to a move, or the better
    // move; else the line follows the move played.
    const auto describe = [&](const std::vector<Fact>& list, const Position& start, std::span<const Move> line,
                              bool own, std::string_view title) -> std::optional<std::string> {
        if (list.empty()) return std::nullopt;
        // A tactic a later move of the line does is told with that move.
        const auto later = [&](const Fact& f) -> std::optional<std::string> {
            if (!own && atOnce(f, before, ctx.played)) return std::nullopt;
            const auto i = doneAt(f, start, line);
            if (!i || (*i == 0 && own)) return std::nullopt;
            auto pos = start;
            for (std::size_t j = 0; j < *i; ++j) pos.playUnchecked(line[j]);
            return moveText(pos, line[*i], letters);
        };
        const auto sentence = [&](const Fact& f) -> std::optional<std::string> {
            auto said = factSentence(f, lang);
            if (!said) return std::nullopt;
            if (const auto mv = later(f))
                return fmt::format(fmt::runtime(fr ? "{} après {}" : "{} after {}"), *said, *mv);
            return said;
        };
        // The weightiest, and always what the line wins: the mechanism tells more, but the count is what it comes to.
        // So too a break the books have a name for.
        const auto always = [](const Fact& f) {
            if (is<fact::WinsMaterial>(f)) return true;
            const auto* brk = std::get_if<fact::PawnBreak>(&f);
            return brk && brk->lever.has_value();
        };
        std::vector<std::string> said;
        std::size_t told = 0;
        for (const auto& f : list) {
            auto s = sentence(f);
            if (!s) continue;
            if (told < FACTS_TOLD || always(f)) said.push_back(std::move(*s));
            ++told;
        }
        if (said.empty()) return std::nullopt;
        std::string joined;
        for (std::size_t i = 0; i < said.size(); ++i) joined += (i ? (fr ? " ; " : "; ") : "") + said[i];
        if (line.empty()) return fmt::format("{} {}", title, joined);
        return fmt::format("{} {} ({})", title, joined, lineText(start, line, line.size(), letters));
    };
    const auto [best_line, reply_line] = shown(ctx, facts);
    auto after                         = before;
    after.playUnchecked(ctx.played);
    const char* achieves = fr ? "Ce que le coup obtient :" : "What the move achieves:";
    const char* punished = fr ? "Ce qui le punit :" : "What punishes it:";
    const char* missed   = fr ? "Ce qu'aurait obtenu le meilleur coup :" : "What the best move would have achieved:";
    if (auto s = describe(facts.achieves, after, reply_line, false, achieves)) lines.push_back(std::move(*s));
    if (const auto other = otherShown(ctx, facts)) {
        auto tried = before;
        tried.playUnchecked(other->first);
        const auto alt   = moveText(before, other->first, letters);
        const auto title = fr ? fmt::format("Ce qui punit {}, le deuxième choix :", alt)
                              : fmt::format("What punishes {}, the next best move:", alt);
        if (auto s = describe(facts.otherwise, tried, other->second, true, title)) lines.push_back(std::move(*s));
    }
    if (auto s = describe(facts.punished_by, after, reply_line, true, punished)) lines.push_back(std::move(*s));
    if (report.judgment && ctx.best && !ctx.best->moves.empty()) {
        const auto best = moveText(before, ctx.best->moves.front(), letters);
        // The best move keeps the position as it stood before the move.
        const char* kept = standing ? standingWords(standing->first, lang) : "";
        // Nothing keeps a lost position: the best move only loses less.
        const bool lost = standing && standing->first == Standing::Lost;
        if (fr && lost)
            lines.push_back(
                fmt::format("Meilleur coup : {} ; la position {} était perdue de toute façon.", best, movers));
        else if (lost)
            lines.push_back(fmt::format("Best move: {}; {} position was lost anyway.", best, movers));
        else if (fr)
            lines.push_back(fmt::format("Meilleur coup : {}, qui garde la position {} {}.", best, movers, kept));
        else
            lines.push_back(fmt::format("Best move: {}, which keeps {} position {}.", best, movers, kept));
        if (auto s = describe(facts.missed, before, best_line, true, missed)) lines.push_back(std::move(*s));
    }
    lines.emplace_back(fr ? "Commentaire :" : "Comment:");
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) out += (i ? "\n" : "") + lines[i];
    return out;
}

}  // namespace

bool worthAComment(const MoveReport& report) { return report.isNotable() || report.novelty; }

namespace brief_detail {

std::string Allowed::grammar(Lang lang) const {
    const auto alternatives = [](const std::vector<std::string>& items) {
        // A rule must exist; this one never matches anything a model would write.
        if (items.empty()) return std::string("\"\\u0001\"");
        std::string out;
        for (std::size_t i = 0; i < items.size(); ++i) {
            std::string escaped;
            for (const char c : items[i]) {
                if (c == '"') escaped += '\\';
                escaped += c;
            }
            out += (i ? " | " : "") + fmt::format("\"{}\"", escaped);
        }
        return out;
    };
    // French puts a space before the double punctuation marks.
    const char* sep = lang == Lang::En ? R"(" " | ", " | ": " | "; ")" : R"(" " | ", " | " : " | " ; ")";
    const char* end = lang == Lang::En ? R"("." | "!")" : R"("." | " !")";
    return fmt::format(
        "root ::= phrase (\" \" phrase)? (\" \" phrase)?\n"
        "phrase ::= item (sep item)* end\n"
        "item ::= word | move | square | name\n"
        "word ::= [a-zàâäçéèêëîïôöûùüÿœæ'’-]+ | [A-ZÀÂÇÉÈÊÎÔÛŒ] ([a-wyzàâäçéèêëîïôöûùüÿœæ'’-] "
        "[a-zàâäçéèêëîïôöûùüÿœæ'’-]* | \"x\" [a-zàâäçéèêëîïôöûùüÿœæ'’-] [a-zàâäçéèêëîïôöûùüÿœæ'’-] "
        "[a-zàâäçéèêëîïôöûùüÿœæ'’-]+)?\n"
        "sep ::= {}\n"
        "end ::= {}\n"
        "move ::= {}\n"
        "square ::= {}\n"
        "name ::= {}\n",
        sep, end, alternatives(moves), alternatives(squares), alternatives(names));
}

std::vector<std::string> forms(std::string_view numbered) {
    // The move without its number: after the last "." or "…".
    std::string_view bare = numbered;
    const auto dot        = numbered.rfind('.');
    const auto ellipsis   = numbered.rfind("…");
    std::size_t cut       = std::string_view::npos;
    if (dot != std::string_view::npos) cut = dot + 1;
    if (ellipsis != std::string_view::npos && (cut == std::string_view::npos || ellipsis + 3 > cut)) cut = ellipsis + 3;
    if (cut != std::string_view::npos) bare = numbered.substr(cut);
    std::vector<std::string> out;
    for (const auto form : {numbered, bare}) {
        out.emplace_back(form);
        auto unsigned_ = form;
        while (!unsigned_.empty() && (unsigned_.back() == '+' || unsigned_.back() == '#')) unsigned_.remove_suffix(1);
        if (unsigned_ != form) out.emplace_back(unsigned_);
    }
    return out;
}

std::optional<std::string> noteShown(std::string_view raw) {
    // A sign left out of the text leaves two spaces after a word: "the  is misplaced".
    for (std::size_t i = 0; i + 2 < raw.size(); ++i) {
        const auto c = static_cast<unsigned char>(raw[i]);
        if (((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) && raw[i + 1] == ' ' &&
            raw[i + 2] == ' ')
            return std::nullopt;
    }
    for (std::size_t at = 0; at < raw.size();) {
        const auto [c, n] = text::decode(raw, at);
        if (!text::isAlphanumeric(c) && !text::isWhitespace(c) && !text::holds(NOTE_PUNCTUATION, c))
            return std::nullopt;
        at += n;
    }
    std::string joined;
    for (const auto w : text::splitWhitespace(raw)) joined += (joined.empty() ? "" : " ") + std::string(w);
    const auto words   = wordsOf(joined);
    const auto foreign = [](const std::string& w) { return in(w, GERMAN) || w == "database"; };
    if (words.size() < 4 || std::any_of(words.begin(), words.end(), foreign)) return std::nullopt;
    const auto is_square = [](std::string_view w) {
        return w.size() == 2 && w[0] >= 'a' && w[0] <= 'h' && w[1] >= '1' && w[1] <= '8';
    };
    for (const auto w : text::split(joined, [](char32_t c) { return !text::isAlphanumeric(c); })) {
        if (std::any_of(w.begin(), w.end(), [](char c) { return c >= '0' && c <= '9'; }) && !is_square(w))
            return std::nullopt;
    }
    if (joined.find("½") != std::string::npos) return std::nullopt;
    // Whole sentences, as many as fit.
    std::string_view note = joined;
    if (note.size() > NOTE_LENGTH) {
        std::size_t cut = NOTE_LENGTH;
        while (cut > 0 && (static_cast<unsigned char>(note[cut]) & 0xC0) == 0x80) --cut;
        const auto end = note.substr(0, cut).rfind(". ");
        if (end == std::string_view::npos) return std::nullopt;
        note = note.substr(0, end + 1);
    }
    note = text::trimEndMatches(note, ":,; ");
    // An unfinished last sentence is left out, and the note with it if it has no other.
    const auto last = [](std::string_view n) {
        auto w = wordsOf(n);
        return w.empty() ? std::string() : w.back();
    };
    if (in(last(note), DANGLING)) {
        std::optional<std::size_t> end;
        for (std::size_t i = note.size(); i-- > 0;) {
            if ((note[i] == '.' || note[i] == '!' || note[i] == '?') && note.substr(i + 1).starts_with(' ')) {
                end = i;
                break;
            }
        }
        if (!end) return std::nullopt;
        note = note.substr(0, *end + 1);
        if (wordsOf(note).size() < 4 || in(last(note), DANGLING)) return std::nullopt;
    }
    if (note.ends_with('.') || note.ends_with('!') || note.ends_with('?')) return std::string(note);
    return std::string(note) + ".";
}

std::vector<Placed> factPieces(const Fact& fact) {
    return std::visit(
        [](const auto& f) -> std::vector<Placed> {
            using T = std::decay_t<decltype(f)>;
            if constexpr (std::is_same_v<T, fact::Fork>) {
                std::vector<Placed> out{f.attacker};
                out.insert(out.end(), f.targets.begin(), f.targets.end());
                return out;
            } else if constexpr (std::is_same_v<T, fact::Pin>) {
                return {f.pinner, f.pinned, f.behind};
            } else if constexpr (std::is_same_v<T, fact::Skewer>) {
                return {f.attacker, f.front, f.behind};
            } else if constexpr (std::is_same_v<T, fact::DiscoveredAttack>) {
                return {f.attacker, f.target};
            } else if constexpr (std::is_same_v<T, fact::Hanging> || std::is_same_v<T, fact::Trapped> ||
                                 std::is_same_v<T, fact::Sacrifice>) {
                return {f.piece};
            } else if constexpr (std::is_same_v<T, fact::ThreatensCapture>) {
                return {f.target};
            } else if constexpr (std::is_same_v<T, fact::PassedPawn> || std::is_same_v<T, fact::IsolatedPawn> ||
                                 std::is_same_v<T, fact::BackwardPawn>) {
                return {f.pawn};
            } else if constexpr (std::is_same_v<T, fact::RookOnOpenFile> || std::is_same_v<T, fact::RookOnSeventh>) {
                return {f.rook};
            } else if constexpr (std::is_same_v<T, fact::Outpost>) {
                return {f.knight};
            } else if constexpr (std::is_same_v<T, fact::BadBishop> || std::is_same_v<T, fact::LongDiagonal>) {
                return {f.bishop};
            } else if constexpr (std::is_same_v<T, fact::ActiveKing> || std::is_same_v<T, fact::ExposedKing>) {
                return {f.king};
            } else if constexpr (std::is_same_v<T, fact::PawnBreak>) {
                return {f.pawn, f.target};
            } else if constexpr (std::is_same_v<T, fact::Promotes>) {
                return {Placed{f.side, f.role, f.square}};
            } else if constexpr (std::is_same_v<T, fact::WinsMaterial>) {
                if (f.taken) return {*f.taken};
                return {};
            } else if constexpr (std::is_same_v<T, fact::Attraction> || std::is_same_v<T, fact::Neglects> ||
                                 std::is_same_v<T, fact::Saves> || std::is_same_v<T, fact::Develops> ||
                                 std::is_same_v<T, fact::MovesAgain>) {
                return {f.piece};
            } else if constexpr (std::is_same_v<T, fact::OpensLine>) {
                return {f.slider};
            } else if constexpr (std::is_same_v<T, fact::Guards>) {
                return {f.guarded};
            } else if constexpr (std::is_same_v<T, fact::ProtectedPasser> || std::is_same_v<T, fact::OutsidePasser> ||
                                 std::is_same_v<T, fact::GainsSpace> || std::is_same_v<T, fact::Outruns>) {
                return {f.pawn};
            } else if constexpr (std::is_same_v<T, fact::Philidor>) {
                return {f.rook};
            } else if constexpr (std::is_same_v<T, fact::TakesWith>) {
                return {f.by};
            } else if constexpr (std::is_same_v<T, fact::Pressure>) {
                return {f.piece, f.target};
            } else if constexpr (std::is_same_v<T, fact::RookBehindPasser>) {
                return {f.rook, f.pawn};
            } else if constexpr (std::is_same_v<T, fact::Blockade>) {
                return {f.piece, f.pawn};
            } else if constexpr (std::is_same_v<T, fact::EntersSquare> || std::is_same_v<T, fact::KeySquare>) {
                return {f.king, f.pawn};
            } else if constexpr (std::is_same_v<T, fact::WrongBishop>) {
                return {f.bishop, f.pawn};
            } else if constexpr (std::is_same_v<T, fact::CutsOff>) {
                return {f.rook, f.king};
            } else if constexpr (std::is_same_v<T, fact::Overloaded>) {
                return {f.piece, f.first, f.second};
            } else if constexpr (std::is_same_v<T, fact::Clearance>) {
                return {f.piece, f.user};
            } else if constexpr (std::is_same_v<T, fact::Breakthrough>) {
                return {f.pawn, f.passer};
            } else if constexpr (std::is_same_v<T, fact::Deflection>) {
                return {f.piece, f.guarded};
            } else if constexpr (std::is_same_v<T, fact::RemovesDefender>) {
                return {f.defender, f.guarded};
            } else if constexpr (std::is_same_v<T, fact::Unguards>) {
                return {f.piece, f.left};
            } else {
                return {};
            }
        },
        fact);
}

std::vector<Square> factSquares(const Fact& fact) {
    std::vector<Square> out;
    for (const auto& p : factPieces(fact)) out.push_back(p.square);
    if (const auto* f = std::get_if<fact::Hole>(&fact)) out.push_back(f->square);
    if (const auto* f = std::get_if<fact::Covers>(&fact)) out.push_back(f->square);
    if (const auto* f = std::get_if<fact::Retakes>(&fact)) out.push_back(f->square);
    if (const auto* f = std::get_if<fact::NotRetaken>(&fact)) out.push_back(f->square);
    if (const auto* f = std::get_if<fact::Formation>(&fact)) {
        for (const auto s : formationSquares(f->kind, f->side)) out.push_back(s);
    }
    return out;
}

}  // namespace brief_detail

Brief::Brief(const MoveContext& ctx, Lang lang, PieceLetters letters) : lang_(lang) {
    facts_             = gather(ctx);
    const auto opening = openingShown(ctx);
    const auto theory  = theoryShown(ctx, lang, letters);
    allowed_           = collect(ctx, facts_, opening, theory, letters);
    const auto present = allFacts(facts_);
    only_ = ctx.report->only_move ||
            std::any_of(present.begin(), present.end(), [](const Fact* f) { return is<fact::OnlyMoveToKeep>(*f); });
    const auto from = ctx.played.from(), to = ctx.played.to();
    const bool back = ctx.before->turn() == Color::White ? rankOf(to) < rankOf(from) : rankOf(to) > rankOf(from);
    system          = commentary::system(lang);
    prompt          = commentary::prompt(ctx, facts_, opening, theory.lines, lang, letters);
    grammar         = allowed_.grammar(lang);
    mover_          = ctx.before->turn();
    judgment_       = ctx.report->judgment;
    standing_       = standings(ctx);
    if (!back) forward_to_ = name(to);
    theory_ = !theory.lines.empty();
    lost_   = brief_detail::Lost::of(*ctx.report);
}

bool Brief::hasSubstance() const {
    return judgment_.has_value() || theory_ || !facts_.achieves.empty() || !facts_.punished_by.empty() ||
           !facts_.missed.empty() || !facts_.otherwise.empty();
}

std::optional<Refusal> Brief::check(std::string_view text) const {
    if (!fitsGrammar(text, allowed_, lang_)) return Refusal{Refusal::Kind::Grammar, {}};
    if (auto r = brief_detail::check(text, facts_, allowed_, mover_)) return r;
    if (auto r = checkVerdict(text, judgment_)) return r;
    if (auto r = checkOnly(text, only_)) return r;
    if (auto r = checkRetreat(text, forward_to_)) return r;
    if (auto r = checkFiller(text)) return r;
    if (auto r = checkStanding(text, standing_)) return r;
    if (lang_ == Lang::Fr)
        if (auto r = checkElision(text)) return r;
    if (auto r = checkBoards(text, facts_, allowed_)) return r;
    if (auto r = checkLost(text, lost_)) return r;
    return checkWording(text, lang_);
}

std::optional<Refusal> Brief::checkTemplate(std::string_view sentence) const {
    // The template keeps to the facts' words and moves by itself: only what its facts may get wrong is read.
    if (auto r = checkBoards(sentence, facts_, allowed_)) return r;
    if (auto r = checkLost(sentence, lost_)) return r;
    return checkWording(sentence, lang_);
}

std::optional<std::string> Brief::heldTemplate(std::string_view text) const {
    return brief_detail::keptSentences(text, [&](std::string_view s) { return checkTemplate(s).has_value(); });
}

}  // namespace fcg::commentary
