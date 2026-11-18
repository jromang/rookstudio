// The assistant's comments on the Board page (mockup M5, boards 3 and 4): Alt+C comments the move shown, "Comment the
// key moments" the moves the review singles out, in the background; "Annotate the game" writes the review in. Each is
// one change, taken back by one Ctrl+Z; a comment already there, the user's or the assistant's, is never replaced.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include <commentary/annotate.hpp>
#include <commentary/commentator.hpp>
#include <commentary/question.hpp>
#include <commentary/story.hpp>
#include <commentary/writing.hpp>
#include <study/game_tree.hpp>

#include "app.hpp"
#include "assistant_notes.hpp"
#include "assistant_service.hpp"
#include "engine_oracle.hpp"
#include "i18n.hpp"
#include "review_bridge.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

commentary::Lang lang() {
    return i18n::language() == i18n::Language::French ? commentary::Lang::Fr : commentary::Lang::En;
}

commentary::PieceLetters letters() {
    return lang() == commentary::Lang::Fr ? commentary::PieceLetters::French : commentary::PieceLetters::English;
}

}  // namespace

std::shared_ptr<const CommentaryReview> App::boardCommentaryReview() {
    const auto& b = board_;
    if (b.reviews.empty() || b.chess960) return nullptr;
    const auto& r  = b.reviews.front();
    const auto key = fmt::format("{}|{}|{}|{}", reviewKey(b.tree.main), r.engine, r.passes, r.positions.size());
    if (key != board_commentary_of_) {
        board_commentary_    = commentaryReview(b.tree.main, b.chess960, r);
        board_commentary_of_ = key;
    }
    return board_commentary_;
}

void App::commentBoardMove() {
    auto& b = board_;
    if (b.ply <= 0 || !assistant_) return;
    if (!assistant_->installed()) {
        // The first use is offered where the assistant is asked (M5, board 1): the Questions tab's card.
        board_side_tab_ = 4;
        boardToast(TR("The assistant needs its model first: the Questions tab offers it"));
        return;
    }
    if (comment_job_) {
        boardToast(TR("The assistant is writing already"));
        return;
    }
    const MoveNotes* notes = notesAt(b.tree, b.line, b.ply);
    if (!notes) return;
    if (!notes->after.empty()) {
        boardToast(TR("The move has a comment already · a double click corrects it"));
        return;
    }
    const auto* engine = analysisEngine();
    if (!engine)
        oracle_.reset();
    else if (!oracle_ || oracle_->engine() != engine->name)
        oracle_ = std::make_shared<EngineOracle>(*engine);
    CommentJob job;
    job.targets.push_back({b.line, b.ply, boardAt(b.game, b.chess960, b.ply).getFen()});
    job.total = 1;
    job.game  = board_game_serial_;
    // The review's view of the move when it has one (the main line); else the engine's, on the spot.
    std::shared_ptr<const CommentaryReview> review = b.line < 0 ? boardCommentaryReview() : nullptr;
    const auto before_fen                          = boardAt(b.game, b.chess960, b.ply - 1).getFen();
    const auto played = chess::uci::moveToUci(b.game.moves[static_cast<std::size_t>(b.ply - 1)].move, b.chess960);
    job.texts =
        std::async(std::launch::async, [review, ply = static_cast<std::size_t>(b.ply), fen = job.targets.front().fen,
                                        before_fen, played, model = assistant_.get(), oracle = oracle_, done = job.done,
                                        l = lang(), letters = letters()]() {
            std::vector<std::string> out(1);
            if (review) {
                if (const auto* m = review->report.get(ply)) {
                    const auto ctx =
                        commentary::moveContext(review->line, *m, review->analyses, nullptr, nullptr, oracle.get());
                    if (const auto w = commentary::writtenComment(*model, ctx, l, letters)) out[0] = w->text;
                    ++*done;
                    return out;
                }
            }
            const auto pos     = commentary::Position::fromFen(fen);
            const auto earlier = commentary::Position::fromFen(before_fen);
            if (pos && earlier) {
                commentary::Asked asked;
                asked.question = "Why this move?";
                asked.before   = &*pos;
                asked.earlier  = &*earlier;
                asked.previous = earlier->parseUci(played);
                asked.lang     = l;
                asked.letters  = letters;
                const auto a   = commentary::answer(asked, *model, oracle.get(), nullptr);
                if (const auto* said = std::get_if<commentary::Answered>(&a)) out[0] = said->text;
            }
            ++*done;
            return out;
        });
    comment_job_ = std::move(job);
    boardToast(TR("The assistant is commenting the move…"));
}

void App::commentKeyMoments() {
    auto& b = board_;
    if (comment_job_) return;
    const auto review = boardCommentaryReview();
    if (!review || !review->report.complete) {
        boardToast(TR("Analyse the game first (G): its key moments come from the review"));
        return;
    }
    const auto* engine = analysisEngine();
    if (!engine)
        oracle_.reset();
    else if (!oracle_ || oracle_->engine() != engine->name)
        oracle_ = std::make_shared<EngineOracle>(*engine);
    // The moves that deserve a comment: mistakes, only moves, turning points, plans, the novelty; those with none yet.
    const auto picked = commentary::templateComments(review->line, review->report, review->analyses, {}, nullptr,
                                                     nullptr, lang(), letters());
    CommentJob job;
    job.key_moments = true;
    job.game        = board_game_serial_;
    std::vector<std::pair<std::size_t, std::string>> wanted;
    for (const auto& [ply, text] : picked) {
        const MoveNotes* notes = notesAt(b.tree, -1, static_cast<int>(ply));
        if (!notes || !notes->after.empty()) continue;
        job.targets.push_back(
            {-1, static_cast<int>(ply), boardAt(b.tree.main, b.chess960, static_cast<int>(ply)).getFen()});
        wanted.push_back({ply, text});
    }
    if (wanted.empty()) {
        boardToast(picked.empty() ? TR("Nothing in the game calls for a comment")
                                  : TR("Every key moment has a comment already"));
        return;
    }
    job.total = static_cast<int>(wanted.size());
    // With the model, its comments (the template's where it has nothing better); without it, the template's.
    AssistantService* model = assistant_->installed() ? assistant_.get() : nullptr;
    job.texts               = std::async(std::launch::async, [review, wanted, model, oracle = oracle_, done = job.done,
                                                              cancel = job.cancel, l = lang(), letters = letters()]() {
        std::vector<std::string> out;
        for (const auto& [ply, text] : wanted) {
            if (*cancel) break;
            std::string said = text;
            if (model) {
                if (const auto* m = review->report.get(ply)) {
                    const auto ctx =
                        commentary::moveContext(review->line, *m, review->analyses, nullptr, nullptr, oracle.get());
                    if (const auto w = commentary::writtenComment(*model, ctx, l, letters)) said = w->text;
                }
            }
            out.push_back(std::move(said));
            ++*done;
        }
        return out;
    });
    comment_job_            = std::move(job);
}

void App::pollBoardComments() {
    if (!comment_job_ || !comment_job_->texts.valid() ||
        comment_job_->texts.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    auto job = std::move(*comment_job_);
    comment_job_.reset();
    const auto texts = job.texts.get();
    if (job.game != board_game_serial_) return;  // another game is on the board since
    auto& b = board_;
    // All of them in one change, on the moves that still lead where they did and have no comment yet.
    auto before = boardSnapshot();
    int added   = 0;
    for (std::size_t i = 0; i < texts.size() && i < job.targets.size(); ++i) {
        const auto& t = job.targets[i];
        if (texts[i].empty() || t.line >= static_cast<int>(b.tree.variations.size())) continue;
        const auto game = lineOf(b.tree, t.line);
        if (t.ply > static_cast<int>(game.moves.size()) || boardAt(game, b.chess960, t.ply).getFen() != t.fen) continue;
        MoveNotes* notes = notesAt(b.tree, t.line, t.ply);
        if (!notes || !notes->after.empty()) continue;
        notes->after = texts[i];
        markByAssistant(*notes, true);
        ++added;
    }
    if (added == 0) {
        boardToast(texts.empty() || std::all_of(texts.begin(), texts.end(), [](const auto& s) { return s.empty(); })
                       ? TR("The assistant found nothing to say")
                       : TR("The game has changed meanwhile: the comments have no move to go on"));
        return;
    }
    boardChanged(std::move(before));
    b.game = lineOf(b.tree, b.line);
    boardToast(job.key_moments ? trn("{} comment added · Ctrl+Z takes it back",
                                     "{} comments added · Ctrl+Z takes them all back", added, added)
                               : std::string(TR("Comment added · Ctrl+Z takes it back")));
}

void App::annotateBoardGame() {
    auto& b           = board_;
    const auto review = boardCommentaryReview();
    if (!review || !review->report.complete) {
        boardToast(TR("Analyse the game first (G): the annotation is its review written in"));
        return;
    }
    auto before      = boardSnapshot();
    const auto added = commentary::annotate(b.tree, review->line, review->report, review->analyses);
    boardChanged(std::move(before));
    b.game = lineOf(b.tree, b.line);
    boardToast(added.empty() ? TR("Game annotated: evaluations and symbols · Ctrl+Z takes it back")
                             : trn("Game annotated: evaluations, symbols and {} better line · Ctrl+Z takes it back",
                                   "Game annotated: evaluations, symbols and {} better lines · Ctrl+Z takes it back",
                                   added.size(), added.size()));
}

void App::drawCommentProgress() {
    if (!comment_job_ || !comment_job_->key_moments) return;
    const int done = comment_job_->done->load();
    ImGui::TextDisabled("%s",
                        trf("The assistant comments the key moments: {} of {}", done, comment_job_->total).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Cancel"))) *comment_job_->cancel = true;
}

void App::drawReviewAssistant() {
    auto& b           = board_;
    const auto review = boardCommentaryReview();
    if (!review) return;
    ImGui::Spacing();
    // The story of the game, from the review's facts (M5, board 4).
    const auto passages = commentary::story(review->line, b.tree.main.result.empty() ? "*" : b.tree.main.result,
                                            review->report, review->analyses, nullptr, lang(), letters());
    if (!passages.empty()) {
        ImGui::TextDisabled("%s", TR("THE STORY OF THE GAME"));
        ImGui::PushTextWrapPos();
        for (const auto& p : passages) {
            ImGui::TextUnformatted(p.text.c_str());
            if (p.ply && ImGui::IsItemClicked()) showBoardLine(-1, static_cast<int>(*p.ply));
            if (p.ply && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("A click goes to the move it is about"));
        }
        ImGui::TextDisabled("%s", TR("Written from the review's facts: its verdicts, its evaluations, the positions."));
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }
    // The assistant's two gestures on the game.
    ImGui::BeginDisabled(comment_job_.has_value() || !review->report.complete);
    if (ui::iconButton("comment_key_moments", ui::Icon::Comment, TR("Comment the key moments"))) commentKeyMoments();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", assistant_ && assistant_->installed()
                                    ? TR("A comment by the assistant on each, in the background; a comment already "
                                         "there is never replaced; Ctrl+Z takes them all back")
                                    : TR("Template comments, without the assistant's model: the Questions tab offers "
                                         "it, for fuller ones"));
    ImGui::SameLine();
    ImGui::BeginDisabled(!review->report.complete);
    if (ImGui::Button(TR("Annotate the game"))) annotateBoardGame();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", TR("The evaluations, the symbols and the better lines written into the notation; "
                                   "Ctrl+Z takes them back"));
    // What the last gesture did, said here too: the notation that shows it is in the other tab.
    if (!b.toast.empty() && ImGui::GetTime() - b.toast_time < 4.0) {
        ImGui::PushTextWrapPos();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", b.toast.c_str());
        ImGui::PopTextWrapPos();
    }
}

}  // namespace fcg
