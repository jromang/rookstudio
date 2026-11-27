// An engine's old versions (#65, decided in M9, point 5; mockup M13, board 9): each version of an engine taken from the
// picker in its own folder of the profile; once an update has replaced one, its folder deleted when no tournament that
// is not over (playing, paused, stopped) plays it, checked again at the end of each tournament (engine_versions.hpp).
// The card lists the versions on the disk with their state: the library's, one a tournament holds ("Keep … too"
// keeps it for good), one kept on purpose ("Stop keeping it"), one from before (deleted on the user's word only), one
// deleted that a tournament asks for again (downloaded again, its hash checked). A notice at the window's corner says
// each version deleted.

#include <algorithm>
#include <chrono>
#include <ctime>
#include <set>

#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <tour/util.hpp>

#include "app.hpp"
#include "console.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "ui.hpp"
#include "widgets.hpp"

namespace fcg {

namespace {

using SteadyClock = std::chrono::steady_clock;
namespace fs      = std::filesystem;

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

void tip(const std::string& text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

using ui::mix;

// A size as the picker writes it: "118 MB".
std::string megabytes(uint64_t bytes) { return trf("{} MB", (bytes + 500'000) / 1'000'000); }

std::string sameDir(const fs::path& p) {
    std::error_code ec;
    auto a = fs::absolute(p, ec);
    return (ec ? p : a).lexically_normal().generic_string();
}

// Where a tournament is: "playing, game 212 of 400".
std::string progressText(const catalogue::ProgramUser& u) {
    const auto game = u.played + 1;
    if (u.total && *u.total > u.played) {
        if (u.playing && !u.paused) return trf("playing, game {} of {}", game, *u.total);
        if (u.playing) return trf("paused, game {} of {}", game, *u.total);
        return trf("stopped, game {} of {}", game, *u.total);
    }
    const auto played = static_cast<int>(std::min<uint64_t>(u.played, 1000000000));
    if (u.playing && !u.paused) return trn("playing, {} game played", "playing, {} games played", played, played);
    if (u.playing) return trn("paused, {} game played", "paused, {} games played", played, played);
    return trn("stopped, {} game played", "stopped, {} games played", played, played);
}

catalogue::ProgramUser userOf(const tour::Summary& s) {
    catalogue::ProgramUser u;
    u.name   = s.spec.name;
    u.dir    = s.dir;
    u.played = s.played;
    u.total  = s.total;
    for (const auto& p : s.spec.participants) u.programs.push_back(p.engine.cmd);
    return u;
}

}  // namespace

std::vector<catalogue::ProgramUser> App::programUsers() const {
    std::vector<catalogue::ProgramUser> out;
    std::set<std::string> seen;
    // Those open, their state from their process.
    for (const auto& r : runs_) {
        const auto* t = r.run->tournament();
        if (!t || t->finished()) continue;
        catalogue::ProgramUser u;
        u.name    = t->spec().name;
        u.dir     = t->dir();
        u.playing = r.run->running();
        u.paused  = r.run->paused();
        for (const auto& result : t->results()) u.played += result.voided ? 0 : 1;
        for (const auto& p : t->spec().participants) u.programs.push_back(p.engine.cmd);
        if (seen.insert(sameDir(u.dir)).second) out.push_back(std::move(u));
    }
    // The tournaments' folder's, as last read.
    for (const auto& s : tournaments_) {
        if (s.finished || !seen.insert(sameDir(s.dir)).second) continue;
        out.push_back(userOf(s));
    }
    return out;
}

void App::checkVersions(std::string ended, std::string replaced) {
    if (!ended.empty()) version_check_ended_ = std::move(ended);
    if (!replaced.empty()) version_check_replaced_ = std::move(replaced);
    // One at a time, and not while an install writes installed.json.
    const bool installing = std::any_of(picker_.engines.begin(), picker_.engines.end(),
                                        [](const PickerEngine& e) { return e.install.valid(); });
    if (version_check_ || installing || version_download_) {
        version_check_again_ = true;
        return;
    }
    std::vector<catalogue::ProgramUser> open;
    for (const auto& r : runs_) {
        const auto* t = r.run->tournament();
        if (!t || t->finished()) continue;
        catalogue::ProgramUser u;
        u.name    = t->spec().name;
        u.dir     = t->dir();
        u.playing = r.run->running();
        u.paused  = r.run->paused();
        for (const auto& p : t->spec().participants) u.programs.push_back(p.engine.cmd);
        open.push_back(std::move(u));
    }
    // The engines serving lichess.org (#96) hold theirs too, without a name, until they start again with the new
    // version.
    catalogue::ProgramUser engines;
    for (const auto& [name, s] : services_)
        if (s.engine.machine.empty()) engines.programs.push_back(s.engine.cmd);
    if (!engines.programs.empty()) open.push_back(std::move(engines));
    VersionCheck c;
    c.ended    = std::exchange(version_check_ended_, std::string());
    c.replaced = std::exchange(version_check_replaced_, std::string());
    c.result   = std::async(
        std::launch::async, [profile = storage_.root(), root = storage_.runsDir(), cache = version_summaries_,
                             library = engines_, users = std::move(open)]() mutable {
            // The tournaments not over: those open, and those of the tournaments' folder (a stopped one holds its
            // versions too).
            std::set<std::string> seen;
            for (const auto& u : users) seen.insert(sameDir(u.dir));
            for (const auto& s : cache->list(root))
                if (!s.finished && seen.insert(sameDir(s.dir)).second) users.push_back(userOf(s));
            const std::lock_guard lock(catalogue::installedMutex());
            auto installed     = catalogue::loadInstalled(profile);
            const auto before  = nlohmann::json(installed).dump();
            const auto deleted = catalogue::deleteRetired(profile, installed, library, users, nowSeconds());
            if (nlohmann::json(installed).dump() != before) (void)catalogue::saveInstalled(profile, installed);
            return deleted;
        });
    version_check_ = std::move(c);
}

void App::pollEngineVersions() {
    const auto now = SteadyClock::now();
    if (auto action = std::exchange(version_action_, std::nullopt)) {
        if (action->keep)
            keepVersion(action->version);
        else
            dropVersion(action->version);
    }
    // Once at the start: a version a tournament held may have been freed while RookStudio was closed.
    if (!versions_checked_ && launch_checked_) {
        versions_checked_    = true;
        const auto installed = catalogue::loadInstalled(storage_.root());
        if (std::any_of(installed.begin(), installed.end(),
                        [](const catalogue::Installed& i) { return i.retired > 0 && i.deleted == 0; }))
            checkVersions();
    }
    if (version_check_ && isReady(version_check_->result)) {
        const auto deleted = version_check_->result.get();
        const auto ended   = version_check_->ended;
        const auto brought = version_check_->replaced;
        version_check_.reset();
        if (!deleted.empty()) {
            picker_.installed_read = false;
            versions_shown_.reset();
        }
        readPickerCatalogue(false);
        for (const auto& d : deleted) {
            const auto* engine = picker_.catalogue.find(d.id);
            const auto label   = fmt::format("{} {}", engine ? engine->name : d.id, d.version);
            VersionNotice n;
            n.since = now;
            for (const auto& e : engines_)
                if (e.catalogue == d.id && e.machine.empty()) n.engine = e.name;
            if (!d.error.empty()) {
                n.text    = trf("{} could not be deleted: {}", label, d.error);
                n.warning = true;
            } else if (!d.held_by.empty() || !ended.empty()) {
                n.text = trf("{} deleted: “{}” is over · {} freed", label, d.held_by.empty() ? ended : d.held_by,
                             megabytes(d.freed));
            } else if (!brought.empty()) {
                n.text = trf("{} deleted: replaced by {} · {} freed", label, brought, megabytes(d.freed));
            } else {
                n.text = trf("{} deleted · {} freed", label, megabytes(d.freed));
            }
            Console::instance().add(n.text, n.warning);
            version_notices_.push_back(std::move(n));
        }
    }
    if (version_check_again_ && !version_check_) {
        const bool installing = std::any_of(picker_.engines.begin(), picker_.engines.end(),
                                            [](const PickerEngine& e) { return e.install.valid(); });
        if (!installing && !version_download_) {
            version_check_again_ = false;
            checkVersions();
        }
    }
    // A version downloaded again for a tournament that asks for it.
    if (version_download_ && isReady(version_download_->result)) {
        auto r             = version_download_->result.get();
        const auto* engine = picker_.catalogue.find(version_download_->id);
        const auto label =
            fmt::format("{} {}", engine ? engine->name : version_download_->id, version_download_->version);
        version_download_.reset();
        picker_.installed_read = false;
        versions_shown_.reset();
        version_download_file_ = r.ok || r.cancelled ? std::filesystem::path() : r.net.path;
        if (r.ok) {
            version_download_error_.clear();
            Console::instance().add(trf("{} downloaded again: its hash checked, it answered uci.", label));
        } else {
            version_download_error_ =
                r.cancelled ? std::string(TR("Stopped: the next try goes on from there.")) : r.error;
        }
    }
    std::erase_if(version_notices_, [&](const VersionNotice& n) { return now - n.since > std::chrono::seconds(30); });
}

void App::keepVersion(const catalogue::DiskVersion& v) {
    const auto it      = std::find_if(engines_.begin(), engines_.end(), [&](const EngineEntry& e) {
        return e.catalogue == v.installed.id && e.machine.empty();
    });
    EngineEntry kept   = it != engines_.end() ? *it : EngineEntry{};
    const auto* engine = picker_.catalogue.find(v.installed.id);
    kept.name          = uniqueEngineName(
        fmt::format("{} {}", it != engines_.end() ? it->name : (engine ? engine->name : v.installed.id),
                    v.installed.version),
        -1);
    kept.cmd = v.installed.program;
    kept.dir.clear();
    kept.catalogue.clear();  // it stays at its version
    engines_.push_back(std::move(kept));
    storage_.saveEngines(engines_);
    startProbe(static_cast<int>(engines_.size()) - 1);
    {
        const std::lock_guard lock(catalogue::installedMutex());
        auto installed = catalogue::loadInstalled(storage_.root());
        catalogue::reviveVersion(installed, v.installed.id, v.installed.version);
        (void)catalogue::saveInstalled(storage_.root(), installed);
    }
    picker_.installed_read = false;
    versions_shown_.reset();
}

void App::dropVersion(const catalogue::DiskVersion& v) {
    // The engines of its own leave the library (not the catalogue's, which plays another version).
    std::vector<std::string> names;
    for (const auto& e : engines_)
        if (e.catalogue.empty() && e.machine.empty() && catalogue::inFolder(e.cmd, v.folder)) names.push_back(e.name);
    if (!names.empty()) removeEngines(names);
    {
        const std::lock_guard lock(catalogue::installedMutex());
        auto installed = catalogue::loadInstalled(storage_.root());
        catalogue::retireVersion(installed, v.installed.id, v.installed.version, nowSeconds());
        (void)catalogue::saveInstalled(storage_.root(), installed);
    }
    picker_.installed_read = false;
    versions_shown_.reset();
    checkVersions();
}

void App::drawEngineVersions(const EngineEntry& e) {
    if (e.catalogue.empty() || !e.machine.empty()) return;
    const auto now = SteadyClock::now();
    if (!versions_shown_ || versions_shown_->id != e.catalogue || now - versions_shown_->at > std::chrono::seconds(3)) {
        refreshTournaments();
        VersionsShown shown;
        shown.id        = e.catalogue;
        shown.at        = now;
        shown.versions  = catalogue::diskVersions(storage_.root(), catalogue::loadInstalled(storage_.root()),
                                                  e.catalogue, engines_, programUsers());
        versions_shown_ = std::move(shown);
    }
    const auto versions = versions_shown_->versions;  // a copy: an action reads it again
    // The library's version alone: nothing to say.
    if (versions.size() < 2 && std::none_of(versions.begin(), versions.end(), [](const catalogue::DiskVersion& v) {
            return v.state != catalogue::VersionState::Current;
        }))
        return;
    const ui::TopicRegion topic(guide::topics::EngineVersions);
    ImGui::Spacing();
    ImGui::TextUnformatted(TR("Its versions on the disk"));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("· one per folder of the profile"));
    const auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##engine_versions", 3, flags)) return;
    ImGui::TableSetupColumn("##version", ImGuiTableColumnFlags_WidthFixed, px(64));
    ImGui::TableSetupColumn("##state", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("##action", ImGuiTableColumnFlags_WidthFixed);
    for (const auto& v : versions) {
        const auto& i = v.installed;
        ImGui::PushID(i.version.c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
        ImGui::TextUnformatted(i.version.c_str());
        ImGui::PopFont();
        ImGui::TableNextColumn();
        ImGui::PushTextWrapPos(0.0f);
        const bool retired = i.retired > 0;
        std::string state;
        switch (v.state) {
            case catalogue::VersionState::Current:
                state =
                    TR("In the library: the one the analysis, new tournaments, the bot and the service for "
                       "Lichess take.");
                break;
            case catalogue::VersionState::Kept:
                state = TR("Kept on purpose (“Keep … too”): never deleted.");
                break;
            case catalogue::VersionState::Held:
                if (v.tournaments.size() == 1) {
                    const auto& u = v.tournaments.front();
                    state = retired ? trf("Kept while “{}” plays it ({}); deleted at its end, {} freed.", u.name,
                                          progressText(u), megabytes(v.bytes))
                                    : trf("Played by “{}” ({}).", u.name, progressText(u));
                } else {
                    const auto n = static_cast<int>(v.tournaments.size());
                    state        = retired ? trn("Kept while {} tournament plays it; deleted at the end of the last "
                                                 "one, {} freed.",
                                                 "Kept while {} tournaments play it; deleted at the end of the last "
                                                 "one, {} freed.",
                                                 n, n, megabytes(v.bytes))
                                           : trn("Played by {} tournament that is not over.",
                                                 "Played by {} tournaments that are not over.", n, n);
                }
                break;
            case catalogue::VersionState::Unused:
                state = trf("Played by nothing: kept from before until you delete it ({}).", megabytes(v.bytes));
                break;
            case catalogue::VersionState::Retired:
                state = TR("Played by nothing: being deleted.");
                break;
            case catalogue::VersionState::Asked:
                state = trf("Deleted; “{}” asks for it again: downloaded again from its release, its hash checked.",
                            v.tournaments.empty() ? std::string() : v.tournaments.front().name);
                break;
        }
        ImGui::TextUnformatted(state.c_str());
        if (v.state == catalogue::VersionState::Held && v.tournaments.size() > 1) {
            std::string names;
            for (const auto& u : v.tournaments)
                names += (names.empty() ? "" : "\n") + fmt::format("“{}” ({})", u.name, progressText(u));
            tip(names);
        }
        if (v.state == catalogue::VersionState::Asked && version_download_ && version_download_->id == i.id &&
            version_download_->version == i.version) {
            const auto size  = version_download_->progress->size.load();
            const auto bytes = version_download_->progress->bytes.load();
            ImGui::ProgressBar(size ? static_cast<float>(bytes) / static_cast<float>(size) : 0.0f, ImVec2(px(160), 0));
        } else if (v.state == catalogue::VersionState::Asked && !version_download_error_.empty()) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", version_download_error_.c_str());
            ui::pathTooltip(version_download_file_);  // the file, out of the sentence (#141)
        }
        ImGui::PopTextWrapPos();
        ImGui::TableNextColumn();
        const auto armed = v.folder.generic_string();
        switch (v.state) {
            case catalogue::VersionState::Held:
                if (ImGui::SmallButton(trf("Keep {} too", i.version).c_str())) version_action_ = VersionAction{true, v};
                tip(
                    trf("{} stays as an engine of its own in the library, for a match against the new version: never "
                        "deleted.",
                        i.version));
                break;
            case catalogue::VersionState::Kept:
                if (ImGui::SmallButton(version_armed_ == armed ? TR("Stop keeping it: click again")
                                                               : TR("Stop keeping it"))) {
                    if (version_armed_ == armed) {
                        version_armed_.clear();
                        version_action_ = VersionAction{false, v};
                    } else {
                        version_armed_ = armed;
                    }
                }
                {
                    std::string names;
                    for (const auto& n : v.engines) names += (names.empty() ? "" : ", ") + fmt::format("“{}”", n);
                    tip(
                        trf("{} leaves the library, and the version's folder is deleted once no tournament that is "
                            "not over plays it ({}). A second click does it.",
                            names, megabytes(v.bytes)));
                }
                break;
            case catalogue::VersionState::Unused:
                if (ImGui::SmallButton(version_armed_ == armed ? TR("Delete it: click again") : TR("Delete it"))) {
                    if (version_armed_ == armed) {
                        version_armed_.clear();
                        version_action_ = VersionAction{false, v};
                    } else {
                        version_armed_ = armed;
                    }
                }
                tip(
                    trf("Its folder deleted from the profile, {} freed; it can be downloaded again from its release. A "
                        "second click does it.",
                        megabytes(v.bytes)));
                break;
            case catalogue::VersionState::Asked: {
                readPickerCatalogue(false);
                const auto* engine = picker_.catalogue.find(i.id);
                const auto pick    = engine ? catalogue::pickAgain(*engine, i) : std::nullopt;
                ImGui::BeginDisabled(!pick || version_download_.has_value());
                if (ImGui::SmallButton(TR("Download it again")) && pick) {
                    VersionDownload d;
                    d.id      = i.id;
                    d.version = i.version;
                    d.result = std::async(std::launch::async, [profile = storage_.root(), engine = *engine,
                                                               pick = *pick, progress = d.progress, cancel = d.cancel] {
                        return catalogue::installEngine(profile, engine, pick, progress.get(), cancel.get());
                    });
                    version_download_error_.clear();
                    version_download_file_.clear();
                    version_download_ = std::move(d);
                }
                ImGui::EndDisabled();
                tip(pick ? TR("From its release on GitHub, checked against the hash it had: the tournament plays it "
                              "again.")
                         : TR("The catalogue no longer names its file: it cannot be downloaded again."));
                break;
            }
            default:
                break;
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void App::drawVersionNotice() {
    if (version_notices_.empty()) return;
    const auto& n        = version_notices_.front();
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - px(18),
                                   viewport->WorkPos.y + viewport->WorkSize.y - px(18)),
                            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(px(300), 0), ImVec2(px(440), FLT_MAX));
    const ImU32 tone = n.warning ? ui::WARN : ui::GOOD;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BG, tone, 0.10f)));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(tone));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(14), px(12)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(10));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    if (ImGui::Begin("##version_notice", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking)) {
        const ui::TopicRegion topic(guide::topics::EngineVersions);
        ImGui::PushTextWrapPos(px(420));
        ui::inlineIcon(n.warning ? ui::Icon::Alert : ui::Icon::Check, tone);
        ImGui::SameLine();
        ImGui::TextUnformatted(n.text.c_str());
        if (!n.warning)
            ImGui::TextDisabled("%s", TR("Downloaded again from its engine's card, its hash checked, if a tournament "
                                         "asks for it again."));
        bool close = false;
        if (!n.engine.empty() && ImGui::SmallButton(TR("The engine's card##version_notice"))) {
            page_ = Page::Engines;
            for (int i = 0; i < static_cast<int>(engines_.size()); ++i)
                if (engines_[static_cast<std::size_t>(i)].name == n.engine) selected_engine_ = i;
            close = true;
        }
        if (!n.engine.empty()) ImGui::SameLine();
        if (ImGui::SmallButton(TR("OK##version_notice"))) close = true;
        ImGui::PopTextWrapPos();
        if (close) version_notices_.erase(version_notices_.begin());
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

}  // namespace fcg
