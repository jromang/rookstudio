#pragma once

// What a language model is told about one move, and what it may write back (the prototype's
// crates/commentary/src/brief.rs).
//
// The model gets the move's facts as plain sentences and may only write what they support: a grammar keeps every move
// and square it writes within those of the facts, and a checker reads the result for motifs the facts do not contain.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "eval.hpp"
#include "fact_types.hpp"
#include "gather.hpp"
#include "notation.hpp"
#include "phrases.hpp"

namespace fcg::commentary {

// Room for three short sentences.
inline constexpr std::size_t MAX_TOKENS = 130;

// The moves a model is asked about: the notable ones, and novelties.
[[nodiscard]] bool worthAComment(const MoveReport& report);

// Why a model's comment was refused.
struct Refusal {
    enum class Kind : uint8_t {
        Notation,      // a word that looks like a move but is not one of the facts'
        Motif,         // a tactical word without the fact that would justify it
        Colour,        // a piece given the wrong colour
        Piece,         // a piece put on a square where neither the positions shown nor the facts have it
        Unsupported,   // a strategic notion the facts say nothing about
        TooManyMoves,  // lines quoted at length: a comment is not a variation
        Aside,         // talk about the facts, the engine or the writer instead of the game
        Verdict,       // a verdict other than the review's: a mistake called a blunder
        Evaluation,    // words for how the position stands that the evaluation contradicts
        Elision,       // French that forgets an elision: « du habituel », « le échec »
        Grammar,       // something the grammar does not allow: digits, other punctuation, a fourth sentence
        Empty,
        // The story's (story_model.hpp):
        Name,    // a name the facts do not give: a player, an opening, a place
        Number,  // a number in words that the facts do not give: "on move thirty-four"
    };
    Kind kind = Kind::Empty;
    // The words refused: the move, the motif, the piece, the name; empty for TooManyMoves, Grammar and Empty.
    std::string what;
    bool operator==(const Refusal&) const = default;
};
// As the prototype's Debug prints it: Motif("fourchette"), Grammar.
[[nodiscard]] std::string debug(const Refusal& refusal);

namespace brief_detail {

// The moves and squares the facts mention: the only ones the model may write.
struct Allowed {
    std::vector<std::string> moves;
    std::vector<std::string> squares;
    // The move commented, in all its forms: naming it quotes no line.
    std::vector<std::string> played;
    // Words of the opening's name a plain word cannot be: "Caro-Kann".
    std::vector<std::string> names;
    // The opening's name in words, which say nothing about the position: "Pin Variation".
    std::vector<std::string> name_words;
    // The words of the theory note, which support the notions and motifs they name.
    std::vector<std::string> noted;
    // The pieces of the positions the prompt shows, and of the facts: a comment may put no other piece on a square.
    std::vector<Placed> pieces;

    // GBNF: words without digits, the allowed moves and squares, one to three sentences, punctuated the language's way.
    [[nodiscard]] std::string grammar(Lang lang) const;
};

// A fact as a plain statement for the prompt.
[[nodiscard]] std::optional<std::string> factSentence(const Fact& fact, Lang lang);
// A piece with its article: "the white rook on h1", « la tour blanche en h1 ».
[[nodiscard]] std::string thePiece(const Placed& p, Lang lang);
// « du fou blanc en c4 », « de la dame noire en d8 », "of the white bishop on c4".
[[nodiscard]] std::string ofThePiece(const Placed& p, Lang lang);
// How a side stands, in words: « équilibrée », "slightly better". The model is given words, no numbers to repeat.
[[nodiscard]] const char* standingWords(Standing s, Lang lang);
// What the move did to the mover's position, said so that no change can be misread. `movers` is « des Blancs »,
// "White's".
[[nodiscard]] std::string change(Standing before, Standing after, std::string_view movers, Lang lang);
// The ways a sentence may write a move: as numbered, without its number, and without the check or mate sign it says in
// words: "41…Txe2#", "41…Txe2", "Txe2#", "Txe2".
[[nodiscard]] std::vector<std::string> forms(std::string_view numbered);
// A survey's remark as the prompt quotes it: English prose in whole sentences, short, without the moves, games,
// results and symbols a model could not write anyway. Squares may stay.
[[nodiscard]] std::optional<std::string> noteShown(std::string_view text);
// Longest note the prompt quotes, in bytes: a remark, not an essay.
inline constexpr std::size_t NOTE_LENGTH = 200;
// A text's words, in lower case; a compound is also its parts: "h-file" says "file".
[[nodiscard]] std::vector<std::string> wordsOf(std::string_view text);

// The checks, each none when the text holds to it.
[[nodiscard]] bool fitsGrammar(std::string_view text, const Allowed& allowed, Lang lang);
[[nodiscard]] std::optional<Refusal> check(std::string_view text, const MoveFacts& facts, const Allowed& allowed,
                                           Color mover);
[[nodiscard]] std::optional<Refusal> checkVerdict(std::string_view text, std::optional<Judgment> judgment);
[[nodiscard]] std::optional<Refusal> checkOnly(std::string_view text, bool only);
[[nodiscard]] std::optional<Refusal> checkRetreat(std::string_view text, const std::optional<std::string>& forward_to);
[[nodiscard]] std::optional<Refusal> checkCentre(std::string_view text, std::span<const Fact* const> present);
[[nodiscard]] std::optional<Refusal> checkFiller(std::string_view text);
[[nodiscard]] std::optional<Refusal> checkStanding(std::string_view text,
                                                   std::optional<std::pair<Standing, Standing>> standing);
[[nodiscard]] std::optional<Refusal> checkElision(std::string_view text);

// A piece a comment puts on a square, with the words that do: "the bishop on g6", « le pion a6 ».
struct PlacedIn {
    Role role = Role::Pawn;
    std::optional<Color> colour;
    Square square = Square::A1;
    std::string said;
};
[[nodiscard]] std::vector<PlacedIn> placedIn(std::string_view text);

// The pieces a fact is about, and the squares it names.
[[nodiscard]] std::vector<Placed> factPieces(const Fact& fact);
[[nodiscard]] std::vector<Square> factSquares(const Fact& fact);

}  // namespace brief_detail

// One move put to a model: the instructions, the facts, and the limits of the answer.
class Brief {
   public:
    Brief(const MoveContext& ctx, Lang lang, PieceLetters letters);

    // Standing instructions: tone, length, what not to invent.
    const char* system = "";
    // The move's facts, in sentences.
    std::string prompt;
    // GBNF the answer must follow: no digits, only the facts' moves and squares.
    std::string grammar;

    // Whether the model has anything to put into words: a verdict, a fact, or what theory says of the move. A novelty
    // with nothing else to it, its opening's name aside, is the template's « Nouveauté. »: a model could only embellish
    // it, and does.
    [[nodiscard]] bool hasSubstance() const;
    // Whether a comment holds to the facts, and is one the grammar lets through (none when it does): a model writing
    // without the grammar is held to it this way.
    [[nodiscard]] std::optional<Refusal> check(std::string_view text) const;

   private:
    friend class StoryBrief;  // a passage of the story about a move is held to the move's facts (story_model.hpp)

    MoveFacts facts_;
    brief_detail::Allowed allowed_;
    Color mover_ = Color::White;
    std::optional<Judgment> judgment_;
    std::optional<std::pair<Standing, Standing>> standing_;
    // Whether the facts make a move the only one: the review's verdict, or the tablebases'.
    bool only_ = false;
    // Where the move played goes, unless it goes back: a retreat it is not.
    std::optional<std::string> forward_to_;
    // Whether the prompt tells what theory says of the move.
    bool theory_ = false;
    Lang lang_   = Lang::En;
};

}  // namespace fcg::commentary
