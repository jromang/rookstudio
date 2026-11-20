// The assistant's comment beside another, never instead (#60, mockup M11, board 5): put on a move that has a comment
// already (Lichess's analysis, the user's, an annotator's), corrected into the user's, taken back by Ctrl+Z, and sent
// to a Lichess study with or without it (#79).

#include <doctest/doctest.hpp>

#include <string>
#include <vector>

#include <study/pgn_reader.hpp>
#include <study/pgn_writer.hpp>

#include "assistant_notes.hpp"
#include "board_page.hpp"
#include "lichess_studies.hpp"

using namespace fcg;

namespace {

// A game analysed by Lichess: its comment on 3...Nd4?! and 4.Nxe5??, none on the other moves.
GameTree analysedByLichess() {
    const auto read = study::readPgnGame(
        "[Event \"Club\"]\n[White \"A\"]\n[Black \"B\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bc4 Nd4?! { [%eval 0.85] Inaccuracy. "
        "Nxd4 was best. } 4. Nxe5?? { [%eval -2.5] Blunder. c3 was best. } 4... Qg5 *\n");
    REQUIRE(read.tree);
    return *read.tree;
}

std::string fenAfter(const GameTree& tree, int ply) { return boardAt(tree.main, false, ply).getFen(); }

std::string oneLine(std::string text) {
    for (auto& c : text) c = c == '\n' ? ' ' : c;
    return text;
}

}  // namespace

TEST_CASE("assistant's comment: on a move commented already, it goes beside the comment, which stays as it was") {
    auto tree      = analysedByLichess();
    const auto nd4 = tree.main.moves[5].notes;
    // "Comment this move" on 3...Nd4?!, and the key moments on 3...Nd4 and 2.Nf3 (none there).
    const std::vector<AssistantTarget> targets = {{-1, 6, fenAfter(tree, 6)}, {-1, 3, fenAfter(tree, 3)}};
    const auto put =
        placeAssistantComments(tree, false, targets, {"Black moves the knight a second time.", "The knight develops."});
    CHECK(put == std::vector<std::string>{"Black moves the knight a second time.", "The knight develops."});
    const auto& notes = tree.main.moves[5].notes;
    CHECK(notes.after == nd4.after);  // Lichess's, never changed
    CHECK(notes.after == "Inaccuracy. Nxd4 was best.");
    CHECK(notes.assistant == "Black moves the knight a second time.");
    CHECK(notes.eval_tag == nd4.eval_tag);
    CHECK(tree.main.moves[2].notes.assistant == "The knight develops.");
    CHECK(tree.main.moves[2].notes.after.empty());
    // In the PGN, two comments in a row, the second marked; read back, each in its slot.
    const auto pgn = study::writePgnGame(tree);
    CHECK(oneLine(pgn).find("{[%eval 0.85] Inaccuracy. Nxd4 was best.} {[%rs assistant] Black moves the knight a "
                            "second time.}") != std::string::npos);
    const auto again = study::readPgnGame(pgn);
    REQUIRE(again.tree);
    CHECK(again.tree->main.moves[5].notes == notes);
    CHECK(again.tree->main.moves[2].notes == tree.main.moves[2].notes);
    // The assistant's own is never written twice: asked again, the move keeps the first.
    CHECK(placeAssistantComments(tree, false, {targets[0]}, {"Another."}).empty());
    CHECK(tree.main.moves[5].notes.assistant == "Black moves the knight a second time.");
    // A move that no longer leads where it did (the game changed meanwhile) gets nothing.
    CHECK(placeAssistantComments(tree, false, {{-1, 8, fenAfter(tree, 7)}}, {"Late."}).empty());
}

TEST_CASE("assistant's comment: corrected, it becomes the user's after the other one; Ctrl+Z brings both back") {
    BoardPage b;
    b.tree = analysedByLichess();
    b.game = b.tree.main;
    b.line = -1;
    b.ply  = 6;
    // The assistant's comment put beside Lichess's, one change.
    auto before = boardSnapshot(b);
    REQUIRE(placeAssistantComments(b.tree, false, {{-1, 6, fenAfter(b.tree, 6)}}, {"Black moves the knight again."})
                .size() == 1);
    boardChanged(b, std::move(before));
    // A double click corrects it: the text becomes the user's, after the move's comment, the assistant's slot emptied.
    BoardPage::CommentEdit edit;
    edit.line      = -1;
    edit.ply       = 6;
    edit.assistant = true;
    CHECK_FALSE(editComment(b.tree, edit, "  Black moves the knight again.  "));  // left as it was: still its
    CHECK(b.tree.main.moves[5].notes.assistant == "Black moves the knight again.");
    before = boardSnapshot(b);
    REQUIRE(editComment(b.tree, edit, "The knight moves again; e5 falls."));
    boardChanged(b, std::move(before));
    const auto& notes = b.tree.main.moves[5].notes;
    CHECK(notes.assistant.empty());
    CHECK(notes.after == "Inaccuracy. Nxd4 was best. The knight moves again; e5 falls.");
    CHECK(oneLine(study::writePgnGame(b.tree)).find("[%rs assistant]") == std::string::npos);
    CHECK(b.changes.count == 2);
    // Ctrl+Z: the assistant's comment again, then the move as Lichess left it; Ctrl+Y makes them again.
    REQUIRE(boardUndo(b, false));
    CHECK(b.tree.main.moves[5].notes.assistant == "Black moves the knight again.");
    CHECK(b.tree.main.moves[5].notes.after == "Inaccuracy. Nxd4 was best.");
    REQUIRE(boardUndo(b, false));
    CHECK(b.tree.main.moves[5].notes.assistant.empty());
    CHECK(b.tree.main.moves[5].notes.after == "Inaccuracy. Nxd4 was best.");
    CHECK_FALSE(boardUndo(b, false));
    CHECK(b.changes.count == 0);
    REQUIRE(boardUndo(b, true));
    CHECK(b.tree.main.moves[5].notes.assistant == "Black moves the knight again.");
    // Corrected into nothing on a move without another comment: the comment is gone.
    BoardPage::CommentEdit alone         = edit;
    alone.ply                            = 1;
    b.tree.main.moves[0].notes.assistant = "The centre.";
    REQUIRE(editComment(b.tree, alone, ""));
    CHECK(b.tree.main.moves[0].notes.empty());
}

TEST_CASE("Lichess studies: a game sent with or without the assistant's comments beside Lichess's") {
    auto tree                          = analysedByLichess();
    tree.main.moves[5].notes.assistant = "Black moves the knight a second time.";
    tree.main.moves[2].notes.assistant = "The knight develops.";
    CHECK(lichess::assistantComments(tree) == 2);
    const auto without = oneLine(lichess::studyGamePgn(tree, false, false));
    CHECK(without.find("Inaccuracy. Nxd4 was best.") != std::string::npos);  // Lichess's, as it was
    CHECK(without.find("knight") == std::string::npos);
    CHECK(without.find("[%rs") == std::string::npos);
    const auto with = oneLine(lichess::studyGamePgn(tree, false, true));
    // Two plain comments in a row: Lichess reads both, without the mark.
    CHECK(with.find("{Inaccuracy. Nxd4 was best.} {Black moves the knight a second time.}") != std::string::npos);
    CHECK(with.find("2. Nf3 {The knight develops.}") != std::string::npos);
    CHECK(with.find("[%rs") == std::string::npos);
    // The board's game is not changed by what is sent.
    CHECK(tree.main.moves[5].notes.assistant == "Black moves the knight a second time.");
}
