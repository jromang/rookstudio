// A tournament's crosstable and standings (#92, mockup M13, board 5). On the Databases page, "Tournaments" beside
// "Games" above the list: the database's tournaments (the games of one event, its site and year), their system, a
// click on it changing it, their players, rounds, mean Elo and games; a click on one opens its crosstable in place of
// the list, Esc going back. A round robin's crosstable has a cell for each opponent, a Swiss's one for each round
// ("4b½": against no. 4, with Black, a draw); then the points, FIDE's tie-breaks (a click on a heading changes them,
// kept for the tournament), the performance and the rating change. What is wrong with the data is said above it, one
// click each. A followed broadcast's crosstable takes the place of its mosaic (view_broadcast_mosaic.cpp).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>

#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>  // SetKeyOwner

#include <study/background.hpp>
#include <study/duplicates.hpp>
#include <study/import.hpp>
#include <study/players.hpp>
#include <study/query.hpp>

#include "app.hpp"
#include "fide_rating_list.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "publish_reports.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

namespace fs = std::filesystem;

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

const ImVec4 LIVE_RED(0.82f, 0.29f, 0.23f, 1.0f);

// A text in a table's cell: wider than the cell, it ends in "…", whole under the mouse.
void cellText(const std::string& text, std::optional<ImU32> colour = std::nullopt, bool muted = false) {
    const float room  = ImGui::GetContentRegionAvail().x;
    std::string shown = text;
    if (room > 0 && ImGui::CalcTextSize(text.c_str()).x > room) {
        while (!shown.empty() && ImGui::CalcTextSize((shown + "…").c_str()).x > room) {
            while (!shown.empty() && (static_cast<unsigned char>(shown.back()) & 0xC0) == 0x80) shown.pop_back();
            if (!shown.empty()) shown.pop_back();
        }
        shown += "…";
    }
    if (colour)
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(*colour), "%s", shown.c_str());
    else if (muted)
        ImGui::TextDisabled("%s", shown.c_str());
    else
        ImGui::TextUnformatted(shown.c_str());
    if (shown != text && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem))
        ImGui::SetTooltip("%s", text.c_str());
}

// Two segments, the mockup's switch: true when the choice changed.
bool twoSegments(const char* id, const char* first, const char* second, bool& on_second) {
    ImGui::PushID(id);
    bool changed = false;
    const ImVec2 pad(px(10), px(3));
    const auto start = ImGui::GetCursorScreenPos();
    const float w1   = ImGui::CalcTextSize(first).x + pad.x * 2;
    const float w2   = ImGui::CalcTextSize(second).x + pad.x * 2;
    const float h    = ImGui::GetTextLineHeight() + pad.y * 2;
    auto* dl         = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(start.x - px(2), start.y - px(2)), ImVec2(start.x + w1 + w2 + px(2), start.y + h + px(2)),
                      ImGui::GetColorU32(ImGuiCol_FrameBg), h * 0.4f);
    for (int k = 0; k < 2; ++k) {
        const char* label = k == 0 ? first : second;
        const float w     = k == 0 ? w1 : w2;
        const bool on     = (k == 1) == on_second;
        const auto pos    = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton(label, ImVec2(w, h)) && !on) {
            on_second = k == 1;
            changed   = true;
        }
        const bool hovered = ImGui::IsItemHovered();
        if (on || hovered)
            dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h),
                              on ? ui::CARD_BG : ImGui::GetColorU32(ImGuiCol_FrameBgHovered), h * 0.35f);
        dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y),
                    on ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
        if (k == 0) ImGui::SameLine(0, 0);
    }
    ImGui::PopID();
    return changed;
}

// "October 2026", or "January 2026 to June 2026".
std::string monthsText(uint32_t first, uint32_t last) {
    const auto key = [](uint32_t date) {
        const auto month = (date >> 5) & 15;
        return month ? fmt::format("{}-{:02d}", date >> 9, month) : std::string();
    };
    const auto a = key(first), b = key(last);
    if (a.empty()) return first ? std::to_string(first >> 9) : std::string();
    if (b.empty() || a == b) return fide::monthText(a);
    return trf("{} to {}", fide::monthText(a), fide::monthText(b));
}

// What a tournament's line says after its event: its site and its months.
std::string whereText(const study::Snapshot& snap, const study::TournamentEntry& e) {
    std::string where(snap.name(e.site));
    const auto months = monthsText(e.first_date, e.last_date);
    if (!months.empty()) where = where.empty() ? months : trf("{}, {}", where, months);
    return where;
}

// A list said as the interface's language joins its items ("a, b, c").
std::string listed(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& item : items) out = out.empty() ? item : trf("{}, {}", out, item);
    return out;
}

bool resultKnown(char r) { return r == '1' || r == '0' || r == '='; }

std::string resultText(char r) { return r == '1' ? "1-0" : r == '0' ? "0-1" : r == '=' ? "½-½" : "*"; }

// A line of what is wrong with the data: an icon, a title in its tone, its explanation.
void issueLine(ui::Icon icon, ImU32 tone, const std::string& title, const std::string& text) {
    ui::inlineIcon(icon, tone);
    ImGui::SameLine();
    ImGui::PushTextWrapPos();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(tone), "%s", title.c_str());
    if (!text.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", text.c_str());
    }
    ImGui::PopTextWrapPos();
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// The lists of tournaments

bool App::drawListSwitch() {
    bool tournaments = db_tournaments_;
    if (!twoSegments("##list_switch", TR("Games"), TR("Tournaments"), tournaments)) return false;
    db_tournaments_ = tournaments;
    if (!tournaments) leaveCrosstable();
    return true;
}

CrosstableStore& App::crosstableStore() {
    if (!crosstable_store_) crosstable_store_ = std::make_unique<CrosstableStore>(storage_.root());
    return *crosstable_store_;
}

const App::TournamentsKept* App::tournamentsOf(std::size_t database, bool start) {
    auto& s = shelf();
    if (database >= s.entries().size()) return nullptr;
    const auto db = s.open(database);
    if (!db) return nullptr;
    const auto snapshot = db->snapshot();
    const auto& folder  = s.entries()[database].folder;
    const auto it       = tournament_lists_.find(folder);
    const bool current  = it != tournament_lists_.end() && it->second.generation == snapshot->generation();
    if (!current && start) {
        const auto* job    = dbTournamentsJob();
        const bool running = job && job->folder == folder && job->generation == snapshot->generation();
        if (!running) {
            if (!job || job->folder == folder) {
                // The list of an older snapshot of the database cancelled and parked: it stops within a moment, out of
                // sight (#105).
                auto& made      = db_tournaments_job_.start([snapshot](TaskState<DbTournamentsState>& list) {
                    study::enterBackground();  // a tournament beside it loses no time
                    return study::listTournaments(snapshot, list.progress, list.cancel);
                });
                made.folder     = folder;
                made.generation = snapshot->generation();
                made.snapshot   = snapshot;
                made.started    = std::chrono::steady_clock::now();
            } else if (std::find(tournaments_wanted_.begin(), tournaments_wanted_.end(), folder) ==
                       tournaments_wanted_.end()) {
                tournaments_wanted_.push_back(folder);
            }
        }
    }
    // An older list stays while the new one is made: its snapshot with it.
    return it != tournament_lists_.end() ? &it->second : nullptr;
}

void App::pollTournaments() {
    if (db_tournaments_job_.ready()) {
        auto& made = db_tournaments_job_.state();
        auto list  = db_tournaments_job_.take();
        if (!list.cancelled) {
            TournamentsKept kept;
            kept.generation                = made.generation;
            kept.snapshot                  = made.snapshot;
            kept.list                      = std::make_shared<const study::TournamentList>(std::move(list));
            tournament_lists_[made.folder] = std::move(kept);
        }
        made.snapshot.reset();  // held by the list kept, if any
    }
    // A tournament asked for at launch (--focus crosstable=WORDS), once its database's list is made.
    if (crosstable_request_ && db_selected_ >= 0 && db_selected_ < static_cast<int>(shelf().entries().size())) {
        const auto database = static_cast<std::size_t>(db_selected_);
        if (const auto* kept = tournamentsOf(database, true)) {
            const auto words = study::foldText(crosstable_request_->first);
            for (const auto& e : kept->list->tournaments)
                if (study::foldText(tournamentLabel(*kept->snapshot, e)).find(words) != std::string::npos) {
                    const auto key = e.key;
                    crosstable_request_.reset();
                    openCrosstable(database, key);
                    break;
                }
            crosstable_request_.reset();
        }
    }
    // The next list asked for.
    if (db_tournaments_job_.idle() && !tournaments_wanted_.empty()) {
        const auto folder = tournaments_wanted_.front();
        tournaments_wanted_.erase(tournaments_wanted_.begin());
        auto& s = shelf();
        for (std::size_t i = 0; i < s.entries().size(); ++i)
            if (s.entries()[i].folder == folder) (void)tournamentsOf(i, true);
    }
    pollCrosstable();
}

void App::drawTournamentList(std::size_t database) {
    auto& s           = shelf();
    const auto& entry = s.entries()[database];
    const ui::TopicRegion topic(guide::topics::Crosstables);
    const bool popup  = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup);
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !popup && !ImGui::GetIO().WantTextInput;
    if (ui::iconButton("tournaments_back", ui::Icon::Back, TR("Databases")) || escape) {
        db_list_ = false;
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("›");
    ImGui::SameLine();
    ImGui::TextUnformatted(entry.name.c_str());
    ImGui::SameLine(0, px(14));
    if (drawListSwitch()) return;
    const auto* kept   = tournamentsOf(database, true);
    const auto* job    = dbTournamentsJob();
    const bool running = job && job->folder == entry.folder;
    if (kept) {
        const auto n = kept->list->tournaments.size();
        ImGui::SameLine(0, px(12));
        const bool by_elo = tournament_sort_ < 0 || (tournament_sort_ == 4 && tournament_descending_);
        const auto count  = trn("{} tournament", "{} tournaments", n, ui::exactCount(n));
        ImGui::TextDisabled(
            "%s",
            n == 0
                ? count.c_str()
                : trf("{} · {}", count, by_elo ? TR("sorted by mean Elo") : TR("a click on a heading sorts")).c_str());
    }
    // Made in the background (R18): its progress, Cancel.
    if (running) {
        const auto total  = job->progress.total.load();
        const float share = total ? static_cast<float>(job->progress.done.load()) / static_cast<float>(total) : 0.0f;
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - job->started).count();
        ImGui::TextDisabled("%s", kept ? TR("Listing its tournaments again: the list below is the previous one.")
                                       : TR("Listing its tournaments…"));
        ImGui::ProgressBar(std::min(1.0f, share), ImVec2(-px(110), 0),
                           fmt::format("{} % · {}", static_cast<int>(share * 100), ui::formatDuration(secs)).c_str());
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"))) {
            db_tournaments_job_.cancel();
            db_tournaments_ = false;
            return;
        }
    }
    if (!kept) return;
    const auto& list = *kept->list;
    const auto& snap = *kept->snapshot;
    if (list.tournaments.empty()) {
        ImGui::Dummy(ImVec2(0, px(8)));
        ui::beginCard("tournaments_none");
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(TR("No tournament in this database."));
        ImGui::TextDisabled("%s", TR("Its games share no event whose rounds are told: online games, for instance. "
                                     "Their list is under “Games”."));
        ImGui::PopTextWrapPos();
        if (ImGui::Button(TR("Games"))) db_tournaments_ = false;
        ui::endCard();
        return;
    }

    // The filter.
    ImGui::SetNextItemWidth(std::min(px(460), ImGui::GetContentRegionAvail().x));
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_L)) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##tournament_filter", TR("Filter: event, site, year"), tournament_filter_.data(),
                             tournament_filter_.size());
    const bool list_keys = !ImGui::GetIO().WantTextInput || ImGui::IsItemActive();
    ImGui::SameLine(0, px(4));
    ImGui::TextDisabled("Ctrl+L");

    // The rows shown: filtered, sorted.
    const auto of = fmt::format("{}|{}|{}|{}|{}", entry.folder.string(), kept->generation, tournament_filter_.data(),
                                tournament_sort_, tournament_descending_);
    if (of != tournaments_shown_of_) {
        tournaments_shown_.clear();
        std::vector<std::string> words;
        {
            const auto folded = study::foldText(tournament_filter_.data());
            std::size_t at    = 0;
            while (at < folded.size()) {
                auto end = folded.find(' ', at);
                if (end == std::string::npos) end = folded.size();
                if (end > at) words.push_back(folded.substr(at, end - at));
                at = end + 1;
            }
        }
        for (std::size_t i = 0; i < list.tournaments.size(); ++i) {
            if (!words.empty()) {
                const auto& e    = list.tournaments[i];
                const auto label = study::foldText(tournamentLabel(snap, e) + " " + whereText(snap, e));
                if (!std::all_of(words.begin(), words.end(),
                                 [&](const std::string& w) { return label.find(w) != std::string::npos; }))
                    continue;
            }
            tournaments_shown_.push_back(i);
        }
        if (tournament_sort_ >= 0) {
            const auto key = [&](std::size_t i) -> double {
                const auto& e = list.tournaments[i];
                switch (tournament_sort_) {
                    case 1:
                        return static_cast<double>(e.shape.system.system) * 100 + e.shape.system.cycles;
                    case 2:
                        return e.shape.players;
                    case 3:
                        return e.shape.rounds;
                    case 4:
                        return e.mean_elo;
                    case 5:
                        return e.shape.games;
                    default:
                        return e.first_date;
                }
            };
            std::stable_sort(tournaments_shown_.begin(), tournaments_shown_.end(), [&](std::size_t a, std::size_t b) {
                if (tournament_sort_ == 0) {
                    const auto la = tournamentLabel(snap, list.tournaments[a]);
                    const auto lb = tournamentLabel(snap, list.tournaments[b]);
                    return tournament_descending_ ? lb < la : la < lb;
                }
                return tournament_descending_ ? key(a) > key(b) : key(a) < key(b);
            });
        }
        tournaments_shown_of_ = of;
        tournament_row_       = 0;
    }
    const auto& rows = tournaments_shown_;
    if (!rows.empty()) tournament_row_ = std::min(tournament_row_, rows.size() - 1);

    std::optional<std::size_t> open;
    std::optional<std::size_t> system_menu;
    constexpr auto flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                           ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
    const float help_h   = ImGui::GetTextLineHeightWithSpacing() * 2.6f;
    if (ImGui::BeginTable("##tournaments", 6, flags, ImVec2(0, -help_h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(TR("Tournament"), ImGuiTableColumnFlags_WidthStretch, 0, 0);
        ImGui::TableSetupColumn(TR("System"), ImGuiTableColumnFlags_WidthFixed, px(170), 1);
        ImGui::TableSetupColumn(TR("Players"), ImGuiTableColumnFlags_WidthFixed, px(64), 2);
        ImGui::TableSetupColumn(TR("Rounds"), ImGuiTableColumnFlags_WidthFixed, px(64), 3);
        ImGui::TableSetupColumn(TR("Mean Elo"),
                                ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending |
                                    ImGuiTableColumnFlags_DefaultSort,
                                px(76), 4);
        ImGui::TableSetupColumn(TR("Games"), ImGuiTableColumnFlags_WidthFixed, px(100), 5);
        ImGui::TableHeadersRow();
        if (auto* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
            if (specs->SpecsCount > 0) {
                tournament_sort_       = static_cast<int>(specs->Specs[0].ColumnUserID);
                tournament_descending_ = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
            }
            specs->SpecsDirty = false;
        }
        if (list_keys) {
            const auto owner = ImGui::GetID("##tournament_keys");
            for (const auto key : {ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_PageUp, ImGuiKey_PageDown,
                                   ImGuiKey_Enter, ImGuiKey_KeypadEnter})
                ImGui::SetKeyOwner(key, owner);
        }
        if (!rows.empty() && list_keys && !popup) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
                tournament_row_ = std::min(tournament_row_ + 1, rows.size() - 1);
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && tournament_row_ > 0) --tournament_row_;
            if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
                tournament_row_ = std::min(tournament_row_ + 20, rows.size() - 1);
            if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) tournament_row_ = tournament_row_ > 20 ? tournament_row_ - 20 : 0;
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
                open = rows[tournament_row_];
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        clipper.IncludeItemByIndex(static_cast<int>(tournament_row_));
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                const auto i  = rows[static_cast<std::size_t>(r)];
                const auto& e = list.tournaments[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(i));
                const bool chosen = static_cast<std::size_t>(r) == tournament_row_;
                const auto pos    = ImGui::GetCursorPos();
                if (ImGui::Selectable("##t", chosen,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                    tournament_row_ = static_cast<std::size_t>(r);
                    open            = i;
                }
                if (chosen && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow) ||
                               ImGui::IsKeyPressed(ImGuiKey_PageDown) || ImGui::IsKeyPressed(ImGuiKey_PageUp)))
                    ImGui::SetScrollHereY();
                ImGui::SameLine();
                ImGui::SetCursorPos(pos);
                // Its event as written, then its site and months.
                const auto label = std::string(snap.name(e.event));
                const auto where = whereText(snap, e);
                cellText(where.empty() ? label : trf("{} · {}", label, where));
                ImGui::TableNextColumn();
                // Its system: a click changes it.
                const auto settings = crosstableStore().get(crosstablePlace(entry.folder, e.key));
                const auto system   = settings.system.value_or(e.shape.system);
                std::string said    = systemName(system);
                if (system.system == study::TournamentSystem::Team ||
                    system.system == study::TournamentSystem::Knockout)
                    said = trf("{}: in a later version", said);
                else if (e.shape.uncertain && !settings.system)
                    said += " ?";
                const bool later = system.system == study::TournamentSystem::Team ||
                                   system.system == study::TournamentSystem::Knockout;
                if (later) {
                    cellText(said, std::nullopt, true);
                } else {
                    if (ImGui::SmallButton(fmt::format("{}##system", said).c_str())) system_menu = i;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", settings.system ? TR("Chosen by you: a click changes it.")
                                                : e.shape.uncertain
                                                    ? TR("Guessed, uncertain: a click chooses it.")
                                                    : TR("Guessed from its games: a click changes it."));
                }
                ImGui::TableNextColumn();
                ImGui::Text("%u", e.shape.players);
                ImGui::TableNextColumn();
                if (e.shape.rounds > 0) ImGui::Text("%d", e.shape.rounds);
                ImGui::TableNextColumn();
                if (e.mean_elo > 0) ImGui::Text("%d", e.mean_elo);
                ImGui::TableNextColumn();
                // A Swiss kept from Lichess (#95): its rounds without a game are its TRF file's, none missing.
                if (e.shape.expected > e.shape.games && settings.hand_from != "lichess-trf") {
                    ImGui::TextColored(
                        ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                        trf("{} of {}", ui::exactCount(e.shape.games), ui::exactCount(e.shape.expected)).c_str());
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem))
                        ImGui::SetTooltip("%s", TR("Games are missing from the database: the crosstable counts them "
                                                   "“?”."));
                } else if (e.shape.expected == e.shape.games && e.shape.expected > 0) {
                    ImGui::TextUnformatted(
                        trf("{} of {}", ui::exactCount(e.shape.games), ui::exactCount(e.shape.expected)).c_str());
                } else {
                    ImGui::TextUnformatted(ui::exactCount(e.shape.games).c_str());
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    if (rows.empty()) ImGui::TextDisabled("%s", TR("No tournament matches."));
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", TR("A tournament: the games of one event, its site and year. Its system is guessed (a "
                                 "round robin when each played each, Swiss otherwise); a click on it changes it. A "
                                 "click on a tournament, or Enter, opens its crosstable; Esc goes back."));
    ImGui::PopTextWrapPos();

    // The system chosen.
    if (system_menu) {
        ImGui::OpenPopup("##system_menu");
        tournament_row_ = static_cast<std::size_t>(std::find(rows.begin(), rows.end(), *system_menu) - rows.begin());
    }
    if (ImGui::BeginPopup("##system_menu")) {
        if (tournament_row_ < rows.size()) {
            const auto& e    = list.tournaments[rows[tournament_row_]];
            const auto place = crosstablePlace(entry.folder, e.key);
            auto settings    = crosstableStore().get(place);
            ImGui::TextDisabled("%s", TR("Played as"));
            const auto item = [&](const study::SystemChoice& choice) {
                if (ImGui::MenuItem(systemName(choice).c_str(), nullptr,
                                    settings.system.value_or(e.shape.system) == choice)) {
                    settings.system = choice;
                    crosstableStore().set(place, settings);
                }
            };
            item({study::TournamentSystem::RoundRobin, 1});
            item({study::TournamentSystem::RoundRobin, 2});
            item({study::TournamentSystem::Swiss, 1});
            if (settings.system) {
                ImGui::Separator();
                if (ImGui::MenuItem(trf("As guessed: {}", systemName(e.shape.system)).c_str())) {
                    settings.system.reset();
                    crosstableStore().set(place, settings);
                }
            }
        }
        ImGui::EndPopup();
    }
    if (open) openCrosstable(database, list.tournaments[*open].key);
}

// ---------------------------------------------------------------------------------------------------------------------
// A database tournament's crosstable

void App::openCrosstable(std::size_t database, const std::string& key) {
    auto& s = shelf();
    if (database >= s.entries().size()) return;
    const auto* kept = tournamentsOf(database, true);
    if (!kept) return;
    const auto& list = kept->list->tournaments;
    const auto it    = std::find_if(list.begin(), list.end(), [&](const auto& e) { return e.key == key; });
    if (it == list.end()) return;
    leaveCrosstable();  // the one shown before
    CrosstableView v;
    v.folder    = s.entries()[database].folder;
    v.database  = s.entries()[database].name;
    v.entry     = *it;
    v.title     = tournamentLabel(*kept->snapshot, *it);
    v.settings  = crosstableStore().get(crosstablePlace(v.folder, key));
    crosstable_ = std::move(v);
    if (crosstable_undo_folder_ != crosstable_->folder) {
        crosstable_undo_.clear();
        crosstable_undo_folder_ = crosstable_->folder;
    }
    page_           = Page::Databases;
    db_selected_    = static_cast<int>(database);
    db_list_        = true;
    db_tournaments_ = true;
    db_search_open_ = false;
    care_.reset();  // a maintenance open steps aside: the crosstable shows in place of the list
    if (dossier_) dossier_->shown = false;
    startCrosstable();
}

void App::leaveCrosstable() {
    if (!crosstable_) return;
    auto& c = *crosstable_;
    // Reading a large tournament's games, or looking for its missing ones in the other databases, takes a while.
    if (c.cancel) *c.cancel = true;
    if (c.look_cancel) *c.look_cancel = true;
    jobs_.park(std::move(c.job));
    jobs_.park(std::move(c.looking));
    crosstable_.reset();
}

void App::startCrosstable() {
    if (!crosstable_) return;
    auto& c = *crosstable_;
    auto& s = shelf();
    std::shared_ptr<study::Database> db;
    for (std::size_t i = 0; i < s.entries().size(); ++i)
        if (s.entries()[i].folder == c.folder) db = s.open(i);
    if (!db) return;
    if (c.job.valid()) {  // read again: the reading under way ends out of sight (#98)
        *c.cancel = true;
        jobs_.park(std::move(c.job));
    }
    const auto snapshot = db->snapshot();
    c.snapshot          = snapshot;
    c.generation        = snapshot->generation();
    c.cancel            = std::make_shared<std::atomic<bool>>(false);
    c.job = std::async(std::launch::async, [snapshot, ref = c.entry.ref, cancel = c.cancel, list = fideList()] {
        study::enterBackground();
        const auto ids = study::tournamentGames(*snapshot, ref, *cancel);
        return study::tournamentInput(*snapshot, ids, list.get());
    });
}

void App::pollCrosstable() {
    if (!crosstable_) return;
    auto& c = *crosstable_;
    if (c.job.valid() && c.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto input = c.job.get();
        if (!*c.cancel) {
            c.input = std::move(input);
            remakeCrosstable();
        }
    }
    // The database changed (a merge, names brought together, a game copied in): read again.
    if (!c.job.valid()) {
        auto& s = shelf();
        for (std::size_t i = 0; i < s.entries().size(); ++i)
            if (s.entries()[i].folder == c.folder)
                if (const auto db = s.open(i); db && db->snapshot()->generation() != c.generation) startCrosstable();
    }
    if (c.looking.valid() && c.looking.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto found = c.looking.get();
        if (!*c.look_cancel) c.found = std::move(found);
    }
}

void App::remakeCrosstable() {
    if (!crosstable_ || !crosstable_->input) return;
    auto& c = *crosstable_;
    study::CrossOptions options;
    options.system = c.settings.system;
    options.order  = c.settings.order;
    options.hand   = c.settings.hand;
    options.kind   = c.input->kind;
    options.year   = c.input->year;
    options.team   = c.input->team;
    c.table        = study::makeCrosstable(c.input->players, c.input->games, options);
}

void App::keepCrosstableSettings() {
    if (!crosstable_) return;
    crosstableStore().set(crosstablePlace(crosstable_->folder, crosstable_->entry.key), crosstable_->settings);
    remakeCrosstable();
}

void App::crosstableUndo() {
    if (crosstable_undo_.empty() || care_job_ || !crosstable_) return;
    auto& s = shelf();
    for (std::size_t i = 0; i < s.entries().size(); ++i) {
        if (s.entries()[i].folder != crosstable_undo_folder_) continue;
        auto undo = std::move(crosstable_undo_.back());
        crosstable_undo_.pop_back();
        startCareRestore(i, std::move(undo), true);
        return;
    }
}

// The menu of a tie-break heading.
bool App::crossTiebreakMenu(std::vector<study::Tiebreak>& order, int index, const study::Crosstable& table, bool custom,
                            bool& reset) {
    bool changed  = false;
    const int n   = static_cast<int>(order.size());
    const auto in = [&](study::Tiebreak t) { return std::find(order.begin(), order.end(), t) != order.end(); };
    if (index >= 0 && index < n) {
        ImGui::TextDisabled("%s", tiebreakName(order[static_cast<std::size_t>(index)]));
        ImGui::PushTextWrapPos(px(360));
        ImGui::TextDisabled("%s", tiebreakHelp(order[static_cast<std::size_t>(index)]));
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (index > 0 && ImGui::MenuItem(TR("Move first"))) {
            const auto t = order[static_cast<std::size_t>(index)];
            order.erase(order.begin() + index);
            order.insert(order.begin(), t);
            changed = true;
        }
        if (index > 0 && ImGui::MenuItem(TR("Move up"))) {
            std::swap(order[static_cast<std::size_t>(index)], order[static_cast<std::size_t>(index - 1)]);
            changed = true;
        }
        if (index + 1 < n && ImGui::MenuItem(TR("Move down"))) {
            std::swap(order[static_cast<std::size_t>(index)], order[static_cast<std::size_t>(index + 1)]);
            changed = true;
        }
        if (ImGui::MenuItem(TR("Remove"))) {
            order.erase(order.begin() + index);
            changed = true;
        }
        if (ImGui::BeginMenu(TR("Instead"))) {
            for (std::size_t k = 0; k < study::TIEBREAKS; ++k) {
                const auto t = static_cast<study::Tiebreak>(k);
                if (in(t)) continue;
                if (ImGui::MenuItem(tiebreakName(t))) {
                    order[static_cast<std::size_t>(index)] = t;
                    changed                                = true;
                }
            }
            ImGui::EndMenu();
        }
    } else {
        ImGui::TextDisabled("%s", TR("Add a tie-break"));
        ImGui::Separator();
        for (std::size_t k = 0; k < study::TIEBREAKS; ++k) {
            const auto t = static_cast<study::Tiebreak>(k);
            if (in(t)) continue;
            if (ImGui::MenuItem(tiebreakName(t))) {
                order.push_back(t);
                changed = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tiebreakHelp(t));
        }
    }
    if (custom) {
        ImGui::Separator();
        const auto label = table.system.system == study::TournamentSystem::Swiss ? TR("FIDE's for a Swiss")
                                                                                 : TR("FIDE's for a round robin");
        if (ImGui::MenuItem(label)) {
            reset   = true;
            changed = true;
        }
    }
    return changed;
}

void App::drawCrossTiebreakLine(const study::Crosstable& table, bool& open, int& index) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TR("Tie-breaks:"));
    if (table.tiebreaks.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("none"));
    }
    for (std::size_t k = 0; k < table.tiebreaks.size(); ++k) {
        ImGui::SameLine(0, k == 0 ? -1.0f : px(4));
        if (k > 0) {
            ImGui::TextDisabled("·");
            ImGui::SameLine(0, px(4));
        }
        const char* name = tiebreakName(table.tiebreaks[k]);
        ImGui::PushID(static_cast<int>(k));
        if (ImGui::Selectable(name, false, 0, ImVec2(ImGui::CalcTextSize(name).x, 0))) {
            open  = true;
            index = static_cast<int>(k);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n%s", tiebreakHelp(table.tiebreaks[k]),
                              TR("A click puts it first, moves, replaces or removes it."));
        ImGui::PopID();
    }
    ImGui::SameLine(0, px(8));
    if (ImGui::SmallButton("+##tiebreak_add")) {
        open  = true;
        index = static_cast<int>(table.tiebreaks.size());
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Add a tie-break"));
    ImGui::SameLine(0, px(10));
    if (table.fide_order) {
        const auto said = table.system.system == study::TournamentSystem::Swiss
                              ? (table.unrated ? TR("FIDE's for a Swiss (the average rating left out: a player has no "
                                                    "rating)")
                                               : TR("FIDE's for a Swiss"))
                              : TR("FIDE's for a round robin");
        ImGui::TextDisabled("%s", said);
    } else {
        ImGui::TextDisabled("%s", TR("yours, kept for this tournament"));
    }
}

App::CrossClicks App::drawCrossTable(const study::Crosstable& table, bool all, bool live) {
    CrossClicks clicks;
    // A Swiss's, and a team event's or a knockout's (each game in its player's row), by round.
    const bool swiss = table.system.system != study::TournamentSystem::RoundRobin;
    std::vector<const study::CrossRow*> rows;
    for (const auto& r : table.rows) {
        if (swiss && !all && rows.size() >= 10) break;
        rows.push_back(&r);
    }
    std::set<uint32_t> shown;
    for (const auto* r : rows) shown.insert(r->player);
    const bool accent = swiss && rows.size() < table.rows.size();
    const int middle  = swiss ? std::max(table.rounds, 1) : static_cast<int>(rows.size());
    const int tbs     = static_cast<int>(table.tiebreaks.size());
    const int first   = swiss ? 4 : 3;  // place, (number), player, Elo
    // A tie-break's column with quarters (a Sonneborn-Berger of a double round robin): two decimals all along it.
    std::vector<bool> decimals(static_cast<std::size_t>(tbs), false);
    for (const auto& row : table.rows)
        for (std::size_t k = 0; k < decimals.size() && k < row.tiebreaks.size(); ++k)
            if (row.states[k] != study::TiebreakState::None) {
                const double twice = row.tiebreaks[k] * 2;
                if (std::abs(twice - std::round(twice)) > 1e-9) decimals[k] = true;
            }
    // A round robin's wins, after its tie-breaks when they do not have them.
    const bool wins      = !swiss && std::find(table.tiebreaks.begin(), table.tiebreaks.end(), study::Tiebreak::Wins) ==
                                         table.tiebreaks.end();
    const int columns    = first + middle + 1 + (live ? 1 : 0) + tbs + (wins ? 1 : 0) + 2;
    constexpr auto flags = ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                           ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
    const float help_h   = ImGui::GetTextLineHeightWithSpacing() * 3.2f;
    // Its columns told by its tie-breaks: another order is another table (ImGui keeps a table's columns by their
    // place).
    std::string id = live ? "##crosstable_live" : "##crosstable";
    for (const auto t : table.tiebreaks) id += "_" + std::string(study::tiebreakCode(t));
    if (!ImGui::BeginTable(id.c_str(), columns, flags, ImVec2(0, -help_h))) return clicks;
    ImGui::TableSetupScrollFreeze(swiss ? 3 : 2, 1);
    ImGui::TableSetupColumn(TR("Pl."), ImGuiTableColumnFlags_WidthFixed);
    if (swiss) ImGui::TableSetupColumn(TR("No."), ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(TR("Player"), ImGuiTableColumnFlags_WidthFixed, px(210));
    ImGui::TableSetupColumn(TR("Elo"), ImGuiTableColumnFlags_WidthFixed);
    std::vector<std::string> middle_names;
    for (int k = 0; k < middle; ++k) {
        middle_names.push_back(swiss ? trf("R{}", k + 1) : std::to_string(k + 1));
        ImGui::TableSetupColumn(middle_names.back().c_str(), ImGuiTableColumnFlags_WidthFixed, swiss ? px(44) : px(30));
    }
    ImGui::TableSetupColumn(TR("Pts"), ImGuiTableColumnFlags_WidthFixed);
    if (live) ImGui::TableSetupColumn(TR("Played"), ImGuiTableColumnFlags_WidthFixed);
    for (int k = 0; k < tbs; ++k)
        ImGui::TableSetupColumn(tiebreakHeading(table.tiebreaks[static_cast<std::size_t>(k)]),
                                ImGuiTableColumnFlags_WidthFixed);
    if (wins) ImGui::TableSetupColumn(TR("Wins"), ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(TR("Perf."), ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(live ? TR("Elo ± live") : TR("Elo ±"), ImGuiTableColumnFlags_WidthFixed);
    // The headings: a tie-break's opens its menu.
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    for (int c = 0; c < columns; ++c) {
        if (!ImGui::TableSetColumnIndex(c)) continue;
        ImGui::TableHeader(ImGui::TableGetColumnName(c));
        const int tb = c - (first + middle + 1 + (live ? 1 : 0));
        if (tb >= 0 && tb < tbs) {
            if (ImGui::IsItemClicked()) clicks.tiebreak = tb;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n%s\n%s", tiebreakName(table.tiebreaks[static_cast<std::size_t>(tb)]),
                                  tiebreakHelp(table.tiebreaks[static_cast<std::size_t>(tb)]),
                                  TR("A click puts it first, moves, replaces or removes it."));
        } else if (ImGui::IsItemHovered()) {
            const int perf = first + middle + 1 + (live ? 1 : 0) + tbs + (wins ? 1 : 0);
            if (c == perf)
                ImGui::SetTooltip("%s", TR("The performance as FIDE computes it: the average rating of the opponents "
                                           "met over the board, plus the difference its table gives the score."));
            else if (c == perf + 1)
                ImGui::SetTooltip("%s", TR("The rating change by FIDE's formula: K × (score − expected score) over the "
                                           "games rated; a forfeit counts in the points only."));
        }
    }
    const auto cellColour = [&](const study::CrossEntry& e) -> std::optional<ImU32> {
        using K = study::CellKind;
        if (e.kind == K::Live) return ImGui::ColorConvertFloat4ToU32(LIVE_RED);
        if (e.kind == K::Missing || (e.kind == K::Absent && e.unsure)) return ui::WARN;
        if (e.kind == K::ToCome || e.kind == K::Absent || e.kind == K::Bye) return ui::MUTED;
        if (accent && e.opponent != study::NO_PLAYER && shown.count(e.opponent)) return ui::ACCENT;
        return std::nullopt;
    };
    // The results set by hand of a Swiss kept from Lichess come from its TRF file (#95).
    const bool from_trf = !live && crosstable_ && crosstable_->settings.hand_from == "lichess-trf";
    const auto entryTip = [&](const study::CrossRow& row, const study::CrossEntry& e) {
        using K          = study::CellKind;
        const auto round = e.round > 0 ? trf("Round {}", e.round) : std::string(TR("Round not known"));
        std::string who;
        if (e.opponent != study::NO_PLAYER) who = table.players[e.opponent].name;
        std::string text;
        switch (e.kind) {
            case K::Game:
                text = e.white ? trf("{}: with White against {}, {}", round, who,
                                     e.points > 0.75   ? TR("won")
                                     : e.points > 0.25 ? TR("a draw")
                                                       : TR("lost"))
                               : trf("{}: with Black against {}, {}", round, who,
                                     e.points > 0.75   ? TR("won")
                                     : e.points > 0.25 ? TR("a draw")
                                                       : TR("lost"));
                if (e.hand) text = from_trf ? trf("{} (from Lichess's TRF file)", text) : trf("{} (set by hand)", text);
                break;
            case K::Forfeit:
                text = e.points > 0.5 ? trf("{}: won by forfeit against {} (in the points, not in the performance nor "
                                            "the rating change)",
                                            round, who)
                                      : trf("{}: lost by forfeit against {}", round, who);
                break;
            case K::Bye:
                text = trf("{}: a bye, the only player unpaired (a win's points)", round);
                break;
            case K::Missing:
                text =
                    trf("{}: the game against {} is not in the database; a click sets its result by hand", round, who);
                break;
            case K::Absent:
                text = e.unsure ? trf("{}: no game in the database (a game missing whose pairing is not known, or a "
                                      "bye); a click sets its points by hand",
                                      round)
                       : e.hand && from_trf ? trf("{}: no game, its points from Lichess's TRF file", round)
                       : e.hand             ? trf("{}: no game, its points set by hand", round)
                                            : trf("{}: no game (a bye asked for, or not playing)", round);
                break;
            case K::Live:
                text = trf("{}: being played against {}; a click follows it on the board", round, who);
                break;
            case K::ToCome:
                text = trf("Not played yet against {}", who);
                break;
        }
        (void)row;
        ImGui::SetTooltip("%s", text.c_str());
    };
    // Only the rows in sight: a Swiss of a thousand players scrolls at once.
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step())
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            const auto* rowp   = rows[static_cast<std::size_t>(index)];
            const auto& row    = *rowp;
            const auto& player = table.players[row.player];
            ImGui::TableNextRow();
            ImGui::PushID(index);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", row.place_from == row.place_to
                                          ? std::to_string(row.place_from).c_str()
                                          : fmt::format("{}–{}", row.place_from, row.place_to).c_str());
            if (swiss) {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d", row.start);
            }
            ImGui::TableNextColumn();
            {
                // The name, a click opening their dossier; the federation after it.
                const std::string fed = player.federation;
                const float fed_w     = fed.empty() ? 0 : ImGui::CalcTextSize(fed.c_str()).x + px(6);
                const float room      = std::max(px(40), ImGui::GetContentRegionAvail().x - fed_w);
                std::string name      = player.name;
                if (ImGui::CalcTextSize(name.c_str()).x > room) {
                    while (!name.empty() && ImGui::CalcTextSize((name + "…").c_str()).x > room) {
                        while (!name.empty() && (static_cast<unsigned char>(name.back()) & 0xC0) == 0x80)
                            name.pop_back();
                        if (!name.empty()) name.pop_back();
                    }
                    name += "…";
                }
                if (ImGui::Selectable(fmt::format("{}##name", name).c_str(), false, 0,
                                      ImVec2(ImGui::CalcTextSize(name.c_str()).x, 0)))
                    clicks.player = row.player;
                if (ImGui::IsItemHovered()) {
                    std::string tip = player.name;
                    if (!player.title.empty()) tip = trf("{} {}", player.title, tip);
                    ImGui::SetTooltip("%s\n%s", tip.c_str(), TR("A click opens their dossier."));
                }
                if (!fed.empty()) {
                    ImGui::SameLine(0, px(6));
                    ImGui::TextDisabled("%s", fed.c_str());
                }
            }
            ImGui::TableNextColumn();
            if (player.elo > 0) ImGui::Text("%d", player.elo);
            // The cells.
            for (int k = 0; k < middle; ++k) {
                ImGui::TableNextColumn();
                std::vector<int> entries;
                if (swiss) {
                    for (int e = 0; e < static_cast<int>(row.entries.size()); ++e)
                        if (row.entries[static_cast<std::size_t>(e)].round == k + 1) entries.push_back(e);
                } else {
                    const auto other = rows[static_cast<std::size_t>(k)]->player;
                    if (other == row.player) {  // its own square, shaded
                        const auto lo = ImGui::GetCursorScreenPos();
                        ImGui::GetWindowDrawList()->AddRectFilled(
                            ImVec2(lo.x - px(4), lo.y - px(1)),
                            ImVec2(lo.x + ImGui::GetContentRegionAvail().x + px(4),
                                   lo.y + ImGui::GetTextLineHeight() + px(2)),
                            ImGui::GetColorU32(ImGuiCol_FrameBg));
                        continue;
                    }
                    for (int e = 0; e < static_cast<int>(row.entries.size()); ++e)
                        if (row.entries[static_cast<std::size_t>(e)].opponent == other) entries.push_back(e);
                }
                for (std::size_t m = 0; m < entries.size(); ++m) {
                    const auto& e = row.entries[static_cast<std::size_t>(entries[m])];
                    if (m > 0) ImGui::SameLine(0, px(3));
                    const auto text   = crossCellText(table, e, swiss);
                    const auto colour = cellColour(e);
                    ImGui::PushID(entries[m]);
                    if (colour) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(*colour));
                    const bool clicked =
                        ImGui::Selectable(text.c_str(), false, 0, ImVec2(ImGui::CalcTextSize(text.c_str()).x, 0));
                    if (colour) ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) entryTip(row, e);
                    if (clicked) {
                        using K = study::CellKind;
                        if (e.game != UINT32_MAX && e.kind != K::Missing && table.games[e.game].ref != UINT64_MAX)
                            clicks.game = e.game;
                        else if (!live && (e.kind == K::Missing || (e.kind == K::Absent && (e.unsure || e.hand))))
                            clicks.unknown = std::make_pair(static_cast<int>(rowp - table.rows.data()), entries[m]);
                        else if (e.game != UINT32_MAX && e.hand)
                            clicks.unknown = std::make_pair(static_cast<int>(rowp - table.rows.data()), entries[m]);
                    }
                    ImGui::PopID();
                }
            }
            // The points; the games played (a broadcast); the tie-breaks; the performance; the rating change.
            ImGui::TableNextColumn();
            {
                const auto text = pointsText(row.points, row.unknown);
                if (row.unknown > 0) {
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", text.c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", trn("{} result is not in the database: the points may be more.",
                                                    "{} results are not in the database: the points may be more.",
                                                    row.unknown, row.unknown)
                                                    .c_str());
                } else {
                    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
                    ImGui::TextUnformatted(text.c_str());
                    ImGui::PopFont();
                }
            }
            if (live) {
                ImGui::TableNextColumn();
                ImGui::Text("%d", row.played);
            }
            for (int k = 0; k < tbs; ++k) {
                ImGui::TableNextColumn();
                const auto state = row.states[static_cast<std::size_t>(k)];
                if (state == study::TiebreakState::None) continue;
                const auto t = table.tiebreaks[static_cast<std::size_t>(k)];
                auto text =
                    tiebreakValue(t, row.tiebreaks[static_cast<std::size_t>(k)], decimals[static_cast<std::size_t>(k)]);
                if (state == study::TiebreakState::Unsure) text += "?";
                if (state == study::TiebreakState::Unsure)
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", text.c_str());
                else
                    ImGui::TextUnformatted(text.c_str());
                if (state == study::TiebreakState::Unsure && ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", TR("It counts a result that is not in the database."));
            }
            if (wins) {
                ImGui::TableNextColumn();
                int won = 0;
                for (const auto& e : row.entries) won += e.known && e.points > 0.75 && e.kind != study::CellKind::Live;
                ImGui::Text("%d", won);
            }
            ImGui::TableNextColumn();
            if (row.performance) {
                ImGui::Text("%d", *row.performance);
                if (ImGui::IsItemHovered()) {
                    const double share = row.over_board_games ? row.over_board / row.over_board_games : 0;
                    ImGui::SetTooltip("%s", trf("The opponents' average {} over {} games, {} %: {} (FIDE's table).",
                                                row.opponents_rating, row.over_board_games,
                                                static_cast<int>(std::lround(share * 100)),
                                                *row.performance >= row.opponents_rating
                                                    ? fmt::format("+{}", *row.performance - row.opponents_rating)
                                                    : fmt::format("−{}", row.opponents_rating - *row.performance))
                                                .c_str());
                }
            }
            ImGui::TableNextColumn();
            if (row.change) {
                const auto text = changeText(*row.change) + (row.unknown > 0 ? "?" : "");
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(*row.change >= 0 ? ui::GOOD : ui::WARN), "%s",
                                   text.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "%s",
                        trf("{} · expected {}, scored {}, over {} rated games",
                            row.k_listed ? trf("K {} from FIDE's list", row.k) : trf("K {} by FIDE's rules", row.k),
                            fmt::format("{:.2f}", row.expected), pointsText(row.scored), row.rated_games)
                            .c_str());
            }
            ImGui::PopID();
        }
    ImGui::EndTable();
    return clicks;
}

void App::drawStudyCrosstable() {
    auto& c = *crosstable_;
    const ui::TopicRegion topic(guide::topics::Crosstables);
    const bool popup  = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup);
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !popup && !ImGui::GetIO().WantTextInput;
    if (ui::iconButton("crosstable_back", ui::Icon::Back, TR("Tournaments")) || escape) {
        leaveCrosstable();
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("›");
    ImGui::SameLine();
    ImGui::TextUnformatted(c.database.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("›");
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("Crosstable"));
    ImGui::SameLine(0, px(16));
    ImGui::TextDisabled("%s", TR("Esc"));
    ImGui::SameLine();
    ui::guideLink("crosstable_help", guide::topics::Crosstables);
    ui::pageTitle(c.title.c_str());
    // Fair play (R17): in an arena from here, no report until Pause (#95).
    if (arenaHoldsAids()) {
        ui::beginCard("crosstable_arena");
        ui::inlineIcon(ui::Icon::Lock, ui::ACCENT);
        ImGui::SameLine();
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(TR("Locked while you are in an arena on Lichess, between two games too."));
        ImGui::TextDisabled("%s", TR("The reports come back with Pause (Alt+P), or at the arena's end."));
        ImGui::PopTextWrapPos();
        ui::endCard();
        return;
    }
    if (!c.input) {
        ImGui::TextDisabled("%s", TR("Reading its games…"));
        return;
    }
    const auto& t    = c.table;
    const bool swiss = t.system.system == study::TournamentSystem::Swiss;
    if (export_when_made_.first == 2) {  // --focus export-crosstable=… (#93)
        const int format  = export_when_made_.second;
        export_when_made_ = {-1, -1};
        openCrosstableExport(format == static_cast<int>(ExportFormat::Html));
        return;
    }
    // What it is.
    {
        std::vector<std::string> parts;
        parts.push_back(systemName(t.system));
        parts.push_back(trn("{} player", "{} players", t.players.size(), t.players.size()));
        if (t.rounds > 0) parts.push_back(trn("{} round", "{} rounds", t.rounds, t.rounds));
        if (t.mean_elo > 0) parts.push_back(trf("mean Elo {}", t.mean_elo));
        if (swiss && !c.all && t.rows.size() > 10) parts.push_back(TR("the first ten"));
        std::string line;
        for (const auto& p : parts) line = line.empty() ? p : trf("{} · {}", line, p);
        ImGui::TextDisabled("%s", line.c_str());
        ImGui::SameLine(0, px(8));
        const auto games    = static_cast<uint32_t>(c.input->games.size());
        const auto expected = t.shape.expected;
        if (expected > games) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               trf("· {} of {} games", ui::exactCount(games), ui::exactCount(expected)).c_str());
        } else {
            ImGui::TextDisabled("%s", trn("· {} game", "· {} games", games, ui::exactCount(games)).c_str());
        }
        if (swiss && t.rows.size() > 10) {
            ImGui::SameLine(0, px(12));
            if (ImGui::SmallButton(c.all ? TR("The first ten") : trf("All players ({})", t.rows.size()).c_str()))
                c.all = !c.all;
        }
        // Printed, and as a page of HTML (#93), at the right.
        const char* print = TR("Print…");
        const char* html  = TR("HTML page");
        const auto& style = ImGui::GetStyle();
        const float w =
            ImGui::CalcTextSize(print).x + ImGui::CalcTextSize(html).x + style.FramePadding.x * 4 + style.ItemSpacing.x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(12), ImGui::GetContentRegionMax().x - w));
        if (ImGui::SmallButton(print)) openCrosstableExport(false);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("The crosstable as a PDF on landscape pages, made by RookStudio"));
        ImGui::SameLine();
        if (ImGui::SmallButton(html)) openCrosstableExport(true);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("The crosstable as a page of its own"));
    }
    if (!c.said.empty()) {
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(c.said.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!c.error.empty()) {
        ImGui::PushTextWrapPos();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::BAD), "%s", c.error.c_str());
        ImGui::PopTextWrapPos();
    }
    if (care_job_ && care_job_->crosstable) ImGui::TextDisabled("%s", TR("Writing in the database…"));

    // What is wrong with the data: a line each, a click each.
    std::size_t index = shelf().entries().size();
    for (std::size_t i = 0; i < shelf().entries().size(); ++i)
        if (shelf().entries()[i].folder == c.folder) index = i;
    const auto db       = index < shelf().entries().size() ? shelf().open(index) : nullptr;
    const bool writable = db && !db->readOnly() && !care_job_;
    const auto name     = [&](uint32_t p) { return p == study::NO_PLAYER ? std::string("?") : t.players[p].name; };
    const auto number   = [&](uint32_t p) {
        const auto* r = t.rowOf(p);
        return r ? trf("no. {}", r->start) : std::string("?");
    };
    if (t.system.system == study::TournamentSystem::Team || t.system.system == study::TournamentSystem::Knockout) {
        issueLine(ui::Icon::Info, ImGui::GetColorU32(ImGuiCol_Text),
                  t.system.system == study::TournamentSystem::Team ? TR("A team event: its crosstable comes later.")
                                                                   : TR("A knockout: its crosstable comes later."),
                  TR("Below, each player's points and games, round by round."));
    }
    if (!t.missing.empty()) {
        std::vector<std::string> games;
        for (const auto& m : t.missing) {
            if (games.size() >= 4) {
                games.push_back("…");
                break;
            }
            const auto who = m.alone != study::NO_PLAYER
                                 ? trf("{} without a game", name(m.alone))
                                 : trf("{} – {}", name(m.white), swiss ? number(m.black) : name(m.black));
            games.push_back(m.round > 0 ? trf("round {}, {}", m.round, who) : who);
        }
        std::string said;
        for (const auto& g : games) said = said.empty() ? g : trf("{}; {}", said, g);
        issueLine(ui::Icon::Alert, ui::WARN,
                  trn("{} game is missing", "{} games are missing", t.missing.size(), t.missing.size()),
                  trf("{}. Counted “?”: the points, the tie-breaks and the rating change say so.", said));
        if (ImGui::SmallButton(TR("Look for them in your other databases"))) {
            if (c.looking.valid()) {  // asked again: the search under way ends out of sight (#98)
                *c.look_cancel = true;
                jobs_.park(std::move(c.looking));
            }
            c.asked.clear();
            for (const auto& m : t.missing)
                if (m.alone == study::NO_PLAYER)
                    c.asked.push_back({name(m.white), name(m.black), m.round, c.input->year});
            std::vector<OtherDatabase> others;
            auto& s = shelf();
            for (std::size_t i = 0; i < s.entries().size(); ++i) {
                if (s.entries()[i].source || s.entries()[i].folder == c.folder) continue;
                if (const auto other = s.open(i))
                    others.push_back({s.entries()[i].folder, s.entries()[i].name, other->snapshot()});
            }
            c.found.reset();
            c.look_cancel = std::make_shared<std::atomic<bool>>(false);
            c.looking =
                std::async(std::launch::async, [asked = c.asked, others = std::move(others), cancel = c.look_cancel] {
                    study::enterBackground();
                    return lookForMissing(asked, others, *cancel);
                });
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(c.hand_open ? TR("Hide the results set by hand") : TR("Set the results by hand")))
            c.hand_open = !c.hand_open;
        if (c.looking.valid()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", TR("Looking…"));
        }
        // What the other databases have.
        if (c.found) {
            if (c.found->empty()) ImGui::TextDisabled("%s", TR("Not found in your other databases."));
            for (std::size_t k = 0; k < c.found->size(); ++k) {
                const auto& f = (*c.found)[k];
                ImGui::PushID(static_cast<int>(k));
                ImGui::TextDisabled("%s", trf("In “{}”: {} – {}, round {}, {}", f.database, f.white, f.black,
                                              f.round.empty() ? std::string("?") : f.round, resultText(f.result))
                                              .c_str());
                ImGui::SameLine();
                ImGui::BeginDisabled(!writable);
                if (ImGui::SmallButton(TR("Copy it into this tournament")) && f.asked < c.asked.size()) {
                    std::shared_ptr<const study::Snapshot> from;
                    auto& s = shelf();
                    for (std::size_t i = 0; i < s.entries().size(); ++i)
                        if (s.entries()[i].folder == f.folder)
                            if (const auto other = s.open(i)) from = other->snapshot();
                    if (from && db) {
                        const auto& a = c.asked[f.asked];
                        auto game =
                            missingGameCopy(from->game(f.id), std::string(c.snapshot->name(c.entry.event)),
                                            std::string(c.snapshot->name(c.entry.site)), a.round, a.white, a.black);
                        try {
                            const auto id    = db->snapshot()->games();
                            const auto added = db->append({std::move(game)}, true, f.database);
                            if (added.added == 1) {
                                Care::Undo undo;
                                undo.what   = TR("a game copied in");
                                undo.states = {study::GameState{id, std::nullopt, true}};
                                crosstable_undo_.push_back(std::move(undo));
                                c.said =
                                    trf("Copied from “{}” into this tournament, its event, site and players "
                                        "written as here. Ctrl+Z undoes.",
                                        f.database);
                                shelf().rebuildTree(index);
                            }
                        } catch (const std::exception& e) {
                            c.error = e.what();
                        }
                        c.found->erase(c.found->begin() + static_cast<std::ptrdiff_t>(k));
                        ImGui::EndDisabled();
                        ImGui::PopID();
                        break;
                    }
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }
        // The results set by hand: each game missing, its result.
        if (c.hand_open) {
            bool changed = false;
            for (std::size_t k = 0; k < t.missing.size(); ++k) {
                const auto& m = t.missing[k];
                ImGui::PushID(static_cast<int>(k));
                if (m.alone != study::NO_PLAYER) {
                    ImGui::TextDisabled("%s", trf("Round {}: {}", m.round, name(m.alone)).c_str());
                    for (const double points : {1.0, 0.5, 0.0}) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton(pointsText(points).c_str())) {
                            study::HandResult h;
                            h.white  = name(m.alone);
                            h.round  = m.round;
                            h.points = points;
                            c.settings.hand.push_back(h);
                            changed = true;
                        }
                    }
                } else {
                    ImGui::TextDisabled("%s",
                                        (m.round > 0 ? trf("Round {}: {} – {}", m.round, name(m.white), name(m.black))
                                                     : trf("{} – {}", name(m.white), name(m.black)))
                                            .c_str());
                    for (const char r : {'1', '=', '0'}) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton(resultText(r).c_str())) {
                            study::HandResult h;
                            h.white  = name(m.white);
                            h.black  = name(m.black);
                            h.round  = m.round;
                            h.result = r;
                            c.settings.hand.push_back(h);
                            changed = true;
                        }
                    }
                }
                ImGui::PopID();
            }
            if (!c.settings.hand.empty()) {
                const auto clear = c.settings.hand.size() == 1
                                       ? std::string(TR("Clear the result set by hand"))
                                       : trn("Clear the {} result set by hand", "Clear the {} results set by hand",
                                             c.settings.hand.size(), c.settings.hand.size());
                if (ImGui::SmallButton(clear.c_str())) {
                    c.settings.hand.clear();
                    changed = true;
                }
            }
            if (changed) {
                c.settings.hand_from.clear();  // the user's now, whatever came from a file
                keepCrosstableSettings();
            }
        }
    } else if (!c.settings.hand.empty() && c.settings.hand_from == "lichess-trf") {
        // A Swiss kept from Lichess (#95): its rounds without a game, as its TRF file gives them.
        issueLine(ui::Icon::Pencil, ImGui::GetColorU32(ImGuiCol_Text),
                  trn("{} result from Lichess's TRF file", "{} results from Lichess's TRF file", c.settings.hand.size(),
                      c.settings.hand.size()),
                  TR("Its rounds without a game (byes, late joins, absences), kept with this tournament in your "
                     "profile; the database is unchanged."));
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Clear them"))) {
            c.settings.hand.clear();
            keepCrosstableSettings();
        }
    } else if (!c.settings.hand.empty()) {
        issueLine(
            ui::Icon::Pencil, ImGui::GetColorU32(ImGuiCol_Text),
            trn("{} result set by hand", "{} results set by hand", c.settings.hand.size(), c.settings.hand.size()),
            TR("Kept with this tournament in your profile; the database is unchanged."));
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Clear them"))) {
            c.settings.hand.clear();
            keepCrosstableSettings();
        }
    }
    for (std::size_t k = 0; k < t.two_names.size(); ++k) {
        const auto& two = t.two_names[k];
        ImGui::PushID(static_cast<int>(1000 + k));
        issueLine(ui::Icon::Users, ui::WARN, TR("A player under two names?"),
                  trf("“{}” (rounds {} to {}) and “{}” (rounds {} to {}), never paired together.", name(two.a),
                      two.a_from, two.a_to, name(two.b), two.b_from, two.b_to));
        ImGui::BeginDisabled(!writable);
        if (ImGui::SmallButton(TR("Bring them together"))) {
            std::vector<uint32_t> spellings = t.players[two.a].spellings;
            for (const auto id : t.players[two.b].spellings) spellings.push_back(id);
            // The fuller name: the one written in full.
            const auto& na  = t.players[two.a].name;
            const auto& nb  = t.players[two.b].name;
            const auto full = study::nameParts(nb).given.size() > study::nameParts(na).given.size() ? nb : na;
            auto table      = study::withPlayer(*c.snapshot->players(), spellings, full, [](study::Player& p) {
                if (p.how.empty()) p.how = "hand";
            });
            startCarePlayers(index, std::move(table), trf("“{}” and “{}” brought together", na, nb),
                             trf("“{}” and “{}” brought together in the table of names. Ctrl+Z undoes.", na, nb), true);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", TR("In the database's table of names: the games stay as they are written."));
        ImGui::PopID();
    }
    if (!c.input->twice.empty()) {
        std::size_t extra = 0, mergeable = 0;
        for (const auto& g : c.input->twice) {
            extra += g.games.size() - 1;
            if (g.same_moves && g.same_results) ++mergeable;
        }
        issueLine(ui::Icon::Copy, ImGui::GetColorU32(ImGuiCol_Text),
                  trn("{} game came twice, counted once", "{} games came twice, counted once", extra, extra),
                  TR("The same players, round and moves from two sources, their headers written otherwise."));
        ImGui::BeginDisabled(!writable || mergeable == 0);
        if (ImGui::SmallButton(TR("Merge them"))) {
            std::vector<study::DuplicateGroup> groups;
            for (const auto& g : c.input->twice) {
                if (!g.same_moves || !g.same_results) continue;
                study::DuplicateGroup group;
                group.games = g.games;
                std::sort(group.games.begin(), group.games.end());
                groups.push_back(std::move(group));
            }
            startCareMerges(index, std::move(groups), {}, true);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", TR("Each kept as one game with the fullest fields of each source, as the "
                                       "maintenance merges duplicates; Ctrl+Z undoes."));
        if (mergeable < c.input->twice.size()) {
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("See those that differ"))) {
                std::vector<study::GameId> ids;
                for (const auto& g : c.input->twice)
                    if (!g.same_moves || !g.same_results) ids.insert(ids.end(), g.games.begin(), g.games.end());
                std::sort(ids.begin(), ids.end());
                db_only_        = std::move(ids);
                db_only_said_   = TR("games that came twice and differ");
                db_tournaments_ = false;
                leaveCrosstable();
                return;
            }
        }
    }
    if (!t.forfeits.empty()) {
        std::vector<std::string> rounds;
        for (const auto g : t.forfeits)
            if (t.games[g].round.round > 0) rounds.push_back(std::to_string(t.games[g].round.round));
        issueLine(ui::Icon::Info, ImGui::GetColorU32(ImGuiCol_Text),
                  rounds.empty() ? trn("{} forfeit", "{} forfeits", t.forfeits.size(), t.forfeits.size())
                                 : trn("{} forfeit, round {}", "{} forfeits, rounds {}", t.forfeits.size(),
                                       t.forfeits.size(), listed(rounds)),
                  TR("A decisive result without moves is read as a forfeit (+ and −): in the points, not in the "
                     "performance nor the rating change."));
    }
    if (t.shape.uncertain && !c.settings.system && t.shape.choices.size() >= 2) {
        std::string why;
        if (t.shape.over_met > 0)
            why = trn("{} for {} players, but {} pair met more often than its system allows.",
                      "{} for {} players, but {} pairs met more often than its system allows.", t.shape.over_met,
                      trn("{} round", "{} rounds", t.rounds, t.rounds), t.players.size(), t.shape.over_met);
        else
            why = trf("{} for {} players, {} of {} games.", trn("{} round", "{} rounds", t.rounds, t.rounds),
                      t.players.size(), ui::exactCount(t.shape.games), ui::exactCount(t.shape.expected));
        issueLine(ui::Icon::Help, ImGui::GetColorU32(ImGuiCol_Text), TR("The system is uncertain"), why);
        for (std::size_t k = 0; k < t.shape.choices.size(); ++k) {
            if (k > 0) ImGui::SameLine();
            ImGui::PushID(static_cast<int>(2000 + k));
            if (ImGui::SmallButton(systemName(t.shape.choices[k]).c_str())) {
                c.settings.system = t.shape.choices[k];
                keepCrosstableSettings();
            }
            ImGui::PopID();
        }
    } else if (c.settings.system) {
        ImGui::TextDisabled("%s", trf("Played as a {}, as you chose.", systemName(*c.settings.system)).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(trf("As guessed: {}", systemName(t.shape.system)).c_str())) {
            c.settings.system.reset();
            keepCrosstableSettings();
        }
    }
    ImGui::Dummy(ImVec2(0, px(2)));

    // The tie-breaks, then the table.
    bool menu = false;
    int which = -1;
    if (t.system.system == study::TournamentSystem::RoundRobin || swiss) drawCrossTiebreakLine(t, menu, which);
    const auto clicks = drawCrossTable(t, c.all || !swiss, false);
    if (clicks.tiebreak) {
        menu  = true;
        which = *clicks.tiebreak;
    }
    if (menu) {
        c.tiebreak_menu = which;
        ImGui::OpenPopup("##crosstable_tiebreak");
    }
    if (ImGui::BeginPopup("##crosstable_tiebreak")) {
        auto order = c.settings.order.value_or(t.tiebreaks);
        bool reset = false;
        if (crossTiebreakMenu(order, c.tiebreak_menu, t, c.settings.order.has_value(), reset)) {
            if (reset)
                c.settings.order.reset();
            else
                c.settings.order = order;
            keepCrosstableSettings();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", swiss
                                  ? TR("“4b½”: against no. 4, with Black, a draw; “+” and “−” forfeits. In the accent "
                                       "colour, the games between the players shown. A click on a game opens it, on "
                                       "a name their dossier, on a tie-break's heading changes the tie-breaks, kept "
                                       "for this tournament. The start numbers follow the ratings.")
                                  : TR("A click on a game opens it, on a name their dossier, on a tie-break's heading "
                                       "changes the tie-breaks, kept for this tournament. The rating change: FIDE's "
                                       "formula, each player's K from FIDE's list when they are matched with it, else "
                                       "by FIDE's rules."));
    ImGui::PopTextWrapPos();

    // A game opened, with the tournament's games as its list (Ctrl+Left, Ctrl+Right).
    if (clicks.game && c.input) {
        auto all = std::make_shared<std::vector<BoardOrigin::Listed>>();
        std::vector<const study::CrossGame*> games;
        for (const auto& g : t.games)
            if (g.ref != UINT64_MAX) games.push_back(&g);
        std::stable_sort(games.begin(), games.end(), [](const auto* a, const auto* b) {
            return std::tie(a->round.round, a->round.board, a->ref) < std::tie(b->round.round, b->round.board, b->ref);
        });
        std::size_t at = 0;
        for (std::size_t k = 0; k < games.size(); ++k) {
            BoardOrigin::Listed l;
            l.folder   = c.folder;
            l.name     = c.database;
            l.snapshot = c.snapshot;
            l.id       = games[k]->ref;
            if (games[k] == &t.games[*clicks.game]) at = k;
            all->push_back(std::move(l));
        }
        BoardOrigin origin;
        origin.listed    = std::move(all);
        origin.list_name = trf("Crosstable of {}", c.title);
        board_origin_    = std::move(origin);
        openListRow(at);
        if (board_back_) board_back_->crosstable = true;
        return;
    }
    if (clicks.player) {
        const auto& p = t.players[*clicks.player];
        if (!p.spellings.empty()) openDossierOf(*c.snapshot, p.spellings.front());
        return;
    }
    // A "?" clicked: its result set by hand.
    if (clicks.unknown) {
        c.cell_menu_row   = clicks.unknown->first;
        c.cell_menu_entry = clicks.unknown->second;
        ImGui::OpenPopup("##crosstable_hand");
    }
    if (ImGui::BeginPopup("##crosstable_hand")) {
        if (c.cell_menu_row >= 0 && c.cell_menu_row < static_cast<int>(t.rows.size())) {
            const auto& row = t.rows[static_cast<std::size_t>(c.cell_menu_row)];
            if (c.cell_menu_entry >= 0 && c.cell_menu_entry < static_cast<int>(row.entries.size())) {
                const auto& e   = row.entries[static_cast<std::size_t>(c.cell_menu_entry)];
                const auto self = t.players[row.player].name;
                bool changed    = false;
                const auto drop = [&](const std::string& a, const std::string& b, int round) {
                    std::erase_if(c.settings.hand, [&](const study::HandResult& h) {
                        return h.round == round && ((h.white == a && h.black == b) || (h.white == b && h.black == a) ||
                                                    (b.empty() && h.black.empty() && h.white == a));
                    });
                };
                if (e.opponent != study::NO_PLAYER) {
                    const auto other = t.players[e.opponent].name;
                    ImGui::TextDisabled("%s", trf("Round {}: {} against {}", e.round, self, other).c_str());
                    const char* labels[] = {TR("Won"), TR("A draw"), TR("Lost")};
                    const char results[] = {'1', '=', '0'};
                    for (int k = 0; k < 3; ++k)
                        if (ImGui::MenuItem(labels[k])) {
                            drop(self, other, e.round);
                            study::HandResult h;
                            h.white  = self;
                            h.black  = other;
                            h.round  = e.round;
                            h.result = results[k];
                            c.settings.hand.push_back(h);
                            changed = true;
                        }
                    if (e.hand && ImGui::MenuItem(TR("Clear the result set by hand"))) {
                        drop(self, other, e.round);
                        changed = true;
                    }
                } else {
                    ImGui::TextDisabled("%s", trf("Round {}: {}, without a game", e.round, self).c_str());
                    const char* labels[]  = {TR("1 point"), TR("½ point"), TR("No point")};
                    const double values[] = {1.0, 0.5, 0.0};
                    for (int k = 0; k < 3; ++k)
                        if (ImGui::MenuItem(labels[k])) {
                            const double points = values[k];
                            drop(self, {}, e.round);
                            study::HandResult h;
                            h.white  = self;
                            h.round  = e.round;
                            h.points = points;
                            c.settings.hand.push_back(h);
                            changed = true;
                        }
                    if (e.hand && ImGui::MenuItem(TR("Clear the points set by hand"))) {
                        drop(self, {}, e.round);
                        changed = true;
                    }
                }
                if (changed) {
                    keepCrosstableSettings();
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndPopup();
    }
    // Ctrl+Z: its last change to the database undone.
    if (!ImGui::GetIO().WantTextInput && !popup && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
        crosstableUndo();
}

// ---------------------------------------------------------------------------------------------------------------------
// A followed broadcast's crosstable

void App::drawBroadcastCrosstable() {
    const FollowedRound* round = nullptr;
    for (const auto& f : followed_rounds_)
        if (f.key() == broadcast_round_) round = &f;
    if (!round) return;
    const ui::TopicRegion topic(guide::topics::Crosstables);
    auto& l          = live_crosstable_;
    const auto place = broadcastPlace(round->entry.tour.id.empty() ? round->key() : round->entry.tour.id);
    if (l.key != round->key()) {
        l          = LiveCrosstableState{};
        l.key      = round->key();
        l.settings = crosstableStore().get(place);
    }
    // The rounds kept in the "Broadcasts" database, for those no longer followed.
    auto& s               = shelf();
    const auto saved_name = std::string(TR("Broadcasts"));
    std::optional<std::size_t> saved;
    for (std::size_t i = 0; i < s.entries().size(); ++i)
        if (!s.entries()[i].source && s.entries()[i].name == saved_name) saved = i;
    const TournamentsKept* kept     = saved ? tournamentsOf(*saved, true) : nullptr;
    const uint64_t saved_generation = kept ? kept->generation : 0;
    const auto changes              = round->follower->changes();
    // Made again at most once a second: a round of a hundred boards sends moves all the time.
    const double now = ImGui::GetTime();
    if ((changes != l.changes && now - l.made_at >= 1.0) || saved_generation != l.saved_generation ||
        l.changes == ~uint64_t{0}) {
        const auto games = round->follower->games();
        std::vector<study::GameId> earlier;
        std::shared_ptr<const study::Snapshot> snapshot;
        auto probe = liveCrosstableInput(games, nullptr, {});
        if (kept && !probe.event.empty()) {
            const auto key = study::eventKey(probe.event);
            int year       = 0;
            for (const auto& g : probe.games)
                if (g.date) year = static_cast<int>(g.date >> 9);
            for (const auto& e : kept->list->tournaments) {
                if (e.key.rfind(key + "|", 0) != 0) continue;
                if (year > 0 && (e.ref.year_from > year || e.ref.year_to < year)) continue;
                std::atomic<bool> cancel{false};
                earlier  = study::tournamentGames(*kept->snapshot, e.ref, cancel);
                snapshot = kept->snapshot;
                break;
            }
        }
        l.input = snapshot ? liveCrosstableInput(games, snapshot.get(), earlier) : std::move(probe);
        study::CrossOptions options;
        options.live       = true;
        options.format     = round->entry.tour.format;
        options.system     = l.settings.system;
        options.order      = l.settings.order;
        l.table            = study::makeCrosstable(l.input.players, l.input.games, options);
        l.changes          = changes;
        l.saved_generation = saved_generation;
        l.made_at          = now;
    }
    const auto& t = l.table;
    // What it is.
    {
        std::vector<std::string> parts;
        parts.push_back(systemName(t.system));
        parts.push_back(trn("{} player", "{} players", t.players.size(), t.players.size()));
        int finished = 0;
        for (const auto& g : t.games) finished += !g.live && resultKnown(g.result);
        parts.push_back(trn("{} game played", "{} games played", finished, finished));
        if (l.input.saved > 0)
            parts.push_back(trn("{} kept in “Broadcasts”", "{} kept in “Broadcasts”", l.input.saved, l.input.saved));
        std::string line;
        for (const auto& p : parts) line = line.empty() ? p : trf("{} · {}", line, p);
        if (t.live > 0) {
            int live_round = 0;
            for (const auto& g : t.games)
                if (g.live) live_round = std::max(live_round, g.round.round);
            ImGui::TextColored(LIVE_RED, "%s",
                               live_round > 0 ? trf("live · round {}", live_round).c_str() : TR("live"));
            ImGui::SameLine();
        }
        ImGui::TextDisabled("%s", line.c_str());
    }
    if (t.system.system == study::TournamentSystem::Team) {
        ImGui::TextDisabled("%s",
                            TR("A team event: Lichess's team standings stay beside; its crosstable comes later."));
        return;
    }
    // Its system chosen: not sure while it is being played.
    if (t.shape.uncertain && !l.settings.system && t.shape.choices.size() >= 2) {
        ImGui::TextDisabled("%s", TR("The system is uncertain while the tournament is played:"));
        for (std::size_t k = 0; k < t.shape.choices.size(); ++k) {
            ImGui::SameLine();
            ImGui::PushID(static_cast<int>(k));
            if (ImGui::SmallButton(systemName(t.shape.choices[k]).c_str())) {
                l.settings.system = t.shape.choices[k];
                crosstableStore().set(place, l.settings);
                l.changes = ~uint64_t{0};
            }
            ImGui::PopID();
        }
    }
    bool menu = false;
    int which = -1;
    drawCrossTiebreakLine(t, menu, which);
    const auto clicks = drawCrossTable(t, true, true);
    if (clicks.tiebreak) {
        menu  = true;
        which = *clicks.tiebreak;
    }
    if (menu) {
        l.tiebreak_menu = which;
        ImGui::OpenPopup("##live_tiebreak");
    }
    if (ImGui::BeginPopup("##live_tiebreak")) {
        auto order = l.settings.order.value_or(t.tiebreaks);
        bool reset = false;
        if (crossTiebreakMenu(order, l.tiebreak_menu, t, l.settings.order.has_value(), reset)) {
            if (reset)
                l.settings.order.reset();
            else
                l.settings.order = order;
            crosstableStore().set(place, l.settings);
            l.changes = ~uint64_t{0};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", TR("● a game being played (a click follows it on the board); · not played yet. Made of "
                                 "the rounds RookStudio follows and those it kept in the “Broadcasts” database; "
                                 "Lichess's standings stay beside. Nothing more is asked of Lichess for it."));
    ImGui::PopTextWrapPos();
    if (clicks.game) {
        const auto& g = t.games[*clicks.game];
        if (g.source == 1 && g.ref < l.input.urls.size()) {
            const auto& url = l.input.urls[g.ref];
            for (const auto& b : broadcast_boards_)
                if (b.url == url) {
                    followBroadcastOnBoard(b);
                    return;
                }
            // A game of another round of the tournament: its moves as Lichess last sent them.
            readOnBoard(l.input.pgns[g.ref], trf("{} – {}", g.white < t.players.size() ? t.players[g.white].name : "?",
                                                 g.black < t.players.size() ? t.players[g.black].name : "?"));
        } else if (kept && g.ref < kept->snapshot->games()) {
            readOnBoard(study::importedPgn(kept->snapshot->game(g.ref)), saved_name);
        }
        return;
    }
    if (clicks.player) {
        study::DossierSubject subject;
        subject.name = t.players[*clicks.player].name;
        openDossier(std::move(subject));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Ctrl+K

void App::lookForCrosstableNames(const std::string& query) {
    // The words typed, less the start of the command's own name ("cross", "crosstable").
    const auto command = study::foldText(TR("Crosstable"));
    std::string words;
    std::size_t letters = 0;
    for (std::size_t at = 0; at < query.size();) {
        auto end = query.find(' ', at);
        if (end == std::string::npos) end = query.size();
        auto word = query.substr(at, end - at);
        at        = end + 1;
        if (word.empty()) continue;
        if (word.back() == ':') word.pop_back();
        const auto folded = study::foldText(word);
        if (folded.size() >= 3 && command.rfind(folded, 0) == 0) continue;
        words += (words.empty() ? "" : " ") + word;
        letters += word.size();
    }
    if (letters < 3) {
        crosstable_names_.clear();
        return;
    }
    // The lists of the user's databases, made in the background when missing; those kept read.
    std::vector<TournamentsOf> lists;
    std::string of = words;
    auto& s        = shelf();
    for (std::size_t i = 0; i < s.entries().size(); ++i) {
        if (s.entries()[i].source) continue;
        if (const auto* kept = tournamentsOf(i, true)) {
            lists.push_back({s.entries()[i].folder, s.entries()[i].name, kept->snapshot, kept->list});
            of += "|" + std::to_string(kept->generation);
        }
    }
    if (of == crosstable_names_of_) return;
    crosstable_names_of_ = of;
    crosstable_names_    = crosstableNames(lists, words, 8);
}

// ---------------------------------------------------------------------------------------------------------------------
// Printed, and as a page of HTML (#93)

publish::Document crosstableDocument(const study::Crosstable& table, const std::string& title, uint32_t games,
                                     bool letter, const publish::DocumentLook& look) {
    using publish::Cell;
    publish::Document d;
    d.title          = title;
    d.header         = title;
    d.letter         = letter;
    d.landscape      = true;
    d.size           = 9;
    d.pieces         = look.pieces;
    d.colours        = look.colours;
    d.lang           = look.lang;
    const bool swiss = table.system.system != study::TournamentSystem::RoundRobin;
    std::vector<std::string> parts;
    parts.push_back(systemName(table.system));
    parts.push_back(trn("{} player", "{} players", table.players.size(), table.players.size()));
    if (table.rounds > 0) parts.push_back(trn("{} round", "{} rounds", table.rounds, table.rounds));
    if (table.mean_elo > 0) parts.push_back(trf("mean Elo {}", table.mean_elo));
    if (table.shape.expected > games)
        parts.push_back(trf("{} of {} games", ui::exactCount(games), ui::exactCount(table.shape.expected)));
    else
        parts.push_back(trn("{} game", "{} games", games, ui::exactCount(games)));
    std::string line;
    for (const auto& p : parts) line = line.empty() ? p : trf("{} · {}", line, p);
    d.blocks.push_back(publish::title(title));
    d.blocks.push_back(publish::subtitle(line));

    const int middle = swiss ? std::max(table.rounds, 1) : static_cast<int>(table.rows.size());
    const int tbs    = static_cast<int>(table.tiebreaks.size());
    std::vector<bool> decimals(static_cast<std::size_t>(tbs), false);
    for (const auto& row : table.rows)
        for (std::size_t k = 0; k < decimals.size() && k < row.tiebreaks.size(); ++k)
            if (row.states[k] != study::TiebreakState::None) {
                const double twice = row.tiebreaks[k] * 2;
                if (std::abs(twice - std::round(twice)) > 1e-9) decimals[k] = true;
            }
    const bool wins = !swiss && std::find(table.tiebreaks.begin(), table.tiebreaks.end(), study::Tiebreak::Wins) ==
                                    table.tiebreaks.end();
    publish::Block b;
    b.kind  = publish::Block::Kind::Table;
    auto& t = b.table;
    t.size  = middle > 14 ? 6.5f : middle > 9 ? 7.5f : 8.5f;
    t.head.push_back(TR("Pl."));
    t.align.push_back(1);
    if (swiss) {
        t.head.push_back(TR("No."));
        t.align.push_back(1);
    }
    t.head.push_back(TR("Player"));
    t.align.push_back(0);
    t.head.push_back(TR("Elo"));
    t.align.push_back(1);
    for (int k = 0; k < middle; ++k) {
        t.head.push_back(swiss ? trf("R{}", k + 1) : std::to_string(k + 1));
        t.align.push_back(2);
    }
    t.head.push_back(TR("Pts"));
    t.align.push_back(1);
    for (const auto tb : table.tiebreaks) {
        t.head.push_back(tiebreakHeading(tb));
        t.align.push_back(1);
    }
    if (wins) {
        t.head.push_back(TR("Wins"));
        t.align.push_back(1);
    }
    t.head.push_back(TR("Perf."));
    t.align.push_back(1);
    t.head.push_back(TR("Elo ±"));
    t.align.push_back(1);
    constexpr publish::Rgba own{232, 235, 239, 255}, warn{178, 93, 16, 255};
    for (const auto& row : table.rows) {
        const auto& player = table.players[row.player];
        std::vector<Cell> cells;
        cells.emplace_back(row.place_from == row.place_to ? std::to_string(row.place_from)
                                                          : fmt::format("{}–{}", row.place_from, row.place_to),
                           publish::FontId::Regular, publish::MUTED_COLOUR);
        if (swiss) cells.emplace_back(std::to_string(row.start), publish::FontId::Regular, publish::MUTED_COLOUR);
        std::string name = player.name;
        if (!player.title.empty()) name = trf("{} {}", player.title, name);
        if (!player.federation.empty()) name = trf("{} ({})", name, player.federation);
        cells.emplace_back(name);
        cells.emplace_back(player.elo > 0 ? std::to_string(player.elo) : std::string());
        for (int k = 0; k < middle; ++k) {
            std::string text;
            bool missing = false;
            if (swiss) {
                for (const auto& e : row.entries)
                    if (e.round == k + 1) {
                        text += (text.empty() ? "" : " ") + crossCellText(table, e, true);
                        missing = missing || e.kind == study::CellKind::Missing;
                    }
            } else {
                const auto other = table.rows[static_cast<std::size_t>(k)].player;
                if (other == row.player) {
                    Cell c;
                    c.fill = own;
                    cells.push_back(c);
                    continue;
                }
                for (const auto& e : row.entries)
                    if (e.opponent == other) {
                        text += (text.empty() ? "" : " ") + crossCellText(table, e, false);
                        missing = missing || e.kind == study::CellKind::Missing;
                    }
            }
            cells.emplace_back(text, publish::FontId::Regular, missing ? warn : publish::TEXT_COLOUR);
        }
        cells.emplace_back(pointsText(row.points, row.unknown), publish::FontId::SemiBold,
                           row.unknown > 0 ? warn : publish::TEXT_COLOUR);
        for (int k = 0; k < tbs; ++k) {
            const auto state = row.states[static_cast<std::size_t>(k)];
            if (state == study::TiebreakState::None) {
                cells.emplace_back("");
                continue;
            }
            auto text =
                tiebreakValue(table.tiebreaks[static_cast<std::size_t>(k)], row.tiebreaks[static_cast<std::size_t>(k)],
                              decimals[static_cast<std::size_t>(k)]);
            if (state == study::TiebreakState::Unsure) text += "?";
            cells.emplace_back(text);
        }
        if (wins) {
            int won = 0;
            for (const auto& e : row.entries) won += e.known && e.points > 0.75 && e.kind != study::CellKind::Live;
            cells.emplace_back(std::to_string(won));
        }
        cells.emplace_back(row.performance ? std::to_string(*row.performance) : std::string());
        cells.emplace_back(row.change ? changeText(*row.change) + (row.unknown > 0 ? "?" : "") : std::string());
        t.rows.push_back(std::move(cells));
    }
    d.blocks.push_back(std::move(b));
    // The tie-breaks named, in their order.
    std::vector<std::string> named;
    for (const auto tb : table.tiebreaks) named.push_back(trf("{}: {}", tiebreakHeading(tb), tiebreakName(tb)));
    std::string tie;
    for (const auto& n : named) tie = tie.empty() ? n : trf("{} · {}", tie, n);
    if (!tie.empty()) d.blocks.push_back(publish::note(trf("Tie-breaks, in their order: {}.", tie)));
    if (!table.missing.empty())
        d.blocks.push_back(publish::note(trn("{} game is missing from the database: its result is counted “?”.",
                                             "{} games are missing from the database: their results are counted “?”.",
                                             table.missing.size(), table.missing.size())));
    return d;
}

}  // namespace fcg
