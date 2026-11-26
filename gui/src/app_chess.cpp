// RookStudio's additions to App::frame() and App::handleDialogs(), out of those functions so that they stay as
// fastchess-gui writes them and its releases merge without conflicts (#108): the polls of RookStudio's pages and jobs,
// its --focus words, and what becomes of the files its dialogs chose.

#include "app.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iterator>
#include <string>
#include <string_view>

#include <study/evals_index.hpp>
#include <study/import.hpp>
#include <study/patterns.hpp>
#include <tour/files.hpp>

#include "assistant_service.hpp"
#include "board_text.hpp"
#include "chesscom.hpp"
#include "fide_rating_list.hpp"
#include "focus_words.hpp"
#include "i18n.hpp"
#include "lichess_months.hpp"
#include "lichess_tv.hpp"
#include "twic.hpp"

namespace fcg {

void App::initChessServers() {
    // Scripted screenshots ask nothing of the internet (AppOptions::scripted): Lichess, its database, TWIC, chess.com
    // and FIDE pointed at a port of this machine where nothing listens, before anything asks them; a focus word puts a
    // test's server in place of one (lichess-api=, lichess-database=, twic-server=, chesscom-server=, fide-server=).
    if (options_.scripted) {
        const std::string nowhere = "http://127.0.0.1:9";
        lichess::servers() = lichess::Servers{nowhere, nowhere + "/tb", nowhere + "/explorer", nowhere + "/engine"};
        lichess::setDatabaseServer(nowhere);
        twic::setServer(nowhere);
        chesscom::setServer(nowhere);
        fide::setServer(nowhere + "/players_list.zip");
    }
    // A test's Lichess (--focus lichess-api=http://127.0.0.1:PORT), in place before anything asks lichess.org: the
    // games in progress are asked at the first frame, before the focus words are read.
    if (const auto& w = options_.focus_window; w.rfind("lichess-api=http://127.0.0.1:", 0) == 0) {
        const auto comma   = w.find(',');
        const auto base    = w.substr(12, comma == std::string::npos ? std::string::npos : comma - 12);
        lichess::servers() = lichess::Servers{base, base + "/tb", base + "/explorer", base + "/engine"};
    }
}

void App::pollChess() {
    pollLichess();             // a link, a sync, the broadcasts: whatever page shows
    pollLinkJob();             // a Lichess link pasted: its game asked, its import
    pollLichessPlaying();      // the user's games in progress on Lichess, and what they lock (#48)
    pollFollowedPlayers();     // the players followed live, their games in the mosaic (#50)
    pollCast();                // a tournament on the user's own Lichess broadcast, once allowed (#51)
    pollPrep();                // a game prepared against an opponent: their games (#52)
    pollDossier();             // a player's dossier: the job that ended, their Lichess games that came (#90)
    pollOpeningReport();       // an opening report: its sections, evaluations, summary, Lichess's answers (#91)
    pollExport();              // the Export sheet: a file written in the background, its weight estimated (#93)
    pollEvalsReading();        // Lichess's evaluations database read for a book (#53)
    pollOfflineEvals();        // Lichess's evaluations kept offline: a preparation's runs, its end (#58)
    pollToLichess();           // a game shared, a position played out, a game bookmarked on Lichess (#54)
    pollStudies();             // the user's Lichess studies: read, imported, followed, a game sent (#79)
    pollTheory();              // the theory studies: read, read again when their files change (#77)
    pollTraining();            // a repertoire's training: its cards prepared, its first question (#76)
    pollPuzzles();             // Lichess's puzzles: a preparation's end, the batch online's requests (#80)
    pollMistakes();            // the user's mistakes: looked for, their games reviewed, a session's engine (#81)
    pollEndgames();            // the endgames: the tables' answers and moves, the tables' download, vision's game (#82)
    pollEndgameCheck();        // a finished tournament's endgames against the tables (#94)
    pollEngineGame();          // a game against an engine: kept at each move, its end, a hint (#83)
    pollOnlinePlay();          // playing on Lichess: a seek, the challenges, the game on the Board (#84)
    pollBot();                 // a Lichess bot for an engine: its account, its challenges, its game (#85)
    pollLichessEngines();      // the engines offered to lichess.org: their registrations, guards, requests (#96)
    pollBroadcastStandings();  // a broadcast's standings, a search among the broadcasts (#55)
    pollFideCards();           // a FIDE card asked for (#57)
    pollEnginePicker();
    pollEngineVersions();  // an engine's old versions deleted once the tournaments that play them are over (#65)
    pollQuestions();       // the answers, the assistant's download, its model unloaded when unused
}

bool App::applyChessFocusWord(const std::string& w) {
    // Tried in this order, before fastchess-gui's words: the first entry whose pattern matches the word is applied
    // (focus_words.hpp).
    FocusWords words(w);
    words.on({"chess-home"}, [&] { page_ = Page::ChessHome; });
    words.on({"databases"}, [&] { page_ = Page::Databases; });
    words.on({"databases-list", "databases-list=*"}, [&] {
        // The first database's games; databases-list=WORDS: through that quick filter.
        page_        = Page::Databases;
        db_selected_ = 0;
        db_list_     = true;
        if (w.size() > 15) std::snprintf(db_filter_.data(), db_filter_.size(), "%s", w.substr(15).c_str());
    });
    words.on({"databases-search", "databases-search-position"}, [&] {
        // The first database's games with the search panel; its position searched.
        page_               = Page::Databases;
        db_selected_        = 0;
        db_list_            = true;
        db_search_open_     = true;
        db_search_.position = w == "databases-search-position";
        std::snprintf(db_search_.year_from.data(), db_search_.year_from.size(), "2020");
    });
    words.on({"databases-search-pattern*", "databases-search-text*"}, [&] {
        // databases-search-pattern[=PRESET][,WORD], databases-search-text[=PRESET][,WORD]: the first database's
        // search panel on its Pattern tab (#89), a preset set (its key: knight-fork, h7-sacrifice…), its text
        // shown; then WORD, another of these (databases-import-into=FILE: an import going on).
        const auto comma = w.find(',');
        if (comma != std::string::npos) focus_next_ = w.substr(comma + 1);
        const auto word = w.substr(0, comma);
        page_           = Page::Databases;
        db_selected_    = 0;
        if (const auto eq = word.find('='); eq != std::string::npos) {
            const auto key = word.substr(eq + 1);
            for (std::size_t i = 0; i < study::patternPresets().size(); ++i)
                if (study::patternPresets()[i].key == key) applyPatternPreset(i);
        }
        openSearchOn(TabPattern);
        db_selected_       = 0;
        pattern_text_open_ = word.rfind("databases-search-text", 0) == 0;
    });
    words.on({"databases-search-route*"}, [&] {
        // databases-search-route[=b1,d2,f1,g3]: the Manoeuvre tab, the man of the first square on the Board page's
        // position going along that route.
        page_        = Page::Databases;
        db_selected_ = 0;
        openSearchOn(TabRoute);
        db_selected_ = 0;
        if (const auto eq = w.find('='); eq != std::string::npos) {
            auto& r = db_search_.route;
            r.squares.clear();
            for (std::size_t at = eq + 1; at + 1 < w.size() + 1;) {
                const auto comma = w.find(',', at);
                const auto item  = w.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
                if (item.size() == 2 && item[0] >= 'a' && item[0] <= 'h' && item[1] >= '1' && item[1] <= '8')
                    r.squares.push_back(static_cast<uint8_t>((item[1] - '1') * 8 + (item[0] - 'a')));
                if (comma == std::string::npos) break;
                at = comma + 1;
            }
            const auto board = boardAt(board_.game, board_.chess960, board_.ply);
            if (!r.squares.empty()) {
                const auto piece = board.at(chess::Square(r.squares[0]));
                if (piece != chess::Piece::NONE) {
                    r.side = piece.color() == chess::Color::WHITE ? 0 : 1;
                    r.kind = static_cast<int>(piece.type());
                }
            }
        }
    });
    words.on({"databases-search-structure"}, [&] {
        // The Structure tab: the Board page's pawns, searched.
        page_        = Page::Databases;
        db_selected_ = 0;
        openSearchOn(TabStructure);
        db_selected_ = 0;
    });
    words.on({"databases-care", "databases-care=*"}, [&] {
        // databases-care[=names|checks|check|pair]: the first database's maintenance (#88), at that part; check:
        // its check started; pair: its first group of duplicates compared.
        const auto part = w.size() > 15 ? w.substr(15) : std::string("duplicates");
        openCare(0, part == "names"                       ? CarePart::Names
                    : part == "checks" || part == "check" ? CarePart::Checks
                                                          : CarePart::Duplicates);
        if (care_) {
            care_->start_check     = part == "check";
            care_->open_first_pair = part == "pair";
        }
    });
    words.on({"dossier=*", "dossier-lichess=*"}, [&] {
        // dossier=NAME[@TAB]: the dossier of NAME (as the databases write it) over the Databases page (#90), at
        // its tab (overview, openings, rating, lichess); dossier-lichess=ACCOUNT[@TAB]: theirs from the Lichess
        // page's "Prepare a game against", its Lichess source ticked (with lichess-api= before it).
        const bool online = w[7] == '-';
        auto spec         = w.substr(w.find('=') + 1);
        std::string tab;
        if (const auto at = spec.rfind('@'); at != std::string::npos) {
            tab  = spec.substr(at + 1);
            spec = spec.substr(0, at);
        }
        page_ = online ? Page::Lichess : Page::Databases;
        study::DossierSubject subject;
        subject.name = spec;
        if (online) subject.lichess = spec;
        openDossier(std::move(subject), online);
        if (dossier_) dossier_->tab = tab == "openings" ? 1 : tab == "rating" ? 2 : tab == "lichess" ? 3 : 0;
    });
    words.on({"tournaments", "tournaments=*", "crosstable=*"}, [&] {
        // tournaments[=DATABASE]: the tournaments of the first database, or of the one of that name (#92);
        // crosstable=WORDS[@DATABASE]: the crosstable of its first tournament whose name holds WORDS, once listed.
        const bool table = w[0] == 'c';
        auto spec        = w.find('=') == std::string::npos ? std::string() : w.substr(w.find('=') + 1);
        std::string base = table ? std::string() : spec;
        if (table) {
            if (const auto at = spec.rfind('@'); at != std::string::npos) {
                base = spec.substr(at + 1);
                spec = spec.substr(0, at);
            }
            crosstable_request_ = std::make_pair(spec, base);
        }
        page_           = Page::Databases;
        db_selected_    = 0;
        db_list_        = true;
        db_tournaments_ = true;
        auto& s         = shelf();
        for (std::size_t i = 0; i < s.entries().size(); ++i)
            if (!base.empty() && s.entries()[i].name == base) db_selected_ = static_cast<int>(i);
    });
    words.on({"broadcast-follow=*"}, [&] {
        // broadcast-follow=ROUND@TOUR@NAME[@FORMAT][,WORD]: a broadcast's round followed (lichess-api= before it),
        // its mosaic shown (#92); then WORD, another of these (broadcast-crosstable).
        auto spec = w.substr(17);
        if (const auto comma = spec.find(','); comma != std::string::npos) {
            focus_next_ = spec.substr(comma + 1);
            spec        = spec.substr(0, comma);
        }
        std::vector<std::string> parts;
        for (std::size_t at = 0;;) {
            const auto end = spec.find('@', at);
            parts.push_back(spec.substr(at, end == std::string::npos ? std::string::npos : end - at));
            if (end == std::string::npos) break;
            at = end + 1;
        }
        lichess::BroadcastEntry entry;
        entry.round.id   = parts[0];
        entry.round.name = "Round";
        if (parts.size() > 1) entry.tour.id = parts[1];
        if (parts.size() > 2) entry.tour.name = parts[2];
        if (parts.size() > 3) entry.tour.format = parts[3];
        page_ = Page::Lichess;
        followBroadcast(entry, true);
    });
    words.on({"broadcast-crosstable"}, [&] {  // the followed round's crosstable in place of its mosaic (#92)
        broadcast_crosstable_ = true;
    });
    words.on({"fide-server=http://127.0.0.1:*"}, [&] {
        // fide-server=http://127.0.0.1:PORT[,WORD]: a test's server in place of ratings.fide.com (#88), on this
        // machine only; then WORD, another of these.
        const auto comma = w.find(',');
        const auto from  = std::string_view("fide-server=").size();
        fide::setServer(w.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
        if (comma != std::string::npos) focus_next_ = w.substr(comma + 1);
    });
    words.on({"databases-import-into=*"}, [&] {  // databases-import-into=FILE: into the first database
        page_        = Page::Databases;
        db_selected_ = 0;
        importIntoDatabase(0, std::filesystem::path(w.substr(22)));
    });
    words.on({"databases-import=*"}, [&] {  // databases-import=FILE: into a new database named after it
        page_ = Page::Databases;
        importIntoDatabase(-1, std::filesystem::path(w.substr(17)));
    });
    words.on({"openings", "openings-theory"}, [&] {  // the Openings page; its Theory part (#77)
        page_            = Page::Openings;
        openings_theory_ = w == "openings-theory";
    });
    words.on({"theory-add=*"}, [&] {
        // theory-add=FILE[,WORD]: FILE added to the theory (the profile left as it was in a run for captures);
        // then WORD, another of these.
        const auto comma = w.find(',');
        addTheoryFile(std::filesystem::path(w.substr(11, comma == std::string::npos ? std::string::npos : comma - 11)));
        if (comma != std::string::npos) focus_next_ = w.substr(comma + 1);
    });
    words.on({"training", "training-new"}, [&] {  // a session of the active repertoire's training (#76)
        if (repertoires().active() >= 0)
            startTraining(static_cast<std::size_t>(repertoires().active()),
                          w == "training" ? TrainingRun::Kind::Due : TrainingRun::Kind::New);
    });
    words.on({"training-page", "training-puzzles", "puzzles-online"}, [&] {
        // The Training page (#80): its Today part, its Puzzles part, the puzzles online chosen.
        page_           = Page::Training;
        training_tab_   = w == "training-page" ? 0 : 1;
        puzzles_online_ = w == "puzzles-online";
    });
    words.on({"puzzles-file=*"}, [&] {  // puzzles-file=FILE: Lichess's puzzle database read from it
        page_         = Page::Training;
        training_tab_ = 1;
        startOfflinePuzzles(w.substr(13));
    });
    words.on({"puzzles-series", "puzzles-series=*"}, [&] {
        // puzzles-series[=THEME]: a series of the offline puzzles at once, of THEME (a Lichess key) or any.
        puzzles_online_ = false;
        puzzles_theme_  = w.size() > 15 ? w.substr(15) : std::string();
        startPuzzles();
    });
    words.on({"review-all"}, [&] {  // the day's review, every kind one after the other (#80)
        startReview();
    });
    words.on({"training-mistakes", "training-guess"}, [&] {
        // The Training page's parts of #81: the mistakes as exercises, guessing the moves.
        page_         = Page::Training;
        training_tab_ = w == "training-mistakes" ? 2 : 3;
    });
    words.on({"mistakes-session", "mistakes-review"}, [&] {
        // A session of the exercises of the user's mistakes: today's, or the day's review of them alone (#81).
        if (!startMistakes(w == "mistakes-review")) {
            page_         = Page::Training;
            training_tab_ = 2;
        }
    });
    words.on({"training-endgames", "training-vision"}, [&] {
        // The Training page's parts of #82: the endgames against the tables and the mates, board vision.
        page_         = Page::Training;
        training_tab_ = w == "training-endgames" ? 4 : 5;
    });
    words.on({"endgame=*"}, [&] {  // endgame=ID: that endgame of the catalogue against the tables
        if (const auto* e = endgameEntry(w.substr(8))) {
            if (!endgameNeed(e->fen).empty() || !startEndgame(*e)) {
                page_         = Page::Training;
                training_tab_ = 4;
                endgame_need_ = e->id;
            }
        }
    });
    words.on({"endgames-review"}, [&] {  // the endgames missed, due today (#82)
        if (!startEndgameReview()) {
            page_         = Page::Training;
            training_tab_ = 4;
        }
    });
    words.on({"vision-coordinates", "vision-blindfold", "vision-lines"}, [&] {
        if (w == "vision-coordinates")
            startCoordinates();
        else if (w == "vision-blindfold")
            startBlindfold();
        else
            startHeadLines();
    });
    words.on({"guess-board", "guess-random"}, [&] {
        // Guessing the moves of the Board page's game from the position shown, or of a random game (#81).
        if (w == "guess-board")
            startGuessBoard();
        else
            (void)startGuessRandom();
    });
    words.on({"board-analysis*"}, [&] {
        // The study Board's Analysis tab (#78); board-analysis=FILE[@PLY]: FILE on the board (at that ply once
        // read), analysed; board-analysis-menu[=FILE[@PLY]]: its "+ An engine" open too.
        toStudyBoard();
        board_side_tab_ = 1;
        if (const auto eq = w.find('='); eq != std::string::npos) {
            auto file = w.substr(eq + 1);
            if (const auto at = file.rfind('@'); at != std::string::npos && at > 1) {
                ply_when_read_ = std::atoi(file.c_str() + at + 1);
                file           = file.substr(0, at);
            }
            dropFile(file);
            setBoardAnalysis(true);
        }
        if (w.rfind("board-analysis-menu", 0) == 0) side_menu_open_ = true;
    });
    words.on({"board-tree", "board-references", "board-tree=*", "board-notation=*"}, [&] {
        // The Chess space's column at that tab; board-tree=FILE[@PLY], board-notation=FILE[@PLY]: FILE on the
        // board (at that ply once read).
        toStudyBoard();
        board_side_tab_ = w.rfind("board-tree", 0) == 0 ? 2 : w.rfind("board-notation", 0) == 0 ? 0 : 3;
        if (const auto eq = w.find('='); eq != std::string::npos) {
            auto file = w.substr(eq + 1);
            if (const auto at = file.rfind('@'); at != std::string::npos && at > 1) {
                ply_when_read_ = std::atoi(file.c_str() + at + 1);
                file           = file.substr(0, at);
            }
            dropFile(file);
        }
    });
    words.on({"export*=*"}, [&] {
        // The Export sheet (#93) on a format (html, diagram, gif, pdf, pgn): export=FILE[@PLY]@FORMAT, FILE on the
        // study Board at PLY once read; export-db=FORMAT[@all], the first database's chosen game, or its filter's
        // games; export-repertoire=N[@FORMAT], the Openings page's repertoire N printed;
        // export-dossier=NAME@FORMAT, export-report=FILE@PLY@FORMAT, export-crosstable=WORDS@FORMAT, once the
        // report is made.
        auto spec       = w.substr(w.find('=') + 1);
        const auto kind = w.substr(0, w.find('='));
        int format      = static_cast<int>(ExportFormat::Html);
        // A gesture once the sheet shows: @print, or @save:PATH.
        if (spec.size() > 6 && spec.substr(spec.size() - 6) == "@print") {
            export_action_ = "print";
            spec.resize(spec.size() - 6);
        } else if (const auto save = spec.find("@save:"); save != std::string::npos) {
            export_action_ = "save:" + spec.substr(save + 6);
            spec.resize(save);
        }
        bool all        = false;
        const auto take = [&] {
            const auto at = spec.rfind('@');
            if (at == std::string::npos) return;
            const auto name = spec.substr(at + 1);
            if (name == "all") {
                all  = true;
                spec = spec.substr(0, at);
                return;
            }
            format = name == "diagram" ? static_cast<int>(ExportFormat::Diagram)
                     : name == "gif"   ? static_cast<int>(ExportFormat::Gif)
                     : name == "pdf"   ? static_cast<int>(ExportFormat::Pdf)
                     : name == "pgn"   ? static_cast<int>(ExportFormat::Pgn)
                                       : static_cast<int>(ExportFormat::Html);
            spec   = spec.substr(0, at);
        };
        if (kind == "export-db") {
            if (spec.size() > 4 && spec.substr(spec.size() - 4) == "@all") {
                all = true;
                spec.resize(spec.size() - 4);
            }
            spec = "x@" + spec;
            take();
            page_               = Page::Databases;
            db_selected_        = 0;
            db_list_            = true;
            export_when_listed_ = format;
            export_listed_all_  = all;
        } else if (kind == "export-repertoire") {
            take();
            page_ = Page::Openings;
            repertoires().setActive(std::atoi(spec.c_str()));
            openRepertoirePrint(static_cast<std::size_t>(std::max(0, std::atoi(spec.c_str()))));
        } else if (kind == "export-dossier") {
            take();
            page_ = Page::Databases;
            study::DossierSubject subject;
            subject.name = spec;
            openDossier(std::move(subject), false);
            export_when_made_ = {0, format};
        } else if (kind == "export-crosstable") {
            take();
            const auto at       = spec.rfind('@');
            crosstable_request_ = std::make_pair(at == std::string::npos ? spec : spec.substr(0, at),
                                                 at == std::string::npos ? std::string() : spec.substr(at + 1));
            page_               = Page::Databases;
            db_selected_        = 0;
            db_list_            = true;
            db_tournaments_     = true;
            export_when_made_   = {2, format};
        } else if (kind == "export-report") {
            take();
            toStudyBoard();
            if (const auto at = spec.rfind('@'); at != std::string::npos && at > 1) {
                ply_when_read_ = std::atoi(spec.c_str() + at + 1);
                spec           = spec.substr(0, at);
            }
            report_when_read_ = 0;
            export_when_made_ = {1, format};
            dropFile(spec);
        } else {
            take();
            toStudyBoard();
            if (const auto at = spec.rfind('@'); at != std::string::npos && at > 1) {
                ply_when_read_ = std::atoi(spec.c_str() + at + 1);
                spec           = spec.substr(0, at);
            }
            export_when_read_ = format;
            dropFile(spec);
        }
    });
    words.on({"opening-report", "opening-report=*"}, [&] {
        // opening-report[=FILE@PLY[@TAB]]: the report of the Board page's position (#91); FILE on the board first,
        // at PLY once read, the report then at TAB (lines, plans, games, novelties, summary).
        toStudyBoard();
        if (const auto eq = w.find('='); eq != std::string::npos) {
            auto spec         = w.substr(eq + 1);
            int tab           = 0;
            const auto tab_at = spec.rfind('@');
            if (tab_at != std::string::npos && tab_at + 1 < spec.size() &&
                !std::isdigit(static_cast<unsigned char>(spec[tab_at + 1]))) {
                const auto name = spec.substr(tab_at + 1);
                tab  = name == "plans" ? 1 : name == "games" ? 2 : name == "novelties" ? 3 : name == "summary" ? 4 : 0;
                spec = spec.substr(0, tab_at);
            }
            if (const auto at = spec.rfind('@'); at != std::string::npos && at > 1) {
                ply_when_read_ = std::atoi(spec.c_str() + at + 1);
                spec           = spec.substr(0, at);
            }
            report_when_read_ = tab;
            dropFile(spec);
        } else {
            openOpeningReport(Page::Board);
        }
    });
    words.on({"lichess-finished=*"}, [&] {
        // lichess-finished=FILE: a game of the user's that ended, as Lichess would send it (#49), its PGN read
        // from FILE; nothing asked of Lichess.
        std::ifstream in(w.substr(17), std::ios::binary);
        lichess::Arrival a;
        a.pgn.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        study::ImportedGame g;
        if (study::readImportedGame(a.pgn, g)) {
            a.plies  = g.plies;
            a.result = g.result;
        }
        a.analysed = a.pgn.find("[%eval ") != std::string::npos;
        a.added    = true;
        FinishedGame f;
        f.game.id              = "abcd1234";
        f.game.full_id         = "abcd1234wxyz";
        f.game.opponent        = "Partner_3";
        f.game.opponent_rating = 2016;
        f.game.speed           = "blitz";
        f.base                 = "Lichess · rs_sample_0000";
        std::promise<lichess::Arrival> came;  // as if Lichess had just sent it
        came.set_value(std::move(a));
        f.arrival = came.get_future();
        lichess_finished_.push_back(std::move(f));
    });
    words.on({"lichess-api=http://127.0.0.1:*"}, [&] {
        // lichess-api=http://127.0.0.1:PORT[,WORD]: a test's server in place of lichess.org's API, explorer
        // (under /explorer), tablebase (under /tb) and external engines' work host (under /engine), on this
        // machine only; then WORD, another of these.
        const auto comma   = w.find(',');
        const auto base    = w.substr(12, comma == std::string::npos ? std::string::npos : comma - 12);
        lichess::servers() = lichess::Servers{base, base + "/tb", base + "/explorer", base + "/engine"};
        if (comma != std::string::npos) focus_next_ = w.substr(comma + 1);
    });
    words.on({"lichess-playing=*"}, [&] {
        // lichess-playing=live|correspondence[@FILE[@PLY]][,WORD]: a game of the user's in progress, as Lichess
        // would list it (#48): live, or by correspondence at the position of the Board page's game at PLY (where
        // it is, without PLY); FILE (.txt, .fen) read on the board first; then WORD, another of these.
        auto spec = w.substr(16);
        if (const auto comma = spec.find(','); comma != std::string::npos) {
            focus_next_ = spec.substr(comma + 1);
            spec        = spec.substr(0, comma);
        }
        const auto at = spec.find('@');
        int ply       = -1;
        if (at != std::string::npos) {
            auto file       = spec.substr(at + 1);
            const auto last = file.rfind('@');
            if (last != std::string::npos) {
                ply  = std::atoi(file.c_str() + last + 1);
                file = file.substr(0, last);
            }
            page_ = Page::Board;
            dropFile(file);
        }
        lichess::PlayingGame g;
        g.id              = "abcd1234";
        g.full_id         = "abcd1234wxyz";
        g.opponent        = "Partner_3";
        g.opponent_rating = 2016;
        const bool live   = spec.substr(0, at) == "live";
        g.speed           = live ? "blitz" : "correspondence";
        g.my_turn         = !live;
        g.seconds_left    = live ? 161 : 100800;
        g.fen = ply >= 0 ? boardAt(board_.game, board_.chess960, std::min<int>(ply, board_.game.moves.size())).getFen()
                         : boardFen();
        lichess_playing_.push_back(g);
    });
    words.on({"lichess", "lichess-studies"}, [&] {  // the Lichess page; its Studies card open (#79)
        page_ = Page::Lichess;
        if (w == "lichess-studies") {
            loadStudiesState();
            studies_.shown = studies_.opened = true;
        }
    });
    words.on({"lichess-tv"}, [&] {  // the Lichess page, its card of Lichess TV's channels (#86)
        page_ = Page::Lichess;
        broadcast_round_.clear();
    });
    words.on({"lichess-tournaments"}, [&] {  // the Lichess page scrolled to its card of tournaments (#95)
        page_ = Page::Lichess;
        broadcast_round_.clear();
        arena_card_y_ = -2;
    });
    words.on({"arena-mosaic=*", "swiss-mosaic=*", "play-arena=*", "play-swiss=*"}, [&] {
        // arena-mosaic=ID[@NAME], swiss-mosaic=ID[@NAME]: a tournament followed in the mosaic;
        // play-arena=ID[@NAME], play-swiss=ID[@NAME]: the Play page's Tournaments part on it (#95), with
        // lichess-api= before them.
        const auto eq   = w.find('=');
        const auto kind = w.substr(0, eq);
        const auto spec = w.substr(eq + 1);
        const auto at   = spec.find('@');
        const auto id   = spec.substr(0, at);
        const auto name = at == std::string::npos ? std::string() : spec.substr(at + 1);
        if (kind == "arena-mosaic" || kind == "swiss-mosaic") {
            followTournament(id, name, kind == "swiss-mosaic");
        } else if (kind == "play-arena") {
            chooseArena(id, name);
        } else {
            lichess::SwissInfo s;
            s.id   = id;
            s.name = name;
            chooseSwiss(s);
            swiss_play_->info = {};  // asked of Lichess
        }
    });
    words.on({"play-tournaments"}, [&] {  // the Play page's "On Lichess › Tournaments" part (#95)
        page_        = Page::Play;
        play_part_   = 1;
        online_part_ = 1;
    });
    words.on({"tv-mosaic=*"}, [&] {  // tv-mosaic=CHANNEL: a channel of Lichess TV in the mosaic (#86)
        openTvMosaic(w.substr(10));
    });
    words.on({"tv-board=*"}, [&] {
        // tv-board=CHANNEL:ID: a game of a channel of Lichess TV followed on the Board, as its featured game (#86).
        const auto spec  = w.substr(9);
        const auto colon = spec.find(':');
        if (colon != std::string::npos && lichess::isTvChannel(spec.substr(0, colon)))
            followTvOnBoard(spec.substr(0, colon), spec.substr(colon + 1), true, spec.substr(colon + 1), {});
    });
    words.on({"board-to-study=*"}, [&] {  // board-to-study=FILE: on the Board, sent to a study (#79)
        std::ifstream in(w.substr(15), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        toStudyBoard();
        readOnBoard(text, std::filesystem::path(w.substr(15)).filename().string());
        openStudySend();
    });
    words.on({"link=*"}, [&] {  // link=URL: a Lichess link, as pasted on the Board page (#47)
        page_ = Page::Board;
        (void)offerLichessLink(w.substr(5));
    });
    words.on({"board-explain=*"}, [&] {
        // board-explain=FILE: its first game on the study Board, reviewed once read, then every move explained, the
        // Explained tab shown (#94).
        toStudyBoard();
        board_side_tab_       = 0;
        board_.open_tab       = 4;
        review_when_read_     = true;
        explain_after_review_ = true;
        dropFile(w.substr(14));
    });
    words.on({"endgame-check=*"}, [&] {
        // endgame-check=DIR: that finished tournament's endgames checked against the tables, as its menu does
        // (#94).
        startEndgameCheck(w.substr(14));
    });
    words.on({"board-visit=*"}, [&] {
        // board-visit=FILE: its first game on the study Board, its guided visit started once its review is read
        // (#94).
        toStudyBoard();
        visit_when_read_ = true;
        dropFile(w.substr(12));
    });
    words.on({"board-tab=explained"}, [&] {  // the Board page's moves card on its Explained tab (#94)
        board_.open_tab = 4;
    });
    words.on({"play", "play-start"}, [&] {  // the Play page (#83); a game against an engine started
        page_      = Page::Play;
        play_part_ = 0;
        if (w == "play-start") startEngineGame();
    });
    words.on({"play-lichess"}, [&] {  // the Play page's "On Lichess" part (#84)
        page_      = Page::Play;
        play_part_ = 1;
    });
    words.on({"engines-bot", "bot-online"}, [&] {
        // The Engines page, the first engine's card: its bot's setup started (#85); bot-online: the bot put online,
        // its account linked already.
        page_            = Page::Engines;
        selected_engine_ = engines_.empty() ? -1 : 0;
        loadBot();
        if (w == "engines-bot" && !engines_.empty()) {
            bot_setup_engine_ = engines_[0].name;
            bot_step_         = BotStep::Account;
        }
        if (w == "bot-online") setBotOnline(true);
    });
    words.on({"lichess-game=*"}, [&] {
        // lichess-game=ID[@black]: a game of the user's on Lichess played on the Board (#84), its stream asked of
        // the server --focus lichess-api= named.
        const auto spec = w.substr(13);
        const auto at   = spec.find('@');
        openOnlineGame(spec.substr(0, at), at == std::string::npos || spec.substr(at + 1) != "black");
    });
    words.on({"books-openings=*"}, [&] {  // books-openings=FILE: Lichess's named openings book there
        page_ = Page::Books;
        writeOpeningSuiteBook(w.substr(15));
    });
    words.on({"twic-server=http://127.0.0.1:*"}, [&] {
        // twic-server=http://127.0.0.1:PORT[,WORD]: a test's server in place of theweekinchess.com (#71), on
        // this machine only; then WORD, another of these.
        const auto comma = w.find(',');
        const auto from  = std::string_view("twic-server=").size();
        twic::setServer(w.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
        if (comma != std::string::npos) focus_next_ = w.substr(comma + 1);
    });
    words.on({"lichess-database=http://127.0.0.1:*"}, [&] {
        // lichess-database=http://127.0.0.1:PORT[,WORD]: a test's server in place of database.lichess.org (#73).
        const auto comma = w.find(',');
        const auto from  = std::string_view("lichess-database=").size();
        lichess::setDatabaseServer(w.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
        if (comma != std::string::npos) focus_next_ = w.substr(comma + 1);
    });
    words.on({"chesscom-server=http://127.0.0.1:*"}, [&] {
        // chesscom-server=http://127.0.0.1:PORT[,WORD]: a test's server in place of api.chess.com (#74).
        const auto comma = w.find(',');
        const auto from  = std::string_view("chesscom-server=").size();
        chesscom::setServer(w.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
        if (comma != std::string::npos) focus_next_ = w.substr(comma + 1);
    });
    words.on({"lichess-evals"}, [&] {  // the card of Lichess's evaluations kept offline (#58)
        page_                = Page::Lichess;
        offline_evals_focus_ = true;
    });
    words.on({"lichess-evals=*"}, [&] {
        // lichess-evals=FILE[@openings]: their preparation from that file (a .jsonl.zst), every position or the
        // openings only.
        page_                = Page::Lichess;
        offline_evals_focus_ = true;
        auto file            = w.substr(14);
        const bool openings  = file.size() > 9 && file.substr(file.size() - 9) == "@openings";
        if (openings) file.resize(file.size() - 9);
        offline_evals_choice_ = openings ? 1 : 0;
        startOfflineEvals(file, openings ? study::EVALS_OPENING_MEN : 0);
    });
    words.on({"databases-chesscom"}, [&] {  // the sheet of a chess.com player's games
        page_           = Page::Databases;
        chesscom_sheet_ = true;
    });
    words.on({"databases-broadcasts"}, [&] {  // the sheet of Lichess's broadcasts by month
        page_            = Page::Databases;
        broadcast_sheet_ = true;
    });
    words.on({"databases-twic"}, [&] {  // TWIC's sheet on the Databases page
        page_       = Page::Databases;
        twic_sheet_ = true;
    });
    words.on({"books-evaluated"}, [&] {  // the form of Lichess's evaluated positions
        page_ = Page::Books;
        openEvaluatedSuite();
    });
    words.on({"books-evaluated=*"}, [&] {
        // books-evaluated=FILE.zst: Lichess's evaluations database read from that file; =FILE.epd: the book
        // written there from what the profile kept.
        page_ = Page::Books;
        openEvaluatedSuite();
        const auto file = w.substr(16);
        if (file.size() > 4 && file.substr(file.size() - 4) == ".epd") {
            writeEvaluatedSuiteBook(file);
        } else {
            startEvalsReading(file);
        }
    });
    return words.applied();
}

void App::handleChessDialog(const Dialogs::Answer& answer) {
    const auto& path = answer.path;
    switch (answer.purpose) {
        case Dialogs::Purpose::Export:  // the Export sheet's file (#93)
            exportTo(path);
            break;
        case Dialogs::Purpose::TheoryPgn:  // a theory study (#77)
            addTheoryFile(path);
            break;
        case Dialogs::Purpose::TheoryFind:
            if (theory_chosen_ >= 0 && static_cast<std::size_t>(theory_chosen_) < theory().studies().size())
                theory().relocate(static_cast<std::size_t>(theory_chosen_), path);
            break;
        case Dialogs::Purpose::CtgPolyglotBook:
            if (selected_book_ >= 0 && selected_book_ < static_cast<int>(books_.size()) &&
                study::isCtg(books_[selected_book_].path))
                startCtgJob(CtgJobKind::Polyglot, books_[selected_book_].path, path);
            break;
        case Dialogs::Purpose::OpeningSuiteBook:
            writeOpeningSuiteBook(path);
            break;
        case Dialogs::Purpose::EvaluatedSuiteBook:
            writeEvaluatedSuiteBook(path);
            break;
        case Dialogs::Purpose::EvalsFile:
            startEvalsReading(path);
            break;
        case Dialogs::Purpose::OfflineEvalsFile:
            startOfflineEvals(path, offline_evals_choice_ == 1 ? study::EVALS_OPENING_MEN : 0);
            break;
        case Dialogs::Purpose::OfflinePuzzlesFile:
            startOfflinePuzzles(path);
            break;
        case Dialogs::Purpose::LichessMonthFile:
            startMonthReading(path);
            break;
        case Dialogs::Purpose::AppendBoardPgn:
            try {
                // After the file's games, a blank line between.
                if (chessBoard()) fillBoardEco();
                std::string text;
                {
                    std::ifstream in(std::filesystem::path(path), std::ios::binary);
                    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                }
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
                    text.pop_back();
                if (!text.empty()) text += "\n\n";
                text += gamePgn(board_.tree, board_.chess960);
                tour::replaceFile(std::filesystem::path(path), text);
                board_.changes.saved(std::filesystem::path(path).filename().string());
                boardToast(trf("Saved at the end of “{}”", std::filesystem::path(path).filename().string()));
            } catch (const std::exception& e) {
                board_.message       = trf("Cannot write {}: {}", path, e.what());
                board_.message_error = true;
            }
            break;
        case Dialogs::Purpose::AssistantModel: {
            // A GGUF file in place of the catalogue's model: loaded at the next question.
            auto settings       = assistant_->settings();
            settings.model_file = path;
            settings.not_now    = false;
            assistant_->setSettings(settings);
            assistant_->unload();  // after: a request that starts meanwhile loads the file chosen
            assistant_->saveSettings();
            break;
        }
        case Dialogs::Purpose::SaveBoardPgn:
            try {
                if (chessBoard()) fillBoardEco();
                tour::replaceFile(std::filesystem::path(path), gamePgn(board_.tree, board_.chess960));
                board_.changes.saved(std::filesystem::path(path).filename().string());
                board_.message       = trf("Saved in {}", path);
                board_.message_error = false;
            } catch (const std::exception& e) {
                board_.message       = trf("Cannot write {}: {}", path, e.what());
                board_.message_error = true;
            }
            break;
        case Dialogs::Purpose::ImportGames:
            importIntoDatabase(db_selected_, std::filesystem::path(path));
            break;
        case Dialogs::Purpose::AddDatabase:
            try {
                db_selected_ = static_cast<int>(shelf().add(std::filesystem::path(path)));
            } catch (const std::exception& e) {
                db_message_ = e.what();
            }
            break;
        case Dialogs::Purpose::FideList:
            startFideList(std::filesystem::path(path));
            break;
        // fastchess-gui's, answered by handleDialogs() before it calls this: named rather than a default, so that
        // the compiler tells of a purpose added without its case in either function (-Wswitch).
        case Dialogs::Purpose::AddEngine:
        case Dialogs::Purpose::EngineCommand:
        case Dialogs::Purpose::EngineDir:
        case Dialogs::Purpose::Book:
        case Dialogs::Purpose::Tablebases:
        case Dialogs::Purpose::TablebasesFolder:
        case Dialogs::Purpose::ExportGames:
        case Dialogs::Purpose::ExportAll:
        case Dialogs::Purpose::OpenTournament:
        case Dialogs::Purpose::ExportStandings:
        case Dialogs::Purpose::ImportEngineList:
        case Dialogs::Purpose::ImportEngineFolder:
        case Dialogs::Purpose::ImportRatingList:
        case Dialogs::Purpose::ExportRatings:
        case Dialogs::Purpose::ExportRatingHtml:
        case Dialogs::Purpose::ExportRatingMarkdown:
        case Dialogs::Purpose::ExportRatingPgn:
        case Dialogs::Purpose::ImportRatingPgn:
        case Dialogs::Purpose::ExportHeadToHead:
        case Dialogs::Purpose::ExportOpenings:
        case Dialogs::Purpose::AddBook:
        case Dialogs::Purpose::BookFile:
        case Dialogs::Purpose::SaveCleanBook:
        case Dialogs::Purpose::Chess324Book:
        case Dialogs::Purpose::Chess324SymmetricalBook:
        case Dialogs::Purpose::BuildBookPgn:
        case Dialogs::Purpose::BuildBookBin:
        case Dialogs::Purpose::SuiteFile:
        case Dialogs::Purpose::ExportSuiteCsv:
        case Dialogs::Purpose::TournamentsFolder:
        case Dialogs::Purpose::PgnOut:
        case Dialogs::Purpose::ExportSubmission:
        case Dialogs::Purpose::EngineOption:
        case Dialogs::Purpose::ConsoleLog:
        case Dialogs::Purpose::BoardPgn:
        case Dialogs::Purpose::EngineLogo:
        case Dialogs::Purpose::PieceFolder:
        case Dialogs::Purpose::MachineKey:
        case Dialogs::Purpose::MachineProgram:
        case Dialogs::Purpose::MachineEngineBuild:
            break;
    }
}

}  // namespace fcg
