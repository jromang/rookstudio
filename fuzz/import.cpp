// A file someone gives the program to import into a database, read as importInto() reads it: PGN, plain or compressed
// (zstd with or without skippable frames, gzip, a zip of PGN files, 7z), told by its first bytes whatever its name.
// Each game read is written as PGN as a database's game is (importedPgn()) and given its key for the duplicates.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <study/import.hpp>
#include <study/store.hpp>

#include "fuzz_file.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    const auto file = fcg::fuzz::inputFile(data, size, ".pgn");
    fcg::study::ImportProgress progress;
    const std::atomic<bool> cancel{false};
    (void)fcg::study::importGames(file, fcg::study::ImportOptions{1, 4096}, progress, cancel,
                                  [](std::vector<fcg::study::ImportedGame>&& batch) {
                                      for (const auto& game : batch) {
                                          (void)fcg::study::importedPgn(game);
                                          (void)fcg::study::duplicateKey(game);
                                      }
                                  });
    return 0;
}
