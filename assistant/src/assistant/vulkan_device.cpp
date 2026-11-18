#include "vulkan_device.hpp"

#include <cstdlib>
#include <string>
#include <vector>

#ifdef FCG_ASSISTANT_VULKAN
#    include <vulkan/vulkan.h>
#endif

#ifdef _WIN32
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

namespace fcg::assistant {

namespace {

void setVariable(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

#ifdef FCG_ASSISTANT_VULKAN
// The Vulkan loader, opened for the time of the choice.
struct Loader {
#    ifdef _WIN32
    HMODULE handle = LoadLibraryA("vulkan-1.dll");
    ~Loader() {
        if (handle) FreeLibrary(handle);
    }
    PFN_vkGetInstanceProcAddr entry() const {
        return handle ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(
                            reinterpret_cast<void*>(GetProcAddress(handle, "vkGetInstanceProcAddr")))
                      : nullptr;
    }
#    else
    void* handle = open();
    static void* open() {
#        ifdef __APPLE__
        for (const char* name : {"libvulkan.1.dylib", "libMoltenVK.dylib"}) {
#        else
        for (const char* name : {"libvulkan.so.1", "libvulkan.so"}) {
#        endif
            if (void* h = dlopen(name, RTLD_NOW | RTLD_LOCAL)) return h;
        }
        return nullptr;
    }
    ~Loader() {
        if (handle) dlclose(handle);
    }
    PFN_vkGetInstanceProcAddr entry() const {
        return handle ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(handle, "vkGetInstanceProcAddr")) : nullptr;
    }
#    endif
};
#endif

}  // namespace

std::optional<unsigned> bestVulkanDevice() {
#ifdef FCG_ASSISTANT_VULKAN
    const Loader loader;
    const auto get = loader.entry();
    if (!get) return std::nullopt;
    const auto create = reinterpret_cast<PFN_vkCreateInstance>(get(nullptr, "vkCreateInstance"));
    if (!create) return std::nullopt;
    VkApplicationInfo app{};
    app.sType      = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo info{};
    info.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    VkInstance instance   = VK_NULL_HANDLE;
    if (create(&info, nullptr, &instance) != VK_SUCCESS) return std::nullopt;
    const auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(get(instance, "vkDestroyInstance"));
    const auto enumerate =
        reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get(instance, "vkEnumeratePhysicalDevices"));
    const auto properties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(get(instance, "vkGetPhysicalDeviceProperties"));
    const auto memory =
        reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(get(instance, "vkGetPhysicalDeviceMemoryProperties"));
    std::optional<unsigned> best;
    if (enumerate && properties && memory) {
        uint32_t count = 0;
        enumerate(instance, &count, nullptr);
        std::vector<VkPhysicalDevice> devices(count);
        if (count > 0 && enumerate(instance, &count, devices.data()) == VK_SUCCESS) {
            int best_rank          = 0;
            VkDeviceSize best_size = 0;
            for (uint32_t i = 0; i < count; ++i) {
                VkPhysicalDeviceProperties p{};
                properties(devices[i], &p);
                const int rank = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 2
                                 : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 1
                                                                                          : 0;
                if (rank == 0) continue;
                VkPhysicalDeviceMemoryProperties m{};
                memory(devices[i], &m);
                VkDeviceSize own = 0;
                for (uint32_t h = 0; h < m.memoryHeapCount; ++h) {
                    if (m.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) own += m.memoryHeaps[h].size;
                }
                if (rank > best_rank || (rank == best_rank && own > best_size)) {
                    best      = i;
                    best_rank = rank;
                    best_size = own;
                }
            }
        }
    }
    if (destroy) destroy(instance, nullptr);
    return best;
#else
    return std::nullopt;
#endif
}

void chooseVulkanDevice(bool gpu) {
    if (std::getenv("GGML_VK_VISIBLE_DEVICES")) return;
    const auto best = gpu ? bestVulkanDevice() : std::nullopt;
    if (best) {
        setVariable("GGML_VK_VISIBLE_DEVICES", std::to_string(*best));
        return;
    }
#ifndef _WIN32
    // An empty list shows ggml no device (Windows has no empty variable: ggml's own choice stands there).
    setVariable("GGML_VK_VISIBLE_DEVICES", std::string());
#endif
}

}  // namespace fcg::assistant
