#pragma once

// Why a request to a site, or a download, gave nothing (#141), as the window keeps it: a kind it words in the user's
// language where it is shown and that tells what repairs it (trying again once the host's pause is over, opening the
// folder), never read from words. Lichess's API keeps its own (lichess::Failure, lichess_http.hpp), in the same shape;
// this one is the other sites' (TWIC, chess.com, FIDE, GitHub, the tables' and the models' hosts) and the downloads',
// Lichess's files too. Without ImGui.

#include <cstdint>
#include <filesystem>
#include <string>

namespace fcg {

struct NetFailure {
    enum class Kind : uint8_t {
        None,         // it went through
        Cancelled,    // the program stopped it (Cancel, a sheet closed, the program closing): nothing to say
        Offline,      // no answer, or one cut short: no network, the site down or too slow, a proxy in the way
        NotFound,     // 404: the site has no such page or file
        RateLimited,  // 429: too many requests; the host pauses (HostGate::pausedFor(host)) before it is asked again
        Status,       // another status: the site in trouble (5xx), or refusing the request (4xx)
        Unwritable,   // a file could not be written (`path`): a folder refused, the disk full
        Unreadable,   // a file on the disk could not be read to its end (`path`): gone, refused, cut short
        Unexpected,   // what came is not what was asked: a page the program cannot read, a file whose SHA-256 or size
                      // differs, the rest of a file the server would not send
    };
    Kind kind  = Kind::None;
    int status = 0;    // HTTP's, 0 without an answer
    std::string host;  // the site's (urlHost()): whose pause a RateLimited waits for
    std::filesystem::path
        path;            // the file concerned, written or that could not be: shown beside the words, not in them
    std::string detail;  // the network's or the program's words, in English with the address: the log's

    [[nodiscard]] bool failed() const noexcept { return kind != Kind::None && kind != Kind::Cancelled; }
    [[nodiscard]] bool cancelled() const noexcept { return kind == Kind::Cancelled; }
    bool operator==(const NetFailure&) const = default;
};

// Its words in the user's language, a phrase without a final stop for a sentence ("The games could not be added:
// {}"): the host named for the network's failures, the file's path never. Empty for None.
[[nodiscard]] std::string describe(const NetFailure& failure);
// A line of its own for an outage or the host's pause, `site` named as the user knows it ("theweekinchess.com cannot be
// reached from here."); empty for the other kinds, which the line words itself.
[[nodiscard]] std::string siteSentence(const NetFailure& failure, const std::string& site);

}  // namespace fcg
