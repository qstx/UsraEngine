#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/TextureResource.hpp>
#include <Serialization/ResourceFile.hpp>
#include <Serialization/JsonArchiveInterop.hpp>
#include <Serialization/SerializationContext.hpp>
#include <limits>

namespace Ursa
{
namespace
{
constexpr Ursa::Size textureHeaderBytes = 40;

void EncodeUint32(Array<Ursa::UInt8, textureHeaderBytes>& bytes, Ursa::Size offset, Ursa::UInt32 value)
{
	for (Ursa::Size index = 0; index < 4; ++index)
	{
		bytes[offset + index] = static_cast<Ursa::UInt8>(value >> (index * 8));
	}
}

Ursa::UInt32 DecodeUint32(const Array<Ursa::UInt8, textureHeaderBytes>& bytes, Ursa::Size offset)
{
	return static_cast<Ursa::UInt32>(bytes[offset]) | (static_cast<Ursa::UInt32>(bytes[offset + 1]) << 8) |
	       (static_cast<Ursa::UInt32>(bytes[offset + 2]) << 16) | (static_cast<Ursa::UInt32>(bytes[offset + 3]) << 24);
}
} // namespace

Result<void> SaveTextureResource(VirtualFileSystem& filesystem, const VirtualPath& path, ResourceId id,
                                 const MaterialTexture& texture)
{
	if (!id)
	{
		return Result<void>::Failure("Cannot save texture with a zero resource ID at " + path.ToString());
	}
	if (std::filesystem::path(ToStdString(path.relative)).extension() != ".ursatex")
	{
		return Result<void>::Failure("Texture resource needs .ursatex extension: " + path.ToString());
	}
	Ursa::UInt64 pixelBytes = texture.GetPixels().size();
	for (const TextureMip& mipmap : texture.GetMipmaps())
	{
		pixelBytes += mipmap.pixels.size();
	}
	if (pixelBytes > maxTextureResourceBytes - textureHeaderBytes || texture.GetMipmaps().size() > 255)
	{
		return Result<void>::Failure("Texture resource exceeds size limit at " + path.ToString());
	}
	Array<Ursa::UInt8, textureHeaderBytes> header{};
	constexpr Array<Ursa::UInt8, 8> magic{'U', 'R', 'S', 'A', 'T', 'E', 'X', 0};
	std::copy(magic.begin(), magic.end(), header.begin());
	const MaterialSampler defaultSampler{};
	const bool extendedHeader = !texture.GetMipmaps().empty() ||
	                            texture.DefaultSampler().linear != defaultSampler.linear ||
	                            texture.DefaultSampler().repeat != defaultSampler.repeat;
	const Ursa::UInt32 formatVersion = extendedHeader ? textureResourceFormatVersion : resourceFormatVersion;
	EncodeUint32(header, 8, formatVersion);
	std::copy(id.bytes.begin(), id.bytes.end(), header.begin() + 12);
	EncodeUint32(header, 28, texture.GetWidth());
	EncodeUint32(header, 32, texture.GetHeight());
	header[36] = texture.GetColorSpace() == TextureColorSpace::Srgb ? 1 : 0;
	if (formatVersion == textureResourceFormatVersion)
	{
		header[37] = static_cast<Ursa::UInt8>(texture.GetMipmaps().size());
		header[38] = texture.DefaultSampler().linear ? 1 : 0;
		header[39] = texture.DefaultSampler().repeat ? 1 : 0;
	}
	Vector<UInt8> bytes(header.begin(), header.end());
	bytes.insert(bytes.end(), texture.GetPixels().begin(), texture.GetPixels().end());
	for (const TextureMip& mipmap : texture.GetMipmaps())
	{
		bytes.insert(bytes.end(), mipmap.pixels.begin(), mipmap.pixels.end());
	}
	return filesystem.WriteAtomic(path, bytes);
}

Result<LoadedTextureResource> LoadTextureResource(const VirtualFileSystem& filesystem, const VirtualPath& path)
{
	auto resource = ReadResourceHeader(filesystem, path);
	if (!resource)
	{
		return Result<LoadedTextureResource>::Failure(resource.error);
	}
	if (resource.value.kind != ResourceKind::Texture)
	{
		return Result<LoadedTextureResource>::Failure("Expected a texture resource at " + path.ToString());
	}

	auto data = filesystem.Read(path);
	if (!data || data.value.size() < textureHeaderBytes)
	{
		return Result<LoadedTextureResource>::Failure("Truncated texture resource at " + path.ToString());
	}
	const auto size = data.value.size();
	Array<UInt8, textureHeaderBytes> header{};
	std::copy(data.value.begin(), data.value.begin() + textureHeaderBytes, header.begin());
	const Ursa::UInt32 width = DecodeUint32(header, 28);
	const Ursa::UInt32 height = DecodeUint32(header, 32);
	if (!width || !height ||
	    static_cast<Ursa::UInt64>(width) * height > (maxTextureResourceBytes - textureHeaderBytes) / 4)
	{
		return Result<LoadedTextureResource>::Failure("Invalid texture dimensions at " + path.ToString());
	}
	const Ursa::UInt8 mipCount = resource.value.version == textureResourceFormatVersion ? header[37] : 0;
	if (resource.value.version == resourceFormatVersion && (header[37] != 0 || header[38] != 0 || header[39] != 0))
	{
		return Result<LoadedTextureResource>::Failure("Invalid legacy texture reserved bytes at " + path.ToString());
	}
	if (header[36] > 1 ||
	    (resource.value.version == textureResourceFormatVersion && (header[38] > 1 || header[39] > 1)))
	{
		return Result<LoadedTextureResource>::Failure("Invalid texture color space or reserved bytes at " +
		                                              path.ToString());
	}
	Ursa::UInt64 expectedPixelBytes = static_cast<Ursa::UInt64>(width) * height * 4;
	Ursa::UInt32 mipWidth = width;
	Ursa::UInt32 mipHeight = height;
	for (Ursa::UInt8 mip = 0; mip < mipCount; ++mip)
	{
		if (mipWidth == 1 && mipHeight == 1)
		{
			return Result<LoadedTextureResource>::Failure("Texture mip count exceeds its dimensions at " +
			                                              path.ToString());
		}
		mipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
		mipHeight = mipHeight > 1 ? mipHeight / 2 : 1;
		expectedPixelBytes += static_cast<Ursa::UInt64>(mipWidth) * mipHeight * 4;
	}
	if (size != textureHeaderBytes + expectedPixelBytes)
	{
		return Result<LoadedTextureResource>::Failure("Texture mip byte count does not match dimensions at " +
		                                              path.ToString());
	}
	Ursa::Size pixelOffset = textureHeaderBytes;
	const Ursa::Size baseBytes = static_cast<Ursa::Size>(width) * height * 4;
	Vector<UInt8> pixels(data.value.begin() + pixelOffset, data.value.begin() + pixelOffset + baseBytes);
	pixelOffset += baseBytes;
	Vector<TextureMip> mipmaps;
	mipWidth = width;
	mipHeight = height;
	for (Ursa::UInt8 mip = 0; mip < mipCount; ++mip)
	{
		mipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
		mipHeight = mipHeight > 1 ? mipHeight / 2 : 1;
		const Ursa::Size mipBytes = static_cast<Ursa::Size>(mipWidth) * mipHeight * 4;
		mipmaps.push_back(
		    {mipWidth, mipHeight,
		     Vector<UInt8>(data.value.begin() + pixelOffset, data.value.begin() + pixelOffset + mipBytes)});
		pixelOffset += mipBytes;
	}
	MaterialSampler sampler{};
	if (resource.value.version == textureResourceFormatVersion)
	{
		sampler = {header[38] != 0, header[39] != 0};
	}
	auto texture =
	    MaterialTexture::Create(width, height, header[36] ? TextureColorSpace::Srgb : TextureColorSpace::Linear,
	                            std::move(pixels), std::move(mipmaps), sampler);
	if (!texture)
	{
		return Result<LoadedTextureResource>::Failure("Invalid texture resource at " + path.ToString() + ": " +
		                                              texture.error);
	}
	return Result<LoadedTextureResource>::Success({resource.value.id, std::move(texture.value)});
}

} // namespace Ursa
