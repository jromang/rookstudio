// The piece letters of each language (#66, mockup M13, board 7, point 21): the notation written in them, a move typed
// in them or in English letters, a letter with two meanings offering both families, the PGN still in English SAN.

#include <algorithm>
#include <string>
#include <vector>

#include <doctest/doctest.hpp>
#include <study/game_tree.hpp>

#include "board_page.hpp"
#include "board_text.hpp"
#include "i18n.hpp"
#include "piece_letters.hpp"

using namespace fcg;
using L = i18n::Language;

namespace {

std::vector<std::string> shownOf(const std::vector<TypedMove>& moves) {
    std::vector<std::string> out;
    for (const auto& m : moves) out.push_back(m.shown);
    return out;
}

bool allPiece(const std::vector<TypedMove>& moves, std::size_t from, std::size_t to, chess::PieceType type,
              const chess::Board& board) {
    for (std::size_t i = from; i < to; ++i)
        if (board.at(moves[i].move.from()).type() != type) return false;
    return true;
}

}  // namespace

TEST_CASE("piece letters: each language's, English's for Chinese") {
    CHECK(pieceLetters(L::English) == std::array<std::string_view, 5>{"K", "Q", "R", "B", "N"});
    CHECK(pieceLetters(L::French) == std::array<std::string_view, 5>{"R", "D", "T", "F", "C"});
    CHECK(pieceLetters(L::German) == std::array<std::string_view, 5>{"K", "D", "T", "L", "S"});
    CHECK(pieceLetters(L::Spanish) == std::array<std::string_view, 5>{"R", "D", "T", "A", "C"});
    CHECK(pieceLetters(L::Italian) == std::array<std::string_view, 5>{"R", "D", "T", "A", "C"});
    CHECK(pieceLetters(L::Portuguese) == std::array<std::string_view, 5>{"R", "D", "T", "B", "C"});
    CHECK(pieceLetters(L::Russian) == std::array<std::string_view, 5>{"Кр", "Ф", "Л", "С", "К"});
    CHECK(pieceLetters(L::Chinese) == pieceLetters(L::English));
}

TEST_CASE("piece letters: a SAN written in a language's letters, its files, castling and signs as they are") {
    CHECK(sanIn("Nbd2", L::German) == "Sbd2");  // the file b stays a file
    CHECK(sanIn("Bxh7+", L::German) == "Lxh7+");
    CHECK(sanIn("Qh5#", L::Spanish) == "Dh5#");
    CHECK(sanIn("Bb5", L::Italian) == "Ab5");
    CHECK(sanIn("Bb5", L::Portuguese) == "Bb5");
    CHECK(sanIn("Rxe8", L::French) == "Txe8");  // the rook, not the king
    CHECK(sanIn("Kd3", L::French) == "Rd3");
    CHECK(sanIn("e8=Q+", L::French) == "e8=D+");
    CHECK(sanIn("bxa1=N", L::Spanish) == "bxa1=C");
    CHECK(sanIn("Nf3", L::Russian) == "Кf3");
    CHECK(sanIn("Kxe2", L::Russian) == "Крxe2");
    CHECK(sanIn("Rd1", L::Russian) == "Лd1");
    CHECK(sanIn("Bc4", L::Russian) == "Сc4");
    CHECK(sanIn("e8=Q", L::Russian) == "e8=Ф");
    CHECK(sanIn("Nf3", L::Chinese) == "Nf3");
    for (const auto& info : i18n::languages()) {
        CHECK(sanIn("O-O-O", info.language) == "O-O-O");
        CHECK(sanIn("exd5", info.language) == "exd5");
        CHECK(sanIn("", info.language).empty());
    }
    // The French letters as the assistant writes them are the same.
    for (const char* san : {"Rxe8", "Kd3", "Qxd8+", "Nbd2", "Bxh7#", "e8=Q+", "bxa1=N", "O-O"})
        CHECK(sanIn(san, L::French) == frenchSan(san));
}

TEST_CASE("piece letters: a text typed read as English SAN, the longest letter first") {
    CHECK(englishTyped("Re1", L::French) == "Ke1");
    CHECK(englishTyped("Te1", L::French) == "Re1");
    CHECK(englishTyped("e8=D", L::French) == "e8=Q");
    CHECK(englishTyped("Sf3", L::German) == "Nf3");
    CHECK(englishTyped("Lxb5", L::German) == "Bxb5");
    CHECK(englishTyped("Крe2", L::Russian) == "Ke2");
    CHECK(englishTyped("Кf3", L::Russian) == "Nf3");
    CHECK(englishTyped("Лxd8+", L::Russian) == "Rxd8+");
    CHECK(englishTyped("0-0", L::German) == "O-O");
    CHECK(englishTyped("Re1", L::English) == "Re1");
    CHECK(englishTyped("Nf3", L::Chinese) == "Nf3");
}

TEST_CASE("piece letters: which characters open the move field") {
    for (const auto& info : i18n::languages()) {
        for (const char32_t c : {U'a', U'h', U'O', U'0', U'K', U'Q', U'R', U'B', U'N'})
            CHECK(beginsMove(c, info.language));  // files, castling, English letters always
        CHECK_FALSE(beginsMove(U'i', info.language));
        CHECK_FALSE(beginsMove(U'x', info.language));
    }
    CHECK(beginsMove(U'S', L::German));
    CHECK(beginsMove(U'L', L::German));
    CHECK(beginsMove(U'D', L::German));
    CHECK_FALSE(beginsMove(U'S', L::French));
    CHECK(beginsMove(U'F', L::French));
    CHECK(beginsMove(U'A', L::Spanish));
    CHECK(beginsMove(U'A', L::Italian));
    CHECK_FALSE(beginsMove(U'A', L::Portuguese));
    CHECK(beginsMove(U'К', L::Russian));
    CHECK(beginsMove(U'Ф', L::Russian));
    CHECK(beginsMove(U'Л', L::Russian));
    CHECK(beginsMove(U'С', L::Russian));
    CHECK_FALSE(beginsMove(U'Ж', L::Russian));
    CHECK_FALSE(beginsMove(U'К', L::German));
    CHECK_FALSE(beginsMove(U'S', L::Chinese));
}

TEST_CASE("piece letters: R gives the king's moves, then the rook's marked as English, in four languages") {
    // The white king on e1, rooks on a1 and h1: R is the king in French, Spanish, Italian and Portuguese.
    const chess::Board board("4k3/8/8/8/8/8/8/R3K2R w - - 0 1");
    for (const auto language : {L::French, L::Spanish, L::Italian, L::Portuguese}) {
        CAPTURE(static_cast<int>(language));
        const auto moves = typedMoves(board, "R", language);
        // The king's five moves, then the rooks' nineteen.
        REQUIRE(moves.size() == 5 + 19);
        CHECK(allPiece(moves, 0, 5, chess::PieceType::KING, board));
        CHECK(allPiece(moves, 5, moves.size(), chess::PieceType::ROOK, board));
        for (std::size_t i = 0; i < 5; ++i) {
            CHECK_FALSE(moves[i].english);
            CHECK(moves[i].shown[0] == 'R');
            CHECK(moves[i].san[0] == 'K');
        }
        for (std::size_t i = 5; i < moves.size(); ++i) {
            REQUIRE(moves[i].english);
            CHECK(*moves[i].english == chess::PieceType::ROOK);
            CHECK(moves[i].shown[0] == 'T');  // shown in the language's letters
            CHECK(moves[i].san[0] == 'R');
        }
        // "Re2": the king; "Ra2": the rook only, the king cannot.
        const auto e2 = typedMoves(board, "Re2", language);
        REQUIRE(e2.size() == 1);
        CHECK(e2[0].san == "Ke2");
        const auto a2 = typedMoves(board, "Ra2", language);
        REQUIRE(a2.size() == 1);
        CHECK(a2[0].san == "Ra2");
        CHECK(a2[0].english == chess::PieceType::ROOK);
        // T is the rook in the language: no mark.
        const auto t = typedMoves(board, "T", language);
        CHECK(t.size() == 19);
        CHECK(std::none_of(t.begin(), t.end(), [](const TypedMove& m) { return m.english.has_value(); }));
    }
    // In English, R is the rook alone; in German, R is no letter of its own: the English rook, unmarked.
    const auto english = typedMoves(board, "R", L::English);
    CHECK(english.size() == 19);
    CHECK(allPiece(english, 0, english.size(), chess::PieceType::ROOK, board));
    const auto german = typedMoves(board, "R", L::German);
    REQUIRE(german.size() == 19);
    CHECK(std::none_of(german.begin(), german.end(), [](const TypedMove& m) { return m.english.has_value(); }));
    CHECK(german[0].shown[0] == 'T');
}

TEST_CASE("piece letters: in Russian, К gives the knight's moves and the king's, Кр the king's only") {
    // The white king on e1, knights on b1 and g1.
    const chess::Board board("4k3/8/8/8/8/8/8/1N2K1N1 w - - 0 1");
    const auto k = typedMoves(board, "К", L::Russian);
    // The knights' six moves, then the king's five.
    REQUIRE(k.size() == 6 + 5);
    CHECK(allPiece(k, 0, 6, chess::PieceType::KNIGHT, board));
    CHECK(allPiece(k, 6, k.size(), chess::PieceType::KING, board));
    CHECK(std::none_of(k.begin(), k.end(), [](const TypedMove& m) { return m.english.has_value(); }));
    CHECK(k[0].shown.rfind("К", 0) == 0);
    CHECK(k[6].shown.rfind("Кр", 0) == 0);
    const auto kr = typedMoves(board, "Кр", L::Russian);
    REQUIRE(kr.size() == 5);
    CHECK(allPiece(kr, 0, kr.size(), chess::PieceType::KING, board));
    const auto kf = typedMoves(board, "Кf3", L::Russian);
    REQUIRE(kf.size() == 1);
    CHECK(kf[0].san == "Nf3");
    // English letters too: N the knights, K the king, shown in Russian letters, unmarked (no Russian letter is Latin).
    const auto n = typedMoves(board, "N", L::Russian);
    REQUIRE(n.size() == 6);
    CHECK(n[0].shown.rfind("К", 0) == 0);
    CHECK_FALSE(n[0].english);
    const auto king = typedMoves(board, "Ke2", L::Russian);
    REQUIRE(king.size() == 1);
    CHECK(king[0].shown == "Крe2");
}

TEST_CASE("piece letters: German letters and English ones find the same moves (the mockup's Sg5, Sg1)") {
    // After 1.e4 e5 2.Nf3 Nc6 3.Bc4 Nf6.
    const chess::Board board("r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4");
    const auto s = typedMoves(board, "Sg", L::German);
    CHECK(shownOf(s) == std::vector<std::string>{"Sg5", "Sg1"});
    const auto n = typedMoves(board, "Ng", L::German);
    CHECK(shownOf(n) == std::vector<std::string>{"Sg5", "Sg1"});
    CHECK(std::none_of(n.begin(), n.end(), [](const TypedMove& m) { return m.english.has_value(); }));
    // L the bishop, B its English letter.
    CHECK(shownOf(typedMoves(board, "Lb5", L::German)) == std::vector<std::string>{"Lb5"});
    CHECK(shownOf(typedMoves(board, "Bb5", L::German)) == std::vector<std::string>{"Lb5"});
    // Castling typed with zeros, a symbol typed after the move.
    CHECK(shownOf(typedMoves(board, "0-0", L::German)) == std::vector<std::string>{"O-O"});
    CHECK(shownOf(typedMoves(board, "Sg5!?", L::German)) == std::vector<std::string>{"Sg5"});
    CHECK(typedMoves(board, "Sx", L::German).size() == 1);  // Nxe5
    CHECK(typedMoves(board, "", L::German).empty());
    CHECK(typedMoves(board, "Xg5", L::German).empty());
}

TEST_CASE("piece letters: English letters in every language, a queen first among promotions") {
    const chess::Board start;
    for (const auto& info : i18n::languages()) {
        CAPTURE(info.code);
        const auto moves = typedMoves(start, "Nf3", info.language);
        REQUIRE(moves.size() == 1);
        CHECK(moves[0].san == "Nf3");
        CHECK(moves[0].shown == sanIn("Nf3", info.language));
    }
    // A pawn on the seventh rank: "e8" offers the four promotions, the queen first, in the language's letters.
    const chess::Board seventh("8/4P3/8/8/8/8/k7/4K3 w - - 0 1");
    const auto french = typedMoves(seventh, "e8", L::French);
    CHECK(shownOf(french) == std::vector<std::string>{"e8=D", "e8=T", "e8=F", "e8=C"});
    const auto queen = typedMoves(seventh, "e8=Q", L::French);  // English letters
    REQUIRE(queen.size() == 1);
    CHECK(queen[0].shown == "e8=D");
    const auto rook = typedMoves(seventh, "e8=R", L::French);  // R is no promotion in French: the English rook
    REQUIRE(rook.size() == 1);
    CHECK(rook[0].shown == "e8=T");
    CHECK(rook[0].english == chess::PieceType::ROOK);
    CHECK(shownOf(typedMoves(seventh, "e8=Ф", L::Russian)) == std::vector<std::string>{"e8=Ф"});
}

TEST_CASE("piece letters: a move typed in German letters is written in English SAN in the PGN") {
    ReplayGame game;
    game.start_fen = std::string(chess::constants::STARTPOS);
    chess::Board board;
    int ply = 0;
    for (const char* typed : {"e4", "e5", "Sf3", "Sc6", "Lb5"}) {
        const auto moves = typedMoves(board, typed, L::German);
        REQUIRE(moves.size() >= 1);
        ply = playOnGame(game, false, ply, moves.front().move);
        board.makeMove(moves.front().move);
    }
    const auto pgn = gamePgn(game, false);
    CHECK(pgn.find("2. Nf3 Nc6 3. Bb5") != std::string::npos);
    CHECK(pgn.find("Sf3") == std::string::npos);
    CHECK(pgn.find("Lb5") == std::string::npos);
}
