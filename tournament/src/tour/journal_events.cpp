#include "journal_events.hpp"

#include <string>

namespace fcg::tour {

namespace {

struct Named {
    EventType type;
    const char* name;
};

constexpr Named NAMES[] = {
    {EventType::EncounterStarted, "encounter_started"},
    {EventType::GameFinished, "game_finished"},
    {EventType::ParticipantAdded, "participant_added"},
    {EventType::ParticipantReplaced, "participant_replaced"},
    {EventType::ParticipantWithdrawn, "participant_withdrawn"},
    {EventType::CyclesChanged, "cycles_changed"},
    {EventType::GamesVoided, "games_voided"},
    {EventType::ResultSet, "result_set"},
    {EventType::EditUndone, "edit_undone"},
    {EventType::RoundPaired, "round_paired"},
    {EventType::TiebreakAdded, "tiebreak_added"},
    {EventType::Finished, "finished"},
    {EventType::PgnOut, "pgn_out"},
};

}  // namespace

const char* eventName(EventType type) noexcept {
    for (const auto& n : NAMES) {
        if (n.type == type) return n.name;
    }
    return "";
}

EventType eventType(std::string_view name) noexcept {
    for (const auto& n : NAMES) {
        if (name == n.name) return n.type;
    }
    return EventType::Unknown;
}

EventType eventTypeOf(const nlohmann::json& event) { return eventType(event.value("t", std::string{})); }

nlohmann::json journalEvent(EventType type, nlohmann::json fields) {
    fields["t"] = eventName(type);
    return fields;
}

bool isEditEvent(EventType type) noexcept {
    switch (type) {
        case EventType::ParticipantAdded:
        case EventType::ParticipantReplaced:
        case EventType::ParticipantWithdrawn:
        case EventType::CyclesChanged:
        case EventType::GamesVoided:
        case EventType::ResultSet:
            return true;
        default:
            return false;
    }
}

}  // namespace fcg::tour
