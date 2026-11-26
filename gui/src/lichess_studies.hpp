#pragma once

// The user's Lichess studies (#79, mockup M11, board 4): listed (public ones without a token allowed to read studies,
// all of them with one), read chapter by chapter from their PGN export, imported into a database (each chapter a game;
// read again, only the changed chapters replaced), followed (a HEAD request and its Last-Modified say whether a study
// changed, nothing downloaded), and filled from the Board: a game sent as a chapter of one of them or of a new one
// (study:write), its moves sent again later on request. Lichess's limits: 64 chapters a study, 30 new studies a day.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <json.hpp>
#include <study/game.hpp>

#include "lichess_api.hpp"

namespace fcg::study {
class Database;
}

namespace fcg::lichess {

inline constexpr int STUDY_CHAPTERS = 64;  // the chapters a study holds at most
inline constexpr int STUDIES_A_DAY  = 30;  // the studies an account may make in a day

using Tags = std::vector<std::pair<std::string, std::string>>;

// A study as the list of a user's gives it: its name and dates (milliseconds since the epoch). Lichess says nothing
// else there: neither its visibility nor its chapters.
struct StudyInfo {
    std::string id, name;
    int64_t created = 0, updated = 0;
    bool operator==(const StudyInfo&) const = default;
};
// The studies of an NDJSON list, a line each; a line that is not one is passed over.
[[nodiscard]] std::vector<StudyInfo> parseStudyList(std::string_view ndjson);
// A user's studies, the most recently changed first as Lichess lists them: with `token` (one allowed to read studies)
// the private and unlisted ones too, without it the public ones.
[[nodiscard]] Answer<std::vector<StudyInfo>> userStudies(const std::string& username, const std::string& token,
                                                         const std::atomic<bool>* cancel = nullptr);

// A chapter of a study's PGN export.
struct StudyChapter {
    std::string id;         // from its ChapterURL tag; empty when it has none
    std::string name;       // its ChapterName tag, else its players, else its event
    std::string pgn;        // its text as Lichess wrote it
    uint64_t hash = 0;      // of that text: a chapter that changed is told at the next reading
    int plies     = 0;      // its main line's
    bool black    = false;  // shown from Black ([Orientation "black"])
};
[[nodiscard]] std::vector<StudyChapter> parseStudyPgn(std::string_view pgn);

// A study's PGN export: its chapters with their comments, variations, clocks and orientation (`whole`), or their main
// lines alone, to count them; with its Last-Modified as Lichess wrote it. `token`: one allowed to read studies, for a
// private or unlisted study.
struct StudyText {
    std::string pgn;
    std::string modified;
};
[[nodiscard]] Answer<StudyText> studyText(const std::string& id, const std::string& token, bool whole = true,
                                          const std::atomic<bool>* cancel = nullptr);
// HEAD of the same: when the study last changed (its Last-Modified), nothing downloaded.
[[nodiscard]] Answer<std::string> studyModified(const std::string& id, const std::string& token,
                                                const std::atomic<bool>* cancel = nullptr);
// An HTTP date ("Wed, 18 Nov 2026 10:12:00 GMT") in seconds since the epoch; none when it is not one.
[[nodiscard]] std::optional<int64_t> httpTime(std::string_view date);
// Whether a study changed since it was read: its Last-Modified now against the one of that reading, else against the
// time of that reading (seconds since the epoch).
[[nodiscard]] bool changedSince(const std::string& modified_now, const std::string& modified_read, int64_t read_at);

enum class Visibility { Private, Unlisted, Public };
[[nodiscard]] const char* visibilityKey(Visibility visibility);  // "private", "unlisted", "public"
[[nodiscard]] std::optional<Visibility> visibilityOf(std::string_view key);
// A name Lichess takes for a study: 2 to 100 characters.
[[nodiscard]] bool studyNameOk(std::string_view name);
// A full study (64 chapters) takes no more: it is not offered.
[[nodiscard]] constexpr bool studyFull(int chapters) noexcept { return chapters >= STUDY_CHAPTERS; }
[[nodiscard]] std::string studyUrl(const std::string& study);
[[nodiscard]] std::string chapterUrl(const std::string& study, const std::string& chapter);

// POST /api/study's form: the name, the visibility always (Lichess's description and schema give two defaults), and
// the rights Lichess requires, as its own page sets them for a new study.
[[nodiscard]] Form newStudyForm(const std::string& name, Visibility visibility);
// A new study: its id. Lichess makes it with an empty first chapter.
[[nodiscard]] Answer<std::string> createStudy(const std::string& name, Visibility visibility, const std::string& token,
                                              const std::atomic<bool>* cancel = nullptr);

// A chapter sent: its game's PGN, its name, the side it is shown from.
struct ChapterSent {
    std::string pgn, name;
    bool black    = false;
    bool chess960 = false;
};
// POST /api/study/{id}/import-pgn's form.
[[nodiscard]] Form chapterForm(const ChapterSent& chapter);
struct ChapterMade {
    std::string id, name;
};
// The chapters Lichess made of it: none when the study is full, with its reason when it gave one.
[[nodiscard]] Answer<std::vector<ChapterMade>> importChapter(const std::string& study, const ChapterSent& chapter,
                                                             const std::string& token,
                                                             const std::atomic<bool>* cancel = nullptr);
// A chapter's moves replaced by those of `pgn` (its comments, variations and marks with them; its tags are not read),
// its tags by those of `tags` (tagsPgn()), a chapter deleted: true once Lichess did it.
[[nodiscard]] Answer<bool> updateChapterMoves(const std::string& study, const std::string& chapter,
                                              const std::string& pgn, const std::string& token,
                                              const std::atomic<bool>* cancel = nullptr);
[[nodiscard]] Answer<bool> updateChapterTags(const std::string& study, const std::string& chapter,
                                             const std::string& tags, const std::string& token,
                                             const std::atomic<bool>* cancel = nullptr);
[[nodiscard]] Answer<bool> deleteChapter(const std::string& study, const std::string& chapter, const std::string& token,
                                         const std::atomic<bool>* cancel = nullptr);

// The game as it goes to a study: its headers, moves, variations, comments, symbols, arrows, squares and clocks. The
// engines' data, the evaluations and other programs' commands stay out, and the assistant's comments too unless
// `assistant`.
[[nodiscard]] GameTree studyGame(const GameTree& tree, bool assistant);
[[nodiscard]] std::string studyGamePgn(const GameTree& tree, bool chess960, bool assistant);
// How many comments of the assistant's the game has.
[[nodiscard]] int assistantComments(const GameTree& tree);
// The game's tags as a chapter keeps them: its headers and result, PlyCount left out.
[[nodiscard]] Tags gameTags(const GameTree& tree);
// Tags as a PGN's, for a chapter's: those of `now`, then those of `before` it no longer has, empty (Lichess deletes
// them so).
[[nodiscard]] std::string tagsPgn(const Tags& now, const Tags& before = {});

// A game sent to a study: one of the user's (`study`), or a new one named `new_name` when `study` is empty.
struct SendRequest {
    std::string study;
    int chapters =
        -1;  // the study's chapters when known; else counted first (its main lines asked), a full one refused
    std::string new_name;
    Visibility visibility = Visibility::Private;
    ChapterSent chapter;
};
struct SendResult {
    std::string study, chapter;  // their ids
    std::string chapter_name;
    int chapters = -1;  // the study's, this one included, when known
    bool created = false;
};
// Into a new study: made, the chapter imported, then its empty first chapter deleted (Lichess makes one with it). An
// error may come with a value: the study made though the chapter was not, or the chapters of a full one counted.
[[nodiscard]] Answer<SendResult> sendToStudy(const SendRequest& request, const std::string& token,
                                             const std::atomic<bool>* cancel = nullptr);

// A study's chapters in a database, each a game: those known from an earlier import replaced when they changed, the
// others added at the end; a game the user deleted in the database stays deleted.
struct ImportedChapter {
    uint64_t game                                 = 0;  // its number in the database
    uint64_t hash                                 = 0;  // of the chapter's text that went there
    bool operator==(const ImportedChapter&) const = default;
};
struct StudyImport {
    std::map<std::string, ImportedChapter> games;  // by chapter id, after the import
    uint64_t added = 0, replaced = 0, unchanged = 0, left_deleted = 0, unreadable = 0;
    std::string error;
};
[[nodiscard]] StudyImport importStudy(study::Database& db, const std::vector<StudyChapter>& chapters,
                                      const std::map<std::string, ImportedChapter>& before);

// What the profile keeps (lichess-studies.json), and the text of each study when it was last read
// (lichess-cache/studies/<id>.pgn), shown again offline.
struct ChapterState {
    std::string id, name;
    uint64_t hash = 0;
    bool changed  = false;  // new or different at the last reading
};
struct StudyRecord {
    std::string id, name;
    std::string visibility;  // known for the studies RookStudio made; empty else (Lichess's list does not say it)
    int chapters    = -1;    // -1 while not counted
    bool followed   = false;
    int64_t read_at = 0;                           // when it was read last, seconds since the epoch; 0 never
    std::string modified;                          // its Last-Modified at that reading
    std::string checked;                           // its Last-Modified at the last HEAD request
    std::vector<ChapterState> read;                // its chapters at that reading
    std::filesystem::path database;                // the database it was imported into, empty when it was not
    std::map<std::string, ImportedChapter> games;  // its chapters there
};
// A game sent as a chapter, or a chapter of the user's own opened on the Board: the game's bar says so, and how many
// changes since.
struct SentRecord {
    std::string study, chapter;
    std::string study_name, chapter_name;
    std::string visibility;
    std::string pgn;         // the game as it went last, as studyGamePgn() writes it
    Tags tags;               // its tags then
    bool assistant = false;  // with the assistant's comments
    bool black     = false;
    bool chess960  = false;
    bool opened    = false;        // a chapter opened from the user's study, not sent from RookStudio
    std::filesystem::path folder;  // the database the game is in, when it is in one
    uint64_t game = 0;
    [[nodiscard]] std::string url() const { return chapterUrl(study, chapter); }
};
struct StudiesState {
    bool opened        = false;  // the Studies part was opened: the permission to read studies asked then
    bool read_declined = false;  // "Without it" said to reading the private and unlisted studies
    bool shown         = false;  // the part open on the Lichess page
    std::string last_study;      // the study the last game went into: offered first
    std::vector<int64_t> made;   // when RookStudio made a study, seconds since the epoch, the last day's
    std::vector<StudyRecord> studies;
    std::vector<SentRecord> sent;
    [[nodiscard]] StudyRecord* find(std::string_view id);
    [[nodiscard]] const StudyRecord* find(std::string_view id) const;
    StudyRecord& record(const std::string& id, const std::string& name);  // found, or added
};
[[nodiscard]] StudiesState loadStudies(const std::filesystem::path& profile);
void saveStudies(const std::filesystem::path& profile, const StudiesState& state);  // throws std::runtime_error
[[nodiscard]] std::filesystem::path studyCache(const std::filesystem::path& profile, const std::string& id);
// A sent game's record as JSON, for the recent games too (recent.json); none when it is not one.
[[nodiscard]] nlohmann::json sentJson(const SentRecord& sent);
[[nodiscard]] std::optional<SentRecord> sentOf(const nlohmann::json& j);

// The chapters of a reading marked against those of the reading before: changed when new or different.
void markChanged(std::vector<ChapterState>& now, const std::vector<ChapterState>& before);
// The studies made within the last 24 hours, the older ones forgotten.
int madeToday(std::vector<int64_t>& made, int64_t now);
// The user's studies a game may go into: those not known to be full, in the list's order.
[[nodiscard]] std::vector<StudyInfo> roomyStudies(const std::vector<StudyInfo>& studies, const StudiesState& state);
// A text's hash, the same on every machine (FNV-1a), its line ends and trailing spaces left out.
[[nodiscard]] uint64_t textHash(std::string_view text);

}  // namespace fcg::lichess
