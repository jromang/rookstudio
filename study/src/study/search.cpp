#include "search.hpp"

#include <algorithm>
#include <bit>

#include <chess.hpp>

#include "signature.hpp"

namespace fcg::study {

namespace {

// The games a thread takes at a time: their search takes far less than a twentieth of a second.
constexpr uint64_t PART = 4096;

// The board a game starts from; false when its start cannot be set.
bool startOf(const Snapshot& snap, GameId id, uint16_t flags, ScanBoard& board) {
    static const ScanBoard standard = [] {
        ScanBoard b;
        b.setFen(chess::constants::STARTPOS, false);
        return b;
    }();
    static const ScanBoard standard960 = [] {
        ScanBoard b;
        b.setFen(chess::constants::STARTPOS, true);
        return b;
    }();
    const bool chess960 = (flags & GAME_CHESS960) != 0;
    if (flags & GAME_FEN) return board.setFen(snap.game(id).start_fen, chess960);
    board = chess960 ? standard960 : standard;
    return true;
}

// Calls at(ply, board) for the start and after each move, up to ply `last`, until it returns true: true then.
template <typename At>
bool replay(std::span<const uint8_t> codes, uint32_t plies, uint32_t last, ScanBoard& board, At&& at) {
    std::size_t p = 0;
    for (uint32_t ply = 0;; ++ply) {
        if (at(ply, static_cast<const ScanBoard&>(board))) return true;
        if (ply >= last || ply >= plies || p >= codes.size()) return false;
        const int count = board.codeCount();
        int code        = codes[p++];
        if (count > 256) {
            if (p >= codes.size()) return false;
            code = (code << 8) | codes[p++];
        }
        if (code >= count) return false;
        board.play(board.decode(code));
    }
}

bool samePosition(const ScanBoard& a, const ScanBoard& b) noexcept {
    if (a.key() != b.key() || a.sideToMove() != b.sideToMove()) return false;
    for (int side = 0; side < 2; ++side)
        for (int kind = PAWN; kind <= KING; ++kind)
            if (a.pieces(side, kind) != b.pieces(side, kind)) return false;
    return true;  // castling and en passant are in the key
}

std::optional<uint32_t> findPosition(const Snapshot& snap, GameId id, const ScanBoard& target, bool use_signature) {
    const auto m  = snap.gameMoves(id);
    uint32_t from = 0, to = m.plies;
    if (use_signature) {
        if (const auto signature = snap.signature(id)) {
            const auto window = plyWindow(*signature, m.plies, target);
            if (!window) return std::nullopt;
            from = window->from;
            to   = std::min(window->to, m.plies);
        }
    }
    ScanBoard board;
    if (!startOf(snap, id, m.flags, board)) return std::nullopt;
    const uint32_t parity = static_cast<uint32_t>((target.sideToMove() + 2 - board.sideToMove()) % 2);
    std::optional<uint32_t> hit;
    replay(m.codes, m.plies, to, board, [&](uint32_t ply, const ScanBoard& b) {
        if (ply < from || ply % 2 != parity || !samePosition(b, target)) return false;
        hit = ply;
        return true;
    });
    return hit;
}

bool materialFits(const ScanBoard& b, const MaterialQuery& q) noexcept {
    for (int side = 0; side < 2; ++side) {
        for (int kind = PAWN; kind <= QUEEN; ++kind) {
            const int count = std::popcount(b.pieces(side, kind));
            const int i     = side * 5 + kind;
            if (count < q.low[i] || count > q.high[i]) return false;
        }
    }
    return true;
}

std::optional<uint32_t> findMaterial(const Snapshot& snap, GameId id, const MaterialQuery& q, bool use_signature) {
    const auto m = snap.gameMoves(id);
    if (use_signature) {
        if (const auto signature = snap.signature(id)) {
            // A side's pawns and men all told, and without promotions each kind, never go below the end's.
            for (int side = 0; side < 2; ++side) {
                int ending = 0, most = 0;
                for (int kind = PAWN; kind <= QUEEN; ++kind) {
                    const int i = side * 5 + kind;
                    if ((kind == PAWN || !signature->promoted) && signature->material[i] > q.high[i])
                        return std::nullopt;
                    ending += signature->material[i];
                    most += q.high[i];
                }
                if (ending > most) return std::nullopt;
            }
        }
    }
    ScanBoard board;
    if (!startOf(snap, id, m.flags, board)) return std::nullopt;
    const uint32_t needed = std::max<uint32_t>(1, q.plies);
    uint32_t run = 0, first = 0;
    std::optional<uint32_t> hit;
    replay(m.codes, m.plies, m.plies, board, [&](uint32_t ply, const ScanBoard& b) {
        if (!materialFits(b, q)) {
            run = 0;
            return false;
        }
        if (run++ == 0) first = ply;
        if (run < needed) return false;
        hit = first;
        return true;
    });
    return hit;
}

bool patternFits(const ScanBoard& b, const PatternQuery& q) noexcept {
    if (q.side_to_move >= 0 && b.sideToMove() != q.side_to_move) return false;
    if (b.occupied() & q.empty) return false;
    for (int side = 0; side < 2; ++side) {
        for (int kind = PAWN; kind <= KING; ++kind) {
            const uint64_t wanted = q.men[side][kind];
            if ((b.pieces(side, kind) & wanted) != wanted) return false;
        }
        if (q.only_these_pawns[side] && b.pieces(side, PAWN) != q.men[side][PAWN]) return false;
    }
    return true;
}

// The plies at which a game may show a pattern, as its signature tells: its home pawns' squares, and its pawns when a
// side has only those given.
std::optional<PlyWindow> patternWindow(const GameSignature& game, uint32_t plies, const PatternQuery& q) {
    int64_t from = 0, to = plies;
    for (int e = 0; e < 16; ++e) {
        const int side      = e < 8 ? WHITE_SIDE : BLACK_SIDE;
        const uint64_t bit  = uint64_t{1} << GameSignature::square(e);
        const bool its_pawn = (q.men[side][PAWN] & bit) != 0;
        bool something_else = (q.empty & bit) != 0 || (q.only_these_pawns[side] && !its_pawn);
        for (int s = 0; s < 2; ++s)
            for (int kind = PAWN; kind <= KING; ++kind)
                if ((s != side || kind != PAWN) && (q.men[s][kind] & bit)) something_else = true;
        const uint8_t gone = game.home[e];
        if (its_pawn) {
            if (gone != GameSignature::STAYED) to = std::min<int64_t>(to, int64_t{gone} - 1);
        } else if (something_else) {
            from = std::max<int64_t>(from, gone);
        }
    }
    for (int side = 0; side < 2; ++side) {
        const uint8_t ending = game.material[side * 5 + PAWN];
        if (q.only_these_pawns[side] && ending < 15 && std::popcount(q.men[side][PAWN]) < ending) return std::nullopt;
    }
    if (from > to) return std::nullopt;
    return PlyWindow{static_cast<uint32_t>(from), static_cast<uint32_t>(to)};
}

std::optional<uint32_t> findPattern(const Snapshot& snap, GameId id, const PatternQuery& q, bool use_signature) {
    const auto m  = snap.gameMoves(id);
    uint32_t from = 0, to = m.plies;
    if (use_signature) {
        if (const auto signature = snap.signature(id)) {
            const auto window = patternWindow(*signature, m.plies, q);
            if (!window) return std::nullopt;
            from = window->from;
            to   = std::min(window->to, m.plies);
        }
    }
    ScanBoard board;
    if (!startOf(snap, id, m.flags, board)) return std::nullopt;
    std::optional<uint32_t> hit;
    replay(m.codes, m.plies, to, board, [&](uint32_t ply, const ScanBoard& b) {
        if (ply < from || !patternFits(b, q)) return false;
        hit = ply;
        return true;
    });
    return hit;
}

}  // namespace

std::optional<uint32_t> searchGame(const Snapshot& snapshot, GameId id, const SearchQuery& query, bool use_signature) {
    if (const auto* p = std::get_if<PositionQuery>(&query))
        return findPosition(snapshot, id, p->position, use_signature);
    if (const auto* m = std::get_if<MaterialQuery>(&query)) return findMaterial(snapshot, id, *m, use_signature);
    return findPattern(snapshot, id, std::get<PatternQuery>(query), use_signature);
}

Search::Search(std::shared_ptr<const Snapshot> snapshot, SearchQuery query, int threads)
    : snapshot_(std::move(snapshot)), query_(std::move(query)) {
    parts_.resize((snapshot_->games() + PART - 1) / PART);
    const int machine = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    const auto count  = std::min<std::size_t>(static_cast<std::size_t>(threads > 0 ? threads : machine), parts_.size());
    threads_.reserve(count);
    for (std::size_t t = 0; t < count; ++t) threads_.emplace_back([this] { work(); });
}

Search::~Search() {
    cancel();
    wait();
}

void Search::cancel() noexcept { cancel_ = true; }

void Search::wait() {
    for (auto& t : threads_)
        if (t.joinable()) t.join();
}

void Search::work() {
    const uint64_t n = snapshot_->games();
    for (;;) {
        const uint64_t part = next_++;
        if (cancel_ || part >= parts_.size()) break;
        std::vector<SearchHit> found;
        const uint64_t from = part * PART, to = std::min(n, from + PART);
        uint64_t looked = 0;
        for (GameId id = from; id < to && !cancel_; ++id, ++looked)
            if (const auto ply = searchGame(*snapshot_, id, query_)) found.push_back({id, *ply});
        searched_ += looked;
        std::lock_guard lock(mutex_);
        parts_[part] = std::move(found);
    }
    ++finished_;
}

std::vector<SearchHit> Search::hits() const {
    std::lock_guard lock(mutex_);
    std::vector<SearchHit> all;
    for (const auto& part : parts_) all.insert(all.end(), part.begin(), part.end());
    return all;
}

std::vector<SearchHit> searchAll(std::shared_ptr<const Snapshot> snapshot, const SearchQuery& query, int threads) {
    Search search(std::move(snapshot), query, threads);
    search.wait();
    return search.hits();
}

}  // namespace fcg::study
