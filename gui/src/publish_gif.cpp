#include "publish_gif.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include <study/game_tree.hpp>

#include "board_text.hpp"

namespace fcg::publish {

namespace {

constexpr Rgba BAND_BG{43, 48, 56, 255};
constexpr Rgba BAND_TEXT{255, 255, 255, 255};

uint32_t key24(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 16 | static_cast<uint32_t>(p[1]) << 8 | p[2]; }

void put16le(std::string& out, int v) {
    out += static_cast<char>(v & 0xff);
    out += static_cast<char>(v >> 8 & 0xff);
}

// The pixels' indexes as GIF's LZW codes them, in sub-blocks.
void lzw(std::string& out, const std::vector<uint8_t>& px) {
    constexpr int MIN_SIZE = 8, CLEAR = 256, END = 257;
    out += static_cast<char>(MIN_SIZE);
    std::string block;
    uint32_t bits = 0;
    int nbits = 0, size = MIN_SIZE + 1, next = END + 1;
    std::unordered_map<uint32_t, int> dict;
    dict.reserve(8192);
    const auto emit = [&](int code) {
        bits |= static_cast<uint32_t>(code) << nbits;
        nbits += size;
        while (nbits >= 8) {
            block += static_cast<char>(bits & 0xff);
            bits >>= 8;
            nbits -= 8;
            if (block.size() == 255) {
                out += static_cast<char>(255);
                out += block;
                block.clear();
            }
        }
        if (next >= (1 << size) && size < 12) ++size;
    };
    emit(CLEAR);
    if (!px.empty()) {
        int prefix = px[0];
        for (std::size_t i = 1; i < px.size(); ++i) {
            const uint32_t key = static_cast<uint32_t>(prefix) << 8 | px[i];
            if (const auto it = dict.find(key); it != dict.end()) {
                prefix = it->second;
                continue;
            }
            emit(prefix);
            prefix = px[i];
            if (next >= 4095) {
                emit(CLEAR);
                next = END + 1;
                size = MIN_SIZE + 1;
                dict.clear();
            } else {
                dict[key] = next++;
            }
        }
        emit(prefix);
    }
    emit(END);
    if (nbits > 0) {
        block += static_cast<char>(bits & 0xff);
        if (block.size() == 255) {
            out += static_cast<char>(255);
            out += block;
            block.clear();
        }
    }
    if (!block.empty()) {
        out += static_cast<char>(block.size());
        out += block;
    }
    out += '\0';
}

}  // namespace

std::vector<Rgba> gifPalette(const std::vector<const Image*>& images, const std::vector<Rgba>& keep) {
    std::vector<Rgba> palette;
    for (const auto& k : keep)
        if (palette.size() < 256 && std::find(palette.begin(), palette.end(), k) == palette.end()) palette.push_back(k);
    std::unordered_map<uint32_t, uint32_t> counts;
    for (const auto* image : images)
        for (std::size_t i = 0; i + 3 < image->rgba.size(); i += 4) ++counts[key24(&image->rgba[i])];
    for (const auto& k : palette)
        counts.erase(static_cast<uint32_t>(k.r) << 16 | static_cast<uint32_t>(k.g) << 8 | k.b);
    struct Entry {
        uint8_t c[3];
        uint32_t n;
    };
    std::vector<Entry> entries;
    entries.reserve(counts.size());
    for (const auto& [rgb, n] : counts)
        entries.push_back(
            {{static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8 & 0xff), static_cast<uint8_t>(rgb & 0xff)},
             n});
    const std::size_t room = 256 - palette.size();
    struct Box {
        std::size_t begin, end;
    };
    std::vector<Box> boxes;
    if (!entries.empty()) boxes.push_back({0, entries.size()});
    const auto measure = [&](const Box& b, int& axis) {
        uint8_t lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
        uint64_t n = 0;
        for (std::size_t i = b.begin; i < b.end; ++i) {
            for (int k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], entries[i].c[k]);
                hi[k] = std::max(hi[k], entries[i].c[k]);
            }
            n += entries[i].n;
        }
        axis      = 0;
        int range = 0;
        for (int k = 0; k < 3; ++k)
            if (hi[k] - lo[k] > range) {
                range = hi[k] - lo[k];
                axis  = k;
            }
        return static_cast<double>(range) * std::sqrt(static_cast<double>(n));
    };
    while (boxes.size() < room) {
        int best = -1, axis = 0;
        double score = 0;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            if (boxes[i].end - boxes[i].begin < 2) continue;
            int a          = 0;
            const double s = measure(boxes[i], a);
            if (s > score) {
                score = s;
                best  = static_cast<int>(i);
                axis  = a;
            }
        }
        if (best < 0) break;
        auto& b = boxes[static_cast<std::size_t>(best)];
        std::sort(entries.begin() + static_cast<std::ptrdiff_t>(b.begin),
                  entries.begin() + static_cast<std::ptrdiff_t>(b.end),
                  [axis](const Entry& x, const Entry& y) { return x.c[axis] < y.c[axis]; });
        uint64_t total = 0, half = 0;
        for (std::size_t i = b.begin; i < b.end; ++i) total += entries[i].n;
        std::size_t cut = b.begin + 1;
        for (std::size_t i = b.begin; i < b.end - 1; ++i) {
            half += entries[i].n;
            cut = i + 1;
            if (half * 2 >= total) break;
        }
        const Box second{cut, b.end};
        b.end = cut;
        boxes.push_back(second);
    }
    for (const auto& b : boxes) {
        double sum[3] = {0, 0, 0}, n = 0;
        for (std::size_t i = b.begin; i < b.end; ++i) {
            for (int k = 0; k < 3; ++k) sum[k] += static_cast<double>(entries[i].c[k]) * entries[i].n;
            n += entries[i].n;
        }
        if (n <= 0) continue;
        palette.push_back({static_cast<uint8_t>(std::lround(sum[0] / n)), static_cast<uint8_t>(std::lround(sum[1] / n)),
                           static_cast<uint8_t>(std::lround(sum[2] / n)), 255});
    }
    while (palette.size() < 256) palette.push_back({0, 0, 0, 255});
    palette.resize(256);
    return palette;
}

GifWriter::GifWriter(int width, int height, const std::vector<Rgba>& palette)
    : width_(width), height_(height), palette_(palette), cache_(1 << 18, -1) {
    palette_.resize(256);
    out_ = "GIF89a";
    put16le(out_, width_);
    put16le(out_, height_);
    out_ += static_cast<char>(0xF7);  // a global table of 256 colours
    out_ += '\0';
    out_ += '\0';
    for (const auto& c : palette_) {
        out_ += static_cast<char>(c.r);
        out_ += static_cast<char>(c.g);
        out_ += static_cast<char>(c.b);
    }
    // Played again and again.
    out_ += "\x21\xFF\x0BNETSCAPE2.0\x03\x01";
    out_ += '\0';
    out_ += '\0';
    out_ += '\0';
}

uint8_t GifWriter::index(const uint8_t* rgb) {
    const int key = (rgb[0] >> 2) << 12 | (rgb[1] >> 2) << 6 | (rgb[2] >> 2);
    int& cached   = cache_[static_cast<std::size_t>(key)];
    if (cached >= 0) return static_cast<uint8_t>(cached);
    int best = 0, distance = 1 << 30;
    for (int i = 0; i < 256; ++i) {
        const int dr = rgb[0] - palette_[static_cast<std::size_t>(i)].r,
                  dg = rgb[1] - palette_[static_cast<std::size_t>(i)].g,
                  db = rgb[2] - palette_[static_cast<std::size_t>(i)].b;
        const int d  = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
        if (d < distance) {
            distance = d;
            best     = i;
        }
    }
    cached = best;
    return static_cast<uint8_t>(best);
}

void GifWriter::frame(const Image& image, int delay_ms) {
    std::vector<uint8_t> indexes(static_cast<std::size_t>(width_) * height_, 0);
    for (int y = 0; y < height_ && y < image.height; ++y)
        for (int x = 0; x < width_ && x < image.width; ++x)
            indexes[static_cast<std::size_t>(y) * width_ + x] =
                index(&image.rgba[(static_cast<std::size_t>(y) * image.width + x) * 4]);
    // Only the rectangle that changed since the frame before.
    int x0 = 0, y0 = 0, x1 = width_ - 1, y1 = height_ - 1;
    if (!previous_.empty()) {
        x0 = width_;
        y0 = height_;
        x1 = -1;
        y1 = -1;
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * width_ + x;
                if (indexes[i] == previous_[i]) continue;
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
        if (x1 < 0) x0 = y0 = x1 = y1 = 0;  // nothing: a pixel, for its delay
    }
    const int cs = std::clamp((delay_ms + 5) / 10, 2, 65535);
    out_ += "\x21\xF9\x04";
    out_ += static_cast<char>(1 << 2);  // left in place for the next frame
    put16le(out_, cs);
    out_ += '\0';
    out_ += '\0';
    out_ += static_cast<char>(0x2C);
    put16le(out_, x0);
    put16le(out_, y0);
    put16le(out_, x1 - x0 + 1);
    put16le(out_, y1 - y0 + 1);
    out_ += '\0';
    std::vector<uint8_t> rect;
    rect.reserve(static_cast<std::size_t>(x1 - x0 + 1) * (y1 - y0 + 1));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) rect.push_back(indexes[static_cast<std::size_t>(y) * width_ + x]);
    lzw(out_, rect);
    previous_ = std::move(indexes);
}

std::string GifWriter::finish() {
    out_ += static_cast<char>(0x3B);
    return std::move(out_);
}

// ---------------------------------------------------------------------------------------------------------------------

std::pair<int, int> gifRange(const GifGame& game, const GifOptions& options) {
    const int last = static_cast<int>(game.game.moves.size());
    const int to   = options.to < 0 ? last : std::clamp(options.to, 0, last);
    const int from = std::clamp(options.from, 0, to);
    return {from, to};
}

namespace {

int boardPixels(const GifOptions& options) { return std::max(8, std::clamp(options.size, 120, 960) / 8 * 8); }
int bandPixels(int board) { return std::max(14, static_cast<int>(std::lround(board * 0.075))); }

std::string moveLabel(const ReplayGame& game, bool chess960, int ply) {
    if (ply <= 0 || ply > static_cast<int>(game.moves.size())) return {};
    const auto board = boardAt(game, chess960, ply - 1);
    const auto& m    = game.moves[static_cast<std::size_t>(ply - 1)];
    return std::to_string(board.fullMoveNumber()) + (m.mover == chess::Color::WHITE ? "." : "…") + localSan(m.san);
}

void band(Image& image, int y, int h, const std::string& text) {
    for (int row = y; row < y + h; ++row)
        for (int x = 0; x < image.width; ++x) {
            uint8_t* p = &image.rgba[(static_cast<std::size_t>(row) * image.width + x) * 4];
            p[0]       = BAND_BG.r;
            p[1]       = BAND_BG.g;
            p[2]       = BAND_BG.b;
            p[3]       = 255;
        }
    if (text.empty()) return;
    float em         = h * 0.5f;
    const float room = image.width * 0.94f;
    if (const float w = textWidth(text, FontId::SemiBold, em); w > room) em *= room / w;
    const float w = textWidth(text, FontId::SemiBold, em);
    Drawing d;
    d.width       = static_cast<float>(image.width);
    d.height      = static_cast<float>(h);
    const auto& f = Fonts::get().font(FontId::SemiBold);
    d.text(text, FontId::SemiBold, em, (image.width - w) / 2, h / 2.0f + (f.ascent() + f.descent()) * em / 2,
           BAND_TEXT);
    image.blend(rasterize(d), 0, y);
}

}  // namespace

Image gifFrame(const GifGame& game, const GifOptions& options, int ply) {
    const int board  = boardPixels(options);
    auto spec        = diagramAt(game.game, game.chess960, ply, &game.start_notes, game.colours);
    spec.flipped     = game.flipped;
    spec.coordinates = false;
    if (!options.last_move) spec.last_move.reset();
    if (!options.arrows) {
        spec.arrows.clear();
        spec.squares.clear();
    }
    const Image squares = game.pieces ? diagramImage(spec, *game.pieces, board) : Image{};
    if (!options.banner) return squares;
    const int h = bandPixels(board);
    Image image;
    image.width  = board;
    image.height = board + 2 * h;
    image.fill({255, 255, 255, 255});
    band(image, 0, h, game.banner);
    image.blend(squares, 0, h);
    band(image, h + board, h, moveLabel(game.game, game.chess960, ply));
    return image;
}

namespace {

std::vector<Rgba> keptColours(const GifGame& game) {
    const auto light = rgba(game.colours.light), dark = rgba(game.colours.dark);
    const auto hl   = rgba(game.colours.highlight);
    const auto over = [&](Rgba base) {
        const int a = hl.a;
        return Rgba{static_cast<uint8_t>((hl.r * a + base.r * (255 - a)) / 255),
                    static_cast<uint8_t>((hl.g * a + base.g * (255 - a)) / 255),
                    static_cast<uint8_t>((hl.b * a + base.b * (255 - a)) / 255), 255};
    };
    return {light, dark, over(light), over(dark), BAND_BG, BAND_TEXT, {0, 0, 0, 255}, {255, 255, 255, 255}};
}

}  // namespace

std::string makeGif(const GifGame& game, const GifOptions& options, GifProgress* progress,
                    const std::atomic<bool>* cancel) {
    const auto [from, to] = gifRange(game, options);
    if (progress) {
        progress->total = to - from + 1;
        progress->done  = 0;
    }
    // The palette from the first, the last and a few frames between.
    std::vector<Image> samples;
    for (int ply = from; ply <= to; ply += std::max(1, (to - from) / 6))
        samples.push_back(gifFrame(game, options, ply));
    samples.push_back(gifFrame(game, options, to));
    std::vector<const Image*> pointers;
    for (const auto& s : samples) pointers.push_back(&s);
    GifWriter writer(samples.front().width, samples.front().height, gifPalette(pointers, keptColours(game)));
    for (int ply = from; ply <= to; ++ply) {
        if (cancel && *cancel) return {};
        writer.frame(gifFrame(game, options, ply), ply == to ? options.last_ms : options.delay_ms);
        if (progress) ++progress->done;
    }
    return writer.finish();
}

uint64_t gifEstimate(const GifGame& game, const GifOptions& options) {
    const auto [from, to] = gifRange(game, options);
    const int frames      = to - from + 1;
    const int sampled     = std::min(frames, 6);
    std::vector<Image> images;
    for (int i = 0; i < sampled; ++i) images.push_back(gifFrame(game, options, from + i));
    std::vector<const Image*> pointers;
    for (const auto& s : images) pointers.push_back(&s);
    GifWriter writer(images.front().width, images.front().height, gifPalette(pointers, keptColours(game)));
    writer.frame(images.front(), options.delay_ms);
    const std::size_t first = writer.size();
    for (int i = 1; i < sampled; ++i) writer.frame(images[static_cast<std::size_t>(i)], options.delay_ms);
    const double each = sampled > 1 ? static_cast<double>(writer.size() - first) / (sampled - 1) : 0.0;
    return static_cast<uint64_t>(static_cast<double>(first) + each * (frames - 1) + 1);
}

}  // namespace fcg::publish
