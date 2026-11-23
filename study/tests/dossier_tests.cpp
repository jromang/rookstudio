// A player's dossier (#90, mockup M13, board 3): their games found through the table of names (all their spellings
// together, a namesake's games left out), and its numbers (openings, results, rating by year, structures, the tree of
// their games and the repertoires' gaps) compared with a count made apart over games of our own. Players' names are
// invented, and so is the FIDE list.
#include <doctest/doctest.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <chess.hpp>
#include <study/dossier.hpp>
#include <study/fide_list.hpp>
#include <study/game_tree.hpp>
#include <study/names.hpp>
#include <study/pgn_reader.hpp>
#include <study/players.hpp>
#include <study/query.hpp>
#include <study/repertoire.hpp>
#include <study/store.hpp>

#include "test_games.hpp"

using namespace fcg::study;
using namespace fcg::test;
namespace fs = std::filesystem;

namespace {

// The named openings of the tests: a few lines of their own, in the table's format.
const char* const OPENINGS =
    "eco\tname\tpgn\n"
    "B00\tKing's Pawn Game\t1. e4\n"
    "D00\tQueen's Pawn Game\t1. d4\n"
    "C60\tRuy Lopez\t1. e4 e5 2. Nf3 Nc6 3. Bb5\n"
    "C65\tRuy Lopez: Berlin Defense\t1. e4 e5 2. Nf3 Nc6 3. Bb5 Nf6\n"
    "C50\tItalian Game\t1. e4 e5 2. Nf3 Nc6 3. Bc4\n"
    "C54\tItalian Game: Classical Variation\t1. e4 e5 2. Nf3 Nc6 3. Bc4 Bc5 4. c3 Nf6\n"
    "B90\tSicilian Defense: Najdorf Variation\t1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6\n"
    "B90\tSicilian Defense: Najdorf Variation, English Attack\t1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 "
    "6. Be3 e5\n"
    "B96\tSicilian Defense: Najdorf Variation\t1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 6. Bg5 e6 7. f4\n"
    "E32\tNimzo-Indian Defense: Classical Variation\t1. d4 Nf6 2. c4 e6 3. Nc3 Bb4 4. Qc2\n"
    "D35\tQueen's Gambit Declined: Exchange Variation\t1. d4 d5 2. c4 e6 3. Nc3 Nf6 4. cxd5 exd5\n"
    "X99\tA line that does not replay\t1. e4 e4\n";

// The openings the games start with, and the ECO code of the last named position they reach.
struct Prefix {
    const char* moves;
    const char* eco;
};
const Prefix BERLIN{"e4 e5 Nf3 Nc6 Bb5 Nf6", "C65"};
const Prefix ITALIAN{"e4 e5 Nf3 Nc6 Bc4 Bc5 c3 Nf6", "C54"};
const Prefix NAJDORF_BE3{"e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6 Nc3 a6 Be3 e5", "B90"};
const Prefix NAJDORF_BG5{"e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6 Nc3 a6 Bg5 e6 f4", "B96"};
const Prefix NIMZO{"d4 Nf6 c4 e6 Nc3 Bb4 Qc2", "E32"};
const Prefix EXCHANGE{"d4 d5 c4 e6 Nc3 Nf6 cxd5 exd5", "D35"};

// A game of the tests: its prefix, then legal moves a seeded generator chooses, up to `plies` (fewer at a mate).
struct G {
    std::string white, black;
    Prefix prefix;
    char result   = '1';
    int white_elo = 0, black_elo = 0;
    std::string date = "2024.05.10";
    int plies        = 56;
    uint32_t seed    = 1;
    std::string white_fide, black_fide;  // the FIDE ids it gives
    std::string time_control;
    std::string event = "Autumn Open";
};

std::vector<std::string> movesOf(const G& g) {
    std::vector<std::string> moves;
    chess::Board board;
    std::string prefix = g.prefix.moves;
    for (std::size_t at = 0; at < prefix.size();) {
        auto end = prefix.find(' ', at);
        if (end == std::string::npos) end = prefix.size();
        const auto san = prefix.substr(at, end - at);
        at             = end + 1;
        board.makeMove(chess::uci::parseSan(board, san));
        moves.push_back(san);
    }
    std::mt19937 random(g.seed);
    while (static_cast<int>(moves.size()) < g.plies) {
        chess::Movelist list;
        chess::movegen::legalmoves(list, board);
        if (list.empty()) break;
        std::vector<chess::Move> sorted(list.begin(), list.end());
        std::sort(sorted.begin(), sorted.end(), [](const chess::Move& a, const chess::Move& b) {
            return chess::uci::moveToUci(a) < chess::uci::moveToUci(b);
        });
        const auto move = sorted[random() % sorted.size()];
        moves.push_back(chess::uci::moveToSan(board, move));
        board.makeMove(move);
    }
    return moves;
}

ImportedGame gameOf(const G& g) {
    const std::string result = g.result == '1' ? "1-0" : g.result == '0' ? "0-1" : g.result == '=' ? "1/2-1/2" : "*";
    std::string text         = "[Event \"" + g.event + "\"]\n[Date \"" + g.date + "\"]\n[White \"" + g.white +
                               "\"]\n[Black \"" + g.black + "\"]\n";
    if (g.white_elo) text += "[WhiteElo \"" + std::to_string(g.white_elo) + "\"]\n";
    if (g.black_elo) text += "[BlackElo \"" + std::to_string(g.black_elo) + "\"]\n";
    if (!g.white_fide.empty()) text += "[WhiteFideId \"" + g.white_fide + "\"]\n";
    if (!g.black_fide.empty()) text += "[BlackFideId \"" + g.black_fide + "\"]\n";
    if (!g.time_control.empty()) text += "[TimeControl \"" + g.time_control + "\"]\n";
    text += "[Result \"" + result + "\"]\n\n";
    const auto moves = movesOf(g);
    for (std::size_t i = 0; i < moves.size(); ++i)
        text += (i % 2 == 0 ? std::to_string(i / 2 + 1) + ". " : std::string()) + moves[i] + " ";
    text += result + "\n";
    ImportedGame game;
    REQUIRE(readImportedGame(text, game));
    REQUIRE_FALSE(game.damaged);
    return game;
}

std::shared_ptr<Database> databaseOf(const std::string& name, const std::vector<G>& games) {
    std::vector<ImportedGame> imported;
    for (const auto& g : games) imported.push_back(gameOf(g));
    return database(name, std::move(imported));
}

// The structures of the tests, a rule of their own on the pawns: an isolated d-pawn (a d-pawn and no c- or e-pawn of
// its side: kind 1), and no d-pawn on the board at all (kind 2, White's).
constexpr uint64_t FILE_C = 0x0404040404040404ull, FILE_D = 0x0808080808080808ull, FILE_E = 0x1010101010101010ull;
void testStructures(uint64_t white, uint64_t black, std::vector<std::pair<int, int>>& out) {
    for (int side = 0; side < 2; ++side) {
        const auto pawns = side == 0 ? white : black;
        if ((pawns & FILE_D) && !(pawns & (FILE_C | FILE_E))) out.emplace_back(1, side);
    }
    if (!((white | black) & FILE_D)) out.emplace_back(2, 0);
}

// The same rule counted apart: the game replayed on chess.hpp's board to its 40th ply.
std::vector<std::pair<int, int>> structuresApart(const G& g) {
    const auto moves = movesOf(g);
    if (moves.size() < 40) return {};
    chess::Board board;
    for (std::size_t i = 0; i < 40; ++i) board.makeMove(chess::uci::parseSan(board, moves[i]));
    std::vector<std::pair<int, int>> out;
    testStructures(board.pieces(chess::PieceType::PAWN, chess::Color::WHITE).getBits(),
                   board.pieces(chess::PieceType::PAWN, chess::Color::BLACK).getBits(), out);
    return out;
}

const DossierSubject DUVAL{"Duval, Anne", 90000011, 1994, ""};

// Anne Duval's games under three spellings, with the user and others; and games that are not hers.
std::vector<G> duvalGames() {
    std::vector<G> v;
    uint32_t seed  = 100;
    const auto add = [&](G g) {
        g.seed = seed++;
        v.push_back(std::move(g));
    };
    // With White: the Berlin, the Italian, the Exchange.
    add({"Duval, Anne", "Ito, Ken", BERLIN, '1', 2402, 2380, "2026.10.21", 60, 0, "90000011"});
    add({"Duval, Anne", "Brennan, Liam", BERLIN, '=', 2398, 2450, "2026.10.20"});
    add({"Duval,A", "Haddad, Sami", BERLIN, '=', 2390, 2290, "2025.06.02"});
    add({"Anne Duval", "Okafor, Ada", BERLIN, '0', 2380, 2410, "2024.03.15", 30});
    add({"Duval, Anne", "Lindqvist, Erik", ITALIAN, '1', 2412, 2300, "2026.04.14"});
    add({"Duval,A", "Ito, Ken", ITALIAN, '1', 2350, 2360, "2023.08.01"});
    add({"Duval, Anne", "Tester, Sam", ITALIAN, '=', 2341, 2105, "2025.02.11"});
    add({"Duval, Anne", "Brennan, Liam", EXCHANGE, '1', 2310, 2290, "2022.09.09", 70});
    add({"Duval, Anne", "Haddad, Sami", EXCHANGE, '0', 2300, 2320, "2021.11.30", 64});
    // With Black: the Najdorf twice over, the Nimzo-Indian.
    add({"Ito, Ken", "Duval, Anne", NAJDORF_BE3, '0', 2380, 2405, "2026.10.17"});
    add({"Lindqvist, Erik", "Duval, Anne", NAJDORF_BE3, '1', 2430, 2399, "2026.10.18"});
    add({"Brennan, Liam", "Duval,A", NAJDORF_BE3, '=', 2440, 2380, "2025.01.05"});
    add({"Okafor, Ada", "Anne Duval", NAJDORF_BG5, '0', 2350, 2370, "2024.07.07", 44});
    add({"Tester, Sam", "Duval, Anne", NAJDORF_BG5, '1', 2120, 2366, "2024.11.11"});
    add({"Haddad, Sami", "Duval, Anne", NIMZO, '=', 2333, 2333, "2023.05.05"});
    add({"Ito, Ken", "Duval, Anne", NIMZO, '0', 2300, 2290, "2022.02.02", 48});
    add({"Brennan, Liam", "Duval, Anne", NIMZO, '*', 0, 2280, "2021.06.06"});
    // A rapid game: its time control says so.
    add({"Duval, Anne", "Okafor, Ada", BERLIN, '1', 2380, 2350, "2025.12.12", 50, 0, "", "", "900+10"});
    // Not hers: two other players, and "Duval, Marc".
    add({"Ito, Ken", "Brennan, Liam", BERLIN, '1', 2380, 2450, "2026.01.01"});
    add({"Duval, Marc", "Haddad, Sami", NAJDORF_BE3, '0', 2000, 2290, "2026.02.02"});
    return v;
}

bool userName(std::string_view name) { return name == "Tester, Sam" || name == "Tester_Sam"; }

// The table of names that brings her three spellings together, matched with FIDE's list.
PlayerTable duvalTable(const Snapshot& snap) {
    std::vector<uint32_t> ids;
    for (uint32_t id = 0; id < snap.names(); ++id) {
        const auto n = snap.name(id);
        if (n == "Duval, Anne" || n == "Duval,A" || n == "Anne Duval") ids.push_back(id);
    }
    REQUIRE(ids.size() == 3);
    return withPlayer(PlayerTable{}, ids, "Duval, Anne", [](Player& p) {
        p.fide       = 90000011;
        p.federation = "FRA";
        p.title      = "WIM";
        p.born       = 1994;
        p.standard   = 2412;
        p.how        = "list";
        p.list       = "2026-11";
    });
}

bool hers(const G& g, bool white) {
    const auto& name = white ? g.white : g.black;
    return name == "Duval, Anne" || name == "Duval,A" || name == "Anne Duval";
}

double pointsOf(const G& g, bool white) {
    if (g.result == '=') return 0.5;
    return (g.result == '1') == white ? 1.0 : 0.0;
}

int yearOf(const std::string& date) { return std::stoi(date.substr(0, 4)); }

uint64_t keyAfter(const std::vector<std::string>& line) {
    chess::Board board;
    for (const auto& san : line) board.makeMove(chess::uci::parseSan(board, san));
    ScanBoard scan;
    REQUIRE(scan.set(board));
    return scan.key();
}

int codeAt(const std::vector<std::string>& line, const std::string& san) {
    chess::Board board;
    for (const auto& m : line) board.makeMove(chess::uci::parseSan(board, m));
    ScanBoard scan;
    REQUIRE(scan.set(board));
    const auto coded = scan.parseSan(san);
    REQUIRE(coded);
    return scan.encode(*coded);
}

std::string pad(std::string s, std::size_t width) {
    s.resize(std::max(s.size(), width), ' ');
    return s;
}

// A FIDE list of invented players in FIDE's fixed columns, read into RookStudio's file.
std::unique_ptr<FideList> inventedList(const std::string& name) {
    constexpr std::size_t widths[] = {15, 61, 4, 4, 5, 5, 15, 4, 6, 4, 3, 6, 4, 3, 6, 4, 3, 6, 4};
    const auto row                 = [&](const std::vector<std::string>& values) {
        std::string out;
        for (std::size_t c = 0; c < values.size(); ++c) out += pad(values[c], widths[c]);
        return out + "\r\n";
    };
    const auto player = [&](const std::string& id, const std::string& who, const std::string& fed, int standard,
                            int rapid, const std::string& born) {
        return row({id, who, fed, "M", "", "", "", "", std::to_string(standard), "0", "20", std::to_string(rapid), "0",
                    "20", "0", "0", "20", born, ""});
    };
    std::string text = row({"ID Number", "Name", "Fed", "Sex", "Tit", "WTit", "OTit", "FOA", "SRtng", "SGm", "SK",
                            "RRtng", "RGm", "Rk", "BRtng", "BGm", "BK", "B-day", "Flag"});
    text += player("90000004", "Martin, Paul", "FRA", 2105, 0, "1979");
    text += player("90000005", "Martin, Paul", "FRA", 1642, 0, "2006");
    text += player("90000006", "Martin, Paul", "CAN", 0, 1980, "1995");
    text += player("90000011", "Duval, Anne", "FRA", 2412, 2380, "1994");
    const auto source = fs::temp_directory_path() / (name + ".txt");
    {
        std::ofstream out(source, std::ios::binary);
        out << text;
    }
    const auto file = fs::temp_directory_path() / name / "list.bin";
    FidePrepareProgress progress;
    std::atomic<bool> cancel{false};
    prepareFideList(source, file, "2026-11", progress, cancel);
    return std::make_unique<FideList>(file);
}

}  // namespace

TEST_CASE("dossier: the named openings by position, each ECO code's shortest line, a family") {
    const OpeningNames names(OPENINGS);
    CHECK(names.size() == 11);  // the line that does not replay left out
    const auto berlin = names.at(keyAfter({"e4", "e5", "Nf3", "Nc6", "Bb5", "Nf6"}));
    REQUIRE(berlin);
    CHECK(names[*berlin].eco == "C65");
    CHECK(names[*berlin].moves == std::vector<std::string>{"e4", "e5", "Nf3", "Nc6", "Bb5", "Nf6"});
    // A transposition finds the name too.
    const auto nimzo = names.at(keyAfter({"c4", "e6", "Nc3", "Nf6", "d4", "Bb4", "Qc2"}));
    REQUIRE(nimzo);
    CHECK(names[*nimzo].eco == "E32");
    CHECK_FALSE(names.at(keyAfter({"e4", "e5", "Nf3"})));
    // B90's shortest line is the Najdorf itself, not its English Attack.
    const auto b90 = names.shortest("B90");
    REQUIRE(b90);
    CHECK(names[*b90].moves.size() == 10);
    CHECK(names[*b90].name == "Sicilian Defense: Najdorf Variation");
    CHECK_FALSE(names.shortest("A00"));
    CHECK(openingFamily("Sicilian Defense: Najdorf Variation, English Attack") ==
          "Sicilian Defense: Najdorf Variation");
    CHECK(openingFamily("Ruy Lopez") == "Ruy Lopez");
}

TEST_CASE("dossier: a game's speed from its time control, else from its event") {
    CHECK(gameSpeed("5400+30", "", false) == Speed::Classical);
    CHECK(gameSpeed("40/7200:3600", "", false) == Speed::Classical);
    CHECK(gameSpeed("900+10", "", false) == Speed::Rapid);
    CHECK(gameSpeed("180+2", "", false) == Speed::Blitz);
    CHECK(gameSpeed("600", "", false) == Speed::Blitz);
    CHECK(gameSpeed("", "World Blitz Championship", false) == Speed::Blitz);
    CHECK(gameSpeed("?", "Open rapide de Lyon", false) == Speed::Rapid);
    CHECK(gameSpeed("", "Autumn Open", false) == Speed::Classical);
    // Online, as Lichess counts: the base and 40 increments.
    CHECK(gameSpeed("60+0", "", true) == Speed::Bullet);
    CHECK(gameSpeed("180+2", "", true) == Speed::Blitz);
    CHECK(gameSpeed("600+0", "", true) == Speed::Rapid);
    CHECK(gameSpeed("1800+20", "", true) == Speed::Classical);
    CHECK(gameSpeed("-", "Rated Correspondence game", true) == Speed::Correspondence);
    CHECK(gameSpeed("1/259200", "", true) == Speed::Correspondence);
    CHECK(gameSpeed("", "", true) == Speed::Unknown);
}

TEST_CASE("dossier: her games under three spellings, and its numbers against a count made apart") {
    const auto games = duvalGames();
    auto db          = databaseOf("dossier-duval", games);
    db->setPlayers(duvalTable(*db->snapshot()));
    const auto snap = db->snapshot();
    const OpeningNames names(OPENINGS);

    // Her spellings found through the table's player, by her FIDE id; "Duval, Marc" is not one.
    const auto source = findSubject("Club", snap, DUVAL);
    CHECK(source.spellings.size() == 3);
    CHECK(source.doubtful.empty());
    // Without her FIDE id, by her name: the same.
    CHECK(findSubject("Club", snap, DossierSubject{"duval, anne"}).spellings == source.spellings);

    DossierOptions options;
    options.openings = &names;
    options.user     = userName;
    options.threads  = 3;
    DossierProgress progress;
    std::atomic<bool> cancel{false};
    const auto dossier = makeDossier({source}, DUVAL, options, progress, cancel);
    REQUIRE_FALSE(dossier.cancelled);
    CHECK(progress.done == progress.total);

    // Apart: her games, by colour, line, year.
    std::size_t count = 0;
    DossierScore all, white, black, stronger, weaker, user;
    std::map<std::pair<bool, std::string>, std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, std::string>> lines;
    std::map<int, std::pair<double, int>> years;
    std::map<std::tuple<int, bool, bool>, std::pair<uint32_t, double>> shapes;
    int highest = 0;
    std::string highest_date, latest;
    for (const auto& g : games) {
        const bool w = hers(g, true), b = hers(g, false);
        if (!w && !b) continue;
        ++count;
        const int own = w ? g.white_elo : g.black_elo, opp = w ? g.black_elo : g.white_elo;
        auto& line = lines[{w, g.prefix.eco}];
        ++std::get<0>(line);
        if (g.result != '*') {
            const auto p = pointsOf(g, w);
            ++(p == 1 ? std::get<1>(line) : p == 0 ? std::get<3>(line) : std::get<2>(line));
        }
        if (g.date > std::get<4>(line)) std::get<4>(line) = g.date;
        if (g.time_control.empty() && own > 0) {  // the classical games
            years[yearOf(g.date)].first += own;
            ++years[yearOf(g.date)].second;
            if (own > highest) {
                highest      = own;
                highest_date = g.date;
            }
        }
        if (g.date > latest) latest = g.date;
        std::set<std::tuple<int, bool, bool>> once;
        for (const auto& [kind, owner] : structuresApart(g)) {
            const auto k = std::tuple{kind, owner == (w ? 0 : 1), w};
            if (!once.insert(k).second) continue;
            ++shapes[k].first;
            shapes[k].second += g.result == '*' ? 0 : pointsOf(g, w);
        }
        (void)opp;
    }
    REQUIRE(dossier.games.size() == count);
    CHECK(count == 18);
    // The latest first.
    CHECK(dateText(dossier.games.front().numbers.date) == latest);
    for (std::size_t i = 1; i < dossier.games.size(); ++i)
        CHECK(dossier.games[i - 1].numbers.date >= dossier.games[i].numbers.date);

    const auto f = dossierFigures(dossier, DossierParts::Board, testStructures);
    CHECK(f.games == count);
    CHECK(f.white.games == 10);
    CHECK(f.black.games == 8);
    CHECK(f.all.won + f.all.drawn + f.all.lost == 17);  // one game without a result
    CHECK(f.first == packDate("2021.06.06"));
    CHECK(f.last == packDate("2026.10.21"));
    CHECK(f.speeds[static_cast<std::size_t>(Speed::Rapid)] == 1);
    CHECK(f.speeds[static_cast<std::size_t>(Speed::Classical)] == 17);
    // Against stronger and weaker players, and the user, counted from the games' Elo and names.
    uint32_t s_games = 0, s_won = 0, w_games = 0, w_won = 0, u_games = 0, u_won = 0, u_lost = 0;
    for (const auto& g : games) {
        const bool w = hers(g, true), b = hers(g, false);
        if (!w && !b) continue;
        const int own = w ? g.white_elo : g.black_elo, opp = w ? g.black_elo : g.white_elo;
        const auto p = g.result == '*' ? -1.0 : pointsOf(g, w);
        if (own > 0 && opp > own) {
            ++s_games;
            s_won += p == 1;
        }
        if (own > 0 && opp > 0 && opp < own) {
            ++w_games;
            w_won += p == 1;
        }
        if (userName(w ? g.black : g.white)) {
            ++u_games;
            u_won += p == 1;
            u_lost += p == 0;
        }
    }
    CHECK(f.stronger.games == s_games);
    CHECK(f.stronger.won == s_won);
    CHECK(f.weaker.games == w_games);
    CHECK(f.weaker.won == w_won);
    CHECK(f.user.games == u_games);
    CHECK(f.user.won == u_won);
    CHECK(f.user.lost == u_lost);
    REQUIRE(f.last_user);
    CHECK(dateText(dossier.games[*f.last_user].numbers.date) == "2025.02.11");

    // Her lines, by colour and ECO code: games, won, drawn, lost, the latest.
    std::size_t listed = 0;
    for (const auto* side : {&f.lines_white, &f.lines_black}) {
        for (const auto& line : *side) {
            const bool w  = side == &f.lines_white;
            const auto it = lines.find({w, line.eco});
            REQUIRE_MESSAGE(it != lines.end(), line.eco);
            const auto& [n, won, drawn, lost, last] = it->second;
            CHECK_MESSAGE(line.score.games == n, line.eco);
            CHECK(line.score.won == won);
            CHECK(line.score.drawn == drawn);
            CHECK(line.score.lost == lost);
            CHECK(line.last == packDate(last));
            CHECK(line.games.size() == n);
            REQUIRE(line.opening);
            CHECK(names[*line.opening].eco == line.eco);
            ++listed;
        }
        for (std::size_t i = 1; i < side->size(); ++i) CHECK((*side)[i - 1].score.games >= (*side)[i].score.games);
    }
    CHECK(listed == lines.size());
    CHECK(f.lines_white.front().eco == "C65");  // four Berlins
    // B90's shortest line names her English Attack games' line: the Najdorf.
    const auto b90 =
        std::find_if(f.lines_black.begin(), f.lines_black.end(), [](const auto& l) { return l.eco == "B90"; });
    REQUIRE(b90 != f.lines_black.end());
    CHECK(names[*b90->opening].moves.size() == 10);

    // Her rating, year by year: the mean of her classical games' Elo.
    REQUIRE(f.years.size() == years.size());
    for (const auto& y : f.years) {
        const auto& [sum, n] = years.at(y.year);
        CHECK_MESSAGE(y.elo == static_cast<int>(std::lround(sum / n)), y.year);
        CHECK(y.rated == static_cast<uint32_t>(n));
    }
    CHECK(f.highest == highest);
    CHECK(f.highest_date == packDate(highest_date));

    // Her structures at move 20.
    std::size_t structures = 0;
    for (const auto& s : f.structures) {
        const auto it = shapes.find({s.kind, s.theirs, s.white});
        REQUIRE(it != shapes.end());
        CHECK(s.score.games == it->second.first);
        CHECK(s.games.size() == it->second.first);
        double points = 0;
        for (const auto i : s.games) points += dossier.games[i].points().value_or(0);
        CHECK(points == doctest::Approx(it->second.second));
        ++structures;
    }
    CHECK(structures == shapes.size());

    // Her sources: one database, the years of her games.
    REQUIRE(f.sources.size() == 1);
    CHECK(f.sources[0].games == count);
    CHECK(f.sources[0].first_year == 2021);
    CHECK(f.sources[0].last_year == 2026);

    // The families of her Black games against 1. e4 and 1. d4.
    const auto e4 = codeAt({}, "e4"), d4 = codeAt({}, "d4");
    const auto against_e4 = dossierFamilies(dossier, DossierParts::Board, false, e4, names);
    REQUIRE(against_e4.size() == 1);
    CHECK(against_e4[0].family == "Sicilian Defense: Najdorf Variation");
    CHECK(against_e4[0].score.games == 5);
    const auto against_d4 = dossierFamilies(dossier, DossierParts::Board, false, d4, names);
    REQUIRE(against_d4.size() == 1);
    CHECK(against_d4[0].family == "Nimzo-Indian Defense: Classical Variation");
    CHECK(against_d4[0].score.games == 3);
}

TEST_CASE("dossier: the tree of her games from each side, and the gaps of a repertoire against them") {
    const auto games = duvalGames();
    auto db          = databaseOf("dossier-tree", games);
    db->setPlayers(duvalTable(*db->snapshot()));
    const OpeningNames names(OPENINGS);
    DossierOptions options;
    options.openings = &names;
    DossierProgress progress;
    std::atomic<bool> cancel{false};
    const auto dossier = makeDossier({findSubject("Club", db->snapshot(), DUVAL)}, DUVAL, options, progress, cancel);

    // Apart: the moves her games played from a position, with White or Black.
    const auto apart = [&](const std::vector<std::string>& line, bool white) {
        std::map<std::string, uint32_t> moves;
        for (const auto& g : games) {
            if (!hers(g, white)) continue;
            const auto m = movesOf(g);
            if (m.size() < line.size() || !std::equal(line.begin(), line.end(), m.begin())) continue;
            ++moves[m.size() > line.size() ? m[line.size()] : std::string("end")];
        }
        return moves;
    };
    const auto check = [&](const std::vector<std::string>& line, bool white) {
        const auto expected = apart(line, white);
        const auto answer   = dossier.at(keyAfter(line), white, DossierParts::Board);
        if (expected.empty()) {
            CHECK_FALSE(answer);
            return;
        }
        REQUIRE(answer);
        uint64_t total = 0;
        for (const auto& [san, n] : expected) total += n;
        CHECK(answer->total.games == total);
        CHECK(answer->moves.size() == expected.size());
        for (const auto& [san, n] : expected) {
            if (san == "end") continue;
            const auto code = codeAt(line, san);
            const auto it   = std::find_if(answer->moves.begin(), answer->moves.end(),
                                           [&](const TreeMove& m) { return m.code == code; });
            REQUIRE_MESSAGE(it != answer->moves.end(), san);
            CHECK(it->stats.games == n);
        }
        for (std::size_t i = 1; i < answer->moves.size(); ++i)
            CHECK(answer->moves[i - 1].stats.games >= answer->moves[i].stats.games);
    };
    check({}, true);
    check({}, false);
    check({"e4"}, false);
    check({"e4", "e5", "Nf3", "Nc6"}, true);
    check({"e4", "c5", "Nf3", "d6", "d4", "cxd4", "Nxd4", "Nf6", "Nc3", "a6"}, false);
    check({"d4", "Nf6", "c4", "e6", "Nc3", "Bb4", "Qc2"}, false);
    check({"e4", "e5", "Nf3", "Nc6", "Bc4", "Bc5", "c3", "Nf6"}, false);  // none of hers with Black
    // The start with White: 1. e4 in 8 of her 10 games.
    const auto start = dossier.at(keyAfter({}), true, DossierParts::Board);
    REQUIRE(start);
    REQUIRE(start->moves.size() == 2);
    CHECK(start->moves[0].code == codeAt({}, "e4"));
    CHECK(start->moves[0].stats.games == 8);
    CHECK(start->moves[1].stats.games == 2);
    // Nothing of the online part: there is none.
    CHECK_FALSE(dossier.at(keyAfter({}), true, DossierParts::Online));

    // The user's White repertoire against her Black games: her 1... c5 (5 games) left without an answer; the
    // repertoire's 1. e4 e5 is not hers.
    const auto read = readPgnGame("[Event \"Repertoire\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 *\n");
    REQUIRE(read.tree);
    const Repertoire repertoire(*read.tree, chess::Color::WHITE);
    CoverageOptions coverage_options;
    coverage_options.least_games = 1;
    coverage_options.least_share = 0;
    const auto gaps       = coverage(repertoire, dossier.moves(false, DossierParts::Board), coverage_options).gaps;
    const auto e4_replies = apart({"e4"}, false);
    REQUIRE(gaps.size() == 1);  // 1. d4 is no move of a White repertoire that plays 1. e4
    CHECK(gaps[0].move == "c5");
    CHECK(gaps[0].games == e4_replies.at("c5"));
    CHECK(gaps[0].line == std::vector<std::string>{"e4"});
}

TEST_CASE("dossier: two namesakes told apart by the FIDE ids their games give, and by the choice kept") {
    // Paul Martin, born in 1979 and rated about 2100 (90000004), and another, born in 2006 and rated about 1640
    // (90000005), both written "Martin, Paul"; some games give their FIDE id, some do not.
    std::vector<G> games;
    uint32_t seed  = 500;
    const auto add = [&](const std::string& white, const std::string& black, int white_elo, int black_elo,
                         const std::string& date, const std::string& white_fide, const std::string& black_fide,
                         char result = '1') {
        G g{white, black, NIMZO, result, white_elo, black_elo, date};
        g.seed       = seed++;
        g.white_fide = white_fide;
        g.black_fide = black_fide;
        games.push_back(g);
    };
    for (int i = 0; i < 6; ++i)
        add("Martin, Paul", "Ito, Ken", 2100 + i, 2050, "201" + std::to_string(5 + i % 5) + ".03.01", "90000004", "");
    for (int i = 0; i < 4; ++i)
        add("Haddad, Sami", "Martin, Paul", 1700, 1640 + i, "202" + std::to_string(4 + i % 3) + ".05.01", "",
            "90000005", '0');
    add("Martin, Paul", "Okafor, Ada", 2110, 2000, "2025.08.08", "", "");  // gives no id
    add("Okafor, Ada", "Martin, Paul", 2010, 2105, "2025.09.09", "", "", '=');
    add("Martin, Paul", "Brennan, Liam", 2050, 2100, "1982.01.01", "", "");  // before the younger was born
    auto db = databaseOf("dossier-namesakes", games);

    const DossierSubject elder{"Martin, Paul", 90000004, 1979, ""};
    const DossierSubject younger{"Martin, Paul", 90000005, 2006, ""};
    const DossierOptions options;
    std::atomic<bool> cancel{false};
    const auto count = [&](const DossierSubject& who) {
        DossierProgress progress;
        const auto dossier = makeDossier({findSubject("Club", db->snapshot(), who)}, who, options, progress, cancel);
        return dossier.games.size();
    };
    // No table yet: the games that give the other's id are left out, and those before one could play.
    CHECK(count(elder) == 6 + 2);
    CHECK(count(younger) == 4 + 2);
    CHECK(count(DossierSubject{"Martin, Paul"}) == 13);

    // The user chooses the elder for "Martin, Paul" (the dossier's question): kept in the table of names; the younger's
    // dossier then takes only the games that give his id.
    const auto snap = db->snapshot();
    std::vector<uint32_t> ids;
    for (uint32_t id = 0; id < snap->names(); ++id)
        if (snap->name(id) == "Martin, Paul") ids.push_back(id);
    REQUIRE(ids.size() == 1);
    db->setPlayers(withPlayer(*snap->players(), ids, "Martin, Paul", [](Player& p) {
        p.fide = 90000004;
        p.born = 1979;
    }));
    const auto doubtful = findSubject("Club", db->snapshot(), younger);
    CHECK(doubtful.spellings.empty());
    CHECK(doubtful.doubtful == ids);
    CHECK(count(younger) == 4);
    CHECK(count(elder) == 6 + 2);

    // FIDE's list has three of that name: the one whose rating is the nearest to the games' first, the one born after
    // the first games left out.
    const auto list       = inventedList("dossier-fide");
    const auto near_elder = fideNamesakes(*list, "Martin, Paul", 2015, 2100);
    REQUIRE(near_elder.size() == 3);
    CHECK(list->player(near_elder[0]).id == 90000004);
    const auto near_younger = fideNamesakes(*list, "Martin, Paul", 2024, 1650);
    REQUIRE(near_younger.size() == 3);
    CHECK(list->player(near_younger[0]).id == 90000005);
    const auto older_games = fideNamesakes(*list, "Martin, Paul", 2008, 0);
    CHECK(older_games.size() == 2);
    CHECK(fideNamesakes(*list, "Martin", 0, 0).empty());  // a name of one word says no person
    CHECK(fideNamesakes(*list, "Duval, Anne", 0, 0).size() == 1);
}

TEST_CASE("dossier: the games of her Lichess account, a part of their own; a cancel") {
    const auto games = duvalGames();
    auto db          = databaseOf("dossier-board", games);
    db->setPlayers(duvalTable(*db->snapshot()));
    std::vector<G> online;
    uint32_t seed  = 900;
    const auto add = [&](const std::string& white, const std::string& black, const Prefix& prefix, char result,
                         const std::string& tc) {
        G g{white, black, prefix, result, 2300, 2300, "2026.09.01"};
        g.seed         = seed++;
        g.time_control = tc;
        g.event        = "Rated game";
        online.push_back(g);
    };
    add("duval_a", "rs_partner", BERLIN, '1', "180+2");
    add("duval_a", "rs_partner", ITALIAN, '1', "180+2");
    add("DUVAL_A", "Tester_Sam", BERLIN, '0', "600+0");
    add("rs_partner", "duval_a", NAJDORF_BE3, '=', "60+0");
    add("Tester_Sam", "duval_a", NIMZO, '0', "-");
    add("rs_partner", "someone_else", NIMZO, '0', "180+2");
    auto lichess = databaseOf("dossier-online", online);

    auto subject    = DUVAL;
    subject.lichess = "Duval_A";
    const auto web  = findSubject("Lichess · duval_a", lichess->snapshot(), subject, true);
    CHECK(web.spellings.size() == 2);  // two ways of writing the account's letters
    const OpeningNames names(OPENINGS);
    DossierOptions options;
    options.openings = &names;
    options.user     = userName;
    DossierProgress progress;
    std::atomic<bool> cancel{false};
    const auto dossier =
        makeDossier({findSubject("Club", db->snapshot(), subject), web}, subject, options, progress, cancel);
    CHECK(dossier.games.size() == 18 + 5);

    const auto board = dossierFigures(dossier, DossierParts::Board);
    const auto web_f = dossierFigures(dossier, DossierParts::Online);
    const auto both  = dossierFigures(dossier, DossierParts::Both);
    CHECK(board.games == 18);
    CHECK(web_f.games == 5);
    CHECK(both.games == 23);
    CHECK(web_f.speeds[static_cast<std::size_t>(Speed::Blitz)] == 2);
    CHECK(web_f.speeds[static_cast<std::size_t>(Speed::Rapid)] == 1);
    CHECK(web_f.speeds[static_cast<std::size_t>(Speed::Bullet)] == 1);
    CHECK(web_f.speeds[static_cast<std::size_t>(Speed::Correspondence)] == 1);
    CHECK(web_f.user.games == 2);  // with the user's account
    CHECK(web_f.user.won == 1);
    CHECK(board.user.games == 2);
    REQUIRE(web_f.sources.size() == 2);
    CHECK(web_f.sources[1].games == 5);
    CHECK(web_f.sources[0].games == 0);
    // With White from the start: online, three of her games; over the board, ten.
    const auto start = keyAfter({});
    CHECK(dossier.at(start, true, DossierParts::Online)->total.games == 3);
    CHECK(dossier.at(start, true, DossierParts::Board)->total.games == 10);
    CHECK(dossier.at(start, true, DossierParts::Both)->total.games == 13);

    // A cancel stops it: nothing given.
    cancel = true;
    const auto stopped =
        makeDossier({findSubject("Club", db->snapshot(), subject)}, subject, options, progress, cancel);
    CHECK(stopped.cancelled);
    CHECK(stopped.games.empty());
}

TEST_CASE("dossier: the table of names keeps the Lichess account linked to a player, and reads its first version") {
    std::vector<Player> players(1);
    players[0].name      = "Duval, Anne";
    players[0].fide      = 90000011;
    players[0].spellings = {3, 7};
    PlayerTable table(players);
    // Linked from her dossier: the player of her spellings gets the account, a spelling of another player moves to her.
    players.emplace_back();
    players[1].name      = "Duval, A.";
    players[1].spellings = {9};
    table                = PlayerTable(players);
    const auto linked    = withPlayer(table, {7, 9}, "Duval, Anne", [](Player& p) { p.lichess = "duval_a"; });
    REQUIRE(linked.players().size() == 1);  // the other one, left with nothing, goes
    CHECK(linked.players()[0].lichess == "duval_a");
    CHECK(linked.players()[0].spellings == std::vector<uint32_t>{3, 7, 9});
    CHECK(linked.players()[0].fide == 90000011);
    const auto again = PlayerTable::fromBytes(linked.bytes());
    REQUIRE(again.players().size() == 1);
    CHECK(again.players()[0].lichess == "duval_a");
    CHECK(again.players()[0].name == "Duval, Anne");
    // A new player for spellings in none.
    const auto fresh = withPlayer(PlayerTable{}, {4}, "Kaya, Emre", [](Player& p) { p.lichess = "emre_k"; });
    REQUIRE(fresh.players().size() == 1);
    CHECK(fresh.players()[0].name == "Kaya, Emre");
    CHECK(fresh.players()[0].how == "hand");

    // The first version, as #88 wrote it: no account.
    std::string v1  = "RSPLAYR1";
    const auto put  = [&](auto v) { v1.append(reinterpret_cast<const char*>(&v), sizeof v); };
    const auto text = [&](const std::string& s) {
        put(static_cast<uint16_t>(s.size()));
        v1 += s;
    };
    put(uint32_t{1});
    put(uint32_t{1});  // one player
    put(uint64_t{0});  // no pair kept apart
    put(int64_t{90000011});
    for (const uint16_t v : {uint16_t{1994}, uint16_t{2412}, uint16_t{0}, uint16_t{0}}) put(v);
    put(uint8_t{2});  // "list"
    text("Duval, Anne");
    text("FRA");
    text("WIM");
    text("2026-11");
    put(uint32_t{2});
    put(uint32_t{3});
    put(uint32_t{7});
    const auto old = PlayerTable::fromBytes(v1);
    REQUIRE(old.players().size() == 1);
    CHECK(old.players()[0].name == "Duval, Anne");
    CHECK(old.players()[0].lichess.empty());
    CHECK(old.players()[0].how == "list");
    CHECK(old.players()[0].spellings == std::vector<uint32_t>{3, 7});
}
