// The opening report (#91) behind its page: its structures named by the assistant's formations and its pawn plans by
// the assistant's facts, counted apart over games of our own (chess.hpp's boards, commentary's facts); the summary's
// figures as the page shows them; the summary written in the assistant's child process (a fake model whose answers the
// checker refuses, the templates' standing); the main moves added to the repertoire and taken out; Lichess's answers
// kept for offline. Players' names are invented.
//
// By hand, the summary written by the real model on the processor, timed (not in the CI: it runs only when its files
// are given):
//
//     FCG_REPORT_MODEL=commentary-v11-Q4_K_M.gguf FCG_REPORT_PROGRAM=build/rookstudio FCG_REPORT_DB=Name.fcgdb \
//     FCG_REPORT_LINE="d4 d5 c4 e6" [FCG_REPORT_ROUNDS=2] [FCG_REPORT_LANG=en|fr] \
//     build/fastchess-gui-tests -tc='opening summary by the model: measured*'
#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>

#include <commentary/formation.hpp>
#include <commentary/opening_summary.hpp>
#include <commentary/position.hpp>
#include <study/import.hpp>
#include <study/store.hpp>
#include <study/tree.hpp>

#include "assistant_process.hpp"
#include "opening_report.hpp"
#include "repertoires.hpp"
#include "self_path.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

const char* const LINE = "d4 d5 c4 e6 Nc3 Nf6 cxd5 exd5 Bg5 c6 e3 Be7 Bd3 Nbd7 Qc2 O-O Nf3 Re8 O-O Nf8";

std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

// A game through the position: each side's plan played when legal, quiet random moves between.
std::vector<std::string> gameMoves(uint32_t seed, const std::vector<std::string>& white_plan,
                                   const std::vector<std::string>& black_plan, int plies) {
    chess::Board board;
    std::vector<std::string> moves;
    for (const auto& san : words(LINE)) {
        board.makeMove(chess::uci::parseSan(board, san));
        moves.push_back(san);
    }
    std::mt19937 random(seed);
    std::size_t wi = 0, bi = 0;
    while (static_cast<int>(moves.size()) < plies) {
        chess::Movelist list;
        chess::movegen::legalmoves(list, board);
        if (list.empty()) break;
        const bool white = board.sideToMove() == chess::Color::WHITE;
        auto& at         = white ? wi : bi;
        const auto& plan = white ? white_plan : black_plan;
        std::optional<chess::Move> chosen;
        if (at < plan.size() && random() % 4 != 0) {
            chess::Move planned = chess::Move::NO_MOVE;
            try {
                planned = chess::uci::parseSan(board, plan[at]);
            } catch (...) {
            }
            ++at;
            if (planned != chess::Move::NO_MOVE && std::find(list.begin(), list.end(), planned) != list.end())
                chosen = planned;
        }
        if (!chosen) {
            std::vector<chess::Move> quiet;
            for (const auto& m : list)
                if (!board.isCapture(m) && m.typeOf() != chess::Move::CASTLING) quiet.push_back(m);
            if (quiet.empty()) break;
            std::sort(quiet.begin(), quiet.end(), [](const chess::Move& a, const chess::Move& b) {
                return chess::uci::moveToUci(a) < chess::uci::moveToUci(b);
            });
            chosen = quiet[random() % quiet.size()];
        }
        moves.push_back(chess::uci::moveToSan(board, *chosen));
        board.makeMove(*chosen);
    }
    return moves;
}

struct Made {
    std::shared_ptr<study::Database> db;
    std::shared_ptr<const study::Tree> tree;
    std::vector<std::vector<std::string>> lines;
};

Made planDatabase(const std::string& name, std::size_t count) {
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    Made m;
    m.db = study::Database::create(dir);
    std::vector<study::ImportedGame> games;
    const char results[] = {'1', '=', '0', '='};
    for (std::size_t i = 0; i < count; ++i) {
        std::vector<std::string> white, black;
        if (i % 2 == 0) white = {"Rab1", "b4", "b5"};
        if (i % 3 == 0) black = {"Ng6", "Ne4"};
        m.lines.push_back(gameMoves(static_cast<uint32_t>(500 + i), white, black, 70));
        const char r             = results[i % 4];
        const std::string result = r == '1' ? "1-0" : r == '0' ? "0-1" : "1/2-1/2";
        std::string text         = fmt::format(
            "[White \"Ansel, Pia\"]\n[Black \"Brandt, Ole\"]\n[Date \"{}.05.01\"]\n"
            "[WhiteElo \"{}\"]\n[BlackElo \"{}\"]\n[Result \"{}\"]\n\n",
            2010 + i % 15, 2300 + (i * 31) % 400, 2290 + (i * 17) % 400, result);
        for (std::size_t k = 0; k < m.lines.back().size(); ++k)
            text += (k % 2 == 0 ? std::to_string(k / 2 + 1) + ". " : std::string()) + m.lines.back()[k] + " ";
        text += result + "\n";
        study::ImportedGame g;
        REQUIRE(study::readImportedGame(text, g));
        games.push_back(std::move(g));
    }
    m.db->append(std::move(games), true);
    study::TreeOptions options;
    options.threads = 2;
    study::TreeProgress progress;
    std::atomic<bool> cancel{false};
    REQUIRE(study::buildTree(*m.db, *m.db->snapshot(), options, progress, cancel).error.empty());
    m.tree = study::Tree::open(dir);
    REQUIRE(m.tree);
    return m;
}

chess::Board boardOf(const std::string& line) {
    chess::Board board;
    for (const auto& san : words(line)) board.makeMove(chess::uci::parseSan(board, san));
    return board;
}

study::OpeningReport reportOf(const Made& m, const std::vector<std::string>& users = {}) {
    std::vector<ReportBase> bases{{"Club", m.db->folder(), m.db->snapshot(), m.tree}};
    study::ReportProgress progress;
    std::atomic<bool> cancel{false};
    ReportShared shared;
    makeReport(boardOf(LINE).getFen(), bases, users, 2, progress, cancel, shared);
    REQUIRE(shared.report);
    CHECK(shared.serial == 3);  // three sections published
    return *shared.report;
}

commentary::Board pawnsOnly(const chess::Board& b) {
    auto board = commentary::Board::empty();
    for (int sq = 0; sq < 64; ++sq) {
        const auto p = b.at(chess::Square(sq));
        if (p.type() != chess::PieceType::PAWN) continue;
        board.setPieceAt(commentary::square(static_cast<uint32_t>(sq)),
                         {p.color() == chess::Color::WHITE ? commentary::Color::White : commentary::Color::Black,
                          commentary::Role::Pawn});
    }
    return board;
}

}  // namespace

TEST_CASE("opening report: the structures and plans named by the assistant's facts, counted apart") {
    const auto m      = planDatabase("fcg-report-plans", 160);
    const auto report = reportOf(m);
    REQUIRE(report.made == study::ReportSection::Done);
    const auto& shapes = report.shapes;
    REQUIRE(shapes.enough);
    CHECK(shapes.replayed == 160);
    // The structures twenty plies on, named by the assistant's formations (the first one each game has), counted apart.
    std::map<std::pair<int, int>, uint64_t> counted;
    uint64_t reached = 0;
    for (const auto& line : m.lines) {
        if (line.size() < 40) continue;
        ++reached;
        std::string first40;
        for (std::size_t k = 0; k < 40; ++k) first40 += line[k] + " ";
        const auto formations = commentary::formations(pawnsOnly(boardOf(first40)));
        if (formations.empty())
            ++counted[{-1, 0}];
        else
            ++counted[{static_cast<int>(formations.front().first),
                       formations.front().second == commentary::Color::White ? 0 : 1}];
    }
    CHECK(shapes.counted == reached);
    REQUIRE(shapes.structures.size() == counted.size());
    for (const auto& s : shapes.structures) {
        CAPTURE(s.kind);
        CHECK(s.stats.games == counted[{s.kind, s.kind < 0 ? 0 : s.owner}]);
    }
    // The position is a Carlsbad, White's: kept where the games keep it.
    const auto carlsbad = std::find_if(shapes.structures.begin(), shapes.structures.end(), [](const auto& s) {
        return s.kind == static_cast<int>(commentary::Formation::Carlsbad);
    });
    REQUIRE(carlsbad != shapes.structures.end());
    CHECK(carlsbad->kept);
    CHECK(carlsbad->owner == 0);

    // White's b-pawn plan, named the minority attack: the name more than half of its games' pushes get from the facts.
    const auto plan = std::find_if(shapes.plans.begin(), shapes.plans.end(), [](const study::ReportPlan& p) {
        return p.side == 0 && p.kind != study::ReportPlan::Kind::Route && !p.squares.empty() && p.squares[0] == 25;
    });
    REQUIRE(plan != shapes.plans.end());
    CHECK(plan->named == static_cast<int>(commentary::PlanName::MinorityAttack));
    std::size_t named = 0, looked = 0;
    for (const auto& g : plan->games) {
        const auto& line = m.lines[g.id];
        auto board       = boardOf(LINE);
        for (std::size_t k = g.ply; k < line.size() && k < g.ply + 30; ++k) {
            const auto move = chess::uci::parseSan(board, line[k]);
            if (board.at(move.from()).type() == chess::PieceType::PAWN && move.to().index() == 25) {
                const auto pos = commentary::Position::fromFen(board.getFen());
                REQUIRE(pos);
                const auto mv = pos->parseUci(chess::uci::moveToUci(move));
                REQUIRE(mv);
                ++looked;
                named += commentary::planOf(*pos, *mv) == commentary::PlanName::MinorityAttack;
                break;
            }
            board.makeMove(move);
        }
    }
    CHECK(looked == plan->games.size());
    CHECK(named * 2 > looked);

    // The summary's figures as the page shows them: the moves numbered in the reader's letters, shares and scores.
    const auto board = boardOf(LINE);
    const auto rows  = reportRows(board, report.moves, {"Club"}, {});
    REQUIRE_FALSE(rows.empty());
    std::vector<std::optional<ReportEval>> evals(rows.size());
    evals[0] = ReportEval{31, std::nullopt, 40, false};
    ReportView view;
    view.board   = board;
    view.opening = "D36 Queen's Gambit Declined: Exchange Variation";
    view.report  = &report;
    view.rows    = &rows;
    view.evals   = &evals;
    {
        chess::Board walk;
        for (const auto& san : words(LINE)) {
            const auto mv = chess::uci::parseSan(walk, san);
            view.path.push_back(mv);
            walk.makeMove(mv);
        }
    }
    const auto en = summaryFigures(view, commentary::Lang::En);
    CHECK(en.after == "10…Nf8");
    CHECK(en.mover == commentary::Color::White);
    uint64_t total = 0;
    for (const auto& r : rows) total += r.games;
    CHECK(en.games == total);
    REQUIRE(en.moves.size() == rows.size());
    CHECK(en.moves[0].san == "11." + chess::uci::moveToSan(board, rows[0].move));
    CHECK(en.moves[0].cp == 31);
    CHECK(en.moves[0].share == static_cast<int>(std::lround(100.0 * rows[0].games / total)));
    CHECK(en.moves[0].score == rows[0].score);
    const auto fr = summaryFigures(view, commentary::Lang::Fr);
    CHECK(fr.after == "10…Cf8");
    // Its plans: their shares of the games replayed, their side's score.
    REQUIRE(en.plans.size() == shapes.plans.size());
    for (std::size_t i = 0; i < en.plans.size(); ++i) {
        const auto& p = shapes.plans[i];
        CHECK(en.plans[i].share == static_cast<int>(std::lround(100.0 * p.stats.games / shapes.replayed)));
        const auto decided = p.stats.white + p.stats.draws + p.stats.black;
        if (decided) {
            const double own = p.side == 0 ? p.stats.white : p.stats.black;
            CHECK(en.plans[i].score == static_cast<int>(std::lround(100.0 * (own + p.stats.draws / 2.0) / decided)));
        }
    }
    const auto minority = std::find_if(en.plans.begin(), en.plans.end(),
                                       [](const auto& p) { return p.name == commentary::PlanName::MinorityAttack; });
    REQUIRE(minority != en.plans.end());
    CHECK(minority->squares.front() == "b4");
    // The templates write from them.
    const auto text = commentary::summaryText(commentary::templateSummary(en, commentary::Lang::En));
    CHECK_MESSAGE(text.find("minority attack, b4") != std::string::npos, text);
}

#ifdef FCG_ASSISTANT
TEST_CASE("opening report: the summary written in the assistant's child process, held to the figures") {
    // The fake model answers each request with what it was asked: the checker refuses it, the templates' stands.
    AssistantProcess process;
    std::string error;
    REQUIRE_MESSAGE(
        process.start((test::selfPath().parent_path() / FCG_MOCK_ASSISTANT).string(), "model.gguf", 0, error), error);
    commentary::SummaryFigures f;
    f.after = "10…Nf8";
    f.games = 4812;
    f.moves = {{"11.Rab1", 1902, 40, 55, std::nullopt, std::nullopt, false},
               {"11.h3", 1120, 23, 53, std::nullopt, std::nullopt, false}};
    f.plans = {{commentary::Color::White, commentary::PlanName::MinorityAttack, {"b4", "b5"}, false, "", 47, 55}};
    std::atomic<bool> cancel{false};
    std::atomic<int> done{0};
    const auto written = writeSummary(&process, f, commentary::Lang::En, 0, cancel, done);
    CHECK(written.by_model);
    CHECK(written.error.empty());
    CHECK(done == 2);
    REQUIRE(written.passages.size() == 2);
    for (const auto& p : written.passages) {
        CHECK_FALSE(p.by_model);
        CHECK(p.attempts == 2);
        CHECK(p.text == p.templ);
        REQUIRE(p.refused.size() == 2);
        // Each request went to the child with its grammar, on the processor: greedy first, then sampled.
        CHECK(
            p.refused[0].first.starts_with("2 messages, seed 1, greedy, held to a grammar, on the CPU: The report: "
                                           "the position after 10…Nf8, White to move, 4,812 games."));
        CHECK(p.refused[1].first.starts_with("2 messages, seed 2, sampled, held to a grammar, on the CPU:"));
        CHECK(debug(p.refused[0].second) == "Figure(\"2\")");
    }
    // Without the model: the templates' version, written from the same figures.
    std::atomic<int> none{0};
    const auto templates = writeSummary(nullptr, f, commentary::Lang::En, 0, cancel, none);
    CHECK_FALSE(templates.by_model);
    CHECK(commentary::summaryText(templates.passages) == commentary::summaryText(written.passages));
}
#endif

TEST_CASE("opening report: the main moves to the repertoire, and taken out") {
    const auto profile = fs::temp_directory_path() / "fcg-report-repertoire";
    fs::remove_all(profile);
    fs::create_directories(profile);
    RepertoireShelf reps(profile);
    const auto index = reps.create("Exchange", chess::Color::WHITE);
    std::vector<chess::Move> path;
    chess::Board board;
    for (const auto& san : words(LINE)) {
        const auto mv = chess::uci::parseSan(board, san);
        path.push_back(mv);
        board.makeMove(mv);
    }
    const auto before = reps.entries()[index].lines;
    std::vector<chess::Move> line;
    auto at = board;
    for (const auto& san : words("Rab1 a5 a3 Ne4")) {
        const auto mv = chess::uci::parseSan(at, san);
        line.push_back(mv);
        at.makeMove(mv);
    }
    CHECK(reps.addLine(index, path, line) == 4);  // the line's four moves, the path coming with the first
    CHECK(reps.has(index, path, line[0]));
    auto after_two = path;
    after_two.push_back(line[0]);
    after_two.push_back(line[1]);
    CHECK(reps.has(index, after_two, line[2]));
    CHECK(reps.addLine(index, path, line) == 0);  // there already
    reps.restore(index, before);
    CHECK_FALSE(reps.has(index, path, line[0]));
    // Read again from its file: as it was.
    RepertoireShelf again(profile);
    CHECK_FALSE(again.has(index, path, line[0]));
}

TEST_CASE("opening report: Lichess's answers kept for offline, by the Tree tab's key") {
    const auto profile = fs::temp_directory_path() / "fcg-report-explorer";
    fs::remove_all(profile);
    lichess::Explorer e;
    e.white               = 1200;
    e.draws               = 1500;
    e.black               = 404;
    e.opening             = "Queen's Gambit Declined: Exchange Variation";
    e.moves               = {{"a1b1", "Rab1", 500, 600, 200, 2560, ""}, {"h2h3", "h3", 300, 400, 150, 2540, ""}};
    const std::string key = "0|r1bqrnk1/pp2bppp/2p2n2/3p2B1/3P4/2NBPN2/PPQ2PPP/R4RK1 w - - 0 11|";
    CHECK_FALSE(keptExplorer(profile, key));
    keepExplorer(profile, key, e);
    const auto kept = keptExplorer(profile, key);
    REQUIRE(kept);
    CHECK(kept->first.white == 1200);
    CHECK(kept->first.opening == e.opening);
    REQUIRE(kept->first.moves.size() == 2);
    CHECK(kept->first.moves[0].uci == "a1b1");
    CHECK(kept->first.moves[0].average_rating == 2560);
    CHECK(kept->second > 0);  // when it was asked
    CHECK_FALSE(keptExplorer(profile, key + "x"));
    // Added to the databases' rows: the explorer's mean rating standing for each side's.
    const auto board = boardOf(LINE);
    study::ReportMoves none;
    const auto rows = reportRows(board, none, {}, {{"Masters (Lichess)", {{"a1b1", 500, 600, 200, 2560}}}});
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].games == 1300);
    CHECK(rows[0].elo == 2560);
    CHECK(rows[0].score == static_cast<int>(std::lround(100.0 * (500 + 300.0) / 1300)));
    CHECK(rows[0].code == -1);
}

TEST_CASE("opening summary by the model: measured on the processor") {
    const auto env = [](const char* name) {
        const char* v = std::getenv(name);
        return v ? std::string(v) : std::string();
    };
    if (env("FCG_REPORT_MODEL").empty() || env("FCG_REPORT_DB").empty()) return;  // by hand only
#ifdef FCG_ASSISTANT
    const auto db   = study::Database::openReadOnly(env("FCG_REPORT_DB"));
    const auto tree = study::Tree::open(env("FCG_REPORT_DB"));
    REQUIRE(tree);
    std::vector<ReportBase> bases{{"database", db->folder(), db->snapshot(), tree}};
    const auto board = boardOf(env("FCG_REPORT_LINE"));
    study::ReportProgress progress;
    std::atomic<bool> cancel{false};
    ReportShared shared;
    auto t = std::chrono::steady_clock::now();
    makeReport(board.getFen(), bases, {}, 0, progress, cancel, shared);
    const auto made = std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
    REQUIRE(shared.report);
    const auto rows = reportRows(board, shared.report->moves, {"database"}, {});
    ReportView view;
    view.board = board;
    {
        chess::Board walk;
        for (const auto& san : words(env("FCG_REPORT_LINE"))) {
            const auto mv = chess::uci::parseSan(walk, san);
            view.path.push_back(mv);
            walk.makeMove(mv);
        }
    }
    view.report    = &*shared.report;
    view.rows      = &rows;
    const auto l   = env("FCG_REPORT_LANG") == "fr" ? commentary::Lang::Fr : commentary::Lang::En;
    const auto fig = summaryFigures(view, l);
    AssistantProcess process;
    std::string error;
    t = std::chrono::steady_clock::now();
    REQUIRE_MESSAGE(process.start(env("FCG_REPORT_PROGRAM"), env("FCG_REPORT_MODEL"), 0, error), error);
    const auto loaded = std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
    std::cout << fmt::format("report made in {:.2f} s; model loaded on {} in {:.1f} s\n", made, process.devices(),
                             loaded);
    const int rounds = env("FCG_REPORT_ROUNDS").empty() ? 1 : std::stoi(env("FCG_REPORT_ROUNDS"));
    for (int round = 0; round < rounds; ++round) {
        std::atomic<int> done{0};
        const auto written = writeSummary(&process, fig, l, static_cast<uint32_t>(round), cancel, done);
        int by_model = 0, refused = 0, requests = 0;
        for (const auto& p : written.passages) {
            by_model += p.by_model;
            refused += static_cast<int>(p.refused.size());
            requests += static_cast<int>(p.attempts);
        }
        std::cout << fmt::format(
            "round {}: {} passages in {:.1f} s ({} requests, {:.1f} s each), {} by the model, {} "
            "refused\n",
            round, written.passages.size(), written.seconds, requests, requests ? written.seconds / requests : 0.0,
            by_model, refused);
        for (const auto& p : written.passages) {
            std::cout << "  " << (p.by_model ? "[model] " : "[template] ") << p.text << "\n";
            for (const auto& [text, why] : p.refused) std::cout << "    refused " << debug(why) << ": " << text << "\n";
        }
    }
    CHECK(error.empty());
#endif
}
