// The assistant as the window uses it (assistant_service.hpp): its settings, its model's file, the model loaded at the
// first request and unloaded, on the scripted model (gui/tests/mock/assistant.cpp).

#ifdef FCG_ASSISTANT

#    include <doctest/doctest.hpp>

#    include <atomic>
#    include <chrono>
#    include <filesystem>
#    include <fstream>
#    include <memory>
#    include <string>
#    include <thread>

#    include "assistant_service.hpp"
#    include "mock_engines.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

fs::path freshProfile(const char* name) {
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// A GGUF file of the disk in place of the catalogue's model.
void useModel(AssistantService& service, const fs::path& file) {
    auto settings       = service.settings();
    settings.model_file = file.string();
    service.setSettings(settings);
}

commentary::Request asked(std::string text) {
    commentary::Request r;
    r.messages   = {{commentary::Speaker::User, std::move(text)}};
    r.max_tokens = 50;
    return r;
}

}  // namespace

TEST_CASE("assistant service: no model, no answer; the model chosen loads at the first request and unloads") {
    const auto profile = freshProfile("assistant_service");
    AssistantService service(profile, test::mockAssistant());
    CHECK_FALSE(service.installed());
    CHECK(service.state() == AssistantService::State::NoModel);
    std::string error;
    CHECK_FALSE(service.generate(asked("hello"), &error));
    CHECK(error.find("not on this machine") != std::string::npos);

    // A GGUF file of the disk in place of the catalogue's model.
    const auto file = profile / "commentary.gguf";
    std::ofstream(file) << "model";
    useModel(service, file);
    CHECK(service.installed());
    CHECK(service.state() == AssistantService::State::Unloaded);
    const auto answer = service.generate(asked("hello"), &error);
    REQUIRE_MESSAGE(answer, error);
    CHECK(answer->text.find("hello") != std::string::npos);
    CHECK(service.state() == AssistantService::State::Ready);
    CHECK_FALSE(service.devices().empty());
    service.unload();
    CHECK(service.state() == AssistantService::State::Unloaded);
    // Loaded again by the next request.
    REQUIRE(service.generate(asked("again"), &error));
    CHECK(service.state() == AssistantService::State::Ready);
}

TEST_CASE("assistant service: a request cancelled is refused before the model loads; a task's model stops asking") {
    const auto profile = freshProfile("assistant_cancel");
    AssistantService service(profile, test::mockAssistant());
    const auto file = profile / "commentary.gguf";
    std::ofstream(file) << "model";
    useModel(service, file);
    // Cancelled by the time its turn came (#98): nothing loaded for it.
    std::atomic<bool> cancelled{true};
    std::string error;
    CHECK_FALSE(service.generate(asked("hello"), &error, &cancelled));
    CHECK(error == "cancelled");
    CHECK(service.state() == AssistantService::State::Unloaded);
    // A question's model: it answers until its task is left, then refuses.
    auto left = std::make_shared<std::atomic<bool>>(false);
    CancellableModel model(service, left);
    const auto answer = model.generate(asked("hello"), &error);
    REQUIRE_MESSAGE(answer, error);
    CHECK(answer->text.find("hello") != std::string::npos);
    *left = true;
    error.clear();
    CHECK_FALSE(model.generate(asked("again"), &error));
    CHECK(error == "cancelled");
    service.unload();
}

TEST_CASE("assistant service: its settings are kept in the profile") {
    const auto profile = freshProfile("assistant_settings");
    {
        AssistantService service(profile, test::mockAssistant());
        CHECK(service.settings().unload_minutes == 15);
        CHECK(service.settings().gpu);
        auto settings           = service.settings();
        settings.model_file     = "/models/other.gguf";
        settings.unload_minutes = 0;
        settings.not_now        = true;
        service.setSettings(settings);
        CHECK(service.settings().model_file == "/models/other.gguf");
        service.saveSettings();
    }
    AssistantService again(profile, test::mockAssistant());
    CHECK(again.settings().model_file == "/models/other.gguf");
    CHECK(again.settings().unload_minutes == 0);
    CHECK(again.settings().not_now);
    CHECK(again.modelPath() == fs::path("/models/other.gguf"));
}

TEST_CASE("assistant service: settings changed while requests are answered; each request takes them as it starts") {
    const auto profile = freshProfile("assistant_settings_race");
    AssistantService service(profile, test::mockAssistant());
    const auto file = profile / "commentary.gguf", other = profile / "other.gguf";
    std::ofstream(file) << "model";
    std::ofstream(other) << "model";
    useModel(service, file);
    // The window's thread changes the settings while another asks (#100): the requests copy them when their turn
    // comes, the page writes them through setSettings() only. Run under ThreadSanitizer, no race.
    std::atomic<bool> stop{false};
    std::atomic<int> answered{0};
    std::thread asking([&] {
        std::string error;
        while (!stop)
            if (service.generate(asked("hello"), &error)) ++answered;
    });
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    for (int i = 0; (i < 200 || answered < 3) && std::chrono::steady_clock::now() < give_up; ++i) {
        auto changed           = service.settings();
        changed.unload_minutes = 15 + i % 2;
        changed.not_now        = i % 2 == 0;
        changed.model_file     = (i % 2 == 0 ? other : file).string();
        service.setSettings(changed);
        (void)service.installed();
        (void)service.modelPath();
        service.poll();
        std::this_thread::yield();
    }
    stop = true;
    asking.join();
    CHECK(answered >= 3);
    // A change the next request takes: on the processor now, the model loaded again there (the scripted model says
    // where it answers).
    std::string error;
    const auto before = service.generate(asked("where"), &error);
    REQUIRE_MESSAGE(before, error);
    CHECK(before->text.find("on the GPU") != std::string::npos);
    auto settings = service.settings();
    settings.gpu  = false;
    service.setSettings(settings);
    const auto after = service.generate(asked("where"), &error);
    REQUIRE_MESSAGE(after, error);
    CHECK(after->text.find("on the CPU") != std::string::npos);
    service.unload();
}

TEST_CASE("assistant service: a model that does not load says why, and the next request tries again") {
    const auto profile = freshProfile("assistant_missing");
    AssistantService service(profile, test::mockAssistant());
    // The scripted model refuses a file named "missing".
    const auto file = profile / "missing";
    std::ofstream(file) << "model";
    useModel(service, file);
    std::string error;
    CHECK_FALSE(service.generate(asked("hello"), &error));
    CHECK_FALSE(error.empty());
    CHECK(service.state() == AssistantService::State::Failed);
    CHECK_FALSE(service.failure().empty());
}

#endif
