// Puzzles (#80): Lichess's database prepared from a file downloaded before and while it downloads (cut short, gone on
// with, updated while the store answers, removed), the user's puzzles kept in the profile (rating, results, cards),
// the RookStudio rating, a puzzle's moves as the board plays them, and the answers judged. The puzzles are made by the
// tests: random games of our own.
#include <doctest/doctest.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>

#include "free_ports.hpp"
#include "https.hpp"
#include "puzzles.hpp"
#include "scripted_server.hpp"
#include "training.hpp"

using namespace fcg;
using fcg::test::ScriptedServer;
using fcg::test::waitUntil;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

// A zstd frame of raw blocks, as the format allows: what a test writes without a compressor.
std::string rawFrame(std::string_view text) {
    std::string out;
    const auto put32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) out += static_cast<char>((v >> (8 * i)) & 0xFF);
    };
    put32(0xFD2FB528);
    out += static_cast<char>(0xA0);  // its content's size on 4 bytes, a single segment
    put32(static_cast<uint32_t>(text.size()));
    constexpr std::size_t BLOCK = 1 << 17;
    for (std::size_t at = 0;;) {
        const auto n      = std::min(BLOCK, text.size() - at);
        const bool last   = at + n >= text.size();
        const auto header = (last ? 1u : 0u) | (static_cast<uint32_t>(n) << 3);  // a raw block
        out += static_cast<char>(header & 0xFF);
        out += static_cast<char>((header >> 8) & 0xFF);
        out += static_cast<char>((header >> 16) & 0xFF);
        out.append(text.substr(at, n));
        at += n;
        if (last) return out;
    }
}

// pzstd's frame before each frame, telling its size.
std::string skippableFrame(uint32_t next) {
    std::string out;
    for (const uint32_t v : {0x184D2A50u, 4u, next})
        for (int i = 0; i < 4; ++i) out += static_cast<char>((v >> (8 * i)) & 0xFF);
    return out;
}

// Puzzles of our own: a position of a random game, then two to six moves played from it.
std::vector<study::Puzzle> puzzles(std::size_t n, uint32_t seed, int shift = 0) {
    static constexpr char DIGITS[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::mt19937 rng(seed);
    std::vector<study::Puzzle> out;
    std::set<std::string> ids;
    while (out.size() < n) {
        chess::Board board;
        const int plies = 6 + static_cast<int>(rng() % 50);
        bool over       = false;
        for (int ply = 0; ply < plies && !over; ++ply) {
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, board);
            if (moves.empty())
                over = true;
            else
                board.makeMove(moves[static_cast<int>(rng() % static_cast<uint32_t>(moves.size()))]);
        }
        if (over) continue;
        study::Puzzle p;
        p.fen = board.getFen();
        for (int i = 0; i < 2 + 2 * static_cast<int>(rng() % 3); ++i) {
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, board);
            if (moves.empty()) break;
            const auto m = moves[static_cast<int>(rng() % static_cast<uint32_t>(moves.size()))];
            p.moves.push_back(chess::uci::moveToUci(m));
            board.makeMove(m);
        }
        if (p.moves.size() < 2) continue;
        p.id.clear();
        for (int i = 0; i < 5; ++i) p.id += DIGITS[rng() % 62];
        if (!ids.insert(p.id).second) continue;
        for (int i = 0; i < 8; ++i) p.game += DIGITS[rng() % 62];
        p.rating     = 600 + static_cast<int>(rng() % 2000) + shift;
        p.deviation  = 75;
        p.popularity = 90;
        p.themes     = {rng() % 2 ? "fork" : "pin", "short"};
        out.push_back(std::move(p));
    }
    return out;
}

std::string lineOf(const study::Puzzle& p) {
    std::string moves, themes;
    for (const auto& m : p.moves) moves += (moves.empty() ? "" : " ") + m;
    for (const auto& t : p.themes) themes += (themes.empty() ? "" : " ") + t;
    return fmt::format("{},{},{},{},{},{},{},{},{},,", p.id, p.fen, moves, p.rating, p.deviation, p.popularity, 500,
                       themes, p.gameUrl());
}

// The database of these puzzles, its header first, cut in frames of about `frame` bytes of text.
std::string database(const std::vector<study::Puzzle>& all, std::size_t frame) {
    std::string text =
        "PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,GameUrl,OpeningTags,DailyDate\n";
    for (const auto& p : all) text += lineOf(p) + '\n';
    std::string out;
    for (std::size_t at = 0; at < text.size(); at += frame) {
        const auto piece = rawFrame(std::string_view(text).substr(at, frame));
        out += skippableFrame(static_cast<uint32_t>(piece.size())) + piece;
    }
    return out;
}

bool keeps(const OfflinePuzzles& offline, const study::Puzzle& p) {
    const auto store = offline.store();
    if (!store) return false;
    for (uint32_t k = 0; k < store->size(); ++k)
        if (store->idOf(k) == p.id) return store->puzzle(k).moves == p.moves;
    return false;
}

}  // namespace

TEST_CASE("puzzles offline: prepared from a file downloaded before") {
    const auto all    = puzzles(600, 80);
    const auto folder = fs::temp_directory_path() / "puzzles-file";
    const auto file   = fs::temp_directory_path() / "puzzles-file.csv.zst";
    fs::remove_all(folder);
    std::ofstream(file, std::ios::binary) << database(all, 9000);
    JobGroup jobs;
    OfflinePuzzles offline(jobs, folder);
    CHECK_FALSE(fs::exists(folder));  // made at the first preparation, not before
    CHECK_FALSE(offline.state().ready);
    CHECK(offline.store() == nullptr);
    offline.setCheckpointEvery(0);
    offline.start(file.string());
    REQUIRE(waitUntil([&] { return !offline.busy(); }, 60s));
    CHECK(offline.finished() == TaskEnd{});
    const auto st = offline.state();
    REQUIRE(st.ready);
    CHECK(st.puzzles == all.size());
    CHECK(st.date.empty());  // a file's date is not the database's
    CHECK(st.bytes == fs::file_size(study::newestPuzzleStore(folder)));
    CHECK_FALSE(st.stopped);
    CHECK_FALSE(fs::exists(folder / "build"));
    CHECK(keeps(offline, all.front()));
    CHECK(keeps(offline, all.back()));
    // Removed: the store gone, as if never made.
    offline.remove();
    CHECK_FALSE(offline.state().ready);
    CHECK(offline.store() == nullptr);
    CHECK_FALSE(fs::exists(folder));
}

TEST_CASE("puzzles offline: downloaded, cut short and gone on with, updated while the store answers") {
    const std::vector<std::vector<study::Puzzle>> versions = {puzzles(900, 81), puzzles(700, 82, 100)};
    const std::vector<std::string> contents = {database(versions[0], 12000), database(versions[1], 12000)};
    const std::vector<std::string> dates    = {"Sun, 01 Nov 2026 07:57:40 GMT", "Tue, 01 Dec 2026 07:00:00 GMT"};
    std::atomic<int> version{1};
    std::atomic<long> cut_at{-1};   // the next answer stops there
    std::atomic<long> hold_at{-1};  // the next answer waits there for `released`
    std::atomic<bool> released{false};
    std::mutex mutex;
    std::vector<std::string> asked;  // "Range|If-Range" of each request
    const auto port = fcg::test::freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        const auto range = httpHeader(head, "Range"), if_range = httpHeader(head, "If-Range");
        {
            std::lock_guard lock(mutex);
            asked.push_back(range.value_or("-") + "|" + if_range.value_or("-"));
        }
        const auto v        = static_cast<std::size_t>(version - 1);
        const auto& content = contents[v];
        const auto etag     = fmt::format("\"p{}\"", version.load());
        std::size_t from    = 0;
        if (range && (!if_range || *if_range == etag)) from = std::stoul(range->substr(6));
        if (from > 0)
            c.sendAll(
                fmt::format("HTTP/1.1 206 Partial Content\r\nETag: {}\r\nContent-Range: bytes {}-{}/{}\r\n"
                            "Content-Length: {}\r\n\r\n",
                            etag, from, content.size() - 1, content.size(), content.size() - from));
        else
            c.sendAll(fmt::format("HTTP/1.1 200 OK\r\nETag: {}\r\nLast-Modified: {}\r\nContent-Length: {}\r\n\r\n",
                                  etag, dates[v], content.size()));
        const long cut       = cut_at.exchange(-1);
        const long hold      = hold_at.exchange(-1);
        const std::size_t to = cut >= 0 ? static_cast<std::size_t>(cut) : content.size();
        if (hold >= 0 && static_cast<std::size_t>(hold) > from) {
            c.sendAll(std::string_view(content).substr(from, static_cast<std::size_t>(hold) - from));
            waitUntil([&] { return released.load(); }, 60s);
            from = static_cast<std::size_t>(hold);
        }
        if (to > from) c.sendAll(std::string_view(content).substr(from, to - from));
    });
    REQUIRE(server.ok());
    const auto url    = fmt::format("http://127.0.0.1:{}/lichess_db_puzzle.csv.zst", port);
    const auto folder = fs::temp_directory_path() / "puzzles-download";
    fs::remove_all(folder);

    // Cut in the middle: on from the last frame's end, of the same file (If-Range), every puzzle once.
    cut_at = static_cast<long>(contents[0].size() / 2);
    {
        JobGroup jobs;
        OfflinePuzzles offline(jobs, folder);
        offline.setCheckpointEvery(0);
        offline.setRetryWait(1ms);
        offline.start(url);
        REQUIRE(waitUntil([&] { return !offline.busy(); }, 60s));
        CHECK(offline.finished() == TaskEnd{});
        const auto st = offline.state();
        REQUIRE(st.ready);
        CHECK(st.puzzles == versions[0].size());
        CHECK(st.date == "2026-11-01");
        CHECK(keeps(offline, versions[0][17]));
    }
    {
        std::lock_guard lock(mutex);
        REQUIRE(asked.size() == 2);
        CHECK(asked[0] == "-|-");
        const auto offset = std::stoul(asked[1].substr(6));
        CHECK(offset > 0);
        CHECK(offset <= contents[0].size() / 2);
        CHECK(asked[1].substr(asked[1].find('|')) == "|\"p1\"");
        asked.clear();
    }

    // Held at three quarters, then Cancel and the program closed: opened again, Go on reads the rest only.
    fs::remove_all(folder);
    hold_at = static_cast<long>(contents[0].size() * 3 / 4);
    {
        JobGroup jobs;
        OfflinePuzzles offline(jobs, folder);
        offline.setCheckpointEvery(0);
        offline.start(url);
        REQUIRE(waitUntil([&] { return offline.progress().bytes >= contents[0].size() * 3 / 4; }));
        offline.cancel();
        REQUIRE(waitUntil([&] { return !offline.busy(); }));
        CHECK(offline.finished() == TaskEnd::cancel());
        const auto st = offline.state();
        CHECK(st.stopped);
        CHECK_FALSE(st.ready);
        CHECK(st.stopped_at > 0);
        CHECK(st.size == contents[0].size());
        CHECK(st.source == url);
        CHECK(st.kept > 0);
    }
    released = true;
    {
        JobGroup jobs;
        OfflinePuzzles offline(jobs, folder);
        auto st = offline.state();
        CHECK(st.stopped);
        offline.start(st.source);
        REQUIRE(waitUntil([&] { return !offline.busy(); }, 60s));
        CHECK(offline.finished() == TaskEnd{});
        CHECK(offline.progress().resumed);
        st = offline.state();
        REQUIRE(st.ready);
        CHECK_FALSE(st.stopped);
        CHECK(st.puzzles == versions[0].size());
        for (std::size_t i = 0; i < versions[0].size(); i += 97) CHECK(keeps(offline, versions[0][i]));

        // Update: the newer file read while the store answers; then it replaces it.
        version  = 2;
        hold_at  = static_cast<long>(contents[1].size() / 2);
        released = false;
        offline.start(url);
        REQUIRE(waitUntil([&] { return offline.progress().bytes >= contents[1].size() / 2; }));
        CHECK(offline.store() != nullptr);
        CHECK(offline.state().puzzles == versions[0].size());  // the old store answers meanwhile
        released = true;
        REQUIRE(waitUntil([&] { return !offline.busy(); }, 60s));
        CHECK(offline.finished() == TaskEnd{});
        st = offline.state();
        CHECK(st.puzzles == versions[1].size());
        CHECK(st.date == "2026-12-01");
        CHECK(keeps(offline, versions[1][3]));
        std::size_t stores = 0;
        for (const auto& entry : fs::directory_iterator(folder))
            stores += entry.path().filename().string().rfind("puzzles-", 0) == 0 ? 1 : 0;
        CHECK(stores == 1);  // the older one removed

        // Removed during an update, held by the server (#105): no store at once and nothing waits for it; its files
        // go once it ended, at the finished() that sees its end, which says nothing.
        hold_at  = static_cast<long>(contents[1].size() / 2);
        released = false;
        offline.start(url);
        REQUIRE(waitUntil([&] { return offline.progress().bytes >= contents[1].size() / 2; }));
        offline.remove();
        CHECK(offline.store() == nullptr);
        CHECK_FALSE(offline.state().ready);
        CHECK(fs::exists(folder / "build"));  // its thread writes there until it ends
        released = true;
        REQUIRE(waitUntil([&] { return !offline.busy(); }, 60s));
        CHECK_FALSE(offline.finished().has_value());
        CHECK_FALSE(fs::exists(folder));
        CHECK_FALSE(offline.state().ready);
        CHECK_FALSE(offline.state().stopped);
    }
}

TEST_CASE("puzzles: the RookStudio rating starts from Lichess's, or a new player's") {
    const auto fresh = startingRating(std::nullopt);
    CHECK(fresh.glicko.rating == 1500);
    CHECK(fresh.glicko.deviation == study::glicko2::MAX_DEVIATION);
    CHECK(fresh.from == "default");
    const auto linked = startingRating(1905, 62);
    CHECK(linked.glicko.rating == 1905);
    CHECK(linked.glicko.deviation == 62);
    CHECK(linked.from == "lichess");
    CHECK(startingRating(1905).glicko.deviation == 100);  // Lichess said no deviation: a hundred

    // A puzzle solved against its rating: up; missed: down, more than a win brings.
    const int64_t now = 1'800'000'000;
    const auto won    = ratedAfter(linked, 1700, 75, true, now);
    const auto lost   = ratedAfter(linked, 1700, 75, false, now);
    CHECK(won.glicko.rating > linked.glicko.rating);
    CHECK(lost.glicko.rating < linked.glicko.rating);
    CHECK(won.games == 1);
    CHECK(won.last == now);
    // A month without puzzles: less sure, and the next result moves the rating more.
    const auto later = ratedAfter(won, 1700, 75, true, now + 30 * 86400);
    const auto soon  = ratedAfter(won, 1700, 75, true, now + 60);
    CHECK(later.glicko.rating - won.glicko.rating > soon.glicko.rating - won.glicko.rating);
}

TEST_CASE("puzzles: the user's puzzles kept in the profile, missed ones as cards of the day's review") {
    const auto folder = fs::temp_directory_path() / "puzzles-record";
    fs::remove_all(folder);
    CHECK_FALSE(readPuzzleRecord(folder).rating);
    const auto all    = puzzles(3, 83);
    const int64_t now = 1'800'000'000;
    PuzzleResult won{all[0].id, now, true, all[0].rating, 1500, 1620, {"fork", "short"}, 9000, false, false};
    PuzzleResult missed{all[1].id, now + 20, false, all[1].rating, 1620, 1580, {"pin", "short"}, 30000, true, false};
    PuzzleResult again{all[1].id, now + 86400, true, all[1].rating, 0, 0, {"pin", "short"}, 5000, false, true};
    for (const auto& r : {won, missed, again}) appendPuzzleResult(folder, r);
    PuzzleRating rating  = startingRating(std::nullopt);
    rating.glicko.rating = 1580;
    rating.games         = 2;
    rating.last          = now + 20;
    REQUIRE(writePuzzleRating(folder, rating));
    std::map<std::string, PuzzleCard> cards;
    const auto day_end = trainingDayEnd(now + 20);
    cards[all[1].id]   = missedCard(all[1], now + 20, day_end, trainingScheduler());
    CHECK(cards[all[1].id].card.due == day_end + 1);  // tomorrow, not later in the series
    CHECK(cards[all[1].id].card.lapses == 0);
    CHECK(cards[all[1].id].card.reps == 1);
    REQUIRE(writePuzzleCards(folder, cards));

    const auto record = readPuzzleRecord(folder);
    REQUIRE(record.rating);
    CHECK(record.rating->glicko.rating == 1580);
    CHECK(record.rating->games == 2);
    CHECK(record.rating->last == now + 20);
    CHECK(record.results == std::vector<PuzzleResult>{won, missed, again});
    CHECK(record.seen == std::set<std::string>{all[0].id, all[1].id});
    // Each theme's results: a review's answer is not counted again.
    CHECK(record.themes.at("short").tried == 2);
    CHECK(record.themes.at("short").won == 1);
    CHECK(record.themes.at("pin").won == 0);
    REQUIRE(record.cards.count(all[1].id) == 1);
    CHECK(record.cards.at(all[1].id) == cards.at(all[1].id));
    CHECK(cardsDue(record.cards, day_end) == 0);  // not today
    CHECK(cardsDue(record.cards, day_end + 86400) == 1);
}

TEST_CASE("puzzles: the moves as the board plays them, the answers judged, a mate taken whatever the solution") {
    // Our own position and line: the solver's first move right, another wrong, the last one solving it.
    study::Puzzle p;
    p.id      = "mAte2";
    p.fen     = "6k1/5p1p/5Q2/8/8/8/5PPP/6K1 b - - 0 30";
    p.moves   = {"h7h6", "f6d8", "g8g7", "d8e7"};
    auto play = puzzlePlay(p);
    REQUIRE(play);
    CHECK(play->before.sideToMove() == chess::Color::BLACK);
    CHECK(chess::uci::moveToUci(play->opponent) == "h7h6");
    REQUIRE(play->solution.size() == 3);
    auto board = play->before;
    board.makeMove(play->opponent);
    const auto first = chess::uci::uciToMove(board, "f6d8");
    CHECK(judgeMove(*play, 0, board, first) == PuzzleAnswer::Right);
    CHECK(judgeMove(*play, 0, board, chess::uci::uciToMove(board, "f6f7")) == PuzzleAnswer::Wrong);
    board.makeMove(first);
    board.makeMove(play->solution[1]);
    CHECK(judgeMove(*play, 2, board, play->solution[2]) == PuzzleAnswer::Solved);

    // A mate in one on the back rank: the solution's rook, or the other one, which mates as well.
    study::Puzzle mate;
    mate.id    = "mAte1";
    mate.fen   = "6k1/2p2ppp/8/8/8/8/5PPP/RR4K1 b - - 0 30";
    mate.moves = {"c7c6", "a1a8"};
    play       = puzzlePlay(mate);
    REQUIRE(play);
    board = play->before;
    board.makeMove(play->opponent);
    CHECK(judgeMove(*play, 0, board, chess::uci::uciToMove(board, "a1a8")) == PuzzleAnswer::Solved);
    CHECK(judgeMove(*play, 0, board, chess::uci::uciToMove(board, "b1b8")) == PuzzleAnswer::Solved);
    CHECK(judgeMove(*play, 0, board, chess::uci::uciToMove(board, "a1a7")) == PuzzleAnswer::Wrong);

    // Moves that do not play: no puzzle.
    auto broken         = mate;
    broken.moves.back() = "a1h8";
    CHECK_FALSE(puzzlePlay(broken));
    broken.moves = {"h7h6"};
    CHECK_FALSE(puzzlePlay(broken));
}

TEST_CASE("puzzles: the themes' names, the openings', the difficulties") {
    CHECK(puzzleThemeName("fork") == "Fork");
    CHECK(puzzleThemeName("mateIn2") == "Mate in 2");
    CHECK(puzzleThemeName("aThemeOfTomorrow") == "aThemeOfTomorrow");
    CHECK(puzzleOpeningName("Sicilian_Defense_Najdorf_Variation") == "Sicilian Defense Najdorf Variation");
    CHECK(puzzleThemesShown().size() >= 70);
    CHECK(difficultyOffset(PuzzleDifficulty::Easier) == -300);
    CHECK(difficultyOffset(PuzzleDifficulty::Normal) == 0);
    CHECK(std::string(difficultyKey(PuzzleDifficulty::Harder)) == "harder");
}
