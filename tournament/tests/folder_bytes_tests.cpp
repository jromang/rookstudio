// The bytes a tournament leaves on the disk, held to those the code of b602c425 wrote, before the journal's events were
// read in one place and the PGN file of the user's written by a class of its own (#113). Tournaments of each format are
// played with games the test makes the same at every run (their moves, scores, clocks and dates), edited, stopped and
// resumed, cut short by a crash or a disk full; each step is hashed with what a read-only copy following the journal, a
// summary and the incident scanner read of them. The hashes are made again only for a change of the format, saying
// why in the commit. And the folder that code left at the match's crash (tournament/tests/data/written-by-b602c425)
// resumes as it resumed it.

#include <doctest/doctest.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <fmt/include/fmt/ranges.h>

#include <tour/edits.hpp>
#include <tour/files.hpp>
#include <tour/incidents.hpp>
#include <tour/journal.hpp>
#include <tour/openings.hpp>
#include <tour/pgn_file.hpp>
#include <tour/standings.hpp>
#include <tour/state.hpp>

#include "test_support.hpp"
#include "tour_fixtures.hpp"

using namespace fcg::tour;
namespace fs = std::filesystem;
using fastchess::MatchTermination;
using fcg::test::readFile;
using fcg::test::ScratchDir;

namespace {

// The positions of a small EPD book, each with the knights at home.
const char* const POSITIONS[] = {"rnbqkbnr/pppppppp/8/8/P7/8/1PPPPPPP/RNBQKBNR b KQkq - 0 1",
                                 "rnbqkbnr/pppppppp/8/8/1P6/8/P1PPPPPP/RNBQKBNR b KQkq - 0 1",
                                 "rnbqkbnr/pppppppp/8/8/2P5/8/PP1PPPPP/RNBQKBNR b KQkq - 0 1",
                                 "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 1",
                                 "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1",
                                 "rnbqkbnr/pppppppp/8/8/7P/8/PPPPPPP1/RNBQKBNR b KQkq - 0 1"};

void useBook(TournamentSpec& spec, const fs::path& file) {
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        for (const auto* fen : POSITIONS) out << fen << "\n";
    }
    spec.conditions.opening.file   = file.string();
    spec.conditions.opening.format = fastchess::FormatType::EPD;
    spec.conditions.opening.order  = fastchess::OrderType::SEQUENTIAL;
}

// A game as fastchess reports it, the same at every run: from the task's opening (its moves as book moves), `plies`
// more moves, each the (ply * 7 + game number)-th of the legal ones, with a score (White `edge` centipawns ahead, from
// the mover's point of view), a depth, nodes, a time and the clock; the dates and the duration fixed. `result` is
// White's. A PV warning on the fourth move of every fifth game.
fastchess::MatchData gameOf(const GameTask& task, std::string_view result, MatchTermination how, int edge = 30,
                            int plies = 10) {
    auto data        = fcg::test::fakeGame(task, result);
    data.date        = "2026.11.27";
    data.start_time  = "2026-11-27T10:00:00 +0100";
    data.end_time    = "2026-11-27T10:00:07 +0100";
    data.duration    = "00:00:07";
    data.termination = how;
    data.reason      = fmt::format("test game {} ({})", task.number() + 1, result);

    chess::Board board;
    REQUIRE(fastchess::book::setPlayablePosition(board, task.opening.fen_epd));
    data.fen = board.getFen();
    for (const auto& move : task.opening.moves) {
        fastchess::MoveData book(chess::uci::moveToUci(move));
        book.book = true;
        data.moves.push_back(book);
        board.makeMove(move);
    }
    int64_t clock[2] = {60000, 60000};
    for (int ply = 0; ply < plies; ++ply) {
        chess::Movelist moves;
        chess::movegen::legalmoves(moves, board);
        if (moves.empty()) break;
        const auto move  = moves[static_cast<int>((uint64_t(ply) * 7 + task.number()) % uint64_t(moves.size()))];
        const bool white = board.sideToMove() == chess::Color::WHITE;
        const fastchess::Score score{fastchess::ScoreType::CP, white ? edge : -edge};
        fastchess::MoveData m(chess::uci::moveToUci(move), score, 100 + ply, 10 + ply % 5, 14, 20000 + 1000 * ply);
        clock[white ? 0 : 1] -= 100 + ply;
        m.timeleft = clock[white ? 0 : 1];
        m.nps      = 200000;
        if (ply == 3 && task.number() % 5 == 0) m.pv_warnings.push_back("illegal PV move e2e5");
        data.moves.push_back(m);
        board.makeMove(move);
    }
    return data;
}

// Plays the games a tournament hands out, each ending as its place among the games played says: White wins, a draw,
// Black wins in turn, and now and then a loss on time, a crash, an illegal move.
struct Player {
    int played = 0;

    // `count` games at most (all of them when negative): how many were played.
    int play(TournamentState& state, int count = -1) {
        int now = 0;
        while (count < 0 || now < count) {
            const auto task = state.claimNext();
            if (!task) break;
            state.complete(*task, next(*task));
            ++now;
            REQUIRE(now < 5000);
        }
        return now;
    }

    fastchess::MatchData next(const GameTask& task) {
        static const char* const IN_TURN[] = {"1-0", "1/2-1/2", "0-1", "1-0", "1/2-1/2"};
        const int n                        = played++;
        const auto how                     = n % 7 == 3    ? MatchTermination::TIMEOUT
                                             : n % 11 == 5 ? MatchTermination::DISCONNECT
                                             : n % 13 == 8 ? MatchTermination::ILLEGAL_MOVE
                                                           : MatchTermination::NORMAL;
        const char* result                 = how == MatchTermination::NORMAL ? IN_TURN[n % 5] : n % 2 ? "0-1" : "1-0";
        return gameOf(task, result, how);
    }
};

// The journal lines the program playing the tournament wrote, as its listener told them, for a copy to follow.
struct Lines {
    struct Line {
        uint64_t at = 0, end = 0;
        nlohmann::json event;
    };
    std::mutex mutex;
    std::vector<Line> lines;

    Journal::Listener listener() {
        return [this](uint64_t at, uint64_t end, const nlohmann::json& e) {
            std::lock_guard<std::mutex> lock(mutex);
            lines.push_back({at, end, e});
        };
    }

    // `copy` follows the lines it does not hold yet; it is opened again when it cannot (a line it missed, an edit
    // undone), as the window's copy is.
    void follow(std::unique_ptr<TournamentState>& copy) {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& l : lines) {
            if (l.at < copy->journalSize()) continue;
            if (l.at > copy->journalSize() || !copy->follow(l.event, l.end)) {
                copy = TournamentState::open(copy->dir(), true);
            }
        }
    }
};

// The journal's bytes, the time each game finished at made 0 (the "at" of its game_finished line, unix milliseconds,
// its first key): the only bytes of a tournament's files that change from one run to the next.
std::string journalText(const fs::path& file) {
    const auto text = readFile(file);
    std::string out;
    for (std::size_t pos = 0; pos < text.size();) {
        auto end  = text.find('\n', pos);
        end       = end == std::string::npos ? text.size() : end + 1;
        auto line = text.substr(pos, end - pos);
        pos       = end;
        if (line.rfind("{\"at\":", 0) == 0 && line.find("\"t\":\"game_finished\"") != std::string::npos) {
            line = "{\"at\":0" + line.substr(line.find_first_not_of("0123456789", 6));
        }
        out += line;
    }
    return out;
}

// The files of a tournament's folder, each its name, size and bytes, the journal's times made 0. Not tournament.json,
// written once when the tournament is made, with the paths of the test's folder in it, nor the empty tournament.lock.
std::string folderText(const fs::path& dir) {
    std::map<std::string, std::string> files;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        const auto name = fs::relative(entry.path(), dir).generic_string();
        if (!entry.is_regular_file() || name == "tournament.json" || name == "tournament.lock") continue;
        files[name] = name == "journal.jsonl" ? journalText(entry.path()) : readFile(entry.path());
    }
    std::string out;
    for (const auto& [name, bytes] : files) out += fmt::format("== {} {}\n", name, bytes.size()) + bytes;
    return out;
}

std::string fileText(const fs::path& file) {
    const auto bytes = readFile(file);
    return fmt::format("== {} {}\n", file.filename().generic_string(), bytes.size()) + bytes;
}

std::string resultText(const GameResult& r) {
    auto out =
        fmt::format("game {} {}-{} {} t{} '{}' plies {} ms {} pgn {}+{} cb {}+{} unbalanced {}", r.number(), r.white,
                    r.black, r.result, static_cast<int>(r.termination), r.reason, r.plies, r.duration_ms, r.pgn.offset,
                    r.pgn.length, r.pgn_chessbase.offset, r.pgn_chessbase.length, r.unbalanced);
    for (const auto& [ply, warning] : r.pv_warnings) out += fmt::format(" pv {} '{}'", ply, warning);
    if (r.voided) out += fmt::format(" voided '{}'", r.void_reason);
    if (r.set) {
        out += fmt::format(" set '{}' played {} t{} '{}'", r.set->why, r.set->played_result,
                           static_cast<int>(r.set->played_termination), r.set->played_reason);
    }
    return out + "\n";
}

// What the state answers of the tournament, standings included (but their ratings, whose last digits may differ from
// one compiler to another).
std::string stateText(const TournamentState& s) {
    const auto spec = s.specSnapshot();
    std::string out = fmt::format("cycles {} journal {} total {} finished {} '{}' error {} warnings {}\n", spec->cycles,
                                  s.journalSize(), s.totalGames() ? std::to_string(*s.totalGames()) : "-", s.finished(),
                                  s.finishReason(), !s.pgnOutError().empty(), s.warnings().size());
    out += fmt::format("work {} undo {} {} replaced {} network {}\n", s.hasWork(), s.canUndo(), s.undoLasts(),
                       s.openingsReplaced(), s.networkVoids());
    for (const auto& p : spec->participants) {
        out += fmt::format("participant {} '{}' active {} replaced by {} team {} elo {}\n", p.id, p.engine.name,
                           p.active, p.replaced_by, p.team, p.elo);
    }
    for (const auto& name : s.enginesLeft()) out += fmt::format("left '{}'\n", name);
    const auto results = s.results();
    for (const auto& r : results) out += resultText(r);
    for (const auto& row : s.scheduleRows()) {
        out += fmt::format("encounter {} round {} cycle {} {}-{} {} {}\n", row.encounter.index, row.encounter.round,
                           row.encounter.cycle, row.encounter.first, row.encounter.second,
                           static_cast<int>(row.status[0]), static_cast<int>(row.status[1]));
    }
    for (const auto& [id, n] : s.byes()) out += fmt::format("bye {} {}\n", id, n);
    for (const auto& [round, places] : s.brackets()) {
        out += fmt::format("bracket {}:", round);
        for (const auto& place : places) out += fmt::format(" {}", fmt::join(place, "/"));
        out += "\n";
    }
    for (const auto& [number, opening] : s.gameOpenings()) {
        const auto text = toText(opening, false);
        out += fmt::format("opening {} {} {}\n", number, text.fen, fmt::join(text.moves, " "));
    }
    for (const auto& a : spec->participants) {
        for (const auto& b : spec->participants) {
            if (a.id >= b.id) continue;
            const auto st = s.pairStats(a.id, b.id);
            out += fmt::format("pair {}-{} +{} ={} -{} penta {} {} {} {} {} history {}\n", a.id, b.id, st.wins,
                               st.draws, st.losses, st.penta_WW, st.penta_WD, st.penta_WL, st.penta_DD, st.penta_LD,
                               s.pairHistory(a.id, b.id).size());
        }
    }
    const auto table = computeTable(*spec, results, std::nullopt, s.byes());
    for (const auto& row : table.rows) {
        out += fmt::format("standing {} '{}' {} +{} ={} -{} points {} sb {} buchholz {} byes {} lost {} {} {}\n",
                           row.id, row.name, row.games, row.wins, row.draws, row.losses, row.points, row.sb,
                           row.buchholz, row.byes, row.timeouts, row.crashes, row.illegal);
    }
    return out;
}

// What a summary of the tournament reads of its journal (TournamentState::replay()).
std::string replayText(const fs::path& dir) {
    auto spec       = nlohmann::json::parse(readFile(dir / "tournament.json")).get<TournamentSpec>();
    const auto r    = TournamentState::replay(Journal::read(dir / "journal.jsonl", false), spec);
    std::string out = fmt::format("replay played {} total {} finished {} '{}'\n", r.played,
                                  r.total ? std::to_string(*r.total) : "-", r.finished, r.finish_reason);
    for (const auto& g : r.results) out += resultText(g);
    for (const auto& [id, n] : r.byes) out += fmt::format("bye {} {}\n", id, n);
    for (const auto& place : r.last_bracket) out += fmt::format("last bracket {}\n", fmt::join(place, "/"));
    for (const auto& p : spec.participants) out += fmt::format("participant {} active {}\n", p.id, p.active);
    return out;
}

// What the incident scanner reads of the tournaments of `root`.
std::string incidentsText(const fs::path& root) {
    IncidentScanner scanner;
    const auto in   = scanner.scan(root, 0);
    std::string out = fmt::format("incidents crashes {} time {} illegal {} network {}\n", in.crashes, in.time_losses,
                                  in.illegal_moves, in.network_cuts);
    for (const auto& e : in.engines) {
        out += fmt::format("engine '{}' {} {} {} {} in {}\n", e.engine, e.crashes, e.time_losses, e.early_time_losses,
                           e.illegal_moves, e.dir.filename().generic_string());
    }
    return out;
}

// The hash of each step of a scenario, FNV-1a, held to those of the code of b602c425 (before #113). With
// FCG_TOUR_BYTES_DUMP set to a folder, the text of each step is also written there, to compare two versions.
struct Steps {
    std::string scenario;
    std::vector<std::pair<std::string, uint64_t>> hashes;

    void mark(const std::string& label, const std::string& text) {
        uint64_t h = 0xcbf29ce484222325ULL;
        for (const unsigned char c : text) h = (h ^ c) * 0x100000001b3ULL;
        hashes.emplace_back(label, h);
        if (const char* dump = std::getenv("FCG_TOUR_BYTES_DUMP")) {
            std::ofstream(fs::path(dump) / fmt::format("{}-{:02}.txt", scenario, hashes.size()), std::ios::binary)
                << label << "\n"
                << text;
        }
    }

    void holdTo(const std::vector<std::pair<std::string, uint64_t>>& before) const {
        if (hashes != before) {
            std::string list;
            for (const auto& [label, h] : hashes) list += fmt::format("        {{\"{}\", 0x{:016x}}},\n", label, h);
            MESSAGE(scenario << ", the hashes now:\n" << list);
        }
        CHECK(hashes == before);
    }
};

// The game of the results that the operator can set by hand, from the last one: an edit setting it to the other
// side's win (a draw for a win), made.
void setLastResult(TournamentState& state, const std::string& why) {
    const auto results = state.results();
    for (auto it = results.rbegin(); it != results.rend(); ++it) {
        if (it->voided) continue;
        Edit edit;
        edit.kind   = Edit::Kind::SetResult;
        edit.games  = {{it->encounter, it->game}};
        edit.result = it->result == "1-0" ? "0-1" : it->result == "0-1" ? "1/2-1/2" : "1-0";
        edit.why    = why;
        if (!state.preview(edit).error.empty()) continue;
        state.applyEdit(edit);
        return;
    }
    FAIL("no result can be set");
}

// Cuts the journal after its last game_finished line, as a crash right after it would leave it.
void cutAfterLastGame(const fs::path& dir) {
    const auto file = dir / "journal.jsonl";
    const auto text = readFile(file);
    const auto at   = text.rfind("\"t\":\"game_finished\"");
    REQUIRE(at != std::string::npos);
    fs::resize_file(file, text.find('\n', at) + 1);
}

}  // namespace

TEST_CASE("folder bytes: a round robin edited as it plays, the PGN file of the user's, a copy following it (#113)") {
    const ScratchDir root("bytes-round-robin");
    const auto dir      = root / "Round robin";
    const auto out      = root / "all.pgn";
    TournamentSpec spec = fcg::test::SpecBuilder(Format::RoundRobin).name("Round robin").players(4).cycles(2);
    for (auto& p : spec.participants) p.elo = 2000 + 50 * p.id;
    spec.pgn_out           = out.string();
    spec.opening_per_round = true;
    useBook(spec, root / "book.epd");

    Steps steps{"round-robin"};
    Lines lines;
    Player player;
    auto state = TournamentState::create(dir, spec);
    state->setJournalListener(lines.listener());
    auto copy       = TournamentState::open(dir, true);
    const auto mark = [&](const std::string& label) {
        lines.follow(copy);
        steps.mark(label, folderText(dir) + fileText(out) + stateText(*state) + "copy\n" + stateText(*copy) +
                              replayText(dir) + incidentsText(root));
    };
    mark("created");
    player.play(*state, 5);
    mark("5 games");

    Edit add;
    add.kind   = Edit::Kind::AddParticipant;
    add.engine = fcg::test::engineNamed("E");
    add.elo    = 2300;
    state->applyEdit(add);
    mark("a participant added");
    player.play(*state, 4);
    mark("4 more games");

    setLastResult(*state, "an illegal move the GUI missed");
    mark("a result set");
    state->undoEdit();
    mark("the result set undone");

    Edit voided;
    voided.kind  = Edit::Kind::VoidGames;
    voided.games = {{0, 0}, {1, 1}};
    voided.why   = "played again";
    state->applyEdit(voided);
    mark("two games voided, played again");
    player.play(*state, 3);
    CHECK(state->voidCutGames({4, 5, 6}, "box"));
    mark("games voided for a broken connection");

    Edit dropped;
    dropped.kind   = Edit::Kind::VoidGames;
    dropped.games  = {{2, 0}};
    dropped.replay = false;
    state->applyEdit(dropped);
    mark("a game voided, not played again");

    Edit withdraw;
    withdraw.kind        = Edit::Kind::Withdraw;
    withdraw.participant = 1;
    withdraw.void_played = true;
    state->applyEdit(withdraw);
    mark("a participant withdrawn, its games voided");

    // A game the operator ends while it is played.
    {
        const auto task = state->claimNext();
        REQUIRE(task);
        CHECK(state->requestEnd(task->number(), "1-0", "the engine hung"));
        const auto end = state->takeEnd(task->number());
        REQUIRE(end);
        state->complete(*task, gameOf(*task, "1-0", MatchTermination::ADJUDICATION),
                        ResultSet{end->second, "*", MatchTermination::INTERRUPT, ""});
    }
    mark("a game ended by the operator");

    // Stopped with a game claimed and not played, resumed.
    REQUIRE(state->claimNext());
    state.reset();
    state = TournamentState::open(dir);
    state->setJournalListener(lines.listener());
    mark("resumed");

    Edit replace;
    replace.kind        = Edit::Kind::ReplaceParticipant;
    replace.participant = 2;
    replace.engine      = fcg::test::engineNamed("F");
    state->applyEdit(replace);
    mark("a participant replaced");

    Edit cycles;
    cycles.kind   = Edit::Kind::SetCycles;
    cycles.cycles = 3;
    state->applyEdit(cycles);
    mark("a third cycle");
    player.play(*state, 6);
    mark("6 more games");
    cycles.cycles = 2;
    state->applyEdit(cycles);
    mark("back to two cycles");
    player.play(*state);
    mark("played to the end");

    state.reset();
    state = TournamentState::open(dir);
    mark("opened again");

    steps.holdTo({
        {"created", 0x84478f69437c853f},
        {"5 games", 0x24a25fbc0268b365},
        {"a participant added", 0x96d40ea3abfe7bc9},
        {"4 more games", 0xe9277e743e65f5b9},
        {"a result set", 0xd7900d9206c5ac1a},
        {"the result set undone", 0x0a737fa2b6a46761},
        {"two games voided, played again", 0xb6a48e141502a69e},
        {"games voided for a broken connection", 0xa754558c69e73b35},
        {"a game voided, not played again", 0xb87599b1c1120b21},
        {"a participant withdrawn, its games voided", 0x795344772085678c},
        {"a game ended by the operator", 0x637a5296d7f8c48a},
        {"resumed", 0x6f4be7fcc76016c8},
        {"a participant replaced", 0xd002aa56f8a6ab9d},
        {"a third cycle", 0x841c6f0268821c3f},
        {"6 more games", 0x3e8df691c905ca9f},
        {"back to two cycles", 0xd49f98b49f2c54a2},
        {"played to the end", 0xce22d9b9778be2d4},
        {"opened again", 0x7b32fa20ee884e83},
    });
}

TEST_CASE("folder bytes: a Swiss stopped by a crash before its next round was paired, the byes (#113)") {
    const ScratchDir root("bytes-swiss");
    const auto dir      = root / "Swiss";
    TournamentSpec spec = fcg::test::SpecBuilder(Format::Swiss).name("Swiss").players(7, "S");
    spec.rounds         = 4;

    Steps steps{"swiss"};
    Player player;
    auto state      = TournamentState::create(dir, spec);
    const auto mark = [&](const std::string& label) {
        steps.mark(label, folderText(dir) + stateText(*state) + replayText(dir) + incidentsText(root));
    };
    mark("created");

    // The first round, its last game cut from the next round's pairing by a crash.
    for (;;) {
        const auto rows = state->scheduleRows().size();
        REQUIRE(player.play(*state, 1) == 1);
        if (state->scheduleRows().size() > rows) break;
    }
    mark("first round");
    state.reset();
    cutAfterLastGame(dir);
    state = TournamentState::open(dir);
    mark("resumed, the second round paired");

    player.play(*state, 5);
    mark("5 more games");
    setLastResult(*state, "adjudicated by the arbiter");
    mark("a result set");
    player.play(*state);
    mark("played to the end");
    state.reset();
    state = TournamentState::open(dir);
    mark("opened again");

    steps.holdTo({
        {"created", 0x7b6e17757caea3c1},
        {"first round", 0xd5de744de64a9c80},
        {"resumed, the second round paired", 0x826b88fdc65593c5},
        {"5 more games", 0x911dcf3aef31a5a7},
        {"a result set", 0xbcd4c0509cd7dcce},
        {"played to the end", 0x84694d0d4962574f},
        {"opened again", 0x51dd897f1d566497},
    });
}

TEST_CASE("folder bytes: a knockout with tiebreaks, the PGN file of the user's in the ChessBase style, cut (#113)") {
    const ScratchDir root("bytes-knockout");
    const auto dir         = root / "Knockout";
    const auto out         = root / "all.pgn";
    TournamentSpec spec    = fcg::test::SpecBuilder(Format::Knockout).name("Knockout").players(6, "K");
    spec.pgn_out           = out.string();
    spec.pgn_out_chessbase = true;

    Steps steps{"knockout"};
    Player player;
    auto state      = TournamentState::create(dir, spec);
    const auto mark = [&](const std::string& label) {
        steps.mark(label, folderText(dir) + fileText(out) + stateText(*state) + replayText(dir) + incidentsText(root));
    };
    // The first games of a match drawn for every other pair, the tiebreaks won by the first player.
    const auto playKnockout = [&](int count) {
        for (int n = 0; n < count; ++n) {
            const auto task = state->claimNext();
            if (!task) return;
            const bool tiebreak = task->encounter.cycle > 1;
            const char* result  = tiebreak                         ? (task->game == 0 ? "1-0" : "0-1")
                                  : task->encounter.index % 2 == 0 ? "1/2-1/2"
                                                                   : (task->game == 0 ? "0-1" : "1-0");
            state->complete(*task, gameOf(*task, result, MatchTermination::NORMAL));
        }
    };
    mark("created");
    playKnockout(3);
    mark("3 games");

    // The write of the next game to the user's file stops after 40 bytes (a disk full); the next one completes it.
    appendSharedCut = [](std::size_t) -> std::size_t { return 40; };
    playKnockout(1);
    appendSharedCut = nullptr;
    CHECK_FALSE(state->pgnOutError().empty());
    mark("a write cut short");
    playKnockout(1);
    CHECK(state->pgnOutError().empty());
    mark("the write completed");

    // A write ended by an exception none expected, where it journals its start (#101): the next one is not held back.
    struct Unexpected {};
    state->setJournalListener([](uint64_t, uint64_t, const nlohmann::json& event) {
        if (event.value("t", std::string{}) == "pgn_out" && event.contains("begun")) throw Unexpected{};
    });
    {
        const auto task = state->claimNext();
        REQUIRE(task);
        CHECK_THROWS_AS(state->complete(*task, gameOf(*task, "1-0", MatchTermination::NORMAL)), Unexpected);
    }
    state->setJournalListener({});
    mark("a write ended by an exception");
    playKnockout(3);
    mark("3 more games");

    setLastResult(*state, "the arbiter's decision");
    mark("a result set in the round played");
    playKnockout(1000);
    mark("played to the end");
    state.reset();
    state = TournamentState::open(dir);
    mark("opened again");

    steps.holdTo({
        {"created", 0xca6acc6260fb1526},
        {"3 games", 0x63a607c4008c0d80},
        {"a write cut short", 0x96a9545aff9a9d14},
        {"the write completed", 0x31882487d1cd8ec1},
        {"a write ended by an exception", 0x5b94a2e2976cf2fb},
        {"3 more games", 0xcef8d284aa5adee9},
        {"a result set in the round played", 0x910c3241a1b256a3},
        {"played to the end", 0x2714ac78a47f454d},
        {"opened again", 0x0204d457b14399bc},
    });
}

namespace {

// The match below: its folder (the tournament's folder "Match", the PGN file of the user's all.pgn, the book book.epd).
struct MatchFolder {
    fs::path root;

    [[nodiscard]] fs::path dir() const { return root / "Match"; }
    [[nodiscard]] fs::path out() const { return root / "all.pgn"; }

    std::string text(const TournamentState& state) const {
        return folderText(dir()) + fileText(out()) + stateText(state) + replayText(dir()) + incidentsText(root);
    }

    // A wins (it has White in every game), but every fourth game is drawn; `edge`: White's advantage in the scores.
    // `played` counts the games.
    static void play(TournamentState& state, int& played, int count, int edge) {
        for (int n = 0; n < count; ++n) {
            const auto task = state.claimNext();
            if (!task) return;
            state.complete(*task, gameOf(*task, played++ % 4 == 3 ? "1/2-1/2" : "1-0", MatchTermination::NORMAL, edge));
        }
    }

    // From the crash in the middle of a write to the user's file on: resumed (the write completed), played until the
    // SPRT decides, opened again. `played`: the games played before.
    void finish(Steps& steps, int played) const {
        auto state = TournamentState::open(dir());
        steps.mark("resumed, the write completed", text(*state));
        play(*state, played, 1000, 30);
        CHECK(state->finishReason() == "sprt_h1");
        steps.mark("decided by the SPRT", text(*state));
        state.reset();
        state = TournamentState::open(dir());
        steps.mark("opened again", text(*state));
    }
};

// The hashes of the match below, those of the code of b602c425.
const std::vector<std::pair<std::string, uint64_t>> MATCH_BEFORE{
    {"created", 0x3904df7cc9f3e3e6},
    {"a pair", 0xf7ec15a8bcd7d6f1},
    {"a pair from an unbalanced opening, played again", 0xf2f5313a668a3fed},
    {"a pair voided for a broken connection", 0x9557dd07c66646f1},
    {"3 more games", 0xe0061b9771783ca3},
    {"resumed, the write completed", 0x06a28ec66d243b9b},
    {"decided by the SPRT", 0xe3a19d0d913f9e19},
    {"opened again", 0xa9da682a98b5e743},
};

// The games of the match played before its crash.
constexpr int MATCH_PLAYED = 9;

}  // namespace

TEST_CASE(
    "folder bytes: a match with locked colours, unbalanced openings replaced, the SPRT, a crash in a write (#113)") {
    const ScratchDir root("bytes-match");
    const MatchFolder match{root};
    TournamentSpec spec          = fcg::test::SpecBuilder(Format::Match).name("Match").players(2);
    spec.colours                 = Colours::AWhite;
    spec.pgn_out                 = match.out().string();
    spec.pgn_out_chessbase       = true;
    spec.unbalanced_cp           = 200;
    spec.unbalanced_moves        = 2;
    spec.unbalanced_replace      = true;
    spec.conditions.sprt.enabled = true;
    spec.conditions.sprt.elo0    = 0;
    spec.conditions.sprt.elo1    = 200;
    spec.conditions.sprt.alpha   = 0.1;
    spec.conditions.sprt.beta    = 0.1;
    spec.conditions.sprt.model   = "logistic";
    useBook(spec, root / "book.epd");

    Steps steps{"match"};
    int played = 0;
    {
        auto state      = TournamentState::create(match.dir(), spec);
        const auto mark = [&](const std::string& label) { steps.mark(label, match.text(*state)); };
        mark("created");
        MatchFolder::play(*state, played, 2, 30);
        mark("a pair");
        MatchFolder::play(*state, played, 2, 300);
        mark("a pair from an unbalanced opening, played again");
        MatchFolder::play(*state, played, 2, 30);
        CHECK(state->voidCutGames({0, 1}, "box"));
        mark("a pair voided for a broken connection");
        MatchFolder::play(*state, played, 3, 30);
        mark("3 more games");
    }
    REQUIRE(played == MATCH_PLAYED);

    // A crash in the middle of the last write to the user's file: its start journaled, not its end, its text cut.
    {
        const auto events = Journal::read(match.dir() / "journal.jsonl");
        REQUIRE(events.back().value("t", "") == "pgn_out");
        REQUIRE_FALSE(events.back().contains("begun"));
        const auto& begun = events[events.size() - 2];
        REQUIRE(begun.contains("begun"));
        const auto text = readFile(match.dir() / "journal.jsonl");
        fs::resize_file(match.dir() / "journal.jsonl", text.rfind('\n', text.size() - 2) + 1);
        fs::resize_file(match.out(), begun.at("at").get<uint64_t>() + begun.at("len").get<uint64_t>() / 2);
    }
    // With FCG_TOUR_BYTES_KEEP set to a folder, the match is copied there as the crash left it: the test below
    // resumes the copy the code of b602c425 made.
    if (const char* keep = std::getenv("FCG_TOUR_BYTES_KEEP")) {
        for (const auto& entry : fs::recursive_directory_iterator(root.path())) {
            if (entry.path().filename() == "tournament.lock") continue;
            const auto to = fs::path(keep) / fs::relative(entry.path(), root);
            if (entry.is_directory()) {
                fs::create_directories(to);
            } else {
                fs::copy_file(entry.path(), to, fs::copy_options::overwrite_existing);
            }
        }
    }
    match.finish(steps, played);

    steps.holdTo(MATCH_BEFORE);
}

TEST_CASE("folder bytes: the match the code of b602c425 left cut by a crash, resumed as it resumed it (#113)") {
    // The folder of the match above as that code left it at its crash (tournament/tests/data/written-by-b602c425,
    // its tournament.json with the paths of the folder it was made in), its book and user's file the copy's.
    const ScratchDir root("bytes-match-b602c425");
    fs::copy(fcg::test::sourcePath() / "tournament" / "tests" / "data" / "written-by-b602c425", root,
             fs::copy_options::recursive);
    const MatchFolder match{root};
    auto spec    = nlohmann::json::parse(readFile(match.dir() / "tournament.json")).get<TournamentSpec>();
    spec.pgn_out = match.out().string();
    spec.conditions.opening.file = (root / "book.epd").string();
    replaceFile(match.dir() / "tournament.json", nlohmann::json(spec).dump(4) + "\n");

    Steps steps{"match-b602c425"};
    match.finish(steps, MATCH_PLAYED);
    REQUIRE(MATCH_BEFORE.size() >= 3);
    steps.holdTo({MATCH_BEFORE.end() - 3, MATCH_BEFORE.end()});
}

TEST_CASE("folder bytes: a team match, the teams and the ratings in the tags, resumed (#113)") {
    const ScratchDir root("bytes-team");
    const auto dir      = root / "Teams";
    TournamentSpec spec = fcg::test::SpecBuilder(Format::TeamMatch).name("Teams").players(4, "T").cycles(2);
    for (auto& p : spec.participants) {
        p.team = p.id % 2;
        p.elo  = 2500 + 10 * p.id;
    }
    spec.team_names = {"North", "South"};

    Steps steps{"team"};
    Player player;
    auto state      = TournamentState::create(dir, spec);
    const auto mark = [&](const std::string& label) {
        steps.mark(label, folderText(dir) + stateText(*state) + replayText(dir) + incidentsText(root));
    };
    mark("created");
    player.play(*state, 3);
    mark("3 games");
    state.reset();
    state = TournamentState::open(dir);
    mark("resumed");
    player.play(*state);
    mark("played to the end");

    steps.holdTo({
        {"created", 0xd716c600b82f46a0},
        {"3 games", 0xc26cb1e4b984f367},
        {"resumed", 0x8cbef54bd2f810b9},
        {"played to the end", 0xcb4977b1ac0c3e63},
    });
}
