// Puzzles (#80, mockup M12, board 1), a module of its own out of App (#107; puzzles_page.hpp). A series runs on the
// Board page, whose column becomes the puzzle's, as the repertoires' training does: a puzzle from Lichess's database
// offline, from the batch of the user's Lichess account, or a puzzle missed asked again by the day's review. The states
// of the mockup: the question, right, wrong, the end of the series. The Training page's Puzzles part: the choice of
// puzzles, Lichess's database offline (its preparation, Update and Remove), the batch online, the results by theme.
//
// The keys of a question: a move played on the board, or typed (its letters open the move field); Alt+H a hint (the
// piece to play; the puzzle then counts as missed), Space "I don't know", Escape stops. After the answer, Enter goes on
// (or tries again after a wrong one), → plays the solution a move at a time, Alt+A the engine. On the Training page,
// Enter starts the puzzles once one of their choices was clicked; Ctrl+K has the page, each theme's puzzles and the
// database's commands.

#include "puzzles_page.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>  // SetKeyOwner

#include <study/game_tree.hpp>
#include <tour/util.hpp>

#include "board_text.hpp"
#include "board_view.hpp"
#include "console.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "keyboard.hpp"
#include "lichess_account.hpp"
#include "lichess_months.hpp"
#include "lichess_profile.hpp"
#include "machines.hpp"
#include "mistakes.hpp"
#include "training.hpp"
#include "ui.hpp"
#include "widgets.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

using Clock       = std::chrono::steady_clock;
using PuzzlePhase = PuzzleRun::Phase;

double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

// The opponent's reply comes after this: time to see the move that was right.
constexpr double REPLY_SECONDS = 0.5;

// How much of its tone a tinted card takes: the answer's on the Board page, a card of the Training page.
constexpr ui::Tint SERIES_TINT{.fill = 0.10f};
constexpr ui::Tint PAGE_TINT{.fill = 0.08f};

// A tooltip for the item just drawn, also when it is disabled, its lines wrapped.
void tip(const std::string& text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

using ui::beginTinted;
using ui::chip;
using ui::endTinted;
using ui::mix;
using ui::numbered;
using ui::primaryButton;
using ui::wrapped;

// A count in large figures over what it counts.
void bigCount(const std::string& n, const std::string& what) {
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.9f);
    ImGui::TextUnformatted(n.c_str());
    ImGui::PopFont();
    ImGui::TextDisabled("%s", what.c_str());
    ImGui::EndGroup();
}

// The space the profile's disk has left; 0 when it cannot be said.
uint64_t freeSpace(const fs::path& folder) {
    std::error_code ec;
    const auto info = fs::space(fs::exists(folder, ec) ? folder : folder.parent_path(), ec);
    return ec ? 0 : info.available;
}

// The game the board shows for a puzzle: from its position, the opponent's move, then `played` moves of the solution.
ReplayGame gameOf(const PuzzlePlay& play, std::size_t played) {
    ReplayGame game;
    game.start_fen = play.before.getFen();
    auto board     = play.before;
    std::vector<chess::Move> moves{play.opponent};
    for (std::size_t i = 0; i < played && i < play.solution.size(); ++i) moves.push_back(play.solution[i]);
    for (const auto m : moves) {
        ReplayMove r;
        r.san   = chess::uci::moveToSan(board, m);
        r.move  = m;
        r.mover = board.sideToMove();
        game.moves.push_back(std::move(r));
        board.makeMove(m);
    }
    return game;
}

// The board after the opponent's move and `played` moves of the solution.
chess::Board boardOf(const PuzzlePlay& play, std::size_t played) {
    auto board = play.before;
    board.makeMove(play.opponent);
    for (std::size_t i = 0; i < played && i < play.solution.size(); ++i) board.makeMove(play.solution[i]);
    return board;
}

// "Fork, middlegame, short": a puzzle's themes in the words of the interface.
std::string themesText(const std::vector<std::string>& themes) {
    std::string out;
    for (const auto& t : themes) out += (out.empty() ? "" : ", ") + puzzleThemeName(t);
    return out;
}

// The theme a puzzle is shown with: its first in the page's order (the motifs before the phases and lengths).
std::string mainTheme(const std::vector<std::string>& themes, const std::string& otherwise) {
    for (const auto key : puzzleThemesShown())
        if (std::find(themes.begin(), themes.end(), key) != themes.end()) return puzzleThemeName(key);
    return themes.empty() ? otherwise : puzzleThemeName(themes.front());
}

// "1742 → 1751 (+9)".
std::string ratingChange(double before, double after) {
    const int b = static_cast<int>(std::lround(before)), a = static_cast<int>(std::lround(after));
    return fmt::format("{} → {} ({:+})", b, a, a - b);
}

// When a card comes back: "tomorrow", "in 6 days".
std::string whenText(int64_t seconds) {
    const auto days = std::max<int64_t>(1, (seconds + study::fsrs::DAY / 2) / study::fsrs::DAY);
    if (days == 1) return TR("tomorrow");
    return trn("in {} day", "in {} days", static_cast<uint64_t>(days), days);
}

// The themes offered first on the page, as chips: the motifs most asked for, then a mate and an ending.
constexpr std::string_view FIRST_THEMES[] = {"fork",        "pin",        "discoveredAttack", "mateIn2",
                                             "rookEndgame", "deflection", "hangingPiece",     "sacrifice"};

constexpr std::string_view OPENING_PREFIX = "opening:";

}  // namespace

PuzzlesPage::PuzzlesPage(PuzzlesContext& app)
    : app_(app), offline_puzzles_(std::make_unique<OfflinePuzzles>(app.jobs(), app.profileDir() / "lichess-puzzles")) {}

PuzzlesPage::~PuzzlesPage() = default;

bool PuzzlesPage::hidesAids() const {
    return puzzle_ && (puzzle_->phase == PuzzlePhase::Asking || puzzle_->phase == PuzzlePhase::Reply);
}

const char* PuzzlesPage::guideTopic() const { return guide::topics::Puzzles; }

// ---------------------------------------------------------------------------
// The user's puzzles, Lichess's database

PuzzleRecord& PuzzlesPage::puzzleRecord() {
    if (!puzzle_record_) puzzle_record_ = readPuzzleRecord(puzzlesFolder());
    return *puzzle_record_;
}

std::optional<int> PuzzlesPage::lichessPuzzleRating() const {
    if (online_batch_ && online_batch_->glicko && online_batch_->glicko->rating > 0)
        return static_cast<int>(std::lround(online_batch_->glicko->rating));
    const auto* profile = app_.lichessProfile();
    if (app_.lichessAccount() && profile && profile->puzzle_rating > 0) return profile->puzzle_rating;
    return std::nullopt;
}

PuzzleRating PuzzlesPage::puzzleRating() {
    auto& r = puzzleRecord();
    if (r.rating) return *r.rating;
    std::optional<int> deviation;
    const auto* profile = app_.lichessProfile();
    if (profile && profile->puzzle_deviation > 0) deviation = profile->puzzle_deviation;
    return startingRating(app_.lichessAccount() ? lichessPuzzleRating() : std::nullopt, deviation);
}

void PuzzlesPage::startOffline(std::string source) {
    if (!offline_puzzles_ || offline_puzzles_->busy()) return;
    offline_puzzles_error_.clear();
    offline_puzzles_failure_ = {};
    offline_puzzles_note_.clear();
    offline_puzzles_since_ = Clock::now();
    offline_puzzles_from_  = 0;
    offline_puzzles_->start(std::move(source));
}

lichess::OnlineBatch* PuzzlesPage::onlineBatch() {
    if (!online_batch_loaded_) {
        online_batch_loaded_ = true;
        online_batch_        = lichess::loadOnlineBatch(puzzlesFolder());
    }
    return online_batch_ ? &*online_batch_ : nullptr;
}

void PuzzlesPage::saveOnlineBatch() {
    if (app_.scripted() || !online_batch_) return;
    if (!lichess::saveOnlineBatch(puzzlesFolder(), *online_batch_))
        Console::instance().add(trf("The puzzles of Lichess's batch could not be kept in {}", puzzlesFolder().string()),
                                true);
}

bool PuzzlesPage::puzzleScopes() const {
    const auto* account = app_.lichessAccount();
    return account && account->has(lichess::SCOPE_PUZZLE_READ) && account->has(lichess::SCOPE_PUZZLE_WRITE);
}

void PuzzlesPage::askPuzzleScopes() {
    const auto* account = app_.lichessAccount();
    if (app_.lichessLinking() || !account) return;
    auto scopes    = account->scopes;
    const auto add = [&](const char* scope) {
        if (std::find(scopes.begin(), scopes.end(), scope) == scopes.end()) scopes.emplace_back(scope);
    };
    add(lichess::SCOPE_PUZZLE_READ);
    add(lichess::SCOPE_PUZZLE_WRITE);
    app_.startLichessLink(scopes, account->username);
}

void PuzzlesPage::askOnlineBatch() {
    if (online_ask_.valid() || online_send_.valid() || !puzzleScopes()) return;
    online_error_.clear();
    online_status_    = 0;
    std::string angle = puzzles_theme_.empty() ? std::string("mix") : puzzles_theme_;
    if (angle.rfind(OPENING_PREFIX, 0) == 0) angle = angle.substr(OPENING_PREFIX.size());
    online_ask_ = std::async(std::launch::async, [token      = app_.lichessAccount()->token, angle,
                                                  difficulty = std::string(difficultyKey(puzzles_difficulty_)),
                                                  stop       = app_.jobs().cancelFlag()] {
        return lichess::puzzleBatch(token, angle, lichess::PUZZLE_BATCH, difficulty, stop.get());
    });
}

void PuzzlesPage::sendOnlineResults() {
    auto* batch = onlineBatch();
    if (!batch || batch->results.empty() || online_send_.valid() || online_ask_.valid() || !puzzleScopes()) return;
    online_error_.clear();
    online_status_ = 0;
    // The next batch in the same answer, unless another theme or difficulty was chosen since: then asked after.
    std::string angle = puzzles_theme_.empty() ? std::string("mix") : puzzles_theme_;
    if (angle.rfind(OPENING_PREFIX, 0) == 0) angle = angle.substr(OPENING_PREFIX.size());
    const std::string difficulty = difficultyKey(puzzles_difficulty_);
    const bool same              = angle == batch->angle && difficulty == batch->difficulty;
    online_send_ = std::async(std::launch::async, [token = app_.lichessAccount()->token, angle = batch->angle,
                                                   solutions = batch->results, difficulty = batch->difficulty, same] {
        return lichess::solvePuzzles(token, angle, solutions, same ? lichess::PUZZLE_BATCH : 0, difficulty);
    });
}

void PuzzlesPage::poll() {
    if (offline_puzzles_) {
        if (isReady(offline_puzzles_update_)) {
            const auto size  = offline_puzzles_update_.get();
            const auto state = offline_puzzles_->state();
            if (size.failed())
                offline_puzzles_note_ = trf("The puzzles kept stay: {}.", lichess::describe(size));
            else if (!size.date.empty() && size.date == state.date)
                offline_puzzles_note_ = trf("Already the latest file, of {}.", size.date);
            else
                startOffline(lichess::puzzlesUrl());
        }
        if (const auto end = offline_puzzles_->finished()) {
            const auto state = offline_puzzles_->state();
            if (end->done()) {
                Console::instance().add(trf("Lichess's puzzles kept offline: {} puzzles, {}",
                                            ui::exactCount(state.puzzles), formatBytes(state.bytes)));
            } else if (end->failed()) {
                offline_puzzles_error_   = end->error;
                offline_puzzles_failure_ = end->net;
                Console::instance().add(trf("Lichess's puzzles offline: the preparation stopped: {}", end->error),
                                        true);
            }
        }
    }
    // Online: a batch asked for, results sent.
    if (isReady(online_ask_)) {
        auto answer    = online_ask_.get();
        online_status_ = answer.status;
        if (answer.value && !answer.value->puzzles.empty()) {
            lichess::OnlineBatch b;
            std::string angle = puzzles_theme_.empty() ? std::string("mix") : puzzles_theme_;
            if (angle.rfind(OPENING_PREFIX, 0) == 0) angle = angle.substr(OPENING_PREFIX.size());
            b.angle      = angle;
            b.difficulty = difficultyKey(puzzles_difficulty_);
            b.rated      = onlineBatch() ? onlineBatch()->rated : true;
            b.received   = app_.now();
            b.puzzles    = std::move(answer.value->puzzles);
            b.glicko     = answer.value->glicko;
            if (const auto* old = onlineBatch()) {  // what the last send said stays shown
                b.sent_at     = old->sent_at;
                b.sent_count  = old->sent_count;
                b.sent_status = old->sent_status;
            }
            online_batch_ = std::move(b);
            saveOnlineBatch();
            if (std::exchange(online_start_after_, false)) startPuzzles();
        } else {
            online_start_after_ = false;
            online_error_       = answer.value ? std::string(TR("Lichess has no puzzle of this theme for you now."))
                                               : lichess::describe(answer);
        }
    }
    if (isReady(online_send_)) {
        auto answer    = online_send_.get();
        online_status_ = answer.status;
        auto* batch    = onlineBatch();
        if (answer.value && batch) {
            const auto count = batch->results.size();
            lichess::OnlineBatch b;
            b.angle       = batch->angle;
            b.difficulty  = batch->difficulty;
            b.rated       = batch->rated;
            b.received    = app_.now();
            b.puzzles     = std::move(answer.value->next.puzzles);
            b.glicko      = answer.value->next.glicko ? answer.value->next.glicko : batch->glicko;
            b.sent_at     = app_.now();
            b.sent_count  = static_cast<int>(count);
            b.sent_status = "accepted";
            online_batch_ = std::move(b);
            online_retry_ = {};
            saveOnlineBatch();
            Console::instance().add(trf("Lichess's puzzles: {} answers sent and taken", count));
            // Another theme chosen since: its batch asked for now.
            if (online_batch_->puzzles.empty()) askOnlineBatch();
        } else {
            online_error_ = lichess::describe(answer);
            // Kept: sent again by itself in a minute, unless Lichess refused the token. The last send that went
            // through stays said as it was.
            if (answer.net != lichess::NetError::Unauthorized && answer.net != lichess::NetError::Forbidden)
                online_retry_ = Clock::now() + std::chrono::seconds(61);
        }
    }
    // A batch done and not taken yet (a 429, a cut, the program closed meanwhile): sent again.
    if (app_.lichessAccount() && puzzleScopes() && !online_ask_.valid() && !online_send_.valid()) {
        auto* batch    = onlineBatch();
        const bool due = online_retry_ == Clock::time_point{} ? batch && batch->complete() && online_error_.empty()
                                                              : Clock::now() >= online_retry_;
        if (batch && batch->complete() && due) {
            online_retry_ = {};
            sendOnlineResults();
        }
    }
}

// ---------------------------------------------------------------------------
// The day's review, the other parts of the Training page

ReviewCount PuzzlesPage::reviewCount() {
    ReviewCount out;
    out.kind       = ReviewKind::Puzzles;
    const auto day = trainingDay(app_.now());
    const auto& r  = puzzleRecord();
    out.due        = cardsDue(r.cards, day.end);
    out.tomorrow   = cardsDue(r.cards, day.end + study::fsrs::DAY) - out.due;
    out.three_days = cardsDue(r.cards, day.end + 3 * study::fsrs::DAY) - out.due;
    // The pace of the last answers.
    int64_t ms = 0, n = 0;
    for (auto it = r.results.rbegin(); it != r.results.rend() && n < 50; ++it, ++n)
        ms += std::min<int64_t>(it->ms, 120000);
    out.answer_ms = n > 0 ? ms / n : 0;
    return out;
}

std::string PuzzlesPage::reviewNextLabel(std::size_t) {
    const auto n = reviewCount().due;
    return trn("Next: {} missed puzzle", "Next: {} missed puzzles", n, ui::exactCount(n));
}

std::string PuzzlesPage::dueWords(std::size_t n) const { return trn("missed puzzle", "missed puzzles", n); }

const char* PuzzlesPage::reviewOnlyLabel() const { return TR("Only the puzzles"); }

void PuzzlesPage::drawHomeLine() {
    if (!puzzle_) return;
    const auto& p    = *puzzle_;
    const auto label = p.phase != PuzzleRun::Phase::End
                           ? trf("Puzzles “{}”: {} of {}", p.title, std::min(p.done + 1, p.total), p.total)
                           : trf("Puzzles “{}”", p.title);
    ImGui::TextUnformatted(label.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Back to the series"))) app_.showSession();
}

bool PuzzlesPage::startTheme(const std::string& theme) {
    puzzles_theme_ = theme;
    if (store()) {
        puzzles_online_ = false;
        startPuzzles();
        return true;
    }
    if (puzzleScopes()) {
        puzzles_online_ = true;
        startPuzzles();
        return true;
    }
    puzzles_online_ = false;
    return false;
}

void PuzzlesPage::addCommands(const PaletteAdd& add, const std::string& training_view) {
    add(TR("Puzzles"), training_view, [this] { app_.showTrainingPage(TrainingPart::Puzzles); });
    if (offline_puzzles_) {
        const auto store = offline_puzzles_->store();
        if (store && puzzleLock().empty()) {
            add(TR("Puzzles: random"), TR("Training"), [this] {
                puzzles_online_ = false;
                puzzles_theme_.clear();
                startPuzzles();
            });
            for (const auto key : puzzleThemesShown()) {
                if (!store->themeList(key)) continue;
                add(trf("Puzzles: {}", puzzleThemeName(key)), TR("Training"), [this, key = std::string(key)] {
                    puzzles_online_ = false;
                    puzzles_theme_  = key;
                    startPuzzles();
                });
            }
        }
        if (offline_puzzles_->busy()) {
            add(TR("Cancel the download of Lichess's puzzles"), TR("Training"), [this] { offline_puzzles_->cancel(); });
        } else if (store) {
            add(TR("Update Lichess's puzzles"), TR("Training"), [this] {
                app_.showTrainingPage(TrainingPart::Puzzles);
                offline_puzzles_note_.clear();
                offline_puzzles_update_ = std::async(std::launch::async, [stop = app_.jobs().cancelFlag()] {
                    return evalsSize(lichess::puzzlesUrl(), stop.get());
                });
            });
            add(TR("Remove Lichess's puzzles…"), TR("Training"), [this] {
                app_.showTrainingPage(TrainingPart::Puzzles);
                offline_puzzles_remove_ = true;
            });
        } else {
            add(TR("Download Lichess's puzzles"), TR("Training"), [this] {
                app_.showTrainingPage(TrainingPart::Puzzles);
                startOffline(lichess::puzzlesUrl());
            });
        }
    }
    add(TR("Puzzles online, from my Lichess account"), "Lichess", [this] {
        app_.showTrainingPage(TrainingPart::Puzzles);
        puzzles_online_ = true;
    });
}

void PuzzlesPage::addFocusWords(FocusWords& words, const std::string& w) {
    words.on({"puzzles-online"}, [&] {  // the Training page's Puzzles part (#80), the puzzles online chosen
        app_.showTrainingPage(TrainingPart::Puzzles);
        puzzles_online_ = true;
    });
    words.on({"puzzles-file=*"}, [&] {  // puzzles-file=FILE: Lichess's puzzle database read from it
        app_.showTrainingPage(TrainingPart::Puzzles);
        startOffline(w.substr(13));
    });
    words.on({"puzzles-series", "puzzles-series=*"}, [&] {
        // puzzles-series[=THEME]: a series of the offline puzzles at once, of THEME (a Lichess key) or any.
        puzzles_online_ = false;
        puzzles_theme_  = w.size() > 15 ? w.substr(15) : std::string();
        startPuzzles();
    });
}

// ---------------------------------------------------------------------------
// A series

std::string PuzzlesPage::puzzleLock() const { return app_.sessionLock(); }

void PuzzlesPage::startPuzzles() {
    if (!puzzleLock().empty()) return;
    auto run = std::make_unique<PuzzleRun>();
    if (puzzles_online_) {
        auto* batch = onlineBatch();
        if (!puzzleScopes()) return;
        if (!batch || batch->left() == 0) {
            online_start_after_ = true;
            askOnlineBatch();
            return;
        }
        run->source = PuzzleRun::Source::Online;
        run->title  = batch->angle == "mix" ? std::string(TR("Random")) : puzzleThemeName(batch->angle);
        for (const auto& p : batch->puzzles)
            if (!batch->answered(p.id)) run->puzzles.push_back(p);
    } else {
        const auto store = offline_puzzles_ ? offline_puzzles_->store() : nullptr;
        if (!store) return;
        int list = -1;
        if (!puzzles_theme_.empty()) {
            const auto opening = std::string_view(puzzles_theme_).rfind("opening:", 0) == 0;
            const auto found =
                opening ? store->openingList(puzzles_theme_.substr(8)) : store->themeList(puzzles_theme_);
            if (!found) {
                app_.toast(trf("No puzzle of “{}” in this database.", puzzleThemeName(puzzles_theme_)));
                return;
            }
            list = *found;
        }
        const int rating =
            static_cast<int>(std::lround(puzzleRating().glicko.rating)) + difficultyOffset(puzzles_difficulty_);
        const auto& seen = puzzleRecord().seen;
        const auto seed  = static_cast<uint64_t>(Clock::now().time_since_epoch().count());
        for (const auto k :
             store->pick(list, rating, PUZZLE_SERIES, [&](const std::string& id) { return seen.count(id) > 0; }, seed))
            run->puzzles.push_back(store->puzzle(k));
        run->source = PuzzleRun::Source::Offline;
        run->title  = puzzles_theme_.empty() ? std::string(TR("Random"))
                      : std::string_view(puzzles_theme_).rfind("opening:", 0) == 0
                          ? puzzleOpeningName(puzzles_theme_.substr(8))
                          : puzzleThemeName(puzzles_theme_);
    }
    if (run->puzzles.empty()) {
        app_.toast(TR("No puzzle left of this choice: every one was seen."));
        return;
    }
    startPuzzleRun(std::move(run));
}

bool PuzzlesPage::startReview() {
    if (!puzzleLock().empty()) return false;
    const auto day = trainingDay(app_.now());
    auto run       = std::make_unique<PuzzleRun>();
    run->source    = PuzzleRun::Source::Review;
    run->title     = TR("Puzzles to review");
    // The longest overdue first.
    std::vector<const PuzzleCard*> due;
    for (const auto& [id, c] : puzzleRecord().cards)
        if (c.card.due <= day.end) due.push_back(&c);
    std::sort(due.begin(), due.end(), [](const auto* a, const auto* b) { return a->card.due < b->card.due; });
    for (const auto* c : due) run->puzzles.push_back(c->puzzle);
    if (run->puzzles.empty()) return false;
    startPuzzleRun(std::move(run));
    return true;
}

void PuzzlesPage::startPuzzleRun(std::unique_ptr<PuzzleRun> run) {
    for (std::size_t i = 0; i < run->puzzles.size(); ++i) run->queue.push_back(i);
    run->total        = run->puzzles.size();
    run->rating_start = puzzleRating().glicko.rating;
    // The Board taken, the session that holds it closed (the day's review going on): the game on the board goes among
    // the recent games (Ctrl+Tab), given back after the series; with the answer, the tree rather than the notation's
    // editing.
    const auto taken = app_.takeBoard(trf("Puzzles · {}", run->title), true);
    run->aside       = taken.aside;
    run->flip        = taken.flip;
    run->tab         = taken.tab;
    run->started     = Clock::now();
    puzzle_          = std::move(run);
    askPuzzle();
}

void PuzzlesPage::showPuzzlePosition(std::size_t index, std::size_t played) {
    auto& p = *puzzle_;
    if (index != p.current || !p.play) return;
    app_.showSessionGame(gameOf(*p.play, played));
}

void PuzzlesPage::askPuzzle() {
    auto& p = *puzzle_;
    while (!p.queue.empty()) {
        p.current = p.queue.front();
        p.play    = puzzlePlay(p.puzzles[p.current]);
        if (p.play) break;
        p.queue.pop_front();  // its moves do not play: left out
        p.total = p.total > 0 ? p.total - 1 : 0;
    }
    if (p.queue.empty()) return finishPuzzles(false);
    p.again = std::any_of(p.outcomes.begin(), p.outcomes.end(), [&](const auto& o) { return o.puzzle == p.current; });
    p.step  = 0;
    p.hint = p.hinted = p.retry = p.shown = p.hovered = p.next_again = false;
    p.solution_shown                                                 = 0;
    p.played.clear();
    p.phase = PuzzlePhase::Asking;
    showPuzzlePosition(p.current, 0);
    // From the side of the one who solves it: the side to move after the opponent's move.
    app_.setFlip(p.play->before.sideToMove() == chess::Color::WHITE);
    p.asked = Clock::now();
}

void PuzzlesPage::puzzleAnswer(chess::Move move) {
    auto& p = *puzzle_;
    if (p.phase != PuzzlePhase::Asking || !p.play || app_.liveGameOn()) return;
    const int ply = app_.sessionPly();
    if (ply != static_cast<int>(1 + p.step)) return;  // the question's position only
    const auto board  = boardAt(app_.sessionGame(), false, ply);
    const auto answer = judgeMove(*p.play, p.step, board, move);
    p.played          = chess::uci::moveToSan(board, move);
    p.played_move     = move;
    app_.playSessionMove(move);
    app_.dropPick();
    app_.dropTypedMove();
    switch (answer) {
        case PuzzleAnswer::Right:
            ++p.step;
            p.hint     = false;
            p.phase    = PuzzlePhase::Reply;
            p.reply_at = Clock::now();
            break;
        case PuzzleAnswer::Solved:
            p.step     = p.play->solution.size();
            p.answered = Clock::now();
            if (!p.retry && !p.practice) puzzleGrade(!p.hinted);
            p.phase = PuzzlePhase::Right;
            break;
        case PuzzleAnswer::Wrong:
            p.answered = Clock::now();
            if (!p.retry && !p.practice) puzzleGrade(false);
            p.phase = PuzzlePhase::Wrong;
            break;
    }
}

void PuzzlesPage::puzzleGrade(bool win) {
    auto& p             = *puzzle_;
    const auto& puzzle  = p.puzzles[p.current];
    p.seconds           = since(p.asked);
    const auto now      = app_.now();
    auto& record        = puzzleRecord();
    const bool scripted = app_.scripted();  // a run for screenshots leaves the profile as it was
    PuzzleResult result;
    result.id     = puzzle.id;
    result.at     = now;
    result.win    = win;
    result.rating = puzzle.rating;
    result.themes = puzzle.themes;
    result.ms     = static_cast<int64_t>(std::min(p.seconds, 600.0) * 1000);
    switch (p.source) {
        case PuzzleRun::Source::Offline: {
            const auto before = puzzleRating();
            const auto after  = ratedAfter(before, puzzle.rating, puzzle.deviation, win, now);
            record.rating     = after;
            p.rating_before   = before.glicko.rating;
            p.rating_after    = after.glicko.rating;
            result.before     = p.rating_before;
            result.after      = p.rating_after;
            if (!scripted) (void)writePuzzleRating(puzzlesFolder(), after);
            break;
        }
        case PuzzleRun::Source::Online:
            result.online = true;
            if (auto* batch = onlineBatch()) {
                batch->answer(puzzle.id, win);
                saveOnlineBatch();
                if (batch->complete()) sendOnlineResults();  // the batch done: its results go in one request
            }
            break;
        case PuzzleRun::Source::Review: {
            result.review = true;
            auto it       = record.cards.find(puzzle.id);
            if (it == record.cards.end()) break;
            // Right Good, after a hint Hard, wrong Again: a puzzle of several moves takes its time, unlike a
            // repertoire's move, so the time does not make it hard.
            const auto rating = !win       ? study::fsrs::Rating::Again
                                : p.hinted ? study::fsrs::Rating::Hard
                                           : study::fsrs::Rating::Good;
            it->second.card   = trainingScheduler().review(it->second.card, rating, now);
            p.next_review     = it->second.card.due;
            // Back within the hour (missed again): asked again a few puzzles later in the series.
            p.next_again = it->second.card.due - now < study::TrainingSession::AGAIN_WITHIN;
            if (p.next_again) {
                const auto at = std::min<std::size_t>(p.queue.size(), 1 + study::TrainingSession::AGAIN_AFTER);
                p.queue.insert(p.queue.begin() + static_cast<std::ptrdiff_t>(at), p.current);
            }
            if (!scripted) (void)writePuzzleCards(puzzlesFolder(), record.cards);
            break;
        }
    }
    // Missed: a card of the day's review, back tomorrow.
    if (!win && p.source != PuzzleRun::Source::Review) {
        record.cards[puzzle.id] = missedCard(puzzle, now, trainingDayEnd(now), trainingScheduler());
        p.next_review           = record.cards[puzzle.id].card.due;
        if (!scripted) (void)writePuzzleCards(puzzlesFolder(), record.cards);
    }
    record.add(result);
    if (!scripted) appendPuzzleResult(puzzlesFolder(), result);
    if (!p.again) {
        p.outcomes.push_back({p.current, win, win ? std::string() : p.played});
        ++p.done;
    }
}

void PuzzlesPage::puzzleDontKnow() {
    auto& p = *puzzle_;
    if (p.phase != PuzzlePhase::Asking || app_.liveGameOn()) return;
    p.played.clear();
    p.answered = Clock::now();
    if (!p.retry && !p.practice) puzzleGrade(false);
    p.shown          = true;
    p.solution_shown = p.step;
    app_.dropTypedMove();
    p.phase = PuzzlePhase::Wrong;
}

void PuzzlesPage::puzzleRetry() {
    auto& p = *puzzle_;
    if (p.phase != PuzzlePhase::Wrong || !p.play) return;
    showPuzzlePosition(p.current, p.step);
    p.phase = PuzzlePhase::Asking;
    p.retry = true;
    p.shown = false;
    p.hint  = false;
    p.played.clear();
    p.asked = Clock::now();
}

void PuzzlesPage::puzzleSolutionStep() {
    auto& p = *puzzle_;
    if ((p.phase != PuzzlePhase::Wrong && p.phase != PuzzlePhase::End) || !p.play) return;
    if (!p.shown) {
        p.shown          = true;
        p.solution_shown = p.step;
    }
    if (p.solution_shown < p.play->solution.size()) ++p.solution_shown;
    showPuzzlePosition(p.current, p.solution_shown);
}

void PuzzlesPage::puzzleNext() {
    auto& p = *puzzle_;
    if (!p.queue.empty()) p.queue.pop_front();
    if (p.queue.empty()) return finishPuzzles(false);
    askPuzzle();
}

void PuzzlesPage::finishPuzzles(bool stopped) {
    auto& p        = *puzzle_;
    p.phase        = PuzzlePhase::End;
    p.stopped      = stopped;
    p.took         = since(p.started);
    p.missed_shown = -1;
    app_.dropPick();
    app_.dropTypedMove();
}

void PuzzlesPage::reviewMissedPuzzles() {
    auto& p = *puzzle_;
    std::deque<std::size_t> missed;
    for (const auto& o : p.outcomes)
        if (!o.win) missed.push_back(o.puzzle);
    if (missed.empty()) return;
    p.queue    = std::move(missed);
    p.practice = true;  // their cards stay as they are: back tomorrow
    p.stopped  = false;
    p.started  = Clock::now();
    askPuzzle();
}

void PuzzlesPage::closePuzzles(bool to_training) {
    if (!puzzle_) return;
    if (to_training) {
        app_.stopReview();
        app_.setEnterStartsPuzzles(false);  // back on the page, Enter reviews again
    }
    BoardTaken taken;
    taken.aside = puzzle_->aside;
    taken.flip  = puzzle_->flip;
    taken.tab   = puzzle_->tab;
    puzzle_.reset();
    app_.giveBoardBack(taken, to_training);  // the series' position is no game to remember
}

void PuzzlesPage::dropPuzzles() {
    BoardTaken taken;
    taken.aside = puzzle_->aside;
    taken.flip  = puzzle_->flip;
    taken.tab   = puzzle_->tab;
    puzzle_.reset();
    app_.stopReview();
    app_.leaveBoard(taken);  // the game that comes takes the board; the one put aside stays among the recent ones
}

// ---------------------------------------------------------------------------
// The Board page during a series

void PuzzlesPage::puzzleKeys() {
    auto& p        = *puzzle_;
    const auto& io = ImGui::GetIO();
    if (!keyboard::available()) return;
    const auto owner = ImGui::GetID("##puzzle_keys");
    for (const auto key : {ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Space, ImGuiKey_Escape, ImGuiKey_LeftArrow,
                           ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Home, ImGuiKey_End})
        ImGui::SetKeyOwner(key, owner);
    const bool plain = !io.KeyCtrl && !io.KeyAlt && !io.KeySuper;
    const bool enter  = plain && keyboard::enterPressed();
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    switch (p.phase) {
        case PuzzlePhase::Asking:
            if (escape) {
                finishPuzzles(true);
            } else if (!app_.liveGameOn()) {
                if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_H)) p.hint = p.hinted = true;
                if (plain && ImGui::IsKeyPressed(ImGuiKey_Space, false))
                    puzzleDontKnow();
                else
                    app_.moveKeys();
            }
            break;
        case PuzzlePhase::Reply:
            if (escape) finishPuzzles(true);
            break;
        case PuzzlePhase::Right:
        case PuzzlePhase::Wrong:
            if (escape) {
                finishPuzzles(true);
            } else if (enter) {
                if (p.phase == PuzzlePhase::Right || p.shown)
                    puzzleNext();
                else
                    puzzleRetry();
            } else if (p.phase == PuzzlePhase::Wrong && plain && ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
                puzzleSolutionStep();
            } else {
                (void)app_.analysisKey();  // Alt+A: the engine, now that the answer is given
            }
            break;
        case PuzzlePhase::End: {
            const bool missed =
                !p.practice && std::any_of(p.outcomes.begin(), p.outcomes.end(), [](const auto& o) { return !o.win; });
            if (app_.reviewRunning()) {
                if (escape)
                    closePuzzles(true);
                else if (enter && !app_.nextReview())
                    closePuzzles(true);
            } else if (escape) {
                closePuzzles(true);
            } else if (enter) {
                missed ? reviewMissedPuzzles() : closePuzzles(true);
            }
            break;
        }
    }
}

void PuzzlesPage::drawPuzzlePage() {
    app_.beginSessionPage();
    puzzleKeys();
    if (!puzzle_) return;  // left by a key
    auto& p = *puzzle_;
    // The opponent's reply, half a second after a right move.
    if (p.phase == PuzzlePhase::Reply && since(p.reply_at) >= REPLY_SECONDS && p.play &&
        p.step < p.play->solution.size()) {
        app_.playSessionMove(p.play->solution[p.step]);
        ++p.step;
        p.phase = PuzzlePhase::Asking;
    }
    // A right answer goes on by itself after a moment, unless the mouse is on it.
    if (p.phase == PuzzlePhase::Right && app_.trainingSettings().advance_ms > 0 && !p.hovered &&
        since(p.answered) * 1000 >= app_.trainingSettings().advance_ms) {
        puzzleNext();
        if (!puzzle_) return;
    }
    app_.drawBoardMessage();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float side   = std::clamp(avail.x * 0.42f, px(340), px(620));
    const float under  = ImGui::GetTextLineHeightWithSpacing() * 4;
    const float size   = std::floor(
        std::max(px(200), std::min(avail.x - side - ImGui::GetStyle().ItemSpacing.x - px(20), avail.y - under)));

    ImGui::BeginGroup();
    // Over the board: the theme and the puzzle's rating, where the series is.
    {
        const auto start = ImGui::GetCursorPosX();
        std::string what = p.title;
        if (p.phase != PuzzlePhase::End && p.current < p.puzzles.size())
            what += " · " + trf("puzzle rated {}", p.puzzles[p.current].rating);
        ImGui::TextDisabled("%s", what.c_str());
        std::string where;
        if (p.phase != PuzzlePhase::End) {
            // The puzzle shown: once answered, it is among those done.
            const bool answered = p.phase == PuzzlePhase::Right || p.phase == PuzzlePhase::Wrong;
            const auto at       = std::clamp<std::size_t>(p.again || answered ? p.done : p.done + 1, 1, p.total);
            where               = trf("{} of {}", at, p.total);
            if (p.again) where += " · " + std::string(TR("asked again"));
        }
        if (!where.empty()) {
            ImGui::SameLine(
                std::max(ImGui::GetCursorPosX() + px(10), start + size - ImGui::CalcTextSize(where.c_str()).x));
            ImGui::TextDisabled("%s", where.c_str());
        }
    }
    drawPuzzleBoard(size);
    // Under it: what was just played, where the puzzle comes from.
    if (p.play && p.phase != PuzzlePhase::End) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size);
        // The opponent's last move: the one before the puzzle, or its last reply.
        const bool black = p.play->before.sideToMove() == chess::Color::BLACK;
        // The last move of the opponent's among those the board shows.
        const auto& game  = app_.sessionGame();
        const auto& moves = game.moves;
        auto just         = numbered(p.play->before, p.play->opponent);
        for (std::size_t i = moves.size(); i-- > 1;) {
            if (moves[i].mover != p.play->before.sideToMove()) continue;
            just = numbered(boardAt(game, false, static_cast<int>(i)), moves[i].move);
            break;
        }
        ImGui::TextDisabled("%s", (black ? trf("Black has just played {} (in pale green). The puzzle's position comes "
                                               "from a game on Lichess.",
                                               just)
                                         : trf("White has just played {} (in pale green). The puzzle's position comes "
                                               "from a game on Lichess.",
                                               just))
                                      .c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, px(18));
    ImGui::BeginChild("##puzzle_side", ImVec2(0, 0));
    drawPuzzleColumn();
    ImGui::EndChild();
}

void PuzzlesPage::drawPuzzleBoard(float size) {
    auto& p          = *puzzle_;
    const auto& game = app_.sessionGame();
    const int ply    = app_.sessionPly();
    std::optional<chess::Move> last_move;
    if (ply > 0) last_move = game.moves[static_cast<std::size_t>(ply - 1)].move;
    BoardDecor marks;  // the squares tinted and the arrows of the puzzle's: the Board adds the rest
    const auto green    = markColour('G', true);
    const auto orange   = IM_COL32(236, 140, 52, 150);
    const bool question = p.play && ply == static_cast<int>(1 + p.step);
    switch (p.phase) {
        case PuzzlePhase::Asking:
            if (p.hint && p.play && p.step < p.play->solution.size())
                marks.tints.push_back({p.play->solution[p.step].from(), markColour('B', true)});
            break;
        case PuzzlePhase::Right:
            if (last_move) {
                marks.tints.push_back({last_move->from(), green});
                marks.tints.push_back({displayedTarget(*last_move), green});
            }
            break;
        case PuzzlePhase::Wrong:
            if (!p.play) break;
            if (p.shown) {  // the solution on the board
                if (last_move && p.solution_shown > p.step) {
                    marks.tints.push_back({last_move->from(), green});
                    marks.tints.push_back({displayedTarget(*last_move), green});
                }
                if (ply == static_cast<int>(1 + p.step) && p.step < p.play->solution.size())
                    marks.arrows.push_back({p.play->solution[p.step], markColour('G', false)});
                break;
            }
            if (!question && last_move) {  // the move played in orange, the solution's arrow in green
                marks.tints.push_back({last_move->from(), orange});
                marks.tints.push_back({displayedTarget(*last_move), orange});
                if (p.step < p.play->solution.size())
                    marks.arrows.push_back({p.play->solution[p.step], markColour('G', false)});
            }
            break;
        case PuzzlePhase::End:
            if (p.play && p.missed_shown >= 0 && p.step < p.play->solution.size() &&
                ply == static_cast<int>(1 + p.step))
                marks.arrows.push_back({p.play->solution[p.step], markColour('G', false)});
            break;
        case PuzzlePhase::Reply:
            break;
    }
    const bool moves = p.phase == PuzzlePhase::Asking && question && !app_.liveGameOn();
    app_.drawSessionBoard(size, marks, moves);
}

void PuzzlesPage::drawPuzzleColumn() {
    auto& p = *puzzle_;
    const ui::TopicRegion topic(guide::topics::Puzzles);
    if (app_.liveGameOn()) {  // fair play (#48): a live game on Lichess stops the series where it is
        app_.drawFairPlayBanner();
        if (ImGui::Button(TR("Stop · Esc"))) finishPuzzles(true);
        return;
    }
    if (p.phase == PuzzlePhase::End) return drawPuzzleEnd();
    if (!p.play) return;
    const auto& puzzle = p.puzzles[p.current];
    const auto& play   = *p.play;

    const float done = p.total ? static_cast<float>(p.done) / static_cast<float>(p.total) : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    ImGui::ProgressBar(done, ImVec2(-1, px(6)), "");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, px(4)));

    const bool white = play.before.sideToMove() == chess::Color::BLACK;  // the solver: the side after the opponent
    const auto at    = [&](std::size_t played) { return boardOf(play, played); };
    const auto first = numbered(at(0), play.solution.front());
    const auto taken = static_cast<int>(std::lround(p.seconds));
    switch (p.phase) {
        case PuzzlePhase::Asking:
        case PuzzlePhase::Reply: {
            beginTinted("##puzzle_question", ui::CARD_BD, SERIES_TINT);
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.2f);
            ImGui::TextUnformatted(p.retry ? (white ? TR("Again, White") : TR("Again, Black"))
                                           : (white ? TR("Your move, White") : TR("Your move, Black")));
            ImGui::PopFont();
            wrapped(TR("Play on the board, or type the move and press Enter."), true);
            app_.drawMoveField();
            if (ImGui::Button(TR("A hint · Alt+H"))) p.hint = p.hinted = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TR("H alone begins a move of the h-pawn: the hint is under Alt."));
            ImGui::SameLine();
            if (ImGui::Button(TR("I don't know · Space"))) puzzleDontKnow();
            ImGui::SameLine();
            if (ImGui::Button(TR("Stop · Esc"))) finishPuzzles(true);
            if (p.phase != PuzzlePhase::Asking && p.phase != PuzzlePhase::Reply) {  // answered by a button
                endTinted();
                return;
            }
            wrapped(p.hinted ? TR("The piece to play is on the square in blue; the puzzle counts as missed.")
                             : TR("The hint shows the piece to play; the puzzle then counts as missed."),
                    true);
            endTinted();
            ImGui::Dummy(ImVec2(0, px(4)));
            wrapped(TR("The engine, the tree and your notes are hidden during the question; the puzzle's themes stay "
                       "hidden until the answer. The opponent's moves are played by RookStudio, the rest of the "
                       "solution."),
                    true);
            break;
        }
        case PuzzlePhase::Right: {
            beginTinted("##puzzle_right", ui::GOOD, SERIES_TINT);
            ui::inlineIcon(ui::Icon::Check, ui::GOOD);
            ImGui::SameLine();
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.2f);
            const auto moves = (play.solution.size() + 1) / 2;
            ImGui::TextUnformatted((p.retry ? trf("{}! Solved, this time", first)
                                            : trn("{}! Solved in {} move, {} s", "{}! Solved in {} moves, {} s", moves,
                                                  first, moves, taken))
                                       .c_str());
            ImGui::PopFont();
            // What it was: the solution in moves.
            {
                std::string line;
                auto board = at(0);
                for (std::size_t i = 0; i < play.solution.size(); ++i) {
                    line +=
                        (line.empty() ? "" : " ") + (i == 0 || board.sideToMove() == chess::Color::WHITE
                                                         ? numbered(board, play.solution[i])
                                                         : localSan(chess::uci::moveToSan(board, play.solution[i])));
                    board.makeMove(play.solution[i]);
                }
                wrapped(trf("The solution: {}.", line));
            }
            std::string after;
            if (p.retry || p.practice)
                after = TR("Played again: it no longer counts.");
            else if (p.source == PuzzleRun::Source::Offline)
                after = p.hinted
                            ? trf("After a hint: missed. Your rating {}", ratingChange(p.rating_before, p.rating_after))
                            : trf("Your rating {}", ratingChange(p.rating_before, p.rating_after));
            else if (p.source == PuzzleRun::Source::Online)
                after = TR("Its result goes to Lichess at the batch's end.");
            else
                after = p.next_again ? std::string(TR("Asked again in this series, then tomorrow."))
                                     : trf("Next review {}.", whenText(p.next_review - app_.now()));
            if (!puzzle.themes.empty()) after += " · " + trf("themes: {}", themesText(puzzle.themes));
            wrapped(after, true);
            if (primaryButton(TR("Next"))) puzzleNext();
            if (puzzle_ && puzzle_->phase == PuzzlePhase::Right) {
                ImGui::SameLine(0, px(8));
                const int advance = app_.trainingSettings().advance_ms;
                ImGui::TextDisabled(
                    "%s", advance > 0 ? trf("Enter, or on its own after {} s", advance / 1000.0).c_str() : TR("Enter"));
                ImGui::SameLine(0, px(10));
                if (ImGui::Button(TR("Analyse here · Alt+A"))) app_.analyse(true);
                if (!puzzle.game.empty()) {
                    ui::sameLineOrWrap(TR("The game on lichess.org"));
                    if (ImGui::SmallButton(TR("The game on lichess.org"))) SDL_OpenURL(puzzle.gameUrl().c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", TR("Opens the game the puzzle comes from in the browser."));
                }
                if (advance > 0 && !p.hovered)
                    ImGui::ProgressBar(static_cast<float>(std::min(1.0, since(p.answered) * 1000 / advance)),
                                       ImVec2(-1, px(3)), "");
            }
            endTinted();
            if (puzzle_)
                puzzle_->hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
                                                        ImGuiHoveredFlags_AllowWhenBlockedByPopup);
            break;
        }
        case PuzzlePhase::Wrong: {
            beginTinted("##puzzle_wrong", ui::WARN, SERIES_TINT);
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.2f);
            const auto question = at(p.step);
            const auto expected = p.step < play.solution.size() ? numbered(question, play.solution[p.step]) : first;
            if (!p.played.empty())
                ImGui::TextUnformatted(trf("{} is not the solution", numbered(question, p.played_move)).c_str());
            else
                ImGui::TextUnformatted(trf("The solution: {}", expected).c_str());
            ImGui::PopFont();
            if (!p.played.empty()) wrapped(trf("The solution: {}, the green arrow.", expected));
            if (!p.retry && !p.practice) {
                std::string what;
                if (p.source == PuzzleRun::Source::Offline)
                    what = trf("Missed: your rating {}. It comes back tomorrow in “To review today”.",
                               ratingChange(p.rating_before, p.rating_after));
                else if (p.source == PuzzleRun::Source::Online)
                    what = TR(
                        "Missed: its result goes to Lichess at the batch's end; it comes back tomorrow in “To review "
                        "today”.");
                else
                    what = p.next_again ? std::string(TR("Asked again in this series, then tomorrow."))
                                        : trf("Next review {}.", whenText(p.next_review - app_.now()));
                wrapped(what, true);
            }
            if (!p.shown) {
                if (primaryButton(TR("Retry · Enter"))) puzzleRetry();
            } else if (primaryButton(TR("Next · Enter"))) {
                puzzleNext();
            }
            if (puzzle_ && puzzle_->phase == PuzzlePhase::Wrong) {
                ImGui::SameLine();
                ImGui::BeginDisabled(p.shown && p.solution_shown >= play.solution.size());
                if (ImGui::Button(TR("The solution move by move · →"))) puzzleSolutionStep();
                ImGui::EndDisabled();
                if (!p.shown) {
                    ImGui::SameLine();
                    if (ImGui::Button(TR("Next"))) puzzleNext();
                }
            }
            if (puzzle_ && !puzzle_->retry)
                wrapped(TR("Played again right, it no longer counts: the grade is given at the first move."), true);
            endTinted();
            break;
        }
        default:
            break;
    }
    if (!puzzle_ || puzzle_->phase == PuzzlePhase::End) return;
    // With the answer, the tree and the engine come back.
    if (puzzle_->phase == PuzzlePhase::Right || puzzle_->phase == PuzzlePhase::Wrong) {
        ImGui::Separator();
        app_.drawBoardTabs();
    }
}

void PuzzlesPage::drawPuzzleEnd() {
    auto& p              = *puzzle_;
    const auto& outcomes = p.outcomes;
    std::size_t won      = 0;
    std::vector<const PuzzleRun::Outcome*> missed;
    for (const auto& o : outcomes) {
        if (o.win)
            ++won;
        else
            missed.push_back(&o);
    }
    ui::inlineIcon(ui::Icon::Check, ui::GOOD);
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.35f);
    if (outcomes.empty())
        ImGui::TextUnformatted(p.stopped ? TR("Stopped before the first answer") : TR("No puzzle to solve now"));
    else
        ImGui::TextUnformatted(trn("{} puzzle in {}", "{} puzzles in {}", outcomes.size(),
                                   ui::exactCount(outcomes.size()), ui::formatDuration(p.took))
                                   .c_str());
    ImGui::PopFont();
    if (p.stopped && p.total > outcomes.size() && p.source == PuzzleRun::Source::Online)
        wrapped(trn("Stopped: {} puzzle of the batch waits for the next time.",
                    "Stopped: {} puzzles of the batch wait for the next time.", p.total - outcomes.size(),
                    p.total - outcomes.size()),
                true);
    if (!outcomes.empty()) {
        ImGui::Dummy(ImVec2(0, px(4)));
        bigCount(ui::exactCount(won), TR("right at the first try"));
        ImGui::SameLine(0, px(30));
        bigCount(ui::exactCount(missed.size()),
                 p.source == PuzzleRun::Source::Review ? TR("missed again") : TR("missed, to review tomorrow"));
        if (p.source == PuzzleRun::Source::Offline) {
            const auto now = puzzleRating().glicko.rating;
            ImGui::SameLine(0, px(30));
            bigCount(
                fmt::format("{:+}", static_cast<int>(std::lround(now)) - static_cast<int>(std::lround(p.rating_start))),
                trf("your rating: {}", static_cast<int>(std::lround(now))));
        }
    }
    if (!missed.empty()) {
        ImGui::Dummy(ImVec2(0, px(6)));
        ImGui::TextUnformatted(TR("The missed"));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("· a click shows the position and the solution"));
        ui::beginCard("##puzzles_missed");
        for (const auto* o : missed) {
            const auto& puzzle = p.puzzles[o->puzzle];
            const auto play    = puzzlePlay(puzzle);
            if (!play) continue;
            const auto question = boardOf(*play, 0);
            const auto label    = fmt::format("{} · {}", mainTheme(puzzle.themes, p.title), puzzle.rating);
            std::string yours   = localSan(o->played);
            try {
                if (const auto m = chess::uci::parseSan(question, o->played); m != chess::Move::NO_MOVE)
                    yours = numbered(question, m);
            } catch (const std::exception&) {
            }
            const auto said =
                o->played.empty()
                    ? trf("I don't know · the solution: {}", numbered(question, play->solution.front()))
                    : trf("you: {} · the solution: {}", yours, numbered(question, play->solution.front()));
            ImGui::PushID(static_cast<int>(o->puzzle));
            if (ImGui::Selectable(label.c_str(), p.missed_shown == static_cast<int>(o->puzzle))) {
                p.missed_shown = static_cast<int>(o->puzzle);
                p.current      = o->puzzle;
                p.play         = play;
                p.step         = 0;
                showPuzzlePosition(p.current, 0);
                app_.setFlip(play->before.sideToMove() == chess::Color::WHITE);
            }
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(10),
                                     ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(said.c_str()).x));
            ImGui::TextDisabled("%s", said.c_str());
            ImGui::PopID();
        }
        ui::endCard();
    }
    // Online: where the batch's results are.
    if (p.source == PuzzleRun::Source::Online) {
        ImGui::Dummy(ImVec2(0, px(4)));
        const auto* batch = onlineBatch();
        if (online_send_.valid())
            wrapped(TR("Sending the batch's answers to Lichess…"), true);
        else if (!online_error_.empty())
            wrapped(trf("Lichess has not taken the batch's answers yet ({}): they are kept, and sent again by "
                        "themselves.",
                        online_error_),
                    true);
        else if (batch && batch->sent_status == "accepted" && batch->results.empty() && batch->sent_at > 0)
            wrapped(trf("Lichess took your {} answers; the next batch is here: {} puzzles.", batch->sent_count,
                        batch->puzzles.size()),
                    true);
        else if (batch && !batch->complete())
            wrapped(trn("{} puzzle of the batch left: its answers go to Lichess once it is done.",
                        "{} puzzles of the batch left: their answers go to Lichess once it is done.", batch->left(),
                        batch->left()),
                    true);
    }
    ImGui::Dummy(ImVec2(0, px(6)));
    if (app_.reviewRunning()) {  // the day's review: the next kind, or back to the Training page
        app_.drawReviewNext(false);
        return;
    }
    if (!missed.empty() && !p.practice) {
        if (primaryButton(trn("Review {} now", "Review the {} now", missed.size(), missed.size()).c_str()))
            return reviewMissedPuzzles();
        ImGui::SameLine();
        if (ImGui::Button(TR("Back to the Training page"))) return closePuzzles(true);
        ImGui::SameLine(0, px(8));
        ImGui::TextDisabled("%s", TR("Enter: review them · Esc: back"));
    } else {
        if (primaryButton(TR("Back to the Training page"))) return closePuzzles(true);
        ImGui::SameLine(0, px(8));
        ImGui::TextDisabled("%s", TR("Enter"));
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    wrapped(p.source == PuzzleRun::Source::Online
                ? TR("A series stopped midway keeps its answers; the batch's results go to Lichess at its end.")
                : TR("A series stopped midway keeps its answers. The missed ones come back in “To review today” "
                     "(FSRS): tomorrow, then further apart when they are right."),
            true);
}

// ---------------------------------------------------------------------------
// The Training page

TrainingKind::Part PuzzlesPage::part() const {
    return {TrainingPart::Puzzles, TR("Puzzles"), "puzzles", guide::topics::Puzzles};
}

void PuzzlesPage::drawTodayCards(TodayCards& cards) {
    const auto state = storeState();
    cards.card("##training_kind_puzzles", ui::Icon::Puzzle, TR("Puzzles"),
               TR("Lichess's, by theme and at your rating, offline or online."),
               state.ready ? trf("{} on the disk · your rating {}", ui::exactCount(state.puzzles),
                                 static_cast<int>(std::lround(puzzleRating().glicko.rating)))
                           : std::string(TR("not on this disk yet")));
}

void PuzzlesPage::drawPart() {
    bool enter = keyboard::available() && keyboard::enterPressed();
    if (enter) {
        const auto owner = ImGui::GetID("##training_enter");
        ImGui::SetKeyOwner(ImGuiKey_Enter, owner);
        ImGui::SetKeyOwner(ImGuiKey_KeypadEnter, owner);
    }
    wrapped(
        TR("Lichess's puzzles: its whole database on your disk, solved without a network, with a RookStudio rating; "
           "or batches of your Lichess account, whose results go back to it."),
        true);
    ImGui::Dummy(ImVec2(0, px(2)));
    drawChooser(enter);
    // The database's own card once it is there (Update, Remove them): before, the choice of puzzles holds it.
    if (offline_puzzles_ && offline_puzzles_->state().ready) {
        ImGui::Dummy(ImVec2(0, px(4)));
        drawPuzzleStoreCard();
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    drawPuzzleThemes();
}

void PuzzlesPage::drawChooser(bool& enter) {
    const ui::TopicRegion topic(puzzles_online_ ? guide::topics::PuzzlesOnline : guide::topics::Puzzles);
    // The linked account's puzzle rating, as the Home page reads it: kept a day, read again when ten minutes old.
    app_.askLichessProfile();
    const auto state  = offline_puzzles_ ? offline_puzzles_->state() : OfflinePuzzles::State{};
    const auto store  = offline_puzzles_ ? offline_puzzles_->store() : nullptr;
    const auto rating = puzzleRating();
    const auto lock   = puzzleLock();
    ui::beginCard("##puzzle_chooser");
    ImGui::PushTextWrapPos();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextUnformatted(TR("Puzzles"));
    ImGui::PopFont();
    ImGui::SameLine();
    {
        std::string line = trf("· your rating {} (RookStudio)", static_cast<int>(std::lround(rating.glicko.rating)));
        if (rating.games == 0) line += " " + std::string(TR("to begin with"));
        if (const auto lichess = lichessPuzzleRating()) line += " · " + trf("Lichess {}", *lichess);
        ImGui::TextDisabled("%s", line.c_str());
        tip(
            trf("Glicko-2, as Lichess rates puzzles: a puzzle solved at the first try, without a hint, is won against "
                "its own rating. It starts from your Lichess puzzle rating when your account is linked, else at 1500; "
                "results of offline puzzles cannot go back to Lichess. {} puzzles rated so far.",
                rating.games));
    }

    // Where they come from.
    ImGui::Spacing();
    const auto source = [&](bool online, const char* name, const std::string& text) {
        ImGui::BeginGroup();
        if (ImGui::RadioButton(fmt::format("{}##puzzles_source_{}", name, online ? 1 : 0).c_str(),
                               puzzles_online_ == online)) {
            puzzles_online_ = online;
            app_.setEnterStartsPuzzles(true);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", text.c_str());
        ImGui::EndGroup();
    };
    source(false, TR("Offline"),
           state.ready ? (state.date.empty()
                              ? trf("Lichess's database, on the disk: {} puzzles (CC0).", ui::exactCount(state.puzzles))
                              : trf("Lichess's database, on the disk: {} puzzles (CC0), of {}.",
                                    ui::exactCount(state.puzzles), state.date))
                       : std::string(TR("Lichess's database (CC0), on your disk once downloaded.")));
    source(true, TR("Online"),
           TR("Your Lichess puzzles, never seen, at your Lichess rating; your answers go back to it."));
    ImGui::Spacing();

    // The theme: chips, then every theme and opening.
    const auto rate = [&](std::string_view key) -> std::string {
        const auto it = puzzleRecord().themes.find(std::string(key));
        if (it == puzzleRecord().themes.end() || it->second.tried < 5) return {};
        return fmt::format(" {} %", 100 * it->second.won / it->second.tried);
    };
    const auto choose = [&](std::string key) {
        puzzles_theme_ = std::move(key);
        app_.setEnterStartsPuzzles(true);
    };
    const bool batch_under_way = puzzles_online_ && onlineBatch() && onlineBatch()->left() > 0 &&
                                 onlineBatch()->left() < onlineBatch()->puzzles.size();
    // What the user's games say (#81): the kinds of their mistakes of the last month ("To work on" of their reviews),
    // as puzzle themes, come first.
    std::vector<std::pair<std::string, std::size_t>> said;
    {
        const auto month = packedDaysBefore(app_.now(), 30);
        std::map<std::string, std::size_t> kinds;
        for (const auto& [kind, n] : app_.mistakeKinds(month))
            if (!mistakeKindTheme(kind).empty()) kinds[kind] = n;
        said.assign(kinds.begin(), kinds.end());
        std::stable_sort(said.begin(), said.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        if (said.size() > 3) said.resize(3);
    }
    if (chip(TR("Random"), puzzles_theme_.empty(), "theme_mix")) choose({});
    std::vector<std::string_view> chips(std::begin(FIRST_THEMES), std::end(FIRST_THEMES));
    for (auto it = said.rbegin(); it != said.rend(); ++it) {
        const auto theme = mistakeKindTheme(it->first);
        std::erase(chips, theme);
        chips.insert(chips.begin(), theme);
    }
    if (!puzzles_theme_.empty() && std::find(chips.begin(), chips.end(), puzzles_theme_) == chips.end())
        chips.insert(chips.begin(), puzzles_theme_);
    for (const auto key : chips) {
        std::string name = key.rfind(OPENING_PREFIX, 0) == 0 ? puzzleOpeningName(key.substr(OPENING_PREFIX.size()))
                                                             : puzzleThemeName(key);
        name += rate(key);
        ui::sameLineOrWrap(name.c_str());
        if (chip(name.c_str(), puzzles_theme_ == key, std::string(key).c_str())) choose(std::string(key));
    }
    ui::sameLineOrWrap(TR("All themes…"));
    if (ImGui::Button(TR("All themes…"))) {
        puzzles_themes_open_ = true;
        puzzles_filter_.fill(0);
        ImGui::OpenPopup("##puzzle_themes");
    }
    if (ImGui::BeginPopup("##puzzle_themes")) {
        ImGui::SetNextItemWidth(px(320));
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##puzzle_filter", TR("A theme or an opening"), puzzles_filter_.data(),
                                 puzzles_filter_.size());
        const std::string filter = puzzles_filter_.data();
        const auto matches       = [&](const std::string& name) {
            if (filter.empty()) return true;
            return lower(name).find(lower(filter)) != std::string::npos;
        };
        ImGui::BeginChild("##puzzle_theme_list", ImVec2(px(420), px(360)));
        ImGui::TextDisabled("%s", TR("Themes"));
        for (const auto key : puzzleThemesShown()) {
            const auto name = puzzleThemeName(key);
            if (!matches(name)) continue;
            std::string count;
            if (store)
                if (const auto list = store->themeList(key)) count = ui::exactCount(store->themes()[*list].puzzles);
            if (ImGui::Selectable(fmt::format("{}{}##{}", name, rate(key), key).c_str(), puzzles_theme_ == key)) {
                choose(std::string(key));
                ImGui::CloseCurrentPopup();
            }
            if (!count.empty()) {
                ImGui::SameLine(px(300));
                ImGui::TextDisabled("%s", count.c_str());
            }
        }
        if (store && !store->openings().empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", TR("Openings (puzzles before move 20)"));
            for (const auto& o : store->openings()) {
                const auto name = puzzleOpeningName(o.name);
                if (!matches(name)) continue;
                const auto key = std::string(OPENING_PREFIX) + o.name;
                if (ImGui::Selectable(fmt::format("{}##{}", name, key).c_str(), puzzles_theme_ == key)) {
                    choose(key);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(px(300));
                ImGui::TextDisabled("%s", ui::exactCount(o.puzzles).c_str());
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    if (!said.empty()) {
        std::string list;
        for (const auto& [kind, n] : said)
            list += (list.empty() ? "" : ", ") + fmt::format("{} {}", std::string(TR(mistakeKindName(kind))), n);
        ImGui::TextDisabled("%s",
                            trf("Your games say: {} this month (“To work on” of their reviews). These themes come "
                                "first.",
                                list)
                                .c_str());
    }
    if (batch_under_way)
        ImGui::TextDisabled(
            "%s", trf("The batch under way is of “{}”: the next one takes the theme chosen.",
                      onlineBatch()->angle == "mix" ? std::string(TR("Random")) : puzzleThemeName(onlineBatch()->angle))
                      .c_str());

    // The difficulty, then what starts.
    ImGui::Spacing();
    for (const auto d : {PuzzleDifficulty::Easier, PuzzleDifficulty::Normal, PuzzleDifficulty::Harder}) {
        const char* label = d == PuzzleDifficulty::Easier   ? TR("Easier")
                            : d == PuzzleDifficulty::Normal ? TR("Your rating")
                                                            : TR("Harder");
        if (chip(label, puzzles_difficulty_ == d, difficultyKey(d))) {
            puzzles_difficulty_ = d;
            app_.setEnterStartsPuzzles(true);
        }
        ImGui::SameLine(0, px(4));
    }
    ImGui::SameLine(0, px(12));
    if (!puzzles_online_) {
        if (state.ready) {
            const auto label = trn("Start · {} puzzle", "Start · {} puzzles", PUZZLE_SERIES, PUZZLE_SERIES);
            if (primaryButton(label.c_str(), lock.empty()) || (enter && lock.empty())) {
                enter = false;
                startPuzzles();
            }
            tip(lock.empty() ? std::string(TR("On the Board page, its column becoming the puzzle's; never a puzzle "
                                              "you saw before."))
                             : lock);
            ImGui::SameLine(0, px(8));
            ImGui::TextDisabled("%s", TR("Enter · Ctrl+K “puzzles fork”"));
        } else {
            ImGui::NewLine();
            drawPuzzleStoreCard();
        }
        ImGui::TextDisabled("%s", TR("What leaves the machine: nothing, once the database is downloaded."));
    } else {
        ImGui::NewLine();
        drawOnlinePuzzles(enter);
    }
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void PuzzlesPage::drawOnlinePuzzles(bool& enter) {
    const auto* account = app_.lichessAccount();
    if (!account) {
        wrapped(TR("Puzzles online come from your Lichess account: link it first. Offline puzzles need no account."),
                true);
        if (ImGui::Button(TR("Link the Lichess account…"))) app_.startLichessLink({}, {});
        return;
    }
    if (!puzzleScopes()) {
        beginTinted("##puzzle_scopes", ui::ACCENT, PAGE_TINT);
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(TR("The first time: Lichess asks for two more permissions"));
        wrapped(TR("Reading your puzzles (puzzle:read), for puzzles at your rating that you never saw; writing your "
                   "results (puzzle:write). Lichess's page opens; your account stays linked meanwhile, and the token "
                   "it replaces is revoked."),
                true);
        if (app_.lichessLinking()) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s",
                               TR("Waiting for Lichess: allow RookStudio in the browser's page, then come back here."));
        } else {
            if (app_.lichessLinkError().failed())
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                                   lichess::gaveNothing(app_.lichessLinkError()).c_str());
            if (primaryButton(TR("Allow on Lichess…")) || enter) {
                enter = false;
                askPuzzleScopes();
            }
            ImGui::SameLine();
            if (ImGui::Button(TR("Not now##puzzle_scopes"))) puzzles_online_ = false;
        }
        ImGui::PopTextWrapPos();
        endTinted();
        return;
    }
    auto* batch     = onlineBatch();
    const auto lock = puzzleLock();
    {
        const auto rating = lichessPuzzleRating();
        ImGui::TextDisabled("%s", (rating ? trf("{} · Lichess puzzle rating {}", account->username, *rating)
                                          : std::string(account->username))
                                      .c_str());
    }
    // The batch: how far, solved offline too; at its end, the results sent in one request.
    if (batch && !batch->puzzles.empty()) {
        const auto done = batch->results.size();
        const auto right =
            std::count_if(batch->results.begin(), batch->results.end(), [](const auto& r) { return r.win; });
        const auto when = [](int64_t t) {
            char text[16] = "";
            const auto tt = static_cast<std::time_t>(t);
            const auto tm = localTime(tt);
            std::strftime(text, sizeof text, "%H:%M", &tm);
            return std::string(text);
        };
        ImGui::TextUnformatted(TR("The batch under way"));
        ImGui::SameLine();
        const char* difficulty = batch->difficulty == "easier"   ? TR("Easier")
                                 : batch->difficulty == "harder" ? TR("Harder")
                                                                 : TR("Your rating");
        ImGui::TextDisabled("%s", trf("· {} · {} · received at {}",
                                      batch->angle == "mix" ? std::string(TR("Random")) : puzzleThemeName(batch->angle),
                                      difficulty, when(batch->received))
                                      .c_str());
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
        ImGui::ProgressBar(static_cast<float>(done) / static_cast<float>(batch->puzzles.size()), ImVec2(-1, px(6)), "");
        ImGui::PopStyleColor();
        ImGui::TextUnformatted(trf("{} done of {} · {} right · it is on the disk: it is solved offline too.", done,
                                   batch->puzzles.size(), right)
                                   .c_str());
        wrapped(trf("At the batch's end, your {} answers go to Lichess in one request, which brings the next batch; "
                    "your Lichess rating moves then.",
                    batch->puzzles.size()),
                true);
        if (ImGui::RadioButton(TR("Rated"), batch->rated)) {
            batch->rated = true;
            saveOnlineBatch();
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(TR("Unrated, to try"), !batch->rated)) {
            batch->rated = false;
            saveOnlineBatch();
        }
        ImGui::SameLine(0, px(12));
        if (batch->left() > 0) {
            const auto label = trn("Resume · {} left", "Resume · {} left", batch->left(), batch->left());
            if (primaryButton(label.c_str(), lock.empty()) || (enter && lock.empty())) {
                enter = false;
                startPuzzles();
            }
            if (!lock.empty()) tip(lock);
        } else if (online_send_.valid()) {
            ImGui::TextDisabled("%s", trf("Sending your {} answers to Lichess…", done).c_str());
        } else {
            if (primaryButton(TR("Send them now")) || enter) {
                enter = false;
                sendOnlineResults();
            }
        }
    } else if (online_ask_.valid()) {
        ImGui::TextDisabled("%s", TR("Asking Lichess for a batch…"));
    } else {
        const auto label =
            trn("Get a batch · {} puzzle", "Get a batch · {} puzzles", lichess::PUZZLE_BATCH, lichess::PUZZLE_BATCH);
        if (primaryButton(label.c_str(), lock.empty()) || (enter && lock.empty())) {
            enter               = false;
            online_start_after_ = true;
            askOnlineBatch();
        }
        tip(lock.empty() ? std::string(TR("Fifty puzzles you never saw, at your Lichess rating, of the theme chosen; "
                                          "kept on the disk until their results are sent."))
                         : lock);
    }
    // The last 30 days on Lichess, by theme: asked once a session.
    if (!online_dashboard_asked_) {
        online_dashboard_asked_ = true;
        online_dashboard_job_ =
            std::async(std::launch::async, [token = account->token, stop = app_.jobs().cancelFlag()] {
                return lichess::puzzleDashboard(token, 30, stop.get());
            });
    }
    if (isReady(online_dashboard_job_)) {
        if (auto answer = online_dashboard_job_.get(); answer.value) online_dashboard_ = std::move(*answer.value);
    }
    if (online_dashboard_ && online_dashboard_->nb > 0) {
        ImGui::Spacing();
        ImGui::TextUnformatted(TR("Your last 30 days on Lichess"));
        ImGui::SameLine();
        ImGui::TextDisabled("%s",
                            trf("· {} puzzles, {} % right at the first try · by theme, read with puzzle:read",
                                online_dashboard_->nb, 100 * online_dashboard_->first_wins / online_dashboard_->nb)
                                .c_str());
        int shown = 0;
        for (const auto& t : online_dashboard_->themes) {
            // The motifs and mates, not the phases or the lengths that every puzzle has.
            if (t.nb == 0 || t.key == "middlegame" || t.key == "endgame" || t.key == "opening" || t.key == "short" ||
                t.key == "long" || t.key == "veryLong" || t.key == "oneMove" || t.key == "advantage" ||
                t.key == "crushing" || t.key == "equality" || t.key == "mate")
                continue;
            if (shown > 0) ImGui::SameLine(0, px(18));
            ImGui::BeginGroup();
            ImGui::TextUnformatted(puzzleThemeName(t.key).c_str());
            ImGui::TextDisabled("%s", trf("{} % · {} done", 100 * t.first_wins / t.nb, t.nb).c_str());
            ImGui::EndGroup();
            if (++shown == 4) break;
        }
    }
    if (batch && batch->sent_at > 0) {
        const auto tt = static_cast<std::time_t>(batch->sent_at);
        const auto tm = localTime(tt);
        char text[32];
        std::strftime(text, sizeof text, "%H:%M", &tm);
        ImGui::TextDisabled("%s", (batch->sent_status == "accepted"
                                       ? trf("The last results sent: {}, {} answers, taken.", text, batch->sent_count)
                                       : trf("The last results sent: {}, {} answers.", text, batch->sent_count))
                                      .c_str());
    }
    // What went wrong, and what is kept.
    if (!online_error_.empty()) {
        beginTinted("##puzzle_online_error", ui::WARN, PAGE_TINT);
        ImGui::PushTextWrapPos();
        ui::inlineIcon(ui::Icon::Alert, ui::WARN);
        ImGui::SameLine();
        const bool kept = batch && !batch->results.empty();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", online_error_.c_str());
        if (online_status_ == 401 || online_status_ == 403) {
            wrapped(TR("Your answers are kept. Link the account again (Lichess page), then they are sent."), true);
        } else if (kept) {
            wrapped(online_status_ == 429
                        ? TR("Too many requests: sent again by itself in a minute; your answers are kept, even if "
                             "RookStudio is closed meanwhile.")
                        : TR("Your answers are kept, and sent again by themselves in a minute."),
                    true);
        }
        if (ImGui::SmallButton(TR("Try again now"))) {
            online_retry_ = {};
            online_error_.clear();
            if (batch && batch->complete())
                sendOnlineResults();
            else
                askOnlineBatch();
        }
        ImGui::PopTextWrapPos();
        endTinted();
    }
    ImGui::TextDisabled("%s", TR("What leaves the machine: the batch asked for (its theme, its difficulty), then each "
                                 "puzzle's id with right or wrong, rated or not; your last 30 days are asked once a "
                                 "session."));
}

void PuzzlesPage::drawPuzzleStoreCard() {
    if (!offline_puzzles_) return;
    const ui::TopicRegion topic(guide::topics::Puzzles);
    auto& puzzles    = *offline_puzzles_;
    const auto state = puzzles.state();
    const ui::Card card("##puzzle_store");
    const ui::TextWrap wrap;
    ui::inlineIcon(ui::Icon::Download, ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextUnformatted(TR("Lichess's puzzles, offline"));
    ImGui::PopFont();
    ImGui::SameLine();
    ui::guideLink("puzzles_help", guide::topics::Puzzles);
    // During: the progress, Cancel.
    if (puzzles.busy()) {
        const auto& p    = puzzles.progress();
        const auto size  = p.size.load();
        const auto bytes = p.bytes.load();
        if (p.writing) {
            const auto total = p.total.load(), written = p.written.load();
            ImGui::TextUnformatted(
                trf("Sorting them by rating: {} puzzles of {}", ui::exactCount(written), ui::exactCount(total))
                    .c_str());
            ImGui::ProgressBar(
                total ? static_cast<float>(static_cast<double>(written) / static_cast<double>(total)) : 0.0f,
                ImVec2(-1, px(8)), "");
        } else {
            if (offline_puzzles_from_ == 0 && bytes > 0) offline_puzzles_from_ = bytes;
            const double seconds = std::chrono::duration<double>(Clock::now() - offline_puzzles_since_).count();
            const double rate    = seconds > 3 && bytes > offline_puzzles_from_
                                       ? static_cast<double>(bytes - offline_puzzles_from_) / seconds
                                       : 0.0;
            ImGui::TextUnformatted((size ? trf("{} read of {} · {} puzzles kept", formatBytes(bytes), formatBytes(size),
                                               ui::exactCount(p.kept))
                                    : bytes
                                        ? trf("{} read · {} puzzles kept", formatBytes(bytes), ui::exactCount(p.kept))
                                        : std::string(TR("Connecting…")))
                                       .c_str());
            if (size) {
                const int percent = static_cast<int>(100.0 * static_cast<double>(bytes) / static_cast<double>(size));
                ImGui::SameLine();
                ImGui::TextDisabled(
                    "%s", rate > 0 ? trf("{} % · about {} left", percent,
                                         ui::formatDuration(static_cast<double>(size - std::min(size, bytes)) / rate))
                                         .c_str()
                                   : fmt::format("{} %", percent).c_str());
            }
            ImGui::ProgressBar(size ? static_cast<float>(static_cast<double>(bytes) / static_cast<double>(size)) : 0.0f,
                               ImVec2(-1, px(8)), "");
        }
        ImGui::TextDisabled("%s",
                            TR("Read while it downloads. Cut short (Cancel, the network, the program closed), the "
                               "download goes on from where it stopped."));
        if (ImGui::Button(TR("Cancel##offline_puzzles"))) puzzles.cancel();
        return;
    }
    // Ready: what is kept, Update, Remove them.
    if (state.ready) {
        ui::inlineIcon(ui::Icon::Check, ui::GOOD);
        ImGui::SameLine();
        std::string what = trf("{} puzzles · {}", ui::exactCount(state.puzzles), formatBytes(state.bytes));
        if (!state.date.empty()) what += " · " + trf("the file of {}", state.date);
        ImGui::TextUnformatted(what.c_str());
        ImGui::TextDisabled("%s", TR("Lichess publishes the file again each month: Update reads the new one, the "
                                     "puzzles kept answering meanwhile. Your rating, results and puzzles to review "
                                     "are kept apart: removing the database leaves them."));
        if (state.stopped) {
            ImGui::TextUnformatted(
                trf("An update stopped at {} of {}.", formatBytes(state.stopped_at), formatBytes(state.size)).c_str());
            if (!offline_puzzles_error_.empty()) {
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", offline_puzzles_error_.c_str());
                ui::pathTooltip(offline_puzzles_failure_.path);  // the file, out of the sentence (#141)
            }
            if (ImGui::Button(TR("Go on##offline_puzzles_update"))) startOffline(state.source);
            ImGui::SameLine();
            if (ImGui::Button(TR("Forget it##offline_puzzles"))) {
                puzzles.forgetStopped();
                offline_puzzles_error_.clear();
                offline_puzzles_failure_ = {};
            }
        } else if (offline_puzzles_remove_) {
            ImGui::TextUnformatted(trf("Remove Lichess's puzzles kept offline? It frees {}; having them again means "
                                       "downloading the file again.",
                                       formatBytes(state.bytes))
                                       .c_str());
            if (ImGui::Button(TR("Remove the puzzles##offline_puzzles_yes"))) {
                if (puzzle_ && puzzle_->source == PuzzleRun::Source::Offline) closePuzzles(true);
                puzzles.remove();
                offline_puzzles_remove_ = false;
                offline_puzzles_note_.clear();
            }
            ImGui::SameLine();
            if (ImGui::Button(TR("Keep the puzzles##offline_puzzles")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                offline_puzzles_remove_ = false;
        } else {
            ImGui::BeginDisabled(offline_puzzles_update_.valid());
            if (ImGui::Button(TR("Update##offline_puzzles"))) {
                offline_puzzles_note_.clear();
                offline_puzzles_update_ = std::async(std::launch::async, [stop = app_.jobs().cancelFlag()] {
                    return evalsSize(lichess::puzzlesUrl(), stop.get());
                });
            }
            ImGui::EndDisabled();
            tip(
                TR("Asks database.lichess.org for its file's date; a newer file is read while it downloads, the "
                   "puzzles kept answering until it is ready."));
            ImGui::SameLine();
            if (ImGui::Button(trf("Remove the puzzles · frees {}##offline_puzzles", formatBytes(state.bytes)).c_str()))
                offline_puzzles_remove_ = true;
            if (offline_puzzles_update_.valid()) ImGui::TextDisabled("%s", TR("Asking database.lichess.org…"));
            if (!offline_puzzles_note_.empty()) ImGui::TextDisabled("%s", offline_puzzles_note_.c_str());
        }
        return;
    }
    // A preparation cut short.
    if (state.stopped) {
        ImGui::TextUnformatted(
            (state.size ? trf("The download stopped at {} of {}: {} puzzles read so far.",
                              formatBytes(state.stopped_at), formatBytes(state.size), ui::exactCount(state.kept))
                        : trf("The download stopped at {}: {} puzzles read so far.", formatBytes(state.stopped_at),
                              ui::exactCount(state.kept)))
                .c_str());
        if (!offline_puzzles_error_.empty()) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", offline_puzzles_error_.c_str());
            ui::pathTooltip(offline_puzzles_failure_.path);  // the file, out of the sentence (#141)
        }
        if (ImGui::Button(TR("Go on##offline_puzzles"))) startOffline(state.source);
        tip(TR("Reads the rest of the file, from where it stopped."));
        ImGui::SameLine();
        if (ImGui::Button(TR("Start again##offline_puzzles"))) {
            puzzles.forgetStopped();
            offline_puzzles_error_.clear();
            offline_puzzles_failure_ = {};
        }
        return;
    }
    // Before: the file, its size, the disk.
    const auto free = freeSpace(puzzles.folder());
    wrapped(
        trf("Lichess's puzzle database (database.lichess.org, CC0): {} puzzles in November 2026, with their themes, "
            "ratings and openings. Downloaded once, on your click, and kept on your disk, it gives puzzles without "
            "a network.",
            ui::exactCount(PUZZLES_COUNT)),
        true);
    std::string disk = trf("The file: about {}, read while it downloads, never kept · on the disk: about {}",
                           formatBytes(PUZZLES_FILE_BYTES), formatBytes(PUZZLES_STORE_BYTES));
    if (free > 0) disk += " · " + trf("{} free", formatBytes(free));
    ImGui::TextDisabled("%s", disk.c_str());
    if (free > 0 && free < PUZZLES_STORE_BYTES * 2)
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           trf("This disk lacks room: free at least {} in {}.",
                               formatBytes(PUZZLES_STORE_BYTES * 2 - free), puzzles.folder().parent_path().string())
                               .c_str());
    if (!offline_puzzles_error_.empty()) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", offline_puzzles_error_.c_str());
        ui::pathTooltip(offline_puzzles_failure_.path);  // the file, out of the sentence (#141)
    }
    if (primaryButton(trf("Download · {}", formatBytes(PUZZLES_FILE_BYTES)).c_str()))
        startOffline(lichess::puzzlesUrl());
    tip(
        TR("Downloads Lichess's puzzle database and keeps it while it arrives; a download cut short goes on from where "
           "it stopped."));
    ImGui::SameLine();
    if (ImGui::Button(TR("A file already downloaded…##offline_puzzles"))) app_.choosePuzzleFile();
    tip(TR("lichess_db_puzzle.csv.zst, from database.lichess.org: read from the disk, nothing is downloaded."));
    ImGui::TextDisabled("%s", TR("What leaves the machine: the request for the file to database.lichess.org, its size "
                                 "said before. Then nothing."));
}

void PuzzlesPage::drawPuzzleThemes() {
    const auto& r = puzzleRecord();
    const ui::Card card("##puzzle_results");
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextUnformatted(TR("Your results by theme"));
    ImGui::PopFont();
    if (r.themes.empty()) {
        ImGui::TextDisabled("%s", TR("None yet: each puzzle answered counts for its themes."));
        return;
    }
    std::vector<std::pair<std::string, ThemeResults>> rows(r.themes.begin(), r.themes.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second.tried > b.second.tried; });
    if (ImGui::BeginTable("##puzzle_theme_table", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn(TR("Theme"));
        ImGui::TableSetupColumn(TR("Puzzles"), ImGuiTableColumnFlags_WidthFixed, px(90));
        ImGui::TableSetupColumn(TR("Right at the first try"), ImGuiTableColumnFlags_WidthFixed, px(170));
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < rows.size() && i < 16; ++i) {
            const auto& [key, t] = rows[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(puzzleThemeName(key).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(ui::exactCount(static_cast<uint64_t>(t.tried)).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(fmt::format("{} %", t.tried ? 100 * t.won / t.tried : 0).c_str());
        }
        ImGui::EndTable();
    }
}

}  // namespace fcg
