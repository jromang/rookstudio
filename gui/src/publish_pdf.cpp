#include "publish_pdf.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>

#include <fmt/include/fmt/format.h>

// stb_image_write's deflate, compiled with its PNG writer (screenshot.cpp); not declared in its header.
extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);

namespace fcg::publish {

std::string deflate(const std::string& data) {
    int length       = 0;
    unsigned char* z = stbi_zlib_compress(reinterpret_cast<unsigned char*>(const_cast<char*>(data.data())),
                                          static_cast<int>(data.size()), &length, 8);
    if (!z) return {};
    std::string out(reinterpret_cast<const char*>(z), static_cast<std::size_t>(length));
    std::free(z);
    return out;
}

namespace {

std::string num(double v) {
    if (std::abs(v) < 0.0005) return "0";
    auto s = fmt::format("{:.3f}", v);
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
    return s;
}

std::string colour(Rgba c) { return num(c.r / 255.0) + " " + num(c.g / 255.0) + " " + num(c.b / 255.0); }

// A text string of the document's Info: UTF-16BE with its mark, in hexadecimal.
std::string infoText(const std::string& text) {
    std::string out = "<FEFF";
    for (char32_t c : utf32(text)) {
        if (c >= 0x10000) {
            c -= 0x10000;
            out += fmt::format("{:04X}{:04X}", 0xD800 + (c >> 10), 0xDC00 + (c & 0x3ff));
        } else {
            out += fmt::format("{:04X}", static_cast<uint32_t>(c));
        }
    }
    return out + ">";
}

struct Writer {
    std::vector<std::string> objects;  // by number - 1
    int alloc() {
        objects.emplace_back();
        return static_cast<int>(objects.size());
    }
    void set(int n, std::string body) { objects[static_cast<std::size_t>(n - 1)] = std::move(body); }
    int add(std::string body) {
        const int n = alloc();
        set(n, std::move(body));
        return n;
    }
    // A stream, deflated.
    int stream(const std::string& data, const std::string& dict = {}) {
        const auto z = deflate(data);
        return add(fmt::format("<< /Length {} /Filter /FlateDecode{} >>\nstream\n", z.size(), dict) + z +
                   "\nendstream");
    }
};

// What a font of the document needs: its glyphs, and the characters each one writes.
struct FontUse {
    std::set<int> glyphs;
    std::map<int, std::u32string> chars;  // by code (glyph or CID)
    bool embedded = true;
};
using FontUses = std::array<FontUse, FONT_COUNT>;

// The glyphs each font writes on the pages, and the characters each code writes.
FontUses fontUses(const std::vector<Page>& pages, const Fonts& fonts) {
    FontUses uses;
    for (const auto& page : pages)
        for (const auto& t : page.texts)
            for (const auto& run : shapeText(t.text, t.font)) {
                auto& use     = uses[static_cast<std::size_t>(run.font)];
                const auto& f = fonts.font(run.font);
                for (std::size_t i = 0; i < run.glyphs.size(); ++i) {
                    use.glyphs.insert(run.glyphs[i]);
                    auto& chars = use.chars[f.cid(run.glyphs[i])];
                    if (chars.empty()) chars = std::u32string(1, run.chars[i]);
                }
            }
    return uses;
}

// A subset's tag: six capitals from what it holds.
std::string subsetTag(const Font& f, const std::set<int>& glyphs) {
    std::string key = f.postscriptName();
    for (const int g : glyphs) key += std::to_string(g) + ",";
    auto hash = std::hash<std::string>{}(key);
    std::string tag;
    for (int k = 0; k < 6; ++k) {
        tag += static_cast<char>('A' + hash % 26);
        hash /= 26;
    }
    return tag;
}

// A font's ToUnicode: what each code writes.
std::string toUnicode(const FontUse& use) {
    std::string cmap =
        "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n/CIDSystemInfo << /Registry (Adobe) "
        "/Ordering (UCS) /Supplement 0 >> def\n/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n1 "
        "begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
    std::vector<std::pair<int, std::u32string>> entries(use.chars.begin(), use.chars.end());
    for (std::size_t i = 0; i < entries.size(); i += 100) {
        const auto n = std::min<std::size_t>(100, entries.size() - i);
        cmap += fmt::format("{} beginbfchar\n", n);
        for (std::size_t k = i; k < i + n; ++k) {
            std::string u;
            for (char32_t c : entries[k].second) {
                if (c >= 0x10000) {
                    c -= 0x10000;
                    u += fmt::format("{:04X}{:04X}", 0xD800 + (c >> 10), 0xDC00 + (c & 0x3ff));
                } else {
                    u += fmt::format("{:04X}", static_cast<uint32_t>(c));
                }
            }
            cmap += fmt::format("<{:04X}> <{}>\n", entries[k].first, u);
        }
        cmap += "endbfchar\n";
    }
    cmap += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
    return cmap;
}

// The fonts embedded, by FontId: their names in the pages' content, and the pages' font resources.
struct EmbeddedFonts {
    std::map<int, std::string> names;
    std::string resources;
};

// The fonts: Type 0, a CIDFont of TrueType or CFF outlines, its descriptor, its program cut down, its ToUnicode. A
// font whose program cannot be cut down is not embedded (`embedded` false): its text is drawn as outlines.
EmbeddedFonts embedFonts(Writer& w, const Fonts& fonts, FontUses& uses) {
    EmbeddedFonts out;
    for (int id = 0; id < FONT_COUNT; ++id) {
        auto& use = uses[static_cast<std::size_t>(id)];
        if (use.glyphs.empty()) continue;
        const auto& f      = fonts.font(static_cast<FontId>(id));
        const auto program = f.subset(use.glyphs);
        if (program.empty()) {
            use.embedded = false;  // drawn as outlines
            continue;
        }
        const std::string name = subsetTag(f, use.glyphs) + "+" + f.postscriptName();
        const int file_n       = f.cff() ? w.stream(program, " /Subtype /CIDFontType0C")
                                         : w.stream(program, fmt::format(" /Length1 {}", program.size()));
        const auto& d          = f.descriptor();
        const int flags        = 32 | (d.italic_angle != 0 ? 64 : 0) | (d.fixed ? 1 : 0);
        const int desc_n       = w.add(fmt::format(
            "<< /Type /FontDescriptor /FontName /{} /Flags {} /FontBBox [{} {} {} {}] /ItalicAngle {} /Ascent {} "
            "/Descent {} /CapHeight {} /StemV {} /{} {} 0 R >>",
            name, flags, d.bbox[0], d.bbox[1], d.bbox[2], d.bbox[3], num(d.italic_angle), d.ascent, d.descent,
            d.cap_height, d.weight >= 600 ? 120 : 80, f.cff() ? "FontFile3" : "FontFile2", file_n));
        std::string widths;
        for (const int g : use.glyphs)
            widths += fmt::format("{} [{}] ", f.cid(g), static_cast<int>(std::lround(f.advance(g) * 1000)));
        std::string registry = "Adobe", ordering = "Identity", supplement = "0";
        if (f.cidKeyed() && !f.ros().empty()) {
            const auto a = f.ros().find(' '), b = f.ros().rfind(' ');
            if (a != std::string::npos && b != a) {
                registry   = f.ros().substr(0, a);
                ordering   = f.ros().substr(a + 1, b - a - 1);
                supplement = f.ros().substr(b + 1);
            }
        }
        const int cid_n = w.add(fmt::format(
            "<< /Type /Font /Subtype /{} /BaseFont /{} /CIDSystemInfo << /Registry ({}) /Ordering ({}) /Supplement {} "
            ">> /FontDescriptor {} 0 R /DW 1000 /W [{}]{} >>",
            f.cff() ? "CIDFontType0" : "CIDFontType2", name, registry, ordering, supplement, desc_n, widths,
            f.cff() ? "" : " /CIDToGIDMap /Identity"));
        const int unicode_n = w.stream(toUnicode(use));
        const int type0_n =
            w.add(fmt::format("<< /Type /Font /Subtype /Type0 /BaseFont /{} /Encoding /Identity-H "
                              "/DescendantFonts [{} 0 R] /ToUnicode {} 0 R >>",
                              name, cid_n, unicode_n));
        out.names[id] = fmt::format("F{}", id);
        out.resources += fmt::format("/F{} {} 0 R ", id, type0_n);
    }
    return out;
}

// Transparency and gradients, shared by the pages: each made once, when a page first paints with it.
class Paints {
   public:
    explicit Paints(Writer& w) : w_(w) {}

    // A graphics state of these opacities, in 255ths: its name.
    std::string alpha(int fill, int stroke) {
        const auto key = std::make_pair(fill, stroke);
        if (const auto it = alphas_.find(key); it != alphas_.end()) return it->second;
        const auto name = fmt::format("A{}", alphas_.size());
        const int n =
            w_.add(fmt::format("<< /Type /ExtGState /ca {} /CA {} >>", num(fill / 255.0), num(stroke / 255.0)));
        gs_resources_ += fmt::format("/{} {} 0 R ", name, n);
        alphas_[key] = name;
        return name;
    }

    // A gradient's shading: its name.
    std::string shading(const Gradient& g) {
        auto stops = g.stops;
        if (stops.empty()) stops.push_back({0, {0, 0, 0, 255}});
        std::sort(stops.begin(), stops.end(), [](const auto& a, const auto& b) { return a.offset < b.offset; });
        if (stops.front().offset > 0) stops.insert(stops.begin(), {0, stops.front().colour});
        if (stops.back().offset < 1) stops.push_back({1, stops.back().colour});
        const auto rgb = [](Rgba c) { return "[" + colour(c) + "]"; };
        std::string function;
        if (stops.size() == 2) {
            function = fmt::format("<< /FunctionType 2 /Domain [0 1] /C0 {} /C1 {} /N 1 >>", rgb(stops[0].colour),
                                   rgb(stops[1].colour));
        } else {
            std::string functions, bounds, encode;
            for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
                functions += fmt::format("<< /FunctionType 2 /Domain [0 1] /C0 {} /C1 {} /N 1 >> ",
                                         rgb(stops[i].colour), rgb(stops[i + 1].colour));
                if (i > 0) bounds += num(std::clamp(stops[i].offset, 0.0f, 1.0f)) + " ";
                encode += "0 1 ";
            }
            function = fmt::format("<< /FunctionType 3 /Domain [0 1] /Functions [{}] /Bounds [{}] /Encode [{}] >>",
                                   functions, bounds, encode);
        }
        const auto dict = fmt::format(
            "<< /ShadingType {} /ColorSpace /DeviceRGB /Coords [{}] /Function {} /Extend [true "
            "true] >>",
            g.radial ? 3 : 2, g.radial ? "0 0 0 0 0 1" : "0 0 0 1", function);
        if (const auto it = shadings_.find(dict); it != shadings_.end()) return it->second;
        const auto name = fmt::format("Sh{}", shadings_.size());
        const int n     = w_.add(dict);
        sh_resources_ += fmt::format("/{} {} 0 R ", name, n);
        shadings_[dict] = name;
        return name;
    }

    [[nodiscard]] const std::string& gsResources() const noexcept { return gs_resources_; }
    [[nodiscard]] const std::string& shResources() const noexcept { return sh_resources_; }

   private:
    Writer& w_;
    std::map<std::pair<int, int>, std::string> alphas_;  // (fill, stroke) in 255ths -> its name
    std::string gs_resources_;
    std::map<std::string, std::string> shadings_;  // its dictionary -> its name
    std::string sh_resources_;
};

// A path's operators, moved by (x, y).
std::string pathOps(const Path& path, double x, double y) {
    std::string out;
    for (const auto& c : path) {
        switch (c.op) {
            case PathCmd::Move:
                out += num(c.x[0] + x) + " " + num(c.y[0] + y) + " m ";
                break;
            case PathCmd::Line:
                out += num(c.x[0] + x) + " " + num(c.y[0] + y) + " l ";
                break;
            case PathCmd::Cubic:
                out += num(c.x[0] + x) + " " + num(c.y[0] + y) + " " + num(c.x[1] + x) + " " + num(c.y[1] + y) + " " +
                       num(c.x[2] + x) + " " + num(c.y[2] + y) + " c ";
                break;
            case PathCmd::Close:
                out += "h ";
                break;
        }
    }
    return out;
}

// A drawing's shapes, moved by (x, y): filled, stroked, or both, with their transparency and gradients.
std::string drawingOps(const Drawing& d, double x, double y, Paints& paints) {
    std::string out;
    for (const auto& s : d.shapes) {
        if (s.path.empty()) continue;
        const bool fill   = s.fill.kind != Paint::Kind::None;
        const bool stroke = s.stroke.kind != Paint::Kind::None && s.stroke_width > 0;
        if (!fill && !stroke) continue;
        out += "q ";
        const int fa = fill && s.fill.kind == Paint::Kind::Solid
                           ? static_cast<int>(std::lround(s.fill.colour.a * s.opacity))
                           : static_cast<int>(std::lround(255 * s.opacity));
        const int sa = stroke && s.stroke.kind == Paint::Kind::Solid
                           ? static_cast<int>(std::lround(s.stroke.colour.a * s.opacity))
                           : static_cast<int>(std::lround(255 * s.opacity));
        if (fa < 255 || sa < 255) out += "/" + paints.alpha(fa, sa) + " gs ";
        const auto p = pathOps(s.path, x, y);
        if (fill && s.fill.kind == Paint::Kind::Gradient && s.fill.gradient >= 0 &&
            s.fill.gradient < static_cast<int>(d.gradients.size())) {
            const auto& g = d.gradients[static_cast<std::size_t>(s.fill.gradient)];
            const auto& m = g.matrix;
            out += "q " + p + (s.even_odd ? "W* n " : "W n ") +
                   fmt::format("{} {} {} {} {} {} cm /{} sh Q ", num(m[0]), num(m[1]), num(m[2]), num(m[3]),
                               num(m[4] + x), num(m[5] + y), paints.shading(g));
        } else if (fill) {
            out += colour(s.fill.colour) + " rg ";
        }
        if (stroke) {
            Rgba sc = s.stroke.colour;
            if (s.stroke.kind == Paint::Kind::Gradient && s.stroke.gradient >= 0 &&
                s.stroke.gradient < static_cast<int>(d.gradients.size()) &&
                !d.gradients[static_cast<std::size_t>(s.stroke.gradient)].stops.empty())
                sc = d.gradients[static_cast<std::size_t>(s.stroke.gradient)].stops.front().colour;
            out += colour(sc) + " RG " + num(s.stroke_width) + " w " + std::to_string(s.cap) + " J " +
                   std::to_string(s.join) + " j " + num(s.miter) + " M ";
            if (!s.dashes.empty()) {
                out += "[";
                for (const auto dash : s.dashes) out += num(dash) + " ";
                out += "] " + num(s.dash_offset) + " d ";
            }
        }
        const bool solid_fill = fill && s.fill.kind == Paint::Kind::Solid;
        if (solid_fill && stroke)
            out += p + (s.even_odd ? "B* " : "B ");
        else if (solid_fill)
            out += p + (s.even_odd ? "f* " : "f ");
        else if (stroke)
            out += p + "S ";
        out += "Q\n";
    }
    return out;
}

// What a page's content needs beside the page: the pieces, the fonts and how they were embedded, the shared paints.
struct PageInk {
    const PieceShapes* pieces;
    const Fonts& fonts;
    const FontUses& uses;
    const EmbeddedFonts& embedded;
    Paints& paints;
};

// A page's content stream: its rectangles, its lines, its diagrams, then its texts.
std::string pageContent(const Page& page, const PageInk& ink) {
    std::string c = fmt::format("1 0 0 -1 0 {} cm\n", num(page.height));
    for (const auto& r : page.rects) {
        c += "q ";
        if (r.fill.a < 255) c += "/" + ink.paints.alpha(r.fill.a, 255) + " gs ";
        c += colour(r.fill) + " rg " + num(r.x) + " " + num(r.y) + " " + num(r.w) + " " + num(r.h) + " re f Q\n";
    }
    for (const auto& l : page.lines)
        c += "q " + colour(l.colour) + " RG " + num(l.width) + " w " + num(l.x0) + " " + num(l.y0) + " m " + num(l.x1) +
             " " + num(l.y1) + " l S Q\n";
    for (const auto& d : page.diagrams)
        if (ink.pieces) c += drawingOps(diagramDrawing(d.spec, *ink.pieces, d.board), d.x, d.y, ink.paints);
    for (const auto& t : page.texts) {
        float x = t.x;
        for (const auto& run : shapeText(t.text, t.font)) {
            const auto& f    = ink.fonts.font(run.font);
            const auto named = ink.embedded.names.find(static_cast<int>(run.font));
            if (ink.uses[static_cast<std::size_t>(run.font)].embedded && named != ink.embedded.names.end()) {
                std::string codes;
                for (const int g : run.glyphs) codes += fmt::format("{:04X}", f.cid(g));
                c += fmt::format("BT /{} {} Tf {} rg 1 0 0 -1 {} {} Tm <{}> Tj ET\n", named->second, num(t.size),
                                 colour(t.colour), num(x), num(t.y), codes);
            } else {
                // A font that could not be embedded: its outlines.
                Drawing outline;
                Shape s;
                float at = x;
                for (const int g : run.glyphs) {
                    f.outline(g, t.size, at, t.y, s.path);
                    at += f.advance(g) * t.size;
                }
                s.fill.kind   = Paint::Kind::Solid;
                s.fill.colour = t.colour;
                outline.shapes.push_back(std::move(s));
                c += drawingOps(outline, 0, 0, ink.paints);
            }
            x += run.width * t.size;
        }
    }
    return c;
}

// The file: its header, its objects, the cross-reference table and the trailer, whose ID is made from the bytes.
std::string fileBytes(const Writer& w, int catalog_n, int info_n, const std::string& title) {
    std::string out = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < w.objects.size(); ++i) {
        offsets.push_back(out.size());
        out += fmt::format("{} 0 obj\n", i + 1) + w.objects[i] + "\nendobj\n";
    }
    const auto id          = fmt::format("{:016x}{:016x}", std::hash<std::string>{}(out),
                                         std::hash<std::string>{}(title + out.substr(out.size() / 2)));
    const std::size_t xref = out.size();
    out += fmt::format("xref\n0 {}\n0000000000 65535 f \n", w.objects.size() + 1);
    for (const auto o : offsets) out += fmt::format("{:010} 00000 n \n", o);
    out += fmt::format("trailer\n<< /Size {} /Root {} 0 R /Info {} 0 R /ID [<{}> <{}>] >>\nstartxref\n{}\n%%EOF\n",
                       w.objects.size() + 1, catalog_n, info_n, id, id, xref);
    return out;
}

}  // namespace

std::string pdfBytes(const std::vector<Page>& pages, const PieceShapes* pieces, const PdfInfo& info) {
    const auto& fonts = Fonts::get();
    Writer w;
    const int catalog_n   = w.alloc();
    const int pages_n     = w.alloc();
    const int resources_n = w.alloc();

    auto uses           = fontUses(pages, fonts);
    const auto embedded = embedFonts(w, fonts, uses);
    Paints paints(w);
    const PageInk ink{pieces, fonts, uses, embedded, paints};

    // The pages: each one's content made first, the shared paints it needs made with it.
    std::vector<int> page_ns;
    for (const auto& page : pages) {
        const int content_n = w.stream(pageContent(page, ink));
        page_ns.push_back(
            w.add(fmt::format("<< /Type /Page /Parent {} 0 R /MediaBox [0 0 {} {}] /Resources {} 0 R "
                              "/Contents {} 0 R >>",
                              pages_n, num(page.width), num(page.height), resources_n, content_n)));
    }
    std::string kids;
    for (const int n : page_ns) kids += fmt::format("{} 0 R ", n);
    w.set(pages_n, fmt::format("<< /Type /Pages /Kids [{}] /Count {} >>", kids, page_ns.size()));
    w.set(resources_n, fmt::format("<< /ProcSet [/PDF /Text] /Font << {}>> /ExtGState << {}>> /Shading << {}>> >>",
                                   embedded.resources, paints.gsResources(), paints.shResources()));
    std::string lang;
    for (const char ch : info.lang)
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-') lang += ch;
    w.set(catalog_n, fmt::format("<< /Type /Catalog /Pages {} 0 R /Lang ({}) /ViewerPreferences << /DisplayDocTitle "
                                 "true >> >>",
                                 pages_n, lang.empty() ? "en" : lang));
    std::string info_dict = "<< /Producer (RookStudio) /Creator (RookStudio)";
    if (!info.title.empty()) info_dict += " /Title " + infoText(info.title);
    if (!info.date.empty()) info_dict += " /CreationDate (" + info.date + ")";
    info_dict += " >>";
    const int info_n = w.add(info_dict);
    return fileBytes(w, catalog_n, info_n, info.title);
}

}  // namespace fcg::publish
