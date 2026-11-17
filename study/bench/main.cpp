// fcg-study-bench: the study store measured on real files, which are large and not all ours to share: never in the
// CI. Its results go in docs/design/study.md.
//
//   fcg-study-bench import <games> <database> [--threads N] [--commit N] [--keep-duplicates] [--quiet]
//   fcg-study-bench query <database> [--threads N]
//   fcg-study-bench decode <database> [--threads N]
//   fcg-study-bench crash <games> <database> [--kills N]          (not on Windows)
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

#include <study/import.hpp>
#include <study/query.hpp>
#include <study/scan_board.hpp>
#include <study/store.hpp>

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
