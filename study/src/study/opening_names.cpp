#include "opening_names.hpp"

#include <utility>

#include <chess.hpp>

#include "scan_board.hpp"

namespace fcg::study {

OpeningNames::OpeningNames(std::string_view tsv) {
    ScanBoard start;
    start.setFen(chess::constants::STARTPOS, false);
    bool header = true;
    for (std::size_t at = 0; at < tsv.size();) {
        auto end = tsv.find('\n', at);
        if (end == std::string_view::npos) end = tsv.size();
        auto line = tsv.substr(at, end - at);
        at        = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (std::exchange(header, false)) continue;
        const auto first  = line.find('\t');
        const auto second = first == std::string_view::npos ? first : line.find('\t', first + 1);
        if (second == std::string_view::npos) continue;
        auto pgn = line.substr(second + 1);
        if (const auto tab = pgn.find('\t'); tab != std::string_view::npos) pgn = pgn.substr(0, tab);
        NamedOpening o;
        o.eco       = std::string(line.substr(0, first));
        o.name      = std::string(line.substr(first + 1, second - first - 1));
        auto board  = start;
        bool played = true;
        for (std::size_t p = 0; p < pgn.size();) {
            while (p < pgn.size() && pgn[p] == ' ') ++p;
            auto q = pgn.find(' ', p);
            if (q == std::string_view::npos) q = pgn.size();
            const auto token = pgn.substr(p, q - p);
            p                = q;
            if (token.empty() || token.back() == '.') continue;
            const auto move = board.parseSan(token);
            if (!move) {
                played = false;
                break;
            }
            board.play(*move);
            o.moves.emplace_back(token);
        }
        if (!played || o.moves.empty()) continue;
        const auto index = static_cast<uint32_t>(entries_.size());
        by_key_.try_emplace(board.key(), index);
        const auto it = by_eco_.find(o.eco);
        if (it == by_eco_.end() || o.moves.size() < entries_[it->second].moves.size()) by_eco_[o.eco] = index;
        entries_.push_back(std::move(o));
    }
}

std::optional<std::size_t> OpeningNames::at(uint64_t key) const {
    const auto it = by_key_.find(key);
    if (it == by_key_.end()) return std::nullopt;
    return it->second;
}

std::optional<std::size_t> OpeningNames::shortest(std::string_view eco) const {
    const auto it = by_eco_.find(std::string(eco));
    if (it == by_eco_.end()) return std::nullopt;
    return it->second;
}

std::string_view openingFamily(std::string_view name) { return name.substr(0, name.find(',')); }

}  // namespace fcg::study
