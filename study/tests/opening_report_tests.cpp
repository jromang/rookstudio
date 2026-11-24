// The opening report of a position (#91, mockup M13, board 4): its moves, main lines, structures, plans, key games and
// novelties compared with a count made apart over games of our own, replayed on chess.hpp's boards (neither the tree
// nor the scanning board). The games follow the exchange variation of the Queen's Gambit Declined to 10…Nf8, then plans
// of each side among random quiet moves; players' names are invented.
#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <chess.hpp>
#include <study/opening_report.hpp>
#include <study/scan_board.hpp>
#include <study/store.hpp>
#include <study/tree.hpp>

#include "test_games.hpp"

using namespace fcg::study;
using namespace fcg::test;
namespace fs = std::filesystem;

namespace {

// The report's position, after 10…Nf8, by the usual order and by another.
const char* const MAIN_ORDER  = "d4 d5 c4 e6 Nc3 Nf6 cxd5 exd5 Bg5 c6 e3 Be7 Bd3 Nbd7 Qc2 O-O Nf3 Re8 O-O Nf8";
const char* const OTHER_ORDER = "d4 Nf6 c4 e6 Nc3 d5 cxd5 exd5 Bg5 c6 e3 Be7 Bd3 Nbd7 Qc2 O-O Nf3 Re8 O-O Nf8";
// The same position four plies later: each side's knight or rook goes and comes back first.
const char* const TEMPO_ORDER =
    "d4 d5 c4 e6 Nc3 Nf6 cxd5 exd5 Bg5 c6 e3 Be7 Bd3 Nbd7 Qc2 O-O Nf3 Re8 Nh4 Rf8 Nf3 Re8 O-O Nf8";

std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> out;
    for (std::size_t at = 0; at < text.size();) {
        auto end = text.find(' ', at);
        if (end == std::string::npos) end = text.size();
        if (end > at) out.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    return out;
}

// A game of the tests: its first moves, then each side's plan (moves in SAN, each played when it is legal and the
// generator wants it), random quiet moves between, up to `plies`.
struct G {
    std::string prefix = MAIN_ORDER;
    std::vector<std::string> white_plan, black_plan;
    std::vector<std::string> then;  // played at once after the prefix, before the plans
    char result   = '1';
    int white_elo = 0, black_elo = 0;
    std::string date  = "2015.05.10";
    std::string white = "Ansel, Pia", black = "Brandt, Ole";
    int plies     = 64;
    uint32_t seed = 1;
};

std::vector<std::string> sanOf(const G& g) {
    std::vector<std::string> moves;
    chess::Board board;
    for (const auto& san : words(g.prefix)) {
        board.makeMove(chess::uci::parseSan(board, san));
        moves.push_back(san);
    }
    for (const auto& san : g.then) {
        board.makeMove(chess::uci::parseSan(board, san));
        moves.push_back(san);
    }
    std::mt19937 random(g.seed);
    std::size_t wi = 0, bi = 0;
    std::array<int, 2> waited{};
    while (static_cast<int>(moves.size()) < g.plies) {
        chess::Movelist list;
        chess::movegen::legalmoves(list, board);
        if (list.empty()) break;
        const bool white = board.sideToMove() == chess::Color::WHITE;
        auto& at         = white ? wi : bi;
        const auto& plan = white ? g.white_plan : g.black_plan;
        auto& wait       = waited[white ? 0 : 1];
        std::optional<chess::Move> chosen;
        if (at < plan.size() && random() % 4 != 0) {
            chess::Move planned = chess::Move::NO_MOVE;
            try {
                planned = chess::uci::parseSan(board, plan[at]);
            } catch (...) {
            }
            if (planned != chess::Move::NO_MOVE && std::find(list.begin(), list.end(), planned) != list.end()) {
                chosen = planned;
                ++at;
                wait = 0;
            } else if (++wait > 6) {  // never legal: the next one
                ++at;
                wait = 0;
            }
        }
        if (!chosen) {
            // A quiet move: no capture, no castling, by a generator of the game's own.
            std::vector<chess::Move> quiet;
            for (const auto& m : list)
                if (!board.isCapture(m) && m.typeOf() != chess::Move::CASTLING) quiet.push_back(m);
            if (quiet.empty()) break;
            std::sort(quiet.begin(), quiet.end(), [](const chess::Move& a, const chess::Move& b) {
                return chess::uci::moveToUci(a) < chess::uci::moveToUci(b);
            });
            chosen = quiet[random() % quiet.size()];
        }
        moves.push_back(chess::uci::moveToSan(board, *chosen));
        board.makeMove(*chosen);
    }
    return moves;
}

ImportedGame gameOf(const G& g, const std::vector<std::string>& moves) {
    const std::string result = g.result == '1' ? "1-0" : g.result == '0' ? "0-1" : g.result == '=' ? "1/2-1/2" : "*";
    std::string text = "[Event \"Report Open\"]\n[Date \"" + g.date + "\"]\n[White \"" + g.white + "\"]\n[Black \"" +
                       g.black + "\"]\n";
    if (g.white_elo) text += "[WhiteElo \"" + std::to_string(g.white_elo) + "\"]\n";
    if (g.black_elo) text += "[BlackElo \"" + std::to_string(g.black_elo) + "\"]\n";
    text += "[Result \"" + result + "\"]\n\n";
    for (std::size_t i = 0; i < moves.size(); ++i)
        text += (i % 2 == 0 ? std::to_string(i / 2 + 1) + ". " : std::string()) + moves[i] + " ";
    text += result + "\n";
    ImportedGame game;
    REQUIRE(readImportedGame(text, game));
    REQUIRE_FALSE(game.damaged);
    return game;
}

// The games of the first test: the plans of each side in their shares, results, ratings and dates of their own; a fifth
// by the other order; a few that leave the line before the position.
std::vector<G> planGames(std::size_t count) {
    std::vector<G> out;
    const std::vector<std::string> minority{"Rab1", "b4", "b5"};
    const std::vector<std::string> centre{"Rae1", "Nd2", "f3", "e4"};
    const std::vector<std::string> knights{"Ng6", "Ne4"};
    const std::vector<std::string> bishop{"Bf5", "Ne6"};
    const char results[] = {'1', '=', '0', '=', '1', '*'};
    for (std::size_t i = 0; i < count; ++i) {
        G g;
        g.seed = static_cast<uint32_t>(1000 + i);
        if (i % 5 == 4) g.prefix = OTHER_ORDER;
        if (i % 20 < 9)
            g.white_plan = minority;
        else if (i % 20 < 14)
            g.white_plan = centre;
        if (i % 10 < 5)
            g.black_plan = knights;
        else if (i % 10 < 8)
            g.black_plan = bishop;
        g.result    = results[i % 6];
        g.white_elo = i % 7 == 0 ? 0 : 2200 + static_cast<int>((i * 37) % 450);
        g.black_elo = i % 11 == 0 ? 0 : 2180 + static_cast<int>((i * 53) % 470);
        g.date      = std::to_string(2008 + i % 15) + ".0" + std::to_string(1 + i % 9) + ".1" + std::to_string(i % 10);
        g.plies     = 56 + static_cast<int>(i % 12);
        out.push_back(std::move(g));
    }
    // Games that leave the line before the position.
    for (std::size_t i = 0; i < 12; ++i) {
        G g;
        g.prefix = "d4 d5 c4 e6 Nc3 Nf6 cxd5 exd5 Bg5 c6 e3 Bf5";
        g.seed   = static_cast<uint32_t>(9000 + i);
        g.plies  = 40;
        out.push_back(std::move(g));
    }
    return out;
}

std::shared_ptr<Database> databaseOf(const std::string& name, const std::vector<G>& specs,
                                     std::vector<std::vector<std::string>>& lines) {
    std::vector<ImportedGame> games;
    for (const auto& g : specs) {
        lines.push_back(sanOf(g));
        games.push_back(gameOf(g, lines.back()));
    }
    return database(name, std::move(games));
}

std::shared_ptr<const Tree> treeOf(const Database& db, uint32_t plies) {
    TreeOptions options;
    options.plies   = plies;
    options.threads = 2;
    TreeProgress progress;
    std::atomic<bool> cancel{false};
    const auto built = buildTree(db, *db.snapshot(), options, progress, cancel);
    REQUIRE_MESSAGE(built.error.empty(), built.error);
    auto tree = Tree::open(db.folder());
    REQUIRE(tree);
    return tree;
}

// A position as the count apart tells positions apart: its men, side to move and castling, and the en passant square
// only when a capture there is legal.
std::string keyOf(const chess::Board& board) {
    auto fen          = board.getFen();
    const auto fields = words(fen);
    std::string key   = fields[0] + " " + fields[1] + " " + fields[2] + " ";
    bool ep           = false;
    if (fields[3] != "-") {
        chess::Movelist list;
        chess::movegen::legalmoves(list, board);
        for (const auto& m : list) ep = ep || m.typeOf() == chess::Move::ENPASSANT;
    }
    return key + (ep ? fields[3] : "-");
}

// What the count apart knows of each game: its numbers, its boards and moves.
struct Counted {
    std::vector<HeaderNumbers> numbers;
    std::vector<std::vector<chess::Board>> boards;  // after each ply, the start first
    std::vector<std::vector<chess::Move>> moves;
};

Counted countApart(const Snapshot& snap, const std::vector<std::vector<std::string>>& lines) {
    Counted c;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        c.numbers.push_back(snap.numbers(i));
        chess::Board board;
        std::vector<chess::Board> boards{board};
        std::vector<chess::Move> moves;
        for (const auto& san : lines[i]) {
            const auto m = chess::uci::parseSan(board, san);
            moves.push_back(m);
            board.makeMove(m);
            boards.push_back(board);
        }
        c.boards.push_back(std::move(boards));
        c.moves.push_back(std::move(moves));
    }
    return c;
}

// The ply each game first reaches a position at, within its first `plies` positions; -1 when it does not.
int reaches(const Counted& c, std::size_t game, const std::string& key, uint32_t plies) {
    for (std::size_t ply = 0; ply < c.boards[game].size() && ply <= plies; ++ply)
        if (keyOf(c.boards[game][ply]) == key) return static_cast<int>(ply);
    return -1;
}

ScanBoard scanOf(const chess::Board& board) {
    ScanBoard s;
    REQUIRE(s.set(board));
    return s;
}

chess::Board boardOf(const std::string& line) {
    chess::Board board;
    for (const auto& san : words(line)) board.makeMove(chess::uci::parseSan(board, san));
    return board;
}

std::string uciOf(const chess::Board& board, int code) {
    const auto scan = scanOf(board);
    const auto move = moveOfCode(board, scan, code);
    REQUIRE(move);
    return chess::uci::moveToUci(*move);
}

void addTo(TreeStats& s, const HeaderNumbers& n) { s.add(n); }

// The tests' names of a structure: 0 when White's d-pawn faces Black's with a black pawn on c6, neither a white c-pawn
// nor a black e-pawn (as a Carlsbad), owned by White; 1 an isolated d-pawn, its side's.
const StructureNamer NAMER = [](uint64_t white, uint64_t black, std::vector<std::pair<int, int>>& out) {
    const auto on   = [](uint64_t pawns, const char* sq) { return (pawns >> ((sq[1] - '1') * 8 + (sq[0] - 'a'))) & 1; };
    const auto file = [](uint64_t pawns, int f) { return (pawns & (0x0101010101010101ull << f)) != 0; };
    if (on(white, "d4") && on(black, "d5") && on(black, "c6") && !file(white, 2) && !file(black, 4))
        out.emplace_back(0, 0);
    for (int side = 0; side < 2; ++side) {
        const auto pawns = side == 0 ? white : black;
        if (file(pawns, 3) && !file(pawns, 2) && !file(pawns, 4)) out.emplace_back(1, side);
    }
};

// The tests' name of a plan: 7 for White's b-pawn going to its fourth or fifth rank, 0 otherwise.
const PlanNamer PLANS = [](const ScanBoard& before, const CodedMove& push) {
    return before.sideToMove() == WHITE_SIDE && push.to % 8 == 1 && (push.to / 8 == 3 || push.to / 8 == 4) ? 7 : 0;
};

uint8_t sq(const char* name) { return static_cast<uint8_t>((name[1] - '1') * 8 + (name[0] - 'a')); }

// A side's pawn pushes (no capture) in the plies after `from`, their squares in order; and where each man standing
// on the position went, its squares from there (castling ends a route, as a capture does).
struct Did {
    std::array<std::vector<uint8_t>, 2> pushes;
    std::map<int, std::vector<uint8_t>> routes;  // by the man's square on the position
};

Did didAfter(const Counted& c, std::size_t game, int from, uint32_t plies) {
    Did d;
    const auto& boards = c.boards[game];
    std::map<int, int> man;  // square now → square on the position
    std::set<int> ended;
    const auto& start = boards[static_cast<std::size_t>(from)];
    for (int s = 0; s < 64; ++s) {
        const auto p = start.at(chess::Square(s));
        if (p == chess::Piece::NONE || p.type() == chess::PieceType::PAWN || p.type() == chess::PieceType::KING)
            continue;
        man[s]      = s;
        d.routes[s] = {static_cast<uint8_t>(s)};
    }
    for (uint32_t k = 0; k < plies && static_cast<std::size_t>(from) + k < c.moves[game].size(); ++k) {
        const auto& b  = boards[static_cast<std::size_t>(from) + k];
        const auto m   = c.moves[game][static_cast<std::size_t>(from) + k];
        const int side = b.sideToMove() == chess::Color::WHITE ? 0 : 1;
        const int f = m.from().index(), t = m.to().index();
        if (m.typeOf() == chess::Move::CASTLING) {
            if (man.count(t)) ended.insert(man[t]);
            continue;
        }
        const auto piece = b.at(m.from());
        if (piece.type() == chess::PieceType::PAWN && !b.isCapture(m))
            d.pushes[side].push_back(static_cast<uint8_t>(t));
        if (man.count(t)) {  // taken
            ended.insert(man[t]);
            man.erase(t);
        }
        if (man.count(f)) {
            const int origin = man[f];
            man.erase(f);
            if (!ended.count(origin)) {
                auto& r = d.routes[origin];
                if (r.size() < 3) r.push_back(static_cast<uint8_t>(t));
            }
            if (m.typeOf() == chess::Move::PROMOTION) ended.insert(origin);
            man[t] = origin;
        }
    }
    return d;
}

bool pushedInOrder(const std::vector<uint8_t>& pushes, const std::vector<uint8_t>& squares) {
    std::size_t want = 0;
    for (const auto p : pushes)
        if (want < squares.size() && p == squares[want]) ++want;
    return want == squares.size();
}

}  // namespace

TEST_CASE("opening report: moves, main lines, structures and plans, each counted apart") {
    std::vector<std::vector<std::string>> lines;
    const auto specs = planGames(300);
    auto db          = databaseOf("report_plans", specs, lines);
    const auto tree  = treeOf(*db, 60);
    const auto snap  = db->snapshot();
    const auto c     = countApart(*snap, lines);
    const auto here  = boardOf(MAIN_ORDER);
    const auto key   = keyOf(here);

    const std::vector<ReportSource> sources{{"Report", snap, tree}};
    ReportOptions options;
    options.structures  = NAMER;
    options.plans_named = PLANS;
    options.plans       = 5;  // every plan the games were made with
    options.threads     = 3;
    ReportProgress progress;
    std::atomic<bool> cancel{false};
    std::vector<ReportSection> told;
    const auto report = makeOpeningReport(sources, scanOf(here), options, progress, cancel,
                                          [&](const OpeningReport& r) { told.push_back(r.made); });
    CHECK_FALSE(report.cancelled);
    CHECK(told == std::vector{ReportSection::Shapes, ReportSection::Novelties, ReportSection::Done});
    CHECK(progress.done == progress.total);

    // The games that reach the position, and the move each plays there.
    std::vector<std::pair<std::size_t, int>> through;
    std::map<std::string, TreeStats> moves;
    TreeStats total;
    for (std::size_t g = 0; g < lines.size(); ++g) {
        const int ply = reaches(c, g, key, 60);
        if (ply < 0) continue;
        through.emplace_back(g, ply);
        addTo(total, c.numbers[g]);
        if (static_cast<std::size_t>(ply) < c.moves[g].size())
            addTo(moves[chess::uci::moveToUci(c.moves[g][static_cast<std::size_t>(ply)])], c.numbers[g]);
    }
    REQUIRE(through.size() == 300);
    CHECK(report.moves.total == total);
    CHECK(report.moves.per_source == std::vector<uint64_t>{300});
    CHECK_FALSE(report.moves.searched);
    REQUIRE(report.moves.moves.size() == moves.size());
    for (std::size_t i = 0; i < report.moves.moves.size(); ++i) {
        const auto& m  = report.moves.moves[i];
        const auto uci = uciOf(here, m.code);
        CAPTURE(uci);
        REQUIRE(moves.count(uci));
        CHECK(m.stats == moves[uci]);
        if (i > 0) CHECK(m.stats.games <= report.moves.moves[i - 1].stats.games);
        CHECK(report.moves.move_per_source[i] == std::vector<uint64_t>{m.stats.games});
    }

    // The main lines, as the count apart follows them: the most played move after the first while it keeps a fifth of
    // the first move's games, and 10 at least.
    const auto statsAt = [&](const chess::Board& at) {
        const auto k = keyOf(at);
        std::map<std::string, TreeStats> next;
        TreeStats all;
        for (std::size_t g = 0; g < lines.size(); ++g) {
            const int ply = reaches(c, g, k, 60);
            if (ply < 0) continue;
            addTo(all, c.numbers[g]);
            if (static_cast<std::size_t>(ply) < c.moves[g].size())
                addTo(next[chess::uci::moveToUci(c.moves[g][static_cast<std::size_t>(ply)])], c.numbers[g]);
        }
        return std::pair{all, next};
    };
    // Whether the count apart follows a move's line one move further: its most played continuation keeps the measure
    // and leads where two games go at least.
    const auto goesOn = [&](const chess::Board& at, uint64_t least) {
        const auto [all, next] = statsAt(at);
        uint64_t most          = 0;
        for (const auto& [u, s] : next) most = std::max(most, s.games);
        bool on = most >= least && most > 0;
        for (const auto& [u, s] : next) {
            if (!on || s.games != most) continue;
            auto after = at;
            after.makeMove(chess::uci::uciToMove(after, u));
            on = statsAt(after).first.games >= 2;
        }
        return on;
    };
    REQUIRE(report.lines.size() >= 2);
    REQUIRE(report.lines.size() <= 3);
    std::size_t first_move = 0;
    for (std::size_t l = 0; l < report.lines.size(); ++l) {
        CAPTURE(l);
        const auto& line = report.lines[l];
        auto board       = here;
        // Its first move: the next most played whose line goes on; those before it have no common continuation.
        while (first_move < report.moves.moves.size() &&
               uciOf(here, report.moves.moves[first_move].code) != uciOf(here, line.codes.front())) {
            auto after = here;
            after.makeMove(chess::uci::uciToMove(after, uciOf(here, report.moves.moves[first_move].code)));
            CHECK_FALSE(goesOn(after, std::max<uint64_t>(10, report.moves.moves[first_move].stats.games / 5)));
            ++first_move;
        }
        REQUIRE(first_move < report.moves.moves.size());
        const uint64_t least = std::max<uint64_t>(10, report.moves.moves[first_move].stats.games / 5);
        ++first_move;
        CHECK(line.codes.size() >= 2);
        board.makeMove(chess::uci::uciToMove(board, uciOf(board, line.codes.front())));
        for (std::size_t k = 1;; ++k) {
            const auto [all, next] = statsAt(board);
            uint64_t most          = 0;
            for (const auto& [u, s] : next) most = std::max(most, s.games);
            if (k == line.codes.size()) {
                // It ends where the most played move falls under the measure, or leads where fewer than two go (each
                // of the most played, when several are played as often).
                bool goes_on = most >= least && most > 0;
                for (const auto& [u, s] : next) {
                    if (!goes_on || s.games != most) continue;
                    auto after = board;
                    after.makeMove(chess::uci::uciToMove(after, u));
                    goes_on = statsAt(after).first.games >= 2;
                }
                CHECK_FALSE(goes_on);
                CHECK(line.stats == all);
                break;
            }
            const auto played = uciOf(board, line.codes[k]);
            REQUIRE(next.count(played));
            CHECK(next.at(played).games == most);  // the most played
            CHECK(most >= least);
            board.makeMove(chess::uci::uciToMove(board, played));
        }
    }

    // Fewer than three lines: no other move's line goes on.
    for (std::size_t i = first_move; report.lines.size() < 3 && i < report.moves.moves.size(); ++i) {
        auto after = here;
        after.makeMove(chess::uci::uciToMove(after, uciOf(here, report.moves.moves[i].code)));
        CHECK_FALSE(goesOn(after, std::max<uint64_t>(10, report.moves.moves[i].stats.games / 5)));
    }

    // The structures at move 20 (twenty plies after the position) and the plans of the thirty plies after it.
    const auto& shapes = report.shapes;
    CHECK(shapes.enough);
    CHECK(shapes.ply == 20);
    CHECK(shapes.replayed == 300);
    std::map<std::pair<int, int>, TreeStats> structures;
    uint64_t counted = 0;
    std::vector<Did> did;
    for (const auto& [g, ply] : through) {
        did.push_back(didAfter(c, g, ply, 30));
        const auto at = static_cast<std::size_t>(ply) + 20;
        if (at >= c.boards[g].size()) continue;
        ++counted;
        const auto& b = c.boards[g][at];
        std::vector<std::pair<int, int>> named;
        NAMER(b.pieces(chess::PieceType::PAWN, chess::Color::WHITE).getBits(),
              b.pieces(chess::PieceType::PAWN, chess::Color::BLACK).getBits(), named);
        addTo(structures[named.empty() ? std::pair{-1, 0} : named.front()], c.numbers[g]);
    }
    CHECK(shapes.counted == counted);
    REQUIRE(shapes.structures.size() == structures.size());
    for (const auto& s : shapes.structures) {
        CAPTURE(s.kind);
        CAPTURE(s.owner);
        CHECK(s.stats == structures[{s.kind, s.owner}]);
        CHECK(s.games.size() == s.stats.games);
        CHECK(s.kept == (s.kind == 0));  // the position is a Carlsbad already
    }
    CHECK(shapes.structures.back().kind == -1);  // "other" last
    CHECK(shapes.structures.front().kind == 0);

    // Each plan's games and results, counted apart; the plans the games were made with found.
    bool minority = false, centre = false, knight = false;
    for (const auto& p : shapes.plans) {
        TreeStats expected;
        uint64_t n = 0;
        for (std::size_t i = 0; i < through.size(); ++i) {
            const auto& d = did[i];
            bool holds    = false;
            if (p.kind == ReportPlan::Kind::Route) {
                const auto r = d.routes.find(p.squares.front());
                holds        = r != d.routes.end() && r->second.size() >= p.squares.size() &&
                               std::equal(p.squares.begin(), p.squares.end(), r->second.begin());
            } else {
                holds = pushedInOrder(d.pushes[p.side], p.squares);
            }
            if (!holds) continue;
            ++n;
            addTo(expected, c.numbers[through[i].first]);
        }
        CAPTURE(static_cast<int>(p.kind));
        CAPTURE(p.side);
        CAPTURE(std::vector<int>(p.squares.begin(), p.squares.end()));
        CHECK(p.stats == expected);
        CHECK(p.games.size() == n);
        CHECK(n * 10 >= 300);  // a tenth of the games at least
        if (p.side == 0 && p.kind == ReportPlan::Kind::Pushes && p.squares == std::vector{sq("b4"), sq("b5")}) {
            minority = true;
            CHECK(p.named == 7);
        }
        // The centre's plan: f3, then e4 when Black's knight leaves e4 free.
        if (p.side == 0 && p.kind != ReportPlan::Kind::Route &&
            std::find(p.squares.begin(), p.squares.end(), sq("f3")) != p.squares.end())
            centre = true;
        if (p.side == 1 && p.kind == ReportPlan::Kind::Route && p.squares.front() == sq("f8") &&
            p.squares[1] == sq("g6"))
            knight = true;
        if (p.kind == ReportPlan::Kind::Route) CHECK(p.named == 0);
    }
    CHECK(minority);
    CHECK(centre);
    CHECK(knight);
    // White's plans first, each side's the most played first, as many as asked at most.
    for (std::size_t i = 1; i < shapes.plans.size(); ++i) {
        CHECK(shapes.plans[i - 1].side <= shapes.plans[i].side);
        if (shapes.plans[i - 1].side == shapes.plans[i].side)
            CHECK(shapes.plans[i - 1].stats.games >= shapes.plans[i].stats.games);
    }
    for (int side = 0; side < 2; ++side)
        CHECK(std::count_if(shapes.plans.begin(), shapes.plans.end(),
                            [&](const ReportPlan& p) { return p.side == side; }) <= 5);

    // The strongest game, and the most recent of 2500 and more, found apart.
    const auto mean = [](const HeaderNumbers& n) {
        if (n.white_elo && n.black_elo) return (n.white_elo + n.black_elo) / 2;
        return n.white_elo ? int{n.white_elo} : int{n.black_elo};
    };
    std::size_t strongest = through.front().first;
    for (const auto& [g, ply] : through) {
        const auto& a = c.numbers[g];
        const auto& b = c.numbers[strongest];
        if (mean(a) > mean(b) || (mean(a) == mean(b) && (a.date > b.date || (a.date == b.date && g < strongest))))
            strongest = g;
    }
    REQUIRE_FALSE(report.key_games.empty());
    CHECK(report.key_games[0].why == ReportKeyGame::Why::Strongest);
    CHECK(report.key_games[0].game.id == strongest);
    std::optional<std::size_t> recent;
    for (const auto& [g, ply] : through) {
        if (g == strongest || mean(c.numbers[g]) < 2500) continue;
        if (!recent || c.numbers[g].date > c.numbers[*recent].date ||
            (c.numbers[g].date == c.numbers[*recent].date && mean(c.numbers[g]) > mean(c.numbers[*recent])))
            recent = g;
    }
    REQUIRE(recent);
    REQUIRE(report.key_games.size() >= 2);
    CHECK(report.key_games[1].why == ReportKeyGame::Why::Recent);
    CHECK(report.key_games[1].numbers.date == c.numbers[*recent].date);
    CHECK(mean(report.key_games[1].numbers) >= 2500);
}

TEST_CASE("opening report: novelties confirmed by the position search, the user's game that left the databases") {
    // A tree of 22 plies: the position (ply 20) and the one after its move are in it, a game four plies late is not.
    std::vector<G> specs = planGames(120);
    for (std::size_t i = 0; i < specs.size(); ++i) {
        auto& g = specs[i];
        g.date  = "20" + std::to_string(10 + g.seed % 9) + ".04.12";  // 2010 to 2018
        // Their move at the position, a rook's: the moves of the novelties are those of the games below alone.
        if (g.prefix == MAIN_ORDER || g.prefix == OTHER_ORDER) g.then = {i % 3 ? "Rab1" : "Rae1"};
    }
    // 11.h3, first in 2025 and repeated twice: a novelty of these years.
    for (int i = 0; i < 3; ++i) {
        G g;
        g.then      = {"h3"};
        g.date      = i == 0 ? "2025.03.02" : i == 1 ? "2025.09.14" : "2026.01.20";
        g.white_elo = 2560;
        g.black_elo = 2540 + i;
        g.result    = i == 1 ? '=' : '1';
        g.seed      = static_cast<uint32_t>(7000 + i);
        specs.push_back(g);
    }
    // 11.a3, first in 2024 by the trees; but a game of 2014 reached that position four plies late: no novelty.
    for (int i = 0; i < 3; ++i) {
        G g;
        g.then = {"a3"};
        g.date = "2024.0" + std::to_string(2 + i) + ".05";
        g.seed = static_cast<uint32_t>(7100 + i);
        specs.push_back(g);
    }
    G late;
    late.prefix = TEMPO_ORDER;
    late.then   = {"a3"};
    late.date   = "2014.06.01";
    late.seed   = 7200;
    specs.push_back(late);
    std::vector<std::vector<std::string>> lines;
    auto db         = databaseOf("report_novelties", specs, lines);
    const auto tree = treeOf(*db, 22);
    // The user's games: one that leaves the databases at once with 11.Kh1, of their names (Settings › Your names).
    G yours;
    yours.then  = {"Kh1"};
    yours.white = "Tester, Sam";
    yours.black = "Okoro, Jide";
    yours.date  = "2026.03.08";
    yours.seed  = 7300;
    std::vector<std::vector<std::string>> mine_lines;
    auto mine            = databaseOf("report_novelties_mine", {yours}, mine_lines);
    const auto mine_tree = treeOf(*mine, 22);

    const auto here = boardOf(MAIN_ORDER);
    const std::vector<ReportSource> sources{{"Club", db->snapshot(), tree}, {"My games", mine->snapshot(), mine_tree}};
    ReportOptions options;
    options.structures = NAMER;
    options.user       = [](std::string_view name) { return name == "Tester, Sam"; };
    options.threads    = 2;
    ReportProgress progress;
    std::atomic<bool> cancel{false};
    const auto report = makeOpeningReport(sources, scanOf(here), options, progress, cancel);
    CHECK_FALSE(report.cancelled);
    // The trees' games at the position: the user's alone in theirs (a tree of one game keeps no position), the 2014
    // game late.
    CHECK(report.moves.per_source[0] == 126);
    const auto& found = report.novelties.moves;
    std::vector<std::string> said;
    for (const auto& n : found) said.push_back(uciOf(here, n.codes.front()) + (n.yours ? " yours" : ""));
    CAPTURE(said);
    const auto h3 = std::find_if(found.begin(), found.end(), [&](const ReportNovelty& n) {
        return !n.yours && n.codes.size() == 1 && uciOf(here, n.codes.front()) == "h2h3";
    });
    REQUIRE(h3 != found.end());
    CHECK(h3->first == packDate("2025.03.02"));
    CHECK(h3->stats.games == 3);
    CHECK(h3->stats.white == 2);
    CHECK(h3->stats.draws == 1);
    CHECK(h3->confirmed);
    REQUIRE(h3->first_game);
    CHECK(h3->first_game->source == 0);
    CHECK(lines[h3->first_game->id].size() > 20);
    CHECK(lines[h3->first_game->id][20] == "h3");
    CHECK(db->snapshot()->numbers(h3->first_game->id).date == packDate("2025.03.02"));
    // 11.a3 is older than the trees said: the position search found the game of 2014.
    CHECK(std::none_of(found.begin(), found.end(),
                       [&](const ReportNovelty& n) { return !n.yours && uciOf(here, n.codes.front()) == "a2a3"; }));
    // Every novelty is of the last five years of the databases, repeated, the most recent first.
    for (std::size_t i = 0; i < found.size(); ++i) {
        if (found[i].yours) continue;
        CHECK((found[i].first >> 9) >= 2022);
        CHECK(found[i].stats.games >= 2);
        if (i > 0 && !found[i - 1].yours) CHECK(found[i - 1].first >= found[i].first);
    }
    // The user's game, last: it left the databases with 11.Kh1, which none of the 126 games played.
    REQUIRE_FALSE(found.empty());
    const auto& last = found.back();
    REQUIRE(last.yours);
    CHECK(last.codes.size() == 1);
    CHECK(uciOf(here, last.codes.front()) == "g1h1");
    REQUIRE(last.game);
    CHECK(last.game->source == 1);
    CHECK(last.game->id == 0);
    CHECK(last.before == 126);
    CHECK(last.confirmed);
    // The first game of 11.h3, a key game.
    const auto key = std::find_if(report.key_games.begin(), report.key_games.end(),
                                  [](const ReportKeyGame& k) { return k.why == ReportKeyGame::Why::Novelty; });
    REQUIRE(key != report.key_games.end());
    CHECK(key->game == *h3->first_game);
    CHECK(&found[key->novelty] == &*h3);
}

TEST_CASE("opening report: a rare position searched, too few games for plans, cancelled") {
    std::vector<std::vector<std::string>> lines;
    auto specs = planGames(60);
    G odd;
    odd.prefix    = "e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6 Nc3 a6 h3";
    odd.seed      = 4242;
    odd.plies     = 30;
    odd.white_elo = 2300;
    odd.black_elo = 2250;
    specs.push_back(odd);
    auto db         = databaseOf("report_rare", specs, lines);
    const auto tree = treeOf(*db, 60);
    const std::vector<ReportSource> sources{{"Club", db->snapshot(), tree}};
    ReportOptions options;
    options.structures = NAMER;
    ReportProgress progress;
    std::atomic<bool> cancel{false};
    // A position a single game reaches: not in the tree, found by the position search.
    const auto rare   = boardOf("e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6 Nc3 a6 h3");
    const auto report = makeOpeningReport(sources, scanOf(rare), options, progress, cancel);
    CHECK_FALSE(report.cancelled);
    CHECK(report.moves.searched);
    CHECK(report.moves.total.games == 1);
    REQUIRE(report.moves.moves.size() == 1);
    CHECK(uciOf(rare, report.moves.moves[0].code) ==
          chess::uci::moveToUci(chess::uci::parseSan(rare, lines.back()[11])));
    CHECK(report.lines.empty());  // no tree to follow
    CHECK_FALSE(report.shapes.enough);
    CHECK(report.shapes.structures.empty());
    CHECK(report.shapes.plans.empty());
    REQUIRE(report.key_games.size() == 1);  // the only game is the strongest
    CHECK(report.key_games[0].game.id == specs.size() - 1);

    // Under fifty games: moves and games, no plans nor structures.
    const auto few = makeOpeningReport(sources, scanOf(boardOf(OTHER_ORDER)), options, progress, cancel);
    CHECK(few.moves.total.games == 60);
    CHECK(few.shapes.enough);
    options.least_games = 61;
    const auto too_few  = makeOpeningReport(sources, scanOf(boardOf(OTHER_ORDER)), options, progress, cancel);
    CHECK_FALSE(too_few.shapes.enough);
    CHECK(too_few.shapes.plans.empty());
    CHECK(too_few.shapes.structures.empty());
    CHECK_FALSE(too_few.moves.moves.empty());

    // Cancelled: stopped, marked so.
    cancel             = true;
    const auto stopped = makeOpeningReport(sources, scanOf(boardOf(OTHER_ORDER)), options, progress, cancel);
    CHECK(stopped.cancelled);
}

TEST_CASE("opening report: the scanning board's FEN reads back to the same position") {
    for (const char* line : {MAIN_ORDER, "e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6 Nc3 a6", "e4 e5 Nf3 Nc6 Bb5 a6 Ba4 Nf6 O-O"}) {
        auto board      = boardOf(line);
        const auto scan = scanOf(board);
        ScanBoard again;
        REQUIRE(again.setFen(scan.fen(), false));
        CHECK(again.key() == scan.key());
        CHECK(keyOf(chess::Board(scan.fen())) == keyOf(board));
    }
    ScanBoard start;
    REQUIRE(start.setFen(chess::constants::STARTPOS, false));
    CHECK(start.fen() == chess::constants::STARTPOS);
}
