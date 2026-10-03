#pragma once

#include <vulkan/vulkan_core.h>

namespace ursa::rhi::vulkan_detail {
void reportValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, const char *message) noexcept;
} // namespace ursa::rhi::vulkan_detail
