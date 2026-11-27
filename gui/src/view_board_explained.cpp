// Every move explained on the study Board (#94, mockup M13, board 7, point 15): "Explain every move" in the review's
// summary (Alt+G first when the game has no review), in the background with its progress, Cancel and partial results
// (R18); the lines in the Explained tab beside the moves, kept with the review, filtered to the moves that matter or
// the key moments; "Write them into the game" makes them the assistant's comments, one change that one Ctrl+Z takes
// back, never in place of a comment already there (#60).

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include <commentary/writing.hpp>
#include <study/scan_board.hpp>

#include "app.hpp"
#include "assistant_notes.hpp"
#include "assistant_service.hpp"
#include "engine_oracle.hpp"
#include "explained.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "review_bridge.hpp"
#include "ui.hpp"
#include "widgets.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

commentary::Lang assistantLang() {
    return i18n::language() == i18n::Language::French ? commentary::Lang::Fr : commentary::Lang::En;
}

commentary::PieceLetters assistantLetters() {
    return assistantLang() == commentary::Lang::Fr ? commentary::PieceLetters::French
                                                   : commentary::PieceLetters::English;
}

}  // namespace

void App::explainEveryMove() {
    if (gameLocked()) {  // fair play (#48)
        boardToast(TR("Locked while one of your games is played on Lichess."));
        return;
    }
    if (engineGameHidesAids()) {  // a game against an engine (#83)
        boardToast(TR("Hidden until the end of your game: Show them first, and the game is marked “with aids”"));
        return;
    }
    auto& b    = board_;
    b.open_tab = 4;  // the Explained tab, beside the moves
    if (explain_job_) return;
    const auto review = boardCommentaryReview();
    if (!review || !review->report.complete || b.reviewing()) {
        // The review first (Alt+G): the explanations come from it, once it is done.
        explain_after_review_ = true;
        if (!b.reviewing()) startGameReview();
        boardToast(TR("The game is analysed first: every move is explained once its review is done"));
        return;
    }
    startExplaining(false);
}

void App::startExplaining(bool rest) {
    auto& b           = board_;
    const auto review = boardCommentaryReview();
    if (!review || !review->report.complete || b.reviews.empty() || explain_job_) return;
    explain_after_review_ = false;
    const auto& r         = b.reviews.front();
    const auto lang       = assistantLang();
    Explained e;
    e.key    = reviewKey(b.tree.main);
    e.review = explainedReview(r.engine, r.passes, r.positions.size());
    e.lang   = lang == commentary::Lang::Fr ? "fr" : "en";
    if (rest && explained_ && explained_->key == e.key && explained_->review == e.review &&
        explained_->lang == e.lang) {
        e = *explained_;
    } else {
        // The games of the user's databases at a position: those their opening trees count.
        auto& sh = shelf();
        std::vector<DatabaseShelf::TreeSource> trees;
        for (std::size_t i = 0; i < sh.entries().size(); ++i)
            if (!sh.entries()[i].source)
                if (auto source = sh.treeSource(i); source && source->tree) trees.push_back(std::move(*source));
        std::string unread;  // a tree that could not be read (#139), left out of `trees`: said once
        const auto games = [&](const commentary::Position& pos) {
            study::ScanBoard scan;
            if (!scan.setFen(pos.fen(), false)) return uint64_t{0};
            auto read = treesAt(trees, scan.key());
            if (unread.empty()) unread = std::move(read.error);
            return read.answer ? read.answer->total.games : uint64_t{0};
        };
        e.lines = explainPlan(review->line, review->report, lang, games);
        if (!unread.empty()) boardToast(unread);  // the theory's games counted without it; also in the log
    }
    const auto* engine = analysisEngine();
    if (!engine)
        oracle_.reset();
    else if (!oracle_ || oracle_->engine() != engine->name)
        oracle_ = std::make_shared<EngineOracle>(*engine);
    ExplainJob job;
    job.game = board_game_serial_;
    job.made = std::make_shared<Explained>(e);
    for (const auto& l : e.lines) job.total += l.written ? 0 : 1;
    // With the model when it is the catalogue's (it learnt RookStudio's comments); the templates' without it.
    std::shared_ptr<AssistantService> model;
    if (assistant_ && assistant_->installed() && assistant_->knowsComments() && storyWithoutModel().empty())
        model = assistant_;
    job.finished  = std::async(std::launch::async, [review, made = job.made, mutex = job.mutex, done = job.done,
                                                    cancel = job.cancel, model, oracle = oracle_,
                                                    tables  = reviewTables() ? review_tables_ : nullptr, lang,
                                                    letters = assistantLetters()] {
        for (std::size_t i = 0;; ++i) {
            ExplainedLine line;
            {
                std::lock_guard lock(*mutex);
                if (i >= made->lines.size()) break;
                line = made->lines[i];
            }
            if (line.written) continue;
            if (*cancel) break;
            WrittenLine w;
            double seconds = 0;
            if (const auto* m = review->report.get(line.to)) {
                const auto ctx =
                    commentary::moveContext(review->line, *m, review->analyses, nullptr, tables.get(), oracle.get());
                const auto start = std::chrono::steady_clock::now();
                w                = writeExplainedLine(model.get(), ctx, lang, letters);
                seconds          = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            }
            {
                std::lock_guard lock(*mutex);
                auto& dst   = made->lines[i];
                dst.text    = std::move(w.text);
                dst.source  = w.source;
                dst.refused = w.refused;
                dst.written = true;
                if (w.asked) {
                    made->model_secs += seconds;
                    ++made->model_lines;
                }
            }
            ++*done;
        }
        std::lock_guard lock(*mutex);
        made->complete = std::all_of(made->lines.begin(), made->lines.end(), [](const auto& l) { return l.written; });
    });
    explained_    = std::move(e);
    explained_of_ = explained_->key + "|" + explained_->lang;
    explain_job_  = std::move(job);
}

void App::pollExplained() {
    auto& b = board_;
    // Asked before the review: made once it is done, its last pass read again (review_key empty until then).
    if (explain_after_review_ && !explain_job_ && !b.reviewing() && !b.review_key.empty()) {
        if (const auto review = boardCommentaryReview(); review && review->report.complete) startExplaining(false);
    }
    if (!explain_job_ || !isReady(explain_job_->finished)) return;
    explain_job_->finished.get();
    Explained made;
    {
        std::lock_guard lock(*explain_job_->mutex);
        made = *explain_job_->made;
    }
    const bool ours = explain_job_->game == board_game_serial_;
    explain_job_.reset();
    if (assistant_ && made.model_lines > 0) made.device = assistant_->onGpu() ? "gpu" : "cpu";
    (void)saveExplained(storage_.root() / "analyses", made);  // kept with the review, whatever the board shows now
    if (ours) {
        explained_    = std::move(made);
        explained_of_ = explained_->key + "|" + explained_->lang;
    }
}

void App::writeExplainedIntoGame() {
    auto& b = board_;
    if (!explained_ || gameLocked()) return;
    std::vector<AssistantTarget> targets;
    std::vector<std::string> texts;
    for (const auto& l : explained_->lines) {
        if (!l.written || l.text.empty() || l.to > b.tree.main.moves.size()) continue;
        targets.push_back(
            {-1, static_cast<int>(l.to), boardAt(b.tree.main, b.chess960, static_cast<int>(l.to)).getFen()});
        texts.push_back(l.text);
    }
    auto before      = boardSnapshot();
    const auto put   = placeAssistantComments(b.tree, b.chess960, targets, texts);
    const auto added = static_cast<int>(put.size());
    if (added == 0) {
        boardToast(TR("Every move has the assistant's comment already"));
        return;
    }
    const std::vector<std::string> basis{TR("the game's review, move by move, and the facts of its positions")};
    for (const auto& text : put) assistant_basis_[text] = basis;
    boardChanged(std::move(before));
    b.game = lineOf(b.tree, b.line);
    boardToast(trn("{} comment written · Ctrl+Z takes it back", "{} comments written · Ctrl+Z takes them all back",
                   added, added));
}

void App::drawBoardExplained() {
    auto& b = board_;
    const ui::TopicRegion topic(guide::topics::ExplainedMoves);
    if (engineGameHidesAids()) {
        drawEngineGameBanner();
        return;
    }
    if (gameLocked()) {
        drawFairPlayBanner(true);
        return;
    }
    // The explanations kept for this game and language.
    const auto lang     = assistantLang();
    const auto key      = reviewKey(b.tree.main);
    const auto lang_key = lang == commentary::Lang::Fr ? "fr" : "en";
    if (!explain_job_ && explained_of_ != key + "|" + lang_key) {
        explained_    = loadExplained(storage_.root() / "analyses", key, lang_key);
        explained_of_ = key + "|" + lang_key;
    }
    // While they are made, the lines that came.
    std::optional<Explained> live;
    if (explain_job_ && explain_job_->game == board_game_serial_) {
        std::lock_guard lock(*explain_job_->mutex);
        live = *explain_job_->made;
    }
    const Explained* e = live ? &*live : explained_ ? &*explained_ : nullptr;
    const ui::TextWrap wrap;
    const auto review = boardCommentaryReview();
    if (!e) {
        ImGui::TextUnformatted(
            TR("Every move explained: a line for each, theory grouped, the moves too simple said in "
               "a word, the others in a sentence of the assistant held to the review's facts."));
        if (!review || !review->report.complete)
            ImGui::TextDisabled("%s",
                                explain_after_review_
                                    ? TR("The game is being analysed: its moves are explained once its review is "
                                         "done.")
                                    : TR("They come from the game's review: “Explain every move” analyses the game "
                                         "first (Alt+G)."));
        if (!explain_after_review_ && ui::iconButton("explain_every_move", ui::Icon::Comment, TR("Explain every move")))
            explainEveryMove();
        return;
    }

    // What they are: the moves, by the model, by the templates, of theory, too simple.
    const auto c = explainedCounts(*e);
    ImGui::TextUnformatted(TR("Every move explained"));
    ImGui::SameLine();
    ImGui::TextDisabled(
        "· %s", trf("{} moves: {} by the model, {} by the templates, {} of theory in {} lines, {} too simple for more "
                    "than a word",
                    c.moves, c.model, c.templates + c.review, c.theory_moves, c.theory_lines, c.simple)
                    .c_str());
    std::string state;
    if (explain_job_) state = trf("In the background: {} of {}", explain_job_->done->load(), explain_job_->total);
    if (e->model_lines > 0) {
        const auto where = e->device == "gpu" || (explain_job_ && assistant_ && assistant_->onGpu())
                               ? std::string(TR("on the GPU"))
                               : std::string(TR("on the processor"));
        state += (state.empty() ? "" : " · ") + trf("{:.1f} s a move {}", e->model_secs / e->model_lines, where);
    }
    if (c.refused > 0)
        state += (state.empty() ? "" : " · ") + trn("the checker refused {} sentence of the model, replaced by the "
                                                    "template's (hover for why)",
                                                    "the checker refused {} sentences of the model, replaced by the "
                                                    "templates' (hover for why)",
                                                    c.refused, c.refused);
    if (!state.empty()) ImGui::TextDisabled("%s", state.c_str());
    if (explain_job_) {
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Cancel"))) *explain_job_->cancel = true;
    }
    if (!b.reviews.empty() && !e->review.empty() &&
        e->review !=
            explainedReview(b.reviews.front().engine, b.reviews.front().passes, b.reviews.front().positions.size()))
        ImGui::TextDisabled("%s",
                            TR("Written from an earlier pass of the review: “Explain again” follows the review as "
                               "it is now."));

    // Which: every move, the moves that matter, the key moments.
    const char* filters[] = {TR("Every move"), TR("The moves that matter"), TR("The key moments")};
    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine(0, px(2));
        const bool on = explained_filter_ == i;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ui::ACCENT);
        if (on) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32_WHITE);
        if (ImGui::SmallButton(fmt::format("{}##explained_filter{}", filters[i], i).c_str())) explained_filter_ = i;
        if (on) ImGui::PopStyleColor(2);
    }
    ImGui::Spacing();

    // The lines: the moves, the sentence in the assistant's colour, where it comes from.
    const float label_w = px(118);
    const auto col      = ImGui::ColorConvertU32ToFloat4(ui::assistantColour());
    for (std::size_t i = 0; i < e->lines.size(); ++i) {
        const auto& l = e->lines[i];
        if ((explained_filter_ == 1 && !l.matters) || (explained_filter_ == 2 && !l.key)) continue;
        ImGui::PushID(static_cast<int>(i));
        const auto label = l.from == l.to ? boardMoveLabel(static_cast<int>(l.to))
                                          : fmt::format("{} … {}", boardMoveLabel(static_cast<int>(l.from)),
                                                        boardMoveLabel(static_cast<int>(l.to)));
        const bool here  = b.line < 0 && b.ply >= static_cast<int>(l.from) && b.ply <= static_cast<int>(l.to);
        const float y    = ImGui::GetCursorPosY();
        if (ImGui::Selectable("##line", here, ImGuiSelectableFlags_AllowOverlap, ImVec2(label_w, 0)))
            showBoardLine(-1, static_cast<int>(l.to));
        ImGui::SameLine(0, 0);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - label_w);
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine(label_w + px(8));
        ImGui::SetCursorPosY(y);
        ImGui::BeginGroup();
        if (!l.written) {
            ImGui::TextDisabled("…");
        } else {
            ImGui::PushFont(ui::italicFont(), 0.0f);
            ImGui::TextColored(col, "%s", l.text.c_str());
            ImGui::PopFont();
            std::string source;
            switch (l.source) {
                case ExplainedLine::Source::Theory:
                    source = l.games > 0 ? trf("theory, {} games of your databases", ui::exactCount(l.games))
                                         : std::string(TR("theory"));
                    break;
                case ExplainedLine::Source::Model:
                    source = TR("the model");
                    break;
                case ExplainedLine::Source::Template:
                    source = l.refused ? TR("template: the checker refused the model's sentence") : TR("template");
                    break;
                case ExplainedLine::Source::Simple:
                    source = TR("too simple for more than a word");
                    break;
                case ExplainedLine::Source::Review:
                    source = TR("the review");
                    break;
            }
            ImGui::TextDisabled("· %s", source.c_str());
            if (l.refused && ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "%s", trf("The model's sentence was refused: {}.", assistantRefusalWords(*l.refused)).c_str());
        }
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(explain_job_.has_value() || c.written == 0);
    if (ImGui::Button(TR("Write them into the game"))) writeExplainedIntoGame();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s",
                          TR("The assistant's comments, marked as its own, beside the comment a move has already, "
                             "which is never replaced; Ctrl+Z takes them all back"));
    ImGui::SameLine();
    ImGui::BeginDisabled(explain_job_.has_value() || !review || !review->report.complete);
    if (!e->complete) {
        if (ImGui::Button(TR("Explain the rest"))) startExplaining(true);
        ImGui::SameLine();
    }
    if (ImGui::Button(TR("Explain again"))) startExplaining(false);
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s", TR("Kept with the review, beside the moves; “Write them into the game” makes them the "
                                 "assistant's comments, which Ctrl+Z takes back."));
}

}  // namespace fcg
