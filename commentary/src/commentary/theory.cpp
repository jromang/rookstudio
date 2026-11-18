#include "theory.hpp"

#include <algorithm>

#include <study/scan_board.hpp>
#include <study/search.hpp>

#include "position.hpp"

namespace fcg::commentary {

namespace {

// Lists of games past this many are not looked through for the game reviewed: one game more or less changes nothing.
constexpr uint64_t LISTED = 64;

}  // namespace

Opening theoryOf(const study::Tree& tree, const std::shared_ptr<const study::Snapshot>& snapshot, const MainLine& line,
                 const TheoryOptions& options) {
    Opening out;
    study::ScanBoard board;
    if (line.positions().empty() || !board.setFen(line.positions()[0].fen(), false)) return out;
    const auto& snap = *snapshot;
    std::optional<study::TreeGame> single;  // the one game known to be in the line's position, when it is not kept
    std::optional<study::GameId> copy;      // the game taken for another copy of the one reviewed
    for (std::size_t ply = 0; ply < line.moves().size() && ply < tree.plies(); ++ply) {
        const auto coded = board.parseSan(line.positions()[ply].san(line.moves()[ply]));
        if (!coded) break;
        const int code = board.encode(*coded);
        uint64_t games = 0;
        if (const auto here = tree.at(snap, board.key())) {
            single.reset();
            // A position where every game ended: beyond the games, no novelty.
            if (std::all_of(here->moves.begin(), here->moves.end(),
                            [](const study::TreeMove& m) { return m.code == study::TreeMove::END; }))
                break;
            for (const auto& m : here->moves)
                if (m.code == code) games = m.stats.games;
            if (games > 0 && games <= LISTED) {
                auto with = tree.gamesWith(snap, board.key(), code);
                if (options.itself)
                    std::erase_if(with, [&](const study::TreeGame& g) { return g.id == *options.itself; });
                games = with.size();
                if (games == 1) single = study::TreeGame{with[0].id, with[0].ply + 1, code};
            }
        } else if (single) {
            // Only that game is here: is the line still its moves?
            const auto m  = snap.gameMoves(single->id);
            const bool on = single->ply < m.plies && single->ply < m.codes.size() && board.codeCount() <= 256 &&
                            m.codes[single->ply] == code;
            games         = on ? 1 : 0;
            single->ply += 1;
        } else {
            break;  // the tree knows no game here
        }
        const bool another_copy = games == 1 && single && options.year &&
                                  static_cast<int>(snap.numbers(single->id).date >> 9) == *options.year;
        if (games == 0 || another_copy) {
            if (another_copy) copy = single->id;
            out.novelty = ply;
            break;
        }
        if (games >= options.book_games) out.book_plies = ply + 1;
        board.play(*coded);
    }
    if (out.novelty && options.confirm) {
        // A position another game reached by other moves: no novelty.
        study::ScanBoard after;
        if (after.setFen(line.positions()[*out.novelty + 1].fen(), false)) {
            const auto hits = study::searchAll(snapshot, study::PositionQuery{after});
            if (std::any_of(hits.begin(), hits.end(), [&](const study::SearchHit& h) {
                    return (!options.itself || h.id != *options.itself) && (!copy || h.id != *copy);
                }))
                out.novelty.reset();
        }
    }
    return out;
}

}  // namespace fcg::commentary
