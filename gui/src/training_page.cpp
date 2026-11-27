// The Training page (#80, mockup M12, boards 1 to 3), a module of its own out of App (#107): its Today part (the day's
// review in one queue, the kinds of exercises, what the puzzles offer to start) and each kind's part, drawn by the
// kind (training_kind.hpp). The day's review (review_queue.hpp): its counts, "Review all" taking the kinds one after
// the other, the Home page's count and its "In progress". The training's settings (training.json). The page offered
// once on the Home page of a view made before it.
//
// The keys: Enter reviews what is due, or starts the puzzles once one of their choices was clicked; Ctrl+K has the
// page, "Review all" and the kinds' commands.

#include "training_page.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>  // SetKeyOwner

#include "guide.hpp"
#include "i18n.hpp"
#include "keyboard.hpp"
#include "training.hpp"
#include "ui.hpp"
#include "views.hpp"
#include "widgets.hpp"

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

using ui::beginTinted;
using ui::endTinted;
using ui::mix;
using ui::primaryButton;
using ui::wrapped;

// A count in large figures over what it counts.
void bigCount(std::size_t n, const std::string& what) {
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.9f);
    ImGui::TextUnformatted(ui::exactCount(n).c_str());
    ImGui::PopFont();
    ImGui::TextDisabled("%s", what.c_str());
    ImGui::EndGroup();
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

// The Today part's cards, three to a row, each a third of the part wide: a click on one shows its kind's part.
class Cards final : public TodayCards {
   public:
    Cards(TrainingKind*& shown, float width, float gap) : shown_(shown), width_(width), gap_(gap) {}

    void of(TrainingKind* kind) { kind_ = kind; }

    void card(const char* id, ui::Icon icon, const char* name, const char* line, const std::string& stat,
              const std::function<void()>& then) override {
        if (drawn_ % 3 != 0)
            ImGui::SameLine(0, gap_);
        else if (drawn_ > 0)  // the next row
            ImGui::Dummy(ImVec2(0, gap_ - ImGui::GetStyle().ItemSpacing.y));
        ++drawn_;
        ui::beginCard(id, ImVec2(width_, 0));
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
                                   ImVec2(std::max(1.0f, width_ - px(32)), std::max(1.0f, end.y - start.y)))) {
            shown_ = kind_;
            if (then) then();
        }
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ui::endCard();
    }

   private:
    TrainingKind*& shown_;
    TrainingKind* kind_ = nullptr;
    float width_, gap_;
    int drawn_ = 0;
};

}  // namespace

TrainingPage::TrainingPage(TrainingPageContext& app, std::vector<TrainingKind*> kinds)
    : app_(app), kinds_(std::move(kinds)) {}

TrainingPage::~TrainingPage() = default;

// ---------------------------------------------------------------------------
// The kinds

TrainingKind* TrainingPage::kindOf(ReviewKind kind) const {
    for (auto* k : kinds_)
        if (k->reviewKind() == kind) return k;
    return nullptr;
}

std::vector<TrainingKind*> TrainingPage::parts() const {
    std::vector<TrainingKind*> out;
    for (auto* k : kinds_)
        if (k->part().place > 0) out.push_back(k);
    std::stable_sort(out.begin(), out.end(), [](auto* a, auto* b) { return a->part().place < b->part().place; });
    return out;
}

BoardSession* TrainingPage::boardSession() const {
    for (auto* k : kinds_)
        if (k->onBoard()) return k;
    return nullptr;
}

void TrainingPage::closeSessionForAnother() {
    const bool chained = review_running_;  // the day's review goes on (#80)
    if (auto* session = boardSession()) session->closeSession(false);
    review_running_ = chained;
}

std::string TrainingPage::lock() const {
    if (app_.liveGameOn()) return TR("Locked while one of your games is played on Lichess.");
    if (app_.boardGamePlayed()) return TR("Once the game played on the board is over.");
    return {};
}

// ---------------------------------------------------------------------------
// The training's settings

const TrainingSettings& TrainingPage::trainingSettings() {
    if (!settings_) settings_ = std::make_unique<TrainingSettings>(readTrainingSettings(app_.profileDir()));
    return *settings_;
}

void TrainingPage::setTrainingSettings(const TrainingSettings& settings) {
    (void)trainingSettings();  // read first, as a setting changed always was
    *settings_ = settings;
    saveSettings();
}

void TrainingPage::saveSettings() {
    if (app_.scripted()) return;  // a run for screenshots leaves the profile as it was
    writeTrainingSettings(app_.profileDir(), *settings_);
}

// ---------------------------------------------------------------------------
// The part shown

void TrainingPage::showTrainingPage(int part) {
    shown_ = nullptr;
    for (auto* k : kinds_)
        if (part > 0 && k->part().place == part) shown_ = k;
    app_.toTrainingPage();
}

bool TrainingPage::trainingShown() const { return app_.trainingPageShown(); }

const char* TrainingPage::guideTopic() const { return shown_ ? shown_->part().topic : guide::topics::TrainingPage; }

// ---------------------------------------------------------------------------
// The day's review

ReviewDay TrainingPage::reviewDay() {
    ReviewDay out;
    for (const auto kind : reviewKinds()) {
        auto* k = kindOf(kind);
        if (!k) continue;
        auto count = k->reviewCount();
        count.kind = kind;
        if (count.due + count.tomorrow + count.three_days > 0) out.kinds.push_back(count);
    }
    return out;
}

void TrainingPage::startReview(std::optional<ReviewKind> only) {
    if (!lock().empty()) return;
    enter_puzzles_ = false;  // back on the page, Enter reviews again
    review_chain_.clear();
    for (const auto kind : reviewKinds()) {
        if (only && kind != *only) continue;
        if (auto* k = kindOf(kind))
            for (const auto step : k->reviewSteps()) review_chain_.push_back({k, step});
    }
    review_running_ = !review_chain_.empty();
    if (!nextReview()) review_running_ = false;
}

bool TrainingPage::nextReview() {
    while (!review_chain_.empty()) {
        const auto step = review_chain_.front();
        review_chain_.pop_front();
        review_running_ = true;
        if (step.kind->startReviewStep(step.step)) return true;
    }
    return false;
}

void TrainingPage::stopReview() {
    review_running_ = false;
    review_chain_.clear();
}

std::string TrainingPage::reviewNextLabel() {
    if (review_chain_.empty()) return {};
    const auto& step = review_chain_.front();
    auto label       = step.kind->reviewNextLabel(step.step);
    return label.empty() ? std::string(TR("Next")) : label;
}

void TrainingPage::drawReviewNext(bool enter) {
    const auto back = [this] {
        if (auto* session = boardSession()) session->closeSession(true);
        review_running_ = false;
        review_chain_.clear();
        app_.toTrainingPage();
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

// ---------------------------------------------------------------------------
// The Home page

bool TrainingPage::due() {
    if (boardSession()) return true;  // a training session holds the Board (#76, #80–#82)
    return reviewDay().due() > 0;     // the day's review, every kind (#80)
}

void TrainingPage::drawHome() {
    // The session under way, whatever its kind (one at a time holds the Board): where it is, the way back to it.
    for (auto* k : kinds_) k->drawHomeLine();
    // The day's review (#80): one count for every kind, a click reviewing them all.
    drawReviewHome();
}

void TrainingPage::drawReviewHome() {
    const auto day = reviewDay();
    if (day.due() == 0) return;
    const ui::TopicRegion topic(guide::topics::DayReview);
    const auto label = trn("{} to review today", "{} to review today", day.due(), ui::exactCount(day.due())) + " · " +
                       TR("Review all");
    if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(ImGui::CalcTextSize(label.c_str()).x, 0))) startReview();
    if (ImGui::IsItemHovered()) {
        std::string parts;
        for (const auto& c : day.kinds) {
            if (c.due == 0) continue;
            const auto* k = kindOf(c.kind);
            parts += (parts.empty() ? "" : " · ") + ui::exactCount(c.due) + " " + (k ? k->dueWords(c.due) : "");
        }
        const auto lock = this->lock();
        ImGui::SetTooltip(
            "%s", (lock.empty() ? trf("{}: one after the other, on the Board page, about {} min.", parts, day.minutes())
                                : lock)
                      .c_str());
    }
}

// ---------------------------------------------------------------------------
// The page offered once

bool TrainingPage::offered() const {
    if (app_.viewsHold(views::PageId::Training)) return true;
    const auto offered = app_.pagesOffered();
    return std::find(offered.begin(), offered.end(), "training") != offered.end();
}

void TrainingPage::answerOffer(bool add) {
    // After the Openings when the view holds them, else before Lichess, else before Settings.
    if (add && app_.hasViews())
        app_.addToView(views::PageId::Training, {views::PageId::Openings},
                       {views::PageId::Lichess, views::PageId::Settings});
    auto offered = app_.pagesOffered();
    if (std::find(offered.begin(), offered.end(), "training") == offered.end()) offered.emplace_back("training");
    if (!app_.scripted()) app_.setPagesOffered(offered);
    offer_ = 1;
}

void TrainingPage::drawOffer() {
    if (offer_ < 0) offer_ = offered() ? 1 : 0;
    if (offer_ != 0 || !app_.hasViews() || app_.viewHolds(views::PageId::Training)) return;
    beginTinted("##training_offer", ui::ACCENT);
    ui::inlineIcon(ui::Icon::Cap, ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.05f);
    ImGui::TextUnformatted(TR("New: the Training page"));
    ImGui::PopFont();
    wrapped(TR("The day's review in one place, and Lichess's puzzles, offline or online. Your view does not change "
               "unless you say so."),
            true);
    if (primaryButton(trf("Add it to {}", app_.viewName()).c_str())) answerOffer(true);
    ImGui::SameLine();
    if (ImGui::Button(TR("Not now##training_offer"))) answerOffer(false);
    ImGui::SameLine(0, px(10));
    ImGui::TextDisabled("%s", TR("Ctrl+K “Training” leads there meanwhile."));
    endTinted();
    ImGui::Dummy(ImVec2(0, px(6)));
}

// ---------------------------------------------------------------------------
// The palette, the focus words

void TrainingPage::addCommands(const PaletteAdd& add, const std::string& training_view) {
    // The page, the day's review, then each kind's with its part's, in the parts' order (#80–#82).
    add(TR("Training"), training_view, [this] { showTrainingPage(0); });
    if (const auto day = reviewDay(); day.due() > 0 && lock().empty())
        add(trn("Review all · {} to review today", "Review all · {} to review today", day.due(),
                ui::exactCount(day.due())),
            TR("Training"), [this] { startReview(); });
    for (auto* k : parts()) k->addCommands(add, training_view);
}

void TrainingPage::addFocusWords(FocusWords& words, const std::string& word) {
    words.on({"review-all"}, [&] {  // the day's review, every kind one after the other (#80)
        startReview();
    });
    // The page at its Today part, or at a kind's part: training-puzzles, training-mistakes, training-guess…
    words.on({"training-page"}, [&] { showTrainingPage(0); });
    for (auto* k : parts()) {
        const auto part    = k->part();
        const auto pattern = std::string("training-") + part.word;
        words.on({std::string_view(pattern)}, [&] { showTrainingPage(part.place); });
    }
    // The kinds' own: their sessions, the puzzles' database, the day's review of one kind…
    for (auto* k : kinds_) k->addFocusWords(words, word);
}

// ---------------------------------------------------------------------------
// The page

void TrainingPage::draw() {
    ui::pageTitle(TR("Training"), guideTopic());
    ImGui::SameLine(0, px(14));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("·");
    ImGui::SameLine(0, px(8));
    if (partLink(TR("Today"), shown_ == nullptr)) shown_ = nullptr;
    for (auto* k : parts()) {
        ImGui::SameLine(0, px(8));
        ImGui::TextDisabled("·");
        ImGui::SameLine(0, px(8));
        if (partLink(k->part().name, shown_ == k)) shown_ = k;
    }
    ImGui::BeginChild("##training_page", ImVec2(0, 0), ImGuiChildFlags_None);
    if (shown_)
        shown_->drawPart();
    else
        drawToday();
    ImGui::EndChild();
}

void TrainingPage::drawToday() {
    wrapped(TR("What comes back today, then the exercises by kind. Everything works offline; what goes to Lichess is "
               "said where you choose it."),
            true);
    ImGui::Dummy(ImVec2(0, px(2)));
    // Enter: the review, or the puzzles once one of their choices was clicked.
    bool enter = keyboard::available() && keyboard::enterPressed();
    if (enter) {
        const auto owner = ImGui::GetID("##training_enter");
        ImGui::SetKeyOwner(ImGuiKey_Enter, owner);
        ImGui::SetKeyOwner(ImGuiKey_KeypadEnter, owner);
    }
    bool review_enter = enter && !enter_puzzles_;
    drawReviewCard(review_enter);
    ImGui::Dummy(ImVec2(0, px(4)));

    // The kinds of exercises, a card each (the endgames' mates one of their own), three to a row: a click on one leads
    // to its part.
    {
        const float gap   = px(10);
        const float width = (ImGui::GetContentRegionAvail().x - 2 * gap) / 3.0f;
        Cards cards(shown_, width, gap);
        for (auto* k : parts()) {
            cards.of(k);
            k->drawTodayCards(cards);
        }
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    // What a kind offers to start under the cards (the puzzles' choice): Enter starts it once one of its choices was
    // clicked, or when nothing is due.
    bool choice_enter = enter && enter_puzzles_;
    if (enter && !enter_puzzles_ && reviewDay().due() == 0) choice_enter = true;
    for (auto* k : parts()) k->drawTodayChoice(choice_enter);
}

void TrainingPage::drawReviewCard(bool& enter) {
    const ui::TopicRegion topic(guide::topics::DayReview);
    const auto day  = reviewDay();
    const auto lock = this->lock();
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
        for (const auto& c : day.kinds) {
            if (c.due == 0) continue;
            if (!first) ImGui::SameLine(0, px(30));
            first         = false;
            const auto* k = kindOf(c.kind);
            bigCount(c.due, k ? k->dueWords(c.due) : std::string());
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
        // A kind's cards alone ("Only my mistakes", "Only the puzzles"), when another kind has cards due too.
        for (const auto kind : reviewKinds()) {
            const auto* k     = kindOf(kind);
            const char* only  = k ? k->reviewOnlyLabel() : nullptr;
            const auto* count = day.find(kind);
            if (!only || !count || count->due == 0 || day.dueKinds().size() <= 1) continue;
            ImGui::SameLine(0, px(14));
            ImGui::BeginDisabled(!lock.empty());
            if (ImGui::Button(only)) startReview(kind);
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
