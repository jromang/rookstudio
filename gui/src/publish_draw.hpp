#pragma once

// What RookStudio draws for what it publishes (#93, mockup M13, board 6): shapes (paths filled and stroked, gradients
// as nanosvg reads the pieces' SVGs), written as SVG, as a PDF's content (publish_pdf.hpp) or rasterized by nanosvg,
// the rasterizer of the board's own pieces. A board's diagram is made of the board's drawing (board_view.hpp): its
// squares in the theme's colours, the last move, the squares and arrows a person marked ([%csl], [%cal]) in the
// board's colours and with its arrows' shape, its coordinates, the pieces of the set in use; then the side to move and
// a caption, in Inter.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <chess.hpp>

#include <study/game.hpp>

#include "appearance.hpp"
#include "publish_fonts.hpp"

namespace fcg::publish {

struct Rgba {
    uint8_t r = 0, g = 0, b = 0, a = 255;
    bool operator==(const Rgba&) const = default;
};
[[nodiscard]] Rgba rgba(ImU32 colour);  // from ImGui's packing

struct GradientStop {
    float offset = 0;
    Rgba colour;
};
struct Gradient {
    bool radial = false;
    // From the gradient's space to the drawing's (a, b, c, d, e, f as SVG's matrix()): a linear gradient goes from
    // (0, 0) to (0, 1) there, a radial one is the circle of radius 1 around (0, 0).
    std::array<double, 6> matrix{1, 0, 0, 1, 0, 0};
    int spread = 0;  // 0 pad, 1 reflect, 2 repeat
    std::vector<GradientStop> stops;
};

struct Paint {
    enum class Kind : uint8_t { None, Solid, Gradient };
    Kind kind = Kind::None;
    Rgba colour;
    int gradient = -1;  // in the drawing's gradients
};

struct Shape {
    Path path;
    Paint fill, stroke;
    float stroke_width = 1;
    int join = 0, cap = 0;  // 0 miter, 1 round, 2 bevel; 0 butt, 1 round, 2 square
    float miter   = 4;
    bool even_odd = false;
    float opacity = 1;
    std::vector<float> dashes;
    float dash_offset = 0;
};

// Shapes in their order, in a box `width` x `height`, y downwards.
struct Drawing {
    float width = 0, height = 0;
    std::vector<Shape> shapes;
    std::vector<Gradient> gradients;

    void rect(float x, float y, float w, float h, Rgba colour);
    void polygon(const std::vector<std::array<float, 2>>& points, Rgba colour);
    void circle(float cx, float cy, float r, Rgba fill, std::optional<Rgba> stroke = std::nullopt, float width = 1);
    // Text filled as outlines, its baseline's start at (x, y); its width.
    float text(std::string_view utf8, FontId font, float em, float x, float y, Rgba colour);
    // Another drawing's shapes, scaled by `scale` and moved to (x, y); its gradients brought along.
    void place(const Drawing& other, float x, float y, float scale);
};

// The shapes as SVG elements (paths, and the gradients in a <defs> first), transformed by `scale` then (x, y);
// `ids` names its gradients ("g3" becomes "<ids>3") so that several drawings live in one document.
[[nodiscard]] std::string svgElements(const Drawing& drawing, const std::string& ids, float x = 0, float y = 0,
                                      float scale = 1);
// A whole SVG document: with `size`, its width and height in pixels; without, only its viewBox.
[[nodiscard]] std::string svgDocument(const std::string& elements, float width, float height, bool size,
                                      const std::string& title = {}, const std::string& comment = {});

// RGBA pixels, rows from the top, not premultiplied.
struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
    void fill(Rgba colour);
    // `over` drawn over this image where its alpha says.
    void blend(const Image& over, int x = 0, int y = 0);
};
// The shapes rasterized by nanosvg, `scale` pixels to a unit, over a transparent image of that size.
[[nodiscard]] Image rasterize(const Drawing& drawing, float scale = 1);
// PNG bytes, with a text chunk ("Comment") when given; and BMP's (for the Windows clipboard).
[[nodiscard]] std::string pngBytes(const Image& image, const std::string& comment = {});
[[nodiscard]] std::string bmpBytes(const Image& image);

// A set of pieces as shapes, each in a box `size` units wide, as nanosvg reads their SVGs: what the board's atlas is
// rasterized from.
struct PieceShapes {
    std::array<Drawing, 12> pieces;  // white K Q R B N P, then black K Q R B N P (appearance::PIECE_FILES)
    appearance::PieceSvgs svgs;      // their text, which the raster images come from
    std::size_t id = 0;              // a hash of that text
    [[nodiscard]] static std::shared_ptr<const PieceShapes> make(const appearance::PieceSvgs& svgs);
    [[nodiscard]] const Drawing& of(chess::Piece piece) const;
};
[[nodiscard]] int pieceIndex(chess::Piece piece);  // in PIECE_FILES' order

// What a board's diagram shows.
struct DiagramSpec {
    std::array<chess::Piece, 64> pieces{};  // a1 = 0 ... h8 = 63
    chess::Color side = chess::Color::WHITE;
    bool flipped      = false;
    std::optional<chess::Move> last_move;
    std::vector<BoardMark> arrows, squares;  // [%cal], [%csl]
    std::optional<chess::Square> check;
    bool coordinates = true;
    bool side_dot    = false;  // a disc of the side to move's colour beside its edge of the board
    std::string caption;
    bool black_and_white = false;  // for printing: white and grey squares, every colour as a grey
    appearance::BoardColours colours;
};
// The position after `ply` moves of `game` as a spec; the marks are those of that move (or of the start).
[[nodiscard]] DiagramSpec diagramAt(const ReplayGame& game, bool chess960, int ply, const MoveNotes* start_notes,
                                    const appearance::BoardColours& colours);

// The diagram's layers, in units of `board` the board's width: under the pieces (squares, marks, coordinates), the
// pieces where they stand, and over them (arrows, the side to move, the caption).
struct DiagramLayers {
    float width = 0, height = 0, board = 0, square = 0;
    Drawing under, over;
    struct Placed {
        chess::Piece piece;
        float x = 0, y = 0;
    };
    std::vector<Placed> pieces;
};
[[nodiscard]] DiagramLayers diagramLayers(const DiagramSpec& spec, float board);

// The diagram as SVG (its pieces as shapes, no size when `sized` is false), as PNG pixels (`board` pixels wide: its
// pieces from the same raster images as the board's atlas), and as one drawing (for a PDF).
[[nodiscard]] std::string diagramSvg(const DiagramSpec& spec, const PieceShapes& pieces, bool sized, float board = 480);
[[nodiscard]] Image diagramImage(const DiagramSpec& spec, const PieceShapes& pieces, int board);
[[nodiscard]] Drawing diagramDrawing(const DiagramSpec& spec, const PieceShapes& pieces, float board);

// The pieces rasterized for a square `pixels` wide, by the board's own rasterizePieces(), kept for the size last asked.
[[nodiscard]] std::shared_ptr<const std::vector<uint8_t>> pieceTiles(const PieceShapes& pieces, int pixels);

// The arrow of a mark as the board draws it (arrowPolygons), from the centre of one square to another's.
[[nodiscard]] std::vector<std::vector<std::array<float, 2>>> arrowShapes(float ax, float ay, float bx, float by,
                                                                         float square);

// The colours of a person's marks (markColour()), and the diagram's for printing: a colour as the grey of its light.
[[nodiscard]] Rgba grey(Rgba colour);

// A file's name from a text: letters, digits and dashes, lower case ("moreau-lindqvist-2026").
[[nodiscard]] std::string fileStem(const std::string& text);

}  // namespace fcg::publish
