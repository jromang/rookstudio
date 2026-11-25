// A ChessBase archive someone gives the program to import into a database, read as importInto() reads it: a CBV (the
// older format's archive, a CBH database inside) or a 2CBV (a zip of a 2CBH database), told by its first bytes. Each
// game read is written as PGN as a database's game is (importedPgn()) and given its key for the duplicates.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <study/chessbase.hpp>
#include <study/import.hpp>
#include <study/store.hpp>

#include "fuzz_file.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    const bool zip  = size >= 2 && data[0] == 'P' && data[1] == 'K';
    const auto file = fcg::fuzz::inputFile(data, size, zip ? ".2cbv" : ".cbv");
    fcg::study::ImportProgress progress;
    const std::atomic<bool> cancel{false};
    fcg::study::ChessBaseReport report;
    (void)fcg::study::importChessBase(
        file, fcg::study::ImportOptions{1, 4096}, progress, cancel,
        [](std::vector<fcg::study::ImportedGame>&& batch) {
            for (const auto& game : batch) {
                (void)fcg::study::importedPgn(game);
                (void)fcg::study::duplicateKey(game);
            }
        },
        &report);
    return 0;
}
