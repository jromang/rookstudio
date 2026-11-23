// The Training page (#80, mockup M12, board 1): its Today part (the day's review in one queue, the kinds of exercises,
// the puzzles to start) and its Puzzles part (Lichess's database offline, its preparation, Update and Remove; the
// batch online; the results by theme). The day's review (review_queue.hpp): its counts, "Review all" taking the kinds
// one after the other, the Home page's count. The page offered once on the Home page of a view made before it.
//
// The keys: Enter reviews what is due, or starts the puzzles once one of their choices was clicked; Ctrl+K has the
// page, "Review all", each theme's puzzles and the database's commands.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>

#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>  // SetKeyOwner

#include "app.hpp"
#include "console.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "lichess_months.hpp"
#include "machines.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

using Clock = std::chrono::steady_clock;

// A tooltip for the item just drawn, also when it is disabled, its lines wrapped.
void tip(const std::string& text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

ImU32 mix(ImU32 a, ImU32 b, float t) {
    const auto fa = ImGui::ColorConvertU32ToFloat4(a), fb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(fa.x + (fb.x - fa.x) * t, fa.y + (fb.y - fa.y) * t, fa.z + (fb.z - fa.z) * t, 1.0f));
}

// A card tinted: the review's accent, a warning's.
void beginTinted(const char* id, ImU32 tone) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BG, tone, 0.08f)));
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
void bigCount(std::size_t n, const std::string& what) {
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.9f);
    ImGui::TextUnformatted(ui::exactCount(n).c_str());
    ImGui::PopFont();
    ImGui::TextDisabled("%s", what.c_str());
    ImGui::EndGroup();
}

void wrapped(const std::string& text, bool grey = false) {
    if (grey) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    if (grey) ImGui::PopStyleColor();
}

// "· Today · Puzzles": the part shown underlined.
bool partLink(const char* label, bool shown) {
    const auto at   = ImGui::GetCursorScreenPos();
    const auto size = ImGui::CalcTextSize(label);
    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("##part", ImVec2(size.x, ImGui::GetFrameHeight()));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    auto* dl      = ImGui::GetWindowDrawList();
    const float y = at.y + (ImGui::GetFrameHeight() - size.y) * 0.5f;
    dl->AddText(ImVec2(at.x, y), ImGui::GetColorU32(shown || hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled), label);
    if (shown)
        dl->AddLine(ImVec2(at.x, y + size.y + px(2)), ImVec2(at.x + size.x, y + size.y + px(2)), ui::ACCENT, px(2));
    if (hovered && !shown) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return clicked && !shown;
}

// A chip: a theme, a difficulty; filled when chosen.
bool chip(const char* label, bool chosen, const char* id = nullptr) {
    const auto& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(12));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), style.FramePadding.y * 0.8f));
    if (chosen) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BG, ui::ACCENT, 0.30f)));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::CARD_BG));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(ui::CARD_BD));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    const bool clicked = ImGui::Button(id ? fmt::format("{}##{}", label, id).c_str() : label);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    return clicked;
}

// The space the profile's disk has left; 0 when it cannot be said.
uint64_t freeSpace(const fs::path& folder) {
    std::error_code ec;
    const auto info = fs::space(fs::exists(folder, ec) ? folder : folder.parent_path(), ec);
    return ec ? 0 : info.available;
}

// What a kind of the day's review calls its cards due.
std::string kindDue(ReviewKind kind, std::size_t n) {
    switch (kind) {
        case ReviewKind::Mistakes:
            return trn("mistake of your games", "mistakes of your games", n);
        case ReviewKind::Repertoire:
            return trn("repertoire position", "repertoire positions", n);
        case ReviewKind::Puzzles:
            return trn("missed puzzle", "missed puzzles", n);
        case ReviewKind::Endgames:
            return trn("missed endgame", "missed endgames", n);
    }
    return {};
}

// The themes offered first on the page, as chips: the motifs most asked for, then a mate and an ending.
constexpr std::string_view FIRST_THEMES[] = {"fork",        "pin",        "discoveredAttack", "mateIn2",
                                             "rookEndgame", "deflection", "hangingPiece",     "sacrifice"};

constexpr std::string_view OPENING_PREFIX = "opening:";

}  // namespace

// ---------------------------------------------------------------------------
// The day's review

ReviewCount App::reviewCount(ReviewKind kind) {
    ReviewCount out;
    out.kind       = kind;
    const auto day = trainingDay(epochNow());
    switch (kind) {
        case ReviewKind::Repertoire: {
            int64_t ms = 0, weight = 0;
            for (std::size_t i = 0; i < repertoires().entries().size(); ++i) {
                const auto& s = trainingSummary(i);
                out.due += s.counts.due;
                out.tomorrow += s.counts.tomorrow;
                out.three_days += s.counts.three_days;
                if (s.kept.answer_ms > 0) {
                    ms += s.kept.answer_ms * static_cast<int64_t>(std::max<std::size_t>(1, s.counts.due));
                    weight += static_cast<int64_t>(std::max<std::size_t>(1, s.counts.due));
                }
            }
            out.answer_ms = weight > 0 ? ms / weight : 0;
            break;
        }
        case ReviewKind::Puzzles: {
            const auto& r  = puzzleRecord();
            out.due        = cardsDue(r.cards, day.end);
            out.tomorrow   = cardsDue(r.cards, day.end + study::fsrs::DAY) - out.due;
            out.three_days = cardsDue(r.cards, day.end + 3 * study::fsrs::DAY) - out.due;
            // The pace of the last answers.
            int64_t ms = 0, n = 0;
            for (auto it = r.results.rbegin(); it != r.results.rend() && n < 50; ++it, ++n)
                ms += std::min<int64_t>(it->ms, 120000);
            out.answer_ms = n > 0 ? ms / n : 0;
            break;
        }
        case ReviewKind::Mistakes: {  // #81
            const auto c   = mistakeDayCounts();
            out.due        = c.due;
            out.tomorrow   = c.tomorrow;
            out.three_days = c.three_days;
            out.answer_ms  = mistakeRecord().answer_ms;
            break;
        }
        case ReviewKind::Endgames: {  // #82
            const auto& r  = endgameRecord();
            out.due        = endgameCardsDue(r, day.end);
            out.tomorrow   = endgameCardsDue(r, day.end + study::fsrs::DAY) - out.due;
            out.three_days = endgameCardsDue(r, day.end + 3 * study::fsrs::DAY) - out.due;
            out.answer_ms  = r.answer_ms;
            break;
        }
    }
    return out;
}

ReviewDay App::reviewDay() {
    ReviewDay out;
    for (const auto kind : reviewKinds()) {
        auto count = reviewCount(kind);
        if (count.due + count.tomorrow + count.three_days > 0) out.kinds.push_back(count);
    }
    return out;
}

bool App::startReviewOf(ReviewKind kind, std::size_t repertoire) {
    switch (kind) {
        case ReviewKind::Repertoire:
            if (repertoire >= repertoires().entries().size() || trainingSummary(repertoire).counts.due == 0)
                return false;
            startTraining(repertoire, TrainingRun::Kind::Review);
            return training_ != nullptr;
        case ReviewKind::Puzzles:
            return startPuzzleReview();
        case ReviewKind::Mistakes:
            return startMistakes(true);
        case ReviewKind::Endgames:
            return startEndgameReview();
    }
    return false;
}

void App::startReview(std::optional<ReviewKind> only) {
    if (!trainingLock().empty()) return;
    training_enter_puzzles_ = false;  // back on the page, Enter reviews again
    review_chain_.clear();
    for (const auto kind : reviewKinds()) {
        if (only && kind != *only) continue;
        if (kind == ReviewKind::Repertoire) {
            for (std::size_t i = 0; i < repertoires().entries().size(); ++i)
                if (trainingSummary(i).counts.due > 0) review_chain_.push_back({kind, i});
        } else if (reviewCount(kind).due > 0) {
            review_chain_.push_back({kind, 0});
        }
    }
    review_running_ = !review_chain_.empty();
    if (!nextReview()) review_running_ = false;
}

bool App::nextReview() {
    while (!review_chain_.empty()) {
        const auto step = review_chain_.front();
        review_chain_.pop_front();
        review_running_ = true;
        if (startReviewOf(step.kind, step.repertoire)) return true;
    }
    return false;
}

std::string App::reviewNextLabel() {
    if (review_chain_.empty()) return {};
    const auto& step = review_chain_.front();
    switch (step.kind) {
        case ReviewKind::Repertoire:
            if (step.repertoire < repertoires().entries().size()) {
                const auto n = trainingSummary(step.repertoire).counts.due;
                return trn("Next: {} position of “{}”", "Next: {} positions of “{}”", n, ui::exactCount(n),
                           repertoires().entries()[step.repertoire].name);
            }
            break;
        case ReviewKind::Puzzles: {
            const auto n = reviewCount(ReviewKind::Puzzles).due;
            return trn("Next: {} missed puzzle", "Next: {} missed puzzles", n, ui::exactCount(n));
        }
        case ReviewKind::Mistakes: {
            const auto n = reviewCount(ReviewKind::Mistakes).due;
            return trn("Next: {} mistake of your games", "Next: {} mistakes of your games", n, ui::exactCount(n));
        }
        case ReviewKind::Endgames: {
            const auto n = reviewCount(ReviewKind::Endgames).due;
            return trn("Next: {} missed endgame", "Next: {} missed endgames", n, ui::exactCount(n));
        }
    }
    return TR("Next");
}

void App::drawReviewNext(bool enter) {
    const auto back = [this] {
        if (training_) closeTraining(true);
        if (puzzle_) closePuzzles(true);
        if (mistake_) closeMistakes(true);
        if (endgame_) closeEndgame(true);
        review_running_ = false;
        review_chain_.clear();
        page_ = Page::Training;
    };
    if (!review_chain_.empty()) {
        const auto label = reviewNextLabel() + " · " + TR("Enter");
        if (primaryButton(label.c_str()) || enter) {
            if (!nextReview()) back();
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Back to the Training page"))) back();
    } else {
        ImGui::TextDisabled("%s", TR("The day's review is done."));
        if (primaryButton((std::string(TR("Back to the Training page")) + " · " + TR("Enter")).c_str()) || enter)
            back();
    }
}

void App::drawReviewHome() {
    if (puzzle_) {
        const auto& p    = *puzzle_;
        const auto label = p.phase != PuzzleRun::Phase::End
                               ? trf("Puzzles “{}”: {} of {}", p.title, std::min(p.done + 1, p.total), p.total)
                               : trf("Puzzles “{}”", p.title);
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Back to the series"))) page_ = Page::Board;
    }
    const auto day = reviewDay();
    if (day.due() == 0) return;
    const ui::TopicRegion topic(guide::topics::DayReview);
    const auto label = trn("{} to review today", "{} to review today", day.due(), ui::exactCount(day.due())) + " · " +
                       TR("Review all");
    if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(ImGui::CalcTextSize(label.c_str()).x, 0))) startReview();
    if (ImGui::IsItemHovered()) {
        std::string parts;
        for (const auto& k : day.kinds)
            if (k.due > 0) parts += (parts.empty() ? "" : " · ") + ui::exactCount(k.due) + " " + kindDue(k.kind, k.due);
        const auto lock = trainingLock();
        ImGui::SetTooltip(
            "%s", (lock.empty() ? trf("{}: one after the other, on the Board page, about {} min.", parts, day.minutes())
                                : lock)
                      .c_str());
    }
}

// ---------------------------------------------------------------------------
// The page offered once

bool App::trainingOffered() const {
    for (const auto& v : views_)
        if (v.holds(views::PageId::Training)) return true;
    const auto offered = storage_.pagesOffered();
    return std::find(offered.begin(), offered.end(), "training") != offered.end();
}

void App::answerTrainingOffer(bool add) {
    if (add && !views_.empty()) {
        auto& pages = views_[static_cast<std::size_t>(view_)].pages;
        // After the Openings when the view holds them, else before Lichess, else before Settings.
        auto at = std::find(pages.begin(), pages.end(), views::PageId::Openings);
        if (at != pages.end()) {
            ++at;
        } else {
            at = std::find(pages.begin(), pages.end(), views::PageId::Lichess);
            if (at == pages.end()) at = std::find(pages.begin(), pages.end(), views::PageId::Settings);
        }
        pages.insert(at, views::PageId::Training);
        views::tidy(views_[static_cast<std::size_t>(view_)]);
        saveViews();
    }
    auto offered = storage_.pagesOffered();
    if (std::find(offered.begin(), offered.end(), "training") == offered.end()) offered.emplace_back("training");
    if (!options_.scripted) storage_.setPagesOffered(offered);
    training_offer_ = 1;
}

void App::drawTrainingOffer() {
    if (training_offer_ < 0) training_offer_ = trainingOffered() ? 1 : 0;
    if (training_offer_ != 0 || views_.empty() || view().holds(views::PageId::Training)) return;
    beginTinted("##training_offer", ui::ACCENT);
    ui::inlineIcon(ui::Icon::Cap, ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.05f);
    ImGui::TextUnformatted(TR("New: the Training page"));
    ImGui::PopFont();
    wrapped(TR("The day's review in one place, and Lichess's puzzles, offline or online. Your view does not change "
               "unless you say so."),
            true);
    if (primaryButton(trf("Add it to {}", view().name).c_str())) answerTrainingOffer(true);
    ImGui::SameLine();
    if (ImGui::Button(TR("Not now##training_offer"))) answerTrainingOffer(false);
    ImGui::SameLine(0, px(10));
    ImGui::TextDisabled("%s", TR("Ctrl+K “Training” leads there meanwhile."));
    endTinted();
    ImGui::Dummy(ImVec2(0, px(6)));
}

// ---------------------------------------------------------------------------
// The user's puzzles, Lichess's database

PuzzleRecord& App::puzzleRecord() {
    if (!puzzle_record_) puzzle_record_ = readPuzzleRecord(puzzlesFolder());
    return *puzzle_record_;
}

std::optional<int> App::lichessPuzzleRating() const {
    if (online_batch_ && online_batch_->glicko && online_batch_->glicko->rating > 0)
        return static_cast<int>(std::lround(online_batch_->glicko->rating));
    if (lichess_account_ && lichess_profile_ && lichess_profile_->puzzle_rating > 0)
        return lichess_profile_->puzzle_rating;
    return std::nullopt;
}

PuzzleRating App::puzzleRating() {
    auto& r = puzzleRecord();
    if (r.rating) return *r.rating;
    std::optional<int> deviation;
    if (lichess_profile_ && lichess_profile_->puzzle_deviation > 0) deviation = lichess_profile_->puzzle_deviation;
    return startingRating(lichess_account_ ? lichessPuzzleRating() : std::nullopt, deviation);
}

void App::startOfflinePuzzles(std::string source) {
    if (!offline_puzzles_ || offline_puzzles_->busy()) return;
    offline_puzzles_error_.clear();
    offline_puzzles_note_.clear();
    offline_puzzles_since_ = Clock::now();
    offline_puzzles_from_  = 0;
    offline_puzzles_->start(std::move(source));
}

lichess::OnlineBatch* App::onlineBatch() {
    if (!online_batch_loaded_) {
        online_batch_loaded_ = true;
        online_batch_        = lichess::loadOnlineBatch(puzzlesFolder());
    }
    return online_batch_ ? &*online_batch_ : nullptr;
}

void App::saveOnlineBatch() {
    if (options_.scripted || !online_batch_) return;
    if (!lichess::saveOnlineBatch(puzzlesFolder(), *online_batch_))
        Console::instance().add(trf("The puzzles of Lichess's batch could not be kept in {}", puzzlesFolder().string()),
                                true);
}

bool App::puzzleScopes() const {
    return lichess_account_ && lichess_account_->has(lichess::SCOPE_PUZZLE_READ) &&
           lichess_account_->has(lichess::SCOPE_PUZZLE_WRITE);
}

void App::askPuzzleScopes() {
    if (lichess_link_ || !lichess_account_) return;
    auto scopes    = lichess_account_->scopes;
    const auto add = [&](const char* scope) {
        if (std::find(scopes.begin(), scopes.end(), scope) == scopes.end()) scopes.emplace_back(scope);
    };
    add(lichess::SCOPE_PUZZLE_READ);
    add(lichess::SCOPE_PUZZLE_WRITE);
    startLichessLink(scopes, lichess_account_->username);
}

void App::askOnlineBatch() {
    if (online_ask_.valid() || online_send_.valid() || !puzzleScopes()) return;
    online_error_.clear();
    online_status_    = 0;
    std::string angle = puzzles_theme_.empty() ? std::string("mix") : puzzles_theme_;
    if (angle.rfind(OPENING_PREFIX, 0) == 0) angle = angle.substr(OPENING_PREFIX.size());
    online_ask_ = std::async(std::launch::async, [token      = lichess_account_->token, angle,
                                                  difficulty = std::string(difficultyKey(puzzles_difficulty_))] {
        return lichess::puzzleBatch(token, angle, lichess::PUZZLE_BATCH, difficulty);
    });
}

void App::sendOnlineResults() {
    auto* batch = onlineBatch();
    if (!batch || batch->results.empty() || online_send_.valid() || online_ask_.valid() || !puzzleScopes()) return;
    online_error_.clear();
    online_status_ = 0;
    // The next batch in the same answer, unless another theme or difficulty was chosen since: then asked after.
    std::string angle = puzzles_theme_.empty() ? std::string("mix") : puzzles_theme_;
    if (angle.rfind(OPENING_PREFIX, 0) == 0) angle = angle.substr(OPENING_PREFIX.size());
    const std::string difficulty = difficultyKey(puzzles_difficulty_);
    const bool same              = angle == batch->angle && difficulty == batch->difficulty;
    online_send_ = std::async(std::launch::async, [token = lichess_account_->token, angle = batch->angle,
                                                   solutions = batch->results, difficulty = batch->difficulty, same] {
        return lichess::solvePuzzles(token, angle, solutions, same ? lichess::PUZZLE_BATCH : 0, difficulty);
    });
}

void App::pollPuzzles() {
    if (offline_puzzles_) {
        if (offline_puzzles_update_.valid() &&
            offline_puzzles_update_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const auto size  = offline_puzzles_update_.get();
            const auto state = offline_puzzles_->state();
            if (!size.error.empty())
                offline_puzzles_note_ =
                    trf("database.lichess.org does not answer ({}): the puzzles kept stay.", size.error);
            else if (!size.date.empty() && size.date == state.date)
                offline_puzzles_note_ = trf("Already the latest file, of {}.", size.date);
            else
                startOfflinePuzzles(lichess::puzzlesUrl());
        }
        if (const auto why = offline_puzzles_->finished()) {
            const auto state = offline_puzzles_->state();
            if (why->empty()) {
                Console::instance().add(trf("Lichess's puzzles kept offline: {} puzzles, {}",
                                            ui::exactCount(state.puzzles), formatBytes(state.bytes)));
            } else if (*why != "cancelled") {
                offline_puzzles_error_ = *why;
                Console::instance().add(trf("Lichess's puzzles offline: the preparation stopped: {}", *why), true);
            }
        }
    }
    // Online: a batch asked for, results sent.
    const auto ready = [](auto& f) {
        return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    };
    if (ready(online_ask_)) {
        auto answer    = online_ask_.get();
        online_status_ = answer.status;
        if (answer.value && !answer.value->puzzles.empty()) {
            lichess::OnlineBatch b;
            std::string angle = puzzles_theme_.empty() ? std::string("mix") : puzzles_theme_;
            if (angle.rfind(OPENING_PREFIX, 0) == 0) angle = angle.substr(OPENING_PREFIX.size());
            b.angle      = angle;
            b.difficulty = difficultyKey(puzzles_difficulty_);
            b.rated      = onlineBatch() ? onlineBatch()->rated : true;
            b.received   = epochNow();
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
            online_error_ =
                answer.value ? std::string(TR("Lichess has no puzzle of this theme for you now.")) : answer.error;
        }
    }
    if (ready(online_send_)) {
        auto answer    = online_send_.get();
        online_status_ = answer.status;
        auto* batch    = onlineBatch();
        if (answer.value && batch) {
            const auto count = batch->results.size();
            lichess::OnlineBatch b;
            b.angle       = batch->angle;
            b.difficulty  = batch->difficulty;
            b.rated       = batch->rated;
            b.received    = epochNow();
            b.puzzles     = std::move(answer.value->next.puzzles);
            b.glicko      = answer.value->next.glicko ? answer.value->next.glicko : batch->glicko;
            b.sent_at     = epochNow();
            b.sent_count  = static_cast<int>(count);
            b.sent_status = "accepted";
            online_batch_ = std::move(b);
            online_retry_ = {};
            saveOnlineBatch();
            Console::instance().add(trf("Lichess's puzzles: {} answers sent and taken", count));
            // Another theme chosen since: its batch asked for now.
            if (online_batch_->puzzles.empty()) askOnlineBatch();
        } else {
            online_error_ = answer.error;
            // Kept: sent again by itself in a minute, unless Lichess refused the token. The last send that went
            // through stays said as it was.
            if (answer.status != 401 && answer.status != 403) online_retry_ = Clock::now() + std::chrono::seconds(61);
        }
    }
    // A batch done and not taken yet (a 429, a cut, the program closed meanwhile): sent again.
    if (lichess_account_ && puzzleScopes() && !online_ask_.valid() && !online_send_.valid()) {
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
// The page

void App::drawTraining() {
    ui::pageTitle(TR("Training"), guideSection());
    ImGui::SameLine(0, px(14));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("·");
    ImGui::SameLine(0, px(8));
    if (partLink(TR("Today"), training_tab_ == 0)) training_tab_ = 0;
    ImGui::SameLine(0, px(8));
    ImGui::TextDisabled("·");
    ImGui::SameLine(0, px(8));
    if (partLink(TR("Puzzles"), training_tab_ == 1)) training_tab_ = 1;
    ImGui::SameLine(0, px(8));
    ImGui::TextDisabled("·");
    ImGui::SameLine(0, px(8));
    if (partLink(TR("My mistakes"), training_tab_ == 2)) training_tab_ = 2;
    ImGui::SameLine(0, px(8));
    ImGui::TextDisabled("·");
    ImGui::SameLine(0, px(8));
    if (partLink(TR("Guess the moves"), training_tab_ == 3)) training_tab_ = 3;
    ImGui::SameLine(0, px(8));
    ImGui::TextDisabled("·");
    ImGui::SameLine(0, px(8));
    if (partLink(TR("Endgames"), training_tab_ == 4)) training_tab_ = 4;
    ImGui::SameLine(0, px(8));
    ImGui::TextDisabled("·");
    ImGui::SameLine(0, px(8));
    if (partLink(TR("Vision"), training_tab_ == 5)) training_tab_ = 5;
    ImGui::BeginChild("##training_page", ImVec2(0, 0), ImGuiChildFlags_None);
    if (training_tab_ == 1)
        drawTrainingPuzzles();
    else if (training_tab_ == 2)
        drawTrainingMistakes();
    else if (training_tab_ == 3)
        drawTrainingGuess();
    else if (training_tab_ == 4)
        drawTrainingEndgames();
    else if (training_tab_ == 5)
        drawTrainingVision();
    else
        drawTrainingToday();
    ImGui::EndChild();
}

void App::drawTrainingToday() {
    wrapped(TR("What comes back today, then the exercises by kind. Everything works offline; what goes to Lichess is "
               "said where you choose it."),
            true);
    ImGui::Dummy(ImVec2(0, px(2)));
    // Enter: the review, or the puzzles once one of their choices was clicked.
    bool enter = !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
                 (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    if (enter) {
        const auto owner = ImGui::GetID("##training_enter");
        ImGui::SetKeyOwner(ImGuiKey_Enter, owner);
        ImGui::SetKeyOwner(ImGuiKey_KeypadEnter, owner);
    }
    bool review_enter = enter && !training_enter_puzzles_;
    drawReviewCard(review_enter);
    ImGui::Dummy(ImVec2(0, px(4)));

    // The kinds of exercises, a card each: a click on one leads to its part.
    {
        const float gap   = px(10);
        const float width = (ImGui::GetContentRegionAvail().x - 2 * gap) / 3.0f;
        const auto card   = [&](const char* id, ui::Icon icon, const char* name, const char* line,
                                const std::string& stat, int tab) {
            ui::beginCard(id, ImVec2(width, 0));
            const auto start = ImGui::GetCursorScreenPos();
            ui::inlineIcon(icon, ui::ACCENT);
            ImGui::SameLine();
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
            ImGui::TextUnformatted(name);
            ImGui::PopFont();
            wrapped(line, true);
            ImGui::TextDisabled("%s", stat.c_str());
            const auto end = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(start);
            if (ImGui::InvisibleButton(fmt::format("{}_go", id).c_str(),
                                       ImVec2(std::max(1.0f, width - px(32)), std::max(1.0f, end.y - start.y))))
                training_tab_ = tab;
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ui::endCard();
        };
        const auto state = offline_puzzles_ ? offline_puzzles_->state() : OfflinePuzzles::State{};
        card("##training_kind_puzzles", ui::Icon::Puzzle, TR("Puzzles"),
             TR("Lichess's, by theme and at your rating, offline or online."),
             state.ready ? trf("{} on the disk · your rating {}", ui::exactCount(state.puzzles),
                               static_cast<int>(std::lround(puzzleRating().glicko.rating)))
                         : std::string(TR("not on this disk yet")),
             1);
        ImGui::SameLine(0, gap);
        {
            const auto all = mistakeExercises();
            const auto c   = mistakeCounts(all, trainingDay(epochNow()), 0);
            card("##training_kind_mistakes", ui::Icon::Target, TR("My mistakes"),
                 TR("Your reviewed games' mistakes, turned into exercises."),
                 userNames().empty() ? std::string(TR("your names first"))
                 : c.exercises == 0
                     ? std::string(TR("none yet"))
                     : trn("{} exercise", "{} exercises", c.exercises, ui::exactCount(c.exercises)) + " " +
                           trn("from {} game", "from {} games", c.games, c.games) +
                           (c.fresh > 0 ? " · " + trn("{} new", "{} new", c.fresh, c.fresh) : std::string()),
                 2);
        }
        ImGui::SameLine(0, gap);
        if (!guess_last_read_) {
            guess_last_read_ = true;
            guess_last_      = lastGuessRecord(guessFolder());
        }
        card("##training_kind_guess", ui::Icon::Eye, TR("Guess the moves"),
             TR("A game of a database, a side, move by move; the engine scores."),
             guess_last_ ? trf("the last: {} points of {}", guess_last_->points, guess_last_->max)
                         : std::string(TR("not tried yet")),
             3);
        // The second row (#82): the endgames against the tables, the mates, board vision.
        ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
        {
            const auto& all  = endgameCatalogue();
            std::size_t done = 0;
            for (const auto& e : all)
                if (const auto it = endgameRecord().results.find(e.id);
                    it != endgameRecord().results.end() && it->second.done > 0)
                    ++done;
            card("##training_kind_endgames", ui::Icon::Crown, TR("Endgames"),
                 TR("Against the tables: win, or hold the draw."), trf("{} endgames · {} done", all.size(), done), 4);
        }
        ImGui::SameLine(0, gap);
        {
            const auto it    = puzzleRecord().themes.find("mateIn2");
            const auto stat  = it != puzzleRecord().themes.end() && it->second.tried >= 5
                                   ? trf("mate in 2: {} % right", 100 * it->second.won / it->second.tried)
                                   : std::string(TR("mates in 1 to 5, by pattern"));
            const int before = training_tab_;
            card("##training_kind_mates", ui::Icon::Zap, TR("Mates"),
                 TR("Mates in 1 to 5 of the puzzles; Q, R, 2B, B+N against the tables."), stat, 4);
            if (before != 4 && training_tab_ == 4) endgame_scroll_mates_ = true;  // to the part's mates
        }
        ImGui::SameLine(0, gap);
        {
            const auto& v = visionRecord();
            int best      = 0;
            for (const auto& [key, n] : v.coordinates) best = std::max(best, n);
            card("##training_kind_vision", ui::Icon::Grid, TR("Vision"),
                 TR("The coordinates, blindfold, a line read in your head."),
                 best > 0 ? trn("record: {} square in 30 s", "record: {} squares in 30 s", best, best)
                          : std::string(TR("not tried yet")),
                 5);
        }
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    bool puzzles_enter = enter && training_enter_puzzles_;
    if (enter && !training_enter_puzzles_ && reviewDay().due() == 0) puzzles_enter = true;
    drawPuzzleChooser(puzzles_enter);
}

void App::drawTrainingPuzzles() {
    bool enter = !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
                 (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
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
    drawPuzzleChooser(enter);
    // The database's own card once it is there (Update, Remove them): before, the choice of puzzles holds it.
    if (offline_puzzles_ && offline_puzzles_->state().ready) {
        ImGui::Dummy(ImVec2(0, px(4)));
        drawPuzzleStoreCard();
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    drawPuzzleThemes();
}

void App::drawReviewCard(bool& enter) {
    const ui::TopicRegion topic(guide::topics::DayReview);
    const auto day  = reviewDay();
    const auto lock = trainingLock();
    beginTinted("##review_card", ui::ACCENT);
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextUnformatted(TR("To review today"));
    ImGui::PopFont();
    if (day.due() == 0) {
        wrapped(day.kinds.empty()
                    ? TR("Nothing yet: the positions of your repertoires (the Openings page), the mistakes of your "
                         "games and the puzzles and endgames you miss come back here, each on its day.")
                    : TR("Nothing more today."),
                true);
    } else {
        bool first = true;
        for (const auto& k : day.kinds) {
            if (k.due == 0) continue;
            if (!first) ImGui::SameLine(0, px(30));
            first = false;
            bigCount(k.due, kindDue(k.kind, k.due));
        }
        ImGui::Dummy(ImVec2(0, px(2)));
        const auto label = trn("Review all · {}", "Review all · {}", day.due(), ui::exactCount(day.due()));
        if (primaryButton(label.c_str(), lock.empty()) || (enter && lock.empty())) {
            enter = false;
            startReview();
        }
        tip(lock.empty() ? std::string(TR("One kind after the other, on the Board page: each answer graded by itself, "
                                          "each card back on its day."))
                         : lock);
        ImGui::SameLine(0, px(8));
        ImGui::TextDisabled("%s", trf("Enter · about {} min", day.minutes()).c_str());
        if (const auto* mistakes = day.find(ReviewKind::Mistakes);
            mistakes && mistakes->due > 0 && day.dueKinds().size() > 1) {
            ImGui::SameLine(0, px(14));
            ImGui::BeginDisabled(!lock.empty());
            if (ImGui::Button(TR("Only my mistakes"))) startReview(ReviewKind::Mistakes);
            ImGui::EndDisabled();
        }
        if (const auto* puzzles = day.find(ReviewKind::Puzzles);
            puzzles && puzzles->due > 0 && day.dueKinds().size() > 1) {
            ImGui::SameLine(0, px(14));
            ImGui::BeginDisabled(!lock.empty());
            if (ImGui::Button(TR("Only the puzzles"))) startReview(ReviewKind::Puzzles);
            ImGui::EndDisabled();
        }
    }
    if (day.tomorrow() + day.threeDays() > 0)
        wrapped(trf("One queue, the spaced repetition (FSRS) of the Openings' training, each card at its date. Then: "
                    "tomorrow {} · within three days {}.",
                    ui::exactCount(day.tomorrow()), ui::exactCount(day.threeDays())),
                true);
    if (!lock.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::WARN));
        ImGui::TextWrapped("%s", lock.c_str());
        ImGui::PopStyleColor();
    }
    endTinted();
}

void App::drawPuzzleChooser(bool& enter) {
    const ui::TopicRegion topic(puzzles_online_ ? guide::topics::PuzzlesOnline : guide::topics::Puzzles);
    // The linked account's puzzle rating, as the Home page reads it: kept a day, asked once when none is known.
    if (lichess_account_) {
        pollLichessProfile();
        if (!lichess_profile_ && !lichess_profile_job_.valid() &&
            lichess_profile_asked_.time_since_epoch().count() == 0) {
            lichess_profile_asked_ = std::chrono::steady_clock::now();
            lichess_profile_job_   = std::async(
                std::launch::async, [token = lichess_account_->token] { return lichess::accountProfile(token); });
        }
    }
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
            puzzles_online_         = online;
            training_enter_puzzles_ = true;
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
        puzzles_theme_          = std::move(key);
        training_enter_puzzles_ = true;
    };
    const bool batch_under_way = puzzles_online_ && onlineBatch() && onlineBatch()->left() > 0 &&
                                 onlineBatch()->left() < onlineBatch()->puzzles.size();
    // What the user's games say (#81): the kinds of their mistakes of the last month ("To work on" of their reviews),
    // as puzzle themes, come first.
    std::vector<std::pair<std::string, std::size_t>> said;
    {
        const auto month = packedDaysBefore(epochNow(), 30);
        std::map<std::string, std::size_t> kinds;
        for (const auto* e : mistakeExercises())
            if (e->packed_date >= month && !mistakeKindTheme(e->kind).empty()) ++kinds[e->kind];
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
            auto lower = [](std::string s) {
                for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return s;
            };
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
            puzzles_difficulty_     = d;
            training_enter_puzzles_ = true;
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

void App::drawOnlinePuzzles(bool& enter) {
    if (!lichess_account_) {
        wrapped(TR("Puzzles online come from your Lichess account: link it first. Offline puzzles need no account."),
                true);
        if (ImGui::Button(TR("Link the Lichess account…"))) startLichessLink();
        return;
    }
    if (!puzzleScopes()) {
        beginTinted("##puzzle_scopes", ui::ACCENT);
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(TR("The first time: Lichess asks for two more permissions"));
        wrapped(TR("Reading your puzzles (puzzle:read), for puzzles at your rating that you never saw; writing your "
                   "results (puzzle:write). Lichess's page opens; your account stays linked meanwhile, and the token "
                   "it replaces is revoked."),
                true);
        if (lichess_link_) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s",
                               TR("Waiting for Lichess: allow RookStudio in the browser's page, then come back here."));
        } else {
            if (!lichess_error_.empty())
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                                   trf("Lichess gave nothing ({}).", lichess_error_).c_str());
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
        ImGui::TextDisabled("%s", (rating ? trf("{} · Lichess puzzle rating {}", lichess_account_->username, *rating)
                                          : std::string(lichess_account_->username))
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
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &tt);
#else
            localtime_r(&tt, &tm);
#endif
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
        online_dashboard_job_   = std::async(
            std::launch::async, [token = lichess_account_->token] { return lichess::puzzleDashboard(token, 30); });
    }
    if (online_dashboard_job_.valid() &&
        online_dashboard_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
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
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        char text[32];
        std::strftime(text, sizeof text, "%H:%M", &tm);
        ImGui::TextDisabled("%s", (batch->sent_status == "accepted"
                                       ? trf("The last results sent: {}, {} answers, taken.", text, batch->sent_count)
                                       : trf("The last results sent: {}, {} answers.", text, batch->sent_count))
                                      .c_str());
    }
    // What went wrong, and what is kept.
    if (!online_error_.empty()) {
        beginTinted("##puzzle_online_error", ui::WARN);
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

void App::drawPuzzleStoreCard() {
    if (!offline_puzzles_) return;
    const ui::TopicRegion topic(guide::topics::Puzzles);
    auto& puzzles    = *offline_puzzles_;
    const auto state = puzzles.state();
    ui::beginCard("##puzzle_store");
    ImGui::PushTextWrapPos();
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
        ImGui::PopTextWrapPos();
        ui::endCard();
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
            if (!offline_puzzles_error_.empty())
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", offline_puzzles_error_.c_str());
            if (ImGui::Button(TR("Go on##offline_puzzles_update"))) startOfflinePuzzles(state.source);
            ImGui::SameLine();
            if (ImGui::Button(TR("Forget it##offline_puzzles"))) {
                puzzles.forgetStopped();
                offline_puzzles_error_.clear();
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
                offline_puzzles_update_ =
                    std::async(std::launch::async, [] { return evalsSize(lichess::puzzlesUrl()); });
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
        ImGui::PopTextWrapPos();
        ui::endCard();
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
        if (!offline_puzzles_error_.empty())
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", offline_puzzles_error_.c_str());
        if (ImGui::Button(TR("Go on##offline_puzzles"))) startOfflinePuzzles(state.source);
        tip(TR("Reads the rest of the file, from where it stopped."));
        ImGui::SameLine();
        if (ImGui::Button(TR("Start again##offline_puzzles"))) {
            puzzles.forgetStopped();
            offline_puzzles_error_.clear();
        }
        ImGui::PopTextWrapPos();
        ui::endCard();
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
    if (!offline_puzzles_error_.empty())
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", offline_puzzles_error_.c_str());
    if (primaryButton(trf("Download · {}", formatBytes(PUZZLES_FILE_BYTES)).c_str()))
        startOfflinePuzzles(lichess::puzzlesUrl());
    tip(
        TR("Downloads Lichess's puzzle database and keeps it while it arrives; a download cut short goes on from where "
           "it stopped."));
    ImGui::SameLine();
    if (ImGui::Button(TR("A file already downloaded…##offline_puzzles")))
        dialogs_.openFile(Dialogs::Purpose::OfflinePuzzlesFile, {{"zstd", "zst"}, {TR("All files"), "*"}});
    tip(TR("lichess_db_puzzle.csv.zst, from database.lichess.org: read from the disk, nothing is downloaded."));
    ImGui::TextDisabled("%s", TR("What leaves the machine: the request for the file to database.lichess.org, its size "
                                 "said before. Then nothing."));
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void App::drawPuzzleThemes() {
    const auto& r = puzzleRecord();
    ui::beginCard("##puzzle_results");
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextUnformatted(TR("Your results by theme"));
    ImGui::PopFont();
    if (r.themes.empty()) {
        ImGui::TextDisabled("%s", TR("None yet: each puzzle answered counts for its themes."));
        ui::endCard();
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
    ui::endCard();
}

}  // namespace fcg
