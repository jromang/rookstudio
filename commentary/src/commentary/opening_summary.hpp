#pragma once

// The summary of an opening report (#91, mockup M13, board 4, point 11): a few sentences written from the report's
// figures and the assistant's facts alone. The templates write it, in English and in French, as passages: the pawn
// structure, each side's plans, the moves played, the main line, the evaluations. The language model writes each
// passage again from the same figures, put to it as the story's passages are (story_model.hpp): a grammar lets it name
// only the passage's moves (the position's, the report's lines), squares, names and figures, and the checker reads each
// sentence against those figures and facts — a figure must be one the report gives of what the sentence names, a move
// one of the position or of its lines, a plan or a structure one the report has, a side the one it is of, no advice. A
// passage refused is written again once; refused twice, the template's stands, with why.

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "brief.hpp"
#include "commentator.hpp"
#include "fact_parts.hpp"
#include "phrases.hpp"
#include "position.hpp"

namespace fcg::commentary {

// A plan the assistant's facts have a name for, carried out by a pawn push.
enum class PlanName : uint8_t {
    None,
    MinorityAttack,   // the b-pawn against three pawns on the queen's wing (fact::MinorityAttack)
    PawnStorm,        // the kings on opposite wings, pawns thrown at the enemy king (fact::PawnStorm)
    ChainBase,        // a break at the base of a locked chain (Lever::Kind::ChainBase)
    ChainHead,        // a break at its head
    Isolani,          // the isolated queen's pawn advancing
    FreeingMaroczy,   // the freeing break against a Maroczy bind
    FreeingHedgehog,  // the hedgehog's break
    Space,            // a pawn advance that gives its side more space in the centre (fact::Space)
};

// The plan a pawn push carries out, by the facts of the move on the board before it (the plans of the comments, of
// the side that plays it); None when it carries out none.
[[nodiscard]] PlanName planOf(const Position& before, const Move& push);
// The plan in words: "the minority attack", « l'attaque de minorité ».
[[nodiscard]] std::string planWords(PlanName name, Lang lang);

// The figures a summary is written from, as the report gives them. Moves are written as the reader writes them (their
// pieces in the letters of the language), numbered from the position.
struct SummaryFigures {
    std::string opening;  // "D36 Queen's Gambit Declined: Exchange Variation"; empty when the position has no name
    std::string after;    // the move that leads to the position, numbered: "10…Nf8"; empty at the start
    Color mover    = Color::White;
    uint64_t games = 0;  // the position's

    struct PlayedMove {
        std::string san;  // "11.Rab1"
        uint64_t games = 0;
        int share      = 0;         // of the position's games, in percent
        std::optional<int> score;   // the mover's, in percent; none without results
        std::optional<int32_t> cp;  // the evaluation after it, White's centipawns
        std::optional<int> mate;    // or a mate, White's moves to it (negative: Black mates)
        bool engine = false;        // the engine's evaluation, not Lichess's
    };
    std::vector<PlayedMove> moves;  // the most played first

    struct Line {
        std::vector<std::string> moves;  // numbered from the position: "11.Rab1", "a5", "12.a3"
        uint64_t games = 0;
        std::optional<int> white_score;
    };
    std::vector<Line> lines;  // the main lines

    struct Structure {
        Formation formation = Formation::Carlsbad;
        Color owner         = Color::White;
        bool kept           = false;  // the position has it already
        int share           = 0;      // of the games that reach move 20
    };
    std::vector<Structure> structures;  // the most met first
    int structure_move = 20;            // the move they are read at; 0: ten moves after the position

    struct Plan {
        Color side    = Color::White;
        PlanName name = PlanName::None;
        // Its moves in words: two pushes ("b4", "b5"), one ("e4"), or a man's route: its letter and squares ("N",
        // "f8", "g6").
        std::vector<std::string> squares;
        bool route = false;
        std::string man;           // a route's man in words: "the knight", « le cavalier »
        int share = 0;             // of the games, in percent
        std::optional<int> score;  // its side's, in percent
    };
    std::vector<Plan> plans;  // White's then Black's, the most played first
};

// A passage of the summary as it is shown.
struct SummaryPassage {
    enum class Topic : uint8_t { Structure, WhitePlans, BlackPlans, Moves, Line, Evaluations };
    Topic topic = Topic::Moves;
    std::string text;   // the model's, or the template's
    std::string templ;  // the template's
    bool by_model     = false;
    uint32_t attempts = 0;  // the model's, 0 when it was not asked
    // The answers the checker refused, each with why, in their order.
    std::vector<std::pair<std::string, Refusal>> refused;
    bool operator==(const SummaryPassage&) const = default;
};

// One passage put to the model: the instructions, its figures, and what the answer may say.
class SummaryBrief {
   public:
    SummaryBrief(const SummaryFigures& figures, SummaryPassage::Topic topic, Lang lang);

    const char* system = "";
    std::string prompt;
    std::string grammar;

    // Whether a passage holds to the report's figures and facts, sentence by sentence: none when it does.
    [[nodiscard]] std::optional<Refusal> check(std::string_view text) const;

   private:
    // A figure of the passage, as written, and the words of what it is of (moves, squares, a plan's or a structure's
    // words); the side whose score it is, when it is a score.
    struct Figure {
        std::string text;
        std::vector<std::string> of;
        std::optional<Color> side;
    };
    std::vector<Figure> figures_;
    std::vector<std::string> subjects_;  // every word a figure is of
    brief_detail::Allowed allowed_;
    std::vector<std::string> told_;  // the prompt's words in lower case
    std::vector<PlanName> plans_;    // the report's plans, of either side
    std::vector<std::pair<PlanName, Color>> sided_;
    std::vector<Formation> formations_;                      // its structures
    std::optional<std::pair<Standing, Standing>> standing_;  // the evaluations': White's lowest and highest
    Lang lang_ = Lang::En;
};

// The passages a report's figures give, in their order: those with nothing to say left out.
[[nodiscard]] std::vector<SummaryPassage::Topic> summaryTopics(const SummaryFigures& figures);
// A passage as the templates write it.
[[nodiscard]] std::string templatePassage(const SummaryFigures& figures, SummaryPassage::Topic topic, Lang lang);
// The summary as the templates write it, passage by passage.
[[nodiscard]] std::vector<SummaryPassage> templateSummary(const SummaryFigures& figures, Lang lang);

// The request for an attempt (from 1) of a round (from 0): the likeliest answer at the first attempt of the first
// round, a sample of its own otherwise, so that writing the summary again writes it otherwise.
[[nodiscard]] Request summaryRequest(const SummaryBrief& brief, uint32_t round, uint32_t attempt);

// The summary written by the model: each passage the model's when it holds at the first attempt or the second, else the
// template's. `cancel` stops it between two requests, the passages left the templates'; `done` counts the passages
// dealt with. A model that gives no answer is not asked again: `error` says why.
[[nodiscard]] std::vector<SummaryPassage> writtenSummary(Model& model, const SummaryFigures& figures, Lang lang,
                                                         uint32_t round = 0, const std::atomic<bool>* cancel = nullptr,
                                                         std::atomic<int>* done = nullptr,
                                                         std::string* error     = nullptr);

// The passages' texts in one paragraph.
[[nodiscard]] std::string summaryText(const std::vector<SummaryPassage>& passages);

namespace summary_detail {

// A percentage, a count of games and an evaluation as the summary writes them: "47%", "1,902", "+0.31" in English;
// « 47 % », « 1 902 », « +0,31 » in French (narrow no-break spaces).
[[nodiscard]] std::string percent(int value, Lang lang);
[[nodiscard]] std::string games(uint64_t value, Lang lang);
[[nodiscard]] std::string evaluation(std::optional<int32_t> cp, std::optional<int> mate, Lang lang);
// The figures written in a text, as written: "+0.31", "1,902", "47%", « 47 % ».
[[nodiscard]] std::vector<std::pair<std::size_t, std::string>> figuresIn(std::string_view text);

}  // namespace summary_detail

}  // namespace fcg::commentary
