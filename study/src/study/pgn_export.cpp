#include "pgn_export.hpp"

#include <utility>
#include <vector>

#include "import.hpp"
#include "players.hpp"

namespace fcg::study {

namespace {

std::string escaped(const std::string& value) {
    std::string out;
    for (const char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

}  // namespace

std::string pgnWithNames(std::string_view pgn, const std::optional<WrittenName>& white,
                         const std::optional<WrittenName>& black) {
    if (!white && !black) return std::string(pgn);
    // The tags: the lines of "[Name "Value"]" at the start, blank lines before them skipped.
    std::vector<std::pair<std::string, std::string>> tags;
    std::size_t at = 0;
    while (at < pgn.size()) {
        std::size_t line_end = pgn.find('\n', at);
        if (line_end == std::string_view::npos) line_end = pgn.size();
        std::string_view line = pgn.substr(at, line_end - at);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.remove_suffix(1);
        std::size_t lead = 0;
        while (lead < line.size() && (line[lead] == ' ' || line[lead] == '\t')) ++lead;
        line.remove_prefix(lead);
        if (line.empty() && tags.empty()) {
            at = line_end + 1;
            continue;
        }
        if (line.empty() || line.front() != '[') break;  // the tags end
        // A tag alone on its line, [Name "Value"]: a line of another shape there, the game stays as it was read.
        const auto space = line.find(' ');
        const auto quote = line.find('"');
        if (line.size() < 4 || line.back() != ']' || space == std::string_view::npos ||
            quote == std::string_view::npos || quote < space)
            return std::string(pgn);
        std::string name(line.substr(1, space - 1));
        std::string value;
        std::size_t i = quote + 1;
        for (; i < line.size(); ++i) {
            if (line[i] == '\\' && i + 1 < line.size()) {
                value += line[++i];
                continue;
            }
            if (line[i] == '"') break;
            value += line[i];
        }
        if (i + 2 != line.size()) return std::string(pgn);  // "]" right after the value's closing quote
        tags.emplace_back(std::move(name), std::move(value));
        at = line_end + 1;
    }
    if (tags.empty()) return std::string(pgn);
    const auto set = [&](const std::string& name, const std::string& value) {
        for (auto& [k, v] : tags)
            if (k == name) {
                v = value;
                return;
            }
        // A new tag after the Seven Tag Roster's last one there, else at the end.
        std::size_t after = tags.size();
        for (std::size_t i = 0; i < tags.size(); ++i)
            if (tags[i].first == "Black" || tags[i].first == "White" || tags[i].first == "Result") after = i + 1;
        tags.insert(tags.begin() + static_cast<std::ptrdiff_t>(after), {name, value});
    };
    for (const auto& [side, written] : {std::pair{"White", &white}, std::pair{"Black", &black}}) {
        if (!*written) continue;
        const auto& n = **written;
        if (!n.name.empty()) set(side, n.name);
        if (n.fide > 0) set(std::string(side) + "FideId", std::to_string(n.fide));
        if (!n.title.empty()) set(std::string(side) + "Title", n.title);
    }
    std::string out;
    for (const auto& [k, v] : tags) out += "[" + k + " \"" + escaped(v) + "\"]\n";
    if (at < pgn.size()) out += pgn.substr(at);
    return out;
}

std::optional<WrittenName> tableName(const Snapshot& snapshot, uint32_t name_id) {
    const auto* player = snapshot.players()->of(name_id);
    if (!player) return std::nullopt;
    return WrittenName{player->name, player->fide, player->title};
}

std::string exportedPgn(const Snapshot& snapshot, GameId id, bool write_names) {
    const auto pgn = importedPgn(snapshot.game(id));
    if (!write_names) return pgn;
    const auto ids = snapshot.rowIds(id);
    return pgnWithNames(pgn, tableName(snapshot, ids.white), tableName(snapshot, ids.black));
}

}  // namespace fcg::study
