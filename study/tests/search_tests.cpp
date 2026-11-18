#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <random>
#include <string>
#include <vector>

#include <chess.hpp>
#include <study/import.hpp>
#include <study/scan_board.hpp>
#include <study/search.hpp>
#include <study/signature.hpp>
#include <study/store.hpp>

#include "test_games.hpp"

using namespace fcg::study;
using namespace fcg::test;
namespace fs = std::filesystem;

namespace {

constexpr int A1 = 0, B2 = 9, E2 = 12, A7 = 48, D7 = 51, E7 = 52, F7 = 53, G7 = 54, H7 = 55, F1 = 5, G1 = 6, G2 = 14,
              G3 = 22, F2 = 13, H2 = 15;

uint64_t bit(int square) { return uint64_t{1} << square; }

ScanBoard start() {
    ScanBoard b;
    REQUIRE(b.setFen(chess::constants::STARTPOS, false));
    return b;
}

ScanBoard after(const std::vector<std::string>& sans, ScanBoard b = start()) {
    for (const auto& san : sans) {
        const auto move = b.parseSan(san);
        REQUIRE(move);
        b.play(*move);
    }
    return b;
}

// Every game replayed to its end, with no signature: what a search must find.
std::vector<SearchHit> everyPly(const Snapshot& snap, const SearchQuery& query) {
    std::vector<SearchHit> hits;
    for (GameId id = 0; id < snap.games(); ++id)
        if (const auto ply = searchGame(snap, id, query, false)) hits.push_back({id, *ply});
    return hits;
}

// Games of every kind: random ones sharing their openings, promotions, starts of their own, Chess960, and long games.
std::shared_ptr<Database> mixed(const std::string& name) {
    auto games = randomGames(2500, 17, 10);
    const std::vector<Spec> specs{
        {"1. e4 d5 2. exd5 Qxd5 3. Nc3 Qxa2 4. Rxa2", '0'},
        {"1. h4 g5 2. hxg5 h6 3. gxh6 Nf6 4. h7 Ng8 5. hxg8=Q Rxg8", '1'},
        {"1. e4 e5 2. Ke2 Ke7 3. Ke1 Ke8", '='},
        {"8. Rd1 Rd8", '=', 0, 0, "2020.01.01", "7k/3r4/8/8/8/8/8/R3K3 w - - 0 8"},
        {"1. e4 e5", '*', 0, 0, "2020.01.01", "", true},
    };
    for (std::size_t i = 0; i < specs.size(); ++i) games.push_back(make(specs[i], 10000 + i));
    return database(name, std::move(games));
}

}  // namespace

TEST_CASE("signatures: when each pawn left its square, the material at the end") {
    const auto game = make(Spec{"1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Bxc6 dxc6 5. O-O Bg4 6. h3 Bxf3 7. Qxf3", '1'}, 0);
    REQUIRE(game.signature);
    const auto& s = *game.signature;
    CHECK(s.home[GameSignature::entry(E2)] == 1);  // the first position without the e2 pawn
    CHECK(s.home[GameSignature::entry(E7)] == 2);
    CHECK(s.home[GameSignature::entry(A7)] == 6);
    CHECK(s.home[GameSignature::entry(D7)] == 8);
    CHECK(s.home[GameSignature::entry(H2)] == 11);
    CHECK(s.home[GameSignature::entry(F2)] == GameSignature::STAYED);
    CHECK(s.home[GameSignature::entry(F7)] == GameSignature::STAYED);
    CHECK(s.material == std::array<uint8_t, 10>{8, 1, 1, 2, 1, 8, 1, 1, 2, 1});
    CHECK_FALSE(s.promoted);
    // The same from the codes, as a database makes it for a game that comes without one.
    CHECK(signatureOf(start(), game.moves, game.plies) == game.signature);

    // A pawn taken on its square; a pawn promoted.
    const auto taken = make(Spec{"1. e4 d5 2. exd5 Qxd5 3. Nc3 Qxa2", '0'}, 1);
    CHECK(taken.signature->home[GameSignature::entry(8)] == 6);  // a2
    CHECK(taken.signature->home[GameSignature::entry(B2)] == GameSignature::STAYED);
    const auto promoted = make(Spec{"1. h4 g5 2. hxg5 h6 3. gxh6 Nf6 4. h7 Ng8 5. hxg8=Q", '1'}, 2);
    CHECK(promoted.signature->promoted);
    CHECK(promoted.signature->material[QUEEN] == 2);
    // A start without pawns on their squares.
    const auto own = make(Spec{"8. Rd1 Rd8", '=', 0, 0, "2020.01.01", "7k/3r4/8/8/8/8/8/R3K3 w - - 0 8"}, 3);
    for (const auto gone : own.signature->home) CHECK(gone == 0);

    // The plies a game may reach a position at: after its pawns left, before they did.
    const auto window = plyWindow(s, game.plies, after({"e4", "e5", "Nf3", "Nc6", "Bb5", "a6"}));
    REQUIRE(window);
    CHECK(window->from == 6);
    CHECK(window->to == 7);  // the d7 pawn leaves after ply 7
    CHECK_FALSE(plyWindow(s, game.plies, after({"d4"})));
    CHECK_FALSE(plyWindow(taken.signature.value(), taken.plies, start()) == std::nullopt);
}

TEST_CASE("search: positions as every ply replayed finds them, the signatures leaving no game out") {
    auto db         = mixed("search-positions");
    const auto snap = db->snapshot();
    std::mt19937 random(3);
    // Positions of the games at plies of chance, and positions one move away from them, which few games reach.
    std::vector<ScanBoard> targets;
    for (int n = 0; n < 80; ++n) {
        const GameId id = random() % snap->games();
        const auto m    = snap->gameMoves(id);
        if (m.flags & (GAME_FEN | GAME_CHESS960)) continue;
        const uint32_t ply = m.plies ? random() % (m.plies + 1) : 0;
        ScanBoard b        = start();
        for (uint32_t i = 0; i < ply; ++i) b.play(b.decode(m.codes[i]));
        targets.push_back(b);
        chess::Movelist list;
        // One move further, by chess.hpp's legal moves: their SAN read on the board.
        chess::Board board(chess::constants::STARTPOS);
        ScanBoard again = start();
        for (uint32_t i = 0; i < ply; ++i) {
            const auto coded = again.decode(m.codes[i]);
            chess::Movelist legal;
            chess::movegen::legalmoves(legal, board);
            for (const auto& mv : legal) {
                const auto san = chess::uci::moveToSan(board, mv);
                if (again.parseSan(san) == coded) {
                    board.makeMove(mv);
                    break;
                }
            }
            again.play(coded);
        }
        chess::movegen::legalmoves(list, board);
        if (!list.empty()) {
            const auto mv = list[static_cast<int>(random() % list.size())];
            targets.push_back(after({chess::uci::moveToSan(board, mv)}, again));
        }
    }
    targets.push_back(after({"Rd1"}, [] {
        ScanBoard b;
        REQUIRE(b.setFen("7k/3r4/8/8/8/8/8/R3K3 w - - 0 8", false));
        return b;
    }()));
    targets.push_back(after({"e4", "d5", "exd5", "Qxd5", "Nc3", "Qxa2"}));
    uint64_t found = 0;
    for (const auto& target : targets) {
        const SearchQuery query = PositionQuery{target};
        const auto hits         = searchAll(snap, query, 4);
        REQUIRE(hits == everyPly(*snap, query));
        found += hits.size();
    }
    CHECK(found > 500);
}

TEST_CASE("search: material and patterns as every ply replayed finds them") {
    auto db         = mixed("search-patterns");
    const auto snap = db->snapshot();
    std::vector<SearchQuery> queries;
    {
        MaterialQuery no_queens;
        no_queens.high[QUEEN] = no_queens.high[5 + QUEEN] = 0;
        queries.push_back(no_queens);
        MaterialQuery rooks = no_queens;
        rooks.high[KNIGHT] = rooks.high[BISHOP] = rooks.high[5 + KNIGHT] = rooks.high[5 + BISHOP] = 0;
        rooks.low[ROOK] = rooks.high[ROOK] = rooks.low[5 + ROOK] = rooks.high[5 + ROOK] = 1;
        queries.push_back(rooks);
        MaterialQuery few  = MaterialQuery{};
        few.high[PAWN]     = 4;
        few.high[5 + PAWN] = 4;
        few.plies          = 6;
        queries.push_back(few);
        MaterialQuery two_queens;
        two_queens.low[QUEEN] = 2;
        queries.push_back(two_queens);
    }
    {
        PatternQuery castled;  // a king castled short, its pawns in front of it
        castled.men[WHITE_SIDE][KING] = bit(G1);
        castled.men[WHITE_SIDE][ROOK] = bit(F1);
        castled.men[WHITE_SIDE][PAWN] = bit(F2) | bit(G2) | bit(H2);
        queries.push_back(castled);
        PatternQuery fianchetto;  // a bishop on g2 before a pawn on g3, Black to move
        fianchetto.men[WHITE_SIDE][BISHOP] = bit(G2);
        fianchetto.men[WHITE_SIDE][PAWN]   = bit(G3);
        fianchetto.side_to_move            = BLACK_SIDE;
        queries.push_back(fianchetto);
        PatternQuery untouched;  // White's pawns all on their squares, the a1 corner empty
        for (int sq = 8; sq < 16; ++sq) untouched.men[WHITE_SIDE][PAWN] |= bit(sq);
        untouched.only_these_pawns[WHITE_SIDE] = true;
        untouched.empty                        = bit(A1);
        queries.push_back(untouched);
        PatternQuery kingside;  // Black's pawns: only f7, g7, h7
        kingside.men[BLACK_SIDE][PAWN]        = bit(F7) | bit(G7) | bit(H7);
        kingside.only_these_pawns[BLACK_SIDE] = true;
        queries.push_back(kingside);
    }
    std::size_t matched = 0;
    for (std::size_t q = 0; q < queries.size(); ++q) {
        CAPTURE(q);
        const auto hits = searchAll(snap, queries[q], 3);
        CHECK(hits == everyPly(*snap, queries[q]));
        if (!hits.empty()) ++matched;
    }
    CHECK(matched >= 5);
}

TEST_CASE("search: hits while it runs, and a cancel that stops it") {
    std::vector<ImportedGame> games;
    for (int round = 0; round < 2; ++round) {
        auto more = randomGames(2000, 40 + round, 6);
        for (auto& g : more) games.push_back(std::move(g));
    }
    auto db         = database("search-cancel", std::move(games));
    const auto snap = db->snapshot();
    PatternQuery castled;  // a king on g1 by a rook on f1: many games, after a few moves
    castled.men[WHITE_SIDE][KING] = bit(G1);
    castled.men[WHITE_SIDE][ROOK] = bit(F1);
    PatternQuery never;  // kings in the far corners: every game replayed to its end
    never.men[WHITE_SIDE][KING] = bit(63);
    never.men[BLACK_SIDE][KING] = bit(A1);
    const auto all              = searchAll(snap, castled, 2);
    {
        Search search(snap, castled, 2);
        for (const auto& h : search.hits()) {
            const auto it = std::lower_bound(all.begin(), all.end(), h,
                                             [](const SearchHit& a, const SearchHit& b) { return a.id < b.id; });
            CHECK((it != all.end() && *it == h));
        }
    }
    {
        Search search(snap, never, 2);
        const auto asked = std::chrono::steady_clock::now();
        search.cancel();
        search.wait();
        const auto took = std::chrono::steady_clock::now() - asked;
        CHECK(search.done());
        CHECK(search.cancelled());
        // A twentieth of a second wanted; a loaded machine, or the sanitizers, get more.
        CHECK(took < std::chrono::seconds(2));
        CHECK(search.hits().empty());
        CHECK(search.searched() <= snap->games());
    }
    Search whole(snap, castled, 3);
    whole.wait();
    CHECK(whole.done());
    CHECK(whole.searched() == snap->games());
    CHECK(whole.hits() == all);
}
