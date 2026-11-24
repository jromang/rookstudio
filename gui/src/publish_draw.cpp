#include "publish_draw.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>

#include <nanosvg.h>
#include <nanosvgrast.h>
#include <stb_image_write.h>

#include <fmt/include/fmt/format.h>

#include <study/game_tree.hpp>

#include "board_view.hpp"

namespace fcg::publish {

Rgba rgba(ImU32 c) {
    return Rgba{static_cast<uint8_t>(c & 0xff), static_cast<uint8_t>(c >> 8 & 0xff),
                static_cast<uint8_t>(c >> 16 & 0xff), static_cast<uint8_t>(c >> 24 & 0xff)};
}

Rgba grey(Rgba c) {
    const auto l = static_cast<uint8_t>(std::lround(0.299 * c.r + 0.587 * c.g + 0.114 * c.b));
    return Rgba{l, l, l, c.a};
}

int pieceIndex(chess::Piece piece) {
    static constexpr int by_type[6] = {5, 4, 3, 2, 1, 0};  // PieceType's P N B R Q K into K Q R B N P
    const int type                  = std::clamp(static_cast<int>(piece.type()), 0, 5);
    return (piece.color() == chess::Color::WHITE ? 0 : 6) + by_type[type];
}

// ---------------------------------------------------------------------------------------------------------------------
// Drawings

void Drawing::rect(float x, float y, float w, float h, Rgba colour) {
    Shape s;
    s.path        = {{PathCmd::Move, {x}, {y}},
                     {PathCmd::Line, {x + w}, {y}},
                     {PathCmd::Line, {x + w}, {y + h}},
                     {PathCmd::Line, {x}, {y + h}},
                     {PathCmd::Close}};
    s.fill.kind   = Paint::Kind::Solid;
    s.fill.colour = colour;
    shapes.push_back(std::move(s));
}

void Drawing::polygon(const std::vector<std::array<float, 2>>& points, Rgba colour) {
    if (points.size() < 3) return;
    Shape s;
    for (std::size_t i = 0; i < points.size(); ++i)
        s.path.push_back({i == 0 ? PathCmd::Move : PathCmd::Line, {points[i][0]}, {points[i][1]}});
    s.path.push_back({PathCmd::Close});
    s.fill.kind   = Paint::Kind::Solid;
    s.fill.colour = colour;
    shapes.push_back(std::move(s));
}

void Drawing::circle(float cx, float cy, float r, Rgba fill, std::optional<Rgba> stroke, float width) {
    // Four cubic quarters.
    constexpr float k = 0.5522847f;
    Shape s;
    s.path        = {{PathCmd::Move, {cx + r}, {cy}},
                     {PathCmd::Cubic, {cx + r, cx + k * r, cx}, {cy + k * r, cy + r, cy + r}},
                     {PathCmd::Cubic, {cx - k * r, cx - r, cx - r}, {cy + r, cy + k * r, cy}},
                     {PathCmd::Cubic, {cx - r, cx - k * r, cx}, {cy - k * r, cy - r, cy - r}},
                     {PathCmd::Cubic, {cx + k * r, cx + r, cx + r}, {cy - r, cy - k * r, cy}},
                     {PathCmd::Close}};
    s.fill.kind   = Paint::Kind::Solid;
    s.fill.colour = fill;
    if (stroke) {
        s.stroke.kind   = Paint::Kind::Solid;
        s.stroke.colour = *stroke;
        s.stroke_width  = width;
    }
    shapes.push_back(std::move(s));
}

float Drawing::text(std::string_view utf8, FontId font, float em, float x, float y, Rgba colour) {
    Shape s;
    const float w = textOutline(utf8, font, em, x, y, s.path);
    if (s.path.empty()) return w;
    s.fill.kind   = Paint::Kind::Solid;
    s.fill.colour = colour;
    shapes.push_back(std::move(s));
    return w;
}

void Drawing::place(const Drawing& other, float x, float y, float scale) {
    const int base = static_cast<int>(gradients.size());
    for (auto g : other.gradients) {
        auto& m = g.matrix;
        m       = {m[0] * scale, m[1] * scale, m[2] * scale, m[3] * scale, m[4] * scale + x, m[5] * scale + y};
        gradients.push_back(std::move(g));
    }
    for (auto s : other.shapes) {
        for (auto& c : s.path)
            for (int i = 0; i < 3; ++i) {
                c.x[i] = c.x[i] * scale + x;
                c.y[i] = c.y[i] * scale + y;
            }
        s.stroke_width *= scale;
        for (auto& d : s.dashes) d *= scale;
        s.dash_offset *= scale;
        if (s.fill.gradient >= 0) s.fill.gradient += base;
        if (s.stroke.gradient >= 0) s.stroke.gradient += base;
        shapes.push_back(std::move(s));
    }
}

namespace {

// A number for SVG: two decimals at most, no trailing zero.
std::string num(double v) {
    auto s = fmt::format("{:.2f}", v);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}

std::string hex(Rgba c) { return fmt::format("#{:02x}{:02x}{:02x}", c.r, c.g, c.b); }

std::string pathData(const Path& path, float x, float y, float scale) {
    std::string d;
    const auto pt = [&](float px, float py) { return num(px * scale + x) + " " + num(py * scale + y); };
    for (const auto& c : path) {
        switch (c.op) {
            case PathCmd::Move:
                d += "M" + pt(c.x[0], c.y[0]);
                break;
            case PathCmd::Line:
                d += "L" + pt(c.x[0], c.y[0]);
                break;
            case PathCmd::Cubic:
                d += "C" + pt(c.x[0], c.y[0]) + " " + pt(c.x[1], c.y[1]) + " " + pt(c.x[2], c.y[2]);
                break;
            case PathCmd::Close:
                d += "Z";
                break;
        }
    }
    return d;
}

}  // namespace

std::string svgElements(const Drawing& drawing, const std::string& ids, float x, float y, float scale) {
    std::string out;
    if (!drawing.gradients.empty()) {
        out += "<defs>";
        for (std::size_t i = 0; i < drawing.gradients.size(); ++i) {
            const auto& g = drawing.gradients[i];
            const auto& m = g.matrix;
            const auto matrix =
                fmt::format("matrix({} {} {} {} {} {})", fmt::format("{:.6g}", m[0] * scale),
                            fmt::format("{:.6g}", m[1] * scale), fmt::format("{:.6g}", m[2] * scale),
                            fmt::format("{:.6g}", m[3] * scale), num(m[4] * scale + x), num(m[5] * scale + y));
            const char* spread = g.spread == 1   ? " spreadMethod=\"reflect\""
                                 : g.spread == 2 ? " spreadMethod=\"repeat\""
                                                 : "";
            out += g.radial ? fmt::format(
                                  "<radialGradient id=\"{}{}\" gradientUnits=\"userSpaceOnUse\" cx=\"0\" cy=\"0\" "
                                  "r=\"1\" gradientTransform=\"{}\"{}>",
                                  ids, i, matrix, spread)
                            : fmt::format(
                                  "<linearGradient id=\"{}{}\" gradientUnits=\"userSpaceOnUse\" x1=\"0\" y1=\"0\" "
                                  "x2=\"0\" y2=\"1\" gradientTransform=\"{}\"{}>",
                                  ids, i, matrix, spread);
            for (const auto& s : g.stops) {
                out += fmt::format("<stop offset=\"{}\" stop-color=\"{}\"", num(s.offset), hex(s.colour));
                if (s.colour.a != 255) out += fmt::format(" stop-opacity=\"{}\"", num(s.colour.a / 255.0));
                out += "/>";
            }
            out += g.radial ? "</radialGradient>" : "</linearGradient>";
        }
        out += "</defs>";
    }
    const auto paint = [&](const Paint& p, const char* name) {
        std::string a;
        if (p.kind == Paint::Kind::None) return fmt::format(" {}=\"none\"", name);
        if (p.kind == Paint::Kind::Gradient) return fmt::format(" {}=\"url(#{}{})\"", name, ids, p.gradient);
        a = fmt::format(" {}=\"{}\"", name, hex(p.colour));
        if (p.colour.a != 255) a += fmt::format(" {}-opacity=\"{}\"", name, num(p.colour.a / 255.0));
        return a;
    };
    for (const auto& s : drawing.shapes) {
        if (s.path.empty()) continue;
        out += "<path d=\"" + pathData(s.path, x, y, scale) + "\"";
        out += paint(s.fill, "fill");
        if (s.even_odd) out += " fill-rule=\"evenodd\"";
        if (s.stroke.kind != Paint::Kind::None) {
            out += paint(s.stroke, "stroke");
            out += fmt::format(" stroke-width=\"{}\"", num(s.stroke_width * scale));
            if (s.join == 1) out += " stroke-linejoin=\"round\"";
            if (s.join == 2) out += " stroke-linejoin=\"bevel\"";
            if (s.cap == 1) out += " stroke-linecap=\"round\"";
            if (s.cap == 2) out += " stroke-linecap=\"square\"";
            if (s.join == 0 && s.miter != 4) out += fmt::format(" stroke-miterlimit=\"{}\"", num(s.miter));
            if (!s.dashes.empty()) {
                std::string dashes;
                for (const auto d : s.dashes) dashes += (dashes.empty() ? "" : " ") + num(d * scale);
                out += " stroke-dasharray=\"" + dashes + "\"";
                if (s.dash_offset != 0) out += fmt::format(" stroke-dashoffset=\"{}\"", num(s.dash_offset * scale));
            }
        }
        if (s.opacity < 1) out += fmt::format(" opacity=\"{}\"", num(s.opacity));
        out += "/>";
    }
    return out;
}

namespace {

std::string xmlText(const std::string& s) {
    std::string out;
    for (const char c : s) {
        if (c == '&')
            out += "&amp;";
        else if (c == '<')
            out += "&lt;";
        else if (c == '>')
            out += "&gt;";
        else if (c == '"')
            out += "&quot;";
        else
            out += c;
    }
    return out;
}

}  // namespace

std::string svgDocument(const std::string& elements, float width, float height, bool size, const std::string& title,
                        const std::string& comment) {
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    if (!comment.empty()) {
        std::string c = comment;
        for (std::size_t at; (at = c.find("--")) != std::string::npos;) c.replace(at, 2, "- -");
        out += "<!--\n" + c + "\n-->\n";
    }
    out += fmt::format("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 {} {}\"", num(width), num(height));
    if (size) out += fmt::format(" width=\"{}\" height=\"{}\"", num(width), num(height));
    out += ">";
    if (!title.empty()) out += "<title>" + xmlText(title) + "</title>";
    out += elements;
    out += "</svg>\n";
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// Images

void Image::fill(Rgba c) {
    rgba.resize(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i]     = c.r;
        rgba[i + 1] = c.g;
        rgba[i + 2] = c.b;
        rgba[i + 3] = c.a;
    }
}

void Image::blend(const Image& over, int x, int y) {
    for (int oy = 0; oy < over.height; ++oy) {
        const int ty = y + oy;
        if (ty < 0 || ty >= height) continue;
        for (int ox = 0; ox < over.width; ++ox) {
            const int tx = x + ox;
            if (tx < 0 || tx >= width) continue;
            const uint8_t* s = &over.rgba[(static_cast<std::size_t>(oy) * over.width + ox) * 4];
            uint8_t* d       = &rgba[(static_cast<std::size_t>(ty) * width + tx) * 4];
            const int a      = s[3];
            if (a == 0) continue;
            if (a == 255) {
                std::memcpy(d, s, 4);
                continue;
            }
            const int da  = d[3];
            const int out = a + da * (255 - a) / 255;  // the alpha left
            for (int k = 0; k < 3; ++k)
                d[k] = static_cast<uint8_t>(out == 0 ? 0 : (s[k] * a + d[k] * da * (255 - a) / 255) / out);
            d[3] = static_cast<uint8_t>(out);
        }
    }
}

Image rasterize(const Drawing& drawing, float scale) {
    Image image;
    image.width  = std::max(1, static_cast<int>(std::ceil(drawing.width * scale - 0.01f)));
    image.height = std::max(1, static_cast<int>(std::ceil(drawing.height * scale - 0.01f)));
    image.rgba.assign(static_cast<std::size_t>(image.width) * image.height * 4, 0);
    if (drawing.shapes.empty()) return image;
    std::string svg   = svgDocument(svgElements(drawing, "g", 0, 0, scale), static_cast<float>(image.width),
                                    static_cast<float>(image.height), true);
    NSVGimage* parsed = nsvgParse(svg.data(), "px", 96.0f);
    if (!parsed) return image;
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (rast) {
        nsvgRasterize(rast, parsed, 0, 0, 1.0f, image.rgba.data(), image.width, image.height, image.width * 4);
        nsvgDeleteRasterizer(rast);
    }
    nsvgDelete(parsed);
    return image;
}

namespace {

uint32_t crc32(std::string_view data) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xffffffffu;
    for (const char ch : data) c = table[(c ^ static_cast<uint8_t>(ch)) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

void putBe32(std::string& out, uint32_t v) {
    for (int k = 3; k >= 0; --k) out += static_cast<char>(v >> (8 * k) & 0xff);
}

void putLe(std::string& out, uint32_t v, int bytes) {
    for (int k = 0; k < bytes; ++k) out += static_cast<char>(v >> (8 * k) & 0xff);
}

}  // namespace

std::string pngBytes(const Image& image, const std::string& comment) {
    std::string out;
    stbi_write_png_to_func(
        [](void* context, void* data, int size) {
            static_cast<std::string*>(context)->append(static_cast<const char*>(data), static_cast<std::size_t>(size));
        },
        &out, image.width, image.height, 4, image.rgba.data(), image.width * 4);
    if (comment.empty() || out.size() < 33) return out;
    // A tEXt chunk after IHDR (8 bytes of signature, 25 of IHDR): Latin-1 text, so its non-ASCII letters left out.
    std::string text = "Comment";
    text += '\0';
    for (const char c : comment)
        if (static_cast<unsigned char>(c) < 0x80) text += c;
    std::string chunk = "tEXt" + text;
    std::string bytes;
    putBe32(bytes, static_cast<uint32_t>(text.size()));
    bytes += chunk;
    putBe32(bytes, crc32(chunk));
    out.insert(33, bytes);
    return out;
}

std::string bmpBytes(const Image& image) {
    const uint32_t row  = static_cast<uint32_t>(image.width) * 4;
    const uint32_t size = 14 + 40 + row * static_cast<uint32_t>(image.height);
    std::string out     = "BM";
    putLe(out, size, 4);
    putLe(out, 0, 4);
    putLe(out, 54, 4);
    putLe(out, 40, 4);
    putLe(out, static_cast<uint32_t>(image.width), 4);
    putLe(out, static_cast<uint32_t>(image.height), 4);
    putLe(out, 1, 2);
    putLe(out, 32, 2);
    putLe(out, 0, 4);  // BI_RGB
    putLe(out, row * static_cast<uint32_t>(image.height), 4);
    putLe(out, 2835, 4);
    putLe(out, 2835, 4);
    putLe(out, 0, 4);
    putLe(out, 0, 4);
    for (int y = image.height - 1; y >= 0; --y) {
        for (int x = 0; x < image.width; ++x) {
            const uint8_t* p = &image.rgba[(static_cast<std::size_t>(y) * image.width + x) * 4];
            out += static_cast<char>(p[2]);
            out += static_cast<char>(p[1]);
            out += static_cast<char>(p[0]);
            out += static_cast<char>(p[3]);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// The pieces

namespace {

Rgba nsvgColour(unsigned int c, float opacity = 1) {
    return Rgba{static_cast<uint8_t>(c & 0xff), static_cast<uint8_t>(c >> 8 & 0xff),
                static_cast<uint8_t>(c >> 16 & 0xff), static_cast<uint8_t>(std::lround((c >> 24 & 0xff) * opacity))};
}

// nanosvg keeps a gradient's transform from the drawing to the gradient's space: the other way, for SVG and PDF.
std::array<double, 6> inverse(const float* t) {
    const double det = static_cast<double>(t[0]) * t[3] - static_cast<double>(t[2]) * t[1];
    if (std::abs(det) < 1e-12) return {1, 0, 0, 1, 0, 0};
    const double inv = 1.0 / det;
    return {t[3] * inv,
            -t[1] * inv,
            -t[2] * inv,
            t[0] * inv,
            (static_cast<double>(t[2]) * t[5] - static_cast<double>(t[3]) * t[4]) * inv,
            (static_cast<double>(t[1]) * t[4] - static_cast<double>(t[0]) * t[5]) * inv};
}

Paint paintOf(const NSVGpaint& p, Drawing& d) {
    Paint out;
    if (p.type == NSVG_PAINT_COLOR) {
        out.kind   = Paint::Kind::Solid;
        out.colour = nsvgColour(p.color);
    } else if ((p.type == NSVG_PAINT_LINEAR_GRADIENT || p.type == NSVG_PAINT_RADIAL_GRADIENT) && p.gradient) {
        Gradient g;
        g.radial = p.type == NSVG_PAINT_RADIAL_GRADIENT;
        g.matrix = inverse(p.gradient->xform);
        g.spread = p.gradient->spread;
        for (int i = 0; i < p.gradient->nstops; ++i)
            g.stops.push_back({p.gradient->stops[i].offset, nsvgColour(p.gradient->stops[i].color)});
        out.kind     = Paint::Kind::Gradient;
        out.gradient = static_cast<int>(d.gradients.size());
        d.gradients.push_back(std::move(g));
    }
    return out;
}

Drawing drawingOf(const std::string& svg_text) {
    Drawing d;
    d.width = d.height = 1;
    std::string text   = svg_text;  // nsvgParse writes into it
    NSVGimage* image   = nsvgParse(text.data(), "px", 96.0f);
    if (!image) return d;
    const float k = 1.0f / std::max(1.0f, std::max(image->width, image->height));
    for (NSVGshape* shape = image->shapes; shape; shape = shape->next) {
        if (!(shape->flags & NSVG_FLAGS_VISIBLE)) continue;
        Shape s;
        for (NSVGpath* path = shape->paths; path; path = path->next) {
            if (path->npts < 1) continue;
            s.path.push_back({PathCmd::Move, {path->pts[0]}, {path->pts[1]}});
            for (int i = 1; i + 2 < path->npts; i += 3) {
                const float* p = &path->pts[i * 2];
                s.path.push_back({PathCmd::Cubic, {p[0], p[2], p[4]}, {p[1], p[3], p[5]}});
            }
            if (path->closed) s.path.push_back({PathCmd::Close});
        }
        s.fill         = paintOf(shape->fill, d);
        s.stroke       = paintOf(shape->stroke, d);
        s.stroke_width = shape->strokeWidth;
        s.join         = shape->strokeLineJoin;
        s.cap          = shape->strokeLineCap;
        s.miter        = shape->miterLimit;
        s.even_odd     = shape->fillRule == NSVG_FILLRULE_EVENODD;
        s.opacity      = shape->opacity;
        for (int i = 0; i < shape->strokeDashCount; ++i) s.dashes.push_back(shape->strokeDashArray[i]);
        s.dash_offset = shape->strokeDashOffset;
        // The stroke under the fill when the paint order says so: two shapes.
        int fill_at = 0, stroke_at = 1;
        for (int j = 0; j < 3; ++j) {
            const int what = shape->paintOrder >> (2 * j) & 0x3;
            if (what == NSVG_PAINT_FILL) fill_at = j;
            if (what == NSVG_PAINT_STROKE) stroke_at = j;
        }
        const bool stroke_first = stroke_at < fill_at;
        if (stroke_first && s.stroke.kind != Paint::Kind::None && s.fill.kind != Paint::Kind::None) {
            Shape stroke = s;
            stroke.fill  = {};
            s.stroke     = {};
            d.shapes.push_back(std::move(stroke));
        }
        d.shapes.push_back(std::move(s));
    }
    nsvgDelete(image);
    // Into a box of 1, as the atlas scales them (rasterizePieces()).
    Drawing unit;
    unit.width = unit.height = 1;
    unit.place(d, 0, 0, k);
    return unit;
}

}  // namespace

std::shared_ptr<const PieceShapes> PieceShapes::make(const appearance::PieceSvgs& svgs) {
    auto out  = std::make_shared<PieceShapes>();
    out->svgs = svgs;
    std::string all;
    for (const auto& svg : svgs) all += svg;
    out->id = std::hash<std::string>{}(all);
    for (std::size_t i = 0; i < 12; ++i) out->pieces[i] = drawingOf(svgs[i]);
    return out;
}

const Drawing& PieceShapes::of(chess::Piece piece) const { return pieces[static_cast<std::size_t>(pieceIndex(piece))]; }

std::shared_ptr<const std::vector<uint8_t>> pieceTiles(const PieceShapes& pieces, int pixels) {
    static std::mutex mutex;
    static std::map<std::pair<std::size_t, int>, std::weak_ptr<const std::vector<uint8_t>>> kept;
    static std::shared_ptr<const std::vector<uint8_t>> last;
    std::lock_guard<std::mutex> lock(mutex);
    const auto key = std::make_pair(pieces.id, pixels);
    if (const auto it = kept.find(key); it != kept.end())
        if (auto tiles = it->second.lock()) return tiles;
    auto tiles = std::make_shared<const std::vector<uint8_t>>(rasterizePieces(pixels, pieces.svgs));
    kept[key]  = tiles;
    last       = tiles;  // the last size stays while it is asked again and again (a GIF's frames)
    return tiles;
}

// ---------------------------------------------------------------------------------------------------------------------
// The diagram

DiagramSpec diagramAt(const ReplayGame& game, bool chess960, int ply, const MoveNotes* start_notes,
                      const appearance::BoardColours& colours) {
    DiagramSpec spec;
    ply              = std::clamp(ply, 0, static_cast<int>(game.moves.size()));
    const auto board = boardAt(game, chess960, ply);
    for (int i = 0; i < 64; ++i) spec.pieces[static_cast<std::size_t>(i)] = board.at(chess::Square(i));
    spec.side = board.sideToMove();
    if (board.inCheck()) spec.check = board.kingSq(board.sideToMove());
    const MoveNotes* notes = start_notes;
    if (ply > 0) {
        const auto& m  = game.moves[static_cast<std::size_t>(ply - 1)];
        spec.last_move = m.move;
        notes          = &m.notes;
    }
    if (notes) {
        spec.arrows  = notes->arrows;
        spec.squares = notes->squares;
    }
    spec.colours = colours;
    return spec;
}

std::vector<std::vector<std::array<float, 2>>> arrowShapes(float ax, float ay, float bx, float by, float square) {
    std::vector<std::vector<std::array<float, 2>>> out;
    const auto polygons = arrowPolygons(ImVec2(ax, ay), ImVec2(bx, by), square, false);
    if (!polygons) return out;
    for (const auto& q : polygons->shaft)
        out.push_back({{q[0].x, q[0].y}, {q[1].x, q[1].y}, {q[2].x, q[2].y}, {q[3].x, q[3].y}});
    out.push_back({{polygons->head[0].x, polygons->head[0].y},
                   {polygons->head[1].x, polygons->head[1].y},
                   {polygons->head[2].x, polygons->head[2].y}});
    return out;
}

namespace {

Drawing greyed(Drawing d) {
    for (auto& s : d.shapes) {
        s.fill.colour   = grey(s.fill.colour);
        s.stroke.colour = grey(s.stroke.colour);
    }
    for (auto& g : d.gradients)
        for (auto& stop : g.stops) stop.colour = grey(stop.colour);
    return d;
}

}  // namespace

DiagramLayers diagramLayers(const DiagramSpec& spec, float board) {
    DiagramLayers l;
    l.board          = board;
    l.square         = board / 8;
    const float sq   = l.square;
    const bool bw    = spec.black_and_white;
    const auto light = bw ? Rgba{255, 255, 255, 255} : rgba(spec.colours.light);
    const auto dark  = bw ? Rgba{200, 200, 200, 255} : rgba(spec.colours.dark);
    auto highlight   = rgba(spec.colours.highlight);
    if (bw) highlight = Rgba{90, 90, 90, 70};
    const float dot_room     = spec.side_dot ? board * 0.05f : 0.0f;
    const float caption_room = spec.caption.empty() ? 0.0f : board * 0.1f;
    l.width                  = board + dot_room;
    l.height                 = board + caption_room;
    l.under.width = l.over.width = l.width;
    l.under.height = l.over.height = l.height;

    const auto origin = [&](chess::Square s) {
        const int file = static_cast<int>(s.file()), rank = static_cast<int>(s.rank());
        const int col = spec.flipped ? 7 - file : file, row = spec.flipped ? rank : 7 - rank;
        return std::array<float, 2>{col * sq, row * sq};
    };
    std::optional<chess::Square> from, to;
    if (spec.last_move) {
        from = spec.last_move->from();
        to   = displayedTarget(*spec.last_move);
    }
    // As drawBoard() does: squares, the last move, the marked squares, the check; coordinates in the other colour.
    const auto& font = Fonts::get().font(FontId::Regular);
    const float size = std::max(9.0f * board / 480.0f, sq * 0.2f);  // ImGui's size: ascent to descent
    const float em   = size / (font.ascent() - font.descent());
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            const chess::Square square{chess::File(file), chess::Rank(rank)};
            const auto [x, y]   = origin(square);
            const bool is_light = (file + rank) % 2 == 1;
            l.under.rect(x, y, sq, sq, is_light ? light : dark);
            if ((from && *from == square) || (to && *to == square)) l.under.rect(x, y, sq, sq, highlight);
            for (const auto& m : spec.squares)
                if (m.from == square.index()) {
                    const auto c = rgba(markColour(m.colour, true));
                    l.under.rect(x, y, sq, sq, bw ? grey(c) : c);
                }
            if (spec.check && *spec.check == square) {
                const auto c = rgba(BOARD_CHECK);
                l.under.circle(x + sq / 2, y + sq / 2, sq * 0.48f, bw ? grey(c) : c);
            }
            const auto coord = is_light ? dark : light;
            const int col = spec.flipped ? 7 - file : file, row = spec.flipped ? rank : 7 - rank;
            if (row == 7 && spec.coordinates) {
                const char label[2] = {static_cast<char>('a' + file), 0};
                l.under.text(label, FontId::Regular, em, x + sq - size * 0.6f,
                             y + sq - size * 1.05f + font.ascent() * em, coord);
            }
            if (col == 0 && spec.coordinates) {
                const char label[2] = {static_cast<char>('1' + rank), 0};
                l.under.text(label, FontId::Regular, em, x + sq / 30, y + sq / 60 + font.ascent() * em, coord);
            }
            const auto piece = spec.pieces[static_cast<std::size_t>(square.index())];
            if (piece != chess::Piece::NONE) l.pieces.push_back({piece, x, y});
        }
    }
    // The arrows a person drew, over the pieces.
    for (const auto& m : spec.arrows) {
        if (m.from < 0 || m.to < 0 || m.from > 63 || m.to > 63 || m.from == m.to) continue;
        const auto a = origin(chess::Square(m.from)), b = origin(chess::Square(m.to));
        auto colour = rgba(markColour(m.colour, false));
        if (bw) colour = grey(colour);
        for (const auto& polygon : arrowShapes(a[0] + sq / 2, a[1] + sq / 2, b[0] + sq / 2, b[1] + sq / 2, sq))
            l.over.polygon(polygon, colour);
    }
    // In black and white, a thin frame: its white squares would melt into the page.
    if (bw) {
        const float w = std::max(1.0f, board / 400);
        l.over.rect(0, 0, board, w, {40, 40, 40, 255});
        l.over.rect(0, board - w, board, w, {40, 40, 40, 255});
        l.over.rect(0, 0, w, board, {40, 40, 40, 255});
        l.over.rect(board - w, 0, w, board, {40, 40, 40, 255});
    }
    // The side to move: a disc of its colour beside its own edge.
    if (spec.side_dot) {
        const bool bottom = (spec.side == chess::Color::WHITE) != spec.flipped;
        const float r     = board * 0.016f;
        const float cx = board + dot_room / 2, cy = bottom ? board - r * 1.6f : r * 1.6f;
        if (spec.side == chess::Color::WHITE)
            l.over.circle(cx, cy, r, {255, 255, 255, 255}, Rgba{30, 30, 30, 255}, std::max(1.0f, board / 480));
        else
            l.over.circle(cx, cy, r, {30, 30, 30, 255});
    }
    if (!spec.caption.empty()) {
        float cem     = board * 0.042f;
        const float w = textWidth(spec.caption, FontId::SemiBold, cem);
        if (w > board) cem *= board / w;
        const float width = textWidth(spec.caption, FontId::SemiBold, cem);
        l.over.text(spec.caption, FontId::SemiBold, cem, (board - width) / 2, board + caption_room * 0.66f,
                    bw ? Rgba{30, 30, 30, 255} : Rgba{31, 35, 41, 255});
    }
    return l;
}

std::string diagramSvg(const DiagramSpec& spec, const PieceShapes& pieces, bool sized, float board) {
    const auto l = diagramLayers(spec, board);
    std::string elements;
    if (!spec.caption.empty() || spec.side_dot) {
        Drawing paper;
        paper.rect(0, 0, l.width, l.height, {255, 255, 255, 255});
        elements += svgElements(paper, "w");
    }
    elements += svgElements(l.under, "u");
    int n = 0;
    for (const auto& p : l.pieces) {
        const auto& piece = spec.black_and_white ? greyed(pieces.of(p.piece)) : pieces.of(p.piece);
        elements += svgElements(piece, fmt::format("p{}-", n++), p.x, p.y, l.square);
    }
    elements += svgElements(l.over, "o");
    return svgDocument(elements, l.width, l.height, sized, spec.caption);
}

Drawing diagramDrawing(const DiagramSpec& spec, const PieceShapes& pieces, float board) {
    auto l    = diagramLayers(spec, board);
    Drawing d = std::move(l.under);
    for (const auto& p : l.pieces)
        d.place(spec.black_and_white ? greyed(pieces.of(p.piece)) : pieces.of(p.piece), p.x, p.y, l.square);
    d.place(l.over, 0, 0, 1);
    d.width  = l.width;
    d.height = l.height;
    return d;
}

Image diagramImage(const DiagramSpec& spec, const PieceShapes& pieces, int board) {
    board        = std::max(8, board / 8 * 8);  // whole squares: the pieces' images land on pixels
    const auto l = diagramLayers(spec, static_cast<float>(board));
    Image image;
    image.width  = static_cast<int>(std::lround(l.width));
    image.height = static_cast<int>(std::lround(l.height));
    image.fill({255, 255, 255, 255});
    image.blend(rasterize(l.under));
    // The pieces as the board's atlas has them: rasterized from their SVGs for a square of that size.
    const int sq     = board / 8;
    const auto tiles = pieceTiles(pieces, sq);
    if (!tiles->empty()) {
        for (const auto& p : l.pieces) {
            Image tile;
            tile.width  = sq;
            tile.height = sq;
            tile.rgba.resize(static_cast<std::size_t>(sq) * sq * 4);
            const int index = pieceIndex(p.piece);
            for (int y = 0; y < sq; ++y)
                std::memcpy(
                    &tile.rgba[static_cast<std::size_t>(y) * sq * 4],
                    &(*tiles)[(static_cast<std::size_t>(y) * sq * 12 + static_cast<std::size_t>(index) * sq) * 4],
                    static_cast<std::size_t>(sq) * 4);
            if (spec.black_and_white)
                for (std::size_t i = 0; i < tile.rgba.size(); i += 4) {
                    const auto g = grey({tile.rgba[i], tile.rgba[i + 1], tile.rgba[i + 2], 255});
                    tile.rgba[i] = tile.rgba[i + 1] = tile.rgba[i + 2] = g.r;
                }
            image.blend(tile, static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y)));
        }
    }
    image.blend(rasterize(l.over));
    return image;
}

std::string fileStem(const std::string& text) {
    static const std::map<char32_t, const char*> folds = {
        {U'à', "a"}, {U'á', "a"}, {U'â', "a"}, {U'ã', "a"}, {U'ä', "a"}, {U'å', "a"},  {U'æ', "ae"}, {U'ç', "c"},
        {U'è', "e"}, {U'é', "e"}, {U'ê', "e"}, {U'ë', "e"}, {U'ì', "i"}, {U'í', "i"},  {U'î', "i"},  {U'ï', "i"},
        {U'ñ', "n"}, {U'ò', "o"}, {U'ó', "o"}, {U'ô', "o"}, {U'õ', "o"}, {U'ö', "o"},  {U'ø', "o"},  {U'ù', "u"},
        {U'ú', "u"}, {U'û', "u"}, {U'ü', "u"}, {U'ý', "y"}, {U'ÿ', "y"}, {U'ß', "ss"}, {U'š', "s"},  {U'ž', "z"},
        {U'č', "c"}, {U'ć', "c"}, {U'ł', "l"}, {U'ń', "n"}, {U'ś', "s"}, {U'ź', "z"},  {U'ż', "z"},  {U'ř', "r"},
        {U'ě', "e"}, {U'ů', "u"}, {U'ğ', "g"}, {U'ı', "i"}, {U'ş', "s"}, {U'œ', "oe"}};
    std::string out;
    bool dash = false;
    for (char32_t c : utf32(text)) {
        if (c >= U'A' && c <= U'Z') c = c - U'A' + U'a';
        if (c >= 0xC0 && c <= 0xDE && c != 0xD7) c += 0x20;  // Latin-1's capitals
        std::string piece;
        if ((c >= U'a' && c <= U'z') || (c >= U'0' && c <= U'9'))
            piece = std::string(1, static_cast<char>(c));
        else if (const auto it = folds.find(c); it != folds.end())
            piece = it->second;
        if (piece.empty()) {
            dash = !out.empty();
            continue;
        }
        if (dash) out += '-';
        dash = false;
        out += piece;
        if (out.size() >= 60) break;
    }
    return out;
}

}  // namespace fcg::publish
