#pragma once

// A 7z archive's PGN files, read as one stream of text: its folders decoded as they are read (LZMA, LZMA2, or stored),
// nothing unpacked onto the disk, the PGN files given one after the other (a line break between two), the other files
// passed over. Lumbra's Gigabase comes so. Uses the LZMA SDK's reader of the archive's headers (public domain).

#include <filesystem>
#include <memory>

#include "sources.hpp"

namespace fcg::study {

// Throws std::runtime_error with a readable message for a file that is no 7z archive, one compressed in a way this
// reader does not decode (PPMd, BZip2, filters, encryption), or one without a PGN file.
[[nodiscard]] std::unique_ptr<ByteSource> sevenZipSource(const std::filesystem::path& file);

}  // namespace fcg::study
