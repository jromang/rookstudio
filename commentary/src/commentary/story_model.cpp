#include "story_model.hpp"

#include <algorithm>
#include <array>

#include <fmt/include/fmt/format.h>

#include "facts.hpp"
#include "text.hpp"
#include "writing.hpp"

namespace fcg::commentary {

using story_detail::Source;
using story_detail::Told;

namespace {

const char* storySystem(Lang lang) {
    if (lang == Lang::Fr)
        return "Tu racontes l'histoire d'une partie d'échecs à ses joueurs, un passage à la fois : une à trois phrases "
               "courtes, simples et sobres, en français, sur le moment que décrivent les faits. N'utilise que les "
               "faits donnés. N'invente aucun coup, aucune case, aucun nom, aucun nombre, aucun motif, aucune idée "
               "stratégique qui n'y figure pas. Pas de chiffres, pas de pourcentages ; cite au plus deux coups.";
    return "You tell the story of a chess game to its players, one passage at a time: one to three short, simple, "
           "sober "
           "sentences in English about the moment the facts describe. Use only the facts given. Never invent a move, a "
           "square, a name, a number, a motif or a strategic idea that is not in them. No digits, no percentages; "
           "quote at most two moves.";
}

// « les Blancs », "White"; « des Blancs »; « aux Blancs ».
const char* sides(Color c, Lang lang) { return sideName(c, lang); }
const char* ofSides(Color c) { return c == Color::White ? "des Blancs" : "des Noirs"; }
const char* toSides(Color c) { return c == Color::White ? "aux Blancs" : "aux Noirs"; }

// What the passage is in the story, as its first line says.
std::string role(const Source& s, Lang lang) {
    const bool fr = lang == Lang::Fr;
    const auto* m = s.move;
    switch (s.kind) {
        case Source::Kind::Opening:
            return fr ? "Dans l'histoire : l'ouverture, et où en sont les deux camps à sa sortie."
                      : "In the story: the opening, and how the sides stand when it is over.";
        case Source::Kind::Swing: {
            const auto mover = m->mover, other = !m->mover;
            switch (s.swing) {
                case Source::Swing::Lead:
                    return fr ? fmt::format("Dans l'histoire : ce coup donne {} le premier avantage net de la partie.",
                                            toSides(other))
                              : fmt::format("In the story: this move gives {} the first clear advantage of the game.",
                                            sides(other, lang));
                case Source::Swing::Slip:
                    return fr ? fmt::format("Dans l'histoire : par ce coup, {} laissent filer un net avantage.",
                                            sides(mover, lang))
                              : fmt::format("In the story: with this move, {} lets a clear advantage slip.",
                                            sides(mover, lang));
                default:
                    // A side already worse does not turn the game: it loses it for good.
                    if (m->win_before < 45.0)
                        return fr ? fmt::format("Dans l'histoire : l'erreur décisive ; {} étaient déjà moins bien.",
                                                sides(mover, lang))
                                  : fmt::format("In the story: the decisive mistake; {} was already worse.",
                                                sides(mover, lang));
                    return fr ? fmt::format("Dans l'histoire : le tournant ; {} ne sont jamais revenus ensuite.",
                                            sides(mover, lang))
                              : fmt::format("In the story: the turning point; {} never came back after it.",
                                            sides(mover, lang));
            }
        }
        case Source::Kind::Gradual:
            return fr ? fmt::format(
                            "Dans l'histoire : une partie décidée peu à peu, sans grosse faute ; de tous les "
                            "coups {}, celui-ci coûte le plus.",
                            ofSides(m->mover))
                      : fmt::format(
                            "In the story: a game decided little by little, without a big mistake; of all of "
                            "{}'s moves, this one costs most.",
                            sides(m->mover, lang));
        case Source::Kind::Plan:
            return fr ? "Dans l'histoire : un plan que les livres connaissent."
                      : "In the story: a plan the books have a name for.";
        case Source::Kind::Endgame:
            return fr ? "Dans l'histoire : la finale." : "In the story: the endgame.";
        case Source::Kind::Lesson:
            return fr ? "Dans l'histoire : ce qu'un camp doit travailler."
                      : "In the story: what a side should work on.";
        default:
            return fr ? "Dans l'histoire : la fin de la partie." : "In the story: how the game ended.";
    }
}

// Whether a word has a capital after its first letter: "Caro-Kann", "McShane". The grammar's words cannot spell one.
bool innerCapital(std::string_view w) {
    bool first = true;
    for (std::size_t at = 0; at < w.size();) {
        const auto [c, n] = text::decode(w, at);
        if (!first && text::isUppercase(c)) return true;
        first = false;
        at += n;
    }
    return false;
}

void namesOf(std::string_view name, std::vector<std::string>& out) {
    for (const auto w : text::split(name, [](char32_t c) { return c == U' ' || c == U','; }))
        if (innerCapital(w)) out.emplace_back(w);
}

void addMove(brief_detail::Allowed& allowed, const Position& before, const Move& mv, PieceLetters letters) {
    for (auto& f : brief_detail::forms(moveText(before, mv, letters))) allowed.moves.push_back(std::move(f));
    allowed.squares.emplace_back(name(mv.from()));
    allowed.squares.emplace_back(name(mv.to()));
}

void addBoard(brief_detail::Allowed& allowed, const Position& pos) {
    for (int i = 0; i < 64; ++i)
        if (const auto p = Placed::at(pos.board(), square(i))) allowed.pieces.push_back(*p);
}

void tidy(std::vector<std::string>& list) {
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
}

// A position called won where the facts never call one so: "throws away the win" for an advantage let go, « une
// position gagnante » for a clear advantage. Not side by side with the facts' words: the passage's facts say "winning"
// or « gagnante » of some side, or it may not.
std::optional<Refusal> checkWinning(std::string_view text, const std::vector<std::string>& told) {
    const auto said = [&](std::string_view w) { return std::find(told.begin(), told.end(), w) != told.end(); };
    if (said("winning") || said("gagnante") || said("gagnant")) return std::nullopt;
    static constexpr std::string_view WON[] = {"winning",  "gagnante", "gagnant", "gagnantes",
                                               "gagnants", "gagnée",   "gagnées"};
    const auto words                        = brief_detail::wordsOf(text);
    for (std::size_t i = 0; i < words.size(); ++i) {
        const auto& w   = words[i];
        const bool noun = w == "win" && i > 0 && (words[i - 1] == "the" || words[i - 1] == "a");
        if (noun || std::find(std::begin(WON), std::end(WON), w) != std::end(WON))
            return Refusal{Refusal::Kind::Evaluation, w};
    }
    return std::nullopt;
}

std::string joinedLines(const std::vector<std::string>& lines) {
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) out += (i ? "\n" : "") + lines[i];
    return out;
}

}  // namespace

StoryBrief::StoryBrief(const Told& told, const MainLine& line, const Analyses& analyses, const Tablebases* tables,
                       Lang lang, PieceLetters letters, const Players& players)
    : lang_(lang) {
    const bool fr  = lang == Lang::Fr;
    const auto& s  = told.source;
    const auto& ps = line.positions();
    system         = storySystem(lang);
    std::vector<std::string> lines;
    // The players by their names, when the game gives them.
    const auto white = story_detail::playerName(players.white), black = story_detail::playerName(players.black);
    if (!white.empty() || !black.empty()) {
        std::vector<std::string> named;
        if (!white.empty())
            named.push_back(fr ? fmt::format("{} avec les Blancs", white) : fmt::format("{} with White", white));
        if (!black.empty())
            named.push_back(fr ? fmt::format("{} avec les Noirs", black) : fmt::format("{} with Black", black));
        lines.push_back(fmt::format(fmt::runtime(fr ? "Joueurs : {}." : "Players: {}."),
                                    named.size() == 2 ? named[0] + ", " + named[1] : named[0]));
    }
    lines.push_back(role(s, lang));
    const bool about_a_move =
        (s.kind == Source::Kind::Swing || s.kind == Source::Kind::Gradual || s.kind == Source::Kind::Plan) && s.move &&
        s.move->ply >= 1 && s.move->ply < ps.size();
    if (about_a_move) {
        // A passage about a move: the facts its comment is written from, and that comment's checks.
        const auto ctx = moveContext(line, *s.move, analyses, nullptr, tables, nullptr);
        const Brief brief(ctx, lang, letters);
        if (s.kind == Source::Kind::Plan)
            lines.push_back(fmt::format(fmt::runtime(fr ? "Le plan : {}" : "The plan: {}"), told.passage.text));
        auto facts = brief.prompt;
        if (const auto last = facts.rfind('\n'); last != std::string::npos) facts.erase(last);  // its "Comment:"
        lines.push_back(std::move(facts));
        allowed_    = brief.allowed_;
        facts_      = brief.facts_;
        mover_      = brief.mover_;
        judgment_   = brief.judgment_;
        standing_   = brief.standing_;
        only_       = brief.only_;
        forward_to_ = brief.forward_to_;
        lost_       = brief.lost_;
    } else {
        // The opening, the endgame, the end: the facts told in sentences, the moves they name, the board they leave.
        for (const auto& said : s.said) lines.push_back(said);
        for (const auto& f : s.facts) {
            facts_.achieves.push_back(f);
            for (const auto sq : brief_detail::factSquares(f)) allowed_.squares.emplace_back(name(sq));
            for (const auto& p : brief_detail::factPieces(f)) allowed_.pieces.push_back(p);
        }
        const auto* m = s.move && s.move->ply >= 1 && s.move->ply < ps.size() ? s.move : nullptr;
        if (m) {
            const auto& before = ps[m->ply - 1];
            const auto& mv     = line.moves()[m->ply - 1];
            addMove(allowed_, before, mv, letters);
            addBoard(allowed_, before);
            addBoard(allowed_, ps[m->ply]);
            allowed_.lines.push_back(
                brief_detail::Shown::of(brief_detail::Shown::Kind::Played, before, std::vector<Move>{mv}, 1, letters));
            lost_ = brief_detail::Lost::of(*m);
        }
        switch (s.kind) {
            case Source::Kind::Opening:
                if (m) {
                    lines.push_back(
                        fmt::format(fmt::runtime(fr ? "L'ouverture se termine sur {}." : "The opening ends with {}."),
                                    moveText(ps[m->ply - 1], line.moves()[m->ply - 1], letters)));
                    const double white_chances = m->mover == Color::White ? m->win_after : 100.0 - m->win_after;
                    standing_                  = std::pair{standingOf(white_chances), standingOf(white_chances)};
                }
                break;
            case Source::Kind::Endgame:
                if (m) {
                    const double white_chances = m->mover == Color::White ? m->win_after : 100.0 - m->win_after;
                    const auto standing        = standingOf(white_chances);
                    lines.push_back(
                        fr ? fmt::format("La position des Blancs : {}.", brief_detail::standingWords(standing, lang))
                           : fmt::format("White's position: {}.", brief_detail::standingWords(standing, lang)));
                    standing_ = std::pair{standing, standing};
                }
                break;
            case Source::Kind::End: {
                if (!line.moves().empty()) {
                    const auto last = line.moves().size();
                    const auto mv   = moveText(ps[last - 1], line.moves()[last - 1], letters);
                    lines.push_back(fmt::format(fmt::runtime(fr ? "Dernier coup : {} ({})." : "Last move: {} ({})."),
                                                mv, sides(ps[last - 1].turn(), lang)));
                    if (!m) {
                        addMove(allowed_, ps[last - 1], line.moves()[last - 1], letters);
                        addBoard(allowed_, ps[last - 1]);
                        addBoard(allowed_, ps[last]);
                        allowed_.lines.push_back(
                            brief_detail::Shown::of(brief_detail::Shown::Kind::Played, ps[last - 1],
                                                    std::vector<Move>{line.moves()[last - 1]}, 1, letters));
                    }
                    // Whose position the final one loses beyond doubt.
                    lost_ = brief_detail::Lost::at(
                        last < analyses.size() && analyses[last] ? analyses[last]->whiteExpected() : std::nullopt);
                    // What the tables said of the final position, when they know it.
                    if (const auto v = tables ? tables->verdict(ps[last]) : std::nullopt) {
                        const auto mover = ps[last].turn();
                        if (*v == Verdict::Draw)
                            lines.emplace_back(fr ? "Les tables : la position finale est nulle."
                                                  : "The tables: the final position is a draw.");
                        else {
                            const auto winner = *v == Verdict::Win ? mover : !mover;
                            lines.push_back(
                                fr ? fmt::format("Les tables : la position finale est gagnée pour {}.",
                                                 sides(winner, lang))
                                   : fmt::format("The tables: the final position is won for {}.", sides(winner, lang)));
                        }
                    }
                }
                // The last move's facts support what is said of it: a mate, what it wins.
                if (m) {
                    const auto ctx   = moveContext(line, *m, analyses, nullptr, tables, nullptr);
                    const auto facts = gather(ctx);
                    for (const auto* list : {&facts.achieves, &facts.punished_by, &facts.missed, &facts.otherwise})
                        for (const auto& f : *list) facts_.achieves.push_back(f);
                }
                if (s.mated) facts_.achieves.emplace_back(fact::Mate{*s.mated, 0, std::nullopt});
                break;
            }
            default:
                break;
        }
        // The opening's name, which may say "Pin Variation" without a pin.
        if (const auto name = openingAt(line, line.moves().size()); name && s.kind == Source::Kind::Opening) {
            namesOf(*name, allowed_.names);
            allowed_.name_words = brief_detail::wordsOf(*name);
        }
    }
    // The game ends in the passage about the end, or in one about its last moves.
    ends_ = s.kind == Source::Kind::End || (s.move && s.move->ply + 1 >= line.moves().size());
    namesOf(white, allowed_.names);
    namesOf(black, allowed_.names);
    tidy(allowed_.moves);
    tidy(allowed_.squares);
    tidy(allowed_.names);
    lines.emplace_back(fr ? "Histoire :" : "Story:");
    prompt  = joinedLines(lines);
    grammar = allowed_.grammar(lang);
    told_   = brief_detail::wordsOf(prompt);
}

std::optional<Refusal> StoryBrief::check(std::string_view text) const {
    using namespace brief_detail;
    if (!fitsGrammar(text, allowed_, lang_)) return Refusal{Refusal::Kind::Grammar, {}};
    if (auto r = brief_detail::check(text, facts_, allowed_, mover_)) return r;
    if (auto r = checkVerdict(text, judgment_)) return r;
    if (auto r = checkOnly(text, only_)) return r;
    if (auto r = checkRetreat(text, forward_to_)) return r;
    if (auto r = checkFiller(text)) return r;
    if (auto r = checkStanding(text, standing_)) return r;
    if (lang_ == Lang::Fr)
        if (auto r = checkElision(text)) return r;
    if (auto r = story_detail::checkNames(text, told_, allowed_)) return r;
    if (auto r = story_detail::checkNumbers(text, told_)) return r;
    if (auto r = checkWinning(text, told_)) return r;
    if (auto r = checkBoards(text, facts_, allowed_)) return r;
    if (auto r = checkLost(text, lost_)) return r;
    if (auto r = checkWording(text, lang_)) return r;
    return story_detail::checkEnd(text, ends_);
}

Request storyRequest(const StoryBrief& brief, uint32_t round, uint32_t attempt) {
    Request r;
    r.messages    = {{Speaker::System, brief.system}, {Speaker::User, brief.prompt}};
    r.grammar     = brief.grammar;
    r.max_tokens  = MAX_TOKENS;
    r.temperature = round == 0 && attempt == 1 ? 0.0f : 0.7f;
    r.seed        = round * STORY_ATTEMPTS + attempt;
    return r;
}

std::vector<StoryPassage> writtenStory(Model& model, const MainLine& line, std::string_view result,
                                       const GameReport& report, const Analyses& analyses, const Tablebases* tables,
                                       Lang lang, PieceLetters letters, const Players& players, uint32_t round,
                                       const std::atomic<bool>* cancel, std::atomic<int>* done, std::string* error) {
    std::vector<StoryPassage> out;
    bool failed = false;  // the model gave no answer: not asked again
    for (const auto& t : story_detail::told(line, result, report, analyses, tables, lang, letters)) {
        if (t.passage.lesson) continue;
        StoryPassage p;
        p.passage = t.passage;
        p.templ   = t.passage.text;
        if (!failed && !(cancel && cancel->load())) {
            const StoryBrief brief(t, line, analyses, tables, lang, letters, players);
            for (uint32_t attempt = 1; attempt <= STORY_ATTEMPTS; ++attempt) {
                if (cancel && cancel->load()) break;
                std::string why;
                const auto answer = model.generate(storyRequest(brief, round, attempt), &why);
                p.attempts        = attempt;
                if (!answer) {
                    failed = true;
                    if (error) *error = why;
                    break;
                }
                if (const auto refused = brief.check(answer->text)) {
                    p.refused.emplace_back(answer->text, *refused);
                    continue;
                }
                p.passage.text = answer->text;
                p.by_model     = true;
                break;
            }
        }
        out.push_back(std::move(p));
        if (done) ++*done;
    }
    return out;
}

namespace story_detail {

std::string playerName(std::string_view tag) {
    auto name = text::trim(tag);
    if (const auto comma = name.find(','); comma != std::string_view::npos) name = text::trim(name.substr(0, comma));
    if (name == "?" || name == "-") return {};
    return std::string(name);
}

std::optional<Refusal> checkNames(std::string_view text, const std::vector<std::string>& told,
                                  const brief_detail::Allowed& allowed) {
    // The sides, in either language, whatever the prompt said of them.
    static constexpr std::array<std::string_view, 4> SIDES = {"white", "black", "blancs", "noirs"};
    const auto known                                       = [&](std::string_view w) {
        const auto lower = text::toLower(w);
        return std::find(told.begin(), told.end(), lower) != told.end() ||
               std::find(SIDES.begin(), SIDES.end(), lower) != SIDES.end();
    };
    const auto listed = [](const std::vector<std::string>& list, std::string_view w) {
        return std::find(list.begin(), list.end(), w) != list.end();
    };
    bool opens = true;  // the word opens a sentence: a capital there is only a capital
    for (const auto raw : text::splitWhitespace(text)) {
        const auto bare = text::trimEndMatches(raw, ".,;:!?");
        // A move, a square or a name the grammar has as a whole: "Rxe5", "Caro-Kann".
        if (listed(allowed.moves, bare) || listed(allowed.squares, bare) || listed(allowed.names, bare)) {
            opens = raw.ends_with('.') || raw.ends_with('!') || raw.ends_with('?');
            continue;
        }
        // Each piece of the word between apostrophes and hyphens: « d'Alekhine », "Carlsen's", "Nimzo-Indian".
        bool first = true;
        for (const auto piece :
             text::split(text::trimMatches(raw, ",;:!?.()«»“”\""), [](char32_t c) { return text::holds("'’-", c); })) {
            if (piece.empty()) continue;
            const auto [c, n]  = text::decode(piece);
            const bool capital = text::isUppercase(c);
            if (capital && !(first && opens) && !known(piece)) return Refusal{Refusal::Kind::Name, std::string(piece)};
            first = false;
        }
        opens = raw.ends_with('.') || raw.ends_with('!') || raw.ends_with('?');
    }
    return std::nullopt;
}

std::optional<Refusal> checkEnd(std::string_view text, bool ends) {
    if (ends) return std::nullopt;
    const auto w  = brief_detail::wordsOf(text);
    const auto at = [&](std::size_t i, std::string_view word) { return i < w.size() && w[i] == word; };
    for (std::size_t i = 0; i < w.size(); ++i) {
        // « la partie se termine », « s'achève », « finit », « prend fin »; "the game ends", "is over", "finishes".
        if (at(i, "partie") && ((at(i + 1, "se") && at(i + 2, "termine")) || (at(i + 1, "s") && at(i + 2, "achève")) ||
                                at(i + 1, "finit") || (at(i + 1, "prend") && at(i + 2, "fin"))))
            return Refusal{Refusal::Kind::Wording, "la partie se termine"};
        if (at(i, "game") && (at(i + 1, "ends") || at(i + 1, "finishes") || (at(i + 1, "is") && at(i + 2, "over"))))
            return Refusal{Refusal::Kind::Wording, "the game ends"};
    }
    return std::nullopt;
}

std::optional<Refusal> checkNumbers(std::string_view text, const std::vector<std::string>& told) {
    // Numbers in words, which the grammar's digits cannot be: the first ordinals and the ranks' are words of the game.
    static constexpr std::string_view NUMBERS[] = {
        "two",        "three",       "four",         "five",        "six",        "seven",      "eight",
        "nine",       "ten",         "eleven",       "twelve",      "thirteen",   "fourteen",   "fifteen",
        "sixteen",    "seventeen",   "eighteen",     "nineteen",    "twenty",     "thirty",     "forty",
        "fifty",      "sixty",       "seventy",      "eighty",      "ninety",     "hundred",    "fourth",
        "fifth",      "sixth",       "ninth",        "tenth",       "eleventh",   "twelfth",    "thirteenth",
        "fourteenth", "fifteenth",   "sixteenth",    "seventeenth", "eighteenth", "nineteenth", "twentieth",
        "thirtieth",  "fortieth",    "fiftieth",     "sixtieth",    "seventieth", "eightieth",  "ninetieth",
        "deux",       "trois",       "quatre",       "cinq",        "six",        "sept",       "huit",
        "neuf",       "dix",         "onze",         "douze",       "treize",     "quatorze",   "quinze",
        "seize",      "vingt",       "vingts",       "trente",      "quarante",   "cinquante",  "soixante",
        "cent",       "cents",       "quatrième",    "cinquième",   "sixième",    "neuvième",   "dixième",
        "onzième",    "douzième",    "treizième",    "quatorzième", "quinzième",  "seizième",   "vingtième",
        "trentième",  "quarantième", "cinquantième", "soixantième", "centième",   "dizaine",    "douzaine",
        "vingtaine"};
    // « les deux camps », "the two players": both, not a count.
    static constexpr std::string_view BOTH[] = {"sides", "players", "camps", "joueurs", "adversaires", "rois", "kings"};
    const auto words                         = brief_detail::wordsOf(text);
    for (std::size_t i = 0; i < words.size(); ++i) {
        const auto& w = words[i];
        if (std::find(std::begin(NUMBERS), std::end(NUMBERS), w) == std::end(NUMBERS)) continue;
        if (std::find(told.begin(), told.end(), w) != told.end()) continue;
        if ((w == "two" || w == "deux") && i + 1 < words.size() &&
            std::find(std::begin(BOTH), std::end(BOTH), words[i + 1]) != std::end(BOTH))
            continue;
        return Refusal{Refusal::Kind::Number, w};
    }
    return std::nullopt;
}

}  // namespace story_detail

}  // namespace fcg::commentary
