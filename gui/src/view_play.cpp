// The Play page (#83, mockup M12, board 4): a game against an engine of the library, its strength, its openings and a
// handicap chosen here; the game played on the Board page, its aids hidden until its end ("Show them" marks it), its
// takebacks and hints counted; its end, saved with its settings in its tags, its review, a rematch; a game RookStudio
// closed during, back at the next launch.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <memory>
#include <random>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include <study/ctg.hpp>
#include <study/import.hpp>
#include <study/repertoire.hpp>
#include <study/scan_board.hpp>
#include <tour/pgn_file.hpp>
#include <tour/polyglot.hpp>

#include "app.hpp"
#include "board_text.hpp"
#include "engine_play.hpp"
#include "game_analysis.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "mistakes_page.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

constexpr double RESIGN_SECONDS = 4.0;  // "Resign…" waits so long for Enter or a second click

// A tooltip for the item just drawn, also when it is disabled, its lines wrapped.
void tip(const std::string& text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

ImU32 mix(ImU32 a, ImU32 b, float t) {
    const auto fa = ImGui::ColorConvertU32ToFloat4(a), fb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(fa.x + (fb.x - fa.x) * t, fa.y + (fb.y - fa.y) * t, fa.z + (fb.z - fa.z) * t, 1.0f));
}

void beginTinted(const char* id, ImU32 tone) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BG, tone, 0.08f)));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BD, tone, 0.55f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(14)));
    ImGui::BeginChild(id, ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
}

void endTinted() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

bool primaryButton(const char* label, bool enabled = true) {
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImGui::ColorConvertU32ToFloat4(mix(ui::ACCENT, IM_COL32(255, 255, 255, 255), 0.15f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImGui::ColorConvertU32ToFloat4(mix(ui::ACCENT, IM_COL32(0, 0, 0, 255), 0.15f)));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    const bool clicked = ImGui::Button(label);
    ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
    return clicked;
}

void wrapped(const std::string& text, bool grey = false) {
    if (grey) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    if (grey) ImGui::PopStyleColor();
}

// "· Against an engine · On Lichess": the part shown underlined; true when another is clicked.
bool partLink(const char* label, bool shown) {
    const auto at   = ImGui::GetCursorScreenPos();
    const auto size = ImGui::CalcTextSize(label);
    const bool clicked =
        ImGui::InvisibleButton(fmt::format("##part_{}", label).c_str(), ImVec2(size.x, ImGui::GetFrameHeight()));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && !shown) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    auto* dl      = ImGui::GetWindowDrawList();
    const float y = at.y + (ImGui::GetFrameHeight() - size.y) * 0.5f;
    dl->AddText(ImVec2(at.x, y), ImGui::GetColorU32(shown || hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled), label);
    if (shown)
        dl->AddLine(ImVec2(at.x, y + size.y + px(2)), ImVec2(at.x + size.x, y + size.y + px(2)), ui::ACCENT, px(2));
    return clicked && !shown;
}

// A chip: a colour, a handicap; filled when chosen.
bool chip(const char* label, bool chosen, const char* id) {
    const auto& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(12));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), style.FramePadding.y * 0.8f));
    if (chosen) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BG, ui::ACCENT, 0.30f)));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::CARD_BG));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(ui::CARD_BD));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    const bool clicked = ImGui::Button(fmt::format("{}##{}", label, id).c_str());
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    return clicked;
}

// A card's heading, as a fieldset's legend.
void heading(const char* text) {
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// The PGN tag of a game, set or replaced.
void setTag(ReplayGame& game, const std::string& key, const std::string& value) {
    std::erase_if(game.headers, [&](const auto& h) { return h.first == key; });
    game.headers.emplace_back(key, value);
}

const char* colourName(bool white) { return white ? TR("White") : TR("Black"); }

constexpr std::string_view PLAYER_PREFIX = "Lichess · ";

}  // namespace

// ---------------------------------------------------------------------------
// The choice, the games played

PlaySettings& App::playSettings() {
    if (!play_settings_) play_settings_ = readPlaySettings(storage_.root());
    return *play_settings_;
}

void App::savePlaySettings() {
    if (play_settings_) (void)writePlaySettings(storage_.root(), *play_settings_);
}

const std::vector<PlayedGame>& App::playedGames() {
    if (!played_games_) played_games_ = readPlayedGames(playFolder());
    return *played_games_;
}

std::string App::playLock() const {
    if (liveGameOn())
        return TR(
            "One of your games is being played live on Lichess: no game against an engine meanwhile (fair "
            "play).");
    if (boardSession()) return TR("Once the exercise on the Board is over.");  // a training session (#76, #80–#82)
    if (engine_game_ && !engine_game_->over) return TR("A game against an engine is being played on the Board.");
    if (board_.match && board_.match_state.running) return TR("Once the game played on the board is over.");
    return {};
}

std::string App::playStartFen(const PlaySettings& s, chess::Color engine_side, std::string* why) {
    const bool chess960 = s.from_board && board_.chess960;
    std::string fen     = s.from_board ? boardFen() : std::string(chess::constants::STARTPOS);
    if (s.handicap != Handicap::None) {
        const auto given = handicapFen(fen, s.handicap, engine_side, chess960);
        if (!given) {
            if (why)
                *why = chess960 ? std::string(TR("No handicap in Chess960."))
                                : trf("The engine cannot give {} in this position.", handicapName(s.handicap));
            return {};
        }
        fen = *given;
    }
    if (const auto problem = positionProblem(fen, chess960); !problem.empty()) {
        if (why) *why = problem;
        return {};
    }
    return fen;
}

OpeningBook::Source App::openingSource(const PlaySettings& s, chess::Color engine_side, std::string& text,
                                       std::string& tag, std::string& note, uint64_t& least, bool build) {
    text     = TR("its own");
    tag      = "engine";
    least    = 1;
    auto& sh = shelf();
    switch (s.openings) {
        case Openings::Own:
            return {};
        case Openings::Databases: {
            std::vector<study::TreeBase> bases;
            std::string name;
            for (std::size_t i = 0; i < sh.entries().size(); ++i) {
                const auto& e = sh.entries()[i];
                if (e.source || (!s.database.empty() && e.folder.string() != s.database)) continue;
                if (const auto src = sh.treeSource(i); src && src->tree) {
                    bases.push_back({src->snapshot, src->tree});
                    name = e.name;
                }
            }
            if (bases.empty()) {
                note = TR("No database with its opening tree: the engine plays from the first move.");
                return {};
            }
            text = s.database.empty() ? std::string(TR("the moves of your databases")) : trf("the moves of “{}”", name);
            tag  = s.database.empty() ? std::string("databases") : "database " + name;
            least = OPENING_LEAST;
            if (!build) return {};
            auto moves = study::treeMoves(std::move(bases));
            return [moves](const chess::Board& board) {
                std::vector<OpeningChoice> out;
                study::ScanBoard scan;
                if (!scan.setFen(board.getFen(), board.chess960())) return out;
                const auto answer = moves(scan);
                if (!answer) return out;
                for (const auto& m : answer->moves) {
                    if (m.code == study::TreeMove::END) continue;
                    if (const auto move = study::moveOfCode(board, scan, m.code))
                        out.push_back({*move, m.stats.games, false});
                }
                return out;
            };
        }
        case Openings::Player: {
            for (std::size_t i = 0; i < sh.entries().size(); ++i) {
                const auto& e = sh.entries()[i];
                if (e.source || e.folder.string() != s.player) continue;
                const auto src = sh.treeSource(i);
                if (!src || !src->tree) break;
                const auto player = e.name.rfind(PLAYER_PREFIX, 0) == 0 ? e.name.substr(PLAYER_PREFIX.size()) : e.name;
                text              = trf("the games of {}", player);
                tag               = "player " + player;
                least             = OPENING_LEAST;
                if (!build) return {};
                auto moves = study::playerMoves({src->snapshot, src->tree}, player, engine_side == chess::Color::WHITE);
                return [moves](const chess::Board& board) {
                    std::vector<OpeningChoice> out;
                    study::ScanBoard scan;
                    if (!scan.setFen(board.getFen(), board.chess960())) return out;
                    const auto answer = moves(scan);
                    if (!answer) return out;
                    for (const auto& m : answer->moves) {
                        if (m.code == study::TreeMove::END) continue;
                        if (const auto move = study::moveOfCode(board, scan, m.code))
                            out.push_back({*move, m.stats.games, false});
                    }
                    return out;
                };
            }
            note = TR("The player's games are no longer there: the engine plays from the first move.");
            return {};
        }
        case Openings::Repertoire: {
            auto& reps = repertoires();
            for (std::size_t i = 0; i < reps.entries().size(); ++i) {
                const auto& r = reps.entries()[i];
                if (r.name != s.repertoire) continue;
                if (r.side == engine_side) {
                    note = trf("“{}” prepares the engine's colour: it plays from the first move.", r.name);
                    return {};
                }
                text = trf("your repertoire “{}”", r.name);
                tag  = "repertoire " + r.name;
                if (!build) return {};
                auto map = std::make_shared<const study::Repertoire>(reps.map(i));
                return [map](const chess::Board& board) {
                    std::vector<OpeningChoice> out;
                    study::ScanBoard scan;
                    if (!scan.setFen(board.getFen(), board.chess960())) return out;
                    const auto* at = map->at(scan.key());
                    if (!at || at->own) return out;
                    for (std::size_t k = 0; k < at->moves.size(); ++k)
                        if (const auto move = study::moveOfCode(board, scan, at->moves[k].code))
                            out.push_back({*move, 1, k == 0});
                    return out;
                };
            }
            note = TR("The repertoire is no longer there: the engine plays from the first move.");
            return {};
        }
        case Openings::Book: {
            const auto it =
                std::find_if(books_.begin(), books_.end(), [&](const BookEntry& b) { return b.path == s.book; });
            if (it == books_.end()) {
                note = TR("The book is no longer in your library: the engine plays from the first move.");
                return {};
            }
            text = trf("the book “{}”", it->name);
            tag  = "book " + it->name;
            if (!build) return {};
            try {
                if (study::isCtg(it->path)) {
                    auto ctg = std::make_shared<const study::CtgBook>(it->path);
                    return [ctg](const chess::Board& board) {
                        std::vector<OpeningChoice> out;
                        if (const auto answer = ctg->at(board))
                            for (const auto& m : answer->moves)
                                if (!m.avoid)
                                    out.push_back(
                                        {m.move, m.games ? std::max<uint64_t>(1, m.games->games) : 1, m.main});
                        return out;
                    };
                }
                auto book = std::make_shared<tour::PolyglotBook>(fs::path(it->path));
                return [book](const chess::Board& board) {
                    std::vector<OpeningChoice> out;
                    for (const auto& c : book->moves(board)) out.push_back({c.move, c.weight, false});
                    return out;
                };
            } catch (const std::exception& e) {
                text = TR("its own");
                tag  = "engine";
                note = trf("“{}” cannot be read ({}): the engine plays from the first move.", it->name, e.what());
                return {};
            }
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// The game

bool App::engineGameRunning() const { return engine_game_ && !engine_game_->over; }

bool App::engineGameHidesAids() const { return engine_game_ && !engine_game_->over && !engine_game_->aids; }

BoardMatchSetup App::engineGameSetup(const KeptGame& kept, const EngineEntry& engine, const std::string& me,
                                     std::string* openings_text, std::string* openings_tag) {
    BoardMatchSetup setup;
    const bool white   = kept.white;
    setup.first        = white ? me : engine.name;
    setup.second       = white ? engine.name : me;
    setup.first_clock  = kept.clocks[0];
    setup.second_clock = kept.clocks[1];
    setup.fen          = kept.start_fen;
    setup.chess960     = kept.chess960;
    setup.human        = white ? 0 : 1;
    setup.nodes =
        white ? std::array<uint64_t, 2>{0, kept.strength.nodes} : std::array<uint64_t, 2>{kept.strength.nodes, 0};
    setup.event = "RookStudio game against an engine";
    // The engine's openings, asked from the game's thread.
    std::string text, tag, note;
    uint64_t least = 1;
    if (auto source =
            openingSource(kept.settings, white ? chess::Color::BLACK : chess::Color::WHITE, text, tag, note, least)) {
        std::random_device seed;
        auto book  = std::make_shared<OpeningBook>(std::move(source), kept.settings.vary, least, seed());
        setup.book = [book](const chess::Board& board) { return book->move(board); };
    }
    if (openings_text) *openings_text = note.empty() ? text : text + " · " + note;
    if (openings_tag) *openings_tag = tag;
    setup.tags = {{"Strength", strengthTag(kept.strength)}, {"Openings", tag}};
    if (kept.settings.handicap != Handicap::None) {
        // The man given, as the start before it said it.
        const auto engine_side   = white ? chess::Color::BLACK : chess::Color::WHITE;
        const std::string before = kept.settings.from_board ? std::string() : std::string(chess::constants::STARTPOS);
        auto given = before.empty() ? std::string() : handicapTag(before, kept.settings.handicap, engine_side);
        if (given.empty()) given = handicapName(kept.settings.handicap);
        setup.tags.emplace_back("Handicap", given);
    }
    return setup;
}

void App::takeBoardFor(const BoardMatchSetup& setup, bool remember) {
    auto& b = board_;
    // The game shown gives way to the one played: among the recent games, with its list and its changes.
    if (remember) rememberBoardGame();
    if (online_game_) online_game_->shown = false;  // a game on Lichess goes on meanwhile (#84)
    board_origin_.reset();
    b.undo.clear();
    b.redo.clear();
    b.start_notes = {};
    b.comment_edit.reset();
    b.changes       = {};
    b.match_version = 0;
    b.match_state   = {};
    b.watching      = true;
    b.play_anyway   = false;
    b.flipped_game  = -1;
    b.chess960      = setup.chess960;
    b.title         = fmt::format("{} – {} · {}", setup.first, setup.second, clockText(setup.first_clock));
    b.message.clear();
}

bool App::startEngineGame(std::optional<chess::Color> colour) {
    play_error_.clear();
    if (const auto lock = playLock(); !lock.empty()) {
        play_error_ = lock;
        return false;
    }
    auto& s                  = playSettings();
    const EngineEntry* found = engineNamed(s.engine);
    if (!found || found->inactive) {
        play_error_ = TR("Choose the engine you play against.");
        return false;
    }
    std::mt19937 rng(std::random_device{}());
    const bool white       = colour ? *colour == chess::Color::WHITE : s.colour == 2 ? rng() % 2 == 0 : s.colour == 0;
    const auto engine_side = white ? chess::Color::BLACK : chess::Color::WHITE;
    std::string why;
    const auto fen = playStartFen(s, engine_side, &why);
    if (fen.empty()) {
        play_error_ = why;
        return false;
    }
    EngineEntry engine = *found;
    KeptGame kept;
    kept.settings        = s;
    kept.engine          = engine.name;
    kept.strength        = strengthFor(s, engine);
    kept.white           = white;
    kept.chess960        = s.from_board && board_.chess960;
    kept.start_fen       = fen;
    const GameClock mine = s.clock, theirs = s.same_clock ? s.clock : s.engine_clock;
    kept.clocks = white ? std::array<GameClock, 2>{mine, theirs} : std::array<GameClock, 2>{theirs, mine};
    applyStrength(engine, kept.strength);
    const auto names = userNames();
    const auto me    = names.empty() ? humanName() : names.front();
    std::string openings_text;
    auto setup = engineGameSetup(kept, engine, me, &openings_text, &kept.openings_tag);
    EngineEntry you;
    you.name = me;
    // The analysis would compete with the engine; it is an aid hidden until the end besides.
    if (board_.analysing) setBoardAnalysis(false);
    board_.threat_on = false;
    if (!board_.match) board_.match = std::make_unique<BoardMatch>();
    std::string error;
    if (!board_.match->start(white ? you : engine, white ? engine : you, setup, {}, error)) {
        play_error_ = error;
        return false;
    }
    takeBoardFor(setup);
    board_.title     = fmt::format("{} – {}", setup.first, setup.second);  // the clocks are in the strips
    board_.play_open = false;
    board_side_tab_  = 0;  // the notation: the other tabs wait for the end
    engine_game_     = std::make_unique<EngineGame>();
    auto& g          = *engine_game_;
    g.kept           = std::move(kept);
    g.openings_text  = openings_text;
    g.me             = me;
    toStudyBoard();
    board_back_.reset();
    return true;
}

void App::resumeEngineGame() {
    if (!engine_game_ || !engine_game_->waiting) return;
    auto& g = *engine_game_;
    play_error_.clear();
    if (liveGameOn() || boardSession() || (board_.match && board_.match_state.running)) {
        boardToast(TR("The game waits: something else is being played on the Board."));
        return;
    }
    const EngineEntry* found = engineNamed(g.kept.engine);
    if (!found) {
        play_error_ = trf("{} is no longer in your library.", g.kept.engine);
        return;
    }
    const auto game = keptReplay(g.kept);
    if (!game) {
        play_error_ = TR("The game kept cannot be read again.");
        return;
    }
    EngineEntry engine = *found;
    applyStrength(engine, g.kept.strength);
    const auto names = userNames();
    std::string me   = g.kept.white ? game->header("White") : game->header("Black");
    if (me.empty()) me = names.empty() ? humanName() : names.front();
    auto setup                 = engineGameSetup(g.kept, engine, me);
    setup.resume               = *game;
    setup.resume_clock_ms      = g.kept.clock_ms;
    setup.resume_clocks_before = g.kept.clocks_before;
    setup.resume_takebacks     = g.kept.takebacks;
    setup.resume_last_book     = g.kept.last_book;
    setup.resume_book_out      = g.kept.book_out;
    EngineEntry you;
    you.name = me;
    if (board_.analysing) setBoardAnalysis(false);
    board_.threat_on = false;
    if (!board_.match) board_.match = std::make_unique<BoardMatch>();
    std::string error;
    if (!board_.match->start(g.kept.white ? you : engine, g.kept.white ? engine : you, setup, {}, error)) {
        play_error_ = error;
        return;
    }
    takeBoardFor(setup, false);
    board_.title = fmt::format("{} – {}", setup.first, setup.second);
    g.waiting    = false;
    toStudyBoard();
}

void App::pollEngineGame() {
    if (!engine_game_) return;
    auto& g = *engine_game_;
    auto& b = board_;
    // A hint: the analysis engine's move, shown as an arrow until the game moves on.
    if (g.hint) {
        g.hint->poll();
        if (g.hint->finished() && g.hint->position() == g.hint_position && !g.hint_move && g.hint_ply >= 0) {
            auto board      = boardAt(b.match_state.current, b.chess960,
                                      std::min(g.hint_ply, static_cast<int>(b.match_state.current.moves.size())));
            const auto move = chess::uci::uciToMove(board, g.hint->bestmove());
            chess::Movelist legal;
            chess::movegen::legalmoves(legal, board);
            if (move != chess::Move::NO_MOVE && std::find(legal.begin(), legal.end(), move) != legal.end())
                g.hint_move = move;
            g.hint_position = {};
        }
    }
    if (g.waiting || g.over) return;
    if (!b.match) {  // gone with the board's games: nothing to finish
        g.over   = true;
        g.reason = TR("stopped");
        removeKeptGame(playFolder());
        return;
    }
    const auto& st = b.match_state;
    if (g.hint_move && g.hint_ply != static_cast<int>(st.current.moves.size())) g.hint_move.reset();
    // Each move kept for the next launch.
    if (st.running && st.version != g.kept_at) {
        g.kept_at = st.version;
        keepEngineGame(false);
    }
    if (!st.running && st.version > 0) {
        if (!st.finished.empty()) {
            finishEngineGame();
        } else if (!st.error.empty()) {
            g.over   = true;
            g.reason = st.error;
            removeKeptGame(playFolder());
        }
    }
}

void App::saveEngineGame(ReplayGame& game) {
    auto& g         = *engine_game_;
    auto& s         = playSettings();
    auto& sh        = shelf();
    const auto name = std::string(TR("My games against engines"));
    std::optional<std::size_t> index;
    for (std::size_t i = 0; i < sh.entries().size(); ++i)
        if (!sh.entries()[i].source && !s.games_folder.empty() && sh.entries()[i].folder.string() == s.games_folder)
            index = i;
    if (!index)
        for (std::size_t i = 0; i < sh.entries().size(); ++i)
            if (!sh.entries()[i].source && lower(sh.entries()[i].name) == lower(name)) index = i;
    std::string error;
    std::shared_ptr<study::Database> db;
    try {
        if (!index) index = sh.create(name);
        if (s.games_folder != sh.entries()[*index].folder.string()) {
            s.games_folder = sh.entries()[*index].folder.string();
            savePlaySettings();
        }
        db = sh.open(*index);
        if (!db)
            error = sh.facts(*index).error;
        else if (db->readOnly())
            error = TR("another program writes to it");
    } catch (const std::exception& e) {
        error = e.what();
    }
    study::ImportedGame imported;
    if (error.empty() && !study::readImportedGame(gamePgn(game, g.kept.chess960), imported))
        error = TR("the game cannot be written as PGN");
    study::GameId id = 0;
    if (error.empty()) {
        try {
            id = db->snapshot()->games();
            std::vector<study::ImportedGame> one;
            one.push_back(std::move(imported));
            (void)db->append(std::move(one), true);
        } catch (const std::exception& e) {
            error = e.what();
        }
    }
    if (!error.empty()) {
        // Kept in the profile instead, as the Board page's games are.
        const auto file = playFolder() / "games.pgn";
        try {
            std::error_code ec;
            fs::create_directories(playFolder(), ec);
            tour::appendShared(file, gamePgn(game, g.kept.chess960));
            g.save_error = trf("Not saved in “{}” ({}): kept in {}.", name, error, file.string());
        } catch (const std::exception& e) {
            g.save_error = trf("Not saved in “{}” ({}), nor in {} ({}).", name, error, file.string(), e.what());
        }
        return;
    }
    const auto& entry = sh.entries()[*index];
    g.saved_in        = entry.name;
    g.record.database = entry.name;
    sh.rebuildTree(*index);
    BoardOrigin origin;
    origin.folder = entry.folder;
    origin.name   = entry.name;
    origin.id     = id;
    g.origin      = origin;
}

void App::endEngineGame(ReplayGame game, int takebacks) {
    auto& g = *engine_game_;
    auto& b = board_;
    g.over  = true;
    if (g.hint) g.hint->stop();
    g.hint_move.reset();
    removeKeptGame(playFolder());
    g.result       = game.result;
    g.reason       = game.final_comment;
    const auto you = g.kept.white ? chess::Color::WHITE : chess::Color::BLACK;
    const bool moved =
        std::any_of(game.moves.begin(), game.moves.end(), [&](const ReplayMove& m) { return m.mover == you; });
    // What happened during it, in its tags with its settings.
    if (g.kept.settings.takebacks) setTag(game, "Takebacks", std::to_string(takebacks));
    if (g.kept.settings.hints) setTag(game, "Hints", std::to_string(g.hints_used));
    if (g.aids) setTag(game, "Aids", "shown");
    const int plies = static_cast<int>(game.moves.size());
    auto& r         = g.record;
    r.at            = static_cast<int64_t>(std::time(nullptr));
    r.engine        = g.kept.engine;
    r.strength      = strengthTag(g.kept.strength);
    r.openings      = g.kept.openings_tag;
    r.white         = g.kept.white;
    r.result        = game.result;
    r.termination   = game.final_comment;
    r.plies         = plies;
    r.takebacks     = takebacks;
    r.hints         = g.hints_used;
    r.aids          = g.aids;
    r.handicap      = game.header("Handicap");
    r.key           = reviewKey(game);
    r.final_fen     = boardAt(game, g.kept.chess960, plies).getFen();
    if (moved) saveEngineGame(game);
    g.final_game = game;
    // The board shows the game as it ended, with what the engine thought of its moves; Ctrl+S puts it back in its
    // database.
    const bool shown = b.watching && b.match;
    if (shown) {
        b.game        = game;
        b.tree        = GameTree{game, {}};
        b.line        = -1;
        b.ply         = plies;
        board_origin_ = g.origin;
        if (g.origin) b.changes.saved(trf("“{}”", g.origin->name));
    }
    if (moved) {
        appendPlayedGame(playFolder(), r);
        if (played_games_) played_games_->push_back(r);
    }
}

void App::finishEngineGame() {
    const auto& st = board_.match_state;
    endEngineGame(st.finished.back(), st.takebacks);
}

void App::stopKeptEngineGame() {
    if (!engine_game_ || !engine_game_->waiting) return;
    auto& g   = *engine_game_;
    auto game = keptReplay(g.kept);
    g.waiting = false;
    if (!game) {
        removeKeptGame(playFolder());
        engine_game_.reset();
        return;
    }
    game->result        = "*";
    game->final_comment = TR("stopped");
    setTag(*game, "Termination", game->final_comment);
    g.me = g.kept.white ? game->header("White") : game->header("Black");
    endEngineGame(*game, g.kept.takebacks);
    // The board shows the kept game already: it is the database's now.
    board_origin_ = g.origin;
    if (g.origin) board_.changes.saved(trf("“{}”", g.origin->name));
}

void App::showEngineGameOnBoard() {
    if (!engine_game_ || !engine_game_->over) return;
    auto& g = *engine_game_;
    openOnBoard(g.final_game, static_cast<int>(g.final_game.moves.size()), g.kept.chess960,
                fmt::format("{} – {}", g.final_game.header("White"), g.final_game.header("Black")), false);
    board_back_.reset();
    board_origin_ = g.origin;
    if (g.origin) board_.changes.saved(trf("“{}”", g.origin->name));
    board_.flip = !g.kept.white;
    toStudyBoard();
}

void App::keepEngineGame(bool closing) {
    if (!engine_game_ || engine_game_->over || engine_game_->waiting || !board_.match) return;
    auto& g        = *engine_game_;
    const auto& st = board_.match_state;
    if (!st.running || st.current.moves.empty()) return;
    KeptGame k      = g.kept;
    k.openings_text = g.openings_text;
    k.headers       = st.current.headers;
    k.moves.clear();
    k.times.clear();
    for (const auto& m : st.current.moves) {
        k.moves.push_back(chess::uci::moveToUci(m.move, k.chess960));
        k.times.push_back(m.time_ms);
    }
    k.clocks_before = st.clocks_before;
    k.clocks_before.resize(k.moves.size(), st.clock_ms);
    k.clock_ms = st.clock_ms;
    // Closing while a side thinks: its clock as it is then.
    if (closing && st.thinking) {
        const auto side = static_cast<std::size_t>(*st.thinking == chess::Color::WHITE ? 0 : 1);
        const auto ran =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - st.think_start)
                .count();
        k.clock_ms[side] = std::max<int64_t>(1, k.clock_ms[side] - ran);
    }
    k.takebacks  = st.takebacks;
    k.hints_used = g.hints_used;
    k.aids       = g.aids;
    k.last_book  = st.last_book;
    k.book_out   = st.book_out;
    (void)writeKeptGame(playFolder(), k);
}

void App::loadKeptEngineGame() {
    auto kept = readKeptGame(playFolder());
    if (!kept) return;
    const auto game = keptReplay(*kept);
    if (!game || game->moves.empty()) {
        removeKeptGame(playFolder());
        return;
    }
    // The board's game kept from the session before is that game, when RookStudio closed during it: not twice.
    forget_board_game_ = reviewKey(board_.tree.main) == reviewKey(*game);
    openOnBoard(*game, static_cast<int>(game->moves.size()), kept->chess960,
                fmt::format("{} – {}", game->header("White"), game->header("Black")), false);
    forget_board_game_ = false;
    board_back_.reset();
    board_.flip     = !kept->white;
    engine_game_    = std::make_unique<EngineGame>();
    auto& g         = *engine_game_;
    g.kept          = *kept;
    g.waiting       = true;
    g.hints_used    = kept->hints_used;
    g.aids          = kept->aids;
    g.openings_text = kept->openings_text;
    g.me            = kept->white ? game->header("White") : game->header("Black");
    toStudyBoard();
}

void App::closeEngineGame() {
    if (!engine_game_) return;
    if (engine_game_->hint) engine_game_->hint->stop();
    if (engine_game_->waiting) removeKeptGame(playFolder());
    engine_game_.reset();
    // The game stays on the board as any game; its match is done.
    if (board_.match && !board_.match_state.running) board_.match.reset();
}

void App::backToEngineGame() {
    auto& b = board_;
    if (!engine_game_ || !b.match) return;
    const auto& g = *engine_game_;
    rememberBoardGame();
    board_origin_.reset();
    b.undo.clear();
    b.redo.clear();
    b.start_notes = {};
    b.comment_edit.reset();
    b.changes  = {};
    b.watching = true;
    b.game     = b.match_state.current;
    b.tree     = GameTree{b.game, {}};
    b.line     = -1;
    b.ply      = static_cast<int>(b.game.moves.size());
    b.chess960 = g.kept.chess960;
    b.flip     = !g.kept.white;
    b.title    = fmt::format("{} – {}", b.game.header("White"), b.game.header("Black"));
    b.selected.reset();
    toStudyBoard();
    board_back_.reset();
}

void App::showEngineGameAids() {
    if (!engineGameRunning()) return;
    engine_game_->aids = true;
    boardToast(TR("The aids are back: the game is marked “with aids”"));
}

void App::engineGameTakeBack() {
    if (!engineGameRunning() || engine_game_->waiting || !board_.match) return;
    auto& g = *engine_game_;
    if (!g.kept.settings.takebacks) {
        boardToast(TR("No takeback in this game: the Play page allows them for the next one"));
        return;
    }
    const auto you   = g.kept.white ? chess::Color::WHITE : chess::Color::BLACK;
    const auto& game = board_.match_state.current;
    if (std::none_of(game.moves.begin(), game.moves.end(), [&](const ReplayMove& m) { return m.mover == you; })) {
        boardToast(TR("No move of yours to take back yet"));
        return;
    }
    board_.match->takeBack();
    g.hint_move.reset();
    board_.selected.reset();
    board_.move_typed.reset();
    if (board_.line >= 0 || board_.ply != static_cast<int>(game.moves.size())) backToEngineGame();
}

void App::engineGameHint() {
    if (!engineGameRunning() || engine_game_->waiting) return;
    auto& g = *engine_game_;
    auto& b = board_;
    if (!g.kept.settings.hints) {
        boardToast(TR("No hints in this game: the Play page allows them for the next one"));
        return;
    }
    if (g.hints_used >= HINTS) {
        boardToast(TR("No hint left in this game"));
        return;
    }
    if (!b.humanTurn()) {
        boardToast(TR("A hint comes at your move"));
        return;
    }
    if (g.hint_move || !g.hint_position.empty()) return;  // one shown, or being looked for
    const auto* engine = analysisEngine();
    if (!engine) {
        boardToast(TR("A hint needs an engine for the analysis: add one on the Engines page"));
        return;
    }
    if (!g.hint) g.hint = std::make_unique<Analyser>();
    AnalysisSettings settings;
    settings.engine  = engine->name;
    settings.threads = 1;
    settings.multipv = 1;
    if (!g.hint->active() || g.hint->engine() != engine->name) g.hint->start(*engine, settings);
    const auto& game = b.match_state.current;
    g.hint_ply       = static_cast<int>(game.moves.size());
    g.hint_position  = analysisPosition(game, b.chess960, g.hint_ply);
    SearchLimit limit;
    limit.movetime_ms = 1000;
    g.hint->analyse(g.hint_position, limit, true);
    ++g.hints_used;
}

void App::engineGameResign() {
    if (!engineGameRunning() || engine_game_->waiting || !board_.match) return;
    auto& g          = *engine_game_;
    const double now = ImGui::GetTime();
    if (now - g.resign_at <= RESIGN_SECONDS) {
        board_.match->resign();
        g.resign_at = -100;
        return;
    }
    g.resign_at = now;
}

bool App::engineGameKeys() {
    if (!engine_game_) return false;
    auto& g        = *engine_game_;
    const auto& io = ImGui::GetIO();
    if (io.WantTextInput) return false;
    const bool enter = !io.KeyCtrl && !io.KeyAlt && !io.KeyShift &&
                       (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    if (g.over) {
        // Enter reviews it, once; then the Board's keys as for any game.
        if (enter && !g.reviewed && !board_.reviewing() && reviewKey(board_.tree.main) == g.record.key) {
            g.reviewed = true;
            startGameReview();
            return true;
        }
        return false;
    }
    if (g.waiting) {
        if (enter) resumeEngineGame();
        boardMoveKeys();
        return true;
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) engineGameTakeBack();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_H)) engineGameHint();
    if (enter && ImGui::GetTime() - g.resign_at <= RESIGN_SECONDS) engineGameResign();
    boardMoveKeys();
    return true;
}

std::string App::engineGameTitle() const {
    const auto& g = *engine_game_;
    PlayedGame r;
    r.white          = g.kept.white;
    r.result         = g.result;
    const auto score = userScore(r);
    const int move   = (g.record.plies + 1) / 2;
    if (!score) return move > 0 ? trf("Stopped at move {}: no result", move) : std::string(TR("Stopped: no result"));
    std::string head;
    if (*score > 0)
        head = g.reason == trf("{} loses on time", g.kept.engine) ? std::string(TR("You won on time"))
               : g.reason.empty()                                 ? std::string(TR("You won"))
                                                                  : trf("You won: {}", g.reason);
    else if (*score < 0)
        head = !g.me.empty() && g.reason == trf("{} resigns", g.me)         ? std::string(TR("You resigned"))
               : !g.me.empty() && g.reason == trf("{} loses on time", g.me) ? std::string(TR("You lost on time"))
               : g.reason.empty()                                           ? std::string(TR("You lost"))
                                                                            : trf("You lost: {}", g.reason);
    else
        head = g.reason == TR("draw by agreement") ? std::string(TR("Draw by agreement"))
               : g.reason.empty()                  ? std::string(TR("Draw"))
                                                   : trf("Draw: {}", g.reason);
    return trf("{} at move {}", head, move);
}

// ---------------------------------------------------------------------------
// The game on the Board page

void App::drawEngineGameCard() {
    auto& g = *engine_game_;
    if (g.over) return drawEngineGameEnd();
    const ui::TopicRegion topic(guide::topics::EngineGame);
    auto& b        = board_;
    const auto& st = b.match_state;
    beginTinted("##engine_game", ui::ACCENT);
    ImGui::PushTextWrapPos();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
    wrapped(trf("Against {} · {}", g.kept.engine, strengthText(g.kept.strength)));
    ImGui::PopFont();
    {
        const auto& c   = g.kept.clocks;
        const auto mine = c[g.kept.white ? 0 : 1], theirs = c[g.kept.white ? 1 : 0];
        std::string sub = clockText(mine) == clockText(theirs)
                              ? clockText(mine)
                              : trf("you {}, the engine {}", clockText(mine), clockText(theirs));
        sub += " · " + trf("its openings: {}", g.openings_text);
        if (g.kept.settings.handicap != Handicap::None)
            sub += " · " + trf("it gives {}", handicapName(g.kept.settings.handicap));
        wrapped(sub, true);
    }
    if (g.waiting) {
        const int move = (static_cast<int>(g.kept.moves.size()) + 2) / 2;
        wrapped(trf("Kept when RookStudio closed, at move {}: the clocks wait until you resume.", move));
        if (primaryButton((std::string(TR("Resume")) + " · " + TR("Enter")).c_str())) resumeEngineGame();
        ImGui::SameLine();
        if (ImGui::Button(TR("Stop it##kept_game"))) stopKeptEngineGame();
        tip(TR("The game ends without a result, saved as it is."));
        if (!play_error_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
            wrapped(play_error_);
            ImGui::PopStyleColor();
        }
        ImGui::PopTextWrapPos();
        endTinted();
        return;
    }
    if (!b.watching) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
        wrapped(TR("Your game goes on meanwhile, your clock running when it is your move."));
        ImGui::PopStyleColor();
        if (primaryButton(TR("Back to the game##engine_game"))) backToEngineGame();
        ImGui::PopTextWrapPos();
        endTinted();
        return;
    }
    // Whose move, the draw offered.
    if (b.humanTurn())
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", TR("Your move"));
    else if (st.thinking)
        ImGui::TextDisabled("%s", trf("{} thinks…", g.kept.engine).c_str());
    if (st.draw_offered) {
        ImGui::TextDisabled("%s", trf("Draw offered: {} answers with its next move.", g.kept.engine).c_str());
    } else if (st.draw_declined >= 0 && st.draw_declined + 2 >= static_cast<int>(st.current.moves.size())) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           trf("{} declines the draw.", g.kept.engine).c_str());
    }
    if (!st.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        wrapped(st.error);
        ImGui::PopStyleColor();
    }
    // What can be done.
    const auto you   = g.kept.white ? chess::Color::WHITE : chess::Color::BLACK;
    const bool moved = std::any_of(st.current.moves.begin(), st.current.moves.end(),
                                   [&](const ReplayMove& m) { return m.mover == you; });
    ImGui::BeginDisabled(st.draw_offered || !st.running);
    if (ImGui::Button(TR("Offer a draw"))) b.match->offerDraw();
    ImGui::EndDisabled();
    tip(
        TR("The engine answers with its next move: it takes the draw when it stands worse, or no better once 20 moves "
           "are played."));
    const bool armed   = ImGui::GetTime() - g.resign_at <= RESIGN_SECONDS;
    const char* resign = armed ? TR("Resign? Enter, or click again") : TR("Resign…");
    ui::sameLineOrWrap(resign);
    if (armed) ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
    if (ImGui::Button(fmt::format("{}###engine_resign", resign).c_str())) engineGameResign();
    if (armed) ImGui::PopStyleColor();
    if (g.kept.settings.takebacks) {
        const auto label = st.takebacks > 0 ? trf("Take back · Ctrl+Z ({} so far)", st.takebacks)
                                            : std::string(TR("Take back · Ctrl+Z"));
        ui::sameLineOrWrap(label.c_str());
        ImGui::BeginDisabled(!moved);
        if (ImGui::Button(fmt::format("{}###engine_takeback", label).c_str())) engineGameTakeBack();
        ImGui::EndDisabled();
        tip(TR("Your last move, and the engine's answer: counted, and written in the game."));
    }
    if (g.kept.settings.hints) {
        const int left   = HINTS - g.hints_used;
        const auto label = trf("Hint · Alt+H ({} left)", left);
        ui::sameLineOrWrap(label.c_str());
        ImGui::BeginDisabled(left <= 0 || !b.humanTurn() || g.hint_move.has_value() || !g.hint_position.empty());
        if (ImGui::Button(fmt::format("{}###engine_hint", label).c_str())) engineGameHint();
        ImGui::EndDisabled();
        tip(TR("The analysis engine's move after a second's search, as an arrow: counted, and written in the game."));
    }
    ui::sameLineOrWrap(TR("Stop##engine_game"));
    if (ImGui::Button(TR("Stop##engine_game"))) b.match->stop();
    tip(TR("The game ends without a result, saved as it is. Closing RookStudio instead keeps it for the next launch."));
    if (!g.hint_position.empty())
        ImGui::TextDisabled("%s", TR("A hint: the analysis engine looks…"));
    else if (g.hint_move)
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::GOOD), "%s", TR("A hint: the green arrow on the board"));
    wrapped(TR("Type your move (Nf3, Enter) or play it on the board. The engine shows nothing of its search during the "
               "game; “Resign…” asks for Enter a second time."),
            true);
    if (!g.aids) {
        wrapped(TR("The analysis, the tree, the tables and the assistant are hidden until the end, as you chose."),
                true);
        if (ImGui::SmallButton(TR("Show them##engine_aids"))) showEngineGameAids();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("the game will be marked “with aids”"));
    } else {
        ImGui::TextDisabled("%s", TR("The aids are shown: the game is marked “with aids”."));
    }
    ImGui::PopTextWrapPos();
    endTinted();
}

void App::drawEngineGameEnd() {
    const ui::TopicRegion topic(guide::topics::AfterGame);
    auto& g          = *engine_game_;
    auto& b          = board_;
    auto& s          = playSettings();
    bool close       = false;
    bool rematch     = false;
    const bool shown = reviewKey(b.tree.main) == g.record.key;
    if (!shown) {
        // Another game on the board: the end in a line, until it is shown again or closed.
        beginTinted("##engine_game_end", ui::ACCENT);
        ImGui::PushTextWrapPos();
        wrapped(trf("Your game against {}: {}.", g.kept.engine, engineGameTitle()));
        ImGui::PopTextWrapPos();
        if (ImGui::SmallButton(TR("Show it on the board"))) showEngineGameOnBoard();
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Close##engine_game_line"))) close = true;
        endTinted();
        if (close) closeEngineGame();
        return;
    }
    beginTinted("##engine_game_end", ui::ACCENT);
    ImGui::PushTextWrapPos();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.25f);
    wrapped(engineGameTitle());
    ImGui::PopFont();
    {
        std::string extras;
        const auto add = [&](const std::string& part) { extras += (extras.empty() ? "" : ", ") + part; };
        if (g.kept.settings.takebacks)
            add(g.record.takebacks == 0 ? std::string(TR("no takeback"))
                                        : trn("{} takeback", "{} takebacks", g.record.takebacks, g.record.takebacks));
        if (g.kept.settings.hints)
            add(g.hints_used == 0 ? std::string(TR("no hint"))
                                  : trn("{} hint", "{} hints", g.hints_used, g.hints_used));
        add(g.aids ? TR("with aids") : TR("without aids"));
        const auto& c = g.kept.clocks;
        wrapped(trf("Against {} at {}, you with {} · {} · {}.", g.kept.engine, strengthText(g.kept.strength),
                    colourName(g.kept.white), clockText(c[g.kept.white ? 0 : 1]), extras));
    }
    if (!g.saved_in.empty()) {
        wrapped(trf("Saved in “{}”, its settings in its tags: strength, openings, handicap, takebacks.", g.saved_in),
                true);
    } else if (!g.save_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
        wrapped(g.save_error);
        ImGui::PopStyleColor();
    }
    ImGui::BeginDisabled(g.reviewed || b.reviewing() || g.final_game.moves.empty());
    if (primaryButton(TR("Review it"))) {
        g.reviewed = true;
        startGameReview();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", b.reviewing() ? TR("the review runs: its marks come into the notation")
                              : g.reviewed  ? TR("reviewed: its marks are in the notation")
                                            : TR("Enter · the review, then your mistakes as exercises"));
    if (ImGui::Button(TR("Rematch, colours reversed"))) rematch = true;
    ImGui::SameLine();
    if (ImGui::Button(TR("Change the settings"))) page_ = Page::Play;
    ImGui::SameLine();
    if (ImGui::Button(TR("Close##engine_game_end"))) close = true;
    tip(TR("The game stays on the board as any game."));
    if (!play_error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        wrapped(play_error_);
        ImGui::PopStyleColor();
    }
    if (userNames().empty() && !g.me.empty())
        wrapped(trf("Your mistakes become exercises once RookStudio knows your names: “{}” plays this game "
                    "(Settings › Your names).",
                    g.me),
                true);
    ImGui::PopTextWrapPos();
    endTinted();

    // The user's games against that engine at that level.
    ImGui::Dummy(ImVec2(0, px(4)));
    ui::beginCard("##engine_game_tally");
    ImGui::PushTextWrapPos();
    const auto strength = strengthTag(g.kept.strength);
    const auto tally    = playTally(playedGames(), g.kept.engine, strength);
    heading(trf("Your games against {} at {}", g.kept.engine, strengthText(g.kept.strength)).c_str());
    if (tally.games == 0) {
        wrapped(TR("None with a result yet at this level."), true);
    } else {
        auto line = trn("{} game: {} won, {} drawn, {} lost.", "{} games: {} won, {} drawn, {} lost.", tally.games,
                        tally.games, tally.won, tally.drawn, tally.lost);
        if (tally.last_lost) {
            const auto& l = *tally.last_lost;
            line += " " + trf("The last lost, {}: {}.", ui::dayText(static_cast<std::time_t>(l.at)),
                              endingOf(l.final_fen, l.plies));
            const auto& ex = mistakes_->mistakeRecord().exercises;
            if (std::any_of(ex.begin(), ex.end(), [&](const auto& e) { return e.second.game_key == l.key; }))
                line += " " + std::string(TR("(It is in your exercises.)"));
        }
        wrapped(line);
    }
    if (tally.with_aids > 0)
        wrapped(trn("{} game with aids, not counted.", "{} games with aids, not counted.", tally.with_aids,
                    tally.with_aids),
                true);
    wrapped(TR("The next level is chosen here; nothing goes up by itself."), true);
    if (const auto* engine = engineNamed(g.kept.engine)) {
        // The level of the next game: around this one's.
        std::vector<Strength> levels;
        if (const auto range = eloRange(*engine)) {
            const int at = g.kept.strength.elo > 0 ? g.kept.strength.elo : range->max;
            for (const int d : {-100, 0, 100}) {
                const int elo = std::clamp(at + d, range->min, range->max);
                if (levels.empty() || levels.back().elo != elo) levels.push_back({elo, 0});
            }
        } else {
            const auto steps = nodeSteps();
            const auto at    = std::find(steps.begin(), steps.end(), static_cast<int>(g.kept.strength.nodes));
            const auto i     = at == steps.end() ? steps.size() - 1 : static_cast<std::size_t>(at - steps.begin());
            for (std::size_t k = i > 0 ? i - 1 : 0; k <= std::min(i + 1, steps.size() - 1); ++k)
                levels.push_back({0, static_cast<uint64_t>(steps[k])});
        }
        const auto next = strengthFor(s, *engine);
        bool first      = true;
        for (const auto& level : levels) {
            const auto label = strengthText(level);
            if (!first) ImGui::SameLine(0, px(4));
            first = false;
            if (chip(label.c_str(), s.engine == engine->name && next == level, strengthTag(level).c_str())) {
                s.engine = engine->name;
                s.full   = false;
                if (level.elo > 0) s.elo = level.elo;
                if (level.nodes > 0) s.nodes = static_cast<int>(level.nodes);
                savePlaySettings();
            }
        }
    }
    ImGui::PopTextWrapPos();
    ui::endCard();

    if (rematch) {
        const bool white = g.kept.white;
        closeEngineGame();
        startEngineGame(white ? chess::Color::BLACK : chess::Color::WHITE);
    } else if (close) {
        closeEngineGame();
    }
}

void App::drawEngineGameBanner() {
    const ui::TopicRegion topic(guide::topics::EngineGame);
    beginTinted("##engine_game_hidden", ui::ACCENT);
    ui::inlineIcon(ui::Icon::Lock, ui::ACCENT);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s",
                       TR("Hidden until the end of your game, as you chose"));
    ImGui::TextDisabled("%s", trf("{} plays you: the analysis, the tree, the reference games, the tables and the "
                                  "assistant come back with the result.",
                                  engine_game_->kept.engine)
                                  .c_str());
    if (ImGui::SmallButton(TR("Show them##engine_banner"))) showEngineGameAids();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("the game will be marked “with aids”"));
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    endTinted();
}

// ---------------------------------------------------------------------------
// The Play page

void App::drawPlay() {
    ui::pageTitle(TR("Play"), guide::topics::PlayPage);
    // Its parts (M12, boards 4 and 5): against an engine, on Lichess (#84); the one shown last kept.
    if (play_part_ < 0) play_part_ = std::clamp(playSettings().part, 0, 1);
    const auto part = [&](const char* label, int which) {
        ImGui::SameLine(0, which == 0 ? px(14) : px(8));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("·");
        ImGui::SameLine(0, px(8));
        if (partLink(label, play_part_ == which)) {
            play_part_          = which;
            playSettings().part = which;
            savePlaySettings();
        }
    };
    part(TR("Against an engine"), 0);
    part(TR("On Lichess"), 1);
    ImGui::BeginChild("##play_page", ImVec2(0, 0), ImGuiChildFlags_None);
    if (play_part_ == 1)
        drawPlayLichess();
    else
        drawPlayEngine();
    ImGui::EndChild();
}

void App::drawPlayEngine() {
    const ui::TopicRegion topic(guide::topics::PlayEngine);
    auto& s      = playSettings();
    bool changed = false;
    // A game being played, or kept: back to it.
    if (engine_game_ && !engine_game_->over) {
        beginTinted("##play_going", ui::ACCENT);
        ImGui::PushTextWrapPos();
        wrapped(engine_game_->waiting
                    ? trf("Your game against {} was kept when RookStudio closed: it waits on the Board.",
                          engine_game_->kept.engine)
                    : trf("Your game against {} is being played on the Board.", engine_game_->kept.engine));
        if (primaryButton(TR("Back to it"))) {
            if (!engine_game_->waiting && !board_.watching)
                backToEngineGame();
            else
                toStudyBoard();
        }
        ImGui::PopTextWrapPos();
        endTinted();
        ImGui::Dummy(ImVec2(0, px(6)));
    }
    if (engines_.empty()) {
        drawFirstEngineCard(TR("A game against an engine needs one in your library."));
        return;
    }
    // The engine chosen, else the analysis's, else the first one.
    const EngineEntry* engine = engineNamed(s.engine);
    if (!engine || engine->inactive) {
        engine = analysisEngine();
        if (engine && engine->inactive) engine = nullptr;
        for (const auto& e : engines_)
            if (!engine && !e.inactive) engine = &e;
        if (engine) {
            s.engine = engine->name;
            changed  = true;
        }
    }
    if (!engine) {
        wrapped(TR("Every engine of your library is set aside: bring one back on the Engines page."), true);
        return;
    }
    const auto lock  = playLock();
    const auto& io   = ImGui::GetIO();
    const bool enter = lock.empty() && !io.WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
                       !io.KeyCtrl && !io.KeyAlt &&
                       (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    if (ImGui::BeginTable("##play_columns", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##choice", ImGuiTableColumnFlags_WidthStretch, 1.15f);
        ImGui::TableSetupColumn("##you", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        drawPlayOpponent(s, changed);
        ImGui::Dummy(ImVec2(0, px(4)));
        if (const auto* chosen = engineNamed(s.engine)) engine = chosen;
        drawPlayStrength(s, *engine, changed);
        ImGui::Dummy(ImVec2(0, px(4)));
        drawPlayOpenings(s, changed);
        ImGui::TableNextColumn();
        drawPlayYou(s, changed);
        ImGui::Dummy(ImVec2(0, px(4)));
        drawPlayHandicap(s, changed);
        ImGui::Dummy(ImVec2(0, px(4)));
        beginTinted("##play_aids", ui::ACCENT);
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(TR("The aids during the game: hidden until the end"));
        wrapped(TR("The analysis, the tree, the tables and the assistant come back with the result. “Show them” gives "
                   "them back during the game; the game is then marked “with aids”."),
                true);
        ImGui::PopTextWrapPos();
        endTinted();
        ImGui::Dummy(ImVec2(0, px(4)));
        // Play: the start the choice makes, the engine's threads in the core budget.
        ImGui::PushTextWrapPos();
        std::string why;
        bool ready = !playStartFen(s, s.colour == 1 ? chess::Color::WHITE : chess::Color::BLACK, &why).empty();
        if (ready && s.colour == 2) ready = !playStartFen(s, chess::Color::WHITE, &why).empty();
        const int need  = engine->threads();
        const int busy  = tournamentThreads() + boardThreads() + botThreads();  // the Lichess bot's game too (#85)
        const bool full = busy + need > topology_.logical;
        if (primaryButton(TR("Play"), ready && lock.empty()) || (enter && ready && !engineGameRunning()))
            startEngineGame();
        ImGui::SameLine(0, px(10));
        ImGui::TextDisabled(
            "%s", trf("Enter · counts in the core budget: {} of {} threads", need, topology_.logical).c_str());
        if (full && lock.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
            wrapped(
                trf("The machine is full: the tournaments' games and the analysis use {} of its {} threads, and "
                    "this game needs {}. It would slow them down, and the engine too.",
                    busy, topology_.logical, need));
            ImGui::PopStyleColor();
        }
        for (const std::string* text : std::initializer_list<const std::string*>{&lock, &why, &play_error_}) {
            if (text->empty()) continue;
            ImGui::PushStyleColor(ImGuiCol_Text, text == &play_error_ ? ui::BAD : ui::WARN);
            wrapped(*text);
            ImGui::PopStyleColor();
        }
        // The user's games against that engine at that level.
        const auto strength = strengthFor(s, *engine);
        const auto tally    = playTally(playedGames(), engine->name, strengthTag(strength));
        if (tally.games > 0)
            wrapped(trn("Your {} game against {} at {}: {} won, {} drawn, {} lost.",
                        "Your {} games against {} at {}: {} won, {} drawn, {} lost.", tally.games, tally.games,
                        engine->name, strengthText(strength), tally.won, tally.drawn, tally.lost),
                    true);
        wrapped(TR("Nothing leaves the machine: the engine runs on it."), true);
        ImGui::PopTextWrapPos();
        ImGui::EndTable();
    }
    if (changed) savePlaySettings();
}

void App::drawPlayOpponent(PlaySettings& s, bool& changed) {
    ui::beginCard("##play_opponent");
    heading(TR("The opponent"));
    std::vector<const EngineEntry*> active;
    for (const auto& e : engines_)
        if (!e.inactive) active.push_back(&e);
    const auto how = [](const EngineEntry& e) {
        if (const auto r = eloRange(e)) return trf("an Elo from {} to {}", r->min, r->max);
        return std::string(TR("a strength in nodes a move"));
    };
    if (active.size() <= 6) {
        for (const auto* e : active) {
            ImGui::PushID(e->name.c_str());
            const bool on = e->name == s.engine;
            ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
            const bool clicked = ImGui::RadioButton(e->name.c_str(), on);
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextDisabled("· %s", how(*e).c_str());
            if (clicked && !on) {
                s.engine = e->name;
                changed  = true;
            }
            ImGui::PopID();
        }
    } else {
        ImGui::SetNextItemWidth(px(240));
        if (ImGui::BeginCombo("##play_engine", s.engine.c_str(), ImGuiComboFlags_HeightLargest)) {
            std::vector<std::string> names, more;
            for (const auto* e : active) {
                names.push_back(e->name);
                more.push_back(engineSearchText(*e));
            }
            if (const int i = ui::searchComboItems(names, s.engine, &more); i >= 0) {
                s.engine = names[static_cast<std::size_t>(i)];
                changed  = true;
            }
            ImGui::EndCombo();
        }
        if (const auto* e = engineNamed(s.engine)) {
            ImGui::SameLine();
            ImGui::TextDisabled("· %s", how(*e).c_str());
        }
    }
    ui::endCard();
}

void App::drawPlayStrength(PlaySettings& s, const EngineEntry& engine, bool& changed) {
    ui::beginCard("##play_strength");
    heading(TR("Its strength"));
    const auto range = eloRange(engine);
    ImGui::BeginDisabled(s.full);
    if (range) {
        int elo = std::clamp(s.elo, range->min, range->max);
        ImGui::SetNextItemWidth(std::max(px(120), ImGui::GetContentRegionAvail().x - px(70)));
        if (ImGui::SliderInt("##play_elo", &elo, range->min, range->max, "")) {
            s.elo   = std::clamp((elo + 5) / 10 * 10, range->min, range->max);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.05f);
        ImGui::Text("%d", std::clamp(s.elo, range->min, range->max));
        ImGui::PopFont();
    } else {
        const auto steps = nodeSteps();
        int index        = 0;
        for (std::size_t i = 0; i < steps.size(); ++i)
            if (steps[i] <= s.nodes) index = static_cast<int>(i);
        ImGui::SetNextItemWidth(std::max(px(120), ImGui::GetContentRegionAvail().x - px(170)));
        if (ImGui::SliderInt("##play_nodes", &index, 0, static_cast<int>(steps.size()) - 1, "")) {
            s.nodes = steps[static_cast<std::size_t>(index)];
            changed = true;
        }
        ImGui::SameLine();
        const auto n = static_cast<uint64_t>(steps[static_cast<std::size_t>(index)]);
        ImGui::TextUnformatted(trn("{} node a move", "{} nodes a move", n, ui::exactCount(n)).c_str());
    }
    ImGui::EndDisabled();
    ImGui::PushTextWrapPos();
    if (range)
        wrapped(trf("The Elo {} aims at (UCI_Elo), on its own scale: an indication, not a FIDE rating. An engine "
                    "without this setting plays a number of nodes a move.",
                    engine.name),
                true);
    else
        wrapped(trf("{} has no Elo setting (UCI_Elo): it plays a number of nodes a move, fewer for a weaker game.",
                    engine.name),
                true);
    ImGui::PopTextWrapPos();
    if (ImGui::Checkbox(TR("Full strength"), &s.full)) changed = true;
    ui::endCard();
}

void App::drawPlayOpenings(PlaySettings& s, bool& changed) {
    ui::beginCard("##play_openings");
    heading(TR("Its style: its openings"));
    auto& sh = shelf();
    struct Base {
        std::string name, folder;
        uint64_t games = 0;
    };
    std::vector<Base> bases, players;
    for (std::size_t i = 0; i < sh.entries().size(); ++i) {
        const auto& e = sh.entries()[i];
        if (e.source) continue;
        const auto facts = sh.facts(i);
        if (!facts.tree) continue;
        bases.push_back({e.name, e.folder.string(), facts.games});
        if (e.name.rfind(PLAYER_PREFIX, 0) == 0) players.push_back(bases.back());
    }
    auto& reps = repertoires();
    std::vector<const BookEntry*> books;
    for (const auto& b : books_) {
        auto ext = fs::path(b.path).extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".bin" || study::isCtg(b.path)) books.push_back(&b);
    }
    const float indent = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
    const auto radio   = [&](const char* label, Openings which, bool enabled) {
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ImGui::RadioButton(label, s.openings == which);
        ImGui::EndDisabled();
        if (clicked && s.openings != which) {
            s.openings = which;
            changed    = true;
        }
    };
    const auto note = [&](const std::string& text) {
        ImGui::Indent(indent);
        ImGui::PushTextWrapPos();
        wrapped(text, true);
        ImGui::PopTextWrapPos();
        ImGui::Unindent(indent);
    };
    // A combo of names after the radio: the one chosen, by its value.
    const auto combo = [&](const char* id, const std::vector<std::pair<std::string, std::string>>& items,
                           std::string& value, Openings which) {
        if (items.empty()) return;
        std::string preview = items.front().first;
        for (const auto& [label, v] : items)
            if (v == value) preview = label;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::min(px(260), std::max(px(120), ImGui::GetContentRegionAvail().x)));
        if (ImGui::BeginCombo(id, preview.c_str())) {
            for (const auto& [label, v] : items) {
                if (ImGui::Selectable(label.c_str(), v == value)) {
                    value      = v;
                    s.openings = which;
                    changed    = true;
                }
            }
            ImGui::EndCombo();
        }
    };

    radio(TR("Its own"), Openings::Own, true);
    note(TR("No book: the engine from the first move."));

    radio(TR("Your databases"), Openings::Databases, !bases.empty());
    if (bases.empty()) {
        note(TR("None has its opening tree yet: import games on the Databases page."));
    } else {
        std::vector<std::pair<std::string, std::string>> items{{TR("all of them"), ""}};
        for (const auto& b : bases)
            items.push_back({fmt::format("{} · {}", b.name, ui::exactCount(b.games)), b.folder});
        combo("##play_database", items, s.database, Openings::Databases);
        note(TR("The moves played most there, then the engine."));
    }

    radio(TR("Your repertoire"), Openings::Repertoire, !reps.entries().empty());
    if (reps.entries().empty()) {
        note(TR("None yet: make one on the Openings page."));
    } else {
        if (std::none_of(reps.entries().begin(), reps.entries().end(),
                         [&](const RepertoireFile& r) { return r.name == s.repertoire; })) {
            s.repertoire = reps.entries().front().name;
            changed      = true;
        }
        std::vector<std::pair<std::string, std::string>> items;
        for (const auto& r : reps.entries())
            items.push_back({fmt::format("{} · {}", r.name, colourName(r.side == chess::Color::WHITE)), r.name});
        const auto before = s.repertoire;
        combo("##play_repertoire", items, s.repertoire, Openings::Repertoire);
        // Your colour follows the repertoire's.
        if (s.repertoire != before || (changed && s.openings == Openings::Repertoire)) {
            for (const auto& r : reps.entries())
                if (r.name == s.repertoire && s.openings == Openings::Repertoire)
                    s.colour = r.side == chess::Color::WHITE ? 0 : 1;
        }
        note(TR("It plays what you prepared against, then the engine; you play the repertoire's colour."));
    }

    radio(TR("A player's games"), Openings::Player, !players.empty());
    if (players.empty()) {
        note(TR("Prepare a game against a player first, on the Lichess page: their games come into a database."));
    } else {
        if (std::none_of(players.begin(), players.end(), [&](const Base& b) { return b.folder == s.player; })) {
            // The player being prepared against, else the first.
            s.player = players.front().folder;
            if (prep_)
                for (const auto& b : players)
                    if (b.name == std::string(PLAYER_PREFIX) + prep_->opponent) s.player = b.folder;
            changed = true;
        }
        std::vector<std::pair<std::string, std::string>> items;
        for (const auto& b : players)
            items.push_back({trn("{} · {} game", "{} · {} games", b.games, b.name.substr(PLAYER_PREFIX.size()),
                                 ui::exactCount(b.games)),
                             b.folder});
        combo("##play_player", items, s.player, Openings::Player);
        note(TR("The moves they played with the engine's colour, then the engine."));
    }

    radio(TR("A book of the library"), Openings::Book, !books.empty());
    if (books.empty()) {
        note(TR("None in your library: add a Polyglot or CTG book on the Books page."));
    } else {
        if (std::none_of(books.begin(), books.end(), [&](const BookEntry* b) { return b->path == s.book; })) {
            s.book  = books.front()->path;
            changed = true;
        }
        std::vector<std::pair<std::string, std::string>> items;
        for (const auto* b : books) items.push_back({b->name, b->path});
        combo("##play_book", items, s.book, Openings::Book);
        note(TR("Polyglot or CTG: its moves, then the engine."));
    }
    if (ImGui::Checkbox(trf("Vary: at random among the moves played at least {} times", OPENING_LEAST).c_str(),
                        &s.vary))
        changed = true;
    tip(
        trf("Unticked: the move played most, a book's main move first, every time. A book or a repertoire offers each "
            "of its moves; your databases and a player's games those played at least {} times.",
            OPENING_LEAST));
    ImGui::TextDisabled("%s", TR("The engine's own settings stay on its card."));
    if (s.handicap != Handicap::None && s.openings != Openings::Own) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
        ImGui::PushTextWrapPos();
        wrapped(
            TR("With a man given, the openings meet positions they do not have: the engine plays from the first "
               "move."));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ui::endCard();
}

void App::drawPlayYou(PlaySettings& s, bool& changed) {
    ui::beginCard("##play_you");
    heading(TR("You"));
    for (int c = 0; c < 3; ++c) {
        const char* label = c == 0 ? TR("White") : c == 1 ? TR("Black") : TR("At random");
        if (c > 0) ImGui::SameLine(0, px(4));
        if (chip(label, s.colour == c, fmt::format("play_colour{}", c).c_str()) && s.colour != c) {
            s.colour = c;
            changed  = true;
        }
    }
    // The clocks: minutes and an increment in seconds.
    const auto clockFields = [&](const char* id, GameClock& clock) {
        double minutes = clock.base_s / 60.0;
        ImGui::SetNextItemWidth(px(60));
        if (ui::inputDouble(fmt::format("##{}_base", id).c_str(), &minutes, "%g")) {
            clock.base_s = std::clamp(minutes, 0.25, 600.0) * 60.0;
            changed      = true;
        }
        tip(TR("Minutes for the game."));
        ImGui::SameLine(0, px(4));
        ImGui::TextUnformatted("+");
        ImGui::SameLine(0, px(4));
        ImGui::SetNextItemWidth(px(52));
        if (ui::inputDouble(fmt::format("##{}_inc", id).c_str(), &clock.inc_s, "%g")) {
            clock.inc_s = std::clamp(clock.inc_s, 0.0, 600.0);
            changed     = true;
        }
        tip(TR("Seconds added after each move."));
    };
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(s.same_clock ? TR("The clock") : TR("Your clock"));
    ImGui::SameLine();
    clockFields("play_clock", s.clock);
    ImGui::SameLine(0, px(12));
    if (ImGui::Checkbox(TR("The same for both##play"), &s.same_clock)) changed = true;
    tip(TR("Untick it to give the engine a clock of its own: unequal time, a handicap too."));
    if (!s.same_clock) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(TR("The engine's"));
        ImGui::SameLine();
        clockFields("play_engine_clock", s.engine_clock);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TR("From"));
    ImGui::SameLine();
    if (chip(TR("the start position"), !s.from_board, "play_start") && s.from_board) {
        s.from_board = false;
        changed      = true;
    }
    ImGui::SameLine(0, px(4));
    if (chip(TR("the Board's position"), s.from_board, "play_board") && !s.from_board) {
        s.from_board = true;
        changed      = true;
    }
    tip(boardFen());
    ui::endCard();
}

void App::drawPlayHandicap(PlaySettings& s, bool& changed) {
    ui::beginCard("##play_handicap");
    heading(TR("The handicap"));
    ImGui::TextDisabled("%s", TR("What the engine gives at the start:"));
    const auto engine_side = s.colour == 1 ? chess::Color::WHITE : chess::Color::BLACK;
    bool first             = true;
    for (const auto h : {Handicap::None, Handicap::Pawn, Handicap::Knight, Handicap::Rook, Handicap::Queen}) {
        auto trial     = s;
        trial.handicap = h;
        const bool can =
            h == Handicap::None || (!playStartFen(trial, engine_side, nullptr).empty() &&
                                    (s.colour != 2 || !playStartFen(trial, chess::Color::WHITE, nullptr).empty()));
        const char* label = handicapName(h);
        if (!first) ui::sameLineOrWrap(ImGui::CalcTextSize(label).x + px(24), px(4));
        first = false;
        ImGui::BeginDisabled(!can);
        if (chip(label, s.handicap == h, fmt::format("play_handicap{}", static_cast<int>(h)).c_str()) &&
            s.handicap != h) {
            s.handicap = h;
            changed    = true;
        }
        ImGui::EndDisabled();
        if (!can) tip(TR("Not from this position: the engine has no such man where it starts, or it is Chess960."));
    }
    if (ImGui::Checkbox(TR("Taking a move back: allowed, counted (Ctrl+Z)"), &s.takebacks)) changed = true;
    if (ImGui::Checkbox(trf("Hints: {} a game (Alt+H)", HINTS).c_str(), &s.hints)) changed = true;
    tip(TR("The analysis engine's move after a second's search, as an arrow: counted, and written in the game."));
    ImGui::PushTextWrapPos();
    wrapped(TR("Unequal time is a handicap too. Everything is written in the game: its start, the takebacks, the "
               "hints."),
            true);
    ImGui::PopTextWrapPos();
    ui::endCard();
}

// ---------------------------------------------------------------------------
// The page offered once

bool App::playOffered() const {
    for (const auto& v : views_)
        if (v.holds(views::PageId::Play)) return true;
    const auto offered = storage_.pagesOffered();
    return std::find(offered.begin(), offered.end(), "play") != offered.end();
}

void App::answerPlayOffer(bool add) {
    if (add && !views_.empty()) {
        auto& pages = views_[static_cast<std::size_t>(view_)].pages;
        // After Training when the view holds it, else after the Openings, else before Lichess, else before Settings.
        auto at = std::find(pages.begin(), pages.end(), views::PageId::Training);
        if (at == pages.end()) at = std::find(pages.begin(), pages.end(), views::PageId::Openings);
        if (at != pages.end()) {
            ++at;
        } else {
            at = std::find(pages.begin(), pages.end(), views::PageId::Lichess);
            if (at == pages.end()) at = std::find(pages.begin(), pages.end(), views::PageId::Settings);
        }
        pages.insert(at, views::PageId::Play);
        views::tidy(views_[static_cast<std::size_t>(view_)]);
        saveViews();
    }
    auto offered = storage_.pagesOffered();
    if (std::find(offered.begin(), offered.end(), "play") == offered.end()) offered.emplace_back("play");
    if (!options_.scripted) storage_.setPagesOffered(offered);
    play_offer_ = 1;
}

void App::drawPlayOffer() {
    if (play_offer_ < 0) play_offer_ = playOffered() ? 1 : 0;
    if (play_offer_ != 0 || views_.empty() || view().holds(views::PageId::Play)) return;
    if (training_offer_ == 0 && !view().holds(views::PageId::Training)) return;  // one offer at a time
    beginTinted("##play_offer", ui::ACCENT);
    ImGui::PushTextWrapPos();
    ui::inlineIcon(ui::Icon::Play, ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::SameLine();
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.05f);
    ImGui::TextUnformatted(TR("New: the Play page"));
    ImGui::PopFont();
    wrapped(TR("A game against an engine of your library at the strength you choose, with the openings of your "
               "databases, your repertoire, a player or a book, and a handicap if you like. Your view does not change "
               "unless you say so."),
            true);
    if (primaryButton(trf("Add it to {}", view().name).c_str())) answerPlayOffer(true);
    ImGui::SameLine();
    if (ImGui::Button(TR("Not now##play_offer"))) answerPlayOffer(false);
    ImGui::SameLine(0, px(10));
    ImGui::TextDisabled("%s", TR("Ctrl+K “Play against an engine” leads there meanwhile."));
    ImGui::PopTextWrapPos();
    endTinted();
    ImGui::Dummy(ImVec2(0, px(6)));
}

}  // namespace fcg
