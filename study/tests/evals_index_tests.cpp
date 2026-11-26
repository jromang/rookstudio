// Lichess's evaluations kept offline (#58): the database's lines read, a FEN's key, the index made in runs then
// merged, its lookups, a preparation cut short and gone on with, and what a crash leaves.
#include <doctest/doctest.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <chess.hpp>

#include <study/evals_index.hpp>

#include "test_support.hpp"

using namespace fcg::study;
namespace fs = std::filesystem;
using fcg::test::readFile;

namespace {

// The four fields Lichess writes: pieces, side to move, castling rights, en passant square.
std::string fen4(const chess::Board& board) {
    const auto fen = board.getFen();
    std::size_t at = 0;
    for (int field = 0; field < 4; ++field) at = fen.find(' ', at + (field ? 1 : 0));
    return fen.substr(0, at);
}

EvalsPosition positionOf(const std::string& fen) {
    EvalsPosition p;
    REQUIRE(evalsPosition(fen, p));
    return p;
}

// The positions of random games from the start, each once, in the order met.
std::vector<std::string> positions(std::size_t n, uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<std::string> out;
    std::set<std::string> seen;
    while (out.size() < n) {
        chess::Board board;
        for (int ply = 0; ply < 80 && out.size() < n; ++ply) {
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, board);
            if (moves.empty()) break;
            board.makeMove(moves[static_cast<int>(rng() % static_cast<uint32_t>(moves.size()))]);
            if (seen.insert(fen4(board)).second) out.push_back(fen4(board));
        }
    }
    return out;
}

struct Expected {
    int cp = 0, mate = 0, depth = 0;
    std::string move;
};

// A line as Lichess writes it: searches of one or more lines each, White's scores; the deepest one is `e`.
std::string dumpLine(const std::string& fen, const Expected& e, uint32_t salt) {
    const auto score = [](int cp, int mate) {
        return mate ? fmt::format("\"mate\":{}", mate) : fmt::format("\"cp\":{}", cp);
    };
    std::string searches;
    // A shallower search first, of two lines, on some lines.
    if (salt % 3 == 0)
        searches +=
            fmt::format(R"({{"pvs":[{{{},"line":"a2a3 a7a6"}},{{"cp":{},"line":"h2h3"}}],"knodes":{},"depth":{}}},)",
                        score(e.cp + 7, 0), e.cp - 30, 100 + salt % 50, std::max(1, e.depth - 5));
    searches += fmt::format(R"({{"pvs":[{{{},"line":"{} e7e5 g1f3"}}],"knodes":{},"depth":{}}})", score(e.cp, e.mate),
                            e.move, 5000 + salt % 1000, e.depth);
    return fmt::format(R"({{"fen":"{}","evals":[{}]}})", fen, searches);
}

// The expected evaluation of the i-th position: scores, mates, depths and moves of every kind.
Expected expected(std::size_t i) {
    Expected e;
    e.depth = 12 + static_cast<int>(i * 7 % 60);
    if (i % 11 == 0)
        e.mate = (i % 2 ? -1 : 1) * static_cast<int>(1 + i % 40);
    else
        e.cp = static_cast<int>(i * 37 % 1200) - 600;
    static const char* MOVES[] = {"e2e4", "g8f6", "e7e8q", "a2a1n", "e1g1", "h7h5"};
    e.move                     = MOVES[i % 6];
    return e;
}

void checkFound(const std::optional<OfflineEval>& got, const Expected& e) {
    REQUIRE(got.has_value());
    CHECK(got->depth == e.depth);
    CHECK(got->cp == e.cp);
    CHECK(got->mate == e.mate);
    CHECK(got->best == e.move);
}

// The database read through a builder as the reading does: frames of `frame` lines, a mark at each frame's end, a
// commit and a saved state every `every` frames. Lines from `from`; the state saved last is returned (null if none).
nlohmann::json feed(EvalsBuilder& builder, const std::vector<std::string>& lines, std::size_t from, std::size_t to,
                    std::size_t frame, std::size_t every, std::size_t* saved_at = nullptr) {
    nlohmann::json saved;
    std::size_t frames = 0;
    for (std::size_t i = from; i < to; ++i) {
        builder.line(lines[i]);
        if ((i + 1) % frame == 0) {
            const auto state = builder.mark();
            if (++frames % every == 0) {
                builder.commit();
                saved = state;
                if (saved_at) *saved_at = i + 1;
            }
        }
    }
    return saved;
}

struct Dump {
    std::vector<std::string> fens, lines;
    std::vector<Expected> evals;
};

Dump makeDump(std::size_t n, uint32_t seed) {
    Dump d;
    d.fens = positions(n, seed);
    for (std::size_t i = 0; i < d.fens.size(); ++i) {
        d.evals.push_back(expected(i));
        d.lines.push_back(dumpLine(d.fens[i], d.evals.back(), static_cast<uint32_t>(i)));
    }
    return d;
}

}  // namespace

TEST_CASE("offline evaluations: a line of the database, its deepest search's first line") {
    const std::string line =
        R"({"fen":"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -","evals":[{"pvs":[{"cp":31,"line":"c7c5 g1f3"},)"
        R"({"cp":40,"line":"e7e5 g1f3"}],"knodes":4189,"depth":41},{"pvs":[{"cp":27,"line":"c7c5 g1f3"}],"knodes":9000,)"
        R"("depth":52},{"pvs":[{"cp":20,"line":"e7e6"}],"knodes":8000,"depth":52}]})";
    EvalsLine l;
    std::vector<int> depths;
    REQUIRE(readEvalsLine(line, l, &depths));
    CHECK_FALSE(l.slow);
    CHECK(l.fen == "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -");
    CHECK(l.searches == 3);
    CHECK(depths == std::vector<int>{41, 52, 52});
    CHECK(l.depth == 52);  // of the two as deep, the one of more nodes
    CHECK(l.knodes == 9000);
    CHECK(l.cp == 27);
    CHECK(l.mate == 0);
    CHECK(l.move == "c7c5");
    CHECK(l.lines == 1);
    // A mate, White's: negative when Black mates.
    REQUIRE(readEvalsLine(R"({"fen":"6k1/6p1/8/4K3/4NN2/8/8/8 w - -","evals":[{"pvs":[{"mate":-15,"line":"e5e6"}],)"
                          R"("knodes":589893,"depth":95}]})",
                          l));
    CHECK(l.mate == -15);
    CHECK(l.cp == 0);
    CHECK(l.move == "e5e6");
    // Its fields in another order, spaces: read as JSON, the same.
    REQUIRE(readEvalsLine(R"({ "evals": [ {"depth": 33, "knodes": 12, "pvs": [ {"line": "d2d4 d7d5", "cp": -14} ]} ],)"
                          R"( "fen": "8/8/8/8/8/8/1k6/K7 w - -" })",
                          l));
    CHECK(l.slow);
    CHECK(l.fen == "8/8/8/8/8/8/1k6/K7 w - -");
    CHECK(l.depth == 33);
    CHECK(l.cp == -14);
    CHECK(l.move == "d2d4");
    // Not a line of the database, or without a score.
    CHECK_FALSE(readEvalsLine("not json", l));
    CHECK_FALSE(readEvalsLine(R"({"fen":"8/8/8/8/8/8/1k6/K7 w - -","evals":[]})", l));
    CHECK_FALSE(readEvalsLine(R"({"fen":"8/8/8/8/8/8/1k6/K7 w - -","evals":[{"pvs":[],"knodes":1,"depth":9}]})", l));
}

TEST_CASE("offline evaluations: a FEN's position is the one the board gives, without en passant") {
    // Every position of random games: the FEN's and the one of the board that played to it.
    std::mt19937 rng(5);
    for (int game = 0; game < 60; ++game) {
        chess::Board board;
        for (int ply = 0; ply < 80; ++ply) {
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, board);
            if (moves.empty()) break;
            board.makeMove(moves[static_cast<int>(rng() % static_cast<uint32_t>(moves.size()))]);
            CAPTURE(board.getFen());
            CHECK(positionOf(fen4(board)) == evalsPosition(board));
            chess::Board read;
            REQUIRE(read.setFen(board.getFen()));
            CHECK(evalsPosition(read) == evalsPosition(board));
            CHECK(evalsMen(evalsPosition(board)) == static_cast<int>(board.occ().count()));
        }
    }
    // A pawn that went two squares: chess.hpp keeps d6 after the move, not from the FEN (d7-d5 uncovers the c8
    // bishop's check, and the capture is not legal: Lichess writes no square); the position is the same either way.
    chess::Board moved;
    REQUIRE(moved.setFen("rnb1k2r/p2pp1b1/5nN1/1pp1PK1p/2P5/4PP1p/PP2Q1PP/R1B2B1R b - - 0 1"));
    moved.makeMove(chess::uci::uciToMove(moved, "d7d5"));
    CHECK(moved.enpassantSq() != chess::Square::NO_SQ);
    CHECK(positionOf("rnb1k2r/p3p1b1/5nN1/1pppPK1p/2P5/4PP1p/PP2Q1PP/R1B2B1R w - d6") == evalsPosition(moved));
    CHECK(positionOf("rnb1k2r/p3p1b1/5nN1/1pppPK1p/2P5/4PP1p/PP2Q1PP/R1B2B1R w - -") == evalsPosition(moved));
    CHECK(positionOf("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6") ==
          positionOf("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq -"));
    // Castling rights in any order; rights the board does not allow (no rook in the corner), or in Shredder's
    // letters: as the board reads them.
    CHECK(positionOf("r3k2r/8/8/8/8/8/8/R3K2R w qkQK -") == positionOf("r3k2r/8/8/8/8/8/8/R3K2R w KQkq -"));
    const auto same = [](const std::string& f) {
        chess::Board board;
        REQUIRE(board.setFen(f));
        return positionOf(f) == evalsPosition(board);
    };
    CHECK(same("r3k3/8/8/8/8/8/8/4K2R w Kq -"));
    CHECK(same("4k3/8/8/8/8/8/8/R3K3 w K -"));
    CHECK(same("r3k2r/8/8/8/8/8/8/R3K2R b HAha -"));
    CHECK(positionOf("4k3/8/8/8/8/8/8/R3K3 w K -") == positionOf("4k3/8/8/8/8/8/8/R3K3 w - -"));
    CHECK_FALSE(positionOf("r3k2r/8/8/8/8/8/8/R3K2R w KQkq -") == positionOf("r3k2r/8/8/8/8/8/8/R3K2R w Kkq -"));
    CHECK_FALSE(positionOf("r3k2r/8/8/8/8/8/8/R3K2R w KQkq -") == positionOf("r3k2r/8/8/8/8/8/8/R3K2R b KQkq -"));
    CHECK_FALSE(positionOf("8/8/8/8/8/8/1k6/K7 w - -") == positionOf("8/8/8/8/8/8/1K6/k7 w - -"));
    // More men than a game can have, as a board set up by hand may hold (Lichess's file has some): read too.
    const auto full = positionOf("qqqqkqqq/pppppppp/pppppppp/pppppppp/PPPPPPPP/PPPPPPPP/PPPPPPPP/QQQQKQQQ w - -");
    CHECK(evalsMen(full) == 64);
    CHECK(full.size == 41);
    // Not a position.
    EvalsPosition p;
    CHECK_FALSE(evalsPosition("", p));
    CHECK_FALSE(evalsPosition("8/8/8 w - -", p));
    CHECK_FALSE(evalsPosition("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq -", p));
    CHECK_FALSE(evalsPosition("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNZ w KQkq -", p));
    CHECK(evalsMen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -") == 32);
    CHECK(evalsMen("8/8/8/8/8/8/1k6/K7 w - -") == 2);
}

TEST_CASE("offline evaluations: keys under a secret, which positions made to share Polyglot's keys do not share") {
    // SipHash-2-4's own example: the key 00 01 … 0f, the message 00 01 … 0e.
    uint8_t message[15];
    for (int i = 0; i < 15; ++i) message[i] = static_cast<uint8_t>(i);
    CHECK(sipHash24(message, 15, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL) == 0xa129ca6149be45e5ULL);
    CHECK(sipHash24(message, 0, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL) == 0x726fdb47dd0e0e31ULL);
    // Two of the positions Lichess's database holds with the start position's Polyglot key: theirs are alike, ours
    // are not.
    const std::string start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    for (const std::string made : {"q2k1q1r/5R2/p1bp1n2/R1p2P2/7r/np2br2/5Pn1/1B2N1K1 w - -",
                                   "1K2Q3/1B1qR2p/rPR1B1PR/P2p2pr/5R2/Nrp1P3/3b4/N3k3 w - -"}) {
        chess::Board a, b;
        REQUIRE(a.setFen(start));
        REQUIRE(b.setFen(made));
        CHECK(a.hash() == b.hash());
        const auto secret = newEvalsSecret();
        CHECK(evalsKey(positionOf(start), secret) != evalsKey(positionOf(made), secret));
    }
    // A secret drawn each time; a key the same for the same position and secret.
    const auto s1 = newEvalsSecret(), s2 = newEvalsSecret();
    CHECK_FALSE(s1 == s2);
    CHECK(evalsKey(positionOf(start), s1) == evalsKey(positionOf(start), s1));
    CHECK(evalsKey(positionOf(start), s1) != evalsKey(positionOf(start), s2));
}

TEST_CASE("offline evaluations: an evaluation in five bytes") {
    const auto back = [](int cp, int mate, int depth, std::string move) {
        return unpackEval(packEval(cp, mate, depth, move));
    };
    CHECK(back(31, 0, 46, "e7e6") == OfflineEval{31, 0, 46, "e7e6"});
    CHECK(back(-30000, 0, 255, "a7a8q") == OfflineEval{-30000, 0, 255, "a7a8q"});
    CHECK(back(99999, 0, 300, "h2h1n") == OfflineEval{30000, 0, 255, "h2h1n"});  // clamped
    CHECK(back(0, 7, 20, "e1g1") == OfflineEval{0, 7, 20, "e1g1"});
    CHECK(back(0, -1, 20, "e8c8") == OfflineEval{0, -1, 20, "e8c8"});
    CHECK(back(0, 0, 9, "") == OfflineEval{0, 0, 9, ""});
    CHECK(back(5, 0, 9, "z9z9") == OfflineEval{5, 0, 9, ""});  // not a move: none kept
    CHECK(packedDepth(packEval(0, 0, 77, "e2e4")) == 77);
}

TEST_CASE("offline evaluations: made in runs then merged; each position found, the others not") {
    const auto dump   = makeDump(2500, 11);
    const auto folder = fs::temp_directory_path() / "evals-made";
    fs::remove_all(folder);
    EvalsBuilder::Options options;
    options.run_entries = 400;  // runs of a few hundred positions: several, merged
    EvalsBuilder builder(folder / "build", options);
    builder.startAgain();
    auto lines = dump.lines;
    // A position met twice, deeper the second time: the deeper kept, wherever the two land.
    lines.push_back(dumpLine(dump.fens[3], {12, 0, 99, "d2d4"}, 1));
    lines.push_back(dumpLine(dump.fens[4], {12, 0, 1, "d2d4"}, 1));  // shallower: not kept
    lines.push_back("not a line of the database");
    (void)feed(builder, lines, 0, lines.size(), 50, 4);
    CHECK(builder.runs().size() >= 5);
    CHECK(builder.lines() == lines.size());
    CHECK(builder.unread() == 1);

    // The runs answer while it prepares.
    {
        std::vector<std::unique_ptr<EvalsRun>> runs;
        for (const auto& f : builder.runs()) runs.push_back(std::make_unique<EvalsRun>(f));
        const EvalsLookup partial(nullptr, std::move(runs));
        CHECK_FALSE(partial.empty());
        CHECK(partial.index() == nullptr);
        checkFound(partial.find(positionOf(dump.fens[10])), dump.evals[10]);
    }

    const auto index = nextEvalsIndex(folder);
    CHECK(index.filename() == "index-1.bin");
    std::atomic<uint64_t> merged{0}, total{0};
    REQUIRE(builder.finish(index, "2026-11-01", &merged, &total) == "");
    CHECK(total.load() >= dump.fens.size());
    CHECK(merged.load() == dump.fens.size());
    CHECK(builder.runs().empty());
    CHECK(fs::directory_iterator(folder / "build") == fs::directory_iterator());  // its runs and logs removed

    const EvalsIndex opened(index);
    CHECK(opened.positions() == dump.fens.size());
    CHECK(opened.lines() == lines.size());
    CHECK(opened.date() == "2026-11-01");
    CHECK(opened.minMen() == 0);
    CHECK(opened.bytes() == fs::file_size(index));
    for (std::size_t i = 0; i < dump.fens.size(); ++i) {
        if (i == 3) continue;
        CAPTURE(dump.fens[i]);
        checkFound(opened.find(positionOf(dump.fens[i])), dump.evals[i]);
    }
    checkFound(opened.find(positionOf(dump.fens[3])), {12, 0, 99, "d2d4"});
    // Positions it has not: none found.
    for (const auto& fen : positions(300, 12345)) {
        if (std::find(dump.fens.begin(), dump.fens.end(), fen) != dump.fens.end()) continue;
        CHECK_FALSE(opened.find(positionOf(fen)).has_value());
    }
    // Through the lookup, as the program asks.
    const EvalsLookup lookup(std::make_unique<EvalsIndex>(index), {});
    CHECK(lookup.positions() == dump.fens.size());
    checkFound(lookup.find(positionOf(dump.fens[0])), dump.evals[0]);
    CHECK(newestEvalsIndex(folder) == index);

    // The openings only: the positions with at least 24 men.
    EvalsBuilder::Options openings;
    openings.min_men     = EVALS_OPENING_MEN;
    openings.run_entries = 300;
    EvalsBuilder builder2(folder / "build2", openings);
    builder2.startAgain();
    (void)feed(builder2, dump.lines, 0, dump.lines.size(), 40, 3);
    const auto index2 = folder / "openings.bin";
    REQUIRE(builder2.finish(index2, "", nullptr, nullptr) == "");
    const EvalsIndex only(index2);
    CHECK(only.minMen() == EVALS_OPENING_MEN);
    std::size_t kept = 0;
    for (std::size_t i = 0; i < dump.fens.size(); ++i) {
        const bool opening = evalsMen(dump.fens[i]) >= EVALS_OPENING_MEN;
        kept += opening ? 1 : 0;
        CHECK(only.find(positionOf(dump.fens[i])).has_value() == opening);
    }
    CHECK(only.positions() == kept);
    CHECK(kept > 0);
    CHECK(kept < dump.fens.size());
}

TEST_CASE("offline evaluations: a preparation cut short goes on from its state, as if it had never stopped") {
    const auto dump   = makeDump(3000, 21);
    const auto folder = fs::temp_directory_path() / "evals-resumed";
    fs::remove_all(folder);
    EvalsBuilder::Options options;
    options.run_entries = 350;
    options.secret      = EvalsSecret{0x1234, 0x5678};  // the same for every build here, to compare their files

    // In one go: the index to compare with.
    const auto whole = folder / "whole.bin";
    {
        EvalsBuilder builder(folder / "one", options);
        builder.startAgain();
        (void)feed(builder, dump.lines, 0, dump.lines.size(), 25, 2);
        REQUIRE(builder.finish(whole, "", nullptr, nullptr) == "");
    }

    // Cut where the program is killed: after a state saved, more lines read, a run written and the log grown since;
    // nothing committed after. Then from the state saved, the rest: the same index, byte for byte.
    for (const std::size_t cut : {712u, 1555u, 2990u}) {
        CAPTURE(cut);
        const auto build = folder / fmt::format("cut-{}", cut);
        nlohmann::json saved;
        std::size_t saved_at = 0;
        {
            EvalsBuilder builder(build, options);
            builder.startAgain();
            saved = feed(builder, dump.lines, 0, cut, 25, 2, &saved_at);
            REQUIRE_FALSE(saved.is_null());
            REQUIRE(saved_at < cut);
            // A run cut after the state saved, and lines after it: on the disk, named by no saved state.
            (void)builder.mark();
            builder.commit();  // its log written, the state of this mark never saved (the program killed)
        }
        // A run being written when it was killed.
        std::ofstream(build / "run-999.bin.tmp", std::ios::binary) << "half a run";
        {
            EvalsBuilder builder(build, options);
            builder.restore(saved);
            REQUIRE(builder.error() == "");
            CHECK(builder.lines() == saved_at);
            CHECK_FALSE(fs::exists(build / "run-999.bin.tmp"));
            (void)feed(builder, dump.lines, saved_at, dump.lines.size(), 25, 2);
            const auto index = folder / fmt::format("cut-{}.bin", cut);
            REQUIRE(builder.finish(index, "", nullptr, nullptr) == "");
            CHECK(readFile(index) == readFile(whole));
        }
    }

    // A state naming a run that is gone: said, not read as if it were whole.
    {
        const auto build = folder / "lost";
        nlohmann::json saved;
        {
            EvalsBuilder builder(build, options);
            builder.startAgain();
            saved = feed(builder, dump.lines, 0, 1500, 25, 2);
        }
        REQUIRE_FALSE(saved["runs"].empty());
        fs::remove(build / fmt::format("run-{}.bin", saved["runs"][0].get<uint64_t>()));
        EvalsBuilder builder(build, options);
        builder.restore(saved);
        CHECK(builder.error().find("missing") != std::string::npos);
    }
}

TEST_CASE("offline evaluations: what a crash leaves is never read as an index") {
    const auto dump   = makeDump(600, 31);
    const auto folder = fs::temp_directory_path() / "evals-crash";
    fs::remove_all(folder);
    EvalsBuilder::Options options;
    options.run_entries = 200;
    {
        EvalsBuilder builder(folder / "build", options);
        builder.startAgain();
        (void)feed(builder, dump.lines, 0, dump.lines.size(), 30, 1);
        REQUIRE(builder.finish(nextEvalsIndex(folder), "", nullptr, nullptr) == "");
    }
    const auto first = folder / "index-1.bin";
    REQUIRE(fs::exists(first));
    const auto bytes = readFile(first);

    // An index being written when the program stopped (the next one's .tmp): not the newest, removed by the tidying.
    std::ofstream(folder / "index-2.bin.tmp", std::ios::binary) << bytes.substr(0, bytes.size() / 2);
    CHECK(newestEvalsIndex(folder) == first);
    CHECK(nextEvalsIndex(folder).filename() == "index-3.bin");
    tidyEvalsFolder(folder);
    CHECK_FALSE(fs::exists(folder / "index-2.bin.tmp"));
    CHECK(fs::exists(first));

    // A file cut short, or another file: refused, whatever its name says.
    const auto cut = folder / "cut.bin";
    std::ofstream(cut, std::ios::binary) << bytes.substr(0, bytes.size() - 1);
    CHECK_THROWS_AS(EvalsIndex{cut}, std::runtime_error);
    std::ofstream(cut, std::ios::binary | std::ios::trunc) << "RSEVALS1 but nothing else";
    CHECK_THROWS_AS(EvalsIndex{cut}, std::runtime_error);
    std::ofstream(cut, std::ios::binary | std::ios::trunc) << "";
    CHECK_THROWS_AS(EvalsIndex{cut}, std::runtime_error);
    CHECK_THROWS_AS(EvalsRun{cut}, std::runtime_error);
    fs::remove(cut);

    // A newer index replaces it: the older one removed by the tidying, the newer answering.
    {
        EvalsBuilder builder(folder / "build", options);
        builder.startAgain();
        auto changed = dump.lines;
        changed[0]   = dumpLine(dump.fens[0], {42, 0, 50, "g1f3"}, 0);
        (void)feed(builder, changed, 0, changed.size(), 30, 1);
        REQUIRE(builder.finish(nextEvalsIndex(folder), "2026-12-01", nullptr, nullptr) == "");
    }
    const auto newest = newestEvalsIndex(folder);
    CHECK(newest.filename() == "index-2.bin");
    tidyEvalsFolder(folder);
    CHECK_FALSE(fs::exists(first));
    const EvalsIndex opened(newest);
    CHECK(opened.date() == "2026-12-01");
    checkFound(opened.find(positionOf(dump.fens[0])), {42, 0, 50, "g1f3"});
}
