// Sources of games (#70, #71, mockup M10): the Databases page's "From a source…"; TWIC's sheet, its issues
// downloaded then imported one after the other into the database chosen, and its report; the weekly look for a new
// issue, off until the user ticks it; the followed sources under the list of databases, and a new issue on the Home
// page.

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>

#include <imgui.h>

#include <fmt/include/fmt/format.h>
#include <tour/files.hpp>

#include "app.hpp"
#include "i18n.hpp"
#include "lichess_months.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

int many(uint64_t n) { return static_cast<int>(std::min<uint64_t>(n, 1000000000)); }

// A packed date (packDate()) as "9 Nov", the year left out.
std::string shortDate(uint32_t packed) {
    static const char* months[] = {N_("Jan"), N_("Feb"), N_("Mar"), N_("Apr"), N_("May"), N_("Jun"),
                                   N_("Jul"), N_("Aug"), N_("Sep"), N_("Oct"), N_("Nov"), N_("Dec")};
    const unsigned month = (packed >> 5) & 15, day = packed & 31;
    if (month < 1 || month > 12 || day == 0) return {};
    return fmt::format("{} {}", day, TR(months[month - 1]));
}

}  // namespace

void App::loadTwic() {
    std::ifstream in(storage_.root() / "twic.json", std::ios::binary);
    if (!in) return;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    twic_ = twic::stateFromJson(text);
}

void App::saveTwic() {
    try {
        tour::replaceFile(storage_.root() / "twic.json", twic::toJson(twic_));
    } catch (const std::exception& e) {
        Console::instance().add(e.what(), true);
    }
}

void App::startTwicIndex(bool weekly) {
    if (twic_index_job_) return;
    if (!weekly) twic_error_.clear();
    TwicIndexJob job;
    job.weekly      = weekly;
    job.done        = std::async(std::launch::async, [] {
        HttpRequest request;
        request.url       = twic::indexUrl();
        request.method    = "GET";
        const auto answer = politeRequest(request);
        std::pair<std::vector<twic::Issue>, std::string> out;
        if (answer.status != 200) {
            out.second = answer.error.empty() ? fmt::format("HTTP {}", answer.status) : answer.error;
            return out;
        }
        out.first = twic::parseIndex(answer.body);
        if (out.first.empty()) out.second = N_("The page of issues lists none the program can read.");
        return out;
    });
    twic_index_job_ = std::move(job);
}

void App::startTwicAdd(std::vector<twic::Issue> issues, int database) {
    if (twic_add_ || issues.empty() || !shelf_) return;
    std::sort(issues.begin(), issues.end(), [](const auto& a, const auto& b) { return a.number < b.number; });
    TwicAdd add;
    add.issues = issues;
    std::string numbers;
    for (const auto& issue : issues) {
        numbers += (numbers.empty() ? "" : ", ") + std::to_string(issue.number);
        add.urls.push_back(twic::pgnUrl(issue.number));
        add.wanted.push_back(storage_.root() / "sources" / "twic" / twic::pgnFile(issue.number));
    }
    add.title = "TWIC " + numbers;
    startFileAdd(std::move(add), database, "TWIC");
}

void App::startFileAdd(TwicAdd add, int database, const std::string& new_name) {
    auto& s = shelf();
    if (database < 0 || database >= static_cast<int>(s.entries().size())) {
        try {
            database = static_cast<int>(s.create(new_name));
            // The sheet shows the new database from now on.
            (add.issues.empty() ? broadcast_target_ : twic_target_) = database;
        } catch (const std::exception& e) {
            twic_done_        = TwicDone{};
            twic_done_->error = e.what();
            return;
        }
    }
    add.index    = static_cast<std::size_t>(database);
    add.folder   = s.entries()[add.index].folder;
    add.cancel   = std::make_shared<std::atomic<bool>>(false);
    add.progress = std::make_shared<DownloadProgress>();
    if (const auto db = s.open(add.index)) add.from = static_cast<study::GameId>(db->snapshot()->games());
    add.downloads = std::async(std::launch::async,
                               [urls = add.urls, wanted = add.wanted, cancel = add.cancel, progress = add.progress] {
                                   std::vector<std::pair<fs::path, std::string>> out;
                                   for (std::size_t i = 0; i < urls.size() && i < wanted.size(); ++i) {
                                       if (cancel->load()) break;
                                       progress->bytes = 0;
                                       progress->size  = 0;
                                       const auto got  = httpDownload(urls[i], wanted[i], progress.get(), cancel.get());
                                       out.emplace_back(wanted[i], got.ok ? std::string() : got.error);
                                       if (!got.ok) break;
                                   }
                                   return out;
                               });
    twic_done_.reset();
    twic_add_ = std::move(add);
}

bool App::twicImportEnded(const fs::path& file, const study::DatabaseImport& report) {
    if (!twic_add_ || !twic_add_->importing || twic_add_->next >= twic_add_->files.size() ||
        twic_add_->files[twic_add_->next] != file)
        return false;
    auto& add = *twic_add_;
    add.added += report.added;
    add.duplicates += report.duplicates;
    if (report.error.empty() && !report.read.cancelled) {
        if (add.next < add.issues.size()) twic_.added[add.folder.string()].insert(add.issues[add.next].number);
        std::error_code ec;
        fs::remove(file, ec);  // its games are in the database now
    } else {
        add.cancel->store(true);
        twic_done_        = TwicDone{};
        twic_done_->error = report.error.empty() ? std::string(TR("Cancelled.")) : report.error;
    }
    add.importing = false;
    ++add.next;
    saveTwic();
    return true;
}

void App::pollTwic() {
    if (!twic_loaded_) {
        loadTwic();
        twic_loaded_ = true;
    }
    const auto now = nowSeconds();
    // The weekly look, when ticked: the page of issues, at most four times a day.
    if (!twic_index_job_ && twic::lookDue(twic_, now)) startTwicIndex(true);
    if (twic_index_job_ && twic_index_job_->done.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto [issues, error] = twic_index_job_->done.get();
        const bool weekly    = twic_index_job_->weekly;
        twic_index_job_.reset();
        twic_.checked = now;
        if (!error.empty()) {
            if (!weekly) twic_error_ = TR(error.c_str());
        } else {
            twic_issues_       = issues;
            twic_.newest       = issues.front().number;
            twic_.newest_games = issues.front().games;
            twic_.newest_date  = issues.front().date;
        }
        saveTwic();
    }
    // Lichess's page of monthly files, asked for by a sheet.
    if (lichess_months_job_ && lichess_months_job_->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto [html, error] = lichess_months_job_->get();
        lichess_months_job_.reset();
        if (!error.empty()) {
            lichess_months_error_ = error;
        } else {
            lichess_broadcast_months_ = lichess::parseMonths(html, lichess::MonthKind::Broadcast);
            lichess_standard_months_  = lichess::parseMonths(html, lichess::MonthKind::Standard);
            if (lichess_broadcast_months_.empty())
                lichess_months_error_ = TR("The page lists no month the program can read.");
        }
    }
    if (!twic_add_ || !shelf_) return;
    auto& add = *twic_add_;
    // The downloads, then the imports one after the other.
    if (!add.downloaded) {
        if (add.downloads.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        add.downloaded = true;
        for (const auto& [file, error] : add.downloads.get()) {
            if (!error.empty()) {
                twic_done_        = TwicDone{};
                twic_done_->error = error == "cancelled" ? std::string(TR("Cancelled.")) : error;
                twic_add_.reset();
                return;
            }
            add.files.push_back(file);
        }
    }
    if (add.importing || shelf_->import()) return;
    if (add.next < add.files.size() && !add.cancel->load()) {
        importIntoDatabase(static_cast<int>(add.index), add.files[add.next], false);
        add.importing = shelf_->import() != nullptr;
        if (!add.importing) {  // refused (read-only…): said by the page
            twic_add_.reset();
        }
        return;
    }
    // All of them in: the report.
    if (!twic_done_ || twic_done_->error.empty()) {
        TwicDone done;
        done.title      = add.title;
        done.index      = add.index;
        done.database   = add.index < shelf_->entries().size() ? shelf_->entries()[add.index].name : std::string();
        done.added      = add.added;
        done.duplicates = add.duplicates;
        for (std::size_t i = 0; i < add.next && i < add.issues.size(); ++i)
            done.numbers.push_back(add.issues[i].number);
        if (const auto db = shelf_->open(add.index)) {
            const auto snap = db->snapshot();
            done.report     = twic::reportOf(*snap, add.from, static_cast<study::GameId>(snap->games()));
        }
        twic_done_ = std::move(done);
        if (!add.issues.empty() &&
            (twic_.database.empty() || twic_.database == add.folder.string() || twic_.added[twic_.database].empty()))
            twic_.database = add.folder.string();
        saveTwic();
    }
    twic_add_.reset();
}

void App::drawSourcesMenu() {
    if (!ImGui::BeginPopup("##sources")) return;
    struct Source {
        const char* name;
        const char* what;
        const char* terms;
    };
    const Source twic{"TWIC", TR("the week's games, every Monday"), TR("theweekinchess.com · free for personal use")};
    const Source broadcasts{TR("Lichess broadcasts"), TR("a month of official broadcasts"),
                            TR("database.lichess.org · CC BY-SA 4.0")};
    int i = 0;
    for (const auto& source : {twic, broadcasts}) {
        ImGui::PushID(i++);
        const auto start = ImGui::GetCursorPos();
        if (ImGui::Selectable("##source", false, 0, ImVec2(px(420), ImGui::GetTextLineHeightWithSpacing() * 2.1f))) {
            twic_sheet_      = i == 1;
            broadcast_sheet_ = i == 2;
            twic_done_.reset();
        }
        ImGui::SetCursorPos(ImVec2(start.x + px(6), start.y + px(2)));
        ImGui::TextUnformatted(source.name);
        ImGui::SameLine(0, px(6));
        ImGui::TextDisabled("· %s", source.what);
        ImGui::SetCursorPosX(start.x + px(6));
        ImGui::TextDisabled("%s", source.terms);
        ImGui::PopID();
    }
    ImGui::EndPopup();
}

void App::drawTwicSheet() {
    auto& s = shelf();
    ui::beginCard("twic_sheet");
    ImGui::TextUnformatted("The Week in Chess");
    ImGui::SameLine(0, px(10));
    ImGui::TextDisabled("%s", TR("an issue every Monday, since 1994"));
    ui::topic(guide::topics::Twic);

    // The database the issues go into: the one the weekly issue goes into, else one named TWIC, else a new one.
    const auto& entries = s.entries();
    if (twic_target_ == -2 || twic_target_ >= static_cast<int>(entries.size())) {
        twic_target_ = -1;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].source) continue;
            if (entries[i].folder.string() == twic_.database) twic_target_ = static_cast<int>(i);
            if (twic_target_ < 0 && entries[i].name == "TWIC") twic_target_ = static_cast<int>(i);
        }
    }
    const std::string folder =
        twic_target_ >= 0 ? entries[static_cast<std::size_t>(twic_target_)].folder.string() : std::string();
    const auto there = twic_.added.find(folder);
    const auto added = [&](int n) { return there != twic_.added.end() && there->second.count(n) > 0; };

    if (twic_issues_.empty()) {
        if (!twic_index_job_ && twic_error_.empty()) startTwicIndex(false);
        if (twic_index_job_) {
            ImGui::TextDisabled("%s", TR("Asking theweekinchess.com for its issues…"));
        } else {
            ImGui::PushTextWrapPos();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::BAD), "%s",
                               trf("theweekinchess.com did not answer: {}", twic_error_).c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::Button(TR("Try again"))) startTwicIndex(false);
        }
    } else {
        if (twic_ticked_.empty() && !added(twic_issues_.front().number))
            twic_ticked_.insert(twic_issues_.front().number);
        if (ImGui::BeginTable("##twic_issues", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("##ticked", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            ImGui::TableSetupColumn(TR("Issue"));
            ImGui::TableSetupColumn(TR("Monday"));
            ImGui::TableSetupColumn(TR("Games"));
            ImGui::TableSetupColumn(TR("In the database"));
            ImGui::TableHeadersRow();
            const std::size_t shown = std::min<std::size_t>(twic_issues_.size(), 12);
            for (std::size_t i = 0; i < shown; ++i) {
                const auto& issue = twic_issues_[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                bool on = twic_ticked_.count(issue.number) > 0;
                if (ImGui::Checkbox(fmt::format("##twic{}", issue.number).c_str(), &on)) {
                    if (on)
                        twic_ticked_.insert(issue.number);
                    else
                        twic_ticked_.erase(issue.number);
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(fmt::format("TWIC {}", issue.number).c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", issue.date.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(issue.games ? ui::exactCount(issue.games).c_str() : "");
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", added(issue.number) ? TR("already there") : TR("not yet"));
            }
            ImGui::EndTable();
        }
        // Into which database.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(TR("Into"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(260));
        const auto chosen = twic_target_ >= 0 ? entries[static_cast<std::size_t>(twic_target_)].name
                                              : std::string(TR("A new database, “TWIC”"));
        if (ImGui::BeginCombo("##twic_into", chosen.c_str())) {
            if (ImGui::Selectable(TR("A new database, “TWIC”"), twic_target_ < 0)) twic_target_ = -1;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].source) continue;
                if (ImGui::Selectable(fmt::format("{}##twic_db{}", entries[i].name, i).c_str(),
                                      twic_target_ == static_cast<int>(i)))
                    twic_target_ = static_cast<int>(i);
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("a game the database has already (the same tags and moves) is not added twice"));
        // The weekly look, offered here, off until ticked.
        if (ImGui::Checkbox(TR("Every Monday, look for a new issue"), &twic_.weekly)) {
            if (twic_.weekly && twic_target_ >= 0) twic_.database = folder;
            saveTwic();
        }
        ImGui::TextDisabled("%s", TR("A small request to theweekinchess.com while RookStudio is open; the new issue is "
                                     "offered on the Home page, never added without you."));

        uint64_t games = 0;
        std::vector<twic::Issue> chosen_issues;
        for (const auto& issue : twic_issues_)
            if (twic_ticked_.count(issue.number)) {
                chosen_issues.push_back(issue);
                games += issue.games;
            }
        ImGui::Spacing();
        const std::string action =
            chosen_issues.empty()
                ? std::string(TR("Tick an issue"))
                : trn("Download and add {} issue ({} games)", "Download and add {} issues ({} games)",
                      static_cast<int>(chosen_issues.size()), chosen_issues.size(), ui::exactCount(games));
        ImGui::BeginDisabled(chosen_issues.empty() || twic_add_.has_value() || s.import() != nullptr);
        if (ImGui::Button(action.c_str())) {
            if (twic_.weekly && twic_target_ >= 0) twic_.database = folder;
            startTwicAdd(chosen_issues, twic_target_);
            twic_ticked_.clear();
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Close"))) twic_sheet_ = false;

    drawFileAddProgress();
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", TR("What leaves the machine: the request for its list of issues, then for the PGN files "
                                 "of the issues ticked (twicNNNNg.zip), nothing else. TWIC is free for personal use; "
                                 "nothing of it ships with RookStudio."));
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void App::drawTwicReport() {
    if (!twic_done_) return;
    const auto& d = *twic_done_;
    ui::beginCard("twic_report");
    if (!d.error.empty()) {
        ImGui::PushTextWrapPos();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::BAD), "%s",
                           trf("The games could not be added: {}", d.error).c_str());
        ImGui::PopTextWrapPos();
    } else {
        ui::inlineIcon(ui::Icon::Check, ui::GOOD);
        ImGui::SameLine();
        ImGui::TextUnformatted(trf("{} added to “{}”: {}, {} already there", d.title, d.database,
                                   trn("{} game", "{} games", many(d.added), ui::exactCount(d.added)),
                                   ui::exactCount(d.duplicates))
                                   .c_str());
        if (!d.report.events.empty()) {
            ImGui::Spacing();
            ImGui::TextUnformatted(TR("The strongest tournaments"));
            for (const auto& e : d.report.events) {
                const auto dates = e.first == e.last ? shortDate(e.first)
                                                     : fmt::format("{} – {}", shortDate(e.first), shortDate(e.last));
                ImGui::TextUnformatted(e.name.c_str());
                ImGui::SameLine(px(360));
                ImGui::TextDisabled("%s", trf("average Elo {}", e.average).c_str());
                ImGui::SameLine(px(500));
                ImGui::TextDisabled("%s", dates.c_str());
            }
        }
        if (!d.report.players.empty()) {
            ImGui::Spacing();
            ImGui::TextUnformatted(TR("The best-rated players"));
            for (const auto& p : d.report.players) {
                // A name opens its games in the database.
                if (ImGui::Selectable(fmt::format("{}##twic_player", p.name).c_str(), false, 0, ImVec2(px(340), 0))) {
                    db_selected_ = static_cast<int>(d.index);
                    db_filter_.fill(0);
                    std::copy_n(p.name.begin(), std::min(p.name.size(), db_filter_.size() - 1), db_filter_.begin());
                    db_list_ = true;
                    db_row_  = 0;
                }
                ImGui::SameLine(px(360));
                ImGui::TextDisabled("%d", p.elo);
                ImGui::SameLine(px(500));
                ImGui::TextDisabled("%s", trn("{} game", "{} games", many(p.games), p.games).c_str());
            }
            ImGui::TextDisabled("%s", TR("A name opens its games in the database. This report is made on your "
                                         "machine, from the games added."));
        }
    }
    if (ImGui::SmallButton(TR("Close"))) twic_done_.reset();
    ui::endCard();
}

void App::drawFollowedSources() {
    if (!twic_.weekly) return;
    ImGui::Dummy(ImVec2(0, px(6)));
    ImGui::TextDisabled("%s", TR("Followed sources"));
    ImGui::Separator();
    ImGui::TextUnformatted(TR("TWIC, every Monday"));
    if (const auto next = twic::newIssue(twic_, twic_.database)) {
        ImGui::TextDisabled("%s", trf("TWIC {} is out", next->number).c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(twic_add_.has_value());
        if (ImGui::SmallButton(TR("Add##twic_followed"))) {
            for (std::size_t i = 0; i < shelf().entries().size(); ++i)
                if (shelf().entries()[i].folder.string() == twic_.database) startTwicAdd({*next}, static_cast<int>(i));
        }
        ImGui::EndDisabled();
    } else if (twic_.newest > 0) {
        const auto due = twic::nextMonday(twic_.newest_date);
        ImGui::TextDisabled("%s", (due.empty() ? trf("after issue {}", twic_.newest)
                                               : trf("issue {} expected on {}", twic_.newest + 1, due))
                                      .c_str());
    }
}

void App::drawTwicHomeNotice() {
    const auto next = twic::newIssue(twic_, twic_.database);
    if (!next || twic_add_) return;
    std::size_t index = shelf().entries().size();
    for (std::size_t i = 0; i < shelf().entries().size(); ++i)
        if (shelf().entries()[i].folder.string() == twic_.database) index = i;
    if (index >= shelf().entries().size()) return;
    ImGui::Spacing();
    ImGui::TextUnformatted((next->games ? trf("TWIC {} is out: {} games", next->number, ui::exactCount(next->games))
                                        : trf("TWIC {} is out", next->number))
                               .c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(trf("Add to “{}”", shelf().entries()[index].name).c_str())) {
        startTwicAdd({*next}, static_cast<int>(index));
        page_ = Page::Databases;
    }
}

void App::drawFileAddProgress() {
    if (!twic_add_) return;
    auto& add = *twic_add_;
    std::string doing;
    if (!add.downloaded) {
        const auto size = add.progress->size.load(), bytes = add.progress->bytes.load();
        doing = size ? trf("Downloading: {} %", static_cast<int>(100 * bytes / size)) : TR("Downloading…");
    } else if (add.next < add.files.size()) {
        doing = add.files.size() > 1 ? trf("Adding the games: file {} of {}", add.next + 1, add.files.size())
                                     : std::string(TR("Adding the games…"));
    }
    ImGui::TextDisabled("%s", doing.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Cancel"))) {
        add.cancel->store(true);
        if (add.importing) shelf().cancelImport();
    }
}

void App::startLichessMonths() {
    if (lichess_months_job_) return;
    lichess_months_error_.clear();
    lichess_months_job_ = std::async(std::launch::async, [] {
        HttpRequest request;
        request.url       = lichess::databaseUrl();
        request.method    = "GET";
        request.timeout_s = 30;
        const auto answer = politeRequest(request);
        if (answer.status != 200)
            return std::pair<std::string, std::string>{
                "", answer.error.empty() ? fmt::format("HTTP {}", answer.status) : answer.error};
        return std::pair<std::string, std::string>{answer.body, ""};
    });
}

void App::drawBroadcastSheet() {
    auto& s = shelf();
    ui::beginCard("broadcast_sheet");
    ImGui::TextUnformatted(TR("Lichess broadcasts"));
    ImGui::SameLine(0, px(10));
    ImGui::TextDisabled("%s", TR("a month of official broadcasts, from database.lichess.org"));
    ui::topic(guide::topics::LichessMonths);
    const auto& entries = s.entries();
    const auto new_name = std::string(TR("Lichess broadcasts"));
    if (broadcast_target_ == -2 || broadcast_target_ >= static_cast<int>(entries.size())) {
        broadcast_target_ = -1;
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (!entries[i].source && entries[i].name == new_name) broadcast_target_ = static_cast<int>(i);
    }
    if (lichess_broadcast_months_.empty()) {
        if (!lichess_months_job_ && lichess_months_error_.empty()) startLichessMonths();
        if (lichess_months_job_) {
            ImGui::TextDisabled("%s", TR("Asking database.lichess.org for its months…"));
        } else {
            ImGui::PushTextWrapPos();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::BAD), "%s",
                               trf("database.lichess.org did not answer: {}", lichess_months_error_).c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::Button(TR("Try again"))) startLichessMonths();
        }
    } else {
        const int shown = static_cast<int>(std::min<std::size_t>(lichess_broadcast_months_.size(), 12));
        broadcast_pick_ = std::clamp(broadcast_pick_, 0, shown - 1);
        if (ImGui::BeginTable("##broadcast_months", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("##picked", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            ImGui::TableSetupColumn(TR("Month"));
            ImGui::TableSetupColumn(TR("Size"));
            ImGui::TableSetupColumn(TR("Games"));
            ImGui::TableHeadersRow();
            for (int i = 0; i < shown; ++i) {
                const auto& m = lichess_broadcast_months_[static_cast<std::size_t>(i)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::RadioButton(fmt::format("##month{}", i).c_str(), broadcast_pick_ == i)) broadcast_pick_ = i;
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(m.month.c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", m.size.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(m.games ? ui::exactCount(m.games).c_str() : "");
            }
            ImGui::EndTable();
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(TR("Into"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(260));
        const auto offer   = trf("A new database, “{}”", new_name);
        const auto current = broadcast_target_ >= 0 ? entries[static_cast<std::size_t>(broadcast_target_)].name : offer;
        if (ImGui::BeginCombo("##broadcast_into", current.c_str())) {
            if (ImGui::Selectable(offer.c_str(), broadcast_target_ < 0)) broadcast_target_ = -1;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].source) continue;
                if (ImGui::Selectable(fmt::format("{}##broadcast_db{}", entries[i].name, i).c_str(),
                                      broadcast_target_ == static_cast<int>(i)))
                    broadcast_target_ = static_cast<int>(i);
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("a game the database has already (the same tags and moves) is not added twice"));
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", TR("Lichess's broadcasts are under CC BY-SA 4.0: their games keep their source, the "
                                     "broadcast's page in their Site tag; what you share of them is shared alike."));
        ImGui::PopTextWrapPos();
        const auto& m = lichess_broadcast_months_[static_cast<std::size_t>(broadcast_pick_)];
        ImGui::BeginDisabled(twic_add_.has_value() || s.import() != nullptr);
        if (ImGui::Button(trf("Download and add {} ({})", m.month, m.size).c_str())) {
            TwicAdd add;
            add.title = trf("Lichess broadcasts of {}", m.month);
            add.urls.push_back(lichess::monthUrl(m));
            add.wanted.push_back(storage_.root() / "sources" / "lichess" / lichess::monthFile(m));
            startFileAdd(std::move(add), broadcast_target_, new_name);
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Close##broadcasts"))) broadcast_sheet_ = false;
    drawFileAddProgress();
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", TR("What leaves the machine: the request for database.lichess.org's page, then for the "
                                 "month's file, nothing else."));
    ImGui::PopTextWrapPos();
    ui::endCard();
}

}  // namespace fcg
