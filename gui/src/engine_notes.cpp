#include "engine_notes.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

#include <fmt/include/fmt/format.h>

#include <study/game_tree.hpp>

#include "board_text.hpp"
#include "game_graph.hpp"
#include "i18n.hpp"
#include "live.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

std::string playerName(const ReplayGame& game, int side) {
    auto name = game.header(side == 0 ? "White" : "Black");
    return name.empty() || name == "?" ? std::string(side == 0 ? TR("White") : TR("Black")) : name;
}

int percent(float share) { return static_cast<int>(std::lround(std::clamp(share, 0.0f, 1.0f) * 100.0f)); }

// "88 %", or "88 to 90 %".
std::string shareRange(float lo, float hi) {
    return percent(lo) == percent(hi) ? fmt::format("{} %", percent(lo)) : trf("{} to {} %", percent(lo), percent(hi));
}

}  // namespace

bool playerEvaluation(const ReplayMove& m) {
    return !m.book && m.score && (!m.notes.eval_tag || m.notes.emt_ms.has_value() || !m.engine_comment.empty());
}

bool hasPlayerEvaluations(const ReplayGame& game) {
    return std::any_of(game.moves.begin(), game.moves.end(), [](const ReplayMove& m) { return playerEvaluation(m); });
}

int64_t moveTime(const ReplayMove& m) { return m.time_ms >= 0 ? m.time_ms : m.notes.emt_ms.value_or(-1); }

std::string engineTag(const ReplayMove& m, EngineTags tags) {
    if (tags == EngineTags::Nothing || !playerEvaluation(m)) return {};
    auto white = *m.score;
    if (m.mover == chess::Color::BLACK) white.value = -white.value;
    auto out = ui::formatScore(white);
    if (m.depth > 0) out += "/" + std::to_string(m.depth);
    if (tags == EngineTags::EvalDepthTime)
        if (const auto t = moveTime(m); t >= 0) out += " " + ui::formatClock(t);
    return out;
}

std::vector<std::string> expectedLine(const ReplayMove& m, bool chess960) {
    std::vector<std::string> pv;
    std::istringstream in(m.pv);
    for (std::string w; in >> w;) pv.push_back(w);
    if (pv.empty() || m.move == chess::Move::NO_MOVE || m.move == chess::Move::NULL_MOVE ||
        pv.front() != chess::uci::moveToUci(m.move, chess960))
        return {};
    pv.erase(pv.begin());
    return pv;
}

std::optional<EngineDetail> engineDetail(const ReplayGame& game, bool chess960, int ply) {
    if (ply < 1 || ply > static_cast<int>(game.moves.size())) return std::nullopt;
    const auto& m = game.moves[static_cast<std::size_t>(ply - 1)];
    if (!playerEvaluation(m)) return std::nullopt;
    const bool white  = m.mover == chess::Color::WHITE;
    const auto before = boardAt(game, chess960, ply - 1);
    EngineDetail d;
    d.title = fmt::format("{}{}{} · {}, {}", before.fullMoveNumber(), white ? "." : "…", localSan(m.san),
                          playerName(game, white ? 0 : 1), white ? TR("White") : TR("Black"));

    auto for_white = *m.score;
    if (!white) for_white.value = -for_white.value;
    d.rows.emplace_back(TR("Evaluation"), white ? trf("{} for White", ui::formatScore(for_white))
                                                : trf("{} for White, {} from its side", ui::formatScore(for_white),
                                                      ui::formatScore(*m.score)));
    if (m.depth > 0)
        d.rows.emplace_back(TR("Depth"),
                            m.seldepth > 0 ? trf("{}, selective {}", m.depth, m.seldepth) : std::to_string(m.depth));
    if (const auto t = moveTime(m); t >= 0) {
        auto text = ui::formatClock(t);
        if (m.decision_ms) text += " · " + trf("move chosen at {}", ui::formatClock(*m.decision_ms));
        d.rows.emplace_back(TR("Time"), std::move(text));
    }
    if (m.nodes > 0 || m.nps > 0) {
        std::string text = m.nodes > 0 ? ui::formatCount(m.nodes) : std::string();
        if (m.nps > 0) text += (text.empty() ? "" : " · ") + ui::formatCount(static_cast<uint64_t>(m.nps)) + " n/s";
        d.rows.emplace_back(TR("Nodes"), std::move(text));
    }
    if (m.fields & PGN_TBHITS)
        d.rows.emplace_back(TR("Tablebases"), m.tbhits == 0
                                                  ? std::string(TR("no access"))
                                                  : trn("{} hit", "{} hits", m.tbhits, ui::formatCount(m.tbhits)));
    if (m.wdl) {
        const auto sum = static_cast<float>(std::max<int64_t>(1, (*m.wdl)[0] + (*m.wdl)[1] + (*m.wdl)[2]));
        d.wdl = std::array<float, 3>{static_cast<float>((*m.wdl)[0]) / sum, static_cast<float>((*m.wdl)[1]) / sum,
                                     static_cast<float>((*m.wdl)[2]) / sum};
        d.wdl_text =
            trf("win {} % · draw {} % · loss {} %", percent((*d.wdl)[0]), percent((*d.wdl)[1]), percent((*d.wdl)[2]));
    }
    if (const auto line = expectedLine(m, chess960); !line.empty())
        d.expected = localLine(pvToSan(boardAt(game, chess960, ply), line, 12));
    return d;
}

float curveAt(const EngineCurves& c, int side, int ply) {
    float out = std::nanf("");
    for (const auto& p : c.win[static_cast<std::size_t>(side)]) {
        if (p.ply > ply) break;
        out = p.win;
    }
    return out;
}

EngineCurves engineCurves(const ReplayGame& game) {
    EngineCurves c;
    c.plies    = static_cast<int>(game.moves.size());
    c.names[0] = playerName(game, 0);
    c.names[1] = playerName(game, 1);
    std::array<double, 2> depth_sum{}, nps_sum{};
    std::array<int, 2> depth_n{}, nps_n{};
    bool leading = true;
    int tables = -1, tables_side = 0, mate = -1;
    for (std::size_t i = 0; i < game.moves.size(); ++i) {
        const auto& m  = game.moves[i];
        const int ply  = static_cast<int>(i) + 1;
        const int side = m.mover == chess::Color::WHITE ? 0 : 1;
        const auto s   = static_cast<std::size_t>(side);
        if (m.book) {
            if (leading) c.book = ply;
            continue;
        }
        leading      = false;
        const auto t = moveTime(m);
        if (t >= 0) {
            c.time[s].emplace_back(ply, t);
            c.time_used[s] += t;
        }
        if (m.depth > 0) depth_sum[s] += m.depth, ++depth_n[s];
        if (m.nps > 0)
            nps_sum[s] += static_cast<double>(m.nps), ++nps_n[s];
        else if (m.nodes > 0 && t > 0)
            nps_sum[s] += static_cast<double>(m.nodes) * 1000.0 / static_cast<double>(t), ++nps_n[s];
        if (!playerEvaluation(m)) continue;
        if (const float w = graphWin(m.score, m.wdl, m.mover); std::isfinite(w)) c.win[s].push_back({ply, w});
        if (tables < 0 && m.tbhits > 0) tables = ply, tables_side = side;
        if (mate < 0 && m.score->type == fastchess::ScoreType::MATE) mate = ply;
    }
    for (std::size_t s = 0; s < 2; ++s) {
        if (depth_n[s]) c.depth[s] = depth_sum[s] / depth_n[s];
        if (nps_n[s]) c.nps[s] = nps_sum[s] / nps_n[s];
    }

    // Where the book ends: how each engine saw the game at its first move.
    if (!c.win[0].empty() && !c.win[1].empty()) {
        const auto first = [&](int side) {
            for (const auto& p : c.win[static_cast<std::size_t>(side)])
                if (p.ply > c.book) return p;
            return CurvePoint{-1, 0};
        };
        const auto a = first(0), b = first(1);
        if (a.ply > 0 && b.ply > 0) {
            EngineMoment moment;
            moment.kind = EngineMoment::Kind::BookEnd;
            moment.ply  = std::min(a.ply, b.ply);
            if (c.book > 0)
                moment.text = trf("Out of the book: {} % for White according to {}, {} % according to {}.",
                                  percent(a.win), c.names[0], percent(b.win), c.names[1]);
            else
                moment.text = trf("From the first moves: {} % for White according to {}, {} % according to {}.",
                                  percent(a.win), c.names[0], percent(b.win), c.names[1]);
            const int gap = std::abs(percent(a.win) - percent(b.win));
            moment.text += " " + (gap <= 10 ? std::string(TR("They agree.")) : trf("{} points apart.", gap));
            c.moments.push_back(std::move(moment));
        }
    }

    // Where they saw different games: each move against the other engine's just before it, apart for four plies in a
    // row at least (two moves each); runs a ply or two apart joined.
    {
        std::vector<int> run;
        std::vector<std::pair<int, int>> runs;
        const auto close_run = [&] {
            if (run.size() >= 4) {
                if (!runs.empty() && run.front() - runs.back().second <= 3)
                    runs.back().second = run.back();
                else
                    runs.emplace_back(run.front() - 1, run.back());
            }
            run.clear();
        };
        for (int ply = 2; ply <= c.plies; ++ply) {
            const int side    = game.moves[static_cast<std::size_t>(ply - 1)].mover == chess::Color::WHITE ? 0 : 1;
            const auto& own   = c.win[static_cast<std::size_t>(side)];
            const auto& other = c.win[static_cast<std::size_t>(1 - side)];
            const auto at     = [](const std::vector<CurvePoint>& v, int p) -> const CurvePoint* {
                const auto it =
                    std::lower_bound(v.begin(), v.end(), p, [](const CurvePoint& x, int q) { return x.ply < q; });
                return it != v.end() && it->ply == p ? &*it : nullptr;
            };
            const auto* a = at(own, ply);
            const auto* b = at(other, ply - 1);
            if (a && b && std::fabs(a->win - b->win) >= APART_SHARE)
                run.push_back(ply);
            else
                close_run();
        }
        close_run();
        c.apart = runs;
        for (const auto& [first, last] : runs) {
            EngineMoment moment;
            moment.kind = EngineMoment::Kind::Apart;
            moment.ply  = first;
            moment.last = last;
            std::array<float, 2> lo{1, 1}, hi{0, 0}, draw_lo{1, 1}, draw_hi{0, 0}, draw_sum{};
            std::array<int, 2> draws{};
            for (int ply = first; ply <= last; ++ply) {
                const auto& m = game.moves[static_cast<std::size_t>(ply - 1)];
                const auto s  = static_cast<std::size_t>(m.mover == chess::Color::WHITE ? 0 : 1);
                for (const auto& p : c.win[s])
                    if (p.ply == ply) lo[s] = std::min(lo[s], p.win), hi[s] = std::max(hi[s], p.win);
                if (playerEvaluation(m) && m.wdl) {
                    const auto sum = static_cast<float>(std::max<int64_t>(1, (*m.wdl)[0] + (*m.wdl)[1] + (*m.wdl)[2]));
                    const float d  = static_cast<float>((*m.wdl)[1]) / sum;
                    draw_lo[s] = std::min(draw_lo[s], d), draw_hi[s] = std::max(draw_hi[s], d);
                    draw_sum[s] += d, ++draws[s];
                }
            }
            moment.text = trf("They see different games: {} for {}; {} for {}.", shareRange(lo[0], hi[0]), c.names[0],
                              shareRange(lo[1], hi[1]), c.names[1]);
            // The one that sees the draw more, when it sees it much.
            const auto s = static_cast<std::size_t>(
                draws[1] > 0 && (draws[0] == 0 || draw_sum[1] / draws[1] > draw_sum[0] / draws[0]) ? 1 : 0);
            if (draws[s] > 0 && draw_sum[s] / static_cast<float>(draws[s]) >= 0.25f)
                moment.text += " " + trf("{} sees a draw at {}.", c.names[s], shareRange(draw_lo[s], draw_hi[s]));
            if (tables >= first && tables <= last) {
                moment.text += " " + std::string(TR("The tablebases enter their searches."));
                tables = -1;  // told here
            }
            c.moments.push_back(std::move(moment));
        }
    }

    // The tablebases in a search, a mate seen: the first time each happens.
    if (tables > 0 && tables != mate) {
        const auto& m = game.moves[static_cast<std::size_t>(tables - 1)];
        EngineMoment moment;
        moment.kind = EngineMoment::Kind::Tables;
        moment.ply  = tables;
        moment.side = tables_side;
        moment.text = trn("The tablebases enter {}'s search: {} hit.", "The tablebases enter {}'s search: {} hits.",
                          m.tbhits, c.names[static_cast<std::size_t>(tables_side)], ui::formatCount(m.tbhits));
        c.moments.push_back(std::move(moment));
    }
    if (mate > 0) {
        const auto& m    = game.moves[static_cast<std::size_t>(mate - 1)];
        const int side   = m.mover == chess::Color::WHITE ? 0 : 1;
        const auto& name = c.names[static_cast<std::size_t>(side)];
        EngineMoment moment;
        moment.kind = EngineMoment::Kind::Mate;
        moment.ply  = mate;
        moment.side = side;
        if (m.score->value > 0) {
            moment.text = trf("Mate in {} for {}", m.score->value, name);
            if (m.tbhits > 0)
                moment.text += ", " + trn("seen with the tablebases ({} hit)", "seen with the tablebases ({} hits)",
                                          m.tbhits, ui::formatCount(m.tbhits));
            moment.text += ".";
        } else {
            moment.text = trf("{} sees itself mated in {}.", name, -m.score->value);
        }
        c.moments.push_back(std::move(moment));
    }

    // How it ended.
    if (c.plies > 0 && (game.result == "1-0" || game.result == "0-1" || game.result == "1/2-1/2")) {
        EngineMoment moment;
        moment.kind = EngineMoment::Kind::End;
        moment.ply  = c.plies;
        moment.text = game.result == "1/2-1/2" ? std::string("½-½") : game.result;
        if (!game.final_comment.empty()) moment.text += ", " + std::string(TR(game.final_comment.c_str()));
        moment.text += ".";
        c.moments.push_back(std::move(moment));
    }
    std::stable_sort(c.moments.begin(), c.moments.end(),
                     [](const EngineMoment& x, const EngineMoment& y) { return x.ply < y.ply; });
    return c;
}

}  // namespace fcg
