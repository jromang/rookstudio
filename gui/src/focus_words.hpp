#pragma once

// A table of --focus words (app_chess.cpp): the word asked is tried against each entry's patterns in turn, and the
// first entry that matches is applied, the others passed over. A pattern is a word as it is, or with a '*' standing for
// any text there, none included: "board-tree=*" is board-tree=FILE, "export*=*" any word of export with a '='.

#include <initializer_list>
#include <string_view>

namespace fcg {

// Whether `word` is `pattern`, each '*' of the pattern any text (none included).
[[nodiscard]] inline bool focusWordMatches(std::string_view pattern, std::string_view word) {
    const auto star = pattern.find('*');
    if (star == std::string_view::npos) return pattern == word;
    if (word.substr(0, star) != pattern.substr(0, star)) return false;
    const auto rest = pattern.substr(star + 1);
    for (auto tail = word.substr(star);; tail.remove_prefix(1)) {
        if (focusWordMatches(rest, tail)) return true;
        if (tail.empty()) return false;
    }
}

class FocusWords {
   public:
    explicit FocusWords(std::string_view word) : word_(word) {}

    // apply() if no entry before took the word and one of `patterns` matches it.
    template <typename Apply>
    void on(std::initializer_list<std::string_view> patterns, Apply&& apply) {
        if (applied_) return;
        for (const auto pattern : patterns) {
            if (focusWordMatches(pattern, word_)) {
                applied_ = true;
                apply();
                return;
            }
        }
    }

    // Whether an entry took the word.
    [[nodiscard]] bool applied() const { return applied_; }

   private:
    std::string_view word_;
    bool applied_ = false;
};

}  // namespace fcg
