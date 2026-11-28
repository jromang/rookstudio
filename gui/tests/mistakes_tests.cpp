// Your mistakes as exercises and guessing the moves (#81): the user's names, the exercises a review gives, an answer
// judged as the review judges a move, the cards kept in the profile and counted for the day, the user's games looked
// for in their databases, a guess scored, the profile's files read with their types checked (#136); and an answer
// searched at the review's settings by the mock engine finds what the review found.

#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <thread>

#include <fmt/include/fmt/format.h>
#include <json.hpp>
#include <study/import.hpp>
#include <study/store.hpp>

#include "analysis.hpp"
#include "board_text.hpp"
#include "console.hpp"
#include "game_analysis.hpp"
#include "mistakes.hpp"
#include "mock_engines.hpp"
#include "review_runner.hpp"
#include "test_support.hpp"
#include "training.hpp"

using namespace fcg;
namespace fs = std::filesystem;
using namespace std::chrono_literals;
using fcg::test::ScratchDir;

namespace {

fastchess::Score cp(int64_t v) { return {fastchess::ScoreType::CP, v}; }
fastchess::Score mate(int64_t v) { return {fastchess::ScoreType::MATE, v}; }

// Black to move, its knight on d4 forking the king and the rook from c2; the game went 23…Be6?? 24.Nxd4 instead.
constexpr const char* FORK_FEN = "r1bq1rk1/ppp2ppp/3p4/8/3nP3/4QN2/PP1B1PPP/R3K2R b KQ - 0 23";

const char* forkPgn(const char* white = "Partner, Alex", const char* black = "Tester, Sam",
                    const char* date = "2026.11.12") {
    static std::string text;
    text = std::string("[Event \"Club game\"]\n[White \"") + white + "\"]\n[Black \"" + black + "\"]\n[Date \"" + date +
           "\"]\n[Result \"1-0\"]\n[SetUp \"1\"]\n[FEN \"" + FORK_FEN + "\"]\n\n23... Be6 24. Nxd4 1-0\n";
    return text.c_str();
}

ReplayGame readGame(const std::string& pgn) {
    std::string error;
    auto game = readPgnGame(pgn, error);
    REQUIRE_MESSAGE(game, error);
    return *game;
}

// The review of the fork game: deep enough, Black's 23…Be6 a blunder, its best move 23…Nc2+.
GameReview forkReview(uint64_t nodes = passNodes(2)) {
    GameReview r;
    r.engine = "Mock 1.0";
    r.passes = 3;
    r.nodes  = nodes;
    PositionEval p0;  // Black to move: winning the exchange
    p0.score = cp(240);
    p0.best  = "d4c2";
    p0.pv    = "d4c2 e1d1 c2a1";
    p0.depth = 18;
    PositionEval p1;  // after 23…Be6, White to move: the knight is taken
    p1.score = cp(290);
    p1.best  = "f3d4";
    p1.pv    = "f3d4 e6d7";
    p1.depth = 18;
    PositionEval p2;  // after 24.Nxd4, Black to move
    p2.score    = cp(-290);
    p2.best     = "d8f6";
    p2.pv       = "d8f6";
    p2.depth    = 18;
    r.positions = {p0, p1, p2};
    return r;
}

using fcg::test::randomMover;

void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value)
        setenv(name, value, 1);
    else
        unsetenv(name);
#endif
}

struct ScopedEnv {
    const char* name;
    ScopedEnv(const char* n, const char* value) : name(n) { setEnv(n, value); }
    ~ScopedEnv() { setEnv(name, nullptr); }
};

}  // namespace

TEST_CASE("mistakes: the user's names, told by whole words, case and accents left out") {
    CHECK(nameWords("Tester, Sam-Éric") == std::vector<std::string>{"tester", "sam", "eric"});
    CHECK(nameWords("  ").empty());
    const std::vector<std::string> names = {"Tester", "Sam Tester", "MyAccount"};
    CHECK(isUserName("Tester, Sam", names));
    CHECK(isUserName("TESTER, S.", names));
    CHECK(isUserName("myaccount", names));
    CHECK_FALSE(isUserName("Testerman, Sam", names));  // a word, not a part of one
    CHECK_FALSE(isUserName("Partner, Alex", names));
    CHECK_FALSE(isUserName("", names));
    CHECK(isUserName("Sam Tester", {"sam  tester"}));
    CHECK_FALSE(isUserName("Sam Other", {"Sam Tester"}));  // every word of a name
    CHECK(userSide("Partner, Alex", "Tester, Sam", names) == chess::Color::BLACK);
    CHECK(userSide("Tester, Sam", "Partner, Alex", names) == chess::Color::WHITE);
    CHECK_FALSE(userSide("Tester, Sam", "MyAccount", names));  // both: no side
    CHECK_FALSE(userSide("Partner, Alex", "Other, Kim", names));
}

TEST_CASE("mistakes: a ? or ?? of the user's, with a clear best move, becomes an exercise") {
    const auto game   = readGame(forkPgn());
    const auto review = forkReview();
    const auto found  = exercisesOf(game, review, chess::Color::BLACK, commentary::Lang::En);
    REQUIRE(found.size() == 1);
    const auto& e = found[0];
    const chess::Board start(FORK_FEN);
    CHECK(e.key == start.hash());
    CHECK(e.board().getFen() == start.getFen());
    CHECK(e.side() == chess::Color::BLACK);
    CHECK(e.moves.empty());  // the game's first position
    CHECK(e.played == "c8e6");
    CHECK(e.best == "d4c2");
    CHECK(e.line == "d4c2 e1d1 c2a1");
    CHECK(e.reply == "f3d4 e6d7");
    CHECK(e.judgement == Judgement::Blunder);
    CHECK(e.engine == "Mock 1.0");
    CHECK(e.nodes == passNodes(2));
    CHECK(e.limit().nodes == passNodes(2));
    CHECK(e.game_key == reviewKey(game));
    CHECK(e.white == "Partner, Alex");
    CHECK(e.black == "Tester, Sam");
    CHECK(e.date == "2026.11.12");
    REQUIRE(e.score);
    CHECK(e.score->value == 240);
    REQUIRE(e.after);
    CHECK(e.after->value == 290);
    CHECK(e.card.fresh());
    CHECK(e.comment_lang == "en");

    // White's move was the engine's: nothing for White.
    CHECK(exercisesOf(game, review, chess::Color::WHITE, commentary::Lang::En).empty());
    // A review of two passes is too shallow to name a best move; one not complete is not judged.
    CHECK(exercisesOf(game, forkReview(passNodes(1)), chess::Color::BLACK, commentary::Lang::En).empty());
    auto partial = review;
    partial.positions.pop_back();
    CHECK(exercisesOf(game, partial, chess::Color::BLACK, commentary::Lang::En).empty());
    // Lost anyway: the best move left Black lost too.
    auto lost               = review;
    lost.positions[0].score = cp(-900);
    lost.positions[1].score = cp(1500);
    lost.positions[2].score = cp(-1500);
    CHECK(exercisesOf(game, lost, chess::Color::BLACK, commentary::Lang::En).empty());
    // Still winning after the move played: nothing to find.
    auto winning               = review;
    winning.positions[0].score = cp(2000);
    winning.positions[1].score = cp(-1100);
    winning.positions[2].score = cp(1100);
    CHECK(exercisesOf(game, winning, chess::Color::BLACK, commentary::Lang::En).empty());
}

TEST_CASE("mistakes: an answer is right as the review's best, or within 0.3 pawn of it, or a win kept") {
    const auto game = readGame(forkPgn());
    const auto all  = exercisesOf(game, forkReview(), chess::Color::BLACK, commentary::Lang::En);
    REQUIRE(all.size() == 1);
    const auto& e    = all[0];
    const auto board = e.board();
    const auto move  = [&](const char* uci) { return chess::uci::uciToMove(board, uci); };
    const auto after = [](fastchess::Score s, const char* pv) {
        PositionEval p;
        p.score = s;
        p.pv    = pv;
        p.best  = std::string(pv).substr(0, 4);
        return p;
    };
    // The review's best move.
    auto check = checkAnswer(e, move("d4c2"), after(cp(-240), "e1d1 c2a1"));
    CHECK(check.judged);
    CHECK(check.right);
    CHECK(check.cp_loss == 0);
    // Another move, 0.2 pawn worse: right.
    check = checkAnswer(e, move("d4b3"), after(cp(-220), "a2b3"));
    CHECK(check.right);
    CHECK(check.cp_loss == 20);
    // 23…Nxf3+ 24.gxf3: about level, 2.6 pawns worse: wrong, and the review would have marked it.
    check = checkAnswer(e, move("d4f3"), after(cp(20), "g2f3"));
    CHECK(check.judged);
    CHECK_FALSE(check.right);
    CHECK(check.cp_loss == 260);
    REQUIRE(check.judgement);
    CHECK(*check.judgement >= Judgement::Mistake);
    // A mate found where the review saw less, or a mate in more moves: right.
    auto mating  = e;
    mating.score = mate(3);
    check        = checkAnswer(mating, move("d4c2"), after(mate(-4), "e1d1"));
    CHECK(check.right);
    check = checkAnswer(mating, move("d4f3"), after(mate(-6), "g2f3"));
    CHECK(check.right);
    // A mate let go for a few pawns more is a blunder to the review: wrong.
    check = checkAnswer(mating, move("d4f3"), after(cp(-900), "g2f3"));
    CHECK_FALSE(check.right);
    // A win kept: 7 pawns up where the review saw 9; the review would not mark it.
    auto crushing  = e;
    crushing.score = cp(900);
    check          = checkAnswer(crushing, move("d4f3"), after(cp(-700), "g2f3"));
    CHECK(check.cp_loss == 200);
    CHECK_FALSE(check.judgement);
    CHECK(check.right);
    // 1.5 pawns up where the review saw 9: the win is not kept.
    check = checkAnswer(crushing, move("d4f3"), after(cp(-150), "g2f3"));
    CHECK_FALSE(check.right);
    // A move that ends the game is scored without an engine.
    CHECK_FALSE(endedAfter(e, move("d4c2")));
}

TEST_CASE("mistakes: the exercises and their cards kept in the profile, counted and ordered for the day") {
    const ScratchDir dir("mistakes-record");
    const auto game = readGame(forkPgn());
    auto found      = exercisesOf(game, forkReview(), chess::Color::BLACK, commentary::Lang::En);
    REQUIRE(found.size() == 1);
    found[0].database = "My games";
    found[0].folder   = dir / "My games.fcgdb";
    found[0].game_id  = 7;
    MistakeRecord record;
    putExercises(record, found[0].game_key, "sig", found);
    record.answer_ms = 4200;
    auto& kept       = record.exercises.begin()->second;
    kept.card        = trainingScheduler().review(kept.card, study::fsrs::Rating::Good, 1000000);
    kept.since       = 1000000;
    REQUIRE(writeMistakeRecord(dir, record));
    const auto back = readMistakeRecord(dir);
    CHECK(back.answer_ms == 4200);
    REQUIRE(back.exercises.size() == 1);
    const auto& e = back.exercises.begin()->second;
    CHECK(e.key == kept.key);
    CHECK(e.played == kept.played);
    CHECK(e.best == kept.best);
    CHECK(e.line == kept.line);
    CHECK(e.database == "My games");
    CHECK(e.folder == dir / "My games.fcgdb");
    CHECK(e.game_id == 7);
    CHECK(e.card == kept.card);
    CHECK(e.since == 1000000);
    REQUIRE(e.score);
    CHECK(e.score->value == 240);
    REQUIRE(back.games.count(kept.game_key) == 1);
    CHECK(back.games.at(kept.game_key).review == "sig");
    CHECK(back.games.at(kept.game_key).keys == std::vector<uint64_t>{kept.key});
    appendMistakeAnswer(dir, e.key, 1000000, study::fsrs::Rating::Good, 5000, "d4c2");
    CHECK(fs::file_size(dir / "answers.jsonl") > 0);

    // Counted: two due (one overdue), a known one, two new; one new one begun today counts against the day's limit.
    const study::TrainingDay day{2000000, 2000000 + 3600};
    std::vector<MistakeExercise> list(5, e);
    for (std::size_t i = 0; i < list.size(); ++i) {
        list[i].key      = 100 + i;
        list[i].game_key = "g" + std::to_string(i % 3);
    }
    list[0].card.due    = day.end - 100;
    list[1].card.due    = day.now - 50000;  // the longest overdue
    list[2].card.due    = day.end + study::fsrs::DAY / 2;
    list[3].card        = {};
    list[3].since       = 0;
    list[3].packed_date = study::packDate("2026.10.01");
    list[4].card        = {};
    list[4].packed_date = study::packDate("2026.11.01");
    list[4].judgement   = Judgement::Blunder;
    list[0].since       = day.end - 1000;  // begun today
    std::vector<const MistakeExercise*> all;
    for (const auto& x : list) all.push_back(&x);
    const auto counts = mistakeCounts(all, day, 2);
    CHECK(counts.exercises == 5);
    CHECK(counts.games == 3);
    CHECK(counts.due == 2);
    CHECK(counts.known == 1);
    CHECK(counts.tomorrow == 1);
    CHECK(counts.three_days == 1);
    CHECK(counts.fresh == 2);
    CHECK(counts.today == 1);
    // The due ones, the longest overdue first; then the new ones, the latest game first.
    CHECK(mistakeOrder(all, day, 2, true) == std::vector<uint64_t>{101, 100, 104, 103});
    CHECK(mistakeOrder(all, day, 1, true) == std::vector<uint64_t>{101, 100, 104});
    CHECK(mistakeOrder(all, day, 2, false) == std::vector<uint64_t>{104, 103});
    CHECK(mistakeOrder(all, day, 0, true) == std::vector<uint64_t>{101, 100});
    fs::remove_all(dir);
}

TEST_CASE("mistakes: a game's exercises replaced by its new review's, their cards kept") {
    const auto game = readGame(forkPgn());
    auto found      = exercisesOf(game, forkReview(), chess::Color::BLACK, commentary::Lang::En);
    REQUIRE(found.size() == 1);
    found[0].game_key = "game-a";
    MistakeRecord record;
    putExercises(record, "game-a", "one", found);
    record.exercises.begin()->second.since = 42;
    // The same position from another, older game: the latest game's data stays, the card is one.
    auto older           = found;
    older[0].game_key    = "game-b";
    older[0].packed_date = study::packDate("2020.01.01");
    older[0].date        = "2020.01.01";
    putExercises(record, "game-b", "one", older);
    REQUIRE(record.exercises.size() == 1);
    CHECK(record.exercises.begin()->second.date == "2026.11.12");
    CHECK(record.exercises.begin()->second.since == 42);
    // Its new review finds nothing there: the exercise stays while the other game gives it.
    putExercises(record, "game-a", "two", {});
    CHECK(record.exercises.size() == 1);
    CHECK(record.games.at("game-a").keys.empty());
    // Answered once, then given by no game: its card is kept apart, and comes back with it.
    record.exercises.begin()->second.card.reps = 2;
    record.exercises.begin()->second.card.last = 1000;
    putExercises(record, "game-b", "two", {});
    CHECK(record.exercises.empty());
    REQUIRE(record.kept.size() == 1);
    putExercises(record, "game-b", "three", older);
    REQUIRE(record.exercises.size() == 1);
    CHECK(record.exercises.begin()->second.card.reps == 2);
    CHECK(record.exercises.begin()->second.since == 42);
    CHECK(record.kept.empty());
}

TEST_CASE("mistakes: the user's games looked for in their databases, the reviewed ones giving their exercises") {
    const ScratchDir dir("mistakes-scan");
    {  // the database and its snapshots closed before the folder goes: Windows removes no file still mapped
        const auto analyses = dir / "analyses";
        fs::create_directories(analyses);
        auto db = study::Database::create(dir / "Games.fcgdb");
        std::vector<study::ImportedGame> games;
        for (const auto* pgn :
             {forkPgn("Partner, Alex", "Tester, Sam", "2026.11.12"),
              "[White \"Tester, Sam\"]\n[Black \"Other, Kim\"]\n[Date \"2026.11.14\"]\n[Result \"1-0\"]\n\n1. e4 e5 2. "
              "Nf3 "
              "1-0\n",
              "[White \"Other, Kim\"]\n[Black \"Partner, Alex\"]\n[Date \"2026.11.15\"]\n[Result \"0-1\"]\n\n1. d4 d5 "
              "0-1\n",
              "[White \"Tester, Sam\"]\n[Black \"Partner, Alex\"]\n[Date \"2026.11.01\"]\n[Result \"*\"]\n\n1. c4 "
              "*\n"}) {
            study::ImportedGame g;
            REQUIRE(study::readImportedGame(pgn, g));
            games.push_back(std::move(g));
        }
        db->append(std::move(games), true);
        const auto snapshot = db->snapshot();
        // The first game reviewed, as the Board page keeps a review: under the key of the game it reads.
        std::string error;
        const auto first = readPgnGame(study::importedPgn(snapshot->game(0)), error);
        REQUIRE(first);
        REQUIRE(saveReview(analyses, *first, forkReview()));

        const std::vector<MistakeSource> sources = {{"Games", dir / "Games.fcgdb", snapshot}};
        const std::vector<std::string> names     = {"Tester"};
        auto scan                                = scanMistakes(sources, names, analyses, {}, 20, commentary::Lang::En);
        CHECK_FALSE(scan.cancelled);
        CHECK(scan.games == 3);  // the third is not theirs
        CHECK(scan.reviewed == 1);
        REQUIRE(scan.record.exercises.size() == 1);
        const auto& e = scan.record.exercises.begin()->second;
        CHECK(e.database == "Games");
        CHECK(e.folder == dir / "Games.fcgdb");
        CHECK(e.game_id == 0);
        CHECK(e.reviewed > 0);
        CHECK(e.best == "d4c2");
        REQUIRE(scan.record.games.size() == 1);
        // The latest without a review, the latest first.
        REQUIRE(scan.unreviewed.size() == 2);
        CHECK(scan.unreviewed[0].id == 1);
        CHECK(scan.unreviewed[0].side == chess::Color::WHITE);
        CHECK(scan.unreviewed[0].game.moves.size() == 3);
        CHECK(scan.unreviewed[1].id == 3);
        // Only the latest one counts when one game is asked for.
        CHECK(scanMistakes(sources, names, analyses, {}, 1, commentary::Lang::En).unreviewed.size() == 1);
        // The source it reads told as it goes: the database a store's error would come from (#146).
        std::size_t reading = SIZE_MAX;
        (void)scanMistakes(sources, names, analyses, {}, 20, commentary::Lang::En, nullptr, &reading);
        CHECK(reading == 0);

        // Scanned again with its card: kept, the review unchanged.
        auto before                                = scan.record;
        before.exercises.begin()->second.card.reps = 3;
        before.exercises.begin()->second.card.last = 1000;
        before.exercises.begin()->second.since     = 77;
        auto again = scanMistakes(sources, names, analyses, before, 20, commentary::Lang::En);
        REQUIRE(again.record.exercises.size() == 1);
        CHECK(again.record.exercises.begin()->second.card.reps == 3);
        CHECK(again.record.exercises.begin()->second.since == 77);
        // Under no name of theirs: nothing, the card answered kept apart; the name back, the exercise comes back with
        // it.
        const auto nobody = scanMistakes(sources, {"Nobody"}, analyses, before, 20, commentary::Lang::En);
        CHECK(nobody.record.exercises.empty());
        REQUIRE(nobody.record.kept.size() == 1);
        const auto back = scanMistakes(sources, names, analyses, nobody.record, 20, commentary::Lang::En);
        REQUIRE(back.record.exercises.size() == 1);
        CHECK(back.record.exercises.begin()->second.card.reps == 3);
        CHECK(back.record.exercises.begin()->second.since == 77);
        CHECK(back.record.kept.empty());
        // Kept in the profile too.
        REQUIRE(writeMistakeRecord(dir / "kept", nobody.record));
        const auto read = readMistakeRecord(dir / "kept");
        REQUIRE(read.kept.size() == 1);
        CHECK(read.kept.begin()->second.card.reps == 3);
        CHECK(read.kept.begin()->second.since == 77);
        // The review gone: its exercise goes.
        REQUIRE(saveReviews(analyses, *first, {}));
        CHECK(scanMistakes(sources, names, analyses, before, 20, commentary::Lang::En).record.exercises.empty());
        // Cancelled: nothing said.
        std::atomic<bool> cancel{true};
        CHECK(scanMistakes(sources, names, analyses, before, 20, commentary::Lang::En, &cancel).cancelled);
    }
    fs::remove_all(dir);
}

TEST_CASE("mistakes: a guess scored 3, 2, 1 or 0, and the games guessed kept") {
    CHECK(guessPoints(true, 0.3, -5.0) == 3);
    CHECK(guessPoints(false, 0.31, 0.28) == 2);   // as good
    CHECK(guessPoints(false, 0.31, 0.11) == 2);   // 0.2 worse
    CHECK(guessPoints(false, 0.0, 0.8) == 2);     // better
    CHECK(guessPoints(false, 0.31, -0.04) == 1);  // 0.35 worse: playable
    CHECK(guessPoints(false, 0.31, -0.19) == 1);
    CHECK(guessPoints(false, 0.31, -0.5) == 0);
    CHECK(scorePawns(cp(-35)) == doctest::Approx(-0.35));
    CHECK(scorePawns(mate(3)) == doctest::Approx(99.7));
    CHECK(scorePawns(mate(-2)) == doctest::Approx(-99.8));
    // A mate in more moves is still as good; a mate given up for a few pawns is not.
    CHECK(guessPoints(false, scorePawns(mate(2)), scorePawns(mate(4))) == 2);
    CHECK(guessPoints(false, scorePawns(mate(2)), scorePawns(cp(500))) == 0);

    const ScratchDir dir("guess-record");
    CHECK_FALSE(lastGuessRecord(dir));
    appendGuessRecord(dir, {100, "A – B", true, 31, 45, 9, 15, 0.42});
    appendGuessRecord(dir, {200, "C – D", false, 12, 15, 3, 5, 0.1});
    const auto last = lastGuessRecord(dir);
    REQUIRE(last);
    CHECK(last->title == "C – D");
    CHECK_FALSE(last->white);
    CHECK(last->points == 12);
    CHECK(last->max == 15);
    fs::remove_all(dir);
}

TEST_CASE("mistakes: a profile file with values of the wrong type keeps its readable entries, the rest said (#136)") {
    const ScratchDir dir("mistakes-types");
    const auto game = readGame(forkPgn());
    auto found      = exercisesOf(game, forkReview(), chess::Color::BLACK, commentary::Lang::En);
    REQUIRE(found.size() == 1);
    MistakeRecord record;
    putExercises(record, found[0].game_key, "sig", found);
    record.answer_ms = 4200;
    record.kept[77]  = {};
    REQUIRE(writeMistakeRecord(dir, record));
    const auto file = dir / "exercises.json";
    const auto key  = fmt::format("{:016x}", found[0].key);
    // Values of another type than the one written: a boolean for a number, a text for a number, a number for a text, a
    // list for a number (an older or newer version, a hand edit). Each used to stop the program at launch.
    auto j                                   = nlohmann::json::parse(test::readFile(file));
    j["answer_ms"]                           = true;
    j["exercises"]["0000000000000001"]       = j["exercises"][key];
    j["exercises"]["0000000000000001"]["id"] = "seven";
    j["games"]["other"]                      = {{"review", 5}};
    j["kept"]["000000000000004d"]["since"]   = nlohmann::json::array({1});
    const auto written                       = j.dump();
    std::ofstream(file, std::ios::binary) << written;
    const auto before = Console::instance().added();
    MistakeRecord back;
    REQUIRE_NOTHROW(back = readMistakeRecord(dir));
    // The readable entries kept: the exercise and its game; the others set aside, said, the file kept as it was.
    REQUIRE(back.exercises.size() == 1);
    CHECK(back.exercises.begin()->second.key == found[0].key);
    CHECK(back.games.count(found[0].game_key) == 1);
    CHECK(back.games.count("other") == 0);
    CHECK(back.kept.empty());
    CHECK(back.answer_ms == 0);
    auto said = Console::instance().since(before);
    REQUIRE(said.size() == 1);
    CHECK(said[0].find("4 entries could not be read and were set aside") != std::string::npos);
    CHECK(said[0].find("exercises/0000000000000001") != std::string::npos);
    CHECK(said[0].find("exercises.json.unreadable") != std::string::npos);
    CHECK(test::readFile(dir / "exercises.json.unreadable") == written);
    // Written again, the record reads whole.
    REQUIRE(writeMistakeRecord(dir, back));
    const auto quiet = Console::instance().added();
    CHECK(readMistakeRecord(dir).exercises.size() == 1);
    CHECK(Console::instance().since(quiet).empty());
    // A file cut short: kept aside, said; the exercises are found again from the reviews.
    std::ofstream(file, std::ios::binary) << "{\"version\": 1, \"exercises\": {";
    const auto cut = Console::instance().added();
    CHECK(readMistakeRecord(dir).exercises.empty());
    said = Console::instance().since(cut);
    REQUIRE(said.size() == 1);
    CHECK(said[0].find("could not be read: kept as") != std::string::npos);
    CHECK(fs::exists(dir / "exercises.json.unreadable2"));

    // The games guessed: a line of the wrong type left out, said; the last readable one given.
    appendGuessRecord(dir, {100, "A – B", true, 31, 45, 9, 15, 0.42});
    std::ofstream(dir / "games.jsonl", std::ios::binary | std::ios::app)
        << R"({"at": 200, "title": "C – D", "points": "twelve", "max": 15})" << '\n';
    const auto guessed = Console::instance().added();
    const auto last    = lastGuessRecord(dir);
    REQUIRE(last);
    CHECK(last->title == "A – B");
    said = Console::instance().since(guessed);
    REQUIRE(said.size() == 1);
    CHECK(said[0].find("1 line could not be read and was left out") != std::string::npos);

    // A game's review whose key is not a text: no review, rather than an end of the program.
    REQUIRE(saveReview(dir / "analyses", game, forkReview()));
    const auto review = dir / "analyses" / (reviewKey(game) + ".json");
    auto r            = nlohmann::json::parse(test::readFile(review));
    r["key"]          = 5;
    std::ofstream(review, std::ios::binary) << r.dump();
    std::optional<GameReview> none;
    REQUIRE_NOTHROW(none = loadReview(dir / "analyses", game));
    CHECK_FALSE(none);
}

TEST_CASE("mistakes: an answer searched at the review's settings finds what the review found there") {
    // The mock remembers its searches since the last ucinewgame, as a hash table would: a search that does not start
    // from an empty table finds something else.
    ScopedEnv analysis("FCG_MOCK_ANALYSIS", "1");
    ScopedEnv hash("FCG_MOCK_HASH", "1");
    EngineEntry engine;
    engine.name     = "mover";
    engine.cmd      = randomMover();
    engine.protocol = "uci";
    // A short game reviewed as the Chess space reviews: nodes, one thread, an empty table each position.
    ReplayGame game;
    {
        chess::Board board;
        for (const char* uci : {"e2e4", "d7d5", "e4d5", "d8d5", "b1c3"}) {
            ReplayMove m;
            m.move  = chess::uci::uciToMove(board, uci);
            m.san   = chess::uci::moveToSan(board, m.move);
            m.mover = board.sideToMove();
            board.makeMove(m.move);
            game.moves.push_back(m);
        }
    }
    ReviewRunner runner;
    ReviewRunner::Setup setup;
    setup.engine       = engine;
    setup.game         = game;
    setup.ceiling_ms   = passTime(1);
    setup.reproducible = true;
    runner.start(setup);
    ReviewRunner::State st;
    uint64_t version = 0;
    const auto until = std::chrono::steady_clock::now() + 60s;
    while (std::chrono::steady_clock::now() < until) {
        (void)runner.state(st, version);
        if (!st.running) break;
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE_FALSE(st.running);
    REQUIRE(st.error.empty());
    REQUIRE(st.settled.positions.size() == 6);

    // The analyser the session judges with: the same engine, one thread, the review's hash, its nodes, a new game each
    // search; two positions one after the other give the review's own values.
    Analyser judge;
    AnalysisSettings settings;
    settings.engine  = engine.name;
    settings.threads = 1;
    settings.hash    = REVIEW_HASH_MB;
    settings.multipv = 1;
    judge.start(engine, settings);
    for (const int ply : {4, 2, 4}) {
        AnalysisPosition position{std::string(chess::constants::STARTPOS), {}, false};
        for (int i = 0; i < ply; ++i)
            position.moves.push_back(chess::uci::moveToUci(game.moves[static_cast<std::size_t>(i)].move));
        judge.analyse(position, SearchLimit{0, 0, st.settled.nodes}, true);
        const auto give_up = std::chrono::steady_clock::now() + 60s;
        while (std::chrono::steady_clock::now() < give_up) {
            judge.poll();
            if (judge.finished() && judge.position() == position) break;
            REQUIRE(judge.state() != Analyser::State::Failed);
            std::this_thread::sleep_for(5ms);
        }
        REQUIRE(judge.finished());
        REQUIRE_FALSE(judge.lines().empty());
        const auto& review = st.settled.positions[static_cast<std::size_t>(ply)];
        CAPTURE(ply);
        REQUIRE(review.score);
        REQUIRE(judge.lines().front().score);
        CHECK(judge.lines().front().score->value == review.score->value);
        CHECK(judge.bestmove() == review.best);
    }
    judge.stop();
}
