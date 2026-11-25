// A database's edits file (the games replaced, with their versions' places, and the games deleted) changed after it
// was written (#102): the input's changes applied to a sound one, made once with games of our own, a game replaced and
// two deleted. A file that does not fit its counts is refused when the database opens; one that opens is read as the
// pages read every game, and checked.

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <json.hpp>
#include <study/store.hpp>

#include "fuzz_database.hpp"
#include "fuzz_file.hpp"

namespace {

struct Template {
    std::filesystem::path dir;  // the database, its edits written again for each input
    std::string edits_name, edits;
};

const Template& made() {
    static const Template t = [] {
        Template t;
        t.dir = fcg::fuzz::ownFolder("edits");
        std::filesystem::remove_all(t.dir);
        {
            auto db          = fcg::study::Database::create(t.dir);
            const auto games = fcg::fuzz::studyGames();
            db->append(games, true);
            db->replace(1, games.at(3));
            db->setDeleted(std::vector<fcg::study::GameId>{2, 4}, true);
        }
        t.edits_name = nlohmann::json::parse(fcg::fuzz::readFile(t.dir / "manifest.json")).value("edits", "");
        t.edits      = fcg::fuzz::readFile(t.dir / t.edits_name);
        return t;
    }();
    return t;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    const auto& t = made();
    auto edits    = t.edits;
    fcg::fuzz::applyChanges(data, size, {&edits});
    fcg::fuzz::writeFile(t.dir / t.edits_name, edits);
    std::shared_ptr<fcg::study::Database> db;
    try {
        db = fcg::study::Database::openReadOnly(t.dir);
    } catch (const std::runtime_error&) {
        return 0;  // damaged edits: refused
    }
    fcg::fuzz::readEverything(*db);
    return 0;
}
