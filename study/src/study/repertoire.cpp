#include "repertoire.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <unordered_set>
#include <utility>

#include <tour/util.hpp>

#include "game_tree.hpp"

namespace fcg::study {

namespace {

chess::Board startOf(const ReplayGame& game, bool chess960) {
    chess::Board board;
    board.set960(chess960);
    board.setFen(game.start_fen.empty() ? std::string(chess::constants::STARTPOS) : game.start_fen);
    return board;
}

bool legal(const chess::Board& board, chess::Move move) {
    chess::Movelist list;
    chess::movegen::legalmoves(list, board);
    return std::find(list.begin(), list.end(), move) != list.end();
}

// A chess.hpp move as the fast board takes it: a castling goes from the king to its rook in both.
CodedMove coded(chess::Move m) {
    CodedMove c;
    c.from = static_cast<uint8_t>(m.from().index());
    c.to   = static_cast<uint8_t>(m.to().index());
    if (m.typeOf() == chess::Move::CASTLING)
        c.flags = m.to().index() > m.from().index() ? MOVE_SHORT_CASTLE : MOVE_LONG_CASTLE;
    else if (m.typeOf() == chess::Move::ENPASSANT)
        c.flags = MOVE_EN_PASSANT;
    else if (m.typeOf() == chess::Move::PROMOTION)
        c.promotion = static_cast<uint8_t>(static_cast<int>(m.promotionType()));
    return c;
}

// The legal move of `board` that a code of the same position names, in SAN; empty when none.
std::string sanOf(const chess::Board& board, const ScanBoard& scan, int code) {
    const auto wanted = scan.decode(code);
    chess::Movelist list;
    chess::movegen::legalmoves(list, board);
    for (const auto& m : list) {
        const auto c = coded(m);
        if (c.from == wanted.from && c.to == wanted.to && c.promotion == wanted.promotion)
            return chess::uci::moveToSan(board, m);
    }
    return {};
}

}  // namespace

Repertoire::Repertoire(const GameTree& lines, chess::Color side, bool chess960) : side_(side), chess960_(chess960) {
    // Every line from the start, the game's and each variation's: a variation's moves before its own are its
    // parents', met again and merged.
    for (int line = -1; line < static_cast<int>(lines.variations.size()); ++line) {
        const auto game = lineOf(lines, line);
        auto board      = startOf(game, chess960);
        ScanBoard scan;
        if (!scan.set(board)) return;
        if (line < 0) start_ = scan.key();
        uint64_t before = 0;
        std::string via;
        for (std::size_t ply = 0;; ++ply) {
            const uint64_t key = scan.key();
            auto [it, fresh]   = positions_.try_emplace(key);
            auto& pos          = it->second;
            if (fresh) {
                pos.key      = key;
                pos.fen      = board.getFen();
                pos.own      = board.sideToMove() == side;
                pos.ply      = static_cast<int>(ply);
                pos.line     = line;
                pos.line_ply = static_cast<int>(ply);
                pos.from     = before;
                pos.via      = via;
            } else if (static_cast<int>(ply) < pos.ply) {
                pos.ply  = static_cast<int>(ply);
                pos.from = before;
                pos.via  = via;
            }
            if (ply == game.moves.size()) break;
            const auto move = game.moves[ply].move;
            if (move == chess::Move(chess::Move::NULL_MOVE) || !legal(board, move)) break;
            const auto c   = coded(move);
            const int code = scan.encode(c);
            via            = chess::uci::moveToSan(board, move);
            board.makeMove<false>(move);
            scan.play(c);
            if (std::none_of(pos.moves.begin(), pos.moves.end(),
                             [&](const RepertoireMove& m) { return m.code == code; }))
                pos.moves.push_back({code, via, scan.key()});
            before = key;
        }
    }
}

const RepertoirePosition* Repertoire::at(uint64_t key) const {
    const auto it = positions_.find(key);
    return it == positions_.end() ? nullptr : &it->second;
}

bool Repertoire::has(uint64_t key, int code) const {
    const auto* pos = at(key);
    return pos &&
           std::any_of(pos->moves.begin(), pos->moves.end(), [&](const RepertoireMove& m) { return m.code == code; });
}

std::vector<std::string> Repertoire::lineTo(uint64_t key) const {
    std::vector<std::string> line;
    for (const auto* pos = at(key); pos && pos->key != start_; pos = at(pos->from)) {
        line.push_back(pos->via);
        if (line.size() > positions_.size()) return {};  // a broken chain: never in a map built above
    }
    std::reverse(line.begin(), line.end());
    return line;
}

bool addMove(GameTree& lines, bool chess960, std::span<const chess::Move> path, chess::Move move) {
    auto board = startOf(lines.main, chess960);
    for (const auto& m : path) {
        if (!legal(board, m)) return false;
        board.makeMove<false>(m);
    }
    if (!legal(board, move)) return false;
    ScanBoard scan;
    if (!scan.set(board)) return false;
    // Where the lines reach the position already, by whatever order: the move goes there.
    const Repertoire known(lines, board.sideToMove(), chess960);
    if (const auto* pos = known.at(scan.key())) {
        playInTree(lines, chess960, pos->line, pos->line_ply, move);
        return true;
    }
    int line = -1, ply = 0;
    for (const auto& m : path) std::tie(line, ply) = playInTree(lines, chess960, line, ply, m);
    playInTree(lines, chess960, line, ply, move);
    return true;
}

MovesAt treeMoves(std::vector<TreeBase> bases, std::size_t* reading) {
    return [bases = std::move(bases), reading](const ScanBoard& position) -> std::optional<TreeAnswer> {
        std::optional<TreeAnswer> out;
        std::map<int, TreeStats> by_code;
        for (std::size_t i = 0; i < bases.size(); ++i) {
            const auto& base = bases[i];
            if (!base.snapshot || !base.tree || base.tree->stale(*base.snapshot)) continue;
            if (reading) *reading = i;
            const auto here = base.tree->at(*base.snapshot, position.key());
            if (!here) continue;
            if (!out) out.emplace();
            out->total.add(here->total);
            for (const auto& m : here->moves) by_code[m.code].add(m.stats);
        }
        if (!out) return out;
        for (const auto& [code, stats] : by_code) out->moves.push_back({code, stats});
        std::stable_sort(out->moves.begin(), out->moves.end(), [](const TreeMove& a, const TreeMove& b) {
            if ((a.code == TreeMove::END) != (b.code == TreeMove::END)) return b.code == TreeMove::END;
            return a.stats.games > b.stats.games;
        });
        return out;
    };
}

MovesAt playerMoves(TreeBase base, std::string_view player, bool white) {
    std::vector<uint32_t> ids;  // the player's name in the database's table of names
    if (base.snapshot) {
        const auto wanted = lower(player);
        for (uint32_t i = 0; i < base.snapshot->names(); ++i)
            if (lower(base.snapshot->name(i)) == wanted) ids.push_back(i);
    }
    return
        [base = std::move(base), ids = std::move(ids), white](const ScanBoard& position) -> std::optional<TreeAnswer> {
            if (ids.empty() || !base.snapshot || !base.tree || base.tree->stale(*base.snapshot)) return std::nullopt;
            TreeAnswer out;
            std::map<int, TreeStats> by_code;
            for (const auto& g : base.tree->gamesAt(*base.snapshot, position.key())) {
                const auto names = base.snapshot->rowIds(g.id);
                if (std::find(ids.begin(), ids.end(), white ? names.white : names.black) == ids.end()) continue;
                const auto numbers = base.snapshot->numbers(g.id);
                out.total.add(numbers);
                by_code[g.code].add(numbers);
            }
            if (out.total.games == 0) return std::nullopt;
            for (const auto& [code, stats] : by_code) out.moves.push_back({code, stats});
            std::stable_sort(out.moves.begin(), out.moves.end(), [](const TreeMove& a, const TreeMove& b) {
                if ((a.code == TreeMove::END) != (b.code == TreeMove::END)) return b.code == TreeMove::END;
                return a.stats.games > b.stats.games;
            });
            return out;
        };
}

Coverage coverage(const Repertoire& repertoire, const MovesAt& moves, const CoverageOptions& options) {
    Coverage out;
    out.positions   = repertoire.size();
    const auto* top = repertoire.at(repertoire.start());
    if (!top) return out;

    // The positions in an order where each comes after those its moves come from: the repertoire's moves followed
    // from the start, depth first; a move back to a position on the way (a repetition) is left out.
    std::vector<const RepertoirePosition*> order;
    {
        std::unordered_set<uint64_t> done, open;
        std::vector<std::pair<const RepertoirePosition*, std::size_t>> stack{{top, 0}};
        open.insert(top->key);
        while (!stack.empty()) {
            auto& [pos, next] = stack.back();
            if (next < pos->moves.size()) {
                const auto* child = repertoire.at(pos->moves[next++].to);
                if (child && !done.contains(child->key) && !open.contains(child->key)) {
                    open.insert(child->key);
                    stack.emplace_back(child, 0);
                }
                continue;
            }
            open.erase(pos->key);
            done.insert(pos->key);
            order.push_back(pos);
            stack.pop_back();
        }
        std::reverse(order.begin(), order.end());
    }
    std::unordered_map<uint64_t, std::size_t> place;
    for (std::size_t i = 0; i < order.size(); ++i) place[order[i]->key] = i;
    // A move that goes back up the order (a repetition) is not followed.
    const auto followed = [&](const RepertoirePosition& from, const RepertoireMove& m) {
        const auto it = place.find(m.to);
        return it != place.end() && it->second > place.at(from.key);
    };

    // What the databases played at each position, and how many games went on from it.
    std::vector<std::optional<TreeAnswer>> played(order.size());
    std::vector<uint64_t> going(order.size(), 0);
    for (std::size_t i = 0; i < order.size(); ++i) {
        ScanBoard scan;
        if (!scan.setFen(order[i]->fen, repertoire.chess960())) continue;
        played[i] = moves(scan);
        if (!played[i]) continue;
        ++out.known;
        for (const auto& m : played[i]->moves)
            if (m.code != TreeMove::END) going[i] += m.stats.games;
    }
    const auto gamesOf = [&](std::size_t i, int code) -> uint64_t {
        if (!played[i]) return 0;
        for (const auto& m : played[i]->moves)
            if (m.code == code) return m.stats.games;
        return 0;
    };

    // How often the games along the repertoire come to each position: its own choices alike, the opponent's moves as
    // often as the games played them (alike where the databases do not know the position).
    std::vector<double> reach(order.size(), 0);
    reach[0] = 1;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const auto& pos = *order[i];
        std::vector<const RepertoireMove*> next;
        for (const auto& m : pos.moves)
            if (followed(pos, m)) next.push_back(&m);
        if (next.empty() || reach[i] == 0) continue;
        for (const auto* m : next) {
            double share = 1.0 / static_cast<double>(next.size());
            if (!pos.own && played[i]) share = going[i] ? static_cast<double>(gamesOf(i, m->code)) / going[i] : 0;
            reach[place.at(m->to)] += reach[i] * share;
        }
    }

    // What each position answers of the games through it, from the ends of the lines back to the start; the gaps met.
    const auto worth = [&](std::size_t i, uint64_t games) {
        return games >= options.least_games && going[i] > 0 &&
               static_cast<double>(games) >= options.least_share * static_cast<double>(going[i]);
    };
    std::vector<double> answered(order.size(), 1);
    for (std::size_t i = order.size(); i-- > 0;) {
        const auto& pos = *order[i];
        std::vector<const RepertoireMove*> next;
        for (const auto& m : pos.moves)
            if (followed(pos, m)) next.push_back(&m);
        if (pos.own) {
            if (!next.empty()) {
                double sum = 0;
                for (const auto* m : next) sum += answered[place.at(m->to)];
                answered[i] = sum / static_cast<double>(next.size());
            } else if (pos.moves.empty() && played[i] && going[i] >= options.least_games) {
                // The opponent's move ends the line, and the games go on.
                answered[i] = 0;
                out.gaps.push_back({pos.key, repertoire.lineTo(pos.key), {}, going[i], 1, reach[i]});
            }
            continue;
        }
        // The opponent moves: its moves the games played often enough, and those the repertoire answers.
        const bool judged = !pos.moves.empty() || pos.ply < options.until_ply;
        if (!played[i] || !judged) {
            if (!next.empty()) {
                double sum = 0;
                for (const auto* m : next) sum += answered[place.at(m->to)];
                answered[i] = sum / static_cast<double>(next.size());
            }
            continue;
        }
        double weight = 0, sum = 0;
        // The SAN of a move left out, from boards made for the first.
        std::optional<std::pair<chess::Board, ScanBoard>> boards;
        const auto sanAt = [&](int code) {
            if (!boards) {
                boards.emplace();
                boards->first.set960(repertoire.chess960());
                boards->first.setFen(pos.fen);
                boards->second.setFen(pos.fen, repertoire.chess960());
            }
            return sanOf(boards->first, boards->second, code);
        };
        for (const auto& m : played[i]->moves) {
            if (m.code == TreeMove::END) continue;
            const auto it = std::find_if(pos.moves.begin(), pos.moves.end(),
                                         [&](const RepertoireMove& r) { return r.code == m.code; });
            if (it != pos.moves.end()) {
                weight += static_cast<double>(m.stats.games);
                if (followed(pos, *it)) sum += static_cast<double>(m.stats.games) * answered[place.at(it->to)];
                continue;
            }
            if (!worth(i, m.stats.games)) continue;
            weight += static_cast<double>(m.stats.games);
            const double share = static_cast<double>(m.stats.games) / static_cast<double>(going[i]);
            out.gaps.push_back(
                {pos.key, repertoire.lineTo(pos.key), sanAt(m.code), m.stats.games, share, reach[i] * share});
        }
        if (weight > 0) answered[i] = sum / weight;
    }
    out.covered = answered[0];

    std::stable_sort(out.gaps.begin(), out.gaps.end(), [](const RepertoireGap& a, const RepertoireGap& b) {
        if (a.reach != b.reach) return a.reach > b.reach;
        return a.games > b.games;
    });
    if (out.gaps.size() > options.listed) out.gaps.resize(options.listed);
    return out;
}

}  // namespace fcg::study
