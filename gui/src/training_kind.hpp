#pragma once

// A kind of training (#76, #80–#82, #107): the repertoires' training, the puzzles, the user's mistakes as exercises,
// guessing the moves of a game, the endgames against the tables and their mates, board vision. Each is a module of its
// own and a session of the Board's (board_session.hpp), and plugs into the Training page (training_page.hpp) through
// this interface, whatever its kind: its part of the page and its cards on the Today part, its line on the Home page
// while its session holds the Board, its share of the day's review (review_queue.hpp: its counts, the steps "Review
// all" takes for it, the "Next: …" label of each), its commands (Ctrl+K) and --focus words. The page takes the kinds
// App gives it and names none of them: a kind that has no part, or no cards in the day's review, keeps the defaults.

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "app_context.hpp"  // PaletteAdd
#include "board_session.hpp"
#include "focus_words.hpp"
#include "review_queue.hpp"

namespace fcg {

namespace ui {
enum class Icon;
}

// The Today part's cards, laid out by the Training page three to a row: a kind draws its own through it, an icon, its
// name, what it is, a figure of the user's. A click on one shows the kind's part, then does `then` when it is given
// (the endgames' "Mates": their part brought to the mates).
class TodayCards {
   public:
    virtual ~TodayCards() = default;

    virtual void card(const char* id, ui::Icon icon, const char* name, const char* line, const std::string& stat,
                      const std::function<void()>& then = {}) = 0;
};

class TrainingKind : public BoardSession {
   public:
    // Its session holds the Board.
    [[nodiscard]] virtual bool onBoard() const = 0;
    // The Home page's "In progress" while its session holds the Board: where it is, the way back to it.
    virtual void drawHomeLine() = 0;
    // Its --focus words, tried where App's table has the training's (app_chess.cpp). `word` is the word `words` was
    // made with.
    virtual void addFocusWords(FocusWords& words, const std::string& word) = 0;

    // Its part of the Training page, for a kind that has one (the repertoires' training is the Openings page's): its
    // place among the parts (Today: none), its name in the page's title line, the word that shows it (--focus
    // training-<word>), the guide's section (F1, the title's `?`).
    struct Part {
        TrainingPart place = TrainingPart::Today;
        const char* name  = nullptr;
        const char* word  = nullptr;
        const char* topic = nullptr;
    };
    [[nodiscard]] virtual Part part() const { return {}; }
    // The part drawn; its cards on the Today part; what it offers to start under them (the puzzles' choice; `enter`:
    // Enter starts it, taken when it did); its entries of the command palette among the Training page's, the part's
    // in the view `training_view` (the repertoires' training adds its own among the Openings page's).
    virtual void drawPart() {}
    virtual void drawTodayCards(TodayCards&) {}
    virtual void drawTodayChoice(bool&) {}
    virtual void addCommands(const PaletteAdd&, const std::string&) {}

    // Its share of the day's review, for a kind whose cards come back (guessing and board vision have none): its place
    // in the queue (the order "Review all" takes the kinds in); its cards due today and after; the steps "Review all"
    // takes for it, one when cards are due (the repertoires' training: one a repertoire with positions due); a step's
    // session of its cards due (false when none can start); the label of a step that comes next ("Next: 4 missed
    // puzzles"; empty: "Next"); its cards due as the Today part and the Home page count them ("missed puzzles"); the
    // button that reviews them alone ("Only the puzzles"; none: no such button).
    [[nodiscard]] virtual std::optional<ReviewKind> reviewKind() const { return std::nullopt; }
    [[nodiscard]] virtual ReviewCount reviewCount() { return {}; }
    [[nodiscard]] virtual std::vector<std::size_t> reviewSteps() {
        return reviewCount().due > 0 ? std::vector<std::size_t>{0} : std::vector<std::size_t>{};
    }
    virtual bool startReviewStep(std::size_t) { return false; }
    [[nodiscard]] virtual std::string reviewNextLabel(std::size_t) { return {}; }
    [[nodiscard]] virtual std::string dueWords(std::size_t) const { return {}; }
    [[nodiscard]] virtual const char* reviewOnlyLabel() const { return nullptr; }
};

}  // namespace fcg
