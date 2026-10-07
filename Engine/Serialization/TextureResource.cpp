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
	if (texture.GetPixels().size() > maxTextureResourceBytes - textureHeaderBytes)
	{
		return Result<void>::Failure("Texture resource exceeds size limit at " + path.ToString());
	}
	ArchiveWriter writer;
	auto saved = texture.Serialize(writer);
	if (!saved)
	{
		return saved;
	}
	auto reader = ArchiveSnapshot(writer);
	if (!reader)
	{
		return Result<void>::Failure(reader.error);
	}
	auto width = reader.value.Unsigned("width");
	auto height = reader.value.Unsigned("height");
	auto space = reader.value.Text("colorSpace");
	auto pixels = reader.value.Bytes("pixels");
	if (!width || !height || !space || !pixels)
	{
		return Result<void>::Failure("Invalid serialized texture fields");
	}
	Array<Ursa::UInt8, textureHeaderBytes> header{};
	constexpr Array<Ursa::UInt8, 8> magic{'U', 'R', 'S', 'A', 'T', 'E', 'X', 0};
	std::copy(magic.begin(), magic.end(), header.begin());
	EncodeUint32(header, 8, resourceFormatVersion);
	std::copy(id.bytes.begin(), id.bytes.end(), header.begin() + 12);
	EncodeUint32(header, 28, UInt32(width.value));
	EncodeUint32(header, 32, UInt32(height.value));
	header[36] = space.value == "Srgb" ? 1 : 0;
	Vector<UInt8> bytes(header.begin(), header.end());
	bytes.insert(bytes.end(), pixels.value.begin(), pixels.value.end());
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
	const Ursa::UInt64 expected = textureHeaderBytes + static_cast<Ursa::UInt64>(width) * height * 4;
	if (size != expected)
	{
		return Result<LoadedTextureResource>::Failure("Texture pixel byte count does not match dimensions at " +
		                                              path.ToString());
	}
	if (header[36] > 1 || header[37] || header[38] || header[39])
	{
		return Result<LoadedTextureResource>::Failure("Invalid texture color space or reserved bytes at " +
		                                              path.ToString());
	}
	Vector<UInt8> pixels(data.value.begin() + textureHeaderBytes, data.value.end());
	ArchiveWriter writer;
	auto fields = writer.Write("width", UInt64(width));
	if (fields)
	{
		fields = writer.Write("height", UInt64(height));
	}
	if (fields)
	{
		fields = writer.Write("colorSpace", String(header[36] ? "Srgb" : "Linear"));
	}
	if (fields)
	{
		fields = writer.WriteBytes("pixels", pixels);
	}
	if (!fields)
	{
		return Result<LoadedTextureResource>::Failure(fields.error);
	}
	auto reader = ArchiveSnapshot(writer);
	if (!reader)
	{
		return Result<LoadedTextureResource>::Failure(reader.error);
	}
	auto texture = MaterialTexture::CreateForDeserialization();
	SerializationContext context;
	auto loaded = texture->Deserialize(reader.value, context);
	if (!loaded)
	{
		return Result<LoadedTextureResource>::Failure("Invalid texture resource at " + path.ToString() + ": " +
		                                              loaded.error);
	}
	return Result<LoadedTextureResource>::Success({resource.value.id, std::move(texture)});
}

} // namespace Ursa
