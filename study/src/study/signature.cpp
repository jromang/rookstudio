#include "signature.hpp"

#include <algorithm>
#include <bit>

namespace fcg::study {

namespace {

// What ScanBoard::on gives for a pawn: 1 + its kind, 6 more for Black's.
constexpr uint8_t WHITE_PAWN = 1 + PAWN;
constexpr uint8_t BLACK_PAWN = 1 + PAWN + 6;

uint8_t pawnOf(int entry) noexcept { return entry < 8 ? WHITE_PAWN : BLACK_PAWN; }

}  // namespace

SignatureMaker::SignatureMaker(const ScanBoard& start) noexcept {
    for (int i = 0; i < 16; ++i)
        signature_.home[i] = start.on(GameSignature::square(i)) == pawnOf(i) ? GameSignature::STAYED : uint8_t{0};
}

void SignatureMaker::before(const ScanBoard& board, const CodedMove& move, uint32_t ply) noexcept {
    if (move.flags & (MOVE_NULL | MOVE_SHORT_CASTLE | MOVE_LONG_CASTLE)) return;
    if (move.promotion != NO_KIND) signature_.promoted = true;
    const uint32_t gone = ply + 1;
    if (gone >= GameSignature::STAYED) return;  // told as STAYED
    // The pawn that moves from its square, or the pawn taken on its square.
    for (const int square : {static_cast<int>(move.from), static_cast<int>(move.to)}) {
        const int e = GameSignature::entry(square);
        if (e >= 0 && signature_.home[e] == GameSignature::STAYED && board.on(square) == pawnOf(e))
            signature_.home[e] = static_cast<uint8_t>(gone);
    }
}

GameSignature SignatureMaker::finish(const ScanBoard& end) const noexcept {
    GameSignature s = signature_;
    for (int side = 0; side < 2; ++side)
        for (int kind = PAWN; kind <= QUEEN; ++kind)
            s.material[side * 5 + kind] = static_cast<uint8_t>(std::min(15, std::popcount(end.pieces(side, kind))));
    return s;
}

std::optional<GameSignature> signatureOf(const ScanBoard& start, std::span<const uint8_t> codes, uint32_t plies) {
    ScanBoard board = start;
    SignatureMaker maker(board);
    std::size_t at = 0;
    for (uint32_t ply = 0; ply < plies; ++ply) {
        const int count = board.codeCount();
        if (at >= codes.size()) return std::nullopt;
        int code = codes[at++];
        if (count > 256) {
            if (at >= codes.size()) return std::nullopt;
            code = (code << 8) | codes[at++];
        }
        if (code >= count) return std::nullopt;
        const auto move = board.decode(code);
        maker.before(board, move, ply);
        board.play(move);
    }
    return maker.finish(board);
}

std::optional<PlyWindow> plyWindow(const GameSignature& game, uint32_t plies, const ScanBoard& position) {
    int64_t from = 0, to = plies;
    for (int e = 0; e < 16; ++e) {
        const uint8_t gone = game.home[e];
        if (position.on(GameSignature::square(e)) == pawnOf(e)) {
            if (gone != GameSignature::STAYED) to = std::min<int64_t>(to, int64_t{gone} - 1);  // before it left
        } else {
            from = std::max<int64_t>(from, gone);  // once it left (STAYED: if ever, after ply 254)
        }
    }
    if (from > to) return std::nullopt;
    // A side's pawns, and its men all told, only ever go down: the position has as many as the end, or more.
    for (int side = 0; side < 2; ++side) {
        int men = 0, final_men = 0;
        bool saturated = false;
        for (int kind = PAWN; kind <= QUEEN; ++kind) {
            const int here  = std::popcount(position.pieces(side, kind));
            const int there = game.material[side * 5 + kind];
            if ((kind == PAWN || !game.promoted) && there < 15 && here < there) return std::nullopt;
            men += here;
            final_men += there;
            saturated = saturated || there >= 15;
        }
        if (!saturated && men < final_men) return std::nullopt;
    }
    return PlyWindow{static_cast<uint32_t>(from), static_cast<uint32_t>(to)};
}

}  // namespace fcg::study
