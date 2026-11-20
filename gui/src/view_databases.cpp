// The Databases page of the Chess space (mockup M1, board 1): the databases of the profile, the chosen one's facts
// and what can be done with it, an import with its progress, a file dropped on the page.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>  // SetKeyOwner

#include <study/import.hpp>
#include <tour/files.hpp>

#include "app.hpp"
#include "board_text.hpp"
#include "board_view.hpp"
#include "console.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "machines.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

// Where a database is, as M1 writes it: from the home folder when it is in it, "Documents › RookStudio › Mega.fcgdb".
std::string folderText(const std::filesystem::path& folder) {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    std::filesystem::path shown = folder;
    if (home && *home) {
        std::error_code ec;
        const auto relative = std::filesystem::relative(folder, home, ec);
        if (!ec && !relative.empty() && *relative.begin() != "..") shown = relative;
    }
    std::string text;
    for (const auto& part : shown) {
        const auto u8 = part.u8string();
        const std::string piece(u8.begin(), u8.end());
        if (piece.empty() || piece == "/" || piece == "\\") continue;
        text += text.empty() ? piece : " › " + piece;
    }
    return text;
}

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

void App::importIntoDatabase(int index, const std::filesystem::path& file, bool pasted) {
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
    // What the database had before: the games added, and how much more the rest would take if the disk fills up.
    ImportStart start;
    start.folder = s.entries()[static_cast<std::size_t>(index)].folder;
    start.pasted = pasted;
    if (const auto db = s.open(static_cast<std::size_t>(index))) start.games = db->snapshot()->games();
    start.bytes = s.facts(static_cast<std::size_t>(index)).bytes;
    if (!s.startImport(static_cast<std::size_t>(index), file)) {
        const auto facts = s.facts(index);
        db_message_      = s.import() ? TR("An import runs already: one at a time.")
                           : facts.read_only
                               ? trf("“{}” is read-only while another program writes to it: import into it once it "
                                     "has finished.",
                                     s.entries()[static_cast<std::size_t>(index)].name)
                               : facts.error;
    } else {
        db_message_.clear();
        db_outcome_.reset();
        db_import_start_ = std::move(start);
    }
}

void App::pasteGamesInto(int index) {
    char* clip             = SDL_GetClipboardText();
    const std::string text = clip ? clip : "";
    SDL_free(clip);
    if (offerLichessLink(text, true)) return;  // a Lichess link: its card (#47)
    if (study::splitGames(text).empty()) {
        db_message_ = TR("No game in the clipboard.");
        return;
    }
    std::error_code ec;
    const auto dir = storage_.root() / "pasted";
    std::filesystem::create_directories(dir, ec);
    const auto file = dir / fmt::format("pasted-{}.pgn", std::chrono::system_clock::now().time_since_epoch().count());
    try {
        tour::replaceFile(file, text);
    } catch (const std::exception& e) {
        db_message_ = e.what();
        return;
    }
    importIntoDatabase(index, file, true);
}

void App::pollDatabases() {
    if (!shelf_) return;
    shelf_->pollTreeJob();
    shelf_->keepReadOnlyCurrent();
    const auto* job = shelf_->import();
    if (!job) return;
    const auto file  = job->file.filename().string();
    const auto path  = job->file;
    const auto index = job->index;
    const auto read = job->progress.bytes.load(), total = job->progress.total_bytes;
    if (const auto report = shelf_->finishedImport()) {
        if (twicImportEnded(path, *report)) return;  // an issue of TWIC: its own report, once they are all in
        // The outcome, said on the database: the disk full and what the rest would take, the damaged games and why.
        ImportOutcome o;
        o.file           = path;
        o.report         = *report;
        const auto start = db_import_start_.value_or(ImportStart{});
        o.pasted         = start.pasted;
        if (index < shelf_->entries().size()) {
            o.folder = shelf_->entries()[index].folder;
            if (!report->error.empty()) {
                std::error_code ec;
                const auto space = std::filesystem::space(o.folder, ec);
                o.disk_full      = !ec && space.available < (uint64_t{64} << 20);
                const auto now   = shelf_->facts(index).bytes;
                if (read > 0 && total > read && now > start.bytes)
                    o.needed =
                        static_cast<uint64_t>(static_cast<double>(total - read) *
                                              static_cast<double>(now - start.bytes) / static_cast<double>(read));
            }
            if (report->read.damaged > 0)
                if (const auto db = shelf_->open(index)) {
                    const auto snap = db->snapshot();
                    for (auto id = start.games; id < snap->games() && o.damaged.size() < 3; ++id) {
                        if (!(snap->numbers(id).flags & study::GAME_DAMAGED)) continue;
                        const auto game   = snap->game(id);
                        const auto damage = study::damageOf(game);
                        const auto kept   = (game.plies + 1) / 2;
                        std::string why;
                        if (damage.bad_start)
                            why = TR("its start position (FEN) is not a position; kept without its moves");
                        else if (damage.move.empty())
                            why = trf("its text stops there; kept up to move {}", kept);
                        else if (damage.cut)
                            why = trf("its text is cut in “{}{}{}”; kept up to move {}", game.plies / 2 + 1,
                                      game.plies % 2 ? "…" : ".", damage.move, kept);
                        else if (kept == 0)
                            why = trf("“{}.{}” is not a legal move; kept without its moves", 1, damage.move);
                        else
                            why = trf("“{}{}{}” is not a legal move; kept up to move {}", game.plies / 2 + 1,
                                      game.plies % 2 ? "…" : ".", damage.move, kept);
                        uint64_t line  = 0;
                        const auto key = snap->key(id);
                        for (const auto& [k, l] : report->damaged_lines)
                            if (k == key) {
                                line = l;
                                break;
                            }
                        o.damaged.push_back({id, std::move(why), line, damage.move});
                    }
                }
        }
        if (o.pasted) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
        db_outcome_ = std::move(o);
        db_import_start_.reset();
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
            db_message_ = trn("Import of {} done: {} game added", "Import of {} done: {} games added", report->added,
                              file, ui::exactCount(report->added));
            if (report->duplicates > 0)
                db_message_ += trf(", {} already there left out", ui::exactCount(report->duplicates));
            if (report->read.damaged > 0)
                db_message_ +=
                    trf(", {} damaged (kept up to the move that cannot be read)", ui::exactCount(report->read.damaged));
            db_message_ += ".";
        }
        Console::instance().add(db_message_, !report->error.empty());
        db_message_.clear();  // said by the banner on the database
    }
}

void App::openFileLines(const std::filesystem::path& file, uint64_t line, std::string move) {
    closeFileLines();
    FileLines v;
    v.file    = file;
    v.line    = line;
    v.move    = std::move(move);
    v.cancel  = std::make_shared<std::atomic<bool>>(false);
    v.reading = std::async(std::launch::async,
                           [file, line, cancel = v.cancel] { return study::readLines(file, line, 80, *cancel); });
    db_lines_ = std::move(v);
}

void App::closeFileLines() {
    if (!db_lines_) return;
    if (db_lines_->cancel) *db_lines_->cancel = true;
    db_lines_.reset();  // the reading stops within a megabyte
}

namespace {

// The line, among a game's, that holds `move` as a word of its moves (after its tags); for a start position that is
// not one, its FEN tag. -1 when none.
int markedLine(const std::vector<std::string>& lines, std::string_view move) {
    const auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; };
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string_view line = lines[i];
        if (move.empty()) {
            if (line.starts_with("[FEN ")) return static_cast<int>(i);
            continue;
        }
        if (line.starts_with('[')) continue;
        for (auto at = line.find(move); at != std::string_view::npos; at = line.find(move, at + 1)) {
            const bool before = at == 0 || !word(line[at - 1]);
            const bool after  = at + move.size() == line.size() || !word(line[at + move.size()]);
            if (before && after) return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace

void App::drawFileLines() {
    auto& v = *db_lines_;
    if (!v.ready && v.reading.valid() && v.reading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            v.lines = v.reading.get();
        } catch (const std::exception& e) {
            v.error = e.what();
        }
        // The game alone: its lines end where the next game's tags start, the blank lines between them left out.
        bool moves = false;
        for (std::size_t i = 0; i < v.lines.size(); ++i) {
            const std::string_view line = v.lines[i];
            if (moves && line.starts_with('[')) {
                v.lines.resize(i);
                break;
            }
            moves = moves || (!line.empty() && !line.starts_with('['));
        }
        while (!v.lines.empty() && v.lines.back().find_first_not_of(" \t") == std::string::npos) v.lines.pop_back();
        v.ready = true;
    }
    ui::beginCard("db_file_lines");
    ImGui::TextUnformatted(trf("“{}”, from line {}", v.file.filename().string(), ui::exactCount(v.line)).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Open the file"))) ui::openPath(v.file);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "%s",
            trf("With the program the system gives it; the game starts at line {}.", ui::exactCount(v.line)).c_str());
    ImGui::SameLine();
    bool close = ImGui::SmallButton((std::string(TR("Close")) + "##file_lines").c_str());
    if (!v.ready) {
        ImGui::TextDisabled("%s", TR("Reading the file…"));
    } else if (!v.error.empty()) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::BAD), "%s",
                           trf("The file cannot be read: {}.", v.error).c_str());
    } else if (v.lines.empty()) {
        ImGui::TextDisabled("%s", TR("The file no longer has this line: it has changed since the import."));
    } else {
        const int marked   = markedLine(v.lines, v.move);
        const float line_h = ImGui::GetTextLineHeightWithSpacing();
        const auto shown   = std::min<std::size_t>(v.lines.size(), 16);
        const auto last    = ui::exactCount(v.line + v.lines.size() - 1);
        const float gutter = ImGui::CalcTextSize(last.c_str()).x + px(12);
        ImGui::BeginChild("##file_lines_text", ImVec2(0, line_h * static_cast<float>(shown) + px(8)),
                          ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushFont(ui::monoFont());
        for (std::size_t i = 0; i < v.lines.size(); ++i) {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            if (static_cast<int>(i) == marked)
                ImGui::GetWindowDrawList()->AddRectFilled(
                    at, ImVec2(at.x + ImGui::GetContentRegionAvail().x + ImGui::GetScrollMaxX(), at.y + line_h),
                    (ui::WARN & 0x00FFFFFF) | 0x40000000);
            ImGui::TextDisabled("%s", ui::exactCount(v.line + i).c_str());
            ImGui::SameLine(gutter);
            ImGui::TextUnformatted(v.lines[i].c_str());
        }
        ImGui::PopFont();
        ImGui::EndChild();
        if (marked >= 0)
            ImGui::TextDisabled("%s", (v.move.empty() ? std::string(TR("Marked: the start position that is not one."))
                                                      : trf("Marked: the line of “{}”.", v.move))
                                          .c_str());
    }
    ui::endCard();
    ImGui::Dummy(ImVec2(0, px(6)));
    if (close) closeFileLines();
}

void App::drawEmptyDatabase(std::size_t index) {
    ui::beginCard("db_empty");
    ImGui::TextUnformatted(trf("“{}” has no game yet", shelf().entries()[index].name).c_str());
    ImGui::TextDisabled("%s", TR("Drop a PGN, .zst, .gz or .zip file here, or:"));
    if (ui::iconButton("db_empty_import", ui::Icon::Folder, TR("Import a file…"))) {
        db_selected_ = static_cast<int>(index);
        dialogs_.openFile(Dialogs::Purpose::ImportGames, gameFiles());
    }
    ImGui::SameLine();
    if (ui::iconButton("db_empty_paste", ui::Icon::Paste, TR("Paste games"))) pasteGamesInto(static_cast<int>(index));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("The games of the clipboard (a PGN text) imported into it."));
    ui::endCard();
}

void App::drawImportOutcome(std::size_t index) {
    const auto& o    = *db_outcome_;
    const auto name  = shelf().entries()[index].name;
    const auto file  = o.pasted ? std::string(TR("the games pasted")) : o.file.filename().string();
    const auto added = ui::exactCount(o.report.added);
    ui::beginCard("db_outcome");
    ImGui::PushTextWrapPos();
    bool resume = false;
    if (!o.report.error.empty()) {
        if (o.disk_full) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", TR("Import stopped: the disk is full."));
            ImGui::TextUnformatted(trf("The first {} games are in “{}”, the database is intact.", added, name).c_str());
            const auto rest =
                (o.needed > 0 ? trf("About {} more is needed for the rest. ", formatBytes(o.needed)) : std::string()) +
                TR("Once room is made, resuming the import adds only the missing games.");
            ImGui::TextDisabled("%s", rest.c_str());
        } else {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::BAD), "%s",
                               trf("Import of {} stopped: {}.", file, o.report.error).c_str());
            ImGui::TextDisabled("%s", trf("The {} games committed before stay; resuming the import adds only the "
                                          "missing ones.",
                                          added)
                                          .c_str());
        }
        resume = !o.pasted && ImGui::Button(TR("Resume the import"));
        ImGui::SameLine();
        if (ImGui::Button(TR("Open the database's folder"))) ui::openPath(o.folder);
    } else if (o.report.read.cancelled) {
        ImGui::TextUnformatted(trf("Import of {} stopped at your request: {} games kept.", file, added).c_str());
        ImGui::TextDisabled("%s", TR("Resuming it adds only the missing games."));
        resume = !o.pasted && ImGui::Button(TR("Resume the import"));
    } else {
        const auto damaged = o.report.read.damaged;
        ImGui::TextUnformatted(
            (damaged > 0 ? trf("Import of {} done: {} games, {} of them damaged.", file, added, ui::exactCount(damaged))
                         : trf("Import of {} done: {} games added.", file, added))
                .c_str());
        if (o.report.duplicates > 0)
            ImGui::TextDisabled("%s", trn("{} game already there left out.", "{} games already there left out.",
                                          o.report.duplicates, ui::exactCount(o.report.duplicates))
                                          .c_str());
        if (damaged > 0) {
            ImGui::TextDisabled("%s", TR("Kept up to the move that cannot be read, their text kept as it was."));
            // A game's line opens the file there, unless its text was pasted: that file is gone.
            const auto openable = [&](const ImportOutcome::Damaged& d) { return d.line > 0 && !o.pasted; };
            for (const auto& d : o.damaged) {
                ImGui::BulletText("%s", (d.line > 0 ? trf("Game {}, line {}: {}.", ui::exactCount(d.id + 1),
                                                          ui::exactCount(d.line), d.why)
                                                    : trf("Game {}: {}.", ui::exactCount(d.id + 1), d.why))
                                            .c_str());
                if (openable(d) && ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::SetTooltip("%s", TR("A click shows the file at this line."));
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) openFileLines(o.file, d.line, d.move);
                }
            }
            if (damaged > o.damaged.size())
                ImGui::TextDisabled("%s", trf("and {} more", ui::exactCount(damaged - o.damaged.size())).c_str());
            if (ImGui::Button(TR("See them in the list"))) {
                db_search_         = SearchFields{};
                db_search_.damaged = true;  // said by the search panel, open on its headers
                db_filter_.fill(0);
                db_search_open_ = true;
                search_tab_     = 0;
                db_list_        = true;
                db_row_         = 0;
            }
            ImGui::SameLine();
            if (!o.damaged.empty() && openable(o.damaged.front())) {
                if (ImGui::Button(TR("Open the file at this line")))
                    openFileLines(o.file, o.damaged.front().line, o.damaged.front().move);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s",
                                      trf("Line {}, where game {} starts.", ui::exactCount(o.damaged.front().line),
                                          ui::exactCount(o.damaged.front().id + 1))
                                          .c_str());
                ImGui::SameLine();
            }
        }
    }
    if (ImGui::SmallButton(TR("Close"))) {
        db_outcome_.reset();
        closeFileLines();
    }
    ImGui::PopTextWrapPos();
    ui::endCard();
    ImGui::Dummy(ImVec2(0, px(6)));
    if (resume) {
        const auto path = o.file;
        importIntoDatabase(static_cast<int>(index), path);
    }
}

void App::drawDatabases() {
    auto& s = shelf();
    // A Lichess link pasted here (#47): its card, its games offered for the database chosen.
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) {
        char* clip             = SDL_GetClipboardText();
        const std::string text = clip ? clip : "";
        SDL_free(clip);
        if (!offerLichessLink(text, true) && !study::splitGames(text).empty() && db_selected_ >= 0)
            pasteGamesInto(db_selected_);
    }
    drawLinkCard();
    if (db_list_ && db_selected_ >= 0 && db_selected_ < static_cast<int>(s.entries().size())) {
        drawGameList();
        return;
    }
    db_list_ = false;
    ui::pageTitle(TR("Databases"), guideSection());
    // The tools, on the title's line.
    {
        const char* make    = TR("New database");
        const char* import  = TR("Import a file…");
        const char* sources = TR("From a source…");
        const char* add     = TR("Add a database…");
        float width         = px(36);
        for (const char* label : {make, import, sources, add}) width += ImGui::CalcTextSize(label).x + px(44);
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
        // The sources of games (#70, mockup M10): each with what it brings, its terms, the database it goes into.
        if (ui::iconButton("db_sources", ui::Icon::Globe, sources)) ImGui::OpenPopup("##sources");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("Games from a site, downloaded when you ask: TWIC's issues of the week."));
        drawSourcesMenu();
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
            db_naming_      = false;
            db_named_frame_ = ImGui::GetFrameCount();  // its Enter does not open the new database's list
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
    drawTwicReport();
    if (twic_sheet_) {
        drawTwicSheet();
        return;
    }
    if (broadcast_sheet_) {
        drawBroadcastSheet();
        return;
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
            if (!entries[i].source && facts.changed) line += " · " + trf("changed {}", ui::dayText(facts.changed));
            ImGui::TextDisabled("%s", line.c_str());
        }
        ImGui::PopID();
    }
    drawFollowedSources();
    ImGui::EndChild();
    ImGui::SameLine(0, px(14));

    ImGui::BeginChild("##db_chosen", ImVec2(0, 0));
    const auto index  = static_cast<std::size_t>(db_selected_);
    const bool linked = entries[index].source.has_value();
    if (linked) s.refresh(index);  // brought up to date once it is shown
    const auto facts = s.facts(index);
    ui::pageTitle(entries[index].name.c_str());
    if (!linked) {
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", folderText(entries[index].folder).c_str());
        ImGui::PopTextWrapPos();
    }
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

    // Read-only (M1, board 4): another program writes to it.
    if (facts.read_only) {
        ui::beginCard("db_read_only");
        ImGui::PushTextWrapPos();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           trf("“{}” is open read-only.", entries[index].name).c_str());
        ImGui::TextUnformatted(TR("Another program writes to it: another RookStudio importing, for instance."));
        ImGui::TextDisabled("%s",
                            TR("The list, the tree and the searches work; importing and saving games into it wait "
                               "until it has finished. The database takes in what that program commits as it "
                               "goes, and is written here again once it lets it go."));
        ImGui::PopTextWrapPos();
        ui::endCard();
        ImGui::Dummy(ImVec2(0, px(6)));
    }

    // The last import's outcome; or, a database of its own with no game, what to put in it (M1, board 4).
    if (db_outcome_ && db_outcome_->folder == entries[index].folder) {
        drawImportOutcome(index);
        if (db_lines_) drawFileLines();
    } else if (!linked && facts.error.empty() && facts.games == 0 && !(s.import() && s.import()->index == index)) {
        drawEmptyDatabase(index);
        ImGui::Dummy(ImVec2(0, px(6)));
    }

    // An import, its progress.
    if (const auto* job = s.import()) {
        ui::beginCard("db_import_card");
        const bool here  = job->index == index;
        const auto where = job->index < entries.size() ? entries[job->index].name : std::string();
        if (job->building) {
            ImGui::TextUnformatted(trf("Building the opening tree of “{}”", where).c_str());
            const auto total = job->tree.total.load();
            ImGui::ProgressBar(total ? static_cast<float>(job->tree.done.load()) / static_cast<float>(total) : 0.0f,
                               ImVec2(-1, 0));
            ImGui::TextDisabled("%s", TR("The database stays open meanwhile."));
        } else {
            ImGui::TextUnformatted(trf("Import: {} into “{}”", job->file.filename().string(), where).c_str());
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
            ImGui::TextDisabled("%s",
                                TR("The games committed stay if the import stops: every 262,144 games, and at the end. "
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
    if (!db_naming_ && !ImGui::GetIO().WantTextInput) {
        ImGui::SetKeyOwner(ImGuiKey_Enter, ImGui::GetID("##db_shelf_keys"));  // Enter is this page's: the list
        ImGui::SetKeyOwner(ImGuiKey_KeypadEnter, ImGui::GetID("##db_shelf_keys"));
    }
    if (ui::iconButton("db_open_list", ui::Icon::Contents, TR("Open the list")) ||
        (!db_naming_ && db_named_frame_ != ImGui::GetFrameCount() && !linkCardTakesKeys() &&
         ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
        db_list_ = true;
        db_row_  = 0;
    }
    ImGui::SameLine(0, px(4));
    ImGui::TextDisabled("%s", TR("Enter"));
    ImGui::SameLine();
    if (ui::iconButton("db_search_here", ui::Icon::Search, TR("Search"))) {
        db_list_        = true;
        db_row_         = 0;
        db_search_open_ = true;
    }
    ImGui::SameLine(0, px(4));
    ImGui::TextDisabled("Ctrl+F");
    ImGui::EndDisabled();
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
    if (!db_view_->snapshot || row >= db_view_->ids.size()) return;
    const auto& entry = shelf().entries()[static_cast<std::size_t>(db_selected_)];
    BoardOrigin origin;
    origin.folder = entry.folder;
    origin.name   = entry.name;
    origin.linked = entry.source.has_value();
    origin.view   = db_view_;
    origin.filter = db_filter_.data();
    board_origin_ = std::move(origin);
    openListRow(row);
}

void App::openListRow(std::size_t row) {
    if (!board_origin_ || !board_origin_->view || row >= board_origin_->view->ids.size()) return;
    // The position shown, to open the next game at it when it passes through it.
    const auto shown = boardAt(board_.game, board_.chess960, board_.ply).hash();
    const bool keep  = board_.ply > 0;
    auto origin      = *board_origin_;
    const auto id    = origin.view->ids[row];
    origin.row       = row;
    origin.id        = id;
    // A game left with changes not saved comes back with them, where it was left.
    if (const auto copy = changedCopyOf(origin.folder, id)) {
        restoreRecentGame(*copy);
        board_origin_ = std::move(origin);  // in the list it is reached from now
        return;
    }
    readOnBoard(study::importedPgn(origin.view->snapshot->game(id)),
                trf("{}, game {}", origin.name, ui::exactCount(id + 1)));
    board_origin_ = std::move(origin);
    page_         = Page::Board;
    if (keep) {
        auto board = boardAt(board_.game, board_.chess960, 0);
        for (int ply = 0; ply <= static_cast<int>(board_.game.moves.size()); ++ply) {
            if (board.hash() == shown) {
                board_.ply = ply;
                break;
            }
            if (ply < static_cast<int>(board_.game.moves.size()))
                board.makeMove(board_.game.moves[static_cast<std::size_t>(ply)].move);
        }
    }
}

bool App::saveBoardToDatabase() {
    if (!board_origin_ || board_origin_->linked) return false;
    auto& s = shelf();
    for (std::size_t i = 0; i < s.entries().size(); ++i) {
        if (s.entries()[i].folder != board_origin_->folder) continue;
        const auto db = s.open(i);
        if (!db) return false;
        if (db->readOnly()) {
            boardToast(trf("“{}” is read-only for now, another program writes to it: the game stays changed here.",
                           board_origin_->name));
            return true;
        }
        fillBoardEco();
        study::ImportedGame game;
        if (!study::readImportedGame(gamePgn(board_.tree, board_.chess960), game)) return false;
        const auto id = board_origin_->id;
        try {
            db->replace(id, std::move(game));
        } catch (const std::exception& e) {
            boardToast(e.what());
            return true;
        }
        board_.changes.saved(trf("“{}”", board_origin_->name));
        s.rebuildTree(i);  // the game replaced made its opening tree stale
        boardToast(trf("Saved in “{}”, game {}", board_origin_->name, ui::exactCount(id + 1)));
        return true;
    }
    return false;
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
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup);
    if (escape && db_search_open_) {
        db_search_open_ = false;  // the panel first, then the list
    } else if (ui::iconButton("db_back", ui::Icon::Back, TR("Databases")) || escape) {
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
    // None yet: what to put in it, in place of an empty list (M1, board 4).
    if (snapshot->games() == 0 && !s.entries()[index].source && !(s.import() && s.import()->index == index)) {
        ImGui::Dummy(ImVec2(0, px(8)));
        drawEmptyDatabase(index);
        return;
    }

    // The quick filter.
    ImGui::SetNextItemWidth(std::min(px(460), ImGui::GetContentRegionAvail().x));
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_L)) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##db_filter", TR("Quick filter: players, event, year, ECO, result"), db_filter_.data(),
                             db_filter_.size());
    // The list's keys work while the quick filter is typed, not while another field is (the search panel's).
    const bool list_keys = !ImGui::GetIO().WantTextInput || ImGui::IsItemActive();
    ImGui::SameLine(0, px(4));
    ImGui::TextDisabled("Ctrl+L");
    ImGui::SameLine();
    if (ui::iconButton("db_search", ui::Icon::Search, db_search_open_ ? TR("Close the search") : TR("Search")) ||
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F))
        db_search_open_ = !db_search_open_;
    ImGui::SameLine(0, px(4));
    ImGui::TextDisabled("Ctrl+F");

    // The view, made again in the background when what it shows changed: narrowed by the searches set (the position
    // added to the filter, a material, a pattern) once they are done.
    pollSearches(snapshot, s.entries()[index].folder.string());
    const auto& p = db_search_;
    std::vector<const SearchJob*> narrowing_jobs;
    if (p.position) narrowing_jobs.push_back(&db_position_job_);
    if (!db_material_job_.of.empty()) narrowing_jobs.push_back(&db_material_job_);
    if (!db_pattern_job_.of.empty()) narrowing_jobs.push_back(&db_pattern_job_);
    std::string narrowing;
    for (const auto* job : narrowing_jobs) narrowing += job->of + (job->ready ? "+" : "-");
    const auto of = fmt::format("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}", s.entries()[index].folder.string(),
                                snapshot->generation(), db_filter_.data(), static_cast<int>(db_sort_), db_descending_,
                                p.player.data(), p.opponent.data(), p.event.data(), p.side, p.year_from.data(),
                                p.year_to.data(), p.eco_from.data(), p.eco_to.data(), p.elo_min.data(),
                                static_cast<int>(p.result), narrowing + (p.damaged ? "|damaged" : ""));
    if (db_view_of_ != of && (!db_view_job_ || db_view_job_->of != of)) {
        if (db_view_job_) *db_view_job_->cancel = true;
        ViewJob job;
        job.of            = of;
        job.cancel        = std::make_shared<std::atomic<bool>>(false);
        job.started       = std::chrono::steady_clock::now();
        const auto filter = listFilter();
        // The games the searches found, those of all of them: the others left out of the view.
        std::optional<std::vector<study::GameId>> through;
        for (const auto* job : narrowing_jobs) {
            if (!job->ready) continue;
            if (!through) {
                through = job->ids;
                continue;
            }
            std::vector<study::GameId> both;
            std::set_intersection(through->begin(), through->end(), job->ids.begin(), job->ids.end(),
                                  std::back_inserter(both));
            through = std::move(both);
        }
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
                view.ids.size() != db_view_->ids.size())
                db_row_ = 0;
            db_view_    = std::make_shared<study::View>(std::move(view));
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
                                          static_cast<int>(std::min<std::size_t>(db_view_->ids.size(), 1000000000)),
                                          ui::exactCount(db_view_->ids.size())),
                                      static_cast<int>(db_view_ms_))
                                      .c_str());

    const auto& ids  = db_view_->ids;
    const auto* snap = db_view_->snapshot.get();
    if (!ids.empty()) db_row_ = std::min(db_row_, ids.size() - 1);

    // The list and, beside it, the game chosen.
    const float preview_w = db_search_open_ ? std::min(px(420), ImGui::GetContentRegionAvail().x * 0.42f)
                                            : std::min(px(300), ImGui::GetContentRegionAvail().x * 0.3f);
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
        // The keys: up and down through the list, Enter opens; also while the filter is typed. They are the
        // list's: claimed before they are pressed, ImGui's keyboard navigation leaves them alone, its cursor not
        // shown on a button that Enter would press too.
        if (list_keys) {
            const auto owner = ImGui::GetID("##db_list_keys");
            for (const auto key : {ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_PageUp, ImGuiKey_PageDown,
                                   ImGuiKey_Enter, ImGuiKey_KeypadEnter})
                ImGui::SetKeyOwner(key, owner);
        }
        if (!ids.empty() && list_keys) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) db_row_ = std::min(db_row_ + 1, ids.size() - 1);
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && db_row_ > 0) --db_row_;
            if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) db_row_ = std::min(db_row_ + 20, ids.size() - 1);
            if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) db_row_ = db_row_ > 20 ? db_row_ - 20 : 0;
            if (!linkCardTakesKeys() && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) open = true;
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
                // A player's name: their FIDE card under the mouse once known, asked by a right click (#57).
                const auto player = [&](std::string_view name, bool white) {
                    text(name);
                    if (name.empty()) return;
                    // Over the row's selectable, which spans the columns: its hover allowed.
                    constexpr auto over = ImGuiHoveredFlags_AllowWhenOverlappedByItem;
                    if (ImGui::IsItemHovered(over) && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        int64_t fide = 0;
                        for (const auto& [tag, value] : snap->game(id).tags)
                            if (tag == (white ? "WhiteFideId" : "BlackFideId")) fide = std::atoll(value.c_str());
                        askFideCard(std::string(name), fide);
                    }
                    if (!ImGui::IsItemHovered(over | ImGuiHoveredFlags_DelayNormal)) return;
                    if (const auto* card = fideCards().byName(name)) {
                        ImGui::BeginTooltip();
                        drawFideCard(*card);
                        ImGui::EndTooltip();
                    } else {
                        ImGui::SetTooltip("%s", TR("A right click: their FIDE card, asked of Lichess (the name only, "
                                                   "without an account)."));
                    }
                };
                player(row.white, true);
                ImGui::TableNextColumn();
                if (row.white_elo) ImGui::Text("%u", row.white_elo);
                player(row.black, false);
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
        ui::TopicRegion topic(guide::topics::Searching);
        drawSearchPanel(snapshot);
    } else if (!ids.empty()) {
        const auto id  = ids[db_row_];
        const auto row = snap->row(id);
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(fmt::format("{} – {}", row.white, row.black).c_str());
        // Their FIDE cards, known or asked for by a click (#57).
        for (const bool white : {true, false}) {
            const std::string name(white ? row.white : row.black);
            if (name.empty() || name == "?") continue;
            ImGui::PushID(white ? "fide_white" : "fide_black");
            if (const auto* card = fideCards().byName(name)) {
                const auto line =
                    fmt::format("{}{} · {} · {}", card->title.empty() ? "" : card->title + " ", card->name,
                                card->federation, card->standard > 0 ? std::to_string(card->standard) : "–");
                ImGui::TextDisabled("%s", line.c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    drawFideCard(*card);
                    ImGui::EndTooltip();
                }
            } else if (ImGui::SmallButton(trf("FIDE card of {}", name).c_str())) {
                int64_t fide = 0;
                for (const auto& [tag, value] : snap->game(id).tags)
                    if (tag == (white ? "WhiteFideId" : "BlackFideId")) fide = std::atoll(value.c_str());
                askFideCard(name, fide);
            }
            ImGui::PopID();
        }
        std::string where(row.event);
        const auto add = [&](const std::string& part) {
            if (part.empty()) return;
            where += where.empty() ? part : " · " + part;
        };
        if (row.date) add(study::dateText(row.date));
        add(row.result == '1' ? "1-0" : row.result == '0' ? "0-1" : row.result == '=' ? "½-½" : "*");
        if (row.eco) add(study::ecoText(row.eco));
        ImGui::TextDisabled("%s", where.c_str());
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
    drawFidePopup();  // a FIDE card asked for (#57)
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
    f.damaged = p.damaged;
    return f;
}

namespace {

// "" any, "2" exactly, "0-2" (or "0 – 2") from low to high; nullopt when it cannot be read.
std::optional<std::pair<int, int>> readRange(std::string text) {
    for (std::size_t at; (at = text.find("–")) != std::string::npos;) text.replace(at, std::strlen("–"), "-");
    std::erase(text, ' ');
    if (text.empty()) return std::pair{-1, -1};
    const auto dash   = text.find('-');
    const auto number = [](const std::string& t) -> std::optional<int> {
        if (t.empty() || t.size() > 2 || !std::all_of(t.begin(), t.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return std::nullopt;
        return std::stoi(t);
    };
    const auto low  = number(text.substr(0, dash));
    const auto high = dash == std::string::npos ? low : number(text.substr(dash + 1));
    if (!low || !high || *low > *high) return std::nullopt;
    return std::pair{*low, *high};
}

constexpr chess::PieceType kKinds[] = {chess::PieceType::PAWN, chess::PieceType::KNIGHT, chess::PieceType::BISHOP,
                                       chess::PieceType::ROOK, chess::PieceType::QUEEN,  chess::PieceType::KING};

chess::Piece patternPiece(int man) {
    return chess::Piece(kKinds[man % 6], man < 6 ? chess::Color::WHITE : chess::Color::BLACK);
}

std::string searchTime(double ms) {
    return ms < 1000 ? fmt::format("{} ms", static_cast<int>(std::lround(ms))) : fmt::format("{:.1f} s", ms / 1000);
}

}  // namespace

std::optional<study::MaterialQuery> App::materialQuery() const {
    study::MaterialQuery q;
    bool any = false;
    for (std::size_t i = 0; i < 10; ++i) {
        const auto range = readRange(db_search_.material[i].data());
        if (!range || range->first < 0) continue;
        q.low[i]  = static_cast<uint8_t>(range->first);
        q.high[i] = static_cast<uint8_t>(std::min<int>(range->second, q.high[i]));
        any       = true;
    }
    q.plies = static_cast<uint32_t>(std::max(1, db_search_.material_plies));
    return any ? std::optional(q) : std::nullopt;
}

std::optional<study::PatternQuery> App::patternQuery() const {
    study::PatternQuery q;
    bool any = false;
    for (int sq = 0; sq < 64; ++sq) {
        const int v = db_search_.pattern[static_cast<std::size_t>(sq)];
        if (v < 0) continue;
        any = true;
        if (v == kEmptySquare)
            q.empty |= uint64_t{1} << sq;
        else
            q.men[static_cast<std::size_t>(v / 6)][static_cast<std::size_t>(v % 6)] |= uint64_t{1} << sq;
    }
    q.only_these_pawns = db_search_.only_pawns;
    any                = any || q.only_these_pawns[0] || q.only_these_pawns[1];
    return any ? std::optional(q) : std::nullopt;
}

void App::pollSearches(const std::shared_ptr<const study::Snapshot>& snapshot, const std::string& folder) {
    const auto poll = [&](SearchJob& job, std::string of, std::optional<study::SearchQuery> query) {
        if (of != job.of) {
            job.search.reset();  // cancelled, its threads joined
            job.ready = false;
            job.ids.clear();
            job.of = std::move(of);
            if (query && !job.of.empty()) job.search = std::make_unique<study::Search>(snapshot, std::move(*query));
        }
        if (job.search && !job.ready && job.search->done()) {
            for (const auto& hit : job.search->hits()) job.ids.push_back(hit.id);
            std::sort(job.ids.begin(), job.ids.end());
            job.ids.erase(std::unique(job.ids.begin(), job.ids.end()), job.ids.end());
            job.ms    = job.search->elapsedMs();
            job.ready = true;
        }
    };
    const auto base = fmt::format("{}|{}", folder, snapshot->generation());
    // The Board page's position: shown by its tab, or in the filter.
    std::optional<study::SearchQuery> position;
    std::string position_of;
    if (db_search_.position || (db_search_open_ && search_tab_ == 1)) {
        study::ScanBoard board;
        const auto fen = db_search_.position_fen.empty() ? boardFen() : db_search_.position_fen;
        if (board.setFen(fen, board_.chess960)) {
            position    = study::PositionQuery{board};
            position_of = base + "|" + fen;
        }
    }
    poll(db_position_job_, position_of, std::move(position));
    std::string material_of;
    const auto material = materialQuery();
    if (material) {
        material_of = base + fmt::format("|{}|", material->plies);
        for (std::size_t i = 0; i < 10; ++i) material_of += fmt::format("{}-{},", material->low[i], material->high[i]);
    }
    poll(db_material_job_, material_of, material ? std::optional<study::SearchQuery>(*material) : std::nullopt);
    std::string pattern_of;
    const auto pattern = patternQuery();
    if (pattern) {
        pattern_of = base + fmt::format("|{}{}|", db_search_.only_pawns[0], db_search_.only_pawns[1]);
        for (const auto v : db_search_.pattern) pattern_of += fmt::format("{},", v);
    }
    poll(db_pattern_job_, pattern_of, pattern ? std::optional<study::SearchQuery>(*pattern) : std::nullopt);
}

void App::drawSearchPanel(const std::shared_ptr<const study::Snapshot>& snapshot) {
    (void)snapshot;
    auto& p = db_search_;
    const auto& entries = shelf().entries();
    const auto name =
        db_selected_ >= 0 && static_cast<std::size_t>(db_selected_) < entries.size() ? entries[db_selected_].name : "";
    ImGui::TextUnformatted(trf("Search in “{}”", name).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("Esc"));
    ImGui::TextDisabled("%s", TR("the list follows it as it goes"));
    // The searches kept (M1, board 6): a click sets their criteria; the right button renames or deletes one.
    if (!kept_searches_.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted(TR("Kept searches"));
        std::optional<std::size_t> apply, remove;
        for (std::size_t i = 0; i < kept_searches_.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            auto& k = kept_searches_[i];
            if (static_cast<int>(i) == kept_renaming_) {
                ImGui::SetNextItemWidth(px(220));
                if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
                const bool done = ImGui::InputText("##kept_rename", kept_rename_.data(), kept_rename_.size(),
                                                   ImGuiInputTextFlags_EnterReturnsTrue);
                if (done && kept_rename_[0]) {
                    k.name = kept_rename_.data();
                    saveKeptSearches();
                }
                if (done || ImGui::IsKeyPressed(ImGuiKey_Escape)) kept_renaming_ = -1;
            } else {
                const float w = ImGui::CalcTextSize(k.name.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
                if (i > 0) ui::sameLineOrWrap(w, px(4));
                if (ImGui::SmallButton(k.name.c_str())) apply = i;
                if (ImGui::BeginPopupContextItem("##kept_menu")) {
                    if (ImGui::MenuItem(TR("Rename"))) {
                        kept_renaming_ = static_cast<int>(i);
                        kept_rename_.fill(0);
                        std::snprintf(kept_rename_.data(), kept_rename_.size(), "%s", k.name.c_str());
                    }
                    if (ImGui::MenuItem(TR("Delete"))) remove = i;
                    ImGui::EndPopup();
                }
            }
            ImGui::PopID();
        }
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", TR("A click sets its criteria · the right button: rename, delete · in Ctrl+K too"));
        ImGui::PopTextWrapPos();
        if (apply) applyKeptSearch(*apply);
        if (remove) {
            kept_searches_.erase(kept_searches_.begin() + static_cast<std::ptrdiff_t>(*remove));
            saveKeptSearches();
        }
    }
    // The tabs of the mockup: the headers, the Board page's position, a material, a pattern.
    const char* tabs[] = {TR("Headers"), TR("Position"), TR("Material"), TR("Pattern")};
    if (ImGui::BeginTabBar("##search_tabs")) {
        for (int i = 0; i < 4; ++i) {
            if (ImGui::BeginTabItem(tabs[i])) {
                search_tab_ = i;
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    const float label_w = px(96);
    const auto label    = [&](const char* text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        ImGui::SameLine(label_w);
    };
    // A small button on the line of the one before when it fits there.
    const auto flow = [](const char* label) {
        const auto& style = ImGui::GetStyle();
        const float w     = ImGui::CalcTextSize(label).x + style.FramePadding.x * 2;
        const float x     = ImGui::GetItemRectMax().x + style.ItemSpacing.x;
        if (x + w <= ImGui::GetWindowPos().x + ImGui::GetContentRegionMax().x) ImGui::SameLine();
    };
    // How far a search went, or what it found.
    const auto status = [&](const SearchJob& job, const char* empty, const char* how) {
        ImGui::PushTextWrapPos();
        if (job.of.empty()) {
            ImGui::TextDisabled("%s", empty);
        } else if (!job.ready && job.search) {
            const auto total = std::max<uint64_t>(1, job.search->total());
            ImGui::ProgressBar(static_cast<float>(job.search->searched()) / static_cast<float>(total), ImVec2(-1, 0));
        } else {
            ImGui::TextUnformatted(trn("{} game", "{} games",
                                       static_cast<int>(std::min<std::size_t>(job.ids.size(), 1000000000)),
                                       ui::exactCount(job.ids.size()))
                                       .c_str());
            ImGui::SameLine(0, px(4));
            ImGui::TextDisabled("· %s%s", searchTime(job.ms).c_str(), how);
        }
        ImGui::PopTextWrapPos();
    };

    if (search_tab_ == 0) {
        const auto field = [&](const char* text, const char* id, std::array<char, 64>& buffer) {
            label(text);
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText(id, buffer.data(), buffer.size());
        };
        field(TR("Player"), "##s_player", p.player);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + label_w);
        ImGui::SetNextItemWidth(-1);
        const char* sides[] = {TR("Either colour"), TR("White"), TR("Black")};
        for (int i = 0; i < 3; ++i) {
            if (i > 0) ImGui::SameLine(0, px(2));
            if (ImGui::RadioButton(sides[i], p.side == i)) p.side = i;
        }
        field(TR("Opponent"), "##s_opponent", p.opponent);
        field(TR("Event"), "##s_event", p.event);
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
        const int result      = static_cast<int>(std::find(std::begin(values), std::end(values), p.result) - values);
        for (int i = 0; i < 4; ++i) {
            if (i > 0) ImGui::SameLine(0, px(2));
            if (ImGui::RadioButton(results[i], result == i)) p.result = values[i];
        }
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + label_w);
        ImGui::Checkbox(TR("Damaged games only"), &p.damaged);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s",
                              TR("Games a move of which could not be read: kept up to it, their text as it was."));
    } else if (search_tab_ == 1) {
        // The Board page's position: the games that reach it, found in the background.
        const float size = std::min(px(200), ImGui::GetContentRegionAvail().x * 0.5f);
        BoardDecor decor;
        decor.eval_white = -1.0f;
        decor.flipped    = board_.flip;
        chess::Board kept;
        kept.set960(board_.chess960);
        const bool from_kept = !p.position_fen.empty() && kept.setFen(p.position_fen);
        drawBoard(atlas_, from_kept ? kept : boardAt(board_.game, board_.chess960, board_.ply), size, decor);
        ImGui::SameLine(0, px(10));
        ImGui::BeginGroup();
        if (from_kept) {
            ImGui::TextUnformatted(TR("The kept search's position"));
            if (ImGui::Button(TR("The Board page's position instead"))) p.position_fen.clear();
            ImGui::EndGroup();
            status(db_position_job_, TR("The position cannot be searched."), "");
        }
        if (!from_kept) {
            ImGui::TextUnformatted(TR("The Board page's position"));
            ImGui::PushTextWrapPos();
            const auto mover = boardAt(board_.game, board_.chess960, board_.ply).sideToMove() == chess::Color::WHITE
                                   ? TR("White to move")
                                   : TR("Black to move");
            const auto shown = board_.ply > 0 ? trf("After {}, {}.", boardMoveLabel(board_.ply), mover)
                                              : trf("The start position, {}.", mover);
            ImGui::TextDisabled("%s", shown.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::Button(TR("Set up the pieces…"))) {
                page_ = Page::Board;
                openPositionEditor();
            }
            ImGui::EndGroup();
            status(db_position_job_, TR("The position cannot be searched."), "");
        }
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", TR("The games that cannot reach it (their pawns, their material) are left out "
                                     "without being replayed."));
        ImGui::PopTextWrapPos();
        if (ImGui::Button(p.position ? TR("Take out of the filter") : TR("Add to the filter")))
            p.position = !p.position;
    } else if (search_tab_ == 2) {
        // A material, for some plies in a row: presets, or the counts typed.
        struct Preset {
            const char* name;
            std::array<const char*, 10> cells;
        };
        const Preset presets[] = {
            {TR("Rook endings"), {"0-8", "0", "0", "1", "0", "0-8", "0", "0", "1", "0"}},
            {TR("No queens"), {"0-8", "0-2", "0-2", "0-2", "0", "0-8", "0-2", "0-2", "0-2", "0"}},
            {TR("Bishop pair against bishop and knight"), {"0-8", "0", "2", "0-2", "0", "0-8", "1", "1", "0-2", "0"}},
            {TR("Queen against two rooks"), {"0-8", "0-2", "0-2", "0", "1", "0-8", "0-2", "0-2", "2", "0"}},
        };
        for (std::size_t i = 0; i < std::size(presets); ++i) {
            if (i > 0) flow(presets[i].name);
            if (ImGui::SmallButton(presets[i].name))
                for (std::size_t c = 0; c < 10; ++c)
                    std::snprintf(p.material[c].data(), p.material[c].size(), "%s", presets[i].cells[c]);
        }
        const char* kinds[] = {TR("Pawns"), TR("Knights"), TR("Bishops"), TR("Rooks"), TR("Queens")};
        if (ImGui::BeginTable("##material", 3, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableSetupColumn(TR("Piece"));
            ImGui::TableSetupColumn(TR("White"));
            ImGui::TableSetupColumn(TR("Black"));
            ImGui::TableHeadersRow();
            for (std::size_t k = 0; k < 5; ++k) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(kinds[k]);
                for (std::size_t side = 0; side < 2; ++side) {
                    ImGui::TableNextColumn();
                    auto& cell     = p.material[side * 5 + k];
                    const bool bad = !readRange(cell.data());
                    if (bad) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::BAD));
                    ImGui::SetNextItemWidth(-1);
                    ImGui::InputTextWithHint(fmt::format("##m{}", side * 5 + k).c_str(), TR("any"), cell.data(),
                                             cell.size());
                    if (bad) ImGui::PopStyleColor();
                }
            }
            ImGui::EndTable();
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(TR("For"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(80));
        ImGui::InputInt("##m_plies", &p.material_plies);
        p.material_plies = std::clamp(p.material_plies, 1, 200);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("plies at least"));
        ImGui::TextDisabled("%s", TR("An exchange under way is not a material balance. A count: 2, or 0-2."));
        status(db_material_job_, TR("Any material: a preset, or the counts typed."), "");
    } else {
        // A pattern: men put on a board, empty squares, the rest anything.
        const float size = std::min(px(220), ImGui::GetContentRegionAvail().x * 0.6f);
        std::array<chess::Piece, 64> pieces;
        pieces.fill(chess::Piece(chess::Piece::NONE));
        BoardDecor decor;
        decor.eval_white = -1.0f;
        for (int sq = 0; sq < 64; ++sq) {
            const int v = p.pattern[static_cast<std::size_t>(sq)];
            if (v >= 0 && v < kEmptySquare) pieces[static_cast<std::size_t>(sq)] = patternPiece(v);
            if (v == kEmptySquare) decor.tints.push_back({chess::Square(sq), IM_COL32(236, 140, 80, 150)});
        }
        const ImVec2 top = ImGui::GetCursorScreenPos();
        drawBoard(atlas_, pieces, size, decor);
        if (ImGui::IsItemHovered()) {
            const bool left  = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            const bool right = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
            if (left || right) {
                if (const auto square = boardSquareAt(ImGui::GetMousePos(), top, size, decor)) {
                    auto& at          = p.pattern[static_cast<std::size_t>(square->index())];
                    const int8_t want = ImGui::GetIO().KeyShift ? kEmptySquare : static_cast<int8_t>(pattern_brush_);
                    at                = right || at == want ? int8_t{-1} : want;
                }
            }
        }
        ImGui::SameLine(0, px(10));
        ImGui::BeginGroup();
        // The men a click puts: white then black.
        const float button = px(26);
        for (int side = 0; side < 2; ++side) {
            for (int kind = 5; kind >= 0; --kind) {
                const int man = side * 6 + kind;
                ImVec2 uv0, uv1;
                atlas_.uv(patternPiece(man), uv0, uv1);
                ImGui::PushID(man);
                const bool chosen = pattern_brush_ == man;
                if (chosen) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
                if (ImGui::ImageButton("##man", atlas_.texture(button), ImVec2(button, button), uv0, uv1))
                    pattern_brush_ = man;
                if (chosen) ImGui::PopStyleColor();
                ImGui::PopID();
                if (kind > 0 && kind % 3 != 0) ImGui::SameLine(0, px(2));
            }
        }
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", TR("A click puts the man chosen (again: takes it away); Shift and a click: an empty "
                                     "square; the rest: anything."));
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::Checkbox(TR("White has only these pawns"), &p.only_pawns[0]);
        ImGui::Checkbox(TR("Black has only these pawns"), &p.only_pawns[1]);
        struct Preset {
            const char* name;
            std::vector<std::pair<int, int>> men;  // square, man
        };
        const auto sq          = [](const char* name) { return (name[1] - '1') * 8 + (name[0] - 'a'); };
        const Preset presets[] = {
            {TR("Short castling, pawns intact"),
             {{sq("g1"), 5},
              {sq("f1"), 3},
              {sq("f2"), 0},
              {sq("g2"), 0},
              {sq("h2"), 0},
              {sq("f3"), kEmptySquare},
              {sq("g3"), kEmptySquare},
              {sq("h3"), kEmptySquare}}},
            {TR("Maroczy bind"), {{sq("c4"), 0}, {sq("e4"), 0}, {sq("d6"), 6}}},
            {TR("King's fianchetto"), {{sq("g1"), 5}, {sq("g2"), 2}, {sq("f2"), 0}, {sq("g3"), 0}, {sq("h2"), 0}}},
        };
        for (std::size_t i = 0; i < std::size(presets); ++i) {
            if (i > 0) flow(presets[i].name);
            if (ImGui::SmallButton(presets[i].name)) {
                p.pattern.fill(-1);
                p.only_pawns = {};
                for (const auto& [square, man] : presets[i].men)
                    p.pattern[static_cast<std::size_t>(square)] = static_cast<int8_t>(man);
            }
        }
        status(db_pattern_job_, TR("Put men on the board: the games where they stand so."),
               TR(" (each game replayed up to the pattern)"));
    }

    ImGui::Separator();
    if (ImGui::Button(TR("See in the list")) ||
        (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::GetIO().WantTextInput))
        db_search_open_ = false;
    ImGui::SameLine(0, px(4));
    ImGui::TextDisabled("%s", TR("Enter"));
    ImGui::SameLine();
    if (ImGui::Button(TR("Clear everything"))) {
        p = SearchFields{};
        db_filter_.fill(0);
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Keep this search…"))) {
        keep_naming_ = true;
        keep_name_.fill(0);
        std::snprintf(keep_name_.data(), keep_name_.size(), "%s", keptSearchName(p).c_str());
    }
    if (keep_naming_) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(TR("Its name"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(px(160), ImGui::GetContentRegionAvail().x - px(90)));
        if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
        const bool enter =
            ImGui::InputText("##keep_name", keep_name_.data(), keep_name_.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button(TR("Keep")) || enter) && keep_name_[0]) {
            auto fields = p;
            // A position kept is that of the board now, not the board's later.
            if (fields.position && fields.position_fen.empty()) fields.position_fen = boardFen();
            kept_searches_.push_back({keep_name_.data(), fields});
            saveKeptSearches();
            keep_naming_ = false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) keep_naming_ = false;
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", TR("Its criteria, not its games: on any database, its new games found next time."));
        ImGui::PopTextWrapPos();
    }
}

}  // namespace fcg
