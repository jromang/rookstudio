#include "explained.hpp"

#include <algorithm>

#include <fmt/include/fmt/format.h>
#include <json.hpp>

#include <commentary/comment.hpp>
#include <commentary/notation.hpp>
#include <commentary/writing.hpp>
#include <tour/files.hpp>

#include "storage.hpp"

namespace fcg {

namespace {

const char* sourceKey(ExplainedLine::Source s) {
    switch (s) {
        case ExplainedLine::Source::Theory:
            return "theory";
        case ExplainedLine::Source::Model:
            return "model";
        case ExplainedLine::Source::Simple:
            return "simple";
        case ExplainedLine::Source::Review:
            return "review";
        case ExplainedLine::Source::Template:
            break;
    }
    return "template";
}

ExplainedLine::Source sourceOf(const std::string& key) {
    if (key == "theory") return ExplainedLine::Source::Theory;
    if (key == "model") return ExplainedLine::Source::Model;
    if (key == "simple") return ExplainedLine::Source::Simple;
    if (key == "review") return ExplainedLine::Source::Review;
    return ExplainedLine::Source::Template;
}

}  // namespace

std::string explainedReview(const std::string& engine, int passes, std::size_t positions) {
    return fmt::format("{}|{}|{}", engine, passes, positions);
}

std::optional<std::string> simpleMoveWord(const commentary::Position& before,
                                          const std::optional<commentary::Move>& previous,
                                          const commentary::Move& played, commentary::Lang lang) {
    const bool fr = lang == commentary::Lang::Fr;
    if (before.legalMoves().size() == 1) return std::string(fr ? "Seul coup légal." : "The only legal move.");
    if (commentary::takesBackTheOnlyWay(before, previous, played))
        return std::string(fr ? "Reprise forcée." : "Forced recapture.");
    return std::nullopt;
}

std::vector<ExplainedLine> explainPlan(const commentary::MainLine& line, const commentary::GameReport& report,
                                       commentary::Lang lang,
                                       const std::function<uint64_t(const commentary::Position&)>& games) {
    std::vector<ExplainedLine> out;
    const auto& positions = line.positions();
    const std::size_t n   = line.moves().size();
    const bool fr         = lang == commentary::Lang::Fr;
    // Theory: its moves by the opening name they reach, three lines at most (the shortest run joins the next).
    struct Run {
        std::size_t from = 0, to = 0;
        std::string name;
    };
    std::vector<Run> runs;
    std::size_t ply = 1;
    for (; ply <= n; ++ply) {
        const auto* m = report.get(ply);
        if (!m || !m->book) break;
        const auto name = commentary::openingAt(line, ply).value_or(std::string());
        if (!runs.empty() && runs.back().name == name)
            runs.back().to = ply;
        else
            runs.push_back({ply, ply, name});
    }
    while (runs.size() > 3) {
        std::size_t shortest = 0;
        for (std::size_t i = 1; i < runs.size(); ++i)
            if (runs[i].to - runs[i].from < runs[shortest].to - runs[shortest].from) shortest = i;
        const std::size_t into = shortest + 1 < runs.size() ? shortest + 1 : shortest - 1;
        runs[into].from        = std::min(runs[into].from, runs[shortest].from);
        runs[into].to          = std::max(runs[into].to, runs[shortest].to);
        runs.erase(runs.begin() + static_cast<std::ptrdiff_t>(shortest));
    }
    for (const auto& r : runs) {
        ExplainedLine l;
        l.from    = r.from;
        l.to      = r.to;
        l.source  = ExplainedLine::Source::Theory;
        l.written = true;
        l.text    = r.name.empty() ? std::string(fr ? "Théorie." : "Theory.") : r.name + ".";
        if (games && r.to < positions.size()) l.games = games(positions[r.to]);
        out.push_back(std::move(l));
    }
    // Each move after it: said in a word when it is too simple for more, else to write.
    for (; ply <= n; ++ply) {
        ExplainedLine l;
        l.from = l.to = ply;
        if (const auto* m = report.get(ply)) {
            l.matters = m->isNotable() || m->novelty;
            l.key     = m->key;
        }
        if (!l.matters)
            if (const auto word =
                    simpleMoveWord(positions[ply - 1], line.moveTo(ply - 1), line.moves()[ply - 1], lang)) {
                l.text    = *word;
                l.source  = ExplainedLine::Source::Simple;
                l.written = true;
            }
        out.push_back(std::move(l));
    }
    return out;
}

WrittenLine writeExplainedLine(commentary::Model* model, const commentary::MoveContext& ctx, commentary::Lang lang,
                               commentary::PieceLetters letters) {
    WrittenLine w;
    const commentary::Brief brief(ctx, lang, letters);
    // The model, held to the facts: its grammar names only legal moves, the checker reads each sentence.
    if (model && brief.hasSubstance()) {
        w.asked = true;
        for (uint32_t attempt = 1; attempt <= commentary::ATTEMPTS; ++attempt) {
            const auto answer = model->generate(commentary::commentRequest(brief, attempt), nullptr);
            if (!answer) break;
            const auto refusal = brief.check(answer->text);
            if (!refusal) {
                w.text   = answer->text;
                w.source = ExplainedLine::Source::Model;
                w.refused.reset();
                return w;
            }
            w.refused = refusal;
        }
    }
    // The template's, its sentences read against the boards as the model's are (#64).
    if (const auto text = commentary::comment(ctx, lang, letters))
        if (auto held = brief.heldTemplate(*text)) {
            w.text   = std::move(*held);
            w.source = ExplainedLine::Source::Template;
            return w;
        }
    // The review's own words: the engine's choice, or the move it preferred.
    const bool fr = lang == commentary::Lang::Fr;
    w.source      = ExplainedLine::Source::Review;
    if (ctx.report && ctx.report->played_best) {
        w.text = fr ? "Le premier choix du moteur." : "The engine's first choice.";
    } else if (ctx.best && !ctx.best->moves.empty() && ctx.before) {
        const auto best = commentary::san(*ctx.before, ctx.best->moves.front(), letters);
        w.text = fr ? fmt::format("Le moteur préférait {}.", best) : fmt::format("The engine preferred {}.", best);
    } else {
        w.text = fr ? "Un coup sans histoire." : "A quiet move.";
    }
    return w;
}

ExplainedCounts explainedCounts(const Explained& e) {
    ExplainedCounts c;
    for (const auto& l : e.lines) {
        const int moves = static_cast<int>(l.to - l.from + 1);
        c.moves += moves;
        if (l.written) ++c.written;
        switch (l.source) {
            case ExplainedLine::Source::Theory:
                c.theory_moves += moves;
                ++c.theory_lines;
                break;
            case ExplainedLine::Source::Model:
                if (l.written) ++c.model;
                break;
            case ExplainedLine::Source::Simple:
                ++c.simple;
                break;
            case ExplainedLine::Source::Review:
                if (l.written) ++c.review;
                break;
            case ExplainedLine::Source::Template:
                if (l.written) ++c.templates;
                break;
        }
        if (l.written && l.refused) ++c.refused;
    }
    return c;
}

bool saveExplained(const std::filesystem::path& dir, const Explained& e) {
    nlohmann::json lines = nlohmann::json::array();
    for (const auto& l : e.lines) {
        nlohmann::json j{
            {"from", l.from},       {"to", l.to},       {"text", l.text},       {"source", sourceKey(l.source)},
            {"written", l.written}, {"games", l.games}, {"matters", l.matters}, {"key", l.key}};
        if (l.refused) j["refused"] = {static_cast<int>(l.refused->kind), l.refused->what};
        lines.push_back(std::move(j));
    }
    const nlohmann::json j{{"key", e.key},
                           {"review", e.review},
                           {"lang", e.lang},
                           {"complete", e.complete},
                           {"model_secs", e.model_secs},
                           {"model_lines", e.model_lines},
                           {"device", e.device},
                           {"lines", lines}};
    return writeJson(dir / (e.key + "." + e.lang + ".explained.json"), j);
}

std::optional<Explained> loadExplained(const std::filesystem::path& dir, const std::string& key,
                                       const std::string& lang) {
    const auto j = readJson<nlohmann::json>(dir / (key + "." + lang + ".explained.json"));
    if (!j || !j->is_object() || j->value("key", std::string()) != key || j->value("lang", std::string()) != lang)
        return std::nullopt;
    try {
        Explained e;
        e.key         = key;
        e.lang        = lang;
        e.review      = j->value("review", std::string());
        e.complete    = j->value("complete", false);
        e.model_secs  = j->value("model_secs", 0.0);
        e.model_lines = j->value("model_lines", 0);
        e.device      = j->value("device", std::string());
        for (const auto& l : j->at("lines")) {
            ExplainedLine line;
            line.from    = l.value("from", std::size_t{0});
            line.to      = l.value("to", std::size_t{0});
            line.text    = l.value("text", std::string());
            line.source  = sourceOf(l.value("source", std::string()));
            line.written = l.value("written", false);
            line.games   = l.value("games", uint64_t{0});
            line.matters = l.value("matters", false);
            line.key     = l.value("key", false);
            if (const auto r = l.find("refused"); r != l.end() && r->is_array() && r->size() == 2)
                line.refused = commentary::Refusal{static_cast<commentary::Refusal::Kind>(r->at(0).get<int>()),
                                                   r->at(1).get<std::string>()};
            e.lines.push_back(std::move(line));
        }
        return e;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace fcg
