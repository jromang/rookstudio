#pragma once

// The signs and short texts what RookStudio publishes writes beside the moves (#93): a symbol (NAG), an evaluation
// from White's side, a clock; and the notice of the pieces it draws.

#include <cstdint>
#include <string>

#include <chess.hpp>
#include <types/score.hpp>

namespace fcg::publish {

// "!", "?!", "□", "⩲", "±", "N"; "$NN" for one without a sign.
[[nodiscard]] std::string nagSign(uint8_t nag);
// A score from its mover's side as White's: "+0.31", "−1.20", "0.00", "#3", "#−3".
[[nodiscard]] std::string evalText(const fastchess::Score& score, chess::Color mover);
// A player's clock: "1:23:45", "4:05".
[[nodiscard]] std::string clockText(int64_t ms);

// The notice of the pieces drawn: a line ("Pieces: Cburnett, by Colin M.L. Burnett, under the BSD 3-clause license.")
// and the text of their license; for the user's own pieces, a line that says so and no license.
struct PiecesNotice {
    std::string line;
    std::string license;
};
[[nodiscard]] PiecesNotice piecesNotice(const std::string& set_id);

}  // namespace fcg::publish
