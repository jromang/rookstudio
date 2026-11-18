// The Databases page of the Chess space (mockup M1, board 1): the databases of the profile, the chosen one's facts
// and what can be done with it, an import with its progress, a file dropped on the page.

#include <algorithm>
#include <chrono>
#include <string>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include "app.hpp"
#include "console.hpp"
#include "i18n.hpp"
#include "machines.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

const std::vector<std::pair<std::string, std::string>>& gameFiles() {
    static const std::vector<std::pair<std::string, std::string>> filters = {{"PGN", "pgn;zst;gz;zip"}};
    return filters;
}

}  // namespace

DatabaseShelf& App::shelf() {
    if (!shelf_) shelf_ = std::make_unique<DatabaseShelf>(storage_.root());
    return *shelf_;
}

void App::importIntoDatabase(int index, const std::filesystem::path& file) {
    auto& s = shelf();
    if (index < 0 || index >= static_cast<int>(s.entries().size())) {
        try {
            index = static_cast<int>(s.create(file.stem().stem().string()));  // games.pgn.zst: games
        } catch (const std::exception& e) {
            db_message_ = e.what();
            return;
        }
    }
    db_selected_ = index;
    if (!s.startImport(static_cast<std::size_t>(index), file))
        db_message_ = s.import() ? TR("An import runs already: one at a time.") : s.facts(index).error;
    else
        db_message_.clear();
}

void App::pollDatabases() {
    if (!shelf_) return;
    const auto* job = shelf_->import();
    if (!job) return;
    const auto file = job->file.filename().string();
    if (const auto report = shelf_->finishedImport()) {
        if (!report->error.empty()) {
            db_message_ =
                trf("Import stopped: {}. The games committed before stay; importing the file again adds the "
                    "missing ones only.",
                    report->error);
        } else if (report->read.cancelled) {
            db_message_ =
                trf("Import of {} stopped at your request: {} games kept. Importing the file again adds the "
                    "missing ones only.",
                    file, ui::exactCount(report->added));
        } else {
            db_message_ = trf("Import of {} done: {} games added", file, ui::exactCount(report->added));
            if (report->duplicates > 0)
                db_message_ += trf(", {} already there left out", ui::exactCount(report->duplicates));
            if (report->read.damaged > 0)
                db_message_ +=
                    trf(", {} damaged (kept up to the move that cannot be read)", ui::exactCount(report->read.damaged));
            db_message_ += ".";
        }
        Console::instance().add(db_message_, !report->error.empty());
    }
}

void App::drawDatabases() {
    auto& s = shelf();
    if (db_list_ && db_selected_ >= 0 && db_selected_ < static_cast<int>(s.entries().size())) {
        drawGameList();
        return;
    }
    db_list_ = false;
    ui::pageTitle(TR("Databases"), guideSection());
    // The tools, on the title's line.
    {
        const char* make   = TR("New database");
        const char* import = TR("Import a file…");
        const char* add    = TR("Add a database…");
        float width        = px(36);
        for (const char* label : {make, import, add}) width += ImGui::CalcTextSize(label).x + px(44);
        width += ImGui::CalcTextSize("Ctrl+I").x + px(8);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(20), ImGui::GetContentRegionMax().x - width));
        if (ui::iconButton("db_new", ui::Icon::Plus, make)) {
            db_naming_ = true;
            db_name_.fill(0);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(s.import() != nullptr);
        if (ui::iconButton("db_import", ui::Icon::Folder, import) ||
            (!s.import() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_I)))
            dialogs_.openFile(Dialogs::Purpose::ImportGames, gameFiles());
        ImGui::EndDisabled();
        ImGui::SameLine(0, px(4));
        ImGui::TextDisabled("Ctrl+I");
        ImGui::SameLine();
        if (ui::iconButton("db_add", ui::Icon::Database, add)) dialogs_.openFolder(Dialogs::Purpose::AddDatabase);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("A database folder (Name.fcgdb) made elsewhere: listed here, its files left "
                                       "where they are."));
    }
    ImGui::TextDisabled("%s", TR("Drop a PGN, .zst, .gz or .zip file here: it goes into the chosen database, or into a "
                                 "new one named after it."));

    // A new database's name, typed in place.
    if (db_naming_) {
        ImGui::SetNextItemWidth(px(320));
        if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
        const bool done = ImGui::InputTextWithHint("##db_name", TR("The new database's name"), db_name_.data(),
                                                   db_name_.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (done || ImGui::Button(TR("Make"))) {
            try {
                db_selected_ = static_cast<int>(s.create(db_name_.data()));
            } catch (const std::exception& e) {
                db_message_ = e.what();
            }
            db_naming_ = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) db_naming_ = false;
    }
    if (!db_message_.empty()) {
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(db_message_.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Dummy(ImVec2(0, px(6)));

    // The linked sources: the Engines space's tournaments, the PGN file opened on the Board page.
    {
        refreshTournaments();
        std::vector<std::pair<std::string, study::LinkedSource>> sources;
        for (const auto& t : tournaments_)
            sources.push_back({t.spec.name.empty() ? t.dir.filename().string() : t.spec.name,
                               study::LinkedSource{study::LinkedSource::Kind::Tournament, t.dir}});
        if (board_.pgn && !board_.pgn->file.empty())
            sources.push_back({board_.pgn->file.filename().string(),
                               study::LinkedSource{study::LinkedSource::Kind::PgnFile, board_.pgn->file}});
        std::string key;
        for (const auto& [name, source] : sources) key += name + "\n" + source.path.string() + "\n";
        if (key != db_linked_key_) {
            s.setLinked(sources);
            db_linked_key_ = key;
        }
    }
    const auto& entries = s.entries();
    if (entries.empty()) {
        ui::beginCard("db_empty");
        ImGui::TextUnformatted(TR("No database yet."));
        ImGui::TextDisabled("%s", TR("Make one, or import a PGN file: a new database takes its name."));
        ui::endCard();
        return;
    }
    if (db_selected_ < 0 || db_selected_ >= static_cast<int>(entries.size())) db_selected_ = 0;

    // The list, and the chosen database beside it.
    const float list_w = std::min(px(380), ImGui::GetContentRegionAvail().x * 0.45f);
    ImGui::BeginChild("##db_list", ImVec2(list_w, 0), ImGuiChildFlags_Borders);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].source && (i == 0 || !entries[i - 1].source)) {
            ImGui::Dummy(ImVec2(0, px(6)));
            ImGui::TextDisabled("%s", TR("Read without a copy"));
            ImGui::Separator();
        }
        ImGui::PushID(static_cast<int>(i));
        if (entries[i].source) s.refresh(i);  // once listed: its count comes
        const bool refreshing = entries[i].source && s.refreshing(i);
        const auto facts      = s.facts(i);
        const bool on    = db_selected_ == static_cast<int>(i);
        const auto start = ImGui::GetCursorPos();
        if (ImGui::Selectable("##db", on, 0, ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 2.2f)))
            db_selected_ = static_cast<int>(i);
        ImGui::SetCursorPos(ImVec2(start.x + px(6), start.y + px(3)));
        ImGui::TextUnformatted(entries[i].name.c_str());
        ImGui::SetCursorPosX(start.x + px(6));
        if (!facts.error.empty()) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::BAD), "%s", TR("cannot be opened"));
        } else {
            auto line = trn("{} game", "{} games", static_cast<int>(std::min<uint64_t>(facts.games, 1000000000)),
                            ui::exactCount(facts.games));
            if (entries[i].source)
                line += entries[i].source->kind == study::LinkedSource::Kind::Tournament ? TR(" · tournament")
                                                                                         : TR(" · PGN file");
            if (refreshing) line += TR(" · bringing up to date…");
            ImGui::TextDisabled("%s", line.c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine(0, px(14));

    ImGui::BeginChild("##db_chosen", ImVec2(0, 0));
    const auto index  = static_cast<std::size_t>(db_selected_);
    const bool linked = entries[index].source.has_value();
    if (linked) s.refresh(index);  // brought up to date once it is shown
    const auto facts = s.facts(index);
    ui::pageTitle(entries[index].name.c_str());
    if (!facts.error.empty()) {
        ImGui::PushTextWrapPos();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::BAD), "%s", facts.error.c_str());
        ImGui::PopTextWrapPos();
    } else {
        if (ImGui::BeginTable("##db_facts", 2, ImGuiTableFlags_SizingFixedFit)) {
            const auto row = [](const std::string& value, const char* label) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(value.c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", label);
            };
            row(ui::exactCount(facts.games), TR("games"));
            row(ui::exactCount(facts.names), TR("names: players, events, sites, rounds"));
            row(formatBytes(facts.bytes), TR("on the disk"));
            ImGui::EndTable();
        }
        if (linked) {
            ImGui::PushTextWrapPos();
            ImGui::TextDisabled("%s", trf("Read from {} without a copy, brought up to date when shown.",
                                          entries[index].source->path.string())
                                          .c_str());
            ImGui::PopTextWrapPos();
        } else {
            ImGui::TextDisabled("%s", facts.tree ? TR("Opening tree up to date.")
                                                 : TR("Opening tree to build: after the next import."));
        }
    }
    ImGui::Dummy(ImVec2(0, px(6)));

    // An import, its progress.
    if (const auto* job = s.import()) {
        ui::beginCard("db_import_card");
        const bool here  = job->index == index;
        const auto where = job->index < entries.size() ? entries[job->index].name : std::string();
        if (job->building) {
            ImGui::TextUnformatted(trf("Building the opening tree of « {} »", where).c_str());
            const auto total = job->tree.total.load();
            ImGui::ProgressBar(total ? static_cast<float>(job->tree.done.load()) / static_cast<float>(total) : 0.0f,
                               ImVec2(-1, 0));
            ImGui::TextDisabled("%s", TR("The database stays open meanwhile."));
        } else {
            ImGui::TextUnformatted(trf("Import: {} into « {} »", job->file.filename().string(), where).c_str());
            const double seconds =
                std::max(0.001, std::chrono::duration<double>(std::chrono::steady_clock::now() - job->started).count());
            const auto bytes = job->progress.bytes.load(), games = job->progress.games.load();
            const auto total = job->progress.total_bytes;
            const float share =
                total ? static_cast<float>(static_cast<double>(bytes) / static_cast<double>(total)) : 0.0f;
            ImGui::ProgressBar(share, ImVec2(-1, 0),
                               fmt::format("{} % · {}/s · {}", static_cast<int>(share * 100),
                                           formatBytes(static_cast<uint64_t>(bytes / seconds)),
                                           trf("{} games/s", ui::formatCount(static_cast<uint64_t>(games / seconds))))
                                   .c_str());
            ImGui::TextDisabled("%s", TR("The games committed stay if the import stops: every 262 144, and at the end. "
                                         "The opening tree is built again afterwards."));
        }
        if (ImGui::Button(TR("Cancel")) || (here && ImGui::IsKeyPressed(ImGuiKey_Escape))) s.cancelImport();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("Esc"));
        ui::endCard();
        ImGui::Dummy(ImVec2(0, px(6)));
    }

    // What can be done with it.
    ImGui::BeginDisabled(!facts.error.empty());
    if (ui::iconButton("db_open_list", ui::Icon::Contents, TR("Open the list")) ||
        (!db_naming_ && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
        db_list_ = true;
        db_row_  = 0;
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0, px(4));
    ImGui::TextDisabled("%s", TR("Enter"));
    if (linked) {
        // A tournament's games or a file: shown where they are.
        ImGui::SameLine();
        if (ui::iconButton("db_source", ui::Icon::FolderClosed, TR("Open the folder"))) {
            const auto& path = entries[index].source->path;
            ui::openPath(entries[index].source->kind == study::LinkedSource::Kind::PgnFile ? path.parent_path() : path);
        }
        ImGui::EndChild();
        return;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(s.import() != nullptr || !facts.error.empty());
    if (ui::iconButton("db_import_here", ui::Icon::Folder, TR("Import into this database…")))
        dialogs_.openFile(Dialogs::Purpose::ImportGames, gameFiles());
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ui::iconButton("db_folder", ui::Icon::FolderClosed, TR("Open the folder"))) ui::openPath(entries[index].folder);
    ImGui::SameLine();
    ImGui::BeginDisabled(s.import() != nullptr && s.import()->index == index);
    if (ImGui::Button(TR("Remove from the list"))) {
        s.remove(index);
        db_selected_ = std::min<int>(db_selected_, static_cast<int>(s.entries().size()) - 1);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", TR("Its files stay where they are: Add a database… lists it again."));
    ImGui::EndChild();
}

void App::openListedGame(std::size_t row) {
    if (!db_view_.snapshot || row >= db_view_.ids.size()) return;
    const auto id   = db_view_.ids[row];
    const auto game = db_view_.snapshot->game(id);
    const auto name = shelf().entries()[static_cast<std::size_t>(db_selected_)].name;
    readOnBoard(study::importedPgn(game), trf("{}, game {}", name, ui::exactCount(id + 1)));
    page_ = Page::Board;
}

void App::drawGameList() {
    auto& s          = shelf();
    const auto index = static_cast<std::size_t>(db_selected_);
    const auto db    = s.open(index);
    if (!db) {
        db_list_ = false;
        return;
    }
    const auto snapshot = db->snapshot();

    // Where it is: Databases › Name · N games.
    if (ui::iconButton("db_back", ui::Icon::Back, TR("Databases")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        db_list_ = false;
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("›");
    ImGui::SameLine();
    ImGui::TextUnformatted(s.entries()[index].name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s",
                        trn("{} game", "{} games", static_cast<int>(std::min<uint64_t>(snapshot->games(), 1000000000)),
                            ui::exactCount(snapshot->games() - snapshot->deletedCount()))
                            .c_str());

    // The quick filter.
    ImGui::SetNextItemWidth(std::min(px(460), ImGui::GetContentRegionAvail().x));
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_L)) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##db_filter", TR("Quick filter: players, event, year, ECO, result"), db_filter_.data(),
                             db_filter_.size());
    ImGui::SameLine(0, px(4));
    ImGui::TextDisabled("Ctrl+L");
    ImGui::SameLine();
    if (ui::iconButton("db_search", ui::Icon::Search, db_search_open_ ? TR("Close the search") : TR("Search")) ||
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F))
        db_search_open_ = !db_search_open_;
    ImGui::SameLine(0, px(4));
    ImGui::TextDisabled("Ctrl+F");

    // The view, made again in the background when what it shows changed.
    const auto& p = db_search_;
    const auto of =
        fmt::format("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}", s.entries()[index].folder.string(),
                    snapshot->generation(), db_filter_.data(), static_cast<int>(db_sort_), db_descending_,
                    p.player.data(), p.opponent.data(), p.event.data(), p.side, p.year_from.data(), p.year_to.data(),
                    p.eco_from.data(), p.eco_to.data(), p.elo_min.data(), static_cast<int>(p.result),
                    p.position ? db_position_of_ + (db_position_ready_ ? "+" : "-") : std::string());
    if (db_view_of_ != of && (!db_view_job_ || db_view_job_->of != of)) {
        if (db_view_job_) *db_view_job_->cancel = true;
        ViewJob job;
        job.of            = of;
        job.cancel        = std::make_shared<std::atomic<bool>>(false);
        job.started       = std::chrono::steady_clock::now();
        const auto filter = listFilter();
        // The games through the position, once found: the others left out of the view.
        std::optional<std::vector<study::GameId>> through;
        if (db_search_.position && db_position_ready_) through = db_position_;
        job.done     = std::async(std::launch::async, [snapshot, filter, sort = db_sort_, down = db_descending_,
                                                       cancel = job.cancel, through = std::move(through)] {
            auto view = study::makeView(snapshot, filter, sort, down, 0, cancel.get());
            if (through)
                std::erase_if(view.ids, [&](study::GameId id) {
                    return !std::binary_search(through->begin(), through->end(), id);
                });
            return view;
        });
        db_view_job_ = std::move(job);
    }
    if (db_view_job_ && db_view_job_->done.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto view = db_view_job_->done.get();
        if (!*db_view_job_->cancel) {
            db_view_ms_ =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - db_view_job_->started)
                    .count();
            if (db_view_of_.substr(0, db_view_of_.find('|')) != of.substr(0, of.find('|')) ||
                view.ids.size() != db_view_.ids.size())
                db_row_ = 0;
            db_view_    = std::move(view);
            db_view_of_ = db_view_job_->of;
        }
        db_view_job_.reset();
    }
    ImGui::SameLine(0, px(16));
    if (db_view_job_)
        ImGui::TextDisabled("%s", TR("Looking…"));
    else
        ImGui::TextDisabled("%s", trf("{} · {} ms",
                                      trn("{} game", "{} games",
                                          static_cast<int>(std::min<std::size_t>(db_view_.ids.size(), 1000000000)),
                                          ui::exactCount(db_view_.ids.size())),
                                      static_cast<int>(db_view_ms_))
                                      .c_str());

    const auto& ids  = db_view_.ids;
    const auto* snap = db_view_.snapshot.get();
    if (!ids.empty()) db_row_ = std::min(db_row_, ids.size() - 1);

    // The list and, beside it, the game chosen.
    const float preview_w = std::min(px(300), ImGui::GetContentRegionAvail().x * 0.3f);
    constexpr auto flags  = ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable |
                            ImGuiTableFlags_SizingFixedFit;
    bool open             = false;
    if (ImGui::BeginTable("##db_games", 10, flags, ImVec2(ImGui::GetContentRegionAvail().x - preview_w - px(14), 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        const auto column = [](const char* name, study::SortBy sort, float width, bool stretch = false) {
            ImGui::TableSetupColumn(name,
                                    stretch ? ImGuiTableColumnFlags_WidthStretch : ImGuiTableColumnFlags_WidthFixed,
                                    width, static_cast<ImGuiID>(sort));
        };
        column(TR("No."), study::SortBy::Number, px(64));
        column(TR("White"), study::SortBy::White, px(130));
        column(TR("Elo"), study::SortBy::WhiteElo, px(44));
        column(TR("Black"), study::SortBy::Black, px(130));
        column(TR("Elo##black"), study::SortBy::BlackElo, px(44));
        column(TR("Result"), study::SortBy::Result, px(48));
        column(TR("ECO"), study::SortBy::Eco, px(40));
        column(TR("Moves"), study::SortBy::Plies, px(48));
        column(TR("Date"), study::SortBy::Date, px(82));
        column(TR("Event"), study::SortBy::Event, px(120), true);
        ImGui::TableHeadersRow();
        if (auto* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
            if (specs->SpecsCount > 0) {
                db_sort_       = static_cast<study::SortBy>(specs->Specs[0].ColumnUserID);
                db_descending_ = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
            }
            specs->SpecsDirty = false;
        }
        // The keys: up and down through the list, Enter opens; also while the filter is typed.
        if (!ids.empty()) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) db_row_ = std::min(db_row_ + 1, ids.size() - 1);
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && db_row_ > 0) --db_row_;
            if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) db_row_ = std::min(db_row_ + 20, ids.size() - 1);
            if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) db_row_ = db_row_ > 20 ? db_row_ - 20 : 0;
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) open = true;
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(ids.size()));
        clipper.IncludeItemByIndex(static_cast<int>(db_row_));
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                const auto id  = ids[static_cast<std::size_t>(r)];
                const auto row = snap->row(id);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(r);
                const bool chosen = static_cast<std::size_t>(r) == db_row_;
                if (ImGui::Selectable(ui::exactCount(id + 1).c_str(), chosen,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    db_row_ = static_cast<std::size_t>(r);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) open = true;
                }
                if (chosen && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow) ||
                               ImGui::IsKeyPressed(ImGuiKey_PageDown) || ImGui::IsKeyPressed(ImGuiKey_PageUp)))
                    ImGui::SetScrollHereY();
                ImGui::PopID();
                const auto text = [](std::string_view v) {
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(v.data(), v.data() + v.size());
                };
                text(row.white);
                ImGui::TableNextColumn();
                if (row.white_elo) ImGui::Text("%u", row.white_elo);
                text(row.black);
                ImGui::TableNextColumn();
                if (row.black_elo) ImGui::Text("%u", row.black_elo);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.result == '1'   ? "1-0"
                                       : row.result == '0' ? "0-1"
                                       : row.result == '=' ? "½-½"
                                                           : "*");
                ImGui::TableNextColumn();
                if (row.eco) ImGui::TextUnformatted(study::ecoText(row.eco).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%u", (row.plies + 1) / 2);
                ImGui::TableNextColumn();
                if (row.date) ImGui::TextUnformatted(study::dateText(row.date).c_str());
                text(row.event);
            }
        }
        ImGui::EndTable();
    }
    ImGui::SameLine(0, px(14));

    // The game chosen: its players, its moves, Open; or the search panel.
    ImGui::BeginChild("##db_preview", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (db_search_open_) {
        drawSearchPanel(snapshot);
    } else if (!ids.empty()) {
        const auto id  = ids[db_row_];
        const auto row = snap->row(id);
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(fmt::format("{} – {}", row.white, row.black).c_str());
        ImGui::TextDisabled("%s", fmt::format("{}{}{}", row.event, row.date ? " · " : "",
                                              row.date ? study::dateText(row.date) : std::string())
                                      .c_str());
        // Its moves, as the PGN gives them (the first ones).
        auto pgn         = study::importedPgn(snap->game(id));
        const auto moves = pgn.find("\n\n");
        auto text        = moves == std::string::npos ? pgn : pgn.substr(moves + 2);
        if (text.size() > 600) text = text.substr(0, text.rfind(' ', 600)) + " …";
        ImGui::Dummy(ImVec2(0, px(4)));
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, px(8)));
        if (ui::iconButton("db_open_game", ui::Icon::Board, TR("Open"))) open = true;
        ImGui::SameLine(0, px(4));
        ImGui::TextDisabled("%s", TR("Enter"));
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", TR("↑ ↓ go through the list · Enter opens · a click on a heading sorts"));
        ImGui::PopTextWrapPos();
    } else if (!db_view_job_) {
        ImGui::TextDisabled("%s", TR("No game matches."));
    }
    ImGui::EndChild();
    if (open) openListedGame(db_row_);
}

study::Filter App::listFilter() const {
    auto f         = quickFilter(db_filter_.data());
    const auto& p  = db_search_;
    const auto add = [](std::string& to, const char* words) {
        if (*words) to += (to.empty() ? "" : " ") + std::string(words);
    };
    if (p.side == 1)
        add(f.white, p.player.data());
    else if (p.side == 2)
        add(f.black, p.player.data());
    else
        add(f.player, p.player.data());
    if (p.side == 1)
        add(f.black, p.opponent.data());
    else if (p.side == 2)
        add(f.white, p.opponent.data());
    else
        add(f.words, p.opponent.data());
    add(f.words, p.event.data());
    const int from = std::atoi(p.year_from.data()), to = std::atoi(p.year_to.data());
    if (from > 0) f.date_from = study::packDate(fmt::format("{:04d}.01.01", from));
    if (to > 0) f.date_to = study::packDate(fmt::format("{:04d}.12.31", to));
    const auto eco = [](const char* code, char fill) {
        std::string c(code);
        if (c.empty()) return uint16_t{0};
        c[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(c[0])));
        while (c.size() < 3) c += fill;
        return study::packEco(c);
    };
    if (p.eco_from[0]) f.eco_from = eco(p.eco_from.data(), '0');
    if (p.eco_to[0] || p.eco_from[0]) f.eco_to = eco(p.eco_to[0] ? p.eco_to.data() : p.eco_from.data(), '9');
    if (const int elo = std::atoi(p.elo_min.data()); elo > 0) f.elo_min = static_cast<uint16_t>(std::min(elo, 4000));
    if (p.result) f.result = p.result;
    return f;
}

void App::drawSearchPanel(const std::shared_ptr<const study::Snapshot>& snapshot) {
    auto& p = db_search_;
    ImGui::TextUnformatted(TR("Search"));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("the list follows at each letter"));
    const float label_w = px(96);
    const auto label    = [&](const char* text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        ImGui::SameLine(label_w);
    };
    const auto field = [&](const char* text, const char* id, std::array<char, 64>& buffer) {
        label(text);
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText(id, buffer.data(), buffer.size());
    };
    field(TR("Player"), "##s_player", p.player);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + label_w);
    ImGui::SetNextItemWidth(-1);
    const char* sides[] = {TR("Either colour"), TR("White"), TR("Black")};
    ImGui::Combo("##s_side", &p.side, sides, 3);
    field(TR("Opponent"), "##s_opponent", p.opponent);
    field(TR("Event"), "##s_event", p.event);
    // The years, the ECO codes, the ratings, the result.
    label(TR("Years"));
    ImGui::SetNextItemWidth(px(60));
    ImGui::InputText("##s_from", p.year_from.data(), p.year_from.size(), ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    ImGui::TextUnformatted("–");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(60));
    ImGui::InputText("##s_to", p.year_to.data(), p.year_to.size(), ImGuiInputTextFlags_CharsDecimal);
    label(TR("ECO code"));
    ImGui::SetNextItemWidth(px(52));
    ImGui::InputText("##s_eco_from", p.eco_from.data(), p.eco_from.size(), ImGuiInputTextFlags_CharsUppercase);
    ImGui::SameLine();
    ImGui::TextUnformatted("–");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(52));
    ImGui::InputText("##s_eco_to", p.eco_to.data(), p.eco_to.size(), ImGuiInputTextFlags_CharsUppercase);
    label(TR("Elo"));
    ImGui::SetNextItemWidth(px(60));
    ImGui::InputText("##s_elo", p.elo_min.data(), p.elo_min.size(), ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("at least, both players"));
    label(TR("Result"));
    const char* results[] = {TR("All"), "1-0", "½-½", "0-1"};
    const char values[]   = {0, '1', '=', '0'};
    int result            = static_cast<int>(std::find(std::begin(values), std::end(values), p.result) - values);
    ImGui::SetNextItemWidth(px(100));
    if (ImGui::Combo("##s_result", &result, results, 4)) p.result = values[result];
    ImGui::Separator();

    // The Board page's position: the games that reach it, found in the background.
    ImGui::Checkbox(TR("The Board page's position"), &p.position);
    if (p.position) {
        const auto fen = boardFen();
        const auto of  = fmt::format("{}|{}", snapshot->generation(), fen);
        if (of != db_position_of_) {
            db_position_search_.reset();
            db_position_ready_ = false;
            db_position_.clear();
            db_position_of_ = of;
            study::ScanBoard board;
            if (board.setFen(fen, board_.chess960))
                db_position_search_ = std::make_unique<study::Search>(snapshot, study::PositionQuery{board});
        }
        if (db_position_search_ && !db_position_ready_ && db_position_search_->done()) {
            for (const auto& hit : db_position_search_->hits()) db_position_.push_back(hit.id);
            std::sort(db_position_.begin(), db_position_.end());
            db_position_.erase(std::unique(db_position_.begin(), db_position_.end()), db_position_.end());
            db_position_ready_ = true;
        }
        ImGui::PushTextWrapPos();
        const auto shown =
            board_.ply > 0 ? trf("After {}.", boardMoveLabel(board_.ply)) : std::string(TR("The start position."));
        ImGui::TextDisabled("%s", shown.c_str());
        if (!db_position_ready_ && db_position_search_) {
            const auto total = std::max<uint64_t>(1, db_position_search_->total());
            ImGui::ProgressBar(static_cast<float>(db_position_search_->searched()) / static_cast<float>(total),
                               ImVec2(-1, 0));
        } else {
            ImGui::TextUnformatted(trn("{} game reaches it", "{} games reach it",
                                       static_cast<int>(std::min<std::size_t>(db_position_.size(), 1000000000)),
                                       ui::exactCount(db_position_.size()))
                                       .c_str());
        }
        ImGui::TextDisabled("%s", TR("The games that cannot reach it (their pawns, their material) are left out "
                                     "without being replayed."));
        ImGui::PopTextWrapPos();
    } else if (db_position_search_ || !db_position_of_.empty()) {
        db_position_search_.reset();
        db_position_of_.clear();
        db_position_ready_ = false;
    }
    ImGui::Separator();
    if (ImGui::Button(TR("Clear everything"))) {
        p = SearchFields{};
        db_filter_.fill(0);
    }
}

}  // namespace fcg
