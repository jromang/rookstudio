#include <doctest/doctest.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <random>
#include <string>
#include <vector>

#include <chess.hpp>

#include <study/scan_board.hpp>

using namespace fcg::study;

namespace {

CodedMove fromChess(chess::Move m) {
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

// The same pieces on the same squares, and the same Polyglot key (castling rights, en passant and the side to move
// included), as a chess.hpp board played with makeMove<false> (Polyglot's en passant).
bool samePosition(const ScanBoard& ours, const chess::Board& theirs) {
    if (ours.key() != theirs.hash()) return false;
    for (int square = 0; square < 64; ++square) {
        const auto piece   = theirs.at(chess::Square(square));
        const int expected = piece == chess::Piece::NONE
                                 ? 0
                                 : 1 + static_cast<int>(piece.type()) + (piece.color() == chess::Color::BLACK ? 6 : 0);
        if (ours.on(square) != expected) return false;
    }
    return true;
}

// A Chess960 start position, in Shredder-FEN (the rooks' files as the castling rights).
std::string random960(std::mt19937& random) {
    std::array<char, 8> rank{};
    std::vector<int> free{0, 1, 2, 3, 4, 5, 6, 7};
    const auto take = [&](int index) {
        const int file = free[static_cast<std::size_t>(index)];
        free.erase(free.begin() + index);
        return file;
    };
    const int dark                        = 2 * static_cast<int>(random() % 4);
    const int light                       = 2 * static_cast<int>(random() % 4) + 1;
    rank[static_cast<std::size_t>(dark)]  = 'B';
    rank[static_cast<std::size_t>(light)] = 'B';
    free.erase(std::remove(free.begin(), free.end(), dark), free.end());
    free.erase(std::remove(free.begin(), free.end(), light), free.end());
    rank[static_cast<std::size_t>(take(static_cast<int>(random() % free.size())))] = 'Q';
    rank[static_cast<std::size_t>(take(static_cast<int>(random() % free.size())))] = 'N';
    rank[static_cast<std::size_t>(take(static_cast<int>(random() % free.size())))] = 'N';
    // R K R in the three files left.
    rank[static_cast<std::size_t>(free[0])] = 'R';
    rank[static_cast<std::size_t>(free[1])] = 'K';
    rank[static_cast<std::size_t>(free[2])] = 'R';
    std::string white(rank.begin(), rank.end()), black = white;
    for (auto& c : black) c = static_cast<char>(c - 'A' + 'a');
    const char long_rook = static_cast<char>('A' + free[0]), short_rook = static_cast<char>('A' + free[2]);
    const std::string rights{short_rook, long_rook, static_cast<char>(short_rook - 'A' + 'a'),
                             static_cast<char>(long_rook - 'A' + 'a')};
    return black + "/pppppppp/8/8/8/8/PPPPPPPP/" + white + " w " + rights + " - 0 1";
}

// The codes of the side to move counted again from its pieces: what the board keeps as they come and go.
int countedCodes(const ScanBoard& board) {
    const int side = board.sideToMove();
    int count      = 1;
    for (int kind = PAWN; kind <= KING; ++kind) {
        for (uint64_t b = board.pieces(side, kind); b; b &= b - 1) {
            const int square  = __builtin_ctzll(b);
            const int rank    = side == WHITE_SIDE ? square / 8 : 7 - square / 8;
            const int codes[] = {rank == 6 ? 12 : 4, 8, 14, 14, 28, 10};
            count += codes[kind];
        }
    }
    return count;
}

}  // namespace

TEST_CASE("codec: random games, standard and Chess960, the same as chess.hpp at every ply") {
    std::mt19937 random(20261117);
    long plies = 0, nulls = 0, promotions = 0, castlings = 0, en_passant = 0, miscounted = 0;
    std::vector<std::vector<int>> games;
    std::vector<std::string> starts;
    for (int game = 0; game < 1200; ++game) {
        const bool chess960 = game % 3 == 0;
        chess::Board board;
        board.set960(chess960);
        const auto fen = chess960 ? random960(random) : std::string(chess::constants::STARTPOS);
        board.setFen(fen);
        ScanBoard ours;
        REQUIRE(ours.set(board));
        REQUIRE(samePosition(ours, board));
        starts.push_back(fen);
        games.emplace_back();
        for (int ply = 0; ply < 400; ++ply) {
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, board);
            if (moves.empty()) break;
            if (!board.inCheck() && random() % 64 == 0) {
                CodedMove null;
                null.flags     = MOVE_NULL;
                const int code = ours.encode(null);
                REQUIRE(code == ours.codeCount() - 1);
                REQUIRE(ours.decode(code) == null);
                REQUIRE(ours.parseSan("--") == null);
                games.back().push_back(code);
                ours.play(null);
                board.makeNullMove();
                REQUIRE(samePosition(ours, board));
                ++nulls;
                continue;
            }
            const auto move  = moves[static_cast<int>(random() % static_cast<unsigned>(moves.size()))];
            const auto coded = fromChess(move);
            const int code   = ours.encode(coded);
            INFO(board.getFen(), " ", chess::uci::moveToUci(move, chess960));
            REQUIRE(code >= 0);
            REQUIRE(code < ours.codeCount() - 1);
            REQUIRE(ours.decode(code) == coded);
            const auto san    = chess::uci::moveToSan(board, move);
            const auto parsed = ours.parseSan(san);
            INFO(san);
            REQUIRE(parsed);
            REQUIRE(*parsed == coded);
            games.back().push_back(code);
            promotions += coded.promotion != NO_KIND;
            castlings += (coded.flags & (MOVE_SHORT_CASTLE | MOVE_LONG_CASTLE)) != 0;
            en_passant += (coded.flags & MOVE_EN_PASSANT) != 0;
            ours.play(coded);
            miscounted += ours.codeCount() != countedCodes(ours);
            board.makeMove<false>(move);
            REQUIRE(samePosition(ours, board));
            ++plies;
        }
    }
    MESSAGE(plies << " plies, " << nulls << " null moves, " << promotions << " promotions, " << castlings
                  << " castlings, " << en_passant << " en passant captures");
    CHECK(miscounted == 0);
    CHECK(promotions > 100);
    CHECK(castlings > 100);
    CHECK(en_passant > 3);

    // Decoding and playing them again, the speed the databases' scans rely on.
    const auto begin = std::chrono::steady_clock::now();
    long replayed    = 0;
    uint64_t check   = 0;
    for (std::size_t g = 0; g < games.size(); ++g) {
        ScanBoard board;
        REQUIRE(board.setFen(starts[g], g % 3 == 0));
        for (const int code : games[g]) {
            board.play(board.decode(code));
            ++replayed;
        }
        check ^= board.key();
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    MESSAGE(replayed << " plies decoded and played in " << seconds << " s: " << replayed / seconds / 1e6
                     << " M plies/s (" << check << ")");
}

TEST_CASE("codec: more queens than one byte holds, and SAN as other programs write it") {
    // Fifteen queens behind a wall of pawns: more than 256 codes, the pawns' above 255, each still one code.
    const char* queens = "7k/8/8/8/8/PPPPPPPP/QQQQQQQQ/QQQQQQQK w - - 0 1";
    ScanBoard many;
    REQUIRE(many.setFen(queens, false));
    CHECK(many.codeCount() > 256);
    chess::Board board(queens);
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    REQUIRE(moves.size() > 0);
    int high = 0;
    for (const auto& m : moves) {
        const auto coded = fromChess(m);
        const int code   = many.encode(coded);
        high += code > 255;
        CHECK(many.decode(code) == coded);
    }
    CHECK(high > 0);

    ScanBoard b;
    REQUIRE(b.setFen("r3k2r/pppq1ppp/2n1bn2/3pp3/1b1PP3/2N1BN2/PPPQ1PPP/R3KB1R w KQkq - 0 1", false));
    // Castling: the long one is free, the short one blocked by the bishop on f1; zeros read as letters.
    CHECK(b.parseSan("Nd1"));  // c3 is not pinned: the queen on d2 stands between b4 and e1
    CHECK(b.parseSan("O-O-O"));
    CHECK(b.parseSan("0-0-0") == b.parseSan("O-O-O"));
    CHECK(!b.parseSan("O-O"));                   // f1 is taken
    CHECK(b.parseSan("e2-e4") == std::nullopt);  // e2 is empty here

    ScanBoard pin;
    // The knight on d2 is pinned by the bishop on b4; "Nf3" can only be the g1 knight's, written without a file.
    REQUIRE(pin.setFen("4k3/8/8/8/1b6/8/3N4/4K1N1 w - - 0 1", false));
    const auto nf3 = pin.parseSan("Nf3");
    REQUIRE(nf3);
    CHECK(nf3->from == 6);  // g1
    CHECK(!pin.parseSan("Ndf3"));
    CHECK(pin.parseSan("Ngf3") == nf3);
    CHECK(pin.parseSan("Nf3+!?") == nf3);
    CHECK(!pin.parseSan("Qe2"));
    CHECK(!pin.parseSan("Kd1x"));

    ScanBoard ep;
    REQUIRE(ep.setFen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", false));
    const auto exd6 = ep.parseSan("exd6");
    REQUIRE(exd6);
    CHECK((exd6->flags & MOVE_EN_PASSANT) != 0);
    ScanBoard promo;
    REQUIRE(promo.setFen("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1", false));
    CHECK(promo.parseSan("b8=N")->promotion == KNIGHT);
    CHECK(promo.parseSan("b8Q")->promotion == QUEEN);
    CHECK(!promo.parseSan("b8"));  // a promotion must say into what
}

TEST_CASE("codec: any code of a position played, its move legal or not, leaves a board that holds together (#102)") {
    // A damaged game's codes are within the position's count but name moves no legal play makes: decode() checks no
    // legality, and play() must not break the board (a castling without its rook took a man from an empty square,
    // out of the bitboards' bounds). Random walks of any codes, from random positions of legal play.
    std::mt19937 random(102);
    long played = 0;
    for (int walk = 0; walk < 400; ++walk) {
        const bool chess960 = walk % 3 == 0;
        chess::Board legal;
        legal.set960(chess960);
        legal.setFen(chess960 ? random960(random) : std::string(chess::constants::STARTPOS));
        for (int ply = static_cast<int>(random() % 40); ply > 0; --ply) {
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, legal);
            if (moves.empty()) break;
            legal.makeMove<false>(moves[static_cast<int>(random() % static_cast<unsigned>(moves.size()))]);
        }
        ScanBoard board;
        REQUIRE(board.set(legal));
        for (int step = 0; step < 60; ++step) {
            const int count = board.codeCount();
            if (count <= 1) break;  // no man of the side to move left: only the null move
            board.play(board.decode(static_cast<int>(random() % static_cast<unsigned>(count))));
            ++played;
            // Each man on one square, its square in its bitboard and in no other, every bitboard's men on a square.
            int men = 0;
            for (int square = 0; square < 64; ++square) {
                const int piece = board.on(square);
                if (piece == 0) {
                    REQUIRE(((board.occupied() >> square) & 1) == 0);
                    continue;
                }
                ++men;
                const int side = (piece - 1) / 6, kind = (piece - 1) % 6;
                REQUIRE(((board.pieces(side, kind) >> square) & 1) == 1);
            }
            int bits = 0;
            for (int side = 0; side < 2; ++side)
                for (int kind = PAWN; kind <= KING; ++kind) bits += __builtin_popcountll(board.pieces(side, kind));
            REQUIRE(bits == men);
            REQUIRE(board.codeCount() == countedCodes(board));
        }
    }
    MESSAGE(played << " codes played, legal or not");
}

TEST_CASE("codec: a game's codes read back as written, the game ended at a code that cannot be read (#112)") {
    // Fifteen queens: White's codes take two bytes, the lone black king's one.
    ScanBoard board;
    REQUIRE(board.setFen("7k/8/8/8/8/PPPPPPPP/QQQQQQQQ/QQQQQQQK w - - 0 1", false));
    const ScanBoard start = board;
    std::vector<uint8_t> codes;
    std::vector<int> played;
    for (const char* san : {"a4", "Kg8", "b4", "Kh8", "c4"}) {
        CAPTURE(san);
        const auto move = board.parseSan(san);
        REQUIRE(move);
        played.push_back(board.encode(*move));
        appendCode(codes, board, *move);
        board.play(*move);
    }
    CHECK(codes.size() == 3 * 2 + 2 * 1);
    {
        ScanBoard again = start;
        Cursor cursor(codes, 5);
        for (const int code : played) {
            CHECK(cursor.next(again) == code);
            again.play(again.decode(code));
        }
        CHECK(cursor.next(again) == Cursor::END);
        CHECK(cursor.ply() == 5);
        CHECK(cursor.bytes() == codes.size());
        CHECK(again.key() == board.key());
    }
    // Fewer plies than codes: the plies count, the bytes after them are not read (the check counts them).
    {
        ScanBoard again = start;
        Cursor cursor(codes, 2);
        for (int k = 0; k < 2; ++k) {
            const int code = cursor.next(again);
            REQUIRE(code == played[static_cast<std::size_t>(k)]);
            again.play(again.decode(code));
        }
        CHECK(cursor.next(again) == Cursor::END);
        CHECK(cursor.ply() == 2);
        CHECK(cursor.bytes() == 3);
    }
    // A two-byte code cut short (a vector of the bytes alone, which ASan watches): the game ends before it, and stays
    // ended.
    {
        const std::vector<uint8_t> cut(codes.begin(), codes.begin() + 1);
        ScanBoard again = start;
        Cursor cursor(cut, 5);
        CHECK(cursor.next(again) == Cursor::END);
        CHECK(cursor.next(again) == Cursor::END);
        CHECK(cursor.ply() == 0);
    }
    // A code past the position's (the black king has 11): the game ends there, and nothing after it is read though
    // its bytes go on.
    {
        auto damaged    = codes;
        damaged[2]      = 200;
        ScanBoard again = start;
        Cursor cursor(damaged, 5);
        const int first = cursor.next(again);
        REQUIRE(first == played[0]);
        again.play(again.decode(first));
        CHECK(cursor.next(again) == Cursor::END);
        CHECK(cursor.next(again) == Cursor::END);
        CHECK(cursor.ply() == 1);
        CHECK(cursor.bytes() == 3);
    }
    // From a later ply of one-byte codes, as the opening tree reads its games on.
    {
        ScanBoard standard;
        REQUIRE(standard.setFen(chess::constants::STARTPOS, false));
        std::vector<uint8_t> line;
        std::vector<ScanBoard> positions{standard};
        for (const char* san : {"e4", "e5", "Nf3", "Nc6"}) {
            const auto move = positions.back().parseSan(san);
            REQUIRE(move);
            appendCode(line, positions.back(), *move);
            positions.push_back(positions.back());
            positions.back().play(*move);
        }
        REQUIRE(line.size() == 4);
        ScanBoard again = positions[2];
        Cursor cursor(line, 4, 2);
        for (int ply = 2; ply < 4; ++ply) {
            const int code = cursor.next(again);
            REQUIRE(code == line[static_cast<std::size_t>(ply)]);
            again.play(again.decode(code));
        }
        CHECK(cursor.next(again) == Cursor::END);
        CHECK(again.key() == positions[4].key());
    }
}
