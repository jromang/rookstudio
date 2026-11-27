#pragma once

// A database's error as a page keeps it to say it (#141): the page's sentence, in the user's language, and the store's
// code (study/store_error.hpp), which tells what can repair it (the database's check for a damaged file, room made on
// the disk), never read from the sentence. Other for a message that names no code.

#include <string>

#include <study/store_error.hpp>

namespace fcg {

struct StoreFailure {
    std::string said;  // the sentence shown; empty when nothing is to be said
    study::StoreErrc code = study::StoreErrc::Other;

    [[nodiscard]] bool empty() const noexcept { return said.empty(); }
    bool operator==(const StoreFailure&) const = default;
};

}  // namespace fcg
