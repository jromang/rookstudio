#include "assistant_service.hpp"

#include "storage.hpp"

#ifdef FCG_ASSISTANT
#    include "assistant_process.hpp"
#endif

namespace fcg {

AssistantService::AssistantService(std::filesystem::path profile, std::string program)
    : profile_(std::move(profile)), program_(std::move(program)) {
    if (const auto json = readJson<nlohmann::json>(profile_ / "assistant.json")) {
        try {
            settings_ = json->get<AssistantSettings>();
        } catch (const std::exception&) {
            settings_ = AssistantSettings{};
        }
    }
}

AssistantService::~AssistantService() {
    cancelDownload();
    if (download_.valid()) download_.wait();
    std::lock_guard lock(mutex_);
    process_.reset();
}

void AssistantService::saveSettings() { (void)writeJson(profile_ / "assistant.json", settings_); }

const AssistantModel& AssistantService::recommended() const { return assistantModels().front(); }

std::filesystem::path AssistantService::modelPath() const {
    return settings_.model_file.empty() ? modelFile(profile_, recommended())
                                        : std::filesystem::path(settings_.model_file);
}

bool AssistantService::installed() const {
    if (settings_.model_file.empty()) return modelInstalled(profile_, recommended());
    std::error_code ec;
    return std::filesystem::is_regular_file(settings_.model_file, ec);
}

void AssistantService::download() {
    if (downloading()) return;
    progress_ = std::make_shared<DownloadProgress>();
    cancel_   = std::make_shared<std::atomic<bool>>(false);
    download_error_.clear();
    speed_at_    = std::chrono::steady_clock::now();
    speed_bytes_ = 0;
    speed_       = 0;
    download_    = std::async(std::launch::async,
                              [profile = profile_, model = recommended(), progress = progress_, cancel = cancel_] {
                               return downloadModel(profile, model, progress.get(), cancel.get());
                              });
}

void AssistantService::cancelDownload() {
    if (cancel_) *cancel_ = true;
}

bool AssistantService::downloading() const { return download_.valid(); }

uint64_t AssistantService::downloaded() const { return progress_ ? progress_->bytes.load() : 0; }

double AssistantService::downloadSpeed() const { return speed_; }

std::string AssistantService::downloadError() const { return download_error_; }

AssistantService::State AssistantService::state() const {
    if (downloading()) return State::Downloading;
    if (!installed()) return State::NoModel;
    if (loading_) return State::Loading;
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock) return State::Ready;  // a request holds it: the model is loaded and answers
#ifdef FCG_ASSISTANT
    if (process_ && process_->running()) return State::Ready;
#endif
    return failure_.empty() ? State::Unloaded : State::Failed;
}

std::string AssistantService::devices() const {
    std::unique_lock lock(mutex_, std::try_to_lock);
    return lock ? devices_ : std::string();
}

std::string AssistantService::failure() const {
    std::unique_lock lock(mutex_, std::try_to_lock);
    return lock ? failure_ : std::string();
}

void AssistantService::unload() {
    // A request under way keeps the model until it is answered: the next frame unloads it.
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock) {
        unload_asked_ = true;
        return;
    }
    unload_asked_ = false;
    process_.reset();
    failure_.clear();
}

void AssistantService::poll() {
    const auto now = std::chrono::steady_clock::now();
    if (download_.valid() && download_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const auto done = download_.get();
        download_error_ = done.ok || done.error == "cancelled" ? std::string() : done.error;
    }
    if (progress_ && now - speed_at_ > std::chrono::seconds(1)) {
        const auto bytes = progress_->bytes.load();
        const auto secs  = std::chrono::duration<double>(now - speed_at_).count();
        if (bytes >= speed_bytes_ && secs > 0)
            speed_ = 0.7 * speed_ + 0.3 * static_cast<double>(bytes - speed_bytes_) / secs;
        speed_bytes_ = bytes;
        speed_at_    = now;
    }
    if (unload_asked_) unload();
    // Unloaded by itself after so long unused (M5, board 5).
    if (settings_.unload_minutes > 0 && last_use_.time_since_epoch().count() != 0 &&
        now - last_use_ > std::chrono::minutes(settings_.unload_minutes)) {
        std::unique_lock lock(mutex_, std::try_to_lock);
#ifdef FCG_ASSISTANT
        if (lock && process_) process_.reset();
#endif
    }
}

std::optional<commentary::Generation> AssistantService::generate(const commentary::Request& request,
                                                                 std::string* error) {
#ifdef FCG_ASSISTANT
    std::lock_guard lock(mutex_);  // one request at a time, as the model answers them
    last_use_ = std::chrono::steady_clock::now();
    if (!process_ || !process_->running()) {
        if (!installed()) {
            if (error) *error = "the assistant's model is not on this machine";
            return std::nullopt;
        }
        loading_     = true;
        auto process = std::make_unique<AssistantProcess>();
        std::string why;
        const bool ok = process->start(program_, modelPath(), settings_.gpu ? 999 : 0, why);
        loading_      = false;
        if (!ok) {
            failure_ = why;
            if (error) *error = why;
            return std::nullopt;
        }
        failure_.clear();
        devices_ = process->devices();
        process_ = std::move(process);
    }
    auto answer = process_->generate(request, error);
    last_use_   = std::chrono::steady_clock::now();
    return answer;
#else
    (void)request;
    if (error) *error = "this program was built without the assistant";
    return std::nullopt;
#endif
}

}  // namespace fcg
