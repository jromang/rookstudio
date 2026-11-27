#pragma once

// Guessing the moves of a game (#81, mockup M12, board 2), a module of its own out of App (#107; mistakes.hpp holds its
// data): the Training page's part "Guess the moves" and its card (a kind of training of the page's,
// training_kind.hpp: a game drawn at random from a database and a filter, or the Board page's game), a game replayed
// on the Board page from the chosen side, a move to guess at each of its turns, scored by the analysis engine (a
// session of the Board's, board_session.hpp), the games guessed kept in the profile, the Home page's line, its palette
// commands and --focus words.
//
// What it needs of the program around it, it asks of its context (GuessContext, app_context.hpp): App's, or a test's.

#include <array>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <chess.hpp>

#include "app_context.hpp"
#include "focus_words.hpp"
#include "mistakes.hpp"
#include "training_kind.hpp"

namespace fcg {

class GuessPage final : public TrainingKind {
   public:
    explicit GuessPage(GuessContext& app);
    ~GuessPage() override;
    GuessPage(const GuessPage&)            = delete;
    GuessPage& operator=(const GuessPage&) = delete;

    // Once a frame, whatever shows: a game drawn at random that came, the theory looked for, the searches after the
    // game's move and the guess.
    void poll();
    // The Training page's part "Guess the moves" (TrainingKind): a game at random (its database, filter and side), the
    // Board page's game; its card on the Today part (the last game guessed).
    [[nodiscard]] Part part() const override;
    void drawPart() override;
    void drawTodayCards(TodayCards& cards) override;
    // The Home page's "In progress", while a game is guessed: its points, Back to the game guessed.
    void drawHomeLine() override;
    // Its entries of the command palette (Ctrl+K): the Board page's game from the position shown, else the part (in the
    // view `training_view`); a random game.
    void addCommands(const PaletteAdd& add, const std::string& training_view) override;
    // Its --focus words, tried where the Training page has them (training_page.cpp): guess-board, guess-random. `word`
    // is the word `words` was made with.
    void addFocusWords(FocusWords& words, const std::string& word) override;

    // The last game guessed (guess/games.jsonl), read once; none before the first.
    [[nodiscard]] const std::optional<GuessRecord>& lastRecord();

    // A game guessed holds the Board; it, while it does.
    [[nodiscard]] bool onBoard() const override { return guess_ != nullptr; }
    [[nodiscard]] const GuessRun* session() const { return guess_.get(); }

    // The Board's hooks while a game guessed holds it (BoardSession).
    void sessionMove(chess::Move move) override { guessAnswer(move); }
    void drawSessionPage() override { drawGuessPage(); }
    void sessionDropped() override { dropGuess(); }
    void closeSession(bool back) override { closeGuess(back); }
    [[nodiscard]] bool hidesAids() const override;
    [[nodiscard]] bool keptAside() const override { return guess_ && guess_->aside; }
    [[nodiscard]] const char* guideTopic() const override;

   private:
    [[nodiscard]] std::filesystem::path guessFolder() const { return app_.profileDir() / "guess"; }
    bool startGuessRandom();  // a game of the chosen database and filter, drawn in the background
    void startGuessBoard();   // the Board page's game, from the position shown, for the side to move
    void startGuess(ReplayGame game, bool chess960, std::string title, chess::Color side, int from,
                    std::optional<std::pair<std::filesystem::path, uint64_t>> origin);
    void askGuess();  // the next question, or the end
    void guessAnswer(chess::Move move);
    void pollGuess();     // the theory looked for, the searches after the game's move and the guess
    void guessScored();   // both searches back: the points
    void guessAdvance();  // the game's move and the reply played, the next question
    void finishGuess(bool stopped);
    void closeGuess(bool to_training);
    void dropGuess();
    // The Board page during a game guessed.
    void guessKeys();
    void drawGuessPage();
    void drawGuessBoard(float size);
    void drawGuessColumn();
    void drawGuessEnd();

    GuessContext& app_;
    // The Training page's part: the database a game is drawn from (the shelf's index; -1 the first of the user's), the
    // side, the players' least Elo, the quick filter's words (players, event, an ECO code, a year).
    int guess_source_ = -1;
    int guess_side_   = 0;  // 0 White, 1 Black
    int guess_elo_    = 0;  // both players at least
    std::array<char, 96> guess_words_{};
    // A game drawn at random from the database and the filter chosen, in the background (a large database's filter
    // takes a moment).
    struct GuessPick {
        std::optional<ReplayGame> game;
        bool chess960 = false;
        std::string title;
        std::filesystem::path folder;
        uint64_t id = 0;
        std::string error;  // "none": no game passes the filter; "short": none long enough
    };
    std::future<GuessPick> guess_pick_;
    chess::Color guess_pick_side_ = chess::Color::WHITE;
    std::optional<GuessRecord> guess_last_;
    bool guess_last_read_ = false;
    std::string guess_error_;
    // A game guessed on the Board page.
    std::unique_ptr<GuessRun> guess_;
};

}  // namespace fcg
