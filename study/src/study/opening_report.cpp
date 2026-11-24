#include "opening_report.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include <chess.hpp>

#include "background.hpp"
#include "search.hpp"

namespace fcg::study {

namespace {

constexpr std::size_t REPLAY_PART = 64;
// A move played in more games than this is not looked at for a novelty: it is the theory of many years.
constexpr uint64_t NOVELTY_GAMES = 20000;
// The games of the user's looked at for the move that left the databases: their latest.
constexpr std::size_t USER_GAMES = 20;
// A novelty's position this many plies or more before the trees' horizon needs no position search: no game wastes as
// many moves to reach it by other moves.
constexpr uint32_t NOVELTY_MARGIN = 12;
// The games of a pawn plan whose pushes the caller names: an even sample.
constexpr std::size_t NAMED_SAMPLE = 64;
constexpr uint8_t NO_SQUARE        = 64;

// The board a game starts from; false when its start cannot be set.
bool startOf(const Snapshot& snap, GameId id, uint16_t flags, ScanBoard& board) {
    static const ScanBoard standard = [] {
        ScanBoard b;
        b.setFen(chess::constants::STARTPOS, false);
        return b;
    }();
    static const ScanBoard standard960 = [] {
        ScanBoard b;
        b.setFen(chess::constants::STARTPOS, true);
        return b;
    }();
    const bool chess960 = (flags & GAME_CHESS960) != 0;
    if (flags & GAME_FEN) return board.setFen(snap.game(id).start_fen, chess960);
    board = chess960 ? standard960 : standard;
    return true;
}

// Runs `work(thread)` on `threads` threads, the background of the caller kept.
template <typename Work>
void onThreads(int threads, Work&& work) {
    if (threads <= 1) {
        work(0);
        return;
    }
    const Inherited background;
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&, t] {
            background.apply();
            work(t);
        });
    for (auto& th : pool) th.join();
}

// A game's moves read one after the other on the board they are played on.
class Cursor {
   public:
    explicit Cursor(const GameMoves& m) : codes_(m.codes), plies_(m.plies) {}
    // The next move's code on `board`; none at the game's end, or for a code that cannot be read.
    std::optional<int> next(const ScanBoard& board) {
        if (ply_ >= plies_ || p_ >= codes_.size()) return std::nullopt;
        const int count = board.codeCount();
        int code        = codes_[p_++];
        if (count > 256) {
            if (p_ >= codes_.size()) return std::nullopt;
            code = (code << 8) | codes_[p_++];
        }
        if (code >= count) return std::nullopt;
        ++ply_;
        return code;
    }
    [[nodiscard]] uint32_t ply() const noexcept { return ply_; }

   private:
    std::span<const uint8_t> codes_;
    uint32_t plies_ = 0;
    std::size_t p_  = 0;
    uint32_t ply_   = 0;
};

// The game replayed up to the ply it reaches the report's position at: the board there, and its cursor.
bool replayTo(const Snapshot& snap, const ReportGame& g, ScanBoard& board, std::optional<Cursor>& cursor) {
    const auto moves = snap.gameMoves(g.id);
    if (!startOf(snap, g.id, moves.flags, board)) return false;
    cursor.emplace(moves);
    while (cursor->ply() < g.ply) {
        const auto code = cursor->next(board);
        if (!code) return false;
        board.play(board.decode(*code));
    }
    return true;
}

// The players' mean rating, one alone when the other has none; 0 for none (as the reference games rank them).
int meanRating(const HeaderNumbers& n) {
    if (n.white_elo && n.black_elo) return (n.white_elo + n.black_elo) / 2;
    return n.white_elo ? n.white_elo : n.black_elo;
}

// The trees' answers at a position, added move by move: none when no tree has it.
std::optional<TreeAnswer> treesAt(std::span<const ReportSource> sources, uint64_t key,
                                  std::vector<uint64_t>* per_source = nullptr) {
    std::optional<TreeAnswer> out;
    for (std::size_t s = 0; s < sources.size(); ++s) {
        const auto& src = sources[s];
        if (!src.tree || !src.snapshot || src.tree->stale(*src.snapshot)) continue;
        const auto one = src.tree->at(*src.snapshot, key);
        if (!one) continue;
        if (per_source) (*per_source)[s] = one->total.games;
        if (!out) out.emplace();
        out->total.add(one->total);
        for (const auto& m : one->moves) {
            auto at =
                std::find_if(out->moves.begin(), out->moves.end(), [&](const TreeMove& t) { return t.code == m.code; });
            if (at == out->moves.end()) at = out->moves.insert(out->moves.end(), {m.code, {}});
            at->stats.add(m.stats);
        }
    }
    if (out)
        std::stable_sort(out->moves.begin(), out->moves.end(), [](const TreeMove& x, const TreeMove& y) {
            if ((x.code == TreeMove::END) != (y.code == TreeMove::END)) return y.code == TreeMove::END;
            return x.stats.games > y.stats.games;
        });
    return out;
}

// The games of a snapshot at a position, by a search of every game; none when cancelled.
std::optional<std::vector<SearchHit>> searchPosition(const std::shared_ptr<const Snapshot>& snapshot,
                                                     const ScanBoard& position, int threads,
                                                     const std::atomic<bool>& cancel) {
    Search search(snapshot, PositionQuery{position}, threads);
    while (!search.done()) {
        if (cancel.load(std::memory_order_relaxed)) {
            search.cancel();
            search.wait();
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    search.wait();
    return search.hits();
}

// What a replayed game did after the position.
struct Replayed {
    ReportGame game;
    HeaderNumbers numbers;
    bool reached  = false;  // the structure's ply
    int structure = -1, owner = WHITE_SIDE;
    // Each side's pawn pushes in their order (from << 6 | to), and its men's routes (routeKey()).
    std::array<std::vector<uint16_t>, 2> pushes;
    std::array<std::vector<uint32_t>, 2> routes;
};

// A man's route: its kind, its squares (two or three; NO_SQUARE for none).
uint32_t routeKey(int kind, uint8_t a, uint8_t b, uint8_t c) {
    return static_cast<uint32_t>(kind) << 20 | static_cast<uint32_t>(a) << 13 | static_cast<uint32_t>(b) << 7 | c;
}
int routeKind(uint32_t key) { return static_cast<int>(key >> 20); }
std::vector<uint8_t> routeSquares(uint32_t key) {
    std::vector<uint8_t> out{static_cast<uint8_t>(key >> 13 & 63), static_cast<uint8_t>(key >> 7 & 63)};
    if ((key & 127) != NO_SQUARE) out.push_back(static_cast<uint8_t>(key & 127));
    return out;
}

// The plies after the position at which the structure is read: move 20, or ten moves on for a later position.
uint32_t structureOffset(const ScanBoard& position, uint32_t structure_ply) {
    const uint32_t ply = static_cast<uint32_t>(std::max(0, (position.fullMove() - 1) * 2 + position.sideToMove()));
    return ply + 10 <= structure_ply ? structure_ply - ply : 10;
}

// Replays a game: the structure at `structure_at` plies after the position, the pushes and routes of its `plan_plies`.
bool replayGame(const Snapshot& snap, const ReportGame& g, uint32_t plan_plies, uint32_t structure_at,
                const StructureNamer& namer, Replayed& out) {
    ScanBoard board;
    std::optional<Cursor> cursor;
    if (!replayTo(snap, g, board, cursor)) return false;
    out.game    = g;
    out.numbers = snap.numbers(g.id);
    // The men followed: knights, bishops, rooks and queens, from where they stand.
    struct Man {
        uint8_t kind = 0, side = 0, length = 0;
        std::array<uint8_t, 3> squares{};
        bool alive = true;
    };
    std::vector<Man> men;
    std::array<uint8_t, 64> at;
    at.fill(255);
    for (int sq = 0; sq < 64; ++sq) {
        const auto p = board.on(sq);
        if (!p) continue;
        const int kind = (p - 1) % 6;
        if (kind == PAWN || kind == KING) continue;
        at[sq] = static_cast<uint8_t>(men.size());
        men.push_back({static_cast<uint8_t>(kind), static_cast<uint8_t>((p - 1) / 6), 1, {static_cast<uint8_t>(sq)}});
    }
    std::vector<std::pair<int, uint16_t>> seen;
    const uint32_t end = std::max(plan_plies, structure_at);
    for (uint32_t step = 0; step <= end; ++step) {
        if (step == structure_at) {
            out.reached = true;
            std::vector<std::pair<int, int>> shapes;
            if (namer) namer(board.pieces(WHITE_SIDE, PAWN), board.pieces(BLACK_SIDE, PAWN), shapes);
            if (!shapes.empty()) {
                out.structure = shapes.front().first;
                out.owner     = shapes.front().second;
            }
        }
        if (step == end) break;
        const auto code = cursor->next(board);
        if (!code) break;
        const auto move = board.decode(*code);
        if (step < plan_plies && !(move.flags & MOVE_NULL)) {
            const int side     = board.sideToMove();
            const auto mover   = board.on(move.from);
            const bool castles = (move.flags & (MOVE_SHORT_CASTLE | MOVE_LONG_CASTLE)) != 0;
            if (castles) {
                // The rook castling ends its route: castling is no manoeuvre.
                if (at[move.to] != 255) men[at[move.to]].alive = false;
            } else {
                const bool takes = board.on(move.to) != 0 || (move.flags & MOVE_EN_PASSANT);
                if (mover && (mover - 1) % 6 == PAWN && !takes)
                    out.pushes[side].push_back(static_cast<uint16_t>(move.from << 6 | move.to));
                if (board.on(move.to) && at[move.to] != 255) men[at[move.to]].alive = false;  // taken
                if (at[move.from] != 255) {
                    auto& m = men[at[move.from]];
                    if (m.alive && m.length < 3) m.squares[m.length++] = move.to;
                    if (move.promotion != NO_KIND) m.alive = false;
                    at[move.to]   = at[move.from];
                    at[move.from] = 255;
                } else {
                    at[move.to] = 255;
                }
            }
        }
        board.play(move);
    }
    for (const auto& m : men) {
        if (m.length < 2) continue;
        auto& r = out.routes[m.side];
        r.push_back(routeKey(m.kind, m.squares[0], m.squares[1], NO_SQUARE));
        if (m.length >= 3) r.push_back(routeKey(m.kind, m.squares[0], m.squares[1], m.squares[2]));
    }
    return true;
}

// Whether a game's pushes hold a plan's: one push to the square, or two pushes in that order.
bool pushesHold(const std::vector<uint16_t>& pushes, const std::vector<uint8_t>& squares) {
    std::size_t want = 0;
    for (const auto p : pushes)
        if (want < squares.size() && (p & 63) == squares[want]) ++want;
    return want == squares.size();
}

// A side's plans from its games' pushes and routes: the most played first, each named by its moves.
std::vector<ReportPlan> sidePlans(const std::vector<Replayed>& games, int side, const ReportOptions& options) {
    std::array<uint64_t, 64> singles{};
    std::unordered_map<uint16_t, uint64_t> pairs;
    std::unordered_map<uint32_t, uint64_t> routes;
    std::vector<uint16_t> mine;
    for (const auto& g : games) {
        const auto& p = g.pushes[side];
        mine.clear();
        std::array<bool, 64> single{};
        for (std::size_t i = 0; i < p.size(); ++i) {
            single[p[i] & 63] = true;
            for (std::size_t j = i + 1; j < p.size(); ++j)
                if ((p[i] & 63) != (p[j] & 63)) mine.push_back(static_cast<uint16_t>((p[i] & 63) << 6 | (p[j] & 63)));
        }
        for (int sq = 0; sq < 64; ++sq) singles[sq] += single[sq];
        std::sort(mine.begin(), mine.end());
        mine.erase(std::unique(mine.begin(), mine.end()), mine.end());
        for (const auto k : mine) ++pairs[k];
        auto r = g.routes[side];
        std::sort(r.begin(), r.end());
        r.erase(std::unique(r.begin(), r.end()), r.end());
        for (const auto k : r) ++routes[k];
    }
    struct Candidate {
        ReportPlan::Kind kind;
        uint32_t key;
        uint64_t count;
    };
    std::vector<Candidate> candidates;
    // A push most of whose games play another with it is told as the two; a route most of whose games go on, as the
    // longer one.
    for (int sq = 0; sq < 64; ++sq) {
        if (!singles[sq]) continue;
        Candidate best{ReportPlan::Kind::Push, static_cast<uint32_t>(sq), singles[sq]};
        uint64_t most = 0;
        for (const auto& [k, n] : pairs)
            if (((k >> 6) == sq || (k & 63) == sq) && (n > most || (n == most && k < best.key))) {
                most = n;
                best = {ReportPlan::Kind::Pushes, k, n};
            }
        if (best.kind == ReportPlan::Kind::Pushes && most * 10 < singles[sq] * 6)
            best = {ReportPlan::Kind::Push, static_cast<uint32_t>(sq), singles[sq]};
        candidates.push_back(best);
    }
    for (const auto& [k, n] : routes) {
        if ((k & 127) != NO_SQUARE) continue;
        Candidate best{ReportPlan::Kind::Route, k, n};
        uint64_t most   = 0;
        uint32_t longer = 0;
        for (const auto& [k3, n3] : routes)
            if ((k3 & 127) != NO_SQUARE && (k3 >> 7) == (k >> 7) && (n3 > most || (n3 == most && k3 < longer))) {
                most   = n3;
                longer = k3;
            }
        if (most * 10 >= n * 6) best = {ReportPlan::Kind::Route, longer, most};
        candidates.push_back(best);
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.count != b.count) return a.count > b.count;
        if (a.kind != b.kind) return a.kind < b.kind;
        return a.key < b.key;
    });
    const double least = std::max(1.0, options.least_share * static_cast<double>(games.size()));
    std::vector<ReportPlan> out;
    std::vector<uint8_t> pawn_squares;
    std::vector<uint32_t> men;  // the routes' men: kind and first square
    for (const auto& c : candidates) {
        if (out.size() >= options.plans) break;
        if (static_cast<double>(c.count) < least) break;
        ReportPlan plan;
        plan.kind = c.kind;
        plan.side = side;
        if (c.kind == ReportPlan::Kind::Route) {
            const uint32_t man = c.key >> 13;
            if (std::find(men.begin(), men.end(), man) != men.end()) continue;
            men.push_back(man);
            plan.man     = static_cast<uint8_t>(routeKind(c.key));
            plan.squares = routeSquares(c.key);
        } else {
            plan.squares = c.kind == ReportPlan::Kind::Push ? std::vector<uint8_t>{static_cast<uint8_t>(c.key)}
                                                            : std::vector<uint8_t>{static_cast<uint8_t>(c.key >> 6),
                                                                                   static_cast<uint8_t>(c.key & 63)};
            if (std::any_of(plan.squares.begin(), plan.squares.end(), [&](uint8_t sq) {
                    return std::find(pawn_squares.begin(), pawn_squares.end(), sq) != pawn_squares.end();
                }))
                continue;
            pawn_squares.insert(pawn_squares.end(), plan.squares.begin(), plan.squares.end());
        }
        // Its games.
        for (const auto& g : games) {
            bool holds = false;
            if (c.kind == ReportPlan::Kind::Route)
                holds = std::find(g.routes[side].begin(), g.routes[side].end(), c.key) != g.routes[side].end();
            else
                holds = pushesHold(g.pushes[side], plan.squares);
            if (!holds) continue;
            plan.stats.add(g.numbers);
            plan.games.push_back(g.game);
        }
        out.push_back(std::move(plan));
    }
    return out;
}

// The name the caller gives a pawn plan: the one most of an even sample of its games give its pushes, played there.
int namedPlan(std::span<const ReportSource> sources, const ReportPlan& plan, const PlanNamer& namer,
              const std::atomic<bool>& cancel) {
    if (!namer || plan.kind == ReportPlan::Kind::Route || plan.games.empty()) return 0;
    std::map<int, std::size_t> names;
    std::size_t sampled = 0;
    const std::size_t n = plan.games.size(), want = std::min(n, NAMED_SAMPLE);
    for (std::size_t k = 0; k < want && !cancel.load(std::memory_order_relaxed); ++k) {
        const auto& g    = plan.games[k * n / want];
        const auto& snap = *sources[g.source].snapshot;
        ScanBoard board;
        std::optional<Cursor> cursor;
        if (!replayTo(snap, g, board, cursor)) continue;
        ++sampled;
        std::size_t found = 0;
        int name          = 0;
        while (found < plan.squares.size()) {
            const auto code = cursor->next(board);
            if (!code) break;
            const auto move   = board.decode(*code);
            const auto mover  = board.on(move.from);
            const bool pushes = mover && (mover - 1) % 6 == PAWN && (mover - 1) / 6 == plan.side &&
                                !board.on(move.to) && !(move.flags & MOVE_EN_PASSANT);
            if (pushes && move.to == plan.squares[found]) {
                ++found;
                if (!name) name = namer(board, move);
            }
            board.play(move);
        }
        if (name) ++names[name];
    }
    for (const auto& [name, count] : names)
        if (count * 2 > sampled) return name;
    return 0;
}

}  // namespace

namespace report_detail {

ReportMoves movesAt(std::span<const ReportSource> sources, const ScanBoard& position,
                    std::vector<std::vector<ReportGame>>* searched_games) {
    ReportMoves out;
    out.per_source.assign(sources.size(), 0);
    if (searched_games) searched_games->assign(sources.size(), {});
    std::map<int, std::vector<uint64_t>> per_move;
    std::map<int, TreeStats> by_code;
    const std::atomic<bool> never{false};
    for (std::size_t s = 0; s < sources.size(); ++s) {
        const auto& src = sources[s];
        if (!src.snapshot) continue;
        std::optional<TreeAnswer> here;
        if (src.tree && !src.tree->stale(*src.snapshot)) here = src.tree->at(*src.snapshot, position.key());
        if (here) {
            out.per_source[s] = here->total.games;
            out.total.add(here->total);
            for (const auto& m : here->moves) {
                by_code[m.code].add(m.stats);
                auto& each = per_move[m.code];
                each.resize(sources.size(), 0);
                each[s] += m.stats.games;
            }
            continue;
        }
        // Fewer than two games reach it in the tree's plies, or it is past them, or the database has no tree: every
        // game searched (as the reference games do).
        const auto hits = searchPosition(src.snapshot, position, 0, never);
        if (!hits) continue;
        out.searched = true;
        for (const auto& hit : *hits) {
            const auto numbers = src.snapshot->numbers(hit.id);
            out.total.add(numbers);
            ++out.per_source[s];
            int code = TreeMove::END;
            ScanBoard board;
            std::optional<Cursor> cursor;
            const ReportGame g{static_cast<uint32_t>(s), hit.id, hit.ply};
            if (position.codeCount() <= 256 && replayTo(*src.snapshot, g, board, cursor))
                code = cursor->next(board).value_or(TreeMove::END);
            by_code[code].add(numbers);
            auto& each = per_move[code];
            each.resize(sources.size(), 0);
            ++each[s];
            if (searched_games) (*searched_games)[s].push_back(g);
        }
    }
    for (const auto& [code, stats] : by_code)
        if (code != TreeMove::END) out.moves.push_back({code, stats});
    std::stable_sort(out.moves.begin(), out.moves.end(),
                     [](const TreeMove& a, const TreeMove& b) { return a.stats.games > b.stats.games; });
    for (const auto& m : out.moves) out.move_per_source.push_back(per_move[m.code]);
    return out;
}

std::vector<ReportLine> mainLines(std::span<const ReportSource> sources, const ScanBoard& position,
                                  const ReportMoves& moves, std::size_t count) {
    std::vector<ReportLine> out;
    uint32_t plies = 0;
    for (const auto& s : sources)
        if (s.tree) plies = std::max(plies, s.tree->plies());
    const uint32_t at_ply = static_cast<uint32_t>(std::max(0, (position.fullMove() - 1) * 2 + position.sideToMove()));
    for (std::size_t i = 0; i < moves.moves.size() && out.size() < count; ++i) {
        const auto& first = moves.moves[i];
        ReportLine line;
        line.codes.push_back(first.code);
        ScanBoard board = position;
        board.play(board.decode(first.code));
        const uint64_t least           = std::max<uint64_t>(10, first.stats.games / 5);
        std::optional<TreeAnswer> here = treesAt(sources, board.key());
        if (!here) continue;  // fewer than two games play it: no line
        while (at_ply + line.codes.size() < plies && line.codes.size() < 16) {
            const auto best = std::find_if(here->moves.begin(), here->moves.end(),
                                           [](const TreeMove& m) { return m.code != TreeMove::END; });
            if (best == here->moves.end() || best->stats.games < least) break;
            line.codes.push_back(best->code);
            board.play(board.decode(best->code));
            auto next = treesAt(sources, board.key());
            if (!next) {  // fewer than two games go on: the line ends at the position before
                line.codes.pop_back();
                break;
            }
            here = std::move(next);
        }
        if (line.codes.size() < 2) continue;  // no continuation stays common: the move alone, which the moves give
        line.stats = here->total;
        out.push_back(std::move(line));
    }
    return out;
}

}  // namespace report_detail

OpeningReport makeOpeningReport(std::span<const ReportSource> sources, const ScanBoard& position,
                                const ReportOptions& options, ReportProgress& progress, const std::atomic<bool>& cancel,
                                const std::function<void(const OpeningReport&)>& section) {
    const auto started = std::chrono::steady_clock::now();
    const auto stopped = [&] { return cancel.load(std::memory_order_relaxed); };
    const int threads =
        options.threads > 0 ? options.threads : std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    OpeningReport out;
    const auto finish = [&](ReportSection made) {
        out.made    = made;
        out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        if (section) section(out);
    };
    const auto cancelled = [&] {
        out.cancelled = true;
        out.seconds   = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        return out;
    };

    // The moves and the main lines.
    progress.section = static_cast<int>(ReportSection::Moves);
    progress.done    = 0;
    progress.total   = 1;
    std::vector<std::vector<ReportGame>> searched;
    out.moves = report_detail::movesAt(sources, position, &searched);
    out.lines = report_detail::mainLines(sources, position, out.moves, options.lines);
    if (stopped()) return cancelled();
    progress.done = 1;
    finish(ReportSection::Shapes);

    // The position's games: each read for its rating and date (the key games) and its players (the user's), an even
    // sample replayed for its structure and plans.
    progress.section      = static_cast<int>(ReportSection::Shapes);
    const uint64_t total  = out.moves.total.games;
    const uint64_t replay = std::min<uint64_t>(total, options.replayed);
    progress.done         = 0;
    progress.total        = total + replay + 1;
    std::vector<ReportGame> sample;
    sample.reserve(static_cast<std::size_t>(replay));
    struct Ranked {
        int rating    = -1;
        uint32_t date = 0;
        ReportGame game;
        HeaderNumbers numbers;
    };
    std::optional<Ranked> strongest;
    std::vector<Ranked> recent;  // the most recent of 2500, the strongest's rival kept apart
    std::vector<Ranked> users;
    uint64_t index = 0;
    std::vector<std::unordered_map<uint32_t, bool>> user_names(sources.size());
    const auto better = [](const Ranked& a, const Ranked& b) {
        if (a.rating != b.rating) return a.rating > b.rating;
        if (a.date != b.date) return a.date > b.date;
        return std::tie(a.game.source, a.game.id) < std::tie(b.game.source, b.game.id);
    };
    const auto newer = [](const Ranked& a, const Ranked& b) {
        if (a.date != b.date) return a.date > b.date;
        if (a.rating != b.rating) return a.rating > b.rating;
        return std::tie(a.game.source, a.game.id) < std::tie(b.game.source, b.game.id);
    };
    for (uint32_t s = 0; s < sources.size() && !stopped(); ++s) {
        const auto& src = sources[s];
        if (!src.snapshot) continue;
        const auto& snap = *src.snapshot;
        auto& names      = user_names[s];
        const auto visit = [&](GameId id, uint32_t ply) {
            if (snap.deleted(id)) return;
            const ReportGame g{s, id, ply};
            if (total > 0 && (index * replay) / total != ((index + 1) * replay) / total) sample.push_back(g);
            ++index;
            if ((index & 4095) == 0) progress.done = index;
            const auto n = snap.numbers(id);
            Ranked r{meanRating(n), n.date, g, n};
            if (!strongest || better(r, *strongest)) strongest = r;
            if (r.rating >= 2500 && r.date) {
                recent.push_back(r);
                std::sort(recent.begin(), recent.end(), newer);
                if (recent.size() > 2) recent.pop_back();
            }
            if (options.user) {
                const auto ids = snap.rowIds(id);
                bool yours     = false;
                for (const auto name : {ids.white, ids.black}) {
                    auto it = names.find(name);
                    if (it == names.end()) it = names.emplace(name, options.user(snap.name(name))).first;
                    yours = yours || it->second;
                }
                if (yours) {
                    users.push_back(r);
                    std::sort(users.begin(), users.end(), newer);
                    if (users.size() > USER_GAMES) users.pop_back();
                }
            }
        };
        if (!(src.tree && src.tree->visitAt(snap, position.key(), visit)))
            for (const auto& g : searched[s]) visit(g.id, g.ply);
    }
    if (stopped()) return cancelled();
    progress.done = total;
    // The key games known now: the strongest, the most recent of 2500 (another game than the strongest).
    if (strongest && strongest->rating > 0)
        out.key_games.push_back({ReportKeyGame::Why::Strongest, strongest->game, strongest->numbers, 0});
    for (const auto& r : recent) {
        if (strongest && r.game == strongest->game) continue;
        out.key_games.push_back({ReportKeyGame::Why::Recent, r.game, r.numbers, 0});
        break;
    }

    // The structures and the plans, from 50 games on.
    auto& shapes    = out.shapes;
    shapes.ply      = structureOffset(position, options.structure_ply);
    shapes.replayed = sample.size();
    shapes.enough   = sample.size() >= options.least_games;
    if (shapes.enough) {
        std::vector<Replayed> replayed(sample.size());
        std::vector<uint8_t> ok(sample.size(), 0);
        std::atomic<std::size_t> next{0};
        onThreads(static_cast<int>(std::min<std::size_t>(threads, sample.size() / REPLAY_PART + 1)), [&](int) {
            for (;;) {
                const auto from = next.fetch_add(REPLAY_PART);
                if (from >= sample.size() || stopped()) break;
                const auto to = std::min(sample.size(), from + REPLAY_PART);
                for (std::size_t i = from; i < to; ++i) {
                    const auto& g = sample[i];
                    ok[i]         = replayGame(*sources[g.source].snapshot, g, options.plan_plies, shapes.ply,
                                               options.structures, replayed[i]);
                }
                progress.done += to - from;
            }
        });
        if (stopped()) return cancelled();
        std::vector<Replayed> games;
        games.reserve(replayed.size());
        for (std::size_t i = 0; i < replayed.size(); ++i)
            if (ok[i]) games.push_back(std::move(replayed[i]));
        shapes.replayed = games.size();
        shapes.enough   = games.size() >= options.least_games;
        // The structures: each game counted once, by the first name the caller gives its pawns.
        std::vector<std::pair<int, int>> own;
        if (options.structures)
            options.structures(position.pieces(WHITE_SIDE, PAWN), position.pieces(BLACK_SIDE, PAWN), own);
        std::map<std::pair<int, int>, ReportStructure> by;
        for (const auto& g : games) {
            if (!g.reached) continue;
            ++shapes.counted;
            auto& s = by[{g.structure, g.structure < 0 ? 0 : g.owner}];
            s.kind  = g.structure;
            s.owner = g.structure < 0 ? WHITE_SIDE : g.owner;
            s.stats.add(g.numbers);
            s.games.push_back(g.game);
        }
        for (auto& [k, s] : by) {
            s.kept = s.kind >= 0 && std::find(own.begin(), own.end(), std::pair{s.kind, s.owner}) != own.end();
            if (options.structures) shapes.structures.push_back(std::move(s));
        }
        std::stable_sort(shapes.structures.begin(), shapes.structures.end(),
                         [](const ReportStructure& a, const ReportStructure& b) {
                             if ((a.kind < 0) != (b.kind < 0)) return b.kind < 0;
                             return a.stats.games > b.stats.games;
                         });
        if (shapes.enough) {
            for (int side = 0; side < 2; ++side)
                for (auto& p : sidePlans(games, side, options)) shapes.plans.push_back(std::move(p));
            for (auto& p : shapes.plans) p.named = namedPlan(sources, p, options.plans_named, cancel);
        } else {
            shapes.structures.clear();
        }
    }
    if (stopped()) return cancelled();
    progress.done = progress.total.load();
    finish(ReportSection::Novelties);

    // The novelties along the main lines: the moves whose first game is of the last years, repeated since.
    progress.section = static_cast<int>(ReportSection::Novelties);
    progress.done    = 0;
    struct Spot {
        ScanBoard board;
        std::vector<int> codes;
    };
    std::vector<Spot> spots{{position, {}}};
    for (const auto& line : out.lines) {
        Spot s{position, {}};
        for (std::size_t k = 0; k + 1 < line.codes.size(); ++k) {
            s.board.play(s.board.decode(line.codes[k]));
            s.codes.push_back(line.codes[k]);
            if (std::none_of(spots.begin(), spots.end(), [&](const Spot& o) { return o.board.key() == s.board.key(); }))
                spots.push_back(s);
        }
    }
    progress.total        = spots.size() + 4;
    const int latest_year = static_cast<int>(out.moves.total.last >> 9);
    const int cutoff      = latest_year - static_cast<int>(options.novelty_years) + 1;
    // The games that reach a position after a move: by the trees (any move order), else those that played it.
    const auto gamesAfter = [&](const ScanBoard& before, int code, const ScanBoard& after) {
        std::vector<std::pair<ReportGame, HeaderNumbers>> games;
        for (uint32_t s = 0; s < sources.size(); ++s) {
            const auto& src = sources[s];
            if (!src.tree || !src.snapshot || src.tree->stale(*src.snapshot)) continue;
            const auto& snap = *src.snapshot;
            const auto add   = [&](GameId id, uint32_t ply) {
                if (!snap.deleted(id)) games.push_back({{s, id, ply}, snap.numbers(id)});
            };
            if (!src.tree->visitAt(snap, after.key(), add))
                for (const auto& g : src.tree->gamesWith(snap, before.key(), code)) add(g.id, g.ply + 1);
        }
        return games;
    };
    std::vector<ReportNovelty> found;
    if (latest_year > 0) {
        for (const auto& spot : spots) {
            if (stopped()) return cancelled();
            const auto here = treesAt(sources, spot.board.key());
            ++progress.done;
            if (!here) continue;
            for (const auto& m : here->moves) {
                if (m.code == TreeMove::END || m.stats.games < 2 || m.stats.games > NOVELTY_GAMES) continue;
                if (static_cast<int>(m.stats.last >> 9) < cutoff) continue;
                ScanBoard after = spot.board;
                after.play(after.decode(m.code));
                ReportNovelty n;
                n.codes = spot.codes;
                n.codes.push_back(m.code);
                for (const auto& [g, numbers] : gamesAfter(spot.board, m.code, after)) {
                    n.stats.add(numbers);
                    if ((numbers.date >> 9) > 0 && (!n.first || numbers.date < n.first)) {
                        n.first      = numbers.date;
                        n.first_game = g;
                    }
                }
                if (n.stats.games >= 2 && n.first && static_cast<int>(n.first >> 9) >= cutoff)
                    found.push_back(std::move(n));
            }
        }
    }
    std::sort(found.begin(), found.end(), [](const ReportNovelty& a, const ReportNovelty& b) {
        if (a.first != b.first) return a.first > b.first;
        if (a.stats.games != b.stats.games) return a.stats.games > b.stats.games;
        return a.codes < b.codes;
    });
    // No game reached its position before: the trees hold every game that reaches it within their plies, by any move
    // order; near their horizon, a game could reach it later by other moves, which the position search finds (one
    // search of 11.7 M games takes 1.7 s on 32 threads, 35 s on one: it is run only there).
    uint32_t horizon = UINT32_MAX;
    for (const auto& src : sources)
        if (src.tree && src.snapshot && !src.tree->stale(*src.snapshot)) horizon = std::min(horizon, src.tree->plies());
    const uint32_t position_ply =
        static_cast<uint32_t>(std::max(0, (position.fullMove() - 1) * 2 + position.sideToMove()));
    for (auto& n : found) {
        if (out.novelties.moves.size() >= 3) break;
        if (stopped()) return cancelled();
        if (horizon == UINT32_MAX || position_ply + n.codes.size() + NOVELTY_MARGIN <= horizon) {
            n.confirmed = true;
            out.novelties.moves.push_back(std::move(n));
            continue;
        }
        ScanBoard after = position;
        for (const auto c : n.codes) after.play(after.decode(c));
        bool earlier = false, whole = true;
        for (uint32_t s = 0; s < sources.size() && !earlier; ++s) {
            const auto hits = searchPosition(sources[s].snapshot, after, threads, cancel);
            if (!hits) {
                whole = false;
                break;
            }
            for (const auto& hit : *hits) {
                const auto d = sources[s].snapshot->numbers(hit.id).date;
                if ((d >> 9) > 0 && d < n.first) earlier = true;
            }
        }
        if (stopped()) return cancelled();
        if (earlier) continue;  // older than the trees told: no novelty of these years
        n.confirmed = whole;
        out.novelties.moves.push_back(std::move(n));
    }
    progress.done = spots.size() + 3;
    // The user's own game that left the databases: their latest game at the position whose move was played by no other,
    // confirmed by the position search as the Tree tab confirms a novelty (a single game reaching its position by other
    // moves is not in the trees).
    for (const auto& u : users) {
        if (stopped()) return cancelled();
        const auto& src  = sources[u.game.source];
        const auto& snap = *src.snapshot;
        ScanBoard board;
        std::optional<Cursor> cursor;
        if (!replayTo(snap, u.game, board, cursor)) continue;
        // The game itself in its database's tree: one game less at each position.
        const bool counted = src.tree && !src.tree->stale(snap) && u.game.id < src.tree->games();
        std::vector<int> codes;
        std::optional<ReportNovelty> left;
        for (uint32_t step = 0; step < options.plan_plies; ++step) {
            const uint32_t ply = u.game.ply + step;
            const auto code    = cursor->next(board);
            if (!code) break;
            ScanBoard after = board;
            after.play(after.decode(*code));
            codes.push_back(*code);
            // The game counted in its own database's tree where the tree keeps the position (another game of the
            // database reaches it too), within its plies.
            const auto own = [&](const ScanBoard& b, uint32_t at_ply) -> uint64_t {
                return counted && at_ply <= src.tree->plies() && src.tree->at(snap, b.key()) ? 1 : 0;
            };
            const uint64_t self_here  = own(board, ply);
            const uint64_t self_there = own(after, ply + 1);
            const auto here           = treesAt(sources, board.key());
            const auto there          = treesAt(sources, after.key());
            const uint64_t at         = here ? here->total.games : 0;
            const uint64_t got        = there ? there->total.games : 0;
            if (at <= self_here) break;  // the position itself was the game's alone: nothing to tell
            if (got <= self_there) {
                ReportNovelty n;
                n.codes  = codes;
                n.yours  = true;
                n.game   = u.game;
                n.before = at - self_here;
                n.first  = u.date;
                n.stats.add(u.numbers);
                left = std::move(n);
                break;
            }
            board = after;
        }
        if (!left) continue;
        // Confirmed by the position search: no other game reached it by other moves.
        ScanBoard after = position;
        for (const auto c : left->codes) after.play(after.decode(c));
        bool other = false, whole = true;
        for (uint32_t s = 0; s < sources.size() && !other; ++s) {
            const auto hits = searchPosition(sources[s].snapshot, after, threads, cancel);
            if (!hits) {
                whole = false;
                break;
            }
            for (const auto& hit : *hits)
                if (!(s == u.game.source && hit.id == u.game.id)) other = true;
        }
        if (stopped()) return cancelled();
        if (other) continue;
        left->confirmed = whole;
        out.novelties.moves.push_back(std::move(*left));
        break;
    }
    // The first game of the novelty repeated most, a key game.
    std::optional<std::size_t> most;
    for (std::size_t i = 0; i < out.novelties.moves.size(); ++i) {
        const auto& n = out.novelties.moves[i];
        if (n.yours || !n.first_game || n.stats.games < 3) continue;
        if (!most || n.stats.games > out.novelties.moves[*most].stats.games) most = i;
    }
    if (most) {
        const auto& n  = out.novelties.moves[*most];
        const auto& fg = *n.first_game;
        if (std::none_of(out.key_games.begin(), out.key_games.end(),
                         [&](const ReportKeyGame& k) { return k.game.source == fg.source && k.game.id == fg.id; }))
            out.key_games.push_back(
                {ReportKeyGame::Why::Novelty, fg, sources[fg.source].snapshot->numbers(fg.id), *most});
    }
    progress.done = progress.total.load();
    finish(ReportSection::Done);
    return out;
}

}  // namespace fcg::study
