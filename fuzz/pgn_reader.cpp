// A game's PGN from anyone (a file opened on the Board page, a database's import), read by the study layer's reader:
// up to four games of the input, each written, read back and written again, the second text the same as the first.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include <study/pgn_reader.hpp>
#include <study/pgn_writer.hpp>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    std::size_t at = 0;
    for (int games = 0; games < 4 && at < text.size(); ++games) {
        const auto read = fcg::study::readPgnGame(text.substr(at));
        if (read.end == 0) break;
        at += read.end;
        if (!read.tree) continue;
        const auto first = fcg::study::writePgnGame(*read.tree, {read.chess960, {}});
        const auto again = fcg::study::readPgnGame(first);
        if (!again.tree) std::abort();
        if (fcg::study::writePgnGame(*again.tree, {again.chess960, {}}) != first) std::abort();
    }
    return 0;
}
