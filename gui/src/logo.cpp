#include "logo.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>

#include <stb_image_write.h>

namespace fcg::logo {

namespace {

// The rook on a 24 x 24 grid; on the tile it takes 24/36 of the width, centred.
constexpr float GRID     = 24.0f;
constexpr float GLYPH    = 24.0f / 36.0f;
constexpr float ROUNDING = 10.0f / 36.0f;

struct Rect {
    float x0, y0, x1, y1, r;
};
constexpr Rect BASE       = {5.6f, 19.4f, 18.4f, 22.0f, 1.0f};
constexpr Rect RING       = {6.4f, 7.2f, 17.6f, 9.8f, 0.6f};  // under the battlements
constexpr Rect MERLONS[3] = {
    {6.4f, 2.6f, 9.0f, 7.6f, 0.5f}, {10.7f, 2.6f, 13.3f, 7.6f, 0.5f}, {15.0f, 2.6f, 17.6f, 7.6f, 0.5f}};
// The tower, a little wider at its foot.
constexpr ImVec2 BODY[4] = {{8.2f, 9.6f}, {15.8f, 9.6f}, {16.8f, 19.6f}, {7.2f, 19.6f}};

bool inRect(const Rect& b, float x, float y) {
    if (x < b.x0 || x > b.x1 || y < b.y0 || y > b.y1) return false;
    const float cx = std::clamp(x, b.x0 + b.r, b.x1 - b.r);
    const float cy = std::clamp(y, b.y0 + b.r, b.y1 - b.r);
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= b.r * b.r;
}

bool inBody(float x, float y) {
    if (y < BODY[0].y || y > BODY[2].y) return false;
    const float t = (y - BODY[0].y) / (BODY[2].y - BODY[0].y);
    return x >= BODY[0].x + t * (BODY[3].x - BODY[0].x) && x <= BODY[1].x + t * (BODY[2].x - BODY[1].x);
}

// (x, y) on the grid.
bool inGlyph(float x, float y) {
    return inRect(BASE, x, y) || inRect(RING, x, y) || inBody(x, y) ||
           std::any_of(std::begin(MERLONS), std::end(MERLONS), [&](const Rect& m) { return inRect(m, x, y); });
}

void append(std::vector<uint8_t>& out, uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

// Big-endian, as in an .icns file.
void appendBig(std::vector<uint8_t>& out, uint32_t value) {
    for (int i = 3; i >= 0; --i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

// `size` x `size` RGBA pixels as a PNG file.
std::vector<uint8_t> png(const std::vector<uint8_t>& pixels, int size) {
    std::vector<uint8_t> out;
    stbi_write_png_to_func(
        [](void* context, void* data, int n) {
            auto* to = static_cast<std::vector<uint8_t>*>(context);
            to->insert(to->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + n);
        },
        &out, size, size, 4, pixels.data(), size * 4);
    return out;
}

}  // namespace

void draw(ImDrawList* dl, ImVec2 min, float size, ImU32 tile, ImU32 glyph) {
    dl->AddRectFilled(min, ImVec2(min.x + size, min.y + size), tile, size * ROUNDING);
    const float k = size * GLYPH / GRID;
    const ImVec2 o(min.x + size * (1 - GLYPH) / 2, min.y + size * (1 - GLYPH) / 2);
    const auto at   = [&](float x, float y) { return ImVec2(o.x + x * k, o.y + y * k); };
    const auto rect = [&](const Rect& b) { dl->AddRectFilled(at(b.x0, b.y0), at(b.x1, b.y1), glyph, b.r * k); };

    for (const auto& m : MERLONS) rect(m);
    rect(RING);
    const ImVec2 body[4] = {at(BODY[0].x, BODY[0].y), at(BODY[1].x, BODY[1].y), at(BODY[2].x, BODY[2].y),
                            at(BODY[3].x, BODY[3].y)};
    dl->AddConvexPolyFilled(body, 4, glyph);
    rect(BASE);
}

std::vector<uint8_t> icon(int size) {
    constexpr int SAMPLES   = 4;  // per side, per pixel
    constexpr float TILE[3] = {47, 111, 222};
    const float radius      = size * ROUNDING;
    const float k           = size * GLYPH / GRID;
    const float offset      = size * (1 - GLYPH) / 2;
    const Rect tile         = {0, 0, static_cast<float>(size), static_cast<float>(size), radius};

    std::vector<uint8_t> out(static_cast<std::size_t>(size) * size * 4);
    for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
            float covered = 0, white = 0;
            for (int sy = 0; sy < SAMPLES; ++sy) {
                for (int sx = 0; sx < SAMPLES; ++sx) {
                    const float x = px + (sx + 0.5f) / SAMPLES, y = py + (sy + 0.5f) / SAMPLES;
                    if (!inRect(tile, x, y)) continue;
                    covered += 1;
                    if (inGlyph((x - offset) / k, (y - offset) / k)) white += 1;
                }
            }
            auto* p = &out[(static_cast<std::size_t>(py) * size + px) * 4];
            if (covered == 0) continue;  // transparent
            const float w = white / covered;
            for (int c = 0; c < 3; ++c) p[c] = static_cast<uint8_t>(std::lround(TILE[c] + w * (255 - TILE[c])));
            p[3] = static_cast<uint8_t>(std::lround(255 * covered / (SAMPLES * SAMPLES)));
        }
    }
    return out;
}

std::vector<uint8_t> icoFile() {
    constexpr int SIZES[] = {16, 20, 24, 32, 40, 48, 64, 256};
    constexpr int COUNT   = static_cast<int>(sizeof(SIZES) / sizeof(SIZES[0]));
    std::vector<std::vector<uint8_t>> images;
    for (const int size : SIZES) images.push_back(png(icon(size), size));

    // ICONDIR, one ICONDIRENTRY per image, then the images.
    std::vector<uint8_t> out;
    append(out, 0, 2);
    append(out, 1, 2);  // an icon
    append(out, COUNT, 2);
    uint32_t at = 6 + 16 * COUNT;
    for (int i = 0; i < COUNT; ++i) {
        append(out, SIZES[i] % 256, 1);  // 0 means 256
        append(out, SIZES[i] % 256, 1);
        append(out, 0, 1);   // no palette
        append(out, 0, 1);   // reserved
        append(out, 1, 2);   // colour planes
        append(out, 32, 2);  // bits per pixel
        append(out, static_cast<uint32_t>(images[i].size()), 4);
        append(out, at, 4);
        at += static_cast<uint32_t>(images[i].size());
    }
    for (const auto& image : images) out.insert(out.end(), image.begin(), image.end());
    return out;
}

std::vector<uint8_t> icnsFile() {
    // Each image by its type: 16 to 512 points, at one and two pixels per point.
    constexpr struct {
        const char* type;
        int size;
    } IMAGES[] = {{"icp4", 16},  {"icp5", 32},  {"ic11", 32},  {"ic12", 64},  {"ic07", 128},
                  {"ic13", 256}, {"ic08", 256}, {"ic14", 512}, {"ic09", 512}, {"ic10", 1024}};

    std::map<int, std::vector<uint8_t>> pngs;  // by size, each drawn once
    std::vector<uint8_t> body;
    for (const auto& image : IMAGES) {
        auto& encoded = pngs[image.size];
        if (encoded.empty()) {
            // The tile on macOS's icon grid: 824 of 1024 pixels, centred, as the system's own icons.
            const int size   = image.size;
            const int tile   = static_cast<int>(std::lround(size * 824.0 / 1024.0));
            const int at     = (size - tile) / 2;
            const auto drawn = icon(tile);
            std::vector<uint8_t> pixels(static_cast<std::size_t>(size) * size * 4);
            for (int y = 0; y < tile; ++y) {
                std::copy_n(&drawn[static_cast<std::size_t>(y) * tile * 4], static_cast<std::size_t>(tile) * 4,
                            &pixels[(static_cast<std::size_t>(y + at) * size + at) * 4]);
            }
            encoded = png(pixels, size);
        }
        body.insert(body.end(), image.type, image.type + 4);
        appendBig(body, static_cast<uint32_t>(8 + encoded.size()));
        body.insert(body.end(), encoded.begin(), encoded.end());
    }

    // "icns", the file's length, then the images.
    std::vector<uint8_t> out = {'i', 'c', 'n', 's'};
    appendBig(out, static_cast<uint32_t>(8 + body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

}  // namespace fcg::logo
