#include "board_page.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>

#include <matchmaking/syzygy.hpp>
#include <tour/pgn_file.hpp>

#include "assistant_notes.hpp"
#include "tablebases.hpp"

namespace fcg {

std::vector<SideEngine> analysisEngines(const BoardSettings& settings) {
    std::vector<SideEngine> out;
    const auto& a = settings.analysis;
    if (!a.engine.empty()) out.push_back({a.engine, std::max(0, settings.first_threads), a.hash, a.multipv});
    for (const auto& e : settings.side_engines) {
        if (out.size() >= MAX_SIDE_ENGINES) break;
        if (e.engine.empty() ||
            std::any_of(out.begin(), out.end(), [&](const SideEngine& o) { return o.engine == e.engine; }))
            continue;
        out.push_back(e);
        out.back().threads = std::max(0, e.threads);
    }
    return out;
}

void setAnalysisEngines(BoardSettings& settings, std::vector<SideEngine> engines) {
    if (engines.size() > MAX_SIDE_ENGINES) engines.resize(MAX_SIDE_ENGINES);
    auto& a = settings.analysis;
    if (engines.empty()) {
        a.engine.clear();
        settings.first_threads = 0;
        settings.side_engines.clear();
        return;
    }
    a.engine               = engines.front().engine;
    a.hash                 = engines.front().hash;
    a.multipv              = engines.front().multipv;
    settings.first_threads = engines.front().threads;
    settings.side_engines.assign(engines.begin() + 1, engines.end());
}

AnalysisPosition analysisPosition(const ReplayGame& game, bool chess960, int ply) {
    AnalysisPosition position;
    position.chess960 = chess960;
    chess::Board board;
    board.set960(chess960);
    board.setFen(game.start_fen.empty() ? std::string(chess::constants::STARTPOS) : game.start_fen);
    position.fen = board.getFen();
    const int n  = std::clamp(ply, 0, static_cast<int>(game.moves.size()));
    for (int i = 0; i < n; ++i) {
        const auto& move = game.moves[static_cast<std::size_t>(i)].move;
        position.moves.push_back(chess::uci::moveToUci(move, chess960));
        board.makeMove<true>(move);
    }
    return position;
}

fastchess::Score whiteScore(const fastchess::Score& score, chess::Color side_to_move) {
    auto white = score;
    if (side_to_move == chess::Color::BLACK) white.value = -white.value;
    return white;
}

chess::Square shownTarget(const chess::Move& move) {
    if (move.typeOf() != chess::Move::CASTLING) return move.to();
    const bool king_side = move.to().index() > move.from().index();
    return chess::Square(chess::File(king_side ? 6 : 2), move.from().rank());
}

std::vector<chess::Move> movesFrom(const chess::Board& board, chess::Square from) {
    chess::Movelist legal;
    chess::movegen::legalmoves(legal, board);
    std::vector<chess::Move> out;
    for (const auto& m : legal) {
        if (m.from() == from) out.push_back(m);
    }
    return out;
}

std::optional<std::string> pgnSourceGame(const PgnSource& source, std::size_t game) {
    if (!source.index || game >= source.index->games.size()) return std::nullopt;
    const auto& span = source.index->games[game].span;
    if (source.text) {
        if (span.offset > source.text->size()) return std::nullopt;
        return source.text->substr(static_cast<std::size_t>(span.offset), span.length);
    }
    return tour::readSpan(source.file, span);
}

namespace {

chess::Piece editorAt(const PositionEditor& e, int file, int rank) {
    return e.squares[static_cast<std::size_t>(rank * 8 + file)];
}

}  // namespace

bool editorCanCastle(const PositionEditor& e, int right) {
    const bool white  = right < 2;
    const bool king   = right % 2 == 0;
    const int rank    = white ? 0 : 7;
    const auto colour = white ? chess::Color::WHITE : chess::Color::BLACK;
    int king_file     = -1;
    for (int f = 0; f < 8; ++f) {
        if (editorAt(e, f, rank) == chess::Piece(chess::PieceType::KING, colour)) king_file = f;
    }
    const auto rook = chess::Piece(chess::PieceType::ROOK, colour);
    if (!e.chess960) return king_file == 4 && editorAt(e, king ? 7 : 0, rank) == rook;
    if (king_file < 1 || king_file > 6) return false;
    for (int f = king ? king_file + 1 : 0; f < (king ? 8 : king_file); ++f) {
        if (editorAt(e, f, rank) == rook) return true;
    }
    return false;
}

std::string editorFen(const PositionEditor& e) {
    std::string fen;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const auto piece = editorAt(e, file, rank);
            if (piece == chess::Piece::NONE) {
                ++empty;
                continue;
            }
            if (empty) fen += static_cast<char>('0' + empty);
            empty = 0;
            fen += static_cast<std::string>(piece);
        }
        if (empty) fen += static_cast<char>('0' + empty);
        if (rank > 0) fen += '/';
    }
    fen += e.stm == chess::Color::WHITE ? " w " : " b ";
    std::string castling;
    static constexpr char letters[] = {'K', 'Q', 'k', 'q'};
    for (int i = 0; i < 4; ++i) {
        if (e.castle[i] && editorCanCastle(e, i)) castling += letters[i];
    }
    fen += castling.empty() ? "-" : castling;
    // En passant: a pawn of the side not to move just went two squares, past an empty square.
    std::string ep = "-";
    if (e.ep_file >= 0 && e.ep_file < 8) {
        const bool white  = e.stm == chess::Color::WHITE;
        const int pawn_rk = white ? 4 : 3, behind = white ? 5 : 2, start = white ? 6 : 1;
        const auto pawn = chess::Piece(chess::PieceType::PAWN, white ? chess::Color::BLACK : chess::Color::WHITE);
        if (editorAt(e, e.ep_file, pawn_rk) == pawn && editorAt(e, e.ep_file, behind) == chess::Piece::NONE &&
            editorAt(e, e.ep_file, start) == chess::Piece::NONE) {
            ep = std::string(1, static_cast<char>('a' + e.ep_file)) + static_cast<char>('1' + behind);
        }
    }
    fen += " " + ep + " 0 " + std::to_string(std::max(1, e.fullmove));
    return fen;
}

bool editorLoad(PositionEditor& e, const std::string& fen) {
    // The placement alone may lack a king while it is edited: read by hand, the rest through the board.
    std::array<chess::Piece, 64> squares{};
    squares.fill(chess::Piece::NONE);
    const auto space = fen.find(' ');
    const auto place = fen.substr(0, space);
    int rank = 7, file = 0;
    for (const char c : place) {
        if (c == '/') {
            if (file != 8) return false;
            --rank;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
        } else {
            const auto piece = chess::Piece(std::string_view(&c, 1));
            if (piece == chess::Piece::NONE || rank < 0 || file > 7) return false;
            squares[static_cast<std::size_t>(rank * 8 + file)] = piece;
            ++file;
        }
        if (file > 8) return false;
    }
    if (rank != 0 || file != 8) return false;
    e.squares = squares;
    // The other fields, when there are.
    std::istringstream rest(space == std::string::npos ? std::string() : fen.substr(space + 1));
    std::string stm, castling, ep, half, full;
    rest >> stm >> castling >> ep >> half >> full;
    e.stm = stm == "b" ? chess::Color::BLACK : chess::Color::WHITE;
    for (int i = 0; i < 4; ++i) e.castle[i] = castling.find("KQkq"[i]) != std::string::npos;
    // Shredder-FEN file letters: a right on that side of the king.
    for (const char c : castling) {
        if (std::string_view("KQkq-").find(c) != std::string_view::npos) continue;
        const bool white = c >= 'A' && c <= 'H';
        const int f      = std::tolower(static_cast<unsigned char>(c)) - 'a';
        int king_file    = 4;
        for (int k = 0; k < 8; ++k) {
            if (editorAt(e, k, white ? 0 : 7) ==
                chess::Piece(chess::PieceType::KING, white ? chess::Color::WHITE : chess::Color::BLACK))
                king_file = k;
        }
        e.castle[(white ? 0 : 2) + (f > king_file ? 0 : 1)] = true;
    }
    e.ep_file  = ep.size() == 2 && ep[0] >= 'a' && ep[0] <= 'h' ? ep[0] - 'a' : -1;
    e.fullmove = 1;
    try {
        if (!full.empty()) e.fullmove = std::clamp(std::stoi(full), 1, 9999);
    } catch (...) {
    }
    e.fen_text = editorFen(e);
    return true;
}

std::vector<std::optional<int64_t>> moveClocks(const ReplayGame& game) {
    std::vector<std::optional<int64_t>> out(game.moves.size());
    // The time control: "300+2", "10+0.1", "600".
    std::optional<double> base;
    double increment = 0;
    {
        const auto tc   = game.header("TimeControl");
        const auto plus = tc.find('+');
        try {
            if (!tc.empty() && tc.find_first_of("/:") == std::string::npos && tc != "-" && tc != "?") {
                base = std::stod(tc.substr(0, plus));
                if (plus != std::string::npos) increment = std::stod(tc.substr(plus + 1));
            }
        } catch (...) {
            base.reset();
        }
    }
    std::array<std::optional<double>, 2> left;
    if (base) left = {*base * 1000.0, *base * 1000.0};
    for (std::size_t i = 0; i < game.moves.size(); ++i) {
        const auto& m  = game.moves[i];
        const int side = m.mover == chess::Color::WHITE ? 0 : 1;
        auto& clock    = left[static_cast<std::size_t>(side)];
        if (m.clock_ms) {
            clock = static_cast<double>(*m.clock_ms);
        } else if (clock && !m.book) {
            if (m.time_ms < 0) {
                clock.reset();  // a move without its time: the clock is lost from here
            } else {
                clock = std::max(0.0, *clock - static_cast<double>(m.time_ms)) + increment * 1000.0;
            }
        }
        if (clock) out[i] = static_cast<int64_t>(std::lround(*clock));
    }
    return out;
}

const char* tablebaseScore(int result, chess::Color stm) {
    if (result == 0) return "½-½";
    const bool white_wins = (result > 0) == (stm == chess::Color::WHITE);
    return white_wins ? "1-0" : "0-1";
}

std::vector<int8_t> tablebaseResults(const ReplayGame& game, bool chess960, const std::string& paths) {
    std::vector<int8_t> out(game.moves.size() + 1, -2);
    if (paths.empty()) return out;
    const auto lock   = lockSyzygy();
    const int largest = loadSyzygy(paths);
    if (largest <= 0) return out;
    auto board = boardAt(game, chess960, 0);
    for (std::size_t p = 0; p <= game.moves.size(); ++p) {
        if (board.occ().count() <= largest) {
            // The tables are exact right after a capture or a pawn move: each position as if it were one, which only
            // errs where the 50-move rule is near.
            auto zeroed = board;
            if (board.halfMoveClock() != 0) {
                std::istringstream fields(board.getFen());
                std::string placement, side, castling, ep;
                fields >> placement >> side >> castling >> ep;
                zeroed.setFen(placement + " " + side + " " + castling + " " + ep + " 0 1");
            }
            const auto r = fastchess::canProbeSyzgyWdl(zeroed) ? fastchess::probeSyzygyWdl(zeroed, false)
                                                               : chess::GameResult::NONE;
            out[p]       = r == chess::GameResult::WIN    ? 1
                           : r == chess::GameResult::DRAW ? 0
                           : r == chess::GameResult::LOSE ? -1
                                                          : -2;
        }
        if (p < game.moves.size()) board.makeMove<true>(game.moves[p].move);
    }
    return out;
}

BoardPage::Snapshot boardSnapshot(const BoardPage& b) { return {b.tree, b.start_notes, b.line, b.ply}; }

void boardChanged(BoardPage& b, BoardPage::Snapshot before) {
    b.undo.push_back(std::move(before));
    if (b.undo.size() > 200) b.undo.erase(b.undo.begin());
    b.redo.clear();
    b.changes.change();
    if (b.sent) b.since_sent.change();  // the chapter on Lichess no longer has it (#79)
}

bool boardUndo(BoardPage& b, bool redo) {
    auto& from = redo ? b.redo : b.undo;
    if (from.empty()) return false;
    (redo ? b.undo : b.redo).push_back(boardSnapshot(b));
    auto s = std::move(from.back());
    from.pop_back();
    b.changes.undone(redo);
    if (b.sent) b.since_sent.undone(redo);
    b.tree        = std::move(s.tree);
    b.start_notes = std::move(s.start_notes);
    b.comment_edit.reset();
    b.line = s.line >= -1 && s.line < static_cast<int>(b.tree.variations.size()) ? s.line : -1;
    b.game = lineOf(b.tree, b.line);
    b.ply  = std::clamp(s.ply, 0, static_cast<int>(b.game.moves.size()));
    b.selected.reset();
    b.preview.reset();
    return true;
}

bool editComment(GameTree& tree, const BoardPage::CommentEdit& edit, std::string text) {
    const auto first = text.find_first_not_of(" \t");
    text             = first == std::string::npos ? "" : text.substr(first, text.find_last_not_of(" \t") - first + 1);
    MoveNotes* notes = notesAt(tree, edit.line, edit.ply);
    if (!notes) return false;
    if (edit.assistant) {
        // Corrected by the user, the assistant's comment becomes theirs: after the move's other comment, which stays as
        // it was (#60).
        if (notes->assistant == text) return false;
        correctAssistant(*notes, text);
        return true;
    }
    auto& slot = edit.before ? notes->before : notes->after;
    if (slot == text) return false;
    slot = std::move(text);
    return true;
}

}  // namespace fcg
