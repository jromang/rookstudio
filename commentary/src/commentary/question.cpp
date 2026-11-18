#include "question.hpp"

#include <algorithm>

#include <fmt/include/fmt/format.h>

#include "openings.hpp"
#include "review.hpp"
#include "text.hpp"

namespace fcg::commentary {

namespace {

constexpr const char* INSTRUCTIONS =
    "You turn a chess player's question about the position on the board into one action, and write the action only.\n"
    "- explain: why the move just played is good or bad, why it is a mistake, what it does\n"
    "- best: what to play now, the best move\n"
    "- threat: what the opponent threatens, the danger\n"
    "- try MOVE: what a move would do, why not that move, what if it is played\n"
    "- compare MOVE MOVE: which of two moves is better\n"
    "- games: which games reached the position, who played it, the reference games\n"
    "- played: what is usually played here, the database's moves and how they scored\n"
    "- played MOVE: how often a move was played here and how it scored\n"
    "- opening: the name of the opening\n"
    "- tablebase: what the endgame tablebases say, whether the endgame is won or drawn\n"
    "Questions come in French or English; moves use English letters (K Q R B N) or French ones (R D T F C).\n"
    "Examples:\n"
    "Pourquoi pas Cf5 ? -> try Cf5\n"
    "What if I play h4? -> try h4\n"
    "What happens after Nf3? -> try Nf3\n"
    "Et si les Blancs jouent Fb5 ? -> try Fb5\n"
    "Que menacent les Noirs ? -> threat\n"
    "What does White threaten? -> threat\n"
    "What should White play here? -> best\n"
    "Quel est le meilleur coup ? -> best\n"
    "Cf5 ou Cd5 ? -> compare Cf5 Cd5\n"
    "Is Nf3 better than Nc3? -> compare Nf3 Nc3\n"
    "Pourquoi ce coup est-il une erreur ? -> explain\n"
    "Why this move? -> explain\n"
    "Qui a joué cette position ? -> games\n"
    "Show me strong games with this position -> games\n"
    "Que joue-t-on ici d'habitude ? -> played\n"
    "What do the masters usually play here? -> played\n"
    "Combien de fois a-t-on joué Fe2 ? -> played Fe2\n"
    "How did Qd2 score? -> played Qd2\n"
    "Que donne Fe2 en pratique ? -> played Fe2\n"
    "Quelle est cette ouverture ? -> opening\n"
    "What opening is this? -> opening\n"
    "Cette finale est-elle gagnée ? -> tablebase\n"
    "Is this endgame a draw? -> tablebase";

// An engine line from `start`, scored for White as the review's lines are.
EngineLine whiteLine(const Position& start, const Searched& found, std::vector<Move> moves) {
    const int32_t sign = start.turn() == Color::White ? 1 : -1;
    EngineLine line;
    line.moves = std::move(moves);
    line.score = found.score.kind == Score::Kind::Cp ? Score::cp(sign * found.score.value)
                                                     : Score::mate(sign * found.score.value);
    line.depth = 0;
    return line;
}

struct Judged {
    MoveReport report;
    EngineLine best, reply;
};

// What the engine makes of `mv` from `before`, the way the review judges a game move: from the review's own lines when
// it analyzed the position, so that the answer keeps to it, else from the engine's.
std::optional<Judged> judged(const Position& before, const Move& mv, const Oracle& oracle,
                             std::span<const std::pair<Position, PositionAnalysis>> known) {
    const std::vector<EngineLine>* reviewed = nullptr;
    for (const auto& [pos, analysis] : known) {
        if (pos == before) {
            if (!analysis.lines.empty()) reviewed = &analysis.lines;
            break;
        }
    }
    const auto searched = [&](std::span<const Move> only) -> std::optional<EngineLine> {
        const auto found = oracle.search(before, only);
        if (!found) return std::nullopt;
        return whiteLine(before, *found, found->moves);
    };
    std::optional<EngineLine> best;
    if (reviewed)
        best = reviewed->front();
    else
        best = searched({});
    if (!best) return std::nullopt;
    std::optional<EngineLine> tried;
    if (reviewed) {
        for (const auto& l : *reviewed) {
            if (!l.moves.empty() && l.moves.front() == mv) {
                tried = l;
                break;
            }
        }
    }
    if (!tried && !best->moves.empty() && best->moves.front() == mv) tried = best;
    if (!tried) tried = searched(std::span<const Move>(&mv, 1));
    if (!tried) return std::nullopt;
    auto after = before;
    after.playUnchecked(mv);
    EngineLine reply = *tried;
    reply.moves =
        tried->moves.empty() ? std::vector<Move>{} : std::vector<Move>(tried->moves.begin() + 1, tried->moves.end());
    PositionAnalysis after_analysis;
    if (const auto end = gameEnd(after))
        after_analysis = PositionAnalysis::ended(*end);
    else if (reply.moves.empty())
        return std::nullopt;
    else
        after_analysis.lines = {reply};
    const MainLine game(before, {mv});
    const Analyses analyses{PositionAnalysis{{*best}, std::nullopt}, after_analysis};
    const auto report = review(game, analyses, Opening{});
    if (report.moves.empty()) return std::nullopt;
    return Judged{report.moves.front(), *best, reply};
}

// The model's comment on `mv` played from `before`, as on a game move, and how the mover stands after it.
std::variant<std::pair<std::string, double>, Problem> commentOn(
    const Position& before, const Move& mv, const std::optional<Move>& previous, const Oracle& oracle,
    std::span<const std::pair<Position, PositionAnalysis>> known, Model& model, const Tablebases* tables, Lang lang,
    PieceLetters letters) {
    const auto j = judged(before, mv, oracle, known);
    if (!j) return Problem::NoEngine;
    MoveContext ctx;
    ctx.before         = &before;
    ctx.played         = mv;
    ctx.report         = &j->report;
    ctx.best           = &j->best;
    ctx.reply          = &j->reply;
    ctx.tables         = tables;
    ctx.previous       = previous;
    ctx.oracle         = &oracle;
    const auto written = writtenComment(model, ctx, lang, letters);
    return std::pair{written ? written->text : std::string(), j->report.win_after};
}

std::string trimmed(std::string_view s) { return std::string(text::trim(s)); }

// What the tablebases say of the position, and the moves that keep their verdict when not every move does.
std::variant<Answered, Problem> tablebaseAnswer(const Position& pos, const Tablebases* tables, Lang lang,
                                                PieceLetters letters) {
    if (!tables) return Problem::NotInTables;
    const auto verdict = tables->verdict(pos);
    if (!verdict) return Problem::NotInTables;
    const bool fr     = lang == Lang::Fr;
    const auto side   = pos.turn();
    const auto winner = *verdict == Verdict::Win ? side : !side;
    Answered a;
    if (*verdict == Verdict::Draw)
        a.text = fr ? "Les tables de finales : nulle." : "The tablebases: a draw.";
    else if (winner == Color::White)
        a.text = fr ? "Les tables de finales : les Blancs gagnent." : "The tablebases: White wins.";
    else
        a.text = fr ? "Les tables de finales : les Noirs gagnent." : "The tablebases: Black wins.";
    if (*verdict == Verdict::Loss) return a;
    if (const auto best = tables->best(pos)) {
        const auto name = moveText(pos, *best, letters);
        a.text +=
            *verdict == Verdict::Win
                ? (fr ? fmt::format(" Le coup gagnant : {}.", name) : fmt::format(" The winning move: {}.", name))
                : (fr ? fmt::format(" Le coup qui tient : {}.", name) : fmt::format(" The move that holds: {}.", name));
        a.moves = {*best};
        a.best  = best;
        return a;
    }
    // The moves that keep the verdict, told when some spoil it.
    const auto legal = pos.legalMoves();
    std::vector<Move> keep;
    for (const auto& mv : legal)
        if (tables->after(pos, mv) == verdict) keep.push_back(mv);
    if (keep.empty() || keep.size() == legal.size()) return a;
    std::string names;
    for (std::size_t i = 0; i < keep.size() && i < 4; ++i) names += (i ? ", " : "") + moveText(pos, keep[i], letters);
    const bool one = keep.size() == 1, win = *verdict == Verdict::Win;
    if (fr && one)
        a.text += win ? fmt::format(" Seul {} gagne.", names) : fmt::format(" Seul {} tient.", names);
    else if (fr)
        a.text +=
            win ? fmt::format(" Les coups gagnants : {}.", names) : fmt::format(" Les coups qui tiennent : {}.", names);
    else if (one)
        a.text += win ? fmt::format(" Only {} wins.", names) : fmt::format(" Only {} holds.", names);
    else
        a.text += win ? fmt::format(" The winning moves: {}.", names) : fmt::format(" The moves that hold: {}.", names);
    a.moves.assign(keep.begin(), keep.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(keep.size(), 4)));
    return a;
}

}  // namespace

MoveNames moveNames(const Position& pos, Lang lang) {
    const PieceLetters spellings[2] = {lang == Lang::Fr ? PieceLetters::French : PieceLetters::English,
                                       lang == Lang::Fr ? PieceLetters::English : PieceLetters::French};
    MoveNames names;
    for (const auto spelling : spellings) {
        for (const auto& mv : pos.legalMoves()) {
            auto name = san(pos, mv, spelling);
            while (!name.empty() && (name.back() == '+' || name.back() == '#')) name.pop_back();
            names.try_emplace(std::move(name), mv);
        }
    }
    return names;
}

std::string actionGrammar(const MoveNames& names) {
    // The names in order: a map keeps them so.
    std::string moves;
    for (const auto& [name, mv] : names) moves += (moves.empty() ? "" : " | ") + fmt::format("\"{}\"", name);
    constexpr const char* plain =
        "root ::= \"explain\" | \"best\" | \"threat\" | \"games\" | \"played\" | \"opening\" | \"tablebase\"";
    // No move to name: the actions about moves are left out; a comparison wants two moves.
    if (moves.empty()) return std::string(plain) + "\n";
    std::vector<Move> distinct;
    for (const auto& [name, mv] : names)
        if (std::find(distinct.begin(), distinct.end(), mv) == distinct.end()) distinct.push_back(mv);
    return fmt::format("{} | \"try \" move | \"played \" move{}\nmove ::= {}\n", plain,
                       distinct.size() >= 2 ? " | \"compare \" move \" \" move" : "", moves);
}

MoveNames namedIn(std::string_view question, const MoveNames& names) {
    MoveNames out;
    for (const auto& [name, mv] : names) {
        if (!mentions(question, name)) continue;
        // Every name of the move: the model may write it in the other notation.
        for (const auto& [other, same] : names)
            if (same == mv) out.emplace(other, same);
    }
    return out;
}

Request actionRequest(std::string_view question, const Position& pos, std::optional<std::string_view> played,
                      const MoveNames& names) {
    const char* side       = pos.turn() == Color::White ? "White" : "Black";
    const auto just_played = played ? fmt::format("Move just played: {}. ", *played) : std::string();
    Request r;
    r.messages    = {{Speaker::System, INSTRUCTIONS},
                     {Speaker::User, fmt::format("{}{} to move.\nQuestion: {}\nAction:", just_played, side, question)}};
    r.grammar     = actionGrammar(namedIn(question, names));
    r.max_tokens  = 16;
    r.temperature = 0.0f;
    r.seed        = 1;
    return r;
}

namespace {

// Where `question` first names `name` as a word of its own.
std::optional<std::size_t> mentionAt(std::string_view question, std::string_view name) {
    if (name.empty()) return std::nullopt;
    // Rust's match_indices: the occurrences that do not overlap.
    for (std::size_t at = question.find(name); at != std::string_view::npos;
         at             = question.find(name, at + name.size())) {
        const auto before = question.substr(0, at);
        const auto after  = question.substr(at + name.size());
        const bool open = before.empty() || !text::isAlphanumeric(text::decode(before, text::lastStart(before)).first);
        const bool closed = after.empty() || !text::isAlphanumeric(text::decode(after).first);
        if (open && closed) return at;
    }
    return std::nullopt;
}

bool says(const std::string& lower, std::initializer_list<std::string_view> words) {
    return std::any_of(words.begin(), words.end(),
                       [&](std::string_view w) { return lower.find(w) != std::string::npos; });
}

}  // namespace

bool mentions(std::string_view question, std::string_view name) { return mentionAt(question, name).has_value(); }

std::optional<Action> namedAction(std::string_view question, const MoveNames& names) {
    // The moves named, in the order the question names them.
    std::vector<std::pair<std::size_t, Move>> named;
    for (const auto& [name, mv] : names) {
        const auto at = mentionAt(question, name);
        if (!at) continue;
        const auto same = std::find_if(named.begin(), named.end(), [&](const auto& n) { return n.second == mv; });
        if (same == named.end())
            named.emplace_back(*at, mv);
        else
            same->first = std::min(same->first, *at);
    }
    std::sort(named.begin(), named.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const auto lower = text::toLower(question);
    if (named.size() == 2 && says(lower, {" ou ", " or ", "better", "mieux", "plutôt", "stronger", "plus fort", " vs",
                                          "versus", "prefer", "préfér", "rather than", "instead of", "au lieu de"}))
        return Action{Action::Kind::Compare, named[0].second, named[1].second};
    if (named.size() == 1 && says(lower, {"combien de fois", "how often", "how many times", "en pratique",
                                          "in practice", "dans les parties", "in the games", "in master games"}))
        return Action{Action::Kind::Played, named[0].second};
    return std::nullopt;
}

std::optional<Action> plainAction(std::string_view question, const MoveNames& names) {
    for (const auto& [name, mv] : names) {
        if (mentions(question, name)) return std::nullopt;
    }
    const auto lower = text::toLower(question);
    const auto says  = [&](std::initializer_list<std::string_view> words) {
        return std::any_of(words.begin(), words.end(),
                           [&](std::string_view w) { return lower.find(w) != std::string::npos; });
    };
    if (says({"menac", "threat"})) return Action{Action::Kind::Threat};
    if (says({"ouverture", "opening", "comment s'appelle", "quel est le nom", "name of this", "is this line called"}))
        return Action{Action::Kind::Opening};
    if (says({"qui a joué", "who played", "parties de référence", "reference games", "quelles parties", "which games"}))
        return Action{Action::Kind::Games};
    if (says({"que joue-t-on", "what is played", "what do people play", "coups joués", "moves played", "les plus joués",
              "most played", "most common", "statistiques", "statistics"}))
        return Action{Action::Kind::Played};
    if (says({"tables de finales", "tablebase"})) return Action{Action::Kind::Tablebase};
    if (says({"meilleur coup", "best move", "que jouer", "what to play"})) return Action{Action::Kind::Best};
    if (says({"pourquoi ce coup", "why this move"})) return Action{Action::Kind::Explain};
    return std::nullopt;
}

std::optional<Action> readAction(std::string_view text, std::string_view question, const MoveNames& names) {
    const auto words = text::splitWhitespace(text);
    std::size_t next = 0;
    const auto word  = [&]() -> std::optional<std::string_view> {
        if (next >= words.size()) return std::nullopt;
        return words[next++];
    };
    // Any of the move's names will do: the model may write it in the other notation.
    const auto named = [&](std::optional<std::string_view> w) -> std::optional<Move> {
        if (!w) return std::nullopt;
        const auto it = names.find(std::string(*w));
        if (it == names.end()) return std::nullopt;
        const auto mv     = it->second;
        const bool spoken = std::any_of(names.begin(), names.end(), [&](const auto& entry) {
            return entry.second == mv && mentions(question, entry.first);
        });
        if (!spoken) return std::nullopt;
        return mv;
    };
    const auto verb = word();
    if (verb == "explain") return Action{Action::Kind::Explain};
    if (verb == "best") return Action{Action::Kind::Best};
    if (verb == "threat") return Action{Action::Kind::Threat};
    if (verb == "try") {
        const auto mv = named(word());
        if (!mv) return std::nullopt;
        return Action{Action::Kind::Try, mv};
    }
    if (verb == "games") return Action{Action::Kind::Games};
    if (verb == "opening") return Action{Action::Kind::Opening};
    if (verb == "tablebase") return Action{Action::Kind::Tablebase};
    if (verb == "played") {
        const auto w = word();
        if (!w) return Action{Action::Kind::Played};
        const auto mv = named(w);
        if (!mv) return std::nullopt;
        return Action{Action::Kind::Played, mv};
    }
    if (verb == "compare") {
        const auto first = named(word());
        if (!first) return std::nullopt;
        const auto second = named(word());
        if (!second) return std::nullopt;
        // A move set against itself: what it does.
        if (*second == *first) return Action{Action::Kind::Try, first};
        return Action{Action::Kind::Compare, first, second};
    }
    return std::nullopt;
}

namespace {

// The answer, and in `action` what the question was taken to ask.
std::variant<Answered, Problem> answered(const Asked& asked, Model& model, const Oracle* oracle,
                                         const Tablebases* tables, std::optional<Action>& action) {
    const auto& pos    = *asked.before;
    const auto lang    = asked.lang;
    const auto letters = asked.letters;
    const bool fr      = lang == Lang::Fr;
    // The moves of the position, and those of the one before, where the move just played was chosen: a name both have
    // is the position's.
    const auto now = moveNames(pos, lang);
    MoveNames names;
    if (asked.earlier) names = moveNames(*asked.earlier, lang);
    for (const auto& [name, mv] : now) names[name] = mv;
    std::optional<std::string> played;
    if (asked.earlier && asked.previous) played = moveText(*asked.earlier, *asked.previous, PieceLetters::English);
    action = plainAction(asked.question, names);
    if (!action) action = namedAction(asked.question, names);
    if (!action) {
        std::optional<std::string_view> said;
        if (played) said = *played;
        const auto text = model.generate(actionRequest(asked.question, pos, said, names), nullptr);
        if (!text) return Problem::NoEngine;
        action = readAction(text->text, asked.question, names);
        if (!action) return Problem::UnknownMove;
    }
    // Where a move of the action is played: the position, or else the one before.
    const auto legal_now = [&](const Move& mv) {
        return std::any_of(now.begin(), now.end(), [&](const auto& entry) { return entry.second == mv; });
    };
    struct From {
        const Position* pos;
        std::optional<Move> previous;
    };
    const auto from_of = [&](const Move& mv) -> std::optional<From> {
        if (legal_now(mv)) return From{&pos, asked.previous};
        if (asked.earlier) return From{asked.earlier, asked.earlier_previous};
        return std::nullopt;
    };
    // What the databases and the tablebases hold: no engine, no model for the answer.
    switch (action->kind) {
        case Action::Kind::Games:
        case Action::Kind::Played: {
            if (asked.bases.empty()) return Problem::NoDatabase;
            if (action->first && !legal_now(*action->first)) return Problem::UnknownMove;
            const auto refs = references(pos, asked.bases);
            Answered a;
            if (action->kind == Action::Kind::Games) {
                a.text  = gamesAnswer(refs, asked.bases, lang);
                a.games = refs.games;
            } else {
                a.text = movesAnswer(pos, refs, asked.bases, action->first, lang, letters);
                if (action->first) {
                    a.moves = {*action->first};
                } else {
                    for (std::size_t i = 0; i < refs.moves.size() && i < 3; ++i) a.moves.push_back(refs.moves[i].move);
                }
            }
            return a;
        }
        case Action::Kind::Opening: {
            Answered a;
            a.text = openingAnswer(asked.line.empty() ? std::nullopt : openingName(asked.line), lang);
            return a;
        }
        case Action::Kind::Tablebase:
            return tablebaseAnswer(pos, tables, lang, letters);
        default:
            break;
    }
    if (action->kind == Action::Kind::Explain) {
        if (!asked.explain) {
            // No review of the game: the engine's comment on the move that led to the position, as for a move tried
            // there.
            if (!asked.earlier || !asked.previous) return Problem::Unanalyzed;
            if (!oracle) return Problem::NoEngine;
            const auto said = commentOn(*asked.earlier, *asked.previous, asked.earlier_previous, *oracle, asked.known,
                                        model, tables, lang, letters);
            if (const auto* p = std::get_if<Problem>(&said)) return *p;
            Answered a;
            a.text    = std::get<0>(said).first;
            a.moves   = {*asked.previous};
            a.earlier = true;
            return a;
        }
        auto ctx           = *asked.explain;
        ctx.tables         = tables;
        ctx.oracle         = oracle;
        const auto written = writtenComment(model, ctx, lang, letters);
        Answered a;
        a.text = written ? written->text : std::string();
        return a;
    }
    if (!oracle) return Problem::NoEngine;
    const auto comment = [&](const Position& before, const Move& mv, const std::optional<Move>& previous) {
        return commentOn(before, mv, previous, *oracle, asked.known, model, tables, lang, letters);
    };
    switch (action->kind) {
        case Action::Kind::Best: {
            std::optional<Move> mv;
            for (const auto& [known, analysis] : asked.known) {
                if (known == pos) {
                    if (const auto* line = analysis.best(); line && !line->moves.empty()) mv = line->moves.front();
                    break;
                }
            }
            if (!mv) {
                const auto best = oracle->search(pos, {});
                if (!best || best->moves.empty()) return Problem::NoEngine;
                mv = best->moves.front();
            }
            const auto said = comment(pos, *mv, asked.previous);
            if (const auto* p = std::get_if<Problem>(&said)) return *p;
            const auto name = moveText(pos, *mv, letters);
            const auto head = fr ? fmt::format("Meilleur coup : {}.", name) : fmt::format("Best move: {}.", name);
            Answered a;
            a.text  = trimmed(head + " " + std::get<0>(said).first);
            a.moves = {*mv};
            a.best  = mv;
            return a;
        }
        case Action::Kind::Threat: {
            const auto side  = !pos.turn();
            const auto again = pos.swappedTurn();
            if (!again) return Problem::InCheck;
            const auto threat = movingAgain(pos, side, *oracle);
            if (!threat || threat->moves.empty()) return Problem::NoEngine;
            const auto mv   = threat->moves.front();
            const auto said = comment(*again, mv, std::nullopt);
            if (const auto* p = std::get_if<Problem>(&said)) return *p;
            // After the colon, the comment goes on the sentence: « Menace : le fou prend en f7 ».
            Answered a;
            a.text   = trimmed(std::string(fr ? "Menace :" : "Threat:") + " " + lowerFirst(std::get<0>(said).first));
            a.moves  = {mv};
            a.threat = true;
            return a;
        }
        case Action::Kind::Try: {
            const auto mv   = *action->first;
            const auto from = from_of(mv);
            if (!from) return Problem::UnknownMove;
            const auto said = comment(*from->pos, mv, from->previous);
            if (const auto* p = std::get_if<Problem>(&said)) return *p;
            Answered a;
            a.text    = std::get<0>(said).first;
            a.moves   = {mv};
            a.earlier = !legal_now(mv);
            return a;
        }
        default: {
            const auto a_mv = *action->first, b_mv = *action->second;
            const auto from = from_of(a_mv);
            if (!from) return Problem::UnknownMove;
            const auto other = from_of(b_mv);
            if (!other) return Problem::UnknownMove;
            if (!(*other->pos == *from->pos)) return Problem::UnknownMove;
            const auto first = comment(*from->pos, a_mv, from->previous);
            if (const auto* p = std::get_if<Problem>(&first)) return *p;
            const auto second = comment(*from->pos, b_mv, from->previous);
            if (const auto* p = std::get_if<Problem>(&second)) return *p;
            const auto& [first_text, a_after]  = std::get<0>(first);
            const auto& [second_text, b_after] = std::get<0>(second);
            const auto a_name = moveText(*from->pos, a_mv, letters), b_name = moveText(*from->pos, b_mv, letters);
            const double d = a_after - b_after;
            std::string verdict;
            if (d < 2.0 && d > -2.0)
                verdict = fr ? "Les deux coups se valent." : "The two moves are worth about the same.";
            else if (d > 0.0)
                verdict = fr ? fmt::format("{} vaut mieux que {}.", a_name, b_name)
                             : fmt::format("{} is the better move.", a_name);
            else
                verdict = fr ? fmt::format("{} vaut mieux que {}.", b_name, a_name)
                             : fmt::format("{} is the better move.", b_name);
            Answered a;
            a.text    = fmt::format("{} {} {}", first_text, second_text, verdict);
            a.moves   = {a_mv, b_mv};
            a.earlier = !legal_now(a_mv);
            return a;
        }
    }
}

}  // namespace

std::variant<Answered, Problem> answer(const Asked& asked, Model& model, const Oracle* oracle,
                                       const Tablebases* tables) {
    std::optional<Action> action;
    auto out = answered(asked, model, oracle, tables, action);
    if (auto* a = std::get_if<Answered>(&out); a && action) a->kind = action->kind;
    return out;
}

}  // namespace fcg::commentary
