// The repertoires' training (#76, mockup M11, board 1), a module of its own out of App (#107; training.hpp is its
// data): the training card of the Openings page, a session on the Board page whose column becomes the training's, the
// end of a session, the day's review of the positions due, the Home page's line.
//
// The keys of a question: a move played on the board, or typed (its letters open the move field, as everywhere on the
// study Board); Alt+H a hint (H begins the h-pawn's moves), Space "I don't know", Escape stops. After the answer,
// Enter goes on (or tries again after a wrong one), Ctrl+Z takes back a move added to the repertoire.

#include "repertoire_training_page.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>  // SetKeyOwner

#include <study/game_tree.hpp>

#include "board_page.hpp"
#include "board_text.hpp"
#include "board_view.hpp"
#include "databases.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "keyboard.hpp"
#include "repertoires.hpp"
#include "ui.hpp"
#include "widgets.hpp"

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

using ui::endTinted;
using ui::mix;
using ui::primaryButton;
using ui::wrapped;

// A card tinted: the training's colour, a right answer's, a wrong one's; a little more than the other pages'.
constexpr ui::Tint TINT{.fill = 0.10f};
void beginTinted(const char* id, ImU32 tone) { ui::beginTinted(id, tone, TINT); }

// A move of the line as the interface writes it: "6.Bg5", "6…e6".
std::string numbered(const chess::Board& before, const std::string& san) { return ui::numbered(before, localSan(san)); }

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

}  // namespace

RepertoireTrainingPage::RepertoireTrainingPage(RepertoireTrainingContext& app) : app_(app) {}

RepertoireTrainingPage::~RepertoireTrainingPage() = default;

bool RepertoireTrainingPage::hidesAids() const {
    // A question, or its cards being prepared.
    return training_ && (training_->phase == Ask::Asking || training_->phase == Ask::Preparing);
}

const char* RepertoireTrainingPage::guideTopic() const { return guide::topics::Training; }

// ---------------------------------------------------------------------------
// Kept and counted

std::optional<std::size_t> RepertoireTrainingPage::repertoireOf(const std::filesystem::path& file) {
    const auto& entries = app_.repertoires().entries();
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (entries[i].file == file) return i;
    return std::nullopt;
}

const RepertoireTrainingPage::Summary& RepertoireTrainingPage::summary(std::size_t index) {
    const auto& e     = app_.repertoires().entries().at(index);
    auto [it, unread] = summaries_.try_emplace(e.file);
    auto& s           = it->second;
    // Its memories read once, then kept up to date by the sessions (keep()): what could not be read in its file is said
    // once (#136), not again at each change of the repertoire.
    if (unread) s.kept = readTrainingFile(e.file);
    if (s.revision != e.revision) {
        s.revision = e.revision;
        s.cards    = study::trainingCards(e.lines, e.side);
        s.counted  = 0;
    }
    const auto now  = app_.now();
    const int limit = app_.trainingSettings().new_per_day;
    if (s.counted / 60 != now / 60 || s.new_per_day != limit) {
        s.counts      = study::trainingCounts(s.cards, s.kept.memories, trainingDay(now),
                                              static_cast<std::size_t>(std::max(0, limit)));
        s.counted     = now;
        s.new_per_day = limit;
    }
    return s;
}

std::vector<study::TreeBase> RepertoireTrainingPage::trainingBases(std::string* key, std::vector<std::string>* names) {
    auto& s = app_.shelf();
    std::vector<study::TreeBase> bases;
    for (std::size_t i = 0; i < s.entries().size(); ++i) {
        const auto& e = s.entries()[i];
        if (e.source || !app_.measuredOn(e.folder)) continue;
        const auto source = s.treeSource(i);
        if (!source || !source->tree) continue;
        bases.push_back({source->snapshot, source->tree});
        if (key) *key += fmt::format("{}:{}|", e.folder.string(), source->snapshot->generation());
        if (names) names->push_back(source->name);
    }
    return bases;
}

const std::vector<study::TrainingCard>* RepertoireTrainingPage::plannedCards(std::size_t index) {
    const auto& e = app_.repertoires().entries().at(index);
    std::string bases_key;
    auto bases    = trainingBases(&bases_key, nullptr);
    const auto of = fmt::format("{}|{}|{}", e.file.string(), e.revision, bases_key);
    if (planning_ && isReady(planning_->cards)) {
        try {
            planned_[planning_->file] = {planning_->of, planning_->cards.get()};
        } catch (const std::exception& ex) {
            // A database's tree that cannot be read: the cards in the repertoire's own order, why said at the Board's
            // corner and in the log; ordered again once the databases change.
            if (const auto planned = repertoireOf(planning_->file)) {
                const auto& p             = app_.repertoires().entries()[*planned];
                planned_[planning_->file] = {planning_->of, study::trainingCards(p.lines, p.side)};
            }
            app_.toast(
                loggedStoreError(trf("The new positions come in the repertoire's order: {}.", storeErrorText(ex)), ex));
        }
        planning_.reset();
    }
    if (const auto it = planned_.find(e.file); it != planned_.end() && it->second.of == of) return &it->second.cards;
    // One at a time: the next once the one being made is done.
    if (!planning_) {
        Planning job;
        job.file  = e.file;
        job.of    = of;
        job.cards = std::async(std::launch::async, [lines = e.lines, side = e.side, bases = std::move(bases)] {
            const study::Repertoire repertoire(lines, side);
            return study::trainingCards(lines, side, study::trainingPlan(repertoire, study::treeMoves(bases)));
        });
        planning_ = std::move(job);
    }
    return nullptr;
}

std::string RepertoireTrainingPage::listLine(std::size_t index) {
    const auto& c = summary(index).counts;
    if (c.cards == 0) return TR("nothing to train yet");
    if (c.due > 0) return trn("{} to review", "{} to review", c.due, ui::exactCount(c.due));
    return TR("nothing to review");
}

std::size_t RepertoireTrainingPage::cardCount(std::size_t index) { return summary(index).counts.cards; }

std::array<std::unordered_set<uint64_t>, 2> RepertoireTrainingPage::repertoirePositions() {
    std::array<std::unordered_set<uint64_t>, 2> keys;
    for (std::size_t i = 0; i < app_.repertoires().entries().size(); ++i) {
        auto& set = keys[app_.repertoires().entries()[i].side == chess::Color::WHITE ? 0 : 1];
        for (const auto& c : summary(i).cards) set.insert(c.key);
    }
    return keys;
}

// ---------------------------------------------------------------------------
// The Openings page

void RepertoireTrainingPage::drawCard(std::size_t index) {
    const auto& e        = app_.repertoires().entries()[index];
    const auto& s        = summary(index);
    const auto& c        = s.counts;
    const auto& settings = app_.trainingSettings();
    const auto lock      = app_.sessionLock();
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
                if (primaryButton(TR("Back to the session"))) app_.showSession();
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
                std::exchange(limit_menu_, false))
                ImGui::OpenPopup("##training_limit_menu");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("New positions a day at most"));
            if (ImGui::BeginPopup("##training_limit_menu")) {
                ImGui::TextDisabled("%s", TR("New positions a day at most"));
                for (const int n : {5, 10, 20, 30, 50, 100}) {
                    if (ImGui::MenuItem(ui::exactCount(static_cast<uint64_t>(n)).c_str(), nullptr,
                                        settings.new_per_day == n)) {
                        auto changed        = settings;
                        changed.new_per_day = n;
                        app_.setTrainingSettings(changed);
                    }
                }
                ImGui::EndPopup();
            }
            // Enter starts, unless a field or a menu has the keys.
            const bool keys = keyboard::available();
            if (keys && !here && session > 0 && lock.empty()) {
                const auto owner = ImGui::GetID("##training_start");
                ImGui::SetKeyOwner(ImGuiKey_Enter, owner);
                ImGui::SetKeyOwner(ImGuiKey_KeypadEnter, owner);
                if (keyboard::enterPressed()) start = true;
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
                        study::sessionOrder(*planned, s.kept.memories, trainingDay(app_.now()), 1, false);
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

void RepertoireTrainingPage::startTraining(std::size_t index, TrainingRun::Kind kind) {
    if (!app_.sessionLock().empty() || index >= app_.repertoires().entries().size()) return;
    // The Board taken, the session that holds it closed first, left where it is, its answers kept (#80–#82): the game
    // on the board goes among the recent games (Ctrl+Tab), given back after the session; with the answer, the tree
    // rather than the notation's editing, the moves of the session being no game.
    const auto taken = app_.takeBoard(trf("Training · {}", app_.repertoires().entries()[index].name), true);
    const auto& e    = app_.repertoires().entries()[index];
    auto run         = std::make_unique<TrainingRun>();
    run->kind        = kind;
    run->file        = e.file;
    run->name        = e.name;
    run->side        = e.side;
    run->start_fen   = e.lines.main.start_fen;
    run->kept        = summary(index).kept;
    run->settings    = app_.trainingSettings();
    run->flip        = taken.flip;
    run->tab         = taken.tab;
    run->aside       = taken.aside;
    app_.showSessionGame(gameOf(run->start_fen, {}));  // the repertoire's start, until its cards are ready
    app_.setFlip(e.side == chess::Color::BLACK);
    run->started = Clock::now();
    training_    = std::move(run);
    poll();  // the first question at once when the cards are ready
}

void RepertoireTrainingPage::poll() {
    if (!training_ || training_->phase != Ask::Preparing) return;
    auto& t          = *training_;
    const auto index = repertoireOf(t.file);
    if (!index) return closeTraining(false);
    const auto* cards = plannedCards(*index);
    if (!cards) return;
    const auto day    = trainingDay(app_.now());
    const auto limit  = static_cast<std::size_t>(std::max(0, t.settings.new_per_day));
    const auto counts = study::trainingCounts(*cards, t.kept.memories, day, limit);
    std::size_t fresh = t.kind == TrainingRun::Kind::Review ? 0 : counts.today;
    // Only the new ones once today's are learnt: as many again, asked for.
    if (t.kind == TrainingRun::Kind::New && fresh == 0) fresh = std::min(counts.fresh, std::max<std::size_t>(1, limit));
    // Fair play (#48): a position on the line of one of the user's correspondence games on Lichess is not asked while
    // the game lasts: the repertoire would tell its next move.
    std::unordered_set<uint64_t> left_out;
    if (app_.playingOnLichess()) {
        for (const auto& card : *cards)
            if (app_.correspondencePly(gameOf(t.start_fen, card.line)) >= 0) left_out.insert(card.key);
    }
    t.left_out = left_out.size();
    auto order = study::sessionOrder(*cards, t.kept.memories, day, fresh,
                                     t.kind == TrainingRun::Kind::Due || t.kind == TrainingRun::Kind::Review, left_out);
    t.session.emplace(*cards, std::move(order), t.kept.memories, trainingScheduler());
    t.started = Clock::now();
    if (!t.session->current()) return finishTraining(false);
    askTrainingCard();
}

void RepertoireTrainingPage::showTrainingCard(const study::TrainingCard& card) {
    app_.showSessionGame(gameOf(training_->start_fen, card.line));
}

void RepertoireTrainingPage::askTrainingCard() {
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

void RepertoireTrainingPage::trainingAnswer(chess::Move move) {
    auto& t = *training_;
    if (t.phase != Ask::Asking || !t.session || !t.session->current() || app_.liveGameOn()) return;
    const auto& card = t.session->cards()[*t.session->current()];
    const int ply    = app_.sessionPly();
    if (ply != static_cast<int>(card.line.size())) return;  // the question's position only
    const auto board = boardAt(app_.sessionGame(), false, ply);
    t.played         = chess::uci::moveToSan(board, move);
    t.played_move    = move;
    t.shown          = false;
    t.answered       = Clock::now();
    const bool right = t.session->right(t.played);
    if (!t.retry) trainingGrade(right);
    // The move on the board, after the opponent's.
    app_.playSessionMove(move);
    app_.dropPick();
    app_.dropTypedMove();
    t.phase = right ? Ask::Right : Ask::Wrong;
}

void RepertoireTrainingPage::trainingGrade(bool right) {
    auto& t              = *training_;
    const auto i         = *t.session->current();
    const auto key       = t.session->cards()[i].key;
    t.seconds            = since(t.asked);
    t.rating             = study::gradeOf(right, t.hint, t.seconds);
    const auto now       = app_.now();
    t.graded             = t.session->grade(t.rating, t.played, now);
    t.graded_at          = now;
    t.kept.memories[key] = t.graded.after;
    // How long an answer takes, for the time a session is foreseen to take: a long pause counts as a minute.
    const auto ms    = static_cast<int64_t>(std::min(t.seconds, 60.0) * 1000);
    t.kept.answer_ms = t.kept.answer_ms > 0 ? (t.kept.answer_ms * 4 + ms) / 5 : ms;
    if (!app_.scripted()) {
        if (!writeTrainingFile(t.file, t.kept))
            app_.sayOnBoard(trf("The answer could not be kept in {}: is the profile's folder writable?",
                                trainingFileOf(t.file).string()));
        appendReview(t.file, key, now, t.rating, ms, t.played);
    }
    keep(t);
}

void RepertoireTrainingPage::keep(const TrainingRun& t) {
    if (const auto it = summaries_.find(t.file); it != summaries_.end()) {
        it->second.kept    = t.kept;
        it->second.counted = 0;
    }
}

void RepertoireTrainingPage::trainingDontKnow() {
    auto& t = *training_;
    if (t.phase != Ask::Asking || app_.liveGameOn()) return;
    t.played.clear();
    t.shown    = true;
    t.answered = Clock::now();
    if (!t.retry) trainingGrade(false);
    app_.dropTypedMove();
    t.phase = Ask::Wrong;
}

void RepertoireTrainingPage::trainingRetry() {
    auto& t = *training_;
    if (t.phase != Ask::Wrong || !t.session || !t.session->current()) return;
    app_.askAgainAt(static_cast<int>(t.session->cards()[*t.session->current()].line.size()));
    t.phase = Ask::Asking;
    t.retry = true;
    t.shown = false;
    t.hint  = false;
    t.played.clear();
    t.added.reset();
    t.asked = Clock::now();
}

void RepertoireTrainingPage::trainingNext() {
    auto& t = *training_;
    if (!t.session) return;
    t.session->next();
    if (!t.session->current()) return finishTraining(false);
    askTrainingCard();
}

void RepertoireTrainingPage::trainingAdd() {
    auto& t = *training_;
    if (t.phase != Ask::Wrong || t.shown || t.played.empty() || t.added || !t.session || !t.session->current()) return;
    const auto index = repertoireOf(t.file);
    if (!index || (!t.start_fen.empty() && t.start_fen != chess::constants::STARTPOS)) return;
    const auto i     = *t.session->current();
    const auto& card = t.session->cards()[i];
    std::vector<chess::Move> path;
    auto board = startBoard(t.start_fen);
    if (!play(board, card.line, &path)) return;
    auto& r = app_.repertoires();
    if (r.has(*index, path, t.played_move)) return;
    TrainingRun::Added added{*index, r.entries()[*index].lines, i, t.played};
    if (!r.toggle(*index, path, t.played_move)) return;
    t.session->addMove(i, t.played);
    if (const auto it = t.session->memories().find(card.key); it != t.session->memories().end())
        t.kept.memories[card.key] = it->second;
    if (!app_.scripted()) (void)writeTrainingFile(t.file, t.kept);
    keep(t);
    t.added = std::move(added);
}

void RepertoireTrainingPage::trainingTakeBack() {
    auto& t = *training_;
    if (!t.added || !t.session) return;
    app_.repertoires().restore(t.added->repertoire, t.added->lines);
    t.session->takeBack(t.added->card, t.added->san);
    const auto key = t.session->cards()[t.added->card].key;
    if (const auto it = t.session->memories().find(key); it != t.session->memories().end())
        t.kept.memories[key] = it->second;
    if (!app_.scripted()) (void)writeTrainingFile(t.file, t.kept);
    keep(t);
    t.added.reset();
}

void RepertoireTrainingPage::finishTraining(bool stopped) {
    auto& t        = *training_;
    t.phase        = Ask::End;
    t.stopped      = stopped;
    t.took         = since(t.started);
    t.missed_shown = -1;
    app_.dropPick();
    app_.dropTypedMove();
}

void RepertoireTrainingPage::reviewMissed() {
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

void RepertoireTrainingPage::closeTraining(bool to_openings) {
    if (!training_) return;
    // Left during the day's review (#80): back to the Training page, what the review had left forgotten.
    const bool to_training = to_openings && app_.reviewRunning();
    if (to_openings) app_.stopReview();
    BoardTaken taken;
    taken.aside = training_->aside;
    taken.flip  = training_->flip;
    taken.tab   = training_->tab;
    training_.reset();
    app_.giveBoardBack(taken, to_training);  // the session's position is no game to remember
    if (to_openings && !to_training) app_.showOpeningsPage();
}

void RepertoireTrainingPage::dropTraining() {
    BoardTaken taken;
    taken.aside = training_->aside;
    taken.flip  = training_->flip;
    taken.tab   = training_->tab;
    training_.reset();
    app_.leaveBoard(taken);  // the game that comes takes the board; the one put aside stays among the recent ones
}

// ---------------------------------------------------------------------------
// The Board page during a session

void RepertoireTrainingPage::trainingKeys() {
    auto& t        = *training_;
    const auto& io = ImGui::GetIO();
    if (!keyboard::available()) return;
    // Enter, Space, Escape and the arrows are the training's: ImGui's keyboard navigation leaves them alone (the Tree
    // tab still reads its arrows).
    const auto owner = ImGui::GetID("##training_keys");
    for (const auto key : {ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Space, ImGuiKey_Escape, ImGuiKey_LeftArrow,
                           ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Home, ImGuiKey_End})
        ImGui::SetKeyOwner(key, owner);
    const bool plain = !io.KeyCtrl && !io.KeyAlt && !io.KeySuper;
    const bool enter  = plain && keyboard::enterPressed();
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    switch (t.phase) {
        case Ask::Preparing:
            if (escape) closeTraining(true);
            break;
        case Ask::Asking:
            if (escape) {
                finishTraining(true);
            } else if (!app_.liveGameOn()) {
                if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_H)) t.hint = true;
                if (plain && ImGui::IsKeyPressed(ImGuiKey_Space, false))
                    trainingDontKnow();
                else
                    app_.moveKeys();
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
            } else {
                (void)app_.analysisKey();  // Alt+A: the engine, now that the answer is given
            }
            break;
        case Ask::End: {
            const bool missed = t.session && std::any_of(t.session->outcomes().begin(), t.session->outcomes().end(),
                                                         [](const auto& o) { return o.rating == Rating::Again; });
            if (app_.reviewRunning()) {  // the day's review (#80): Enter goes on, Escape back to the Training page
                if (escape)
                    closeTraining(true);
                else if (enter && !app_.nextReview())
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

void RepertoireTrainingPage::drawTrainingPage() {
    // The page's title, the letters typed reaching the move field whatever the keyboard's layout.
    app_.beginSessionPage();
    trainingKeys();
    if (!training_) return;  // left by a key
    auto& t = *training_;
    // A right answer goes on by itself after a moment, unless the mouse is on it.
    if (t.phase == Ask::Right && t.settings.advance_ms > 0 && !t.hovered &&
        since(t.answered) * 1000 >= t.settings.advance_ms) {
        trainingNext();
        if (!training_) return;
    }
    app_.drawBoardMessage();
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

void RepertoireTrainingPage::drawTrainingBoard(float size) {
    auto& t          = *training_;
    const auto& game = app_.sessionGame();
    const int ply    = app_.sessionPly();
    const auto board = boardAt(game, false, ply);
    // The session's marks over the board, which draws the position shown, its last move and the side below.
    BoardDecor decor;
    std::optional<chess::Move> last_move;
    if (ply > 0) last_move = game.moves[static_cast<std::size_t>(ply - 1)].move;
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
    const bool question = card && ply == static_cast<int>(card->line.size());
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
            if (last_move) {
                decor.tints.push_back({last_move->from(), green});
                decor.tints.push_back({shownTarget(*last_move), green});
            }
            break;
        case Ask::Wrong:
            if (t.added) {  // the move played is the repertoire's now
                if (last_move && !question) {
                    decor.tints.push_back({last_move->from(), green});
                    decor.tints.push_back({shownTarget(*last_move), green});
                }
                break;
            }
            if (!t.shown && !question && last_move) {  // the move played, in orange
                decor.tints.push_back({last_move->from(), orange});
                decor.tints.push_back({shownTarget(*last_move), orange});
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
    // The user's moves taken on the board only at the question's position, while no live game holds the aids.
    const bool moves = t.phase == Ask::Asking && question && !app_.liveGameOn();
    app_.drawSessionBoard(size, decor, moves);
}

void RepertoireTrainingPage::drawTrainingColumn() {
    auto& t = *training_;
    const ui::TopicRegion topic(guide::topics::Training);
    if (app_.liveGameOn()) {  // fair play (#48): a live game on Lichess stops the training where it is
        app_.drawFairPlayBanner();
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
            app_.drawMoveField();
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
                            t.settings.advance_ms = ms;
                            auto changed          = app_.trainingSettings();
                            changed.advance_ms    = ms;
                            app_.setTrainingSettings(changed);
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
                if (ImGui::Button(TR("See the tree here"))) app_.showTreeAt(static_cast<int>(card.line.size()));
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
        app_.drawBoardTabs();
    }
}

void RepertoireTrainingPage::drawTrainingEnd() {
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
    const auto day       = trainingDay(app_.now());
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
    if (app_.reviewRunning()) {  // the day's review (#80): the next kind, or back to the Training page
        app_.drawReviewNext(false);
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
    if (!app_.reviewRunning()) {
        ImGui::SameLine(0, px(8));
        ImGui::TextDisabled("%s", missed.empty() ? TR("Enter") : TR("Enter: review them · Esc: back"));
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    wrapped(TR("A session stopped midway keeps what was answered; the next one goes on with what is still due."), true);
}

// ---------------------------------------------------------------------------
// The day's review, the Home page, the palette, the focus words

ReviewCount RepertoireTrainingPage::reviewCount() {
    ReviewCount out;
    out.kind       = ReviewKind::Repertoire;
    int64_t ms     = 0;
    int64_t weight = 0;
    for (std::size_t i = 0; i < app_.repertoires().entries().size(); ++i) {
        const auto& s = summary(i);
        out.due += s.counts.due;
        out.tomorrow += s.counts.tomorrow;
        out.three_days += s.counts.three_days;
        if (s.kept.answer_ms > 0) {
            ms += s.kept.answer_ms * static_cast<int64_t>(std::max<std::size_t>(1, s.counts.due));
            weight += static_cast<int64_t>(std::max<std::size_t>(1, s.counts.due));
        }
    }
    out.answer_ms = weight > 0 ? ms / weight : 0;
    return out;
}

std::vector<std::size_t> RepertoireTrainingPage::dueRepertoires() {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < app_.repertoires().entries().size(); ++i)
        if (summary(i).counts.due > 0) out.push_back(i);
    return out;
}

bool RepertoireTrainingPage::startReview(std::size_t repertoire) {
    if (repertoire >= app_.repertoires().entries().size() || summary(repertoire).counts.due == 0) return false;
    startTraining(repertoire, TrainingRun::Kind::Review);
    return training_ != nullptr;
}

std::string RepertoireTrainingPage::reviewNextLabel(std::size_t repertoire) {
    if (repertoire >= app_.repertoires().entries().size()) return {};
    const auto n = summary(repertoire).counts.due;
    return trn("Next: {} position of “{}”", "Next: {} positions of “{}”", n, ui::exactCount(n),
               app_.repertoires().entries()[repertoire].name);
}

std::string RepertoireTrainingPage::dueWords(std::size_t n) const {
    return trn("repertoire position", "repertoire positions", n);
}

void RepertoireTrainingPage::drawHomeLine() {
    if (!training_) return;
    const auto& t    = *training_;
    const auto label = t.session && t.phase != Ask::End && t.phase != Ask::Preparing
                           ? trf("Training “{}”: position {} of {}", t.name, t.position, t.session->total())
                           : trf("Training “{}”", t.name);
    ImGui::TextUnformatted(label.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Back to the session"))) app_.showSession();
}

void RepertoireTrainingPage::addOpeningsCommands(const PaletteAdd& add) {
    for (std::size_t i = 0; i < app_.repertoires().entries().size(); ++i) {
        const auto& counts = summary(i).counts;
        if (counts.cards == 0) continue;
        const auto& name = app_.repertoires().entries()[i].name;
        add(trf("Train the repertoire “{}”", name),
            counts.due > 0
                ? trn("{} position to review", "{} positions to review", counts.due, ui::exactCount(counts.due))
                : std::string(TR("Openings")),
            [this, i] { startTraining(i, TrainingRun::Kind::Due); });
        if (counts.fresh > 0)
            add(trf("Train only the new positions of “{}”", name), TR("Openings"),
                [this, i] { startTraining(i, TrainingRun::Kind::New); });
    }
    add(app_.trainingSettings().advance_ms > 0 ? TR("Training: the next position on Enter only")
                                               : TR("Training: the next position on its own after a right answer"),
        TR("Openings"), [this] {
            auto s       = app_.trainingSettings();
            s.advance_ms = s.advance_ms > 0 ? 0 : 1000;
            if (training_) training_->settings.advance_ms = s.advance_ms;
            app_.setTrainingSettings(s);
        });
    add(TR("Training: new positions a day…"), TR("Openings"), [this] {
        app_.showOpeningsPage();
        limit_menu_ = true;
    });
}

void RepertoireTrainingPage::addFocusWords(FocusWords& words, const std::string& word) {
    words.on({"training", "training-new"}, [&] {  // a session of the active repertoire's training (#76)
        if (app_.repertoires().active() >= 0)
            startTraining(static_cast<std::size_t>(app_.repertoires().active()),
                          word == "training" ? TrainingRun::Kind::Due : TrainingRun::Kind::New);
    });
}

}  // namespace fcg
