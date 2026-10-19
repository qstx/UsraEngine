#include <Rendering/Diagnostics/FrameCapture.hpp>
#include <Rendering/Diagnostics/FrameImage.hpp>
#include <Core/Log.hpp>
#include <Core/Profiler.hpp>

namespace Ursa::Rendering
{
namespace
{
void Report(Result<void> result)
{
	if (!result)
	{
		URSA_LOG_WARN("Capture", result.error);
	}
}
} // namespace

Result<void> FrameCapture::Configure(std::filesystem::path selectedOutput, UInt32 frame)
{
	if (selectedOutput.empty() || selectedOutput.filename().empty() || !frame)
	{
		return Result<void>::Failure("Frame capture needs an output filename and a frame number greater than zero");
	}
	output = std::move(selectedOutput);
	requestedFrame = frame;
	renderedFrames = 0;
	saved = false;
	return {};
}

void FrameCapture::StartWindowCapture(const String& application, const std::filesystem::path& controlRoot)
{
	Report(requests.Start(application, controlRoot));
	frameSerial = 0;
	readbackCount = 0;
}

void FrameCapture::ReadyWindowCapture()
{
	Report(requests.SetReady());
}

void FrameCapture::PollWindowCapture()
{
	const auto result = requests.Poll();
	if (!result)
	{
		Report(result);
		requests.Close();
	}
}

void FrameCapture::StopWindowCapture()
{
	requests.Close();
}

Result<void> FrameCapture::Capture(const Rhi::DevicePtr& device, const Rhi::TexturePtr& texture,
                                   Rhi::ResourceState state, bool countScheduled)
{
	if (!texture)
	{
		return {};
	}
	++frameSerial;
	const bool scheduled = countScheduled && !output.empty() && !saved && ++renderedFrames == requestedFrame;
	const bool requested = requests.Pending() != nullptr;
	if (!scheduled && !requested)
	{
		return {};
	}
	URSA_PROFILE_SCOPE("Rendering.FrameCapture");
	++readbackCount;
	const auto image = ReadFrameImage(device, texture, state);
	const auto capturedAt = Diagnostics::CaptureUtcMilliseconds();
	Result<void> scheduledResult;
	if (scheduled)
	{
		scheduledResult = image ? WriteFrameImage(output, image.value) : Result<void>::Failure(image.error);
		saved = bool(scheduledResult);
	}
	if (const auto* request = requests.Pending())
	{
		if (!image)
		{
			Report(requests.Complete("CaptureFailed", image.error));
		}
		else
		{
			const auto written =
			    scheduled && saved && std::filesystem::absolute(output).lexically_normal() == request->output
			        ? Result<void>{}
			        : WriteFrameImage(request->output, image.value);
			Report(requests.Complete(written ? "Success" : "WriteFailed", written.error, image.value.width,
			                         image.value.height, frameSerial, capturedAt));
		}
	}
	return scheduledResult;
}

Result<void> FrameCapture::Finish() const
{
	return output.empty() || saved ? Result<void>{} : Result<void>::Failure("Requested capture frame was not rendered");
}
} // namespace Ursa::Rendering
