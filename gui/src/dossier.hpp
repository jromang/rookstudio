#pragma once

// A player's dossier (#90, mockup M13, board 3) behind its page (view_dossier.cpp): its subject found in the user's
// databases through their tables of names and FIDE's rating list, the dossier made in the background from their
// games (study/dossier.hpp), its pawn structures named by the assistant's facts, what the user's reviews of their games
// say, and the user's repertoires against them (the gaps of #52, over the board and over both sources). A choice the
// user keeps (a namesake of FIDE's list, their Lichess account) is written in the tables of names first. Nothing here
// leaves the machine.

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <study/dossier.hpp>
#include <study/fide_list.hpp>
#include <study/players.hpp>
#include <study/repertoire.hpp>
#include <study/store.hpp>

namespace fcg {

// The assistant's pawn formations (commentary/formation.hpp) as the dossier's structures: a structure's kind is its
// formation's number (commentary::Formation).
[[nodiscard]] const study::StructureNamer& dossierStructures();
// Lichess's named openings, the assistant's table, read once.
[[nodiscard]] const study::OpeningNames& dossierOpenings();

// What the user's reviews of their games say (the profile's analyses/, the Board page's reviews): how many of their
// games were reviewed, their accuracy in each phase (the mean of the games'), and where their clear mistakes (the
// moves marked ? and ??) fall. Nothing is analysed for it.
struct DossierReviews {
    uint32_t games = 0;
    std::array<std::optional<double>, 3> accuracy{};  // opening, middlegame, endgame
    std::array<uint32_t, 3> mistakes{};
};
[[nodiscard]] DossierReviews dossierReviews(const study::Dossier& dossier, const std::filesystem::path& analyses,
                                            const std::atomic<bool>& cancel);

// A database the dossier reads.
struct DossierBase {
    std::string name;
    std::filesystem::path folder;
    std::shared_ptr<study::Database> db;  // its table of names written when a choice is kept
};

// A player of FIDE's list who may be the subject, as the dossier's question shows them.
struct DossierNamesake {
    int64_t fide = 0;
    std::string name, federation, title;
    int born = 0, standard = 0, rapid = 0, blitz = 0;
    std::string list;  // the list's month
};

// What a dossier is made of and asked.
struct DossierAsk {
    study::DossierSubject subject;
    std::vector<DossierBase> bases;     // the user's databases
    std::optional<DossierBase> online;  // the Lichess account's database, when the source is ticked
    std::vector<std::pair<std::string, study::Repertoire>> repertoires;  // the user's, by name
    std::vector<std::string> user_names;                                 // Settings › Your names, the linked account
    std::filesystem::path analyses;                                      // the reviews
    std::shared_ptr<const study::FideList> fide_list;                    // none when not taken
    // A choice kept, written in the tables of names before the dossier is made: the namesake chosen, their Lichess
    // account linked; or the tables put back as they were before the last choice (Ctrl+Z).
    std::optional<DossierNamesake> keep_namesake;
    std::string link_lichess;
    std::vector<std::pair<std::filesystem::path, study::PlayerTable>> restore;
    int threads = 0;
};

// What they play, as #52 says it: their first moves with White, and with Black their answers to the two first moves
// they met most, by opening family.
struct DossierFirst {
    std::string san;
    uint64_t games = 0;
    double share   = 0;  // of their games with White
    double score   = 0;  // theirs in those games, 0 to 1
};
struct DossierAgainst {
    std::string san;  // the first move they met
    uint64_t games = 0;
    std::vector<study::DossierFamily> families;
};
struct DossierPlay {
    std::vector<DossierFirst> with_white;
    std::vector<DossierAgainst> with_black;
};

struct DossierGap {
    std::string repertoire;
    study::RepertoireGap gap;
    bool white = false;  // the repertoire's side: their games of the other colour
};

struct DossierReport {
    study::DossierSubject subject;            // as found: the tables' FIDE identity and Lichess account
    std::optional<DossierNamesake> identity;  // what FIDE's list (or a table) says of them, when matched
    std::shared_ptr<const study::Dossier> dossier;
    study::DossierFigures board, online;  // the parts' figures; online empty without the source
    DossierPlay board_play, online_play;
    uint32_t spellings = 0;  // their spellings in the databases, each written once
    // FIDE's list has several players of that name: which one? The one whose ratings fit their games first. Empty
    // when there is none to choose.
    std::vector<DossierNamesake> namesakes;
    DossierReviews reviews;
    std::vector<DossierGap> gaps, gaps_both;  // over the board; over both sources (with the Lichess source)
    // The tables of names before the choice kept, for Ctrl+Z; empty when none was kept.
    std::vector<std::pair<std::filesystem::path, study::PlayerTable>> tables_before;
    std::string error;  // why a choice could not be kept
    bool cancelled = false;
    double seconds = 0;
};

// The dossier made: the choice written first, the subject found, their games read, what they say.
[[nodiscard]] DossierReport makeDossierReport(DossierAsk ask, study::DossierProgress& progress,
                                              const std::atomic<bool>& cancel);

// The players of the user's databases whose names (the tables' or as written) contain each of `words` (letter case
// and accents aside), those of the tables of names first, then the shortest names: Ctrl+K's "Dossier: …". At most
// `most`.
struct DossierName {
    std::string name;
};
[[nodiscard]] std::vector<DossierName> dossierNames(
    const std::vector<std::shared_ptr<const study::Snapshot>>& snapshots, const std::string& words, std::size_t most,
    const std::atomic<bool>& cancel);

}  // namespace fcg
