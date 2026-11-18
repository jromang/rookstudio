#pragma once

// The Vulkan device the assistant's model goes to, chosen before ggml starts, which then initializes that device alone
// (GGML_VK_VISIBLE_DEVICES). With two devices initialized, this ggml crashes when the model is given one of them (a
// discrete NVIDIA GPU beside a built-in AMD one), and a model split across them runs slower than on the discrete GPU
// alone. Without GPU layers, ggml is shown no device (but on Windows): a failing driver cannot take the CPU's
// work down.

#include <optional>

namespace fcg::assistant {

// The best Vulkan device, by its index among the instance's physical devices: a discrete GPU before one built in, the
// one with the most memory of its own. None without a Vulkan loader or a GPU.
[[nodiscard]] std::optional<unsigned> bestVulkanDevice();

// Shows ggml the best device alone when `gpu` is true, and none when it is false; a choice the user made already
// (GGML_VK_VISIBLE_DEVICES set) is kept. Before ggml loads its backends.
void chooseVulkanDevice(bool gpu);

}  // namespace fcg::assistant
