#include "publish_doc.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include <SDL3/SDL_locale.h>
#include <SDL3/SDL_stdinc.h>

#include <fmt/include/fmt/format.h>

#include "i18n.hpp"

namespace fcg::publish {

Block title(std::string text) {
    Block b;
    b.kind = Block::Kind::Title;
    b.spans.push_back({std::move(text), FontId::SemiBold});
    return b;
}

Block subtitle(std::string text) {
    Block b;
    b.kind = Block::Kind::Subtitle;
    b.spans.push_back({std::move(text), FontId::Regular, MUTED_COLOUR});
    return b;
}

Block heading(std::string text) {
    Block b;
    b.kind           = Block::Kind::Heading;
    b.keep_with_next = true;
    b.spans.push_back({std::move(text), FontId::SemiBold});
    return b;
}

Block paragraph(std::string text, Rgba colour, FontId font) {
    Block b;
    b.spans.push_back({std::move(text), font, colour});
    return b;
}

Block note(std::string text) {
    Block b;
    b.kind = Block::Kind::Note;
    b.spans.push_back({std::move(text), FontId::Regular, MUTED_COLOUR});
    return b;
}

Block space(float points) {
    Block b;
    b.kind  = Block::Kind::Space;
    b.space = points;
    return b;
}

std::string printable(const std::string& text) {
    const auto& fonts                                    = Fonts::get();
    static const std::map<char32_t, const char*> instead = {{U'⩲', "+="}, {U'⩱', "=+"}, {U'⨀', "(.)"}, {U'□', "[]"},
                                                            {U'∞', "oo"}, {U'−', "-"},  {U'…', "..."}, {U'⇅', "^v"},
                                                            {U'‹', "<"},  {U'›', ">"},  {U'●', "*"},   {U'·', "."}};
    std::u32string out;
    bool changed = false;
    for (const char32_t c : utf32(text)) {
        if (c < 0x80 || fonts.font(FontId::Regular).glyph(c) != 0 ||
            (c > 0x2000 && fonts.cjk() && fonts.font(FontId::Cjk).glyph(c) != 0)) {
            out += c;
            continue;
        }
        changed = true;
        if (const auto it = instead.find(c); it != instead.end())
            out += utf32(it->second);
        else
            out += U'?';
    }
    return changed ? utf8(out) : text;
}

namespace {

constexpr float MARGIN = 42;  // 15 mm
constexpr float FOOT   = 16;  // the page number's room
constexpr float GAP    = 18;  // between two columns

float fontWidth(const std::string& text, FontId font, float size) { return textWidth(printable(text), font, size); }

// The longest start of `text` that fits `width` with "…" after it; the text itself when it fits.
std::string fitted(const std::string& text, FontId font, float size, float width) {
    if (fontWidth(text, font, size) <= width) return text;
    auto chars = utf32(text);
    while (!chars.empty()) {
        chars.pop_back();
        const auto s = utf8(chars) + "…";
        if (fontWidth(s, font, size) <= width) return s;
    }
    return "…";
}

struct Layout {
    const Document& doc;
    std::vector<Page> pages;
    float pw = 0, ph = 0;
    float left = MARGIN, top = MARGIN, bottom = 0, width = 0;
    int col       = 0;
    float col_top = 0;  // where the columns start on this page (under a title)
    float y       = 0;
    std::vector<float> col_bottoms;  // how far each column of the page went

    explicit Layout(const Document& d) : doc(d) {
        pw = d.letter ? 612.0f : 595.28f;
        ph = d.letter ? 792.0f : 841.89f;
        if (d.landscape) std::swap(pw, ph);
        width  = pw - 2 * MARGIN;
        bottom = ph - MARGIN - FOOT;
        newPage();
    }

    [[nodiscard]] int columns() const { return std::max(1, doc.columns); }
    [[nodiscard]] float colWidth() const {
        return (width - GAP * static_cast<float>(columns() - 1)) / static_cast<float>(columns());
    }
    [[nodiscard]] float colLeft() const { return left + static_cast<float>(col) * (colWidth() + GAP); }
    Page& page() { return pages.back(); }

    void newPage() {
        Page p;
        p.width  = pw;
        p.height = ph;
        pages.push_back(std::move(p));
        col     = 0;
        col_top = top;
        y       = top;
        col_bottoms.assign(static_cast<std::size_t>(columns()), top);
        if (pages.size() > 1 && !doc.header.empty()) {
            page().texts.push_back({left, top - 14, FontId::Regular, 7.5f, MUTED_COLOUR,
                                    printable(fitted(doc.header, FontId::Regular, 7.5f, width))});
            page().lines.push_back({left, top - 9, left + width, top - 9, 0.4f, {200, 204, 210, 255}});
        }
    }
    void nextColumn() {
        col_bottoms[static_cast<std::size_t>(col)] = std::max(col_bottoms[static_cast<std::size_t>(col)], y);
        if (col + 1 < columns()) {
            ++col;
            y = col_top;
        } else {
            newPage();
        }
    }
    // Room for `h` more in this column, else the next.
    void need(float h) {
        if (y + h > bottom && y > col_top + 0.5f) nextColumn();
    }
    void text(float x, float baseline, const Span& span, float size, const std::string& t) {
        if (t.empty()) return;
        auto& texts = page().texts;
        texts.push_back({x, baseline, span.font, size, span.colour, printable(t)});
    }

    // A paragraph's spans broken into lines `w` wide from `x`.
    void spans(const std::vector<Span>& list, float size, float x, float w, float leading, bool full_width) {
        struct Atom {
            const Span* span;
            std::string text;
            float width;
            bool space_before;  // a space between it and the atom before
        };
        // Words: atoms without a space between them. A line may break before a word: after a space, or between two
        // Chinese characters, which have none.
        struct Word {
            std::vector<Atom> atoms;
            bool space_before = false;
        };
        std::vector<Word> words;
        bool space = false, fresh = true;
        for (const auto& s : list) {
            const float sz = size * s.scale;
            std::u32string piece;
            const auto flush = [&] {
                if (piece.empty()) return;
                const auto t = utf8(piece);
                if (fresh || words.empty()) words.push_back({{}, space && !words.empty()});
                words.back().atoms.push_back({&s, t, fontWidth(t, s.font, sz), false});
                space = fresh = false;
                piece.clear();
            };
            for (const char32_t c : utf32(s.text)) {
                if (c == U' ' || c == U'\n' || c == U'\t') {
                    flush();
                    space = fresh = true;
                    continue;
                }
                if (wideChar(c)) {
                    flush();
                    fresh = true;
                    piece += c;
                    flush();
                    fresh = true;
                    continue;
                }
                piece += c;
            }
            flush();
        }
        if (words.empty()) return;
        float line_x = 0;
        std::vector<std::pair<float, Atom>> line;  // its atoms, at their x
        float line_size = size;
        const auto emit = [&] {
            if (line.empty()) return;
            const float h = line_size * leading;
            if (!full_width) need(h);
            const float baseline = y + line_size * 1.0f;
            // Atoms of one look side by side make one text.
            std::size_t i = 0;
            while (i < line.size()) {
                const auto& [ax, a] = line[i];
                std::string t       = a.text;
                std::size_t j       = i + 1;
                for (; j < line.size(); ++j) {
                    const auto& b = line[j].second;
                    if (b.span->font != a.span->font || !(b.span->colour == a.span->colour) ||
                        b.span->scale != a.span->scale)
                        break;
                    t += (b.space_before ? " " : "") + b.text;
                }
                text((full_width ? x : colLeft()) + ax, baseline, *a.span, size * a.span->scale, t);
                i = j;
            }
            y += h;
            line.clear();
            line_x    = 0;
            line_size = size;
        };
        for (auto& word : words) {
            float ww = 0;
            for (const auto& a : word.atoms) ww += a.width;
            const auto* first = word.atoms.front().span;
            const bool spaced = !line.empty() && word.space_before;
            const float gap   = spaced ? fontWidth(" ", first->font, size * first->scale) : 0.0f;
            if (!line.empty() && line_x + gap + ww > w) emit();
            const bool still = !line.empty();
            float at         = still ? line_x + gap : 0;
            for (std::size_t k = 0; k < word.atoms.size(); ++k) {
                auto a         = word.atoms[k];
                a.space_before = k == 0 && still && word.space_before;
                line.emplace_back(at, a);
                at += a.width;
                line_size = std::max(line_size, size * a.span->scale);
            }
            line_x = at;
        }
        emit();
    }

    void table(const Table& t) {
        const float size = t.size, pad = 4, row_h = size * 1.6f;
        const std::size_t n = t.head.size();
        if (n == 0) return;
        std::vector<float> natural(n, 0);
        for (std::size_t c = 0; c < n; ++c) natural[c] = fontWidth(t.head[c], FontId::SemiBold, size) + 2 * pad;
        for (const auto& row : t.rows)
            for (std::size_t c = 0; c < n && c < row.size(); ++c)
                natural[c] = std::max(
                    natural[c], fontWidth(row[c].text, row[c].font, size) + 2 * pad + (row[c].bar >= 0 ? 40.0f : 0.0f));
        const float avail = colWidth();
        float sum         = 0;
        for (const auto v : natural) sum += v;
        std::vector<float> widths = natural;
        if (sum > avail) {
            // The widest columns cut down to a width that makes them fit.
            float lo = 0, hi = *std::max_element(natural.begin(), natural.end());
            for (int k = 0; k < 40; ++k) {
                const float cap = (lo + hi) / 2;
                float s         = 0;
                for (const auto v : natural) s += std::min(v, cap);
                (s > avail ? hi : lo) = cap;
            }
            for (auto& v : widths) v = std::min(v, lo);
        }
        const auto align = [&](std::size_t c) { return c < t.align.size() ? t.align[c] : 0; };
        const auto head  = [&] {
            float x = colLeft();
            for (std::size_t c = 0; c < n; ++c) {
                const auto s   = fitted(t.head[c], FontId::SemiBold, size, widths[c] - 2 * pad);
                const float w  = fontWidth(s, FontId::SemiBold, size);
                const float at = align(c) == 1   ? x + widths[c] - pad - w
                                 : align(c) == 2 ? x + (widths[c] - w) / 2
                                                 : x + pad;
                text(at, y + size * 1.15f, {"", FontId::SemiBold, MUTED_COLOUR}, size, s);
                x += widths[c];
            }
            y += row_h;
            page().lines.push_back(
                {colLeft(), y - 1, colLeft() + std::min(avail, sum), y - 1, 0.6f, {190, 195, 202, 255}});
        };
        need(row_h * 2);
        head();
        for (const auto& row : t.rows) {
            if (y + row_h > bottom) {
                nextColumn();
                head();
            }
            float x = colLeft();
            for (std::size_t c = 0; c < n && c < row.size(); ++c) {
                const auto& cell = row[c];
                if (cell.fill) page().rects.push_back({x, y, widths[c], row_h, *cell.fill});
                float room = widths[c] - 2 * pad;
                float at   = x + pad;
                if (cell.bar >= 0) {
                    const float bw = 36.0f * std::clamp(cell.bar, 0.0f, 1.0f);
                    page().rects.push_back(
                        {at, y + row_h * 0.3f, std::max(0.5f, bw), row_h * 0.4f, {150, 180, 235, 255}});
                    at += 40;
                    room -= 40;
                }
                const auto s  = fitted(cell.text, cell.font, size, std::max(0.0f, room));
                const float w = fontWidth(s, cell.font, size);
                if (align(c) == 1) at = x + widths[c] - pad - w;
                if (align(c) == 2) at = x + (widths[c] - w) / 2;
                text(at, y + size * 1.15f, {"", cell.font, cell.colour}, size, s);
                x += widths[c];
            }
            y += row_h;
            page().lines.push_back(
                {colLeft(), y - 0.5f, colLeft() + std::min(avail, sum), y - 0.5f, 0.3f, {225, 228, 233, 255}});
        }
        y += 4;
    }

    void diagram(const Block& b) {
        const float board   = b.diagram_size > 0 ? std::min(b.diagram_size, colWidth()) : std::min(colWidth(), 180.0f);
        const auto probe    = diagramLayers(b.diagram, board);
        const float caption = b.caption.empty() ? 0 : doc.size * 1.6f;
        need(probe.height + caption + 8);
        const float x = colLeft() + (colWidth() - probe.width) / 2;
        page().diagrams.push_back({x, y + 2, board, b.diagram});
        y += probe.height + 4;
        if (!b.caption.empty()) {
            const float size = doc.size * 0.88f;
            const auto s     = fitted(b.caption, FontId::Regular, size, colWidth());
            const float w    = fontWidth(s, FontId::Regular, size);
            text(colLeft() + (colWidth() - w) / 2, y + size, {"", FontId::Regular, MUTED_COLOUR}, size, s);
            y += caption;
        }
        y += 4;
    }

    void bars(const Block& b) {
        const float size = doc.size * 0.9f, row_h = size * 1.6f;
        float label_w = 0;
        for (const auto& bar : b.bars) label_w = std::max(label_w, fontWidth(bar.label, FontId::Regular, size));
        float value_w = 0;
        for (const auto& bar : b.bars) value_w = std::max(value_w, fontWidth(bar.value, FontId::SemiBold, size));
        const float room = std::max(20.0f, colWidth() - label_w - value_w - 18);
        for (const auto& bar : b.bars) {
            need(row_h);
            text(colLeft(), y + size * 1.15f, {"", FontId::Regular, MUTED_COLOUR}, size, bar.label);
            const float bx = colLeft() + label_w + 6, bw = room * std::clamp(bar.length, 0.0f, 1.0f);
            page().rects.push_back({bx, y + row_h * 0.22f, std::max(0.5f, bw), row_h * 0.56f, {150, 180, 235, 255}});
            text(bx + bw + 5, y + size * 1.15f, {"", FontId::SemiBold, TEXT_COLOUR}, size, bar.value);
            y += row_h;
        }
        y += 4;
    }

    void block(const Block& b, const Block* next) {
        const float size = doc.size;
        switch (b.kind) {
            case Block::Kind::Title:
            case Block::Kind::Subtitle: {
                // Across the page, above the columns.
                if (col != 0 || y > col_top + 0.5f) {
                    float lowest = y;
                    for (const auto v : col_bottoms) lowest = std::max(lowest, v);
                    if (columns() > 1) {
                        col = 0;
                        y   = lowest;
                    }
                }
                const float s = b.kind == Block::Kind::Title ? size * 1.5f : size;
                if (y + s * 1.4f > bottom) newPage();
                spans(b.spans, s, left, width, 1.35f, true);
                y += b.kind == Block::Kind::Title ? 2 : 6;
                col_top = y;
                std::fill(col_bottoms.begin(), col_bottoms.end(), y);
                break;
            }
            case Block::Kind::Heading: {
                const float s = size * 1.18f;
                float after   = s * 1.4f + 6;
                if (b.keep_with_next && next) after += size * 1.45f * 2;
                if (y > col_top + 0.5f) y += 6;
                need(after);
                spans(b.spans, s, colLeft(), colWidth(), 1.4f, false);
                y += 2;
                break;
            }
            case Block::Kind::Paragraph:
                spans(b.spans, size, colLeft(), colWidth(), 1.45f, false);
                y += size * 0.45f;
                break;
            case Block::Kind::Note:
                spans(b.spans, size * 0.85f, colLeft(), colWidth(), 1.4f, false);
                y += size * 0.35f;
                break;
            case Block::Kind::Table:
                table(b.table);
                break;
            case Block::Kind::Diagram:
                diagram(b);
                break;
            case Block::Kind::Bars:
                bars(b);
                break;
            case Block::Kind::Rule:
                need(8);
                page().lines.push_back({colLeft(), y + 3, colLeft() + colWidth(), y + 3, 0.5f, {200, 204, 210, 255}});
                y += 8;
                break;
            case Block::Kind::Space:
                y += b.space;
                if (y > bottom) nextColumn();
                break;
            case Block::Kind::PageBreak:
                newPage();
                break;
        }
        col_bottoms[static_cast<std::size_t>(col)] = std::max(col_bottoms[static_cast<std::size_t>(col)], y);
    }

    // The pieces' notice and their license, across the page under the rest.
    void notice() {
        if (doc.notice.line.empty()) return;
        float lowest = y;
        for (const auto v : col_bottoms) lowest = std::max(lowest, v);
        const float size = 5.6f;
        // The license's paragraphs, each a line made of its lines.
        std::vector<std::string> paragraphs;
        std::string current;
        std::size_t start          = 0;
        const std::string& license = doc.notice.license;
        while (start <= license.size()) {
            auto end = license.find('\n', start);
            if (end == std::string::npos) end = license.size();
            std::string line = license.substr(start, end - start);
            while (!line.empty() && line.front() == ' ') line.erase(line.begin());
            if (line.empty()) {
                if (!current.empty()) paragraphs.push_back(current);
                current.clear();
            } else {
                current += (current.empty() ? "" : " ") + line;
            }
            start = end + 1;
        }
        if (!current.empty()) paragraphs.push_back(current);
        // Its height, laid out once to know it.
        Layout probe(doc);
        probe.y = 0;
        probe.spans({{doc.notice.line, FontId::Regular, MUTED_COLOUR}}, 7, 0, width, 1.4f, true);
        for (const auto& p : paragraphs) probe.spans({{p, FontId::Regular, MUTED_COLOUR}}, size, 0, width, 1.3f, true);
        const float h = probe.y + 10;
        if (lowest + h > bottom) {
            newPage();
            lowest = y;
        }
        col = 0;
        y   = std::max(lowest + 6, bottom - h);
        page().lines.push_back({left, y, left + width, y, 0.4f, {210, 214, 219, 255}});
        y += 5;
        spans({{doc.notice.line, FontId::Regular, MUTED_COLOUR}}, 7, left, width, 1.4f, true);
        for (const auto& p : paragraphs) spans({{p, FontId::Regular, MUTED_COLOUR}}, size, left, width, 1.3f, true);
    }
};

}  // namespace

std::vector<Page> layOut(const Document& document) {
    Layout l(document);
    for (std::size_t i = 0; i < document.blocks.size(); ++i)
        l.block(document.blocks[i], i + 1 < document.blocks.size() ? &document.blocks[i + 1] : nullptr);
    l.notice();
    // The pages' numbers.
    const auto count = l.pages.size();
    for (std::size_t i = 0; i < count; ++i) {
        auto& p           = l.pages[i];
        const auto number = fmt::format("{} / {}", i + 1, count);
        const float w     = textWidth(number, FontId::Regular, 7.5f);
        p.texts.push_back({(p.width - w) / 2, p.height - MARGIN / 2 - 2, FontId::Regular, 7.5f, MUTED_COLOUR, number});
    }
    return l.pages;
}

// ---------------------------------------------------------------------------------------------------------------------

namespace {

std::string spanHtml(const Span& s) {
    std::string text = htmlText(s.text);
    if (s.font == FontId::SemiBold) text = "<b>" + text + "</b>";
    if (s.font == FontId::Italic) text = "<i>" + text + "</i>";
    if (s.game >= 0 && s.node >= 0)
        return fmt::format("<span class=\"mv\" id=\"m{}-{}\" data-g=\"{}\" data-n=\"{}\">{}</span>", s.game, s.node,
                           s.game, s.node, text);
    if (!s.cls.empty()) return "<span class=\"" + s.cls + "\">" + text + "</span>";
    if (!(s.colour == TEXT_COLOUR))
        return fmt::format("<span style=\"color:#{:02x}{:02x}{:02x}\">{}</span>", s.colour.r, s.colour.g, s.colour.b,
                           text);
    return text;
}

std::string spansHtml(const std::vector<Span>& spans) {
    std::string out;
    for (const auto& s : spans) out += spanHtml(s);
    return out;
}

// A diagram as inline SVG, without the namespace an HTML page does not need.
std::string inlineDiagram(const DiagramSpec& spec, const PieceShapes& pieces) {
    auto svg = diagramSvg(spec, pieces, false, 400);
    if (const auto at = svg.find("<svg"); at != std::string::npos) svg = svg.substr(at);
    if (const auto ns = svg.find(" xmlns=\"http://www.w3.org/2000/svg\""); ns != std::string::npos)
        svg.erase(ns, std::string(" xmlns=\"http://www.w3.org/2000/svg\"").size());
    return svg;
}

}  // namespace

std::string documentHtml(const Document& doc) {
    std::string out  = pageStart(doc.title, doc.lang, boardStyles(doc.colours));
    const bool board = !doc.games.empty();
    out += "<main class=\"doc\"><div class=\"text\">\n";
    for (const auto& b : doc.blocks) {
        switch (b.kind) {
            case Block::Kind::Title:
                out += "<h1>" + spansHtml(b.spans) + "</h1>\n";
                break;
            case Block::Kind::Subtitle:
                out += "<p class=\"sub\">" + spansHtml(b.spans) + "</p>\n";
                break;
            case Block::Kind::Heading:
                out += "<h2>" + spansHtml(b.spans) + "</h2>\n";
                break;
            case Block::Kind::Paragraph:
                out += "<p>" + spansHtml(b.spans) + "</p>\n";
                break;
            case Block::Kind::Note:
                out += "<p class=\"hint\">" + spansHtml(b.spans) + "</p>\n";
                break;
            case Block::Kind::Table: {
                const auto& t  = b.table;
                const auto cls = [&](std::size_t c) {
                    const int a = c < t.align.size() ? t.align[c] : 0;
                    return a == 1 ? " class=\"n\"" : a == 2 ? " class=\"c\"" : "";
                };
                out += "<div class=\"wrap\"><table><thead><tr>";
                for (std::size_t c = 0; c < t.head.size(); ++c)
                    out += std::string("<th") + cls(c) + ">" + htmlText(t.head[c]) + "</th>";
                out += "</tr></thead><tbody>";
                for (const auto& row : t.rows) {
                    out += "<tr>";
                    for (std::size_t c = 0; c < row.size(); ++c) {
                        const auto& cell = row[c];
                        std::string style;
                        if (cell.fill)
                            style = fmt::format(" style=\"background:#{:02x}{:02x}{:02x}\"", cell.fill->r, cell.fill->g,
                                                cell.fill->b);
                        std::string text = htmlText(cell.text);
                        if (cell.font == FontId::SemiBold) text = "<b>" + text + "</b>";
                        if (!(cell.colour == TEXT_COLOUR))
                            text = fmt::format("<span style=\"color:#{:02x}{:02x}{:02x}\">{}</span>", cell.colour.r,
                                               cell.colour.g, cell.colour.b, text);
                        if (cell.bar >= 0)
                            text = fmt::format("<span class=\"bar\" style=\"width:{}px\"></span> ",
                                               static_cast<int>(std::lround(60 * std::clamp(cell.bar, 0.0f, 1.0f)))) +
                                   text;
                        out += std::string("<td") + cls(c) + style + ">" + text + "</td>";
                    }
                    out += "</tr>";
                }
                out += "</tbody></table></div>\n";
                break;
            }
            case Block::Kind::Diagram:
                if (doc.pieces)
                    out += "<div class=\"diagram\">" + inlineDiagram(b.diagram, *doc.pieces) +
                           (b.caption.empty() ? "" : "<div>" + htmlText(b.caption) + "</div>") + "</div>\n";
                break;
            case Block::Kind::Bars: {
                out += "<table>";
                for (const auto& bar : b.bars)
                    out += fmt::format(
                        "<tr><td>{}</td><td><span class=\"bar\" style=\"width:{}px\"></span> "
                        "<b>{}</b></td></tr>",
                        htmlText(bar.label), static_cast<int>(std::lround(240 * std::clamp(bar.length, 0.0f, 1.0f))),
                        htmlText(bar.value));
                out += "</table>\n";
                break;
            }
            case Block::Kind::Rule:
                out += "<hr>\n";
                break;
            case Block::Kind::Space:
            case Block::Kind::PageBreak:
                break;
        }
    }
    out += "</div>";
    if (board) out += "<div class=\"side\">" + boardWidget(false) + "</div>";
    out += "</main>\n";
    HtmlLook look;
    look.pieces_notice  = doc.notice.line;
    look.pieces_license = doc.notice.license;
    out += noticeHtml(look);
    if (doc.pieces && board) out += pieceSymbols(*doc.pieces);
    if (board) {
        HtmlOptions options;
        out += "<script>\nvar MARKS=true;\nvar GAMES=[";
        for (std::size_t i = 0; i < doc.games.size(); ++i)
            out += (i ? ",\n" : "") + gameData(doc.games[i], options, static_cast<int>(i));
        out += "];\n" + boardScript() + "</script>\n";
    }
    out += "</body>\n</html>\n";
    return out;
}

bool systemLetter() {
    static const char* letter[] = {"US", "CA", "MX", "PH", "CL", "CO", "VE", "GT",
                                   "PR", "DO", "CR", "PA", "SV", "HN", "NI", "BZ"};
    int count                   = 0;
    SDL_Locale** locales        = SDL_GetPreferredLocales(&count);
    bool out                    = false;
    if (locales && count > 0 && locales[0] && locales[0]->country) {
        const std::string country = locales[0]->country;
        out = std::find_if(std::begin(letter), std::end(letter), [&](const char* c) { return country == c; }) !=
              std::end(letter);
    }
    SDL_free(locales);
    return out;
}

}  // namespace fcg::publish
