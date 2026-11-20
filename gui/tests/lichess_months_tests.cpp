// Lichess's monthly files (#72, #73): the months its page lists, of each kind.
#include <doctest/doctest.hpp>

#include "lichess_months.hpp"

using namespace fcg::lichess;

namespace {

// A page as the site writes it: a table per kind, the newest month first, with its size, games and links.
const char* PAGE = R"(<h2 id="standard_games">Standard Chess</h2><table><tbody>
<tr>
    <td>2026 - October</td>
    <td class="right">31 GB</td>
    <td class="right">95,395,628</td>
    <td><a href="standard/lichess_db_standard_rated_2026-10.pgn.zst">.pgn.zst</a> <span class="sep">/</span> <a href="standard/lichess_db_standard_rated_2026-10.pgn.zst.torrent">.torrent</a></td>
</tr>
<tr><td>2026 - September</td><td class="right">30 GB</td><td class="right">93,001,002</td><td><a href="standard/lichess_db_standard_rated_2026-09.pgn.zst">.pgn.zst</a></td></tr>
</tbody></table>
<h2 id="broadcasts">Broadcasts</h2><table><tbody>
<tr>
    <td>2026 - September</td>
    <td class="right">18.4 MB</td>
    <td class="right">28,638</td>
    <td><a href="https://lichess.org/broadcast/calendar/2026/09">eye</a></td>
    <td><a href="broadcast/lichess_db_broadcast_2026-09.pgn.zst">.pgn.zst</a></td>
</tr>
<tr><td>2026 - October</td><td class="right">22.6 MB</td><td class="right">35,430</td><td></td><td><a href="broadcast/lichess_db_broadcast_2026-10.pgn.zst">.pgn.zst</a></td></tr>
</tbody></table>)";

}  // namespace

TEST_CASE("Lichess months: each kind's months, the newest first, with their size and games") {
    const auto broadcasts = parseMonths(PAGE, MonthKind::Broadcast);
    REQUIRE(broadcasts.size() == 2);
    CHECK(broadcasts[0] == Month{"2026-10", "22.6 MB", 35430, "broadcast/lichess_db_broadcast_2026-10.pgn.zst"});
    CHECK(broadcasts[1] == Month{"2026-09", "18.4 MB", 28638, "broadcast/lichess_db_broadcast_2026-09.pgn.zst"});
    const auto standard = parseMonths(PAGE, MonthKind::Standard);
    REQUIRE(standard.size() == 2);
    CHECK(standard[0].month == "2026-10");
    CHECK(standard[0].size == "31 GB");
    CHECK(standard[0].games == 95395628);
    CHECK(monthFile(standard[0]) == "lichess_db_standard_rated_2026-10.pgn.zst");
    CHECK(monthUrl(broadcasts[0]) == "https://database.lichess.org/broadcast/lichess_db_broadcast_2026-10.pgn.zst");
    CHECK(parseMonths("<html>a page that changed</html>", MonthKind::Broadcast).empty());
}
