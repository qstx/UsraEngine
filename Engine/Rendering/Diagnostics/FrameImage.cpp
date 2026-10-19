#include <Rendering/Diagnostics/FrameImage.hpp>
#include <Core/Log.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <stdexcept>
#include <algorithm>

namespace Ursa::Rendering
{
namespace
{
template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

void AppendBigEndian(Vector<UInt8>& bytes, UInt32 value)
{
	for (Int32 shift = 24; shift >= 0; shift -= 8)
	{
		bytes.push_back(static_cast<UInt8>(value >> shift));
	}
}

void AppendChunk(Vector<UInt8>& bytes, const char* type, const Vector<UInt8>& data)
{
	AppendBigEndian(bytes, static_cast<UInt32>(data.size()));
	const auto start = bytes.size();
	bytes.insert(bytes.end(), type, type + 4);
	bytes.insert(bytes.end(), data.begin(), data.end());
	UInt32 crc = 0xffffffffu;
	for (Size index = start; index < bytes.size(); ++index)
	{
		crc ^= bytes[index];
		for (UInt32 bit = 0; bit < 8; ++bit)
		{
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
		}
	}
	AppendBigEndian(bytes, ~crc);
}

// PNG with uncompressed DEFLATE blocks: diagnostic output needs no additional codec dependency.
Vector<UInt8> EncodePng(UInt32 width, UInt32 height, const Vector<UInt8>& pixels)
{
	Vector<UInt8> rows;
	const Size stride = static_cast<Size>(width) * 4;
	rows.reserve((stride + 1) * height);
	for (UInt32 y = 0; y < height; ++y)
	{
		rows.push_back(0);
		rows.insert(rows.end(), pixels.begin() + y * stride, pixels.begin() + (y + 1) * stride);
	}
	Vector<UInt8> compressed{0x78, 0x01};
	for (Size offset = 0; offset < rows.size();)
	{
		const auto count = static_cast<UInt16>(std::min<Size>(65535, rows.size() - offset));
		const UInt16 inverse = static_cast<UInt16>(~count);
		compressed.push_back(offset + count == rows.size() ? 1 : 0);
		compressed.push_back(static_cast<UInt8>(count));
		compressed.push_back(static_cast<UInt8>(count >> 8));
		compressed.push_back(static_cast<UInt8>(inverse));
		compressed.push_back(static_cast<UInt8>(inverse >> 8));
		compressed.insert(compressed.end(), rows.begin() + offset, rows.begin() + offset + count);
		offset += count;
	}
	UInt32 a = 1;
	UInt32 b = 0;
	for (const auto value : rows)
	{
		a = (a + value) % 65521;
		b = (b + a) % 65521;
	}
	AppendBigEndian(compressed, (b << 16) | a);
	Vector<UInt8> png{137, 80, 78, 71, 13, 10, 26, 10};
	Vector<UInt8> header;
	AppendBigEndian(header, width);
	AppendBigEndian(header, height);
	header.insert(header.end(), {8, 6, 0, 0, 0});
	AppendChunk(png, "IHDR", header);
	AppendChunk(png, "IDAT", compressed);
	AppendChunk(png, "IEND", {});
	return png;
}
} // namespace

Result<FrameImage> ReadFrameImage(const Rhi::DevicePtr& device, const Rhi::TexturePtr& texture,
                                  Rhi::ResourceState state)
{
	try
	{
		if (!device || !texture || !Rhi::Has(texture->desc.usage, Rhi::TextureUsage::TransferSource) ||
		    (texture->desc.format != Rhi::Format::RgbA8Unorm && texture->desc.format != Rhi::Format::RgbA8Srgb &&
		     texture->desc.format != Rhi::Format::BgrA8Unorm))
		{
			throw std::runtime_error("Frame capture requires a readable RGBA8/BGRA8 render target");
		}
		const UInt64 size = static_cast<UInt64>(texture->desc.width) * texture->desc.height * 4;
		if (!size || size > 256 * 1024 * 1024)
		{
			throw std::runtime_error("Frame capture exceeds the 256 MiB readback limit");
		}
		auto readback = Take(device->CreateBuffer(
		    {static_cast<Size>(size), Rhi::BufferUsage::TransferDestination, Rhi::Memory::Readback}));
		auto command = Take(device->CreateCommandList());
		Done(command->Begin());
		Done(command->Barrier(texture, state, Rhi::ResourceState::TransferSource));
		Done(command->Barrier(readback, Rhi::ResourceState::Undefined, Rhi::ResourceState::TransferDestination));
		Done(command->CopyTextureToBuffer(texture, readback));
		Done(command->Barrier(texture, Rhi::ResourceState::TransferSource, state));
		Done(command->End());
		Done(Take(device->Submit(command))->Wait());
		Vector<UInt8> pixels(static_cast<Size>(size));
		Done(readback->Read(0, pixels.data(), pixels.size()));
		for (Size offset = 0; offset < pixels.size(); offset += 4)
		{
			if (texture->desc.format == Rhi::Format::BgrA8Unorm)
			{
				std::swap(pixels[offset], pixels[offset + 2]);
			}
			pixels[offset + 3] = 255;
		}

		FrameImage image;
		image.width = texture->desc.width;
		image.height = texture->desc.height;
		image.png = EncodePng(image.width, image.height, pixels);
		return Result<FrameImage>::Success(std::move(image));
	}
	catch (const std::exception& error)
	{
		return Result<FrameImage>::Failure("Frame capture failed: " + ToUrsaString(error.what()));
	}
}

Result<void> WriteFrameImage(const std::filesystem::path& output, const FrameImage& image)
{
	try
	{
		const auto absolute = std::filesystem::absolute(output);
		VirtualFileSystem files;
		Done(files.MountProvider("capture", Take(DirectoryFileProvider::Open(absolute.parent_path(), true)),
		                         MountAccess::ReadWrite));
		const auto path = Take(VirtualPath::Parse("capture:" + ToUrsaString(absolute.filename().u8string())));
		Done(files.WriteAtomic(path, image.png));
		URSA_LOG_INFO("Capture", "Saved rendered frame to " + ToUrsaString(absolute.u8string()));
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure("Frame capture write failed: " + ToUrsaString(error.what()));
	}
}
} // namespace Ursa::Rendering
