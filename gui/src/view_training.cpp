// The repertoires' training (#76, mockup M11, board 1; training.hpp): the training card of the Openings page, a
// session on the Board page whose column becomes the training's, the end of a session, the Home page's line.
//
// The keys of a question: a move played on the board, or typed (its letters open the move field, as everywhere on the
// study Board); Alt+H a hint (H begins the h-pawn's moves), Space "I don't know", Escape stops. After the answer,
// Enter goes on (or tries again after a wrong one), Ctrl+Z takes back a move added to the repertoire.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>  // SetKeyOwner

#include <study/game_tree.hpp>

#include "app.hpp"
#include "board_session.hpp"
#include "board_text.hpp"
#include "board_view.hpp"
#include "guess_page.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

using Clock = std::chrono::steady_clock;
using Ask   = TrainingRun::Phase;  // where a session is
using study::fsrs::Rating;

double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

chess::Board startBoard(const std::string& fen) {
    chess::Board board;
    board.setFen(fen.empty() ? std::string(chess::constants::STARTPOS) : fen);
    return board;
}

// A move of the line as the interface writes it: "6.Bg5", "6…e6".
std::string numbered(const chess::Board& before, const std::string& san) {
    return fmt::format("{}{}{}", before.fullMoveNumber(), before.sideToMove() == chess::Color::WHITE ? "." : "…",
                       localSan(san));
}

// The move of a SAN on the board, NO_MOVE when it is not legal there.
chess::Move moveOf(const chess::Board& board, const std::string& san) {
    try {
        return chess::uci::parseSan(board, san);
    } catch (...) {
        return chess::Move(chess::Move::NO_MOVE);
    }
}

// The board after the line, false when a move of it is not legal (the repertoire changed meanwhile).
bool play(chess::Board& board, const std::vector<std::string>& line, std::vector<chess::Move>* moves = nullptr) {
    for (const auto& san : line) {
        const auto m = moveOf(board, san);
        if (m == chess::Move::NO_MOVE) return false;
        if (moves) moves->push_back(m);
        board.makeMove<false>(m);
    }
    return true;
}

// "1.e4 c5 2.Nf3 d6": a line from the start.
std::string lineText(const std::string& start_fen, const std::vector<std::string>& line) {
    auto board = startBoard(start_fen);
    std::string out;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const auto m = moveOf(board, line[i]);
        if (m == chess::Move::NO_MOVE) break;
        out += (out.empty() ? "" : " ") +
               (i == 0 || board.sideToMove() == chess::Color::WHITE ? numbered(board, line[i]) : localSan(line[i]));
        board.makeMove<false>(m);
    }
    return out;
}

// The game the board shows for a card: the line from the repertoire's start to it.
ReplayGame gameOf(const std::string& start_fen, const std::vector<std::string>& line) {
    ReplayGame game;
    game.start_fen = start_fen.empty() ? std::string(chess::constants::STARTPOS) : start_fen;
    auto board     = startBoard(game.start_fen);
    for (const auto& san : line) {
        const auto m = moveOf(board, san);
        if (m == chess::Move::NO_MOVE) break;
        ReplayMove r;
        r.san   = chess::uci::moveToSan(board, m);
        r.move  = m;
        r.mover = board.sideToMove();
        game.moves.push_back(std::move(r));
        board.makeMove<false>(m);
    }
    return game;
}

ImU32 mix(ImU32 a, ImU32 b, float t) {
    const auto fa = ImGui::ColorConvertU32ToFloat4(a), fb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(fa.x + (fb.x - fa.x) * t, fa.y + (fb.y - fa.y) * t, fa.z + (fb.z - fa.z) * t, 1.0f));
}

// A card tinted: the training's colour, a right answer's, a wrong one's.
void beginTinted(const char* id, ImU32 tone) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BG, tone, 0.10f)));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BD, tone, 0.55f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(14)));
    ImGui::BeginChild(id, ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
}

void endTinted() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

// The button of the main gesture, filled with the accent.
bool primaryButton(const char* label, bool enabled = true) {
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImGui::ColorConvertU32ToFloat4(mix(ui::ACCENT, IM_COL32(255, 255, 255, 255), 0.15f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImGui::ColorConvertU32ToFloat4(mix(ui::ACCENT, IM_COL32(0, 0, 0, 255), 0.15f)));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    const bool clicked = ImGui::Button(label);
    ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
    return clicked;
}

// A count in large figures over what it counts.
void bigCount(std::size_t n, const char* what) {
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.9f);
    ImGui::TextUnformatted(ui::exactCount(n).c_str());
    ImGui::PopFont();
    ImGui::TextDisabled("%s", what);
    ImGui::EndGroup();
}

// When a card comes back: "in 6 days", "tomorrow".
std::string whenText(int64_t seconds) {
    const auto days = std::max<int64_t>(1, (seconds + study::fsrs::DAY / 2) / study::fsrs::DAY);
    if (days == 1) return TR("tomorrow");
    return trn("in {} day", "in {} days", static_cast<uint64_t>(days), days);
}

// A wrapped line of text, greyed or not.
void wrapped(const std::string& text, bool grey = false) {
    if (grey) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    if (grey) ImGui::PopStyleColor();
}

}  // namespace

// ---------------------------------------------------------------------------
// Kept and counted

TrainingSettings& App::trainingSettings() {
    if (!training_settings_) training_settings_ = readTrainingSettings(storage_.root());
    return *training_settings_;
}

void App::saveTrainingSettings() {
    if (options_.scripted) return;  // a run for screenshots leaves the profile as it was
    writeTrainingSettings(storage_.root(), trainingSettings());
}

std::optional<std::size_t> App::repertoireOf(const std::filesystem::path& file) {
    const auto& entries = repertoires().entries();
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (entries[i].file == file) return i;
    return std::nullopt;
}

const App::TrainingSummary& App::trainingSummary(std::size_t index) {
    const auto& e = repertoires().entries().at(index);
    auto& s       = training_summaries_[e.file];
    if (s.revision != e.revision) {
        s.revision = e.revision;
        s.cards    = study::trainingCards(e.lines, e.side);
        s.kept     = readTrainingFile(e.file);
        s.counted  = 0;
    }
    const auto now  = epochNow();
    const int limit = trainingSettings().new_per_day;
    if (s.counted / 60 != now / 60 || s.new_per_day != limit) {
        s.counts      = study::trainingCounts(s.cards, s.kept.memories, trainingDay(now),
                                              static_cast<std::size_t>(std::max(0, limit)));
        s.counted     = now;
        s.new_per_day = limit;
    }
    return s;
}

std::vector<study::TreeBase> App::trainingBases(std::string* key, std::vector<std::string>* names) {
    auto& s = shelf();
    std::vector<study::TreeBase> bases;
    for (std::size_t i = 0; i < s.entries().size(); ++i) {
        const auto& e = s.entries()[i];
        if (e.source || coverage_left_.count(e.folder)) continue;
        const auto source = s.treeSource(i);
        if (!source || !source->tree) continue;
        bases.push_back({source->snapshot, source->tree});
        if (key) *key += fmt::format("{}:{}|", e.folder.string(), source->snapshot->generation());
        if (names) names->push_back(source->name);
    }
    return bases;
}

const std::vector<study::TrainingCard>* App::plannedCards(std::size_t index) {
    const auto& e = repertoires().entries().at(index);
    std::string bases_key;
    auto bases    = trainingBases(&bases_key, nullptr);
    const auto of = fmt::format("{}|{}|{}", e.file.string(), e.revision, bases_key);
    if (training_planning_ &&
        training_planning_->cards.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        training_planned_[training_planning_->file] = {training_planning_->of, training_planning_->cards.get()};
        training_planning_.reset();
    }
    if (const auto it = training_planned_.find(e.file); it != training_planned_.end() && it->second.of == of)
        return &it->second.cards;
    // One at a time: the next once the one being made is done.
    if (!training_planning_) {
        TrainingPlanning job;
        job.file           = e.file;
        job.of             = of;
        job.cards          = std::async(std::launch::async, [lines = e.lines, side = e.side, bases = std::move(bases)] {
            const study::Repertoire repertoire(lines, side);
            return study::trainingCards(lines, side, study::trainingPlan(repertoire, study::treeMoves(bases)));
        });
        training_planning_ = std::move(job);
    }
    return nullptr;
}

std::string App::trainingLock() const {
    if (liveGameOn()) return TR("Locked while one of your games is played on Lichess.");
    if ((board_.match && board_.match_state.running) || engineGameRunning())
        return TR("Once the game played on the board is over.");
    return {};
}

bool App::trainingHidesAids() const {
    // The question of the training session that holds the Board, whatever its kind (#76, #80–#82).
    const auto* session = boardSession();
    return session && session->hidesAids();
}

std::string App::trainingListLine(std::size_t index) {
    const auto& c = trainingSummary(index).counts;
    if (c.cards == 0) return TR("nothing to train yet");
    if (c.due > 0) return trn("{} to review", "{} to review", c.due, ui::exactCount(c.due));
    return TR("nothing to review");
}

// ---------------------------------------------------------------------------
// The Openings page

void App::drawTrainingCard(std::size_t index) {
    const auto& e   = repertoires().entries()[index];
    const auto& s   = trainingSummary(index);
    const auto& c   = s.counts;
    auto& settings  = trainingSettings();
    const auto lock = trainingLock();
    beginTinted("##training_card", ui::ACCENT);
    {
        const ui::TopicRegion topic(guide::topics::Training);
        ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
        ImGui::TextUnformatted(TR("Train"));
        ImGui::PopFont();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("Spaced repetition: FSRS, the scheduler Anki uses. Each position where it is "
                                       "your move comes back further apart each time you know it."));
        if (c.cards == 0) {
            wrapped(TR("Nothing to train yet: each position of the repertoire where it is your move, and where it has "
                       "your move, becomes a position to train."),
                    true);
        } else {
            bigCount(c.due, TR("due today"));
            ImGui::SameLine(0, px(30));
            bigCount(c.today, TR("new today"));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "%s",
                    trf("{} new in all, {} a day at most", ui::exactCount(c.fresh), settings.new_per_day).c_str());
            ImGui::SameLine(0, px(30));
            bigCount(c.known, TR("known, to review later"));
            ImGui::Dummy(ImVec2(0, px(2)));

            const bool here    = training_ && training_->file == e.file;
            const auto session = c.due + c.today;
            bool start         = false;
            if (here) {
                if (primaryButton(TR("Back to the session"))) page_ = Page::Board;
            } else {
                const auto label = trn("Start · {} position", "Start · {} positions", session, ui::exactCount(session));
                start            = primaryButton(label.c_str(), session > 0 && lock.empty());
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", !lock.empty()  ? lock.c_str()
                                            : session == 0 ? TR("Nothing to review today.")
                                                           : TR("The positions due today, then today's new ones, "
                                                                "on the Board page."));
                if (session > 0) {
                    const double each = s.kept.answer_ms > 0 ? static_cast<double>(s.kept.answer_ms) : 10000.0;
                    const auto minutes =
                        std::max<int64_t>(1, std::lround(static_cast<double>(session) * each / 60000.0));
                    ImGui::SameLine(0, px(8));
                    ImGui::TextDisabled("%s", trf("Enter · about {} min", minutes).c_str());
                }
            }
            ImGui::SameLine(0, px(14));
            ImGui::BeginDisabled(c.fresh == 0 || !lock.empty());
            if (ImGui::Button(TR("Only the new ones"))) startTraining(index, TrainingRun::Kind::New);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                const auto batch =
                    c.today > 0
                        ? c.today
                        : std::min<std::size_t>(c.fresh, static_cast<std::size_t>(std::max(1, settings.new_per_day)));
                ImGui::SetTooltip("%s", c.fresh == 0 ? TR("Every position has been learnt.")
                                                     : trn("{} new position",
                                                           "{} new positions, the most met in your databases first",
                                                           batch, ui::exactCount(batch))
                                                           .c_str());
            }
            ImGui::SameLine(0, px(8));
            if (ImGui::SmallButton(trf("{} a day##training_limit", settings.new_per_day).c_str()) ||
                std::exchange(training_limit_menu_, false))
                ImGui::OpenPopup("##training_limit_menu");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("New positions a day at most"));
            if (ImGui::BeginPopup("##training_limit_menu")) {
                ImGui::TextDisabled("%s", TR("New positions a day at most"));
                for (const int n : {5, 10, 20, 30, 50, 100}) {
                    if (ImGui::MenuItem(ui::exactCount(static_cast<uint64_t>(n)).c_str(), nullptr,
                                        settings.new_per_day == n)) {
                        settings.new_per_day = n;
                        saveTrainingSettings();
                    }
                }
                ImGui::EndPopup();
            }
            // Enter starts, unless a field or a menu has the keys.
            const bool keys = !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
            if (keys && !here && session > 0 && lock.empty()) {
                const auto owner = ImGui::GetID("##training_start");
                ImGui::SetKeyOwner(ImGuiKey_Enter, owner);
                ImGui::SetKeyOwner(ImGuiKey_KeypadEnter, owner);
                if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
                    start = true;
            }

            // What comes next, and the order of the new ones.
            std::string next = trf("Then: {} tomorrow · {} within three days · {} within a week.",
                                   ui::exactCount(c.tomorrow), ui::exactCount(c.three_days), ui::exactCount(c.week));
            if (c.fresh > 0) {
                next += " ";
                next += TR("New positions come in the order your databases meet them most");
                std::vector<std::string> names;
                (void)trainingBases(nullptr, &names);
                const auto* planned = plannedCards(index);
                if (planned && !names.empty()) {
                    const auto order =
                        study::sessionOrder(*planned, s.kept.memories, trainingDay(epochNow()), 1, false);
                    if (!order.empty() && (*planned)[order.front()].games > 0 &&
                        !(*planned)[order.front()].line.empty()) {
                        const auto& card = (*planned)[order.front()];
                        auto board       = startBoard(e.lines.main.start_fen);
                        const std::vector<std::string> before(card.line.begin(), card.line.end() - 1);
                        if (play(board, before)) {
                            next += " (";
                            next +=
                                names.size() == 1
                                    ? trn("{}: {} game in {}", "{}: {} games in {}", card.games,
                                          numbered(board, card.line.back()), ui::exactCount(card.games), names[0])
                                    : trn("{}: {} game in your databases", "{}: {} games in your databases", card.games,
                                          numbered(board, card.line.back()), ui::exactCount(card.games));
                            next += ")";
                        }
                    }
                }
                next += ".";
            }
            wrapped(next, true);
            if (!lock.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::WARN));
                ImGui::TextWrapped("%s", lock.c_str());
                ImGui::PopStyleColor();
            }
            if (start) startTraining(index, TrainingRun::Kind::Due);
        }
    }
    endTinted();
}

// ---------------------------------------------------------------------------
// A session

void App::startTraining(std::size_t index, TrainingRun::Kind kind) {
    if (!trainingLock().empty() || index >= repertoires().entries().size()) return;
    closeBoardSession();  // the session that holds the Board left where it is, its answers kept (#80–#82)
    const auto& e  = repertoires().entries()[index];
    auto run       = std::make_unique<TrainingRun>();
    run->kind      = kind;
    run->file      = e.file;
    run->name      = e.name;
    run->side      = e.side;
    run->start_fen = e.lines.main.start_fen;
    run->kept      = trainingSummary(index).kept;
    run->settings  = trainingSettings();
    run->flip      = board_.flip;
    run->tab       = board_side_tab_;
    // The game on the board goes among the recent games (Ctrl+Tab), given back after the session.
    const auto& b = board_;
    run->aside    = !b.game.moves.empty() || !b.tree.variations.empty() || !b.title.empty();
    toStudyBoard();
    openOnBoard(gameOf(run->start_fen, {}), 0, false, trf("Training · {}", e.name), false);
    board_.flip = e.side == chess::Color::BLACK;
    board_back_.reset();
    // With the answer, the tree rather than the notation's editing: the moves of the session are not a game.
    if (board_side_tab_ == 0) board_side_tab_ = 2;
    run->started = Clock::now();
    training_    = std::move(run);
    pollTraining();  // the first question at once when the cards are ready
}

void App::pollTraining() {
    if (!training_ || training_->phase != Ask::Preparing) return;
    auto& t          = *training_;
    const auto index = repertoireOf(t.file);
    if (!index) return closeTraining(false);
    const auto* cards = plannedCards(*index);
    if (!cards) return;
    const auto day    = trainingDay(epochNow());
    const auto limit  = static_cast<std::size_t>(std::max(0, t.settings.new_per_day));
    const auto counts = study::trainingCounts(*cards, t.kept.memories, day, limit);
    std::size_t fresh = t.kind == TrainingRun::Kind::Review ? 0 : counts.today;
    // Only the new ones once today's are learnt: as many again, asked for.
    if (t.kind == TrainingRun::Kind::New && fresh == 0) fresh = std::min(counts.fresh, std::max<std::size_t>(1, limit));
    // Fair play (#48): a position on the line of one of the user's correspondence games on Lichess is not asked while
    // the game lasts: the repertoire would tell its next move.
    std::unordered_set<uint64_t> left_out;
    if (!lichess_playing_.empty()) {
        for (const auto& card : *cards)
            if (correspondencePly(gameOf(t.start_fen, card.line), false, INT32_MAX) >= 0) left_out.insert(card.key);
    }
    t.left_out = left_out.size();
    auto order = study::sessionOrder(*cards, t.kept.memories, day, fresh,
                                     t.kind == TrainingRun::Kind::Due || t.kind == TrainingRun::Kind::Review, left_out);
    t.session.emplace(*cards, std::move(order), t.kept.memories, trainingScheduler());
    t.started = Clock::now();
    if (!t.session->current()) return finishTraining(false);
    askTrainingCard();
}

void App::showTrainingCard(const study::TrainingCard& card) {
    auto& b    = board_;
    b.game     = gameOf(training_->start_fen, card.line);
    b.tree     = GameTree{b.game, {}};
    b.line     = -1;
    b.ply      = static_cast<int>(b.game.moves.size());
    b.chess960 = false;
    b.selected.reset();
    b.promotion.reset();
    b.preview.reset();
    b.drag.reset();
    b.drag_over.reset();
    b.move_typed.reset();
    b.move_error.clear();
    b.comment_edit.reset();
    b.undo.clear();
    b.redo.clear();
    b.changes     = {};
    b.start_notes = {};
    b.new_game    = true;
}

void App::askTrainingCard() {
    auto& t      = *training_;
    const auto i = t.session ? t.session->current() : std::nullopt;
    if (!i) return finishTraining(false);
    showTrainingCard(t.session->cards()[*i]);
    t.position = t.session->asked();
    t.again    = t.session->askedAgain();
    t.phase    = Ask::Asking;
    t.hint     = false;
    t.retry    = false;
    t.shown    = false;
    t.hovered  = false;
    t.played.clear();
    t.added.reset();
    t.asked = Clock::now();
}

void App::trainingAnswer(chess::Move move) {
    auto& t = *training_;
    if (t.phase != Ask::Asking || !t.session || !t.session->current() || liveGameOn()) return;
    auto& b          = board_;
    const auto& card = t.session->cards()[*t.session->current()];
    if (b.ply != static_cast<int>(card.line.size())) return;  // the question's position only
    const auto board = boardAt(b.game, b.chess960, b.ply);
    t.played         = chess::uci::moveToSan(board, move);
    t.played_move    = move;
    t.shown          = false;
    t.answered       = Clock::now();
    const bool right = t.session->right(t.played);
    if (!t.retry) trainingGrade(right);
    // The move on the board, after the opponent's.
    b.ply  = playOnGame(b.game, b.chess960, b.ply, move);
    b.tree = GameTree{b.game, {}};
    b.selected.reset();
    b.move_typed.reset();
    t.phase = right ? Ask::Right : Ask::Wrong;
}

void App::trainingGrade(bool right) {
    auto& t              = *training_;
    const auto i         = *t.session->current();
    const auto key       = t.session->cards()[i].key;
    t.seconds            = since(t.asked);
    t.rating             = study::gradeOf(right, t.hint, t.seconds);
    const auto now       = epochNow();
    t.graded             = t.session->grade(t.rating, t.played, now);
    t.graded_at          = now;
    t.kept.memories[key] = t.graded.after;
    // How long an answer takes, for the time a session is foreseen to take: a long pause counts as a minute.
    const auto ms    = static_cast<int64_t>(std::min(t.seconds, 60.0) * 1000);
    t.kept.answer_ms = t.kept.answer_ms > 0 ? (t.kept.answer_ms * 4 + ms) / 5 : ms;
    if (!options_.scripted) {
        if (!writeTrainingFile(t.file, t.kept)) {
            board_.message       = trf("The answer could not be kept in {}: is the profile's folder writable?",
                                       trainingFileOf(t.file).string());
            board_.message_error = true;
        }
        appendReview(t.file, key, now, t.rating, ms, t.played);
    }
    if (const auto it = training_summaries_.find(t.file); it != training_summaries_.end()) {
        it->second.kept    = t.kept;
        it->second.counted = 0;
    }
}

void App::trainingDontKnow() {
    auto& t = *training_;
    if (t.phase != Ask::Asking || liveGameOn()) return;
    t.played.clear();
    t.shown    = true;
    t.answered = Clock::now();
    if (!t.retry) trainingGrade(false);
    board_.move_typed.reset();
    t.phase = Ask::Wrong;
}

void App::trainingRetry() {
    auto& t = *training_;
    if (t.phase != Ask::Wrong || !t.session || !t.session->current()) return;
    auto& b      = board_;
    const auto q = t.session->cards()[*t.session->current()].line.size();
    if (b.game.moves.size() > q) {
        b.game.moves.resize(q);
        b.tree = GameTree{b.game, {}};
    }
    b.line = -1;
    b.ply  = static_cast<int>(q);
    b.selected.reset();
    t.phase = Ask::Asking;
    t.retry = true;
    t.shown = false;
    t.hint  = false;
    t.played.clear();
    t.added.reset();
    t.asked = Clock::now();
}

void App::trainingNext() {
    auto& t = *training_;
    if (!t.session) return;
    t.session->next();
    if (!t.session->current()) return finishTraining(false);
    askTrainingCard();
}

void App::trainingAdd() {
    auto& t = *training_;
    if (t.phase != Ask::Wrong || t.shown || t.played.empty() || t.added || !t.session || !t.session->current()) return;
    const auto index = repertoireOf(t.file);
    if (!index || (!t.start_fen.empty() && t.start_fen != chess::constants::STARTPOS)) return;
    const auto i     = *t.session->current();
    const auto& card = t.session->cards()[i];
    std::vector<chess::Move> path;
    auto board = startBoard(t.start_fen);
    if (!play(board, card.line, &path)) return;
    auto& r = repertoires();
    if (r.has(*index, path, t.played_move)) return;
    TrainingRun::Added added{*index, r.entries()[*index].lines, i, t.played};
    if (!r.toggle(*index, path, t.played_move)) return;
    t.session->addMove(i, t.played);
    if (const auto it = t.session->memories().find(card.key); it != t.session->memories().end())
        t.kept.memories[card.key] = it->second;
    if (!options_.scripted) (void)writeTrainingFile(t.file, t.kept);
    t.added = std::move(added);
}

void App::trainingTakeBack() {
    auto& t = *training_;
    if (!t.added || !t.session) return;
    repertoires().restore(t.added->repertoire, t.added->lines);
    t.session->takeBack(t.added->card, t.added->san);
    const auto key = t.session->cards()[t.added->card].key;
    if (const auto it = t.session->memories().find(key); it != t.session->memories().end())
        t.kept.memories[key] = it->second;
    if (!options_.scripted) (void)writeTrainingFile(t.file, t.kept);
    t.added.reset();
}

void App::finishTraining(bool stopped) {
    auto& t        = *training_;
    t.phase        = Ask::End;
    t.stopped      = stopped;
    t.took         = since(t.started);
    t.missed_shown = -1;
    board_.selected.reset();
    board_.move_typed.reset();
}

void App::reviewMissed() {
    auto& t = *training_;
    if (!t.session) return;
    std::vector<std::size_t> order;
    for (const auto& o : t.session->outcomes())
        if (o.rating == Rating::Again) order.push_back(o.card);
    if (order.empty()) return;
    std::sort(order.begin(), order.end());  // the walk's order
    auto cards = t.session->cards();
    t.session.emplace(std::move(cards), std::move(order), t.kept.memories, trainingScheduler());
    t.kind    = TrainingRun::Kind::Missed;
    t.stopped = false;
    t.started = Clock::now();
    askTrainingCard();
}

void App::closeTraining(bool to_openings) {
    if (!training_) return;
    // Left during the day's review (#80): back to the Training page, what the review had left forgotten.
    const bool to_training = to_openings && review_running_;
    if (to_openings) {
        review_running_ = false;
        review_chain_.clear();
    }
    const bool aside = training_->aside, flip = training_->flip;
    board_side_tab_ = training_->tab;
    training_.reset();
    forget_board_game_ = true;  // the session's position is no game to remember
    if (aside && !recent_games_.empty())
        restoreRecentGame(0);
    else
        newBoardGame();
    forget_board_game_ = false;
    board_.flip        = flip;
    board_back_.reset();
    page_ = to_training ? Page::Training : to_openings ? Page::Openings : Page::Board;
}

void App::dropTraining() {
    board_.flip     = training_->flip;
    board_side_tab_ = training_->tab;
    training_.reset();
    forget_board_game_ = true;  // the game that comes takes the board; the one put aside stays among the recent ones
}

// ---------------------------------------------------------------------------
// The Board page during a session

void App::trainingKeys() {
    auto& t        = *training_;
    const auto& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    // Enter, Space, Escape and the arrows are the training's: ImGui's keyboard navigation leaves them alone (the Tree
    // tab still reads its arrows).
    const auto owner = ImGui::GetID("##training_keys");
    for (const auto key : {ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Space, ImGuiKey_Escape, ImGuiKey_LeftArrow,
                           ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Home, ImGuiKey_End})
        ImGui::SetKeyOwner(key, owner);
    const bool plain = !io.KeyCtrl && !io.KeyAlt && !io.KeySuper;
    const bool enter =
        plain && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    switch (t.phase) {
        case Ask::Preparing:
            if (escape) closeTraining(true);
            break;
        case Ask::Asking:
            if (escape) {
                finishTraining(true);
            } else if (!liveGameOn()) {
                if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_H)) t.hint = true;
                if (plain && ImGui::IsKeyPressed(ImGuiKey_Space, false))
                    trainingDontKnow();
                else
                    boardMoveKeys();
            }
            break;
        case Ask::Right:
        case Ask::Wrong:
            if (escape) {
                finishTraining(true);
            } else if (enter) {
                if (t.phase == Ask::Right || t.added)
                    trainingNext();
                else
                    trainingRetry();
            } else if (t.phase == Ask::Wrong && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
                trainingTakeBack();
            } else if (boardKey()) {
                setBoardAnalysis(!board_.analysing);  // Alt+A: the engine, now that the answer is given
            }
            break;
        case Ask::End: {
            const bool missed = t.session && std::any_of(t.session->outcomes().begin(), t.session->outcomes().end(),
                                                         [](const auto& o) { return o.rating == Rating::Again; });
            if (review_running_) {  // the day's review (#80): Enter goes on, Escape back to the Training page
                if (escape)
                    closeTraining(true);
                else if (enter && !nextReview())
                    closeTraining(true);
            } else if (escape) {
                closeTraining(true);
            } else if (enter) {
                missed ? reviewMissed() : closeTraining(true);
            }
            break;
        }
    }
}

void App::drawTrainingPage() {
    ui::pageTitle(TR("Board"), guideSection());
    // The letters typed reach the move field whatever the keyboard's layout (see drawBoardPage).
    if (!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
        !SDL_TextInputActive(window_))
        SDL_StartTextInput(window_);
    trainingKeys();
    if (!training_) return;  // left by a key
    auto& t = *training_;
    // A right answer goes on by itself after a moment, unless the mouse is on it.
    if (t.phase == Ask::Right && t.settings.advance_ms > 0 && !t.hovered &&
        since(t.answered) * 1000 >= t.settings.advance_ms) {
        trainingNext();
        if (!training_) return;
    }
    if (!board_.message.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, board_.message_error ? ui::BAD : ui::MUTED);
        ImGui::TextWrapped("%s", board_.message.c_str());
        ImGui::PopStyleColor();
    }
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float side   = std::clamp(avail.x * 0.42f, px(340), px(620));
    const float under  = ImGui::GetTextLineHeightWithSpacing() * 4;
    const float size   = std::floor(
        std::max(px(200), std::min(avail.x - side - ImGui::GetStyle().ItemSpacing.x - px(20), avail.y - under)));

    ImGui::BeginGroup();
    // Over the board: the repertoire, and where the session is.
    {
        const auto start = ImGui::GetCursorPosX();
        ImGui::TextDisabled("%s · %s", t.name.c_str(), t.side == chess::Color::WHITE ? TR("White") : TR("Black"));
        std::string where;
        if (t.session && t.phase != Ask::Preparing && t.phase != Ask::End) {
            where = trf("position {} of {}", t.position, t.session->total());
            if (t.again) where += " · " + std::string(TR("asked again"));
        }
        if (!where.empty()) {
            ImGui::SameLine(
                std::max(ImGui::GetCursorPosX() + px(10), start + size - ImGui::CalcTextSize(where.c_str()).x));
            ImGui::TextDisabled("%s", where.c_str());
        }
    }
    drawTrainingBoard(size);
    // Under it, the line that leads to the position.
    if (t.session && t.session->current() && t.phase != Ask::End) {
        auto line = t.session->cards()[*t.session->current()].line;
        if (t.phase != Ask::Asking && !t.played.empty()) line.push_back(t.played);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size);
        ImGui::TextDisabled("%s", lineText(t.start_fen, line).c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, px(18));
    ImGui::BeginChild("##training_side", ImVec2(0, 0));
    drawTrainingColumn();
    ImGui::EndChild();
}

void App::drawTrainingBoard(float size) {
    auto& b          = board_;
    auto& t          = *training_;
    const auto board = boardAt(b.game, b.chess960, b.ply);
    BoardDecor decor;
    decor.flipped    = b.flip;
    decor.eval_white = -1.0f;  // no bar: no engine's word on the position
    if (b.ply > 0) decor.last_move = b.game.moves[static_cast<std::size_t>(b.ply - 1)].move;
    const study::TrainingCard* card = nullptr;
    if (t.session && t.session->current() && t.phase != Ask::End) card = &t.session->cards()[*t.session->current()];
    if (t.phase == Ask::End && t.session && t.missed_shown >= 0 &&
        t.missed_shown < static_cast<int>(t.session->cards().size()))
        card = &t.session->cards()[static_cast<std::size_t>(t.missed_shown)];
    const auto green  = markColour('G', true);
    const auto orange = IM_COL32(236, 140, 52, 150);
    // The repertoire's moves at the question's position: their squares in green, an arrow each.
    const auto repertoireMoves = [&](bool arrows) {
        if (!card) return;
        auto at = startBoard(t.start_fen);
        if (!play(at, card->line)) return;
        for (std::size_t i = 0; i < card->moves.size(); ++i) {
            const auto m = moveOf(at, card->moves[i]);
            if (m == chess::Move::NO_MOVE) continue;
            if (i == 0) {
                decor.tints.push_back({m.from(), green});
                decor.tints.push_back({shownTarget(m), green});
            }
            if (arrows) decor.arrows.push_back({m, markColour('G', false)});
        }
    };
    const bool question = card && b.ply == static_cast<int>(card->line.size());
    switch (t.phase) {
        case Ask::Asking:
            // The hint: the piece to play, in blue.
            if (t.hint && card && !card->moves.empty()) {
                auto at = startBoard(t.start_fen);
                if (play(at, card->line))
                    if (const auto m = moveOf(at, card->moves.front()); m != chess::Move::NO_MOVE)
                        decor.tints.push_back({m.from(), markColour('B', true)});
            }
            break;
        case Ask::Right:
            if (decor.last_move) {
                decor.tints.push_back({decor.last_move->from(), green});
                decor.tints.push_back({shownTarget(*decor.last_move), green});
            }
            break;
        case Ask::Wrong:
            if (t.added) {  // the move played is the repertoire's now
                if (decor.last_move && !question) {
                    decor.tints.push_back({decor.last_move->from(), green});
                    decor.tints.push_back({shownTarget(*decor.last_move), green});
                }
                break;
            }
            if (!t.shown && !question && decor.last_move) {  // the move played, in orange
                decor.tints.push_back({decor.last_move->from(), orange});
                decor.tints.push_back({shownTarget(*decor.last_move), orange});
            }
            if (!t.shown && question) decor.arrows.push_back({t.played_move, orange});
            repertoireMoves(true);
            break;
        case Ask::End: {
            if (!card) break;
            repertoireMoves(true);
            for (const auto& o : t.session->outcomes()) {
                if (static_cast<int>(o.card) != t.missed_shown || o.played.empty()) continue;
                const auto m = moveOf(board, o.played);
                if (m != chess::Move::NO_MOVE) decor.arrows.insert(decor.arrows.begin(), {m, orange});
            }
            break;
        }
        case Ask::Preparing:
            break;
    }
    const bool moves = t.phase == Ask::Asking && question && !liveGameOn();
    decor.selected   = moves ? b.selected : std::nullopt;
    if (decor.selected)
        for (const auto& m : movesFrom(board, *decor.selected)) decor.targets.push_back(shownTarget(m));
    const bool dragging = moves && b.drag && b.drag->moved && b.selected == b.drag->from;
    if (dragging) {
        decor.lifted = b.drag->from;
        if (b.drag_over && std::find(decor.targets.begin(), decor.targets.end(), *b.drag_over) != decor.targets.end())
            decor.drop = b.drag_over;
    }
    const ImVec2 top = ImGui::GetCursorScreenPos();
    if (appearance::current().board3d.on && board3d_.available()) {
        board3d_.setMarks(Board3d::Marks::Off);
        const auto input = board3d_.draw(board, size, decor);
        if (moves) boardPointer(board, input.clicked, input.hovered, input.right_click);
    } else {
        if (!appearance::current().board3d.on) board3d_.release();
        drawBoard(atlas_, board, size, decor);
        const auto hover = boardSquareAt(ImGui::GetMousePos(), top, size, decor);
        if (moves) {
            const bool left  = ImGui::IsItemClicked(ImGuiMouseButton_Left),
                       right = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            boardPointer(board, left ? hover : std::nullopt, hover, right);
            if (dragging && b.drag) drawDraggedPiece(atlas_, board.at(b.drag->from), size);
        }
    }
    drawPromotionPopup(board);
}

void App::drawTrainingColumn() {
    auto& t = *training_;
    const ui::TopicRegion topic(guide::topics::Training);
    if (liveGameOn()) {  // fair play (#48): a live game on Lichess stops the training where it is
        drawFairPlayBanner();
        if (ImGui::Button(TR("Stop · Esc"))) finishTraining(true);
        return;
    }
    if (t.phase == Ask::Preparing) {
        ImGui::TextDisabled("%s", TR("Preparing the session: what your databases play…"));
        if (ImGui::Button(TR("Cancel"))) closeTraining(true);
        return;
    }
    if (t.phase == Ask::End) return drawTrainingEnd();
    if (!t.session || !t.session->current()) return;
    const auto& card = t.session->cards()[*t.session->current()];
    const auto board = startBoard(t.start_fen);
    auto before      = board;
    (void)play(before, card.line);
    const auto yours = [&](const std::string& san) { return numbered(before, san); };
    std::string moves;  // the repertoire's moves here, "6…e6" or "6…e6 or 6…e5"
    for (std::size_t i = 0; i < card.moves.size(); ++i)
        moves += (i == 0                       ? ""
                  : i + 1 == card.moves.size() ? std::string(" ") + TR("or") + " "
                                               : std::string(", ")) +
                 yours(card.moves[i]);

    // The session's progress.
    const float done = t.session->total() ? static_cast<float>(t.session->done()) / t.session->total() : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    ImGui::ProgressBar(done, ImVec2(-1, px(6)), "");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, px(4)));

    const auto comment = [&](const char* intro) {
        if (card.comment.empty()) return;
        wrapped(trf("{} “{}”", intro, card.comment));
    };
    switch (t.phase) {
        case Ask::Asking: {
            beginTinted("##training_question", ui::CARD_BD);
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.2f);
            ImGui::TextUnformatted(t.retry ? TR("Again: the move of your repertoire")
                                           : TR("Your move: the move of your repertoire"));
            ImGui::PopFont();
            wrapped(TR("Play it on the board, or type it and press Enter. The tree, the engine and your notes are "
                       "hidden during the question; they come back with the answer."),
                    true);
            drawMoveField();
            if (ImGui::Button(TR("A hint · Alt+H"))) t.hint = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TR("H alone begins a move of the h-pawn: the hint is under Alt."));
            ImGui::SameLine();
            if (ImGui::Button(TR("I don't know · Space"))) trainingDontKnow();
            ImGui::SameLine();
            if (ImGui::Button(TR("Stop · Esc"))) finishTraining(true);
            if (t.phase != Ask::Asking) {  // answered by a button
                endTinted();
                return;
            }
            wrapped(t.hint ? TR("The piece to play is on the square in blue; the answer counts as hard.")
                           : TR("The hint shows the piece to play; the answer then counts as hard."),
                    true);
            endTinted();
            break;
        }
        case Ask::Right: {
            beginTinted("##training_right", ui::GOOD);
            ui::inlineIcon(ui::Icon::Check, ui::GOOD);
            ImGui::SameLine();
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.2f);
            ImGui::TextUnformatted((card.moves.size() > 1 ? trf("{}: one of your moves", yours(t.played))
                                                          : trf("{}: your move", yours(t.played)))
                                       .c_str());
            ImGui::PopFont();
            comment(TR("Your note in the repertoire:"));
            std::string when;
            if (t.retry) {
                when = TR("Right this time: asked again in this session, then tomorrow.");
            } else {
                const auto next = t.graded.again ? std::string(TR("again in this session"))
                                                 : whenText(t.graded.after.card.due - t.graded_at);
                const auto took = ui::formatDuration(t.seconds);
                when = t.rating == Rating::Good ? trf("Right at the first try, in {}: next review {}.", took, next)
                       : t.hint                 ? trf("Right after a hint, in {}: next review {}.", took, next)
                                                : trf("Right in {}, more than 20 s: next review {}.", took, next);
            }
            wrapped(when, true);
            if (primaryButton(TR("Next"))) trainingNext();
            if (t.phase == Ask::Right) {
                ImGui::SameLine(0, px(8));
                const auto& s = t.settings;
                ImGui::TextDisabled("%s", s.advance_ms > 0
                                              ? trf("Enter, or on its own after {} s", s.advance_ms / 1000.0).c_str()
                                              : TR("Enter"));
                ImGui::SameLine(0, px(4));
                if (ImGui::SmallButton("…##training_advance")) ImGui::OpenPopup("##training_advance_menu");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("After a right answer, the next position"));
                if (ImGui::BeginPopup("##training_advance_menu")) {
                    ImGui::TextDisabled("%s", TR("After a right answer, the next position"));
                    for (const int ms : {1000, 2000, 3000, 5000, 0}) {
                        const auto label =
                            ms > 0 ? trf("On its own after {} s", ms / 1000) : std::string(TR("On Enter only"));
                        if (ImGui::MenuItem(label.c_str(), nullptr, s.advance_ms == ms)) {
                            t.settings.advance_ms         = ms;
                            trainingSettings().advance_ms = ms;
                            saveTrainingSettings();
                        }
                    }
                    ImGui::EndPopup();
                }
                // Its countdown, stopped while the mouse is on the answer.
                if (s.advance_ms > 0 && !t.hovered)
                    ImGui::ProgressBar(static_cast<float>(std::min(1.0, since(t.answered) * 1000 / s.advance_ms)),
                                       ImVec2(-1, px(3)), "");
            }
            endTinted();
            t.hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
                                             ImGuiHoveredFlags_AllowWhenBlockedByPopup);
            break;
        }
        case Ask::Wrong: {
            beginTinted("##training_wrong", ui::WARN);
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.2f);
            if (t.added)
                ImGui::TextUnformatted(trf("{}: added to your repertoire", yours(t.played)).c_str());
            else if (t.shown)
                ImGui::TextUnformatted(trf("Your repertoire's move: {}", moves).c_str());
            else
                ImGui::TextUnformatted(trf("{} is not in your repertoire", yours(t.played)).c_str());
            ImGui::PopFont();
            if (t.added) {
                wrapped(TR("It is a second answer accepted here; Ctrl+Z takes it back."), true);
            } else {
                wrapped(t.shown ? trf("Its squares in green.") : trf("Yours: {}, its squares in green.", moves));
                comment(TR("Your note:"));
            }
            if (!t.retry) {
                wrapped(t.graded.again ? TR("To review in this session, then tomorrow.")
                                       : trf("Next review {}.", whenText(t.graded.after.card.due - t.graded_at)),
                        true);
            }
            if (t.added) {
                if (primaryButton(TR("Next · Enter"))) trainingNext();
            } else {
                if (primaryButton(TR("Retry · Enter"))) trainingRetry();
            }
            if (t.phase == Ask::Wrong) {
                ImGui::SameLine();
                if (ImGui::Button(TR("See the tree here"))) {
                    if (board_.ply > static_cast<int>(card.line.size()))
                        board_.ply = static_cast<int>(card.line.size());
                    board_side_tab_ = 2;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", TR("The position of the question, the Tree tab beside it."));
                if (!t.shown && !t.added) {
                    ImGui::SameLine();
                    const bool standard = t.start_fen.empty() || t.start_fen == chess::constants::STARTPOS;
                    ImGui::BeginDisabled(!standard);
                    if (ImGui::Button(trf("Add {} to the repertoire", yours(t.played)).c_str())) trainingAdd();
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("%s", TR("Added, it becomes a second answer accepted here; Ctrl+Z takes it "
                                                   "back."));
                } else if (t.added) {
                    ImGui::SameLine();
                    if (ImGui::Button(TR("Take it back · Ctrl+Z"))) trainingTakeBack();
                }
            }
            endTinted();
            break;
        }
        default:
            break;
    }
    if (!training_ || t.phase == Ask::End) return;
    ImGui::Dummy(ImVec2(0, px(4)));
    wrapped(t.side == chess::Color::BLACK
                ? TR("White's moves are played by RookStudio, along the repertoire; where it has several, the most "
                     "frequent in your databases comes first.")
                : TR("Black's moves are played by RookStudio, along the repertoire; where it has several, the most "
                     "frequent in your databases comes first."),
            true);
    if (t.left_out > 0)
        wrapped(
            trn("{} position left out while a correspondence game on Lichess goes through it.",
                "{} positions left out while correspondence games on Lichess go through them.", t.left_out, t.left_out),
            true);
    // With the answer, the tree, the engine and the notes come back.
    if (t.phase == Ask::Right || t.phase == Ask::Wrong) {
        ImGui::Separator();
        drawChessTabs();
    }
}

void App::drawTrainingEnd() {
    auto& t = *training_;
    if (!t.session) return;
    const auto& outcomes = t.session->outcomes();
    std::size_t first    = 0;
    std::vector<const study::TrainingSession::Outcome*> missed;
    for (const auto& o : outcomes) {
        if (o.rating == Rating::Again)
            missed.push_back(&o);
        else
            ++first;
    }
    const auto day       = trainingDay(epochNow());
    std::size_t tomorrow = 0, tomorrow_all = 0;
    for (const auto& o : outcomes) {
        const auto it = t.session->memories().find(t.session->cards()[o.card].key);
        if (it != t.session->memories().end() && it->second.card.due <= day.end + study::fsrs::DAY) ++tomorrow;
    }
    for (const auto& card : t.session->cards())
        if (const auto* m = study::memoryOf(t.session->memories(), card);
            m && m->card.due <= day.end + study::fsrs::DAY)
            ++tomorrow_all;

    ui::inlineIcon(ui::Icon::Check, ui::GOOD);
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.35f);
    const auto took = ui::formatDuration(t.took);
    if (outcomes.empty())
        ImGui::TextUnformatted(t.stopped ? TR("Stopped before the first answer") : TR("Nothing to review now"));
    else
        ImGui::TextUnformatted(trn("{} position reviewed in {}", "{} positions reviewed in {}", outcomes.size(),
                                   ui::exactCount(outcomes.size()), took)
                                   .c_str());
    ImGui::PopFont();
    if (t.stopped && t.session->total() > outcomes.size())
        wrapped(
            trn("Stopped: {} position waits for the next session.", "Stopped: {} positions wait for the next session.",
                t.session->total() - outcomes.size(), ui::exactCount(t.session->total() - outcomes.size())),
            true);
    if (!outcomes.empty()) {
        ImGui::Dummy(ImVec2(0, px(4)));
        bigCount(first, TR("right at the first try"));
        ImGui::SameLine(0, px(30));
        bigCount(tomorrow, TR("to review tomorrow"));
        ImGui::SameLine(0, px(30));
        bigCount(tomorrow_all, TR("for tomorrow in all"));
    }
    if (!missed.empty()) {
        ImGui::Dummy(ImVec2(0, px(6)));
        ImGui::TextUnformatted(TR("The moves missed"));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("· a click shows the position"));
        ui::beginCard("##training_missed");
        for (const auto* o : missed) {
            const auto& card = t.session->cards()[o->card];
            auto before      = startBoard(t.start_fen);
            const std::vector<std::string> to(card.line.begin(),
                                              card.line.empty() ? card.line.end() : card.line.end() - 1);
            auto at = before;
            (void)play(at, to);
            std::string against;
            if (!card.line.empty()) against = numbered(at, card.line.back());
            auto pos = before;
            (void)play(pos, card.line);
            const auto right = card.moves.empty() ? std::string() : numbered(pos, card.moves.front());
            const auto label = against.empty() ? right : trf("{} against {}", right, against);
            const auto said =
                o->played.empty() ? std::string(TR("I don't know")) : trf("you: {}", numbered(pos, o->played));
            ImGui::PushID(static_cast<int>(o->card));
            if (ImGui::Selectable(label.c_str(), t.missed_shown == static_cast<int>(o->card))) {
                t.missed_shown = static_cast<int>(o->card);
                showTrainingCard(card);
            }
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(10),
                                     ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(said.c_str()).x));
            ImGui::TextDisabled("%s", said.c_str());
            ImGui::PopID();
        }
        ui::endCard();
    }
    ImGui::Dummy(ImVec2(0, px(6)));
    if (review_running_) {  // the day's review (#80): the next kind, or back to the Training page
        drawReviewNext(false);
        if (!training_) return;
        if (!missed.empty()) {
            ImGui::SameLine();
            if (ImGui::Button(trn("Review the {} missed position now", "Review the {} missed positions now",
                                  missed.size(), missed.size())
                                  .c_str()))
                return reviewMissed();
        }
    } else if (!missed.empty()) {
        if (primaryButton(trn("Review the {} missed position now", "Review the {} missed positions now", missed.size(),
                              missed.size())
                              .c_str()))
            return reviewMissed();
        ImGui::SameLine();
        if (ImGui::Button(TR("Back to the Openings"))) return closeTraining(true);
    } else if (primaryButton(TR("Back to the Openings"))) {
        return closeTraining(true);
    }
    if (!review_running_) {
        ImGui::SameLine(0, px(8));
        ImGui::TextDisabled("%s", missed.empty() ? TR("Enter") : TR("Enter: review them · Esc: back"));
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    wrapped(TR("A session stopped midway keeps what was answered; the next one goes on with what is still due."), true);
}

// ---------------------------------------------------------------------------
// The Home page

bool App::trainingDue() {
    if (boardSession()) return true;  // a training session holds the Board (#76, #80–#82)
    return reviewDay().due() > 0;  // the day's review, every kind (#80)
}

void App::drawTrainingHome() {
    if (training_) {
        const auto& t    = *training_;
        const auto label = t.session && t.phase != Ask::End && t.phase != Ask::Preparing
                               ? trf("Training “{}”: position {} of {}", t.name, t.position, t.session->total())
                               : trf("Training “{}”", t.name);
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Back to the session"))) page_ = Page::Board;
    }
    // A session of exercises, a game guessed (#81).
    if (mistake_) {
        const auto& m    = *mistake_;
        const auto label = m.phase != MistakeRun::Phase::End
                               ? trf("Your mistakes: {} of {}", std::min(m.done + 1, m.total), m.total)
                               : std::string(TR("Your mistakes"));
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Back to the session"))) page_ = Page::Board;
    }
    guessing_->drawHomeLine();  // its module's (#107)
    // An endgame against the tables, a vision exercise (#82).
    if (endgame_) {
        const auto& r = *endgame_;
        const auto label =
            r.phase != EndgameRun::Phase::End && r.tasks.size() > 1
                ? trf("Endgames “{}”: {} of {}", r.title, std::min(r.current + 1, r.tasks.size()), r.tasks.size())
                : trf("Endgame “{}”", r.title);
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Back to the endgame"))) page_ = Page::Board;
    }
    if (vision_) {
        ImGui::TextUnformatted(TR("Board vision"));
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Back to the exercise"))) page_ = Page::Board;
    }
    // The day's review (#80): one count for every kind, a click reviewing them all.
    drawReviewHome();
}

}  // namespace fcg
