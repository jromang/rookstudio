#pragma once

// What went wrong in a database, as a code the window says in the user's language, carried with the store's own text
// (in English, with its details: a folder, a file) for the log and for the errors no code names.

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>

namespace fcg::study {

enum class StoreErrc : uint8_t {
    Other,         // none of the others: the text alone says it
    ReadOnly,      // a change asked of a database another program writes to: nothing written here
    DiskFull,      // the disk has no room left for what is written
    Cancelled,     // stopped on request
    Damaged,       // a file of the database does not read as it was written
    NotADatabase,  // the folder holds no database
    NewerFormat,   // written by a newer program
    Busy,          // another program holds the folder (tour::FolderBusy): open it read-only
};

// An error of the store: its code and its text.
class StoreError : public std::runtime_error {
   public:
    StoreError(StoreErrc code, const std::string& what) : std::runtime_error(what), code_(code) {}
    [[nodiscard]] StoreErrc code() const noexcept { return code_; }

   private:
    StoreErrc code_;
};

// The code of an error: a StoreError's own; Busy for tour::FolderBusy; DiskFull for a system error saying so (a file
// written by std::filesystem); Other for anything else.
[[nodiscard]] StoreErrc errorCode(const std::exception& e) noexcept;
[[nodiscard]] StoreErrc errorCode(const std::exception_ptr& e) noexcept;

}  // namespace fcg::study
