#pragma once

// FIDE's rating list (mockup M13, board 1), to match the players of the databases with FIDE's: downloaded by the user
// from ratings.fide.com (its combined list, a zip of a text file of fixed columns, about two million players), never
// shipped with RookStudio nor sent anywhere. Read once into a file of RookStudio's in the profile, mapped when it is
// read: its players by FIDE id, and by surname (spelled as written, or as its transliterations alike: nameSkeleton()).
//
// The file read: a header line naming its columns ("ID Number", "Name", "Fed", "Sex", "Tit", "WTit", "OTit", "FOA",
// "SRtng", "SGm", "SK", "RRtng", "RGm", "Rk", "BRtng", "BGm", "BK", "B-day", "Flag"), then a line a player, each value
// under its column's name. A list whose columns differ is refused (FideFormatError), the list kept before staying.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "mapped_file.hpp"

namespace fcg::study {

struct FidePlayer {
    uint32_t id = 0;
    std::string_view name;                   // "Surname, Given", as FIDE writes it
    std::string federation;                  // "FRA"
    std::string title;                       // "GM", "IM", "WGM"…: the title, else the woman's title; empty when none
    int born     = 0;                        // year, 0 when not told
    int standard = 0, rapid = 0, blitz = 0;  // 0 when not rated
    // The development coefficients K of each rating (#92: a crosstable's rating change); 0 when the list does not
    // give them (a list read before they were kept).
    int k_standard = 0, k_rapid = 0, k_blitz = 0;
    bool woman    = false;
    bool inactive = false;
};

// The list's columns are not those of the lists known: its format changed.
class FideFormatError : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

struct FideListInfo {
    std::string month;  // "2026-11"
    uint64_t players = 0, rated = 0, standard = 0, rapid = 0, blitz = 0;
};

struct FidePrepareProgress {
    std::atomic<uint64_t> bytes{0};  // of the file read
    std::atomic<uint64_t> total{0};
};

// FIDE's file (the zip as downloaded, or the text file in it) read into `out`, a file of RookStudio's, the list's
// `month` kept in it. Written beside it first, then renamed: a list kept before stays until the new one is whole.
// Throws FideFormatError when the columns are not those known, std::runtime_error when the file cannot be read or
// written, or `cancel` stopped it.
FideListInfo prepareFideList(const std::filesystem::path& source, const std::filesystem::path& out,
                             const std::string& month, FidePrepareProgress& progress, const std::atomic<bool>& cancel);

class FideList {
   public:
    // Throws std::runtime_error when the file is not one prepareFideList() wrote.
    explicit FideList(const std::filesystem::path& file);
    [[nodiscard]] const FideListInfo& info() const noexcept { return info_; }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] FidePlayer player(std::size_t i) const;
    [[nodiscard]] std::optional<std::size_t> byId(uint32_t id) const;
    // The players whose surname (nameParts()) is `surname`; whose surname's skeleton (nameSkeleton()) is `skeleton`.
    [[nodiscard]] std::vector<std::size_t> bySurname(std::string_view surname) const;
    [[nodiscard]] std::vector<std::size_t> bySkeleton(std::string_view skeleton) const;

   private:
    [[nodiscard]] std::string_view nameOf(std::size_t i) const;
    MappedFile file_;
    FideListInfo info_;
    std::size_t count_          = 0;
    std::size_t record_         = 0;  // the bytes of a player: 20 in the first version, 24 with the K
    const uint8_t* records_     = nullptr;
    const char* names_          = nullptr;
    uint64_t names_end_         = 0;
    const uint8_t* by_surname_  = nullptr;
    const uint8_t* by_skeleton_ = nullptr;
    std::vector<std::string> federations_;
};

}  // namespace fcg::study
