// Files downloaded, resumed where an earlier try stopped and checked against their SHA-256 (https.hpp), on httpStream.

#include <cctype>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <memory>
#include <system_error>
#include <vector>

#include <psa/crypto.h>

#include <fmt/include/fmt/format.h>

#include <tour/files.hpp>

#include "https.hpp"

namespace fcg {

std::string sha256File(const std::filesystem::path& file) {
    if (psa_crypto_init() != PSA_SUCCESS) return {};
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    if (psa_hash_setup(&operation, PSA_ALG_SHA_256) != PSA_SUCCESS) return {};
    std::vector<char> buffer(std::size_t{1} << 20);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto n = static_cast<std::size_t>(in.gcount());
        if (n > 0 && psa_hash_update(&operation, reinterpret_cast<const uint8_t*>(buffer.data()), n) != PSA_SUCCESS) {
            psa_hash_abort(&operation);
            return {};
        }
    }
    if (in.bad()) {
        psa_hash_abort(&operation);
        return {};
    }
    uint8_t digest[32];
    std::size_t size = 0;
    if (psa_hash_finish(&operation, digest, sizeof(digest), &size) != PSA_SUCCESS || size != sizeof(digest)) return {};
    std::string out;
    for (const auto b : digest) out += fmt::format("{:02x}", b);
    return out;
}

Download httpDownload(const std::string& url, const std::filesystem::path& file, DownloadProgress* progress,
                      const std::atomic<bool>* cancel, std::string_view sha256) {
    namespace fs = std::filesystem;
    Download out;
    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    const auto part = fs::path(file).concat(".part");
    const auto tag  = fs::path(file).concat(".part.tag");  // what tells the file on the server has not changed
    // A second try only when the part kept turned out to be of another file.
    for (int attempt = 0;; ++attempt) {
        uint64_t have = fs::is_regular_file(part, ec) ? fs::file_size(part, ec) : 0;
        if (ec) have = 0;
        HttpRequest request;
        request.url       = url;
        request.method    = "GET";
        request.timeout_s = 30;
        if (have > 0) {
            request.headers.emplace_back("Range", fmt::format("bytes={}-", have));
            std::string validator;
            std::ifstream in(tag);
            if (std::getline(in, validator) && !validator.empty()) request.headers.emplace_back("If-Range", validator);
        }
        bool other_file = false;
        out             = Download{};
        tour::File f;
        HttpStream stream;
        stream.cancel  = cancel;
        stream.on_head = [&](int status, std::string_view head) {
            // "bytes 100-199/200", or "bytes */200" with a 416.
            uint64_t from = 0, total = 0;
            bool ranged = false;
            if (const auto range = httpHeader(head, "Content-Range"); range && range->rfind("bytes ", 0) == 0) {
                const auto slash = range->find('/');
                if (slash != std::string::npos) {
                    ranged = true;
                    if ((*range)[6] != '*') std::from_chars(range->data() + 6, range->data() + range->size(), from);
                    std::from_chars(range->data() + slash + 1, range->data() + range->size(), total);
                }
            }
            if (status == 206 && have > 0) {
                if (!ranged || from != have) {
                    other_file = true;
                    return false;
                }
                out.resumed = true;
                f.reset(tour::openFile(part, "ab"));
            } else if (status == 200) {
                have  = 0;  // the whole file, from its start
                total = 0;
                if (const auto length = httpHeader(head, "Content-Length")) {
                    std::from_chars(length->data(), length->data() + length->size(), total);
                }
                // A strong ETag, or the file's date, to resume from later.
                auto validator = httpHeader(head, "ETag");
                if (validator && validator->rfind("W/", 0) == 0) validator.reset();
                if (!validator) validator = httpHeader(head, "Last-Modified");
                if (validator) {
                    std::ofstream(tag, std::ios::trunc) << *validator << '\n';
                } else {
                    fs::remove(tag, ec);
                }
                f.reset(tour::openFile(part, "wb"));
            } else if (status == 416 && have > 0) {
                // Nothing after what was kept: the whole file when it is its size, else the file has changed.
                other_file = !ranged || total != have;
                return false;
            } else {
                out.error = fmt::format("the server answered {} for {}", status, url);
                return false;
            }
            if (!f) {
                out.error = "cannot write " + part.string();
                return false;
            }
            if (progress) {
                progress->size  = total;
                progress->bytes = have;
            }
            return true;
        };
        stream.on_data = [&](std::string_view data) {
            if (std::fwrite(data.data(), 1, data.size(), f.get()) != data.size()) {
                out.error = "cannot write " + part.string() + " (is the disk full?)";
                return false;
            }
            have += data.size();
            if (progress) progress->bytes = have;
            return true;
        };
        const auto result = httpStream(request, stream);
        const bool closed = !f || std::fclose(f.release()) == 0;
        out.status        = result.status;
        if (other_file) {
            fs::remove(part, ec);
            fs::remove(tag, ec);
            if (attempt == 0) continue;
            out.error = "the server does not send the rest of " + url + " as asked";
            return out;
        }
        if (out.error.empty()) out.error = result.error;
        if (out.error.empty() && result.status == 0) out.error = "no answer for " + url;
        if (out.error.empty() && !closed) out.error = "cannot write " + part.string();
        out.bytes = have;
        if (!out.error.empty()) return out;  // the part kept: the next call goes on from there
        break;
    }
    if (!sha256.empty()) {
        std::string want(sha256);
        for (auto& c : want) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (sha256File(part) != want) {
            fs::remove(part, ec);
            fs::remove(tag, ec);
            out.error = "the file downloaded from " + url + " is not the one expected: its SHA-256 differs";
            return out;
        }
    }
    fs::rename(part, file, ec);
    if (ec) {
        out.error = "cannot rename " + part.string() + ": " + ec.message();
        return out;
    }
    fs::remove(tag, ec);
    out.ok = true;
    return out;
}

}  // namespace fcg
