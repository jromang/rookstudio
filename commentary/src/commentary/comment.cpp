#include "comment.hpp"

#include <algorithm>

#include "facts.hpp"
#include "material.hpp"
#include "structure.hpp"

namespace fcg::commentary {

namespace {

template <typename T>
const T* as(const Fact& f) {
    return std::get_if<T>(&f);
}

// What refutes a move, in sentences: how the move made the answer possible, then what the answer does, with what it
// costs unless it already says it. `after` is the position after the move, `answer` the opponent's reply, `punishment`
// what it brings, most telling first. Without `causes`, the cause is told only when it says by itself what is lost: the
// one sentence a move mentioned in passing gets.
std::vector<std::string> refutation(const Phrasebook& book, const std::vector<Fact>& punishment, const Position& after,
                                    std::optional<Move> answer, PieceLetters letters, bool causes) {
    std::vector<std::string> sentences;
    std::optional<std::string> answer_text;
    if (answer) answer_text = moveText(after, *answer, letters);
    const Fact* cause = nullptr;
    for (const auto& f : punishment) {
        if (isCause(f)) {
            cause = &f;
            break;
        }
    }
    std::vector<const Fact*> told;
    for (const auto& f : punishment) {
        if (!isCause(f)) told.push_back(&f);
    }
    // What the answer does at once tells more than how things then stand.
    if (!told.empty() && salience(*told.front()) < TACTIC && answer) {
        const auto it = std::find_if(told.begin(), told.end(), [&](const Fact* f) {
            return (as<fact::PawnBreak>(*f) || as<fact::Sacrifice>(*f)) && atOnce(*f, after, *answer);
        });
        if (it != told.end()) std::swap(told.front(), *it);
    }
    const Fact* cost = nullptr;
    for (const auto* f : told) {
        if (as<fact::WinsMaterial>(*f)) {
            cost = f;
            break;
        }
    }
    // The piece the cause leaves alone is the one that is lost, to the answer itself: one sentence says both.
    const auto is_lost = [&](const Placed& piece) {
        if (!cost) return false;
        const auto& w = std::get<fact::WinsMaterial>(*cost);
        return w.taken == piece || (!w.taken && w.pawns == value(piece.role) && answer && answer->isCapture() &&
                                    answer->to() == piece.square);
    };
    bool falls = false;
    if (cause) {
        if (const auto* u = as<fact::Unguards>(*cause)) falls = is_lost(u->left);
        if (const auto* n = as<fact::Neglects>(*cause)) falls = is_lost(n->piece);
    }
    if (cause && (causes || falls)) {
        std::optional<std::string_view> falls_to;
        if (falls && answer_text) falls_to = *answer_text;
        if (auto s = book.cause(*cause, falls_to)) sentences.push_back(std::move(*s));
    }
    if (!told.empty() && !falls) {
        const Fact& top = *told.front();
        std::optional<std::string> said;
        if (answer && answer_text) said = book.refuted(top, *answer_text, atOnce(top, after, *answer));
        if (!said) said = book.punishment(top);
        const bool costly = !(as<fact::WinsMaterial>(top) || as<fact::Mate>(top) || as<fact::Hanging>(top));
        if (said) sentences.push_back(cost && costly ? book.costs(*said, *cost) : *said);
    }
    return sentences;
}

}  // namespace

std::optional<std::string> comment(const MoveContext& ctx, Lang lang, PieceLetters letters) {
    const auto& book   = phrasebook(lang);
    const auto& before = *ctx.before;
    const auto facts   = gather(ctx);
    std::vector<std::string> sentences;
    if (ctx.report->novelty) {
        const bool taken_up =
            ctx.theory && ctx.theory->source == Theory::Source::Surveys && ctx.theory->main() == ctx.played;
        std::optional<std::pair<std::string, Theory::Source>> usual;
        if (ctx.theory) {
            const auto main = ctx.theory->main();
            if (main && *main != ctx.played) usual = std::pair{moveText(before, *main, letters), ctx.theory->source};
        }
        if (taken_up) {
            sentences.push_back(book.noveltyTakenUp());
        } else {
            std::optional<std::pair<std::string_view, Theory::Source>> said;
            if (usual) said = std::pair<std::string_view, Theory::Source>{usual->first, usual->second};
            sentences.push_back(book.novelty(said));
        }
    }
    if (ctx.report->judgment) {
        auto after = before;
        after.playUnchecked(ctx.played);
        std::optional<Move> answer;
        if (ctx.reply && !ctx.reply->moves.empty()) answer = ctx.reply->moves.front();
        for (auto& s : refutation(book, facts.punished_by, after, answer, letters, true))
            sentences.push_back(std::move(s));
        if (ctx.best && !ctx.best->moves.empty() && ctx.best->moves.front() != ctx.played) {
            const auto better = ctx.best->moves.front();
            // Against a move that loses outright, only what would have stopped the loss, or won something, is a reason.
            const bool lost = std::any_of(facts.punished_by.begin(), facts.punished_by.end(), isConcrete);
            // A feature of the structure is the move's doing when the move creates it, not the line after it; a
            // sacrifice, when the move gives the piece.
            const auto own = structureChanges(before, std::span<const Move>(&better, 1));
            // Against a mate on the back rank, a square for the king is the answer.
            const bool back_rank = std::any_of(facts.punished_by.begin(), facts.punished_by.end(), [](const Fact& f) {
                const auto* m = as<fact::Mate>(f);
                return m && m->pattern == MatePattern::BackRank;
            });
            std::vector<Fact> reasons;
            for (const auto& f : facts.missed) {
                const bool airs = back_rank && as<fact::Luft>(f);
                if (!(!lost || isConcrete(f) || salience(f) >= TACTIC || airs)) continue;
                if (isFeature(f) && std::find(own.begin(), own.end(), f) == own.end()) continue;
                if (as<fact::Sacrifice>(f) && !atOnce(f, before, better)) continue;
                reasons.push_back(f);
            }
            sentences.push_back(book.better(moveText(before, better, letters), reasons, facts.missedChance()));
        }
    } else if (ctx.report->only_move) {
        sentences.push_back(
            book.onlyMove(ctx.report->win_after, facts.achieves.empty() ? nullptr : &facts.achieves.front()));
        // What the next best move runs into.
        if (const auto other = alternative(ctx)) {
            auto tried = before;
            tried.playUnchecked(other->alt);
            const std::optional<Move> answer = other->rest.front();
            const auto said                  = refutation(book, facts.otherwise, tried, answer, letters, false);
            const auto alt_text              = moveText(before, other->alt, letters);
            for (std::size_t i = 0; i < said.size(); ++i)
                sentences.push_back(i == 0 ? book.otherwise(alt_text, said[i]) : said[i]);
        }
    } else {
        // What the move wins or threatens; else the plan it carries out, when the books have a name for it. A tactic
        // the line only comes to later is not the move's own doing.
        const auto later = [&](const Fact& f) {
            return (as<fact::Fork>(f) || as<fact::Pin>(f) || as<fact::Skewer>(f) || as<fact::DiscoveredAttack>(f) ||
                    as<fact::Overloaded>(f) || as<fact::Clearance>(f)) &&
                   !atOnce(f, before, ctx.played);
        };
        const Fact* telling = nullptr;
        for (const auto& f : facts.achieves) {
            if (!later(f)) {
                telling = &f;
                break;
            }
        }
        if (telling && salience(*telling) < WORTH_SAYING) telling = nullptr;
        const Fact* plan = nullptr;
        for (const auto& f : facts.achieves) {
            if (isPlan(f)) {
                plan = &f;
                break;
            }
        }
        const Fact* chosen = telling ? telling : plan;
        if (chosen) {
            if (auto s = book.achieves(*chosen)) sentences.push_back(std::move(*s));
        }
    }
    if (sentences.empty()) return std::nullopt;
    std::string out;
    for (std::size_t i = 0; i < sentences.size(); ++i) out += (i ? " " : "") + sentences[i];
    return out;
}

}  // namespace fcg::commentary
