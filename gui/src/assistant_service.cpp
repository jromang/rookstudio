#include "assistant_service.hpp"

#include "storage.hpp"

#ifdef FCG_ASSISTANT
#    include "assistant_process.hpp"
#else
namespace fcg {
// Built without the assistant's runtime: no model ever loads.
class AssistantProcess {
   public:
    bool start(const std::string&, const std::filesystem::path&, int, std::string& error) {
        error = "this program was built without the assistant";
        return false;
    }
    std::optional<commentary::Generation> generate(const commentary::Request&, std::string* error) {
        if (error) *error = "this program was built without the assistant";
        return std::nullopt;
    }
    bool running() { return false; }
    [[nodiscard]] bool gpuFailed() const noexcept { return false; }
    [[nodiscard]] const std::string& devices() const noexcept { return devices_; }

   private:
    std::string devices_;
};
}  // namespace fcg
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

bool AssistantService::knowsComments() const {
    if (settings_.model_file.empty()) return true;
    const auto name = std::filesystem::path(settings_.model_file).filename().string();
    for (const auto& m : assistantModels())
        if (name == m.file) return true;
    return false;
}

bool AssistantService::partlyDownloaded() const {
    std::error_code ec;
    auto part = modelFile(profile_, recommended());
    part += ".part";
    return std::filesystem::is_regular_file(part, ec);
}

bool AssistantService::gpuFailed() const { return gpu_failed_; }

void AssistantService::retryGpu() {
    gpu_failed_ = false;
    unload();
}

void AssistantService::setGpuTaken(const std::string& tournament) {
    if (tournament.empty()) gpu_anyway_ = false;  // asked for that tournament only
    gpu_taken_ = tournament;
    gpu_busy_  = !tournament.empty() && !gpu_anyway_;
}

void AssistantService::takeGpuAnyway() {
    gpu_anyway_ = true;
    gpu_busy_   = false;
    unload();  // loaded again on the GPU at the next request
}

bool AssistantService::onGpu() const { return loaded_on_gpu_; }

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
    if (process_ && process_->running()) return State::Ready;
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
    loaded_on_gpu_ = false;
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
    // Unloaded by itself after so long unused (M5, board 5); a request under way holds the lock, and the model.
    if (settings_.unload_minutes > 0) {
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (lock && process_ && now - last_use_ > std::chrono::minutes(settings_.unload_minutes)) {
            process_.reset();
            loaded_on_gpu_ = false;
        }
    }
}

std::optional<commentary::Generation> AssistantService::generate(const commentary::Request& request,
                                                                 std::string* error) {
    return generate(request, error, nullptr);
}

std::optional<commentary::Generation> AssistantService::generate(const commentary::Request& request, std::string* error,
                                                                 const std::atomic<bool>* cancel) {
    if (cancel && *cancel) {  // not even its turn waited for
        if (error) *error = "cancelled";
        return std::nullopt;
    }
    std::lock_guard lock(mutex_);  // one request at a time, as the model answers them
    if (cancel && *cancel) {       // cancelled while the request before it was answered
        if (error) *error = "cancelled";
        return std::nullopt;
    }
    last_use_ = std::chrono::steady_clock::now();
    // On the GPU when it is wanted and free; a model loaded where it no longer should be is loaded again.
    const bool want_gpu = settings_.gpu && !gpu_busy_ && !gpu_failed_;
    if (process_ && process_->running() && loaded_on_gpu_ && !want_gpu) process_.reset();
    if (!process_ || !process_->running()) {
        if (!installed()) {
            if (error) *error = "the assistant's model is not on this machine";
            return std::nullopt;
        }
        loading_     = true;
        auto process = std::make_unique<AssistantProcess>();
        std::string why;
        const bool ok = process->start(program_, modelPath(), want_gpu ? 999 : 0, why);
        loading_      = false;
        if (!ok) {
            failure_ = why;
            if (error) *error = why;
            return std::nullopt;
        }
        failure_.clear();
        devices_    = process->devices();
        gpu_failed_ = process->gpuFailed();
        // On the GPU only when its process found one to use: a program built without a GPU backend, or a machine
        // without a GPU, runs it on the processor whatever was asked.
        loaded_on_gpu_ = want_gpu && !process->gpuFailed() &&
                         (devices_.find("GPU") != std::string::npos || devices_.find("Vulkan") != std::string::npos);
        process_       = std::move(process);
    }
    auto answer = process_->generate(request, error);
    // A GPU that failed while it answered: the model runs on the processor since (AssistantProcess).
    if (process_->gpuFailed() && loaded_on_gpu_) {
        gpu_failed_    = true;
        loaded_on_gpu_ = false;
        devices_       = process_->devices();
    }
    last_use_ = std::chrono::steady_clock::now();
    return answer;
}

}  // namespace fcg
