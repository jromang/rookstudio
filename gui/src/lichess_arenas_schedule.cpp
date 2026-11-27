#include "lichess_arenas_schedule.hpp"

#include "refreshing.hpp"

namespace fcg::lichess {

bool teamSwissesDue(const TeamSwissesAsk& ask, SteadyTime now) {
    return ask.shown && ask.readable && !ask.asking && (ask.new_token || now - ask.asked >= TEAM_SWISSES_EVERY);
}

bool standingDue(const StandingAsk& ask, SteadyTime now) {
    return ask.drawn && now >= ask.waits_until && !ask.asking &&
           dueAfter(ask.asked, ask.swiss ? SWISS_STANDING_EVERY : STANDING_EVERY, now);
}

bool statusesDue(const StatusesAsk& ask, SteadyTime now) {
    return ask.shown && ask.playing && ask.top > 0 && !ask.asking && now >= ask.waits_until &&
           (ask.top_changed || never(ask.asked) ||
            (now - ask.asked >= STANDING_EVERY && (ask.some_over || ask.places < ask.top)));
}

bool arenaPageDue(const ArenaPageAsk& ask, SteadyTime now) {
    return ask.looked && now >= ask.waits_until && !ask.asking && !ask.over && dueAfter(ask.asked, STANDING_EVERY, now);
}

bool rejoinDue(const ArenaSeat& seat, bool linked, bool joining, SteadyTime waits_until, SteadyTime now) {
    return seat.due(now) && !joining && linked && now >= waits_until;
}

bool swissPageDue(const SwissPageAsk& ask, SteadyTime now) {
    const std::chrono::steady_clock::duration every =
        ask.joined || ask.started ? SWISS_PLAYED_EVERY : std::chrono::seconds(SWISS_LOOKED_EVERY);
    return ask.looked && !ask.asking && dueAfter(ask.asked, every, now) && !ask.complete;
}

bool swissRowsDue(bool rows, const SwissInfo* info, int rows_round) {
    return !rows || !info || info->round != rows_round || info->state == TourState::Finished;
}

}  // namespace fcg::lichess
