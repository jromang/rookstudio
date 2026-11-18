#pragma once

// The story of a game, from its review: what the opening left, where the game turned, what each side kept getting wrong
// and should work on, how it ended (the prototype's crates/commentary/src/story.rs).
//
// Like the comments on single moves, it is made of facts only: the review's verdicts and evaluations, the formations on
// the board, and the facts of the moves it singles out, told in the words of their comments.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gather.hpp"
#include "notation.hpp"
#include "phrases.hpp"
#include "review.hpp"

namespace fcg::commentary {

// One paragraph of the story, with the move it is about: the ply of the position after it.
struct Passage {
    std::string text;
    std::optional<std::size_t> ply;
    bool lesson                           = false;  // what a side should work on: workOn() gives it on its own
    bool operator==(const Passage&) const = default;
};

// The story of the reviewed game, in a few paragraphs; nothing while the review is not complete, or for a game too
// short to have one. `result` is the game's: "1-0", "0-1", "1/2-1/2" or "*".
[[nodiscard]] std::vector<Passage> story(const MainLine& line, std::string_view result, const GameReport& report,
                                         const Analyses& analyses, const Tablebases* tables, Lang lang,
                                         PieceLetters letters);

// What one side should work on, from its mistakes: the story's lessons on their own (mockup M5, board 4). The kinds
// of mistakes it made most, each twice at least or the one that decided the game, with the plies of the moves that
// show them; and the advice the first one that has one calls for, empty when none.
struct WorkOn {
    Color side = Color::White;
    struct Topic {
        std::string name;  // « les fourchettes », "rook endgames"
        std::vector<std::size_t> plies;
        bool operator==(const Topic&) const = default;
    };
    std::vector<Topic> topics;
    std::string advice;
    bool operator==(const WorkOn&) const = default;
};

// What each side that made mistakes should work on, White first; nothing where story() tells nothing. `result` as
// for story(): the mistake that decided the game is a lesson by itself.
[[nodiscard]] std::vector<WorkOn> workOn(const MainLine& line, std::string_view result, const GameReport& report,
                                         const Analyses& analyses, const Tablebases* tables, Lang lang,
                                         PieceLetters letters);

namespace story_detail {

// A tactic by its kind, as a player trains it.
enum class Motif : uint8_t {
    Fork,
    Pin,
    Skewer,
    Discovery,
    Guard,  // a defender drawn away, overloaded, taken first, or a piece lured
    Trapped,
    BackRank,
};

// What a mistake overlooked.
struct Oversight {
    enum class Kind : uint8_t {
        EnPrise,  // material left to be taken
        Tactic,   // a tactic allowed, by its kind when it has one
        King,     // the king left in danger
        Missed,   // a chance to win something not taken
    };
    Oversight() = default;
    Oversight(Kind k, std::optional<Motif> m = std::nullopt) : kind(k), motif(m) {}
    Kind kind = Kind::EnPrise;
    std::optional<Motif> motif;  // Tactic: its kind, when it has one
    bool operator==(const Oversight&) const = default;
};

// What a mistake overlooked, by what its facts say of it; `takes` is whether the answer to it takes something.
[[nodiscard]] std::optional<Oversight> oversight(const MoveFacts& facts, bool takes);

}  // namespace story_detail

}  // namespace fcg::commentary
