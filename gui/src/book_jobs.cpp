#include "book_jobs.hpp"

#include <system_error>

#include "i18n.hpp"

namespace fcg {

namespace fs = std::filesystem;

PgnBookJobs::Status PgnBookJobs::want(const std::string& pgn, int depth, bool chess960) {
    const Key key{pgn, tour::pgnBookPlies(depth), chess960};
    auto& book = books_[key];
    book.asked = frame_;

    std::error_code ec;
    const auto size = fs::file_size(pgn, ec);
    const auto time = ec ? fs::file_time_type{} : fs::last_write_time(pgn, ec);
    if (ec) {
        book.known = false;
        Status missing;
        missing.state = Status::State::Failed;
        missing.error = TR("the file is not found");
        return missing;
    }
    // Found for the file as it is: the cache is looked at again only when the file changes.
    if (!book.known || book.size != size || book.time != time) {
        book.known  = true;
        book.size   = size;
        book.time   = time;
        book.status = {};
        if (const auto cached = tour::cachedPgnBook(pgn, key.plies, chess960)) {
            book.status.state = Status::State::Ready;
            book.status.stats = cached->stats;
        }
    }
    if (!job_.idle() && job_.state().key == key && book.status.state == Status::State::Queued) {
        auto status     = book.status;
        status.state    = Status::State::Preparing;
        status.progress = job_.state().progress;
        return status;
    }
    return book.status;
}

void PgnBookJobs::retry(const std::string& pgn, int depth, bool chess960) {
    const auto it = books_.find(Key{pgn, tour::pgnBookPlies(depth), chess960});
    if (it != books_.end() && it->second.status.state == Status::State::Failed) it->second.known = false;
}

std::vector<PgnBookJobs::Ended> PgnBookJobs::poll() {
    std::vector<Ended> ended;
    if (job_.ready()) {
        const auto& job = job_.state();
        auto& book      = books_[job.key];
        // Its result stands for the file as it was read; the file changed since, the cache is looked at again.
        const bool same = book.known && book.size == job.size && book.time == job.time;
        try {
            const auto prepared = job_.take();
            if (same) {
                book.status       = {};
                book.status.state = Status::State::Ready;
                book.status.stats = prepared.stats;
                ended.push_back({job.key.pgn, job.key.plies, book.status});
            } else {
                book.known = false;
            }
        } catch (const tour::BookCancelled&) {
            book.known = false;
        } catch (const std::exception& e) {
            if (same) {
                book.status       = {};
                book.status.state = Status::State::Failed;
                book.status.error = e.what();
                ended.push_back({job.key.pgn, job.key.plies, book.status});
            } else {
                book.known = false;
            }
        }
    }

    // Nobody asked for it since the last poll: its depth changed, or it is no longer chosen.
    if (!job_.idle() && books_[job_.state().key].asked < frame_) job_.cancel();

    if (job_.idle()) {
        for (const auto& [key, book] : books_) {
            if (book.asked < frame_ || !book.known || book.status.state != Status::State::Queued) continue;
            auto& job = job_.start([key](TaskState<Converting>& converting) {
                return tour::preparePgnBook(key.pgn, key.plies, key.chess960, [&](double share) {
                    converting.progress = share;
                    return !converting.cancel;
                });
            });
            job.key   = key;
            job.size  = book.size;
            job.time  = book.time;
            break;
        }
    }

    // Those not asked for in a while are forgotten: a depth dragged through many values leaves many behind.
    for (auto it = books_.begin(); it != books_.end();) {
        const bool running = !job_.idle() && job_.state().key == it->first;
        it                 = !running && it->second.asked + 600 < frame_ ? books_.erase(it) : std::next(it);
    }
    ++frame_;
    return ended;
}

bool PolyglotBuild::start(const std::string& pgn, const std::string& bin, const tour::PgnBookOptions& options) {
    if (busy()) return false;
    auto& job   = job_.start([pgn, bin, options](TaskState<Building>& building) {
        return tour::pgnToPolyglot(pgn, bin, options, [&](double share) {
            building.progress = share;
            return !building.cancel;
        });
    });
    job.pgn     = pgn;
    job.bin     = bin;
    job.options = options;
    return true;
}

void PolyglotBuild::cancel() {
    if (busy()) job_.cancel();
}

std::optional<PolyglotBuild::Ended> PolyglotBuild::poll() {
    if (!job_.ready()) return std::nullopt;
    const auto& job = job_.state();
    Ended ended;
    ended.pgn     = job.pgn;
    ended.bin     = job.bin;
    ended.options = job.options;
    try {
        ended.stats = job_.take();
    } catch (const tour::BookCancelled&) {
        ended.cancelled = true;
    } catch (const tour::EmptyPgnBook& e) {
        ended.empty = true;
        ended.stats = e.stats;
        ended.error = e.what();
    } catch (const std::exception& e) {
        ended.error = e.what();
    }
    return ended;
}

}  // namespace fcg
