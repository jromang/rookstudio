#include <doctest/doctest.hpp>

#include <atomic>
#include <string>
#include <vector>

#include <chess.hpp>
#include <study/pgn_reader.hpp>
#include <study/repertoire.hpp>
#include <study/scan_board.hpp>
#include <study/training.hpp>
#include <study/tree.hpp>

#include "test_games.hpp"

using namespace fcg::study;
using namespace fcg::test;
using fcg::GameTree;

namespace {

GameTree linesOf(const std::string& movetext) {
    const auto read = readPgnGame("[Event \"Repertoire\"]\n\n" + movetext + "\n");
    REQUIRE(read.tree);
    REQUIRE_MESSAGE(read.error.empty(), read.error);
    return *read.tree;
}

uint64_t keyAfter(const std::vector<std::string>& line) {
    chess::Board board;
    for (const auto& san : line) board.makeMove<false>(chess::uci::parseSan(board, san));
    ScanBoard scan;
    REQUIRE(scan.set(board));
    return scan.key();
}

TreeBase treeBase(const std::string& name, const std::vector<Spec>& specs) {
    auto db = database(name, make(specs));
    TreeProgress progress;
    std::atomic<bool> cancel{false};
    const auto built = buildTree(*db, *db->snapshot(), {}, progress, cancel);
    REQUIRE_MESSAGE(built.error.empty(), built.error);
    return {db->snapshot(), Tree::open(db->folder())};
}

void repeat(std::vector<Spec>& specs, int times, const std::string& moves) {
    for (int i = 0; i < times; ++i) specs.push_back({moves, '1'});
}

// A Najdorf for Black: 2. Nc3 answered too, comments on some of Black's moves.
const char* NAJDORF =
    "1. e4 c5 {The Sicilian.} 2. Nf3 (2. Nc3 Nc6 {Closed.}) 2... d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 "
    "{The Najdorf.} *";

std::vector<uint64_t> keysOf(const std::vector<TrainingCard>& cards) {
    std::vector<uint64_t> keys;
    for (const auto& c : cards) keys.push_back(c.key);
    return keys;
}

TrainingCard card(uint64_t key, std::vector<std::string> moves, uint64_t games = 0) {
    TrainingCard c;
    c.key   = key;
    c.moves = std::move(moves);
    c.games = games;
    return c;
}

fsrs::Scheduler steps() {
    fsrs::Settings s;
    s.learning_steps   = {600};
    s.relearning_steps = {600};
    s.fuzz             = false;
    return fsrs::Scheduler(s);
}

}  // namespace

TEST_CASE("training: the cards of a repertoire: its side's positions with a move, in its variations") {
    const auto cards = trainingCards(linesOf(NAJDORF), chess::Color::BLACK);
    // Black moves after 1. e4, 2. Nf3, 2. Nc3, 3. d4, 4. Nxd4 and 5. Nc3; the positions where White moves are not
    // cards, nor the start.
    REQUIRE(cards.size() == 6);
    // The walk: the main line first, 2. Nc3 after it.
    CHECK(keysOf(cards) ==
          std::vector<uint64_t>{
              keyAfter({"e4"}), keyAfter({"e4", "c5", "Nf3"}), keyAfter({"e4", "c5", "Nf3", "d6", "d4"}),
              keyAfter({"e4", "c5", "Nf3", "d6", "d4", "cxd4", "Nxd4"}),
              keyAfter({"e4", "c5", "Nf3", "d6", "d4", "cxd4", "Nxd4", "Nf6", "Nc3"}), keyAfter({"e4", "c5", "Nc3"})});
    CHECK(cards[0].line == std::vector<std::string>{"e4"});
    CHECK(cards[0].moves == std::vector<std::string>{"c5"});
    CHECK(cards[0].comment == "The Sicilian.");
    CHECK(cards[4].moves == std::vector<std::string>{"a6"});
    CHECK(cards[4].comment == "The Najdorf.");
    CHECK(cards[5].line == std::vector<std::string>{"e4", "c5", "Nc3"});
    CHECK(cards[5].moves == std::vector<std::string>{"Nc6"});
    CHECK(cards[5].comment == "Closed.");
    CHECK(cards[1].comment.empty());

    // The same lines for White: White's positions, the start among them.
    const auto white = trainingCards(linesOf(NAJDORF), chess::Color::WHITE);
    CHECK(white.size() == 5);  // the start, after 1... c5, 2... d6, 3... cxd4 and 4... Nf6; not after 2... Nc6 nor
                               // 5... a6, where the lines end
    CHECK(white.front().line.empty());
    CHECK(white.front().moves == std::vector<std::string>{"e4"});
    // One of White's moves to choose at 1... c5: both are right.
    CHECK(white[1].moves == std::vector<std::string>{"Nf3", "Nc3"});
}

TEST_CASE("training: a position the lines reach by two orders is one card, with every move of both") {
    const auto lines = linesOf("1. d4 (1. c4 Nf6 2. d4 e6 3. g3) 1... Nf6 2. c4 e6 3. Nf3 (3. Nc3 Bb4) *");
    const auto cards = trainingCards(lines, chess::Color::WHITE);
    // The start, after 1. d4 Nf6, after 1. c4 Nf6, and the position after 2... e6 by either order once; after
    // 3. Nc3 Bb4 the line ends: no move of White's, no card.
    REQUIRE(cards.size() == 4);
    CHECK(cards[0].moves == std::vector<std::string>{"d4", "c4"});
    CHECK(cards[2].key == keyAfter({"c4", "Nf6", "d4", "e6"}));
    CHECK(cards[2].line == std::vector<std::string>{"d4", "Nf6", "c4", "e6"});  // the first order the walk meets
    CHECK(cards[2].moves == std::vector<std::string>{"Nf3", "g3", "Nc3"});
    CHECK(cards[3].line == std::vector<std::string>{"c4", "Nf6"});
}

TEST_CASE("training: the grade comes from the answer") {
    CHECK(gradeOf(true, false, 4) == fsrs::Rating::Good);
    CHECK(gradeOf(true, false, 20) == fsrs::Rating::Good);
    CHECK(gradeOf(true, false, 20.5) == fsrs::Rating::Hard);  // more than 20 s
    CHECK(gradeOf(true, true, 3) == fsrs::Rating::Hard);      // after a hint
    CHECK(gradeOf(false, false, 2) == fsrs::Rating::Again);
    CHECK(gradeOf(false, true, 40) == fsrs::Rating::Again);
}

TEST_CASE("training: the opponent's moves the most played first, and the cards' lines and order with them") {
    std::vector<Spec> specs;
    repeat(specs, 30, "1. e4 c5 2. Nc3 Nc6 3. g3");
    repeat(specs, 10, "1. e4 c5 2. Nf3 d6 3. d4");
    const auto base      = treeBase("training-opponent", specs);
    const GameTree lines = linesOf(NAJDORF);
    const Repertoire repertoire(lines, chess::Color::BLACK);
    const auto plan = trainingPlan(repertoire, treeMoves({base}));
    CHECK(plan.games.at(keyAfter({"e4"})) == 40);
    CHECK(plan.games.at(keyAfter({"e4", "c5", "Nc3"})) == 30);
    CHECK(plan.games.at(keyAfter({"e4", "c5", "Nf3"})) == 10);
    CHECK_FALSE(plan.games.contains(keyAfter({"e4", "c5", "Nf3", "d6", "d4", "cxd4"})));  // no game goes there

    // After 1. e4 c5, the repertoire answers 2. Nf3 first, the games play 2. Nc3 three times as often.
    const auto* sicilian = repertoire.at(keyAfter({"e4", "c5"}));
    REQUIRE(sicilian);
    const auto order = opponentOrder(*sicilian, plan);
    REQUIRE(order.size() == 2);
    CHECK(order[0]->san == "Nc3");
    CHECK(order[1]->san == "Nf3");
    // Without the databases, the repertoire's order.
    const auto plain = opponentOrder(*sicilian, {});
    CHECK(plain[0]->san == "Nf3");

    // The walk plays 2. Nc3 first: its card comes second, the main line's after it.
    const auto cards = trainingCards(lines, chess::Color::BLACK, plan);
    REQUIRE(cards.size() == 6);
    CHECK(cards[1].key == keyAfter({"e4", "c5", "Nc3"}));
    CHECK(cards[1].games == 30);
    CHECK(cards[2].key == keyAfter({"e4", "c5", "Nf3"}));
    CHECK(cards[2].games == 10);
    CHECK(cards[0].games == 40);
    CHECK(cards[5].games == 0);
}

TEST_CASE("training: a session's cards: those due in the walk's order, then the new ones the most met first") {
    std::vector<TrainingCard> cards = {card(1, {"e4"}, 500), card(2, {"Nf3"}, 50), card(3, {"d4"}, 900),
                                       card(4, {"c4"}, 50),  card(5, {"g3"}, 0),   card(6, {"b3"}, 700)};
    const TrainingDay day{1'000'000, 1'000'000 + 3600};
    Memories memories;
    const auto learnt = [&](uint64_t key, const std::string& moves, int64_t due, int64_t since) {
        CardMemory m;
        m.card.state     = fsrs::State::Review;
        m.card.stability = 5;
        m.card.last      = day.now - 5 * fsrs::DAY;
        m.card.due       = due;
        m.moves          = moves;
        m.since          = since;
        memories[key]    = m;
    };
    learnt(6, "b3", day.now - 10, day.now - 30 * fsrs::DAY);  // due
    learnt(2, "Nf3", day.end - 1, day.now - 30 * fsrs::DAY);  // due later today
    learnt(1, "e4", day.end + 2 * fsrs::DAY, day.now - 60);   // known, begun today

    // New: cards 3, 4 and 5, the most met first, the walk's order between equals.
    CHECK(sessionOrder(cards, memories, day, 10, true) == std::vector<std::size_t>{1, 5, 2, 3, 4});
    CHECK(sessionOrder(cards, memories, day, 2, true) == std::vector<std::size_t>{1, 5, 2, 3});
    CHECK(sessionOrder(cards, memories, day, 2, false) == std::vector<std::size_t>{2, 3});
    CHECK(sessionOrder(cards, memories, day, 10, true, {6, 3}) == std::vector<std::size_t>{1, 3, 4});

    const auto counts = trainingCounts(cards, memories, day, 2);
    CHECK(counts.cards == 6);
    CHECK(counts.due == 2);
    CHECK(counts.fresh == 3);
    CHECK(counts.today == 1);  // two a day, one begun today
    CHECK(counts.known == 1);
    CHECK(counts.tomorrow == 0);
    CHECK(counts.three_days == 1);
    CHECK(counts.week == 1);
    CHECK(trainingCounts(cards, memories, day, 20).today == 3);

    // A move changed in the repertoire: the card is new again.
    cards[5].moves = {"b4"};
    CHECK(memoryOf(memories, cards[5]) == nullptr);
    CHECK(trainingCounts(cards, memories, day, 20).fresh == 4);
    CHECK(sessionOrder(cards, memories, day, 2, true) == std::vector<std::size_t>{1, 2, 5});
    // The same moves in another order are the same moves.
    cards[5].moves = {"b3"};
    CHECK(memoryOf(memories, cards[5]) != nullptr);
    CHECK(movesKey({"Nf3", "c4", "b3"}) == movesKey({"c4", "b3", "Nf3"}));
}

TEST_CASE("training: a session asks a card answered wrong again a few cards later, and keeps the first answers") {
    std::vector<TrainingCard> cards;
    for (uint64_t k = 1; k <= 5; ++k) cards.push_back(card(k, {"e4"}));
    TrainingSession session(cards, {0, 1, 2, 3, 4}, {}, steps());
    const int64_t now = 2'000'000;
    CHECK(session.total() == 5);
    CHECK(session.asked() == 1);
    REQUIRE(session.current() == 0);
    CHECK(session.right("e4"));
    CHECK_FALSE(session.right("d4"));
    // Wrong: asked again three cards later, then tomorrow once right.
    auto graded = session.grade(fsrs::Rating::Again, "d4", now);
    CHECK(graded.again);
    CHECK(graded.after.card.due == now + 600);
    CHECK(graded.after.since == now);
    CHECK(session.done() == 0);
    CHECK(session.asked() == 1);  // the card answered still counts once
    session.next();
    CHECK(session.asked() == 2);
    std::vector<std::size_t> asked;
    while (const auto c = session.current()) {
        asked.push_back(*c);
        if (*c == 0) {
            CHECK(session.askedAgain());
            CHECK(session.asked() == 4);  // asked again: no new position
        }
        graded = session.grade(fsrs::Rating::Good, "e4", now + 60);
        CHECK_FALSE(graded.again);
        session.next();
    }
    CHECK(asked == std::vector<std::size_t>{1, 2, 3, 0, 4});
    CHECK(session.done() == 5);
    // The first answer of each card, the wrong one kept for card 0.
    REQUIRE(session.outcomes().size() == 5);
    CHECK(session.outcomes()[0].card == 0);
    CHECK(session.outcomes()[0].rating == fsrs::Rating::Again);
    CHECK(session.outcomes()[0].played == "d4");
    CHECK(session.memories().at(cards[1].key).card.due == now + 60 + 2 * fsrs::DAY);
    CHECK(session.memories().at(cards[0].key).moves == "e4");
    // Card 0, right after the step: tomorrow.
    const auto& first = session.memories().at(cards[0].key);
    CHECK(first.card.due - first.card.last == fsrs::DAY);

    // The last card wrong: asked again at once.
    TrainingSession alone(cards, {2}, {}, steps());
    (void)alone.grade(fsrs::Rating::Again, "", now);
    alone.next();
    CHECK(alone.current() == 2);
    CHECK(alone.askedAgain());
}

TEST_CASE("training: a move added to the repertoire during a session keeps the card's memory, Ctrl+Z too") {
    std::vector<TrainingCard> cards = {card(7, {"e6"})};
    TrainingSession session(cards, {0}, {}, steps());
    const auto graded = session.grade(fsrs::Rating::Again, "Nbd7", 3'000'000);
    CHECK(graded.after.moves == "e6");
    session.addMove(0, "Nbd7");
    CHECK(session.cards()[0].moves == std::vector<std::string>{"e6", "Nbd7"});
    CHECK(session.right("Nbd7"));
    CHECK(session.memories().at(7).moves == "Nbd7 e6");
    CHECK(memoryOf(session.memories(), session.cards()[0]) != nullptr);
    session.takeBack(0, "Nbd7");
    CHECK_FALSE(session.right("Nbd7"));
    CHECK(session.memories().at(7).moves == "e6");
    CHECK(memoryOf(session.memories(), session.cards()[0]) != nullptr);
}
