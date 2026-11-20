#pragma once

// Theory studies (#77, mockup M11, board 2): annotated PGN files the user adds, read where they are and read again when
// they change on the disk (RookStudio never writes into them); a Lichess study's PGN as the profile keeps it (#79),
// read again with the study; and the repertoires' comments, under their names. Their notes are indexed by position
// (study/theory_notes.hpp): the Board page's Tree tab says what they say at the position shown (its Theory column and
// "What your studies say" under the moves), the Notation tab how many notes there are. The list is kept in the profile
// (theory.json). Nothing leaves the machine for it.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <study/theory_notes.hpp>

namespace fcg {

struct TheorySource {
    enum class Kind { File, Lichess, Repertoire };
    Kind kind = Kind::File;
    std::string name;            // the file's name without .pgn, the study's, the repertoire's
    std::filesystem::path file;  // read where it is
    std::string lichess;         // a Lichess study's id
    bool shown = true;           // its notes in the tree (the repertoires: TheoryShelf::repertoiresShown())

    // What was read, and when.
    std::shared_ptr<const study::TheoryNotes> notes;
    int64_t read_at = 0;  // seconds since the epoch; 0 never
    int readings    = 0;  // this session's: more than one when it changed on the disk
    bool missing    = false;
    std::string error;  // why the file could not be read

    // Its reading under way, and the file as it was when it was read last.
    std::future<study::TheoryNotes> job;
    std::shared_ptr<std::atomic<uint64_t>> done;  // the bytes read
    std::shared_ptr<std::atomic<bool>> cancel;
    uint64_t total = 0;  // the size being read
    std::optional<std::filesystem::file_time_type> stamp, job_stamp;
    uintmax_t size = 0, job_size = 0;

    [[nodiscard]] bool reading() const { return job.valid(); }
    [[nodiscard]] double progress() const {
        return total && done ? std::min(1.0, static_cast<double>(done->load()) / static_cast<double>(total)) : 0.0;
    }
};

// A note found at a position, with where it comes from.
struct TheoryFound {
    TheorySource::Kind kind = TheorySource::Kind::File;
    std::string source;          // its name
    std::filesystem::path file;  // to open its game
    std::string lichess;
    const study::TheoryNote* note = nullptr;  // kept alive by TheoryAt::held
    std::string game;                         // its game's name, empty for none
    std::size_t games = 1;                    // the games of its file
    bool on_move      = false;                // on a move from the position (note->san); else about the position
    bool transposed   = false;                // its moves reach the position by another order than the board's
};

// What the notes on a move from the position say of it: its symbols, each once, and how many notes have more than
// symbols (a comment, an evaluation, arrows or squares).
struct MoveTheory {
    std::vector<uint8_t> symbols;
    int notes = 0;
};

struct TheoryAt {
    std::vector<TheoryFound> found;           // the sources in the list's order, the repertoires last; in each, the
                                              // notes about the position, then those on its moves, in the file's order
    std::map<std::string, MoveTheory> moves;  // by the moves' SAN
    std::vector<std::shared_ptr<const study::TheoryNotes>> held;
};

class TheoryShelf {
   public:
    // The theory studies of `profile` (theory.json), read at the first poll(). `keep`: false for a run that leaves the
    // profile as it was (captures): the list is not written.
    explicit TheoryShelf(std::filesystem::path profile, bool keep = true);
    ~TheoryShelf();
    TheoryShelf(const TheoryShelf&)            = delete;
    TheoryShelf& operator=(const TheoryShelf&) = delete;

    [[nodiscard]] const std::vector<TheorySource>& studies() const noexcept { return studies_; }
    [[nodiscard]] const std::vector<TheorySource>& repertoires() const noexcept { return repertoires_; }
    [[nodiscard]] bool repertoiresShown() const noexcept { return repertoires_shown_; }
    void showRepertoires(bool shown);

    // A PGN file added, its reading started: its index; for a file added before, its index (`added` false).
    std::size_t addFile(const std::filesystem::path& file, bool* added = nullptr);
    // A Lichess study by its id, its PGN where the profile keeps it (lichess::studyCache()).
    std::size_t addLichess(const std::string& id, const std::string& name, bool* added = nullptr);
    [[nodiscard]] std::optional<std::size_t> findLichess(const std::string& id) const;
    // Taken out of the theory, its file left where it is: what it was, to put it back (an undo).
    struct Removed {
        TheorySource::Kind kind = TheorySource::Kind::File;
        std::string name;
        std::filesystem::path file;
        std::string lichess;
        bool shown     = true;
        std::size_t at = 0;
    };
    std::optional<Removed> remove(std::size_t index);
    std::size_t restore(const Removed& removed);
    void setShown(std::size_t index, bool shown);
    // The file found elsewhere (it was moved): read there.
    void relocate(std::size_t index, const std::filesystem::path& file);
    // The repertoires, their names and files: their sources follow them.
    void syncRepertoires(const std::vector<std::pair<std::string, std::filesystem::path>>& files);

    // The readings finished taken in; at most every `every`, the files looked at (a stat each), those changed since
    // their reading read again in the background. True when what the notes say changed.
    bool poll(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now(),
              std::chrono::milliseconds every           = std::chrono::milliseconds(1000));
    // The readings under way waited for and taken in (the tests).
    void wait();
    [[nodiscard]] bool reading() const;
    // One more each time what the notes say changes: a reading, a source shown or hidden, added or removed.
    [[nodiscard]] uint64_t generation() const noexcept { return generation_; }
    // Whether a source shown has notes.
    [[nodiscard]] bool anyNotes() const;

    // What the sources shown say at a position (its key). `line`: the board's moves from the standard start to it, in
    // SAN, to tell a transposition; nullptr when it does not start there.
    [[nodiscard]] TheoryAt at(uint64_t key, const std::vector<std::string>* line) const;

   private:
    void save() const;
    void read(TheorySource& source);
    void load();

    std::filesystem::path profile_;
    bool keep_ = true;
    std::vector<TheorySource> studies_;
    std::vector<TheorySource> repertoires_;
    bool repertoires_shown_ = true;
    uint64_t generation_    = 1;
    std::chrono::steady_clock::time_point looked_{};
    bool looked_once_ = false;
};

}  // namespace fcg
