// The files of the profile that fell back to their defaults in silence on a value of another type, the next save then
// writing the defaults over what the user had (#143): each read entry by entry, the readable entries kept, those of
// another type set aside and said in the console, the file as it was copied beside it (.unreadable); a file cut short
// copied aside and said. A file read again as it is says nothing more.

#include <doctest/doctest.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

#include <json.hpp>

#include "assistant_service.hpp"
#include "console.hpp"
#include "engine_downloads.hpp"
#include "engine_play.hpp"
#include "lichess_board.hpp"
#include "lichess_bot.hpp"
#include "lichess_profile.hpp"
#include "lichess_settings.hpp"
#include "machines.hpp"
#include "opening_report.hpp"
#include "profile_entries.hpp"
#include "test_support.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

void spit(const fs::path& file, const std::string& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary | std::ios::trunc) << bytes;
}

// What the console said while `read` ran.
std::vector<std::string> saidWhile(const std::function<void()>& read) {
    const auto before = Console::instance().added();
    read();
    return Console::instance().since(before);
}

// One line said, holding each of `parts`.
void saidOnce(const std::vector<std::string>& said, std::initializer_list<std::string> parts) {
    REQUIRE(said.size() == 1);
    for (const auto& part : parts) {
        CAPTURE(part);
        CHECK(said[0].find(part) != std::string::npos);
    }
}

// A reader checked on its file: written with a value of another type (`mistyped`, the entries set aside said as
// `named`, what was read checked by `mistyped_read`), read again as it is (nothing more said), then cut short (what
// was read checked by `cut_read`). `read` reads the file as the program does.
void checkReader(const fs::path& file, const std::string& mistyped, const std::string& named,
                 const std::function<void()>& read, const std::function<void()>& mistyped_read,
                 const std::function<void()>& cut_read) {
    spit(file, mistyped);
    const auto said = saidWhile(read);
    mistyped_read();
    saidOnce(said, {"could not be read and", "set aside (" + named + ")", file.filename().string() + ".unreadable"});
    CHECK(test::readFile(fs::path(file).concat(".unreadable")) == mistyped);
    // Read again as it is (a page shows it again): said once.
    CHECK(saidWhile(read).empty());
    CHECK_FALSE(fs::exists(fs::path(file).concat(".unreadable2")));
    // Cut short (a power cut while it was written): kept aside and said, rather than replaced in silence.
    const auto cut = mistyped.substr(0, mistyped.size() / 2);
    spit(file, cut);
    const auto cut_said = saidWhile(read);
    cut_read();
    saidOnce(cut_said, {"could not be read: kept as", file.filename().string() + ".unreadable2"});
    CHECK(test::readFile(fs::path(file).concat(".unreadable2")) == cut);
}

}  // namespace

TEST_CASE("profile types: play.json, a setting of another type keeps its default, the others read (#143)") {
    const test::ScratchDir profile("play-types");
    PlaySettings s;
    s.engine = "Gaia";
    s.elo    = 1800;
    s.colour = 1;
    REQUIRE(writePlaySettings(profile, s));
    auto j   = nlohmann::json::parse(test::readFile(profile / "play.json"));
    j["elo"] = "strong";
    PlaySettings back;
    checkReader(
        profile / "play.json", j.dump(), "elo", [&] { back = readPlaySettings(profile); },
        [&] {
            CHECK(back.engine == "Gaia");
            CHECK(back.colour == 1);
            CHECK(back.elo == PlaySettings{}.elo);
        },
        [&] { CHECK(back.engine.empty()); });
}

TEST_CASE("profile types: assistant.json, a setting of another type keeps its default, the others read (#143)") {
    const test::ScratchDir profile("assistant-types");
    AssistantSettings back;
    checkReader(
        profile / "assistant.json", R"({"model_file": "mine.gguf", "gpu": "yes", "unload_minutes": 30})", "gpu",
        [&] { back = AssistantService(profile, "no-program").settings(); },
        [&] {
            CHECK(back.model_file == "mine.gguf");
            CHECK(back.unload_minutes == 30);
            CHECK(back.gpu == AssistantSettings{}.gpu);
        },
        [&] { CHECK(back.model_file.empty()); });
}

TEST_CASE("profile types: lichess.json, a setting of another type keeps its default, the others read (#143)") {
    // Read by App::loadLichess() through readProfileEntries(), as here.
    const test::ScratchDir profile("lichess-types");
    LichessSettings s;
    s.cloud               = 1;
    s.player              = "claire";
    s.followed            = {"duval", "brennan"};
    auto j                = nlohmann::json(s);
    j["explorer_ratings"] = "high";
    j["tv_channel"]       = 7;
    LichessSettings back;
    checkReader(
        profile / "lichess.json", j.dump(), "explorer_ratings, tv_channel",
        [&] { back = readProfileEntries<LichessSettings>(profile / "lichess.json"); },
        [&] {
            CHECK(back.cloud == 1);
            CHECK(back.player == "claire");
            CHECK(back.followed == s.followed);
            CHECK(back.explorer_ratings == LichessSettings{}.explorer_ratings);
            CHECK(back.tv_channel == LichessSettings{}.tv_channel);
        },
        [&] { CHECK(back.player.empty()); });
}

TEST_CASE("profile types: machines.json, a machine of another type set aside, the others kept (#143)") {
    const test::ScratchDir profile("machines-types");
    Machine one;
    one.id      = "m1";
    one.name    = "box-1";
    one.host    = "box-1.lan";
    Machine two = one;
    two.id      = "m2";
    two.name    = "box-2";
    REQUIRE(saveMachines(profile, {one, two}));
    auto j       = nlohmann::json::parse(test::readFile(profile / "machines.json"));
    j[1]["port"] = "ssh";
    std::vector<Machine> back;
    checkReader(
        profile / "machines.json", j.dump(), "box-2", [&] { back = loadMachines(profile); },
        [&] {
            REQUIRE(back.size() == 1);
            CHECK(back[0].name == "box-1");
        },
        [&] { CHECK(back.empty()); });
}

TEST_CASE("profile types: the bot's settings, one of another type keeps its default, the others read (#143)") {
    const test::ScratchDir folder("bot-types");
    lichess::BotSettings s;
    s.engine   = "Gaia";
    s.tables   = false;
    s.greeting = "Hello";
    s.declined = 3;
    REQUIRE(lichess::writeBotSettings(folder, s));
    auto j        = nlohmann::json::parse(test::readFile(folder / "bot.json"));
    j["declined"] = "many";
    lichess::BotSettings back;
    checkReader(
        folder / "bot.json", j.dump(), "declined", [&] { back = lichess::readBotSettings(folder); },
        [&] {
            CHECK(back.engine == "Gaia");
            CHECK_FALSE(back.tables);
            CHECK(back.greeting == "Hello");
            CHECK(back.declined == 0);
        },
        [&] { CHECK(back.engine.empty()); });
}

TEST_CASE("profile types: the online game's settings, one of another type keeps its default, the others read (#143)") {
    const test::ScratchDir folder("online-types");
    lichess::OnlinePlaySettings s;
    s.seek_minutes = 3;
    s.opponent     = "duval";
    s.seek_rated   = false;
    REQUIRE(lichess::writeOnlinePlay(folder, s));
    auto j          = nlohmann::json::parse(test::readFile(folder / "lichess.json"));
    j["seek_rated"] = "no";
    lichess::OnlinePlaySettings back;
    checkReader(
        folder / "lichess.json", j.dump(), "seek_rated", [&] { back = lichess::readOnlinePlay(folder); },
        [&] {
            CHECK(back.seek_minutes == 3);
            CHECK(back.opponent == "duval");
            CHECK(back.seek_rated == lichess::OnlinePlaySettings{}.seek_rated);
        },
        [&] { CHECK(back.opponent.empty()); });
}

TEST_CASE(
    "profile types: the engine downloads, an engine or a release of another type set aside, the others kept "
    "(#143)") {
    const test::ScratchDir profile("downloads-types");
    // The engines installed, read again and again as the pages show them.
    catalogue::Installed a;
    a.id      = "gaia";
    a.version = "1.0";
    a.program = (profile / "engines" / "gaia" / "gaia").string();
    auto b    = a;
    b.id      = "mover";
    REQUIRE(catalogue::saveInstalled(profile, {a, b}));
    const auto installed = catalogue::enginesFolder(profile) / "installed.json";
    auto j               = nlohmann::json::parse(test::readFile(installed));
    j[1]["installed"]    = "today";
    std::vector<catalogue::Installed> engines;
    checkReader(
        installed, j.dump(), "mover", [&] { engines = catalogue::loadInstalled(profile); },
        [&] {
            REQUIRE(engines.size() == 1);
            CHECK(engines[0].id == "gaia");
        },
        [&] { CHECK(engines.empty()); });

    // The releases kept a day: one of another type stopped the page that asked, json's type_error escaping.
    catalogue::Release one;
    one.tag       = "v1.0";
    one.published = "2026-11-18T00:00:00Z";
    auto two      = one;
    two.tag       = "v1.1";
    catalogue::keepReleases(profile, "mover", {one, two});
    const auto kept = catalogue::enginesFolder(profile) / "releases" / "mover.json";
    auto r          = nlohmann::json::parse(test::readFile(kept));
    r[1]["draft"]   = "no";
    std::optional<std::vector<catalogue::Release>> releases;
    checkReader(
        kept, r.dump(), "#2", [&] { REQUIRE_NOTHROW(releases = catalogue::keptReleases(profile, "mover")); },
        [&] {
            REQUIRE(releases);
            REQUIRE(releases->size() == 1);
            CHECK(releases->front().tag == "v1.0");
        },
        [&] { CHECK_FALSE(releases); });
}

TEST_CASE("profile types: the account's cache, an entry of another type set aside, the others kept (#143)") {
    const test::ScratchDir root("account-types");
    lichess::Profile p;
    p.username = "claire";
    p.read_at  = 1'700'000'000;
    p.speeds.push_back({"blitz", 1850, 120, false, {{1'690'000'000, 1800}}});
    p.speeds.push_back({"rapid", 1900, 40, true, {}});
    p.puzzle_rating = 2000;
    lichess::keepProfile(root, p);
    const auto file          = root.path() / "lichess-cache" / "account.json";
    auto j                   = nlohmann::json::parse(test::readFile(file));
    j["speeds"][1]["rating"] = "high";
    std::optional<lichess::Profile> back;
    checkReader(
        file, j.dump(), "speeds/#2", [&] { back = lichess::keptProfile(root); },
        [&] {
            REQUIRE(back);
            CHECK(back->username == "claire");
            CHECK(back->read_at == p.read_at);
            REQUIRE(back->speeds.size() == 1);
            CHECK(back->speeds[0] == p.speeds[0]);
            CHECK(back->puzzle_rating == 2000);
        },
        [&] { CHECK_FALSE(back); });
}

TEST_CASE("profile types: the explorer's cache, a move of another type set aside, the others kept (#143)") {
    const test::ScratchDir profile("explorer-types");
    lichess::Explorer e;
    e.white   = 10;
    e.draws   = 5;
    e.black   = 3;
    e.opening = "King's Pawn";
    lichess::ExplorerMove e4;
    e4.uci                = "e2e4";
    e4.san                = "e4";
    e4.white              = 6;
    auto d4               = e4;
    d4.uci                = "d2d4";
    d4.san                = "d4";
    e.moves               = {e4, d4};
    const std::string key = "lichess|blitz|start";
    keepExplorer(profile, key, e);
    fs::path file;
    for (const auto& entry : fs::directory_iterator(profile / "lichess-cache" / "explorer")) file = entry.path();
    REQUIRE_FALSE(file.empty());
    auto j                 = nlohmann::json::parse(test::readFile(file));
    j["moves"][1]["white"] = "many";
    std::optional<std::pair<lichess::Explorer, int64_t>> back;
    checkReader(
        file, j.dump(), "moves/#2", [&] { back = keptExplorer(profile, key); },
        [&] {
            REQUIRE(back);
            CHECK(back->first.white == 10);
            CHECK(back->first.opening == "King's Pawn");
            REQUIRE(back->first.moves.size() == 1);
            CHECK(back->first.moves[0].san == "e4");
            CHECK(back->second > 0);
        },
        [&] { CHECK_FALSE(back); });
}

TEST_CASE("profile types: play/games.jsonl, a game of another type or cut short left out and said (#143)") {
    const test::ScratchDir folder("played-types");
    PlayedGame one;
    one.at     = 1'700'000'000;
    one.engine = "Gaia";
    one.result = "1-0";
    auto two   = one;
    two.result = "0-1";
    appendPlayedGame(folder, one);
    appendPlayedGame(folder, two);
    auto three      = nlohmann::json(one);
    three["white"]  = "yes";  // a game of another type
    const auto file = folder / "games.jsonl";
    spit(file, test::readFile(file) + three.dump() + "\n" + nlohmann::json(two).dump().substr(0, 20) + "\n");
    std::vector<PlayedGame> back;
    saidOnce(saidWhile([&] { back = readPlayedGames(folder); }), {"games.jsonl: 2 lines could not be read"});
    REQUIRE(back.size() == 2);
    CHECK(back[0].result == "1-0");
    CHECK(back[1].result == "0-1");
    // Added to, never written again: the lines left out stay in the file.
    appendPlayedGame(folder, one);
    CHECK(readPlayedGames(folder).size() == 3);
}
