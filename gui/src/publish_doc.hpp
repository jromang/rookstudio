#pragma once

// What RookStudio prints (#93, mockup M13, board 6, point 14): a document of blocks (titles, paragraphs of runs,
// tables, diagrams, bars), laid out on pages of A4 or Letter, in one or two columns, by its own layout: the pages a
// PDF is written from (publish_pdf.hpp) and the preview of the Export sheet draws, the same. The same document is also
// a page of HTML (the reports' "HTML page"), its lines replayable on a board when it has some.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "publish_draw.hpp"
#include "publish_html.hpp"
#include "publish_text.hpp"

namespace fcg::publish {

inline constexpr Rgba TEXT_COLOUR{31, 35, 41, 255};
inline constexpr Rgba MUTED_COLOUR{93, 101, 112, 255};
inline constexpr Rgba COMMENT_COLOUR{46, 125, 79, 255};
inline constexpr Rgba VARIATION_COLOUR{74, 81, 92, 255};
inline constexpr Rgba ASSISTANT_COLOUR{84, 104, 138, 255};
inline constexpr Rgba ACCENT_COLOUR{47, 111, 222, 255};

// A piece of text with its look; a move of a replayable line names its game and node there (the HTML page).
struct Span {
    Span() = default;
    Span(std::string t, FontId f = FontId::Regular, Rgba c = TEXT_COLOUR) : text(std::move(t)), font(f), colour(c) {}
    std::string text;
    FontId font = FontId::Regular;
    Rgba colour = TEXT_COLOUR;
    float scale = 1;  // of the block's size
    int game = -1, node = -1;
    std::string cls;  // its class in the HTML page ("mv", "cm", "as", "var", "num", "nag", "ev")
};

struct Cell {
    Cell() = default;
    Cell(std::string t, FontId f = FontId::Regular, Rgba c = TEXT_COLOUR) : text(std::move(t)), font(f), colour(c) {}
    std::string text;
    FontId font = FontId::Regular;
    Rgba colour = TEXT_COLOUR;
    std::optional<Rgba> fill;  // a background
    float bar = -1;            // 0 to 1: a bar of that length in place of the text's room before it
};

struct Table {
    std::vector<std::string> head;
    std::vector<int> align;  // by column: 0 left, 1 right, 2 centre
    std::vector<std::vector<Cell>> rows;
    float size = 8.5f;  // its text, in points
};

struct Block {
    enum class Kind { Title, Subtitle, Heading, Paragraph, Note, Table, Diagram, Bars, Rule, Space, PageBreak };
    Kind kind = Kind::Paragraph;
    std::vector<Span> spans;
    Table table;
    DiagramSpec diagram;
    std::string caption;
    float diagram_size = 0;  // the board, in points; 0: as the column allows (at most 180)
    struct Bar {
        std::string label, value;
        float length = 0;  // 0 to 1
    };
    std::vector<Bar> bars;
    float space         = 6;      // a Space's height
    bool keep_with_next = false;  // a heading: never alone at the bottom of a column
};

struct Document {
    std::string title;   // its name: the PDF's title, the HTML page's
    std::string header;  // a line at the top of each page after the first ("Moreau – Lindqvist · Autumn Open 2026")
    std::string lang = "en";
    bool letter      = false;  // Letter, else A4
    bool landscape   = false;
    int columns      = 1;
    float size       = 9.5f;  // the text, in points
    std::vector<Block> blocks;
    PiecesNotice notice;
    appearance::BoardColours colours;
    std::shared_ptr<const PieceShapes> pieces;
    // The lines a page of HTML replays: the moves' spans name them by their place here and their node.
    std::vector<HtmlGame> games;
};

// Helpers for building one.
Block title(std::string text);
Block subtitle(std::string text);
Block heading(std::string text);
Block paragraph(std::string text, Rgba colour = TEXT_COLOUR, FontId font = FontId::Regular);
Block note(std::string text);
Block space(float points);

// A laid-out page, in points from its top left corner, its text on baselines.
struct PageText {
    float x = 0, y = 0;
    FontId font = FontId::Regular;
    float size  = 10;
    Rgba colour = TEXT_COLOUR;
    std::string text;
};
struct PageRect {
    float x = 0, y = 0, w = 0, h = 0;
    Rgba fill;
};
struct PageLine {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0, width = 0.5f;
    Rgba colour = MUTED_COLOUR;
};
struct PageDiagram {
    float x = 0, y = 0, board = 0;  // its top left corner, its board's width
    DiagramSpec spec;
};
struct Page {
    float width = 0, height = 0;
    std::vector<PageRect> rects;
    std::vector<PageLine> lines;
    std::vector<PageDiagram> diagrams;
    std::vector<PageText> texts;
};

// The document on its pages; each page numbered at its foot, the pieces' notice and license on the last.
[[nodiscard]] std::vector<Page> layOut(const Document& document);

// A text as the PDF's fonts can write it: a character neither Inter nor the Chinese font has given another way.
[[nodiscard]] std::string printable(const std::string& text);

// The document as a page of HTML: its blocks in order, its tables, its diagrams drawn in SVG; with lines to replay, a
// board beside the text that shows a move clicked.
[[nodiscard]] std::string documentHtml(const Document& document);

// "A4" when the system's language is that of a country of A4 paper, else "Letter" (the United States, Canada,
// Mexico, the Philippines and a few others).
[[nodiscard]] bool systemLetter();

}  // namespace fcg::publish
