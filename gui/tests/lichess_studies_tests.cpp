// The user's Lichess studies (#79): their list, the HEAD that says whether one changed, a study made and a game sent
// into it (what each request carries, the visibility always), the full-study rule, a chapter's moves and tags sent
// again, the game made fit to send, a study imported into a database and read again, and what the profile keeps;
// against a server on this machine that answers as Lichess does.
#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <study/import.hpp>
#include <study/pgn_reader.hpp>
#include <study/store.hpp>

#include "answering_server.hpp"
#include "assistant_notes.hpp"
#include "free_ports.hpp"
#include "https.hpp"
#include "lichess_api.hpp"
#include "lichess_studies.hpp"
#include "scripted_server.hpp"

using namespace fcg;
using namespace fcg::lichess;
using namespace fcg::test;
namespace fs = std::filesystem;

namespace {

constexpr const char* USER     = "rs_sample_0000";  // no one's name
constexpr const char* MODIFIED = "Wed, 18 Nov 2026 10:12:00 GMT";

std::string chapter(const std::string& study, const std::string& id, const std::string& name,
                    const std::string& moves) {
    return fmt::format(
        "[Event \"Openings: {2}\"]\n[Result \"*\"]\n[StudyName \"Openings\"]\n[ChapterName \"{2}\"]\n"
        "[ChapterURL \"https://lichess.org/study/{0}/{1}\"]\n[Annotator \"https://lichess.org/@/{4}\"]\n\n{3} *\n\n\n",
        study, id, name, moves, USER);
}

std::string studyOf(const std::string& id, int chapters) {
    std::string out;
    for (int i = 0; i < chapters; ++i)
        out += chapter(id, fmt::format("Chap{:04}", i + 1), fmt::format("Line {}", i + 1), "1. e4 e5 2. Nf3");
    return out;
}

std::string formValue(const std::string& body, const std::string& name) {
    const auto at = ("&" + body).find("&" + name + "=");
    if (at == std::string::npos) return "<none>";
    const auto from = at + name.size() + 1;
    return body.substr(from, body.find('&', from) - from);
}

// Lichess, as far as studies go: a user's list, the studies' exports and HEAD, a study made, chapters imported, moves
// and tags replaced, a chapter deleted. Each request kept: its method, path, token and body.
struct Lichess {
    struct Seen {
        std::string method, path, authorization, body;
    };
    Lichess()
        : port(freePort()), server(port, [this](const std::string& head, net::TcpConnection& c) {
              Seen s;
              s.method          = head.substr(0, head.find(' '));
              s.path            = requestPath(head);
              s.authorization   = httpHeader(head, "Authorization").value_or("");
              const auto length = std::stoul(httpHeader(head, "Content-Length").value_or("0"));
              s.body            = head.substr(head.find("\r\n\r\n") + 4);
              while (s.body.size() < length) {
                  const auto more = c.readUntil("\x01never", length - s.body.size(), 2000);
                  if (more.empty()) break;
                  s.body += more;
              }
              {
                  std::lock_guard lock(mutex);
                  seen.push_back(s);
              }
              const bool reader = s.authorization == "Bearer reader" || s.authorization == "Bearer writer";
              const bool writer = s.authorization == "Bearer writer";
              int status        = 200;
              std::string body, type = "application/json", extra;
              const auto& p = s.path;
              if (p == fmt::format("/api/study/by/{}", USER)) {
                  type = "application/x-ndjson";
                  body = R"({"id":"AbCdEfGh","name":"Openings","createdAt":1763000000000,"updatedAt":1763400000000})"
                         "\n\n";
                  if (reader)
                      body +=
                          R"({"id":"PrivStdy","name":"Préparation","createdAt":1763000000000,"updatedAt":1763500000000})"
                          "\n"
                          R"({"id":"FullStdy","name":"Rook endings","createdAt":1762000000000,"updatedAt":1762100000000})"
                          "\n";
              } else if (p.rfind("/api/study/by/", 0) == 0) {
                  status = 404;
              } else if (p.rfind("/api/study/AbCdEfGh.pgn", 0) == 0 || p.rfind("/api/study/NewStdy1.pgn", 0) == 0 ||
                         p.rfind("/api/study/FullStdy.pgn", 0) == 0 || p.rfind("/api/study/PrivStdy.pgn", 0) == 0) {
                  const auto id = p.substr(11, 8);
                  extra         = fmt::format("Last-Modified: {}\r\n", MODIFIED);
                  type          = "application/x-chess-pgn";
                  if (id == "PrivStdy" && !reader) {
                      status = 404;
                  } else if (id == "NewStdy1") {
                      // As Lichess makes it: an empty first chapter, then the one imported.
                      body = chapter(id, "Empty001", "Chapter 1", "") + chapter(id, "Chap0042", "Sent", "1. d4 d5");
                  } else {
                      body = studyOf(id, id == "FullStdy" ? STUDY_CHAPTERS : 3);
                  }
                  if (s.method == "HEAD") {
                      status = 204;
                      body.clear();
                  }
              } else if (s.method == "POST" && p == "/api/study") {
                  status = writer ? 200 : 403;
                  body   = writer ? R"({"id":"NewStdy1"})" : R"({"error":"Missing scope"})";
              } else if (s.method == "POST" && p.rfind("/api/study/", 0) == 0 &&
                         p.find("/import-pgn") != std::string::npos) {
                  if (!writer) {
                      status = 403;
                      body   = R"({"error":"Missing scope"})";
                  } else if (p.find("FullStdy") != std::string::npos) {
                      body = R"({"chapters":[],"error":"This study has too many chapters"})";
                  } else {
                      body =
                          R"({"chapters":[{"id":"Chap0042","name":"Sent","players":[{"name":"A"},{"name":"B"}],"status":"*"}],"error":null})";
                  }
              } else if (s.method == "POST" && (p.ends_with("/moves") || p.ends_with("/tags"))) {
                  if (p.find("Gone0000") != std::string::npos) {
                      status = 400;
                      body   = R"({"error":"Chapter not found"})";
                  } else {
                      status = 204;
                  }
              } else if (s.method == "DELETE" && p.rfind("/api/study/", 0) == 0) {
                  status = 204;
              } else {
                  status = 404;
              }
              c.sendAll(fmt::format(
                  "HTTP/1.1 {} X\r\nContent-Type: {}\r\n{}Content-Length: {}\r\nConnection: close\r\n\r\n{}", status,
                  type, extra, s.method == "HEAD" ? 0 : body.size(), body));
          }) {
        REQUIRE(server.ok());
        servers().api = fmt::format("http://127.0.0.1:{}", port);
    }
    ~Lichess() { servers() = Servers{}; }
    std::vector<Seen> requests() {
        std::lock_guard lock(mutex);
        return seen;
    }
    std::mutex mutex;
    std::vector<Seen> seen;
    uint16_t port;
    ScriptedServer server;
};

std::shared_ptr<study::Database> emptyDatabase(const std::string& name) {
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    return study::Database::create(dir);
}

}  // namespace

TEST_CASE("Lichess studies: a user's list, its private ones with a token allowed to read them") {
    // Each line a study; the keep-alives, a broken line and a line without an id passed over.
    const auto list = parseStudyList(
        "{\"id\":\"AbCdEfGh\",\"name\":\"Openings\",\"createdAt\":1763000000000,\"updatedAt\":1763400000000}\n"
        "\n"
        "{\"id\":\"PrivStdy\",\"name\":\"Pr\xC3\xA9paration\",\"createdAt\":1,\"updatedAt\":2}\r\n"
        "{\"id\":\"cut\n"
        "{\"name\":\"no id\"}\n"
        "{\"id\":\"NoDates1\",\"name\":\"Endings\"}");
    REQUIRE(list.size() == 3);
    CHECK(list[0] == StudyInfo{"AbCdEfGh", "Openings", 1763000000000, 1763400000000});
    CHECK(list[1].name == "Pr\xC3\xA9paration");
    CHECK(list[2].updated == 0);

    Lichess lichess;
    const auto mine = userStudies(USER, "reader");
    REQUIRE(mine.value);
    CHECK(mine.value->size() == 3);
    const auto open = userStudies(USER, "");
    REQUIRE(open.value);
    REQUIRE(open.value->size() == 1);  // the public one alone
    CHECK(open.value->front().id == "AbCdEfGh");
    const auto nobody = userStudies("nobody_here", "");
    CHECK_FALSE(nobody.value);
    CHECK(nobody.status == 404);
    CHECK(describe(nobody) == "Lichess has no account of that name");
    const auto seen = lichess.requests();
    REQUIRE(seen.size() == 3);
    CHECK(seen[0].method == "GET");
    CHECK(seen[0].path == "/api/study/by/rs_sample_0000");
    CHECK(seen[0].authorization == "Bearer reader");
    CHECK(seen[1].authorization.empty());  // no token without the permission: the public studies

    // A study read: its chapters, their names, ids, hashes and orientation, with its Last-Modified.
    const auto text = studyText("AbCdEfGh", "");
    REQUIRE(text.value);
    CHECK(text.value->modified == MODIFIED);
    const auto chapters = parseStudyPgn(text.value->pgn);
    REQUIRE(chapters.size() == 3);
    CHECK(chapters[0].id == "Chap0001");
    CHECK(chapters[1].name == "Line 2");
    CHECK(chapters[0].plies == 3);
    CHECK(chapters[0].hash != 0);
    CHECK(lichess.requests().back().path ==
          "/api/study/AbCdEfGh.pgn?clocks=true&comments=true&variations=true&orientation=true");
    const auto oriented =
        parseStudyPgn("[Event \"E\"]\n[White \"A\"]\n[Black \"B\"]\n[Orientation \"black\"]\n\n1. e4 *\n");
    REQUIRE(oriented.size() == 1);
    CHECK(oriented[0].black);
    CHECK(oriented[0].name == "A – B");
    CHECK(oriented[0].id.empty());
    // The same text but its line ends and trailing spaces: the same hash.
    CHECK(textHash("[Event \"E\"]\r\n\r\n1. e4 e5  \r\n") == textHash("[Event \"E\"]\n\n1. e4 e5\n"));
    CHECK(textHash("1. e4 e5") != textHash("1. e4 e6"));
}

TEST_CASE("Lichess studies: the HEAD request says whether a followed study changed, nothing downloaded") {
    Lichess lichess;
    const auto modified = studyModified("AbCdEfGh", "");
    REQUIRE(modified.value);
    CHECK(*modified.value == MODIFIED);
    const auto seen = lichess.requests();
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].method == "HEAD");
    CHECK(seen[0].path == "/api/study/AbCdEfGh.pgn");
    CHECK(studyModified("Missing0", "").status == 404);

    REQUIRE(httpTime(MODIFIED));
    CHECK(*httpTime(MODIFIED) == 1794996720);  // 2026-11-18 10:12:00 UTC
    CHECK(*httpTime("Thu, 01 Jan 1970 00:00:00 GMT") == 0);
    CHECK_FALSE(httpTime("yesterday"));
    // Against the Last-Modified of the last reading, else against the time of that reading.
    CHECK_FALSE(changedSince(MODIFIED, MODIFIED, 0));
    CHECK(changedSince("Thu, 19 Nov 2026 08:00:00 GMT", MODIFIED, 0));
    CHECK_FALSE(changedSince("Tue, 17 Nov 2026 08:00:00 GMT", MODIFIED, 0));
    CHECK(changedSince(MODIFIED, "", 1794996720 - 60));
    CHECK_FALSE(changedSince(MODIFIED, "", 1794996720 + 60));
    CHECK_FALSE(changedSince("", MODIFIED, 0));
    CHECK(changedSince("not a date", "another", 0));
}

TEST_CASE("Lichess studies: a study made and a chapter imported, the visibility always sent") {
    // The visibility goes whatever it is: Lichess's description and schema give two defaults.
    const auto form = newStudyForm("  My annotated games 2026 ", Visibility::Private);
    REQUIRE(form.size() == 7);
    CHECK(form[0] == std::pair<std::string, std::string>{"name", "My annotated games 2026"});
    CHECK(form[1] == std::pair<std::string, std::string>{"visibility", "private"});
    for (const char* right : {"computer", "explorer", "cloneable", "shareable", "chat"})
        CHECK(std::any_of(form.begin(), form.end(), [&](const auto& f) { return f.first == right; }));
    CHECK(studyNameOk("Ab"));
    CHECK_FALSE(studyNameOk(" A "));
    CHECK(studyNameOk(std::string(100, 'x')));
    CHECK_FALSE(studyNameOk(std::string(101, 'x')));
    CHECK(studyNameOk("\xC3\xA9t\xC3\xA9"));  // three characters, five bytes

    Lichess lichess;
    const auto made = createStudy("Mes parties", Visibility::Unlisted, "writer");
    REQUIRE(made.value);
    CHECK(*made.value == "NewStdy1");
    const auto refused = createStudy("Mes parties", Visibility::Public, "reader");
    CHECK_FALSE(refused.value);
    CHECK(refused.status == 403);
    CHECK(refused.net == NetError::Forbidden);
    CHECK(describe(refused) == "the token may not do it: one more permission is needed");
    const auto too_short = createStudy("M", Visibility::Public, "writer");  // refused here, nothing asked
    CHECK_FALSE(too_short.value);

    ChapterSent sent;
    sent.pgn      = "[Event \"Club\"]\n[White \"A\"]\n[Black \"B\"]\n\n1. e4 e5 *\n";
    sent.name     = "A – B, round 7";
    sent.black    = true;
    const auto ch = importChapter("AbCdEfGh", sent, "writer");
    REQUIRE(ch.value);
    REQUIRE(ch.value->size() == 1);
    CHECK(ch.value->front().id == "Chap0042");
    sent.chess960 = true;
    sent.black    = false;
    (void)importChapter("AbCdEfGh", sent, "writer");

    const auto seen = lichess.requests();
    REQUIRE(seen.size() == 4);
    CHECK(seen[0].method == "POST");
    CHECK(seen[0].path == "/api/study");
    CHECK(seen[0].authorization == "Bearer writer");
    CHECK(formValue(seen[0].body, "name") == "Mes+parties");
    CHECK(formValue(seen[0].body, "visibility") == "unlisted");
    CHECK(formValue(seen[1].body, "visibility") == "public");
    CHECK(formValue(seen[0].body, "chat") == "member");
    CHECK(seen[2].path == "/api/study/AbCdEfGh/import-pgn");
    CHECK(seen[2].body == formEncode({{"pgn", sent.pgn}, {"name", "A – B, round 7"}, {"orientation", "black"}}));
    CHECK(formValue(seen[3].body, "orientation") == "white");
    CHECK(formValue(seen[3].body, "variant") == "chess960");
    CHECK(formValue(seen[2].body, "variant") == "<none>");
    // A name longer than Lichess takes is cut at 100 characters.
    sent.name = std::string(120, 'n');
    CHECK(formValue(formEncode(chapterForm(sent)), "name") == std::string(100, 'n'));
}

TEST_CASE("Lichess studies: a full study is not offered, and a game sent into one is refused") {
    CHECK_FALSE(studyFull(63));
    CHECK(studyFull(STUDY_CHAPTERS));
    StudiesState state;
    state.record("FullStdy", "Rook endings").chapters = 64;
    state.record("AbCdEfGh", "Openings").chapters     = 12;
    const std::vector<StudyInfo> list                 = {
        {"AbCdEfGh", "Openings", 0, 0}, {"FullStdy", "Rook endings", 0, 0}, {"PrivStdy", "Préparation", 0, 0}};
    const auto roomy = roomyStudies(list, state);
    REQUIRE(roomy.size() == 2);  // the full one left out; the one not counted offered
    CHECK(roomy[0].id == "AbCdEfGh");
    CHECK(roomy[1].id == "PrivStdy");

    Lichess lichess;
    // Its chapters not known: counted first, and the game refused without being sent.
    SendRequest r;
    r.study         = "FullStdy";
    r.chapter.pgn   = "[Event \"Club\"]\n\n1. e4 *\n";
    const auto full = sendToStudy(r, "writer");
    CHECK(full.error.find("full") != std::string::npos);
    REQUIRE(full.value);  // what was counted, to offer it no more
    CHECK(full.value->chapters == STUDY_CHAPTERS);
    CHECK(full.value->chapter.empty());
    auto seen = lichess.requests();
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].path == "/api/study/FullStdy.pgn?clocks=false&comments=false&variations=false");
    // Known not to be: sent at once, the count going on. And when Lichess refuses the chapter, its reason.
    r.chapters      = 63;
    const auto took = importChapter("FullStdy", r.chapter, "writer");
    CHECK_FALSE(took.value);
    CHECK(describe(took) == "This study has too many chapters");
    r.study         = "AbCdEfGh";
    r.chapters      = 12;
    const auto sent = sendToStudy(r, "writer");
    REQUIRE(sent.value);
    CHECK(sent.value->chapters == 13);
    CHECK(sent.value->chapter == "Chap0042");
    CHECK_FALSE(sent.value->created);
    seen = lichess.requests();
    REQUIRE(seen.size() == 3);
    CHECK(seen[2].path == "/api/study/AbCdEfGh/import-pgn");
}

TEST_CASE("Lichess studies: a game sent into a new study, its empty first chapter deleted") {
    Lichess lichess;
    SendRequest r;
    r.new_name      = "My annotated games";
    r.visibility    = Visibility::Private;
    r.chapter.pgn   = "[Event \"Club\"]\n\n1. d4 d5 *\n";
    r.chapter.name  = "Sent";
    const auto sent = sendToStudy(r, "writer");
    REQUIRE(sent.value);
    CHECK(sent.value->study == "NewStdy1");
    CHECK(sent.value->chapter == "Chap0042");
    CHECK(sent.value->created);
    CHECK(sent.value->chapters == 1);
    const auto seen = lichess.requests();
    REQUIRE(seen.size() == 4);
    CHECK((seen[0].method + " " + seen[0].path) == "POST /api/study");
    CHECK(formValue(seen[0].body, "visibility") == "private");
    CHECK((seen[1].method + " " + seen[1].path) == "POST /api/study/NewStdy1/import-pgn");
    CHECK(seen[2].method == "GET");
    CHECK((seen[3].method + " " + seen[3].path) == "DELETE /api/study/NewStdy1/Empty001");
    // Without the permission to write: nothing made.
    const auto refused = sendToStudy(r, "reader");
    CHECK_FALSE(refused.value);
    CHECK(refused.status == 403);
}

TEST_CASE("Lichess studies: a chapter's moves and tags sent again") {
    Lichess lichess;
    const std::string pgn = "[Event \"Club\"]\n[White \"A\"]\n\n1. e4 { Best by test. } e5 2. Nf3 *\n";
    const auto moves      = updateChapterMoves("AbCdEfGh", "Chap0042", pgn, "writer");
    REQUIRE(moves.value);
    const Tags before = {{"Event", "Club"}, {"White", "A"}, {"Annotator", "me"}, {"Result", "*"}};
    const Tags now    = {{"Event", "Club \"B\""}, {"White", "A"}, {"Result", "1-0"}};
    const auto tags   = tagsPgn(now, before);
    CHECK(tags == "[Event \"Club \\\"B\\\"\"]\n[White \"A\"]\n[Result \"1-0\"]\n[Annotator \"\"]\n");
    REQUIRE(updateChapterTags("AbCdEfGh", "Chap0042", tags, "writer").value);
    const auto gone = updateChapterMoves("AbCdEfGh", "Gone0000", pgn, "writer");
    CHECK_FALSE(gone.value);
    CHECK(gone.net == NetError::Server);
    CHECK(describe(gone) == "Chapter not found");
    const auto seen = lichess.requests();
    REQUIRE(seen.size() == 3);
    CHECK((seen[0].method + " " + seen[0].path) == "POST /api/study/AbCdEfGh/Chap0042/moves");
    CHECK(seen[0].body == formEncode({{"pgn", pgn}}));
    CHECK((seen[1].method + " " + seen[1].path) == "POST /api/study/AbCdEfGh/Chap0042/tags");
    CHECK(seen[1].body == formEncode({{"pgn", tags}}));
}

TEST_CASE("Lichess studies: the game as it goes, with or without the assistant's comments") {
    const std::string pgn =
        "[Event \"Club\"]\n[White \"A\"]\n[Black \"B\"]\n[Result \"*\"]\n[PlyCount \"4\"]\n\n"
        "{ Before the first move. } 1. e4! { +0.35/18 1.234s } 1... e5 { [%clk 0:05:00] [%eval 0.17] [%tqu \"x\"] "
        "Mine. } 2. Nf3 { [%cal Ge2e4] [%csl Rd5] [%rs assistant] The knight develops with tempo. } "
        "( 2. Bc4 { Another. } ) 2... Nc6 *\n";
    const auto read = study::readPgnGame(pgn);
    REQUIRE(read.tree);
    CHECK(assistantComments(*read.tree) == 1);
    const auto without = studyGamePgn(*read.tree, false, false);
    CHECK(without.find("Before the first move.") != std::string::npos);
    CHECK(without.find("e4!") != std::string::npos);
    CHECK(without.find("+0.35") == std::string::npos);  // the engine's data
    CHECK(without.find("[%eval") == std::string::npos);
    CHECK(without.find("[%tqu") == std::string::npos);
    CHECK(without.find("[%rs") == std::string::npos);
    CHECK(without.find("[%clk 0:05:00]") != std::string::npos);
    CHECK(without.find("[%cal Ge2e4]") != std::string::npos);
    CHECK(without.find("[%csl Rd5]") != std::string::npos);
    CHECK(without.find("Mine.") != std::string::npos);
    CHECK(without.find("(2. Bc4") != std::string::npos);
    CHECK(without.find("knight develops") == std::string::npos);
    const auto with = studyGamePgn(*read.tree, false, true);
    CHECK(with.find("The knight develops with tempo.") != std::string::npos);
    CHECK(with.find("[%rs") == std::string::npos);
    // What it writes reads back to the same game, as sent.
    const auto again = study::readPgnGame(without);
    REQUIRE(again.tree);
    CHECK(studyGamePgn(*again.tree, false, false) == without);
    // Its tags as a chapter keeps them: PlyCount left out, the result last.
    const auto tags = gameTags(*read.tree);
    REQUIRE_FALSE(tags.empty());
    CHECK(tags.back() == std::pair<std::string, std::string>{"Result", "*"});
    CHECK(std::none_of(tags.begin(), tags.end(), [](const auto& t) { return t.first == "PlyCount"; }));
}

TEST_CASE("Lichess studies: imported into a database, read again, only the changed chapters replaced") {
    auto db = emptyDatabase("lichess_study_import");
    REQUIRE(db);
    // Something of the user's there already.
    study::ImportedGame mine;
    REQUIRE(study::readImportedGame("[Event \"Mine\"]\n\n1. c4 *\n", mine));
    (void)db->append({mine});

    const std::string id = "AbCdEfGh";
    const auto first     = parseStudyPgn(chapter(id, "Chap0001", "Najdorf", "1. e4 c5") +
                                         chapter(id, "Chap0002", "Dragon", "1. e4 c5 2. Nf3 d6") +
                                         chapter(id, "Chap0003", "Sveshnikov", "1. e4 c5 2. Nf3 Nc6"));
    const auto one       = importStudy(*db, first, {});
    CHECK(one.error.empty());
    CHECK(one.added == 3);
    REQUIRE(one.games.size() == 3);
    CHECK(one.games.at("Chap0001").game == 1);
    CHECK(one.games.at("Chap0003").game == 3);
    CHECK(db->snapshot()->games() == 4);

    // The user deletes the first chapter's game; on Lichess the second changes, the third stays, a fourth comes.
    const study::GameId deleted[] = {1};
    db->setDeleted(deleted, true);
    const auto second = parseStudyPgn(chapter(id, "Chap0001", "Najdorf", "1. e4 c5 2. Nf3") +
                                      chapter(id, "Chap0002", "Dragon", "1. e4 c5 2. Nf3 d6 3. d4 cxd4") +
                                      chapter(id, "Chap0003", "Sveshnikov", "1. e4 c5 2. Nf3 Nc6") +
                                      chapter(id, "Chap0004", "Kan", "1. e4 c5 2. Nf3 e6"));
    const auto two    = importStudy(*db, second, one.games);
    CHECK(two.error.empty());
    CHECK(two.replaced == 1);
    CHECK(two.unchanged == 1);
    CHECK(two.left_deleted == 1);
    CHECK(two.added == 1);
    CHECK(two.games.at("Chap0002").game == 2);  // its number kept
    CHECK(two.games.at("Chap0004").game == 4);
    const auto snapshot = db->snapshot();
    CHECK(snapshot->games() == 5);
    CHECK(snapshot->deleted(1));
    CHECK(snapshot->game(2).plies == 6);
    CHECK(snapshot->game(3).plies == 4);
    CHECK(snapshot->game(0).plies == 1);  // the user's own game untouched
    // Read a third time, nothing changed: nothing written.
    const auto three = importStudy(*db, second, two.games);
    CHECK(three.added == 0);
    CHECK(three.replaced == 0);
    CHECK(three.unchanged == 3);
}

TEST_CASE("Lichess studies: what the profile keeps") {
    const auto profile = fs::temp_directory_path() / "lichess_studies_profile";
    fs::remove_all(profile);
    fs::create_directories(profile);
    CHECK(loadStudies(profile).studies.empty());  // none yet

    StudiesState state;
    state.opened        = true;
    state.made          = {1794996720};
    auto& r             = state.record("AbCdEfGh", "Openings");
    r.visibility        = "private";
    r.chapters          = 12;
    r.followed          = true;
    r.read_at           = 1763460000;
    r.modified          = MODIFIED;
    r.read              = {{"Chap0001", "Najdorf", 0xfedcba9876543210ULL, true}};
    r.database          = profile / "databases" / "studies";
    r.games["Chap0001"] = {7, 0x0123456789abcdefULL};
    SentRecord s;
    s.study        = "AbCdEfGh";
    s.chapter      = "Chap0042";
    s.chapter_name = "A – B";
    s.pgn          = "[Event \"Club\"]\n\n1. e4 *\n";
    s.tags         = {{"Event", "Club"}, {"Result", "*"}};
    s.assistant    = true;
    s.folder       = profile / "databases" / "mine";
    s.game         = 3;
    state.sent.push_back(s);
    saveStudies(profile, state);

    const auto back = loadStudies(profile);
    CHECK(back.opened);
    CHECK_FALSE(back.read_declined);
    CHECK(back.made == state.made);
    REQUIRE(back.studies.size() == 1);
    const auto& b = back.studies[0];
    CHECK(b.name == "Openings");
    CHECK(b.visibility == "private");
    CHECK(b.chapters == 12);
    CHECK(b.followed);
    CHECK(b.modified == MODIFIED);
    REQUIRE(b.read.size() == 1);
    CHECK(b.read[0].hash == 0xfedcba9876543210ULL);
    CHECK(b.read[0].changed);
    CHECK(b.database == r.database);
    CHECK(b.games.at("Chap0001") == ImportedChapter{7, 0x0123456789abcdefULL});
    REQUIRE(back.sent.size() == 1);
    CHECK(back.sent[0].pgn == s.pgn);
    CHECK(back.sent[0].tags == s.tags);
    CHECK(back.sent[0].assistant);
    CHECK(back.sent[0].folder == s.folder);
    CHECK(back.sent[0].game == 3);
    CHECK(back.sent[0].url().ends_with("/study/AbCdEfGh/Chap0042"));
    CHECK(studyCache(profile, "AbCdEfGh") == profile / "lichess-cache" / "studies" / "AbCdEfGh.pgn");

    // A reading against the one before: new and different chapters marked.
    std::vector<ChapterState> now = {
        {"Chap0001", "Najdorf", 1, false}, {"Chap0002", "Dragon", 2, false}, {"Chap0003", "Kan", 9, false}};
    markChanged(now, {{"Chap0001", "Najdorf", 1, true}, {"Chap0002", "Dragon", 3, false}});
    CHECK_FALSE(now[0].changed);
    CHECK(now[1].changed);
    CHECK(now[2].changed);
    // Lichess's 30 a day: those of the last 24 hours counted, the older forgotten.
    std::vector<int64_t> made = {1000, 90000, 100000};
    CHECK(madeToday(made, 100000 + 3600) == 2);
    CHECK(made.size() == 2);
}

TEST_CASE("Lichess studies: each failure of a study's requests told by its kind") {
    AnsweringServer server;
    REQUIRE(server.ok());
    HostGate::instance().setPause(std::chrono::milliseconds(50));
    ChapterSent chapter;
    chapter.pgn = "[Event \"Club\"]\n\n1. e4 *\n";
    struct Case {
        int status;
        const char* body;
        NetError net;
    };
    for (const auto& [status, body, net] :
         {Case{401, R"({"error":"No such token"})", NetError::Unauthorized},
          Case{403, R"({"error":"Missing scope"})", NetError::Forbidden}, Case{404, "{}", NetError::NotFound},
          Case{429, "{}", NetError::RateLimited}, Case{500, "<html>Internal error</html>", NetError::Server}}) {
        CAPTURE(status);
        server.answer(status, body);
        CHECK(userStudies("rs_sample_0000", "reader").net == net);
        CHECK(studyText("AbCdEfGh", "reader").net == net);
        CHECK(studyModified("AbCdEfGh", "reader").net == net);
        CHECK(createStudy("Mes parties", Visibility::Private, "writer").net == net);
        CHECK(importChapter("AbCdEfGh", chapter, "writer").net == net);
        CHECK(updateChapterMoves("AbCdEfGh", "Chap0042", chapter.pgn, "writer").net == net);
        CHECK(deleteChapter("AbCdEfGh", "Chap0042", "writer").net == net);
    }
    // What a 404 means for each: no such study, no such chapter any more.
    server.answer(404, "{}");
    CHECK(describe(studyText("AbCdEfGh", "")) == "Lichess has no such study, or it is private");
    CHECK(describe(updateChapterTags("AbCdEfGh", "Gone0000", "", "writer")) == "Lichess has no such chapter any more");
    // An answer that cannot be read; a study made without its id.
    server.answer(200, "<html>not JSON</html>");
    CHECK(createStudy("Mes parties", Visibility::Private, "writer").net == NetError::BadAnswer);
    CHECK(importChapter("AbCdEfGh", chapter, "writer").net == NetError::BadAnswer);
    server.answer(200, R"({"name":"Mes parties"})");
    CHECK(createStudy("Mes parties", Visibility::Private, "writer").net == NetError::BadAnswer);
    // Nothing listens; cancelled in a pause.
    pointNowhere();
    CHECK(studyText("AbCdEfGh", "").net == NetError::Offline);
    CHECK(createStudy("Mes parties", Visibility::Private, "writer").net == NetError::Offline);
    pointAtPausedHost("lichess-studies.invalid");
    std::atomic<bool> cancel{true};
    CHECK(userStudies("rs_sample_0000", "", &cancel).net == NetError::Cancelled);
    CHECK(studyModified("AbCdEfGh", "", &cancel).net == NetError::Cancelled);
}
