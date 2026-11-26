// Every move explained (#94, mockup M13, board 7, point 15): the theory grouped, its games counted apart over a
// database of our own; a forced recapture said in a word; the model's sentence when the checker lets it through, the
// template's with what it refused when it does not (the assistant's scripted model, and a fake in this process); kept
// with the review and read back; written into the game as the assistant's comments, taken back by one Ctrl+Z. Players'
// names are invented.

#include <doctest/doctest.hpp>

#include <atomic>
#include <deque>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>

#include <commentary/theory.hpp>
#include <commentary/writing.hpp>
#include <study/import.hpp>
#include <study/scan_board.hpp>
#include <study/store.hpp>
#include <study/tree.hpp>

#include "assistant_notes.hpp"
#include "assistant_service.hpp"
#include "board_page.hpp"
#include "explained.hpp"
#include "mock_engines.hpp"
#include "pgn_store.hpp"
#include "review_bridge.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

// The game: the Two Knights' theory, a bishop given on f7, its forced recapture, then quiet moves.
constexpr const char* GAME = "1. e4 e5 2. Nf3 Nc6 3. Bc4 Nf6 4. d3 Bc5 5. Bxf7+ Kxf7 6. Nc3 d6 7. O-O Rf8 *";

PositionEval eval(int64_t cp, std::string pv) {
    PositionEval e;
    e.score = fastchess::Score{fastchess::ScoreType::CP, cp};
    e.pv    = std::move(pv);
    e.best  = e.pv.substr(0, e.pv.find(' '));
    e.depth = 20;
    return e;
}

// Its review: level up to 5.Bxf7+, which costs White three pawns; the game's moves the engine's elsewhere.
GameReview reviewOf(const ReplayGame& game) {
    GameReview review;
    review.engine = "Engine";
    review.passes = 3;
    for (std::size_t i = 0; i < game.moves.size(); ++i) {
        const bool white = i % 2 == 0;
        int64_t cp       = i <= 8 ? 20 : 300;  // after 5.Bxf7+ (the move before position 9) Black is three pawns up
        if (white && i > 8) cp = -300;
        if (!white && i <= 8) cp = -20;
        review.positions.push_back(eval(cp, i == 8 ? "e1g1" : chess::uci::moveToUci(game.moves[i].move)));
    }
    review.positions.push_back(eval(-300, "a2a3"));  // White to move after 7...Rf8
    return review;
}

// Our database: 240 games through the Two Knights, 80 of them on with 4.d3, the others 4.Ng5.
struct Made {
    std::shared_ptr<study::Database> db;
    std::shared_ptr<const study::Tree> tree;
    std::vector<std::vector<std::string>> lines;
};

Made database(const fs::path& dir) {
    fs::remove_all(dir);
    Made m;
    m.db = study::Database::create(dir);
    std::vector<study::ImportedGame> games;
    for (int i = 0; i < 240; ++i) {
        const bool d3 = i % 3 == 0;
        std::vector<std::string> line{"e4", "e5", "Nf3", "Nc6", "Bc4", "Nf6", d3 ? "d3" : "Ng5", d3 ? "Be7" : "d5"};
        const std::string text = fmt::format(
            "[White \"Player_{}\"]\n[Black \"Partner_{}\"]\n[Result \"1-0\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bc4 Nf6 4. {} "
            "{} "
            "1-0\n",
            i, i, line[6], line[7]);
        study::ImportedGame g;
        REQUIRE(study::readImportedGame(text, g));
        games.push_back(std::move(g));
        m.lines.push_back(std::move(line));
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

// A model that gives its answers in turn.
class Scripted final : public commentary::Model {
   public:
    explicit Scripted(std::deque<std::string> answers) : answers_(std::move(answers)) {}
    std::optional<commentary::Generation> generate(const commentary::Request&, std::string* error) override {
        if (answers_.empty()) {
            if (error) *error = "no more answers";
            return std::nullopt;
        }
        commentary::Generation g;
        g.text = answers_.front();
        answers_.pop_front();
        return g;
    }

   private:
    std::deque<std::string> answers_;
};

}  // namespace

TEST_CASE("every move explained: theory grouped, its games counted apart over games of our own") {
    const auto game = parseReplay(GAME);
    REQUIRE(game);
    const auto made = database(fs::temp_directory_path() / "explained-db");
    const auto line = mainLineOf(*game, false);
    REQUIRE(line);
    // The game's theory in our database, as the review takes it.
    const auto opening = commentary::theoryOf(*made.tree, made.db->snapshot(), *line);
    CHECK(opening.book_plies == 7);  // no game played 4...Bc5: 4.d3 is the last move of theory
    const auto review = commentaryReview(*game, false, reviewOf(*game), opening);
    REQUIRE(review);
    REQUIRE(review->report.complete);
    const auto games = [&](const commentary::Position& pos) {
        study::ScanBoard scan;
        REQUIRE(scan.setFen(pos.fen(), false));
        const auto at = made.tree->at(*made.db->snapshot(), scan.key());
        return at ? at->total.games : uint64_t{0};
    };
    const auto plan = explainPlan(review->line, review->report, commentary::Lang::En, games);
    // The theory's lines cover its plies, three at most, then a line for each move.
    std::size_t theory_lines = 0, covered = 0;
    for (const auto& l : plan) {
        if (l.source != ExplainedLine::Source::Theory) continue;
        ++theory_lines;
        CHECK(l.from == covered + 1);
        covered = l.to;
        CHECK(l.written);
        // Counted apart: the games whose first moves are the game's up to the line's end.
        uint64_t apart = 0;
        for (const auto& g : made.lines) {
            bool same = true;
            for (std::size_t k = 0; k < l.to && same; ++k)
                same = g[k] == chess::uci::moveToSan(boardAt(*game, false, static_cast<int>(k)), game->moves[k].move);
            apart += same ? 1 : 0;
        }
        CHECK(l.games == apart);
    }
    CHECK(theory_lines >= 1);
    CHECK(theory_lines <= 3);
    CHECK(covered == review->report.moves.size() - (plan.size() - theory_lines));
    CHECK(plan.back().to == game->moves.size());
    // The last theory line ends at 4.d3, which 80 games reached.
    const auto last_theory = std::find_if(plan.rbegin(), plan.rend(),
                                          [](const auto& l) { return l.source == ExplainedLine::Source::Theory; });
    REQUIRE(last_theory != plan.rend());
    CHECK(last_theory->games == 80);
    // 5...Kxf7: the only way to take back, said in a word.
    const auto kxf7 = std::find_if(plan.begin(), plan.end(), [](const auto& l) { return l.from == 10; });
    REQUIRE(kxf7 != plan.end());
    CHECK(kxf7->source == ExplainedLine::Source::Simple);
    CHECK(kxf7->text == "Forced recapture.");
    // 5.Bxf7+, the blunder, matters: to write.
    const auto bxf7 = std::find_if(plan.begin(), plan.end(), [](const auto& l) { return l.from == 9; });
    REQUIRE(bxf7 != plan.end());
    CHECK(bxf7->matters);
    CHECK_FALSE(bxf7->written);
    // The folder goes with the tests' own temporary folder: the database, still open here, holds a file of it that
    // Windows would not let go.
}

TEST_CASE("every move explained: the model's sentence held by the checker, the template's when it refuses it") {
    const auto game = parseReplay(GAME);
    REQUIRE(game);
    const auto review = commentaryReview(*game, false, reviewOf(*game));
    REQUIRE(review);
    const auto* m = review->report.get(9);  // 5.Bxf7+
    REQUIRE(m);
    REQUIRE(m->judgment);
    const auto ctx = commentary::moveContext(review->line, *m, review->analyses, nullptr, nullptr, nullptr);
    // A sentence the checker lets through: the model's.
    Scripted good({"A blunder."});
    const auto kept = writeExplainedLine(&good, ctx, commentary::Lang::En, commentary::PieceLetters::English);
    CHECK(kept.source == ExplainedLine::Source::Model);
    CHECK(kept.text == "A blunder.");
    CHECK_FALSE(kept.refused);
    // A verdict other than the review's, three times: the template's, with why.
    Scripted wrong({"An inaccuracy.", "An inaccuracy.", "An inaccuracy."});
    const auto held = writeExplainedLine(&wrong, ctx, commentary::Lang::En, commentary::PieceLetters::English);
    CHECK(held.asked);
    CHECK(held.source != ExplainedLine::Source::Model);
    REQUIRE(held.refused);
    CHECK(held.refused->kind == commentary::Refusal::Kind::Verdict);
    CHECK_FALSE(held.text.empty());
    // Without a model: the template's at once.
    const auto plain = writeExplainedLine(nullptr, ctx, commentary::Lang::En, commentary::PieceLetters::English);
    CHECK_FALSE(plain.asked);
    CHECK(plain.text == held.text);
    CHECK_FALSE(plain.refused);
}

#ifdef FCG_ASSISTANT
TEST_CASE("every move explained: the assistant's scripted model refused by the checker, the template standing") {
    // The program's model process, scripted: it answers with what it was asked, digits and all, which the grammar of
    // a comment refuses.
    const auto profile = fs::temp_directory_path() / "explained-assistant";
    fs::remove_all(profile);
    fs::create_directories(profile);
    AssistantService service(profile, test::mockAssistant());
    std::ofstream(profile / "commentary.gguf") << "model";
    auto settings       = service.settings();
    settings.model_file = (profile / "commentary.gguf").string();
    settings.gpu        = false;
    service.setSettings(settings);
    const auto game = parseReplay(GAME);
    REQUIRE(game);
    const auto review = commentaryReview(*game, false, reviewOf(*game));
    REQUIRE(review);
    const auto* m = review->report.get(9);
    REQUIRE(m);
    const auto ctx = commentary::moveContext(review->line, *m, review->analyses, nullptr, nullptr, nullptr);
    const auto w   = writeExplainedLine(&service, ctx, commentary::Lang::En, commentary::PieceLetters::English);
    CHECK(w.asked);
    REQUIRE(w.refused);
    CHECK(w.refused->kind == commentary::Refusal::Kind::Grammar);
    CHECK(w.source != ExplainedLine::Source::Model);
    CHECK_FALSE(w.text.empty());
    service.unload();
    fs::remove_all(profile);
}
#endif

TEST_CASE("every move explained: kept with the review and read back, written into the game and taken back") {
    const auto game = parseReplay(GAME);
    REQUIRE(game);
    const auto review = commentaryReview(*game, false, reviewOf(*game));
    REQUIRE(review);
    Explained e;
    e.key    = reviewKey(*game);
    e.review = explainedReview("Engine", 3, game->moves.size() + 1);
    e.lang   = "en";
    e.lines  = explainPlan(review->line, review->report, commentary::Lang::En, {});
    REQUIRE(review->report.complete);
    for (auto& l : e.lines) {
        if (l.written) continue;
        const auto* m = review->report.get(l.to);
        REQUIRE(m);
        const auto ctx = commentary::moveContext(review->line, *m, review->analyses, nullptr, nullptr, nullptr);
        const auto w   = writeExplainedLine(nullptr, ctx, commentary::Lang::En, commentary::PieceLetters::English);
        l.text         = w.text;
        l.source       = w.source;
        l.written      = true;
    }
    e.lines.back().refused = commentary::Refusal{commentary::Refusal::Kind::Motif, "fourchette"};
    e.complete             = true;
    e.model_secs           = 12.5;
    e.model_lines          = 5;
    const auto dir         = fs::temp_directory_path() / "explained-kept";
    fs::remove_all(dir);
    fs::create_directories(dir);
    REQUIRE(saveExplained(dir, e));
    const auto back = loadExplained(dir, e.key, "en");
    REQUIRE(back);
    CHECK(back->review == e.review);
    CHECK(back->complete);
    CHECK(back->model_lines == 5);
    REQUIRE(back->lines.size() == e.lines.size());
    for (std::size_t i = 0; i < e.lines.size(); ++i) {
        CHECK(back->lines[i].text == e.lines[i].text);
        CHECK(back->lines[i].source == e.lines[i].source);
        CHECK(back->lines[i].from == e.lines[i].from);
        CHECK(back->lines[i].to == e.lines[i].to);
        CHECK(back->lines[i].matters == e.lines[i].matters);
    }
    REQUIRE(back->lines.back().refused);
    CHECK(back->lines.back().refused->what == "fourchette");
    CHECK_FALSE(loadExplained(dir, e.key, "fr"));  // another language: its own
    const auto counts = explainedCounts(*back);
    CHECK(counts.moves == static_cast<int>(game->moves.size()));
    CHECK(counts.simple == 1);
    CHECK(counts.refused == 1);

    // Into the game: one change, the assistant's comments beside the user's, one Ctrl+Z.
    BoardPage b;
    b.tree.main                       = *game;
    b.tree.main.moves[13].notes.after = "My own comment.";  // 7...Rf8
    b.game                            = b.tree.main;
    std::vector<AssistantTarget> targets;
    std::vector<std::string> texts;
    for (const auto& l : back->lines) {
        targets.push_back({-1, static_cast<int>(l.to), boardAt(b.tree.main, false, static_cast<int>(l.to)).getFen()});
        texts.push_back(l.text);
    }
    auto before    = boardSnapshot(b);
    const auto put = placeAssistantComments(b.tree, false, targets, texts);
    boardChanged(b, std::move(before));
    CHECK(put.size() == back->lines.size());
    CHECK(b.tree.main.moves[9].notes.assistant == "Forced recapture.");
    CHECK(b.tree.main.moves[13].notes.after == "My own comment.");  // never replaced
    CHECK_FALSE(b.tree.main.moves[13].notes.assistant.empty());
    REQUIRE(boardUndo(b, false));
    for (const auto& mv : b.tree.main.moves) CHECK(mv.notes.assistant.empty());
    CHECK(b.tree.main.moves[13].notes.after == "My own comment.");
    // Written again, a move the assistant commented already keeps it: nothing twice.
    before = boardSnapshot(b);
    (void)placeAssistantComments(b.tree, false, targets, texts);
    boardChanged(b, std::move(before));
    CHECK(placeAssistantComments(b.tree, false, targets, texts).empty());
    fs::remove_all(dir);
}

TEST_CASE("every move explained: a forced recapture takes back what the captures took, not a trade one may decline") {
    // 5...exd4 6.Qxd4 Qxd4 7.Nxd4: 6...Qxd4, the only way to take on d4, is a queen trade Black may decline (the
    // captures on d4 have left it no worse off); 7.Nxd4 takes the queen back, the only way.
    auto pos = commentary::Position::fromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    REQUIRE(pos);
    const commentary::Position start = *pos;
    std::vector<commentary::Move> moves;
    for (const char* san :
         {"e4", "e5", "Nf3", "Nc6", "Bb5", "a6", "Bxc6", "dxc6", "d4", "exd4", "Qxd4", "Qxd4", "Nxd4"}) {
        const auto m = pos->parseSan(san);
        REQUIRE_MESSAGE(m, san);
        moves.push_back(*m);
        pos->playUnchecked(*m);
    }
    const commentary::MainLine line(start, moves);
    const auto word = [&](std::size_t ply) {
        return simpleMoveWord(line.positions()[ply - 1], line.moveTo(ply - 1), line.moves()[ply - 1],
                              behindSinceTheCaptures(line, ply), commentary::Lang::En);
    };
    CHECK(behindSinceTheCaptures(line, 8));   // 4...dxc6: a bishop down since 4.Bxc6
    CHECK(word(8) == std::nullopt);           // but two ways to take back (b7 and d7)
    CHECK(behindSinceTheCaptures(line, 11));  // 6.Qxd4: a pawn down since 5...exd4
    CHECK_FALSE(behindSinceTheCaptures(line, 12));
    CHECK(word(12) == std::nullopt);
    CHECK(behindSinceTheCaptures(line, 13));
    CHECK(word(13) == std::optional<std::string>("Forced recapture."));
}
