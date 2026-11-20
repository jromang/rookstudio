#pragma once

// The parts of the ChessBase reader (chessbase.hpp) its formats share: the files of a database, read at positions
// whether they lie beside each other or in an archive; the integers and texts of their records; a game's tree as its
// moves are walked; and the notes of a game before they are placed on its moves.
//
// Partly ported from cbvault 0.1.6, MIT License, Copyright (c) 2026 cbvault Contributors: the text of names and
// comments (codepage.rs, cbh/bytes.rs, pgn/comments.rs). cbvault's readers of the older format are themselves ported
// from oschess-cb-bridge (MIT License, Copyright (c) 2026 the oschess-cb-bridge contributors).

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <chess.hpp>

#include "chessbase.hpp"
#include "game.hpp"

namespace fcg::study::chessbase {

// Bytes that are not what the format holds there: a record that runs past its file, a size that disagrees with its
// record, a move that is not legal. The message says which file and where.
struct Damage : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Integers at a position of a record whose bounds the caller checked: big-endian in the older format's records,
// little-endian in the newer format's and in name files.
inline uint32_t be16(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 8 | p[1]; }
inline uint32_t be24(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 16 | be16(p + 1); }
inline uint32_t be32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 24 | be24(p + 1); }
inline uint64_t be64(const uint8_t* p) { return static_cast<uint64_t>(be32(p)) << 32 | be32(p + 4); }
inline uint32_t le16(const uint8_t* p) { return static_cast<uint32_t>(p[1]) << 8 | p[0]; }
inline uint32_t le32(const uint8_t* p) { return static_cast<uint32_t>(le16(p + 2)) << 16 | le16(p); }
inline uint64_t le64(const uint8_t* p) { return static_cast<uint64_t>(le32(p + 4)) << 32 | le32(p); }

// One file of a database, read at positions.
class Source {
   public:
    virtual ~Source() = default;
    // The file, as messages name it: its path, or the archive and the member.
    [[nodiscard]] virtual const std::string& name() const noexcept = 0;
    [[nodiscard]] virtual uint64_t size() const noexcept           = 0;
    // `size` bytes from `offset`, valid until the next read of this file. Throws Damage when the file does not hold
    // them all.
    [[nodiscard]] virtual std::span<const uint8_t> read(uint64_t offset, std::size_t size) = 0;

   protected:
    [[noreturn]] void outside(uint64_t offset, std::size_t size) const;
};

// A file mapped in memory; throws std::runtime_error when it cannot be opened.
[[nodiscard]] std::unique_ptr<Source> mappedSource(const std::filesystem::path& file);
// Bytes held in memory, under a name for the messages.
[[nodiscard]] std::unique_ptr<Source> memorySource(std::string name, std::vector<uint8_t> bytes);

// The files of one database, found by their extensions: beside each other, or members of an archive.
class Members {
   public:
    virtual ~Members() = default;
    // The database as messages name it.
    [[nodiscard]] virtual std::string label() const = 0;
    // The file with `extension` (".cbg"), none when the database has no such file. `random`: read anywhere, often (a
    // file of names), rather than mostly in order.
    [[nodiscard]] virtual std::unique_ptr<Source> open(std::string_view extension, bool random) = 0;
    [[nodiscard]] virtual bool has(std::string_view extension) const                            = 0;
};

// The files of the database whose files share `stem`'s path, their extensions in any case.
[[nodiscard]] std::unique_ptr<Members> folderMembers(const std::filesystem::path& stem);
// The database in the archive `file` (.cbv, or .cbz deciphered with `password`), its members unpacked as they are
// read: whole up to `whole` bytes. Throws std::runtime_error with a readable message when the archive is damaged or
// holds no database.
[[nodiscard]] std::unique_ptr<Members> archiveMembers(const std::filesystem::path& file, const std::string& password,
                                                      uint64_t whole);

// DES in ECB mode over `data`'s whole blocks of 8 bytes, the key made of `password` as a .cbz's is.
void desBlocks(const std::string& password, std::span<uint8_t> data, bool decrypt);

// `path` with `extension` appended, matched in any case among the files of its folder; the exact name when none
// matches.
[[nodiscard]] std::filesystem::path sibling(const std::filesystem::path& stem, std::string_view extension);

// A fixed-size name field as UTF-8: up to its first zero byte, UTF-8 when the bytes are (a character cut at the
// field's end left out), else Windows-1252.
[[nodiscard]] std::string nameText(std::span<const uint8_t> field);
// A comment's text as UTF-8, its braces made parentheses, its runs of spaces and line ends one space, the diagram
// marker "[#]", the figurines of a text in a code page their letters. `own_language`: a text of a language of its
// own, where the byte 4 is the diagram.
[[nodiscard]] std::string commentText(std::span<const uint8_t> bytes, bool own_language);

// The language numbers of a text: English, German, French... and any language.
inline constexpr uint16_t ENGLISH      = 0;
inline constexpr uint16_t ANY_LANGUAGE = 7;

// What an annotation is, by its type code.
enum class NoteKind : uint8_t {
    Text,             // a comment after its move (02)
    TextBefore,       // a comment before its move (82)
    Symbols,          // up to three NAGs (03)
    Squares,          // coloured squares (04)
    Arrows,           // coloured arrows (05)
    TimeSpent,        // the time a move took (07)
    ClockWhite,       // White's clock (16)
    ClockBlack,       // Black's clock (17)
    Critical,         // a critical position (18)
    Evaluation,       // an engine's evaluation of a move (21)
    Medal,            // medals (22)
    TimeControl,      // the game's time control (24)
    Evaluations,      // the main line's evaluations (26)
    Training,         // a training question (09)
    Sound,            // 0a
    Picture,          // 0b
    Quotation,        // a game quoted (13)
    PawnStructure,    // 14
    PiecePath,        // 15
    Correspondence,   // a correspondence move's dates (19)
    WebLink,          // 1c
    Video,            // 20 and 25
    VariationColour,  // 23
    Other,            // a type with no name here
};
[[nodiscard]] NoteKind noteKind(uint16_t code) noexcept;

// One annotation of a game, decoded.
struct Note {
    int32_t position  = -1;  // the move it belongs to, counted as the format counts; -1 the game as a whole
    NoteKind kind     = NoteKind::Other;
    uint16_t language = ANY_LANGUAGE;  // a text's
    std::string text;                  // a text, as UTF-8; a time control as PGN writes it
    std::array<uint8_t, 3> nags{};     // in the order PGN writes them, 0 for none
    std::vector<BoardMark> marks;      // squares or arrows
    int64_t value = 0;                 // a time in milliseconds, a medal's bits, an evaluation in centipawns or moves
    bool mate     = false;             // an evaluation that is a mate in `value` moves (> 0 White mates)
    int depth     = 0;                 // an evaluation's
    // The main line's evaluations, from its start position on: centipawns for White, mates ±(30000 - moves),
    // INT16_MIN for none; and their depth.
    std::vector<std::pair<int16_t, int>> profile;
    bool decoded = true;  // false: a kind whose layout this reader cannot read here
};

// A game's tree, made as its moves are walked in the order the format stores them: a line's moves, then, when it
// ends, the alternatives met along it, the last met first.
class TreeBuilder {
   public:
    void reset(const chess::Board& start);
    [[nodiscard]] const chess::Board& board() const noexcept { return board_; }
    // Plays `move`, legal in the current position, on the current line.
    void play(chess::Move move);
    // The last move played has an alternative, which the walk takes once the current line ends.
    void branchLast();
    // The current line ends: true when the walk goes on with the alternative met last, false when none is left.
    bool endLine();
    static constexpr std::size_t MAX_MOVES = 100000;  // a tree of more is damaged
    [[nodiscard]] std::size_t pendingCount() const noexcept { return pending_.size(); }

    GameTree tree;
    // Each move in the order it was played: its line (-1 the game) and its index there.
    std::vector<std::pair<int, int>> stored;

   private:
    struct Pending {
        int line = -1;
        int ply  = 0;  // of the move it replaces, counted from the game's start
        chess::Board before;
    };
    std::vector<ReplayMove>& moves(int line);
    chess::Board board_, before_;
    int line_ = -1;
    int base_ = 0;  // the ply of the current line's first move
    std::vector<Pending> pending_;
};

// How a format counts the moves its annotations name.
enum class Order : uint8_t {
    // As they are stored: a line's moves, then, once it has ended, the alternatives met along it, the last met first.
    Stored,
    // As PGN writes them: a move, then the variations that replace it, then the move after it.
    Pgn,
};

// A record of a database, read: a game's tags, its tree, its notes.
struct RawGame {
    std::vector<std::pair<std::string, std::string>> tags;
    std::string fen;  // empty: the standard start
    bool chess960 = false;
    TreeBuilder builder;
    std::vector<Note> notes;
    Order order = Order::Stored;
    std::string problem;  // what could not be read, when something could not
};

enum class RecordKind : uint8_t { Game, Text, Deleted, Unknown };

// The reader of one format, record by record.
class FormatReader {
   public:
    virtual ~FormatReader()                                       = default;
    [[nodiscard]] virtual ChessBaseFormat format() const noexcept = 0;
    [[nodiscard]] virtual uint32_t records() const noexcept       = 0;
    // The size of the header file and where record `record` ends in it: a reading's progress.
    [[nodiscard]] virtual uint64_t headerBytes() const noexcept              = 0;
    [[nodiscard]] virtual uint64_t headerEnd(uint32_t record) const noexcept = 0;
    // Record `record` (from 1) into `out` (reset first). A game whose moves or notes could not all be read keeps what
    // was read and says why in `problem`. Throws Damage when the record itself cannot be read.
    virtual RecordKind read(uint32_t record, RawGame& out) = 0;
};

[[nodiscard]] std::unique_ptr<FormatReader> cbhReader(std::unique_ptr<Members> members);
[[nodiscard]] std::unique_ptr<FormatReader> twoCbhReader(std::unique_ptr<Members> members);

// Tag values both formats write alike.
[[nodiscard]] std::string dateText(uint32_t packed);  // YYYY.MM.DD, ?? for an unknown part
[[nodiscard]] std::string roundText(int round, int subround);
[[nodiscard]] std::string ecoText(uint32_t field);  // A00..E99, empty for none
[[nodiscard]] std::string resultText(uint32_t field);
[[nodiscard]] std::string playerText(const std::string& last, const std::string& first);
// The tags of a record in the order PGN writes them, the empty ones left out (the Seven Tag Roster kept, "?" then).
void addTag(RawGame& game, std::string name, std::string value);

// The legal move of `board` from `from` to `to` (squares rank * 8 + file), with `promotion` (a chess::PieceType, or
// -1); `castle` 1 for the king's side, 2 for the queen's (a castling given by its sides, whatever the squares).
[[nodiscard]] std::optional<chess::Move> legalMove(const chess::Board& board, int from, int to, int promotion,
                                                   int castle = 0);

}  // namespace fcg::study::chessbase
