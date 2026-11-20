#pragma once

// Comments written by the language model: its text when it holds to the facts, the template's otherwise (the
// prototype's crates/commentary/src/llm.rs, and the request types of crates/llm).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "brief.hpp"

namespace fcg::commentary {

// Who says a message.
enum class Speaker : uint8_t { System, User, Assistant };

struct Request {
    std::vector<std::pair<Speaker, std::string>> messages;
    // GBNF the answer must follow, rule `root` first.
    std::optional<std::string> grammar;
    std::size_t max_tokens = 0;
    // Zero picks the likeliest token every time.
    float temperature                     = 0.0f;
    uint32_t seed                         = 0;
    bool operator==(const Request&) const = default;
};

struct Generation {
    std::string text;
    std::size_t prompt_tokens = 0;
    std::size_t tokens        = 0;
    double seconds            = 0.0;
};

// A language model, loaded: the assistant's runtime gives one, the tests a fake. One request at a time.
class Model {
   public:
    virtual ~Model() = default;
    // The model's answer; none when it gives none, `error` then saying why.
    [[nodiscard]] virtual std::optional<Generation> generate(const Request& request, std::string* error) = 0;
};

// What came out, whether the model wrote it, and after how many attempts.
struct Written {
    std::string text;
    bool by_model                         = false;
    uint32_t attempts                     = 0;
    bool operator==(const Written&) const = default;
};

// Attempts before falling back to the template.
inline constexpr uint32_t ATTEMPTS = 3;

// The request for one attempt, from 1: the likeliest answer first, then samples.
[[nodiscard]] Request commentRequest(const Brief& brief, uint32_t attempt);

// A comment on the move: the model's when it passes the checks, else the template's, which is also what a move with
// nothing for the model to explain gets (no attempt), its sentences the checks of the boards refuse left out
// (Brief::heldTemplate()). None when neither has anything to say.
[[nodiscard]] std::optional<Written> writtenComment(Model& model, const MoveContext& ctx, Lang lang,
                                                    PieceLetters letters);

}  // namespace fcg::commentary
