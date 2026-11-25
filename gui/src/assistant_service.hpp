#pragma once

// The assistant as the window uses it (mockup M5): its model's file (the catalogue's, downloaded once, or a GGUF file
// chosen), the download, and the model loaded in its process at the first request and unloaded by Unload, or by itself
// after a quarter of an hour unused. It is the commentary::Model the questions and the comments are written with: a
// request loads the model when it is not loaded, on the thread that asks (never the window's).

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <commentary/commentator.hpp>
#include <json.hpp>

#include "assistant_models.hpp"
#include "https.hpp"

namespace fcg {

class AssistantProcess;

struct AssistantSettings {
    std::string model_file;      // a GGUF file chosen instead of the catalogue's model; empty: the catalogue's
    bool gpu           = true;   // on the GPU when the program has one to use
    int unload_minutes = 15;     // unloaded after so long unused; 0: kept until Unload
    bool not_now       = false;  // "Not now" said to the first use: offered again where it is asked, quietly
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AssistantSettings, model_file, gpu, unload_minutes, not_now)

class AssistantService final : public commentary::Model {
   public:
    AssistantService(std::filesystem::path profile, std::string program);
    ~AssistantService() override;
    AssistantService(const AssistantService&)            = delete;
    AssistantService& operator=(const AssistantService&) = delete;

    // Read on the window's thread, and copied by a request when its turn comes (generate(), on the thread that asks):
    // a copy, changed with setSettings() only, under a lock of their own (#100). saveSettings() writes them.
    [[nodiscard]] AssistantSettings settings() const;
    void setSettings(const AssistantSettings& settings);
    void saveSettings();

    // The model: the catalogue's recommended one, unless a file was chosen.
    [[nodiscard]] const AssistantModel& recommended() const;
    [[nodiscard]] std::filesystem::path modelPath() const;
    [[nodiscard]] bool installed() const;  // its file is there, whole
    // The model is one of the catalogue's, which learnt RookStudio's comments; another GGUF file answers the
    // questions, the comments staying the templates' (M5, board 5).
    [[nodiscard]] bool knowsComments() const;
    // Part of the recommended model is there, from a download that stopped: the next one resumes it.
    [[nodiscard]] bool partlyDownloaded() const;

    // The recommended model downloaded into the profile, resumed where an earlier try stopped, checked at its end.
    void download();
    void cancelDownload();
    [[nodiscard]] bool downloading() const;
    [[nodiscard]] uint64_t downloaded() const;        // bytes so far
    [[nodiscard]] double downloadSpeed() const;       // bytes a second, lately
    [[nodiscard]] std::string downloadError() const;  // why the last download stopped; empty after a success

    enum class State { NoModel, Downloading, Unloaded, Loading, Ready, Failed };
    [[nodiscard]] State state() const;
    [[nodiscard]] std::string devices() const;  // where the model runs, as its process named it
    [[nodiscard]] std::string failure() const;  // why it could not load
    void unload();                              // its process ended: its memory, and the GPU's, freed
    // The GPU failed the model (its driver fell): it runs on the processor since. Trying again unloads it, and the
    // next request loads it on the GPU.
    [[nodiscard]] bool gpuFailed() const;
    void retryGpu();
    // A tournament whose engines use the GPU is running (its name; empty when none): the model loads on the processor
    // meanwhile, unless the user takes the GPU anyway (M5, board 5).
    void setGpuTaken(const std::string& tournament);
    [[nodiscard]] const std::string& gpuTaken() const noexcept { return gpu_taken_; }
    void takeGpuAnyway();
    [[nodiscard]] bool onGpu() const;  // loaded with its layers on the GPU
    // Once a frame: a download that ended, the model unloaded after unload_minutes unused.
    void poll();

    // commentary::Model: the model's answer, loading it first when it is not loaded (on the caller's thread).
    [[nodiscard]] std::optional<commentary::Generation> generate(const commentary::Request& request,
                                                                 std::string* error) override;
    // The same, refused ("cancelled") when `cancel` is set by the time the request's turn comes, before the model is
    // loaded: a request waits for the one before it (#98).
    [[nodiscard]] std::optional<commentary::Generation> generate(const commentary::Request& request, std::string* error,
                                                                 const std::atomic<bool>* cancel);

   private:
    // The model's file and whether it is there, for these settings.
    [[nodiscard]] std::filesystem::path modelPathFor(const AssistantSettings& settings) const;
    [[nodiscard]] bool installedFor(const AssistantSettings& settings) const;

    std::filesystem::path profile_;
    std::string program_;
    AssistantSettings settings_;  // under settings_mutex_
    mutable std::mutex settings_mutex_;
    mutable std::mutex mutex_;  // a request's: one at a time
    std::unique_ptr<AssistantProcess> process_;
    std::atomic<bool> loading_{false};
    std::atomic<bool> unload_asked_{false};  // Unload while a request held the model: done once it is answered
    std::string gpu_taken_;                  // the window's thread's
    std::atomic<bool> gpu_busy_{false};      // gpu_taken_ not empty, and the GPU not taken anyway
    std::atomic<bool> gpu_anyway_{false};
    std::atomic<bool> loaded_on_gpu_{false};
    std::atomic<bool> gpu_failed_{false};
    std::string failure_, devices_;
    std::chrono::steady_clock::time_point last_use_{};
    std::future<Download> download_;
    std::shared_ptr<DownloadProgress> progress_;
    std::shared_ptr<std::atomic<bool>> cancel_;
    std::string download_error_;
    std::chrono::steady_clock::time_point speed_at_{};
    uint64_t speed_bytes_ = 0;
    double speed_         = 0;
};

// The assistant as the model of one task of the window's (a question, a comment): its requests refused once `cancel`
// is set, the one waiting for its turn too, so that a task left stops at its next request instead of its last (#98).
class CancellableModel final : public commentary::Model {
   public:
    CancellableModel(AssistantService& service, std::shared_ptr<const std::atomic<bool>> cancel)
        : service_(service), cancel_(std::move(cancel)) {}
    [[nodiscard]] std::optional<commentary::Generation> generate(const commentary::Request& request,
                                                                 std::string* error) override {
        return service_.generate(request, error, cancel_.get());
    }

   private:
    AssistantService& service_;
    std::shared_ptr<const std::atomic<bool>> cancel_;
};

}  // namespace fcg
