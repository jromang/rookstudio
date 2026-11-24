#pragma once

// An animated GIF of a game (#93, mockup M13, board 6, point 14), made by RookStudio in the app's theme: its board
// drawn as its diagrams are (publish_draw.hpp), from one move to another, a frame a position, the last one held
// longer; the players' names and the result in a banner, the move under the board, the last move marked, the game's
// arrows. Each frame keeps only what changed since the one before; one palette for all, chosen from the frames by
// median cut, the board's own colours in it exactly. Nothing is sent anywhere.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <study/game.hpp>

#include "publish_draw.hpp"

namespace fcg::publish {

struct GifOptions {
    int size       = 360;   // the board, in pixels (the image is as wide)
    int delay_ms   = 1000;  // a frame
    int last_ms    = 3000;  // the last
    bool banner    = true;
    bool last_move = true;
    bool arrows    = false;  // the game's own arrows and squares, at their move
    int from = 0, to = -1;   // the positions shown, by ply; -1: the last
};

struct GifGame {
    ReplayGame game;
    bool chess960 = false;
    bool flipped  = false;
    MoveNotes start_notes;
    std::string banner;  // "Moreau – Lindqvist · ½-½"
    appearance::BoardColours colours;
    std::shared_ptr<const PieceShapes> pieces;
};

// The plies the GIF shows, `from` to `to` within the game.
[[nodiscard]] std::pair<int, int> gifRange(const GifGame& game, const GifOptions& options);
// The frame of the position after `ply` moves, as the GIF has it.
[[nodiscard]] Image gifFrame(const GifGame& game, const GifOptions& options, int ply);

struct GifProgress {
    std::atomic<int> done{0};
    std::atomic<int> total{0};
};
// The GIF's bytes; empty when cancelled.
[[nodiscard]] std::string makeGif(const GifGame& game, const GifOptions& options, GifProgress* progress = nullptr,
                                  const std::atomic<bool>* cancel = nullptr);
// What it will weigh: its first frames made, the rest counted like them.
[[nodiscard]] uint64_t gifEstimate(const GifGame& game, const GifOptions& options);

// A GIF of frames already drawn, all of one size, with their delays (for the tests and the game's).
class GifWriter {
   public:
    GifWriter(int width, int height, const std::vector<Rgba>& palette);
    void frame(const Image& image, int delay_ms);
    [[nodiscard]] std::string finish();
    [[nodiscard]] std::size_t size() const noexcept { return out_.size(); }

   private:
    int width_, height_;
    std::vector<Rgba> palette_;
    std::vector<uint8_t> previous_;  // the indexes of the frame before
    std::string out_;
    std::vector<int> cache_;  // by 18-bit colour: its index, -1 unknown
    [[nodiscard]] uint8_t index(const uint8_t* rgb);
};

// 256 colours at most for images like these: `keep` exactly, the others by median cut of their pixels.
[[nodiscard]] std::vector<Rgba> gifPalette(const std::vector<const Image*>& images, const std::vector<Rgba>& keep);

}  // namespace fcg::publish
