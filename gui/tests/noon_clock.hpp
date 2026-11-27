#pragma once

// The clock a test's fake gives the training (context::Clock, #138): the day's noon, local time, going on from there as
// the system's clock goes. The training day ends at 4 a.m. (trainingDayEnd()): a test that schedules a card ten
// minutes or a day ahead finds it where it expects, whatever the hour it runs at.

#include <cstdint>
#include <ctime>

#include <tour/util.hpp>

#include "training.hpp"

namespace fcg::test {

class NoonClock {
   public:
    NoonClock() : start_(epochNow()), noon_(noonOf(start_)) {}

    [[nodiscard]] int64_t now() const { return noon_ + (epochNow() - start_); }

   private:
    static int64_t noonOf(int64_t t) {
        std::tm day     = localTime(static_cast<std::time_t>(t));
        day.tm_hour     = 12;
        day.tm_min      = 0;
        day.tm_sec      = 0;
        day.tm_isdst    = -1;
        const auto made = std::mktime(&day);
        return made == static_cast<std::time_t>(-1) ? t : static_cast<int64_t>(made);
    }

    int64_t start_ = 0;
    int64_t noon_  = 0;
};

}  // namespace fcg::test
