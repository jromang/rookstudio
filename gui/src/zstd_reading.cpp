#include "zstd_reading.hpp"

#include <charconv>
#include <fstream>
#include <functional>
#include <thread>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <study/sources.hpp>
#include <tour/files.hpp>

#include "https.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

bool number(std::string_view text, uint64_t& out) {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    const auto end = text.data() + text.size();
    return std::from_chars(text.data(), end, out).ec == std::errc{};
}

// "Tue, 03 Nov 2026 10:00:00 GMT" as 2026-11-03.
std::string httpDate(const std::string& date) {
    static constexpr std::string_view MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    std::vector<std::string> parts;
    std::size_t from = 0;
    while (from <= date.size()) {
        const auto space = date.find(' ', from);
        parts.push_back(date.substr(from, space == std::string::npos ? std::string::npos : space - from));
        if (space == std::string::npos) break;
        from = space + 1;
    }
    if (parts.size() < 4) return date;
    for (std::size_t m = 0; m < 12; ++m)
        if (parts[2] == MONTHS[m]) return fmt::format("{}-{:02}-{}", parts[3], m + 1, parts[1]);
    return date;
}

std::optional<nlohmann::json> readJsonFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    return j;
}

}  // namespace

TaskEnd readZstd(const std::string& source, const fs::path& state_file, ZstdConsumer& consumer, ZstdProgress& progress,
                 const std::atomic<bool>& cancel, const ZstdReadingOptions& options) {
    std::error_code ec;
    fs::create_directories(state_file.parent_path(), ec);
    const bool remote = source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0;

    // Where the reading is: the end of a frame, and the consumer's state there.
    uint64_t offset = 0;
    nlohmann::json kept;
    std::string validator;  // that the file has not changed: the server's ETag or date, a file's size and time
    uint64_t size = 0;
    std::string date;
    if (!remote) {  // a file downloaded before: its date is not the server's
        size      = fs::file_size(source, ec);
        validator = fmt::format("{}:{}", size, fs::last_write_time(source, ec).time_since_epoch().count());
    }
    if (const auto j = readJsonFile(state_file);
        j && j->value("source", "") == source && (remote || j->value("validator", "") == validator)) {
        offset = j->value("offset", uint64_t{0});
        kept   = j->value("state", nlohmann::json::object());
        if (remote) {
            validator = j->value("validator", "");
            size      = j->value("size", uint64_t{0});
            date      = j->value("date", "");
        }
        consumer.restore(kept);
        progress.resumed = true;
    } else {
        consumer.startAgain();
    }
    const auto saveState = [&] {
        nlohmann::json j{{"source", source}, {"validator", validator}, {"size", size},
                         {"date", date},     {"offset", offset},       {"state", kept}};
        tour::replaceFile(state_file, j.dump());
        if (options.on_saved) options.on_saved();
    };
    const auto startAgain = [&] {
        consumer.startAgain();
        offset = 0;
        kept   = nlohmann::json::object();
    };

    study::ZstdStream zstd;
    uint64_t at = 0, chunk_start = 0;  // at: the compressed bytes read; chunk_start: the piece being decoded's offset
    auto last_saved                                     = std::chrono::steady_clock::now();
    const std::function<bool(std::string_view)> on_text = [&](std::string_view piece) {
        return consumer.text(piece) && !cancel.load();
    };
    const std::function<void(std::size_t)> on_frame_end = [&](std::size_t read) {
        offset = chunk_start + read;
        kept   = consumer.mark();
        if (const auto now = std::chrono::steady_clock::now();
            std::chrono::duration<double>(now - last_saved).count() >= options.checkpoint_every) {
            consumer.commit();
            saveState();
            last_saved = now;
        }
    };
    const auto feed = [&](std::string_view data) {
        chunk_start     = at;
        const bool more = zstd.feed(data, on_text, on_frame_end);
        at += data.size();
        progress.bytes = at;
        return more && consumer.error().empty() && !cancel;
    };
    // From the place kept: the decoder at a frame's start, the consumer as it was there.
    const auto fromPlace = [&] {
        zstd.restart();
        at = offset;
    };
    // At a stop: what was found made safe, the place of the last frame's end kept, unless nothing was read.
    const auto stop = [&](TaskEnd end) {
        consumer.commit();
        if (offset > 0) saveState();
        return end;
    };
    const auto done = [&] {
        if (!consumer.error().empty()) return TaskEnd::of(consumer.error());
        auto why = consumer.finish(date);
        if (why.empty()) fs::remove(state_file, ec);
        return TaskEnd::of(std::move(why));
    };
    // Why the source stopped, as a kind (#141): its words without the file's path, the file beside them.
    const auto failed = [&](NetFailure::Kind kind, std::string detail, int status = 0) {
        NetFailure f;
        f.kind   = kind;
        f.status = status;
        f.detail = std::move(detail);
        if (remote)
            f.host = urlHost(source);
        else
            f.path = source;
        return TaskEnd::of(std::move(f));
    };

    if (!remote) {
        progress.size = size;
        std::ifstream in(source, std::ios::binary);
        if (!in) return failed(NetFailure::Kind::Unreadable, "cannot open " + source);
        fromPlace();
        in.seekg(static_cast<std::streamoff>(offset));
        std::vector<char> buffer(1 << 20);
        bool more = true;
        while (more) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto n = static_cast<std::size_t>(in.gcount());
            if (n == 0) break;
            more = feed(std::string_view(buffer.data(), n));
        }
        if (cancel) return stop(TaskEnd::cancel());
        if (!consumer.error().empty()) return stop(TaskEnd::of(consumer.error()));
        if (!zstd.betweenFrames()) return stop(failed(NetFailure::Kind::Unreadable, source + " is cut short"));
        return done();
    }

    for (int attempt = 0;; ++attempt) {
        // Each try goes on from the place kept: the consumer back to its state there.
        if (attempt > 0 || offset > 0) consumer.restore(kept);
        fromPlace();
        HttpRequest request;
        request.url       = source;
        request.method    = "GET";
        request.timeout_s = 30;
        if (offset > 0) {
            request.headers.emplace_back("Range", fmt::format("bytes={}-", offset));
            if (!validator.empty()) request.headers.emplace_back("If-Range", validator);
        }
        std::string refused;
        int refused_status = 0;  // the status that refused it; 0 for the rest of the file not sent as asked
        HttpStream stream;
        stream.cancel  = &cancel;
        stream.on_head = [&](int status, std::string_view head) {
            uint64_t from = 0, total = 0;
            bool ranged = false;
            if (const auto range = httpHeader(head, "Content-Range"); range && range->rfind("bytes ", 0) == 0) {
                if (const auto slash = range->find('/'); slash != std::string::npos) {
                    ranged = true;
                    if ((*range)[6] != '*') std::from_chars(range->data() + 6, range->data() + slash, from);
                    std::from_chars(range->data() + slash + 1, range->data() + range->size(), total);
                }
            }
            if (status == 206 && offset > 0 && ranged && from == offset) {
                size = total;
            } else if (status == 200) {
                if (offset > 0) {  // the file has changed, or the server sends it whole: from its start
                    startAgain();
                    fromPlace();
                    progress.resumed = false;
                }
                size = 0;
                if (const auto length = httpHeader(head, "Content-Length")) (void)number(*length, size);
                validator.clear();  // a strong ETag, or the file's date
                if (const auto etag = httpHeader(head, "ETag"); etag && etag->rfind("W/", 0) != 0) {
                    validator = *etag;
                } else if (const auto modified = httpHeader(head, "Last-Modified")) {
                    validator = *modified;
                }
                if (const auto modified = httpHeader(head, "Last-Modified")) date = httpDate(*modified);
            } else if (status == 416 || status == 206) {
                refused = "the server does not send the rest of the file as asked";
                return false;
            } else {
                refused        = fmt::format("the server answered {} for {}", status, source);
                refused_status = status;
                return false;
            }
            progress.size = size;
            return true;
        };
        stream.on_data    = feed;
        const auto result = politeStream(request, stream);
        if (cancel) return stop(TaskEnd::cancel());
        if (!consumer.error().empty()) return stop(TaskEnd::of(consumer.error()));
        if (!refused.empty()) {
            if (offset > 0 && attempt == 0) {  // read again from the start
                startAgain();
                saveState();
                continue;
            }
            if (refused_status == 0) return failed(NetFailure::Kind::Unexpected, refused);
            return failed(refused_status == 404   ? NetFailure::Kind::NotFound
                          : refused_status == 429 ? NetFailure::Kind::RateLimited
                                                  : NetFailure::Kind::Status,
                          refused, refused_status);
        }
        if (result.error.empty() && result.status != 0 && zstd.betweenFrames() && (size == 0 || at == size))
            return done();
        // Cut short: on from the last frame's end, a few times, waiting longer each time.
        consumer.commit();
        if (offset > 0) saveState();
        if (attempt >= options.retries)
            return failed(NetFailure::Kind::Offline,
                          result.error.empty() ? std::string("the download was cut short") : result.error);
        const auto wait = std::chrono::steady_clock::now() + options.retry_wait * (1 << attempt);
        while (std::chrono::steady_clock::now() < wait) {
            if (cancel) return TaskEnd::cancel();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

}  // namespace fcg
