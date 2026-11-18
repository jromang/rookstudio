#pragma once

// What a game's moves tell without replaying them, for the searches to leave out the games that cannot match: when
// each pawn left its starting square (a pawn never comes back to it), and the material at the end (a side's pawns, and
// its men all told, only ever go down).

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "scan_board.hpp"

namespace fcg::study {

struct GameSignature {
    // For each pawn's home square, a2..h2 then a7..h7: the first ply after which the pawn that stood there at the
    // start is gone (the first position without it). 0 when none stood there; STAYED when it is still there at the
    // end, or left after ply 254.
    static constexpr uint8_t STAYED = 255;
    std::array<uint8_t, 16> home{};
    // The material at the end: for White then Black, pawns, knights, bishops, rooks and queens, at most 15.
    std::array<uint8_t, 10> material{};
    // A pawn was promoted: without that, each kind of men only ever goes down too.
    bool promoted = false;

    // The home square of entry `i`, and the entry of a home square (-1 for another square).
    [[nodiscard]] static int square(int i) noexcept { return i < 8 ? 8 + i : 48 + (i - 8); }
    [[nodiscard]] static int entry(int square) noexcept {
        return square >= 8 && square < 16 ? square - 8 : square >= 48 && square < 56 ? 8 + (square - 48) : -1;
    }
    bool operator==(const GameSignature&) const = default;
};

// Made as a game is read, a move at a time.
class SignatureMaker {
   public:
    explicit SignatureMaker(const ScanBoard& start) noexcept;
    // Before `move`, the ply-th of the game (0 for the first), is played on `board`.
    void before(const ScanBoard& board, const CodedMove& move, uint32_t ply) noexcept;
    [[nodiscard]] GameSignature finish(const ScanBoard& end) const noexcept;

   private:
    GameSignature signature_;
};

// A game's signature from its codes: none when a code is not a move.
[[nodiscard]] std::optional<GameSignature> signatureOf(const ScanBoard& start, std::span<const uint8_t> codes,
                                                       uint32_t plies);

// The plies at which a game may reach a position, as its signature tells: none when it cannot reach it.
struct PlyWindow {
    uint32_t from = 0;
    uint32_t to   = 0;  // inclusive
};
[[nodiscard]] std::optional<PlyWindow> plyWindow(const GameSignature& game, uint32_t plies, const ScanBoard& position);

}  // namespace fcg::study
