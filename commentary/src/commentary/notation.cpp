#include "notation.hpp"

namespace fcg::commentary {

const char* pieceLetter(PieceLetters letters, Role role) noexcept {
    static constexpr const char* french[]    = {"", "C", "F", "T", "D", "R"};
    static constexpr const char* english[]   = {"", "N", "B", "R", "Q", "K"};
    static constexpr const char* figurines[] = {"", "♞", "♝", "♜", "♛", "♚"};
    const auto i                             = static_cast<int>(role);
    switch (letters) {
        case PieceLetters::French:
            return french[i];
        case PieceLetters::English:
            return english[i];
        default:
            return figurines[i];
    }
}

std::string san(const Position& before, const Move& mv, PieceLetters letters) {
    // The English SAN, its piece letters (capitals: squares are small letters, castling an O) written again.
    const auto english = before.sanPlus(mv);
    std::string out;
    for (const char c : english) {
        const auto role = c == 'N'   ? Role::Knight
                          : c == 'B' ? Role::Bishop
                          : c == 'R' ? Role::Rook
                          : c == 'Q' ? Role::Queen
                          : c == 'K' ? Role::King
                                     : Role::Pawn;
        if (role == Role::Pawn)
            out += c;
        else
            out += pieceLetter(letters, role);
    }
    return out;
}

std::string moveNumber(uint32_t fullmove, Color mover) {
    return std::to_string(fullmove) + (mover == Color::White ? "." : "…");
}

std::string moveText(const Position& before, const Move& mv, PieceLetters letters) {
    return moveNumber(before.fullmoves(), before.turn()) + san(before, mv, letters);
}

std::string lineText(const Position& start, std::span<const Move> moves, std::size_t plies, PieceLetters letters) {
    auto pos = start;
    std::string out;
    for (std::size_t i = 0; i < moves.size() && i < plies; ++i) {
        if (i) out += ' ';
        out += i == 0 || pos.turn() == Color::White ? moveText(pos, moves[i], letters) : san(pos, moves[i], letters);
        pos.playUnchecked(moves[i]);
    }
    return out;
}

}  // namespace fcg::commentary
