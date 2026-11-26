// One of the user's engines offered to lichess.org's analysis board (#96, mockup M13, board 9): on an engine's card,
// "On lichess.org, for the analysis", one line until it is opened: its name at Lichess, the machine, its threads and
// memory at most, the Chess960 mode Lichess asks for, and "Offer to Lichess…" (Enter); engine:read and engine:write
// asked the first time, as #45's card does; then registered and served without asking anything more
// (lichess_engine.hpp). In service: its requests, lines, threads, memory, depth and speed, today's requests and
// minutes, the position asked, "Open Lichess's analysis board", "Stop the service"; stopped: "Resume", "Withdraw from
// Lichess"; the errors, each with its fix. The guards each frame (R6, R17): a tournament's threads first, the bot's
// game's, the user's live game on Lichess (paused until its end), their correspondence games' positions (an empty
// answer). The Lichess page lists the engines offered, the Home page and the Tournaments page show those serving (R3).

#include <algorithm>
#include <chrono>
#include <ctime>
#include <string_view>

#include <SDL3/SDL.h>
#include <fmt/include/fmt/format.h>
#include <imgui.h>
#include <chess.hpp>
#include <tour/budget.hpp>

#include "app.hpp"
#include "board_view.hpp"
#include "console.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "lichess_arenas.hpp"
#include "lichess_bot_page.hpp"
#include "machine_jobs.hpp"
#include "ui.hpp"
#include "update_check.hpp"

namespace fcg {

namespace {

using SteadyClock = std::chrono::steady_clock;

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

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

void coloured(const std::string& text, ImU32 tone) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(tone));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

void heading(const char* text) {
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

// A box of the card: its icon, a title in its tone, what it says, and its fix's button (none when `fix` is empty);
// true when the fix is pressed.
bool messageBox(const char* id, ui::Icon icon, ImU32 tone, const std::string& title, const std::string& text,
                const std::string& fix = {}) {
    beginTinted(id, tone);
    ui::inlineIcon(icon, tone);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos();
    coloured(title, tone);
    if (!text.empty()) wrapped(text, true);
    bool pressed = false;
    if (!fix.empty()) pressed = ImGui::SmallButton(fix.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    endTinted();
    return pressed;
}

// A calm box of the card (a tournament playing, the bot's game): light grey, its title in the text's colour, as the
// mockup's.
void calmBox(const char* id, ui::Icon icon, const std::string& title, const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(
                                                mix(ui::CARD_BG, ImGui::GetColorU32(ImGuiCol_TextDisabled), 0.06f)));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(ui::CARD_BD));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(12)));
    ImGui::BeginChild(id, ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
    const auto ink = ImGui::GetColorU32(ImGuiCol_Text);
    ui::inlineIcon(icon, ink);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos();
    coloured(title, ink);
    if (!text.empty()) wrapped(text, true);
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    endTinted();
}

// A key's hint beside the one button it presses.
void keyHint(const char* key) {
    ImGui::SameLine(0, px(4));
    ImGui::TextDisabled("%s", key);
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

std::string megabytes(int mb) { return trf("{} MB", mb); }

// "1 request in progress · 3 lines · 8 threads · 1024 MB · depth 31 · 4.2M nodes/s"
std::string jobLine(const lichess::ServiceJob& job, bool busy) {
    std::vector<std::string> parts;
    if (busy) parts.push_back(TR("1 request in progress"));
    parts.push_back(trn("{} line", "{} lines", job.multipv, job.multipv));
    parts.push_back(trn("{} thread", "{} threads", job.threads, job.threads));
    parts.push_back(megabytes(job.hash));
    if (job.depth > 0) parts.push_back(trf("depth {}", job.depth));
    if (job.nps > 0) parts.push_back(trf("{} nodes/s", ui::formatCount(static_cast<uint64_t>(job.nps))));
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : " · ") + p;
    return out;
}

// A name the card can show whole: a tab in a name typed is a space.
std::string nameOf(const std::array<char, 256>& field) {
    std::string out(field.data());
    std::replace(out.begin(), out.end(), '\t', ' ');
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// What the profile keeps

std::vector<lichess::OfferedEngine>& App::offers() {
    if (!offers_) offers_ = lichess::loadOffers(storage_.root());
    return *offers_;
}

lichess::OfferedEngine* App::offerOf(const std::string& engine) {
    for (auto& o : offers())
        if (o.engine == engine) return &o;
    return nullptr;
}

void App::saveOffers() {
    if (!offers_) return;
    offers_dirty_   = false;
    offers_written_ = SteadyClock::now();
    if (options_.scripted && lichess::servers().engine.rfind("http://127.0.0.1:", 0) != 0) return;
    std::string error;
    if (!lichess::saveOffers(storage_.root(), *offers_, &error))
        Console::instance().add(trf("The engines offered to Lichess could not be written: {}", error), true);
}

bool App::engineScopes() const {
    return lichess_account_ && lichess_account_->has(lichess::SCOPE_ENGINE_READ) &&
           lichess_account_->has(lichess::SCOPE_ENGINE_WRITE);
}

bool App::knowsChess960(const EngineEntry& e) {
    if (e.uci_options.empty()) return true;  // not read yet: its start tells
    return std::any_of(e.uci_options.begin(), e.uci_options.end(),
                       [](const UciOptionInfo& o) { return o.name == "UCI_Chess960"; });
}

std::optional<EngineEntry> App::serviceEngine(const lichess::OfferedEngine& o, std::string& error) const {
    const int index = engineIndex(o.engine);
    if (index < 0) {
        error = trf("{} is no longer in the library.", o.engine);
        return std::nullopt;
    }
    auto e = engines_[static_cast<std::size_t>(index)];
    if (o.machine.empty() || o.machine == e.machine) return e;  // here, or on its own machine
    const auto m =
        std::find_if(machines_.begin(), machines_.end(), [&](const Machine& x) { return x.id == o.machine; });
    if (m == machines_.end()) {
        error = TR("The machine chosen is no longer in Settings > Machines.");
        return std::nullopt;
    }
    const auto* there = m->engine(e.name);
    if (!there || there->path.empty()) {
        error = trf("{} has no path on {}: give it in Settings > Machines > Engines on this machine.", e.name,
                    m->name.empty() ? m->host : m->name);
        return std::nullopt;
    }
    e.machine = m->id;
    e.cmd     = there->path;
    e.dir     = there->copied_dir;
    for (auto& [name, value] : e.options) {
        if (name == "SyzygyPath" && !value.empty())
            value = m->syzygyDir();
        else
            value = optionThere(*there, m->system.shell, value);  // a file of its folder, in its copy there
    }
    return e;
}

int App::lichessServiceThreads() const {
    int threads = 0;
    for (const auto& [name, s] : services_)
        if (s.engine.machine.empty()) threads += s.status.threads_now;
    return threads;
}

std::string App::serviceState(const std::string& engine) const {
    if (const auto it = services_.find(engine); it != services_.end()) {
        const auto& st = it->second.status;
        switch (st.state) {
            case lichess::ServiceState::Busy:
                return TR("in service, 1 request in progress");
            case lichess::ServiceState::Waiting:
                return TR("in service, waiting for a request");
            case lichess::ServiceState::Paused:
                return st.pause == lichess::ServicePause::LiveGame ? TR("paused during your live game")
                                                                   : TR("paused: a tournament takes the threads");
            case lichess::ServiceState::Offline:
                return TR("waiting for the network");
            default:
                return TR("stopped");
        }
    }
    if (const auto it = service_ended_.find(engine); it != service_ended_.end()) {
        switch (it->second.state) {
            case lichess::ServiceState::Refused:
                return TR("stopped: Lichess refuses the secret");
            case lichess::ServiceState::ApiChanged:
                return TR("stopped: Lichess's API changed");
            default:
                return TR("stopped: the engine could not serve");
        }
    }
    return TR("stopped");
}

// ---------------------------------------------------------------------------
// Offer, serve, stop, withdraw

void App::openOfferForm(const EngineEntry& e) {
    OfferForm f;
    f.engine        = e.name;
    const auto* o   = offerOf(e.name);
    const auto name = o && !o->name.empty() ? o->name : fmt::format("{} (RookStudio)", e.name);
    std::snprintf(f.name.data(), f.name.size(), "%s", name.c_str());
    f.machine = o ? o->machine : e.machine;
    // Half the machine's threads, 8 at most, by default; 1024 MB, less on a small machine.
    f.threads   = o && o->threads > 0 ? o->threads : std::clamp(topology_.logical / 2, 1, 8);
    f.hash_mb   = o && o->hash_mb > 0 ? o->hash_mb : (installed_mb_ == 0 || installed_mb_ >= 8192 ? 1024 : 256);
    offer_form_ = f;
    offer_errors_.erase(e.name);
    page_ = Page::Engines;
    for (int i = 0; i < static_cast<int>(engines_.size()); ++i)
        if (engines_[static_cast<std::size_t>(i)].name == e.name) selected_engine_ = i;
}

void App::offerEngine(const std::string& engine) {
    offer_errors_.erase(engine);
    if (!lichess_account_ || !engineScopes()) return;  // the card asks for the link or the permissions first
    if (std::any_of(offer_calls_.begin(), offer_calls_.end(), [&](const OfferCall& c) { return c.engine == engine; }))
        return;
    lichess::OfferedEngine o;
    if (const auto* had = offerOf(engine)) o = *had;
    o.engine = engine;
    if (offer_form_ && offer_form_->engine == engine) {
        o.name    = nameOf(offer_form_->name);
        o.machine = offer_form_->machine;
        o.threads = offer_form_->threads;
        o.hash_mb = offer_form_->hash_mb;
    }
    if (o.name.empty()) o.name = fmt::format("{} (RookStudio)", engine);
    if (o.threads < 1) o.threads = std::clamp(topology_.logical / 2, 1, 8);
    if (o.hash_mb < 1) o.hash_mb = 1024;
    try {
        o.secret = lichess::newProviderSecret();
    } catch (const std::exception& e) {
        offer_errors_[engine] = e.what();
        return;
    }
    const lichess::EngineRegistration r{o.name, o.threads, o.hash_mb, o.secret, {}};
    if (const auto problem = lichess::registrationProblem(r); !problem.empty()) {
        offer_errors_[engine] = problem;
        return;
    }
    OfferCall call;
    call.engine = engine;
    call.offer  = o;
    // Registered again with a new secret when Lichess still has it; else a new registration.
    call.kind   = o.id.empty() ? OfferCall::Kind::Register : OfferCall::Kind::Update;
    call.result = std::async(std::launch::async, [token = lichess_account_->token, r, id = o.id] {
        if (id.empty()) return lichess::registerEngine(token, r);
        auto updated = lichess::updateEngine(token, id, r);
        if (updated.net == lichess::NetError::NotFound)
            return lichess::registerEngine(token, r);  // withdrawn on lichess.org meanwhile
        return updated;
    });
    offer_calls_.push_back(std::move(call));
}

void App::registerAgain(const std::string& engine) {
    service_ended_.erase(engine);
    if (!engineScopes()) {
        if (const auto* o = offerOf(engine); o) {
            // The form, its values kept: the permissions asked from it.
            for (const auto& e : engines_)
                if (e.name == engine) openOfferForm(e);
            if (offer_form_) offer_form_->asked = true;
        }
        return;
    }
    offerEngine(engine);
}

void App::startService(const std::string& engine) {
    auto* o = offerOf(engine);
    if (!o || o->id.empty() || o->secret.empty() || services_.count(engine)) return;
    std::string error;
    auto run = serviceEngine(*o, error);
    if (!run) {
        offer_errors_[engine] = error;
        return;
    }
    if (!run->machine.empty()) {
        syncEngineHub();
        if (!remoteReady(*run, [this, engine] { startService(engine); })) return;  // its key's passphrase asked first
    }
    LichessService s;
    s.engine = *run;
    lichess::EngineService::Setup setup;
    setup.engine      = *run;
    setup.secret      = o->secret;
    setup.engine_id   = o->id;
    setup.max_threads = std::max(1, o->threads);
    setup.max_hash    = std::max(1, o->hash_mb);
    setup.host        = lichess::servers().engine;
    s.service         = std::make_unique<lichess::EngineService>(std::move(setup));
    services_[engine] = std::move(s);
    service_ended_.erase(engine);
    offer_errors_.erase(engine);
    if (!o->serving) {
        o->serving = true;
        saveOffers();
    }
}

void App::stopService(const std::string& engine) {
    if (const auto it = services_.find(engine); it != services_.end()) {
        // Its thread ends within a quarter of a second, a request under way within its engine's stop: not waited
        // for here.
        jobs_.park(std::async(std::launch::async, [s = std::move(it->second.service)]() mutable { s.reset(); }));
        services_.erase(it);
    }
    if (auto* o = offerOf(engine); o && o->serving) {
        o->serving = false;
        saveOffers();
    }
}

void App::withdrawOffer(const std::string& engine) {
    stopService(engine);
    auto* o = offerOf(engine);
    if (!o) return;
    service_ended_.erase(engine);
    if (o->id.empty()) {
        std::erase_if(offers(), [&](const lichess::OfferedEngine& x) { return x.engine == engine; });
        saveOffers();
        return;
    }
    if (!lichess_account_) {
        offer_errors_[engine] = TR("Withdrawing it needs your Lichess account linked: link it on the Lichess page.");
        return;
    }
    OfferCall call;
    call.engine    = engine;
    call.kind      = OfferCall::Kind::Withdraw;
    call.offer     = *o;
    call.withdrawn = std::async(std::launch::async, [token = lichess_account_->token, id = o->id] {
        return lichess::withdrawEngine(token, id);
    });
    offer_calls_.push_back(std::move(call));
}

void App::pollLichessEngines() {
    const auto now = SteadyClock::now();
    if (!service_stop_.empty()) stopService(std::exchange(service_stop_, std::string()));
    // At the start: those in service when RookStudio closed (nothing asked of the internet for a scripted capture).
    if (!offers_resumed_) {
        offers_resumed_ = true;
        if (!options_.scripted || lichess::servers().engine.rfind("http://127.0.0.1:", 0) == 0)
            for (const auto& name : lichess::offersToResume(offers(), engines_)) startService(name);
    }
    // The requests about registrations.
    for (auto it = offer_calls_.begin(); it != offer_calls_.end();) {
        auto& c = *it;
        if (c.kind == OfferCall::Kind::Withdraw) {
            if (c.withdrawn.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                ++it;
                continue;
            }
            if (const auto failure = c.withdrawn.get(); !failure.failed()) {
                std::erase_if(offers(), [&](const lichess::OfferedEngine& x) { return x.engine == c.engine; });
                saveOffers();
            } else {
                offer_errors_[c.engine] = lichess::describe(failure);
            }
            it = offer_calls_.erase(it);
            continue;
        }
        if (c.result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        auto answer = c.result.get();
        if (answer.value) {
            auto o    = c.offer;
            o.id      = answer.value->id;
            o.user    = answer.value->user_id;
            o.serving = true;
            if (auto* had = offerOf(c.engine))
                *had = o;
            else
                offers().push_back(o);
            saveOffers();
            if (offer_form_ && offer_form_->engine == c.engine) offer_form_.reset();
            stopService(c.engine);  // a service with the secret before, if one ran
            startService(c.engine);
        } else {
            offer_errors_[c.engine] = lichess::describe(answer);
        }
        it = offer_calls_.erase(it);
    }
    // Linked with the permissions: the registration, without asking anything more.
    if (!offer_after_link_.empty() && !lichess_link_) {
        const auto engine = std::exchange(offer_after_link_, std::string());
        if (engineScopes()) offerEngine(engine);
    }
    // The services: their guards, their state, today's counts.
    const bool live = liveGameOn() != nullptr;
    std::vector<std::string> locked;
    bool start_locked = false;
    for (const auto& g : lichess_playing_) {
        if (g.live()) continue;
        if (g.atStart())
            start_locked = true;
        else if (!g.fen.empty())
            locked.push_back(g.fen);
    }
    int local_tournaments = 0;
    std::map<std::string, int> machine_tournaments;
    for (const auto& r : runs_) {
        const auto* t = r.run->tournament();
        if (!t || !r.run->running()) continue;
        // Paused, its games in progress only, then nothing: its cores are taken back as soon as it resumes.
        const int games = r.run->paused() ? r.run->runningGames() : std::max(1, t->spec().conditions.concurrency);
        const int need  = games * tour::largestGame(t->spec()).threads;
        if (!r.machine_id.empty())
            machine_tournaments[r.machine_id] += need;
        else if (r.run->remote())
            local_tournaments += need;
    }
    const int local_others = localThreads();  // the window's engines and the bot's game, not the services'
    for (auto it = services_.begin(); it != services_.end();) {
        auto& [name, s] = *it;
        auto* o         = offerOf(name);
        lichess::ServiceGuards g;
        g.live_game     = live;
        g.locked        = locked;
        g.start_locked  = start_locked;
        const int limit = o ? std::max(1, o->threads) : 1;
        int others      = 0;  // the other services' threads on the same machine
        for (const auto& [other, t] : services_)
            if (other != name && t.engine.machine == s.engine.machine) others += t.status.threads_now;
        if (s.engine.machine.empty()) {
            g.threads =
                lichess::serviceThreads(limit, machineBudget().threads, local_others + others, local_tournaments);
        } else {
            const auto m       = std::find_if(machines_.begin(), machines_.end(),
                                              [&](const Machine& x) { return x.id == s.engine.machine; });
            const int capacity = m == machines_.end() ? limit : m->coreBudget();
            g.threads = lichess::serviceThreads(limit, capacity, others, machine_tournaments[s.engine.machine]);
        }
        s.service->setGuards(g);
        (void)s.service->status(s.status, s.version);
        if (const auto counts = s.service->takeCounts(); (counts.requests > 0 || counts.ms > 0) && o) {
            lichess::countRequest(*o, counts.requests, counts.ms, today());
            offers_dirty_ = true;
        }
        if (s.service->ended()) {
            // Refused, the API changed, the engine could not serve: said on its card, not served at the next start.
            service_ended_[name] = s.service->status();
            const auto engine    = name;
            jobs_.park(std::async(std::launch::async, [svc = std::move(s.service)]() mutable { svc.reset(); }));
            it = services_.erase(it);
            if (auto* stopped = offerOf(engine); stopped && stopped->serving) {
                stopped->serving = false;
                offers_dirty_    = true;
            }
            continue;
        }
        ++it;
    }
    // A service whose engine changed in the library (an update, another program, its options): started again with it
    // once its request is answered, and the version it played freed.
    std::vector<std::string> restart;
    for (const auto& [name, s] : services_) {
        const int index = engineIndex(name);
        const auto* o   = offerOf(name);
        if (index < 0 || !o || s.status.state == lichess::ServiceState::Busy) continue;
        const auto& e = engines_[static_cast<std::size_t>(index)];
        if (!o->machine.empty() && o->machine != e.machine) continue;  // its copy on another machine: its path there
        if (e.cmd != s.engine.cmd || e.args != s.engine.args || e.dir != s.engine.dir || e.options != s.engine.options)
            restart.push_back(name);
    }
    for (const auto& name : restart) {
        stopService(name);
        startService(name);
        checkVersions();
    }
    if (offers_dirty_ && now - offers_written_ > std::chrono::seconds(10)) saveOffers();
}

// ---------------------------------------------------------------------------
// On the engine's card

void App::drawLichessEnginePart(const EngineEntry& e) {
    const ui::TopicRegion topic(guide::topics::LichessEngine);
    auto* o            = offerOf(e.name);
    const bool offered = o && !o->id.empty();
    const bool form    = offer_form_ && offer_form_->engine == e.name;
    const bool requested =
        std::any_of(offer_calls_.begin(), offer_calls_.end(), [&](const OfferCall& c) { return c.engine == e.name; });
    if (!offered && !form && !requested && offer_after_link_ != e.name) {
        drawLichessEngineLine(e);
        return;
    }
    ImGui::Spacing();
    beginTinted("##lichess_engine_part", ui::ACCENT);
    ImGui::PushTextWrapPos();
    ui::inlineIcon(ui::Icon::Globe, ui::ACCENT);
    ImGui::SameLine();
    heading(TR("On lichess.org, for the analysis"));
    if (offered && !(form && !services_.count(e.name) && offer_form_->asked))
        drawLichessEngineService(e, *o);
    else
        drawLichessEngineForm(e);
    ImGui::PopTextWrapPos();
    endTinted();
}

void App::drawLichessEngineLine(const EngineEntry& e) {
    ui::inlineIcon(ui::Icon::Globe, ui::ACCENT);
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("On lichess.org, for the analysis"));
    ImGui::SameLine();
    if (!knowsChess960(e)) {
        ImGui::TextDisabled("%s", TR("· Lichess asks for the Chess960 mode, which this engine does not know: it cannot "
                                     "serve."));
        tip(
            TR("Lichess runs an engine offered to its analysis board in the Chess960 mode (UCI_Chess960), for every "
               "game; an engine without it cannot be offered."));
        return;
    }
    if (ImGui::SmallButton(TR("Offer to Lichess…"))) openOfferForm(e);
    tip(
        TR("Your engine in Lichess's analysis board: it computes here, on your machine, and its lines show there. Only "
           "your account sees it, in the analysis board's engine menu, while RookStudio is open."));
    const char* summary = TR("its lines in lichess.org's analysis board, for your account only");
    ui::sameLineOrWrap(ImGui::CalcTextSize(summary).x);
    ImGui::TextDisabled("%s", summary);
}

void App::drawLichessEngineForm(const EngineEntry& e) {
    if (!offer_form_ || offer_form_->engine != e.name) openOfferForm(e);
    auto& f = *offer_form_;
    const bool calling =
        std::any_of(offer_calls_.begin(), offer_calls_.end(), [&](const OfferCall& c) { return c.engine == e.name; });
    wrapped(
        TR("Your engine in Lichess's analysis board: it computes here, on your machine, and its lines show there. Only "
           "your account sees it, in the analysis board's engine menu, while RookStudio is open."));
    ImGui::Spacing();
    // The fields, two by two.
    ImGui::BeginDisabled(calling);
    // A row of labels, then a row of fields: a cell's frame would lower the text of the next cell of its row.
    if (ImGui::BeginTable("##offer_fields", 2, ImGuiTableFlags_SizingStretchSame)) {
        // This computer, and the machines of Settings where the engine has a path.
        std::vector<std::pair<std::string, std::string>> machines;  // id, label
        if (e.machine.empty())
            machines.emplace_back("", trn("This computer · {} thread", "This computer · {} threads", topology_.logical,
                                          topology_.logical));
        for (const auto& m : machines_) {
            const auto* there = m.engine(e.name);
            if (m.id != e.machine && (!there || there->path.empty())) continue;
            const int threads = std::max(1, m.system.threads);
            machines.emplace_back(
                m.id, trn("{} · {} thread", "{} · {} threads", threads, m.name.empty() ? m.host : m.name, threads));
        }
        std::string current;
        for (const auto& [id, label] : machines)
            if (id == f.machine) current = label;
        if (current.empty() && !machines.empty()) {
            f.machine = machines.front().first;
            current   = machines.front().second;
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", TR("Its name at Lichess"));
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("%s", TR("The machine"));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##offer_name", f.name.data(), f.name.size());
        const int length = lichess::nameLength(nameOf(f.name));
        if (length < lichess::NAME_CHARS_MIN || length > lichess::NAME_CHARS_MAX)
            coloured(
                trf("{} characters: Lichess takes {} to {}.", length, lichess::NAME_CHARS_MIN, lichess::NAME_CHARS_MAX),
                ui::WARN);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##offer_machine", current.c_str())) {
            for (const auto& [id, label] : machines)
                if (ImGui::Selectable(label.c_str(), id == f.machine)) f.machine = id;
            ImGui::EndCombo();
        }
        tip(
            TR("A remote machine of the Tests view (Settings > Machines, by SSH) can serve instead of this computer, "
               "with its own threads; the engine must have its path there."));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", TR("Threads at most (Lichess asks between 1 and this number)"));
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("%s", TR("Memory at most"));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::SetNextItemWidth(px(120));
        if (ui::inputInt("##offer_threads", &f.threads)) f.threads = std::clamp(f.threads, 1, lichess::THREADS_MOST);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(px(120));
        if (ui::inputInt("##offer_hash", &f.hash_mb)) f.hash_mb = std::clamp(f.hash_mb, 1, lichess::HASH_MOST_MB);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("MB"));
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
    // The Chess960 mode Lichess asks for.
    ui::inlineIcon(ui::Icon::Check, ui::GOOD);
    ImGui::SameLine();
    if (e.uci_options.empty())
        wrapped(TR("Chess, in the Chess960 mode Lichess asks for: checked when the engine starts."));
    else
        wrapped(trf("Chess, in the Chess960 mode Lichess asks for: {} knows it.", e.name));
    ImGui::Spacing();
    const auto error = offer_errors_.find(e.name);
    if (error != offer_errors_.end()) coloured(trf("Lichess gave nothing ({}).", error->second), ui::WARN);
    const int length = lichess::nameLength(nameOf(f.name));
    const bool valid = length >= lichess::NAME_CHARS_MIN && length <= lichess::NAME_CHARS_MAX;
    const bool enter = !ImGui::GetIO().WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                       ImGui::IsKeyPressed(ImGuiKey_Enter, false);
    if (calling) {
        coloured(TR("Registering the engine with Lichess…"), ui::ACCENT);
    } else if (lichess_link_ && offer_after_link_ == e.name) {
        coloured(TR("Waiting for Lichess: allow RookStudio in the browser's page, then come back here."), ui::ACCENT);
    } else if (f.asked && !lichess_account_) {
        // Without a linked account (M4): the card says so.
        beginTinted("##offer_link", ui::ACCENT);
        heading(TR("Your Lichess account is not linked"));
        wrapped(TR("Lichess offers an engine to the analysis board of an account: linking yours asks for the "
                   "permissions to register an engine (engine:read, engine:write). Lichess's page opens."),
                true);
        if (lichess_error_.failed())
            coloured(trf("Lichess gave nothing ({}).", lichess::describe(lichess_error_)), ui::WARN);
        if (primaryButton(TR("Link the account…")) || enter) {
            offer_after_link_ = e.name;
            startLichessLink({lichess::SCOPE_ENGINE_READ, lichess::SCOPE_ENGINE_WRITE});
        }
        keyHint(TR("Enter"));
        ImGui::SameLine();
        if (ImGui::Button(TR("Not now##offer_link"))) f.asked = false;
        endTinted();
    } else if (f.asked && !engineScopes()) {
        // The first time: the permissions, as #45's card asks them.
        beginTinted("##offer_scope", ui::ACCENT);
        heading(TR("The first time: Lichess asks for two more permissions"));
        wrapped(TR("engine:read and engine:write: to register an engine for your account, read it, withdraw it. "
                   "Lichess's page opens; your account stays linked, the token it replaces is revoked."),
                true);
        if (lichess_error_.failed())
            coloured(trf("Lichess gave nothing ({}).", lichess::describe(lichess_error_)), ui::WARN);
        if (primaryButton(TR("Allow on Lichess…##engine")) || enter) {
            offer_after_link_ = e.name;
            auto scopes       = lichess::scopesAdding(lichess_account_->scopes, lichess::SCOPE_ENGINE_READ);
            scopes            = lichess::scopesAdding(scopes, lichess::SCOPE_ENGINE_WRITE);
            startLichessLink(scopes, lichess_account_->username);
        }
        keyHint(TR("Enter"));
        ImGui::SameLine();
        if (ImGui::Button(TR("Not now##engine"))) f.asked = false;
        endTinted();
        ImGui::Spacing();
        heading(TR("Then, without asking anything more"));
        wrapped(TR("1. RookStudio registers the engine with your account (one request, with the token)."), true);
        wrapped(TR("2. It waits for work at engine.lichess.ovh with the secret alone: a long wait, renewed while the "
                   "service runs."),
                true);
        wrapped(trf("3. On lichess.org, the analysis board offers “{}” in its engine menu.", nameOf(f.name)), true);
    } else {
        if (primaryButton(TR("Offer to Lichess…"), valid) || (enter && valid)) {
            f.asked = true;
            if (engineScopes()) offerEngine(e.name);
        }
        keyHint(TR("Enter"));
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel##offer"))) {
            offer_form_.reset();
            offer_errors_.erase(e.name);
            return;
        }
    }
    wrapped(TR("What leaves the machine: this name, these limits and a secret drawn here, which Lichess never shows "
               "again; then, at each request of your analysis board, the engine's lines. A remote machine of the "
               "Tests view (SSH) can serve instead: its own threads."),
            true);
}

void App::drawLichessEngineService(const EngineEntry& e, lichess::OfferedEngine& o) {
    const auto service = services_.find(e.name);
    const bool serving = service != services_.end();
    const auto ended   = service_ended_.find(e.name);
    const bool calling =
        std::any_of(offer_calls_.begin(), offer_calls_.end(), [&](const OfferCall& c) { return c.engine == e.name; });
    // Who it is at Lichess.
    std::string where = TR("this computer");
    if (!o.machine.empty())
        for (const auto& m : machines_)
            if (m.id == o.machine) where = m.name.empty() ? m.host : m.name;
    ImGui::TextDisabled("%s", trf("“{}” at Lichess, for {}'s account · {} · {} at most", o.name,
                                  o.user.empty() ? std::string("?") : o.user,
                                  trn("{} thread", "{} threads", o.threads, o.threads), megabytes(o.hash_mb))
                                  .c_str());
    ImGui::TextDisabled("%s", trf("Computed on {}.", where).c_str());
    const std::string day = o.day == today() ? today() : std::string();
    const int requests    = day.empty() ? 0 : o.requests;
    const auto minutes    = day.empty() ? 0 : static_cast<int>((o.analysis_ms + 30000) / 60000);
    if (serving) {
        const auto& st     = service->second.status;
        const bool busy    = st.state == lichess::ServiceState::Busy && st.job;
        const bool paused  = st.state == lichess::ServiceState::Paused;
        const bool offline = st.state == lichess::ServiceState::Offline;
        const ImU32 tone   = offline ? ui::WARN : paused ? ui::ACCENT : ui::GOOD;
        // The state, in green while it serves.
        beginTinted("##service_state", tone);
        const auto* job = busy ? &*st.job : st.last ? &*st.last : nullptr;
        if (job && !job->fen.empty()) {
            // The position asked, and the arrow of the engine's best line.
            chess::Board board;
            board.set960(true);
            board.setFen(job->fen);
            BoardDecor decor;
            decor.eval_white  = -1.0f;
            decor.coordinates = false;
            // The engine's first move, as UCI writes it (the board reads nothing else).
            if (lichess::uciMoveText(job->best)) {
                try {
                    decor.arrows.push_back({chess::uci::uciToMove(board, job->best), ui::ACCENT});
                } catch (...) {
                }
            }
            ImGui::BeginGroup();
            ImGui::TextDisabled("%s", busy ? TR("The position asked") : TR("The last position asked"));
            drawBoard(atlas_, board, px(162), decor);
            ImGui::EndGroup();
            ImGui::SameLine(0, px(14));
        }
        ImGui::BeginGroup();
        {
            auto* dl        = ImGui::GetWindowDrawList();
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float r   = px(5);
            dl->AddCircleFilled(ImVec2(at.x + r, at.y + ImGui::GetTextLineHeight() * 0.5f), r, tone);
            ImGui::Dummy(ImVec2(r * 2, ImGui::GetTextLineHeight()));
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(tone));
        ImGui::TextUnformatted(offline ? TR("Offline") : paused ? TR("Paused") : TR("In service"));
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", busy      ? TR("· Lichess analyses with it")
                                  : paused  ? TR("· the requests stay unanswered meanwhile")
                                  : offline ? TR("· it waits for the network, then resumes by itself")
                                            : TR("· waiting for a request from your analysis board"));
        if (offline) {
            // When it asks again, and why it could not.
            const auto left =
                std::chrono::duration_cast<std::chrono::seconds>(st.retry_at - SteadyClock::now()).count();
            if (st.failure.net == lichess::NetError::RateLimited)
                coloured(TR("Too many requests (429): Lichess asks to wait a minute."), ui::WARN);
            else
                wrapped(lichess::describe(st.failure), true);
            ImGui::TextDisabled("%s", left > 0 ? trf("Asked again in {} s.", left).c_str() : TR("Asking again…"));
        }
        if (job && job->locked)
            coloured(TR("The last request was a position of one of your correspondence games: answered empty, never "
                        "analysed."),
                     ui::ACCENT);
        else if (busy)
            ImGui::TextUnformatted(jobLine(*job, true).c_str());
        else if (job && job->cut)
            ImGui::TextDisabled(
                "%s", trf("The last request, closed by Lichess before its end: {}", jobLine(*job, false)).c_str());
        else if (job)
            ImGui::TextDisabled("%s", trf("The last request: {}", jobLine(*job, false)).c_str());
        ImGui::TextDisabled("%s", trf("Today: {}, {}.", trn("{} request", "{} requests", requests, requests),
                                      trn("{} min of analysis", "{} min of analysis", minutes, minutes))
                                      .c_str());
        if (ImGui::SmallButton(TR("Open Lichess's analysis board")))
            SDL_OpenURL((lichess::servers().api + "/analysis").c_str());
        tip(TR("lichess.org's analysis board in the browser: choose this engine in its engine menu."));
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Stop the service"))) service_stop_ = e.name;  // at the next frame: drawn from it
        tip(
            TR("RookStudio no longer waits for work; the engine stays registered at Lichess, unanswered. “Resume” "
               "serves it again."));
        ImGui::EndGroup();
        endTinted();
        // The guards (R6, R17).
        if (paused && st.pause == lichess::ServicePause::LiveGame)
            (void)messageBox("##service_live", ui::Icon::Lock, ui::ACCENT,
                             TR("Your live game on Lichess: the service is paused until its end"),
                             TR("The requests stay unanswered; it resumes by itself at the end of the game."));
        std::string tournament;  // a tournament playing where it computes
        for (const auto& r : runs_)
            if (r.run->running() && r.run->tournament() && r.machine_id == service->second.engine.machine)
                tournament = r.run->tournament()->spec().name;
        if (!tournament.empty()) {
            if (paused && st.pause == lichess::ServicePause::Tournament)
                calmBox(
                    "##service_tournament", ui::Icon::Engine,
                    trf("The tournament “{}” is playing and leaves no thread free: the service is paused", tournament),
                    TR("It resumes by itself once threads are free again; Lichess waits. The tournament "
                       "first, as for the bot."));
            else
                calmBox("##service_tournament", ui::Icon::Engine,
                        trf("The tournament “{}” is playing: the service takes the threads it leaves free", tournament),
                        TR("If none is left, it pauses and says so here; Lichess waits. The tournament first, "
                           "as for the bot."));
        }
        if (bot_->playing() && service->second.engine.machine.empty())
            calmBox("##service_bot", ui::Icon::Bot, TR("Your Lichess bot is playing a game"),
                    TR("Its threads first; the service makes do with the rest."));
        if (std::any_of(lichess_playing_.begin(), lichess_playing_.end(),
                        [](const lichess::PlayingGame& g) { return !g.live(); }))
            (void)messageBox("##service_correspondence", ui::Icon::Lock, ui::ACCENT,
                             TR("Your correspondence games: their positions are never analysed"),
                             TR("A request on one of them (compared by the men and the side to move, as the fair "
                                "play's lock does) gets an empty answer."));
        ImGui::Spacing();
        wrapped(TR("Also on the Lichess page, under “Your engines on lichess.org”, and on the Home page, under “In "
                   "progress”."),
                true);
        return;
    }
    // Stopped by itself: the error, and its fix.
    if (ended != service_ended_.end()) {
        const auto& st = ended->second;
        if (st.state == lichess::ServiceState::Refused) {
            (void)messageBox("##service_refused", ui::Icon::Alert, ui::WARN, TR("Lichess refuses the secret (401)"),
                             TR("The registration was withdrawn on lichess.org, or the secret changed."));
        } else if (st.state == lichess::ServiceState::ApiChanged) {
            if (messageBox("##service_api", ui::Icon::Alert, ui::WARN,
                           TR("The API changed at Lichess (it is in alpha)"),
                           trf("The service stopped rather than answer wrongly: {}.", st.error),
                           TR("See whether a version of RookStudio follows it")))
                SDL_OpenURL(update::RELEASES_PAGE);
        } else {
            (void)messageBox("##service_engine", ui::Icon::Engine, ui::WARN, trf("{} could not serve", e.name),
                             st.chess960_missing
                                 ? std::string(TR("Lichess asks for the Chess960 mode, which this engine does not "
                                                  "know: it cannot serve."))
                                 : st.error);
        }
    } else {
        wrapped(TR("Stopped: RookStudio no longer waits for work; the engine stays registered at Lichess, unanswered."),
                true);
        if (requests > 0)
            ImGui::TextDisabled("%s", trf("Today: {}, {}.", trn("{} request", "{} requests", requests, requests),
                                          trn("{} min of analysis", "{} min of analysis", minutes, minutes))
                                          .c_str());
    }
    if (const auto error = offer_errors_.find(e.name); error != offer_errors_.end())
        coloured(trf("Lichess gave nothing ({}).", error->second), ui::WARN);
    if (calling) {
        coloured(TR("Asking Lichess…"), ui::ACCENT);
        return;
    }
    const bool enter = !ImGui::GetIO().WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                       ImGui::IsKeyPressed(ImGuiKey_Enter, false);
    // The secret refused: served again with a new one only; else served again as it was.
    if (ended != service_ended_.end() && ended->second.state == lichess::ServiceState::Refused) {
        if (primaryButton(TR("Register again (a new secret)")) || enter) registerAgain(e.name);
        tip(
            TR("A new secret drawn here, and the registration changed at Lichess with it (one request, with your "
               "token); made again if it was withdrawn."));
    } else if (primaryButton(TR("Resume")) || enter) {
        startService(e.name);
    }
    keyHint(TR("Enter"));
    ImGui::SameLine(0, px(16));
    if (ImGui::SmallButton(TR("Change…"))) {
        openOfferForm(e);
        offer_form_->asked = true;
    }
    tip(TR("Its name at Lichess, the machine, its threads and memory at most: registered again with Lichess."));
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Withdraw from Lichess"))) withdrawOffer(e.name);
    tip(
        TR("The registration is deleted at Lichess (one request) and the secret is no longer used. “Offer to Lichess…” "
           "makes it again."));
    wrapped(
        TR("RookStudio closed, the engine stays in Lichess's list without answering; at the next start, the service "
           "resumes if it was in service when you closed."),
        true);
}

// ---------------------------------------------------------------------------
// The Lichess page, the Home page, the Tournaments page

void App::drawLichessEnginesCard() {
    std::vector<const lichess::OfferedEngine*> shown;
    for (const auto& o : offers())
        if (!o.id.empty()) shown.push_back(&o);
    if (shown.empty()) return;
    const ui::TopicRegion topic(guide::topics::LichessEngine);
    ui::beginCard("lichess_engines");
    ImGui::PushTextWrapPos();
    ui::inlineIcon(ui::Icon::Globe, ui::ACCENT);
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("Your engines on lichess.org"));
    std::string withdraw;  // after the list: it changes the offers
    for (const auto* o : shown) {
        ImGui::PushID(o->engine.c_str());
        ImGui::TextUnformatted(o->engine.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("· %s", serviceState(o->engine).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("The engine's card"))) {
            page_ = Page::Engines;
            for (int i = 0; i < static_cast<int>(engines_.size()); ++i)
                if (engines_[static_cast<std::size_t>(i)].name == o->engine) selected_engine_ = i;
        }
        ImGui::SameLine();
        if (services_.count(o->engine)) {
            if (ImGui::SmallButton(TR("Stop the service"))) service_stop_ = o->engine;
        } else if (engineIndex(o->engine) < 0) {
            // Its engine left the library: its registration can still be withdrawn from here.
            if (ImGui::SmallButton(TR("Withdraw from Lichess"))) withdraw = o->engine;
            tip(trf("{} is no longer in the library. The registration is deleted at Lichess (one request).",
                    o->engine));
        } else if (!service_ended_.count(o->engine)) {
            if (ImGui::SmallButton(TR("Resume"))) startService(o->engine);
        }
        ImGui::PopID();
    }
    ImGui::TextDisabled("%s", TR("Offered from an engine's card on the Engines page; lichess.org shows them in its "
                                 "analysis board's engine menu, for your account only."));
    if (const auto error =
            std::find_if(shown.begin(), shown.end(),
                         [&](const lichess::OfferedEngine* o) { return offer_errors_.count(o->engine); });
        error != shown.end())
        coloured(trf("Lichess gave nothing ({}).", offer_errors_[(*error)->engine]), ui::WARN);
    ImGui::PopTextWrapPos();
    ui::endCard();
    if (!withdraw.empty()) withdrawOffer(withdraw);
}

void App::drawLichessEngineRunning(bool home) {
    if (services_.empty()) return;
    const ui::TopicRegion topic(guide::topics::LichessEngine);
    if (!home) {
        ImGui::Spacing();
        ui::beginCard("lichess_engine_running");
    }
    for (const auto& [name, s] : services_) {
        ImGui::PushID(name.c_str());
        ui::inlineIcon(ui::Icon::Globe, ui::ACCENT);
        ImGui::SameLine();
        ImGui::TextUnformatted(trf("Your engine on lichess.org: {}", name).c_str());
        ImGui::SameLine();
        std::string state = serviceState(name);
        if (s.status.state == lichess::ServiceState::Busy && s.status.job && s.status.job->depth > 0)
            state += " · " + trf("depth {}", s.status.job->depth);
        ImGui::TextDisabled("· %s", state.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("The engine's card"))) {
            page_ = Page::Engines;
            for (int i = 0; i < static_cast<int>(engines_.size()); ++i)
                if (engines_[static_cast<std::size_t>(i)].name == name) selected_engine_ = i;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Stop the service"))) service_stop_ = name;
        ImGui::PopID();
    }
    if (!home) ui::endCard();
}

}  // namespace fcg
