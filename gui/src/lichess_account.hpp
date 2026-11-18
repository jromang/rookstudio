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

// What proves that the program that asked is the one that exchanges the code (RFC 7636), and what the answer must
// bring back.
struct Pkce {
    std::string verifier;   // 64 random characters
    std::string challenge;  // base64url(SHA-256(verifier)), without padding
    std::string state;
};
[[nodiscard]] Pkce makePkce();
[[nodiscard]] std::string challengeOf(std::string_view verifier);
// Lichess's page that asks the user, for `scopes` (none: the account's name and public data only).
[[nodiscard]] std::string authorizeUrl(const Pkce& pkce, std::string_view redirect_uri,
                                       const std::vector<std::string>& scopes);

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
    // come back to the program. Other requests (a browser's icon) are answered 404 and the wait goes on.
    [[nodiscard]] Reply wait(std::chrono::milliseconds within, const std::atomic<bool>* cancel = nullptr);

   private:
    net::TcpListener listener_;
};

struct Account {
    std::string username;
    std::string token;
    int64_t expires = 0;  // seconds since the epoch, 0 when Lichess did not say
};

// The code exchanged for a token (no username yet).
[[nodiscard]] Answer<Account> exchange(std::string_view code, const Pkce& pkce, std::string_view redirect_uri,
                                       const std::atomic<bool>* cancel = nullptr);
// The name of the account a token is for.
[[nodiscard]] Answer<std::string> accountName(std::string_view token, const std::atomic<bool>* cancel = nullptr);
// The token revoked: an error, empty when Lichess took it.
[[nodiscard]] std::string revoke(std::string_view token, const std::atomic<bool>* cancel = nullptr);

// The whole question: `open` is given Lichess's page to open in the browser (false: the program could not, and shows
// the address to copy; the wait goes on).
[[nodiscard]] Answer<Account> link(const std::function<bool(const std::string& url)>& open,
                                   const std::vector<std::string>& scopes,
                                   std::chrono::milliseconds within = std::chrono::minutes(5),
                                   const std::atomic<bool>* cancel  = nullptr);

// In the profile: lichess-account.json, readable and writable by its owner alone.
[[nodiscard]] std::optional<Account> loadAccount(const std::filesystem::path& profile);
void saveAccount(const std::filesystem::path& profile, const Account& account);  // throws std::runtime_error
// Unlinks: the token revoked at Lichess, then the file removed whatever Lichess said; Lichess's error, if any.
std::string forgetAccount(const std::filesystem::path& profile, const std::atomic<bool>* cancel = nullptr);

}  // namespace fcg::lichess
