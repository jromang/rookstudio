// Several engines side by side in the study Board's Analysis tab (#78, mockup M11, board 3): up to four engines of the
// library, each a card with its colour, its evaluation, its search and its lines; one arrow per engine on the board,
// one for those that agree, with their count; a line that says in words how they agree; one total of threads shared
// between them. The first engine is the analysis's: the game's review and the assistant use it.

#include <algorithm>
#include <ctime>
#include <filesystem>

#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include <tour/budget.hpp>

#include "app.hpp"
#include "board_text.hpp"
#include "i18n.hpp"
#include "keyboard.hpp"
#include "notation_view.hpp"
#include "ui.hpp"
#include "widgets.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

constexpr const char* kEnginePayload = "RS_ANALYSIS_ENGINE";

// "+0.42", "#5": a line's score, White's, with ≥ or ≤ for a bound.
std::string whiteText(const AnalysisLine& line, chess::Color stm) {
    if (!line.score) return "";
    auto text        = ui::formatScore(whiteScore(*line.score, stm));
    const bool white = stm == chess::Color::WHITE;
    if (line.lowerbound) text = (white ? "≥" : "≤") + text;
    if (line.upperbound) text = (white ? "≤" : "≥") + text;
    return text;
}

// Runs of text from `at`, wrapped to `width`, the strong ones bolder; the height they take. Nothing is drawn without
// a draw list: they are measured.
float drawRuns(ImDrawList* dl, ImVec2 at, float width, const std::vector<TextRun>& runs, ImU32 colour) {
    const float lh = ImGui::GetTextLineHeightWithSpacing();
    float x = 0, y = 0;
    for (const auto& r : runs) {
        std::size_t i = 0;
        while (i < r.text.size()) {
            auto j          = r.text.find(' ', i);
            j               = j == std::string::npos ? r.text.size() : j + 1;  // the word and the space after it
            const auto word = r.text.substr(i, j - i);
            const auto bare = word.back() == ' ' ? word.substr(0, word.size() - 1) : word;
            if (x > 0 && x + ImGui::CalcTextSize(bare.c_str()).x > width) {
                x = 0;
                y += lh;
            }
            if (dl) {
                dl->AddText(ImVec2(at.x + x, at.y + y), colour, word.c_str());
                if (r.strong) dl->AddText(ImVec2(at.x + x + 0.6f, at.y + y), colour, word.c_str());
            }
            x += ImGui::CalcTextSize(word.c_str()).x;
            i = j;
        }
    }
    return y + ImGui::GetTextLineHeight();
}

using ui::numbered;

}  // namespace

// ---------------------------------------------------------------------------
// The engines and their threads

const EngineEntry* App::engineNamed(const std::string& name) const {
    for (const auto& e : engines_)
        if (e.name == name) return &e;
    return nullptr;
}

Analyser* App::sideAnalyser(std::size_t index) {
    if (index == 0) return board_.analyser.get();
    return index - 1 < board_.side.size() ? board_.side[index - 1].get() : nullptr;
}

int App::tournamentCores() const {
    int cores = 0;
    for (const auto& r : runs_) {
        const auto* t = r.run->tournament();
        // A tournament on another machine leaves this one's cores alone.
        if (!r.run->running() || !t || !r.machine_id.empty()) continue;
        cores += std::max(1, t->spec().conditions.concurrency) * tour::largestGame(t->spec()).threads;
    }
    return std::min(cores, std::max(1, topology_.logical));
}

int App::analysisTotal() const {
    int total = std::max(1, board_.settings.analysis.threads);
    // A running tournament keeps its cores: the analysis takes what it leaves.
    if (const int busy = tournamentCores(); busy > 0) total = std::min(total, std::max(1, topology_.logical - busy));
    return total;
}

std::vector<int> App::sideThreads() const {
    std::vector<ThreadAsk> asks;
    for (const auto& e : analysisEngines(board_.settings)) {
        const auto* entry = engineNamed(e.engine);
        asks.push_back({e.threads, entry && searchesOnGpu(*entry)});
    }
    return shareThreads(analysisTotal(), asks);
}

// ---------------------------------------------------------------------------
// What the engines say: arrows, words, the graph's lines

std::vector<BoardArrow> App::sideBySideArrows(const chess::Board& board, const AnalysisPosition& position) {
    const auto list = analysisEngines(board_.settings);
    std::vector<std::string> best(list.size());
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto* a = sideAnalyser(i);
        if (a && a->active() && a->engine() == list[i].engine && a->position() == position && !a->lines().empty() &&
            !a->lines().front().pv.empty())
            best[i] = a->lines().front().pv.front();
    }
    std::vector<BoardArrow> out;
    for (const auto& a : sideArrows(best)) {
        const auto moves = legalPrefix(board, {a.move}, 1);
        if (moves.empty()) continue;
        BoardArrow arrow{moves.front(), (sideColour(a.engine) & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 215)};
        if (a.count > 1) arrow.count = std::to_string(a.count);
        out.push_back(std::move(arrow));
    }
    // The longest first, so that a shorter one along it (f2-f3 under f2-f4) stays in sight on top; between arrows as
    // long, the first engine's on top.
    const auto length = [](const BoardArrow& a) {
        const int from = a.move.from().index(), to = a.move.to().index();
        const int df = (to & 7) - (from & 7), dr = (to >> 3) - (from >> 3);
        return df * df + dr * dr;
    };
    std::reverse(out.begin(), out.end());
    std::stable_sort(out.begin(), out.end(),
                     [&](const BoardArrow& x, const BoardArrow& y) { return length(x) > length(y); });
    return out;
}

std::vector<EngineVerdict> App::sideVerdicts(const chess::Board& board, const AnalysisPosition& position) {
    std::vector<EngineVerdict> out;
    const auto list = analysisEngines(board_.settings);
    for (std::size_t i = 0; i < list.size(); ++i) {
        EngineVerdict v;
        v.name        = list[i].engine;
        const auto* a = sideAnalyser(i);
        if (a && a->active() && a->engine() == v.name && a->position() == position && !a->lines().empty()) {
            const auto& line = a->lines().front();
            if (!line.pv.empty()) {
                if (const auto moves = legalPrefix(board, {line.pv.front()}, 1); !moves.empty())
                    v.move = numbered(board, boardSan(chess::uci::moveToSan(board, moves.front())));
            }
            if (line.score && !line.lowerbound && !line.upperbound)
                v.white = whiteScore(*line.score, board.sideToMove());
        }
        out.push_back(std::move(v));
    }
    return out;
}

void App::keepSideTrails(const AnalysisPosition& position) {
    auto& b = board_;
    if (b.line >= 0) return;  // a variation's positions are not the game's
    const auto list = analysisEngines(b.settings);
    std::string key;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto* a = sideAnalyser(i);
        if (!a || !a->active() || a->engine() != list[i].engine || !(a->position() == position) || a->lines().empty())
            continue;
        if (key.empty()) {
            key = reviewKey(b.tree.main);
            if (key != b.trails_key) {  // another game, or another move in it
                b.trails.clear();
                b.trails_key = key;
            }
        }
        auto& trail  = b.trails[list[i].engine];
        trail.engine = list[i].engine;
        keepTrail(trail, b.tree.main.moves.size(), b.ply, a->lines().front());
    }
}

// ---------------------------------------------------------------------------
// Adding, removing, moving

bool App::addSideEngine(const std::string& name) {
    loadBoardSettings();
    auto& b = board_;
    if (aidsLocked()) {  // fair play (#48)
        b.message       = TR("Locked while one of your games is played on Lichess.");
        b.message_error = false;
        return false;
    }
    auto list = analysisEngines(b.settings);
    if (list.size() >= MAX_SIDE_ENGINES || !engineNamed(name) ||
        std::any_of(list.begin(), list.end(), [&](const SideEngine& e) { return e.engine == name; }))
        return false;
    SideEngine e;
    e.engine = name;
    list.push_back(e);
    setAnalysisEngines(b.settings, list);
    saveBoardSettings();
    // It searches at once: what it thinks is what it was added for.
    if (!b.analysing) setBoardAnalysis(true);
    return true;
}

namespace {

// The board's analysers in the list's order, the first included, as many as `count` at least.
std::vector<std::unique_ptr<Analyser>> takeAnalysers(BoardPage& b, std::size_t count) {
    std::vector<std::unique_ptr<Analyser>> all;
    all.push_back(std::move(b.analyser));
    for (auto& a : b.side) all.push_back(std::move(a));
    b.side.clear();
    while (all.size() < count) all.push_back(nullptr);
    for (auto& a : all)
        if (!a) a = std::make_unique<Analyser>();
    return all;
}

void giveAnalysers(BoardPage& b, std::vector<std::unique_ptr<Analyser>> all) {
    b.analyser = std::move(all.front());
    for (std::size_t i = 1; i < all.size(); ++i) b.side.push_back(std::move(all[i]));
}

}  // namespace

void App::removeSideEngine(std::size_t index) {
    auto& b           = board_;
    auto list         = analysisEngines(b.settings);
    const auto before = b.settings.analysis.engine;
    if (list.size() <= 1 || index >= list.size()) return;  // the analysis keeps one engine
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(index));
    setAnalysisEngines(b.settings, list);
    saveBoardSettings();
    // Its process ends; the others keep theirs.
    auto all = takeAnalysers(b, list.size() + 1);
    all.erase(all.begin() + static_cast<std::ptrdiff_t>(index));
    giveAnalysers(b, std::move(all));
    firstEngineChanged(before);
}

void App::moveSideEngine(std::size_t from, std::size_t to) {
    auto& b   = board_;
    auto list = analysisEngines(b.settings);
    if (from >= list.size() || to >= list.size() || from == to) return;
    const auto before = b.settings.analysis.engine;
    const auto engine = list[from];
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(from));
    list.insert(list.begin() + static_cast<std::ptrdiff_t>(to), engine);
    setAnalysisEngines(b.settings, list);
    saveBoardSettings();
    // The processes move with them: none starts again.
    auto all   = takeAnalysers(b, list.size());
    auto moved = std::move(all[from]);
    all.erase(all.begin() + static_cast<std::ptrdiff_t>(from));
    all.insert(all.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
    giveAnalysers(b, std::move(all));
    firstEngineChanged(before);
}

void App::firstEngineChanged(const std::string& before) {
    auto& b         = board_;
    const auto& now = b.settings.analysis.engine;
    if (now == before || now.empty()) return;
    b.review_offered = 0;
    if (b.threat_on) setBoardThreat(true);  // the threat is the first engine's: started again with the new one
    // The game's curve follows the first engine: its analysis of the game, when there is one, judges the moves.
    if (!b.tree.main.moves.empty()) {
        const auto kept = loadReviews(storage_.root() / "analyses", b.tree.main);
        if (std::any_of(kept.begin(), kept.end(), [&](const GameReview& r) { return r.engine == now; })) judgeWith(now);
    }
}

// ---------------------------------------------------------------------------
// The tab

void App::drawSideBySide() {
    // No engine in the library: the card that offers one (M9, board 2).
    if (engines_.empty()) {
        drawFirstEngineCard(
            TR("The analysis, the game's review, the questions and the assistant's comments need an "
               "engine."));
        return;
    }
    loadBoardSettings();
    auto& b = board_;
    if (b.settings.analysis.engine.empty()) (void)analysisEngine();  // the first that works, as A would take
    const auto list     = analysisEngines(b.settings);
    const auto threads  = sideThreads();
    const auto board    = boardAt(b.game, b.chess960, b.ply);
    const auto position = analysisPosition(b.game, b.chess960, b.ply);
    SideGesture gesture;  // an engine dragged onto another's place, made first, removed

    ui::beginCard("side_by_side");
    {
        const ui::TopicRegion region(guide::topics::SideBySide);
        // Its head: how many engines, the threads they share; at the right, another engine and the analysis's keys.
        ImGui::AlignTextToFramePadding();
        {
            const auto title = trn("Analysis · {} engine", "Analysis · {} engines", list.size(), list.size());
            const ImVec2 p   = ImGui::GetCursorScreenPos();
            ImGui::TextUnformatted(title.c_str());
            ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 0.6f, p.y + ImGui::GetStyle().FramePadding.y),
                                                ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
        }
        {
            int planned = 0;
            for (std::size_t i = 0; i < list.size(); ++i) {
                const auto* entry = engineNamed(list[i].engine);
                if (!(entry && searchesOnGpu(*entry)) || list[i].threads > 0) planned += threads[i];
            }
            const int busy = tournamentCores();
            auto chip      = trf("{} / {} threads", planned, topology_.logical);
            if (busy > 0) chip += " · " + trf("a tournament takes {}", busy);
            ui::sameLineOrWrap(chip.c_str());
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() / 2);
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Button((chip + "###side_threads").c_str()) || std::exchange(side_threads_open_, false))
                ImGui::OpenPopup("##analysis_threads");
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            if (ImGui::IsItemHovered()) {
                std::string tip =
                    trf("The analysis's threads: {}, shared equally between its engines, but those whose "
                        "threads are set by hand; a GPU engine's are not counted. A click sets them.",
                        std::max(1, b.settings.analysis.threads));
                if (busy > 0)
                    tip += "\n" + trf("A tournament is running here: it keeps its {} threads, the analysis takes at "
                                      "most {}.",
                                      busy, analysisTotal());
                tip += "\n" + trf("Now in use by the Board page: {}.", boardThreads());
                ImGui::SetTooltip("%s", tip.c_str());
            }
            drawAnalysisThreadsPopup();
        }
        {
            const char* add     = TR("An engine");
            const char* analyse = b.analysing ? TR("Stop") : TR("Analyse");
            const auto& style   = ImGui::GetStyle();
            const float icon    = ImGui::GetFrameHeight() * 0.42f + style.FramePadding.x * 2 + style.ItemInnerSpacing.x;
            const float width   = icon * 2 + ImGui::CalcTextSize(add).x + ImGui::CalcTextSize(analyse).x +
                                  ImGui::CalcTextSize("Alt+A").x + style.ItemSpacing.x * 2 + px(4);
            ui::sameLineOrWrap(width + px(8), px(8));
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - width));
            const bool full = list.size() >= MAX_SIDE_ENGINES;
            if (ui::iconButton("side_add", ui::Icon::Plus, add, !full) || std::exchange(side_menu_open_, false)) {
                side_menu_pick_ = 0;
                side_installed_.clear();
                ImGui::OpenPopup("##add_engine");
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", full ? TR("Four engines at most side by side: remove one first.")
                                             : TR("Another engine of the library analyses beside the others."));
            const ImVec2 under(ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y + px(4));
            if (ImGui::IsPopupOpen("##add_engine")) ImGui::SetNextWindowPos(under, ImGuiCond_Always, ImVec2(1, 0));
            drawAddEngineMenu();
            ImGui::SameLine();
            if (ui::iconButton("side_analyse", b.analysing ? ui::Icon::Stop : ui::Icon::Analyse, analyse))
                setBoardAnalysis(!b.analysing);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TR("Alt+A: the engines analyse the position shown, and follow the board."));
            ImGui::SameLine(0, px(4));
            ImGui::TextDisabled("Alt+A");
        }

        // How they agree, in words: the arrows need not be looked at.
        if (const auto runs = agreementRuns(sideVerdicts(board, position)); !runs.empty()) {
            const bool light  = ui::currentTheme() == ui::Theme::Light;
            const ImU32 fill  = light ? IM_COL32(245, 248, 254, 255) : IM_COL32(30, 40, 58, 255);
            const ImU32 edge  = light ? IM_COL32(201, 216, 245, 255) : IM_COL32(52, 74, 112, 255);
            const float pad_x = px(12), pad_y = px(7);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w  = ImGui::GetContentRegionAvail().x;
            const float h  = drawRuns(nullptr, p, w - pad_x * 2, runs, 0) + pad_y * 2;
            auto* dl       = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), fill, px(8));
            dl->AddRect(p, ImVec2(p.x + w, p.y + h), edge, px(8));
            drawRuns(dl, ImVec2(p.x + pad_x, p.y + pad_y), w - pad_x * 2, runs, ImGui::GetColorU32(ImGuiCol_Text));
            ImGui::Dummy(ImVec2(w, h));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s",
                                  TR("The engines' best moves and how far apart their evaluations are (White's)."));
        }

        // The engines, each a card.
        for (std::size_t i = 0; i < list.size(); ++i) drawSideEngineCard(i, list, threads, board, position, gesture);
        if (!b.analysing)
            ImGui::TextDisabled("%s", TR("Alt+A, or Analyse, for the engines' best moves here; + An engine sets "
                                         "another beside them."));

        // The threat, the first engine's.
        if (b.threat_on) {
            const auto& t = b.threat.lines();
            ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
            if (board.inCheck()) {
                ImGui::TextUnformatted(TR("Threat: none while in check."));
            } else if (!t.empty() && !t.front().pv.empty()) {
                auto passed = board;
                passed.makeNullMove();
                ImGui::TextUnformatted(trf("Threat: {} ({})", pvToSan(passed, t.front().pv, 4),
                                           t.front().score ? whiteText(t.front(), ~board.sideToMove()) : "")
                                           .c_str());
            } else {
                ImGui::TextUnformatted(TR("Threat: …"));
            }
            ImGui::PopStyleColor();
        }
    }
    ui::endCard();

    if (gesture.remove)
        removeSideEngine(*gesture.remove);
    else if (gesture.move)
        moveSideEngine(gesture.move->first, gesture.move->second);
}

void App::drawSideEngineCard(std::size_t index, const std::vector<SideEngine>& list, const std::vector<int>& threads,
                             const chess::Board& board, const AnalysisPosition& position, SideGesture& gesture) {
    auto& b            = board_;
    const auto& e      = list[index];
    const auto* entry  = engineNamed(e.engine);
    auto* a            = sideAnalyser(index);
    const bool mine    = a && a->engine() == e.engine;
    const bool gpu     = entry && searchesOnGpu(*entry);
    const auto stm     = board.sideToMove();
    const ImU32 colour = sideColour(index);

    ImGui::PushID(static_cast<int>(index));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(10), px(7)));
    ImGui::BeginChild("##engine_card", ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    auto* dl = ImGui::GetWindowDrawList();

    // Its colour and name: what is dragged onto another's place (to the top: the first).
    {
        const float dot  = px(12);
        const auto name  = e.engine;
        const float wide = dot + px(8) + ImGui::CalcTextSize(name.c_str()).x;
        ImGui::AlignTextToFramePadding();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##handle", ImVec2(wide, ImGui::GetFrameHeight()));
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("%s", index == 0
                                        ? TR("The first engine: the game's review and the assistant use it. Drag "
                                             "another here to make it the first.")
                                        : TR("Drag it onto another engine to take its place; at the top, it becomes "
                                             "the first, which the game's review and the assistant use."));
        }
        if (ImGui::BeginDragDropSource()) {
            const int from = static_cast<int>(index);
            ImGui::SetDragDropPayload(kEnginePayload, &from, sizeof from);
            ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }
        const float cy = p.y + ImGui::GetFrameHeight() / 2;
        dl->AddCircleFilled(ImVec2(p.x + dot / 2, cy), dot / 2, colour, 20);
        dl->AddCircle(ImVec2(p.x + dot / 2, cy), dot / 2, IM_COL32(255, 255, 255, 90), 20, 1.0f);
        const ImVec2 tp(p.x + dot + px(8), cy - ImGui::GetTextLineHeight() / 2);
        dl->AddText(tp, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
        dl->AddText(ImVec2(tp.x + 0.6f, tp.y), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
    }

    // Its evaluation, its search, its lines; or why there are none.
    const bool fresh = mine && a->active() && a->position() == position && !a->lines().empty();
    const auto* best = fresh ? &a->lines().front() : nullptr;
    const float menu = ImGui::GetFrameHeight();
    std::string waiting;  // in place of the evaluation
    if (!entry) {
        waiting = TR("not in the library any more");
    } else if (mine && a->state() == Analyser::State::Starting) {
        waiting = trf("Starting {}…", e.engine);
    } else if (mine && a->state() == Analyser::State::Failed) {
        waiting = TR("failed");
    } else if (!mine || !a->active()) {
        waiting = TR("waiting for Analyse");
    } else if (!fresh) {
        waiting = b.analysing ? "…" : TR("not this position");
    }
    if (best && best->score) {
        ImGui::SameLine(0, px(10));
        ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.3f);
        ImGui::TextUnformatted(whiteText(*best, stm).c_str());
        ImGui::PopFont();
        if (ImGui::IsItemHovered()) {
            std::string tip = TR("Its evaluation, White's.");
            if (best->wdl) {
                auto wdl = *best->wdl;
                if (stm == chess::Color::BLACK) std::swap(wdl[0], wdl[2]);
                tip += "\n" + trf("White wins {} %, draws {} %, loses {} %", wdl[0] / 10, wdl[1] / 10, wdl[2] / 10);
            }
            ImGui::SetTooltip("%s", tip.c_str());
        }
        auto search = trf("depth {}/{}", best->depth, best->seldepth) + " · " + ui::formatCount(best->nps) + " n/s";
        search += " · " + (gpu && e.threads <= 0 ? std::string(TR("on the GPU"))
                                                 : trn("{} thread", "{} threads", threads[index], threads[index]));
        ui::sameLineOrWrap(ImGui::CalcTextSize(search.c_str()).x + menu + px(12), px(10));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", search.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s",
                              trf("{} nodes · {} · tablebase hits {} · hash {}", ui::formatCount(best->nodes),
                                  ui::formatClock(best->time_ms), ui::formatCount(best->tbhits),
                                  best->hashfull >= 0 ? fmt::format("{} %", best->hashfull / 10) : std::string("–"))
                                  .c_str());
        }
    } else if (!waiting.empty()) {
        ui::sameLineOrWrap(ImGui::CalcTextSize(waiting.c_str()).x + menu + px(12), px(10));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", waiting.c_str());
    }
    // Its menu, at the right.
    {
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - menu));
        const auto tip = trf("{}: its lines, threads and hash; make it first, remove it", e.engine);
        if (ui::iconOnlyButton("##engine_menu_button", ui::Icon::More, tip.c_str()) ||
            side_engine_menu_ == static_cast<int>(index)) {
            if (side_engine_menu_ == static_cast<int>(index)) side_engine_menu_ = -1;
            ImGui::OpenPopup("##engine_menu");
        }
        if (ImGui::BeginPopup("##engine_menu")) {
            if (!drawSideEngineMenu(index, threads, gesture)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    if (mine && a->state() == Analyser::State::Failed) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextWrapped("%s", a->error().c_str());
        ImGui::PopStyleColor();
    } else if (!entry) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::BAD), "%s",
                           trf("{} is no longer in the library: add it there again, or remove it from the analysis "
                               "(its menu).",
                               e.engine)
                               .c_str());
        ImGui::PopTextWrapPos();
    }

    if (fresh) {
        const auto* figurines = appearance::current().figurines ? &atlas_ : nullptr;
        const auto& lines     = a->lines();
        // A line of moves: hovered, the position after a move on the board; clicked, the line played up to it.
        const auto line = [&](const AnalysisLine& l, const char* id, int max_lines) {
            auto items = lineItems(board, l.pv, 24);
            for (auto& it : items) {
                if (it.kind != NotationItem::Kind::Move) continue;
                it.bold = true;  // the first move strong
                break;
            }
            if (!figurines)  // letters rather than figurines: the language's (#66)
                for (auto& it : items)
                    if (it.kind == NotationItem::Kind::Move) it.text = localSan(it.text);
            const auto ev = drawNotation(id, items, ImGui::GetContentRegionAvail().x, figurines, max_lines);
            if (ev.hovered >= 0) {
                BoardPreview preview;
                preview.ply = preview.at = b.ply;
                preview.moves.assign(
                    l.pv.begin(),
                    l.pv.begin() + std::min<std::size_t>(l.pv.size(), static_cast<std::size_t>(ev.hovered) + 1));
                preview.label     = pvToSan(board, preview.moves, preview.moves.size());
                preview.hover     = true;
                b.preview         = std::move(preview);
                b.preview_hovered = true;
                ImGui::SetTooltip("%s", TR("Click: the line played up to this move. Scores are White's."));
            }
            if (ev.clicked >= 0) {
                for (const auto& m : legalPrefix(board, l.pv, static_cast<std::size_t>(ev.clicked) + 1)) boardPlay(m);
                b.preview.reset();
            }
            // The right button: "Why not?" its first move, against the game's (#94).
            if (ev.right_clicked >= 0 && !l.pv.empty()) {
                why_not_move_ = l.pv.front();
                ImGui::OpenPopup("##why_not_line");
            }
            if (ImGui::BeginPopup("##why_not_line")) {
                const auto moves = legalPrefix(board, {why_not_move_}, 1);
                if (!moves.empty() &&
                    ImGui::MenuItem(
                        trf("Why not {}?", numbered(board, boardSan(chess::uci::moveToSan(board, moves.front()))))
                            .c_str(),
                        "Alt+Q"))
                    whyNot(b.ply, why_not_move_, whyNotReference(board, why_not_move_));
                ImGui::EndPopup();
            }
        };
        if (!lines.front().pv.empty()) line(lines.front(), "##best", 2);
        // Its other lines, folded into one: a click shows them.
        std::vector<const AnalysisLine*> others;
        for (std::size_t i = 1; i < lines.size(); ++i)
            if (!lines[i].pv.empty()) others.push_back(&lines[i]);
        if (!others.empty()) {
            const bool unfolded = side_unfolded_.count(e.engine) > 0;
            std::string folded;
            for (const auto* l : others) {
                const auto moves = legalPrefix(board, {l->pv.front()}, 1);
                if (moves.empty()) continue;
                folded += (folded.empty() ? "" : " · ") +
                          numbered(board, boardSan(chess::uci::moveToSan(board, moves.front()))) + " " +
                          whiteText(*l, stm);
            }
            const auto label =
                unfolded ? std::string(TR("Fold the other lines"))
                         : trn("{} other line: {}", "{} other lines: {}", others.size(), others.size(), folded);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(
                    (label + "###fold").c_str(), false, 0,
                    ImVec2(std::min(ImGui::GetContentRegionAvail().x, ImGui::CalcTextSize(label.c_str()).x + px(4)),
                           0))) {
                if (unfolded)
                    side_unfolded_.erase(e.engine);
                else
                    side_unfolded_.insert(e.engine);
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", unfolded ? TR("Its best line only.") : TR("A click: each of its lines."));
            if (unfolded) {
                for (std::size_t i = 0; i < others.size(); ++i) {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%s", whiteText(*others[i], stm).c_str());
                    ImGui::SameLine(0, px(8));
                    line(*others[i], fmt::format("##line{}", i).c_str(), 1);
                }
            }
        }
    }
    if (gpu && e.threads <= 0)
        ImGui::TextDisabled("%s", TR("On the graphics card: its threads do not count in the share."));

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    // Another engine dropped here takes this place.
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload(kEnginePayload)) {
            const int from = *static_cast<const int*>(payload->Data);
            if (from >= 0 && static_cast<std::size_t>(from) != index)
                gesture.move = std::make_pair(static_cast<std::size_t>(from), index);
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::PopID();
}

bool App::drawSideEngineMenu(std::size_t index, const std::vector<int>& threads, SideGesture& gesture) {
    auto& b   = board_;
    auto list = analysisEngines(b.settings);
    if (index >= list.size()) return false;
    auto& e           = list[index];
    const auto* entry = engineNamed(e.engine);
    const bool gpu    = entry && searchesOnGpu(*entry);
    bool changed      = false;
    ImGui::TextDisabled("%s", e.engine.c_str());
    const auto number = [&](const char* id, const char* label, int& value, int lo, int hi, const char* tip) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::SameLine(px(90));
        ImGui::SetNextItemWidth(px(80));
        if (ui::inputInt(id, &value)) {
            value   = std::clamp(value, lo, hi);
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    };
    number("##side_lines", TR("Lines"), e.multipv, 1, 10, TR("How many best moves it shows (MultiPV)."));
    // Its threads: its share, or set by hand.
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(TR("Threads"));
        ImGui::SameLine(px(90));
        bool hand = e.threads > 0;
        if (ImGui::Checkbox(TR("set by hand"), &hand)) {
            e.threads = hand ? std::max(1, threads[index]) : 0;
            changed   = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", gpu ? TR("It searches on the graphics card: its threads, its own unless set "
                                             "here, do not count in the analysis's share.")
                                        : TR("Unticked, it takes its share of the analysis's threads; ticked, the "
                                             "threads set here, which the others' share leaves aside."));
        }
        if (hand) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(px(80));
            if (ui::inputInt("##side_threads", &e.threads)) {
                e.threads = std::clamp(e.threads, 1, std::max(1, topology_.logical));
                changed   = true;
            }
        } else {
            ImGui::SameLine();
            ImGui::TextDisabled(
                "%s",
                gpu ? TR("its own, on the GPU")
                    : trn("its share: {} thread", "its share: {} threads", threads[index], threads[index]).c_str());
        }
    }
    number("##side_hash", TR("Hash"), e.hash, 0, 1 << 20, TR("MB; 0: the engine's own."));
    if (changed) {
        setAnalysisEngines(b.settings, list);
        saveBoardSettings();
    }
    ImGui::Separator();
    bool open = true;
    if (ImGui::MenuItem(TR("Make it first"), nullptr, false, index > 0)) {
        gesture.move = std::make_pair(index, std::size_t{0});
        open         = false;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", TR("The first engine's analysis is the one the game's review and the assistant use."));
    if (ImGui::MenuItem(TR("Remove from the analysis"), nullptr, false, list.size() > 1)) {
        gesture.remove = index;
        open           = false;
    }
    if (list.size() <= 1 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", TR("The analysis keeps one engine: add another first."));
    ImGui::Separator();
    // The first engine's threat, and the processes of all of them.
    if (index == 0) {
        if (ImGui::MenuItem(TR("Show the threat"), "Alt+X", b.threat_on)) {
            setBoardThreat(!b.threat_on);
            open = false;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("The opponent's best move if it could move again, as a red arrow (another "
                                       "process of the first engine, its threads counted)."));
    }
    const bool started = std::any_of(b.side.begin(), b.side.end(), [](const auto& a) { return a && a->active(); }) ||
                         b.analyser->active() || b.threat.active();
    if (ImGui::MenuItem(TR("Unload the analysis's engines"), nullptr, false, started)) {
        unloadBoardEngines();
        open = false;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", TR("They end and free their memory; Analyse starts them again."));
    ImGui::TextDisabled("%s", TR("this analysis only: neither the library nor a tournament sees them"));
    return open;
}

void App::drawAnalysisThreadsPopup() {
    if (!ImGui::BeginPopup("##analysis_threads")) return;
    auto& s = board_.settings.analysis;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("The analysis's threads"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(80));
    if (ui::inputInt("##analysis_total", &s.threads)) {
        s.threads = std::clamp(s.threads, 1, std::max(1, topology_.logical));
        saveBoardSettings();
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + px(360));
    ImGui::TextDisabled("%s", TR("Shared equally between its engines, but those whose threads are set by hand (their "
                                 "menu); a GPU engine's are not counted. One setting for the whole analysis."));
    if (const int busy = tournamentCores(); busy > 0) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           trf("A tournament is running here: it keeps its {} threads, the analysis takes at most {}.",
                               busy, analysisTotal())
                               .c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::TextDisabled("%s", TR("this analysis only"));
    ImGui::EndPopup();
}

void App::drawAddEngineMenu() {
    if (!ImGui::BeginPopup("##add_engine")) return;
    auto& b         = board_;
    const auto list = analysisEngines(b.settings);
    const bool full = list.size() >= MAX_SIDE_ENGINES;
    // The library's engines: those of the analysis ticked, the others offered, Enter adding the one picked.
    struct Row {
        const EngineEntry* engine;
        bool in = false;
    };
    std::vector<Row> rows;
    std::vector<std::size_t> offered;  // rows that can be added
    for (const auto& e : engines_) {
        const bool in = std::any_of(list.begin(), list.end(), [&](const SideEngine& s) { return s.engine == e.name; });
        if (e.inactive && !in) continue;
        if (!in) offered.push_back(rows.size());
        rows.push_back({&e, in});
    }
    if (!offered.empty()) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            side_menu_pick_ = (side_menu_pick_ + 1) % static_cast<int>(offered.size());
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            side_menu_pick_ =
                (side_menu_pick_ + static_cast<int>(offered.size()) - 1) % static_cast<int>(offered.size());
        side_menu_pick_ = std::clamp(side_menu_pick_, 0, static_cast<int>(offered.size()) - 1);
    }
    // When an engine was installed, as far as the library tells.
    if (side_installed_.empty()) {
        if (!picker_.installed_read) {
            picker_.installed_read = true;
            picker_.installed      = catalogue::loadInstalled(storage_.root());
        }
        for (const auto& r : rows) {
            std::optional<std::time_t> when;
            for (const auto& i : picker_.installed)
                if (!r.engine->catalogue.empty() && i.id == r.engine->catalogue && i.program == r.engine->cmd)
                    when = static_cast<std::time_t>(i.installed);
            if (!when && r.engine->machine.empty()) {
                std::error_code ec;
                const auto written = std::filesystem::last_write_time(r.engine->cmd, ec);
                if (!ec)
                    when = std::chrono::system_clock::to_time_t(
                        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                            std::chrono::file_clock::to_sys(written)));
            }
            side_installed_[r.engine->name] = when ? trf("installed {}", ui::dayText(*when)) : std::string();
        }
        side_installed_[""] = "";  // read for this opening of the menu, even without engines
    }

    ImGui::TextDisabled("%s", TR("The library"));
    std::optional<std::string> chosen;
    const float width = px(380);
    for (std::size_t k = 0; k < rows.size(); ++k) {
        const auto& r = rows[k];
        ImGui::PushID(static_cast<int>(k));
        const bool picked  = !r.in && !offered.empty() && offered[static_cast<std::size_t>(side_menu_pick_)] == k;
        std::string detail = r.in ? std::string(TR("in the analysis")) : side_installed_[r.engine->name];
        if (searchesOnGpu(*r.engine)) detail += std::string(detail.empty() ? "" : " · ") + TR("GPU");
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(r.in || full);
        if (ImGui::Selectable("##row", picked, 0, ImVec2(width, ImGui::GetTextLineHeight() + px(6))))
            chosen = r.engine->name;
        ImGui::EndDisabled();
        if (full && !r.in && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", TR("Four engines at most side by side: remove one first."));
        auto* dl         = ImGui::GetWindowDrawList();
        const ImU32 text = ImGui::GetColorU32(r.in || full ? ImGuiCol_TextDisabled : ImGuiCol_Text);
        const float ty   = p.y + px(3);
        dl->AddText(ImVec2(p.x + px(4), ty), text, r.engine->name.c_str());
        dl->AddText(ImVec2(p.x + px(4.6f), ty), text, r.engine->name.c_str());
        if (!detail.empty()) {
            const float x = p.x + px(4) + ImGui::CalcTextSize(r.engine->name.c_str()).x + px(6);
            dl->AddText(ImVec2(x, ty), ImGui::GetColorU32(ImGuiCol_TextDisabled), ("· " + detail).c_str());
        }
        const char* right = r.in ? "✓" : picked ? TR("Enter") : "";
        if (*right) {
            const float rw = ImGui::CalcTextSize(right).x;
            dl->AddText(ImVec2(p.x + width - rw - px(6), ty), ImGui::GetColorU32(ImGuiCol_TextDisabled), right);
        }
        ImGui::PopID();
    }
    if (!offered.empty() && !full && keyboard::enterPressed())
        chosen = rows[offered[static_cast<std::size_t>(side_menu_pick_)]].engine->name;
    ImGui::Separator();
    if (ImGui::Selectable(TR("Download an engine…"), false, 0, ImVec2(width, 0))) {
        ImGui::CloseCurrentPopup();
        page_ = Page::Engines;
        openEnginePicker();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("The popular engines, from their authors' releases (the Engines page)."));
    // What adding the one picked does to the threads.
    if (!offered.empty() && !full) {
        const auto& picked = *rows[offered[static_cast<std::size_t>(side_menu_pick_)]].engine;
        std::vector<ThreadAsk> asks;
        for (const auto& e : list) {
            const auto* entry = engineNamed(e.engine);
            asks.push_back({e.threads, entry && searchesOnGpu(*entry)});
        }
        asks.push_back({0, searchesOnGpu(picked)});
        const auto shared = shareThreads(analysisTotal(), asks);
        int low = 1 << 20, high = 0;
        for (std::size_t i = 0; i < asks.size(); ++i) {
            if (asks[i].hand > 0 || asks[i].gpu) continue;
            low  = std::min(low, shared[i]);
            high = std::max(high, shared[i]);
        }
        const auto each = low == high ? trn("{} thread each", "{} threads each", high, high)
                                      : trf("{} to {} threads each", low, high);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextDisabled(
            "%s",
            asks.back().gpu
                ? trf("Added, {} searches on the GPU: its threads do not count in the share.", picked.name).c_str()
                : trf("Added, it takes its share of the analysis's threads: {} engines, {}.", asks.size(), each)
                      .c_str());
        ImGui::TextDisabled("%s", TR("Its settings here are the analysis's; the library and tournaments see none of "
                                     "them."));
        ImGui::PopTextWrapPos();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    if (chosen) {
        ImGui::CloseCurrentPopup();
        (void)addSideEngine(*chosen);
    }
    ImGui::EndPopup();
}

}  // namespace fcg
