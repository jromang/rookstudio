#include <doctest/doctest.hpp>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <chess.hpp>

#include <study/import.hpp>
#include <study/pgn_reader.hpp>
#include <study/scan_board.hpp>

using namespace fcg::study;
namespace fs = std::filesystem;

namespace {

fs::path data(const std::string& name) {
    const char* env = std::getenv("FCG_SOURCE_DIR");
    return fs::path(env ? env : FCG_SOURCE_DIR) / "study" / "tests" / "data" / name;
}

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
    for (const std::string name : {"games.pgn.zst", "games.pgn.gz", "games.zip", "stored.zip"}) {
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
