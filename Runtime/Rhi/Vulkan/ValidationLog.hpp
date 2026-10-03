#pragma once

#include <Vulkan/vulkan_core.h>

namespace Ursa::Rhi::VulkanDetail
{
void ReportValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, const char* message) noexcept;
} // namespace Ursa::Rhi::VulkanDetail
