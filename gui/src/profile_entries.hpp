#pragma once

// A file of the profile read entry by entry, its types checked (#136): an entry of another type than the one written
// (by an older or newer version, or by hand) is set aside and the readable ones kept, rather than json's type_error
// stopping the program at launch. What was set aside is said in the console with the entries' names, and the file as it
// was is copied beside it (<file>.unreadable, then .unreadable2…), since the next save writes it without them. A file
// there but unreadable as a whole (cut short) is copied aside and said the same way, rather than replaced in silence by
// the next save. The user's mistakes (mistakes.hpp), the endgames' record (endgames.hpp) and board vision's
// (vision.hpp) are read so.

#include <exception>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace fcg {

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

// A file of the profile there but unreadable as a whole (cut short, not a JSON object): copied beside it and said in
// the console; nothing when there is no file.
void sayUnreadableFile(const std::filesystem::path& file);

}  // namespace fcg
