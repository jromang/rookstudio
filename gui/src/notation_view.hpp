#pragma once

// Chess notation drawn as flowing text (the Board page's moves and the analysis's lines): wrapped to the width,
// each move a target the mouse can hover, click or right-click, the pieces as figurines from the board's piece set
// when the appearance asks for them.

#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include <chess.hpp>

#include "board_view.hpp"
#include "ui.hpp"

namespace fcg {

// A symbol (a NAG) as the notation shows it: "!", "?!", "□", "⩲" (or "+=" when the font lacks it), "N"... "$NN" for
// one it has no sign for.
[[nodiscard]] std::string nagText(uint8_t nag);
// The colour of a move's symbol (! green, ? orange...), 0 for the text's own.
[[nodiscard]] ImU32 nagColour(uint8_t nag);

struct NotationItem {
    // Break: what follows starts a new line. Field: a line of its own where text is written (field, field_size), its
    // hint in `text`. Block: a box of its own across the width, `icon` and `title` on its first line in `colour`, its
    // `text` wrapped under them, and `hint` under it when there is one (the assistant's comment beside another, mockup
    // M11, board 5).
    enum class Kind { Move, Number, Text, Break, Field, Block };
    Kind kind = Kind::Move;
    std::string text;  // a move's SAN, a number ("12.", "12…"), a text
    ImU32 colour = 0;  // 0: the text's
    std::string mark;  // after a move: ?!, ?, ??
    ImU32 mark_colour = 0;
    bool current      = false;  // framed
    bool bold         = false;
    bool italic       = false;     // a text in italics (the assistant's comments)
    std::optional<ui::Icon> icon;  // a text that is an icon, as wide as the font is high
    int id                 = -1;  // what the mouse events give back (a text too when it has one: a comment)
    std::string title;            // a block's
    std::string hint;             // a block's last line, smaller
    float indent      = 0;   // a line starting with it starts this far in (a variation)
    char* field            = nullptr;
    std::size_t field_size = 0;
    bool field_focus       = false;  // the keys go to the field
};

struct NotationEvents {
    int hovered       = -1;  // ids
    int clicked       = -1;
    int right_clicked = -1;
    int double_clicked = -1;                         // a move's, or a text's that has an id (a comment)
    std::optional<ImVec2> current_min, current_max;  // where the current move is, to scroll to it
    bool field_done      = false;                    // the field left with Enter or a click elsewhere
    bool field_cancelled = false;                    // left with Escape
};

// The items from the cursor, `width` wide (0: the room left), at most `max_lines` lines (0: all; the last one
// ends with "…" when cut). `figurines`: the atlas their pieces come from, none for letters.
NotationEvents drawNotation(const char* id, const std::vector<NotationItem>& items, float width,
                            const PieceAtlas* figurines, int max_lines = 0);

// A line of UCI moves from `board` as items: the move numbers, each legal move's SAN with its index as id; at most
// `max_moves` (the rest left out).
[[nodiscard]] std::vector<NotationItem> lineItems(chess::Board board, const std::vector<std::string>& pv,
                                                  std::size_t max_moves);

// A SAN with its piece letter as a figurine: the piece, and the text after the letter; nullopt for a pawn move or
// castling. The promotion keeps its letter.
struct Figurine {
    chess::PieceType type;
    std::string rest;
};
[[nodiscard]] std::optional<Figurine> figurineOf(const std::string& san);

}  // namespace fcg
