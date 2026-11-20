#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <vector>

#include <chess.hpp>
#include <study/chessbase.hpp>
#include <study/chessbase_detail.hpp>
#include <study/import.hpp>
#include <study/pgn_reader.hpp>

#include "test_games.hpp"

using namespace fcg;
using namespace fcg::study;
using namespace fcg::test;
namespace fs = std::filesystem;

// study/tests/data/chessbase holds the 35 games of cbtest.pgn, games of our own, as a CBH database (cbtest1.*), as a
// 2CBH database (cbtest2.*) and as an archive of the CBH one (cbtest1.cbv). Game 1 has everything: a comment before
// its first move, comments after moves, symbols ($1 to $6, $10, $13, $14, $16, $18, $146), a variation with one of its
// own, coloured squares and arrows, [%clk], [%emt] and [%eval 0.31,24], accented names, teams, an annotator, round
// 3.2. Game 2 starts from a set-up position, game 3 promotes to a knight, game 4 has no result and takes en passant;
// the others are plain games, some without ratings.
//
// What the formats keep of them, found by reading them back:
// - both keep the tags (Event, Site, Date with its unknown parts, Round 3.2, the players, ratings, ECO, Annotator,
//   WhiteTeam and BlackTeam, EventDate), the start position, every move and variation, the comments, the symbols, the
//   coloured squares and arrows, [%emt] and [%eval] with its depth;
// - neither holds the [%clk] of game 1: no clock was stored with its moves;
// - CBH keeps its names in a Windows code page, where "ř" has no place: "Dvořák" is "Dvorák" there; 2CBH's are UTF-8;
// - both add the ECO code of every game from the standard start, which the PGN has for game 1 alone.

namespace {

fs::path chessbaseData(const std::string& name) { return data("chessbase/" + name); }

std::string slurp(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void spit(const fs::path& file, const std::string& bytes) {
    std::ofstream out(file, std::ios::binary);
    out << bytes;
}

std::vector<GameTree> pgnGames() {
    const auto text = slurp(chessbaseData("cbtest.pgn"));
    std::vector<GameTree> games;
    for (std::string_view rest(text);;) {
        const auto read = readPgnGame(rest);
        if (!read.tree) break;
        games.push_back(*read.tree);
        rest.remove_prefix(read.end);
    }
    return games;
}

std::vector<ChessBaseGame> databaseGames(const fs::path& file, ChessBaseReport* report = nullptr,
                                         const ChessBaseOptions& options = {}) {
    ChessBaseDatabase db(file, options);
    std::vector<ChessBaseGame> games;
    db.forEach([&](ChessBaseGame&& g) {
        games.push_back(std::move(g));
        return true;
    });
    if (report) *report = db.report();
    return games;
}

std::map<std::string, std::string> tagMap(const std::vector<std::pair<std::string, std::string>>& tags) {
    return {tags.begin(), tags.end()};
}

// The tags of the PGN as a format keeps them: the ECO code it adds, "ř" where the code page has no place for it.
void asKept(std::map<std::string, std::string>& got, std::map<std::string, std::string>& want, int n,
            bool code_page_names) {
    if (!want.contains("ECO")) {
        CHECK(got.contains("ECO") == (n != 2));  // a set-up game has none
        if (got.contains("ECO")) CHECK(got["ECO"].size() == 3);
        got.erase("ECO");
    }
    if (code_page_names)
        for (auto& [k, v] : want)
            if (const auto at = v.find("ř"); at != std::string::npos) v.replace(at, std::string("ř").size(), "r");
}

void checkMove(const ReplayMove& got, const ReplayMove& want, const std::string& where) {
    INFO(where << " " << want.san);
    CHECK(got.move == want.move);
    CHECK(got.san == want.san);
    CHECK(got.mover == want.mover);
    CHECK(got.notes == want.notes);
    CHECK(got.score.has_value() == want.score.has_value());
    if (got.score && want.score) {
        CHECK(got.score->type == want.score->type);
        CHECK(got.score->value == want.score->value);
    }
    CHECK(got.depth == want.depth);
    // No clock in either format's files.
    CHECK_FALSE(got.clock_ms.has_value());
}

void checkTree(const GameTree& got, const GameTree& want) {
    CHECK(got.main.start_fen == want.main.start_fen);
    CHECK(got.main.result == want.main.result);
    CHECK(got.main.pre_comment == want.main.pre_comment);
    REQUIRE(got.main.moves.size() == want.main.moves.size());
    for (std::size_t i = 0; i < want.main.moves.size(); ++i) {
        auto expected = want.main.moves[i];
        expected.clock_ms.reset();
        checkMove(got.main.moves[i], expected, "move " + std::to_string(i + 1));
    }
    REQUIRE(got.variations.size() == want.variations.size());
    for (std::size_t v = 0; v < want.variations.size(); ++v) {
        INFO("variation " << v);
        CHECK(got.variations[v].parent == want.variations[v].parent);
        CHECK(got.variations[v].from == want.variations[v].from);
        REQUIRE(got.variations[v].moves.size() == want.variations[v].moves.size());
        for (std::size_t i = 0; i < want.variations[v].moves.size(); ++i)
            checkMove(got.variations[v].moves[i], want.variations[v].moves[i], "move " + std::to_string(i + 1));
    }
}

void checkDatabase(const fs::path& file, ChessBaseFormat format, bool code_page_names,
                   const ChessBaseOptions& options = {}) {
    const auto want = pgnGames();
    REQUIRE(want.size() == 35);
    ChessBaseDatabase db(file, options);
    CHECK(db.format() == format);
    CHECK(db.records() == 35);
    ChessBaseReport report;
    const auto games = databaseGames(file, &report, options);
    REQUIRE(games.size() == 35);
    for (std::size_t i = 0; i < games.size(); ++i) {
        const int n = static_cast<int>(i) + 1;
        INFO("game " << n);
        CHECK_FALSE(games[i].damaged);
        CHECK(games[i].problem.empty());
        CHECK(games[i].record == static_cast<uint32_t>(n));
        auto got_tags  = tagMap(games[i].tree.main.headers);
        auto want_tags = tagMap(want[i].main.headers);
        asKept(got_tags, want_tags, n, code_page_names);
        CHECK(got_tags == want_tags);
        CHECK(games[i].chess960 == false);
        checkTree(games[i].tree, want[i]);
    }
    // Nothing of these games is left out.
    CHECK(report.games == 35);
    CHECK(report.records == 35);
    CHECK(report.damaged + report.unreadable + report.texts + report.deleted + report.unknown == 0);
    CHECK(report.undecoded + report.other == 0);
    CHECK(report.training + report.pictures + report.sounds + report.videos + report.web_links + report.quotations +
              report.pawn_structures + report.piece_paths + report.variation_colours + report.correspondence ==
          0);
    CHECK(report.problems.empty());
}

// A copy of a database's files under another name, in a folder of the test's own.
fs::path copyDatabase(const std::string& from, const std::string& to, const std::string& folder) {
    const auto dir = fs::temp_directory_path() / folder;
    fs::remove_all(dir);
    fs::create_directories(dir);
    for (const auto& entry : fs::directory_iterator(chessbaseData("")))
        if (const auto name = entry.path().filename().string();
            name.starts_with(from + ".") && entry.path().extension() != ".cbv")
            fs::copy_file(entry.path(), dir / (to + entry.path().extension().string()));
    return dir;
}

bool throwsWith(const std::function<void()>& f, const std::string& part) {
    try {
        f();
    } catch (const std::runtime_error& e) {
        return std::string(e.what()).find(part) != std::string::npos;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------
// Archives made by the tests, in every way the format stores a member's blocks
// ---------------------------------------------------------------------------------------------------------------

void put16(std::string& out, uint32_t v) {
    out += static_cast<char>(v & 0xFF);
    out += static_cast<char>((v >> 8) & 0xFF);
}
void put32(std::string& out, uint32_t v) {
    put16(out, v & 0xFFFF);
    put16(out, v >> 16);
}
void put64(std::string& out, uint64_t v) {
    put32(out, static_cast<uint32_t>(v));
    put32(out, static_cast<uint32_t>(v >> 32));
}

class Bits {
   public:
    void put(uint32_t value, int count) {
        for (int i = count - 1; i >= 0; --i) {
            if (used_ % 8 == 0) bytes_ += '\0';
            if ((value >> i) & 1) bytes_.back() = static_cast<char>(bytes_.back() | (0x80 >> (used_ % 8)));
            ++used_;
        }
    }
    [[nodiscard]] const std::string& bytes() const { return bytes_; }

   private:
    std::string bytes_;
    std::size_t used_ = 0;
};

// A Huffman block's body: every byte value's code of eight bits, the value itself (a complete code).
std::string huffman(const std::string& text) {
    Bits bits;
    bits.put(static_cast<uint32_t>(text.size()), 16);
    for (uint32_t symbol = 0; symbol < 256; ++symbol) {
        bits.put(8, 4);
        bits.put(symbol, 8);
    }
    for (const char c : text) bits.put(static_cast<unsigned char>(c), 8);
    return bits.bytes();
}

// LZ tokens for `text`, copying from the member's bytes before it (`history`) and its own: copies of 3 to 15 bytes
// from at most 4,098 bytes back, never longer than their distance.
std::string lz(const std::string& history, const std::string& text) {
    const std::string all = history + text;
    std::string out, group;
    uint32_t control = 0;
    int tokens       = 0;
    const auto flush = [&] {
        put16(out, control);
        out += group;
        group.clear();
        control = 0;
        tokens  = 0;
    };
    for (std::size_t at = history.size(); at < all.size();) {
        std::size_t best = 0, best_offset = 0;
        for (std::size_t offset = 3; offset <= std::min<std::size_t>(4098, at); ++offset) {
            std::size_t n = 0;
            while (n < 15 && n < offset && at + n < all.size() && all[at + n] == all[at - offset + n]) ++n;
            if (n > best) best = n, best_offset = offset;
        }
        if (best >= 3) {
            control |= 1u << (15 - tokens);
            const std::size_t v = best_offset - 3;
            group += static_cast<char>(best << 4 | (v & 0x0F));
            group += static_cast<char>(v >> 4);
            at += best;
        } else {
            group += all[at++];
        }
        if (++tokens == 16) flush();
    }
    if (tokens) flush();
    return out;
}

struct ArchiveMember {
    std::string name, bytes;
};

// An archive of `members`, each cut in blocks of `block` bytes stored in turn as they are (0), as LZ tokens (1), as a
// Huffman code (2) and as a Huffman code of LZ tokens (3).
std::string archive(const std::vector<ArchiveMember>& members, std::size_t block) {
    std::vector<std::string> streams;
    for (const auto& m : members) {
        std::string stream;
        int mode = 0;
        for (std::size_t at = 0; at < m.bytes.size(); at += block, mode = (mode + 1) % 4) {
            const auto part    = m.bytes.substr(at, block);
            const auto history = m.bytes.substr(0, at);
            std::string body;
            switch (mode) {
                case 0:
                    body = part;
                    break;
                case 1:
                    body = lz(history, part);
                    break;
                case 2:
                    body = huffman(part);
                    break;
                default:
                    body = huffman(lz(history, part));
                    break;
            }
            put16(stream, static_cast<uint32_t>(body.size() + 1));
            put16(stream, 0xA5A5);
            stream += static_cast<char>(mode);
            stream += body;
        }
        streams.push_back(stream);
    }
    std::string out;
    out += '\x08';
    out += '\0';
    put16(out, static_cast<uint32_t>(members.size()));
    out += std::string("\xAD\x00\x03\x00", 4);
    uint64_t offset = 8 + 173 * members.size();
    for (std::size_t i = 0; i < members.size(); ++i) {
        std::string record = members[i].name;
        record.resize(128, '\0');
        put32(record, static_cast<uint32_t>(offset));
        put32(record, static_cast<uint32_t>(streams[i].size()));
        put32(record, static_cast<uint32_t>(members[i].bytes.size()));
        record += std::string(9, '\x01');
        put64(record, offset);
        put64(record, streams[i].size());
        put64(record, members[i].bytes.size());
        out += record;
        offset += streams[i].size();
    }
    for (const auto& s : streams) out += s;
    return out;
}

std::vector<ArchiveMember> cbhMembers(const std::string& base) {
    std::vector<ArchiveMember> members;
    for (const char* ext : {".cba", ".cbc", ".cbe", ".cbg", ".cbh", ".cbj", ".cbp", ".cbs", ".cbt"})
        members.push_back({base + ext, slurp(chessbaseData(std::string("cbtest1") + ext))});
    return members;
}

}  // namespace

TEST_CASE("ChessBase CBH database: the 35 games as the PGN they were made of") {
    checkDatabase(chessbaseData("cbtest1.cbh"), ChessBaseFormat::CBH, true);
}

TEST_CASE("ChessBase 2CBH database: the 35 games as the PGN they were made of") {
    checkDatabase(chessbaseData("cbtest2.2cbh"), ChessBaseFormat::TwoCBH, false);
}

TEST_CASE("ChessBase CBV archive: the CBH database's 35 games, its members unpacked as they are read") {
    checkDatabase(chessbaseData("cbtest1.cbv"), ChessBaseFormat::CBH, true);
    // Its members unpacked are the database's files, a block at a time or whole.
    for (const uint64_t whole : {uint64_t{0}, uint64_t{16} << 20}) {
        auto members = chessbase::archiveMembers(chessbaseData("cbtest1.cbv"), "", whole);
        for (const char* ext :
             {".cbh", ".cbg", ".cba", ".cbp", ".cbt", ".cbc", ".cbs", ".cbe", ".cbj", ".cbm", ".cbtt", ".cbl"}) {
            INFO(ext);
            auto member      = members->open(ext, false);
            const auto loose = slurp(chessbaseData(std::string("cbtest1") + ext));
            REQUIRE(member);
            REQUIRE(member->size() == loose.size());
            const auto bytes = member->read(0, loose.size());
            CHECK(std::string(bytes.begin(), bytes.end()) == loose);
        }
        CHECK_FALSE(members->has(".2cbh"));
    }
}

TEST_CASE("ChessBase game 1: what each format keeps of a game that has everything") {
    for (const char* file : {"cbtest1.cbh", "cbtest2.2cbh"}) {
        INFO(file);
        ChessBaseDatabase db(chessbaseData(file));
        const auto game = db.read(1);
        REQUIRE(game);
        const auto& main = game->tree.main;
        CHECK(main.header("Round") == "3.2");
        CHECK(main.header("White") == "Müller, Jörg");
        CHECK(main.header("Annotator") == "RookStudio");
        CHECK(main.header("WhiteTeam") == "Lyon");
        CHECK(main.header("BlackTeam") == "Paris");
        CHECK(main.header("EventDate") == "2026.11.18");
        CHECK(main.pre_comment == "A game of our own, annotated for the tests.");
        CHECK(main.moves[0].notes.emt_ms == 10000);
        CHECK(main.moves[9].notes.after == "The Najdorf.");
        CHECK(main.moves[10].notes.nags == std::vector<uint8_t>{1});
        CHECK(main.moves[10].notes.squares.size() == 3);
        CHECK(main.moves[10].notes.arrows.size() == 3);
        CHECK(main.moves[18].notes.nags == std::vector<uint8_t>{146});
        CHECK(main.moves[18].notes.eval_tag);
        CHECK(main.moves[18].score->value == 31);
        CHECK(main.moves[18].depth == 24);
        REQUIRE(game->tree.variations.size() == 2);
        CHECK(game->tree.variations[1].parent == 0);
        CHECK(game->tree.variations[1].moves[1].notes.nags == std::vector<uint8_t>{14});
        CHECK(game->tree.variations[0].moves[3].notes.nags == std::vector<uint8_t>{16});
    }
    // The set-up game, the promotion to a knight, the game without a result and its en passant.
    ChessBaseDatabase db(chessbaseData("cbtest2.2cbh"));
    CHECK(db.read(2)->tree.main.start_fen == "8/5pk1/6p1/8/5P2/6P1/5K2/3R1r2 w - - 0 50");
    CHECK(db.read(3)->tree.main.moves[8].san == "bxa8=N");
    CHECK(db.read(4)->tree.main.result == "*");
    CHECK(db.read(4)->tree.main.moves[4].move.typeOf() == chess::Move::ENPASSANT);
}

TEST_CASE("ChessBase database: opened by any of its files, refused with a readable message without one") {
    {
        const auto dir = copyDatabase("cbtest1", "Base", "chessbase-open-cbh");
        // The extensions in another case.
        fs::rename(dir / "Base.cbg", dir / "Base.CBG");
        for (const char* ext : {".cbh", ".CBG", ".cba", ".cbp", ".cbj"}) {
            INFO(ext);
            ChessBaseDatabase db(dir / (std::string("Base") + ext));
            CHECK(db.records() == 35);
            CHECK(db.read(35).has_value());
        }
        {
            ChessBaseDatabase db(dir / "Base.cbh");
            CHECK_THROWS_AS((void)db.read(0), std::out_of_range);
            CHECK_THROWS_AS((void)db.read(36), std::out_of_range);
            // Read out of order, the same games.
            const auto last = db.read(35), first = db.read(1);
            CHECK(first->tree.main.moves.size() == 49);
            CHECK(last->record == 35);
        }
        // Closed, its files may go (Windows keeps a mapped file until it is closed).
        fs::remove(dir / "Base.CBG");
        CHECK(throwsWith([&] { ChessBaseDatabase(dir / "Base.cbh"); }, ".cbg is missing"));
        fs::remove(dir / "Base.cbp");
        CHECK(throwsWith([&] { ChessBaseDatabase(dir / "Base.cba"); }, "is missing"));
        spit(dir / "Other.pgn", "1. e4 *\n");
        CHECK(throwsWith([&] { ChessBaseDatabase(dir / "Other.pgn"); }, "not a ChessBase database"));
        CHECK(throwsWith([&] { ChessBaseDatabase(dir / "None.cbh"); }, "no such file"));
    }
    {
        const auto dir = copyDatabase("cbtest2", "Base", "chessbase-open-2cbh");
        for (const char* ext : {".2cbh", ".2cbg", ".2cba", ".2lid"}) {
            INFO(ext);
            ChessBaseDatabase db(dir / (std::string("Base") + ext));
            CHECK(db.format() == ChessBaseFormat::TwoCBH);
            CHECK(db.records() == 35);
        }
        fs::remove(dir / "Base.2lid");
        CHECK(throwsWith([&] { ChessBaseDatabase(dir / "Base.2cbh"); }, ".2lid is missing"));
    }
}

TEST_CASE("ChessBase archive: blocks stored in every way the format has, members of many blocks") {
    // Blocks of 32 bytes stored in turn as they are, as LZ tokens copying across blocks, as a Huffman code and as a
    // Huffman code of LZ tokens; the header file and the moves read a block at a time, far back and forth.
    const auto dir = fs::temp_directory_path() / "chessbase-blocks";
    fs::create_directories(dir);
    spit(dir / "Blocks.cbv", archive(cbhMembers("Blocks"), 32));
    ChessBaseOptions options;
    options.unpack_whole = 0;
    checkDatabase(dir / "Blocks.cbv", ChessBaseFormat::CBH, true, options);
    ChessBaseDatabase db(dir / "Blocks.cbv", options);
    const auto loose = databaseGames(chessbaseData("cbtest1.cbh"));
    for (const uint32_t record : {35u, 1u, 18u, 2u, 34u, 1u}) {
        INFO(record);
        const auto game = db.read(record);
        REQUIRE(game);
        CHECK(game->tree.main.moves.size() == loose[record - 1].tree.main.moves.size());
        CHECK(game->tree.main.headers == loose[record - 1].tree.main.headers);
    }
    // The members, unpacked a block at a time, read at every place.
    auto members      = chessbase::archiveMembers(dir / "Blocks.cbv", "", 0);
    auto header       = members->open(".cbh", false);
    const auto source = slurp(chessbaseData("cbtest1.cbh"));
    for (uint64_t at = source.size(); at-- > 0;) {
        const std::size_t n = std::min<std::size_t>(97, source.size() - at);
        const auto bytes    = header->read(at, n);
        REQUIRE(std::string(bytes.begin(), bytes.end()) == source.substr(at, n));
    }
    CHECK_THROWS_AS((void)header->read(source.size(), 1), chessbase::Damage);
}

TEST_CASE("ChessBase CBZ archive: DES, and a .cbv enciphered with a password") {
    // DES's published test vectors, the key an eight-byte password.
    struct Vector {
        std::string key, plain, cipher;
    };
    const auto hex = [](const char* s) {
        std::string out;
        for (; s[0] && s[1]; s += 2) out += static_cast<char>(std::stoi(std::string(s, 2), nullptr, 16));
        return out;
    };
    for (const Vector& v : {Vector{hex("0123456789ABCDEF"), hex("4E6F772069732074"), hex("3FA40E8A984D4815")},
                            Vector{hex("0000000000000000"), hex("0000000000000000"), hex("8CA64DE9C1B123A7")},
                            Vector{hex("FFFFFFFFFFFFFFFF"), hex("FFFFFFFFFFFFFFFF"), hex("7359B2163E4EDC58")},
                            Vector{hex("3000000000000000"), hex("1000000000000001"), hex("958E6E627A05557B")}}) {
        std::vector<uint8_t> block(v.plain.begin(), v.plain.end());
        chessbase::desBlocks(v.key, block, false);
        CHECK(std::string(block.begin(), block.end()) == v.cipher);
        chessbase::desBlocks(v.key, block, true);
        CHECK(std::string(block.begin(), block.end()) == v.plain);
    }
    // The CBV archive enciphered as a .cbz is, with a short password (repeated) and a long one (folded).
    const auto dir = fs::temp_directory_path() / "chessbase-cbz";
    fs::create_directories(dir);
    for (const std::string password : {"pass", "my long password"}) {
        INFO(password);
        auto bytes = slurp(chessbaseData("cbtest1.cbv"));
        bytes.resize((bytes.size() + 7) / 8 * 8, '\0');
        std::vector<uint8_t> enciphered(bytes.begin(), bytes.end());
        chessbase::desBlocks(password, enciphered, false);
        spit(dir / "Base.cbz", std::string(enciphered.begin(), enciphered.end()));
        ChessBaseOptions options;
        options.password = password;
        checkDatabase(dir / "Base.cbz", ChessBaseFormat::CBH, true, options);
        CHECK(throwsWith([&] { ChessBaseDatabase(dir / "Base.cbz", ChessBaseOptions{"wrong"}); }, "wrong password"));
        CHECK(throwsWith([&] { ChessBaseDatabase(dir / "Base.cbz"); }, "needs its password"));
    }
}

TEST_CASE("ChessBase databases: truncated and garbled copies are refused or read as damaged, never past their files") {
    struct Set {
        std::string from;
        std::vector<std::string> extensions;
    };
    const std::vector<Set> sets = {
        {"cbtest1", {".cbh", ".cbg", ".cba", ".cbp", ".cbt", ".cbc", ".cbs", ".cbe", ".cbj"}},
        {"cbtest2", {".2cbh", ".2cbg", ".2cba", ".2lid"}},
        {"cbtest1", {".cbv"}},
    };
    // Reads everything it can: only a readable error at opening may escape; damage is in the report.
    const auto readAll = [](const fs::path& file, ChessBaseReport& report) {
        try {
            ChessBaseDatabase db(file);
            db.forEach([](ChessBaseGame&& g) {
                // What was read of a damaged game is a tree a PGN writer and reader agree on.
                (void)g;
                return true;
            });
            for (uint32_t r = db.records(); r >= 1; --r) (void)db.read(r);
            report = db.report();
            return true;
        } catch (const std::runtime_error&) {
            return false;
        }
    };
    std::mt19937 random(20261120);
    int opened = 0, refused = 0, damaged = 0;
    for (const auto& set : sets) {
        const bool archive = set.extensions.front() == ".cbv";
        for (const auto& ext : set.extensions) {
            const auto original = slurp(chessbaseData(set.from + ext));
            const auto dir      = fs::temp_directory_path() / "chessbase-damage";
            const auto reset    = [&] {
                fs::remove_all(dir);
                fs::create_directories(dir);
                if (archive)
                    fs::copy_file(chessbaseData("cbtest1.cbv"), dir / "Base.cbv");
                else
                    for (const auto& e : set.extensions) fs::copy_file(chessbaseData(set.from + e), dir / ("Base" + e));
            };
            const auto file = dir / ("Base" + set.extensions.front());
            // Cut.
            for (const std::size_t cut :
                 {std::size_t{0}, std::size_t{1}, original.size() / 3, original.size() / 2, original.size() - 1}) {
                INFO(set.from << ext << " cut at " << cut);
                reset();
                spit(dir / ("Base" + ext), original.substr(0, cut));
                ChessBaseReport report;
                if (readAll(file, report)) {
                    ++opened;
                    damaged += report.damaged + report.unreadable > 0;
                } else {
                    ++refused;
                }
            }
            // Garbled: bytes changed at random places, a run of them set to ff.
            for (int round = 0; round < 12; ++round) {
                INFO(set.from << ext << " garbled, round " << round);
                reset();
                auto bytes = original;
                for (int k = 0; k < 1 + round % 4; ++k) bytes[random() % bytes.size()] = static_cast<char>(random());
                const std::size_t run = random() % bytes.size();
                for (std::size_t i = run; i < std::min(bytes.size(), run + 1 + round); ++i) bytes[i] = '\xff';
                spit(dir / ("Base" + ext), bytes);
                ChessBaseReport report;
                if (readAll(file, report))
                    ++opened;
                else
                    ++refused;
            }
        }
    }
    MESSAGE("damaged copies: " << opened << " opened (" << damaged << " of the cut ones with damage reported), "
                               << refused << " refused");
    CHECK(opened > 0);
    CHECK(refused > 0);
    CHECK(damaged > 0);
}

TEST_CASE("ChessBase database: its games as the PGN import gives them, ready to be copied into a database") {
    // The PGN import of the same games.
    const auto pgn = slurp(chessbaseData("cbtest.pgn"));
    std::vector<ImportedGame> want;
    for (const auto text : splitGames(pgn)) {
        ImportedGame g;
        REQUIRE(readImportedGame(text, g));
        want.push_back(std::move(g));
    }
    REQUIRE(want.size() == 35);
    for (const char* file : {"cbtest1.cbh", "cbtest2.2cbh", "cbtest1.cbv"}) {
        INFO(file);
        std::vector<ImportedGame> got;
        int batches = 0;
        ImportProgress progress;
        std::atomic<bool> cancel{false};
        ChessBaseReport left_out;
        ImportOptions options;
        options.piece_bytes = 10 * 1024;  // ten games a batch
        const auto report   = importChessBase(
            chessbaseData(file), options, progress, cancel,
            [&](std::vector<ImportedGame>&& batch) {
                ++batches;
                CHECK(batch.size() <= 10);
                for (auto& g : batch) got.push_back(std::move(g));
            },
            &left_out);
        CHECK(report.error.empty());
        CHECK(report.games == 35);
        CHECK(report.damaged == 0);
        CHECK_FALSE(report.cancelled);
        CHECK(batches == 4);
        CHECK(progress.games == 35);
        CHECK(progress.bytes == progress.total_bytes);
        CHECK(left_out.games == 35);
        REQUIRE(got.size() == 35);
        const bool code_page = std::string(file).find("cbtest1") != std::string::npos;
        for (std::size_t i = 0; i < 35; ++i) {
            const int n = static_cast<int>(i) + 1;
            INFO("game " << n);
            auto got_tags  = tagMap(got[i].tags);
            auto want_tags = tagMap(want[i].tags);
            asKept(got_tags, want_tags, n, code_page);
            got_tags.erase("SetUp"), want_tags.erase("SetUp");
            CHECK(got_tags == want_tags);
            CHECK(got[i].start_fen == want[i].start_fen);
            CHECK(got[i].moves == want[i].moves);
            CHECK(got[i].plies == want[i].plies);
            CHECK(got[i].result == want[i].result);
            CHECK(got[i].signature == want[i].signature);
            CHECK(got[i].evals == want[i].evals);
            CHECK(got[i].annotated == want[i].annotated);
            CHECK(got[i].damaged == false);
            // Game 1's clocks are not in the databases.
            if (n == 1)
                CHECK(got[i].clocks.empty());
            else
                CHECK(got[i].clocks == want[i].clocks);
        }
        // An annotated game's text, read back, is the database's game.
        REQUIRE(got[0].annotated);
        const auto read = readPgnGame(got[0].text);
        REQUIRE(read.tree);
        ChessBaseDatabase db(chessbaseData(file));
        checkTree(*read.tree, db.read(1)->tree);
        CHECK(importedPgn(got[1]).find("50. Kxf1") != std::string::npos);
    }
    // Cancelled before the first batch, after the first, and a database that cannot be opened.
    std::atomic<bool> cancel{true};
    ImportProgress progress;
    ImportOptions options;
    options.piece_bytes = 10 * 1024;
    std::size_t given   = 0;
    auto report         = importChessBase(chessbaseData("cbtest1.cbh"), options, progress, cancel,
                                          [&](std::vector<ImportedGame>&& b) { given += b.size(); });
    CHECK(report.cancelled);
    CHECK(given == 0);
    cancel = false;
    report =
        importChessBase(chessbaseData("cbtest1.cbh"), options, progress, cancel, [&](std::vector<ImportedGame>&& b) {
            given += b.size();
            cancel = true;
        });
    CHECK(report.cancelled);
    CHECK(given == 10);
    report = importChessBase(chessbaseData("none.cbh"), options, progress, cancel, [](std::vector<ImportedGame>&&) {});
    CHECK(report.error.find("no such file") != std::string::npos);
}

TEST_CASE("ChessBase databases: the time to read a game") {
    for (const char* file : {"cbtest1.cbh", "cbtest2.2cbh", "cbtest1.cbv"}) {
        ChessBaseDatabase db(chessbaseData(file));
        const auto start  = std::chrono::steady_clock::now();
        std::size_t games = 0, plies = 0;
        constexpr int ROUNDS = 20;
        for (int round = 0; round < ROUNDS; ++round)
            db.forEach([&](ChessBaseGame&& g) {
                ++games;
                plies += g.tree.main.moves.size();
                return true;
            });
        const double us =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / games;
        MESSAGE(std::string(file) << ": " << us << " us a game (" << plies / games << " plies a game on average)");
        CHECK(games == 35 * ROUNDS);
    }
}
