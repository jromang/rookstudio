// The prototype's crates/facts/src/endgame.rs tests.

#include "helpers.hpp"

#include <algorithm>
#include <cstdlib>
#include <vector>

#include <commentary/endgame.hpp>
#include <commentary/structure.hpp>

using namespace fcg::commentary;
using test::moves;
using test::pos;

namespace {

std::vector<Fact> found(std::string_view fen, std::string_view uci) {
    const auto p  = pos(fen);
    const auto mv = moves(p, {uci})[0];
    return endgameFacts(p, mv);
}

bool has(const std::vector<Fact>& facts, const Fact& fact) {
    return std::find(facts.begin(), facts.end(), fact) != facts.end();
}

template <typename T>
bool any(const std::vector<Fact>& facts) {
    return std::any_of(facts.begin(), facts.end(), [](const Fact& f) { return std::holds_alternative<T>(f); });
}

std::size_t state(std::size_t wk, std::size_t bk, std::size_t p, bool black_to_move) {
    return ((wk * 64 + bk) * 64 + p) * 2 + (black_to_move ? 1 : 0);
}

// Whether White wins king and pawn against king, for every placing of the three men (squares counted from a1) and
// either side to move, found by working back from the promotions: the ground truth the key squares are held to.
std::vector<bool> pawnEndings() {
    const auto d = [](std::size_t a, std::size_t b) {
        const auto files = a % 8 > b % 8 ? a % 8 - b % 8 : b % 8 - a % 8;
        const auto ranks = a / 8 > b / 8 ? a / 8 - b / 8 : b / 8 - a / 8;
        return std::max(files, ranks);
    };
    // The squares the pawn attacks.
    const auto hits = [](std::size_t p, std::size_t t) {
        const auto df = t % 8 > p % 8 ? t % 8 - p % 8 : p % 8 - t % 8;
        return t / 8 == p / 8 + 1 && df == 1;
    };
    const auto legal = [&](std::size_t wk, std::size_t bk, std::size_t p, bool black) {
        const bool apart = wk != bk && wk != p && bk != p && d(wk, bk) > 1;
        return apart && p / 8 >= 1 && p / 8 <= 6 && (black || !hits(p, bk));
    };
    std::vector<bool> wins(64 * 64 * 64 * 2, false);
    for (;;) {
        bool changed = false;
        for (std::size_t wk = 0; wk < 64; ++wk) {
            for (std::size_t bk = 0; bk < 64; ++bk) {
                for (std::size_t p = 0; p < 64; ++p) {
                    // White to move: a move that wins.
                    if (legal(wk, bk, p, false) && !wins[state(wk, bk, p, false)]) {
                        bool king = false;
                        for (std::size_t t = 0; t < 64 && !king; ++t) {
                            if (t != wk && d(wk, t) == 1 && t != p && d(t, bk) > 1 && wins[state(t, bk, p, true)])
                                king = true;
                        }
                        const auto free      = [&](std::size_t t) { return t != wk && t != bk; };
                        const std::size_t st = p + 8;
                        const bool push      = free(st) && (p / 8 == 6 ? (d(bk, st) > 1 || d(wk, st) == 1)
                                                                       : wins[state(wk, bk, st, true)]);
                        const bool dbl = p / 8 == 1 && free(st) && free(st + 8) && wins[state(wk, bk, st + 8, true)];
                        if (king || push || dbl) {
                            wins[state(wk, bk, p, false)] = true;
                            changed                       = true;
                        }
                    }
                    // Black to move: no move that holds, and one move at least.
                    if (legal(wk, bk, p, true) && !wins[state(wk, bk, p, true)]) {
                        std::vector<std::size_t> to;
                        for (std::size_t t = 0; t < 64; ++t) {
                            if (t != bk && d(bk, t) == 1 && d(t, wk) > 1 && !hits(p, t)) to.push_back(t);
                        }
                        const bool lost = !to.empty() && std::all_of(to.begin(), to.end(), [&](std::size_t t) {
                            return t != p && wins[state(wk, t, p, false)];
                        });
                        if (lost) {
                            wins[state(wk, bk, p, true)] = true;
                            changed                      = true;
                        }
                    }
                }
            }
        }
        if (!changed) return wins;
    }
}

const std::vector<bool>& wins() {
    static const auto w = pawnEndings();
    return w;
}

std::optional<Position> kpk(std::size_t wk, std::size_t bk, std::size_t p, Color turn) {
    Board board;
    board.setPieceAt(square(static_cast<int>(wk)), {Color::White, Role::King});
    board.setPieceAt(square(static_cast<int>(bk)), {Color::Black, Role::King});
    board.setPieceAt(square(static_cast<int>(p)), {Color::White, Role::Pawn});
    return Position::fromBoard(board, turn);
}

}  // namespace

TEST_CASE("endgame: the opposition is taken in a pawn ending") {
    // Kd5 faces the king on d7: Black has to give way.
    CHECK(has(found("8/3k4/8/8/3KP3/8/8/8 w - - 0 1", "d4d5"), Fact{fact::Opposition{Color::White, false}}));
    // From afar: three squares between the kings.
    CHECK(found("3k4/8/8/8/8/4P3/8/4K3 w - - 0 1", "e1d1").empty());
    CHECK(has(found("3k4/8/8/8/8/4P3/4K3/8 w - - 0 1", "e2d2"), Fact{fact::Opposition{Color::White, true}}));
    // Along a rank, only kings two files apart are told.
    CHECK(found("8/8/8/8/1k3K2/8/4P3/8 w - - 0 1", "f4f5").empty());
    CHECK(found("8/8/8/1k6/5K2/8/4P3/8 w - - 0 1", "f4f5").empty());
    CHECK(has(found("8/8/8/3k4/5K2/8/4P3/8 w - - 0 1", "f4f5"), Fact{fact::Opposition{Color::White, false}}));
    // With a rook on the board, it is no pawn ending.
    CHECK_FALSE(any<fact::Opposition>(found("8/3k4/8/8/3KP3/8/8/7R w - - 0 1", "d4d5")));
}

TEST_CASE("endgame: the square of the pawn") {
    const auto a5 = placed(Color::White, Role::Pawn, Square::A5);
    // After a5 the pawn needs three moves; the king on f5, even to move, is five away from a8.
    CHECK(has(found("8/8/8/5k2/P7/8/8/7K w - - 0 1", "a4a5"), Fact{fact::Outruns{a5}}));
    // One file closer, the king steps into the square.
    CHECK(found("8/8/8/4k3/P7/8/8/7K w - - 0 1", "a4a5").empty());
    // The king enters the square of the a-pawn just in time.
    CHECK(has(found("8/8/8/P7/4k3/8/8/7K b - - 0 1", "e4d5"),
              Fact{fact::EntersSquare{placed(Color::Black, Role::King, Square::D5), a5}}));
    // A step that stays outside does not.
    CHECK(found("8/8/8/P7/4k3/8/8/7K b - - 0 1", "e4e5").empty());
    CHECK(outruns(pos("8/8/8/P7/5k2/8/8/7K w - - 0 1"), Square::A5));
    // A knight would stop the pawn: the rule says nothing.
    CHECK_FALSE(outruns(pos("8/8/8/P7/5k2/8/8/n6K w - - 0 1"), Square::A5));
}

TEST_CASE("endgame: a king on a key square wins") {
    int told = 0, wrong = 0;
    for (std::size_t wk = 0; wk < 64; ++wk) {
        for (std::size_t bk = 0; bk < 64; ++bk) {
            for (std::size_t p = 8; p < 56; ++p) {
                const auto king = square(static_cast<int>(wk)), enemy = square(static_cast<int>(bk)),
                           pawn  = square(static_cast<int>(p));
                const bool apart = wk != bk && wk != p && bk != p && distance(king, enemy) > 1;
                // As key_square tells it: the king has just come, the pawn is not simply taken.
                const bool taken = distance(enemy, pawn) <= 1 && distance(king, pawn) > 1;
                if (apart && !taken && keySquares(pawn, Color::White).contains(king)) {
                    const auto placed_   = kpk(wk, bk, p, Color::Black);
                    const bool stalemate = placed_ && placed_->isStalemate();
                    ++told;
                    if (!(stalemate || wins()[state(wk, bk, p, true)])) ++wrong;
                }
            }
        }
    }
    CHECK(wrong == 0);
    CHECK(told > 7000);
    // The king steps in front of its pawn, two ranks ahead.
    CHECK(has(found("4k3/8/8/3K4/4P3/8/8/8 w - - 0 1", "d5e6"),
              Fact{fact::KeySquare{placed(Color::White, Role::King, Square::E6),
                                   placed(Color::White, Role::Pawn, Square::E4)}}));
    // A rook's pawn has no such squares.
    CHECK_FALSE(any<fact::KeySquare>(found("6k1/8/8/6K1/7P/8/8/8 w - - 0 1", "g5g6")));
}

TEST_CASE("endgame: a pawn out of the square wins") {
    int told = 0, wrong = 0;
    for (std::size_t wk = 0; wk < 64; ++wk) {
        for (std::size_t bk = 0; bk < 64; ++bk) {
            for (std::size_t p = 8; p < 56; ++p) {
                // The king is at least as far from the promotion square as the pawn.
                const auto df  = bk % 8 > p % 8 ? bk % 8 - p % 8 : p % 8 - bk % 8;
                const bool far = std::max(df, 7 - bk / 8) >= 7 - p / 8;
                if (!far) continue;
                for (const bool black_to_move : {false, true}) {
                    const auto placed_ = kpk(wk, bk, p, black_to_move ? Color::Black : Color::White);
                    if (!placed_) continue;
                    if (outruns(*placed_, square(static_cast<int>(p)))) {
                        ++told;
                        if (!wins()[state(wk, bk, p, black_to_move)]) ++wrong;
                    }
                }
            }
        }
    }
    CHECK(wrong == 0);
    CHECK(told > 10000);
}

TEST_CASE("endgame: the rook endings of the books") {
    // The king steps in front of its pawn on the seventh, the black king three files away.
    CHECK(has(found("2K5/3P2k1/8/8/8/8/2r5/5R2 w - - 0 1", "c8d8"), Fact{fact::Lucena{Color::White}}));
    // With the black king next to the pawn, it is not that position.
    CHECK(found("2K5/3P4/4k3/8/8/8/2r5/5R2 w - - 0 1", "c8d8").empty());
    // The rook takes its third rank before the white king and pawn get there.
    CHECK(has(found("3k4/R7/8/3PK3/8/8/8/7r b - - 0 1", "h1h6"),
              Fact{fact::Philidor{placed(Color::Black, Role::Rook, Square::H6)}}));
    // Too late once the king is on the sixth rank.
    CHECK(found("3k4/R7/4K3/3P4/8/8/8/7r b - - 0 1", "h1h6").empty());
}

TEST_CASE("endgame: a bishop of the wrong colour does not win") {
    const auto wrong = [](std::string_view fen) { return wrongBishop(pos(fen).board(), Color::White).has_value(); };
    // The light-squared bishop cannot chase the king from h8.
    CHECK(wrong("7k/8/8/8/7P/8/8/3BK3 w - - 0 1"));
    // The dark-squared one can; and a knight's pawn wins with either.
    CHECK_FALSE(wrong("7k/8/8/8/7P/8/8/2B1K3 w - - 0 1"));
    CHECK_FALSE(wrong("7k/8/8/8/6P1/8/8/3BK3 w - - 0 1"));
    // The king is too far from the corner to be sure of it.
    CHECK_FALSE(wrong("8/8/8/8/3k3P/8/8/3BK3 w - - 0 1"));
}

TEST_CASE("endgame: a rook cuts the king off") {
    // Ra5: the black king cannot cross the fifth rank towards the b-pawn.
    CHECK(has(found("8/8/5k2/8/1P6/8/8/R3K3 w - - 0 1", "a1a5"),
              Fact{fact::CutsOff{placed(Color::White, Role::Rook, Square::A5),
                                 placed(Color::Black, Role::King, Square::F6), LineKind::Rank}}));
    // Re2: nor the e-file.
    CHECK(has(found("8/8/5k2/8/1P6/8/7R/4K3 w - - 0 1", "h2e2"),
              Fact{fact::CutsOff{placed(Color::White, Role::Rook, Square::E2),
                                 placed(Color::Black, Role::King, Square::F6), LineKind::File}}));
    // The pawn on the king's side of the file: nothing is cut off.
    CHECK(found("8/8/5k2/8/6P1/8/7R/4K3 w - - 0 1", "h2e2").empty());
}
