#pragma once

// The repertoires' training (#76, mockup M11, board 1), a module of its own out of App (#107; training.hpp is its data,
// repertoires.hpp the repertoires, study/training.hpp and study/fsrs.hpp the cards and their scheduler): each
// repertoire's positions where it is the user's move, its cards, counted for the day with their memories; the cards
// ordered by what the databases play, made in the background; the Openings page's training card and the line of its
// list; a session on the Board page, whose column becomes the training's (a session of the Board's,
// board_session.hpp): the opponent's moves played along the repertoire, the answer graded without a button, the move
// played added to the repertoire from a wrong answer (Ctrl+Z takes it back), the end and the moves missed; the day's
// review of each repertoire's positions due; the Home page's line; its palette commands and --focus words.
//
// What it needs of the program around it, it asks of its context (RepertoireTrainingContext, app_context.hpp): App's,
// or a test's. The repertoires stay App's (the Board's star, the opening report, the Play page and the prints read and
// change them too): context::Openings. The training's settings (training.json) are the Training page's:
// context::Training.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <chess.hpp>
#include <study/repertoire.hpp>
#include <study/training.hpp>

#include "app_context.hpp"
#include "board_session.hpp"
#include "focus_words.hpp"
#include "review_queue.hpp"
#include "training.hpp"

namespace fcg {

class RepertoireTrainingPage final : public BoardSession {
   public:
    explicit RepertoireTrainingPage(RepertoireTrainingContext& app);
    ~RepertoireTrainingPage() override;
    RepertoireTrainingPage(const RepertoireTrainingPage&)            = delete;
    RepertoireTrainingPage& operator=(const RepertoireTrainingPage&) = delete;

    // Once a frame, whatever shows: a session's cards prepared, its first question.
    void poll();
    // The Openings page's parts of the repertoire `index` (view_repertoires.cpp): its training card (what is due, the
    // new positions, Start); the line of the list of repertoires ("23 to review"); its positions where it is the user's
    // move.
    void drawCard(std::size_t index);
    [[nodiscard]] std::string listLine(std::size_t index);
    [[nodiscard]] std::size_t cardCount(std::size_t index);
    // The Home page's "In progress", while a session runs: where it is, Back to the session.
    void drawHomeLine();
    // Its entries of the command palette (Ctrl+K): a session of each repertoire, its new positions alone, the
    // training's two settings.
    void addCommands(const PaletteAdd& add);
    // Its --focus words, tried where App's table has them (app_chess.cpp): training, training-new (a session of the
    // active repertoire). `word` is the word `words` was made with.
    void addFocusWords(FocusWords& words, const std::string& word);

    // The day's review (#80): the repertoires' positions due today and after, how long one takes; the repertoires that
    // have positions due today, in the list's order; a repertoire's session of its positions due (false when none is
    // due, or none can start now); "Next: 4 positions of “Najdorf”" (empty when there is no such repertoire).
    [[nodiscard]] ReviewCount reviewCount();
    [[nodiscard]] std::vector<std::size_t> dueRepertoires();
    bool startReview(std::size_t repertoire);
    [[nodiscard]] std::string reviewNextLabel(std::size_t repertoire);
    // The positions the repertoires train, by the side that trains them (0 White, 1 Black): App answers
    // context::Repertoires with them, and the user's mistakes leave them out.
    [[nodiscard]] std::array<std::unordered_set<uint64_t>, 2> repertoirePositions();

    // A session holds the Board; it, while it does.
    [[nodiscard]] bool onBoard() const { return training_ != nullptr; }
    [[nodiscard]] const TrainingRun* session() const { return training_.get(); }

    // The Board's hooks while a session holds it (BoardSession).
    void sessionMove(chess::Move move) override { trainingAnswer(move); }
    void drawSessionPage() override { drawTrainingPage(); }
    void sessionDropped() override { dropTraining(); }
    // Back to the Openings page, or the Training page in the day's review.
    void closeSession(bool back) override { closeTraining(back); }
    [[nodiscard]] bool hidesAids() const override;
    [[nodiscard]] bool keptAside() const override { return training_ && training_->aside; }
    [[nodiscard]] const char* guideTopic() const override;

   private:
    // Each repertoire's cards and memories, counted for the day.
    struct Summary {
        uint64_t revision = UINT64_MAX;  // the repertoire's when its cards were made
        std::vector<study::TrainingCard> cards;
        TrainingFile kept;
        study::TrainingCounts counts;
        int64_t counted = 0;   // when (seconds), counted again each minute
        int new_per_day = -1;  // the limit it was counted with
    };
    [[nodiscard]] const Summary& summary(std::size_t index);
    void keep(const TrainingRun& t);  // its memories as the session wrote them, counted again
    // The cards ordered by what the databases play (the walk, the games of each), made in the background, one
    // repertoire at a time: nullptr until they are ready.
    [[nodiscard]] const std::vector<study::TrainingCard>* plannedCards(std::size_t index);
    // The databases whose tree is built and that the Openings page measures on; `key` names them and their state.
    [[nodiscard]] std::vector<study::TreeBase> trainingBases(std::string* key, std::vector<std::string>* names);
    [[nodiscard]] std::optional<std::size_t> repertoireOf(const std::filesystem::path& file);
    // A session.
    void startTraining(std::size_t index, TrainingRun::Kind kind);
    void trainingAnswer(chess::Move move);  // a move played on the board or typed
    void trainingGrade(bool right);         // the first answer to the card asked, kept
    void trainingDontKnow();
    void trainingRetry();
    void trainingNext();
    void trainingAdd();  // the move played added to the repertoire (Ctrl+Z takes it back)
    void trainingTakeBack();
    void finishTraining(bool stopped);
    void askTrainingCard();  // the board on the card asked, the opponent's move just played
    void showTrainingCard(const study::TrainingCard& card);  // its line on the board, the question's position
    void reviewMissed();                   // "Review them now": the missed moves in a session of their own
    void closeTraining(bool to_openings);  // the session left, the board given back
    void dropTraining();                   // another game came on the board: the session left where it is
    // The Board page during a session.
    void trainingKeys();
    void drawTrainingPage();
    void drawTrainingBoard(float size);
    void drawTrainingColumn();
    void drawTrainingEnd();

    RepertoireTrainingContext& app_;
    std::map<std::filesystem::path, Summary> summaries_;  // by the repertoire's file
    struct Planned {
        std::string of;  // the repertoire's file and revision, the databases
        std::vector<study::TrainingCard> cards;
    };
    std::map<std::filesystem::path, Planned> planned_;
    struct Planning {
        std::filesystem::path file;
        std::string of;
        std::future<std::vector<study::TrainingCard>> cards;
    };
    std::optional<Planning> planning_;
    bool limit_menu_ = false;  // the menu of the new positions a day, asked from Ctrl+K
    // A session on the Board page.
    std::unique_ptr<TrainingRun> training_;
};

}  // namespace fcg
