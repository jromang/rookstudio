// fcg-study-bench: the study store measured on real files, which are large and not all ours to share: never in the
// CI. Its results go in docs/design/study.md.
//
//   fcg-study-bench import <games> <database> [--threads N] [--commit N] [--keep-duplicates] [--quiet]
//   fcg-study-bench query <database> [--threads N]
//   fcg-study-bench decode <database> [--threads N]
//   fcg-study-bench crash <games> <database> [--kills N]          (not on Windows)
//   fcg-study-bench tree <database> [--threads N] [--plies N] [--memory MB]
//   fcg-study-bench search <database> [--threads N]
//   fcg-study-bench repertoire <database> <repertoire.pgn> white|black   (the database's tree built before)
//   fcg-study-bench chessbase <file> [--quiet]   (a ChessBase database or archive, every game read, nothing written)
//
// <games>: PGN, .pgn.zst, .pgn.gz or .zip. A database that does not exist is made.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <chess.hpp>

#include <study/chessbase.hpp>
#include <study/import.hpp>
#include <study/pgn_reader.hpp>
#include <study/query.hpp>
#include <study/repertoire.hpp>
#include <study/scan_board.hpp>
#include <study/search.hpp>
#include <study/store.hpp>
#include <study/tree.hpp>

#ifndef _WIN32
#    include <signal.h>
#    include <spawn.h>
#    include <sys/resource.h>
#    include <sys/wait.h>
#    include <unistd.h>
extern char** environ;
#endif

namespace fs = std::filesystem;
using namespace fcg::study;
using Clock = std::chrono::steady_clock;

namespace {

double seconds(Clock::time_point from, Clock::time_point to = Clock::now()) {
    return std::chrono::duration<double>(to - from).count();
}

double ms(Clock::time_point from, Clock::time_point to = Clock::now()) { return seconds(from, to) * 1000.0; }

// The most memory the process has held, in MB.
double peakMb() {
#ifndef _WIN32
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
#    ifdef __APPLE__
    return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);
#    else
    return static_cast<double>(usage.ru_maxrss) / 1024.0;
#    endif
#else
    return 0;
#endif
}

// The memory the process holds now, in MB: its own (the heap), and the pages of the files it maps, which the system
// takes back when it needs them (Linux only: 0 elsewhere).
std::pair<double, double> heldMb() {
    std::FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return {0, 0};
    char line[256];
    double anon = 0, file = 0;
    while (std::fgets(line, sizeof line, f)) {
        long kb = 0;
        if (std::sscanf(line, "RssAnon: %ld kB", &kb) == 1) anon = kb / 1024.0;
        if (std::sscanf(line, "RssFile: %ld kB", &kb) == 1) file = kb / 1024.0;
    }
    std::fclose(f);
    return {anon, file};
}

uint64_t folderBytes(const fs::path& dir) {
    uint64_t total = 0;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec)) total += entry.file_size(ec);
    }
    return total;
}

struct Args {
    std::vector<std::string> words;
    int threads          = 0;
    std::size_t commit   = std::size_t{1} << 18;
    bool keep_duplicates = false;
    bool quiet           = false;
    int kills            = 200;
    uint32_t plies       = TREE_PLIES;
    std::size_t memory   = 2048;  // MB
};

Args parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string_view word = argv[i];
        const auto value            = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", argv[i]);
                std::exit(2);
            }
            return argv[++i];
        };
        if (word == "--threads") {
            a.threads = std::stoi(value());
        } else if (word == "--commit") {
            a.commit = std::stoul(value());
        } else if (word == "--plies") {
            a.plies = static_cast<uint32_t>(std::stoul(value()));
        } else if (word == "--memory") {
            a.memory = std::stoul(value());
        } else if (word == "--kills") {
            a.kills = std::stoi(value());
        } else if (word == "--keep-duplicates") {
            a.keep_duplicates = true;
        } else if (word == "--quiet") {
            a.quiet = true;
        } else {
            a.words.emplace_back(word);
        }
    }
    return a;
}

std::shared_ptr<Database> openOrCreate(const fs::path& dir) {
    if (fs::exists(dir / "manifest.json")) return Database::open(dir);
    return Database::create(dir);
}

int runImport(const Args& a) {
    if (a.words.size() != 3) return 2;
    const fs::path file = a.words[1], dir = a.words[2];
    auto db           = openOrCreate(dir);
    const auto before = db->snapshot()->games();
    ImportOptions options;
    options.threads = a.threads;
    ImportProgress progress;
    std::atomic<bool> cancel{false}, done{false};
    const auto start = Clock::now();
    DatabaseImport result;
    std::thread worker([&] {
        result = importInto(*db, file, options, progress, cancel, a.keep_duplicates, a.commit);
        done   = true;
    });
    while (!done) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (a.quiet) continue;
        const double t = seconds(start);
        std::fprintf(stderr, "\r%6.1f s  %10llu games  %6.1f %%  %7.1f MB/s  ", t,
                     static_cast<unsigned long long>(progress.games.load()),
                     progress.total_bytes ? 100.0 * static_cast<double>(progress.bytes) / progress.total_bytes : 0.0,
                     static_cast<double>(progress.bytes) / 1e6 / std::max(t, 1e-9));
    }
    worker.join();
    const double t = seconds(start);
    if (a.quiet) return result.error.empty() && result.read.error.empty() ? 0 : 1;
    std::fprintf(stderr, "\n");
    const auto snap = db->snapshot();
    std::printf("import %s\n", file.string().c_str());
    std::printf("  threads           %d\n",
                options.threads ? options.threads : static_cast<int>(std::thread::hardware_concurrency()));
    std::printf("  time              %.1f s\n", t);
    std::printf("  file              %.1f MB, %.1f MB/s\n", progress.total_bytes / 1e6, progress.total_bytes / 1e6 / t);
    std::printf("  games read        %llu (%.0f games/s), damaged %llu\n",
                static_cast<unsigned long long>(result.read.games), result.read.games / t,
                static_cast<unsigned long long>(result.read.damaged));
    std::printf("  added             %llu, duplicates %llu, commits %llu\n",
                static_cast<unsigned long long>(result.added), static_cast<unsigned long long>(result.duplicates),
                static_cast<unsigned long long>(result.commits));
    std::printf("  database          %llu games (%llu before), %.1f MB on disk, %u names\n",
                static_cast<unsigned long long>(snap->games()), static_cast<unsigned long long>(before),
                folderBytes(dir) / 1e6, snap->names());
    std::printf("  peak memory       %.0f MB\n", peakMb());
    if (!result.read.error.empty()) std::printf("  read error        %s\n", result.read.error.c_str());
    if (!result.error.empty()) std::printf("  disk error        %s\n", result.error.c_str());
    return result.error.empty() && result.read.error.empty() ? 0 : 1;
}

int runQuery(const Args& a) {
    if (a.words.size() != 2) return 2;
    const fs::path dir = a.words[1];
    const auto start   = Clock::now();
    auto db            = Database::open(dir);
    const auto snap    = db->snapshot();
    const double open  = ms(start);
    const auto t_view  = Clock::now();
    const auto all     = makeView(snap, Filter{}, SortBy::Number, false, a.threads);
    const double view  = ms(t_view);
    const auto t_rows  = Clock::now();
    std::size_t chars  = 0;
    for (std::size_t i = 0; i < std::min<std::size_t>(50, all.ids.size()); ++i) {
        const auto row = snap->row(all.ids[i]);
        chars += row.white.size() + row.black.size() + row.event.size() + row.site.size();
    }
    const double rows = ms(t_rows);
    std::printf("query %s: %llu games, %u names\n", dir.string().c_str(),
                static_cast<unsigned long long>(snap->games()), snap->names());
    std::printf("  open              %8.1f ms\n", open);
    std::printf("  view of all       %8.1f ms\n", view);
    std::printf("  50 rows read      %8.3f ms (%zu chars)\n", rows, chars);
    std::printf("  first rows        %8.1f ms (open, view, rows)\n", ms(start));
    double most_heap = heldMb().first;
    const auto note  = [&] { most_heap = std::max(most_heap, heldMb().first); };
    std::printf("  memory, the list  %8.0f MB own, %.0f MB of mapped files\n", heldMb().first, heldMb().second);

    const std::vector<std::pair<const char*, SortBy>> sorts{
        {"White", SortBy::White},       {"Black", SortBy::Black}, {"Event", SortBy::Event}, {"Date", SortBy::Date},
        {"WhiteElo", SortBy::WhiteElo}, {"Eco", SortBy::Eco},     {"Plies", SortBy::Plies}, {"Result", SortBy::Result}};
    for (const auto& [name, sort] : sorts) {
        for (const bool descending : {false, true}) {
            const auto t0  = Clock::now();
            const auto got = makeView(snap, Filter{}, sort, descending, a.threads);
            std::printf("  sort %-8s %-4s %8.1f ms\n", name, descending ? "desc" : "asc", ms(t0));
            note();
            (void)got;
        }
    }

    std::vector<std::pair<std::string, Filter>> filters;
    Filter f;
    f.words = "carlsen";
    filters.emplace_back("words carlsen", f);
    f       = {};
    f.white = "kasparov";
    filters.emplace_back("white kasparov", f);
    f        = {};
    f.player = "polgar judit";
    filters.emplace_back("player polgar judit", f);
    f           = {};
    f.date_from = packDate("2000.01.01");
    f.date_to   = packDate("2009.12.31");
    filters.emplace_back("dates 2000-2009", f);
    f         = {};
    f.elo_min = 2600;
    filters.emplace_back("both 2600+", f);
    f          = {};
    f.eco_from = packEco("B90");
    f.eco_to   = packEco("B99");
    filters.emplace_back("ECO B90-B99", f);
    f           = {};
    f.annotated = true;
    filters.emplace_back("annotated", f);
    f         = {};
    f.words   = "carlsen";
    f.elo_min = 2700;
    f.result  = '1';
    filters.emplace_back("carlsen, 2700+, 1-0", f);
    for (const auto& [name, filter] : filters) {
        const auto t0     = Clock::now();
        const auto got    = makeView(snap, filter, SortBy::Number, false, a.threads);
        const double t    = ms(t0);
        const auto t1     = Clock::now();
        const auto sorted = makeView(snap, filter, SortBy::Date, true, a.threads);
        std::printf("  filter %-24s %8.1f ms, %9zu games; by date %8.1f ms\n", name.c_str(), t, got.ids.size(), ms(t1));
        (void)sorted;
    }
    const auto [own, mapped] = heldMb();
    std::printf("  memory, at the end %7.0f MB own (at most %.0f MB after a sort), %.0f MB of mapped files\n", own,
                most_heap, mapped);
    std::printf("  peak memory       %.0f MB, mapped files included\n", peakMb());
    return 0;
}

// Plies replayed from their codes, from the game's start: the plies, or -1 when a code is not a legal move's.
long long replay(const Snapshot& snap, GameId id, const ScanBoard& start) {
    const auto game     = snap.gameMoves(id);
    const bool chess960 = (game.flags & GAME_CHESS960) != 0;
    ScanBoard board     = start;
    if (game.flags & GAME_FEN) {
        if (!board.setFen(snap.game(id).start_fen, chess960)) return -1;
    } else if (chess960 && !board.setFen(chess::constants::STARTPOS, true)) {
        return -1;
    }
    const auto* p   = game.codes.data();
    const auto* end = p + game.codes.size();
    for (uint32_t ply = 0; ply < game.plies; ++ply) {
        const int count = board.codeCount();
        if (p == end) return -1;
        int code = *p++;
        if (count > 256) {
            if (p == end) return -1;
            code = (code << 8) | *p++;
        }
        if (code >= count) return -1;
        board.play(board.decode(code));
    }
    return p == end ? game.plies : -1;
}

// Every game replayed on `threads` threads: the plies and the games that failed.
std::pair<uint64_t, uint64_t> replayAll(const Snapshot& snap, int threads) {
    std::atomic<uint64_t> plies{0}, bad{0};
    const uint64_t n = snap.games();
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([&, t] {
            uint64_t mine = 0, wrong = 0;
            ScanBoard start;
            start.setFen(chess::constants::STARTPOS, false);
            for (GameId id = n * t / threads; id < n * (t + 1) / threads; ++id) {
                const auto got = replay(snap, id, start);
                if (got < 0)
                    ++wrong;
                else
                    mine += static_cast<uint64_t>(got);
            }
            plies += mine;
            bad += wrong;
        });
    }
    for (auto& t : pool) t.join();
    return {plies, bad};
}

// A ChessBase database read through, each game as a GameTree: how many, how fast, what they left out, the first
// problems met.
int runChessBase(const Args& a) {
    if (a.words.size() < 2) return 2;
    const auto start = std::chrono::steady_clock::now();
    ChessBaseDatabase db{fs::path(std::string(a.words[1]))};
    std::printf("%s: %s, %u records\n", std::string(a.words[1]).c_str(),
                db.format() == ChessBaseFormat::CBH ? "CBH" : "2CBH", db.records());
    uint64_t games = 0, damaged = 0, plies = 0, variations = 0;
    std::vector<std::string> problems;
    auto last = start;
    db.forEach([&](ChessBaseGame&& g) {
        ++games;
        plies += g.tree.main.moves.size();
        variations += g.tree.variations.size();
        if (g.damaged) {
            ++damaged;
            if (problems.size() < 20) problems.push_back(std::to_string(g.record) + ": " + g.problem);
        }
        const auto now = std::chrono::steady_clock::now();
        if (!a.quiet && now - last > std::chrono::seconds(10)) {
            std::printf("  %llu games\n", static_cast<unsigned long long>(games));
            std::fflush(stdout);
            last = now;
        }
        return true;
    });
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto& r        = db.report();
    std::printf("games %llu, damaged %llu, plies %llu, variations %llu, %.1f s (%.1f us a game)\n",
                static_cast<unsigned long long>(games), static_cast<unsigned long long>(damaged),
                static_cast<unsigned long long>(plies), static_cast<unsigned long long>(variations), seconds,
                games ? 1e6 * seconds / static_cast<double>(games) : 0.0);
    std::printf("report: records %llu games %llu texts %llu deleted %llu unknown %llu damaged %llu unreadable %llu\n",
                static_cast<unsigned long long>(r.records), static_cast<unsigned long long>(r.games),
                static_cast<unsigned long long>(r.texts), static_cast<unsigned long long>(r.deleted),
                static_cast<unsigned long long>(r.unknown), static_cast<unsigned long long>(r.damaged),
                static_cast<unsigned long long>(r.unreadable));
    std::printf(
        "left out: training %llu pictures %llu sounds %llu videos %llu links %llu quotations %llu pawns %llu "
        "paths %llu colours %llu correspondence %llu undecoded %llu other %llu\n",
        static_cast<unsigned long long>(r.training), static_cast<unsigned long long>(r.pictures),
        static_cast<unsigned long long>(r.sounds), static_cast<unsigned long long>(r.videos),
        static_cast<unsigned long long>(r.web_links), static_cast<unsigned long long>(r.quotations),
        static_cast<unsigned long long>(r.pawn_structures), static_cast<unsigned long long>(r.piece_paths),
        static_cast<unsigned long long>(r.variation_colours), static_cast<unsigned long long>(r.correspondence),
        static_cast<unsigned long long>(r.undecoded), static_cast<unsigned long long>(r.other));
    for (const auto& p : problems) std::printf("  problem %s\n", p.c_str());
    for (std::size_t i = 0; i < r.problems.size() && i < 20; ++i) std::printf("  report %s\n", r.problems[i].c_str());
    return 0;
}

int runDecode(const Args& a) {
    if (a.words.size() != 2) return 2;
    auto db          = Database::open(a.words[1]);
    const auto snap  = db->snapshot();
    const int all    = a.threads ? a.threads : static_cast<int>(std::thread::hardware_concurrency());
    const auto t0    = Clock::now();
    const auto one   = replayAll(*snap, 1);
    const double t1s = seconds(t0);
    const auto t1    = Clock::now();
    const auto many  = replayAll(*snap, all);
    const double tns = seconds(t1);
    std::printf("decode %s: %llu games\n", a.words[1].c_str(), static_cast<unsigned long long>(snap->games()));
    std::printf("  one core          %.2f s, %.1f M plies/s (%llu plies, %llu games failed)\n", t1s,
                one.first / 1e6 / t1s, static_cast<unsigned long long>(one.first),
                static_cast<unsigned long long>(one.second));
    std::printf("  %2d cores          %.2f s, %.1f M plies/s\n", all, tns, many.first / 1e6 / tns);
    return one.second == 0 ? 0 : 1;
}

#ifndef _WIN32
// The database checked as a reader finds it after a crash: it opens, has at least `at_least` games, and every game's
// moves replay.
bool sound(const fs::path& dir, uint64_t at_least, uint64_t& games, std::string& why) {
    if (!fs::exists(dir / "manifest.json")) {
        // Killed while it made the database: nothing there but what Database::create clears.
        games = 0;
        std::error_code ec;  // killed before the folder was made: none
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            const auto name = entry.path().filename().string();
            if (name != "tournament.lock" && name.find(".tmp") == std::string::npos) {
                why = "no manifest, and " + name;
                return false;
            }
        }
        if (at_least > 0) why = "the manifest of a database of " + std::to_string(at_least) + " games is gone";
        return at_least == 0;
    }
    try {
        auto db         = Database::open(dir);
        const auto snap = db->snapshot();
        games           = snap->games();
        if (games < at_least) {
            why = "games went from " + std::to_string(at_least) + " to " + std::to_string(games);
            return false;
        }
        const auto [plies, bad] = replayAll(*snap, 8);
        if (bad) {
            why = std::to_string(bad) + " games do not replay";
            return false;
        }
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.path().filename().string().find(".tmp") != std::string::npos) {
                why = "a leftover: " + entry.path().filename().string();
                return false;
            }
        }
        (void)plies;
        return true;
    } catch (const std::exception& e) {
        why = e.what();
        return false;
    }
}

pid_t spawnImport(const char* self, const fs::path& file, const fs::path& dir) {
    const std::string f = file.string(), d = dir.string();
    std::vector<const char*> argv{self, "import", f.c_str(), d.c_str(), "--commit", "5000", "--quiet", nullptr};
    pid_t pid = 0;
    if (posix_spawn(&pid, self, nullptr, nullptr, const_cast<char* const*>(argv.data()), environ) != 0) return -1;
    return pid;
}

int runCrash(const Args& a, const char* self) {
    if (a.words.size() != 3) return 2;
    const fs::path file = a.words[1], dir = a.words[2];
    if (fs::exists(dir)) {
        std::fprintf(stderr, "%s exists: the crash test makes its own database\n", dir.string().c_str());
        return 2;
    }
    // How long a whole import takes, and how many games it makes, in a database of its own.
    const auto reference = fs::path(dir.string() + ".reference");
    fs::remove_all(reference);
    const auto t0     = Clock::now();
    const pid_t whole = spawnImport(self, file, reference);
    int status        = 0;
    waitpid(whole, &status, 0);
    const double full_ms = ms(t0);
    uint64_t expected    = 0;
    std::string why;
    if (!sound(reference, 0, expected, why)) {
        std::fprintf(stderr, "the reference import failed: %s\n", why.c_str());
        return 1;
    }
    std::printf("crash %s: a whole import takes %.0f ms and makes %llu games\n", file.string().c_str(), full_ms,
                static_cast<unsigned long long>(expected));
    std::mt19937 random(12345);
    std::uniform_real_distribution<double> when(0.0, full_ms * 1.1);
    uint64_t games = 0;
    int finished   = 0;
    for (int k = 1; k <= a.kills; ++k) {
        const pid_t pid = spawnImport(self, file, dir);
        if (pid < 0) {
            std::fprintf(stderr, "cannot start an import\n");
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(static_cast<long long>(when(random) * 1000)));
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) ++finished;  // it was done before the kill
        uint64_t now = 0;
        if (!sound(dir, games, now, why)) {
            std::printf("  kill %d: the database is not sound: %s\n", k, why.c_str());
            return 1;
        }
        games = now;
        if (k % 50 == 0 || k == a.kills) {
            std::printf("  %4d kills: %llu games, sound (%d imports had finished)\n", k,
                        static_cast<unsigned long long>(games), finished);
            std::fflush(stdout);
        }
        if (games == expected) {
            // Whole: started again, to kill it at other points.
            fs::remove_all(dir);
            games = 0;
        }
    }
    // Finished without a kill, it has every game.
    const pid_t last = spawnImport(self, file, dir);
    waitpid(last, &status, 0);
    if (!sound(dir, games, games, why) || games != expected) {
        std::printf("  the last import, not killed, ends with %llu games of %llu: %s\n",
                    static_cast<unsigned long long>(games), static_cast<unsigned long long>(expected), why.c_str());
        return 1;
    }
    std::printf("  then an import not killed: %llu games, as many as the reference\n",
                static_cast<unsigned long long>(games));
    fs::remove_all(reference);
    return 0;
}
#endif

// Latencies, in ms: their median, 99th percentile and largest.
std::string spread(std::vector<double> v) {
    if (v.empty()) return "none";
    std::sort(v.begin(), v.end());
    char text[96];
    std::snprintf(text, sizeof text, "median %.3f, p99 %.3f, max %.3f ms", v[v.size() / 2], v[v.size() * 99 / 100],
                  v.back());
    return text;
}

int runTree(const Args& a) {
    if (a.words.size() != 2) return 2;
    auto db         = Database::open(a.words[1]);
    const auto snap = db->snapshot();
    TreeOptions options;
    options.threads = a.threads;
    options.plies   = a.plies;
    options.memory  = a.memory << 20;
    TreeProgress progress;
    std::atomic<bool> cancel{false};
    const auto start  = Clock::now();
    const auto built  = buildTree(*db, *snap, options, progress, cancel);
    const double took = seconds(start);
    if (!built.error.empty()) {
        std::fprintf(stderr, "tree: %s\n", built.error.c_str());
        return 1;
    }
    const auto tree = Tree::open(db->folder());
    uint64_t bytes  = 0;
    for (const auto& entry : fs::directory_iterator(db->folder() / tree->folderName())) bytes += entry.file_size();
    std::printf(
        "tree %s: %llu games, %u plies: %llu positions in %.1f s (%u passes, %.0f MB at most), %.0f MB on disk\n",
        a.words[1].c_str(), static_cast<unsigned long long>(built.games), tree->plies(),
        static_cast<unsigned long long>(built.positions), took, built.passes, peakMb(), bytes / 1e6);

    // The main line: from the start, the most played move each time; the first walk cold, the second warm.
    for (const char* walk : {"first", "again"}) {
        ScanBoard board;
        board.setFen(chess::constants::STARTPOS, false);
        std::vector<double> times;
        std::string line;
        for (int ply = 0; ply < 40; ++ply) {
            const auto t      = Clock::now();
            const auto answer = tree->at(*snap, board.key());
            times.push_back(ms(t));
            if (!answer || answer->moves.empty() || answer->moves[0].code == TreeMove::END) break;
            if (ply < 12) line += std::to_string(answer->total.games) + " ";
            board.play(board.decode(answer->moves[0].code));
        }
        std::printf("main line (%s): %zu positions, %s; games along it: %s...\n", walk, times.size(),
                    spread(times).c_str(), line.c_str());
    }

    // Positions of random games, within their first 30 plies.
    std::mt19937_64 random(1);
    std::vector<uint64_t> keys;
    ScanBoard start_board;
    start_board.setFen(chess::constants::STARTPOS, false);
    while (keys.size() < 20000) {
        const GameId id = random() % snap->games();
        const auto game = snap->gameMoves(id);
        if (game.flags & (GAME_FEN | GAME_CHESS960)) continue;
        const uint32_t ply = static_cast<uint32_t>(random() % 31);
        if (ply > game.plies) continue;
        ScanBoard board = start_board;
        for (uint32_t i = 0; i < ply; ++i) board.play(board.decode(game.codes[i]));
        keys.push_back(board.key());
    }
    for (const char* pass : {"cold", "warm"}) {
        std::vector<double> times, kept;
        uint64_t found = 0, games = 0;
        for (const auto key : keys) {
            const auto t         = Clock::now();
            const auto answer    = tree->at(*snap, key);
            const double took_ms = ms(t);
            times.push_back(took_ms);
            if (answer) {
                ++found;
                games += answer->total.games;
                kept.push_back(took_ms);
            }
        }
        std::printf("random positions (%s): %zu, %llu kept (%.0f games each on average); all %s; kept %s\n", pass,
                    keys.size(), static_cast<unsigned long long>(found), found ? double(games) / found : 0.0,
                    spread(times).c_str(), spread(kept).c_str());
    }
    return 0;
}

// A search to its end, on every thread: its hits and its time in ms.
std::pair<std::size_t, double> timed(const std::shared_ptr<const Snapshot>& snap, const SearchQuery& query,
                                     int threads) {
    const auto t    = Clock::now();
    const auto hits = searchAll(snap, query, threads);
    return {hits.size(), ms(t)};
}

int runSearch(const Args& a) {
    if (a.words.size() != 2) return 2;
    auto db           = Database::open(a.words[1]);
    const auto snap   = db->snapshot();
    const int threads = a.threads > 0 ? a.threads : static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    std::printf("search %s: %llu games, %d threads\n", a.words[1].c_str(),
                static_cast<unsigned long long>(snap->games()), threads);
    ScanBoard start_board;
    start_board.setFen(chess::constants::STARTPOS, false);

    // Exact positions of random games, by how far into them.
    std::mt19937_64 random(7);
    for (const uint32_t ply : {8u, 16u, 24u, 40u, 60u, 90u}) {
        std::vector<double> times;
        std::size_t hits = 0;
        int asked        = 0;
        while (asked < 40) {
            const GameId id = random() % snap->games();
            const auto game = snap->gameMoves(id);
            if (game.flags & (GAME_FEN | GAME_CHESS960) || game.plies < ply) continue;
            ScanBoard board = start_board;
            for (uint32_t i = 0; i < ply; ++i) board.play(board.decode(game.codes[i]));
            const auto [found, took] = timed(snap, PositionQuery{board}, threads);
            times.push_back(took);
            hits += found;
            ++asked;
        }
        std::printf("position after %u plies: %s; %.1f games found each\n", ply, spread(times).c_str(),
                    static_cast<double>(hits) / asked);
    }
    // One position replayed in every game, without the signatures: what they save.
    {
        const GameId id = 12345 % snap->games();
        const auto game = snap->gameMoves(id);
        ScanBoard board = start_board;
        for (uint32_t i = 0; i < std::min<uint32_t>(30, game.plies); ++i) board.play(board.decode(game.codes[i]));
        const SearchQuery query = PositionQuery{board};
        std::atomic<uint64_t> next{0}, found{0};
        const auto t = Clock::now();
        std::vector<std::thread> pool;
        for (int k = 0; k < threads; ++k)
            pool.emplace_back([&] {
                for (uint64_t g; (g = next++) < snap->games();)
                    if (searchGame(*snap, g, query, false)) ++found;
            });
        for (auto& th : pool) th.join();
        const double without = ms(t);
        const auto with      = timed(snap, query, threads);
        std::printf("a position without the signatures: %.0f ms (%llu games); with them %.1f ms (%zu games)\n", without,
                    static_cast<unsigned long long>(found.load()), with.second, with.first);
    }
    // Material.
    MaterialQuery rooks;  // rook endings: a rook each, pawns, nothing else
    for (const int side : {0, 5}) {
        rooks.high[side + KNIGHT] = rooks.high[side + BISHOP] = rooks.high[side + QUEEN] = 0;
        rooks.low[side + ROOK] = rooks.high[side + ROOK] = 1;
    }
    rooks.plies = 4;
    MaterialQuery queenless;  // queens off, for 10 plies
    queenless.high[QUEEN] = queenless.high[5 + QUEEN] = 0;
    queenless.plies                                   = 10;
    MaterialQuery bishops;  // the bishop pair against bishop and knight, no queens, 6 plies
    bishops.low[BISHOP] = bishops.high[BISHOP] = 2;
    bishops.high[KNIGHT]                       = 0;
    bishops.low[5 + BISHOP] = bishops.high[5 + BISHOP] = 1;
    bishops.low[5 + KNIGHT] = bishops.high[5 + KNIGHT] = 1;
    bishops.high[QUEEN] = bishops.high[5 + QUEEN] = 0;
    bishops.plies                                 = 6;
    for (const auto& [name, query] : {std::pair<const char*, MaterialQuery>{"rook endings", rooks},
                                      {"queens off", queenless},
                                      {"bishop pair against bishop and knight", bishops}}) {
        const auto [found, took] = timed(snap, query, threads);
        std::printf("material, %s: %zu games in %.0f ms\n", name, found, took);
    }
    // Patterns.
    PatternQuery castled;  // White castled short, the king's pawns unmoved
    castled.men[WHITE_SIDE][KING] = uint64_t{1} << 6;
    castled.men[WHITE_SIDE][ROOK] = uint64_t{1} << 5;
    castled.men[WHITE_SIDE][PAWN] = (uint64_t{1} << 13) | (uint64_t{1} << 14) | (uint64_t{1} << 15);
    PatternQuery maroczy;  // pawns on c4 and e4 against a pawn on d6, no white d-pawn on d4
    maroczy.men[WHITE_SIDE][PAWN] = (uint64_t{1} << 26) | (uint64_t{1} << 28);
    maroczy.men[BLACK_SIDE][PAWN] = uint64_t{1} << 43;
    maroczy.empty                 = uint64_t{1} << 27;
    PatternQuery corner;  // kings in opposite far corners: every game replayed to its end
    corner.men[WHITE_SIDE][KING] = uint64_t{1} << 63;
    corner.men[BLACK_SIDE][KING] = uint64_t{1} << 0;
    for (const auto& [name, query] : {std::pair<const char*, PatternQuery>{"castled short, pawns unmoved", castled},
                                      {"Maroczy bind", maroczy},
                                      {"kings in far corners (every game to its end)", corner}}) {
        const auto [found, took] = timed(snap, query, threads);
        std::printf("pattern, %s: %zu games in %.0f ms\n", name, found, took);
    }
    // A cancel: how long the threads take to stop.
    {
        Search search(snap, corner, threads);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const auto t = Clock::now();
        search.cancel();
        search.wait();
        std::printf("cancel: stopped in %.2f ms, %llu games searched by then\n", ms(t),
                    static_cast<unsigned long long>(search.searched()));
    }
    return 0;
}

// A repertoire's coverage of the database's games, from its tree: the time it takes, and its gaps.
int runRepertoire(const Args& a) {
    if (a.words.size() != 4 || (a.words[3] != "white" && a.words[3] != "black")) return 2;
    auto db         = Database::open(a.words[1]);
    const auto snap = db->snapshot();
    const auto tree = Tree::open(db->folder());
    if (!tree || tree->stale(*snap)) {
        std::fprintf(stderr, "repertoire: no tree for this database (fcg-study-bench tree)\n");
        return 1;
    }
    std::string text;
    {
        std::FILE* f = std::fopen(a.words[2].c_str(), "rb");
        if (!f) {
            std::fprintf(stderr, "repertoire: cannot read %s\n", a.words[2].c_str());
            return 1;
        }
        char buffer[65536];
        for (std::size_t n; (n = std::fread(buffer, 1, sizeof buffer, f)) > 0;) text.append(buffer, n);
        std::fclose(f);
    }
    const auto read = readPgnGame(text);
    if (!read.tree) {
        std::fprintf(stderr, "repertoire: no game in %s (%s)\n", a.words[2].c_str(), read.error.c_str());
        return 1;
    }
    const auto side = a.words[3] == "white" ? chess::Color::WHITE : chess::Color::BLACK;
    auto start      = Clock::now();
    const Repertoire rep(*read.tree, side);
    const double map = ms(start);
    const auto moves = treeMoves({{snap, tree}});
    for (const char* walk : {"first", "again"}) {
        start             = Clock::now();
        const auto cover  = coverage(rep, moves);
        const double took = ms(start);
        std::printf(
            "repertoire (%s): %zu positions (%.1f ms), %zu in the database; covered %.1f %% in %.1f ms; %zu gaps\n",
            walk, cover.positions, map, cover.known, 100 * cover.covered, took, cover.gaps.size());
        if (std::string_view(walk) == "first") continue;
        for (std::size_t i = 0; i < cover.gaps.size() && i < 15; ++i) {
            const auto& g = cover.gaps[i];
            std::string line;
            for (std::size_t p = 0; p < g.line.size(); ++p)
                line += (p % 2 == 0 ? std::to_string(p / 2 + 1) + "." : "") + g.line[p] + " ";
            std::printf("  %5.1f %% of the games, %8llu: %s%s\n", 100 * g.reach,
                        static_cast<unsigned long long>(g.games), line.c_str(),
                        g.move.empty() ? "(no move of ours)" : ("-> " + g.move).c_str());
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const auto a = parse(argc, argv);
    if (a.words.empty()) {
        std::fprintf(stderr, "usage: fcg-study-bench import|query|decode|crash ... (see study/bench/main.cpp)\n");
        return 2;
    }
    try {
        int rc = 2;
        if (a.words[0] == "import") rc = runImport(a);
        if (a.words[0] == "query") rc = runQuery(a);
        if (a.words[0] == "decode") rc = runDecode(a);
        if (a.words[0] == "tree") rc = runTree(a);
        if (a.words[0] == "search") rc = runSearch(a);
        if (a.words[0] == "repertoire") rc = runRepertoire(a);
        if (a.words[0] == "chessbase") rc = runChessBase(a);
#ifndef _WIN32
        if (a.words[0] == "crash") {
            // posix_spawn takes a path: the program's own, wherever it was started from.
            std::error_code ec;
            const auto self = fs::read_symlink("/proc/self/exe", ec);
            rc              = runCrash(a, ec ? argv[0] : self.c_str());
        }
#endif
        if (rc == 2)
            std::fprintf(stderr, "usage: fcg-study-bench import|query|decode|crash ... (see study/bench/main.cpp)\n");
        return rc;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fcg-study-bench: %s\n", e.what());
        return 1;
    }
}
