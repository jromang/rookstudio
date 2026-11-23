// Your mistakes as exercises (#81, mockup M12, board 2; mistakes.hpp): the user's names, the Training page's part "My
// mistakes" (the exercises and their kinds, the games they come from, the latest games without a review reviewed in the
// background), and a session on the Board page whose column becomes the exercise's, as the repertoires' training and
// the puzzles do. The states of the mockup: the list, an exercise, the answer right or wrong, the end.
//
// The keys of an exercise: a move played on the board, or typed (its letters open the move field); Alt+H a hint (the
// piece to play), Space "I don't know", Escape stops. After the answer, Enter goes on (or tries again after a wrong
// one), Alt+A the engine.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <future>
#include <set>
#include <sstream>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>  // SetKeyOwner

#include <study/background.hpp>
#include <study/game_tree.hpp>
#include <study/import.hpp>

#include "app.hpp"
#include "board_text.hpp"
#include "board_view.hpp"
#include "console.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "review_bridge.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

using Clock        = std::chrono::steady_clock;
using MistakePhase = MistakeRun::Phase;

double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

// The latest games of the user's that tell which wait for a review.
constexpr std::size_t RECENT_GAMES = 20;

commentary::Lang lang() {
    return i18n::language() == i18n::Language::French ? commentary::Lang::Fr : commentary::Lang::En;
}

ImU32 mix(ImU32 a, ImU32 b, float t) {
    const auto fa = ImGui::ColorConvertU32ToFloat4(a), fb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(fa.x + (fb.x - fa.x) * t, fa.y + (fb.y - fa.y) * t, fa.z + (fb.z - fa.z) * t, 1.0f));
}

void beginTinted(const char* id, ImU32 tone) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BG, tone, 0.08f)));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BD, tone, 0.55f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(14)));
    ImGui::BeginChild(id, ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
}

void endTinted() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

bool primaryButton(const char* label, bool enabled = true) {
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImGui::ColorConvertU32ToFloat4(mix(ui::ACCENT, IM_COL32(255, 255, 255, 255), 0.15f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImGui::ColorConvertU32ToFloat4(mix(ui::ACCENT, IM_COL32(0, 0, 0, 255), 0.15f)));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    const bool clicked = ImGui::Button(label);
    ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
    return clicked;
}

void bigCount(const std::string& n, const std::string& what) {
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.9f);
    ImGui::TextUnformatted(n.c_str());
    ImGui::PopFont();
    ImGui::TextDisabled("%s", what.c_str());
    ImGui::EndGroup();
}

void wrapped(const std::string& text, bool grey = false) {
    if (grey) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    if (grey) ImGui::PopStyleColor();
}

void tip(const std::string& text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

bool chip(const char* label, bool chosen, const char* id) {
    const auto& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(12));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), style.FramePadding.y * 0.8f));
    if (chosen) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BG, ui::ACCENT, 0.30f)));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::CARD_BG));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(ui::CARD_BD));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    const bool clicked = ImGui::Button(fmt::format("{}##{}", label, id).c_str());
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    return clicked;
}

// A move with its number as the interface writes it: "24.Nc7+", "23…Qxd4".
std::string numbered(const chess::Board& before, chess::Move move) {
    return fmt::format("{}{}{}", before.fullMoveNumber(), before.sideToMove() == chess::Color::WHITE ? "." : "…",
                       localSan(chess::uci::moveToSan(before, move)));
}

// A line of UCI moves from `board`, numbered as the interface writes it, `most` moves at most: "23…Nc2+ 24.Kd1 Nxa1".
std::string lineText(chess::Board board, const std::string& uci, std::size_t most) {
    std::istringstream in(uci);
    std::string out, word;
    for (std::size_t n = 0; n < most && in >> word; ++n) {
        chess::Move m = chess::Move::NO_MOVE;
        try {
            m = chess::uci::uciToMove(board, word);
        } catch (const std::exception&) {
        }
        chess::Movelist legal;
        chess::movegen::legalmoves(legal, board);
        if (m == chess::Move::NO_MOVE || std::find(legal.begin(), legal.end(), m) == legal.end()) break;
        const bool number = n == 0 || board.sideToMove() == chess::Color::WHITE;
        out += (out.empty() ? "" : " ") + (number ? numbered(board, m) : localSan(chess::uci::moveToSan(board, m)));
        board.makeMove(m);
    }
    return out;
}

// A score for the side to move, as White's: "+2.9", "−2.4", "#3".
std::string whiteText(const std::optional<fastchess::Score>& score, chess::Color stm) {
    if (!score) return "?";
    return ui::formatScore(whiteScore(*score, stm));
}

// When a card comes back: "today", "tomorrow", "in 6 days".
std::string whenText(int64_t due, int64_t day_end) {
    if (due <= day_end) return TR("today");
    const auto days = std::max<int64_t>(1, (due - day_end + study::fsrs::DAY - 1) / study::fsrs::DAY);
    if (days == 1) return TR("tomorrow");
    return trn("in {} day", "in {} days", static_cast<uint64_t>(days), days);
}

// The page's choice of kind for the exercises "To work on" quotes under no topic.
constexpr const char* OTHERS = "-";

// A "To work on" topic as a kind of exercise.
std::string kindName(const std::string& key) {
    const char* name = mistakeKindName(key);
    return *name ? std::string(TR(name)) : key;
}

// The game an exercise comes from, in a line: "Partner – Tester · Club game, 10+5".
std::string gameLine(const MistakeExercise& e) {
    auto out          = fmt::format("{} – {}", e.white.empty() ? "?" : e.white, e.black.empty() ? "?" : e.black);
    std::string where = e.event == "?" ? std::string() : e.event;
    if (!e.time_control.empty() && e.time_control != "-" && e.time_control != "?") {
        // Lichess's "600+5" as "10+5".
        auto tc = e.time_control;
        if (const auto plus = tc.find('+'); plus != std::string::npos) {
            try {
                const int base = std::stoi(tc.substr(0, plus));
                if (base >= 60 && base % 60 == 0) tc = std::to_string(base / 60) + tc.substr(plus);
            } catch (...) {
            }
        }
        where += (where.empty() ? "" : ", ") + tc;
    }
    if (!where.empty()) out += " · " + where;
    return out;
}

// What the review said of the mistake, in the language of the interface when it was written in it: "21…Rfd8??: Loses a
// pawn after 22.b3. 21…Rfe8 was better."
std::string reviewSaid(const MistakeExercise& e, const chess::Board& board) {
    if (e.comment.empty() || e.comment_lang != (lang() == commentary::Lang::Fr ? "fr" : "en")) return {};
    try {
        const auto played = chess::uci::uciToMove(board, e.played);
        return fmt::format("{}{}: {}", numbered(board, played), judgementMark(e.judgement), e.comment);
    } catch (const std::exception&) {
        return e.comment;
    }
}

std::string shortDate(const std::string& date) {
    if (date.empty() || date.find("????") == 0) return {};
    std::string out = date;
    while (out.size() > 4 && (out.ends_with(".??") || out.ends_with("."))) out.erase(out.size() - 3);
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// The user's names

std::vector<std::string> App::userNames() const {
    auto out = player_names_;
    if (lichess_account_ && !lichess_account_->username.empty() && !isUserName(lichess_account_->username, out))
        out.push_back(lichess_account_->username);
    return out;
}

void App::setPlayerNames(std::vector<std::string> names) {
    std::erase_if(names, [](const std::string& n) { return nameWords(n).empty(); });
    player_names_ = std::move(names);
    if (!options_.scripted) storage_.setPlayerNames(player_names_);
    mistake_scan_checked_ = {};  // looked for again at once
}

void App::drawPlayerNames(const char* id) {
    ImGui::PushID(id);
    std::optional<std::size_t> remove;
    for (std::size_t i = 0; i < player_names_.size(); ++i) {
        const auto label = player_names_[i] + "  ×";
        if (i > 0) ui::sameLineOrWrap(label.c_str());
        if (chip(label.c_str(), false, fmt::format("name{}", i).c_str())) remove = i;
        tip(TR("Click to take this name away."));
    }
    if (lichess_account_ && !lichess_account_->username.empty()) {
        const auto label = trf("{} (Lichess)", lichess_account_->username);
        if (!player_names_.empty()) ui::sameLineOrWrap(label.c_str());
        ImGui::BeginDisabled();
        (void)chip(label.c_str(), true, "lichess_name");
        ImGui::EndDisabled();
        tip(TR("The linked Lichess account's name counts by itself."));
    }
    if (remove) {
        auto names = player_names_;
        names.erase(names.begin() + static_cast<std::ptrdiff_t>(*remove));
        setPlayerNames(std::move(names));
    }
    ImGui::SetNextItemWidth(px(260));
    const bool entered =
        ImGui::InputTextWithHint("##player_name", TR("A name you play under, then Enter"), player_name_input_.data(),
                                 player_name_input_.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const bool add = ImGui::Button(TR("Add##player_name"));
    if ((entered || add) && !nameWords(player_name_input_.data()).empty()) {
        auto names = player_names_;
        names.emplace_back(player_name_input_.data());
        setPlayerNames(std::move(names));
        player_name_input_.fill(0);
    }
    ImGui::PopID();
}

// ---------------------------------------------------------------------------
// The exercises, looked for

MistakeRecord& App::mistakeRecord() {
    if (!mistake_record_) mistake_record_ = readMistakeRecord(mistakesFolder());
    return *mistake_record_;
}

void App::saveMistakeRecord() {
    if (options_.scripted || !mistake_record_) return;  // a run for captures leaves the profile as it was
    if (!writeMistakeRecord(mistakesFolder(), *mistake_record_))
        Console::instance().add(
            trf("The exercises of your mistakes could not be kept in {}", mistakesFolder().string()), true);
}

std::string App::mistakeScanKey() {
    std::string key = lang() == commentary::Lang::Fr ? "fr" : "en";
    for (const auto& n : userNames()) key += "|" + n;
    key += "#";
    auto& s = shelf();
    for (std::size_t i = 0; i < s.entries().size(); ++i) {
        const auto& e = s.entries()[i];
        if (e.source) continue;  // a tournament's games or a file read where it is: not the user's databases
        const auto db = s.open(i);
        key += fmt::format("|{}:{}", e.folder.string(), db ? db->snapshot()->generation() : 0);
    }
    std::error_code ec;
    const auto written = fs::last_write_time(storage_.root() / "analyses", ec);
    key += fmt::format("|{}", ec ? 0 : written.time_since_epoch().count());
    return key;
}

std::vector<const MistakeExercise*> App::mistakeExercises(const std::string* kind) {
    // The positions the repertoires train, by side: trained on the Openings page, not here. Gathered once a frame.
    if (mistake_keys_frame_ != ImGui::GetFrameCount()) {
        mistake_keys_frame_ = ImGui::GetFrameCount();
        for (auto& set : mistake_repertoire_keys_) set.clear();
        for (std::size_t i = 0; i < repertoires().entries().size(); ++i) {
            auto& set = mistake_repertoire_keys_[repertoires().entries()[i].side == chess::Color::WHITE ? 0 : 1];
            for (const auto& c : trainingSummary(i).cards) set.insert(c.key);
        }
    }
    std::vector<const MistakeExercise*> out;
    for (const auto& [key, e] : mistakeRecord().exercises) {
        if (kind && e.kind != (*kind == OTHERS ? std::string() : *kind)) continue;
        if (mistake_repertoire_keys_[e.side() == chess::Color::WHITE ? 0 : 1].contains(key)) continue;
        out.push_back(&e);
    }
    return out;
}

MistakeCounts App::mistakeDayCounts(const std::string* kind) {
    return mistakeCounts(mistakeExercises(kind), trainingDay(epochNow()),
                         static_cast<std::size_t>(std::max(0, trainingSettings().new_per_day)));
}

void App::pollMistakes() {
    const auto now = Clock::now();
    // Looked for again when what it looks at changed: on the Training page, every two seconds at most.
    if (page_ == Page::Training && !mistake_scan_job_ && now - mistake_scan_checked_ >= std::chrono::seconds(2)) {
        mistake_scan_checked_ = now;
        auto key              = mistakeScanKey();
        if (key != mistake_scan_of_) {
            std::vector<MistakeSource> sources;
            auto& s = shelf();
            for (std::size_t i = 0; i < s.entries().size(); ++i) {
                const auto& e = s.entries()[i];
                if (e.source) continue;
                if (const auto db = s.open(i)) sources.push_back({e.name, e.folder, db->snapshot()});
            }
            MistakeScanJob job;
            job.of = std::move(key);
            job.done =
                std::async(std::launch::async, [sources = std::move(sources), names = userNames(),
                                                analyses = storage_.root() / "analyses", before = mistakeRecord(),
                                                language = lang(), cancel = job.cancel] {
                    study::enterBackground();
                    return scanMistakes(sources, names, analyses, before, RECENT_GAMES, language, cancel.get());
                });
            mistake_scan_job_ = std::move(job);
        }
    }
    if (mistake_scan_job_ && mistake_scan_job_->done.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto scan = mistake_scan_job_->done.get();
        if (!scan.cancelled) {
            // The cards as they are now: answers given while it looked are kept, those of exercises it no longer
            // finds kept apart.
            auto& live = mistakeRecord();
            for (auto& [key, e] : scan.record.exercises) {
                if (const auto it = live.exercises.find(key); it != live.exercises.end()) {
                    e.card  = it->second.card;
                    e.since = it->second.since;
                }
            }
            for (const auto& [key, e] : live.exercises)
                if (!scan.record.exercises.contains(key)) scan.record.keep(e);
            for (const auto& [key, k] : live.kept)
                if (!scan.record.exercises.contains(key)) scan.record.kept.try_emplace(key, k);
            scan.record.answer_ms = live.answer_ms;
            mistake_record_       = std::move(scan.record);
            saveMistakeRecord();
            mistake_games_      = scan.games;
            mistake_judged_     = scan.reviewed;
            mistake_unreviewed_ = std::move(scan.unreviewed);
            mistake_scanned_    = true;
            mistake_scan_of_    = mistake_scan_job_->of;
        }
        mistake_scan_job_.reset();
    }

    // The games reviewed in the background, one after the other.
    if (mistake_reviews_ && mistake_reviews_->runner) {
        auto& r = *mistake_reviews_;
        // The Board page's own analysis and review have the machine meanwhile; a live game of the user's on Lichess
        // silences every engine (#48).
        r.runner->setPaused(board_.analysing || board_.reviewing() || liveGameOn() != nullptr);
        if (r.runner->state(r.state, r.version) && !r.state.running) {
            const auto& u = r.games[r.done];
            if (!r.state.error.empty()) {
                r.error = r.state.error;
                r.runner.reset();
            } else if (r.state.settled.complete(u.game.moves.size())) {
                const auto analyses = storage_.root() / "analyses";
                (void)saveReview(analyses, u.game, r.state.settled);
                auto exercises = exercisesOf(u.game, r.state.settled, u.side, lang());
                for (auto& e : exercises) {
                    e.database = u.database;
                    e.folder   = u.folder;
                    e.game_id  = u.id;
                    e.reviewed = epochNow();
                }
                const auto key = reviewKey(u.game);
                putExercises(mistakeRecord(), key,
                             reviewSignature(r.state.settled) + "|" + (lang() == commentary::Lang::Fr ? "fr" : "en"),
                             std::move(exercises));
                saveMistakeRecord();
                std::erase_if(mistake_unreviewed_, [&](const auto& g) { return reviewKey(g.game) == key; });
                ++mistake_judged_;
                r.plies_done += static_cast<double>(u.game.moves.size());
                r.seconds += since(r.game_started);
                ++r.done;
                startMistakeReview();
            } else {
                r.runner.reset();  // stopped
            }
        }
    }

    // A session's answer, judged by the review's engine.
    if (mistake_) {
        auto& m = *mistake_;
        m.judge.poll();
        if (m.phase == MistakePhase::Checking) {
            if (m.judge.state() == Analyser::State::Failed) {
                m.check_error = m.judge.error();
                m.phase       = MistakePhase::Wrong;  // not graded: said, Retry or Next
            } else if (m.judge.finished() && m.judge.position() == m.checking && !m.judge.lines().empty()) {
                const auto& l    = m.judge.lines().front();
                m.searched       = {};
                m.searched.score = l.score;
                m.searched.wdl   = l.wdl;
                m.searched.depth = static_cast<int>(l.depth);
                for (const auto& u : l.pv) m.searched.pv += (m.searched.pv.empty() ? "" : " ") + u;
                m.searched.best = m.judge.bestmove();
                m.judge_used    = true;
                m.check         = checkAnswer(m.exercises[m.current], m.played_move, m.searched);
                mistakeJudged();
            }
        }
    }
    // A game drawn at random for guessing.
    if (guess_pick_.valid() && guess_pick_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto pick = guess_pick_.get();
        if (pick.game)
            startGuess(std::move(*pick.game), pick.chess960, std::move(pick.title), guess_pick_side_, 0,
                       std::make_pair(pick.folder, pick.id));
        else
            guess_error_ = pick.error == "none" ? TR("No game of this database passes the filter.")
                                                : TR("No game long enough passes the filter.");
    }
    if (guess_) pollGuess();
}

// ---------------------------------------------------------------------------
// The games without a review, reviewed in the background

void App::startMistakeReviews() {
    if (mistake_reviews_ || mistake_unreviewed_.empty()) return;
    const auto* engine = analysisEngine();
    if (!engine) {
        boardToast(TR("No engine to review with: add one on the Engines page."));
        return;
    }
    MistakeReviews r;
    r.games          = mistake_unreviewed_;
    r.engine         = engine->name;
    r.started        = Clock::now();
    mistake_reviews_ = std::move(r);
    startMistakeReview();
}

void App::startMistakeReview() {
    auto& r = *mistake_reviews_;
    r.runner.reset();
    if (r.done >= r.games.size()) {
        Console::instance().add(trn("Your games reviewed in the background: {} game",
                                    "Your games reviewed in the "
                                    "background: {} games",
                                    r.done, r.done));
        mistake_reviews_.reset();
        mistake_scan_checked_ = {};
        return;
    }
    const auto it = std::find_if(engines_.begin(), engines_.end(), [&](const auto& e) { return e.name == r.engine; });
    if (it == engines_.end()) {
        r.error = trf("{} is no longer in the library: add an engine on the Engines page.", r.engine);
        return;
    }
    // As the Board page reviews a game in the Chess space (#61, #63): nodes, one thread, an empty table each
    // position, Lichess's evaluations kept offline taken as they are, the processes the tournaments leave.
    loadBoardSettings();
    const auto& u = r.games[r.done];
    ReviewRunner::Setup setup;
    setup.engine       = *it;
    setup.settings     = board_.settings.analysis;
    setup.game         = u.game;
    setup.ceiling_ms   = board_.settings.review_ceiling_ms;
    setup.known        = reviewKnown();
    setup.reproducible = true;
    r.processes = reviewProcesses(std::max(1, board_.settings.analysis.threads), topology_.physical, topology_.logical,
                                  tournamentCores());
    setup.processes   = r.processes;
    setup.second_line = secondLines(u.game, false);
    r.runner          = std::make_unique<ReviewRunner>();
    r.runner->start(std::move(setup));
    r.state        = {};
    r.version      = 0;
    r.game_started = Clock::now();
}

void App::cancelMistakeReviews() {
    if (!mistake_reviews_) return;
    if (mistake_reviews_->runner) mistake_reviews_->runner->stop();
    mistake_reviews_.reset();
}

void App::drawMistakeReviewsCard() {
    if (mistake_reviews_) {
        auto& r = *mistake_reviews_;
        beginTinted("##mistake_reviews", ui::ACCENT);
        ImGui::PushTextWrapPos();
        const auto total = r.games.size();
        ImGui::TextUnformatted(trn("{} of your latest games is being reviewed.",
                                   "{} of your latest games are being "
                                   "reviewed.",
                                   total, total)
                                   .c_str());
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
        double part = 0;
        if (r.runner && r.state.running) {
            const auto n        = std::max<std::size_t>(1, r.state.done.size());
            const auto searched = static_cast<double>(std::count(r.state.done.begin(), r.state.done.end(), true)) /
                                  static_cast<double>(n);
            const int passes    = reviewPasses(board_.settings.review_ceiling_ms);
            part                = (static_cast<double>(r.state.pass) + searched) / std::max(1, passes);
        }
        ImGui::ProgressBar(static_cast<float>((static_cast<double>(r.done) + std::clamp(part, 0.0, 1.0)) /
                                              static_cast<double>(std::max<std::size_t>(1, total))),
                           ImVec2(-1, px(6)), "");
        ImGui::PopStyleColor();
        if (!r.error.empty()) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", r.error.c_str());
        } else {
            std::string left;
            if (r.plies_done > 0 && r.seconds > 0) {
                double plies = 0;
                for (std::size_t i = r.done; i < total; ++i) plies += static_cast<double>(r.games[i].game.moves.size());
                left = " · " + trf("about {} left", ui::formatDuration(plies * r.seconds / r.plies_done));
            }
            const bool live   = liveGameOn() != nullptr;
            const bool paused = live || board_.analysing || board_.reviewing();
            ImGui::TextDisabled(
                "%s", (paused ? (live ? trf("In the background: {} of {} · waiting while one of your games is played "
                                            "on Lichess",
                                            r.done, total)
                                      : trf("In the background: {} of {} · waiting while the Board page analyses",
                                            r.done, total))
                              : trn("In the background: {} of {}{} · {} process · the exercises come game by game",
                                    "In the background: {} of {}{} · {} processes · the exercises come game by game",
                                    r.processes, r.done, total, left, r.processes))
                          .c_str());
        }
        if (ImGui::Button(TR("Cancel##mistake_reviews"))) cancelMistakeReviews();
        tip(TR("The games reviewed so far keep their reviews and their exercises."));
        ImGui::PopTextWrapPos();
        endTinted();
        return;
    }
    if (mistake_unreviewed_.empty()) return;
    beginTinted("##mistake_reviews", ui::ACCENT);
    ImGui::PushTextWrapPos();
    const auto n = mistake_unreviewed_.size();
    ImGui::TextUnformatted(
        trn("{} of your latest games has no review.", "{} of your latest games have no review.", n, n).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("Reviewed, they give their exercises."));
    const auto* engine = analysisEngine();
    if (primaryButton(trn("Review it · in the background", "Review them · in the background", n).c_str(),
                      engine != nullptr))
        startMistakeReviews();
    tip(engine ? trf("{}, as the Board page reviews a game: the same settings, the processes the tournaments leave; "
                     "the reviews kept with the games, as if each had been reviewed on the Board page.",
                     engine->name)
               : std::string(TR("No engine to review with: add one on the Engines page.")));
    ImGui::SameLine(0, px(8));
    std::string which;
    for (std::size_t i = 0; i < n && i < 3; ++i) {
        const auto& g = mistake_unreviewed_[i].game;
        which += (which.empty() ? "" : " · ") + fmt::format("{} – {}", g.header("White"), g.header("Black"));
    }
    if (n > 3) which += " …";
    ImGui::TextDisabled("%s", which.c_str());
    ImGui::PopTextWrapPos();
    endTinted();
}

// ---------------------------------------------------------------------------
// The Training page's part

void App::drawTrainingMistakes() {
    const ui::TopicRegion topic(guide::topics::Mistakes);
    bool enter = !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
                 (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    if (enter) {
        const auto owner = ImGui::GetID("##training_enter");
        ImGui::SetKeyOwner(ImGuiKey_Enter, owner);
        ImGui::SetKeyOwner(ImGuiKey_KeypadEnter, owner);
    }
    const auto names = userNames();
    {
        std::string who;
        for (std::size_t i = 0; i < names.size() && i < 4; ++i) who += (who.empty() ? "“" : ", “") + names[i] + "”";
        wrapped(
            names.empty()
                ? std::string(TR("The moves of your reviewed games marked ? and ?? on your side, where a better move "
                                 "is clear. Which games are yours? Those where you play under one of your names."))
                : trf("The moves of your reviewed games marked ? and ?? on your side, where a better move is "
                      "clear. Your games: those you played as {} (Settings › Your names).",
                      who),
            true);
    }
    if (names.empty()) {
        beginTinted("##mistake_names", ui::ACCENT);
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(TR("Your names first"));
        wrapped(TR("The name you play under, as your databases write it (“Tester, Sam”, an account's name): a game "
                   "where it plays is yours. A word is enough when it is yours alone; the linked Lichess account's "
                   "name counts by itself."),
                true);
        drawPlayerNames("training_names");
        ImGui::PopTextWrapPos();
        endTinted();
        return;
    }
    const auto lock   = mistakeLock();
    const auto* kind  = mistake_kind_.empty() ? nullptr : &mistake_kind_;
    const auto all    = mistakeExercises();
    const auto counts = mistakeDayCounts(kind);
    ImGui::Dummy(ImVec2(0, px(2)));
    if (!all.empty()) {
        const auto every = mistakeCounts(all, trainingDay(epochNow()), 0);
        bigCount(ui::exactCount(every.exercises), trn("exercise", "exercises", every.exercises) + ", " +
                                                      trn("from {} game", "from {} games", every.games, every.games));
        ImGui::SameLine(0, px(30));
        bigCount(ui::exactCount(every.due), TR("to review today"));
        ImGui::SameLine(0, px(30));
        bigCount(ui::exactCount(every.fresh), TR("new"));
        ImGui::SameLine(0, px(30));
        bigCount(ui::exactCount(every.known), TR("known, to review later"));
        // The kinds: a chip each, a click keeps to it.
        std::map<std::string, std::size_t> kinds;
        for (const auto* e : all) ++kinds[e->kind];
        std::vector<std::pair<std::string, std::size_t>> order(kinds.begin(), kinds.end());
        std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) {
            if (a.first.empty() != b.first.empty()) return b.first.empty();  // the others last
            return a.second > b.second;
        });
        ImGui::Dummy(ImVec2(0, px(2)));
        if (chip(trf("Every kind {}", all.size()).c_str(), mistake_kind_.empty(), "kind_all")) mistake_kind_.clear();
        for (const auto& [k, n] : order) {
            const auto label  = fmt::format("{} {}", kindName(k), n);
            const auto choice = k.empty() ? std::string(OTHERS) : k;
            ui::sameLineOrWrap(label.c_str());
            if (chip(label.c_str(), mistake_kind_ == choice, choice.c_str()))
                mistake_kind_ = mistake_kind_ == choice ? std::string() : choice;
        }
        if (!mistake_kind_.empty() && !kinds.contains(mistake_kind_ == OTHERS ? std::string() : mistake_kind_))
            mistake_kind_.clear();
        // The exercises: the game, where and when, the move played and the review's, when it comes back.
        ImGui::Dummy(ImVec2(0, px(4)));
        const auto list = mistakeExercises(kind);
        std::vector<const MistakeExercise*> rows(list.begin(), list.end());
        const auto day = trainingDay(epochNow());
        std::stable_sort(rows.begin(), rows.end(), [&](const auto* a, const auto* b) {
            // The due first, then the new, the latest games first; then the known, the soonest first.
            const auto rank = [&](const MistakeExercise* e) {
                return e->card.fresh() ? 1 : e->card.due <= day.end ? 0 : 2;
            };
            if (rank(a) != rank(b)) return rank(a) < rank(b);
            if (rank(a) == 2) return a->card.due < b->card.due;
            return a->packed_date > b->packed_date;
        });
        const float height = std::min(px(260), ImGui::GetFrameHeightWithSpacing() * (1.4f + rows.size()));
        if (ImGui::BeginTable("##mistake_rows", 4,
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_BordersOuter,
                              ImVec2(0, height))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(TR("The game"), ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn(TR("Where, when"), ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn(TR("Your move, the best"), ImGuiTableColumnFlags_WidthStretch, 1.3f);
            ImGui::TableSetupColumn(TR("Comes back"), ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rows.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const auto& e = *rows[static_cast<std::size_t>(i)];
                    ImGui::PushID(i);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(fmt::format("{} – {}", e.white, e.black).c_str());
                    ImGui::TableNextColumn();
                    std::string where = e.database;
                    if (!e.event.empty() && e.event != "?") where += " · " + e.event;
                    if (const auto d = shortDate(e.date); !d.empty()) where += " · " + d;
                    ImGui::TextDisabled("%s", where.c_str());
                    ImGui::TableNextColumn();
                    try {
                        const auto board  = e.board();
                        const auto played = chess::uci::uciToMove(board, e.played);
                        const auto best   = chess::uci::uciToMove(board, e.best);
                        ImGui::TextUnformatted(trf("{} {} · better: {}", numbered(board, played),
                                                   judgementMark(e.judgement), numbered(board, best))
                                                   .c_str());
                    } catch (const std::exception&) {
                        ImGui::TextUnformatted("?");
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", e.card.fresh() ? TR("new") : whenText(e.card.due, day.end).c_str());
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
    } else {
        wrapped(mistake_scan_job_ ? TR("Looking for your games and their reviews…")
                : mistake_games_ == 0 && mistake_scanned_
                    ? TR("No game of yours in your databases yet: import them (the Databases page), or link your "
                         "Lichess account and bring its games.")
                    : TR("No exercise yet: your reviewed games had no mistake of yours with a clear better move. "
                         "Review your games on the Board page (Alt+G), or below."),
                true);
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    drawMistakeReviewsCard();
    ImGui::Dummy(ImVec2(0, px(4)));
    // What starts: the due, then today's new ones.
    const auto session = counts.due + counts.today;
    if (session > 0) {
        const auto label = trn("Start · {} exercise", "Start · {} exercises", session, ui::exactCount(session));
        if (primaryButton(label.c_str(), lock.empty()) || (enter && lock.empty())) startMistakes(false);
        tip(lock.empty() ? std::string(TR("On the Board page, its column becoming the exercise's; each answer graded "
                                          "by itself, each card back on its day."))
                         : lock);
        ImGui::SameLine(0, px(8));
        ImGui::TextDisabled(
            "%s", (counts.due > 0 && counts.today > 0 ? trf("Enter · the {} due, then {} new", counts.due, counts.today)
                   : counts.due > 0 ? trn("Enter · {} due", "Enter · {} due", counts.due, counts.due)
                                    : trn("Enter · {} new", "Enter · {} new", counts.today, counts.today))
                      .c_str());
    } else if (!all.empty()) {
        ImGui::TextDisabled("%s", TR("Nothing more to do today: each exercise comes back on its day."));
    }
    if (!lock.empty() && session > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::WARN));
        ImGui::TextWrapped("%s", lock.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    wrapped(
        TR("A position of your repertoire is not taken here: it is trained on the Openings page. A game removed from "
           "its database takes its exercises with it. What leaves the machine: nothing, your games, their reviews "
           "and the engine are on it."),
        true);
}

// ---------------------------------------------------------------------------
// A session

std::string App::mistakeLock() const { return trainingLock(); }

bool App::startMistakes(bool review, bool new_only) {
    if (!mistakeLock().empty()) return false;
    const auto day          = trainingDay(epochNow());
    const std::string* kind = review || mistake_kind_.empty() ? nullptr : &mistake_kind_;
    const auto list         = mistakeExercises(kind);
    const auto counts = mistakeCounts(list, day, static_cast<std::size_t>(std::max(0, trainingSettings().new_per_day)));
    const auto order  = mistakeOrder(list, day, review ? 0 : counts.today, !new_only);
    if (order.empty()) return false;
    auto run    = std::make_unique<MistakeRun>();
    run->review = review;
    run->kind   = kind ? *kind : std::string();
    for (const auto key : order)
        for (const auto* e : list)
            if (e->key == key) run->exercises.push_back(*e);
    startMistakeRun(std::move(run));
    return true;
}

void App::startMistakeRun(std::unique_ptr<MistakeRun> run) {
    if (training_) closeTraining(false);
    const bool chained = review_running_;
    if (puzzle_) closePuzzles(false);
    if (guess_) closeGuess(false);
    if (mistake_) closeMistakes(false);
    if (endgame_) closeEndgame(false);  // an endgame against the tables, a vision exercise (#82)
    if (vision_) closeVision(false);
    review_running_ = chained;
    for (std::size_t i = 0; i < run->exercises.size(); ++i) run->queue.push_back(i);
    run->total = run->exercises.size();
    run->flip  = board_.flip;
    run->tab   = board_side_tab_;
    // The game on the board goes among the recent games (Ctrl+Tab), given back after the session.
    const auto& b = board_;
    run->aside    = !b.game.moves.empty() || !b.tree.variations.empty() || !b.title.empty();
    toStudyBoard();
    openOnBoard(ReplayGame{}, 0, false, TR("Your mistakes"), false);
    board_back_.reset();
    if (board_side_tab_ == 0) board_side_tab_ = 2;  // with the answer, the tree rather than the notation's editing
    run->started = Clock::now();
    mistake_     = std::move(run);
    askMistake();
}

void App::showMistakePosition(const MistakeExercise& e, std::optional<chess::Move> then) {
    auto& b = board_;
    ReplayGame game;
    game.start_fen = e.start_fen;
    chess::Board board(e.start_fen.empty() ? std::string(chess::constants::STARTPOS) : e.start_fen);
    std::vector<chess::Move> moves;
    for (const auto& u : e.moves) moves.push_back(chess::uci::uciToMove(board, u)), board.makeMove(moves.back());
    if (then) moves.push_back(*then);
    board.setFen(game.start_fen.empty() ? std::string(chess::constants::STARTPOS) : game.start_fen);
    for (const auto m : moves) {
        ReplayMove r;
        r.san   = chess::uci::moveToSan(board, m);
        r.move  = m;
        r.mover = board.sideToMove();
        game.moves.push_back(std::move(r));
        board.makeMove(m);
    }
    game.headers = {{"White", e.white}, {"Black", e.black}, {"Event", e.event}, {"Date", e.date}};
    b.game       = std::move(game);
    b.tree       = GameTree{b.game, {}};
    b.line       = -1;
    b.ply        = static_cast<int>(b.game.moves.size());
    b.chess960   = false;
    b.selected.reset();
    b.promotion.reset();
    b.preview.reset();
    b.drag.reset();
    b.drag_over.reset();
    b.move_typed.reset();
    b.move_error.clear();
    b.comment_edit.reset();
    b.undo.clear();
    b.redo.clear();
    b.changes     = {};
    b.start_notes = {};
    b.new_game    = true;
}

void App::askMistake() {
    auto& r = *mistake_;
    if (r.queue.empty()) return finishMistakes(false);
    r.current = r.queue.front();
    r.again = std::any_of(r.outcomes.begin(), r.outcomes.end(), [&](const auto& o) { return o.exercise == r.current; });
    r.hint = r.hinted = r.retry = r.shown = r.hovered = r.next_again = false;
    r.played.clear();
    r.check = {};
    r.check_error.clear();
    r.searched    = {};
    r.phase       = MistakePhase::Asking;
    const auto& e = r.exercises[r.current];
    showMistakePosition(e);
    board_.flip = e.side() == chess::Color::BLACK;
    r.asked     = Clock::now();
}

void App::mistakeAnswer(chess::Move move) {
    auto& r = *mistake_;
    if (r.phase != MistakePhase::Asking || liveGameOn()) return;
    const auto& e = r.exercises[r.current];
    if (board_.ply != static_cast<int>(e.moves.size())) return;  // the question's position only
    const auto board = e.board();
    r.played         = chess::uci::moveToSan(board, move);
    r.played_move    = move;
    r.seconds        = since(r.asked);
    r.answered       = Clock::now();
    showMistakePosition(e, move);
    board_.flip    = e.side() == chess::Color::BLACK;
    const auto uci = chess::uci::moveToUci(move, false);
    r.check        = {};
    r.judge_used   = false;
    r.check_error.clear();
    // The review's own move, or the one played in the game: no engine needed.
    if (uci == e.best) {
        r.check.right  = true;
        r.check.judged = true;
        return mistakeJudged();
    }
    if (uci == e.played) {
        r.searched       = {};
        r.searched.score = e.after;
        r.searched.wdl   = e.after_wdl;
        r.searched.pv    = e.reply;
        r.check          = checkAnswer(e, move, r.searched);
        r.check.right    = false;  // the review marked it
        return mistakeJudged();
    }
    if (const auto ended = endedAfter(e, move)) {
        r.searched = *ended;
        r.check    = checkAnswer(e, move, *ended);
        return mistakeJudged();
    }
    // The review's engine at the review's settings: one thread, its table's size, its nodes, from an empty table.
    const EngineEntry* engine = nullptr;
    for (const auto& x : engines_)
        if (x.name == e.engine) engine = &x;
    r.judge_other = engine == nullptr;
    if (!engine) engine = analysisEngine();
    if (!engine) {
        r.check_error = TR("No engine to judge the move: add one on the Engines page.");
        r.phase       = MistakePhase::Wrong;
        return;
    }
    if (!r.judge.active() || r.judge_engine != engine->name) {
        AnalysisSettings settings;
        settings.engine  = engine->name;
        settings.threads = 1;
        settings.hash    = REVIEW_HASH_MB;
        settings.multipv = 1;
        r.judge.start(*engine, settings);
        r.judge_engine = engine->name;
    }
    auto position = e.position();
    position.moves.push_back(uci);
    r.checking = position;
    r.judge.analyse(position, e.limit(), true);
    r.phase = MistakePhase::Checking;
}

void App::mistakeJudged() {
    auto& r          = *mistake_;
    const bool right = r.check.right;
    if (!r.retry && !r.practice) mistakeGrade(right);
    r.phase = right ? MistakePhase::Right : MistakePhase::Wrong;
}

void App::mistakeGrade(bool right) {
    auto& r        = *mistake_;
    auto& e        = r.exercises[r.current];
    const auto now = epochNow();
    r.rating       = study::gradeOf(right, r.hinted, r.seconds);
    auto& record   = mistakeRecord();
    if (const auto it = record.exercises.find(e.key); it != record.exercises.end()) {
        auto& kept = it->second;
        if (kept.since == 0) kept.since = now;
        const uint64_t seed = kept.key ^ (static_cast<uint64_t>(kept.card.reps) * 0x9E3779B97F4A7C15ull);
        kept.card           = trainingScheduler().review(kept.card, r.rating, now, seed);
        e.card              = kept.card;
        e.since             = kept.since;
        r.next_review       = kept.card.due;
        // Back within the hour: asked again a few exercises later in the session.
        r.next_again = kept.card.due - now < study::TrainingSession::AGAIN_WITHIN;
        if (r.next_again) {
            const auto at = std::min<std::size_t>(r.queue.size(), 1 + study::TrainingSession::AGAIN_AFTER);
            r.queue.insert(r.queue.begin() + static_cast<std::ptrdiff_t>(at), r.current);
        }
        const auto ms    = static_cast<int64_t>(std::min(r.seconds, 600.0) * 1000);
        record.answer_ms = record.answer_ms > 0 ? (record.answer_ms * 7 + ms) / 8 : ms;
        saveMistakeRecord();
        if (!options_.scripted) appendMistakeAnswer(mistakesFolder(), e.key, now, r.rating, ms, r.played);
    }
    if (!r.again) {
        r.outcomes.push_back({r.current, right, right ? std::string() : r.played});
        ++r.done;
    }
}

void App::mistakeDontKnow() {
    auto& r = *mistake_;
    if (r.phase != MistakePhase::Asking || liveGameOn()) return;
    r.played.clear();
    r.seconds  = since(r.asked);
    r.answered = Clock::now();
    if (!r.retry && !r.practice) mistakeGrade(false);
    r.shown = true;
    board_.move_typed.reset();
    r.phase = MistakePhase::Wrong;
}

void App::mistakeRetry() {
    auto& r = *mistake_;
    if (r.phase != MistakePhase::Wrong) return;
    showMistakePosition(r.exercises[r.current]);
    r.phase = MistakePhase::Asking;
    r.retry = true;
    r.shown = r.hint = false;
    r.played.clear();
    r.check_error.clear();
    r.asked = Clock::now();
}

void App::mistakeNext() {
    auto& r = *mistake_;
    if (!r.queue.empty()) r.queue.pop_front();
    if (r.queue.empty()) return finishMistakes(false);
    askMistake();
}

void App::finishMistakes(bool stopped) {
    auto& r        = *mistake_;
    r.phase        = MistakePhase::End;
    r.stopped      = stopped;
    r.took         = since(r.started);
    r.missed_shown = -1;
    r.judge.stop();
    board_.selected.reset();
    board_.move_typed.reset();
}

void App::reviewMissedMistakes() {
    auto& r = *mistake_;
    std::deque<std::size_t> missed;
    for (const auto& o : r.outcomes)
        if (!o.right) missed.push_back(o.exercise);
    if (missed.empty()) return;
    r.queue    = std::move(missed);
    r.practice = true;  // their cards stay as they are
    r.stopped  = false;
    r.started  = Clock::now();
    askMistake();
}

void App::closeMistakes(bool to_training) {
    if (!mistake_) return;
    if (to_training) {
        review_running_ = false;
        review_chain_.clear();
        training_enter_puzzles_ = false;
    }
    const bool aside = mistake_->aside, flip = mistake_->flip;
    board_side_tab_ = mistake_->tab;
    mistake_.reset();
    forget_board_game_ = true;  // the session's position is no game to remember
    if (aside && !recent_games_.empty())
        restoreRecentGame(0);
    else
        newBoardGame();
    forget_board_game_ = false;
    board_.flip        = flip;
    board_back_.reset();
    page_ = to_training ? Page::Training : Page::Board;
}

void App::dropMistakes() {
    board_.flip     = mistake_->flip;
    board_side_tab_ = mistake_->tab;
    mistake_.reset();
    review_running_ = false;
    review_chain_.clear();
    forget_board_game_ = true;
}

void App::openMistakeGame(const MistakeExercise& e) {
    auto& s = shelf();
    for (std::size_t i = 0; i < s.entries().size(); ++i) {
        if (s.entries()[i].folder != e.folder || s.entries()[i].source) continue;
        const auto db = s.open(i);
        if (!db) break;
        const auto snapshot = db->snapshot();
        if (e.game_id >= snapshot->games() || snapshot->deleted(e.game_id)) break;
        readOnBoard(study::importedPgn(snapshot->game(e.game_id)),
                    trf("{}, game {}", s.entries()[i].name, ui::exactCount(e.game_id + 1)));
        BoardOrigin origin;
        origin.folder = e.folder;
        origin.name   = s.entries()[i].name;
        origin.id     = e.game_id;
        board_origin_ = std::move(origin);
        board_.ply    = std::min(static_cast<int>(e.moves.size()), static_cast<int>(board_.game.moves.size()));
        return;
    }
    // Its database is gone: the game up to the mistake, as the exercise keeps it.
    ReplayGame game;
    game.start_fen = e.start_fen;
    game.headers   = {{"White", e.white}, {"Black", e.black}, {"Event", e.event}, {"Date", e.date}};
    chess::Board board(e.start_fen.empty() ? std::string(chess::constants::STARTPOS) : e.start_fen);
    for (auto u : e.moves) {
        ReplayMove r;
        r.move  = chess::uci::uciToMove(board, u);
        r.san   = chess::uci::moveToSan(board, r.move);
        r.mover = board.sideToMove();
        board.makeMove(r.move);
        game.moves.push_back(std::move(r));
    }
    const auto ply = static_cast<int>(game.moves.size());
    openOnBoard(std::move(game), ply, false, gameLine(e), false);
}

// ---------------------------------------------------------------------------
// The Board page during a session

void App::mistakeKeys() {
    auto& r        = *mistake_;
    const auto& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    const auto owner = ImGui::GetID("##mistake_keys");
    for (const auto key : {ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Space, ImGuiKey_Escape, ImGuiKey_LeftArrow,
                           ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Home, ImGuiKey_End})
        ImGui::SetKeyOwner(key, owner);
    const bool plain = !io.KeyCtrl && !io.KeyAlt && !io.KeySuper;
    const bool enter =
        plain && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    switch (r.phase) {
        case MistakePhase::Asking:
            if (escape) {
                finishMistakes(true);
            } else if (!liveGameOn()) {
                if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_H)) r.hint = r.hinted = true;
                if (plain && ImGui::IsKeyPressed(ImGuiKey_Space, false))
                    mistakeDontKnow();
                else
                    boardMoveKeys();
            }
            break;
        case MistakePhase::Checking:
            if (escape) finishMistakes(true);
            break;
        case MistakePhase::Right:
        case MistakePhase::Wrong:
            if (escape) {
                finishMistakes(true);
            } else if (enter) {
                if (r.phase == MistakePhase::Right || r.shown)
                    mistakeNext();
                else
                    mistakeRetry();
            } else if (boardKey()) {
                setBoardAnalysis(!board_.analysing);  // Alt+A: the engine, now that the answer is given
            }
            break;
        case MistakePhase::End: {
            const bool missed = !r.practice && std::any_of(r.outcomes.begin(), r.outcomes.end(),
                                                           [](const auto& o) { return !o.right; });
            if (review_running_) {
                if (escape)
                    closeMistakes(true);
                else if (enter && !nextReview())
                    closeMistakes(true);
            } else if (escape) {
                closeMistakes(true);
            } else if (enter) {
                missed ? reviewMissedMistakes() : closeMistakes(true);
            }
            break;
        }
    }
}

void App::drawMistakePage() {
    ui::pageTitle(TR("Board"), guideSection());
    if (!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
        !SDL_TextInputActive(window_))
        SDL_StartTextInput(window_);
    mistakeKeys();
    if (!mistake_) return;  // left by a key
    auto& r = *mistake_;
    // A right answer goes on by itself after a moment, unless the mouse is on it.
    if (r.phase == MistakePhase::Right && trainingSettings().advance_ms > 0 && !r.hovered &&
        since(r.answered) * 1000 >= trainingSettings().advance_ms + 1500) {
        mistakeNext();
        if (!mistake_) return;
    }
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float side   = std::clamp(avail.x * 0.42f, px(340), px(620));
    const float under  = ImGui::GetTextLineHeightWithSpacing() * 4;
    const float size   = std::floor(
        std::max(px(200), std::min(avail.x - side - ImGui::GetStyle().ItemSpacing.x - px(20), avail.y - under)));

    ImGui::BeginGroup();
    const MistakeExercise* e = r.phase != MistakePhase::End || r.missed_shown >= 0 ? &r.exercises[r.current] : nullptr;
    // Over the board: the game, where the session is.
    {
        const auto start = ImGui::GetCursorPosX();
        ImGui::TextDisabled("%s", e ? gameLine(*e).c_str() : TR("Your mistakes"));
        if (r.phase != MistakePhase::End) {
            const bool answered = r.phase == MistakePhase::Right || r.phase == MistakePhase::Wrong;
            const auto at       = std::clamp<std::size_t>(r.again || answered ? r.done : r.done + 1, 1, r.total);
            auto where          = trf("{} of {}", at, r.total);
            if (r.again) where += " · " + std::string(TR("asked again"));
            ImGui::SameLine(
                std::max(ImGui::GetCursorPosX() + px(10), start + size - ImGui::CalcTextSize(where.c_str()).x));
            ImGui::TextDisabled("%s", where.c_str());
        }
    }
    drawMistakeBoard(size);
    // Under it: when, after what, what was played then.
    if (e && r.phase != MistakePhase::End) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size);
        const auto board = e->board();
        std::string after;
        if (!e->moves.empty()) {
            chess::Board before(e->start_fen.empty() ? std::string(chess::constants::STARTPOS) : e->start_fen);
            for (std::size_t i = 0; i + 1 < e->moves.size(); ++i)
                before.makeMove(chess::uci::uciToMove(before, e->moves[i]));
            after = numbered(before, chess::uci::uciToMove(before, e->moves.back()));
        }
        std::string text;
        const auto date = shortDate(e->date);
        if (!date.empty() && !after.empty())
            text = trf("{}, after {}.", date, after);
        else if (!after.empty())
            text = trf("After {}.", after);
        try {
            const auto played = chess::uci::uciToMove(board, e->played);
            auto next         = board;
            next.makeMove(played);
            const auto reply = lineText(next, e->reply, 1);
            text += (text.empty() ? "" : " ") +
                    (reply.empty() ? trf("You had played {} {}.", numbered(board, played), judgementMark(e->judgement))
                                   : trf("You had played {} {}; the review answers {}.", numbered(board, played),
                                         judgementMark(e->judgement), reply));
        } catch (const std::exception&) {
        }
        ImGui::TextDisabled("%s", text.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, px(18));
    ImGui::BeginChild("##mistake_side", ImVec2(0, 0));
    drawMistakeColumn();
    ImGui::EndChild();
}

void App::drawMistakeBoard(float size) {
    auto& b          = board_;
    auto& r          = *mistake_;
    const auto board = boardAt(b.game, b.chess960, b.ply);
    BoardDecor decor;
    decor.flipped    = b.flip;
    decor.eval_white = -1.0f;  // no bar: no engine's word on the position
    if (b.ply > 0) decor.last_move = b.game.moves[static_cast<std::size_t>(b.ply - 1)].move;
    const auto green         = markColour('G', true);
    const auto orange        = IM_COL32(236, 140, 52, 150);
    const MistakeExercise* e = r.phase != MistakePhase::End || r.missed_shown >= 0 ? &r.exercises[r.current] : nullptr;
    const bool question      = e && b.ply == static_cast<int>(e->moves.size());
    std::optional<chess::Move> best;
    if (e) {
        try {
            const auto m = chess::uci::uciToMove(e->board(), e->best);
            if (m != chess::Move::NO_MOVE) best = m;
        } catch (const std::exception&) {
        }
    }
    switch (r.phase) {
        case MistakePhase::Asking:
            if (r.hint && best) decor.tints.push_back({best->from(), markColour('B', true)});
            break;
        case MistakePhase::Checking:
            break;
        case MistakePhase::Right:
            if (decor.last_move) {
                decor.tints.push_back({decor.last_move->from(), green});
                decor.tints.push_back({shownTarget(*decor.last_move), green});
            }
            break;
        case MistakePhase::Wrong:
            if (!question && decor.last_move) {  // the answer in orange
                decor.tints.push_back({decor.last_move->from(), orange});
                decor.tints.push_back({shownTarget(*decor.last_move), orange});
            }
            if (best && r.check_error.empty()) decor.arrows.push_back({*best, markColour('G', false)});
            break;
        case MistakePhase::End:
            if (best && question) decor.arrows.push_back({*best, markColour('G', false)});
            break;
    }
    const bool moves = r.phase == MistakePhase::Asking && question && !liveGameOn();
    decor.selected   = moves ? b.selected : std::nullopt;
    if (decor.selected)
        for (const auto& m : movesFrom(board, *decor.selected)) decor.targets.push_back(shownTarget(m));
    const bool dragging = moves && b.drag && b.drag->moved && b.selected == b.drag->from;
    if (dragging) {
        decor.lifted = b.drag->from;
        if (b.drag_over && std::find(decor.targets.begin(), decor.targets.end(), *b.drag_over) != decor.targets.end())
            decor.drop = b.drag_over;
    }
    const ImVec2 top = ImGui::GetCursorScreenPos();
    if (appearance::current().board3d.on && board3d_.available()) {
        board3d_.setMarks(Board3d::Marks::Off);
        const auto input = board3d_.draw(board, size, decor);
        if (moves) boardPointer(board, input.clicked, input.hovered, input.right_click);
    } else {
        if (!appearance::current().board3d.on) board3d_.release();
        drawBoard(atlas_, board, size, decor);
        const auto hover = boardSquareAt(ImGui::GetMousePos(), top, size, decor);
        if (moves) {
            const bool left  = ImGui::IsItemClicked(ImGuiMouseButton_Left),
                       right = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            boardPointer(board, left ? hover : std::nullopt, hover, right);
            if (dragging && b.drag) drawDraggedPiece(atlas_, board.at(b.drag->from), size);
        }
    }
    drawPromotionPopup(board);
}

void App::drawMistakeColumn() {
    auto& r = *mistake_;
    const ui::TopicRegion topic(guide::topics::Mistakes);
    if (liveGameOn()) {  // fair play (#48): a live game on Lichess stops the session where it is
        drawFairPlayBanner();
        if (ImGui::Button(TR("Stop · Esc"))) finishMistakes(true);
        return;
    }
    if (r.phase == MistakePhase::End) return drawMistakeEnd();
    const auto& e    = r.exercises[r.current];
    const auto board = e.board();
    const bool black = e.side() == chess::Color::BLACK;
    const float done = r.total ? static_cast<float>(r.done) / static_cast<float>(r.total) : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    ImGui::ProgressBar(done, ImVec2(-1, px(6)), "");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, px(4)));
    chess::Move best = chess::Move::NO_MOVE;
    try {
        best = chess::uci::uciToMove(board, e.best);
    } catch (const std::exception&) {
    }
    const auto review_date = [&] {
        if (e.reviewed <= 0) return std::string();
        const auto t = static_cast<std::time_t>(e.reviewed);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char text[16];
        std::strftime(text, sizeof text, "%Y-%m-%d", &tm);
        return std::string(text);
    };
    switch (r.phase) {
        case MistakePhase::Asking:
        case MistakePhase::Checking: {
            beginTinted("##mistake_question", ui::CARD_BD);
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.2f);
            ImGui::TextUnformatted(
                r.retry ? (black ? TR("Again, Black") : TR("Again, White"))
                        : (black ? TR("Your move, Black: find better") : TR("Your move, White: find better")));
            ImGui::PopFont();
            if (r.phase == MistakePhase::Checking) {
                wrapped(
                    trf("{}: {} looks at it, as the review would…", numbered(board, r.played_move), r.judge_engine));
                if (ImGui::Button(TR("Stop · Esc"))) finishMistakes(true);
                endTinted();
                break;
            }
            wrapped(TR("The position before your move. Play on the board, or type the move and press Enter."), true);
            drawMoveField();
            if (ImGui::Button(TR("A hint · Alt+H"))) r.hint = r.hinted = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TR("H alone begins a move of the h-pawn: the hint is under Alt."));
            ImGui::SameLine();
            if (ImGui::Button(TR("I don't know · Space"))) mistakeDontKnow();
            ImGui::SameLine();
            if (ImGui::Button(TR("Stop · Esc"))) finishMistakes(true);
            if (mistake_ && (mistake_->phase == MistakePhase::Asking))
                wrapped(r.hinted ? TR("The piece to play is on the square in blue.")
                                 : TR("The hint shows the piece to play; the answer then counts as hard."),
                        true);
            endTinted();
            if (!mistake_ || mistake_->phase != MistakePhase::Asking) break;
            ImGui::Dummy(ImVec2(0, px(4)));
            wrapped(TR("Accepted: the review's best move, or any move it finds as good (within 0.3 pawn). The grade "
                       "comes from the answer, without a button, as for the repertoire."),
                    true);
            {
                const auto when = review_date();
                wrapped((when.empty() ? trf("The review of this game: {}.", e.engine)
                                      : trf("The review of this game: {}, on {}.", e.engine, when)) +
                            " " + TR("The engine, the tree and your notes are hidden during the question."),
                        true);
            }
            break;
        }
        case MistakePhase::Right: {
            beginTinted("##mistake_right", ui::GOOD);
            ui::inlineIcon(ui::Icon::Check, ui::GOOD);
            ImGui::SameLine();
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.2f);
            const auto answer = numbered(board, r.played_move);
            const int taken   = static_cast<int>(std::lround(r.seconds));
            ImGui::TextUnformatted((r.retry    ? trf("{}! Right, this time", answer)
                                    : r.hinted ? trf("{}! Right, after a hint, in {} s", answer, taken)
                                               : trf("{}! Right, at the first try, in {} s", answer, taken))
                                       .c_str());
            ImGui::PopFont();
            ImGui::PushTextWrapPos();
            if (r.judge_used) {
                wrapped(trf("As good as the review's {}: {} gives {} after it.", numbered(board, best), r.judge_engine,
                            whiteText(r.searched.score, ~e.side())));
                if (r.judge_other)
                    wrapped(trf("{}, the engine of the review, is no longer in the library.", e.engine), true);
            }
            if (const auto said = reviewSaid(e, board); !said.empty()) wrapped(said);
            {
                const auto line = lineText(board, e.line, 3);
                std::string yours;
                try {
                    const auto played = chess::uci::uciToMove(board, e.played);
                    auto next         = board;
                    next.makeMove(played);
                    const auto reply = lineText(next, e.reply, 1);
                    yours            = fmt::format("{} {}{}", numbered(board, played), judgementMark(e.judgement),
                                                   reply.empty() ? "" : " " + reply);
                } catch (const std::exception&) {
                }
                wrapped(trf("The review: {}, {}. In your game: {}, {}.", line, whiteText(e.score, e.side()), yours,
                            whiteText(e.after, ~e.side())));
            }
            std::string after;
            if (r.retry || r.practice)
                after = TR("Played again: it no longer counts.");
            else if (r.next_again)
                after = TR("Asked again in this session, then on its day.");
            else {
                const auto day = trainingDay(epochNow());
                after          = trf("Back {}.", whenText(r.next_review, day.end));
            }
            ImGui::TextDisabled("%s", after.c_str());
            ImGui::PopTextWrapPos();
            if (primaryButton(TR("Next"))) mistakeNext();
            if (mistake_ && mistake_->phase == MistakePhase::Right) {
                ImGui::SameLine(0, px(8));
                const int advance = trainingSettings().advance_ms;
                ImGui::TextDisabled(
                    "%s", advance > 0 ? trf("Enter, or on its own after {} s", (advance + 1500) / 1000.0).c_str()
                                      : TR("Enter"));
                ImGui::SameLine(0, px(10));
                if (ImGui::Button(TR("Open the game at this move"))) {
                    const auto copy = e;
                    openMistakeGame(copy);
                    endTinted();
                    return;
                }
                if (advance > 0 && !r.hovered)
                    ImGui::ProgressBar(static_cast<float>(std::min(1.0, since(r.answered) * 1000 / (advance + 1500))),
                                       ImVec2(-1, px(3)), "");
            }
            endTinted();
            if (mistake_)
                mistake_->hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
                                                         ImGuiHoveredFlags_AllowWhenBlockedByPopup);
            break;
        }
        case MistakePhase::Wrong: {
            beginTinted("##mistake_wrong", ui::WARN);
            ImGui::PushTextWrapPos();
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.2f);
            if (!r.check_error.empty())
                ImGui::TextUnformatted(trf("{} could not be judged", numbered(board, r.played_move)).c_str());
            else if (!r.played.empty())
                ImGui::TextUnformatted(trf("{} is not as good", numbered(board, r.played_move)).c_str());
            else
                ImGui::TextUnformatted(trf("The review's move: {}", numbered(board, best)).c_str());
            ImGui::PopFont();
            if (!r.check_error.empty()) {
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", r.check_error.c_str());
                wrapped(TR("Not counted: Retry, or Next."), true);
            } else {
                if (!r.played.empty()) {
                    auto next = board;
                    next.makeMove(r.played_move);
                    const auto reply = lineText(next, r.searched.pv, 1);
                    wrapped(trf("The review's move is the green arrow, yours is in orange: {}{}: {}.",
                                numbered(board, r.played_move), reply.empty() ? "" : " " + reply,
                                whiteText(r.searched.score, ~e.side())));
                    if (r.judge_used && r.judge_other)
                        wrapped(trf("Judged by {}: {}, the engine of the review, is no longer in the library.",
                                    r.judge_engine, e.engine),
                                true);
                }
                wrapped(trf("The review: {}, {}.", lineText(board, e.line, 3), whiteText(e.score, e.side())));
                if (const auto said = reviewSaid(e, board); !said.empty()) wrapped(said, true);
                if (!r.retry && !r.practice)
                    wrapped(r.next_again ? std::string(TR("It comes back a few exercises later, then tomorrow."))
                                         : trf("Back {}.", whenText(r.next_review, trainingDay(epochNow()).end)),
                            true);
            }
            ImGui::PopTextWrapPos();
            if (!r.shown) {
                if (primaryButton(TR("Retry · Enter"))) mistakeRetry();
                if (mistake_ && mistake_->phase == MistakePhase::Wrong) {
                    ImGui::SameLine();
                    if (ImGui::Button(TR("Next"))) mistakeNext();
                }
            } else if (primaryButton(TR("Next · Enter"))) {
                mistakeNext();
            }
            if (mistake_ && mistake_->phase == MistakePhase::Wrong) {
                ImGui::SameLine();
                if (ImGui::Button(TR("Open the game at this move"))) {
                    const auto copy = e;
                    openMistakeGame(copy);
                    endTinted();
                    return;
                }
            }
            endTinted();
            break;
        }
        default:
            break;
    }
    if (!mistake_ || mistake_->phase == MistakePhase::End) return;
    // With the answer, the tree and the engine come back.
    if (mistake_->phase == MistakePhase::Right || mistake_->phase == MistakePhase::Wrong) {
        ImGui::Separator();
        drawChessTabs();
    }
}

void App::drawMistakeEnd() {
    auto& r              = *mistake_;
    const auto& outcomes = r.outcomes;
    std::size_t right    = 0;
    std::vector<const MistakeRun::Outcome*> missed;
    for (const auto& o : outcomes) {
        if (o.right)
            ++right;
        else
            missed.push_back(&o);
    }
    ui::inlineIcon(ui::Icon::Check, ui::GOOD);
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.35f);
    if (outcomes.empty())
        ImGui::TextUnformatted(r.stopped ? TR("Stopped before the first answer") : TR("No exercise to do now"));
    else
        ImGui::TextUnformatted(trn("{} exercise in {}", "{} exercises in {}", outcomes.size(),
                                   ui::exactCount(outcomes.size()), ui::formatDuration(r.took))
                                   .c_str());
    ImGui::PopFont();
    if (!outcomes.empty()) {
        ImGui::Dummy(ImVec2(0, px(4)));
        bigCount(ui::exactCount(right), TR("right at the first try"));
        ImGui::SameLine(0, px(30));
        bigCount(ui::exactCount(missed.size()), TR("missed, back tomorrow"));
    }
    if (!missed.empty()) {
        ImGui::Dummy(ImVec2(0, px(6)));
        ImGui::TextUnformatted(TR("The missed"));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("· a click shows the position and the review's move"));
        ui::beginCard("##mistakes_missed");
        for (const auto* o : missed) {
            const auto& e    = r.exercises[o->exercise];
            const auto board = e.board();
            std::string said;
            try {
                const auto best   = numbered(board, chess::uci::uciToMove(board, e.best));
                std::string yours = localSan(o->played);
                if (const auto m = chess::uci::parseSan(board, o->played); m != chess::Move::NO_MOVE)
                    yours = numbered(board, m);
                said = o->played.empty() ? trf("I don't know · the review: {}", best)
                                         : trf("you: {} · the review: {}", yours, best);
            } catch (const std::exception&) {
            }
            ImGui::PushID(static_cast<int>(o->exercise));
            if (ImGui::Selectable(fmt::format("{} – {}", e.white, e.black).c_str(),
                                  r.missed_shown == static_cast<int>(o->exercise))) {
                r.missed_shown = static_cast<int>(o->exercise);
                r.current      = o->exercise;
                showMistakePosition(e);
                board_.flip = e.side() == chess::Color::BLACK;
            }
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(10),
                                     ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(said.c_str()).x));
            ImGui::TextDisabled("%s", said.c_str());
            ImGui::PopID();
        }
        ui::endCard();
    }
    ImGui::Dummy(ImVec2(0, px(6)));
    if (review_running_) {  // the day's review: the next kind, or back to the Training page
        drawReviewNext(false);
        return;
    }
    if (!missed.empty() && !r.practice) {
        if (primaryButton(trn("Review {} now", "Review the {} now", missed.size(), missed.size()).c_str()))
            return reviewMissedMistakes();
        ImGui::SameLine();
        if (ImGui::Button(TR("Back to the Training page"))) return closeMistakes(true);
        ImGui::SameLine(0, px(8));
        ImGui::TextDisabled("%s", TR("Enter: review them · Esc: back"));
    } else {
        if (primaryButton(TR("Back to the Training page"))) return closeMistakes(true);
        ImGui::SameLine(0, px(8));
        ImGui::TextDisabled("%s", TR("Enter"));
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    wrapped(
        TR("A session stopped midway keeps its answers. Each exercise comes back on its day (FSRS): a missed one in "
           "the session and tomorrow, a right one further apart."),
        true);
}

}  // namespace fcg
