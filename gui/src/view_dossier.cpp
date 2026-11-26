// A player's dossier (#90, mockup M13, board 3), in place of the page it was opened from (Esc goes back): who they are
// (FIDE's list, when matched), what it is made of (the user's databases, Lichess when ticked), then four tabs: an
// overview (six cards, their last games), their openings with each colour and the gaps of the user's repertoires,
// their rating year by year, structures and reviewed games, and with Lichess what they play online beside the board.
// Made again in the background at each opening (dossier.hpp); its games open on the Board page with the dossier as
// their list, its lines with the Tree tab on their games alone.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>  // SetKeyOwner

#include <commentary/fact_parts.hpp>
#include <study/background.hpp>
#include <study/query.hpp>

#include "app.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "publish_reports.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

int percent(double share) { return static_cast<int>(std::lround(share * 100)); }

std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// A list said as the interface's language joins its items ("a, b, c").
std::string listed(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& item : items) out = out.empty() ? item : trf("{}, {}", out, item);
    return out;
}

// "1.e4 e5 2.Nf3": moves from the start.
std::string movesText(const std::vector<std::string>& moves) {
    std::string out;
    for (std::size_t i = 0; i < moves.size(); ++i) {
        if (!out.empty()) out += ' ';
        out += i % 2 == 0 ? fmt::format("{}.{}", i / 2 + 1, moves[i]) : moves[i];
    }
    return out;
}

std::string years(int first, int last) {
    if (first == 0) return {};
    return first == last ? fmt::format("{}", first) : fmt::format("{}–{}", first, last);
}

std::string score(const study::DossierScore& s) { return fmt::format("+{} ={} −{}", s.won, s.drawn, s.lost); }

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

// A source of the dossier as a chip: filled when it is in, outlined in dashes when it can be added. True when clicked.
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

// A text that wraps, its percentages ("44 %") kept whole on a line: the space before "%" one that does not break.
std::string percentsKept(std::string text) {
    for (std::size_t at = 0; (at = text.find(" %", at)) != std::string::npos; at += 3) text.replace(at, 1, "\u00a0");
    return text;
}

// A card of the overview: what it is, its value large, what it means.
void card(const char* id, const char* title, const std::string& big, const std::string& text) {
    ui::beginCard(id);
    ImGui::TextDisabled("%s", title);
    ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize() * 1.15f);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(big.empty() ? "–" : percentsKept(big).c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    if (!text.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", percentsKept(text).c_str());
        ImGui::PopTextWrapPos();
    }
    ui::endCard();
}

// A structure's name, as the assistant's facts name its formation.
const char* formationName(int kind) {
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
    return "";
}

// "Isolated queen's pawn, theirs, with White": whose it is, and the colour they had.
std::string structureLabel(const study::DossierStructure& s) {
    const char* name = formationName(s.kind);
    if (s.theirs) return s.white ? trf("{}, theirs, with White", name) : trf("{}, theirs, with Black", name);
    return s.white ? trf("{}, their opponent's, with White", name) : trf("{}, their opponent's, with Black", name);
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Opening it, and its job

void App::openDossier(study::DossierSubject subject, bool lichess) {
    if (dossier_ && dossier_->job.valid()) {
        *dossier_->cancel = true;
        dossier_->job.wait();  // stopped within a tenth of a second
    }
    // Shown in a view that holds the Databases page: from a view of the engines, the first one that does.
    if (!viewHolds(Page::Databases)) page_ = Page::Databases;
    DossierState d;
    d.subject = std::move(subject);
    d.over    = page_;
    d.lichess = lichess && !d.subject.lichess.empty();
    dossier_  = std::move(d);
    if (dossier_->lichess && (!prep_ || lower(prep_->opponent) != lower(dossier_->subject.lichess)))
        startPrep(dossier_->subject.lichess);
    startDossier();
}

void App::openDossierOf(const study::Snapshot& snapshot, uint32_t name_id) {
    study::DossierSubject subject;
    subject.name = std::string(snapshot.shownName(name_id));
    if (const auto* p = snapshot.players()->of(name_id)) {
        subject.fide    = p->fide;
        subject.born    = p->born;
        subject.lichess = p->lichess;
    }
    openDossier(std::move(subject));
}

void App::startDossier() {
    if (!dossier_) return;
    auto& d = *dossier_;
    if (d.job.valid()) {  // made again once the one running ends
        d.remake  = true;
        *d.cancel = true;
        return;
    }
    DossierAsk ask;
    ask.subject            = d.subject;
    auto& s                = shelf();
    const auto online_name = d.subject.lichess.empty() ? std::string() : lower("Lichess · " + d.subject.lichess);
    d.folders.clear();
    // The user's databases; those of Lichess players' games ("Lichess · name", their syncs) are online games: their
    // account's is the Lichess source, ticked; the others' are left out (the same games again).
    const std::string synced = lower("Lichess · ");
    for (std::size_t i = 0; i < s.entries().size(); ++i) {
        const auto& e = s.entries()[i];
        if (e.source) continue;
        if (!online_name.empty() && lower(e.name) == online_name) {
            if (d.lichess)
                if (auto db = s.open(i)) ask.online = DossierBase{e.name, e.folder, std::move(db)};
            continue;
        }
        if (lower(e.name).rfind(synced, 0) == 0) continue;
        if (auto db = s.open(i)) ask.bases.push_back(DossierBase{e.name, e.folder, std::move(db)});
    }
    for (const auto& b : ask.bases) d.folders.emplace_back(b.folder, b.name);
    if (ask.online) d.folders.emplace_back(ask.online->folder, ask.online->name);
    auto& reps = repertoires();
    for (std::size_t i = 0; i < reps.entries().size(); ++i)
        ask.repertoires.emplace_back(reps.entries()[i].name, reps.map(i));
    ask.user_names    = userNames();
    ask.analyses      = storage_.root() / "analyses";
    ask.fide_list     = fideList();
    ask.keep_namesake = std::exchange(d.keep_namesake, std::nullopt);
    ask.link_lichess  = std::exchange(d.link_lichess, std::string());
    ask.restore       = std::exchange(d.restore, {});
    d.cancel          = std::make_shared<std::atomic<bool>>(false);
    d.progress        = std::make_shared<study::DossierProgress>();
    d.remake          = false;
    d.job = std::async(std::launch::async, [ask = std::move(ask), progress = d.progress, cancel = d.cancel]() mutable {
        study::enterBackground();  // the lowest priority: a tournament beside it loses no time
        return makeDossierReport(std::move(ask), *progress, *cancel);
    });
}

void App::pollDossier() {
    if (!dossier_) return;
    auto& d = *dossier_;
    // Another page shown (the rail, a command): the dossier steps aside.
    if (d.shown && page_ != d.over) d.shown = false;
    if (d.job.valid() && d.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto report = d.job.get();
        // A choice it kept in the tables stays kept, and undone by Ctrl+Z, even when it is made again at once.
        if (!report.tables_before.empty()) d.undo = report.tables_before;
        if (!report.error.empty()) d.said = trf("Not kept in the tables of names: {}", report.error);
        d.subject = report.subject;
        if (d.remake) {
            startDossier();
            return;
        }
        if (report.cancelled) return;
        if (!report.namesakes.empty() && d.namesake >= static_cast<int>(report.namesakes.size())) d.namesake = 0;
        d.listed.reset();
        d.report = std::move(report);
    }
    // The Lichess source: the dossier made again once their games came.
    if (d.lichess && prep_ && lower(prep_->opponent) == lower(d.subject.lichess) && !prep_->sync.valid() &&
        d.synced != prep_->synced) {
        d.synced = prep_->synced;
        startDossier();
    }
}

void App::openDossierGame(std::size_t index) {
    if (!dossier_ || !dossier_->report || !dossier_->report->dossier) return;
    auto& d             = *dossier_;
    const auto& dossier = *d.report->dossier;
    if (index >= dossier.games.size()) return;
    if (!d.listed) {
        auto all = std::make_shared<std::vector<BoardOrigin::Listed>>();
        for (const auto& g : dossier.games) {
            BoardOrigin::Listed l;
            if (g.source < d.folders.size()) {
                l.folder = d.folders[g.source].first;
                l.name   = d.folders[g.source].second;
            }
            l.snapshot = dossier.sources[g.source].snapshot;
            l.id       = g.id;
            all->push_back(std::move(l));
        }
        d.listed = std::move(all);
    }
    BoardOrigin origin;
    origin.listed    = d.listed;
    origin.list_name = trf("Dossier of {}", d.subject.name);
    board_origin_    = std::move(origin);
    openListRow(index);
    if (board_back_) board_back_->dossier = true;
    d.shown = false;
}

void App::openDossierLine(const std::vector<std::string>& moves, bool white) {
    if (!dossier_) return;
    auto& d = *dossier_;
    readOnBoard(movesText(moves), white ? trf("{} with White", d.subject.name) : trf("{} with Black", d.subject.name));
    board_.ply            = static_cast<int>(board_.game.moves.size());  // the position the line comes to
    board_side_tab_       = 2;                                           // the Tree tab, on their games
    tree_dossier_request_ = white ? 0 : 1;
    if (board_back_) board_back_->dossier = true;
    d.shown = false;
}

void App::dossierInList(std::size_t source, std::vector<study::GameId> ids, std::string said) {
    if (!dossier_ || source >= dossier_->folders.size()) return;
    const auto& folder = dossier_->folders[source].first;
    const auto& s      = shelf();
    for (std::size_t i = 0; i < s.entries().size(); ++i) {
        if (s.entries()[i].folder != folder) continue;
        std::sort(ids.begin(), ids.end());
        page_           = Page::Databases;
        db_selected_    = static_cast<int>(i);
        db_list_        = true;
        db_only_        = std::move(ids);
        db_only_said_   = std::move(said);
        db_row_         = 0;
        db_search_open_ = false;
        dossier_->shown = false;
        return;
    }
}

void App::lookForDossierNames(const std::string& query) {
    // The words typed, less the start of the command's own name ("dos", "dossier").
    const auto command = study::foldText(TR("Dossier"));
    std::string words;
    std::size_t letters = 0;
    for (std::size_t at = 0; at < query.size();) {
        auto end = query.find(' ', at);
        if (end == std::string::npos) end = query.size();
        auto word = query.substr(at, end - at);
        at        = end + 1;
        if (word.empty()) continue;
        if (!word.empty() && word.back() == ':') word.pop_back();
        const auto folded = study::foldText(word);
        if (folded.size() >= 3 && command.rfind(folded, 0) == 0) continue;
        words += (words.empty() ? "" : " ") + word;
        letters += word.size();
    }
    if (letters < 3) words.clear();
    auto& job = dossier_names_job_;
    if (job && job->done.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto found = job->done.get();
        if (!*job->cancel && job->of == words) {
            dossier_names_    = std::move(found);
            dossier_names_of_ = job->of;
        }
        job.reset();
    }
    if (words.empty()) {
        dossier_names_.clear();
        dossier_names_of_.clear();
        if (job) *job->cancel = true;
        return;
    }
    if (words == dossier_names_of_ || (job && job->of == words)) return;
    if (job) {  // another one runs: stopped, this one asked once it has
        *job->cancel = true;
        return;
    }
    std::vector<std::shared_ptr<const study::Snapshot>> snapshots;
    auto& s = shelf();
    for (std::size_t i = 0; i < s.entries().size(); ++i)
        if (!s.entries()[i].source)
            if (const auto db = s.open(i)) snapshots.push_back(db->snapshot());
    DossierNamesJob j;
    j.of     = words;
    j.cancel = std::make_shared<std::atomic<bool>>(false);
    j.done   = std::async(std::launch::async, [snapshots = std::move(snapshots), words, cancel = j.cancel] {
        study::enterBackground();
        return dossierNames(snapshots, words, 8, *cancel);
    });
    job      = std::move(j);
}

// ---------------------------------------------------------------------------------------------------------------------
// The page

void App::drawDossier() {
    auto& d = *dossier_;
    const ui::TopicRegion topic(guide::topics::Dossier);
    const bool typing = ImGui::GetIO().WantTextInput;
    const bool popup  = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup);

    // Where it is, and the way back.
    const bool escape = !typing && !popup && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (escape && d.lichess_typing) {
        d.lichess_typing = false;
    } else if (ui::iconButton("dossier_back", ui::Icon::Back,
                              d.over == Page::Databases ? TR("Databases")
                              : d.over == Page::Lichess ? "Lichess"
                              : d.over == Page::Board   ? TR("Board")
                                                        : TR("Back")) ||
               escape) {
        d.shown = false;
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("›");
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("Dossier"));
    ImGui::SameLine(0, px(16));
    ImGui::TextDisabled("%s", TR("Esc"));
    ImGui::SameLine();
    ui::guideLink("dossier_help", guide::topics::Dossier);

    // Fair play (R17): in an arena from here, no report until Pause (#95).
    if (arenaHoldsAids()) {
        ImGui::Dummy(ImVec2(0, px(8)));
        ui::beginCard("dossier_arena");
        ui::inlineIcon(ui::Icon::Lock, ui::ACCENT);
        ImGui::SameLine();
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(TR("Locked while you are in an arena on Lichess, between two games too."));
        ImGui::TextDisabled("%s", TR("The reports come back with Pause (Alt+P), or at the arena's end."));
        ImGui::PopTextWrapPos();
        ui::endCard();
        return;
    }
    // Fair play (R17): while the user plays them on Lichess, nothing of them.
    if (!d.subject.lichess.empty() && std::any_of(lichess_playing_.begin(), lichess_playing_.end(), [&](const auto& g) {
            return lower(g.opponent) == lower(d.subject.lichess);
        })) {
        ImGui::Dummy(ImVec2(0, px(8)));
        ui::beginCard("dossier_locked");
        ui::inlineIcon(ui::Icon::Lock, ui::ACCENT);
        ImGui::SameLine();
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(
            trf("Locked while your game against {} is played on Lichess.", d.subject.lichess).c_str());
        ImGui::TextDisabled("%s", TR("Their dossier comes back at the end of the game."));
        ImGui::PopTextWrapPos();
        ui::endCard();
        return;
    }

    const auto* r  = d.report ? &*d.report : nullptr;
    const auto* id = r && r->identity ? &*r->identity : nullptr;
    if (export_when_made_.first == 0 && r && r->board.games > 0) {  // --focus export-dossier=… (#93)
        const int format  = export_when_made_.second;
        export_when_made_ = {-1, -1};
        openDossierExport(format == static_cast<int>(ExportFormat::Html));
        return;
    }

    // Who: their name, and FIDE's card when they are matched with the list.
    ImGui::Dummy(ImVec2(0, px(4)));
    ImGui::AlignTextToFramePadding();
    ui::inlineIcon(ui::Icon::User, ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize() * 1.4f);
    ImGui::TextUnformatted(d.subject.name.c_str());
    ImGui::PopFont();
    if (id) {
        std::vector<std::string> parts;
        if (!id->title.empty()) parts.push_back(id->title);
        if (!id->federation.empty()) parts.push_back(id->federation);
        if (id->born) parts.push_back(trf("born {}", id->born));
        if (id->standard) parts.push_back(trf("standard {}", id->standard));
        if (id->rapid) parts.push_back(trf("rapid {}", id->rapid));
        if (id->blitz) parts.push_back(trf("blitz {}", id->blitz));
        if (!id->list.empty()) parts.push_back(trf("FIDE's list of {}", fide::monthText(id->list)));
        std::string line;
        for (const auto& p : parts) line += (line.empty() ? "" : " · ") + p;
        ImGui::SameLine(0, px(12));
        ImGui::TextDisabled("%s", line.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", trf("FIDE id {}", id->fide).c_str());
    }
    // Their games in the game list: of the database, or chosen among them.
    if (r && r->board.games > 0) {
        const char* in_list = TR("Their games in the list");
        const char* print   = TR("Print…");
        const char* html    = TR("HTML page");
        const auto& style   = ImGui::GetStyle();
        const float w = ImGui::CalcTextSize(in_list).x + ImGui::CalcTextSize(print).x + ImGui::CalcTextSize(html).x +
                        style.FramePadding.x * 6 + style.ItemSpacing.x * 2;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(12), ImGui::GetContentRegionMax().x - w));
        // Printed, and as a page of HTML (#93).
        if (ImGui::Button(print)) openDossierExport(false);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("The dossier as a PDF of a few pages, made by RookStudio"));
        ImGui::SameLine();
        if (ImGui::Button(html)) openDossierExport(true);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("The dossier as a page of its own, their lines replayable"));
        ImGui::SameLine();
        if (ImGui::Button(in_list)) {
            std::vector<std::size_t> with;
            for (std::size_t s = 0; s < r->board.sources.size(); ++s)
                if (r->board.sources[s].games > 0) with.push_back(s);
            if (with.size() == 1) {
                std::vector<study::GameId> ids;
                for (const auto& g : r->dossier->games)
                    if (g.source == with.front()) ids.push_back(g.id);
                dossierInList(with.front(), std::move(ids), trf("the games of {}", d.subject.name));
                return;
            }
            ImGui::OpenPopup("##dossier_in_list");
        }
        if (ImGui::BeginPopup("##dossier_in_list")) {
            for (std::size_t s = 0; s < r->board.sources.size() && s < d.folders.size(); ++s) {
                const auto n = r->board.sources[s].games;
                if (n == 0) continue;
                if (ImGui::Selectable(fmt::format("{} · {}##in{}", d.folders[s].second,
                                                  trn("{} game", "{} games", n, ui::exactCount(n)), s)
                                          .c_str())) {
                    std::vector<study::GameId> ids;
                    for (const auto& g : r->dossier->games)
                        if (g.source == s) ids.push_back(g.id);
                    ImGui::EndPopup();
                    dossierInList(s, std::move(ids), trf("the games of {}", d.subject.name));
                    return;
                }
            }
            ImGui::EndPopup();
        }
    }

    // What it is made of: the databases, Lichess, the spellings brought together.
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TR("Made of:"));
    ImGui::SameLine();
    {
        std::string text;
        if (!r) {
            text = TR("Your databases");
        } else if (r->board.games == 0) {
            text = TR("Your databases · no game of theirs");
        } else {
            std::vector<std::string> each;
            int with = 0;
            for (std::size_t s = 0; s < r->board.sources.size() && s < d.folders.size(); ++s)
                if (const auto n = r->board.sources[s].games; n > 0) {
                    ++with;
                    each.push_back(fmt::format("{} {}", d.folders[s].second, ui::exactCount(n)));
                }
            const auto n     = r->board.games;
            const auto games = with > 1 ? trn("{} game ({})", "{} games ({})", n, ui::exactCount(n), listed(each))
                                        : trn("{} game", "{} games", n, ui::exactCount(n));
            text             = trf("Your databases · {} · {}", games,
                                   years(static_cast<int>(r->board.first >> 9), static_cast<int>(r->board.last >> 9)));
        }
        chip("##dossier_bases", text, !r || r->board.games > 0);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("Their games in your databases, all their spellings together (the tables of "
                                       "names): nothing leaves the machine."));
    }
    ImGui::SameLine(0, px(6));
    if (d.lichess_typing) {
        if (d.lichess_focus) {
            ImGui::SetKeyboardFocusHere();
            d.lichess_focus = false;
        }
        ImGui::SetNextItemWidth(px(180));
        const bool enter =
            ImGui::InputTextWithHint("##dossier_lichess", TR("Their Lichess name"), d.lichess_name.data(),
                                     d.lichess_name.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        const std::string name = d.lichess_name.data();
        const bool valid = name.size() >= 2 && name.size() <= 30 && std::all_of(name.begin(), name.end(), [](char c) {
                               return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
                           });
        ImGui::SameLine();
        ImGui::BeginDisabled(!valid);
        if (ImGui::SmallButton(TR("Link")) || (enter && valid)) {
            d.undo_subject    = d.subject;
            d.lichess_typing  = false;
            d.link_lichess    = name;
            d.subject.lichess = name;
            d.lichess         = true;
            startPrep(name);
            startDossier();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("Enter · kept in the tables of names"));
    } else if (d.subject.lichess.empty()) {
        if (chip("##dossier_add_lichess", TR("+ Lichess…"), false)) {
            d.lichess_typing = true;
            d.lichess_focus  = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("Their Lichess account: its games of the last year beside those of your "
                                       "databases, the link kept in the tables of names."));
    } else {
        std::string text;
        const auto* p = prep_ && lower(prep_->opponent) == lower(d.subject.lichess) ? &*prep_ : nullptr;
        if (!d.lichess) {
            text = trf("Lichess · {}", d.subject.lichess);
        } else if (p && p->sync.valid()) {
            text = trf("Lichess · {} · {}", d.subject.lichess, TR("coming from Lichess…"));
        } else if (p && p->error.failed()) {
            // Offline: the games of the last sync, dated.
            text = p->last_sync.empty() ? trf("Lichess · {} · never synced", d.subject.lichess)
                                        : trf("Lichess · {} · synced on {}", d.subject.lichess, p->last_sync);
        } else {
            const auto n = r ? r->online.games : 0;
            text         = trf("Lichess · {} · {}", d.subject.lichess,
                               trn("{} game of the last year", "{} games of the last year", n, ui::exactCount(n)));
        }
        if (chip("##dossier_lichess_chip", text, d.lichess)) {
            d.lichess = !d.lichess;
            if (d.lichess && (!prep_ || lower(prep_->opponent) != lower(d.subject.lichess)))
                startPrep(d.subject.lichess);
            startDossier();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", d.lichess ? TR("Asked of Lichess: their name with a year of blitz, rapid and "
                                                   "classical games, and their public profile. A click takes the "
                                                   "source out.")
                                              : TR("A click asks Lichess for their games of the last year and their "
                                                   "public profile: their name and those filters only."));
    }
    if (r && r->spellings >= 2) {
        ImGui::SameLine(0, px(10));
        ImGui::TextDisabled("%s", trn("{} spelling brought together (the tables of names)",
                                      "{} spellings brought together (the tables of names)", r->spellings, r->spellings)
                                      .c_str());
    }
    if (prep_ && d.lichess && lower(prep_->opponent) == lower(d.subject.lichess) && prep_->error.failed())
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           lichess::describeAlone(prep_->error).c_str());
    if (!d.said.empty()) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", d.said.c_str());
    }

    // Being made: its progress, Cancel (R18).
    if (d.job.valid()) {
        const auto total = d.progress->total.load(), done = d.progress->done.load();
        const float fraction = total ? static_cast<float>(done) / static_cast<float>(total) : 0.0f;
        ImGui::ProgressBar(fraction, ImVec2(px(320), 0), trf("Making the dossier… {} %", percent(fraction)).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Cancel"))) {
            *d.cancel = true;
            d.remake  = false;
        }
        if (!r) return;
    } else if (!r) {
        ImGui::TextDisabled("%s", TR("Cancelled."));
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Make it again"))) startDossier();
        return;
    }

    // Ctrl+Z: the last choice kept in the tables of names, undone.
    if (!typing && !popup && !d.undo.empty() && d.undo_subject && !d.job.valid() &&
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
        d.restore = std::exchange(d.undo, {});
        d.subject = *std::exchange(d.undo_subject, std::nullopt);
        if (d.subject.lichess.empty()) d.lichess = false;
        d.said = TR("The tables of names are as they were.");
        startDossier();
    }

    // Namesakes: FIDE's list has several players of that name.
    if (!r->namesakes.empty() && !d.namesakes_aside) {
        ImGui::Dummy(ImVec2(0, px(4)));
        // ↑ ↓ and Enter are the question's: claimed, ImGui's keyboard navigation leaves them alone.
        const auto keys = ImGui::GetID("##dossier_namesake_keys");
        for (const auto key : {ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Enter, ImGuiKey_KeypadEnter})
            ImGui::SetKeyOwner(key, keys);
        ui::beginCard("dossier_namesakes");
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(trf("Dossier of “{}”", d.subject.name).c_str());
        ImGui::SameLine();
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
            trn("· {} player of this name on FIDE's list: which one?",
                "· {} players of this name on FIDE's list: which one?", r->namesakes.size(), r->namesakes.size())
                .c_str());
        int low = 0, high = 0;
        for (const auto& g : r->dossier->games)
            if (!g.online && g.ownElo() > 0) {
                low  = low ? std::min(low, g.ownElo()) : g.ownElo();
                high = std::max(high, g.ownElo());
            }
        const int count = static_cast<int>(r->namesakes.size());
        if (!typing && !popup) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) d.namesake = std::min(d.namesake + 1, count - 1);
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) d.namesake = std::max(d.namesake - 1, 0);
        }
        for (int i = 0; i < count; ++i) {
            const auto& n = r->namesakes[static_cast<std::size_t>(i)];
            std::vector<std::string> parts;
            if (!n.federation.empty()) parts.push_back(n.federation);
            if (n.born) parts.push_back(trf("born {}", n.born));
            if (n.standard) parts.push_back(trf("standard {}", n.standard));
            if (n.rapid) parts.push_back(trf("rapid {}", n.rapid));
            if (n.blitz) parts.push_back(trf("blitz {}", n.blitz));
            std::string line;
            for (const auto& p : parts) line += (line.empty() ? "" : " · ") + p;
            if (ImGui::RadioButton(fmt::format("{}##namesake{}", line, i).c_str(), d.namesake == i)) d.namesake = i;
            if (i == 0 && high > 0) {
                ImGui::SameLine();
                ImGui::TextColored(
                    ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s",
                    (low == high ? trf("proposed: their games in your databases are at {}", low)
                                 : trf("proposed: their games in your databases are at {}–{}", low, high))
                        .c_str());
            }
        }
        const bool enter =
            !typing && !popup &&
            (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
        if (ImGui::Button(TR("Keep this one")) || enter) {
            d.undo_subject  = d.subject;
            d.keep_namesake = r->namesakes[static_cast<std::size_t>(d.namesake)];
            d.said          = trf("Kept in the tables of names: {}. Ctrl+Z undoes.", d.keep_namesake->name);
            startDossier();
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Not now"))) d.namesakes_aside = true;
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("Enter keeps the choice in the tables of names: the dossier then takes only that "
                                     "player's games."));
        ImGui::PopTextWrapPos();
        ui::endCard();
        // Its border in the colour of a question to answer.
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ui::WARN,
                                            ImGui::GetStyle().ChildRounding, 0, px(1.5f));
    }

    const auto& b = r->board;
    // None of theirs in the databases: where their games can come from.
    if (b.games == 0) {
        ImGui::Dummy(ImVec2(0, px(6)));
        ui::beginCard("dossier_none");
        ui::inlineIcon(ui::Icon::Search, ImGui::GetColorU32(ImGuiCol_Text));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(trf("Dossier of “{}”: no game in your databases", d.subject.name).c_str());
        ImGui::TextDisabled("%s", TR("The dossier is made of their games."));
        ImGui::PopTextWrapPos();
        if (ImGui::SmallButton(TR("From a source… (TWIC, a broadcast)"))) {
            page_            = Page::Databases;
            db_list_         = false;
            db_sources_open_ = true;
            d.shown          = false;
        }
        if (d.subject.lichess.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("+ Lichess… (their account)"))) {
                d.lichess_typing = true;
                d.lichess_focus  = true;
            }
        }
        ImGui::EndGroup();
        ui::endCard();
        if (!d.lichess || r->online.games == 0) return;
    } else if (b.games < 20) {
        ImGui::Dummy(ImVec2(0, px(2)));
        ui::inlineIcon(ui::Icon::Info, ui::MUTED);
        ImGui::SameLine();
        ImGui::TextUnformatted(
            trn("{} game: hints, not trends", "{} games: hints, not trends", b.games, ui::exactCount(b.games)).c_str());
    }

    // The tabs (Ctrl+Page Down, Ctrl+Page Up).
    const char* tabs[] = {TR("Overview"), TR("Openings"), TR("Rating and structures"), "Lichess"};
    const int count    = d.lichess ? 4 : 3;
    if (!typing && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_PageDown)) d.tab = (d.tab + 1) % count;
    if (!typing && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_PageUp)) d.tab = (d.tab + count - 1) % count;
    if (b.games == 0) d.tab = 3;  // only the online part has something
    d.tab = std::clamp(d.tab, 0, count - 1);
    if (ImGui::BeginTabBar("##dossier_tabs")) {
        const int asked = d.tab_shown != d.tab ? d.tab : -1;
        for (int i = 0; i < count; ++i) {
            if (b.games == 0 && i < 3) continue;
            if (ImGui::BeginTabItem(tabs[i], nullptr, asked == i ? ImGuiTabItemFlags_SetSelected : 0)) {
                if (asked < 0) d.tab = i;
                ImGui::EndTabItem();
            }
        }
        d.tab_shown = d.tab;
        ImGui::EndTabBar();
    }
    ImGui::BeginChild("##dossier_body", ImVec2(0, 0), ImGuiChildFlags_None);
    switch (d.tab) {
        case 0:
            drawDossierOverview();
            break;
        case 1:
            drawDossierOpenings();
            break;
        case 2:
            drawDossierRating();
            break;
        default:
            drawDossierOnline();
            break;
    }
    ImGui::EndChild();
}

DossierCards dossierCards(const DossierReport& r, bool user_names) {
    const auto& b       = r.board;
    const auto& dossier = *r.dossier;
    const auto& names   = dossierOpenings();
    DossierCards cards;
    auto& big  = cards.big;
    auto& text = cards.text;
    {
        const auto& p = r.board_play;
        if (!p.with_white.empty())
            big[0] = trf("1.{} in {} % · score {} %", p.with_white[0].san, percent(p.with_white[0].share),
                         percent(p.with_white[0].score));
        // Their openings by family ("Ruy Lopez: Berlin Defense (34)"), the lines of a family counted together.
        std::vector<std::string> lines;
        const auto played = study::dossierFamilies(dossier, study::DossierParts::Board, true, -1, names);
        for (std::size_t i = 0; i < played.size() && i < 3; ++i)
            lines.push_back(trf("{} ({})", played[i].family, played[i].score.games));
        text[0] = listed(lines);
        std::vector<std::string> families, against;
        for (std::size_t i = 0; i < p.with_black.size(); ++i) {
            const auto& a = p.with_black[i];
            if (a.families.empty() || a.games == 0) continue;
            const auto& f    = a.families.front();
            const auto share = percent(static_cast<double>(f.score.games) / static_cast<double>(a.games));
            const auto name  = f.family.substr(0, f.family.find(':'));
            if (std::find(families.begin(), families.end(), name) == families.end()) families.push_back(name);
            against.push_back(against.empty() ? trf("Against 1.{}: {} in {} %", a.san, f.family, share)
                                              : trf("against 1.{}: {} in {} %", a.san, f.family, share));
        }
        for (const auto& f : families) big[1] += (big[1].empty() ? "" : " · ") + f;
        std::string sentence;
        for (const auto& a : against) sentence = sentence.empty() ? a : trf("{}; {}", sentence, a);
        if (b.black.decided() > 0)
            sentence = sentence.empty() ? trf("Score {} %.", percent(b.black.share()))
                                        : trf("{}. Score {} %.", sentence, percent(b.black.share()));
        text[1] = sentence;
    }
    if (!b.years.empty()) {
        big[2]           = b.years.size() == 1 ? fmt::format("{}", b.years.front().elo)
                                               : fmt::format("{} → {}", b.years.front().elo, b.years.back().elo);
        const auto month = b.highest_date >> 5 & 15;
        const auto when  = month ? fide::monthText(fmt::format("{}-{:02}", b.highest_date >> 9, month))
                                 : fmt::format("{}", b.highest_date >> 9);
        text[2] = b.years.size() == 1
                      ? trf("In {}, from their games; the highest, {} in {}.", b.years.front().year, b.highest, when)
                      : trf("From {} to {}, from their games; the highest, {} in {}.", b.years.front().year,
                            b.years.back().year, b.highest, when);
    }
    big[3] = score(b.all);
    {
        const bool stronger = b.stronger.decided() > 0, weaker = b.weaker.decided() > 0;
        if (stronger && weaker)
            text[3] = trf("Against stronger players: {} %; against weaker ones: {} %.", percent(b.stronger.share()),
                          percent(b.weaker.share()));
        else if (stronger)
            text[3] = trf("Against stronger players: {} %.", percent(b.stronger.share()));
        else if (weaker)
            text[3] = trf("Against weaker players: {} %.", percent(b.weaker.share()));
    }
    if (b.games < 5) {
        text[4] = TR("Fewer than five games: no structures.");
    } else if (b.structures.empty()) {
        text[4] = TR("No named structure at move 20 in their games.");
    } else {
        std::vector<std::string> names_of, said;
        for (std::size_t i = 0; i < b.structures.size() && i < 2; ++i) {
            const auto& s = b.structures[i];
            names_of.push_back(formationName(s.kind));
            said.push_back(trn("{} ({} game, {} %)", "{} ({} games, {} %)", s.score.games, structureLabel(s),
                               s.score.games, percent(s.score.share())));
        }
        big[4] =
            names_of.size() == 2 && names_of[0] != names_of[1] ? trf("{}, {}", names_of[0], names_of[1]) : names_of[0];
        text[4] = said.size() == 2 ? trf("{}; {}", said[0], said[1]) : said[0];
    }
    if (b.user.games > 0) {
        // The user's results against them.
        big[5] = trn("{} game: +{} ={} −{}", "{} games: +{} ={} −{}", b.user.games, b.user.games, b.user.lost,
                     b.user.drawn, b.user.won);
        if (b.last_user) {
            const auto& g  = dossier.games[*b.last_user];
            const auto row = dossier.sources[g.source].snapshot->row(g.id);
            text[5] = row.event.empty() ? trf("The last in {} (Settings › Your names).", g.numbers.date >> 9)
                                        : trf("The last in {}, at {} (Settings › Your names).", g.numbers.date >> 9,
                                              std::string(row.event));
        }
    } else {
        big[5]  = TR("No game between you");
        text[5] = !user_names ? TR("Your names (Settings › Your names) tell your games against them.")
                              : TR("None in your databases (Settings › Your names).");
    }
    cards.titles = {TR("With White"),    TR("With Black"),       TR("Their rating"),
                    TR("Their results"), TR("Their structures"), TR("You against them")};
    return cards;
}

void App::drawDossierOverview() {
    auto& d             = *dossier_;
    const auto& r       = *d.report;
    const auto& dossier = *r.dossier;
    const auto& names   = dossierOpenings();
    // The six cards.
    const auto cards = dossierCards(r, !player_names_.empty());
    if (ImGui::BeginTable("##dossier_cards", 3, ImGuiTableFlags_SizingStretchSame)) {
        for (int i = 0; i < 6; ++i) {
            ImGui::TableNextColumn();
            card(fmt::format("dossier_card{}", i).c_str(), cards.titles[static_cast<std::size_t>(i)].c_str(),
                 cards.big[static_cast<std::size_t>(i)], cards.text[static_cast<std::size_t>(i)]);
        }
        ImGui::EndTable();
    }

    // Their last games: a click opens one, the dossier as its list.
    ImGui::Dummy(ImVec2(0, px(4)));
    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##dossier_last", 4, flags)) {
        ImGui::TableSetupColumn(TR("Their last games"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn(TR("Event"), ImGuiTableColumnFlags_WidthStretch, 2.4f);
        ImGui::TableSetupColumn(TR("Result"), ImGuiTableColumnFlags_WidthFixed, px(52));
        ImGui::TableSetupColumn(TR("Opening"), ImGuiTableColumnFlags_WidthStretch, 2.6f);
        ImGui::TableHeadersRow();
        int shown = 0;
        for (std::size_t i = 0; i < dossier.games.size() && shown < 8; ++i) {
            const auto& g = dossier.games[i];
            if (g.online) continue;
            ++shown;
            const auto& snap = *dossier.sources[g.source].snapshot;
            const auto row   = snap.row(g.id);
            const auto ids   = snap.rowIds(g.id);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable("##game", false, ImGuiSelectableFlags_SpanAllColumns)) openDossierGame(i);
            ImGui::PopID();
            ImGui::SameLine(0, 0);
            cellText(fmt::format("{}  {} – {}", g.numbers.date ? study::dateText(g.numbers.date) : std::string("?"),
                                 snap.shownName(ids.white), snap.shownName(ids.black)));
            ImGui::TableNextColumn();
            const std::string event(row.event), round(row.round);
            cellText(round.empty() || round == "?" || round == "-" ? event : trf("{}, round {}", event, round));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.result == '1'   ? "1-0"
                                   : row.result == '0' ? "0-1"
                                   : row.result == '=' ? "½-½"
                                                       : "*");
            ImGui::TableNextColumn();
            if (g.opening >= 0)
                cellText(fmt::format("{} {}", names[static_cast<std::size_t>(g.opening)].eco,
                                     names[static_cast<std::size_t>(g.opening)].name),
                         true);
            else if (row.eco)
                cellText(study::ecoText(row.eco), true);
        }
        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", TR("A click on a game opens it on the Board page, with this dossier as its list (Ctrl+← "
                                 "and Ctrl+→)."));
    const double seconds = std::max(0.1, r.seconds);
    ImGui::TextDisabled(
        "%s", (d.lichess ? trf("Made in {:.1f} s; only what the Lichess source asks leaves the machine.", seconds)
                         : trf("Made of your databases alone, in {:.1f} s; nothing leaves the machine.", seconds))
                  .c_str());
    ImGui::PopTextWrapPos();
}

void App::drawDossierOpenings() {
    auto& d           = *dossier_;
    const auto& r     = *d.report;
    const auto& b     = r.board;
    const auto& names = dossierOpenings();
    const auto side   = [&](bool white) {
        const auto& lines = white ? b.lines_white : b.lines_black;
        const auto n      = white ? b.white.games : b.black.games;
        ImGui::TextUnformatted(
            white ? trn("With White · {} game", "With White · {} games", n, ui::exactCount(n)).c_str()
                  : trn("With Black · {} game", "With Black · {} games", n, ui::exactCount(n)).c_str());
        constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable(white ? "##lines_white" : "##lines_black", 5, flags)) {
            ImGui::TableSetupColumn(TR("Line"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(TR("Games"), ImGuiTableColumnFlags_WidthFixed, px(50));
            ImGui::TableSetupColumn("+ = −", ImGuiTableColumnFlags_WidthFixed, px(64));
            ImGui::TableSetupColumn(TR("Score"), ImGuiTableColumnFlags_WidthFixed, px(48));
            ImGui::TableSetupColumn(TR("Last"), ImGuiTableColumnFlags_WidthFixed, px(44));
            ImGui::TableHeadersRow();
            for (std::size_t i = 0; i < lines.size() && i < 12; ++i) {
                const auto& l = lines[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(i));
                const auto& moves = l.opening ? names[*l.opening].moves : std::vector<std::string>{};
                const float h     = ImGui::GetTextLineHeightWithSpacing() * 2;
                if (ImGui::Selectable("##line", false, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, h)) &&
                    !moves.empty())
                    openDossierLine(moves, white);
                ImGui::PopID();
                ImGui::SameLine(0, 0);
                ImGui::BeginGroup();
                cellText(fmt::format("{} {}", l.eco, l.opening ? names[*l.opening].name : std::string()));
                cellText(movesText(moves), true);
                ImGui::EndGroup();
                ImGui::TableNextColumn();
                ImGui::Text("%u", l.score.games);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(fmt::format("{} {} {}", l.score.won, l.score.drawn, l.score.lost).c_str());
                ImGui::TableNextColumn();
                if (l.score.decided() > 0) ImGui::Text("%d %%", percent(l.score.share()));
                ImGui::TableNextColumn();
                if (l.last) ImGui::TextDisabled("%u", l.last >> 9);
            }
            ImGui::EndTable();
        }
        if (lines.size() > 12) {
            const auto rest = lines.size() - 12;
            ImGui::TextDisabled("%s", trn("{} more line", "{} more lines", rest, rest).c_str());
        }
    };
    if (ImGui::BeginTable("##dossier_sides", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        side(true);
        ImGui::TableNextColumn();
        side(false);
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, px(6)));
    drawDossierGaps(r.gaps, TR("The lines they play three times or more, in your databases, that your repertoires "
                               "leave without an answer."));
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s",
                        TR("A click on a line opens it on the Board page, the Tree tab on their games alone, from "
                           "their side."));
    ImGui::PopTextWrapPos();
}

void App::drawDossierGaps(const std::vector<DossierGap>& gaps, const char* how) {
    ui::beginCard("dossier_gaps");
    ImGui::PushTextWrapPos();
    if (repertoires().entries().empty()) {
        ImGui::TextUnformatted(TR("Your repertoires against them"));
        ImGui::TextDisabled("%s", TR("No repertoire yet: the Tree tab's stars make one, measured on the Openings "
                                     "page."));
    } else if (gaps.empty()) {
        ImGui::TextUnformatted(TR("Your repertoires against them"));
        ImGui::TextDisabled("%s", TR("Your repertoires answer the lines they play often."));
    } else {
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
            trn("Your repertoires against them: {} gap, the most frequent first",
                "Your repertoires against them: {} gaps, the most frequent first", gaps.size(), gaps.size())
                .c_str());
        for (std::size_t i = 0; i < gaps.size(); ++i) {
            const auto& g = gaps[i];
            auto line     = g.gap.line;
            if (!g.gap.move.empty()) line.push_back(g.gap.move);
            if (ImGui::SmallButton(fmt::format("{}##dossier_gap{}", movesText(line), i).c_str())) {
                openDossierLine(line, !g.white);
                ImGui::PopTextWrapPos();
                ui::endCard();
                return;
            }
            ImGui::SameLine();
            ImGui::TextDisabled(
                "%s",
                trn("{} · {} game", "{} · {} games", g.gap.games, g.repertoire, ui::exactCount(g.gap.games)).c_str());
        }
    }
    ImGui::TextDisabled("%s", how);
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void App::drawDossierRating() {
    auto& d             = *dossier_;
    const auto& r       = *d.report;
    const auto& b       = r.board;
    const auto& dossier = *r.dossier;
    if (ImGui::BeginTable("##dossier_rating", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ui::beginCard("dossier_years");
        ImGui::TextUnformatted(TR("Their rating, from the Elo of their games"));
        if (b.years.size() < 3) {
            ImGui::TextDisabled("%s", TR("Fewer than three years of rated games: no curve."));
        } else {
            // The years as bars: their rating above, the year below; the last in the accent colour.
            int low = b.years.front().elo, high = low;
            for (const auto& y : b.years) {
                low  = std::min(low, y.elo);
                high = std::max(high, y.elo);
            }
            const float height = px(110);
            const auto shown   = std::min<std::size_t>(b.years.size(), 16);
            const auto first   = b.years.size() - shown;
            const float width  = ImGui::GetContentRegionAvail().x;
            const float cell   = width / static_cast<float>(shown);
            const ImVec2 top   = ImGui::GetCursorScreenPos();
            auto* dl           = ImGui::GetWindowDrawList();
            const float line   = ImGui::GetTextLineHeight();
            for (std::size_t i = 0; i < shown; ++i) {
                const auto& y    = b.years[first + i];
                const float bar  = std::min(px(34), cell * 0.7f);
                const float x    = top.x + cell * static_cast<float>(i) + (cell - bar) * 0.5f;
                const float k    = high > low ? static_cast<float>(y.elo - low) / static_cast<float>(high - low) : 1.0f;
                const float h    = px(14) + k * (height - px(14) - line * 2);
                const float base = top.y + height - line;
                const bool last  = i + 1 == shown;
                dl->AddRectFilled(ImVec2(x, base - h), ImVec2(x + bar, base),
                                  last ? ui::ACCENT : (ui::ACCENT & 0x00ffffff) | 0x80000000, px(3),
                                  ImDrawFlags_RoundCornersTop);
                const auto elo  = fmt::format("{}", y.elo);
                const auto year = fmt::format("{}", y.year);
                const float ew = ImGui::CalcTextSize(elo.c_str()).x, yw = ImGui::CalcTextSize(year.c_str()).x;
                if (ew <= cell)
                    dl->AddText(ImVec2(x + bar * 0.5f - ew * 0.5f, base - h - line),
                                ImGui::GetColorU32(ImGuiCol_TextDisabled), elo.c_str());
                if (yw <= cell)
                    dl->AddText(ImVec2(x + bar * 0.5f - yw * 0.5f, base + 1), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                year.c_str());
            }
            ImGui::Dummy(ImVec2(width, height + px(4)));
        }
        ImGui::PushTextWrapPos();
        std::vector<std::string> said;
        if (b.highest) {
            const auto month = b.highest_date >> 5 & 15;
            const auto when  = month ? fide::monthText(fmt::format("{}-{:02}", b.highest_date >> 9, month))
                                     : fmt::format("{}", b.highest_date >> 9);
            said.push_back(trf("The highest: {} in {}.", b.highest, when));
        }
        if (r.identity && r.identity->standard)
            said.push_back(trf("FIDE's list of {} says {}.", fide::monthText(r.identity->list), r.identity->standard));
        using study::Speed;
        const auto speed     = [&](Speed s) { return b.speeds[static_cast<std::size_t>(s)]; };
        const auto classical = speed(Speed::Classical) + speed(Speed::Unknown) + speed(Speed::Correspondence);
        const auto rapid = speed(Speed::Rapid), blitz = speed(Speed::Blitz) + speed(Speed::Bullet);
        std::vector<std::string> games;
        if (classical)
            games.push_back(trn("{} classical game", "{} classical games", classical, ui::exactCount(classical)));
        if (rapid) games.push_back(trn("{} rapid game", "{} rapid games", rapid, ui::exactCount(rapid)));
        if (blitz) games.push_back(trn("{} blitz game", "{} blitz games", blitz, ui::exactCount(blitz)));
        if (!games.empty()) said.push_back(trf("Their games: {}.", listed(games)));
        std::string text;
        for (const auto& s : said) text += (text.empty() ? "" : " ") + s;
        ImGui::TextDisabled("%s", text.c_str());
        ImGui::PopTextWrapPos();
        ui::endCard();

        ImGui::TableNextColumn();
        ui::beginCard("dossier_structures");
        ImGui::TextUnformatted(TR("Their structures, from the assistant's facts at move 20"));
        if (b.games < 5) {
            ImGui::TextDisabled("%s", TR("Fewer than five games: no structures."));
        } else if (b.structures.empty()) {
            ImGui::TextDisabled("%s", TR("No named structure at move 20 in their games."));
        } else if (ImGui::BeginTable("##structures", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn(TR("Structure"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(TR("Games"), ImGuiTableColumnFlags_WidthFixed, px(50));
            ImGui::TableSetupColumn(TR("Score"), ImGuiTableColumnFlags_WidthFixed, px(48));
            ImGui::TableHeadersRow();
            for (std::size_t i = 0; i < b.structures.size() && i < 8; ++i) {
                const auto& s = b.structures[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(i));
                bool clicked = ImGui::Selectable("##structure", false, ImGuiSelectableFlags_SpanAllColumns);
                ImGui::PopID();
                ImGui::SameLine(0, 0);
                cellText(structureLabel(s));
                ImGui::TableNextColumn();
                ImGui::Text("%u", s.score.games);
                ImGui::TableNextColumn();
                if (s.score.decided() > 0) ImGui::Text("%d %%", percent(s.score.share()));
                if (clicked && !s.games.empty()) {
                    // Those games in the list of the database that has most of them, the Structure search starting
                    // from the pawns of the latest at move 20.
                    std::map<uint32_t, std::vector<study::GameId>> by;
                    for (const auto gi : s.games) by[dossier.games[gi].source].push_back(dossier.games[gi].id);
                    auto most = by.begin();
                    for (auto it = by.begin(); it != by.end(); ++it)
                        if (it->second.size() > most->second.size()) most = it;
                    // The latest of them in that database (the structure's games come the latest first).
                    const auto* latest = &dossier.games[s.games.front()];
                    for (const auto gi : s.games)
                        if (dossier.games[gi].source == most->first) {
                            latest = &dossier.games[gi];
                            break;
                        }
                    auto& st = db_search_.structure;
                    st.pawns = latest->pawns;
                    st.taken = true;
                    st.set   = false;
                    st.said  = trf("at move 20 of game {}", ui::exactCount(latest->id + 1));
                    dossierInList(most->first, most->second,
                                  trf("the games of {} in this structure: {}", d.subject.name, structureLabel(s)));
                    db_search_open_ = true;
                    search_select_  = TabStructure;
                    search_tab_     = TabStructure;
                    break;
                }
            }
            ImGui::EndTable();
            ImGui::PushTextWrapPos();
            ImGui::TextDisabled("%s",
                                TR("A click: those games in the list, the Structure search starting from there."));
            ImGui::PopTextWrapPos();
        }
        ui::endCard();
        ImGui::EndTable();
    }

    // Their games the user reviewed: accuracy by phase, where their clear mistakes fall.
    const auto& v = r.reviews;
    if (v.games > 0) {
        ImGui::Dummy(ImVec2(0, px(4)));
        ui::beginCard("dossier_reviews");
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(trn("Their games reviewed: {}, by you (Alt+G)",
                                   "Their games reviewed: {}, by you (Alt+G)", v.games, v.games)
                                   .c_str());
        const auto acc = [](const std::optional<double>& a) {
            return a ? fmt::format("{} %", static_cast<int>(std::lround(*a))) : std::string("–");
        };
        std::string text = trf("Accuracy: opening {}, middlegame {}, endgame {}.", acc(v.accuracy[0]),
                               acc(v.accuracy[1]), acc(v.accuracy[2]));
        const auto total = v.mistakes[0] + v.mistakes[1] + v.mistakes[2];
        if (total > 0) {
            const auto most =
                static_cast<std::size_t>(std::max_element(v.mistakes.begin(), v.mistakes.end()) - v.mistakes.begin());
            const char* where[] = {TR("Their clear mistakes fall mostly in the opening ({} of {})."),
                                   TR("Their clear mistakes fall mostly in the middlegame ({} of {})."),
                                   TR("Their clear mistakes fall mostly in the endgame ({} of {}).")};
            text += " " + fmt::format(fmt::runtime(where[most]), v.mistakes[most], total);
        } else {
            text += std::string(" ") + TR("No clear mistake of theirs in these games.");
        }
        ImGui::TextUnformatted(text.c_str());
        ImGui::TextDisabled("%s", TR("Shown only when games of theirs were reviewed; nothing is analysed for the "
                                     "dossier."));
        ImGui::PopTextWrapPos();
        ui::endCard();
    }
}

void App::drawDossierOnline() {
    auto& d       = *dossier_;
    const auto& r = *d.report;
    const auto& b = r.board;
    const auto& o = r.online;
    const auto* p = prep_ && lower(prep_->opponent) == lower(d.subject.lichess) ? &*prep_ : nullptr;
    // What they play on the board and online, side by side.
    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##dossier_compare", 3, flags)) {
        ImGui::TableSetupColumn(TR("What they play"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn(TR("On the board (your databases)"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn(TR("Online (Lichess)"), ImGuiTableColumnFlags_WidthStretch, 2.4f);
        ImGui::TableHeadersRow();
        const auto first = [](const DossierPlay& play) {
            return play.with_white.empty()
                       ? std::string("–")
                       : fmt::format("1.{} · {} %", play.with_white[0].san, percent(play.with_white[0].share));
        };
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", TR("With White"));
        ImGui::TableNextColumn();
        cellText(first(r.board_play));
        ImGui::TableNextColumn();
        cellText(first(r.online_play));
        std::vector<std::string> moves;
        for (const auto* play : {&r.board_play, &r.online_play})
            for (const auto& a : play->with_black)
                if (std::find(moves.begin(), moves.end(), a.san) == moves.end()) moves.push_back(a.san);
        for (const auto& san : moves) {
            const auto families = [&](const DossierPlay& play, std::size_t most) {
                for (const auto& a : play.with_black) {
                    if (a.san != san || a.games == 0) continue;
                    std::vector<std::string> items;
                    for (std::size_t i = 0; i < a.families.size() && i < most; ++i)
                        items.push_back(trf(
                            "{} · {} %", a.families[i].family,
                            percent(static_cast<double>(a.families[i].score.games) / static_cast<double>(a.games))));
                    return listed(items);
                }
                return std::string("–");
            };
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", trf("Against 1.{}", san).c_str());
            ImGui::TableNextColumn();
            cellText(families(r.board_play, 1));
            ImGui::TableNextColumn();
            cellText(families(r.online_play, 2));
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", TR("Games"));
        ImGui::TableNextColumn();
        cellText(b.games == 0 ? std::string("–")
                 : (b.first >> 9) == (b.last >> 9)
                     ? trn("{} game, in {}", "{} games, in {}", b.games, ui::exactCount(b.games), b.first >> 9)
                     : trn("{} game, from {} to {}", "{} games, from {} to {}", b.games, ui::exactCount(b.games),
                           b.first >> 9, b.last >> 9));
        ImGui::TableNextColumn();
        {
            using study::Speed;
            std::vector<std::string> each;
            const auto add = [&](Speed s, const char* format) {
                if (const auto n = o.speeds[static_cast<std::size_t>(s)]; n > 0)
                    each.push_back(fmt::format(fmt::runtime(format), ui::exactCount(n)));
            };
            add(Speed::Bullet, TR("bullet {}"));
            add(Speed::Blitz, TR("blitz {}"));
            add(Speed::Rapid, TR("rapid {}"));
            add(Speed::Classical, TR("classical {}"));
            add(Speed::Correspondence, TR("correspondence {}"));
            cellText(each.empty() ? trn("{} game in a year", "{} games in a year", o.games, ui::exactCount(o.games))
                                  : trn("{} game in a year ({})", "{} games in a year ({})", o.games,
                                        ui::exactCount(o.games), listed(each)));
        }
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    // Their level online, between you two, the repertoires over both sources.
    if (ImGui::BeginTable("##dossier_online_cards", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        std::string level, from = TR("their public profile");
        if (p && p->profile) {
            std::vector<std::string> each;
            for (const auto& sp : p->profile->speeds) {
                // Each speed in the interface's language, a provisional rating with "?".
                const char* format = sp.key == "bullet"      ? TR("bullet {}")
                                     : sp.key == "blitz"     ? TR("blitz {}")
                                     : sp.key == "rapid"     ? TR("rapid {}")
                                     : sp.key == "classical" ? TR("classical {}")
                                                             : nullptr;
                if (format && sp.games > 0)
                    each.push_back(
                        fmt::format(fmt::runtime(format), fmt::format("{}{}", sp.rating, sp.provisional ? "?" : "")));
            }
            for (const auto& e : each) level += (level.empty() ? "" : " · ") + e;
        } else if (p && p->profile_error.failed()) {
            from = lichess::describeAlone(p->profile_error);
        }
        card("dossier_level", TR("Their level online"), level, from);
        ImGui::TableNextColumn();
        card("dossier_between", TR("Between you two"), trf("{} on the board, {} online", b.user.games, o.user.games),
             TR("Settings › Your names, and your linked account"));
        ImGui::TableNextColumn();
        card("dossier_both", TR("Your repertoires against them"),
             trn("{} gap", "{} gaps", r.gaps_both.size(), r.gaps_both.size()), TR("both sources together"));
        ImGui::EndTable();
    }
    drawDossierGaps(r.gaps_both, TR("The lines they play three times or more, on the board and online, that your "
                                    "repertoires leave without an answer."));
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", TR("Asked of Lichess: their name with a year of blitz, rapid and classical games, and "
                                 "their public profile. Nothing of yours."));
    ImGui::PopTextWrapPos();
}

// ---------------------------------------------------------------------------------------------------------------------
// Printed, and as a page of HTML (#93)

publish::Document dossierDocument(const DossierReport& r, const std::string& name,
                                  const std::vector<std::string>& sources, bool user_names, bool letter,
                                  const publish::DocumentLook& look) {
    using publish::Block;
    using publish::Cell;
    publish::Document d;
    d.title   = trf("Dossier: {}", name);
    d.header  = d.title;
    d.letter  = letter;
    d.pieces  = look.pieces;
    d.notice  = look.notice;
    d.colours = look.colours;
    d.lang    = look.lang;
    d.blocks.push_back(publish::title(name));
    if (const auto* id = r.identity ? &*r.identity : nullptr) {
        std::vector<std::string> parts;
        if (!id->title.empty()) parts.push_back(id->title);
        if (!id->federation.empty()) parts.push_back(id->federation);
        if (id->born) parts.push_back(trf("born {}", id->born));
        if (id->standard) parts.push_back(trf("standard {}", id->standard));
        if (id->rapid) parts.push_back(trf("rapid {}", id->rapid));
        if (id->blitz) parts.push_back(trf("blitz {}", id->blitz));
        if (!id->list.empty()) parts.push_back(trf("FIDE's list of {}", fide::monthText(id->list)));
        std::string line;
        for (const auto& p : parts) line = line.empty() ? p : trf("{} · {}", line, p);
        if (!line.empty()) d.blocks.push_back(publish::subtitle(line));
    }
    const auto& b = r.board;
    if (!r.dossier || b.games == 0) {
        d.blocks.push_back(publish::paragraph(TR("No game of theirs in your databases.")));
        return d;
    }
    const auto& dossier = *r.dossier;
    const auto& names   = dossierOpenings();
    {
        std::vector<std::string> made;
        for (std::size_t s = 0; s < b.sources.size() && s < sources.size(); ++s) {
            const auto& src = b.sources[s];
            if (src.games == 0) continue;
            const auto when = years(src.first_year, src.last_year);
            const auto n    = trn("{} game", "{} games", src.games, ui::exactCount(src.games));
            made.push_back(when.empty() ? trf("{} · {}", sources[s], n) : trf("{} · {} · {}", sources[s], n, when));
        }
        d.blocks.push_back(publish::note(trf("Made of: {}", listed(made))));
    }
    // The six cards.
    const auto cards = dossierCards(r, user_names);
    d.blocks.push_back(publish::heading(TR("Overview")));
    for (std::size_t i = 0; i < 6; ++i) {
        Block p;
        p.spans.emplace_back(cards.titles[i] + " · ", publish::FontId::Regular, publish::MUTED_COLOUR);
        p.spans.emplace_back(cards.big[i].empty() ? std::string("–") : cards.big[i], publish::FontId::SemiBold);
        if (!cards.text[i].empty())
            p.spans.emplace_back(" " + cards.text[i], publish::FontId::Regular, publish::MUTED_COLOUR);
        d.blocks.push_back(std::move(p));
    }
    // Their last games.
    {
        Block t;
        t.kind        = Block::Kind::Table;
        t.table.head  = {TR("Their last games"), TR("Event"), TR("Result"), TR("Opening")};
        t.table.align = {0, 0, 2, 0};
        int shown     = 0;
        for (std::size_t i = 0; i < dossier.games.size() && shown < 12; ++i) {
            const auto& g = dossier.games[i];
            if (g.online) continue;
            ++shown;
            const auto& snap = *dossier.sources[g.source].snapshot;
            const auto row   = snap.row(g.id);
            const auto ids   = snap.rowIds(g.id);
            const std::string event(row.event), round(row.round);
            std::string opening;
            if (g.opening >= 0)
                opening = fmt::format("{} {}", names[static_cast<std::size_t>(g.opening)].eco,
                                      names[static_cast<std::size_t>(g.opening)].name);
            else if (row.eco)
                opening = study::ecoText(row.eco);
            t.table.rows.push_back(
                {{fmt::format("{}  {} – {}", g.numbers.date ? study::dateText(g.numbers.date) : std::string("?"),
                              snap.shownName(ids.white), snap.shownName(ids.black))},
                 {round.empty() || round == "?" || round == "-" ? event : trf("{}, round {}", event, round)},
                 {row.result == '1'   ? "1-0"
                  : row.result == '0' ? "0-1"
                  : row.result == '=' ? "½-½"
                                      : "*"},
                 {opening, publish::FontId::Regular, publish::MUTED_COLOUR}});
        }
        d.blocks.push_back(std::move(t));
    }
    // Their lines with each colour, the first of each to replay on the page.
    for (const bool white : {true, false}) {
        const auto& lines = white ? b.lines_white : b.lines_black;
        const auto n      = white ? b.white.games : b.black.games;
        d.blocks.push_back(
            publish::heading(white ? trn("With White · {} game", "With White · {} games", n, ui::exactCount(n))
                                   : trn("With Black · {} game", "With Black · {} games", n, ui::exactCount(n))));
        Block t;
        t.kind        = Block::Kind::Table;
        t.table.head  = {TR("Line"), TR("Games"), "+ = −", TR("Score"), TR("Last")};
        t.table.align = {0, 1, 2, 1, 1};
        for (std::size_t i = 0; i < lines.size() && i < 15; ++i) {
            const auto& l = lines[i];
            t.table.rows.push_back(
                {{fmt::format("{} {}", l.eco, l.opening ? names[*l.opening].name : std::string())},
                 {ui::exactCount(l.score.games)},
                 {fmt::format("{} {} {}", l.score.won, l.score.drawn, l.score.lost)},
                 {l.score.decided() > 0 ? fmt::format("{} %", percent(l.score.share())) : std::string()},
                 {l.last ? fmt::format("{}", l.last >> 9) : std::string(), publish::FontId::Regular,
                  publish::MUTED_COLOUR}});
        }
        d.blocks.push_back(std::move(t));
        for (std::size_t i = 0; i < lines.size() && i < 6; ++i) {
            const auto& l = lines[i];
            if (!l.opening) continue;
            const auto& moves = names[*l.opening].moves;
            const auto game   = publish::lineGame(moves);
            if (!game || moves.empty()) continue;
            Block p;
            p.spans.emplace_back(fmt::format("{} {}: ", l.eco, names[*l.opening].name), publish::FontId::SemiBold);
            for (auto& s : publish::lineSpans(moves, static_cast<int>(d.games.size()))) p.spans.push_back(std::move(s));
            d.games.push_back(*game);
            d.blocks.push_back(std::move(p));
        }
    }
    // Their rating, year by year.
    if (b.years.size() >= 2) {
        d.blocks.push_back(publish::heading(TR("Their rating")));
        int low = b.years.front().elo, high = low;
        for (const auto& y : b.years) {
            low  = std::min(low, y.elo);
            high = std::max(high, y.elo);
        }
        Block bars;
        bars.kind = Block::Kind::Bars;
        for (const auto& y : b.years)
            bars.bars.push_back(
                {fmt::format("{}", y.year), fmt::format("{}", y.elo),
                 high > low ? 0.25f + 0.75f * static_cast<float>(y.elo - low) / static_cast<float>(high - low) : 1.0f});
        d.blocks.push_back(std::move(bars));
    }
    // Their structures.
    if (b.games >= 5 && !b.structures.empty()) {
        d.blocks.push_back(publish::heading(TR("Their structures, from the assistant's facts at move 20")));
        Block t;
        t.kind        = Block::Kind::Table;
        t.table.head  = {TR("Structure"), TR("Games"), TR("Score")};
        t.table.align = {0, 1, 1};
        for (std::size_t i = 0; i < b.structures.size() && i < 8; ++i) {
            const auto& st = b.structures[i];
            t.table.rows.push_back(
                {{structureLabel(st)},
                 {ui::exactCount(st.score.games)},
                 {st.score.decided() > 0 ? fmt::format("{} %", percent(st.score.share())) : std::string()}});
        }
        d.blocks.push_back(std::move(t));
    }
    // The gaps of the user's repertoires against them.
    if (!r.gaps.empty()) {
        d.blocks.push_back(publish::heading(TR("The gaps of your repertoires against them")));
        Block t;
        t.kind        = Block::Kind::Table;
        t.table.head  = {TR("Repertoire"), TR("The line"), TR("Missing"), TR("Games")};
        t.table.align = {0, 0, 0, 1};
        for (std::size_t i = 0; i < r.gaps.size() && i < 20; ++i) {
            const auto& g = r.gaps[i];
            t.table.rows.push_back({{g.repertoire},
                                    {movesText(g.gap.line)},
                                    {g.gap.move.empty() ? std::string(TR("no move of yours here")) : "→ " + g.gap.move},
                                    {ui::exactCount(g.gap.games)}});
        }
        d.blocks.push_back(std::move(t));
    }
    return d;
}

}  // namespace fcg
