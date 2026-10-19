#include <Rendering/Diagnostics/FrameCapture.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <stb_image.h>
#include <Core/StringInterop.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
using namespace Ursa;

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

Array<UInt8, 4> DecodeFirstPixel(const std::filesystem::path& path, UInt32 width, UInt32 height)
{
	VirtualFileSystem files;
	Done(files.Mount("image", path.parent_path(), MountAccess::ReadOnly));
	const auto bytes = Take(files.Read(Take(VirtualPath::Parse("image:" + PathToUrsaString(path.filename())))));
	int decodedWidth = 0;
	int decodedHeight = 0;
	int channels = 0;
	auto* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &decodedWidth, &decodedHeight,
	                                     &channels, 4);
	Expect(pixels && decodedWidth == static_cast<int>(width) && decodedHeight == static_cast<int>(height),
	       "On-demand PNG dimensions or decoding failed");
	Array<UInt8, 4> first{pixels[0], pixels[1], pixels[2], pixels[3]};
	stbi_image_free(pixels);
	return first;
}

Diagnostics::CaptureResult CompleteRequest(Rendering::FrameCapture& capture, Diagnostics::CaptureClient& client,
                                           const Rhi::DevicePtr& device, const Rhi::TexturePtr& color)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	while (!client.Poll())
	{
		Expect(std::chrono::steady_clock::now() < deadline, "On-demand GPU capture did not complete");
		capture.PollWindowCapture();
		Done(capture.Capture(device, color, Rhi::ResourceState::ColorAttachment));
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	const auto result = client.Wait();
	std::cout << Diagnostics::SerializeCaptureResult(result).c_str() << '\n';
	return result;
}

void TestOnDemand(const Rhi::DevicePtr& device, const Rhi::TexturePtr& color, const std::filesystem::path& directory)
{
	using namespace Diagnostics;
	const auto root = directory / ("Demand-" + ToStdString(NewCaptureId()));
	Rendering::FrameCapture capture;
	capture.StartWindowCapture("GpuTest", root / "Control");
	capture.ReadyWindowCapture();
	Done(capture.Capture({}, color, Rhi::ResourceState::ColorAttachment));
	Expect(capture.ReadbackCount() == 0, "Idle capture performed GPU work");
	Done(capture.Configure(root / "Scheduled.png", 1));
	CaptureClient first(CaptureProcessId(), root / std::filesystem::u8path(u8"第一次 截图.png"), 2000,
	                    root / "Control");
	capture.PollWindowCapture();
	Done(capture.Capture(device, color, Rhi::ResourceState::ColorAttachment));
	const auto initial = first.Wait();
	Expect(initial.Succeeded() && capture.ReadbackCount() == 1 && bool(capture.Finish()),
	       "Scheduled and on-demand requests did not share one readback");
	const auto firstPixel = DecodeFirstPixel(initial.request.output, initial.width, initial.height);
	Expect(firstPixel[0] == 255 && firstPixel[3] == 255, "Initial on-demand pixels or alpha are incorrect");
	// Change actual GPU content, then request another completed image.
	auto command = Take(device->CreateCommandList());
	Done(command->Begin());
	Rhi::RenderTarget target;
	target.color = color;
	target.colorValue[0] = 0;
	target.colorValue[1] = 0;
	target.colorValue[2] = 1;
	target.colorValue[3] = 0;
	Done(command->BeginRendering(target));
	Done(command->EndRendering());
	Done(command->End());
	Done(Take(device->Submit(command))->Wait());
	CaptureClient second(CaptureProcessId(), root / "Second.png", 2000, root / "Control");
	const auto repeated = CompleteRequest(capture, second, device, color);
	Expect(repeated.Succeeded() && repeated.frameSerial > initial.frameSerial &&
	           repeated.request.requestId != initial.request.requestId,
	       "Repeated request did not capture a new frame");
	const auto secondPixel = DecodeFirstPixel(repeated.request.output, repeated.width, repeated.height);
	Expect(secondPixel[0] == 0 && secondPixel[2] == 255 && secondPixel[3] == 255, "Repeated capture reused old pixels");
	VirtualFileSystem output;
	Done(output.Mount("output", root, MountAccess::ReadWrite));
	const auto secondPath = Take(VirtualPath::Parse("output:Second.png"));
	const auto original = Take(output.Read(secondPath));
#ifdef _WIN32
	// Hold the destination open without sharing to force failure at atomic replacement.
	auto lease = Take(output.AcquireExclusiveLease(secondPath));
	CaptureClient blocked(CaptureProcessId(), root / "Second.png", 2000, root / "Control");
	Expect(CompleteRequest(capture, blocked, device, color).status == "WriteFailed",
	       "Locked output did not fail independently");
	lease.reset();
	Expect(Take(output.Read(secondPath)) == original, "Failed atomic output corrupted the existing PNG");
#else
	std::ofstream(root / "Blocked") << "not a directory";
	CaptureClient blocked(CaptureProcessId(), root / "Blocked/No.png", 2000, root / "Control");
	Expect(CompleteRequest(capture, blocked, device, color).status == "WriteFailed",
	       "Invalid output did not fail independently");
	Expect(Take(output.Read(secondPath)) == original, "Failed output altered an existing PNG");
#endif
	CaptureClient recovery(CaptureProcessId(), root / "Recovered.png", 2000, root / "Control");
	Expect(CompleteRequest(capture, recovery, device, color).Succeeded(),
	       "Capture did not recover after output failure");
	const auto count = capture.ReadbackCount();
	CaptureClient expired(CaptureProcessId(), root / "Expired.png", 20, root / "Control");
	Expect(expired.Wait().status == "Timeout", "Undrawn request did not time out");
	capture.PollWindowCapture();
	Done(capture.Capture(device, color, Rhi::ResourceState::ColorAttachment, false));
	Expect(capture.ReadbackCount() == count && bool(capture.Finish()),
	       "Expired request read back pixels or reset the scheduled result");
	capture.StopWindowCapture();
	std::cout << "On-demand GPU evidence: " << root << '\n';
}
} // namespace

int main()
{
	try
	{
		Rhi::DeviceDesc description;
		description.validation = true;
		auto device = Take(Rhi::CreateDevice(description));
		const auto directory = std::filesystem::current_path() / "FrameCaptureTests";
		for (const auto format : {Rhi::Format::RgbA8Unorm, Rhi::Format::BgrA8Unorm})
		{
			// More than one DEFLATE block, with an asymmetric color to detect swapped channels.
			auto color = Take(device->CreateTexture(
			    {257, 65, format, Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource}));
			auto command = Take(device->CreateCommandList());
			Done(command->Begin());
			Done(command->Barrier(color, Rhi::ResourceState::Undefined, Rhi::ResourceState::ColorAttachment));
			Rhi::RenderTarget target;
			target.color = color;
			target.colorValue[0] = 1;
			target.colorValue[1] = 0.25f;
			target.colorValue[2] = 0.5f;
			target.colorValue[3] = 0;
			Done(command->BeginRendering(target));
			Done(command->EndRendering());
			Done(command->End());
			auto submitted = Take(device->Submit(command));
			Rendering::FrameCapture capture;
			Expect(!capture.Configure(directory / "Invalid.png", 0), "Accepted frame zero");
			const char* filename = format == Rhi::Format::RgbA8Unorm ? "RGBA.png" : "BGRA.png";
			Done(capture.Configure(directory / filename, 2));
			Done(capture.Capture(device, color, Rhi::ResourceState::ColorAttachment));
			Expect(!capture.Finish(), "Reported an unsaved capture as complete");
			Done(capture.Capture(device, color, Rhi::ResourceState::ColorAttachment));
			Done(capture.Finish());
			Done(capture.Capture(device, color, Rhi::ResourceState::ColorAttachment));
			Done(submitted->Wait());
			VirtualFileSystem files;
			Done(files.Mount("capture", directory, MountAccess::ReadOnly));
			const auto bytes = Take(files.Read(Take(VirtualPath::Parse(String("capture:") + filename))));
			int width = 0;
			int height = 0;
			int channels = 0;
			auto* pixels =
			    stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
			Expect(pixels != nullptr, "An independent PNG decoder rejected the capture");
			bool correct = width == 257 && height == 65 && channels == 4;
			for (int index = 0; index < width * height; ++index)
			{
				correct &= pixels[index * 4] == 255 && pixels[index * 4 + 1] >= 63 && pixels[index * 4 + 1] <= 64 &&
				           pixels[index * 4 + 2] >= 127 && pixels[index * 4 + 2] <= 128 && pixels[index * 4 + 3] == 255;
			}
			stbi_image_free(pixels);
			Expect(correct, "Capture dimensions, channel order, pixels or opacity are incorrect");
			TestOnDemand(device, color, directory);
		}
		Done(device->WaitIdle());
		std::cout << "Frame capture GPU and PNG decoding tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
