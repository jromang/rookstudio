#pragma once

// A game's result in one character, as the databases keep it (#117): '1' for 1-0, '0' for 0-1, '=' for a draw, '*' for
// none (not known, or the game goes on).

#include <string>
#include <string_view>

namespace fcg::study {

// From a result as PGN writes it ("1-0", "0-1", "1/2-1/2"); '*' for anything else.
[[nodiscard]] constexpr char resultChar(std::string_view pgn) noexcept {
    return pgn == "1-0" ? '1' : pgn == "0-1" ? '0' : pgn == "1/2-1/2" ? '=' : '*';
}

// And back, as PGN writes it: "1-0", "0-1", "1/2-1/2", "*".
[[nodiscard]] inline std::string resultText(char result) {
    return result == '1' ? "1-0" : result == '0' ? "0-1" : result == '=' ? "1/2-1/2" : "*";
}

// As the window shows it, a draw "½-½".
[[nodiscard]] inline std::string shownResult(char result) {
    return result == '1' ? "1-0" : result == '0' ? "0-1" : result == '=' ? "½-½" : "*";
}

}  // namespace fcg::study
