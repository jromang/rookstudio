#pragma once

// Endgames against the tablebases and the mates (#82, mockup M12, board 3), a module of its own out of App (#107;
// endgames.hpp is their data): the Training page's part "Endgames" (what answers, the catalogue, the endgames of the
// user's games, the mates of Lichess's puzzles), the tables missing (download those of 3 to 5 pieces, a folder already
// on the disk, Lichess's online), and an endgame played on the Board page, whose column becomes the endgame's (a
// session of the Board's, board_session.hpp): the tables answer each move, a fault is said with the moves that kept the
// result, the position before it comes back in the day's review; the record endgames/record.json; the Home page's line;
// its palette commands and --focus words.
//
// What it needs of the program around it, it asks of its context (EndgamesContext, app_context.hpp): App's, or a
// test's. The tables of the disk and their download stay App's (the Board's Endgames tab and a tournament's endgame
// check probe them too): context::DiskTables.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <chess.hpp>

#include "app_context.hpp"
#include "board_session.hpp"
#include "endgames.hpp"
#include "focus_words.hpp"
#include "review_queue.hpp"

namespace fcg {

class EndgamesPage final : public BoardSession {
   public:
    explicit EndgamesPage(EndgamesContext& app);
    ~EndgamesPage() override;
    EndgamesPage(const EndgamesPage&)            = delete;
    EndgamesPage& operator=(const EndgamesPage&) = delete;

    // Once a frame, whatever shows: Lichess's answers, a question that gave nothing asked again, the tables' move half
    // a second after the user's, the line of "See the win".
    void poll();
    // The Training page's part "Endgames": what answers, the tables missing and their download, the catalogue, the
    // endgames of the user's games, the mates.
    void drawPart();
    // The Home page's "In progress", while an endgame is played: where it is, Back to the endgame.
    void drawHomeLine();
    // Its entries of the command palette (Ctrl+K): the part and its mates (in the view `training_view`), each endgame
    // of the catalogue, the missed ones due, those of the user's games, the tables' download or its Cancel.
    void addCommands(const PaletteAdd& add, const std::string& training_view);
    // Its --focus words, tried where App's table has them (app_chess.cpp): endgame=ID, endgames-review. `word` is the
    // word `words` was made with.
    void addFocusWords(FocusWords& words, const std::string& word);

    // The day's review: the endgames missed due today and after, how long one takes; their session, the longest overdue
    // first (false when none is due, or none can be played now).
    [[nodiscard]] ReviewCount reviewCount();
    bool startReview();
    // The record (endgames/record.json), read once; kept (not by a run for screenshots), a failure said in the console.
    [[nodiscard]] EndgameRecord& endgameRecord();
    void saveEndgameRecord();
    // The endgames of the catalogue done (the Today part's card).
    [[nodiscard]] std::size_t doneCount();
    // The part brought to its mates at its next draw (the Today part's card "Mates").
    void showMates() { endgame_scroll_mates_ = true; }

    // An endgame holds the Board; it, while it does.
    [[nodiscard]] bool onBoard() const { return endgame_ != nullptr; }
    [[nodiscard]] const EndgameRun* session() const { return endgame_.get(); }

    // The Board's hooks while an endgame holds it (BoardSession).
    void sessionMove(chess::Move move) override { endgameAnswer(move); }
    void drawSessionPage() override { drawEndgamePage(); }
    void sessionDropped() override { dropEndgame(); }
    void closeSession(bool back) override { closeEndgame(back); }
    [[nodiscard]] bool hidesAids() const override;
    [[nodiscard]] bool keptAside() const override { return endgame_ && endgame_->aside; }
    [[nodiscard]] const char* guideTopic() const override;

   private:
    [[nodiscard]] std::filesystem::path endgamesFolder() const { return app_.profileDir() / "endgames"; }
    // Why the position cannot be played against the tables now, empty when it can: the tables it needs.
    [[nodiscard]] std::string endgameNeed(const std::string& fen);
    [[nodiscard]] std::string endgameLock() const;  // why a session cannot start, empty when it can
    void startSyzygyDownload();                     // the tables of 3 to 5 pieces, its pace measured from now
    // The Training page's part.
    void drawEndgameCatalogue(bool& enter);
    void drawEndgameTablesCard(bool& enter);  // the tables missing: download them, a folder, Lichess's online
    void drawSyzygyDownloadCard();
    void drawEndgamesFromGames();
    void drawMatesCard();
    [[nodiscard]] std::vector<EndgameTask> gameEndgames();  // the user's mistakes in positions of 7 pieces or fewer
    [[nodiscard]] std::string endgameState(const EndgameEntry& entry);  // "done in 9 (the tables: 7)", "5 pieces"…
    // A session.
    bool startEndgame(const EndgameEntry& entry);
    bool startGameEndgames();
    void startEndgameRun(std::unique_ptr<EndgameRun> run);
    void beginEndgameTask();
    void showEndgamePosition();  // the task's start and its moves on the board
    void askEndgameTables();     // the tables' answer for the board's position: the disk's at once, Lichess's coming
    void endgameAnswered();      // an answer came: a move kept judged, the tables' move
    void endgameAnswer(chess::Move move);
    void endgameJudge(chess::Move move);
    void endgameTablesMove();
    void endgameTakeBack();
    void endgameRestart();
    void endgameContinue();
    void endgameSeeWin();
    void endgameNext();
    void endgameEnded(EndgameOutcome outcome);
    void finishEndgames(bool stopped);
    void closeEndgame(bool to_training);
    void dropEndgame();
    void leaveEndgameQuestions();  // the questions to Lichess of a session left: cancelled, parked in the jobs
    // The Board page during an endgame.
    void endgameKeys();
    void drawEndgamePage();
    void drawEndgameBoard(float size);
    void drawEndgameColumn();
    void drawEndgameEnd();

    EndgamesContext& app_;
    std::optional<EndgameRecord> endgame_record_;
    // The Training page's part.
    std::string endgame_need_;           // the endgame whose tables are missing: the card says what it needs
    int endgame_tables_choice_ = 0;      // 0 download the tables, 1 a folder already there, 2 Lichess's online
    int endgame_selected_      = 0;      // the catalogue's endgame the arrows move over
    bool endgame_keys_         = false;  // the arrows and Enter go to the catalogue: one of its endgames was clicked
    bool endgame_scroll_mates_ = false;  // the page brought to its mates (the Today part's card)
    std::string endgame_error_;
    // The tables' download, as its card measures its pace: since when, from how many bytes.
    std::chrono::steady_clock::time_point syzygy_since_{};
    uint64_t syzygy_from_ = 0;
    // An endgame on the Board page.
    std::unique_ptr<EndgameRun> endgame_;
};

}  // namespace fcg
