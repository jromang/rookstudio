#include "import_into.hpp"

#include <iterator>

namespace fcg::study {

DatabaseImport importInto(Database& db, const std::filesystem::path& file, const ImportOptions& options,
                          ImportProgress& progress, std::atomic<bool>& cancel, bool keep_duplicates,
                          std::size_t commit_games, const ChessBaseOptions& chessbase,
                          const std::vector<uint64_t>& forget) {
    DatabaseImport out;
    std::vector<ImportedGame> pending;
    const auto u8 = file.u8string();
    const std::string source(u8.begin(), u8.end());
    const auto commit = [&] {
        if (pending.empty() || !out.error.empty()) return;
        try {
            const auto appended = db.append(std::move(pending), keep_duplicates, source, forget);
            out.added += appended.added;
            out.duplicates += appended.duplicates;
            out.damaged += appended.damaged;
            ++out.commits;
        } catch (const std::exception& e) {
            out.error      = e.what();
            out.error_code = errorCode(e);
            cancel         = true;  // the rest of the file would have nowhere to go
        }
        pending.clear();
    };
    const auto take = [&](std::vector<ImportedGame>&& batch) {
        if (!out.error.empty()) return;
        for (const auto& game : batch)
            if (game.damaged && out.damaged_lines.size() < 64)
                out.damaged_lines.emplace_back(duplicateKey(game), game.line);
        if (pending.empty()) {
            pending = std::move(batch);
        } else {
            pending.insert(pending.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));
        }
        if (pending.size() >= commit_games) commit();
    };
    if (isChessBase(file)) {
        ChessBaseReport left_out;
        out.read      = importChessBase(file, options, progress, cancel, take, &left_out, chessbase);
        out.chessbase = std::move(left_out);
    } else {
        out.read = importGames(file, options, progress, cancel, take);
    }
    commit();
    return out;
}

}  // namespace fcg::study
