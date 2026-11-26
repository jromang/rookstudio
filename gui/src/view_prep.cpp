// Preparing a game against an opponent (#52, mockup M8, board "Prep"), now a way into their dossier (#90, mockup M13,
// board 3): the Lichess page's card takes a Lichess name and opens their dossier with its Lichess source ticked. Their
// games of the last year (blitz, rapid and classical) come into "Lichess · opponent" through their sync, their level
// from their public profile; the dossier reads that database beside the user's, each time the sync brings games.
// Locked while a game of the user's against them is played (fair play).

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include "app.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool lichessName(std::string_view name) {
    if (name.size() < 2 || name.size() > 30) return false;
    return std::all_of(name.begin(), name.end(),
                       [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; });
}

}  // namespace

void App::startPrep(const std::string& opponent) {
    if (prep_) {
        if (prep_->cancel) *prep_->cancel = true;
        prep_.reset();  // its jobs end first
    }
    lichess_.prep_player = opponent;
    saveLichessSettings();
    Prep p;
    p.opponent = opponent;
    p.cancel   = std::make_shared<std::atomic<bool>>(false);
    p.progress = std::make_shared<lichess::SyncProgress>();
    // Their database: "Lichess · opponent", made when missing; a sync already writing in it brings the games itself.
    const auto name = "Lichess · " + opponent;
    auto& s         = shelf();
    for (std::size_t i = 0; i < s.entries().size(); ++i)
        if (!s.entries()[i].source && lower(s.entries()[i].name) == lower(name)) p.index = i;
    std::shared_ptr<study::Database> db;
    try {
        if (p.index == SIZE_MAX) p.index = s.create(name);
        if (!(lichess_sync_ && lichess_sync_->index == p.index)) db = s.open(p.index);
    } catch (const std::exception& e) {
        p.error = lichess::Failure::own(e.what());
    }
    // A year of their blitz, rapid and classical games (what Lichess's export is asked: their name and these filters).
    if (db) {
        lichess::SyncFilters filters;
        filters.perf_types = lichess::perfTypes(1);
        filters.since =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count() -
            int64_t{365} * 24 * 3600 * 1000;
        const std::string token = lichess_account_ ? lichess_account_->token : std::string();
        p.sync =
            std::async(std::launch::async, [db, opponent, token, progress = p.progress, cancel = p.cancel, filters] {
                return lichess::syncGames(*db, opponent, token, *progress, *cancel, 500, filters);
            });
    }
    p.profile_job = std::async(std::launch::async, [opponent] { return lichess::publicProfile(opponent); });
    prep_         = std::move(p);
}

void App::pollPrep() {
    if (!prep_) return;
    auto& p          = *prep_;
    const auto ready = [](auto& job) {
        return job.valid() && job.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    };
    if (ready(p.profile_job)) {
        auto answer = p.profile_job.get();
        if (answer.value)
            p.profile = std::move(*answer.value);
        else
            p.profile_error = answer;
    }
    if (ready(p.sync)) {
        const auto report = p.sync.get();
        if (report.failed() && !report.cancelled) p.error = report;
        if (report.added > 0 && p.index < shelf().entries().size()) shelf().rebuildTree(p.index);
        ++p.synced;
    }
    // The day their database last took games: what the dossier says of the source offline.
    if (p.index < shelf().entries().size() && p.last_sync.empty() && !p.sync.valid()) {
        const auto facts = shelf().facts(p.index);
        if (facts.games > 0 && facts.changed > 0) {
            char day[32]  = "";
            std::tm tm{};  // localtime() shares one buffer between the threads
#ifdef _WIN32
            const bool local = localtime_s(&tm, &facts.changed) == 0;
#else
            const bool local = localtime_r(&facts.changed, &tm) != nullptr;
#endif
            if (local) std::strftime(day, sizeof day, "%Y-%m-%d", &tm);
            p.last_sync = day;
        }
    }
}

void App::drawPrepCard() {
    const ui::TopicRegion topic(guide::topics::Prep);
    ui::beginCard("lichess_prep");
    ImGui::PushTextWrapPos();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Prepare a game against"));
    ImGui::SameLine();
    if (!prep_prefilled_) {
        prep_prefilled_   = true;
        const auto length = std::min(lichess_.prep_player.size(), prep_name_.size() - 1);
        std::copy_n(lichess_.prep_player.data(), length, prep_name_.data());
    }
    if (prep_focus_) {
        ImGui::SetKeyboardFocusHere();
        prep_focus_ = false;
    }
    ImGui::SetNextItemWidth(px(200));
    const bool enter = ImGui::InputTextWithHint("##prep_name", TR("A Lichess name"), prep_name_.data(),
                                                prep_name_.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const std::string typed = prep_name_.data();
    ImGui::BeginDisabled(!lichessName(typed));
    if (ImGui::SmallButton(TR("Prepare")) || (enter && lichessName(typed))) {
        startPrep(typed);  // their games synced again, as each time
        study::DossierSubject subject;
        subject.name    = typed;
        subject.lichess = typed;
        openDossier(std::move(subject), true);
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s",
                        TR("Their dossier opens with its Lichess source: their games of the last year come into a "
                           "database of their own; then what they play on the board and online, your games "
                           "against them, and the lines they play that your repertoires do not answer."));
    // Their dossier again, while it is kept.
    if (dossier_ && !dossier_->subject.lichess.empty() && !dossier_->shown) {
        if (ImGui::SmallButton(trf("Dossier of {}", dossier_->subject.name).c_str())) {
            dossier_->shown = true;
            dossier_->over  = page_;
        }
    }
    ImGui::TextDisabled("%s", TR("Asked of Lichess: their name with a year of blitz, rapid and classical games, and "
                                 "their public profile. Nothing of yours."));
    ImGui::PopTextWrapPos();
    ui::endCard();
}

}  // namespace fcg
