#pragma once

// Questions about the position, asked in words (the prototype's crates/app/src/question.rs). The language model turns
// a question into an action of a closed list, held to a grammar that names only the legal moves; the engine carries the
// action out, and the model writes the answer as it writes a comment, held to the facts.

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "commentator.hpp"
#include "eval.hpp"
#include "gather.hpp"
#include "notation.hpp"
#include "oracle.hpp"
#include "phrases.hpp"
#include "references.hpp"
#include "tablebase.hpp"

namespace fcg::commentary {

// What a question asks for.
struct Action {
    enum class Kind : uint8_t {
        Explain,  // why the move on the board is good or bad
        Best,     // what to play
        Threat,   // what the side not to move threatens
        Try,      // what a move would do
        Compare,  // which of two moves is better
        // What the databases hold (references.hpp), the answers written from their numbers alone:
        Games,      // the games that reached the position
        Played,     // the moves they played from it and how each scored; with a move, that one's numbers
        Opening,    // the opening's name
        Tablebase,  // what the endgame tablebases say
    };
    Action() = default;
    Action(Kind k, std::optional<Move> a = std::nullopt, std::optional<Move> b = std::nullopt)
        : kind(k), first(a), second(b) {}
    Kind kind = Kind::Explain;
    std::optional<Move> first, second;  // Try, Played: the move; Compare: the two
    bool operator==(const Action&) const = default;
};

// An answer, and the moves it is about: the board shows them on request.
struct Answered {
    std::string text;
    // The moves the answer is about, in the position they are played in.
    std::vector<Move> moves;
    // The moves are the opponent's threat: played as if it were their turn.
    bool threat = false;
    // The moves are played in the position before the last move.
    bool earlier = false;
    // The best move, when that was the question; the tablebases' best, when they name one.
    std::optional<Move> best;
    // The games the answer names, the strongest that reached the position.
    std::vector<ReferenceGame> games;
    // What the question was taken to ask: the window says what the answer rests on from it.
    Action::Kind kind                      = Action::Kind::Explain;
    bool operator==(const Answered&) const = default;
};

// Why a question has no answer.
enum class Problem : uint8_t {
    UnknownMove,  // the question names no move of the position, or not the one the model picked
    Unanalyzed,   // the move on the board has no analysis yet
    InCheck,      // the side not to move is in check: no threat beyond it
    NoEngine,     // the engine gave no answer, or the game is over
    NoDatabase,   // no database to look in
    NotInTables,  // no tablebase knows the position (too many men, or none at hand)
};

// The legal moves by the names a question may give them, without check signs: in the reader's language first, then in
// the other, so that "Rf1" is the king's move for a French reader and the rook's for an English one.
using MoveNames = std::map<std::string, Move>;
[[nodiscard]] MoveNames moveNames(const Position& pos, Lang lang);

// The grammar of an action: its word, then the moves it is about, by the names in `names`; without names, only the
// actions about no move.
[[nodiscard]] std::string actionGrammar(const MoveNames& names);
// The moves `question` names, by all their names: those the model may write, any other being one it made up.
[[nodiscard]] MoveNames namedIn(std::string_view question, const MoveNames& names);

// The request turning `question` into an action, held to a grammar of the moves it names; `played` is the move just
// played, in words.
[[nodiscard]] Request actionRequest(std::string_view question, const Position& pos,
                                    std::optional<std::string_view> played, const MoveNames& names);

// What a question naming no move asks in so many words: the questions the program suggests are read without the model.
[[nodiscard]] std::optional<Action> plainAction(std::string_view question, const MoveNames& names);

// What a question naming moves asks in so many words: two moves set against each other ("Nf3 or Nc3?", "Is Nf3
// better than Nc3?"), a move's record in the games ("How often was Nf3 played?"). Read without the model too.
[[nodiscard]] std::optional<Action> namedAction(std::string_view question, const MoveNames& names);

// The action the model wrote, its moves checked against the question: a move it picked but the question does not name
// is a move it made up. None for an unknown move.
[[nodiscard]] std::optional<Action> readAction(std::string_view text, std::string_view question,
                                               const MoveNames& names);

// Whether `question` names the move `name` as a word of its own: "Cf5" in « pourquoi pas 23.Cf5 ? », not "f5" in it.
[[nodiscard]] bool mentions(std::string_view question, std::string_view name);

// Everything a question is answered from.
struct Asked {
    std::string_view question;
    const Position* before = nullptr;
    // The move that led to the position.
    std::optional<Move> previous;
    // The position before that move, and the move that led to it: "why not 8.Nc3?" asks about it, the move it names
    // being legal there only.
    const Position* earlier = nullptr;
    std::optional<Move> earlier_previous;
    // What the review found in these positions.
    std::span<const std::pair<Position, PositionAnalysis>> known;
    // The move on the board, as the review saw it, for "explain".
    const MoveContext* explain = nullptr;
    Lang lang                  = Lang::En;
    PieceLetters letters       = PieceLetters::English;
    // The databases the questions about games look in, and the moves from the usual start to the position, which
    // name its opening (none when it comes from a position of its own).
    std::span<const ReferenceBase> bases;
    std::span<const Move> line;
};

// The answer to a question: what the model writes, from what the engine finds; or why there is none.
[[nodiscard]] std::variant<Answered, Problem> answer(const Asked& asked, Model& model, const Oracle* oracle,
                                                     const Tablebases* tables);

}  // namespace fcg::commentary
