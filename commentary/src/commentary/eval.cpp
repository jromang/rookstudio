#include "eval.hpp"

#include <algorithm>
#include <cmath>

namespace fcg::commentary {

namespace {

// Where the win-rate curve of expected() gives half the games won, and how fast it rises, in centipawns; fitted on
// GaiaChess's own win, draw and loss figures.
constexpr double ANCHOR = 100.0;
constexpr double SPREAD = 31.0;

}  // namespace

int32_t cappedCp(Score score) noexcept {
    if (score.kind == Score::Kind::Cp) return std::clamp(score.value, -CP_CAP, CP_CAP);
    return score.value > 0 ? CP_CAP : -CP_CAP;
}

int32_t EngineLine::whiteCp() const noexcept { return cappedCp(score); }

bool EngineLine::isExact() const noexcept {
    const bool certain = wdl && ((*wdl)[0] == 1000 || (*wdl)[1] == 1000 || (*wdl)[2] == 1000) &&
                         (*wdl)[0] + (*wdl)[1] + (*wdl)[2] == 1000;
    if (!certain) return false;
    if (score.kind == Score::Kind::Cp) return score.value == 0 || std::abs(score.value) == CP_CAP;
    return true;
}

double EngineLine::whiteExpected() const noexcept {
    if (score.kind == Score::Kind::Mate) return score.value > 0 ? 100.0 : 0.0;
    if (wdl) {
        const auto [w, d, l] = *wdl;
        if (w + d + l > 0)
            return 100.0 * (static_cast<double>(w) + static_cast<double>(d) / 2.0) / static_cast<double>(w + d + l);
    }
    return expected(score.value);
}

double expected(int32_t cp) noexcept {
    const double c  = static_cast<double>(std::clamp(cp, -CP_CAP, CP_CAP));
    const auto wins = [](double x) { return 1.0 / (1.0 + std::exp((ANCHOR - x) / SPREAD)); };
    return 50.0 * (1.0 + wins(c) - wins(-c));
}

double winPercent(int32_t cp) noexcept {
    const double c = static_cast<double>(std::clamp(cp, -CP_CAP, CP_CAP));
    return 50.0 + 50.0 * (2.0 / (1.0 + std::exp(-0.00368208 * c)) - 1.0);
}

Searched searched(const Position& pos, const EngineLine& line) {
    const int32_t sign = pos.turn() == Color::White ? 1 : -1;
    Searched out;
    out.moves = line.moves;
    out.score =
        line.score.kind == Score::Kind::Cp ? Score::cp(sign * line.score.value) : Score::mate(sign * line.score.value);
    return out;
}

Standing standingOf(double expected) noexcept {
    if (expected < 10.0) return Standing::Lost;
    if (expected < 30.0) return Standing::ClearlyWorse;
    if (expected < 45.0) return Standing::SlightlyWorse;
    if (expected <= 55.0) return Standing::Balanced;
    if (expected <= 70.0) return Standing::SlightlyBetter;
    if (expected <= 90.0) return Standing::ClearlyBetter;
    return Standing::Winning;
}

uint8_t positionNag(double white_expected) noexcept {
    switch (standingOf(white_expected)) {
        case Standing::Balanced:
            return 10;
        case Standing::SlightlyBetter:
            return 14;
        case Standing::SlightlyWorse:
            return 15;
        case Standing::ClearlyBetter:
            return 16;
        case Standing::ClearlyWorse:
            return 17;
        case Standing::Winning:
            return 18;
        default:
            return 19;
    }
}

}  // namespace fcg::commentary
