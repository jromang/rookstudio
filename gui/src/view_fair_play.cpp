// Fair play (#48, mockup M8, board "Fairplay"): the user's games in progress on Lichess, wherever they play them. A
// live game locks every aid on any position (Lichess forbids any help, a database's included), until it ends; a
// correspondence game locks the engine, the cloud, the tables and the assistant on its position and those that come
// from it, the tree of the user's databases staying open (Lichess allows a book and a database there). The games are
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
    if (!lichess_account_) {
        lichess_playing_.clear();
        lichess_events_.reset();
        return;
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
            if (e.kind == lichess::GameEvent::Kind::Started) lichess_playing_.push_back(std::move(e.game));
        }
    }
    const bool listening = lichess_events_ && !lichess_events_->refused();
    // Else asked: once at the start, when the Home page shows, every half-minute while a live game lasts (its end gives
    // the aids back), every ten minutes while a game by correspondence does; never twice in twenty seconds.
    if (lichess_playing_job_.valid() &&
        lichess_playing_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto answer = lichess_playing_job_.get();
        if (answer.value) lichess_playing_ = std::move(*answer.value);
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
    std::vector<std::pair<Men, const lichess::PlayingGame*>> wanted;
    for (const auto& g : lichess_playing_)
        if (!g.live())
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
    if (correspondencePly(b.game, b.chess960, b.ply, &game) >= 0) return game;
    if (whole_game && correspondencePly(b.tree.main, b.chess960, INT32_MAX, &game) >= 0) return game;
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
            trf("Correspondence game against {}: the engine is locked on its position", opponentText(*g)).c_str());
        ImGui::TextDisabled("%s", TR("Lichess allows an opening book and a database there: the tree of your databases "
                                     "stays open. The engine, the cloud, the tables and the assistant do not look at "
                                     "this position, nor at those that come from it."));
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

}  // namespace fcg
