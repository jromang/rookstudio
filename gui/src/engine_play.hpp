#pragma once

// Playing against an engine of the library (#83, mockup M12, board 4), without ImGui: the Play page's choice (the
// opponent, its strength, its openings, the user's colour and clock, a handicap), what a game takes of it, and what
// the profile keeps of the games played: play.json (the choice), play/games.jsonl (a line a game) and
// play/current.json (the game being played, resumed at the next launch when RookStudio closed during it).
//
// The engine's style is its openings (M12, board 9, point 6), never its own settings: the moves of the user's
// databases, of a player's games, the answers the user's repertoire prepared, or a book of the library, then the
// engine at the strength chosen.

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <chess.hpp>
#include <json.hpp>
#include <types/score.hpp>

#include "board_game.hpp"
#include "model.hpp"

namespace fcg {

// What the engine gives at the start: nothing, its f-pawn, its b-knight, its a-rook or its queen.
enum class Handicap { None, Pawn, Knight, Rook, Queen };
NLOHMANN_JSON_SERIALIZE_ENUM(Handicap, {{Handicap::None, "none"},
                                        {Handicap::Pawn, "pawn"},
                                        {Handicap::Knight, "knight"},
                                        {Handicap::Rook, "rook"},
                                        {Handicap::Queen, "queen"}})

// Where the engine's openings come from before its own moves.
enum class Openings { Own, Databases, Repertoire, Player, Book };
NLOHMANN_JSON_SERIALIZE_ENUM(Openings, {{Openings::Own, "own"},
                                        {Openings::Databases, "databases"},
                                        {Openings::Repertoire, "repertoire"},
                                        {Openings::Player, "player"},
                                        {Openings::Book, "book"}})

// The Play page's choice, kept in play.json.
struct PlaySettings {
    std::string engine;         // a library engine, by name
    int elo           = 1500;   // the Elo aimed at, for an engine with UCI_Elo
    int nodes         = 1000;   // the nodes a move, for an engine without it
    bool full         = false;  // at its full strength
    Openings openings = Openings::Own;
    std::string database;      // Openings::Databases: the database's folder; empty, every database with a tree
    std::string repertoire;    // Openings::Repertoire: its name
    std::string player;        // Openings::Player: the folder of the player's database ("Lichess · name")
    std::string book;          // Openings::Book: the library book's file
    bool vary  = true;         // at random among the moves played often enough, else the most played
    int colour = 0;            // the user's: 0 White, 1 Black, 2 at random
    GameClock clock{900, 10};  // the user's, and the engine's too with same_clock
    bool same_clock = true;
    GameClock engine_clock{900, 10};
    bool from_board   = false;  // from the Board page's position, else the start position
    Handicap handicap = Handicap::None;
    bool takebacks    = true;   // taking a move back allowed (counted)
    bool hints        = false;  // HINTS a game
    std::string games_folder;   // the database the games go into ("My games against engines"), made at the first
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PlaySettings, engine, elo, nodes, full, openings, database, repertoire,
                                                player, book, vary, colour, clock, same_clock, engine_clock, from_board,
                                                handicap, takebacks, hints, games_folder)

inline constexpr int HINTS              = 3;  // a game, when hints are allowed
inline constexpr uint64_t OPENING_LEAST = 5;  // a database's move played fewer times is left to the engine

[[nodiscard]] PlaySettings readPlaySettings(const std::filesystem::path& profile);
bool writePlaySettings(const std::filesystem::path& profile, const PlaySettings& settings);

// The UCI_Elo an engine accepts, from its options as the library read them; none without UCI_Elo.
struct EloRange {
    int min = 0, max = 0;
};
[[nodiscard]] std::optional<EloRange> eloRange(const EngineEntry& engine);

// The nodes a move the strength of an engine without UCI_Elo is chosen among: 1 to a million, by half decades.
[[nodiscard]] std::span<const int> nodeSteps();

// What the engine is told of the strength chosen: an Elo (UCI_LimitStrength and UCI_Elo), or nodes a move; neither
// at its full strength.
struct Strength {
    int elo        = 0;
    uint64_t nodes = 0;
    [[nodiscard]] bool full() const noexcept { return elo <= 0 && nodes == 0; }
    bool operator==(const Strength&) const = default;
};
[[nodiscard]] Strength strengthFor(const PlaySettings& settings, const EngineEntry& engine);
// The engine's copy given the options of an Elo: UCI_LimitStrength on, UCI_Elo; nothing for the others.
void applyStrength(EngineEntry& engine, const Strength& strength);
// "Elo 1,800", "1,000 nodes a move", "full strength".
[[nodiscard]] std::string strengthText(const Strength& strength);
// As the game's Strength tag writes it: "UCI_Elo 1800", "nodes 1000", "full"; the record's key for a level too.
[[nodiscard]] std::string strengthTag(const Strength& strength);

// `fen` with the man `handicap` names taken from `side`: its f-pawn (f2 or f7), its b-knight (b1 or b8), its a-rook
// (a1 or a8, castling on that side gone) or its queen (d1 or d8, else the only one it has). None when that man is not
// there, for Handicap::None, or for Chess960 (its castling rights name files).
[[nodiscard]] std::optional<std::string> handicapFen(const std::string& fen, Handicap handicap, chess::Color side,
                                                     bool chess960 = false);
// "a pawn (f)", "a knight (b)", "a rook (a)", "the queen", "nothing", in the interface's language.
[[nodiscard]] const char* handicapName(Handicap handicap);
// As the game's Handicap tag writes it: "pawn f7", "knight b1", "rook a8", "queen d1"; empty for none.
[[nodiscard]] std::string handicapTag(const std::string& fen, Handicap handicap, chess::Color side);

// A move the engine's openings give a position, with how often their source played it.
struct OpeningChoice {
    chess::Move move;
    uint64_t weight = 1;
    bool main       = false;  // the source's own first choice (a book's main move, a repertoire's first line)
};

// Of the choices played `least` times or more, the engine's move: the main one, else the most played; varying, one of
// them at random, as often as their source played them. None when no choice is played that often: the engine plays.
[[nodiscard]] std::optional<chess::Move> pickOpening(std::vector<OpeningChoice> choices, bool vary, uint64_t least,
                                                     std::mt19937& rng);

// The engine's openings as a game's thread asks them (BoardMatchSetup::book): the source's choices for a position,
// then pickOpening(). Used from one thread at a time.
class OpeningBook {
   public:
    using Source = std::function<std::vector<OpeningChoice>(const chess::Board&)>;
    OpeningBook(Source source, bool vary, uint64_t least, uint32_t seed)
        : source_(std::move(source)), vary_(vary), least_(least), rng_(seed) {}

    // The engine's move in UCI, none out of its openings.
    [[nodiscard]] std::optional<std::string> move(const chess::Board& board);

   private:
    Source source_;
    bool vary_      = true;
    uint64_t least_ = 1;
    std::mt19937 rng_;
};

// A game played against an engine, as play/games.jsonl keeps it: a line a game.
struct PlayedGame {
    int64_t at = 0;  // when it ended, seconds since the epoch
    std::string engine;
    std::string strength;  // strengthTag()
    std::string openings;  // as its Openings tag
    bool white = true;     // the user's colour
    std::string result;    // "1-0", "0-1", "1/2-1/2", "*" for a game stopped
    std::string termination;
    int plies     = 0;
    int takebacks = 0;
    int hints     = 0;
    bool aids     = false;  // shown during the game: its result is not counted
    std::string handicap;   // its Handicap tag
    std::string key;        // reviewKey(): the exercises of its mistakes found by it
    std::string final_fen;  // where it ended
    std::string database;   // where it was saved; empty when it was not
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PlayedGame, at, engine, strength, openings, white, result, termination,
                                                plies, takebacks, hints, aids, handicap, key, final_fen, database)

void appendPlayedGame(const std::filesystem::path& folder, const PlayedGame& game);
[[nodiscard]] std::vector<PlayedGame> readPlayedGames(const std::filesystem::path& folder);

// 1 the user won, 0 drew, -1 lost; none for a game without a result.
[[nodiscard]] std::optional<int> userScore(const PlayedGame& game);

// The user's games against an engine at a level: those with a result and without aids counted, those with aids
// apart; the last one lost.
struct PlayTally {
    int games = 0, won = 0, drawn = 0, lost = 0;
    int with_aids = 0;
    std::optional<PlayedGame> last_lost;
};
[[nodiscard]] PlayTally playTally(const std::vector<PlayedGame>& games, const std::string& engine,
                                  const std::string& strength);

// Where a game ended, as one says it: "the opening", "the middlegame", "a rook endgame"…, from its last position and
// its length.
[[nodiscard]] std::string endingOf(const std::string& fen, int plies);

// The game being played, kept in play/current.json at each move: RookStudio closed during it, it is resumed at the
// next launch.
struct KeptGame {
    PlaySettings settings;  // as the game was started (its colour drawn when at random)
    std::string engine;
    Strength strength;
    std::string openings_tag;
    std::string openings_text;  // as the game said it, in the interface's language
    bool white    = true;       // the user's colour
    bool chess960 = false;
    std::string start_fen;
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<std::string> moves;                     // UCI
    std::vector<int64_t> times;                         // what each took, ms
    std::vector<std::array<int64_t, 2>> clocks_before;  // White's and Black's clocks before each move
    std::array<int64_t, 2> clock_ms{};                  // now, White's and Black's
    std::array<GameClock, 2> clocks;                    // White's and Black's time control
    int takebacks  = 0;
    int hints_used = 0;
    bool aids      = false;
    int last_book  = -1;  // the ply of the engine's last move from its openings
    bool book_out  = false;
};
bool writeKeptGame(const std::filesystem::path& folder, const KeptGame& game);
[[nodiscard]] std::optional<KeptGame> readKeptGame(const std::filesystem::path& folder);
void removeKeptGame(const std::filesystem::path& folder);
// The kept game's moves as a game: none when one of them cannot be played (a file changed by hand).
[[nodiscard]] std::optional<ReplayGame> keptReplay(const KeptGame& game);

}  // namespace fcg
