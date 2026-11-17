#include "case.hpp"

namespace fcg::commentary {

namespace {

CaseLine lineFrom(const nlohmann::json& j) {
    CaseLine l;
    l.moves = j.at("moves").get<std::vector<std::string>>();
    if (j.contains("cp")) l.cp = j.at("cp").get<int32_t>();
    if (j.contains("mate")) l.mate = j.at("mate").get<int32_t>();
    if (j.contains("wdl")) l.wdl = j.at("wdl").get<std::array<uint16_t, 3>>();
    l.depth = j.value("depth", 0u);
    return l;
}

std::optional<Move> parseMove(const Position& pos, const std::string& uci, std::string* error) {
    const auto m = pos.parseUci(uci);
    if (!m && error) *error = uci + ": not a legal move in " + pos.fen();
    return m;
}

std::optional<EngineLine> engineLine(const CaseLine& l, const Position& start, std::string* error) {
    EngineLine out;
    auto pos = start;
    for (const auto& uci : l.moves) {
        const auto m = parseMove(pos, uci, error);
        if (!m) return std::nullopt;
        pos.playUnchecked(*m);
        out.moves.push_back(*m);
    }
    if (l.mate)
        out.score = Score::mate(*l.mate);
    else if (l.cp)
        out.score = Score::cp(*l.cp);
    else {
        if (error) *error = "a line without a score";
        return std::nullopt;
    }
    out.wdl   = l.wdl;
    out.depth = l.depth;
    return out;
}

}  // namespace

Case Case::fromJson(const nlohmann::json& j) {
    Case c;
    c.id                = j.at("id").get<std::string>();
    c.moves             = j.at("moves").get<std::vector<std::string>>();
    const auto& r       = j.at("review");
    c.review.phase      = r.at("phase").get<std::string>();
    c.review.win_before = r.at("win_before").get<double>();
    c.review.win_after  = r.at("win_after").get<double>();
    c.review.accuracy   = r.at("accuracy").get<double>();
    c.review.cp_loss    = r.at("cp_loss").get<int32_t>();
    if (r.contains("judgment")) c.review.judgment = r.at("judgment").get<std::string>();
    c.review.played_best = r.at("played_best").get<bool>();
    c.review.only_move   = r.at("only_move").get<bool>();
    c.review.only_gap    = r.at("only_gap").get<double>();
    c.review.key         = r.at("key").get<bool>();
    c.review.book        = r.at("book").get<bool>();
    c.review.novelty     = r.at("novelty").get<bool>();
    c.review.plan        = r.value("plan", false);
    if (j.contains("best")) c.best = lineFrom(j.at("best"));
    if (j.contains("reply")) c.reply = lineFrom(j.at("reply"));
    if (j.contains("second")) c.second = lineFrom(j.at("second"));
    if (j.contains("human")) c.human = j.at("human").get<std::string>();
    if (j.contains("answers")) {
        std::vector<CaseAnswer> answers;
        for (const auto& a : j.at("answers")) {
            CaseAnswer answer;
            answer.fen = a.at("fen").get<std::string>();
            if (a.contains("only")) answer.only = a.at("only").get<std::vector<std::string>>();
            if (a.contains("line") && !a.at("line").is_null()) answer.line = lineFrom(a.at("line"));
            answers.push_back(std::move(answer));
        }
        c.answers = std::move(answers);
    }
    return c;
}

std::optional<Replayed> replay(const Case& c, std::string* error,
                               std::optional<std::string> (*opening)(std::span<const Move>)) {
    if (c.moves.empty()) {
        if (error) *error = "a case without moves";
        return std::nullopt;
    }
    Replayed out;
    out.before = Position::initial();
    std::vector<Move> played_before;
    for (std::size_t i = 0; i + 1 < c.moves.size(); ++i) {
        const auto m = parseMove(out.before, c.moves[i], error);
        if (!m) return std::nullopt;
        out.before.playUnchecked(*m);
        played_before.push_back(*m);
    }
    if (!played_before.empty()) out.previous = played_before.back();
    const auto played = parseMove(out.before, c.moves.back(), error);
    if (!played) return std::nullopt;
    out.played = *played;
    auto after = out.before;
    after.playUnchecked(out.played);

    const auto& r    = c.review;
    out.report.mover = out.before.turn();
    if (r.phase == "opening")
        out.report.phase = Phase::Opening;
    else if (r.phase == "middlegame")
        out.report.phase = Phase::Middlegame;
    else if (r.phase == "endgame")
        out.report.phase = Phase::Endgame;
    else {
        if (error) *error = "unknown phase " + r.phase;
        return std::nullopt;
    }
    out.report.win_before = r.win_before;
    out.report.win_after  = r.win_after;
    out.report.accuracy   = r.accuracy;
    out.report.cp_loss    = r.cp_loss;
    if (r.judgment) {
        if (*r.judgment == "inaccuracy")
            out.report.judgment = Judgment::Inaccuracy;
        else if (*r.judgment == "mistake")
            out.report.judgment = Judgment::Mistake;
        else if (*r.judgment == "blunder")
            out.report.judgment = Judgment::Blunder;
        else {
            if (error) *error = "unknown judgment " + *r.judgment;
            return std::nullopt;
        }
    }
    out.report.played_best = r.played_best;
    out.report.only_move   = r.only_move;
    out.report.only_gap    = r.only_gap;
    out.report.key         = r.key;
    out.report.book        = r.book;
    out.report.novelty     = r.novelty;
    out.report.plan        = r.plan;

    if (c.best) {
        out.best = engineLine(*c.best, out.before, error);
        if (!out.best) return std::nullopt;
    }
    if (c.reply) {
        out.reply = engineLine(*c.reply, after, error);
        if (!out.reply) return std::nullopt;
    }
    if (c.second) {
        out.second = engineLine(*c.second, out.before, error);
        if (!out.second) return std::nullopt;
    }
    if (opening) out.opening = opening(played_before);
    if (c.answers) {
        RecordedOracle oracle;
        for (const auto& a : *c.answers) {
            // An answer with nothing to say is as good as no answer: the search gives nothing either way.
            if (!a.line || (!a.line->cp && !a.line->mate)) continue;
            // The score from White's point of view turned to the side to move in the answer's position.
            const auto fields  = a.fen.find(' ');
            const bool black   = fields != std::string::npos && fields + 1 < a.fen.size() && a.fen[fields + 1] == 'b';
            const int32_t sign = black ? -1 : 1;
            const auto score   = a.line->mate ? Score::mate(sign * *a.line->mate) : Score::cp(sign * *a.line->cp);
            oracle.answer(a.fen, a.only, a.line->moves, score);
        }
        out.oracle = std::move(oracle);
    }
    return out;
}

MoveContext Replayed::context(const Tablebases* tables) const {
    MoveContext ctx;
    ctx.before   = &before;
    ctx.played   = played;
    ctx.report   = &report;
    ctx.best     = best ? &*best : nullptr;
    ctx.reply    = reply ? &*reply : nullptr;
    ctx.second   = second ? &*second : nullptr;
    ctx.tables   = tables;
    ctx.previous = previous;
    ctx.opening  = opening;
    ctx.oracle   = oracle ? &*oracle : nullptr;
    return ctx;
}

}  // namespace fcg::commentary
