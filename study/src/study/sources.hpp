#pragma once

// Where games come from: a PGN file as it is, compressed with zstd (.zst) or gzip (.gz), or the PGN files of a zip
// archive (.zip, zip64 included). Each is read as a stream of text, whatever its size: a month of Lichess games is
// 30 GB compressed.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
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

}  // namespace fcg::study
