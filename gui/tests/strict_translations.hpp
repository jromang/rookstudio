#pragma once

#include <cstdlib>
#include <string_view>

namespace fcg::test {

// RookStudio translates in a batch before each preview (PLAN.md): while a feature is being written, a message or a
// guide section without its translation is reported, not a failure. FCG_STRICT_TRANSLATIONS=1 makes it one: the CI
// sets it for a tag, and it is set by hand before tagging a preview.
inline bool strictTranslations() {
    const char* value = std::getenv("FCG_STRICT_TRANSLATIONS");
    return value && std::string_view(value) == "1";
}

}  // namespace fcg::test
