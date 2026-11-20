// Lichess's evaluations kept offline (#58): prepared from a file or while the database downloads, cut short and gone
// on with (the positions read answering meanwhile), a crash at a saved place, an update while the index answers,
// Remove them, and the review's depth threshold.
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
#include "offline_evals.hpp"
#include "scripted_server.hpp"

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

struct Position {
    chess::Board board;
    std::string fen;  // the four fields Lichess writes
    std::string best;
    int cp = 0, mate = 0, depth = 0;
};

// The positions of random games of our own, each once, with one of its legal moves as its best and evaluations of
// every kind: White's scores, mates, depths from 14 to 57.
std::vector<Position> positions(std::size_t n, uint32_t seed, int shift = 0) {
    std::mt19937 rng(seed);
    std::vector<Position> out;
    std::set<std::string> seen;
    while (out.size() < n) {
        chess::Board board;
        for (int ply = 0; ply < 70 && out.size() < n; ++ply) {
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, board);
            if (moves.empty()) break;
            board.makeMove(moves[static_cast<int>(rng() % static_cast<uint32_t>(moves.size()))]);
            chess::Movelist next;
            chess::movegen::legalmoves(next, board);
            if (next.empty()) break;
            const auto fen = board.getFen();
            std::size_t at = 0;
            for (int field = 0; field < 4; ++field) at = fen.find(' ', at + (field ? 1 : 0));
            if (!seen.insert(fen.substr(0, at)).second) continue;
            Position p;
            p.board      = board;
            p.fen        = fen.substr(0, at);
            p.best       = chess::uci::moveToUci(next[static_cast<int>(rng() % static_cast<uint32_t>(next.size()))]);
            const auto i = out.size();
            p.depth      = 14 + static_cast<int>(i * 13 % 44);
            if (i % 9 == 4)
                p.mate = (i % 2 ? -1 : 1) * static_cast<int>(1 + i % 30);
            else
                p.cp = static_cast<int>(i * 31 % 900) - 450 + shift;
            out.push_back(std::move(p));
        }
    }
    return out;
}

std::string dumpLine(const Position& p) {
    const auto score = p.mate ? fmt::format("\"mate\":{}", p.mate) : fmt::format("\"cp\":{}", p.cp);
    return fmt::format(R"({{"fen":"{}","evals":[{{"pvs":[{{{},"line":"{}"}}],"knodes":{},"depth":{}}}]}})", p.fen,
                       score, p.best, 1000 + p.depth, p.depth);
}

// The database of these positions, cut in frames of about `frame` bytes of text, each after pzstd's skippable frame.
std::string database(const std::vector<Position>& all, std::size_t frame) {
    std::string text;
    for (const auto& p : all) text += dumpLine(p) + '\n';
    std::string out;
    for (std::size_t at = 0; at < text.size(); at += frame) {
        const auto piece = rawFrame(std::string_view(text).substr(at, frame));
        out += skippableFrame(static_cast<uint32_t>(piece.size())) + piece;
    }
    return out;
}

void checkFound(const OfflineEvals& evals, const Position& p) {
    CAPTURE(p.fen);
    const auto e = evals.find(p.board);
    REQUIRE(e.has_value());
    CHECK(e->cp == p.cp);
    CHECK(e->mate == p.mate);
    CHECK(e->depth == p.depth);
    CHECK(e->best == p.best);
}

std::string readFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::size_t indexes(const fs::path& folder) {
    std::size_t n = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder, ec))
        n += entry.path().filename().string().rfind("index-", 0) == 0 ? 1 : 0;
    return n;
}

}  // namespace

TEST_CASE("offline evaluations: prepared from a file downloaded before, then answering for the board's position") {
    auto all = positions(1200, 3);
    // Pawns just gone two squares, as Lichess writes them: the capture en passant legal, and the best move; then not
    // legal (d7-d5 uncovers the c8 bishop's check), where chess.hpp keeps the square after the move all the same.
    const auto after = [&](const char* fen, const char* move, const char* written, const char* best) {
        Position p;
        REQUIRE(p.board.setFen(fen));
        p.board.makeMove(chess::uci::uciToMove(p.board, move));
        p.fen   = written;
        p.best  = best;
        p.cp    = 80;
        p.depth = 33;
        all.push_back(p);
    };
    after("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 3", "f7f5",
          "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6", "e5f6");
    after("rnb1k2r/p2pp1b1/5nN1/1pp1PK1p/2P5/4PP1p/PP2Q1PP/R1B2B1R b - - 0 1", "d7d5",
          "rnb1k2r/p3p1b1/5nN1/1pppPK1p/2P5/4PP1p/PP2Q1PP/R1B2B1R w - -", "f5g5");
    CHECK(all.back().board.enpassantSq() != chess::Square::NO_SQ);
    const auto folder = fs::temp_directory_path() / "offline-evals-file";
    const auto file   = fs::temp_directory_path() / "offline-evals-file.jsonl.zst";
    fs::remove_all(folder);
    std::ofstream(file, std::ios::binary) << database(all, 20000);

    OfflineEvals evals(folder);
    CHECK_FALSE(fs::exists(folder));  // made at the first preparation, not before
    CHECK_FALSE(evals.state().ready);
    CHECK_FALSE(evals.state().stopped);
    CHECK(evals.lookup() == nullptr);
    CHECK_FALSE(evals.find(all[0].board).has_value());
    evals.setRunEntries(250);
    evals.setCheckpointEvery(0);
    evals.start(file.string(), 0);
    REQUIRE(waitUntil([&] { return !evals.busy(); }, 60s));
    CHECK(evals.finished() == std::string());
    const auto st = evals.state();
    REQUIRE(st.ready);
    CHECK(st.positions == all.size());
    CHECK(st.lines == all.size());
    CHECK(st.min_men == 0);
    CHECK(st.date.empty());  // a file's date is not the database's
    CHECK(st.bytes == fs::file_size(study::newestEvalsIndex(folder)));
    CHECK_FALSE(st.stopped);
    CHECK_FALSE(fs::exists(folder / "build"));  // its runs, logs and state gone with it
    for (const auto& p : all) checkFound(evals, p);

    // A position it does not have; the same position in Chess960: nothing.
    chess::Board other;
    REQUIRE(other.setFen("8/8/3k4/8/8/3K4/8/7R w - - 0 1"));
    CHECK_FALSE(evals.find(other).has_value());
    chess::Board chess960 = all[5].board;
    chess960.set960(true);
    CHECK_FALSE(evals.find(chess960).has_value());

    // The review takes a known position as it is from depth 30, the side to move's score.
    std::size_t known = 0, searched = 0;
    for (const auto& p : all) {
        const auto value = reviewKnownEval(*evals.lookup(), p.board);
        if (p.depth < REVIEW_KNOWN_DEPTH) {
            CHECK_FALSE(value.has_value());
            ++searched;
            continue;
        }
        ++known;
        REQUIRE(value.has_value());
        CHECK(value->known);
        CHECK(value->depth == p.depth);
        CHECK(value->best == p.best);
        CHECK(value->pv == p.best);
        REQUIRE(value->score.has_value());
        const bool white = p.board.sideToMove() == chess::Color::WHITE;
        if (p.mate) {
            CHECK(value->score->type == fastchess::ScoreType::MATE);
            CHECK(value->score->value == (white ? p.mate : -p.mate));
        } else {
            CHECK(value->score->type == fastchess::ScoreType::CP);
            CHECK(value->score->value == (white ? p.cp : -p.cp));
        }
    }
    CHECK(known > 100);
    CHECK(searched > 100);

    // The openings only: the positions with 24 men or more.
    const auto folder2 = fs::temp_directory_path() / "offline-evals-openings";
    fs::remove_all(folder2);
    OfflineEvals openings(folder2);
    openings.setRunEntries(250);
    openings.start(file.string(), study::EVALS_OPENING_MEN);
    REQUIRE(waitUntil([&] { return !openings.busy(); }, 60s));
    CHECK(openings.finished() == std::string());
    CHECK(openings.state().min_men == study::EVALS_OPENING_MEN);
    std::size_t kept = 0;
    for (const auto& p : all) {
        const bool opening = static_cast<int>(p.board.occ().count()) >= study::EVALS_OPENING_MEN;
        kept += opening ? 1 : 0;
        CHECK(openings.find(p.board).has_value() == opening);
    }
    CHECK(openings.state().positions == kept);
    CHECK(kept > 0);
    CHECK(kept < all.size());
}

TEST_CASE("offline evaluations: downloaded, cut short and gone on with, the positions read answering meanwhile") {
    const std::vector<std::vector<Position>> versions = {positions(1600, 7), positions(1600, 7, 33)};
    const std::vector<std::string> contents           = {database(versions[0], 25000), database(versions[1], 25000)};
    const std::vector<std::string> dates = {"Sun, 01 Nov 2026 17:23:27 GMT", "Tue, 01 Dec 2026 17:00:00 GMT"};
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
        const auto etag     = fmt::format("\"v{}\"", version.load());
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
    const auto url    = fmt::format("http://127.0.0.1:{}/lichess_db_eval.jsonl.zst", port);
    const auto folder = fs::temp_directory_path() / "offline-evals-download";
    fs::remove_all(folder);

    // Cut in the middle: on from the last frame's end, of the same file (If-Range), every position once.
    cut_at = static_cast<long>(contents[0].size() / 2);
    {
        OfflineEvals evals(folder);
        evals.setRunEntries(300);
        evals.setCheckpointEvery(0);
        evals.setRetryWait(1ms);
        evals.start(url, 0);
        REQUIRE(waitUntil([&] { return !evals.busy(); }, 60s));
        CHECK(evals.finished() == std::string());
        CHECK(evals.progress().bytes == contents[0].size());
        const auto st = evals.state();
        REQUIRE(st.ready);
        CHECK(st.positions == versions[0].size());
        CHECK(st.date == "2026-11-01");
        for (const auto& p : versions[0]) checkFound(evals, p);
    }
    {
        std::lock_guard lock(mutex);
        REQUIRE(asked.size() == 2);
        CHECK(asked[0] == "-|-");
        const auto offset = std::stoul(asked[1].substr(6));
        CHECK(offset > 0);
        CHECK(offset <= contents[0].size() / 2);
        CHECK(asked[1].substr(asked[1].find('|')) == "|\"v1\"");
        asked.clear();
    }

    // Held at three quarters: the runs written answer while it waits; then Cancel, and the program closed.
    fs::remove_all(folder);
    hold_at = static_cast<long>(contents[0].size() * 3 / 4);
    {
        OfflineEvals evals(folder);
        evals.setRunEntries(300);
        evals.setCheckpointEvery(0);
        evals.start(url, 0);
        REQUIRE(waitUntil([&] { return evals.progress().bytes >= contents[0].size() * 3 / 4; }));
        REQUIRE(waitUntil(
            [&] {
                evals.poll();
                return evals.find(versions[0][10].board).has_value();
            },
            20s));
        CHECK_FALSE(evals.state().ready);
        CHECK(evals.lookup()->index() == nullptr);  // the runs answer
        checkFound(evals, versions[0][10]);
        evals.cancel();
        REQUIRE(waitUntil([&] { return !evals.busy(); }));
        CHECK(evals.finished() == std::string("cancelled"));
        const auto st = evals.state();
        CHECK(st.stopped);
        CHECK(st.stopped_at > 0);
        CHECK(st.size == contents[0].size());
        CHECK(st.source == url);
        CHECK(st.kept > 0);
        CHECK(st.answering > 0);  // the runs' positions; those of the log answer once a run holds them
        CHECK(st.answering <= st.kept);
    }
    released = true;
    {
        // Opened again: what it read answers at once; Go on reads the rest only.
        OfflineEvals evals(folder);
        auto st = evals.state();
        CHECK(st.stopped);
        CHECK_FALSE(st.ready);
        REQUIRE(evals.lookup() != nullptr);
        checkFound(evals, versions[0][10]);
        evals.setRunEntries(300);
        evals.start(st.source, st.stopped_min_men);
        REQUIRE(waitUntil([&] { return !evals.busy(); }, 60s));
        CHECK(evals.finished() == std::string());
        CHECK(evals.progress().resumed);
        st = evals.state();
        REQUIRE(st.ready);
        CHECK_FALSE(st.stopped);
        CHECK(st.positions == versions[0].size());
        for (const auto& p : versions[0]) checkFound(evals, p);

        // Update: the newer file read while the index answers; then it replaces it.
        version  = 2;
        hold_at  = static_cast<long>(contents[1].size() / 2);
        released = false;
        evals.setCheckpointEvery(0);
        evals.start(url, 0);
        REQUIRE(waitUntil([&] { return evals.progress().bytes >= contents[1].size() / 2; }));
        evals.poll();
        CHECK(evals.lookup()->index() != nullptr);  // the index answers, not the runs of the update
        checkFound(evals, versions[0][20]);
        released = true;
        REQUIRE(waitUntil([&] { return !evals.busy(); }, 60s));
        CHECK(evals.finished() == std::string());
        st = evals.state();
        CHECK(st.date == "2026-12-01");
        for (const auto& p : versions[1]) checkFound(evals, p);
        CHECK(indexes(folder) == 1);  // the older one removed

        // Remove them: nothing left, nothing answers.
        evals.remove();
        CHECK_FALSE(evals.state().ready);
        CHECK(evals.lookup() == nullptr);
        CHECK_FALSE(evals.find(versions[1][0].board).has_value());
        CHECK_FALSE(fs::exists(folder));
    }
}

TEST_CASE("offline evaluations: the program killed at a place saved, then gone on with: the same index") {
    const auto all    = positions(1500, 17);
    const auto file   = fs::temp_directory_path() / "offline-evals-crash.jsonl.zst";
    const auto folder = fs::temp_directory_path() / "offline-evals-crash";
    const auto killed = fs::temp_directory_path() / "offline-evals-killed";
    fs::remove_all(folder);
    fs::remove_all(killed);
    std::ofstream(file, std::ios::binary) << database(all, 15000);

    // The folder as a kill leaves it right after the third place saved: copied there, on the reading's thread.
    int saves = 0;
    {
        OfflineEvals evals(folder);
        evals.setRunEntries(200);
        evals.setCheckpointEvery(0);
        evals.setOnSaved([&] {
            if (++saves != 3) return;
            fs::create_directories(killed);
            fs::copy(folder / "build", killed / "build", fs::copy_options::recursive);
            fs::copy_file(folder / "build" / "reading.json", killed / "state.json");
        });
        evals.start(file.string(), 0);
        REQUIRE(waitUntil([&] { return !evals.busy(); }, 60s));
        CHECK(evals.finished() == std::string());
    }
    REQUIRE(saves > 3);
    REQUIRE(fs::exists(killed / "build" / "reading.json"));
    // A run being written at that moment, left half done.
    std::ofstream(killed / "build" / "run-9999.bin.tmp", std::ios::binary) << "half a run";
    {
        OfflineEvals evals(killed);
        CHECK(evals.state().stopped);
        CHECK(evals.state().kept > 0);
        evals.setRunEntries(200);
        evals.start(file.string(), 0);
        REQUIRE(waitUntil([&] { return !evals.busy(); }, 60s));
        CHECK(evals.finished() == std::string());
        CHECK(evals.progress().resumed);
        CHECK(evals.state().positions == all.size());
        for (const auto& p : all) checkFound(evals, p);
    }
    CHECK(readFile(study::newestEvalsIndex(killed)) == readFile(study::newestEvalsIndex(folder)));

    // Killed at the very end: the index made and its runs removed, its state not yet; or runs without a state. Nothing
    // to go on from: cleared when the program starts, the index answering.
    for (const bool with_state : {true, false}) {
        const auto end = fs::temp_directory_path() / fmt::format("offline-evals-end-{}", with_state);
        fs::remove_all(end);
        fs::create_directories(end / "build");
        fs::copy_file(study::newestEvalsIndex(folder), end / "index-1.bin");
        if (with_state)
            fs::copy_file(killed / "state.json", end / "build" / "reading.json");  // its runs gone
        else
            std::ofstream(end / "build" / "run-3.bin", std::ios::binary) << "a run";
        std::ofstream(end / "index-2.bin.tmp", std::ios::binary) << "an index being written";
        OfflineEvals evals(end);
        CHECK(evals.state().ready);
        CHECK_FALSE(evals.state().stopped);
        CHECK_FALSE(fs::exists(end / "build"));
        CHECK_FALSE(fs::exists(end / "index-2.bin.tmp"));
        checkFound(evals, all[7]);
    }
}
