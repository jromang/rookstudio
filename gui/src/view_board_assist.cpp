// The assistant's comments on the Board page (mockup M5, boards 3 and 4): Alt+C comments the move shown, "Comment the
// key moments" the moves the review singles out, in the background; "Annotate the game" writes the review in. Each is
// one change, taken back by one Ctrl+Z; a comment already there, the user's or the assistant's, is never replaced.

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include <commentary/annotate.hpp>
#include <commentary/commentator.hpp>
#include <commentary/question.hpp>
#include <commentary/story.hpp>
#include <commentary/theory.hpp>
#include <commentary/writing.hpp>
#include <study/game_tree.hpp>

#include "app.hpp"
#include "assistant_notes.hpp"
#include "assistant_service.hpp"
#include "engine_oracle.hpp"
#include "game_graph.hpp"
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

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

}  // namespace

std::string App::boardTheoryKey() {
    const auto& b = board_;
    if (b.chess960 || b.tree.main.moves.empty()) return {};
    if (!b.tree.main.start_fen.empty() && b.tree.main.start_fen != std::string(chess::constants::STARTPOS)) return {};
    auto& s = shelf();
    std::optional<std::size_t> base;
    if (tree_db_ >= 0 && static_cast<std::size_t>(tree_db_) < s.entries().size()) base = tree_db_;
    for (std::size_t i = 0; i < s.entries().size() && !base; ++i)
        if (!s.entries()[i].source) base = i;
    if (!base || s.entries()[*base].source) return {};
    const auto facts = s.facts(*base);  // a database that changed: looked for again
    if (!facts.tree) return {};
    return fmt::format("{}|{}|{}|{}", reviewKey(b.tree.main), s.entries()[*base].folder.string(), facts.games,
                       facts.changed);
}

std::shared_ptr<const CommentaryReview> App::boardCommentaryReview() {
    auto& b = board_;
    if (b.reviews.empty() || b.chess960) return nullptr;
    // The game's theory, looked for once per game and database; the review is made again when it comes.
    const auto theory_key = boardTheoryKey();
    if (!theory_key.empty() && theory_key != theory_of_ && (!theory_job_ || theory_job_->of != theory_key)) {
        auto& s = shelf();
        std::optional<std::size_t> base;
        if (tree_db_ >= 0 && static_cast<std::size_t>(tree_db_) < s.entries().size()) base = tree_db_;
        for (std::size_t i = 0; i < s.entries().size() && !base; ++i)
            if (!s.entries()[i].source) base = i;
        if (base) {
            if (auto source = s.treeSource(*base); source && source->tree) {
                commentary::TheoryOptions options;
                // The game itself, when it came from that database, is not counted.
                if (board_origin_ && board_origin_->folder == s.entries()[*base].folder)
                    options.itself = board_origin_->id;
                const auto date = b.tree.main.header("Date");
                if (date.size() >= 4 && std::isdigit(static_cast<unsigned char>(date[0])))
                    options.year = std::atoi(date.substr(0, 4).c_str());
                TheoryJob job;
                job.of             = theory_key;
                board_theory_base_ = source->name;
                job.done           = std::async(std::launch::async, [source = *source, main = b.tree.main, options] {
                    const auto line = mainLineOf(main, false);
                    return line ? commentary::theoryOf(*source.tree, source.snapshot, *line, options)
                                : commentary::Opening{};
                });
                theory_job_        = std::move(job);
            }
        }
    }
    if (theory_job_ && theory_job_->done.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        board_theory_ = theory_job_->done.get();
        theory_of_    = theory_job_->of;
        theory_job_.reset();
        board_commentary_of_.clear();  // made again with it
        b.review_key.clear();          // and the symbols with it
    }
    const bool theory = !theory_key.empty() && theory_key == theory_of_;
    const auto& r     = b.reviews.front();
    const auto key    = fmt::format("{}|{}|{}|{}|{}", reviewKey(b.tree.main), r.engine, r.passes, r.positions.size(),
                                    theory ? theory_of_ : std::string());
    if (key != board_commentary_of_) {
        // Theory never hides an error the engine sees: a database of club games has its traps played a hundred
        // times. Its book ends before the first mistake.
        board_theory_used_ = theory ? board_theory_ : commentary::Opening{};
        if (board_theory_used_.book_plies > 0)
            if (const auto plain = commentaryReview(b.tree.main, b.chess960, r)) {
                for (const auto& m : plain->report.moves) {
                    if (m.ply > board_theory_used_.book_plies) break;
                    if (m.judgment && *m.judgment != commentary::Judgment::Inaccuracy) {
                        board_theory_used_.book_plies = m.ply - 1;
                        break;
                    }
                }
            }
        board_commentary_    = commentaryReview(b.tree.main, b.chess960, r, board_theory_used_);
        board_commentary_of_ = key;
    }
    return board_commentary_;
}

std::vector<std::string> App::commentBasis(bool from_review) {
    std::vector<std::string> out;
    if (from_review && !board_.reviews.empty())
        out.push_back(trf("{}: the game's analysis, position by position", board_.reviews.front().engine));
    else if (oracle_)
        out.push_back(trf("{}, 300,000 nodes per position", oracle_->engine()));
    out.emplace_back(TR("the facts of the positions its lines lead to"));
    if (from_review && !theory_of_.empty() && theory_of_ == boardTheoryKey())
        out.push_back(trf("« {} »: the game's theory and its novelty", board_theory_base_));
    out.emplace_back(assistant_ && assistant_->installed()
                         ? TR("written by the model, held to these facts; a template where it had nothing better")
                         : TR("a template comment, written without the model"));
    return out;
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
    job.basis = commentBasis(review && review->report.get(static_cast<std::size_t>(b.ply)));
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
    job.basis = commentBasis(true);
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
        assistant_basis_[texts[i]] = job.basis;
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

void App::drawReviewCurve() {
    auto& b = board_;
    if (b.reviews.empty() || b.tree.main.moves.empty()) return;
    const auto graph = buildGameGraph(b.tree.main, b.chess960, GraphChart::Win,
                                      {GraphAnalysis{b.reviews.front().engine, "", 0, &b.reviews.front()}}, b.judged);
    if (graph.analyses.empty() || graph.analyses.front().x.empty()) return;
    const auto& curve = graph.analyses.front();
    const int plies   = std::max(1, graph.plies);
    const auto win_at = [&](int ply) {
        for (std::size_t i = 0; i < curve.x.size() && i < curve.win.size(); ++i)
            if (static_cast<int>(std::lround(curve.x[i])) == ply) return curve.win[i];
        return std::nanf("");
    };

    const float w = ImGui::GetContentRegionAvail().x, h = px(88), pad = px(8);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##review_curve", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    const bool light = ui::currentTheme() == ui::Theme::Light;
    auto* dl         = ImGui::GetWindowDrawList();
    const auto x_of  = [&](float ply) { return p0.x + pad + (w - 2 * pad) * ply / static_cast<float>(plies); };
    const auto y_of  = [&](float win) { return p0.y + pad + (h - 2 * pad) * (1.0f - std::clamp(win, 0.0f, 1.0f)); };
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), light ? IM_COL32(243, 245, 248, 255) : IM_COL32(32, 36, 43, 255),
                      px(6));
    // Even chances, and the move shown.
    dl->AddLine(ImVec2(p0.x + pad, y_of(0.5f)), ImVec2(p0.x + w - pad, y_of(0.5f)),
                light ? IM_COL32(201, 206, 214, 255) : IM_COL32(70, 76, 88, 255));
    if (b.line < 0) {
        const float cx = x_of(static_cast<float>(boardMainPly()));
        dl->AddLine(ImVec2(cx, p0.y + px(3)), ImVec2(cx, p0.y + h - px(3)), ui::MUTED);
    }
    std::vector<ImVec2> points;
    for (std::size_t i = 0; i < curve.x.size() && i < curve.win.size(); ++i)
        if (std::isfinite(curve.win[i])) points.emplace_back(x_of(curve.x[i]), y_of(curve.win[i]));
    if (points.size() >= 2)
        dl->AddPolyline(points.data(), static_cast<int>(points.size()), ui::ACCENT, ImDrawFlags_None, px(2.2f));

    // The review's key moments, dark for a good move, orange for a mistake.
    struct Dot {
        int ply;
        ImVec2 at;
    };
    std::vector<Dot> dots;
    if (const auto review = boardCommentaryReview()) {
        for (const auto key : review->report.key_moments) {
            const int ply = static_cast<int>(key);
            if (const float v = win_at(ply); std::isfinite(v)) dots.push_back({ply, ImVec2(x_of(ply), y_of(v))});
        }
    }
    const auto judgement = [&](int ply) {
        return ply >= 1 && static_cast<std::size_t>(ply - 1) < b.judged.size()
                   ? b.judged[static_cast<std::size_t>(ply - 1)].judgement
                   : Judgement::None;
    };
    const ImU32 good = light ? IM_COL32(31, 79, 158, 255) : IM_COL32(150, 186, 240, 255);
    const ImU32 bad  = IM_COL32(224, 138, 46, 255);
    for (const auto& d : dots) {
        dl->AddCircleFilled(d.at, px(6), light ? IM_COL32(255, 255, 255, 255) : IM_COL32(32, 36, 43, 255));
        dl->AddCircleFilled(d.at, px(4.5f), judgement(d.ply) != Judgement::None ? bad : good);
    }

    // The move under the pointer, a dot first: its tooltip, and where a click or a drag goes.
    if (hovered || active) {
        const ImVec2 m  = ImGui::GetIO().MousePos;
        // (Not "near": Windows' headers make it a macro.)
        const Dot* under = nullptr;
        for (const auto& d : dots)
            if (std::hypot(d.at.x - m.x, d.at.y - m.y) <= px(8)) under = &d;
        const int ply = under
                            ? under->ply
                            : std::clamp(static_cast<int>(std::lround((m.x - p0.x - pad) / std::max(1.0f, w - 2 * pad) *
                                                                      static_cast<float>(plies))),
                                         0, plies);
        if (active && (b.line >= 0 || boardMainPly() != ply)) showBoardLine(-1, ply);
        if (hovered) {
            std::string tip = ply == 0 ? std::string(TR("at the start")) : boardMoveLabel(ply);
            if (ply > 0) tip += judgementMark(judgement(ply));
            if (const float v = win_at(ply); std::isfinite(v))
                tip += " · " + trf("White's chances {} %", static_cast<int>(std::lround(v * 100.0f)));
            if (under && review_moments_of_ == board_commentary_of_) {
                for (const auto& [at, text] : review_moments_)
                    if (static_cast<int>(at) == ply) tip += "\n" + text;
            }
            if (ImGui::BeginTooltip()) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
                ImGui::TextUnformatted(tip.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
    }
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", TR("A click on the curve goes to the move. The dots are the key moments: dark for a good "
                                 "move, orange for a mistake."));
    ImGui::PopTextWrapPos();
}

void App::drawReviewAssistant() {
    auto& b           = board_;
    const auto review = boardCommentaryReview();
    if (!review) return;
    ImGui::Spacing();
    // Where the game left theory, in the user's database (M5, board 4).
    if (!theory_of_.empty() && theory_of_ == boardTheoryKey()) {
        const auto& t = board_theory_used_;
        std::string line;
        if (t.book_plies > 0)
            line = trf("Theory in « {} » up to {}", board_theory_base_, boardMoveLabel(static_cast<int>(t.book_plies)));
        else
            line = trf("No theory from « {} » before the first error", board_theory_base_);
        if (t.book_plies < board_theory_.book_plies)
            line += " · " + trf("its games played on to {}, an error the engine sees",
                                boardMoveLabel(static_cast<int>(board_theory_.book_plies)));
        if (board_theory_.novelty) {
            // Measured against the games that had come so far: a few, or thousands (M5, board 4).
            const auto move = boardMoveLabel(static_cast<int>(*board_theory_.novelty) + 1);
            const auto n    = board_theory_.reached;
            line += " · " + (n == 0 ? trf("{} was new: no game had played it", move)
                                    : trn("{} was new: the one game that had reached the position played another move",
                                          "{} was new: none of the {} games that had reached the position played it", n,
                                          move, ui::exactCount(n)));
        }
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", line.c_str());
        ImGui::PopTextWrapPos();
    } else if (theory_job_) {
        ImGui::TextDisabled("%s", trf("Looking for the game's theory in « {} »…", board_theory_base_).c_str());
    }
    // The moves the review singles out, in words: a click goes to the move.
    if (review->report.complete) {
        if (review_moments_of_ != board_commentary_of_) {
            review_moments_ = commentary::templateComments(review->line, review->report, review->analyses, {}, nullptr,
                                                           nullptr, lang(), letters());
            review_moments_of_ = board_commentary_of_;
        }
        if (!review_moments_.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", TR("THE KEY MOMENTS, IN WORDS"));
            ImGui::PushTextWrapPos();
            for (const auto& [ply, text] : review_moments_) {
                ImGui::PushID(static_cast<int>(ply));
                const auto label = boardMoveLabel(static_cast<int>(ply));
                if (ImGui::Selectable(fmt::format("{}##moment", label).c_str(),
                                      b.line < 0 && b.ply == static_cast<int>(ply), ImGuiSelectableFlags_None,
                                      ImVec2(ImGui::CalcTextSize(label.c_str()).x, 0)))
                    showBoardLine(-1, static_cast<int>(ply));
                ImGui::SameLine();
                ImGui::TextUnformatted(text.c_str());
                ImGui::PopID();
            }
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::Spacing();
    // The story of the game, from the review's facts, and what each side should work on (M5, board 4): told once for
    // each review.
    const std::string result = b.tree.main.result.empty() ? "*" : b.tree.main.result;
    const auto story_of      = fmt::format("{}|{}|{}", board_commentary_of_, result, static_cast<int>(lang()));
    if (review_story_of_ != story_of) {
        review_story_ =
            commentary::story(review->line, result, review->report, review->analyses, nullptr, lang(), letters());
        review_work_on_ =
            commentary::workOn(review->line, result, review->report, review->analyses, nullptr, lang(), letters());
        review_story_of_ = story_of;
    }
    if (std::any_of(review_story_.begin(), review_story_.end(), [](const auto& p) { return !p.lesson; })) {
        ImGui::TextDisabled("%s", TR("THE STORY OF THE GAME"));
        ImGui::PushTextWrapPos();
        for (const auto& p : review_story_) {
            if (p.lesson) continue;  // in a section of their own, below
            ImGui::TextUnformatted(p.text.c_str());
            if (p.ply && ImGui::IsItemClicked()) showBoardLine(-1, static_cast<int>(*p.ply));
            if (p.ply && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("A click goes to the move it is about"));
        }
        ImGui::TextDisabled("%s", TR("Written from the review's facts: its verdicts, its evaluations, the positions."));
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }
    // What each side should work on: the kinds of mistakes it made most, the moves that show them a click away, and
    // the advice they call for.
    if (!review_work_on_.empty()) {
        ImGui::TextDisabled("%s", TR("WHAT TO WORK ON"));
        for (const auto& w : review_work_on_) {
            const bool white = w.side == commentary::Color::White;
            ImGui::PushID(white ? "white" : "black");
            ImGui::TextUnformatted(white ? TR("For White") : TR("For Black"));
            if (const auto name = b.tree.main.header(white ? "White" : "Black"); !name.empty() && name != "?") {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", name.c_str());
            }
            for (std::size_t t = 0; t < w.topics.size(); ++t) {
                const auto& topic = w.topics[t];
                ImGui::PushID(static_cast<int>(t));
                ImGui::Bullet();
                ImGui::SameLine();
                auto name = topic.name;
                if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
                ImGui::TextUnformatted(trf("{}:", name).c_str());
                for (std::size_t k = 0; k < topic.plies.size(); ++k) {
                    const int ply = static_cast<int>(topic.plies[k]);
                    auto move     = boardMoveLabel(ply);
                    if (ply >= 1 && static_cast<std::size_t>(ply - 1) < b.judged.size())
                        move += judgementMark(b.judged[static_cast<std::size_t>(ply - 1)].judgement);
                    const float move_w = ImGui::CalcTextSize(move.c_str()).x;
                    ui::sameLineOrWrap(move_w);
                    ImGui::PushID(static_cast<int>(k));
                    if (ImGui::Selectable(move.c_str(), b.line < 0 && b.ply == ply, ImGuiSelectableFlags_None,
                                          ImVec2(move_w, 0)))
                        showBoardLine(-1, ply);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("A click goes to the move"));
                    ImGui::PopID();
                }
                ImGui::PopID();
            }
            if (!w.advice.empty()) {
                ImGui::PushTextWrapPos();
                ImGui::TextDisabled("%s", w.advice.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::PopID();
            ImGui::Spacing();
        }
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
