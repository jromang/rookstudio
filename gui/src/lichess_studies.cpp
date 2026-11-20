#include "lichess_studies.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <set>

#include <fmt/include/fmt/format.h>
#include <json.hpp>
#include <study/import.hpp>
#include <study/pgn_writer.hpp>
#include <study/store.hpp>
#include <tour/files.hpp>
#include <tour/pgn_file.hpp>

#include "assistant_notes.hpp"
#include "https.hpp"
#include "lichess_links.hpp"
#include "portable.hpp"

namespace fcg::lichess {

namespace fs = std::filesystem;

namespace {

constexpr const char* STATE_FILE = "lichess-studies.json";

// Lichess's reason, when its answer gives one: {"error": "..."}.
std::string reasonOf(const HttpResult& r) {
    const auto j = nlohmann::json::parse(r.body, nullptr, false);
    if (!j.is_discarded() && j.is_object()) {
        if (j.contains("error") && j["error"].is_string()) return j["error"].get<std::string>();
        if (j.contains("global") && j["global"].is_array() && !j["global"].empty() && j["global"][0].is_string())
            return j["global"][0].get<std::string>();
    }
    return {};
}

// Why a request gave nothing, in words.
std::string refusal(const HttpResult& r, const char* missing = "Lichess has no such study, or it is private") {
    if (const auto reason = reasonOf(r); !reason.empty() && r.status >= 400 && r.status != 429) return reason;
    switch (r.status) {
        case 0:
            return r.error.empty() ? std::string("Lichess did not answer") : r.error;
        case 401:
            return "Lichess refused the token: link the account again";
        case 403:
            return "the token may not do it: one more permission is needed";
        case 404:
            return missing;
        case 429:
            return "Lichess asks to wait a minute: too many requests";
        default:
            return fmt::format("Lichess answered {}", r.status);
    }
}

HttpRequest request(const std::string& method, std::string url, const std::string& token, const char* accept) {
    HttpRequest r;
    r.method = method;
    r.url    = std::move(url);
    r.token  = token;
    r.headers.emplace_back("Accept", accept);
    return r;
}

HttpResult post(const std::string& path, const std::string& token, const Form& fields,
                const std::atomic<bool>* cancel) {
    auto r         = request("POST", servers().api + path, token, "application/json");
    r.content_type = "application/x-www-form-urlencoded";
    r.body         = formEncode(fields);
    return politeRequest(r, cancel);
}

std::string tagOf(std::string_view pgn, std::string_view name) {
    const auto key = fmt::format("[{} \"", name);
    std::size_t at = 0;
    while ((at = pgn.find(key, at)) != std::string_view::npos) {
        if (at == 0 || pgn[at - 1] == '\n' || pgn[at - 1] == '\r' || pgn[at - 1] == ' ') break;
        at += key.size();
    }
    if (at == std::string_view::npos) return {};
    std::string out;
    for (std::size_t i = at + key.size(); i < pgn.size() && pgn[i] != '\n'; ++i) {
        if (pgn[i] == '\\' && i + 1 < pgn.size()) {
            out += pgn[++i];
        } else if (pgn[i] == '"') {
            return out;
        } else {
            out += pgn[i];
        }
    }
    return out;
}

// UTF-8 characters, not bytes.
std::size_t characters(std::string_view text) {
    return static_cast<std::size_t>(
        std::count_if(text.begin(), text.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

// At most `most` characters of `text`, cut on a character's boundary.
std::string cut(std::string text, std::size_t most) {
    std::size_t seen = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue;
        if (seen++ == most) return text.substr(0, i);
    }
    return text;
}

int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {  // Howard Hinnant's
    y -= m <= 2;
    const int64_t era  = (y >= 0 ? y : y - 399) / 400;
    const auto yoe     = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

template <typename T>
bool number(std::string_view text, T& out) {
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc() && end == text.data() + text.size();
}

nlohmann::json tagsJson(const Tags& tags) {
    auto out = nlohmann::json::array();
    for (const auto& [name, value] : tags) out.push_back({name, value});
    return out;
}

Tags tagsFrom(const nlohmann::json& j) {
    Tags out;
    if (!j.is_array()) return out;
    for (const auto& t : j)
        if (t.is_array() && t.size() == 2 && t[0].is_string() && t[1].is_string())
            out.emplace_back(t[0].get<std::string>(), t[1].get<std::string>());
    return out;
}

std::string hex(uint64_t v) { return fmt::format("{:016x}", v); }

uint64_t unhex(const std::string& s) {
    uint64_t v = 0;
    std::from_chars(s.data(), s.data() + s.size(), v, 16);
    return v;
}

}  // namespace

// ---------------------------------------------------------------------------
// Reading

std::vector<StudyInfo> parseStudyList(std::string_view ndjson) {
    std::vector<StudyInfo> out;
    std::size_t at = 0;
    while (at < ndjson.size()) {
        auto end = ndjson.find('\n', at);
        if (end == std::string_view::npos) end = ndjson.size();
        const auto line = ndjson.substr(at, end - at);
        at              = end + 1;
        const auto j    = nlohmann::json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object() || !j.contains("id") || !j["id"].is_string()) continue;
        StudyInfo s;
        s.id      = j["id"].get<std::string>();
        s.name    = j.contains("name") && j["name"].is_string() ? j["name"].get<std::string>() : s.id;
        s.created = j.contains("createdAt") && j["createdAt"].is_number() ? j["createdAt"].get<int64_t>() : 0;
        s.updated = j.contains("updatedAt") && j["updatedAt"].is_number() ? j["updatedAt"].get<int64_t>() : 0;
        if (!s.id.empty()) out.push_back(std::move(s));
    }
    return out;
}

Answer<std::vector<StudyInfo>> userStudies(const std::string& username, const std::string& token,
                                           const std::atomic<bool>* cancel) {
    Answer<std::vector<StudyInfo>> a;
    auto r       = request("GET", fmt::format("{}/api/study/by/{}", servers().api, urlEncode(username)), token,
                           "application/x-ndjson");
    r.timeout_s  = 60;  // Lichess streams them, a few dozen a second
    const auto h = politeRequest(r, cancel);
    a.status     = h.status;
    if (h.status != 200) {
        a.error = refusal(h, "Lichess has no account of that name");
        return a;
    }
    a.value = parseStudyList(h.body);
    return a;
}

std::vector<StudyChapter> parseStudyPgn(std::string_view pgn) {
    std::vector<StudyChapter> out;
    for (const auto piece : study::splitGames(pgn)) {
        StudyChapter c;
        c.pgn = std::string(piece);
        while (!c.pgn.empty() && (c.pgn.back() == '\n' || c.pgn.back() == '\r' || c.pgn.back() == ' '))
            c.pgn.pop_back();
        if (c.pgn.empty()) continue;
        c.pgn += '\n';
        if (const auto link = parseLink(tagOf(c.pgn, "ChapterURL")); link && link->kind == LinkKind::Chapter)
            c.id = link->chapter;
        c.name = tagOf(c.pgn, "ChapterName");
        if (c.name.empty()) {
            const auto white = tagOf(c.pgn, "White"), black = tagOf(c.pgn, "Black");
            c.name = !white.empty() || !black.empty()
                         ? fmt::format("{} – {}", white.empty() ? "?" : white, black.empty() ? "?" : black)
                         : tagOf(c.pgn, "Event");
        }
        c.black = tagOf(c.pgn, "Orientation") == "black";
        c.hash  = textHash(c.pgn);
        study::ImportedGame g;
        if (study::readImportedGame(c.pgn, g)) c.plies = static_cast<int>(g.plies);
        out.push_back(std::move(c));
    }
    return out;
}

Answer<StudyText> studyText(const std::string& id, const std::string& token, bool whole,
                            const std::atomic<bool>* cancel) {
    Answer<StudyText> a;
    const auto query = whole ? "clocks=true&comments=true&variations=true&orientation=true"
                             : "clocks=false&comments=false&variations=false";
    auto r           = request("GET", fmt::format("{}/api/study/{}.pgn?{}", servers().api, urlEncode(id), query), token,
                               "application/x-chess-pgn");
    r.timeout_s      = 60;
    const auto h     = politeRequest(r, cancel);
    a.status         = h.status;
    if (h.status != 200) {
        a.error = refusal(h);
        return a;
    }
    StudyText t;
    t.pgn      = h.body;
    t.modified = httpHeader(h.head, "Last-Modified").value_or("");
    a.value    = std::move(t);
    return a;
}

Answer<std::string> studyModified(const std::string& id, const std::string& token, const std::atomic<bool>* cancel) {
    Answer<std::string> a;
    const auto h = politeRequest(
        request("HEAD", fmt::format("{}/api/study/{}.pgn", servers().api, urlEncode(id)), token, "*/*"), cancel);
    a.status = h.status;
    if (h.status != 200 && h.status != 204) {
        a.error = refusal(h);
        return a;
    }
    const auto modified = httpHeader(h.head, "Last-Modified");
    if (!modified || modified->empty()) {
        a.error = "Lichess did not say when it changed";
        return a;
    }
    a.value = *modified;
    return a;
}

std::optional<int64_t> httpTime(std::string_view date) {
    // "Wed, 18 Nov 2026 10:12:00 GMT"
    static constexpr std::array<std::string_view, 12> MONTHS = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (const auto comma = date.find(", "); comma != std::string_view::npos) date.remove_prefix(comma + 2);
    std::vector<std::string_view> parts;
    for (std::size_t at = 0; at < date.size();) {
        const auto space = date.find(' ', at);
        const auto part  = date.substr(at, space == std::string_view::npos ? std::string_view::npos : space - at);
        if (!part.empty()) parts.push_back(part);
        if (space == std::string_view::npos) break;
        at = space + 1;
    }
    if (parts.size() < 4 || parts[3].size() != 8 || parts[3][2] != ':' || parts[3][5] != ':') return std::nullopt;
    unsigned day = 0, hour = 0, minute = 0, second = 0;
    int64_t year     = 0;
    const auto month = std::find(MONTHS.begin(), MONTHS.end(), parts[1]);
    if (month == MONTHS.end() || !number(parts[0], day) || !number(parts[2], year) ||
        !number(parts[3].substr(0, 2), hour) || !number(parts[3].substr(3, 2), minute) ||
        !number(parts[3].substr(6, 2), second) || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60)
        return std::nullopt;
    const auto m = static_cast<unsigned>(month - MONTHS.begin() + 1);
    return daysFromCivil(year, m, day) * 86400 + hour * 3600 + minute * 60 + second;
}

bool changedSince(const std::string& modified_now, const std::string& modified_read, int64_t read_at) {
    if (modified_now.empty()) return false;
    const auto now = httpTime(modified_now);
    if (!modified_read.empty()) {
        const auto then = httpTime(modified_read);
        return now && then ? *now > *then : modified_now != modified_read;
    }
    return now && read_at > 0 && *now > read_at;
}

// ---------------------------------------------------------------------------
// Writing

const char* visibilityKey(Visibility visibility) {
    switch (visibility) {
        case Visibility::Unlisted:
            return "unlisted";
        case Visibility::Public:
            return "public";
        case Visibility::Private:
            break;
    }
    return "private";
}

std::optional<Visibility> visibilityOf(std::string_view key) {
    if (key == "private") return Visibility::Private;
    if (key == "unlisted") return Visibility::Unlisted;
    if (key == "public") return Visibility::Public;
    return std::nullopt;
}

bool studyNameOk(std::string_view name) {
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.remove_prefix(1);
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.remove_suffix(1);
    const auto n = characters(name);
    return n >= 2 && n <= 100;
}

std::string studyUrl(const std::string& study) { return fmt::format("{}/study/{}", servers().api, study); }

std::string chapterUrl(const std::string& study, const std::string& chapter) {
    return fmt::format("{}/study/{}/{}", servers().api, study, chapter);
}

Form newStudyForm(const std::string& name, Visibility visibility) {
    std::string trimmed = name;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(0, 1);
    return {{"name", trimmed},         {"visibility", visibilityKey(visibility)},
            {"computer", "everyone"},  {"explorer", "everyone"},
            {"cloneable", "everyone"}, {"shareable", "everyone"},
            {"chat", "member"}};
}

Answer<std::string> createStudy(const std::string& name, Visibility visibility, const std::string& token,
                                const std::atomic<bool>* cancel) {
    Answer<std::string> a;
    if (!studyNameOk(name)) {
        a.error = "a study's name has 2 to 100 characters";
        return a;
    }
    const auto h = post("/api/study", token, newStudyForm(name, visibility), cancel);
    a.status     = h.status;
    if (h.status != 200) {
        a.error = refusal(h);
        return a;
    }
    const auto j = nlohmann::json::parse(h.body, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("id") || !j["id"].is_string()) {
        a.error = "Lichess's answer could not be read";
        return a;
    }
    a.value = j["id"].get<std::string>();
    return a;
}

Form chapterForm(const ChapterSent& chapter) {
    Form f{{"pgn", chapter.pgn}};
    if (!chapter.name.empty()) f.emplace_back("name", cut(chapter.name, 100));
    f.emplace_back("orientation", chapter.black ? "black" : "white");
    if (chapter.chess960) f.emplace_back("variant", "chess960");
    return f;
}

Answer<std::vector<ChapterMade>> importChapter(const std::string& study, const ChapterSent& chapter,
                                               const std::string& token, const std::atomic<bool>* cancel) {
    Answer<std::vector<ChapterMade>> a;
    const auto h = post(fmt::format("/api/study/{}/import-pgn", urlEncode(study)), token, chapterForm(chapter), cancel);
    a.status     = h.status;
    if (h.status != 200) {
        a.error = refusal(h);
        return a;
    }
    const auto j = nlohmann::json::parse(h.body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        a.error = "Lichess's answer could not be read";
        return a;
    }
    std::vector<ChapterMade> made;
    if (j.contains("chapters") && j["chapters"].is_array()) {
        for (const auto& c : j["chapters"]) {
            if (!c.is_object() || !c.contains("id") || !c["id"].is_string()) continue;
            made.push_back({c["id"].get<std::string>(), c.value("name", std::string())});
        }
    }
    if (made.empty()) {
        a.error = j.contains("error") && j["error"].is_string() ? j["error"].get<std::string>()
                                                                : std::string("Lichess made no chapter of it");
        return a;
    }
    a.value = std::move(made);
    return a;
}

namespace {

Answer<bool> done(const HttpResult& h, const char* missing) {
    Answer<bool> a;
    a.status = h.status;
    if (h.status < 200 || h.status >= 300) {
        a.error = refusal(h, missing);
        return a;
    }
    a.value = true;
    return a;
}

}  // namespace

Answer<bool> updateChapterMoves(const std::string& study, const std::string& chapter, const std::string& pgn,
                                const std::string& token, const std::atomic<bool>* cancel) {
    return done(post(fmt::format("/api/study/{}/{}/moves", urlEncode(study), urlEncode(chapter)), token, {{"pgn", pgn}},
                     cancel),
                "Lichess has no such chapter any more");
}

Answer<bool> updateChapterTags(const std::string& study, const std::string& chapter, const std::string& tags,
                               const std::string& token, const std::atomic<bool>* cancel) {
    return done(post(fmt::format("/api/study/{}/{}/tags", urlEncode(study), urlEncode(chapter)), token, {{"pgn", tags}},
                     cancel),
                "Lichess has no such chapter any more");
}

Answer<bool> deleteChapter(const std::string& study, const std::string& chapter, const std::string& token,
                           const std::atomic<bool>* cancel) {
    return done(
        politeRequest(
            request("DELETE", fmt::format("{}/api/study/{}/{}", servers().api, urlEncode(study), urlEncode(chapter)),
                    token, "application/json"),
            cancel),
        "Lichess has no such chapter any more");
}

// ---------------------------------------------------------------------------
// The game sent

GameTree studyGame(const GameTree& tree, bool assistant) {
    GameTree out     = tree;
    const auto clean = [&](std::vector<ReplayMove>& moves) {
        for (auto& m : moves) {
            m.score.reset();
            m.depth   = 0;
            m.time_ms = -1;
            m.nps     = 0;
            m.wdl.reset();
            m.nodes    = 0;
            m.seldepth = 0;
            m.tbhits   = 0;
            m.pv.clear();
            m.decision_ms.reset();
            m.fields = 0;
            m.book   = false;
            m.engine_comment.clear();  // the tournament's comment as it was read (#87)
            auto& n  = m.notes;
            if (!assistant) n.assistant.clear();  // with them, a comment after the move's, without the mark
            n.commands.clear();                   // other programs' commands
            n.eval_tag = false;
        }
    };
    clean(out.main.moves);
    for (auto& v : out.variations) clean(v.moves);
    out.main.fields = 0;
    out.main.final_comment.clear();
    return out;
}

std::string studyGamePgn(const GameTree& tree, bool chess960, bool assistant) {
    study::PgnWriteOptions options{chess960, nullptr};
    options.assistant_mark = false;  // Lichess reads plain comments
    return study::writePgnGame(studyGame(tree, assistant), options);
}

int assistantComments(const GameTree& tree) {
    int n            = 0;
    const auto count = [&](const std::vector<ReplayMove>& moves) {
        for (const auto& m : moves)
            if (byAssistant(m.notes)) ++n;
    };
    count(tree.main.moves);
    for (const auto& v : tree.variations) count(v.moves);
    return n;
}

Tags gameTags(const GameTree& tree) {
    static constexpr std::array<std::string_view, 5> LEFT = {"PlyCount", "Result", "SetUp", "FEN", "Variant"};
    Tags out;
    for (const auto& [name, value] : tree.main.headers)
        if (std::find(LEFT.begin(), LEFT.end(), name) == LEFT.end()) out.emplace_back(name, value);
    auto result = tree.main.result == "\xC2\xBD-\xC2\xBD" ? std::string("1/2-1/2") : tree.main.result;
    if (result != "1-0" && result != "0-1" && result != "1/2-1/2") result = "*";
    out.emplace_back("Result", result);
    return out;
}

std::string tagsPgn(const Tags& now, const Tags& before) {
    std::string out;
    std::set<std::string> named;
    for (const auto& [name, value] : now) {
        out += fmt::format("[{} \"{}\"]\n", name, tour::pgnTagValue(value));
        named.insert(name);
    }
    for (const auto& [name, value] : before)
        if (!named.count(name)) out += fmt::format("[{} \"\"]\n", name);
    return out;
}

Answer<SendResult> sendToStudy(const SendRequest& r, const std::string& token, const std::atomic<bool>* cancel) {
    Answer<SendResult> a;
    SendResult out;
    int chapters = r.chapters;
    if (r.study.empty()) {
        const auto made = createStudy(r.new_name, r.visibility, token, cancel);
        a.status        = made.status;
        if (!made.value) {
            a.error = made.error;
            return a;
        }
        out.study   = *made.value;
        out.created = true;
        chapters    = 1;  // the empty one Lichess makes with it
    } else {
        out.study = r.study;
        if (chapters < 0) {  // counted first: a full study takes no more
            if (const auto text = studyText(r.study, token, false, cancel); text.value)
                chapters = static_cast<int>(parseStudyPgn(text.value->pgn).size());
        }
        if (chapters >= 0 && studyFull(chapters)) {
            a.error      = fmt::format("the study is full: Lichess keeps {} chapters at most", STUDY_CHAPTERS);
            out.chapters = chapters;  // counted: no longer offered
            a.value      = out;
            return a;
        }
    }
    const auto chapter = importChapter(out.study, r.chapter, token, cancel);
    a.status           = chapter.status;
    if (!chapter.value) {
        a.error = chapter.error;
        if (out.created) a.value = out;  // the study is there, empty
        return a;
    }
    out.chapter      = chapter.value->front().id;
    out.chapter_name = chapter.value->front().name;
    if (out.created) {
        // The empty first chapter Lichess made with the study goes; when it cannot be read (no permission to read
        // studies), it stays.
        if (const auto text = studyText(out.study, token, false, cancel); text.value) {
            const auto all = parseStudyPgn(text.value->pgn);
            chapters       = static_cast<int>(all.size());
            for (const auto& c : all) {
                if (c.id.empty() || c.id == out.chapter || c.plies > 0) continue;
                if (deleteChapter(out.study, c.id, token, cancel).value) --chapters;
            }
        } else {
            chapters = 2;
        }
    } else if (chapters >= 0) {
        ++chapters;
    }
    out.chapters = chapters;
    a.value      = std::move(out);
    return a;
}

// ---------------------------------------------------------------------------
// Into a database

StudyImport importStudy(study::Database& db, const std::vector<StudyChapter>& chapters,
                        const std::map<std::string, ImportedChapter>& before) {
    StudyImport report;
    report.games = before;
    std::vector<study::ImportedGame> added;
    std::vector<std::pair<std::string, uint64_t>> added_keys;  // chapter id, hash
    try {
        const auto snapshot = db.snapshot();
        for (std::size_t i = 0; i < chapters.size(); ++i) {
            const auto& c  = chapters[i];
            const auto key = c.id.empty() ? fmt::format("#{}", i) : c.id;
            study::ImportedGame g;
            if (!study::readImportedGame(c.pgn, g)) {
                ++report.unreadable;
                continue;
            }
            const auto known = before.find(key);
            if (known != before.end() && known->second.game < snapshot->games()) {
                if (snapshot->deleted(known->second.game)) {
                    ++report.left_deleted;
                } else if (known->second.hash == c.hash) {
                    ++report.unchanged;
                } else {
                    db.replace(known->second.game, std::move(g));
                    report.games[key].hash = c.hash;
                    ++report.replaced;
                }
                continue;
            }
            added.push_back(std::move(g));
            added_keys.emplace_back(key, c.hash);
        }
        if (!added.empty()) {
            const auto first = db.snapshot()->games();
            (void)db.append(std::move(added), true);
            // Their numbers: from their ChapterURL, read back (another import may have added games meanwhile), else in
            // their order.
            const auto after = db.snapshot();
            std::map<std::string, uint64_t> found;
            for (uint64_t id = first; id < after->games(); ++id) {
                const auto g = after->game(id);
                for (const auto& [tag, value] : g.tags) {
                    if (tag != "ChapterURL") continue;
                    if (const auto link = parseLink(value); link && link->kind == LinkKind::Chapter)
                        found.emplace(link->chapter, id);
                }
            }
            for (std::size_t i = 0; i < added_keys.size(); ++i) {
                const auto& [key, hash] = added_keys[i];
                const auto at           = found.find(key);
                const uint64_t id       = at != found.end() ? at->second : first + i;
                report.games[key]       = {id, hash};
                ++report.added;
            }
        }
    } catch (const std::exception& e) {
        report.error = e.what();
    }
    return report;
}

// ---------------------------------------------------------------------------
// The profile

StudyRecord* StudiesState::find(std::string_view id) {
    for (auto& s : studies)
        if (s.id == id) return &s;
    return nullptr;
}

const StudyRecord* StudiesState::find(std::string_view id) const {
    for (const auto& s : studies)
        if (s.id == id) return &s;
    return nullptr;
}

StudyRecord& StudiesState::record(const std::string& id, const std::string& name) {
    if (auto* s = find(id)) {
        if (!name.empty()) s->name = name;
        return *s;
    }
    StudyRecord s;
    s.id   = id;
    s.name = name;
    studies.push_back(std::move(s));
    return studies.back();
}

StudiesState loadStudies(const fs::path& profile) {
    StudiesState state;
    std::ifstream in(profile / STATE_FILE, std::ios::binary);
    if (!in) return state;
    const auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return state;
    state.opened        = j.value("opened", false);
    state.read_declined = j.value("read_declined", false);
    state.shown         = j.value("shown", false);
    state.last_study    = j.value("last_study", std::string());
    if (const auto m = j.find("made"); m != j.end() && m->is_array())
        for (const auto& t : *m)
            if (t.is_number_integer()) state.made.push_back(t.get<int64_t>());
    if (const auto all = j.find("studies"); all != j.end() && all->is_array()) {
        for (const auto& s : *all) {
            if (!s.is_object() || !s.contains("id") || !s["id"].is_string()) continue;
            StudyRecord r;
            r.id         = s["id"].get<std::string>();
            r.name       = s.value("name", r.id);
            r.visibility = s.value("visibility", std::string());
            r.chapters   = s.value("chapters", -1);
            r.followed   = s.value("followed", false);
            r.read_at    = s.value("read_at", int64_t{0});
            r.modified   = s.value("modified", std::string());
            r.checked    = s.value("checked", std::string());
            if (const auto read = s.find("read"); read != s.end() && read->is_array()) {
                for (const auto& c : *read) {
                    if (!c.is_object()) continue;
                    r.read.push_back({c.value("id", std::string()), c.value("name", std::string()),
                                      unhex(c.value("hash", std::string())), c.value("changed", false)});
                }
            }
            if (const auto db = s.value("database", std::string()); !db.empty())
                r.database = fs::path(portable::resolved(db));
            if (const auto games = s.find("games"); games != s.end() && games->is_object()) {
                for (const auto& [key, g] : games->items()) {
                    if (!g.is_object()) continue;
                    r.games[key] = {g.value("game", uint64_t{0}), unhex(g.value("hash", std::string()))};
                }
            }
            state.studies.push_back(std::move(r));
        }
    }
    if (const auto all = j.find("sent"); all != j.end() && all->is_array()) {
        for (const auto& s : *all)
            if (auto r = sentOf(s)) state.sent.push_back(std::move(*r));
    }
    return state;
}

nlohmann::json sentJson(const SentRecord& r) {
    nlohmann::json s{{"study", r.study},           {"chapter", r.chapter},
                     {"study_name", r.study_name}, {"chapter_name", r.chapter_name},
                     {"visibility", r.visibility}, {"pgn", r.pgn},
                     {"tags", tagsJson(r.tags)},   {"assistant", r.assistant},
                     {"black", r.black},           {"chess960", r.chess960},
                     {"opened", r.opened}};
    if (!r.folder.empty()) {
        s["folder"] = portable::stored(r.folder.string());
        s["game"]   = r.game;
    }
    return s;
}

std::optional<SentRecord> sentOf(const nlohmann::json& s) {
    if (!s.is_object()) return std::nullopt;
    SentRecord r;
    r.study        = s.value("study", std::string());
    r.chapter      = s.value("chapter", std::string());
    r.study_name   = s.value("study_name", std::string());
    r.chapter_name = s.value("chapter_name", std::string());
    r.visibility   = s.value("visibility", std::string());
    r.pgn          = s.value("pgn", std::string());
    r.tags         = tagsFrom(s.value("tags", nlohmann::json::array()));
    r.assistant    = s.value("assistant", false);
    r.black        = s.value("black", false);
    r.chess960     = s.value("chess960", false);
    r.opened       = s.value("opened", false);
    if (const auto folder = s.value("folder", std::string()); !folder.empty())
        r.folder = fs::path(portable::resolved(folder));
    r.game = s.value("game", uint64_t{0});
    if (r.study.empty() || r.chapter.empty()) return std::nullopt;
    return r;
}

void saveStudies(const fs::path& profile, const StudiesState& state) {
    nlohmann::json j;
    j["opened"]        = state.opened;
    j["read_declined"] = state.read_declined;
    j["shown"]         = state.shown;
    j["last_study"]    = state.last_study;
    j["made"]          = state.made;
    auto studies       = nlohmann::json::array();
    for (const auto& r : state.studies) {
        nlohmann::json s{{"id", r.id},
                         {"name", r.name},
                         {"visibility", r.visibility},
                         {"chapters", r.chapters},
                         {"followed", r.followed},
                         {"read_at", r.read_at},
                         {"modified", r.modified},
                         {"checked", r.checked}};
        auto read = nlohmann::json::array();
        for (const auto& c : r.read)
            read.push_back({{"id", c.id}, {"name", c.name}, {"hash", hex(c.hash)}, {"changed", c.changed}});
        s["read"] = std::move(read);
        if (!r.database.empty()) {
            s["database"] = portable::stored(r.database.string());
            auto games    = nlohmann::json::object();
            for (const auto& [key, g] : r.games) games[key] = {{"game", g.game}, {"hash", hex(g.hash)}};
            s["games"] = std::move(games);
        }
        studies.push_back(std::move(s));
    }
    j["studies"] = std::move(studies);
    auto sent    = nlohmann::json::array();
    for (const auto& r : state.sent) sent.push_back(sentJson(r));
    j["sent"] = std::move(sent);
    tour::replaceFile(profile / STATE_FILE, j.dump(1));
}

fs::path studyCache(const fs::path& profile, const std::string& id) {
    return profile / "lichess-cache" / "studies" / (tour::fileNameOf(id) + ".pgn");
}

void markChanged(std::vector<ChapterState>& now, const std::vector<ChapterState>& before) {
    for (auto& c : now) {
        const auto was = std::find_if(before.begin(), before.end(), [&](const ChapterState& b) {
            return !c.id.empty() ? b.id == c.id : b.name == c.name;
        });
        c.changed      = was == before.end() || was->hash != c.hash;
    }
}

int madeToday(std::vector<int64_t>& made, int64_t now) {
    std::erase_if(made, [&](int64_t t) { return now - t >= 24 * 3600 || t > now + 3600; });
    return static_cast<int>(made.size());
}

std::vector<StudyInfo> roomyStudies(const std::vector<StudyInfo>& studies, const StudiesState& state) {
    std::vector<StudyInfo> out;
    for (const auto& s : studies) {
        const auto* r = state.find(s.id);
        if (r && studyFull(r->chapters)) continue;
        out.push_back(s);
    }
    return out;
}

uint64_t textHash(std::string_view text) {
    uint64_t h         = 1469598103934665603ULL;
    std::size_t spaces = 0;  // held back until something follows them on their line
    for (const char c : text) {
        if (c == '\r') continue;
        if (c == ' ' || c == '\t') {
            ++spaces;
            continue;
        }
        if (c != '\n')
            for (; spaces > 0; --spaces) h = (h ^ static_cast<unsigned char>(' ')) * 1099511628211ULL;
        spaces = 0;
        h      = (h ^ static_cast<unsigned char>(c)) * 1099511628211ULL;
    }
    return h;
}

}  // namespace fcg::lichess
