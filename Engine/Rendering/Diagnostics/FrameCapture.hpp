#pragma once

#include <Rhi/RHI.hpp>
#include <Diagnostics/CaptureChannel.hpp>
#include <filesystem>

namespace Ursa::Rendering
{
// Diagnostic capture of completed client-area images, independent of desktop overlays.
class FrameCapture final
{
public:
	Result<void> Configure(std::filesystem::path output, UInt32 frame = 1);
	void StartWindowCapture(const String& application, const std::filesystem::path& controlRoot = {});
	void ReadyWindowCapture();
	void PollWindowCapture();
	void StopWindowCapture();

	UInt64 ReadbackCount() const
	{
		return readbackCount;
	}

	// Call after rendering has been submitted, before presenting the acquired image.
	Result<void> Capture(const Rhi::DevicePtr& device, const Rhi::TexturePtr& texture, Rhi::ResourceState state,
	                     bool countScheduled = true);

	Result<void> Finish() const;

private:
	std::filesystem::path output;
	UInt32 requestedFrame = 1;
	UInt32 renderedFrames = 0;
	bool saved = false;
	Diagnostics::CaptureServer requests;
	UInt64 frameSerial = 0;
	UInt64 readbackCount = 0;
};

class WindowCaptureScope final
{
public:
	WindowCaptureScope(FrameCapture& selectedCapture, const String& application) : capture(selectedCapture)
	{
		capture.StartWindowCapture(application);
	}

	~WindowCaptureScope()
	{
		capture.StopWindowCapture();
	}

	WindowCaptureScope(const WindowCaptureScope&) = delete;
	WindowCaptureScope& operator=(const WindowCaptureScope&) = delete;

private:
	FrameCapture& capture;
};
} // namespace Ursa::Rendering
