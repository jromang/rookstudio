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
        // The kind of lesson, the same in every language: "en_prise", "fork", "pin", "skewer", "discovery", "guard",
        // "trapped", "back_rank", "tactics", "king", "missed", "conversion", "pawn_endgame", "rook_endgame",
        // "queen_endgame", "minor_endgame", "bishops_endgame", "endgame", "opening" (the exercises of #81).
        std::string key;
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

// What a passage of the story is told from: the language model tells it again from the same facts (#62,
// story_model.hpp).
struct Source {
    enum class Kind : uint8_t {
        Opening,  // the opening's name, how the sides stand when it is over (`move`, its last move), the pawns
                  // (`facts`)
        Swing,    // a mistake that made the course of the game (`move`), and what it did (`swing`)
        Gradual,  // the costliest mistake of a game decided little by little (`move`)
        Plan,     // a plan the books have a name for (`facts`), carried out by `move`
        Endgame,  // the endgame the game went into, from `move` on, and the pawn majorities taken into it (`facts`)
        Lesson,   // what a side should work on
        End,      // how the game ended; `move`, the last one, when it was reviewed
    };
    enum class Swing : uint8_t {
        Lead,  // the first clear advantage of the game, given away
        Slip,  // a clear advantage let go
        Turn,  // after it, its side never came back
    };
    Kind kind              = Kind::End;
    const MoveReport* move = nullptr;  // in the report the story is told from
    Swing swing            = Swing::Turn;
    std::vector<Fact> facts;
    std::optional<Color> mated;  // End: the side that gave mate
    // Opening, Endgame, End: the facts in sentences, as the model is told them: the passage's own, a move named where
    // the passage gives its number ("into a rook endgame with 34.Rxd5" for "on move 34").
    std::vector<std::string> said;
};

struct Told {
    Passage passage;
    Source source;
};

// The story's passages, each with what it is told from; story() gives the passages alone.
[[nodiscard]] std::vector<Told> told(const MainLine& line, std::string_view result, const GameReport& report,
                                     const Analyses& analyses, const Tablebases* tables, Lang lang,
                                     PieceLetters letters);

}  // namespace story_detail

}  // namespace fcg::commentary
