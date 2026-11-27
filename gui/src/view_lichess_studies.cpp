// The user's Lichess studies (#79, mockup M11, board 4). On the Lichess page, the Studies card: the account's studies
// (the public ones without a permission, the private and unlisted ones once RookStudio may read them, asked at the
// card's first opening), a study's chapters, imported into a database (each chapter a game; read again, only the
// changed chapters replaced), a chapter opened on the Board, a study followed: asked whether it changed when the page
// opens (a HEAD request each, nothing downloaded until "Read again"). On the Board, lichess.org… › To a Lichess
// study…: the game sent into one of the user's studies (a full one is not offered) or a new one, its chapter's name,
// side and the assistant's comments chosen there, what goes said; the permission to write asked at the first send;
// then its link, and on the game's strip the changes since, sent again on request only.

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <string>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include <study/store.hpp>
#include <tour/files.hpp>
#include <tour/util.hpp>

#include "app.hpp"
#include "console.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "lichess_studies.hpp"
#include "ui.hpp"
#include "widgets.hpp"

namespace fcg {

namespace {

namespace fs = std::filesystem;

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

const char* visibilityName(std::string_view key) {
    if (key == "private") return TR("private");
    if (key == "unlisted") return TR("unlisted");
    if (key == "public") return TR("public");
    return "";
}

// Who sees a study of that visibility.
const char* visibilityMeaning(std::string_view key) {
    if (key == "private") return TR("Private study: you alone see it, and the members you invite.");
    if (key == "unlisted") return TR("Unlisted study: whoever has its link sees it.");
    if (key == "public") return TR("Public study: anyone sees it, and Lichess lists it.");
    return "";
}

// The database a study goes into unless another is chosen: a study's link's (#47).
std::string defaultBase() { return TR("Lichess studies"); }

// `text` cut with "…" to fit `width`.
std::string fitted(std::string text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
    while (!text.empty() && ImGui::CalcTextSize((text + "…").c_str()).x > width) {
        text.pop_back();
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) text.pop_back();
        if (!text.empty() && static_cast<unsigned char>(text.back()) >= 0xC0) text.pop_back();
    }
    return text + "…";
}

// A day as the card says it, from seconds since the epoch.
std::string dayOf(int64_t seconds) { return seconds > 0 ? ui::dayText(static_cast<std::time_t>(seconds)) : ""; }

using ui::endTinted;

// A card tinted with `tone`, as the broadcast's banner is: its border drawn from the background, softer than the
// training's.
void beginTinted(const char* id, ImU32 tone) {
    ui::beginTinted(id, tone, {.edge = 0.45f, .soft_edge = true, .padding = ImVec2(12, 8)});
}

// Whether a study read before changed on Lichess since: the last HEAD's Last-Modified against the reading's, or the
// list's date of its last change (both in Lichess's clock).
bool changedOnLichess(const lichess::StudyRecord& r, const lichess::StudyInfo* info) {
    if (r.read_at == 0) return false;
    if (lichess::changedSince(r.checked, r.modified, r.read_at)) return true;
    if (info && info->updated > 0) {
        if (const auto read = lichess::httpTime(r.modified)) return info->updated / 1000 > *read + 60;
    }
    return false;
}

// What the game has: its moves, comments, variations, arrows and squares, for the card's line.
std::string gameSummary(const GameTree& tree) {
    int comments = 0, marks = 0;
    const auto count = [&](const std::vector<ReplayMove>& moves) {
        for (const auto& m : moves) {
            comments += (m.notes.after.empty() ? 0 : 1) + (m.notes.before.empty() ? 0 : 1);
            marks += static_cast<int>(m.notes.arrows.size() + m.notes.squares.size());
        }
    };
    count(tree.main.moves);
    for (const auto& v : tree.variations) count(v.moves);
    const auto moves = static_cast<int>((tree.main.moves.size() + 1) / 2);
    auto out         = trn("{} move long", "{} moves long", moves, moves);
    if (comments > 0) out += " · " + trn("{} comment", "{} comments", comments, comments);
    if (!tree.variations.empty())
        out += " · " + trn("{} variation", "{} variations", tree.variations.size(), tree.variations.size());
    if (marks > 0) out += " · " + trn("{} arrow or square", "{} arrows and squares", marks, marks);
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// State, jobs

void App::loadStudiesState() {
    if (studies_loaded_) return;
    studies_loaded_ = true;
    studies_        = lichess::loadStudies(storage_.root());
}

void App::saveStudiesState() {
    if (options_.scripted) return;  // a run for screenshots leaves the profile as it was
    try {
        lichess::saveStudies(storage_.root(), studies_);
    } catch (const std::exception& e) {
        Console::instance().add(trf("The Lichess studies could not be kept: {}", e.what()), true);
    }
}

std::string App::studyToken(bool write) const {
    if (!lichess_account_) return {};
    return lichess_account_->has(write ? lichess::SCOPE_STUDY_WRITE : lichess::SCOPE_STUDY_READ)
               ? lichess_account_->token
               : std::string();
}

void App::askStudyPermission(bool write) {
    if (lichess_link_) return;
    auto scopes    = lichess_account_ ? lichess_account_->scopes : std::vector<std::string>{};
    const auto add = [&](const char* scope) {
        if (std::find(scopes.begin(), scopes.end(), scope) == scopes.end()) scopes.emplace_back(scope);
    };
    add(write ? lichess::SCOPE_STUDY_WRITE : lichess::SCOPE_STUDY_READ);
    // Writing, reading too unless the user said no to it: their private studies to choose from, and the empty first
    // chapter of a new study found and deleted.
    if (write && !studies_.read_declined) add(lichess::SCOPE_STUDY_READ);
    startLichessLink(scopes, lichess_account_ ? lichess_account_->username : std::string());
}

void App::readStudy(const std::string& id, std::optional<std::size_t> base, bool open_after) {
    if (study_job_) return;
    StudyJob job;
    job.id         = id;
    job.open_after = open_after;
    std::shared_ptr<study::Database> db;
    std::map<std::string, lichess::ImportedChapter> before;
    if (base) {
        auto& s = shelf();
        if (*base >= s.entries().size()) return;
        db = s.open(*base);
        if (!db || db->readOnly()) {
            study_said_of_  = id;
            study_said_bad_ = true;
            study_said_ =
                trf("“{}” is read-only while another program writes to it: import into it once it has finished.",
                    s.entries()[*base].name);
            return;
        }
        job.base      = *base;
        job.base_name = s.entries()[*base].name;
        job.folder    = s.entries()[*base].folder;
        if (const auto* r = studies_.find(id); r && r->database == s.entries()[*base].folder) before = r->games;
    }
    study_said_.clear();
    study_said_of_ = id;
    job.done       = std::async(std::launch::async, [id, token = studyToken(false), db, before,
                                                     cache = lichess::studyCache(storage_.root(), id)] {
        StudyRead out;
        out.text = lichess::studyText(id, token);
        if (!out.text.value) return out;
        out.chapters = lichess::parseStudyPgn(out.text.value->pgn);
        try {  // kept for the next time, shown offline too
            std::error_code ec;
            fs::create_directories(cache.parent_path(), ec);
            tour::replaceFile(cache, out.text.value->pgn);
        } catch (const std::exception&) {
        }
        if (db) out.import = lichess::importStudy(*db, out.chapters, before);
        return out;
    });
    study_job_     = std::move(job);
}

void App::checkFollowedStudies() {
    if (study_checks_job_.valid()) return;
    std::vector<std::string> ids;
    for (const auto& r : studies_.studies)
        if (r.followed && r.read_at > 0) ids.push_back(r.id);
    if (ids.empty()) return;
    study_checked_    = std::chrono::steady_clock::now();
    study_checks_job_ = std::async(std::launch::async, [ids, token = studyToken(false)] {
        std::vector<std::pair<std::string, lichess::Answer<std::string>>> out;
        for (const auto& id : ids) out.emplace_back(id, lichess::studyModified(id, token));
        return out;
    });
}

void App::pollStudies() {
    loadStudiesState();
    // The account's list.
    if (isReady(studies_list_job_)) {
        auto answer = studies_list_job_.get();
        if (answer.value) {
            studies_list_ = std::move(*answer.value);
            studies_list_error_ = {};
            bool renamed = false;  // the names Lichess has now
            for (const auto& s : *studies_list_) {
                if (auto* r = studies_.find(s.id); r && r->name != s.name) {
                    r->name = s.name;
                    renamed = true;
                }
            }
            if (renamed) saveStudiesState();
        } else {
            studies_list_error_ = answer;
        }
    }
    // A study read, imported too.
    if (study_job_ && isReady(study_job_->done)) {
        auto job  = std::move(*study_job_);
        auto read = job.done.get();
        study_job_.reset();
        study_said_of_  = job.id;
        study_said_bad_ = false;
        if (!read.text.value) {
            study_said_     = trf("Lichess did not give the study: {}.", lichess::describe(read.text));
            study_said_bad_ = true;
        } else {
            std::string name = job.id;
            if (studies_list_)
                for (const auto& s : *studies_list_)
                    if (s.id == job.id) name = s.name;
            if (const auto* known = studies_.find(job.id); known && name == job.id) name = known->name;
            for (const auto& c : read.chapters) {  // a study read from a link has its name in its chapters
                if (name != job.id) break;
                const auto at = c.pgn.find("[StudyName \"");
                if (at != std::string::npos) name = c.pgn.substr(at + 12, c.pgn.find('"', at + 12) - at - 12);
            }
            auto& r          = studies_.record(job.id, name);
            const bool first = r.read_at == 0;
            std::vector<lichess::ChapterState> now;
            for (const auto& c : read.chapters) now.push_back({c.id, c.name, c.hash, false});
            lichess::markChanged(now, r.read);
            if (first)
                for (auto& c : now) c.changed = false;  // nothing to compare with
            const auto changed = std::count_if(now.begin(), now.end(), [](const auto& c) { return c.changed; });
            r.read             = std::move(now);
            r.chapters         = static_cast<int>(read.chapters.size());
            r.read_at          = nowSeconds();
            r.modified         = read.text.value->modified;
            r.checked          = r.modified;
            if (const auto at = lichess::httpTime(r.modified); at && studies_list_) {  // the list knows no better now
                for (auto& x : *studies_list_)
                    if (x.id == job.id) x.updated = *at * 1000;
            }
            study_chapters_[job.id] = std::move(read.chapters);
            if (read.import) {
                const auto& i = *read.import;
                if (!i.error.empty()) {
                    study_said_     = trf("“{}” could not take the chapters: {}.", job.base_name, i.error);
                    study_said_bad_ = true;
                } else {
                    r.database  = job.folder;
                    r.games     = i.games;
                    study_said_ = trf("In “{}”: {} added, {} replaced, {} unchanged.", job.base_name,
                                      ui::exactCount(i.added), ui::exactCount(i.replaced), ui::exactCount(i.unchanged));
                    if (i.left_deleted > 0)
                        study_said_ += " " + trn("{} chapter deleted there stays deleted.",
                                                 "{} chapters deleted there stay deleted.", i.left_deleted,
                                                 ui::exactCount(i.left_deleted));
                    // Its opening tree takes the games in, the database found again by its folder.
                    if (i.added + i.replaced > 0)
                        for (std::size_t e = 0; e < shelf().entries().size(); ++e)
                            if (shelf().entries()[e].folder == job.folder) shelf().rebuildTree(e);
                }
            } else {
                study_said_ = first         ? trn("{} chapter read.", "{} chapters read.", r.chapters, r.chapters)
                              : changed > 0 ? trn("{} chapter changed since the last reading.",
                                                  "{} chapters changed since the last reading.", changed, changed)
                                            : std::string(TR("Nothing changed since the last reading."));
            }
            saveStudiesState();
            if (job.open_after) openStudyChapter(job.id, static_cast<std::size_t>(std::max(0, study_chapter_)));
            if (job.theory_after) makeTheoryStudy(job.id);  // read to be made a theory study (#77)
        }
    }
    // The followed studies: has each changed?
    if (isReady(study_checks_job_)) {
        bool any = false;
        for (auto& [id, answer] : study_checks_job_.get()) {
            if (auto* r = studies_.find(id); r && answer.value && r->checked != *answer.value) {
                r->checked = *answer.value;
                any        = true;
            }
        }
        if (any) saveStudiesState();
    }
    // The game sent.
    if (isReady(study_send_job_)) {
        const auto answer = study_send_job_.get();
        if (study_send_) {
            auto& s = *study_send_;
            s.busy  = false;
            if (answer.value && answer.value->created) {  // made, whatever came next: Lichess counts it
                studies_.made.push_back(nowSeconds());
                auto& r      = studies_.record(answer.value->study, s.going.study_name);
                r.visibility = s.going.visibility;
                r.chapters   = std::max(r.chapters, 1);
                if (studies_list_ && std::none_of(studies_list_->begin(), studies_list_->end(),
                                                  [&](const auto& x) { return x.id == answer.value->study; }))
                    studies_list_->insert(studies_list_->begin(),
                                          lichess::StudyInfo{answer.value->study, s.going.study_name,
                                                             nowSeconds() * 1000, nowSeconds() * 1000});
            }
            if (answer.failed() || !answer.value || answer.value->chapter.empty()) {
                // Lichess's failure kept as it came (#146), said with its repair; the program's own words otherwise.
                s.failure = answer;
                if (!answer.failed()) s.error = trf("Not sent: {}.", TR("Lichess made no chapter of it"));
                if (answer.value && !answer.value->created && answer.value->chapters >= 0)  // a full study, counted
                    studies_.record(answer.value->study, s.going.study_name).chapters = answer.value->chapters;
            } else {
                const auto& v = *answer.value;
                auto sent     = s.going;
                sent.study    = v.study;
                sent.chapter  = v.chapter;
                if (!v.chapter_name.empty()) sent.chapter_name = v.chapter_name;
                if (board_origin_ && !board_origin_->linked) {
                    sent.folder = board_origin_->folder;
                    sent.game   = board_origin_->id;
                }
                auto& r = studies_.record(v.study, sent.study_name);
                if (v.chapters >= 0) r.chapters = v.chapters;
                studies_.last_study = v.study;
                std::erase_if(studies_.sent, [&](const auto& x) { return x.chapter == sent.chapter; });
                studies_.sent.push_back(sent);
                if (studies_.sent.size() > 500) studies_.sent.erase(studies_.sent.begin());
                s.sent     = sent;
                s.chapters = v.chapters;
                if (board_game_serial_ == s.serial) {  // the game still on the board: its changes counted from here
                    board_.sent             = sent;
                    board_.since_sent       = {};
                    board_.since_sent.count = board_.changes.count - s.count;
                }
                Console::instance().add(
                    trf("The game is on Lichess, in the study “{}”: {}", sent.study_name, sent.url()));
            }
            saveStudiesState();
        }
    }
    // The changes sent.
    if (isReady(study_update_job_)) {
        const auto answer = study_update_job_.get();
        if (answer.value) {
            study_update_error_ = {};
            for (auto& r : studies_.sent) {
                if (r.chapter != study_update_.chapter) continue;
                r.pgn  = study_update_.pgn;
                r.tags = study_update_.tags;
            }
            if (std::none_of(studies_.sent.begin(), studies_.sent.end(),
                             [&](const auto& x) { return x.chapter == study_update_.chapter; }))
                studies_.sent.push_back(study_update_);
            if (board_game_serial_ == study_update_serial_ && board_.sent &&
                board_.sent->chapter == study_update_.chapter) {
                board_.sent->pgn        = study_update_.pgn;
                board_.sent->tags       = study_update_.tags;
                board_.since_sent       = {};
                board_.since_sent.count = board_.changes.count - study_update_count_;
            }
            saveStudiesState();
            boardToast(TR("The chapter on Lichess has the game's changes now"));
        } else {
            study_update_error_ = answer;
        }
    }
    // Lichess's page answered: what waited for the permission goes on, or says why not.
    if (!lichess_link_) {
        if (studies_after_link_) {
            studies_after_link_ = false;
            studies_list_of_.clear();  // asked again, with the token
        }
        if (study_send_ && study_send_->after_link) {
            study_send_->after_link = false;
            if (!studyToken(true).empty())
                startStudySend();
            else {
                study_send_->failure = lichess_error_;  // the link's failure, as it came (#146)
                if (!lichess_error_.failed())
                    study_send_->error = TR("Lichess gave no permission to write in your studies: nothing was sent.");
                study_send_->refused = lichess::linkRefused(lichess_error_);
            }
        }
        if (study_update_after_link_) {
            study_update_after_link_ = false;
            if (!studyToken(true).empty()) sendStudyChanges();
        }
    }
    // A game of a database on the board, sent before (or a chapter opened from there): the strip says so again; a game
    // sent and since saved in a database, its record knows where.
    if (board_origin_ && !board_origin_->linked) {
        const auto key = fmt::format("{}#{}", board_origin_->folder.string(), board_origin_->id);
        if (!board_.sent && key != study_origin_seen_) {
            study_origin_seen_ = key;
            for (const auto& r : studies_.sent) {
                if (r.folder != board_origin_->folder || r.game != board_origin_->id) continue;
                board_.sent       = r;
                board_.since_sent = {};
                // How many changes is not known any more: changed or not.
                board_.since_sent.lost = lichess::studyGamePgn(board_.tree, board_.chess960, r.assistant) != r.pgn;
                break;
            }
        }
        if (board_.sent) {
            study_origin_seen_ = key;
            for (auto& r : studies_.sent) {
                if (r.chapter != board_.sent->chapter ||
                    (r.folder == board_origin_->folder && r.game == board_origin_->id))
                    continue;
                r.folder = board_.sent->folder = board_origin_->folder;
                r.game = board_.sent->game = board_origin_->id;
                saveStudiesState();
            }
        }
    } else {
        study_origin_seen_.clear();
    }
}

// ---------------------------------------------------------------------------
// The Lichess page's card

void App::openStudyChapter(const std::string& id, std::size_t index) {
    // The chapters as last read, from the profile when this session has not read them.
    if (!study_chapters_.count(id)) {
        std::ifstream in(lichess::studyCache(storage_.root(), id), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        study_chapters_[id] = lichess::parseStudyPgn(text);
    }
    const auto& chapters = study_chapters_[id];
    if (index >= chapters.size()) return;
    const auto c           = chapters[index];
    const auto* r          = studies_.find(id);
    const std::string name = r ? r->name : id;
    const auto serial      = board_game_serial_;
    readOnBoard(c.pgn, fmt::format("{} · {}", c.name, name));
    if (board_game_serial_ == serial) return;  // it could not be read: the board says why
    toStudyBoard();
    board_.title = fmt::format("{} · {}", c.name, name);
    board_.flip  = c.black;
    // A chapter of the user's own study: its changes can go back to it ("Send the changes").
    if (!c.id.empty()) {
        lichess::SentRecord s;
        s.study           = id;
        s.chapter         = c.id;
        s.study_name      = name;
        s.chapter_name    = c.name;
        s.visibility      = r ? r->visibility : std::string();
        s.black           = c.black;
        s.chess960        = board_.chess960;
        s.opened          = true;
        s.pgn             = lichess::studyGamePgn(board_.tree, board_.chess960, false);
        s.tags            = lichess::gameTags(board_.tree);
        board_.sent       = s;
        board_.since_sent = {};
    }
}

void App::drawLichessStudies() {
    loadStudiesState();
    const ui::Card card("lichess_studies");
    ImGui::PushTextWrapPos();
    // The card folds; its first opening asks for the permission to read the private studies.
    bool open = studies_.shown;
    if (ImGui::ArrowButton("##studies_fold", open ? ImGuiDir_Down : ImGuiDir_Right)) open = !open;
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Your studies"));
    if (ImGui::IsItemClicked()) open = !open;
    if (open != studies_.shown) {
        studies_.shown  = open;
        studies_.opened = studies_.opened || open;
        saveStudiesState();
    }
    const std::string user = lichess_account_ ? lichess_account_->username : std::string();
    const bool may_read    = !studyToken(false).empty();
    // The list, once the card is open: again when the account or its permission changed, or on Try again.
    const auto key = user + (may_read ? "+read" : "");
    if (open && !user.empty() && !studies_list_job_.valid() && studies_list_of_ != key && !lichess_link_) {
        studies_list_of_  = key;
        studies_list_job_ = std::async(std::launch::async,
                                       [user, token = studyToken(false)] { return lichess::userStudies(user, token); });
    }
    if (studies_list_) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ui::exactCount(studies_list_->size()).c_str());
        if (open) {  // asked again on request: a study made on lichess.org meanwhile
            ImGui::SameLine();
            ImGui::BeginDisabled(studies_list_job_.valid() || lichess_link_ != nullptr);
            if (ImGui::SmallButton(TR("Refresh##studies"))) studies_list_of_.clear();
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", TR("The list of your studies asked of Lichess again."));
        }
    }
    // Folded: the followed studies that changed, a line each.
    const auto infoOf = [&](const std::string& id) -> const lichess::StudyInfo* {
        if (!studies_list_) return nullptr;
        for (const auto& s : *studies_list_)
            if (s.id == id) return &s;
        return nullptr;
    };
    if (!open) {
        ImGui::TextDisabled("%s", TR("Read, imported into a database, followed, filled from the Board."));
        for (const auto& r : studies_.studies) {
            if (!r.followed || !changedOnLichess(r, infoOf(r.id))) continue;
            ImGui::PushID(r.id.c_str());
            ImGui::TextUnformatted(
                trf("“{}” changed on Lichess since the last reading ({}).", r.name, dayOf(r.read_at)).c_str());
            ImGui::SameLine();
            ImGui::BeginDisabled(study_job_.has_value());
            if (ImGui::SmallButton(TR("Read again"))) {
                study_shown_   = r.id;
                studies_.shown = true;
                std::optional<std::size_t> base;
                for (std::size_t i = 0; i < shelf().entries().size(); ++i)
                    if (!r.database.empty() && shelf().entries()[i].folder == r.database) base = i;
                readStudy(r.id, base);
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::PopTextWrapPos();
        return;
    }
    if (!lichess_account_) {
        ImGui::TextDisabled("%s", TR("Your studies come with your Lichess account linked. Another's study comes by "
                                     "its link: Ctrl+V on the Board or on the Databases page."));
        if (!lichess_link_ && ui::iconButton("studies_link", ui::Icon::External, TR("Link the account…")))
            startLichessLink({lichess::SCOPE_STUDY_READ});
        ImGui::PopTextWrapPos();
        return;
    }
    // The permission to read the private and unlisted studies: asked here, once.
    if (may_read) {
        ImGui::TextDisabled("%s", TR("RookStudio may read your private and unlisted studies (study:read); your public "
                                     "ones are read without it."));
    } else if (!studies_.read_declined) {
        beginTinted("studies_ask", ui::ACCENT);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(TR("Lichess asks for one more permission"));
        ImGui::TextDisabled("%s",
                            TR("To list your private and unlisted studies too, RookStudio must be allowed to read "
                               "your studies; your public ones are read without it. It writes nothing. "
                               "Lichess's page opens for it; your account stays linked meanwhile, and the token "
                               "it replaces is revoked."));
        if (lichess_link_) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s",
                               TR("Waiting for Lichess: allow RookStudio in the browser's page, then come back here."));
        } else {
            if (ui::iconButton("studies_allow", ui::Icon::External, TR("Allow on Lichess…"))) {
                studies_after_link_ = true;
                askStudyPermission(false);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Without it"))) {
                studies_.read_declined = true;
                saveStudiesState();
            }
        }
        ImGui::PopTextWrapPos();
        endTinted();
    } else {
        ImGui::TextDisabled("%s", TR("Your public studies only: RookStudio may not read the others."));
        ImGui::SameLine();
        if (!lichess_link_ && ImGui::SmallButton(TR("Allow on Lichess…"))) {
            studies_after_link_ = true;
            askStudyPermission(false);
        }
    }

    // The studies: Lichess's list, or what the profile knows of them offline.
    std::vector<lichess::StudyInfo> shown;
    if (studies_list_) {
        shown = *studies_list_;
    } else {
        for (const auto& r : studies_.studies) shown.push_back({r.id, r.name, 0, 0});
    }
    if (!shown.empty() && std::none_of(shown.begin(), shown.end(), [&](const auto& s) { return s.id == study_shown_; }))
        study_shown_ = shown.front().id;
    if (!studies_list_ && studies_list_job_.valid()) ImGui::TextDisabled("%s", TR("Asking Lichess…"));
    if (studies_list_error_.failed()) {  // a token refused, the pause, an outage: each with its repair (#141)
        drawRepairNotice(lichessNotice(studies_list_error_,
                                       trf("Lichess did not give the list: {}. What was read before stays.",
                                           lichess::describe(studies_list_error_)),
                                       lichessAccess(), retryState(Retry::StudiesList, lichess::apiHost())),
                         "studies_list");
    }
    if (shown.empty()) {
        if (studies_list_)
            ImGui::TextDisabled("%s", may_read ? TR("No study on this account yet: a game sent from the Board makes "
                                                    "one.")
                                               : TR("No public study on this account."));
    } else {
        const float avail  = ImGui::GetContentRegionAvail().x;
        const bool side    = avail > px(600);
        const float list_w = side ? std::clamp(avail * 0.38f, px(220), px(320)) : avail;
        const float line   = ImGui::GetTextLineHeight();
        const float row_h  = line * 2 + px(8);
        const auto& style  = ImGui::GetStyle();
        const float list_h = std::min(
            px(330), (row_h + style.ItemSpacing.y) * static_cast<float>(shown.size()) + style.WindowPadding.y * 2);
        ImGui::BeginChild("##studies_list", ImVec2(list_w, list_h), ImGuiChildFlags_Borders);
        for (const auto& s : shown) {
            const auto* r   = studies_.find(s.id);
            const bool on   = s.id == study_shown_;
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float w   = ImGui::GetContentRegionAvail().x;
            if (ImGui::Selectable(("##study" + s.id).c_str(), on, ImGuiSelectableFlags_None, ImVec2(0, row_h))) {
                study_shown_   = s.id;
                study_chapter_ = 0;
                study_said_.clear();
                // A study never read: its chapters asked at once (the click is the asking).
                if ((!r || r->read_at == 0) && !fs::exists(lichess::studyCache(storage_.root(), s.id)))
                    readStudy(s.id, {});
            }
            auto* dl          = ImGui::GetWindowDrawList();
            const auto vis    = r ? std::string(visibilityName(r->visibility)) : std::string();
            const float vis_w = vis.empty() ? 0 : ImGui::CalcTextSize(vis.c_str()).x + px(8);
            const auto name   = fitted(s.name, w - vis_w - px(12));
            dl->AddText(ImVec2(at.x + px(6), at.y + px(4)), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
            if (!vis.empty())
                dl->AddText(ImVec2(at.x + w - vis_w, at.y + px(4)), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                            vis.c_str());
            std::string sub;
            const auto add = [&](const std::string& part) { sub += (sub.empty() ? "" : " · ") + part; };
            if (r && r->chapters >= 0)
                add(lichess::studyFull(r->chapters) ? trf("{} chapters, full", r->chapters)
                                                    : trn("{} chapter", "{} chapters", r->chapters, r->chapters));
            if (r && r->followed) add(TR("followed"));
            if (r && changedOnLichess(*r, &s))
                add(TR("changed since the last reading"));
            else if (s.updated > 0)
                add(trf("changed on Lichess {}", dayOf(s.updated / 1000)));
            if (!r || r->read_at == 0) add(TR("not read yet"));
            dl->AddText(ImVec2(at.x + px(6), at.y + px(4) + line), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                        fitted(sub, w - px(12)).c_str());
        }
        ImGui::EndChild();
        if (side) ImGui::SameLine(0, px(14));
        ImGui::BeginGroup();
        for (const auto& s : shown)
            if (s.id == study_shown_) drawStudyDetail(s);
        ImGui::EndGroup();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("%s", TR("Another's study comes by its link: Ctrl+V on the Board or on the Databases page."));
    ImGui::TextDisabled("%s", TR("Lichess's limits: 64 chapters a study (a full one is not offered for a game), 30 new "
                                 "studies a day."));
    ImGui::TextDisabled("%s", TR("Sent to Lichess: your name for the list of your studies, then the study you choose, "
                                 "asked as PGN; for each study followed, one request when this page opens (has it "
                                 "changed?), nothing downloaded. Your token goes with them once RookStudio may read "
                                 "your studies."));
    ImGui::PopTextWrapPos();
}

void App::drawStudyDetail(const lichess::StudyInfo& study) {
    const auto& id = study.id;
    auto* r        = studies_.find(id);
    // The chapters as last read: this session's, else the profile's copy.
    if (!study_chapters_.count(id)) {  // looked for once: none when it was never read
        std::ifstream in(lichess::studyCache(storage_.root(), id), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        study_chapters_[id] = lichess::parseStudyPgn(text);
    }
    const auto found     = study_chapters_.find(id);
    const auto* chapters = found != study_chapters_.end() ? &found->second : nullptr;
    const bool reading   = study_job_ && study_job_->id == id;
    ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize() * 1.15f);
    ImGui::TextUnformatted(study.name.c_str());
    ImGui::PopFont();
    std::string chip;
    const auto add = [&](const std::string& part) { chip += (chip.empty() ? "" : " · ") + part; };
    if (r && !r->visibility.empty()) add(visibilityName(r->visibility));
    if (r && r->chapters >= 0) add(trn("{} chapter", "{} chapters", r->chapters, r->chapters));
    if (r && r->followed) add(TR("followed"));
    if (r && r->read_at > 0) add(trf("read {}", dayOf(r->read_at)));
    if (!chip.empty()) ImGui::TextDisabled("%s", chip.c_str());
    if (ImGui::IsItemHovered() && r && r->visibility.empty())
        ImGui::SetTooltip("%s", TR("Lichess's list does not say a study's visibility: it is known for the studies "
                                   "RookStudio made."));
    // The database it went into, if it still is one of the user's.
    std::optional<std::size_t> imported;
    for (std::size_t i = 0; i < shelf().entries().size(); ++i)
        if (r && !r->database.empty() && shelf().entries()[i].folder == r->database) imported = i;
    // Changed on Lichess since it was read: nothing downloaded until Read again.
    if (r && changedOnLichess(*r, &study)) {
        beginTinted("study_changed", ui::ACCENT);
        ImGui::PushTextWrapPos(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(trf("It changed on Lichess since the last reading ({}).", dayOf(r->read_at)).c_str());
        ImGui::PopTextWrapPos();
        ui::sameLineOrWrap(ImGui::CalcTextSize(TR("Read again")).x + ImGui::GetFrameHeight() +
                           ImGui::GetStyle().FramePadding.x * 3);
        ImGui::BeginDisabled(study_job_.has_value());
        if (ui::iconButton("study_read_again", ui::Icon::Download, TR("Read again"))) readStudy(id, imported);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", imported ? trf("Its PGN asked again; its changed chapters replaced in “{}”.",
                                                   shelf().entries()[*imported].name)
                                                   .c_str()
                                             : TR("Its PGN asked again, the changed chapters marked."));
        endTinted();
    }
    // Its chapters.
    if (reading) {
        ImGui::TextDisabled("%s", TR("Asking Lichess for the study…"));
    } else if (!chapters || chapters->empty()) {
        ImGui::TextDisabled("%s", TR("Its chapters show once it is read."));
        ImGui::SameLine();
        ImGui::BeginDisabled(study_job_.has_value());
        if (ImGui::SmallButton(TR("Read it"))) readStudy(id, {});
        ImGui::EndDisabled();
    } else {
        const float line = ImGui::GetTextLineHeightWithSpacing();
        const float h =
            std::min(px(240), line * static_cast<float>(chapters->size()) + ImGui::GetStyle().WindowPadding.y * 2);
        ImGui::BeginChild("##study_chapters", ImVec2(0, h), ImGuiChildFlags_Borders);
        study_chapter_ = std::clamp(study_chapter_, 0, static_cast<int>(chapters->size()) - 1);
        for (std::size_t i = 0; i < chapters->size(); ++i) {
            const auto& c      = (*chapters)[i];
            const bool changed = r && i < r->read.size() && r->read[i].changed;
            const auto label   = fmt::format("{} · {}##chapter{}", i + 1, c.name, i);
            const float right  = changed ? ImGui::CalcTextSize(TR("changed")).x + px(8) : 0.0f;
            if (ImGui::Selectable(label.c_str(), study_chapter_ == static_cast<int>(i),
                                  ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(ImGui::GetContentRegionAvail().x - right, 0))) {
                study_chapter_ = static_cast<int>(i);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openStudyChapter(id, i);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("A double click opens it on the Board."));
            if (changed) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", TR("changed"));
            }
        }
        ImGui::EndChild();
    }
    // What can be done with it.
    auto& s          = shelf();
    const auto fresh = defaultBase();
    std::optional<std::size_t> chosen;
    if (study_base_ >= 0 && study_base_ < static_cast<int>(s.entries().size()) &&
        !s.entries()[static_cast<std::size_t>(study_base_)].source)
        chosen = static_cast<std::size_t>(study_base_);
    else if (imported)
        chosen = imported;
    else
        for (std::size_t i = 0; i < s.entries().size(); ++i)
            if (!s.entries()[i].source && s.entries()[i].name == fresh) chosen = i;
    ImGui::BeginDisabled(study_job_.has_value());
    if (ui::iconButton("study_import", ui::Icon::Database, TR("Import into a database"))) {
        std::optional<std::size_t> base = chosen;
        if (!base) {
            try {
                base = s.create(fresh);
            } catch (const std::exception& e) {
                study_said_of_  = id;
                study_said_bad_ = true;
                study_said_     = e.what();
            }
        }
        if (base) readStudy(id, base);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s",
                          TR("Its PGN asked of Lichess, each chapter a game of the database; done again, only the "
                             "chapters changed since are replaced, and the new ones added."));
    ImGui::SameLine(0, px(4));
    ImGui::SetNextItemWidth(px(190));
    const auto current = chosen ? s.entries()[*chosen].name : fresh + " " + TR("(new)");
    if (ImGui::BeginCombo("##study_base", current.c_str())) {
        if (!std::any_of(s.entries().begin(), s.entries().end(),
                         [&](const auto& e) { return !e.source && e.name == fresh; }) &&
            ImGui::Selectable((fresh + " " + TR("(new)")).c_str(), !chosen))
            study_base_ = -1;
        for (std::size_t i = 0; i < s.entries().size(); ++i) {
            const auto& e = s.entries()[i];
            if (e.source) continue;  // tournaments and files read without a copy
            if (ImGui::Selectable(e.name.c_str(), chosen == i)) study_base_ = static_cast<int>(i);
        }
        ImGui::EndCombo();
    }
    const char* open_label = TR("Open a chapter on the Board");
    ui::sameLineOrWrap(ImGui::CalcTextSize(open_label).x + px(40));
    ImGui::BeginDisabled(!chapters || chapters->empty());
    if (ui::iconButton("study_open", ui::Icon::Board, open_label))
        openStudyChapter(id, static_cast<std::size_t>(study_chapter_));
    ImGui::EndDisabled();
    // A theory study (#77): its notes in the Tree tab, read again with the study.
    const auto in_theory = theory().findLichess(id);
    const char* theory   = in_theory ? TR("In the theory ›") : TR("Make it a theory study");
    ui::sameLineOrWrap(ImGui::CalcTextSize(theory).x + px(24));
    ImGui::BeginDisabled(!in_theory && study_job_.has_value());
    if (ImGui::Button(theory)) {
        if (in_theory)
            showTheoryPart(*in_theory);
        else
            makeTheoryStudy(id);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s",
                          in_theory ? TR("Its notes show in the Tree tab; the Openings page's Theory part hides "
                                         "them or takes it out.")
                          : chapters && !chapters->empty()
                              ? TR("Its comments, symbols, evaluations, arrows and squares in the Tree tab, at each "
                                   "position its chapters reach; read again with the study. Nothing more is asked "
                                   "of Lichess.")
                              : TR("Its PGN asked of Lichess first, then its comments, symbols, evaluations, "
                                   "arrows and squares in the Tree tab, at each position its chapters reach."));
    // Followed: asked whether it changed when this page opens.
    bool followed = r && r->followed;
    ImGui::BeginDisabled(!r || r->read_at == 0);
    if (ImGui::Checkbox(TR("Follow it"), &followed) && r) {
        r->followed = followed;
        saveStudiesState();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", r && r->read_at > 0
                                    ? TR("Asked whether it changed each time this page opens: one request, nothing "
                                         "downloaded. Read again downloads it.")
                                    : TR("Once it is read."));
    if (r && r->read_at > 0 && !changedOnLichess(*r, &study)) {
        ImGui::SameLine(0, px(16));
        ImGui::BeginDisabled(study_job_.has_value());
        if (ImGui::SmallButton(TR("Read again"))) readStudy(id, imported);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", imported ? trf("Its PGN asked again; its changed chapters replaced in “{}”.",
                                                   shelf().entries()[*imported].name)
                                                   .c_str()
                                             : TR("Its PGN asked again, the changed chapters marked."));
    }
    if (study_said_of_ == id && !study_said_.empty())
        ImGui::TextColored(
            study_said_bad_ ? ImGui::ColorConvertU32ToFloat4(ui::WARN) : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s",
            study_said_.c_str());
    if (imported && !(study_said_of_ == id && !study_said_.empty())) {
        ImGui::TextDisabled("%s", trf("In “{}”: each chapter a game; read again, only the changed chapters are "
                                      "replaced.",
                                      s.entries()[*imported].name)
                                      .c_str());
        ui::sameLineOrWrap(TR("Open the database"));
        if (ImGui::SmallButton(TR("Open the database"))) {
            db_selected_ = static_cast<int>(*imported);
            db_list_     = true;
            page_        = Page::Databases;
        }
    }
}

// ---------------------------------------------------------------------------
// From the Board

void App::openStudySend() {
    loadStudiesState();
    if (!chessBoard() || toLichessLock(true) || board_.tree.main.moves.empty()) return;
    StudySend s;
    const auto& main  = board_.tree.main;
    std::string title = board_.title;
    if (title.empty()) {
        const auto w = main.header("White"), b = main.header("Black");
        if (!w.empty() || !b.empty()) title = fmt::format("{} – {}", w.empty() ? "?" : w, b.empty() ? "?" : b);
    }
    std::snprintf(s.chapter.data(), s.chapter.size(), "%s", title.c_str());
    s.black = board_.flip;
    // The study of last time first, when it can still take one.
    if (const auto* r = studies_.find(studies_.last_study); r && !lichess::studyFull(r->chapters)) s.study = r->id;
    s.where     = s.study.empty() && studies_.studies.empty() ? 1 : 0;
    study_send_ = std::move(s);
    page_       = Page::Board;
    // The account's studies to choose from, asked once a session (the Lichess page's card asks the same).
    const std::string user = lichess_account_ ? lichess_account_->username : std::string();
    const auto key         = user + (studyToken(false).empty() ? "" : "+read");
    if (!user.empty() && !studies_list_job_.valid() && studies_list_of_ != key && !lichess_link_) {
        studies_list_of_  = key;
        studies_list_job_ = std::async(std::launch::async,
                                       [user, token = studyToken(false)] { return lichess::userStudies(user, token); });
    }
}

void App::startStudySend() {
    if (!study_send_ || study_send_job_.valid()) return;
    auto& s = *study_send_;
    s.error.clear();
    s.failure = {};
    s.refused = false;
    if (toLichessLock(true)) {
        s.error =
            TR("A game you are playing on Lichess is on this line: nothing of it leaves the machine while it is "
               "played.");
        return;
    }
    if (!lichess_account_ || studyToken(true).empty()) {  // the permission first, then the game sent
        s.after_link = true;
        askStudyPermission(true);
        return;
    }
    lichess::SendRequest r;
    if (s.where == 0) {
        if (s.study.empty()) {
            s.error = TR("Choose one of your studies, or a new one.");
            return;
        }
        r.study = s.study;
        if (const auto* known = studies_.find(s.study)) r.chapters = known->chapters;
    } else {
        if (!lichess::studyNameOk(s.name.data())) {
            s.error = TR("A study's name has 2 to 100 characters.");
            return;
        }
        if (lichess::madeToday(studies_.made, nowSeconds()) >= lichess::STUDIES_A_DAY) {
            s.error = trf("Lichess lets an account make {} studies a day: send it into one of yours, or tomorrow.",
                          lichess::STUDIES_A_DAY);
            return;
        }
        r.new_name   = s.name.data();
        r.visibility = s.visibility == 2   ? lichess::Visibility::Public
                       : s.visibility == 1 ? lichess::Visibility::Unlisted
                                           : lichess::Visibility::Private;
    }
    auto& b            = board_;
    r.chapter.pgn      = lichess::studyGamePgn(b.tree, b.chess960, s.assistant);
    r.chapter.name     = s.chapter.data();
    r.chapter.black    = s.black;
    r.chapter.chess960 = b.chess960;
    s.going            = {};
    s.going.study_name = s.where == 1 ? std::string(s.name.data()) : s.study;
    if (s.where == 0) {
        if (const auto* known = studies_.find(s.study)) s.going.study_name = known->name;
        if (studies_list_)
            for (const auto& x : *studies_list_)
                if (x.id == s.study) s.going.study_name = x.name;
    }
    s.going.chapter_name = r.chapter.name;
    s.going.visibility   = s.where == 1             ? lichess::visibilityKey(r.visibility)
                           : studies_.find(s.study) ? studies_.find(s.study)->visibility
                                                    : std::string();
    s.going.pgn          = r.chapter.pgn;
    s.going.tags         = lichess::gameTags(b.tree);
    s.going.assistant    = s.assistant;
    s.going.black        = s.black;
    s.going.chess960     = b.chess960;
    s.serial             = board_game_serial_;
    s.count              = b.changes.count;
    s.busy               = true;
    study_send_job_ =
        std::async(std::launch::async, [r, token = studyToken(true)] { return lichess::sendToStudy(r, token); });
}

void App::sendStudyChanges() {
    auto& b = board_;
    if (!b.sent || study_update_job_.valid()) return;
    if (toLichessLock(true)) {
        boardToast(
            TR("A game you are playing on Lichess is on this line: nothing of it leaves the machine while it is "
               "played."));
        return;
    }
    if (studyToken(true).empty()) {
        study_update_after_link_ = true;
        askStudyPermission(true);
        return;
    }
    study_update_        = *b.sent;
    study_update_.pgn    = lichess::studyGamePgn(b.tree, b.chess960, b.sent->assistant);
    study_update_.tags   = lichess::gameTags(b.tree);
    study_update_serial_ = board_game_serial_;
    study_update_count_  = b.changes.count;
    study_update_error_  = {};
    const auto tags =
        study_update_.tags != b.sent->tags ? lichess::tagsPgn(study_update_.tags, b.sent->tags) : std::string();
    study_update_job_ = std::async(std::launch::async, [study = study_update_.study, chapter = study_update_.chapter,
                                                        pgn = study_update_.pgn, tags, token = studyToken(true)] {
        auto moves = lichess::updateChapterMoves(study, chapter, pgn, token);
        if (!moves.value || tags.empty()) return moves;
        auto tagged = lichess::updateChapterTags(study, chapter, tags, token);
        return tagged.value ? moves : tagged;
    });
}

void App::drawSentLine() {
    auto& b = board_;
    if (!b.sent) return;
    const ui::TopicRegion topic(guide::topics::LichessStudies);
    const auto& s      = *b.sent;
    const bool changed = b.since_sent.any();
    const auto n       = static_cast<uint64_t>(b.since_sent.count < 0 ? -b.since_sent.count : b.since_sent.count);
    std::string text   = s.opened ? TR("From your Lichess study") : TR("Sent to Lichess");
    if (changed)
        text += " · " + (b.since_sent.lost || n == 0 ? std::string(TR("changed since"))
                                                     : trn("{} change since", "{} changes since", n, n));
    ImGui::AlignTextToFramePadding();
    if (changed)
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", text.c_str());
    else
        ImGui::TextDisabled("%s", text.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "%s", trf("“{}”, in the study “{}” on lichess.org\n{}", s.chapter_name, s.study_name, s.url()).c_str());
    ImGui::SameLine(0, px(4));
    if (ui::iconOnlyButton("##sent_open", ui::Icon::External, TR("Open the chapter on lichess.org")))
        SDL_OpenURL(s.url().c_str());
    if (changed) {
        ImGui::SameLine();
        const auto* locked = toLichessLock(true);
        if (study_update_job_.valid()) {
            ImGui::TextDisabled("%s", TR("Sending…"));
        } else {
            ImGui::BeginDisabled(locked != nullptr || lichess_link_ != nullptr);
            if (ImGui::SmallButton(TR("Send the changes"))) sendStudyChanges();
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", locked ? TR("Not on the line of a game you are playing.")
                                        : s.assistant
                                            ? TR("The chapter's moves, variations and notes replaced on Lichess by the "
                                                 "game's, the assistant's comments with them, and its tags when they "
                                                 "changed. Never on its own.")
                                            : TR("The chapter's moves, variations and notes replaced on Lichess by the "
                                                 "game's, without the assistant's comments, and its tags when they "
                                                 "changed. Never on its own."));
        }
    }
    if (study_update_error_.failed()) {  // writing the chapter: the permission it needs, a token refused (#141)
        drawRepairNotice(
            lichessNotice(study_update_error_,
                          trf("Lichess did not take the changes: {}.", lichess::describe(study_update_error_)),
                          lichessAccess(lichess::SCOPE_STUDY_WRITE), {}),
            "study_update");
    }
}

void App::drawStudySendCard() {
    if (!study_send_) return;
    auto& s    = *study_send_;
    bool close = false;
    // Enter and Escape are the card's, unless a field or a list (its combo) has them: a list closed by Escape in this
    // frame was open in the one before.
    const int frame = ImGui::GetFrameCount();
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) study_popup_frame_ = frame;
    const bool keys = !ImGui::GetIO().WantTextInput && frame - study_popup_frame_ > 1;
    {
        const ui::TopicRegion topic(guide::topics::LichessStudies);  // around the card, closed after it
        if (s.sent) {
            // Sent: its link, opened or copied.
            const auto& sent = *s.sent;
            beginTinted("study_sent", ui::GOOD);
            ImGui::PushTextWrapPos(0);
            ui::inlineIcon(ui::Icon::Check, ui::GOOD);
            ImGui::SameLine();
            ImGui::TextUnformatted((s.chapters > 0 ? trf("Sent: chapter {} of “{}”", s.chapters, sent.study_name)
                                                   : trf("Sent: “{}” in “{}”", sent.chapter_name, sent.study_name))
                                       .c_str());
            if (const char* who = visibilityMeaning(sent.visibility); *who) ImGui::TextDisabled("%s", who);
            if (ui::iconButton("study_sent_open", ui::Icon::External, TR("Open it on lichess.org")))
                SDL_OpenURL(sent.url().c_str());
            ImGui::SameLine();
            if (ui::iconButton("study_sent_copy", ui::Icon::Copy, TR("Copy its link"))) {
                ImGui::SetClipboardText(sent.url().c_str());
                boardToast(TR("The chapter's link is copied"));
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Close")) || (keys && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) close = true;
            ImGui::TextDisabled("%s", TR("Later, the game's strip says how many changes since, with Send the changes: "
                                         "nothing goes on its own."));
            ImGui::PopTextWrapPos();
            endTinted();
        } else {
            ui::beginCard("study_send");
            ImGui::PushTextWrapPos(0);
            ImGui::PushFont(ui::titleFont(), ImGui::GetFontSize() * 1.1f);
            ImGui::TextUnformatted(TR("Send the game to a Lichess study"));
            ImGui::PopFont();
            const auto& b = board_;
            ImGui::TextDisabled(
                "%s",
                (b.title.empty() ? gameSummary(b.tree) : fmt::format("{} · {}", b.title, gameSummary(b.tree))).c_str());
            ImGui::BeginDisabled(s.busy || lichess_link_ != nullptr);
            // Where: one of the user's studies (a full one is not offered), or a new one, private first.
            ImGui::SeparatorText(TR("Where"));
            const auto studies = lichess::roomyStudies(
                studies_list_ ? *studies_list_ : [&] {
                    std::vector<lichess::StudyInfo> known;
                    for (const auto& r : studies_.studies) known.push_back({r.id, r.name, 0, 0});
                    return known;
                }(),
                studies_);
            if (s.where == 0 && s.study.empty() && !studies.empty()) s.study = studies.front().id;
            if (std::none_of(studies.begin(), studies.end(), [&](const auto& x) { return x.id == s.study; }))
                s.study = studies.empty() ? std::string() : studies.front().id;
            ImGui::RadioButton(TR("One of your studies:"), &s.where, 0);
            ImGui::SameLine();
            const auto label = [&](const lichess::StudyInfo& x) {
                const auto* r = studies_.find(x.id);
                return r && r->chapters >= 0 ? trn("{} ({} chapter of {})", "{} ({} chapters of {})", r->chapters,
                                                   x.name, r->chapters, lichess::STUDY_CHAPTERS)
                                             : x.name;
            };
            std::string current = TR("none yet");
            for (const auto& x : studies)
                if (x.id == s.study) current = label(x);
            ImGui::SetNextItemWidth(std::max(px(160), ImGui::GetContentRegionAvail().x));
            ImGui::BeginDisabled(studies.empty());
            if (ImGui::BeginCombo("##send_study", current.c_str())) {
                for (const auto& x : studies) {
                    if (ImGui::Selectable(label(x).c_str(), x.id == s.study)) {
                        s.study = x.id;
                        s.where = 0;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            if (!studies_list_ && studies_list_job_.valid())
                ImGui::TextDisabled("%s", TR("Asking Lichess for your studies (your name goes)…"));
            else if (!studies_list_ && studies_list_error_.failed())
                drawRepairNotice(lichessNotice(studies_list_error_,
                                               trf("Lichess did not give your studies ({}): those read before show "
                                                   "here.",
                                                   lichess::describe(studies_list_error_)),
                                               lichessAccess(), retryState(Retry::StudiesList, lichess::apiHost())),
                                 "send_studies_list");
            ImGui::RadioButton(TR("A new study"), &s.where, 1);
            if (s.where == 1) {
                ImGui::Indent();
                ImGui::SetNextItemWidth(std::max(px(160), ImGui::GetContentRegionAvail().x));
                if (s.focus) {
                    ImGui::SetKeyboardFocusHere();
                    s.focus = false;
                }
                const bool enter = ImGui::InputTextWithHint("##send_new_name", TR("Its name"), s.name.data(),
                                                            s.name.size(), ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::RadioButton(TR("Private"), &s.visibility, 0);
                ImGui::SameLine();
                ImGui::RadioButton(TR("Unlisted"), &s.visibility, 1);
                ImGui::SameLine();
                ImGui::RadioButton(TR("Public"), &s.visibility, 2);
                const int made = lichess::madeToday(studies_.made, nowSeconds());
                ImGui::TextDisabled("%s", trf("Lichess lets an account make {} studies a day ({} made from here in the "
                                              "last day), each up to {} chapters.",
                                              lichess::STUDIES_A_DAY, made, lichess::STUDY_CHAPTERS)
                                              .c_str());
                ImGui::Unindent();
                if (enter) startStudySend();
            }
            // The chapter: its name, the side it is seen from, the assistant's comments or not.
            ImGui::SeparatorText(TR("The chapter"));
            ImGui::SetNextItemWidth(std::max(px(160), ImGui::GetContentRegionAvail().x));
            if (ImGui::InputTextWithHint("##send_chapter", TR("Its name"), s.chapter.data(), s.chapter.size(),
                                         ImGuiInputTextFlags_EnterReturnsTrue))
                startStudySend();
            int side = s.black ? 1 : 0;
            if (ImGui::RadioButton(TR("seen from White"), &side, 0)) s.black = false;
            ImGui::SameLine();
            if (ImGui::RadioButton(TR("from Black"), &side, 1)) s.black = true;
            const int notes = lichess::assistantComments(b.tree);
            ImGui::BeginDisabled(notes == 0);
            ImGui::Checkbox(notes > 0 ? trf("with the assistant's comments ({})", notes).c_str()
                                      : TR("with the assistant's comments"),
                            &s.assistant);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", notes > 0 ? TR("Left out, the comments the assistant wrote stay here only.")
                                                  : TR("The game has no comment of the assistant's."));
            ImGui::EndDisabled();
            // What goes.
            ImGui::SeparatorText(TR("What goes to lichess.org"));
            const bool may_write = !studyToken(true).empty();
            std::string what =
                TR("The game as PGN: its headers, its moves and variations, your comments and "
                   "symbols, its arrows and squares, its clocks. Nothing else of yours.");
            if (!lichess_account_)
                what += " " + std::string(TR("Lichess's page opens to link your account, with the permission to write "
                                             "in your studies (study:write)."));
            else if (!may_write)
                what += " " + std::string(TR("The first time, Lichess asks you to allow RookStudio to write in your "
                                             "studies (study:write)."));
            ImGui::TextDisabled("%s", what.c_str());
            if (lichess_link_) {
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s",
                                   TR("Waiting for Lichess: allow RookStudio in the browser's page, then come back "
                                      "here."));
            } else if (s.refused) {  // the user's no: the Send button asks again
                drawNotice(Notice{Notice::Level::Warning, TR("You refused it on Lichess: nothing was sent."), {}, {}},
                           "study_send_error");
            } else if (s.failure.failed()) {
                // Lichess's failure as it came, with its repair (#146): a token refused, Link Lichess again…; the
                // permission to write missing, Allow writing studies…; the game sent once Lichess gave it.
                const auto notice = lichessNotice(s.failure, trf("Not sent: {}.", lichess::describe(s.failure)),
                                                  lichessAccess(lichess::SCOPE_STUDY_WRITE), {});
                if (const auto* fix = drawNotice(notice, "study_send_error")) {
                    if (fix->kind == Fix::Kind::LinkLichess) s.after_link = true;
                    applyFix(*fix);
                }
            } else if (!s.error.empty()) {  // the program's own sentence: a study to choose, a name, the quota, a lock
                drawNotice(Notice{Notice::Level::Warning, s.error, {}, {}}, "study_send_error");
            }
            const bool can =
                !s.busy && !lichess_link_ && (s.where == 0 ? !s.study.empty() : lichess::studyNameOk(s.name.data()));
            ImGui::BeginDisabled(!can);
            const bool enter = keys && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
            if (ImGui::Button(s.busy ? TR("Sending…") : TR("Send")) || (enter && can)) startStudySend();
            ImGui::EndDisabled();
            ImGui::SameLine(0, px(4));
            ImGui::TextDisabled("%s", TR("Enter"));
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Cancel")) || (keys && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
                close = true;
                if (s.after_link && lichess_link_ && lichess_link_->cancel) *lichess_link_->cancel = true;
            }
            ImGui::SameLine(0, px(4));
            ImGui::TextDisabled("%s", TR("Esc"));
            ImGui::PopTextWrapPos();
            ui::endCard();
        }
    }
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) study_popup_frame_ = frame;
    if (close) study_send_.reset();
}

}  // namespace fcg
