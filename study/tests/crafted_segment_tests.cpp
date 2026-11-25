// A segment that opens, its header and its files' sizes sound, but whose bytes were changed after it was written: a
// disk's damage, or a database made by hand to break the program (#102). Its games' offsets out of order or past their
// files, its annex records cut short or made up: nothing reads outside the files, nothing throws where nothing
// catches, every page's work goes to its end, and the games read as damaged. Games of our own.
#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <chess.hpp>
#include <study/dossier.hpp>
#include <study/import.hpp>
#include <study/opening_report.hpp>
#include <study/pgn_reader.hpp>
#include <study/query.hpp>
#include <study/scan_board.hpp>
#include <study/search.hpp>
#include <study/store.hpp>
#include <study/tree.hpp>

#include "test_games.hpp"

using namespace fcg::study;
using namespace fcg::test;
namespace fs = std::filesystem;

namespace {

// cols.bin as docs/design/study.md describes it: a 32-byte header, then each column 8-byte aligned, in this order (the
// seven names, the date and the plies 4 bytes a game; the ratings, ECO and flags 2; the result 1; the key 8; the
// offsets in moves.bin and annex.bin 8, n + 1 of each).
constexpr int PLIES = 8, MOVES = 15, ANNEX = 16, END = 17;
std::size_t columnAt(int column, std::size_t n) {
    const auto bytes = [&](int c) -> std::size_t {
        return c <= PLIES ? 4 * n : c <= 12 ? 2 * n : c == 13 ? n : c == 14 ? 8 * n : 8 * (n + 1);
    };
    std::size_t at = 32;
    for (int c = 0; c < column; ++c) at += (bytes(c) + 7) & ~std::size_t{7};
    return at;
}

std::string bytesOf(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeBytes(const fs::path& file, const std::string& bytes) {
    std::ofstream(file, std::ios::binary | std::ios::trunc) << bytes;
}

uint64_t u64At(const std::string& bytes, std::size_t at) {
    uint64_t v = 0;
    std::memcpy(&v, bytes.data() + at, 8);
    return v;
}

void putU64(std::string& bytes, std::size_t at, uint64_t v) { std::memcpy(bytes.data() + at, &v, 8); }

// The fixture's games (clocks, evaluations, positions of their own, Chess960, a damaged one), then a game with clocks
// alone in its annex (its record: the parts, the clocks' count, the clocks), twenty plies long.
std::vector<ImportedGame> gamesOfOurOwn() {
    std::vector<ImportedGame> games;
    ImportProgress progress;
    std::atomic<bool> cancel{false};
    const auto report = importGames(data("games.pgn"), ImportOptions{2, 4096}, progress, cancel,
                                    [&](std::vector<ImportedGame>&& batch) {
                                        for (auto& g : batch) games.push_back(std::move(g));
                                    });
    REQUIRE(report.error.empty());
    std::string text =
        "[Event \"Club Cup\"]\n[Site \"Lyon\"]\n[Date \"2026.11.25\"]\n[Round \"1\"]\n[White \"Moreau, Claire\"]\n"
        "[Black \"Duval, Marc\"]\n[Result \"1-0\"]\n\n";
    const char* moves[] = {"e4",  "e5", "Nf3", "Nc6", "Bb5", "a6",  "Ba4", "Nf6", "O-O", "Be7",
                           "Re1", "b5", "Bb3", "d6",  "c3",  "O-O", "h3",  "Nb8", "d4",  "Nbd7"};
    for (int i = 0; i < 20; ++i)
        text += (i % 2 == 0 ? std::to_string(i / 2 + 1) + ". " : std::string()) + moves[i] +
                " { [%clk 0:" + std::to_string(59 - i) + ":00] } ";
    text += "1-0\n";
    ImportedGame clocks;
    REQUIRE(readImportedGame(text, clocks));
    REQUIRE(clocks.clocks.size() == 20);
    REQUIRE(clocks.text.empty());
    games.push_back(std::move(clocks));
    return games;
}

// Everything the pages do with a database, on the snapshot given: the list (its rows, a filter by names, sorts), every
// game read and written as PGN, the searches, the opening tree built and asked, a player's dossier, an opening report
// and the check. Nothing of it may crash or throw; the check finds the games given as damaged.
void everything(Database& db, const std::vector<GameId>& damaged) {
    const auto snap = db.snapshot();
    std::atomic<bool> cancel{false};

    CHECK(makeView(snap, Filter{}, SortBy::Number).ids.size() == snap->games());
    Filter named;
    named.player = "moreau";
    (void)makeView(snap, named, SortBy::White);
    (void)makeView(snap, Filter{}, SortBy::Date, true);
    (void)makeView(snap, Filter{}, SortBy::Event);
    for (GameId id = 0; id < snap->games(); ++id) {
        const auto row   = snap->row(id);
        const auto names = snap->rowIds(id);
        CHECK(names.white < snap->names());
        CHECK(names.round < snap->names());
        (void)snap->shownName(names.black);
        const auto game = snap->game(id);
        CHECK(game.clocks.size() <= game.plies);
        CHECK(game.plies <= game.moves.size());
        (void)readPgnGame(importedPgn(game));
        (void)snap->tag(id, "Site");
        (void)snap->startFen(id);
        (void)snap->storedBytes(id);
        (void)row;
    }

    ScanBoard start;
    REQUIRE(start.setFen(chess::constants::STARTPOS, false));
    ScanBoard after_e4 = start;
    after_e4.play(*after_e4.parseSan("e4"));
    (void)searchAll(snap, PositionQuery{after_e4}, 2);
    MaterialQuery material;
    material.high = {8, 2, 2, 2, 1, 8, 2, 2, 2, 1};
    material.low  = {0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    (void)searchAll(snap, material, 2);
    PatternQuery pattern;
    pattern.men[WHITE_SIDE][KNIGHT] = uint64_t{1} << 21;  // a knight on f3
    (void)searchAll(snap, pattern, 2);
    ManoeuvreQuery route;
    route.route = {6, 21, 36};  // Ng1-f3-e5
    (void)searchAll(snap, route, 2);
    StructureQuery structure;
    structure.pawns    = {0xFF00, 0x00FF'0000'0000'0000};
    structure.distance = 2;
    (void)searchAll(snap, structure, 2);

    TreeProgress tree_progress;
    TreeOptions tree_options;
    tree_options.threads = 2;
    REQUIRE(buildTree(db, *snap, tree_options, tree_progress, cancel).error.empty());
    const auto tree = Tree::open(db.folder());
    REQUIRE(tree);
    (void)tree->at(*snap, start.key());
    (void)tree->gamesAt(*snap, after_e4.key());

    const DossierSubject subject{std::string(snap->row(0).white)};
    DossierOptions dossier_options;
    dossier_options.threads = 2;
    DossierProgress dossier_progress;
    const auto dossier =
        makeDossier({findSubject("Crafted", snap, subject)}, subject, dossier_options, dossier_progress, cancel);
    CHECK_FALSE(dossier.games.empty());
    (void)dossierFigures(dossier, DossierParts::Board);

    const std::vector<ReportSource> sources{{"Crafted", snap, tree}};
    ReportOptions report_options;
    report_options.threads     = 2;
    report_options.least_games = 5;
    ReportProgress report_progress;
    (void)makeOpeningReport(sources, start, report_options, report_progress, cancel);
    (void)makeOpeningReport(sources, after_e4, report_options, report_progress, cancel);

    CheckProgress check_progress;
    const auto report = db.check(check_progress, cancel, 2);
    for (const auto id : damaged) {
        CAPTURE(id);
        CHECK(std::binary_search(report.damaged.begin(), report.damaged.end(), id));
    }
}

}  // namespace

TEST_CASE("crafted segment: offsets out of order or past their files, annex records made up; nothing crashes") {
    const auto games = gamesOfOurOwn();
    const auto base  = fs::temp_directory_path() / "crafted-base.fcgdb";
    fs::remove_all(base);
    Database::create(base)->append(games, true);
    const auto n = games.size();
    std::string segment;
    {
        auto db = Database::open(base);
        REQUIRE(db->snapshot()->games() == n);
        REQUIRE(db->snapshot()->segmentInfos().size() == 1);
        segment = db->snapshot()->segmentInfos().front().name;
    }
    const auto cols_base  = bytesOf(base / segment / "cols.bin");
    const auto annex_base = bytesOf(base / segment / "annex.bin");
    const auto moves_size = fs::file_size(base / segment / "moves.bin");
    REQUIRE(cols_base.size() == columnAt(END, n));  // the format as written down
    REQUIRE(u64At(cols_base, columnAt(MOVES, n) + 8 * n) == moves_size);
    REQUIRE(u64At(cols_base, columnAt(ANNEX, n) + 8 * n) == annex_base.size());

    // A database of the base's files, `cols` and `annex` written over its segment's.
    const auto made = [&](const std::string& cols, const std::string& annex) {
        const auto dir = fs::temp_directory_path() / "crafted.fcgdb";
        fs::remove_all(dir);
        fs::copy(base, dir, fs::copy_options::recursive);
        writeBytes(dir / segment / "cols.bin", cols);
        writeBytes(dir / segment / "annex.bin", annex);
        auto db = Database::open(dir);
        REQUIRE(db->snapshot()->unreadable().empty());  // it opens: only the games' bytes are changed
        return db;
    };
    const auto annexOf = [&](GameId id) { return u64At(cols_base, columnAt(ANNEX, n) + 8 * id); };
    GameId fen_game    = n;  // a game of a position of its own, past the games damaged below
    {
        auto db = Database::open(base);
        for (GameId id = 11; id + 1 < n && fen_game == n; ++id)
            if (db->snapshot()->numbers(id).flags & GAME_FEN) fen_game = id;
    }
    REQUIRE(fen_game + 1 < n);
    const GameId clocks_game = n - 1;

    SUBCASE("each kind of damage on its own, the games it touches read as damaged") {
        auto cols  = cols_base;
        auto annex = annex_base;
        // An offset is a game's start and the end of the game before. Games 1 and 2's moves past moves.bin; game 5's
        // annex ends before it begins (game 4's then ends in game 6's: it reads whole); games 9 and 10's annex past
        // annex.bin.
        putU64(cols, columnAt(MOVES, n) + 8 * 2, moves_size + 1000);
        putU64(cols, columnAt(ANNEX, n) + 8 * 5, u64At(cols, columnAt(ANNEX, n) + 8 * 6) + 1);
        putU64(cols, columnAt(ANNEX, n) + 8 * 10, ~uint64_t{0});
        // A position of its own whose FEN's length is made up: past the record.
        const auto fen = annexOf(fen_game);
        REQUIRE((annex[fen] & 1) != 0);
        annex[fen + 1] = static_cast<char>(0xFF);
        // The clocks' count made up, and the plies: four thousand million of each, which no record holds.
        const auto clocks = annexOf(clocks_game);
        REQUIRE(annex[clocks] == 8);  // clocks alone
        for (int k = 1; k <= 4; ++k) annex[clocks + k] = static_cast<char>(0xFF);
        annex[clocks + 5]    = 0x0F;
        const uint32_t plies = 0xFFFFFFFF;
        std::memcpy(cols.data() + columnAt(PLIES, n) + 4 * clocks_game, &plies, 4);

        auto db         = made(cols, annex);
        const auto snap = db->snapshot();
        for (const GameId id : {GameId{1}, GameId{2}}) {  // their moves lost, their annex read
            CAPTURE(id);
            const auto game = snap->game(id);
            CHECK(game.damaged);
            CHECK(game.moves.empty());
            CHECK(game.plies == 0);
            CHECK(game.clocks.empty());
            CHECK(snap->gameMoves(id).codes.empty());
        }
        for (const GameId id : {GameId{5}, GameId{9}, GameId{10}}) {  // their annex lost, their moves read
            CAPTURE(id);
            const auto game = snap->game(id);
            CHECK(game.damaged);
            CHECK(game.clocks.empty());
            CHECK(game.text.empty());
            CHECK(snap->gameMoves(id).codes.size() == games[id].moves.size());
            CHECK(game.moves == ((snap->numbers(id).flags & GAME_FEN) ? std::vector<uint8_t>{} : games[id].moves));
        }
        CHECK(snap->game(4).damaged == games[4].damaged);  // read whole, as it was imported
        CHECK(snap->game(4).clocks == games[4].clocks);
        CHECK(snap->startFen(fen_game).empty());
        ScanBoard board;
        CHECK_FALSE(startOf(*snap, fen_game, snap->numbers(fen_game).flags, board));
        const auto lost = snap->game(fen_game);
        CHECK(lost.damaged);
        CHECK(lost.moves.empty());  // played from a position that is lost
        const auto crafted = snap->game(clocks_game);
        CHECK(crafted.damaged);
        CHECK(crafted.clocks.empty());
        CHECK(crafted.plies <= crafted.moves.size());
        CHECK(crafted.moves == games.back().moves);
        everything(*db, {1, 2, 5, 9, 10, fen_game, clocks_game});
    }

    SUBCASE("offsets and annex bytes changed at random, sixty times") {
        std::mt19937 random(102);
        for (int round = 0; round < 60; ++round) {
            CAPTURE(round);
            auto cols  = cols_base;
            auto annex = annex_base;
            for (int k = 0; k < 3; ++k) {
                const bool of_moves = random() % 2;
                const auto size     = of_moves ? moves_size : annex.size();
                // Never the last one, which must be the file's size for the segment to open.
                const auto game = random() % n;
                uint64_t value  = 0;
                switch (random() % 3) {
                    case 0:
                        value = size + 1 + random() % 1000;
                        break;
                    case 1:
                        value = ~uint64_t{0} - random() % 16;
                        break;
                    default:
                        value = random() % (size + 1);  // most often out of order
                }
                putU64(cols, columnAt(of_moves ? MOVES : ANNEX, n) + 8 * game, value);
            }
            for (int k = 0; k < 12; ++k) annex[random() % annex.size()] ^= static_cast<char>(1 + random() % 255);
            auto db = made(cols, annex);
            everything(*db, {});
        }
    }
}

TEST_CASE("crafted segment: an edits file's counts made up to overflow its size check; a manifest made up") {
    auto db = database("crafted-edits.fcgdb", randomGames(20, 7, 4));
    db->setDeleted(std::vector<GameId>{3}, true);
    const auto dir = db->folder();
    db.reset();
    std::string edits_name;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().filename().string().rfind("edits-", 0) == 0) edits_name = e.path().filename().string();
    REQUIRE_FALSE(edits_name.empty());
    const auto edits = bytesOf(dir / edits_name);
    REQUIRE(edits.size() == 40);  // the header, one game deleted
    const auto manifest = bytesOf(dir / "manifest.json");

    // Counts whose products wrap around to the file's size: refused, nothing read past the file.
    for (const auto& [replaced, deleted] :
         std::vector<std::pair<uint64_t, uint64_t>>{{uint64_t{1} << 60, 1},
                                                    {0, (uint64_t{1} << 61) + 1},
                                                    {(uint64_t{1} << 60) + 1, (uint64_t{1} << 62) - 1}}) {
        CAPTURE(replaced);
        CAPTURE(deleted);
        auto crafted = edits;
        putU64(crafted, 16, replaced);
        putU64(crafted, 24, deleted);
        writeBytes(dir / edits_name, crafted);
        CHECK_THROWS_AS(Database::open(dir), std::runtime_error);
    }
    // A game deleted twice in the file counts once.
    auto twice = edits;
    putU64(twice, 24, 2);
    twice += twice.substr(32, 8);
    writeBytes(dir / edits_name, twice);
    {
        auto again = Database::open(dir);
        CHECK(again->snapshot()->deletedCount() == 1);
        CHECK(again->snapshot()->deleted(3));
    }
    writeBytes(dir / edits_name, edits);

    // A manifest that gives a segment more games than a commit writes, or games without names: refused.
    const auto games_at = manifest.find("\"games\"");
    REQUIRE(games_at != std::string::npos);
    auto many           = manifest;
    const auto value_at = many.find_first_of("0123456789", games_at);
    many.replace(value_at, many.find_first_not_of("0123456789", value_at) - value_at, "4000000000");
    writeBytes(dir / "manifest.json", many);
    CHECK_THROWS_AS(Database::open(dir), std::runtime_error);
    auto nameless       = manifest;
    const auto names_at = nameless.find_first_of("0123456789", nameless.find("\"names\""));
    nameless.replace(names_at, nameless.find_first_not_of("0123456789", names_at) - names_at, "0");
    writeBytes(dir / "manifest.json", nameless);
    CHECK_THROWS_AS(Database::open(dir), std::runtime_error);
    writeBytes(dir / "manifest.json", manifest);
    CHECK(Database::open(dir)->snapshot()->games() == 20);
}

TEST_CASE("crafted segment: a table of names whose spellings are past the database's names") {
    auto db         = database("crafted-players.fcgdb", gamesOfOurOwn());
    const auto snap = db->snapshot();
    Player claire;
    claire.name      = "Moreau, Claire";
    claire.spellings = {snap->rowIds(snap->games() - 1).white};
    db->setPlayers(PlayerTable(std::vector<Player>{claire}));
    const auto dir = db->folder();
    db.reset();
    std::string players_name;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().filename().string().rfind("players-", 0) == 0) players_name = e.path().filename().string();
    REQUIRE_FALSE(players_name.empty());
    // The file written again with name ids no game has, one of them four thousand million: a dossier would have made
    // room for each name up to it.
    claire.spellings.push_back(0xFF000000);
    claire.spellings.push_back(snap->names());
    writeBytes(dir / players_name, PlayerTable(std::vector<Player>{claire}).bytes());

    db              = Database::open(dir);
    const auto read = db->snapshot();
    REQUIRE(read->players()->players().size() == 1);
    CHECK(read->players()->players()[0].spellings == std::vector<uint32_t>{snap->rowIds(snap->games() - 1).white});
    CHECK_FALSE(read->players()->playerOf(0xFF000000));
    const DossierSubject subject{"Moreau, Claire"};
    const auto source = findSubject("Crafted", read, subject);
    CHECK(
        std::all_of(source.spellings.begin(), source.spellings.end(), [&](uint32_t id) { return id < read->names(); }));
    DossierProgress progress;
    std::atomic<bool> cancel{false};
    CHECK_FALSE(makeDossier({source}, subject, DossierOptions{}, progress, cancel).games.empty());
}
