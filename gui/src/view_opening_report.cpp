// The opening report of a position (#91, mockup M13, board 4), in place of the Board page (Esc goes back to the
// position): opened from the Tree tab ("Report on this position"), a line of the Openings page and Ctrl+K. At its top
// the position, its opening and its moves, and what it is made of (the Tree tab's sources: the user's databases, and
// Lichess's masters and players when ticked; the evaluations' sources); then five tabs: the moves with their figures
// and the main lines, the structures and plans, the key games, the novelties, the assistant's summary. Made again in
// the background at each opening (opening_report.hpp), section by section, with its progress and Cancel (R18); locked
// during a live game on Lichess, without evaluations nor summary on a correspondence game's position (R17).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <utility>

#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <commentary/fact_parts.hpp>
#include <study/dossier.hpp>
#include <study/import.hpp>
#include <study/scan_board.hpp>

#include "app.hpp"
#include "assistant_service.hpp"
#include "board_text.hpp"
#include "fide_rating_list.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

commentary::Lang lang() {
    return i18n::language() == i18n::Language::French ? commentary::Lang::Fr : commentary::Lang::En;
}

int percent(uint64_t part, uint64_t whole) {
    return whole ? static_cast<int>(std::lround(100.0 * static_cast<double>(part) / static_cast<double>(whole))) : 0;
}

// White's score in a set of games, in percent; -1 without results.
int whiteScore(const study::TreeStats& s) {
    const auto decided = s.white + s.draws + s.black;
    return decided ? percent(2 * s.white + s.draws, 2 * decided) : -1;
}

// A table cell's text: wider than the cell, it ends in "…", whole under the mouse.
void cellText(const std::string& text, bool muted = false) {
    const float room  = ImGui::GetContentRegionAvail().x;
    std::string shown = text;
    if (ImGui::CalcTextSize(text.c_str()).x > room) {
        while (!shown.empty() && ImGui::CalcTextSize((shown + "…").c_str()).x > room) {
            while (!shown.empty() && (static_cast<unsigned char>(shown.back()) & 0xC0) == 0x80) shown.pop_back();
            if (!shown.empty()) shown.pop_back();
        }
        shown += "…";
    }
    if (muted)
        ImGui::TextDisabled("%s", shown.c_str());
    else
        ImGui::TextUnformatted(shown.c_str());
    if (shown != text && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem))
        ImGui::SetTooltip("%s", text.c_str());
}

// A text cut to `room` pixels, ending in "…" when it is wider.
std::string fitted(const std::string& text, float room) {
    if (ImGui::CalcTextSize(text.c_str()).x <= room) return text;
    std::string shown = text;
    while (!shown.empty() && ImGui::CalcTextSize((shown + "…").c_str()).x > room) {
        while (!shown.empty() && (static_cast<unsigned char>(shown.back()) & 0xC0) == 0x80) shown.pop_back();
        if (!shown.empty()) shown.pop_back();
    }
    return shown + "…";
}

// A source as a chip: filled when it is in, outlined in dashes when a click adds it. True when clicked.
bool chip(const char* id, const std::string& text, bool on) {
    const ImVec2 pad(px(10), px(3));
    const ImVec2 label = ImGui::CalcTextSize(text.c_str());
    const ImVec2 size(label.x + pad.x * 2, label.y + pad.y * 2);
    const ImVec2 pos     = ImGui::GetCursorScreenPos();
    const bool clicked   = ImGui::InvisibleButton(id, size);
    const bool hovered   = ImGui::IsItemHovered();
    auto* dl             = ImGui::GetWindowDrawList();
    const ImVec2 end     = ImVec2(pos.x + size.x, pos.y + size.y);
    const float rounding = size.y * 0.5f;
    if (on) {
        dl->AddRectFilled(pos, end, (ui::ACCENT & 0x00ffffff) | (hovered ? 0x55000000 : 0x33000000), rounding);
        dl->AddRect(pos, end, ui::ACCENT, rounding);
    } else {
        if (hovered) dl->AddRectFilled(pos, end, ImGui::GetColorU32(ImGuiCol_FrameBgHovered), rounding);
        const ImU32 col  = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const float dash = px(4), gap = px(3);
        const auto line = [&](ImVec2 a, ImVec2 b) {
            const float length = std::hypot(b.x - a.x, b.y - a.y);
            for (float t = 0; t < length; t += dash + gap) {
                const float u = std::min(t + dash, length);
                dl->AddLine(ImVec2(a.x + (b.x - a.x) * t / length, a.y + (b.y - a.y) * t / length),
                            ImVec2(a.x + (b.x - a.x) * u / length, a.y + (b.y - a.y) * u / length), col);
            }
        };
        line(ImVec2(pos.x + rounding, pos.y), ImVec2(end.x - rounding, pos.y));
        line(ImVec2(pos.x + rounding, end.y - 1), ImVec2(end.x - rounding, end.y - 1));
        constexpr float pi = 3.14159265f;
        dl->PathArcTo(ImVec2(pos.x + rounding, pos.y + rounding), rounding - 0.5f, pi * 0.5f, pi * 1.5f, 10);
        dl->PathStroke(col, 0);
        dl->PathArcTo(ImVec2(end.x - rounding, pos.y + rounding), rounding - 0.5f, -pi * 0.5f, pi * 0.5f, 10);
        dl->PathStroke(col, 0);
    }
    dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), on ? ui::ACCENT : ImGui::GetColorU32(ImGuiCol_Text),
                text.c_str());
    return clicked;
}

// A list said as the interface's language joins its items.
std::string listed(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& item : items) out = out.empty() ? item : trf("{}; {}", out, item);
    return out;
}

// A structure's name, as the assistant's facts name its formation.
std::string formationName(int kind) {
    using F = commentary::Formation;
    switch (static_cast<F>(kind)) {
        case F::Carlsbad:
            return TR("Carlsbad");
        case F::Maroczy:
            return TR("Maroczy bind");
        case F::Hedgehog:
            return TR("Hedgehog");
        case F::Stonewall:
            return TR("Stonewall");
        case F::KingsideChain:
            return TR("Pawn chain towards the king's wing");
        case F::QueensideChain:
            return TR("Pawn chain towards the queen's wing");
        case F::Benoni:
            return TR("Benoni");
        case F::Isolani:
            return TR("Isolated queen's pawn");
    }
    return {};
}

// A structure as the report's cards name it: kept from the position, or whose it is.
std::string structureLabel(const study::ReportStructure& s) {
    if (s.kind < 0) return TR("another structure, or none of these");
    const auto name = formationName(s.kind);
    if (s.kept) return trf("{}, already on the board", name);
    return s.owner == 0 ? trf("{}, White's", name) : trf("{}, Black's", name);
}

std::string squareName(uint8_t sq) {
    return fmt::format("{}{}", static_cast<char>('a' + sq % 8), static_cast<char>('1' + sq / 8));
}

// A plan's moves in words: "b4 then b5", "The push e4", "The knight from f8 to g6".
std::string planMoves(const study::ReportPlan& p) {
    const auto& q = p.squares;
    if (p.kind == study::ReportPlan::Kind::Route && q.size() >= 2) {
        const auto a = squareName(q[0]), b = squareName(q[1]);
        const bool three = q.size() >= 3;
        const auto c     = three ? squareName(q[2]) : std::string();
        switch (p.man) {
            case study::KNIGHT:
                return three ? trf("The knight from {} to {}, then {}", a, b, c)
                             : trf("The knight from {} to {}", a, b);
            case study::BISHOP:
                return three ? trf("The bishop from {} to {}, then {}", a, b, c)
                             : trf("The bishop from {} to {}", a, b);
            case study::ROOK:
                return three ? trf("The rook from {} to {}, then {}", a, b, c) : trf("The rook from {} to {}", a, b);
            default:
                return three ? trf("The queen from {} to {}, then {}", a, b, c) : trf("The queen from {} to {}", a, b);
        }
    }
    if (q.size() >= 2) return trf("{} then {}", squareName(q[0]), squareName(q[1]));
    if (q.size() == 1) return trf("The push {}", squareName(q[0]));
    return {};
}

// A plan as the assistant's facts name it, then its moves.
std::string planLabel(const study::ReportPlan& p) {
    using P          = commentary::PlanName;
    const char* name = nullptr;
    switch (static_cast<P>(p.named)) {
        case P::MinorityAttack:
            name = TR("The minority attack");
            break;
        case P::PawnStorm:
            name = TR("The pawn storm against the king");
            break;
        case P::ChainBase:
            name = TR("The attack on the base of the pawn chain");
            break;
        case P::ChainHead:
            name = TR("The attack on the head of the pawn chain");
            break;
        case P::Isolani:
            name = TR("The isolated queen's pawn's advance");
            break;
        case P::FreeingMaroczy:
            name = TR("The freeing break against the Maroczy bind");
            break;
        case P::FreeingHedgehog:
            name = TR("The hedgehog's break");
            break;
        case P::Space:
            name = TR("The gain of space in the centre");
            break;
        case P::None:
            break;
    }
    auto moves = planMoves(p);
    if (!name) return moves;
    if (!moves.empty()) moves[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(moves[0])));
    return trf("{}: {}", name, moves);
}

// What the checker refused in a passage of the summary, in words.
std::string refusalWords(const commentary::Refusal& r) {
    using K = commentary::Refusal::Kind;
    switch (r.kind) {
        case K::Figure:
            return trf("it wrote “{}”, a figure the report does not give of what it names", r.what);
        case K::Notation:
            return trf("it quoted “{}”, a move neither of the position nor of the report's lines", r.what);
        case K::Unsupported:
            return trf("it named “{}”, which the report does not have, or not for that side", r.what);
        case K::Advice:
            return trf("it gave advice (“{}”) where the report gives frequencies", r.what);
        case K::Evaluation:
            return trf("it said “{}”, which the evaluations contradict", r.what);
        case K::Name:
            return trf("it named “{}”, a name the report does not give", r.what);
        case K::Number:
            return trf("it said “{}”, a number the report does not give", r.what);
        case K::Elision:
            return trf("its French forgot an elision: “{}”", r.what);
        case K::Wording:
            return trf("its words say something else: “{}”", r.what);
        case K::Grammar:
            return TR("it was cut short, or wrote outside the words it may use");
        default:
            return TR("it wrote nothing");
    }
}

// White's share light, the draws grey, Black's dark, as the Tree tab draws them.
void wdlBar(double white, double draws, double black) {
    const auto pos = ImGui::GetCursorScreenPos();
    const float w  = std::max(px(60), ImGui::GetContentRegionAvail().x);
    const float h  = ImGui::GetTextLineHeight() * 0.8f;
    auto* dl       = ImGui::GetWindowDrawList();
    const float y  = pos.y + ImGui::GetTextLineHeight() * 0.1f;
    const float x1 = pos.x + w * static_cast<float>(white);
    const float x2 = x1 + w * static_cast<float>(draws);
    dl->AddRectFilled(ImVec2(pos.x, y), ImVec2(x1, y + h), IM_COL32(246, 247, 249, 255));
    dl->AddRectFilled(ImVec2(x1, y), ImVec2(x2, y + h), IM_COL32(154, 163, 176, 255));
    dl->AddRectFilled(ImVec2(x2, y), ImVec2(pos.x + w, y + h), IM_COL32(43, 48, 56, 255));
    dl->AddRect(ImVec2(pos.x, y), ImVec2(pos.x + w, y + h), IM_COL32(201, 206, 214, 255));
    ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", trf("White {} %, draws {} %, Black {} %", std::lround(100 * white),
                                    std::lround(100 * draws), std::lround(100 * black))
                                    .c_str());
}

// "1.d4 d5 2.c4": moves from a board, in the interface's letters.
std::string lineText(const chess::Board& from, const std::vector<chess::Move>& moves) {
    std::string out;
    auto board = from;
    for (std::size_t i = 0; i < moves.size(); ++i) {
        const bool white = board.sideToMove() == chess::Color::WHITE;
        auto san         = localSan(chess::uci::moveToSan(board, moves[i]));
        if (white)
            out += fmt::format("{}{}.{}", out.empty() ? "" : " ", board.fullMoveNumber(), san);
        else if (i == 0)
            out += fmt::format("{}…{}", board.fullMoveNumber(), san);
        else
            out += " " + san;
        board.makeMove(moves[i]);
    }
    return out;
}

// A move numbered from its board, in the interface's letters: "11.Rab1", "14…Ng6".
std::string numbered(const chess::Board& before, const chess::Move& move) {
    const auto san = localSan(chess::uci::moveToSan(before, move));
    return before.sideToMove() == chess::Color::WHITE ? fmt::format("{}.{}", before.fullMoveNumber(), san)
                                                      : fmt::format("{}…{}", before.fullMoveNumber(), san);
}

std::string evalText(const ReportEval& e) {
    if (e.mate) return fmt::format("#{}", *e.mate);
    return e.cp ? fmt::format("{:+.2f}", *e.cp / 100.0) : std::string();
}

std::string resultText(char r) { return r == '1' ? "1-0" : r == '0' ? "0-1" : r == '=' ? "½-½" : "*"; }

// The explorer's key of a question, as the Tree tab makes it: the same answer serves both.
std::string explorerKey(const lichess::ExplorerQuery& q, int source) {
    std::string key = fmt::format("{}|{}", source, q.fen);
    for (const auto& s : q.speeds) key += "|" + s;
    for (const auto r : q.ratings) key += fmt::format("|{}", r);
    key += "|" + q.player;
    if (q.history) key += "|months";
    return key;
}

std::string dateOf(int64_t seconds) {
    const auto t = static_cast<std::time_t>(seconds);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char text[16];
    std::strftime(text, sizeof(text), "%Y-%m-%d", &tm);
    return text;
}

// A job's future kept until it ends, its end looked at without waiting.
template <typename Leftovers, typename T>
void leave(Leftovers& into, std::future<T>& job) {
    if (!job.valid()) return;
    auto held       = std::make_shared<std::future<T>>(std::move(job));
    const auto* raw = held.get();
    into.push_back({held, [raw] { return raw->wait_for(std::chrono::seconds(0)) == std::future_status::ready; }});
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Opening it, and its jobs

void App::leaveReportJobs() {
    if (!opening_report_) return;
    auto& r = *opening_report_;
    for (auto* c : {r.cancel.get(), r.evals_cancel.get(), r.summary_cancel.get()})
        if (c) *c = true;
    leave(report_leftovers_, r.job);
    leave(report_leftovers_, r.evals_job);
    leave(report_leftovers_, r.summary_job);
}

void App::openOpeningReport(Page over) {
    auto& b = board_;
    if (b.chess960) {
        boardToast(TR("The opening report is for standard chess"));
        return;
    }
    leaveReportJobs();  // the one before: cancelled, its jobs kept until they end
    OpeningReportState r;
    r.over  = over;
    r.board = boardAt(b.game, b.chess960, b.ply);
    r.fen   = r.board.getFen();
    if (const auto path = boardPath()) r.path = *path;
    // Its opening: the last named position of its moves (Lichess's named openings).
    const auto& names = dossierOpenings();
    study::ScanBoard scan;
    chess::Board walk;
    std::optional<std::size_t> named;
    if (scan.set(walk)) {
        for (const auto& m : r.path) {
            walk.makeMove(m);
            if (!scan.set(walk)) break;
            if (const auto n = names.at(scan.key())) named = n;
        }
        if (r.path.empty() && scan.set(r.board))
            if (const auto n = names.at(scan.key())) named = n;
    }
    if (named) r.opening = fmt::format("{} {}", names[*named].eco, names[*named].name);
    // Lichess's sources: those the Tree tab shows (it asked them already), or its masters when no database can tell.
    const bool lichess = lichess_account_ && lichess_.explorer;
    r.masters          = lichess && tree_online_source_ == 0;
    r.players          = lichess && tree_online_source_ == 1;
    opening_report_    = std::move(r);
    page_              = over;
    startOpeningReport();
    auto& o = *opening_report_;
    if (o.bases.empty() && lichess) o.masters = true;
}

void App::startOpeningReport() {
    if (!opening_report_) return;
    auto& r = *opening_report_;
    leaveReportJobs();  // those running: cancelled, kept until they end, their results not taken
    // The Tree tab's databases, those of "My databases": whose tree covers their games; the others, a fix each.
    auto& s = shelf();
    r.bases.clear();
    r.stale.clear();
    for (std::size_t i = 0; i < s.entries().size(); ++i) {
        const auto& e = s.entries()[i];
        if (e.source || tree_left_.count(e.folder)) continue;
        const auto source = s.treeSource(i);
        if (source && source->tree)
            r.bases.push_back({source->name, e.folder, source->snapshot, source->tree});
        else if (s.open(i))
            r.stale.push_back(i);
    }
    r.report.reset();
    r.serial    = 0;
    r.cancelled = false;
    r.rows.clear();
    r.rows_of.clear();
    r.evals.clear();
    r.evals_of.clear();
    r.summary.reset();
    r.summary_of.clear();
    r.row      = -1;
    r.cancel   = std::make_shared<std::atomic<bool>>(false);
    r.progress = std::make_shared<study::ReportProgress>();
    r.shared   = std::make_shared<ReportShared>();
    if (liveGameOn()) {  // fair play (R17): nothing while a live game is played; made at its end
        r.locked = true;
        return;
    }
    r.locked = false;
    r.job    = std::async(std::launch::async,
                          [fen = r.fen, bases = r.bases, names = userNames(), shared = r.shared, progress = r.progress,
                           cancel = r.cancel] { makeReport(fen, bases, names, 0, *progress, *cancel, *shared); });
}

void App::askReportLichess(int source) {
    auto& r = *opening_report_;
    if (!lichess_account_ || !lichess_.explorer || aidsLocked()) return;
    // The answers that came, for the Tree tab and the report alike.
    if (explorer_job_.valid() && explorer_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        if (explorer_answers_.size() > 2000) explorer_answers_.clear();
        auto answer = explorer_job_.get();
        if (!answer.value) explorer_failed_[explorer_asked_] = ImGui::GetTime();
        explorer_answers_[explorer_asked_] = std::move(answer);
    }
    lichess::ExplorerQuery q;
    q.source = source == 0 ? lichess::ExplorerSource::Masters : lichess::ExplorerSource::Lichess;
    q.fen    = r.fen;
    q.token  = lichess_account_->token;
    if (source == 1) {
        q.speeds  = lichess_.explorer_speeds;
        q.ratings = lichess_.explorer_ratings;
        q.history = lichess_.explorer_months;
    }
    const auto key   = explorerKey(q, source);
    const auto found = explorer_answers_.find(key);
    if (found == explorer_answers_.end()) {
        if (!explorer_job_.valid()) {
            explorer_asked_     = key;
            explorer_job_since_ = ImGui::GetTime();
            explorer_job_       = std::async(std::launch::async, [q] { return lichess::explorer(q); });
        }
        return;
    }
    const auto i = static_cast<std::size_t>(source);
    if (found->second.value) {
        if (!r.lichess[i]) {
            r.lichess[i] = *found->second.value;
            if (!options_.scripted) keepExplorer(storage_.root(), key, *found->second.value);
        }
        r.lichess_when[i] = 0;
        r.lichess_error[i].clear();
    } else {
        // Offline, or Lichess refused: its last figures for this position, dated, when the report kept them.
        r.lichess_error[i] = found->second.error.empty() ? std::string("?") : found->second.error;
        if (!r.lichess[i])
            if (auto kept = keptExplorer(storage_.root(), key)) {
                r.lichess[i]      = std::move(kept->first);
                r.lichess_when[i] = kept->second;
            }
        // Asked again after a minute, as the Tree tab does: never in a loop.
        if (ImGui::GetTime() - explorer_failed_[key] > 60) {
            explorer_answers_.erase(found);
            explorer_failed_.erase(key);
        }
    }
}

void App::pollOpeningReport() {
    std::erase_if(report_leftovers_, [](const ReportLeftover& l) { return l.ready(); });
    if (!opening_report_) return;
    auto& r = *opening_report_;
    // Another page shown (the rail, a command): the report steps aside.
    if (r.shown && page_ != r.over) r.shown = false;
    if (!r.shown) r.drawn = false;
    // Fair play (R17): a live game on Lichess stops it; it comes back by itself at the end.
    if (liveGameOn()) {
        if (!r.locked) {
            for (auto* c : {r.cancel.get(), r.evals_cancel.get(), r.summary_cancel.get()})
                if (c) *c = true;
            r.locked = true;
        }
        return;
    }
    if (r.locked) {
        if (r.job.valid() && r.job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        startOpeningReport();
        return;
    }
    if (r.shared) {
        const std::lock_guard lock(r.shared->mutex);
        if (r.shared->serial != r.serial) {
            r.serial = r.shared->serial;
            if (r.shared->report && r.shared->report->cancelled) {
                r.cancelled = true;
                if (!r.report) r.report = r.shared->report;
                r.report->cancelled = true;
            } else {
                r.report = r.shared->report;
            }
        }
    }
    if (r.job.valid() && r.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) r.job.get();
    // A database's tree built meanwhile (its fix): the report made again with it.
    if (!r.job.valid() && !r.stale.empty()) {
        auto& s = shelf();
        for (const auto i : r.stale)
            if (i < s.entries().size())
                if (const auto source = s.treeSource(i); source && source->tree) {
                    r.said.clear();
                    startOpeningReport();
                    return;
                }
    }
    for (int source = 0; source < 2; ++source)
        if ((source == 0 ? r.masters : r.players)) askReportLichess(source);
    // The rows shown: the databases' moves, Lichess's sources ticked added.
    if (r.report) {
        const auto of = fmt::format("{}|{}{}|{}{}", r.serial, r.masters, r.lichess[0] ? r.lichess[0]->moves.size() : 0,
                                    r.players, r.lichess[1] ? r.lichess[1]->moves.size() : 0);
        if (of != r.rows_of) {
            r.rows_of = of;
            std::vector<std::string> names;
            for (const auto& base : r.bases) names.push_back(base.name);
            std::vector<std::pair<std::string, std::vector<ReportLichessMove>>> lichess;
            for (int source = 0; source < 2; ++source) {
                const auto& answer = r.lichess[static_cast<std::size_t>(source)];
                if (!answer || !(source == 0 ? r.masters : r.players)) continue;
                std::vector<ReportLichessMove> moves;
                for (const auto& m : answer->moves)
                    moves.push_back({m.uci, m.white, m.draws, m.black, m.average_rating});
                lichess.emplace_back(source == 0 ? std::string(TR("Masters (Lichess)")) : std::string("Lichess"),
                                     std::move(moves));
            }
            r.rows = reportRows(r.board, r.report->moves, names, lichess);
        }
    }
    // The evaluations of the moves shown, once they are known: Lichess's offline, then the analysis' engine. Not on a
    // correspondence game's position (R17).
    const bool correspondence = aidsLocked();
    if (r.report && !correspondence && !r.rows.empty()) {
        std::string of;
        for (const auto& row : r.rows) of += chess::uci::moveToUci(row.move) + " ";
        if (r.evals_job.valid() && r.evals_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            r.evals = r.evals_job.get();
            r.evals_offline =
                std::any_of(r.evals.begin(), r.evals.end(), [](const auto& e) { return e && !e->engine; });
        }
        if (of != r.evals_of && !r.evals_job.valid()) {
            r.evals_of = of;
            ReportEvalAsk ask;
            for (std::size_t i = 0; i < r.rows.size() && i < 8; ++i) {
                auto after = r.board;
                after.makeMove(r.rows[i].move);
                ask.fens.push_back(after.getFen());
            }
            if (offline_evals_) ask.lookup = offline_evals_->lookup();
            if (const auto* engine = analysisEngine()) {
                ask.engine     = *engine;
                r.evals_engine = engine->name;
            }
            r.evals_total  = static_cast<int>(ask.fens.size());
            r.evals_cancel = std::make_shared<std::atomic<bool>>(false);
            r.evals_done   = std::make_shared<std::atomic<int>>(0);
            r.evals.assign(ask.fens.size(), std::nullopt);
            r.evals_error.clear();
            r.evals_job =
                std::async(std::launch::async, [ask = std::move(ask), done = r.evals_done, cancel = r.evals_cancel] {
                    return evaluateReport(ask, *done, *cancel);
                });
        }
    }
    // The summary, once the report, its evaluations and Lichess's sources ticked are in.
    if (r.summary_job.valid() && r.summary_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto written = r.summary_job.get();
        if (!written.cancelled) r.summary = std::move(written);
    }
    const bool lichess_in = (!r.masters || r.lichess[0] || !r.lichess_error[0].empty()) &&
                            (!r.players || r.lichess[1] || !r.lichess_error[1].empty());
    if (r.report && r.report->made == study::ReportSection::Done && !r.cancelled && !correspondence &&
        !r.evals_job.valid() && lichess_in && !r.summary_job.valid()) {
        const auto of = fmt::format("{}|{}|{}|{}", r.rows_of, r.evals_of, r.round, static_cast<int>(lang()));
        if (of != r.summary_of) {
            r.summary_of = of;
            ReportView view;
            view.board               = r.board;
            view.path                = r.path;
            view.opening             = r.opening;
            view.report              = &*r.report;
            view.rows                = &r.rows;
            view.evals               = &r.evals;
            const auto figures       = summaryFigures(view, lang());
            commentary::Model* model = storyWithoutModel().empty() ? assistant_.get() : nullptr;
            r.summary_total          = static_cast<int>(commentary::summaryTopics(figures).size());
            r.summary_cancel         = std::make_shared<std::atomic<bool>>(false);
            r.summary_done           = std::make_shared<std::atomic<int>>(0);
            r.summary_job = std::async(std::launch::async, [model, figures, l = lang(), round = r.round,
                                                            cancel = r.summary_cancel, done = r.summary_done] {
                return writeSummary(model, figures, l, round, *cancel, *done);
            });
        }
    }
}

void App::reportToRepertoire() {
    auto& r = *opening_report_;
    if (r.path.empty() && !r.board.getFen().starts_with("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w")) {
        r.said = TR("The repertoire needs the moves from the start that lead to the position.");
        return;
    }
    if (!r.report || r.report->lines.empty()) {
        r.said = TR("No main line to add yet.");
        return;
    }
    auto& reps       = repertoires();
    const auto side  = r.board.sideToMove();
    const auto index = reps.active() >= 0 ? static_cast<std::size_t>(reps.active()) : reps.activeOrNew(side);
    auto before      = reps.entries()[index].lines;
    // The main moves: the first main line, the moves of the repertoire's side and the replies the games play most.
    const auto moves = movesOfCodes(r.board, r.report->lines.front().codes);
    const auto added = reps.addLine(index, r.path, moves);
    if (added == 0) {
        r.said = trf("“{}” has these moves already.", reps.entries()[index].name);
        return;
    }
    r.undo = std::pair{index, std::move(before)};
    r.said = trn("{} move added to “{}” · Ctrl+Z takes it out", "{} moves added to “{}” · Ctrl+Z takes them out",
                 static_cast<int>(added), added, reps.entries()[index].name);
}

void App::openReportGame(const study::ReportGame& game) {
    auto& r = *opening_report_;
    if (game.source >= r.bases.size()) return;
    const auto& base = r.bases[game.source];
    const auto names = base.snapshot->rowIds(game.id);
    const auto title =
        fmt::format("{} – {}", base.snapshot->shownName(names.white), base.snapshot->shownName(names.black));
    readOnBoard(study::importedPgn(base.snapshot->game(game.id)), title);
    board_.ply = std::min(static_cast<int>(game.ply), static_cast<int>(board_.game.moves.size()));
    page_      = Page::Board;
    BoardBack back;
    back.page   = Page::Board;
    back.report = true;
    board_back_ = back;
    r.shown     = false;
}

void App::reportGamesInList(const std::vector<study::ReportGame>& games, std::string said) {
    auto& r = *opening_report_;
    if (games.empty()) return;
    // The database with the most of them.
    std::vector<std::size_t> count(r.bases.size(), 0);
    for (const auto& g : games)
        if (g.source < count.size()) ++count[g.source];
    const auto best = static_cast<uint32_t>(std::max_element(count.begin(), count.end()) - count.begin());
    const auto& s   = shelf();
    for (std::size_t i = 0; i < s.entries().size(); ++i) {
        if (s.entries()[i].folder != r.bases[best].folder) continue;
        std::vector<study::GameId> ids;
        for (const auto& g : games)
            if (g.source == best) ids.push_back(g.id);
        std::sort(ids.begin(), ids.end());
        page_           = Page::Databases;
        db_selected_    = static_cast<int>(i);
        db_list_        = true;
        db_only_        = std::move(ids);
        db_only_said_   = std::move(said);
        db_row_         = 0;
        db_search_open_ = false;
        r.shown         = false;
        return;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The page

void App::drawOpeningReport() {
    auto& r = *opening_report_;
    const ui::TopicRegion topic(guide::topics::OpeningReport);
    // Shown anew (from the Tree tab, Ctrl+K, back from a game): ImGui's keyboard cursor of what was shown before is
    // hidden, so that Enter plays the move chosen rather than press a button.
    if (!std::exchange(r.drawn, true)) ImGui::SetNavCursorVisible(false);
    const bool typing = ImGui::GetIO().WantTextInput;
    const bool popup  = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup);

    // Where it is, and the way back to the position.
    const bool escape = !typing && !popup && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (ui::iconButton("report_back", ui::Icon::Back, TR("Board")) || escape) {
        r.shown = false;
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("›");
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("Opening report"));
    ImGui::SameLine(0, px(16));
    ImGui::TextDisabled("%s", TR("Esc"));
    ImGui::SameLine();
    ui::guideLink("report_help", guide::topics::OpeningReport);

    // Fair play (R17): nothing while one of the user's games is played live on Lichess.
    if (r.locked) {
        ImGui::Dummy(ImVec2(0, px(8)));
        ui::beginCard("report_locked");
        ui::inlineIcon(ui::Icon::Lock, ui::ACCENT);
        ImGui::SameLine();
        ImGui::PushTextWrapPos();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s",
                           TR("Your live game on Lichess: the report is locked"));
        ImGui::TextDisabled("%s", TR("It comes back by itself at the end of the game."));
        ImGui::PopTextWrapPos();
        ui::endCard();
        return;
    }

    // The position, its opening, its moves and what it is made of.
    ImGui::Dummy(ImVec2(0, px(4)));
    const float size = std::floor(std::min(px(150), ImGui::GetContentRegionAvail().x * 0.25f));
    BoardDecor decor;
    decor.eval_white  = -1;
    decor.coordinates = false;
    if (!r.path.empty()) decor.last_move = r.path.back();
    if (r.row >= 0 && r.row < static_cast<int>(r.rows.size()))
        decor.arrows.push_back({r.rows[static_cast<std::size_t>(r.row)].move, ui::ACCENT});
    drawBoard(atlas_, r.board, size, decor);
    ImGui::SameLine(0, px(16));
    ImGui::BeginGroup();
    {
        ui::inlineIcon(ui::Icon::File, ImGui::GetColorU32(ImGuiCol_Text));
        ImGui::SameLine();
        ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize() * 1.3f);
        ImGui::TextUnformatted(r.opening.empty() ? TR("Opening report") : r.opening.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        const bool white = r.board.sideToMove() == chess::Color::WHITE;
        std::string where;
        if (r.path.empty())
            where = white ? TR("the position shown, White to move") : TR("the position shown, Black to move");
        else {
            chess::Board before;
            for (std::size_t i = 0; i + 1 < r.path.size(); ++i) before.makeMove(r.path[i]);
            const auto last = numbered(before, r.path.back());
            where           = white ? trf("after {}, White to move", last) : trf("after {}, Black to move", last);
        }
        ImGui::TextDisabled("· %s", where.c_str());
        // To the repertoire, at the right.
        const char* to = TR("To the repertoire");
        const float w  = ImGui::CalcTextSize(to).x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(12), ImGui::GetContentRegionMax().x - w));
        ImGui::BeginDisabled(!r.report || r.report->lines.empty());
        if (ImGui::Button(to)) reportToRepertoire();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", TR("The main moves added to the active repertoire (Ctrl+Z takes them out)"));
    }
    if (!r.path.empty()) {
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", lineText(chess::Board(), r.path).c_str());
        ImGui::PopTextWrapPos();
    }
    // The sources, those of the Tree tab.
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TR("Sources, the Tree tab's:"));
    ImGui::SameLine();
    {
        uint64_t mine = 0;
        if (r.report)
            for (const auto n : r.report->moves.per_source) mine += n;
        const auto text = r.bases.empty() ? std::string(TR("Your databases · no tree"))
                          : r.report      ? trf("Your databases · {}", ui::exactCount(mine))
                                          : std::string(TR("Your databases"));
        chip("##report_bases", text, !r.bases.empty());
        if (ImGui::IsItemHovered()) {
            std::vector<std::string> each;
            for (std::size_t i = 0; i < r.bases.size(); ++i)
                each.push_back(r.report && i < r.report->moves.per_source.size()
                                   ? trf("{}: {}", r.bases[i].name, ui::exactCount(r.report->moves.per_source[i]))
                                   : r.bases[i].name);
            ImGui::SetTooltip("%s", (each.empty() ? std::string(TR("No database whose opening tree is built."))
                                                  : listed(each) + "\n" + TR("Nothing leaves the machine."))
                                        .c_str());
        }
    }
    const bool linked = lichess_account_ && lichess_.explorer;
    for (int source = 0; source < 2; ++source) {
        ImGui::SameLine(0, px(6));
        const auto i      = static_cast<std::size_t>(source);
        const bool on     = source == 0 ? r.masters : r.players;
        std::string label = source == 0
                                ? std::string(TR("Masters (Lichess)"))
                                : (lichess_.explorer_ratings.empty()
                                       ? std::string("Lichess")
                                       : trf("Lichess {}+", *std::min_element(lichess_.explorer_ratings.begin(),
                                                                              lichess_.explorer_ratings.end())));
        if (on && r.lichess[i]) {
            const auto& e = *r.lichess[i];
            label += " · " + ui::exactCount(e.white + e.draws + e.black);
            if (r.lichess_when[i]) label += " · " + trf("on {}", dateOf(r.lichess_when[i]));
        } else if (on && r.lichess_error[i].empty() && linked && !aidsLocked()) {
            label += " · …";
        }
        const std::string id = fmt::format("##report_lichess{}", source);
        if (chip(id.c_str(), label, on) && linked) {
            (source == 0 ? r.masters : r.players) = !on;
            r.rows_of.clear();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", !linked ? TR("Lichess's explorer needs a linked account (the Lichess page).")
                                    : on    ? TR("Asked of Lichess, as the Tree tab asks it: the position, with its "
                                                 "filters. A click takes the source out.")
                                            : TR("A click asks Lichess's explorer for this position, as the Tree tab "
                                                 "does: the position and its filters only."));
    }
    // Where the evaluations come from.
    {
        std::vector<std::string> from;
        if (offline_evals_ && offline_evals_->lookup()) from.emplace_back(TR("Lichess, kept offline"));
        if (const auto* engine = analysisEngine()) from.push_back(engine->name);
        std::string said;
        for (const auto& f : from) said = said.empty() ? f : trf("{}, then {}", said, f);
        if (!said.empty()) {
            ImGui::SameLine(0, px(8));
            ImGui::TextDisabled("%s", trf("· evaluations: {}", said).c_str());
        }
    }
    ImGui::EndGroup();
    if (!r.said.empty()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", r.said.c_str());
    // Ctrl+Z: the moves added to the repertoire taken out.
    if (!typing && !popup && r.undo && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
        repertoires().restore(r.undo->first, std::move(r.undo->second));
        r.undo.reset();
        r.said = TR("The repertoire is as it was.");
    }

    // Being made: its progress, section by section, and Cancel (R18).
    const bool making = r.job.valid() || r.evals_job.valid() || r.summary_job.valid();
    if (making && !r.cancelled) {
        const char* section = TR("the moves and the main lines");
        float fraction      = 0;
        if (r.job.valid()) {
            switch (static_cast<study::ReportSection>(r.progress->section.load())) {
                case study::ReportSection::Moves:
                    section = TR("the moves and the main lines");
                    break;
                case study::ReportSection::Shapes:
                case study::ReportSection::KeyGames:
                    section = TR("the structures, the plans and the key games");
                    break;
                default:
                    section = TR("the novelties");
                    break;
            }
            const auto total = r.progress->total.load(), done = r.progress->done.load();
            fraction = total ? static_cast<float>(done) / static_cast<float>(total) : 0.0f;
        } else if (r.evals_job.valid()) {
            section = TR("the evaluations");
            fraction =
                r.evals_total ? static_cast<float>(r.evals_done->load()) / static_cast<float>(r.evals_total) : 0.0f;
        } else {
            section  = TR("the summary");
            fraction = r.summary_total
                           ? static_cast<float>(r.summary_done->load()) / static_cast<float>(r.summary_total)
                           : 0.0f;
        }
        ImGui::ProgressBar(
            fraction, ImVec2(px(360), 0),
            trf("Making the report: {}… {} %", section, static_cast<int>(std::lround(100 * fraction))).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Cancel"))) {
            for (auto* c : {r.cancel.get(), r.evals_cancel.get(), r.summary_cancel.get()})
                if (c) *c = true;
            r.cancelled = true;
        }
    } else if (r.cancelled || (r.report && r.report->cancelled)) {
        ImGui::TextDisabled("%s", TR("Cancelled."));
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Make it again"))) startOpeningReport();
    }

    drawReportStates();

    // Its sections (Ctrl+Page Down, Ctrl+Page Up).
    const char* tabs[]  = {TR("Lines"), TR("Plans and structures"), TR("Key games"), TR("Novelties"), TR("Summary")};
    constexpr int count = 5;
    if (!typing && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_PageDown)) r.tab = (r.tab + 1) % count;
    if (!typing && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_PageUp)) r.tab = (r.tab + count - 1) % count;
    r.tab = std::clamp(r.tab, 0, count - 1);
    if (ImGui::BeginTabBar("##report_tabs")) {
        const int asked = r.tab_shown != r.tab ? r.tab : -1;
        for (int i = 0; i < count; ++i) {
            if (ImGui::BeginTabItem(tabs[i], nullptr, asked == i ? ImGuiTabItemFlags_SetSelected : 0)) {
                if (asked < 0) r.tab = i;
                ImGui::EndTabItem();
            }
        }
        r.tab_shown = r.tab;
        ImGui::EndTabBar();
    }
    ImGui::BeginChild("##report_body", ImVec2(0, 0), ImGuiChildFlags_None);
    switch (r.tab) {
        case 0:
            drawReportLines();
            break;
        case 1:
            drawReportShapes();
            break;
        case 2:
            drawReportKeyGames();
            break;
        case 3:
            drawReportNovelties();
            break;
        default:
            drawReportSummary();
            break;
    }
    ImGui::EndChild();
}

void App::drawReportStates() {
    auto& r          = *opening_report_;
    const auto state = [&](const char* id, ui::Icon icon, ImU32 tone, const std::string& title,
                           const std::string& text) {
        ImGui::Dummy(ImVec2(0, px(2)));
        ui::beginCard(id);
        ui::inlineIcon(icon, tone);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(tone), "%s", title.c_str());
        ImGui::TextDisabled("%s", text.c_str());
        ImGui::PopTextWrapPos();
    };
    const auto endState = [] {
        ImGui::EndGroup();
        ui::endCard();
    };
    const auto text = ImGui::GetColorU32(ImGuiCol_Text);
    // No database with an up-to-date tree: the report from Lichess's masters alone, a fix for each.
    if (r.bases.empty()) {
        const bool linked = lichess_account_ && lichess_.explorer;
        state("report_no_tree", ui::Icon::Database, ui::WARN, TR("No database has an up-to-date opening tree"),
              linked ? TR("The report is made of Lichess's masters alone (your linked account).")
                     : TR("The report is made of your databases' opening trees, or of Lichess's masters with a linked "
                          "account."));
        auto& s = shelf();
        for (const auto i : r.stale) {
            if (i >= s.entries().size()) continue;
            if (ImGui::SmallButton(trf("Rebuild the tree of “{}”##rebuild{}", s.entries()[i].name, i).c_str())) {
                s.rebuildTree(i);
                r.said =
                    trf("The tree of “{}” is being built: the report is made again once it is.", s.entries()[i].name);
            }
            ImGui::SameLine();
        }
        if (ImGui::SmallButton(TR("Import a database"))) {
            page_   = Page::Databases;
            r.shown = false;
        }
        endState();
    }
    // A rare position: moves and games, no plans nor structures.
    if (r.report && r.report->made >= study::ReportSection::Novelties && !r.report->shapes.enough &&
        r.report->moves.total.games > 0) {
        const auto n = r.report->moves.total.games;
        state("report_rare", ui::Icon::Info, text,
              trn("Rare position: {} game in your databases", "Rare position: {} games in your databases",
                  static_cast<int>(std::min<uint64_t>(n, 1000000000)), ui::exactCount(n)),
              TR("The moves and the games are given; no plans nor structures under 50 games."));
        if (!r.players && lichess_account_ && lichess_.explorer && !aidsLocked()) {
            if (ImGui::SmallButton(TR("Add Lichess's players as a source"))) {
                r.players = true;
                r.rows_of.clear();
            }
        }
        endState();
    }
    // Offline: the databases and the evaluations kept answer; Lichess's sources their last figures, dated.
    if ((r.masters && !r.lichess_error[0].empty()) || (r.players && !r.lichess_error[1].empty())) {
        state("report_offline", ui::Icon::WifiOff, text, TR("Offline"),
              TR("Your databases and the evaluations kept offline answer; Lichess's sources show their last figures "
                 "for this position, dated, when the report kept them."));
        endState();
    }
    // A correspondence game on this position: the lines and figures stay, the evaluations and the summary do not.
    if (aidsLocked()) {
        state(
            "report_correspondence", ui::Icon::Lock, ui::ACCENT,
            TR("Your correspondence game passes through this position"),
            TR("The lines and figures of your databases stay (Lichess allows a book and a database); the evaluations, "
               "Lichess's explorer and the summary wait for the end of the game."));
        endState();
    } else if (!(offline_evals_ && offline_evals_->lookup()) && !analysisEngine()) {
        // Neither an engine nor Lichess's evaluations kept offline: the column stays empty.
        state("report_no_engine", ui::Icon::Engine, text, TR("Neither an engine nor offline evaluations"),
              TR("The Eval. column stays empty, and the summary says nothing of the engine."));
        if (!r.engine_card && ImGui::SmallButton(TR("Download Gaia…"))) r.engine_card = true;
        endState();
        if (r.engine_card)
            drawFirstEngineCard(
                TR("The opening report evaluates its moves with an engine when Lichess's evaluations "
                   "are not kept offline."));
    }
}

void App::drawReportLines() {
    auto& r = *opening_report_;
    if (!r.report) {
        ImGui::TextDisabled("%s", TR("Looking…"));
        return;
    }
    if (r.rows.empty()) {
        ImGui::TextDisabled("%s", r.bases.empty() && !r.masters
                                      ? TR("No source answers for this position yet.")
                                      : TR("No game of these sources goes on from this position."));
        return;
    }
    // The keys, as in the Tree tab: ↑ ↓ choose a move, Enter plays it.
    std::optional<chess::Move> play;
    const auto& io = ImGui::GetIO();
    const bool keys =
        !io.WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && !io.KeyCtrl && !io.KeyAlt;
    if (keys) {
        const int last = static_cast<int>(r.rows.size()) - 1;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) r.row = std::min(r.row + 1, last), r.scroll = true;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) r.row = std::max(r.row - 1, -1), r.scroll = true;
        if (r.row >= 0 &&
            (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
            play = r.rows[static_cast<std::size_t>(r.row)].move;
    }
    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerH;
    if (ImGui::BeginTable("##report_moves", 8, flags)) {
        ImGui::TableSetupColumn(TR("Move"));
        ImGui::TableSetupColumn(TR("Games"));
        ImGui::TableSetupColumn(TR("White · draws · Black"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(TR("Score"));
        ImGui::TableSetupColumn(TR("Avg. Elo"));
        ImGui::TableSetupColumn(TR("Perf."));
        ImGui::TableSetupColumn(TR("Last"));
        ImGui::TableSetupColumn(TR("Eval."));
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < r.rows.size(); ++i) {
            const auto& row = r.rows[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(numbered(r.board, row.move).c_str(), r.row == static_cast<int>(i),
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                play = row.move;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("A click plays it on the Board page"));
            if (r.row == static_cast<int>(i) && r.scroll) {
                ImGui::SetScrollHereY();
                r.scroll = false;
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(ui::exactCount(row.games).c_str());
            if (row.by_source.size() > 1 && ImGui::IsItemHovered()) {
                std::string each;
                for (const auto& [name, n] : row.by_source)
                    each += (each.empty() ? "" : "\n") + fmt::format("{}: {}", name, ui::exactCount(n));
                ImGui::SetTooltip("%s", each.c_str());
            }
            ImGui::TableNextColumn();
            wdlBar(row.white, row.draws, row.black);
            ImGui::TableNextColumn();
            if (row.score >= 0) ImGui::Text("%d %%", row.score);
            ImGui::TableNextColumn();
            if (row.elo > 0) ImGui::Text("%d", row.elo);
            ImGui::TableNextColumn();
            if (row.perf > 0) ImGui::Text("%d", row.perf);
            ImGui::TableNextColumn();
            if (row.last > 0) ImGui::TextDisabled("%d", row.last);
            ImGui::TableNextColumn();
            if (i < r.evals.size() && r.evals[i]) {
                const auto& e = *r.evals[i];
                ImGui::TextUnformatted(evalText(e).c_str());
                if (ImGui::IsItemHovered()) {
                    const auto san = numbered(r.board, row.move);
                    ImGui::SetTooltip(
                        "%s", e.engine
                                  ? trf("{}'s evaluation after {}, depth {}, a million nodes on one thread. Scores "
                                        "are White's.",
                                        r.evals_engine, san, e.depth)
                                        .c_str()
                                  : trf("Lichess's evaluation after {}, depth {}, kept offline. Scores are White's.",
                                        san, e.depth)
                                        .c_str());
                }
            } else if (r.evals_job.valid() && i < static_cast<std::size_t>(r.evals_total)) {
                ImGui::TextDisabled("…");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    // The main lines: the continuations played most, as far as they stay common.
    if (!r.report->lines.empty()) {
        ImGui::Dummy(ImVec2(0, px(6)));
        ImGui::TextUnformatted(TR("The main lines"));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("· the continuations played most, as far as they stay common"));
        for (std::size_t l = 0; l < r.report->lines.size(); ++l) {
            const auto& line = r.report->lines[l];
            const auto moves = movesOfCodes(r.board, line.codes);
            const auto text  = lineText(r.board, moves);
            const int score  = whiteScore(line.stats);
            const auto stat =
                score >= 0
                    ? trf("{} · White {} %",
                          trn("{} game", "{} games", static_cast<int>(std::min<uint64_t>(line.stats.games, 1000000000)),
                              ui::exactCount(line.stats.games)),
                          score)
                    : trn("{} game", "{} games", static_cast<int>(std::min<uint64_t>(line.stats.games, 1000000000)),
                          ui::exactCount(line.stats.games));
            ImGui::PushID(static_cast<int>(100 + l));
            const float stat_w = ImGui::CalcTextSize(stat.c_str()).x;
            const float room   = ImGui::GetContentRegionAvail().x - stat_w - px(24);
            const auto shown   = fitted(text, room);
            if (ImGui::Selectable("##line", false, 0, ImVec2(0, ImGui::GetTextLineHeight()))) {
                // The line on the Board page, from the start when known.
                auto all = r.path;
                all.insert(all.end(), moves.begin(), moves.end());
                if (!r.path.empty() || r.board.getFen() == std::string(chess::constants::STARTPOS)) {
                    readOnBoard(lineText(chess::Board(), all), TR("Opening report"));
                    board_.ply = static_cast<int>(board_.game.moves.size());
                } else {
                    readOnBoard(r.fen, TR("Opening report"));
                }
                page_   = Page::Board;
                r.shown = false;
            }
            if (ImGui::IsItemHovered()) {
                const auto tip = (shown != text ? text + "\n" : std::string()) +
                                 std::string(TR("A click shows the line on the Board page"));
                ImGui::SetTooltip("%s", tip.c_str());
            }
            ImGui::SameLine(px(8));
            ImGui::TextUnformatted(shown.c_str());
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(8), ImGui::GetContentRegionMax().x - stat_w));
            ImGui::TextDisabled("%s", stat.c_str());
            ImGui::PopID();
        }
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", TR("A click plays the move on the Board page; ↑ ↓ and Enter as in the Tree tab; the "
                                 "evaluation's source and depth at the cursor."));
    ImGui::PopTextWrapPos();
    if (play) {
        // On the Board page, from the report's position.
        if (boardFen() != r.fen) {
            if (!r.path.empty())
                readOnBoard(lineText(chess::Board(), r.path), TR("Opening report"));
            else
                readOnBoard(r.fen, TR("Opening report"));
            board_.ply = static_cast<int>(board_.game.moves.size());
        }
        page_   = Page::Board;
        r.shown = false;
        r.row   = -1;
        boardPlay(*play);
    }
}

void App::drawReportShapes() {
    auto& r = *opening_report_;
    if (!r.report || r.report->made < study::ReportSection::Novelties) {
        ImGui::TextDisabled("%s", TR("Counting the structures and the plans in the position's games…"));
        return;
    }
    const auto& s = r.report->shapes;
    if (!s.enough) {
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", TR("No plans nor structures under 50 games: a frequency needs them."));
        ImGui::PopTextWrapPos();
        return;
    }
    // The structures at move 20: the three met most, then the rest.
    std::vector<std::pair<std::string, int>> cards;
    uint64_t rest = 0;
    for (const auto& st : s.structures) {
        if (st.kind >= 0 && cards.size() < 3 && percent(st.stats.games, s.counted) >= 1)
            cards.emplace_back(structureLabel(st), percent(st.stats.games, s.counted));
        else
            rest += st.stats.games;
    }
    if (rest > 0) cards.emplace_back(TR("another structure, or none of these"), percent(rest, s.counted));
    if (!cards.empty() &&
        ImGui::BeginTable("##report_shapes", static_cast<int>(cards.size()), ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        for (std::size_t i = 0; i < cards.size(); ++i) {
            ImGui::TableSetColumnIndex(static_cast<int>(i));
            ui::beginCard(fmt::format("report_shape{}", i).c_str());
            ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize() * 1.4f);
            ImGui::Text("%d %%", cards[i].second);
            ImGui::PopFont();
            cellText(cards[i].first, true);
            ui::endCard();
        }
        ImGui::EndTable();
    }
    const int ply = (r.board.fullMoveNumber() - 1) * 2 + (r.board.sideToMove() == chess::Color::WHITE ? 0 : 1);
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled(
        "%s", (ply + static_cast<int>(s.ply) == 40
                   ? trn("The structure at move 20, in the {} game that got there (the assistant's facts).",
                         "The structure at move 20, in the {} games that got there (the assistant's facts).",
                         static_cast<int>(s.counted), ui::exactCount(s.counted))
                   : trn("The structure ten moves later, in the {} game that got there (the assistant's facts).",
                         "The structure ten moves later, in the {} games that got there (the assistant's facts).",
                         static_cast<int>(s.counted), ui::exactCount(s.counted)))
                  .c_str());
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, px(4)));
    // What the players do in the next fifteen moves.
    if (s.plans.empty()) {
        ImGui::TextDisabled("%s", TR("No pawn push nor piece route is played in a tenth of the games."));
    } else if (ImGui::BeginTable("##report_plans", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn(TR("What the players do in the next 15 moves"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(TR("In"), ImGuiTableColumnFlags_WidthFixed, px(70));
        ImGui::TableSetupColumn(TR("White's score"), ImGuiTableColumnFlags_WidthFixed, px(110));
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < s.plans.size(); ++i) {
            const auto& p = s.plans[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableSetColumnIndex(0);
            const auto label = planLabel(p);
            const auto who   = p.side == 0 ? std::string(TR("(White)")) : std::string(TR("(Black)"));
            const auto whole = label + " " + who;
            const auto shown = fitted(whole, ImGui::GetContentRegionAvail().x);
            const bool clicked =
                ImGui::Selectable(fmt::format("{}##plan", shown).c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
            if (ImGui::IsItemHovered()) {
                const auto games =
                    trn("A click shows its {} game in the list", "A click shows its {} games in the list",
                        static_cast<int>(p.games.size()), ui::exactCount(p.games.size()));
                ImGui::SetTooltip("%s", (shown != whole ? whole + "\n" + games : games).c_str());
            }
            ImGui::TableNextColumn();
            ImGui::Text("%d %%", percent(p.stats.games, s.replayed));
            ImGui::TableNextColumn();
            if (const int score = whiteScore(p.stats); score >= 0) ImGui::Text("%d %%", score);
            ImGui::PopID();
            if (clicked) {
                reportGamesInList(p.games, trf("the games of the report where {}", label));
                return;
            }
        }
        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos();
    std::string note =
        TR("Frequencies, not advice: the pawn pushes and the pieces' routes (as the Manoeuvre search "
           "follows a piece) counted in the position's games. A click opens those games in the list; a "
           "frequency needs 50 games.");
    const auto total = r.report->moves.total.games;
    if (s.replayed < total)
        note += " " +
                trf("Counted in {} of the {} games, evenly spread.", ui::exactCount(s.replayed), ui::exactCount(total));
    ImGui::TextDisabled("%s", note.c_str());
    ImGui::PopTextWrapPos();
}

void App::drawReportKeyGames() {
    auto& r = *opening_report_;
    if (!r.report || r.report->made < study::ReportSection::Novelties) {
        ImGui::TextDisabled("%s", TR("Looking…"));
        return;
    }
    if (r.report->key_games.empty()) {
        ImGui::TextDisabled("%s", TR("No game of the position gives its players' ratings."));
        return;
    }
    const auto& novelties = r.report->novelties.moves;
    for (std::size_t i = 0; i < r.report->key_games.size(); ++i) {
        const auto& k = r.report->key_games[i];
        if (k.game.source >= r.bases.size()) continue;
        const auto& snap = *r.bases[k.game.source].snapshot;
        const auto row   = snap.row(k.game.id);
        const auto ids   = snap.rowIds(k.game.id);
        ImGui::PushID(static_cast<int>(i));
        ui::beginCard("report_key");
        const char* open = TR("Open");
        const float w    = ImGui::CalcTextSize(open).x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - w - px(12));
        const auto players = fmt::format("{} – {}", snap.shownName(ids.white), snap.shownName(ids.black));
        ImGui::PushFont(ui::titleFont(), 0.0f);
        ImGui::TextUnformatted(players.c_str());
        ImGui::PopFont();
        std::vector<std::string> where;
        if (!row.event.empty() && row.event != "?") where.emplace_back(row.event);
        if (row.date >> 9) where.push_back(std::to_string(row.date >> 9));
        where.push_back(resultText(row.result));
        where.push_back(trn("{} move", "{} moves", static_cast<int>((row.plies + 1) / 2), (row.plies + 1) / 2));
        std::string w_text;
        for (const auto& part : where) w_text += (w_text.empty() ? "" : " · ") + part;
        ImGui::SameLine();
        ImGui::TextDisabled("· %s", w_text.c_str());
        const int mean = k.numbers.white_elo && k.numbers.black_elo
                             ? (k.numbers.white_elo + k.numbers.black_elo) / 2
                             : std::max(k.numbers.white_elo, k.numbers.black_elo);
        std::string why;
        switch (k.why) {
            case study::ReportKeyGame::Why::Strongest:
                why = trf("The strongest: {} on average.", mean);
                break;
            case study::ReportKeyGame::Why::Recent:
                why = trf("The most recent of 2500 and more on average ({}).", mean);
                break;
            case study::ReportKeyGame::Why::Novelty:
                if (k.novelty < novelties.size()) {
                    const auto& n    = novelties[k.novelty];
                    const auto moves = movesOfCodes(r.board, n.codes);
                    auto before      = r.board;
                    for (std::size_t m = 0; m + 1 < moves.size(); ++m) before.makeMove(moves[m]);
                    const auto label  = moves.empty() ? std::string() : numbered(before, moves.back());
                    const auto repeat = n.stats.games > 0 ? n.stats.games - 1 : 0;
                    why = trn("The first game of {}: the novelty repeated {} time since.",
                              "The first game of {}: the novelty repeated {} times since.",
                              static_cast<int>(std::min<uint64_t>(repeat, 1000000000)), label, ui::exactCount(repeat));
                }
                break;
        }
        // The plan it plays, when it is one of the report's.
        for (const auto& p : r.report->shapes.plans)
            if (std::find(p.games.begin(), p.games.end(), k.game) != p.games.end()) {
                auto plan = planLabel(p);
                if (!plan.empty() && plan[0] >= 'A' && plan[0] <= 'Z') plan[0] = static_cast<char>(plan[0] - 'A' + 'a');
                why += " " + trf("It plays this plan: {}.", plan);
                break;
            }
        ImGui::TextDisabled("%s", why.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(8), ImGui::GetContentRegionMax().x - w));
        if (ImGui::Button(open)) {
            ui::endCard();
            ImGui::PopID();
            openReportGame(k.game);
            return;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("On the Board page, at this position"));
        ui::endCard();
        ImGui::PopID();
    }
}

void App::drawReportNovelties() {
    auto& r = *opening_report_;
    if (!r.report || r.report->made < study::ReportSection::Done) {
        ImGui::TextDisabled("%s", TR("Looking for the novelties along the main lines…"));
        return;
    }
    const auto& novelties = r.report->novelties.moves;
    if (novelties.empty()) {
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", TR("No move along the main lines was first played in the last five years of your "
                                     "databases, nor left them in a game of yours."));
        ImGui::PopTextWrapPos();
    } else if (ImGui::BeginTable("##report_novelties", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, px(90));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        for (std::size_t i = 0; i < novelties.size(); ++i) {
            const auto& n    = novelties[i];
            const auto moves = movesOfCodes(r.board, n.codes);
            auto before      = r.board;
            for (std::size_t m = 0; m + 1 < moves.size(); ++m) before.makeMove(moves[m]);
            const auto label = moves.empty() ? std::string() : numbered(before, moves.back());
            const int year   = static_cast<int>(n.first >> 9);
            std::string text;
            if (n.yours && n.game && n.game->source < r.bases.size()) {
                const int month = static_cast<int>((n.first >> 5) & 15);
                const auto when = month ? fide::monthText(fmt::format("{}-{:02}", year, month)) : std::to_string(year);
                text = trn("Your game of {} ({}) left your databases here: none of the {} game had played it.",
                           "Your game of {} ({}) left your databases here: none of the {} games had played it.",
                           static_cast<int>(std::min<uint64_t>(n.before, 1000000000)), when,
                           r.bases[n.game->source].name, ui::exactCount(n.before));
            } else {
                const bool white = before.sideToMove() == chess::Color::WHITE;
                const int ws     = whiteScore(n.stats);
                const int score  = ws < 0 ? -1 : white ? ws : 100 - ws;
                const auto again = n.stats.games > 0 ? n.stats.games - 1 : 0;
                if (n.stats.games >= 10)
                    text = score < 0 ? trn("First played in {}, repeated {} time since.",
                                           "First played in {}, repeated {} times since.",
                                           static_cast<int>(std::min<uint64_t>(again, 1000000000)), year,
                                           ui::exactCount(again))
                           : white   ? trn("First played in {}, repeated {} time since; White scores {} % with it.",
                                           "First played in {}, repeated {} times since; White scores {} % with it.",
                                           static_cast<int>(std::min<uint64_t>(again, 1000000000)), year,
                                           ui::exactCount(again), score)
                                     : trn("First played in {}, repeated {} time since; Black scores {} % with it.",
                                           "First played in {}, repeated {} times since; Black scores {} % with it.",
                                           static_cast<int>(std::min<uint64_t>(again, 1000000000)), year,
                                           ui::exactCount(again), score);
                else
                    text = score < 0 ? trn("Since {}: {} game. Still rare.", "Since {}: {} games. Still rare.",
                                           static_cast<int>(n.stats.games), year, n.stats.games)
                           : white ? trn("Since {}: {} game, White {} %. Still rare.",
                                         "Since {}: {} games, White {} %. Still rare.", static_cast<int>(n.stats.games),
                                         year, n.stats.games, score)
                                   : trn("Since {}: {} game, Black {} %. Still rare.",
                                         "Since {}: {} games, Black {} %. Still rare.", static_cast<int>(n.stats.games),
                                         year, n.stats.games, score);
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushFont(ui::titleFont(), 0.0f);
            ImGui::TextUnformatted(label.c_str());
            ImGui::PopFont();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", lineText(r.board, moves).c_str());
            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos();
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", TR("A move's first year comes from the trees of your databases, which hold every game "
                                 "that reaches its position; your game that left them is confirmed by a search of the "
                                 "position, as the Tree tab confirms a novelty."));
    ImGui::PopTextWrapPos();
}

void App::drawReportSummary() {
    auto& r = *opening_report_;
    if (aidsLocked()) {
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", TR("The summary waits for the end of your correspondence game."));
        ImGui::PopTextWrapPos();
        return;
    }
    const bool writing = r.summary_job.valid();
    if (!r.summary && !writing) {
        ImGui::TextDisabled("%s", r.report && r.report->made == study::ReportSection::Done
                                      ? TR("Waiting for the evaluations…")
                                      : TR("The summary is written once the report is made."));
        return;
    }
    // What it rests on (R9): at the cursor over its title, and in its card below.
    std::vector<std::string> basis;
    {
        std::vector<std::string> sources;
        if (!r.bases.empty()) sources.emplace_back(TR("your databases"));
        if (r.masters && r.lichess[0]) sources.emplace_back(TR("Lichess's masters"));
        if (r.players && r.lichess[1]) sources.emplace_back(TR("Lichess's players"));
        basis.push_back(trf("the figures of this report ({})", listed(sources)));
        if (r.report && r.report->shapes.enough)
            basis.emplace_back(TR("the assistant's facts on the positions at move 20"));
        std::vector<std::string> evals;
        if (r.evals_offline) evals.emplace_back(TR("Lichess, kept offline"));
        if (std::any_of(r.evals.begin(), r.evals.end(), [](const auto& e) { return e && e->engine; }))
            evals.push_back(r.evals_engine);
        if (!evals.empty()) basis.push_back(trf("the evaluations ({})", listed(evals)));
        if (!r.opening.empty()) basis.emplace_back(TR("the opening's name (Lichess's named openings, public domain)"));
    }
    const auto col  = ui::assistantColour();
    const auto tint = ImGui::ColorConvertU32ToFloat4(col);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(tint.x, tint.y, tint.z, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(tint.x, tint.y, tint.z, 0.40f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(14), px(12)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(8));
    ImGui::BeginChild("##report_summary", ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
    ImGui::PushTextWrapPos();
    ui::inlineIcon(ui::Icon::Comment, col);
    ImGui::SameLine();
    ImGui::TextColored(tint, "%s", TR("The assistant's summary"));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", trf("What it rests on: {}.", listed(basis)).c_str());
    ImGui::SameLine();
    const bool model = r.summary && r.summary->by_model;
    ImGui::TextDisabled(
        "%s", writing
                  ? trf("· its model is writing: {} of {} passages…", r.summary_done->load(), r.summary_total).c_str()
              : model ? TR("· written by its model, read by the checker")
                      : TR("· written by the templates, from the same figures"));
    if (writing) {
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Cancel"))) *r.summary_cancel = true;
    }
    if (r.summary) {
        ImGui::PushFont(ui::italicFont(), 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, tint);
        std::string all;
        for (const auto& p : r.summary->passages) {
            const auto& text = r.templates_shown ? p.templ : p.text;
            if (text.empty()) continue;
            ImGui::TextUnformatted(text.c_str());
            all += (all.empty() ? "" : " ") + text;
            // A passage the checker refused twice: the templates', and why.
            if (!r.templates_shown && !p.by_model && !p.refused.empty() && ImGui::IsItemHovered()) {
                std::vector<std::string> why;
                for (const auto& [said, refusal] : p.refused) why.push_back(refusalWords(refusal));
                ImGui::SetTooltip("%s",
                                  trf("The templates' sentences: the model's were refused ({}).", listed(why)).c_str());
            }
        }
        ImGui::PopStyleColor();
        ImGui::PopFont();
        if (model && ImGui::SmallButton(r.templates_shown ? TR("The model's version") : TR("The templates' version")))
            r.templates_shown = !r.templates_shown;
        if (model) ImGui::SameLine();
        ImGui::BeginDisabled(!storyWithoutModel().empty() || writing);
        if (ImGui::SmallButton(TR("Rewrite"))) {
            ++r.round;
            r.templates_shown = false;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", storyWithoutModel().empty()
                                        ? TR("The model writes it otherwise, held to the same figures")
                                        : trf("Without the model: {}.", storyWithoutModel()).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Copy"))) {
            ImGui::SetClipboardText(all.c_str());
            boardToast(TR("The summary is copied"));
        }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    if (!r.summary) return;

    // What it rests on, and what the checker did.
    ImGui::Dummy(ImVec2(0, px(4)));
    if (ImGui::BeginTable("##report_basis", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ui::beginCard("report_rests");
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(TR("What it rests on"));
        ImGui::TextDisabled("%s", listed(basis).c_str());
        ImGui::PopTextWrapPos();
        ui::endCard();
        ImGui::TableNextColumn();
        int refused = 0, fallen = 0;
        for (const auto& p : r.summary->passages) {
            refused += static_cast<int>(p.refused.size());
            if (!p.by_model && !p.refused.empty()) ++fallen;
        }
        ui::beginCard("report_checker");
        ImGui::PushTextWrapPos();
        if (!model) {
            ImGui::TextUnformatted(TR("Written without the model"));
            ImGui::TextDisabled("%s", (storyWithoutModel().empty() ? std::string(TR("The model gave no answer."))
                                                                   : trf("Without the model: {}.", storyWithoutModel()))
                                          .c_str());
        } else if (refused == 0) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::GOOD), "%s", TR("The checker refused nothing"));
            ImGui::TextDisabled("%s", TR("Each sentence read against these figures and facts. A false sentence would "
                                         "have given way to the templates', and the cursor would say why."));
        } else {
            ImGui::TextColored(
                ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                trn("The checker refused {} sentence", "The checker refused {} sentences", refused, refused).c_str());
            ImGui::TextDisabled(
                "%s",
                trn("{} passage refused twice is the templates': the cursor over it says why.",
                    "{} passages refused twice are the templates': the cursor over them says why.", fallen, fallen)
                    .c_str());
        }
        ImGui::PopTextWrapPos();
        ui::endCard();
        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos();
    std::string note =
        TR("The model (4 billion parameters, on this machine) does not know the openings: it writes from "
           "the figures and facts it is given, and its grammar lets it name only the position's moves and "
           "the report's lines. Without it, the templates' version, written from the same figures.");
    if (model) note += " " + trf("Written in {} s.", fmt::format("{:.1f}", r.summary->seconds));
    ImGui::TextDisabled("%s", note.c_str());
    ImGui::PopTextWrapPos();
}

}  // namespace fcg
