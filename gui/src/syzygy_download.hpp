#pragma once

// The Syzygy tables of 3 to 5 pieces (#82, mockup M12, board 3): 290 files (WDL and DTZ, 939 MB), downloaded on the
// user's click from Lichess's mirror (tablebase.lichess.ovh/tables/standard/3-4-5/) into the profile's syzygy folder,
// for the endgames against the tables and offered to the tournaments (Settings' tablebases). One file at a time, after
// the host's pause (HostGate), taking no turn: a file may take minutes, which the online tables' questions do not wait
// for. The smallest piece counts first, so that the endgames of 3 and 4 pieces are ready within seconds. A file cut
// short goes on from where it stopped; each is checked: its size and Syzygy's mark (both variants of the 3-piece files
// in circulation are valid, so no single hash is). Nothing else leaves the machine.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "https.hpp"
#include "jobs.hpp"

namespace fcg {

struct SyzygyFile {
    const char* name;  // "KQvK.rtbw"
    uint64_t size;
};
// The files of 3, 4 and 5 pieces, by piece count, each table's WDL file before its DTZ file.
[[nodiscard]] const std::vector<SyzygyFile>& syzygy345Files();
[[nodiscard]] uint64_t syzygy345Bytes();
// The pieces a table's file is for: "KRPvKR.rtbw" 5.
[[nodiscard]] int syzygyFilePieces(const char* name);
// Whether a file there is a Syzygy table: its mark (WDL or DTZ, by its extension) and a size a table can have.
[[nodiscard]] bool syzygyFileValid(const std::filesystem::path& file);
[[nodiscard]] const char* syzygy345Url();  // Lichess's mirror, the folder's address ending with '/'

class SyzygyDownload {
   public:
    struct Progress {
        std::atomic<uint64_t> done{0};  // the bytes of the files there
        DownloadProgress current;       // the file under way
        std::atomic<std::size_t> files{0};
        std::atomic<int> ready{0};  // the most pieces all of whose files are there
        [[nodiscard]] uint64_t bytes() const noexcept { return done + current.bytes; }
    };
    // What the folder holds.
    struct State {
        std::size_t files = 0;  // valid files of the set
        uint64_t bytes    = 0;
        int ready         = 0;  // the most pieces all of whose files are there (0, 3, 4, 5)
        bool complete     = false;
    };

    // `files`: those of the set, the tests' few in place of the 290. A download under way when it goes is cancelled
    // and parked in `jobs` (#105).
    explicit SyzygyDownload(JobGroup& jobs, std::filesystem::path folder, std::string base_url = syzygy345Url(),
                            std::vector<SyzygyFile> files = syzygy345Files());
    SyzygyDownload(const SyzygyDownload&)            = delete;
    SyzygyDownload& operator=(const SyzygyDownload&) = delete;

    [[nodiscard]] const std::filesystem::path& folder() const noexcept { return folder_; }
    [[nodiscard]] State state() const;  // reads the folder: names and sizes, the marks of the files not checked yet
    void start();                       // the files missing, one after the other
    [[nodiscard]] bool busy() const noexcept { return job_.running(); }
    void cancel() noexcept { job_.cancel(); }
    [[nodiscard]] const Progress& progress() const noexcept { return job_.state(); }
    // Once stopped: empty when every file is there, else why ("cancelled" for Cancel).
    [[nodiscard]] std::optional<std::string> finished();
    // Changes when a piece count's files are all there: the tables to be loaded again.
    [[nodiscard]] uint64_t generation() const noexcept { return *generation_; }

   private:
    std::filesystem::path folder_;
    std::string base_url_;
    std::vector<SyzygyFile> files_;
    std::shared_ptr<std::atomic<uint64_t>> generation_ = std::make_shared<std::atomic<uint64_t>>(0);  // the task's too
    Job<std::string, Progress> job_;
};

}  // namespace fcg
