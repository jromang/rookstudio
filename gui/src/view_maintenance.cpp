// The maintenance of a database (mockup M13, board 1), in place of the Databases page while open: its duplicate games
// (the same game from two sources, compared field by field and merged), its players' names (their spellings brought
// together in the database's table of names, matched with FIDE's rating list), its check and its compaction. Each long
// task runs in the background, at a low priority, with its progress and Cancel; Ctrl+Z undoes merges and names brought
// together; the compaction keeps the old files until RookStudio closes, "Undo the compaction" putting them back.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <string>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>  // SetKeyOwner

#include <study/background.hpp>

#include "app.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "machines.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

namespace fs = std::filesystem;

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

ImU32 tinted(ImU32 colour, int alpha) { return (colour & 0x00FFFFFF) | (static_cast<ImU32>(alpha) << 24); }

// A game's source as people name it, or a dash: from the file it came from, else from its tags (read only then).
std::string sourceOf(const study::Snapshot& snap, study::GameId id) {
    auto label = study::sourceLabel(snap.source(id), study::ImportedGame{});
    if (label.empty()) label = study::sourceLabel({}, snap.game(id));
    return label.empty() ? std::string("—") : label;
}

std::string resultText(char r) { return r == '1' ? "1-0" : r == '0' ? "0-1" : r == '=' ? "½-½" : "*"; }

// How long a task took: milliseconds under a second.
std::string tookText(double seconds) {
    return seconds < 1 ? fmt::format("{} ms", std::max(1, static_cast<int>(std::lround(seconds * 1000))))
                       : ui::formatDuration(seconds);
}

// A count large, its label under it: the mockup's figures.
void figure(const std::string& number, const std::string& label, float width) {
    ImGui::BeginGroup();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.45f);
    ImGui::TextUnformatted(number.c_str());
    ImGui::PopFont();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextDisabled("%s", label.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
}

// The names of the fields of a merge, as the comparison shows them.
const char* fieldName(study::MergeField field) {
    switch (field) {
        case study::MergeField::White:
            return TR("White");
        case study::MergeField::Black:
            return TR("Black");
        case study::MergeField::Event:
            return TR("Event");
        case study::MergeField::Site:
            return TR("Site");
        case study::MergeField::Round:
            return TR("Round");
        case study::MergeField::Date:
            return TR("Date");
        case study::MergeField::Ratings:
            return TR("Elo");
        case study::MergeField::Eco:
            return TR("ECO");
        case study::MergeField::FideIds:
            return TR("FIDE ids");
        case study::MergeField::Moves:
            return TR("Moves");
        case study::MergeField::Clocks:
            return TR("Clocks [%clk]");
        case study::MergeField::Evals:
            return TR("Evaluations [%eval]");
        case study::MergeField::Result:
            return TR("Result");
    }
    return "";
}

// A field's value as the comparison writes it: the moves as plies, clocks and evaluations as how many have one.
std::string fieldText(study::MergeField field, const std::string& value, uint32_t plies) {
    if (value.empty()) return "—";
    switch (field) {
        case study::MergeField::Moves: {
            const auto n = std::stoul(value);
            return trn("{} ply", "{} plies", n, ui::exactCount(n));
        }
        case study::MergeField::Clocks:
        case study::MergeField::Evals: {
            const auto n = std::stoul(value);
            return n >= plies ? std::string(TR("at each move")) : trn("at {} ply", "at {} plies", n, ui::exactCount(n));
        }
        case study::MergeField::Result:
            return value == "1/2-1/2" ? std::string("½-½") : value;
        default:
            return value;
    }
}

// What differs between a group's games, in a few words.
std::string differences(const std::vector<std::array<std::string, study::MERGE_FIELDS>>& fields) {
    std::vector<std::string> parts;
    const auto differs = [&](study::MergeField f) {
        const auto i = static_cast<std::size_t>(f);
        for (std::size_t g = 1; g < fields.size(); ++g)
            if (fields[g][i] != fields[0][i]) return true;
        return false;
    };
    if (differs(study::MergeField::White) || differs(study::MergeField::Black)) parts.push_back(TR("names"));
    if (differs(study::MergeField::Event)) parts.push_back(TR("event"));
    if (differs(study::MergeField::Round)) parts.push_back(TR("round"));
    if (differs(study::MergeField::Date)) parts.push_back(TR("date"));
    if (differs(study::MergeField::Ratings)) parts.push_back(TR("ratings"));
    if (differs(study::MergeField::Eco)) parts.push_back(TR("ECO"));
    if (differs(study::MergeField::FideIds)) parts.push_back(TR("FIDE identifiers"));  // a list's word, not a label
    if (differs(study::MergeField::Clocks)) parts.push_back(TR("clocks"));
    if (differs(study::MergeField::Evals)) parts.push_back(TR("evaluations"));
    if (differs(study::MergeField::Site)) parts.push_back(TR("site"));
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : ", ") + p;
    return out.empty() ? std::string(TR("the headers' spelling")) : out;
}

// The kinds "Merge all" takes: those that need no look.
bool mergeable(const study::DuplicateGroup& g) { return g.kind != study::DuplicateKind::Results; }

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------

bool App::careBusy(const fs::path& folder) const {
    return care_job_ && care_job_->kind == CareJob::Kind::Compact && care_job_->folder == folder;
}

void App::openCare(int database, CarePart part) {
    auto& s             = shelf();
    const auto& entries = s.entries();
    if (database < 0 || database >= static_cast<int>(entries.size()) || entries[database].source) {
        database = -1;
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (!entries[i].source) {
                database = static_cast<int>(i);
                break;
            }
    }
    page_ = Page::Databases;
    if (database < 0) {
        db_message_ = TR("No database of yours yet: make one, or import a file, first.");
        return;
    }
    db_selected_      = database;
    db_list_          = false;
    const auto folder = entries[static_cast<std::size_t>(database)].folder;
    if (!care_ || care_->folder != folder) {
        care_         = Care{};
        care_->folder = folder;
    }
    care_->part = part;
    care_->pair = -1;
}

const fide::Kept* App::fideKept() {
    if (!fide_kept_read_) {
        fide_kept_      = fide::readKept(storage_.root());
        fide_kept_read_ = true;
    }
    return fide_kept_ ? &*fide_kept_ : nullptr;
}

std::shared_ptr<const study::FideList> App::fideList() {
    if (!fide_list_ && fideKept()) {
        try {
            fide_list_ = std::make_shared<const study::FideList>(fide::listFile(storage_.root(), *fide_kept_));
        } catch (const std::exception& e) {
            Console::instance().add(trf("FIDE's rating list in the profile cannot be read: {}", e.what()), true);
            fide_kept_.reset();
        }
    }
    return fide_list_;
}

void App::startFideList(const fs::path& file) {
    if (fide_job_) return;
    fide_outcome_.reset();
    fide_list_.reset();  // read again once the job ends; its file, let go, can go then
    fide_job_ = fide::start(storage_.root(), file);
}

void App::pollFideList() {
    if (!fide_job_ || fide_job_->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto outcome = fide_job_->done.get();
    fide_job_.reset();
    if (outcome.ok) {
        fide_list_.reset();  // the new one read when needed
        fide_kept_      = outcome.kept;
        fide_kept_read_ = true;
        Console::instance().add(trf("FIDE's rating list of {}: {} players, kept in your profile",
                                    fide::monthText(outcome.kept.month), ui::exactCount(outcome.kept.info.players)));
    }
    if (outcome.same && fideKept()) {
        fide_kept_->offered = fide::monthDue(*fide_kept_, std::time(nullptr));  // set aside until the next month
        if (!fide_kept_->offered.empty()) fide::writeKept(storage_.root(), *fide_kept_);
    }
    fide_outcome_ = std::move(outcome);
    if (care_) care_->names_of = ~uint64_t{0};  // matched again with the new list
}

// ---------------------------------------------------------------------------------------------------------------------
// The background tasks.

namespace {

std::size_t indexOf(DatabaseShelf& s, const fs::path& folder) {
    for (std::size_t i = 0; i < s.entries().size(); ++i)
        if (s.entries()[i].folder == folder) return i;
    return s.entries().size();
}

}  // namespace

void App::startCareMerges(std::size_t index, std::vector<study::DuplicateGroup> groups,
                          std::vector<study::MergeChoice> choices, bool crosstable) {
    if (care_job_ || groups.empty()) return;
    const auto db = shelf().open(index);
    if (!db || db->readOnly()) return;
    auto job        = std::make_unique<CareJob>();
    job->kind       = CareJob::Kind::Merge;
    job->crosstable = crosstable;
    job->folder  = shelf().entries()[index].folder;
    job->started = std::chrono::steady_clock::now();
    CareJob* j   = job.get();
    j->done      = std::async(std::launch::async, [db, j, groups = std::move(groups), choices = std::move(choices)] {
        study::enterBackground();
        const auto snap = db->snapshot();
        std::vector<study::Database::Merge> merges;
        for (std::size_t g = 0; g < groups.size(); ++g) {
            if (j->cancel) return;
            const auto& group = groups[g];
            std::vector<study::ImportedGame> games;
            std::vector<std::pair<study::GameId, std::string>> origins;
            bool gone = false;
            for (const auto id : group.games) {
                if (id >= snap->games() || snap->deleted(id)) gone = true;  // merged or deleted since
                if (gone) break;
                games.push_back(snap->game(id));
                origins.emplace_back(id, study::sourceLabel(snap->source(id), games.back()));
            }
            if (gone) continue;
            const auto choice = g < choices.size() ? choices[g] : study::fullestChoice(games);
            study::Database::Merge m;
            m.keep = group.games.front();
            m.game = study::mergeGames(games, choice, origins);
            m.drop.assign(group.games.begin() + 1, group.games.end());
            merges.push_back(std::move(m));
        }
        if (merges.empty()) return;
        try {
            const auto n = merges.size();
            Care::Undo undo;
            undo.states = db->merge(std::move(merges));
            undo.what   = trn("{} merge", "{} merges", n, ui::exactCount(n));
            j->undo     = std::move(undo);
            j->merged   = n;
            j->said =
                trn("Merged: {} game kept, its duplicate marked deleted. Ctrl+Z undoes.",
                    "Merged: {} games kept, their duplicates marked deleted. Ctrl+Z undoes.", n, ui::exactCount(n));
        } catch (const std::exception& e) {
            j->error = e.what();
        }
    });
    care_job_    = std::move(job);
}

void App::startCarePlayers(std::size_t index, study::PlayerTable table, std::string what, std::string said,
                           bool crosstable) {
    if (care_job_) return;
    const auto db = shelf().open(index);
    if (!db || db->readOnly()) return;
    auto job        = std::make_unique<CareJob>();
    job->kind       = CareJob::Kind::Players;
    job->crosstable = crosstable;
    job->folder  = shelf().entries()[index].folder;
    job->started = std::chrono::steady_clock::now();
    job->said    = std::move(said);
    CareJob* j   = job.get();
    Care::Undo undo;
    undo.what    = std::move(what);
    undo.players = *db->snapshot()->players();
    j->undo      = std::move(undo);
    j->done      = std::async(std::launch::async, [db, j, table = std::move(table)]() mutable {
        study::enterBackground();
        try {
            db->setPlayers(std::move(table));
        } catch (const std::exception& e) {
            j->error = e.what();
            j->undo.reset();
        }
    });
    care_job_    = std::move(job);
}

void App::careUndo() {
    if (!care_ || care_->undo.empty() || care_job_) return;
    const auto index = indexOf(shelf(), care_->folder);
    const auto db    = shelf().open(index);
    if (!db || db->readOnly()) return;
    auto undo = std::move(care_->undo.back());
    care_->undo.pop_back();
    startCareRestore(index, std::move(undo), false);
}

void App::startCareRestore(std::size_t index, Care::Undo undo, bool crosstable) {
    if (care_job_) return;
    const auto db = shelf().open(index);
    if (!db || db->readOnly()) return;
    auto job        = std::make_unique<CareJob>();
    job->kind       = CareJob::Kind::Restore;
    job->folder     = shelf().entries()[index].folder;
    job->started    = std::chrono::steady_clock::now();
    job->said       = trf("Undone: {}.", undo.what);
    job->crosstable = crosstable;
    job->kept       = undo.kept;
    CareJob* j      = job.get();
    // Keep what can be read is undone while nothing else was written to the database (#97).
    std::string refused = undo.kept ? TR("something was written to the database since") : "";
    j->done   = std::async(std::launch::async, [db, j, undo = std::move(undo), refused = std::move(refused)]() mutable {
        study::enterBackground();
        try {
            if (undo.kept && !db->undoKeepReadable()) j->error = refused;
            if (!undo.states.empty()) db->restore(undo.states);
            if (undo.players) db->setPlayers(std::move(*undo.players));
        } catch (const std::exception& e) {
            j->error = e.what();
        }
    });
    care_job_ = std::move(job);
}

void App::startCareKeep(std::size_t index, std::vector<study::GameId> damaged) {
    if (care_job_) return;
    const auto db = shelf().open(index);
    if (!db || db->readOnly()) return;
    auto job     = std::make_unique<CareJob>();
    job->kind    = CareJob::Kind::Keep;
    job->folder  = shelf().entries()[index].folder;
    job->started = std::chrono::steady_clock::now();
    CareJob* j   = job.get();
    j->done      = std::async(std::launch::async, [db, j, damaged = std::move(damaged)] {
        study::enterBackground();
        try {
            j->set_aside = db->keepReadable(damaged);
        } catch (const std::exception& e) {
            j->error = e.what();
        }
    });
    care_job_    = std::move(job);
}

void App::pollCare() {
    pollFideList();
    if (!care_job_ || care_job_->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto job = std::move(care_job_);
    job->done.get();
    const auto index = indexOf(shelf(), job->folder);
    const bool here  = care_ && care_->folder == job->folder;
    switch (job->kind) {
        case CareJob::Kind::Duplicates:
            if (here) {  // a search cancelled kept too: it does not start again by itself
                care_->duplicates    = std::move(job->duplicates);
                care_->duplicates_of = job->generation;
                care_->since         = job->since;
            }
            break;
        case CareJob::Kind::Names:
            if (here) {
                care_->names      = std::move(job->names);
                care_->names_of   = job->generation;
                care_->names_list = job->names_list;
                care_->confirm    = 0;
                care_->answered.clear();
            }
            break;
        case CareJob::Kind::Check:
            if (here && !job->check.cancelled) {
                care_->check   = std::move(job->check);
                care_->checked = std::time(nullptr);
            }
            break;
        case CareJob::Kind::Keep:
            if (here) {
                if (!job->error.empty()) {
                    care_->error = trf("Not done: {}.", job->error);
                    break;
                }
                Care::Undo undo;
                undo.what = trn("{} damaged game set aside", "{} damaged games set aside", job->set_aside,
                                ui::exactCount(job->set_aside));
                undo.kept = true;
                care_->undo.push_back(std::move(undo));
                care_->said = trn("{} damaged game set aside, its number kept. Ctrl+Z undoes.",
                                  "{} damaged games set aside, their numbers kept. Ctrl+Z undoes.", job->set_aside,
                                  ui::exactCount(job->set_aside));
                care_->error.clear();
                care_->check.reset();
            }
            if (job->error.empty() && index < shelf().entries().size()) shelf().rebuildTree(index);
            break;
        case CareJob::Kind::Compact:
            if (here) {
                care_->disk_full = job->compacted.error == "disk full";
                if (!job->compacted.error.empty() && !care_->disk_full)
                    care_->error =
                        trf("The compaction stopped: {}. Nothing changed in the database.", job->compacted.error);
                if (job->compacted.done) {
                    care_->undo.clear();  // the versions they would put back are gone from the files
                    care_->said =
                        job->compacted.undoable
                            ? trf("Compacted: {} given back once RookStudio closes.", formatBytes(job->compacted.bytes))
                            : trf("Compacted: {} given back.", formatBytes(job->compacted.bytes));
                }
                care_->compacted = std::move(job->compacted);
            }
            break;
        case CareJob::Kind::Merge:
        case CareJob::Kind::Players:
        case CareJob::Kind::Restore:
            if (!job->error.empty()) {
                if (job->crosstable && crosstable_ && crosstable_->folder == job->folder)
                    crosstable_->error = trf("Not done: {}.", job->error);
                else if (here)
                    care_->error = trf("Not done: {}.", job->error);
                break;
            }
            if (job->crosstable) {  // asked from a crosstable (#92): its undo, and what it says, there
                if (job->undo) {
                    if (crosstable_undo_folder_ != job->folder) crosstable_undo_.clear();
                    crosstable_undo_folder_ = job->folder;
                    crosstable_undo_.push_back(std::move(*job->undo));
                }
                if (crosstable_ && crosstable_->folder == job->folder) {
                    crosstable_->said = job->said;
                    crosstable_->error.clear();
                }
            } else if (here) {
                if (job->undo) care_->undo.push_back(std::move(*job->undo));
                care_->said = job->said;
                care_->error.clear();
                if (job->kept) care_->check.reset();  // the damaged segments back: said again as when opened
            }
            // Merges and undoings change games: the opening tree is built again; the table of names changes none.
            if (job->kind != CareJob::Kind::Players && index < shelf().entries().size()) shelf().rebuildTree(index);
            if (db_outcome_ && db_outcome_->folder == job->folder && job->kind == CareJob::Kind::Merge)
                db_outcome_->duplicates.clear();  // merged: no longer to say
            break;
    }
}

namespace {

// The progress of a task as a bar, its Cancel beside.
bool taskBar(const char* what, uint64_t done, uint64_t total, double seconds) {
    ImGui::TextDisabled("%s", what);
    const float share = total ? static_cast<float>(static_cast<double>(done) / static_cast<double>(total)) : 0.0f;
    ImGui::ProgressBar(std::min(1.0f, share), ImVec2(-px(110), 0),
                       fmt::format("{} % · {}", static_cast<int>(share * 100), ui::formatDuration(seconds)).c_str());
    ImGui::SameLine();
    return ImGui::Button(TR("Cancel"));
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// The page.

void App::drawCare() {
    auto& s          = shelf();
    const auto index = indexOf(s, care_->folder);
    const auto db    = index < s.entries().size() ? s.open(index) : nullptr;
    if (!db) {
        care_.reset();
        return;
    }
    const auto snap   = db->snapshot();
    const auto& entry = s.entries()[index];
    ui::TopicRegion topic(guide::topics::Maintenance);

    const bool typing = ImGui::GetIO().WantTextInput;
    const bool escape =
        !typing && ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup);
    if (escape && care_->pair >= 0) {
        care_->pair = -1;
    } else if (ui::iconButton("care_back", ui::Icon::Back, TR("Databases")) || escape) {
        care_.reset();
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("›");
    ImGui::SameLine();
    ImGui::TextUnformatted(entry.name.c_str());
    ImGui::SameLine();
    const auto games = snap->games() - snap->deletedCount();
    ImGui::TextDisabled("%s", trn("{} game", "{} games", games, ui::exactCount(games)).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("›");
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("Maintenance"));
    ImGui::SameLine(0, px(16));
    ImGui::TextDisabled("%s", TR("Esc: the databases"));
    ImGui::SameLine();
    ui::guideLink("care_help", guide::topics::Maintenance);

    // The three parts.
    const char* parts[] = {TR("Duplicate games"), TR("Players' names"), TR("Check and compact")};
    if (ImGui::BeginTabBar("##care_parts")) {
        const int asked = care_->tab_shown != static_cast<int>(care_->part) ? static_cast<int>(care_->part) : -1;
        for (int i = 0; i < 3; ++i) {
            if (ImGui::BeginTabItem(parts[i], nullptr, asked == i ? ImGuiTabItemFlags_SetSelected : 0)) {
                if (asked < 0) care_->part = static_cast<CarePart>(i);
                ImGui::EndTabItem();
            }
        }
        care_->tab_shown = static_cast<int>(care_->part);
        ImGui::EndTabBar();
    }

    // Read-only (M13, board 1, errors): another program writes it.
    if (db->readOnly()) {
        ui::beginCard("care_read_only");
        ui::inlineIcon(ui::Icon::Lock, ui::MUTED);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(trf("“{}” is written by another program", entry.name).c_str());
        ImGui::TextDisabled("%s", TR("Open read-only: the list, the tree and the searches work; the maintenance waits "
                                     "until that program lets it go (read again every 2 s)."));
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ui::endCard();
        ImGui::Dummy(ImVec2(0, px(6)));
    }

    // What was just done, and why something could not be.
    if (!care_->error.empty() || !care_->said.empty()) {
        ImGui::PushTextWrapPos();
        if (!care_->error.empty())
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", care_->error.c_str());
        else
            ImGui::TextDisabled("%s", care_->said.c_str());
        ImGui::PopTextWrapPos();
    }

    // The task running, with its progress and Cancel.
    if (care_job_ && care_job_->folder == care_->folder) {
        auto& j              = *care_job_;
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - j.started).count();
        bool cancel          = false;
        ui::beginCard("care_job");
        switch (j.kind) {
            case CareJob::Kind::Duplicates:
                cancel = taskBar(TR("Looking for the same games from other sources…"), j.duplicates_progress.done,
                                 j.duplicates_progress.total, seconds);
                break;
            case CareJob::Kind::Names:
                cancel =
                    taskBar(TR("Reading the players' names…"), j.names_progress.done, j.names_progress.total, seconds);
                break;
            case CareJob::Kind::Check:
                cancel = taskBar(TR("Checking: every segment read again, every game replayed…"), j.check_progress.done,
                                 j.check_progress.total, seconds);
                ImGui::TextDisabled("%s", TR("The database stays readable and writable all along."));
                break;
            case CareJob::Kind::Compact: {
                const auto segment = j.compact_progress.segment.load(), segments = j.compact_progress.segments.load();
                cancel = taskBar(
                    trf("Compacting: segment {} of {}", std::max<uint64_t>(segment, 1), std::max<uint64_t>(segments, 1))
                        .c_str(),
                    segment, std::max<uint64_t>(segments, 1), seconds);
                ImGui::PushTextWrapPos();
                ImGui::TextDisabled("%s", TR("The list, the tree and the searches work; imports and saves into it "
                                             "wait."));
                ImGui::PopTextWrapPos();
                break;
            }
            default:
                ImGui::TextDisabled("%s", TR("Writing…"));
                break;
        }
        if (cancel) j.cancel = true;
        ui::endCard();
        ImGui::Dummy(ImVec2(0, px(6)));
    }

    ImGui::BeginDisabled(db->readOnly());
    switch (care_->part) {
        case CarePart::Duplicates: {
            ui::TopicRegion part(guide::topics::DuplicateGames);
            if (care_->open_first_pair && care_->duplicates && !care_->duplicates->groups.empty()) {
                care_->open_first_pair = false;
                care_->pair            = 0;
                for (const auto id : care_->duplicates->groups.front().games) {
                    care_->pair_games.push_back(snap->game(id));
                    care_->pair_sources.push_back(sourceOf(*snap, id));
                }
                care_->choice = study::fullestChoice(care_->pair_games);
            }
            if (care_->pair >= 0 && care_->duplicates &&
                care_->pair < static_cast<int>(care_->duplicates->groups.size()))
                drawCarePair(index);
            else
                drawCareDuplicates(index);
            break;
        }
        case CarePart::Names: {
            ui::TopicRegion part(guide::topics::PlayersNames);
            drawCareNames(index);
            break;
        }
        case CarePart::Checks: {
            ui::TopicRegion part(guide::topics::CheckCompact);
            drawCareChecks(index);
            break;
        }
    }
    ImGui::EndDisabled();
    if (!care_) return;  // left for the list, or for an import

    // Ctrl+Z: the last merge, names brought together or games set aside, undone.
    if (!typing && !care_job_ && !db->readOnly() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
        if (!care_->undo.empty())
            careUndo();
        else
            care_->said = TR("Nothing to undo here.");
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Duplicate games.

void App::drawCareDuplicates(std::size_t index) {
    auto& c         = *care_;
    const auto db   = shelf().open(index);
    const auto snap = db->snapshot();
    const bool idle = !care_job_;
    // Looked for when shown, and again once the database changed.
    if (idle && (!c.duplicates || c.duplicates_of != snap->generation())) {
        auto job        = std::make_unique<CareJob>();
        job->kind       = CareJob::Kind::Duplicates;
        job->folder     = c.folder;
        job->since      = c.since;
        job->generation = snap->generation();
        job->started    = std::chrono::steady_clock::now();
        CareJob* j      = job.get();
        j->done         = std::async(std::launch::async, [snap, j] {
            study::enterBackground();
            j->duplicates = study::findDuplicates(snap, j->since, j->duplicates_progress, j->cancel);
        });
        care_job_       = std::move(job);
    }
    if (!c.duplicates) return;
    if (c.duplicates->cancelled) {
        ui::beginCard("care_duplicates_stopped");
        ImGui::TextUnformatted(TR("The search for duplicates was cancelled."));
        if (ImGui::Button(TR("Look for them"))) c.duplicates_of = ~uint64_t{0};  // started again
        ui::endCard();
        return;
    }
    const auto& found = *c.duplicates;
    std::vector<std::size_t> live;  // the groups still to merge: their games still there
    for (std::size_t g = 0; g < found.groups.size(); ++g) {
        const auto& games = found.groups[g].games;
        if (std::all_of(games.begin(), games.end(),
                        [&](study::GameId id) { return id < snap->games() && !snap->deleted(id); }))
            live.push_back(g);
    }
    const auto same    = found.count(study::DuplicateKind::SameMoves),
               shorter = found.count(study::DuplicateKind::Shorter),
               differ  = found.count(study::DuplicateKind::Results);

    if (found.groups.empty()) {
        // The empty state (M13, board 1, errors).
        ui::beginCard("care_no_duplicates");
        ui::inlineIcon(ui::Icon::Info, ui::MUTED);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(c.since ? TR("No game of the import was already there from another source")
                                       : TR("No duplicate from another source"));
        ImGui::TextDisabled("%s", trf("The {} games looked at in {}. A database made of one source has none: the "
                                      "duplicates of one file do not come in at the import.",
                                      ui::exactCount(found.games), tookText(found.seconds))
                                      .c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        if (c.since && ImGui::SmallButton(TR("Look in the whole database"))) {
            c.since         = 0;
            c.duplicates_of = ~uint64_t{0};
        }
        ui::endCard();
        return;
    }

    // What was found, and Merge all.
    ui::beginCard("care_duplicates");
    ImGui::PushTextWrapPos();
    ImGui::TextUnformatted(c.since ? TR("The import's games already there from another source")
                                   : TR("The same games from two sources"));
    ImGui::TextDisabled("%s", trf("Looked for in the background: the same moves, the same players (read in the table "
                                  "of names), the same year. The {} games looked at in {}; a game already there "
                                  "exactly the same never came in twice.",
                                  ui::exactCount(found.games), tookText(found.seconds))
                                  .c_str());
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, px(2)));
    const float w = px(170);
    figure(ui::exactCount(same), TR("the same moves, headers written differently"), w);
    ImGui::SameLine(0, px(28));
    figure(ui::exactCount(shorter), TR("one is shorter (a broadcast cut short)"), w);
    ImGui::SameLine(0, px(28));
    figure(ui::exactCount(differ), TR("results that differ: to see one at a time"), w);
    std::vector<study::DuplicateGroup> to_merge;
    for (const auto g : live)
        if (mergeable(found.groups[g])) to_merge.push_back(found.groups[g]);
    ImGui::Dummy(ImVec2(0, px(2)));
    ImGui::BeginDisabled(to_merge.empty() || !idle);
    const auto merge_label = trf("Merge all · {}", ui::exactCount(to_merge.size()));
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(tinted(ui::ACCENT, 200)));
    const bool enter = !ImGui::GetIO().WantTextInput && c.pair < 0 && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
    if ((ImGui::Button(merge_label.c_str()) || enter) && !to_merge.empty() && idle)
        startCareMerges(index, to_merge, {});
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", TR("Each group merged into one game, the fullest of each field; the others marked "
                                   "deleted. The results that differ are left to you."));
    ImGui::SameLine(0, px(6));
    ImGui::TextDisabled("%s", TR("Enter · Ctrl+Z undoes"));
    ImGui::SameLine(0, px(14));
    if (ImGui::Button(TR("See in the list"))) {
        std::vector<study::GameId> ids;
        for (const auto g : live)
            for (const auto id : found.groups[g].games) ids.push_back(id);
        std::sort(ids.begin(), ids.end());
        db_only_      = std::move(ids);
        db_only_said_ = TR("the duplicate games");
        db_list_      = true;
        db_row_       = 0;
        care_.reset();
        return;
    }
    // Where they come from: the pairs of sources, the most first; counted once for each result.
    const auto where_of = fmt::format("{}|{}", c.duplicates_of, live.size());
    if (c.where_of != where_of) {
        c.where_of = where_of;
        std::map<std::string, std::size_t> pairs;
        for (const auto g : live) {
            std::set<std::string> sources;
            std::set<std::string> files;
            for (const auto id : found.groups[g].games) {
                sources.insert(sourceOf(*snap, id));
                files.insert(snap->source(id));
            }
            std::string key;
            if (files.size() == 1 && !files.begin()->empty()) {
                key = TR("the same file imported twice");
            } else {
                for (const auto& source : sources) key = key.empty() ? source : trf("{} and {}", key, source);
                if (sources.size() == 1) key = trf("{}, twice", *sources.begin());
            }
            ++pairs[key];
        }
        std::vector<std::pair<std::size_t, std::string>> ranked;
        for (const auto& [k, n] : pairs) ranked.emplace_back(n, k);
        std::sort(ranked.rbegin(), ranked.rend());
        c.where.clear();
        for (std::size_t i = 0; i < ranked.size() && i < 4; ++i)
            c.where += (c.where.empty() ? "" : " · ") +
                       fmt::format("{} ({})", ranked[i].second, ui::exactCount(ranked[i].first));
    }
    if (!c.where.empty()) {
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", trf("Where they come from: {}.", c.where).c_str());
        ImGui::PopTextWrapPos();
    }
    ui::endCard();
    ImGui::Dummy(ImVec2(0, px(6)));

    // The groups: a click opens one, compared field by field.
    if (ImGui::BeginTable("##care_groups", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0, std::max(px(160), ImGui::GetContentRegionAvail().y - px(40))))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(TR("The game"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn(TR("Its sources"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn(TR("What differs"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(live.size()));
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                const auto g      = live[static_cast<std::size_t>(r)];
                const auto& group = found.groups[g];
                const auto first  = snap->row(group.games.front());
                const auto ids    = snap->rowIds(group.games.front());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(r);
                const auto title = fmt::format("{} – {}", snap->shownName(ids.white), snap->shownName(ids.black));
                if (ImGui::Selectable(title.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                    c.pair = static_cast<int>(g);
                    c.pair_games.clear();
                    c.pair_sources.clear();
                    for (const auto id : group.games) {
                        c.pair_games.push_back(snap->game(id));
                        c.pair_sources.push_back(sourceOf(*snap, id));
                    }
                    c.choice = study::fullestChoice(c.pair_games);
                }
                ImGui::PopID();
                ImGui::SameLine();
                std::string where_game(first.event);
                if (!first.round.empty()) where_game += (where_game.empty() ? "" : ", ") + trf("r. {}", first.round);
                where_game += " · " + resultText(first.result);
                ImGui::TextDisabled("· %s", where_game.c_str());
                ImGui::TableNextColumn();
                std::string sources;
                for (const auto id : group.games) {
                    std::string one = sourceOf(*snap, id);
                    if (group.kind == study::DuplicateKind::Shorter)
                        one += " (" +
                               trn("{} ply", "{} plies", snap->numbers(id).plies,
                                   ui::exactCount(snap->numbers(id).plies)) +
                               ")";
                    sources += (sources.empty() ? "" : " · ") + one;
                }
                ImGui::TextUnformatted(sources.c_str());
                ImGui::TableNextColumn();
                if (group.kind == study::DuplicateKind::Results) {
                    std::string results, said;
                    for (const auto id : group.games) {
                        results += (results.empty() ? "" : " / ") + resultText(snap->numbers(id).result);
                        said += (said.empty() ? "" : "\n") +
                                trf("{} in {}", resultText(snap->numbers(id).result), sourceOf(*snap, id));
                    }
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                                       trf("results {}: to see", results).c_str());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", said.c_str());
                } else if (group.kind == study::DuplicateKind::Shorter) {
                    ImGui::TextUnformatted(TR("one stops early: the longer game's moves kept"));
                } else {
                    std::vector<study::ImportedGame> games;
                    for (const auto id : group.games) games.push_back(snap->game(id));
                    ImGui::TextUnformatted(differences(study::mergeFields(games)).c_str());
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", TR("At the import, a game already there from another source is told in its report, with "
                                 "“Merge them”: never merged without your click."));
    ImGui::PopTextWrapPos();
}

void App::drawCarePair(std::size_t index) {
    auto& c           = *care_;
    const auto db     = shelf().open(index);
    const auto snap   = db->snapshot();
    const auto& found = *c.duplicates;
    const auto& group = found.groups[static_cast<std::size_t>(c.pair)];
    const auto& games = c.pair_games;
    if (games.size() != group.games.size() || games.empty()) {
        c.pair = -1;
        return;
    }
    const auto fields = study::mergeFields(games);
    const auto merged = study::mergeGames(games, c.choice);
    const auto mfield = study::mergeFields({merged}).front();
    const auto ids    = snap->rowIds(group.games.front());
    const auto first  = snap->row(group.games.front());
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
    std::string title = fmt::format("{} – {}", snap->shownName(ids.white), snap->shownName(ids.black));
    if (!first.event.empty()) title += " · " + std::string(first.event);
    if (!first.round.empty()) title += " · " + trf("round {}", first.round);
    title += " · " + resultText(merged.result);
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", trf("group {} of {}", c.pair + 1, ui::exactCount(found.groups.size())).c_str());

    // A value wider than its cell (a broadcast's address) ends in "…", whole under the mouse.
    const auto fitted = [](const std::string& text, float room) {
        if (ImGui::CalcTextSize(text.c_str()).x <= room) return text;
        std::string cut = text;
        while (!cut.empty() && ImGui::CalcTextSize((cut + "…").c_str()).x > room) {
            while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80) cut.pop_back();
            if (!cut.empty()) cut.pop_back();
        }
        return cut + "…";
    };
    const int columns = static_cast<int>(games.size()) + 2;
    if (ImGui::BeginTable("##care_pair", columns,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, px(150));
        for (std::size_t g = 0; g < games.size(); ++g) {
            const auto label =
                fmt::format("{} · {}", c.pair_sources[g], trf("No. {}", ui::exactCount(group.games[g] + 1)));
            ImGui::TableSetupColumn(label.c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        }
        ImGui::TableSetupColumn(TR("The merged game"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        for (std::size_t f = 0; f < study::MERGE_FIELDS; ++f) {
            const auto field = static_cast<study::MergeField>(f);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", fieldName(field));
            bool all_same = true;
            for (std::size_t g = 1; g < games.size(); ++g) all_same = all_same && fields[g][f] == fields[0][f];
            for (std::size_t g = 0; g < games.size(); ++g) {
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(f * 16 + g));
                const auto text   = fieldText(field, fields[g][f], games[g].plies);
                const bool chosen = c.choice[f] == g;
                if (chosen && !all_same) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, tinted(ui::ACCENT, 55));
                if (all_same) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                const auto shown = fitted(text, ImGui::GetContentRegionAvail().x);
                if (ImGui::Selectable(shown.c_str(), false)) c.choice[f] = static_cast<uint8_t>(g);
                if (shown != text && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text.c_str());
                if (all_same) ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::TableNextColumn();
            const auto text  = fieldText(field, mfield[f], merged.plies);
            const auto shown = fitted(text, ImGui::GetContentRegionAvail().x);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", shown.c_str());
            if (shown != text && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    const bool idle = !care_job_;
    const bool keys = !ImGui::GetIO().WantTextInput && idle;
    const auto next = [&] {
        // The next group still to see: its games all there.
        for (auto g = static_cast<std::size_t>(c.pair) + 1; g < found.groups.size(); ++g) {
            const auto& other = found.groups[g].games;
            if (std::all_of(other.begin(), other.end(), [&](study::GameId id) { return !snap->deleted(id); })) {
                c.pair = static_cast<int>(g);
                c.pair_games.clear();
                c.pair_sources.clear();
                for (const auto id : other) {
                    c.pair_games.push_back(snap->game(id));
                    c.pair_sources.push_back(sourceOf(*snap, id));
                }
                c.choice = study::fullestChoice(c.pair_games);
                return;
            }
        }
        c.pair = -1;
    };
    ImGui::BeginDisabled(!idle);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(tinted(ui::ACCENT, 200)));
    if (ImGui::Button(TR("Merge · Enter")) || (keys && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
        startCareMerges(index, {group}, {c.choice});
        next();
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button(TR("Keep both"))) {
        // Told apart for good: never offered again (Ctrl+Z puts them back among the duplicates).
        auto apart = snap->players()->apart();
        for (std::size_t a = 0; a < group.games.size(); ++a)
            for (std::size_t b = a + 1; b < group.games.size(); ++b) apart.emplace_back(group.games[a], group.games[b]);
        startCarePlayers(index, study::PlayerTable(snap->players()->players(), std::move(apart)),
                         TR("two games kept apart"),
                         TR("Kept apart: never offered as duplicates again. Ctrl+Z undoes."));
        next();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Two games, not one: never offered as duplicates again."));
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(TR("Next group · ↓")) || (keys && ImGui::IsKeyPressed(ImGuiKey_DownArrow))) next();
    ImGui::SameLine();
    if (ImGui::Button(TR("Back to the duplicates"))) c.pair = -1;
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s",
                        trf("A click on a cell takes that field from that source. The merged game keeps the number "
                            "of the oldest (No. {}); the other is marked deleted, and Ctrl+Z brings it back. "
                            "Where each field came from is kept with the game (its MergedFrom tag); a "
                            "broadcast's game keeps its source.",
                            ui::exactCount(group.games.front() + 1))
                            .c_str());
    ImGui::PopTextWrapPos();
}

// ---------------------------------------------------------------------------------------------------------------------
// Players' names and FIDE's rating list.

void App::drawFideListCard() {
    const auto* kept = fideKept();
    ui::TopicRegion topic(guide::topics::FideList);
    ui::beginCard("care_fide");
    ImGui::PushTextWrapPos();
    ui::inlineIcon(ui::Icon::Download, ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("FIDE's rating list"));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("· downloaded when you ask, never shipped with RookStudio"));
    if (fide_job_) {
        auto& j          = *fide_job_;
        bool cancel      = false;
        const auto phase = j.phase.load();
        if (phase == fide::Job::Phase::Asking) {
            ImGui::TextDisabled("%s", TR("Asking ratings.fide.com for its latest list…"));
            cancel = ImGui::SmallButton(TR("Cancel"));
        } else if (phase == fide::Job::Phase::Downloading) {
            const auto bytes = j.download.bytes.load(), size = j.download.size.load();
            ImGui::TextDisabled("%s", (size ? trf("Downloading: {} of {} · Cancel, or a cut, resumes where it "
                                                  "stopped",
                                                  formatBytes(bytes), formatBytes(size))
                                            : trf("Downloading: {}", formatBytes(bytes)))
                                          .c_str());
            ImGui::ProgressBar(size ? static_cast<float>(static_cast<double>(bytes) / static_cast<double>(size)) : 0.0f,
                               ImVec2(-px(110), 0));
            ImGui::SameLine();
            cancel = ImGui::Button(TR("Cancel"));
        } else {
            const auto bytes = j.reading.bytes.load(), total = j.reading.total.load();
            ImGui::TextDisabled("%s", TR("Reading the list into your profile…"));
            ImGui::ProgressBar(
                total ? static_cast<float>(static_cast<double>(bytes) / static_cast<double>(total)) : 0.0f,
                ImVec2(-px(110), 0));
            ImGui::SameLine();
            cancel = ImGui::Button(TR("Cancel"));
        }
        if (cancel) j.cancel = true;
    } else if (kept) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::GOOD), "%s",
                           trf("Kept: {}", fide::monthText(kept->month)).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled(
            "%s", trf("· published on {} · {} players, {} rated · {} in your profile", kept->published,
                      ui::exactCount(kept->info.players), ui::exactCount(kept->info.rated), formatBytes(kept->bytes))
                      .c_str());
        if (ImGui::SmallButton(TR("Look for a newer list"))) startFideList();
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Remove it"))) {
            fide_list_.reset();  // let go before its file goes
            fide::removeKept(storage_.root());
            fide_kept_.reset();
            if (care_) care_->names_of = ~uint64_t{0};
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("The list goes from your profile; the players already brought together stay as "
                                       "they are."));
    } else {
        ImGui::TextUnformatted(
            TR("To match your players' names with FIDE's: their FIDE ids, federations, titles and "
               "ratings. FIDE's combined list, a zip of about 43 MB, about two million players; "
               "read into about 90 MB of your profile."));
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(tinted(ui::ACCENT, 200)));
        if (ImGui::Button(TR("Download · 43 MB"))) startFideList();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button(TR("A file already downloaded…")))
            dialogs_.openFile(Dialogs::Purpose::FideList, {{TR("FIDE's list"), "zip;txt"}});
    }
    // What went wrong last (M13, board 1, errors), and its gesture.
    if (!fide_job_ && fide_outcome_) {
        const auto& o = *fide_outcome_;
        if (o.format) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               o.month.empty()
                                   ? TR("The list does not read like the others: its columns changed.")
                                   : trf("The list of {} does not read like the others: its columns changed.",
                                         fide::monthText(o.month))
                                         .c_str());
            ImGui::TextDisabled("%s", kept ? trf("The list of {} stays in service; the names brought together do not "
                                                 "move.",
                                                 fide::monthText(kept->month))
                                                 .c_str()
                                           : TR("Nothing was kept of it."));
            if (ImGui::SmallButton(TR("Report it")))
                SDL_OpenURL(
                    "https://github.com/jromang/rookstudio/issues/new?title=FIDE%27s%20list%20does%20not%20read");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TR("Opens RookStudio's issues in your browser; nothing of the list is sent."));
        } else if (o.cut > 0) {
            ui::inlineIcon(ui::Icon::WifiOff, ui::WARN);
            ImGui::SameLine();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               trf("The download stopped at {}.", formatBytes(o.cut)).c_str());
            ImGui::TextDisabled(
                "%s", o.error.empty() ? TR("What came is kept.") : trf("What came is kept ({}).", o.error).c_str());
            if (ImGui::SmallButton(TR("Resume (the rest only)"))) startFideList();
        } else if (o.same) {
            ImGui::TextDisabled("%s", trf("FIDE has published no newer list: the list of {} is the latest.",
                                          fide::monthText(o.kept.month))
                                          .c_str());
        } else if (!o.ok && !o.cancelled && !o.error.empty()) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               trf("FIDE's list could not be taken: {}.", o.error).c_str());
            if (ImGui::SmallButton(TR("Try again"))) startFideList();
        }
    }
    ImGui::TextDisabled("%s", TR("FIDE gives its list no licence: it stays on your machine, never sent nor shared. "
                                 "What leaves the machine: the request for the file to ratings.fide.com, when you "
                                 "download it."));
    ImGui::PopTextWrapPos();
    ui::endCard();
    ImGui::Dummy(ImVec2(0, px(6)));
}

void App::drawCareNames(std::size_t index) {
    auto& c          = *care_;
    const auto db    = shelf().open(index);
    const auto snap  = db->snapshot();
    const auto list  = fideList();
    const auto month = list ? list->info().month : std::string();
    drawFideListCard();
    // Read again when shown, when the database changed, when the list did.
    if (!care_job_ && !fide_job_ && (!c.names || c.names_of != snap->generation() || c.names_list != month)) {
        auto job        = std::make_unique<CareJob>();
        job->kind       = CareJob::Kind::Names;
        job->folder     = c.folder;
        job->generation = snap->generation();
        job->names_list = month;
        job->started    = std::chrono::steady_clock::now();
        CareJob* j      = job.get();
        j->done         = std::async(std::launch::async, [snap, list, j] {
            study::enterBackground();
            j->names = study::analyseNames(snap, list.get(), j->names_progress, j->cancel);
        });
        care_job_       = std::move(job);
    }
    if (!c.names) return;
    if (c.names->cancelled) {
        ui::beginCard("care_names_stopped");
        ImGui::TextUnformatted(TR("The reading of the players' names was cancelled."));
        if (ImGui::Button(TR("Read them"))) c.names_of = ~uint64_t{0};  // started again
        ui::endCard();
        return;
    }
    const auto& a       = *c.names;
    const bool idle     = !care_job_;
    const auto& name    = shelf().entries()[index].name;
    const auto bring    = a.bringing();
    // A player found at once that brings nothing (one spelling, no FIDE card to take) counts with those kept.
    const auto brings = [](const study::PlayerProposal& p) {
        return p.spellings.size() > 1 || !p.candidates.empty() || p.player.has_value();
    };
    const auto kept_as_is = [&](const study::PlayerProposal& p) {
        return p.match == study::NameMatch::None || (p.match == study::NameMatch::Auto && !brings(p));
    };
    std::size_t confirm = 0, kept = 0, done = 0;
    for (std::size_t i = 0; i < a.proposals.size(); ++i) {
        const auto m = a.proposals[i].match;
        if (m == study::NameMatch::Confirm && !c.answered.count(i)) ++confirm;
        if (kept_as_is(a.proposals[i])) ++kept;
        if (m == study::NameMatch::Done) ++done;
    }

    ui::beginCard("care_names");
    ImGui::PushTextWrapPos();
    ImGui::TextUnformatted(trf("{} players in “{}”, under {} spellings", ui::exactCount(a.proposals.size()), name,
                               ui::exactCount(a.spellings.size()))
                               .c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", a.list_month.empty()
                                  ? TR("· without FIDE's list")
                                  : trf("· matched with FIDE's list of {}", fide::monthText(a.list_month)).c_str());
    ImGui::PopTextWrapPos();
    const float w = px(190);
    figure(ui::exactCount(bring),
           TR("brought together at once: a FIDE id in the games, or a single player of that name"), w);
    ImGui::SameLine(0, px(28));
    figure(ui::exactCount(confirm), TR("to confirm: several players possible"), w);
    ImGui::SameLine(0, px(28));
    figure(
        ui::exactCount(kept),
        a.list_month.empty() ? TR("without FIDE's list: kept as they are") : TR("with no FIDE card: kept as they are"),
        w);
    if (done > 0) {
        ImGui::SameLine(0, px(28));
        figure(ui::exactCount(done), TR("brought together already"), w);
    }
    ImGui::BeginDisabled(bring == 0 || !idle);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(tinted(ui::ACCENT, 200)));
    const bool keys = !ImGui::GetIO().WantTextInput && idle;
    if (ImGui::Button(trf("Bring together the {} · Enter", ui::exactCount(bring)).c_str()) ||
        (keys && c.names_shown != 0 && bring > 0 && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
        std::vector<study::Bringing> chosen;
        for (std::size_t i = 0; i < a.proposals.size(); ++i) {
            const auto& p = a.proposals[i];
            if (p.match == study::NameMatch::Auto && (p.spellings.size() > 1 || !p.candidates.empty() || p.player))
                chosen.push_back({i, p.candidates.empty() ? std::nullopt : std::optional<std::size_t>(0)});
        }
        startCarePlayers(index, study::bringTogether(*snap->players(), a, chosen, list.get(), *snap),
                         trn("{} player brought together", "{} players brought together", chosen.size(),
                             ui::exactCount(chosen.size())),
                         trn("Brought together: {} player. The games are untouched; Ctrl+Z undoes.",
                             "Brought together: {} players. The games are untouched; Ctrl+Z undoes.", chosen.size(),
                             ui::exactCount(chosen.size())));
    }
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(trf("The {} to confirm", ui::exactCount(confirm)).c_str())) {
        c.names_shown  = 0;
        c.names_chosen = true;
    }
    ImGui::SameLine(0, px(8));
    ImGui::TextDisabled("%s", TR("Ctrl+Z undoes"));
    ui::endCard();
    ImGui::Dummy(ImVec2(0, px(6)));

    // Which players: to confirm, brought together at once, kept as they are.
    const char* shows[] = {TR("To confirm"), TR("Brought together at once"), TR("Kept as they are"),
                           TR("Brought together already")};
    for (int i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine();
        if (ImGui::RadioButton(shows[i], c.names_shown == i)) {
            c.names_shown  = i;
            c.names_chosen = true;
        }
    }
    const auto want = c.names_shown == 0   ? study::NameMatch::Confirm
                      : c.names_shown == 1 ? study::NameMatch::Auto
                      : c.names_shown == 2 ? study::NameMatch::None
                                           : study::NameMatch::Done;
    std::vector<std::size_t> rows;
    for (std::size_t i = 0; i < a.proposals.size(); ++i) {
        const auto& p = a.proposals[i];
        const bool in = want == study::NameMatch::None   ? kept_as_is(p)
                        : want == study::NameMatch::Auto ? p.match == want && brings(p)
                                                         : p.match == want;
        if (in && !(want == study::NameMatch::Confirm && c.answered.count(i))) rows.push_back(i);
    }
    if (want == study::NameMatch::Confirm && rows.empty() && !c.names_chosen && bring > 0) {
        c.names_shown = 1;  // nothing to confirm: what is brought together at once, shown
        return;
    }

    // A player's lines: the name, the FIDE card, its spellings with their source and games, why.
    const auto card_line = [&](const study::FidePlayer& p) {
        std::string line = " · " + p.federation;
        if (p.born) line += " · " + trf("born {}", p.born);
        if (!p.title.empty()) line += " · " + p.title;
        if (p.standard)
            line += " · " + trf("standard {}", p.standard);
        else if (p.rapid)
            line += " · " + trf("rapid {}", p.rapid);
        else if (p.blitz)
            line += " · " + trf("blitz {}", p.blitz);
        if (p.inactive) line += " · " + std::string(TR("inactive"));
        return line;
    };
    const auto spellings_line = [&](const study::PlayerProposal& p) {
        std::string line;
        for (std::size_t k = 0; k < p.spellings.size() && k < 6; ++k) {
            const auto& sp = a.spellings[p.spellings[k]];
            auto source    = study::sourceLabel(sp.source, study::ImportedGame{});
            if (source.empty()) source = study::sourceLabel({}, snap->game(sp.sample));
            line += (line.empty() ? "" : "   ") + std::string(snap->name(sp.name)) +
                    (source.empty() ? "" : " · " + source) + " · " + ui::exactCount(sp.games);
        }
        if (p.spellings.size() > 6) line += "   " + trf("and {} more", p.spellings.size() - 6);
        return line;
    };
    const auto why = [&](const study::PlayerProposal& p) -> std::string {
        switch (p.reason) {
            case study::NameReason::Table:
                return TR("In the table of names already.");
            case study::NameReason::FideIdInGames:
                return brings(p) ? TR("Brought together by the FIDE id their games give.")
                                 : TR("Its games give its FIDE id; one spelling only, nothing to bring together.");
            case study::NameReason::OneOnList:
                return TR("A single player of that name on the list.");
            case study::NameReason::Transliteration:
                return TR(
                    "Another transliteration of the name: a single player of that name on the list, the games' "
                    "ratings near theirs.");
            case study::NameReason::Several:
                return TR("Several players of that name on the list.");
            case study::NameReason::NotOnList:
                return p.spellings.size() > 1 ? TR("Not on FIDE's list: kept as they are. Their spellings are brought "
                                                   "together by hand (right click › Bring together).")
                                              : TR("Not on FIDE's list, or not a person's name: kept as it is.");
            case study::NameReason::NoList:
                return p.spellings.size() > 1
                           ? TR("Without FIDE's list: kept as they are, or brought together by hand (right click › "
                                "Bring together).")
                           : TR("Without FIDE's list: kept as it is.");
        }
        return {};
    };

    // To confirm: the first one open, its candidates chosen with ↑ ↓, Enter keeping the choice and going on.
    if (want == study::NameMatch::Confirm && !rows.empty()) {
        c.confirm     = std::min(c.confirm, rows.size() - 1);
        const auto at = rows[c.confirm];
        const auto& p = a.proposals[at];
        auto& pick    = c.picks.try_emplace(at, 0).first->second;
        const int n   = static_cast<int>(std::min<std::size_t>(p.candidates.size(), 6));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(ui::WARN));
        ui::beginCard("care_confirm");
        ImGui::PopStyleColor();
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(p.name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
            trn("· to confirm: {} player of that name on the list", "· to confirm: {} players of that name on the list",
                p.candidates.size(), ui::exactCount(p.candidates.size()))
                .c_str());
        ImGui::TextDisabled("%s", spellings_line(p).c_str());
        if (p.elo_min)
            ImGui::TextDisabled("%s",
                                (p.elo_min == p.elo_max ? trf("Its games give it {}.", p.elo_min)
                                                        : trf("Its games give it {} to {}.", p.elo_min, p.elo_max))
                                    .c_str());
        for (int k = 0; k <= n; ++k) {
            ImGui::PushID(k);
            const bool none = k == n;
            std::string label;
            if (none) {
                label = TR("None of them: another player");
            } else {
                const auto card = list ? list->player(p.candidates[static_cast<std::size_t>(k)]) : study::FidePlayer{};
                label           = std::string(card.name) + card_line(card);
                if (k == 0 && p.elo_min)
                    label += "  " + std::string(TR("— proposed: the nearest to its games' ratings"));
            }
            if (ImGui::RadioButton(label.c_str(), pick == (none ? -1 : k))) pick = none ? -1 : k;
            ImGui::PopID();
        }
        ImGui::TextDisabled("%s", TR("↑ ↓ choose, Enter keeps and goes to the next."));
        ImGui::PopTextWrapPos();
        if (keys) {
            const auto owner = ImGui::GetID("##care_confirm_keys");
            for (const auto key : {ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Enter, ImGuiKey_KeypadEnter})
                ImGui::SetKeyOwner(key, owner);
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) pick = pick < 0 ? -1 : pick + 1 >= n ? -1 : pick + 1;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) pick = pick < 0 ? n - 1 : std::max(0, pick - 1);
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
                if (pick >= 0) {
                    startCarePlayers(index,
                                     study::bringTogether(*snap->players(), a, {{at, static_cast<std::size_t>(pick)}},
                                                          list.get(), *snap),
                                     trf("“{}” confirmed", p.name), trf("Confirmed: “{}”. Ctrl+Z undoes.", p.name));
                }
                c.answered.insert(at);
            }
        }
        ui::endCard();
        ImGui::Dummy(ImVec2(0, px(4)));
    }

    // The others, a line or two each: to confirm, those after the one open.
    if (want == study::NameMatch::Confirm && !rows.empty())
        rows.erase(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(std::min(c.confirm + 1, rows.size())));
    if (rows.empty() && want == study::NameMatch::Confirm) return;
    if (rows.empty()) {
        ImGui::TextDisabled("%s", want == study::NameMatch::Confirm ? TR("Nothing to confirm.") : TR("None."));
        return;
    }
    const float line = ImGui::GetTextLineHeightWithSpacing();
    ImGui::BeginChild("##care_players", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()), line * 3.2f);
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            const auto at = rows[static_cast<std::size_t>(r)];
            const auto& p = a.proposals[at];
            ImGui::PushID(static_cast<int>(at));
            const auto start = ImGui::GetCursorPos();
            if (ImGui::Selectable("##player", false, 0, ImVec2(0, line * 3.0f)) && want == study::NameMatch::Confirm)
                c.confirm += static_cast<std::size_t>(r) + 1;  // the rows after the one open
            // By hand: two spellings or more of one surname and given name, not on the list.
            if ((p.match == study::NameMatch::None) && p.spellings.size() > 1 &&
                ImGui::BeginPopupContextItem("##hand")) {
                if (ImGui::MenuItem(TR("Bring together"))) {
                    startCarePlayers(index,
                                     study::bringTogether(*snap->players(), a, {{at, std::nullopt}}, nullptr, *snap),
                                     trf("“{}” brought together", p.name),
                                     trf("Brought together by hand: “{}”. Ctrl+Z undoes.", p.name));
                }
                ImGui::EndPopup();
            }
            ImGui::SetCursorPos(ImVec2(start.x + px(6), start.y + px(2)));
            ImGui::TextUnformatted(p.name.c_str());
            std::string card;
            if (!p.candidates.empty() && list) {
                card = card_line(list->player(p.candidates.front()));
            } else if (p.player) {
                const auto& pl = snap->players()->players()[*p.player];
                if (pl.fide)
                    card = " · " + pl.federation + (pl.title.empty() ? "" : " · " + pl.title) +
                           (pl.standard ? " · " + trf("standard {}", pl.standard) : "");
            } else if (p.match == study::NameMatch::None && !a.list_month.empty()) {
                card = TR("· not on FIDE's list");
            }
            if (!card.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", card.c_str());
            }
            ImGui::SetCursorPosX(start.x + px(6));
            ImGui::TextDisabled("%s", spellings_line(p).c_str());
            ImGui::SetCursorPosX(start.x + px(6));
            ImGui::TextDisabled("%s", why(p).c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------------------------------------------------
// Check and compact.

void App::drawCareChecks(std::size_t index) {
    auto& c          = *care_;
    auto& s          = shelf();
    const auto db    = s.open(index);
    const auto snap  = db->snapshot();
    const bool idle  = !care_job_;
    const auto facts = s.facts(index);
    const float half = (ImGui::GetContentRegionAvail().x - px(12)) / 2;

    // Check.
    ImGui::BeginChild("##care_check", ImVec2(half, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::PushTextWrapPos();
    ui::inlineIcon(ui::Icon::ShieldCheck, ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("Check"));
    ImGui::TextDisabled("%s",
                        TR("Every segment read again from the disk, every game replayed, its signature and its key "
                           "made again and compared; the opening tree compared with the games. In the "
                           "background, the database readable all along."));
    // A segment that could not be read when the database was opened (M13, board 1, #97): said at once, as a check
    // would say it.
    if (!c.check && !snap->unreadable().empty()) {
        c.check   = study::unreadableReport(*snap);
        c.checked = std::time(nullptr);
    }
    if (c.check) {
        const auto& r = *c.check;
        char when[16] = {};
        std::tm t{};
#ifdef _WIN32
        localtime_s(&t, &c.checked);
#else
        localtime_r(&c.checked, &t);
#endif
        std::strftime(when, sizeof when, "%H:%M", &t);
        if (r.damaged.empty() && r.segments.empty()) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::GOOD), "%s", TR("Sound"));
            ImGui::SameLine();
            ImGui::TextDisabled(
                "%s",
                trf("· {} games read, none damaged · {} · {} · {} · checked at {}", ui::exactCount(r.games),
                    r.tree ? TR("the opening tree up to date") : TR("the opening tree to build"),
                    r.signatures ? TR("the signatures there") : TR("no signatures (a database of the first format)"),
                    formatBytes(facts.bytes), when)
                    .c_str());
            // The tree to build: built in the background, as after an import.
            if (!r.tree && !facts.tree && ImGui::SmallButton(TR("Build the opening tree"))) {
                s.rebuildTree(index);
                c.said = TR("The opening tree is being built in the background.");
            }
        } else {
            // A segment that cannot be read again, damaged games (M13, board 1, errors).
            const auto summary = damageSummary(r, *snap);
            for (const auto& bad : summary.segments) {
                ui::inlineIcon(ui::Icon::Alert, ui::WARN);
                ImGui::SameLine();
                ImGui::TextColored(
                    ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                    (bad.missing ? trf("Segment {} cannot be read again: a file of it is missing.", bad.number)
                                 : trf("Segment {} cannot be read again: a block changed on the disk.", bad.number))
                        .c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", bad.why.c_str());  // the store's reason
                if (bad.names_lost)
                    ImGui::TextDisabled("%s", TR("The players, events and sites first imported with it are lost too: "
                                                 "other games show “?” in their place."));
            }
            if (!summary.segments.empty() && summary.elsewhere == 0) {
                // The board's message: the other segments work.
                if (summary.in_segments > 0)
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                                       trn("The other segments work. {} game cannot be read.",
                                           "The other segments work. {} games cannot be read.", summary.in_segments,
                                           ui::exactCount(summary.in_segments))
                                           .c_str());
            } else {
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                                   trn("{} game reads differently from how it was written.",
                                       "{} games read differently from how they were written.", r.damaged.size(),
                                       ui::exactCount(r.damaged.size()))
                                       .c_str());
                ImGui::TextDisabled("%s", trf("The other games are sound. Checked at {}.", when).c_str());
            }
            for (const auto& [source, n] : summary.sources) {
                const std::filesystem::path file(std::u8string(source.begin(), source.end()));
                const auto name = file.filename().u8string();
                ImGui::BulletText(
                    "%s", (source.empty() ? trn("{} game of a source not known", "{} games of a source not known", n,
                                                ui::exactCount(n))
                                          : trn("{} game imported from {}", "{} games imported from {}", n,
                                                ui::exactCount(n), std::string(name.begin(), name.end())))
                              .c_str());
            }
            // A segment that cannot be read is written again (#97); damaged games of the others are set aside alone.
            const bool rewrite = !snap->unreadable().empty();
            ImGui::BeginDisabled(!idle);
            const bool keep = ImGui::Button(TR("Keep what can be read"));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", rewrite ? TR("The games that cannot be read set aside (deleted), their numbers "
                                                     "kept, and their segment written again; undone until RookStudio "
                                                     "closes.")
                                                : TR("The damaged games deleted, the others kept; Ctrl+Z brings them "
                                                     "back."));
            if (keep && rewrite) {
                startCareKeep(index, r.damaged);  // in the background: it writes the segment again
            } else if (keep) {
                // The damaged games set aside, deleted (Ctrl+Z brings them back); a compaction writes their segments
                // again.
                Care::Undo undo;
                for (const auto id : r.damaged) undo.states.push_back(snap->state(id));
                undo.what = trn("{} damaged game set aside", "{} damaged games set aside", r.damaged.size(),
                                ui::exactCount(r.damaged.size()));
                try {
                    db->setDeleted(r.damaged, true);
                    c.undo.push_back(std::move(undo));
                    c.said =
                        TR("The damaged games are set aside (deleted): compact the database to write its segments "
                           "again. Ctrl+Z undoes.");
                    c.check.reset();
                    s.rebuildTree(index);
                } catch (const std::exception& e) {
                    c.error = trf("Not done: {}.", e.what());
                }
                ImGui::EndDisabled();
                ImGui::PopTextWrapPos();
                ImGui::EndChild();
                return;
            }
            for (const auto& file : summary.files) {
                const auto name  = file.filename().u8string();
                const auto label = trf("Import their file again ({})", std::string(name.begin(), name.end()));
                ui::sameLineOrWrap(label.c_str());
                if (ImGui::Button(label.c_str())) {
                    std::vector<uint64_t> keys;
                    for (const auto id : r.damaged) keys.push_back(snap->key(id));
                    std::sort(keys.begin(), keys.end());
                    db_selected_ = static_cast<int>(index);
                    care_.reset();
                    importIntoDatabase(static_cast<int>(index), file, false, std::move(keys));
                    ImGui::EndDisabled();
                    ImGui::PopTextWrapPos();
                    ImGui::EndChild();
                    return;
                }
            }
            ImGui::EndDisabled();
        }
    }
    // Keep what can be read of a segment that could not be read (#97): undone until RookStudio closes, as long as
    // nothing else is written to the database.
    if (db->keepReadableUndoable()) {
        ImGui::TextUnformatted(
            TR("What could be read is kept. The damaged files stay until RookStudio closes: until "
               "then, “Undo” puts them back."));
        ImGui::BeginDisabled(!idle);
        if (ImGui::Button(TR("Undo"))) {
            if (db->undoKeepReadable()) {
                const auto games = db->snapshot()->unreadableGames();
                c.said = trf("Undone: {}.", trn("{} damaged game set aside", "{} damaged games set aside", games,
                                                ui::exactCount(games)));
                std::erase_if(c.undo, [](const Care::Undo& u) { return u.kept; });
                c.check.reset();  // said again as when the database was opened
                c.error.clear();
                s.rebuildTree(index);
            }
        }
        ImGui::EndDisabled();
    }
    ImGui::BeginDisabled(!idle);
    const bool keys  = !ImGui::GetIO().WantTextInput && idle;
    const bool asked = c.start_check && idle && !db->readOnly();
    c.start_check    = c.start_check && !asked;
    if (ImGui::Button(TR("Check · Enter")) || (keys && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) || asked) {
        auto job        = std::make_unique<CareJob>();
        job->kind       = CareJob::Kind::Check;
        job->folder     = c.folder;
        job->generation = snap->generation();
        job->started    = std::chrono::steady_clock::now();
        CareJob* j      = job.get();
        j->done         = std::async(std::launch::async, [db, j] {
            study::enterBackground();
            j->check = db->check(j->check_progress, j->cancel);
        });
        care_job_       = std::move(job);
    }
    ImGui::EndDisabled();
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::SameLine(0, px(12));

    // Compact.
    ImGui::BeginChild("##care_compact", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::PushTextWrapPos();
    ui::inlineIcon(ui::Icon::Archive, ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("Compact"));
    // What it gives back, made in the background once for each snapshot (it reads every game's place).
    if (c.plan_job && c.plan_job->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        c.plan = c.plan_job->get();
        c.plan_job.reset();
    }
    if (!c.plan_job && c.plan_of != snap->generation()) {
        c.plan_of  = snap->generation();
        c.plan_job = std::make_shared<std::future<study::CompactPlan>>(std::async(std::launch::async, [db] {
            study::enterBackground();
            return db->compactPlan();
        }));
    }
    const auto plan = c.plan.value_or(study::CompactPlan{});
    if (!snap->unreadable().empty()) {
        // Its segments are copied: one that cannot be read is dealt with first (#97).
        ImGui::TextDisabled("%s", TR("Compacting waits until the segment that cannot be read is dealt with: “Keep "
                                     "what can be read” first."));
    } else if (db->compactionUndoable()) {
        ImGui::TextUnformatted(
            TR("Compacted: the old files are kept until RookStudio closes, the room given back "
               "then."));
        ImGui::BeginDisabled(!idle);
        if (ImGui::Button(TR("Undo the compaction"))) {
            if (db->undoCompaction()) {
                c.said = TR("The compaction is undone: the database is as before it.");
                c.compacted.reset();
                s.rebuildTree(index);
            }
        }
        ImGui::EndDisabled();
    } else if (!c.plan || c.plan_job) {
        ImGui::TextDisabled("%s", TR("Counting what compacting would give back…"));
    } else if (plan.segments.empty()) {
        ImGui::TextDisabled("%s", TR("Nothing to compact: no game deleted or replaced takes room in the files."));
    } else {
        const auto games = plan.deleted + plan.replaced;
        ImGui::TextDisabled("%s",
                            trn("{} game deleted or replaced (merges included) still takes {}. Compacting takes it "
                                "out of the files; the other games' numbers do not change, nor does the opening "
                                "tree.",
                                "{} games deleted or replaced (merges included) still take {}. Compacting takes "
                                "them out of the files; the other games' numbers do not change, nor does the "
                                "opening tree.",
                                games, ui::exactCount(games), formatBytes(plan.bytes))
                                .c_str());
        const auto start = [&](bool one_at_a_time) {
            auto job           = std::make_unique<CareJob>();
            job->kind          = CareJob::Kind::Compact;
            job->folder        = c.folder;
            job->one_at_a_time = one_at_a_time;
            job->started       = std::chrono::steady_clock::now();
            CareJob* j         = job.get();
            j->done            = std::async(std::launch::async, [db, j] {
                study::enterBackground();
                j->compacted = db->compact(j->compact_progress, j->cancel, j->one_at_a_time);
            });
            care_job_          = std::move(job);
            c.disk_full        = false;
            c.error.clear();
        };
        ImGui::BeginDisabled(!idle);
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(tinted(ui::ACCENT, 200)));
        if (ImGui::Button(trf("Compact · gives back {}", formatBytes(plan.bytes)).c_str())) start(false);
        ImGui::PopStyleColor();
        ImGui::EndDisabled();
        if (c.disk_full) {
            // The disk too full for the copy (M13, board 1, errors).
            std::error_code ec;
            const auto space = std::filesystem::space(c.folder, ec);
            const auto more  = !ec && plan.needed > space.available ? plan.needed - space.available : plan.needed;
            ImGui::TextColored(
                ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                trf("Disk full: compacting needs {} more for the time of the copy.", formatBytes(more)).c_str());
            ImGui::TextDisabled("%s", TR("Nothing changed in the database."));
            if (ImGui::SmallButton(TR("Free some room"))) ui::openPath(c.folder.parent_path());
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Compact segment by segment"))) start(true);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TR("Slower, and not undone: each segment written again and its old files "
                                           "removed at once, the room of one segment enough."));
        }
        ImGui::TextDisabled("%s",
                            TR("The old files are kept until RookStudio closes: “Undo the compaction” until then; "
                               "the room is given back at closing."));
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::Dummy(ImVec2(0, px(6)));
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s",
                        TR("The Home page speaks of the maintenance only when it finds something: a damaged game, "
                           "duplicates after an import. Ctrl+K: “Check the database”, “Compact the database”."));
    ImGui::PopTextWrapPos();
}

// ---------------------------------------------------------------------------------------------------------------------
// The Home page.

void App::drawFideHomeNotice() {
    const auto* kept = fideKept();
    if (!kept || fide_job_) return;
    const auto due = fide::monthDue(*kept, std::time(nullptr));
    if (due.empty()) return;
    ImGui::Spacing();
    ImGui::TextUnformatted(trf("FIDE's list of {} is out", fide::monthText(due)).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Take it"))) {
        startFideList();
        openCare(db_selected_, CarePart::Names);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Not now"))) {
        fide_kept_->offered = due;
        fide::writeKept(storage_.root(), *fide_kept_);
    }
}

void App::drawCareHomeNotice() {
    // Duplicates an import found, not merged yet.
    if (db_outcome_ && !db_outcome_->duplicates.empty()) {
        const auto index = indexOf(shelf(), db_outcome_->folder);
        if (index < shelf().entries().size()) {
            ImGui::Spacing();
            ImGui::TextUnformatted(trn("“{}”: {} game already there from another source",
                                       "“{}”: {} games already there from another source",
                                       db_outcome_->duplicates.size(), shelf().entries()[index].name,
                                       ui::exactCount(db_outcome_->duplicates.size()))
                                       .c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("See them"))) {
                openCare(static_cast<int>(index), CarePart::Duplicates);
                if (care_) care_->since = db_outcome_->games_before;
            }
        }
    }
    // Damaged games a check found.
    if (care_ && care_->check && !care_->check->damaged.empty()) {
        const auto index = indexOf(shelf(), care_->folder);
        if (index < shelf().entries().size()) {
            ImGui::Spacing();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               trn("“{}”: {} damaged game", "“{}”: {} damaged games", care_->check->damaged.size(),
                                   shelf().entries()[index].name, ui::exactCount(care_->check->damaged.size()))
                                   .c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("See them##damaged"))) openCare(static_cast<int>(index), CarePart::Checks);
        }
    }
    // The games of a segment that could not be read when a database was opened (#97), told as those of a check.
    auto& s = shelf();
    for (std::size_t i = 0; i < s.entries().size(); ++i) {
        if (s.entries()[i].source || (care_ && care_->check && care_->folder == s.entries()[i].folder)) continue;
        const auto facts = s.facts(i);
        if (facts.unreadable == 0) continue;
        ImGui::PushID(static_cast<int>(i));
        ImGui::Spacing();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           trn("“{}”: {} damaged game", "“{}”: {} damaged games", facts.unreadable, s.entries()[i].name,
                               ui::exactCount(facts.unreadable))
                               .c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("See them##damaged"))) openCare(static_cast<int>(i), CarePart::Checks);
        ImGui::PopID();
    }
}

}  // namespace fcg
