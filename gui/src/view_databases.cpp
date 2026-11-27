// The Databases page of the Chess space (mockup M1, board 1): the databases of the profile, the chosen one's facts
// and what can be done with it, an import with its progress, a file dropped on the page.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <set>
#include <string>
#include <thread>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>  // SetKeyOwner

#include <study/background.hpp>
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
    static const std::vector<std::pair<std::string, std::string>> filters = {
        {"PGN, ChessBase", "pgn;zst;gz;zip;7z;cbh;2cbh;cbv;cbz;2cbv;2cbz"},
        {"PGN", "pgn;zst;gz;zip;7z"},
        {"ChessBase", "cbh;2cbh;cbv;cbz;2cbv;2cbz"}};
    return filters;
}

}  // namespace

DatabaseShelf& App::shelf() {
    if (!shelf_) shelf_ = std::make_unique<DatabaseShelf>(jobs_, storage_.root());
    return *shelf_;
}

void App::importIntoDatabase(int index, const std::filesystem::path& file, bool pasted, std::vector<uint64_t> forget) {
    auto& s = shelf();
    // A ChessBase database: what it brings, asked first (M10, board 1).
    if (study::isChessBase(file) && !db_chessbase_go_) {
        ChessBaseAsk ask;
        ask.index      = index;
        ask.file       = file;
        ask.archive    = study::isChessBaseArchive(file);   // .cbv, .cbz, .2cbv, .2cbz
        ask.enciphered = study::isEncipheredArchive(file);  // .cbz, .2cbz
        if (!ask.archive) {  // its headers only: the moves are read by the copy
            try {
                const study::ChessBaseDatabase cb(file);
                ask.format  = cb.format() == study::ChessBaseFormat::CBH ? "CBH" : "2CBH";
                ask.records = cb.records();
            } catch (const std::exception& e) {
                ask.error = e.what();
            }
        }
        db_chessbase_ = std::move(ask);
        page_         = Page::Databases;
        db_list_      = false;  // the card shows on the page of the databases, not over a game list
        return;
    }
    if (monthReading()) {  // it writes to a database too: one at a time
        db_message_ = TR("A month of Lichess is being read into a database: import once it has finished.");
        return;
    }
    bool created = false;
    if (index < 0 || index >= static_cast<int>(s.entries().size())) {
        try {
            index   = static_cast<int>(s.create(file.stem().stem().string()));  // games.pgn.zst: games
            created = true;
        } catch (const std::exception& e) {
            db_message_ = e.what();
            return;
        }
    }
    db_selected_ = index;
    // What the database had before: the games added, and how much more the rest would take if the disk fills up.
    ImportStart start;
    start.folder  = s.entries()[static_cast<std::size_t>(index)].folder;
    start.pasted  = pasted;
    start.created = created;
    if (const auto db = s.open(static_cast<std::size_t>(index))) start.games = db->snapshot()->games();
    start.bytes = s.facts(static_cast<std::size_t>(index)).bytes;
    if (careBusy(s.entries()[static_cast<std::size_t>(index)].folder)) {
        db_message_ = trf("“{}” is being compacted: import once it has finished.",
                          s.entries()[static_cast<std::size_t>(index)].name);
        return;
    }
    if (!s.startImport(static_cast<std::size_t>(index), file, db_chessbase_password_, std::move(forget))) {
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
    pollCare();         // the maintenance's tasks (M13, board 1), FIDE's list
    pollTournaments();  // the lists of tournaments, the crosstable shown (#92)
    if (!shelf_) return;
    shelf_->pollTreeJob();
    shelf_->keepReadOnlyCurrent();
    const auto* job = shelf_->import();
    if (!job) return;
    const auto file  = job->file.filename().string();
    const auto path  = job->file;
    const auto index = job->index;
    const auto read = job->progress.bytes.load(), total = job->progress.total_bytes.load();
    if (const auto report = shelf_->finishedImport()) {
        // The games already there from another source, the same game with other headers (M13, board 1).
        auto found = shelf_->takeImportDuplicates();
        if (twicImportEnded(path, *report, found.groups)) return;  // an issue of TWIC: its own report, once all in
        const auto start = db_import_start_.value_or(ImportStart{});
        // A file refused at once (not text, an archive that cannot be read) into a database made for it: no empty
        // database stays, and the page says why.
        if (start.created && report->added == 0 && !report->read.error.empty() && index < shelf_->entries().size() &&
            shelf_->entries()[index].folder == start.folder) {
            const auto db = shelf_->open(index);
            if (!db || db->snapshot()->games() == 0) {
                shelf_->discard(index);
                if (db_selected_ >= static_cast<int>(shelf_->entries().size())) db_selected_ = -1;
                db_import_start_.reset();
                db_outcome_.reset();
                if (start.pasted) {
                    std::error_code ec;
                    std::filesystem::remove(path, ec);
                }
                db_message_ = trf("Nothing imported: {}.", report->read.error);
                Console::instance().add(db_message_, true);
                return;  // said on the page, the database gone
            }
        }
        // The outcome, said on the database: the disk full and what the rest would take, the damaged games and why.
        ImportOutcome o;
        o.file         = path;
        o.report       = *report;
        o.pasted       = start.pasted;
        o.duplicates   = std::move(found.groups);
        o.games_before = start.games;
        if (index < shelf_->entries().size()) {
            o.folder = shelf_->entries()[index].folder;
            if (!report->error.empty()) {
                std::error_code ec;
                const auto space = std::filesystem::space(o.folder, ec);
                o.disk_full =
                    report->error_code == study::StoreErrc::DiskFull || (!ec && space.available < (uint64_t{64} << 20));
                const auto now   = shelf_->facts(index).bytes;
                if (read > 0 && total > read && now > start.bytes)
                    o.needed =
                        static_cast<uint64_t>(static_cast<double>(total - read) *
                                              static_cast<double>(now - start.bytes) / static_cast<double>(read));
            }
            if (report->damaged > 0)
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
                    storeErrorText(report->error_code, report->error));
        } else if (!report->read.error.empty()) {
            db_message_ = trf("Import of {} stopped: {}.", file, report->read.error);
            if (report->added > 0)
                db_message_ += " " + trn("The {} game read before it stays in the database.",
                                         "The {} games read before it stay in the database.", report->added,
                                         ui::exactCount(report->added));
        } else if (report->read.cancelled) {
            db_message_ =
                trf("Import of {} stopped at your request: {} games kept. Importing the file again adds the "
                    "missing ones only.",
                    file, ui::exactCount(report->added));
        } else {
            db_message_ = trn("Import of {} done: {} game added", "Import of {} done: {} games added", report->added,
                              file, ui::exactCount(report->added));
            // The damaged games right after the added ones, which they are among.
            if (report->damaged > 0)
                db_message_ += trn(", {} of them damaged (kept up to the move that cannot be read)",
                                   ", {} of them damaged (each kept up to the move that cannot be read)",
                                   report->damaged, ui::exactCount(report->damaged));
            if (report->duplicates > 0)
                db_message_ += trf(", {} already there left out", ui::exactCount(report->duplicates));
            db_message_ += ".";
        }
        Console::instance().add(db_message_, !report->error.empty() || !report->read.error.empty());
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
    ImGui::TextDisabled("%s", TR("Drop a PGN, .zst, .gz, .zip or .7z file, or a ChessBase database or archive, here, "
                                 "or:"));
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
            ImGui::TextColored(
                ImGui::ColorConvertU32ToFloat4(ui::BAD), "%s",
                trf("Import of {} stopped: {}.", file, storeErrorText(o.report.error_code, o.report.error)).c_str());
            ImGui::TextDisabled("%s", trf("The {} games committed before stay; resuming the import adds only the "
                                          "missing ones.",
                                          added)
                                          .c_str());
        }
        resume = !o.pasted && ImGui::Button(TR("Resume the import"));
        ImGui::SameLine();
        if (ImGui::Button(TR("Open the database's folder"))) ui::openPath(o.folder);
    } else if (!o.report.read.error.empty()) {
        // The file could not be read on: what was read before it stays.
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::BAD), "%s",
                           trf("Import of {} stopped: {}.", file, o.report.read.error).c_str());
        if (o.report.added > 0)
            ImGui::TextDisabled("%s", trn("The {} game read before it stays in the database.",
                                          "The {} games read before it stay in the database.", o.report.added, added)
                                          .c_str());
    } else if (o.report.read.cancelled) {
        ImGui::TextUnformatted(trf("Import of {} stopped at your request: {} games kept.", file, added).c_str());
        ImGui::TextDisabled("%s", TR("Resuming it adds only the missing games."));
        resume = !o.pasted && ImGui::Button(TR("Resume the import"));
    } else {
        // Each count with its own plural: the damaged games are among those added.
        const auto damaged = o.report.damaged;
        ImGui::TextUnformatted(
            trn("Import of {} done: {} game added.", "Import of {} done: {} games added.", o.report.added, file, added)
                .c_str());
        if (o.report.duplicates > 0)
            ImGui::TextDisabled("%s", trn("{} game already there left out.", "{} games already there left out.",
                                          o.report.duplicates, ui::exactCount(o.report.duplicates))
                                          .c_str());
        // The same games from another source, their headers written differently (M13, board 1): told, and merged
        // on a click, never without it.
        if (!o.duplicates.empty()) {
            std::set<std::string> sources;
            if (const auto db = shelf().open(index)) {
                const auto snap = db->snapshot();
                for (const auto& g : o.duplicates)
                    for (const auto id : g.games)
                        if (id < o.games_before && id < snap->games()) {
                            const auto label = study::sourceLabel(snap->source(id), snap->game(id));
                            if (!label.empty()) sources.insert(label);
                        }
            }
            std::string from;
            for (const auto& s : sources) from += (from.empty() ? "" : ", ") + s;
            ImGui::TextUnformatted((from.empty()
                                        ? trn("{} game already there, from another source.",
                                              "{} games already there, from another source.", o.duplicates.size(),
                                              ui::exactCount(o.duplicates.size()))
                                        : trn("{} game already there, from {}.", "{} games already there, from {}.",
                                              o.duplicates.size(), ui::exactCount(o.duplicates.size()), from))
                                       .c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Merge them"))) {
                std::vector<study::DuplicateGroup> merge;
                for (const auto& g : o.duplicates)
                    if (g.kind != study::DuplicateKind::Results) merge.push_back(g);
                const auto since = o.games_before;
                openCare(static_cast<int>(index), CarePart::Duplicates);
                if (care_) care_->since = since;
                startCareMerges(index, std::move(merge), {});
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TR("Each kept once, with the fullest of each field; the other marked deleted. "
                                           "Ctrl+Z undoes, on the maintenance page that opens."));
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("See them first"))) {
                const auto since = o.games_before;
                openCare(static_cast<int>(index), CarePart::Duplicates);
                if (care_) care_->since = since;
            }
        }
        if (damaged > 0) {
            ImGui::TextDisabled("%s", trn("Of the games added, {} is damaged: kept up to the move that cannot be read, "
                                          "its text kept as it was.",
                                          "Of the games added, {} are damaged: each kept up to the move that cannot be "
                                          "read, its text kept as it was.",
                                          damaged, ui::exactCount(damaged))
                                          .c_str());
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
    // A ChessBase database: what its games left out, by kind (M10, board 2); nothing when it could not be read.
    if (o.report.chessbase && o.report.read.error.empty()) {
        const auto& c   = *o.report.chessbase;
        std::vector<std::string> left;
        // Each count's two forms written in its trn(), where gui/i18n/update.py finds them as one plural message.
        const auto add = [&](uint64_t n, std::string said) {
            if (n) left.push_back(std::move(said));
        };
        const uint64_t media = c.pictures + c.sounds + c.videos, drawn = c.pawn_structures + c.piece_paths,
                       other = c.correspondence + c.undecoded + c.other;
        add(c.texts, trn("{} text", "{} texts", c.texts, ui::exactCount(c.texts)));
        add(c.training, trn("{} training question", "{} training questions", c.training, ui::exactCount(c.training)));
        add(media, trn("{} picture, sound or video", "{} pictures, sounds or videos", media, ui::exactCount(media)));
        add(c.web_links, trn("{} web link", "{} web links", c.web_links, ui::exactCount(c.web_links)));
        add(c.quotations, trn("{} game quoted", "{} games quoted", c.quotations, ui::exactCount(c.quotations)));
        add(drawn,
            trn("{} pawn structure or piece path", "{} pawn structures or piece paths", drawn, ui::exactCount(drawn)));
        add(c.variation_colours, trn("{} variation colour", "{} variation colours", c.variation_colours,
                                     ui::exactCount(c.variation_colours)));
        add(other, trn("{} other annotation", "{} other annotations", other, ui::exactCount(other)));
        std::string said;
        for (const auto& l : left) said += (said.empty() ? "" : ", ") + l;
        ImGui::TextDisabled(
            "%s", said.empty() ? TR("Nothing of its games stayed out.") : trf("Stayed out: {}.", said).c_str());
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
    if (care_) {  // a database's maintenance (M13, board 1)
        drawCare();
        return;
    }
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
        if (ui::iconButton("db_sources", ui::Icon::Globe, sources) || std::exchange(db_sources_open_, false))
            ImGui::OpenPopup("##sources");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s",
                              TR("Games from a site, downloaded when you ask: TWIC's issues of the week, Lichess's "
                                 "broadcasts and monthly files, a chess.com player's games, Lumbra's Gigabase."));
        drawSourcesMenu();
        ImGui::SameLine();
        if (ui::iconButton("db_add", ui::Icon::Database, add)) dialogs_.openFolder(Dialogs::Purpose::AddDatabase);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("A database folder (Name.fcgdb) made elsewhere: listed here, its files left "
                                       "where they are."));
    }
    ImGui::TextDisabled("%s",
                        TR("Drop a PGN (.pgn, .zst, .gz, .zip, .7z) or a ChessBase database (.cbh, .2cbh) or archive "
                           "(.cbv, .cbz, .2cbv, .2cbz) here: it goes into the chosen database, or into a new one named "
                           "after it."));

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
    drawChessBaseAsk();
    drawTwicReport();
    if (twic_sheet_) {
        drawTwicSheet();
        return;
    }
    if (broadcast_sheet_) {
        drawBroadcastSheet();
        return;
    }
    if (chesscom_sheet_) {
        drawChesscomSheet();
        return;
    }
    if (month_sheet_) {
        drawMonthSheet();
        return;
    }
    if (lumbra_sheet_) {
        drawLumbraSheet();
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
        if (!entries[i].source && ImGui::BeginPopupContextItem("##db_menu")) {
            ImGui::TextDisabled("%s", TR("Maintenance"));
            if (ImGui::MenuItem(TR("Duplicate games"))) openCare(static_cast<int>(i), CarePart::Duplicates);
            if (ImGui::MenuItem(TR("Players' names"))) openCare(static_cast<int>(i), CarePart::Names);
            if (ImGui::MenuItem(TR("Check and compact"))) openCare(static_cast<int>(i), CarePart::Checks);
            ImGui::EndPopup();
        }
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

    // A segment that could not be read when it was opened (#97): told as the Home page tells it, its maintenance says
    // what to do.
    if (!linked && facts.unreadable > 0) {
        ui::beginCard("db_damaged");
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           trn("“{}”: {} damaged game", "“{}”: {} damaged games", facts.unreadable, entries[index].name,
                               ui::exactCount(facts.unreadable))
                               .c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("See them##damaged"))) openCare(static_cast<int>(index), CarePart::Checks);
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
            const auto total = job->progress.total_bytes.load();
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
    // Enter is the ChessBase card's while it is shown (its password field's too).
    if (ui::iconButton("db_open_list", ui::Icon::Contents, TR("Open the list")) ||
        (!db_naming_ && !db_chessbase_ && db_named_frame_ != ImGui::GetFrameCount() && !linkCardTakesKeys() &&
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
    // ⋯: its maintenance (M13, board 1).
    ImGui::SameLine();
    if (ui::iconOnlyButton("##db_more", ui::Icon::More, TR("More: the database's maintenance")))
        ImGui::OpenPopup("##db_more_menu");
    if (ImGui::BeginPopup("##db_more_menu")) {
        ImGui::TextDisabled("%s", TR("Maintenance"));
        if (ImGui::MenuItem(TR("Duplicate games"))) openCare(static_cast<int>(index), CarePart::Duplicates);
        if (ImGui::MenuItem(TR("Players' names"))) openCare(static_cast<int>(index), CarePart::Names);
        if (ImGui::MenuItem(TR("Check and compact"))) openCare(static_cast<int>(index), CarePart::Checks);
        ImGui::EndPopup();
    }
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
    if (const auto* job = foundJob(); job && !job->hits->empty()) origin.found = job->hits;
    board_origin_ = std::move(origin);
    openListRow(row);
}

void App::openListRow(std::size_t row) {
    if (!board_origin_) return;
    const auto& o = *board_origin_;
    if (o.listed ? row >= o.listed->size() : (!o.view || row >= o.view->ids.size())) return;
    // The position shown, to open the next game at it when it passes through it.
    const auto shown = boardAt(board_.game, board_.chess960, board_.ply).hash();
    const bool keep  = board_.ply > 0;
    auto origin      = *board_origin_;
    // A list over several databases (a player's dossier): the game's own database.
    std::shared_ptr<const study::Snapshot> snapshot = origin.view ? origin.view->snapshot : nullptr;
    if (origin.listed) {
        const auto& l = (*origin.listed)[row];
        origin.folder = l.folder;
        origin.name   = l.name;
        origin.linked = false;
        snapshot      = l.snapshot;
    }
    const auto id = origin.listed ? (*origin.listed)[row].id : origin.view->ids[row];
    origin.row    = row;
    origin.id     = id;
    // A game left with changes not saved comes back with them, where it was left.
    if (const auto copy = changedCopyOf(origin.folder, id)) {
        restoreRecentGame(*copy);
        board_origin_ = std::move(origin);  // in the list it is reached from now
        return;
    }
    readOnBoard(study::importedPgn(snapshot->game(id)), trf("{}, game {}", origin.name, ui::exactCount(id + 1)));
    board_origin_ = std::move(origin);
    page_         = Page::Board;
    // At the position its search found it at.
    if (const auto& found = board_origin_->found) {
        const auto it = std::lower_bound(found->begin(), found->end(), id,
                                         [](const study::SearchHit& h, study::GameId g) { return h.id < g; });
        if (it != found->end() && it->id == id && it->ply <= board_.game.moves.size()) {
            board_.ply = static_cast<int>(it->ply);
            return;
        }
    }
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
        if (careBusy(s.entries()[i].folder)) {
            boardToast(trf("“{}” is being compacted: Ctrl+S once it has finished; the game stays changed here.",
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
    // A tournament's crosstable, or the database's tournaments, in place of its games (#92, M13, board 5).
    if (crosstable_ && crosstable_->folder == s.entries()[index].folder) {
        drawStudyCrosstable();
        return;
    }
    leaveCrosstable();  // another database's
    if (db_tournaments_ && snapshot->games() > 0) {
        drawTournamentList(index);
        return;
    }

    // Where it is: Databases › Name · N games.
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup) &&
                        !pattern_text_active_;  // Esc leaves the pattern's text first
    if (escape && db_search_open_) {
        db_search_open_ = false;  // the panel first, then the list
    } else if (ui::iconButton("db_back", ui::Icon::Back, TR("Databases")) || escape) {
        db_list_ = false;
        db_only_.reset();
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
    // Its tournaments beside its games (#92).
    if (snapshot->games() > 0) {
        ImGui::SameLine(0, px(14));
        if (drawListSwitch()) return;
    }
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
    if (!db_route_job_.of.empty()) narrowing_jobs.push_back(&db_route_job_);
    if (p.structure.set && !db_structure_job_.of.empty()) narrowing_jobs.push_back(&db_structure_job_);
    // A search still going narrows the list to the games it found so far (R18): the list follows as they come.
    std::string narrowing;
    for (const auto* job : narrowing_jobs)
        narrowing += job->of + (job->ready ? "+" : "-" + std::to_string(job->ids.size()));
    const auto of = fmt::format(
        "{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}", s.entries()[index].folder.string(), snapshot->generation(),
        db_filter_.data(), static_cast<int>(db_sort_), db_descending_, p.player.data(), p.opponent.data(),
        p.event.data(), p.side, p.year_from.data(), p.year_to.data(), p.eco_from.data(), p.eco_to.data(),
        p.elo_min.data(), static_cast<int>(p.result),
        narrowing + (p.damaged ? "|damaged" : "") + (db_only_ ? "|only" + std::to_string(db_only_->size()) : ""));
    if (db_view_of_ != of && (!db_view_job_ || db_view_job_->of != of)) {
        if (db_view_job_) *db_view_job_->cancel = true;
        ViewJob job;
        job.of            = of;
        job.cancel        = std::make_shared<std::atomic<bool>>(false);
        job.started       = std::chrono::steady_clock::now();
        const auto filter = listFilter();
        // The games the searches found, those of all of them (so far, for a search still going): the others left
        // out of the view; and those the maintenance shows, when it shows some.
        std::optional<std::vector<study::GameId>> through = db_only_;
        for (const auto* job : narrowing_jobs) {
            if (!through) {
                through = job->ids;
                continue;
            }
            // (db_only_ and the searches' ids are sorted alike.)
            std::vector<study::GameId> both;
            std::set_intersection(through->begin(), through->end(), job->ids.begin(), job->ids.end(),
                                  std::back_inserter(both));
            through = std::move(both);
        }
        job.done     = std::async(std::launch::async, [snapshot, filter, sort = db_sort_, down = db_descending_,
                                                       cancel = job.cancel, through = std::move(through)] {
            auto view = study::makeView(snapshot, filter, sort, down, 0, cancel.get());
            if (through) {
                // A bit a game: millions of games found are told apart at once.
                std::vector<uint64_t> found((snapshot->games() + 64) / 64, 0);
                for (const auto id : *through)
                    if (id / 64 < found.size()) found[id / 64] |= uint64_t{1} << (id % 64);
                std::erase_if(view.ids, [&](study::GameId id) {
                    return id / 64 >= found.size() || !(found[id / 64] >> (id % 64) & 1);
                });
            }
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
    if (db_only_) {
        ImGui::SameLine(0, px(16));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", trf("Only {}", db_only_said_).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Show all"))) db_only_.reset();
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
    // The chosen game, or the filter's games, as a page to replay, a GIF, a PDF or PGN (#93).
    if (export_when_listed_ >= 0 && !db_view_job_ && !db_view_->ids.empty()) {  // --focus export-db=…
        openDatabaseExport(export_listed_all_, std::exchange(export_when_listed_, -1));
        return;
    }
    if (!db_view_job_ && !db_view_->ids.empty()) {
        ImGui::SameLine(0, px(12));
        if (ui::iconButton("db_export", ui::Icon::Share, TR("Export…"))) openDatabaseExport(false);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s",
                              TR("The chosen game, or the filter's games, as a page to replay, an animated GIF, a "
                                 "PDF or PGN."));
    }

    const auto& ids  = db_view_->ids;
    const auto* snap = db_view_->snapshot.get();
    if (!ids.empty()) db_row_ = std::min(db_row_, ids.size() - 1);

    // The list and, beside it, the game chosen.
    // The search panel as wide for each of its tabs, so that the tabs stay where they are when one is chosen: the
    // advanced searches' builder, board and results want the room.
    const float preview_w = db_search_open_ ? std::min(px(600), ImGui::GetContentRegionAvail().x * 0.5f)
                                            : std::min(px(300), ImGui::GetContentRegionAvail().x * 0.3f);
    // Each game at the position its search found it at (the most precise search set): a column says the move.
    const auto* found_job = foundJob();
    const auto found      = found_job && !found_job->hits->empty() ? found_job->hits : nullptr;
    constexpr auto flags  = ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable |
                            ImGuiTableFlags_SizingFixedFit;
    bool open             = false;
    if (ImGui::BeginTable(found ? "##db_games_found" : "##db_games", found ? 11 : 10, flags,
                          ImVec2(ImGui::GetContentRegionAvail().x - preview_w - px(14), 0))) {
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
        if (found)
            ImGui::TableSetupColumn(TR("Found at"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                    px(76));
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
            // (Ctrl+Page Down and Ctrl+Page Up are the search panel's: from tab to tab.)
            const bool ctrl = ImGui::GetIO().KeyCtrl;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) db_row_ = std::min(db_row_ + 1, ids.size() - 1);
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && db_row_ > 0) --db_row_;
            if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_PageDown)) db_row_ = std::min(db_row_ + 20, ids.size() - 1);
            if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_PageUp)) db_row_ = db_row_ > 20 ? db_row_ - 20 : 0;
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
                // A player's name as the table of names gives it (M13, board 1): their FIDE card under the mouse once
                // known, asked by a right click (#57); the spelling of the game said when it differs.
                const auto ids    = snap->rowIds(id);
                const auto player = [&](std::string_view name, bool white) {
                    const auto shown_id = white ? ids.white : ids.black;
                    const auto* known   = snap->players()->of(shown_id);
                    if (known && !known->name.empty()) name = known->name;
                    text(name);
                    if (name.empty()) return;
                    // Over the row's selectable, which spans the columns: its hover allowed. The right button: their
                    // dossier (#90) or their FIDE card (#57).
                    constexpr auto over = ImGuiHoveredFlags_AllowWhenOverlappedByItem;
                    if (ImGui::IsItemHovered(over) && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        int64_t fide = 0;
                        for (const auto& [tag, value] : snap->game(id).tags)
                            if (tag == (white ? "WhiteFideId" : "BlackFideId")) fide = std::atoll(value.c_str());
                        db_name_menu_      = NameMenu{std::string(name), fide, shown_id, db_view_->snapshot};
                        db_name_menu_open_ = true;
                    }
                    if (!ImGui::IsItemHovered(over | ImGuiHoveredFlags_DelayNormal)) return;
                    if (known && (known->fide || snap->name(shown_id) != known->name)) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(known->name.c_str());
                        if (known->fide) {
                            std::string line = known->federation;
                            if (!known->title.empty()) line += " · " + known->title;
                            if (known->born) line += " · " + trf("born {}", known->born);
                            if (known->standard) line += " · " + trf("standard {}", known->standard);
                            if (known->rapid) line += " · " + trf("rapid {}", known->rapid);
                            if (known->blitz) line += " · " + trf("blitz {}", known->blitz);
                            ImGui::TextDisabled("%s", line.c_str());
                            ImGui::TextDisabled("%s", trf("FIDE id {}", known->fide).c_str());
                        }
                        const auto written = snap->name(shown_id);
                        if (written != known->name)
                            ImGui::TextDisabled("%s", trf("Written “{}” in this game.", written).c_str());
                        ImGui::EndTooltip();
                        return;  // the table's card: no need of Lichess's
                    }
                    if (const auto* card = fideCards().byName(name)) {
                        ImGui::BeginTooltip();
                        drawFideCard(*card);
                        ImGui::EndTooltip();
                    } else {
                        ImGui::SetTooltip("%s", TR("A right click: their dossier, or their FIDE card asked of Lichess "
                                                   "(the name only, without an account)."));
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
                if (found) {
                    ImGui::TableNextColumn();
                    const auto it =
                        std::lower_bound(found->begin(), found->end(), id,
                                         [](const study::SearchHit& h, study::GameId g) { return h.id < g; });
                    if (it != found->end() && it->id == id) {
                        // A move wider than its cell ends in "…", whole under the mouse.
                        const auto move   = foundMove(db_view_->snapshot, id, it->ply);
                        const float room  = ImGui::GetContentRegionAvail().x;
                        std::string shown = move;
                        while (!shown.empty() && ImGui::CalcTextSize((shown + "…").c_str()).x > room &&
                               ImGui::CalcTextSize(move.c_str()).x > room) {
                            while (!shown.empty() && (static_cast<unsigned char>(shown.back()) & 0xC0) == 0x80)
                                shown.pop_back();
                            if (!shown.empty()) shown.pop_back();
                        }
                        if (shown != move) shown += "…";
                        ImGui::TextUnformatted(shown.c_str());
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem))
                            ImGui::SetTooltip("%s\n%s", move.c_str(),
                                              TR("The position found: the game opens at this move."));
                    }
                }
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
        const auto id    = ids[db_row_];
        const auto row   = snap->row(id);
        const auto names = snap->rowIds(id);
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(
            fmt::format("{} – {}", snap->shownName(names.white), snap->shownName(names.black)).c_str());
        // Their FIDE cards: the table of names' (M13, board 1), else known or asked for by a click (#57).
        for (const bool white : {true, false}) {
            const std::string name(snap->shownName(white ? names.white : names.black));
            if (name.empty() || name == "?") continue;
            ImGui::PushID(white ? "fide_white" : "fide_black");
            if (const auto* known = snap->players()->of(white ? names.white : names.black); known && known->fide) {
                std::string line =
                    (known->title.empty() ? "" : known->title + " ") + known->name + " · " + known->federation;
                if (known->standard) line += " · " + std::to_string(known->standard);
                ImGui::TextDisabled("%s", line.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s",
                                      trf("From FIDE's list of {} (FIDE id {}), kept in the database's table of names.",
                                          fide::monthText(known->list), known->fide)
                                          .c_str());
            } else if (const auto* card = fideCards().byName(name)) {
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
        // Their dossiers (#90).
        for (const bool white : {true, false}) {
            const auto name_id = white ? names.white : names.black;
            const std::string name(snap->shownName(name_id));
            if (name.empty() || study::anonymousName(name)) continue;
            const auto label = trf("Dossier of {}", name);
            if (!white) ui::sameLineOrWrap(label.c_str());
            if (ImGui::SmallButton(fmt::format("{}##dossier_{}", label, white ? 'w' : 'b').c_str()))
                openDossierOf(*snap, name_id);
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
    // The right button on a name: their dossier (#90), their FIDE card (#57).
    if (std::exchange(db_name_menu_open_, false)) ImGui::OpenPopup("##db_name_menu");
    if (ImGui::BeginPopup("##db_name_menu")) {
        if (db_name_menu_) {
            const auto m = *db_name_menu_;
            if (ImGui::Selectable(trf("Dossier of {}", m.name).c_str()) && m.snapshot) {
                ImGui::CloseCurrentPopup();
                openDossierOf(*m.snapshot, m.name_id);
            }
            if (ImGui::Selectable(trf("FIDE card of {}", m.name).c_str())) {
                ImGui::CloseCurrentPopup();
                askFideCard(m.name, m.fide);
            }
        }
        ImGui::EndPopup();
    }
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

std::unique_ptr<study::Search> App::startSearch(std::shared_ptr<const study::Snapshot> snapshot,
                                                study::SearchQuery query, const study::Search* resume) {
    // Started from a thread in the background: its threads run at the lowest priority, beside the tournaments (R6).
    std::unique_ptr<study::Search> search;
    std::thread([&] {
        study::enterBackground();
        search = std::make_unique<study::Search>(std::move(snapshot), std::move(query), 0, resume);
    }).join();
    return search;
}

void App::pollSearches(const std::shared_ptr<const study::Snapshot>& snapshot, const std::string& folder) {
    const double now = ImGui::GetTime();
    const auto poll  = [&](SearchJob& job, std::string of, std::optional<study::SearchQuery> query) {
        if (of != job.of) {
            job.search.reset();  // cancelled, its threads joined
            job.cancelled.reset();
            job.ready = false;
            job.ids.clear();
            job.hits       = std::make_shared<std::vector<study::SearchHit>>();  // a board's list keeps the old ones
            job.next_part  = 0;
            job.stats      = {};
            job.searched   = 0;
            job.broad_kept = false;
            job.of         = std::move(of);
            if (query && !job.of.empty()) job.search = startSearch(snapshot, std::move(*query));
        }
        if (!job.search || job.ready) return;
        // Its games as they come, a few times a second: the first ones in the list already (R18).
        const bool done = job.search->done();
        if (!done && now - job.read_at < 0.3) return;
        job.read_at       = now;
        const auto before = job.hits->size();
        job.search->hitsFrom(job.next_part, *job.hits);
        for (std::size_t i = before; i < job.hits->size(); ++i) job.ids.push_back((*job.hits)[i].id);
        job.stats = job.search->stats();
        if (done && !job.search->cancelled()) {
            job.ms    = job.search->elapsedMs();
            job.ready = true;
        }
    };
    // While the database is imported into, a search keeps the games of when it started, made again at the end.
    auto& shelf_ = shelf();
    const bool importing =
        shelf_.import() && db_selected_ >= 0 && shelf_.import()->index == static_cast<std::size_t>(db_selected_);
    const auto base =
        fmt::format("{}|{}", folder, importing ? std::string("importing") : std::to_string(snapshot->generation()));
    // The Board page's position: shown by its tab, or in the filter.
    std::optional<study::SearchQuery> position;
    std::string position_of;
    if (db_search_.position || (db_search_open_ && search_tab_ == TabPosition)) {
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
    // The pattern: the language's when it says more than the men on squares, else the men alone (preview 1's search);
    // nothing when two of its conditions cannot both hold.
    std::string pattern_of;
    std::optional<study::SearchQuery> pattern;
    const auto full     = fullPattern(db_search_);
    const auto& l       = db_search_.language;
    const bool language = !(l.variables.empty() && l.conditions.empty() && l.line.empty());
    if (!study::contradiction(full)) {
        if (language) {
            pattern_of = base + "|L|" + study::writePattern(full);
            if (pattern_of != db_pattern_job_.of) pattern = languageQuery();  // its matcher made once
        } else if (const auto men = patternQuery()) {
            pattern    = *men;
            pattern_of = base + fmt::format("|M|{}{}|", db_search_.only_pawns[0], db_search_.only_pawns[1]);
            for (const auto v : db_search_.pattern) pattern_of += fmt::format("{},", v);
        }
    }
    poll(db_pattern_job_, pattern_of, std::move(pattern));
    // A man's route, once it has a move.
    std::string route_of;
    const auto route = routeQuery();
    if (route) {
        const auto& r = db_search_.route;
        route_of      = base + fmt::format("|R|{}{}|{}{}{}|", r.side, r.kind, r.others_between, r.same_man, r.within);
        for (const auto sq : r.squares) route_of += fmt::format("{},", sq);
        if (r.from_board) route_of += "|" + (r.board_fen.empty() ? boardFen() : r.board_fen);
    }
    poll(db_route_job_, route_of, route ? std::optional<study::SearchQuery>(*route) : std::nullopt);
    // The structure, when its tab shows or the filter has it.
    std::string structure_of;
    std::optional<study::StructureQuery> structure;
    if (db_search_.structure.set || (db_search_open_ && search_tab_ == TabStructure)) structure = structureQuery();
    if (structure)
        structure_of = base + fmt::format("|S|{:x},{:x}|{}{}{}{}|{}", structure->pawns[0], structure->pawns[1],
                                          structure->distance, structure->both_sides, structure->side,
                                          structure->reversed, structure->plies);
    poll(db_structure_job_, structure_of, structure ? std::optional<study::SearchQuery>(*structure) : std::nullopt);
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
    // The tabs of the mockups: the headers, the Board page's position, a material, a pattern (M1), a man's route and
    // a pawn structure (M13, board 2). Ctrl+Page Down and Ctrl+Page Up go from one to the next; Alt+T shows the
    // pattern's text.
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_PageDown)) search_select_ = (search_tab_ + 1) % SearchTabs;
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_PageUp))
        search_select_ = (search_tab_ + SearchTabs - 1) % SearchTabs;
    if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_T)) {
        pattern_text_open_ = search_tab_ != TabPattern || !pattern_text_open_;
        search_select_     = TabPattern;
    }
    const char* tabs[] = {TR("Headers"), TR("Position"),  TR("Material"),
                          TR("Pattern"), TR("Manoeuvre"), TR("Structure")};
    if (ImGui::BeginTabBar("##search_tabs")) {
        for (int i = 0; i < SearchTabs; ++i) {
            const auto flags = search_select_ == i ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem(tabs[i], nullptr, flags)) {
                search_tab_ = i;
                ImGui::EndTabItem();
            }
        }
        search_select_ = -1;
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
    } else if (search_tab_ == TabPattern) {
        ui::TopicRegion topic(guide::topics::Patterns);
        drawPatternTab();
    } else if (search_tab_ == TabRoute) {
        ui::TopicRegion topic(guide::topics::Manoeuvres);
        drawRouteTab();
    } else {
        ui::TopicRegion topic(guide::topics::PawnStructures);
        drawStructureTab();
    }

    ImGui::Separator();
    if (ImGui::Button(TR("See in the list")) ||
        (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::GetIO().WantTextInput)) {
        // The structure shown goes into the filter, as the route and the pattern are.
        if (search_tab_ == TabStructure) p.structure.set = true;
        db_search_open_ = false;
    }
    ImGui::SameLine(0, px(4));
    ImGui::TextDisabled("%s", TR("Enter"));
    ImGui::SameLine();
    if (ImGui::Button(TR("Clear everything"))) {
        p = SearchFields{};
        db_filter_.fill(0);
        pattern_text_.clear();
        pattern_error_.reset();
        step_by_board_ = false;
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

void App::drawChessBaseAsk() {
    if (!db_chessbase_) return;
    auto& a    = *db_chessbase_;
    auto& s    = shelf();
    bool go    = false;
    bool later = false;
    ui::beginCard("db_chessbase");
    ImGui::PushTextWrapPos();
    ImGui::TextUnformatted(a.file.stem().string().c_str());
    ImGui::SameLine(0, px(10));
    // The archive's kind by its name: CBV, CBZ, 2CBV, 2CBZ.
    auto kind = a.file.extension().string();
    if (!kind.empty()) kind.erase(0, 1);
    for (auto& c : kind) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    ImGui::TextDisabled("%s", (a.archive ? trf("ChessBase archive · {} · only read", kind)
                                         : trf("ChessBase database · {} · only read", a.format))
                                  .c_str());
    ui::topic(guide::topics::ChessBaseImport);
    if (!a.error.empty()) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::BAD), "%s", a.error.c_str());
    } else {
        if (!a.archive)
            ImGui::TextUnformatted(
                trn("According to its header file (.cbh), without opening its moves: {} record (a game, or a text)",
                    "According to its header file (.cbh), without opening its moves: {} records (games, and texts "
                    "if it has some)",
                    static_cast<int>(std::min<uint32_t>(a.records, 1000000000)), ui::exactCount(a.records))
                    .c_str());
        else
            ImGui::TextUnformatted(TR("An archive: unpacked while it is read, its games counted as they come."));
        ImGui::Spacing();
        ImGui::TextUnformatted(TR("Comes in"));
        ImGui::TextDisabled("%s", TR("The moves and their variations, the comments before and after the moves, the "
                                     "symbols, the coloured squares and arrows, the clocks and the time spent, the "
                                     "engines' evaluations, the medals, the critical positions, the time controls."));
        ImGui::TextUnformatted(TR("Stays out, counted at the end"));
        ImGui::TextDisabled("%s", TR("Training (questions and solutions), pictures, sounds and videos, web links, "
                                     "games quoted, pawn structures and piece paths, variation colours, the texts "
                                     "(articles)."));
        ImGui::Spacing();
        const bool new_one = a.index < 0 || a.index >= static_cast<int>(s.entries().size());
        const auto into    = new_one ? trf("Into a new database, “{}”", a.file.stem().string())
                                     : trf("Into “{}”", s.entries()[static_cast<std::size_t>(a.index)].name);
        std::string estimate;
        if (a.records > 0) {  // about 350 bytes and 25 µs a game, as measured on large databases
            estimate = trf(
                " · about {} · about {} on this machine", formatBytes(static_cast<uint64_t>(a.records) * 350),
                a.records < 2'400'000 ? std::string(TR("a minute or less")) : trf("{} min", a.records / 2'400'000 + 1));
        }
        ImGui::TextUnformatted((into + estimate).c_str());
        bool typed = false;  // Enter in the password field
        if (a.enciphered) {
            ImGui::SetNextItemWidth(px(260));
            if (a.focus) {
                ImGui::SetKeyboardFocusHere();
                a.focus = false;
            }
            typed = ImGui::InputTextWithHint("##cbz_password", TR("The archive's password"), a.password.data(),
                                             a.password.size(),
                                             ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue |
                                                 ImGuiInputTextFlags_AutoSelectAll);
            if (a.wrong_password) {
                ImGui::SameLine();
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::BAD), "%s",
                                   TR("Wrong password: type it again, then Enter."));
            }
        }
        go = ImGui::Button(TR("Import")) || typed ||
             (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Enter, false));
        ImGui::SameLine(0, px(4));
        ImGui::TextDisabled("%s", TR("Enter"));
        ImGui::SameLine();
    }
    later = ImGui::Button(TR("Not now")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    ImGui::SameLine(0, px(4));
    ImGui::TextDisabled("%s", TR("Esc"));
    ImGui::TextDisabled("%s", TR("The ChessBase files are only read: nothing is written beside them. Imported again, "
                                 "only its new games come in. An enciphered archive's password (.cbz, .2cbz) is asked "
                                 "here and not kept."));
    ImGui::PopTextWrapPos();
    ui::endCard();
    // A wrong password is told here, before anything is copied: it shows in the archive's first block.
    if (go && a.enciphered && a.error.empty() && !study::archivePasswordFits(a.file, a.password.data())) {
        a.wrong_password = true;
        a.focus          = true;
        go               = false;
    }
    if (go && a.error.empty() && !s.import()) {
        const auto ask = std::move(*db_chessbase_);
        db_chessbase_.reset();
        db_named_frame_        = ImGui::GetFrameCount();  // its Enter does not open the database's list too
        db_chessbase_go_       = true;
        db_chessbase_password_ = ask.password.data();
        importIntoDatabase(ask.index, ask.file, false);
        db_chessbase_go_ = false;
        db_chessbase_password_.clear();
    } else if (later) {
        db_chessbase_.reset();
    }
}

}  // namespace fcg
