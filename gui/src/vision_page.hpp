#pragma once

// Board vision (#82, mockup M12, board 3), a module of its own out of App (#107; vision.hpp is its data): the Training
// page's part "Vision" and its card (a kind of training of the page's, training_kind.hpp), its exercises on the Board
// page, whose column becomes the exercise's (a session of the Board's, board_session.hpp): thirty seconds of
// coordinates, a game played blindfold against the Play page's opponent, a line read in one's head; the records kept in
// the profile's vision.json; the Home page's line; its palette commands and --focus words.
//
// What it needs of the program around it, it asks of its context (VisionContext, app_context.hpp): App's, or a test's.

#include <memory>
#include <optional>
#include <string>

#include <chess.hpp>

#include "app_context.hpp"
#include "focus_words.hpp"
#include "notice.hpp"
#include "training_kind.hpp"
#include "vision.hpp"

namespace fcg {

class VisionPage final : public TrainingKind {
   public:
    explicit VisionPage(VisionContext& app);
    ~VisionPage() override;
    VisionPage(const VisionPage&)            = delete;
    VisionPage& operator=(const VisionPage&) = delete;

    // Once a frame, whatever shows: the coordinates' thirty seconds, the blindfold game's moves (stopped by a game of
    // the user's played live on Lichess).
    void poll();
    // The Training page's part "Vision" (TrainingKind): the coordinates, blindfold, a line read in one's head; its card
    // on the Today part (the coordinates' record).
    [[nodiscard]] Part part() const override;
    void drawPart() override;
    void drawTodayCards(TodayCards& cards) override;
    // The Home page's "In progress", while an exercise runs: Back to the exercise.
    void drawHomeLine() override;
    // Its entries of the command palette (Ctrl+K): the part (in the view `training_view`), each exercise.
    void addCommands(const PaletteAdd& add, const std::string& training_view) override;
    // Its --focus words, tried where the Training page has them (training_page.cpp): vision-coordinates,
    // vision-blindfold, vision-lines. `word` is the word `words` was made with.
    void addFocusWords(FocusWords& words, const std::string& word) override;

    // The records (vision.json), read once; kept (not by a run for screenshots), a failure said in the console.
    [[nodiscard]] VisionRecord& visionRecord();
    void saveVisionRecord();

    // An exercise holds the Board; it, while it does.
    [[nodiscard]] bool onBoard() const override { return vision_ != nullptr; }
    [[nodiscard]] const VisionRun* session() const { return vision_.get(); }

    // The Board's hooks while an exercise holds it (BoardSession).
    void sessionMove(chess::Move move) override { visionAnswer(move); }
    void drawSessionPage() override { drawVisionPage(); }
    void sessionDropped() override { dropVision(); }
    void closeSession(bool back) override { closeVision(back); }
    [[nodiscard]] bool hidesAids() const override;
    [[nodiscard]] bool keptAside() const override { return vision_ && vision_->aside; }
    [[nodiscard]] const char* guideTopic() const override;

    // Why the last exercise asked for did not start, or stopped, as the part says it in place of its line (#146): no
    // engine to play blindfold against with Choose an engine…; an engine that did not start, the fair play's stop,
    // without a button. None once one started. Its fix, carried out by the program.
    [[nodiscard]] const std::optional<Notice>& startNotice() const { return vision_notice_; }
    void repair(const Fix& fix) { app_.repair(fix); }

   private:
    void startCoordinates();
    void startBlindfold();
    void startHeadLines();
    void startVisionRun(std::unique_ptr<VisionRun> run, const std::string& title);
    void visionAnswer(chess::Move move);
    void visionSquare(chess::Square square);  // a square clicked: the coordinates, "Where is…?"
    void visionNextSquare();
    void finishVision(bool stopped);
    void closeVision(bool to_training);
    void dropVision();
    void pollVision();
    // The Board page during an exercise.
    void visionKeys();
    void drawVisionPage();
    void drawVisionBoard(float size);
    void drawVisionColumn();

    VisionContext& app_;
    std::optional<VisionRecord> vision_record_;
    // The Training page's part: the coordinates' choice, blindfold's, what Enter starts, why the last one did not.
    CoordMode vision_mode_ = CoordMode::Find;
    CoordSide vision_side_ = CoordSide::White;
    Hidden vision_hidden_  = Hidden::All;
    int vision_colour_     = 0;  // blindfold: 0 White, 1 Black, 2 at random; its engine is the Play page's (#83)
    int vision_enter_      = 0;  // what Enter starts on the page: 0 the coordinates, 1 blindfold, 2 the lines
    std::optional<Notice> vision_notice_;
    // An exercise on the Board page.
    std::unique_ptr<VisionRun> vision_;
};

}  // namespace fcg
