#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Serialization/JsonInterop.hpp>
#include <Serialization/ResourceFile.hpp>
#include <Vfs/PakArchive.hpp>
#include <algorithm>
#include <cctype>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>

namespace Ursa
{
namespace
{
constexpr Ursa::Size textureHeaderBytes = 28;
constexpr Array<Ursa::UInt8, 8> textureMagic{'U', 'R', 'S', 'A', 'T', 'E', 'X', 0};
constexpr Ursa::Size shaderHeaderBytes = 40;
constexpr Array<Ursa::UInt8, 8> shaderMagic{'U', 'R', 'S', 'A', 'S', 'H', 'D', 0};

String Lowered(const String& text)
{
	String result = text;
	std::transform(result.begin(), result.end(), result.begin(),
	               [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
	return result;
}

const char* FormatName(ResourceKind kind)
{
	switch (kind)
	{
		case ResourceKind::Mesh:
			return "UrsaMesh";
		case ResourceKind::Material:
			return "UrsaMaterial";
		case ResourceKind::Scene:
			return "UrsaScene";
		case ResourceKind::Prefab:
			return "UrsaPrefab";
		case ResourceKind::Texture:
			return "UrsaTexture";
		case ResourceKind::Shader:
			return "UrsaShader";
	}
	return "";
}

Ursa::UInt32 ReadUint32(const Array<Ursa::UInt8, textureHeaderBytes>& bytes, Ursa::Size offset)
{
	return static_cast<Ursa::UInt32>(bytes[offset]) | (static_cast<Ursa::UInt32>(bytes[offset + 1]) << 8) |
	       (static_cast<Ursa::UInt32>(bytes[offset + 2]) << 16) | (static_cast<Ursa::UInt32>(bytes[offset + 3]) << 24);
}

Ursa::UInt32 ReadUint32(const Vector<Ursa::UInt8>& bytes, Ursa::Size offset)
{
	return static_cast<Ursa::UInt32>(bytes[offset]) | (static_cast<Ursa::UInt32>(bytes[offset + 1]) << 8) |
	       (static_cast<Ursa::UInt32>(bytes[offset + 2]) << 16) | (static_cast<Ursa::UInt32>(bytes[offset + 3]) << 24);
}

Ursa::UInt64 ReadUint64(const Vector<Ursa::UInt8>& bytes, Ursa::Size offset)
{
	Ursa::UInt64 value = 0;
	for (Ursa::Size index = 0; index < 8; ++index)
	{
		value |= static_cast<Ursa::UInt64>(bytes[offset + index]) << (index * 8);
	}
	return value;
}

Result<ResourceHeader> FinishHeader(ResourceHeader header, const String& where)
{
	const bool supportedVersion =
	    header.kind == ResourceKind::Material
	        ? header.version == resourceFormatVersion || header.version == materialResourceFormatVersion
	    : header.kind == ResourceKind::Texture
	        ? header.version == resourceFormatVersion || header.version == textureResourceFormatVersion
	        : header.version == resourceFormatVersion;
	if (!supportedVersion)
	{
		return Result<ResourceHeader>::Failure("Unsupported resource version " +
		                                       ToUrsaString(std::to_string(header.version)) + " at " + where);
	}
	if (!header.id)
	{
		return Result<ResourceHeader>::Failure("Zero resource ID at " + where);
	}
	return Result<ResourceHeader>::Success(header);
}

Result<ResourceHeader> ParseTextureHeader(const Vector<Ursa::UInt8>& bytes, const String& where)
{
	if (bytes.size() < textureHeaderBytes)
	{
		return Result<ResourceHeader>::Failure("Truncated texture header at " + where);
	}
	Array<Ursa::UInt8, textureHeaderBytes> header{};
	std::copy(bytes.begin(), bytes.begin() + textureHeaderBytes, header.begin());
	if (!std::equal(textureMagic.begin(), textureMagic.end(), header.begin()))
	{
		return Result<ResourceHeader>::Failure("Invalid texture magic at " + where);
	}
	ResourceHeader resource{ResourceKind::Texture};
	resource.version = ReadUint32(header, 8);
	std::copy(header.begin() + 12, header.end(), resource.id.bytes.begin());
	return FinishHeader(resource, where);
}

Result<ResourceHeader> ParseShaderHeader(const Vector<Ursa::UInt8>& bytes, Ursa::UInt64 fileSize, const String& where)
{
	if (bytes.size() < shaderHeaderBytes || fileSize < shaderHeaderBytes)
	{
		return Result<ResourceHeader>::Failure("Truncated Shader header at " + where);
	}
	if (!std::equal(shaderMagic.begin(), shaderMagic.end(), bytes.begin()))
	{
		return Result<ResourceHeader>::Failure("Invalid Shader magic at " + where);
	}
	Array<Ursa::UInt8, textureHeaderBytes> header{};
	std::copy(bytes.begin(), bytes.begin() + textureHeaderBytes, header.begin());
	ResourceHeader resource{ResourceKind::Shader};
	resource.version = ReadUint32(header, 8);
	std::copy(bytes.begin() + 12, bytes.begin() + 28, resource.id.bytes.begin());
	const Ursa::UInt64 payloadSize = ReadUint64(bytes, 28);
	if (payloadSize > maxShaderResourceBytes || fileSize != shaderHeaderBytes + payloadSize)
	{
		return Result<ResourceHeader>::Failure("Invalid Shader payload size at " + where);
	}
	return FinishHeader(resource, where);
}

Result<ResourceHeader> ParseJsonHeader(const Vector<Ursa::UInt8>& bytes, const String& where, ResourceKind kind)
{
	const std::string content(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	const auto document = nlohmann::json::parse(content, nullptr, false);
	if (document.is_discarded() || !document.is_object())
	{
		return Result<ResourceHeader>::Failure("Invalid JSON resource at " + where);
	}
	if (!document.contains("format") || !document["format"].is_string() ||
	    document["format"].get<String>() != FormatName(kind))
	{
		return Result<ResourceHeader>::Failure("Invalid resource format at " + where + " field format");
	}
	if (!document.contains("version") || !document["version"].is_number_unsigned() ||
	    document["version"].get<Ursa::UInt64>() > std::numeric_limits<Ursa::UInt32>::max())
	{
		return Result<ResourceHeader>::Failure("Invalid resource version at " + where + " field version");
	}
	ResourceHeader resource{kind};
	resource.version = document["version"].get<Ursa::UInt32>();
	if (!document.contains("id") || !document["id"].is_string())
	{
		return Result<ResourceHeader>::Failure("Invalid resource ID at " + where + " field id");
	}
	auto parsed = ResourceId::Parse(document["id"].get<String>());
	if (!parsed)
	{
		return Result<ResourceHeader>::Failure("Invalid resource ID at " + where + ": " + parsed.error);
	}
	resource.id = parsed.value;
	return FinishHeader(resource, where);
}

Ursa::UInt64 SizeLimitFor(ResourceKind kind)
{
	return kind == ResourceKind::Mesh      ? maxMeshResourceBytes
	       : kind == ResourceKind::Texture ? maxTextureResourceBytes
	       : kind == ResourceKind::Shader  ? maxShaderResourceBytes
	                                       : maxJsonResourceBytes;
}

Result<ResourceKind> KindFromExtension(const String& extension, const String& where)
{
	const String loweredExtension = Lowered(extension);
	if (loweredExtension == ".ursamesh")
	{
		return Result<ResourceKind>::Success(ResourceKind::Mesh);
	}
	if (loweredExtension == ".ursatex")
	{
		return Result<ResourceKind>::Success(ResourceKind::Texture);
	}
	if (loweredExtension == ".ursamat")
	{
		return Result<ResourceKind>::Success(ResourceKind::Material);
	}
	if (loweredExtension == ".ursascene")
	{
		return Result<ResourceKind>::Success(ResourceKind::Scene);
	}
	if (loweredExtension == ".ursaprefab")
	{
		return Result<ResourceKind>::Success(ResourceKind::Prefab);
	}
	if (loweredExtension == ".ursashaderasset")
	{
		return Result<ResourceKind>::Success(ResourceKind::Shader);
	}
	return Result<ResourceKind>::Failure("Unsupported resource file extension: " + where);
}
} // namespace

Result<ResourceKind> ResourceKindForPath(const VirtualPath& path)
{
	const auto dot = path.relative.rfind('.');
	if (dot == String::npos)
	{
		return Result<ResourceKind>::Failure("Unsupported resource file extension: " + path.ToString());
	}
	return KindFromExtension(path.relative.substr(dot), path.ToString());
}

Result<ResourceHeader> ReadResourceHeader(const VirtualFileSystem& filesystem, const VirtualPath& path)
{
	const String where = path.ToString();
	auto kind = ResourceKindForPath(path);
	if (!kind)
	{
		return Result<ResourceHeader>::Failure(kind.error);
	}
	auto size = filesystem.Size(path);
	if (!size)
	{
		return Result<ResourceHeader>::Failure(size.error);
	}
	if (size.value > SizeLimitFor(kind.value))
	{
		return Result<ResourceHeader>::Failure("Resource exceeds size limit at " + where);
	}
	if (kind.value == ResourceKind::Mesh)
	{
		auto prefix = filesystem.ReadPrefix(path, 40);
		if (!prefix)
		{
			return Result<ResourceHeader>::Failure(prefix.error);
		}
		const Array<UInt8, 8> magic{'U', 'R', 'S', 'A', 'M', 'S', 'H', 0};
		if (!prefix.value.empty() && prefix.value[0] == 'U')
		{
			if (prefix.value.size() != 40 || !std::equal(magic.begin(), magic.end(), prefix.value.begin()) ||
			    ReadUint64(prefix.value, 28) != size.value - 40 ||
			    std::any_of(prefix.value.begin() + 36, prefix.value.end(), [](UInt8 value) { return value != 0; }))
			{
				return Result<ResourceHeader>::Failure("Invalid Mesh envelope at " + where);
			}
			ResourceHeader header{ResourceKind::Mesh, ReadUint32(prefix.value, 8), {}};
			std::copy(prefix.value.begin() + 12, prefix.value.begin() + 28, header.id.bytes.begin());
			return FinishHeader(header, where);
		}
	}
	if (kind.value == ResourceKind::Texture)
	{
		auto bytes = filesystem.ReadPrefix(path, textureHeaderBytes);
		if (!bytes)
		{
			return Result<ResourceHeader>::Failure(bytes.error);
		}
		return ParseTextureHeader(bytes.value, where);
	}
	if (kind.value == ResourceKind::Shader)
	{
		auto bytes = filesystem.ReadPrefix(path, shaderHeaderBytes);
		if (!bytes)
		{
			return Result<ResourceHeader>::Failure(bytes.error);
		}
		return ParseShaderHeader(bytes.value, size.value, where);
	}
	auto bytes = filesystem.Read(path);
	if (!bytes)
	{
		return Result<ResourceHeader>::Failure(bytes.error);
	}
	return ParseJsonHeader(bytes.value, where, kind.value);
}
} // namespace Ursa
