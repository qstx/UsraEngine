#pragma once

#include <Rhi/RHI.hpp>
#include <filesystem>

namespace Ursa::Rendering
{
struct FrameImage
{
	UInt32 width = 0;
	UInt32 height = 0;
	Vector<UInt8> png;
};

Result<FrameImage> ReadFrameImage(const Rhi::DevicePtr& device, const Rhi::TexturePtr& texture,
                                  Rhi::ResourceState state);
Result<void> WriteFrameImage(const std::filesystem::path& output, const FrameImage& image);
} // namespace Ursa::Rendering
