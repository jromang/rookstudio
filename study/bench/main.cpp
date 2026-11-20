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
//   fcg-study-bench chessbase-dump <file.2cbh> <record>...   (a few records' bytes in hex: header, moves, annotations)
//   fcg-study-bench chessbase-scan <file.2cbh>   (its kinds of records, how its games start, their castling words, the
//                                                 types of annotations met, those of a size not known, the records
//                                                 whose annotations do not read to their end: a few records of each
//                                                 named)
//   fcg-study-bench chessbase-item <file.2cbh> <type>|@<offset> <bytes> <record>...   (the first annotation of a type
//                                                 (hex) in each record, or its annotations from an offset: that many
//                                                 bytes in hex)
//   fcg-study-bench evals <lichess_db_eval.jsonl.zst> <folder> [--min-men N] [--run-entries N]
//                                                 (Lichess's evaluations database read whole: its positions, searches,
//                                                 depths and men counted, the offline index made in <folder> as the
//                                                 program makes it, its size, the lookups' speed cold and warm on the
//                                                 runs and on the index; a sample kept in <folder>/sample.tsv)
//   fcg-study-bench evals-lookup <folder>          (the lookups again, on the folder's index and its sample)
//   fcg-study-bench puzzles <lichess_db_puzzle.csv.zst> <folder>
//                                                 (Lichess's puzzle database read whole and kept as the program keeps
//                                                 it: the store's size, the time to make it, a puzzle in a thousand
//                                                 compared with its line, the picks and the reading of a puzzle, cold
//                                                 and warm)
//   fcg-study-bench puzzles-pick <folder>          (the picks and reads again, on the folder's store)
//
// <games>: PGN, .pgn.zst, .pgn.gz or .zip. A database that does not exist is made.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <chess.hpp>
#include <tour/chess_text.hpp>

#include <study/chessbase.hpp>
#include <study/evals_index.hpp>
#include <study/import.hpp>
#include <study/mapped_file.hpp>
#include <study/pgn_reader.hpp>
#include <study/puzzles.hpp>
#include <study/query.hpp>
#include <study/repertoire.hpp>
#include <study/scan_board.hpp>
#include <study/search.hpp>
#include <study/sources.hpp>
#include <study/store.hpp>
#include <study/tree.hpp>

#ifndef _WIN32
#    include <fcntl.h>
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
    int min_men             = 0;
    std::size_t run_entries = std::size_t{1} << 23;
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
        } else if (word == "--min-men") {
            a.min_men = std::stoi(value());
        } else if (word == "--run-entries") {
            a.run_entries = std::stoul(value());
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
    uint64_t games = 0, damaged = 0, plies = 0, variations = 0, undecoded = 0;
    std::vector<std::string> problems;
    std::vector<uint32_t> undecoded_in;  // the first games with notes that could not be read or placed
    auto last = start;
    db.forEach([&](ChessBaseGame&& g) {
        ++games;
        plies += g.tree.main.moves.size();
        variations += g.tree.variations.size();
        if (g.damaged) {
            ++damaged;
            if (problems.size() < 20) problems.push_back(std::to_string(g.record) + ": " + g.problem);
        }
        if (db.report().undecoded != undecoded) {
            undecoded = db.report().undecoded;
            if (undecoded_in.size() < 50) undecoded_in.push_back(g.record);
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
    if (!undecoded_in.empty()) {
        std::printf("  undecoded notes in games");
        for (const auto record : undecoded_in) std::printf(" %u", record);
        std::printf("\n");
    }
    for (std::size_t i = 0; i < r.problems.size() && i < 20; ++i) std::printf("  report %s\n", r.problems[i].c_str());
    return 0;
}

// The files of a 2CBH database, mapped, read raw: what its records hold, in hex.
struct TwoCbhFiles {
    explicit TwoCbhFiles(const fs::path& any) {
        auto stem = any;
        stem.replace_extension();
        const auto open = [&](const char* ext) {
            auto p = stem;
            p += ext;
            return MappedFile(p);
        };
        header = open(".2cbh");
        moves  = open(".2cbg");
        notes  = open(".2cba");
    }
    static uint64_t le(const uint8_t* p, int n) {
        uint64_t v = 0;
        for (int i = n - 1; i >= 0; --i) v = v << 8 | p[i];
        return v;
    }
    [[nodiscard]] uint32_t records() const { return static_cast<uint32_t>(header.size() / 192); }
    [[nodiscard]] const uint8_t* record(uint32_t id) const { return header.data() + uint64_t{id} * 192; }
    // A framed record's content: none when its frame is not one.
    static std::pair<const uint8_t*, std::size_t> content(const MappedFile& f, uint64_t at) {
        static const uint8_t magic[8] = {0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
        if (at < 12 || at + 24 > f.size() || !std::equal(magic, magic + 8, f.data() + at)) return {nullptr, 0};
        const uint64_t a = le(f.data() + at + 8, 4), b = le(f.data() + at + 12, 4);
        if (at + 34 + a + b > f.size()) return {nullptr, 0};
        return {f.data() + at + 24, static_cast<std::size_t>(a)};
    }
    MappedFile header, moves, notes;
};

void hexLine(const char* label, const uint8_t* p, std::size_t n) {
    std::printf("%s", label);
    for (std::size_t i = 0; i < n; ++i) std::printf("%s%02x", i % 2 == 0 ? " " : "", p[i]);
    std::printf("\n");
}

int runChessBaseDump(const Args& a) {
    if (a.words.size() < 3) return 2;
    const TwoCbhFiles db{fs::path(std::string(a.words[1]))};
    for (std::size_t w = 2; w < a.words.size(); ++w) {
        const auto id = static_cast<uint32_t>(std::stoul(std::string(a.words[w])));
        if (id >= db.records()) continue;
        const uint8_t* r = db.record(id);
        std::printf("record %u: kind %02x %02x %02x %02x\n", id, r[0], r[1], r[2], r[3]);
        for (int row = 0; row < 192; row += 32) {
            char label[16];
            std::snprintf(label, sizeof label, "  h%02x:", row);
            hexLine(label, r + row, 32);
        }
        const auto [m, mn] = TwoCbhFiles::content(db.moves, TwoCbhFiles::le(r + 8, 8));
        std::printf("  moves at %llu: %zu bytes\n", static_cast<unsigned long long>(TwoCbhFiles::le(r + 8, 8)), mn);
        if (m)
            for (std::size_t at = 0; at < std::min<std::size_t>(mn, 160); at += 32)
                hexLine("   ", m + at, std::min<std::size_t>(32, mn - at));
        const auto [n, nn] = TwoCbhFiles::content(db.notes, TwoCbhFiles::le(r + 16, 8));
        std::printf("  annotations at %llu: %zu bytes\n", static_cast<unsigned long long>(TwoCbhFiles::le(r + 16, 8)),
                    nn);
        if (n)
            for (std::size_t at = 0; at < std::min<std::size_t>(nn, 256); at += 32)
                hexLine("   ", n + at, std::min<std::size_t>(32, nn - at));
    }
    return 0;
}

// The size of an annotation's data, as the reader reads it (chessbase_2cbh.cpp): 0 for a type it does not know.
std::size_t twoCbhItemSize(uint32_t type, const uint8_t* p, std::size_t left) {
    switch (type) {
        case 0x02:
        case 0x82:
            return left >= 8 ? 8 + TwoCbhFiles::le(p + 4, 4) : 0;
        case 0x03:
            return 3;
        case 0x04:
        case 0x05:
            return left >= 4 ? 4 + TwoCbhFiles::le(p, 4) : 0;
        case 0x07:
            return 4;
        case 0x21:
            return 6;
        case 0x24:
            return 38;
        case 0x26:
            return left >= 5 ? 5 + TwoCbhFiles::le(p + 1, 4) : 0;
        case 0x16:
        case 0x17:
        case 0x22:
        case 0x23:
        case 0x25:
            return 4;
        case 0x18:
        case 0x14:
            return 1;
        case 0x08:
            return 4;
        case 0x15:
            return left >= 4 ? 4 + TwoCbhFiles::le(p, 4) : 0;
        case 0x09: {
            // 12 bytes, four lists, the answers: as trainingSize() in chessbase_2cbh.cpp.
            if (left < 12) return 0;
            std::size_t at  = 12;
            const auto list = [&] {
                if (at + 2 > left) return false;
                const auto count = TwoCbhFiles::le(p + at, 2);
                at += 2;
                for (uint64_t k = 0; k < count; ++k) {
                    if (at + 6 > left) return false;
                    at += 6 + TwoCbhFiles::le(p + at + 2, 4);
                }
                return true;
            };
            for (int k = 0; k < 4; ++k)
                if (!list()) return 0;
            // The answers. A move to find (kind 1): from, to, a byte, the points, a list. A choice (kind 2): the
            // points, its list, a list.
            if (at + 1 > left) return 0;
            const unsigned answers = p[at++];
            const auto kind        = TwoCbhFiles::le(p + 2, 4);
            for (unsigned i = 0; i < answers; ++i) {
                if (kind == 1) {
                    at += 4;
                    if (!list()) return 0;
                } else if (kind == 2) {
                    at += 1;
                    if (!list() || !list()) return 0;
                } else {
                    return 0;
                }
            }
            return at;
        }
        case 0x13: {
            // 10 bytes, six texts, 79 bytes, the two ratings' kinds, 26 bytes, a byte (0: the 64 bytes of a board and
            // 11 more follow), 2 bytes, the moves, 4 zero bytes: as quotationSize() in chessbase_2cbh.cpp.
            std::size_t at = 10;
            for (int i = 0; i < 6; ++i) {
                if (at + 1 > left) return 0;
                at += 1 + p[at];
            }
            at += 79;
            for (int i = 0; i < 2; ++i) {
                if (at + 9 > left) return 0;
                at += 9 + TwoCbhFiles::le(p + at + 5, 4);
            }
            at += 26;
            if (at + 1 > left) return 0;
            if (p[at] == 0) at += 75;
            at += 3;
            if (at + 4 > left) return 0;
            at += 4 + 5 * TwoCbhFiles::le(p + at, 4);
            if (at + 4 > left || TwoCbhFiles::le(p + at, 4) != 0) return 0;
            return at + 4;
        }
        case 0x20:
            return left >= 8 ? 8 + TwoCbhFiles::le(p + 4, 4) : 0;
        case 0x1c: {
            if (left < 5) return 0;
            const std::size_t first = 5 + TwoCbhFiles::le(p + 1, 4);
            return left >= first + 4 ? first + 4 + TwoCbhFiles::le(p + first, 4) : 0;
        }
        default:
            return 0;
    }
}

int runChessBaseItem(const Args& a) {
    if (a.words.size() < 5) return 2;
    const TwoCbhFiles db{fs::path(std::string(a.words[1]))};
    // A type in hex, or @offset for the bytes from there.
    const std::string what = std::string(a.words[2]);
    const bool at_offset   = what.starts_with("@");
    const auto want =
        static_cast<uint32_t>(std::stoul(at_offset ? what.substr(1) : what, nullptr, at_offset ? 10 : 16));
    const auto bytes = std::stoul(std::string(a.words[3]));
    for (std::size_t w = 4; w < a.words.size(); ++w) {
        const auto id = static_cast<uint32_t>(std::stoul(std::string(a.words[w])));
        if (id >= db.records()) continue;
        const auto [n, nn] = TwoCbhFiles::content(db.notes, TwoCbhFiles::le(db.record(id) + 16, 8));
        if (!n) continue;
        if (at_offset) {
            std::printf("record %u: bytes %u of %zu\n", id, want, nn);
            for (std::size_t i = want; i < std::min<std::size_t>(nn, want + bytes); i += 32)
                hexLine("   ", n + i, std::min<std::size_t>({32, nn - i, want + bytes - i}));
            continue;
        }
        std::size_t at = 2;
        bool found     = false;
        while (!found && at + 8 <= nn) {
            const auto position  = static_cast<int32_t>(TwoCbhFiles::le(n + at, 4));
            const uint64_t count = TwoCbhFiles::le(n + at + 4, 4);
            at += 8;
            for (uint64_t k = 0; k < count && at + 2 <= nn; ++k) {
                const auto type = static_cast<uint32_t>(TwoCbhFiles::le(n + at, 2));
                if (type == want) {
                    std::printf("record %u: type %x at %zu of %zu (position %d, item %llu of %llu)\n", id, type, at, nn,
                                position, static_cast<unsigned long long>(k + 1),
                                static_cast<unsigned long long>(count));
                    for (std::size_t i = at; i < std::min<std::size_t>(nn, at + bytes); i += 32)
                        hexLine("   ", n + i, std::min<std::size_t>({32, nn - i, at + bytes - i}));
                    found = true;
                    break;
                }
                const std::size_t size = twoCbhItemSize(type, n + at + 2, nn - at - 2);
                if (size == 0 || at + 2 + size > nn) {
                    at = nn;
                    break;
                }
                at += 2 + size;
            }
        }
        if (!found) std::printf("record %u: no type %x reached\n", id, want);
    }
    return 0;
}

int runChessBaseScan(const Args& a) {
    if (a.words.size() < 2) return 2;
    const TwoCbhFiles db{fs::path(std::string(a.words[1]))};
    struct Seen {
        uint64_t count = 0;
        std::vector<uint32_t> records;
        void add(uint32_t id) {
            ++count;
            if (records.size() < 8) records.push_back(id);
        }
    };
    std::map<uint32_t, Seen> kinds, types, stops, fails;
    std::map<std::string, Seen> starts, words;
    uint64_t clean = 0, unclean = 0;
    std::map<uint32_t, std::vector<std::string>> fail_samples, oracle;
    const auto itemSize = twoCbhItemSize;
    const auto hex      = [](const uint8_t* p, std::size_t n) {
        std::string out;
        for (std::size_t i = 0; i < n; ++i) {
            char b[4];
            std::snprintf(b, sizeof b, "%02x ", p[i]);
            out += b;
        }
        return out;
    };
    // Whether the annotations from `at` on parse to their end, `left` items of the current group first.
    const auto parsesFrom = [&](const uint8_t* n, std::size_t nn, std::size_t at, uint64_t left) {
        for (int groups = 0; groups < 100000; ++groups) {
            for (; left > 0; --left) {
                if (at + 2 > nn) return false;
                const auto type        = static_cast<uint32_t>(TwoCbhFiles::le(n + at, 2));
                const std::size_t size = itemSize(type, n + at + 2, nn - at - 2);
                if (size == 0 || at + 2 + size > nn) return false;
                at += 2 + size;
            }
            if (nn - at == 2 && TwoCbhFiles::le(n + at, 2) == 0xFFFF) return true;
            if (at + 8 > nn) return false;
            const auto position = static_cast<int32_t>(TwoCbhFiles::le(n + at, 4));
            left                = TwoCbhFiles::le(n + at + 4, 4);
            if (position < -1 || left == 0 || left > 64) return false;
            at += 8;
        }
        return false;
    };
    for (uint32_t id = 1; id < db.records(); ++id) {
        const uint8_t* r = db.record(id);
        kinds[r[0]].add(id);
        if ((r[0] & 0x83) != 1) continue;
        // How its moves start (the first word, then fffc, fffb and a set-up, or fffb, a number and fffc), and the
        // words of castling and above the table's moves they hold.
        const auto [m, mn] = TwoCbhFiles::content(db.moves, TwoCbhFiles::le(r + 8, 8));
        if (m && mn >= 4) {
            const auto word = [&](std::size_t i) {
                return i < mn / 2 ? static_cast<uint32_t>(TwoCbhFiles::le(m + 2 * i, 2)) : 0u;
            };
            const uint32_t first = word(0);
            std::string start    = "its moves";
            int64_t number       = -1;
            std::size_t i        = 2;
            if (word(1) == 0xFFFB && word(3) == 0xFFFC) {
                start  = word(2) < 960 ? "a number below 960" : "a number of 960 or more";
                number = word(2);
                i      = 4;
            } else if (word(1) == 0xFFFB) {
                start = "a set-up position";
                for (i = 5; i < mn / 2 && word(i) != 0xFFFC; ++i) {
                }
                ++i;
            }
            starts["first word " + std::to_string(first) + ", " + start].add(id);
            for (; i < mn / 2; ++i) {
                const uint32_t w = word(i);
                std::string what;
                if (w >= 0xB129 && w < 0xC02D) {
                    const uint32_t group = (w - 0xB129) / 4;
                    what = group == 0                           ? "castling of the standard group"
                           : number >= 0 && group == number + 1 ? "castling of its number's group (number + 1)"
                           : number >= 0 && group == number     ? "castling of its number's group (number)"
                                                                : "castling of another group";
                } else if (w >= 0xC02D && w < 0xFFFA) {
                    what = "a word between the castling and fffa";
                } else {
                    continue;
                }
                words["first word " + std::to_string(first) + ", " + start + ": " + what].add(id);
            }
        }
        const auto [n, nn] = TwoCbhFiles::content(db.notes, TwoCbhFiles::le(r + 16, 8));
        if (!n || nn < 2) continue;
        if (parsesFrom(n, nn, 2, 0)) {
            ++clean;
        } else {
            ++unclean;
            // Where it stops: the first item whose size is not known or runs past the record.
            std::size_t at     = 2;
            uint32_t where     = 0xFFFF;
            std::size_t offset = 0;
            for (int guard = 0; guard < 100000 && at + 8 <= nn && where == 0xFFFF; ++guard) {
                const uint64_t count = TwoCbhFiles::le(n + at + 4, 4);
                if (static_cast<int32_t>(TwoCbhFiles::le(n + at, 4)) < -1 || count == 0 || count > 64) {
                    where  = 0xFFFE;
                    offset = at;
                    break;
                }
                at += 8;
                for (uint64_t k = 0; k < count; ++k) {
                    const auto type        = at + 2 <= nn ? static_cast<uint32_t>(TwoCbhFiles::le(n + at, 2)) : 0xFFFD;
                    const std::size_t size = at + 2 <= nn ? itemSize(type, n + at + 2, nn - at - 2) : 0;
                    if (size == 0 || at + 2 + size > nn) {
                        where  = type;
                        offset = at;
                        break;
                    }
                    at += 2 + size;
                }
                if (nn - at == 2 && TwoCbhFiles::le(n + at, 2) == 0xFFFF) break;
            }
            fails[where].add(id);
            auto& samples = fail_samples[where];
            if (samples.size() < 6)
                samples.push_back("record " + std::to_string(id) + " +" + std::to_string(offset) + "/" +
                                  std::to_string(nn) + ": " + hex(n + offset, std::min<std::size_t>(nn - offset, 48)));
        }
        std::size_t at = 2;
        while (at + 8 <= nn) {
            const uint64_t count = TwoCbhFiles::le(n + at + 4, 4);
            at += 8;
            bool stop = false;
            for (uint64_t k = 0; k < count && at + 2 <= nn; ++k) {
                const auto type        = static_cast<uint32_t>(TwoCbhFiles::le(n + at, 2));
                const uint8_t* d       = n + at + 2;
                const std::size_t size = itemSize(type, d, nn - at - 2);
                types[type].add(id);
                if (size == 0 || at + 2 + size > nn) {
                    auto& found = oracle[type];
                    if (found.size() < 24) {
                        std::string line = "record " + std::to_string(id) + " data " +
                                           hex(d, std::min<std::size_t>(nn - at - 2, 28)) + "-> sizes";
                        int candidates   = 0;
                        for (std::size_t s = 0; at + 2 + s <= nn && s <= 8192 && candidates < 6; ++s)
                            if (parsesFrom(n, nn, at + 2 + s, count - k - 1)) {
                                line += " " + std::to_string(s);
                                ++candidates;
                            }
                        found.push_back(line);
                    }
                    stops[type].add(id);
                    stop = true;
                    break;
                }
                at += 2 + size;
            }
            if (stop) break;
            if (nn - at == 2 && TwoCbhFiles::le(n + at, 2) == 0xFFFF) break;
        }
    }
    const auto show = [](const char* what, const auto& map) {
        std::printf("%s:\n", what);
        for (const auto& [key, seen] : map) {
            if constexpr (std::is_same_v<std::decay_t<decltype(key)>, std::string>)
                std::printf("  %s: %llu (", key.c_str(), static_cast<unsigned long long>(seen.count));
            else
                std::printf("  %llx: %llu (", static_cast<unsigned long long>(key),
                            static_cast<unsigned long long>(seen.count));
            for (const auto id : seen.records) std::printf(" %u", id);
            std::printf(" )\n");
        }
    };
    show("record kinds", kinds);
    show("how games start", starts);
    show("castling words and words above them", words);
    show("annotation types met", types);
    show("annotation types of unknown size (where reading stops)", stops);
    std::printf("annotation records that parse to their end: %llu, that do not: %llu\n",
                static_cast<unsigned long long>(clean), static_cast<unsigned long long>(unclean));
    show("where those that do not stop (type, fffe a group's head)", fails);
    for (const auto& [type, samples] : fail_samples)
        for (const auto& line : samples) std::printf("  %x %s\n", type, line.c_str());
    std::printf("the sizes after which the rest parses to its end:\n");
    for (const auto& [type, lines] : oracle)
        for (const auto& line : lines) std::printf("  %x %s\n", type, line.c_str());
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

// ---------------------------------------------------------------------------
// Lichess's evaluations kept offline (#58)

// A file's pages dropped from the system's cache, so that the next reads of it come from the disk (a clean page is
// dropped without rights). False where it cannot be done.
bool dropFromCache(const fs::path& file) {
#ifndef _WIN32
    const int fd = ::open(file.c_str(), O_RDONLY);
    if (fd < 0) return false;
    const bool ok = ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED) == 0;
    ::close(fd);
    return ok;
#else
    (void)file;
    return false;
#endif
}

struct LookupTimes {
    std::vector<double> us;
    std::size_t found = 0;
};

void printTimes(const char* what, LookupTimes t) {
    if (t.us.empty()) return;
    std::sort(t.us.begin(), t.us.end());
    const auto at = [&](double q) {
        return t.us[std::min(t.us.size() - 1, static_cast<std::size_t>(q * t.us.size()))];
    };
    double sum = 0;
    for (const auto v : t.us) sum += v;
    std::printf("  %-34s %7zu lookups, %7zu found: mean %8.2f us, median %8.2f, p90 %8.2f, p99 %8.2f, max %9.2f\n",
                what, t.us.size(), t.found, sum / static_cast<double>(t.us.size()), at(0.5), at(0.9), at(0.99),
                t.us.back());
}

template <typename F>
LookupTimes timeLookups(const std::vector<EvalsPosition>& positions, F&& find) {
    LookupTimes t;
    t.us.reserve(positions.size());
    for (const auto& position : positions) {
        const auto start = Clock::now();
        const bool found = find(position);
        t.us.push_back(seconds(start) * 1e6);
        t.found += found ? 1 : 0;
    }
    return t;
}

struct EvalsSample {
    std::string fen;
    EvalsPosition position;
    PackedEval eval;
};

// Positions no file has: men put at random (a king of each side, a few others).
std::vector<EvalsPosition> randomPositions(std::size_t n, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<EvalsPosition> out;
    const char men[] = "QRBNPqrbnp";
    while (out.size() < n) {
        std::string squares(64, '.');
        squares[rng() % 64] = 'K';
        for (int k = 0; k < 30;) {
            const auto at = rng() % 64;
            if (squares[at] != '.') continue;
            squares[at] = k == 0 ? 'k' : men[rng() % 10];
            ++k;
        }
        std::string fen;
        for (int rank = 7; rank >= 0; --rank) {
            int empty = 0;
            for (int file = 0; file < 8; ++file) {
                const char c = squares[static_cast<std::size_t>(rank * 8 + file)];
                if (c == '.') {
                    ++empty;
                    continue;
                }
                if (empty) fen += static_cast<char>('0' + empty);
                empty = 0;
                fen += c;
            }
            if (empty) fen += static_cast<char>('0' + empty);
            if (rank) fen += '/';
        }
        EvalsPosition p;
        if (evalsPosition(fen + " w - -", p)) out.push_back(p);
    }
    return out;
}

// The lookups of a sample: cold (the files' pages dropped first, then each position once), then warm (the same again).
void measureLookups(const char* what, const std::vector<fs::path>& files, const std::vector<EvalsSample>& sample,
                    const std::function<std::optional<PackedEval>(const EvalsPosition&)>& find,
                    const std::function<void()>& reopen) {
    std::vector<EvalsPosition> keys;
    for (const auto& s : sample) keys.push_back(s.position);
    bool dropped = true;
    for (const auto& f : files) dropped &= dropFromCache(f);
    reopen();  // mapped after the drop
    std::printf("%s (%zu file%s%s)\n", what, files.size(), files.size() == 1 ? "" : "s",
                dropped ? ", pages dropped from the cache first" : ", NOT dropped from the cache");
    const auto take = [&](const EvalsPosition& p) { return find(p).has_value(); };
    std::vector<EvalsPosition> cold_keys(keys.begin(), keys.begin() + static_cast<std::ptrdiff_t>(keys.size() / 2));
    const auto cold_misses = randomPositions(std::min<std::size_t>(5000, keys.size() / 2), 7);
    printTimes("cold, positions it has", timeLookups(cold_keys, take));
    printTimes("cold, positions it has not", timeLookups(cold_misses, take));
    printTimes("warm, the same positions again", timeLookups(cold_keys, take));
    printTimes("warm, the same misses again", timeLookups(cold_misses, take));
    // Correct answers: each sampled position's evaluation, or a deeper one of the same position met elsewhere.
    std::size_t wrong = 0, missing = 0;
    for (const auto& s : sample) {
        const auto got = find(s.position);
        if (!got)
            ++missing;
        else if (!(*got == s.eval) && packedDepth(*got) < packedDepth(s.eval))
            ++wrong;
    }
    std::printf("  checked: %zu sampled positions, %zu missing, %zu with another evaluation not deeper\n",
                sample.size(), missing, wrong);
}

int runEvals(const Args& a) {
    if (a.words.size() != 3) return 2;
    const fs::path dump = a.words[1], folder = a.words[2];
    fs::create_directories(folder);
    const auto build = folder / "build";
    fs::remove_all(build);
    EvalsBuilder::Options options;
    options.min_men     = a.min_men;
    options.run_entries = a.run_entries;
    EvalsBuilder builder(build, options);
    builder.startAgain();

    // The counts.
    uint64_t text = 0, lines = 0, unread = 0, slow = 0, mates = 0, with_passant = 0, odd_castling = 0;
    uint64_t key_checked = 0, key_wrong = 0, key_unreadable = 0, line_bytes_max = 0;
    uint64_t pvs_all = 0, moves_all = 0, pvs_best = 0, all_searches = 0, over_32 = 0;
    std::map<std::string, uint64_t> odd_castlings;
    std::vector<uint64_t> searches(12, 0), lines_of(12, 0), men(33, 0), depth_best(256, 0), depth_all(256, 0);
    std::vector<uint64_t> depth_by_men_sum(33, 0);
    std::vector<int> depths;
    std::vector<EvalsSample> sample;
    std::string pending;
    const auto one = [&](std::string_view line) {
        if (line.empty()) return;
        ++lines;
        line_bytes_max = std::max<uint64_t>(line_bytes_max, line.size());
        depths.clear();
        EvalsLine l;
        if (!readEvalsLine(line, l, &depths)) {
            ++unread;
            builder.add(EvalsLine{});  // counted as read, not kept
            return;
        }
        slow += l.slow ? 1 : 0;
        // The lines (PVs) and their moves, of every search and of the deepest: what keeping them would take.
        all_searches += static_cast<uint64_t>(l.searches);
        pvs_best += static_cast<uint64_t>(l.lines);
        for (std::size_t at = line.find("\"line\":\""); at != std::string_view::npos;
             at             = line.find("\"line\":\"", at + 8)) {
            const auto from = at + 8, to = line.find('"', from);
            if (to == std::string_view::npos) break;
            ++pvs_all;
            if (to > from)
                moves_all += 1 + static_cast<uint64_t>(std::count(line.begin() + from, line.begin() + to, ' '));
        }
        ++searches[std::min<std::size_t>(11, static_cast<std::size_t>(l.searches))];
        ++lines_of[std::min<std::size_t>(11, static_cast<std::size_t>(l.lines))];
        const int all_men = evalsMen(l.fen);
        over_32 += all_men > 32 ? 1 : 0;
        const int m = std::min(32, all_men);
        ++men[static_cast<std::size_t>(m)];
        ++depth_best[static_cast<std::size_t>(std::clamp(l.depth, 0, 255))];
        depth_by_men_sum[static_cast<std::size_t>(m)] += static_cast<uint64_t>(std::clamp(l.depth, 0, 255));
        for (const auto d : depths) ++depth_all[static_cast<std::size_t>(std::clamp(d, 0, 255))];
        mates += l.mate != 0 ? 1 : 0;
        // The FEN's fields: an en passant square, castling rights other than standard chess's letters.
        const auto f2 = l.fen.find(' ', l.fen.find(' ') + 1), f3 = l.fen.find(' ', f2 + 1);
        if (f3 != std::string_view::npos && l.fen.substr(f3 + 1, 1) != "-") ++with_passant;
        if (f2 != std::string_view::npos) {
            const auto castling =
                l.fen.substr(f2 + 1, f3 == std::string_view::npos ? std::string_view::npos : f3 - f2 - 1);
            if (castling.find_first_not_of("KQkq-") != std::string_view::npos) {
                ++odd_castling;
                ++odd_castlings[std::string(castling)];
            }
        }
        // The FEN's position read the fast way against the board chess.hpp reads, on one line in 97.
        if (lines % 97 == 0) {
            const std::string fen(l.fen);
            if (fcg::tour::readableFen(fen)) {
                chess::Board board;
                EvalsPosition position;
                if (board.setFen(fen)) {
                    ++key_checked;
                    if (!evalsPosition(fen, position) || !(position == evalsPosition(board))) {
                        if (++key_wrong <= 10) std::printf("position differs: %s\n", fen.c_str());
                    }
                }
            } else {
                ++key_unreadable;
            }
        }
        if (lines % 10007 == 0 && (options.min_men == 0 || m >= options.min_men)) {
            EvalsSample s;
            if (evalsPosition(l.fen, s.position)) {
                s.fen  = std::string(l.fen);
                s.eval = packEval(l.cp, l.mate, l.depth, l.move);
                sample.push_back(std::move(s));
            }
        }
        builder.add(l);
    };
    const auto text_piece = [&](std::string_view piece) {
        text += piece.size();
        for (std::size_t from = 0;;) {
            const auto end = piece.find('\n', from);
            if (end == std::string_view::npos) {
                pending.append(piece.substr(from));
                return true;
            }
            if (pending.empty()) {
                one(piece.substr(from, end - from));
            } else {
                pending.append(piece.substr(from, end - from));
                one(pending);
                pending.clear();
            }
            from = end + 1;
        }
    };
    auto last_commit = Clock::now(), last_print = Clock::now();
    double cut_seconds   = 0;
    uint64_t frames      = 0;
    const auto frame_end = [&](std::size_t) {
        ++frames;
        const auto before = builder.runs().size();
        const auto t0     = Clock::now();
        (void)builder.mark();
        if (builder.runs().size() != before) cut_seconds += seconds(t0);
        if (seconds(last_commit) >= 5) {
            builder.commit();
            last_commit = Clock::now();
        }
    };
    ZstdStream zstd;
    std::FILE* in = std::fopen(dump.string().c_str(), "rb");
    if (!in) throw std::runtime_error("cannot open " + dump.string());
    const auto size = fs::file_size(dump);
    std::vector<char> buffer(std::size_t{4} << 20);
    uint64_t read    = 0;
    const auto start = Clock::now();
    for (;;) {
        const auto n = std::fread(buffer.data(), 1, buffer.size(), in);
        if (n == 0) break;
        read += n;
        zstd.feed(std::string_view(buffer.data(), n), text_piece, frame_end);
        if (seconds(last_print) >= 30) {
            last_print = Clock::now();
            std::fprintf(stderr, "%5.1f %% · %.1f GB of text · %llu lines · %zu runs · %.0f s\n", 100.0 * read / size,
                         text / 1e9, static_cast<unsigned long long>(lines), builder.runs().size(), seconds(start));
        }
        if (!builder.error().empty()) throw std::runtime_error(builder.error());
    }
    std::fclose(in);
    if (!pending.empty()) one(pending);
    const double read_seconds = seconds(start);

    // The runs answer before the index is made: their lookups measured too.
    builder.commit();
    {
        // The last positions in memory go into a run first, as finish() does, for the measure to see them all.
        uint64_t runs_bytes  = 0;
        const auto run_files = builder.runs();
        for (const auto& f : run_files) runs_bytes += fs::file_size(f);
        std::printf("read: %.0f s for %.2f GB compressed, %.1f GB of text (%llu frames), %.1f s of it writing runs\n",
                    read_seconds, size / 1e9, text / 1e9, static_cast<unsigned long long>(frames), cut_seconds);
        std::printf("runs: %zu, %.2f GB (12 bytes a position, plus 256 KB each)\n", run_files.size(), runs_bytes / 1e9);
        std::vector<std::unique_ptr<EvalsRun>> runs;
        measureLookups(
            "lookups in the runs (while it prepares)", run_files, sample,
            [&](const EvalsPosition& position) -> std::optional<PackedEval> {
                // The runs share their secret: the key made once, as the program does.
                if (runs.empty()) return std::nullopt;
                const auto key = evalsKey(position, runs.front()->secret());
                std::optional<PackedEval> best;
                for (const auto& r : runs)
                    if (auto f = r->findKey(key); f && (!best || packedDepth(*f) > packedDepth(*best))) best = f;
                return best;
            },
            [&] {
                runs.clear();
                for (const auto& f : run_files) runs.push_back(std::make_unique<EvalsRun>(f));
            });
    }

    const auto index = nextEvalsIndex(folder);
    std::atomic<uint64_t> merged{0}, total{0};
    const auto merge_start = Clock::now();
    uint64_t peak_disk     = 0;
    for (const auto& f : builder.runs()) peak_disk += fs::file_size(f);
    const auto why = builder.finish(index, "", &merged, &total);
    if (!why.empty()) throw std::runtime_error(why);
    const double merge_seconds = seconds(merge_start);
    const auto index_bytes     = fs::file_size(index);
    peak_disk += index_bytes;
    tidyEvalsFolder(folder);

    // The sample, for evals-lookup.
    {
        std::FILE* out = std::fopen((folder / "sample.tsv").string().c_str(), "wb");
        for (const auto& s : sample) {
            std::fprintf(out, "%s", s.fen.c_str());
            for (const auto b : s.eval.bytes) std::fprintf(out, "\t%u", b);
            std::fprintf(out, "\n");
        }
        std::fclose(out);
    }

    const auto pct = [&](uint64_t n) {
        return lines ? 100.0 * static_cast<double>(n) / static_cast<double>(lines) : 0.0;
    };
    std::printf("\nthe database: %s, %.2f GB compressed, %.1f GB of text, %llu lines (the longest %llu bytes)\n",
                dump.filename().string().c_str(), size / 1e9, text / 1e9, static_cast<unsigned long long>(lines),
                static_cast<unsigned long long>(line_bytes_max));
    std::printf("  not read: %llu; read as JSON (another order of the fields): %llu; mates: %llu (%.2f %%)\n",
                static_cast<unsigned long long>(unread), static_cast<unsigned long long>(slow),
                static_cast<unsigned long long>(mates), pct(mates));
    std::printf("  en passant squares: %llu; castling rights other than KQkq: %llu; more than 32 men: %llu\n",
                static_cast<unsigned long long>(with_passant), static_cast<unsigned long long>(odd_castling),
                static_cast<unsigned long long>(over_32));
    std::printf("  castling rights other than KQkq, the most common:");
    {
        std::vector<std::pair<uint64_t, std::string>> odd;
        for (const auto& [c, n] : odd_castlings) odd.emplace_back(n, c);
        std::sort(odd.rbegin(), odd.rend());
        for (std::size_t i = 0; i < odd.size() && i < 8; ++i)
            std::printf(" %s %llu", odd[i].second.c_str(), static_cast<unsigned long long>(odd[i].first));
        std::printf("\n");
    }
    std::printf(
        "  searches %llu; lines (PVs) of every search %llu, of the deepest %llu; moves in every line %llu "
        "(%.2f a line)\n",
        static_cast<unsigned long long>(all_searches), static_cast<unsigned long long>(pvs_all),
        static_cast<unsigned long long>(pvs_best), static_cast<unsigned long long>(moves_all),
        pvs_all ? static_cast<double>(moves_all) / static_cast<double>(pvs_all) : 0.0);
    std::printf(
        "  positions read the fast way, checked against chess.hpp's board: %llu, %llu different, %llu FENs it "
        "cannot read\n",
        static_cast<unsigned long long>(key_checked), static_cast<unsigned long long>(key_wrong),
        static_cast<unsigned long long>(key_unreadable));
    std::printf("searches per position:");
    for (std::size_t i = 1; i < searches.size(); ++i)
        if (searches[i]) std::printf(" %zu%s: %.2f %%", i, i == 11 ? "+" : "", pct(searches[i]));
    std::printf("\nlines of the deepest search (MultiPV):");
    for (std::size_t i = 1; i < lines_of.size(); ++i)
        if (lines_of[i]) std::printf(" %zu%s: %.2f %%", i, i == 11 ? "+" : "", pct(lines_of[i]));
    std::printf("\n");
    const auto depthTable = [&](const char* what, const std::vector<uint64_t>& h) {
        uint64_t n = 0;
        for (const auto v : h) n += v;
        std::printf("%s (%llu):", what, static_cast<unsigned long long>(n));
        const int edges[] = {0, 10, 20, 25, 30, 35, 40, 45, 50, 60, 70, 100, 256};
        for (std::size_t e = 0; e + 1 < std::size(edges); ++e) {
            uint64_t c = 0;
            for (int d = edges[e]; d < edges[e + 1]; ++d) c += h[static_cast<std::size_t>(d)];
            std::printf(" %d-%d: %.2f %%", edges[e], edges[e + 1] - 1, n ? 100.0 * c / n : 0.0);
        }
        uint64_t acc = 0;
        int median   = 0;
        double mean  = 0;
        for (int d = 0; d < 256; ++d) {
            mean += static_cast<double>(d) * h[static_cast<std::size_t>(d)];
            if (acc * 2 < n && (acc += h[static_cast<std::size_t>(d)]) * 2 >= n) median = d;
        }
        std::printf("; median %d, mean %.1f\n", median, n ? mean / n : 0.0);
        // At least: for a threshold.
        std::printf("  at least:");
        for (const int t : {20, 25, 30, 35, 40, 45, 50}) {
            uint64_t c = 0;
            for (int d = t; d < 256; ++d) c += h[static_cast<std::size_t>(d)];
            std::printf(" %d: %.1f %%", t, n ? 100.0 * c / n : 0.0);
        }
        std::printf("\n");
    };
    depthTable("depth of each position's deepest search", depth_best);
    depthTable("depth of every search", depth_all);
    std::printf("men on the board (positions, then the mean depth of their deepest search):\n");
    uint64_t acc = 0;
    for (int m = 32; m >= 2; --m) {
        acc += men[static_cast<std::size_t>(m)];
        std::printf(
            "  %2d: %6.2f %%, %llu or more: %6.2f %%, depth %.1f\n", m, pct(men[static_cast<std::size_t>(m)]),
            static_cast<unsigned long long>(m), pct(acc),
            men[static_cast<std::size_t>(m)]
                ? static_cast<double>(depth_by_men_sum[static_cast<std::size_t>(m)]) / men[static_cast<std::size_t>(m)]
                : 0.0);
    }
    std::printf(
        "\nthe index: %s, %llu positions (%llu merged), %.3f GB (%.2f bytes a position); merged in %.0f s; "
        "at most %.2f GB on the disk while it prepares (runs and index)\n",
        index.filename().string().c_str(), static_cast<unsigned long long>(merged.load()),
        static_cast<unsigned long long>(total.load()), index_bytes / 1e9,
        static_cast<double>(index_bytes) / std::max<uint64_t>(1, merged.load()), merge_seconds, peak_disk / 1e9);
    std::printf("total: %.0f s, peak memory %.0f MB\n", seconds(start), peakMb());
    std::unique_ptr<EvalsIndex> opened;
    measureLookups(
        "lookups in the index", {index}, sample,
        [&](const EvalsPosition& position) -> std::optional<PackedEval> {
            const auto f = opened->find(position);
            if (!f) return std::nullopt;
            return packEval(f->cp, f->mate, f->depth, f->best);
        },
        [&] { opened = std::make_unique<EvalsIndex>(index); });
    return 0;
}

int runEvalsLookup(const Args& a) {
    if (a.words.size() != 2) return 2;
    const fs::path folder = a.words[1];
    const auto index      = newestEvalsIndex(folder);
    if (index.empty()) throw std::runtime_error("no index in " + folder.string());
    std::vector<EvalsSample> sample;
    std::FILE* in = std::fopen((folder / "sample.tsv").string().c_str(), "rb");
    if (!in) throw std::runtime_error("no sample.tsv in " + folder.string());
    char line[512];
    while (std::fgets(line, sizeof line, in)) {
        const std::string_view text(line);
        const auto tab = text.find('\t');
        if (tab == std::string_view::npos) continue;
        EvalsSample s;
        s.fen = std::string(text.substr(0, tab));
        unsigned b[5];
        if (std::sscanf(line + tab, "\t%u\t%u\t%u\t%u\t%u", &b[0], &b[1], &b[2], &b[3], &b[4]) != 5 ||
            !evalsPosition(s.fen, s.position))
            continue;
        for (int i = 0; i < 5; ++i) s.eval.bytes[i] = static_cast<uint8_t>(b[i]);
        sample.push_back(std::move(s));
    }
    std::fclose(in);
    std::unique_ptr<EvalsIndex> opened;
    measureLookups(
        "lookups in the index", {index}, sample,
        [&](const EvalsPosition& position) -> std::optional<PackedEval> {
            const auto f = opened->find(position);
            if (!f) return std::nullopt;
            return packEval(f->cp, f->mate, f->depth, f->best);
        },
        [&] { opened = std::make_unique<EvalsIndex>(index); });
    std::printf("index: %s, %llu positions, %.3f GB\n", index.filename().string().c_str(),
                static_cast<unsigned long long>(opened->positions()), opened->bytes() / 1e9);
    return 0;
}

// ---------------------------------------------------------------------------
// Lichess's puzzles kept offline (#80)

// Picks of twenty, cold then warm: a theme at random, a rating at random; then the puzzles picked read.
void measurePicks(const fs::path& store) {
    std::mt19937_64 rng(80);
    for (const bool cold : {true, false}) {
        if (cold && !dropFromCache(store)) std::printf("(the store's pages could not be dropped from the cache)\n");
        PuzzleStore s(store);
        std::vector<double> times, reads;
        for (int i = 0; i < 200; ++i) {
            const int list   = static_cast<int>(rng() % (s.themes().size() + 1)) - 1;
            const int rating = 800 + static_cast<int>(rng() % 1800);
            const auto t0    = Clock::now();
            const auto got   = s.pick(list, rating, 20, {}, rng());
            times.push_back(ms(t0));
            const auto t1 = Clock::now();
            for (const auto k : got) (void)s.puzzle(k);
            if (!got.empty()) reads.push_back(ms(t1) / static_cast<double>(got.size()));
        }
        std::sort(times.begin(), times.end());
        std::sort(reads.begin(), reads.end());
        std::printf(
            "%s: a pick of 20 median %.2f ms, 90 %% within %.2f ms, the slowest %.2f ms; a puzzle read median %.3f "
            "ms\n",
            cold ? "cold" : "warm", times[times.size() / 2], times[times.size() * 9 / 10], times.back(),
            reads.empty() ? 0.0 : reads[reads.size() / 2]);
    }
}

int runPuzzles(const Args& a) {
    if (a.words.size() < 3) return 2;
    const fs::path file = a.words[1], folder = a.words[2];
    fs::create_directories(folder);
    const auto build = folder / "build";
    fs::remove_all(build);
    // The database read as the program reads it: its frames decoded, its lines given to the builder; a puzzle in a
    // thousand kept aside to compare with the store.
    PuzzleBuilder builder(build);
    std::vector<std::string> sample;
    uint64_t text = 0, lines = 0;
    const auto start = Clock::now();
    {
        std::FILE* in = std::fopen(file.string().c_str(), "rb");
        if (!in) throw std::runtime_error("cannot open " + file.string());
        ZstdStream zstd;
        std::string pending;
        std::vector<char> buffer(1 << 20);
        const auto line = [&](std::string_view l) {
            builder.line(l);
            if (++lines % 1000 == 500) sample.emplace_back(l);
        };
        const std::function<bool(std::string_view)> out = [&](std::string_view piece) {
            text += piece.size();
            for (std::size_t from = 0;;) {
                const auto end = piece.find('\n', from);
                if (end == std::string_view::npos) {
                    pending.append(piece.substr(from));
                    break;
                }
                if (pending.empty()) {
                    line(piece.substr(from, end - from));
                } else {
                    pending.append(piece.substr(from, end - from));
                    line(pending);
                    pending.clear();
                }
                from = end + 1;
            }
            return true;
        };
        std::size_t n = 0;
        while ((n = std::fread(buffer.data(), 1, buffer.size(), in)) > 0)
            zstd.feed(std::string_view(buffer.data(), n), out);
        std::fclose(in);
        if (!pending.empty()) line(pending);
    }
    const double read_s = seconds(start);
    const auto store    = nextPuzzleStore(folder);
    const auto finish   = Clock::now();
    const auto why      = builder.finish(store, "");
    if (!why.empty()) throw std::runtime_error(why);
    const double finish_s = seconds(finish);
    std::printf(
        "read: %.1f s (%.2f GB of text, %llu lines, %llu puzzles kept), sorted and written: %.1f s, peak %.0f MB\n",
        read_s, text / 1e9, static_cast<unsigned long long>(builder.lines()),
        static_cast<unsigned long long>(builder.kept()), finish_s, peakMb());
    PuzzleStore opened(store);
    std::printf("store: %s, %u puzzles, %.1f MB, %.2f bytes a puzzle; %zu themes, %zu openings\n",
                store.filename().string().c_str(), opened.size(), opened.bytes() / 1e6,
                static_cast<double>(opened.bytes()) / opened.size(), opened.themes().size(), opened.openings().size());
    uint64_t list_bytes = 0, postings = 0;
    for (const auto* tags : {&opened.themes(), &opened.openings()})
        for (const auto& t : *tags) postings += t.puzzles;
    (void)list_bytes;
    std::printf("lists: %llu puzzles listed in all\n", static_cast<unsigned long long>(postings));

    // A puzzle in a thousand, as its line has it.
    std::map<std::string, uint32_t> numbers;
    for (uint32_t k = 0; k < opened.size(); ++k) numbers[opened.idOf(k)] = k;
    std::size_t same = 0, differ = 0;
    for (const auto& l : sample) {
        PuzzleLine line;
        if (!readPuzzleLine(l, line)) continue;
        const auto it = numbers.find(std::string(line.id));
        if (it == numbers.end()) {
            ++differ;
            continue;
        }
        const auto p = opened.puzzle(it->second);
        std::string moves, themes, openings;
        for (const auto& m : p.moves) moves += (moves.empty() ? "" : " ") + m;
        std::vector<std::string> want_themes, got_themes = p.themes;
        std::size_t from = 0;
        while (from <= line.themes.size()) {
            const auto space = line.themes.find(' ', from);
            want_themes.emplace_back(
                line.themes.substr(from, space == std::string_view::npos ? std::string_view::npos : space - from));
            if (space == std::string_view::npos) break;
            from = space + 1;
        }
        std::sort(want_themes.begin(), want_themes.end());
        std::sort(got_themes.begin(), got_themes.end());
        for (const auto& o : p.openings) openings += (openings.empty() ? "" : " ") + o;
        const bool ok = p.fen == line.fen && moves == line.moves && p.rating == line.rating &&
                        p.deviation == std::min(line.deviation, 255) && p.popularity == line.popularity &&
                        got_themes == want_themes && openings == line.openings && p.gameUrl() == line.url;
        if (ok) {
            ++same;
        } else {
            if (++differ <= 5) std::printf("differs: %s\n  kept: %s %s\n", l.c_str(), p.fen.c_str(), moves.c_str());
        }
    }
    std::printf("compared with their lines: %zu the same, %zu differ\n", same, differ);

    measurePicks(store);
    return 0;
}

int runPuzzlePicks(const Args& a) {
    if (a.words.size() < 2) return 2;
    const auto store = newestPuzzleStore(a.words[1]);
    if (store.empty()) throw std::runtime_error("no store of puzzles in " + a.words[1]);
    measurePicks(store);
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
        if (a.words[0] == "chessbase-dump") rc = runChessBaseDump(a);
        if (a.words[0] == "chessbase-scan") rc = runChessBaseScan(a);
        if (a.words[0] == "chessbase-item") rc = runChessBaseItem(a);
        if (a.words[0] == "evals") rc = runEvals(a);
        if (a.words[0] == "evals-lookup") rc = runEvalsLookup(a);
        if (a.words[0] == "puzzles") rc = runPuzzles(a);
        if (a.words[0] == "puzzles-pick") rc = runPuzzlePicks(a);
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
