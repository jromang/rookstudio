// The Chess space's Analysis tab, with Lichess when the user has chosen it (mockup M4, board 5): the endgame tables
// online up to seven pieces, beyond the tables on the disk, and the lines Lichess's cloud has already computed. Each
// asks first, once (R16): for this position, always, or not now; the answers are kept in the profile (the tables' for
// good, a position not changing; the cloud's for a month). The engine always answers too.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <json.hpp>

#include <tour/files.hpp>

#include "app.hpp"
#include "board_text.hpp"
#include "https.hpp"
#include "i18n.hpp"
#include "lichess_api.hpp"
#include "live.hpp"
#include "online_cache.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

template <typename T>
bool ready(const std::future<T>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// The position a FEN gives, without its counters: what the tables and the cloud answer about.
std::string positionOf(const std::string& fen) {
    std::string out;
    int fields = 0;
    for (const char c : fen) {
        if (c == ' ' && ++fields == 4) break;
        out += c;
    }
    return out;
}

std::filesystem::path cacheFile(const std::filesystem::path& root, const char* kind, const std::string& fen) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const unsigned char c : positionOf(fen)) h = (h ^ c) * 0x100000001b3ULL;
    return root / "lichess-cache" / kind / fmt::format("{:016x}.json", h);
}

std::optional<nlohmann::json> readCached(const std::filesystem::path& file, const std::string& fen, int64_t lasts) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    try {
        auto j = nlohmann::json::parse(in);
        if (j.value("position", std::string()) != positionOf(fen)) return std::nullopt;
        if (lasts > 0 && nowSeconds() - j.value("when", int64_t{0}) > lasts) return std::nullopt;
        return j;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void writeCached(const std::filesystem::path& file, const std::string& fen, nlohmann::json answer) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    answer["position"] = positionOf(fen);
    answer["when"]     = nowSeconds();
    try {
        tour::replaceFile(file, answer.dump());
    } catch (const std::exception&) {
    }
}

nlohmann::json opt(const std::optional<int>& v) { return v ? nlohmann::json(*v) : nlohmann::json(); }
std::optional<int> opt(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number_integer() ? std::optional<int>(it->get<int>()) : std::nullopt;
}

nlohmann::json tablebaseJson(const lichess::Tablebase& t) {
    nlohmann::json moves = nlohmann::json::array();
    for (const auto& m : t.moves)
        moves.push_back({{"uci", m.uci},
                         {"san", m.san},
                         {"outcome", static_cast<int>(m.outcome)},
                         {"dtz", opt(m.dtz)},
                         {"dtm", opt(m.dtm)},
                         {"dtc", opt(m.dtc)},
                         {"zeroing", m.zeroing},
                         {"checkmate", m.checkmate},
                         {"stalemate", m.stalemate}});
    return {{"outcome", static_cast<int>(t.outcome)},
            {"dtz", opt(t.dtz)},
            {"dtm", opt(t.dtm)},
            {"dtc", opt(t.dtc)},
            {"checkmate", t.checkmate},
            {"stalemate", t.stalemate},
            {"insufficient", t.insufficient_material},
            {"moves", moves}};
}

lichess::Tablebase tablebaseFrom(const nlohmann::json& j) {
    lichess::Tablebase t;
    t.outcome               = static_cast<lichess::Outcome>(j.value("outcome", 4));
    t.dtz                   = opt(j, "dtz");
    t.dtm                   = opt(j, "dtm");
    t.dtc                   = opt(j, "dtc");
    t.checkmate             = j.value("checkmate", false);
    t.stalemate             = j.value("stalemate", false);
    t.insufficient_material = j.value("insufficient", false);
    for (const auto& m : j.value("moves", nlohmann::json::array())) {
        lichess::TablebaseMove move;
        move.uci       = m.value("uci", std::string());
        move.san       = m.value("san", std::string());
        move.outcome   = static_cast<lichess::Outcome>(m.value("outcome", 4));
        move.dtz       = opt(m, "dtz");
        move.dtm       = opt(m, "dtm");
        move.dtc       = opt(m, "dtc");
        move.zeroing   = m.value("zeroing", false);
        move.checkmate = m.value("checkmate", false);
        move.stalemate = m.value("stalemate", false);
        t.moves.push_back(std::move(move));
    }
    return t;
}

nlohmann::json cloudJson(const lichess::CloudEval& c) {
    nlohmann::json lines = nlohmann::json::array();
    for (const auto& l : c.lines) lines.push_back({{"moves", l.moves}, {"cp", opt(l.cp)}, {"mate", opt(l.mate)}});
    return {{"depth", c.depth}, {"knodes", c.knodes}, {"lines", lines}};
}

lichess::CloudEval cloudFrom(const nlohmann::json& j) {
    lichess::CloudEval c;
    c.depth  = j.value("depth", 0);
    c.knodes = j.value("knodes", uint64_t{0});
    for (const auto& l : j.value("lines", nlohmann::json::array())) {
        lichess::CloudLine line;
        line.moves = l.value("moves", std::vector<std::string>{});
        line.cp    = opt(l, "cp");
        line.mate  = opt(l, "mate");
        c.lines.push_back(std::move(line));
    }
    return c;
}

std::string clockText(int64_t seconds) {
    const auto t = static_cast<std::time_t>(seconds);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char text[16];
    std::strftime(text, sizeof(text), "%H:%M", &tm);
    return text;
}

// A count of plies (Lichess's DTZ and DTM) as moves, said as the mockup says them.
int movesOf(int plies) { return (std::abs(plies) + 1) / 2; }

// A position of eight pieces Lichess's op1 tables know, by their server's own test: no castling, a white pawn with a
// black pawn ahead on its file, and more than a lone pawn on each side. They give its verdict and its distance to the
// next capture, promotion or mate (DTC), not to mate.
bool op1(const chess::Board& board) {
    if (board.occ().count() != 8 || !board.castlingRights().isEmpty()) return false;
    const auto black_pawns = board.pieces(chess::PieceType::PAWN, chess::Color::BLACK);
    bool opposed           = false;
    for (auto w = board.pieces(chess::PieceType::PAWN, chess::Color::WHITE); !w.empty() && !opposed;) {
        const int square = w.pop();
        for (int ahead = square + 8; ahead < 64 && !opposed; ahead += 8) opposed = black_pawns.check(ahead);
    }
    const auto more_than_lone_pawn = [&](chess::Color c) {
        const auto pawns = board.pieces(chess::PieceType::PAWN, c);
        const auto rest  = board.us(c).count() - pawns.count() - 1;  // without the king
        return pawns.count() > 1 || rest > 0;
    };
    return opposed && more_than_lone_pawn(chess::Color::WHITE) && more_than_lone_pawn(chess::Color::BLACK);
}

// Why Lichess gave nothing, said with what still works (M4, board 6).
std::string trouble(const std::string& error, const char* host) {
    const auto pause = HostGate::instance().pausedFor(host);
    if (pause.count() > 0) {
        const auto s = std::chrono::duration_cast<std::chrono::seconds>(pause).count();
        return trf("Too many requests in a short time: Lichess asked for a pause. Trying again in {}:{:02}.", s / 60,
                   s % 60);
    }
    return trf("Lichess cannot be reached from here ({}): the answers kept still show, your engine answers.", error);
}

}  // namespace

std::optional<lichess::Tablebase> cachedTablebase(const std::filesystem::path& root, const std::string& fen) {
    if (const auto cached = readCached(cacheFile(root, "tablebase", fen), fen, 0)) return tablebaseFrom(*cached);
    return std::nullopt;
}

void keepTablebase(const std::filesystem::path& root, const std::string& fen, const lichess::Tablebase& t) {
    writeCached(cacheFile(root, "tablebase", fen), fen, tablebaseJson(t));
}

namespace {}  // namespace

void App::drawBoardOnline() {
    if (!chessBoard()) return;
    auto& b          = board_;
    const auto board = boardAt(b.game, b.chess960, b.ply);
    const auto fen   = board.getFen();
    const double now = ImGui::GetTime();
    if (fen != online_seen_) {
        online_seen_  = fen;
        online_since_ = now;
    }
    const bool settled = now - online_since_ > 0.4;  // stepping through a game asks nothing on the way
    const int pieces   = static_cast<int>(board.occ().count());
    const auto root    = storage_.root();

    // The answers that came.
    if (ready(online_tb_job_)) {
        auto answer = online_tb_job_.get();
        // Kept for good, a verdict not changing; not a position the tables do not know (op1's eight pieces), which
        // they may know later.
        if (answer.value && answer.value->outcome != lichess::Outcome::Unknown)
            writeCached(cacheFile(root, "tablebase", online_tb_asked_), online_tb_asked_, tablebaseJson(*answer.value));
        online_tb_ = OnlineTablebase{online_tb_asked_, std::move(answer), nowSeconds()};
    }
    if (ready(online_cloud_job_)) {
        auto answer = online_cloud_job_.get();
        if (answer.value)
            writeCached(cacheFile(root, "cloud", online_cloud_asked_), online_cloud_asked_, cloudJson(*answer.value));
        else if (answer.error.empty())
            online_not_in_cloud_.insert(positionOf(online_cloud_asked_));
        online_cloud_ = OnlineCloud{online_cloud_asked_, std::move(answer), nowSeconds()};
    }
    if (ready(online_line_job_)) {
        const auto line = online_line_job_.get();
        if (line.value && !line.value->empty() && online_line_asked_ == fen) {
            BoardPreview preview;
            preview.ply   = b.ply;
            preview.at    = b.ply;
            preview.moves = *line.value;
            preview.label = TR("The tables' line");
            b.preview     = std::move(preview);
        }
    }

    // A first use: what would go, and three answers (M4, board 5).
    const auto consent = [&](const char* id, const char* what, const char* sent, int& choice, bool& not_now) {
        ui::beginCard(id);
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(what);
        ImGui::TextDisabled("%s", sent);
        if (ImGui::SmallButton(TR("Ask for this position"))) online_once_.insert(positionOf(fen));
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Always"))) {
            choice = 1;
            saveLichessSettings();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Not now"))) not_now = true;
        ImGui::TextDisabled("%s",
                            TR("“Always” can be changed on the Lichess page, under Online for the analysis, which "
                               "also says what is sent."));
        ImGui::PopTextWrapPos();
        ui::endCard();
        ImGui::Dummy(ImVec2(0, px(4)));
    };
    const bool once = online_once_.count(positionOf(fen)) > 0;

    // The tables, for seven pieces or fewer that those of the disk do not know, and the eight of op1 (#42).
    const bool tb_fits     = ((pieces >= 3 && pieces <= 7) || op1(board)) && !b.chess960;
    const bool local_knows = b.line < 0 && b.ply < static_cast<int>(b.tb.size()) && b.tb[b.ply] != -2;
    bool tables_shown      = false;
    bool op1_unknown       = false;  // eight pieces the op1 tables do not know: the cloud may
    if (tb_fits && !local_knows && lichess_.tablebase != 2) {
        if (lichess_.tablebase == 1 || once) {
            if (!online_tb_ || positionOf(online_tb_->fen) != positionOf(fen)) {
                const auto file = cacheFile(root, "tablebase", fen);
                if (const auto cached = readCached(file, fen, 0)) {
                    lichess::Answer<lichess::Tablebase> answer;
                    answer.value = tablebaseFrom(*cached);
                    online_tb_   = OnlineTablebase{fen, std::move(answer), cached->value("when", int64_t{0})};
                } else if (settled && !online_tb_job_.valid()) {
                    online_tb_asked_ = fen;
                    online_tb_job_   = std::async(std::launch::async, [fen] { return lichess::tablebase(fen); });
                }
            }
            if (online_tb_ && positionOf(online_tb_->fen) == positionOf(fen)) {
                // Eight pieces: the op1 tables know some of them only, and one they do not shows nothing.
                op1_unknown = pieces == 8 && online_tb_->answer.value &&
                              online_tb_->answer.value->outcome == lichess::Outcome::Unknown;
                if (!op1_unknown) {
                    tables_shown = true;
                    drawOnlineTablebase(board, pieces);
                }
            } else if (online_tb_job_.valid()) {
                ImGui::TextDisabled("%s", TR("Asking Lichess's tables…"));
            }
        } else if (!online_tb_not_now_) {
            consent(
                "online_tb_consent",
                pieces == 8
                    ? std::string(TR("Lichess's tables may know this ending: eight pieces with opposing "
                                     "pawns, their verdict and the moves to the next capture."))
                          .c_str()
                    : trf("Lichess's tables know this ending ({} pieces); those of your disk do not.", pieces).c_str(),
                TR("To ask, its FEN would go to tablebase.lichess.ovh, without an account. Nothing else."),
                lichess_.tablebase, online_tb_not_now_);
        }
    }

    // The cloud, for a position of standard chess the tables do not answer (more than seven pieces, or eight the op1
    // tables do not know).
    if (!tables_shown && (!tb_fits || op1_unknown) && !b.chess960 && lichess_.cloud != 2) {
        if (lichess_.cloud == 1 || once) {
            const bool known_empty = online_not_in_cloud_.count(positionOf(fen)) > 0;
            if (!known_empty && (!online_cloud_ || positionOf(online_cloud_->fen) != positionOf(fen))) {
                const auto file = cacheFile(root, "cloud", fen);
                if (const auto cached = readCached(file, fen, 30 * 24 * 3600)) {
                    lichess::Answer<lichess::CloudEval> answer;
                    answer.value  = cloudFrom(*cached);
                    online_cloud_ = OnlineCloud{fen, std::move(answer), cached->value("when", int64_t{0})};
                } else if (settled && !online_cloud_job_.valid()) {
                    online_cloud_asked_ = fen;
                    online_cloud_job_   = std::async(std::launch::async, [fen] { return lichess::cloudEval(fen, 3); });
                }
            }
            if (online_cloud_ && positionOf(online_cloud_->fen) == positionOf(fen)) drawOnlineCloud(board);
        } else if (!online_cloud_not_now_) {
            consent("online_cloud_consent", TR("Lichess's cloud may have computed this position already."),
                    TR("To ask, its FEN would go to lichess.org, without an account. Your engine answers either way."),
                    lichess_.cloud, online_cloud_not_now_);
        }
    }
}

void App::drawOnlineTablebase(const chess::Board& board, int pieces) {
    auto& b       = board_;
    const auto& a = online_tb_->answer;
    ui::beginCard("online_tb");
    ImGui::PushTextWrapPos();
    ImGui::TextUnformatted(TR("Endgame tables"));
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%s",
        trf("· {} pieces · Lichess · asked at {}, kept in your profile", pieces, clockText(online_tb_->when)).c_str());
    if (!a.value) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           trouble(a.error, "tablebase.lichess.ovh").c_str());
        if (ImGui::SmallButton(TR("Try again"))) online_tb_.reset();
        ImGui::PopTextWrapPos();
        ui::endCard();
        return;
    }
    const auto& t     = *a.value;
    const bool white  = board.sideToMove() == chess::Color::WHITE;
    const char* mover = white ? TR("White") : TR("Black");
    const char* other = white ? TR("Black") : TR("White");
    using O           = lichess::Outcome;
    std::string verdict;
    switch (t.outcome) {
        case O::Win:
            verdict = trf("{} wins", mover);
            break;
        case O::CursedWin:
            verdict = trf("{} wins, but a draw by the 50-move rule", mover);
            break;
        case O::MaybeWin:
            verdict = trf("{} wins, maybe: the 50-move rule and the game played do not settle it", mover);
            break;
        case O::Draw:
            verdict = TR("Draw");
            break;
        case O::BlessedLoss:
            verdict = trf("{} wins, but a draw by the 50-move rule", other);
            break;
        case O::MaybeLoss:
            verdict = trf("{} wins, maybe: the 50-move rule and the game played do not settle it", other);
            break;
        case O::Loss:
            verdict = trf("{} wins", other);
            break;
        default:
            verdict = TR("Not known");
    }
    if (t.checkmate) verdict = TR("Checkmate");
    if (t.stalemate) verdict = TR("Stalemate");
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.15f);
    ImGui::TextUnformatted(verdict.c_str());
    ImGui::PopFont();
    if (t.dtm) ImGui::TextDisabled("%s", trf("mate in {} moves at best", movesOf(*t.dtm)).c_str());
    if (t.dtz && *t.dtz != 0)
        ImGui::TextDisabled("%s", trf("{} moves at most before a capture or a pawn move", movesOf(*t.dtz)).c_str());
    // Eight pieces (op1): no distance to mate nor DTZ, the moves to the next conversion instead.
    const bool conversion = t.dtc && !t.dtz && !t.dtm;
    if (conversion)
        ImGui::TextDisabled(
            "%s", trf("{} moves at most before the next capture, promotion or mate", std::abs(*t.dtc)).c_str());
    // Each move, and what it leaves for the side that plays it (the tables give it for the other side).
    std::optional<std::string> play;
    if (!t.moves.empty() && ImGui::BeginTable("##online_tb_moves", 4, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn(TR("Move"));
        ImGui::TableSetupColumn(TR("What it leaves"));
        ImGui::TableSetupColumn(TR("Mate in"));
        ImGui::TableSetupColumn(conversion ? TR("Before a capture, a promotion or mate")
                                           : TR("Before a capture or a pawn move"));
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < t.moves.size() && i < 10; ++i) {
            const auto& m = t.moves[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::SmallButton(localSan(m.san).c_str())) play = m.uci;
            ImGui::PopID();
            ImGui::TableNextColumn();
            const char* leaves = m.checkmate                   ? TR("mate")
                                 : m.stalemate                 ? TR("stalemate")
                                 : m.outcome == O::Loss        ? TR("a win")
                                 : m.outcome == O::BlessedLoss ? TR("a win, drawn by the 50-move rule")
                                 : m.outcome == O::MaybeLoss   ? TR("a win, maybe")
                                 : m.outcome == O::Draw        ? TR("a draw")
                                 : m.outcome == O::Win         ? TR("a loss")
                                 : m.outcome == O::CursedWin   ? TR("a loss, drawn by the 50-move rule")
                                 : m.outcome == O::MaybeWin    ? TR("a loss, maybe")
                                                               : "?";
            ImGui::TextUnformatted(leaves);
            ImGui::TableNextColumn();
            if (m.dtm) ImGui::Text("%d", movesOf(*m.dtm));
            ImGui::TableNextColumn();
            if (m.zeroing)
                ImGui::TextDisabled("%s", TR("now"));
            else if (conversion && m.dtc)
                ImGui::Text("%d", std::abs(*m.dtc));
            else if (m.dtz && *m.dtz != 0)
                ImGui::Text("%d", movesOf(*m.dtz));
        }
        ImGui::EndTable();
    }
    if (t.outcome != O::Draw && !t.checkmate && !t.stalemate) {
        if (ImGui::SmallButton(TR("See the winning line on the board")) && !online_line_job_.valid()) {
            online_line_asked_ = board.getFen();
            online_line_job_ =
                std::async(std::launch::async, [fen = online_line_asked_] { return lichess::tablebaseMainline(fen); });
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("the best line to the end, previewed; a click on a move plays it"));
    }
    ImGui::TextDisabled("%s", TR("A win drawn by the 50-move rule: the mate takes more than 50 moves without a capture "
                                 "or a pawn move (a “cursed win”, the tables say); “maybe” when the rule and the "
                                 "game played do not settle it."));
    ImGui::PopTextWrapPos();
    ui::endCard();
    ImGui::Dummy(ImVec2(0, px(4)));
    if (play) {
        const auto move = chess::uci::uciToMove(board, *play);
        if (move != chess::Move::NO_MOVE) boardPlay(move);
    }
    (void)b;
}

void App::drawOnlineCloud(const chess::Board& board) {
    const auto& a = online_cloud_->answer;
    if (!a.value && a.error.empty()) return;  // a position the cloud has not computed says nothing
    ui::beginCard("online_cloud");
    ImGui::PushTextWrapPos();
    if (!a.value) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", trouble(a.error, "lichess.org").c_str());
        if (ImGui::SmallButton(TR("Try again"))) online_cloud_.reset();
        ImGui::PopTextWrapPos();
        ui::endCard();
        return;
    }
    const auto& c = *a.value;
    ImGui::TextUnformatted(TR("In Lichess's cloud"));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", trf("· depth {} · already computed by its servers", c.depth).c_str());
    for (std::size_t i = 0; i < c.lines.size(); ++i) {
        const auto& l    = c.lines[i];
        const auto score = l.mate ? fmt::format("#{}", *l.mate)
                           : l.cp ? fmt::format("{:+.2f}", static_cast<double>(*l.cp) / 100.0)
                                  : std::string("?");
        const auto line  = localLine(pvToSan(board, l.moves, 12));
        const auto label = fmt::format("{}  {}##cloud_{}", score, line, i);
        if (ImGui::Selectable(label.c_str())) {
            BoardPreview preview;
            preview.ply    = board_.ply;
            preview.at     = board_.ply;
            preview.moves  = l.moves;
            preview.label  = TR("Lichess's cloud");
            board_.preview = std::move(preview);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("A click shows the line on the board. Scores are White's."));
    }
    ImGui::TextDisabled("%s", TR("Sent: the position's FEN, without an account. A position Lichess has not computed "
                                 "says nothing here: your engine alone answers."));
    ImGui::PopTextWrapPos();
    ui::endCard();
    ImGui::Dummy(ImVec2(0, px(4)));
}

}  // namespace fcg
