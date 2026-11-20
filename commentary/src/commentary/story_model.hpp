#pragma once

// The story of a game told by the language model (#62, mockup M11, board 5). Each passage of story() is put to it with
// the facts the template told it from: the review's verdicts and evaluations, the facts of the move it is about, the
// opening, the endgame, how the game ended and what the tables said. The model's passage is held to the grammar and the
// checker of the comments, plus the names and numbers of the facts; refused, it is written again, and refused twice,
// the template's passage stands.

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "brief.hpp"
#include "commentator.hpp"
#include "story.hpp"

namespace fcg::commentary {

// The players, as the game's tags name them ("Carlsen, Magnus" told as "Carlsen"); empty when unknown. Their names are
// the only ones the model may write besides those of the facts.
struct Players {
    std::string white, black;
};

// Attempts at each passage: the likeliest answer, then a sample.
inline constexpr uint32_t STORY_ATTEMPTS = 2;

// One passage of the story put to the model: the instructions, its facts, and what the answer may say.
class StoryBrief {
   public:
    StoryBrief(const story_detail::Told& told, const MainLine& line, const Analyses& analyses, const Tablebases* tables,
               Lang lang, PieceLetters letters, const Players& players);

    const char* system = "";
    std::string prompt;
    // GBNF the answer must follow: the comments' words, the passage's moves and squares, the names of the facts.
    std::string grammar;

    // Whether a passage holds to the facts: none when it does.
    [[nodiscard]] std::optional<Refusal> check(std::string_view text) const;

   private:
    brief_detail::Allowed allowed_;
    MoveFacts facts_;
    Color mover_ = Color::White;
    std::optional<Judgment> judgment_;
    std::optional<std::pair<Standing, Standing>> standing_;
    bool only_ = false;
    std::optional<std::string> forward_to_;
    Lang lang_ = Lang::En;
    // The prompt's words in lower case: the names and the numbers in words a passage may use.
    std::vector<std::string> told_;
};

// The request for an attempt (from 1) of a round (from 0): the likeliest answer at the first attempt of the first
// round, a sample of its own at every other one, so that writing the story again tells it otherwise.
[[nodiscard]] Request storyRequest(const StoryBrief& brief, uint32_t round, uint32_t attempt);

// A passage of the story as it is shown.
struct StoryPassage {
    Passage passage;    // the model's text, or the template's
    std::string templ;  // the template's text
    bool by_model     = false;
    uint32_t attempts = 0;  // the model's, 0 when it was not asked
    // The answers the checker refused, each with why, in their order.
    std::vector<std::pair<std::string, Refusal>> refused;
    bool operator==(const StoryPassage&) const = default;
};

// The story of the reviewed game written by the model: the passages of story() but the lessons (workOn() tells them),
// each the model's when it holds to its facts at the first attempt or the second, else the template's. `cancel` stops
// it between two requests, the passages left the templates'; `done` counts the passages dealt with. A model that gives
// no answer is not asked again: `error` says why.
[[nodiscard]] std::vector<StoryPassage> writtenStory(Model& model, const MainLine& line, std::string_view result,
                                                     const GameReport& report, const Analyses& analyses,
                                                     const Tablebases* tables, Lang lang, PieceLetters letters,
                                                     const Players& players, uint32_t round = 0,
                                                     const std::atomic<bool>* cancel = nullptr,
                                                     std::atomic<int>* done = nullptr, std::string* error = nullptr);

namespace story_detail {

// The name a player's tag gives the model: its part before a comma ("Carlsen, Magnus"), trimmed; empty for "?".
[[nodiscard]] std::string playerName(std::string_view tag);
// The checks the story adds to the comments': a capital word inside a sentence must be a name the facts give, a number
// in words one they give too (`told`, their words in lower case).
[[nodiscard]] std::optional<Refusal> checkNames(std::string_view text, const std::vector<std::string>& told,
                                                const brief_detail::Allowed& allowed);
[[nodiscard]] std::optional<Refusal> checkNumbers(std::string_view text, const std::vector<std::string>& told);

}  // namespace story_detail

}  // namespace fcg::commentary
