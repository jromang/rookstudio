// The actionable messages of the setup summary (notice.hpp, setup_checks.hpp) and the fixes they offer: the App
// carries them out here, the Chess space's repairs too (#141, repairs.hpp).

#include <algorithm>

#include <imgui.h>

#include "app.hpp"
#include "i18n.hpp"
#include "lichess_arenas.hpp"  // scopesAdding()
#include "setup_checks.hpp"
#include "ui.hpp"

namespace fcg {

// What the repairs ask of the program (repairs.hpp), answered by App.
class App::Repairs final : public RepairActions {
   public:
    explicit Repairs(App& app) : app_(app) {}
    void linkLichess(const std::string& scope) override { app_.linkLichessFor(scope); }
    void retryNow(Retry what) override { app_.retryNow(what); }
    [[nodiscard]] std::chrono::milliseconds pausedFor(const std::string& host) const override {
        return HostGate::instance().pausedFor(host);
    }
    void checkDatabase(int database) override { app_.openCare(database, CarePart::Checks); }
    void chooseEngine() override {
        // The study Board's Analysis tab: its "+ An engine" opened at the next frame, the library's engines and one
        // to download; with an empty library, the card that offers the first engine (M9, board 2) in its place.
        app_.toStudyBoard();
        app_.board_side_tab_ = 1;
        if (!app_.engines_.empty()) app_.side_menu_open_ = true;
    }
    void registerEngineAgain(const std::string& engine) override { app_.registerAgain(engine); }

   private:
    App& app_;
};

void App::runDueRetries() {
    if (retries_.empty()) return;
    for (const auto what : retries_.due([](const std::string& host) { return HostGate::instance().pausedFor(host); }))
        retryNow(what);
}

void App::retryNow(Retry what) {
    // Each the page's own Try again, as its button took it before (#141).
    switch (what) {
        case Retry::None:
            break;
        case Retry::TwicIndex:
            startTwicIndex(false);
            break;
        case Retry::ChesscomMonths: {
            const auto player = chesscom_player_;
            startChesscomLookup(player);
            break;
        }
        case Retry::LichessMonths:
            startLichessMonths();
            break;
        case Retry::StudiesList:
            studies_list_of_.clear();
            studies_list_error_ = {};
            break;
        case Retry::FideList:
            startFideList();
            break;
        case Retry::BoardTables:
            board_tables_.nothing.reset();
            break;
        case Retry::OnlineTablebase:
            online_tb_.reset();
            break;
        case Retry::OnlineCloud:
            online_cloud_.reset();
            break;
        case Retry::EndgameCheck:
            if (endgame_check_) endgame_check_->failure = {};
            break;
        case Retry::FileAdd: {
            // The same files into the same database, while the report that failed still shows.
            if (!twic_done_ || !twic_done_->download.failed() || twic_done_->urls.empty() || twic_add_) break;
            const auto& d = *twic_done_;
            TwicAdd add;
            add.title          = d.title;
            add.issues         = d.issues;
            add.urls           = d.urls;
            add.wanted         = d.wanted;
            add.player         = d.player;
            add.months         = d.months;
            const auto title   = d.title;
            const int database = static_cast<int>(d.index);
            startFileAdd(std::move(add), database, title, nullptr);
            break;
        }
        case Retry::Broadcasts:  // asked again at once, the failure said again if it fails again
            lichess_broadcasts_time_                            = {};
            static_cast<lichess::Failure&>(lichess_broadcasts_) = {};
            askBroadcasts();
            break;
        case Retry::BroadcastSearch:
            if (broadcast_found_ && !broadcast_search_job_.valid()) {
                broadcast_found_.reset();
                startBroadcastSearch();  // the same words, still in the field
            }
            break;
        case Retry::ReportExplorer:
            // The explorer's answers that gave nothing forgotten: the report asks for its position again.
            for (auto it = explorer_answers_.begin(); it != explorer_answers_.end();) {
                if (it->second.value) {
                    ++it;
                    continue;
                }
                explorer_failed_.erase(it->first);
                it = explorer_answers_.erase(it);
            }
            if (opening_report_) opening_report_->lichess_failure = {};
            break;
        case Retry::TvChannels:
        case Retry::ArenaList:
        case Retry::TeamSwisses:
            break;  // the modules' own, asked by them (OwnRetries)
    }
}

void App::linkLichessFor(const std::string& scope) {
    if (lichess_link_) return;  // a link waits for the browser already
    // The studies' permissions as their cards ask for them: writing, reading too unless the user said no to it.
    if (scope == lichess::SCOPE_STUDY_READ || scope == lichess::SCOPE_STUDY_WRITE) {
        askStudyPermission(scope == lichess::SCOPE_STUDY_WRITE);
        return;
    }
    // An engine registered with Lichess: read and written, as its card asks (#96).
    std::vector<std::string> asked = lichess_account_ ? lichess_account_->scopes : std::vector<std::string>{};
    if (!scope.empty()) asked = lichess::scopesAdding(asked, scope);
    if (scope == lichess::SCOPE_ENGINE_WRITE) asked = lichess::scopesAdding(asked, lichess::SCOPE_ENGINE_READ);
    startLichessLink(asked, lichess_account_ ? lichess_account_->username : std::string());
}

RetryState App::retryState(Retry what, const std::string& host) const {
    return RetryState{what, host, HostGate::instance().pausedFor(host), retries_.queued(what)};
}

LichessAccess App::lichessAccess(const std::string& needs) const {
    LichessAccess access;
    access.linked = lichess_account_.has_value();
    if (lichess_account_) access.scopes = lichess_account_->scopes;
    access.needs = needs;
    return access;
}

void App::drawRepairNotice(const Notice& notice, const char* id) {
    if (const auto* fix = drawNotice(notice, id)) applyFix(*fix);
}

std::vector<Notice> App::setupNotices() {
    std::vector<Notice> notices;
    const auto append = [&](std::vector<Notice> more) {
        for (auto& n : more) notices.push_back(std::move(n));
    };
    append(threadNotices(setup_, engines_, topology_));
    append(optionNotices(setup_, engines_));
    // The tablebases, as their last check found them: each path is read on a worker thread.
    if (auto none = adjudicationFolderNotice(setup_)) notices.push_back(std::move(*none));
    for (const auto& use : tablebaseUses(setup_, engines_)) {
        if (const auto* check = tb_checks_.get(use.paths)) append(tablebaseNotices(use, *check));
    }
    return notices;
}

void App::drawSetupNotices(std::vector<Notice> notices, const char* id) {
    // While a tournament runs, the setup is locked, and the engines it plays are not changed.
    for (auto& n : notices) {
        std::erase_if(n.fixes, [&](const Fix& f) {
            switch (f.kind) {
                case Fix::Kind::LocateBook:
                case Fix::Kind::LocateTablebases:
                case Fix::Kind::LowerParticipantThreads:
                case Fix::Kind::LowerCommonThreads:
                case Fix::Kind::LowerTablebasePieces:
                case Fix::Kind::SetRunOn:
                case Fix::Kind::SetMargin:
                    return run_->running();
                case Fix::Kind::LocateProgram:
                case Fix::Kind::LocateDir:
                case Fix::Kind::LowerEngineThreads:
                    return engineInUse(f.engine);
                default:
                    return false;
            }
        });
    }
    Fix clicked;
    if (drawNotices(notices, id, clicked)) applyFix(clicked);
}

void App::setStartFixes() {
    // A start refused names what the summary says: its errors' fixes, with the log, where fastchess said more.
    start_fixes_.clear();
    std::vector<Notice> errors = programNotices(setup_, engines_);
    if (auto book = bookNotice(setup_)) errors.push_back(std::move(*book));
    if (auto folder = adjudicationFolderNotice(setup_)) errors.push_back(std::move(*folder));
    if (const auto memory = setupMemory(setup_, engines_, topology_); memory.aboveInstalled())
        errors.push_back(memoryNotice(setup_, engines_, memory));
    for (const auto& n : errors) {
        for (const auto& f : n.fixes) {
            if (std::find(start_fixes_.begin(), start_fixes_.end(), f) == start_fixes_.end()) start_fixes_.push_back(f);
        }
    }
    // On a machine: the engines without a path there, each offered a copy of this computer's.
    if (const auto* machine = setup_.machine.empty() ? nullptr : machineById(setup_.machine)) {
        for (const auto& name : enginesOfSetup(setup_)) {
            const auto* there = machine->engine(name);
            const auto fix = copyToMachineFix(name, machine->id, machine->name.empty() ? machine->host : machine->name);
            if ((!there || there->path.empty()) &&
                std::find(start_fixes_.begin(), start_fixes_.end(), fix) == start_fixes_.end()) {
                start_fixes_.push_back(fix);
            }
        }
    }
    start_fixes_.push_back(openLogFix());
    start_fixes_for_ = start_error_;
}

void App::setFinishFixes() {
    finish_fixes_.clear();
    if (const auto engine = libraryEngine(engines_, stopped_by_); !engine.empty())
        finish_fixes_.push_back(openEngineFix(engine));
    finish_fixes_.push_back(openLogFix());
    finish_fixes_for_ = finish_message_;
}

void App::drawMessage(const std::string& text, const std::vector<Fix>& fixes, const std::string& fixes_for, bool error,
                      const char* id) {
    Notice n{error ? Notice::Level::Error : Notice::Level::Warning, text, {}, {}};
    if (fixes_for == text) {
        n.fixes = fixes;
    } else if (error) {
        n.fixes = {openLogFix()};
    }
    if (const auto* fix = drawNotice(n, id)) applyFix(*fix);
}

void App::selectEngine(const std::string& name) {
    for (std::size_t i = 0; i < engines_.size(); ++i) {
        if (engines_[i].name != name) continue;
        page_            = Page::Engines;
        selected_engine_ = static_cast<int>(i);
        engine_marked_.clear();
        return;
    }
}

void App::applyFix(const Fix& fix) {
    const auto engine =
        std::find_if(engines_.begin(), engines_.end(), [&](const auto& e) { return e.name == fix.engine; });
    const bool known = engine != engines_.end();
    switch (fix.kind) {
        case Fix::Kind::OpenEngine:
            selectEngine(fix.engine);
            break;
        case Fix::Kind::LocateProgram:
        case Fix::Kind::LocateDir:
            // The dialog's answer goes to the engine selected.
            if (!known) break;
            selected_engine_ = static_cast<int>(engine - engines_.begin());
            engine_marked_.clear();
            if (fix.kind == Fix::Kind::LocateProgram) {
                dialogs_.openFile(Dialogs::Purpose::EngineCommand, {}, engine->cmd);
            } else {
                dialogs_.openFolder(Dialogs::Purpose::EngineDir, engine->dir);
            }
            break;
        case Fix::Kind::LocateBook:
            dialogs_.openFile(Dialogs::Purpose::Book,
                              {{TR("Books (EPD, PGN, Polyglot)"), "epd;pgn;bin"}, {TR("All files"), "*"}}, setup_.book);
            break;
        case Fix::Kind::LocateTablebases: {
            const auto dirs = splitTablebasePath(setup_.tb_paths);
            dialogs_.openFolder(Dialogs::Purpose::TablebasesFolder, dirs.empty() ? std::string() : dirs.front());
            break;
        }
        case Fix::Kind::LowerEngineThreads:
            if (!known) break;
            engine->setOption("Threads", std::to_string(fix.value));
            pruneOptions(*engine);  // the engine's default, when that is what it comes back to
            storage_.saveEngines(engines_);
            Console::instance().add(trf("Threads of {} set to {} in the engine library.", fix.engine, fix.value));
            break;
        case Fix::Kind::LowerParticipantThreads:
            if (fix.index >= 0 && fix.index < static_cast<int>(setup_.participants.size()) &&
                setup_.participants[fix.index].name == fix.engine) {
                setup_.participants[fix.index].threads = fix.value;
                saveSetupSoon();
            }
            break;
        case Fix::Kind::LowerCommonThreads:
            setup_.common_threads = fix.value;
            saveSetupSoon();
            break;
        case Fix::Kind::LowerTablebasePieces:
            setup_.tb_pieces = fix.value;
            saveSetupSoon();
            break;
        case Fix::Kind::OpenLog:
            page_ = Page::Log;
            break;
        case Fix::Kind::OpenFolder:
            ui::openPath(fix.path);
            break;
        case Fix::Kind::ChangeTournamentsFolder:
            dialogs_.openFolder(Dialogs::Purpose::TournamentsFolder, storage_.runsDir().string());
            break;
        case Fix::Kind::CopyToMachine:
            offerEngineCopy(fix.path, fix.engine);
            break;
        case Fix::Kind::TestMachine:
            startMachineJob(fix.path, MachineJob::Kind::Test);
            break;
        case Fix::Kind::SetRunOn:
            setup_.machine = fix.path;
            saveSetupSoon();
            break;
        case Fix::Kind::SetMargin:
            setup_.tc_margin_ms = fix.value;
            saveSetupSoon();
            break;
        case Fix::Kind::EnterPassphrase:
            passphrase_machine_ = fix.path;
            passphrase_then_    = [] {};  // only kept
            open_passphrase_    = true;
            break;
        case Fix::Kind::LinkLichess:
        case Fix::Kind::RetryAfterPause:
        case Fix::Kind::CheckDatabase:
        case Fix::Kind::ChooseEngine:
        case Fix::Kind::RegisterEngineAgain: {
            Repairs repairs(*this);
            (void)carryOutRepair(fix, repairs, retries_);
            break;
        }
    }
}

}  // namespace fcg
