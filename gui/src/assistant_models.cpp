#include "assistant_models.hpp"

#include <system_error>

namespace fcg {

namespace {

// commentary-v11: Qwen3.5-4B fine-tuned on chess comments that hold to the facts (jromanghf/rookstudio-commentary on
// Hugging Face), quantized to Q4_K_M. Its prompts are the ones the commentary writes (commentary/brief.hpp).
constexpr AssistantModel MODELS[] = {
    {"commentary-v11", "commentary-v11-Q4_K_M.gguf",
     "https://huggingface.co/jromanghf/rookstudio-commentary/resolve/main/commentary-v11-Q4_K_M.gguf",
     "afc147563a98a1f69f991b30b417e6745564b260348c2e4960c0ad95298ad508", 2708804128, "Apache-2.0", "English, French"},
};

}  // namespace

std::span<const AssistantModel> assistantModels() { return MODELS; }

std::filesystem::path modelsFolder(const std::filesystem::path& profile) { return profile / "models"; }

std::filesystem::path modelFile(const std::filesystem::path& profile, const AssistantModel& model) {
    return modelsFolder(profile) / std::filesystem::path(std::string(model.file));
}

bool modelInstalled(const std::filesystem::path& profile, const AssistantModel& model) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(modelFile(profile, model), ec);
    return !ec && size == model.size;
}

Download downloadModel(const std::filesystem::path& profile, const AssistantModel& model, DownloadProgress* progress,
                       const std::atomic<bool>* cancel, std::string_view url) {
    return politeDownload(std::string(url.empty() ? model.url : url), modelFile(profile, model), progress, cancel,
                          model.sha256);
}

}  // namespace fcg
