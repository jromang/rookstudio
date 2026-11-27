#include "state.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <tuple>

#include <game/pgn/pgn_builder.hpp>

#include <fmt/include/fmt/format.h>

#include "book_adapter.hpp"
#include "engine_text.hpp"
#include "journal_events.hpp"
#include "pgn_book.hpp"
#include "sprt_rules.hpp"

namespace fcg::tour {

namespace fs = std::filesystem;

namespace {

constexpr const char* READ_ONLY = "this tournament is open in another program, and only shown here";

// The file every game is also appended to (TournamentSpec::pgn_out) is a .pgn file: a tournament folder from
// elsewhere must not make the program append its text to any file at all (a shell's startup file...).
bool pgnOutAllowed(const std::string& file) {
    if (file.size() <= 4) return false;
    auto extension = file.substr(file.size() - 4);
    for (auto& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return extension == ".pgn";
}

// The events without the edits an "edit_undone" cancels, nor the end of tournament such an edit caused.
std::vector<nlohmann::json> withoutUndoneEdits(const std::vector<nlohmann::json>& events) {
    std::vector<nlohmann::json> out;
    out.reserve(events.size());
    for (const auto& e : events) {
        if (eventTypeOf(e) != EventType::EditUndone) {
            out.push_back(e);
            continue;
        }
        for (auto i = out.size(); i-- > 0;) {
            if (!isEditEvent(eventTypeOf(out[i]))) continue;
            const auto completed = [](const nlohmann::json& x) {
                return eventTypeOf(x) == EventType::Finished && x.value("why", std::string{}) == "complete";
            };
            out.erase(std::remove_if(out.begin() + static_cast<std::ptrdiff_t>(i) + 1, out.end(), completed),
                      out.end());
            out.erase(out.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    return out;
}

bool validResult(const std::string& result) { return result == "1-0" || result == "0-1" || result == "1/2-1/2"; }

// The result `r` counts with, set by hand; the result it was played to takes it back as played.
void setResult(GameResult& r, const std::string& result, const std::string& why) {
    if (!r.set) r.set = ResultSet{why, r.result, r.termination, r.reason};
    if (result == r.set->played_result) {
        r.result      = r.set->played_result;
        r.termination = r.set->played_termination;
        r.reason      = r.set->played_reason;
        r.set.reset();
        return;
    }
    r.set->why    = why;
    r.result      = result;
    r.termination = fastchess::MatchTermination::ADJUDICATION;
    r.reason      = OPERATOR_REASON;
}

// The points of each participant in the games that count.
std::map<int, double> pointsOf(const std::vector<GameResult>& results) {
    std::map<int, double> points;
    for (const auto& r : results) {
        if (r.voided) continue;
        points[r.white] += r.scoreOf(r.white);
        points[r.black] += r.scoreOf(r.black);
    }
    return points;
}

// fastchess gives the duration as "HH:MM:SS".
uint32_t parseDuration(const std::string& text) {
    int h = 0, m = 0, s = 0;
    if (std::sscanf(text.c_str(), "%d:%d:%d", &h, &m, &s) != 3) return 0;
    const int64_t ms = (static_cast<int64_t>(h) * 3600 + m * 60 + s) * 1000;
    return static_cast<uint32_t>(std::clamp<int64_t>(ms, 0, UINT32_MAX));
}

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// The reference ratings after the Result tag, where rating lists and databases read them.
std::string withElo(std::string pgn, int white_elo, int black_elo) {
    std::string tags;
    if (white_elo > 0) tags += fmt::format("[WhiteElo \"{}\"]\n", white_elo);
    if (black_elo > 0) tags += fmt::format("[BlackElo \"{}\"]\n", black_elo);
    const auto result = pgn.find("\n[Result ");
    const auto end    = result == std::string::npos ? result : pgn.find('\n', result + 1);
    if (!tags.empty() && end != std::string::npos) pgn.insert(end + 1, tags);
    return pgn;
}

// A team match: the WhiteTeam and BlackTeam tags after the Result tag, as databases read the teams of a game.
std::string withTeams(std::string pgn, const TournamentSpec& spec, int white, int black) {
    if (spec.format != Format::TeamMatch) return pgn;
    const auto team = [&](int id) {
        const auto* p = spec.participant(id);
        return pgnTagValue(spec.teamName(p ? p->team : 0));
    };
    const auto result = pgn.find("\n[Result ");
    const auto end    = result == std::string::npos ? result : pgn.find('\n', result + 1);
    if (end != std::string::npos) {
        pgn.insert(end + 1, fmt::format("[WhiteTeam \"{}\"]\n[BlackTeam \"{}\"]\n", team(white), team(black)));
    }
    return pgn;
}

// Games the tournament will have; Swiss and knockout schedules grow round by round, so their plan counts too.
uint64_t plannedGames(const TournamentSpec& spec, const Schedule& schedule, std::size_t dropped) {
    const auto size = schedule.size();
    if (!size) return 0;
    const auto n = spec.participants.size();
    if (spec.format == Format::Swiss) {
        return std::max<uint64_t>(*size, uint64_t(std::max(1, spec.rounds)) * (n / 2)) * 2 - dropped;
    }
    if (spec.format == Format::Knockout) {
        // Every match eliminates one player; tiebreaks come on top.
        const uint64_t planned = (n > 0 ? n - 1 : 0) * uint64_t(std::max(1, spec.cycles));
        return std::max<uint64_t>(*size, planned) * 2 - dropped;
    }
    return uint64_t{*size} * 2 - dropped;
}

}  // namespace

fastchess::Stats encounterStats(const GameResult& a, const GameResult& b, int pov) {
    int w = 0, d = 0, l = 0;
    for (const double score : {a.scoreOf(pov), b.scoreOf(pov)}) {
        if (score == 1.0) {
            ++w;
        } else if (score == 0.0) {
            ++l;
        } else {
            ++d;
        }
    }

    fastchess::Stats stats(w, l, d);
    stats.penta_WW = w == 2;
    stats.penta_WD = w == 1 && d == 1;
    stats.penta_WL = w == 1 && l == 1;
    stats.penta_DD = d == 2;
    stats.penta_LD = l == 1 && d == 1;
    stats.penta_LL = l == 2;
    return stats;
}

TournamentState::Replayed TournamentState::replay(const std::vector<nlohmann::json>& events, TournamentSpec& spec) {
    Replayed out;
    Schedule schedule(spec);
    std::vector<GameResult> results;
    std::set<uint64_t> done, dropped;

    // The settings, the schedule and the results only; the openings and the writes to spec.pgn_out do not count, nor
    // what a newer version may journal.
    JournalReader reader;
    reader.strict        = false;
    reader.game_finished = [&](const nlohmann::json& e) {
        const auto r = gameResultFromJson(e);
        if (done.insert(r.number()).second) results.push_back(r);
    };
    reader.edit = [&](const nlohmann::json& e) {
        effects(e, spec, schedule, results, done, dropped);
        if (out.finished && out.finish_reason == "complete") out.finished = false;
    };
    reader.round = [&](const nlohmann::json& e) {
        Rounds::addEncounters(e, spec, schedule);
        if (eventTypeOf(e) == EventType::RoundPaired) {
            for (const auto& id : e.value("byes", std::vector<int>{})) out.byes[id]++;
            if (e.contains("bracket")) out.last_bracket = e.at("bracket").get<std::vector<std::vector<int>>>();
        }
    };
    reader.finished = [&](const nlohmann::json& e) {
        out.finished      = true;
        out.finish_reason = e.value("why", std::string{});
    };
    for (const auto& e : withoutUndoneEdits(events)) readEvent(e, reader);

    for (const auto& r : results) {
        if (r.voided) continue;
        out.played++;
        out.results.push_back(r);
    }
    if (schedule.size()) out.total = plannedGames(spec, schedule, dropped.size());
    return out;
}

bool TournamentState::isTournament(const fs::path& dir) {
    std::error_code ec;
    return fs::is_regular_file(dir / SPEC_FILE, ec);
}

std::unique_ptr<TournamentState> TournamentState::create(const fs::path& dir, TournamentSpec spec) {
    if (isTournament(dir)) throw std::runtime_error("a tournament already exists in " + dir.string());

    // Validates the participants against the format before anything is written.
    (void)Schedule(spec);
    if (!spec.pgn_out.empty() && !pgnOutAllowed(spec.pgn_out))
        throw std::runtime_error("the games can only be appended to a .pgn file, not to " + spec.pgn_out);

    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) throw std::runtime_error("cannot create the folder " + dir.string());

    if (spec.pgn_file.empty() || spec.pgn_chessbase_file.empty()) namePgnFiles(spec);
    spec.book = fingerprint(spec.conditions.opening.file);
    for (auto& p : spec.participants) p.program = p.remote ? FileFingerprint{} : fingerprint(p.engine.cmd);
    replaceFile(dir / SPEC_FILE,
                nlohmann::json(spec).dump(4, ' ', false, nlohmann::json::error_handler_t::replace) + "\n");

    auto state = open(dir);

    // A missing or unreadable book is reported now, not when the first game starts.
    std::lock_guard<std::mutex> lock(state->mutex_);
    (void)state->openings();
    return state;
}

std::unique_ptr<TournamentState> TournamentState::open(const fs::path& dir, bool read_only) {
    std::ifstream in(dir / SPEC_FILE, std::ios::binary);
    if (!in) throw std::runtime_error("no tournament in " + dir.string());

    TournamentSpec spec;
    try {
        spec = nlohmann::json::parse(in).get<TournamentSpec>();
    } catch (const std::exception& e) {
        throw std::runtime_error((dir / SPEC_FILE).string() + ": " + e.what());
    }

    // A tournament folder may come from someone else (an archive...): what it holds must not make the program write
    // outside it. The files written in it are no symbolic links (opening it cuts the PGN files past their last game,
    // and FolderLock refuses its own), and the games are appended to no other file than a .pgn one.
    if (!read_only) {
        for (const auto& name : {std::string(JOURNAL_FILE), spec.pgnFile(), spec.chessbasePgnFile()}) {
            std::error_code ec;
            if (fs::is_symlink(dir / name, ec))
                throw std::runtime_error((dir / name).string() +
                                         " is a symbolic link: a tournament's files must be in its folder");
        }
    }
    std::string ignored_out;
    if (!spec.pgn_out.empty() && !pgnOutAllowed(spec.pgn_out)) std::swap(ignored_out, spec.pgn_out);

    std::unique_ptr<TournamentState> state(new TournamentState(dir, std::move(spec)));
    state->read_only_ = read_only;
    if (!ignored_out.empty())
        state->warnings_.push_back("the games are not appended to " + ignored_out + ": not a .pgn file");
    // Before anything is read: the journal of a tournament another program plays may grow meanwhile.
    if (!read_only) state->lock_ = std::make_unique<FolderLock>(dir);
    state->load();
    if (!read_only) {
        std::lock_guard<std::mutex> lock(state->mutex_);
        state->keepOwnBooks();
    }
    return state;
}

TournamentState::TournamentState(fs::path dir, TournamentSpec spec)
    : dir_(std::move(dir)),
      spec_(std::move(spec)),
      pgn_path_(dir_ / spec_.pgnFile()),
      chessbase_path_(dir_ / spec_.chessbasePgnFile()),
      schedule_(spec_),
      pgn_out_({spec_, results_, done_, dropped_, finished_, pgn_path_, chessbase_path_}),
      rounds_({spec_, schedule_, results_, done_, dropped_}) {
    const auto& s = spec_.conditions.sprt;
    if (spec_.format == Format::Match && s.enabled) {
        sprt_ = fastchess::SPRT(s.alpha, s.beta, s.elo0, s.elo1, s.model, true);
    }
}

bool TournamentState::frc() const noexcept { return spec_.conditions.variant == fastchess::VariantType::FRC; }

const std::string& TournamentState::ownBookFileOf(const Participant& p) {
    const auto key = std::pair{p.book, p.book_depth};
    auto it        = own_book_files_.find(key);
    if (it == own_book_files_.end()) {
        return own_book_files_.emplace(key, ownBookFile(p.book, p.book_depth, frc())).first->second;
    }
    if (isPgnBook(p.book)) {
        std::error_code ec;
        if (fs::is_regular_file(it->second, ec)) {
            keepCachedBook(it->second);  // not pruned by another program while the tournament plays for weeks
        } else {
            // Not there when the tournament was opened (a resume prepares its books after opening it), or removed
            // since: asked again.
            it->second = ownBookFile(p.book, p.book_depth, frc());
        }
    }
    return it->second;
}

void TournamentState::keepOwnBooks() {
    if (finished_) return;
    for (const auto& p : spec_.participants) {
        if (p.active && !p.book.empty()) (void)ownBookFileOf(p);
    }
}

const OpeningSource& TournamentState::openings() {
    if (!openings_) {
        openings_ = std::make_unique<OpeningSource>(spec_.conditions.opening, frc(), spec_.conditions.seed);
    }
    return *openings_;
}

bool TournamentState::openingPerRound() const noexcept {
    return spec_.opening_per_round && spec_.format != Format::Match && spec_.format != Format::Knockout;
}

bool TournamentState::ownOpenings() const noexcept { return spec_.format == Format::Match && spec_.coloursLocked(); }

fastchess::book::Opening TournamentState::openingFor(const Encounter& encounter, int game) {
    if (ownOpenings()) return openings().at(std::size_t{encounter.index} * 2 + static_cast<std::size_t>(game));
    if (!openingPerRound()) return openings().at(encounter.index);
    const auto key = std::pair{encounter.cycle, encounter.round};
    if (const auto it = round_openings_.find(key); it != round_openings_.end()) return it->second;
    const auto opening = openings().at(round_openings_.size());
    round_openings_.emplace(key, opening);
    return opening;
}

void TournamentState::readStarted(const nlohmann::json& e) {
    OpeningText text{e.at("fen").get<std::string>(), e.value("moves", std::vector<std::string>{})};
    const auto encounter = e.at("encounter").get<uint32_t>();
    // The opening of game 1 of a match with locked colours, after its encounter's (game 0's): replaced with it.
    if (e.value("game", 0) == 1) {
        second_[encounter] = fromText(text, frc());
        return;
    }
    // Started again: its opening was replaced, as unbalanced (replaceUnbalanced()).
    if (started_.count(encounter)) {
        ++replaced_;
        ++replacements_[encounter];
    }
    started_[encounter] = fromText(text, frc());
    if (const auto enc = schedule_.at(encounter); enc && openingPerRound()) {
        round_openings_.emplace(std::pair{enc->cycle, enc->round}, started_[encounter]);
    }
    next_encounter_ = std::max(next_encounter_, encounter + 1);
}

JournalReader TournamentState::applying() {
    JournalReader reader;
    reader.encounter_started = [this](const nlohmann::json& e) { readStarted(e); };
    reader.edit              = [this](const nlohmann::json& e) {
        if (eventTypeOf(e) == EventType::GamesVoided && e.value("network", false))
            network_voids_ += e.at("games").size();
        applyEvent(e);
    };
    reader.round    = [this](const nlohmann::json& e) { applyRoundEvent(e); };
    reader.finished = [this](const nlohmann::json& e) {
        finished_      = true;
        finish_reason_ = e.value("why", std::string{});
    };
    return reader;
}

void TournamentState::load() {
    // A damaged line (a disk that lost part of a write) is skipped rather than making the tournament impossible to
    // open; read-only, the file is not touched.
    std::vector<std::string> damaged;
    const auto events =
        withoutUndoneEdits(Journal::read(dir_ / JOURNAL_FILE, !read_only_, &damaged, &journal_size_));
    for (const auto& d : damaged) warnings_.push_back(d + ": skipped");
    std::set<uint64_t> written_out;           // pgnPath() offsets of the games written to spec_.pgn_out
    std::optional<nlohmann::json> out_begun;  // a write to spec_.pgn_out begun, its end not journaled
    bool imported = false;                    // a match of fastchess-gui 0.2.x (see legacy.hpp)
    // Every game a journal line points to, by its pgnPath() offset: (pgnPath(), chessbasePgnPath()). A game_finished
    // line that a resume ignores (its game recorded already) points to its text all the same.
    std::map<uint64_t, std::pair<PgnSpan, PgnSpan>> journaled;

    // The events applied as follow() applies them, and what only opening the tournament reads of them: where each
    // game's text is, the writes to spec_.pgn_out, a match imported from 0.2.x.
    auto reader          = applying();
    reader.game_finished = [&](const nlohmann::json& e) {
        const auto result            = gameResultFromJson(e);
        journaled[result.pgn.offset] = {result.pgn, result.pgn_chessbase};
        apply(result);
    };
    reader.finished = [&, applied = reader.finished](const nlohmann::json& e) {
        applied(e);
        imported = imported || finish_reason_ == "imported";
    };
    reader.pgn_out = [&](const nlohmann::json& e) {
        // With "begun", the start of a write (with "pgn" empty, which older versions read as such); without, its end,
        // or a write of older versions.
        for (const auto& offset : e.at("pgn")) written_out.insert(offset.get<uint64_t>());
        if (e.contains("begun")) {
            out_begun = e;
        } else {
            out_begun.reset();
        }
    };
    for (std::size_t i = 0; i < events.size(); ++i) {
        try {
            readEvent(events[i], reader);
        } catch (const std::exception& ex) {
            throw std::runtime_error((dir_ / JOURNAL_FILE).string() + ", event " + std::to_string(i + 1) + ": " +
                                     ex.what());
        }
    }

    rebuildOpen();

    // Text no journal line points to belongs to no game: cut past the last game, blanked between games. Not between
    // the games of an imported match of 0.2.x: the games the import left out (of other engines...) are the user's.
    // Nor when a journal line was damaged: it may have been a game's, whose text is then the only trace of it.
    if (!read_only_) {
        const bool between = !imported && damaged.empty();
        std::vector<PgnSpan> kept, kept_cb;
        kept.reserve(journaled.size());
        kept_cb.reserve(journaled.size());
        for (const auto& [offset, spans] : journaled) {
            kept.push_back(spans.first);
            kept_cb.push_back(spans.second);
        }
        pgn_           = std::make_unique<PgnFile>(pgnPath(), std::move(kept), between);
        pgn_chessbase_ = std::make_unique<PgnFile>(chessbasePgnPath(), std::move(kept_cb), between);
        journal_       = std::make_unique<Journal>(dir_ / JOURNAL_FILE);
        for (const auto* file : {pgn_.get(), pgn_chessbase_.get()}) {
            if (file->missing() == 0) continue;
            warnings_.push_back(file->path().string() + " lost its end (a power cut?): " +
                                std::to_string(file->missing()) + " bytes of the last games are missing");
        }
        if (out_begun && pgn_out_.settle(*out_begun, journaled, journal_.get())) {
            for (const auto& offset : out_begun->at("begun")) written_out.insert(offset.get<uint64_t>());
        }
    }
    pgn_out_.opened(written_out);

    // A round may have ended right before a stop, before the next one was paired.
    pairIfReady();

    // Every game played but no end recorded: a stop right after the last game, or an undone edit had ended it.
    if (!finished_ && schedule_.size() && !gamesLeft()) {
        finished_      = true;
        finish_reason_ = "complete";
        if (journal_) journal_->append(journalEvent(EventType::Finished, {{"why", finish_reason_}}));
    }

    if (spec_.book.size > 0 && changedSince(spec_.conditions.opening.file, spec_.book)) {
        warnings_.push_back("the opening book changed since the tournament started: " + spec_.conditions.opening.file);
    }
    for (const auto& p : spec_.participants) {
        if (!p.active || p.program.size == 0 || !changedSince(p.engine.cmd, p.program)) continue;
        warnings_.push_back(std::filesystem::exists(p.engine.cmd)
                                ? "the program of " + p.engine.name +
                                      " changed since it joined the tournament: " + p.engine.cmd
                                : "the program of " + p.engine.name + " is missing: " + p.engine.cmd);
    }

    // Games held when the tournament was stopped, which may now be written (the tournament is over...).
    std::unique_lock<std::mutex> lock(mutex_);
    pgn_out_.write(lock, journal_.get());
}

bool TournamentState::apply(const GameResult& result) {
    if (done(result.encounter, result.game)) return false;

    results_.push_back(result);
    done_.insert(result.number());
    next_encounter_ = std::max(next_encounter_, result.encounter + 1);
    return count(results_.size() - 1);
}

bool TournamentState::count(std::size_t index) {
    const auto& result = results_[index];
    const auto it      = half_.find(result.encounter);
    if (it == half_.end()) {
        half_[result.encounter] = index;
        return false;
    }

    // The encounter is complete: count its two games, from the lower id's point of view.
    const int lo = std::min(result.white, result.black);
    const int hi = std::max(result.white, result.black);
    pairs_[{lo, hi}] += encounterStats(results_[it->second], result, lo);

    half_.erase(it);
    return true;
}

void TournamentState::rebuildStats() {
    pairs_.clear();
    half_.clear();
    for (std::size_t i = 0; i < results_.size(); ++i) {
        if (!results_[i].voided) count(i);
    }
}

void TournamentState::rebuildOpen() {
    open_.clear();
    const auto size = schedule_.size();
    const auto end  = size ? std::min(next_encounter_, *size) : next_encounter_;
    for (uint32_t e = 0; e < end; ++e) {
        for (int g = 0; g < 2; ++g) {
            const auto n = uint64_t{e} * 2 + g;
            if (!done_.count(n) && !dropped_.count(n) && !running_.count(n)) open_.emplace_back(e, g);
        }
    }
}

void TournamentState::effects(const nlohmann::json& event, TournamentSpec& spec, Schedule& schedule,
                              std::vector<GameResult>& results, std::set<uint64_t>& done, std::set<uint64_t>& dropped) {
    const auto type = eventTypeOf(event);
    applyEditEvent(event, spec, schedule);

    const auto voidWhere = [&](auto involves, bool replay, const std::string& why) {
        for (auto& r : results) {
            if (r.voided || !involves(r)) continue;
            r.voided      = true;
            r.void_reason = why;
            done.erase(r.number());
            if (!replay) dropped.insert(r.number());
        }
    };

    if (type == EventType::ParticipantReplaced) {
        // The new engine plays again every game of the old one.
        const int old = event.at("old").get<int>();
        voidWhere([&](const GameResult& r) { return r.white == old || r.black == old; }, true, "replaced");
    } else if (type == EventType::ParticipantWithdrawn) {
        const int id = event.at("participant").get<int>();
        // Games of started encounters it has not played will not be played.
        const auto at = std::min(event.at("at").get<uint32_t>(), schedule.size().value_or(0));
        for (uint32_t e = 0; e < at; ++e) {
            const auto enc = schedule.at(e);
            if (!enc || (enc->first != id && enc->second != id)) continue;
            for (int g = 0; g < 2; ++g) {
                if (!done.count(uint64_t{e} * 2 + g)) dropped.insert(uint64_t{e} * 2 + g);
            }
        }
        if (event.value("void_played", false)) {
            voidWhere([&](const GameResult& r) { return r.white == id || r.black == id; }, false, "withdrawn");
        }
    } else if (type == EventType::GamesVoided) {
        const bool replay = event.value("replay", true);
        const auto why    = event.value("why", std::string{});
        for (const auto& g : event.at("games")) {
            const auto n = g.at(0).get<uint64_t>() * 2 + g.at(1).get<uint64_t>();
            voidWhere([&](const GameResult& r) { return r.number() == n; }, replay, why);
            if (!replay && !done.count(n)) dropped.insert(n);
        }
    } else if (type == EventType::ResultSet) {
        const auto result = event.at("result").get<std::string>();
        if (!validResult(result)) throw std::runtime_error("a result is 1-0, 0-1 or 1/2-1/2");
        const auto why = event.value("why", std::string{});
        for (const auto& g : event.at("games")) {
            const auto n = g.at(0).get<uint64_t>() * 2 + g.at(1).get<uint64_t>();
            for (auto& r : results) {
                if (!r.voided && r.number() == n) setResult(r, result, why);
            }
        }
    }
}

void TournamentState::applyEvent(const nlohmann::json& event) {
    spec_snapshot_.reset();  // the next specSnapshot() copies spec_ as the event leaves it
    effects(event, spec_, schedule_, results_, done_, dropped_);
    rebuildStats();
    rebuildOpen();
    pgn_out_.rewind();  // voided games are to be played again

    // Games added to a finished tournament reopen it.
    if (finished_ && finish_reason_ == "complete" && gamesLeft()) {
        finished_ = false;
        finish_reason_.clear();
    }
}

uint64_t TournamentState::totalLocked() const { return plannedGames(spec_, schedule_, dropped_.size()); }

std::shared_ptr<const TournamentSpec> TournamentState::specSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!spec_snapshot_) spec_snapshot_ = std::make_shared<const TournamentSpec>(spec_);
    return spec_snapshot_;
}

void TournamentState::applyRoundEvent(const nlohmann::json& event) {
    Rounds::addEncounters(event, spec_, schedule_);
    rounds_.apply(event);
    rebuildOpen();
}

void TournamentState::pairIfReady() {
    if (!pairedAsItGoes(spec_.format) || finished_ || !journal_) return;
    for (const auto& event : rounds_.next()) {
        journal_->append(event);
        applyRoundEvent(event);
    }
}

std::map<int, int> TournamentState::byes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rounds_.byes();
}

std::map<uint32_t, std::vector<std::vector<int>>> TournamentState::brackets() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rounds_.brackets();
}

Impact TournamentState::preview(const Edit& edit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return previewLocked(edit);
}

int TournamentState::nextParticipantId() const {
    int id = 0;
    for (const auto& p : spec_.participants) id = std::max(id, p.id + 1);
    return id;
}

Impact TournamentState::previewLocked(const Edit& edit) const {
    Impact impact;
    impact.games_before = totalLocked();
    if (edit.kind != Edit::Kind::VoidGames && edit.kind != Edit::Kind::SetResult && !schedule_.editable()) {
        impact.error = "a match cannot be edited this way";
        return impact;
    }
    if (edit.kind == Edit::Kind::SetResult) {
        // The messages are those of the GUI's list of edit errors, translated there.
        if (!validResult(edit.result)) {
            impact.error = "a result is 1-0, 0-1 or 1/2-1/2";
            return impact;
        }
        for (const auto& [encounter, game] : edit.games) {
            const auto n  = uint64_t{encounter} * 2 + game;
            const auto it = std::find_if(results_.begin(), results_.end(),
                                         [&](const GameResult& r) { return !r.voided && r.number() == n; });
            if (it == results_.end()) {
                impact.error = "this game does not count: it was voided, or is not over";
                return impact;
            }
            if (it->result == edit.result) {
                impact.error = "the game already has this result";
                return impact;
            }
            // The rounds after it were paired from the results: a game of theirs is the last one a result can change.
            const auto enc = schedule_.at(encounter);
            if (spec_.format == Format::Knockout && (!enc || enc->round != rounds_.round())) {
                impact.error = "in a knockout, only the games of the round being played can have their result set";
                return impact;
            }
        }
    }

    const int new_id = nextParticipantId();

    auto spec     = spec_;
    auto schedule = schedule_;
    auto results  = results_;
    auto done     = done_;
    auto dropped  = dropped_;
    try {
        effects(editEvent(edit, next_encounter_, new_id, spec_), spec, schedule, results, done, dropped);
    } catch (const std::exception& e) {
        impact.error = e.what();
        return impact;
    }

    impact.games_after = plannedGames(spec, schedule, dropped.size());
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (!results[i].voided || results_[i].voided) continue;
        (dropped.count(results[i].number()) ? impact.voided : impact.replayed)++;
    }
    auto before = pointsOf(results_);
    auto after  = pointsOf(results);
    for (const auto& [id, points] : after) before.emplace(id, 0.0);
    for (const auto& [id, points] : before) {
        const auto it  = after.find(id);
        const double a = it == after.end() ? 0.0 : it->second;
        if (a != points) impact.points[id] = {points, a};
    }
    return impact;
}

void TournamentState::applyEdit(const Edit& edit) {
    if (read_only_) throw std::runtime_error(READ_ONLY);
    const Bump bump{version_};
    // Checked under the lock it is made with: a game ending in between on another thread could pair the next round of
    // a knockout from the results, and a result set then would land on a round paired already.
    std::unique_lock<std::mutex> lock(mutex_);
    if (const auto impact = previewLocked(edit); !impact.error.empty()) throw std::runtime_error(impact.error);

    const int new_id = nextParticipantId();

    auto event = editEvent(edit, next_encounter_, new_id, spec_);
    // The newcomer's program is read here rather than in editEvent(), which every preview calls.
    if (auto it = event.find("participant"); it != event.end() && it->is_object()) {
        auto p    = it->get<Participant>();
        p.program = p.remote ? FileFingerprint{} : fingerprint(p.engine.cmd);
        *it       = p;
    }
    journal_->append(event);

    undo_               = UndoPoint{spec_, schedule_, results_, done_, dropped_, claims_};
    undo_->results_only = edit.kind == Edit::Kind::SetResult && spec_.format != Format::Knockout;

    applyEvent(event);
    keepOwnBooks();  // the newcomer's, prepared before the change
    // A participant withdrawn or replaced is no longer waited for, whether its engine starts or not.
    for (const auto& p : spec_.participants) {
        if (!p.active) start_failures_.forget(p.id);
    }
    // The games of a withdrawn participant may have been the last ones to play. A Swiss or a knockout would pair
    // its next round first, which only a game's end does.
    if (!pairedAsItGoes(spec_.format)) finishIfComplete();
    pgn_out_.write(lock, journal_.get());  // games of a withdrawn participant no longer hold the others
}

bool TournamentState::canUndo() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (read_only_) return replica_.can_undo;
    return undo_ && (undo_->results_only || undo_->claims == claims_);
}

bool TournamentState::undoLasts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (read_only_) return replica_.undo_lasts;
    return undo_ && undo_->results_only;
}

void TournamentState::undoEdit() {
    if (read_only_) throw std::runtime_error(READ_ONLY);
    const Bump bump{version_};
    std::unique_lock<std::mutex> lock(mutex_);
    if (!undo_ || (!undo_->results_only && undo_->claims != claims_)) {
        throw std::runtime_error("a game started since the last change, which can no longer be undone");
    }
    journal_->append(journalEvent(EventType::EditUndone));

    auto& u = *undo_;
    // The results then; those recorded since come after them and are kept.
    for (std::size_t i = 0; i < results_.size() && i < u.results.size(); ++i) results_[i] = std::move(u.results[i]);
    if (u.results_only) {
        undo_.reset();
        rebuildStats();
        pgn_out_.write(lock, journal_.get());
        return;
    }
    spec_snapshot_.reset();
    spec_     = std::move(u.spec);
    schedule_ = std::move(u.schedule);
    done_     = std::move(u.done);
    dropped_  = std::move(u.dropped);
    for (std::size_t i = u.results.size(); i < results_.size(); ++i) {
        done_.insert(results_[i].number());  // running when the edit was made, finished since
    }
    undo_.reset();

    rebuildStats();
    rebuildOpen();
    pgn_out_.rewind();
    if (finished_ && finish_reason_ == "complete" && gamesLeft()) {
        finished_ = false;
        finish_reason_.clear();
    } else if (!finished_ && schedule_.size() && !gamesLeft() && running_.empty()) {
        // The edit had reopened a finished tournament.
        finished_      = true;
        finish_reason_ = "complete";
        journal_->append(journalEvent(EventType::Finished, {{"why", finish_reason_}}));
    }
    keepOwnBooks();
    pgn_out_.write(lock, journal_.get());
}

std::vector<ScheduleRow> TournamentState::scheduleRows() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ScheduleRow> rows;
    if (spec_.format == Format::Match) return rows;

    const auto size = schedule_.size().value_or(0);
    rows.reserve(size);
    for (uint32_t e = 0; e < size; ++e) {
        ScheduleRow row;
        row.encounter = *schedule_.at(e);
        for (int g = 0; g < 2; ++g) {
            const auto n  = uint64_t{e} * 2 + g;
            const auto& running = read_only_ ? replica_.running : running_;
            row.status[g]       = done_.count(n)      ? GameStatus::Done
                                  : dropped_.count(n) ? GameStatus::Dropped
                                  : running.count(n)  ? GameStatus::Running
                                                      : GameStatus::Pending;
        }
        rows.push_back(row);
    }
    return rows;
}

bool TournamentState::done(uint32_t encounter, int game) const {
    return done_.count(uint64_t{encounter} * 2 + game) > 0;
}

std::optional<uint64_t> TournamentState::totalGames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!schedule_.size()) return std::nullopt;
    return totalLocked();
}

GameTask TournamentState::makeTask(uint32_t encounter, int game) {
    const auto enc = schedule_.at(encounter);
    if (!enc) throw std::logic_error("encounter outside the schedule");

    auto it = started_.find(encounter);
    if (it == started_.end()) {
        const auto opening = openingFor(*enc);
        const auto text    = toText(opening, frc());
        journal_->append(journalEvent(EventType::EncounterStarted,
                                      {{"encounter", encounter}, {"fen", text.fen}, {"moves", text.moves}}));
        it = started_.emplace(encounter, opening).first;
    }
    auto opening = it->second;
    if (ownOpenings() && game == 1) {
        auto second = second_.find(encounter);
        if (second == second_.end()) {
            const auto own  = openingFor(*enc, 1);
            const auto text = toText(own, frc());
            journal_->append(
                journalEvent(EventType::EncounterStarted,
                             {{"encounter", encounter}, {"game", 1}, {"fen", text.fen}, {"moves", text.moves}}));
            second = second_.emplace(encounter, own).first;
        }
        opening = second->second;
    }

    const auto* first  = spec_.participant(enc->first);
    const auto* second = spec_.participant(enc->second);
    if (!first || !second) throw std::logic_error("unknown participant in the schedule");

    const bool first_white = spec_.firstHasWhite(game);
    GameTask task;
    task.encounter = *enc;
    task.game      = game;
    task.opening   = opening;
    task.white     = first_white ? first->engine : second->engine;
    task.black     = first_white ? second->engine : first->engine;
    // As the engines read them; the spec keeps the UTF-8.
    task.white.options = forEngine(std::move(task.white.options));
    task.black.options = forEngine(std::move(task.black.options));
    // A book of its own: the engine plays through the book adapter, its seed drawn from the tournament's. The
    // same for all its games, as fastchess keeps an engine's process (and the configuration it started with)
    // from game to game; each adapter process mixes a random number of its own into it (book_adapter.hpp).
    const auto own_book = [&](fastchess::EngineConfiguration& config, const Participant& p) {
        if (p.book.empty()) return;
        const auto seed = spec_.conditions.seed ^ (0x9e3779b97f4a7c15ULL * static_cast<uint64_t>(p.id + 1));
        config = throughBookAdapter(std::move(config), ownBookFileOf(p), p.book_depth, seed, spec_.book_randomness);
    };
    own_book(task.white, first_white ? *first : *second);
    own_book(task.black, first_white ? *second : *first);
    return task;
}

std::optional<GameTask> TournamentState::claimNext(const Admit& admit) {
    const Bump bump{version_};
    std::lock_guard<std::mutex> lock(mutex_);
    if (finished_ || read_only_) return std::nullopt;

    // The game is held back (nothing claimed) when the runner cannot start it now.
    const auto admitted = [&](uint32_t encounter) {
        if (!admit) return true;
        const auto e = schedule_.at(encounter);
        if (!e) return true;
        const auto* first  = spec_.participant(e->first);
        const auto* second = spec_.participant(e->second);
        return !first || !second || admit(*first, *second);
    };

    // The games of the participants whose engine cannot start wait for its next try.
    const auto aside_ids = start_failures_.setAside(StartFailures::Clock::now());
    const auto aside     = [&](uint32_t encounter) {
        if (aside_ids.empty()) return false;
        const auto e = schedule_.at(encounter);
        return e && (aside_ids.count(e->first) || aside_ids.count(e->second));
    };
    const auto claim = [&](uint32_t encounter, int game) {
        auto task = makeTask(encounter, game);
        running_.insert(task.number());
        ++claims_;
        // An engine due for its next try gets it with this game.
        start_failures_.claimed(task.encounter.first, task.number());
        start_failures_.claimed(task.encounter.second, task.number());
        return task;
    };

    for (auto it = open_.begin(); it != open_.end();) {
        const auto [encounter, game] = *it;
        const auto n                 = uint64_t{encounter} * 2 + game;
        if (done(encounter, game) || dropped_.count(n) || running_.count(n)) {
            it = open_.erase(it);
        } else if (aside(encounter)) {
            ++it;
        } else {
            if (!admitted(encounter)) return std::nullopt;
            open_.erase(it);
            return claim(encounter, game);
        }
    }

    // The next encounter of the schedule whose participants can play. Those passed over count as started: an edit
    // leaves them in place (it only changes encounters never started), and their games come first once they can be
    // played. A match without a game limit plays the same pair forever: it is not looked through to its end.
    auto next = next_encounter_;
    for (; schedule_.at(next) && aside(next); ++next) {
        if (!schedule_.size() && next - next_encounter_ > 1000) return std::nullopt;
    }
    if (!schedule_.at(next) || !admitted(next)) return std::nullopt;
    for (auto passed = next_encounter_; passed < next; ++passed) {
        open_.emplace_back(passed, 0);
        open_.emplace_back(passed, 1);
    }

    next_encounter_ = next + 1;
    open_.emplace_front(next, 1);  // its second game comes next
    return claim(next, 0);
}

void TournamentState::release(const GameTask& task) {
    const Bump bump{version_};
    std::lock_guard<std::mutex> lock(mutex_);
    running_.erase(task.number());
    if (!done(task.encounter.index, task.game) && !dropped_.count(task.number())) {
        open_.emplace_front(task.encounter.index, task.game);
    }
    start_failures_.released(task.number());
}

bool TournamentState::begun(uint32_t encounter) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int g = 0; g < 2; ++g) {
        const auto n = uint64_t{encounter} * 2 + g;
        if (done_.count(n) || running_.count(n)) return true;
    }
    return false;
}

Recorded TournamentState::complete(const GameTask& task, const fastchess::MatchData& data,
                                   const std::optional<ResultSet>& set) {
    const Bump bump{version_};
    using fastchess::pgn::CommentStyle;
    using fastchess::pgn::PgnBuilder;

    const auto round = std::size_t{task.encounter.index} + 1;

    GameResult result;
    result.encounter   = task.encounter.index;
    result.game        = task.game;
    result.result      = PgnBuilder::getResultFromWhiteMatch(data.players.white);
    result.termination = data.termination;
    result.reason      = data.reason;
    result.plies       = static_cast<uint32_t>(data.moves.size());
    result.duration_ms = parseDuration(data.duration);
    result.finished_at = nowMs();
    result.set         = set;
    for (std::size_t ply = 0; ply < data.moves.size(); ++ply) {
        for (const auto& w : data.moves[ply].pv_warnings) result.pv_warnings.emplace_back(ply, w);
    }

    // What the settings say of the game, read under the lock as an edit may change them on another thread: who had
    // White, the variant, the rule of unbalanced openings, the participants' own books. The moves those books played
    // are book moves in the PGN, as those of the opening are: those of the Polyglot books their adapters read (a PGN
    // book's conversion). The books are read without the lock.
    std::vector<std::tuple<chess::Color, std::string, int>> books;
    bool chess960 = false;
    UnbalancedRule unbalanced;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool first_white = spec_.firstHasWhite(task.game);
        result.white           = first_white ? task.encounter.first : task.encounter.second;
        result.black           = first_white ? task.encounter.second : task.encounter.first;
        chess960               = frc();
        unbalanced             = {spec_.unbalanced_cp, spec_.unbalanced_moves};
        for (const auto& [id, side] :
             {std::pair{result.white, chess::Color::WHITE}, std::pair{result.black, chess::Color::BLACK}}) {
            const auto* p = spec_.participant(id);
            if (p && !p->book.empty()) books.emplace_back(side, ownBookFileOf(*p), p->book_depth);
        }
    }
    std::optional<fastchess::MatchData> marked;
    for (const auto& [side, file, depth] : books) {
        try {
            PolyglotBook book(file);
            if (!marked) marked = data;
            markOwnBookMoves(*marked, side, book, depth, chess960);
        } catch (const std::exception&) {
            // The book is gone: its moves keep the comments of moves the engine made.
        }
    }
    const auto& game = marked ? *marked : data;
    // An opening that decided the game by itself, from the evaluations after the book (the own books' moves are book
    // moves by now). The result is not changed.
    if (unbalanced.score_cp > 0) result.unbalanced = unbalancedOpening(game, unbalanced);

    std::unique_lock<std::mutex> lock(mutex_);
    // Not open for writing (read-only, or let go of since the game was claimed: see letGo()): nothing is recorded.
    if (!journal_) {
        running_.erase(task.number());
        throw std::logic_error("a game ended in a tournament not open for writing");
    }
    // Until it returns, the files are in use, some of the time without the lock: letGo() refuses meanwhile.
    ++recording_;
    const struct Recording {
        std::atomic<int>& count;
        ~Recording() { --count; }
    } recording{recording_};
    const auto& pgn_config = spec_.conditions.pgn;
    const auto elo         = [&](int id) {
        const auto* p = spec_.participant(id);
        return p ? p->elo : 0;
    };
    const auto text     = withTeams(withElo(PgnBuilder(pgn_config, game, round, CommentStyle::Fastchess).get(),
                                            elo(result.white), elo(result.black)),
                                    spec_, result.white, result.black);
    const auto text_cb  = withTeams(withElo(PgnBuilder(pgn_config, game, round, CommentStyle::ChessBase).get(),
                                            elo(result.white), elo(result.black)),
                                    spec_, result.white, result.black);
    const auto unwanted = [&]() -> std::optional<Recorded> {
        // Already played, or no longer to be played (its participant was withdrawn meanwhile). It may have been the
        // last game the tournament was waiting for.
        if (done(task.encounter.index, task.game) || dropped_.count(task.number())) {
            running_.erase(task.number());
            pairIfReady();
            finishIfComplete();
            pgn_out_.write(lock, journal_.get());
            return Recorded{result, std::nullopt, false};
        }
        // One of its engines was replaced meanwhile: the game is played again, by the new engine.
        const auto now = schedule_.at(task.encounter.index);
        if (!now || now->first != task.encounter.first || now->second != task.encounter.second) {
            running_.erase(task.number());
            open_.emplace_front(task.encounter.index, task.game);
            return Recorded{result, std::nullopt, false};
        }
        return std::nullopt;
    };
    if (auto recorded = unwanted()) return *recorded;

    // The PGN text goes to the disk without the lock, which the views and the other game threads need meanwhile;
    // PgnFile keeps the places of concurrent games apart. The game stays running until it is journaled.
    lock.unlock();
    PgnSpan span, span_cb;
    bool written = false;
    try {
        span    = pgn_->append(text);
        written = true;
        span_cb = pgn_chessbase_->append(text_cb);
    } catch (...) {
        // The game will not be journaled: its text in the PGN files would show as a game in another PGN viewer.
        if (written) (void)pgn_->blank(span);
        lock.lock();
        running_.erase(task.number());
        throw;
    }
    lock.lock();
    // An edit made meanwhile may have dropped the game: the journal never points to its text, which is blanked
    // (or, after a crash before that, when the tournament is opened again).
    if (auto recorded = unwanted()) {
        lock.unlock();
        (void)pgn_->blank(span);
        (void)pgn_chessbase_->blank(span_cb);
        return *recorded;
    }

    running_.erase(task.number());
    result.pgn           = span;
    result.pgn_chessbase = span_cb;
    journal_->append(gameResultJson(result));

    Recorded recorded{result, std::nullopt};
    if (apply(result)) {
        recorded.pair = pairs_[{std::min(result.white, result.black), std::max(result.white, result.black)}];
        // Its pair played from an unbalanced opening, played again from another: its statistics did not count.
        if (replaceUnbalanced(result.encounter)) recorded.pair.reset();
    }
    pgn_out_.hold(results_.size() - 1);
    checkSprt();
    pairIfReady();
    finishIfComplete();
    pgn_out_.write(lock, journal_.get());

    return recorded;
}

bool TournamentState::replaceUnbalanced(uint32_t encounter) {
    if (!spec_.unbalanced_replace || spec_.unbalanced_cp <= 0 || finished_ || !journal_) return false;
    const bool unbalanced = std::any_of(results_.begin(), results_.end(), [&](const GameResult& r) {
        return !r.voided && r.encounter == encounter && r.unbalanced != 0;
    });
    if (!unbalanced || replacements_[encounter] >= MAX_OPENING_REPLACEMENTS) return false;
    const auto& book = openings();
    const auto n     = book.size();
    if (n < 2) return false;  // no other opening to play

    // One from the end of the book, which the tournament would reach last (in its playing order: shuffled with the
    // seed in random order): the openings of the encounters to come are not moved. Not the same one again. With
    // ownOpenings(), two: one for each game.
    const auto pick = [&](uint64_t k, const fastchess::book::Opening& was) {
        const auto current = toText(was, frc());
        auto index         = n - 1 - static_cast<std::size_t>(k % n);
        auto text          = toText(book.at(index), frc());
        if (text.fen == current.fen && text.moves == current.moves) {
            index = (index + n - 1) % n;
            text  = toText(book.at(index), frc());
        }
        return std::pair{index, text};
    };
    const bool own           = ownOpenings();
    const auto [index, text] = pick(own ? 2 * replaced_ : replaced_, started_[encounter]);
    std::optional<std::pair<std::size_t, OpeningText>> second;
    if (own) second = pick(2 * replaced_ + 1, second_.count(encounter) ? second_[encounter] : started_[encounter]);

    // Its games voided and played again, as a void of the Games tab would, then the encounter started anew from
    // that opening: a resume, and the versions before, read the last opening an encounter started with.
    nlohmann::json games = nlohmann::json::array();
    games.push_back(nlohmann::json::array({encounter, 0}));
    games.push_back(nlohmann::json::array({encounter, 1}));
    const auto voided =
        journalEvent(EventType::GamesVoided, {{"games", games}, {"replay", true}, {"why", UNBALANCED_REASON}});
    journal_->append(voided);
    applyEvent(voided);
    // The user's last change is no longer the last edit of the journal: undoing it would undo this void instead.
    undo_.reset();
    journal_->append(journalEvent(EventType::EncounterStarted,
                                  {{"encounter", encounter}, {"fen", text.fen}, {"moves", text.moves}}));
    started_[encounter] = book.at(index);
    if (second) {
        journal_->append(journalEvent(
            EventType::EncounterStarted,
            {{"encounter", encounter}, {"game", 1}, {"fen", second->second.fen}, {"moves", second->second.moves}}));
        second_[encounter] = book.at(second->first);
    }
    ++replaced_;
    ++replacements_[encounter];
    return true;
}

uint64_t TournamentState::openingsReplaced() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return replaced_;
}

bool TournamentState::voidCutGames(const std::vector<uint64_t>& numbers, const std::string& machine) {
    if (read_only_) throw std::logic_error("a read-only tournament does not void games");
    const Bump bump{version_};
    std::lock_guard<std::mutex> lock(mutex_);
    if (finished_ || !journal_) return false;
    nlohmann::json games = nlohmann::json::array();
    for (const auto n : numbers) {
        if (!done_.count(n)) continue;
        const bool counts = std::any_of(results_.begin(), results_.end(),
                                        [&](const GameResult& r) { return !r.voided && r.number() == n; });
        if (counts) games.push_back(nlohmann::json::array({n / 2, n % 2}));
    }
    if (games.empty()) return false;
    const auto voided = journalEvent(EventType::GamesVoided, {{"games", games},
                                                              {"replay", true},
                                                              {"why", std::string(NETWORK_REASON) + ": " + machine},
                                                              {"network", true},
                                                              {"machine", machine}});
    journal_->append(voided);
    applyEvent(voided);
    undo_.reset();  // the user's last change is no longer the journal's last edit
    network_voids_ += games.size();
    return true;
}

uint64_t TournamentState::networkVoids() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return network_voids_;
}

void TournamentState::finishIfComplete() {
    if (finished_ || !schedule_.size() || hasWorkLocked() || !running_.empty()) return;
    finished_      = true;
    finish_reason_ = "complete";
    journal_->append(journalEvent(EventType::Finished, {{"why", finish_reason_}}));
}

std::string TournamentState::pgnOutError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (read_only_) return replica_.pgn_out_error;
    return pgn_out_.error();
}

void TournamentState::checkSprt() {
    if (finished_ || !sprt_.isEnabled()) return;

    const int a   = spec_.participants[0].id;
    const int b   = spec_.participants[1].id;
    const auto it = pairs_.find({std::min(a, b), std::max(a, b)});
    if (it == pairs_.end()) return;
    const auto stats = a < b ? it->second : ~it->second;

    // No decision before the minimum of games, however far the LLR is beyond a bound (sprt_rules.hpp).
    const auto games   = static_cast<uint64_t>(std::max(0, stats.sum()));
    const auto verdict = sprtDecision(sprt_, sprt_.getLLR(stats, true), games, spec_.sprt_min_games);
    if (verdict == fastchess::SPRT_CONTINUE) return;

    finished_      = true;
    finish_reason_ = verdict == fastchess::SPRT_H1 ? "sprt_h1" : "sprt_h0";
    journal_->append(journalEvent(EventType::Finished, {{"why", finish_reason_}}));
}

bool TournamentState::wanted(const GameTask& task) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (done(task.encounter.index, task.game) || dropped_.count(task.number())) return false;
    const auto now = schedule_.at(task.encounter.index);
    return now && now->first == task.encounter.first && now->second == task.encounter.second;
}

bool TournamentState::hasWork() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hasWorkLocked();
}

bool TournamentState::settled(uint32_t encounter) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int g = 0; g < 2; ++g) {
        const auto n = uint64_t{encounter} * 2 + g;
        if (!done_.count(n) && !dropped_.count(n)) return false;
    }
    return true;
}

std::set<std::string> TournamentState::enginesLeft() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::set<int> active;
    for (const auto& p : spec_.participants) {
        if (p.active) active.insert(p.id);
    }

    // The last round of a Swiss is paired: no encounter will be added, its schedule is known as a round robin's is.
    const auto round = rounds_.round();
    const bool last_swiss_round =
        spec_.format == Format::Swiss && round > 0 && round >= static_cast<uint32_t>(spec_.rounds);

    if (!finished_ && (schedule_.editable() || last_swiss_round)) {
        // The games of started encounters, then the encounters never started, until every participant is seen:
        // after a round or two in a round robin, after the whole rest of the schedule in a gauntlet.
        std::set<int> left;
        const auto add = [&](const Encounter& e) {
            if (active.count(e.first)) left.insert(e.first);
            if (active.count(e.second)) left.insert(e.second);
        };
        for (const auto& [encounter, game] : open_) {
            const auto n = uint64_t{encounter} * 2 + game;
            if (done(encounter, game) || dropped_.count(n)) continue;
            if (const auto e = schedule_.at(encounter)) add(*e);
        }
        // The games being played too: one just claimed by a runner thread has not taken its engines yet, and a game
        // ending on another thread meanwhile would have their idle processes ended, to be started again.
        for (const auto n : running_) {
            if (const auto e = schedule_.at(static_cast<uint32_t>(n / 2))) add(*e);
        }
        for (auto i = next_encounter_; left.size() < active.size(); ++i) {
            const auto e = schedule_.at(i);
            if (!e) break;
            add(*e);
        }
        active = std::move(left);
    } else if (!finished_ && spec_.format == Format::Knockout && rounds_.brackets().count(round)) {
        // A knockout: the players of the round being played, less those who lost their match of it already, while
        // the other matches of the round go on. The next rounds are not known, but they are played among these.
        std::set<int> in;
        for (const auto& place : rounds_.brackets().at(round)) {
            if (place.size() == 2 && rounds_.placeOver(place[0], place[1])) {
                if (const auto winner = rounds_.placeWinner(place[0], place[1])) {
                    in.insert(*winner);
                    continue;
                }
            }
            in.insert(place.begin(), place.end());
        }
        std::set<int> left;
        for (const int id : active) {
            if (in.count(id)) left.insert(id);
        }
        active = std::move(left);
    } else if (finished_) {
        active.clear();
    }

    std::set<std::string> names;
    for (const auto& p : spec_.participants) {
        if (active.count(p.id)) names.insert(p.engine.name);
    }
    return names;
}

bool TournamentState::hasWorkLocked() const { return !finished_ && gamesLeft(); }

bool TournamentState::gamesLeft() const {
    for (const auto& [encounter, game] : open_) {
        const auto n = uint64_t{encounter} * 2 + game;
        if (!done(encounter, game) && !dropped_.count(n) && !running_.count(n)) return true;
    }
    return schedule_.at(next_encounter_).has_value();
}

void TournamentState::letGo() {
    std::lock_guard<std::mutex> lock(mutex_);
    // The files closed under complete() or the writer of spec_.pgn_out, which write them without the lock, would be
    // written to nothing. Games claimed and not ended do not hold it back: a runner stopped drops those it had not
    // started yet.
    if (recording_ > 0 || pgn_out_.writing()) {
        throw std::logic_error("a tournament cannot let go of its folder while it records a game");
    }
    read_only_ = true;
    journal_.reset();  // pairIfReady() and the writes stop at it
    pgn_.reset();
    pgn_chessbase_.reset();
    lock_.reset();
}

void TournamentState::setJournalListener(Journal::Listener listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (journal_) journal_->setListener(std::move(listener));
}

uint64_t TournamentState::journalSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return journal_size_;
}

bool TournamentState::follow(const nlohmann::json& e, uint64_t end) {
    if (!read_only_) throw std::logic_error("only a read-only tournament follows another program's journal");
    const Bump bump{version_};
    std::lock_guard<std::mutex> lock(mutex_);
    // What the writer wrote, the games being played as its status tells them. Undoing an edit needs what it changed,
    // which only a fresh open knows: an edit undone is not followed.
    bool undone          = false;
    auto reader          = applying();
    reader.game_finished = [this](const nlohmann::json& event) {
        const auto result = gameResultFromJson(event);
        replica_.running.erase(result.number());
        apply(result);
    };
    reader.edit_undone = [&](const nlohmann::json&) { undone = true; };
    readEvent(e, reader);
    if (undone) return false;
    journal_size_ = std::max(journal_size_, end);
    return true;
}

void TournamentState::setReplicaStatus(ReplicaStatus status) {
    if (!read_only_) return;
    const Bump bump{version_};
    std::lock_guard<std::mutex> lock(mutex_);
    replica_ = std::move(status);
}

bool TournamentState::finished() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return finished_;
}

std::string TournamentState::finishReason() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return finish_reason_;
}

std::vector<GameResult> TournamentState::results() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return results_;
}

bool TournamentState::requestEnd(uint64_t number, const std::string& result, const std::string& why) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (read_only_ || !running_.count(number) || ending_.count(number) || !validResult(result)) return false;
    end_requests_[number] = {result, why};
    return true;
}

namespace {

// The request for game `number` taken out of `requests`, if there is one.
std::optional<std::pair<std::string, std::string>> takeOut(
    std::map<uint64_t, std::pair<std::string, std::string>>& requests, uint64_t number) {
    const auto it = requests.find(number);
    if (it == requests.end()) return std::nullopt;
    auto request = std::move(it->second);
    requests.erase(it);
    return request;
}

}  // namespace

std::optional<std::pair<std::string, std::string>> TournamentState::takeEnd(uint64_t number) {
    std::lock_guard<std::mutex> lock(mutex_);
    ending_.erase(number);
    return takeOut(end_requests_, number);
}

std::optional<std::pair<std::string, std::string>> TournamentState::takeEndAsItEnds(uint64_t number) {
    std::lock_guard<std::mutex> lock(mutex_);
    ending_.insert(number);
    return takeOut(end_requests_, number);
}

std::optional<GameResult> TournamentState::resultOf(uint64_t number) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!done_.count(number)) return std::nullopt;
    for (auto it = results_.rbegin(); it != results_.rend(); ++it) {
        if (!it->voided && it->number() == number) return *it;
    }
    return std::nullopt;
}

std::map<uint32_t, fastchess::book::Opening> TournamentState::encounterOpenings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return started_;
}

std::map<uint64_t, fastchess::book::Opening> TournamentState::gameOpenings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<uint64_t, fastchess::book::Opening> out;
    const bool own = ownOpenings();
    for (const auto& [encounter, opening] : started_) {
        out.emplace(uint64_t{encounter} * 2, opening);
        if (!own) {
            out.emplace(uint64_t{encounter} * 2 + 1, opening);
        } else if (const auto it = second_.find(encounter); it != second_.end()) {
            out.emplace(uint64_t{encounter} * 2 + 1, it->second);
        }
    }
    return out;
}

fastchess::Stats TournamentState::pairStats(int a, int b) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = pairs_.find({std::min(a, b), std::max(a, b)});
    if (it == pairs_.end()) return {};
    return a < b ? it->second : ~it->second;
}

std::vector<fastchess::Stats> TournamentState::pairHistory(int a, int b) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<fastchess::Stats> history;
    std::map<uint32_t, const GameResult*> first;
    fastchess::Stats total;

    for (const auto& r : results_) {
        const bool between = (r.white == a && r.black == b) || (r.white == b && r.black == a);
        if (!between || r.voided) continue;

        const auto it = first.find(r.encounter);
        if (it == first.end()) {
            first.emplace(r.encounter, &r);
            continue;
        }
        total += encounterStats(*it->second, r, a);
        history.push_back(total);
        first.erase(it);
    }
    return history;
}

}  // namespace fcg::tour
