#pragma once

// Board vision (#82, mockup M12, board 3): the coordinates, blindfold play against an engine, a line read in one's
// head. Each runs on the Board page as the other exercises of the Training page do (vision_page.hpp); the records are
// kept in the profile's vision.json. Nothing leaves the machine.

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <chess.hpp>

#include "board_game.hpp"

namespace fcg {

// ---------------------------------------------------------------------------
// The coordinates

// Thirty seconds of squares: to find (a click on a board without its letters and figures) or to name (two keys), seen
// from White, from Black, or from either at random, square by square.
enum class CoordMode { Find, Name };
enum class CoordSide { White, Black, Random };
inline constexpr double COORD_SECONDS = 30.0;
// The record's key of a mode and a side: "find-white", "name-random"…
[[nodiscard]] std::string coordKey(CoordMode mode, CoordSide side);
// The next square asked: any but the last one.
[[nodiscard]] chess::Square nextCoordinate(std::mt19937& rng, std::optional<chess::Square> last);
// A square named with two keys ("e5", "E5"); none when they name no square.
[[nodiscard]] std::optional<chess::Square> typedSquare(std::string_view text);
[[nodiscard]] std::string squareName(chess::Square square);

// ---------------------------------------------------------------------------
// Blindfold

// What the board hides of the pieces: every one, every one from the 10th move, all but the pawns.
enum class Hidden { All, AfterTen, AllButPawns };
// The pieces the board shows of `board`, `peek` showing them all.
[[nodiscard]] std::array<chess::Piece, 64> shownPieces(const chess::Board& board, Hidden hidden, bool peek);
inline constexpr int PEEKS           = 3;    // the pieces shown, Shift held, at most this many times a game
inline constexpr double PEEK_SECONDS = 2.0;  // each for this long at most

// "Where is…?": a piece that its colour and kind name alone (a bishop by its squares' colour when its side has two).
struct PieceQuestion {
    const char* text     = "";  // N_(): "the white queen", "the black dark-squared bishop"
    chess::Square square = chess::Square::NO_SQ;
};
[[nodiscard]] std::optional<PieceQuestion> pieceQuestion(const chess::Board& board, std::mt19937& rng);

// ---------------------------------------------------------------------------
// A line read in one's head

// A position of a well-known opening, the next six moves read without the board moving, then the piece on a square
// they changed: what it is, or that it is empty.
inline constexpr int HEAD_MOVES = 6;
struct HeadLine {
    const char* opening = "";        // N_()
    std::string fen;                 // the position shown
    std::vector<chess::Move> moves;  // read, from `fen`
    chess::Square square = chess::Square::NO_SQ;
    chess::Piece answer  = chess::Piece::NONE;  // NONE: the square is empty
};
// The openings the lines come from: a name (N_()) and its moves in SAN, from the start.
struct HeadOpening {
    const char* name;
    const char* moves;
};
[[nodiscard]] const std::vector<HeadOpening>& headOpenings();
[[nodiscard]] HeadLine headLine(std::mt19937& rng);
// An answer typed as FEN writes a piece ("N" a white knight, "n" a black one), "-" or "0" for an empty square: the
// piece, NONE for empty; none for another key.
[[nodiscard]] std::optional<chess::Piece> typedPiece(char key);

// ---------------------------------------------------------------------------
// The records

struct VisionRecord {
    std::map<std::string, int> coordinates;  // by coordKey(): the most squares right in thirty seconds
    int lines = 0, lines_right = 0;          // lines read, answered right
    int blind = 0;                           // blindfold games played
    int where = 0, where_right = 0;          // "Where is…?" asked, answered right
    bool operator==(const VisionRecord&) const = default;
};
[[nodiscard]] VisionRecord readVisionRecord(const std::filesystem::path& profile);
bool writeVisionRecord(const std::filesystem::path& profile, const VisionRecord& record);

// ---------------------------------------------------------------------------
// A session on the Board page (vision_page.hpp)

struct VisionRun {
    enum class Kind { Coordinates, Blindfold, Line };
    // Ready: the coordinates before Enter; Asking: a question (a square, a move, a line's piece); Answered: a line's
    // answer shown; End.
    enum class Phase { Ready, Asking, Answered, End };
    Kind kind   = Kind::Coordinates;
    Phase phase = Phase::Ready;
    std::mt19937 rng{std::random_device{}()};
    std::chrono::steady_clock::time_point began, answered_at;

    // The coordinates: the square asked, the last one missed (in green) and the square given for it (in orange).
    CoordMode mode      = CoordMode::Find;
    CoordSide side      = CoordSide::White;
    bool flipped        = false;
    chess::Square asked = chess::Square::NO_SQ;
    std::optional<chess::Square> missed, wrong;
    int right = 0, faults = 0;
    std::string typed;      // the keys of a square named
    int record_before = 0;  // the record when it began

    // Blindfold: the game against the engine, what the board hides, the pieces shown on Shift, "Where is…?" (F2).
    std::unique_ptr<BoardMatch> match;
    BoardMatch::State state;
    uint64_t version = 0;
    Hidden hidden    = Hidden::All;
    chess::Color you = chess::Color::WHITE;
    std::string engine;
    std::string strength;  // as the Play page chose it (#83): "Elo 1,500", "1,000 nodes a move", "full strength"
    int peeks    = PEEKS;
    bool peeking = false;
    std::chrono::steady_clock::time_point peek_until{};
    std::optional<PieceQuestion> where;
    std::optional<chess::Square> where_said;  // the answer given
    bool counted = false;                     // the game counted in the record

    // A line read in one's head.
    std::vector<HeadLine> lines;
    std::size_t line = 0;
    std::optional<chess::Piece> said;
    int lines_right = 0;

    // The board before the session, given back after it.
    bool aside = false;
    bool flip  = false;
    int tab    = 0;
};

}  // namespace fcg
