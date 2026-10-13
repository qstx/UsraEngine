#pragma once

#include <Rhi/RHI.hpp>
#include <filesystem>

namespace Ursa::Rendering
{
// Temporary diagnostic capture of the rendered client area, independent of desktop overlays.
class FrameCapture final
{
public:
	Result<void> Configure(std::filesystem::path output, UInt32 frame = 1);

	// Call after rendering has been submitted, before presenting the acquired image.
	Result<void> Capture(const Rhi::DevicePtr& device, const Rhi::TexturePtr& texture, Rhi::ResourceState state);

	Result<void> Finish() const;

private:
	std::filesystem::path output;
	UInt32 requestedFrame = 1;
	UInt32 renderedFrames = 0;
	bool saved = false;
};
} // namespace Ursa::Rendering
