#pragma once

// What the Export sheet keeps (#93, mockup M13, board 6): the format chosen last time and each format's settings, in
// export.json in the profile; and what the sheet needs around its files: a file's name, whether a folder takes files,
// the system's reader of PDF files.

#include <filesystem>
#include <functional>
#include <string>

#include <json.hpp>

namespace fcg {

// The sheet's formats, in the order its chips show them.
enum class ExportFormat : int { Html = 0, Diagram = 1, Gif = 2, Pdf = 3, Pgn = 4 };

struct ExportSettings {
    int format = 0;  // ExportFormat
    // The HTML page.
    bool html_comments = true, html_variations = true, html_marks = true, html_assistant = false, html_evals = false,
         html_clocks = false;
    // The diagram.
    bool svg = false, dense = false;  // dense: twice the size, for a dense screen
    bool coordinates = true, diagram_marks = true, last_move = false, side_dot = true, black_below = false;
    bool grey = false;  // black and white, for printing
    // The GIF.
    int gif_size = 360, gif_delay = 1000, gif_last = 3000;
    bool gif_banner = true, gif_last_move = true, gif_arrows = false;
    // The PDF.
    int paper          = -1;  // -1: by the system's language, 0 A4, 1 Letter
    int columns        = 2;
    int diagrams       = 0;  // publish::DiagramsAt
    bool pdf_assistant = false, pdf_evals = false, pdf_headers = true;
    // PGN, from the Databases page.
    bool write_names = false;
    std::string folder;  // where the last file went
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ExportSettings, format, html_comments, html_variations, html_marks,
                                                html_assistant, html_evals, html_clocks, svg, dense, coordinates,
                                                diagram_marks, last_move, side_dot, black_below, grey, gif_size,
                                                gif_delay, gif_last, gif_banner, gif_last_move, gif_arrows, paper,
                                                columns, diagrams, pdf_assistant, pdf_evals, pdf_headers, write_names,
                                                folder)

// export.json of a profile: the defaults when it has none, or one that cannot be read.
[[nodiscard]] ExportSettings loadExportSettings(const std::filesystem::path& profile);
void saveExportSettings(const std::filesystem::path& profile, const ExportSettings& settings);

// Whether files can be written in a folder: one made there and removed.
[[nodiscard]] bool folderWritable(const std::filesystem::path& folder);

// Whether the system has a reader for PDF files: on Linux, the one xdg-mime names; elsewhere, true (Windows and macOS
// have one, and opening the file says when not).
[[nodiscard]] bool systemHasPdfReader();
// The step that opens a file in the system's program for it: true when the system took it. The tests replace it.
using FileOpener = std::function<bool(const std::filesystem::path&)>;
[[nodiscard]] bool openInSystem(const std::filesystem::path& file);

// "120 KB", "2.6 MB": a file's weight as the sheet says it.
[[nodiscard]] std::string fileWeight(uint64_t bytes);

}  // namespace fcg
