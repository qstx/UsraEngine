#include <rhi/vulkan/ValidationLog.hpp>
#include <core/Log.hpp>

namespace ursa::rhi::vulkan_detail {
void reportValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, const char *message) noexcept {
    if (!message)
        return;
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        URSA_LOG_ERROR("RHI", String("Vulkan validation: ") + message);
    else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        URSA_LOG_WARN("RHI", String("Vulkan validation: ") + message);
}
} // namespace ursa::rhi::vulkan_detail
