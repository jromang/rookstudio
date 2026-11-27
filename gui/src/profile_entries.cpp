#include "profile_entries.hpp"

#include <fstream>
#include <optional>
#include <system_error>

#include <fmt/include/fmt/format.h>

#include "console.hpp"
#include "i18n.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

// A copy of a profile file that did not read whole, beside it (".unreadable", then ".unreadable2"…): what was set aside
// stays there when the file is written again; none when it could not be made.
std::optional<fs::path> keptAside(const fs::path& file) {
    std::error_code ec;
    for (int n = 1; n < 100; ++n) {
        const auto aside = fs::path(file).concat(n == 1 ? std::string(".unreadable") : fmt::format(".unreadable{}", n));
        if (fs::exists(aside, ec)) continue;
        if (fs::copy_file(file, aside, ec) && !ec) return aside;
        return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace

void ProfileEntries::sayUnread() const {
    if (unread_.empty()) return;
    std::string what;
    for (std::size_t i = 0; i < unread_.size() && i < 5; ++i) what += (i > 0 ? ", " : "") + unread_[i];
    if (unread_.size() > 5) what += ", …";
    const auto aside = keptAside(file_);
    Console::instance().add(
        trn("{}: {} entry could not be read and was set aside ({}); the file as it was is kept as {}",
            "{}: {} entries could not be read and were set aside ({}); the file as it was is kept as {}",
            unread_.size(), file_.string(), unread_.size(), what, aside ? aside->string() : std::string("-")),
        true);
}

void sayUnreadableFile(const fs::path& file) {
    std::error_code ec;
    if (!fs::exists(file, ec)) return;
    const auto aside = keptAside(file);
    Console::instance().add(aside ? trf("{} could not be read: kept as {}", file.string(), aside->string())
                                  : trf("{} could not be read", file.string()),
                            true);
}

std::optional<nlohmann::json> readProfileJson(const fs::path& file, nlohmann::json::value_t type) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    auto j = nlohmann::json::parse(in, nullptr, false);
    in.close();  // let go before it is copied aside
    if (j.is_discarded() || j.type() != type) {
        sayUnreadableFile(file);
        return std::nullopt;
    }
    return j;
}

}  // namespace fcg
