#pragma once

// The databases of the store's fuzz targets (#102): games of our own (a site that is the game's address, clocks and
// evaluations, a position of its own, Chess960, comments and a variation, an illegal move), and what the pages do with
// a database once it has opened, every game read. Nothing of it may crash, read outside the files, or throw where
// nothing catches it; the contracts of what is read are checked (std::abort(): a crash for libFuzzer).

#include <atomic>
#include <cstdlib>
#include <string>
#include <vector>

#include <chess.hpp>
#include <study/dossier.hpp>
#include <study/import.hpp>
#include <study/query.hpp>
#include <study/scan_board.hpp>
#include <study/search.hpp>
#include <study/store.hpp>

namespace fcg::fuzz {

inline constexpr const char* STUDY_GAMES = R"pgn([Event "Rated Blitz game"]
[Site "https://lichess.org/abcdefgh"]
[Date "2026.11.25"]
[Round "1"]
[White "Moreau, Claire"]
[Black "Duval, Marc"]
[Result "1-0"]
[WhiteElo "2010"]
[BlackElo "1985"]
[ECO "C50"]
[TimeControl "180+2"]

1. e4 { [%eval 0.2] [%clk 0:03:00] } 1... e5 { [%eval 0.3] [%clk 0:03:00] } 2. Nf3 { [%eval 0.2] [%clk 0:02:58] }
2... Nc6 { [%eval 0.3] [%clk 0:02:59] } 3. Bc4 { [%eval 0.1] [%clk 0:02:55] } 3... Bc5 { [%eval 0.2] [%clk 0:02:57] }
4. c3 { [%eval 0.3] [%clk 0:02:50] } 4... Nf6 { [%eval 0.2] [%clk 0:02:55] } 1-0

[Event "Club Cup"]
[Site "Lyon"]
[Date "2026.10.01"]
[Round "2"]
[White "Lindqvist, Erik"]
[Black "Moreau, Claire"]
[Result "0-1"]
[SetUp "1"]
[FEN "4k3/8/8/8/8/8/4P3/4K3 b - - 0 1"]

1... Kd7 2. e4 Ke6 3. Ke2 Ke5 4. Ke3 Kd6 0-1

[Event "Club Freestyle"]
[Site "Nantes"]
[Date "2026.09.12"]
[Round "3"]
[White "Duval, Marc"]
[Black "Lindqvist, Erik"]
[Result "1/2-1/2"]
[Variant "Chess960"]
[SetUp "1"]
[FEN "nrbbqkrn/pppppppp/8/8/8/8/PPPPPPPP/NRBBQKRN w KQkq - 0 1"]

1. e4 e5 2. Ng3 Ng6 3. Nb3 Nb6 1/2-1/2

[Event "Club Cup"]
[Site "Lyon"]
[Date "2026.10.02"]
[Round "4"]
[White "Brennan, Kate"]
[Black "Duval, Marc"]
[Result "*"]
[Annotator "Moreau, Claire"]
[Opening "Queen's Gambit"]

1. d4 d5 2. c4 { A gambit of our own. } 2... e6 (2... c6 3. Nf3) 3. Nc3 $1 Nf6 *

[Event "Club Cup"]
[Site "Lyon"]
[Date "2026.10.03"]
[Round "5"]
[White "Moreau, Claire"]
[Black "Brennan, Kate"]
[Result "1-0"]

1. e4 e5 2. Ke3 Nc6 1-0

[Event "Club Cup"]
[Site "Lyon"]
[Date "2026.10.04"]
[Round "6"]
[White "Duval, Marc"]
[Black "Moreau, Claire"]
[Result "1/2-1/2"]
[WhiteElo "1990"]
[BlackElo "2015"]

1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 6. Be3 e5 7. Nb3 Be6 8. f3 Be7 9. Qd2 O-O 10. O-O-O Nbd7
11. g4 b5 12. g5 b4 13. Ne2 Ne8 14. f4 a5 15. f5 a4 1/2-1/2
)pgn";

inline std::vector<study::ImportedGame> studyGames() {
    std::vector<study::ImportedGame> games;
    for (const auto text : study::splitGames(STUDY_GAMES)) {
        study::ImportedGame g;
        if (study::readImportedGame(text, g)) games.push_back(std::move(g));
    }
    return games;
}

// Every game of the database read as the pages read it: its row and names, the whole game and its PGN, a tag, its
// start, its moves replayed from it as the searches replay them; the list filtered by a name and sorted; a position
// search, a player's dossier and the check.
inline void readEverything(study::Database& db) {
    using namespace study;
    const auto snap = db.snapshot();
    for (GameId id = 0; id < snap->games(); ++id) {
        (void)snap->row(id);
        (void)snap->numbers(id);
        const auto names = snap->rowIds(id);
        if (names.white >= snap->names() || names.black >= snap->names() || names.event >= snap->names() ||
            names.site >= snap->names() || names.round >= snap->names())
            std::abort();
        (void)snap->shownName(names.white);
        const auto game = snap->game(id);
        if (game.plies > game.moves.size() || game.clocks.size() > game.plies || game.evals.size() > game.plies)
            std::abort();
        (void)importedPgn(game);
        (void)snap->tag(id, "Site");
        (void)snap->signature(id);
        (void)snap->state(id);
        (void)snap->source(id);
        (void)snap->storedBytes(id);
        const auto moves = snap->gameMoves(id);
        if (moves.plies > moves.codes.size()) std::abort();
        ScanBoard board;
        if (!startOf(*snap, id, moves.flags, board)) continue;
        std::size_t p = 0;
        for (uint32_t ply = 0; ply < moves.plies && p < moves.codes.size(); ++ply) {
            const int count = board.codeCount();
            int code        = moves.codes[p++];
            if (count > 256) {
                if (p >= moves.codes.size()) break;
                code = code << 8 | moves.codes[p++];
            }
            if (code >= count) break;
            board.play(board.decode(code));
        }
    }
    Filter named;
    named.player       = "moreau";
    named.with_deleted = true;
    (void)makeView(snap, named, SortBy::White, false, 1);
    (void)makeView(snap, Filter{}, SortBy::Date, true, 1);

    ScanBoard after_e4;
    after_e4.setFen(chess::constants::STARTPOS, false);
    after_e4.play(*after_e4.parseSan("e4"));
    (void)searchAll(snap, PositionQuery{after_e4}, 1);
    std::atomic<bool> cancel{false};
    const DossierSubject subject{"Moreau, Claire"};
    DossierOptions options;
    options.threads = 1;
    DossierProgress progress;
    (void)makeDossier({findSubject("Fuzz", snap, subject)}, subject, options, progress, cancel);
    CheckProgress check;
    (void)db.check(check, cancel, 1);
}

}  // namespace fcg::fuzz
