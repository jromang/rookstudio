#pragma once

// A file of the profile read entry by entry, its types checked (#136): an entry of another type than the one written
// (by an older or newer version, or by hand) is set aside and the readable ones kept, rather than json's type_error
// stopping the program at launch. What was set aside is said in the console with the entries' names, and the file as it
// was is copied beside it (<file>.unreadable, then .unreadable2…), since the next save writes it without them. A file
// there but unreadable as a whole (cut short) is copied aside and said the same way, rather than replaced in silence by
// the next save. The user's mistakes (mistakes.hpp), the endgames' record (endgames.hpp), board vision's (vision.hpp),
// the training's settings and each repertoire's training (training.hpp), the puzzles' record (puzzles.hpp), the kept
// searches (search_fields.hpp), the crosstables' settings (crosstables.hpp), the Lichess studies (lichess_studies.hpp),
// the FIDE cards' cache (fide_cards.hpp) and the Suites page's setup are read so; and (#143) play.json and the games
// played (engine_play.hpp), assistant.json, lichess.json, machines.json, the bot's settings (lichess_bot.hpp), the
// online game's (lichess_board.hpp), the engines installed and their releases kept (engine_downloads.hpp), the
// account's cache (lichess_profile.hpp) and the explorer's (opening_report.hpp). A file read again and again while it
// stays as it is is said and copied aside once.

#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <json.hpp>

namespace fcg {

// A file of the profile there but unreadable as a whole (cut short, not a JSON object): copied beside it and said in
// the console; nothing when there is no file.
void sayUnreadableFile(const std::filesystem::path& file);

// A file of the profile read whole as JSON of the `type` it is written as (an object, an array): none when it is not
// there, nor when it is there but unreadable as a whole (cut short, of another type), which sayUnreadableFile() then
// copies aside and says.
[[nodiscard]] std::optional<nlohmann::json> readProfileJson(
    const std::filesystem::path& file, nlohmann::json::value_t type = nlohmann::json::value_t::object);

class ProfileEntries {
   public:
    explicit ProfileEntries(std::filesystem::path file) : file_(std::move(file)) {}

    // `read` run; when it throws, the entry `what` is set aside. `read` keeps what it read only once it read it whole.
    template <typename Read>
    void entry(const std::string& what, Read&& read) {
        try {
            read();
        } catch (const std::exception&) {
            unread_.push_back(what);
        }
    }
    // The entries set aside said in the console, the file as it was copied beside it; nothing when all were read.
    void sayUnread() const;
    [[nodiscard]] const std::vector<std::string>& unread() const { return unread_; }

   private:
    std::filesystem::path file_;
    std::vector<std::string> unread_;
};

// An object of the profile written whole from a struct (its to_json and from_json, nlohmann's ..._WITH_DEFAULT) read
// key by key (#143): each key of `j` taken over what was read before it, set aside when it does not read (a value of
// another type), which keeps its default; a key the struct does not have is passed over, as from_json passes it over.
template <typename T>
[[nodiscard]] T readEntries(const nlohmann::json& j, ProfileEntries& entries) {
    T out{};
    for (const auto& [key, value] : j.items()) {
        entries.entry(key, [&] {
            auto tried = nlohmann::json(out);
            tried[key] = value;
            out        = tried.template get<T>();
        });
    }
    return out;
}

// A file of the profile holding such an object, read so: its default when it is not there; when it is there but
// unreadable as a whole (cut short), copied aside and said (readProfileJson()), its default kept; the keys set aside
// said, the file as it was copied beside it.
template <typename T>
[[nodiscard]] T readProfileEntries(const std::filesystem::path& file) {
    const auto j = readProfileJson(file);
    if (!j) return T{};
    ProfileEntries entries(file);
    auto out = readEntries<T>(*j, entries);
    entries.sayUnread();
    return out;
}

}  // namespace fcg
