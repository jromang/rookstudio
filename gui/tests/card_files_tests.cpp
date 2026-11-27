// The files that keep FSRS cards, and the JSON lines of the answers, byte for byte (#117): the repertoire's training,
// the endgames, the puzzles and the mistakes each wrote and read a card's fields with code of its own before one
// reader and one writer served them all, and their files stayed the same.

#include <doctest/doctest.hpp>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>

#include <tour/util.hpp>

#include "endgames.hpp"
#include "engine_play.hpp"
#include "mistakes.hpp"
#include "puzzles.hpp"
#include "test_support.hpp"
#include "training.hpp"

using namespace fcg;
using fcg::test::readFile;
using fcg::test::ScratchDir;

namespace {

// A card of each state, every field set.
study::fsrs::Card cardOf(study::fsrs::State state, int k) {
    study::fsrs::Card c;
    c.state      = state;
    c.step       = k % 2;
    c.stability  = 12.5 + k;
    c.difficulty = 4.25 + k / 8.0;
    c.due        = 1764000000 + k * 86400;
    c.last       = 1763000000 + k;
    c.reps       = 7 + k;
    c.lapses     = k;
    return c;
}

}  // namespace

TEST_CASE("cards: the files of the four kinds of cards and the answers' lines, byte for byte, read back the same") {
    const ScratchDir dir("card-files");
    using study::fsrs::State;
    const auto learning = cardOf(State::Learning, 1), review = cardOf(State::Review, 2);
    const auto relearning = cardOf(State::Relearning, 3);

    // The repertoire's training.
    TrainingFile training;
    training.answer_ms               = 4200;
    training.memories[0x1234abcdULL] = {review, "e2e4 e7e5", 1762000000};
    training.memories[0x00ff00ffULL] = {learning, "d2d4", 1762000001};
    training.memories[0xdeadbeefULL] = {relearning, "", 0};
    REQUIRE(writeTrainingFile(dir / "Najdorf.pgn", training));
    appendReview(dir / "Najdorf.pgn", 0x1234abcdULL, 1763000000, study::fsrs::Rating::Good, 3100, "e7e5");
    appendReview(dir / "Najdorf.pgn", 0x00ff00ffULL, 1763000060, study::fsrs::Rating::Again, 900, "");

    // The endgames.
    EndgameRecord endgames;
    endgames.answer_ms = 5100;
    EndgameCard rook;
    rook.fen                              = "8/8/8/8/8/4k3/8/4K2R w K - 0 1";
    rook.entry                            = "KRK";
    rook.title                            = "Rook and king";
    rook.mate                             = true;
    rook.card                             = review;
    rook.since                            = 1761000000;
    endgames.cards[positionKey(rook.fen)] = rook;
    EndgameCard pawn;
    pawn.fen                              = "8/8/8/8/8/4k3/4P3/4K3 w - - 0 1";
    pawn.goal                             = EndgameGoal::Draw;
    pawn.card                             = relearning;
    endgames.cards[positionKey(pawn.fen)] = pawn;
    REQUIRE(writeEndgameRecord(dir / "endgames", endgames));

    // The puzzles.
    std::map<std::string, PuzzleCard> puzzles;
    PuzzleCard fork;
    fork.puzzle.id          = "00sHx";
    fork.puzzle.fen         = "q3k1nr/1pp1nQpp/3p4/1P2p3/4P3/B1PP1b2/B5PP/5K2 b k - 0 17";
    fork.puzzle.moves       = {"e8d7", "a2e6", "d7d8", "f7f8"};
    fork.puzzle.rating      = 1760;
    fork.puzzle.themes      = {"mate", "mateIn2", "middlegame"};
    fork.puzzle.openings    = {"Italian_Game"};
    fork.card               = learning;
    fork.since              = 1760000000;
    puzzles[fork.puzzle.id] = fork;
    REQUIRE(writePuzzleCards(dir / "puzzles", puzzles));
    PuzzleResult result;
    result.id     = "00sHx";
    result.at     = 1763000000;
    result.rating = 1760;
    result.before = 1500.25;
    result.after  = 1512.5;
    result.themes = {"mate", "mateIn2"};
    result.ms     = 7300;
    appendPuzzleResult(dir / "puzzles", result);

    // The mistakes: an exercise's card, and a card kept of an exercise gone.
    MistakeRecord mistakes;
    mistakes.answer_ms = 6200;
    MistakeExercise exercise;
    exercise.start_fen      = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1";
    exercise.game_key       = "0123456789abcdef";
    exercise.played         = "f7f6";
    exercise.best           = "e7e5";
    exercise.card           = review;
    exercise.since          = 1762500000;
    const uint64_t key      = exercise.board().hash();  // what the reader checks it against
    mistakes.exercises[key] = exercise;
    mistakes.kept[0x43ULL]  = {relearning, 1762600000};
    REQUIRE(writeMistakeRecord(dir / "mistakes", mistakes));
    appendMistakeAnswer(dir / "mistakes", key, 1763000000, study::fsrs::Rating::Hard, 4100, "e7e5");
    GuessRecord guess;
    guess.at     = 1763000100;
    guess.title  = "Carlsen - Nakamura";
    guess.points = 31;
    guess.max    = 40;
    guess.same   = 12;
    guess.moves  = 20;
    guess.loss   = 0.375;
    appendGuessRecord(dir / "mistakes", guess);

    // A game played against an engine, a line of its own file.
    PlayedGame played;
    played.at       = 1763000200;
    played.engine   = "Stockfish 17";
    played.strength = "1500";
    played.result   = "1-0";
    played.plies    = 61;
    appendPlayedGame(dir / "play", played);

    // The bytes: those the code wrote before #117 gave one reader and one writer to the cards (FNV-1a of each file).
    // With FCG_CARD_FILES_DUMP set to a folder, the files are copied there, to be compared whole.
    const std::pair<const char*, uint64_t> files[] = {
        {"Najdorf.training.json", 4616623765967435076ULL},   {"Najdorf.reviews.jsonl", 4052777614721613732ULL},
        {"endgames/record.json", 7567317260552413644ULL},    {"puzzles/review.json", 16071130860804472564ULL},
        {"puzzles/results.jsonl", 11348817826163931813ULL},  {"mistakes/exercises.json", 3276369389705110813ULL},
        {"mistakes/answers.jsonl", 14161968178726414081ULL}, {"mistakes/games.jsonl", 14819683291396412524ULL},
        {"play/games.jsonl", 10895490116538870040ULL},
    };
    for (const auto& [name, hash] : files) {
        const std::string file = name;
        CAPTURE(file);
        const auto bytes = readFile(dir / file);
        REQUIRE_FALSE(bytes.empty());
        CHECK(fnv1a(bytes) == hash);
        if (const char* dump = std::getenv("FCG_CARD_FILES_DUMP")) {
            std::error_code ec;
            const auto to = std::filesystem::path(dump) / file;
            std::filesystem::create_directories(to.parent_path(), ec);
            std::filesystem::copy_file(dir / file, to, std::filesystem::copy_options::overwrite_existing, ec);
        }
    }

    // And read back the same.
    CHECK(readTrainingFile(dir / "Najdorf.pgn").memories == training.memories);
    CHECK(readEndgameRecord(dir / "endgames").cards == endgames.cards);
    CHECK(readPuzzleRecord(dir / "puzzles").cards == puzzles);
    const auto again = readMistakeRecord(dir / "mistakes");
    REQUIRE(again.exercises.count(key) == 1);
    CHECK(again.exercises.at(key).card == review);
    REQUIRE(again.kept.count(0x43ULL) == 1);
    CHECK(again.kept.at(0x43ULL).card == relearning);
    CHECK(again.kept.at(0x43ULL).since == 1762600000);
}
