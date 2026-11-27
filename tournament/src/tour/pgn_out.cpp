#include "pgn_out.hpp"

#include <algorithm>
#include <stdexcept>

#include "journal_events.hpp"

namespace fcg::tour {

void PgnOutWriter::opened(const std::set<uint64_t>& written) {
    for (std::size_t i = 0; i < source_.results.size(); ++i) {
        if (written.count(source_.results[i].pgn.offset)) {
            written_.insert(source_.results[i].number());
        } else {
            held_.push_back(i);
        }
    }
}

void PgnOutWriter::write(std::unique_lock<std::mutex>& lock, Journal* journal) {
    if (source_.spec.pgn_out.empty() || !journal) return;

    // One thread writes at a time, without the lock (syncs to the disk take long); the games that become ready
    // meanwhile are written by it right after.
    if (writing_) {
        again_ = true;
        return;
    }
    writing_ = true;
    // However the write ends, an exception of any kind from anywhere included, the writes after it are not held back
    // for good: writing_ is reset under the lock, held again as when it returns.
    const struct Reset {
        std::unique_lock<std::mutex>& lock;
        bool& writing;
        ~Reset() {
            if (!lock.owns_lock()) lock.lock();
            writing = false;
        }
    } reset{lock, writing_};
    const auto written = [&](const std::vector<std::size_t>& games) {
        for (const auto i : games) written_.insert(source_.results[i].number());
        const std::set<std::size_t> gone(games.begin(), games.end());
        held_.erase(std::remove_if(held_.begin(), held_.end(), [&](std::size_t i) { return gone.count(i); }),
                    held_.end());
    };
    do {
        again_ = false;

        // A write that failed partway first: what it left is completed (or is all there, only its sync failed).
        // When its text is not there (nothing was written, or another program wrote right after the part), it is
        // written again below.
        if (cut_) {
            const auto cut = *cut_;
            lock.unlock();
            bool settled = false;
            std::string error;
            try {
                settled = settleShared(cut.file, cut.at, cut.text);
            } catch (const std::exception& e) {
                error = e.what();
            }
            if (settled) journal->append(journalEvent(EventType::PgnOut, {{"pgn", cut.offsets}}));
            lock.lock();
            error_ = error;
            if (!error.empty()) break;
            cut_.reset();
            if (settled) written(cut.games);
        }

        // The first game that holds the others: neither done, nor dropped, nor written once already (its replay).
        uint64_t until = UINT64_MAX;
        if (!source_.finished) {
            while (source_.done.count(floor_) || source_.dropped.count(floor_) || written_.count(floor_)) {
                ++floor_;
            }
            until = floor_;
        }

        std::vector<std::size_t> ready;
        for (const auto i : held_) {
            if (!source_.results[i].voided && source_.results[i].number() < until) ready.push_back(i);
        }
        if (ready.empty()) break;
        std::stable_sort(ready.begin(), ready.end(), [&](std::size_t a, std::size_t b) {
            return source_.results[a].number() < source_.results[b].number();
        });

        const bool chessbase = source_.spec.pgn_out_chessbase;
        const auto from      = chessbase ? source_.chessbase : source_.pgn;
        const auto out       = source_.spec.pgn_out;
        std::vector<std::pair<uint64_t, PgnSpan>> games;  // (number, span)
        nlohmann::json offsets = nlohmann::json::array();
        // The games whose result was set by hand, as they are written: [offset, result, comment] (see settle()).
        nlohmann::json set = nlohmann::json::array();
        std::map<uint64_t, std::pair<std::string, std::string>> set_of;  // by number: (result, comment)
        for (const auto i : ready) {
            const auto& r = source_.results[i];
            games.emplace_back(r.number(), chessbase ? r.pgn_chessbase : r.pgn);
            offsets.push_back(r.pgn.offset);
            if (r.set) {
                set_of[r.number()] = {r.result, setResultComment(r)};
                set.push_back({r.pgn.offset, r.result, set_of[r.number()].second});
            }
        }

        lock.unlock();
        std::string text, error;
        for (const auto& [number, span] : games) {
            const auto game = readSpan(from, span);
            if (!game) {
                error = "cannot read game " + std::to_string(number + 1) + " back from the tournament folder";
                break;
            }
            const auto it = set_of.find(number);
            text += it == set_of.end() ? *game
                                       : withResult(*game, it->second.first, OPERATOR_TERMINATION, it->second.second);
        }
        // Journaled before, with the place it goes to, and after: a crash in between is settled by settle() when
        // the tournament is opened again, which looks for the text there. No game is written twice, nor lost.
        bool journal_failed = false;
        std::optional<uint64_t> begun_at;  // the write was journaled as begun there
        const auto append = [&](const nlohmann::json& event) {
            try {
                journal->append(event);
            } catch (...) {
                journal_failed = true;
                throw;
            }
        };
        try {
            if (error.empty()) {
                appendShared(out, text, [&](uint64_t at) {
                    auto begun = journalEvent(EventType::PgnOut, {{"pgn", nlohmann::json::array()},
                                                                  {"begun", offsets},
                                                                  {"at", at},
                                                                  {"len", text.size()},
                                                                  {"cb", chessbase}});
                    if (!set.empty()) begun["set"] = set;
                    append(begun);
                    begun_at = at;
                });
                append(journalEvent(EventType::PgnOut, {{"pgn", offsets}}));
            }
        } catch (const std::exception& e) {
            if (journal_failed) throw;
            error = e.what();
        }
        lock.lock();

        error_ = error;
        if (!error.empty()) {
            if (begun_at) cut_ = Cut{ready, offsets, out, *begun_at, std::move(text)};
            break;
        }
        written(ready);
    } while (again_);
}

bool PgnOutWriter::settle(const nlohmann::json& begun, const std::map<uint64_t, std::pair<PgnSpan, PgnSpan>>& journaled,
                          Journal* journal) {
    if (source_.spec.pgn_out.empty() || !journal) return false;

    // The text of the write, read back as write() did, with the results set by hand then. Games it cannot read
    // back are written again, or fail to.
    const bool chessbase = begun.value("cb", source_.spec.pgn_out_chessbase);
    const auto from      = chessbase ? source_.chessbase : source_.pgn;
    std::map<uint64_t, std::pair<std::string, std::string>> set;  // by offset: (result, comment)
    if (const auto it = begun.find("set"); it != begun.end() && it->is_array()) {
        for (const auto& s : *it)
            set[s.at(0).get<uint64_t>()] = {s.at(1).get<std::string>(), s.at(2).get<std::string>()};
    }
    std::string text;
    for (const auto& offset : begun.at("begun")) {
        const auto it = journaled.find(offset.get<uint64_t>());
        if (it == journaled.end()) return false;
        const auto game = readSpan(from, chessbase ? it->second.second : it->second.first);
        if (!game) return false;
        const auto s = set.find(offset.get<uint64_t>());
        text += s == set.end() ? *game : withResult(*game, s->second.first, OPERATOR_TERMINATION, s->second.second);
    }
    if (text.size() != begun.value("len", uint64_t{0})) return false;

    try {
        if (!settleShared(source_.spec.pgn_out, begun.at("at").get<uint64_t>(), text)) return false;
    } catch (const std::exception& e) {
        error_ = e.what();
        return false;
    }
    journal->append(journalEvent(EventType::PgnOut, {{"pgn", begun.at("begun")}}));
    return true;
}

}  // namespace fcg::tour
