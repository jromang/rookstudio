#pragma once

// A zstd file read while it downloads, or from the disk, and resumable: its text is given to a consumer as it comes,
// and the reading's place is saved at a frame's end now and then (the compressed offset, and what the consumer keeps
// of its own), so that a reading cut short (Cancel, a network cut, the program closed) goes on from there. A file that
// changed on the server since is read again from its start. Lichess's big files are made of frames of a few megabytes,
// each decoded without the ones before it. Used for Lichess's evaluations (#53) and its months of games (#72).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <json.hpp>

namespace fcg {

class ZstdConsumer {
   public:
    virtual ~ZstdConsumer() = default;
    // A piece of the text, lines cut anywhere; false to stop (error() says why, unless it was a cancel).
    virtual bool text(std::string_view piece) = 0;
    // At each frame's end: its own state there, light (the text begun, its counts), kept with the place.
    [[nodiscard]] virtual nlohmann::json mark() = 0;
    // What it found so far made safe (written, committed): before a place is saved, now and then and at a stop. It may
    // hold what came after the last frame's end: read again from there, it must not count twice.
    virtual void commit() = 0;
    // A reading cut short goes on from its place: the state its mark() gave there.
    virtual void restore(const nlohmann::json& state) = 0;
    // From the file's start: what it found forgotten (the file changed, or there was nothing to go on from).
    virtual void startAgain() = 0;
    // The file's end: what it found made final; empty, or why it could not be. `date`: the file's, from the server.
    [[nodiscard]] virtual std::string finish(const std::string& date) = 0;
    [[nodiscard]] virtual std::string error() const                   = 0;
};

struct ZstdProgress {
    std::atomic<uint64_t> bytes{0};  // of the compressed file, read so far
    std::atomic<uint64_t> size{0};   // of the whole file, 0 while not known
    std::atomic<bool> resumed{false};
};

struct ZstdReadingOptions {
    double checkpoint_every = 5;                 // seconds between saved places; the tests make it 0
    std::chrono::milliseconds retry_wait{2000};  // before going on after a cut, doubled each time
    int retries = 5;
};

// Reads `source` (an http:// or https:// URL, else a file) through `consumer`, its place kept in `state_file`, on the
// calling thread. Returns "" at the end, "cancelled" for a cancel, else why it stopped.
[[nodiscard]] std::string readZstd(const std::string& source, const std::filesystem::path& state_file,
                                   ZstdConsumer& consumer, ZstdProgress& progress, const std::atomic<bool>& cancel,
                                   const ZstdReadingOptions& options = {});

}  // namespace fcg
