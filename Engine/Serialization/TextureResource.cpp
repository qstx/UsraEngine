#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/TextureResource.hpp>
#include <Serialization/AtomicFile.hpp>
#include <Serialization/ResourceFile.hpp>
#include <fstream>
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

Result<void> SaveTextureResource(const std::filesystem::path& path, ResourceId id, const MaterialTexture& texture)
{
	if (!id)
	{
		return Result<void>::Failure("Cannot save texture with a zero resource ID at " + PathToUrsaString(path));
	}
	if (path.extension() != ".ursatex")
	{
		return Result<void>::Failure("Texture resource needs .ursatex extension: " + PathToUrsaString(path));
	}
	if (texture.GetPixels().size() > maxTextureResourceBytes - textureHeaderBytes)
	{
		return Result<void>::Failure("Texture resource exceeds size limit at " + PathToUrsaString(path));
	}
	Array<Ursa::UInt8, textureHeaderBytes> header{};
	constexpr Array<Ursa::UInt8, 8> magic{'U', 'R', 'S', 'A', 'T', 'E', 'X', 0};
	std::copy(magic.begin(), magic.end(), header.begin());
	EncodeUint32(header, 8, resourceFormatVersion);
	std::copy(id.bytes.begin(), id.bytes.end(), header.begin() + 12);
	EncodeUint32(header, 28, texture.GetWidth());
	EncodeUint32(header, 32, texture.GetHeight());
	header[36] = texture.GetColorSpace() == TextureColorSpace::Srgb ? 1 : 0;
	return WriteResourceAtomically(path,
	                               [&](std::ostream& output)
	                               {
		                               output.write(reinterpret_cast<const char*>(header.data()), header.size());
		                               output.write(reinterpret_cast<const char*>(texture.GetPixels().data()),
		                                            static_cast<std::streamsize>(texture.GetPixels().size()));
		                               return output ? Result<void>{}
		                                             : Result<void>::Failure("Cannot write texture resource " +
		                                                                     PathToUrsaString(path));
	                               });
}

Result<LoadedTextureResource> LoadTextureResource(const std::filesystem::path& path)
{
	auto resource = ReadResourceHeader(path);
	if (!resource)
	{
		return Result<LoadedTextureResource>::Failure(resource.error);
	}
	if (resource.value.kind != ResourceKind::Texture)
	{
		return Result<LoadedTextureResource>::Failure("Expected a texture resource at " + PathToUrsaString(path));
	}

	std::error_code error;
	const auto size = std::filesystem::file_size(path, error);
	if (error || size < textureHeaderBytes)
	{
		return Result<LoadedTextureResource>::Failure("Truncated texture resource at " + PathToUrsaString(path));
	}
	std::ifstream input(path, std::ios::binary);
	Array<Ursa::UInt8, textureHeaderBytes> header{};
	if (!input.read(reinterpret_cast<char*>(header.data()), header.size()))
	{
		return Result<LoadedTextureResource>::Failure("Truncated texture header at " + PathToUrsaString(path));
	}
	const Ursa::UInt32 width = DecodeUint32(header, 28);
	const Ursa::UInt32 height = DecodeUint32(header, 32);
	if (!width || !height ||
	    static_cast<Ursa::UInt64>(width) * height > (maxTextureResourceBytes - textureHeaderBytes) / 4)
	{
		return Result<LoadedTextureResource>::Failure("Invalid texture dimensions at " + PathToUrsaString(path));
	}
	const Ursa::UInt64 expected = textureHeaderBytes + static_cast<Ursa::UInt64>(width) * height * 4;
	if (size != expected)
	{
		return Result<LoadedTextureResource>::Failure("Texture pixel byte count does not match dimensions at " +
		                                              PathToUrsaString(path));
	}
	if (header[36] > 1 || header[37] || header[38] || header[39])
	{
		return Result<LoadedTextureResource>::Failure("Invalid texture color space or reserved bytes at " +
		                                              PathToUrsaString(path));
	}
	Vector<Ursa::UInt8> pixels(static_cast<Ursa::Size>(expected - textureHeaderBytes));
	if (!input.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size())))
	{
		return Result<LoadedTextureResource>::Failure("Truncated texture pixel data at " + PathToUrsaString(path));
	}
	auto texture = MaterialTexture::Create(
	    width, height, header[36] ? TextureColorSpace::Srgb : TextureColorSpace::Linear, std::move(pixels));
	if (!texture)
	{
		return Result<LoadedTextureResource>::Failure("Invalid texture resource at " + PathToUrsaString(path) + ": " +
		                                              texture.error);
	}
	return Result<LoadedTextureResource>::Success({resource.value.id, std::move(texture.value)});
}
} // namespace Ursa
