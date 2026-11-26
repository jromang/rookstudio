// A month of Lichess's games (#72): kept by their headers, read from a zstd file into a database, a reading cut short.
#include <doctest/doctest.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <fmt/include/fmt/format.h>
#include <study/store.hpp>

#include "lichess_month.hpp"
#include "test_support.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

// A zstd frame of raw blocks, as the format allows: what a test writes without a compressor.
std::string rawFrame(std::string_view text) {
    std::string out;
    const auto put32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) out += static_cast<char>((v >> (8 * i)) & 0xFF);
    };
    put32(0xFD2FB528);
    out += static_cast<char>(0xA0);
    put32(static_cast<uint32_t>(text.size()));
    constexpr std::size_t BLOCK = 1 << 17;
    for (std::size_t at = 0;;) {
        const auto n      = std::min(BLOCK, text.size() - at);
        const bool last   = at + n >= text.size();
        const auto header = (last ? 1u : 0u) | (static_cast<uint32_t>(n) << 3);
        out += static_cast<char>(header & 0xFF);
        out += static_cast<char>((header >> 8) & 0xFF);
        out += static_cast<char>((header >> 16) & 0xFF);
        out.append(text.substr(at, n));
        at += n;
        if (last) return out;
    }
}

// Games as Lichess writes them: every third with a player under 2400, every fourth a bullet game.
std::string month(int games, int& kept) {
    std::string out;
    kept = 0;
    for (int i = 0; i < games; ++i) {
        const int white = i % 3 == 0 ? 2300 : 2450 + i % 50, black = 2410 + i % 30;
        const char* tc = i % 4 == 0 ? "60+0" : "180+2";
        out += fmt::format(
            "[Event \"Rated game\"]\n[Site \"https://lichess.org/g{:06}\"]\n[Date \"2026.10.{:02}\"]\n[Round \"-\"]\n"
            "[White \"w{}\"]\n[Black \"b{}\"]\n[Result \"1-0\"]\n[WhiteElo \"{}\"]\n[BlackElo \"{}\"]\n"
            "[TimeControl \"{}\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bb5 {} 1-0\n\n",
            i, 1 + i % 28, i, i, white, black, tc, i % 2 ? "a6" : "Nf6");
        if (i % 3 != 0 && i % 4 != 0) ++kept;
    }
    return out;
}

bool waitIdle(const lichess::MonthReading& reading) {
    return test::waitUntil([&] { return !reading.busy(); }, std::chrono::seconds(60));
}

}  // namespace

TEST_CASE("Lichess month: a game kept by its players' ratings and its time control") {
    const lichess::MonthFilter f;
    const auto game = [](int w, int b, const char* tc) {
        return fmt::format("[Event \"x\"]\n[WhiteElo \"{}\"]\n[BlackElo \"{}\"]\n[TimeControl \"{}\"]\n\n1. e4 *\n", w,
                           b, tc);
    };
    CHECK(lichess::keepGame(game(2400, 2401, "180+0"), f));
    CHECK_FALSE(lichess::keepGame(game(2399, 2500, "180+0"), f));
    CHECK_FALSE(lichess::keepGame(game(2500, 2500, "60+1"), f));      // 100 s: bullet
    CHECK_FALSE(lichess::keepGame(game(2500, 2500, "120+1"), f));     // 160 s: bullet
    CHECK(lichess::keepGame(game(2500, 2500, "180+0"), f));           // 180 s: blitz
    CHECK(lichess::keepGame(game(2500, 2500, "-"), f));               // by correspondence
    CHECK_FALSE(lichess::keepGame("[Event \"x\"]\n\n1. e4 *\n", f));  // no rating
    CHECK(lichess::keepGame(game(1500, 1500, "30+0"), lichess::MonthFilter{0, true}));
}

TEST_CASE("Lichess month: its games read from a file, those kept added; cut short, then read again whole") {
    const auto dir = fs::temp_directory_path() / "lichess-month";
    fs::remove_all(dir);
    fs::create_directories(dir);
    int kept       = 0;
    const auto pgn = month(300, kept);
    // Three frames cut anywhere in the text: in a game's headers, in its moves.
    const auto a = pgn.size() / 3, b = 2 * pgn.size() / 3 + 7;
    const auto whole = rawFrame(pgn.substr(0, a)) + rawFrame(pgn.substr(a, b - a)) + rawFrame(pgn.substr(b));
    const auto file  = dir / "month.pgn.zst";
    auto db          = study::Database::create(dir / "Month.fcgdb");
    JobGroup jobs;
    lichess::MonthReading reading(jobs);
    ZstdReadingOptions options;
    options.checkpoint_every = 0;
    reading.setOptions(options);
    // Cut short in its last frame: said, where it stopped kept, the games up to there committed.
    std::ofstream(file, std::ios::binary | std::ios::trunc) << whole.substr(0, whole.size() - 100);
    reading.start(file.string(), db, {}, dir / "reading.json");
    REQUIRE(waitIdle(reading));
    const auto why = reading.finished();
    REQUIRE(why);
    CHECK(why->find("cut short") != std::string::npos);
    CHECK(fs::exists(dir / "reading.json"));
    const auto first = db->snapshot()->games();
    CHECK(first > 0);
    CHECK(first < static_cast<uint64_t>(kept));
    // The whole file (another file: read from its start): every game kept once, none twice.
    std::ofstream(file, std::ios::binary | std::ios::trunc) << whole;
    reading.start(file.string(), db, {}, dir / "reading.json");
    REQUIRE(waitIdle(reading));
    CHECK(reading.finished() == std::string());
    CHECK(reading.progress().games == 300);
    CHECK(reading.progress().kept == static_cast<uint64_t>(kept));
    CHECK(db->snapshot()->games() == static_cast<uint64_t>(kept));
    CHECK(reading.progress().added == static_cast<uint64_t>(kept) - first);
    CHECK_FALSE(fs::exists(dir / "reading.json"));  // gone at the end
}
