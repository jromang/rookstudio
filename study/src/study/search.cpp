#include "search.hpp"

#include <algorithm>
#include <bit>

#include <chess.hpp>

#include "background.hpp"
#include "signature.hpp"

namespace fcg::study {

namespace {

// The games a thread takes at a time: their search takes far less than a twentieth of a second.
constexpr uint64_t PART = 4096;

// Calls at(ply, board) for the start and after each move, up to ply `last`, until it returns true: true then.
template <typename At>
bool replay(std::span<const uint8_t> codes, uint32_t plies, uint32_t last, ScanBoard& board, At&& at) {
    std::size_t p = 0;
    for (uint32_t ply = 0;; ++ply) {
        if (at(ply, static_cast<const ScanBoard&>(board))) return true;
        if (ply >= last || ply >= plies || p >= codes.size()) return false;
        const int count = board.codeCount();
        int code        = codes[p++];
        if (count > 256) {
            if (p >= codes.size()) return false;
            code = (code << 8) | codes[p++];
        }
        if (code >= count) return false;
        board.play(board.decode(code));
    }
}

bool samePosition(const ScanBoard& a, const ScanBoard& b) noexcept {
    if (a.key() != b.key() || a.sideToMove() != b.sideToMove()) return false;
    for (int side = 0; side < 2; ++side)
        for (int kind = PAWN; kind <= KING; ++kind)
            if (a.pieces(side, kind) != b.pieces(side, kind)) return false;
    return true;  // castling and en passant are in the key
}

std::optional<uint32_t> findPosition(const Snapshot& snap, GameId id, const ScanBoard& target, bool use_signature) {
    const auto m  = snap.gameMoves(id);
    uint32_t from = 0, to = m.plies;
    if (use_signature) {
        if (const auto signature = snap.signature(id)) {
            const auto window = plyWindow(*signature, m.plies, target);
            if (!window) return std::nullopt;
            from = window->from;
            to   = std::min(window->to, m.plies);
        }
    }
    ScanBoard board;
    if (!startOf(snap, id, m.flags, board)) return std::nullopt;
    const uint32_t parity = static_cast<uint32_t>((target.sideToMove() + 2 - board.sideToMove()) % 2);
    std::optional<uint32_t> hit;
    replay(m.codes, m.plies, to, board, [&](uint32_t ply, const ScanBoard& b) {
        if (ply < from || ply % 2 != parity || !samePosition(b, target)) return false;
        hit = ply;
        return true;
    });
    return hit;
}

bool materialFits(const ScanBoard& b, const MaterialQuery& q) noexcept {
    for (int side = 0; side < 2; ++side) {
        for (int kind = PAWN; kind <= QUEEN; ++kind) {
            const int count = std::popcount(b.pieces(side, kind));
            const int i     = side * 5 + kind;
            if (count < q.low[i] || count > q.high[i]) return false;
        }
    }
    return true;
}

std::optional<uint32_t> findMaterial(const Snapshot& snap, GameId id, const MaterialQuery& q, bool use_signature) {
    const auto m = snap.gameMoves(id);
    if (use_signature) {
        if (const auto signature = snap.signature(id)) {
            // A side's pawns and men all told, and without promotions each kind, never go below the end's.
            for (int side = 0; side < 2; ++side) {
                int ending = 0, most = 0;
                for (int kind = PAWN; kind <= QUEEN; ++kind) {
                    const int i = side * 5 + kind;
                    if ((kind == PAWN || !signature->promoted) && signature->material[i] > q.high[i])
                        return std::nullopt;
                    ending += signature->material[i];
                    most += q.high[i];
                }
                if (ending > most) return std::nullopt;
            }
        }
    }
    ScanBoard board;
    if (!startOf(snap, id, m.flags, board)) return std::nullopt;
    const uint32_t needed = std::max<uint32_t>(1, q.plies);
    uint32_t run = 0, first = 0;
    std::optional<uint32_t> hit;
    replay(m.codes, m.plies, m.plies, board, [&](uint32_t ply, const ScanBoard& b) {
        if (!materialFits(b, q)) {
            run = 0;
            return false;
        }
        if (run++ == 0) first = ply;
        if (run < needed) return false;
        hit = first;
        return true;
    });
    return hit;
}

bool patternFits(const ScanBoard& b, const PatternQuery& q) noexcept {
    if (q.side_to_move >= 0 && b.sideToMove() != q.side_to_move) return false;
    if (b.occupied() & q.empty) return false;
    for (int side = 0; side < 2; ++side) {
        for (int kind = PAWN; kind <= KING; ++kind) {
            const uint64_t wanted = q.men[side][kind];
            if ((b.pieces(side, kind) & wanted) != wanted) return false;
        }
        if (q.only_these_pawns[side] && b.pieces(side, PAWN) != q.men[side][PAWN]) return false;
    }
    return true;
}

// The plies at which a game may show a pattern, as its signature tells: its home pawns' squares, and its pawns when a
// side has only those given.
std::optional<PlyWindow> patternWindow(const GameSignature& game, uint32_t plies, const PatternQuery& q) {
    int64_t from = 0, to = plies;
    for (int e = 0; e < 16; ++e) {
        const int side      = e < 8 ? WHITE_SIDE : BLACK_SIDE;
        const uint64_t bit  = uint64_t{1} << GameSignature::square(e);
        const bool its_pawn = (q.men[side][PAWN] & bit) != 0;
        bool something_else = (q.empty & bit) != 0 || (q.only_these_pawns[side] && !its_pawn);
        for (int s = 0; s < 2; ++s)
            for (int kind = PAWN; kind <= KING; ++kind)
                if ((s != side || kind != PAWN) && (q.men[s][kind] & bit)) something_else = true;
        const uint8_t gone = game.home[e];
        if (its_pawn) {
            if (gone != GameSignature::STAYED) to = std::min<int64_t>(to, int64_t{gone} - 1);
        } else if (something_else) {
            from = std::max<int64_t>(from, gone);
        }
    }
    for (int side = 0; side < 2; ++side) {
        const uint8_t ending = game.material[side * 5 + PAWN];
        if (q.only_these_pawns[side] && ending < 15 && std::popcount(q.men[side][PAWN]) < ending) return std::nullopt;
    }
    if (from > to) return std::nullopt;
    return PlyWindow{static_cast<uint32_t>(from), static_cast<uint32_t>(to)};
}

std::optional<uint32_t> findPattern(const Snapshot& snap, GameId id, const PatternQuery& q, bool use_signature) {
    const auto m  = snap.gameMoves(id);
    uint32_t from = 0, to = m.plies;
    if (use_signature) {
        if (const auto signature = snap.signature(id)) {
            const auto window = patternWindow(*signature, m.plies, q);
            if (!window) return std::nullopt;
            from = window->from;
            to   = std::min(window->to, m.plies);
        }
    }
    ScanBoard board;
    if (!startOf(snap, id, m.flags, board)) return std::nullopt;
    std::optional<uint32_t> hit;
    replay(m.codes, m.plies, to, board, [&](uint32_t ply, const ScanBoard& b) {
        if (ply < from || !patternFits(b, q)) return false;
        hit = ply;
        return true;
    });
    return hit;
}

// ---------------------------------------------------------------------------------------------------------------
// Patterns of the language

inline uint64_t sq(int square) noexcept { return uint64_t{1} << square; }

// A name's man followed through a move: where it goes, or gone when the move takes it.
void follow(Binding& b, const ScanBoard& before, const CodedMove& move) noexcept {
    if (move.flags & MOVE_NULL) return;
    if (move.flags & (MOVE_SHORT_CASTLE | MOVE_LONG_CASTLE)) {
        const int rank    = before.sideToMove() == WHITE_SIDE ? 0 : 7;
        const bool short_ = (move.flags & MOVE_SHORT_CASTLE) != 0;
        const int king_to = rank * 8 + (short_ ? 6 : 2);
        const int rook_to = rank * 8 + (short_ ? 5 : 3);
        for (auto& s : b.square) {
            if (s == move.from)
                s = static_cast<int8_t>(king_to);
            else if (s == move.to)
                s = static_cast<int8_t>(rook_to);
        }
        return;
    }
    int taken = -1;
    if (move.flags & MOVE_EN_PASSANT)
        taken = move.to + (before.sideToMove() == WHITE_SIDE ? -8 : 8);
    else if (before.on(move.to) != 0)
        taken = move.to;
    for (auto& s : b.square) {
        if (s < 0) continue;
        if (s == taken) s = -1;
        if (s == move.from) s = static_cast<int8_t>(move.to);
    }
}

// The plies a game may show the first position of a pattern at, as its signature tells: the men it needs on their
// squares.
std::optional<PlyWindow> relationWindow(const GameSignature& game, uint32_t plies, const PatternMatcher& m) {
    const auto& needs = m.needs();
    PatternQuery q;
    q.men              = needs.men;
    q.empty            = needs.empty;
    q.only_these_pawns = needs.only_these_pawns;
    q.side_to_move     = needs.side_to_move;
    return patternWindow(game, plies, q);
}

// Whether the steps of a line from `k` on follow the position `board` at ply `ply`, the names bound as `binding`.
// `moves`: the game's moves as far as they were read; `plies`: its length, whose last position alone can be mate.
bool followLine(const PatternMatcher& m, std::size_t k, ScanBoard board, Binding binding, uint32_t ply,
                const std::vector<CodedMove>& moves, uint32_t plies) {
    const auto& steps = m.pattern().line;
    if (k == steps.size()) return true;
    const auto& step = steps[k];
    for (uint32_t gap = 0; gap <= step.max_gap; ++gap) {
        if (ply >= moves.size()) return false;
        const auto& move = moves[ply];
        ScanBoard after  = board;
        after.play(move);
        Binding followed = binding;
        follow(followed, board, move);
        if (gap >= step.min_gap && (!step.move || m.moveFits(*step.move, board, move, binding)) &&
            m.holds(step.conditions, after, followed, ply + 1 == plies) &&
            followLine(m, k + 1, after, followed, ply + 1, moves, plies))
            return true;
        board   = after;
        binding = followed;
        ++ply;
    }
    return false;
}

std::optional<uint32_t> findRelation(const Snapshot& snap, GameId id, const PatternMatcher& m, bool use_signature) {
    const auto g  = snap.gameMoves(id);
    uint32_t from = 0, to = g.plies;
    if (use_signature) {
        if (const auto signature = snap.signature(id)) {
            const auto window = relationWindow(*signature, g.plies, m);
            if (!window) return std::nullopt;
            from = window->from;
            to   = std::min(window->to, g.plies);
        }
    }
    ScanBoard board;
    if (!startOf(snap, id, g.flags, board)) return std::nullopt;
    const auto& steps = m.pattern().line;
    if (steps.empty()) {
        // Mate and stalemate: the last position only.
        if (m.asksMate() && (to < g.plies || from > g.plies)) return std::nullopt;
        if (m.asksMate()) from = g.plies;
        std::optional<uint32_t> hit;
        replay(g.codes, g.plies, to, board, [&](uint32_t ply, const ScanBoard& b) {
            if (ply < from || !m.first(b, ply == g.plies, [](const Binding&) { return true; })) return false;
            hit = ply;
            return true;
        });
        return hit;
    }
    // A line: the first positions that fit, with their bindings, then the steps after each, in order.
    uint32_t span = 0;
    for (const auto& s : steps) span += s.max_gap + 1;
    const uint32_t last = std::min<uint32_t>(g.plies, to + span);
    struct Candidate {
        uint32_t ply;
        ScanBoard board;
        Binding binding;
    };
    thread_local std::vector<CodedMove> moves;
    thread_local std::vector<Candidate> candidates;
    moves.clear();
    candidates.clear();
    std::size_t p = 0;
    for (uint32_t ply = 0;; ++ply) {
        if (ply >= from && ply <= to)
            m.first(board, ply == g.plies, [&](const Binding& b) {
                candidates.push_back({ply, board, b});
                return false;  // every binding
            });
        if (ply >= last || ply >= g.plies || p >= g.codes.size()) break;
        const int count = board.codeCount();
        int code        = g.codes[p++];
        if (count > 256) {
            if (p >= g.codes.size()) break;
            code = (code << 8) | g.codes[p++];
        }
        if (code >= count) break;
        const auto move = board.decode(code);
        moves.push_back(move);
        board.play(move);
    }
    for (const auto& c : candidates)
        if (followLine(m, 0, c.board, c.binding, c.ply, moves, g.plies)) return c.ply;
    return std::nullopt;
}

// ---------------------------------------------------------------------------------------------------------------
// A man's route

struct Found {
    std::optional<uint32_t> ply;
    uint8_t level    = 0;
    bool started     = false;
    int next         = -1;
    uint32_t variant = UINT32_MAX;
    std::vector<std::vector<uint8_t>> parted;
};

// Where a move takes the man on `square`, if it moves it: a castling moves the king and its rook.
int destinationOf(const ScanBoard& before, const CodedMove& move, int square) noexcept {
    if (move.flags & MOVE_NULL) return -1;
    if (move.flags & (MOVE_SHORT_CASTLE | MOVE_LONG_CASTLE)) {
        const int rank    = before.sideToMove() == WHITE_SIDE ? 0 : 7;
        const bool short_ = (move.flags & MOVE_SHORT_CASTLE) != 0;
        if (square == move.from) return rank * 8 + (short_ ? 6 : 2);
        if (square == move.to) return rank * 8 + (short_ ? 5 : 3);
        return -1;
    }
    return square == move.from ? move.to : -1;
}

// The square a move takes a man on, -1 for none.
int takenBy(const ScanBoard& before, const CodedMove& move) noexcept {
    if (move.flags & (MOVE_NULL | MOVE_SHORT_CASTLE | MOVE_LONG_CASTLE)) return -1;
    if (move.flags & MOVE_EN_PASSANT) return move.to + (before.sideToMove() == WHITE_SIDE ? -8 : 8);
    return before.on(move.to) != 0 ? move.to : -1;
}

Found findManoeuvre(const Snapshot& snap, GameId id, const ManoeuvreQuery& q, bool use_signature, bool stats) {
    Found found;
    if (q.route.size() < 2) return found;
    const auto g   = snap.gameMoves(id);
    uint32_t start = 0;
    if (q.from) {
        const auto at = findPosition(snap, id, *q.from, use_signature);
        if (!at) return found;
        start = *at;
    }
    ScanBoard board;
    if (!startOf(snap, id, g.flags, board)) return found;
    const auto man = static_cast<uint8_t>(1 + q.kind + 6 * q.side);
    struct Attempt {
        int square;        // the man followed (same_man), else the last square reached
        std::size_t next;  // the route's square it goes to next
        uint32_t moves;    // the side's moves since it set off
    };
    std::vector<Attempt> attempts;
    std::vector<std::vector<uint8_t>> parted;
    int done_on   = -1;  // the route done: the square of its man, whose next move is looked for
    std::size_t p = 0;
    for (uint32_t ply = 0; ply < g.plies && p < g.codes.size(); ++ply) {
        const int count = board.codeCount();
        int code        = g.codes[p++];
        if (count > 256) {
            if (p >= g.codes.size()) break;
            code = (code << 8) | g.codes[p++];
        }
        if (code >= count) break;
        const auto move = board.decode(code);
        if (ply >= start) {
            const bool ours = board.sideToMove() == q.side;
            if (done_on >= 0) {
                // After the route: the man's next move, unless it is taken first.
                if (ours) {
                    const int to = destinationOf(board, move, done_on);
                    if (to >= 0) {
                        found.next = to;
                        break;
                    }
                } else if (takenBy(board, move) == done_on) {
                    break;
                }
            } else if (ours) {
                const bool moving_kind = board.on(move.from) == man && !(move.flags & MOVE_NULL);
                for (auto a = attempts.begin(); a != attempts.end();) {
                    bool keep = true;
                    ++a->moves;
                    int to = -1;
                    if (q.same_man)
                        to = destinationOf(board, move, a->square);
                    else if (moving_kind)
                        to = destinationOf(board, move, move.from);
                    if (to >= 0 && to == q.route[a->next]) {
                        a->square = to;
                        ++a->next;
                    } else if (q.same_man && to >= 0) {
                        if (stats) {  // the man left the route: a route parted from it
                            std::vector<uint8_t> r(q.route.begin(), q.route.begin() + static_cast<long>(a->next));
                            r.push_back(static_cast<uint8_t>(to));
                            if (std::find(parted.begin(), parted.end(), r) == parted.end())
                                parted.push_back(std::move(r));
                        }
                        keep = false;
                    } else if (!q.others_between) {
                        keep = false;
                    }
                    if (keep && a->moves > q.within) keep = false;
                    if (keep && a->next == q.route.size()) {
                        found.ply = ply + 1;
                        done_on   = a->square;
                        break;
                    }
                    a = keep ? a + 1 : attempts.erase(a);
                }
                // A man of that kind setting off along the route.
                if (!found.ply && moving_kind && move.from == q.route[0]) {
                    const int to = destinationOf(board, move, move.from);
                    if (to == q.route[1]) {
                        found.started = true;
                        if (q.route.size() == 2) {
                            found.ply = ply + 1;
                            done_on   = to;
                        } else {
                            attempts.push_back({to, 2, 1});
                        }
                    }
                }
                if (found.ply && !stats) break;
            } else if (q.same_man) {
                // The opponent's move: a man followed that it takes is gone.
                const int taken = takenBy(board, move);
                if (taken >= 0) std::erase_if(attempts, [&](const Attempt& a) { return a.square == taken; });
            }
        }
        board.play(move);
    }
    if (stats) found.parted = std::move(parted);
    return found;
}

// ---------------------------------------------------------------------------------------------------------------
// A pawn structure

inline uint64_t mirrored(uint64_t b) noexcept { return __builtin_bswap64(b); }  // the board turned: a2 to a7

inline int sideDistance(uint64_t wanted, uint64_t pawns) noexcept {
    return std::max(std::popcount(wanted & ~pawns), std::popcount(pawns & ~wanted));
}

// How near a position's pawns are to the structure: the pawns of difference, as asked or with the colours reversed.
struct Nearness {
    int level    = 99;
    bool reverse = false;  // the reversed colours are the nearer
};

Nearness nearness(const StructureQuery& q, uint64_t white, uint64_t black) noexcept {
    const auto as_is = [&](uint64_t w, uint64_t b) {
        if (q.both_sides) return sideDistance(q.pawns[0], w) + sideDistance(q.pawns[1], b);
        return q.side == WHITE_SIDE ? sideDistance(q.pawns[0], w) : sideDistance(q.pawns[1], b);
    };
    Nearness n{as_is(white, black), false};
    if (q.reversed) {
        const int r = as_is(mirrored(black), mirrored(white));
        if (r < n.level) n = {r, true};
    }
    return n;
}

// The plies a game may hold a structure at, as its signature tells: a pawn of the structure on its starting square
// is there until it leaves, a starting square the structure leaves empty holds its pawn until then; the end's pawns
// bound how few a position has.
std::optional<PlyWindow> structureWindow(const GameSignature& game, uint32_t plies, const StructureQuery& q) {
    std::optional<PlyWindow> window;
    for (const bool reverse : {false, true}) {
        if (reverse && !q.reversed) break;
        // The structure as the game's colours see it.
        const std::array<uint64_t, 2> wanted =
            reverse ? std::array<uint64_t, 2>{mirrored(q.pawns[1]), mirrored(q.pawns[0])} : q.pawns;
        // The bound changes where a pawn leaves its square, and after ply 254 (where a pawn told as staying may go).
        std::vector<uint32_t> cuts{0, plies + 1};
        for (const auto gone : game.home)
            if (gone != 0 && gone != GameSignature::STAYED) cuts.push_back(gone);
        if (plies >= 255) cuts.push_back(255);
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
        for (std::size_t c = 0; c + 1 < cuts.size(); ++c) {
            const uint32_t ply = cuts[c];
            if (ply > plies) break;
            int bound = 0;
            for (int side = 0; side < 2; ++side) {
                const bool compared = q.both_sides || (reverse ? side != q.side : side == q.side);
                if (!compared) continue;
                int missing = 0, extra = 0;
                for (int e = side * 8; e < side * 8 + 8; ++e) {
                    const uint8_t gone = game.home[e];
                    if (gone == 0) continue;
                    const bool wants      = (wanted[static_cast<std::size_t>(side)] >> GameSignature::square(e)) & 1;
                    const bool there      = gone == GameSignature::STAYED ? ply <= 254 : ply < gone;
                    const bool known_gone = gone != GameSignature::STAYED && ply >= gone;
                    if (wants && known_gone) ++missing;
                    if (!wants && there) ++extra;
                }
                const int ending = game.material[static_cast<std::size_t>(side) * 5 + PAWN];
                const int fewest = ending < 15 ? ending - std::popcount(wanted[static_cast<std::size_t>(side)]) : 0;
                bound += std::max({missing, extra, fewest});
            }
            if (bound > q.distance) continue;
            const uint32_t last = cuts[c + 1] - 1;
            if (!window)
                window = PlyWindow{ply, last};
            else {
                window->from = std::min(window->from, ply);
                window->to   = std::max(window->to, last);
            }
        }
    }
    return window;
}

Found findStructure(const Snapshot& snap, GameId id, const StructureQuery& q, bool use_signature, bool stats) {
    Found found;
    const auto g  = snap.gameMoves(id);
    uint32_t from = 0, to = g.plies;
    if (use_signature) {
        if (const auto signature = snap.signature(id)) {
            const auto window = structureWindow(*signature, g.plies, q);
            if (!window) return found;
            from = window->from;
            to   = std::min(window->to, g.plies);
        }
    }
    ScanBoard board;
    if (!startOf(snap, id, g.flags, board)) return found;
    const uint32_t needed = std::max<uint32_t>(1, q.plies);
    // For each level up to the distance: the run of positions in a row at that level or nearer, and where it began.
    std::array<uint32_t, 3> run{}, began{};
    std::array<uint64_t, 3> first_white{}, first_black{};
    std::array<bool, 3> first_reverse{};
    int best = 99;
    replay(g.codes, g.plies, to, board, [&](uint32_t ply, const ScanBoard& b) {
        if (ply < from) return false;
        const uint64_t white = b.pieces(WHITE_SIDE, PAWN), black = b.pieces(BLACK_SIDE, PAWN);
        const auto n = nearness(q, white, black);
        for (int level = 0; level <= q.distance && level < best; ++level) {
            if (n.level > level) {
                run[static_cast<std::size_t>(level)] = 0;
                continue;
            }
            auto& r = run[static_cast<std::size_t>(level)];
            if (r++ == 0) {
                began[static_cast<std::size_t>(level)]         = ply;
                first_white[static_cast<std::size_t>(level)]   = white;
                first_black[static_cast<std::size_t>(level)]   = black;
                first_reverse[static_cast<std::size_t>(level)] = n.reverse;
            }
            if (r >= needed) {
                best      = level;
                found.ply = began[static_cast<std::size_t>(level)];
                break;
            }
        }
        return best == 0;
    });
    if (!found.ply) return found;
    found.level = static_cast<uint8_t>(best);
    if (stats && best == 1) {
        // What differs, in the structure's colours.
        const auto l   = static_cast<std::size_t>(best);
        const bool rev = first_reverse[l];
        const std::array<uint64_t, 2> pawns =
            rev ? std::array<uint64_t, 2>{mirrored(first_black[l]), mirrored(first_white[l])}
                : std::array<uint64_t, 2>{first_white[l], first_black[l]};
        for (int side = 0; side < 2; ++side) {
            const uint64_t gone  = q.pawns[static_cast<std::size_t>(side)] & ~pawns[static_cast<std::size_t>(side)];
            const uint64_t added = pawns[static_cast<std::size_t>(side)] & ~q.pawns[static_cast<std::size_t>(side)];
            if (!q.both_sides && side != q.side) continue;
            if (!gone && !added) continue;
            found.variant = variantKey(side, gone ? __builtin_ctzll(gone) : 64, added ? __builtin_ctzll(added) : 64);
        }
    }
    return found;
}

Found find(const Snapshot& snapshot, GameId id, const SearchQuery& query, bool use_signature, bool stats) {
    Found f;
    if (const auto* p = std::get_if<PositionQuery>(&query))
        f.ply = findPosition(snapshot, id, p->position, use_signature);
    else if (const auto* m = std::get_if<MaterialQuery>(&query))
        f.ply = findMaterial(snapshot, id, *m, use_signature);
    else if (const auto* t = std::get_if<PatternQuery>(&query))
        f.ply = findPattern(snapshot, id, *t, use_signature);
    else if (const auto* r = std::get_if<RelationQuery>(&query))
        f.ply = r->matcher ? findRelation(snapshot, id, *r->matcher, use_signature) : std::nullopt;
    else if (const auto* route = std::get_if<ManoeuvreQuery>(&query))
        f = findManoeuvre(snapshot, id, *route, use_signature, stats);
    else
        f = findStructure(snapshot, id, std::get<StructureQuery>(query), use_signature, stats);
    return f;
}

}  // namespace

void SearchStats::add(const SearchStats& o) {
    for (std::size_t i = 0; i < results.size(); ++i) results[i] += o.results[i];
    exact += o.exact;
    for (const auto& [eco, n] : o.ecos) ecos[eco] += n;
    for (const auto& [key, r] : o.variants) {
        auto& mine = variants[key];
        for (std::size_t i = 0; i < mine.size(); ++i) mine[i] += r[i];
    }
    started += o.started;
    for (std::size_t i = 0; i < next.size(); ++i) next[i] += o.next[i];
    for (const auto& [route, n] : o.parted) parted[route] += n;
}

std::optional<uint32_t> searchGame(const Snapshot& snapshot, GameId id, const SearchQuery& query, bool use_signature) {
    return find(snapshot, id, query, use_signature, false).ply;
}

std::optional<SearchHit> findInGame(const Snapshot& snapshot, GameId id, const SearchQuery& query, bool use_signature) {
    const auto f = find(snapshot, id, query, use_signature, false);
    if (!f.ply) return std::nullopt;
    return SearchHit{id, *f.ply, f.level};
}

Search::Search(std::shared_ptr<const Snapshot> snapshot, SearchQuery query, int threads, const Search* resume)
    : snapshot_(std::move(snapshot)), query_(std::move(query)) {
    parts_.resize((snapshot_->games() + PART - 1) / PART);
    stats_.resize(parts_.size());
    done_.assign(parts_.size(), 0);
    if (resume && resume->snapshot_ == snapshot_) {
        std::lock_guard lock(resume->mutex_);
        uint64_t kept = 0;
        for (std::size_t part = 0; part < parts_.size() && part < resume->done_.size(); ++part) {
            if (!resume->done_[part]) continue;
            parts_[part] = resume->parts_[part];
            stats_[part] = resume->stats_[part];
            done_[part]  = 1;
            kept += std::min<uint64_t>(snapshot_->games(), (part + 1) * PART) - part * PART;
        }
        searched_ = kept;
    }
    const int machine = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    const auto count  = std::min<std::size_t>(static_cast<std::size_t>(threads > 0 ? threads : machine), parts_.size());
    planned_          = count;
    threads_.reserve(count);
    const Inherited inherited;
    for (std::size_t t = 0; t < count; ++t)
        threads_.emplace_back([this, inherited] {
            inherited.apply();
            work();
        });
}

Search::~Search() {
    cancel();
    wait();
}

void Search::cancel() noexcept { cancel_ = true; }

void Search::wait() {
    for (auto& t : threads_)
        if (t.joinable()) t.join();
}

void Search::work() {
    const uint64_t n = snapshot_->games();
    // What a route or a structure tells besides its games.
    const bool route     = std::holds_alternative<ManoeuvreQuery>(query_);
    const bool structure = std::holds_alternative<StructureQuery>(query_);
    for (;;) {
        const uint64_t part = next_++;
        if (cancel_ || part >= parts_.size()) break;
        if (done_[part]) continue;  // kept from the search resumed
        std::vector<SearchHit> found;
        SearchStats stats;
        const uint64_t from = part * PART, to = std::min(n, from + PART);
        uint64_t looked = 0;
        for (GameId id = from; id < to && !cancel_; ++id, ++looked) {
            if (snapshot_->deleted(id)) continue;
            auto f = find(*snapshot_, id, query_, true, route || structure);
            if (route) {
                if (f.started || f.ply) ++stats.started;
                for (auto& r : f.parted) ++stats.parted[std::move(r)];
            }
            if (!f.ply) continue;
            found.push_back({id, *f.ply, f.level});
            const auto numbers       = snapshot_->numbers(id);
            const std::size_t result = numbers.result == '1'   ? 0
                                       : numbers.result == '=' ? 1
                                       : numbers.result == '0' ? 2
                                                               : 3;
            ++stats.results[result];
            if (route && f.next >= 0) ++stats.next[static_cast<std::size_t>(f.next)];
            if (structure) {
                if (f.level == 0) ++stats.exact;
                ++stats.ecos[numbers.eco];
                if (f.variant != UINT32_MAX) ++stats.variants[f.variant][result];
            }
        }
        if (cancel_ && looked < to - from) break;  // a part cut short: searched again when resumed
        searched_ += looked;
        std::lock_guard lock(mutex_);
        parts_[part] = std::move(found);
        stats_[part] = std::move(stats);
        done_[part]  = 1;
    }
    if (++finished_ == planned_)
        elapsed_us_ =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started_).count();
}

std::vector<SearchHit> Search::hits() const {
    std::lock_guard lock(mutex_);
    std::vector<SearchHit> all;
    for (const auto& part : parts_) all.insert(all.end(), part.begin(), part.end());
    return all;
}

void Search::hitsFrom(std::size_t& part, std::vector<SearchHit>& out) const {
    std::lock_guard lock(mutex_);
    for (; part < parts_.size() && done_[part]; ++part) out.insert(out.end(), parts_[part].begin(), parts_[part].end());
}

SearchStats Search::stats() const {
    std::lock_guard lock(mutex_);
    SearchStats all;
    for (std::size_t part = 0; part < stats_.size(); ++part)
        if (done_[part]) all.add(stats_[part]);
    return all;
}

std::vector<SearchHit> searchAll(std::shared_ptr<const Snapshot> snapshot, const SearchQuery& query, int threads) {
    Search search(std::move(snapshot), query, threads);
    search.wait();
    return search.hits();
}

}  // namespace fcg::study
