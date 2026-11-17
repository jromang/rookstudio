#include "sources.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include <zlib.h>
#include <zstd.h>

namespace fcg::study {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t INPUT = 1 << 20;

std::ifstream openFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + file.string());
    return in;
}

class PlainSource : public ByteSource {
   public:
    explicit PlainSource(const fs::path& file) : in_(openFile(file)) { name_ = file.filename().string(); }
    std::size_t read(char* out, std::size_t size) override {
        in_.read(out, static_cast<std::streamsize>(size));
        const auto n = static_cast<std::size_t>(in_.gcount());
        consumed_ += n;
        return n;
    }
    [[nodiscard]] uint64_t consumed() const override { return consumed_; }

   private:
    std::ifstream in_;
    uint64_t consumed_ = 0;
};

class ZstdSource : public ByteSource {
   public:
    explicit ZstdSource(const fs::path& file) : in_(openFile(file)), buffer_(INPUT) {
        name_   = file.filename().string();
        stream_ = ZSTD_createDStream();
        if (!stream_) throw std::runtime_error("out of memory");
        // Lichess's files are made with long windows: up to 1 GB taken, more refused.
        ZSTD_DCtx_setParameter(stream_, ZSTD_d_windowLogMax, 30);
    }
    ~ZstdSource() override { ZSTD_freeDStream(stream_); }
    ZstdSource(const ZstdSource&)            = delete;
    ZstdSource& operator=(const ZstdSource&) = delete;

    std::size_t read(char* out, std::size_t size) override {
        ZSTD_outBuffer output{out, size, 0};
        while (output.pos == 0) {
            if (input_.pos == input_.size) {
                in_.read(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
                const auto n = static_cast<std::size_t>(in_.gcount());
                if (n == 0) {
                    if (pending_ != 0) throw std::runtime_error(name_ + ": the file is cut short");
                    return 0;
                }
                consumed_ += n;
                input_ = ZSTD_inBuffer{buffer_.data(), n, 0};
            }
            pending_ = ZSTD_decompressStream(stream_, &output, &input_);
            if (ZSTD_isError(pending_)) throw std::runtime_error(name_ + ": " + ZSTD_getErrorName(pending_));
        }
        return output.pos;
    }
    [[nodiscard]] uint64_t consumed() const override { return consumed_; }

   private:
    std::ifstream in_;
    std::vector<char> buffer_;
    ZSTD_DStream* stream_ = nullptr;
    ZSTD_inBuffer input_{nullptr, 0, 0};
    std::size_t pending_ = 0;  // what the decoder still waits for: 0 at the end of a frame
    uint64_t consumed_   = 0;
};

// Deflate data: gzip members one after the other (raw = false), or a zip entry's raw deflate limited to `limit` bytes
// of the file from where the stream stands.
class InflateSource : public ByteSource {
   public:
    InflateSource(const fs::path& file, std::string name, bool raw, uint64_t offset, uint64_t limit)
        : in_(openFile(file)), buffer_(INPUT), raw_(raw), left_(limit) {
        name_ = std::move(name);
        in_.seekg(static_cast<std::streamoff>(offset));
        if (inflateInit2(&z_, raw ? -15 : 15 + 32) != Z_OK) throw std::runtime_error("out of memory");
    }
    ~InflateSource() override { inflateEnd(&z_); }
    InflateSource(const InflateSource&)            = delete;
    InflateSource& operator=(const InflateSource&) = delete;

    std::size_t read(char* out, std::size_t size) override {
        if (done_) return 0;
        z_.next_out     = reinterpret_cast<Bytef*>(out);
        z_.avail_out    = static_cast<uInt>(std::min<std::size_t>(size, 1u << 30));
        const auto room = z_.avail_out;
        while (z_.avail_out == room) {
            if (z_.avail_in == 0) {
                const auto want = static_cast<std::size_t>(std::min<uint64_t>(buffer_.size(), left_));
                in_.read(buffer_.data(), static_cast<std::streamsize>(want));
                const auto n = static_cast<std::size_t>(in_.gcount());
                left_ -= n;
                consumed_ += n;
                if (n == 0) {
                    if (!ended_) throw std::runtime_error(name_ + ": the file is cut short");
                    done_ = true;
                    break;
                }
                z_.next_in  = reinterpret_cast<Bytef*>(buffer_.data());
                z_.avail_in = static_cast<uInt>(n);
            }
            if (ended_) {
                // Another gzip member after the one that ended.
                if (raw_) {
                    done_ = true;
                    break;
                }
                inflateReset(&z_);
                ended_ = false;
            }
            const int ret = inflate(&z_, Z_NO_FLUSH);
            if (ret == Z_STREAM_END) {
                ended_ = true;
                if (raw_) {
                    done_ = true;
                    break;
                }
            } else if (ret != Z_OK && ret != Z_BUF_ERROR) {
                throw std::runtime_error(name_ + ": " + (z_.msg ? z_.msg : "damaged deflate data"));
            }
        }
        return room - z_.avail_out;
    }
    [[nodiscard]] uint64_t consumed() const override { return consumed_; }

   private:
    std::ifstream in_;
    std::vector<char> buffer_;
    z_stream z_{};
    bool raw_          = false;
    bool ended_        = false;
    bool done_         = false;
    uint64_t left_     = 0;
    uint64_t consumed_ = 0;
};

class StoredSource : public ByteSource {
   public:
    StoredSource(const fs::path& file, std::string name, uint64_t offset, uint64_t size)
        : in_(openFile(file)), left_(size) {
        name_ = std::move(name);
        in_.seekg(static_cast<std::streamoff>(offset));
    }
    std::size_t read(char* out, std::size_t size) override {
        const auto want = static_cast<std::size_t>(std::min<uint64_t>(size, left_));
        in_.read(out, static_cast<std::streamsize>(want));
        const auto n = static_cast<std::size_t>(in_.gcount());
        left_ -= n;
        consumed_ += n;
        if (n < want) throw std::runtime_error(name_ + ": the file is cut short");
        return n;
    }
    [[nodiscard]] uint64_t consumed() const override { return consumed_; }

   private:
    std::ifstream in_;
    uint64_t left_     = 0;
    uint64_t consumed_ = 0;
};

uint64_t le(const unsigned char* p, int bytes) {
    uint64_t v = 0;
    for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

bool endsWithPgn(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name.size() > 4 && name.compare(name.size() - 4, 4, ".pgn") == 0;
}

// The PGN files of a zip archive, by its central directory (zip64 included).
std::vector<std::unique_ptr<ByteSource>> zipSources(const fs::path& file) {
    auto in         = openFile(file);
    const auto size = fs::file_size(file);
    const auto tail = std::min<uint64_t>(size, 65536 + 22);
    std::vector<unsigned char> end(static_cast<std::size_t>(tail));
    in.seekg(static_cast<std::streamoff>(size - tail));
    in.read(reinterpret_cast<char*>(end.data()), static_cast<std::streamsize>(tail));
    std::size_t at = std::string::npos;
    for (std::size_t i = end.size() >= 22 ? end.size() - 22 + 1 : 0; i-- > 0;) {
        if (le(&end[i], 4) == 0x06054b50) {
            at = i;
            break;
        }
    }
    if (at == std::string::npos) throw std::runtime_error(file.filename().string() + ": not a zip archive");
    uint64_t entries   = le(&end[at + 10], 2);
    uint64_t dir_size  = le(&end[at + 12], 4);
    uint64_t dir_start = le(&end[at + 16], 4);
    // zip64: the locator just before, and its record.
    if (at >= 20 && le(&end[at - 20], 4) == 0x07064b50) {
        const auto record = le(&end[at - 20 + 8], 8);
        std::array<unsigned char, 56> z{};
        in.seekg(static_cast<std::streamoff>(record));
        in.read(reinterpret_cast<char*>(z.data()), z.size());
        if (in && le(z.data(), 4) == 0x06064b50) {
            entries   = le(&z[32], 8);
            dir_size  = le(&z[40], 8);
            dir_start = le(&z[48], 8);
        }
    }
    if (dir_size > (uint64_t{1} << 31)) throw std::runtime_error(file.filename().string() + ": damaged zip directory");
    std::vector<unsigned char> dir(static_cast<std::size_t>(dir_size));
    in.seekg(static_cast<std::streamoff>(dir_start));
    in.read(reinterpret_cast<char*>(dir.data()), static_cast<std::streamsize>(dir_size));
    if (!in) throw std::runtime_error(file.filename().string() + ": damaged zip directory");

    std::vector<std::unique_ptr<ByteSource>> out;
    std::size_t p = 0;
    for (uint64_t e = 0; e < entries && p + 46 <= dir.size(); ++e) {
        if (le(&dir[p], 4) != 0x02014b50) break;
        const auto method      = le(&dir[p + 10], 2);
        uint64_t compressed    = le(&dir[p + 20], 4);
        uint64_t uncompressed  = le(&dir[p + 24], 4);
        const auto name_length = le(&dir[p + 28], 2), extra_length = le(&dir[p + 30], 2),
                   note_length = le(&dir[p + 32], 2);
        uint64_t local         = le(&dir[p + 42], 4);
        if (p + 46 + name_length + extra_length > dir.size()) break;
        const std::string name(reinterpret_cast<const char*>(&dir[p + 46]), static_cast<std::size_t>(name_length));
        // The zip64 extra field holds what did not fit in 32 bits, in this order.
        for (std::size_t x = p + 46 + name_length; x + 4 <= p + 46 + name_length + extra_length;) {
            const auto id = le(&dir[x], 2), length = le(&dir[x + 2], 2);
            if (id == 0x0001) {
                std::size_t v   = x + 4;
                const auto take = [&](uint64_t& field) {
                    if (field != 0xFFFFFFFF || v + 8 > x + 4 + length) return;
                    field = le(&dir[v], 8);
                    v += 8;
                };
                take(uncompressed);
                take(compressed);
                take(local);
            }
            x += 4 + length;
        }
        p += 46 + name_length + extra_length + note_length;
        if (!endsWithPgn(name)) continue;
        // The entry's data: after its local header, whose name and extra field may differ from the directory's.
        std::array<unsigned char, 30> header{};
        in.seekg(static_cast<std::streamoff>(local));
        in.read(reinterpret_cast<char*>(header.data()), header.size());
        if (!in || le(header.data(), 4) != 0x04034b50)
            throw std::runtime_error(file.filename().string() + ": damaged zip entry " + name);
        const auto data = local + 30 + le(&header[26], 2) + le(&header[28], 2);
        if (method == 0)
            out.push_back(std::make_unique<StoredSource>(file, name, data, compressed));
        else if (method == 8)
            out.push_back(std::make_unique<InflateSource>(file, name, true, data, compressed));
        else
            throw std::runtime_error(name + ": a compression this program does not read (method " +
                                     std::to_string(method) + ")");
        (void)uncompressed;
    }
    return out;
}

}  // namespace

std::vector<std::unique_ptr<ByteSource>> openSources(const fs::path& file) {
    std::array<unsigned char, 4> magic{};
    {
        auto in = openFile(file);
        in.read(reinterpret_cast<char*>(magic.data()), magic.size());
    }
    std::vector<std::unique_ptr<ByteSource>> out;
    // A zstd frame, or a skippable frame before one: Lichess's files begin with one (pzstd's, which gives the
    // size of each frame for decoding them in parallel).
    const auto first = le(magic.data(), 4);
    if (first == 0xFD2FB528 || (first & 0xFFFFFFF0) == 0x184D2A50) {
        out.push_back(std::make_unique<ZstdSource>(file));
    } else if (magic[0] == 0x1F && magic[1] == 0x8B) {
        out.push_back(std::make_unique<InflateSource>(file, file.filename().string(), false, 0, ~uint64_t{0}));
    } else if (le(magic.data(), 4) == 0x04034b50 || le(magic.data(), 4) == 0x06054b50) {
        out = zipSources(file);
    } else {
        out.push_back(std::make_unique<PlainSource>(file));
    }
    return out;
}

}  // namespace fcg::study
