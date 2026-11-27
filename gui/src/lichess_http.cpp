#include "lichess_http.hpp"

#include <mutex>
#include <set>

#include <fmt/include/fmt/format.h>

#include "console.hpp"
#include "i18n.hpp"

namespace fcg::lichess {

const char* netName(NetError net) noexcept {
    switch (net) {
        case NetError::None:
            return "None";
        case NetError::Cancelled:
            return "Cancelled";
        case NetError::Offline:
            return "Offline";
        case NetError::TimedOut:
            return "TimedOut";
        case NetError::Unauthorized:
            return "Unauthorized";
        case NetError::Forbidden:
            return "Forbidden";
        case NetError::NotFound:
            return "NotFound";
        case NetError::RateLimited:
            return "RateLimited";
        case NetError::Server:
            return "Server";
        case NetError::BadAnswer:
            return "BadAnswer";
    }
    return "?";
}

std::string describe(NetError net, Asked asked, int status, std::string_view said) {
    // Who may read what a link names: the same words for 401 and 403, a token or none.
    const auto refusedLink = [&]() -> const char* {
        switch (asked) {
            case Asked::Page:
                return TR("Lichess refuses it: private, or for another account");
            case Asked::Broadcast:
            case Asked::BroadcastRound:
            case Asked::BroadcastTour:
                return TR("Lichess refuses this without an account allowed to read broadcasts");
            case Asked::Bookmarks:
                return TR("Lichess's bookmarks need a linked account");
            default:
                return nullptr;
        }
    };
    switch (net) {
        case NetError::None:
            return {};
        case NetError::Cancelled:
            return TR("cancelled");
        case NetError::Offline:
            return TR("Lichess cannot be reached");
        case NetError::TimedOut:
            return TR("no answer came from Lichess in time");
        case NetError::Unauthorized:
            if (const char* words = refusedLink()) return words;
            return TR("Lichess refused the token: link the account again");
        case NetError::Forbidden:
            if (const char* words = refusedLink()) return words;
            if (asked == Asked::Engines) return TR("the token may not register engines (engine:write)");
            if (asked == Asked::Link && status == 0) return TR("the link was refused on Lichess");
            return TR("the token may not do it: one more permission is needed");
        case NetError::NotFound:
            switch (asked) {
                case Asked::Game:
                    return TR("Lichess has no such game");
                case Asked::Study:
                    return TR("Lichess has no such study, or it is private");
                case Asked::Chapter:
                    return TR("Lichess has no such chapter any more");
                case Asked::Player:
                    return TR("Lichess has no account of that name");
                case Asked::FidePlayer:
                    return TR("Lichess has no such FIDE player");
                case Asked::Tournament:
                case Asked::BroadcastTour:
                    return TR("Lichess has no such tournament");
                case Asked::BroadcastRound:
                    return TR("Lichess has no such round");
                case Asked::Page:
                case Asked::Broadcast:
                case Asked::Bookmarks:
                    return TR("Lichess has nothing at this address");
                default:
                    return TR("Lichess does not know it");
            }
        case NetError::RateLimited:
            return TR("Lichess asks to wait a minute: too many requests");
        case NetError::Server:
            if (asked == Asked::Link && status == 400) return TR("Lichess did not take the code");
            if (!said.empty()) return std::string(said);  // Lichess's own words, as they are
            return trf("Lichess answered {}", status);
        case NetError::BadAnswer:
            if (asked == Asked::Engines)
                return TR("Lichess's answer could not be read: the external engine API may have changed");
            return TR("Lichess's answer could not be read");
    }
    return {};
}

std::string describe(const Failure& failure) {
    if (failure.net == NetError::None) return failure.error;
    return describe(failure.net, failure.asked, failure.status, failure.said);
}

std::string describeAlone(const Failure& failure) {
    const auto words = describe(failure);
    if (failure.net == NetError::Server && !failure.said.empty() && words == failure.said)
        return trf("Lichess refuses: “{}”", words);
    return words;
}

std::string gaveNothing(const Failure& failure) {
    if (failure.net == NetError::None) return failure.error;  // the program's own words: nothing asked of Lichess
    if (failure.net == NetError::Offline) return TR("Lichess cannot be reached from here.");
    return trf("Lichess gave nothing ({}).", describe(failure));
}

NetError netErrorOf(const HttpResult& r) {
    if (r.error == "cancelled") return NetError::Cancelled;  // https.hpp's word for a request `cancel` stopped
    if (r.status == 0) return NetError::Offline;
    if (r.status >= 200 && r.status < 300) return r.error.empty() ? NetError::None : NetError::Offline;  // cut short
    switch (r.status) {
        case 401:
            return NetError::Unauthorized;
        case 403:
            return NetError::Forbidden;
        case 404:
            return NetError::NotFound;
        case 429:
            return NetError::RateLimited;
        default:
            return NetError::Server;
    }
}

std::string lichessWords(std::string_view body) {
    const auto j = nlohmann::json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return {};
    if (const auto it = j.find("error"); it != j.end()) {
        if (it->is_string()) return it->get<std::string>();
        if (it->is_object()) {  // a form's: {"error": {"name": ["…"]}}
            std::string out;
            for (const auto& [field, messages] : it->items()) {
                std::string said;
                if (messages.is_array()) {
                    for (const auto& m : messages)
                        if (m.is_string()) said += (said.empty() ? "" : " ") + m.get<std::string>();
                } else if (messages.is_string()) {
                    said = messages.get<std::string>();
                }
                out += (out.empty() ? "" : "; ") + field + ": " + said;
            }
            return out;
        }
    }
    if (const auto it = j.find("global"); it != j.end() && it->is_array() && !it->empty() && (*it)[0].is_string())
        return (*it)[0].get<std::string>();
    return {};
}

HttpRequest request(std::string method, std::string url, std::string token, std::string accept) {
    HttpRequest r;
    r.method = std::move(method);
    r.url    = std::move(url);
    r.token  = std::move(token);
    r.headers.emplace_back("Accept", std::move(accept));
    return r;
}

HttpRequest withForm(HttpRequest request, const Form& fields) {
    request.content_type = "application/x-www-form-urlencoded";
    request.body         = formEncode(fields);
    return request;
}

HttpRequest withJson(HttpRequest request, const nlohmann::json& body) {
    request.content_type = "application/json";
    request.body         = body.dump();
    return request;
}

namespace {

// The hosts (with their port) that gave no answer to the last request sent to them: an outage is logged when it
// starts, not at each try of what asks again and again meanwhile (the TV card, the explorer).
std::mutex g_unanswered_mutex;
std::set<std::string> g_unanswered;

// "lichess.org", "127.0.0.1:4000": what is between the scheme and the path.
std::string hostOf(std::string_view url) {
    const auto scheme = url.find("://");
    const auto from   = scheme == std::string_view::npos ? 0 : scheme + 3;
    const auto end    = url.find_first_of("/?#", from);
    return std::string(url.substr(from, end == std::string_view::npos ? std::string_view::npos : end - from));
}

// Whether `host` answered until now, its outage starting: it is marked unanswered.
bool outageStarts(const std::string& host) {
    std::lock_guard lock(g_unanswered_mutex);
    return g_unanswered.insert(host).second;
}

void answered(const std::string& host) {
    std::lock_guard lock(g_unanswered_mutex);
    g_unanswered.erase(host);
}

}  // namespace

Response responseOf(const HttpRequest& request, HttpResult result) {
    Response out;
    out.asked_for = request.method + " " + request.url;
    out.status    = result.status;
    out.net       = netErrorOf(result);
    if (out.net == NetError::Cancelled) {
        // nothing to say, nothing learnt of the host
    } else if (result.status == 0) {  // no answer: the host's outage logged once, when it starts
        out.detail = result.error.empty() ? std::string("no answer") : result.error;
        if (outageStarts(hostOf(request.url)))
            Console::instance().add(fmt::format("{}: {}", out.asked_for, out.detail), true, "network");
    } else {
        answered(hostOf(request.url));
        if (out.net == NetError::Offline) {  // an answer cut short: each time
            out.detail = result.error;
            Console::instance().add(fmt::format("{}: {}", out.asked_for, out.detail), true, "network");
        } else if (out.net != NetError::None) {
            out.said = lichessWords(result.body);
        }
    }
    out.body = std::move(result.body);
    out.head = std::move(result.head);
    return out;
}

Response send(const HttpRequest& request, const std::atomic<bool>* cancel) {
    return responseOf(request, politeRequest(request, cancel));
}

void logUnreadable(const Response& response, std::string_view why) {
    Console::instance().add(fmt::format("{}: the answer could not be read ({})", response.asked_for, why), true,
                            "network");
}

std::string textOf(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool boolOf(const nlohmann::json& j, const char* key, bool otherwise) {
    const auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : otherwise;
}

}  // namespace fcg::lichess
