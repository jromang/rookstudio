#pragma once

// Lichess's answers kept in the profile (lichess-cache/), so that a position is asked once: the tables' for good (their
// verdicts do not change), the cloud's a month (view_board_online.cpp).

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "lichess_api.hpp"

namespace fcg {

[[nodiscard]] std::optional<lichess::Tablebase> cachedTablebase(const std::filesystem::path& root,
                                                                const std::string& fen);
void keepTablebase(const std::filesystem::path& root, const std::string& fen, const lichess::Tablebase& t);
// The tables' main line from a position (lichess::tablebaseMainline()), kept for good too (#42).
[[nodiscard]] std::optional<std::vector<std::string>> cachedTablebaseLine(const std::filesystem::path& root,
                                                                          const std::string& fen);
void keepTablebaseLine(const std::filesystem::path& root, const std::string& fen, const std::vector<std::string>& line);

}  // namespace fcg
