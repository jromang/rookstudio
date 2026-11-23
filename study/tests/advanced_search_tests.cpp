#include <doctest/doctest.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <chess.hpp>
#include <study/import.hpp>
#include <study/patterns.hpp>
#include <study/scan_board.hpp>
#include <study/search.hpp>
#include <study/store.hpp>

#include "test_games.hpp"

using namespace fcg::study;
using namespace fcg::test;

// What the searches of the language, of a route and of a structure must find: a brute force written apart from them,
// on chess.hpp's boards and legal moves, square by square, with no signature.

namespace {

int sq(const char* name) { return (name[1] - '1') * 8 + (name[0] - 'a'); }
uint64_t bit(int square) { return uint64_t{1} << square; }

// Games of our own composition that hold each pattern, beside random ones.
std::vector<Spec> composed() {
    return {
        // A knight fork of the king and a rook: 6. Nxc7+.
        {"1. e4 d5 2. exd5 Qxd5 3. Nc3 Qd8 4. Nb5 Na6 5. d4 Bd7 6. Nxc7+ Nxc7", '1'},
        // A knight pinned on the queen (4. Bg5), a Carlsbad structure (5...exd5), a trap.
        {"1. d4 d5 2. c4 e6 3. Nc3 Nf6 4. Bg5 Nbd7 5. cxd5 exd5 6. Nxd5 Nxd5 7. Bxd8 Bb4+ 8. Qd2 Bxd2+ 9. Kxd2 "
         "Kxd8",
         '0'},
        // The sacrifice on h7.
        {"1. e4 e6 2. d4 d5 3. Nc3 Nf6 4. e5 Nfd7 5. Nf3 Be7 6. Bd3 O-O 7. Bxh7+ Kxh7 8. Ng5+ Kg8 9. Qh5 Bxg5", '1'},
        // A mate in the middle of the board.
        {"1. e4 e5 2. Nf3 d6 3. Bc4 Bg4 4. Nc3 g6 5. Nxe5 Bxd1 6. Bxf7+ Ke7 7. Nd5#", '1'},
        // A back-rank mate, from a position set up.
        {"1. Rd8#", '1', 0, 0, "2021.01.01", "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1"},
        // A stalemate.
        {"1. Qf7", '=', 0, 0, "2021.01.01", "7k/8/6K1/5Q2/8/8/8/8 w - - 0 1"},
        // The knight's route b1-d2-f1-g3, then on to f5.
        {"1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O Be7 6. Re1 b5 7. Bb3 d6 8. c3 O-O 9. h3 Nb8 10. d4 Nbd7 "
         "11. Nbd2 Bb7 12. Bc2 Re8 13. Nf1 Bf8 14. Ng3 g6 15. a4 c5 16. d5 c4 17. Bg5 h6 18. Be3 Nc5 19. Qd2 h5 20. "
         "Nf5 gxf5",
         '='},
        // The same route parted from: b1-d2-b3.
        {"1. e4 e5 2. Nf3 Nc6 3. d4 exd4 4. Nbd2 d5 5. Nb3 dxe4", '0'},
        // A protected passed pawn.
        {"1. Ke2 Ke7", '1', 0, 0, "2021.01.01", "4k3/8/8/3P4/2P5/8/8/4K3 w - - 0 1"},
        // The queen's double attack: the king and a rook nothing defends.
        {"1. Qa4+ Kf8 2. Qxa8+ Kg7", '1', 0, 0, "2021.01.01", "r3k3/8/8/8/8/8/8/3QK3 w - - 0 1"},
        // Castling, en passant and a promotion for the names followed through the moves.
        {"1. e4 Nf6 2. e5 d5 3. exd6 e5 4. dxc7 Qe7 5. cxb8=Q Rxb8 6. Nf3 Qe6 7. Be2 Bd6 8. O-O O-O", '='},
    };
}

std::shared_ptr<Database> games(const std::string& name) {
    auto all          = randomGames(1800, 89, 8);
    const auto extras = composed();
    for (std::size_t i = 0; i < extras.size(); ++i) all.push_back(make(extras[i], 20000 + i));
    return database(name, std::move(all));
}

// A game as chess.hpp plays it: its boards after 0 to all its plies, and its moves.
struct Played {
    std::vector<chess::Board> boards;
    std::vector<chess::Move> moves;
};

Played played(const Snapshot& snap, GameId id) {
    Played p;
    const auto g = snap.gameMoves(id);
    chess::Board board;
    board.set960((g.flags & GAME_CHESS960) != 0);
    board.setFen((g.flags & GAME_FEN) ? snap.game(id).start_fen : std::string(chess::constants::STARTPOS));
    ScanBoard scan;
    REQUIRE(scan.set(board));
    p.boards.push_back(board);
    std::size_t at = 0;
    for (uint32_t ply = 0; ply < g.plies; ++ply) {
        const int count = scan.codeCount();
        int code        = g.codes[at++];
        if (count > 256) code = (code << 8) | g.codes[at++];
        const auto move = moveOfCode(board, scan, code);
        REQUIRE(move);
        scan.play(scan.decode(code));
        board.makeMove(*move);
        p.moves.push_back(*move);
        p.boards.push_back(board);
    }
    return p;
}

int manAt(const chess::Board& b, int square) {
    const auto piece = b.at(chess::Square(square));
    if (piece == chess::Piece::NONE) return -1;
    return (piece.color() == chess::Color::WHITE ? 0 : 6) + static_cast<int>(piece.type());
}
int sideOf(int man) { return man / 6; }
int kindOf(int man) { return man % 6; }
int fileOf(int square) { return square % 8; }
int rankOf(int square) { return square / 8; }

// Whether the man on `from` attacks `to`, pins aside.
bool hits(const chess::Board& b, int from, int to) {
    const int man = manAt(b, from);
    const auto f  = chess::Square(from);
    chess::Bitboard a;
    switch (kindOf(man)) {
        case PAWN:
            a = chess::attacks::pawn(sideOf(man) == 0 ? chess::Color::WHITE : chess::Color::BLACK, f);
            break;
        case KNIGHT:
            a = chess::attacks::knight(f);
            break;
        case BISHOP:
            a = chess::attacks::bishop(f, b.occ());
            break;
        case ROOK:
            a = chess::attacks::rook(f, b.occ());
            break;
        case QUEEN:
            a = chess::attacks::queen(f, b.occ());
            break;
        default:
            a = chess::attacks::king(f);
            break;
    }
    return (a.getBits() >> to) & 1;
}

bool noLegalMove(const chess::Board& b) {
    chess::Movelist list;
    chess::movegen::legalmoves(list, b);
    return list.empty();
}

constexpr int VALUE[6] = {1, 3, 3, 5, 9, 100};

struct Brute {
    const Pattern& p;
    std::array<int, 4> bound{-1, -1, -1, -1};
    std::array<std::set<int>, 2> only_pawns;

    explicit Brute(const Pattern& pattern) : p(pattern) {
        for (const auto& c : p.conditions)
            if (manOnSquare(c)) {
                const int b = __builtin_ctz(c.a.men.pieces);
                if (b == PAWN || b == 6 + PAWN)
                    only_pawns[static_cast<std::size_t>(b / 6)].insert(__builtin_ctzll(c.a.men.squares));
            }
    }

    std::vector<int> squaresOf(const Subject& s, const chess::Board& b) const {
        std::vector<int> out;
        if (s.variable >= 0) {
            if (bound[static_cast<std::size_t>(s.variable)] >= 0)
                out.push_back(bound[static_cast<std::size_t>(s.variable)]);
            return out;
        }
        for (int square = 0; square < 64; ++square) {
            if (!((s.men.squares >> square) & 1)) continue;
            if (s.men.pieces == 0) {
                out.push_back(square);
                continue;
            }
            const int man = manAt(b, square);
            if (man < 0 ? (s.men.pieces & Men::EMPTY) != 0 : (s.men.pieces & (1u << man)) != 0) out.push_back(square);
        }
        return out;
    }
    static bool bare(const Subject& s) { return s.variable < 0 && s.men.pieces == 0; }

    bool body(const Condition& c, const chess::Board& b, bool last) const {
        switch (c.relation) {
            case Relation::On:
                return !squaresOf(c.a, b).empty();
            case Relation::Attacks:
            case Relation::Defends: {
                const bool attack = c.relation == Relation::Attacks;
                for (const int x : squaresOf(c.a, b)) {
                    const int man = manAt(b, x);
                    if (man < 0) continue;
                    for (const int y : squaresOf(c.b, b)) {
                        if (y == x) continue;
                        const int target = manAt(b, y);
                        const bool own   = target >= 0 && sideOf(target) == sideOf(man);
                        const bool enemy = target >= 0 && !own;
                        if (bare(c.b) ? (attack ? own : !own) : (attack ? !enemy : !own)) continue;
                        if (hits(b, x, y)) return true;
                    }
                }
                return false;
            }
            case Relation::Pins:
                for (const int t : squaresOf(c.c, b)) {
                    const int tm = manAt(b, t);
                    if (tm < 0) continue;
                    for (const int s : squaresOf(c.a, b)) {
                        const int sm = manAt(b, s);
                        if (sm < 0 || sideOf(sm) == sideOf(tm) || s == t) continue;
                        const int k = kindOf(sm), df = fileOf(t) - fileOf(s), dr = rankOf(t) - rankOf(s);
                        const bool straight = df == 0 || dr == 0, diagonal = std::abs(df) == std::abs(dr);
                        if (!((straight && (k == ROOK || k == QUEEN)) || (diagonal && (k == BISHOP || k == QUEEN))))
                            continue;
                        const int step = (df > 0) - (df < 0) + 8 * ((dr > 0) - (dr < 0));
                        int count = 0, x = -1;
                        for (int at = s + step; at != t; at += step)
                            if (manAt(b, at) >= 0) {
                                ++count;
                                x = at;
                            }
                        if (count != 1) continue;
                        const int xm      = manAt(b, x);
                        const auto within = squaresOf(c.b, b);
                        if (sideOf(xm) == sideOf(tm) && kindOf(xm) != KING &&
                            std::find(within.begin(), within.end(), x) != within.end())
                            return true;
                    }
                }
                return false;
            case Relation::EnPrise:
                for (const int x : squaresOf(c.a, b)) {
                    const int xm = manAt(b, x);
                    if (xm < 0 || kindOf(xm) == KING) continue;
                    int least = 1000, defenders = 0, attackers = 0;
                    for (int y = 0; y < 64; ++y) {
                        const int ym = manAt(b, y);
                        if (ym < 0 || y == x || !hits(b, y, x)) continue;
                        if (sideOf(ym) == sideOf(xm)) {
                            ++defenders;
                        } else {
                            ++attackers;
                            least = std::min(least, VALUE[kindOf(ym)]);
                        }
                    }
                    if (attackers && (!defenders || least < VALUE[kindOf(xm)])) return true;
                }
                return false;
            case Relation::Passed:
                for (const int x : squaresOf(c.a, b)) {
                    const int xm = manAt(b, x);
                    if (xm < 0 || kindOf(xm) != PAWN) continue;
                    bool stopped = false;
                    for (int y = 0; y < 64; ++y) {
                        const int ym = manAt(b, y);
                        if (ym < 0 || kindOf(ym) != PAWN || sideOf(ym) == sideOf(xm)) continue;
                        const bool ahead = sideOf(xm) == 0 ? rankOf(y) > rankOf(x) : rankOf(y) < rankOf(x);
                        if (ahead && std::abs(fileOf(y) - fileOf(x)) <= 1) stopped = true;
                    }
                    if (!stopped) return true;
                }
                return false;
            case Relation::Check:
                return b.inCheck();
            case Relation::Mate:
                return last && b.inCheck() && noLegalMove(b);
            case Relation::Stalemate:
                return last && !b.inCheck() && noLegalMove(b);
            case Relation::WhiteToMove:
                return b.sideToMove() == chess::Color::WHITE;
            case Relation::BlackToMove:
                return b.sideToMove() == chess::Color::BLACK;
            case Relation::OnlyPawns: {
                std::set<int> pawns;
                for (int y = 0; y < 64; ++y)
                    if (manAt(b, y) == c.side * 6 + PAWN) pawns.insert(y);
                return pawns == only_pawns[static_cast<std::size_t>(c.side)];
            }
        }
        return false;
    }

    bool holds(const Condition& c, const chess::Board& b, bool last) const {
        if (body(c, b, last) != c.negated) return true;
        for (const auto& alt : c.alternatives)
            if (holds(alt, b, last)) return true;
        return false;
    }
    bool all(const std::vector<Condition>& cs, const chess::Board& b, bool last) const {
        for (const auto& c : cs)
            if (!holds(c, b, last)) return false;
        return true;
    }

    static int kingTo(const chess::Move& m) {
        return rankOf(m.from().index()) * 8 + (m.to().index() > m.from().index() ? 6 : 2);
    }
    static int rookTo(const chess::Move& m) {
        return rankOf(m.from().index()) * 8 + (m.to().index() > m.from().index() ? 5 : 3);
    }
    static int taken(const chess::Board& before, const chess::Move& m) {
        if (m.typeOf() == chess::Move::CASTLING) return -1;
        if (m.typeOf() == chess::Move::ENPASSANT)
            return m.to().index() + (before.sideToMove() == chess::Color::WHITE ? -8 : 8);
        return manAt(before, m.to().index()) >= 0 ? m.to().index() : -1;
    }

    bool fits(const PatternMove& w, const chess::Board& before, const chess::Move& m) const {
        const bool castle = m.typeOf() == chess::Move::CASTLING;
        const int to      = castle ? kingTo(m) : m.to().index();
        const auto in = [](const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); };
        if (w.from && !in(squaresOf(*w.from, before), m.from().index())) return false;
        if (w.to && !in(squaresOf(*w.to, before), to)) return false;
        if (w.captures) {
            const int t = taken(before, m);
            if (t < 0) return false;
            if (w.captured && !in(squaresOf(*w.captured, before), t)) return false;
        }
        if (w.promote >= 0 &&
            (m.typeOf() != chess::Move::PROMOTION || static_cast<int>(m.promotionType()) != w.promote))
            return false;
        return true;
    }

    void follow(const chess::Board& before, const chess::Move& m) {
        if (m.typeOf() == chess::Move::CASTLING) {
            for (auto& s : bound) {
                if (s == m.from().index())
                    s = kingTo(m);
                else if (s == m.to().index())
                    s = rookTo(m);
            }
            return;
        }
        const int t = taken(before, m);
        for (auto& s : bound) {
            if (s < 0) continue;
            if (s == t) s = -1;
            if (s == m.from().index()) s = m.to().index();
        }
    }

    bool line(std::size_t k, uint32_t ply, const Played& g) {
        if (k == p.line.size()) return true;
        const auto& step = p.line[k];
        const auto saved = bound;
        bool found       = false;
        for (uint32_t gap = step.min_gap; gap <= step.max_gap && !found; ++gap) {
            bound            = saved;
            const uint32_t t = ply + gap + 1;  // the step's position
            if (t > g.moves.size()) break;
            for (uint32_t i = ply; i + 1 < t; ++i) follow(g.boards[i], g.moves[i]);
            const auto before = bound;
            if (step.move && !fits(*step.move, g.boards[t - 1], g.moves[t - 1])) continue;
            follow(g.boards[t - 1], g.moves[t - 1]);
            if (!all(step.conditions, g.boards[t], t == g.moves.size())) continue;
            (void)before;
            found = line(k + 1, t, g);
        }
        bound = saved;
        return found;
    }

    std::optional<uint32_t> find(const Played& g) {
        const auto plies = static_cast<uint32_t>(g.moves.size());
        for (uint32_t ply = 0; ply <= plies; ++ply) {
            const auto& b       = g.boards[ply];
            const std::size_t n = p.variables.size();
            std::vector<std::vector<int>> choices(n);
            for (std::size_t v = 0; v < n; ++v)
                for (const int s : squaresOf(Subject{p.variables[v].men, -1}, b))
                    if (manAt(b, s) >= 0) choices[v].push_back(s);
            // Every binding, two names never on one man.
            std::vector<std::size_t> at(n, 0);
            bool any_choice = std::all_of(choices.begin(), choices.end(), [](const auto& c) { return !c.empty(); });
            if (!any_choice) continue;
            for (;;) {
                bound = {-1, -1, -1, -1};
                std::set<int> used;
                for (std::size_t v = 0; v < n; ++v) {
                    bound[v] = choices[v][at[v]];
                    used.insert(bound[v]);
                }
                if (used.size() == n && all(p.conditions, b, ply == plies) && line(0, ply, g)) return ply;
                std::size_t v = 0;
                for (; v < n; ++v) {
                    if (++at[v] < choices[v].size()) break;
                    at[v] = 0;
                }
                if (v == n) break;
            }
        }
        return std::nullopt;
    }
};

// The searches of a pattern: with the signatures, without them, and the brute force, game by game.
std::size_t compare(const std::shared_ptr<const Snapshot>& snap, const std::vector<Played>& all,
                    const std::string& text) {
    CAPTURE(text);
    const auto read = readPattern(text);
    REQUIRE(read.pattern);
    const auto matcher      = std::make_shared<const PatternMatcher>(*read.pattern);
    const SearchQuery query = RelationQuery{matcher};
    const auto hits         = searchAll(snap, query, 3);
    std::vector<SearchHit> brute, unsigned_hits;
    for (GameId id = 0; id < snap->games(); ++id) {
        Brute b(*read.pattern);
        if (const auto ply = b.find(all[id])) brute.push_back({id, *ply, 0});
        if (const auto ply = searchGame(*snap, id, query, false)) unsigned_hits.push_back({id, *ply, 0});
    }
    CHECK(hits == brute);
    CHECK(unsigned_hits == brute);
    return hits.size();
}

}  // namespace

TEST_CASE("the board's attackers and legal moves are chess.hpp's") {
    std::vector<ImportedGame> gs = randomGames(120, 11, 4);
    auto db                      = database("search-board", std::move(gs));
    const auto snap              = db->snapshot();
    std::size_t positions = 0, without = 0;
    for (GameId id = 0; id < snap->games(); ++id) {
        const auto g = played(*snap, id);
        ScanBoard scan;
        for (const auto& board : g.boards) {
            REQUIRE(scan.set(board));
            CHECK(scan.hasLegalMove() == !noLegalMove(board));
            if (noLegalMove(board)) ++without;
            for (int square = 0; square < 64; ++square)
                for (int by = 0; by < 2; ++by) {
                    uint64_t expected = 0;
                    for (int y = 0; y < 64; ++y) {
                        const int m = manAt(board, y);
                        if (m >= 0 && sideOf(m) == by && y != square && hits(board, y, square)) expected |= bit(y);
                    }
                    CHECK(scan.attackers(square, by) == expected);
                }
            ++positions;
        }
    }
    // Mates and stalemates, from positions set up.
    for (const char* fen : {"6k1/5ppp/8/8/8/8/5PPP/3R2K1 b - - 0 1", "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",
                            "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", "3R2k1/5ppp/8/8/8/8/5PPP/6K1 b - - 0 1",
                            "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3",
                            "4k3/8/8/8/8/8/3pp3/r3K3 w - - 0 1", "8/8/8/8/8/5k2/4p3/4K3 w - - 0 1",
                            "8/8/8/2k5/1pP5/8/8/2K5 b - c3 0 1", "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"}) {
        CAPTURE(fen);
        chess::Board board(fen);
        ScanBoard scan;
        REQUIRE(scan.set(board));
        CHECK(scan.hasLegalMove() == !noLegalMove(board));
    }
    CHECK(positions > 2000);
    (void)without;
}

TEST_CASE("search: patterns of the language, as a brute force on chess.hpp finds them") {
    auto db         = games("search-language");
    const auto snap = db->snapshot();
    std::vector<Played> all;
    for (GameId id = 0; id < snap->games(); ++id) all.push_back(played(*snap, id));
    std::map<std::string, std::size_t> found;
    const char* texts[] = {
        "N attacks q",
        "piece $n in N\n$n attacks k\n$n attacks [qr]\nbtm",
        "pin",
        "pin from B through n to q",
        "q en prise",
        "[QR] en prise\nbtm",
        "P passed\nwtm",
        "check",
        "not check\nbtm\nk[g8,h8]",
        "mate",
        "stalemate",
        "k[a-h8]\n[QR][a-h8] attacks k\nmate",
        "Nf3 or Nc3\nnot Bc4",
        "Kg1 Pf2 Pg2 Ph2\nonly P",
        "N attacks f7",
        "B defends [Nn]",
        "piece $q in Q\npiece $x in [rbn]\n$q attacks k\n$q attacks $x\nnot a defends $x",
        "Bd3 Nf3 Qd1 Pe5 kg8 ph7 _f6\nline --> move from B to h7 check\n --> move from k to h7\n --> move from N to g5 "
        "check",
        "line --> move capture --> move capture",
        "line --> move from P to [a-h8] promote Q",
        "line --> move to [g1,c1] --> .{0,3} --> move to [g8,c8]",
        "piece $b in [Bb]\nline --> move from $b capture --> move capture $b",
        "line --> check --> . --> check",
        "N attacks q\nline --> .{0,2} --> move capture q",
        "piece $p in P[a-h4-7]\n$p passed\nP defends $p",
    };
    for (const auto* text : texts) found[text] = compare(snap, all, text);
    // Each found in some games, the composed ones at least.
    for (const auto& [text, count] : found) {
        CAPTURE(text);
        CHECK(count > 0);
    }
    for (const auto& preset : patternPresets()) {
        CAPTURE(preset.key);
        CHECK(compare(snap, all, std::string(preset.text)) > 0);
    }
}

TEST_CASE("search: a man's route as a brute force finds it, where it goes next and the routes parted from it") {
    auto db         = games("search-routes");
    const auto snap = db->snapshot();
    std::vector<Played> all;
    for (GameId id = 0; id < snap->games(); ++id) all.push_back(played(*snap, id));
    struct Expect {
        std::optional<uint32_t> ply;
        bool started = false;
        int next     = -1;
        std::set<std::vector<uint8_t>> parted;
        std::vector<std::pair<std::vector<uint8_t>, uint32_t>> parted_at;  // and the ply of the move that parts
    };
    const auto brute = [&](const ManoeuvreQuery& q, const Played& g, uint32_t start) {
        Expect out;
        const int man          = q.side * 6 + q.kind;
        int done_square        = -1;
        const auto destination = [](const chess::Move& m, int square) {
            if (m.typeOf() == chess::Move::CASTLING) {
                if (square == m.from().index()) return Brute::kingTo(m);
                if (square == m.to().index()) return Brute::rookTo(m);
                return -1;
            }
            return square == m.from().index() ? m.to().index() : -1;
        };
        const auto plies = static_cast<uint32_t>(g.moves.size());
        for (uint32_t i = start; i < plies; ++i) {
            const auto& m = g.moves[i];
            const auto& b = g.boards[i];
            if (static_cast<int>(b.sideToMove() == chess::Color::WHITE ? 0 : 1) != q.side) continue;
            if (manAt(b, m.from().index()) != man || m.from().index() != q.route[0]) continue;
            int to = destination(m, m.from().index());
            if (to != q.route[1]) continue;
            out.started = true;
            std::optional<uint32_t> end;
            int square = to;
            if (q.route.size() == 2) {
                end = i + 1;
            } else {
                std::size_t next = 2;
                uint32_t moves   = 1;
                for (uint32_t j = i + 1; j < plies; ++j) {
                    const auto& mj = g.moves[j];
                    const auto& bj = g.boards[j];
                    if (static_cast<int>(bj.sideToMove() == chess::Color::WHITE ? 0 : 1) == q.side) {
                        ++moves;
                        int d = -1;
                        if (q.same_man)
                            d = destination(mj, square);
                        else if (manAt(bj, mj.from().index()) == man)
                            d = destination(mj, mj.from().index());
                        if (d >= 0 && d == q.route[next]) {
                            square = d;
                            ++next;
                        } else if (q.same_man && d >= 0) {
                            std::vector<uint8_t> r(q.route.begin(), q.route.begin() + static_cast<long>(next));
                            r.push_back(static_cast<uint8_t>(d));
                            out.parted_at.emplace_back(r, j);
                            break;
                        } else if (!q.others_between) {
                            break;
                        }
                        if (moves > q.within) break;
                        if (next == q.route.size()) {
                            end = j + 1;
                            break;
                        }
                    } else if (q.same_man && Brute::taken(bj, mj) == square) {
                        break;
                    }
                }
            }
            if (end && (!out.ply || *end < *out.ply)) {
                out.ply     = end;
                done_square = square;
            }
        }
        if (out.ply) {
            for (uint32_t j = *out.ply; j < plies; ++j) {
                const auto& mj = g.moves[j];
                const auto& bj = g.boards[j];
                if (static_cast<int>(bj.sideToMove() == chess::Color::WHITE ? 0 : 1) == q.side) {
                    const int d = destination(mj, done_square);
                    if (d >= 0) {
                        out.next = d;
                        break;
                    }
                } else if (Brute::taken(bj, mj) == done_square) {
                    break;
                }
            }
        }
        // The routes parted before the route was done (all of them when it never is).
        for (const auto& [r, j] : out.parted_at)
            if (!out.ply || j < *out.ply) out.parted.insert(r);
        return out;
    };
    std::vector<ManoeuvreQuery> queries;
    {
        ManoeuvreQuery q;  // the knight's route of the Closed Spanish
        q.route = {static_cast<uint8_t>(sq("b1")), static_cast<uint8_t>(sq("d2")), static_cast<uint8_t>(sq("f1")),
                   static_cast<uint8_t>(sq("g3"))};
        queries.push_back(q);
        auto tight           = q;
        tight.others_between = false;
        queries.push_back(tight);
        auto short_   = q;
        short_.within = 4;
        queries.push_back(short_);
        auto any     = q;
        any.same_man = false;
        queries.push_back(any);
        ManoeuvreQuery king;  // a king's walk, castling counted
        king.kind  = KING;
        king.route = {static_cast<uint8_t>(sq("e1")), static_cast<uint8_t>(sq("g1")), static_cast<uint8_t>(sq("h1"))};
        queries.push_back(king);
        ManoeuvreQuery rook;
        rook.side  = BLACK_SIDE;
        rook.kind  = ROOK;
        rook.route = {static_cast<uint8_t>(sq("a8")), static_cast<uint8_t>(sq("b8"))};
        queries.push_back(rook);
        ManoeuvreQuery knight;  // random games' knights: Ng1-f3-…
        knight.route = {static_cast<uint8_t>(sq("g1")), static_cast<uint8_t>(sq("f3"))};
        queries.push_back(knight);
        auto further = knight;
        further.route.push_back(static_cast<uint8_t>(sq("e5")));
        queries.push_back(further);
        ManoeuvreQuery black;
        black.side  = BLACK_SIDE;
        black.route = {static_cast<uint8_t>(sq("b8")), static_cast<uint8_t>(sq("c6")), static_cast<uint8_t>(sq("e5"))};
        black.others_between = false;
        queries.push_back(black);
    }
    for (std::size_t n = 0; n < queries.size(); ++n) {
        CAPTURE(n);
        const auto& q = queries[n];
        Search search(snap, q, 3);
        search.wait();
        const auto hits  = search.hits();
        const auto stats = search.stats();
        std::vector<SearchHit> expected;
        SearchStats want;
        for (GameId id = 0; id < snap->games(); ++id) {
            const auto e = brute(q, all[id], 0);
            if (e.started || e.ply) ++want.started;
            if (e.ply) {
                expected.push_back({id, *e.ply, 0});
                if (e.next >= 0) ++want.next[static_cast<std::size_t>(e.next)];
            }
            for (const auto& r : e.parted) ++want.parted[r];
        }
        CHECK(hits == expected);
        CHECK(stats.started == want.started);
        CHECK(stats.next == want.next);
        CHECK(stats.parted == want.parted);
        if (n == 0) {
            // The Closed Spanish's knight: found, on to f5 next; b1-d2-b3 parted from it.
            CHECK(!hits.empty());
            CHECK(stats.next[static_cast<std::size_t>(sq("f5"))] >= 1);
            CHECK(stats.parted.count({static_cast<uint8_t>(sq("b1")), static_cast<uint8_t>(sq("d2")),
                                      static_cast<uint8_t>(sq("b3"))}) == 1);
        }
    }
    // From the Board page's position: the route once a game reaches it.
    {
        ScanBoard target;
        REQUIRE(target.setFen("rnbq1rk1/2p1bppp/p2p1n2/1p2p3/4P3/1BP2N1P/PP1P1PP1/RNBQR1K1 w - - 1 10", false));
        ManoeuvreQuery q = queries[0];
        q.from           = target;
        const auto hits  = searchAll(snap, q, 2);
        REQUIRE(hits.size() == 1);
        q.from.reset();
        CHECK(searchAll(snap, q, 2).size() >= hits.size());
    }
}

TEST_CASE("search: a pawn structure exactly, one or two pawns away, reversed, as a brute force finds it") {
    auto db         = games("search-structures");
    const auto snap = db->snapshot();
    std::vector<Played> all;
    for (GameId id = 0; id < snap->games(); ++id) all.push_back(played(*snap, id));
    const auto pawnsOf = [](const chess::Board& b, int side) {
        uint64_t out = 0;
        for (int s = 0; s < 64; ++s)
            if (manAt(b, s) == side * 6 + PAWN) out |= bit(s);
        return out;
    };
    const auto distance = [](uint64_t want, uint64_t have) {
        int missing = 0, extra = 0;
        for (int s = 0; s < 64; ++s) {
            const bool w = (want >> s) & 1, h = (have >> s) & 1;
            missing += w && !h;
            extra += h && !w;
        }
        return std::max(missing, extra);
    };
    const auto turned = [](uint64_t b) {
        uint64_t out = 0;
        for (int s = 0; s < 64; ++s)
            if ((b >> s) & 1) out |= bit(s ^ 56);
        return out;
    };
    const auto levelOf = [&](const StructureQuery& q, uint64_t w, uint64_t b, bool& reverse) {
        const auto as_is = [&](uint64_t ww, uint64_t bb) {
            if (q.both_sides) return distance(q.pawns[0], ww) + distance(q.pawns[1], bb);
            return q.side == 0 ? distance(q.pawns[0], ww) : distance(q.pawns[1], bb);
        };
        int level = as_is(w, b);
        reverse   = false;
        if (q.reversed) {
            const int r = as_is(turned(b), turned(w));
            if (r < level) {
                level   = r;
                reverse = true;
            }
        }
        return level;
    };
    struct Expect {
        std::optional<uint32_t> ply;
        int level = 0;
        std::optional<uint32_t> variant;
    };
    const auto brute = [&](const StructureQuery& q, const Played& g) {
        Expect out;
        const auto n        = static_cast<uint32_t>(g.boards.size());
        const uint32_t need = std::max<uint32_t>(1, q.plies);
        for (int level = 0; level <= q.distance; ++level) {
            for (uint32_t s = 0; s + need <= n; ++s) {
                bool ok = true;
                for (uint32_t i = s; i < s + need && ok; ++i) {
                    bool rev = false;
                    ok       = levelOf(q, pawnsOf(g.boards[i], 0), pawnsOf(g.boards[i], 1), rev) <= level;
                }
                if (!ok) continue;
                out.ply   = s;
                out.level = level;
                if (level == 1) {
                    bool rev         = false;
                    const uint64_t w = pawnsOf(g.boards[s], 0), b = pawnsOf(g.boards[s], 1);
                    (void)levelOf(q, w, b, rev);
                    const std::array<uint64_t, 2> have =
                        rev ? std::array<uint64_t, 2>{turned(b), turned(w)} : std::array<uint64_t, 2>{w, b};
                    for (int side = 0; side < 2; ++side) {
                        if (!q.both_sides && side != q.side) continue;
                        const uint64_t gone =
                            q.pawns[static_cast<std::size_t>(side)] & ~have[static_cast<std::size_t>(side)];
                        const uint64_t added =
                            have[static_cast<std::size_t>(side)] & ~q.pawns[static_cast<std::size_t>(side)];
                        if (!gone && !added) continue;
                        out.variant =
                            variantKey(side, gone ? __builtin_ctzll(gone) : 64, added ? __builtin_ctzll(added) : 64);
                    }
                }
                return out;
            }
        }
        return out;
    };
    // The Carlsbad structure of the composed game after 5...exd5, and others of random games.
    chess::Board carlsbad("r1bqkb1r/pppn1ppp/5n2/3p2B1/3P4/2N5/PP2PPPP/R2QKBNR w KQkq - 0 6");
    std::vector<StructureQuery> queries;
    for (const int distance_ : {0, 1, 2}) {
        StructureQuery q;
        q.pawns    = {pawnsOf(carlsbad, 0), pawnsOf(carlsbad, 1)};
        q.distance = distance_;
        queries.push_back(q);
        auto held  = q;
        held.plies = 4;
        queries.push_back(held);
        auto reversed     = q;
        reversed.reversed = true;
        queries.push_back(reversed);
        auto white_only       = q;
        white_only.both_sides = false;
        queries.push_back(white_only);
        auto black_reversed     = white_only;
        black_reversed.side     = BLACK_SIDE;
        black_reversed.reversed = true;
        queries.push_back(black_reversed);
    }
    {
        StructureQuery start;  // the starting pawns, held 6 plies: broad
        start.pawns    = {0xff00ull, 0x00ff000000000000ull};
        start.distance = 2;
        start.plies    = 6;
        queries.push_back(start);
        StructureQuery few;  // a few pawns: endings of random games, both colours reversed
        few.pawns    = {bit(sq("a2")) | bit(sq("g2")) | bit(sq("h2")), bit(sq("a7")) | bit(sq("g7"))};
        few.distance = 2;
        few.reversed = true;
        queries.push_back(few);
    }
    std::size_t exact_somewhere = 0;
    for (std::size_t n = 0; n < queries.size(); ++n) {
        CAPTURE(n);
        const auto& q = queries[n];
        Search search(snap, q, 3);
        search.wait();
        const auto hits  = search.hits();
        const auto stats = search.stats();
        std::vector<SearchHit> expected, unsigned_hits;
        SearchStats want;
        for (GameId id = 0; id < snap->games(); ++id) {
            const auto e = brute(q, all[id]);
            if (e.ply) {
                expected.push_back({id, *e.ply, static_cast<uint8_t>(e.level)});
                if (e.level == 0) ++want.exact;
                ++want.ecos[snap->numbers(id).eco];
                if (e.variant) {
                    const char r = snap->numbers(id).result;
                    ++want.variants[*e.variant][r == '1' ? 0 : r == '=' ? 1 : r == '0' ? 2 : 3];
                }
            }
            if (const auto hit = findInGame(*snap, id, q, false)) unsigned_hits.push_back(*hit);
        }
        CHECK(hits == expected);
        CHECK(unsigned_hits == expected);
        CHECK(stats.exact == want.exact);
        CHECK(stats.ecos == want.ecos);
        CHECK(stats.variants == want.variants);
        exact_somewhere += want.exact;
    }
    CHECK(exact_somewhere > 0);
}

TEST_CASE("search: a cancelled search resumed finds what a whole one finds; its hits read as they come") {
    std::vector<ImportedGame> gs;
    for (int round = 0; round < 3; ++round) {
        auto more = randomGames(3000, 70 + static_cast<uint32_t>(round), 6);
        for (auto& g : more) gs.push_back(std::move(g));
    }
    auto db                 = database("search-resume", std::move(gs));
    const auto snap         = db->snapshot();
    const auto read         = readPattern("N attacks [qr]\nbtm");
    const SearchQuery query = RelationQuery{std::make_shared<const PatternMatcher>(*read.pattern)};
    const auto whole        = searchAll(snap, query, 2);
    REQUIRE(!whole.empty());
    // Cancelled at once or partway: what it kept, then the rest.
    for (int tries = 0; tries < 3; ++tries) {
        Search first(snap, query, 2);
        if (tries > 0) {
            // Partway: once some part is searched (bounded by the search itself, not by a sleep).
            while (!first.done() && first.searched() == 0) std::this_thread::yield();
        }
        first.cancel();
        first.wait();
        CHECK(first.searched() <= snap->games());
        Search rest(snap, query, 2, &first);
        rest.wait();
        CHECK(rest.done());
        CHECK(rest.searched() == snap->games());
        CHECK(rest.hits() == whole);
    }
    // The hits read part after part, in order, without reading again those read before.
    Search search(snap, query, 3);
    std::vector<SearchHit> read_so_far;
    std::size_t part = 0;
    while (!search.done()) search.hitsFrom(part, read_so_far);
    search.wait();
    search.hitsFrom(part, read_so_far);
    CHECK(read_so_far == whole);
    CHECK(search.stats().results[0] + search.stats().results[1] + search.stats().results[2] +
              search.stats().results[3] ==
          whole.size());
}
