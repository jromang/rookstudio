#include <doctest/doctest.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include <chess.hpp>

#include "repertoires.hpp"

namespace fs = std::filesystem;
using namespace fcg;

namespace {

std::vector<chess::Move> moves(const std::vector<std::string>& line) {
    chess::Board board;
    std::vector<chess::Move> out;
    for (const auto& san : line) {
        out.push_back(chess::uci::parseSan(board, san));
        board.makeMove<false>(out.back());
    }
    return out;
}

chess::Move next(const std::vector<std::string>& line, const std::string& san) {
    chess::Board board;
    for (const auto& s : line) board.makeMove<false>(chess::uci::parseSan(board, s));
    return chess::uci::parseSan(board, san);
}

}  // namespace

TEST_CASE("repertoires: made, starred moves added and taken out, kept in their PGN files") {
    const auto profile = fs::temp_directory_path() / "repertoires-profile";
    fs::remove_all(profile);
    fs::create_directories(profile);
    {
        RepertoireShelf shelf(profile);
        CHECK(shelf.entries().empty());
        CHECK(shelf.active() == -1);
        const auto white = shelf.activeOrNew(chess::Color::WHITE);
        CHECK(shelf.entries()[white].name == "White");
        CHECK(shelf.active() == static_cast<int>(white));
        CHECK(shelf.toggle(white, {}, next({}, "e4")));
        CHECK(shelf.toggle(white, moves({"e4", "c5"}), next({"e4", "c5"}, "Nf3")));
        CHECK(shelf.has(white, moves({"e4", "c5"}), next({"e4", "c5"}, "Nf3")));
        CHECK_FALSE(shelf.has(white, moves({"e4", "c5"}), next({"e4", "c5"}, "Nc3")));
        const auto black = shelf.create("Najdorf", chess::Color::BLACK);
        CHECK(shelf.active() == static_cast<int>(black));
        CHECK(shelf.entries()[black].side == chess::Color::BLACK);
        shelf.setActive(static_cast<int>(white));
    }
    {
        RepertoireShelf shelf(profile);
        REQUIRE(shelf.entries().size() == 2);
        REQUIRE(shelf.active() >= 0);
        const auto white = static_cast<std::size_t>(shelf.active());
        CHECK(shelf.entries()[white].name == "White");
        CHECK(shelf.entries()[1 - white].side == chess::Color::BLACK);
        CHECK(shelf.has(white, {}, next({}, "e4")));
        CHECK(shelf.has(white, moves({"e4", "c5"}), next({"e4", "c5"}, "Nf3")));
        // Taken out: the move and what follows it.
        CHECK_FALSE(shelf.toggle(white, {}, next({}, "e4")));
        CHECK_FALSE(shelf.has(white, {}, next({}, "e4")));
        CHECK_FALSE(shelf.has(white, moves({"e4", "c5"}), next({"e4", "c5"}, "Nf3")));
    }
}

TEST_CASE("repertoires: a move starred at a position its lines reach by another order") {
    const auto profile = fs::temp_directory_path() / "repertoires-transposed";
    fs::remove_all(profile);
    RepertoireShelf shelf(profile);
    const auto white = shelf.activeOrNew(chess::Color::WHITE);
    // 1. d4 Nf6 2. c4 e6, a move at a time.
    const std::vector<std::pair<std::vector<std::string>, std::string>> steps = {
        {{}, "d4"}, {{"d4"}, "Nf6"}, {{"d4", "Nf6"}, "c4"}, {{"d4", "Nf6", "c4"}, "e6"}};
    for (const auto& [line, san] : steps) CHECK(shelf.toggle(white, moves(line), next(line, san)));
    // Reached by 1. c4 Nf6 2. d4 e6, the position takes the move where the lines have it already.
    CHECK(shelf.toggle(white, moves({"c4", "Nf6", "d4", "e6"}), next({"c4", "Nf6", "d4", "e6"}, "Nf3")));
    CHECK(shelf.has(white, moves({"d4", "Nf6", "c4", "e6"}), next({"d4", "Nf6", "c4", "e6"}, "Nf3")));
    CHECK(shelf.entries()[white].lines.variations.empty());
    // Taken out from the other order too.
    CHECK_FALSE(shelf.toggle(white, moves({"c4", "Nf6", "d4", "e6"}), next({"c4", "Nf6", "d4", "e6"}, "Nf3")));
    CHECK_FALSE(shelf.has(white, moves({"d4", "Nf6", "c4", "e6"}), next({"d4", "Nf6", "c4", "e6"}, "Nf3")));
}
