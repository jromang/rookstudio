#pragma once

// A PDF made by RookStudio (#93, mockup M13, board 6, point 14), the same on every system: the pages of a document as
// its layout placed them (publish_doc.hpp), their text in fonts embedded and cut down to the glyphs used (Inter, and
// the system's Chinese font when the text needs it), selectable and copied back as written (each font's ToUnicode
// map); the diagrams as vector shapes, the pieces as their SVGs draw them, gradients included. Nothing is sent.

#include <string>
#include <vector>

#include "publish_doc.hpp"

namespace fcg::publish {

struct PdfInfo {
    std::string title;
    std::string lang = "en";
    std::string date;  // "D:20261124103000", empty for none
};

[[nodiscard]] std::string pdfBytes(const std::vector<Page>& pages, const PieceShapes* pieces, const PdfInfo& info);

// The data of a stream, deflated as a PDF's /FlateDecode wants it.
[[nodiscard]] std::string deflate(const std::string& data);

}  // namespace fcg::publish
