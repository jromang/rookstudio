// A Lichess bot for one of the user's engines (#85, mockup M12, board 6): on the Engines page, the engine's card gets
// "On Lichess, as a bot": the bot's account (a second Lichess account, new and game-less, that RookStudio cannot
// create: a link to lichess.org), linked through Lichess's page with bot:play only, or by a token pasted; its checks
// before any request; the upgrade, irreversible, once its name is typed; its rules; online and offline. Online, its
// own event stream brings the challenges, accepted or declined with Lichess's reason (lichess_bot.hpp); its game is
// played by the engine, followed on the Board with the engine's search, and brought into "Lichess · <bot>" at its end
// (#49's way). It shows where what runs shows: the Home page and the Tournaments page (R3). A tournament comes first
// (R6): the bot declines "later" while it needs the cores. During the user's own live game, the bot's search is not
// shown (#48's lock); its games go on.

#include <algorithm>
#include <chrono>
#include <ctime>
#include <string_view>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <engine/uci_engine.hpp>
#include <study/game_tree.hpp>

#include "app.hpp"
#include "board_page.hpp"
#include "board_text.hpp"
#include "console.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "live.hpp"
#include "memory.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

using SteadyClock = std::chrono::steady_clock;

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

constexpr const char* LICHESS_SIGNUP = "https://lichess.org/signup";
// Lichess's page that makes a token, the bot's permission ticked (the Bot API's documentation gives it).
constexpr const char* LICHESS_BOT_TOKEN =
    "https://lichess.org/account/oauth/token/create?scopes[]=bot:play&description=RookStudio+bot";

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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(12)));
    ImGui::BeginChild(id, ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
}

void endTinted() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

bool primaryButton(const char* label, bool enabled = true, ImU32 tone = 0) {
    if (!tone) tone = ui::ACCENT;
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(tone));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImGui::ColorConvertU32ToFloat4(mix(tone, IM_COL32(255, 255, 255, 255), 0.15f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImGui::ColorConvertU32ToFloat4(mix(tone, IM_COL32(0, 0, 0, 255), 0.15f)));
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

void coloured(const std::string& text, ImU32 tone) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(tone));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

void heading(const char* text, float scale = 1.0f) {
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * scale);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

// A chip that turns on and off: a speed taken or not.
bool toggleChip(const char* label, bool on, const char* id) {
    const auto& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(12));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), style.FramePadding.y * 0.8f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImGui::ColorConvertU32ToFloat4(on ? mix(ui::CARD_BG, ui::ACCENT, 0.30f) : ui::CARD_BG));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(on ? ui::ACCENT : ui::CARD_BD));
    const bool clicked = ImGui::Button(fmt::format("{}##{}", label, id).c_str());
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    return clicked;
}

// A step of the setup: its number in a circle, green with a tick once done.
void stepMark(int n, bool done) {
    auto* dl        = ImGui::GetWindowDrawList();
    const float r   = ImGui::GetFontSize() * 0.66f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImVec2 c(at.x + r, at.y + r);
    dl->AddCircleFilled(c, r, done ? ui::GOOD : ui::ACCENT);
    const auto text = done ? std::string("\xE2\x9C\x93") : std::to_string(n);
    const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32_WHITE, text.c_str());
    ImGui::Dummy(ImVec2(r * 2, r * 2));
}

const char* speedName(std::string_view key) {
    if (key == "ultraBullet") return TR("UltraBullet");
    if (key == "bullet") return TR("Bullet");
    if (key == "blitz") return TR("Blitz");
    if (key == "rapid") return TR("Rapid");
    if (key == "classical") return TR("Classical");
    return TR("Correspondence");
}

// What Lichess shows the challenger for a reason, in the interface's language.
std::string declineText(std::string_view key) {
    if (key == "later") return TR("later");
    if (key == "tooFast") return TR("too fast");
    if (key == "tooSlow") return TR("too slow");
    if (key == "timeControl") return TR("not this clock");
    if (key == "casual") return TR("casual only");
    if (key == "rated") return TR("rated only");
    if (key == "standard") return TR("standard chess only");
    if (key == "variant") return TR("not this variant");
    if (key == "noBot") return TR("no bots");
    if (key == "onlyBot") return TR("bots only");
    return TR("not now");
}

std::string today() {
    const auto now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[16];
    std::strftime(text, sizeof(text), "%Y-%m-%d", &local);
    return text;
}

// "created today", "created 3 days ago", from Lichess's createdAt.
std::string createdText(int64_t created_ms) {
    if (created_ms <= 0) return TR("created on lichess.org");
    const auto now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    const auto days = std::max<int64_t>(0, (now - created_ms) / 86'400'000);
    if (days == 0) return TR("created today");
    return trn("created {} day ago", "created {} days ago", days, days);
}

// "Casual blitz 3+2", from a game's state.
std::string gameKindText(const lichess::BoardState& s) {
    std::string clock;
    if (s.days > 0)
        clock = lichess::clockText({0, 0, s.days});
    else if (s.clock_ms >= 0)
        clock = fmt::format("{:g}+{}", static_cast<double>(s.clock_ms) / 60000.0, s.increment_ms / 1000);
    const auto speed = speedName(s.speed.empty() ? "rapid" : s.speed);
    return s.rated ? trf("Rated · {} {}", speed, clock) : trf("Casual · {} {}", speed, clock);
}

std::string clockOf(int64_t ms) {
    ms = std::max<int64_t>(0, ms);
    if (ms >= 3'600'000) return fmt::format("{}:{:02}:{:02}", ms / 3'600'000, ms / 60'000 % 60, ms / 1000 % 60);
    if (ms >= 20'000) return fmt::format("{}:{:02}", ms / 60'000, ms / 1000 % 60);
    return fmt::format("{}:{:02}.{}", ms / 60'000, ms / 1000 % 60, ms / 100 % 10);
}

bool whiteToMove(const lichess::BoardState& s) {
    const auto& fen    = s.initial_fen;
    const auto space   = fen.find(' ');
    const bool first_w = space == std::string::npos || space + 1 >= fen.size() || fen[space + 1] != 'b';
    return first_w == (s.moves.size() % 2 == 0);
}

}  // namespace

// ---------------------------------------------------------------------------
// The account, the settings

void App::loadBot() {
    if (bot_loaded_) return;
    bot_loaded_  = true;
    bot_account_ = lichess::loadAccount(storage_.root(), lichess::BOT_ACCOUNT_FILE);
    if (bot_account_ && !botSettings().facts.username.empty()) bot_facts_ = botSettings().facts;
}

lichess::BotSettings& App::botSettings() {
    if (!bot_settings_) {
        bot_settings_ = lichess::readBotSettings(botFolder());
        // Its words, until chosen: the interface's language's.
        if (!bot_settings_->words_set) {
            bot_settings_->greeting = TR("Have a good game!");
            bot_settings_->thanks   = TR("Thank you for the game!");
        }
    }
    return *bot_settings_;
}

void App::saveBotSettings() {
    if (bot_settings_ && !options_.scripted) (void)lichess::writeBotSettings(botFolder(), *bot_settings_);
}

bool App::botReady() const { return bot_account_ && bot_facts_ && bot_facts_->bot() && botEngine(); }

const EngineEntry* App::botEngine() const {
    if (!bot_settings_) return nullptr;
    for (const auto& e : engines_)
        if (e.name == bot_settings_->engine) return &e;
    return nullptr;
}

lichess::BotPlay App::botPlay() const {
    lichess::BotPlay play;
    if (const auto* e = botEngine()) play.engine = *e;
    if (!bot_settings_) return play;
    const auto& s = *bot_settings_;
    if (!s.book.empty() &&
        std::any_of(books_.begin(), books_.end(), [&](const BookEntry& b) { return b.path == s.book; }))
        play.book = s.book;
    // The tournaments' tables, unless the engine has its own path set.
    if (s.tables && !setup_.tb_paths.empty() && !play.engine.option("SyzygyPath")) play.syzygy = setup_.tb_paths;
    play.greeting = s.greeting;
    play.thanks   = s.thanks;
    return play;
}

void App::startBotLink() {
    if (bot_link_ || bot_check_.valid()) return;
    bot_error_.clear();
    bot_unanswered_ = false;
    auto link       = std::make_unique<BotLink>();
    link->cancel    = std::make_shared<std::atomic<bool>>(false);
    auto url        = std::make_shared<std::string>();
    auto opened     = std::make_shared<std::atomic<bool>>(true);
    link->url       = url;
    link->opened    = opened;
    link->result    = std::async(std::launch::async, [cancel = link->cancel, url, opened] {
        const auto linked = lichess::link(
            [url, opened](const std::string& page) {
                *url    = page;
                *opened = SDL_OpenURL(page.c_str());
                return opened->load();
            },
            {lichess::SCOPE_BOT_PLAY}, std::chrono::minutes(5), cancel.get());
        if (!linked.value) {
            lichess::BotTokenCheck c;
            c.error  = linked.error;
            c.status = linked.status;
            return c;
        }
        // The link told the token's name and scopes already: its account's facts only.
        lichess::BotTokenCheck c;
        c.linked   = true;
        c.account  = linked.value;  // revoked if it is refused
        auto facts = lichess::botAccount(linked.value->token, cancel.get());
        c.status   = facts.status;
        c.error    = facts.error;
        c.facts    = std::move(facts.value);
        return c;
    });
    bot_link_       = std::move(link);
}

void App::checkBotToken(std::string token) {
    if (bot_link_ || bot_check_.valid()) return;
    token.erase(0, token.find_first_not_of(" \t\r\n"));
    token.erase(token.find_last_not_of(" \t\r\n") + 1);
    if (token.empty()) return;
    bot_error_.clear();
    bot_unanswered_ = false;
    bot_check_      = std::async(std::launch::async, [token] { return lichess::checkBotToken(token); });
}

void App::keepBotAccount(const lichess::Account& account) {
    try {
        lichess::saveAccount(storage_.root(), account, lichess::BOT_ACCOUNT_FILE);
    } catch (const std::exception& e) {
        bot_error_ = e.what();
        return;
    }
    // Another account than the last one: its games and words stay, its account's facts follow.
    if (bot_account_ && bot_account_->token != account.token && !bot_account_->token.empty()) {
        const auto previous = bot_account_->token;
        lichess_revokes_.push_back(std::async(std::launch::async, [previous] { return lichess::revoke(previous); }));
    }
    bot_account_ = account;
}

void App::forgetBotAccount() {
    setBotOnline(false);
    bot_unlink_ = std::async(std::launch::async, [root = storage_.root()] {
        return lichess::forgetAccount(root, nullptr, lichess::BOT_ACCOUNT_FILE);
    });
    bot_account_.reset();
    bot_facts_.reset();
    botSettings().facts = {};
    saveBotSettings();
    bot_step_ = BotStep::Card;
    bot_error_.clear();
}

// ---------------------------------------------------------------------------
// Online

int App::botThreads() const { return bot_game_ && !bot_state_.done ? bot_game_->threads() : 0; }

bool App::botPlaying() const { return bot_game_ && !bot_state_.done; }

bool App::botLive() const {
    return botPlaying() && (!bot_state_.game.full || bot_state_.game.live()) && !bot_state_.game.over();
}

bool App::botCoresNeeded() const {
    // A tournament playing on this computer, and fewer cores left beside it than the bot's engine takes.
    bool playing = false;
    for (const auto& r : runs_)
        if (r.run->remote() && r.run->running() && r.machine_id.empty() && r.run->tournament()) playing = true;
    if (!playing) return false;
    const auto* e = botEngine();
    return freeCores() < (e ? e->threads() : 1);
}

void App::setBotOnline(bool online) {
    if (online) {
        if (!botReady()) return;
        bot_online_error_.clear();
        bot_online_  = true;
        bot_closing_ = false;
        if (!bot_events_ || bot_events_->token() != bot_account_->token)
            bot_events_ = std::make_unique<lichess::EventListener>(bot_account_->token);
        return;
    }
    // Offline: no new challenge; a live game being played goes on to its end, its stream kept until then. A
    // correspondence game waits on Lichess for the bot's next time online.
    if (botLive()) {
        bot_closing_ = true;
        return;
    }
    bot_online_  = false;
    bot_closing_ = false;
    bot_events_.reset();
    bot_game_.reset();
    bot_waiting_.clear();
    bot_accepted_.clear();
}

void App::botEvent(const lichess::GameEvent& e) {
    using Kind = lichess::GameEvent::Kind;
    if (!bot_account_) return;
    switch (e.kind) {
        case Kind::Challenge: {
            const auto& c = e.challenge;
            if (!c.incoming) break;  // the bot sends none
            if (std::any_of(bot_answered_.begin(), bot_answered_.end(),
                            [&](const BotAnswer& a) { return a.challenge.id == c.id; }))
                break;  // the stream opened again: answered already
            lichess::BotSituation now;
            now.busy           = botPlaying() || !bot_accepted_.empty() || !bot_waiting_.empty();
            now.closing        = bot_closing_ || !bot_online_;
            now.cores_needed   = botCoresNeeded();
            const auto* engine = botEngine();
            now.engine_chess960 =
                engine && std::any_of(engine->uci_options.begin(), engine->uci_options.end(),
                                      [](const UciOptionInfo& o) { return o.name == "UCI_Chess960"; });
            if (lichess_account_) now.own_account = lichess_account_->username;
            const auto reason = lichess::declineReason(c, botSettings().rules, now);
            const auto id     = c.id;
            const auto token  = bot_account_->token;
            const auto what   = trf("The challenge of {}", c.challenger);
            if (reason.empty()) {
                bot_accepted_    = id;
                bot_accepted_at_ = SteadyClock::now();
                bot_calls_.push_back({what, id, std::async(std::launch::async, [token, id] {
                                          return lichess::acceptChallenge(token, id);
                                      })});
            } else {
                bot_calls_.push_back({what, {}, std::async(std::launch::async, [token, id, reason] {
                                          return lichess::declineChallenge(token, id, reason);
                                      })});
                lichess::countDeclined(botSettings(), today());
                saveBotSettings();
            }
            bot_answered_.insert(bot_answered_.begin(), {c, reason});
            if (bot_answered_.size() > 50) bot_answered_.resize(50);
            break;
        }
        case Kind::Started: {
            const auto& g = e.game;
            if (bot_game_ && bot_game_->id() == g.id) break;
            if (g.id == bot_accepted_) bot_accepted_.clear();
            if (!botPlaying())
                startBotGame(g.id, g.white);
            else if (std::none_of(bot_waiting_.begin(), bot_waiting_.end(),
                                  [&](const auto& w) { return w.first == g.id; }))
                bot_waiting_.emplace_back(g.id, g.white);
            break;
        }
        case Kind::Finished:
            std::erase_if(bot_waiting_, [&](const auto& w) { return w.first == e.game.id; });
            break;
        case Kind::ChallengeCanceled:  // called back by its sender: the bot is free again
            if (e.challenge.id == bot_accepted_) bot_accepted_.clear();
            break;
        default:
            break;
    }
}

void App::startBotGame(const std::string& id, bool white) {
    if (!bot_account_) return;
    // The Board follows the bot's next game when it still showed its last one.
    const bool follow = bot_shown_ && !bot_shown_id_.empty() && bot_game_ == nullptr;
    bot_game_  = std::make_unique<lichess::BotGame>(bot_account_->token, id, bot_account_->username, white, botPlay());
    bot_state_ = {};
    bot_version_ = 0;
    if (follow) {
        bot_shown_id_    = id;
        bot_flipped_     = false;
        bot_board_dirty_ = true;
    }
}

void App::finishBotGame(const lichess::BotGame::State& st) {
    const auto& g = st.game;
    if (!g.full || g.id.empty()) return;
    // Its record, for the card.
    lichess::BotRecord r;
    r.id             = g.id;
    const auto& them = st.white ? g.black : g.white;
    r.opponent       = them.name;
    r.rating         = them.rating;
    r.opponent_bot   = them.title == "BOT";
    r.speed          = g.speed;
    r.rated          = g.rated;
    r.white          = st.white;
    r.plies          = static_cast<int>(g.moves.size());
    r.result         = lichess::replayOf(g).result;
    r.at             = static_cast<int64_t>(std::time(nullptr));
    if (g.days > 0)
        r.clock = lichess::clockText({0, 0, g.days});
    else if (g.clock_ms >= 0)
        r.clock = fmt::format("{:g}+{}", static_cast<double>(g.clock_ms) / 60000.0, g.increment_ms / 1000);
    lichess::addBotRecord(botSettings(), r, today());
    saveBotSettings();
    if (g.moves.size() < 2 || !bot_account_) return;  // an aborted game stays out
    // Into "Lichess · <bot>", made when missing, as the user's own finished games are (#49).
    const auto name  = "Lichess · " + bot_account_->username;
    const auto lower = [](std::string text) {
        for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    auto& s = shelf();
    std::optional<std::size_t> index;
    for (std::size_t i = 0; i < s.entries().size(); ++i)
        if (!s.entries()[i].source && lower(s.entries()[i].name) == lower(name)) index = i;
    try {
        if (!index) index = s.create(name);
        auto db = s.open(*index);
        bot_arrivals_.push_back(
            {g.id, *index, std::async(std::launch::async, [id = g.id, token = bot_account_->token, db] {
                 return lichess::bringGame(id, token, db.get());
             })});
    } catch (const std::exception& e) {
        Console::instance().add(trf("Lichess bot: “{}” could not be made: {}", name, e.what()), true);
    }
}

void App::pollBot() {
    loadBot();
    const auto ready = [](auto& f) {
        return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    };
    // An account linked or a token pasted: checked before it is kept, and before anything changes it.
    const auto take = [&](lichess::BotTokenCheck c) {
        if (!c.account || !c.facts) {
            if (c.error == "cancelled") return;
            bot_unanswered_ = c.status == 0 && c.error != "the link was refused on Lichess";
            bot_error_      = c.error.empty() ? std::string(TR("Lichess did not answer")) : c.error;
            if (c.linked && c.account) {
                const auto token = c.account->token;
                lichess_revokes_.push_back(std::async(std::launch::async, [token] { return lichess::revoke(token); }));
            }
            return;
        }
        const auto verdict = lichess::botVerdict(*c.facts, lichess_account_ ? lichess_account_->username : "",
                                                 c.account->has(lichess::SCOPE_BOT_PLAY));
        // Never the user's own account, nor one that cannot play: not kept; a token RookStudio made is revoked.
        if (verdict == lichess::BotVerdict::OwnAccount || verdict == lichess::BotVerdict::Closed ||
            verdict == lichess::BotVerdict::NoScope) {
            bot_candidate_ = c.facts;
            bot_verdict_   = verdict;
            if (c.linked) {
                const auto token = c.account->token;
                lichess_revokes_.push_back(std::async(std::launch::async, [token] { return lichess::revoke(token); }));
            }
            return;
        }
        bot_candidate_.reset();
        keepBotAccount(*c.account);
        bot_facts_          = c.facts;
        botSettings().facts = *c.facts;
        saveBotSettings();
        if (window_) SDL_RaiseWindow(window_);  // back in front of the browser
    };
    if (bot_link_ && ready(bot_link_->result)) {
        auto c = bot_link_->result.get();
        bot_link_.reset();
        take(std::move(c));
    }
    if (ready(bot_check_)) take(bot_check_.get());
    // The upgrade: done, refused with Lichess's words, or unanswered (nothing changed).
    if (ready(bot_upgrade_)) {
        const auto r = bot_upgrade_.get();
        if (r.ok && bot_facts_) {
            bot_facts_->title         = "BOT";
            botSettings().facts       = *bot_facts_;
            botSettings().upgraded_at = static_cast<int64_t>(std::time(nullptr));
            saveBotSettings();
            bot_step_ = BotStep::Card;
            bot_error_.clear();
            std::fill(bot_confirm_.begin(), bot_confirm_.end(), '\0');
        } else {
            bot_unanswered_ = r.status == 0;
            bot_error_      = r.error;
            bot_step_       = BotStep::Account;
            // Refused: the account looked at again, what Lichess saw in it then said.
            if (r.status != 0 && bot_account_ && !bot_check_.valid())
                bot_check_ = std::async(std::launch::async,
                                        [token = bot_account_->token] { return lichess::checkBotToken(token); });
        }
    }
    if (ready(bot_unlink_)) {
        if (const auto error = bot_unlink_.get(); !error.empty())
            Console::instance().add(trf("Lichess: the bot's token could not be revoked: {}", error), true);
    }
    // Its answers to the challenges: what failed is said on its card.
    for (auto& c : bot_calls_) {
        if (!ready(c.reply)) continue;
        const auto r = c.reply.get();
        if (r.ok) continue;
        bot_online_error_ =
            r.status == 0 ? trf("{}: Lichess cannot be reached ({})", c.what, r.error) : trf("{}: {}", c.what, r.error);
        if (!c.accepted.empty() && c.accepted == bot_accepted_) bot_accepted_.clear();  // not to be played
    }
    std::erase_if(bot_calls_, [](const auto& c) { return !c.reply.valid(); });
    // Its finished games brought in.
    for (auto& a : bot_arrivals_) {
        if (!ready(a.arrival)) continue;
        const auto got = a.arrival.get();
        if (got.added && a.index < shelf().entries().size()) shelf().rebuildTree(a.index);
        if (!got.error.empty()) Console::instance().add(trf("Lichess bot: the game {}: {}", a.id, got.error), true);
    }
    std::erase_if(bot_arrivals_, [](const auto& a) { return !a.arrival.valid(); });
    if (!bot_account_) {
        bot_events_.reset();
        bot_game_.reset();
        bot_online_ = bot_closing_ = false;
        return;
    }
    // Its event stream: the challenges, its games' starts and ends.
    if (bot_events_) {
        if (bot_events_->refused()) {
            bot_online_error_ = TR("Lichess refused the bot's token: link its account again.");
            bot_events_.reset();
            bot_online_ = bot_closing_ = false;
        } else {
            for (auto& e : bot_events_->take()) botEvent(e);
        }
    }
    // A challenge accepted whose game never started (the challenger called it back meanwhile): free again.
    if (!bot_accepted_.empty() && SteadyClock::now() - bot_accepted_at_ > std::chrono::seconds(30))
        bot_accepted_.clear();
    // Its game: followed, then ended.
    if (bot_game_) {
        if (bot_game_->state(bot_state_, bot_version_)) bot_board_dirty_ = true;
        if (bot_state_.done) {
            finishBotGame(bot_state_);
            const auto shown = bot_shown_ && bot_shown_id_ == bot_game_->id();
            if (shown) showBotGame();  // its end on the Board
            bot_game_.reset();
            bot_board_dirty_ = false;
            // The next game started meanwhile, or none: offline once asked.
            if (!bot_waiting_.empty() && bot_online_) {
                const auto [id, white] = bot_waiting_.front();
                bot_waiting_.erase(bot_waiting_.begin());
                startBotGame(id, white);
            }
        }
    }
    if (bot_closing_ && !botLive()) setBotOnline(false);
    if (bot_board_dirty_ && botShown()) showBotGame();
}

// ---------------------------------------------------------------------------
// Its game on the Board

bool App::botShown() const {
    return bot_shown_ && bot_game_ && bot_game_->id() == bot_shown_id_ && !bot_state_.done && !bot_state_.game.over();
}

void App::openBotGame() {
    if (!bot_game_) return;
    auto& b = board_;
    if (!(bot_shown_ && bot_shown_id_ == bot_game_->id())) {
        rememberBoardGame();
        board_origin_.reset();
        b.undo.clear();
        b.redo.clear();
        b.start_notes = {};
        b.comment_edit.reset();
        b.changes  = {};
        b.watching = false;
        b.selected.reset();
        b.move_typed.reset();
        b.preview.reset();
        b.pgn.reset();
        if (b.analysing) setBoardAnalysis(false);
        b.game           = ReplayGame{};
        b.game.start_fen = std::string(chess::constants::STARTPOS);
        b.tree           = GameTree{b.game, {}};
        b.line           = -1;
        b.ply            = INT32_MAX;
        b.chess960       = false;
        b.title          = trf("Lichess · {}", bot_game_->id());
        b.message.clear();
        if (online_game_) online_game_->shown = false;
        bot_shown_    = true;
        bot_shown_id_ = bot_game_->id();
        bot_flipped_  = false;
        ++board_game_serial_;
    }
    bot_board_dirty_ = true;
    showBotGame();
    if (!views_.empty() && !view().holds(views::PageId::Board))
        toStudyBoard();
    else
        page_ = Page::Board;
    board_back_.reset();
}

void App::showBotGame() {
    auto& b             = board_;
    const auto& s       = bot_state_.game;
    auto game           = lichess::replayOf(s);
    const bool chess960 = s.variant == "chess960";
    // The board follows the game, unless the user went back in it.
    const bool at_end = b.line >= 0 || b.ply >= static_cast<int>(b.game.moves.size());
    b.game            = std::move(game);
    b.tree            = GameTree{b.game, {}};
    b.line            = -1;
    b.chess960        = chess960;
    if (at_end) b.ply = static_cast<int>(b.game.moves.size());
    b.ply = std::clamp(b.ply, 0, static_cast<int>(b.game.moves.size()));
    if (s.full) {
        b.title = fmt::format("{} – {}", s.white.name, s.black.name);
        if (!bot_flipped_) {
            b.flip       = !bot_state_.white;  // the bot's side below
            bot_flipped_ = true;
        }
    }
    bot_board_dirty_ = false;
}

std::optional<std::string> App::botStripClock(chess::Color side) const {
    if (!botShown() || !bot_state_.game.full) return std::nullopt;
    const auto& s    = bot_state_.game;
    const bool white = side == chess::Color::WHITE;
    if (!s.live() || s.clock_ms < 0) return std::nullopt;
    return clockOf(s.clockNow(white, SteadyClock::now()));
}

std::string App::botOpponent() const {
    const auto& s    = bot_state_.game;
    const auto& them = bot_state_.white ? s.black : s.white;
    if (them.name.empty()) return TR("its opponent");
    return them.rating > 0 ? fmt::format("{} ({})", them.name, them.rating) : them.name;
}

void App::drawBotGameCard() {
    if (!bot_shown_ || !bot_account_) return;
    const ui::TopicRegion topic(guide::topics::LichessBot);
    const bool same = bot_game_ && bot_game_->id() == bot_shown_id_;
    const auto& st  = bot_state_;
    const auto& s   = st.game;
    beginTinted("##bot_game", ui::ACCENT);
    ImGui::PushTextWrapPos();
    ui::inlineIcon(ui::Icon::Bot, ui::ACCENT);
    ImGui::SameLine();
    const auto* engine = botEngine();
    heading(fmt::format("{} · {}", bot_account_->username, engine ? engine->name : botSettings().engine).c_str(),
            1.05f);
    if (!same || st.done || s.over()) {
        // Ended: how, and where it went.
        std::string how;
        if (same || s.over()) {
            const auto result = lichess::replayOf(s).result;
            const bool won    = (result == "1-0") == st.white && result != "1/2-1/2" && result != "*";
            how               = result == "*"         ? std::string(TR("The game was aborted."))
                                : result == "1/2-1/2" ? trf("Draw against {}.", botOpponent())
                                : won ? trf("{} won against {}.", bot_account_->username, botOpponent())
                                      : trf("{} lost against {}.", bot_account_->username, botOpponent());
        } else {
            how = TR("Its game is over.");
        }
        wrapped(how);
        if (s.moves.size() >= 2)
            wrapped(trf("The game goes into the database “Lichess · {}”.", bot_account_->username), true);
        if (ImGui::SmallButton(TR("Close##bot_game"))) bot_shown_ = false;
        ImGui::PopTextWrapPos();
        endTinted();
        return;
    }
    if (s.full)
        wrapped(trf("{} against {}", gameKindText(s), botOpponent()));
    else
        wrapped(TR("Its game on Lichess, connecting…"), true);
    if (s.full && !s.connected) coloured(TR("The connection to Lichess is cut: tried again by itself."), ui::WARN);
    // Whose move, and what the engine searches (its own search: it plays this game), unless the user plays live.
    const bool its_turn = s.full && whiteToMove(s) == st.white;
    if (s.full) {
        ImGui::TextDisabled("%s", its_turn ? trf("Its move · move {}", s.moves.size() / 2 + 1).c_str()
                                           : trf("Its opponent's move · move {}", s.moves.size() / 2 + 1).c_str());
    }
    if (botSearchHidden()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
        ui::inlineIcon(ui::Icon::Lock, ui::WARN);
        ImGui::SameLine();
        wrapped(TR("Its search is hidden while you play a live game on Lichess (fair play); its game goes on."));
        ImGui::PopStyleColor();
    } else if (st.from_book && !st.thinking) {
        ImGui::TextDisabled("%s", TR("from its book"));
    } else if (!st.info.empty()) {
        const auto info  = fastchess::engine::UciEngine::parseInfo(st.info);
        const int ply    = st.info_ply >= 0 ? st.info_ply : static_cast<int>(s.moves.size());
        auto game        = lichess::replayOf(s);
        const auto board = boardAt(game, s.variant == "chess960", std::min(ply, static_cast<int>(game.moves.size())));
        std::string score;
        if (info.score) score = ui::formatScore(whiteScore(*info.score, board.sideToMove()));
        ImGui::TextUnformatted(
            fmt::format("{}  d{}  {}", score, info.depth.value_or(0), boardSan(pvToSan(board, info.pv, 8))).c_str());
        tip(TR("The engine's search: its evaluation for White, its depth and the line it expects."));
    }
    if (!st.error.empty()) coloured(st.error, ui::WARN);
    if (ImGui::SmallButton(TR("Open on lichess.org##bot_game")))
        SDL_OpenURL(fmt::format("https://lichess.org/{}", s.id.empty() ? bot_shown_id_ : s.id).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(bot_unlink_asked_ ? TR("Resign for it: click again") : TR("Resign for it…"))) {
        if (bot_unlink_asked_) {
            bot_game_->resign();
            bot_unlink_asked_ = false;
        } else {
            bot_unlink_asked_ = true;
        }
    }
    tip(TR("The bot resigns this game (aborts it before both sides moved); a second click does it."));
    if (bot_online_ && !bot_closing_) {
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Put offline##bot_game"))) setBotOnline(false);
        tip(TR("No new challenge; this game goes on to its end."));
    }
    ImGui::PopTextWrapPos();
    endTinted();
}

// ---------------------------------------------------------------------------
// The engine's card

void App::drawBotPart(const EngineEntry& e) {
    loadBot();
    const ui::TopicRegion topic(guide::topics::LichessBot);
    ImGui::Spacing();
    auto& s = botSettings();
    if (bot_account_ && bot_facts_ && bot_facts_->bot() && !s.engine.empty() && s.engine != e.name &&
        std::any_of(engines_.begin(), engines_.end(), [&](const EngineEntry& o) { return o.name == s.engine; })) {
        // The bot plays another engine: this one a click away, while it does not play.
        ui::inlineIcon(ui::Icon::Bot, ui::MUTED);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", trf("Your Lichess bot {} plays {}.", bot_account_->username, s.engine).c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(bot_online_ || botPlaying());
        if (ImGui::SmallButton(TR("Play this engine instead"))) {
            s.engine = e.name;
            saveBotSettings();
        }
        ImGui::EndDisabled();
        tip(bot_online_ ? TR("Once the bot is offline.") : TR("The bot's games are then played by this engine."));
        return;
    }
    const bool ready = bot_account_ && bot_facts_ && bot_facts_->bot();
    const bool setup = bot_setup_engine_ == e.name && bot_step_ != BotStep::Card;
    // No bot yet: one line, the options below staying in sight (R2); the setup opens from it.
    if (!ready && !setup && !bot_account_ && !bot_link_ && !bot_check_.valid()) {
        drawBotIntro(e);
        return;
    }
    beginTinted("##bot_part", ui::ACCENT);
    ImGui::PushTextWrapPos();
    ui::inlineIcon(ui::Icon::Bot, ui::ACCENT);
    ImGui::SameLine();
    heading(TR("On Lichess, as a bot"));
    if (ready && !setup) {
        if (s.engine.empty()) {
            s.engine = e.name;
            saveBotSettings();
        }
        if (bot_online_ || botPlaying())
            drawBotOnline();
        else
            drawBotRules(e);
    } else {
        bot_setup_engine_ = e.name;
        if (bot_step_ == BotStep::Card) bot_step_ = BotStep::Account;
        if (bot_step_ == BotStep::Confirm)
            drawBotConfirm();
        else
            drawBotSteps();
    }
    ImGui::PopTextWrapPos();
    endTinted();
}

void App::drawBotIntro(const EngineEntry& e) {
    ui::inlineIcon(ui::Icon::Bot, ui::ACCENT);
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("On Lichess, as a bot"));
    ImGui::SameLine();
    const bool start = ImGui::SmallButton(TR("Make it a Lichess bot…"));
    tip(
        TR("Have this engine play on Lichess under a bot account: it accepts the challenges you choose, while "
           "RookStudio is open. It needs a new Lichess account, apart from yours, that has never played. Becoming a "
           "bot cannot be undone."));
    const char* summary = TR("the challenges you choose, while RookStudio is open");
    ui::sameLineOrWrap(ImGui::CalcTextSize(summary).x);
    ImGui::TextDisabled("%s", summary);
    if (start) {
        bot_setup_engine_    = e.name;
        bot_step_            = BotStep::Account;
        botSettings().engine = e.name;
        saveBotSettings();
    }
}

void App::drawBotSteps() {
    const auto* f = bot_facts_ ? &*bot_facts_ : nullptr;
    const bool shown_refusal =
        bot_candidate_.has_value() || (f && !f->bot() && f->games > 0) || (!bot_error_.empty() && !bot_link_);
    // 1: an account of its own, made on lichess.org.
    const bool linked = bot_account_.has_value();
    ImGui::Spacing();
    stepMark(1, linked);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(TR("An account for the bot"));
    wrapped(TR("Created on lichess.org, with a name of its own, without playing a single game there. RookStudio does "
               "not create accounts."),
            true);
    if (ImGui::SmallButton(TR("Create the account on lichess.org"))) SDL_OpenURL(LICHESS_SIGNUP);
    ImGui::EndGroup();
    // 2: linked, with bot:play only; checked.
    ImGui::Spacing();
    stepMark(2, linked && f);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(TR("Link it to RookStudio"));
    wrapped(TR("Lichess's page opens: log in with the bot's account, not yours (a private window avoids logging you "
               "out). The permission asked: play as a bot (bot:play)."),
            true);
    if (bot_link_) {
        coloured(TR("Waiting for Lichess: log in with the bot's account and allow RookStudio, then come back here."),
                 ui::ACCENT);
        if (!bot_link_->opened->load() && !bot_link_->url->empty()) {
            wrapped(TR("No browser opened: paste this address in a browser of this machine."), true);
            ImGui::TextUnformatted(bot_link_->url->c_str());
            if (ImGui::SmallButton(TR("Copy the address"))) ImGui::SetClipboardText(bot_link_->url->c_str());
        }
        if (ImGui::SmallButton(TR("Cancel##bot_link")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            bot_link_->cancel->store(true);
    } else if (bot_check_.valid()) {
        coloured(TR("Checking the account…"), ui::ACCENT);
    } else if (ImGui::Button(linked ? TR("Link another account…") : TR("Link the bot's account…"))) {
        startBotLink();
    }
    if (f && linked) {
        // Its checks, each with its mark.
        const auto own   = lichess_account_ ? lichess_account_->username : std::string();
        std::string line = fmt::format("{} · {} · ", f->username, createdText(f->created_ms));
        line += f->bot() ? std::string(TR("a bot already ✓"))
                         : (f->games == 0 ? std::string(TR("0 games played ✓"))
                                          : trn("{} game played ✗", "{} games played ✗", f->games, f->games));
        if (!f->bot()) line += std::string(" · ") + TR("not a bot yet ✓");
        line +=
            " · " + (own.empty() ? std::string(TR("no account of yours linked")) : trf("not your account ({}) ✓", own));
        coloured(line, f->bot() || f->games == 0 ? ui::GOOD : ui::WARN);
    }
    ImGui::EndGroup();
    // 3: the upgrade, definitive: asked on the next step.
    ImGui::Spacing();
    stepMark(3, f && f->bot());
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(TR("Make it a bot"));
    if (f && f->bot()) {
        wrapped(TR("It is a bot already: it can serve as it is."), true);
        if (primaryButton(TR("Its rules…"))) bot_step_ = BotStep::Card;
    } else {
        wrapped(TR("Cannot be undone: the next step says so and asks for its name."), true);
        const bool can = linked && f && f->games == 0 && !f->bot() && !bot_upgrade_.valid();
        if (primaryButton(TR("Make it a bot…"), can)) {
            bot_step_          = BotStep::Confirm;
            bot_confirm_focus_ = true;
            std::fill(bot_confirm_.begin(), bot_confirm_.end(), '\0');
        }
    }
    ImGui::EndGroup();
    if (shown_refusal) drawBotRefusal();
    // Or a token, made on lichess.org.
    ImGui::Spacing();
    wrapped(TR("Or paste a token of the bot's account (bot:play), made on lichess.org. Your own account stays linked "
               "apart, for the rest of RookStudio."),
            true);
    ImGui::SetNextItemWidth(px(280));
    const bool enter =
        ImGui::InputTextWithHint("##bot_token", TR("lip_…"), bot_token_text_.data(), bot_token_text_.size(),
                                 ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::BeginDisabled(bot_token_text_[0] == '\0' || bot_link_ || bot_check_.valid());
    if (ImGui::Button(TR("Check it")) || enter) {
        checkBotToken(bot_token_text_.data());
        std::fill(bot_token_text_.begin(), bot_token_text_.end(), '\0');
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Make a token on lichess.org"))) SDL_OpenURL(LICHESS_BOT_TOKEN);
    tip(TR("Lichess's page, logged in with the bot's account, the permission to play as a bot (bot:play) ticked."));
    if (bot_account_ && !shown_refusal) {
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Unlink this one"))) forgetBotAccount();
        tip(TR("Its token is revoked at Lichess and deleted here."));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Close##bot_steps"))) {
        bot_step_ = BotStep::Card;
        bot_setup_engine_.clear();
        if (!bot_account_) bot_error_.clear();
    }
}

void App::drawBotRefusal() {
    ImGui::Spacing();
    beginTinted("##bot_refused", ui::WARN);
    ImGui::PushTextWrapPos();
    ui::inlineIcon(ui::Icon::Alert, ui::WARN);
    ImGui::SameLine();
    ImGui::BeginGroup();
    const auto own = lichess_account_ ? lichess_account_->username : std::string();
    bool create = false, other = false;
    if (bot_candidate_) {
        // Refused before any request: the account was not kept.
        switch (bot_verdict_) {
            case lichess::BotVerdict::OwnAccount:
                coloured(trf("It is your own account ({}): never that one.", own), ui::WARN);
                wrapped(
                    TR("Log in with the bot's account on lichess.org (a private window avoids logging you out), "
                       "then link again. Nothing was kept."));
                other = true;
                break;
            case lichess::BotVerdict::Closed:
                coloured(trf("{} is closed, or marked by Lichess: it cannot be a bot.", bot_candidate_->username),
                         ui::WARN);
                wrapped(TR("Create a new account, play nothing on it, then link it."));
                create = other = true;
                break;
            default:
                coloured(TR("This token may not play as a bot."), ui::WARN);
                wrapped(
                    TR("Make one with the permission to play as a bot (bot:play), logged in with the bot's "
                       "account, then paste it."));
                break;
        }
    } else if (bot_facts_ && !bot_facts_->bot() && bot_facts_->games > 0) {
        coloured(trn("Lichess refuses: this account has already played {} game",
                     "Lichess refuses: this account has already played {} games", bot_facts_->games, bot_facts_->games),
                 ui::WARN);
        wrapped(
            TR("An account that has played cannot become a bot. Create a new one, play nothing on it, then link "
               "it."));
        create = other = true;
    } else if (bot_unanswered_) {
        coloured(TR("Lichess did not answer: nothing changed, try again."), ui::WARN);
        if (!bot_error_.empty()) ImGui::TextDisabled("%s", bot_error_.c_str());
    } else if (!bot_error_.empty()) {
        coloured(trf("Lichess refuses: {}", bot_error_), ui::WARN);
        wrapped(TR("Nothing changed on the account."), true);
        other = true;
    }
    if (create && ImGui::SmallButton(TR("Create an account on lichess.org"))) SDL_OpenURL(LICHESS_SIGNUP);
    if (create && other) ImGui::SameLine();
    if (other && !bot_link_ && ImGui::SmallButton(TR("Link another account…"))) {
        bot_candidate_.reset();
        startBotLink();
    }
    // The account kept, refused all the same: let go from here.
    if (bot_account_ && !bot_candidate_ && other) {
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Unlink this one##refused"))) forgetBotAccount();
        tip(TR("Its token is revoked at Lichess and deleted here."));
    }
    ImGui::EndGroup();
    ImGui::PopTextWrapPos();
    endTinted();
}

void App::drawBotConfirm() {
    if (!bot_account_ || !bot_facts_) {
        bot_step_ = BotStep::Account;
        return;
    }
    const auto& name = bot_facts_->username;
    beginTinted("##bot_confirm", ui::WARN);
    ImGui::PushTextWrapPos();
    ui::inlineIcon(ui::Icon::Alert, ui::WARN);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
    heading(trf("Make {} a bot: it cannot be undone", name).c_str(), 1.1f);
    ImGui::PopStyleColor();
    ImGui::BulletText("%s", TR("It can never play as a person again, nor become an ordinary account again."));
    ImGui::BulletText("%s", TR("Lichess marks it BOT; it plays challenges only: no lobby, no tournaments, but the "
                               "arenas open to bots."));
    ImGui::BulletText("%s", TR("Lichess takes it only from an account that has never played."));
    ImGui::TextUnformatted(TR("To confirm, type its name"));
    ImGui::SetNextItemWidth(px(280));
    if (bot_confirm_focus_) {
        ImGui::SetKeyboardFocusHere();
        bot_confirm_focus_ = false;
    }
    const bool enter  = ImGui::InputText("##bot_confirm_name", bot_confirm_.data(), bot_confirm_.size(),
                                         ImGuiInputTextFlags_EnterReturnsTrue);
    std::string typed = bot_confirm_.data();
    const auto lower  = [](std::string t) {
        for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return t;
    };
    const bool matches = !typed.empty() && lower(typed) == lower(name);
    const bool busy    = bot_upgrade_.valid();
    if (primaryButton(busy ? TR("Asking Lichess…") : TR("Make it a bot"), matches && !busy, ui::BAD) ||
        (enter && matches && !busy)) {
        bot_error_.clear();
        bot_unanswered_ = false;
        bot_upgrade_ =
            std::async(std::launch::async, [token = bot_account_->token] { return lichess::upgradeToBot(token); });
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Cancel · Esc")) || (!busy && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
        bot_step_ = BotStep::Account;
        std::fill(bot_confirm_.begin(), bot_confirm_.end(), '\0');
    }
    wrapped(TR("The button lights up once the name typed is its own. What leaves the machine: the request to make it a "
               "bot, with the bot's token."),
            true);
    ImGui::PopTextWrapPos();
    endTinted();
}

void App::drawBotRules(const EngineEntry& e) {
    auto& s      = botSettings();
    auto& r      = s.rules;
    bool changed = false;
    // Its heading: the account, a bot since when.
    std::string since;
    if (s.upgraded_at > 0) {
        const auto t = static_cast<std::time_t>(s.upgraded_at);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &t);
#else
        localtime_r(&t, &local);
#endif
        // The time today, else the date: "a bot since on Monday" did not read.
        char time_text[16], date_text[16];
        std::strftime(time_text, sizeof(time_text), "%H:%M", &local);
        std::strftime(date_text, sizeof(date_text), "%Y-%m-%d", &local);
        since =
            ui::dayText(t) == TR("today") ? trf("a bot since today, {}", time_text) : trf("a bot since {}", date_text);
    } else {
        since = TR("a bot");
    }
    ImGui::TextUnformatted(TR("The challenges it accepts"));
    ImGui::SameLine();
    ImGui::TextDisabled("· %s, %s", bot_account_->username.c_str(), since.c_str());
    const float label = px(150);
    const auto row    = [&](const char* text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", text);
        ImGui::SameLine(label);
    };
    // Speeds.
    row(TR("Speeds"));
    struct Speed {
        const char* key;
        bool* on;
    };
    const Speed speeds[] = {{"bullet", &r.bullet},
                            {"blitz", &r.blitz},
                            {"rapid", &r.rapid},
                            {"classical", &r.classical},
                            {"correspondence", &r.correspondence}};
    for (const auto& sp : speeds) {
        if (toggleChip(speedName(sp.key), *sp.on, sp.key)) {
            *sp.on  = !*sp.on;
            changed = true;
        }
        ImGui::SameLine();
    }
    ImGui::TextDisabled("%s", TR("· no UltraBullet (Lichess)"));
    if (r.correspondence) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + label);
        ImGui::TextDisabled("%s", TR("A correspondence game waits on Lichess while the bot is offline."));
    }
    // Rated.
    row(TR("Rated"));
    if (ImGui::Checkbox(TR("Rated games too"), &r.rated)) changed = true;
    ImGui::SameLine();
    ImGui::TextDisabled("%s", r.rated ? TR("· never against your own account (Lichess forbids it)")
                                      : TR("· unrated while you test it, as Lichess asks; tick it afterwards"));
    // Opponents.
    row(TR("Opponents"));
    if (ImGui::Checkbox(TR("People"), &r.humans)) changed = true;
    ImGui::SameLine();
    if (ImGui::Checkbox(TR("Bots"), &r.bots)) changed = true;
    ImGui::SameLine(0, px(16));
    ImGui::TextUnformatted(TR("rated from"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(70));
    if (ui::inputInt("##bot_min", &r.rating_min)) {
        r.rating_min = std::clamp(r.rating_min, 0, 4000);
        changed      = true;
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("to"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(70));
    if (ui::inputInt("##bot_max", &r.rating_max)) {
        r.rating_max = std::clamp(r.rating_max, 0, 4000);
        changed      = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("(0: no bound)"));
    // Variants.
    row(TR("Variants"));
    const bool knows960 = std::any_of(e.uci_options.begin(), e.uci_options.end(),
                                      [](const UciOptionInfo& o) { return o.name == "UCI_Chess960"; });
    ImGui::TextUnformatted(TR("Standard chess"));
    ImGui::SameLine();
    ImGui::BeginDisabled(!knows960);
    bool c960 = r.chess960 && knows960;
    if (ImGui::Checkbox("Chess960", &c960)) {
        r.chess960 = c960;
        changed    = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s",
                        knows960 ? trf("({} knows it)", e.name).c_str() : trf("({} does not know it)", e.name).c_str());
    // At once, beside a tournament.
    row(TR("At once"));
    const auto* hash = e.option("Hash");
    std::string memory;
    for (const auto& o : e.uci_options)
        if (o.name == "Hash") memory = hash ? *hash : o.default_value;
    ImGui::TextUnformatted(
        (memory.empty() ? trn("1 game · {} thread · within the core budget",
                              "1 game · {} threads · within the core budget", e.threads(), e.threads())
                        : trn("1 game · {} thread, {} MB · within the core budget",
                              "1 game · {} threads, {} MB · within the core budget", e.threads(), e.threads(), memory))
            .c_str());
    row(TR("Beside a tournament"));
    wrapped(TR("the tournament first: it declines “later” while the tournament needs the cores"));
    if (botCoresNeeded()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + label);
        coloured(TR("A tournament needs the cores now: the challenges are declined “later”."), ui::WARN);
    }
    // Its play.
    row(TR("Its play"));
    {
        std::vector<const BookEntry*> polyglot;
        for (const auto& b : books_) {
            auto ext = std::filesystem::path(b.path).extension().string();
            for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext == ".bin") polyglot.push_back(&b);
        }
        const BookEntry* chosen = nullptr;
        for (const auto* b : polyglot)
            if (b->path == s.book) chosen = b;
        ImGui::SetNextItemWidth(px(200));
        if (ImGui::BeginCombo("##bot_book", chosen ? trf("the book “{}”", chosen->name).c_str() : TR("no book"))) {
            if (ImGui::Selectable(TR("no book"), !chosen)) {
                s.book.clear();
                changed = true;
            }
            for (const auto* b : polyglot) {
                if (ImGui::Selectable(fmt::format("{}##{}", b->name, b->path).c_str(), b == chosen)) {
                    s.book  = b->path;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        tip(polyglot.empty() ? TR("No Polyglot book in your library (the Books page).")
                             : TR("A Polyglot book of your library, played while it has a move; then the engine."));
        ImGui::SameLine();
        const bool syzygy = std::any_of(e.uci_options.begin(), e.uci_options.end(),
                                        [](const UciOptionInfo& o) { return o.name == "SyzygyPath"; });
        ImGui::BeginDisabled(!syzygy || setup_.tb_paths.empty() || e.option("SyzygyPath"));
        bool tables = s.tables && syzygy && !setup_.tb_paths.empty() && !e.option("SyzygyPath");
        if (ImGui::Checkbox(TR("the tournaments' tables"), &tables)) {
            s.tables = tables;
            changed  = true;
        }
        ImGui::EndDisabled();
        tip(e.option("SyzygyPath")    ? TR("The engine's own SyzygyPath is used.")
            : !syzygy                 ? TR("The engine has no SyzygyPath option.")
            : setup_.tb_paths.empty() ? TR("No tables for the tournaments yet (New tournament › Engine settings).")
                                      : trf("SyzygyPath = {}", setup_.tb_paths));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("· no thinking on the opponent's time"));
    }
    // Its words.
    row(TR("Its words"));
    {
        const auto field = [&](const char* id, std::string& text, const char* hint) {
            std::array<char, 141> buffer{};
            std::snprintf(buffer.data(), buffer.size(), "%s", text.c_str());
            ImGui::SetNextItemWidth(px(200));
            if (ImGui::InputTextWithHint(id, hint, buffer.data(), buffer.size())) {
                text        = buffer.data();
                s.words_set = true;
                changed     = true;
            }
        };
        field("##bot_greeting", s.greeting, TR("at the start"));
        ImGui::SameLine();
        field("##bot_thanks", s.thanks, TR("at the end"));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("· in the chat"));
    }
    ImGui::Spacing();
    wrapped(TR("The other challenges are declined politely, with the reason Lichess shows the challenger (“too fast”, "
               "“casual only”…)."),
            true);
    wrapped(
        TR("What leaves the machine: the engine's moves, its words in the chat, its answers to the challenges, with "
           "the bot's token; nothing of your account. Its profile (name, description) is set on lichess.org."),
        true);
    if (!bot_online_error_.empty()) coloured(bot_online_error_, ui::WARN);
    const bool missing = e.program_state == ProgramState::Missing || e.program_state == ProgramState::MissingDir;
    if (missing) coloured(trf("{}'s program is missing: locate it above first.", e.name), ui::WARN);
    const bool go =
        primaryButton(TR("Put online"), !missing) ||
        (!missing && !ImGui::GetIO().WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
         ImGui::IsKeyPressed(ImGuiKey_Enter, false));
    ImGui::SameLine(0, px(4));
    ImGui::TextDisabled("%s", TR("Enter"));
    if (go) setBotOnline(true);
    ImGui::SameLine(0, px(24));
    if (ImGui::SmallButton(bot_unlink_asked_ ? TR("Unlink the bot: click again") : TR("Unlink the bot…"))) {
        if (bot_unlink_asked_) {
            bot_unlink_asked_ = false;
            forgetBotAccount();
        } else {
            bot_unlink_asked_ = true;
        }
    }
    tip(TR("Its token is revoked at Lichess and deleted here; the account stays a bot. A second click does it."));
    if (changed) saveBotSettings();
}

void App::drawBotOnline() {
    auto& s        = botSettings();
    const auto day = lichess::botDay(s, today());
    // The banner: online, what it does today.
    const ImU32 red = IM_COL32(209, 67, 47, 255);
    {
        auto* dl        = ImGui::GetWindowDrawList();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float r   = px(4);
        dl->AddCircleFilled(ImVec2(at.x + r, at.y + ImGui::GetTextLineHeight() * 0.5f), r,
                            bot_closing_ ? ui::WARN : red);
        ImGui::Dummy(ImVec2(r * 2, ImGui::GetTextLineHeight()));
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(bot_closing_ ? trf("{} going offline", bot_account_->username).c_str()
                                        : trf("{} online", bot_account_->username).c_str());
    ImGui::SameLine();
    std::string what = botPlaying() ? std::string(TR("1 game in progress")) : std::string(TR("waiting for challenges"));
    what += " · " + trn("{} today", "{} today", day.games, day.games);
    if (day.games > 0) what += ": " + trf("{} won, {} drawn, {} lost", day.won, day.drawn, day.lost);
    const int declined = s.day == today() ? s.declined : 0;
    what += " · " + trn("{} challenge declined", "{} challenges declined", declined, declined);
    ImGui::TextDisabled("%s", what.c_str());
    if (ImGui::SmallButton(TR("Its page on lichess.org")))
        SDL_OpenURL(fmt::format("https://lichess.org/@/{}", bot_account_->username).c_str());
    ImGui::SameLine();
    if (!bot_closing_) {
        if (ImGui::SmallButton(TR("Put offline"))) setBotOnline(false);
        tip(TR("No new challenge; the game in progress goes on to its end."));
    } else {
        ImGui::TextDisabled("%s", TR("No new challenge; the game in progress goes on to its end."));
    }
    if (!bot_online_error_.empty()) coloured(bot_online_error_, ui::WARN);
    if (bot_events_ && !bot_events_->connected() && !bot_events_->error().empty())
        coloured(trf("The connection to Lichess is cut ({}): tried again by itself.", bot_events_->error()), ui::WARN);
    if (botCoresNeeded())
        coloured(TR("A tournament needs the cores: the challenges are declined “later” meanwhile."), ui::WARN);
    // Its game: on a small board, the Board a click away.
    ImGui::Spacing();
    if (botPlaying() && bot_state_.game.full) {
        const auto& g    = bot_state_.game;
        auto game        = lichess::replayOf(g);
        const auto board = boardAt(game, g.variant == "chess960", static_cast<int>(game.moves.size()));
        BoardDecor decor;
        decor.eval_white = -1.0f;
        decor.flipped    = !bot_state_.white;
        if (!game.moves.empty()) decor.last_move = game.moves.back().move;
        drawBoard(atlas_, board, px(180), decor);
        ImGui::SameLine(0, px(14));
    }
    ImGui::BeginGroup();
    if (botPlaying()) {
        const auto& g = bot_state_.game;
        ImGui::TextUnformatted(trf("against {}", botOpponent()).c_str());
        if (g.full)
            ImGui::TextDisabled(
                "%s", fmt::format("{} · {}", gameKindText(g), trf("move {}", g.moves.size() / 2 + 1)).c_str());
        if (botSearchHidden()) {
            coloured(TR("Its search is hidden while you play a live game on Lichess (fair play); its game goes on."),
                     ui::WARN);
        } else if (!bot_state_.info.empty()) {
            const auto info = fastchess::engine::UciEngine::parseInfo(bot_state_.info);
            auto game       = lichess::replayOf(g);
            const int ply =
                std::min(bot_state_.info_ply >= 0 ? bot_state_.info_ply : static_cast<int>(game.moves.size()),
                         static_cast<int>(game.moves.size()));
            const auto b = boardAt(game, g.variant == "chess960", ply);
            if (info.score)
                ImGui::TextDisabled("%s",
                                    fmt::format("{}  d{}", ui::formatScore(whiteScore(*info.score, b.sideToMove())),
                                                info.depth.value_or(0))
                                        .c_str());
        }
        if (!bot_state_.error.empty()) coloured(bot_state_.error, ui::WARN);
        if (primaryButton(TR("On the Board"))) openBotGame();
        tip(TR("The game followed on the Board, with the engine's search (its own)."));
    }
    // Its last games and the challenges it answered.
    const auto& games = s.games;
    for (std::size_t i = 0; i < std::min<std::size_t>(games.size(), 4); ++i) {
        const auto& g     = games[i];
        const auto score  = lichess::botScore(g);
        // Nouns ("a win"), as the Board's online tables say them: "won" is an endgame's result, another word in some
        // languages.
        const char* state = !score       ? TR("aborted")
                            : *score > 0 ? TR("a win")
                            : *score < 0 ? TR("a loss")
                                         : TR("a draw");
        ImGui::TextUnformatted(
            fmt::format("{}{}", g.opponent, g.rating > 0 ? fmt::format(" ({})", g.rating) : "").c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s",
                            fmt::format("· {} {} · {} · {}", speedName(g.speed), g.clock, g.result, state).c_str());
    }
    for (std::size_t i = 0, shown = 0; i < bot_answered_.size() && shown < 3; ++i) {
        const auto& a = bot_answered_[i];
        if (a.reason.empty()) continue;
        ++shown;
        ImGui::TextDisabled(
            "%s", trf("Declined: {} ({}), {}", a.challenge.challenger,
                      speedName(a.challenge.speed.empty() ? "rapid" : a.challenge.speed), declineText(a.reason))
                      .c_str());
    }
    ImGui::EndGroup();
    wrapped(trf("The game in progress is followed on the Board, with the engine's search (its own). The finished games "
                "go into the database “Lichess · {}”.",
                bot_account_->username),
            true);
    wrapped(TR("Online, the bot shows where what runs shows: the Home page and the Tournaments page."), true);
}

// ---------------------------------------------------------------------------
// Where what runs shows: the Home page's "In progress", the Tournaments page

void App::drawBotRunning(bool home) {
    if (!bot_account_ || !(bot_online_ || botPlaying())) return;
    const ui::TopicRegion topic(guide::topics::LichessBot);
    if (!home) {
        ImGui::Spacing();
        ui::beginCard("bot_running");
    }
    ImGui::PushID("bot_running");
    ui::inlineIcon(ui::Icon::Bot, ui::ACCENT);
    ImGui::SameLine();
    const auto* engine = botEngine();
    const auto who     = fmt::format("{} · {}", bot_account_->username, engine ? engine->name : botSettings().engine);
    ImGui::TextUnformatted(bot_closing_ ? trf("Your Lichess bot {} is going offline", who).c_str()
                                        : trf("Your Lichess bot {} is online", who).c_str());
    ImGui::SameLine();
    if (botPlaying()) {
        const auto& g = bot_state_.game;
        ImGui::TextDisabled("%s", g.full ? fmt::format("· {} · {} · {}", trf("against {}", botOpponent()),
                                                       gameKindText(g), trf("move {}", g.moves.size() / 2 + 1))
                                               .c_str()
                                         : TR("· a game starts"));
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("On the Board"))) openBotGame();
    } else {
        ImGui::TextDisabled("%s", TR("· waiting for challenges"));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Its card"))) {
        for (int i = 0; i < static_cast<int>(engines_.size()); ++i)
            if (engine && engines_[static_cast<std::size_t>(i)].name == engine->name) selected_engine_ = i;
        page_ = Page::Engines;
    }
    if (!bot_closing_) {
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Put offline"))) setBotOnline(false);
    }
    if (botCoresNeeded())
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           TR("The tournament first: the challenges are declined “later” while it needs the cores."));
    ImGui::PopID();
    if (!home) ui::endCard();
}

void App::drawQuitBot() {
    if (quit_bot_open_) {
        ImGui::OpenPopup(TR("Close RookStudio?###quit_bot"));
        quit_bot_open_ = false;
    }
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(TR("Close RookStudio?###quit_bot"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    {
        const ui::TopicRegion topic(guide::topics::LichessBot);
        ImGui::PushTextWrapPos(px(420));
        if (bot_account_ && botLive()) {
            ImGui::TextUnformatted(trf("Your bot {} is playing {} on Lichess: closing RookStudio stops its engine, and "
                                       "the game would be lost on time.",
                                       bot_account_->username, botOpponent())
                                       .c_str());
            ImGui::TextDisabled("%s", TR("Put it offline to close once the game is over: it accepts no new challenge "
                                         "meanwhile."));
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    if (primaryButton(TR("Stay · Esc")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    if (!bot_closing_) {
        ImGui::SameLine();
        if (ImGui::Button(TR("Put it offline"))) {
            setBotOnline(false);
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Close anyway"))) {
        quit_bot_said_ = true;
        ImGui::CloseCurrentPopup();
        requestQuit();
    }
    ImGui::EndPopup();
}

}  // namespace fcg
