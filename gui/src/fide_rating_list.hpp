#pragma once

// FIDE's rating list in the profile (mockup M13, board 1): downloaded from ratings.fide.com when the user asks (its
// combined list, about 43 MB zipped), offered again each month, read into RookStudio's file (study/fide_list.hpp) to
// match the players of the databases. FIDE gives it no licence ("all rights reserved"): it never ships with
// RookStudio, is never sent anywhere, and stays on the machine; names, birth years and sexes are personal data.
//
//   <profile>/fide/list-2026-11.bin  the players, as study::FideList reads them (a file of its own for each list: the
//                                    one before, maybe still mapped, removed once the new one is kept)
//   <profile>/fide/list.json         its file, its month, the day FIDE published it, its counts, the last month the
//                                    Home page offered

#include <atomic>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <study/fide_list.hpp>

#include "https.hpp"

namespace fcg::fide {

// The list's address: FIDE's, or a test's server in its place (http://127.0.0.1:PORT/...).
void setServer(std::string url);
[[nodiscard]] std::string listUrl();

// A day as HTTP's Last-Modified writes it ("Sun, 22 Nov 2026 22:03:51 GMT"); none when it is not one.
struct Day {
    int year = 0, month = 0, day = 0;
    [[nodiscard]] std::string monthKey() const;  // "2026-11"
    [[nodiscard]] std::string text() const;      // "2026-11-22"
};
[[nodiscard]] std::optional<Day> httpDay(std::string_view text);
// The day a zip's first text file was written, as the archive says (FIDE's list: the day it was made); none when the
// file is no zip or says none.
[[nodiscard]] std::optional<Day> zipDay(const std::filesystem::path& file);

// "November 2026", the month's name in the interface's language.
[[nodiscard]] std::string monthText(std::string_view month_key);

struct Kept {
    std::string month;      // "2026-11"
    std::string published;  // "2026-11-22"
    study::FideListInfo info;
    std::string offered;  // the month the Home page offered last, so that "Not now" waits for the next one
    std::string file;     // its file in the profile's fide folder
    uint64_t bytes = 0;   // that file's size
};
[[nodiscard]] std::optional<Kept> readKept(const std::filesystem::path& profile);
void writeKept(const std::filesystem::path& profile, const Kept& kept);
[[nodiscard]] std::filesystem::path listFile(const std::filesystem::path& profile, const Kept& kept);
// Its list removed from the profile ("Remove it"): the database's players brought together stay as they are.
void removeKept(const std::filesystem::path& profile);

// The month whose list is due: the one after the kept list's, once it has begun (FIDE publishes one a month), and not
// offered and set aside yet; empty when none.
[[nodiscard]] std::string monthDue(const Kept& kept, std::time_t now);

// A download and its reading, in the background.
struct Job {
    enum class Phase { Asking, Downloading, Reading };
    std::atomic<Phase> phase{Phase::Asking};
    std::atomic<bool> cancel{false};
    DownloadProgress download;
    study::FidePrepareProgress reading;
    struct Outcome {
        bool ok        = false;
        bool cancelled = false;
        bool format    = false;  // its columns changed: the list kept before stays
        bool same      = false;  // FIDE has not published a newer list than the one kept
        uint64_t cut   = 0;      // the bytes kept of a download cut short, which "Resume" goes on from
        std::string error;
        std::string month;  // of the list tried, "2026-12"
        Kept kept;          // the list now in the profile, when ok
    };
    std::future<Outcome> done;
};
// FIDE's list downloaded (unless it is the one kept already, then `same`) and read into the profile; or `file`, one
// downloaded already (a zip or its text file), read.
[[nodiscard]] std::unique_ptr<Job> start(const std::filesystem::path& profile, const std::filesystem::path& file = {});

}  // namespace fcg::fide
