#pragma once

// FIDE cards for the players of the databases (#57, mockup M8, board "Prep"): title, federation, birth year and the
// three ratings, asked of Lichess, which keeps FIDE's list (no account), on demand only, since a private database may
// name private persons: by the FIDE id a game gives (WhiteFideId), else by the name, the user confirming the match when
// several players have it. The cards are kept in the profile.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lichess_api.hpp"

namespace fcg::lichess {

struct FideCard {
    int64_t id = 0;
    std::string name, title, federation;
    int year     = 0;  // of birth, 0 when not told
    int standard = 0, rapid = 0, blitz = 0;
    bool inactive = false;
    int64_t asked = 0;  // when Lichess gave it, seconds since the epoch
};
[[nodiscard]] Answer<FideCard> fidePlayer(int64_t id, const std::atomic<bool>* cancel = nullptr);
[[nodiscard]] Answer<std::vector<FideCard>> fideSearch(const std::string& name,
                                                       const std::atomic<bool>* cancel = nullptr);

// A name as a key: its letters and digits, lower case, in order ("Carlsen, Magnus" and "carlsen magnus" match).
[[nodiscard]] std::string fideNameKey(std::string_view name);
// Of a search's players, the one whose name is `name`'s; none when none or several are (the user chooses then).
[[nodiscard]] std::optional<FideCard> fideMatch(const std::vector<FideCard>& found, std::string_view name);
// Its page on lichess.org.
[[nodiscard]] std::string fideUrl(const FideCard& card);

// The profile's cards, by id and by the names confirmed for them, kept in a file.
class FideCards {
   public:
    explicit FideCards(std::filesystem::path file);
    [[nodiscard]] const FideCard* byName(std::string_view name) const;
    [[nodiscard]] const FideCard* byId(int64_t id) const;
    // A card Lichess gave, and the database's name it is the card of (none: by its id only).
    void keep(const FideCard& card, std::string_view name = {});
    void save() const;

   private:
    std::filesystem::path file_;
    std::map<int64_t, FideCard> cards_;
    std::map<std::string, int64_t> names_;  // fideNameKey → id
};

}  // namespace fcg::lichess
