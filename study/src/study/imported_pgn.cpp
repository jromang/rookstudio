#include <string>

#include <chess.hpp>
#include <tour/pgn_file.hpp>

#include "import.hpp"
#include "scan_board.hpp"

namespace fcg::study {

namespace {

// The legal move of `board` that the fast board's move names.
chess::Move chessMove(const chess::Board& board, const CodedMove& wanted) {
    chess::Movelist list;
    chess::movegen::legalmoves(list, board);
    for (const auto& m : list) {
        const bool castle = m.typeOf() == chess::Move::CASTLING;
        const int promotion =
            m.typeOf() == chess::Move::PROMOTION ? static_cast<int>(m.promotionType()) : static_cast<int>(NO_KIND);
        if (m.from().index() == wanted.from && m.to().index() == wanted.to && promotion == wanted.promotion &&
            castle == ((wanted.flags & (MOVE_SHORT_CASTLE | MOVE_LONG_CASTLE)) != 0))
            return m;
    }
    return chess::Move(chess::Move::NO_MOVE);
}

}  // namespace

std::string importedPgn(const ImportedGame& game) {
    if (!game.text.empty()) return game.text;
    std::string out;
    for (const auto& [key, value] : game.tags) out += tour::pgnTagPair(key, value);
    out += "\n";
    const std::string fen = game.start_fen.empty() ? std::string(chess::constants::STARTPOS) : game.start_fen;
    chess::Board board;
    board.set960(game.chess960);
    board.setFen(fen);
    ScanBoard scan;
    std::string moves;
    if (scan.setFen(fen, game.chess960)) {
        // A code no move of the position has, or one cut short: the game is damaged from there, written up to it as
        // the other decoders read it (decode() would make it a null move, "--").
        Cursor cursor(game.moves, game.plies);
        for (uint32_t ply = 0;; ++ply) {
            const int code = cursor.next(scan);
            if (code == Cursor::END) break;
            const auto coded = scan.decode(code);
            if (coded.flags & MOVE_NULL) {
                moves += (board.sideToMove() == chess::Color::WHITE ? std::to_string(board.fullMoveNumber()) + ". "
                                                                    : std::string()) +
                         "-- ";
                board.makeNullMove();
                scan.play(coded);
                continue;
            }
            const auto move = chessMove(board, coded);
            if (move == chess::Move(chess::Move::NO_MOVE)) break;
            if (board.sideToMove() == chess::Color::WHITE)
                moves += std::to_string(board.fullMoveNumber()) + ". ";
            else if (ply == 0)
                moves += std::to_string(board.fullMoveNumber()) + "... ";
            moves += chess::uci::moveToSan(board, move) + " ";
            board.makeMove(move);
            scan.play(coded);
        }
    }
    const char* result = game.result == '1' ? "1-0" : game.result == '0' ? "0-1" : game.result == '=' ? "1/2-1/2" : "*";
    return out + moves + result + "\n";
}

}  // namespace fcg::study
