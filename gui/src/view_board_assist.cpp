// The assistant's comments on the Board page (mockup M5, boards 3 and 4): Alt+C comments the move shown, "Comment the
// key moments" the moves the review singles out, in the background; "Annotate the game" writes the review in. Each is
// one change, taken back by one Ctrl+Z; a comment already there, the user's or the assistant's, is never replaced: the
// assistant's goes beside another's (#60, mockup M11, board 5).

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
#include <commentary/story_model.hpp>
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

// A card tinted with the assistant's colour, as its story is shown (M11, board 5); pair with endTintedCard().
void beginTintedCard(const char* id) {
    const auto tint = ImGui::ColorConvertU32ToFloat4(ui::assistantColour());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(tint.x, tint.y, tint.z, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(tint.x, tint.y, tint.z, 0.40f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(14), px(12)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(8));
    ImGui::BeginChild(id, ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
}

void endTintedCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// The assistant's bubble and a line in its colour, in semi-bold.
void assistantTitle(const char* text) {
    const ImU32 col   = ui::assistantColour();
    const float fs    = ImGui::GetFontSize();
    const ImVec2 at   = ImGui::GetCursorScreenPos();
    auto* dl          = ImGui::GetWindowDrawList();
    const float width = ImGui::GetContentRegionAvail().x;
    ui::drawIcon(dl, ui::Icon::Comment, ImVec2(at.x + fs / 2, at.y + ImGui::GetTextLineHeight() / 2), fs * 0.85f, col);
    const ImVec2 text_at(at.x + fs + px(6), at.y);
    const float wrap = std::max(px(80), width - fs - px(6));
    dl->AddText(ImGui::GetFont(), fs, text_at, col, text, nullptr, wrap);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(text_at.x + 0.6f, text_at.y), col, text, nullptr, wrap);
    const auto size = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, wrap, text);
    ImGui::Dummy(ImVec2(width, size.y));
}

}  // namespace

// What the checker refused in a passage of the story or a move's sentence, in words (#62, #94). A motif or a notion is
// known to the checker by its French word, quoted only to a reader of French.
std::string assistantRefusalWords(const commentary::Refusal& r) {
    using K       = commentary::Refusal::Kind;
    const bool fr = lang() == commentary::Lang::Fr;
    switch (r.kind) {
        case K::Notation:
            return trf("it quoted “{}”, a move the review does not give", r.what);
        case K::Motif:
            return fr ? trf("it said “{}”, a tactic the review's facts do not show", r.what)
                      : std::string(TR("it named a tactic the review's facts do not show"));
        case K::Colour:
            return trf("it gave “{}” the wrong colour", r.what);
        case K::Piece:
            return trf("it put “{}” where the board has no such piece", r.what);
        case K::Unsupported:
            return fr ? trf("it said “{}”, which the review's facts do not support", r.what)
                      : std::string(TR("it named an idea the review's facts do not support"));
        case K::TooManyMoves:
            return TR("it quoted too many moves: a story is not a variation");
        case K::Aside:
            return trf("it spoke of its sources (“{}”) instead of the game", r.what);
        case K::Verdict:
            return trf("it said “{}”, which is not the review's verdict", r.what);
        case K::Evaluation:
            return trf("it said “{}”, which the evaluation contradicts", r.what);
        case K::Elision:
            return trf("its French forgot an elision: “{}”", r.what);
        case K::Name:
            return trf("it named “{}”, a name the review does not give", r.what);
        case K::Number:
            return trf("it said “{}”, a number the review does not give", r.what);
        case K::Board:
            return trf("it said “{}” of a position that does not have it", r.what);
        case K::Material:
            return trf("it said “{}” changes hands, which the line it speaks of does not count", r.what);
        case K::Reason:
            return trf("it praised the better move for “{}”, which the move played does too", r.what);
        case K::Wording:
            return trf("its words say something else: “{}”", r.what);
        case K::Grammar:
            return TR("it was cut short, or wrote outside the words it may use");
        default:
            return TR("it wrote nothing");
    }
}

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
    updateReviewTables();
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
    const auto key    = fmt::format("{}|{}|{}|{}|{}|{}", reviewKey(b.tree.main), r.engine, r.passes, r.positions.size(),
                                    theory ? theory_of_ : std::string(), review_tables_taught_);
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
        board_commentary_    = commentaryReview(b.tree.main, b.chess960, r, board_theory_used_, reviewTables());
        board_commentary_of_ = key;
    }
    return board_commentary_;
}

const commentary::Tablebases* App::reviewTables() const {
    return review_tables_taught_ > 0 ? review_tables_.get() : nullptr;
}

void App::updateReviewTables() {
    auto& b        = board_;
    const auto key = b.chess960 || b.tree.main.moves.empty() ? std::string() : reviewKey(b.tree.main);
    if (key != review_tables_of_) {
        // Another game: its own tables; an asking still going stops, its answers kept in the profile all the same.
        if (review_tables_job_) *review_tables_job_->cancel = true;
        review_tables_of_      = key;
        review_tables_         = std::make_shared<commentary::Tablebases>();
        review_tables_taught_  = 0;
        review_tables_once_    = false;
        review_tables_not_now_ = false;
        review_tables_started_ = false;
        review_tables_error_   = {};
        review_tables_positions_.clear();
        review_tables_line_.reset();
        if (!key.empty()) {
            if (auto line = mainLineOf(b.tree.main, false)) {
                review_tables_positions_ = tablePositions(*line, *review_tables_);
                review_tables_line_      = std::make_shared<const commentary::MainLine>(std::move(*line));
            }
        }
    }
    // Its end: the review made again, held to what they said (an earlier game's asking just ends).
    if (review_tables_job_ &&
        review_tables_job_->finished.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto error      = review_tables_job_->finished.get();
        const bool ours = review_tables_job_->of == review_tables_of_;
        const int done  = *review_tables_job_->done;
        review_tables_job_.reset();
        if (ours) {
            review_tables_error_  = std::move(error);
            review_tables_taught_ = std::max(done, 1);
            b.review_key.clear();  // the moves' symbols with it
        }
    }
    const bool allowed = lichess_.tablebase == 1 || review_tables_once_;
    if (!allowed || lichess_.tablebase == 2 || review_tables_started_ || review_tables_job_ ||
        review_tables_positions_.empty() || !review_tables_line_ || !chessBoard())
        return;
    review_tables_started_ = true;
    ReviewTablesJob job;
    job.of       = key;
    job.total    = static_cast<int>(review_tables_positions_.size());
    job.finished = std::async(std::launch::async,
                              [positions = review_tables_positions_, line = review_tables_line_, root = storage_.root(),
                               tables = review_tables_, cancel = job.cancel,
                               done = job.done] { return askTables(positions, *line, root, *tables, *cancel, *done); });
    review_tables_job_ = std::move(job);
}

void App::drawReviewTables() {
    if (review_tables_positions_.empty() || lichess_.tablebase == 2) return;
    const int n = static_cast<int>(review_tables_positions_.size());
    ImGui::PushTextWrapPos();
    if (review_tables_job_ && review_tables_job_->of == review_tables_of_) {
        ImGui::TextDisabled("%s", trf("Asking Lichess's tables about the endgame: {} of {} positions…",
                                      review_tables_job_->done->load(), review_tables_job_->total)
                                      .c_str());
    } else if (review_tables_taught_ > 0) {
        if (review_tables_error_.failed())
            ImGui::TextDisabled("%s", trf("Lichess's tables could not be asked to the end ({}): the engine judged the "
                                          "moves they did not answer for.",
                                          lichess::describe(review_tables_error_))
                                          .c_str());
        else
            ImGui::TextDisabled("%s", trn("The endgame judged by Lichess's tables: {} position of seven pieces or "
                                          "fewer, each move by what it does to the exact result.",
                                          "The endgame judged by Lichess's tables: {} positions of seven pieces or "
                                          "fewer, each move by what it does to the exact result.",
                                          n, n)
                                          .c_str());
    } else if (lichess_.tablebase == 0 && !review_tables_once_ && !review_tables_not_now_) {
        // The question of the Analysis tab, for the whole game (M4, board 5).
        ui::beginCard("review_tables");
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(trn("Lichess's tables can judge this endgame exactly: {} position of seven pieces or "
                                   "fewer.",
                                   "Lichess's tables can judge this endgame exactly: {} positions of seven pieces or "
                                   "fewer.",
                                   n, n)
                                   .c_str());
        ImGui::TextDisabled(
            "%s", TR("To ask, their FEN would go to tablebase.lichess.ovh, one at a time, without an account; the "
                     "answers are kept in the profile."));
        if (ImGui::SmallButton(TR("Ask for this game"))) review_tables_once_ = true;
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Always"))) {
            lichess_.tablebase = 1;
            saveLichessSettings();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Not now"))) review_tables_not_now_ = true;
        ImGui::TextDisabled("%s",
                            TR("“Always” can be changed on the Lichess page, under Online for the analysis, which "
                               "also says what is sent."));
        ImGui::PopTextWrapPos();
        ui::endCard();
    }
    ImGui::PopTextWrapPos();
}

std::vector<std::string> App::commentBasis(bool from_review) {
    std::vector<std::string> out;
    if (from_review && !board_.reviews.empty())
        out.push_back(trf("{}: the game's analysis, position by position", board_.reviews.front().engine));
    else if (oracle_)
        out.push_back(trf("{}, 300,000 nodes per position", oracle_->engine()));
    out.emplace_back(TR("the facts of the positions its lines lead to"));
    if (from_review && !theory_of_.empty() && theory_of_ == boardTheoryKey())
        out.push_back(trf("“{}”: the game's theory and its novelty", board_theory_base_));
    out.emplace_back(assistant_ && assistant_->installed()
                         ? TR("written by the model, held to these facts; a template where it had nothing better")
                         : TR("a template comment, written without the model"));
    return out;
}

void App::commentBoardMove() {
    if (aidsLocked()) {  // fair play (#48)
        boardToast(TR("Locked while one of your games is played on Lichess."));
        return;
    }
    if (engineGameHidesAids()) {  // a game against an engine (#83)
        boardToast(TR("Hidden until the end of your game: Show them first, and the game is marked “with aids”"));
        return;
    }
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
    // Beside another comment, never instead (#60): only its own is never written twice.
    if (byAssistant(*notes)) {
        boardToast(TR("The assistant has commented the move already · a double click corrects it"));
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
    // The tables: the review's for the game's move, else those that know the position (#42).
    auto tables  = review && reviewTables() ? review_tables_ : boardPositionTables();
    job.texts    = std::async(std::launch::async,
                              [review, ply = static_cast<std::size_t>(b.ply), fen = job.targets.front().fen, before_fen,
                               played, service = assistant_, cancel = job.cancel, oracle = oracle_, done = job.done,
                               tables, l = lang(), letters = letters()]() {
                               std::vector<std::string> out(1);
                               CancellableModel model(*service, cancel);  // cancelled, it asks nothing more (#98)
                               if (review) {
                                   if (const auto* m = review->report.get(ply)) {
                                       const auto ctx = commentary::moveContext(review->line, *m, review->analyses,
                                                                                nullptr, tables.get(), oracle.get());
                                       if (const auto w = commentary::writtenComment(model, ctx, l, letters))
                                           out[0] = w->text;
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
                                   const auto a   = commentary::answer(asked, model, oracle.get(), tables.get());
                                   if (const auto* said = std::get_if<commentary::Answered>(&a)) out[0] = said->text;
                               }
                               ++*done;
                               return out;
                              });
    comment_job_ = std::move(job);
    boardToast(TR("The assistant is commenting the move…"));
}

void App::commentKeyMoments() {
    if (gameLocked()) {  // fair play (#48)
        boardToast(TR("Locked while one of your games is played on Lichess."));
        return;
    }
    auto& b = board_;
    if (comment_job_) return;
    const auto review = boardCommentaryReview();
    if (!review || !review->report.complete) {
        boardToast(TR("Analyse the game first (Alt+G): its key moments come from the review"));
        return;
    }
    const auto* engine = analysisEngine();
    if (!engine)
        oracle_.reset();
    else if (!oracle_ || oracle_->engine() != engine->name)
        oracle_ = std::make_shared<EngineOracle>(*engine);
    // The moves that deserve a comment: mistakes, only moves, turning points, plans, the novelty; those without the
    // assistant's yet. A move that has another comment (Lichess's analysis, the user's, an annotator's) gets the
    // assistant's beside it (#60). The templates' sentences the checks of the boards refuse are left out (#64).
    const auto picked = commentary::heldTemplateComments(review->line, review->report, review->analyses, {},
                                                         reviewTables(), nullptr, lang(), letters());
    CommentJob job;
    job.key_moments = true;
    job.game        = board_game_serial_;
    std::vector<std::pair<std::size_t, std::string>> wanted;
    for (const auto& [ply, text] : picked) {
        const MoveNotes* notes = notesAt(b.tree, -1, static_cast<int>(ply));
        if (!notes || byAssistant(*notes)) continue;
        job.targets.push_back(
            {-1, static_cast<int>(ply), boardAt(b.tree.main, b.chess960, static_cast<int>(ply)).getFen()});
        wanted.push_back({ply, text});
    }
    if (wanted.empty()) {
        boardToast(picked.empty() ? TR("Nothing in the game calls for a comment")
                                  : TR("Every key moment has the assistant's comment already"));
        return;
    }
    job.total = static_cast<int>(wanted.size());
    job.basis = commentBasis(true);
    // With the model, its comments (the template's where it has nothing better); without it, the template's.
    std::shared_ptr<AssistantService> model;
    if (assistant_->installed()) model = assistant_;
    job.texts = std::async(std::launch::async, [review, wanted, model, oracle = oracle_, done = job.done,
                                                tables = reviewTables() ? review_tables_ : nullptr, cancel = job.cancel,
                                                l = lang(), letters = letters()]() {
        std::vector<std::string> out;
        for (const auto& [ply, text] : wanted) {
            if (*cancel) break;
            std::string said = text;
            if (model) {
                if (const auto* m = review->report.get(ply)) {
                    const auto ctx = commentary::moveContext(review->line, *m, review->analyses, nullptr, tables.get(),
                                                             oracle.get());
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
    // All of them in one change, on the moves that still lead where they did and have no comment of the assistant's
    // yet; a comment of another's stays as it is, the assistant's after it (#60).
    auto before      = boardSnapshot();
    const auto put   = placeAssistantComments(b.tree, b.chess960, job.targets, texts);
    const auto added = static_cast<int>(put.size());
    for (const auto& text : put) assistant_basis_[text] = job.basis;
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
    if (gameLocked()) {  // fair play (#48)
        boardToast(TR("Locked while one of your games is played on Lichess."));
        return;
    }
    auto& b           = board_;
    const auto review = boardCommentaryReview();
    if (!review || !review->report.complete) {
        boardToast(TR("Analyse the game first (Alt+G): the annotation is its review written into the game"));
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
    // Where the game left theory, in the user's database (M5, board 4).
    if (!theory_of_.empty() && theory_of_ == boardTheoryKey()) {
        const auto& t = board_theory_used_;
        std::string line;
        if (t.book_plies > 0)
            line = trf("Theory in “{}” up to {}", board_theory_base_, boardMoveLabel(static_cast<int>(t.book_plies)));
        else
            line = trf("No theory from “{}” before the first error", board_theory_base_);
        if (t.book_plies < board_theory_.book_plies)
            line += " · " + trf("its games went on to {}, a move the engine sees as an error",
                                boardMoveLabel(static_cast<int>(board_theory_.book_plies)));
        if (board_theory_.novelty) {
            // Measured against the games that had come so far: a few, or thousands (M5, board 4).
            const auto move = boardMoveLabel(static_cast<int>(*board_theory_.novelty) + 1);
            const auto n    = board_theory_.reached;
            line += " · " +
                    (n == 0   ? trf("{} was new: no game had played it", move)
                     : n == 1 ? trf("{} was new: the one game that had reached the position played another move", move)
                              : trn("{} was new: none of the {} game that had reached the position played it",
                                    "{} was new: none of the {} games that had reached the position played it", n, move,
                                    ui::exactCount(n)));
        }
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", line.c_str());
        ImGui::PopTextWrapPos();
    } else if (theory_job_) {
        ImGui::TextDisabled("%s", trf("Looking for the game's theory in “{}”…", board_theory_base_).c_str());
    }
    // How its endgame is judged: Lichess's tables, asked when allowed (#42).
    drawReviewTables();
    // The moves the review singles out, in words: a click goes to the move.
    if (review->report.complete) {
        if (review_moments_of_ != board_commentary_of_) {
            review_moments_    = commentary::heldTemplateComments(review->line, review->report, review->analyses, {},
                                                                  reviewTables(), nullptr, lang(), letters());
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
        review_story_    = commentary::heldStory(review->line, result, review->report, review->analyses, reviewTables(),
                                                 lang(), letters());
        review_work_on_  = commentary::workOn(review->line, result, review->report, review->analyses, reviewTables(),
                                              lang(), letters());
        review_story_of_ = story_of;
    }
    drawReviewStory(*review);
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
    // The assistant's gestures on the game.
    ImGui::BeginDisabled(comment_job_.has_value() || !review->report.complete);
    if (ui::iconButton("comment_key_moments", ui::Icon::Comment, TR("Comment the key moments"))) commentKeyMoments();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", assistant_ && assistant_->installed()
                                    ? TR("A comment by the assistant on each, in the background, beside the comment a "
                                         "move has already, which is never replaced; Ctrl+Z takes them all back")
                                    : TR("Template comments, without the assistant's model: the Questions tab offers "
                                         "it, for fuller ones"));
    ImGui::SameLine();
    ImGui::BeginDisabled(!review->report.complete);
    if (ImGui::Button(TR("Annotate the game"))) annotateBoardGame();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", TR("The evaluations, the symbols and the better lines written into the notation; "
                                   "Ctrl+Z takes them back"));
    // Every move explained (#94), in the Explained tab beside the moves; the guided visit of the key moments.
    ui::sameLineOrWrap(ImGui::CalcTextSize(TR("Explain every move")).x + ImGui::GetStyle().FramePadding.x * 2);
    ImGui::BeginDisabled(explain_job_.has_value() || !review->report.complete);
    if (ImGui::Button(TR("Explain every move"))) explainEveryMove();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", TR("A line for each move, beside the moves, kept with the review: theory grouped, the "
                                   "moves too simple said in a word, the others by the assistant, held to the facts"));
    ui::sameLineOrWrap(ImGui::CalcTextSize(TR("Guided visit")).x + ImGui::GetStyle().FramePadding.x * 2);
    ImGui::BeginDisabled(!review->report.complete);
    if (ImGui::Button(TR("Guided visit"))) startVisit();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", TR("Each key moment of the review: what would you have played? Then the engine's move "
                                   "and why; a wrong answer becomes a card of today's review"));
    // What the last gesture did, said here too: the notation that shows it is in the other tab.
    if (!b.toast.empty() && ImGui::GetTime() - b.toast_time < 4.0) {
        ImGui::PushTextWrapPos();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", b.toast.c_str());
        ImGui::PopTextWrapPos();
    }
}

std::string App::storyWithoutModel() const {
    if (!assistant_) return TR("the program was built without the assistant");
    const auto& a = *assistant_;
    if (!a.installed()) return TR("the assistant's model is not downloaded (the Questions tab offers it)");
    if (!a.knowsComments()) return TR("the model chosen has not learnt RookStudio's comments");
    // A tournament on this machine has the processor: the model, which would take it from its engines, waits (M11,
    // board 5), unless it runs on a GPU no tournament uses.
    const bool on_gpu = a.onGpu() && a.gpuTaken().empty();
    for (const auto& r : runs_) {
        const auto* t = r.run->tournament();
        if (r.run->running() && !r.run->remote() && t && !on_gpu)
            return trf("the tournament “{}” has the processor", t->spec().name);
    }
    return {};
}

void App::writeStory(bool again) {
    auto& b           = board_;
    const auto review = boardCommentaryReview();
    if (!review || !review->report.complete || story_job_ || gameLocked() || !storyWithoutModel().empty()) return;
    StoryJob job;
    job.of    = review_story_of_;
    job.round = again ? model_story_round_ + 1 : 0;
    for (const auto& p : review_story_) job.templ.push_back(p.templ);
    job.total                = static_cast<int>(job.templ.size());
    const std::string result = b.tree.main.result.empty() ? "*" : b.tree.main.result;
    commentary::Players players{b.tree.main.header("White"), b.tree.main.header("Black")};
    job.story  = std::async(std::launch::async, [review, model = assistant_, result, players, round = job.round,
                                                 tables = reviewTables() ? review_tables_ : nullptr, l = lang(),
                                                 letters = letters(), cancel = job.cancel, done = job.done]() {
        std::string error;
        auto told =
            commentary::writtenStory(*model, review->line, result, review->report, review->analyses, tables.get(), l,
                                     letters, players, round, cancel.get(), done.get(), &error);
        return std::pair{std::move(told), std::move(error)};
    });
    story_job_ = std::move(job);
    story_template_shown_ = false;
    story_cancelled_of_.clear();
}

void App::pollStory() {
    if (!story_job_ || !story_job_->story.valid() ||
        story_job_->story.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    auto job = std::move(*story_job_);
    story_job_.reset();
    auto [told, why] = job.story.get();
    if (*job.cancel) {
        story_cancelled_of_ = job.of;  // the templates' until "Write it again"
        return;
    }
    model_story_       = std::move(told);
    model_story_of_    = job.of;
    model_story_round_ = job.round;
    model_story_error_ = std::move(why);
}

std::string App::storyText() const {
    const bool model = model_story_of_ == review_story_of_ && !story_template_shown_ &&
                       std::any_of(model_story_.begin(), model_story_.end(), [](const auto& p) { return p.by_model; });
    std::string out;
    // A passage none of whose sentences holds to the boards is not shown (#64).
    const auto add = [&](const std::string& text) {
        if (!text.empty()) out += (out.empty() ? "" : "\n\n") + text;
    };
    for (const auto& p : model ? model_story_ : review_story_) add(p.passage.text);
    return out;
}

void App::drawReviewStory(const CommentaryReview& review) {
    if (review_story_.empty()) return;
    // A later pass of the analysis that tells the same story (the templates' passages the same) keeps the model's, or
    // the one it is writing: its facts did not change. Another story is written again.
    std::vector<std::string> told;
    for (const auto& p : review_story_) told.push_back(p.templ);
    const auto same = [&](const std::vector<commentary::StoryPassage>& story) {
        return story.size() == told.size() && std::equal(story.begin(), story.end(), told.begin(),
                                                         [](const auto& p, const auto& t) { return p.templ == t; });
    };
    if (model_story_of_ != review_story_of_ && !model_story_.empty() && same(model_story_))
        model_story_of_ = review_story_of_;
    if (story_job_ && story_job_->of != review_story_of_) {
        if (story_job_->templ == told)
            story_job_->of = review_story_of_;
        else
            *story_job_->cancel = true;
    }
    const auto without = storyWithoutModel();
    if (without.empty() && review.report.complete && model_story_of_ != review_story_of_ && !story_job_ &&
        story_cancelled_of_ != review_story_of_)
        writeStory(false);
    const bool writing = story_job_ && story_job_->of == review_story_of_;
    const bool written = model_story_of_ == review_story_of_ && !model_story_.empty();
    const bool by_model =
        written && std::any_of(model_story_.begin(), model_story_.end(), [](const auto& p) { return p.by_model; });
    const auto passage = [&](const std::string& text, const std::optional<std::size_t>& ply) {
        if (text.empty()) return;  // none of its sentences holds to the boards (#64)
        ImGui::TextUnformatted(text.c_str());
        if (ply && ImGui::IsItemClicked()) showBoardLine(-1, static_cast<int>(*ply));
        if (ply && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("A click goes to the move it is about"));
    };
    const auto templates = [&] {
        for (const auto& p : review_story_) passage(p.passage.text, p.passage.ply);  // the lessons: below, apart
    };
    ImGui::TextDisabled("%s", TR("THE STORY OF THE GAME"));
    ImGui::PushTextWrapPos();
    if (writing) {
        // The model writes it in the background: the templates' meanwhile.
        assistantTitle(trf("The assistant's model is writing the story: {} of {} passages…", story_job_->done->load(),
                           story_job_->total)
                           .c_str());
        if (ImGui::SmallButton(TR("Cancel"))) *story_job_->cancel = true;
        ImGui::BeginDisabled();
        templates();
        ImGui::EndDisabled();
    } else if (by_model) {
        // The model's story, in a card of the assistant's colour (M11, board 5).
        beginTintedCard("##model_story");
        ImGui::PushTextWrapPos();
        if (story_template_shown_) {
            ImGui::TextDisabled("%s", TR("The templates' version, written from the same facts"));
            templates();
        } else {
            assistantTitle(TR("Written by the assistant's model, checked against the review's facts"));
            for (const auto& p : model_story_) passage(p.passage.text, p.passage.ply);
            // What the checker did: each move, evaluation and name comes from the review; what it refused, and which
            // passages are the templates'.
            int first = 0, second = 0, fallen = 0;
            std::vector<std::string> refused;
            for (const auto& p : model_story_) {
                if (p.by_model && p.attempts <= 1) ++first;
                if (p.by_model && p.attempts >= 2) ++second;
                if (!p.by_model && p.attempts > 0 && !p.passage.text.empty()) ++fallen;  // the template shown instead
                for (const auto& [text, why] : p.refused) refused.push_back(assistantRefusalWords(why));
            }
            std::string note = TR("Every move, evaluation and name it quotes comes from the review.");
            if (refused.empty()) {
                note += " " + std::string(TR("The checker refused nothing."));
            } else {
                if (second > 0)
                    note += " " + trn("{} passage was refused, then written again.",
                                      "{} passages were refused, then written again.", second, second);
                if (fallen > 0)
                    note += " " + trn("{} passage refused twice is the template's.",
                                      "{} passages refused twice are the templates'.", fallen, fallen);
                std::string listed;
                for (const auto& r : refused) listed += (listed.empty() ? "" : "; ") + r;
                note += " " + trf("What the checker refused: {}.", listed);
            }
            ImGui::TextDisabled("%s", note.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", trf("{} passages at the first try, {} at the second, {} the templates'", first,
                                            second, fallen)
                                            .c_str());
        }
        ImGui::PopTextWrapPos();
        if (ImGui::SmallButton(story_template_shown_ ? TR("The model's version") : TR("The template version")))
            story_template_shown_ = !story_template_shown_;
        ImGui::SameLine();
        ImGui::BeginDisabled(!storyWithoutModel().empty());
        if (ImGui::SmallButton(TR("Write it again"))) writeStory(true);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", TR("The model tells it otherwise, held to the same facts"));
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Copy it"))) {
            ImGui::SetClipboardText(storyText().c_str());
            boardToast(TR("The story is copied"));
        }
        endTintedCard();
    } else {
        // The templates': without the model, or when it could not answer, and said so.
        templates();
        std::string why = without;
        if (why.empty() && written && !model_story_error_.empty())
            why = trf("the model gave no answer ({})", model_story_error_);
        else if (why.empty() && written)
            why = TR("the checker refused each of the model's passages twice");
        else if (why.empty() && story_cancelled_of_ == review_story_of_)
            why = TR("the model's story was cancelled");
        if (why.empty())
            ImGui::TextDisabled("%s",
                                TR("Written from the review's facts: its verdicts, its evaluations, the positions."));
        else
            ImGui::TextDisabled("%s", trf("Written by the templates from the review's facts: {}.", why).c_str());
        if ((written || story_cancelled_of_ == review_story_of_) && without.empty()) {
            if (ImGui::SmallButton(TR("Write it again"))) writeStory(written);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton(TR("Copy it"))) {
            ImGui::SetClipboardText(storyText().c_str());
            boardToast(TR("The story is copied"));
        }
    }
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
}

}  // namespace fcg
