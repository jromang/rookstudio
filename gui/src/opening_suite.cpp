#include "opening_suite.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string_view>

#include <fmt/include/fmt/format.h>
#include <chess.hpp>

namespace fcg::assets {
extern const unsigned char lichess_openings[];
extern const std::size_t lichess_openings_size;
}  // namespace fcg::assets

namespace fcg {

namespace {

// The position after `pgn`'s moves, and their number; none when a move cannot be read.
std::optional<std::pair<chess::Board, int>> replay(std::string_view pgn) {
    chess::Board board;
    int plies      = 0;
    std::size_t at = 0;
    while (at < pgn.size()) {
        while (at < pgn.size() && pgn[at] == ' ') ++at;
        auto end = pgn.find(' ', at);
        if (end == std::string_view::npos) end = pgn.size();
        const auto token = pgn.substr(at, end - at);
        at               = end;
        if (token.empty() || token.back() == '.') continue;  // "1." and "1..."
        chess::Move move = chess::Move::NO_MOVE;
        try {
            move = chess::uci::parseSan(board, std::string(token));
        } catch (const std::exception&) {
            return std::nullopt;  // not a move of the position
        }
        if (move == chess::Move::NO_MOVE) return std::nullopt;
        board.makeMove(move);
        ++plies;
    }
    return std::make_pair(board, plies);
}

std::string withoutQuotes(std::string_view text) {
    std::string out;
    for (const char c : text)
        if (c != '"' && c != ';') out += c;
    return out;
}

}  // namespace

const std::vector<NamedOpening>& namedOpenings() {
    static const auto table = [] {
        std::vector<NamedOpening> out;
        const std::string_view all(reinterpret_cast<const char*>(assets::lichess_openings),
                                   assets::lichess_openings_size);
        bool header = true;
        for (std::size_t at = 0; at < all.size();) {
            auto end = all.find('\n', at);
            if (end == std::string_view::npos) end = all.size();
            auto line = all.substr(at, end - at);
            at        = end + 1;
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (header) {  // eco, name, pgn
                header = false;
                continue;
            }
            const auto first  = line.find('\t');
            const auto second = first == std::string_view::npos ? first : line.find('\t', first + 1);
            if (second == std::string_view::npos) continue;
            auto pgn = line.substr(second + 1);
            if (const auto tab = pgn.find('\t'); tab != std::string_view::npos) pgn = pgn.substr(0, tab);
            out.push_back({std::string(line.substr(0, first)), std::string(line.substr(first + 1, second - first - 1)),
                           std::string(pgn)});
        }
        return out;
    }();
    return table;
}

std::vector<SuitePosition> openingSuite(const std::vector<NamedOpening>& all, const OpeningSuiteOptions& options) {
    std::vector<SuitePosition> kept;
    std::set<std::string> names, positions;
    for (const auto& o : all) {
        if (o.eco.empty() || options.families.find(o.eco[0]) == std::string::npos) continue;
        if (options.one_per_name && names.count(o.name)) continue;
        const auto played = replay(o.pgn);
        if (!played || played->second < options.min_plies || played->second > options.max_plies) continue;
        if (!positions.insert(played->first.getFen(false)).second) continue;  // reached by another line already
        names.insert(o.name);
        // The four fields, then the counters (hmvc, fmvn): the games' moves numbered from there.
        kept.push_back({fmt::format("{} c0 \"{} {}\";", played->first.getEpd(), o.eco, withoutQuotes(o.name)), o.eco,
                        o.name, played->second});
    }
    std::stable_sort(kept.begin(), kept.end(), [](const auto& a, const auto& b) { return a.eco < b.eco; });
    keepEvenly(kept, options.most);
    return kept;
}

void keepEvenly(std::vector<SuitePosition>& positions, std::size_t most) {
    if (most == 0 || positions.size() <= most) return;
    std::vector<SuitePosition> spread;
    spread.reserve(most);
    for (std::size_t i = 0; i < most; ++i) spread.push_back(std::move(positions[i * positions.size() / most]));
    positions = std::move(spread);
}

void writeOpeningSuite(const std::filesystem::path& path, const std::vector<SuitePosition>& positions) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error(fmt::format("{} cannot be written", path.string()));
    for (const auto& p : positions) out << p.epd << '\n';
    out.flush();
    if (!out) throw std::runtime_error(fmt::format("{} cannot be written", path.string()));
}

}  // namespace fcg
