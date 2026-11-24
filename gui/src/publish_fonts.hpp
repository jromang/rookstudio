#pragma once

// The fonts of what RookStudio publishes (#93): Inter, the program's own (embedded), and the system's font of Chinese
// characters (ui::cjkFontFile()) when a text needs it. Their metrics lay text out on a PDF's pages, their glyphs'
// outlines draw the diagrams' coordinates and captions and the GIF's banner, and their files, cut down to the glyphs a
// PDF uses, are embedded in it: TrueType's glyf outlines, or a CFF's charstrings (Noto Sans CJK, Source Han).

#include <cstdint>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fcg::publish {

// A path in a drawing's coordinates, y downwards: moves, lines, cubic curves, closes.
struct PathCmd {
    enum Op : uint8_t { Move, Line, Cubic, Close };
    Op op      = Move;
    float x[3] = {0, 0, 0}, y[3] = {0, 0, 0};  // a curve's two control points then its end; the point otherwise
};
using Path = std::vector<PathCmd>;

enum class FontId : uint8_t { Regular, SemiBold, Italic, Cjk };
inline constexpr int FONT_COUNT = 4;

class Font {
   public:
    Font();
    ~Font();
    Font(const Font&)            = delete;
    Font& operator=(const Font&) = delete;

    // The font `index` of a file (a collection holds several), its bytes kept by the caller for as long as the font.
    bool init(std::span<const uint8_t> data, int index);
    [[nodiscard]] bool ready() const noexcept { return ready_; }

    [[nodiscard]] int glyph(char32_t c) const;                         // 0 (.notdef) when it has none
    [[nodiscard]] float advance(int glyph) const;                      // in ems
    [[nodiscard]] float ascent() const noexcept { return ascent_; }    // in ems, above the baseline
    [[nodiscard]] float descent() const noexcept { return descent_; }  // in ems, negative
    // A glyph's outline, `em` units wide to an em, its origin at (x, y) on the baseline, appended to `path`.
    void outline(int glyph, float em, float x, float y, Path& path) const;

    // For a PDF: whether its outlines are a CFF's, its glyphs' numbers there (a CID-keyed CFF's CIDs, else the glyphs'
    // own), its PostScript name, and what its descriptor says (in thousandths of an em).
    [[nodiscard]] bool cff() const noexcept { return cff_; }
    [[nodiscard]] bool cidKeyed() const noexcept { return !cid_of_.empty(); }
    [[nodiscard]] int cid(int glyph) const;
    [[nodiscard]] const std::string& postscriptName() const noexcept { return ps_name_; }
    [[nodiscard]] const std::string& ros() const noexcept { return ros_; }  // a CID-keyed CFF's "Adobe Identity 0"
    struct Descriptor {
        int bbox[4]        = {0, -250, 1000, 1000};
        float italic_angle = 0;
        int ascent = 800, descent = -200, cap_height = 700;
        bool fixed = false;
        int weight = 400;
    };
    [[nodiscard]] const Descriptor& descriptor() const noexcept { return descriptor_; }
    // The font's program cut down to `glyphs` (and what they are made of): a TrueType font of its glyf tables for a
    // PDF's FontFile2, or a bare CFF for FontFile3 /CIDFontType0C. Glyph numbers are kept: those not wanted are
    // emptied. Empty when the font cannot be cut.
    [[nodiscard]] std::string subset(const std::set<int>& glyphs) const;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool ready_ = false, cff_ = false;
    float ascent_ = 0.8f, descent_ = -0.2f;
    std::span<const uint8_t> data_;
    uint32_t offset_ = 0;           // of its table directory in the file
    std::vector<uint16_t> cid_of_;  // by glyph, a CID-keyed CFF's
    std::string ps_name_, ros_;
    Descriptor descriptor_;
};

// The fonts, made at their first use; the Chinese font read from the system the first time a text needs one of its
// characters. Thread-safe.
class Fonts {
   public:
    [[nodiscard]] static const Fonts& get();
    [[nodiscard]] const Font& font(FontId id) const;  // Inter's when the system has no Chinese font
    // Whether the font of Chinese characters is there: read from the system at the first call.
    [[nodiscard]] bool cjk() const;

   private:
    Fonts();
    Font fonts_[FONT_COUNT];
};

// A text cut into runs of one font: what the font asked for lacks is taken from the Chinese one when it has it.
struct GlyphRun {
    FontId font = FontId::Regular;
    std::vector<int> glyphs;
    std::vector<char32_t> chars;  // one a glyph
    float width = 0;              // in ems
};
[[nodiscard]] std::vector<GlyphRun> shapeText(std::string_view utf8, FontId font);
// Its width, `em` wide to an em.
[[nodiscard]] float textWidth(std::string_view utf8, FontId font, float em);
// Whether a character is Chinese or Japanese (a line may break between two of them).
[[nodiscard]] bool wideChar(char32_t c);

// The text as code points, an invalid byte read as U+FFFD; and back.
[[nodiscard]] std::u32string utf32(std::string_view utf8);
[[nodiscard]] std::string utf8(std::u32string_view text);

// The outlines of a line of text in `font` at `em`, its baseline's start at (x, y), appended to `path`; its width.
float textOutline(std::string_view utf8, FontId font, float em, float x, float y, Path& path);

}  // namespace fcg::publish
