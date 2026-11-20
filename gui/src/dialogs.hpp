#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;

namespace fcg {

// Native file dialogs (SDL3: portal or zenity on Linux, the shell dialogs on Windows).
// SDL may answer on another thread, so answers are queued and picked up with take().
class Dialogs {
   public:
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
        CtgPolyglotBook,  // a CTG book's Polyglot copy for engines (#68)
        Chess324Book,
        Chess324SymmetricalBook,
        OpeningSuiteBook,    // a book from Lichess's named openings (#53)
        EvaluatedSuiteBook,  // a book from Lichess's evaluated positions (#53)
        EvalsFile,           // Lichess's evaluations database downloaded before (#53)
        BuildBookPgn,
        BuildBookBin,
        SuiteFile,
        ExportSuiteCsv,
        TournamentsFolder,
        PgnOut,
        ExportSubmission,
        EngineOption,    // a file or a folder for a UCI option (EvalFile, SyzygyPath)
        ConsoleLog,      // the engine console's log
        BoardPgn,        // a PGN file for the Board page
        SaveBoardPgn,    // the Board page's game saved as a PGN file
        AppendBoardPgn,  // the same, after the games of a PGN file
        AssistantModel,  // a GGUF file for the assistant, instead of the catalogue's model
        EngineLogo,
        PieceFolder,
        MachineKey,          // a remote machine's key file (Settings)
        MachineProgram,      // the fastchess-gui to install on a remote machine of another system
        MachineEngineBuild,  // an engine's build for a remote machine's system, copied there
        ImportGames,         // a PGN (.pgn, .zst, .gz, .zip) imported into a database
        AddDatabase          // a database folder made elsewhere, added to the list
    };

    struct Answer {
        Purpose purpose;
        std::string path;
    };

    explicit Dialogs(SDL_Window* window) : window_(window) {}

    void openFile(Purpose purpose, const std::vector<std::pair<std::string, std::string>>& filters = {},
                  const std::string& start = {});
    void openFolder(Purpose purpose, const std::string& start = {});
    void saveFile(Purpose purpose, const std::vector<std::pair<std::string, std::string>>& filters,
                  const std::string& start = {});

    [[nodiscard]] std::optional<Answer> take();

    [[nodiscard]] bool busy() const;

   private:
    struct Request;

    static void callback(void* userdata, const char* const* files, int filter);

    SDL_Window* window_;

    mutable std::mutex mutex_;
    std::vector<Answer> answers_;
    int pending_ = 0;
};

}  // namespace fcg
