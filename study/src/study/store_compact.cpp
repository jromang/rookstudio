// The compaction (Database::compact()) and Keep what can be read (Database::keepReadable()): segments written again
// from those of a snapshot, committed in one manifest, undone while nothing else was committed.

#include "store.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <chess.hpp>

#include "scan_board.hpp"
#include "signature.hpp"
#include "store_detail.hpp"
#include "store_error.hpp"

namespace fcg::study {

namespace fs = std::filesystem;

using namespace store;

namespace {

// Whether a deleted game's record still holds its moves or its annex: what a compaction takes away.
bool heldDeleted(const Snapshot& snap, GameId id) { return snap.deleted(id) && snap.storedBytes(id) > 0; }

// Whether a logical segment holds a game replaced, or a deleted game not emptied yet.
bool staleSegment(const Snapshot& snap, const Snapshot::SegmentInfo& info) {
    if (info.versions) return false;
    for (GameId id = info.first; id < info.first + info.games; ++id)
        if (snap.state(id).version || heldDeleted(snap, id)) return true;
    return false;
}

}  // namespace

CompactPlan Database::compactPlan() const {
    const auto snap = snapshot();
    CompactPlan plan;
    if (!snap->unreadable().empty()) return plan;  // keepReadable() first: those segments cannot be copied
    const auto infos = snap->segmentInfos();
    // What the segments written again take now, and what their new files will take.
    uint64_t before = 0, after = 0;
    for (std::size_t s = 0; s < infos.size(); ++s) {
        const auto& info = infos[s];
        const auto& seg  = *snap->segments_[s];
        if (info.versions) {
            if (info.games == 0) continue;  // its names only: compacted already
            plan.segments.push_back(info.name);
            before += seg.bytes();
            after += layout(0)[COLUMNS] + seg.namesText().size();
            continue;
        }
        if (!staleSegment(*snap, info)) continue;
        plan.segments.push_back(info.name);
        uint64_t kept = layout(info.games)[COLUMNS] + seg.namesText().size() + uint64_t{info.games} * SIG_BYTES;
        for (GameId id = info.first; id < info.first + info.games; ++id) {
            if (snap->deleted(id)) {
                if (snap->storedBytes(id) > 0) ++plan.deleted;
                continue;
            }
            if (snap->state(id).version) ++plan.replaced;
            kept += snap->storedBytes(id);
        }
        before += seg.bytes();
        after += kept;
    }
    if (plan.segments.empty()) return CompactPlan{};
    plan.needed = after;
    plan.bytes  = before > after ? before - after : 0;
    return plan;
}

namespace {

// A segment's files written in folder `tmp`: `n` records, each copied from (segment, record) or emptied when deleted,
// its names those of `names`. A record without a segment is a game set aside whose record could not be read: no
// headers, its names `blank_name`.
struct RecordSource {
    const Segment* segment = nullptr;
    std::size_t index      = 0;
    bool emptied           = false;  // a deleted game: its headers only
};

void writeSegment(const fs::path& tmp, const std::vector<RecordSource>& records, std::string_view names,
                  uint32_t name_count, const std::atomic<bool>& cancel, std::atomic<uint64_t>& done,
                  uint32_t blank_name = 0) {
    const auto n  = records.size();
    const auto at = layout(n);
    std::string cols(at[COLUMNS], '\0'), moves, annex, sig(n * SIG_BYTES, '\0');
    std::memcpy(cols.data(), MAGIC.data(), MAGIC.size());
    const auto put32 = [&](std::size_t offset, uint32_t v) { std::memcpy(cols.data() + offset, &v, 4); };
    const auto put   = [&](Column column, std::size_t i, auto v) {
        std::memcpy(cols.data() + at[column] + i * sizeof(v), &v, sizeof(v));
    };
    put32(8, VERSION);
    put32(12, static_cast<uint32_t>(n));
    put32(16, name_count);
    for (std::size_t k = 0; k < n; ++k) {
        if ((k & 0xFFF) == 0 && cancel.load(std::memory_order_relaxed))
            throw StoreError(StoreErrc::Cancelled, "cancelled");
        const auto& r = records[k];
        if (!r.segment) {  // set aside: nothing kept of it but its number
            for (int c = 0; c < NAME_COLUMNS; ++c) put(static_cast<Column>(c), k, blank_name);
            put(RESULT, k, static_cast<uint8_t>('*'));
            put(MOVES, k, static_cast<uint64_t>(moves.size()));
            put(ANNEX, k, static_cast<uint64_t>(annex.size()));
            ++done;
            continue;
        }
        const auto& seg = *r.segment;
        for (int c = 0; c < NAME_COLUMNS; ++c)
            put(static_cast<Column>(c), k, seg.value<uint32_t>(static_cast<Column>(c), r.index));
        put(DATE, k, seg.value<uint32_t>(DATE, r.index));
        put(WHITE_ELO, k, seg.value<uint16_t>(WHITE_ELO, r.index));
        put(BLACK_ELO, k, seg.value<uint16_t>(BLACK_ELO, r.index));
        put(ECO, k, seg.value<uint16_t>(ECO, r.index));
        put(RESULT, k, seg.value<uint8_t>(RESULT, r.index));
        put(KEY, k, seg.value<uint64_t>(KEY, r.index));
        put(MOVES, k, static_cast<uint64_t>(moves.size()));
        put(ANNEX, k, static_cast<uint64_t>(annex.size()));
        if (r.emptied) {
            put(PLIES, k, uint32_t{0});
            put(FLAGS, k, uint16_t{0});
        } else {
            put(PLIES, k, seg.value<uint32_t>(PLIES, r.index));
            put(FLAGS, k, seg.value<uint16_t>(FLAGS, r.index));
            const auto m = seg.moves(r.index), a = seg.annex(r.index);
            moves.append(reinterpret_cast<const char*>(m.data()), m.size());
            annex.append(reinterpret_cast<const char*>(a.data()), a.size());
            if (const auto* bytes = seg.signatureBytes(r.index)) {
                std::memcpy(sig.data() + k * SIG_BYTES, bytes, SIG_BYTES);
            } else {
                // A segment of the first format: the signature made from the moves.
                ScanBoard start;
                const auto flags = seg.value<uint16_t>(FLAGS, r.index);
                std::string fen;
                if (flags & GAME_FEN) {
                    const uint8_t* p   = a.data();
                    const uint8_t* end = p + a.size();
                    if (p < end && (*p++ & ANNEX_FEN)) fen = getText(p, end);
                }
                if (start.setFen(fen.empty() ? std::string_view(chess::constants::STARTPOS) : std::string_view(fen),
                                 flags & GAME_CHESS960))
                    if (const auto signature = signatureOf(start, m, seg.value<uint32_t>(PLIES, r.index))) {
                        char* out = sig.data() + k * SIG_BYTES;
                        std::memcpy(out, signature->home.data(), 16);
                        for (int j = 0; j < 5; ++j)
                            out[16 + j] =
                                static_cast<char>((signature->material[2 * j] & 0x0F) |
                                                  (std::min<uint8_t>(signature->material[2 * j + 1], 15) << 4));
                        out[21] = static_cast<char>(SIG_KNOWN | (signature->promoted ? SIG_PROMOTED : 0));
                    }
            }
        }
        ++done;
    }
    put(MOVES, n, static_cast<uint64_t>(moves.size()));
    put(ANNEX, n, static_cast<uint64_t>(annex.size()));
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    if (ec) throw StoreError(writeErrc(ec), "cannot create " + tmp.string());
    writeFile(tmp / "cols.bin", cols);
    writeFile(tmp / "moves.bin", moves);
    writeFile(tmp / "annex.bin", annex);
    writeFile(tmp / "names.bin", names);
    writeFile(tmp / "sig.bin", sig);
    syncFolder(tmp);
}

}  // namespace

Compacted Database::compact(CompactProgress& progress, const std::atomic<bool>& cancel, bool one_at_a_time) {
    Compacted out;
    if (!lock_) {
        out.error      = readOnlyError(dir_).what();
        out.error_code = StoreErrc::ReadOnly;
        return out;
    }
    // One compaction at a time. The segments are written without the writers' lock, from the snapshot of when it
    // starts: games added meanwhile are kept; a game replaced or deleted meanwhile stops it (compact again).
    std::lock_guard compacting(compact_mutex_);
    auto snap = snapshot();
    if (!snap->unreadable().empty()) {
        out.error      = "a segment cannot be read: keep what can be read first";
        out.error_code = StoreErrc::Damaged;
        return out;
    }
    const auto infos = snap->segmentInfos();
    const auto plan  = compactPlan();
    std::vector<std::string> unremoved;  // one segment at a time: old files still mapped, removed at the end
    if (plan.segments.empty()) {
        out.done = true;
        return out;
    }
    if (!one_at_a_time) {
        std::error_code ec;
        const auto space = fs::space(dir_, ec);
        if (!ec && space.available < plan.needed + (uint64_t{64} << 20)) {
            out.error      = "disk full";
            out.error_code = StoreErrc::DiskFull;
            return out;
        }
    }
    const auto manifest_before = Manifest::read(dir_);  // what undoCompaction() puts back
    // The steps: all the segments at once; or each logical segment, then the segments of versions.
    std::vector<std::size_t> logical_stale, versions;
    for (std::size_t s = 0; s < infos.size(); ++s) {
        if (infos[s].versions) {
            if (infos[s].games > 0) versions.push_back(s);
        } else if (staleSegment(*snap, infos[s])) {
            logical_stale.push_back(s);
        }
    }
    std::vector<std::vector<std::size_t>> steps;
    if (one_at_a_time) {
        for (const auto s : logical_stale) steps.push_back({s});
        if (!versions.empty()) steps.push_back(versions);
    } else {
        auto all = logical_stale;
        all.insert(all.end(), versions.begin(), versions.end());
        steps.push_back(all);
    }
    progress.segments = logical_stale.size() + versions.size();
    progress.segment  = 0;
    auto replaced     = snap->replaced_;  // what stays replaced, the segments folded so far taken out
    std::vector<std::string> old_files, new_files;
    uint64_t old_bytes    = 0;
    std::string edits_now = manifest_before.edits;  // the edits file the manifest names
    for (const auto& batch : steps) {
        std::vector<std::pair<std::size_t, std::string>> written;  // a segment's index, its new folder
        std::string edits_written;
        std::string tmp_now;
        const auto clean = [&] {
            std::error_code ec;
            for (const auto& [s, name] : written) fs::remove_all(dir_ / name, ec);
            if (!tmp_now.empty()) fs::remove_all(dir_ / tmp_now, ec);
            if (!edits_written.empty()) fs::remove(dir_ / edits_written, ec);
        };
        try {
            for (const auto s : batch) {
                ++progress.segment;
                progress.games   = 0;
                const auto& info = infos[s];
                const auto& seg  = *snap->segments_[s];
                std::vector<RecordSource> records;
                if (!info.versions) {
                    records.reserve(info.games);
                    for (GameId id = info.first; id < info.first + info.games; ++id) {
                        const auto [where, i] = snap->locate(id);
                        records.push_back({where, i, snap->deleted(id)});
                    }
                }
                std::string name;
                {
                    std::lock_guard lock(mutex_);  // the numbers the appends take too
                    name = segmentName(next_segment_++);
                }
                tmp_now = name + ".tmp";
                writeSegment(dir_ / tmp_now, records, seg.namesText(), seg.nameCount(), cancel, progress.games);
                step("rename segment");
                std::error_code ec;
                fs::rename(dir_ / tmp_now, dir_ / name, ec);
                if (ec) throw std::runtime_error("cannot rename " + (dir_ / tmp_now).string());
                tmp_now.clear();
                written.emplace_back(s, name);
            }
            syncFolder(dir_);
            if (cancel) throw StoreError(StoreErrc::Cancelled, "cancelled");
        } catch (const std::exception& e) {
            // Nothing of this step committed: its files go. Those of the steps before stay committed.
            clean();
            out.cancelled  = cancel.load();
            out.error      = out.cancelled ? std::string() : std::string(e.what());
            out.error_code = out.cancelled ? StoreErrc::Other : errorCode(e);
            return out;
        }
        {
            // The commit, under the writers' lock: the manifest as it is now (the segments added meanwhile kept)
            // naming the new segments, the edits without the versions folded in, the edits generation kept: the
            // games read the same, and the opening tree holds.
            std::lock_guard writing(write_mutex_);
            try {
                if (snapshot()->editsGeneration() != snap->editsGeneration())
                    throw std::runtime_error("a game was replaced or deleted meanwhile");
                auto next = Manifest::read(dir_);
                for (const auto& [s, name] : written) {
                    auto& entry = next.segments.at(s);
                    entry.name  = name;
                    if (infos[s].versions) {
                        entry.games = 0;
                    } else {
                        const auto first = infos[s].first, last = first + infos[s].games;
                        std::erase_if(replaced,
                                      [&](const Snapshot::Version& v) { return v.id >= first && v.id < last; });
                    }
                }
                const auto generation = next.generation + 1;
                if (!replaced.empty() || !snap->deleted_.empty()) {
                    edits_written = editsName(generation);
                    writeFile(dir_ / edits_written, editsBytes(replaced, snap->deleted_));
                    next.edits = edits_written;
                } else {
                    next.edits.clear();
                }
                next.generation = generation;
                step("write manifest");
                next.write(dir_);
            } catch (const std::exception& e) {
                clean();
                out.error      = e.what();
                out.error_code = errorCode(e);
                return out;
            }
            // Committed: read again, the old files kept for now.
            step("manifest written");
            load(false);
            keys_indexed_ = false;
        }
        std::vector<std::string> step_old;
        for (const auto& [s, name] : written) {
            step_old.push_back(infos[s].name);
            new_files.push_back(name);
            old_bytes += snap->segments_[s]->bytes();
        }
        if (!edits_written.empty()) new_files.push_back(edits_written);
        if (edits_now != edits_written && !edits_now.empty()) step_old.push_back(edits_now);
        edits_now = edits_written;
        if (one_at_a_time) {
            for (const auto& name : step_old) {
                std::error_code ec;
                fs::remove_all(dir_ / name, ec);
                if (ec) unremoved.push_back(name);  // Windows: mapped by the snapshot of the start
            }
        } else {
            old_files.insert(old_files.end(), step_old.begin(), step_old.end());
        }
    }
    // What was given back: the old segments' bytes less the new ones'.
    const auto now     = snapshot();
    uint64_t new_bytes = 0;
    for (const auto& name : new_files)
        for (std::size_t s = 0; s < now->segment_names_.size(); ++s)
            if (now->segment_names_[s] == name) new_bytes += now->segments_[s]->bytes();
    out.bytes = old_bytes > new_bytes ? old_bytes - new_bytes : 0;
    out.done  = true;
    if (one_at_a_time) {
        // The start's snapshot let go: what Windows kept mapped can go now (what a reader still maps, at the next
        // open).
        snap.reset();
        for (const auto& name : unremoved) {
            std::error_code ec;
            fs::remove_all(dir_ / name, ec);
        }
        return out;
    }
    // Undoing it puts the manifest before back, unless something was committed since: the segments added meanwhile
    // would go with it.
    Leftovers left;
    left.manifest   = now->generation() == snap->generation() + 1 ? manifest_before.text() : std::string();
    left.generation = now->generation();
    left.files      = old_files;
    left.new_files  = new_files;
    std::lock_guard lock(mutex_);
    leftovers_   = std::move(left);
    out.undoable = !leftovers_->manifest.empty();
    return out;
}

bool Database::compactionUndoable() const {
    std::lock_guard lock(mutex_);
    return leftovers_ && !leftovers_->manifest.empty() && current_->generation_ == leftovers_->generation;
}

bool Database::undoCompaction() {
    std::lock_guard compacting(compact_mutex_);
    std::lock_guard writing(write_mutex_);
    Manifest manifest;
    {
        std::lock_guard lock(mutex_);
        if (!leftovers_ || leftovers_->manifest.empty() || current_->generation_ != leftovers_->generation)
            return false;
        manifest            = Manifest::parse(leftovers_->manifest, dir_);
        manifest.generation = current_->generation_ + 1;  // readers see a commit
    }
    manifest.write(dir_);
    std::vector<std::string> made;
    {
        std::lock_guard lock(mutex_);
        made = leftovers_->new_files;
        leftovers_.reset();
    }
    load(true);  // the compaction's files, no longer named, go
    // Again, once the snapshot that mapped them is let go (Windows removes no mapped file); a reader holding it
    // still leaves them to the next open.
    for (const auto& name : made) {
        std::error_code ec;
        fs::remove_all(dir_ / name, ec);
    }
    keys_indexed_ = false;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// The segments that cannot be read, written again (#97).

uint64_t Database::keepReadable(std::span<const GameId> damaged) {
    if (!lock_) throw readOnlyError(dir_);
    std::lock_guard compacting(compact_mutex_);
    std::lock_guard writing(write_mutex_);
    const auto snap = snapshot();
    // What is set aside: the games that cannot be read, and the damaged ones given.
    auto deleted       = snap->deleted_;
    uint64_t set_aside = 0;
    const auto aside   = [&](GameId id) {
        if (id >= snap->games_ || (id / 64 < deleted.size() && (deleted[id / 64] >> (id % 64) & 1))) return;
        setBit(deleted, id, true);
        ++set_aside;
    };
    for (const auto id : snap->unreadableIds()) aside(id);
    for (const auto id : damaged) aside(id);
    if (snap->unreadable_segments_.empty() && set_aside == 0) return 0;

    const auto manifest_before = Manifest::read(dir_);  // what undoKeepReadable() puts back
    auto manifest              = manifest_before;
    const auto old_edits       = manifest.edits;
    const uint64_t generation  = snap->generation_ + 1;
    const auto blank          = snap->blankName();
    const uint32_t blank_name = blank.value_or(snap->names_);  // none: a name "" added after the others
    std::vector<std::string> written, old_files;
    std::string tmp_now, edits_written;
    try {
        const auto made = [&](std::vector<RecordSource> records, std::string_view names, uint32_t count) {
            std::string name;
            {
                std::lock_guard lock(mutex_);  // the numbers the appends take too
                name = segmentName(next_segment_++);
            }
            tmp_now = name + ".tmp";
            std::atomic<bool> go_on{false};
            std::atomic<uint64_t> done{0};
            writeSegment(dir_ / tmp_now, records, names, count, go_on, done, blank_name);
            step("rename segment");
            std::error_code ec;
            fs::rename(dir_ / tmp_now, dir_ / name, ec);
            if (ec) throw std::runtime_error("cannot rename " + (dir_ / tmp_now).string() + ": " + ec.message());
            tmp_now.clear();
            written.push_back(name);
            return name;
        };
        for (std::size_t s = 0; s < snap->segments_.size(); ++s) {
            const auto& segment = *snap->segments_[s];
            if (!segment.unreadable()) continue;
            // Its names as they read, or "?" for each when they cannot: the later games' ids of names hold.
            std::string names;
            if (segment.namesKnown()) {
                names = segment.namesText();
            } else {
                names.reserve(std::size_t{segment.nameCount()} * 2);
                for (uint32_t k = 0; k < segment.nameCount(); ++k) names += "?\n";
            }
            auto& entry = manifest.segments.at(s);
            old_files.push_back(entry.name);
            entry.name = made(std::vector<RecordSource>(segment.games()), names, segment.nameCount());
            if (!segment.namesKnown() && segment.nameCount() > 0) entry.unknown_names = true;
        }
        if (!blank && !snap->unreadable_segments_.empty()) {  // a segment of names only, as a compaction leaves them
            Manifest::Entry entry;
            entry.name     = made({}, "\n", 1);
            entry.names    = 1;
            entry.versions = true;
            manifest.segments.push_back(std::move(entry));
        }
        syncFolder(dir_);
        edits_written = editsName(generation);
        writeFile(dir_ / edits_written, editsBytes(snap->replaced_, deleted));
        manifest.edits            = edits_written;
        manifest.edits_generation = generation;
        manifest.generation       = generation;
        step("write manifest");
        manifest.write(dir_);
    } catch (const std::exception&) {
        // Nothing committed: what it wrote goes, the database as before.
        std::error_code ec;
        for (const auto& name : written) fs::remove_all(dir_ / name, ec);
        if (!tmp_now.empty()) fs::remove_all(dir_ / tmp_now, ec);
        if (!edits_written.empty()) fs::remove(dir_ / edits_written, ec);
        throw;
    }
    // Committed: read again, the damaged files kept until the database is closed (or the commit undone).
    step("manifest written");
    load(false);
    keys_indexed_  = false;
    names_indexed_ = false;
    name_ids_.clear();
    if (!old_edits.empty() && old_edits != edits_written) old_files.push_back(old_edits);
    Leftovers left;
    left.manifest   = manifest_before.text();
    left.generation = generation;
    left.files      = std::move(old_files);
    left.new_files  = std::move(written);
    left.new_files.push_back(edits_written);
    std::lock_guard lock(mutex_);
    if (kept_)  // an earlier one's, no longer undone: its files go at closing all the same
        dropped_.insert(dropped_.end(), kept_->files.begin(), kept_->files.end());
    kept_ = std::move(left);
    return set_aside;
}

bool Database::keepReadableUndoable() const {
    std::lock_guard lock(mutex_);
    return kept_ && !kept_->manifest.empty() && current_->generation_ == kept_->generation;
}

bool Database::undoKeepReadable() {
    std::lock_guard compacting(compact_mutex_);
    std::lock_guard writing(write_mutex_);
    Manifest manifest;
    std::vector<std::string> made;
    {
        std::lock_guard lock(mutex_);
        if (!kept_ || kept_->manifest.empty() || current_->generation_ != kept_->generation) return false;
        manifest            = Manifest::parse(kept_->manifest, dir_);
        manifest.generation = current_->generation_ + 1;  // readers see a commit
    }
    manifest.write(dir_);
    {
        std::lock_guard lock(mutex_);
        made = kept_->new_files;
        kept_.reset();
    }
    load(true);                      // the files it wrote, no longer named, go; those it set aside are named again
    for (const auto& name : made) {  // again, once their snapshot is let go (Windows removes no mapped file)
        std::error_code ec;
        fs::remove_all(dir_ / name, ec);
    }
    keys_indexed_  = false;
    names_indexed_ = false;
    name_ids_.clear();
    return true;
}

}  // namespace fcg::study
