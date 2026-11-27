// The Export sheet (#93, mockup M13, board 6), in place of the page it is opened from (Esc goes back, Enter saves): its
// formats as chips (the one chosen last time first), the preview on its left as the file will be, each format's
// settings on its right, kept in export.json; what can go wrong, said with what mends it. Opened from the Board page
// ("Export…" under the board, Ctrl+K), the Databases page (the chosen game or the filter's games › Export…), the
// Openings page (a repertoire › Print…) and each report (Print…, HTML page). Ctrl+Shift+C copies the Board page's
// diagram from anywhere on the page. The files are made by publish_*.hpp; nothing leaves the machine.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>

#include <SDL3/SDL_clipboard.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include <study/game_tree.hpp>
#include <study/pgn_export.hpp>
#include <study/pgn_reader.hpp>

#include "app.hpp"
#include "board_text.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "publish_pdf.hpp"
#include "publish_reports.hpp"
#include "renderer.hpp"
#include "ui.hpp"
#include "widgets.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

const char* formatLabel(ExportFormat f) {
    switch (f) {
        case ExportFormat::Html:
            return TR("HTML page");
        case ExportFormat::Diagram:
            return TR("Diagram, PNG or SVG");
        case ExportFormat::Gif:
            return TR("Animated GIF");
        case ExportFormat::Pdf:
            return TR("Print · PDF");
        case ExportFormat::Pgn:
            return "PGN";
    }
    return "";
}

ui::Icon formatIcon(ExportFormat f) {
    switch (f) {
        case ExportFormat::Html:
            return ui::Icon::FileCode;
        case ExportFormat::Diagram:
            return ui::Icon::Image;
        case ExportFormat::Gif:
            return ui::Icon::Film;
        case ExportFormat::Pdf:
            return ui::Icon::Printer;
        case ExportFormat::Pgn:
            return ui::Icon::FileText;
    }
    return ui::Icon::File;
}

const char* extensionOf(ExportFormat f, bool svg) {
    switch (f) {
        case ExportFormat::Html:
            return ".html";
        case ExportFormat::Diagram:
            return svg ? ".svg" : ".png";
        case ExportFormat::Gif:
            return ".gif";
        case ExportFormat::Pdf:
            return ".pdf";
        case ExportFormat::Pgn:
            return ".pgn";
    }
    return "";
}

// A chip of the formats: its icon and label, framed in the accent when chosen. True when clicked.
bool formatChip(const char* id, ui::Icon icon, const char* label, bool on) {
    const ImVec2 pad(px(12), px(6));
    const float icon_w = ImGui::GetFontSize();
    const ImVec2 text  = ImGui::CalcTextSize(label);
    const ImVec2 size(pad.x * 2 + icon_w + px(6) + text.x, pad.y * 2 + text.y);
    const ImVec2 pos   = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    auto* dl           = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + size.x, pos.y + size.y);
    if (on) {
        dl->AddRectFilled(pos, end, (ui::ACCENT & 0x00ffffff) | 0x22000000, px(8));
        dl->AddRect(pos, end, ui::ACCENT, px(8), 0, 1.5f);
    } else {
        if (hovered) dl->AddRectFilled(pos, end, ImGui::GetColorU32(ImGuiCol_FrameBgHovered), px(8));
        dl->AddRect(pos, end, ImGui::GetColorU32(ImGuiCol_Border), px(8));
    }
    const ImU32 col = on ? ui::ACCENT : ImGui::GetColorU32(ImGuiCol_Text);
    ui::drawIcon(dl, icon, ImVec2(pos.x + pad.x + icon_w * 0.5f, pos.y + size.y * 0.5f), icon_w, col);
    dl->AddText(ImVec2(pos.x + pad.x + icon_w + px(6), pos.y + pad.y), col, label);
    return clicked;
}

// A choice among a few, as a row of small buttons, the chosen one framed. True when changed.
bool choice(const char* id, int& value, const std::vector<std::string>& labels) {
    bool changed = false;
    ImGui::PushID(id);
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (i > 0) ImGui::SameLine(0, px(4));
        const bool on = value == static_cast<int>(i);
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::ColorConvertU32ToFloat4((ui::ACCENT & 0x00ffffff) | 0x33000000));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
        }
        if (ImGui::SmallButton(fmt::format("{}##{}", labels[i], i).c_str()) && !on) {
            value   = static_cast<int>(i);
            changed = true;
        }
        if (on) ImGui::PopStyleColor(2);
    }
    ImGui::PopID();
    return changed;
}

ImFont* pageFont(publish::FontId id) {
    switch (id) {
        case publish::FontId::SemiBold:
            return ui::titleFont() ? ui::titleFont() : ImGui::GetIO().FontDefault;
        case publish::FontId::Italic:
            return ui::italicFont();
        default:
            return ImGui::GetIO().FontDefault;
    }
}

ImU32 imColour(publish::Rgba c) { return IM_COL32(c.r, c.g, c.b, c.a); }

// Spans wrapped to `width` with their colours, from the line `first` on, `lines` at most; the span of `node` framed.
// The line of that node, for the next call.
int drawSpans(const std::vector<publish::Span>& spans, float width, int first, int lines, int node) {
    struct Word {
        std::string text;
        ImU32 colour;
        bool bold, on;
        bool joined;  // glued to the word before it ("12." and "g5"): no space between them, nor a row
        float w;
    };
    // The words, those glued together in a group that goes to the next row as one.
    std::vector<std::vector<Word>> groups;
    const float sp   = ImGui::CalcTextSize(" ").x;
    bool after_space = true;  // the text so far ends with a space: the next word starts a group
    for (const auto& s : spans) {
        std::size_t at = 0;
        bool glued     = !s.text.empty() && s.text.front() != ' ' && !after_space;
        if (!s.text.empty()) after_space = s.text.back() == ' ';
        while (at <= s.text.size()) {
            const auto end  = std::min(s.text.find(' ', at), s.text.size());
            const auto word = s.text.substr(at, end - at);
            if (!word.empty()) {
                ImFont* font    = s.font == publish::FontId::SemiBold ? pageFont(s.font) : ImGui::GetFont();
                const float w   = font->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0, word.c_str()).x;
                const bool join = glued && !groups.empty();
                if (!join) groups.emplace_back();
                groups.back().push_back({word, imColour(s.colour), s.font == publish::FontId::SemiBold,
                                         s.node == node && node >= 0, join, w});
            }
            glued = false;
            at    = end + 1;
        }
    }
    std::vector<std::vector<Word>> rows(1);
    float x      = 0;
    int node_row = -1;
    for (const auto& g : groups) {
        float gw = 0;
        for (const auto& w : g) gw += w.w;
        if (!rows.back().empty() && x + sp + gw > width) {
            rows.emplace_back();
            x = 0;
        }
        if (!rows.back().empty()) x += sp;
        for (const auto& w : g) {
            if (w.on) node_row = static_cast<int>(rows.size()) - 1;
            rows.back().push_back(w);
        }
        x += gw;
    }
    auto* dl           = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float h      = ImGui::GetTextLineHeightWithSpacing();
    int drawn          = 0;
    for (int r = std::max(0, first); r < static_cast<int>(rows.size()) && drawn < lines; ++r, ++drawn) {
        float wx = start.x;
        for (std::size_t i = 0; i < rows[static_cast<std::size_t>(r)].size(); ++i) {
            const auto& w = rows[static_cast<std::size_t>(r)][i];
            if (i > 0 && !w.joined) wx += sp;
            if (w.on)
                dl->AddRectFilled(ImVec2(wx - px(2), start.y + drawn * h),
                                  ImVec2(wx + w.w + px(2), start.y + drawn * h + h - px(2)),
                                  (ui::ACCENT & 0x00ffffff) | 0x40000000, px(3));
            dl->AddText(w.bold ? pageFont(publish::FontId::SemiBold) : ImGui::GetFont(), ImGui::GetFontSize(),
                        ImVec2(wx, start.y + drawn * h), w.colour, w.text.c_str());
            wx += w.w;
        }
    }
    ImGui::Dummy(ImVec2(width, h * static_cast<float>(drawn)));
    return node_row;
}

// A clipboard image: its PNG, and a BMP for the programs of Windows that want one, until the clipboard lets it go.
struct ClipboardImage {
    std::string png, bmp;
};
const void* clipboardData(void* userdata, const char* mime, std::size_t* size) {
    const auto* image = static_cast<const ClipboardImage*>(userdata);
    if (mime && std::string_view(mime) == "image/bmp") {
        *size = image->bmp.size();
        return image->bmp.data();
    }
    *size = image->png.size();
    return image->png.data();
}
void clipboardCleanup(void* userdata) { delete static_cast<ClipboardImage*>(userdata); }

bool writeFile(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << bytes;
    return static_cast<bool>(out);
}

std::string pathText(const fs::path& path) {
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

// A long path by its last folders: "…/p4-93/out/ro".
std::string shortPath(const fs::path& path) {
    const auto whole = pathText(path);
    if (whole.size() <= 48) return whole;
    std::vector<std::string> parts;
    for (const auto& part : path) parts.push_back(pathText(part));
    std::string out;
    for (std::size_t i = parts.size() > 3 ? parts.size() - 3 : 0; i < parts.size(); ++i) out += "/" + parts[i];
    return "…" + out;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Opening it

ExportSettings& App::exportSettings() {
    if (!export_settings_loaded_) {
        export_settings_        = loadExportSettings(storage_.root());
        export_settings_loaded_ = true;
    }
    return export_settings_;
}

void App::keepExportSettings() { saveExportSettings(storage_.root(), exportSettings()); }

std::shared_ptr<const publish::PieceShapes> App::exportPieces() {
    const auto& a        = appearance::current();
    const std::string of = a.pieces + "|" + a.piece_folder;
    if (!export_pieces_ || of != export_pieces_of_) {
        std::string error;
        export_pieces_    = publish::PieceShapes::make(appearance::piecesFor(a, error));
        export_pieces_of_ = of;
    }
    return export_pieces_;
}

publish::DocumentLook App::exportLook() {
    publish::DocumentLook look;
    look.pieces  = exportPieces();
    look.notice  = publish::piecesNotice(appearance::current().pieces);
    look.colours = appearance::boardColours();
    look.lang    = i18n::info(i18n::language()).code;
    return look;
}

void App::showExport(ExportSheet sheet, int format) {
    auto& st = exportSettings();
    int f    = format >= 0 ? format : st.format;
    if (std::find(sheet.formats.begin(), sheet.formats.end(), static_cast<ExportFormat>(f)) == sheet.formats.end())
        f = static_cast<int>(sheet.formats.front());
    st.format = f;
    // The diagram's caption from the side to move, as the user may then write it.
    const auto line = lineOf(sheet.game.tree, sheet.game.line);
    const auto board =
        boardAt(line, sheet.game.chess960, std::clamp(sheet.game.ply, 0, static_cast<int>(line.moves.size())));
    const std::string caption = board.sideToMove() == chess::Color::WHITE ? TR("White to play") : TR("Black to play");
    std::copy_n(caption.data(), std::min(caption.size(), sheet.caption.size() - 1), sheet.caption.data());
    sheet.over = page_;
    closeExport();
    export_ = std::move(sheet);
}

void App::closeExport() {
    if (!export_) return;
    if (export_->job.valid()) {
        if (export_->cancel) *export_->cancel = true;
        export_->job.wait();
    }
    if (export_->estimate_job.valid()) export_->estimate_job.wait();
    if (export_->texture != ImTextureID_Invalid) export_old_textures_.push_back(export_->texture);
    export_.reset();
}

void App::openBoardExport(int format) {
    auto& b = board_;
    ExportSheet s;
    s.subject          = ExportSheet::Subject::Board;
    s.game.tree        = b.tree;
    s.game.chess960    = b.chess960;
    s.game.start_notes = b.start_notes;
    s.game.line        = b.line;
    s.game.ply         = b.ply;
    // Its players as the table of names writes them, when it is a database's game.
    if (board_origin_ && board_origin_->view && board_origin_->view->snapshot) {
        const auto& snap = *board_origin_->view->snapshot;
        if (board_origin_->id < snap.games()) {
            const auto ids = snap.rowIds(board_origin_->id);
            if (std::string(snap.name(ids.white)) == b.tree.main.header("White") &&
                std::string(snap.name(ids.black)) == b.tree.main.header("Black")) {
                s.game.white = std::string(snap.shownName(ids.white));
                s.game.black = std::string(snap.shownName(ids.black));
            }
        }
    }
    // The games of the list it was opened from, too: the page holds this game or the filter's games.
    if (board_origin_ && board_origin_->view && board_origin_->view->snapshot && board_origin_->view->ids.size() > 1) {
        s.snapshot = board_origin_->view->snapshot;
        s.ids      = board_origin_->view->ids;
        s.chosen   = board_origin_->id;
        s.all_text =
            trn("the {} game of the filter", "the {} games of the filter", s.ids.size(), ui::exactCount(s.ids.size()));
        s.all_title = board_origin_->name;
        s.all_stem  = publish::fileStem(board_origin_->name);
        if (s.all_stem.empty()) s.all_stem = "games";
    }
    // Its key moments: the review's mistakes and blunders.
    for (std::size_t i = 0; i < b.judged.size(); ++i)
        if (b.judged[i].judgement == Judgement::Mistake || b.judged[i].judgement == Judgement::Blunder)
            s.key_plies.push_back(static_cast<int>(i) + 1);
    s.subject_text = b.title.empty() ? publish::gameTitle(s.game) : b.title;
    const auto& m  = b.tree.main;
    s.stem         = publish::fileStem(fmt::format("{} {} {}", m.header("White").substr(0, m.header("White").find(',')),
                                                   m.header("Black").substr(0, m.header("Black").find(',')),
                                                   m.header("Date").substr(0, 4)));
    if (s.stem.empty()) s.stem = "game";
    s.formats = {ExportFormat::Html, ExportFormat::Diagram, ExportFormat::Gif, ExportFormat::Pdf};
    showExport(std::move(s), format);
}

std::vector<publish::HtmlGame> App::exportGames(std::size_t most) const {
    std::vector<publish::HtmlGame> out;
    if (!export_ || !export_->snapshot) return out;
    const auto& e    = *export_;
    const auto& snap = *e.snapshot;
    const auto read  = [&](study::GameId id) {
        auto r = study::readPgnGame(study::exportedPgn(snap, id, false));
        if (!r.tree) return;
        publish::HtmlGame g;
        g.tree         = std::move(*r.tree);
        g.chess960     = r.chess960;
        const auto ids = snap.rowIds(id);
        g.white        = std::string(snap.shownName(ids.white));
        g.black        = std::string(snap.shownName(ids.black));
        out.push_back(std::move(g));
    };
    if (!e.all) {
        read(e.chosen);
        return out;
    }
    for (std::size_t i = 0; i < e.ids.size() && out.size() < most; ++i) read(e.ids[i]);
    return out;
}

void App::openDatabaseExport(bool all, int format) {
    auto& s          = shelf();
    const auto index = static_cast<std::size_t>(db_selected_);
    if (db_selected_ < 0 || index >= s.entries().size() || !db_view_ || !db_view_->snapshot) return;
    ExportSheet e;
    e.subject  = ExportSheet::Subject::Database;
    e.folder   = s.entries()[index].folder;
    e.snapshot = db_view_->snapshot;
    e.ids      = db_view_->ids;
    e.all      = all && e.ids.size() > 1;
    if (e.ids.empty()) return;
    e.chosen = e.ids[std::min(db_row_, e.ids.size() - 1)];
    // The chosen game read now: the page, the GIF and the PDF are made of it.
    const auto& snap = *e.snapshot;
    auto r           = study::readPgnGame(study::exportedPgn(snap, e.chosen, false));
    if (r.tree) {
        e.game.tree     = std::move(*r.tree);
        e.game.chess960 = r.chess960;
    }
    const auto ids = snap.rowIds(e.chosen);
    e.game.white   = std::string(snap.shownName(ids.white));
    e.game.black   = std::string(snap.shownName(ids.black));
    e.game.ply     = 0;
    e.subject_text = publish::gameTitle(e.game);
    e.stem         = publish::fileStem(fmt::format("{} {} {}", e.game.white.substr(0, e.game.white.find(',')),
                                                   e.game.black.substr(0, e.game.black.find(',')),
                                                   e.game.tree.main.header("Date").substr(0, 4)));
    if (e.stem.empty()) e.stem = "game";
    e.all_text =
        trn("the {} game of the filter", "the {} games of the filter", e.ids.size(), ui::exactCount(e.ids.size()));
    e.all_title = s.entries()[index].name;
    e.all_stem  = publish::fileStem(s.entries()[index].name);
    if (e.all_stem.empty()) e.all_stem = "games";
    e.formats = {ExportFormat::Html, ExportFormat::Gif, ExportFormat::Pdf, ExportFormat::Pgn};
    showExport(std::move(e), format);
}

void App::openRepertoirePrint(std::size_t index) {
    auto& r = repertoires();
    if (index >= r.entries().size()) return;
    ExportSheet e;
    e.subject      = ExportSheet::Subject::Repertoire;
    e.repertoire   = static_cast<int>(index);
    e.subject_text = r.entries()[index].name;
    e.stem         = publish::fileStem(r.entries()[index].name);
    if (e.stem.empty()) e.stem = "repertoire";
    e.formats = {ExportFormat::Pdf};
    showExport(std::move(e), static_cast<int>(ExportFormat::Pdf));
}

void App::openDossierExport(bool html) {
    if (!dossier_ || !dossier_->report || !dossier_->report->dossier) return;
    std::vector<std::string> sources;
    for (const auto& [folder, name] : dossier_->folders) sources.push_back(name);
    ExportSheet e;
    e.subject      = ExportSheet::Subject::Dossier;
    e.subject_text = trf("the dossier of {}", dossier_->subject.name);
    e.stem         = publish::fileStem(dossier_->subject.name);
    if (e.stem.empty()) e.stem = "dossier";
    const auto letter = exportSettings().paper < 0 ? publish::systemLetter() : exportSettings().paper == 1;
    e.document = dossierDocument(*dossier_->report, dossier_->subject.name, sources, !player_names_.empty(), letter,
                                 exportLook());
    e.formats  = {ExportFormat::Html, ExportFormat::Pdf};
    showExport(std::move(e), static_cast<int>(html ? ExportFormat::Html : ExportFormat::Pdf));
}

void App::openReportExport(bool html) {
    if (!opening_report_ || !opening_report_->report) return;
    auto& r = *opening_report_;
    ReportView view;
    view.board   = r.board;
    view.path    = r.path;
    view.opening = r.opening;
    view.report  = &*r.report;
    view.rows    = &r.rows;
    view.evals   = &r.evals;
    ExportSheet e;
    e.subject = ExportSheet::Subject::Report;
    e.subject_text =
        r.opening.empty() ? std::string(TR("the opening report")) : trf("the opening report: {}", r.opening);
    e.stem            = publish::fileStem(r.opening.empty() ? std::string("opening report") : r.opening.substr(0, 40));
    const auto letter = exportSettings().paper < 0 ? publish::systemLetter() : exportSettings().paper == 1;
    e.document        = openingReportDocument(view, r.bases, r.summary ? &*r.summary : nullptr, letter, exportLook());
    e.formats         = {ExportFormat::Html, ExportFormat::Pdf};
    showExport(std::move(e), static_cast<int>(html ? ExportFormat::Html : ExportFormat::Pdf));
}

void App::openCrosstableExport(bool html) {
    if (!crosstable_ || !crosstable_->input) return;
    auto& c = *crosstable_;
    ExportSheet e;
    e.subject      = ExportSheet::Subject::Crosstable;
    e.subject_text = trf("the crosstable of {}", c.title);
    e.stem         = publish::fileStem(c.title);
    if (e.stem.empty()) e.stem = "crosstable";
    const auto letter = exportSettings().paper < 0 ? publish::systemLetter() : exportSettings().paper == 1;
    e.document =
        crosstableDocument(c.table, c.title, static_cast<uint32_t>(c.input->games.size()), letter, exportLook());
    e.formats = {ExportFormat::Html, ExportFormat::Pdf};
    showExport(std::move(e), static_cast<int>(html ? ExportFormat::Html : ExportFormat::Pdf));
}

// ---------------------------------------------------------------------------------------------------------------------
// What it makes

publish::DiagramSpec App::exportDiagramSpec() const {
    const auto& st = export_settings_;
    // The sheet's game, else the Board page's (Ctrl+Shift+C).
    const publish::HtmlGame* g = export_ ? &export_->game : nullptr;
    const auto& tree           = g ? g->tree : board_.tree;
    const int line_index       = g ? g->line : board_.line;
    const int ply              = g ? g->ply : board_.ply;
    const bool chess960        = g ? g->chess960 : board_.chess960;
    const auto& start          = g ? g->start_notes : board_.start_notes;
    const auto line            = lineOf(tree, line_index);
    auto spec = publish::diagramAt(line, chess960, std::clamp(ply, 0, static_cast<int>(line.moves.size())), &start,
                                   appearance::boardColours());
    spec.coordinates     = st.coordinates;
    spec.side_dot        = st.side_dot;
    spec.black_and_white = st.grey;
    spec.flipped         = st.black_below && spec.side == chess::Color::BLACK;
    if (!st.diagram_marks) {
        spec.arrows.clear();
        spec.squares.clear();
    }
    if (!st.last_move) spec.last_move.reset();
    if (export_) spec.caption = export_->caption.data();
    return spec;
}

publish::GifOptions App::exportGifOptions() const {
    const auto& st = export_settings_;
    publish::GifOptions o;
    o.size      = st.gif_size;
    o.delay_ms  = st.gif_delay;
    o.last_ms   = st.gif_last;
    o.banner    = st.gif_banner;
    o.last_move = st.gif_last_move;
    o.arrows    = st.gif_arrows;
    if (export_) {
        o.from = export_->gif_from;
        o.to   = export_->gif_to;
    }
    return o;
}

publish::Document App::exportDocument() {
    auto& e           = *export_;
    const auto& st    = exportSettings();
    const bool letter = st.paper < 0 ? publish::systemLetter() : st.paper == 1;
    if (e.document) {
        auto d   = *e.document;
        d.letter = letter;
        return d;
    }
    if (e.subject == ExportSheet::Subject::Repertoire) {
        auto& r = repertoires();
        if (e.repertoire < 0 || static_cast<std::size_t>(e.repertoire) >= r.entries().size()) return {};
        const auto& file = r.entries()[static_cast<std::size_t>(e.repertoire)];
        std::optional<study::Coverage> cover;
        if (coverage_ && r.active() == e.repertoire) cover = *coverage_;
        return publish::repertoireDocument(file.name, file.side, file.lines, cover, letter, exportLook());
    }
    publish::PrintGame g;
    g.tree        = e.game.tree;
    g.chess960    = e.game.chess960;
    g.start_notes = e.game.start_notes;
    g.white       = e.game.white;
    g.black       = e.game.black;
    g.key_plies   = e.key_plies;
    publish::GamePrintOptions o;
    o.letter    = letter;
    o.columns   = st.columns;
    o.diagrams  = static_cast<publish::DiagramsAt>(std::clamp(st.diagrams, 0, 3));
    o.assistant = st.pdf_assistant;
    o.evals     = st.pdf_evals;
    o.headers   = st.pdf_headers;
    return publish::gameDocument(g, o, exportLook());
}

std::string App::exportFileName(ExportFormat format) const {
    if (!export_) return {};
    const bool all =
        export_->all && !export_->document && (format == ExportFormat::Html || format == ExportFormat::Pgn);
    return (all ? export_->all_stem : export_->stem) + extensionOf(format, export_settings_.svg);
}

bool App::exportAll() const {
    if (!export_ || !export_->all || export_->document) return false;
    const auto format = static_cast<ExportFormat>(export_settings_.format);
    return format == ExportFormat::Html || format == ExportFormat::Pgn;
}

// ---------------------------------------------------------------------------------------------------------------------
// Saving

void App::exportSave() {
    if (!export_ || export_->job.valid()) return;
    auto& e           = *export_;
    const auto& st    = exportSettings();
    const auto format = static_cast<ExportFormat>(st.format);
    e.problem         = ExportSheet::Problem::None;
    e.error.clear();
    if (format == ExportFormat::Pdf && e.subject == ExportSheet::Subject::Repertoire) {
        auto& r = repertoires();
        if (e.repertoire < 0 || static_cast<std::size_t>(e.repertoire) >= r.entries().size() ||
            r.entries()[static_cast<std::size_t>(e.repertoire)].lines.main.moves.empty()) {
            e.problem = ExportSheet::Problem::Empty;
            return;
        }
    }
    // Past MAX_GAMES_PER_PAGE games, the page waits for its choice (a page per game, or the first ones): Enter too.
    if (format == ExportFormat::Html && exportAll() && e.ids.size() > publish::MAX_GAMES_PER_PAGE && e.split == 0)
        return;
    std::vector<std::pair<std::string, std::string>> filters;
    switch (format) {
        case ExportFormat::Html:
            filters = {{TR("HTML page"), "html;htm"}};
            break;
        case ExportFormat::Diagram:
            filters = st.svg ? std::vector<std::pair<std::string, std::string>>{{TR("SVG image"), "svg"}}
                             : std::vector<std::pair<std::string, std::string>>{{TR("PNG image"), "png"}};
            break;
        case ExportFormat::Gif:
            filters = {{TR("Animated GIF"), "gif"}};
            break;
        case ExportFormat::Pdf:
            filters = {{"PDF", "pdf"}};
            break;
        case ExportFormat::Pgn:
            filters = {{"PGN", "pgn"}};
            break;
    }
    std::error_code ec;
    fs::path folder = st.folder.empty() ? fs::path() : fs::path(st.folder);
    if (folder.empty() || !fs::is_directory(folder, ec)) folder = storage_.root();
    dialogs_.saveFile(Dialogs::Purpose::Export, filters, pathText(folder / exportFileName(format)));
}

void App::exportTo(const fs::path& chosen) {
    if (!export_ || export_->job.valid()) return;
    auto& e           = *export_;
    auto& st          = exportSettings();
    const auto format = static_cast<ExportFormat>(st.format);
    fs::path path     = chosen;
    if (path.extension().empty()) path += extensionOf(format, st.svg);
    const auto folder = path.parent_path();
    if (!folderWritable(folder)) {
        e.problem      = ExportSheet::Problem::Folder;
        e.problem_path = folder;
        return;
    }
    e.problem = ExportSheet::Problem::None;
    st.folder = pathText(folder);
    keepExportSettings();
    const auto done = [&](bool ok, const fs::path& written) {
        if (ok) {
            e.saved = written;
            e.said  = trf("Saved: {}", shortPath(written));
            e.error.clear();
        } else {
            e.error = trf("Cannot write {}", pathText(written));
        }
    };
    const auto look = exportLook();
    switch (format) {
        case ExportFormat::Html: {
            if (e.document) {
                auto d = *e.document;
                done(writeFile(path, publish::documentHtml(d)), path);
                break;
            }
            publish::HtmlOptions o;
            o.comments   = st.html_comments;
            o.variations = st.html_variations;
            o.marks      = st.html_marks;
            o.assistant  = st.html_assistant;
            o.evals      = st.html_evals;
            o.clocks     = st.html_clocks;
            publish::HtmlLook l;
            l.lang           = look.lang;
            l.colours        = look.colours;
            l.pieces         = look.pieces;
            l.pieces_notice  = look.notice.line;
            l.pieces_license = look.notice.license;
            if (!e.all || !e.snapshot) {
                done(writeFile(path, publish::gamesHtml({e.game}, o, l, publish::gameTitle(e.game))), path);
                break;
            }
            // The filter's games, in the background: one page, or a folder of pages with their list.
            const bool split = e.ids.size() > publish::MAX_GAMES_PER_PAGE && e.split == 1;
            const auto most =
                e.ids.size() > publish::MAX_GAMES_PER_PAGE && e.split != 1 ? publish::MAX_GAMES_PER_PAGE : e.ids.size();
            e.cancel = std::make_shared<std::atomic<bool>>(false);
            e.done   = std::make_shared<std::atomic<int>>(0);
            e.total  = std::make_shared<std::atomic<int>>(static_cast<int>(most));
            e.saving = split ? folder / path.stem() : path;
            std::vector<study::GameId> ids(e.ids.begin(), e.ids.begin() + static_cast<std::ptrdiff_t>(most));
            e.job = std::async(std::launch::async,
                               [snap = e.snapshot, ids, o, l, split, target = e.saving, cancel = e.cancel,
                                count = e.done, title = e.all_title]() -> std::string {
                                   const auto read = [&](std::size_t i) -> std::optional<publish::HtmlGame> {
                                       auto r = study::readPgnGame(study::exportedPgn(*snap, ids[i], false));
                                       if (!r.tree) return std::nullopt;
                                       publish::HtmlGame g;
                                       g.tree         = std::move(*r.tree);
                                       g.chess960     = r.chess960;
                                       const auto rid = snap->rowIds(ids[i]);
                                       g.white        = std::string(snap->shownName(rid.white));
                                       g.black        = std::string(snap->shownName(rid.black));
                                       return g;
                                   };
                                   if (split)
                                       return publish::writeGamePages(target, ids.size(), read, o, l, title,
                                                                      cancel.get(), count.get());
                                   std::vector<publish::HtmlGame> games;
                                   for (std::size_t i = 0; i < ids.size(); ++i) {
                                       if (*cancel) return TR("Cancelled.");
                                       if (auto g = read(i)) games.push_back(std::move(*g));
                                       ++*count;
                                   }
                                   return writeFile(target, publish::gamesHtml(games, o, l, title))
                                              ? std::string()
                                              : trf("Cannot write {}", pathText(target));
                               });
            break;
        }
        case ExportFormat::Diagram: {
            const auto spec   = exportDiagramSpec();
            const int board   = st.dense ? 960 : 480;
            const auto pieces = exportPieces();
            if (st.svg)
                done(writeFile(path, publish::diagramSvg(spec, *pieces, false, 480)), path);
            else
                done(writeFile(path, publish::pngBytes(publish::diagramImage(spec, *pieces, board), look.notice.line)),
                     path);
            break;
        }
        case ExportFormat::Gif: {
            publish::GifGame g;
            g.game        = lineOf(e.game.tree, e.game.line);
            g.chess960    = e.game.chess960;
            g.start_notes = e.game.start_notes;
            g.banner =
                trf("{} – {} · {}", e.game.white.empty() ? g.game.header("White") : e.game.white,
                    e.game.black.empty() ? g.game.header("Black") : e.game.black, publish::resultText(g.game.result));
            g.colours          = look.colours;
            g.pieces           = look.pieces;
            const auto options = exportGifOptions();
            auto progress      = std::make_shared<publish::GifProgress>();
            const auto range   = publish::gifRange(g, options);
            progress->total    = range.second - range.first + 1;
            e.cancel           = std::make_shared<std::atomic<bool>>(false);
            e.done             = std::shared_ptr<std::atomic<int>>(progress, &progress->done);
            e.total            = std::shared_ptr<std::atomic<int>>(progress, &progress->total);
            e.saving           = path;
            e.job = std::async(std::launch::async, [g, options, path, progress, cancel = e.cancel]() -> std::string {
                const auto bytes = publish::makeGif(g, options, progress.get(), cancel.get());
                if (bytes.empty()) return TR("Cancelled.");
                return writeFile(path, bytes) ? std::string() : trf("Cannot write {}", pathText(path));
            });
            break;
        }
        case ExportFormat::Pdf: {
            const auto doc = exportDocument();
            done(
                writeFile(path, publish::pdfBytes(publish::layOut(doc), look.pieces.get(), {doc.title, look.lang, ""})),
                path);
            break;
        }
        case ExportFormat::Pgn: {
            if (!e.snapshot) break;
            if (!e.all) {
                done(writeFile(path, study::exportedPgn(*e.snapshot, e.chosen, st.write_names) + "\n"), path);
                break;
            }
            e.cancel = std::make_shared<std::atomic<bool>>(false);
            e.done   = std::make_shared<std::atomic<int>>(0);
            e.total  = std::make_shared<std::atomic<int>>(static_cast<int>(e.ids.size()));
            e.saving = path;
            e.job    = std::async(std::launch::async,
                                  [snap = e.snapshot, ids = e.ids, names = st.write_names, path, cancel = e.cancel,
                                   count = e.done]() -> std::string {
                                   std::ofstream out(path, std::ios::binary | std::ios::trunc);
                                   if (!out) return trf("Cannot write {}", pathText(path));
                                   for (const auto id : ids) {
                                       if (*cancel) return TR("Cancelled.");
                                       out << study::exportedPgn(*snap, id, names) << "\n";
                                       ++*count;
                                   }
                                   return out ? std::string() : trf("Cannot write {}", pathText(path));
                                  });
            break;
        }
    }
}

void App::exportPrint() {
    if (!export_) return;
    auto& e = *export_;
    if (e.subject == ExportSheet::Subject::Repertoire) {
        auto& r = repertoires();
        if (e.repertoire < 0 || static_cast<std::size_t>(e.repertoire) >= r.entries().size() ||
            r.entries()[static_cast<std::size_t>(e.repertoire)].lines.main.moves.empty()) {
            e.problem = ExportSheet::Problem::Empty;
            return;
        }
    }
    const auto look   = exportLook();
    const auto doc    = exportDocument();
    const auto folder = storage_.root() / "prints";
    std::error_code ec;
    fs::create_directories(folder, ec);
    const auto file = folder / (e.stem + ".pdf");
    if (!writeFile(file, publish::pdfBytes(publish::layOut(doc), look.pieces.get(), {doc.title, look.lang, ""}))) {
        e.problem      = ExportSheet::Problem::Folder;
        e.problem_path = folder;
        return;
    }
    e.saved   = file;
    e.problem = ExportSheet::Problem::None;
    // No reader: the PDF stays where it is, its folder opened.
    if (!systemHasPdfReader() || !open_file_(file)) {
        e.problem = ExportSheet::Problem::NoReader;
        (void)open_file_(folder);
        return;
    }
    e.said = trf("Opened in the system's reader: {}", shortPath(file));
}

void App::copyDiagram() {
    auto& st            = exportSettings();
    const auto spec     = exportDiagramSpec();
    const auto pieces   = exportPieces();
    const auto image    = publish::diagramImage(spec, *pieces, st.dense ? 960 : 480);
    auto* data          = new ClipboardImage{publish::pngBytes(image), publish::bmpBytes(image)};
    const char* mimes[] = {"image/png", "image/bmp"};
    if (SDL_SetClipboardData(clipboardData, clipboardCleanup, data, mimes, 2)) {
        boardToast(TR("The diagram is in the clipboard"));
        if (export_) export_->said = TR("The diagram is in the clipboard");
    } else {
        boardToast(trf("The clipboard refused the image: {}", SDL_GetError()));
    }
}

void App::pollExport() {
    for (const auto t : export_old_textures_) render::destroyTexture(t);
    export_old_textures_.clear();
    if (!export_) return;
    auto& e = *export_;
    if (e.job.valid() && e.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::string error;
        try {
            error = e.job.get();
        } catch (const std::exception& ex) {
            // A database that cannot be read: said on its error line.
            error = loggedStoreError(trf("The games could not be read: {}.", storeErrorText(ex)), ex);
        }
        if (error.empty()) {
            e.saved = e.saving;
            e.said  = trf("Saved: {}", shortPath(e.saving));
            e.error.clear();
        } else {
            e.error = error;
        }
    }
    if (e.estimate_job.valid() && e.estimate_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        e.estimate    = e.estimate_job.get();
        e.estimate_of = e.estimating;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The sheet

namespace {

// A problem's card: its icon, what happened, what it means, and the gestures that mend it.
void problemCard(const char* id, ui::Icon icon, bool warn, const std::string& title, const std::string& text) {
    ui::beginCard(id);
    ui::inlineIcon(icon, warn ? ui::WARN : ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos();
    if (warn)
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", title.c_str());
    else
        ImGui::TextUnformatted(title.c_str());
    if (!text.empty()) ImGui::TextDisabled("%s", text.c_str());
    ImGui::PopTextWrapPos();
}

void endProblemCard() {
    ImGui::EndGroup();
    ui::endCard();
}

// The sheet's primary buttons keep a translucent hover until a mockup settles it (#110).
bool primaryButton(const char* label) { return ui::primaryButton(label, true, 0, ui::PrimaryHover::Translucent); }

}  // namespace

void App::drawExport() {
    auto& e  = *export_;
    auto& st = exportSettings();
    const ui::TopicRegion topic(guide::topics::Export);
    const bool typing = ImGui::GetIO().WantTextInput;
    const bool popup  = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup);
    if (!typing && !popup && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        closeExport();
        return;
    }
    // What it is, and the way back.
    ImGui::AlignTextToFramePadding();
    ui::inlineIcon(ui::Icon::Share, ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize() * 1.3f);
    ImGui::TextUnformatted(TR("Export"));
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("· %s", (exportAll() ? e.all_text : e.subject_text).c_str());
    {
        const char* back = TR("Esc goes back");
        const float w    = ImGui::CalcTextSize(back).x + ImGui::GetFrameHeight() + px(8);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(12), ImGui::GetContentRegionMax().x - w));
        ImGui::TextDisabled("%s", back);
        ImGui::SameLine();
        ui::guideLink("export_help", guide::topics::Export);
    }
    // The formats, the one chosen last time first; Ctrl+Page Down and Ctrl+Page Up go through them.
    ImGui::Dummy(ImVec2(0, px(2)));
    auto at = std::find(e.formats.begin(), e.formats.end(), static_cast<ExportFormat>(st.format));
    if (at == e.formats.end()) at = e.formats.begin();
    if (!typing && e.formats.size() > 1) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_PageDown))
            at = at + 1 == e.formats.end() ? e.formats.begin() : at + 1;
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_PageUp))
            at = at == e.formats.begin() ? e.formats.end() - 1 : at - 1;
    }
    if (static_cast<int>(*at) != st.format) {
        st.format = static_cast<int>(*at);
        keepExportSettings();
    }
    for (std::size_t i = 0; i < e.formats.size(); ++i) {
        if (i > 0) ImGui::SameLine(0, px(6));
        const auto f = e.formats[i];
        if (formatChip(fmt::format("##format{}", i).c_str(), formatIcon(f), formatLabel(f),
                       static_cast<int>(f) == st.format) &&
            static_cast<int>(f) != st.format) {
            st.format = static_cast<int>(f);
            keepExportSettings();
        }
    }
    ImGui::Dummy(ImVec2(0, px(8)));
    const float avail     = ImGui::GetContentRegionAvail().x;
    const float preview_w = std::max(px(260), std::min(px(560), avail * 0.55f));
    ImGui::BeginGroup();
    drawExportPreview(preview_w);
    ImGui::EndGroup();
    ImGui::SameLine(0, px(20));
    ImGui::BeginGroup();
    ImGui::PushItemWidth(px(260));
    drawExportOptions();
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    if (!export_) return;  // closed by a gesture of the options
    ImGui::Dummy(ImVec2(0, px(6)));
    drawExportProblems();
    // Enter saves; with a PDF, it opens it to print (Ctrl+P too).
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    // --focus export…@print, @save:PATH: that gesture once the sheet shows, without the system's dialog.
    if (export_ && !export_action_.empty() && ImGui::GetFrameCount() % 30 == 0) {
        const auto action = std::exchange(export_action_, std::string());
        if (action == "print")
            exportPrint();
        else if (action.rfind("save:", 0) == 0)
            exportTo(fs::path(action.substr(5)));
    }
    if (export_ && !typing && !popup && !export_->job.valid()) {
        const bool pdf = st.format == static_cast<int>(ExportFormat::Pdf);
        if (enter && !ImGui::GetIO().KeyCtrl) pdf ? exportPrint() : exportSave();
        if (pdf && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P)) exportPrint();
    }
}

void App::drawExportPreview(float width) {
    auto& e           = *export_;
    auto& st          = exportSettings();
    const auto format = static_cast<ExportFormat>(st.format);
    auto* dl          = ImGui::GetWindowDrawList();
    const auto frame  = [&](const std::string& caption) { ImGui::TextDisabled("%s", caption.c_str()); };
    // A texture of an image, made again when it changes.
    const auto texture = [&](const std::string& of, const std::function<publish::Image()>& make) {
        if (e.texture_of == of && e.texture != ImTextureID_Invalid) return;
        const auto image = make();
        if (e.texture != ImTextureID_Invalid) export_old_textures_.push_back(e.texture);
        e.texture    = image.rgba.empty() ? ImTextureID_Invalid
                                          : render::createTexture(image.width, image.height, image.rgba.data());
        e.texture_w  = image.width;
        e.texture_h  = image.height;
        e.texture_of = of;
    };
    // A page of the document as its PDF will have it.
    const auto pages = [&] {
        const auto key =
            fmt::format("{}|{}|{}|{}|{}|{}|{}|{}|{}", static_cast<int>(e.subject), st.paper, st.columns, st.diagrams,
                        st.pdf_assistant, st.pdf_evals, st.pdf_headers, e.repertoire, appearance::current().pieces);
        if (key != e.pages_of) {
            e.pages    = publish::layOut(exportDocument());
            e.pages_of = key;
            e.page     = std::clamp(e.page, 0, std::max(0, static_cast<int>(e.pages.size()) - 1));
        }
    };
    const auto drawPage = [&](const publish::Page& page, float w) {
        const float scale = w / page.width;
        const ImVec2 o    = ImGui::GetCursorScreenPos();
        const ImVec2 end(o.x + page.width * scale, o.y + page.height * scale);
        dl->AddRectFilled(ImVec2(o.x + px(3), o.y + px(5)), ImVec2(end.x + px(3), end.y + px(5)),
                          IM_COL32(20, 24, 31, 40), px(3));
        dl->AddRectFilled(o, end, IM_COL32_WHITE);
        dl->AddRect(o, end, IM_COL32(201, 206, 214, 255));
        for (const auto& r : page.rects)
            dl->AddRectFilled(ImVec2(o.x + r.x * scale, o.y + r.y * scale),
                              ImVec2(o.x + (r.x + r.w) * scale, o.y + (r.y + r.h) * scale), imColour(r.fill));
        for (const auto& l : page.lines)
            dl->AddLine(ImVec2(o.x + l.x0 * scale, o.y + l.y0 * scale), ImVec2(o.x + l.x1 * scale, o.y + l.y1 * scale),
                        imColour(l.colour), std::max(1.0f, l.width * scale));
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        for (const auto& d : page.diagrams) {
            ImGui::SetCursorScreenPos(ImVec2(o.x + d.x * scale, o.y + d.y * scale));
            BoardDecor decor;
            decor.eval_white  = -1;
            decor.flipped     = d.spec.flipped;
            decor.last_move   = d.spec.last_move;
            decor.coordinates = d.spec.coordinates;
            decor.colours     = d.spec.colours;
            for (const auto& q : d.spec.squares)
                if (q.from >= 0) decor.tints.push_back({chess::Square(q.from), markColour(q.colour, true)});
            for (const auto& a : d.spec.arrows)
                if (a.from >= 0 && a.to >= 0)
                    decor.arrows.push_back(
                        {chess::Move::make<chess::Move::NORMAL>(chess::Square(a.from), chess::Square(a.to)),
                         markColour(a.colour, false)});
            drawBoard(atlas_, d.spec.pieces, d.board * scale, decor, d.spec.check);
        }
        ImGui::SetCursorScreenPos(cursor);
        const auto& font = publish::Fonts::get().font(publish::FontId::Regular);
        const float line = font.ascent() - font.descent();
        for (const auto& t : page.texts) {
            const float size = t.size * scale * line;
            dl->AddText(pageFont(t.font), size, ImVec2(o.x + t.x * scale, o.y + (t.y - font.ascent() * t.size) * scale),
                        imColour(t.colour), t.text.c_str());
        }
        ImGui::Dummy(ImVec2(page.width * scale, page.height * scale + px(6)));
    };
    switch (format) {
        case ExportFormat::Html: {
            if (e.document) {
                frame(trf("Preview · {}: its content, as the page lays it out", exportFileName(format)));
                pages();
                if (!e.pages.empty()) drawPage(e.pages.front(), std::min(width, px(380)));
                break;
            }
            frame(trf("Preview · {}, as a browser opens it", exportFileName(format)));
            ui::beginCard("export_html_preview", ImVec2(width, 0));
            const auto line  = lineOf(e.game.tree, e.game.line);
            const int ply    = std::clamp(e.game.ply, 0, static_cast<int>(line.moves.size()));
            const float side = std::min(px(210), width * 0.42f);
            ImGui::BeginGroup();
            auto spec = publish::diagramAt(line, e.game.chess960, ply, &e.game.start_notes, appearance::boardColours());
            BoardDecor decor;
            decor.eval_white = -1;
            decor.last_move  = spec.last_move;
            if (st.html_marks) {
                for (const auto& q : spec.squares)
                    if (q.from >= 0) decor.tints.push_back({chess::Square(q.from), markColour(q.colour, true)});
                for (const auto& a : spec.arrows)
                    if (a.from >= 0 && a.to >= 0)
                        decor.arrows.push_back(
                            {chess::Move::make<chess::Move::NORMAL>(chess::Square(a.from), chess::Square(a.to)),
                             markColour(a.colour, false)});
            }
            drawBoard(atlas_, spec.pieces, side, decor, spec.check);
            ImGui::TextDisabled("%s", "  |‹    ‹    ›    ›|    ⇅");
            ImGui::EndGroup();
            ImGui::SameLine(0, px(12));
            ImGui::BeginGroup();
            ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize());
            ImGui::TextUnformatted(publish::gameTitle(e.game).c_str());
            ImGui::PopFont();
            const auto sub = publish::gameSubtitle(e.game);
            if (!sub.empty()) {
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - side - px(40));
                ImGui::TextDisabled("%s", sub.c_str());
                ImGui::PopTextWrapPos();
            }
            // Its moves, around the position it opens at.
            publish::PrintGame g;
            g.tree     = e.game.tree;
            g.chess960 = e.game.chess960;
            if (!st.html_comments)
                for (auto& m : g.tree.main.moves) m.notes.after.clear();
            if (!st.html_variations) g.tree.variations.clear();
            publish::GamePrintOptions o;
            o.diagrams     = publish::DiagramsAt::None;
            o.headers      = false;
            o.assistant    = st.html_assistant;
            o.evals        = st.html_evals;
            const auto doc = publish::gameDocument(g, o, {});
            std::vector<publish::Span> spans;
            for (const auto& b : doc.blocks) spans.insert(spans.end(), b.spans.begin(), b.spans.end());
            int node = -1, seen = 0;
            for (auto& s : spans)
                if (s.font == publish::FontId::SemiBold && s.cls.empty() && ++seen == ply) node = seen;
            seen = 0;
            for (auto& s : spans)
                if (s.font == publish::FontId::SemiBold && s.cls.empty()) s.node = ++seen;
            const float text_w = std::max(px(120), width - side - px(40));
            const int row      = drawSpans(spans, text_w, 0, 0, node);
            (void)drawSpans(spans, text_w, std::max(0, row - 2), 9, node);
            ImGui::EndGroup();
            ui::endCard();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
            ImGui::TextDisabled("%s",
                                TR("Variations follow a click; the arrows and squares show at their move; ← and → "
                                   "on the keyboard."));
            ImGui::PopTextWrapPos();
            break;
        }
        case ExportFormat::Diagram: {
            const auto spec = exportDiagramSpec();
            std::string of  = fmt::format("diagram|{}|{}|{}|{}|{}|{}|{}|{}", spec.caption, spec.coordinates,
                                          spec.side_dot, spec.black_and_white, spec.flipped, spec.last_move.has_value(),
                                          spec.arrows.size(), appearance::current().pieces);
            for (const auto& p : spec.pieces) of += static_cast<char>('a' + static_cast<int>(p.internal()));
            texture(of, [&] { return publish::diagramImage(spec, *exportPieces(), 480); });
            frame(trf("Preview · {}", exportFileName(format)));
            if (e.texture != ImTextureID_Invalid) {
                const float w = std::min(width, px(360));
                const float h = w * static_cast<float>(e.texture_h) / static_cast<float>(std::max(1, e.texture_w));
                ImGui::Image(ImTextureRef(e.texture), ImVec2(w, h));
            }
            break;
        }
        case ExportFormat::Gif: {
            publish::GifGame g;
            g.game        = lineOf(e.game.tree, e.game.line);
            g.chess960    = e.game.chess960;
            g.start_notes = e.game.start_notes;
            g.banner =
                trf("{} – {} · {}", e.game.white.empty() ? g.game.header("White") : e.game.white,
                    e.game.black.empty() ? g.game.header("Black") : e.game.black, publish::resultText(g.game.result));
            g.colours             = appearance::boardColours();
            g.pieces              = exportPieces();
            const auto options    = exportGifOptions();
            const auto [from, to] = publish::gifRange(g, options);
            const int ply         = std::clamp(e.game.ply, from, to);
            const auto of = fmt::format("gif|{}|{}|{}|{}|{}|{}|{}|{}", ply, options.size, options.banner,
                                        options.last_move, options.arrows, from, to, appearance::current().pieces);
            texture(of, [&] { return publish::gifFrame(g, options, ply); });
            frame(trf("Preview · {}", exportFileName(format)));
            if (e.texture != ImTextureID_Invalid) {
                const float w = std::min(width, px(300));
                const float h = w * static_cast<float>(e.texture_h) / static_cast<float>(std::max(1, e.texture_w));
                ImGui::Image(ImTextureRef(e.texture), ImVec2(w, h));
            }
            std::string move = TR("the start");
            if (ply > 0) {
                const auto before = boardAt(g.game, g.chess960, ply - 1);
                const auto& m     = g.game.moves[static_cast<std::size_t>(ply - 1)];
                move = fmt::format("{}{}{}", before.fullMoveNumber(), m.mover == chess::Color::WHITE ? "." : "…",
                                   localSan(m.san));
            }
            ImGui::TextDisabled("%s", trf("{} · frame {} of {}", move, ply - from + 1, to - from + 1).c_str());
            // Its weight, estimated in the background from its first frames.
            e.gif_key = of.substr(of.find('|', 4));
            if (e.estimate_of != e.gif_key && !e.estimate_job.valid()) {
                e.estimating = e.gif_key;
                e.estimate_job =
                    std::async(std::launch::async, [g, options] { return publish::gifEstimate(g, options); });
            }
            break;
        }
        case ExportFormat::Pdf: {
            pages();
            frame(trf("Preview · {}, the exact page", exportFileName(format)));
            if (!e.pages.empty()) {
                drawPage(e.pages[static_cast<std::size_t>(e.page)], std::min(width, px(380)));
                if (e.pages.size() > 1) {
                    if (ImGui::SmallButton("‹##page_prev") && e.page > 0) --e.page;
                    ImGui::SameLine();
                    ImGui::TextDisabled("%d / %d", e.page + 1, static_cast<int>(e.pages.size()));
                    ImGui::SameLine();
                    if (ImGui::SmallButton("›##page_next") && e.page + 1 < static_cast<int>(e.pages.size())) ++e.page;
                }
            }
            break;
        }
        case ExportFormat::Pgn: {
            if (!e.snapshot) break;
            const auto of = fmt::format("{}|{}", e.chosen, st.write_names);
            if (of != e.text_of) {
                const auto pgn = study::exportedPgn(*e.snapshot, e.chosen, st.write_names);
                std::string lines;
                int n = 0;
                for (std::size_t at = 0; at < pgn.size() && n < 22; ++n) {
                    auto end = pgn.find('\n', at);
                    if (end == std::string::npos) end = pgn.size();
                    lines += pgn.substr(at, end - at) + "\n";
                    at = end + 1;
                }
                e.text_preview = lines;
                e.text_of      = of;
            }
            frame(trf("Preview · {}: its first game", exportFileName(format)));
            ui::beginCard("export_pgn_preview", ImVec2(width, 0));
            ImGui::PushFont(ui::monoFont(), ImGui::GetFontSize() * 0.9f);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - px(24));
            ImGui::TextUnformatted(e.text_preview.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopFont();
            ui::endCard();
            break;
        }
    }
}

void App::drawExportOptions() {
    auto& e           = *export_;
    auto& st          = exportSettings();
    const auto format = static_cast<ExportFormat>(st.format);
    const auto keep   = [&](bool changed) {
        if (changed) keepExportSettings();
    };
    const auto note = [&](const std::string& text) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + px(360));
        ImGui::TextDisabled("%s", text.c_str());
        ImGui::PopTextWrapPos();
    };
    const auto progress = [&] {
        if (!e.job.valid() || !e.done || !e.total) return;
        const int done = e.done->load(), total = std::max(1, e.total->load());
        ImGui::TextDisabled("%s",
                            (format == ExportFormat::Gif ? trf("Frame {} of {}", done, total)
                                                         : trf("{} of {}", ui::exactCount(static_cast<uint64_t>(done)),
                                                               ui::exactCount(static_cast<uint64_t>(total))))
                                .c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Cancel")) && e.cancel) *e.cancel = true;
        ImGui::ProgressBar(static_cast<float>(done) / static_cast<float>(total), ImVec2(px(260), px(6)), "");
    };
    const auto save = [&](const char* label) {
        ImGui::BeginDisabled(e.job.valid());
        if (primaryButton(label)) exportSave();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("Enter"));
    };
    const bool game = e.subject == ExportSheet::Subject::Board || e.subject == ExportSheet::Subject::Database;
    switch (format) {
        case ExportFormat::Html: {
            if (e.document) {
                ImGui::TextUnformatted(TR("A page of its own"));
                note(TR(
                    "One file with everything in it, its lines replayable on a board: it opens without the Internet, "
                    "on a phone too."));
                save(TR("Save…"));
                break;
            }
            ImGui::TextUnformatted(TR("What it holds"));
            if (e.snapshot && e.ids.size() > 1) {
                int all = e.all ? 1 : 0;
                if (choice("html_what", all,
                           {TR("This game"), trn("The {} game of the filter", "The {} games of the filter",
                                                 e.ids.size(), ui::exactCount(e.ids.size()))}))
                    e.all = all == 1;
            }
            keep(ImGui::Checkbox(TR("The comments"), &st.html_comments));
            keep(ImGui::Checkbox(TR("The variations"), &st.html_variations));
            keep(ImGui::Checkbox(TR("The arrows and squares"), &st.html_marks));
            keep(ImGui::Checkbox(TR("The assistant's comments, marked"), &st.html_assistant));
            keep(ImGui::Checkbox(TR("The evaluations and their curve"), &st.html_evals));
            keep(ImGui::Checkbox(TR("The clocks"), &st.html_clocks));
            // Its weight, estimated from a few of its games.
            const std::size_t count = e.all ? e.ids.size() : 1;
            const auto of           = fmt::format("html|{}|{}{}{}{}{}{}", count, st.html_comments, st.html_variations,
                                                  st.html_marks, st.html_assistant, st.html_evals, st.html_clocks);
            if (e.estimate_of != of) {
                publish::HtmlOptions o;
                o.comments   = st.html_comments;
                o.variations = st.html_variations;
                o.marks      = st.html_marks;
                o.assistant  = st.html_assistant;
                o.evals      = st.html_evals;
                o.clocks     = st.html_clocks;
                publish::HtmlLook l;
                l.pieces   = exportPieces();
                auto games = e.all ? exportGames(5) : std::vector<publish::HtmlGame>{e.game};
                e.estimate = publish::estimateHtml(games, std::min(count, publish::MAX_GAMES_PER_PAGE), o, l);
                if (count > publish::MAX_GAMES_PER_PAGE) e.estimate = e.estimate * count / publish::MAX_GAMES_PER_PAGE;
                e.estimate_of = of;
            }
            note(
                trf("One file with everything in it: it opens without the Internet, on a phone too. About {} "
                    "(estimated). The board's colours and pieces are the app's.",
                    fileWeight(e.estimate)));
            if (e.all && e.ids.size() > publish::MAX_GAMES_PER_PAGE) {
                problemCard(
                    "export_big", ui::Icon::Info, false,
                    trf("A page of {} games would be {}", ui::exactCount(e.ids.size()), fileWeight(e.estimate)),
                    trf("Beyond {} games, a page per game in a folder, with their list.", publish::MAX_GAMES_PER_PAGE));
                int split = e.split - 1;
                if (choice("html_split", split,
                           {TR("A page per game"), trf("The first {}", publish::MAX_GAMES_PER_PAGE)}))
                    e.split = split + 1;
                endProblemCard();
            }
            progress();
            ImGui::BeginDisabled(e.all && e.ids.size() > publish::MAX_GAMES_PER_PAGE && e.split == 0);
            save(TR("Save…"));
            ImGui::EndDisabled();
            break;
        }
        case ExportFormat::Diagram: {
            int svg = st.svg ? 1 : 0;
            if (choice("diagram_file", svg, {"PNG", "SVG"})) {
                st.svg = svg == 1;
                keepExportSettings();
            }
            if (st.svg) {
                ImGui::TextDisabled("%s", TR("An SVG has no size: it is drawn at any size."));
            } else {
                ImGui::TextUnformatted(trf("Size: {} px", st.dense ? 960 : 480).c_str());
                keep(ImGui::Checkbox(TR("Twice as large, for a dense screen"), &st.dense));
            }
            keep(ImGui::Checkbox(TR("The coordinates"), &st.coordinates));
            keep(ImGui::Checkbox(TR("The arrows and squares of this move"), &st.diagram_marks));
            keep(ImGui::Checkbox(TR("The last move marked"), &st.last_move));
            keep(ImGui::Checkbox(TR("The side to move, a dot beside its edge"), &st.side_dot));
            keep(ImGui::Checkbox(TR("Black below when it is Black's move"), &st.black_below));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(TR("Caption:"));
            ImGui::SameLine();
            ImGui::InputTextWithHint("##caption", TR("White to play and win"), e.caption.data(), e.caption.size());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(TR("Colours:"));
            ImGui::SameLine();
            int grey = st.grey ? 1 : 0;
            if (choice("diagram_theme", grey, {TR("the app's"), TR("black and white, for printing")})) {
                st.grey = grey == 1;
                keepExportSettings();
            }
            if (primaryButton(TR("Copy the image"))) copyDiagram();
            ImGui::SameLine();
            ImGui::TextDisabled("%s", TR("Ctrl+Shift+C, from anywhere on the Board page"));
            if (ImGui::Button(TR("Save…"))) exportSave();
            ImGui::SameLine();
            ImGui::TextDisabled("%s", TR("Enter"));
            note(
                TR("The arrows and squares are those you drew at this move (the right button); the caption starts "
                   "from the side to move."));
            break;
        }
        case ExportFormat::Gif: {
            const auto line  = lineOf(e.game.tree, e.game.line);
            const int count  = static_cast<int>(line.moves.size());
            const auto label = [&](int ply) -> std::string {
                if (ply <= 0) return TR("the start");
                const auto before = boardAt(line, e.game.chess960, ply - 1);
                const auto& m     = line.moves[static_cast<std::size_t>(ply - 1)];
                return fmt::format("{}{}{}", before.fullMoveNumber(), m.mover == chess::Color::WHITE ? "." : "…",
                                   localSan(m.san));
            };
            const int to    = e.gif_to < 0 ? count : std::min(e.gif_to, count);
            const int from  = std::clamp(e.gif_from, 0, to);
            const auto pick = [&](const char* id, int& value, int lo, int hi) {
                ImGui::SetNextItemWidth(px(110));
                if (e.focus_range && std::string_view(id) == "##gif_from") {
                    ImGui::SetKeyboardFocusHere();
                    e.focus_range = false;
                }
                if (ImGui::BeginCombo(id, label(value).c_str())) {
                    for (int p = lo; p <= hi; ++p) {
                        const bool on = p == value;
                        if (ImGui::Selectable(fmt::format("{}##{}", label(p), p).c_str(), on)) value = p;
                        if (on) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            };
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(TR("From"));
            ImGui::SameLine();
            int f = from, t = to;
            pick("##gif_from", f, 0, to);
            ImGui::SameLine();
            ImGui::TextUnformatted(TR("to"));
            ImGui::SameLine();
            pick("##gif_to", t, from, count);
            e.gif_from = f;
            e.gif_to   = t == count ? -1 : t;
            ImGui::SameLine();
            ImGui::TextDisabled("%s", trn("({} ply)", "({} plies)", std::max(0, t - f), std::max(0, t - f)).c_str());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(TR("A move:"));
            ImGui::SameLine();
            int speed = st.gif_delay <= 500 ? 0 : st.gif_delay <= 1000 ? 1 : 2;
            if (choice("gif_speed", speed, {trf("{} s", "½"), trf("{} s", 1), trf("{} s", 2)})) {
                st.gif_delay = speed == 0 ? 500 : speed == 1 ? 1000 : 2000;
                keepExportSettings();
            }
            ImGui::SameLine(0, px(12));
            ImGui::TextUnformatted(TR("the last:"));
            ImGui::SameLine();
            int last = st.gif_last <= 1000 ? 0 : st.gif_last <= 3000 ? 1 : 2;
            if (choice("gif_last", last, {trf("{} s", 1), trf("{} s", 3), trf("{} s", 5)})) {
                st.gif_last = last == 0 ? 1000 : last == 1 ? 3000 : 5000;
                keepExportSettings();
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(TR("Size:"));
            ImGui::SameLine();
            int size = st.gif_size <= 240 ? 0 : st.gif_size <= 360 ? 1 : 2;
            if (choice("gif_size", size, {"240 px", "360 px", "480 px"})) {
                st.gif_size = size == 0 ? 240 : size == 1 ? 360 : 480;
                keepExportSettings();
            }
            ImGui::SameLine(0, px(10));
            const bool estimated = e.estimate > 0 && e.estimate_of == e.gif_key;
            if (estimated)
                ImGui::TextDisabled("%s", trf("about {} (estimated)", fileWeight(e.estimate)).c_str());
            else
                ImGui::TextDisabled("%s", TR("estimating…"));
            keep(ImGui::Checkbox(TR("The names and the result in a banner"), &st.gif_banner));
            keep(ImGui::Checkbox(TR("The last move marked"), &st.gif_last_move));
            keep(ImGui::Checkbox(TR("The game's arrows and squares"), &st.gif_arrows));
            // Heavy for a message: a passage, a smaller size.
            if (estimated && e.estimate > 2'000'000) {
                problemCard("export_heavy", ui::Icon::Film, false,
                            trn("A GIF of {} ply: {}", "A GIF of {} plies: {}", std::max(0, t - f), std::max(0, t - f),
                                fileWeight(e.estimate)),
                            TR("Heavy for a message or a forum."));
                if (ImGui::SmallButton(TR("Only from … to …"))) e.focus_range = true;
                ImGui::SameLine();
                if (st.gif_size > 240 && ImGui::SmallButton("240 px")) {
                    st.gif_size = 240;
                    keepExportSettings();
                }
                endProblemCard();
            }
            progress();
            save(TR("Save…"));
            note(
                TR("Made here, in the app's theme, in the background: nothing is sent anywhere (Lichess makes GIFs "
                   "too, through an address of its site outside its API, and the game would leave)."));
            break;
        }
        case ExportFormat::Pdf: {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(TR("Paper:"));
            ImGui::SameLine();
            int paper = st.paper < 0 ? (publish::systemLetter() ? 1 : 0) : st.paper;
            if (choice("pdf_paper", paper, {"A4", "Letter"})) {
                st.paper = paper;
                keepExportSettings();
            }
            if (st.paper < 0) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", TR("(by the system's language)"));
            }
            if (game) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(TR("Columns:"));
                ImGui::SameLine();
                int columns = st.columns == 1 ? 0 : 1;
                if (choice("pdf_columns", columns, {"1", "2"})) {
                    st.columns = columns + 1;
                    keepExportSettings();
                }
                ImGui::TextUnformatted(TR("Diagrams:"));
                publish::PrintGame g;
                g.tree         = e.game.tree;
                g.key_plies    = e.key_plies;
                const auto key = publish::diagramPlies(g, publish::DiagramsAt::KeyMoments).size();
                int at         = std::clamp(st.diagrams, 0, 3);
                if (choice("pdf_diagrams", at,
                           {trf("at the key moments ({})", key), TR("at each marked comment"), TR("every 10 moves"),
                            TR("none")})) {
                    st.diagrams = at;
                    keepExportSettings();
                }
                keep(ImGui::Checkbox(TR("The assistant's comments, marked"), &st.pdf_assistant));
                keep(ImGui::Checkbox(TR("The evaluations, beside the moves"), &st.pdf_evals));
                keep(ImGui::Checkbox(TR("The headers at the top of the page"), &st.pdf_headers));
            } else if (e.subject == ExportSheet::Subject::Crosstable) {
                ImGui::TextDisabled("%s", TR("Landscape, every player listed."));
            }
            if (primaryButton(TR("Open to print"))) exportPrint();
            ImGui::SameLine();
            ImGui::TextDisabled("%s", TR("Enter · Ctrl+P"));
            ImGui::SameLine();
            if (ImGui::Button(TR("Save the PDF…"))) exportSave();
            note(
                TR("A PDF made by RookStudio, the same on the three systems: Open to print gives it to the system's "
                   "reader, which prints. The preview is the exact page."));
            break;
        }
        case ExportFormat::Pgn: {
            ImGui::TextUnformatted(TR("What it holds"));
            if (e.ids.size() > 1) {
                int all = e.all ? 1 : 0;
                if (choice("pgn_what", all,
                           {TR("This game"), trn("The {} game of the filter", "The {} games of the filter",
                                                 e.ids.size(), ui::exactCount(e.ids.size()))}))
                    e.all = all == 1;
            }
            keep(ImGui::Checkbox(TR("Write the names in the games"), &st.write_names));
            note(TR(
                "Off, the games as they were read. On, White and Black as the table of names writes them, with their "
                "FIDE ids and titles when it knows them."));
            progress();
            save(TR("Save…"));
            break;
        }
    }
    // What happened last.
    if (!export_) return;
    if (!e.error.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + px(360));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", e.error.c_str());
        ImGui::PopTextWrapPos();
    } else if (!e.said.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + px(360));
        ImGui::TextDisabled("%s", e.said.c_str());
        ImGui::PopTextWrapPos();
        if (!e.saved.empty() && !e.job.valid()) {
            if (ImGui::SmallButton(TR("Open##export_open"))) ui::openPath(e.saved);
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Its folder"))) ui::openPath(e.saved.parent_path());
        }
    }
}

void App::drawExportProblems() {
    auto& e = *export_;
    switch (e.problem) {
        case ExportSheet::Problem::None:
            break;
        case ExportSheet::Problem::Folder:
            problemCard("export_folder", ui::Icon::Alert, true, TR("The folder chosen does not take files"),
                        trf("“{}” cannot be written: it is read-only, or full.", shortPath(e.problem_path)));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", pathText(e.problem_path).c_str());
            if (ImGui::SmallButton(TR("Choose another folder"))) exportSave();
            endProblemCard();
            break;
        case ExportSheet::Problem::Empty:
            problemCard("export_empty", ui::Icon::Book, false, TR("The repertoire is empty"),
                        TR("Nothing to print: star moves in the Board page's Tree tab."));
            endProblemCard();
            break;
        case ExportSheet::Problem::NoReader:
            problemCard("export_reader", ui::Icon::Printer, false, TR("No PDF reader on this system"),
                        trf("The PDF is saved in {}; its folder opens.", shortPath(e.saved.parent_path())));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", pathText(e.saved).c_str());
            if (ImGui::SmallButton(TR("Open the folder"))) ui::openPath(e.saved.parent_path());
            endProblemCard();
            break;
    }
}

}  // namespace fcg
