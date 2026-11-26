#include <doctest/doctest.hpp>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <chess.hpp>
#include <json.hpp>

#include "repertoires.hpp"
#include "test_support.hpp"
#include "training.hpp"

namespace fs = std::filesystem;
using namespace fcg;
using fcg::test::readFile;
using study::fsrs::Rating;

namespace {

std::vector<chess::Move> moves(const std::vector<std::string>& line) {
    chess::Board board;
    std::vector<chess::Move> out;
    for (const auto& san : line) {
        out.push_back(chess::uci::parseSan(board, san));
        board.makeMove<false>(out.back());
    }
    return out;
}

chess::Move next(const std::vector<std::string>& line, const std::string& san) {
    chess::Board board;
    for (const auto& s : line) board.makeMove<false>(chess::uci::parseSan(board, s));
    return chess::uci::parseSan(board, san);
}

const study::TrainingCard* cardAfter(const std::vector<study::TrainingCard>& cards,
                                     const std::vector<std::string>& line) {
    for (const auto& c : cards)
        if (c.line == line) return &c;
    return nullptr;
}

// A Black repertoire: 1. e4 c5 2. Nf3 d6, and 1. d4 Nf6.
std::size_t najdorf(RepertoireShelf& shelf) {
    const auto index = shelf.create("Najdorf", chess::Color::BLACK);
    REQUIRE(shelf.toggle(index, moves({"e4"}), next({"e4"}, "c5")));
    REQUIRE(shelf.toggle(index, moves({"e4", "c5", "Nf3"}), next({"e4", "c5", "Nf3"}, "d6")));
    REQUIRE(shelf.toggle(index, moves({"d4"}), next({"d4"}, "Nf6")));
    return index;
}

}  // namespace

TEST_CASE("training: the cards' memories kept beside the repertoire, read back the same, its PGN untouched") {
    const auto profile = fs::temp_directory_path() / "training-kept";
    fs::remove_all(profile);
    RepertoireShelf shelf(profile);
    const auto index  = najdorf(shelf);
    const auto& entry = shelf.entries()[index];
    const auto pgn    = readFile(entry.file);
    CHECK(trainingFileOf(entry.file) == profile / "repertoires" / "Najdorf.training.json");
    CHECK(reviewsFileOf(entry.file) == profile / "repertoires" / "Najdorf.reviews.jsonl");
    // Nothing kept yet: every card is new.
    CHECK(readTrainingFile(entry.file).memories.empty());

    const auto cards = study::trainingCards(entry.lines, entry.side);
    REQUIRE(cards.size() == 3);
    study::TrainingSession session(cards, {0, 1, 2}, {}, trainingScheduler());
    const int64_t now = 1'700'000'000;
    (void)session.grade(Rating::Good, "c5", now);
    session.next();
    (void)session.grade(Rating::Again, "", now + 9);
    TrainingFile kept;
    kept.memories  = session.memories();
    kept.answer_ms = 4200;
    REQUIRE(writeTrainingFile(entry.file, kept));
    appendReview(entry.file, cards[0].key, now, Rating::Good, 4200, "c5");
    appendReview(entry.file, cards[1].key, now + 9, Rating::Again, 9000, "");

    const auto read = readTrainingFile(entry.file);
    CHECK(read.answer_ms == 4200);
    REQUIRE(read.memories.size() == 2);
    CHECK(read.memories == kept.memories);
    const auto& first = read.memories.at(cards[0].key);
    CHECK(first.moves == "c5");
    CHECK(first.since == now);
    CHECK(first.card.state == study::fsrs::State::Review);
    CHECK(first.card.due == now + 2 * study::fsrs::DAY);
    CHECK(read.memories.at(cards[1].key).card.state == study::fsrs::State::Learning);
    // The repertoire's PGN is as it was.
    CHECK(readFile(entry.file) == pgn);
    // One line per answer.
    std::ifstream log(reviewsFileOf(entry.file));
    std::vector<nlohmann::json> lines;
    for (std::string line; std::getline(log, line);) lines.push_back(nlohmann::json::parse(line));
    REQUIRE(lines.size() == 2);
    CHECK(lines[0]["rating"] == 3);
    CHECK(lines[0]["played"] == "c5");
    CHECK(lines[1]["rating"] == 1);
    CHECK(lines[1]["ms"] == 9000);

    // The settings, kept in the profile.
    CHECK(readTrainingSettings(profile).advance_ms == 1000);
    CHECK(readTrainingSettings(profile).new_per_day == 20);
    writeTrainingSettings(profile, {0, 35});
    CHECK(readTrainingSettings(profile).advance_ms == 0);
    CHECK(readTrainingSettings(profile).new_per_day == 35);

    // Read again by another shelf, as at the next launch: the same cards, the same memories.
    RepertoireShelf again(profile);
    REQUIRE(again.entries().size() == 1);
    const auto counted =
        study::trainingCounts(study::trainingCards(again.entries()[0].lines, chess::Color::BLACK),
                              readTrainingFile(again.entries()[0].file).memories, {now + 60, now + 3600}, 20);
    CHECK(counted.cards == 3);
    CHECK(counted.fresh == 1);
    CHECK(counted.due == 1);  // the one answered wrong, due again in ten minutes
    CHECK(counted.known == 1);
}

TEST_CASE("training: a card whose move changed in the repertoire starts again, the others keep their memory") {
    const auto profile = fs::temp_directory_path() / "training-reset";
    fs::remove_all(profile);
    RepertoireShelf shelf(profile);
    const auto index = najdorf(shelf);
    auto cards       = study::trainingCards(shelf.entries()[index].lines, chess::Color::BLACK);
    study::TrainingSession session(cards, {0, 1, 2}, {}, trainingScheduler());
    const int64_t now = 1'700'000'000;
    for (int i = 0; i < 3; ++i) {
        (void)session.grade(Rating::Good, cards[static_cast<std::size_t>(i)].moves.front(), now);
        session.next();
    }
    const auto memories = session.memories();
    for (const auto& c : cards) CHECK(study::memoryOf(memories, c) != nullptr);

    // 1. d4 answered by 1... d5 now, in place of 1... Nf6.
    const auto revision = shelf.entries()[index].revision;
    CHECK_FALSE(shelf.toggle(index, moves({"d4"}), next({"d4"}, "Nf6")));
    CHECK(shelf.toggle(index, moves({"d4"}), next({"d4"}, "d5")));
    CHECK(shelf.entries()[index].revision != revision);
    cards = study::trainingCards(shelf.entries()[index].lines, chess::Color::BLACK);
    REQUIRE(cards.size() == 3);
    const auto* d4 = cardAfter(cards, {"d4"});
    REQUIRE(d4);
    CHECK(d4->moves == std::vector<std::string>{"d5"});
    CHECK(study::memoryOf(memories, *d4) == nullptr);
    CHECK(study::memoryOf(memories, *cardAfter(cards, {"e4"})) != nullptr);
    CHECK(study::memoryOf(memories, *cardAfter(cards, {"e4", "c5", "Nf3"})) != nullptr);
    const auto counts = study::trainingCounts(cards, memories, {now + 60, now + 3600}, 20);
    CHECK(counts.fresh == 1);
    CHECK(counts.known == 2);

    // A second move added to a position: the position's moves changed, the card starts again too.
    CHECK(shelf.toggle(index, moves({"e4"}), next({"e4"}, "e5")));
    cards = study::trainingCards(shelf.entries()[index].lines, chess::Color::BLACK);
    CHECK(cardAfter(cards, {"e4"})->moves == std::vector<std::string>{"c5", "e5"});
    CHECK(study::memoryOf(memories, *cardAfter(cards, {"e4"})) == nullptr);
}

TEST_CASE("training: the training day ends at 4 in the morning, local time") {
    const auto at = [](int days, int hour, int minute) {
        const auto now = std::time(nullptr);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        local.tm_mday += days;
        local.tm_hour  = hour;
        local.tm_min   = minute;
        local.tm_sec   = 0;
        local.tm_isdst = -1;
        return static_cast<int64_t>(std::mktime(&local));
    };
    CHECK(trainingDayEnd(at(0, 10, 0)) == at(1, 4, 0));
    CHECK(trainingDayEnd(at(0, 23, 59)) == at(1, 4, 0));
    CHECK(trainingDayEnd(at(0, 2, 30)) == at(0, 4, 0));  // after midnight: still the evening before's day
    CHECK(trainingDayEnd(at(0, 4, 0)) == at(1, 4, 0));
    CHECK(trainingDay(at(0, 10, 0)).now == at(0, 10, 0));
    // The training's steps: one of ten minutes to learn, one to relearn.
    const auto scheduler = trainingScheduler();
    CHECK(scheduler.settings().learning_steps == std::vector<int64_t>{600});
    CHECK(scheduler.settings().relearning_steps == std::vector<int64_t>{600});
    CHECK(scheduler.settings().retention == doctest::Approx(0.9));
}
