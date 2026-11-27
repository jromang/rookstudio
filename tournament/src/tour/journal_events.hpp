#pragma once

// The files of a tournament's folder, and the events of its journal (journal.jsonl, see journal.hpp): each line one
// JSON object whose "t" names its type. Their names are written here only, and readEvent() hands an event to the
// handler of its type: the readers of the journal (TournamentState's load(), follow() and replay()) say what they do
// with each type, not how a type is told.

#include <functional>
#include <string_view>

#include <json.hpp>

namespace fcg::tour {

// --- the folder (see state.hpp) ---

inline constexpr const char* SPEC_FILE    = "tournament.json";  // the settings and participants, written once
inline constexpr const char* JOURNAL_FILE = "journal.jsonl";    // everything that happened since
inline constexpr const char* LOCK_FILE    = "tournament.lock";  // held by the program that writes it (FolderLock)
// The PGN files of the tournaments of 0.4.12 and earlier, and of the matches of 0.2.x (legacy.hpp); a tournament made
// since names them after itself (namePgnFiles()).
inline constexpr const char* OLD_PGN_FILE           = "games.pgn";
inline constexpr const char* OLD_CHESSBASE_PGN_FILE = "games-chessbase.pgn";

// --- the journal's events ---

enum class EventType {
    Unknown,           // none of the types below: a line of a newer version, or no event
    EncounterStarted,  // "encounter_started": the opening an encounter started with (with "game": 1, its game 1's own)
    GameFinished,      // "game_finished": a game recorded (gameResultJson())
    // The edits (editEvent(), applyEditEvent()).
    ParticipantAdded,      // "participant_added"
    ParticipantReplaced,   // "participant_replaced"
    ParticipantWithdrawn,  // "participant_withdrawn"
    CyclesChanged,         // "cycles_changed"
    GamesVoided,           // "games_voided"
    ResultSet,             // "result_set"
    EditUndone,            // "edit_undone": the last edit undone
    RoundPaired,           // "round_paired": the next round of a Swiss or a knockout
    TiebreakAdded,         // "tiebreak_added": another pair of games for a tied match of a knockout
    Finished,              // "finished": the end, and why ("complete", "sprt_h0", "sprt_h1", "imported")
    PgnOut,                // "pgn_out": games written to the PGN file of the user's (TournamentSpec::pgn_out)
};

// The name events of `type` have in the journal; "" for Unknown.
[[nodiscard]] const char* eventName(EventType type) noexcept;

// The type the journal names `name`; Unknown when none.
[[nodiscard]] EventType eventType(std::string_view name) noexcept;

// The type of `event`, as its "t" names it; Unknown when it has none. Throws, as nlohmann::json::value() does, when
// `event` is no object or its "t" no text.
[[nodiscard]] EventType eventTypeOf(const nlohmann::json& event);

// An event of `type` and `fields`, as the journal is written.
[[nodiscard]] nlohmann::json journalEvent(EventType type, nlohmann::json fields = nlohmann::json::object());

// The edits: those editEvent() makes.
[[nodiscard]] bool isEditEvent(EventType type) noexcept;

// What a reader of the journal does with the events of each type. A handler left empty passes over its events.
struct JournalReader {
    std::function<void(const nlohmann::json&)> encounter_started;
    std::function<void(const nlohmann::json&)> game_finished;
    std::function<void(const nlohmann::json&)> edit;  // the edits, isEditEvent()
    std::function<void(const nlohmann::json&)> edit_undone;
    std::function<void(const nlohmann::json&)> round;  // round_paired, tiebreak_added
    std::function<void(const nlohmann::json&)> finished;
    std::function<void(const nlohmann::json&)> pgn_out;
    // An event without a "t", or of a type this version does not know, is an error ("unknown event"); not strict, it
    // is passed over (a summary reads a journal a newer version may be writing).
    bool strict = true;
};

// Hands `event` to the handler of its type in `reader`.
void readEvent(const nlohmann::json& event, const JournalReader& reader);

}  // namespace fcg::tour
