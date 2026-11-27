#pragma once

// One way to ask Lichess (#106), without ImGui. A request is built by request(), sent by send() as Lichess asks its
// clients (politeRequest: one request at a time to a host, and the host's minute of pause after a 429) and read in one
// place: its status, Lichess's own words when it refuses, and its JSON (call()). Why it gave nothing is a kind,
// NetError, carried by every answer (Failure, Answer<T>): the interface puts it in words with describe(), translated,
// and never reads an error's text to decide what to do.

#include <atomic>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <json.hpp>

#include "https.hpp"

namespace fcg::lichess {

// Why a request gave nothing.
enum class NetError {
    None,          // it went through: an answer without a value then means Lichess has nothing for it
    Cancelled,     // the program stopped it (Cancel, a page left, the program closing): nothing to say
    Offline,       // no answer: no network, Lichess down or too slow, a proxy in the way
    TimedOut,      // a wait of the program's own ran out: the browser's answer to a link (link()'s five minutes)
    Unauthorized,  // 401: the token refused (revoked, expired), or one is needed
    Forbidden,     // 403: the token may not do it; in a link's answer, without a status: the user's no
    NotFound,      // 404: Lichess has no such thing, or it is private
    RateLimited,   // 429: too many requests; the host pauses a minute (HostGate)
    Server,        // another status: Lichess in trouble (5xx), or refusing the request (4xx), in its own words if any
    BadAnswer,     // an answer that cannot be read: not what Lichess sends
};
// Its name, for the log and the tests ("RateLimited").
[[nodiscard]] const char* netName(NetError net) noexcept;

// What was asked, for the words of a failure (describe()): what Lichess may not have, who may read it.
enum class Asked {
    Anything,        // the words that fit any request
    Link,            // an account's link: its code exchanged, its name asked
    Game,            // a game
    Study,           // a study, or a chapter to read
    Chapter,         // a chapter of the user's study, to change or delete
    Player,          // a player's account (their studies)
    FidePlayer,      // a FIDE player's card
    Page,            // a page of lichess.org a link names (a study, a game…)
    Broadcast,       // a broadcast a link names, or one searched
    Bookmarks,       // the account's bookmarks
    Engines,         // the external engines' API (#96), in alpha
    Tournament,      // an arena or a Swiss tournament (#95)
    BroadcastRound,  // a broadcast's round, followed (#44)
    BroadcastTour,   // a broadcast's tournament: followed whole, its players, its teams (#55)
};

// Why a request gave nothing, as an answer carries it and the interface keeps it to say it later (describe()).
struct Failure {
    NetError net = NetError::None;
    int status   = 0;  // HTTP's, 0 without an answer
    Asked asked  = Asked::Anything;
    std::string said;    // Lichess's own words when it refused ({"error": "…"}), shown as they are
    std::string error;   // a problem of the program's own, in words (no port to listen on, a name too long, a
                         // database that refused the games); empty for Lichess's and the network's failures
    std::string detail;  // the network's or the reader's words, for the log and the tests: never shown
    [[nodiscard]] bool failed() const noexcept { return net != NetError::None || !error.empty(); }
    // A problem of the program's own, in its words.
    [[nodiscard]] static Failure own(std::string words) {
        Failure f;
        f.error = std::move(words);
        return f;
    }
};

// What a request gave: its value, or none and why. No value and no failure: the server has nothing for it (a position
// the cloud has not evaluated).
template <typename T>
struct Answer : Failure {
    std::optional<T> value;
};

// An answer without a value, for `failure` (one of another request, which this answer stops at).
template <typename T>
[[nodiscard]] Answer<T> failedWith(const Failure& failure) {
    Answer<T> a;
    static_cast<Failure&>(a) = failure;
    return a;
}

// The user-visible words of a failure, translated: a phrase without a final stop, to stand alone or within a sentence
// ("Lichess gave nothing ({})."). Lichess's own words (`said`) are given as they are for a refusal NetError does not
// name (Server). None has no words.
[[nodiscard]] std::string describe(NetError net, Asked asked = Asked::Anything, int status = 0,
                                   std::string_view said = {});
// The same for a failure as it was kept; a problem of the program's own (`error`) in its own words.
[[nodiscard]] std::string describe(const Failure& failure);
// The same for a line of its own, which names Lichess once: describe()'s words, Lichess's own quoted after "Lichess
// refuses: ".
[[nodiscard]] std::string describeAlone(const Failure& failure);
// Why nothing is shown where Lichess's answer would be, as a sentence: "Lichess cannot be reached from here." when no
// answer came, else "Lichess gave nothing (…)." with describe()'s words; a problem of the program's own in its own
// words, Lichess not having been asked.
[[nodiscard]] std::string gaveNothing(const Failure& failure);

// The kind of failure an HTTP result is: None for a whole 2xx; Cancelled when the program stopped it.
[[nodiscard]] NetError netErrorOf(const HttpResult& result);

// Lichess's own words in a refusal's body: {"error": "…"}, a form's {"error": {"field": ["…"]}} ("field: …"), or
// {"global": ["…"]}; empty when it gave none.
[[nodiscard]] std::string lichessWords(std::string_view body);

// A form's fields, in order: (name, value).
using Form = std::vector<std::pair<std::string, std::string>>;

// A request to one of Lichess's hosts (servers()): `method` to `url`, with the account's `token` when there is one
// (sent as "Authorization: Bearer"), asking for `accept` (JSON, PGN, NDJSON…).
[[nodiscard]] HttpRequest request(std::string method, std::string url, std::string token = {},
                                  std::string accept = "application/json");
// `request` with a body: a form (application/x-www-form-urlencoded), or JSON.
[[nodiscard]] HttpRequest withForm(HttpRequest request, const Form& fields);
[[nodiscard]] HttpRequest withJson(HttpRequest request, const nlohmann::json& body);

// What a request gave once sent: its failure's kind (none for a 2xx), its status and Lichess's words, and its body.
struct Response : Failure {
    std::string body;
    std::string head;       // the status line and the headers, as they came (httpHeader() reads one)
    std::string asked_for;  // the method and the address, for the log
};
// `request` sent politely: in its host's turn (one request at a time), once the host's pause after a 429 is over;
// `cancel` stops the wait for the turn, or the request. The network's words go to the log, never to the interface: no
// answer once, when the host's outage starts (not at each try until it answers again); an answer cut short, or a body
// that cannot be read later, each time.
[[nodiscard]] Response send(const HttpRequest& request, const std::atomic<bool>* cancel = nullptr);
// A result had otherwise (a stream's, an upload's) read the same way, for `request`.
[[nodiscard]] Response responseOf(const HttpRequest& request, HttpResult result);

// The log's line for an answer that could not be read (BadAnswer), with the reader's words.
void logUnreadable(const Response& response, std::string_view why);

namespace detail {
template <typename T>
struct IsOptional : std::false_type {};
template <typename T>
struct IsOptional<std::optional<T>> : std::true_type {};
}  // namespace detail

// The value of a response's JSON body, given to `parse`: it returns a T, or an std::optional<T> (none: an answer that
// is not what Lichess sends, BadAnswer), and may throw on what it cannot read (BadAnswer too). The response's failure
// otherwise.
template <typename T, typename Parse>
[[nodiscard]] Answer<T> readAnswer(const Response& response, Parse&& parse) {
    auto a = failedWith<T>(response);
    if (response.failed()) return a;
    try {
        auto got = parse(nlohmann::json::parse(response.body));
        if constexpr (detail::IsOptional<decltype(got)>::value) {
            if (got) {
                a.value = std::move(*got);
            } else {
                a.net    = NetError::BadAnswer;
                a.detail = "not the fields Lichess sends";
            }
        } else {
            a.value = std::move(got);
        }
    } catch (const std::exception& e) {
        a.value.reset();
        a.net    = NetError::BadAnswer;
        a.detail = e.what();
    }
    if (a.net == NetError::BadAnswer) logUnreadable(response, a.detail);
    return a;
}

// The value of a response's body that is not JSON (NDJSON, PGN, text), made of it by `read`; the response's failure
// otherwise.
template <typename T, typename Read>
[[nodiscard]] Answer<T> readBody(const Response& response, Read&& read) {
    auto a = failedWith<T>(response);
    if (!response.failed()) a.value = read(response.body);
    return a;
}

// An answer without a value for a response whose body could not be read (BadAnswer): `why` in the log.
template <typename T>
[[nodiscard]] Answer<T> unreadable(const Response& response, std::string why) {
    auto a   = failedWith<T>(response);
    a.net    = NetError::BadAnswer;
    a.detail = std::move(why);
    logUnreadable(response, a.detail);
    return a;
}

// `request` sent (send()), its answer read by `parse` (readAnswer()).
template <typename T, typename Parse>
[[nodiscard]] Answer<T> call(const HttpRequest& request, Parse&& parse, const std::atomic<bool>* cancel = nullptr) {
    return readAnswer<T>(send(request, cancel), std::forward<Parse>(parse));
}

// A JSON object's fields, read without throwing: a missing key, or one of another type, gives the default.
[[nodiscard]] std::string textOf(const nlohmann::json& j, const char* key);
[[nodiscard]] bool boolOf(const nlohmann::json& j, const char* key, bool otherwise = false);

}  // namespace fcg::lichess
