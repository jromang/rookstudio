#pragma once

// Where games come from: a PGN file as it is, compressed with zstd (.zst) or gzip (.gz), or the PGN files of a zip
// archive (.zip, zip64 included) or of a 7z archive (.7z). Each is read as a stream of text, whatever its size: a month of Lichess games is
// 30 GB compressed.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fcg::study {

class ByteSource {
   public:
    virtual ~ByteSource() = default;

    // Up to `size` bytes of text into `out`; 0 at the end. Throws std::runtime_error on a damaged or cut file.
    virtual std::size_t read(char* out, std::size_t size) = 0;
    // The bytes of the file read so far (compressed ones for a compressed file), for a progress bar.
    [[nodiscard]] virtual uint64_t consumed() const = 0;
    // The file, or the archive's file, it reads.
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

   protected:
    std::string name_;
};

// The sources of a file: one, or one for each PGN file of a zip archive, in the archive's order. The kind is told by
// the first bytes, whatever the file's name. Throws std::runtime_error when it cannot be opened or read.
[[nodiscard]] std::vector<std::unique_ptr<ByteSource>> openSources(const std::filesystem::path& file);

// The files of a zip archive whose names `wanted` takes (directories end with '/'), in the archive's order, read as
// openSources() reads its PGN files: for other files of an archive, an engine's program in its release's. Throws
// std::runtime_error when the file is not a zip archive or cannot be read.
[[nodiscard]] std::vector<std::unique_ptr<ByteSource>> zipFiles(
    const std::filesystem::path& file, const std::function<bool(std::string_view name)>& wanted);

// One file of a zip archive, as its central directory lists it.
struct ZipEntry {
    std::string name;
    uint32_t method     = 0;  // 0 stored, 8 deflated
    uint64_t compressed = 0;
    uint64_t size       = 0;
    uint64_t header     = 0;  // where its local header starts
    uint32_t crc        = 0;  // its data's CRC-32
};
// An archive's bytes from an offset: fewer at its end.
using ArchiveBytes = std::function<std::vector<unsigned char>(uint64_t offset, std::size_t size)>;
// A zip archive's files by its central directory (zip64 included), `size` bytes read through `read`: a file on the
// disk, or one deciphered as it is read (a ChessBase .2cbz). `label` names it in the messages. Throws
// std::runtime_error when it is not a zip archive or its directory is damaged.
[[nodiscard]] std::vector<ZipEntry> zipEntries(uint64_t size, const ArchiveBytes& read, const std::string& label);
// Where an entry's data starts: after its local header, whose name and extra field may differ from the directory's.
// Throws std::runtime_error when its local header is not one.
[[nodiscard]] uint64_t zipData(const ZipEntry& entry, const ArchiveBytes& read, const std::string& label);

// A zstd stream decoded as its bytes come, a download read while it arrives: compressed bytes in, text out. It tells
// where its frames end, so that a download cut short can go on from the last one: Lichess's big files are made of
// frames of a few megabytes (pzstd's), each decoded without the ones before it.
class ZstdStream {
   public:
    ZstdStream();
    ~ZstdStream();
    ZstdStream(const ZstdStream&)            = delete;
    ZstdStream& operator=(const ZstdStream&) = delete;

    // `in` decoded, its text given to `out` a piece at a time; false when `out` asked to stop. After the last text of
    // each frame, `frame_end` is given the number of `in`'s bytes read up to the frame's end. Throws
    // std::runtime_error on bytes that are not zstd's.
    bool feed(std::string_view in, const std::function<bool(std::string_view text)>& out,
              const std::function<void(std::size_t read)>& frame_end = {});
    // Ready for a frame's first byte, forgetting the frame begun: to go on from a frame's start.
    void restart();
    // True between two frames: a stream cut there lost nothing.
    [[nodiscard]] bool betweenFrames() const noexcept { return between_; }

   private:
    struct Decoder;
    std::unique_ptr<Decoder> decoder_;
    std::vector<char> buffer_;
    bool between_ = true;
};

}  // namespace fcg::study
