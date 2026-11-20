#pragma once

// A game as the program holds it: its moves, the engines' data a tournament wrote for them, what a person wrote
// about them (comments, symbols, arrows and squares), and its variations. The Board page, the Games tab and the
// databases all use it; the PGN reader and writer (pgn_reader.hpp, pgn_writer.hpp) turn it into text and back.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <chess.hpp>
#include <types/score.hpp>

namespace fcg {

// The optional fields of a move comment, as the PGN options of the setup turn them on: a set of these bits.
enum PgnField : uint8_t {
    PGN_NODES    = 1 << 0,  // n=
    PGN_SELDEPTH = 1 << 1,  // sd=
    PGN_NPS      = 1 << 2,  // nps=
    PGN_TBHITS   = 1 << 3,  // tbhits=
    PGN_PV       = 1 << 4,  // pv="..."
    PGN_DECISION = 1 << 5,  // dt=
};

// A coloured arrow or square drawn on the board, as [%cal] and [%csl] write them: "Ge2e4" is a green arrow from e2
// to e4, "Rd5" a red square on d5. The colour is the letter written (G, R, Y, B, and any other kept as it is).
struct BoardMark {
    char colour = 'G';
    int8_t from = -1;  // 0 for a1 to 63 for h8
    int8_t to   = -1;  // the arrow's head; -1 for a square

    bool operator==(const BoardMark&) const = default;
};

// The mark of the assistant's comments (mockups M5, board 3, and M11, board 5): a comment of their own, marked so at
// the next reading; another program reads an ordinary comment.
inline constexpr std::string_view ASSISTANT_MARK = "[%rs assistant]";

// What a person, or a program other than a tournament's engines, wrote about a move. Kept as it was read, to be
// written back.
struct MoveNotes {
    std::string before;                 // a comment before the move: only a variation's first move has one
    std::string after;                  // the comment after it, its [%...] commands taken out
    std::vector<uint8_t> nags;          // $1 to $255 in their order; !, ?, !!, ??, !? and ?! are $1 to $6
    std::vector<BoardMark> arrows;      // [%cal]
    std::vector<BoardMark> squares;     // [%csl]
    std::optional<int64_t> emt_ms;      // [%emt]: the time the move took
    std::vector<std::string> commands;  // other [%...] commands, verbatim and in their order ("[%tqu ...]")
    bool eval_tag = false;              // the move's score came from [%eval], to be written back as [%eval]
    // The assistant's comment after the move, apart from `after`, which it never changes (#60): written as a comment
    // of its own that follows it, marked ASSISTANT_MARK; read back here, beside another comment or alone.
    std::string assistant;

    [[nodiscard]] bool empty() const noexcept {
        return before.empty() && after.empty() && nags.empty() && arrows.empty() && squares.empty() && !emt_ms &&
               commands.empty() && !eval_tag && assistant.empty();
    }
    bool operator==(const MoveNotes&) const = default;
};

// A game read back from a PGN written in the fastchess comment style, or in any other.
struct ReplayMove {
    std::string san;
    chess::Move move;
    chess::Color mover = chess::Color::WHITE;
    bool book          = false;

    std::optional<fastchess::Score> score;  // from the mover's point of view
    int depth       = 0;
    int64_t time_ms = -1;
    int64_t nps     = 0;                        // only when the PGN was written with nps=
    std::optional<std::array<int64_t, 3>> wdl;  // wdl=W/D/L: permille, from the mover's point of view
    // Only when the PGN was written with them: n=, sd=, tbhits=, pv="...", dt=.
    uint64_t nodes   = 0;
    int64_t seldepth = 0;
    uint64_t tbhits  = 0;
    std::string pv;  // UCI moves, from the move played
    std::optional<int64_t> decision_ms;
    std::optional<int64_t> clock_ms;  // the mover's clock after the move, when the comment has [%clk]
    uint8_t fields = 0;               // the PgnField found in its comment, a zero told apart from a field not written
    MoveNotes notes;
};

struct ReplayGame {
    std::vector<std::pair<std::string, std::string>> headers;
    std::string start_fen;
    std::vector<ReplayMove> moves;
    std::string result;
    std::string final_comment;  // how a tournament's game ended, from its last move's comment
    std::string pre_comment;    // a comment before the first move
    uint8_t fields = 0;         // the PgnField of any of its moves: those the tournament wrote

    [[nodiscard]] std::string header(const std::string& key) const;
};

// Moves played where the game, or another variation, went another way: the moves from that position on.
struct Variation {
    int parent = -1;  // the line it branches from: -1 the game, else a variation
    int from   = 0;   // the position of that line it starts from: its first move instead of that line's move from+1
    std::vector<ReplayMove> moves;
};

// A game and its variations.
struct GameTree {
    ReplayGame main;
    std::vector<Variation> variations;
};

// "{+0.35/18 1.234s, ...}" (the body without braces) into score, depth, time and nps.
void parseFastchessComment(std::string_view comment, ReplayMove& move);

// The "[%eval 0.17]" or "[%eval #-3]" of a comment (lichess, ChessBase: White's point of view, in pawns or a mate
// in moves) into the move's score, from its mover's point of view; nothing when the comment has none. The mover must
// be set.
void parseEvalTag(std::string_view comment, ReplayMove& move);
// [%clk 0:01:23.4] in a move's comment: the mover's clock after it.
void parseClockTag(std::string_view comment, ReplayMove& move);

}  // namespace fcg
