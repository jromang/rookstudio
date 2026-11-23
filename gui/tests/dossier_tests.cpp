// A player's dossier (#90) behind its page: #52's report on an opponent (their games online, those against the user,
// what they play, the repertoires' gaps) now made from their Lichess account's database; the assistant's structures
// counted apart; the user's reviews of their games; a namesake of FIDE's list chosen, kept in the table of names, and
// undone; a Lichess account linked; Ctrl+K's names. Players' names are invented, and so is the FIDE list.
#include <doctest/doctest.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <commentary/formation.hpp>
#include <study/fide_list.hpp>
#include <study/import.hpp>
#include <study/store.hpp>

#include "board_text.hpp"
#include "dossier.hpp"
#include "game_analysis.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

std::shared_ptr<study::Database> databaseOf(const std::string& name, const std::vector<std::string>& pgns) {
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    auto db = study::Database::create(dir);
    std::vector<study::ImportedGame> games;
    for (const auto& text : pgns) {
        study::ImportedGame g;
        REQUIRE(study::readImportedGame(text, g));
        games.push_back(std::move(g));
    }
    if (!games.empty()) db->append(std::move(games), true);
    return db;
}

void add(std::vector<std::string>& pgns, int times, const std::string& white, const std::string& black,
         const std::string& moves, const std::string& result = "1/2-1/2", const std::string& date = "2026.03.01") {
    for (int i = 0; i < times; ++i)
        pgns.push_back(fmt::format("[White \"{}\"]\n[Black \"{}\"]\n[Date \"{}\"]\n[Result \"{}\"]\n\n{} {}\n", white,
                                   black, date, result, moves, result));
}

study::Repertoire repertoireOf(const char* pgn, chess::Color side) {
    std::string error;
    const auto game = readPgnGame(pgn, error);
    REQUIRE(game);
    return study::Repertoire(GameTree{*game, {}}, side);
}

DossierReport made(DossierAsk ask) {
    study::DossierProgress progress;
    std::atomic<bool> cancel{false};
    return makeDossierReport(std::move(ask), progress, cancel);
}

// A game's moves: its opening, then moves of the pieces that take nothing (its pawns stay as they are) up to `plies`.
std::vector<std::string> quietGame(const std::string& opening, int plies, uint32_t seed) {
    std::vector<std::string> moves;
    chess::Board board;
    for (std::size_t at = 0; at < opening.size();) {
        auto end = opening.find(' ', at);
        if (end == std::string::npos) end = opening.size();
        const auto san = opening.substr(at, end - at);
        at             = end + 1;
        board.makeMove(chess::uci::parseSan(board, san));
        moves.push_back(san);
    }
    std::mt19937 random(seed);
    while (static_cast<int>(moves.size()) < plies) {
        chess::Movelist list;
        chess::movegen::legalmoves(list, board);
        std::vector<chess::Move> quiet;
        for (const auto& m : list)
            if (board.at(m.from()).type() != chess::PieceType::PAWN && !board.isCapture(m) &&
                m.typeOf() != chess::Move::CASTLING)
                quiet.push_back(m);
        if (quiet.empty()) break;
        std::sort(quiet.begin(), quiet.end(), [](const chess::Move& a, const chess::Move& b) {
            return chess::uci::moveToUci(a) < chess::uci::moveToUci(b);
        });
        const auto move = quiet[random() % quiet.size()];
        moves.push_back(chess::uci::moveToSan(board, move));
        board.makeMove(move);
    }
    return moves;
}

std::string pgnOf(const std::string& white, const std::string& black, const std::vector<std::string>& moves,
                  const std::string& result, const std::string& date, int white_elo = 2200, int black_elo = 2200) {
    std::string text = fmt::format(
        "[Event \"Club championship\"]\n[Date \"{}\"]\n[White \"{}\"]\n[Black \"{}\"]\n[WhiteElo \"{}\"]\n"
        "[BlackElo \"{}\"]\n[Result \"{}\"]\n\n",
        date, white, black, white_elo, black_elo, result);
    for (std::size_t i = 0; i < moves.size(); ++i)
        text += (i % 2 == 0 ? std::to_string(i / 2 + 1) + ". " : std::string()) + moves[i] + " ";
    return text + result + "\n";
}

std::string pad(std::string s, std::size_t width) {
    s.resize(std::max(s.size(), width), ' ');
    return s;
}

std::shared_ptr<const study::FideList> inventedList(const std::string& name) {
    constexpr std::size_t widths[] = {15, 61, 4, 4, 5, 5, 15, 4, 6, 4, 3, 6, 4, 3, 6, 4, 3, 6, 4};
    const auto row                 = [&](const std::vector<std::string>& values) {
        std::string out;
        for (std::size_t c = 0; c < values.size(); ++c) out += pad(values[c], widths[c]);
        return out + "\r\n";
    };
    const auto player = [&](const std::string& id, const std::string& who, const std::string& fed, int standard,
                            const std::string& born) {
        return row({id, who, fed, "M", "", "", "", "", std::to_string(standard), "0", "20", "0", "0", "20", "0", "0",
                    "20", born, ""});
    };
    std::string text = row({"ID Number", "Name", "Fed", "Sex", "Tit", "WTit", "OTit", "FOA", "SRtng", "SGm", "SK",
                            "RRtng", "RGm", "Rk", "BRtng", "BGm", "BK", "B-day", "Flag"});
    text += player("90000004", "Martin, Paul", "FRA", 2105, "1979");
    text += player("90000005", "Martin, Paul", "FRA", 1642, "2006");
    text += player("90000006", "Martin, Paul", "CAN", 1980, "1995");
    const auto source = fs::temp_directory_path() / (name + ".txt");
    {
        std::ofstream out(source, std::ios::binary);
        out << text;
    }
    const auto file = fs::temp_directory_path() / name / "list.bin";
    study::FidePrepareProgress progress;
    std::atomic<bool> cancel{false};
    study::prepareFideList(source, file, "2026-11", progress, cancel);
    return std::make_shared<const study::FideList>(file);
}

}  // namespace

TEST_CASE("dossier: #52's report from their Lichess games, their player of the databases found by the account") {
    std::vector<std::string> online;
    add(online, 6, "Someone", "Opponent_A", "1. e4 c5");
    add(online, 2, "Someone", "Opponent_A", "1. e4 e5");
    add(online, 3, "Another", "Opponent_A", "1. d4 Nf6");
    add(online, 5, "Opponent_A", "Third", "1. e4 e6", "1/2-1/2", "2025.11.02");
    add(online, 2, "Opponent_A", "Third", "1. d4 d5");
    add(online, 2, "rs_me", "Opponent_A", "1. e4 c5", "1-0", "2026.10.12");  // the user won both
    add(online, 1, "Opponent_A", "rs_me", "1. e4 e6");                       // a draw
    add(online, 1, "Opponent_A", "rs_me", "1. d4 d5", "1-0");                // a loss
    add(online, 4, "Fourth", "Fifth", "1. e4 e5");                           // not theirs
    auto lichess = databaseOf("fcg-dossier-lichess", online);
    // Over the board, a player of the club linked to that account.
    std::vector<std::string> club;
    add(club, 3, "Moreau, Claire", "Ito, Ken", "1. e4 e5 2. Nf3 Nc6 3. Bb5", "1-0", "2025.05.05");
    add(club, 2, "Ito, Ken", "Moreau, Claire", "1. d4 Nf6 2. c4 e6", "0-1", "2025.06.06");
    auto board = databaseOf("fcg-dossier-club", club);
    {
        const auto snap = board->snapshot();
        std::vector<uint32_t> ids;
        for (uint32_t id = 0; id < snap->names(); ++id)
            if (snap->name(id) == "Moreau, Claire") ids.push_back(id);
        board->setPlayers(study::withPlayer(*snap->players(), ids, "Moreau, Claire",
                                            [](study::Player& p) { p.lichess = "opponent_a"; }));
    }

    DossierAsk ask;
    ask.subject.name    = "Opponent_A";
    ask.subject.lichess = "opponent_a";
    ask.bases.push_back({"Club", board->folder(), board});
    ask.online = DossierBase{"Lichess · opponent_a", lichess->folder(), lichess};
    ask.repertoires.emplace_back("White", repertoireOf("1. e4 e5 2. Nf3 *", chess::Color::WHITE));
    ask.repertoires.emplace_back("Black", repertoireOf("1. e4 c5 *", chess::Color::BLACK));
    ask.user_names = {"RS_ME"};
    const auto r   = made(ask);
    REQUIRE_FALSE(r.cancelled);
    REQUIRE(r.dossier);

    // Their player of the databases, found by the account linked to them.
    CHECK(r.subject.name == "Moreau, Claire");
    CHECK(r.board.games == 5);
    CHECK(r.board.white.won == 3);
    CHECK(r.board.black.won == 2);

    // Online: their games and years, those against the user (the user won two, drew one, lost one).
    const auto& o = r.online;
    CHECK(o.games == 22);
    CHECK(o.first == study::packDate("2025.11.02"));
    CHECK(o.last == study::packDate("2026.10.12"));
    CHECK(o.user.games == 4);
    CHECK(o.user.won == 1);
    CHECK(o.user.drawn == 1);
    CHECK(o.user.lost == 2);
    // With White: 1. e4 in 6 of their 9 games, then 1. d4.
    REQUIRE(r.online_play.with_white.size() == 2);
    CHECK(r.online_play.with_white[0].san == "e4");
    CHECK(r.online_play.with_white[0].games == 6);
    CHECK(r.online_play.with_white[0].share == doctest::Approx(6.0 / 9));
    CHECK(r.online_play.with_white[1].san == "d4");
    // With Black: against 1. e4, the Sicilian in 8 of 10; against 1. d4, the Indian defences.
    REQUIRE(r.online_play.with_black.size() == 2);
    CHECK(r.online_play.with_black[0].san == "e4");
    CHECK(r.online_play.with_black[0].games == 10);
    REQUIRE_FALSE(r.online_play.with_black[0].families.empty());
    CHECK(r.online_play.with_black[0].families[0].family == "Sicilian Defense");
    CHECK(r.online_play.with_black[0].families[0].score.games == 8);
    CHECK(r.online_play.with_black[1].san == "d4");
    REQUIRE_FALSE(r.online_play.with_black[1].families.empty());
    CHECK(r.online_play.with_black[1].families[0].family == "Indian Defense");
    // The gaps over both sources: 1... c5 against the White repertoire (8 games), 1. d4 against the Black one (3).
    REQUIRE(r.gaps_both.size() >= 2);
    CHECK(r.gaps_both[0].repertoire == "White");
    CHECK(r.gaps_both[0].gap.move == "c5");
    CHECK(r.gaps_both[0].gap.games == 8);
    CHECK(r.gaps_both[1].repertoire == "Black");
    CHECK(r.gaps_both[1].gap.move == "d4");
    CHECK(r.gaps_both[1].gap.games == 3);
    // Over the board alone: her 1. d4 games are Black's.
    CHECK(r.gaps.empty());

    // Without the Lichess source: nothing online, the database of the account left out.
    ask.online.reset();
    const auto alone = made(ask);
    CHECK(alone.online.games == 0);
    CHECK(alone.board.games == 5);
    CHECK(alone.gaps_both.empty());

    // Someone else: nothing of theirs.
    DossierAsk nobody;
    nobody.subject.name = "Nobody";
    nobody.bases.push_back({"Club", board->folder(), board});
    const auto none = made(nobody);
    CHECK(none.board.games == 0);
    CHECK(none.board_play.with_white.empty());
}

TEST_CASE("dossier: the assistant's structures at move 20, counted apart; their games the user reviewed") {
    // Rui Ferreira's games: the Queen's Gambit's exchange (its pawns kept to move 20), the Berlin, the Italian.
    struct Played {
        std::string white, black, opening, result;
        int plies;
    };
    const std::vector<Played> played{
        {"Ferreira, Rui", "Ito, Ken", "d4 d5 c4 e6 Nc3 Nf6 cxd5 exd5", "1-0", 50},
        {"Ferreira, Rui", "Haddad, Sami", "d4 d5 c4 e6 Nc3 Nf6 cxd5 exd5", "1/2-1/2", 46},
        {"Ferreira, Rui", "Okafor, Ada", "d4 d5 c4 e6 Nc3 Nf6 cxd5 exd5", "0-1", 44},
        {"Brennan, Liam", "Ferreira, Rui", "d4 d5 c4 e6 Nc3 Nf6 cxd5 exd5", "0-1", 48},
        {"Lindqvist, Erik", "Ferreira, Rui", "d4 d5 c4 e6 Nc3 Nf6 cxd5 exd5", "1-0", 42},
        {"Ferreira, Rui", "Ito, Ken", "e4 e5 Nf3 Nc6 Bb5 Nf6", "1/2-1/2", 60},
        {"Ito, Ken", "Ferreira, Rui", "e4 e5 Nf3 Nc6 Bc4 Bc5 c3 Nf6 d4 exd4 cxd4 Bb4", "1-0", 52},
        {"Ferreira, Rui", "Haddad, Sami", "e4 c5 Nf3 d6", "1-0", 30},  // ends before move 20
    };
    std::vector<std::string> pgns;
    std::vector<std::vector<std::string>> moves;
    for (std::size_t i = 0; i < played.size(); ++i) {
        moves.push_back(quietGame(played[i].opening, played[i].plies, static_cast<uint32_t>(7 + i)));
        pgns.push_back(
            pgnOf(played[i].white, played[i].black, moves.back(), played[i].result, fmt::format("2024.0{}.10", 1 + i)));
    }
    auto db = databaseOf("fcg-dossier-structures", pgns);

    // Apart: each game replayed to its 40th ply, the assistant's formations read there.
    std::map<std::tuple<int, bool, bool>, std::pair<uint32_t, double>> apart;
    for (std::size_t i = 0; i < played.size(); ++i) {
        if (moves[i].size() < 40) continue;
        chess::Board b;
        for (std::size_t k = 0; k < 40; ++k) b.makeMove(chess::uci::parseSan(b, moves[i][k]));
        auto board = commentary::Board::empty();
        for (int sq = 0; sq < 64; ++sq) {
            const auto piece = b.at(chess::Square(sq));
            if (piece.type() != chess::PieceType::PAWN) continue;
            board.setPieceAt(
                commentary::square(static_cast<uint32_t>(sq)),
                {piece.color() == chess::Color::WHITE ? commentary::Color::White : commentary::Color::Black,
                 commentary::Role::Pawn});
        }
        const bool white = played[i].white == "Ferreira, Rui";
        const auto& res  = played[i].result;
        const double pts = res == "1/2-1/2" ? 0.5 : (res == "1-0") == white ? 1.0 : 0.0;
        std::set<std::tuple<int, bool, bool>> once;
        for (const auto& [formation, owner] : commentary::formations(board)) {
            const auto key =
                std::tuple{static_cast<int>(formation), (owner == commentary::Color::White) == white, white};
            if (!once.insert(key).second) continue;
            ++apart[key].first;
            apart[key].second += pts;
        }
    }
    REQUIRE_FALSE(apart.empty());
    CHECK(apart.count({static_cast<int>(commentary::Formation::Carlsbad), true, true}) == 1);  // his, with White

    // One of his games reviewed by the user: a blunder of his on his fifth move.
    const auto analyses = fs::temp_directory_path() / "fcg-dossier-analyses";
    fs::remove_all(analyses);
    {
        const auto snap = db->snapshot();
        std::string error;
        const auto game = readPgnGame(study::importedPgn(snap->game(0)), error);
        REQUIRE(game);
        GameReview review;
        review.engine = "Test engine";
        review.nodes  = 20000;
        for (std::size_t p = 0; p <= game->moves.size(); ++p) {
            PositionEval e;
            // The side to move's score: even, but after White's ninth ply Black is winning.
            e.score = fastchess::Score{fastchess::ScoreType::CP, p == 9 ? 600 : p > 9 ? (p % 2 ? 600 : -600) : 0};
            e.best  = "0000";
            review.positions.push_back(e);
        }
        REQUIRE(saveReview(analyses, *game, review));
    }

    DossierAsk ask;
    ask.subject.name = "Ferreira, Rui";
    ask.bases.push_back({"Club", db->folder(), db});
    ask.analyses = analyses;
    const auto r = made(ask);
    REQUIRE(r.dossier);
    CHECK(r.board.games == played.size());
    std::size_t listed = 0;
    for (const auto& s : r.board.structures) {
        const auto it = apart.find({s.kind, s.theirs, s.white});
        REQUIRE(it != apart.end());
        CHECK(s.score.games == it->second.first);
        double points = 0;
        for (const auto i : s.games) points += r.dossier->games[i].points().value_or(0);
        CHECK(points == doctest::Approx(it->second.second));
        ++listed;
    }
    CHECK(listed == apart.size());

    // The review: one game of his, his blunder in the opening.
    CHECK(r.reviews.games == 1);
    CHECK(r.reviews.mistakes[0] + r.reviews.mistakes[1] + r.reviews.mistakes[2] == 1);
    CHECK(r.reviews.accuracy[0].has_value());
    // No review of a game of his: nothing said.
    ask.analyses = fs::temp_directory_path() / "fcg-dossier-no-analyses";
    CHECK(made(ask).reviews.games == 0);
}

TEST_CASE("dossier: a namesake of FIDE's list chosen and kept in the table of names, then undone; an account linked") {
    std::vector<std::string> pgns;
    add(pgns, 4, "Martin, Paul", "Ito, Ken", "1. d4 d5 2. c4", "1-0", "2018.05.05");
    for (auto& p : pgns) p.insert(p.find("[Result"), "[WhiteElo \"2110\"]\n");
    add(pgns, 2, "Okafor, Ada", "Martin, Paul", "1. e4 c5", "0-1", "2020.04.04");
    auto db = databaseOf("fcg-dossier-namesakes", pgns);

    DossierAsk ask;
    ask.subject.name = "Martin, Paul";
    ask.bases.push_back({"Club", db->folder(), db});
    ask.fide_list    = inventedList("fcg-dossier-fide");
    const auto first = made(ask);
    CHECK(first.board.games == 6);
    // Three players of that name on the list: the one rated nearest his games first (2110: the elder).
    REQUIRE(first.namesakes.size() == 3);
    CHECK(first.namesakes[0].fide == 90000004);
    CHECK_FALSE(first.identity);

    // Enter keeps the choice: his table's player, his identity, the table before kept for Ctrl+Z.
    ask.keep_namesake = first.namesakes[0];
    const auto kept   = made(ask);
    CHECK(kept.namesakes.empty());
    CHECK(kept.subject.fide == 90000004);
    REQUIRE(kept.identity);
    CHECK(kept.identity->born == 1979);
    CHECK(kept.board.games == 6);
    REQUIRE(kept.tables_before.size() == 1);
    REQUIRE(db->snapshot()->players()->players().size() == 1);
    CHECK(db->snapshot()->players()->players()[0].fide == 90000004);
    // Opened again later, by his name: his FIDE id from the table.
    DossierAsk again;
    again.subject.name = "Martin, Paul";
    again.bases.push_back({"Club", db->folder(), db});
    again.fide_list = ask.fide_list;
    CHECK(made(again).subject.fide == 90000004);

    // Ctrl+Z: the table as it was.
    DossierAsk undo = again;
    undo.restore    = kept.tables_before;
    const auto back = made(undo);
    CHECK(db->snapshot()->players()->players().empty());
    CHECK(back.namesakes.size() == 3);

    // Their Lichess account linked from the dossier: kept with their player.
    DossierAsk link    = again;
    link.subject.fide  = 0;
    link.keep_namesake = first.namesakes[0];
    link.link_lichess  = "paul_m";
    const auto linked  = made(link);
    CHECK(linked.subject.lichess == "paul_m");
    REQUIRE(db->snapshot()->players()->players().size() == 1);
    CHECK(db->snapshot()->players()->players()[0].lichess == "paul_m");
    CHECK(made(again).subject.lichess == "paul_m");
}

TEST_CASE("dossier: Ctrl+K's names, the tables' players first") {
    std::vector<std::string> pgns;
    add(pgns, 2, "Duval, Anne", "Duval,A", "1. e4 e5");
    add(pgns, 1, "Duvalier, Jean", "Ito, Ken", "1. d4 d5");
    add(pgns, 1, "Anne Duval", "?", "1. c4 e5");
    auto db = databaseOf("fcg-dossier-names", pgns);
    {
        const auto snap = db->snapshot();
        std::vector<uint32_t> ids;
        for (uint32_t id = 0; id < snap->names(); ++id)
            if (snap->name(id) == "Duval,A" || snap->name(id) == "Anne Duval") ids.push_back(id);
        db->setPlayers(study::withPlayer(*snap->players(), ids, "Duval, Anne", nullptr));
    }
    std::atomic<bool> cancel{false};
    const auto found = dossierNames({db->snapshot()}, "duval", 8, cancel);
    REQUIRE(found.size() == 2);  // her three spellings shown as one name, and Jean Duvalier
    CHECK(found[0].name == "Duval, Anne");
    CHECK(found[1].name == "Duvalier, Jean");
    CHECK(dossierNames({db->snapshot()}, "anne duv", 8, cancel).size() == 1);
    CHECK(dossierNames({db->snapshot()}, "nobody", 8, cancel).empty());
}
