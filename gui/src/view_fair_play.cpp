// Fair play (#48, mockup M8, board "Fairplay"): the user's games in progress on Lichess, wherever they play them. A
// live game locks every aid on any position (Lichess forbids any help, a database's included), until it ends; a
// correspondence game locks the engine, the cloud, the tables and the assistant on the whole of a line that passes
// through its position, before it and after it, the tree of the user's databases staying open (Lichess allows a book
// and a database there). The games are
// looked at when the Home page shows, every half-minute while a live one lasts and every ten minutes while a game by
// correspondence does; with the right to read challenges, heard as they start and end. The Home page lists them, "your
// turn" first (board "Main").

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include <study/game_tree.hpp>

#include "app.hpp"
#include "console.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// A position's men and side to move, without its castling rights and en passant square, which FENs write differently:
// what tells a game's position apart here.
struct Men {
    std::array<uint64_t, 12> bb{};  // pawns to kings, White then Black
    bool white                        = true;
    bool operator==(const Men&) const = default;
};

Men menOf(const chess::Board& board) {
    Men m;
    m.white       = board.sideToMove() == chess::Color::WHITE;
    std::size_t i = 0;
    for (const auto type : {chess::PieceType::PAWN, chess::PieceType::KNIGHT, chess::PieceType::BISHOP,
                            chess::PieceType::ROOK, chess::PieceType::QUEEN, chess::PieceType::KING}) {
        m.bb[i++] = board.pieces(type, chess::Color::WHITE).getBits();
        m.bb[i++] = board.pieces(type, chess::Color::BLACK).getBits();
    }
    return m;
}

// The men of a FEN as Lichess writes it; none when it cannot be read (a variant's pockets end the placement).
std::optional<Men> menOf(std::string_view fen) {
    Men m;
    int rank = 7, file = 0;
    std::size_t at = 0;
    for (; at < fen.size() && fen[at] != ' ' && fen[at] != '['; ++at) {
        const char c = fen[at];
        if (c == '/') {
            if (file != 8 || rank == 0) return std::nullopt;
            --rank;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
        } else {
            static constexpr std::string_view LETTERS = "PNBRQKpnbrqk";
            const auto k                              = LETTERS.find(c);
            if (k == std::string_view::npos || file > 7) return std::nullopt;
            const std::size_t type = k % 6, black = k / 6;
            m.bb[type * 2 + black] |= uint64_t{1} << (rank * 8 + file);
            ++file;
        }
        if (file > 8) return std::nullopt;
    }
    if (rank != 0 || file != 8) return std::nullopt;
    while (at < fen.size() && fen[at] != ' ') ++at;
    if (at + 1 >= fen.size()) return std::nullopt;
    m.white = fen[at + 1] != 'b';
    return m;
}

const char* speedName(std::string_view key) {
    if (key == "ultraBullet" || key == "bullet") return TR("Bullet");
    if (key == "blitz") return TR("Blitz");
    if (key == "rapid") return TR("Rapid");
    if (key == "classical") return TR("Classical");
    return TR("Correspondence");
}

// A live game's clock ("2:41", "1:05:12"), or the time left to move by correspondence ("1 d 4 h left").
std::string timeText(const lichess::PlayingGame& g) {
    const auto s = g.seconds_left;
    if (s < 0) return {};
    if (g.live())
        return s >= 3600 ? fmt::format("{}:{:02}:{:02}", s / 3600, s % 3600 / 60, s % 60)
                         : fmt::format("{}:{:02}", s / 60, s % 60);
    if (s >= 86400) return trf("{} d {} h left", s / 86400, s % 86400 / 3600);
    if (s >= 3600) return trf("{} h {} min left", s / 3600, s % 3600 / 60);
    return trf("{} min left", s / 60);
}

std::string opponentText(const lichess::PlayingGame& g) {
    return g.opponent_rating > 0 ? fmt::format("{} ({})", g.opponent, g.opponent_rating) : g.opponent;
}

// A short label on a coloured ground, on the line of the text around it.
void chip(const char* text, ImU32 tone) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 pad(px(7), px(2));
    const ImVec2 at = ImGui::GetCursorScreenPos();
    auto* dl        = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(at.x, at.y - pad.y), ImVec2(at.x + size.x + pad.x * 2, at.y + size.y + pad.y),
                      tone ? tone : ImGui::GetColorU32(ImGuiCol_FrameBg), px(5));
    dl->AddText(ImVec2(at.x + pad.x, at.y), tone ? IM_COL32_WHITE : ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
    ImGui::Dummy(ImVec2(size.x + pad.x * 2, size.y));
}

// A card tinted with `tone`: the fair play's banners.
void beginBanner(const char* id, ImU32 tone) {
    const auto bg  = ImGui::ColorConvertU32ToFloat4(ui::CARD_BG);
    const auto t   = ImGui::ColorConvertU32ToFloat4(tone);
    const auto mix = [&](float k) {
        return ImVec4(bg.x + (t.x - bg.x) * k, bg.y + (t.y - bg.y) * k, bg.z + (t.z - bg.z) * k, 1.0f);
    };
    ImGui::PushStyleColor(ImGuiCol_ChildBg, mix(0.08f));
    ImGui::PushStyleColor(ImGuiCol_Border, mix(0.45f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(14), px(12)));
    ImGui::BeginChild(id, ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
}

void endBanner() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

}  // namespace

void App::pollLichessPlaying() {
    pollLichessFinished();
    if (!lichess_account_) {
        lichess_playing_.clear();
        lichess_events_.reset();
        lichess_playing_fresh_ = true;
        return;
    }
    // Another token (more rights, another account): the games asked again at once.
    if (lichess_playing_token_ != lichess_account_->token) {
        lichess_playing_token_ = lichess_account_->token;
        lichess_playing_asked_ = {};
        lichess_playing_fresh_ = true;
    }
    // Heard as they start and end, when the token may read challenges; Lichess sends the games in progress each time
    // the stream opens, so the list starts again then.
    const bool may_listen = lichess_account_->has(lichess::SCOPE_CHALLENGE_READ);
    if (lichess_events_ && (!may_listen || lichess_events_->token() != lichess_account_->token))
        lichess_events_.reset();
    if (may_listen && !lichess_events_)
        lichess_events_ = std::make_unique<lichess::EventListener>(lichess_account_->token);
    if (lichess_events_) {
        for (auto& e : lichess_events_->take()) {
            if (e.kind == lichess::GameEvent::Kind::Opened) {
                lichess_playing_.clear();
                continue;
            }
            std::erase_if(lichess_playing_, [&](const auto& g) { return g.id == e.game.id; });
            if (e.kind == lichess::GameEvent::Kind::Started)
                lichess_playing_.push_back(std::move(e.game));
            else
                finishLichessGame(e.game);
        }
    }
    const bool listening = lichess_events_ && !lichess_events_->refused();
    // Else asked: once at the start, when the Home page shows, every half-minute while a live game lasts (its end gives
    // the aids back), every ten minutes while a game by correspondence does; never twice in twenty seconds.
    if (lichess_playing_job_.valid() &&
        lichess_playing_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto answer = lichess_playing_job_.get();
        if (answer.value) {
            // Those no longer listed ended (#49).
            if (!lichess_playing_fresh_)
                for (const auto& g : lichess::endedGames(lichess_playing_, *answer.value)) finishLichessGame(g);
            lichess_playing_       = std::move(*answer.value);
            lichess_playing_fresh_ = false;
        }
    }
    const bool shown = std::exchange(lichess_playing_ask_now_, false);
    if (listening || lichess_playing_job_.valid()) return;
    const auto now   = std::chrono::steady_clock::now();
    const auto since = now - lichess_playing_asked_;
    const bool due   = lichess_playing_asked_.time_since_epoch().count() == 0 ||
                       (shown && since > std::chrono::seconds(20)) ||
                       (liveGameOn() && since > std::chrono::seconds(30)) ||
                       (!lichess_playing_.empty() && since > std::chrono::minutes(10));
    if (!due) return;
    lichess_playing_asked_ = now;
    lichess_playing_job_ =
        std::async(std::launch::async, [token = lichess_account_->token] { return lichess::playingGames(token); });
}

const lichess::PlayingGame* App::liveGameOn() const {
    for (const auto& g : lichess_playing_)
        if (g.live()) return &g;
    return nullptr;
}

int App::correspondencePly(const ReplayGame& line, bool chess960, int last, const lichess::PlayingGame** game) const {
    return playingPly(line, chess960, last, game, false);
}

int App::playingPly(const ReplayGame& line, bool chess960, int last, const lichess::PlayingGame** game,
                    bool live_too) const {
    std::vector<std::pair<Men, const lichess::PlayingGame*>> wanted;
    for (const auto& g : lichess_playing_)
        if (live_too || !g.live())
            if (const auto men = menOf(std::string_view(g.fen))) wanted.emplace_back(*men, &g);
    if (wanted.empty()) return -1;
    auto board    = boardAt(line, chess960, 0);
    const int end = std::min(last, static_cast<int>(line.moves.size()));
    for (int ply = 0;; ++ply) {
        const auto men = menOf(board);
        for (const auto& [w, g] : wanted) {
            if (w == men) {
                if (game) *game = g;
                return ply;
            }
        }
        if (ply >= end) return -1;
        board.makeMove(line.moves[static_cast<std::size_t>(ply)].move);
    }
}

const lichess::PlayingGame* App::fairPlayLock(bool whole_game) const {
    if (const auto* live = liveGameOn()) return live;
    const auto& b                    = board_;
    const lichess::PlayingGame* game = nullptr;
    // The line shown passes through a correspondence game's position: the whole of it, the moves that led there too
    // (the engine's line from the position before the opponent's move would give the answer to it).
    if (correspondencePly(b.game, b.chess960, INT32_MAX, &game) >= 0) return game;
    // The main line does: the whole game, and a position of it shown from a variation.
    if (correspondencePly(b.tree.main, b.chess960, INT32_MAX, &game) >= 0) {
        if (whole_game) return game;
        const auto shown = menOf(boardAt(b.game, b.chess960, b.ply));
        auto board       = boardAt(b.tree.main, b.chess960, 0);
        for (std::size_t ply = 0;; ++ply) {
            if (menOf(board) == shown) return game;
            if (ply >= b.tree.main.moves.size()) break;
            board.makeMove(b.tree.main.moves[ply].move);
        }
    }
    return nullptr;
}

const lichess::PlayingGame* App::toLichessLock(bool whole_game) const {
    const auto& b                    = board_;
    const lichess::PlayingGame* game = nullptr;
    if (playingPly(b.game, b.chess960, INT32_MAX, &game, true) >= 0) return game;
    if (playingPly(b.tree.main, b.chess960, INT32_MAX, &game, true) >= 0) {
        if (whole_game) return game;
        const auto shown = menOf(boardAt(b.game, b.chess960, b.ply));
        auto board       = boardAt(b.tree.main, b.chess960, 0);
        for (std::size_t ply = 0;; ++ply) {
            if (menOf(board) == shown) return game;
            if (ply >= b.tree.main.moves.size()) break;
            board.makeMove(b.tree.main.moves[ply].move);
        }
    }
    return nullptr;
}

void App::drawFairPlayBanner(bool whole_game) {
    const auto* g = fairPlayLock(whole_game);
    if (!g) return;
    const ui::TopicRegion topic(guide::topics::FairPlay);
    const ImU32 tone = g->live() ? ui::ACCENT : ui::WARN;
    beginBanner("fair_play", tone);
    ui::inlineIcon(ui::Icon::Lock, tone);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(0.0f);
    if (g->live()) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(tone), "%s",
                           TR("One of your games is being played on Lichess: the aids are locked"));
        ImGui::TextDisabled("%s",
                            trf("{} against {}. The engine, the tree, the cloud, the tables and the assistant come "
                                "back when the game ends, by themselves.",
                                speedName(g->speed), opponentText(*g))
                                .c_str());
    } else {
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(tone), "%s",
            trf("Correspondence game against {}: the engine is locked on this game", opponentText(*g)).c_str());
        ImGui::TextDisabled("%s", TR("Lichess allows an opening book and a database there: the tree of your databases "
                                     "stays open. The engine, the cloud, the tables and the assistant look at no "
                                     "position of this game, before the one it has reached or after it."));
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    endBanner();
}

void App::drawPlayingGames() {
    // The Home page just showed: the games looked at again, if that was a while ago.
    const int frame = ImGui::GetFrameCount();
    if (frame - lichess_playing_home_frame_ > 2) lichess_playing_ask_now_ = true;
    lichess_playing_home_frame_ = frame;
    if (lichess_playing_.empty()) return;
    const ui::TopicRegion topic(guide::topics::FairPlay);
    ImGui::Dummy(ImVec2(0, px(4)));
    ImGui::TextUnformatted(TR("My games in progress"));
    // Your turn first, then theirs, then the live games.
    const auto rank = [](const lichess::PlayingGame& g) { return g.live() ? 2 : g.my_turn ? 0 : 1; };
    std::vector<const lichess::PlayingGame*> games;
    for (const auto& g : lichess_playing_) games.push_back(&g);
    std::stable_sort(games.begin(), games.end(), [&](const auto* a, const auto* b) { return rank(*a) < rank(*b); });
    const auto& style = ImGui::GetStyle();
    for (const auto* p : games) {
        const auto& g = *p;
        ImGui::PushID(g.id.c_str());
        chip(g.live()    ? TR("Live")
             : g.my_turn ? TR("Your turn")
                         : TR("Their turn"),
             g.live()    ? ui::ACCENT
             : g.my_turn ? ui::WARN
                         : 0);
        ImGui::SameLine();
        ImGui::TextUnformatted(opponentText(g).c_str());
        ImGui::SameLine();
        std::string what = speedName(g.speed);
        if (g.move() > 0) what += " · " + trf("move {}", g.move());
        ImGui::TextDisabled("%s", what.c_str());
        // On the right: the clock, or the time left to move; the game on lichess.org, where it is played.
        const auto time   = g.live() || g.my_turn ? timeText(g) : std::string();
        const char* open  = TR("Open on lichess.org");
        const float right = (time.empty() ? 0.0f : ImGui::CalcTextSize(time.c_str()).x + style.ItemSpacing.x) +
                            ImGui::CalcTextSize(open).x + style.FramePadding.x * 2;
        ImGui::SameLine();
        if (const float avail = ImGui::GetContentRegionAvail().x; avail > right)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - right);
        else
            ImGui::NewLine();
        if (!time.empty()) {
            ImGui::TextDisabled("%s", time.c_str());
            ImGui::SameLine();
        }
        if (ImGui::SmallButton(open)) SDL_OpenURL(fmt::format("https://lichess.org/{}", g.full_id).c_str());
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, px(4)));
}

void App::drawListenOffer() {
    if (!lichess_account_ || lichess_account_->has(lichess::SCOPE_CHALLENGE_READ) || lichess_.listen_declined) return;
    const ui::TopicRegion topic(guide::topics::FairPlay);
    ui::beginCard("lichess_listen");
    ImGui::PushTextWrapPos();
    ImGui::TextUnformatted(TR("Know when one of your games starts and ends, wherever you play"));
    ImGui::TextDisabled("%s", TR("RookStudio listens to the starts and ends of your Lichess games while it is open, to "
                                 "lock its aids while one is played. It does not read your moves during a game, and "
                                 "plays none. One more permission: reading your challenges."));
    if (lichess_link_) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s",
                           TR("Waiting for Lichess: allow RookStudio in the browser's page, then come back here."));
    } else {
        if (ui::iconButton("lichess_listen_allow", ui::Icon::External, TR("Allow on Lichess…"))) {
            auto scopes = lichess_account_->scopes;
            scopes.push_back(lichess::SCOPE_CHALLENGE_READ);
            startLichessLink(scopes, lichess_account_->username);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Not now"))) {
            lichess_.listen_declined = true;
            saveLichessSettings();
        }
    }
    ImGui::TextDisabled("%s", TR("Without it: your games in progress are looked at when the Home page shows, and the "
                                 "lock comes then."));
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void App::finishLichessGame(const lichess::PlayingGame& game) {
    if (!lichess_account_) return;
    if (std::find(lichess_finished_ids_.begin(), lichess_finished_ids_.end(), game.id) != lichess_finished_ids_.end())
        return;
    lichess_finished_ids_.push_back(game.id);
    // Its database: "Lichess · account", made when missing; the player's sync writing in it brings the game itself.
    const auto name  = "Lichess · " + lichess_account_->username;
    const auto lower = [](std::string text) {
        for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    auto& s = shelf();
    std::optional<std::size_t> index;
    for (std::size_t i = 0; i < s.entries().size(); ++i)
        if (!s.entries()[i].source && lower(s.entries()[i].name) == lower(name)) index = i;
    FinishedGame f;
    f.game = game;
    f.base = name;
    std::shared_ptr<study::Database> db;
    try {
        if (!index) index = s.create(name);
        f.index = *index;
        f.base  = s.entries()[*index].name;
        if (!(lichess_sync_ && lichess_sync_->index == *index)) db = s.open(*index);
    } catch (const std::exception& e) {
        Console::instance().add(trf("Lichess: “{}” could not be made: {}", name, e.what()), true);
    }
    f.arrival = std::async(std::launch::async, [id = game.id, token = lichess_account_->token, db] {
        return lichess::bringGame(id, token, db.get());
    });
    lichess_finished_.push_back(std::move(f));
}

void App::pollLichessFinished() {
    const auto ready = [](auto& job) {
        return job.valid() && job.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    };
    for (auto& f : lichess_finished_) {
        if (ready(f.arrival)) {
            f.got = f.arrival.get();
            if (f.got->added && f.index < shelf().entries().size()) shelf().rebuildTree(f.index);
            if (!f.got->error.empty())
                Console::instance().add(trf("Lichess: your game against {}: {}", opponentText(f.game), f.got->error),
                                        true);
            // An aborted game is not worth a word; a finished one's review at once, when chosen, no live game going
            // on and the Board holding nothing that would be lost.
            if (!f.got->pgn.empty() && f.got->plies < 2) f.done = true;
            if (!f.done && !f.got->pgn.empty() && lichess_.review_finished && !liveGameOn() && boardFree()) {
                f.automatic = true;
                reviewFinishedGame(f);
            }
        }
        if (ready(f.again)) {
            auto again = f.again.get();
            openFinishedGame(f, again.pgn.empty() ? f.got->pgn : again.pgn);
        }
    }
    std::erase_if(lichess_finished_, [](const auto& f) { return f.done && !f.arrival.valid() && !f.again.valid(); });
}

bool App::boardFree() const {
    const auto& b = board_;
    return !b.changes.any() && !b.editor && !b.reviewing() && !(b.watching && b.match) && !b.play_open && !link_card_;
}

void App::reviewFinishedGame(FinishedGame& f) {
    if (!f.got || f.got->pgn.empty() || f.again.valid()) return;
    if (f.got->analysed || !lichess_account_) {
        openFinishedGame(f, f.got->pgn);
        return;
    }
    // Asked again: Lichess's analysis may have been asked for on lichess.org since the game ended.
    f.again = std::async(std::launch::async, [id = f.game.id, token = lichess_account_->token] {
        return lichess::bringGame(id, token, nullptr);
    });
}

void App::openFinishedGame(FinishedGame& f, const std::string& pgn) {
    const auto page = page_;
    readOnBoard(pgn, "Lichess");
    startGameReview();
    if (f.automatic) {  // where the user was, told where it runs
        page_ = page;
        Console::instance().add(
            trf("Lichess: your game against {} is over; its review runs on the Board page.", opponentText(f.game)));
    }
    f.done = true;
}

void App::drawFinishedBanner(bool home) {
    FinishedGame* shown = nullptr;
    for (auto& f : lichess_finished_)
        if (!f.done) shown = &f;
    if (!shown) return;
    auto& f = *shown;
    const ui::TopicRegion topic(guide::topics::FinishedGames);
    const auto against = opponentText(f.game);
    const bool came    = f.got && !f.got->pgn.empty();
    std::string title;
    if (came) {
        const auto moves  = (f.got->plies + 1) / 2;
        const auto result = f.got->result == '1'   ? std::string("1-0")
                            : f.got->result == '0' ? std::string("0-1")
                            : f.got->result == '=' ? std::string("½-½")
                                                   : std::string("*");
        title = trn("Your game against {} is over: {} in {} move", "Your game against {} is over: {} in {} moves",
                    moves, against, result, moves);
    } else {
        title = trf("Your game against {} is over", against);
    }
    std::string text;
    if (f.arrival.valid())
        text = TR("Lichess is sending it…");
    else if (f.again.valid())
        text = TR("Asking Lichess for its analysis…");
    else if (!came)
        text = trf("Lichess did not send it ({}).", f.got ? f.got->error : std::string());
    else if (!f.got->error.empty())
        text = trf("It could not go into “{}”: {}.", f.base, f.got->error);
    else
        text = f.got->analysed ? trf("It is in “{}”, with Lichess's analysis.", f.base) : trf("It is in “{}”.", f.base);
    if (came && !f.again.valid() && !liveGameOn()) text += std::string(" ") + TR("The aids are back.");
    const bool can_review = came && !f.again.valid();
    if (home) {  // one line on the Home page's Lichess card
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::GOOD), "%s", title.c_str());
        if (can_review) {
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Review it"))) reviewFinishedGame(f);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Not now"))) f.done = true;
        ImGui::Dummy(ImVec2(0, px(4)));
        return;
    }
    // On the Board page: Enter reviews it, Escape puts it aside.
    if (finishedCardTakesKeys() && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
            link_key_frame_ = ImGui::GetFrameCount();
            reviewFinishedGame(f);
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            link_key_frame_ = ImGui::GetFrameCount();
            f.done          = true;
            return;
        }
    }
    beginBanner("finished_game", ui::GOOD);
    ui::inlineIcon(ui::Icon::Check, ui::GOOD);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::GOOD), "%s", title.c_str());
    ImGui::TextDisabled("%s", text.c_str());
    ImGui::PopTextWrapPos();
    if (can_review) {
        if (ImGui::Button(TR("Review it"))) reviewFinishedGame(f);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("Enter"));
        ImGui::SameLine();
    } else if (!came && !f.arrival.valid()) {
        if (ImGui::SmallButton(TR("Open on lichess.org")))
            SDL_OpenURL(fmt::format("https://lichess.org/{}", f.game.full_id).c_str());
        ImGui::SameLine();
    }
    if (ImGui::SmallButton(TR("Not now"))) f.done = true;
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("Esc"));
    ImGui::EndGroup();
    endBanner();
}

}  // namespace fcg
