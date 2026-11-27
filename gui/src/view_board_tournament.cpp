// A tournament's game on the study Board (#87, mockup M12, board 8): its engines' evaluation and depth beside each
// move, every field of a move under the mouse, the line they expected on the board or as a variation; the review
// tab's curve as the two engines saw the game, drawn from the game alone, the review's over it once done, their
// moves' times under it, their figures and the moments that stand out.

#include <algorithm>
#include <cmath>
#include <tuple>

#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include "app.hpp"
#include "board_text.hpp"
#include "i18n.hpp"
#include "keyboard.hpp"
#include "live.hpp"
#include "review_bridge.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

bool light() { return ui::currentTheme() == ui::Theme::Light; }

ImU32 withAlpha(ImU32 col, int alpha) {
    return (col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

}  // namespace

EngineTags App::engineTags() const { return static_cast<EngineTags>(std::clamp(board_.settings.engine_tags, 0, 2)); }

void App::drawEngineTagChips() {
    auto& b = board_;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TR("Beside the moves:"));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("What the notation shows beside the moves of a tournament's engines: the evaluation "
                                   "they gave (for White, as the bar and the curve) and their depth, with their time, "
                                   "or nothing. Every other field is under the mouse; the PGN keeps them all."));
    const char* labels[] = {TR("evaluation and depth"), TR("and the time"), TR("nothing")};
    for (int i = 0; i < 3; ++i) {
        ImGui::SameLine();
        const bool on = engineTags() == static_cast<EngineTags>(i);
        if (on) {  // the choice made stays blue under the mouse, its text white
            const auto accent = ImGui::ColorConvertU32ToFloat4(ui::ACCENT);
            ImGui::PushStyleColor(ImGuiCol_Button, accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        }
        if (ImGui::SmallButton(fmt::format("{}##engine_tags{}", labels[i], i).c_str()) && !on) {
            b.settings.engine_tags = i;
            saveBoardSettings();
        }
        if (on) ImGui::PopStyleColor(4);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("· %s", keyboard::name(keyboard::PALETTE).c_str());
}

void App::drawEngineMoveTip(int ply, const MoveReview& judged) {
    const auto& b = board_;
    const auto d  = engineDetail(b.tree.main, b.chess960, ply);
    if (!d || !ImGui::BeginTooltip()) return;
    const float fs = ImGui::GetFontSize();
    ImGui::PushTextWrapPos(fs * 27);
    ImGui::PushFont(ui::titleFont(), fs);
    ImGui::TextUnformatted(d->title.c_str());
    ImGui::PopFont();
    const char* chances  = TR("Its chances");
    const char* expected = TR("It expected");
    float label_w        = std::max(ImGui::CalcTextSize(chances).x, ImGui::CalcTextSize(expected).x);
    for (const auto& [label, text] : d->rows) label_w = std::max(label_w, ImGui::CalcTextSize(label.c_str()).x);
    label_w += px(12);
    const float x0 = ImGui::GetCursorPosX();
    for (const auto& [label, text] : d->rows) {
        ImGui::TextDisabled("%s", label.c_str());
        ImGui::SameLine(x0 + label_w);
        ImGui::TextWrapped("%s", text.c_str());
    }
    if (d->wdl) {
        // The mover's win, draw and loss shares: a bar, then their figures.
        ImGui::TextDisabled("%s", chances);
        ImGui::SameLine(x0 + label_w);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float w = px(170), h = px(8), y = at.y + (ImGui::GetTextLineHeight() - h) / 2;
        auto* dl            = ImGui::GetWindowDrawList();
        const ImU32 cols[3] = {IM_COL32(29, 127, 78, 255),
                               light() ? IM_COL32(201, 206, 214, 255) : IM_COL32(96, 102, 114, 255),
                               light() ? IM_COL32(43, 48, 56, 255) : IM_COL32(16, 18, 22, 255)};
        float x             = at.x;
        for (int k = 0; k < 3; ++k) {
            const float seg = w * (*d->wdl)[static_cast<std::size_t>(k)];
            if (seg > 0) dl->AddRectFilled(ImVec2(x, y), ImVec2(x + seg, y + h), cols[k]);
            x += seg;
        }
        dl->AddRect(ImVec2(at.x - 1, y - 1), ImVec2(at.x + w + 1, y + h + 1), ImGui::GetColorU32(ImGuiCol_Border));
        ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
        ImGui::SetCursorPosX(x0 + label_w);
        ImGui::TextDisabled("%s", d->wdl_text.c_str());
    }
    if (!d->expected.empty()) {
        ImGui::TextDisabled("%s", expected);
        ImGui::SameLine(x0 + label_w);
        ImGui::TextWrapped("%s", d->expected.c_str());
    }
    if (judged.judgement != Judgement::None) {
        ImGui::Separator();
        ImGui::TextUnformatted(judgementTip(ply - 1, judged).c_str());
    }
    ImGui::Spacing();
    ImGui::TextDisabled("%s", TR("A click goes to the move · the right button: the line it expected, on the board or "
                                 "as a variation, and its comment as it is"));
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

void App::boardExpectedVariation(int ply) {
    auto& b          = board_;
    const auto& main = b.tree.main;
    if (ply < 1 || ply > static_cast<int>(main.moves.size())) return;
    const auto pv = expectedLine(main.moves[static_cast<std::size_t>(ply - 1)], b.chess960);
    // From where the game leaves the line: the moves it played as expected stay the game's.
    auto board    = boardAt(main, b.chess960, ply);
    std::size_t k = 0;
    while (k < pv.size() && ply + static_cast<int>(k) < static_cast<int>(main.moves.size()) &&
           chess::uci::moveToUci(main.moves[static_cast<std::size_t>(ply) + k].move, b.chess960) == pv[k]) {
        board.makeMove<true>(main.moves[static_cast<std::size_t>(ply) + k].move);
        ++k;
    }
    if (k == pv.size()) {
        boardToast(TR("The game went the way it expected"));
        return;
    }
    if (ply + static_cast<int>(k) == static_cast<int>(main.moves.size())) {
        boardToast(TR("The game ends there: its line goes past the game's end"));
        return;
    }
    const auto moves = legalPrefix(
        board, std::vector<std::string>(pv.begin() + static_cast<std::ptrdiff_t>(k), pv.end()), pv.size() - k);
    if (moves.empty()) return;
    auto before = boardSnapshot();
    int line = -1, at = ply + static_cast<int>(k);
    for (const auto& move : moves) std::tie(line, at) = playInTree(b.tree, b.chess960, line, at, move);
    boardChanged(std::move(before));
    b.game = lineOf(b.tree, line);
    showBoardLine(line, ply + static_cast<int>(k) + 1);
    boardToast(TR("The line it expected, a variation · Ctrl+Z takes it away"));
}

void App::drawEnginesCurve() {
    auto& b          = board_;
    const auto& main = b.tree.main;
    // Made once per game (and language: its moments are sentences).
    const auto key = reviewKey(main) + "|" + std::to_string(static_cast<int>(i18n::language()));
    if (key != engine_curves_of_) {
        engine_curves_    = engineCurves(main);
        engine_curves_of_ = key;
    }
    const auto& c       = engine_curves_;
    const bool reviewed = !b.reviews.empty() && b.reviews.front().complete(main.moves.size());

    ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize());
    ImGui::TextUnformatted(TR("What the two engines thought"));
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("· %s", reviewed ? TR("the review's curve over theirs, thicker")
                                         : TR("without analysis: the curve comes from the game"));
    drawGameCurve(&c);

    // Under it, the time of each move: White's above the line, Black's below.
    const float w = ImGui::GetContentRegionAvail().x, h = px(44), pad = px(8);
    const int plies = std::max(1, c.plies);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##engines_time", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    auto* dl        = ImGui::GetWindowDrawList();
    const auto x_of = [&](float ply) { return p0.x + pad + (w - 2 * pad) * ply / static_cast<float>(plies); };
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h),
                      light() ? IM_COL32(243, 245, 248, 255) : IM_COL32(32, 36, 43, 255), px(6));
    const float mid = p0.y + h / 2;
    dl->AddLine(ImVec2(p0.x + pad, mid), ImVec2(p0.x + w - pad, mid),
                light() ? IM_COL32(201, 206, 214, 255) : IM_COL32(70, 76, 88, 255));
    int64_t longest = 1;
    for (const auto& side : c.time)
        for (const auto& [ply, ms] : side) longest = std::max(longest, ms);
    const float bar = std::clamp((w - 2 * pad) / static_cast<float>(plies) * 0.55f, 1.0f, px(6));
    for (int side = 0; side < 2; ++side) {
        const ImU32 col = withAlpha(graphPlayerColour(side), 220);
        for (const auto& [ply, ms] : c.time[static_cast<std::size_t>(side)]) {
            const float len =
                std::max(px(0.6f), (h / 2 - px(3)) * static_cast<float>(ms) / static_cast<float>(longest));
            const float x = x_of(static_cast<float>(ply)) - bar / 2;
            if (side == 0)
                dl->AddRectFilled(ImVec2(x, mid - len), ImVec2(x + bar, mid), col);
            else
                dl->AddRectFilled(ImVec2(x, mid), ImVec2(x + bar, mid + len), col);
        }
    }
    if (b.line < 0) {
        const float cx = x_of(static_cast<float>(boardMainPly()));
        dl->AddLine(ImVec2(cx, p0.y + px(3)), ImVec2(cx, p0.y + h - px(3)), ui::MUTED);
    }
    if (hovered || active) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const int ply  = std::clamp(
            static_cast<int>(std::lround((m.x - p0.x - pad) / std::max(1.0f, w - 2 * pad) * static_cast<float>(plies))),
            0, plies);
        if (active && (b.line >= 0 || boardMainPly() != ply)) showBoardLine(-1, ply);
        if (hovered && ply > 0) {
            const auto& move = main.moves[static_cast<std::size_t>(ply - 1)];
            std::string tip  = boardMoveLabel(ply);
            if (move.book)
                tip += " · " + std::string(TR("book move"));
            else if (const auto t = moveTime(move); t >= 0)
                tip += " · " + ui::formatClock(t);
            ImGui::SetTooltip("%s", tip.c_str());
        }
    }

    // Each engine: its colour, its side, its time, its depth and speed on average.
    for (int side = 0; side < 2; ++side) {
        const auto s = static_cast<std::size_t>(side);
        if (c.win[s].empty() && c.time[s].empty()) continue;
        std::string text = fmt::format("{}, {}", c.names[s], side == 0 ? TR("White") : TR("Black"));
        std::string figures;
        const auto add = [&](const std::string& part) { figures += (figures.empty() ? "" : ", ") + part; };
        if (c.time_used[s] > 0) add(ui::formatDuration(static_cast<double>(c.time_used[s]) / 1000.0));
        if (c.depth[s]) add(trf("average depth {:.1f}", *c.depth[s]));
        if (c.nps[s]) add(ui::formatCount(static_cast<uint64_t>(*c.nps[s])) + " n/s");
        if (!figures.empty()) text += " · " + figures;
        if (side == 1) ui::sameLineOrWrap(ImGui::CalcTextSize(text.c_str()).x + px(30));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float y   = at.y + ImGui::GetTextLineHeight() / 2;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, y - px(1.5f)), ImVec2(at.x + px(18), y + px(1.5f)),
                                                  graphPlayerColour(side));
        ImGui::Dummy(ImVec2(px(24), ImGui::GetTextLineHeight()));
        ImGui::SameLine(0, 0);
        ImGui::TextDisabled("%s", text.c_str());
    }
    // The review's curve, over theirs.
    if (reviewed) {
        const auto text = fmt::format("{} · {}", b.reviews.front().engine, TR("analysis"));
        ui::sameLineOrWrap(ImGui::CalcTextSize(text.c_str()).x + px(30));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float y   = at.y + ImGui::GetTextLineHeight() / 2;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, y - px(2)), ImVec2(at.x + px(18), y + px(2)),
                                                  ImGui::GetColorU32(ImGuiCol_Text, 0.85f));
        ImGui::Dummy(ImVec2(px(24), ImGui::GetTextLineHeight()));
        ImGui::SameLine(0, 0);
        ImGui::TextDisabled("%s", text.c_str());
    }
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s",
                        TR("White's chances: the share of wins, draws and losses (WDL) each engine gives, turned "
                           "around for Black; otherwise its evaluation converted, as the review does. Under them, "
                           "the time of each move, White's above the line and Black's below."));
    if (reviewed)
        ImGui::TextDisabled("%s", TR("A click on the curve goes to the move. The dots are the key moments: dark for a "
                                     "good move, orange for a mistake."));
    ImGui::PopTextWrapPos();

    // The moments that stand out, each a click away.
    if (!c.moments.empty()) {
        ImGui::Spacing();
        std::vector<std::string> labels;
        float label_w = 0;
        for (const auto& m : c.moments) {
            auto label = boardMoveLabel(m.ply);
            if (m.last > m.ply) label += " → " + boardMoveLabel(m.last);
            label_w = std::max(label_w, ImGui::CalcTextSize(label.c_str()).x);
            labels.push_back(std::move(label));
        }
        label_w = std::min(label_w + px(16), ImGui::GetContentRegionAvail().x * 0.4f);
        for (std::size_t k = 0; k < c.moments.size(); ++k) {
            const auto& m = c.moments[k];
            ImGui::PushID(static_cast<int>(k));
            const bool current = b.line < 0 && boardMainPly() == m.ply;
            if (ImGui::Selectable("##engine_moment", current, ImGuiSelectableFlags_None)) showBoardLine(-1, m.ply);
            ImGui::SameLine(px(4));
            ImGui::TextUnformatted(labels[k].c_str());
            ImGui::SameLine(label_w);
            ImGui::TextWrapped("%s", m.text.c_str());
            ImGui::PopID();
        }
    }
}

void App::drawReviewCurve() { drawGameCurve(nullptr); }

void App::drawGameCurve(const EngineCurves* engines) {
    auto& b          = board_;
    const auto& main = b.tree.main;
    if (main.moves.empty()) return;
    // The review's curve, once the game is analysed; drawn over the engines' when they are there.
    const bool reviewed = !b.reviews.empty() && b.reviews.front().complete(main.moves.size());
    GraphCurve curve;
    if (!b.reviews.empty() && (reviewed || !engines)) {
        const auto graph =
            buildGameGraph(main, b.chess960, GraphChart::Win,
                           {GraphAnalysis{b.reviews.front().engine, "", 0, &b.reviews.front()}}, b.judged);
        if (!graph.analyses.empty()) curve = graph.analyses.front();
    }
    if (!engines && curve.x.empty()) return;
    const int plies   = std::max(1, static_cast<int>(main.moves.size()));
    const auto win_at = [&](int ply) {
        for (std::size_t i = 0; i < curve.x.size() && i < curve.win.size(); ++i)
            if (static_cast<int>(std::lround(curve.x[i])) == ply) return curve.win[i];
        return std::nanf("");
    };

    const float w = ImGui::GetContentRegionAvail().x, h = engines ? px(116) : px(88), pad = px(8);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##review_curve", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    auto* dl        = ImGui::GetWindowDrawList();
    const auto x_of = [&](float ply) { return p0.x + pad + (w - 2 * pad) * ply / static_cast<float>(plies); };
    const auto y_of = [&](float win) { return p0.y + pad + (h - 2 * pad) * (1.0f - std::clamp(win, 0.0f, 1.0f)); };
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h),
                      light() ? IM_COL32(243, 245, 248, 255) : IM_COL32(32, 36, 43, 255), px(6));
    if (engines) {
        // The book, and where the two engines saw different games.
        if (engines->book > 0) {
            dl->AddRectFilled(ImVec2(p0.x + pad, p0.y + pad),
                              ImVec2(x_of(static_cast<float>(engines->book)), p0.y + h - pad),
                              light() ? IM_COL32(230, 234, 240, 255) : IM_COL32(40, 45, 54, 255));
            const float fs = ImGui::GetFontSize() * 0.75f;
            dl->AddText(ImGui::GetFont(), fs, ImVec2(p0.x + pad + px(4), p0.y + h - pad - fs - px(2)), ui::MUTED,
                        TR("book"));
        }
        for (const auto& [first, last] : engines->apart)
            dl->AddRectFilled(ImVec2(x_of(static_cast<float>(first)), p0.y + pad),
                              ImVec2(std::max(x_of(static_cast<float>(last)), x_of(static_cast<float>(first)) + px(3)),
                                     p0.y + h - pad),
                              IM_COL32(224, 145, 47, 34));
    }
    // Even chances, and the move shown.
    dl->AddLine(ImVec2(p0.x + pad, y_of(0.5f)), ImVec2(p0.x + w - pad, y_of(0.5f)),
                light() ? IM_COL32(201, 206, 214, 255) : IM_COL32(70, 76, 88, 255));
    if (b.line < 0) {
        const float cx = x_of(static_cast<float>(boardMainPly()));
        dl->AddLine(ImVec2(cx, p0.y + px(3)), ImVec2(cx, p0.y + h - px(3)), ui::MUTED);
    }
    // Each engine's chances at its own moves; thinner once the review's curve is over them.
    if (engines) {
        for (int side = 1; side >= 0; --side) {
            std::vector<ImVec2> points;
            for (const auto& p : engines->win[static_cast<std::size_t>(side)])
                points.emplace_back(x_of(static_cast<float>(p.ply)), y_of(p.win));
            if (points.size() >= 2)
                dl->AddPolyline(points.data(), static_cast<int>(points.size()), graphPlayerColour(side),
                                ImDrawFlags_None, curve.x.empty() ? px(side == 0 ? 2.2f : 1.8f) : px(1.2f));
        }
    }
    {
        std::vector<ImVec2> points;
        for (std::size_t i = 0; i < curve.x.size() && i < curve.win.size(); ++i)
            if (std::isfinite(curve.win[i])) points.emplace_back(x_of(curve.x[i]), y_of(curve.win[i]));
        // Over the engines' in the text's colour, apart from their blue and orange; alone in the accent's, as before.
        const ImU32 col = engines ? ImGui::GetColorU32(ImGuiCol_Text, 0.85f) : ui::ACCENT;
        if (points.size() >= 2)
            dl->AddPolyline(points.data(), static_cast<int>(points.size()), col, ImDrawFlags_None,
                            engines ? px(2.6f) : px(2.2f));
    }

    // The review's key moments, dark for a good move, orange for a mistake; the engines' moments as rings.
    struct Dot {
        int ply;
        ImVec2 at;
        const EngineMoment* moment = nullptr;
    };
    std::vector<Dot> dots;
    if (!curve.x.empty()) {
        if (const auto review = boardCommentaryReview()) {
            for (const auto key : review->report.key_moments) {
                const int ply = static_cast<int>(key);
                if (const float v = win_at(ply); std::isfinite(v)) dots.push_back({ply, ImVec2(x_of(ply), y_of(v))});
            }
        }
    }
    const auto judgement = [&](int ply) {
        return ply >= 1 && static_cast<std::size_t>(ply - 1) < b.judged.size()
                   ? b.judged[static_cast<std::size_t>(ply - 1)].judgement
                   : Judgement::None;
    };
    const ImU32 good = light() ? IM_COL32(31, 79, 158, 255) : IM_COL32(150, 186, 240, 255);
    const ImU32 bad  = IM_COL32(224, 138, 46, 255);
    for (const auto& d : dots) {
        dl->AddCircleFilled(d.at, px(6), light() ? IM_COL32(255, 255, 255, 255) : IM_COL32(32, 36, 43, 255));
        dl->AddCircleFilled(d.at, px(4.5f), judgement(d.ply) != Judgement::None ? bad : good);
    }
    std::vector<Dot> rings;
    if (engines) {
        for (const auto& m : engines->moments) {
            if (m.kind == EngineMoment::Kind::BookEnd || m.kind == EngineMoment::Kind::End) continue;
            const int side =
                m.kind == EngineMoment::Kind::Apart
                    ? (main.moves[static_cast<std::size_t>(m.ply - 1)].mover == chess::Color::WHITE ? 0 : 1)
                    : m.side;
            const float v = curveAt(*engines, side, m.ply);
            if (!std::isfinite(v)) continue;
            const ImVec2 at(x_of(static_cast<float>(m.ply)), y_of(v));
            const ImU32 col = m.kind == EngineMoment::Kind::Mate    ? IM_COL32(29, 127, 78, 255)
                              : m.kind == EngineMoment::Kind::Apart ? IM_COL32(194, 86, 12, 255)
                                                                    : ui::MUTED;
            if (m.kind == EngineMoment::Kind::Mate) {  // from here the result is seen: a dashed line down
                for (float y = p0.y + pad; y < p0.y + h - pad; y += px(6))
                    dl->AddLine(ImVec2(at.x, y), ImVec2(at.x, std::min(y + px(3), p0.y + h - pad)), ui::MUTED);
            }
            dl->AddCircleFilled(at, px(4.5f), light() ? IM_COL32(255, 255, 255, 255) : IM_COL32(32, 36, 43, 255));
            dl->AddCircle(at, px(4.5f), col, 0, px(2));
            rings.push_back({m.ply, at, &m});
        }
    }

    // The move under the pointer, a dot first: its tooltip, and where a click or a drag goes.
    if (hovered || active) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        // (Not "near": Windows' headers make it a macro.)
        const Dot* under = nullptr;
        for (const auto& d : rings)
            if (std::hypot(d.at.x - m.x, d.at.y - m.y) <= px(8)) under = &d;
        for (const auto& d : dots)
            if (std::hypot(d.at.x - m.x, d.at.y - m.y) <= px(8)) under = &d;
        const int ply = under
                            ? under->ply
                            : std::clamp(static_cast<int>(std::lround((m.x - p0.x - pad) / std::max(1.0f, w - 2 * pad) *
                                                                      static_cast<float>(plies))),
                                         0, plies);
        if (active && (b.line >= 0 || boardMainPly() != ply)) showBoardLine(-1, ply);
        if (hovered) {
            std::string tip = ply == 0 ? std::string(TR("at the start")) : boardMoveLabel(ply);
            if (ply > 0) tip += judgementMark(judgement(ply));
            if (const float v = win_at(ply); std::isfinite(v) && !engines)
                tip += " · " + trf("White's chances {} %", static_cast<int>(std::lround(v * 100.0f)));
            if (const float v = win_at(ply); std::isfinite(v) && engines)  // told apart from the engines' own
                tip += "\n" + trf("{}: {} %", trf("{} (analysis)", b.reviews.front().engine),
                                  static_cast<int>(std::lround(v * 100.0f)));
            if (engines) {
                for (int side = 0; side < 2; ++side) {
                    if (const float v = curveAt(*engines, side, ply); std::isfinite(v))
                        tip += "\n" + trf("{}: {} %", engines->names[static_cast<std::size_t>(side)],
                                          static_cast<int>(std::lround(v * 100.0f)));
                }
            }
            if (under && under->moment) tip += "\n" + under->moment->text;
            if (under && !under->moment && review_moments_of_ == board_commentary_of_) {
                for (const auto& [at, text] : review_moments_)
                    if (static_cast<int>(at) == ply) tip += "\n" + text;
            }
            if (ImGui::BeginTooltip()) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
                ImGui::TextUnformatted(tip.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
    }
    if (!curve.x.empty() && !engines) {  // with the engines', said under their legend
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", TR("A click on the curve goes to the move. The dots are the key moments: dark for a "
                                     "good move, orange for a mistake."));
        ImGui::PopTextWrapPos();
    }
}

}  // namespace fcg
