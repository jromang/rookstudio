// Theory studies (#77): PGN files read where they are and read again when they change on the disk, a study removed
// and put back, the repertoires' comments under their names, a Lichess study's PGN as the profile keeps it, what the
// profile keeps. The games are our own analyses, written for these tests.
#include <doctest/doctest.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <chess.hpp>
#include <study/theory_notes.hpp>
#include <tour/files.hpp>

#include "lichess_studies.hpp"
#include "repertoires.hpp"
#include "theory_shelf.hpp"

namespace fs = std::filesystem;
using namespace fcg;

namespace {

const std::vector<std::string> NAJDORF      = {"e4",  "c5",  "Nf3", "d6",  "d4", "cxd4", "Nxd4",
                                               "Nf6", "Nc3", "a6",  "Bg5", "e6", "f4"};
const std::vector<std::string> SCHEVENINGEN = {"e4",  "c5",  "Nf3", "e6",  "d4", "cxd4", "Nxd4",
                                               "Nf6", "Nc3", "d6",  "Bg5", "a6", "f4"};

uint64_t keyAfter(const std::vector<std::string>& line) {
    chess::Board board;
    for (const auto& san : line) board.makeMove<true>(chess::uci::parseSan(board, san));
    return study::theoryKey(board);
}

void write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

fs::path fresh(const std::string& name) {
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

constexpr const char* NOTES = R"([Event "My Najdorf"]

1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 6. Bg5 e6 7. f4 {The main position.} 7... Qb6 $1 {The Poisoned
Pawn.} ( 7... Be7 {The main line.} ) ( 7... h6 $6 ) 8. Qd2 *
)";

// The same position by the Scheveningen's order, commented in another file.
constexpr const char* PLANS = R"([Event "Plans"]

1. e4 c5 2. Nf3 e6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 d6 6. Bg5 a6 7. f4 {White aims at e5 and castles long.
[%cal Ge1c1]} *
)";

}  // namespace

TEST_CASE("theory: a PGN read where it is, its notes at a position, a transposition said as such") {
    const auto dir     = fresh("theory-read");
    const auto profile = dir / "profile";
    write(dir / "najdorf.pgn", NOTES);
    write(dir / "plans.pgn", PLANS);
    TheoryShelf shelf(profile);
    bool added = false;
    CHECK(shelf.addFile(dir / "najdorf.pgn", &added) == 0);
    CHECK(added);
    CHECK(shelf.addFile(dir / "plans.pgn") == 1);
    CHECK(shelf.addFile(dir / "najdorf.pgn", &added) == 0);  // already there
    CHECK_FALSE(added);
    shelf.wait();
    REQUIRE(shelf.studies().size() == 2);
    CHECK(shelf.studies()[0].name == "najdorf");
    REQUIRE(shelf.studies()[0].notes);
    CHECK(shelf.studies()[0].notes->counts().notes == 4);
    CHECK(shelf.anyNotes());

    // At the position after 7.f4 by the Najdorf's order.
    const auto at = shelf.at(keyAfter(NAJDORF), &NAJDORF);
    REQUIRE(at.found.size() == 5);
    // The list's order; in a file, about the position first, then its moves.
    CHECK(at.found[0].source == "najdorf");
    CHECK_FALSE(at.found[0].on_move);
    CHECK(at.found[0].note->after == "The main position.");
    CHECK_FALSE(at.found[0].transposed);
    CHECK(at.found[1].on_move);
    CHECK(at.found[1].note->san == "Qb6");
    CHECK(at.found[2].note->san == "Be7");
    CHECK(at.found[3].note->san == "h6");
    CHECK(at.found[4].source == "plans");
    CHECK(at.found[4].transposed);  // reached there by 2...e6, 5...d6, 6...a6
    CHECK(at.found[4].note->arrows.size() == 1);
    // The moves the notes are on: their symbols, and the notes with more than symbols.
    REQUIRE(at.moves.size() == 3);
    CHECK(at.moves.at("Qb6").symbols == std::vector<uint8_t>{1});
    CHECK(at.moves.at("Qb6").notes == 1);
    CHECK(at.moves.at("Be7").notes == 1);
    CHECK(at.moves.at("h6").symbols == std::vector<uint8_t>{6});
    CHECK(at.moves.at("h6").notes == 0);

    // By the Scheveningen's order, the first file's notes are the transposition.
    const auto other = shelf.at(keyAfter(SCHEVENINGEN), &SCHEVENINGEN);
    REQUIRE(other.found.size() == 5);
    CHECK(other.found[0].transposed);
    CHECK(other.found[1].transposed);
    CHECK_FALSE(other.found[4].transposed);
    // A board that does not start at the standard start says nothing of the move order.
    CHECK_FALSE(shelf.at(keyAfter(NAJDORF), nullptr).found[4].transposed);

    // Hidden, a study says nothing; shown again, it does.
    shelf.setShown(1, false);
    CHECK(shelf.at(keyAfter(NAJDORF), &NAJDORF).found.size() == 4);
    shelf.setShown(1, true);
    CHECK(shelf.at(keyAfter(NAJDORF), &NAJDORF).found.size() == 5);
}

TEST_CASE("theory: a file changed on the disk is read again by itself; RookStudio never writes into it") {
    const auto dir  = fresh("theory-changed");
    const auto file = dir / "lines.pgn";
    write(file, NOTES);
    TheoryShelf shelf(dir / "profile");
    shelf.addFile(file);
    shelf.wait();
    const auto before = fs::last_write_time(file);
    REQUIRE(shelf.studies()[0].notes);
    CHECK(shelf.studies()[0].readings == 1);
    CHECK(shelf.at(keyAfter(NAJDORF), &NAJDORF).found.size() == 4);
    // Nothing changed: looked at, not read again.
    auto now = std::chrono::steady_clock::now();
    CHECK_FALSE(shelf.poll(now, std::chrono::milliseconds(0)));
    CHECK_FALSE(shelf.reading());

    // Its author writes more into it, with another program.
    write(file, std::string(NOTES) + "\n" + PLANS);
    fs::last_write_time(file, before + std::chrono::seconds(2));  // whatever the file system's clock
    const auto generation = shelf.generation();
    shelf.poll(now + std::chrono::seconds(2), std::chrono::milliseconds(1000));
    CHECK(shelf.reading());
    shelf.wait();
    CHECK(shelf.generation() > generation);
    CHECK(shelf.studies()[0].readings == 2);
    CHECK(shelf.studies()[0].notes->counts().games == 2);
    CHECK(shelf.at(keyAfter(NAJDORF), &NAJDORF).found.size() == 5);
    // The file as its author left it, the stream closed after: Windows renames no file a stream holds open.
    {
        std::ifstream in(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(text == std::string(NOTES) + "\n" + PLANS);
    }

    // Moved away: missing, its notes of the last reading kept; back again, read again.
    fs::rename(file, dir / "elsewhere.pgn");
    shelf.poll(now + std::chrono::seconds(4), std::chrono::milliseconds(1000));
    CHECK(shelf.studies()[0].missing);
    shelf.relocate(0, dir / "elsewhere.pgn");
    shelf.wait();
    CHECK_FALSE(shelf.studies()[0].missing);
    CHECK(shelf.studies()[0].name == "elsewhere");
    CHECK(shelf.at(keyAfter(NAJDORF), &NAJDORF).found.size() == 5);
}

TEST_CASE("theory: a study removed from the theory, its file left where it is; put back; what the profile keeps") {
    const auto dir     = fresh("theory-removed");
    const auto profile = dir / "profile";
    write(dir / "najdorf.pgn", NOTES);
    write(dir / "plans.pgn", PLANS);
    {
        TheoryShelf shelf(profile);
        shelf.addFile(dir / "najdorf.pgn");
        shelf.addFile(dir / "plans.pgn");
        shelf.setShown(1, false);
        shelf.wait();
        const auto removed = shelf.remove(0);
        REQUIRE(removed);
        CHECK(removed->name == "najdorf");
        CHECK(fs::exists(dir / "najdorf.pgn"));
        REQUIRE(shelf.studies().size() == 1);
        CHECK(shelf.at(keyAfter(NAJDORF), &NAJDORF).found.empty());  // the one left is hidden
        // Undone: at its place again.
        CHECK(shelf.restore(*removed) == 0);
        shelf.wait();
        CHECK(shelf.studies()[0].name == "najdorf");
        CHECK(shelf.at(keyAfter(NAJDORF), &NAJDORF).found.size() == 4);
        shelf.remove(0);
        shelf.showRepertoires(false);
    }
    // Read again from the profile: the study left, hidden, the repertoires' comments hidden.
    TheoryShelf again(profile);
    REQUIRE(again.studies().size() == 1);
    CHECK(again.studies()[0].name == "plans");
    CHECK_FALSE(again.studies()[0].shown);
    CHECK_FALSE(again.repertoiresShown());
    again.poll();
    again.wait();
    REQUIRE(again.studies()[0].notes);
    CHECK(again.studies()[0].notes->counts().notes == 1);
    // A run that leaves the profile as it was writes nothing.
    {
        TheoryShelf scripted(profile, false);
        scripted.addFile(dir / "najdorf.pgn");
        scripted.wait();
    }
    CHECK(TheoryShelf(profile).studies().size() == 1);
}

TEST_CASE("theory: the repertoires' comments show the same way, under their names") {
    const auto dir     = fresh("theory-repertoires");
    const auto profile = dir / "profile";
    write(profile / "repertoires" / "Najdorf.pgn",
          "[Event \"Repertoire\"]\n[RepertoireSide \"Black\"]\n\n1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 "
          "6. Bg5 e6 7. f4 Qb6 {My choice: the Poisoned Pawn.} *\n");
    RepertoireShelf repertoires(profile);
    REQUIRE(repertoires.entries().size() == 1);
    TheoryShelf shelf(profile);
    std::vector<std::pair<std::string, fs::path>> files;
    for (const auto& e : repertoires.entries()) files.emplace_back(e.name, e.file);
    shelf.syncRepertoires(files);
    shelf.poll();
    shelf.wait();
    const auto at = shelf.at(keyAfter(NAJDORF), &NAJDORF);
    REQUIRE(at.found.size() == 1);
    CHECK(at.found[0].kind == TheorySource::Kind::Repertoire);
    CHECK(at.found[0].source == "Najdorf");
    CHECK(at.found[0].on_move);
    CHECK(at.found[0].note->after == "My choice: the Poisoned Pawn.");
    CHECK(at.moves.at("Qb6").notes == 1);

    // A star of the Tree tab writes the repertoire: read again.
    chess::Board board;
    std::vector<chess::Move> path;
    for (const auto& san :
         {"e4", "c5", "Nf3", "d6", "d4", "cxd4", "Nxd4", "Nf6", "Nc3", "a6", "Bg5", "e6", "f4", "Qb6"}) {
        path.push_back(chess::uci::parseSan(board, san));
        board.makeMove<false>(path.back());
    }
    const auto stamp = fs::last_write_time(repertoires.entries()[0].file);
    CHECK(repertoires.toggle(0, path, chess::uci::parseSan(board, "Qd2")));
    fs::last_write_time(repertoires.entries()[0].file, stamp + std::chrono::seconds(2));
    shelf.poll(std::chrono::steady_clock::now() + std::chrono::hours(1));
    shelf.wait();
    CHECK(shelf.repertoires()[0].readings == 2);
    CHECK(shelf.at(keyAfter(NAJDORF), &NAJDORF).found.size() == 1);  // its comment kept

    // Hidden, and the repertoire gone: nothing.
    shelf.showRepertoires(false);
    CHECK(shelf.at(keyAfter(NAJDORF), &NAJDORF).found.empty());
    shelf.showRepertoires(true);
    shelf.syncRepertoires({});
    CHECK(shelf.repertoires().empty());
    CHECK(shelf.at(keyAfter(NAJDORF), &NAJDORF).found.empty());
}

TEST_CASE("theory: a Lichess study's PGN as the profile keeps it, read again with the study") {
    const auto dir     = fresh("theory-lichess");
    const auto profile = dir / "profile";
    const auto cache   = lichess::studyCache(profile, "abcd1234");
    // As Lichess exports a study, a chapter a game.
    const std::string chapter1 =
        "[Event \"Openings: Plans\"]\n[Result \"*\"]\n[StudyName \"Openings\"]\n[ChapterName \"Plans\"]\n"
        "[ChapterURL \"https://lichess.org/study/abcd1234/Chap0001\"]\n[Orientation \"black\"]\n\n"
        "1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 6. Bg5 e6 7. f4 { [%csl Ge5][%cal Ge1c1] White aims at "
        "e5. } 7... Nbd7 { [%clk 0:03:00] } *\n\n\n";
    const std::string chapter2 =
        "[Event \"Openings: Sidelines\"]\n[Result \"*\"]\n[StudyName \"Openings\"]\n[ChapterName \"Sidelines\"]\n"
        "[ChapterURL \"https://lichess.org/study/abcd1234/Chap0002\"]\n\n"
        "1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 6. Bg5 e6 7. f4 h6 { [%eval 0.6] Dubious. } *\n\n\n";
    write(cache, chapter1);
    TheoryShelf shelf(profile);
    bool added = false;
    CHECK(shelf.addLichess("abcd1234", "Openings", &added) == 0);
    CHECK(added);
    CHECK(shelf.findLichess("abcd1234") == std::optional<std::size_t>(0));
    shelf.wait();
    REQUIRE(shelf.studies()[0].notes);
    CHECK(shelf.studies()[0].kind == TheorySource::Kind::Lichess);
    auto at = shelf.at(keyAfter(NAJDORF), &NAJDORF);
    REQUIRE(at.found.size() == 1);
    CHECK(at.found[0].source == "Openings");
    CHECK(at.found[0].game == "Plans");
    CHECK(at.found[0].lichess == "abcd1234");
    CHECK(at.found[0].note->squares.size() == 1);
    CHECK(at.found[0].note->arrows.size() == 1);

    // Read again on the Lichess page (#79), a chapter more: the profile's copy replaced, the notes follow.
    const auto stamp = fs::last_write_time(cache);
    tour::replaceFile(cache, chapter1 + chapter2);
    fs::last_write_time(cache, stamp + std::chrono::seconds(2));
    shelf.poll(std::chrono::steady_clock::now() + std::chrono::hours(1));
    shelf.wait();
    at = shelf.at(keyAfter(NAJDORF), &NAJDORF);
    REQUIRE(at.found.size() == 2);
    CHECK(at.found[1].game == "Sidelines");
    CHECK(at.found[1].on_move);
    REQUIRE(at.found[1].note->eval);
    CHECK(at.found[1].note->eval->value == 60);
    CHECK(at.found[1].games == 2);

    // Kept by its id: the profile's copy found again.
    {
        TheoryShelf again(profile);
        REQUIRE(again.studies().size() == 1);
        CHECK(again.studies()[0].lichess == "abcd1234");
        CHECK(again.studies()[0].name == "Openings");
        CHECK(again.studies()[0].file == cache);
    }
    // Not read yet on this machine: missing until it is.
    TheoryShelf other(dir / "other");
    other.addLichess("zzzz9999", "Another");
    other.wait();
    CHECK(other.studies()[0].missing);
    CHECK_FALSE(other.studies()[0].notes);
}
