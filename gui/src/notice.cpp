#include "notice.hpp"

#include <algorithm>

#include <fmt/format.h>
#include <imgui.h>

#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

Fix make(Fix::Kind kind, std::string label, std::string tooltip, std::string engine = {}, int value = 0,
         int index = -1) {
    Fix f;
    f.kind    = kind;
    f.label   = std::move(label);
    f.tooltip = std::move(tooltip);
    f.engine  = std::move(engine);
    f.value   = value;
    f.index   = index;
    return f;
}

}  // namespace

Fix openEngineFix(const std::string& engine) {
    return make(Fix::Kind::OpenEngine, TR("Open the engine"),
                trf("{} on the Engines page: its program and options", engine), engine);
}

Fix locateProgramFix(const std::string& engine) {
    return make(Fix::Kind::LocateProgram, TR("Locate…"), trf("Choose the program of {}", engine), engine);
}

Fix locateDirFix(const std::string& engine) {
    return make(Fix::Kind::LocateDir, TR("Locate…"), trf("Choose the working directory of {}", engine), engine);
}

Fix locateBookFix() { return make(Fix::Kind::LocateBook, TR("Locate…"), TR("Choose the opening book")); }

Fix locateTablebasesFix() {
    return make(Fix::Kind::LocateTablebases, TR("Locate…"),
                TR("Choose the tablebase folder of this tournament (the adjudication's, and the engines' when it "
                   "gives them its tablebases)"));
}

Fix lowerEngineThreadsFix(const std::string& engine, int threads) {
    return make(Fix::Kind::LowerEngineThreads, trf("Lower Threads to {}", threads),
                trf("Sets the Threads option of {} to {} in the engine library: every tournament plays it so, unless "
                    "its setup gives other Threads.",
                    engine, threads),
                engine, threads);
}

Fix lowerParticipantThreadsFix(const std::string& engine, int index, int threads) {
    return make(Fix::Kind::LowerParticipantThreads, trf("Lower Threads to {}", threads),
                trf("Sets the Threads of {} to {} in this tournament's participants.", engine, threads), engine,
                threads, index);
}

Fix lowerCommonThreadsFix(int threads) {
    return make(Fix::Kind::LowerCommonThreads, trf("Lower Threads to {}", threads),
                trf("Sets the Threads of this tournament's common settings to {}.", threads), {}, threads);
}

Fix lowerTablebasePiecesFix(int pieces) {
    return make(Fix::Kind::LowerTablebasePieces, trn("Lower to {} piece", "Lower to {} pieces", pieces, pieces),
                TR("The tablebase adjudication's Max pieces, in this tournament's Adjudication section."), {}, pieces);
}

Fix openLogFix() {
    return make(Fix::Kind::OpenLog, TR("Open the log"),
                TR("The Log page: fastchess' output and the program's messages"));
}

Fix openFolderFix(const std::string& path) {
    auto f = make(Fix::Kind::OpenFolder, TR("Open the folder"), path);
    f.path = path;
    return f;
}

Fix changeTournamentsFolderFix() {
    return make(Fix::Kind::ChangeTournamentsFolder, TR("Change the tournaments folder…"),
                TR("Where new tournaments are written, as in Settings. Those played so far stay where they are: Open "
                   "a folder… opens them."));
}

Fix copyToMachineFix(const std::string& engine, const std::string& machine_id, const std::string& machine_name) {
    auto f = make(Fix::Kind::CopyToMachine, trf("Copy {} to {}…", engine, machine_name),
                  trf("Settings > Machines: copies this computer's program of {} to {}, sets its path there and checks "
                      "it.",
                      engine, machine_name),
                  engine);
    f.path = machine_id;
    return f;
}

Fix testMachineFix(const std::string& machine_id, const std::string& machine_name) {
    auto f = make(Fix::Kind::TestMachine, trf("Test {}", machine_name),
                  trf("Settings > Machines: connects to {}, shows its host key and trusts it, and reads its system.",
                      machine_name));
    f.path = machine_id;
    return f;
}

Fix setRunOnFix(const std::string& machine_id, const std::string& machine_name) {
    auto f = make(Fix::Kind::SetRunOn,
                  machine_id.empty() ? std::string(TR("Run the tournament here")) : trf("Run it on {}", machine_name),
                  TR("This tournament: its Run on, in Concurrency."));
    f.path = machine_id;
    return f;
}

Fix setMarginFix(int milliseconds) {
    auto f  = make(Fix::Kind::SetMargin, trf("Set a margin of {} ms", milliseconds),
                   TR("This tournament: the time an engine may go over its clock before it loses on time (its time "
                      "control's Margin)."));
    f.value = milliseconds;
    return f;
}

Fix enterPassphraseFix(const std::string& machine_id) {
    auto f =
        make(Fix::Kind::EnterPassphrase, TR("Enter it now…"), TR("Kept in memory until RookStudio ends, never saved."));
    f.path = machine_id;
    return f;
}

std::string allowScopeLabel(std::string_view scope) {
    // What each permission lets RookStudio do, as the user asks for it.
    if (scope == "study:read") return TR("Allow reading studies…");
    if (scope == "study:write") return TR("Allow writing studies…");
    if (scope == "preference:write") return TR("Allow bookmarking games…");
    if (scope == "board:play") return TR("Allow playing on Lichess…");
    if (scope == "challenge:read") return TR("Allow reading challenges…");
    if (scope == "follow:read") return TR("Allow reading whom you follow…");
    if (scope == "team:read") return TR("Allow reading your teams…");
    if (scope == "tournament:write") return TR("Allow making tournaments…");
    if (scope == "engine:read" || scope == "engine:write") return TR("Allow registering engines…");
    if (scope == "puzzle:read" || scope == "puzzle:write") return TR("Allow reading puzzles…");
    return trf("Allow {}…", scope);
}

std::string pauseClock(std::chrono::milliseconds pause) {
    const auto s = std::chrono::ceil<std::chrono::seconds>(std::max(pause, std::chrono::milliseconds(0))).count();
    return fmt::format("{}:{:02}", s / 60, s % 60);
}

Fix linkLichessFix(const std::string& scope, bool linked) {
    Fix f;
    f.kind = Fix::Kind::LinkLichess;
    f.path = scope;
    if (!linked) {
        f.label   = TR("Link the Lichess account…");
        f.tooltip = scope.empty() ? std::string(TR("Opens lichess.org in your browser to link an account, without "
                                                   "giving RookStudio a password. Nothing is sent before you agree "
                                                   "there."))
                                  : trf("Opens lichess.org in your browser to link an account, asking for {}, without "
                                        "giving RookStudio a password. Nothing is sent before you agree there.",
                                        scope);
    } else if (!scope.empty()) {
        f.label = allowScopeLabel(scope);
        f.tooltip =
            trf("Opens lichess.org in your browser, asking also for {}. Nothing is sent before you agree "
                "there; your account stays linked meanwhile.",
                scope);
    } else {
        f.label = TR("Link Lichess again…");
        f.tooltip =
            TR("Opens lichess.org in your browser to link the account again. Nothing is sent before you agree "
               "there.");
    }
    return f;
}

Fix retryAfterPauseFix(Retry what, const std::string& host, std::chrono::milliseconds pause, bool queued) {
    Fix f;
    f.kind          = Fix::Kind::RetryAfterPause;
    f.value         = static_cast<int>(what);
    f.path          = host;
    const auto site = host.empty() ? std::string(TR("the site")) : host;
    if (pause.count() > 0 && queued) {
        f.label    = trf("Asks again in {}", pauseClock(pause));
        f.tooltip  = trf("Asks {} again by itself once its pause is over; nothing is sent before.", site);
        f.disabled = true;
    } else if (pause.count() > 0) {
        f.label   = trf("Try again in {}", pauseClock(pause));
        f.tooltip = trf("Asks {} again once its pause is over; nothing is sent before.", site);
    } else {
        f.label   = TR("Try again");
        f.tooltip = trf("Asks {} again what was asked; nothing else is sent.", site);
    }
    return f;
}

Fix checkDatabaseFix(int database) {
    auto f  = make(Fix::Kind::CheckDatabase, TR("Check the database…"),
                   TR("Opens the database's care at its checks, which find what can be kept. Nothing leaves the "
                      "machine."));
    f.index = database;
    return f;
}

Fix chooseEngineFix() {
    return make(Fix::Kind::ChooseEngine, TR("Choose an engine…"),
                TR("Opens the engine picker of the Board: an engine of this machine analyses, nothing leaves it."));
}

Fix registerEngineAgainFix(const std::string& engine) {
    return make(Fix::Kind::RegisterEngineAgain, TR("Register it again"),
                trf("Registers {} with Lichess again, under the same name and settings: they go to lichess.org, the "
                    "engine stays on this machine.",
                    engine),
                engine);
}

const char* noticeTopic(const Notice& notice) {
    if (notice.topic) return notice.topic;
    const char* topic = nullptr;
    for (const auto& f : notice.fixes) {
        switch (f.kind) {
            case Fix::Kind::OpenEngine:
            case Fix::Kind::LocateProgram:
            case Fix::Kind::LocateDir:
            case Fix::Kind::CopyToMachine:
                if (!topic) topic = guide::topics::TroubleEngine;
                break;
            case Fix::Kind::LowerEngineThreads:
            case Fix::Kind::LowerParticipantThreads:
            case Fix::Kind::LowerCommonThreads:
            case Fix::Kind::LocateTablebases:
            case Fix::Kind::LowerTablebasePieces:
                return guide::topics::TroubleGames;
            case Fix::Kind::LocateBook:
                if (!topic) topic = guide::topics::Openings;
                break;
            case Fix::Kind::ChangeTournamentsFolder:
                if (!topic) topic = guide::topics::TournamentsPage;
                break;
            case Fix::Kind::OpenLog:
                if (!topic) topic = guide::topics::Troubleshooting;
                break;
            case Fix::Kind::OpenFolder:
                break;
            case Fix::Kind::TestMachine:
            case Fix::Kind::SetRunOn:
            case Fix::Kind::EnterPassphrase:
            case Fix::Kind::SetMargin:
                if (!topic) topic = guide::topics::RemoteEngines;
                break;
            case Fix::Kind::LinkLichess:
                if (!topic && f.path.empty()) topic = guide::topics::LichessLink;
                break;
            case Fix::Kind::CheckDatabase:
                if (!topic) topic = guide::topics::Maintenance;
                break;
            case Fix::Kind::RetryAfterPause:
            case Fix::Kind::ChooseEngine:
            case Fix::Kind::RegisterEngineAgain:
                break;
        }
    }
    return topic;
}

namespace {

// The fixes of `notice` from `text_x` on, then the `?` of the guide's section that explains it; the fix clicked.
const Fix* drawFixLine(const Notice& notice, const char* id, const char* topic, float text_x) {
    const Fix* clicked = nullptr;
    ImGui::PushID(id);
    ImGui::SetCursorPosX(text_x);
    for (std::size_t i = 0; i < notice.fixes.size(); ++i) {
        const auto& fix = notice.fixes[i];
        if (i > 0) ui::sameLineOrWrap(fix.label.c_str());
        if (ImGui::GetCursorPosX() < text_x) ImGui::SetCursorPosX(text_x);
        ImGui::PushID(static_cast<int>(i));
        // A disabled fix (a retry waiting for its host's pause) keeps its place and its tooltip.
        ImGui::BeginDisabled(fix.disabled);
        if (ImGui::SmallButton(fix.label.c_str()) && !fix.disabled) clicked = &fix;
        ImGui::EndDisabled();
        ImGui::PopID();
        if (!fix.tooltip.empty() &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", fix.tooltip.c_str());
    }
    if (topic) {
        if (!notice.fixes.empty()) ImGui::SameLine();
        ui::guideLink("notice_guide", topic);
    }
    ImGui::PopID();
    return clicked;
}

}  // namespace

const Fix* drawNotice(const Notice& notice, const char* id) {
    const bool ok    = notice.level == Notice::Level::Ok;
    const bool error = notice.level == Notice::Level::Error;
    ImGui::PushStyleColor(ImGuiCol_Text, ok ? ui::GOOD : error ? ui::BAD : ui::WARN);
    ImGui::TextUnformatted(ok ? "✓" : "!");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    const float text_x = ImGui::GetCursorPosX();
    if (error) ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
    ImGui::TextWrapped("%s", notice.text.c_str());
    if (error) ImGui::PopStyleColor();
    if (!notice.detail.empty() && ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
        ImGui::TextUnformatted(notice.detail.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    const char* topic = noticeTopic(notice);
    if (notice.fixes.empty() && !topic) return nullptr;
    // The fixes under the text, from where it starts.
    return drawFixLine(notice, id, topic, text_x);
}

const Fix* drawFixes(const Notice& notice, const char* id) {
    const char* topic = noticeTopic(notice);
    if (notice.fixes.empty() && !topic) return nullptr;
    return drawFixLine(notice, id, topic, ImGui::GetCursorPosX());
}

bool drawNotices(const std::vector<Notice>& notices, const char* id, Fix& clicked) {
    bool any = false;
    ImGui::PushID(id);
    for (std::size_t i = 0; i < notices.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (const auto* fix = drawNotice(notices[i], "notice"); fix && !any) {
            clicked = *fix;
            any     = true;
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    return any;
}

}  // namespace fcg
