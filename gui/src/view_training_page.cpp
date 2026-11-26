// The Training page (#80, mockup M12, board 1): its Today part (the day's review in one queue, the kinds of exercises,
// the puzzles to start) and its Puzzles part, the puzzles module's (Lichess's database offline, the batch online, the
// results by theme; puzzles_page.hpp, #107). The day's review (review_queue.hpp): its counts, "Review all" taking the
// kinds one after the other, the Home page's count. The page offered once on the Home page of a view made before it.
//
// The keys: Enter reviews what is due, or starts the puzzles once one of their choices was clicked; Ctrl+K has the
// page, "Review all", each theme's puzzles and the database's commands.

#include <algorithm>
#include <cmath>

#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>  // SetKeyOwner

#include "app.hpp"
#include "board_session.hpp"
#include "guess_page.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "puzzles_page.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

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
        case ReviewKind::Puzzles:  // their module's (#107)
            out = puzzles_->reviewCount();
            break;
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
            return puzzles_->startReview();
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
        if (auto* session = boardSession()) session->closeSession(true);
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
    puzzles_->drawHomeLine();  // a series of puzzles under way (#80)
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
        puzzles_->drawPart();
    else if (training_tab_ == 2)
        drawTrainingMistakes();
    else if (training_tab_ == 3)
        guessing_->drawPart();
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
        const auto state = puzzles_->storeState();
        card("##training_kind_puzzles", ui::Icon::Puzzle, TR("Puzzles"),
             TR("Lichess's, by theme and at your rating, offline or online."),
             state.ready ? trf("{} on the disk · your rating {}", ui::exactCount(state.puzzles),
                               static_cast<int>(std::lround(puzzles_->puzzleRating().glicko.rating)))
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
        const auto& last = guessing_->lastRecord();
        card("##training_kind_guess", ui::Icon::Eye, TR("Guess the moves"),
             TR("A game of a database, a side, move by move; the engine scores."),
             last ? trf("the last: {} points of {}", last->points, last->max) : std::string(TR("not tried yet")), 3);
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
            const auto& mates = puzzles_->puzzleRecord().themes;
            const auto it     = mates.find("mateIn2");
            const auto stat   = it != mates.end() && it->second.tried >= 5
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
    puzzles_->drawChooser(puzzles_enter);
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

}  // namespace fcg
