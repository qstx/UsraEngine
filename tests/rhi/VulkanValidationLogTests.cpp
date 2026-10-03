#include <core/Log.hpp>
#include <rhi/vulkan/ValidationLog.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main() {
    ursa::rhi::vulkan_detail::reportValidationMessage(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
                                                       "fallback before initialization");
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("UrsaVulkanValidationLogTests-" + std::to_string(unique));
    const auto path = root / "validation.log";
    ursa::LogConfig config;
    config.filePath = path;
    auto initialized = ursa::Log::initialize(root / "test.exe", config);
    if (!initialized) {
        std::cerr << initialized.error.c_str() << '\n';
        return 1;
    }
    ursa::rhi::vulkan_detail::reportValidationMessage(VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT,
                                                       "ignored info");
    ursa::rhi::vulkan_detail::reportValidationMessage(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
                                                       "synthetic warning");
    ursa::rhi::vulkan_detail::reportValidationMessage(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                                                       "synthetic error");
    ursa::Log::shutdown();
    std::ifstream input(path, std::ios::binary);
    const std::string contents(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>());
    ursa::rhi::vulkan_detail::reportValidationMessage(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                                                       "fallback after shutdown");
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    if (contents.find("[warning] [RHI] Vulkan validation: synthetic warning") == std::string::npos ||
        contents.find("[error] [RHI] Vulkan validation: synthetic error") == std::string::npos ||
        contents.find("ignored info") != std::string::npos) {
        std::cerr << "Vulkan validation messages used incorrect log levels\n";
        return 1;
    }
    return 0;
}
