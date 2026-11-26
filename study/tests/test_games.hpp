#pragma once

// Games for the study tests: written as PGN and read as a database takes them, random ones that share their first
// moves (transpositions, repetitions), and databases of them in the tests' own temporary folder.

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <doctest/doctest.hpp>

#include <chess.hpp>
#include <study/import.hpp>
#include <study/store.hpp>

#include "test_support.hpp"

namespace fcg::test {

using namespace fcg::study;
namespace fs = std::filesystem;

// A file of the study's test data, in the sources.
inline fs::path data(const std::string& name) { return sourcePath() / "study" / "tests" / "data" / name; }

struct Spec {
    std::string moves;
    char result   = '*';
    int white_elo = 0;
    int black_elo = 0;
    std::string date{"2020.01.01"};
    std::string fen;
    bool chess960 = false;
    std::string white, black;  // the players, when the test needs them
};

inline ImportedGame make(const Spec& s, std::size_t n) {
    const std::string result = s.result == '1' ? "1-0" : s.result == '0' ? "0-1" : s.result == '=' ? "1/2-1/2" : "*";
    std::string text         = "[Event \"Tree\"]\n[Round \"" + std::to_string(n) + "\"]\n[Date \"" + s.date + "\"]\n";
    if (s.white_elo) text += "[WhiteElo \"" + std::to_string(s.white_elo) + "\"]\n";
    if (s.black_elo) text += "[BlackElo \"" + std::to_string(s.black_elo) + "\"]\n";
    if (!s.white.empty()) text += "[White \"" + s.white + "\"]\n";
    if (!s.black.empty()) text += "[Black \"" + s.black + "\"]\n";
    text += "[Result \"" + result + "\"]\n";
    if (s.chess960) text += "[Variant \"Chess960\"]\n";
    if (!s.fen.empty()) text += "[SetUp \"1\"]\n[FEN \"" + s.fen + "\"]\n";
    text += "\n" + s.moves + " " + result + "\n";
    ImportedGame g;
    REQUIRE(readImportedGame(text, g));
    REQUIRE_FALSE(g.damaged);
    return g;
}

inline std::vector<ImportedGame> make(const std::vector<Spec>& specs) {
    std::vector<ImportedGame> games;
    for (std::size_t i = 0; i < specs.size(); ++i) games.push_back(make(specs[i], i));
    return games;
}

inline std::shared_ptr<Database> database(const std::string& name, std::vector<ImportedGame> games) {
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    auto db = Database::create(dir);
    if (!games.empty()) db->append(std::move(games), true);
    return db;
}

// Random games whose first plies choose among the first three legal moves in UCI order: games share them, cross
// their orders (transpositions) and repeat positions (a knight that goes and comes back).
inline std::vector<ImportedGame> randomGames(std::size_t count, uint32_t seed, int narrow) {
    std::mt19937 random(seed);
    std::vector<ImportedGame> games;
    const char results[] = {'1', '0', '=', '*'};
    for (std::size_t i = 0; i < count; ++i) {
        chess::Board board;
        std::string moves;
        const int length = static_cast<int>(random() % 90);
        for (int ply = 0; ply < length; ++ply) {
            chess::Movelist list;
            chess::movegen::legalmoves(list, board);
            if (list.empty()) break;
            std::vector<chess::Move> sorted(list.begin(), list.end());
            std::sort(sorted.begin(), sorted.end(), [](const chess::Move& a, const chess::Move& b) {
                return chess::uci::moveToUci(a) < chess::uci::moveToUci(b);
            });
            const std::size_t choices = ply < narrow ? std::min<std::size_t>(3, sorted.size()) : sorted.size();
            const auto move           = sorted[random() % choices];
            if (ply % 2 == 0) moves += std::to_string(ply / 2 + 1) + ". ";
            moves += chess::uci::moveToSan(board, move) + " ";
            board.makeMove(move);
        }
        Spec s;
        s.moves     = moves;
        s.result    = results[random() % 4];
        s.white_elo = random() % 4 ? 1800 + static_cast<int>(random() % 1000) : 0;
        s.black_elo = random() % 4 ? 1800 + static_cast<int>(random() % 1000) : 0;
        // Drawn one at a time, the day first: in one expression the operands of + go in an order each compiler
        // chooses (gcc from the right, clang from the left), and the games, which a test holds to bytes written
        // before, must be the same built by either.
        const auto day   = random() % 10;
        const auto month = 1 + random() % 9;
        const auto year  = 1990 + random() % 37;
        s.date = std::to_string(year) + ".0" + std::to_string(month) + ".1" + std::to_string(day);
        games.push_back(make(s, i));
    }
    return games;
}

// Lines of moves made as randomGames makes games: from the start, the first plies among the first three moves in UCI
// order, then any move.
inline std::vector<std::vector<std::string>> randomLines(std::size_t count, uint32_t seed, int narrow, int longest) {
    std::mt19937 random(seed);
    std::vector<std::vector<std::string>> lines;
    for (std::size_t i = 0; i < count; ++i) {
        chess::Board board;
        std::vector<std::string> line;
        const int length = static_cast<int>(random() % static_cast<uint32_t>(longest));
        for (int ply = 0; ply < length; ++ply) {
            chess::Movelist list;
            chess::movegen::legalmoves(list, board);
            if (list.empty()) break;
            std::vector<chess::Move> sorted(list.begin(), list.end());
            std::sort(sorted.begin(), sorted.end(), [](const chess::Move& a, const chess::Move& b) {
                return chess::uci::moveToUci(a) < chess::uci::moveToUci(b);
            });
            const std::size_t choices = ply < narrow ? std::min<std::size_t>(3, sorted.size()) : sorted.size();
            const auto move           = sorted[random() % choices];
            line.push_back(chess::uci::moveToSan(board, move));
            board.makeMove(move);
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

inline std::string movetext(const std::vector<std::string>& line) {
    std::string text;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (i % 2 == 0) text += std::to_string(i / 2 + 1) + ". ";
        text += line[i] + " ";
    }
    return text;
}


}  // namespace fcg::test
