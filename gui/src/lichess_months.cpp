#include "lichess_months.hpp"

#include <algorithm>
#include <cctype>

namespace fcg::lichess {

namespace {

std::string& databaseServer() {
    static std::string base = "https://database.lichess.org";
    return base;
}

std::string cellText(std::string_view cell) {
    std::string out;
    bool in_tag = false;
    for (const char c : cell) {
        if (c == '<')
            in_tag = true;
        else if (c == '>')
            in_tag = false;
        else if (!in_tag)
            out += c;
    }
    const auto first = out.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return out.substr(first, out.find_last_not_of(" \t\r\n") - first + 1);
}

}  // namespace

void setDatabaseServer(std::string base) { databaseServer() = std::move(base); }

std::string databaseUrl() { return databaseServer() + "/"; }

std::string monthUrl(const Month& month) { return databaseServer() + "/" + month.path; }

std::string evalsUrl() { return databaseServer() + "/lichess_db_eval.jsonl.zst"; }

std::string puzzlesUrl() { return databaseServer() + "/lichess_db_puzzle.csv.zst"; }

std::string monthFile(const Month& month) {
    const auto slash = month.path.rfind('/');
    return slash == std::string::npos ? month.path : month.path.substr(slash + 1);
}

std::vector<Month> parseMonths(std::string_view html, MonthKind kind) {
    const std::string prefix =
        kind == MonthKind::Broadcast ? "broadcast/lichess_db_broadcast_" : "standard/lichess_db_standard_rated_";
    std::vector<Month> out;
    // Each row: its month in words, its size, its games, its links; the file's name holds the month.
    std::size_t at = 0;
    while ((at = html.find("<tr", at)) != std::string_view::npos) {
        const auto end  = html.find("</tr>", at);
        const auto row  = html.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
        at              = end == std::string_view::npos ? html.size() : end;
        const auto link = row.find("href=\"" + prefix);
        if (link == std::string_view::npos) continue;
        const auto path_end = row.find('"', link + 6);
        if (path_end == std::string_view::npos) continue;
        Month m;
        m.path = std::string(row.substr(link + 6, path_end - link - 6));
        if (m.path.size() < prefix.size() + 7 + 8 || m.path.substr(m.path.size() - 8) != ".pgn.zst") continue;
        m.month = m.path.substr(prefix.size(), 7);
        std::vector<std::string> cells;
        std::size_t c = 0;
        while ((c = row.find("<td", c)) != std::string_view::npos) {
            const auto open  = row.find('>', c);
            const auto close = row.find("</td>", c);
            if (open == std::string_view::npos || close == std::string_view::npos || close < open) break;
            cells.push_back(cellText(row.substr(open + 1, close - open - 1)));
            c = close + 5;
        }
        if (cells.size() >= 3) {
            m.size = cells[1];
            for (const char ch : cells[2])
                if (std::isdigit(static_cast<unsigned char>(ch)))
                    m.games = m.games * 10 + static_cast<uint64_t>(ch - '0');
        }
        out.push_back(std::move(m));
    }
    std::sort(out.begin(), out.end(), [](const Month& a, const Month& b) { return a.month > b.month; });
    out.erase(std::unique(out.begin(), out.end(), [](const Month& a, const Month& b) { return a.month == b.month; }),
              out.end());
    return out;
}

}  // namespace fcg::lichess
