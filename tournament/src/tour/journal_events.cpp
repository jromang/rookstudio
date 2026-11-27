#include "journal_events.hpp"

#include <stdexcept>
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
        case EventType::Unknown:
        case EventType::EncounterStarted:
        case EventType::GameFinished:
        case EventType::EditUndone:
        case EventType::RoundPaired:
        case EventType::TiebreakAdded:
        case EventType::Finished:
        case EventType::PgnOut:
            return false;
    }
    return false;
}

void readEvent(const nlohmann::json& event, const JournalReader& reader) {
    const auto name = reader.strict ? event.at("t").get<std::string>() : event.value("t", std::string{});
    const std::function<void(const nlohmann::json&)>* handler = nullptr;
    switch (eventType(name)) {
        case EventType::EncounterStarted:
            handler = &reader.encounter_started;
            break;
        case EventType::GameFinished:
            handler = &reader.game_finished;
            break;
        case EventType::ParticipantAdded:
        case EventType::ParticipantReplaced:
        case EventType::ParticipantWithdrawn:
        case EventType::CyclesChanged:
        case EventType::GamesVoided:
        case EventType::ResultSet:
            handler = &reader.edit;
            break;
        case EventType::EditUndone:
            handler = &reader.edit_undone;
            break;
        case EventType::RoundPaired:
        case EventType::TiebreakAdded:
            handler = &reader.round;
            break;
        case EventType::Finished:
            handler = &reader.finished;
            break;
        case EventType::PgnOut:
            handler = &reader.pgn_out;
            break;
        case EventType::Unknown:
            if (reader.strict) throw std::runtime_error("unknown event \"" + name + "\"");
            return;
    }
    if (*handler) (*handler)(event);
}

}  // namespace fcg::tour
