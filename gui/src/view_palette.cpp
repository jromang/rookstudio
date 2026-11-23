// The command palette (Ctrl+K): a page, a panel, an engine, a book, a tournament or an action, found by typing a few of
// its letters. And F1: the guide at the section of the page shown, as the `?` beside the page titles.

#include <algorithm>
#include <string_view>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include "app.hpp"
#include "assistant_service.hpp"
#include "board_text.hpp"
#include "i18n.hpp"
#include "layout.hpp"
#include "lichess_months.hpp"
#include "machines.hpp"
#include "palette.hpp"
#include "participants.hpp"
#include "review_stats.hpp"
#include "setup_checks.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// What a label shows, without its "##" or "###" id.
std::string shown(const char* label) {
    const std::string_view text(label);
    return std::string(text.substr(0, text.find("##")));
}

}  // namespace

const char* App::guideSection() const {
    using namespace guide::topics;
    switch (page_) {
        case Page::Home:
            return TournamentsPage;
        case Page::ChessHome:
            return Home;
        case Page::NewMatch:
            return NewTournament;
        case Page::Tournament:
            if (!run_->tournament()) return Running;
            switch (shown_tab_) {
                case Tab::Overview:
                    return Overview;
                case Tab::Standings:
                    return Standings;
                case Tab::Bracket:
                    return Schedule;
                case Tab::Schedule:
                    return Changing;
                case Tab::Live:
                    return Live;
                case Tab::Games:
                    return Games;
                case Tab::Openings:
                    return OpeningsTab;
                case Tab::Broadcast:
                    return Broadcast;
            }
            return Running;
        case Page::Engines:
            return Engines;
        case Page::Books:
            return Books;
        case Page::Board:
            // A game against an engine (#83).
            if (engine_game_) return guide::topics::EngineGame;
            if (training_) return Training;  // a session of the repertoire's training (#76)
            if (puzzle_) return Puzzles;     // a series of puzzles (#80)
            if (mistake_) return Mistakes;   // a session of exercises, a game guessed (#81)
            if (guess_) return GuessMoves;
            if (endgame_) return Endgames;  // an endgame against the tables, a vision exercise (#82)
            if (vision_) return Vision;
            if (!chessBoard()) return Board;
            switch (board_side_tab_) {
                case 0:
                    return Notation;
                case 1:
                    return SideBySide;
                case 2:
                    return TreeTab;
                case 3:
                    return ReferenceGames;
                case 4:
                    return Questions;
                default:
                    return StudyBoard;
            }
        case Page::Databases:
            return Databases;
        case Page::Openings:
            return openings_theory_ ? TheoryStudies : OpeningsPage;
        case Page::Lichess:
            return LichessPage;
        case Page::Training:
            return training_tab_ == 1   ? Puzzles
                   : training_tab_ == 2 ? Mistakes
                   : training_tab_ == 3 ? GuessMoves
                   : training_tab_ == 4 ? Endgames
                   : training_tab_ == 5 ? Vision
                                        : TrainingPage;
        case Page::Play:
            return PlayEngine;
        case Page::Count:  // the views' sheet
            return Views;
        case Page::Suites:
            return Suites;
        case Page::Ratings:
            return Ratings;
        case Page::Log:
            return Report;
        case Page::Settings:
            break;
    }
    return Window;
}

void App::toggleGuide() {
    // Over the panel itself, F1 closes it; else it opens at what is under the mouse, or at the page.
    if (guide_.isOpen() && guide_.hovered()) {
        guide_.close();
        return;
    }
    const char* topic = ui::hoveredTopic();
    guide_.toggle(topic ? topic : guideSection());
}

// The actions possible now first, then the pages, the panels of the tournament and the tabs of the ratings, the
// tournaments, the engines and the books.
std::vector<App::PaletteEntry> App::paletteEntries() {
    std::vector<PaletteEntry> out;
    const auto add = [&](std::string label, std::string where, std::function<void()> run) {
        out.push_back({std::move(label), std::move(where), std::move(run)});
    };
    const auto* t          = run_->tournament();
    const std::string name = t ? t->spec().name : std::string();

    // Actions, as their buttons allow them.
    if (run_->running()) {
        if (run_->paused()) {
            if (!run_->stopping() && !run_->stoppingWhenIdle())
                add(TR("Resume"), name, [this] { setRunPaused(false); });
        } else if (!run_->stopping()) {
            add(TR("Pause"), name, [this] { setRunPaused(true); });
        }
        if (!run_->stopping()) {
            add(TR("Finish the running games, then stop"), name, [this] { stopRun(false); });
            add(TR("Stop now (running games are lost)"), name, [this] { stopRun(true); });
        }
    } else {
        if (unfinished_) add(TR("Resume"), unfinished_->spec.name, [this] { resumeRun(); });
        if (!queue_.empty()) add(TR("Start now"), queue_.front().label, [this] { startFromQueue(); });
    }
    // Pause all and Resume all, as the home page offers them.
    if (allPaused()) {
        add(TR("Resume all"), TR("Tournaments"), [this] { resumeAll(); });
    } else if (canPauseAll()) {
        add(TR("Pause all"), TR("Tournaments"), [this] { pauseAll(); });
    }
    // The last matches finished, relaunched with another engine A from their row of the home page.
    {
        const auto busy = run_->running() && t ? t->dir() : std::filesystem::path();
        for (const auto* s : home::lastFinished(tournaments_, home::LAST_FINISHED, busy)) {
            if (s->spec.format != tour::Format::Match || s->spec.participants.size() < 2) continue;
            add(trf("Relaunch {} with…", s->spec.name), TR("Last finished"), [this, dir = s->dir] {
                page_             = Page::Home;
                relaunch_request_ = dir;
            });
        }
    }
    // Clone: the tournament shown, and the last finished, as their headers and rows offer it.
    {
        std::vector<std::pair<std::filesystem::path, std::string>> sources;
        if (t) sources.emplace_back(t->dir(), name);
        const auto busy = run_->running() && t ? t->dir() : std::filesystem::path();
        for (const auto* s : home::lastFinished(tournaments_, home::LAST_FINISHED, busy)) {
            if (!t || s->dir != t->dir()) sources.emplace_back(s->dir, s->spec.name);
        }
        for (const auto& source : sources) {
            // Its date beside it: a tournament cloned is often named as the one it came from.
            if (canClone(source.first)) {
                add(trf("Clone {}", source.second), fmt::format("{} · {}", TR("Tournaments"), folderDate(source.first)),
                    [this, dir = source.first] { cloneTournament(dir); });
            }
        }
    }
    // The engines that cannot start, as their card offers.
    if (t) {
        const bool editable = t->spec().format == tour::Format::RoundRobin ||
                              t->spec().format == tour::Format::Gauntlet || t->spec().format == tour::Format::TeamMatch;
        for (const auto& e : t->startFailures().engines()) {
            const auto* p = t->spec().participant(e.participant);
            if (!p || !p->active) continue;
            if (run_->running() && !run_->stopping() && !e.trying) {
                add(trf("Try again to start {}", e.name), name,
                    [this, id = e.participant] { run_->tryEngineAgain(id); });
            }
            if (editable) {
                add(trf("Withdraw {}…", e.name), name,
                    [this, id = e.participant] { openParticipantEdit(tour::Edit::Kind::Withdraw, id); });
                add(trf("Replace {}…", e.name), name,
                    [this, id = e.participant] { openParticipantEdit(tour::Edit::Kind::ReplaceParticipant, id); });
            }
        }
    }
    // The other tournaments open, a tab each: shown, or paused and resumed from here.
    for (std::size_t i = 0; i < runs_.size(); ++i) {
        if (i == shown_run_) continue;
        const auto* other = runs_[i].run->tournament();
        if (!other) continue;
        const auto& other_name = other->spec().name;
        add(trf("Show {}", other_name), TR("Tournaments"), [this, i] {
            showRun(i);
            page_ = Page::Tournament;
        });
        auto& r = *runs_[i].run;
        if (r.running() && !r.stopping()) {
            if (r.paused()) {
                add(TR("Resume"), other_name, [this, i] { runs_[i].run->setPaused(false); });
            } else {
                add(TR("Pause"), other_name, [this, i] { runs_[i].run->setPaused(true); });
            }
        }
    }
    const bool ready = setupChosen() && !setupBadSprt();
    if (ready && !startPending("start")) add(TR("Start"), TR("New tournament"), [this] { startRun(); });
    if (ready) add(TR("Add to the queue"), TR("New tournament"), [this] { enqueue(); });
    // The fixes the setup summary offers (Lower Threads, Lower to N pieces, Locate…), named after their engine. The
    // engines open from their own entries, below.
    {
        std::vector<Notice> notices = programNotices(setup_, engines_);
        if (auto book = bookNotice(setup_)) notices.push_back(std::move(*book));
        for (auto& n : setupNotices()) notices.push_back(std::move(n));
        std::vector<Fix> seen;
        for (const auto& n : notices) {
            for (const auto& f : n.fixes) {
                const bool generic =
                    f.kind == Fix::Kind::OpenEngine || f.kind == Fix::Kind::OpenLog || f.kind == Fix::Kind::OpenFolder;
                const bool setup = f.kind != Fix::Kind::LocateProgram && f.kind != Fix::Kind::LocateDir &&
                                   f.kind != Fix::Kind::LowerEngineThreads;
                if (generic || (!setup && engineInUse(f.engine)) ||
                    std::find(seen.begin(), seen.end(), f) != seen.end())
                    continue;
                seen.push_back(f);
                add(f.engine.empty() ? f.label : fmt::format("{}: {}", f.engine, f.label), TR("New tournament"),
                    [this, f] { applyFix(f); });
            }
        }
    }
    if (t && !games_.empty()) {
        add(TR("Export for a rating list…"), name, [this] {
            page_        = Page::Tournament;
            tab_request_ = Tab::Games;
            openSubmissionExport();
        });
    }
    // The games of the openings flagged unbalanced as they ended.
    if (t && t->spec().unbalanced_cp > 0 &&
        std::any_of(games_.begin(), games_.end(), [](const GameRecord& g) { return g.unbalanced != 0; })) {
        add(TR("Show the games from unbalanced openings"), TR("Games"), [this] { showUnbalancedGames(); });
        add(TR("Show the unbalanced openings"), TR("Openings"), [this] {
            page_                = Page::Tournament;
            tab_request_         = Tab::Openings;
            opening_show_        = UNBALANCED_OPENINGS;
            opening_order_dirty_ = true;
        });
    }
    // The big screen: the game followed, filling the window.
    if (t && run_->observer()) {
        if (big_.on) {
            add(TR("Leave the big screen"), TR("Live"), [this] { leaveBigScreen(); });
        } else {
            add(TR("Big screen"), TR("Live"), [this] { startBigScreen(live_slot_); });
        }
    }
    // The game shown in the Games tab, and the game followed in the Live tab: their result set by hand.
    if (t && selected_game_ >= 0 && selected_game_ < static_cast<int>(games_.size())) {
        const auto id = games_[static_cast<std::size_t>(selected_game_)].game_id;
        add(trf("Set the result of game {}…", id), TR("Games"), [this, id] {
            page_           = Page::Tournament;
            tab_request_    = Tab::Games;
            open_game_menu_ = id;
        });
    }
    if (t && run_->running() && live_slot_ >= 0 && live_slot_ < static_cast<int>(slot_views_.size()) &&
        slot_views_[static_cast<std::size_t>(live_slot_)].state.active) {
        const auto& st = slot_views_[static_cast<std::size_t>(live_slot_)].state;
        add(trf("End game {} with a result…", st.game_id), TR("Live"), [this, slot = live_slot_] {
            page_          = Page::Tournament;
            tab_request_   = Tab::Live;
            open_end_menu_ = slot + 1;
        });
    }
    if (t && !isMatch()) {
        const auto copy = [&](const char* how, ui::CopyAs as) {
            add(fmt::format("{}: {}", TR("Copy"), how), TR("Standings"), [this, as] { copyStandings(as); });
        };
        copy(TR("As text"), ui::CopyAs::Text);
        copy(TR("As BBCode, for a forum"), ui::CopyAs::Bbcode);
        if (t->spec().format == tour::Format::Gauntlet) {
            copy(TR("Gauntlet table as text"), ui::CopyAs::GauntletText);
            copy(TR("Gauntlet table as BBCode, for a forum"), ui::CopyAs::GauntletBbcode);
        }
    }
    const auto import       = fmt::format("{} · {}", TR("Engines"), TR("Import…"));
    const auto engines_page = [this](std::function<void()> then) {
        return [this, then = std::move(then)] {
            page_ = Page::Engines;
            then();
        };
    };
    add(TR("An engine list…"), import, engines_page([this] { askEngineList(); }));
    add(TR("Every engine of a folder…"), import,
        engines_page([this] { dialogs_.openFolder(Dialogs::Purpose::ImportEngineFolder); }));
    if (!engineFolder().empty())
        add(TR("Refresh: the new engines of the folder"), import, engines_page([this] { refreshEngineFolder(); }));
    add(TR("A rating list, for the reference Elo…"), import, engines_page([this] { openRatingImport(); }));
    // The engines of a machine, played from this computer.
    for (const auto& m : machines_) {
        const auto name = m.name.empty() ? m.host : m.name;
        add(trf("Add an engine from {}…", name), TR("Engines"), engines_page([this, id = m.id] {
                openRemotePicker(RemotePicker::Purpose::AddEngine, id);
            }));
        add(trf("Every engine of a folder on {}…", name), import, engines_page([this, id = m.id] {
                openRemotePicker(RemotePicker::Purpose::ImportFolder, id);
            }));
    }
    if (const auto unchecked = uncheckedEngines(); !unchecked.empty()) {
        add(trf("Check those not checked ({})", unchecked.size()), TR("Engines"),
            engines_page([this, unchecked] { queueChecks(unchecked); }));
    }
    // The engines whose rebuilt program's new read found changes: their card, the choices for all, the dismissal.
    for (const auto& e : engines_) {
        if (!e.changes) continue;
        const auto& n = e.name;
        add(trf("What changed: {}", n), TR("Engines"), [this, n] { showEngineChanges(n); });
        if (choicesLeft(*e.changes) > 0) {
            add(trf("Keep my values: {}", n), TR("Engines"), [this, n] { settleEngineChanges(n, ValueChoice::Keep); });
            add(trf("Use the new defaults: {}", n), TR("Engines"),
                [this, n] { settleEngineChanges(n, ValueChoice::NewDefault); });
        }
        add(trf("Dismiss what changed: {}", n), TR("Engines"), [this, n] { settleEngineChanges(n, std::nullopt); });
    }
    if (!resumed_programs_.empty() && run_->running())
        add(TR("Continue with the changed programs"), name, [this] { resumed_programs_.clear(); });
    // The console of the engine shown, or the one open.
    if (page_ == Page::Engines && selected_engine_ >= 0 && selected_engine_ < static_cast<int>(engines_.size())) {
        add(TR("Console…"), engines_[static_cast<std::size_t>(selected_engine_)].name,
            [this] { openEngineConsole(selected_engine_); });
    }
    if (engine_console_) add(TR("Close the console"), engine_console_->engine, [this] { closeEngineConsole(); });

    // Pages, the panels of the tournament open, and the tabs of the ratings.
    const auto page = [this](Page p) {
        return [this, p] {
            if (p == Page::Home) tournaments_dirty_ = true;
            if (p == Page::Ratings && page_ != Page::Ratings) game_db_dirty_ = true;
            page_ = p;
        };
    };
    add(TR("Tournaments"), "", page(Page::Home));
    add(TR("New tournament"), "", page(Page::NewMatch));
    // The profiles of each section, applied to the setup, and the templates, loaded.
    {
        for (const auto section : SETUP_SECTIONS) {
            for (const auto& p : profiles_.list(section)) {
                add(p.label(), fmt::format("{} · {}", TR("Profile"), sectionTitle(section)),
                    [this, section, name = p.name] {
                        page_ = Page::NewMatch;
                        if (const auto* profile = profiles_.find(section, name)) applySetupProfile(section, *profile);
                    });
            }
        }
        // Each row of the setup, opened in place.
        const auto row = [&](const char* title, SetupRow r) {
            add(title, TR("New tournament"), [this, r] {
                page_          = Page::NewMatch;
                setup_open_[r] = true;
            });
        };
        for (const auto section : SETUP_SECTIONS) row(sectionTitle(section), static_cast<SetupRow>(section));
        row(TR("Concurrency"), RowConcurrency);
        row(TR("PGN and misc"), RowPgn);
        // A team match: the participants selected to a team, or the halves of the list again.
        if (setup_.format == TournamentFormat::TeamMatch && !setup_.participants.empty()) {
            std::vector<int> picked;
            for (int i = 0; i < static_cast<int>(setup_.participants.size()); ++i) {
                if (participants_selected_.Contains(static_cast<ImGuiID>(i))) picked.push_back(i);
            }
            for (int team = 0; team < 2 && !picked.empty(); ++team) {
                add(team == 0 ? TR("Put the participants selected in team A.")
                              : TR("Put the participants selected in team B."),
                    TR("Team match"), [this, picked, team] {
                        page_ = Page::NewMatch;
                        setTeam(setup_, picked, team);
                        saveSetupSoon();
                    });
            }
            add(TR("The teams made again from the list: its upper half against its lower half."), TR("Team match"),
                [this] {
                    page_ = Page::NewMatch;
                    splitTeams(setup_);
                    saveSetupSoon();
                });
        }
        add(TR("Save the setup as a template…"), TR("New tournament"), [this] {
            page_               = Page::NewMatch;
            open_save_template_ = true;
        });
        listTemplates();
        for (const auto& name : template_names_) {
            add(name, TR("Template"), [this, name] {
                page_ = Page::NewMatch;
                loadTemplate(name);
            });
        }
    }
    if (t) {
        // Every panel of the workspace: shown (docked again if it was closed) and brought to the front. Then the
        // layouts, ready-made and saved.
        std::vector<std::string> saved;
        for (const auto& l : layouts_) saved.push_back(l.name);
        for (const auto& c : layout::commands(panelsAvailable(), saved)) {
            if (c.kind == layout::Command::Kind::Show) {
                add(c.label, name, [this, panel = c.target] {
                    page_ = Page::Tournament;
                    showPanel(panel);
                });
            } else if (c.kind == layout::Command::Kind::Preset) {
                add(c.label, TR("Layout"), [this, key = c.target] { applyLayout(key); });
            } else {
                // A layout of the user's, even if named as the key of a ready-made one.
                add(c.label, TR("Layout"), [this, name = c.target] {
                    for (const auto& l : layouts_)
                        if (l.name == name) applyLayout(l);
                });
            }
        }
        add(TR("Save the layout as…"), TR("Layout"), [this] {
            page_ = Page::Tournament;
            layout_name_.clear();
            open_save_layout_ = true;
        });
        add(TR("Default layout"), TR("Layout"), [this] { applyLayout(layout::preset(layout::Preset::Tabs)); });
    }
    add(TR("Engines"), "", page(Page::Engines));
    add(TR("Books"), "", page(Page::Books));
    for (const bool symmetrical : {false, true}) {
        add(fmt::format("{}: {}", TR("Generate Chess324 book"),
                        symmetrical ? TR("Only the 18 symmetrical ones…") : TR("All 324 positions…")),
            TR("Books"), [this, symmetrical] {
                page_ = Page::Books;
                askChess324(symmetrical);
            });
    }
    add(TR("Build a Polyglot book…"), TR("Books"), [this] {
        page_ = Page::Books;
        openBookBuild();
    });
    if (book_build_.busy()) {
        add(TR("Cancel the Polyglot book being built"), std::filesystem::path(book_build_.bin()).filename().string(),
            [this] { book_build_.cancel(); });
    }
    add(TR("Board"), "", page(Page::Board));
    add(board_.analysing ? TR("Stop the analysis") : TR("Analyse"), TR("Board"), [this] {
        page_ = Page::Board;
        setBoardAnalysis(!board_.analysing);
    });
    if (board_.analyser->active() || board_.threat.active() ||
        std::any_of(board_.side.begin(), board_.side.end(), [](const auto& a) { return a && a->active(); }))
        add(chessBoard() && analysisEngines(board_.settings).size() > 1 ? TR("Unload the analysis's engines")
                                                                        : TR("Unload the engine"),
            TR("Board"), [this] { unloadBoardEngines(); });
    // Several engines side by side in the study Board's analysis (#78): each gesture of its tab.
    if (chessBoard()) {
        loadBoardSettings();
        const auto list   = analysisEngines(board_.settings);
        const auto at_tab = [this] {
            toStudyBoard();
            board_side_tab_ = 1;
        };
        const auto index = [this](const std::string& name) -> std::optional<std::size_t> {
            const auto now = analysisEngines(board_.settings);
            for (std::size_t i = 0; i < now.size(); ++i)
                if (now[i].engine == name) return i;
            return std::nullopt;
        };
        if (list.size() < MAX_SIDE_ENGINES) {
            add(TR("Add an engine to the analysis…"), TR("Board"), [this, at_tab] {
                at_tab();
                side_menu_open_ = true;
            });
            for (const auto& e : engines_) {
                if (e.inactive ||
                    std::any_of(list.begin(), list.end(), [&](const auto& s) { return s.engine == e.name; }))
                    continue;
                add(trf("Analyse with {} too", e.name), TR("Board"), [this, at_tab, name = e.name] {
                    at_tab();
                    (void)addSideEngine(name);
                });
            }
        }
        for (std::size_t k = 0; k < list.size(); ++k) {
            const auto& name = list[k].engine;
            if (k > 0) {
                add(trf("Make {} first in the analysis", name), TR("Board"), [this, at_tab, index, name] {
                    at_tab();
                    if (const auto i = index(name)) moveSideEngine(*i, 0);
                });
            }
            if (list.size() > 1) {
                add(trf("Remove {} from the analysis", name), TR("Board"), [this, index, name] {
                    if (const auto i = index(name)) removeSideEngine(*i);
                });
            }
            add(trf("{} in the analysis: lines, threads, hash…", name), TR("Board"), [this, at_tab, index, name] {
                at_tab();
                if (const auto i = index(name)) side_engine_menu_ = static_cast<int>(*i);
            });
        }
        add(TR("The analysis's threads…"), TR("Board"), [this, at_tab] {
            at_tab();
            side_threads_open_ = true;
        });
    }
    add(TR("Copy FEN"), TR("Board"), [this] { ImGui::SetClipboardText(boardFen().c_str()); });
    add(TR("Open a PGN…"), TR("Board"), [this] {
        page_ = Page::Board;
        dialogs_.openFile(Dialogs::Purpose::BoardPgn, {{"PGN", "pgn"}}, storage_.runsDir().string());
    });
    add(TR("Paste"), TR("Board"), [this] {
        page_ = Page::Board;
        pasteOnBoard();
    });
    add(TR("Save the game as PGN…"), TR("Board"), [this] {
        page_ = Page::Board;
        saveBoardPgn();
    });
    add(TR("Edit the position"), TR("Board"), [this] {
        page_ = Page::Board;
        openPositionEditor();
    });
    if (board_.match && board_.match_state.running) {
        add(TR("Stop the game"), TR("Board"), [this] { board_.match->stop(); });
    } else {
        add(TR("Play a game…"), TR("Board"), [this] {
            page_            = Page::Board;
            board_.play_open = true;
        });
    }
    if (board_.reviewing()) {
        add(TR("Cancel the analysis of the game"), TR("Board"), [this] { cancelGameReview(); });
    } else if (!board_.game.moves.empty()) {
        add(TR("Analyse the game"), TR("Board"), [this] {
            page_ = Page::Board;
            startGameReview();
        });
    }
    add(TR("Copy PGN"), TR("Board"),
        [this] { ImGui::SetClipboardText(gamePgn(board_.tree, board_.chess960).c_str()); });
    add(board_.threat_on ? TR("Hide the threat") : TR("Show the threat"), TR("Board"), [this] {
        page_ = Page::Board;
        setBoardThreat(!board_.threat_on);
    });
    if (!board_.reviews.empty()) {
        add(TR("Next marked move"), TR("Board"), [this] {
            page_ = Page::Board;
            if (const int to = judgedPly(1); to >= 0) showBoardLine(-1, to);
        });
        add(TR("Previous marked move"), TR("Board"), [this] {
            page_ = Page::Board;
            if (const int to = judgedPly(-1); to >= 0) showBoardLine(-1, to);
        });
        add(TR("Copy PGN with the analysis"), TR("Board"), [this] {
            ImGui::SetClipboardText(
                pgnWithAnalysis(board_.tree, board_.chess960, board_.reviews.front(), board_.judged).c_str());
        });
    }
    add(TR("The game's review"), TR("Board"), [this] {
        page_           = Page::Board;
        board_.open_tab = 3;
    });
    // What the study Board's notation shows beside a tournament engine's moves (#87, mockup M12, board 8).
    if (chessBoard() && hasPlayerEvaluations(board_.tree.main)) {
        const char* beside[] = {TR("Beside the moves: the engines' evaluation and depth"),
                                TR("Beside the moves: their evaluation, depth and time"),
                                TR("Beside the moves: nothing")};
        for (int i = 0; i < 3; ++i) {
            if (static_cast<int>(engineTags()) == i) continue;
            add(beside[i], TR("Notation"), [this, i] {
                board_.settings.engine_tags = i;
                saveBoardSettings();
            });
        }
    }
    add(appearance::current().figurines ? TR("Letters in the notation") : TR("Figurines in the notation"), TR("Board"),
        [this] {
            auto s      = appearance::current();
            s.figurines = !s.figurines;
            applyAppearance(s);
        });
    if (board_.line >= 0) {
        add(TR("Back to the game"), TR("Board"), [this] { showBoardLine(-1, boardMainPly()); });
    }
    if (live_slot_ >= 0 && live_slot_ < static_cast<int>(slot_views_.size()) &&
        slot_views_[static_cast<std::size_t>(live_slot_)].state.has_game) {
        add(TR("Analyse the game followed"), name, [this] { analyseLiveGame(live_slot_); });
    }
    if (replay_) add(TR("Analyse the game shown"), name, [this] { analyseReplay(); });
    add(TR("Test suites"), "", page(Page::Suites));
    add(TR("Ratings"), "", page(Page::Ratings));
    // The views, and the pages of every view: a page of another view takes it there (followPage).
    for (int i = 0; i < static_cast<int>(views_.size()); ++i)
        add(trf("View: {}", views_[static_cast<std::size_t>(i)].name), fmt::format("Ctrl+{}", i + 1),
            [this, i] { switchView(i); });
    add(TR("Edit the views…"), TR("Settings"), [this] { openViewsSheet(view_); });
    // Each with the view it opens in: the view shown when it holds the page, else the first that does.
    const auto where = [this](Page p) -> std::string {
        const auto root = rootOf(p);
        if (!root || view().holds(*root)) return view().name;
        for (const auto& v : views_)
            if (v.holds(*root)) return v.name;
        return TR("in no view");
    };
    add(TR("Home"), where(Page::ChessHome), page(Page::ChessHome));
    add(TR("Databases"), where(Page::Databases), page(Page::Databases));
    add(TR("Openings: repertoires"), where(Page::Openings), [this, go = page(Page::Openings)] {
        go();
        openings_theory_ = false;
    });
    // The theory studies (#77): their part of the Openings page, a PGN added, what they say at the board's position.
    add(TR("Openings: theory studies"), where(Page::Openings), [this, go = page(Page::Openings)] {
        go();
        openings_theory_ = true;
    });
    add(TR("Add a PGN to the theory…"), TR("Openings"), [this, go = page(Page::Openings)] {
        go();
        openings_theory_ = true;
        openTheoryDialog();
    });
    if (chessBoard() && !liveGameOn())
        if (const auto n = boardTheory().found.size(); n > 0)
            add(trn("What my studies say here · {} note", "What my studies say here · {} notes", n, n), "Alt+3",
                [this] {
                    toStudyBoard();
                    board_side_tab_ = 2;
                });
    // The repertoires' training (#76): a session of each, its new positions alone, its settings.
    for (std::size_t i = 0; i < repertoires().entries().size(); ++i) {
        const auto& counts = trainingSummary(i).counts;
        if (counts.cards == 0) continue;
        const auto& name = repertoires().entries()[i].name;
        add(trf("Train the repertoire “{}”", name),
            counts.due > 0
                ? trn("{} position to review", "{} positions to review", counts.due, ui::exactCount(counts.due))
                : std::string(TR("Openings")),
            [this, i] { startTraining(i, TrainingRun::Kind::Due); });
        if (counts.fresh > 0)
            add(trf("Train only the new positions of “{}”", name), TR("Openings"),
                [this, i] { startTraining(i, TrainingRun::Kind::New); });
    }
    add(trainingSettings().advance_ms > 0 ? TR("Training: the next position on Enter only")
                                          : TR("Training: the next position on its own after a right answer"),
        TR("Openings"), [this] {
            auto& s      = trainingSettings();
            s.advance_ms = s.advance_ms > 0 ? 0 : 1000;
            if (training_) training_->settings.advance_ms = s.advance_ms;
            saveTrainingSettings();
        });
    add(TR("Training: new positions a day…"), TR("Openings"), [this] {
        page_                = Page::Openings;
        training_limit_menu_ = true;
    });
    // Playing against an engine (#83): the Play page; a game's commands while it lasts, a rematch after it.
    add(TR("Play against an engine"), where(Page::Play), page(Page::Play));
    if (engine_game_ && !engine_game_->over && !engine_game_->waiting && board_.match && board_.match_state.running) {
        add(TR("Offer a draw"), TR("Play"), [this] { board_.match->offerDraw(); });
        add(TR("Resign the game…"), TR("Play"), [this] {
            toStudyBoard();
            engineGameResign();  // armed: Enter, or a second click, resigns
        });
        if (engine_game_->kept.settings.takebacks)
            add(TR("Take a move back"), "Ctrl+Z", [this] { engineGameTakeBack(); });
        if (engine_game_->kept.settings.hints) add(TR("A hint"), "Alt+H", [this] { engineGameHint(); });
        if (!engine_game_->aids)
            add(TR("Show the aids, the game marked “with aids”"), TR("Play"), [this] { showEngineGameAids(); });
    }
    if (engine_game_ && engine_game_->waiting)
        add(TR("Resume the game against the engine"), TR("Play"), [this] { resumeEngineGame(); });
    if (engine_game_ && engine_game_->over)
        add(TR("Rematch, colours reversed"), TR("Play"), [this] {
            const bool white = engine_game_->kept.white;
            closeEngineGame();
            startEngineGame(white ? chess::Color::BLACK : chess::Color::WHITE);
        });
    // Playing on Lichess (#84): the page's part, a seek with the last clock, a challenge; the challenges received; the
    // game's commands while it lasts.
    const auto lichess_part = [this] {
        play_part_          = 1;
        playSettings().part = 1;
        savePlaySettings();
        page_ = Page::Play;
    };
    add(TR("Play on Lichess"), where(Page::Play), lichess_part);
    if (onlineScope()) {
        const auto clock = onlineSettings().seekClock();
        if (!online_seek_ && lichess::seekAllowed(clock) && !liveGameOn())
            add(trf("Seek a game on Lichess · {}", lichess::clockText(clock)), TR("Play"), [this, lichess_part] {
                lichess_part();
                startSeek();
            });
        if (online_seek_) add(TR("Call the seek back"), TR("Play"), [this] { cancelSeek(); });
        add(TR("Challenge a player on Lichess…"), TR("Play"), [this, lichess_part] {
            lichess_part();
            online_user_focus_ = true;
        });
        if (online_challenge_) add(TR("Call the challenge back"), TR("Play"), [this] { cancelChallenge(); });
        for (const auto& c : online_incoming_) {
            const auto id = c.id;
            if (c.board && !liveGameOn())
                add(trf("Accept the challenge of {}", c.challenger), TR("Play"),
                    [this, id] { answerChallenge(id, true); });
            add(trf("Decline the challenge of {}", c.challenger), TR("Play"),
                [this, id] { answerChallenge(id, false, "generic"); });
        }
    } else if (lichess_account_) {
        add(TR("Allow RookStudio to play your Lichess games…"), TR("Play"), [this, lichess_part] {
            lichess_part();
            askOnlineScope();
        });
    }
    if (online_game_ && !online_game_->ended && !online_game_->state.over()) {
        add(TR("Back to the game on Lichess"), TR("Play"), [this] { backToOnlineGame(); });
        add(TR("Offer a draw on Lichess"), TR("Play"), [this] { onlineAction("draw/yes"); });
        add(TR("Resign the game on Lichess…"), TR("Play"), [this] {
            backToOnlineGame();
            onlineResign();  // armed: Enter, or a second click, resigns
        });
        add(TR("Ask for a takeback on Lichess"), TR("Play"), [this] { onlineAction("takeback/yes"); });
        if (online_game_->state.moves.size() < 2)
            add(TR("Abort the game on Lichess"), TR("Play"), [this] { onlineAction("abort"); });
        if (online_game_->state.opponent_gone)
            add(TR("Claim the victory on Lichess"), TR("Play"), [this] { onlineAction("claim-victory"); });
        add(TR("Open the game on lichess.org"), "Lichess",
            [this] { SDL_OpenURL(fmt::format("https://lichess.org/{}", online_game_->id).c_str()); });
    }
    // A Lichess bot for an engine (#85): online or offline, its game on the Board, its card; the engine shown's setup.
    loadBot();
    const auto bot_card = [this] {
        page_ = Page::Engines;
        for (int i = 0; i < static_cast<int>(engines_.size()); ++i)
            if (engines_[static_cast<std::size_t>(i)].name == botSettings().engine) selected_engine_ = i;
        engine_marked_.clear();
    };
    if (botReady()) {
        if (!bot_online_ || bot_closing_)
            add(TR("Put the Lichess bot online"), bot_account_->username, [this] { setBotOnline(true); });
        else
            add(TR("Put the Lichess bot offline"), bot_account_->username, [this] { setBotOnline(false); });
        if (botPlaying())
            add(TR("The Lichess bot's game on the Board"), bot_account_->username, [this] { openBotGame(); });
        add(TR("The Lichess bot's card"), bot_account_->username, bot_card);
    } else if (page_ == Page::Engines && selected_engine_ >= 0 &&
               selected_engine_ < static_cast<int>(engines_.size())) {
        add(trf("Make {} a Lichess bot…", engines_[static_cast<std::size_t>(selected_engine_)].name), TR("Engines"),
            [this] {
                bot_setup_engine_    = engines_[static_cast<std::size_t>(selected_engine_)].name;
                bot_step_            = BotStep::Account;
                botSettings().engine = bot_setup_engine_;
                saveBotSettings();
            });
    }
    // The Training page (#80): the page, the day's review, each theme's puzzles, Lichess's database, the batch online.
    add(TR("Training"), where(Page::Training), [this, go = page(Page::Training)] {
        go();
        training_tab_ = 0;
    });
    if (const auto day = reviewDay(); day.due() > 0 && trainingLock().empty())
        add(trn("Review all · {} to review today", "Review all · {} to review today", day.due(),
                ui::exactCount(day.due())),
            TR("Training"), [this] { startReview(); });
    add(TR("Puzzles"), where(Page::Training), [this, go = page(Page::Training)] {
        go();
        training_tab_ = 1;
    });
    if (offline_puzzles_) {
        const auto store = offline_puzzles_->store();
        if (store && puzzleLock().empty()) {
            add(TR("Puzzles: random"), TR("Training"), [this] {
                puzzles_online_ = false;
                puzzles_theme_.clear();
                startPuzzles();
            });
            for (const auto key : puzzleThemesShown()) {
                if (!store->themeList(key)) continue;
                add(trf("Puzzles: {}", puzzleThemeName(key)), TR("Training"), [this, key = std::string(key)] {
                    puzzles_online_ = false;
                    puzzles_theme_  = key;
                    startPuzzles();
                });
            }
        }
        if (offline_puzzles_->busy()) {
            add(TR("Cancel the download of Lichess's puzzles"), TR("Training"), [this] { offline_puzzles_->cancel(); });
        } else if (store) {
            add(TR("Update Lichess's puzzles"), TR("Training"), [this, go = page(Page::Training)] {
                go();
                training_tab_ = 1;
                offline_puzzles_note_.clear();
                offline_puzzles_update_ =
                    std::async(std::launch::async, [] { return evalsSize(lichess::puzzlesUrl()); });
            });
            add(TR("Remove Lichess's puzzles…"), TR("Training"), [this, go = page(Page::Training)] {
                go();
                training_tab_           = 1;
                offline_puzzles_remove_ = true;
            });
        } else {
            add(TR("Download Lichess's puzzles"), TR("Training"), [this, go = page(Page::Training)] {
                go();
                training_tab_ = 1;
                startOfflinePuzzles(lichess::puzzlesUrl());
            });
        }
    }
    add(TR("Puzzles online, from my Lichess account"), "Lichess", [this, go = page(Page::Training)] {
        go();
        training_tab_   = 1;
        puzzles_online_ = true;
    });
    // Your mistakes as exercises, guessing the moves (#81).
    add(TR("My mistakes"), where(Page::Training), [this, go = page(Page::Training)] {
        go();
        training_tab_ = 2;
    });
    if (mistakeLock().empty()) {
        const auto c = mistakeDayCounts();
        if (c.due > 0)
            add(trn("Review my mistakes · {} due", "Review my mistakes · {} due", c.due, ui::exactCount(c.due)),
                TR("Training"), [this] { startReview(ReviewKind::Mistakes); });
        if (c.due + c.today > 0)
            add(trn("Exercises from my mistakes · {}", "Exercises from my mistakes · {}", c.due + c.today,
                    ui::exactCount(c.due + c.today)),
                TR("Training"), [this] {
                    mistake_kind_.clear();
                    startMistakes(false);
                });
    }
    if (!mistake_unreviewed_.empty() && !mistake_reviews_)
        add(trn("Review {} recent game of mine in the background", "Review {} recent games of mine in the background",
                mistake_unreviewed_.size(), mistake_unreviewed_.size()),
            TR("Training"), [this] { startMistakeReviews(); });
    if (mistake_reviews_) add(TR("Cancel the reviews of my games"), TR("Training"), [this] { cancelMistakeReviews(); });
    add(TR("Your names…"), TR("Settings"), page(Page::Settings));
    add(TR("Guess the moves"), where(Page::Training), [this, go = page(Page::Training)] {
        // The Board page's game, from the position shown; else the Training page's part.
        if (page_ == Page::Board && !training_ && !puzzle_ && !mistake_ && !guess_ &&
            board_.ply < static_cast<int>(board_.game.moves.size()) && trainingLock().empty()) {
            startGuessBoard();
            return;
        }
        go();
        training_tab_ = 3;
    });
    if (trainingLock().empty())
        add(TR("Guess the moves: a random game"), TR("Training"), [this] { (void)startGuessRandom(); });
    // The endgames against the tables, the mates, board vision (#82).
    add(TR("Endgames"), where(Page::Training), [this, go = page(Page::Training)] {
        go();
        training_tab_ = 4;
    });
    add(TR("Mates"), where(Page::Training), [this, go = page(Page::Training)] {
        go();
        training_tab_         = 4;
        endgame_scroll_mates_ = true;
    });
    if (endgameLock().empty()) {
        for (const auto& e : endgameCatalogue())
            add(trf("Endgame: {}", TR(e.name)), TR("Training"), [this, &e, go = page(Page::Training)] {
                if (endgameNeed(e.fen).empty() && startEndgame(e)) return;
                go();
                training_tab_ = 4;
                endgame_need_ = e.id;
            });
        if (const auto n = reviewCount(ReviewKind::Endgames).due; n > 0)
            add(trn("Review the missed endgames · {} due", "Review the missed endgames · {} due", n, ui::exactCount(n)),
                TR("Training"), [this] { startReview(ReviewKind::Endgames); });
        if (!gameEndgames().empty())
            add(TR("Endgames of my games"), TR("Training"), [this] { (void)startGameEndgames(); });
    }
    if (syzygy_download_) {
        if (syzygy_download_->busy())
            add(TR("Cancel the download of the Syzygy tables"), TR("Training"), [this] { syzygy_download_->cancel(); });
        else if (!syzygyState().complete)
            add(trf("Download the Syzygy tables of 3 to 5 pieces · {}", formatBytes(syzygy345Bytes())), TR("Training"),
                [this, go = page(Page::Training)] {
                    go();
                    training_tab_ = 4;
                    startSyzygyDownload();
                });
    }
    add(TR("Vision"), where(Page::Training), [this, go = page(Page::Training)] {
        go();
        training_tab_ = 5;
    });
    if (trainingLock().empty()) {
        add(TR("Coordinates: find the square"), TR("Training"), [this] {
            vision_mode_ = CoordMode::Find;
            startCoordinates();
        });
        add(TR("Coordinates: name the square"), TR("Training"), [this] {
            vision_mode_ = CoordMode::Name;
            startCoordinates();
        });
        add(TR("Blindfold against an engine"), TR("Training"), [this] { startBlindfold(); });
        add(TR("A line read in your head"), TR("Training"), [this] { startHeadLines(); });
    }
    add("Lichess", where(Page::Lichess), page(Page::Lichess));
    // The assistant (M5): a question about the board's position, its model unloaded.
    add(TR("Ask the assistant about the position"), "Alt+5", [this] {
        toStudyBoard();
        board_side_tab_ = 4;
        question_focus_ = true;
    });
    if (board_.ply > 0)
        add(TR("Comment this move"), "Alt+C", [this] {
            toStudyBoard();
            commentBoardMove();
        });
    if (!board_.reviews.empty()) {
        add(TR("Comment the key moments"), TR("Board"), [this] {
            toStudyBoard();
            commentKeyMoments();
        });
        add(TR("Annotate the game"), TR("Board"), [this] {
            toStudyBoard();
            annotateBoardGame();
        });
        // The story of the game (#62): written again by the model, told otherwise; copied as it is shown.
        if (!review_story_.empty()) {
            if (storyWithoutModel().empty())
                add(TR("Write the story of the game again"), TR("The assistant's model, held to the review's facts"),
                    [this] { writeStory(model_story_of_ == review_story_of_); });
            add(TR("Copy the story of the game"), TR("Board"), [this] {
                ImGui::SetClipboardText(storyText().c_str());
                boardToast(TR("The story is copied"));
            });
        }
    }
    if (assistant_ && assistant_->state() == AssistantService::State::Ready)
        add(TR("Unload the assistant's model"), TR("Its memory freed; loaded again at the next question"),
            [this] { assistant_->unload(); });
    if (!lichess_account_)
        add(TR("Link the Lichess account…"), "Lichess", [this, go = page(Page::Lichess)] {
            go();
            startLichessLink();
        });
    // Fair play (#48): hearing the user's games start and end, offered again after "Not now"; the right to play
    // opens the same stream (#84).
    if (lichess_account_ && !lichess_account_->has(lichess::SCOPE_CHALLENGE_READ) &&
        !lichess_account_->has(lichess::SCOPE_BOARD_PLAY))
        add(TR("Know when my Lichess games start and end…"), "Lichess", [this, go = page(Page::Lichess)] {
            go();
            lichess_.listen_declined = false;
            saveLichessSettings();
        });
    // The followed players' games, in the broadcasts' mosaic (#50).
    add(TR("Followed players' games in the mosaic"), "Lichess", [this] { openPlayersMosaic(); });
    // Lichess TV (#86): its card; each channel in the mosaic, its featured game on the Board once the card read them;
    // the Board's game.
    add(TR("Lichess TV"), where(Page::Lichess), [this] {
        broadcast_round_.clear();  // the page itself, where its card is
        page_ = Page::Lichess;
    });
    for (const auto key : lichess::tvChannels()) {
        add(trf("Lichess TV: {} in the mosaic", tvChannelName(key)), "Lichess",
            [this, k = std::string(key)] { openTvMosaic(k); });
        if (tv_rows_.value)
            add(trf("Lichess TV: {} on the Board", tvChannelName(key)), "Lichess",
                [this, k = std::string(key)] { followTvFeatured(k); });
    }
    if (board_tv_) {
        add(board_tv_->stay ? TR("Lichess TV: go on to the channel's next game") : TR("Lichess TV: stay on this game"),
            TR("Board"), [this] {
                if (board_tv_) board_tv_->stay = !board_tv_->stay;
            });
        add(TR("Lichess TV: keep this game…"), TR("Board"), [this] {
            page_         = Page::Board;
            tv_keep_menu_ = true;
        });
    }
    if (boardBackToMosaic()) add(TR("Back to the mosaic"), TR("Board"), [this] { backToMosaic(); });
    // To lichess.org, from the Board (#54).
    if (page_ == Page::Board) {
        if (!toLichessLock(false)) {
            add(TR("Open the position on lichess.org"), "Lichess", [this] { openPositionOnLichess(); });
            add(TR("Have the position played out on Lichess…"), "Lichess", [this] { open_to_lichess_ = true; });
        }
        if (!toLichessLock(true) && !board_.tree.main.moves.empty())
            add(TR("Share the game on Lichess"), "Lichess", [this] { shareGameOnLichess(); });
        if (lichess_account_ && !boardLichessGameId().empty())
            add(TR("Bookmark the game on Lichess"), "Lichess", [this] { bookmarkGameOnLichess(); });
    }
    // The user's studies (#79): their card, the game sent into one, its changes sent again.
    add(TR("Lichess studies"), "Lichess", [this, go = page(Page::Lichess)] {
        go();
        loadStudiesState();
        studies_.shown = studies_.opened = true;
        saveStudiesState();
    });
    if (chessBoard() && !toLichessLock(true) && !board_.tree.main.moves.empty())
        add(TR("Send the game to a Lichess study…"), "Lichess", [this] {
            toStudyBoard();
            openStudySend();
        });
    if (board_.sent && board_.since_sent.any() && !toLichessLock(true))
        add(TR("Send the changes to the Lichess study"), "Lichess", [this] { sendStudyChanges(); });
    // The books from Lichess's open data (#53).
    add(TR("From Lichess's named openings…"), TR("Books"), [this] {
        page_                  = Page::Books;
        opening_suite_shown_   = true;
        evaluated_suite_shown_ = false;
        book_build_shown_      = false;
    });
    add(TR("From Lichess's evaluated positions…"), TR("Books"), [this] {
        page_ = Page::Books;
        openEvaluatedSuite();
    });
    // Lichess's evaluations kept offline (#58): their card, and what it does.
    if (offline_evals_) {
        const auto show = [this, go = page(Page::Lichess)] {
            go();
            offline_evals_focus_ = true;
        };
        const auto st = offline_evals_->state();
        if (offline_evals_->busy()) {
            add(TR("Lichess's evaluations offline: the preparation"), "Lichess", show);
            add(TR("Cancel the preparation of Lichess's evaluations"), "Lichess", [this] { offline_evals_->cancel(); });
        } else if (st.ready) {
            add(TR("Lichess's evaluations offline"), "Lichess", show);
            add(TR("Update Lichess's evaluations offline"), "Lichess", [this, show] {
                show();
                offline_evals_note_.clear();
                if (!offline_evals_update_.valid())
                    offline_evals_update_ =
                        std::async(std::launch::async, [] { return evalsSize(lichess::evalsUrl()); });
            });
            add(TR("Remove Lichess's evaluations offline…"), "Lichess", [this, show] {
                show();
                offline_evals_remove_ = true;
            });
        } else {
            add(TR("Lichess's evaluations offline: download and prepare…"), "Lichess", show);
        }
    }
    // A game prepared against an opponent (#52).
    add(TR("Prepare a game against…"), "Lichess", [this, go = page(Page::Lichess)] {
        go();
        prep_focus_ = true;
    });
    // The tournament on the user's own Lichess broadcast (#51).
    if (run_->running() && !setup_.broadcast_lichess)
        add(TR("Broadcast on Lichess…"), TR("Tournament"), [this] {
            page_ = Page::Tournament;
            openCastSheet();
        });
    // The kept searches (M1, board 6): on the database chosen, else the first of the user's.
    for (std::size_t i = 0; i < kept_searches_.size(); ++i) {
        add(trf("Search: {}", kept_searches_[i].name), TR("Databases"), [this, i] {
            page_               = Page::Databases;
            const auto& entries = shelf().entries();
            if (db_selected_ < 0 || static_cast<std::size_t>(db_selected_) >= entries.size()) {
                for (std::size_t e = 0; e < entries.size(); ++e)
                    if (!entries[e].source) {
                        db_selected_ = static_cast<int>(e);
                        break;
                    }
            }
            if (db_selected_ >= 0) applyKeptSearch(i);
        });
    }
    const auto ratings_tab = [&](const char* label, int index) {
        add(shown(label), TR("Ratings"), [this, index, go = page(Page::Ratings)] {
            go();
            ratings_tab_request_ = index;
        });
    };
    ratings_tab(TR("Rating list###rt_list"), 0);
    ratings_tab(TR("Engine###rt_engine"), 1);
    ratings_tab(TR("Head to head###rt_h2h"), 2);
    ratings_tab(TR("Progress###rt_progress"), 3);
    add(TR("Log"), "", page(Page::Log));
    add(TR("Settings"), "", page(Page::Settings));
    add(TR("Copy the version"), TR("Settings"), [this] { copyVersion(); });
    add(TR("Report a problem…"), TR("Settings"), [this] { reportProblem(); });
    add(TR("Open the profile folder"), TR("Settings"), [this] { ui::openPath(storage_.root()); });
    add(TR("Open the tournaments folder"), TR("Settings"), [this] { ui::openPath(storage_.runsDir()); });
    add(TR("Change the tournaments folder…"), TR("Settings"), [this] { applyFix(changeTournamentsFolderFix()); });
    if (const auto* release = update::notice(updates_, FCG_VERSION)) {
        add(trf("Download page of RookStudio {}", release->version), TR("Settings"),
            [url = release->url] { SDL_OpenURL(url.c_str()); });
    }
    add(TR("Software rendering"), TR("Settings"), page(Page::Settings));  // the checkbox and what draws the window
    // Appearance: its section of Settings, then its choices made at once (the board of the theme in use).
    add(TR("Appearance: board colours, pieces, move sounds"), TR("Settings"), [this, go = page(Page::Settings)] {
        go();
        scroll_to_appearance_ = true;
    });
    for (const auto& p : appearance::boardPresets()) {
        add(trf("Board colours: {}", TR(p.name)), TR("Settings"), [this, &p] {
            auto s       = appearance::current();
            auto& board  = s.board(ui::currentTheme());
            board.preset = p.id;
            board.custom = p.colours;
            applyAppearance(s);
        });
    }
    for (const auto& p : appearance::pieceSets()) {
        add(trf("Pieces: {}", p.name), TR("Settings"), [this, id = p.id] {
            auto s   = appearance::current();
            s.pieces = id;
            applyAppearance(s);
        });
    }
    // The remote machines: their section of Settings, a new one, and each one's test.
    add(TR("Machines"), TR("Settings"), [this, go = page(Page::Settings)] {
        go();
        scroll_to_machines_ = true;
    });
    add(TR("Add a machine"), TR("Settings"), [this, go = page(Page::Settings)] {
        go();
        addMachine();
        scroll_to_machines_ = true;
    });
    for (const auto& m : machines_) {
        add(trf("Test the connection to {}", m.name), TR("Machines"), [this, go = page(Page::Settings), id = m.id] {
            go();
            machine_open_       = id;
            scroll_to_machines_ = true;
            startMachineJob(id, MachineJob::Kind::Test);
        });
        if (!m.engines.empty()) {
            add(trf("Check the engines on {}", m.name), TR("Machines"), [this, go = page(Page::Settings), id = m.id] {
                go();
                machine_open_                      = id;
                scroll_to_machines_                = true;
                machine_activity_[id].open_engines = true;
                startMachineJob(id, MachineJob::Kind::Engines);
            });
        }
        // The library's engines: copied to the machine when they have no path there, replaced when the build there
        // is another one.
        for (const auto& e : engines_) {
            if (e.inactive) continue;
            const auto* there = m.engine(e.name);
            if (!there || there->path.empty()) {
                add(trf("Copy {} to {}", e.name, m.name), TR("Machines"),
                    [this, id = m.id, name = e.name] { offerEngineCopy(id, name); });
            } else if (!there->remote_hash.empty() && there->remote_hash != there->local_hash) {
                add(trf("Replace {} on {} with this computer's build", e.name, m.name), TR("Machines"),
                    [this, id = m.id, name = e.name] { offerEngineCopy(id, name); });
            }
        }
    }
    add(TR("Pieces: your own folder…"), TR("Settings"),
        [this] { dialogs_.openFolder(Dialogs::Purpose::PieceFolder, appearance::current().piece_folder); });
    add(appearance::current().sounds ? TR("Turn the move sounds off") : TR("Turn the move sounds on"), TR("Settings"),
        [this] {
            auto s   = appearance::current();
            s.sounds = !s.sounds;
            applyAppearance(s);
        });
    add(appearance::current().engine_logos ? TR("Hide the engines' logos") : TR("Show the engines' logos"),
        TR("Settings"), [this] {
            auto s         = appearance::current();
            s.engine_logos = !s.engine_logos;
            applyAppearance(s);
        });
    // The logo of the engine whose card is open.
    if (page_ == Page::Engines && selected_engine_ >= 0 && selected_engine_ < static_cast<int>(engines_.size())) {
        add(trf("Choose a logo for {}…", engines_[static_cast<std::size_t>(selected_engine_)].name), TR("Engines"),
            [this] {
                dialogs_.openFile(Dialogs::Purpose::EngineLogo,
                                  {{TR("Pictures (PNG, JPEG, SVG)"), "png;jpg;jpeg;svg"}, {TR("All files"), "*"}});
            });
    }
    add(setup_.nav_buttons ? TR("Hide the navigation buttons under the boards")
                           : TR("Show the navigation buttons under the boards"),
        TR("Settings"), [this] {
            setup_.nav_buttons = !setup_.nav_buttons;
            saveSetupSoon();
        });

    // The tournaments of the home page: one opens as its Open button does.
    for (const auto& s : tournaments_) {
        add(s.spec.name, fmt::format("{} · {}", TR("Tournament"), folderDate(s.dir)), [this, dir = s.dir] {
            openTournament(dir);
            if (!start_error_.empty()) page_ = Page::Home;  // where the reason is shown
        });
    }
    // The engines, each on its card of the Engines page, and the books.
    for (std::size_t i = 0; i < engines_.size(); ++i) {
        const auto& e = engines_[i];
        add(e.name, e.inactive ? fmt::format("{} · {}", TR("Engines"), TR("Inactive")) : std::string(TR("Engines")),
            [this, i] {
                page_            = Page::Engines;
                selected_engine_ = static_cast<int>(i);
                engine_marked_.clear();
            });
    }
    for (std::size_t i = 0; i < books_.size(); ++i) {
        add(books_[i].name, TR("Books"), [this, i] {
            page_          = Page::Books;
            selected_book_ = static_cast<int>(i);
            book_item_     = -1;
        });
    }
    // The guide's sections, by their titles in the interface's language, and its search with what was typed.
    if (const auto* en = guide::doc("en")) {
        const auto code = guide::currentCode();
        for (std::size_t i = 1; i < en->headings.size(); ++i) {
            const auto s = guide::shown(code, i);
            if (!s.heading) continue;
            add(trf("Guide: {}", s.heading->title), TR("Guide"), [this, id = en->headings[i].id] { guide_.open(id); });
        }
    }
    if (!palette_query_.empty())
        add(trf("Search the guide for “{}”", palette_query_), TR("Guide"),
            [this, q = palette_query_] { guide_.search(q); });
    return out;
}

void App::drawPalette() {
    // F1: the guide at what is under the mouse, or at the page shown, also from a text field (none uses it).
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) toggleGuide();

    // Ctrl+K, but not while a text field has the keys, nor over another popup or dialog: opening this one would close
    // it.
    const bool ctrl_k = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_K);
    if (ctrl_k && !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        palette_query_.clear();
        palette_request_ = true;
    }
    if (palette_request_) {
        palette_request_   = false;
        palette_selected_  = 0;
        tournaments_dirty_ = true;  // the list of the home page, read again
        ImGui::OpenPopup("##palette");
    }

    // A column in the middle of the window, near its top.
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.12f),
        ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(std::min(px(640), viewport->WorkSize.x - px(32)), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(12), px(12)));
    const bool open = ImGui::BeginPopup("##palette", ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar();
    if (!open) return;
    refreshTournaments();

    std::function<void()> chosen;
    bool close = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || (ctrl_k && !ImGui::IsWindowAppearing());

    // The search field keeps the keys: Up and Down move in the list (the history callback keeps them from the
    // navigation), Enter runs the line chosen.
    // Not while the mouse is down: it may be clicking a line, which needs the press and the release.
    if (ImGui::IsWindowAppearing() || (!ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)))
        ImGui::SetKeyboardFocusHere();
    char buffer[256];
    const auto n = std::min(palette_query_.size(), sizeof(buffer) - 1);
    std::copy_n(palette_query_.data(), n, buffer);
    buffer[n] = 0;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##palette_search", TR("A page, an engine, a tournament, an action…"), buffer,
                                 sizeof(buffer), ImGuiInputTextFlags_CallbackHistory,
                                 [](ImGuiInputTextCallbackData*) { return 0; })) {
        palette_query_    = buffer;
        palette_selected_ = 0;
    }

    const auto entries = paletteEntries();
    std::vector<std::string> labels, wheres;
    labels.reserve(entries.size());
    wheres.reserve(entries.size());
    for (const auto& e : entries) {
        labels.push_back(e.label);
        wheres.push_back(e.where);
    }
    const auto order  = palette::rank(palette_query_, labels, &wheres);
    const int matches = static_cast<int>(order.size());

    bool moved = false;
    if (matches > 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) palette_selected_++, moved = true;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) palette_selected_--, moved = true;
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) palette_selected_ += 10, moved = true;
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) palette_selected_ -= 10, moved = true;
    }
    palette_selected_ = std::clamp(palette_selected_, 0, std::max(0, matches - 1));
    if (matches > 0 && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
        chosen = entries[static_cast<std::size_t>(order[static_cast<std::size_t>(palette_selected_)])].run;

    ImGui::Spacing();
    if (matches == 0) {
        ImGui::TextDisabled("%s", TR("Nothing matches."));
    } else {
        // Twelve lines in sight, the others scrolled to.
        const float line = ImGui::GetTextLineHeightWithSpacing();
        ImGui::BeginChild("##palette_list", ImVec2(0, line * static_cast<float>(std::min(matches, 12))),
                          ImGuiChildFlags_None, ImGuiWindowFlags_NoNav);
        auto* dl          = ImGui::GetWindowDrawList();
        const ImU32 text  = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const float gap   = px(16);
        ImGuiListClipper clipper;
        clipper.Begin(matches);
        if (moved) clipper.IncludeItemByIndex(palette_selected_);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& e = entries[static_cast<std::size_t>(order[static_cast<std::size_t>(row)])];
                ImGui::PushID(row);
                if (ImGui::Selectable("##line", row == palette_selected_)) chosen = e.run;
                if (moved && row == palette_selected_) ImGui::SetScrollHereY();
                ImGui::PopID();

                // The name on the left, where it belongs on the right; a long name stops before it.
                const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
                const float pad   = ImGui::GetStyle().FramePadding.x * 0.5f;
                const float where = e.where.empty() ? 0.0f : ImGui::CalcTextSize(e.where.c_str()).x;
                if (where > 0) dl->AddText(ImVec2(max.x - pad - where, min.y), muted, e.where.c_str());
                const ImVec4 clip(min.x, min.y, max.x - pad - where - (where > 0 ? gap : 0.0f), max.y);
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(min.x + pad, min.y), text, e.label.c_str(),
                            nullptr, 0.0f, &clip);
            }
        }
        ImGui::EndChild();
    }

    if (chosen) close = true;
    if (close) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    // Once the palette is closed: an action may open a popup or a dialog of its own.
    if (chosen) chosen();
}

}  // namespace fcg
