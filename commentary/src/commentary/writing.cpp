#include "writing.hpp"

#include "comment.hpp"
#include "openings.hpp"

namespace fcg::commentary {

std::optional<std::string> openingAt(const MainLine& line, std::size_t ply) {
    const auto& start  = line.positions().front();
    const auto initial = Position::initial();
    const bool usual   = start.board() == initial.board() && start.turn() == initial.turn() &&
                         start.castles() == initial.castles() && start.epSquare() == initial.epSquare();
    if (!usual) return std::nullopt;
    return openingName(std::span<const Move>(line.moves()).first(std::min(ply, line.moves().size())));
}

MoveContext moveContext(const MainLine& line, const MoveReport& m, const Analyses& analyses, const Theory* theory,
                        const Tablebases* tables, const Oracle* oracle) {
    const auto parent   = m.ply - 1;
    const auto analysis = [&](std::size_t i) -> const PositionAnalysis* {
        return i < analyses.size() && analyses[i] ? &*analyses[i] : nullptr;
    };
    MoveContext ctx;
    ctx.before = &line.positions()[parent];
    ctx.played = line.moves()[parent];
    ctx.report = &m;
    if (const auto* a = analysis(parent)) {
        ctx.best   = a->best();
        ctx.second = a->second();
    }
    if (const auto* a = analysis(m.ply)) ctx.reply = a->best();
    ctx.tables   = tables;
    ctx.previous = line.moveTo(parent);
    ctx.opening  = openingAt(line, parent);
    ctx.theory   = theory;
    ctx.oracle   = oracle;
    return ctx;
}

std::vector<std::pair<std::size_t, std::string>> templateComments(const MainLine& line, const GameReport& report,
                                                                  const Analyses& analyses, const Theories& theories,
                                                                  const Tablebases* tables, const Oracle* oracle,
                                                                  Lang lang, PieceLetters letters) {
    std::vector<std::pair<std::size_t, std::string>> comments;
    for (const auto& m : report.moves) {
        const auto theory = theories.find(m.ply);
        if (!m.isNotable() && !(m.novelty && theory != theories.end())) continue;
        const auto ctx =
            moveContext(line, m, analyses, theory != theories.end() ? &theory->second : nullptr, tables, oracle);
        if (auto text = comment(ctx, lang, letters)) comments.emplace_back(m.ply, std::move(*text));
    }
    return comments;
}

}  // namespace fcg::commentary
