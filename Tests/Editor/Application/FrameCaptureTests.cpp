#include <Rendering/Diagnostics/FrameCapture.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <stb_image.h>
#include <iostream>
#include <stdexcept>

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
