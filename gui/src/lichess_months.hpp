#pragma once

// Lichess's monthly files (database.lichess.org): every month's rated games (CC0, about 30 GB compressed), and every
// month's official broadcasts (CC BY-SA 4.0, a few tens of MB), each a .pgn.zst listed on its page with its size and
// its number of games (#72, #73, mockup M10).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fcg::lichess {

enum class MonthKind { Standard, Broadcast };

struct Month {
    std::string month;  // YYYY-MM
    std::string size;   // as the page writes it: "22.6 MB", "31 GB"
    uint64_t games = 0;
    std::string path;  // on the site: "broadcast/lichess_db_broadcast_2026-10.pgn.zst"
    bool operator==(const Month&) const = default;
};

// The site's address: database.lichess.org's, or a test's server (http://127.0.0.1:PORT).
void setDatabaseServer(std::string base);
[[nodiscard]] std::string databaseUrl();  // its page
[[nodiscard]] std::string monthUrl(const Month& month);
[[nodiscard]] std::string monthFile(const Month& month);  // the file's name
// Lichess's evaluations database (lichess_db_eval.jsonl.zst, CC0), on the same site (#53, #58).
[[nodiscard]] std::string evalsUrl();
// Lichess's puzzle database (lichess_db_puzzle.csv.zst, CC0), on the same site (#80).
[[nodiscard]] std::string puzzlesUrl();

// The months of one kind its page lists, the newest first; empty when it lists none (the page changed).
[[nodiscard]] std::vector<Month> parseMonths(std::string_view html, MonthKind kind);

}  // namespace fcg::lichess
