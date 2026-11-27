#include "training.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>

#include <fmt/include/fmt/format.h>
#include <json.hpp>

#include <tour/files.hpp>
#include <tour/util.hpp>

#include "profile_entries.hpp"
#include "storage.hpp"

namespace fs = std::filesystem;

namespace fcg {

namespace {

fs::path besideOf(const fs::path& repertoire, const char* suffix) {
    auto out = repertoire;
    out.replace_filename(repertoire.stem().string() + suffix);
    return out;
}

std::string hexKey(uint64_t key) { return fmt::format("{:016x}", key); }

}  // namespace

fs::path trainingFileOf(const fs::path& repertoire) { return besideOf(repertoire, ".training.json"); }
fs::path reviewsFileOf(const fs::path& repertoire) { return besideOf(repertoire, ".reviews.jsonl"); }

TrainingFile readTrainingFile(const fs::path& repertoire) {
    TrainingFile out;
    const auto file = trainingFileOf(repertoire);
    const auto json = readJson<nlohmann::json>(file);
    if (!json || !json->is_object()) {
        // There but unreadable (cut short, written by hand): kept aside and said, rather than replaced in silence by
        // the next answer (#136).
        sayUnreadableFile(file);
        return out;
    }
    // An entry of another type than the one written (by another version, edited by hand) is set aside, the readable
    // ones kept (#136): said in the console, the file as it was kept beside it, which the next answer replaces.
    ProfileEntries entries(file);
    entries.entry("answer_ms", [&] { out.answer_ms = json->value("answer_ms", int64_t{0}); });
    const auto cards = json->find("cards");
    if (cards != json->end() && cards->is_object()) {
        for (const auto& [key, j] : cards->items()) {
            if (!j.is_object()) continue;
            entries.entry("cards/" + key, [&] {
                const uint64_t k = std::stoull(key, nullptr, 16);  // a position's key, as hexKey() writes it
                study::CardMemory m;
                m.moves           = j.value("moves", std::string());
                m.since           = j.value("since", int64_t{0});
                const int state   = j.value("state", 1);
                m.card.state      = state == 2   ? study::fsrs::State::Review
                                    : state == 3 ? study::fsrs::State::Relearning
                                                 : study::fsrs::State::Learning;
                m.card.step       = j.value("step", 0);
                m.card.stability  = j.value("stability", 0.0);
                m.card.difficulty = j.value("difficulty", 0.0);
                m.card.due        = j.value("due", int64_t{0});
                m.card.last       = j.value("last", int64_t{0});
                m.card.reps       = j.value("reps", 0);
                m.card.lapses     = j.value("lapses", 0);
                out.memories[k]   = std::move(m);  // read whole first: nothing kept of an entry set aside
            });
        }
    }
    entries.sayUnread();
    return out;
}

bool writeTrainingFile(const fs::path& repertoire, const TrainingFile& file) {
    nlohmann::json cards = nlohmann::json::object();
    for (const auto& [key, m] : file.memories) {
        cards[hexKey(key)] = {{"moves", m.moves},
                              {"since", m.since},
                              {"state", static_cast<int>(m.card.state)},
                              {"step", m.card.step},
                              {"stability", m.card.stability},
                              {"difficulty", m.card.difficulty},
                              {"due", m.card.due},
                              {"last", m.card.last},
                              {"reps", m.card.reps},
                              {"lapses", m.card.lapses}};
    }
    const nlohmann::json json = {{"version", 1}, {"answer_ms", file.answer_ms}, {"cards", std::move(cards)}};
    return writeJson(trainingFileOf(repertoire), json);
}

void appendReview(const fs::path& repertoire, uint64_t key, int64_t at, study::fsrs::Rating rating, int64_t ms,
                  const std::string& played) {
    const nlohmann::json line = {
        {"key", hexKey(key)}, {"at", at}, {"rating", static_cast<int>(rating)}, {"ms", ms}, {"played", played}};
    std::ofstream out(reviewsFileOf(repertoire), std::ios::binary | std::ios::app);
    out << line.dump() << '\n';
}

TrainingSettings readTrainingSettings(const fs::path& profile) {
    TrainingSettings out;
    const auto file = profile / "training.json";
    const auto json = readJson<nlohmann::json>(file);
    if (!json || !json->is_object()) {
        // There but unreadable (cut short, written by hand): kept aside and said, rather than replaced in silence by
        // the next change of a setting (#136).
        sayUnreadableFile(file);
        return out;
    }
    // A setting of another type than the one written is set aside, its default kept, the others read (#136): said in
    // the console, the file as it was kept beside it.
    ProfileEntries entries(file);
    entries.entry("advance_ms",
                  [&] { out.advance_ms = std::clamp(json->value("advance_ms", out.advance_ms), 0, 10000); });
    entries.entry("new_per_day",
                  [&] { out.new_per_day = std::clamp(json->value("new_per_day", out.new_per_day), 0, 1000); });
    entries.entry("guess_theory", [&] { out.guess_theory = json->value("guess_theory", out.guess_theory); });
    entries.sayUnread();
    return out;
}

void writeTrainingSettings(const fs::path& profile, const TrainingSettings& settings) {
    (void)writeJson(profile / "training.json", {{"advance_ms", settings.advance_ms},
                                                {"new_per_day", settings.new_per_day},
                                                {"guess_theory", settings.guess_theory}});
}

int64_t trainingDayEnd(int64_t now) {
    const auto t = static_cast<std::time_t>(now);
    const auto local = localTime(t);
    // 4 a.m. of this day, or of the next one once past it.
    std::tm end  = local;
    end.tm_hour  = 4;
    end.tm_min   = 0;
    end.tm_sec   = 0;
    end.tm_isdst = -1;
    if (local.tm_hour >= 4) ++end.tm_mday;
    const auto made = std::mktime(&end);
    return made == static_cast<std::time_t>(-1) ? now + study::fsrs::DAY : static_cast<int64_t>(made);
}

study::TrainingDay trainingDay(int64_t now) { return {now, trainingDayEnd(now)}; }

int64_t epochNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

study::fsrs::Scheduler trainingScheduler() {
    study::fsrs::Settings settings;
    settings.learning_steps   = {600};
    settings.relearning_steps = {600};
    return study::fsrs::Scheduler(std::move(settings));
}

}  // namespace fcg
