#pragma once

// What a language model is told about one move, and what it may write back (the prototype's
// crates/commentary/src/brief.rs).
//
// The model gets the move's facts as plain sentences and may only write what they support: a grammar keeps every move
// and square it writes within those of the facts, and a checker reads the result for motifs the facts do not contain.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
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
        // What a sentence says of the boards (#64):
        Board,     // a pawn structure or a tactic said of a position that does not have it: "doubled pawns" after the
                   // recapture that undid them, a discovered attack told right after a move that only prepares it
        Material,  // material said to be won, given up or taken that the line spoken of does not count: "wins the
                   // bishop and two pawns" for two pawns, "takes the queen on f3" where a knight stands
        Reason,    // the better move praised for what the move played does too: "misses 30.Rg7, which won a pawn"
                   // where 30.Bxh7 wins one
        Wording,   // words that say something else than what is meant: « la partie se termine » for the opening,
                   // « donne le roi », « parier » for « parer »
        // The opening report's summary (opening_summary.hpp):
        Figure,  // a figure the report does not give, or gives of something else: "52%" for a plan of 47%
        Advice,  // what to play, where the report gives frequencies: "White should push b4"
    };
    Kind kind = Kind::Empty;
    // The words refused: the move, the motif, the piece, the name; empty for TooManyMoves, Grammar and Empty.
    std::string what;
    bool operator==(const Refusal&) const = default;
};
// As the prototype's Debug prints it: Motif("fourchette"), Grammar.
[[nodiscard]] std::string debug(const Refusal& refusal);

namespace brief_detail {

// A line the prompt shows, from the position it starts in: the boards a sentence may speak of (#64).
struct Shown {
    enum class Kind : uint8_t {
        Played,  // the move played, then the engine's answer to it
        Best,    // the engine's best line, the better move first
        Other,   // for an only move, the next best move and its refutation
        Theory,  // the surveys' main move
    };
    Kind kind = Kind::Played;
    Position start;
    // The engine's whole line, the prompt showing its first `shown` plies: material is counted further, as the facts
    // count it.
    std::vector<Move> moves;
    std::size_t shown = 0;
    // The ways a sentence may write each move shown (forms()).
    std::vector<std::vector<std::string>> texts;
    // The position before each move shown, then the one after the last: boards[i + 1] follows moves[i].
    std::vector<Position> boards;

    [[nodiscard]] static Shown of(Kind kind, const Position& start, std::span<const Move> moves, std::size_t shown,
                                  PieceLetters letters);
};

// Whether a side's position is lost beyond doubt, before the move and after it: its expected score, as the engine sees
// it, is below this, the review's "beyond saving" (review.cpp), which a mate and the tables' loss are too. The prompt
// calls a position lost from 10 % (standingOf()): Gaia gave Black 9.7 % at −1.48 on November 18's reread, which "lost
// anyway" must not repeat (#64).
inline constexpr double LOST_BEYOND_DOUBT = 3.0;
struct Lost {
    std::array<bool, 2> before{}, after{};  // White, Black
    // From the mover's expected scores before and after its move.
    [[nodiscard]] static Lost of(const MoveReport& move);
    // From White's expected score in a position, both before and after.
    [[nodiscard]] static Lost at(std::optional<double> white_expected);
};

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
    // The lines the prompt shows, the move played first: each sentence is read against the boards it speaks of.
    // Without them, as in the prototype, a piece may stand on any board of the prompt.
    std::vector<Shown> lines;

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

// What each sentence says of the boards it speaks of (#64), read against the lines the prompt shows (Allowed::lines;
// nothing to check without them). A piece it names on a square stands there: around the move played when it quotes no
// move of a line; after "after 27…Qf7", « après 27…Df7 », around that move and those it quotes next; told with a move
// of a line quoted otherwise ("7…e5 was better: ..."), somewhere along that line; told as what follows ("then",
// "allows", "becomes", « ensuite »), where the line brings it later. A pawn called doubled, backward, isolated or
// passed is so after the last move quoted before the word, as its line goes on; a tactic is not told after a move of
// the line that comes before the one doing it; material said to be won, given up or taken is counted along the lines,
// kind by kind; the better move is not praised for material the move played takes itself.
[[nodiscard]] std::optional<Refusal> checkBoards(std::string_view text, const MoveFacts& facts, const Allowed& allowed);
// A position called lost ("lost anyway", « perdue de toute façon », "White is lost") only when it is so beyond doubt.
[[nodiscard]] std::optional<Refusal> checkLost(std::string_view text, const Lost& lost);
// Words that say something else than what is meant: a king taken, won or given; « parier » for « parer ».
[[nodiscard]] std::optional<Refusal> checkWording(std::string_view text, Lang lang);

// The sentences of a text as the checks read them, each as written: a word that ends with a full stop, an exclamation
// or a question mark ends one (a move keeps its dots).
[[nodiscard]] std::vector<std::string_view> sentenceTexts(std::string_view text);
// A text with the sentences `refused` refuses left out, those kept as written, one space between two; none when no
// sentence is left.
[[nodiscard]] std::optional<std::string> keptSentences(std::string_view text,
                                                       const std::function<bool(std::string_view)>& refused);

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

    // A sentence of the template's comment (comment.hpp) read with the checks of the boards (#64) a model's comment
    // gets: none when it holds. The templates are the prototype's, held by the golden files, and some of the facts
    // they are written from are false, so some of their sentences are: "After 29…Qf7, a discovered attack hits the pawn
    // on f2" where 31…Nd5 makes the attack, "Leaves doubled pawns on the c-file" where the recapture undoes them.
    [[nodiscard]] std::optional<Refusal> checkTemplate(std::string_view sentence) const;
    // The template's comment read so, sentence by sentence: those refused left out, none when no sentence is left.
    // What is shown and written of a template, wherever a comment of the move's context is.
    [[nodiscard]] std::optional<std::string> heldTemplate(std::string_view text) const;

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
    // Whose position is lost beyond doubt, before the move and after it.
    brief_detail::Lost lost_;
};

}  // namespace fcg::commentary
