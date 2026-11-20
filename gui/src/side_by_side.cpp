#include "side_by_side.hpp"

#include <algorithm>
#include <cctype>

#include <fmt/include/fmt/format.h>

#include "i18n.hpp"

namespace fcg {

namespace {

// "Alpha", "Alpha and Beta", "Alpha, Beta and Gamma".
std::string names(const std::vector<std::string>& list) {
    if (list.empty()) return {};
    std::string out = list[0];
    for (std::size_t i = 1; i + 1 < list.size(); ++i) out += ", " + list[i];
    return list.size() == 1 ? out : trf("{} and {}", out, list.back());
}

// The runs of a translated message whose placeholders ({} in turn, or {0}, {1}… by number) take `args`.
std::vector<TextRun> fill(const std::string& message, const std::vector<TextRun>& args) {
    std::vector<TextRun> out;
    std::string text;
    std::size_t next = 0;
    const auto flush = [&] {
        if (!text.empty()) out.push_back({std::move(text), false});
        text.clear();
    };
    for (std::size_t i = 0; i < message.size(); ++i) {
        const char c = message[i];
        if ((c == '{' || c == '}') && i + 1 < message.size() && message[i + 1] == c) {  // {{ and }}
            text += c;
            ++i;
            continue;
        }
        const auto close = c == '{' ? message.find('}', i) : std::string::npos;
        if (close == std::string::npos) {
            text += c;
            continue;
        }
        std::size_t index = next++;
        const auto inside = message.substr(i + 1, close - i - 1);
        if (!inside.empty() && std::isdigit(static_cast<unsigned char>(inside[0])))
            index = static_cast<std::size_t>(std::atoi(inside.c_str()));
        flush();
        if (index < args.size()) out.push_back(args[index]);
        i = close;
    }
    flush();
    return out;
}

}  // namespace

uint32_t sideColour(std::size_t index) {
    // #1f55b5, #e0912f, #2a9d8f and #6b6f76: lightness about 38, 67, 59 and 47.
    static constexpr uint32_t colours[] = {0xFFB5551Fu, 0xFF2F91E0u, 0xFF8F9D2Au, 0xFF766F6Bu};
    return colours[index % 4];
}

std::vector<int> shareThreads(int total, const std::vector<ThreadAsk>& asks) {
    std::vector<int> out(asks.size(), 0);
    int left    = std::max(1, total);
    int sharing = 0;
    for (std::size_t i = 0; i < asks.size(); ++i) {
        if (asks[i].hand > 0) {
            out[i] = asks[i].hand;
            if (!asks[i].gpu) left -= asks[i].hand;
        } else if (!asks[i].gpu) {
            ++sharing;
        }
    }
    if (sharing == 0) return out;
    const int each = left > 0 ? left / sharing : 0, extra = left > 0 ? left % sharing : 0;
    int given = 0;
    for (std::size_t i = 0; i < asks.size(); ++i) {
        if (asks[i].hand > 0 || asks[i].gpu) continue;
        out[i] = std::max(1, each + (given < extra ? 1 : 0));
        ++given;
    }
    return out;
}

bool searchesOnGpu(const EngineEntry& entry) {
    if (entry.gpu) return true;
    // Lc0 and the engines built on it name their backend; its options name those a meta backend (multiplexing,
    // demux, roundrobin) runs.
    std::string backend;
    for (const char* name : {"Backend", "BackendOptions"}) {
        for (const auto& o : entry.uci_options) {
            if (o.name != name) continue;
            const auto* value = entry.option(name);
            backend += (value ? *value : o.default_value) + " ";
        }
    }
    for (auto& c : backend) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* gpu : {"cuda", "cudnn", "opencl", "metal", "dx12", "dml", "trt", "rocm", "hip", "sycl", "vulkan",
                            "xla", "onednn-gpu"}) {
        if (backend.find(gpu) != std::string::npos) return true;
    }
    return false;
}

std::vector<TextRun> agreementRuns(const std::vector<EngineVerdict>& verdicts) {
    std::vector<const EngineVerdict*> with;
    for (const auto& v : verdicts)
        if (!v.move.empty()) with.push_back(&v);
    if (with.size() < 2) return {};

    std::vector<TextRun> out;
    const auto add = [&](const std::vector<TextRun>& runs) {
        if (!out.empty()) out.push_back({" · ", false});
        out.insert(out.end(), runs.begin(), runs.end());
    };
    // The moves, in the order of the first engine that plays each, with the engines that play it.
    std::vector<std::pair<std::string, std::vector<std::string>>> moves;
    for (const auto* v : with) {
        const auto it = std::find_if(moves.begin(), moves.end(), [&](const auto& m) { return m.first == v->move; });
        if (it == moves.end())
            moves.push_back({v->move, {v->name}});
        else
            it->second.push_back(v->name);
    }
    if (moves.size() == 1) {
        add(fill(TR("{} agree on {}"), {{names(moves[0].second)}, {moves[0].first, true}}));
    } else {
        for (const auto& [move, who] : moves) add(fill(TR("{} for {}"), {{move, true}, {names(who)}}));
    }

    // How far apart their evaluations are; a mate, as each engine sees it.
    std::vector<std::pair<int64_t, std::vector<std::string>>> mates;  // White mates in N, Black when negative
    std::optional<int64_t> low, high;
    int scored = 0;
    for (const auto* v : with) {
        if (!v->white) continue;
        if (v->white->type == fastchess::ScoreType::MATE) {
            const auto it =
                std::find_if(mates.begin(), mates.end(), [&](const auto& m) { return m.first == v->white->value; });
            if (it == mates.end())
                mates.push_back({v->white->value, {v->name}});
            else
                it->second.push_back(v->name);
        } else if (v->white->type == fastchess::ScoreType::CP) {
            low  = std::min(low.value_or(v->white->value), v->white->value);
            high = std::max(high.value_or(v->white->value), v->white->value);
            ++scored;
        }
    }
    if (!mates.empty()) {
        for (const auto& [value, who] : mates)
            add(fill(i18n::trPlural("{} sees {}", "{} see {}", who.size()),
                     {{names(who)}, {fmt::format("#{}", value)}}));
    } else if (scored >= 2) {
        const double spread = static_cast<double>(*high - *low) / 100.0;
        if (spread < 0.005)
            add({{TR("the same evaluation")}});
        else if (spread <= 0.5)
            add({{trf("their evaluations within {}", fmt::format("{:.2f}", spread))}});
        else
            add({{trf("their evaluations {} apart", fmt::format("{:.2f}", spread))}});
    }
    return out;
}

std::string runsText(const std::vector<TextRun>& runs) {
    std::string out;
    for (const auto& r : runs) out += r.text;
    return out;
}

std::vector<SideArrow> sideArrows(const std::vector<std::string>& best) {
    std::vector<SideArrow> out;
    for (std::size_t i = 0; i < best.size(); ++i) {
        if (best[i].empty()) continue;
        const auto it = std::find_if(out.begin(), out.end(), [&](const SideArrow& a) { return a.move == best[i]; });
        if (it != out.end())
            ++it->count;
        else
            out.push_back({best[i], i, 1});
    }
    return out;
}

void keepTrail(GameReview& trail, std::size_t plies, int ply, const AnalysisLine& line) {
    if (ply < 0 || static_cast<std::size_t>(ply) > plies || !line.score || line.pv.empty() || line.lowerbound ||
        line.upperbound)
        return;
    if (trail.positions.size() != plies + 1) trail.positions.resize(plies + 1);
    auto& p = trail.positions[static_cast<std::size_t>(ply)];
    if (p.score && p.depth > line.depth) return;
    p.score   = line.score;
    p.best    = line.pv.front();
    p.depth   = static_cast<int>(line.depth);
    p.time_ms = line.time_ms;
    p.wdl     = line.wdl;
    p.pv.clear();
    for (const auto& m : line.pv) p.pv += (p.pv.empty() ? "" : " ") + m;
}

void runSideBySide(std::vector<std::unique_ptr<Analyser>>& analysers, const std::vector<SideRun>& runs, bool search,
                   const AnalysisPosition& position, bool new_game) {
    while (analysers.size() > runs.size()) analysers.pop_back();  // its process ends with it
    while (analysers.size() < runs.size()) analysers.push_back(std::make_unique<Analyser>());
    for (std::size_t i = 0; i < runs.size(); ++i) {
        if (!analysers[i]) analysers[i] = std::make_unique<Analyser>();
        auto& a         = *analysers[i];
        const auto& run = runs[i];
        if (!run.entry) {
            if (a.state() != Analyser::State::Off) a.stop();
            continue;
        }
        const bool same = a.engine() == run.entry->name;
        if (same && a.state() == Analyser::State::Failed) continue;  // its error stays shown
        if (!same && a.state() != Analyser::State::Off) a.stop();
        if (a.state() == Analyser::State::Off) {
            if (!search) continue;
            a.start(*run.entry, run.settings);
        } else {
            a.update(run.settings);
        }
        if (search)
            a.analyse(position, {}, new_game);
        else
            a.halt();
    }
}

}  // namespace fcg
