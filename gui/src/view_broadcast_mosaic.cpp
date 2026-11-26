// The broadcast mosaic (mockup M4, board 4): a round followed on Lichess, its games side by side as in the Live
// mosaic, each analysed by an engine of the library, a thread or a few a game, while the mosaic shows. The moves come
// from Lichess's stream, opened again after a break; a game can be followed on the Board page, which moves on with it;
// once the whole round has ended, it goes into the « Broadcasts » database.

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <vector>

#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <study/game_tree.hpp>
#include <study/import.hpp>

#include "app.hpp"
#include "board_text.hpp"
#include "board_view.hpp"
#include "console.hpp"
#include "home.hpp"
#include "i18n.hpp"
#include "live.hpp"
#include "online_cache.hpp"
#include "pgn_store.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

bool playing(const std::string& result) { return result.empty() || result == "*"; }

// The clock a side had after its last move, from the game's [%clk]; empty when the broadcast sends none.
std::string clockOf(const ReplayGame& game, chess::Color side) {
    for (auto it = game.moves.rbegin(); it != game.moves.rend(); ++it)
        if (it->mover == side && it->clock_ms) return ui::formatClock(*it->clock_ms);
    return {};
}

// The engine's score for White, in pawns, and its line in the reader's notation.
struct Seen {
    std::optional<fastchess::Score> white;  // White's point of view
    std::string score, line;
    int64_t depth = 0;
};

Seen seenBy(const Analyser* analyser, const chess::Board& board) {
    Seen s;
    if (!analyser || analyser->lines().empty()) return s;
    const auto& l = analyser->lines().front();
    if (!l.score) return s;
    auto w = *l.score;
    if (board.sideToMove() == chess::Color::BLACK) w.value = -w.value;
    s.white = w;
    s.score = w.type == fastchess::ScoreType::MATE ? fmt::format("#{}", w.value)
                                                   : fmt::format("{:+.2f}", static_cast<double>(w.value) / 100.0);
    s.line  = localLine(pvToSan(board, l.pv, 4));
    s.depth = l.depth;
    return s;
}

// What the tables say of a position, for White: none when they do not know it.
const char* tablesSay(const std::optional<lichess::Outcome>& outcome, bool white_to_move) {
    if (!outcome || *outcome == lichess::Outcome::Unknown) return nullptr;
    const auto o   = *outcome;
    const bool win = o == lichess::Outcome::Win, loss = o == lichess::Outcome::Loss;
    if (o == lichess::Outcome::Draw || o == lichess::Outcome::CursedWin || o == lichess::Outcome::BlessedLoss)
        return TR("draw");
    if ((win && white_to_move) || (loss && !white_to_move)) return TR("White wins");
    if (win || loss) return TR("Black wins");
    return nullptr;  // a win or a loss the fifty moves may change: not said
}

}  // namespace

void App::openBroadcastMosaic(const std::string& round_id) {
    if (broadcast_round_ != round_id) {
        broadcast_boards_.clear();  // their engines end
        broadcast_selected_.clear();
    }
    broadcast_round_ = round_id;
    page_            = Page::Lichess;  // its view, or the view shown
}

void App::pollBroadcastBoards() {
    const FollowedRound* round = nullptr;
    for (const auto& f : followed_rounds_)
        if (f.key() == broadcast_round_) round = &f;
    const bool players = broadcast_round_ == lichess::PLAYERS_MOSAIC;  // the followed players' games (#50)
    const bool tv      = !tvChannelShown().empty();                    // a channel of Lichess TV's (#86)
    const bool tour    = tourMosaicShown();                            // a tournament of Lichess's (#95)
    if (!round && !players && !tv && !tour) {
        broadcast_boards_.clear();
        if (!broadcast_round_.empty()) broadcast_round_.clear();  // no longer followed: the page itself again
    }
    // The games, read again when Lichess sent them again.
    if (round || players || tv || tour) {
        std::vector<lichess::BroadcastGame> games;
        if (round) {
            games = round->follower->games();
        } else if (players) {
            if (players_follower_) games = players_follower_->games();
        } else if (tour) {
            // The first eight's games in the order of their places; a game that ended keeps its tile until the one
            // taking its place has come, so that the tiles do not move (as Lichess TV's).
            auto& f = *tour_follow_;
            if (f.follower) {
                const auto all  = f.follower->games();
                const auto find = [&](const std::string& url) {
                    return std::find_if(all.begin(), all.end(), [&](const auto& g) { return g.url == url; });
                };
                std::vector<std::string> slots;
                for (std::size_t i = 0; i < f.places.size(); ++i) {
                    const auto url     = fmt::format("https://lichess.org/{}", f.places[i]);
                    const bool kept_on = find(url) == all.end() && i < f.slots.size() && find(f.slots[i]) != all.end();
                    slots.push_back(kept_on ? f.slots[i] : url);
                }
                f.slots = std::move(slots);
                for (const auto& url : f.slots)
                    if (const auto it = find(url); it != all.end()) games.push_back(*it);
            }
        } else if (tv_follower_) {
            // The channel's games in the order of their places, not the Board's game nor those kept; a game that ended
            // keeps its tile until the one taking its place has come, so that the tiles do not move.
            const auto all  = tv_follower_->games();
            const auto find = [&](const std::string& url) {
                return std::find_if(all.begin(), all.end(), [&](const auto& g) { return g.url == url; });
            };
            std::vector<std::string> slots;
            for (std::size_t i = 0; i < tv_places_.size(); ++i) {
                const auto url     = fmt::format("https://lichess.org/{}", tv_places_[i]);
                const bool kept_on = find(url) == all.end() && i < tv_slots_.size() && find(tv_slots_[i]) != all.end();
                slots.push_back(kept_on ? tv_slots_[i] : url);
            }
            tv_slots_ = std::move(slots);
            for (const auto& url : tv_slots_)
                if (const auto it = find(url); it != all.end()) games.push_back(*it);
        }
        if (players || tv || tour)  // a game no longer followed leaves the mosaic
            std::erase_if(broadcast_boards_, [&](const auto& b) {
                return std::none_of(games.begin(), games.end(), [&](const auto& g) { return g.url == b.url; });
            });
        for (const auto& g : games) {
            auto it = std::find_if(broadcast_boards_.begin(), broadcast_boards_.end(),
                                   [&](const auto& b) { return b.url == g.url; });
            if (it == broadcast_boards_.end()) {
                broadcast_boards_.emplace_back();
                it      = broadcast_boards_.end() - 1;
                it->url = g.url;
            }
            if (it->version == g.version) continue;
            it->version   = g.version;
            it->round_id  = g.round_id;
            it->round     = g.round;
            it->white     = g.white;
            it->black     = g.black;
            it->white_elo = g.white_elo;
            it->black_elo = g.black_elo;
            it->result    = g.result;
            it->pgn       = g.pgn;
            if (auto game = parseReplay(g.pgn)) {
                it->game  = std::move(*game);
                it->board = boardAt(it->game, false, static_cast<int>(it->game.moves.size()));
            }
        }
        // A channel's tiles in the order of their places: a game that ended gives its place to another.
        if (tv || tour) {
            const auto& order = tour ? tour_follow_->slots : tv_slots_;
            const auto place  = [&](const BroadcastBoard& b) {
                return std::find(order.begin(), order.end(), b.url) - order.begin();
            };
            std::stable_sort(broadcast_boards_.begin(), broadcast_boards_.end(),
                             [&](const auto& x, const auto& y) { return place(x) < place(y); });
        }
        const bool chosen = std::any_of(broadcast_boards_.begin(), broadcast_boards_.end(),
                                        [&](const auto& b) { return b.url == broadcast_selected_; });
        if ((broadcast_selected_.empty() || ((players || tv || tour) && !chosen)) && !broadcast_boards_.empty())
            broadcast_selected_ = broadcast_boards_[0].url;
    }
    // Their engines: one each for the games still played, while the mosaic shows (its engine and threads a game).
    const bool shown        = ImGui::GetFrameCount() - broadcast_drawn_ < 3;
    const EngineEntry* with = nullptr;
    for (const auto& e : engines_)
        if (e.name == lichess_.broadcast_engine) with = &e;
    // As many games as half the cores the tournaments leave allow, the game chosen first: an open's round has a
    // hundred boards.
    const int threads = std::clamp(lichess_.broadcast_threads, 1, 8);
    int room          = std::max(1, (topology_.logical - tournamentThreads()) / 2 / threads);
    std::vector<BroadcastBoard*> order;
    for (auto& b : broadcast_boards_)
        if (b.url == broadcast_selected_) order.push_back(&b);
    for (auto& b : broadcast_boards_)
        if (b.url != broadcast_selected_) order.push_back(&b);
    for (auto* p : order) {
        auto& b = *p;
        if (!shown || !with || !playing(b.result) || room <= 0 || liveGameOn()) {
            b.analyser.reset();
            continue;
        }
        --room;
        if (!b.analyser) {
            b.analyser = std::make_unique<Analyser>();
            AnalysisSettings settings;
            settings.engine  = with->name;
            settings.threads = threads;
            settings.multipv = 1;
            b.analyser->start(*with, settings);
        }
        AnalysisPosition position;
        position.fen = b.game.start_fen.empty() ? std::string(chess::constants::STARTPOS) : b.game.start_fen;
        for (const auto& m : b.game.moves) position.moves.push_back(chess::uci::moveToUci(m.move, false));
        b.analyser->analyse(position);  // the same position: nothing happens
        b.analyser->poll();
    }
    // The tables' verdict for the games of 7 pieces or fewer, when the user said « always » to Lichess's tables (M4,
    // board 4): from the profile's answers, else asked, one position at a time, while the mosaic shows.
    if (broadcast_tb_job_.valid() && broadcast_tb_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const auto answer = broadcast_tb_job_.get();
        if (answer.value) keepTablebase(storage_.root(), broadcast_tb_fen_, *answer.value);
    }
    for (auto& b : broadcast_boards_) {
        const auto pieces = b.board.occ().count();
        if (pieces > 7 || pieces < 3 || lichess_.tablebase != 1) {
            b.tb.reset();
            continue;
        }
        const auto fen = b.board.getFen();
        if (fen == b.tb_fen && b.tb) continue;
        if (const auto known = cachedTablebase(storage_.root(), fen)) {
            b.tb_fen = fen;
            b.tb     = known->outcome;
        } else if (shown && !broadcast_tb_job_.valid()) {
            broadcast_tb_fen_ = fen;
            broadcast_tb_job_ = std::async(std::launch::async, [fen] { return lichess::tablebase(fen); });
        }
    }

    // A followed round whose games have all ended goes into the « Broadcasts » database, once, shown or not; a
    // tournament followed whole, each of its rounds as it ends.
    for (const auto& f : followed_rounds_) {
        const auto changes = f.follower->changes();
        auto& seen         = broadcast_checked_[f.key()];
        if (seen == changes) continue;
        seen = changes;
        std::map<std::string, std::vector<lichess::BroadcastGame>> rounds;  // its games by their round
        for (auto& g : f.follower->games()) {
            const auto id = f.whole && !g.round_id.empty() ? g.round_id : f.entry.round.id;
            rounds[id].push_back(std::move(g));
        }
        for (const auto& [id, games] : rounds) {
            if (broadcast_saved_.count(id)) continue;
            if (std::none_of(games.begin(), games.end(), [](const auto& g) { return playing(g.result); }))
                saveBroadcastRound(f, id, games);
        }
    }

    // The game the Board page follows: it moves on, unless the user changed it.
    if (board_broadcast_) {
        const BroadcastBoard* game = nullptr;
        for (const auto& b : broadcast_boards_)
            if (b.url == board_broadcast_->url) game = &b;
        for (const auto& f : followed_rounds_) {
            if (game || f.key() != board_broadcast_->round) continue;
            // Another round on the mosaic: read from its follower.
            for (const auto& g : f.follower->games()) {
                if (g.url != board_broadcast_->url || g.version == board_broadcast_->version) continue;
                if (auto replay = parseReplay(g.pgn)) {
                    BroadcastBoard b;
                    b.url     = g.url;
                    b.version = g.version;
                    b.game    = std::move(*replay);
                    auto& bb  = board_;
                    if (bb.tree.variations.empty() && bb.undo.empty()) {
                        const bool last = bb.ply >= static_cast<int>(bb.game.moves.size());
                        bb.tree         = GameTree{b.game, {}};
                        bb.line         = -1;
                        bb.game         = b.game;
                        if (last) bb.ply = static_cast<int>(bb.game.moves.size());
                        board_broadcast_->version = g.version;
                    }
                }
            }
        }
        // A game of Lichess TV's the mosaic does not show (another channel's, or the mosaic closed): from its follower.
        if (!game && isTvMosaic(board_broadcast_->round) && tv_follower_) {
            for (const auto& g : tv_follower_->games()) {
                if (g.url != board_broadcast_->url || g.version == board_broadcast_->version) continue;
                auto replay = parseReplay(g.pgn);
                if (!replay) continue;
                auto& bb = board_;
                if (!bb.tree.variations.empty() || !bb.undo.empty()) {  // the user's own changes: the game is theirs
                    board_broadcast_.reset();
                    break;
                }
                const bool last = bb.ply >= static_cast<int>(bb.game.moves.size());
                bb.game         = std::move(*replay);
                bb.tree         = GameTree{bb.game, {}};
                bb.line         = -1;
                if (last) bb.ply = static_cast<int>(bb.game.moves.size());
                bb.ply                    = std::min(bb.ply, static_cast<int>(bb.game.moves.size()));
                board_broadcast_->version = g.version;
            }
        }
        auto& bb = board_;
        if (game && board_broadcast_ && game->version != board_broadcast_->version) {
            // The user's own changes end the following: the game is theirs then.
            if (!bb.tree.variations.empty() || !bb.undo.empty()) {
                board_broadcast_.reset();
            } else {
                const bool last = bb.ply >= static_cast<int>(bb.game.moves.size());
                bb.game         = game->game;
                bb.tree         = GameTree{bb.game, {}};
                bb.line         = -1;
                if (last) bb.ply = static_cast<int>(bb.game.moves.size());
                bb.ply                    = std::min(bb.ply, static_cast<int>(bb.game.moves.size()));
                board_broadcast_->version = game->version;
            }
        }
    }
}

void App::saveBroadcastRound(const FollowedRound& round, const std::string& round_id,
                             const std::vector<lichess::BroadcastGame>& games_sent) {
    broadcast_saved_.insert(round_id);
    auto& s          = shelf();
    const auto name  = std::string(TR("Broadcasts"));
    std::size_t base = s.entries().size();
    for (std::size_t i = 0; i < s.entries().size(); ++i)
        if (!s.entries()[i].source && s.entries()[i].name == name) base = i;
    try {
        if (base == s.entries().size()) base = s.create(name);
        const auto db = s.open(base);
        if (!db || db->readOnly()) return;
        std::vector<study::ImportedGame> games;
        for (const auto& g : games_sent) {
            study::ImportedGame game;
            if (study::readImportedGame(g.pgn, game)) games.push_back(std::move(game));
        }
        const auto count = games.size();
        (void)db->append(std::move(games), false);
        s.rebuildTree(base);
        const auto round_name = round.whole && !games_sent.empty() && !games_sent.front().round.empty()
                                    ? trf("Round {}", games_sent.front().round)
                                    : round.entry.round.name;
        Console::instance().add(
            trf("{} · {}: the round's {} games saved in “{}”.", round.entry.tour.name, round_name, count, name), false);
    } catch (const std::exception& e) {
        Console::instance().add(trf("The round could not be saved in “{}”: {}", name, e.what()), true);
    }
}

void App::followWholeTournament(const std::string& key) {
    for (std::size_t i = 0; i < followed_rounds_.size(); ++i) {
        auto& f = followed_rounds_[i];
        if (f.key() != key || f.whole) continue;
        const auto whole_key = "tour:" + f.entry.tour.id;
        f.follower->stop();
        // Followed whole already: that one shows, the round's follow ends.
        if (std::any_of(followed_rounds_.begin(), followed_rounds_.end(),
                        [&](const auto& o) { return o.key() == whole_key; })) {
            followed_rounds_.erase(followed_rounds_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            f.whole    = true;
            f.follower = std::make_unique<lichess::RoundFollower>(
                f.entry.tour.id, f.with_token && lichess_account_ ? lichess_account_->token : std::string(), true);
        }
        broadcast_tour_round_.clear();
        openBroadcastMosaic(whole_key);
        return;
    }
}

void App::pollBroadcastStandings() {
    using namespace std::chrono_literals;
    auto& s = broadcast_standings_;
    if (s.job.valid() && s.job.wait_for(0s) == std::future_status::ready) {
        auto [players, teams] = s.job.get();
        s.players             = std::move(players);
        s.teams               = std::move(teams);
    }
    if (broadcast_search_job_.valid() && broadcast_search_job_.wait_for(0s) == std::future_status::ready)
        broadcast_found_ = broadcast_search_job_.get();
    // A round followed from a link: its tournament and its names, from Lichess.
    if (broadcast_named_job_.valid() && broadcast_named_job_.wait_for(0s) == std::future_status::ready) {
        auto [round_id, answer] = broadcast_named_job_.get();
        if (answer.value && !answer.value->tour.id.empty())
            for (auto& f : followed_rounds_)
                if (!f.whole && f.entry.round.id == round_id) {
                    const auto url = f.entry.round.url;
                    f.entry        = *answer.value;
                    if (f.entry.round.id.empty()) f.entry.round.id = round_id;
                    if (f.entry.round.url.empty()) f.entry.round.url = url;
                }
    }
    if (!broadcast_named_job_.valid())
        for (const auto& f : followed_rounds_) {
            if (!f.entry.tour.id.empty() || f.entry.round.url.empty() || broadcast_named_.count(f.entry.round.id))
                continue;
            broadcast_named_.insert(f.entry.round.id);
            const auto token = f.with_token && lichess_account_ ? lichess_account_->token : std::string();
            broadcast_named_job_ =
                std::async(std::launch::async, [id = f.entry.round.id, url = f.entry.round.url, token] {
                    return std::make_pair(id, lichess::broadcastRoundEntry(url, token));
                });
            break;
        }
    // The permission given: the search asked for made.
    if (broadcast_search_after_link_ && !lichess_link_ && lichess_account_ &&
        lichess_account_->has(lichess::SCOPE_STUDY_READ)) {
        broadcast_search_after_link_ = false;
        startBroadcastSearch();
    }
    // The standings of the tournament whose mosaic shows: asked again every two minutes while it shows.
    const FollowedRound* round = nullptr;
    for (const auto& f : followed_rounds_)
        if (f.key() == broadcast_round_) round = &f;
    const bool shown = round && !round->entry.tour.id.empty() && ImGui::GetFrameCount() - broadcast_drawn_ < 3;
    if (!shown || s.job.valid()) return;
    const auto now = std::chrono::steady_clock::now();
    if (s.tour == round->entry.tour.id && now - s.asked < 2min) return;
    if (s.tour != round->entry.tour.id) {
        s.players = {};
        s.teams   = {};
    }
    s.tour  = round->entry.tour.id;
    s.asked = now;
    s.job   = std::async(std::launch::async, [tour = s.tour] {
        return std::make_pair(lichess::broadcastPlayers(tour), lichess::teamStandings(tour));
    });
}

void App::drawBroadcastStandings() {
    const auto& s              = broadcast_standings_;
    const FollowedRound* round = nullptr;
    for (const auto& f : followed_rounds_)
        if (f.key() == broadcast_round_) round = &f;
    if (!round || s.tour != round->entry.tour.id) return;
    const auto* players = s.players.value ? &*s.players.value : nullptr;
    const auto* teams   = s.teams.value ? &*s.teams.value : nullptr;
    if ((!players || players->empty()) && (!teams || teams->empty())) {
        if (!s.players.error.empty()) ImGui::TextDisabled("%s", trf("Standings: {}", s.players.error).c_str());
        return;
    }
    ImGui::Dummy(ImVec2(0, px(6)));
    const ui::TopicRegion topic(guide::topics::BroadcastMore);  // around the card, closed after it
    ui::beginCard("broadcast_standings");
    ImGui::PushTextWrapPos();
    if (teams && !teams->empty()) {
        ImGui::TextUnformatted(TR("The teams"));
        ImGui::PushTextWrapPos(-1.0f);  // a number wraps in no column: the names are cut instead
        if (ImGui::BeginTable("##teams", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(TR("Team"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(TR("Match points"), ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn(TR("Game points"), ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();
            int rank = 0;
            for (const auto& team : *teams) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(fmt::format("{}. {}", ++rank, team.name).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(home::halfPoints(team.match_points).c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", home::halfPoints(team.game_points).c_str());
            }
            ImGui::EndTable();
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }
    if (players && !players->empty()) {
        const bool scored = std::any_of(players->begin(), players->end(), [](const auto& p) { return p.scored; });
        ImGui::TextUnformatted(scored ? TR("The standings") : TR("The players"));
        const std::size_t most = broadcast_all_players_ ? players->size() : std::min<std::size_t>(players->size(), 12);
        ImGui::PushTextWrapPos(-1.0f);
        if (ImGui::BeginTable("##players", scored ? 3 : 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(TR("Player"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(TR("Elo"), ImGuiTableColumnFlags_WidthFixed);
            if (scored) ImGui::TableSetupColumn(TR("Points"), ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();
            for (std::size_t i = 0; i < most; ++i) {
                const auto& p = (*players)[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const auto name = p.title.empty() ? p.name : fmt::format("{} {}", p.title, p.name);
                ImGui::TextUnformatted((p.rank > 0 ? fmt::format("{}. {}", p.rank, name) : name).c_str());
                if (ImGui::IsItemHovered()) {
                    std::string more = p.fed.empty() ? std::string() : p.fed;
                    if (!p.team.empty()) more += (more.empty() ? "" : " · ") + p.team;
                    if (p.performance > 0) more += (more.empty() ? "" : " · ") + trf("performance {}", p.performance);
                    if (p.rating_diff != 0) more += fmt::format("{}{:+}", more.empty() ? "" : " · ", p.rating_diff);
                    if (!more.empty()) ImGui::SetTooltip("%s", more.c_str());
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", p.rating > 0 ? std::to_string(p.rating).c_str() : "–");
                if (scored) {
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(p.scored ? trf("{} / {}", home::halfPoints(p.score), p.played).c_str()
                                                    : "–");
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopTextWrapPos();
        if (players->size() > 12 &&
            ImGui::SmallButton(broadcast_all_players_ ? TR("Fewer")
                                                      : trf("All the players ({})", players->size()).c_str()))
            broadcast_all_players_ = !broadcast_all_players_;
    }
    ImGui::TextDisabled("%s", TR("From Lichess, as the organisers give them, asked again every two minutes."));
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void App::followBroadcastOnBoard(const BroadcastBoard& game) {
    // A channel's game (#86): the Board goes on to the channel's next one when it ends.
    if (const auto channel = tvChannelShown(); !channel.empty()) {
        const auto id = game.url.substr(game.url.rfind('/') + 1);
        followTvOnBoard(channel, id, id == tv_listed_.featured,
                        fmt::format("{} – {}", tvName(game.white), tvName(game.black)),
                        lichess::tvClockText(game.game.header("TimeControl")), &game.game, game.version);
        return;
    }
    openOnBoard(game.game, static_cast<int>(game.game.moves.size()), false,
                fmt::format("{} – {}", game.white, game.black), false);
    BoardBroadcast follow;
    follow.round     = broadcast_round_;
    follow.url       = game.url;
    follow.version   = game.version;
    board_broadcast_ = std::move(follow);
}

void App::drawBoardBroadcastLine() {
    if (!board_broadcast_) return;
    auto& b = board_;
    ImGui::TextColored(ImVec4(0.82f, 0.29f, 0.23f, 1.0f), "%s", TR("Live from Lichess"));
    if (b.ply < static_cast<int>(b.game.moves.size()) || b.line >= 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Back to live"))) {
            b.line = -1;
            b.game = lineOf(b.tree, -1);
            b.ply  = static_cast<int>(b.game.moves.size());
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("The game went on meanwhile: the board goes to its last move."));
    }
}

void App::drawBroadcastMosaic() {
    broadcast_drawn_           = ImGui::GetFrameCount();
    const FollowedRound* round = nullptr;
    for (const auto& f : followed_rounds_)
        if (f.key() == broadcast_round_) round = &f;
    if (ImGui::SmallButton(TR("‹ Lichess"))) {
        broadcast_round_.clear();
        return;
    }
    const bool players = broadcast_round_ == lichess::PLAYERS_MOSAIC;  // the followed players' games (#50)
    const auto channel = tvChannelShown();                             // a channel of Lichess TV's (#86)
    const bool tv      = !channel.empty();
    const bool tour    = tourMosaicShown();  // a tournament of Lichess's (#95)
    if (!round && !players && !tv && !tour) return;
    if (round) mosaic_round_ = round->key();
    // Its sources (mockup M12, board 7): the rounds followed, the followed players, Lichess TV.
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("The mosaic"));
    const auto source = [&](const char* label, bool shown, const std::string& tooltip) {
        ImGui::SameLine();
        ImGui::TextDisabled("·");
        ImGui::SameLine();
        if (shown) ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
        const ImVec2 size(ImGui::CalcTextSize(label).x, 0);
        const bool clicked = ImGui::Selectable(label, shown, ImGuiSelectableFlags_None, size);
        if (shown) {
            const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(lo.x, hi.y), hi, ui::ACCENT, px(2));
            ImGui::PopFont();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip.c_str());
        return clicked && !shown;
    };
    if (source(TR("Broadcasts"), round != nullptr,
               followed_rounds_.empty() ? TR("No round followed: the Lichess page lists the broadcasts.")
                                        : TR("The rounds you follow."))) {
        const bool known = std::any_of(followed_rounds_.begin(), followed_rounds_.end(),
                                       [&](const auto& f) { return f.key() == mosaic_round_; });
        if (followed_rounds_.empty())
            broadcast_round_.clear();  // the page, its broadcasts
        else
            openBroadcastMosaic(known ? mosaic_round_ : followed_rounds_.front().key());
        return;
    }
    if (source(TR("Followed players"), players, TR("The games of the players you follow."))) {
        openPlayersMosaic();
        return;
    }
    if (source(TR("Lichess TV"), tv, TR("The games of a channel of Lichess TV."))) {
        openTvMosaic(lichess_.tv_channel);
        return;
    }
    if (source(TR("Lichess tournaments"), tour,
               tour_follow_ ? TR("The tournament you follow: its standing, the games of its first eight.")
                            : TR("No tournament followed: the Lichess page lists them."))) {
        if (tour_follow_) {
            openBroadcastMosaic(tour_follow_->key);
        } else {
            broadcast_round_.clear();  // the page, its card of the tournaments
            arena_card_y_ = -2;
        }
        return;
    }
    if (tour) {
        drawTourMosaicHead();
        if (!tour_follow_) return;  // stopped following
    } else if (tv) {
        // Its channels, as chips; the games it follows, Lichess's limit, the fair play.
        for (const auto key : lichess::tvChannels()) {
            if (key != lichess::tvChannels().front()) ImGui::SameLine();
            const bool on = key == channel;
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(12));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(on ? ui::SELECT : ui::CARD_BG));
            ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(on ? ui::ACCENT : ui::CARD_BD));
            const bool clicked = ImGui::SmallButton(fmt::format("{}##tv_{}", tvChannelName(key), key).c_str());
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            if (clicked && !on) {
                openTvMosaic(std::string(key));
                return;
            }
        }
        const auto live = std::count_if(broadcast_boards_.begin(), broadcast_boards_.end(),
                                        [](const auto& b) { return playing(b.result); });
        auto state      = trn("{} game in progress", "{} games in progress", static_cast<uint64_t>(live), live);
        if (liveGameOn()) state += " · " + std::string(TR("the engines rest during your game on Lichess"));
        state = "· " + state;
        ui::sameLineOrWrap(ImGui::CalcTextSize(state.c_str()).x);
        ImGui::TextDisabled("%s", state.c_str());
        if (tv_list_error_.failed()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", trf("· Lichess: {}", lichess::describe(tv_list_error_)).c_str());
        }
    } else if (players) {
        const auto live = std::count_if(broadcast_boards_.begin(), broadcast_boards_.end(),
                                        [](const auto& b) { return playing(b.result); });
        if (live > 0)
            ImGui::TextColored(ImVec4(0.82f, 0.29f, 0.23f, 1.0f), "%s",
                               trn("{} game live", "{} games live", static_cast<uint64_t>(live), live).c_str());
        const auto error = players_follower_ ? players_follower_->error() : std::string();
        if (!error.empty()) {
            if (live > 0) ImGui::SameLine();
            ImGui::TextDisabled("%s", trf("· Lichess: {}", error).c_str());
        }
    } else {
        ImGui::TextUnformatted(fmt::format("{} · {}", round->entry.tour.name,
                                           round->whole ? TR("the whole tournament") : round->entry.round.name)
                                   .c_str());
        ImGui::SameLine();
        const auto error = round->follower->error();
        if (round->follower->streaming())
            ImGui::TextColored(ImVec4(0.82f, 0.29f, 0.23f, 1.0f), "%s", TR("live"));
        else
            ImGui::TextDisabled("%s", error.empty()
                                          ? TR("connecting…")
                                          : trf("the stream broke ({}): it opens again by itself", error).c_str());
        // A round's tournament followed whole: its rounds one after another (#55).
        if (!round->whole && !round->entry.tour.id.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("The whole tournament"))) {
                followWholeTournament(round->key());
                return;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TR("Follows all the tournament's ongoing rounds, and each next one as it "
                                           "starts, in this mosaic."));
        }
    }

    // Analysed by: an engine of the library, its threads for each game; or none.
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TR("Analysed by"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(200));
    const char* none = TR("no engine");
    if (ImGui::BeginCombo("##broadcast_engine",
                          lichess_.broadcast_engine.empty() ? none : lichess_.broadcast_engine.c_str())) {
        if (ImGui::Selectable(none, lichess_.broadcast_engine.empty())) {
            lichess_.broadcast_engine.clear();
            saveLichessSettings();
        }
        for (const auto& e : engines_) {
            if (ImGui::Selectable(e.name.c_str(), e.name == lichess_.broadcast_engine)) {
                lichess_.broadcast_engine = e.name;
                for (auto& b : broadcast_boards_) b.analyser.reset();  // started again with it
                saveLichessSettings();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(100));
    int threads = lichess_.broadcast_threads;
    if (ImGui::InputInt("##broadcast_threads", &threads, 1, 1)) {
        lichess_.broadcast_threads = std::clamp(threads, 1, 8);
        for (auto& b : broadcast_boards_)
            if (b.analyser) {
                auto settings    = b.analyser->settings();
                settings.threads = lichess_.broadcast_threads;
                b.analyser->update(settings);
            }
        saveLichessSettings();
    }
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%s", trn("thread a game", "threads a game", static_cast<uint64_t>(lichess_.broadcast_threads)).c_str());
    ImGui::SameLine();
    if (round && ImGui::SmallButton(TR("Stop following"))) {
        for (std::size_t r = 0; r < followed_rounds_.size(); ++r) {
            if (followed_rounds_[r].key() != broadcast_round_) continue;
            followed_rounds_[r].follower->stop();
            followed_rounds_.erase(followed_rounds_.begin() + static_cast<std::ptrdiff_t>(r));
            break;
        }
        broadcast_boards_.clear();
        broadcast_round_.clear();
        return;
    }
    ImGui::Spacing();

    // A tournament followed whole: one of its rounds shown, the newest live one unless the user chose another.
    std::string shown_round;
    if (round && round->whole) {
        std::vector<std::pair<std::string, std::string>> rounds;  // id, number, in the order they came
        std::string live;
        for (const auto& b : broadcast_boards_) {
            if (std::none_of(rounds.begin(), rounds.end(), [&](const auto& r) { return r.first == b.round_id; }))
                rounds.emplace_back(b.round_id, b.round);
            if (playing(b.result)) live = b.round_id;
        }
        const auto number = [](const std::string& n) { return std::atoi(n.c_str()); };
        std::stable_sort(rounds.begin(), rounds.end(),
                         [&](const auto& x, const auto& y) { return number(x.second) < number(y.second); });
        const bool chosen =
            std::any_of(rounds.begin(), rounds.end(), [&](const auto& r) { return r.first == broadcast_tour_round_; });
        shown_round = chosen ? broadcast_tour_round_ : !live.empty() ? live : rounds.empty() ? "" : rounds.back().first;
        if (!rounds.empty()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", TR("The rounds"));
            for (const auto& [id, n] : rounds) {
                ImGui::SameLine();
                const bool on_now = std::any_of(broadcast_boards_.begin(), broadcast_boards_.end(),
                                                [&](const auto& b) { return b.round_id == id && playing(b.result); });
                const auto text   = on_now ? trf("{} · live", n.empty() ? "?" : n) : (n.empty() ? "?" : n);
                const auto label  = text + "##" + id;
                const ImVec2 size(ImGui::CalcTextSize(text.c_str()).x + px(12), ImGui::GetFrameHeight());
                if (ImGui::Selectable(label.c_str(), id == shown_round, 0, size))
                    broadcast_tour_round_ = id == live ? std::string() : id;  // the live one: following the next
            }
            ImGui::Spacing();
        }
    }
    const auto shown = [&](const BroadcastBoard& b) { return shown_round.empty() || b.round_id == shown_round; };
    // The game chosen, one of the round shown.
    if (!shown_round.empty()) {
        const auto chosen = std::find_if(broadcast_boards_.begin(), broadcast_boards_.end(),
                                         [&](const auto& b) { return b.url == broadcast_selected_; });
        if (chosen == broadcast_boards_.end() || !shown(*chosen)) {
            const auto first    = std::find_if(broadcast_boards_.begin(), broadcast_boards_.end(), shown);
            broadcast_selected_ = first == broadcast_boards_.end() ? std::string() : first->url;
        }
    }

    if (broadcast_boards_.empty() && tour) {
        const auto& f       = *tour_follow_;
        const bool started  = f.swiss ? f.swiss_info.value && f.swiss_info.value->state != lichess::TourState::Created
                                      : f.arena.value && f.arena.value->info.state != lichess::TourState::Created;
        const bool finished = f.swiss ? f.swiss_info.value && f.swiss_info.value->state == lichess::TourState::Finished
                                      : f.arena.value && f.arena.value->info.state == lichess::TourState::Finished;
        ImGui::PushTextWrapPos();
        if (f.quota == 0)
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               trf("Lichess lets an address follow {} games at once: the rounds and the players "
                                   "followed take them all now.",
                                   lichess::MOST_GAME_STREAMS)
                                   .c_str());
        else
            ImGui::TextDisabled("%s", finished   ? TR("It is over: its standing stays.")
                                      : !started ? TR("It has not started: the games of its first eight come here when "
                                                      "it does.")
                                      : f.swiss ? TR("No game of its first eight now: the next round's games come here "
                                                     "as it starts, three moves late.")
                                                : TR("None of its first eight plays now: their games come here as they "
                                                     "start, three moves late."));
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, px(6)));
        drawTourStanding();
        return;
    }
    if (broadcast_boards_.empty()) {
        if (tv && tv_room_ == 0)  // the rounds and the players followed hold the streams Lichess allows
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               trf("Lichess lets an address follow {} games at once: the rounds and the players "
                                   "followed take them all now.",
                                   lichess::MOST_GAME_STREAMS)
                                   .c_str());
        else if (tv && tv_list_error_.failed() && tv_listed_of_ != channel)
            ImGui::TextDisabled(
                "%s", tv_list_error_.net == lichess::NetError::Offline
                          ? TR("Lichess cannot be reached from here.")
                          : trf("Lichess cannot be reached from here: {}.", lichess::describe(tv_list_error_)).c_str());
        else
            ImGui::TextDisabled("%s", tv        ? TR("No game of the channel has come yet: they appear as Lichess "
                                                     "lists them, three moves late.")
                                      : players ? TR("None of them plays now: their games come here as they start, "
                                                     "three moves behind, as Lichess shows them.")
                                                : TR("No game of the round has come yet: they appear as Lichess sends "
                                                     "them."));
        return;
    }
    // A side as a tile names it: a channel's and a player's games with their ratings, Lichess's AI by its level.
    const auto side_name = [&](const BroadcastBoard& b, chess::Color color) {
        const auto& name = color == chess::Color::WHITE ? b.white : b.black;
        if (!players && !tv && !tour) return name;
        const int elo   = color == chess::Color::WHITE ? b.white_elo : b.black_elo;
        const auto said = tvName(name);
        return elo > 0 && said == name ? fmt::format("{} {}", name, elo) : said;
    };
    // The mosaic on the left, the game chosen and the round on the right.
    const float avail  = ImGui::GetContentRegionAvail().x;
    const float side_w = std::min(px(360), avail * 0.34f);
    const float grid_w = avail - side_w - px(12);
    const float room   = grid_w - ImGui::GetStyle().ScrollbarSize - px(2);  // the cards beside the scrollbar
    const int cols     = std::max(1, static_cast<int>(room / px(230)));
    const float card   = (room - px(8) * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    ImGui::BeginChild("##broadcast_grid", ImVec2(grid_w, 0));
    const float line = ImGui::GetTextLineHeight();
    std::vector<std::size_t> cards;  // those of the round shown
    // The crosstable of the rounds followed in place of the tiles (#92); Esc brings the tiles back.
    const bool crosstable = broadcast_crosstable_ && round;
    if (crosstable) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup) &&
            !ImGui::GetIO().WantTextInput)
            broadcast_crosstable_ = false;
        drawBroadcastCrosstable();
    } else {
        for (std::size_t i = 0; i < broadcast_boards_.size(); ++i)
            if (shown(broadcast_boards_[i])) cards.push_back(i);
    }
    for (std::size_t k = 0; k < cards.size(); ++k) {
        auto& b = broadcast_boards_[cards[k]];
        if (k % static_cast<std::size_t>(cols) != 0) ImGui::SameLine(0, px(8));
        const bool selected = b.url == broadcast_selected_;
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(selected ? ui::ACCENT : ui::CARD_BD));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(ui::CARD_BG));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(6), px(5)));
        const float inner = card - px(12);
        // Two names, the board, the bar, a line of text; their spacing and the card's padding.
        const float h = inner + line * 3 + px(4) + ImGui::GetStyle().ItemSpacing.y * 4 + px(5) * 2 + px(4);
        ImGui::BeginChild(fmt::format("##bcard{}", cards[k]).c_str(), ImVec2(card, h),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const auto name_row = [&](chess::Color color) {
            const auto name  = side_name(b, color);
            const auto clock = clockOf(b.game, color);
            const bool moves = playing(b.result) && b.board.sideToMove() == color;
            ImGui::PushStyleColor(ImGuiCol_Text, moves ? ui::ACCENT : ImGui::GetColorU32(ImGuiCol_Text));
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float end  = pos.x + inner - ImGui::CalcTextSize(clock.c_str()).x - px(6);
            ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), pos, ImVec2(end, pos.y + line), end, name.c_str(),
                                      nullptr, nullptr);
            ImGui::Dummy(ImVec2(end - pos.x, line));
            ImGui::PopStyleColor();
            if (!clock.empty()) {
                ImGui::SameLine(inner - ImGui::CalcTextSize(clock.c_str()).x + ImGui::GetStyle().WindowPadding.x);
                ImGui::TextDisabled("%s", clock.c_str());
            }
        };
        name_row(chess::Color::BLACK);
        BoardDecor decor;
        decor.eval_white = -1.0f;
        if (!b.game.moves.empty()) decor.last_move = b.game.moves.back().move;
        drawBoard(atlas_, b.board, inner, decor);
        name_row(chess::Color::WHITE);
        // The engine's view: a thin bar, its score and line; the result once the game ended.
        const auto seen = seenBy(b.analyser.get(), b.board);
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const auto pos = ImGui::GetCursorScreenPos();
            const float f =
                seen.white ? std::clamp(0.5f + ui::whitePawns(*seen.white, chess::Color::WHITE) / 16.0f, 0.0f, 1.0f)
                           : 0.5f;
            const float bar = px(4);
            dl->AddRectFilled(pos, ImVec2(pos.x + inner, pos.y + bar), IM_COL32(25, 25, 25, 255), bar);
            dl->AddRectFilled(pos, ImVec2(pos.x + inner * f, pos.y + bar), IM_COL32(236, 236, 236, 255), bar);
            dl->AddRect(pos, ImVec2(pos.x + inner, pos.y + bar), IM_COL32(130, 130, 130, 255), bar);
            ImGui::Dummy(ImVec2(inner, bar));
        }
        if (!playing(b.result)) {
            ImGui::TextUnformatted(b.result.c_str());
        } else if (const char* says = b.tb_fen == b.board.getFen()
                                          ? tablesSay(b.tb, b.board.sideToMove() == chess::Color::WHITE)
                                          : nullptr) {
            // The tables' verdict, for White: exact, before any engine's score.
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s",
                               trf("Tables (Lichess): {}", says).c_str());
        } else if (!seen.score.empty()) {
            ImGui::TextDisabled("%s", fmt::format("{}  {}", seen.score, seen.line).c_str());
        } else if (lichess_.broadcast_engine.empty()) {
            ImGui::TextDisabled(" ");
        } else {
            ImGui::TextDisabled("%s", b.analyser     ? TR("analysing…")
                                      : liveGameOn() ? TR("resting during your game on Lichess")
                                                     : TR("not analysed: the cores are taken"));
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemClicked()) broadcast_selected_ = b.url;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) followBroadcastOnBoard(b);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("A click chooses the game; a double click follows it on the board."));
    }
    ImGui::EndChild();
    ImGui::SameLine(0, px(12));

    // The game chosen, and the round.
    ImGui::BeginChild("##broadcast_side", ImVec2(side_w, 0));
    ImGui::PushTextWrapPos();
    const BroadcastBoard* chosen = nullptr;
    for (const auto& b : broadcast_boards_)
        if (b.url == broadcast_selected_) chosen = &b;
    if (chosen) {
        ui::beginCard("broadcast_chosen");
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(fmt::format("{} – {}", tvName(chosen->white), tvName(chosen->black)).c_str());
        const auto moves = chosen->game.moves.size();
        const auto move  = moves / 2 + 1;
        ImGui::TextDisabled("%s", playing(chosen->result) ? (chosen->board.sideToMove() == chess::Color::WHITE
                                                                 ? trf("playing · move {}, White to move", move)
                                                                 : trf("playing · move {}, Black to move", move))
                                                                .c_str()
                                                          : trf("ended · {}", chosen->result).c_str());
        // Its last moves, numbered.
        std::string last;
        const std::size_t from = moves > 12 ? moves - 12 : 0;
        auto at                = boardAt(chosen->game, false, static_cast<int>(from));
        for (std::size_t i = from; i < moves; ++i) {
            const auto& m = chosen->game.moves[i];
            if (at.sideToMove() == chess::Color::WHITE)
                last += fmt::format("{}.", at.fullMoveNumber());
            else if (i == from)
                last += fmt::format("{}…", at.fullMoveNumber());
            last += localSan(m.san.empty() ? chess::uci::moveToSan(at, m.move) : m.san) + " ";
            at.makeMove<true>(m.move);
        }
        if (from > 0) last = "… " + last;
        ImGui::TextUnformatted(last.c_str());
        if (ImGui::Button(TR("Follow on the board"))) followBroadcastOnBoard(*chosen);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tv ? TR("On the board, the game moves on with each move received, and the board "
                                            "goes on to the channel's next game when it ends; going back to analyse it "
                                            "lets it move on behind, “Back to live” brings it back.")
                                       : TR("On the board, the game moves on with each move received; going back to "
                                            "analyse it lets it move on behind, “Back to live” brings it back."));
        ImGui::SameLine();
        if (ImGui::Button(TR("Copy the PGN"))) ImGui::SetClipboardText(chosen->pgn.c_str());
        ImGui::PopTextWrapPos();
        ui::endCard();
        ImGui::Dummy(ImVec2(0, px(6)));
    }
    if (tour) {
        drawTourStanding();  // its standing, its players' sheets
    } else {
        ui::beginCard("broadcast_round");
        ImGui::TextUnformatted(tv ? TR("The channel's games") : players ? TR("Their games") : TR("The round"));
        if (ImGui::BeginTable("##round_games", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("##game", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##state", ImGuiTableColumnFlags_WidthFixed);  // as wide as its widest
            for (const auto& b : broadcast_boards_) {
                if (!shown(b)) continue;
                ImGui::PushID(b.url.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                auto state = playing(b.result) ? trf("move {}", b.game.moves.size() / 2 + 1) : b.result;
                if (const char* says = playing(b.result) && b.tb_fen == b.board.getFen()
                                           ? tablesSay(b.tb, b.board.sideToMove() == chess::Color::WHITE)
                                           : nullptr)
                    state += " · " + trf("tables: {}", says);
                if (ImGui::Selectable(fmt::format("{} – {}##round", tvName(b.white), tvName(b.black)).c_str(),
                                      b.url == broadcast_selected_, ImGuiSelectableFlags_SpanAllColumns))
                    broadcast_selected_ = b.url;
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    followBroadcastOnBoard(b);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", state.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ui::endCard();
    }
    // Its crosstable, made of the rounds followed, beside the standings Lichess gives (#92).
    if (round) {
        ImGui::Dummy(ImVec2(0, px(4)));
        if (ui::iconButton("broadcast_crosstable", broadcast_crosstable_ ? ui::Icon::Grid : ui::Icon::Chart,
                           broadcast_crosstable_ ? TR("The mosaic") : TR("Crosstable")))
            broadcast_crosstable_ = !broadcast_crosstable_;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", broadcast_crosstable_
                                        ? TR("The games' boards again (Esc).")
                                        : TR("The crosstable of the rounds followed, in place of the boards: the games "
                                             "being played, those to come, the live rating change."));
    }
    if (round) drawBroadcastStandings();  // the players' standings, a team event's (#55)
    ImGui::TextDisabled("%s", tour ? TR("The games of its first eight in progress, as the tournament gives them; a "
                                        "finished game leaves its place to the next. The moves come three moves late "
                                        "(Lichess's rule for spectators); eight games at most (eight streams an "
                                        "address); in orange, a streak's points (doubled). A click picks a game, a "
                                        "double click opens it on the Board; never your own game.")
                              : tv
                                  ? TR("The moves come three moves late: Lichess's rule for spectators. A finished "
                                       "game gives its place to another of the channel; a double click follows a game "
                                       "on the board. The analysis is made on your machine, with your engines; nothing "
                                       "is kept unless you keep it.")
                              : players ? TR("The moves come from Lichess, three moves behind, as it shows them to "
                                             "anyone; the analysis is made on your machine, with your engines. A game "
                                             "that ends stays here until another takes its place.")
                                        : TR("The moves come from Lichess's stream, opened again by itself after a "
                                             "break; the analysis is made on your machine, with your engines. A "
                                             "finished game stays here until the round ends, then the whole round "
                                             "goes into the “Broadcasts” database."));
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

}  // namespace fcg
