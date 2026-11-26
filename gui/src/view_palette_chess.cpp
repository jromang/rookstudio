// RookStudio's entries of the command palette (Ctrl+K), out of App::paletteEntries() so that it stays as fastchess-gui
// writes it and its releases merge without conflicts (#108). Each function is called where its entries were among
// fastchess-gui's, made with the same add() and page(): the palette lists the same entries, in the same order.

#include <algorithm>
#include <future>
#include <optional>
#include <string>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include "app.hpp"
#include "assistant_service.hpp"
#include "i18n.hpp"
#include "lichess_months.hpp"
#include "ui.hpp"

namespace fcg {

void App::addEndgameCheckCommands(const PaletteAdd& add, const tour::TournamentState* t, const std::string& name) {
    // The endgames of a finished tournament against the tables (#94), as its menu offers: the one shown, the last
    // finished.
    {
        std::vector<std::pair<std::filesystem::path, std::string>> sources;
        const bool shown_done = t && !run_->running() && !(unfinished_ && unfinished_->dir == t->dir());
        if (shown_done) sources.emplace_back(t->dir(), name);
        const auto busy = run_->running() && t ? t->dir() : std::filesystem::path();
        for (const auto* s : home::lastFinished(tournaments_, home::LAST_FINISHED, busy))
            if (!shown_done || s->dir != t->dir()) sources.emplace_back(s->dir, s->spec.name);
        for (const auto& source : sources)
            add(TR("Check the endgames against the tables"),
                fmt::format("{} · {}", source.second, folderDate(source.first)),
                [this, dir = source.first] { startEndgameCheck(dir); });
    }
}

void App::addAnalysisCommands(const PaletteAdd& add) {
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
}

void App::addNotationCommands(const PaletteAdd& add) {
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
}

void App::addChessCommands(const PaletteAdd& add, const PalettePage& page) {
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
    // A database's maintenance (#88, M13, board 1): of the database chosen, else of the first of the user's.
    add(TR("Duplicate games"), where(Page::Databases), [this] { openCare(db_selected_, CarePart::Duplicates); });
    add(TR("Players' names"), where(Page::Databases), [this] { openCare(db_selected_, CarePart::Names); });
    add(TR("FIDE's rating list"), where(Page::Databases), [this] { openCare(db_selected_, CarePart::Names); });
    add(TR("Check the database"), where(Page::Databases), [this] {
        openCare(db_selected_, CarePart::Checks);
        if (care_) care_->start_check = true;
    });
    add(TR("Compact the database"), where(Page::Databases), [this] { openCare(db_selected_, CarePart::Checks); });
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
    // The engines offered to lichess.org's analysis board (#96): stop or resume each; the engine shown's offer.
    {
        std::vector<std::string> serving, stopped;
        for (const auto& [name, s] : services_) serving.push_back(name);
        for (const auto& o : offers())
            if (!o.id.empty() && !services_.count(o.engine) && engineIndex(o.engine) >= 0) stopped.push_back(o.engine);
        for (const auto& name : serving)
            add(trf("Stop the service of {} on lichess.org", name), "Lichess", [this, name] { stopService(name); });
        for (const auto& name : stopped)
            add(trf("Resume the service of {} on lichess.org", name), "Lichess", [this, name] { startService(name); });
        if (page_ == Page::Engines && selected_engine_ >= 0 && selected_engine_ < static_cast<int>(engines_.size())) {
            const auto& e = engines_[static_cast<std::size_t>(selected_engine_)];
            const auto* o = offerOf(e.name);
            if ((!o || o->id.empty()) && knowsChess960(e))
                add(trf("Offer {} to lichess.org…", e.name), TR("Engines"), [this, name = e.name] {
                    const int i = engineIndex(name);
                    if (i >= 0) openOfferForm(engines_[static_cast<std::size_t>(i)]);
                });
        }
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
    // The practical moves against a human of a level (#94), in the Analysis tab.
    add(board_.settings.practical ? TR("Practical moves: off") : TR("Practical moves against a human of a level"),
        TR("Board"), [this] {
            toStudyBoard();
            board_side_tab_           = 1;
            board_.settings.practical = !board_.settings.practical;
            saveBoardSettings();
        });
    // The tables' verdict and each move by its outcome (#42).
    add(TR("Endgames: what the tables say of the position"), "Alt+6", [this] {
        toStudyBoard();
        board_side_tab_ = 5;
    });
    // The assistant (M5): a question about the board's position, its model unloaded.
    add(TR("Ask the assistant about the position"), "Alt+5", [this] {
        toStudyBoard();
        board_side_tab_ = 4;
        question_focus_ = true;
    });
    if (board_.ply > 0) {
        add(TR("Comment this move"), "Alt+C", [this] {
            toStudyBoard();
            commentBoardMove();
        });
        add(TR("Why not? The move tried against the game's"), "Alt+Q", [this] {
            toStudyBoard();
            whyNotHere();
        });
    }
    if (!board_.reviews.empty()) {
        add(TR("Comment the key moments"), TR("Board"), [this] {
            toStudyBoard();
            commentKeyMoments();
        });
        add(TR("Annotate the game"), TR("Board"), [this] {
            toStudyBoard();
            annotateBoardGame();
        });
        add(TR("Explain every move"), TR("Board"), [this] {
            toStudyBoard();
            explainEveryMove();
        });
        add(TR("Guided visit of the key moments"), TR("Board"), [this] {
            toStudyBoard();
            startVisit();
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
    if (board_.reviews.empty() && !board_.game.moves.empty())  // the review first, then every move (#94)
        add(TR("Explain every move"), TR("Board"), [this] {
            toStudyBoard();
            explainEveryMove();
        });
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
    // Lichess's tournaments (#95): the card; an arena of its list followed or joined; the Play page's part; in an
    // arena, the pause and berserk; the user's teams' Swiss tournaments; a finished Swiss kept.
    add(TR("Lichess tournaments: arenas and Swiss tournaments"), where(Page::Lichess), [this] {
        broadcast_round_.clear();  // the page itself, where its card is
        page_         = Page::Lichess;
        arena_card_y_ = -2;
    });
    add(TR("Play an arena or a Swiss tournament on Lichess"), where(Page::Play), [this] {
        play_part_            = 1;
        playSettings().part   = 1;
        online_part_          = 1;
        onlineSettings().part = 1;
        savePlaySettings();
        saveOnlineSettings();
        page_ = Page::Play;
    });
    if (arena_card_.list.value)
        for (const auto& a : *arena_card_.list.value) {
            add(trf("Follow the arena: {}", a.name), "Lichess",
                [this, id = a.id, name = a.name] { followTournament(id, name, false); });
            if (lichess::playableHere(a) && lichess_account_)
                add(trf("Join the arena: {}", a.name), TR("Play"),
                    [this, id = a.id, name = a.name] { chooseArena(id, name); });
        }
    if (tour_follow_)
        add(trf("The tournament followed in the mosaic: {}", tour_follow_->name), "Lichess",
            [this] { openBroadcastMosaic(tour_follow_->key); });
    if (arena_play_ && (arena_play_->seat.in() || arena_play_->seat.paused())) {
        add(arena_play_->seat.in() ? TR("Pause in the arena") : TR("Back in the arena"), "Alt+P",
            [this] { toggleArenaPause(); });
        add(TR("Back to the arena"), TR("Play"), [this] { chooseArena(arena_play_->id, arena_play_->name); });
    }
    if (online_game_ && arena_play_ && online_game_->state.tournament == arena_play_->id && arena_play_->state.value &&
        arena_play_->state.value->info.berserkable &&
        lichess::berserkAllowed(true, online_game_->state, online_game_->white))
        add(TR("Berserk"), TR("Play"), [this] {
            onlineCall(TR("Berserk"),
                       [id = online_game_->id](const std::string& token) { return lichess::berserk(token, id); });
        });
    if (lichess_account_ && !lichess_account_->has(lichess::SCOPE_TEAM_READ))
        add(TR("Read my teams' Swiss tournaments…"), "Lichess", [this] {
            broadcast_round_.clear();
            page_ = Page::Lichess;
            LichessScopeAsk ask;
            ask.scope          = lichess::SCOPE_TEAM_READ;
            lichess_scope_ask_ = ask;
        });
    if (swiss_play_ && swiss_play_->info.value && swiss_play_->info.value->state == lichess::TourState::Finished &&
        swiss_play_->kept_in.empty() && !swiss_play_->keep_job.valid())
        add(TR("Keep the Swiss in a database…"), TR("Play"), [this] {
            chooseSwiss(*swiss_play_->info.value);
            swiss_play_->keep_menu = true;
        });
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
    // A game prepared against an opponent (#52): their dossier, its Lichess source ticked (#90).
    add(TR("Prepare a game against…"), "Lichess", [this, go = page(Page::Lichess)] {
        go();
        prep_focus_ = true;
    });
    // A player's dossier (#90): the players whose names have the words typed, found in the background.
    lookForDossierNames(palette_query_);
    for (const auto& n : dossier_names_)
        add(trf("Dossier: {}", n.name), TR("Databases"), [this, name = n.name] {
            study::DossierSubject subject;
            subject.name = name;
            openDossier(std::move(subject));
        });
    // A tournament's crosstable (#92): the tournaments of the user's databases whose names have the words typed.
    lookForCrosstableNames(palette_query_);
    for (const auto& n : crosstable_names_)
        add(trf("Crosstable: {}", n.label), n.database, [this, folder = n.folder, key = n.key] {
            auto& s = shelf();
            for (std::size_t i = 0; i < s.entries().size(); ++i)
                if (s.entries()[i].folder == folder) openCrosstable(i, key);
        });
    // The tournament on the user's own Lichess broadcast (#51).
    if (run_->running() && !setup_.broadcast_lichess)
        add(TR("Broadcast on Lichess…"), TR("Tournament"), [this] {
            page_ = Page::Tournament;
            openCastSheet();
        });
    // The presets of the patterns (#89, M13, board 2): on the database chosen, else the first of the user's.
    for (std::size_t i = 0; i < study::patternPresets().size(); ++i) {
        const char* name = presetName(study::patternPresets()[i].key);
        if (std::any_of(kept_searches_.begin(), kept_searches_.end(),
                        [&](const KeptSearch& k) { return k.name == name; }))
            continue;  // kept under that name: the kept search's entry
        add(trf("Pattern: {}", name), TR("Databases"), [this, i] { applyPatternPreset(i); });
    }
    // From the Board page: the man under the mouse and its route in the game; the games at its pawn structure.
    if (chessBoard() && !liveGameOn()) {
        add(TR("This manoeuvre"), TR("Board"), [this] { searchThisManoeuvre(); });
        add(TR("Games at this structure"), TR("Board"), [this] { searchThisStructure(); });
    }
    // The opening report of the Board page's position (#91), in place of the page; locked during a live game (R17).
    if (chessBoard() && !board_.chess960)
        add(TR("Opening report"), TR("Board"), [this] {
            toStudyBoard();
            openOpeningReport(Page::Board);
        });
    // Publishing (#93): the study Board's game exported, its diagram copied; the reports shown printed or made pages;
    // a repertoire printed.
    if (chessBoard()) {
        add(TR("Export…"), TR("Board"), [this] {
            toStudyBoard();
            openBoardExport();
        });
        add(TR("Copy the diagram"), "Ctrl+Shift+C", [this] { copyDiagram(); });
    }
    if (dossier_ && dossier_->shown && dossier_->report && dossier_->report->board.games > 0) {
        add(trf("Print the dossier of {}…", dossier_->subject.name), TR("Dossier"),
            [this] { openDossierExport(false); });
        add(trf("The dossier of {} as an HTML page…", dossier_->subject.name), TR("Dossier"),
            [this] { openDossierExport(true); });
    }
    if (opening_report_ && opening_report_->shown && opening_report_->report) {
        add(TR("Print the opening report…"), TR("Board"), [this] { openReportExport(false); });
        add(TR("The opening report as an HTML page…"), TR("Board"), [this] { openReportExport(true); });
    }
    if (crosstable_ && crosstable_->input) {
        add(trf("Print the crosstable of {}…", crosstable_->title), TR("Databases"),
            [this] { openCrosstableExport(false); });
        add(trf("The crosstable of {} as an HTML page…", crosstable_->title), TR("Databases"),
            [this] { openCrosstableExport(true); });
    }
    for (std::size_t i = 0; i < repertoires().entries().size(); ++i)
        add(trf("Print the repertoire “{}”…", repertoires().entries()[i].name), TR("Openings"), [this, i] {
            page_ = Page::Openings;
            openRepertoirePrint(i);
        });
    // The kept searches (M1, board 6): on the database chosen, else the first of the user's; a pattern alone said so.
    for (std::size_t i = 0; i < kept_searches_.size(); ++i) {
        const auto& f    = kept_searches_[i].fields;
        const bool heads = f.player[0] || f.opponent[0] || f.event[0] || f.year_from[0] || f.year_to[0] ||
                           f.eco_from[0] || f.eco_to[0] || f.elo_min[0] || f.result || f.damaged || f.position ||
                           std::any_of(f.material.begin(), f.material.end(), [](const auto& m) { return m[0] != 0; }) ||
                           f.route.squares.size() >= 2 || f.structure.set;
        const bool pattern_alone = !heads && (!f.language.empty() || std::any_of(f.pattern.begin(), f.pattern.end(),
                                                                                 [](int8_t v) { return v >= 0; }));
        add(pattern_alone ? trf("Pattern: {}", kept_searches_[i].name) : trf("Search: {}", kept_searches_[i].name),
            TR("Databases"), [this, i] {
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
}

}  // namespace fcg
