#pragma once

// Lichess's answers kept in the profile (lichess-cache/), so that a position is asked once: the tables' for good (their
// verdicts do not change), the cloud's a month (view_board_online.cpp).

#include <filesystem>
#include <optional>
#include <string>

#include "lichess_api.hpp"

namespace fcg {

[[nodiscard]] std::optional<lichess::Tablebase> cachedTablebase(const std::filesystem::path& root,
                                                                const std::string& fen);
void keepTablebase(const std::filesystem::path& root, const std::string& fen, const lichess::Tablebase& t);

}  // namespace fcg
