#pragma once

// Training a repertoire with spaced repetition (#76, mockup M11, board 1). Each position of a repertoire where its
// side moves, and where it has a move ready, is a card: asked by playing the opponent's moves along the repertoire up
// to it, answered by playing one of the repertoire's moves there. FSRS (fsrs.hpp) tells when to ask it again from how
// the answer went, graded without a button: right at the first try Good, right after a hint or after more than 20 s
// Hard, wrong or "I don't know" Again.
//
// What the databases play orders the training: the new positions the most often met first, and where the repertoire
// answers several moves of the opponent's, the most played first. A walk along the repertoire from its start, its own
// moves in its order and the opponent's most played first, gives each card the line it is shown with (the first that
// reaches it, for a position its lines reach by several orders) and the order the cards due come in.
//
// What is kept of a card (CardMemory) is its FSRS memory and the repertoire's moves it was learnt with: when those
// moves change, the card is new again.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <chess.hpp>

#include "fsrs.hpp"
#include "game.hpp"
#include "repertoire.hpp"

namespace fcg::study {

struct TrainingCard {
    uint64_t key = 0;  // the position (Polyglot)
    std::string fen;
    std::vector<std::string>
        moves;                      // the repertoire's moves here, in SAN, its first choice first: any of them is right
    std::vector<std::string> line;  // the moves from the repertoire's start to it, in SAN, along the walk
    std::string comment;            // what the repertoire says of its move here (the first of them that has a comment)
    uint64_t games = 0;             // the games of the databases that reach it
};

// What the databases say of a repertoire's positions: the games that reach each one, and at the opponent's, how often
// each of the repertoire's moves there was played (by its code on the position's board).
struct TrainingPlan {
    std::unordered_map<uint64_t, uint64_t> games;
    std::unordered_map<uint64_t, std::unordered_map<int, uint64_t>> played;
};
[[nodiscard]] TrainingPlan trainingPlan(const Repertoire& repertoire, const MovesAt& moves);

// The order the repertoire's moves at an opponent's position are played in: the most played in the databases first,
// those they never played after them, in the repertoire's order.
[[nodiscard]] std::vector<const RepertoireMove*> opponentOrder(const RepertoirePosition& position,
                                                               const TrainingPlan& plan);

// The cards of the repertoire `lines` of `side`, in the order the walk meets them, each with its line.
[[nodiscard]] std::vector<TrainingCard> trainingCards(const GameTree& lines, chess::Color side,
                                                      const TrainingPlan& plan = {}, bool chess960 = false);

// The grade of an answer: right at the first try Good; right after a hint, or after more than HARD_AFTER seconds,
// Hard; wrong, or "I don't know", Again.
inline constexpr double HARD_AFTER = 20.0;
[[nodiscard]] fsrs::Rating gradeOf(bool right, bool hint, double seconds);

// What the profile keeps of a card.
struct CardMemory {
    fsrs::Card card;
    std::string moves;                             // the repertoire's moves when it was last reviewed (movesKey)
    int64_t since                            = 0;  // its first review, seconds since the epoch
    bool operator==(const CardMemory&) const = default;
};
using Memories = std::unordered_map<uint64_t, CardMemory>;  // by position

// Moves as a card's memory keeps them: sorted, a space between two.
[[nodiscard]] std::string movesKey(std::vector<std::string> sans);
// The memory of a card: none when it was never reviewed, or when the repertoire's moves there changed since.
[[nodiscard]] const CardMemory* memoryOf(const Memories& memories, const TrainingCard& card);

// The time and the end of the training day that holds it (seconds since the epoch).
struct TrainingDay {
    int64_t now = 0;
    int64_t end = 0;
};

struct TrainingCounts {
    std::size_t cards = 0;
    std::size_t due   = 0;  // reviewed before, due by the end of the day
    std::size_t fresh = 0;  // never reviewed (or new again)
    std::size_t today = 0;  // the new ones for today: the day's limit less those begun today, as many as there are
    std::size_t known = 0;  // reviewed, due after today
    // Of those, due by the end of tomorrow, of the third day, of the seventh.
    std::size_t tomorrow = 0, three_days = 0, week = 0;
};
[[nodiscard]] TrainingCounts trainingCounts(const std::vector<TrainingCard>& cards, const Memories& memories,
                                            const TrainingDay& day, std::size_t new_per_day);

// The cards of a session, by their index: those due today in the walk's order (unless `with_due` is false), then
// `fresh` new ones, the most met in the databases first. The positions of `left_out` are not asked.
[[nodiscard]] std::vector<std::size_t> sessionOrder(const std::vector<TrainingCard>& cards, const Memories& memories,
                                                    const TrainingDay& day, std::size_t fresh, bool with_due,
                                                    const std::unordered_set<uint64_t>& left_out = {});

// A session: its cards asked one after the other. A card FSRS brings back within the hour (a learning or relearning
// step: answered wrong, or right with difficulty while it is new) is asked again a few cards later in the session.
class TrainingSession {
   public:
    static constexpr int64_t AGAIN_WITHIN    = 3600;  // seconds: due again within this, asked again in the session
    static constexpr std::size_t AGAIN_AFTER = 3;     // cards between

    TrainingSession(std::vector<TrainingCard> cards, std::vector<std::size_t> order, Memories memories,
                    fsrs::Scheduler scheduler);

    [[nodiscard]] const std::vector<TrainingCard>& cards() const noexcept { return cards_; }
    [[nodiscard]] const Memories& memories() const noexcept { return memories_; }
    [[nodiscard]] const fsrs::Scheduler& scheduler() const noexcept { return scheduler_; }
    // The card asked, none at the end.
    [[nodiscard]] std::optional<std::size_t> current() const;
    // The card asked was answered earlier in the session, and comes back.
    [[nodiscard]] bool askedAgain() const;
    [[nodiscard]] std::size_t total() const noexcept { return total_; }  // the cards of the session
    [[nodiscard]] std::size_t done() const noexcept { return done_; }    // those it is done with
    // The cards asked so far, the one asked counted: "position 3 of 17".
    [[nodiscard]] std::size_t asked() const;
    // Whether the move (SAN) is one of the repertoire's at the card asked.
    [[nodiscard]] bool right(const std::string& san) const;

    struct Graded {
        CardMemory before, after;
        bool again = false;  // asked again later in the session
    };
    // The card asked graded at `now`, its memory updated; `played` is the move played (SAN), empty for "I don't know".
    Graded grade(fsrs::Rating rating, const std::string& played, int64_t now);
    // On to the next card.
    void next();

    // The first answer of each card, in the order they were asked.
    struct Outcome {
        std::size_t card    = 0;
        fsrs::Rating rating = fsrs::Rating::Good;
        std::string played;
    };
    [[nodiscard]] const std::vector<Outcome>& outcomes() const noexcept { return outcomes_; }

    // A move added to the repertoire at a card: right there from now on, the card's memory following it rather than
    // starting again; and taken back.
    void addMove(std::size_t card, const std::string& san);
    void takeBack(std::size_t card, const std::string& san);

   private:
    std::vector<TrainingCard> cards_;
    std::deque<std::size_t> queue_;
    Memories memories_;
    fsrs::Scheduler scheduler_;
    std::size_t total_ = 0, done_ = 0;
    std::vector<Outcome> outcomes_;
    std::unordered_set<std::size_t> answered_;
    std::optional<std::size_t> back_;  // the card graded, to be asked again
};

}  // namespace fcg::study
