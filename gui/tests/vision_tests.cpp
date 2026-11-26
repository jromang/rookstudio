// Board vision (#82): the coordinates, what a blindfold board shows, "Where is…?", a line read in one's head, the
// records, read with their types checked (#136).
#include <doctest/doctest.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include <json.hpp>

#include "console.hpp"
#include "test_support.hpp"
#include "vision.hpp"

using namespace fcg;
namespace fs = std::filesystem;

TEST_CASE("vision: the coordinates, named and found") {
    for (int i = 0; i < 64; ++i) {
        const chess::Square sq(i);
        const auto name = squareName(sq);
        CHECK(name.size() == 2);
        CHECK(typedSquare(name) == sq);
    }
    CHECK(typedSquare("E5") == chess::Square(chess::File::FILE_E, chess::Rank::RANK_5));
    CHECK_FALSE(typedSquare("e9"));
    CHECK_FALSE(typedSquare("i1"));
    CHECK_FALSE(typedSquare("e"));
    CHECK(coordKey(CoordMode::Find, CoordSide::White) == "find-white");
    CHECK(coordKey(CoordMode::Name, CoordSide::Random) == "name-random");
    // Never the same square twice in a row, every square in time.
    std::mt19937 rng(82);
    std::optional<chess::Square> last;
    std::set<int> seen;
    for (int i = 0; i < 2000; ++i) {
        const auto sq = nextCoordinate(rng, last);
        if (last) CHECK(sq != *last);
        seen.insert(sq.index());
        last = sq;
    }
    CHECK(seen.size() == 64);
}

TEST_CASE("vision: what a blindfold board shows, and Where is…?") {
    const chess::Board start;
    const auto none = shownPieces(start, Hidden::All, false);
    for (const auto p : none) CHECK(p == chess::Piece::NONE);
    const auto all = shownPieces(start, Hidden::All, true);
    CHECK(all[chess::Square(chess::File::FILE_E, chess::Rank::RANK_1).index()] == chess::Piece::WHITEKING);
    const auto pawns = shownPieces(start, Hidden::AllButPawns, false);
    int shown        = 0;
    for (const auto p : pawns) {
        if (p == chess::Piece::NONE) continue;
        ++shown;
        CHECK(p.type() == chess::PieceType::PAWN);
    }
    CHECK(shown == 16);
    // Shown until the 10th move, hidden after it.
    CHECK(shownPieces(chess::Board("4k3/8/8/8/8/8/8/4K3 w - - 0 10"), Hidden::AfterTen, false)[4] ==
          chess::Piece::WHITEKING);
    CHECK(shownPieces(chess::Board("4k3/8/8/8/8/8/8/4K3 w - - 0 11"), Hidden::AfterTen, false)[4] ==
          chess::Piece::NONE);

    // The pieces asked for are those their words name alone, and they stand on the square given.
    std::mt19937 rng(7);
    std::set<std::string> texts;
    for (int i = 0; i < 200; ++i) {
        const auto q = pieceQuestion(start, rng);
        REQUIRE(q);
        texts.insert(q->text);
        const auto piece       = start.at(q->square);
        const std::string text = q->text;
        CHECK(piece != chess::Piece::NONE);
        CHECK((text.find("white") != std::string::npos) == (piece.color() == chess::Color::WHITE));
        if (text.find("queen") != std::string::npos) CHECK(piece.type() == chess::PieceType::QUEEN);
        if (text.find("bishop") != std::string::npos) CHECK(piece.type() == chess::PieceType::BISHOP);
        if (text.find("light-squared") != std::string::npos) CHECK((q->square.file() + q->square.rank()) % 2 == 1);
    }
    // Two rooks and two knights a side name neither; the queens and the bishops by their squares do.
    CHECK(texts == std::set<std::string>{"the white queen", "the black queen", "the white light-squared bishop",
                                         "the white dark-squared bishop", "the black light-squared bishop",
                                         "the black dark-squared bishop"});
    // Kings alone: the kings.
    const auto kings = pieceQuestion(chess::Board("4k3/8/8/8/8/8/8/4K3 w - - 0 1"), rng);
    REQUIRE(kings);
    CHECK(chess::Board("4k3/8/8/8/8/8/8/4K3 w - - 0 1").at(kings->square).type() == chess::PieceType::KING);
}

TEST_CASE("vision: a line read in one's head, its answer what the moves leave") {
    // Every opening's moves play from the start.
    for (const auto& o : headOpenings()) {
        CAPTURE(o.name);
        chess::Board board;
        std::istringstream words(o.moves);
        std::string san;
        int plies = 0;
        while (words >> san) {
            chess::Move m = chess::Move::NO_MOVE;
            CHECK_NOTHROW(m = chess::uci::parseSan(board, san));
            REQUIRE(m != chess::Move::NO_MOVE);
            board.makeMove(m);
            ++plies;
        }
        CHECK(plies >= 4 + HEAD_MOVES);
    }
    std::mt19937 rng(82);
    for (int i = 0; i < 300; ++i) {
        const auto line = headLine(rng);
        REQUIRE_FALSE(line.fen.empty());
        REQUIRE(line.moves.size() == HEAD_MOVES);
        chess::Board board(line.fen);
        const auto before = board.at(line.square);
        for (const auto m : line.moves) board.makeMove(m);
        CHECK(board.at(line.square) == line.answer);
        CHECK(before != line.answer);  // a square the moves changed
    }
    CHECK(typedPiece('N') == chess::Piece::WHITEKNIGHT);
    CHECK(typedPiece('q') == chess::Piece::BLACKQUEEN);
    CHECK(typedPiece('-') == chess::Piece::NONE);
    CHECK(typedPiece('0') == chess::Piece::NONE);
    CHECK_FALSE(typedPiece('x'));
}

TEST_CASE("vision: the records kept in the profile") {
    const auto profile = fs::temp_directory_path() / "vision-profile";
    fs::remove_all(profile);
    CHECK(readVisionRecord(profile) == VisionRecord{});
    VisionRecord r;
    r.coordinates["find-white"] = 27;
    r.coordinates["name-black"] = 19;
    r.lines                     = 10;
    r.lines_right               = 7;
    r.blind                     = 2;
    r.where                     = 5;
    r.where_right               = 4;
    REQUIRE(writeVisionRecord(profile, r));
    CHECK(readVisionRecord(profile) == r);
    fs::remove_all(profile);
}

TEST_CASE("vision: records with values of the wrong type keep the readable ones, the rest said (#136)") {
    const test::ScratchDir profile("vision-types");
    VisionRecord r;
    r.coordinates["find-white"] = 27;
    r.lines                     = 10;
    r.lines_right               = 7;
    r.blind                     = 2;
    r.where                     = 5;
    r.where_right               = 4;
    REQUIRE(writeVisionRecord(profile, r));
    const auto file = profile / "vision.json";
    // A text for a count, a list for another (an older or newer version, a hand edit): each used to stop the program
    // as soon as the records were read.
    auto j             = nlohmann::json::parse(test::readFile(file));
    j["lines"]         = "ten";
    j["where_right"]   = nlohmann::json::array({4});
    const auto written = j.dump();
    std::ofstream(file, std::ios::binary) << written;
    const auto before = Console::instance().added();
    VisionRecord back;
    REQUIRE_NOTHROW(back = readVisionRecord(profile));
    // The readable counts kept; the others set aside, said, the file kept as it was.
    CHECK(back.coordinates == r.coordinates);
    CHECK(back.lines == 0);
    CHECK(back.lines_right == 7);
    CHECK(back.blind == 2);
    CHECK(back.where == 5);
    CHECK(back.where_right == 0);
    auto said = Console::instance().since(before);
    REQUIRE(said.size() == 1);
    CHECK(said[0].find("2 entries could not be read and were set aside (lines, where_right)") != std::string::npos);
    CHECK(test::readFile(fs::path(file).concat(".unreadable")) == written);
    // Written again, the records read whole.
    REQUIRE(writeVisionRecord(profile, back));
    const auto quiet = Console::instance().added();
    CHECK(readVisionRecord(profile) == back);
    CHECK(Console::instance().since(quiet).empty());
    // A file cut short: kept aside, said, rather than replaced in silence by the next save.
    std::ofstream(file, std::ios::binary) << "{\"version\": 1, \"coordinates\": {";
    const auto cut = Console::instance().added();
    CHECK(readVisionRecord(profile) == VisionRecord{});
    said = Console::instance().since(cut);
    REQUIRE(said.size() == 1);
    CHECK(said[0].find("could not be read: kept as") != std::string::npos);
    CHECK(fs::exists(fs::path(file).concat(".unreadable2")));
}
