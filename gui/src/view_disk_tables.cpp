// The Syzygy tables of the disk (#82), App's: the setup's folders (Settings, the tournaments') with the profile's own
// once it holds tables, which the Board's Endgames tab (#42), its questions, a tournament's endgame check (#94) and the
// endgames against the tables (endgames_page.hpp, through context::DiskTables) probe; the tables of 3 to 5 pieces
// downloaded into the profile on the user's click (syzygy_download.hpp), offered to the tournaments once the set is
// whole, and loaded again as each piece count's files are all there.

#include <algorithm>
#include <chrono>
#include <string>

#include "app.hpp"
#include "console.hpp"
#include "i18n.hpp"

namespace fcg {

namespace {

using Clock = std::chrono::steady_clock;

double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

const char* separator() {
#ifdef _WIN32
    return ";";
#else
    return ":";
#endif
}

}  // namespace

const SyzygyDownload::State& App::syzygyState() {
    // Read again now and then (a folder's files, their marks), not while the download writes them: its progress says.
    const bool busy = syzygy_download_ && syzygy_download_->busy();
    if (!syzygy_state_ || (!busy && since(syzygy_state_at_) > 10.0)) {
        syzygy_state_    = syzygy_download_ ? syzygy_download_->state() : SyzygyDownload::State{};
        syzygy_state_at_ = Clock::now();
    }
    return *syzygy_state_;
}

std::string App::endgameTablesPaths() {
    std::string paths = setup_.tb_paths;
    if (syzygy_download_ && (syzygy_download_->busy() || syzygyState().files > 0)) {
        const auto own  = syzygy_download_->folder().string();
        const auto dirs = splitTablebasePath(paths);
        if (std::find(dirs.begin(), dirs.end(), own) == dirs.end()) paths += (paths.empty() ? "" : separator()) + own;
    }
    return paths;
}

int App::diskTablesPieces() {
    const auto paths = endgameTablesPaths();
    if (paths.empty()) return 0;
    // The most pieces whose WDL tables are all there, those of fewer pieces too (a capture leads to them): a set half
    // downloaded covers what it covers whole.
    const auto covered = [](const TablebaseCheck& c) {
        int n = 0;
        for (int k = 3; k <= c.largest; ++k) {
            if (const auto it = c.missing.find(k); it != c.missing.end() && !it->second.empty()) break;
            n = k;
        }
        return n;
    };
    // Checked again off the interface's thread every few seconds; the first time at once (names and sizes only), so
    // that an endgame chosen at once finds its tables.
    if (const auto* check = tb_checks_.get(paths)) return covered(*check);
    if (disk_tables_of_ != paths) {
        disk_tables_of_     = paths;
        disk_tables_pieces_ = covered(checkTablebases(paths));
    }
    return disk_tables_pieces_;
}

bool App::diskTablesLocked() const {
    if (run_->running() && !run_->remote()) return true;
    for (const auto& r : runs_)
        if (r.run->running() && !r.run->remote()) return true;
    return false;
}

void App::pollTablesDownload() {
    // The download: its end, and the tables loaded again as each piece count's files are all there.
    if (!syzygy_download_) return;
    if (const auto end = syzygy_download_->finished()) {
        syzygy_state_.reset();
        if (end->done()) {
            // The set is whole: offered to the tournaments, the setup's folders taking it in.
            const auto own  = syzygy_download_->folder().string();
            const auto dirs = splitTablebasePath(setup_.tb_paths);
            if (std::find(dirs.begin(), dirs.end(), own) == dirs.end()) {
                setup_.tb_paths += (setup_.tb_paths.empty() ? "" : separator()) + own;
                saveSetupSoon();
            }
            Console::instance().add(trf("The Syzygy tables of 3 to 5 pieces are on the disk: {}", own));
        } else if (end->failed()) {
            syzygy_error_ = end->error;
            Console::instance().add(trf("The Syzygy tables' download stopped: {}", end->error), true);
        }
    }
    if (syzygy_download_->generation() != syzygy_generation_ && !diskTablesLocked()) {
        syzygy_generation_ = syzygy_download_->generation();
        syzygy_state_.reset();
        const auto lock = lockSyzygy();
        forgetSyzygy();
    }
}

}  // namespace fcg
