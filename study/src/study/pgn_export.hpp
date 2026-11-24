#pragma once

// Games leaving a database as PGN (#93, from the Databases page): as they were read, or with "Write the names in the
// games" (mockup M13, board 1: the table of names is the database's, the games stay as they were read until one asks
// for their names to be written at the export): White and Black as the table of names writes them, and the player's
// FIDE id and title in WhiteFideId, BlackFideId, WhiteTitle and BlackTitle when the table knows them.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "store.hpp"

namespace fcg::study {

struct WrittenName {
    std::string name;
    int64_t fide = 0;  // 0: not known
    std::string title;
};

// A game's PGN with its players' tags written so; a side without a name keeps its tags. The rest of the text, its
// other tags in their order included, is as it was.
[[nodiscard]] std::string pgnWithNames(std::string_view pgn, const std::optional<WrittenName>& white,
                                       const std::optional<WrittenName>& black);

// What the table of names says of a game's players: none for a name in no player of the table.
[[nodiscard]] std::optional<WrittenName> tableName(const Snapshot& snapshot, uint32_t name_id);

// A game of the database as PGN, its names written when asked.
[[nodiscard]] std::string exportedPgn(const Snapshot& snapshot, GameId id, bool write_names);

}  // namespace fcg::study
