#include "players.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

#include "query.hpp"

namespace fcg::study {

PlayerTable::PlayerTable(std::vector<Player> players, std::vector<std::pair<GameId, GameId>> apart)
    : players_(std::move(players)), apart_(std::move(apart)) {
    for (auto& p : players_) {
        std::sort(p.spellings.begin(), p.spellings.end());
        p.spellings.erase(std::unique(p.spellings.begin(), p.spellings.end()), p.spellings.end());
    }
    for (auto& [a, b] : apart_)
        if (b < a) std::swap(a, b);
    std::sort(apart_.begin(), apart_.end());
    apart_.erase(std::unique(apart_.begin(), apart_.end()), apart_.end());
    index();
}

void PlayerTable::index() {
    by_name_.clear();
    for (std::size_t p = 0; p < players_.size(); ++p)
        for (const auto id : players_[p].spellings) by_name_.emplace(id, static_cast<uint32_t>(p));  // the first holds
}

bool PlayerTable::keptApart(GameId a, GameId b) const {
    if (b < a) std::swap(a, b);
    return std::binary_search(apart_.begin(), apart_.end(), std::pair{a, b});
}

std::optional<std::size_t> PlayerTable::playerOf(uint32_t name_id) const {
    const auto it = by_name_.find(name_id);
    if (it == by_name_.end()) return std::nullopt;
    return it->second;
}

const Player* PlayerTable::of(uint32_t name_id) const {
    const auto p = playerOf(name_id);
    return p ? &players_[*p] : nullptr;
}

namespace {

constexpr std::string_view TABLE_MAGIC = "RSPLAYR1";
constexpr std::array<std::string_view, 4> HOW{"", "fide-id", "list", "hand"};

template <typename T>
void put(std::string& out, T v) {
    out.append(reinterpret_cast<const char*>(&v), sizeof v);
}

void putText(std::string& out, std::string_view s) {
    const auto n = static_cast<uint16_t>(std::min<std::size_t>(s.size(), 65535));
    put(out, n);
    out.append(s.substr(0, n));
}

struct Reader {
    std::string_view bytes;
    std::size_t at = 0;
    template <typename T>
    T get() {
        if (at + sizeof(T) > bytes.size()) throw std::runtime_error("damaged table of names");
        T v;
        std::memcpy(&v, bytes.data() + at, sizeof v);
        at += sizeof v;
        return v;
    }
    std::string text() {
        const auto n = get<uint16_t>();
        if (at + n > bytes.size()) throw std::runtime_error("damaged table of names");
        std::string s(bytes.substr(at, n));
        at += n;
        return s;
    }
};

}  // namespace

std::string PlayerTable::bytes() const {
    std::string out(TABLE_MAGIC);
    put(out, uint32_t{1});
    put(out, static_cast<uint32_t>(players_.size()));
    put(out, static_cast<uint64_t>(apart_.size()));
    for (const auto& [a, b] : apart_) {
        put(out, static_cast<uint64_t>(a));
        put(out, static_cast<uint64_t>(b));
    }
    for (const auto& p : players_) {
        put(out, static_cast<int64_t>(p.fide));
        for (const int v : {p.born, p.standard, p.rapid, p.blitz})
            put(out, static_cast<uint16_t>(std::clamp(v, 0, 65535)));
        const auto how = std::find(HOW.begin(), HOW.end(), p.how);
        put(out, static_cast<uint8_t>(how == HOW.end() ? 0 : how - HOW.begin()));
        putText(out, p.name);
        putText(out, p.federation);
        putText(out, p.title);
        putText(out, p.list);
        put(out, static_cast<uint32_t>(p.spellings.size()));
        for (const auto id : p.spellings) put(out, id);
    }
    return out;
}

PlayerTable PlayerTable::fromBytes(std::string_view bytes) {
    if (bytes.substr(0, TABLE_MAGIC.size()) != TABLE_MAGIC) throw std::runtime_error("not a table of names");
    Reader r{bytes, TABLE_MAGIC.size()};
    if (r.get<uint32_t>() > 1) throw std::runtime_error("a table of names of a newer program");
    const auto count = r.get<uint32_t>();
    const auto pairs = r.get<uint64_t>();
    if (pairs > bytes.size() / 16) throw std::runtime_error("damaged table of names");
    std::vector<std::pair<GameId, GameId>> apart;
    for (uint64_t i = 0; i < pairs; ++i) {
        const auto a = r.get<uint64_t>();
        apart.emplace_back(a, r.get<uint64_t>());
    }
    std::vector<Player> players;
    players.reserve(std::min<std::size_t>(count, bytes.size() / 20));
    for (uint32_t i = 0; i < count; ++i) {
        Player p;
        p.fide       = r.get<int64_t>();
        p.born       = r.get<uint16_t>();
        p.standard   = r.get<uint16_t>();
        p.rapid      = r.get<uint16_t>();
        p.blitz      = r.get<uint16_t>();
        const auto h = r.get<uint8_t>();
        p.how        = h < HOW.size() ? std::string(HOW[h]) : std::string();
        p.name       = r.text();
        p.federation = r.text();
        p.title      = r.text();
        p.list       = r.text();
        const auto n = r.get<uint32_t>();
        if (n > (bytes.size() - r.at) / 4) throw std::runtime_error("damaged table of names");
        p.spellings.reserve(n);
        for (uint32_t k = 0; k < n; ++k) p.spellings.push_back(r.get<uint32_t>());
        players.push_back(std::move(p));
    }
    if (r.at != bytes.size()) throw std::runtime_error("damaged table of names");
    return PlayerTable(std::move(players), std::move(apart));
}

// ---------------------------------------------------------------------------------------------------------------------

namespace {

std::vector<std::string> letterWords(std::string_view folded) {
    std::vector<std::string> out;
    std::string word;
    for (const char c : folded) {
        if (c >= 'a' && c <= 'z') {
            word += c;
        } else if (c == ' ' || c == '.' || c == '\t') {
            if (!word.empty()) out.push_back(std::move(word));
            word.clear();
        }
        // Hyphens, apostrophes, underscores and other signs join the letters around them: "Jean-Pierre", "O'Kelly",
        // an account's name "john_smith".
    }
    if (!word.empty()) out.push_back(std::move(word));
    return out;
}

std::string letters(std::string_view folded) {
    std::string out;
    for (const char c : folded)
        if (c >= 'a' && c <= 'z') out += c;
    return out;
}

std::string joined(const std::vector<std::string>& words, std::size_t from, std::size_t to) {
    std::string out;
    for (std::size_t i = from; i < to; ++i) out += (out.empty() ? "" : " ") + words[i];
    return out;
}

}  // namespace

NameParts nameParts(std::string_view name) {
    NameParts parts;
    const auto folded = foldText(name);
    const auto comma  = folded.find(',');
    if (comma != std::string::npos) {
        parts.surname    = letters(std::string_view(folded).substr(0, comma));
        const auto given = letterWords(std::string_view(folded).substr(comma + 1));
        parts.given      = joined(given, 0, given.size());
    } else {
        const auto words = letterWords(folded);
        if (words.size() == 1) {
            parts.surname = words[0];
        } else if (words.size() > 1) {
            // Initials after the surname ("Lindqvist E."), or before it ("E. Lindqvist"); else the given names first.
            std::size_t last = words.size();
            while (last > 1 && words[last - 1].size() == 1) --last;
            if (last < words.size()) {
                parts.surname = joined(words, 0, last);
                parts.given   = joined(words, last, words.size());
            } else {
                std::size_t first = 0;
                while (first + 1 < words.size() && words[first].size() == 1) ++first;
                parts.given   = joined(words, 0, first > 0 ? first : words.size() - 1);
                parts.surname = joined(words, first > 0 ? first : words.size() - 1, words.size());
            }
            std::erase(parts.surname, ' ');
        }
    }
    if (!parts.given.empty()) parts.initial = parts.given.front();
    return parts;
}

bool givenInitialOnly(std::string_view given) {
    if (given.empty()) return false;
    std::size_t at = 0;
    while (at < given.size()) {
        const auto end = std::min(given.find(' ', at), given.size());
        if (end - at > 1) return false;
        at = end + 1;
    }
    return true;
}

bool givenCompatible(std::string_view a, std::string_view b) {
    if (a.empty() || b.empty()) return true;
    const auto first = [](std::string_view s) { return s.substr(0, std::min(s.find(' '), s.size())); };
    const auto fa = first(a), fb = first(b);
    if (fa.size() == 1 || fb.size() == 1) return fa.front() == fb.front();
    return fa == fb || nameSkeleton(fa) == nameSkeleton(fb);
}

std::string nameSkeleton(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == 'w') {
            c = 'v';
        } else if (c == 'y' || c == 'j') {
            c = 'i';
        } else if (c == 'p' && i + 1 < s.size() && s[i + 1] == 'h') {
            c = 'f';
            ++i;
        } else if (c == 'c' && i + 1 < s.size() && s[i + 1] == 'k') {
            c = 'k';
            ++i;
        } else if (c == 'x') {
            out += 'k';
            c = 's';
        }
        if (out.empty() || out.back() != c) out += c;
    }
    return out;
}

bool anonymousName(std::string_view name) {
    const auto l = letters(foldText(name));
    return l.empty() || l == "nn" || l == "white" || l == "black" || l == "unknown";
}

}  // namespace fcg::study
