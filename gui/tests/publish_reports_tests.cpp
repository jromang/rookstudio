// What RookStudio prints, each from data of our own (#93): a game (its headers, moves, variations, comments, the
// assistant's marked, evaluations, diagrams where asked, one or two columns, A4 or Letter), a repertoire (each line
// under the one it leaves, a diagram at each branching, its coverage), a crosstable (landscape), a player's dossier and
// an opening report: their pages as the PDF writes them, the PDF's pages counted and its fonts embedded (its text read
// back through its fonts' maps is publish_tests.cpp's), their pages of HTML read back.

#include <doctest/doctest.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>

#include <study/crosstable.hpp>
#include <study/import.hpp>
#include <study/pgn_reader.hpp>
#include <study/store.hpp>
#include <study/tree.hpp>
#include <tour/files.hpp>

#include "dossier.hpp"
#include "opening_report.hpp"
#include "publish_doc.hpp"
#include "publish_game.hpp"
#include "publish_pdf.hpp"
#include "publish_reports.hpp"

using namespace fcg;
using namespace fcg::publish;
namespace fs = std::filesystem;

namespace {

// The texts of a document's pages, as the PDF writes them: a line's pieces side by side, a space where they leave one.
std::string pageText(const std::vector<Page>& pages) {
    std::string out;
    for (const auto& p : pages) {
        float y = -1, end = 0;
        for (const auto& t : p.texts) {
            if (!out.empty()) out += t.y != y || t.x > end + 0.5f ? " " : "";
            out += t.text;
            y   = t.y;
            end = t.x + textWidth(t.text, t.font, t.size);
        }
        out += "\n";
    }
    return out;
}

std::size_t diagramsOf(const std::vector<Page>& pages) {
    std::size_t n = 0;
    for (const auto& p : pages) n += p.diagrams.size();
    return n;
}

std::size_t count(const std::string& text, const std::string& what) {
    std::size_t n = 0;
    for (auto at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) ++n;
    return n;
}

DocumentLook look() {
    DocumentLook l;
    static const auto pieces = PieceShapes::make(appearance::builtInPieces("cburnett"));
    l.pieces                 = pieces;
    l.notice                 = piecesNotice("cburnett");
    return l;
}

// The PDF of pages: its page objects counted, its fonts embedded. With FCG_PUBLISH_DUMP set to a folder, written there
// as <title>.pdf, to be compared whole.
void checkPdf(const std::vector<Page>& pages, const std::string& title) {
    const auto pdf = pdfBytes(pages, look().pieces.get(), {title, "en", ""});
    if (const char* dir = std::getenv("FCG_PUBLISH_DUMP"))
        std::ofstream(std::filesystem::path(dir) / (tour::fileNameOf(title) + ".pdf"), std::ios::binary) << pdf;
    CHECK(pdf.rfind("%PDF-1.7", 0) == 0);
    CHECK(count(pdf, "/Type /Page ") == pages.size());
    CHECK(count(pdf, "/FontFile2") + count(pdf, "/FontFile3") >= 1);
    CHECK(count(pdf, "/Subtype /Type0") == count(pdf, "/Type /FontDescriptor"));
}

const char* GAME = R"([Event "Autumn Open 2026"]
[Site "Lyon"]
[Date "2026.10.20"]
[Round "9"]
[White "Moreau, Claire"]
[Black "Lindqvist, Erik"]
[Result "1/2-1/2"]

1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 6. Be3 e5 7. Nb3 Be6 8. f3 Be7 9. Qd2 O-O 10. O-O-O Nbd7
11. g4 b5 12. g5 {[%eval 0.31]} b4 {[%cal Gc3e2] Each side pushes on the other's wing.} 13. Ne2 {[%rs assistant] The
knight goes to e2 to cover g3.} (13. Na4 Qa5) 13... Ne8 14. f4 a5 15. f5 $1 Bc4 16. Kb1 a4 17. Nbc1 $2 a3 18. b3 Bxe2
19. Qxe2 Nc5 20. Qc4 Rc8 21. Qd5 Qc7 1/2-1/2
)";

}  // namespace

TEST_CASE("publish: a game printed: its headers, moves, variations and comments, diagrams where asked") {
    const auto read = study::readPgnGame(GAME);
    REQUIRE(read.tree);
    PrintGame g;
    g.tree  = *read.tree;
    g.white = "Moreau, Claire";
    g.black = "Lindqvist, Erik";
    GamePrintOptions o;
    auto doc   = gameDocument(g, o, look());
    auto pages = layOut(doc);
    REQUIRE(pages.size() == 1);
    auto text = pageText(pages);
    CHECK(text.find("Moreau, Claire – Lindqvist, Erik · ½-½") != std::string::npos);
    CHECK(text.find("Autumn Open 2026, Lyon · round 9") != std::string::npos);
    CHECK(text.find("Nf3") != std::string::npos);
    CHECK(text.find("(13.Na4") != std::string::npos);  // the variation, in brackets
    CHECK(text.find("Each side pushes on the other's wing.") != std::string::npos);
    CHECK(text.find("cover g3") == std::string::npos);  // the assistant's, only when asked
    CHECK(text.find("+0.31") == std::string::npos);
    CHECK(text.find("Cburnett") != std::string::npos);  // the pieces' notice
    // The key moments without a review: the moves marked !! ?? ? ! (15.f5! and 17.Nbc1?).
    CHECK(diagramPlies(g, DiagramsAt::KeyMoments) == std::vector<int>{29, 33});
    CHECK(diagramsOf(pages) == 2);
    CHECK(text.find("after 15.f5") != std::string::npos);
    // The review's mistakes when it has them.
    g.key_plies = {24};
    CHECK(diagramPlies(g, DiagramsAt::KeyMoments) == std::vector<int>{24});
    CHECK(diagramPlies(g, DiagramsAt::MarkedComments) == std::vector<int>{24});  // the arrow of 12…b4
    CHECK(diagramPlies(g, DiagramsAt::EveryTenMoves) == std::vector<int>{20, 40});
    CHECK(diagramPlies(g, DiagramsAt::None).empty());
    // The assistant's comments marked, the evaluations, in one column on Letter, without the headers.
    o.assistant = true;
    o.evals     = true;
    o.columns   = 1;
    o.letter    = true;
    o.headers   = false;
    o.diagrams  = DiagramsAt::EveryTenMoves;
    doc         = gameDocument(g, o, look());
    pages       = layOut(doc);
    text        = pageText(pages);
    CHECK(pages.front().width == doctest::Approx(612));
    CHECK(pages.front().height == doctest::Approx(792));
    CHECK(text.find("Assistant:") != std::string::npos);
    CHECK(text.find("cover g3") != std::string::npos);
    CHECK(text.find("+0.31") != std::string::npos);
    CHECK(text.find("Autumn Open 2026, Lyon") == std::string::npos);
    CHECK(diagramsOf(pages) == 2);
    checkPdf(pages, doc.title);
}

TEST_CASE("publish: a repertoire printed: each line under the one it leaves, a diagram at each branching, its gaps") {
    const auto read = study::readPgnGame(
        "[RepertoireSide \"White\"]\n\n1. e4 c5 (1... e5 2. Nf3 Nc6 (2... d6 3. d4) 3. Bb5) (1... c6 {My Caro notes.} "
        "2. d4 d5) 2. Nf3 d6 3. d4 *\n");
    REQUIRE(read.tree);
    REQUIRE(read.tree->variations.size() == 3);
    study::Coverage cover;
    cover.covered = 0.84;
    study::RepertoireGap gap;
    gap.line  = {"e4", "c5", "Nf3"};
    gap.move  = "Nc6";
    gap.games = 1234;
    gap.reach = 0.12;
    cover.gaps.push_back(gap);
    const auto doc   = repertoireDocument("Open Sicilian", chess::Color::WHITE, *read.tree, cover, false, look());
    const auto pages = layOut(doc);
    const auto text  = pageText(pages);
    CHECK(text.find("Open Sicilian") != std::string::npos);
    CHECK(text.find("4 lines") != std::string::npos);
    CHECK(text.find("The main line") != std::string::npos);
    CHECK(text.find("My Caro notes.") != std::string::npos);  // the user's notes
    // The lines under the ones they leave: after 1.e4 (two lines, one diagram), after 2.Nf3 in the 1…e5 line.
    CHECK(count(text, "After 1.e4") == 2);
    CHECK(text.find("After 2.Nf3") != std::string::npos);
    CHECK(diagramsOf(pages) == 2);
    CHECK(text.find("84 % of the games along the repertoire") != std::string::npos);
    CHECK(text.find("→ Nc6") != std::string::npos);
    CHECK(text.find("1,234") != std::string::npos);  // a count of games as the app writes it
    checkPdf(pages, doc.title);
}

TEST_CASE("publish: a crosstable printed on landscape pages, and as a page of HTML") {
    using namespace study;
    std::vector<CrossPlayer> players(4);
    const char* names[] = {"Ansel, Pia", "Brandt, Ole", "Chen, Mei", "Duarte, Luis"};
    const int elos[]    = {2410, 2380, 2350, 2300};
    for (std::size_t i = 0; i < 4; ++i) {
        players[i].name       = names[i];
        players[i].elo        = elos[i];
        players[i].federation = i % 2 ? "NOR" : "FRA";
    }
    // A round robin of four: each pair once.
    std::vector<CrossGame> games;
    const auto game = [&](uint32_t w, uint32_t b, char result, int round) {
        CrossGame g;
        g.white       = w;
        g.black       = b;
        g.result      = result;
        g.round.round = round;
        g.plies       = 60;
        g.ref         = games.size();
        games.push_back(g);
    };
    game(0, 3, '1', 1);
    game(1, 2, '=', 1);
    game(3, 2, '0', 2);
    game(0, 1, '=', 2);
    game(1, 3, '1', 3);
    game(2, 0, '0', 3);
    CrossOptions options;
    options.year     = 2026;
    const auto table = makeCrosstable(players, games, options);
    REQUIRE(table.rows.size() == 4);
    const auto doc   = crosstableDocument(table, "Club Championship 2026", 6, false, look());
    const auto pages = layOut(doc);
    REQUIRE_FALSE(pages.empty());
    CHECK(pages.front().width > pages.front().height);  // landscape
    const auto text = pageText(pages);
    CHECK(text.find("Club Championship 2026") != std::string::npos);
    CHECK(text.find("round robin") != std::string::npos);
    CHECK(text.find("Ansel, Pia (FRA)") != std::string::npos);
    CHECK(text.find("2½") != std::string::npos);  // Ansel's points
    CHECK(text.find("SB") != std::string::npos);
    CHECK(text.find("Sonneborn-Berger") != std::string::npos);
    CHECK(text.find("Cburnett") == std::string::npos);  // no piece drawn: no pieces' notice
    checkPdf(pages, doc.title);
    const auto html = documentHtml(doc);
    CHECK(html.find("<table>") != std::string::npos);
    CHECK(html.find("Duarte, Luis") != std::string::npos);
    CHECK(html.find("http") == std::string::npos);
    CHECK(html.find("<script") == std::string::npos);  // no line to replay: no board
    CHECK(html.find("Cburnett") == std::string::npos);
}

TEST_CASE("publish: a player's dossier printed, and as a page whose lines replay") {
    // Rui Ferreira's games, under our own names.
    std::vector<study::ImportedGame> games;
    const char* lines[] = {"1. e4 e5 2. Nf3 Nc6 3. Bb5 Nf6 4. O-O Nxe4", "1. d4 d5 2. c4 e6 3. Nc3 Nf6 4. cxd5 exd5",
                           "1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6"};
    for (int i = 0; i < 12; ++i) {
        const bool white  = i % 3 != 2;
        const auto result = i % 4 == 0 ? "1-0" : i % 4 == 1 ? "1/2-1/2" : i % 4 == 2 ? "0-1" : "1-0";
        const auto text   = fmt::format(
            "[Event \"Club Open {}\"]\n[Date \"{}.03.0{}\"]\n[White \"{}\"]\n[Black \"{}\"]\n"
            "[WhiteElo \"2310\"]\n[BlackElo \"2290\"]\n[Result \"{}\"]\n\n{} {}\n",
            2020 + i % 5, 2020 + i % 5, 1 + i % 9, white ? "Ferreira, Rui" : "Ito, Ken",
            white ? "Haddad, Sami" : "Ferreira, Rui", result, lines[i % 3], result);
        study::ImportedGame g;
        REQUIRE(study::readImportedGame(text, g));
        games.push_back(std::move(g));
    }
    const auto dir = fs::temp_directory_path() / "publish-dossier";
    fs::remove_all(dir);
    auto db = study::Database::create(dir);
    db->append(std::move(games), true);
    DossierAsk ask;
    ask.subject.name = "Ferreira, Rui";
    ask.bases.push_back({"Club", dir, db});
    study::DossierProgress progress;
    std::atomic<bool> cancel{false};
    const auto report = makeDossierReport(std::move(ask), progress, cancel);
    REQUIRE(report.dossier);
    REQUIRE(report.board.games == 12);
    const auto doc   = dossierDocument(report, "Ferreira, Rui", {"Club"}, false, false, look());
    const auto pages = layOut(doc);
    const auto text  = pageText(pages);
    CHECK(text.find("Ferreira, Rui") != std::string::npos);
    CHECK(text.find("Made of: Club · 12 games · 2020–2024") != std::string::npos);
    CHECK(text.find("With White") != std::string::npos);
    CHECK(text.find("Their results") != std::string::npos);
    CHECK(text.find("Their last games") != std::string::npos);
    CHECK(text.find("Club Open 2024") != std::string::npos);
    checkPdf(pages, doc.title);
    // As a page: its lines replayable on a board beside the text.
    const auto html = documentHtml(doc);
    CHECK(html.find("<svg id=\"board\"") != std::string::npos);
    CHECK(html.find("var GAMES=[") != std::string::npos);
    CHECK(html.find("class=\"mv\" id=\"m0-0\"") != std::string::npos);
    CHECK(html.find("http") == std::string::npos);
}

TEST_CASE("publish: an opening report printed: its position, moves, lines, key games and summary marked") {
    // Games through a Queen's Gambit position, ours.
    const auto dir = fs::temp_directory_path() / "publish-report";
    fs::remove_all(dir);
    auto db = study::Database::create(dir);
    std::vector<study::ImportedGame> games;
    const char* tails[] = {"7. Bd3 Nbd7", "7. Qc2 Nbd7", "7. Bd3 O-O", "7. Nf3 Nbd7"};
    for (int i = 0; i < 16; ++i) {
        const auto result = i % 3 == 0 ? "1-0" : i % 3 == 1 ? "1/2-1/2" : "0-1";
        const auto text   = fmt::format(
            "[White \"Ansel, Pia\"]\n[Black \"Brandt, Ole\"]\n[Date \"{}.05.01\"]\n"
            "[WhiteElo \"{}\"]\n[BlackElo \"2400\"]\n[Result \"{}\"]\n\n1. d4 d5 2. c4 e6 3. Nc3 Nf6 "
            "4. cxd5 exd5 5. Bg5 c6 6. e3 Be7 {} {}\n",
            2010 + i, 2450 + i * 10, result, tails[i % 4], result);
        study::ImportedGame g;
        REQUIRE(study::readImportedGame(text, g));
        games.push_back(std::move(g));
    }
    db->append(std::move(games), true);
    study::TreeOptions tree_options;
    tree_options.threads = 1;
    study::TreeProgress tree_progress;
    std::atomic<bool> cancel{false};
    REQUIRE(study::buildTree(*db, *db->snapshot(), tree_options, tree_progress, cancel).error.empty());
    const auto tree = study::Tree::open(dir);
    REQUIRE(tree);
    chess::Board board;
    std::vector<chess::Move> path;
    for (const auto* san : {"d4", "d5", "c4", "e6", "Nc3", "Nf6", "cxd5", "exd5", "Bg5", "c6", "e3", "Be7"}) {
        path.push_back(chess::uci::parseSan(board, san));
        board.makeMove(path.back());
    }
    std::vector<ReportBase> bases{{"Club", dir, db->snapshot(), tree}};
    study::ReportProgress progress;
    ReportShared shared;
    makeReport(board.getFen(), bases, {}, 1, progress, cancel, shared);
    REQUIRE(shared.report);
    const auto& report = *shared.report;
    const auto rows    = reportRows(board, report.moves, {"Club"}, {});
    REQUIRE(rows.size() >= 3);
    std::vector<std::optional<ReportEval>> evals(rows.size());
    evals[0] = ReportEval{31, std::nullopt, 30, false};
    ReportView view;
    view.board   = board;
    view.path    = path;
    view.opening = "D36 Queen's Gambit Declined: Exchange Variation";
    view.report  = &report;
    view.rows    = &rows;
    view.evals   = &evals;
    ReportSummary summary;
    commentary::SummaryPassage passage;
    passage.text = "After 6…Be7, White plays 7.Bd3 in half of the games.";
    summary.passages.push_back(passage);
    const auto doc   = openingReportDocument(view, bases, &summary, false, look());
    const auto pages = layOut(doc);
    const auto text  = pageText(pages);
    CHECK(text.find("D36 Queen's Gambit Declined: Exchange Variation") != std::string::npos);
    CHECK(text.find("7.Bd3") != std::string::npos);
    CHECK(text.find("+0.31") != std::string::npos);
    CHECK(text.find("Sources: Club") != std::string::npos);
    CHECK(text.find("Assistant:") != std::string::npos);
    CHECK(text.find("White plays 7.Bd3 in half of the games.") != std::string::npos);
    CHECK(diagramsOf(pages) == 1);  // its position
    checkPdf(pages, doc.title);
    const auto html = documentHtml(doc);
    CHECK(html.find("class=\"as\"") != std::string::npos);  // the summary marked as the assistant's
    CHECK(html.find("http") == std::string::npos);
}
