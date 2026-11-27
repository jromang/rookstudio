#pragma once

// An FSRS card as the profile's files keep it (#117): its fields "state" (1 learning, 2 review, 3 relearning), "step",
// "stability", "difficulty", "due", "last", "reps" and "lapses", in an object of their own or among the fields of what
// the card schedules. One writer and one reader for the repertoire's training, the endgames, the puzzles and the
// mistakes; nlohmann's json finds them by the card's type.

#include <cstdint>

#include <json.hpp>

#include "fsrs.hpp"

namespace fcg::study::fsrs {

inline void to_json(nlohmann::json& j, const Card& c) {
    j = {{"state", static_cast<int>(c.state)},
         {"step", c.step},
         {"stability", c.stability},
         {"difficulty", c.difficulty},
         {"due", c.due},
         {"last", c.last},
         {"reps", c.reps},
         {"lapses", c.lapses}};
}

// A field missing reads as a fresh card's, a value that is not an object as a fresh card; a field of another type
// throws nlohmann's type_error, for the reader to set the entry aside (#136).
inline void from_json(const nlohmann::json& j, Card& c) {
    c = Card{};
    if (!j.is_object()) return;
    const int state = j.value("state", 1);
    c.state         = state == 2 ? State::Review : state == 3 ? State::Relearning : State::Learning;
    c.step          = j.value("step", 0);
    c.stability     = j.value("stability", 0.0);
    c.difficulty    = j.value("difficulty", 0.0);
    c.due           = j.value("due", int64_t{0});
    c.last          = j.value("last", int64_t{0});
    c.reps          = j.value("reps", 0);
    c.lapses        = j.value("lapses", 0);
}

}  // namespace fcg::study::fsrs
