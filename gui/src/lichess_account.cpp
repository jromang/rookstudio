#include "lichess_account.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include <psa/crypto.h>
#include <json.hpp>

#include <fmt/include/fmt/format.h>
#include <tour/files.hpp>

#include "https.hpp"

namespace fcg::lichess {

namespace {

std::string base64url(const uint8_t* data, std::size_t n) {
    static constexpr char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    for (std::size_t i = 0; i < n; i += 3) {
        const uint32_t v = (uint32_t{data[i]} << 16) | (i + 1 < n ? uint32_t{data[i + 1]} << 8 : 0) |
                           (i + 2 < n ? uint32_t{data[i + 2]} : 0);
        out += ALPHABET[(v >> 18) & 63];
        out += ALPHABET[(v >> 12) & 63];
        if (i + 1 < n) out += ALPHABET[(v >> 6) & 63];
        if (i + 2 < n) out += ALPHABET[v & 63];
    }
    return out;
}

// Random characters of RFC 7636's unreserved set.
std::string randomText(std::size_t n) {
    static constexpr char CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    std::vector<uint8_t> bytes(n);
    if (psa_crypto_init() != PSA_SUCCESS || psa_generate_random(bytes.data(), bytes.size()) != PSA_SUCCESS)
        throw std::runtime_error("no random numbers to ask Lichess with");
    std::string out;
    for (const auto b : bytes) out += CHARS[b % 66];
    return out;
}

std::string percentDecoded(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
                   std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += static_cast<char>(std::stoi(std::string(s.substr(i + 1, 2)), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string page(std::string_view title, std::string_view text) {
    return fmt::format(
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>{0}</title></head>"
        "<body style=\"font-family:sans-serif;margin:3em\"><h1>{0}</h1><p>{1}</p></body></html>",
        title, text);
}

void reply(net::TcpConnection& c, int status, const std::string& body) {
    c.sendAll(fmt::format("HTTP/1.1 {} {}\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: {}\r\n"
                          "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                          status, status == 200 ? "OK" : "Not Found", body.size()) +
              body);
}

std::string reason(const HttpResult& r) {
    if (r.status == 0) return r.error.empty() ? std::string("Lichess did not answer") : r.error;
    if (r.status == 401) return "Lichess refused the token";
    if (r.status == 429) return "Lichess asks to wait a minute: too many requests";
    return fmt::format("Lichess answered {}", r.status);
}

}  // namespace

std::string challengeOf(std::string_view verifier) {
    std::array<uint8_t, 32> digest{};
    std::size_t size = 0;
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_compute(PSA_ALG_SHA_256, reinterpret_cast<const uint8_t*>(verifier.data()), verifier.size(),
                         digest.data(), digest.size(), &size) != PSA_SUCCESS)
        return {};
    return base64url(digest.data(), size);
}

Pkce makePkce() {
    Pkce p;
    p.verifier  = randomText(64);
    p.challenge = challengeOf(p.verifier);
    p.state     = randomText(24);
    return p;
}

std::string authorizeUrl(const Pkce& pkce, std::string_view redirect_uri, const std::vector<std::string>& scopes,
                         std::string_view username) {
    std::string scope;
    for (const auto& s : scopes) scope += (scope.empty() ? "" : " ") + s;
    std::string url = servers().api + "/oauth?response_type=code&client_id=" + urlEncode(CLIENT_ID) +
                      "&redirect_uri=" + urlEncode(redirect_uri) +
                      "&code_challenge_method=S256&code_challenge=" + urlEncode(pkce.challenge) +
                      "&state=" + urlEncode(pkce.state);
    if (!scope.empty()) url += "&scope=" + urlEncode(scope);
    if (!username.empty()) url += "&username=" + urlEncode(username);
    return url;
}

bool Account::has(std::string_view scope) const {
    return std::find(scopes.begin(), scopes.end(), scope) != scopes.end();
}

Answer<TokenState> testToken(std::string_view token, const std::atomic<bool>* cancel) {
    Answer<TokenState> a;
    HttpRequest request;
    request.url          = servers().api + "/api/token/test";
    request.method       = "POST";
    request.content_type = "text/plain";
    request.body         = std::string(token);
    request.headers.emplace_back("Accept", "application/json");
    const auto r = politeRequest(request, cancel);
    a.status     = r.status;
    if (r.status != 200) {
        a.error = reason(r);
        return a;
    }
    try {
        const auto j  = nlohmann::json::parse(r.body);
        const auto it = j.find(std::string(token));
        if (it == j.end() || it->is_null()) return a;  // no longer a token
        TokenState t;
        t.user = it->value("userId", std::string());
        // "study:read,puzzle:read", empty when it has none.
        const auto scopes = it->value("scopes", std::string());
        for (std::size_t from = 0; from < scopes.size();) {
            const auto comma = scopes.find(',', from);
            const auto one   = scopes.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
            if (!one.empty()) t.scopes.push_back(one);
            if (comma == std::string::npos) break;
            from = comma + 1;
        }
        if (const auto e = it->find("expires"); e != it->end() && e->is_number_integer())
            t.expires = e->get<int64_t>() / 1000;  // milliseconds
        a.value = std::move(t);
    } catch (const std::exception& e) {
        a.error = std::string("Lichess's answer could not be read: ") + e.what();
    }
    return a;
}

Callback::Callback() { (void)listener_.listen(0, true); }

std::string Callback::redirectUri() const { return fmt::format("http://127.0.0.1:{}/callback", port()); }

Callback::Reply Callback::wait(std::chrono::milliseconds within, const std::atomic<bool>* cancel) {
    Reply r;
    if (port() == 0) {
        r.error = "the program cannot receive Lichess's answer (no port on 127.0.0.1)";
        return r;
    }
    const auto deadline = std::chrono::steady_clock::now() + within;
    for (;;) {
        if (cancel && *cancel) {
            r.error = "cancelled";
            return r;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            r.error = "no answer came from Lichess in time";
            return r;
        }
        const auto fd = listener_.accept(100);
        if (!fd) continue;
        net::TcpConnection c(*fd);
        const auto head = c.readUntil("\r\n\r\n", 1 << 14, 5000);
        const auto end  = head.find(" HTTP/");
        if (head.rfind("GET ", 0) != 0 || end == std::string::npos) continue;
        const std::string target = head.substr(4, end - 4);
        if (target != "/callback" && target.rfind("/callback?", 0) != 0) {
            reply(c, 404, page("Not here", "This address only takes Lichess's answer."));
            continue;
        }
        const auto question = target.find('?');
        std::string_view query =
            question == std::string::npos ? std::string_view() : std::string_view(target).substr(question + 1);
        while (!query.empty()) {
            const auto amp   = query.find('&');
            const auto pair  = query.substr(0, amp);
            const auto equal = pair.find('=');
            const auto name  = pair.substr(0, equal);
            const auto value = equal == std::string_view::npos ? std::string() : percentDecoded(pair.substr(equal + 1));
            if (name == "code") r.code = value;
            if (name == "state") r.state = value;
            if (name == "error") r.error = value;
            query = amp == std::string_view::npos ? std::string_view() : query.substr(amp + 1);
        }
        if (r.code.empty() && r.error.empty()) r.error = "Lichess's answer has no code";
        reply(c, 200,
              r.error.empty() ? page("RookStudio", "Your Lichess account is being linked: you can close this page.")
                              : page("RookStudio", "Your Lichess account was not linked: you can close this page."));
        return r;
    }
}

Answer<Account> exchange(std::string_view code, const Pkce& pkce, std::string_view redirect_uri,
                         const std::atomic<bool>* cancel) {
    Answer<Account> a;
    HttpRequest request;
    request.url          = servers().api + "/api/token";
    request.method       = "POST";
    request.content_type = "application/x-www-form-urlencoded";
    request.body         = formEncode({{"grant_type", "authorization_code"},
                                       {"code", std::string(code)},
                                       {"code_verifier", pkce.verifier},
                                       {"redirect_uri", std::string(redirect_uri)},
                                       {"client_id", CLIENT_ID}});
    request.headers.emplace_back("Accept", "application/json");
    const auto r = politeRequest(request, cancel);
    a.status     = r.status;
    if (r.status != 200) {
        a.error = r.status == 400 ? "Lichess did not take the code" : reason(r);
        return a;
    }
    try {
        const auto j = nlohmann::json::parse(r.body);
        Account account;
        account.token = j.at("access_token").get<std::string>();
        if (const auto e = j.find("expires_in"); e != j.end() && e->is_number_integer())
            account.expires =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count() +
                e->get<int64_t>();
        if (account.token.empty()) throw std::runtime_error("an empty token");
        a.value = std::move(account);
    } catch (const std::exception& e) {
        a.error = std::string("Lichess's answer could not be read: ") + e.what();
    }
    return a;
}

Answer<std::string> accountName(std::string_view token, const std::atomic<bool>* cancel) {
    Answer<std::string> a;
    HttpRequest request;
    request.url    = servers().api + "/api/account";
    request.method = "GET";
    request.token  = std::string(token);
    request.headers.emplace_back("Accept", "application/json");
    const auto r = politeRequest(request, cancel);
    a.status     = r.status;
    if (r.status != 200) {
        a.error = reason(r);
        return a;
    }
    try {
        a.value = nlohmann::json::parse(r.body).at("username").get<std::string>();
    } catch (const std::exception& e) {
        a.error = std::string("Lichess's answer could not be read: ") + e.what();
    }
    return a;
}

std::string revoke(std::string_view token, const std::atomic<bool>* cancel) {
    HttpRequest request;
    request.url    = servers().api + "/api/token";
    request.method = "DELETE";
    request.token  = std::string(token);
    const auto r   = politeRequest(request, cancel);
    return r.status == 204 || r.status == 200 ? std::string() : reason(r);
}

Answer<Account> link(const std::function<bool(const std::string& url)>& open, const std::vector<std::string>& scopes,
                     std::chrono::milliseconds within, const std::atomic<bool>* cancel, std::string_view username) {
    Answer<Account> a;
    Callback callback;
    if (callback.port() == 0) {
        a.error = "the program cannot receive Lichess's answer (no port on 127.0.0.1)";
        return a;
    }
    Pkce pkce;
    try {
        pkce = makePkce();
    } catch (const std::exception& e) {
        a.error = e.what();
        return a;
    }
    (void)open(authorizeUrl(pkce, callback.redirectUri(), scopes, username));
    const auto reply = callback.wait(within, cancel);
    if (!reply.error.empty()) {
        a.error = reply.error == "access_denied" ? "the link was refused on Lichess" : reply.error;
        return a;
    }
    if (reply.state != pkce.state) {
        a.error = "an answer that does not come from this question";
        return a;
    }
    a = exchange(reply.code, pkce, callback.redirectUri(), cancel);
    if (!a.value) return a;
    const auto name = accountName(a.value->token, cancel);
    if (!name.value) {
        (void)revoke(a.value->token, cancel);
        a.value.reset();
        a.error  = name.error;
        a.status = name.status;
        return a;
    }
    a.value->username = *name.value;
    // What the token may do, as Lichess says it; else what was asked.
    a.value->scopes = scopes;
    if (const auto tested = testToken(a.value->token, cancel); tested.value) {
        a.value->scopes = tested.value->scopes;
        if (tested.value->expires > 0) a.value->expires = tested.value->expires;
    }
    return a;
}

std::optional<Account> loadAccount(const std::filesystem::path& profile, const char* file) {
    std::ifstream in(profile / file, std::ios::binary);
    if (!in) return std::nullopt;
    try {
        const auto j = nlohmann::json::parse(in);
        Account a;
        a.username = j.at("username").get<std::string>();
        a.token    = j.at("token").get<std::string>();
        a.expires  = j.value("expires", int64_t{0});
        a.scopes   = j.value("scopes", std::vector<std::string>{});  // none before #45: the link asked for none
        if (a.token.empty()) return std::nullopt;
        return a;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void saveAccount(const std::filesystem::path& profile, const Account& account, const char* file) {
    const nlohmann::json j{{"username", account.username},
                           {"token", account.token},
                           {"expires", account.expires},
                           {"scopes", account.scopes}};
    tour::replaceFile(profile / file, j.dump(1), false, true);
}

std::string forgetAccount(const std::filesystem::path& profile, const std::atomic<bool>* cancel, const char* file) {
    std::string error;
    if (const auto account = loadAccount(profile, file)) error = revoke(account->token, cancel);
    std::error_code ec;
    std::filesystem::remove(profile / file, ec);
    return error;
}

}  // namespace fcg::lichess
