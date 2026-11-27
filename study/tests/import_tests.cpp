#include <doctest/doctest.hpp>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include <chess.hpp>

#include <study/import.hpp>
#include <study/pgn_reader.hpp>
#include <study/scan_board.hpp>

#include "test_support.hpp"

using namespace fcg::study;
namespace fs = std::filesystem;

namespace {

fs::path data(const std::string& name) { return fcg::test::sourcePath() / "study" / "tests" / "data" / name; }

struct Imported {
    std::vector<ImportedGame> games;
    ImportReport report;
};

Imported importFile(const fs::path& file, int threads, std::size_t piece = 4096) {
    Imported out;
    ImportProgress progress;
    std::atomic<bool> cancel{false};
    out.report =
        importGames(file, ImportOptions{threads, piece}, progress, cancel, [&](std::vector<ImportedGame>&& batch) {
            for (auto& g : batch) out.games.push_back(std::move(g));
        });
    return out;
}

// What a database keeps of a game, as text: equal for two imports of the same games.
std::string summary(const ImportedGame& g) {
    std::string out;
    for (const auto& [k, v] : g.tags) out += k + "=" + v + ";";
    out += "|" + g.start_fen + "|" + (g.chess960 ? "960" : "") + "|" + std::string(1, g.result) + "|";
    out += std::to_string(g.plies) + "|" + (g.annotated ? "A" : "") + (g.damaged ? "D" : "") + "|";
    for (const auto b : g.moves) out += std::to_string(b) + ",";
    out += "|" + std::to_string(g.clocks.size()) + "|" + std::to_string(g.evals.size()) + "|" + g.text;
    return out;
}

std::vector<std::string> summaries(const std::vector<ImportedGame>& games) {
    std::vector<std::string> out;
    for (const auto& g : games) out.push_back(summary(g));
    return out;
}

// The game's moves, decoded from their codes and played: the final position's key.
uint64_t replayedKey(const ImportedGame& g) {
    ScanBoard board;
    REQUIRE(board.setFen(g.start_fen.empty() ? std::string(chess::constants::STARTPOS) : g.start_fen, g.chess960));
    std::size_t at = 0;
    for (uint32_t ply = 0; ply < g.plies; ++ply) {
        int code = g.moves.at(at++);
        if (board.codeCount() > 256) code = (code << 8) | g.moves.at(at++);
        board.play(board.decode(code));
    }
    CHECK(at == g.moves.size());
    return board.key();
}

// A text with its lines ended by "\n", as a game's text is kept.
std::string unixLines(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\r')
            out += text[i];
        else if (i + 1 == text.size() || text[i + 1] != '\n')
            out += '\n';
    }
    return out;
}

}  // namespace

TEST_CASE("import: a game's tags, moves, clocks and evaluations, and what makes its text kept") {
    std::ifstream in(data("games.pgn"), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto games = splitGames(text);
    REQUIRE(games.size() == 120);

    ImportedGame lichess;
    REQUIRE(readImportedGame(games[0], lichess));
    CHECK(lichess.plies == 9);
    CHECK(lichess.result == '1');
    CHECK(!lichess.annotated);  // only clocks and evaluations: kept as such, not as text
    CHECK(lichess.text.empty());
    REQUIRE(lichess.clocks.size() == 9);
    CHECK(lichess.clocks[0] == 180000);
    CHECK(lichess.clocks[5] == 175000);
    REQUIRE(lichess.evals.size() == 9);
    CHECK(lichess.evals[0] == 17);
    CHECK(lichess.evals[1] == 19);  // White's point of view, as written
    CHECK(lichess.evals[6] == -250);
    CHECK(lichess.evals[8] == -29997);
    // The codes give back the moves: the same final position as chess.hpp's reading of the game.
    const auto read = readPgnGame(games[0]);
    REQUIRE(read.tree);
    chess::Board board;
    for (const auto& m : read.tree->main.moves) board.makeMove<false>(m.move);
    CHECK(replayedKey(lichess) == board.hash());

    ImportedGame annotated;
    REQUIRE(readImportedGame(games[1], annotated));
    CHECK(annotated.annotated);
    CHECK(annotated.text == unixLines(games[1]));
    CHECK(annotated.plies == 11);

    ImportedGame tournament;
    REQUIRE(readImportedGame(games[2], tournament));
    CHECK(tournament.result == '=');
    CHECK(tournament.annotated);  // the engines' comments

    ImportedGame c960;
    REQUIRE(readImportedGame(games[3], c960));
    CHECK(c960.chess960);
    CHECK(c960.plies == 4);
    CHECK(!c960.damaged);

    ImportedGame broken;
    REQUIRE(readImportedGame(games[4], broken));
    CHECK(broken.damaged);
    CHECK(broken.plies == 2);
    CHECK(broken.text == unixLines(games[4]));

    CHECK(damageOf(broken).move == "Ke3");  // not a king's move from e1
    CHECK(!damageOf(broken).bad_start);

    ImportedGame anonymous;
    REQUIRE(readImportedGame(games[5], anonymous));
    CHECK(anonymous.plies == 4);
    CHECK(anonymous.result == '0');

    ImportedGame none;
    CHECK(!readImportedGame("  \n", none));
}

TEST_CASE("import: the same games whatever the workers, the pieces and the compression") {
    const auto plain = importFile(data("games.pgn"), 1);
    CHECK(plain.report.error.empty());
    REQUIRE(plain.games.size() == 120);
    CHECK(plain.report.damaged == 20);
    const auto expected = summaries(plain.games);
    for (const int threads : {2, 8}) {
        for (const std::size_t piece : {std::size_t{4096}, std::size_t{100000}}) {
            CAPTURE(threads);
            CAPTURE(piece);
            CHECK(summaries(importFile(data("games.pgn"), threads, piece).games) == expected);
        }
    }
    // games-frames.pgn.zst: two frames, each after a skippable frame giving its size, as pzstd writes them (and
    // Lichess's monthly files are).
    for (const std::string name :
         {"games.pgn.zst", "games-frames.pgn.zst", "games.pgn.gz", "games.zip", "stored.zip"}) {
        CAPTURE(name);
        const auto other = importFile(data(name), 4);
        CHECK(other.report.error.empty());
        CHECK(summaries(other.games) == expected);
    }
    // The same file with Windows line ends ("\r\n").
    std::ifstream in(data("games.pgn"), std::ios::binary);
    const std::string unix_text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string windows_text;
    for (const char c : unix_text) {
        if (c == '\r') continue;  // a checkout that has them already
        if (c == '\n') windows_text += '\r';
        windows_text += c;
    }
    const auto crlf = fs::temp_directory_path() / "games-crlf.pgn";
    std::ofstream(crlf, std::ios::binary) << windows_text;
    CHECK(summaries(importFile(crlf, 4).games) == expected);
}

TEST_CASE("import: cancelled between batches, and a file that cannot be read") {
    ImportProgress progress;
    std::atomic<bool> cancel{false};
    std::size_t got   = 0;
    const auto report = importGames(data("games.pgn"), ImportOptions{2, 4096}, progress, cancel,
                                    [&](std::vector<ImportedGame>&& batch) {
                                        got += batch.size();
                                        cancel = true;
                                    });
    CHECK(report.cancelled);
    CHECK(got > 0);
    CHECK(got < 120);

    std::atomic<bool> go{false};
    ImportProgress none;
    const auto missing =
        importGames(fs::temp_directory_path() / "no such file.pgn", {}, none, go, [](std::vector<ImportedGame>&&) {});
    CHECK(!missing.error.empty());

    // A compressed file cut short: what was read before is given, and the problem said.
    std::ifstream in(data("games.pgn.zst"), std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto cut = fs::temp_directory_path() / "cut.pgn.zst";
    std::ofstream(cut, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size() / 2));
    const auto damaged = importFile(cut, 2);
    CHECK(!damaged.report.error.empty());
}

TEST_CASE("import: what the caller's side throws reaches it once the reading threads have stopped (#112)") {
    // The batches go to the caller's sink on its thread: what the sink throws stops the workers and the reader, waited
    // for, before it goes on (a thread left running ended the program).
    ImportProgress progress;
    std::atomic<bool> cancel{false};
    int batches = 0;
    CHECK_THROWS_WITH_AS(importGames(data("games.pgn"), ImportOptions{4, 4096}, progress, cancel,
                                     [&](std::vector<ImportedGame>&&) {
                                         ++batches;
                                         throw std::runtime_error("no room for them");
                                     }),
                         "no room for them", std::runtime_error);
    CHECK(batches == 1);
}

TEST_CASE("import: a file whose bytes are not text is refused, before it makes junk games") {
    // An enciphered archive given as a PGN: DES's output, bytes of every value.
    std::ifstream in(data("chessbase/cbtest2.2cbz"), std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(bytes.size() > 4096);
    const auto file = fs::temp_directory_path() / "binary.pgn";
    std::ofstream(file, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    ImportProgress progress;
    std::atomic<bool> go{false};
    std::size_t given = 0;
    const auto report = importGames(file, {}, progress, go, [&](std::vector<ImportedGame>&& b) { given += b.size(); });
    CHECK(report.error == "binary.pgn is not a PGN file: its bytes are not text");
    CHECK(report.games == 0);
    CHECK(given == 0);
    // Text, as PGN files are: tabs, Windows line ends, a form feed, the end-of-file mark of old files, Latin-1, UTF-8.
    CHECK(looksLikeText("[Event \"x\"]\r\n\t1. e4 e5\f\x1a"));
    CHECK(looksLikeText("[White \"Dvo\xc5\x99\xc3\xa1k\"] [Black \"M\xfcller\"]"));
    CHECK(looksLikeText(""));
    CHECK_FALSE(looksLikeText(std::string("[Event \"x\"]\0", 12)));
    CHECK_FALSE(looksLikeText(bytes.substr(0, 4096)));
    // A rare control byte in a comment stays text.
    CHECK(looksLikeText("1. e4 {a \x01 comment} e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O Be7 1-0"));
}

TEST_CASE("import: the line each game starts on, the same in pieces, with Windows line ends and in archives") {
    // The line of a game is that of its first tag: readLines gives it back, from the file as from its copies.
    const auto starts_where = [](const fs::path& file, const std::vector<ImportedGame>& games) {
        std::atomic<bool> go{false};
        uint64_t before = 0;
        for (const auto& g : games) {
            REQUIRE(g.line > before);  // in the file's order
            before = g.line;
            if (g.tags.empty()) continue;
            const auto lines = readLines(file, g.line, 1, go);
            REQUIRE(lines.size() == 1);
            CHECK(lines[0].starts_with("[" + g.tags.front().first + " "));
        }
    };
    const auto lines_of = [](const std::vector<ImportedGame>& games) {
        std::vector<uint64_t> lines;
        for (const auto& g : games) lines.push_back(g.line);
        return lines;
    };
    const auto whole = importFile(data("games.pgn"), 1, std::size_t{1} << 20);
    REQUIRE(whole.games.size() == 120);
    starts_where(data("games.pgn"), whole.games);
    const auto expected = lines_of(whole.games);
    // In pieces of 4 KB read by three workers, compressed: the same lines.
    CHECK(lines_of(importFile(data("games.pgn"), 3).games) == expected);
    CHECK(lines_of(importFile(data("games.pgn.zst"), 3).games) == expected);
    CHECK(lines_of(importFile(data("games.pgn.gz"), 3).games) == expected);
    // With "\r\n": the same lines too, readLines leaving the "\r" out.
    std::ifstream in(data("games.pgn"), std::ios::binary);
    const std::string unix_text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string windows_text;
    for (const char c : unix_text) {
        if (c == '\r') continue;
        if (c == '\n') windows_text += '\r';
        windows_text += c;
    }
    const auto crlf = fs::temp_directory_path() / "lines-crlf.pgn";
    std::ofstream(crlf, std::ios::binary) << windows_text;
    const auto windows = importFile(crlf, 3);
    CHECK(lines_of(windows.games) == expected);
    starts_where(crlf, windows.games);
    // An archive's members one after the other, for importGames as for readLines.
    const auto zip = importFile(data("games.zip"), 3);
    REQUIRE(!zip.games.empty());
    starts_where(data("games.zip"), zip.games);
}

TEST_CASE("import: the lines of a file, from a line on") {
    const auto file = fs::temp_directory_path() / "lines.pgn";
    const std::string longest(5000, 'x');
    std::ofstream(file, std::ios::binary) << "a\r\nb\n\nd\n" << longest << "\nlast";
    std::atomic<bool> go{false};
    CHECK(readLines(file, 1, 2, go) == std::vector<std::string>{"a", "b"});
    CHECK(readLines(file, 3, 2, go) == std::vector<std::string>{"", "d"});
    const auto tail = readLines(file, 5, 10, go);  // fewer than asked: the file ends
    REQUIRE(tail.size() == 2);
    CHECK(tail[0] == std::string(4096, 'x') + "…");  // cut after 4 KB
    CHECK(tail[1] == "last");                        // no line end after it
    CHECK(readLines(file, 7, 1, go).empty());
    CHECK(readLines(file, 1, 0, go).empty());
    std::atomic<bool> stop{true};
    CHECK(readLines(file, 1, 3, stop).empty());
    CHECK_THROWS((void)readLines(fs::temp_directory_path() / "no such file.pgn", 1, 1, go));
}

TEST_CASE("import: why a damaged game's moves stop") {
    ImportedGame cut;
    REQUIRE(readImportedGame("[Event \"Cut\"]\n\n1. e4 {fine} (1. d4 d5) e5 2. Nf3 Nc6 3. Bb", cut));
    CHECK(cut.damaged);
    CHECK(cut.plies == 4);
    CHECK(damageOf(cut).move == "Bb");  // a move cut by the end of the file
    CHECK(damageOf(cut).cut);

    ImportedGame glyph;
    REQUIRE(readImportedGame("[Event \"Glyph\"]\n\n1. e4 e5 2. Qxf7?! *", glyph));
    CHECK(glyph.damaged);
    CHECK(damageOf(glyph).move == "Qxf7");
    CHECK(!damageOf(glyph).cut);  // the result follows it

    ImportedGame position;
    REQUIRE(readImportedGame("[Event \"FEN\"]\n[SetUp \"1\"]\n[FEN \"8/8/8 w - - 0 1\"]\n\n1. e4 *", position));
    CHECK(position.damaged);
    CHECK(position.plies == 0);
    CHECK(damageOf(position).bad_start);
    CHECK(damageOf(position).move.empty());
}

TEST_CASE(
    "import: a zip archive whose directory is said larger than the archive is refused, nothing made of its size") {
    // A damaged or crafted end record: the directory's size and start read there were a buffer's size, up to 2 GB
    // for an archive of a few kilobytes (#102).
    std::ifstream in(data("games.zip"), std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto end = bytes.rfind(std::string("PK\x05\x06", 4));
    REQUIRE(end != std::string::npos);
    for (const auto& [at, value] : std::vector<std::pair<std::size_t, uint32_t>>{
             {end + 12, 0x7FFFFFFF}, {end + 12, static_cast<uint32_t>(bytes.size()) + 1}, {end + 16, 0xFFFFFFF0}}) {
        CAPTURE(at - end);
        CAPTURE(value);
        auto crafted = bytes;
        std::memcpy(crafted.data() + at, &value, 4);
        const auto file = fs::temp_directory_path() / "crafted.zip";
        std::ofstream(file, std::ios::binary | std::ios::trunc)
            .write(crafted.data(), static_cast<std::streamsize>(crafted.size()));
        const auto read = importFile(file, 1);
        CHECK(read.games.empty());
        CHECK(read.report.error.find("damaged zip directory") != std::string::npos);
    }
}
