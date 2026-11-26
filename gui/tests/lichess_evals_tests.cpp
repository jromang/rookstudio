// The positions Lichess's players had analysed (#53): the candidates, the database's lines, the book, and the
// reading of the database while it downloads, cut short and resumed.
#include <doctest/doctest.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>

#include "answering_server.hpp"
#include "free_ports.hpp"
#include "https.hpp"
#include "lichess_evals.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using fcg::test::ScriptedServer;
using fcg::test::waitUntil;
namespace fs = std::filesystem;

namespace {

const EvalCandidate* findCandidate(const std::vector<EvalCandidate>& all, std::string_view key) {
    for (const auto& c : all)
        if (evalKey(c.epd) == key) return &c;
    return nullptr;
}

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

std::string evalLine(std::string_view fen, int depth, int cp, std::string_view padding = {}) {
    return fmt::format(
        R"({{"fen":"{}","evals":[{{"pvs":[{{"cp":{},"line":"e2e4 e7e5{}"}}],"knodes":1000,"depth":{}}}]}})", fen, cp,
        padding, depth);
}

const std::string AFTER_E4    = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -";
const std::string AFTER_E4_C5 = "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -";
const std::string AFTER_E4_A6 = "rnbqkbnr/1ppppppp/p7/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -";
const std::string AFTER_H4_H5 = "rnbqkbnr/ppppppp1/8/7p/7P/8/PPPPPPP1/RNBQKBNR w KQkq -";

// A database of `fillers` positions no book wants and three of the named openings', cut in frames of about
// `frame` bytes of text, each after pzstd's skippable frame; `cp` changes the evaluations (another version).
std::string database(int fillers, std::size_t frame, int cp, int& lines) {
    std::vector<std::string> all;
    for (int i = 0; i < fillers; ++i)
        all.push_back(evalLine(fmt::format("8/8/8/8/8/8/{}/K6k w - -", i % 2 ? "P7" : "8"), 30 + i % 20, i % 300,
                               std::string(static_cast<std::size_t>(200 + i % 150), 'x')));
    all.insert(all.begin() + fillers / 5, evalLine(AFTER_E4, 45, cp));
    all.insert(all.begin() + fillers / 2, evalLine(AFTER_E4_C5, 38, cp + 10));
    all.insert(all.begin() + fillers * 4 / 5, evalLine(AFTER_H4_H5, 33, -cp));
    lines = static_cast<int>(all.size());
    std::string text;
    for (const auto& l : all) text += l + '\n';
    std::string out;
    for (std::size_t at = 0; at < text.size(); at += frame) {
        const auto piece = rawFrame(std::string_view(text).substr(at, frame));
        out += skippableFrame(static_cast<uint32_t>(piece.size())) + piece;
    }
    return out;
}

std::string readAll(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

}  // namespace

TEST_CASE("evaluations: the positions of the named lines and one move off them, by the shortest way") {
    const std::vector<NamedOpening> all = {
        {"B00", "King's Pawn Game", "1. e4"},
        {"C44", "King's Knight Opening: Normal Variation", "1. e4 e5 2. Nf3 Nc6"},
        {"A06", "Zukertort Opening: Transposed", "1. Nf3 Nc6 2. e4 e5"},  // the same position at the end
        {"A00", "Bad Line", "1. e4 Zz9"},                                 // cannot be read: left out
    };
    const auto candidates = evalCandidates(all);
    std::size_t on_line   = 0;
    for (const auto& c : candidates) on_line += c.on_line ? 1 : 0;
    CHECK(on_line == 7);  // 1. e4, 1... e5, 2. Nf3, 2... Nc6; 1. Nf3, 1... Nc6, 2. e4
    // The end of two lines: named by the first in the table's order.
    const auto* end = findCandidate(candidates, "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq");
    REQUIRE(end != nullptr);
    CHECK(end->on_line);
    CHECK(end->plies == 4);
    CHECK(end->eco == "C44");
    CHECK(end->epd == "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - hmvc 2; fmvn 3;");
    // On a line before its end: the last named variation on the way.
    const auto* e5 = findCandidate(candidates, "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq");
    REQUIRE(e5 != nullptr);
    CHECK(e5->name == "King's Pawn Game");
    CHECK(e5->plies == 2);
    // One move off: of the variation it leaves, a ply further.
    const auto* c5 = findCandidate(candidates, evalKey(AFTER_E4_C5));
    REQUIRE(c5 != nullptr);
    CHECK_FALSE(c5->on_line);
    CHECK(c5->plies == 2);
    CHECK(c5->name == "King's Pawn Game");
    // Each position once: 7 on the lines and the moves off them (20 after 1. e4, ...).
    std::set<std::string> keys;
    for (const auto& c : candidates) keys.insert(std::string(evalKey(c.epd)));
    CHECK(keys.size() == candidates.size());
    CHECK(candidates.size() > 100);
    // The program's table: tens of thousands, the named positions among them.
    const auto real = evalCandidates(namedOpenings());
    CHECK(real.size() > 50000);
    CHECK(findCandidate(real, evalKey(AFTER_E4)) != nullptr);
    CHECK(findCandidate(real, evalKey(AFTER_H4_H5)) != nullptr);
}

TEST_CASE("evaluations: a line of the database, its deepest evaluation; the positions found") {
    // As Lichess writes them: White's point of view, several searches of several lines.
    const std::string line =
        R"({"fen":")" + AFTER_E4 +
        R"(","evals":[{"pvs":[{"cp":31,"line":"c7c5 g1f3"},{"cp":40,"line":"e7e5 g1f3"}],"knodes":4189,"depth":41},)"
        R"({"pvs":[{"cp":27,"line":"c7c5 g1f3"}],"knodes":9000,"depth":52},{"pvs":[{"cp":20,"line":"e7e6"}],)"
        R"("knodes":8000,"depth":52}]})";
    std::string_view key;
    Evaluation e;
    REQUIRE(readEvalLine(line, key, e));
    CHECK(key == evalKey(AFTER_E4));
    CHECK(e.depth == 52);
    CHECK(e.knodes == 9000);
    CHECK(e.cp == 27);
    CHECK(e.mate == 0);
    const std::string mate = R"({"fen":"6k1/6p1/8/4K3/4NN2/8/8/8 w - -","evals":[{"pvs":[{"mate":15,"line":"e5e6"}],)"
                             R"("knodes":589893,"depth":95}]})";
    REQUIRE(readEvalLine(mate, key, e));
    CHECK(e.mate == 15);
    CHECK_FALSE(readEvalLine("not json", key, e));

    EvalScanner scanner(evalCandidates({{"B00", "King's Pawn Game", "1. e4"}}));
    CHECK(scanner.line(mate) == nullptr);  // no book's
    const auto* found = scanner.line(line);
    REQUIRE(found != nullptr);
    CHECK(found->evaluation.depth == 52);
    CHECK(found->position.name == "King's Pawn Game");
    CHECK(scanner.line(evalLine(AFTER_E4, 30, 99)) == nullptr);       // shallower: the deeper one stays
    REQUIRE(scanner.line(evalLine(AFTER_E4_A6, 36, 70)) != nullptr);  // one move off 1. e4
    CHECK(scanner.kept() == 2);
    const auto kept = scanner.found();  // in the candidates' order: 1. e4 first
    REQUIRE(kept.size() == 2);
    CHECK(kept[0].evaluation.cp == 27);
    CHECK(kept[1].evaluation.cp == 70);
    // As the files keep them.
    for (const auto& k : kept) {
        const auto back = readEvaluatedRow(evaluatedRow(k));
        REQUIRE(back);
        CHECK(back->position.epd == k.position.epd);
        CHECK(back->position.plies == k.position.plies);
        CHECK(back->position.on_line == k.position.on_line);
        CHECK(back->position.name == k.position.name);
        CHECK(back->evaluation == k.evaluation);
    }
    EvaluatedPosition mated{{"8/8/8/8/8/8/8/K6k w - - hmvc 0; fmvn 1;", "A00", "Mate", 9, false}, {40, 0, -3, 5}};
    const auto back = readEvaluatedRow(evaluatedRow(mated));
    REQUIRE(back);
    CHECK(back->evaluation.mate == -3);
    CHECK_FALSE(readEvaluatedRow("too\tfew\tfields"));
}

TEST_CASE("evaluations: the book: the window, the depth, the plies, one per variation, an even spread") {
    const auto at = [](const std::string& name, const std::string& eco, int plies, bool on_line, int depth, int cp,
                       const std::string& side = "w") {
        return EvaluatedPosition{{fmt::format("8/8/8/8/8/8/{}/K6k {} - - hmvc 0; fmvn {};", plies, side, plies / 2 + 1),
                                  eco, name, plies, on_line},
                                 {depth, cp, 0, 1}};
    };
    std::vector<EvaluatedPosition> all = {
        at("Sicilian Defense", "B20", 10, false, 40, 50),
        at("Sicilian Defense", "B20", 9, true, 31, 35, "b"),  // on its line: before a deeper one off it
        at("Italian Game", "C50", 12, true, 35, 60),
        at("Italian Game", "C50", 12, true, 42, 75),    // deeper
        at("Queen's Gambit", "D06", 8, true, 29, 50),   // too shallow
        at("Ruy Lopez", "C60", 7, true, 50, 50),        // too early
        at("French Defense", "C00", 10, true, 50, 90),  // out of the window
        at("Caro-Kann Defense", "B10", 16, true, 33, 30),
    };
    all.push_back({{"x w - - hmvc 0; fmvn 5;", "B01", "Mate in", 10, true}, {60, 0, 4, 1}});  // a mate: never
    EvaluatedSuiteOptions options;
    auto kept = evaluatedSuite(all, options);
    REQUIRE(kept.size() == 3);
    CHECK(kept[0].name == "Caro-Kann Defense");
    CHECK(kept[1].name == "Sicilian Defense");
    CHECK(kept[1].plies == 9);
    // Its evaluation for the side to move (Black there), its depth and its opening.
    CHECK(kept[1].epd == "8/8/8/8/8/8/9/K6k b - - hmvc 0; fmvn 5; ce -35; acd 31; c0 \"B20 Sicilian Defense\";");
    CHECK(kept[2].name == "Italian Game");
    CHECK(kept[2].cp == 75);
    CHECK(kept[2].depth == 42);
    // Every position of the window, not one per variation.
    options.one_per_name = false;
    CHECK(evaluatedSuite(all, options).size() == 5);
    // A wider window and depth, fewer wanted: spread over the ECO order.
    options           = {};
    options.from_cp   = 0;
    options.to_cp     = 100;
    options.depth     = 0;
    options.min_plies = 1;
    options.most      = 2;
    kept              = evaluatedSuite(all, options);
    REQUIRE(kept.size() == 2);
    CHECK(kept[0].eco == "B10");
    CHECK(kept[1].eco == "C50");  // of B10 B20 C00 C50 C60 D06
}

TEST_CASE("evaluations: the database's size asked by its first byte") {
    const auto port = fcg::test::freePort();
    ScriptedServer server(port, [&](const std::string& head, net::TcpConnection& c) {
        if (httpHeader(head, "Range") == "bytes=0-0")
            c.sendAll(
                "HTTP/1.1 206 Partial Content\r\nLast-Modified: Sun, 01 Nov 2026 17:23:27 GMT\r\n"
                "Content-Range: bytes 0-0/22499667845\r\nContent-Length: 1\r\n\r\nx");
        else
            c.sendAll("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    });
    REQUIRE(server.ok());
    const auto size = evalsSize(fmt::format("http://127.0.0.1:{}/lichess_db_eval.jsonl.zst", port));
    CHECK_FALSE(size.failed());
    CHECK(size.bytes == 22499667845ull);
    CHECK(size.date == "2026-11-01");
}

TEST_CASE("evaluations: the database's size not known, each failure told by its kind") {
    fcg::test::AnsweringServer server;
    REQUIRE(server.ok());
    HostGate::instance().setPause(std::chrono::milliseconds(50));
    const auto url = server.base() + "/lichess_db_eval.jsonl.zst";
    struct Case {
        int status;
        lichess::NetError net;
    };
    for (const auto& [status, net] :
         {Case{401, lichess::NetError::Unauthorized}, Case{403, lichess::NetError::Forbidden},
          Case{404, lichess::NetError::NotFound}, Case{429, lichess::NetError::RateLimited},
          Case{503, lichess::NetError::Server}}) {
        CAPTURE(status);
        server.answer(status, "<html></html>");
        const auto size = evalsSize(url);
        CHECK(size.net == net);
        CHECK(size.bytes == 0);
    }
    // An answer without the size: not what the server sends.
    server.answer(200, "");
    CHECK(evalsSize(url).net == lichess::NetError::BadAnswer);
    const auto nobody = evalsSize(fmt::format("http://127.0.0.1:{}/lichess_db_eval.jsonl.zst", fcg::test::freePort()));
    CHECK(nobody.net == lichess::NetError::Offline);
    CHECK(lichess::describe(nobody) == "Lichess cannot be reached");
    HostGate::instance().setPause(std::chrono::seconds(60));
    HostGate::instance().tooMany("lichess-evals.invalid");
    std::atomic<bool> cancel{true};
    CHECK(evalsSize("http://lichess-evals.invalid/lichess_db_eval.jsonl.zst", &cancel).net ==
          lichess::NetError::Cancelled);
}

TEST_CASE("evaluations: read while it downloads, cut short and resumed at a frame's end, or from its start") {
    int lines_v1 = 0, lines_v2 = 0;
    const std::vector<std::string> contents = {database(3000, 60000, 20, lines_v1),
                                               database(2000, 50000, 40, lines_v2)};
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
        const auto& content = contents[static_cast<std::size_t>(version - 1)];
        const auto etag     = fmt::format("\"v{}\"", version.load());
        std::size_t from    = 0;
        if (range && (!if_range || *if_range == etag)) from = std::stoul(range->substr(6));
        if (from > 0)
            c.sendAll(
                fmt::format("HTTP/1.1 206 Partial Content\r\nETag: {}\r\nContent-Range: bytes {}-{}/{}\r\n"
                            "Content-Length: {}\r\n\r\n",
                            etag, from, content.size() - 1, content.size(), content.size() - from));
        else
            c.sendAll(
                fmt::format("HTTP/1.1 200 OK\r\nETag: {}\r\nLast-Modified: Sun, 01 Nov 2026 17:23:27 GMT\r\n"
                            "Content-Length: {}\r\n\r\n",
                            etag, content.size()));
        const long cut       = cut_at.exchange(-1);
        const long hold      = hold_at.exchange(-1);
        const std::size_t to = cut >= 0 ? static_cast<std::size_t>(cut) : content.size();
        if (hold >= 0 && static_cast<std::size_t>(hold) > from) {
            c.sendAll(std::string_view(content).substr(from, static_cast<std::size_t>(hold) - from));
            waitUntil([&] { return released.load(); });
            from = static_cast<std::size_t>(hold);
        }
        if (to > from) c.sendAll(std::string_view(content).substr(from, to - from));
    });
    REQUIRE(server.ok());
    const auto url    = fmt::format("http://127.0.0.1:{}/lichess_db_eval.jsonl.zst", port);
    const auto folder = fs::temp_directory_path() / "evaluations";
    fs::remove_all(folder);

    // Cut short in the middle: on from the last frame's end, of the same file, everything found once.
    cut_at = static_cast<long>(contents[0].size() / 2);
    {
        JobGroup jobs;
        EvalsReading reading(jobs);
        reading.setCheckpointEvery(0);
        reading.setRetryWait(std::chrono::milliseconds(1));
        reading.start(url, folder);
        REQUIRE(waitUntil([&] { return !reading.busy(); }, std::chrono::seconds(60)));
        CHECK(reading.finished() == std::string());
        CHECK(reading.progress().lines == static_cast<uint64_t>(lines_v1));
        CHECK(reading.progress().bytes == contents[0].size());
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
    auto folder_state = evalsFolder(folder);
    CHECK(folder_state.done);
    CHECK(folder_state.date == "2026-11-01");
    CHECK(folder_state.lines == static_cast<uint64_t>(lines_v1));
    CHECK(folder_state.kept == 3);
    CHECK(folder_state.stopped_at == 0);
    auto found = evaluatedPositions(folder);
    REQUIRE(found.size() == 3);
    for (const auto& f : found) {
        if (evalKey(f.position.epd) == evalKey(AFTER_E4)) CHECK(f.evaluation.cp == 20);
        if (evalKey(f.position.epd) == evalKey(AFTER_E4_C5)) CHECK(f.evaluation.depth == 38);
        if (evalKey(f.position.epd) == evalKey(AFTER_H4_H5)) CHECK(f.evaluation.cp == -20);
    }
    CHECK_FALSE(fs::exists(folder / "reading.tsv"));

    // Cancelled, then the program closed: the next reading goes on from where it stopped.
    fs::remove_all(folder);
    hold_at = static_cast<long>(contents[0].size() * 3 / 4);
    {
        JobGroup jobs;
        EvalsReading reading(jobs);
        reading.setCheckpointEvery(0);
        reading.start(url, folder);
        REQUIRE(waitUntil([&] { return reading.progress().bytes >= contents[0].size() * 3 / 4; }));
        reading.cancel();
        REQUIRE(waitUntil([&] { return !reading.busy(); }));
        CHECK(reading.finished() == std::string("cancelled"));
    }
    released     = true;
    folder_state = evalsFolder(folder);
    CHECK_FALSE(folder_state.done);
    CHECK(folder_state.stopped_at > 0);
    CHECK(folder_state.size == contents[0].size());
    CHECK(folder_state.source == url);
    {
        JobGroup jobs;
        EvalsReading reading(jobs);
        reading.start(url, folder);
        REQUIRE(waitUntil([&] { return !reading.busy(); }, std::chrono::seconds(60)));
        CHECK(reading.finished() == std::string());
        CHECK(reading.progress().resumed);
        CHECK(reading.progress().lines == static_cast<uint64_t>(lines_v1));
    }
    CHECK(evaluatedPositions(folder).size() == 3);

    // Cut short, then the file changes on the server: read again from its start, the new evaluations only.
    fs::remove_all(folder);
    cut_at = static_cast<long>(contents[0].size() / 3);
    {
        JobGroup jobs;
        EvalsReading reading(jobs);
        reading.setCheckpointEvery(0);
        reading.setRetryWait(std::chrono::milliseconds(200));
        reading.start(url, folder);
        REQUIRE(waitUntil([&] { return reading.progress().bytes >= contents[0].size() / 3; }));
        version = 2;
        REQUIRE(waitUntil([&] { return !reading.busy(); }, std::chrono::seconds(60)));
        CHECK(reading.finished() == std::string());
        CHECK_FALSE(reading.progress().resumed);
        CHECK(reading.progress().lines == static_cast<uint64_t>(lines_v2));
    }
    found = evaluatedPositions(folder);
    REQUIRE(found.size() == 3);
    for (const auto& f : found)
        if (evalKey(f.position.epd) == evalKey(AFTER_E4)) CHECK(f.evaluation.cp == 40);

    // A file downloaded before: read from the disk, the same positions.
    fs::remove_all(folder);
    const auto file = fs::temp_directory_path() / "lichess_db_eval.jsonl.zst";
    {
        std::ofstream(file, std::ios::binary) << contents[0];
        JobGroup jobs;
        EvalsReading reading(jobs);
        reading.start(file.string(), folder);
        REQUIRE(waitUntil([&] { return !reading.busy(); }, std::chrono::seconds(60)));
        CHECK(reading.finished() == std::string());
        CHECK(reading.progress().lines == static_cast<uint64_t>(lines_v1));
    }
    CHECK(evaluatedPositions(folder).size() == 3);
    CHECK(evalsFolder(folder).date.empty());  // a file's date is not the database's
    // A file cut short: said, and where it stopped kept.
    fs::remove_all(folder);
    {
        std::ofstream(file, std::ios::binary | std::ios::trunc) << contents[0].substr(0, contents[0].size() - 100);
        JobGroup jobs;
        EvalsReading reading(jobs);
        reading.start(file.string(), folder);
        REQUIRE(waitUntil([&] { return !reading.busy(); }, std::chrono::seconds(60)));
        const auto why = reading.finished();
        REQUIRE(why);
        CHECK(why->find("cut short") != std::string::npos);
    }
    CHECK(evalsFolder(folder).stopped_at > 0);
    (void)readAll;
}
