#pragma once

// An actionable message: what is wrong, naming the engine (and its option or file) and the resource (memory, cores,
// a folder), and the fixes, one button each. The setup summary, the home page and the tournament header show them,
// and the Chess space's pages in place of their errors' lines (#141, repairs.hpp).

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace fcg {

// What a RetryAfterPause asks again (its `value`): each a page's own way to ask again, the one its Try again took
// before (App::retryNow()).
enum class Retry : int {
    None,             // nothing to ask again
    TwicIndex,        // TWIC's page of issues
    ChesscomMonths,   // the months of the chess.com player looked up last
    LichessMonths,    // database.lichess.org's page of monthly files
    StudiesList,      // the Lichess account's list of studies
    FideList,         // FIDE's rating list downloaded (the rest only, when a part came)
    BoardTables,      // the Endgames tab's question to Lichess's tables
    OnlineTablebase,  // the Board's line of Lichess's tables
    OnlineCloud,      // the Board's line of Lichess's cloud
    EndgameCheck,     // a tournament's endgame check, its position to Lichess's tables
    FileAdd,          // the downloads of the sources' sheet that failed (TWIC, chess.com, Lichess's broadcasts)
    // #146: the Lichess page's other cards; a module's own are asked again by it (OwnRetries).
    Broadcasts,       // the Broadcasts card's list of official broadcasts
    BroadcastSearch,  // the Broadcasts card's search, its words again
    TvChannels,       // Lichess TV's card: each channel's featured game (its module's)
    ArenaList,        // the Lichess tournaments card: the arenas in progress and to come (its module's)
    TeamSwisses,      // the same card: the user's teams' Swiss tournaments (its module's)
};

// What fixes a problem. The App carries it out (App::applyFix); the views only show its button.
struct Fix {
    enum class Kind {
        OpenEngine,               // `engine` selected on the Engines page
        LocateProgram,            // the file dialog for the program of `engine`
        LocateDir,                // the folder dialog for the working directory of `engine`
        LocateBook,               // the file dialog for the setup's opening book
        LocateTablebases,         // the folder dialog for the setup's tablebases (the adjudication's)
        LowerEngineThreads,       // the Threads option of `engine`, in the library, set to `value`
        LowerParticipantThreads,  // the Threads of participant `index` of the setup set to `value`
        LowerCommonThreads,       // the Threads of the setup's common settings set to `value`
        LowerTablebasePieces,     // the tablebase adjudication up to `value` pieces
        OpenLog,                  // the Log page
        OpenFolder,               // `path` in the file manager of the system
        ChangeTournamentsFolder,  // the folder dialog for where new tournaments are written
        CopyToMachine,            // the copy of `engine` to the machine whose id is `path` offered (Settings)
        TestMachine,              // the test of the connection to the machine whose id is `path`
        SetRunOn,                 // the setup's Run on set to the machine whose id is `path` (empty: this computer)
        SetMargin,                // the setup's time margin set to `value` milliseconds
        EnterPassphrase,          // the passphrase of the key of the machine whose id is `path` asked
        // The Chess space's repairs (#141, repairs.hpp):
        LinkLichess,          // the Lichess account linked again, asking also for the permission `path` (empty: none)
        RetryAfterPause,      // what `value` (Retry) names asked again once the pause of the host `path` is over
        CheckDatabase,        // the care of the database `index` opened at its checks
        ChooseEngine,         // the Board's engine picker opened, for an engine to analyse with
        RegisterEngineAgain,  // the engine `engine` registered with Lichess again (#96)
    };
    Kind kind;
    std::string label;    // the button's
    std::string tooltip;  // what it does, and where it applies (the library, this tournament); may be empty
    std::string engine;
    std::string path;
    int value = 0;
    int index = -1;
    bool disabled = false;  // drawn disabled, its tooltip still shown: a retry that waits for its host's pause

    bool operator==(const Fix&) const = default;
};

// The fixes, with their labels in the language of the interface.
[[nodiscard]] Fix openEngineFix(const std::string& engine);
[[nodiscard]] Fix locateProgramFix(const std::string& engine);
[[nodiscard]] Fix locateDirFix(const std::string& engine);
[[nodiscard]] Fix locateBookFix();
[[nodiscard]] Fix locateTablebasesFix();
[[nodiscard]] Fix lowerEngineThreadsFix(const std::string& engine, int threads);
[[nodiscard]] Fix lowerParticipantThreadsFix(const std::string& engine, int index, int threads);
[[nodiscard]] Fix lowerCommonThreadsFix(int threads);
[[nodiscard]] Fix lowerTablebasePiecesFix(int pieces);
[[nodiscard]] Fix openLogFix();
[[nodiscard]] Fix openFolderFix(const std::string& path);
[[nodiscard]] Fix changeTournamentsFolderFix();
[[nodiscard]] Fix copyToMachineFix(const std::string& engine, const std::string& machine_id,
                                   const std::string& machine_name);
[[nodiscard]] Fix testMachineFix(const std::string& machine_id, const std::string& machine_name);
[[nodiscard]] Fix setRunOnFix(const std::string& machine_id, const std::string& machine_name);
[[nodiscard]] Fix setMarginFix(int milliseconds);
[[nodiscard]] Fix enterPassphraseFix(const std::string& machine_id);
// The Chess space's (#141). Link Lichess again… (`linked`: an account is linked, else Link the Lichess account…), or,
// for a permission the account lacks (`scope`), the button named by what it allows: "Allow writing studies…".
[[nodiscard]] Fix linkLichessFix(const std::string& scope = {}, bool linked = true);
// Try again: "Try again in 0:42" while `host` pauses (`pause` left), "Asks again in 0:41" and disabled once `queued`.
[[nodiscard]] Fix retryAfterPauseFix(Retry what, const std::string& host, std::chrono::milliseconds pause = {},
                                     bool queued = false);
[[nodiscard]] Fix checkDatabaseFix(int database);
[[nodiscard]] Fix chooseEngineFix();
[[nodiscard]] Fix registerEngineAgainFix(const std::string& engine);
// What a Lichess permission lets RookStudio do, as a button that asks for it: "Allow writing studies…".
[[nodiscard]] std::string allowScopeLabel(std::string_view scope);
// A pause left as the buttons count it, whole seconds rounded up: "0:42".
[[nodiscard]] std::string pauseClock(std::chrono::milliseconds pause);

struct Notice {
    enum class Level { Ok, Warning, Error };
    Level level = Level::Warning;
    std::string text;
    std::string detail;  // shown when the mouse is on the text (the whole list of files missing...); may be empty
    std::vector<Fix> fixes;
    // The guide's section that explains it (guide::topics), shown as a `?` after its fixes; nullptr: the one its
    // fixes point to (noticeTopic()).
    const char* topic = nullptr;
};

// The guide's section of a notice: its own, else the troubleshooting entry its fixes point to (an engine's program,
// threads or tablebases, the log), nullptr when none.
[[nodiscard]] const char* noticeTopic(const Notice& notice);

// Draws `notice` as the lines of the setup summary are: a mark (✓, or ! orange for a warning, red for an error), the
// text wrapped, then its fixes, small buttons on the next line. `id` tells its buttons from another notice's.
// Returns the fix clicked, or nullptr.
const Fix* drawNotice(const Notice& notice, const char* id);

// drawNotice() of each, the fix clicked (if any) in `clicked`. Returns true when one was.
bool drawNotices(const std::vector<Notice>& notices, const char* id, Fix& clicked);

}  // namespace fcg
