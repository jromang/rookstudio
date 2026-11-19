#include "prep.hpp"

#include <algorithm>
#include <cctype>

#include <chess.hpp>
#include <study/scan_board.hpp>
#include <study/tree.hpp>

namespace fcg {

namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// The moves of an answer, in SAN on `board`, with their share of its games; at most `most`.
std::vector<PrepMove> movesOf(const std::optional<study::TreeAnswer>& answer, const chess::Board& board,
                              std::size_t most) {
    std::vector<PrepMove> out;
    if (!answer || answer->total.games == 0) return out;
    study::ScanBoard scan;
    if (!scan.set(board)) return out;
    for (const auto& m : answer->moves) {
        if (m.code == study::TreeMove::END || out.size() >= most) continue;
        const auto move = study::moveOfCode(board, scan, m.code);
        if (!move) continue;
        out.push_back({chess::uci::moveToSan(board, *move), m.stats.games,
                       static_cast<double>(m.stats.games) / static_cast<double>(answer->total.games)});
    }
    return out;
}

}  // namespace

PrepReport prepReport(const study::TreeBase& base, const std::string& opponent, const std::string& me,
                      const std::vector<std::pair<std::string, study::Repertoire>>& repertoires, std::size_t gaps) {
    PrepReport out;
    if (!base.snapshot) return out;
    const auto& snapshot = *base.snapshot;
    // Their names, and the user's, in the database's table of names.
    std::vector<uint32_t> theirs, mine;
    const auto them = lower(opponent), self = lower(me);
    for (uint32_t i = 0; i < snapshot.names(); ++i) {
        const auto name = lower(snapshot.name(i));
        if (name == them) theirs.push_back(i);
        if (!self.empty() && name == self) mine.push_back(i);
    }
    const auto in = [](const std::vector<uint32_t>& ids, uint32_t id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };
    // Their games, their years; those against the user.
    for (study::GameId id = 0; id < snapshot.games(); ++id) {
        if (snapshot.deleted(id)) continue;
        const auto names      = snapshot.rowIds(id);
        const bool white_them = in(theirs, names.white), black_them = in(theirs, names.black);
        if (!white_them && !black_them) continue;
        const auto numbers = snapshot.numbers(id);
        ++out.games;
        if (numbers.date != 0) {
            out.first = out.first == 0 ? numbers.date : std::min(out.first, numbers.date);
            out.last  = std::max(out.last, numbers.date);
        }
        const bool against_me = (white_them && in(mine, names.black)) || (black_them && in(mine, names.white));
        if (!against_me) continue;
        const char result = numbers.result;
        if (result == '=')
            ++out.drawn;
        else if ((result == '1' && in(mine, names.white)) || (result == '0' && in(mine, names.black)))
            ++out.won;
        else if (result == '1' || result == '0')
            ++out.lost;
        out.last_between = std::max(out.last_between, numbers.date);
    }
    // What they play: with White from the start; with Black, against the first moves they met most.
    const chess::Board start;
    study::ScanBoard scan;
    if (!scan.set(start)) return out;
    out.with_white   = movesOf(study::playerMoves(base, opponent, true)(scan), start, 4);
    const auto black = study::playerMoves(base, opponent, false);
    const auto met   = movesOf(black(scan), start, 2);
    for (const auto& first : met) {
        auto board = start;
        board.makeMove(chess::uci::parseSan(board, first.san));
        study::ScanBoard after;
        if (!after.set(board)) continue;
        out.with_black.push_back({first.san, movesOf(black(after), board, 3)});
    }
    // The user's repertoires against their games with the other colour: the lines they play that have no answer.
    study::CoverageOptions options;
    options.least_games = 3;
    options.least_share = 0.05;
    options.listed      = 20;
    for (const auto& [name, repertoire] : repertoires) {
        const bool they_white = repertoire.side() == chess::Color::BLACK;
        for (auto& gap : study::coverage(repertoire, study::playerMoves(base, opponent, they_white), options).gaps)
            out.gaps.push_back({name, std::move(gap)});
    }
    std::stable_sort(out.gaps.begin(), out.gaps.end(),
                     [](const auto& a, const auto& b) { return a.gap.games > b.gap.games; });
    if (out.gaps.size() > gaps) out.gaps.resize(gaps);
    return out;
}

}  // namespace fcg
