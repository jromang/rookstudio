// A database's table of names (players-NNNNNN.bin: the pairs of games told apart, then each player, its numbers, its
// names and the name ids it gathers) changed after it was written (#102): the input's changes applied to a sound one,
// made once for games of our own. Read alone, a table that reads is written and read back the same; in its database,
// a table that does not read is refused when it opens, one that does is read with every game, a player's dossier and
// the list's names folded with their players'.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <json.hpp>
#include <study/players.hpp>
#include <study/store.hpp>

#include "fuzz_database.hpp"
#include "fuzz_file.hpp"

namespace {

struct Template {
    std::filesystem::path dir;  // the database, its table written again for each input
    std::string players_name, players;
};

const Template& made() {
    static const Template t = [] {
        using namespace fcg::study;
        Template t;
        t.dir = fcg::fuzz::ownFolder("players");
        std::filesystem::remove_all(t.dir);
        {
            auto db = Database::create(t.dir);
            db->append(fcg::fuzz::studyGames(), true);
            const auto snap = db->snapshot();
            Player moreau;
            moreau.name       = "Moreau, Claire";
            moreau.fide       = 90000001;
            moreau.how        = "list";
            moreau.federation = "FRA";
            moreau.title      = "WFM";
            moreau.born       = 1990;
            moreau.standard   = 2010;
            moreau.list       = "2026-11";
            moreau.spellings  = {snap->rowIds(0).white, snap->rowIds(1).black};
            Player duval;
            duval.name      = "Duval, Marc";
            duval.how       = "hand";
            duval.lichess   = "marc_duval";
            duval.spellings = {snap->rowIds(0).black};
            db->setPlayers(PlayerTable({moreau, duval}, {{0, 5}, {3, 2}}));
        }
        t.players_name = nlohmann::json::parse(fcg::fuzz::readFile(t.dir / "manifest.json")).value("players", "");
        t.players      = fcg::fuzz::readFile(t.dir / t.players_name);
        return t;
    }();
    return t;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    using namespace fcg::study;
    const auto& t = made();
    auto players  = t.players;
    fcg::fuzz::applyChanges(data, size, {&players});
    try {
        const auto table = PlayerTable::fromBytes(players);
        const auto bytes = table.bytes();
        if (PlayerTable::fromBytes(bytes).bytes() != bytes) std::abort();  // written and read back the same
        for (uint32_t id = 0; id < 16; ++id) (void)table.of(id);
        (void)table.keptApart(0, 5);
    } catch (const std::runtime_error&) {
    }
    fcg::fuzz::writeFile(t.dir / t.players_name, players);
    std::shared_ptr<Database> db;
    try {
        db = Database::openReadOnly(t.dir);
    } catch (const std::runtime_error&) {
        return 0;  // a table that does not read: refused
    }
    fcg::fuzz::readEverything(*db);
    return 0;
}
