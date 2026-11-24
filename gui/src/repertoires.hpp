#pragma once

// The user's opening repertoires (mockup M3, board 2): PGN files in the profile's repertoires folder, a game and its
// variations each, the colour they prepare in their [RepertoireSide] tag. One of them is active: the star of the
// Board page's opening tree adds a move to it or takes it out.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <chess.hpp>
#include <study/game.hpp>
#include <study/repertoire.hpp>

namespace fcg {

struct RepertoireFile {
    std::string name;  // the file's name without .pgn
    std::filesystem::path file;
    chess::Color side = chess::Color::WHITE;
    GameTree lines;
    uint64_t revision = 0;  // changed with its lines: what is made of them (the training's cards) is made again
};

class RepertoireShelf {
   public:
    // The repertoires in `profile`'s repertoires folder, by name.
    explicit RepertoireShelf(std::filesystem::path profile);

    [[nodiscard]] const std::vector<RepertoireFile>& entries() const noexcept { return entries_; }
    [[nodiscard]] int active() const noexcept { return active_; }  // -1 when there is none
    void setActive(int index);
    // A new repertoire of `side`, named `name` (made unique), active: its index. Throws std::runtime_error.
    std::size_t create(const std::string& name, chess::Color side);
    // The active repertoire, made when there is none: "White" or "Black" for `side`.
    std::size_t activeOrNew(chess::Color side);

    // Its positions (study::Repertoire), made again after a change.
    [[nodiscard]] const study::Repertoire& map(std::size_t index);
    // Whether the move from the position after `path` (moves from the start) is in the repertoire.
    [[nodiscard]] bool has(std::size_t index, std::span<const chess::Move> path, chess::Move move);
    // The move added from the position after `path` (study::addMove), or taken out where the lines play it, with
    // what follows it there; the file written. True when it is in the repertoire afterwards.
    bool toggle(std::size_t index, std::span<const chess::Move> path, chess::Move move);
    // The repertoire's lines put back (an undo), the file written.
    void restore(std::size_t index, GameTree lines);
    // The moves of `line`, played one after the other from the position after `path`, added where the lines do not
    // have them yet (study::addMove), the file written once: the opening report's main moves (#91). The moves added.
    std::size_t addLine(std::size_t index, std::span<const chess::Move> path, std::span<const chess::Move> line);

   private:
    void save(std::size_t index) const;
    void changed(std::size_t index);  // its map made again, its revision new, its file written

    std::filesystem::path dir_;
    std::vector<RepertoireFile> entries_;
    std::vector<std::optional<study::Repertoire>> maps_;
    int active_ = -1;
    uint64_t revisions_ = 0;
};

}  // namespace fcg
