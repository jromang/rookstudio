// A book from the positions Lichess's players had analysed (#53, mockup M8, board "Moteurs"): Lichess's evaluations
// database read once, its size said before, then the book made from what was kept, as often as wanted.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <future>
#include <set>

#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include "app.hpp"
#include "console.hpp"
#include "i18n.hpp"
#include "lichess_months.hpp"
#include "machines.hpp"
#include "ui.hpp"
#include "widgets.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

// A tooltip for the item just drawn, also when it is disabled, its lines wrapped.
void tip(const char* text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

}  // namespace

std::filesystem::path App::evalsFolderPath() const { return storage_.root() / "lichess-cache" / "evaluations"; }

void App::openEvaluatedSuite() {
    evaluated_suite_shown_ = true;
    opening_suite_shown_   = false;
    book_build_shown_      = false;
    evals_folder_          = evalsFolder(evalsFolderPath());
    if (evals_folder_->done && !evaluated_positions_ && !evaluated_job_.valid())
        evaluated_job_ = std::async(std::launch::async, evaluatedPositions, evalsFolderPath());
    // Its size, said before anything is downloaded: asked of the server by the file's first byte.
    if (!evals_folder_->done && !evals_size_ && !evals_size_job_.valid())
        evals_size_job_ = std::async(std::launch::async, [] { return evalsSize(lichess::evalsUrl()); });
}

void App::startEvalsReading(std::string source) {
    evals_stopped_.clear();
    evals_reading_.start(std::move(source), evalsFolderPath());
}

void App::pollEvalsReading() {
    using namespace std::chrono_literals;
    if (isReady(evals_size_job_)) evals_size_ = evals_size_job_.get();
    if (isReady(evaluated_job_)) {
        evaluated_positions_ = evaluated_job_.get();
        evaluated_suite_preview_.reset();
    }
    const auto why = evals_reading_.finished();
    if (!why) return;
    evals_folder_ = evalsFolder(evalsFolderPath());
    if (why->empty()) {
        Console::instance().add(trf("Lichess's evaluations read: {} positions kept of {}",
                                    ui::exactCount(evals_folder_->kept), ui::exactCount(evals_folder_->lines)));
        evaluated_positions_.reset();
        evaluated_job_ = std::async(std::launch::async, evaluatedPositions, evalsFolderPath());
    } else if (*why != "cancelled") {
        evals_stopped_ = *why;
        Console::instance().add(trf("Lichess's evaluations: the reading stopped: {}", *why), true);
    }
}

void App::drawEvaluatedSuite() {
    const ui::TopicRegion topic(guide::topics::EvaluatedSuite);
    if (!evals_folder_) evals_folder_ = evalsFolder(evalsFolderPath());
    const auto& folder = *evals_folder_;
    auto& o            = evaluated_suite_;
    const ui::TextWrap wrap(0);
    ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize() * 1.1f);
    ImGui::TextUnformatted(TR("A book from Lichess's evaluated positions"));
    ImGui::PopFont();
    ImGui::TextDisabled("%s", TR("The positions Lichess's players had analysed, with their evaluation and its depth: "
                                 "those of the named openings' lines and one move off them, for an engine "
                                 "tournament. Lichess's evaluations database (CC0) is read once for them; what it "
                                 "keeps stays in the profile."));
    ImGui::Spacing();

    if (evals_reading_.busy()) {
        const auto& p     = evals_reading_.progress();
        const auto size   = p.size.load();
        const auto share  = size ? static_cast<float>(static_cast<double>(p.bytes) / static_cast<double>(size)) : 0.f;
        const auto amount = size ? trf("{} of {}", formatBytes(p.bytes), formatBytes(size))
                                 : (p.bytes ? formatBytes(p.bytes) : std::string(TR("Connecting…")));
        ImGui::ProgressBar(share, ImVec2(ImGui::GetFontSize() * 22, 0), amount.c_str());
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel##evals_reading"))) evals_reading_.cancel();
        ImGui::TextUnformatted(
            trf("{} positions read · {} kept", ui::exactCount(p.lines), ui::exactCount(p.kept)).c_str());
        ImGui::TextDisabled("%s", TR("Cancel keeps what was read: the reading goes on from there later, even after "
                                     "the program was closed."));
        return;
    }

    if (!folder.done) {
        if (folder.stopped_at > 0) {
            // A reading cut short: on from where it stopped, or from the start.
            ImGui::TextUnformatted(
                folder.size
                    ? trf("The reading stopped at {} of {}.", formatBytes(folder.stopped_at), formatBytes(folder.size))
                          .c_str()
                    : trf("The reading stopped at {}.", formatBytes(folder.stopped_at)).c_str());
            if (!evals_stopped_.empty())
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", evals_stopped_.c_str());
            if (ImGui::Button(TR("Go on"))) startEvalsReading(folder.source);
            tip(TR("Downloads and reads the rest, from where it stopped."));
            ImGui::SameLine();
            if (ImGui::Button(TR("Start again"))) {
                std::error_code ec;
                fs::remove(evalsFolderPath() / "reading.json", ec);
                fs::remove(evalsFolderPath() / "reading.tsv", ec);
                evals_folder_ = evalsFolder(evalsFolderPath());
                evals_stopped_.clear();
                if (!evals_size_ && !evals_size_job_.valid())
                    evals_size_job_ = std::async(std::launch::async, [] { return evalsSize(lichess::evalsUrl()); });
            }
        } else {
            if (evals_size_ && !evals_size_->failed()) {
                ImGui::TextUnformatted(
                    (evals_size_->date.empty()
                         ? trf("Lichess's evaluations database: {}.", formatBytes(evals_size_->bytes))
                         : trf("Lichess's evaluations database: {}, of {}.", formatBytes(evals_size_->bytes),
                               evals_size_->date))
                        .c_str());
            } else if (evals_size_) {
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                                   trf("Its size is not known: {}", lichess::describe(*evals_size_)).c_str());
            } else {
                ImGui::TextDisabled("%s", TR("Asking database.lichess.org for its size…"));
            }
            ImGui::TextDisabled("%s", TR("It is read while it downloads and is not kept: only the positions found "
                                         "stay, a few megabytes. A download cut short goes on from where it "
                                         "stopped."));
            ImGui::TextDisabled("%s", TR("What leaves the machine: the request for the file, nothing else."));
            ImGui::Spacing();
            const auto label = evals_size_ && !evals_size_->failed()
                                   ? trf("Download and read ({})", formatBytes(evals_size_->bytes))
                                   : std::string(TR("Download and read"));
            if (ImGui::Button(label.c_str())) startEvalsReading(lichess::evalsUrl());
            ImGui::SameLine();
            if (ImGui::Button(TR("Read a file already downloaded…")))
                dialogs_.openFile([this](const std::string& path) { startEvalsReading(path); },
                                  {{"zstd", "zst"}, {TR("All files"), "*"}});
            tip(
                TR("lichess_db_eval.jsonl.zst, from database.lichess.org: read from the disk, nothing is "
                   "downloaded."));
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Cancel##evals_form"))) evaluated_suite_shown_ = false;
        return;
    }

    // Read: the book's choices.
    ImGui::TextDisabled(
        "%s", (folder.date.empty() ? trf("{} positions found among the {} of Lichess's evaluations.",
                                         ui::exactCount(folder.kept), ui::exactCount(folder.lines))
                                   : trf("{} positions found among the {} of Lichess's evaluations of {}.",
                                         ui::exactCount(folder.kept), ui::exactCount(folder.lines), folder.date))
                  .c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Read again"))) {
        std::error_code ec;
        fs::remove(evalsFolderPath() / "evaluations.json", ec);
        evals_folder_ = evalsFolder(evalsFolderPath());
        if (!evals_size_ && !evals_size_job_.valid())
            evals_size_job_ = std::async(std::launch::async, [] { return evalsSize(lichess::evalsUrl()); });
        return;
    }
    tip(TR("Reads Lichess's evaluations database again, for its newer evaluations."));
    ImGui::Spacing();
    const float field = ImGui::GetFontSize() * 6;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Evaluation"));
    ImGui::SameLine();
    float from = static_cast<float>(o.from_cp) / 100.f, to = static_cast<float>(o.to_cp) / 100.f;
    const float evaluation = ImGui::GetFontSize() * 8;  // "+0.30" whole beside its buttons
    ImGui::SetNextItemWidth(evaluation);
    if (ImGui::InputFloat("##evals_from", &from, 0.05f, 0.25f, "%+.2f"))
        o.from_cp = std::clamp(static_cast<int>(std::lround(from * 100)), -900, 900);
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("to"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(evaluation);
    if (ImGui::InputFloat("##evals_to", &to, 0.05f, 0.25f, "%+.2f"))
        o.to_cp = std::clamp(static_cast<int>(std::lround(to * 100)), -900, 900);
    if (o.to_cp < o.from_cp) o.to_cp = o.from_cp;
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("for White"));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Depth"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(field);
    if (ImGui::InputInt("##evals_depth", &o.depth)) o.depth = std::clamp(o.depth, 0, 99);
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("at least"));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("After"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(field);
    if (ImGui::InputInt("##evals_min", &o.min_plies)) o.min_plies = std::clamp(o.min_plies, 1, 40);
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("to"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(field);
    if (ImGui::InputInt("##evals_max", &o.max_plies)) o.max_plies = std::clamp(o.max_plies, 1, 40);
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("plies"));
    if (o.max_plies < o.min_plies) o.max_plies = o.min_plies;
    ImGui::Checkbox(TR("One position per named variation"), &o.one_per_name);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("At most"));
    ImGui::SameLine();
    int most = static_cast<int>(o.most);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
    if (ImGui::InputInt("##evals_most", &most, 50)) o.most = static_cast<std::size_t>(std::clamp(most, 1, 5000));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("positions, spread over the ECO codes"));

    ImGui::Spacing();
    if (!evaluated_positions_) {
        ImGui::TextDisabled("%s", TR("Reading the positions kept…"));
        return;
    }
    // What it would hold, worked out again when the choices change.
    if (!evaluated_suite_preview_ || !(evaluated_suite_preview_->first == o))
        evaluated_suite_preview_ = std::make_pair(o, evaluatedSuite(*evaluated_positions_, o));
    const auto& kept = evaluated_suite_preview_->second;
    if (kept.empty()) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", TR("No position: widen the choices."));
    } else {
        std::set<std::string> openings;
        double sum = 0;
        for (const auto& p : kept) {
            openings.insert(p.name.substr(0, p.name.find(':')));
            sum += p.cp;
        }
        ImGui::TextUnformatted(trf("{} positions · {} openings · average evaluation {:+.2f}", kept.size(),
                                   openings.size(), sum / static_cast<double>(kept.size()) / 100.0)
                                   .c_str());
    }
    ImGui::BeginDisabled(kept.empty());
    if (ImGui::Button(TR("Make the book…##evals")))
        dialogs_.saveFile([this](const std::string& path) { writeEvaluatedSuiteBook(path); }, {{"EPD", "epd"}},
                          newBookPath(fmt::format("lichess-{:+.2f}-{:+.2f}.epd", o.from_cp / 100.0, o.to_cp / 100.0)));
    ImGui::EndDisabled();
    tip(
        TR("An EPD file, each position with its move number, its evaluation for the side to move and its depth "
           "(ce, acd) and its opening (c0), added to the library."));
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Cancel##evals_book"))) evaluated_suite_shown_ = false;
}

void App::writeEvaluatedSuiteBook(std::string path) {
    if (!fs::path(path).has_extension()) path += ".epd";  // a book is read by its extension
    if (evaluated_job_.valid()) {
        evaluated_positions_ = evaluated_job_.get();
    } else if (!evaluated_positions_) {
        evaluated_positions_ = evaluatedPositions(evalsFolderPath());
    }
    const auto& o   = evaluated_suite_;
    const auto kept = evaluatedSuite(*evaluated_positions_, o);
    if (kept.empty()) {
        Console::instance().add(TR("Lichess's evaluations: no position for the book, widen the choices"), true);
        return;
    }
    try {
        writeOpeningSuite(path, kept);
    } catch (const std::exception& e) {
        Console::instance().add(e.what(), true);
        return;
    }
    Console::instance().add(
        trn("{} position written to {}", "{} positions written to {}", kept.size(), kept.size(), path));
    addWrittenBook(path, trf("Lichess, {:+.2f} to {:+.2f}: {} positions at depth {} or more, after {} to {} plies",
                             o.from_cp / 100.0, o.to_cp / 100.0, kept.size(), o.depth, o.min_plies, o.max_plies));
    evaluated_suite_shown_ = false;
}

}  // namespace fcg
