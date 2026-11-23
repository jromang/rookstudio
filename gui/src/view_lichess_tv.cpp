// Lichess TV (#86, mockup M12, board 7): the Lichess page's card (each channel's featured game, read again while it
// shows, every 30 s at most; the variants' channels left out), a channel's games in the broadcasts' mosaic (a third
// source: the same tiles, an engine of the user's per game, a finished game giving its place to another of the
// channel), and a game on the Board, which goes on to the channel's next featured game when it ends unless the user
// stays on it, and goes into a database of theirs at its end when kept. No account; never the user's own games (their
// linked account's, their bot's), even featured; the game streams of this address shared with the rounds and the
// followed players, eight at most, as Lichess allows. Nothing is kept by itself.

#include <algorithm>
#include <chrono>

#include <fmt/include/fmt/format.h>
#include <imgui.h>

#include <study/game_tree.hpp>
#include <study/import.hpp>

#include "app.hpp"
#include "console.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "storage.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

using Clock = std::chrono::steady_clock;

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

template <typename T>
bool ready(const std::future<T>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string gameUrl(const std::string& id) { return fmt::format("https://lichess.org/{}", id); }

std::string tagOf(const std::string& pgn, std::string_view tag) {
    const auto key = fmt::format("[{} \"", tag);
    const auto at  = pgn.find(key);
    if (at == std::string::npos) return {};
    const auto start = at + key.size();
    const auto end   = pgn.find('"', start);
    return end == std::string::npos ? std::string() : pgn.substr(start, end - start);
}

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

}  // namespace

const char* App::tvChannelName(std::string_view key) {
    if (key == "best") return TR("Top rated");
    if (key == "bullet") return TR("Bullet");
    if (key == "blitz") return TR("Blitz");
    if (key == "rapid") return TR("Rapid");
    if (key == "classical") return TR("Classical");
    if (key == "ultraBullet") return TR("UltraBullet");
    if (key == "bot") return TR("Bots");
    if (key == "computer") return TR("Computer");
    return "";
}

std::string App::tvName(const std::string& name) {
    if (const int level = lichess::aiLevelOf(name); level > 0) return trf("Lichess AI, level {}", level);
    return name;
}

std::string App::tvChannelShown() const {
    if (!isTvMosaic(broadcast_round_)) return {};
    const auto channel = broadcast_round_.substr(lichess::TV_MOSAIC.size());
    return lichess::isTvChannel(channel) ? channel : std::string();
}

std::vector<std::string> App::ownLichessIds() {
    loadBot();  // the user's bot (#85) is theirs too
    std::vector<std::string> out;
    for (const auto* a : {lichess_account_ ? &*lichess_account_ : nullptr, bot_account_ ? &*bot_account_ : nullptr}) {
        if (!a) continue;
        const auto id = lower(a->username);
        if (!id.empty() && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    }
    return out;
}

void App::openTvMosaic(const std::string& channel) {
    if (!lichess::isTvChannel(channel)) return;
    if (lichess_.tv_channel != channel) {
        lichess_.tv_channel = channel;
        saveLichessSettings();
    }
    openBroadcastMosaic(std::string(lichess::TV_MOSAIC) + channel);
}

void App::followTvOnBoard(const std::string& channel, const std::string& game, bool featured, const std::string& title,
                          const std::string& clock, const ReplayGame* known, uint64_t version) {
    // What the follower has of it already, else nothing yet: the board fills as Lichess sends it.
    ReplayGame replay;
    const auto url = gameUrl(game);
    if (known) {
        replay = *known;
    } else if (tv_follower_) {
        for (const auto& g : tv_follower_->games())
            if (g.url == url)
                if (auto r = parseReplay(g.pgn)) {
                    replay  = std::move(*r);
                    version = g.version;
                }
    }
    const int ply = static_cast<int>(replay.moves.size());
    openOnBoard(std::move(replay), ply, false, title, false);
    BoardBroadcast follow;
    follow.round     = std::string(lichess::TV_MOSAIC) + channel;
    follow.url       = url;
    follow.version   = version;
    board_broadcast_ = std::move(follow);
    BoardTv tv;
    tv.channel  = channel;
    tv.game     = game;
    tv.featured = featured;
    tv.clock    = clock;
    board_tv_   = std::move(tv);
}

bool App::boardBackToMosaic() const {
    if (!board_broadcast_) return false;
    const auto& key = board_broadcast_->round;
    if (isTvMosaic(key)) return lichess::isTvChannel(key.substr(lichess::TV_MOSAIC.size()));
    if (key == lichess::PLAYERS_MOSAIC) return true;
    return std::any_of(followed_rounds_.begin(), followed_rounds_.end(), [&](const auto& f) { return f.key() == key; });
}

void App::backToMosaic() {
    if (!boardBackToMosaic()) return;
    const auto key = board_broadcast_->round;
    if (isTvMosaic(key))
        openTvMosaic(key.substr(lichess::TV_MOSAIC.size()));
    else
        openBroadcastMosaic(key);
    board_back_.reset();
}

void App::loadTvKept() {
    if (tv_kept_loaded_) return;
    tv_kept_loaded_ = true;
    const auto json = readJson<nlohmann::json>(storage_.root() / "lichess-tv.json");
    if (!json || !json->is_object()) return;
    if (const auto it = json->find("kept"); it != json->end() && it->is_array())
        for (const auto& k : *it) {
            if (!k.is_object()) continue;
            TvKept kept;
            kept.id     = k.value("id", std::string());
            kept.title  = k.value("title", std::string());
            kept.folder = k.value("folder", std::string());
            kept.base   = k.value("base", std::string());
            if (!kept.id.empty() && !kept.folder.empty()) tv_kept_.push_back(std::move(kept));
        }
}

void App::saveTvKept() {
    if (options_.scripted) return;
    auto kept = nlohmann::json::array();
    for (const auto& k : tv_kept_)
        kept.push_back({{"id", k.id}, {"title", k.title}, {"folder", k.folder}, {"base", k.base}});
    (void)writeJson(storage_.root() / "lichess-tv.json", nlohmann::json{{"kept", kept}});
}

void App::storeTvGame(const TvKept& kept, const std::string& pgn) {
    auto& s           = shelf();
    std::size_t index = s.entries().size();
    for (std::size_t i = 0; i < s.entries().size(); ++i)
        if (!s.entries()[i].source && s.entries()[i].folder == std::filesystem::path(kept.folder)) index = i;
    try {
        if (index == s.entries().size()) {
            // Gone from the list since: one of its name, made again, rather than a game lost.
            index = s.create(kept.base.empty() ? std::string("Lichess TV") : kept.base);
        }
        const auto db = s.open(index);
        if (!db || db->readOnly()) {
            Console::instance().add(
                trf("Lichess TV: “{}” could not be written: {} was not kept.", s.entries()[index].name, kept.title),
                true);
            return;
        }
        study::ImportedGame game;
        if (!study::readImportedGame(pgn, game)) return;
        std::vector<study::ImportedGame> one;
        one.push_back(std::move(game));
        (void)db->append(std::move(one), false);
        s.rebuildTree(index);
        tv_stored_[kept.id] = s.entries()[index].name;
        Console::instance().add(trf("Lichess TV: {} kept in “{}”.", kept.title, s.entries()[index].name), false);
        boardToast(trf("{} kept in “{}”", kept.title, s.entries()[index].name));
    } catch (const std::exception& e) {
        Console::instance().add(trf("Lichess TV: {} could not be kept: {}", kept.title, e.what()), true);
    }
}

void App::keepTvGame(const std::string& folder, const std::string& name) {
    if (!board_tv_) return;
    TvKept kept;
    kept.id     = board_tv_->game;
    kept.title  = board_.title;
    kept.folder = folder;
    kept.base   = name;
    if (kept.folder.empty()) {  // "Lichess TV", made when missing
        auto& s = shelf();
        try {
            std::size_t index = s.entries().size();
            for (std::size_t i = 0; i < s.entries().size(); ++i)
                if (!s.entries()[i].source && s.entries()[i].name == name) index = i;
            if (index == s.entries().size()) index = s.create(name);
            kept.folder = s.entries()[index].folder.string();
            kept.base   = s.entries()[index].name;
        } catch (const std::exception& e) {
            boardToast(e.what());
            return;
        }
    }
    lichess_.tv_keep_base = kept.folder;
    saveLichessSettings();
    std::erase_if(tv_kept_, [&](const auto& k) { return k.id == kept.id; });
    // Ended already: kept now.
    if (tv_follower_)
        for (const auto& g : tv_follower_->games())
            if (g.url == gameUrl(kept.id) && g.result != "*" && !g.result.empty()) {
                storeTvGame(kept, g.pgn);
                return;
            }
    tv_kept_.push_back(std::move(kept));
    saveTvKept();
}

void App::pollLichessTv() {
    using namespace std::chrono_literals;
    const auto now = Clock::now();
    loadTvKept();
    const auto own = ownLichessIds();

    // The card's answer; asked again while it shows, every 30 s at most.
    if (ready(tv_rows_job_)) {
        auto a = tv_rows_job_.get();
        if (a.value) {
            tv_rows_ = std::move(a);
        } else {  // the rows of before stay, the failure said
            tv_rows_.error  = a.error.empty() ? std::string("Lichess did not answer") : a.error;
            tv_rows_.status = a.status;
        }
    }
    const bool card_shown = ImGui::GetFrameCount() - tv_card_drawn_ < 3;
    if (card_shown && !tv_rows_job_.valid() &&
        (tv_rows_asked_.time_since_epoch().count() == 0 || now - tv_rows_asked_ >= 30s)) {
        tv_rows_asked_ = now;
        tv_rows_job_   = std::async(std::launch::async, [] { return lichess::tvRows(); });
    }

    // What the follower knows: the games over (ended, or not read), and the Board's.
    const auto games = tv_follower_ ? tv_follower_->games() : std::vector<lichess::BroadcastGame>{};
    const auto over  = tv_follower_ ? tv_follower_->over() : std::set<std::string>{};
    tv_over_.insert(over.begin(), over.end());
    for (const auto& id : tv_over_) tv_over_since_.emplace(id, now);  // the first time it is seen over
    const auto ended = [&](const std::string& id) {
        for (const auto& g : games)
            if (g.url == gameUrl(id)) return !g.result.empty() && g.result != "*";
        return false;
    };

    // The Board's game left for another one, or changed by the user: it is no longer followed.
    if (board_tv_ && (!board_broadcast_ || board_broadcast_->url != gameUrl(board_tv_->game))) board_tv_.reset();

    // The games kept: into their database once they end; one that could not be read is asked again, as is the Board's.
    for (std::size_t i = 0; i < tv_kept_.size();) {
        const auto& k                   = tv_kept_[i];
        const lichess::BroadcastGame* g = nullptr;
        for (const auto& game : games)
            if (game.url == gameUrl(k.id)) g = &game;
        if (g && !g->result.empty() && g->result != "*") {
            const auto kept = k;
            const auto pgn  = g->pgn;
            tv_kept_.erase(tv_kept_.begin() + static_cast<std::ptrdiff_t>(i));
            saveTvKept();
            storeTvGame(kept, pgn);
            continue;
        }
        if (over.count(k.id) && tv_follower_) tv_follower_->again(k.id);
        ++i;
    }
    if (board_tv_ && over.count(board_tv_->game) && !ended(board_tv_->game) && tv_follower_)
        tv_follower_->again(board_tv_->game);

    // The Board's game ended: on to the channel's next one, by itself, unless the user stays on it.
    if (board_tv_) {
        auto& t = *board_tv_;
        if (ready(t.next)) {
            auto a = t.next.get();
            if (a.value && !a.value->game.empty() && a.value->game != t.game) {
                const auto next = *a.value;
                const auto side = [](const lichess::TvPlayer& p) {
                    return p.ai > 0 ? trf("Lichess AI, level {}", p.ai) : p.name;
                };
                const auto title = fmt::format("{} – {}", side(next.white), side(next.black));
                // The page and the way back stay as they are: the board changes its game where it is.
                const auto page    = page_;
                const auto back    = board_back_;
                const auto channel = t.channel;
                followTvOnBoard(channel, next.game, next.featured, title, next.clock);
                page_       = page;
                board_back_ = back;
                return;  // the rest at the next frame, with the new game
            }
            ++t.tries;
            t.error   = a.value ? std::string() : a.error;
            t.next_at = now + 3s;
        }
        // Lichess chooses the channel's next featured game within seconds: asked again every 3 s, another of the
        // channel's games taken after four tries, given up after a minute.
        if (ended(t.game) && !t.stay && !t.next.valid() && t.tries < 20 && now >= t.next_at) {
            t.next = std::async(std::launch::async, [channel = t.channel, game = t.game, own, others = t.tries >= 4] {
                return lichess::askTvNext(channel, game, own, others);
            });
        }
    }

    // The mosaic's channel: its games asked when it opens, when one of them ends (every 10 s at most), and every 30 s
    // while there is room for more.
    const auto channel = tvChannelShown();
    if (ready(tv_list_job_)) {
        auto [of, a] = tv_list_job_.get();
        if (a.value) {
            tv_listed_    = std::move(*a.value);
            tv_listed_of_ = of;
            tv_list_error_.clear();
        } else {
            tv_list_error_ = a.error;
        }
    }
    if (channel.empty()) {  // asked afresh when it opens again
        tv_listed_of_.clear();
        tv_list_asked_of_.clear();
    }
    if (channel.empty() || tv_listed_of_ != channel) {
        tv_places_.clear();
        tv_slots_.clear();
    }

    // The game streams of this address: eight, a round followed taking one; the Board's game and those kept first.
    const std::size_t total = followed_rounds_.size() >= lichess::MOST_GAME_STREAMS
                                  ? 0
                                  : lichess::MOST_GAME_STREAMS - followed_rounds_.size();
    std::vector<std::string> fixed;  // the Board's game and those kept, going on
    if (board_tv_ && !ended(board_tv_->game) && !over.count(board_tv_->game)) fixed.push_back(board_tv_->game);
    for (const auto& k : tv_kept_)
        if (!over.count(k.id) && std::find(fixed.begin(), fixed.end(), k.id) == fixed.end()) fixed.push_back(k.id);
    const std::size_t held        = std::min(fixed.size(), total);
    const bool players            = broadcast_round_ == lichess::PLAYERS_MOSAIC;
    const std::size_t players_has = players_follower_ ? players_follower_->open() : 0;
    const std::size_t tv_has      = tv_follower_ ? tv_follower_->open() : 0;
    tv_quota_           = std::min(channel.empty() ? held : total, total > players_has ? total - players_has : 0);
    players_quota_      = std::min(players ? total - held : 0, total > tv_has ? total - tv_has : 0);
    std::size_t outside = 0;  // of the fixed ones, those not in the mosaic's places
    for (const auto& id : fixed)
        if (std::find(tv_places_.begin(), tv_places_.end(), id) == tv_places_.end()) ++outside;
    tv_room_ = tv_quota_ > outside ? tv_quota_ - outside : 0;

    if (!channel.empty()) {
        const bool some_over =
            std::any_of(tv_places_.begin(), tv_places_.end(), [&](const auto& id) { return tv_over_.count(id) > 0; });
        std::size_t going = 0;
        for (const auto& id : tv_places_) going += tv_over_.count(id) ? 0 : 1;
        const auto since = now - tv_list_asked_at_;
        const bool fresh = tv_list_asked_of_ != channel;
        if (!tv_list_job_.valid() && (fresh || (some_over && since >= 10s) || (going < tv_room_ && since >= 30s))) {
            tv_list_asked_of_ = channel;
            tv_list_asked_at_ = now;
            tv_list_job_ =
                std::async(std::launch::async, [channel] { return std::make_pair(channel, lichess::tvList(channel)); });
        }
        // A game that ended keeps its tile 5 s, its result shown, before another takes it.
        std::set<std::string> resting;
        for (const auto& [id, at] : tv_over_since_)
            if (now - at < 5s) resting.insert(id);
        if (tv_listed_of_ == channel)
            tv_places_ =
                lichess::tvPlaces(tv_places_, tv_over_, tv_listed_.games, tv_listed_.featured, own, tv_room_, resting);
    }
    // What is known to be over: those still listed or shown, the rest forgotten.
    std::erase_if(tv_over_, [&](const std::string& id) {
        const bool listed =
            std::any_of(tv_listed_.games.begin(), tv_listed_.games.end(), [&](const auto& g) { return g.id == id; });
        return !listed && std::find(tv_places_.begin(), tv_places_.end(), id) == tv_places_.end() && !over.count(id);
    });
    std::erase_if(tv_over_since_, [&](const auto& entry) { return !tv_over_.count(entry.first); });

    // The follower: the Board's game, those kept, then the mosaic's; none, and it stops.
    std::vector<std::string> wanted;
    const auto want = [&](const std::string& id) {
        if (!id.empty() && std::find(wanted.begin(), wanted.end(), id) == wanted.end()) wanted.push_back(id);
    };
    if (board_tv_) want(board_tv_->game);
    for (const auto& k : tv_kept_) want(k.id);
    for (const auto& id : tv_places_) want(id);
    if (wanted.empty()) {
        tv_follower_.reset();
        return;
    }
    if (!tv_follower_) tv_follower_ = std::make_unique<lichess::PlayersFollower>();
    tv_follower_->watch(wanted, tv_quota_);
}

void App::drawLichessTvCard() {
    tv_card_drawn_ = ImGui::GetFrameCount();
    const ui::TopicRegion topic(guide::topics::LichessTv);
    ui::beginCard("lichess_tv");
    ImGui::PushTextWrapPos();
    ui::inlineIcon(ui::Icon::Tv, ui::ACCENT);
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("Lichess TV"));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("· each channel's featured game"));
    const auto own = ownLichessIds();
    if (!tv_rows_.value) {
        ImGui::TextDisabled("%s", tv_rows_.error.empty()
                                      ? TR("Asking Lichess…")
                                      : trf("Lichess cannot be reached from here: {}.", tv_rows_.error).c_str());
    } else {
        const auto ai = [](const lichess::TvPlayer& p) { return trf("Lichess AI, level {}", p.ai); };
        float name_w  = 0;
        for (const auto key : lichess::tvChannels())
            name_w = std::max(name_w, ImGui::CalcTextSize(tvChannelName(key)).x);
        const float buttons_w = ImGui::CalcTextSize(TR("The mosaic")).x + ImGui::CalcTextSize(TR("The Board")).x +
                                ImGui::GetStyle().FramePadding.x * 4 + ImGui::GetStyle().ItemSpacing.x;
        if (ImGui::BeginTable("##tv_channels", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("##channel", ImGuiTableColumnFlags_WidthFixed, name_w);
            ImGui::TableSetupColumn("##game", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##go", ImGuiTableColumnFlags_WidthFixed, buttons_w);
            for (const auto& row : *tv_rows_.value) {
                const auto& f = row.featured;
                const auto& g = row.game;
                ImGui::PushID(f.channel.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(tvChannelName(f.channel));
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                // Fair play: the user's own game never shows, even featured.
                const bool mine = !g.id.empty() ? lichess::playedBy(g, own)
                                                : std::find(own.begin(), own.end(), f.user_id) != own.end();
                std::string white, black;
                if (mine) {
                    ImGui::TextDisabled("%s", TR("your own game: not shown"));
                } else if (!g.id.empty()) {
                    white            = lichess::tvSide(g.white, ai(g.white), false);
                    black            = lichess::tvSide(g.black, ai(g.black), false);
                    auto text        = fmt::format("{} – {}", lichess::tvSide(g.white, ai(g.white), true),
                                                   lichess::tvSide(g.black, ai(g.black), true));
                    const auto clock = lichess::tvClockText(g.initial, g.increment);
                    const auto speed = f.channel == "best" ? std::string(tvChannelName(g.speed)) : std::string();
                    if (!clock.empty()) text += " · " + (speed.empty() ? clock : fmt::format("{} {}", speed, clock));
                    ImGui::TextUnformatted(text.c_str());
                } else {  // its player followed only, Lichess not saying more
                    lichess::TvPlayer p;
                    p.name   = f.user;
                    p.title  = f.title;
                    p.rating = f.rating;
                    ImGui::TextUnformatted(lichess::tvSide(p, {}, true).c_str());
                }
                ImGui::TableNextColumn();
                if (ImGui::SmallButton(TR("The mosaic"))) openTvMosaic(f.channel);
                tip(
                    trf("The channel's games in progress in the mosaic, each analysed by your engine: {} at most "
                        "from this address.",
                        lichess::MOST_GAME_STREAMS));
                ImGui::SameLine();
                ImGui::BeginDisabled(mine);
                if (ImGui::SmallButton(TR("The Board"))) {
                    toStudyBoard();
                    const auto clock = lichess::tvClockText(g.initial, g.increment);
                    followTvOnBoard(f.channel, f.game, true,
                                    g.id.empty() ? f.user : fmt::format("{} – {}", white, black), clock);
                }
                ImGui::EndDisabled();
                tip(mine ? TR("Your own game: it is never shown here (fair play).")
                         : TR("The featured game on the Board, which follows it; when it ends, the channel goes on to "
                              "the next one."));
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (!tv_rows_.error.empty())
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               trf("Lichess: {}. The list shown was read earlier.", tv_rows_.error).c_str());
    }
    ImGui::TextDisabled("%s", TR("The variants' channels (Crazyhouse, Atomic…) are not shown. The list is read again "
                                 "when the card shows, every 30 s at most."));
    ImGui::TextDisabled("%s", TR("Asked of Lichess without an account: the channels, and the games you follow. Nothing "
                                 "is kept unless you keep it."));
    ImGui::PopTextWrapPos();
    ui::endCard();
}

void App::drawTvKeepMenu() {
    constexpr const char* kId = "##tv_keep";
    if (tv_keep_menu_) {
        tv_keep_menu_ = false;
        ImGui::OpenPopup(kId);
    }
    if (!ImGui::BeginPopup(kId)) return;
    auto& s                = shelf();
    const std::string name = "Lichess TV";
    ImGui::TextDisabled("%s", TR("Keep the game, at its end, in"));
    // The database kept into last first, then "Lichess TV" (made when missing), then the user's others.
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < s.entries().size(); ++i)
        if (!s.entries()[i].source) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const auto rank = [&](std::size_t i) {
            return s.entries()[i].folder == std::filesystem::path(lichess_.tv_keep_base) ? 0
                   : s.entries()[i].name == name                                         ? 1
                                                                                         : 2;
        };
        return rank(a) < rank(b);
    });
    const bool has_tv =
        std::any_of(order.begin(), order.end(), [&](std::size_t i) { return s.entries()[i].name == name; });
    std::optional<std::pair<std::string, std::string>> chosen;  // folder, name
    for (const auto i : order) {
        const auto& e = s.entries()[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(e.name.c_str(), false, 0, ImVec2(px(300), 0))) chosen = {e.folder.string(), e.name};
        if (e.folder == std::filesystem::path(lichess_.tv_keep_base)) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", TR("where you kept the last one"));
        }
        ImGui::PopID();
    }
    if (!has_tv) {
        if (ImGui::Selectable(trf("“{}”, a new database", name).c_str(), false, 0, ImVec2(px(300), 0)))
            chosen = {std::string(), name};
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    if (chosen) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    if (chosen) keepTvGame(chosen->first, chosen->second);
}

void App::drawTvBoardCard() {
    if (!board_tv_) return;
    auto& t = *board_tv_;
    const ui::TopicRegion topic(guide::topics::LichessTv);
    const lichess::BroadcastGame* g = nullptr;
    std::vector<lichess::BroadcastGame> games;
    if (tv_follower_) games = tv_follower_->games();
    for (const auto& game : games)
        if (game.url == gameUrl(t.game)) g = &game;
    const bool ended = g && !g->result.empty() && g->result != "*";
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BG, ui::ACCENT, 0.08f)));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(mix(ui::CARD_BD, ui::ACCENT, 0.55f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(14), px(10)));
    ImGui::BeginChild("##tv_board", ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
    ImGui::PushTextWrapPos();
    ui::inlineIcon(ui::Icon::Tv, ui::ACCENT);
    ImGui::SameLine();
    auto clock = t.clock;
    if (clock.empty() && g) clock = lichess::tvClockText(tagOf(g->pgn, "TimeControl"));
    const auto what =
        t.featured ? (clock.empty() ? trf("Lichess TV · {} · the featured game", tvChannelName(t.channel))
                                    : trf("Lichess TV · {} · the featured game, {}", tvChannelName(t.channel), clock))
                   : (clock.empty() ? trf("Lichess TV · {}", tvChannelName(t.channel))
                                    : trf("Lichess TV · {} · {}", tvChannelName(t.channel), clock));
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", what.c_str());
    ImGui::PopFont();
    // When it ends, what follows.
    std::string then;
    if (!g)
        then = TR("Its game on Lichess, connecting…");
    else if (ended && t.stay)
        then = trf("It ended: {}. The board stays on it.", g->result);
    else if (ended && t.next.valid())
        then = trf("It ended: {}. The channel's next game comes…", g->result);
    else if (ended && t.tries >= 20)
        then = trf("It ended: {}. The channel has no other game for now.", g->result);
    else if (ended)
        then = trf("It ended: {}. The channel's next game comes in a moment.", g->result);
    else if (t.stay)
        then = TR("When it ends, the board stays on it.");
    else
        then = TR("When it ends, the channel goes on to the next one, by itself.");
    ImGui::TextDisabled("%s", then.c_str());
    if (!t.error.empty() && ended && !t.stay)
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", trf("Lichess: {}", t.error).c_str());
    if (!g && tv_quota_ == 0)
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           trf("Lichess lets an address follow {} games at once: the rounds and the players followed "
                               "take them all now.",
                               lichess::MOST_GAME_STREAMS)
                               .c_str());
    if (ImGui::Checkbox(TR("Stay on this game"), &t.stay)) {
        t.tries   = 0;  // asked again from now on, when it goes on
        t.next_at = {};
    }
    tip(
        TR("Unticked, the board goes on to the channel's next featured game when this one ends; the game left stays a "
           "Ctrl+Tab away."));
    ImGui::TextDisabled("%s", TR("Lichess shows its moves three moves late, to anyone."));
    // Keep it, back to the mosaic.
    const auto kept = std::find_if(tv_kept_.begin(), tv_kept_.end(), [&](const auto& k) { return k.id == t.game; });
    if (const auto stored = tv_stored_.find(t.game); stored != tv_stored_.end()) {
        tv_keep_menu_ = false;  // kept already
        ImGui::TextUnformatted(trf("Kept in “{}”", stored->second).c_str());
    } else if (kept != tv_kept_.end()) {
        tv_keep_menu_ = false;
        ImGui::TextUnformatted(trf("Kept in “{}” at its end", kept->base).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Not any more"))) {
            tv_kept_.erase(kept);
            saveTvKept();
        }
    } else {
        if (ImGui::SmallButton(TR("Keep this game…"))) tv_keep_menu_ = true;
        tip(ended ? TR("Into a database of your choice, now.") : TR("Into a database of your choice, at its end."));
        drawTvKeepMenu();
        ImGui::SameLine();
        ImGui::TextDisabled(
            "%s", ended ? TR("in a database of your choice") : TR("in a database of your choice, at its end"));
    }
    if (boardBackToMosaic()) {
        if (ImGui::SmallButton(TR("Back to the mosaic · Esc"))) backToMosaic();
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

}  // namespace fcg

namespace fcg {

void App::drawTvEngineLine(const EngineEntry& e) {
    // The Bots and Computer channels interest the tests too (M12, board 7): Lichess's best bots, this engine beside
    // them, in the mosaic.
    const ui::TopicRegion topic(guide::topics::LichessTv);
    ui::inlineIcon(ui::Icon::Tv, ui::MUTED);
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("Lichess TV"));
    for (const char* key : {"bot", "computer"}) {
        ImGui::SameLine();
        if (ImGui::SmallButton(fmt::format("{}##tv_engine_{}", tvChannelName(key), key).c_str())) {
            if (lichess_.broadcast_engine != e.name) {
                lichess_.broadcast_engine = e.name;
                for (auto& b : broadcast_boards_) b.analyser.reset();  // started again with it
                saveLichessSettings();
            }
            openTvMosaic(key);
        }
        tip(std::string_view(key) == "bot"
                ? trf("The games of Lichess TV's Bots channel in the mosaic, each analysed by {} on your machine; no "
                      "account needed.",
                      e.name)
                : trf("The games of Lichess TV's Computer channel, players against Lichess's AI, in the mosaic, each "
                      "analysed by {} on your machine; no account needed.",
                      e.name));
    }
    const char* summary = TR("Lichess's best bots, this engine analysing beside them");
    ui::sameLineOrWrap(ImGui::CalcTextSize(summary).x);
    ImGui::TextDisabled("%s", summary);
}

void App::followTvFeatured(const std::string& channel) {
    if (!tv_rows_.value) return;
    const auto own = ownLichessIds();
    for (const auto& row : *tv_rows_.value) {
        if (row.featured.channel != channel) continue;
        const auto& g = row.game;
        if (g.id.empty() ? std::find(own.begin(), own.end(), row.featured.user_id) != own.end()
                         : lichess::playedBy(g, own))
            return;  // the user's own game, never shown
        const auto side = [](const lichess::TvPlayer& p) {
            return p.ai > 0 ? trf("Lichess AI, level {}", p.ai) : p.name;
        };
        followTvOnBoard(channel, row.featured.game, true,
                        g.id.empty() ? row.featured.user : fmt::format("{} – {}", side(g.white), side(g.black)),
                        lichess::tvClockText(g.initial, g.increment));
        return;
    }
}

}  // namespace fcg
