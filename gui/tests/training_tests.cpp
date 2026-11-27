// The repertoires' training kept in the profile (#76): the cards' memories beside the repertoire, a card started again
// when its moves change, the training day, the settings; the files read with their types checked (#136).

#include <doctest/doctest.hpp>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <chess.hpp>
#include <json.hpp>

#include "console.hpp"
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

TEST_CASE("training: a repertoire's training of the wrong types keeps its readable cards, the rest said (#136)") {
    const test::ScratchDir profile("training-types");
    RepertoireShelf shelf(profile);
    const auto index  = najdorf(shelf);
    const auto& entry = shelf.entries()[index];
    const auto cards  = study::trainingCards(entry.lines, entry.side);
    REQUIRE(cards.size() == 3);
    study::TrainingSession session(cards, {0, 1, 2}, {}, trainingScheduler());
    const int64_t now = 1'700'000'000;
    for (const auto& card : cards) {
        (void)session.grade(Rating::Good, card.moves.front(), now);
        session.next();
    }
    TrainingFile kept;
    kept.memories  = session.memories();
    kept.answer_ms = 4200;
    REQUIRE(writeTrainingFile(entry.file, kept));
    const auto file = trainingFileOf(entry.file);
    // Values of another type than the one written: a text for a time, a text for a date, a number for the moves, a card
    // whose key is no position's (an older or newer version, a hand edit). Each used to stop the program as soon as the
    // Openings page, the Home page or the Training page counted the repertoire's positions.
    const auto hex                         = [](uint64_t key) { return fmt::format("{:016x}", key); };
    auto j                                 = nlohmann::json::parse(test::readFile(file));
    j["answer_ms"]                         = "slow";
    j["cards"][hex(cards[1].key)]["due"]   = "tomorrow";
    j["cards"][hex(cards[2].key)]["moves"] = 7;
    j["cards"]["no-key"]                   = j["cards"][hex(cards[0].key)];
    const auto written                     = j.dump();
    std::ofstream(file, std::ios::binary) << written;
    const auto before = Console::instance().added();
    TrainingFile back;
    REQUIRE_NOTHROW(back = readTrainingFile(entry.file));
    // The readable card kept; the others set aside, said in the file's order, the file kept as it was.
    REQUIRE(back.memories.size() == 1);
    CHECK(back.memories.at(cards[0].key) == kept.memories.at(cards[0].key));
    CHECK(back.answer_ms == 0);
    auto bad = std::vector<std::string>{hex(cards[1].key), hex(cards[2].key)};
    std::sort(bad.begin(), bad.end());
    auto said = Console::instance().since(before);
    REQUIRE(said.size() == 1);
    CHECK(said[0].find("4 entries could not be read and were set aside") != std::string::npos);
    CHECK(said[0].find(fmt::format("(answer_ms, cards/{}, cards/{}, cards/no-key)", bad[0], bad[1])) !=
          std::string::npos);
    CHECK(said[0].find("Najdorf.training.json.unreadable") != std::string::npos);
    CHECK(test::readFile(fs::path(file).concat(".unreadable")) == written);
    // Written again, as the next answer writes it: the file reads whole, nothing said.
    REQUIRE(writeTrainingFile(entry.file, back));
    const auto quiet = Console::instance().added();
    CHECK(readTrainingFile(entry.file).memories == back.memories);
    CHECK(Console::instance().since(quiet).empty());
    // A file cut short: kept aside, said, rather than replaced in silence by the next answer.
    std::ofstream(file, std::ios::binary) << "{\"version\": 1, \"cards\": {";
    const auto cut = Console::instance().added();
    CHECK(readTrainingFile(entry.file).memories.empty());
    said = Console::instance().since(cut);
    REQUIRE(said.size() == 1);
    CHECK(said[0].find("could not be read: kept as") != std::string::npos);
    CHECK(fs::exists(fs::path(file).concat(".unreadable2")));
    // A repertoire never trained has no file: nothing to say.
    fs::remove(file);
    const auto none = Console::instance().added();
    CHECK(readTrainingFile(entry.file).memories.empty());
    CHECK(Console::instance().since(none).empty());
}

TEST_CASE("training: settings of the wrong type keep their defaults, the others read, what was set aside said (#136)") {
    const test::ScratchDir profile("training-settings-types");
    writeTrainingSettings(profile, {3000, 35, false});
    const auto file = profile / "training.json";
    // A text where milliseconds are, a number where a yes or no is: each used to stop the program at the first page
    // that read the training's settings.
    auto j             = nlohmann::json::parse(test::readFile(file));
    j["advance_ms"]    = "slow";
    j["guess_theory"]  = 1;
    const auto written = j.dump();
    std::ofstream(file, std::ios::binary) << written;
    const auto before = Console::instance().added();
    TrainingSettings back;
    REQUIRE_NOTHROW(back = readTrainingSettings(profile));
    CHECK(back.advance_ms == TrainingSettings{}.advance_ms);
    CHECK(back.new_per_day == 35);
    CHECK(back.guess_theory == TrainingSettings{}.guess_theory);
    auto said = Console::instance().since(before);
    REQUIRE(said.size() == 1);
    CHECK(said[0].find("2 entries could not be read and were set aside (advance_ms, guess_theory)") !=
          std::string::npos);
    CHECK(test::readFile(fs::path(file).concat(".unreadable")) == written);
    // Saved again, as a change of a setting saves them: they read whole, nothing said.
    writeTrainingSettings(profile, back);
    const auto quiet = Console::instance().added();
    CHECK(readTrainingSettings(profile).new_per_day == 35);
    CHECK(Console::instance().since(quiet).empty());
    // A file cut short: the defaults, the file kept aside and said.
    std::ofstream(file, std::ios::binary) << "{\"advance_ms\": 20";
    const auto cut = Console::instance().added();
    CHECK(readTrainingSettings(profile).new_per_day == TrainingSettings{}.new_per_day);
    said = Console::instance().since(cut);
    REQUIRE(said.size() == 1);
    CHECK(said[0].find("could not be read: kept as") != std::string::npos);
    CHECK(fs::exists(fs::path(file).concat(".unreadable2")));
}
