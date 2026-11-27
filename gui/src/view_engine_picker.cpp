// The engine picker (#59, mockup M9): the strongest engines from their authors' GitHub releases, the build this
// processor runs downloaded, checked against its published hash, started and added to the library; the card that
// offers Gaia where an engine is first needed; the new versions of the engines taken from it. What leaves the machine:
// the catalogue read from the repository and a request to GitHub per engine, kept a day, only when the picker opens or
// an engine is added; once a day when the Engines page opens, for the library's engines from the catalogue, unless
// Settings say otherwise (R16).

#include <algorithm>
#include <chrono>
#include <ctime>
#include <string>

#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include "app.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "ui.hpp"
#include "update_check.hpp"
#include "widgets.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

const char* systemName() {
#ifdef _WIN32
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Linux";
#endif
}

// This processor in words: its name, its Zen generation, what the engines' builds look for.
std::string describeCpu(const CpuFeatures& cpu) {
    std::string brand = cpu.brand;
    while (!brand.empty() && brand.back() == ' ') brand.pop_back();
    if (brand.empty()) brand = TR("This processor");
    std::string what;
    if (cpu.arch == CpuArch::Arm64 && !cpu.x86_emulated) {
        what = "ARM64";
        if (cpu.i8mm)
            what += " " + std::string(TR("with dot product and I8MM"));
        else if (cpu.dotprod)
            what += " " + std::string(TR("with dot product"));
    } else if (const int level = cpu.x86Level(); level > 0) {
        what = fmt::format("x86-64-v{}", level);
        std::vector<std::string> extras;
        if (cpu.avx512f) extras.push_back("AVX-512");
        if (cpu.avx512vnni) extras.push_back("VNNI");
        if (cpu.avx512vbmi2) extras.push_back("VBMI2");
        if (!extras.empty()) {
            std::string list = extras.front();
            for (std::size_t i = 1; i < extras.size(); ++i)
                list += (i + 1 == extras.size() ? std::string(TR(" and ")) : std::string(", ")) + extras[i];
            what += " " + trf("with {}", list);
        }
        if (cpu.x86_emulated) what += " " + std::string(TR("(by emulation)"));
    }
    if (const int zen = cpu.zen(); zen > 0) brand += fmt::format(" (Zen {})", zen);
    return what.empty() ? fmt::format("{} · {}", brand, systemName())
                        : fmt::format("{}, {} · {}", brand, what, systemName());
}

std::string megabytes(uint64_t bytes) { return trf("{} MB", (bytes + 500'000) / 1'000'000); }

// A release's day: "today", else its date as GitHub gives it (2026-11-18).
std::string releaseDay(const std::string& published) {
    if (published.size() < 10) return published;
    const auto day        = published.substr(0, 10);
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char today[16];
    std::strftime(today, sizeof(today), "%Y-%m-%d", &utc);
    return day == today ? std::string(TR("today")) : day;
}

// The picker's primary buttons keep the same colour under the mouse until a mockup settles it (#110).
bool primaryButton(const char* label) { return ui::primaryButton(label, true, 0, ui::PrimaryHover::Flat); }

void progressBar(float fraction, float width) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float h   = px(6);
    const float y   = at.y + (ImGui::GetFrameHeight() - h) / 2;
    auto* dl        = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(at.x, y), ImVec2(at.x + width, y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), h / 2);
    if (fraction > 0) {
        dl->AddRectFilled(ImVec2(at.x, y), ImVec2(at.x + width * std::min(1.0f, fraction), y + h), ui::ACCENT, h / 2);
    }
    ImGui::Dummy(ImVec2(width, ImGui::GetFrameHeight()));
}

bool ready(auto& future) {
    return future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

}  // namespace

const CpuFeatures& App::cpu() {
    if (!cpu_) cpu_ = detectCpu();
    return *cpu_;
}

App::PickerEngine* App::pickerEngine(std::string_view id) {
    for (auto& e : picker_.engines) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

void App::readPickerCatalogue(bool fetch) {
    auto& p = picker_;
    if (!p.catalogue_read) {
        p.catalogue_read = true;
        p.catalogue      = catalogue::currentCatalogue(storage_.root(), false);
        syncPickerEngines();
    }
    // The repository's, when the one kept is more than a day old: in the background, once a session.
    if (fetch && !p.catalogue_fetched) {
        p.catalogue_fetched = true;
        p.catalogue_job     = std::async(
            std::launch::async, [profile = storage_.root()] { return catalogue::currentCatalogue(profile, true); });
    }
}

void App::syncPickerEngines() {
    auto& p = picker_;
    // The engines in the catalogue's order, each keeping what it has. Only when the catalogue is read: the engines
    // move, so that no reference to one may be held across this.
    std::vector<PickerEngine> engines;
    for (const auto& c : p.catalogue.engines) {
        if (auto* had = pickerEngine(c.id)) {
            engines.push_back(std::move(*had));
        } else {
            PickerEngine e;
            e.id = c.id;
            engines.push_back(std::move(e));
        }
    }
    // An install under way of an engine the new catalogue left out goes on, out of sight.
    for (auto& e : p.engines) {
        if (!e.id.empty() && e.install.valid()) engines.push_back(std::move(e));
    }
    p.engines = std::move(engines);
}

void App::askReleases(PickerEngine& e) {
    if (e.asked) return;
    e.asked = true;
    picker_.to_ask.push_back(e.id);
}

void App::openEnginePicker() {
    readPickerCatalogue(true);
    picker_.open = true;
    for (auto& e : picker_.engines) askReleases(e);
}

void App::pollEnginePicker() {
    auto& p = picker_;
    if (ready(p.catalogue_job)) {
        p.catalogue = p.catalogue_job.get();
        syncPickerEngines();
        for (auto& e : p.engines) {
            if (p.open) askReleases(e);
        }
    }
    if (ready(p.answer)) {
        auto answer = p.answer.get();
        if (auto* e = pickerEngine(p.asking)) {
            p.limited |= answer.limited();
            e->pick = {};
            if (const auto* engine = p.catalogue.find(e->id); engine && !answer.list.empty())
                e->pick = catalogue::pickBuild(*engine, answer.list, cpu());
            e->releases = std::move(answer);
            if (e->install_when_known) {
                e->install_when_known = false;
                installPicked(*e, e->keep_old);
            }
        }
        p.asking.clear();
    }
    while (!p.answer.valid() && !p.to_ask.empty()) {
        const auto id = p.to_ask.front();
        p.to_ask.pop_front();
        auto* e            = pickerEngine(id);
        const auto* engine = p.catalogue.find(id);
        if (!e || !engine) continue;
        // What the profile kept of the last day first; GitHub otherwise, while it takes requests.
        const auto profile = storage_.root();
        if (auto kept = catalogue::keptReleases(profile, id)) {
            e->pick     = catalogue::pickBuild(*engine, *kept, cpu());
            e->releases = catalogue::Releases(std::move(*kept));
            if (e->install_when_known) {
                e->install_when_known = false;
                installPicked(*e, e->keep_old);
            }
            continue;
        }
        if (p.limited) {  // not asked: GitHub would refuse it
            catalogue::Releases refused;
            refused.failure       = catalogue::GitHubFailure::RateLimited;
            e->releases           = std::move(refused);
            e->install_when_known = false;
            continue;
        }
        p.asking = id;
        p.answer = std::async(std::launch::async, [profile, engine = *engine, cpu = cpu()] {
            auto answer = catalogue::fetchReleases(engine, cpu);
            if (!answer.list.empty()) catalogue::keepReleases(profile, engine.id, answer.list);
            return answer;
        });
    }
    for (auto& e : p.engines) {
        if (ready(e.install)) pickerInstalled(e, e.install.get());
    }
}

void App::installPicked(PickerEngine& e, bool keep_old) {
    if (e.install.valid()) return;
    readPickerCatalogue(true);
    e.keep_old     = keep_old;
    e.result_error = false;
    e.result.clear();
    if (!e.releases) {
        e.install_when_known = true;
        askReleases(e);
        return;
    }
    const auto* engine = picker_.catalogue.find(e.id);
    if (!engine || e.pick.choices.empty()) {
        e.result       = e.releases->failed() ? catalogue::describe(*e.releases) : e.pick.message();
        e.result_error = true;
        e.for_board    = false;
        return;
    }
    e.progress = std::make_shared<DownloadProgress>();
    e.cancel   = std::make_shared<std::atomic<bool>>(false);
    e.install  = std::async(std::launch::async, [profile = storage_.root(), engine = *engine, pick = e.pick,
                                                 progress = e.progress, cancel = e.cancel] {
        return catalogue::installEngine(profile, engine, pick, progress.get(), cancel.get());
    });
}

void App::pickerInstalled(PickerEngine& e, catalogue::InstallResult r) {
    picker_.installed_read = false;  // installed.json has changed
    if (r.cancelled) {
        e.result       = TR("Stopped: what came is kept, the next try goes on from there.");
        e.result_error = false;
        e.for_board    = false;
        return;
    }
    if (!r.ok) {
        e.result       = r.error;
        e.result_error = true;
        e.for_board    = false;
        return;
    }
    const auto* engine = picker_.catalogue.find(e.id);
    // The library's engine from this entry of the catalogue: an update gives it the new program, the version before
    // kept as an engine of its own when asked.
    int index = -1;
    for (int i = 0; i < static_cast<int>(engines_.size()); ++i) {
        if (engines_[static_cast<std::size_t>(i)].catalogue == e.id &&
            engines_[static_cast<std::size_t>(i)].machine.empty()) {
            index = i;
            break;
        }
    }
    std::string before;
    if (index >= 0) {
        before = installedVersion(engines_[static_cast<std::size_t>(index)]);
        if (e.keep_old && !before.empty()) {
            EngineEntry old = engines_[static_cast<std::size_t>(index)];
            old.name        = uniqueEngineName(fmt::format("{} {}", old.name, before), -1);
            old.catalogue.clear();  // it stays at its version
            engines_.push_back(std::move(old));
        }
        auto& entry = engines_[static_cast<std::size_t>(index)];
        entry.cmd   = r.engine.program;
        entry.dir.clear();
    } else {
        EngineEntry entry;
        entry.cmd       = r.engine.program;
        entry.name      = uniqueEngineName(engine ? engine->name : r.engine.id, -1);
        entry.catalogue = e.id;
        engines_.push_back(std::move(entry));
        index = static_cast<int>(engines_.size()) - 1;
    }
    storage_.saveEngines(engines_);
    startProbe(index);
    // The version replaced, not kept as an engine of its own: deleted once no tournament that is not over plays it
    // (#65).
    if (!before.empty() && before != r.engine.version && !e.keep_old) {
        {
            const std::lock_guard lock(catalogue::installedMutex());
            auto installed = catalogue::loadInstalled(storage_.root());
            catalogue::retireVersion(installed, e.id, before, static_cast<int64_t>(std::time(nullptr)));
            (void)catalogue::saveInstalled(storage_.root(), installed);
        }
        picker_.installed_read = false;
        checkVersions({}, r.engine.version);
    }
    const auto& name = engines_[static_cast<std::size_t>(index)].name;
    e.result_error   = false;
    e.result = before.empty() || before == r.engine.version
                   ? trf("Its hash checked, it answered uci: {} is in the library.", name)
                   : trf("{} went from {} to {}: its hash checked, it answered uci.", name, before, r.engine.version);
    // From the first-need card: the engine of the analysis and of the assistant.
    if (e.for_board) {
        e.for_board = false;
        loadBoardSettings();
        board_.settings.analysis.engine = name;
        saveBoardSettings();
    }
}

std::string App::installedVersion(const EngineEntry& e) {
    if (e.catalogue.empty()) return {};
    if (!picker_.installed_read) {
        picker_.installed_read = true;
        picker_.installed      = catalogue::loadInstalled(storage_.root());
    }
    // The latest install of its program (a version installed again replaces its entry).
    const catalogue::Installed* last = nullptr;
    for (const auto& i : picker_.installed) {
        if (i.id == e.catalogue && i.program == e.cmd && (!last || i.installed >= last->installed)) last = &i;
    }
    return last ? last->version : std::string();
}

std::string App::newerVersion(const EngineEntry& e) {
    const auto have = installedVersion(e);
    if (have.empty()) return {};
    const auto* p = pickerEngine(e.catalogue);
    if (!p || p->pick.choices.empty()) return {};
    const auto& offered = p->pick.choices.front().version;
    return update::compareVersions(offered, have) > 0 ? offered : std::string();
}

void App::checkEngineUpdates() {
    auto& p = picker_;
    if (!setup_.engine_updates_daily) return;
    const auto now = std::chrono::steady_clock::now();
    if (p.updates_asked && now - p.updates_checked < std::chrono::hours(24)) return;
    bool any = false;
    for (const auto& e : engines_) any |= !e.catalogue.empty();
    if (!any) return;
    p.updates_asked   = true;
    p.updates_checked = now;
    readPickerCatalogue(false);
    for (const auto& e : engines_) {
        if (e.catalogue.empty()) continue;
        if (auto* pe = pickerEngine(e.catalogue)) {
            // A day later, asked again.
            if (pe->asked && !pe->install.valid() && p.asking != pe->id) {
                pe->asked = false;
                pe->releases.reset();
            }
            askReleases(*pe);
        }
    }
}

void App::drawEngineUpdates() {
    std::vector<std::pair<int, std::string>> updates;  // library index, the newer version
    for (int i = 0; i < static_cast<int>(engines_.size()); ++i) {
        if (auto v = newerVersion(engines_[static_cast<std::size_t>(i)]); !v.empty()) updates.emplace_back(i, v);
    }
    if (updates.empty()) return;
    ui::beginCard("engine_updates");
    ImGui::PushFont(ui::titleFont(), 0.0f);
    ImGui::TextUnformatted(trn("{} new version", "{} new versions", updates.size(), updates.size()).c_str());
    ImGui::PopFont();
    for (const auto& [i, version] : updates) {
        const auto& e = engines_[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(fmt::format("{}  {} → {}", e.name, installedVersion(e), version).c_str());
        if (auto* pe = pickerEngine(e.catalogue)) {
            ImGui::SameLine();
            if (pe->install.valid()) {
                const auto size  = pe->progress ? pe->progress->size.load() : 0;
                const auto bytes = pe->progress ? pe->progress->bytes.load() : 0;
                progressBar(size ? static_cast<float>(bytes) / static_cast<float>(size) : 0.0f, px(90));
            } else if (primaryButton(TR("Update"))) {
                installPicked(*pe);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", TR("Its new version in a folder of its own: the engine takes it once it has "
                                           "answered uci. A tournament that is not over keeps the files it started "
                                           "with."));
            }
            if (!pe->result.empty() && pe->result_error) {
                ImGui::PushTextWrapPos(0);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::BAD), "%s", pe->result.c_str());
                ImGui::PopTextWrapPos();
            }
        }
        ImGui::PopID();
    }
    ui::endCard();
}

void App::drawEnginePicker() {
    auto& p = picker_;
    // Back to the Engines page: the button, or Esc.
    if (ui::iconButton("picker_back", ui::Icon::Back, TR("Engines")) ||
        (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape) &&
         !ImGui::IsAnyItemActive())) {
        p.open = false;
        return;
    }
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.15f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Add a popular engine"));
    ImGui::PopFont();
    ui::topic(guide::topics::PopularEngines);
    ImGui::Spacing();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool aside   = avail.x >= px(1060);
    const float side_w = aside ? px(340) : 0.0f;
    const float gap    = ImGui::GetStyle().ItemSpacing.x * 2;

    ImGui::BeginChild("##picker_engines", ImVec2(aside ? avail.x - side_w - gap : 0, 0), ImGuiChildFlags_None);
    // The heading, and this processor as the builds are chosen for it.
    ImGui::PushFont(ui::titleFont(), 0.0f);
    ImGui::TextUnformatted(TR("The strongest engines, from their authors"));
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(ui::MUTED4, "%s", trf("This processor: {}", describeCpu(cpu())).c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const float width = ImGui::GetContentRegionAvail().x;
    const int columns = width >= px(720) ? 2 : 1;
    if (ImGui::BeginTable("##picker_grid", columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX)) {
        int n = 0;
        for (auto& e : p.engines) {
            const auto* engine = p.catalogue.find(e.id);
            if (!engine) continue;
            if (n++ % columns == 0) ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(e.id.c_str());
            // The library's engine from this entry, and what it has.
            const EngineEntry* have = nullptr;
            for (const auto& x : engines_) {
                if (x.catalogue == e.id && x.machine.empty()) {
                    have = &x;
                    break;
                }
            }
            const auto have_version = have ? installedVersion(*have) : std::string();
            const auto* choice      = e.pick.choices.empty() ? nullptr : &e.pick.choices.front();
            const bool newer =
                have && choice && !have_version.empty() && update::compareVersions(choice->version, have_version) > 0;
            const bool busy = e.install.valid();

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(ui::CARD_BG));
            ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(newer ? ui::ACCENT : ui::CARD_BD));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(14), px(12)));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(10));
            ImGui::BeginChild(
                "##card", ImVec2(0, 0),
                ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY,
                ImGuiWindowFlags_NoScrollbar);
            // Its name, version and date, its licence at the right; its repository.
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.08f);
            ImGui::TextUnformatted(engine->name.c_str());
            ImGui::PopFont();
            if (choice) {
                ImGui::SameLine();
                const auto* release = [&]() -> const catalogue::Release* {
                    for (const auto& r : e.releases->list) {
                        if (r.tag == choice->tag) return &r;
                    }
                    return nullptr;
                }();
                ImGui::TextColored(ui::MUTED4, "%s",
                                   release
                                       ? fmt::format("{} · {}", choice->version, releaseDay(release->published)).c_str()
                                       : choice->version.c_str());
            }
            const auto licence = choice ? choice->licence : engine->licence;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(),
                                     ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(licence.c_str()).x));
            ImGui::TextColored(ui::MUTED4, "%s", licence.c_str());
            ImGui::TextColored(ui::MUTED4, "%s", engine->repo.c_str());

            // What this processor gets, or why nothing.
            ImGui::PushTextWrapPos(0);
            if (choice) {
                ImGui::TextColored(
                    ui::MUTED4, "%s",
                    trf("For this processor: {}, {}", choice->asset.name, megabytes(choice->asset.size)).c_str());
            } else if (!e.releases) {
                ImGui::TextDisabled("%s", TR("Looking at its latest release…"));
            } else if (e.releases->failed()) {
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                                   catalogue::describe(*e.releases).c_str());
            } else {
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", e.pick.message().c_str());
            }
            // A word on it.
            std::string note;
            if (newer) {
                note = trf("Installed: {}. Version {} is out.", have_version, choice->version);
            } else if (choice && choice->asset.size >= 200'000'000) {
                note = TR("Its network is large: the longest to download.");
            } else if (choice && choice->build.needs.rfind("zen", 0) == 0 && cpu().zen() > 0) {
                note = trf("A build made for this processor, Zen {}.", cpu().zen());
            } else if (choice && choice->asset.name.find("universal") != std::string::npos) {
                note = TR("It picks this processor's code itself when it starts.");
            }
            if (!e.pick.newer.empty() && choice) {
                if (!note.empty()) note += " ";
                note += trf("Version {} is out, without a build for this processor yet.", e.pick.newer);
            }
            if (!note.empty()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", note.c_str());
            ImGui::PopTextWrapPos();

            // Its action, the download, how it ended.
            if (busy) {
                if (ImGui::Button(TR("Cancel"))) *e.cancel = true;
                ImGui::SameLine();
                const auto size  = e.progress->size.load();
                const auto bytes = e.progress->bytes.load();
                const float frac = size ? static_cast<float>(bytes) / static_cast<float>(size) : 0.0f;
                progressBar(frac, std::max(px(60), ImGui::GetContentRegionAvail().x * 0.35f));
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(
                    ui::MUTED4, "%s",
                    size && bytes >= size
                        ? TR("checked, then started…")
                        : trf("downloaded at {} %, then checked", size ? static_cast<int>(frac * 100) : 0).c_str());
            } else if (newer) {
                if (primaryButton(TR("Update"))) installPicked(e);
                ImGui::SameLine();
                if (ImGui::Button(trf("Keep {} too", have_version).c_str())) installPicked(e, true);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", TR("The new version for the engine, and the one before as an engine of "
                                               "its own: for a match between them."));
                }
            } else if (have) {
                ImGui::BeginDisabled();
                ImGui::Button(TR("In the library"));
                ImGui::EndDisabled();
            } else if (choice) {
                if (primaryButton(TR("Add"))) installPicked(e);
            } else if (e.releases && e.releases->failed() && !e.releases->limited()) {
                if (ImGui::Button(TR("Try again"))) {
                    e.asked = false;
                    e.releases.reset();
                    askReleases(e);
                }
            }
            if (!busy && !e.result.empty()) {
                ImGui::PushTextWrapPos(0);
                if (e.result_error)
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::BAD), "%s", e.result.c_str());
                else
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::GOOD), "%s", e.result.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!aside) ImGui::Spacing();
    if (aside) {
        ImGui::EndChild();
        ImGui::SameLine(0, gap);
        ImGui::BeginChild("##picker_aside", ImVec2(0, 0), ImGuiChildFlags_None);
    }

    // What happens, what leaves the machine, their licences.
    ui::beginCard("picker_steps");
    ImGui::PushFont(ui::titleFont(), 0.0f);
    ImGui::TextUnformatted(TR("What happens"));
    ImGui::PopFont();
    const char* steps[] = {
        TR("The list comes from a catalogue kept in RookStudio's repository: adding an engine to it needs no new "
           "version of the program."),
        TR("Each engine's latest release is asked of GitHub; a side release (another network, a tool) is left out."),
        TR("The file for this processor: the fastest it runs, from what it supports (AVX2, BMI2, AVX-512…)."),
        TR("Downloaded from its author's release, its SHA-256 hash checked, into the engines folder of the profile."),
        TR("Tried with \"uci\" before it is added to the library, with its name, author, licence and version."),
    };
    ImGui::PushTextWrapPos(0);
    for (int i = 0; i < 5; ++i) {
        ImGui::TextColored(ui::MUTED4, "%d.", i + 1);
        ImGui::SameLine();
        ImGui::TextUnformatted(steps[i]);
    }
    ImGui::PopTextWrapPos();
    ui::endCard();
    ImGui::Spacing();
    ui::beginCard("picker_leaves");
    ImGui::PushFont(ui::titleFont(), 0.0f);
    ImGui::TextUnformatted(TR("What leaves the machine"));
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(ui::MUTED4, "%s",
                       TR("The catalogue, read from RookStudio's repository; a request to api.github.com per engine, "
                          "kept a day; the file chosen, from github.com. Only while this page is open or an engine is "
                          "added, and once a day for the engines taken from it, unless Settings say otherwise."));
    ImGui::PopTextWrapPos();
    ui::endCard();
    ImGui::Spacing();
    ui::beginCard("picker_licences");
    ImGui::PushFont(ui::titleFont(), 0.0f);
    ImGui::TextUnformatted(TR("Their licences"));
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(ui::MUTED4, "%s",
                       TR("Each engine comes from its author's release, under its licence, shown on its card and kept "
                          "with it; RookStudio distributes none of them."));
    ImGui::PopTextWrapPos();
    ui::endCard();
    ImGui::EndChild();
}

void App::drawFirstEngineCard(const char* why) {
    readPickerCatalogue(false);
    auto* gaia         = pickerEngine("gaia");
    const auto* engine = picker_.catalogue.find("gaia");
    // Its version and size when the profile kept its releases: nothing is asked of GitHub before the click.
    if (gaia && engine && !gaia->releases) {
        if (auto kept = catalogue::keptReleases(storage_.root(), "gaia", std::chrono::hours(24 * 30))) {
            gaia->pick     = catalogue::pickBuild(*engine, *kept, cpu());
            gaia->releases = catalogue::Releases(std::move(*kept));
            gaia->asked    = true;
        }
    }
    ui::beginCard("first_engine");
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextUnformatted(TR("An engine to analyse with"));
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(ui::MUTED4, "%s", why);
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (gaia && engine) {
        const auto* choice = gaia->pick.choices.empty() ? nullptr : &gaia->pick.choices.front();
        ImGui::PushFont(ui::titleFont(), 0.0f);
        ImGui::TextUnformatted(choice ? fmt::format("{} {}", engine->name, choice->version).c_str()
                                      : engine->name.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextColored(ui::MUTED4, "· %s · %s", TR("the engine offered by default"),
                           (choice ? choice->licence : engine->licence).c_str());
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(
            ui::MUTED4, "%s",
            choice ? trf("This processor's build, {}, from its GitHub release", megabytes(choice->asset.size)).c_str()
                   : TR("This processor's build of its latest version, from its GitHub release"));
        ImGui::PopTextWrapPos();
        if (gaia->install.valid() || gaia->install_when_known) {
            if (ImGui::Button(TR("Cancel"))) {
                gaia->install_when_known = false;
                gaia->for_board          = false;
                if (gaia->cancel) *gaia->cancel = true;
            }
            ImGui::SameLine();
            const auto size  = gaia->progress && gaia->install.valid() ? gaia->progress->size.load() : 0;
            const auto bytes = gaia->progress && gaia->install.valid() ? gaia->progress->bytes.load() : 0;
            progressBar(size ? static_cast<float>(bytes) / static_cast<float>(size) : 0.0f, px(160));
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ui::MUTED4, "%s",
                               !gaia->install.valid()  ? TR("Looking at its latest release…")
                               : size && bytes >= size ? TR("checked, then started…")
                                                       : trf("downloaded at {} %, then checked",
                                                             size ? static_cast<int>(100.0 * bytes / size) : 0)
                                                             .c_str());
        } else {
            const bool enter = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                               ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::IsAnyItemActive();
            if (primaryButton(TR("Download it")) || enter) {
                gaia->for_board = true;
                installPicked(*gaia);
            }
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", TR("Enter"));
        }
        if (!gaia->result.empty() && gaia->result_error) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::BAD), "%s", gaia->result.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::Spacing();
    if (ImGui::SmallButton(TR("Another popular engine…"))) {
        page_ = Page::Engines;
        openEnginePicker();
    }
    ImGui::SameLine(0, px(16));
    if (ImGui::SmallButton(TR("An engine from your disk…"))) {
#ifdef _WIN32
        dialogs_.openFile(Dialogs::Purpose::AddEngine, {{TR("Programs"), "exe"}, {TR("All files"), "*"}});
#else
        dialogs_.openFile(Dialogs::Purpose::AddEngine);
#endif
    }
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", TR("Once downloaded, it becomes the engine of the analysis and of the assistant; the "
                                 "page goes on where it was."));
    ImGui::PopTextWrapPos();
    ui::endCard();
}

}  // namespace fcg
