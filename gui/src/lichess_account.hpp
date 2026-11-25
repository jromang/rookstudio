#pragma once

// A Lichess account linked to the program, as Lichess asks a desktop program to (OAuth 2 with PKCE, no secret, no
// registration): the browser opens Lichess's page, where the user says yes; Lichess sends the browser back to a server
// the program opens on 127.0.0.1 for that one answer, with a code the program exchanges for a token. The token is kept
// in the profile, readable by its owner alone; Lichess's tokens last a year. Unlinking revokes it.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lichess_api.hpp"
#include "net.hpp"

namespace fcg::lichess {

inline constexpr const char* CLIENT_ID = "rookstudio";

// What a token may be allowed to do, asked when a feature first needs it (#45). Reading studies and broadcasts: Lichess
// asks it of every broadcast tournament and round request since 2026-08, unauthenticated ones being "heavily
// rate-limited" and possibly stopped.
inline constexpr const char* SCOPE_STUDY_READ = "study:read";
// Creating studies and broadcasts: a tournament on the user's own broadcast (#51).
inline constexpr const char* SCOPE_STUDY_WRITE = "study:write";

// What proves that the program that asked is the one that exchanges the code (RFC 7636), and what the answer must
// bring back.
struct Pkce {
    std::string verifier;   // 64 random characters
    std::string challenge;  // base64url(SHA-256(verifier)), without padding
    std::string state;
};
[[nodiscard]] Pkce makePkce();
[[nodiscard]] std::string challengeOf(std::string_view verifier);
// Lichess's page that asks the user, for `scopes` (none: the account's name and public data only); `username`, when
// given, the account Lichess offers to log in with.
[[nodiscard]] std::string authorizeUrl(const Pkce& pkce, std::string_view redirect_uri,
                                       const std::vector<std::string>& scopes, std::string_view username = {});

// The server Lichess sends the browser back to: on 127.0.0.1, a port of its own, for one answer.
class Callback {
   public:
    Callback();  // port() is 0 when it cannot listen
    [[nodiscard]] uint16_t port() const { return listener_.port(); }
    [[nodiscard]] std::string redirectUri() const;  // http://127.0.0.1:<port>/callback
    struct Reply {
        std::string code, state;
        std::string error;  // Lichess's ("access_denied": the user said no), or why no answer came
    };
    // Waits for the browser's request, at most `within`, or until `cancel`; the browser is shown a page that says to
    // come back to the program. Other requests (a browser's icon) are answered 404 and the wait goes on. Given the
    // question's `state`, an answer that does not carry it (another program's, an old page's) is answered 400 and the
    // wait goes on: it can neither end the link nor make it look refused.
    [[nodiscard]] Reply wait(std::chrono::milliseconds within, const std::atomic<bool>* cancel = nullptr,
                             std::string_view state = {});

   private:
    net::TcpListener listener_;
};

struct Account {
    std::string username;
    std::string token;
    int64_t expires = 0;              // seconds since the epoch, 0 when Lichess did not say
    std::vector<std::string> scopes;  // what the token may do, as Lichess granted it
    [[nodiscard]] bool has(std::string_view scope) const;
};

// What Lichess says of a token: its account, what it may do, until when (seconds since the epoch, 0: for ever).
struct TokenState {
    std::string user;
    std::vector<std::string> scopes;
    int64_t expires = 0;
};
// The token tested (POST /api/token/test); no value, with no error, when Lichess no longer takes it (revoked, or
// expired).
[[nodiscard]] Answer<TokenState> testToken(std::string_view token, const std::atomic<bool>* cancel = nullptr);

// The code exchanged for a token (no username yet).
[[nodiscard]] Answer<Account> exchange(std::string_view code, const Pkce& pkce, std::string_view redirect_uri,
                                       const std::atomic<bool>* cancel = nullptr);
// The name of the account a token is for.
[[nodiscard]] Answer<std::string> accountName(std::string_view token, const std::atomic<bool>* cancel = nullptr);
// The token revoked: an error, empty when Lichess took it.
[[nodiscard]] std::string revoke(std::string_view token, const std::atomic<bool>* cancel = nullptr);

// The whole question: `open` is given Lichess's page to open in the browser (false: the program could not, and shows
// the address to copy; the wait goes on). The account's scopes are those Lichess says the token has, else those asked.
// `username`: the account Lichess offers, for a link made again with more scopes. The user's no on Lichess's page is
// the error LINK_REFUSED, which linkRefused() tells from every other: a token Lichess refuses after a yes (a 401 on the
// account's name) says "refused" too, and is no refusal of the user's (#101).
[[nodiscard]] Answer<Account> link(const std::function<bool(const std::string& url)>& open,
                                   const std::vector<std::string>& scopes,
                                   std::chrono::milliseconds within = std::chrono::minutes(5),
                                   const std::atomic<bool>* cancel = nullptr, std::string_view username = {});
inline constexpr std::string_view LINK_REFUSED = "the link was refused on Lichess";
[[nodiscard]] inline bool linkRefused(std::string_view error) noexcept { return error == LINK_REFUSED; }

// In the profile, readable and writable by its owner alone: lichess-account.json, the user's account; a Lichess bot's
// account has its own (#85), its token apart from the user's.
inline constexpr const char* ACCOUNT_FILE     = "lichess-account.json";
inline constexpr const char* BOT_ACCOUNT_FILE = "lichess-bot.json";
[[nodiscard]] std::optional<Account> loadAccount(const std::filesystem::path& profile, const char* file = ACCOUNT_FILE);
// Throws std::runtime_error.
void saveAccount(const std::filesystem::path& profile, const Account& account, const char* file = ACCOUNT_FILE);
// Unlinks: the token revoked at Lichess, then the file removed whatever Lichess said; Lichess's error, if any.
std::string forgetAccount(const std::filesystem::path& profile, const std::atomic<bool>* cancel = nullptr,
                          const char* file = ACCOUNT_FILE);

}  // namespace fcg::lichess
