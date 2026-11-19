#pragma once

// The first start (mockups M0, board 3, and M6, board 4): the language, the views to begin with, and a copy of
// fastchess-gui's profile when one is on the machine and the program's own has never started. fastchess-gui's profile
// is only read: both programs keep running side by side.

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace fcg::first {

// A profile that has never started: its settings.json has neither views nor a space yet.
[[nodiscard]] bool fresh(const std::filesystem::path& profile);

// fastchess-gui's profile in the system's place for it, beside the program's own `own` (".../rookstudio/" there,
// ".../fastchess/fastchess-gui/" for fastchess-gui), or FCG_LEGACY_PROFILE's; empty when there is none to copy (no
// engines.json, setup.json nor settings.json in it).
[[nodiscard]] std::filesystem::path legacyProfile(const std::filesystem::path& own);

// What a profile holds, as the first start tells it.
struct ProfileSummary {
    int engines     = 0;
    int books       = 0;
    int tournaments = 0;  // the folders of its tournaments folder
    bool settings   = false;
    uint64_t bytes  = 0;  // what a copy writes
};
[[nodiscard]] ProfileSummary summarize(const std::filesystem::path& profile);

// Whether a file or folder of a profile is copied: not what tells of tournaments running or waiting there
// (running.json, queue.json: fastchess-gui plays them), nor its logs, sockets and locks.
[[nodiscard]] bool copied(const std::filesystem::path& relative);

// Copies `from`'s files into `to`, those copied() leaves out aside, over what `to` has; `done` counts the bytes
// written. False and `error` on the first failure, what was copied kept.
bool copyProfile(const std::filesystem::path& from, const std::filesystem::path& to, std::string& error,
                 std::atomic<uint64_t>* done = nullptr);

// The page, drawn each frame by drawPage() until a choice is made.
struct Page {
    std::filesystem::path own, legacy;  // the program's profile; fastchess-gui's, empty for none
    ProfileSummary summary;             // of `legacy`
    std::array<bool, 3> views{true, true, false};  // the views offered (views::proposed()) that are chosen
    std::string language;               // the language chosen, as the setup keeps it ("fr")
    std::optional<bool> copy;           // set once chosen: copy fastchess-gui's profile or not
};
// Draws the page over the whole window; true once its choice is made (Continue, Start without, Enter).
bool drawPage(Page& page);

// Of the views chosen (views::proposed()'s indexes), the one shown first: Tests for fastchess-gui's users, Study for
// the others, when chosen; else the first chosen.
[[nodiscard]] int shownFirst(const Page& page);

}  // namespace fcg::first
