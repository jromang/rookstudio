#pragma once

// The pattern language of the searches (mockup M13, board 2): our own, with CQL's words, read from a text and written
// back to it, so that the search panel's builder and the text say the same pattern.
//
//   ; Knight fork                       a comment (also // …)
//   piece $c in N                       $c: one white knight, the same wherever it is named
//   $c attacks k                        conditions, one per line, all true in the same position
//   $c attacks [qr]
//   btm
//
//   Bd3 Nf3 Qd1 Pe5 kg8 ph7 _f6         men on squares, side by side: each true
//   line --> move from B to h7 check    then, a ply after another: a move, or what holds after it
//        --> move from k to h7
//        --> .{0,2} --> move from N to g5 check     .{m,n}: that many plies of anything before the next step
//
// Men: K Q R B N P for White's, k q r b n p for Black's, A and a any white or black man, _ an empty square, [qr] one of
// several; squares after them (Nf3, R[a1,h1], p[a-h7], k[f-h7-8]); squares alone (h7, [g5,h5]) for whatever stands
// there. Words: attacks, defends, pin from … through … to …, en prise, passed, check, mate, stalemate, wtm, btm, not,
// or, only P (only p): the side has no other pawns than those named on their squares; move from … to … capture …
// promote …. Two names are always two men. The rest of CQL is told as not understood.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "scan_board.hpp"

namespace fcg::study {

// Men on squares: one or more kinds of men (bit side * 6 + kind; EMPTY for an empty square), on some squares.
struct Men {
    static constexpr uint16_t EMPTY = uint16_t{1} << 12;
    static constexpr uint16_t WHITE = 0x03f;         // A: any white man
    static constexpr uint16_t BLACK = 0xfc0;         // a: any black man
    uint16_t pieces                 = 0;             // none: the squares alone, whatever stands there
    uint64_t squares                = ~uint64_t{0};  // all of them: anywhere

    [[nodiscard]] bool anywhere() const noexcept { return squares == ~uint64_t{0}; }
    [[nodiscard]] static constexpr uint16_t man(int side, int kind) noexcept {
        return static_cast<uint16_t>(1u << (side * 6 + kind));
    }
    bool operator==(const Men&) const = default;
};

// What a word of a condition names: men, or the man of a name (piece $c in N), followed through the moves of a line.
struct Subject {
    Men men;
    int variable                          = -1;  // an index in Pattern::variables
    bool operator==(const Subject&) const = default;
};

enum class Relation : uint8_t {
    On,         // a: such a man on one of its squares (an empty square for _)
    Attacks,    // a attacks b: an enemy man of b, or one of b's squares when b names squares alone
    Defends,    // a defends b: a man of its own side
    Pins,       // a pins b on c: b, the only man between a man of a that slides and c, of c's side
    EnPrise,    // a: attacked, and defended by nothing or attacked by a man worth less
    Passed,     // a: a pawn no enemy pawn stands in front of, on its file or one beside
    Check,      // the side to move is in check
    Mate,       // checkmated (a game's last position)
    Stalemate,  // stalemated (a game's last position)
    WhiteToMove,
    BlackToMove,
    OnlyPawns,  // side's pawns: those the conditions name on their squares, no other
};

struct Condition {
    Relation relation = Relation::On;
    bool negated      = false;
    Subject a, b, c;        // On, EnPrise, Passed: a; Attacks, Defends: a, b; Pins: a (from), b (through), c (to)
    int side = WHITE_SIDE;  // OnlyPawns
    std::vector<Condition> alternatives;  // "or": this condition, or one of these
    int line = 0;                         // where the text has it, from 1
    std::string comment;                  // what follows it on its line
    bool operator==(const Condition& o) const {
        return relation == o.relation && negated == o.negated && a == o.a && b == o.b && c == o.c && side == o.side &&
               alternatives == o.alternatives;
    }
};

// A name for one man (piece $c in N): bound once in the first position.
struct Variable {
    std::string name;  // without its $
    Men men;
    int line = 0;
    bool operator==(const Variable& o) const { return name == o.name && men == o.men; }
};

// The move a step of a line is reached by. `from`, `to` and the man captured are read in the position before it.
struct PatternMove {
    std::optional<Subject> from, to;
    bool captures = false;                           // it takes a man
    std::optional<Subject> captured;                 // and that man is one of these
    int promote                               = -1;  // the kind a pawn becomes
    bool operator==(const PatternMove&) const = default;
};

// A step of a line: after `min_gap` to `max_gap` plies of anything, the move of a ply and what holds after it.
struct Step {
    uint32_t min_gap = 0, max_gap = 0;
    std::optional<PatternMove> move;
    std::vector<Condition> conditions;
    int line = 0;
    bool operator==(const Step& o) const {
        return min_gap == o.min_gap && max_gap == o.max_gap && move == o.move && conditions == o.conditions;
    }
};

struct Pattern {
    static constexpr std::size_t MAX_VARIABLES = 4;
    static constexpr std::size_t MAX_STEPS     = 8;
    static constexpr uint32_t MAX_GAP          = 20;
    std::vector<std::string> title;  // the comments before anything else, without their ;
    std::vector<Variable> variables;
    std::vector<Condition> conditions;  // the first position's: all true at once
    std::vector<Step> line;             // then, ply after ply

    [[nodiscard]] bool empty() const noexcept { return conditions.empty() && line.empty() && variables.empty(); }
    bool operator==(const Pattern& o) const {
        return variables == o.variables && conditions == o.conditions && line == o.line;
    }
};

// Why a text does not read: the first thing that stops it, on its line.
enum class PatternErrorKind : uint8_t {
    UnknownWord,      // `word` is no word of the language (`suggestion`: the one it looks like, if any)
    NotUnderstood,    // `word` is CQL that RookStudio does not read
    BadSquares,       // `word`: men whose squares do not read ("Ni9")
    NeedMen,          // after `word` (attacks, defends, from, through, to, capture, in), men or a name are wanted
    NeedCondition,    // `word` is where a condition is wanted
    NeedKind,         // after promote, a kind of man (Q, R, B or N)
    NeedPawns,        // after only, P or p
    NeedName,         // after piece, a name ($c) then in and men
    NotNamed,         // the name `word` is not given by a piece line
    NamedTwice,       // the name `word` is given twice
    TooManyNames,     // more than Pattern::MAX_VARIABLES names
    SecondLine,       // a second line: one sequence a pattern
    StepWithoutLine,  // a line that starts with --> and no line before it
    EmptyLine,        // line without a step after its -->
    TooManySteps,     // more than Pattern::MAX_STEPS steps
    GapAtEnd,         // .{m,n} with no step after it
    BadGap,           // .{m,n} that does not read, or a gap over Pattern::MAX_GAP
    Unexpected,       // `word` where the line should end
};

struct PatternError {
    PatternErrorKind kind = PatternErrorKind::UnknownWord;
    int line              = 0;  // from 1
    std::string word;
    std::string suggestion;
};

struct PatternRead {
    std::optional<Pattern> pattern;
    std::optional<PatternError> error;
};

// The pattern a text says, or why it does not read.
[[nodiscard]] PatternRead readPattern(std::string_view text);
// The text of a pattern, one condition a line, the men on squares first; reading it gives the same pattern.
[[nodiscard]] std::string writePattern(const Pattern& pattern);
// The first position's conditions in the order the text is written in: the men on squares first (White's then
// Black's, kings first, each kind by its squares, then the empty squares), the others as they come, "only" last.
// A pattern read is in that order; the builder puts it back in it after a change.
void canonicalize(Pattern& pattern);
// The text of men ("Nf3", "[qr]", "R[a1,h1]", "p[a-h7]", "_f6", "[g5,h5]") and of one condition (its line).
[[nodiscard]] std::string menText(const Men& men);
[[nodiscard]] std::string conditionText(const Pattern& pattern, const Condition& condition);
[[nodiscard]] std::string stepText(const Pattern& pattern, const Step& step);
// Squares as the language writes them: "f3", "a-h8", "[f3,g3]", or a list without its brackets ("f3, g3"); none when
// they do not read. And back: "f3", "[a-h8]", "[f3,g3]".
[[nodiscard]] std::optional<uint64_t> readSquares(std::string_view text);
[[nodiscard]] std::string squaresText(uint64_t squares);
// Whether a word is CQL's but not read by RookStudio.
[[nodiscard]] bool cqlWord(std::string_view word);

// The presets of the search panel and of Ctrl+K: a key the program names them by, and their text.
struct PatternPreset {
    std::string_view key;
    std::string_view text;
};
[[nodiscard]] const std::vector<PatternPreset>& patternPresets();

// A condition among the first position's that is a man on one square ("Nf3", "_f6"): the men put on the search
// panel's small board.
[[nodiscard]] bool manOnSquare(const Condition& condition);

// Two conditions of the first position that cannot both hold ("Nc7" and "_c7", "wtm" and "btm"), or one that never
// can ("Pa1"): told before searching. `second` is -1 for one alone. Indexes in Pattern::conditions.
struct Contradiction {
    int first  = 0;
    int second = -1;
};
[[nodiscard]] std::optional<Contradiction> contradiction(const Pattern& pattern);

// The men a variable or its men stand on, each followed by its square: -1 for none (taken).
struct Binding {
    std::array<int8_t, Pattern::MAX_VARIABLES> square{-1, -1, -1, -1};
};

// A pattern made ready to match positions fast: the conditions without a name first, the cheapest first.
class PatternMatcher {
   public:
    explicit PatternMatcher(Pattern pattern);
    [[nodiscard]] const Pattern& pattern() const noexcept { return pattern_; }
    // Whether some binding of the names makes the first position's conditions hold on `board`; `last`: the game's
    // last position, the only one that can be mate or stalemate. Each binding that does is given to `found`, which
    // returns true to stop.
    template <typename Found>
    bool first(const ScanBoard& board, bool last, Found&& found) const;
    // The conditions of a step after `binding` (followed to the step's position).
    [[nodiscard]] bool holds(const std::vector<Condition>& conditions, const ScanBoard& board, const Binding& binding,
                             bool last) const;
    [[nodiscard]] bool holds(const Condition& condition, const ScanBoard& board, const Binding& binding,
                             bool last) const;
    // Whether the move `move`, played on `before` and giving `after`, is the move of a step.
    [[nodiscard]] bool moveFits(const PatternMove& wanted, const ScanBoard& before, const CodedMove& move,
                                const Binding& binding) const;
    // The squares of a subject on a board.
    [[nodiscard]] uint64_t squares(const Subject& subject, const ScanBoard& board, const Binding& binding) const;
    // The men every first position needs on their squares, to leave out games their signatures say cannot have them:
    // [side][kind] squares, the empty squares, a side's pawns exactly those, the side to move (-1 either).
    struct Needs {
        std::array<std::array<uint64_t, 6>, 2> men{};
        uint64_t empty = 0;
        std::array<bool, 2> only_these_pawns{};
        int side_to_move = -1;
    };
    [[nodiscard]] const Needs& needs() const noexcept { return needs_; }
    [[nodiscard]] bool asksMate() const noexcept { return asks_mate_; }

   private:
    // Every binding of the names to men of theirs, two names never on one man, that makes the conditions with names
    // hold on `board`.
    void bindingsOf(const ScanBoard& board, bool last, std::vector<Binding>& out) const;

    Pattern pattern_;
    std::vector<Condition> unnamed_, named_;  // the first position's conditions, those without a name first
    std::array<uint64_t, 2> only_pawns_{};    // the pawns named on their squares, for "only"
    Needs needs_;
    bool asks_mate_ = false;  // mate or stalemate among the first position's conditions, not negated
};

template <typename Found>
bool PatternMatcher::first(const ScanBoard& board, bool last, Found&& found) const {
    if (asks_mate_ && !last) return false;
    if (!holds(unnamed_, board, Binding{}, last)) return false;
    if (pattern_.variables.empty()) {
        found(Binding{});
        return true;
    }
    thread_local std::vector<Binding> bindings;
    bindings.clear();
    bindingsOf(board, last, bindings);
    for (const auto& b : bindings)
        if (found(b)) break;
    return !bindings.empty();
}

}  // namespace fcg::study
