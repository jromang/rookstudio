// The check of a database (Database::check()), and what the database found when it was opened said as a check says
// it (unreadableReport()).

#include "store.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <chess.hpp>

#include "mapped_file.hpp"
#include "parallel.hpp"
#include "scan_board.hpp"
#include "signature.hpp"
#include "store_detail.hpp"
#include "tree.hpp"

namespace fcg::study {

using namespace store;

CheckReport unreadableReport(const Snapshot& snapshot) {
    CheckReport report;
    report.games    = snapshot.games() - snapshot.deletedCount();
    report.segments = snapshot.unreadable();
    report.damaged  = snapshot.unreadableIds();
    for (const auto id : report.damaged) ++report.damaged_sources[snapshot.source(id)];
    return report;
}

namespace {

// Whether segment record i reads as it was written: its names within the database's, its offsets within its files,
// its annex whole, its moves replayed to their end, its signature and its key made again the same.
bool recordSound(const Snapshot& snap, const Segment& segment, std::size_t i) {
    try {
        for (int c = 0; c < NAME_COLUMNS; ++c)
            if (segment.value<uint32_t>(static_cast<Column>(c), i) >= snap.names()) return false;
        if (!segment.offsetsSound(i)) return false;
        const auto game  = recordGame(snap, segment, i, true);  // throws on a damaged annex
        const auto flags = segment.value<uint16_t>(FLAGS, i);
        ScanBoard board;
        const bool start = board.setFen(
            game.start_fen.empty() ? std::string_view(chess::constants::STARTPOS) : std::string_view(game.start_fen),
            game.chess960);
        if (!start) {
            if (!(flags & GAME_DAMAGED) || game.plies != 0 || !game.moves.empty()) return false;
        } else {
            SignatureMaker signature(board);
            Cursor cursor(game.moves, game.plies);
            for (uint32_t ply = 0;; ++ply) {
                const int code = cursor.next(board);
                if (code == Cursor::END) break;
                const auto move = board.decode(code);
                signature.before(board, move, ply);
                board.play(move);
            }
            // Every ply read, and nothing after them.
            if (cursor.ply() < game.plies || cursor.bytes() != game.moves.size()) return false;
            if (const auto* stored = segment.signatureBytes(i); stored && (stored[21] & SIG_KNOWN)) {
                // Its plies and material as made again, and whether a pawn was promoted (its flags' other bits left).
                const auto again = packSignature(signature.finish(board));
                if (std::memcmp(stored, again.data(), 21) != 0 || ((stored[21] ^ again[21]) & SIG_PROMOTED))
                    return false;
            }
        }
        // A name lost with a segment that cannot be read reads "?": the key made of it cannot be made again.
        const bool known =
            snap.nameKnown(segment.value<uint32_t>(WHITE, i)) && snap.nameKnown(segment.value<uint32_t>(BLACK, i)) &&
            snap.nameKnown(segment.value<uint32_t>(EVENT, i)) && snap.nameKnown(segment.value<uint32_t>(SITE, i)) &&
            snap.nameKnown(segment.value<uint32_t>(ROUND, i));
        return !known || duplicateKey(game) == segment.value<uint64_t>(KEY, i);
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace

CheckReport Database::check(CheckProgress& progress, const std::atomic<bool>& cancel, int threads) const {
    const auto started = std::chrono::steady_clock::now();
    const auto snap    = snapshot();
    CheckReport report;
    const auto infos = snap->segmentInfos();
    uint64_t total   = 0;
    for (const auto& info : infos) total += info.games;
    progress.total = total;
    progress.done  = 0;
    threads        = threadCount(threads);
    // The game a record is the current version of, if any.
    std::map<std::pair<uint32_t, uint32_t>, GameId> current_version;
    for (const auto& v : snap->replaced_) current_version[{v.segment, v.local}] = v.id;
    const auto replaced = [&](GameId id) {
        const auto v = std::lower_bound(snap->replaced_.begin(), snap->replaced_.end(), id,
                                        [](const Snapshot::Version& r, GameId x) { return r.id < x; });
        return v != snap->replaced_.end() && v->id == id;
    };
    std::vector<GameId> damaged;
    std::mutex found;
    std::atomic<uint64_t> checked{0};
    for (std::size_t s = 0; s < infos.size() && !cancel; ++s) {
        const auto& info = infos[s];
        // The game record i is, when it is one as the database reads it; none for an old version.
        const auto gameOf = [&](std::size_t i) -> std::optional<GameId> {
            if (info.versions) {
                const auto it = current_version.find({static_cast<uint32_t>(s), static_cast<uint32_t>(i)});
                if (it == current_version.end()) return std::nullopt;
                return it->second;
            }
            const GameId id = info.first + i;
            if (replaced(id)) return std::nullopt;
            return id;
        };
        std::shared_ptr<const Segment> segment;
        const auto names = snap->segments_[s]->nameCount();
        try {
            segment = std::make_shared<const Segment>(dir_ / info.name, info.games, names);
        } catch (const std::exception& e) {
            MappedFile file;
            std::vector<uint32_t> offsets;
            const bool names_kept =
                names == 0 || Segment::readNames(dir_ / info.name / "names.bin", names, file, offsets);
            report.segments.push_back(
                {info.name, info.first, info.games, info.versions, e.what(), info.source, names_kept});
            for (std::size_t i = 0; i < info.games; ++i)
                if (const auto id = gameOf(i); id && !snap->deletedByEdit(*id)) damaged.push_back(*id);
            progress.done += info.games;
            continue;
        }
        if (segment->version() < 2 && info.games > 0) report.signatures = false;
        std::atomic<std::size_t> next{0};
        const auto parts = std::min<std::size_t>(static_cast<std::size_t>(threads), info.games / 4096 + 1);
        onThreads(static_cast<int>(parts), [&](int) {
            std::vector<GameId> mine;
            for (;;) {
                const auto from = next.fetch_add(4096);
                if (from >= info.games || cancel.load(std::memory_order_relaxed)) break;
                const auto to = std::min<std::size_t>(from + 4096, info.games);
                for (std::size_t i = from; i < to; ++i) {
                    const auto id = gameOf(i);
                    if (!id || snap->deleted(*id)) continue;  // an old version, or a game no one reads
                    ++checked;
                    if (!recordSound(*snap, *segment, i)) mine.push_back(*id);
                }
                progress.done += to - from;
            }
            std::lock_guard lock(found);
            damaged.insert(damaged.end(), mine.begin(), mine.end());
        });
    }
    report.games     = checked.load();
    report.cancelled = cancel.load();
    std::sort(damaged.begin(), damaged.end());
    damaged.erase(std::unique(damaged.begin(), damaged.end()), damaged.end());
    for (const auto id : damaged) ++report.damaged_sources[snap->source(id)];
    report.damaged = std::move(damaged);
    try {
        const auto tree = Tree::open(dir_);
        report.tree     = tree && !tree->stale(*snap) && tree->games() == snap->games();
    } catch (const std::exception&) {
        report.tree = false;
    }
    report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return report;
}

}  // namespace fcg::study
