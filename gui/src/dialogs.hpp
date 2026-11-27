#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;

namespace fcg {

// Native file dialogs (SDL3: portal or zenity on Linux, the shell dialogs on Windows).
// SDL may answer on another thread, so answers are queued and picked up with take(). A dialog opened with a callback
// (RookStudio's) is answered by it, called from App::handleDialogs() on the main thread; one opened for a purpose
// (fastchess-gui's) by that purpose's case there.
class Dialogs {
   public:
    // The file chosen, as the dialog gives it (none when it is cancelled: never called).
    using Chosen = std::function<void(const std::string& path)>;

    enum class Purpose {
        AddEngine,
        EngineCommand,
        EngineDir,
        Book,
        Tablebases,
        TablebasesFolder,  // the setup's tablebase folder replaced, from a notice of the summary
        ExportGames,
        ExportAll,
        OpenTournament,
        ExportStandings,
        ImportEngineList,
        ImportEngineFolder,
        ImportRatingList,
        ExportRatings,
        ExportRatingHtml,
        ExportRatingMarkdown,
        ExportRatingPgn,
        ImportRatingPgn,
        ExportHeadToHead,
        ExportOpenings,
        AddBook,
        BookFile,
        SaveCleanBook,
        Chess324Book,
        Chess324SymmetricalBook,
        BuildBookPgn,
        BuildBookBin,
        SuiteFile,
        ExportSuiteCsv,
        TournamentsFolder,
        PgnOut,
        ExportSubmission,
        EngineOption,  // a file or a folder for a UCI option (EvalFile, SyzygyPath)
        ConsoleLog,    // the engine console's log
        BoardPgn,      // a PGN file for the Board page
        EngineLogo,
        PieceFolder,
        MachineKey,         // a remote machine's key file (Settings)
        MachineProgram,     // the fastchess-gui to install on a remote machine of another system
        MachineEngineBuild  // an engine's build for a remote machine's system, copied there
    };

    struct Answer {
        Purpose purpose{};
        std::string path;
        Chosen chosen;  // set for a dialog opened with a callback, which answers in place of the purpose
    };

    explicit Dialogs(SDL_Window* window) : window_(window) {}

    void openFile(Purpose purpose, const std::vector<std::pair<std::string, std::string>>& filters = {},
                  const std::string& start = {});
    void openFolder(Purpose purpose, const std::string& start = {});
    void saveFile(Purpose purpose, const std::vector<std::pair<std::string, std::string>>& filters,
                  const std::string& start = {});
    // The same, the file chosen given to `chosen`.
    void openFile(Chosen chosen, const std::vector<std::pair<std::string, std::string>>& filters = {},
                  const std::string& start = {});
    void openFolder(Chosen chosen, const std::string& start = {});
    void saveFile(Chosen chosen, const std::vector<std::pair<std::string, std::string>>& filters,
                  const std::string& start = {});

    [[nodiscard]] std::optional<Answer> take();

    [[nodiscard]] bool busy() const;

   private:
    struct Request;

    static void callback(void* userdata, const char* const* files, int filter);
    void show(Request* request, int kind);  // kind: 0 open a file, 1 a folder, 2 save a file

    SDL_Window* window_;

    mutable std::mutex mutex_;
    std::vector<Answer> answers_;
    int pending_ = 0;
};

}  // namespace fcg
