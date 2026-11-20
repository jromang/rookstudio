#include "commentator.hpp"

#include "comment.hpp"

namespace fcg::commentary {

Request commentRequest(const Brief& brief, uint32_t attempt) {
    Request r;
    r.messages    = {{Speaker::System, brief.system}, {Speaker::User, brief.prompt}};
    r.grammar     = brief.grammar;
    r.max_tokens  = MAX_TOKENS;
    r.temperature = attempt == 1 ? 0.0f : 0.7f;
    r.seed        = attempt;
    return r;
}

std::optional<Written> writtenComment(Model& model, const MoveContext& ctx, Lang lang, PieceLetters letters) {
    const Brief brief(ctx, lang, letters);
    const uint32_t attempts = brief.hasSubstance() ? ATTEMPTS : 0;
    for (uint32_t attempt = 1; attempt <= attempts; ++attempt) {
        const auto answer = model.generate(commentRequest(brief, attempt), nullptr);
        if (!answer) break;
        if (!brief.check(answer->text)) return Written{answer->text, true, attempt};
    }
    const auto text = comment(ctx, lang, letters);
    if (!text) return std::nullopt;
    auto held = brief.heldTemplate(*text);  // its sentences read against the boards, as the model's are (#64)
    if (!held) return std::nullopt;
    return Written{std::move(*held), false, attempts};
}

}  // namespace fcg::commentary
